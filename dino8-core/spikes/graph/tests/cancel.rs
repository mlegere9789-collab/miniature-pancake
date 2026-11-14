//! Cooperative cancellation: a cancelled evaluation returns promptly, leaves the
//! database consistent (no claimed or half-written nodes), keeps all completed
//! work, and a later evaluation finishes the job without repeating it.

use dino8_graph::{CancelToken, Config, Db, EvalError, EvalOptions, Interrupt, NodeId, Priority};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

const LAYERS: usize = 8;
const WIDTH: usize = 25;
const N_IN: usize = 16;
const MODULUS: i64 = 1_000_003;

type Hook = Arc<dyn Fn(u64) + Send + Sync>;

/// Layered graph: node `(l, i)` reads three nodes of the previous layer.
struct Layered {
    db: Db<i64>,
    inputs: Vec<NodeId>,
    nodes: Vec<NodeId>,
    deps: Arc<Vec<[usize; 3]>>,
    input_values: Vec<i64>,
}

fn layered(threads: usize, hook: Hook) -> Layered {
    let mut db = Db::<i64>::new(Config { threads, serial_budget: 4, split_threshold: 2, max_nodes: 1 << 12 });
    let inp = db.register_input("in", Some(N_IN));
    // Query index j = layer * WIDTH + i; layer 0 reads inputs, later layers read the previous layer.
    let deps: Arc<Vec<[usize; 3]>> = Arc::new(
        (0..LAYERS * WIDTH)
            .map(|j| {
                let l = j / WIDTH;
                let mix = |k: usize| (j * 7 + k * 13 + l * 3 + 1) % if l == 0 { N_IN } else { WIDTH };
                let base = if l == 0 { 0 } else { (l - 1) * WIDTH };
                [base + mix(0), base + mix(1), base + mix(2)]
            })
            .collect(),
    );
    let d2 = deps.clone();
    let q = db.register_query("q", Some(LAYERS * WIDTH), move |ctx| {
        let j = ctx.arg() as usize;
        let ids: Vec<NodeId> = d2[j]
            .iter()
            .map(|&s| if j / WIDTH == 0 { ctx.node(dino8_graph::Kind(0), s as u64) } else { ctx.node(dino8_graph::Kind(1), s as u64) })
            .collect();
        let v = ctx.get_all(&ids)?;
        ctx.check_cancel()?;
        hook(j as u64);
        Ok((v[0] + 2 * v[1] + 3 * v[2]) % MODULUS)
    });
    assert_eq!(q, dino8_graph::Kind(1));
    let input_values: Vec<i64> = (0..N_IN as i64).map(|i| i * 37 + 5).collect();
    let inputs = (0..N_IN).map(|i| db.create_input(inp, i as u64, input_values[i])).collect();
    let nodes = (0..LAYERS * WIDTH).map(|j| db.node(q, j as u64)).collect();
    Layered { db, inputs, nodes, deps, input_values }
}

impl Layered {
    fn naive(&self, j: usize, memo: &mut Vec<Option<i64>>) -> i64 {
        if let Some(v) = memo[j] {
            return v;
        }
        let d = self.deps[j];
        let get = |s: usize, me: &Self, memo: &mut Vec<Option<i64>>| if j / WIDTH == 0 { me.input_values[s] } else { me.naive(s, memo) };
        let a = get(d[0], self, memo);
        let b = get(d[1], self, memo);
        let c = get(d[2], self, memo);
        let v = (a + 2 * b + 3 * c) % MODULUS;
        memo[j] = Some(v);
        v
    }

    fn assert_consistent(&self, what: &str) {
        let dump = self.db.canonical_dump();
        let by_key: std::collections::HashMap<(String, u64), &dino8_graph::engine::NodeRecord> =
            dump.iter().map(|r| ((r.kind.clone(), r.key), r)).collect();
        for r in &dump {
            assert!(matches!(r.state, "fresh" | "stale" | "clean"), "{what}: node {}[{}] left {}", r.kind, r.key, r.state);
            if r.state == "clean" && r.kind == "q" {
                for d in &r.deps {
                    let dr = by_key[&(d.0.clone(), d.1)];
                    assert_eq!(dr.state, "clean", "{what}: clean q[{}] has non-clean dependency {}[{}]", r.key, d.0, d.1);
                }
            }
        }
        let mut memo = vec![None; LAYERS * WIDTH];
        for (j, &n) in self.nodes.iter().enumerate() {
            if let Some(v) = self.db.value(n) {
                assert_eq!(v, self.naive(j, &mut memo), "{what}: q[{j}] holds a wrong value");
            }
        }
    }

    fn assert_complete(&self, what: &str) {
        let mut memo = vec![None; LAYERS * WIDTH];
        for (j, &n) in self.nodes.iter().enumerate() {
            assert_eq!(self.db.value(n), Some(self.naive(j, &mut memo)), "{what}: q[{j}]");
        }
    }
}

