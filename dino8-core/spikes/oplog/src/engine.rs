//! The workspace: the operation log as a tree/DAG, the live document, and everything that moves
//! between versions (undo, redo, time travel, design options, merge, sync, recovery).
//!
//! Model in one paragraph. The log is a Merkle DAG of [`Operation`]s. Each operation has one
//! *first parent* (so the log is a tree) and merge operations add a second parent. The document
//! state after an operation is a persistent [`hamt`] whose root hash is recorded in the operation
//! (`state_after`). Every operation stores, for each object it touched, the content hash of the
//! snapshot before and after, so undo and redo are snapshot restores: O(objects touched), not
//! O(log length). Every `checkpoint_every`-th level of the tree also persists the document index
//! nodes, so jumping to an arbitrary operation costs one checkpoint load plus at most that many
//! recorded operations. Branches are named bookmarks into the tree.

use crate::crdt::{Conflict, Obj};
use crate::error::{Error, Result};
use crate::exec::{execute, join_roots, ExecInput, Registry, Rng};
use crate::hamt::{self, empty_root_hash, root_hash, Root};
use crate::ids::{Actor, Hash, HashMap, ObjectId, OpId, DOM_NODE, DOM_OP};
use crate::op::{Operation, Output, ToolVersion};
use crate::store::{blob_hash, Store};
use crate::value::{put_varint, Reader, Value};
use crate::wal::{read_at, ScanReport, SyncPolicy, Wal};
use std::collections::{BTreeMap, BTreeSet};
use std::fs::File;
use std::ops::Range;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Instant;

pub(crate) const NONE: u32 = u32::MAX;

const K_OP: u8 = 1;
const K_REF: u8 = 2;
const K_CKPT: u8 = 3;
const REF_HEAD: u8 = 0;
const REF_BRANCH_SET: u8 = 1;
const REF_BRANCH_DEL: u8 = 2;

/// Source of wall-clock time for new operations.
pub trait Clock: Send {
    fn now_ms(&mut self) -> u64;
}

pub struct SystemClock;

impl Clock for SystemClock {
    fn now_ms(&mut self) -> u64 {
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_or(0, |d| d.as_millis() as u64)
    }
}

/// Deterministic clock: `start`, `start + step`, ... (for tests and reproducible scripts).
pub struct StepClock {
    pub next: u64,
    pub step: u64,
}

impl Clock for StepClock {
    fn now_ms(&mut self) -> u64 {
        let v = self.next;
        self.next += self.step;
        v
    }
}

#[derive(Clone, Debug)]
pub struct Options {
    pub sync: SyncPolicy,
    /// Persist the document index at every operation whose depth is a multiple of this (0 = never).
    pub checkpoint_every: u32,
    pub tool: ToolVersion,
}

impl Default for Options {
    fn default() -> Self {
        Options { sync: SyncPolicy::Never, checkpoint_every: 4096, tool: ToolVersion::CURRENT }
    }
}

#[derive(Debug, Default, Clone)]
pub struct RecoveryReport {
    pub frames: u64,
    pub ops: usize,
    pub checkpoints: usize,
    pub discarded_bytes: u64,
    pub discarded_path: Option<PathBuf>,
    pub scan_ms: f64,
    pub state_ms: f64,
}

