//! Tests of the C ABI, called from Rust exactly as C++ would call it (same symbols, same structs).

use dino8_ffi_probe::*;
use std::ffi::{c_void, CStr, CString};
use std::ptr;

fn new_err() -> D8Error {
    D8Error {
        code: 0,
        reserved: 0,
        detail: 0,
        message: [0; D8_ERROR_MESSAGE_LEN],
    }
}

fn msg(e: &D8Error) -> String {
    let bytes: Vec<u8> = e
        .message
        .iter()
        .take_while(|&&c| c != 0)
        .map(|&c| c as u8)
        .collect();
    String::from_utf8_lossy(&bytes).into_owned()
}

fn id(a: u8, b: u8) -> D8ObjectId {
    let mut bytes = [0u8; 16];
    bytes[0] = a;
    bytes[15] = b;
    D8ObjectId { bytes }
}

fn open_memory() -> D8LogHandle {
    let mut h = D8LogHandle::default();
    let mut e = new_err();
    assert_eq!(
        unsafe { d8_log_open(ptr::null(), 0, &mut h, &mut e) },
        D8_OK
    );
    assert_ne!(h.id, 0);
    h
}

fn close(mut h: D8LogHandle) -> D8Status {
    let mut e = new_err();
    unsafe { d8_log_close(&mut h, &mut e) }
}

fn record<'a>(
    cmd: u32,
    params: &'a [u8],
    ins: &'a [D8ObjectId],
    outs: &'a [D8ObjectId],
) -> D8OpRecord {
    D8OpRecord {
        command_id: cmd,
        tool_version: 800,
        random_seed: 99,
        params: params.as_ptr(),
        params_len: params.len(),
        inputs: ins.as_ptr(),
        n_inputs: ins.len(),
        outputs: outs.as_ptr(),
        n_outputs: outs.len(),
    }
}

fn read_args(params: &mut [u8], ins: &mut [D8ObjectId], outs: &mut [D8ObjectId]) -> D8OpRead {
    D8OpRead {
        seq: 0,
        ref_seq: 0,
        random_seed: 0,
        kind: 0,
        command_id: 0,
        tool_version: 0,
        reserved: 0,
        params_len: 0,
        n_inputs: 0,
        n_outputs: 0,
        params_buf: params.as_mut_ptr(),
        params_cap: params.len(),
        inputs_buf: ins.as_mut_ptr(),
        inputs_cap: ins.len(),
        outputs_buf: outs.as_mut_ptr(),
        outputs_cap: outs.len(),
    }
}

#[test]
fn abi_version_and_status_names() {
    assert_eq!(d8_abi_version(), D8_ABI_VERSION);
    let name = unsafe { CStr::from_ptr(d8_status_name(D8_ERR_PANIC)) };
    assert_eq!(name.to_str().unwrap(), "D8_ERR_PANIC");
    for code in 0..=14 {
        let n = unsafe { CStr::from_ptr(d8_status_name(code)) }
            .to_str()
            .unwrap();
        assert_eq!(n, status_name_str(code));
    }
    assert_eq!(d8_probe_nothing(1), 2);
}

#[test]
fn round_trip_through_the_c_api() {
    let h = open_memory();
    let mut e = new_err();
    let (ins, outs) = ([id(1, 2), id(3, 4)], [id(9, 9)]);
    let rec = record(42, b"offset 0.5", &ins, &outs);
    let mut seq = 0;
    assert_eq!(unsafe { d8_log_append(h, &rec, &mut seq, &mut e) }, D8_OK);
    assert_eq!((seq, e.code), (1, D8_OK));
    assert_eq!(msg(&e), "");

    let (mut p, mut i, mut o) = (
        [0u8; 32],
        [D8ObjectId::default(); 4],
        [D8ObjectId::default(); 4],
    );
    let mut rd = read_args(&mut p, &mut i, &mut o);
    assert_eq!(unsafe { d8_log_read(h, 1, &mut rd, &mut e) }, D8_OK);
    assert_eq!(
        (
            rd.seq,
            rd.kind,
            rd.command_id,
            rd.tool_version,
            rd.random_seed
        ),
        (1, 0, 42, 800, 99)
    );
    assert_eq!(&p[..rd.params_len], b"offset 0.5");
    assert_eq!(&i[..rd.n_inputs], &ins);
    assert_eq!(&o[..rd.n_outputs], &outs);

    let (mut marker, mut undone) = (0, 0);
    assert_eq!(
        unsafe { d8_log_undo_marker(h, &mut marker, &mut undone, &mut e) },
        D8_OK
    );
    assert_eq!((marker, undone), (2, 1));
    assert_eq!(
        unsafe { d8_log_undo_marker(h, ptr::null_mut(), ptr::null_mut(), &mut e) },
        D8_ERR_NOTHING_TO_UNDO
    );

    let mut len = 0;
    assert_eq!(unsafe { d8_log_len(h, &mut len, &mut e) }, D8_OK);
    assert_eq!(len, 2);
    let mut info = D8LogInfo::default();
    assert_eq!(unsafe { d8_log_info(h, &mut info, &mut e) }, D8_OK);
    assert_eq!((info.records, info.live_ops, info.wal_bytes), (2, 0, 0));
    assert_eq!(unsafe { d8_log_sync(h, &mut e) }, D8_OK);
    assert_eq!(close(h), D8_OK);
}

