//! C ABI sketch: registering kernel compute functions as queries.
//!
//! A kernel function is a plain C function pointer plus a `user` cookie.  Values cross the
//! boundary as opaque, immutable, content-hashed blobs: the engine owns every blob and the
//! kernel sees them only as read-only `(pointer, length, hash)` views that are valid for the
//! duration of one call; results are handed back through an output builder that the engine
//! allocates.  The kernel never calls back into the engine (no re-entrancy), it only declares
//! its dependencies (`deps` callback, a cheap planning phase) and then computes once with every
//! input resolved.  Each side frees only what it allocated; failures are status codes plus an
//! error record; Rust panics are caught at the boundary and never unwind into C or C++.
//!
//! The full header is reproduced in RESULTS.md.  This module is a working subset used by the
//! tests to prove that the protocol is implementable on the engine.

#![allow(unsafe_code)]

use crate::engine::{CancelProbe, CancelToken, Config, Db, EvalError, EvalOptions, Interrupt, Kind, NodeId, Priority};
use crate::hash::{ContentHash, ContentHasher, Hash128};
use std::ffi::{c_char, c_void, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Arc, Mutex};

pub const D8G_ABI_VERSION: u32 = 1;
pub const D8G_OK: i32 = 0;
/// Returned by a `deps` callback that pushed more dependencies and wants to be called again once
/// they are resolved.
pub const D8G_NEED_DEPS: i32 = 1;
pub const D8G_ERR_CANCELLED: i32 = -1;
pub const D8G_ERR_KERNEL: i32 = -2;
pub const D8G_ERR_PANIC: i32 = -3;
pub const D8G_ERR_BAD_ARG: i32 = -4;
pub const D8G_ERR_CYCLE: i32 = -5;
pub const D8G_ERR_MISSING_INPUT: i32 = -6;
pub const D8G_ERR_STALLED: i32 = -7;

/// The kernel may be called concurrently from many threads (it has been audited).
pub const D8G_THREAD_SAFE: u32 = 1;
/// The engine serializes every call into this kernel with one mutex shared by all serialized kernels.
pub const D8G_SERIALIZED: u32 = 2;

/// Immutable value of the ABI database: bytes plus a content hash (supplied by the kernel or
/// computed from the bytes).
#[derive(Clone)]
pub struct Blob {
    hash: Hash128,
    bytes: Arc<[u8]>,
}

impl Blob {
    pub fn new(bytes: &[u8]) -> Blob {
        let mut h = ContentHasher::new();
        h.write_bytes(bytes);
        Blob { hash: h.finish(), bytes: bytes.into() }
    }

    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }

    pub fn hash(&self) -> Hash128 {
        self.hash
    }
}

impl ContentHash for Blob {
    fn content_hash(&self, h: &mut ContentHasher) {
        self.hash.content_hash(h);
    }
}

/// Read-only view of an input blob, valid for the duration of the call it is passed to.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct D8gBlobRef {
    pub data: *const u8,
    pub len: usize,
    pub hash_hi: u64,
    pub hash_lo: u64,
}

/// Error record filled on failure (caller allocated).
#[repr(C)]
pub struct D8gError {
    pub code: i32,
    pub message: [c_char; 256],
}

/// Opaque database handle.
pub struct D8gDb {
    db: Db<Blob>,
    kernel_lock: Arc<Mutex<()>>,
}

/// Opaque output builder (engine allocated; the kernel appends bytes and may set the hash).
pub struct D8gOut {
    bytes: Vec<u8>,
    hash: Option<Hash128>,
}

/// Opaque dependency list under construction.
pub struct D8gDeps {
    items: Vec<(u32, u64)>,
}

/// Opaque, call-scoped view of the evaluation's cancellation state handed to kernels
/// (poll with [`d8g_cancel_requested`]).
pub struct D8gCancel {
    probe: CancelProbe,
}

/// Opaque host-side cancellation token ([`d8g_cancel_token_new`]).
pub struct D8gCancelToken {
    token: CancelToken,
}

/// Kernel compute function.  Must be pure and deterministic; may be called concurrently when the
/// kernel is registered with `D8G_THREAD_SAFE`; must not unwind (C++: catch everything and return
/// a status).
pub type D8gComputeFn =
    unsafe extern "C" fn(user: *mut c_void, arg: u64, inputs: *const D8gBlobRef, n_inputs: u32, out: *mut D8gOut, cancel: *const D8gCancel) -> i32;