/// Result of moving the head.
#[derive(Debug, Clone, Default)]
pub struct Nav {
    pub head: OpId,
    /// Objects whose snapshot differs between the old and the new head.
    pub changed: Vec<ObjectId>,
    /// Operations walked (for path navigation) or replayed from a checkpoint.
    pub steps: usize,
    pub via_checkpoint: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MergeKind {
    /// The other head contributed nothing the current state lacks.
    AlreadyMerged,
    /// The current state was an ancestor state of the other head: the head moved there.
    FastForward,
    /// A merge operation with two parents was created.
    Merged,
}

#[derive(Debug, Clone)]
pub struct MergeOutcome {
    pub kind: MergeKind,
    pub op: Option<OpId>,
    /// Objects whose state changed through this merge.
    pub changed: usize,
    /// Conflict items among the changed objects.
    pub conflicts: Vec<Conflict>,
}

/// One operation in transit between replicas: its record and the blobs the receiver lacks.
#[derive(Debug, Clone)]
pub struct Packet {
    pub record: Vec<u8>,
    pub blobs: Vec<(Hash, Vec<u8>)>,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct IngestReport {
    pub added: usize,
    pub skipped: usize,
}

pub(crate) struct OpMeta {
    pub(crate) id: OpId,
    pub(crate) parent: u32,
    pub(crate) parent2: u32,
    pub(crate) depth: u32,
    pub(crate) lamport: u64,
    pub(crate) wall_ms: u64,
    pub(crate) state8: u64,
    off: u64,
    len: u32,
    mem: Option<Arc<[u8]>>,
    first_child: u32,
    next_sibling: u32,
}

struct Session {
    id: u64,
    epoch: u32,
    seq: u32,
    chain_ok: bool,
}

pub struct Workspace {
    pub(crate) opts: Options,
    pub(crate) registry: Registry,
    clock: Box<dyn Clock>,
    pub(crate) store: Store,
    file: Option<Arc<File>>,
    wal: Option<Wal>,
    pub(crate) ops: Vec<OpMeta>,
    pub(crate) by_id: HashMap<OpId, u32>,
    checkpoints: HashMap<u32, Hash>,
    max_epoch: BTreeMap<u64, u32>,
    pub(crate) root: Root,
    pub(crate) head: u32,
    branch: Option<String>,
    refs: BTreeMap<String, OpId>,
    session: Session,
    redo_hint: std::collections::HashMap<u32, u32>,
    seed_rng: Rng,
    scratch: Vec<u8>,
    tear: Option<usize>,
}

// ---------------------------------------------------------------------------------------------
// frame layout helpers
// ---------------------------------------------------------------------------------------------

struct OpFrame {
    local: bool,
    rec: Range<usize>,
    blobs: Vec<Range<usize>>,
}

fn parse_op_frame(p: &[u8]) -> Result<OpFrame> {
    let mut r = Reader::new(p);
    if r.u8()? != K_OP {
        return Err(Error::Corrupt("not an operation frame".into()));
    }
    let flags = r.u8()?;
    let rec_len = r.varint()? as usize;
    let s = r.pos();
    r.bytes(rec_len)?;
    let rec = s..s + rec_len;
    let n = r.varint()? as usize;
    if n > p.len() {
        return Err(Error::Corrupt("blob count".into()));
    }
    let mut blobs = Vec::with_capacity(n);
    for _ in 0..n {
        let len = r.varint()? as usize;
        let s = r.pos();
        r.bytes(len)?;
        blobs.push(s..s + len);
    }
    if !r.is_empty() {
        return Err(Error::Corrupt("trailing bytes in operation frame".into()));
    }
    Ok(OpFrame { local: flags & 1 != 0, rec, blobs })
}

/// Build an operation frame; returns the record's offset and each blob's `(offset, len)`.
fn build_op_frame(
    buf: &mut Vec<u8>,
    local: bool,
    rec: &[u8],
    blobs: &[(Hash, Vec<u8>)],
) -> (usize, Vec<(usize, usize)>) {
    buf.push(K_OP);
    buf.push(u8::from(local));
    put_varint(buf, rec.len() as u64);
    let rec_off = buf.len();
    buf.extend_from_slice(rec);
    put_varint(buf, blobs.len() as u64);
    let mut locs = Vec::with_capacity(blobs.len());
    for (_, b) in blobs {
        put_varint(buf, b.len() as u64);
        locs.push((buf.len(), b.len()));
        buf.extend_from_slice(b);
    }
    (rec_off, locs)
}

fn genesis_op(doc_id: &str) -> Operation {
    Operation {
        parents: vec![],
        session: 0,
        epoch: 0,
        seq: 0,
        lamport: 0,
        command: "genesis".into(),
        schema: "genesis/1".into(),
        params: Value::map([("doc", Value::str(doc_id))]),
        inputs: vec![],
        outputs: vec![],
        tool: ToolVersion::default(),
        seed: 0,
        wall_ms: 0,
        state_after: empty_root_hash(),
    }
}

/// Everything needed to record a merge, computed identically by `merge` and by replay.
pub(crate) struct MergeBuild {
    pub root: Root,
    pub outputs: Vec<Output>,
    pub new_blobs: Vec<(Hash, Vec<u8>)>,
    pub changed: usize,
    pub conflicts: Vec<Conflict>,
}

/// Join `first` (the first parent's state) with `second`. Outputs are relative to `first`.
pub(crate) fn build_merge(store: &Store, first: &Root, second: &Root) -> Result<MergeBuild> {
    let j = join_roots(store, first, second)?;
    let mut outputs = Vec::new();
    let mut conflicts = Vec::new();
    for (k, a, _b, joined) in &j.changed {
        if *a != Some(*joined) {
            outputs.push(Output { id: *k, before: *a, after: *joined });
        }
        let env = j.new_blobs.iter().find(|(h, _)| h == joined);
        let obj = match env {
            Some((_, e)) => Obj::from_bytes(&e[1..])?,
            None => store.get_obj(joined)?,
        };
        conflicts.extend(obj.conflicts(*k));
    }
    outputs.sort_by_key(|o| o.id);
    conflicts.sort_by(|x, y| (x.object, &x.prop, x.elem).cmp(&(y.object, &y.prop, y.elem)));
    Ok(MergeBuild { root: j.root, outputs, new_blobs: j.new_blobs, changed: j.changed.len(), conflicts })
}

pub(crate) fn merge_op(
    p0: (OpId, u64, u64),
    p1: (OpId, u64, u64),
    b: &MergeBuild,
) -> Operation {
    Operation {
        parents: vec![p0.0, p1.0],
        session: 0,
        epoch: 0,
        seq: 0,
        lamport: p0.1.max(p1.1) + 1,
        command: "merge".into(),
        schema: "merge/1".into(),
        params: Value::map([
            ("changed", Value::Int(b.changed as i64)),
            ("conflicts", Value::Int(b.conflicts.len() as i64)),
        ]),
        inputs: vec![],
        outputs: b.outputs.clone(),
        tool: ToolVersion::default(),
        seed: 0,
        wall_ms: p0.2.max(p1.2),
        state_after: root_hash(&b.root),
    }
}

impl Workspace {
    fn blank(session: u64, opts: Options) -> Workspace {
        Workspace {
            opts,
            registry: crate::commands::standard(),
            clock: Box::new(SystemClock),
            store: Store::new(None),
            file: None,
            wal: None,
            ops: Vec::new(),
            by_id: HashMap::default(),
            checkpoints: HashMap::default(),
            max_epoch: BTreeMap::new(),
            root: None,
            head: 0,
            branch: None,
            refs: BTreeMap::new(),
            session: Session { id: session, epoch: 0, seq: 0, chain_ok: false },
            redo_hint: std::collections::HashMap::new(),
            seed_rng: Rng::new(session ^ 0xD1B5_4A32_D192_ED03),
            scratch: Vec::with_capacity(4096),
            tear: None,
        }
    }

    /// A fresh document held in memory only (no durability).
    pub fn in_memory(session: u64, doc_id: &str) -> Workspace {
        let mut ws = Workspace::blank(session, Options::default());
        ws.append_op(&genesis_op(doc_id), &[], true)
            .expect("genesis append cannot fail in memory");
        ws
    }