#[test]
fn null_misaligned_and_oversized_arguments_are_status_codes() {
    let h = open_memory();
    let mut e = new_err();
    let rec = record(1, b"abc", &[], &[]);
    let mut seq = 0;

    assert_eq!(
        unsafe { d8_log_append(h, ptr::null(), &mut seq, &mut e) },
        D8_ERR_NULL_ARG
    );
    assert!(msg(&e).contains("`op`"), "{}", msg(&e));
    assert_eq!(
        unsafe { d8_log_append(h, &rec, ptr::null_mut(), &mut e) },
        D8_OK,
        "out_seq is optional"
    );
    assert_eq!(
        unsafe { d8_log_append(h, &rec, &mut seq, ptr::null_mut()) },
        D8_OK,
        "err is optional"
    );
    assert_eq!(
        unsafe { d8_log_open(ptr::null(), 0, ptr::null_mut(), &mut e) },
        D8_ERR_NULL_ARG
    );

    let mut bad = rec;
    bad.params = ptr::null();
    assert_eq!(
        unsafe { d8_log_append(h, &bad, &mut seq, &mut e) },
        D8_ERR_NULL_ARG
    );
    bad = rec;
    bad.params_len = usize::MAX;
    assert_eq!(
        unsafe { d8_log_append(h, &bad, &mut seq, &mut e) },
        D8_ERR_LIMIT,
        "refused before a slice is formed"
    );
    bad.params_len = 0;
    bad.params = ptr::null();
    assert_eq!(
        unsafe { d8_log_append(h, &bad, &mut seq, &mut e) },
        D8_OK,
        "zero length accepts NULL"
    );

    // A misaligned D8OpRecord pointer must be refused, not dereferenced.
    let mut raw = vec![0u8; std::mem::size_of::<D8OpRecord>() + 8];
    let base = raw.as_mut_ptr();
    let misaligned = unsafe { base.add(if (base as usize) & 7 == 0 { 1 } else { 0 }) };
    assert_eq!(
        unsafe { d8_log_append(h, misaligned.cast::<D8OpRecord>(), &mut seq, &mut e) },
        D8_ERR_INVALID_ARG
    );
    assert!(msg(&e).contains("misaligned"));

    // An unknown flag and a path that is not UTF-8.
    let mut tmp = D8LogHandle::default();
    assert_eq!(
        unsafe { d8_log_open(ptr::null(), 0x80, &mut tmp, &mut e) },
        D8_ERR_INVALID_ARG
    );
    assert_eq!(tmp.id, 0);
    let bad_path = CString::new(vec![0xFFu8, 0xFE, b'x']).unwrap();
    assert_eq!(
        unsafe { d8_log_open(bad_path.as_ptr(), 0, &mut tmp, &mut e) },
        D8_ERR_INVALID_ARG
    );
    assert_eq!(close(h), D8_OK);
}

#[test]
fn closed_zero_and_forged_handles_are_rejected() {
    let h = open_memory();
    let mut e = new_err();
    let rec = record(1, b"abc", &[], &[]);
    let mut seq = 0;
    assert_eq!(close(h), D8_OK);
    assert_eq!(
        unsafe { d8_log_append(h, &rec, &mut seq, &mut e) },
        D8_ERR_BAD_HANDLE
    );
    assert_eq!(close(h), D8_ERR_BAD_HANDLE, "closing a stale copy");
    let mut zero = D8LogHandle::default();
    assert_eq!(
        unsafe { d8_log_close(&mut zero, &mut e) },
        D8_OK,
        "closing a zero handle is a no-op"
    );
    for forged in [0u64, 1, u64::MAX, 0xDEAD_BEEF_0000_0001] {
        assert_eq!(
            unsafe { d8_log_append(D8LogHandle { id: forged }, &rec, &mut seq, &mut e) },
            D8_ERR_BAD_HANDLE
        );
    }
    let mut copy = h;
    assert_eq!(
        unsafe { d8_log_close(&mut copy, &mut e) },
        D8_ERR_BAD_HANDLE
    );
    assert_eq!(copy.id, 0, "the handle variable is zeroed even on failure");
}

