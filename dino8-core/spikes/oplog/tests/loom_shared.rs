//! Exhaustive interleaving checks of the shared sequencer with loom.
//!
//! Run with: RUSTFLAGS="--cfg loom" cargo test --release --test loom_shared
#![cfg(loom)]

use loom::sync::Arc;
use loom::thread;
use oplog::shared::{AppendError, Entry, SharedLog};

#[derive(Debug)]
struct E(u32, Vec<u32>);

impl Entry for E {
    type Id = u32;
    fn id(&self) -> u32 {
        self.0
    }
    fn parents(&self) -> &[u32] {
        &self.1
    }
}

/// Two appenders extend the same branch head optimistically: append a child of the head they
/// saw, then compare-and-swap the head; on a lost race they re-read the head and try again.
#[test]
fn optimistic_appenders_never_lose_an_operation_and_the_head_is_linear() {
    loom::model(|| {
        let log = Arc::new(SharedLog::new());
        log.append(E(0, vec![])).unwrap();
        log.cas_head("main", None, 0).unwrap();
        let worker = |log: Arc<SharedLog<E>>, mine: u32| {
            move || {
                let mut attempt = 0;
                loop {
                    let head = log.head("main").unwrap();
                    let id = mine * 10 + attempt;
                    log.append(E(id, vec![head])).unwrap();
                    if log.cas_head("main", Some(head), id).is_ok() {
                        return id;
                    }
                    attempt += 1;
                }
            }
        };
        let t1 = thread::spawn(worker(log.clone(), 1));
        let t2 = thread::spawn(worker(log.clone(), 2));
        let (a, b) = (t1.join().unwrap(), t2.join().unwrap());
        // every appended entry is present exactly once and the log is causally ordered
        let n = log.len();
        assert!((3..=5).contains(&n), "genesis + two winners + at most two abandoned attempts, got {n}");
        for i in 0..n {
            let e = log.get(i).unwrap();
            for p in e.parents() {
                assert!(log.position(p).unwrap() < i, "parent after child");
            }
        }
        // the head chain contains both winners: one extends the other
        let mut chain = vec![];
        let mut cur = log.head("main").unwrap();
        loop {
            chain.push(cur);
            let e = log.get(log.position(&cur).unwrap()).unwrap();
            match e.parents().first() {
                Some(p) => cur = *p,
                None => break,
            }
        }
        assert!(chain.contains(&a) && chain.contains(&b), "both winners are on the head chain: {chain:?}");
        assert_eq!(chain.len(), 3, "genesis plus the two winners");
    });
}

#[test]
fn a_reader_that_sees_a_length_sees_every_entry_below_it_with_its_parents_first() {
    loom::model(|| {
        let log = Arc::new(SharedLog::new());
        let l2 = log.clone();
        let w = thread::spawn(move || {
            l2.append(E(1, vec![])).unwrap();
            l2.append(E(2, vec![1])).unwrap();
        });
        let n = log.len();
        for i in 0..n {
            let e = log.get(i).expect("a published entry is readable");
            for p in e.parents() {
                assert!(log.position(p).unwrap() < i);
            }
        }
        let m = log.len();
        assert!(m >= n, "the length never decreases");
        w.join().unwrap();
        assert_eq!(log.len(), 2);
    });
}

#[test]
fn concurrent_duplicate_submissions_are_accepted_exactly_once() {
    loom::model(|| {
        let log = Arc::new(SharedLog::new());
        let go = |log: Arc<SharedLog<E>>| move || log.append(E(7, vec![])).is_ok();
        let t1 = thread::spawn(go(log.clone()));
        let t2 = thread::spawn(go(log.clone()));
        let (a, b) = (t1.join().unwrap(), t2.join().unwrap());
        assert!(a ^ b, "exactly one submission wins");
        assert_eq!(log.len(), 1);
        assert_eq!(log.append(E(7, vec![])), Err(AppendError::Duplicate(0)));
    });
}

#[test]
fn compare_and_swap_of_the_head_has_exactly_one_winner() {
    loom::model(|| {
        let log = Arc::new(SharedLog::new());
        log.append(E(1, vec![])).unwrap();
        log.append(E(2, vec![])).unwrap();
        let go = |log: Arc<SharedLog<E>>, id: u32| move || log.cas_head("main", None, id).is_ok();
        let t1 = thread::spawn(go(log.clone(), 1));
        let t2 = thread::spawn(go(log.clone(), 2));
        let (a, b) = (t1.join().unwrap(), t2.join().unwrap());
        assert!(a ^ b);
        let h = log.head("main").unwrap();
        assert_eq!(h, if a { 1 } else { 2 });
    });
}