    /// Open or create the document stored in `dir`, recovering from any torn tail.
    pub fn open(dir: &Path, session: u64, doc_id: &str, opts: Options) -> Result<(Workspace, RecoveryReport)> {
        std::fs::create_dir_all(dir)?;
        let path = dir.join("oplog.wal");
        let mut ws = Workspace::blank(session, opts.clone());
        let t0 = Instant::now();
        let (wal, scan): (Wal, ScanReport) =
            Wal::open(&path, opts.sync, &mut |off, payload| ws.apply_frame(off, payload))?;
        let scan_ms = t0.elapsed().as_secs_f64() * 1e3;
        ws.file = Some(wal.file());
        ws.store.attach_file(wal.file());
        ws.wal = Some(wal);
        let t1 = Instant::now();
        if ws.ops.is_empty() {
            ws.append_op(&genesis_op(doc_id), &[], true)?;
        } else {
            ws.root = ws.state_at(ws.head)?;
        }
        let state_ms = t1.elapsed().as_secs_f64() * 1e3;
        let report = RecoveryReport {
            frames: scan.frames,
            ops: ws.ops.len(),
            checkpoints: ws.checkpoints.len(),
            discarded_bytes: scan.discarded_bytes,
            discarded_path: scan.discarded_path,
            scan_ms,
            state_ms,
        };
        Ok((ws, report))
    }

    pub fn with_registry(mut self, r: Registry) -> Self {
        self.registry = r;
        self
    }

    pub fn with_clock(mut self, c: Box<dyn Clock>) -> Self {
        self.clock = c;
        self
    }

    /// Fault injection: the next appended operation frame is cut after `keep` bytes.
    #[doc(hidden)]
    pub fn inject_tear(&mut self, keep: usize) {
        self.tear = Some(keep);
    }

    /// Fault injection: write every frame in two halves with a pause between them.
    #[doc(hidden)]
    pub fn set_slow_write(&mut self, pause: Option<std::time::Duration>) {
        if let Some(w) = self.wal.as_mut() {
            w.set_slow_write(pause);
        }
    }

    pub fn registry(&self) -> &Registry {
        &self.registry
    }

    // -----------------------------------------------------------------------------------------
    // log access
    // -----------------------------------------------------------------------------------------

    pub fn len(&self) -> usize {
        self.ops.len()
    }

    pub fn is_empty(&self) -> bool {
        self.ops.is_empty()
    }

    pub fn head(&self) -> OpId {
        self.ops[self.head as usize].id
    }

    pub fn head_depth(&self) -> u32 {
        self.ops[self.head as usize].depth
    }

    /// Every operation id in log order (parents always precede children).
    pub fn op_ids(&self) -> Vec<OpId> {
        self.ops.iter().map(|m| m.id).collect()
    }

    pub fn genesis(&self) -> OpId {
        self.ops[0].id
    }

    pub fn state_hash(&self) -> Hash {
        root_hash(&self.root)
    }

    pub fn current_branch(&self) -> Option<&str> {
        self.branch.as_deref()
    }

    pub fn branches(&self) -> impl Iterator<Item = (&String, &OpId)> {
        self.refs.iter()
    }

    pub fn contains(&self, id: &OpId) -> bool {
        self.by_id.contains_key(id)
    }

    pub fn store(&self) -> &Store {
        &self.store
    }

    pub fn wal_len(&self) -> Option<u64> {
        self.wal.as_ref().map(Wal::len)
    }

    pub fn wal_syncs(&self) -> u64 {
        self.wal.as_ref().map_or(0, |w| w.syncs)
    }

    pub fn session_id(&self) -> u64 {
        self.session.id
    }

    pub(crate) fn idx(&self, id: OpId) -> Result<u32> {
        self.by_id.get(&id).copied().ok_or(Error::UnknownOp(id))
    }

    pub(crate) fn record_bytes(&self, i: u32) -> Result<Vec<u8>> {
        let m = &self.ops[i as usize];
        if let Some(b) = &m.mem {
            return Ok(b.to_vec());
        }
        let f = self.file.as_ref().ok_or_else(|| Error::Invalid("no backing file".into()))?;
        let mut buf = vec![0u8; m.len as usize];
        read_at(f, &mut buf, m.off)?;
        Ok(buf)
    }

    pub(crate) fn record(&self, i: u32) -> Result<Operation> {
        let m = &self.ops[i as usize];
        if let Some(b) = &m.mem {
            return Operation::decode(b);
        }
        Operation::decode(&self.record_bytes(i)?)
    }

    pub fn op(&self, id: OpId) -> Result<Operation> {
        self.record(self.idx(id)?)
    }

    pub fn parent(&self, id: OpId) -> Result<Option<OpId>> {
        let m = &self.ops[self.idx(id)? as usize];
        Ok((m.parent != NONE).then(|| self.ops[m.parent as usize].id))
    }

    pub fn depth(&self, id: OpId) -> Result<u32> {
        Ok(self.ops[self.idx(id)? as usize].depth)
    }

    /// Children of `id` in the tree, newest first.
    pub fn children(&self, id: OpId) -> Result<Vec<OpId>> {
        let mut c = self.ops[self.idx(id)? as usize].first_child;
        let mut out = Vec::new();
        while c != NONE {
            out.push(self.ops[c as usize].id);
            c = self.ops[c as usize].next_sibling;
        }
        Ok(out)
    }

    /// Tips of the tree: operations without children (the open design alternatives).
    pub fn leaves(&self) -> Vec<OpId> {
        self.ops.iter().filter(|m| m.first_child == NONE).map(|m| m.id).collect()
    }