/// Dependency planning callback: pushes the `(kind, arg)` pairs the kernel needs.  `inputs` holds
/// the dependencies resolved so far, in request order.  Returns `D8G_OK` when the list is complete
/// or `D8G_NEED_DEPS` after pushing more.  Cheap, pure, thread-safe.
pub type D8gDepsFn = unsafe extern "C" fn(user: *mut c_void, arg: u64, inputs: *const D8gBlobRef, n_inputs: u32, deps: *mut D8gDeps) -> i32;

#[repr(C)]
pub struct D8gKernelDesc {
    pub abi_version: u32,
    pub flags: u32,
    pub name: *const c_char,
    pub compute: Option<D8gComputeFn>,
    pub deps: Option<D8gDepsFn>,
    pub user: *mut c_void,
    /// Arguments are `0..capacity` (dense table); 0 selects a sparse table.
    pub capacity: u32,
}

struct UserPtr(*mut c_void);
// SAFETY: the registering side promises (via D8G_THREAD_SAFE or D8G_SERIALIZED) that calls with
// this cookie are safe from any thread; the engine never dereferences it.
unsafe impl Send for UserPtr {}
unsafe impl Sync for UserPtr {}

impl UserPtr {
    // A method (not field access) so that closures capture the whole wrapper, not the raw pointer.
    fn get(&self) -> *mut c_void {
        self.0
    }
}

fn fill_error(err: *mut D8gError, code: i32, msg: &str) {
    if err.is_null() {
        return;
    }
    // SAFETY: the caller passed a valid, exclusively owned error record.
    let e = unsafe { &mut *err };
    e.code = code;
    e.message = [0; 256];
    for (dst, src) in e.message.iter_mut().zip(msg.bytes().take(255)) {
        *dst = src as c_char;
    }
}

fn status_of(e: &EvalError) -> i32 {
    match e {
        EvalError::Cancelled => D8G_ERR_CANCELLED,
        EvalError::Panic { message, .. } if message.starts_with("[kernel]") => D8G_ERR_KERNEL,
        EvalError::Panic { .. } => D8G_ERR_PANIC,
        EvalError::Cycle(_) => D8G_ERR_CYCLE,
        EvalError::MissingInput(_) => D8G_ERR_MISSING_INPUT,
        EvalError::Stalled(_) => D8G_ERR_STALLED,
    }
}

fn guard<F: FnOnce() -> i32>(err: *mut D8gError, f: F) -> i32 {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(code) => code,
        Err(_) => {
            fill_error(err, D8G_ERR_PANIC, "panic caught at the C boundary");
            D8G_ERR_PANIC
        }
    }
}

fn refs_of(blobs: &[Blob]) -> Vec<D8gBlobRef> {
    blobs
        .iter()
        .map(|b| D8gBlobRef { data: b.bytes.as_ptr(), len: b.bytes.len(), hash_hi: b.hash.hi, hash_lo: b.hash.lo })
        .collect()
}

// ---- functions the kernel calls ---------------------------------------------------------------------

/// Appends bytes to the output.
///
/// # Safety
/// `out` must be the pointer passed to the compute callback; `data` must be valid for `len` bytes.
pub unsafe extern "C" fn d8g_out_write(out: *mut D8gOut, data: *const u8, len: usize) -> i32 {
    if out.is_null() || (data.is_null() && len > 0) {
        return D8G_ERR_BAD_ARG;
    }
    // SAFETY: guaranteed by the caller contract above.
    let (o, s) = unsafe { (&mut *out, if len == 0 { &[][..] } else { std::slice::from_raw_parts(data, len) }) };
    o.bytes.extend_from_slice(s);
    D8G_OK
}

/// Supplies the content hash of the output (default: the hash of its bytes).  Kernels whose result
/// is an opaque handle must call this with a canonical hash of the result.
///
/// # Safety
/// `out` must be the pointer passed to the compute callback.
pub unsafe extern "C" fn d8g_out_set_hash(out: *mut D8gOut, hi: u64, lo: u64) -> i32 {
    if out.is_null() {
        return D8G_ERR_BAD_ARG;
    }
    // SAFETY: guaranteed by the caller contract above.
    unsafe { (*out).hash = Some(Hash128 { hi, lo }) };
    D8G_OK
}

