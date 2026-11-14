//! The C ABI. This is the only module with unsafe code.
//!
//! Rules every function here follows (see `ADR-boundary.md`):
//! * returns a [`D8Status`] and, when `err` is not NULL, fills the caller-owned [`D8Error`];
//! * runs its body inside `catch_unwind`, so a Rust panic becomes `D8_ERR_PANIC` and never
//!   unwinds into the caller (the functions are plain `extern "C"`, so an escaped panic would
//!   abort the process, never unwind);
//! * takes only plain C types: integers, pointers to caller memory, opaque handle ids;
//! * copies what it keeps and frees only what it allocated; the caller frees its own memory;
//! * validates every pointer (NULL, alignment) and every size before using it.
//!
//! # Safety
//!
//! Every pointer argument must be NULL where the function allows it, or valid for the access the
//! function describes, and memory a call reads or writes through those pointers must not be
//! modified by another thread while the call runs.

use crate::handle::HandleTable;
use crate::log::{D8LogInfo, D8ObjectId, Log, OpInput, ReadBuffers};
use crate::status::{
    truncate_utf8, BoundaryError, D8Error, D8Status, D8_ABI_VERSION, D8_ERROR_MESSAGE_LEN,
    D8_ERR_BAD_HANDLE, D8_ERR_BUFFER_TOO_SMALL, D8_ERR_CALLBACK, D8_ERR_INVALID_ARG,
    D8_ERR_NULL_ARG, D8_ERR_PANIC, D8_OK,
};
use crate::wal::SyncMode;
use std::any::Any;
use std::ffi::{c_char, c_void, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::Path;
use std::ptr::{self, addr_of_mut};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, PoisonError, RwLock};

/// Log file option: buffer frames, flush on `d8_log_sync`, close and when the buffer fills.
pub const D8_LOG_SYNC_BUFFERED: u32 = 0;
/// Log file option: write every append to the OS (survives a process crash).
pub const D8_LOG_SYNC_WRITE: u32 = 1;
/// Log file option: write and `fdatasync` every append (survives power failure).
pub const D8_LOG_SYNC_FSYNC: u32 = 2;
/// Log file option: start with an empty file even when one exists.
pub const D8_LOG_TRUNCATE: u32 = 4;

/// Opaque handle to an open log. Treat the id as a token: never interpret, modify or reuse it
/// after closing. Zero is never a valid handle.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct D8LogHandle {
    /// Token issued by the library.
    pub id: u64,
}

/// One operation to append. All pointers refer to caller memory that the library only reads for
/// the duration of the call; it copies what it keeps.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct D8OpRecord {
    /// Command identifier from the command catalog.
    pub command_id: u32,
    /// Version of the tool that produced the operation.
    pub tool_version: u32,
    /// Random seed the command used.
    pub random_seed: u64,
    /// Serialized parameters; may be NULL when `params_len` is zero.
    pub params: *const u8,
    /// Bytes at `params`.
    pub params_len: usize,
    /// Ids of the objects the command read; may be NULL when `n_inputs` is zero.
    pub inputs: *const D8ObjectId,
    /// Number of input ids.
    pub n_inputs: usize,
    /// Ids of the objects the command created or changed; may be NULL when `n_outputs` is zero.
    pub outputs: *const D8ObjectId,
    /// Number of output ids.
    pub n_outputs: usize,
}

/// A record read back, with caller-supplied destination buffers. Initialize the whole struct
/// (zero it), then set the buffer pointers and capacities. On `D8_OK` every field of the first
/// group is filled and the buffers hold the data. On `D8_ERR_BUFFER_TOO_SMALL` only the three
/// size fields are filled so the caller can resize and retry.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct D8OpRead {
    /// Sequence number of the record.
    pub seq: u64,
    /// For an undo marker, the sequence number of the undone operation; zero for an operation.
    pub ref_seq: u64,
    /// Random seed.
    pub random_seed: u64,
    /// 0 for an operation, 1 for an undo marker.
    pub kind: u32,
    /// Command identifier.
    pub command_id: u32,
    /// Tool version.
    pub tool_version: u32,
    /// Reserved, always zero.
    pub reserved: u32,
    /// Bytes of parameters in the record.
    pub params_len: usize,
    /// Number of input ids in the record.
    pub n_inputs: usize,
    /// Number of output ids in the record.
    pub n_outputs: usize,
    /// Destination for the parameter blob (caller memory); may be NULL when `params_cap` is zero.
    pub params_buf: *mut u8,
    /// Capacity of `params_buf` in bytes.
    pub params_cap: usize,
    /// Destination for the input ids (caller memory).
    pub inputs_buf: *mut D8ObjectId,
    /// Capacity of `inputs_buf` in ids.
    pub inputs_cap: usize,
    /// Destination for the output ids (caller memory).
    pub outputs_buf: *mut D8ObjectId,
    /// Capacity of `outputs_buf` in ids.
    pub outputs_cap: usize,
}