    /// Operation ids from the root to `id` along first parents (oldest first).
    pub fn path_to(&self, id: OpId) -> Result<Vec<OpId>> {
        let mut i = self.idx(id)?;
        let mut v = Vec::new();
        while i != NONE {
            v.push(self.ops[i as usize].id);
            i = self.ops[i as usize].parent;
        }
        v.reverse();
        Ok(v)
    }

    // -----------------------------------------------------------------------------------------
    // indexing and appending
    // -----------------------------------------------------------------------------------------

    fn index_op(&mut self, id: OpId, op: &Operation, off: u64, len: u32, mem: Option<Arc<[u8]>>) -> Result<u32> {
        let idx = self.ops.len() as u32;
        let (parent, parent2, depth) = match op.parents.as_slice() {
            [] => {
                if idx != 0 {
                    return Err(Error::Corrupt("a second root operation".into()));
                }
                (NONE, NONE, 0)
            }
            [p] => {
                let pi = self.by_id.get(p).copied().ok_or(Error::MissingParent(*p))?;
                (pi, NONE, self.ops[pi as usize].depth + 1)
            }
            [p, q] => {
                let pi = self.by_id.get(p).copied().ok_or(Error::MissingParent(*p))?;
                let qi = self.by_id.get(q).copied().ok_or(Error::MissingParent(*q))?;
                (pi, qi, self.ops[pi as usize].depth + 1)
            }
            _ => return Err(Error::Corrupt("an operation with more than two parents".into())),
        };
        let first_child = NONE;
        let next_sibling = if parent == NONE { NONE } else { self.ops[parent as usize].first_child };
        self.ops.push(OpMeta {
            id,
            parent,
            parent2,
            depth,
            lamport: op.lamport,
            wall_ms: op.wall_ms,
            state8: op.state_after.prefix64(),
            off,
            len,
            mem,
            first_child,
            next_sibling,
        });
        if parent != NONE {
            self.ops[parent as usize].first_child = idx;
        }
        self.by_id.insert(id, idx);
        if op.session != 0 {
            let e = self.max_epoch.entry(op.session).or_insert(0);
            if op.epoch > *e {
                *e = op.epoch;
            }
        }
        Ok(idx)
    }

    /// Append an operation frame (write-ahead), then index it.
    fn append_op(&mut self, op: &Operation, blobs: &[(Hash, Vec<u8>)], local: bool) -> Result<u32> {
        let rec = op.to_bytes();
        let id = OpId(Hash::digest(DOM_OP, &rec));
        if self.by_id.contains_key(&id) {
            return Err(Error::Invalid(format!("operation {id} is already in the log")));
        }
        let mut buf = std::mem::take(&mut self.scratch);
        buf.clear();
        let (rec_off, blob_locs) = build_op_frame(&mut buf, local, &rec, blobs);
        if let (Some(keep), Some(w)) = (self.tear.take(), self.wal.as_mut()) {
            // fault injection: persist only a prefix of the frame, as a crash would
            w.append_partial(&buf, keep)?;
            self.scratch = buf;
            return Err(Error::Invalid("simulated crash: partial frame written".into()));
        }
        let result = if let Some(w) = self.wal.as_mut() {
            w.append(&buf).map(|base| {
                for ((h, _), (o, l)) in blobs.iter().zip(&blob_locs) {
                    self.store.note_file(*h, base + *o as u64, *l as u32);
                }
                (base + rec_off as u64, None)
            })
        } else {
            for (h, b) in blobs {
                self.store.insert_mem(*h, Arc::from(b.as_slice()));
            }
            Ok((0, Some(Arc::<[u8]>::from(rec.as_slice()))))
        };
        self.scratch = buf;
        let (off, mem) = result?;
        let idx = self.index_op(id, op, off, rec.len() as u32, mem)?;
        if local {
            self.head = idx;
            if let Some(b) = &self.branch {
                self.refs.insert(b.clone(), id);
            }
        }
        Ok(idx)
    }

    fn write_ref(&mut self, sub: u8, name: &str, target: Option<OpId>, branch: Option<&str>) -> Result<()> {
        let Some(w) = self.wal.as_mut() else { return Ok(()) };
        let mut buf = vec![K_REF, sub];
        match sub {
            REF_HEAD => {
                buf.extend_from_slice(&target.map_or([0u8; 32], |t| t.0 .0));
                match branch {
                    Some(b) => {
                        buf.push(1);
                        put_varint(&mut buf, b.len() as u64);
                        buf.extend_from_slice(b.as_bytes());
                    }
                    None => buf.push(0),
                }
            }
            _ => {
                put_varint(&mut buf, name.len() as u64);
                buf.extend_from_slice(name.as_bytes());
                if sub == REF_BRANCH_SET {
                    buf.extend_from_slice(&target.map_or([0u8; 32], |t| t.0 .0));
                }
            }
        }
        w.append(&buf)?;
        Ok(())
    }

    fn persist_head(&mut self) -> Result<()> {
        let id = self.ops[self.head as usize].id;
        let b = self.branch.clone();
        self.write_ref(REF_HEAD, "", Some(id), b.as_deref())
    }

