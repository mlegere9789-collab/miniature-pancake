//! Differential and property tests.
//!
//! A deliberately simple sequential reference model implements the engine's
//! revision semantics (changed_at / verified_at, early cutoff, dynamic
//! dependencies).  Random DAGs, random edit batches and random demands are run
//! against the real engine for 1 to 16 threads; after every evaluation the
//! engine must agree with the model on *every* node: value hash, state, the
//! revisions, the number of computations (exactly the same nodes recompute: no
//! missed and no unnecessary recomputation) and the recorded dependency order.

use dino8_graph::{Config, Ctx, Db, Hash128, Interrupt, Kind, NodeId, Priority};
use proptest::prelude::*;
use std::sync::Arc;

const INP: Kind = Kind(0);
const QK: Kind = Kind(1);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Src {
    In(usize),
    Q(usize),
}

#[derive(Clone, Debug)]
enum Op {
    /// Batch request of all sources, wrapping sum.
    Sum(Vec<Src>),
    /// Sequential requests (one restart per missing source), wrapping sum.
    SeqSum(Vec<Src>),
    Max(Vec<Src>),
    /// Batch request, sum modulo m (many equal results: early cutoff).
    ModSum(Vec<Src>, i64),
    /// Reads the condition, then only the selected branch (dynamic dependencies).
    Select(Src, Src, Src),
}

#[derive(Clone, Debug)]
struct Round {
    edits: Vec<(usize, i64)>,
    demand: Vec<(usize, u8)>,
}

#[derive(Clone, Debug)]
struct Scenario {
    n_in: usize,
    ops: Vec<Op>,
    rounds: Vec<Round>,
}

// ---- engine side ----------------------------------------------------------------------------

fn src_node(ctx: &Ctx<'_, i64>, s: Src) -> NodeId {
    match s {
        Src::In(i) => ctx.node(INP, i as u64),
        Src::Q(k) => ctx.node(QK, k as u64),
    }
}

fn run_op(ctx: &mut Ctx<'_, i64>, op: &Op) -> Result<i64, Interrupt> {
    match op {
        Op::Sum(srcs) => {
            let ids: Vec<NodeId> = srcs.iter().map(|&s| src_node(ctx, s)).collect();
            Ok(ctx.get_all(&ids)?.iter().fold(0i64, |a, &b| a.wrapping_add(b)))
        }
        Op::SeqSum(srcs) => {
            let mut a = 0i64;
            for &s in srcs {
                let id = src_node(ctx, s);
                a = a.wrapping_add(ctx.get(id)?);
            }
            Ok(a)
        }
        Op::Max(srcs) => {
            let mut a = i64::MIN;
            for &s in srcs {
                let id = src_node(ctx, s);
                a = a.max(ctx.get(id)?);
            }
            Ok(a)
        }
        Op::ModSum(srcs, m) => {
            let ids: Vec<NodeId> = srcs.iter().map(|&s| src_node(ctx, s)).collect();
            Ok(ctx.get_all(&ids)?.iter().fold(0i64, |a, &b| a.wrapping_add(b)).rem_euclid(*m))
        }
        Op::Select(c, a, b) => {
            let cid = src_node(ctx, *c);
            let cv = ctx.get(cid)?;
            let id = src_node(ctx, if cv % 2 == 0 { *a } else { *b });
            ctx.get(id)
        }
    }
}

struct Engine {
    db: Db<i64>,
    ins: Vec<NodeId>,
    qs: Vec<NodeId>,
}

fn build_engine(sc: &Scenario, cfg: Config) -> Engine {
    let mut db = Db::<i64>::new(cfg);
    let inp = db.register_input("in", Some(sc.n_in));
    let ops = Arc::new(sc.ops.clone());
    let q = db.register_query("q", Some(sc.ops.len()), move |ctx| {
        let j = ctx.arg() as usize;
        run_op(ctx, &ops[j])
    });
    assert_eq!((inp, q), (INP, QK));
    let ins = (0..sc.n_in).map(|i| db.create_input(inp, i as u64, i as i64 + 1)).collect();
    let qs = (0..sc.ops.len()).map(|j| db.node(q, j as u64)).collect();
    Engine { db, ins, qs }
}

// ---- reference model ------------------------------------------------------------------------

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum MState {
    Fresh,
    Stale,
    Clean,
}

#[derive(Clone, Debug)]
struct MNode {
    state: MState,
    value: i64,
    deps: Vec<Src>,
    changed_at: u32,
    verified_at: u32,
    computed_at: u32,
    computes: u32,
}

