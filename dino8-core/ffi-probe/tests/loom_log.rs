//! Model-checked concurrency tests. They compile only with `--cfg loom`:
//!
//!     RUSTFLAGS="--cfg loom" cargo test --release --test loom_log
//!
//! loom explores every interleaving of the threads (up to the preemption bound) and checks the
//! memory orderings of the atomics, so a missing happens-before edge or a lost update fails here
//! deterministically instead of once in a million runs.
#![cfg(loom)]

use dino8_ffi_probe::log::{Log, OpInput, ReadBuffers};
use dino8_ffi_probe::D8_ERR_NOTHING_TO_UNDO;
use loom::sync::atomic::{AtomicU64, Ordering};
use loom::sync::Arc;
use loom::thread;
use std::sync::atomic::{AtomicUsize, Ordering as StdOrdering};
use std::sync::Arc as StdArc;

/// Runs `f` under the model checker and returns how many distinct executions (interleavings) it
/// explored. A count of 1 would mean loom saw no concurrency at all.
fn model<F: Fn() + Sync + Send + 'static>(f: F) -> usize {
    let executions = StdArc::new(AtomicUsize::new(0));
    let counter = StdArc::clone(&executions);
    let mut b = loom::model::Builder::new();
    b.preemption_bound = Some(3);
    b.check(move || {
        counter.fetch_add(1, StdOrdering::Relaxed);
        f();
    });
    let n = executions.load(StdOrdering::Relaxed);
    println!("loom explored {n} executions");
    n
}

fn append(log: &Log, cmd: u32) -> u64 {
    let params = [cmd as u8; 5];
    log.append(&OpInput {
        command_id: cmd,
        tool_version: 1,
        random_seed: u64::from(cmd),
        params: &params,
        inputs: &[],
        outputs: &[],
    })
    .unwrap()
}

fn command_of(log: &Log, seq: u64) -> (u32, [u8; 5]) {
    let mut p = [0u8; 8];
    let m = log
        .read(
            seq,
            ReadBuffers {
                params: &mut p,
                inputs: &mut [],
                outputs: &mut [],
            },
        )
        .expect("a committed record is readable");
    assert!(m.copied);
    let mut out = [0u8; 5];
    out.copy_from_slice(&p[..5]);
    (m.command_id, out)
}

#[test]
fn two_appenders_and_a_lock_free_reader() {
    let explored = model(|| {
        let log = Arc::new(Log::open_memory());
        let a = {
            let log = log.clone();
            thread::spawn(move || append(&log, 1))
        };
        let b = {
            let log = log.clone();
            thread::spawn(move || append(&log, 2))
        };
        // Whatever the reader observes through the lock-free length is fully readable.
        let n = log.len();
        for seq in 1..=n {
            let (cmd, bytes) = command_of(&log, seq);
            assert!(cmd == 1 || cmd == 2);
            assert_eq!(bytes, [cmd as u8; 5]);
        }
        let (sa, sb) = (a.join().unwrap(), b.join().unwrap());
        assert_ne!(sa, sb);
        assert_eq!(sa.min(sb), 1);
        assert_eq!(sa.max(sb), 2);
        assert_eq!(log.len(), 2);
        assert_eq!(command_of(&log, sa).0, 1);
        assert_eq!(command_of(&log, sb).0, 2);
    });
    assert!(
        explored > 10,
        "loom must explore many interleavings, explored {explored}"
    );
}

#[test]
fn undo_racing_with_an_append_is_consistent() {
    let explored = model(|| {
        let log = Arc::new(Log::open_memory());
        append(&log, 7); // one live operation to start with
        let appender = {
            let log = log.clone();
            thread::spawn(move || append(&log, 8))
        };
        let undoer = {
            let log = log.clone();
            thread::spawn(move || log.undo_marker())
        };
        let new_seq = appender.join().unwrap();
        let undo = undoer.join().unwrap();
        let (marker, undone) = undo.expect("a live operation existed, so undo succeeds");
        // The undo targets the newest live operation at the instant it ran: 8 if the append went
        // first, otherwise 7. Either way the log has exactly three records and a consistent stack.
        assert_eq!(log.len(), 3);
        assert!(undone == 1 || undone == new_seq);
        assert_eq!(marker, if new_seq == 2 { 3 } else { 2 });
        let live = log.info().unwrap().live_ops;
        assert_eq!(live, 1);
        let second = log.undo_marker().unwrap();
        assert_eq!(second.1, if undone == 1 { new_seq } else { 1 });
        assert_eq!(log.undo_marker().unwrap_err().code, D8_ERR_NOTHING_TO_UNDO);
    });
    assert!(explored > 1, "loom must explore more than one interleaving");
}

#[test]
fn close_racing_with_an_append_never_loses_a_committed_record() {
    let explored = model(|| {
        let log = Arc::new(Log::open_memory());
        let appender = {
            let log = log.clone();
            thread::spawn(move || {
                let params = [3u8; 5];
                log.append(&OpInput {
                    command_id: 3,
                    tool_version: 1,
                    random_seed: 0,
                    params: &params,
                    inputs: &[],
                    outputs: &[],
                })
            })
        };
        log.close().unwrap();
        let res = appender.join().unwrap();
        // Either the append won (and the record counts), or the close won and it was refused.
        match res {
            Ok(seq) => {
                assert_eq!(seq, 1);
                assert_eq!(log.len(), 1);
            }
            Err(e) => {
                assert_eq!(e.code, dino8_ffi_probe::D8_ERR_CLOSED);
                assert_eq!(log.len(), 0);
            }
        }
    });
    assert!(explored > 1, "loom must explore more than one interleaving");
}

/// Negative control: a deliberate lost update (non-atomic read-modify-write). The checker must
/// find the interleaving that loses an increment, which proves it actually explores them.
#[test]
#[should_panic(expected = "lost update")]
fn negative_control_a_lost_update_is_found() {
    model(|| {
        let counter = Arc::new(AtomicU64::new(0));
        let bump = |c: Arc<AtomicU64>| {
            thread::spawn(move || {
                let v = c.load(Ordering::Relaxed);
                c.store(v + 1, Ordering::Relaxed);
            })
        };
        let (a, b) = (bump(counter.clone()), bump(counter.clone()));
        a.join().unwrap();
        b.join().unwrap();
        assert_eq!(counter.load(Ordering::Relaxed), 2, "lost update");
    });
}
