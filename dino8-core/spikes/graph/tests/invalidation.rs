//! Invalidation correctness: editing an unrelated input must not recompute;
//! editing a dependency must; early cutoff, dynamic dependencies, laziness,
//! memoization, content-keyed sharing and the explanation API.

use dino8_graph::{Config, Db, EvalError, Kind, NodeId, Priority, Why};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

fn db_with(threads: usize) -> Db<i64> {
    Db::new(Config { threads, ..Config::default() })
}

/// `n` independent chains `input[i] -> double[i] -> plus_one[i]`.
struct Chains {
    db: Db<i64>,
    inputs: Vec<NodeId>,
    doubles: Vec<NodeId>,
    tops: Vec<NodeId>,
}

fn chains(n: u64, threads: usize) -> Chains {
    let mut db = db_with(threads);
    let inp = db.register_input("in", Some(n as usize));
    let dbl = db.register_query("double", Some(n as usize), move |ctx| {
        let a = ctx.arg();
        Ok(ctx.read(inp, a)? * 2)
    });
    let top = db.register_query("plus_one", Some(n as usize), move |ctx| {
        let a = ctx.arg();
        Ok(ctx.read(dbl, a)? + 1)
    });
    let inputs: Vec<NodeId> = (0..n).map(|i| db.create_input(inp, i, i as i64 + 10)).collect();
    let doubles: Vec<NodeId> = (0..n).map(|i| db.node(dbl, i)).collect();
    let tops: Vec<NodeId> = (0..n).map(|i| db.node(top, i)).collect();
    Chains { db, inputs, doubles, tops }
}

#[test]
fn unrelated_edit_does_not_recompute() {
    for threads in [1, 4] {
        let mut c = chains(8, threads);
        let rep = c.db.evaluate(&c.tops).unwrap();
        assert_eq!(rep.computed, 16, "8 doubles + 8 plus_one on the first pass");
        assert!(c.doubles.iter().chain(&c.tops).all(|&n| c.db.computes(n) == 1));

        // Edit chain 3 only.
        assert!(c.db.set_input(c.inputs[3], 1000));
        let rep = c.db.evaluate(&c.tops).unwrap();
        assert_eq!(rep.computed, 2, "only double[3] and plus_one[3] recompute");
        for i in 0..8 {
            let expect = if i == 3 { 2 } else { 1 };
            assert_eq!(c.db.computes(c.doubles[i]), expect, "double[{i}] threads={threads}");
            assert_eq!(c.db.computes(c.tops[i]), expect, "plus_one[{i}] threads={threads}");
        }
        assert_eq!(c.db.value(c.tops[3]), Some(2001));
        assert_eq!(c.db.value(c.tops[4]), Some(29));
    }
}

#[test]
fn dependency_edit_recomputes_every_dependent_and_only_them() {
    // a -> b, a -> c, (b, c) -> d, e unrelated.
    let mut db = db_with(1);
    let inp = db.register_input("in", Some(4));
    let b = db.register_query("b", Some(1), move |ctx| Ok(ctx.read(inp, 0)? + 1));
    let c = db.register_query("c", Some(1), move |ctx| Ok(ctx.read(inp, 0)? * 3));
    let d = db.register_query("d", Some(1), move |ctx| Ok(ctx.read(b, 0)? + ctx.read(c, 0)?));
    let e = db.register_query("e", Some(1), move |ctx| Ok(ctx.read(inp, 1)? - 1));
    let a = db.create_input(inp, 0, 5);
    let a2 = db.create_input(inp, 1, 100);
    let (nb, nc, nd, ne) = (db.node(b, 0), db.node(c, 0), db.node(d, 0), db.node(e, 0));
    db.evaluate(&[nd, ne]).unwrap();
    assert_eq!(db.value(nd), Some(6 + 15));
    db.set_input(a, 6);
    let rep = db.evaluate(&[nd, ne]).unwrap();
    assert_eq!(rep.computed, 3, "b, c, d");
    assert_eq!((db.computes(nb), db.computes(nc), db.computes(nd), db.computes(ne)), (2, 2, 2, 1));
    assert_eq!(db.value(nd), Some(7 + 18));
    // Editing the other input touches e only.
    db.set_input(a2, 7);
    let rep = db.evaluate(&[nd, ne]).unwrap();
    assert_eq!(rep.computed, 1);
    assert_eq!((db.computes(nb), db.computes(nc), db.computes(nd), db.computes(ne)), (2, 2, 2, 2));
}