#[test]
fn a_panic_never_crosses_the_boundary() {
    let h = open_memory();
    let other = open_memory();
    let mut e = new_err();
    let rec = record(1, b"abc", &[], &[]);
    let mut seq = 0;
    let drops_before = d8_probe_drop_count();
    for (mode, expect) in [
        (0u32, "static message"),
        (1, "formatted message"),
        (2, "non-string"),
    ] {
        assert_eq!(
            unsafe { d8_probe_trigger_panic(h, mode, &mut e) },
            D8_ERR_PANIC
        );
        assert_eq!(e.code, D8_ERR_PANIC);
        assert!(
            msg(&e).contains("panic caught at the C boundary"),
            "{}",
            msg(&e)
        );
        assert!(msg(&e).contains(expect), "{}", msg(&e));
        assert_eq!(
            unsafe { d8_log_append(h, &rec, &mut seq, &mut e) },
            D8_OK,
            "same handle still works"
        );
    }
    assert!(
        d8_probe_drop_count() >= drops_before + 3,
        "destructors ran during unwinding"
    );

    // Mode 3 panics while the log's lock is held: that log is poisoned and refuses work.
    assert_eq!(
        unsafe { d8_probe_trigger_panic(h, 3, &mut e) },
        D8_ERR_PANIC
    );
    assert_eq!(
        unsafe { d8_log_append(h, &rec, &mut seq, &mut e) },
        D8_ERR_POISONED
    );
    let (mut p, mut i, mut o) = (
        [0u8; 8],
        [D8ObjectId::default(); 1],
        [D8ObjectId::default(); 1],
    );
    let mut rd = read_args(&mut p, &mut i, &mut o);
    assert_eq!(
        unsafe { d8_log_read(h, 1, &mut rd, &mut e) },
        D8_ERR_POISONED
    );
    assert_eq!(
        unsafe { d8_log_append(other, &rec, &mut seq, &mut e) },
        D8_OK,
        "other logs are unaffected"
    );
    assert_eq!(close(h), D8_OK, "a poisoned log can still be closed");
    assert_eq!(
        unsafe { d8_probe_trigger_panic(other, 77, &mut e) },
        D8_ERR_INVALID_ARG
    );
    assert_eq!(close(other), D8_OK);
}

#[test]
fn error_messages_are_truncated_on_a_character_boundary_and_nul_terminated() {
    let mut e = new_err();
    let mut h = D8LogHandle::default();
    // 3-byte characters: the cut must not land inside one.
    let path = format!("/nonexistent/{}", "\u{20AC}".repeat(300));
    let c = CString::new(path).unwrap();
    assert_eq!(
        unsafe { d8_log_open(c.as_ptr(), 0, &mut h, &mut e) },
        D8_ERR_IO
    );
    assert_eq!(h.id, 0);
    assert!(e.detail != 0, "OS error number");
    let n = e
        .message
        .iter()
        .position(|&b| b == 0)
        .expect("NUL terminated");
    assert!(n < D8_ERROR_MESSAGE_LEN);
    assert!(std::str::from_utf8(
        &e.message
            .iter()
            .take(n)
            .map(|&b| b as u8)
            .collect::<Vec<_>>()
    )
    .is_ok());
}

#[test]
fn batch_append_is_contiguous_and_all_or_nothing() {
    let h = open_memory();
    let mut e = new_err();
    let recs: Vec<D8OpRecord> = (0..5).map(|k| record(k, b"b", &[], &[])).collect();
    let mut first = 0;
    assert_eq!(
        unsafe { d8_log_append_batch(h, recs.as_ptr(), recs.len(), &mut first, &mut e) },
        D8_OK
    );
    assert_eq!(first, 1);
    assert_eq!(
        unsafe { d8_log_append_batch(h, recs.as_ptr(), recs.len(), &mut first, &mut e) },
        D8_OK
    );
    assert_eq!(first, 6);
    let mut bad = recs.clone();
    bad[3].params_len = D8_MAX_PARAMS_LEN + 1;
    assert_eq!(
        unsafe { d8_log_append_batch(h, bad.as_ptr(), bad.len(), &mut first, &mut e) },
        D8_ERR_LIMIT
    );
    let mut len = 0;
    assert_eq!(unsafe { d8_log_len(h, &mut len, &mut e) }, D8_OK);
    assert_eq!(len, 10, "the invalid batch wrote nothing");
    assert_eq!(
        unsafe { d8_log_append_batch(h, ptr::null(), 0, &mut first, &mut e) },
        D8_ERR_INVALID_ARG
    );
    assert_eq!(close(h), D8_OK);
}