struct Model {
    rev: u32,
    ins: Vec<(i64, u32)>,
    nodes: Vec<MNode>,
    ops: Arc<Vec<Op>>,
}

impl Model {
    fn new(sc: &Scenario) -> Self {
        Model {
            rev: 1,
            ins: (0..sc.n_in).map(|i| (i as i64 + 1, 1)).collect(),
            nodes: vec![
                MNode { state: MState::Fresh, value: 0, deps: vec![], changed_at: 0, verified_at: 0, computed_at: 0, computes: 0 };
                sc.ops.len()
            ],
            ops: Arc::new(sc.ops.clone()),
        }
    }

    fn edit(&mut self, edits: &[(usize, i64)]) {
        let next = self.rev + 1;
        let mut changed = vec![false; self.ins.len()];
        let mut any = false;
        for &(i, v) in edits {
            if self.ins[i].0 == v {
                continue;
            }
            self.ins[i] = (v, next);
            changed[i] = true;
            any = true;
        }
        if !any {
            return;
        }
        self.rev = next;
        for j in 0..self.nodes.len() {
            if self.nodes[j].state != MState::Clean {
                continue;
            }
            let hit = self.nodes[j].deps.iter().any(|d| match *d {
                Src::In(i) => changed[i],
                Src::Q(k) => self.nodes[k].state == MState::Stale,
            });
            if hit {
                self.nodes[j].state = MState::Stale;
            }
        }
    }

    fn dep_state(&self, d: Src) -> (bool, u32) {
        match d {
            Src::In(i) => (true, self.ins[i].1),
            Src::Q(k) => (self.nodes[k].state == MState::Clean, self.nodes[k].changed_at),
        }
    }

    fn ensure(&mut self, j: usize) {
        match self.nodes[j].state {
            MState::Clean => {}
            MState::Fresh => self.compute(j),
            MState::Stale => {
                // Rule: bring every previous dependency up to date, then compare revisions.
                let deps = self.nodes[j].deps.clone();
                for d in &deps {
                    if let Src::Q(k) = *d {
                        self.ensure(k);
                    }
                }
                let verified_at = self.nodes[j].verified_at;
                if deps.iter().any(|&d| self.dep_state(d).1 > verified_at) {
                    self.compute(j);
                } else {
                    self.nodes[j].verified_at = self.rev;
                    self.nodes[j].state = MState::Clean;
                }
            }
        }
    }

    fn read(&mut self, s: Src, deps: &mut Vec<Src>) -> i64 {
        deps.push(s);
        match s {
            Src::In(i) => self.ins[i].0,
            Src::Q(k) => {
                self.ensure(k);
                self.nodes[k].value
            }
        }
    }

    fn compute(&mut self, j: usize) {
        let ops = self.ops.clone();
        let mut deps = Vec::new();
        let v = match &ops[j] {
            Op::Sum(srcs) | Op::SeqSum(srcs) => srcs.iter().fold(0i64, |a, &s| a.wrapping_add(self.read(s, &mut deps))),
            Op::Max(srcs) => srcs.iter().fold(i64::MIN, |a, &s| a.max(self.read(s, &mut deps))),
            Op::ModSum(srcs, m) => srcs.iter().fold(0i64, |a, &s| a.wrapping_add(self.read(s, &mut deps))).rem_euclid(*m),
            Op::Select(c, a, b) => {
                let cv = self.read(*c, &mut deps);
                self.read(if cv % 2 == 0 { *a } else { *b }, &mut deps)
            }
        };
        let mut uniq: Vec<Src> = Vec::new();
        for d in deps {
            if !uniq.contains(&d) {
                uniq.push(d);
            }
        }
        let rev = self.rev;
        let n = &mut self.nodes[j];
        let first = n.state == MState::Fresh;
        if first || n.value != v {
            n.changed_at = rev;
        }
        n.value = v;
        n.deps = uniq;
        n.verified_at = rev;
        n.computed_at = rev;
        n.computes += 1;
        n.state = MState::Clean;
    }

    fn evaluate(&mut self, demand: &[(usize, u8)]) {
        for class in 0..3u8 {
            for &(j, c) in demand {
                if c == class {
                    self.ensure(j);
                }
            }
        }
    }

