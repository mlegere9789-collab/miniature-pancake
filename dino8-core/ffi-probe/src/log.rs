//! The operation-log core: a safe, FFI-agnostic append-only log.
//!
//! Records live in a chunked in-memory arena (fixed 4 MiB segments, so growth never copies the
//! stored data and no append pays a large reallocation) with a paged index from sequence number
//! to location. An optional write-ahead file mirrors every record; reopening the file replays it.
//! All state sits behind one mutex; `len()` is a lock-free read of the committed count.
//!
//! Wire layout of one record (little endian), also the payload of one WAL frame:
//! `total_len u32 | kind u32 | seq u64 | ref_seq u64 | random_seed u64 | command_id u32 |
//! tool_version u32 | params_len u32 | n_inputs u16 | n_outputs u16 | params | inputs | outputs`.

use crate::status::{
    BoundaryError, D8_ERR_CLOSED, D8_ERR_CORRUPT, D8_ERR_FAILED, D8_ERR_INVALID_ARG, D8_ERR_LIMIT,
    D8_ERR_NOTHING_TO_UNDO, D8_ERR_NOT_FOUND, D8_ERR_POISONED,
};
use crate::sync::{AtomicU64, Mutex, MutexGuard, Ordering};
use crate::wal::{SyncMode, Wal};
use std::path::Path;

/// Bytes of the fixed record header.
/// cbindgen:ignore
pub const HEADER_LEN: usize = 48;
/// Bytes of one object id.
pub const D8_OBJECT_ID_LEN: usize = 16;
/// Record kind: an operation (a command that ran).
pub const D8_KIND_OP: u32 = 0;
/// Record kind: an undo marker.
pub const D8_KIND_UNDO: u32 = 1;
/// Largest parameter blob of one operation. Bulk data travels as content-addressed blobs.
pub const D8_MAX_PARAMS_LEN: usize = 1 << 20;
/// Largest number of operations in one batch append.
pub const D8_MAX_BATCH_OPS: usize = 1 << 20;
/// Largest number of input ids (and of output ids) of one operation.
pub const D8_MAX_OBJECT_IDS: usize = 65535;

/// Size of one arena segment in bytes.
/// cbindgen:ignore
const SEGMENT_SIZE: usize = 4 << 20;
/// Largest record, which must fit in one segment.
const MAX_RECORD_LEN: usize = SEGMENT_SIZE;
/// Entries per index page.
const INDEX_PAGE: usize = if cfg!(loom) { 64 } else { 8192 };
const _: () = assert!(INDEX_PAGE.is_power_of_two());

/// Stable identifier of a scene object (a UUID in the application).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct D8ObjectId {
    /// The 16 identifier bytes.
    pub bytes: [u8; D8_OBJECT_ID_LEN],
}

/// Counters describing an open log.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct D8LogInfo {
    /// Committed records (operations and undo markers); also the last sequence number.
    pub records: u64,
    /// Operations that have not been undone.
    pub live_ops: u64,
    /// Bytes held in the in-memory arena.
    pub bytes_stored: u64,
    /// Bytes in the log file, zero for an in-memory log.
    pub wal_bytes: u64,
    /// Arena segments allocated.
    pub segments: u64,
    /// Records replayed from the file at open.
    pub recovered_records: u64,
    /// Bytes cut from a torn or corrupt file tail at open.
    pub discarded_tail_bytes: u64,
}

/// Borrowed input of one append; the log copies everything it needs.
#[derive(Clone, Copy, Debug)]
pub struct OpInput<'a> {
    /// Command identifier from the command catalog.
    pub command_id: u32,
    /// Version of the tool that produced the operation.
    pub tool_version: u32,
    /// Random seed the command used, for deterministic replay.
    pub random_seed: u64,
    /// Serialized command parameters.
    pub params: &'a [u8],
    /// Objects the command read.
    pub inputs: &'a [D8ObjectId],
    /// Objects the command created or changed.
    pub outputs: &'a [D8ObjectId],
}

