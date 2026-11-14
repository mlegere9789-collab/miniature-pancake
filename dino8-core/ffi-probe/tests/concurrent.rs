//! Concurrent-append tests: many threads share one log through plain Rust and through the C ABI.

use dino8_ffi_probe::log::{Log, OpInput, ReadBuffers};
use dino8_ffi_probe::*;
use std::ptr;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::thread;

fn id(a: u64) -> D8ObjectId {
    let mut bytes = [0u8; 16];
    bytes[..8].copy_from_slice(&a.to_le_bytes());
    bytes[8..].copy_from_slice(&(!a).to_le_bytes());
    D8ObjectId { bytes }
}

/// A record whose every byte is derived from `k`, so a torn or mixed record is detectable.
fn pattern(k: u64) -> (Vec<u8>, [D8ObjectId; 2]) {
    let b = (k % 251) as u8;
    (vec![b; 97], [id(k), id(k ^ 0xFF)])
}

#[test]
fn many_writers_get_unique_contiguous_sequence_numbers() {
    const THREADS: u64 = 8;
    const PER: u64 = 20_000;
    let log = Arc::new(Log::open_memory());
    let handles: Vec<_> = (0..THREADS)
        .map(|t| {
            let log = Arc::clone(&log);
            thread::spawn(move || {
                let mut mine = Vec::with_capacity(PER as usize);
                for i in 0..PER {
                    let k = (t << 32) | i;
                    let (params, ids) = pattern(k);
                    let op = OpInput {
                        command_id: t as u32,
                        tool_version: 1,
                        random_seed: k,
                        params: &params,
                        inputs: &ids,
                        outputs: &[],
                    };
                    mine.push((k, log.append(&op).unwrap()));
                }
                mine
            })
        })
        .collect();
    let per_thread: Vec<Vec<(u64, u64)>> = handles.into_iter().map(|h| h.join().unwrap()).collect();

    let mut all: Vec<u64> = Vec::new();
    for mine in &per_thread {
        assert!(
            mine.windows(2).all(|w| w[0].1 < w[1].1),
            "a thread sees increasing numbers"
        );
        all.extend(mine.iter().map(|&(_, s)| s));
    }
    all.sort_unstable();
    assert_eq!(all.len() as u64, THREADS * PER);
    assert!(
        all.iter().enumerate().all(|(i, &s)| s == i as u64 + 1),
        "unique and gap free"
    );
    assert_eq!(log.len(), THREADS * PER);

    // Every record carries exactly what its writer produced.
    for mine in &per_thread {
        for &(k, seq) in mine.iter().step_by(101) {
            let (params, ids) = pattern(k);
            let mut p = vec![0u8; 128];
            let mut i = [D8ObjectId::default(); 2];
            let m = log
                .read(
                    seq,
                    ReadBuffers {
                        params: &mut p,
                        inputs: &mut i,
                        outputs: &mut [],
                    },
                )
                .unwrap();
            assert!(m.copied);
            assert_eq!(m.random_seed, k);
            assert_eq!(&p[..m.params_len], &params[..]);
            assert_eq!(i, ids);
        }
    }
}