#[test]
fn early_cutoff_stops_propagation() {
    // x -> parity -> label -> report.  Changing x from 2 to 4 keeps parity == 0.
    let mut db = db_with(1);
    let inp = db.register_input("x", Some(1));
    let parity = db.register_query("parity", Some(1), move |ctx| Ok(ctx.read(inp, 0)? % 2));
    let label = db.register_query("label", Some(1), move |ctx| Ok(ctx.read(parity, 0)? + 100));
    let report = db.register_query("report", Some(1), move |ctx| Ok(ctx.read(label, 0)? * 2));
    let x = db.create_input(inp, 0, 2);
    let (p, l, r) = (db.node(parity, 0), db.node(label, 0), db.node(report, 0));
    db.evaluate(&[r]).unwrap();
    db.set_input(x, 4);
    let rep = db.evaluate(&[r]).unwrap();
    assert_eq!(rep.computed, 1, "only parity recomputes");
    assert_eq!(rep.cutoffs, 1, "parity recomputed to an equal value");
    assert_eq!(rep.verified, 2, "label and report revalidate without running");
    assert_eq!((db.computes(p), db.computes(l), db.computes(r)), (2, 1, 1));
    // The cutoff did not touch the value's change revision, so a later real change still works.
    db.set_input(x, 5);
    db.evaluate(&[r]).unwrap();
    assert_eq!(db.value(r), Some(202));
    assert_eq!((db.computes(p), db.computes(l), db.computes(r)), (3, 2, 2));
}

#[test]
fn dynamic_dependencies_follow_the_data() {
    // select(c, a, b) reads c, then a when c is even, otherwise b.
    let mut db = db_with(1);
    let inp = db.register_input("in", Some(3));
    let sel = db.register_query("select", Some(1), move |ctx| {
        let c = ctx.read(inp, 0)?;
        if c % 2 == 0 {
            ctx.read(inp, 1)
        } else {
            ctx.read(inp, 2)
        }
    });
    let c = db.create_input(inp, 0, 0);
    let a = db.create_input(inp, 1, 10);
    let b = db.create_input(inp, 2, 20);
    let s = db.node(sel, 0);
    db.evaluate(&[s]).unwrap();
    assert_eq!(db.value(s), Some(10));
    assert_eq!(db.info(s).deps, vec![c, a]);

    // The unused branch does not invalidate select.
    db.set_input(b, 21);
    let rep = db.evaluate(&[s]).unwrap();
    assert_eq!((rep.computed, rep.verified), (0, 0), "select was never marked stale");
    assert_eq!(db.computes(s), 1);

    // The used branch does.
    db.set_input(a, 11);
    db.evaluate(&[s]).unwrap();
    assert_eq!((db.value(s), db.computes(s)), (Some(11), 2));

    // Flip the condition: the dependency set switches from {c, a} to {c, b}.
    db.set_input(c, 1);
    db.evaluate(&[s]).unwrap();
    assert_eq!(db.value(s), Some(21));
    assert_eq!(db.info(s).deps, vec![c, b]);
    assert_eq!(db.info(a).n_rdeps, 0, "reverse edge to the abandoned branch is removed");

    // Now editing the old branch must not recompute select, editing the new one must.
    db.set_input(a, 12);
    let rep = db.evaluate(&[s]).unwrap();
    assert_eq!(rep.computed + rep.verified, 0);
    assert_eq!(db.computes(s), 3);
    db.set_input(b, 22);
    db.evaluate(&[s]).unwrap();
    assert_eq!((db.value(s), db.computes(s)), (Some(22), 4));
}

#[test]
fn noop_edit_keeps_revision_and_marks_nothing() {
    let mut c = chains(2, 1);
    c.db.evaluate(&c.tops).unwrap();
    let rev = c.db.rev();
    let (changed, rep) = c.db.edit("same value", |tx| tx.set(c.inputs[0], 10));
    assert!(!changed);
    assert_eq!((rep.changed, rep.marked_stale), (0, 0));
    assert_eq!(c.db.rev(), rev);
    let rep = c.db.evaluate(&c.tops).unwrap();
    assert_eq!((rep.computed, rep.verified, rep.steps), (0, 0, 0));
}