/// Fixed-size part of a stored record.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct RecordMeta {
    /// [`D8_KIND_OP`] or [`D8_KIND_UNDO`].
    pub kind: u32,
    /// Sequence number of this record (first record is 1).
    pub seq: u64,
    /// For an undo marker, the sequence number of the undone operation; zero otherwise.
    pub ref_seq: u64,
    /// Random seed.
    pub random_seed: u64,
    /// Command identifier.
    pub command_id: u32,
    /// Tool version.
    pub tool_version: u32,
    /// Bytes of parameters.
    pub params_len: usize,
    /// Number of input ids.
    pub n_inputs: usize,
    /// Number of output ids.
    pub n_outputs: usize,
    /// True when the variable parts were copied into the caller buffers.
    pub copied: bool,
}

/// Caller-provided destination buffers for [`Log::read`].
pub struct ReadBuffers<'a> {
    /// Receives the parameter blob.
    pub params: &'a mut [u8],
    /// Receives the input ids.
    pub inputs: &'a mut [D8ObjectId],
    /// Receives the output ids.
    pub outputs: &'a mut [D8ObjectId],
}

/// An owned, decoded copy of a stored record.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RecordCopy {
    /// Fixed-size part; `copied` is always true.
    pub meta: RecordMeta,
    /// Parameter blob.
    pub params: Vec<u8>,
    /// Input ids.
    pub inputs: Vec<D8ObjectId>,
    /// Output ids.
    pub outputs: Vec<D8ObjectId>,
}

struct Inner {
    segments: Vec<Vec<u8>>,
    index_pages: Vec<Box<[u64]>>,
    count: u64,
    live: Vec<u64>,
    bytes: u64,
    wal: Option<Wal>,
    closed: bool,
    failed: bool,
    recovered: u64,
    discarded: u64,
}

/// An append-only operation log. Share it with `Arc`; every method takes `&self`.
pub struct Log {
    inner: Mutex<Inner>,
    committed: AtomicU64,
}

fn put_u32(b: &mut [u8], off: usize, v: u32) {
    b[off..off + 4].copy_from_slice(&v.to_le_bytes());
}
fn put_u64(b: &mut [u8], off: usize, v: u64) {
    b[off..off + 8].copy_from_slice(&v.to_le_bytes());
}
fn put_u16(b: &mut [u8], off: usize, v: u16) {
    b[off..off + 2].copy_from_slice(&v.to_le_bytes());
}
fn get_u32(b: &[u8], off: usize) -> u32 {
    let mut a = [0u8; 4];
    a.copy_from_slice(&b[off..off + 4]);
    u32::from_le_bytes(a)
}
fn get_u64(b: &[u8], off: usize) -> u64 {
    let mut a = [0u8; 8];
    a.copy_from_slice(&b[off..off + 8]);
    u64::from_le_bytes(a)
}
fn get_u16(b: &[u8], off: usize) -> u16 {
    u16::from_le_bytes([b[off], b[off + 1]])
}

fn record_len(op: &OpInput<'_>) -> usize {
    HEADER_LEN + op.params.len() + D8_OBJECT_ID_LEN * (op.inputs.len() + op.outputs.len())
}

/// Checks the size limits of an input before any state is touched.
pub fn validate(op: &OpInput<'_>) -> Result<(), BoundaryError> {
    if op.params.len() > D8_MAX_PARAMS_LEN {
        return Err(BoundaryError::new(
            D8_ERR_LIMIT,
            format!(
                "parameter blob of {} bytes exceeds the limit of {D8_MAX_PARAMS_LEN}",
                op.params.len()
            ),
        ));
    }
    if op.inputs.len() > D8_MAX_OBJECT_IDS || op.outputs.len() > D8_MAX_OBJECT_IDS {
        return Err(BoundaryError::new(
            D8_ERR_LIMIT,
            format!(
                "{} input and {} output ids exceed the limit of {D8_MAX_OBJECT_IDS} each",
                op.inputs.len(),
                op.outputs.len()
            ),
        ));
    }
    if record_len(op) > MAX_RECORD_LEN {
        return Err(BoundaryError::new(
            D8_ERR_LIMIT,
            "record exceeds the segment size",
        ));
    }
    Ok(())
}