/// A record lent to a visitor callback. The pointers are valid only until the callback returns.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct D8OpView {
    /// Sequence number of the record.
    pub seq: u64,
    /// For an undo marker, the sequence number of the undone operation; zero for an operation.
    pub ref_seq: u64,
    /// Random seed.
    pub random_seed: u64,
    /// 0 for an operation, 1 for an undo marker.
    pub kind: u32,
    /// Command identifier.
    pub command_id: u32,
    /// Tool version.
    pub tool_version: u32,
    /// Reserved, always zero.
    pub reserved: u32,
    /// Parameter blob.
    pub params: *const u8,
    /// Bytes at `params`.
    pub params_len: usize,
    /// Input ids.
    pub inputs: *const D8ObjectId,
    /// Number of input ids.
    pub n_inputs: usize,
    /// Output ids.
    pub outputs: *const D8ObjectId,
    /// Number of output ids.
    pub n_outputs: usize,
}

/// Visitor callback for `d8_log_visit`. Return 0 to continue or any other value to stop. It must
/// not let a C++ exception escape: catch everything inside and return a non-zero value. If one
/// escapes anyway, the library aborts the process with a message (the type is declared
/// "C-unwind" so that case is defined behavior instead of undefined behavior).
pub type D8VisitFn =
    Option<unsafe extern "C-unwind" fn(user: *mut c_void, op: *const D8OpView) -> i32>;

static LOGS: RwLock<HandleTable<Log>> = RwLock::new(HandleTable::new());
static DROPS_DURING_UNWIND: AtomicU64 = AtomicU64::new(0);

fn lookup(h: D8LogHandle) -> Result<Arc<Log>, BoundaryError> {
    let table = LOGS.read().unwrap_or_else(PoisonError::into_inner);
    table
        .get(h.id)
        .ok_or_else(|| BoundaryError::new(D8_ERR_BAD_HANDLE, "invalid, stale or closed log handle"))
}

fn panic_message(payload: &(dyn Any + Send)) -> String {
    if let Some(s) = payload.downcast_ref::<&'static str>() {
        (*s).to_string()
    } else if let Some(s) = payload.downcast_ref::<String>() {
        s.clone()
    } else {
        "non-string panic payload".to_string()
    }
}

/// Writes the success state into the caller's error record, if any.
///
/// # Safety
/// `err` is NULL or points to writable memory of at least `size_of::<D8Error>()` bytes.
unsafe fn write_ok(err: *mut D8Error) {
    if err.is_null() || !err.is_aligned() {
        return;
    }
    // SAFETY: checked non-null and aligned; field-wise writes never read the old contents.
    unsafe {
        addr_of_mut!((*err).code).write(D8_OK);
        addr_of_mut!((*err).reserved).write(0);
        addr_of_mut!((*err).detail).write(0);
        addr_of_mut!((*err).message).cast::<u8>().write(0);
    }
}

/// Writes an error into the caller's error record, if any.
///
/// # Safety
/// `err` is NULL or points to writable memory of at least `size_of::<D8Error>()` bytes.
unsafe fn write_error(err: *mut D8Error, code: D8Status, detail: i64, msg: &str) {
    if err.is_null() || !err.is_aligned() {
        return;
    }
    let msg = truncate_utf8(msg, D8_ERROR_MESSAGE_LEN - 1);
    // SAFETY: checked non-null and aligned; `msg.len() < D8_ERROR_MESSAGE_LEN`, so the copy and
    // the terminating NUL stay inside `message`.
    unsafe {
        addr_of_mut!((*err).code).write(code);
        addr_of_mut!((*err).reserved).write(0);
        addr_of_mut!((*err).detail).write(detail);
        let dst = addr_of_mut!((*err).message).cast::<u8>();
        ptr::copy_nonoverlapping(msg.as_ptr(), dst, msg.len());
        dst.add(msg.len()).write(0);
    }
}