/// Adds a dependency request.
///
/// # Safety
/// `deps` must be the pointer passed to the deps callback.
pub unsafe extern "C" fn d8g_deps_push(deps: *mut D8gDeps, kind: u32, arg: u64) -> i32 {
    if deps.is_null() {
        return D8G_ERR_BAD_ARG;
    }
    // SAFETY: guaranteed by the caller contract above.
    unsafe { (*deps).items.push((kind, arg)) };
    D8G_OK
}

/// Cooperative cancellation poll: nonzero when the kernel should return `D8G_ERR_CANCELLED` soon.
///
/// # Safety
/// `cancel` must be the pointer passed to the compute callback (or null).
pub unsafe extern "C" fn d8g_cancel_requested(cancel: *const D8gCancel) -> i32 {
    if cancel.is_null() {
        return 0;
    }
    // SAFETY: guaranteed by the caller contract above.
    i32::from(unsafe { (*cancel).probe.requested() })
}

// ---- functions the host calls ------------------------------------------------------------------------

/// Creates a database with `threads` evaluation threads (1 evaluates on the calling thread).
/// Free it with [`d8g_db_free`].
pub extern "C" fn d8g_db_new(threads: u32, max_nodes: u32) -> *mut D8gDb {
    let cfg = Config { threads: threads as usize, max_nodes: max_nodes as usize, ..Config::default() };
    Box::into_raw(Box::new(D8gDb { db: Db::new(cfg), kernel_lock: Arc::new(Mutex::new(())) }))
}

/// # Safety
/// `db` must come from [`d8g_db_new`] and not be used afterwards.  No evaluation may be running.
pub unsafe extern "C" fn d8g_db_free(db: *mut D8gDb) {
    if !db.is_null() {
        // SAFETY: caller contract.
        drop(unsafe { Box::from_raw(db) });
    }
}

/// Registers an input table; `capacity` 0 selects a sparse table.
///
/// # Safety
/// `db` must be valid and not shared with a running evaluation; `name` must be a NUL-terminated
/// string; `out_kind` must be writable.
pub unsafe extern "C" fn d8g_register_input_kind(db: *mut D8gDb, name: *const c_char, capacity: u32, out_kind: *mut u32) -> i32 {
    if db.is_null() || name.is_null() || out_kind.is_null() {
        return D8G_ERR_BAD_ARG;
    }
    guard(std::ptr::null_mut(), || {
        // SAFETY: caller contract.
        let (d, n) = unsafe { (&mut *db, CStr::from_ptr(name).to_string_lossy().into_owned()) };
        let k = d.db.register_input(&n, (capacity > 0).then_some(capacity as usize));
        // SAFETY: caller contract.
        unsafe { *out_kind = u32::from(k.0) };
        D8G_OK
    })
}