    /// From-scratch evaluation (no memoization, no revisions): the ground truth for values.
    fn naive(&self, j: usize) -> i64 {
        let get = |s: Src| match s {
            Src::In(i) => self.ins[i].0,
            Src::Q(k) => self.naive(k),
        };
        match &self.ops[j] {
            Op::Sum(srcs) | Op::SeqSum(srcs) => srcs.iter().fold(0i64, |a, &s| a.wrapping_add(get(s))),
            Op::Max(srcs) => srcs.iter().fold(i64::MIN, |a, &s| a.max(get(s))),
            Op::ModSum(srcs, m) => srcs.iter().fold(0i64, |a, &s| a.wrapping_add(get(s))).rem_euclid(*m),
            Op::Select(c, a, b) => {
                if get(*c) % 2 == 0 {
                    get(*a)
                } else {
                    get(*b)
                }
            }
        }
    }
}

fn dep_key(s: Src) -> (String, u64) {
    match s {
        Src::In(i) => ("in".to_string(), i as u64),
        Src::Q(k) => ("q".to_string(), k as u64),
    }
}

fn state_str(s: MState) -> &'static str {
    match s {
        MState::Fresh => "fresh",
        MState::Stale => "stale",
        MState::Clean => "clean",
    }
}

fn compare(e: &Engine, m: &Model, what: &str) {
    let dump = e.db.canonical_dump();
    let mut seen = vec![false; m.nodes.len()];
    for rec in &dump {
        match rec.kind.as_str() {
            "in" => {
                let (v, ch) = m.ins[rec.key as usize];
                assert_eq!(rec.hash, v.hash128_i64(), "{what}: input {} value", rec.key);
                assert_eq!(rec.changed_at, ch, "{what}: input {} changed_at", rec.key);
            }
            "q" => {
                let j = rec.key as usize;
                seen[j] = true;
                let n = &m.nodes[j];
                assert_eq!(rec.state, state_str(n.state), "{what}: q[{j}] state");
                assert_eq!(rec.computes, n.computes, "{what}: q[{j}] number of computations (exact recompute set)");
                if n.state != MState::Fresh {
                    assert_eq!(rec.hash, n.value.hash128_i64(), "{what}: q[{j}] value hash");
                    assert_eq!(rec.changed_at, n.changed_at, "{what}: q[{j}] changed_at");
                    assert_eq!(rec.verified_at, n.verified_at, "{what}: q[{j}] verified_at");
                    assert_eq!(rec.computed_at, n.computed_at, "{what}: q[{j}] computed_at");
                    let want: Vec<(String, u64)> = n.deps.iter().map(|&d| dep_key(d)).collect();
                    assert_eq!(rec.deps, want, "{what}: q[{j}] dependency order");
                }
            }
            other => panic!("unexpected kind {other}"),
        }
    }
    for (j, n) in m.nodes.iter().enumerate() {
        if !seen[j] {
            assert_eq!((n.state, n.computes), (MState::Fresh, 0), "{what}: q[{j}] missing in the engine but computed in the model");
        }
    }
    for (j, n) in m.nodes.iter().enumerate() {
        if n.state == MState::Clean {
            assert_eq!(n.value, m.naive(j), "{what}: model value of q[{j}] differs from from-scratch evaluation");
            assert_eq!(e.db.value(e.qs[j]), Some(n.value), "{what}: engine value of q[{j}]");
        }
    }
}

trait H128 {
    fn hash128_i64(&self) -> Hash128;
}
impl H128 for i64 {
    fn hash128_i64(&self) -> Hash128 {
        use dino8_graph::ContentHash;
        self.hash128()
    }
}

const CONFIGS: [(usize, usize, usize); 6] = [(1, 512, 32), (2, 0, 2), (3, 1, 2), (4, 0, 4), (8, 2, 2), (16, 0, 2)];

fn run_scenario(sc: &Scenario, check_each_round: bool) {
    for (threads, serial_budget, split_threshold) in CONFIGS {
        let cfg = Config { threads, serial_budget, split_threshold, max_nodes: 1 << 14 };
        let mut e = build_engine(sc, cfg);
        let mut m = Model::new(sc);
        for (r, round) in sc.rounds.iter().enumerate() {
            let what = format!("threads={threads} budget={serial_budget} split={split_threshold} round={r}");
            let edits = round.edits.clone();
            let ins = e.ins.clone();
            e.db.edit("round", |tx| {
                for &(i, v) in &edits {
                    tx.set(ins[i], v);
                }
            });
            m.edit(&round.edits);
            let reqs: Vec<(NodeId, Priority)> = round
                .demand
                .iter()
                .map(|&(j, c)| (e.qs[j], [Priority::Visible, Priority::Nearby, Priority::Background][c as usize % 3]))
                .collect();
            let demand: Vec<(usize, u8)> = round.demand.iter().map(|&(j, c)| (j, c % 3)).collect();
            e.db.evaluate_with(&reqs, &Default::default()).unwrap_or_else(|err| panic!("{what}: {err}"));
            m.evaluate(&demand);
            if check_each_round || r + 1 == sc.rounds.len() {
                compare(&e, &m, &what);
            }
        }
    }
}