struct VisitState {
    seen: Vec<u64>,
    stop_at: u64,
    reenter: Option<D8LogHandle>,
}

unsafe extern "C-unwind" fn visit_cb(user: *mut c_void, op: *const D8OpView) -> i32 {
    let state = unsafe { &mut *user.cast::<VisitState>() };
    let op = unsafe { &*op };
    state.seen.push(op.seq);
    if let Some(h) = state.reenter {
        let mut len = 0;
        assert_eq!(unsafe { d8_log_len(h, &mut len, ptr::null_mut()) }, D8_OK);
    }
    if op.seq == state.stop_at {
        7
    } else {
        0
    }
}

#[test]
fn visit_walks_records_and_stops_on_a_nonzero_return() {
    let h = open_memory();
    let mut e = new_err();
    for k in 0..600u32 {
        let rec = record(k, &k.to_le_bytes(), &[], &[]);
        assert_eq!(
            unsafe { d8_log_append(h, &rec, ptr::null_mut(), &mut e) },
            D8_OK
        );
    }
    let mut st = VisitState {
        seen: vec![],
        stop_at: 0,
        reenter: Some(h),
    };
    let user = ptr::addr_of_mut!(st).cast::<c_void>();
    assert_eq!(
        unsafe { d8_log_visit(h, 1, u64::MAX, Some(visit_cb), user, &mut e) },
        D8_OK
    );
    assert_eq!(
        st.seen,
        (1..=600).collect::<Vec<u64>>(),
        "crosses the internal 256-record batches in order"
    );

    st.seen.clear();
    st.stop_at = 300;
    assert_eq!(
        unsafe { d8_log_visit(h, 250, 600, Some(visit_cb), user, &mut e) },
        D8_ERR_CALLBACK
    );
    assert_eq!(e.detail, 7);
    assert_eq!(st.seen.len(), 51);
    assert_eq!(
        unsafe { d8_log_visit(h, 1, 5, None, user, &mut e) },
        D8_ERR_NULL_ARG
    );
    assert_eq!(
        unsafe { d8_log_visit(h, 0, 5, Some(visit_cb), user, &mut e) },
        D8_ERR_INVALID_ARG
    );
    assert_eq!(close(h), D8_OK);
}

#[test]
fn file_backed_log_through_the_c_api_survives_reopen() {
    let dir = env!("CARGO_TARGET_TMPDIR");
    let path = CString::new(format!("{dir}/ffi_api_reopen_{}.d8log", std::process::id())).unwrap();
    let mut e = new_err();
    let mut h = D8LogHandle::default();
    assert_eq!(
        unsafe {
            d8_log_open(
                path.as_ptr(),
                D8_LOG_SYNC_WRITE | D8_LOG_TRUNCATE,
                &mut h,
                &mut e,
            )
        },
        D8_OK
    );
    for k in 0..100u32 {
        let rec = record(k, &k.to_le_bytes(), &[id(k as u8, 0)], &[]);
        assert_eq!(
            unsafe { d8_log_append(h, &rec, ptr::null_mut(), &mut e) },
            D8_OK
        );
    }
    assert_eq!(
        unsafe { d8_log_undo_marker(h, ptr::null_mut(), ptr::null_mut(), &mut e) },
        D8_OK
    );
    assert_eq!(unsafe { d8_log_sync(h, &mut e) }, D8_OK);
    assert_eq!(close(h), D8_OK);

    assert_eq!(
        unsafe { d8_log_open(path.as_ptr(), D8_LOG_SYNC_WRITE, &mut h, &mut e) },
        D8_OK
    );
    let mut info = D8LogInfo::default();
    assert_eq!(unsafe { d8_log_info(h, &mut info, &mut e) }, D8_OK);
    assert_eq!(
        (
            info.records,
            info.live_ops,
            info.recovered_records,
            info.discarded_tail_bytes
        ),
        (101, 99, 101, 0)
    );
    assert_eq!(close(h), D8_OK);
    std::fs::remove_file(path.to_str().unwrap()).unwrap();
}