    /// Replay one recovered frame into the in-memory index.
    fn apply_frame(&mut self, payload_off: u64, payload: &[u8]) -> Result<()> {
        match payload.first() {
            Some(&K_OP) => {
                let f = parse_op_frame(payload)?;
                for r in &f.blobs {
                    let h = blob_hash(&payload[r.clone()]);
                    self.store.note_file(h, payload_off + r.start as u64, r.len() as u32);
                }
                let rec = &payload[f.rec.clone()];
                let op = Operation::decode(rec)?;
                let id = OpId(Hash::digest(DOM_OP, rec));
                let idx = self.index_op(id, &op, payload_off + f.rec.start as u64, rec.len() as u32, None)?;
                if f.local {
                    self.head = idx;
                    if let Some(b) = &self.branch {
                        self.refs.insert(b.clone(), id);
                    }
                }
                Ok(())
            }
            Some(&K_REF) => {
                let mut r = Reader::new(payload);
                r.u8()?;
                match r.u8()? {
                    REF_HEAD => {
                        let target = OpId(Hash(r.array::<32>()?));
                        let has_branch = r.u8()?;
                        self.branch = match has_branch {
                            0 => None,
                            1 => Some(r.string()?),
                            _ => return Err(Error::Corrupt("bad branch flag".into())),
                        };
                        self.head = self.idx(target)?;
                    }
                    REF_BRANCH_SET => {
                        let name = r.string()?;
                        let target = OpId(Hash(r.array::<32>()?));
                        self.refs.insert(name, target);
                    }
                    REF_BRANCH_DEL => {
                        let name = r.string()?;
                        self.refs.remove(&name);
                    }
                    t => return Err(Error::Corrupt(format!("unknown ref frame {t}"))),
                }
                Ok(())
            }
            Some(&K_CKPT) => {
                let mut r = Reader::new(payload);
                r.u8()?;
                let op = OpId(Hash(r.array::<32>()?));
                let root = Hash(r.array::<32>()?);
                let n = r.varint()? as usize;
                if n > payload.len() {
                    return Err(Error::Corrupt("node count".into()));
                }
                for _ in 0..n {
                    let len = r.varint()? as usize;
                    let s = r.pos();
                    let bytes = r.bytes(len)?;
                    self.store.note_file(Hash::digest(DOM_NODE, bytes), payload_off + s as u64, len as u32);
                }
                if let Some(i) = self.by_id.get(&op) {
                    self.checkpoints.insert(*i, root);
                }
                Ok(())
            }
            _ => Err(Error::Corrupt("unknown frame kind".into())),
        }
    }

    fn write_checkpoint(&mut self, idx: u32) -> Result<()> {
        let mut nodes = Vec::new();
        hamt::collect_unstored(&self.root, &mut nodes);
        let root = root_hash(&self.root);
        let mut buf = vec![K_CKPT];
        buf.extend_from_slice(&self.ops[idx as usize].id.0 .0);
        buf.extend_from_slice(&root.0);
        put_varint(&mut buf, nodes.len() as u64);
        let mut locs = Vec::with_capacity(nodes.len());
        let encoded: Vec<Vec<u8>> = nodes.iter().map(|n| n.to_bytes()).collect();
        for b in &encoded {
            put_varint(&mut buf, b.len() as u64);
            locs.push((buf.len(), b.len()));
            buf.extend_from_slice(b);
        }
        if let Some(w) = self.wal.as_mut() {
            let base = w.append(&buf)?;
            for (n, (o, l)) in nodes.iter().zip(&locs) {
                self.store.note_file(n.hash, base + *o as u64, *l as u32);
            }
        } else {
            for (n, b) in nodes.iter().zip(encoded) {
                self.store.insert_mem(n.hash, Arc::from(b));
            }
        }
        hamt::mark_stored(&nodes);
        self.checkpoints.insert(idx, root);
        Ok(())
    }

    fn maybe_checkpoint(&mut self, idx: u32) -> Result<()> {
        let k = self.opts.checkpoint_every;
        if k > 0 && self.ops[idx as usize].depth % k == 0 {
            self.write_checkpoint(idx)?;
        }
        Ok(())
    }

    /// Force a checkpoint at the current head.
    pub fn checkpoint_now(&mut self) -> Result<()> {
        self.write_checkpoint(self.head)
    }

    // -----------------------------------------------------------------------------------------
    // running commands
    // -----------------------------------------------------------------------------------------

    fn new_actor(&mut self) {
        let e = self.max_epoch.get(&self.session.id).map_or(0, |e| e + 1);
        self.session.epoch = e;
        self.session.seq = 0;
        self.session.chain_ok = true;
        self.max_epoch.insert(self.session.id, e);
    }

    /// The dot the next operation will use (lets scripts predict the ids of objects it creates).
    pub fn next_dot(&self) -> (Actor, u32) {
        if self.session.chain_ok {
            (Actor { session: self.session.id, epoch: self.session.epoch }, self.session.seq + 1)
        } else {
            let e = self.max_epoch.get(&self.session.id).map_or(0, |e| e + 1);
            (Actor { session: self.session.id, epoch: e }, 1)
        }
    }

    /// Run a command through the dispatcher and append the resulting operation.
    pub fn run(&mut self, command: &str, params: Value) -> Result<OpId> {
        let seed = self.seed_rng.next_u64();
        self.run_with(command, params, seed)
    }

    /// Like [`run`](Self::run) with an explicit random seed (recorded in the operation).
    pub fn run_with(&mut self, command: &str, params: Value, seed: u64) -> Result<OpId> {
        let spec = *self
            .registry
            .get(command)
            .ok_or_else(|| Error::UnknownCommand(command.to_string()))?;
        if !self.session.chain_ok {
            self.new_actor();
        }
        let actor = Actor { session: self.session.id, epoch: self.session.epoch };
        let seq = self.session.seq + 1;
        let parent = self.head;
        let lamport = self.ops[parent as usize].lamport + 1;
        let wall_ms = self.clock.now_ms();
        let res = execute(
            &self.registry,
            &self.root,
            &self.store,
            &ExecInput { command, params: &params, actor, seq, lamport, wall_ms, seed },
        )?;
        let op = Operation {
            parents: vec![self.ops[parent as usize].id],
            session: actor.session,
            epoch: actor.epoch,
            seq,
            lamport,
            command: command.to_string(),
            schema: spec.schema.to_string(),
            params,
            inputs: res.inputs,
            outputs: res.outputs,
            tool: self.opts.tool,
            seed,
            wall_ms,
            state_after: root_hash(&res.new_root),
        };
        let idx = self.append_op(&op, &res.new_blobs, true)?;
        self.session.seq = seq;
        self.root = res.new_root;
        self.maybe_checkpoint(idx)?;
        Ok(self.ops[idx as usize].id)
    }