/// Runs `f` so that neither an error nor a panic escapes: both become a status code and a filled
/// error record.
///
/// # Safety
/// `err` is NULL or points to writable memory of at least `size_of::<D8Error>()` bytes.
unsafe fn guarded<F>(err: *mut D8Error, f: F) -> D8Status
where
    F: FnOnce() -> Result<(), BoundaryError>,
{
    // AssertUnwindSafe: the closure only touches raw caller pointers and an `Arc<Log>` whose
    // interior is behind a mutex; a panic while that mutex is held poisons it, and a poisoned
    // log refuses further use (D8_ERR_POISONED) instead of exposing half-updated state.
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(())) => {
            // SAFETY: forwarded from this function's contract.
            unsafe { write_ok(err) };
            D8_OK
        }
        Ok(Err(e)) => {
            // SAFETY: forwarded from this function's contract.
            unsafe { write_error(err, e.code, e.detail, &e.message) };
            e.code
        }
        Err(payload) => {
            let msg = format!(
                "panic caught at the C boundary: {}",
                panic_message(&*payload)
            );
            // SAFETY: forwarded from this function's contract.
            unsafe { write_error(err, D8_ERR_PANIC, 0, &msg) };
            D8_ERR_PANIC
        }
    }
}

fn null_arg(name: &str) -> BoundaryError {
    BoundaryError::new(D8_ERR_NULL_ARG, format!("argument `{name}` is NULL"))
}

fn misaligned(name: &str) -> BoundaryError {
    BoundaryError::new(
        D8_ERR_INVALID_ARG,
        format!("argument `{name}` is misaligned"),
    )
}

/// Checked shared reference to caller memory.
///
/// # Safety
/// A non-NULL aligned `p` must point to an initialized `T` that stays valid and unmodified for `'a`.
unsafe fn arg_ref<'a, T>(p: *const T, name: &str) -> Result<&'a T, BoundaryError> {
    if p.is_null() {
        return Err(null_arg(name));
    }
    if !p.is_aligned() {
        return Err(misaligned(name));
    }
    // SAFETY: non-null, aligned; validity is the caller's contract.
    Ok(unsafe { &*p })
}

/// Checked exclusive reference to caller memory.
///
/// # Safety
/// A non-NULL aligned `p` must point to an initialized `T` that stays valid and is not accessed by
/// anyone else for `'a`.
unsafe fn arg_mut<'a, T>(p: *mut T, name: &str) -> Result<&'a mut T, BoundaryError> {
    if p.is_null() {
        return Err(null_arg(name));
    }
    if !p.is_aligned() {
        return Err(misaligned(name));
    }
    // SAFETY: non-null, aligned; validity and exclusivity are the caller's contract.
    Ok(unsafe { &mut *p })
}

/// Checked write destination that is allowed to be NULL (the output is then skipped).
fn arg_out_opt<T>(p: *mut T, name: &str) -> Result<Option<*mut T>, BoundaryError> {
    if p.is_null() {
        return Ok(None);
    }
    if !p.is_aligned() {
        return Err(misaligned(name));
    }
    Ok(Some(p))
}

/// Checked write destination that must not be NULL.
fn arg_out<T>(p: *mut T, name: &str) -> Result<*mut T, BoundaryError> {
    arg_out_opt(p, name)?.ok_or_else(|| null_arg(name))
}

/// Checked slice over caller memory. A zero length accepts NULL.
///
/// # Safety
/// A non-NULL aligned `p` must point to `len` initialized `T` that stay valid and unmodified for `'a`.
unsafe fn arg_slice<'a, T>(p: *const T, len: usize, name: &str) -> Result<&'a [T], BoundaryError> {
    if len == 0 {
        return Ok(&[]);
    }
    if p.is_null() {
        return Err(null_arg(name));
    }
    if !p.is_aligned() {
        return Err(misaligned(name));
    }
    if len
        .checked_mul(std::mem::size_of::<T>())
        .map_or(true, |n| n > isize::MAX as usize)
    {
        return Err(BoundaryError::new(
            D8_ERR_INVALID_ARG,
            format!("`{name}` length is absurd"),
        ));
    }
    // SAFETY: non-null, aligned, length-checked; validity is the caller's contract.
    Ok(unsafe { std::slice::from_raw_parts(p, len) })
}