// ---- proptest strategies --------------------------------------------------------------------

fn build_scenario(
    n_in: usize,
    raw_ops: Vec<(u8, Vec<u32>, i64)>,
    raw_rounds: Vec<(Vec<(usize, i64)>, Vec<(usize, u8)>)>,
) -> Scenario {
    let src = |j: usize, s: u32| -> Src {
        if s % 3 == 0 || j == 0 {
            Src::In(s as usize % n_in)
        } else {
            Src::Q(s as usize % j)
        }
    };
    let ops = raw_ops
        .iter()
        .enumerate()
        .map(|(j, (kind, srcs, m))| {
            let v: Vec<Src> = srcs.iter().map(|&s| src(j, s)).collect();
            match kind % 5 {
                0 => Op::Sum(v),
                1 => Op::SeqSum(v),
                2 => Op::Max(v),
                3 => Op::ModSum(v, *m),
                _ => {
                    let g = |i: usize| v[i % v.len()];
                    Op::Select(g(0), g(1), g(2))
                }
            }
        })
        .collect::<Vec<_>>();
    let n_q = ops.len();
    let rounds = raw_rounds
        .into_iter()
        .map(|(edits, demand)| Round {
            edits: edits.into_iter().map(|(i, v)| (i % n_in, v)).collect(),
            demand: demand.into_iter().map(|(j, c)| (j % n_q, c)).collect(),
        })
        .collect();
    Scenario { n_in, ops, rounds }
}

fn scenario_strategy() -> impl Strategy<Value = Scenario> {
    (2usize..8, 3usize..40).prop_flat_map(|(n_in, n_q)| {
        let raw_op = (0u8..5, prop::collection::vec(any::<u32>(), 1..5), 2i64..6);
        let ops = prop::collection::vec(raw_op, n_q..=n_q);
        let round = (
            prop::collection::vec((0usize..64, -3i64..4), 0..4),
            prop::collection::vec((0usize..64, 0u8..3), 1..8),
        );
        let rounds = prop::collection::vec(round, 1..8);
        (Just(n_in), ops, rounds).prop_map(|(n_in, ops, rounds)| build_scenario(n_in, ops, rounds))
    })
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 48, max_shrink_iters: 200, .. ProptestConfig::default() })]

    /// The engine agrees with the sequential reference on every node after every round, for 1 to 16 threads.
    #[test]
    fn engine_matches_reference_model(sc in scenario_strategy()) {
        run_scenario(&sc, true);
    }

    /// Editing an input that is not in the transitive dependencies of the demanded roots never recomputes anything.
    #[test]
    fn unrelated_edits_never_recompute(sc in scenario_strategy(), v in -50i64..50) {
        let mut e = build_engine(&sc, Config { threads: 2, serial_budget: 0, ..Config::default() });
        let roots: Vec<NodeId> = e.qs.iter().copied().take(3).collect();
        e.db.evaluate(&roots).unwrap();
        // Inputs reachable from the roots through the recorded dependency lists.
        let dump = e.db.canonical_dump();
        let mut reach_in = std::collections::HashSet::new();
        let mut stack: Vec<usize> = (0..roots.len()).collect();
        let mut seen_q = std::collections::HashSet::new();
        while let Some(j) = stack.pop() {
            if !seen_q.insert(j) { continue; }
            let rec = dump.iter().find(|r| r.kind == "q" && r.key == j as u64).unwrap();
            for (k, a) in &rec.deps {
                if k == "in" { reach_in.insert(*a as usize); } else { stack.push(*a as usize); }
            }
        }
        if let Some(i) = (0..sc.n_in).find(|i| !reach_in.contains(i)) {
            let before: Vec<u32> = e.qs.iter().map(|&q| e.db.computes(q)).collect();
            e.db.set_input(e.ins[i], v + 1000);
            let rep = e.db.evaluate(&roots).unwrap();
            prop_assert_eq!((rep.computed, rep.verified, rep.steps), (0, 0, 0));
            let after: Vec<u32> = e.qs.iter().map(|&q| e.db.computes(q)).collect();
            prop_assert_eq!(before, after);
        }
    }
}