    // -----------------------------------------------------------------------------------------
    // state access
    // -----------------------------------------------------------------------------------------

    pub fn object(&self, id: ObjectId) -> Result<Option<Obj>> {
        match hamt::get(&self.root, &id, &self.store)? {
            Some(h) => Ok(Some(self.store.get_obj(&h)?)),
            None => Ok(None),
        }
    }

    pub fn object_hash(&self, id: ObjectId) -> Result<Option<Hash>> {
        hamt::get(&self.root, &id, &self.store)
    }

    pub fn object_count(&self) -> usize {
        hamt::root_count(&self.root)
    }

    pub fn objects(&self) -> Result<Vec<(ObjectId, Obj)>> {
        hamt::entries(&self.root, &self.store)?
            .into_iter()
            .map(|(k, h)| Ok((k, self.store.get_obj(&h)?)))
            .collect()
    }

    /// All current conflict items (a full scan of the live state).
    pub fn conflicts(&self) -> Result<Vec<Conflict>> {
        let mut out = Vec::new();
        for (id, obj) in self.objects()? {
            out.extend(obj.conflicts(id));
        }
        Ok(out)
    }

    /// Document state after operation `i`, rebuilt from the nearest checkpoint on its chain.
    pub(crate) fn state_at(&self, target: u32) -> Result<Root> {
        let mut path = Vec::new();
        let mut cur = target;
        let mut root: Root = loop {
            if let Some(h) = self.checkpoints.get(&cur) {
                break hamt::load_root(h, &self.store)?;
            }
            if cur == 0 {
                break None;
            }
            path.push(cur);
            cur = self.ops[cur as usize].parent;
        };
        for &i in path.iter().rev() {
            let rec = self.record(i)?;
            for o in &rec.outputs {
                root = hamt::insert(&root, o.id, o.after, &self.store)?;
            }
        }
        if root_hash(&root).prefix64() != self.ops[target as usize].state8 {
            return Err(Error::Mismatch(format!(
                "state rebuilt for {} does not match its recorded state hash",
                self.ops[target as usize].id
            )));
        }
        Ok(root)
    }

    /// The document state at an arbitrary operation, without moving the head.
    pub fn state_hash_at(&self, id: OpId) -> Result<Hash> {
        Ok(root_hash(&self.state_at(self.idx(id)?)?))
    }

    pub fn objects_at(&self, id: OpId) -> Result<Vec<(ObjectId, Obj)>> {
        let root = self.state_at(self.idx(id)?)?;
        hamt::entries(&root, &self.store)?
            .into_iter()
            .map(|(k, h)| Ok((k, self.store.get_obj(&h)?)))
            .collect()
    }

    // -----------------------------------------------------------------------------------------
    // undo, redo, time travel, branches
    // -----------------------------------------------------------------------------------------

    fn apply_back(&mut self, rec: &Operation) -> Result<()> {
        for o in &rec.outputs {
            self.root = match o.before {
                Some(b) => hamt::insert(&self.root, o.id, b, &self.store)?,
                None => hamt::remove(&self.root, &o.id, &self.store)?,
            };
        }
        Ok(())
    }

    fn apply_forward(&mut self, rec: &Operation) -> Result<()> {
        for o in &rec.outputs {
            self.root = hamt::insert(&self.root, o.id, o.after, &self.store)?;
        }
        Ok(())
    }

    fn arrive(&mut self, new_head: u32) -> Result<()> {
        if root_hash(&self.root).prefix64() != self.ops[new_head as usize].state8 {
            return Err(Error::Mismatch(format!(
                "restored state does not match the recorded state hash of {}",
                self.ops[new_head as usize].id
            )));
        }
        self.head = new_head;
        self.session.chain_ok = false;
        Ok(())
    }

    /// Step back to the parent operation by restoring the snapshots it replaced.
    pub fn undo(&mut self) -> Result<Option<Nav>> {
        let h = self.head;
        let p = self.ops[h as usize].parent;
        if p == NONE {
            return Ok(None);
        }
        let rec = self.record(h)?;
        self.apply_back(&rec)?;
        self.redo_hint.insert(p, h);
        self.arrive(p)?;
        self.persist_head()?;
        Ok(Some(Nav {
            head: self.ops[p as usize].id,
            changed: rec.outputs.iter().map(|o| o.id).collect(),
            steps: 1,
            via_checkpoint: false,
        }))
    }

    /// Step forward to the child we last stepped back from (else the newest child).
    pub fn redo(&mut self) -> Result<Option<Nav>> {
        let h = self.head;
        let c = match self.redo_hint.get(&h) {
            Some(c) => *c,
            None => self.ops[h as usize].first_child,
        };
        if c == NONE {
            return Ok(None);
        }
        let rec = self.record(c)?;
        self.apply_forward(&rec)?;
        self.arrive(c)?;
        self.persist_head()?;
        Ok(Some(Nav {
            head: self.ops[c as usize].id,
            changed: rec.outputs.iter().map(|o| o.id).collect(),
            steps: 1,
            via_checkpoint: false,
        }))
    }

    fn path(&self, from: u32, to: u32, limit: usize) -> Option<(Vec<u32>, Vec<u32>)> {
        let (mut a, mut b) = (from, to);
        let mut ups = Vec::new();
        let mut downs = Vec::new();
        let mut budget = limit;
        let d = |i: u32| self.ops[i as usize].depth;
        let p = |i: u32| self.ops[i as usize].parent;
        while d(a) > d(b) {
            budget = budget.checked_sub(1)?;
            ups.push(a);
            a = p(a);
        }
        while d(b) > d(a) {
            budget = budget.checked_sub(1)?;
            downs.push(b);
            b = p(b);
        }
        while a != b {
            budget = budget.checked_sub(2)?;
            ups.push(a);
            downs.push(b);
            a = p(a);
            b = p(b);
        }
        downs.reverse();
        Some((ups, downs))
    }