/// Checked mutable slice over caller memory. A zero length accepts NULL.
///
/// # Safety
/// A non-NULL aligned `p` must point to `len` writable `T` that nobody else touches for `'a`.
unsafe fn arg_slice_mut<'a, T>(
    p: *mut T,
    len: usize,
    name: &str,
) -> Result<&'a mut [T], BoundaryError> {
    if len == 0 {
        return Ok(&mut []);
    }
    if p.is_null() {
        return Err(null_arg(name));
    }
    if !p.is_aligned() {
        return Err(misaligned(name));
    }
    if len
        .checked_mul(std::mem::size_of::<T>())
        .map_or(true, |n| n > isize::MAX as usize)
    {
        return Err(BoundaryError::new(
            D8_ERR_INVALID_ARG,
            format!("`{name}` length is absurd"),
        ));
    }
    // SAFETY: non-null, aligned, length-checked; exclusivity is the caller's contract.
    Ok(unsafe { std::slice::from_raw_parts_mut(p, len) })
}

/// Converts the borrowed C description of an operation into the safe input type.
///
/// # Safety
/// The pointers inside `rec` obey the contract documented on [`D8OpRecord`].
unsafe fn op_input<'a>(rec: &'a D8OpRecord) -> Result<OpInput<'a>, BoundaryError> {
    // SAFETY: forwarded from this function's contract.
    unsafe {
        Ok(OpInput {
            command_id: rec.command_id,
            tool_version: rec.tool_version,
            random_seed: rec.random_seed,
            params: arg_slice(rec.params, rec.params_len, "op.params")?,
            inputs: arg_slice(rec.inputs, rec.n_inputs, "op.inputs")?,
            outputs: arg_slice(rec.outputs, rec.n_outputs, "op.outputs")?,
        })
    }
}

/// Version of the C ABI the library was built with (major in the high 16 bits). A caller compares
/// it with `D8_ABI_VERSION` from the header it was compiled against.
#[unsafe(no_mangle)]
pub extern "C" fn d8_abi_version() -> u32 {
    D8_ABI_VERSION
}

/// Name of a status code, for example "D8_ERR_NOT_FOUND". The string is static: never free it.
#[unsafe(no_mangle)]
pub extern "C" fn d8_status_name(code: D8Status) -> *const c_char {
    let name: &'static CStr = match code {
        crate::status::D8_OK => c"D8_OK",
        crate::status::D8_ERR_NULL_ARG => c"D8_ERR_NULL_ARG",
        crate::status::D8_ERR_INVALID_ARG => c"D8_ERR_INVALID_ARG",
        crate::status::D8_ERR_BAD_HANDLE => c"D8_ERR_BAD_HANDLE",
        crate::status::D8_ERR_NOT_FOUND => c"D8_ERR_NOT_FOUND",
        crate::status::D8_ERR_BUFFER_TOO_SMALL => c"D8_ERR_BUFFER_TOO_SMALL",
        crate::status::D8_ERR_NOTHING_TO_UNDO => c"D8_ERR_NOTHING_TO_UNDO",
        crate::status::D8_ERR_IO => c"D8_ERR_IO",
        crate::status::D8_ERR_CORRUPT => c"D8_ERR_CORRUPT",
        crate::status::D8_ERR_POISONED => c"D8_ERR_POISONED",
        crate::status::D8_ERR_PANIC => c"D8_ERR_PANIC",
        crate::status::D8_ERR_LIMIT => c"D8_ERR_LIMIT",
        crate::status::D8_ERR_CLOSED => c"D8_ERR_CLOSED",
        crate::status::D8_ERR_FAILED => c"D8_ERR_FAILED",
        crate::status::D8_ERR_CALLBACK => c"D8_ERR_CALLBACK",
        _ => c"D8_ERR_UNKNOWN",
    };
    name.as_ptr()
}

