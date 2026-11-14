//! The C ABI sketch: kernel functions registered as queries through plain C function pointers.
//! The kernels below are `extern "C"` Rust functions with exactly the signatures a C++ kernel
//! would export.

use dino8_graph::abi::*;
use std::ffi::{c_void, CString};
use std::sync::atomic::{AtomicI32, AtomicUsize, Ordering};
use std::time::{Duration, Instant};

unsafe fn input_u64(inputs: *const D8gBlobRef, i: usize) -> u64 {
    let r = *inputs.add(i);
    let mut b = [0u8; 8];
    std::ptr::copy_nonoverlapping(r.data, b.as_mut_ptr(), r.len.min(8));
    u64::from_le_bytes(b)
}

struct Host {
    db: *mut D8gDb,
}

impl Host {
    fn new(threads: u32) -> Host {
        Host { db: d8g_db_new(threads, 4096) }
    }
    fn input_kind(&self, name: &str, cap: u32) -> u32 {
        let n = CString::new(name).unwrap();
        let mut k = 0;
        assert_eq!(unsafe { d8g_register_input_kind(self.db, n.as_ptr(), cap, &mut k) }, D8G_OK);
        k
    }
    fn kernel(&self, name: &str, flags: u32, compute: D8gComputeFn, deps: Option<D8gDepsFn>, user: *mut c_void, cap: u32) -> (i32, u32, String) {
        let n = CString::new(name).unwrap();
        let desc = D8gKernelDesc { abi_version: D8G_ABI_VERSION, flags, name: n.as_ptr(), compute: Some(compute), deps, user, capacity: cap };
        let mut k = 0;
        let mut err = D8gError { code: 0, message: [0; 256] };
        let st = unsafe { d8g_register_kernel_query(self.db, &desc, &mut k, &mut err) };
        let msg = unsafe { std::ffi::CStr::from_ptr(err.message.as_ptr()) }.to_string_lossy().into_owned();
        (st, k, msg)
    }
    fn input(&self, kind: u32, arg: u64, v: u64) -> u32 {
        let mut n = 0;
        assert_eq!(unsafe { d8g_create_input(self.db, kind, arg, v.to_le_bytes().as_ptr(), 8, &mut n) }, D8G_OK);
        n
    }
    fn node(&self, kind: u32, arg: u64) -> u32 {
        let mut n = 0;
        assert_eq!(unsafe { d8g_node(self.db, kind, arg, &mut n) }, D8G_OK);
        n
    }
    fn set(&self, node: u32, v: u64) -> bool {
        let mut c = 0;
        assert_eq!(unsafe { d8g_set_input(self.db, node, v.to_le_bytes().as_ptr(), 8, &mut c) }, D8G_OK);
        c == 1
    }
    fn eval(&self, nodes: &[u32], cancel: *const D8gCancelToken) -> (i32, String) {
        let mut err = D8gError { code: 0, message: [0; 256] };
        let st = unsafe { d8g_evaluate(self.db, nodes.as_ptr(), nodes.len(), cancel, &mut err) };
        (st, unsafe { std::ffi::CStr::from_ptr(err.message.as_ptr()) }.to_string_lossy().into_owned())
    }
    fn read(&self, node: u32) -> Option<(u64, [u64; 2])> {
        let mut buf = [0u8; 8];
        let mut len = 0usize;
        let mut h = [0u64; 2];
        let st = unsafe { d8g_read(self.db, node, buf.as_mut_ptr(), 8, &mut len, h.as_mut_ptr()) };
        (st == D8G_OK).then(|| (u64::from_le_bytes(buf), h))
    }
    fn computes(&self, node: u32) -> u32 {
        unsafe { d8g_computes(self.db, node) }
    }
}

impl Drop for Host {
    fn drop(&mut self) {
        unsafe { d8g_db_free(self.db) };
    }
}

// sum2(arg) = x[arg] + x[arg + 1]
unsafe extern "C" fn sum2_deps(user: *mut c_void, arg: u64, _inputs: *const D8gBlobRef, n: u32, deps: *mut D8gDeps) -> i32 {
    if n == 0 {
        let x_kind = *(user as *const u32);
        d8g_deps_push(deps, x_kind, arg);
        d8g_deps_push(deps, x_kind, arg + 1);
        return D8G_NEED_DEPS;
    }
    D8G_OK
}