    /// Move the head to any operation (time travel). Short hops walk the tree restoring
    /// snapshots; long jumps rebuild from the nearest checkpoint and diff structurally.
    pub fn checkout(&mut self, target: OpId) -> Result<Nav> {
        let t = self.idx(target)?;
        self.checkout_idx(t, None)
    }

    /// `branch` is the design option the head belongs to afterwards (None = detached).
    fn checkout_idx(&mut self, t: u32, branch: Option<&str>) -> Result<Nav> {
        const WALK_LIMIT: usize = 256;
        if t == self.head {
            if self.branch.as_deref() != branch {
                self.branch = branch.map(str::to_string);
                self.persist_head()?;
            }
            return Ok(Nav { head: self.ops[t as usize].id, ..Nav::default() });
        }
        let mut changed = BTreeSet::new();
        let (steps, via_checkpoint) = if let Some((ups, downs)) = self.path(self.head, t, WALK_LIMIT) {
            for &u in &ups {
                let rec = self.record(u)?;
                self.apply_back(&rec)?;
                changed.extend(rec.outputs.iter().map(|o| o.id));
            }
            for &d in &downs {
                let rec = self.record(d)?;
                self.apply_forward(&rec)?;
                changed.extend(rec.outputs.iter().map(|o| o.id));
            }
            (ups.len() + downs.len(), false)
        } else {
            let new_root = self.state_at(t)?;
            hamt::diff(&self.root, &new_root, &self.store, &mut |k, _, _| {
                changed.insert(*k);
            })?;
            self.root = new_root;
            (self.ops[t as usize].depth as usize % self.opts.checkpoint_every.max(1) as usize, true)
        };
        self.arrive(t)?;
        self.branch = branch.map(str::to_string);
        self.persist_head()?;
        Ok(Nav { head: self.ops[t as usize].id, changed: changed.into_iter().collect(), steps, via_checkpoint })
    }

    /// Create (or move) the named design option at the current head and switch to it.
    pub fn branch(&mut self, name: &str) -> Result<()> {
        let id = self.ops[self.head as usize].id;
        self.refs.insert(name.to_string(), id);
        self.write_ref(REF_BRANCH_SET, name, Some(id), None)?;
        self.branch = Some(name.to_string());
        self.persist_head()
    }

    pub fn delete_branch(&mut self, name: &str) -> Result<bool> {
        let existed = self.refs.remove(name).is_some();
        if existed {
            self.write_ref(REF_BRANCH_DEL, name, None, None)?;
            if self.branch.as_deref() == Some(name) {
                self.branch = None;
                self.persist_head()?;
            }
        }
        Ok(existed)
    }

    /// Switch to a named design option.
    pub fn switch(&mut self, name: &str) -> Result<Nav> {
        let target = *self
            .refs
            .get(name)
            .ok_or_else(|| Error::Invalid(format!("no branch named `{name}`")))?;
        let t = self.idx(target)?;
        self.checkout_idx(t, Some(name))
    }

    /// Restore named design options and the current head after importing a log.
    pub fn restore_refs(&mut self, refs: &[(String, OpId)], head: OpId, branch: Option<&str>) -> Result<()> {
        for (n, t) in refs {
            self.idx(*t)?;
            self.refs.insert(n.clone(), *t);
            self.write_ref(REF_BRANCH_SET, n, Some(*t), None)?;
        }
        let h = self.idx(head)?;
        self.checkout_idx(h, branch)?;
        Ok(())
    }

    /// Objects that differ between two operations' states (structural diff).
    pub fn diff_ops(&self, a: OpId, b: OpId) -> Result<Vec<DiffEntry>> {
        let ra = self.state_at(self.idx(a)?)?;
        let rb = self.state_at(self.idx(b)?)?;
        let mut out = Vec::new();
        hamt::diff(&ra, &rb, &self.store, &mut |k, x, y| out.push((*k, x, y)))?;
        out.sort_by_key(|e| e.0);
        Ok(out)
    }

    // -----------------------------------------------------------------------------------------
    // merge and sync
    // -----------------------------------------------------------------------------------------

    /// Merge another head into the current state. The result is the join of the two document
    /// states; the merge operation itself is canonical (parents sorted by id, system author, no
    /// wall time of its own), so two replicas that merge the same pair create the *same* operation.
    pub fn merge(&mut self, other: OpId) -> Result<MergeOutcome> {
        let o = self.idx(other)?;
        let nothing = |kind| MergeOutcome { kind, op: None, changed: 0, conflicts: vec![] };
        if o == self.head {
            return Ok(nothing(MergeKind::AlreadyMerged));
        }
        let other_root = self.state_at(o)?;
        let cur_hash = root_hash(&self.root);
        let other_hash = root_hash(&other_root);
        // decide the first parent canonically so both sides compute identical outputs
        let cur_id = self.ops[self.head as usize].id;
        let (first_is_cur, first_root, second_root) = if cur_id <= other {
            (true, &self.root, &other_root)
        } else {
            (false, &other_root, &self.root)
        };
        let b = build_merge(&self.store, first_root, second_root)?;
        let joined_hash = root_hash(&b.root);
        if joined_hash == cur_hash {
            return Ok(nothing(MergeKind::AlreadyMerged));
        }
        if joined_hash == other_hash {
            let keep = self.branch.clone();
            let nav = self.checkout_idx(o, keep.as_deref())?;
            return Ok(MergeOutcome {
                kind: MergeKind::FastForward,
                op: None,
                changed: nav.changed.len(),
                conflicts: vec![],
            });
        }
        let cur = self.head;
        let meta = |i: u32| (self.ops[i as usize].id, self.ops[i as usize].lamport, self.ops[i as usize].wall_ms);
        let (p0, p1) = if first_is_cur { (meta(cur), meta(o)) } else { (meta(o), meta(cur)) };
        let op = merge_op(p0, p1, &b);
        let blobs: Vec<(Hash, Vec<u8>)> =
            b.new_blobs.iter().filter(|(h, _)| !self.store.contains(h)).cloned().collect();
        let idx = self.append_op(&op, &blobs, true)?;
        self.root = b.root;
        self.session.chain_ok = false;
        self.maybe_checkpoint(idx)?;
        Ok(MergeOutcome {
            kind: MergeKind::Merged,
            op: Some(self.ops[idx as usize].id),
            changed: b.changed,
            conflicts: b.conflicts,
        })
    }