/// Opens a log. A NULL `path` makes an in-memory log; otherwise the file at `path` (UTF-8, NUL
/// terminated) is created or replayed, and its torn tail, if any, is cut. `flags` combines one
/// `D8_LOG_SYNC_*` value with optionally `D8_LOG_TRUNCATE`. On success `*out` holds the handle;
/// on failure it is zero.
///
/// # Safety
/// Pointer rules: see the preamble of this header. `path` is NULL or a NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_log_open(
    path: *const c_char,
    flags: u32,
    out: *mut D8LogHandle,
    err: *mut D8Error,
) -> D8Status {
    // SAFETY: the ABI contract makes `err` NULL or writable; the closure's pointer uses are
    // checked below and covered by the same contract.
    unsafe {
        guarded(err, || {
            let out = arg_out(out, "out")?;
            out.write(D8LogHandle { id: 0 });
            if flags & !(3 | D8_LOG_TRUNCATE) != 0 || flags & 3 == 3 {
                return Err(BoundaryError::new(
                    D8_ERR_INVALID_ARG,
                    format!("unknown log flags {flags:#x}"),
                ));
            }
            let log = if path.is_null() {
                Log::open_memory()
            } else {
                let p = CStr::from_ptr(path).to_str().map_err(|_| {
                    BoundaryError::new(D8_ERR_INVALID_ARG, "path is not valid UTF-8")
                })?;
                let mode = match flags & 3 {
                    D8_LOG_SYNC_WRITE => SyncMode::Write,
                    D8_LOG_SYNC_FSYNC => SyncMode::Fsync,
                    _ => SyncMode::Buffered,
                };
                Log::open_file(Path::new(p), mode, flags & D8_LOG_TRUNCATE != 0)?
            };
            let id = LOGS
                .write()
                .unwrap_or_else(PoisonError::into_inner)
                .insert(Arc::new(log));
            out.write(D8LogHandle { id });
            Ok(())
        })
    }
}

/// Closes a log: flushes its file, invalidates every copy of the handle and zeroes `*h`.
/// Closing a zero handle is a no-op, so a handle may be closed twice safely; a stale copy of a
/// closed handle yields `D8_ERR_BAD_HANDLE`.
///
/// # Safety
/// Pointer rules: see the preamble of this header.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_log_close(h: *mut D8LogHandle, err: *mut D8Error) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe {
        guarded(err, || {
            let h = arg_mut(h, "h")?;
            let id = std::mem::take(&mut h.id);
            if id == 0 {
                return Ok(());
            }
            let removed = LOGS
                .write()
                .unwrap_or_else(PoisonError::into_inner)
                .remove(id);
            match removed {
                Some(log) => log.close(),
                None => Err(BoundaryError::new(
                    D8_ERR_BAD_HANDLE,
                    "invalid, stale or closed log handle",
                )),
            }
        })
    }
}

/// Appends one operation. `*out_seq` (optional, may be NULL) receives its sequence number.
///
/// # Safety
/// Pointer rules: see the preamble of this header; operation layout: D8OpRecord.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_log_append(
    h: D8LogHandle,
    op: *const D8OpRecord,
    out_seq: *mut u64,
    err: *mut D8Error,
) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe {
        guarded(err, || {
            let log = lookup(h)?;
            let rec = arg_ref(op, "op")?;
            let out_seq = arg_out_opt(out_seq, "out_seq")?;
            let seq = log.append(&op_input(rec)?)?;
            if let Some(p) = out_seq {
                p.write(seq);
            }
            Ok(())
        })
    }
}

/// Appends `n` operations under one lock acquisition. Their sequence numbers are contiguous;
/// `*out_first_seq` (optional) receives the first. Nothing is written if any operation is invalid.
///
/// # Safety
/// Pointer rules: see the preamble of this header; `ops` points to `n` records.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_log_append_batch(
    h: D8LogHandle,
    ops: *const D8OpRecord,
    n: usize,
    out_first_seq: *mut u64,
    err: *mut D8Error,
) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe {
        guarded(err, || {
            let log = lookup(h)?;
            let recs = arg_slice(ops, n, "ops")?;
            let out_first = arg_out_opt(out_first_seq, "out_first_seq")?;
            let inputs = recs
                .iter()
                .map(|r| op_input(r))
                .collect::<Result<Vec<_>, _>>()?;
            let first = log.append_batch(&inputs)?;
            if let Some(p) = out_first {
                p.write(first);
            }
            Ok(())
        })
    }
}