unsafe extern "C" fn sum2_compute(_user: *mut c_void, _arg: u64, inputs: *const D8gBlobRef, n: u32, out: *mut D8gOut, _cancel: *const D8gCancel) -> i32 {
    assert_eq!(n, 2);
    let v = input_u64(inputs, 0) + input_u64(inputs, 1);
    d8g_out_write(out, v.to_le_bytes().as_ptr(), 8)
}

#[test]
fn kernel_registered_through_the_c_abi_is_incremental() {
    for threads in [1, 4] {
        let h = Host::new(threads);
        let x = h.input_kind("x", 16);
        let x_cell: &'static u32 = Box::leak(Box::new(x));
        let (st, sum2, msg) = h.kernel("sum2", D8G_THREAD_SAFE, sum2_compute, Some(sum2_deps), x_cell as *const u32 as *mut c_void, 16);
        assert_eq!(st, D8G_OK, "{msg}");
        let ins: Vec<u32> = (0..9).map(|i| h.input(x, i, 10 * (i + 1))).collect();
        let roots: Vec<u32> = (0..8).map(|i| h.node(sum2, i)).collect();
        assert_eq!(h.eval(&roots, std::ptr::null()).0, D8G_OK);
        assert_eq!(h.read(roots[3]).unwrap().0, 40 + 50);
        assert!(roots.iter().all(|&r| h.computes(r) == 1));
        // x[5] feeds sum2(4) and sum2(5) only.
        assert!(h.set(ins[5], 1000));
        assert_eq!(h.eval(&roots, std::ptr::null()).0, D8G_OK);
        let counts: Vec<u32> = roots.iter().map(|&r| h.computes(r)).collect();
        assert_eq!(counts, vec![1, 1, 1, 1, 2, 2, 1, 1], "threads={threads}");
        assert_eq!(h.read(roots[4]).unwrap().0, 50 + 1000);
        // Setting an equal value changes nothing.
        assert!(!h.set(ins[5], 1000));
    }
}

// select(arg): deps phase 1 asks for sel[0]; phase 2 asks for x[1] or x[2] depending on its value.
unsafe extern "C" fn select_deps(user: *mut c_void, _arg: u64, inputs: *const D8gBlobRef, n: u32, deps: *mut D8gDeps) -> i32 {
    let kinds = *(user as *const [u32; 2]);
    match n {
        0 => {
            d8g_deps_push(deps, kinds[0], 0);
            D8G_NEED_DEPS
        }
        1 => {
            d8g_deps_push(deps, kinds[1], if input_u64(inputs, 0) % 2 == 0 { 1 } else { 2 });
            D8G_NEED_DEPS
        }
        _ => D8G_OK,
    }
}

unsafe extern "C" fn select_compute(_user: *mut c_void, _arg: u64, inputs: *const D8gBlobRef, n: u32, out: *mut D8gOut, _cancel: *const D8gCancel) -> i32 {
    assert_eq!(n, 2);
    let v = input_u64(inputs, 1);
    d8g_out_write(out, v.to_le_bytes().as_ptr(), 8)
}

#[test]
fn dependencies_that_depend_on_values_use_the_planning_callback() {
    let h = Host::new(2);
    let sel = h.input_kind("sel", 1);
    let x = h.input_kind("x", 4);
    let kinds: &'static [u32; 2] = Box::leak(Box::new([sel, x]));
    let (st, select, msg) = h.kernel("select", D8G_THREAD_SAFE, select_compute, Some(select_deps), kinds as *const [u32; 2] as *mut c_void, 1);
    assert_eq!(st, D8G_OK, "{msg}");
    let s = h.input(sel, 0, 0);
    let _x0 = h.input(x, 0, 5);
    let x1 = h.input(x, 1, 11);
    let x2 = h.input(x, 2, 22);
    let root = h.node(select, 0);
    assert_eq!(h.eval(&[root], std::ptr::null()).0, D8G_OK);
    assert_eq!(h.read(root).unwrap().0, 11);
    // The unused branch does not recompute the kernel; the used one does.
    h.set(x2, 23);
    h.eval(&[root], std::ptr::null());
    assert_eq!(h.computes(root), 1);
    h.set(x1, 12);
    h.eval(&[root], std::ptr::null());
    assert_eq!((h.computes(root), h.read(root).unwrap().0), (2, 12));
    // Flipping the selector switches the branch.
    h.set(s, 1);
    h.eval(&[root], std::ptr::null());
    assert_eq!((h.computes(root), h.read(root).unwrap().0), (3, 23));
}