/// Parses and validates the fixed header of a stored record.
fn parse_header(rec: &[u8]) -> Result<RecordMeta, BoundaryError> {
    let bad = |m: &str| BoundaryError::new(D8_ERR_CORRUPT, m.to_string());
    if rec.len() < HEADER_LEN {
        return Err(bad("record shorter than its header"));
    }
    let total = get_u32(rec, 0) as usize;
    let kind = get_u32(rec, 4);
    let params_len = get_u32(rec, 40) as usize;
    let n_inputs = usize::from(get_u16(rec, 44));
    let n_outputs = usize::from(get_u16(rec, 46));
    if total != rec.len()
        || total != HEADER_LEN + params_len + D8_OBJECT_ID_LEN * (n_inputs + n_outputs)
    {
        return Err(bad("record length fields disagree"));
    }
    if kind != D8_KIND_OP && kind != D8_KIND_UNDO {
        return Err(bad("unknown record kind"));
    }
    Ok(RecordMeta {
        kind,
        seq: get_u64(rec, 8),
        ref_seq: get_u64(rec, 16),
        random_seed: get_u64(rec, 24),
        command_id: get_u32(rec, 32),
        tool_version: get_u32(rec, 36),
        params_len,
        n_inputs,
        n_outputs,
        copied: false,
    })
}

impl Inner {
    fn new() -> Inner {
        Inner {
            segments: Vec::new(),
            index_pages: Vec::new(),
            count: 0,
            live: Vec::new(),
            bytes: 0,
            wal: None,
            closed: false,
            failed: false,
            recovered: 0,
            discarded: 0,
        }
    }

    fn check_writable(&self) -> Result<(), BoundaryError> {
        if self.closed {
            return Err(BoundaryError::new(D8_ERR_CLOSED, "the log is closed"));
        }
        if self.failed {
            return Err(BoundaryError::new(
                D8_ERR_FAILED,
                "an earlier write failed; the log is fail-stopped",
            ));
        }
        Ok(())
    }

    fn check_open(&self) -> Result<(), BoundaryError> {
        if self.closed {
            return Err(BoundaryError::new(D8_ERR_CLOSED, "the log is closed"));
        }
        Ok(())
    }

    /// Makes room for `total` bytes at the end of the arena; returns (segment, offset).
    fn reserve_tail(&mut self, total: usize) -> (usize, usize) {
        let need_new = match self.segments.last() {
            Some(s) => s.capacity() - s.len() < total,
            None => true,
        };
        if need_new {
            self.segments.push(Vec::with_capacity(SEGMENT_SIZE));
        }
        let seg = self.segments.len() - 1;
        (seg, self.segments[seg].len())
    }

    /// Publishes the record that was just written at `(seg, off)`.
    fn commit_tail(&mut self, seg: usize, off: usize, total: usize, kind: u32, seq: u64) {
        let n = (seq - 1) as usize;
        if n & (INDEX_PAGE - 1) == 0 {
            self.index_pages
                .push(vec![0u64; INDEX_PAGE].into_boxed_slice());
        }
        self.index_pages[n / INDEX_PAGE][n % INDEX_PAGE] = ((seg as u64) << 32) | off as u64;
        self.count = seq;
        self.bytes += total as u64;
        if kind == D8_KIND_OP {
            self.live.push(seq);
        } else {
            self.live.pop();
        }
    }