    /// Package operation `idx` for another replica; `have` says which blobs it already holds.
    pub fn export_op(&self, idx: u32, have: &dyn Fn(&Hash) -> bool) -> Result<Packet> {
        let record = self.record_bytes(idx)?;
        let op = Operation::decode(&record)?;
        let mut blobs: Vec<(Hash, Vec<u8>)> = Vec::new();
        let mut seen: BTreeSet<Hash> = BTreeSet::new();
        for o in &op.outputs {
            if !have(&o.after) && seen.insert(o.after) {
                let b = self.store.get(&o.after)?;
                let obj = Obj::from_bytes(&b[1..])?;
                blobs.push((o.after, b.to_vec()));
                let mut refs = Vec::new();
                hash_refs(&obj.to_value(), &mut refs);
                for g in refs {
                    if !have(&g) && seen.insert(g) {
                        blobs.push((g, self.store.get(&g)?.to_vec()));
                    }
                }
            }
        }
        Ok(Packet { record, blobs })
    }

    /// Add operations received from another replica to this log (parents first).
    pub fn ingest(&mut self, packets: &[Packet]) -> Result<IngestReport> {
        let mut rep = IngestReport::default();
        for p in packets {
            let id = OpId(Hash::digest(DOM_OP, &p.record));
            if self.by_id.contains_key(&id) {
                rep.skipped += 1;
                continue;
            }
            let op = Operation::decode(&p.record)?;
            let mut fresh: Vec<(Hash, Vec<u8>)> = Vec::new();
            for (h, bytes) in &p.blobs {
                if blob_hash(bytes) != *h {
                    return Err(Error::Mismatch(format!("blob {h} does not hash to its name")));
                }
                if !self.store.contains(h) && !fresh.iter().any(|(x, _)| x == h) {
                    fresh.push((*h, bytes.clone()));
                }
            }
            for o in &op.outputs {
                if !self.store.contains(&o.after) && !fresh.iter().any(|(h, _)| *h == o.after) {
                    return Err(Error::MissingBlob(o.after));
                }
            }
            self.append_op(&op, &fresh, false)?;
            rep.added += 1;
        }
        Ok(rep)
    }

    /// Fetch everything `other` has that this log lacks.
    pub fn pull_from(&mut self, other: &Workspace) -> Result<IngestReport> {
        let mut packets = Vec::new();
        for i in 0..other.ops.len() {
            if !self.by_id.contains_key(&other.ops[i].id) {
                packets.push(other.export_op(i as u32, &|h| self.store.contains(h))?);
            }
        }
        self.ingest(&packets)
    }

    // -----------------------------------------------------------------------------------------
    // integrity
    // -----------------------------------------------------------------------------------------

    /// Full integrity pass: every record hashes to its id, parents precede children, Lamport
    /// clocks are consistent, every blob hashes to its name, and the live state matches the log.
    pub fn verify(&self) -> Result<VerifyReport> {
        let mut rep = VerifyReport::default();
        for i in 0..self.ops.len() {
            let m = &self.ops[i];
            let bytes = self.record_bytes(i as u32)?;
            if OpId(Hash::digest(DOM_OP, &bytes)) != m.id {
                return Err(Error::Corrupt(format!("record {i} does not hash to its id")));
            }
            let op = Operation::decode(&bytes)?;
            let want = op.parents.iter().filter_map(|p| self.by_id.get(p)).map(|&j| self.ops[j as usize].lamport).max();
            if want.map_or(0, |l| l + 1) != op.lamport {
                return Err(Error::Corrupt(format!("operation {i} has an inconsistent lamport clock")));
            }
            for o in &op.outputs {
                if !self.store.contains(&o.after) {
                    return Err(Error::MissingBlob(o.after));
                }
            }
            rep.ops += 1;
        }
        rep.blobs = self.store.verify_all()?;
        let root = self.state_at(self.head)?;
        if root_hash(&root) != root_hash(&self.root) {
            return Err(Error::Mismatch("live state differs from the state rebuilt from the log".into()));
        }
        rep.head_state = root_hash(&root);
        Ok(rep)
    }
}

/// One differing object between two document states: `(id, hash in a, hash in b)`.
pub type DiffEntry = (ObjectId, Option<Hash>, Option<Hash>);

#[derive(Debug, Default, Clone)]
pub struct VerifyReport {
    pub ops: usize,
    pub blobs: usize,
    pub head_state: Hash,
}

/// Collect every blob reference inside a value tree.
pub(crate) fn hash_refs(v: &Value, out: &mut Vec<Hash>) {
    match v {
        Value::Hash(h) => out.push(*h),
        Value::List(l) => l.iter().for_each(|x| hash_refs(x, out)),
        Value::Map(m) => m.values().for_each(|x| hash_refs(x, out)),
        _ => {}
    }
}