fn no_hook() -> Hook {
    Arc::new(|_| {})
}

#[test]
fn precancelled_token_does_no_work_and_changes_nothing() {
    for threads in [1, 4] {
        let l = layered(threads, no_hook());
        let tok = CancelToken::new();
        tok.cancel();
        let opts = EvalOptions { cancel: Some(&tok), ..Default::default() };
        let reqs: Vec<(NodeId, Priority)> = l.nodes.iter().map(|&n| (n, Priority::Visible)).collect();
        assert_eq!(l.db.evaluate_with(&reqs, &opts).unwrap_err(), EvalError::Cancelled);
        assert!(l.nodes.iter().all(|&n| l.db.computes(n) == 0));
        l.assert_consistent("pre-cancelled");
        l.db.evaluate(&l.nodes).unwrap();
        l.assert_complete("after pre-cancelled");
    }
}

#[test]
fn cancel_at_any_point_keeps_completed_work_and_resume_finishes_exactly_once() {
    let points = [1usize, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 199];
    for threads in [1, 2, 4, 8] {
        for &k in &points {
            let tok = CancelToken::new();
            let count = Arc::new(AtomicUsize::new(0));
            let (t2, c2) = (tok.clone(), count.clone());
            let l = layered(
                threads,
                Arc::new(move |_| {
                    if c2.fetch_add(1, Ordering::SeqCst) + 1 == k {
                        t2.cancel();
                    }
                }),
            );
            let opts = EvalOptions { cancel: Some(&tok), ..Default::default() };
            let reqs: Vec<(NodeId, Priority)> = l.nodes.iter().map(|&n| (n, Priority::Visible)).collect();
            let r = l.db.evaluate_with(&reqs, &opts);
            let done_at_cancel: u32 = l.nodes.iter().map(|&n| l.db.computes(n)).sum();
            if k < LAYERS * WIDTH {
                assert_eq!(r.unwrap_err(), EvalError::Cancelled, "threads={threads} k={k}");
                // The cancel stops work: at most one more step per worker can be in flight.
                assert!(
                    done_at_cancel as usize <= k + 2 * threads,
                    "threads={threads} k={k}: {done_at_cancel} nodes completed after the cancel request"
                );
            }
            l.assert_consistent(&format!("cancelled threads={threads} k={k}"));
            // Resume without the token: everything completes and every node ran exactly once overall.
            l.db.evaluate(&l.nodes).unwrap();
            l.assert_complete(&format!("resumed threads={threads} k={k}"));
            for (j, &n) in l.nodes.iter().enumerate() {
                assert_eq!(l.db.computes(n), 1, "threads={threads} k={k}: q[{j}] completed {} times", l.db.computes(n));
            }
        }
    }
}

#[test]
fn cancel_after_an_edit_then_resume_recomputes_each_affected_node_once() {
    for threads in [1, 3, 8] {
        for k in [1usize, 4, 9, 20] {
            let tok = CancelToken::new();
            let armed = Arc::new(AtomicBool::new(false));
            let count = Arc::new(AtomicUsize::new(0));
            let (t2, a2, c2) = (tok.clone(), armed.clone(), count.clone());
            let mut l = layered(
                threads,
                Arc::new(move |_| {
                    if a2.load(Ordering::SeqCst) && c2.fetch_add(1, Ordering::SeqCst) + 1 == k {
                        t2.cancel();
                    }
                }),
            );
            l.db.evaluate(&l.nodes).unwrap();
            l.input_values[3] += 1000;
            let inp3 = l.inputs[3];
            let v = l.input_values[3];
            l.db.set_input(inp3, v);
            armed.store(true, Ordering::SeqCst);
            let opts = EvalOptions { cancel: Some(&tok), ..Default::default() };
            let reqs: Vec<(NodeId, Priority)> = l.nodes.iter().map(|&n| (n, Priority::Visible)).collect();
            let first = l.db.evaluate_with(&reqs, &opts);
            l.assert_consistent(&format!("edit+cancel threads={threads} k={k}"));
            if first.is_ok() {
                // Fewer than k affected nodes: the cancel never fired.
                assert!(!tok.is_cancelled());
            }
            l.db.evaluate(&l.nodes).unwrap();
            l.assert_complete(&format!("edit+resume threads={threads} k={k}"));
            for &n in &l.nodes {
                assert!(l.db.computes(n) <= 2, "a node recomputed more than once for one edit");
            }
        }
    }
}