struct Gauge {
    in_flight: AtomicI32,
    max_seen: AtomicI32,
    calls: AtomicUsize,
}

unsafe extern "C" fn gauge_compute(user: *mut c_void, arg: u64, _inputs: *const D8gBlobRef, _n: u32, out: *mut D8gOut, _cancel: *const D8gCancel) -> i32 {
    let g = &*(user as *const Gauge);
    let now = g.in_flight.fetch_add(1, Ordering::SeqCst) + 1;
    g.max_seen.fetch_max(now, Ordering::SeqCst);
    std::thread::sleep(Duration::from_millis(1));
    g.in_flight.fetch_sub(1, Ordering::SeqCst);
    g.calls.fetch_add(1, Ordering::SeqCst);
    d8g_out_write(out, (arg * 2).to_le_bytes().as_ptr(), 8)
}

#[test]
fn serialized_kernels_never_run_concurrently_and_thread_safe_ones_do() {
    let h = Host::new(8);
    let serial: &'static Gauge = Box::leak(Box::new(Gauge { in_flight: AtomicI32::new(0), max_seen: AtomicI32::new(0), calls: AtomicUsize::new(0) }));
    let parallel: &'static Gauge = Box::leak(Box::new(Gauge { in_flight: AtomicI32::new(0), max_seen: AtomicI32::new(0), calls: AtomicUsize::new(0) }));
    let (_, ks, _) = h.kernel("serialized", D8G_SERIALIZED, gauge_compute, None, serial as *const Gauge as *mut c_void, 64);
    let (_, kp, _) = h.kernel("threadsafe", D8G_THREAD_SAFE, gauge_compute, None, parallel as *const Gauge as *mut c_void, 64);
    let a: Vec<u32> = (0..64).map(|i| h.node(ks, i)).collect();
    let b: Vec<u32> = (0..64).map(|i| h.node(kp, i)).collect();
    assert_eq!(h.eval(&a, std::ptr::null()).0, D8G_OK);
    assert_eq!(h.eval(&b, std::ptr::null()).0, D8G_OK);
    assert_eq!(serial.calls.load(Ordering::SeqCst), 64);
    assert_eq!(serial.max_seen.load(Ordering::SeqCst), 1, "a SERIALIZED kernel must never be entered concurrently");
    assert!(parallel.max_seen.load(Ordering::SeqCst) >= 1);
    assert_eq!(h.read(a[21]).unwrap().0, 42);
}

unsafe extern "C" fn failing_compute(_user: *mut c_void, _arg: u64, _inputs: *const D8gBlobRef, _n: u32, _out: *mut D8gOut, _cancel: *const D8gCancel) -> i32 {
    D8G_ERR_KERNEL
}

#[test]
fn kernel_failure_becomes_a_status_and_an_error_record() {
    let h = Host::new(1);
    let (st, k, _) = h.kernel("boom", D8G_THREAD_SAFE, failing_compute, None, std::ptr::null_mut(), 1);
    assert_eq!(st, D8G_OK);
    let n = h.node(k, 0);
    let (st, msg) = h.eval(&[n], std::ptr::null());
    assert_eq!(st, D8G_ERR_KERNEL);
    assert!(msg.contains("boom") && msg.contains("-2"), "{msg}");
}

unsafe extern "C" fn spin_compute(_user: *mut c_void, _arg: u64, _inputs: *const D8gBlobRef, _n: u32, out: *mut D8gOut, cancel: *const D8gCancel) -> i32 {
    for _ in 0..5000 {
        if d8g_cancel_requested(cancel) != 0 {
            return D8G_ERR_CANCELLED;
        }
        std::thread::sleep(Duration::from_millis(1));
    }
    d8g_out_write(out, 7u64.to_le_bytes().as_ptr(), 8)
}