// ---- larger deterministic scenarios -----------------------------------------------------------

struct Lcg(u64);
impl Lcg {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        self.0 >> 33
    }
    fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }
}

fn big_scenario(seed: u64, n_in: usize, n_q: usize, rounds: usize) -> Scenario {
    let mut r = Lcg(seed);
    let raw_ops: Vec<(u8, Vec<u32>, i64)> = (0..n_q)
        .map(|_| {
            let k = 1 + r.below(5);
            ((r.next() % 5) as u8, (0..k).map(|_| r.next() as u32).collect(), 2 + (r.next() % 4) as i64)
        })
        .collect();
    let raw_rounds = (0..rounds)
        .map(|_| {
            let edits = (0..r.below(6)).map(|_| (r.below(n_in), (r.next() % 7) as i64 - 3)).collect();
            let demand = (0..1 + r.below(12)).map(|_| (r.below(n_q), (r.next() % 3) as u8)).collect();
            (edits, demand)
        })
        .collect();
    build_scenario(n_in, raw_ops, raw_rounds)
}

#[test]
fn big_graphs_match_the_model_for_every_thread_configuration() {
    for seed in 1..=6u64 {
        let sc = big_scenario(seed, 24, 600, 10);
        run_scenario(&sc, true);
    }
}

#[test]
fn identical_results_for_one_to_sixteen_threads() {
    // Same edits and demands, thread counts 1..=16: the full database (values, revisions, dependency
    // order, number of computations per node) must be bit-identical.
    let sc = big_scenario(99, 40, 3000, 8);
    let mut digests = Vec::new();
    for threads in 1..=16usize {
        let cfg = Config { threads, serial_budget: 0, split_threshold: 4, max_nodes: 1 << 15 };
        let mut e = build_engine(&sc, cfg);
        for round in &sc.rounds {
            let ins = e.ins.clone();
            e.db.edit("round", |tx| {
                for &(i, v) in &round.edits {
                    tx.set(ins[i], v);
                }
            });
            let reqs: Vec<(NodeId, Priority)> = round
                .demand
                .iter()
                .map(|&(j, c)| (e.qs[j], [Priority::Visible, Priority::Nearby, Priority::Background][c as usize % 3]))
                .collect();
            e.db.evaluate_with(&reqs, &Default::default()).unwrap();
        }
        digests.push((threads, e.db.digest(), e.db.node_count()));
    }
    let first = digests[0];
    for d in &digests {
        assert_eq!((d.1, d.2), (first.1, first.2), "threads={} differs from threads=1", d.0);
    }
}

#[test]
fn full_demand_equals_from_scratch_after_many_edits() {
    // Demand everything after a long edit history: every value equals a from-scratch evaluation and
    // the number of computations never exceeds (nodes x edits) while staying far below it.
    let sc = big_scenario(7, 30, 1500, 1);
    let mut e = build_engine(&sc, Config { threads: 4, serial_budget: 8, max_nodes: 1 << 15, ..Config::default() });
    let mut m = Model::new(&sc);
    let all: Vec<NodeId> = e.qs.clone();
    e.db.evaluate(&all).unwrap();
    m.evaluate(&(0..sc.ops.len()).map(|j| (j, 0)).collect::<Vec<_>>());
    let mut r = Lcg(5);
    for round in 0..30 {
        let edits: Vec<(usize, i64)> = (0..1 + r.below(3)).map(|_| (r.below(sc.n_in), (r.next() % 9) as i64 - 4)).collect();
        let ins = e.ins.clone();
        e.db.edit("full", |tx| {
            for &(i, v) in &edits {
                tx.set(ins[i], v);
            }
        });
        m.edit(&edits);
        e.db.evaluate(&all).unwrap();
        m.evaluate(&(0..sc.ops.len()).map(|j| (j, 0)).collect::<Vec<_>>());
        compare(&e, &m, &format!("full round {round}"));
    }
    let total: u64 = e.db.computes_by_kind().iter().map(|(_, n)| n).sum();
    assert!(total < 1500 * 31 / 2, "incremental evaluation recomputed {total} nodes, close to a global recompute per edit");
}