#[test]
fn cancellation_latency_is_bounded_by_one_query() {
    // 64 independent 5 ms queries on 4 threads would take 80 ms; cancel after 20 ms.
    let ran = Arc::new(AtomicUsize::new(0));
    let r2 = ran.clone();
    let mut db = Db::<i64>::new(Config { threads: 4, serial_budget: 0, split_threshold: 2, max_nodes: 1 << 10 });
    let inp = db.register_input("x", Some(1));
    let q = db.register_query("sleepy", Some(64), move |ctx| {
        let v = ctx.read(inp, 0)?;
        ctx.check_cancel()?;
        std::thread::sleep(Duration::from_millis(5));
        r2.fetch_add(1, Ordering::SeqCst);
        Ok(v + ctx.arg() as i64)
    });
    db.create_input(inp, 0, 1);
    let roots: Vec<NodeId> = (0..64).map(|i| db.node(q, i)).collect();
    let tok = CancelToken::new();
    let t2 = tok.clone();
    let stamp = Arc::new(std::sync::Mutex::new(None::<Instant>));
    let s2 = stamp.clone();
    let canceller = std::thread::spawn(move || {
        std::thread::sleep(Duration::from_millis(20));
        *s2.lock().unwrap() = Some(Instant::now());
        t2.cancel();
    });
    let reqs: Vec<(NodeId, Priority)> = roots.iter().map(|&n| (n, Priority::Visible)).collect();
    let r = db.evaluate_with(&reqs, &EvalOptions { cancel: Some(&tok), ..Default::default() });
    let returned = Instant::now();
    canceller.join().unwrap();
    assert_eq!(r.unwrap_err(), EvalError::Cancelled);
    let latency = returned.duration_since(stamp.lock().unwrap().expect("cancel stamp"));
    assert!(latency < Duration::from_millis(250), "cancel took {latency:?} to take effect");
    assert!(ran.load(Ordering::SeqCst) < 64, "all queries ran: the cancel did not stop any work");
    // The database is consistent and the work can be resumed.
    let done: usize = roots.iter().filter(|&&n| db.is_clean(n)).count();
    assert_eq!(done, ran.load(Ordering::SeqCst), "every completed query is kept, every other one is back to fresh");
    db.evaluate(&roots).unwrap();
    assert!(roots.iter().all(|&n| db.is_clean(n) && db.computes(n) == 1));
}

#[test]
fn a_long_running_query_can_poll_for_cancellation() {
    let mut db = Db::<i64>::new(Config { threads: 2, serial_budget: 0, ..Config::default() });
    let spins = Arc::new(AtomicUsize::new(0));
    let sp = spins.clone();
    let release = Arc::new(AtomicBool::new(false));
    let rel = release.clone();
    let q = db.register_query("spin", Some(1), move |ctx| {
        // Pretend to be a kernel call that can be interrupted between work units.
        while !rel.load(Ordering::SeqCst) {
            ctx.check_cancel()?;
            sp.fetch_add(1, Ordering::SeqCst);
            std::thread::sleep(Duration::from_millis(1));
        }
        Ok::<i64, Interrupt>(42)
    });
    let n = db.node(q, 0);
    let tok = CancelToken::new();
    let t2 = tok.clone();
    let canceller = std::thread::spawn(move || {
        std::thread::sleep(Duration::from_millis(30));
        t2.cancel();
    });
    let t0 = Instant::now();
    let r = db.evaluate_with(&[(n, Priority::Visible)], &EvalOptions { cancel: Some(&tok), ..Default::default() });
    canceller.join().unwrap();
    assert_eq!(r.unwrap_err(), EvalError::Cancelled);
    assert!(t0.elapsed() < Duration::from_millis(500));
    assert!(spins.load(Ordering::SeqCst) > 0);
    assert!(!db.is_clean(n) && db.computes(n) == 0, "an interrupted query leaves no value behind");
    release.store(true, Ordering::SeqCst);
    db.evaluate(&[n]).unwrap();
    assert_eq!(db.value(n), Some(42));
}

#[test]
fn cancel_during_the_visible_phase_never_starts_the_background_phase() {
    let tok = CancelToken::new();
    let t2 = tok.clone();
    let mut db = Db::<i64>::new(Config { threads: 1, ..Config::default() });
    let inp = db.register_input("x", Some(1));
    let q = db.register_query("q", Some(10), move |ctx| {
        let v = ctx.read(inp, 0)?;
        // The last visible query cancels the evaluation.
        if ctx.arg() == 2 {
            t2.cancel();
        }
        Ok(v + ctx.arg() as i64)
    });
    db.create_input(inp, 0, 1);
    let reqs: Vec<(NodeId, Priority)> = (0..10)
        .map(|i| (db.node(q, i), if i < 3 { Priority::Visible } else { Priority::Background }))
        .collect();
    let r = db.evaluate_with(&reqs, &EvalOptions { cancel: Some(&tok), ..Default::default() });
    assert_eq!(r.unwrap_err(), EvalError::Cancelled);
    for (i, (n, _)) in reqs.iter().enumerate() {
        let want = u32::from(i < 3);
        assert_eq!(db.computes(*n), want, "q[{i}]");
    }
}