/// Registers a kernel compute function as a query kind.
///
/// # Safety
/// `db` must be valid and not shared with a running evaluation; `desc` must point to a valid
/// descriptor whose `name` is NUL-terminated and whose callbacks obey the contracts documented on
/// [`D8gComputeFn`] and [`D8gDepsFn`]; `out_kind` must be writable; `err` may be null.
pub unsafe extern "C" fn d8g_register_kernel_query(db: *mut D8gDb, desc: *const D8gKernelDesc, out_kind: *mut u32, err: *mut D8gError) -> i32 {
    if db.is_null() || desc.is_null() || out_kind.is_null() {
        return D8G_ERR_BAD_ARG;
    }
    // SAFETY: caller contract.
    let desc = unsafe { &*desc };
    let Some(compute) = desc.compute else {
        fill_error(err, D8G_ERR_BAD_ARG, "kernel has no compute function");
        return D8G_ERR_BAD_ARG;
    };
    if desc.abi_version != D8G_ABI_VERSION || desc.name.is_null() || (desc.flags & (D8G_THREAD_SAFE | D8G_SERIALIZED)) == 0 {
        fill_error(err, D8G_ERR_BAD_ARG, "bad ABI version, missing name, or neither THREAD_SAFE nor SERIALIZED declared");
        return D8G_ERR_BAD_ARG;
    }
    guard(err, || {
        // SAFETY: caller contract.
        let (d, name) = unsafe { (&mut *db, CStr::from_ptr(desc.name).to_string_lossy().into_owned()) };
        let user = UserPtr(desc.user);
        let deps_fn = desc.deps;
        let serialized = desc.flags & D8G_SERIALIZED != 0;
        let lock = d.kernel_lock.clone();
        let kname = name.clone();
        let kind = d.db.register_query(&name, (desc.capacity > 0).then_some(desc.capacity as usize), move |ctx| {
            let arg = ctx.arg();
            let cancel = D8gCancel { probe: ctx.cancel_probe() };
            // Planning phase: collect dependencies (restarts re-run only this cheap part).
            let mut ids: Vec<NodeId> = Vec::new();
            let blobs: Vec<Blob> = loop {
                let vals = ctx.get_all(&ids)?;
                let Some(f) = deps_fn else { break vals };
                let refs = refs_of(&vals);
                let mut deps = D8gDeps { items: Vec::new() };
                let _g = serialized.then(|| lock.lock().unwrap_or_else(std::sync::PoisonError::into_inner));
                // SAFETY: the registering side guarantees the callback and cookie are valid and thread-safe.
                let st = unsafe { f(user.get(), arg, refs.as_ptr(), refs.len() as u32, &mut deps) };
                drop(_g);
                match st {
                    D8G_OK => break vals,
                    D8G_NEED_DEPS => ids.extend(deps.items.iter().map(|&(k, a)| ctx.node(Kind(k as u16), a))),
                    D8G_ERR_CANCELLED => return Err(Interrupt::Cancelled),
                    code => panic!("[kernel] {kname}: deps callback failed with status {code}"),
                }
            };
            ctx.check_cancel()?;
            let refs = refs_of(&blobs);
            let mut out = D8gOut { bytes: Vec::new(), hash: None };
            let _g = serialized.then(|| lock.lock().unwrap_or_else(std::sync::PoisonError::into_inner));
            // SAFETY: as above; `out` and `cancel` outlive the call.
            let st = unsafe { compute(user.get(), arg, refs.as_ptr(), refs.len() as u32, &mut out, &cancel) };
            drop(_g);
            match st {
                D8G_OK => {
                    let mut b = Blob::new(&out.bytes);
                    if let Some(h) = out.hash {
                        b.hash = h;
                    }
                    Ok(b)
                }
                D8G_ERR_CANCELLED => Err(Interrupt::Cancelled),
                code => panic!("[kernel] {kname}: compute failed with status {code}"),
            }
        });
        // SAFETY: caller contract.
        unsafe { *out_kind = u32::from(kind.0) };
        D8G_OK
    })
}

/// Creates an input node with its first value.
///
/// # Safety
/// `db` must be valid and idle; `data` valid for `len` bytes; `out_node` writable.
pub unsafe extern "C" fn d8g_create_input(db: *mut D8gDb, kind: u32, arg: u64, data: *const u8, len: usize, out_node: *mut u32) -> i32 {
    if db.is_null() || out_node.is_null() || (data.is_null() && len > 0) {
        return D8G_ERR_BAD_ARG;
    }
    guard(std::ptr::null_mut(), || {
        // SAFETY: caller contract.
        let (d, s) = unsafe { (&mut *db, if len == 0 { &[][..] } else { std::slice::from_raw_parts(data, len) }) };
        let id = d.db.create_input(Kind(kind as u16), arg, Blob::new(s));
        // SAFETY: caller contract.
        unsafe { *out_node = id.0 };
        D8G_OK
    })
}

/// Finds or creates the node `(kind, arg)` (used to name query roots).
///
/// # Safety
/// `db` must be valid; `out_node` writable.
pub unsafe extern "C" fn d8g_node(db: *mut D8gDb, kind: u32, arg: u64, out_node: *mut u32) -> i32 {
    if db.is_null() || out_node.is_null() {
        return D8G_ERR_BAD_ARG;
    }
    guard(std::ptr::null_mut(), || {
        // SAFETY: caller contract.
        let id = unsafe { (*db).db.node(Kind(kind as u16), arg) };
        // SAFETY: caller contract.
        unsafe { *out_node = id.0 };
        D8G_OK
    })
}

/// Sets an input in a one-input edit transaction; `*changed` is 1 when the content hash differed.
///
/// # Safety
/// `db` must be valid and idle (no evaluation running); `data` valid for `len` bytes; `changed` writable or null.
pub unsafe extern "C" fn d8g_set_input(db: *mut D8gDb, node: u32, data: *const u8, len: usize, changed: *mut i32) -> i32 {
    if db.is_null() || (data.is_null() && len > 0) {
        return D8G_ERR_BAD_ARG;
    }
    guard(std::ptr::null_mut(), || {
        // SAFETY: caller contract.
        let (d, s) = unsafe { (&mut *db, if len == 0 { &[][..] } else { std::slice::from_raw_parts(data, len) }) };
        let c = d.db.set_input(NodeId(node), Blob::new(s));
        if !changed.is_null() {
            // SAFETY: caller contract.
            unsafe { *changed = i32::from(c) };
        }
        D8G_OK
    })
}