    fn record(&self, seq: u64) -> Option<&[u8]> {
        if seq == 0 || seq > self.count {
            return None;
        }
        let n = (seq - 1) as usize;
        let loc = self.index_pages[n / INDEX_PAGE][n % INDEX_PAGE];
        let seg = &self.segments[(loc >> 32) as usize];
        let off = (loc & 0xFFFF_FFFF) as usize;
        let total = get_u32(seg, off) as usize;
        seg.get(off..off + total)
    }

    /// Encodes and commits one record. The caller has validated `op` and holds the lock.
    fn append_record(
        &mut self,
        kind: u32,
        ref_seq: u64,
        op: &OpInput<'_>,
    ) -> Result<u64, BoundaryError> {
        self.check_writable()?;
        let total = record_len(op);
        let seq = self.count + 1;
        let (seg_idx, off) = self.reserve_tail(total);

        let mut hdr = [0u8; HEADER_LEN];
        put_u32(&mut hdr, 0, total as u32);
        put_u32(&mut hdr, 4, kind);
        put_u64(&mut hdr, 8, seq);
        put_u64(&mut hdr, 16, ref_seq);
        put_u64(&mut hdr, 24, op.random_seed);
        put_u32(&mut hdr, 32, op.command_id);
        put_u32(&mut hdr, 36, op.tool_version);
        put_u32(&mut hdr, 40, op.params.len() as u32);
        put_u16(&mut hdr, 44, op.inputs.len() as u16);
        put_u16(&mut hdr, 46, op.outputs.len() as u16);

        let seg = &mut self.segments[seg_idx];
        seg.extend_from_slice(&hdr);
        seg.extend_from_slice(op.params);
        for id in op.inputs.iter().chain(op.outputs) {
            seg.extend_from_slice(&id.bytes);
        }
        if let Some(wal) = self.wal.as_mut() {
            if let Err(e) = wal.append_frame(&seg[off..off + total]) {
                seg.truncate(off);
                self.failed = true;
                return Err(e);
            }
        }
        self.commit_tail(seg_idx, off, total, kind, seq);
        Ok(seq)
    }

    /// Replays one stored record during recovery. Returns false when it is not acceptable.
    fn replay_record(&mut self, rec: &[u8]) -> bool {
        let Ok(meta) = parse_header(rec) else {
            return false;
        };
        if meta.seq != self.count + 1 || rec.len() > MAX_RECORD_LEN {
            return false;
        }
        if meta.kind == D8_KIND_UNDO && self.live.last() != Some(&meta.ref_seq) {
            return false;
        }
        let (seg_idx, off) = self.reserve_tail(rec.len());
        self.segments[seg_idx].extend_from_slice(rec);
        self.commit_tail(seg_idx, off, rec.len(), meta.kind, meta.seq);
        true
    }
}

impl Log {
    fn from_inner(inner: Inner) -> Log {
        let committed = AtomicU64::new(inner.count);
        Log {
            inner: Mutex::new(inner),
            committed,
        }
    }

    /// A log that lives only in memory.
    pub fn open_memory() -> Log {
        Log::from_inner(Inner::new())
    }

    /// Opens or creates a file-backed log, replaying the valid prefix of an existing file.
    pub fn open_file(path: &Path, mode: SyncMode, truncate: bool) -> Result<Log, BoundaryError> {
        let mut inner = Inner::new();
        let (wal, recovery) = Wal::open(path, mode, truncate, HEADER_LEN, MAX_RECORD_LEN, |rec| {
            inner.replay_record(rec)
        })?;
        inner.wal = Some(wal);
        inner.recovered = recovery.records;
        inner.discarded = recovery.discarded_bytes;
        Ok(Log::from_inner(inner))
    }