#[test]
fn readers_never_observe_a_torn_record_while_writers_append() {
    const WRITERS: u64 = 4;
    const PER: u64 = 15_000;
    let log = Arc::new(Log::open_memory());
    let done = Arc::new(AtomicBool::new(false));
    let checked = Arc::new(AtomicU64::new(0));

    let readers: Vec<_> = (0..3)
        .map(|r| {
            let (log, done, checked) = (Arc::clone(&log), Arc::clone(&done), Arc::clone(&checked));
            thread::spawn(move || {
                let mut x = 0x9E37_79B9_7F4A_7C15u64 ^ r;
                while !done.load(Ordering::Acquire) {
                    let n = log.len();
                    if n == 0 {
                        thread::yield_now();
                        continue;
                    }
                    x ^= x << 13;
                    x ^= x >> 7;
                    x ^= x << 17;
                    let seq = 1 + x % n; // anything at or below the committed length must be readable
                    let mut p = vec![0u8; 128];
                    let mut i = [D8ObjectId::default(); 2];
                    let m = log
                        .read(
                            seq,
                            ReadBuffers {
                                params: &mut p,
                                inputs: &mut i,
                                outputs: &mut [],
                            },
                        )
                        .expect("a committed record is readable");
                    assert!(m.copied && m.params_len == 97);
                    assert!(p[..97].iter().all(|&b| b == p[0]), "no torn parameter blob");
                    assert_eq!(
                        i[0].bytes[..8],
                        m.random_seed.to_le_bytes(),
                        "ids belong to this record"
                    );
                    checked.fetch_add(1, Ordering::Relaxed);
                }
            })
        })
        .collect();

    let writers: Vec<_> = (0..WRITERS)
        .map(|t| {
            let log = Arc::clone(&log);
            thread::spawn(move || {
                for i in 0..PER {
                    let k = (t << 32) | i;
                    let (params, ids) = pattern(k);
                    let op = OpInput {
                        command_id: 1,
                        tool_version: 1,
                        random_seed: k,
                        params: &params,
                        inputs: &ids,
                        outputs: &[],
                    };
                    log.append(&op).unwrap();
                }
            })
        })
        .collect();
    for w in writers {
        w.join().unwrap();
    }
    done.store(true, Ordering::Release);
    for r in readers {
        r.join().unwrap();
    }
    assert_eq!(log.len(), WRITERS * PER);
    assert!(checked.load(Ordering::Relaxed) > 0, "readers made progress");
}

fn open_memory() -> D8LogHandle {
    let mut h = D8LogHandle::default();
    assert_eq!(
        unsafe { d8_log_open(ptr::null(), 0, &mut h, ptr::null_mut()) },
        D8_OK
    );
    h
}

#[test]
fn concurrent_open_append_close_through_the_c_api() {
    let ok = Arc::new(AtomicU64::new(0));
    let threads: Vec<_> = (0..8)
        .map(|_| {
            let ok = Arc::clone(&ok);
            thread::spawn(move || {
                for _ in 0..300 {
                    let mut h = open_memory();
                    let params = [1u8, 2, 3];
                    let rec = D8OpRecord {
                        command_id: 1,
                        tool_version: 1,
                        random_seed: 0,
                        params: params.as_ptr(),
                        params_len: 3,
                        inputs: ptr::null(),
                        n_inputs: 0,
                        outputs: ptr::null(),
                        n_outputs: 0,
                    };
                    let mut seq = 0;
                    for expect in 1..=10u64 {
                        assert_eq!(
                            unsafe { d8_log_append(h, &rec, &mut seq, ptr::null_mut()) },
                            D8_OK
                        );
                        assert_eq!(seq, expect, "each log has its own sequence");
                    }
                    assert_eq!(unsafe { d8_log_close(&mut h, ptr::null_mut()) }, D8_OK);
                    ok.fetch_add(1, Ordering::Relaxed);
                }
            })
        })
        .collect();
    for t in threads {
        t.join().unwrap();
    }
    assert_eq!(ok.load(Ordering::Relaxed), 8 * 300);
}

#[test]
fn closing_while_another_thread_appends_yields_only_defined_statuses() {
    for _ in 0..50 {
        let h = open_memory();
        let appender = {
            thread::spawn(move || {
                let params = [7u8; 16];
                let rec = D8OpRecord {
                    command_id: 1,
                    tool_version: 1,
                    random_seed: 0,
                    params: params.as_ptr(),
                    params_len: params.len(),
                    inputs: ptr::null(),
                    n_inputs: 0,
                    outputs: ptr::null(),
                    n_outputs: 0,
                };
                let mut seen_ok = 0u32;
                loop {
                    let st = unsafe { d8_log_append(h, &rec, ptr::null_mut(), ptr::null_mut()) };
                    match st {
                        D8_OK => seen_ok += 1,
                        D8_ERR_BAD_HANDLE | D8_ERR_CLOSED => break,
                        other => panic!("unexpected status {other}"),
                    }
                }
                seen_ok
            })
        };
        thread::yield_now();
        let mut closing = h;
        assert_eq!(
            unsafe { d8_log_close(&mut closing, ptr::null_mut()) },
            D8_OK
        );
        appender.join().unwrap();
    }
}