#[test]
fn evaluation_is_lazy_and_stale_work_is_deferred_until_demanded() {
    let mut c = chains(4, 1);
    // Demand only chain 0: chains 1..3 are never computed.
    c.db.evaluate(&c.tops[..1]).unwrap();
    assert_eq!(c.db.computes(c.doubles[1]), 0);
    let (fresh, stale, clean) = c.db.state_counts();
    assert_eq!((stale, clean >= 1), (0, true));
    assert!(fresh > 0);

    // Edit input 0, evaluate nothing: stale nodes stay stale and nothing computes.
    c.db.set_input(c.inputs[0], 77);
    assert!(!c.db.is_clean(c.tops[0]));
    assert_eq!(c.db.computes(c.tops[0]), 1);
    // Demand it later: it recomputes exactly once.
    c.db.evaluate(&c.tops[..1]).unwrap();
    assert_eq!(c.db.computes(c.tops[0]), 2);
    // A second edit before any demand coalesces: still one recomputation on demand.
    c.db.set_input(c.inputs[0], 78);
    c.db.set_input(c.inputs[0], 79);
    c.db.evaluate(&c.tops[..1]).unwrap();
    assert_eq!(c.db.computes(c.tops[0]), 3);
    assert_eq!(c.db.value(c.tops[0]), Some(159));
}

#[test]
fn memoized_results_are_reused_across_demands() {
    let mut c = chains(4, 1);
    for _ in 0..5 {
        c.db.evaluate(&c.tops).unwrap();
    }
    assert!(c.db.computes(c.tops[2]) == 1 && c.db.computes(c.doubles[2]) == 1);
    let rep = c.db.evaluate(&c.tops).unwrap();
    assert_eq!((rep.steps, rep.computed), (0, 0), "nothing is claimed when everything is clean");
    // Edit and revert to the original value: the second edit is a real change (revision numbers), so the
    // chain recomputes once more and ends with the original value.
    let orig = c.db.value(c.inputs[1]).unwrap();
    c.db.set_input(c.inputs[1], 5);
    c.db.evaluate(&c.tops).unwrap();
    c.db.set_input(c.inputs[1], orig);
    c.db.evaluate(&c.tops).unwrap();
    assert_eq!(c.db.value(c.tops[1]), Some(2 * orig + 1));
    assert_eq!(c.db.computes(c.tops[1]), 3);
}

#[test]
fn content_keyed_queries_are_shared_between_callers() {
    // Two parents ask for square(7); one asks for square(8).  square runs twice in total.
    let mut db = db_with(1);
    let calls = Arc::new(AtomicUsize::new(0));
    let calls2 = calls.clone();
    let inp = db.register_input("in", Some(3));
    let square = db.register_content_query("square", move |ctx| {
        calls2.fetch_add(1, Ordering::SeqCst);
        let v = *ctx.arg_value().expect("content node has an argument");
        Ok(v * v)
    });
    let user = db.register_query("user", Some(3), move |ctx| {
        let a = ctx.arg();
        let x = ctx.read(inp, a)?;
        ctx.content(square, &x)
    });
    let ins: Vec<NodeId> = [7i64, 7, 8].iter().enumerate().map(|(i, &v)| db.create_input(inp, i as u64, v)).collect();
    let users: Vec<NodeId> = (0..3).map(|i| db.node(user, i)).collect();
    db.evaluate(&users).unwrap();
    assert_eq!(calls.load(Ordering::SeqCst), 2);
    assert_eq!(users.iter().map(|&u| db.value(u).unwrap()).collect::<Vec<_>>(), vec![49, 49, 64]);
    // Changing input 1 to 8 reuses the cached square(8): no new square computation.
    db.set_input(ins[1], 8);
    db.evaluate(&users).unwrap();
    assert_eq!(calls.load(Ordering::SeqCst), 2);
    assert_eq!(db.value(users[1]), Some(64));
    // A genuinely new argument computes once.
    db.set_input(ins[1], 9);
    db.evaluate(&users).unwrap();
    assert_eq!(calls.load(Ordering::SeqCst), 3);
}

#[test]
fn batch_edit_shares_one_revision() {
    let mut c = chains(4, 1);
    c.db.evaluate(&c.tops).unwrap();
    let r0 = c.db.rev();
    let (_, rep) = c.db.edit("move walls 0 and 1", |tx| {
        tx.set(c.inputs[0], 500);
        tx.set(c.inputs[1], 600);
        tx.set(c.inputs[1], 601);
    });
    assert_eq!((rep.rev, rep.changed), (r0 + 1, 2), "two distinct inputs, one revision");
    assert_eq!(rep.marked_stale, 4);
    let rep = c.db.evaluate(&c.tops).unwrap();
    assert_eq!(rep.computed, 4);
    assert_eq!(c.db.value(c.tops[1]), Some(1203));
}