    fn lock(&self) -> Result<MutexGuard<'_, Inner>, BoundaryError> {
        self.inner.lock().map_err(|_| {
            BoundaryError::new(
                D8_ERR_POISONED,
                "the log is poisoned: a panic happened while it was in use",
            )
        })
    }

    /// Appends one operation and returns its sequence number.
    pub fn append(&self, op: &OpInput<'_>) -> Result<u64, BoundaryError> {
        validate(op)?;
        let mut g = self.lock()?;
        let seq = g.append_record(D8_KIND_OP, 0, op)?;
        self.committed.store(seq, Ordering::Release);
        Ok(seq)
    }

    /// Appends several operations under one lock acquisition; their sequence numbers are
    /// contiguous. Returns the first one. All inputs are validated before anything is written.
    pub fn append_batch(&self, ops: &[OpInput<'_>]) -> Result<u64, BoundaryError> {
        if ops.is_empty() {
            return Err(BoundaryError::new(
                D8_ERR_INVALID_ARG,
                "an empty batch has no first sequence number",
            ));
        }
        for op in ops {
            validate(op)?;
        }
        let mut g = self.lock()?;
        g.check_writable()?;
        let first = g.count + 1;
        for op in ops {
            let seq = g.append_record(D8_KIND_OP, 0, op)?;
            self.committed.store(seq, Ordering::Release);
        }
        Ok(first)
    }

    /// Appends an undo marker for the newest live operation. Returns (marker seq, undone seq).
    pub fn undo_marker(&self) -> Result<(u64, u64), BoundaryError> {
        let mut g = self.lock()?;
        g.check_writable()?;
        let Some(&target) = g.live.last() else {
            return Err(BoundaryError::new(
                D8_ERR_NOTHING_TO_UNDO,
                "no live operation to undo",
            ));
        };
        let empty = OpInput {
            command_id: 0,
            tool_version: 0,
            random_seed: 0,
            params: &[],
            inputs: &[],
            outputs: &[],
        };
        let seq = g.append_record(D8_KIND_UNDO, target, &empty)?;
        self.committed.store(seq, Ordering::Release);
        Ok((seq, target))
    }

    /// Copies record `seq` into the caller buffers. When a buffer is too small nothing is copied
    /// and `copied` is false; the sizes in the result say what is needed.
    pub fn read(&self, seq: u64, bufs: ReadBuffers<'_>) -> Result<RecordMeta, BoundaryError> {
        let g = self.lock()?;
        g.check_open()?;
        let Some(rec) = g.record(seq) else {
            return Err(BoundaryError::new(
                D8_ERR_NOT_FOUND,
                format!(
                    "no record with sequence number {seq} (log holds {})",
                    g.count
                ),
            )
            .with_detail(seq as i64));
        };
        let mut meta = parse_header(rec)?;
        if meta.params_len <= bufs.params.len()
            && meta.n_inputs <= bufs.inputs.len()
            && meta.n_outputs <= bufs.outputs.len()
        {
            let p_start = HEADER_LEN;
            let i_start = p_start + meta.params_len;
            let o_start = i_start + meta.n_inputs * D8_OBJECT_ID_LEN;
            bufs.params[..meta.params_len].copy_from_slice(&rec[p_start..i_start]);
            for (k, dst) in bufs.inputs[..meta.n_inputs].iter_mut().enumerate() {
                let s = i_start + k * D8_OBJECT_ID_LEN;
                dst.bytes.copy_from_slice(&rec[s..s + D8_OBJECT_ID_LEN]);
            }
            for (k, dst) in bufs.outputs[..meta.n_outputs].iter_mut().enumerate() {
                let s = o_start + k * D8_OBJECT_ID_LEN;
                dst.bytes.copy_from_slice(&rec[s..s + D8_OBJECT_ID_LEN]);
            }
            meta.copied = true;
        }
        Ok(meta)
    }

    /// Copies records `first..=last` (clamped to the log length) out under the lock. The caller
    /// can then work on them without holding any lock, for example to run callbacks.
    pub fn copy_range(&self, first: u64, last: u64) -> Result<Vec<RecordCopy>, BoundaryError> {
        if first == 0 {
            return Err(BoundaryError::new(
                D8_ERR_INVALID_ARG,
                "sequence numbers start at 1",
            ));
        }
        let g = self.lock()?;
        g.check_open()?;
        let last = last.min(g.count);
        let mut out = Vec::new();
        for seq in first..=last {
            let Some(rec) = g.record(seq) else {
                return Err(BoundaryError::new(
                    D8_ERR_CORRUPT,
                    "index points outside the arena",
                ));
            };
            let mut meta = parse_header(rec)?;
            meta.copied = true;
            let i_start = HEADER_LEN + meta.params_len;
            let o_start = i_start + meta.n_inputs * D8_OBJECT_ID_LEN;
            let ids = |start: usize, n: usize| -> Vec<D8ObjectId> {
                (0..n)
                    .map(|k| {
                        let mut id = D8ObjectId::default();
                        let s = start + k * D8_OBJECT_ID_LEN;
                        id.bytes.copy_from_slice(&rec[s..s + D8_OBJECT_ID_LEN]);
                        id
                    })
                    .collect()
            };
            out.push(RecordCopy {
                meta,
                params: rec[HEADER_LEN..i_start].to_vec(),
                inputs: ids(i_start, meta.n_inputs),
                outputs: ids(o_start, meta.n_outputs),
            });
        }
        Ok(out)
    }

    /// Number of committed records, read without taking the lock.
    pub fn len(&self) -> u64 {
        self.committed.load(Ordering::Acquire)
    }

    /// True when no record has been committed.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Counters of the log.
    pub fn info(&self) -> Result<D8LogInfo, BoundaryError> {
        let g = self.lock()?;
        Ok(D8LogInfo {
            records: g.count,
            live_ops: g.live.len() as u64,
            bytes_stored: g.bytes,
            wal_bytes: g.wal.as_ref().map_or(0, Wal::bytes),
            segments: g.segments.len() as u64,
            recovered_records: g.recovered,
            discarded_tail_bytes: g.discarded,
        })
    }

    /// Forces the log file to stable storage (a no-op for an in-memory log).
    pub fn sync(&self) -> Result<(), BoundaryError> {
        let mut g = self.lock()?;
        g.check_open()?;
        match g.wal.as_mut() {
            Some(w) => w.sync(),
            None => Ok(()),
        }
    }

    /// Flushes the file and refuses all further use. Works on a poisoned log too.
    pub fn close(&self) -> Result<(), BoundaryError> {
        let mut g = self
            .inner
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if g.closed {
            return Ok(());
        }
        g.closed = true;
        match g.wal.as_mut() {
            Some(w) => w.flush(),
            None => Ok(()),
        }
    }

    /// Probe only: panics while holding the internal lock, which poisons the log.
    pub fn probe_panic_with_lock_held(&self) {
        let _guard = self.inner.lock();
        panic!("probe panic while holding the log lock");
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn id(n: u8) -> D8ObjectId {
        let mut b = [0u8; 16];
        b[0] = n;
        b[15] = n.wrapping_mul(3);
        D8ObjectId { bytes: b }
    }

    fn op<'a>(cmd: u32, params: &'a [u8], i: &'a [D8ObjectId], o: &'a [D8ObjectId]) -> OpInput<'a> {
        OpInput {
            command_id: cmd,
            tool_version: 7,
            random_seed: 0xDEAD_BEEF_0000_0000 + u64::from(cmd),
            params,
            inputs: i,
            outputs: o,
        }
    }

    fn read_all(log: &Log, seq: u64) -> (RecordMeta, Vec<u8>, Vec<D8ObjectId>, Vec<D8ObjectId>) {
        let mut p = vec![0u8; 4096];
        let mut i = vec![D8ObjectId::default(); 64];
        let mut o = vec![D8ObjectId::default(); 64];
        let m = log
            .read(
                seq,
                ReadBuffers {
                    params: &mut p,
                    inputs: &mut i,
                    outputs: &mut o,
                },
            )
            .unwrap();
        assert!(m.copied);
        p.truncate(m.params_len);
        i.truncate(m.n_inputs);
        o.truncate(m.n_outputs);
        (m, p, i, o)
    }

    #[test]
    fn append_and_read_back_round_trip() {
        let log = Log::open_memory();
        assert!(log.is_empty());
        let params = b"extrude depth=12.5 cap=1";
        let ins = [id(1), id(2)];
        let outs = [id(9)];
        let seq = log.append(&op(42, params, &ins, &outs)).unwrap();
        assert_eq!(seq, 1);
        let (m, p, i, o) = read_all(&log, 1);
        assert_eq!(
            (m.kind, m.seq, m.command_id, m.tool_version),
            (D8_KIND_OP, 1, 42, 7)
        );
        assert_eq!(m.random_seed, 0xDEAD_BEEF_0000_002A);
        assert_eq!(p, params);
        assert_eq!(i, ins);
        assert_eq!(o, outs);
        assert_eq!(log.len(), 1);
    }

    #[test]
    fn empty_blobs_and_id_lists_are_fine() {
        let log = Log::open_memory();
        log.append(&op(1, &[], &[], &[])).unwrap();
        let (m, p, i, o) = read_all(&log, 1);
        assert_eq!((m.params_len, m.n_inputs, m.n_outputs), (0, 0, 0));
        assert!(p.is_empty() && i.is_empty() && o.is_empty());
    }

    #[test]
    fn undo_markers_follow_a_stack_discipline() {
        let log = Log::open_memory();
        assert_eq!(log.undo_marker().unwrap_err().code, D8_ERR_NOTHING_TO_UNDO);
        for c in 1..=3 {
            log.append(&op(c, b"x", &[], &[])).unwrap();
        }
        assert_eq!(log.undo_marker().unwrap(), (4, 3));
        assert_eq!(log.undo_marker().unwrap(), (5, 2));
        let (m, ..) = read_all(&log, 5);
        assert_eq!((m.kind, m.ref_seq), (D8_KIND_UNDO, 2));
        log.append(&op(9, b"y", &[], &[])).unwrap();
        assert_eq!(log.undo_marker().unwrap(), (7, 6));
        assert_eq!(log.undo_marker().unwrap(), (8, 1));
        assert_eq!(log.undo_marker().unwrap_err().code, D8_ERR_NOTHING_TO_UNDO);
        assert_eq!(log.info().unwrap().live_ops, 0);
        assert_eq!(log.len(), 8);
    }

    #[test]
    fn missing_records_and_small_buffers_are_reported() {
        let log = Log::open_memory();
        log.append(&op(1, &[7u8; 100], &[id(1)], &[id(2), id(3)]))
            .unwrap();
        let mut none: [u8; 0] = [];
        let e = log
            .read(
                2,
                ReadBuffers {
                    params: &mut none,
                    inputs: &mut [],
                    outputs: &mut [],
                },
            )
            .unwrap_err();
        assert_eq!((e.code, e.detail), (D8_ERR_NOT_FOUND, 2));
        let e = log
            .read(
                0,
                ReadBuffers {
                    params: &mut none,
                    inputs: &mut [],
                    outputs: &mut [],
                },
            )
            .unwrap_err();
        assert_eq!(e.code, D8_ERR_NOT_FOUND);
        let m = log
            .read(
                1,
                ReadBuffers {
                    params: &mut [0u8; 10],
                    inputs: &mut [D8ObjectId::default(); 4],
                    outputs: &mut [D8ObjectId::default(); 4],
                },
            )
            .unwrap();
        assert!(!m.copied);
        assert_eq!((m.params_len, m.n_inputs, m.n_outputs), (100, 1, 2));
    }

    #[test]
    fn limits_are_enforced_before_anything_is_written() {
        let log = Log::open_memory();
        let big = vec![0u8; D8_MAX_PARAMS_LEN + 1];
        assert_eq!(
            log.append(&op(1, &big, &[], &[])).unwrap_err().code,
            D8_ERR_LIMIT
        );
        let many = vec![D8ObjectId::default(); D8_MAX_OBJECT_IDS + 1];
        assert_eq!(
            log.append(&op(1, &[], &many, &[])).unwrap_err().code,
            D8_ERR_LIMIT
        );
        assert!(log.is_empty());
        let max_ok = vec![1u8; D8_MAX_PARAMS_LEN];
        let ids = vec![D8ObjectId::default(); D8_MAX_OBJECT_IDS];
        assert!(
            log.append(&op(1, &max_ok, &ids, &ids)).is_ok(),
            "largest legal record fits one segment"
        );
    }

    #[test]
    fn records_never_straddle_segments_and_stay_readable() {
        let log = Log::open_memory();
        let params = vec![0xABu8; 600_000];
        let mut seqs = Vec::new();
        for k in 0..40u32 {
            let mut p = params.clone();
            p[0] = k as u8;
            seqs.push(log.append(&op(k, &p, &[id(k as u8)], &[])).unwrap());
        }
        let info = log.info().unwrap();
        assert!(
            info.segments >= 5,
            "24 MB of records need several 4 MiB segments"
        );
        for (k, &s) in seqs.iter().enumerate() {
            let mut p = vec![0u8; 600_000];
            let mut i = [D8ObjectId::default(); 1];
            let m = log
                .read(
                    s,
                    ReadBuffers {
                        params: &mut p,
                        inputs: &mut i,
                        outputs: &mut [],
                    },
                )
                .unwrap();
            assert!(m.copied);
            assert_eq!(p[0], k as u8);
            assert_eq!(i[0], id(k as u8));
        }
    }

    #[test]
    fn index_pages_roll_over() {
        let log = Log::open_memory();
        let n = INDEX_PAGE * 2 + 3;
        for k in 0..n {
            log.append(&op(k as u32, &[1, 2, 3], &[], &[])).unwrap();
        }
        for seq in [1, INDEX_PAGE as u64, INDEX_PAGE as u64 + 1, n as u64] {
            let (m, ..) = read_all(&log, seq);
            assert_eq!(m.seq, seq);
            assert_eq!(u64::from(m.command_id), seq - 1);
        }
    }

    #[test]
    fn a_batch_gets_contiguous_sequence_numbers() {
        let log = Log::open_memory();
        log.append(&op(0, b"first", &[], &[])).unwrap();
        let ops: Vec<_> = (1..=5).map(|c| op(c, b"b", &[], &[])).collect();
        assert_eq!(log.append_batch(&ops).unwrap(), 2);
        assert_eq!(log.len(), 6);
        assert_eq!(read_all(&log, 6).0.command_id, 5);
        let mut bad = ops.clone();
        let big = vec![0u8; D8_MAX_PARAMS_LEN + 1];
        bad.push(op(9, &big, &[], &[]));
        assert_eq!(log.append_batch(&bad).unwrap_err().code, D8_ERR_LIMIT);
        assert_eq!(log.len(), 6, "an invalid batch writes nothing");
    }

    #[test]
    fn close_refuses_further_use() {
        let log = Log::open_memory();
        log.append(&op(1, b"a", &[], &[])).unwrap();
        log.close().unwrap();
        log.close().unwrap();
        assert_eq!(
            log.append(&op(2, b"b", &[], &[])).unwrap_err().code,
            D8_ERR_CLOSED
        );
        assert_eq!(log.undo_marker().unwrap_err().code, D8_ERR_CLOSED);
        assert_eq!(log.info().map(|i| i.records), Ok(1));
    }
}