/// Appends an undo marker for the newest operation that has not been undone. `*out_marker_seq`
/// receives the marker's sequence number and `*out_undone_seq` the undone operation's (either
/// may be NULL). Returns `D8_ERR_NOTHING_TO_UNDO` when no live operation exists.
///
/// # Safety
/// Pointer rules: see the preamble of this header.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_log_undo_marker(
    h: D8LogHandle,
    out_marker_seq: *mut u64,
    out_undone_seq: *mut u64,
    err: *mut D8Error,
) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe {
        guarded(err, || {
            let log = lookup(h)?;
            let out_marker = arg_out_opt(out_marker_seq, "out_marker_seq")?;
            let out_undone = arg_out_opt(out_undone_seq, "out_undone_seq")?;
            let (marker, undone) = log.undo_marker()?;
            if let Some(p) = out_marker {
                p.write(marker);
            }
            if let Some(p) = out_undone {
                p.write(undone);
            }
            Ok(())
        })
    }
}

/// Reads record `seq` into the buffers described by `*io`; see D8OpRead for the protocol.
///
/// # Safety
/// Pointer rules: see the preamble of this header; `io` and the buffers it points to are caller memory.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_log_read(
    h: D8LogHandle,
    seq: u64,
    io: *mut D8OpRead,
    err: *mut D8Error,
) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe {
        guarded(err, || {
            let log = lookup(h)?;
            let io = arg_mut(io, "io")?;
            let params = arg_slice_mut(io.params_buf, io.params_cap, "io.params_buf")?;
            let inputs = arg_slice_mut(io.inputs_buf, io.inputs_cap, "io.inputs_buf")?;
            let outputs = arg_slice_mut(io.outputs_buf, io.outputs_cap, "io.outputs_buf")?;
            let meta = log.read(
                seq,
                ReadBuffers {
                    params,
                    inputs,
                    outputs,
                },
            )?;
            io.params_len = meta.params_len;
            io.n_inputs = meta.n_inputs;
            io.n_outputs = meta.n_outputs;
            if !meta.copied {
                return Err(BoundaryError::new(
                    D8_ERR_BUFFER_TOO_SMALL,
                    format!(
                        "buffers too small: record needs {} parameter bytes, {} inputs, {} outputs",
                        meta.params_len, meta.n_inputs, meta.n_outputs
                    ),
                )
                .with_detail(seq as i64));
            }
            io.seq = meta.seq;
            io.ref_seq = meta.ref_seq;
            io.random_seed = meta.random_seed;
            io.kind = meta.kind;
            io.command_id = meta.command_id;
            io.tool_version = meta.tool_version;
            io.reserved = 0;
            Ok(())
        })
    }
}

/// Calls `cb(user, op)` for each record `first_seq..=last_seq` (clamped to the log length) in
/// order. The callback runs on the calling thread with no library lock held, so it may call back
/// into the library. It must not let a C++ exception escape (an escaping exception aborts the
/// process); a non-zero return stops the walk and yields `D8_ERR_CALLBACK` with the value in the
/// detail field.
///
/// # Safety
/// Pointer rules: see the preamble of this header; `cb` must be a valid function.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_log_visit(
    h: D8LogHandle,
    first_seq: u64,
    last_seq: u64,
    cb: D8VisitFn,
    user: *mut c_void,
    err: *mut D8Error,
) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe {
        guarded(err, || {
            let log = lookup(h)?;
            let cb = cb.ok_or_else(|| null_arg("cb"))?;
            if first_seq == 0 {
                return Err(BoundaryError::new(
                    D8_ERR_INVALID_ARG,
                    "sequence numbers start at 1",
                ));
            }
            let last = last_seq.min(log.len());
            let mut next = first_seq;
            while next <= last {
                let batch = log.copy_range(next, (next + 255).min(last))?;
                for rec in &batch {
                    let view = D8OpView {
                        seq: rec.meta.seq,
                        ref_seq: rec.meta.ref_seq,
                        random_seed: rec.meta.random_seed,
                        kind: rec.meta.kind,
                        command_id: rec.meta.command_id,
                        tool_version: rec.meta.tool_version,
                        reserved: 0,
                        params: rec.params.as_ptr(),
                        params_len: rec.params.len(),
                        inputs: rec.inputs.as_ptr(),
                        n_inputs: rec.inputs.len(),
                        outputs: rec.outputs.as_ptr(),
                        n_outputs: rec.outputs.len(),
                    };
                    let rc = cb(user, &view);
                    if rc != 0 {
                        return Err(BoundaryError::new(
                            D8_ERR_CALLBACK,
                            format!("visitor returned {rc} at record {}", rec.meta.seq),
                        )
                        .with_detail(i64::from(rc)));
                    }
                }
                next += batch.len() as u64;
            }
            Ok(())
        })
    }
}

