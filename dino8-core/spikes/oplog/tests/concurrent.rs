//! Concurrent appenders against the real engine: several sessions on separate threads edit the
//! same objects, publish through the shared sequencer, and must converge.

mod common;

use common::*;
use oplog::engine::MergeKind;
use oplog::hub::Hub;
use oplog::ids::OpId;
use oplog::script::Step;
use oplog::shared::SharedLog;
use oplog::Workspace;
use std::collections::HashSet;
use std::sync::{Arc, Barrier, Mutex};

/// `(session, final head, final state hash, conflict count)` reported by each thread.
type Outcome = (u64, OpId, oplog::ids::Hash, usize);

struct Client {
    ws: Workspace,
    cursor: usize,
    unsent: Vec<OpId>,
}

impl Client {
    fn new(session: u64) -> Client {
        Client { ws: mem(session), cursor: 0, unsent: Vec::new() }
    }

    fn publish(&mut self, hub: &Hub) {
        for id in std::mem::take(&mut self.unsent) {
            let idx = self.ws.op_ids().iter().position(|x| *x == id).unwrap();
            let pk = self.ws.export_op(idx as u32, &|_| false).unwrap();
            hub.submit(pk).unwrap();
        }
    }

    fn pull(&mut self, hub: &Hub) {
        let entries = hub.since(self.cursor);
        self.cursor += entries.len();
        let pk: Vec<_> = entries.iter().filter(|e| !self.ws.contains(&e.id)).map(|e| e.packet.clone()).collect();
        self.ws.ingest(&pk).unwrap();
    }
}

#[test]
fn six_sessions_editing_shared_objects_converge_to_the_same_state() {
    const SESSIONS: usize = 6;
    let hub = Arc::new(Hub::new());
    // session 0 creates the shared objects and publishes them
    let mut c0 = Client::new(1000);
    // every session starts from the same genesis operation; publish it first
    hub.submit(c0.ws.export_op(0, &|_| false).unwrap()).unwrap();
    {
        let mut sc = script(1, false);
        for _ in 0..160 {
            if let Step::Op(id) = sc.step(&mut c0.ws).unwrap() {
                c0.unsent.push(id);
            }
        }
        c0.publish(&hub);
    }
    let shared_ids: Vec<_> = c0.ws.objects().unwrap().into_iter().map(|(id, _)| id).collect();
    assert!(shared_ids.len() >= 15, "{} shared objects", shared_ids.len());
    let barrier = Arc::new(Barrier::new(SESSIONS));
    let results: Arc<Mutex<Vec<Outcome>>> = Arc::new(Mutex::new(Vec::new()));
    let mut handles = Vec::new();
    let mut c0 = Some(c0);
    for s in 0..SESSIONS {
        let hub = hub.clone();
        let barrier = barrier.clone();
        let results = results.clone();
        let ids = shared_ids.clone();
        let first = if s == 0 { c0.take() } else { None };
        handles.push(std::thread::spawn(move || {
            let mut c = first.unwrap_or_else(|| Client::new(1000 + s as u64));
            c.pull(&hub);
            c.ws.checkout(c.ws.op_ids().into_iter().last().unwrap()).unwrap();
            // edit the shared objects concurrently with the other sessions
            let mut sc = script(50 + s as u64, false);
            sc.adopt(ids);
            barrier.wait();
            for round in 0..25 {
                for _ in 0..8 {
                    if let Step::Op(id) = sc.step(&mut c.ws).unwrap() {
                        c.unsent.push(id);
                    }
                }
                c.publish(&hub);
                if round % 3 == 0 {
                    c.pull(&hub);
                }
            }
            // everyone drains the sequencer, then merges every other session's tip
            barrier.wait();
            c.publish(&hub);
            barrier.wait();
            c.pull(&hub);
            let mine = c.ws.head();
            let mut tips: Vec<OpId> = c.ws.leaves();
            tips.sort();
            for t in tips {
                if t != mine {
                    let o = c.ws.merge(t).unwrap();
                    assert!(matches!(o.kind, MergeKind::Merged | MergeKind::FastForward | MergeKind::AlreadyMerged));
                }
            }
            c.ws.verify().unwrap();
            let conflicts = c.ws.conflicts().unwrap().len();
            results.lock().unwrap().push((c.ws.session_id(), c.ws.head(), c.ws.state_hash(), conflicts));
        }));
    }
    for h in handles {
        h.join().unwrap();
    }
    let r = results.lock().unwrap();
    assert_eq!(r.len(), SESSIONS);
    let hashes: HashSet<_> = r.iter().map(|x| x.2).collect();
    assert_eq!(hashes.len(), 1, "all sessions must converge to one state hash: {r:?}");
    let conflicts: HashSet<_> = r.iter().map(|x| x.3).collect();
    assert_eq!(conflicts.len(), 1, "and report the same number of conflicts");
    println!("converged: state {:?}, {} conflict items, {} published operations", r[0].2, r[0].3, hub.len());
}

#[test]
fn many_threads_hammering_the_sequencer_keep_it_consistent() {
    use oplog::shared::Entry;
    #[derive(Debug)]
    struct E(u64, Vec<u64>);
    impl Entry for E {
        type Id = u64;
        fn id(&self) -> u64 {
            self.0
        }
        fn parents(&self) -> &[u64] {
            &self.1
        }
    }
    let log = Arc::new(SharedLog::new());
    log.append(E(0, vec![])).unwrap();
    log.cas_head("main", None, 0).unwrap();
    const THREADS: u64 = 8;
    const EACH: u64 = 2000;
    let hs: Vec<_> = (1..=THREADS)
        .map(|t| {
            let log = log.clone();
            std::thread::spawn(move || {
                let mut wins = 0u64;
                let mut attempt = 0u64;
                for i in 0..EACH {
                    loop {
                        let head = log.head("main").unwrap();
                        attempt += 1;
                        let id = t * 10_000_000 + attempt;
                        log.append(E(id, vec![head])).unwrap();
                        if log.cas_head("main", Some(head), id).is_ok() {
                            wins += 1;
                            break;
                        }
                        // lost the race: the abandoned entry stays in the log as a side branch
                    }
                    // readers must always see causally closed prefixes
                    if i % 500 == 0 {
                        let n = log.len();
                        for k in (0..n).step_by(97) {
                            let e = log.get(k).unwrap();
                            for p in e.parents() {
                                assert!(log.position(p).unwrap() < k);
                            }
                        }
                    }
                }
                wins
            })
        })
        .collect();
    let wins: u64 = hs.into_iter().map(|h| h.join().unwrap()).sum();
    assert_eq!(wins, THREADS * EACH, "every thread advanced the head exactly EACH times");
    // the head chain is exactly as long as the number of successful advances
    let mut len = 0;
    let mut cur = log.head("main").unwrap();
    while let Some(p) = log.get(log.position(&cur).unwrap()).unwrap().parents().first() {
        len += 1;
        cur = *p;
    }
    assert_eq!(len, wins);
    assert!(log.len() as u64 > wins);
}