#[test]
fn explanation_names_the_edit_that_caused_a_recompute() {
    // wall -> geom -> join -> tag
    let mut db = db_with(1);
    let wall = db.register_input("wall", Some(1));
    let geom = db.register_query("geom", Some(1), move |ctx| Ok(ctx.read(wall, 0)? * 10));
    let join = db.register_query("join", Some(1), move |ctx| Ok(ctx.read(geom, 0)? + 1));
    let tag = db.register_query("tag", Some(1), move |ctx| Ok(ctx.read(join, 0)? * 2));
    let w = db.create_input(wall, 0, 3);
    let t = db.node(tag, 0);
    db.evaluate(&[t]).unwrap();
    let ex = db.explain_recompute(t);
    assert!(matches!(ex.steps[0].why, Why::FirstEvaluation { .. }));

    db.edit("move wall 17", |tx| tx.set(w, 4));
    db.evaluate(&[t]).unwrap();
    let ex = db.explain_recompute(t);
    let text = ex.to_string();
    assert_eq!(ex.steps.len(), 4, "tag <- join <- geom <- wall\n{text}");
    assert_eq!(ex.steps[0].node.to_string(), "tag[0]");
    assert!(matches!(&ex.steps[0].why, Why::DependencyChanged { dep, .. } if dep.to_string() == "join[0]"));
    assert!(matches!(&ex.steps[1].why, Why::DependencyChanged { dep, .. } if dep.to_string() == "geom[0]"));
    assert!(matches!(&ex.steps[2].why, Why::DependencyChanged { dep, .. } if dep.to_string() == "wall[0]"));
    assert!(matches!(&ex.steps[3].why, Why::InputSet { label: Some(l), .. } if l == "move wall 17"));
    assert!(text.contains("move wall 17"), "{text}");
}

#[test]
fn explanation_says_why_a_node_did_not_recompute() {
    // x -> parity -> label.  Edit x so that parity recomputes to the same value.
    let mut db = db_with(1);
    let x = db.register_input("x", Some(1));
    let parity = db.register_query("parity", Some(1), move |ctx| Ok(ctx.read(x, 0)? % 2));
    let label = db.register_query("label", Some(1), move |ctx| Ok(ctx.read(parity, 0)? + 100));
    let xi = db.create_input(x, 0, 2);
    let l = db.node(label, 0);
    db.evaluate(&[l]).unwrap();
    db.edit("x: 2 -> 4", |tx| tx.set(xi, 4));
    db.evaluate(&[l]).unwrap();
    let ex = db.explain_recompute(l);
    assert!(ex.headline.contains("did NOT recompute"), "{}", ex.headline);
    assert!(ex.headline.contains("early cutoff") && ex.headline.contains("parity[0]"), "{}", ex.headline);
}

#[test]
fn stale_node_is_explained_as_pending() {
    let mut c = chains(2, 1);
    c.db.evaluate(&c.tops).unwrap();
    c.db.set_input(c.inputs[0], 1);
    let ex = c.db.explain_recompute(c.tops[0]);
    assert!(ex.headline.contains("stale"), "{}", ex.headline);
}

#[test]
fn cycles_are_reported_not_hung() {
    let mut db = db_with(1);
    let a = db.register_query("a", Some(1), |ctx| ctx.read(Kind(1), 0));
    let b = db.register_query("b", Some(1), |ctx| ctx.read(Kind(0), 0));
    assert_eq!((a, b), (Kind(0), Kind(1)));
    let na = db.node(a, 0);
    match db.evaluate(&[na]) {
        Err(EvalError::Cycle(path)) => {
            assert!(path.iter().any(|p| p == "a[0]") && path.iter().any(|p| p == "b[0]"), "{path:?}");
            assert_eq!(path.first(), path.last(), "a cycle path closes on itself: {path:?}");
        }
        other => panic!("expected a cycle error, got {other:?}"),
    }
    // The database is still usable and the nodes are back to fresh.
    assert_eq!(db.state_counts().0, 2);
}

#[test]
fn self_dependency_is_a_cycle() {
    let mut db = db_with(4);
    let a = db.register_query("a", Some(1), |ctx| ctx.read(Kind(0), 0));
    let n = db.node(a, 0);
    assert!(matches!(db.evaluate(&[n]), Err(EvalError::Cycle(_))));
}