/// Number of committed records, which is also the newest sequence number. A lock-free read.
///
/// # Safety
/// Pointer rules: see the preamble of this header.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_log_len(
    h: D8LogHandle,
    out_len: *mut u64,
    err: *mut D8Error,
) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe {
        guarded(err, || {
            let log = lookup(h)?;
            let out = arg_out(out_len, "out_len")?;
            out.write(log.len());
            Ok(())
        })
    }
}

/// Fills `*out` with counters of the log.
///
/// # Safety
/// Pointer rules: see the preamble of this header.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_log_info(
    h: D8LogHandle,
    out: *mut D8LogInfo,
    err: *mut D8Error,
) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe {
        guarded(err, || {
            let log = lookup(h)?;
            let out = arg_out(out, "out")?;
            out.write(log.info()?);
            Ok(())
        })
    }
}

/// Forces the log file to stable storage. A no-op for an in-memory log.
///
/// # Safety
/// Pointer rules: see the preamble of this header.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_log_sync(h: D8LogHandle, err: *mut D8Error) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe { guarded(err, || lookup(h)?.sync()) }
}

/// Probe only: a call that does nothing but pass through the full guard (panic catch and error
/// record). Measures the fixed cost of one boundary crossing.
///
/// # Safety
/// Pointer rules: see the preamble of this header.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_probe_noop(err: *mut D8Error) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe { guarded(err, || Ok(())) }
}

/// Probe only: the cheapest possible call, with no guard at all. Its cost is the floor of any
/// C to Rust call.
#[unsafe(no_mangle)]
pub extern "C" fn d8_probe_nothing(x: u64) -> u64 {
    x.wrapping_add(1)
}

struct DropProbe;

impl Drop for DropProbe {
    fn drop(&mut self) {
        DROPS_DURING_UNWIND.fetch_add(1, Ordering::SeqCst);
    }
}

/// Probe only: deliberately panics inside the guard. `mode` 0 panics with a static message, 1
/// with a formatted message, 2 with a non-string payload, 3 while holding the log's internal lock
/// (which poisons that log). Every mode returns `D8_ERR_PANIC`; the panic never reaches the
/// caller. Never call this from product code.
///
/// # Safety
/// Pointer rules: see the preamble of this header.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn d8_probe_trigger_panic(
    h: D8LogHandle,
    mode: u32,
    err: *mut D8Error,
) -> D8Status {
    // SAFETY: see `d8_log_open`.
    unsafe {
        guarded(err, || {
            let log = lookup(h)?;
            let _probe = DropProbe;
            let outcome: Result<(), BoundaryError> = match mode {
                0 => panic!("probe panic: static message"),
                1 => panic!("probe panic: formatted message, mode {mode}"),
                2 => std::panic::panic_any(42u32),
                3 => {
                    log.probe_panic_with_lock_held();
                    Ok(())
                }
                _ => Err(BoundaryError::new(D8_ERR_INVALID_ARG, "unknown panic mode")),
            };
            outcome
        })
    }
}

/// Probe only: number of destructors that ran while a probe panic unwound inside the guard.
#[unsafe(no_mangle)]
pub extern "C" fn d8_probe_drop_count() -> u64 {
    DROPS_DURING_UNWIND.load(Ordering::SeqCst)
}

/// Probe only: a plain `extern "C"` function that panics with NO guard. This is what a boundary
/// function without `catch_unwind` would do. The Rust runtime aborts the process instead of
/// unwinding into the caller. Never call this from product code.
#[unsafe(no_mangle)]
pub extern "C" fn d8_probe_unguarded_panic() {
    panic!("probe: deliberate panic in an unguarded extern C function");
}