/// Creates a cancellation token.  Free it with [`d8g_cancel_token_free`].
pub extern "C" fn d8g_cancel_token_new() -> *mut D8gCancelToken {
    Box::into_raw(Box::new(D8gCancelToken { token: CancelToken::new() }))
}

/// # Safety
/// `c` must come from [`d8g_cancel_token_new`]; safe to call from any thread while an evaluation runs.
pub unsafe extern "C" fn d8g_cancel_token_request(c: *mut D8gCancelToken) {
    if !c.is_null() {
        // SAFETY: caller contract.
        unsafe { (*c).token.cancel() };
    }
}

/// # Safety
/// `c` must come from [`d8g_cancel_token_new`] and not be used afterwards.
pub unsafe extern "C" fn d8g_cancel_token_free(c: *mut D8gCancelToken) {
    if !c.is_null() {
        // SAFETY: caller contract.
        drop(unsafe { Box::from_raw(c) });
    }
}

/// Brings `n` nodes up to date (all at visible priority).  `cancel` may be null.
///
/// # Safety
/// `db` must be valid; `nodes` valid for `n` ids; `cancel` null or valid; `err` null or writable.
pub unsafe extern "C" fn d8g_evaluate(db: *mut D8gDb, nodes: *const u32, n: usize, cancel: *const D8gCancelToken, err: *mut D8gError) -> i32 {
    if db.is_null() || (nodes.is_null() && n > 0) {
        return D8G_ERR_BAD_ARG;
    }
    guard(err, || {
        // SAFETY: caller contract.
        let (d, ids) = unsafe { (&*db, if n == 0 { &[][..] } else { std::slice::from_raw_parts(nodes, n) }) };
        let reqs: Vec<(NodeId, Priority)> = ids.iter().map(|&i| (NodeId(i), Priority::Visible)).collect();
        let token = if cancel.is_null() { None } else { Some(unsafe { (*cancel).token.clone() }) };
        let opts = EvalOptions { cancel: token.as_ref(), ..EvalOptions::default() };
        match d.db.evaluate_with(&reqs, &opts) {
            Ok(_) => D8G_OK,
            Err(e) => {
                let code = status_of(&e);
                fill_error(err, code, &e.to_string());
                code
            }
        }
    })
}

/// Copies the node's value into the caller's buffer (the caller allocates, the engine never hands
/// out memory the caller must free).  `*len` receives the full length; returns `D8G_ERR_BAD_ARG`
/// when the node is not up to date or the buffer is too small.
///
/// # Safety
/// `db` must be valid; `buf` valid for `cap` bytes (or null with `cap == 0`); `len` writable; `hash` null or writable for two `u64`.
pub unsafe extern "C" fn d8g_read(db: *mut D8gDb, node: u32, buf: *mut u8, cap: usize, len: *mut usize, hash: *mut u64) -> i32 {
    if db.is_null() || len.is_null() || (buf.is_null() && cap > 0) {
        return D8G_ERR_BAD_ARG;
    }
    guard(std::ptr::null_mut(), || {
        // SAFETY: caller contract.
        let Some(v) = (unsafe { (*db).db.value(NodeId(node)) }) else {
            return D8G_ERR_BAD_ARG;
        };
        // SAFETY: caller contract.
        unsafe { *len = v.bytes.len() };
        if !hash.is_null() {
            // SAFETY: caller contract.
            unsafe {
                *hash = v.hash.hi;
                *hash.add(1) = v.hash.lo;
            }
        }
        if v.bytes.len() > cap {
            return D8G_ERR_BAD_ARG;
        }
        // SAFETY: caller contract; the lengths were just checked.
        unsafe { std::ptr::copy_nonoverlapping(v.bytes.as_ptr(), buf, v.bytes.len()) };
        D8G_OK
    })
}

/// Number of completed computations of a node (diagnostics and tests).
///
/// # Safety
/// `db` must be valid.
pub unsafe extern "C" fn d8g_computes(db: *mut D8gDb, node: u32) -> u32 {
    if db.is_null() {
        return 0;
    }
    // SAFETY: caller contract.
    unsafe { (*db).db.computes(NodeId(node)) }
}