#[test]
fn a_panicking_query_is_reported_and_isolated() {
    let mut db = db_with(4);
    let inp = db.register_input("x", Some(4));
    let q = db.register_query("q", Some(4), move |ctx| {
        let v = ctx.read(inp, ctx.arg())?;
        assert!(v != 3, "kernel exploded on {v}");
        Ok(v + 1)
    });
    let ins: Vec<NodeId> = (0..4).map(|i| db.create_input(inp, i, i as i64)).collect();
    let qs: Vec<NodeId> = (0..4).map(|i| db.node(q, i)).collect();
    match db.evaluate(&qs) {
        Err(EvalError::Panic { node, message }) => {
            assert_eq!(node, "q[3]");
            assert!(message.contains("kernel exploded on 3"), "{message}");
        }
        other => panic!("expected panic error, got {other:?}"),
    }
    // Fix the input; the evaluation completes and the node is usable.
    db.set_input(ins[3], 9);
    db.evaluate(&qs).unwrap();
    assert_eq!(db.value(qs[3]), Some(10));
}

#[test]
fn missing_input_is_an_error() {
    let mut db = db_with(1);
    let inp = db.register_input("x", Some(2));
    let q = db.register_query("q", Some(1), move |ctx| ctx.read(inp, 1));
    let n = db.node(q, 0);
    assert!(matches!(db.evaluate(&[n]), Err(EvalError::MissingInput(s)) if s == "x[1]"));
}

#[test]
fn priorities_run_visible_before_nearby_before_background() {
    // Every query logs when it starts; with one thread the order is fully determined.
    let order = Arc::new(std::sync::Mutex::new(Vec::<u64>::new()));
    let mut db = db_with(1);
    let inp = db.register_input("x", Some(9));
    let o2 = order.clone();
    let q = db.register_query("q", Some(9), move |ctx| {
        let a = ctx.arg();
        let v = ctx.read(inp, a)?;
        o2.lock().unwrap().push(a);
        Ok(v)
    });
    for i in 0..9 {
        db.create_input(inp, i, i as i64);
    }
    let reqs: Vec<(NodeId, Priority)> = [(0, Priority::Background), (1, Priority::Visible), (2, Priority::Nearby), (3, Priority::Visible), (4, Priority::Background)]
        .iter()
        .map(|&(i, p)| (db.node(q, i), p))
        .collect();
    let rep = db.evaluate_with(&reqs, &Default::default()).unwrap();
    let got = order.lock().unwrap().clone();
    let rank = |a: u64| match a {
        1 | 3 => 0,
        2 => 1,
        _ => 2,
    };
    assert!(got.windows(2).all(|w| rank(w[0]) <= rank(w[1])), "{got:?}");
    assert!(rep.class_done[0].unwrap() <= rep.class_done[1].unwrap());
    assert!(rep.class_done[1].unwrap() <= rep.class_done[2].unwrap());
}

#[test]
fn parallel_visible_phase_finishes_before_any_background_query_starts() {
    // 16 visible and 64 background queries on 4 threads: with ordered phases no background query
    // may start before every visible query has finished, so the visible result is not delayed by
    // the (larger) background workload.
    let starts = Arc::new((0..80).map(|_| AtomicUsize::new(usize::MAX)).collect::<Vec<_>>());
    let ends = Arc::new((0..80).map(|_| AtomicUsize::new(0)).collect::<Vec<_>>());
    let seq = Arc::new(AtomicUsize::new(0));
    let mut db = Db::<i64>::new(Config { threads: 4, serial_budget: 0, split_threshold: 2, max_nodes: 1 << 10 });
    let inp = db.register_input("x", Some(1));
    let (s2, e2, q2) = (starts.clone(), ends.clone(), seq.clone());
    let q = db.register_query("q", Some(80), move |ctx| {
        let v = ctx.read(inp, 0)?;
        let a = ctx.arg() as usize;
        s2[a].store(q2.fetch_add(1, Ordering::SeqCst), Ordering::SeqCst);
        std::thread::sleep(std::time::Duration::from_millis(2));
        e2[a].store(q2.fetch_add(1, Ordering::SeqCst), Ordering::SeqCst);
        Ok(v + a as i64)
    });
    db.create_input(inp, 0, 1);
    let reqs: Vec<(NodeId, Priority)> = (0..80).map(|i| (db.node(q, i), if i < 16 { Priority::Visible } else { Priority::Background })).collect();
    let rep = db.evaluate_with(&reqs, &Default::default()).unwrap();
    let last_visible_end = (0..16).map(|i| ends[i].load(Ordering::SeqCst)).max().unwrap();
    let first_background_start = (16..80).map(|i| starts[i].load(Ordering::SeqCst)).min().unwrap();
    assert!(last_visible_end < first_background_start, "a background query started ({first_background_start}) before the visible phase ended ({last_visible_end})");
    let visible = rep.class_done[0].unwrap();
    assert!(visible < rep.elapsed, "visible {visible:?} must be ready before the whole evaluation {:?}", rep.elapsed);
    assert!(rep.parallel);
}