#[test]
fn kernels_poll_the_cancellation_flag_through_the_abi() {
    let h = Host::new(2);
    let (_, k, _) = h.kernel("spin", D8G_THREAD_SAFE, spin_compute, None, std::ptr::null_mut(), 1);
    let n = h.node(k, 0);
    let tok = d8g_cancel_token_new();
    let tok_addr = tok as usize;
    let canceller = std::thread::spawn(move || {
        std::thread::sleep(Duration::from_millis(30));
        unsafe { d8g_cancel_token_request(tok_addr as *mut D8gCancelToken) };
    });
    let t0 = Instant::now();
    let (st, _) = h.eval(&[n], tok);
    canceller.join().unwrap();
    assert_eq!(st, D8G_ERR_CANCELLED);
    assert!(t0.elapsed() < Duration::from_millis(1000), "took {:?}", t0.elapsed());
    assert_eq!(h.computes(n), 0);
    unsafe { d8g_cancel_token_free(tok) };
}

#[test]
fn registration_requires_a_thread_safety_declaration() {
    let h = Host::new(1);
    let (st, _, msg) = h.kernel("undeclared", 0, failing_compute, None, std::ptr::null_mut(), 1);
    assert_eq!(st, D8G_ERR_BAD_ARG);
    assert!(msg.contains("THREAD_SAFE"), "{msg}");
}

// Output hash override: the kernel reports the same canonical hash for different bytes.
unsafe extern "C" fn canon_compute(_user: *mut c_void, _arg: u64, inputs: *const D8gBlobRef, _n: u32, out: *mut D8gOut, _cancel: *const D8gCancel) -> i32 {
    let v = input_u64(inputs, 0);
    d8g_out_write(out, v.to_le_bytes().as_ptr(), 8);
    d8g_out_set_hash(out, 0xabc, 0xdef + v / 100)
}

unsafe extern "C" fn one_dep(user: *mut c_void, arg: u64, _inputs: *const D8gBlobRef, n: u32, deps: *mut D8gDeps) -> i32 {
    if n == 0 {
        d8g_deps_push(deps, *(user as *const u32), arg);
        return D8G_NEED_DEPS;
    }
    D8G_OK
}

unsafe extern "C" fn copy_compute(_user: *mut c_void, _arg: u64, inputs: *const D8gBlobRef, _n: u32, out: *mut D8gOut, _cancel: *const D8gCancel) -> i32 {
    let v = input_u64(inputs, 0);
    d8g_out_write(out, v.to_le_bytes().as_ptr(), 8)
}

#[test]
fn a_kernel_supplied_content_hash_drives_early_cutoff() {
    let h = Host::new(1);
    let x = h.input_kind("x", 1);
    let xk: &'static u32 = Box::leak(Box::new(x));
    let (_, canon, _) = h.kernel("canon", D8G_THREAD_SAFE, canon_compute, Some(one_dep), xk as *const u32 as *mut c_void, 1);
    let ck: &'static u32 = Box::leak(Box::new(canon));
    let (_, user, _) = h.kernel("user", D8G_THREAD_SAFE, copy_compute, Some(one_dep), ck as *const u32 as *mut c_void, 1);
    let xi = h.input(x, 0, 100);
    let top = h.node(user, 0);
    assert_eq!(h.eval(&[top], std::ptr::null()).0, D8G_OK);
    // 100 -> 150: canon produces different bytes but the same canonical hash (v / 100 unchanged).
    h.set(xi, 150);
    assert_eq!(h.eval(&[top], std::ptr::null()).0, D8G_OK);
    assert_eq!(h.computes(h.node(canon, 0)), 2);
    assert_eq!(h.computes(top), 1, "the dependent was not recomputed: the kernel declared the result unchanged");
    // 150 -> 250 changes the canonical hash.
    h.set(xi, 250);
    h.eval(&[top], std::ptr::null());
    assert_eq!((h.computes(top), h.read(top).unwrap().0), (2, 250));
}
