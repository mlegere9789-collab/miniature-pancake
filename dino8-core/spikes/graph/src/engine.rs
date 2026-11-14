//! The incremental derived-data engine (Spike C).
//!
//! # Model
//!
//! * **Inputs** are revision-numbered cells.  Setting an input whose content hash
//!   is unchanged is a no-op; otherwise the database revision advances and every
//!   transitive dependent is marked `Stale` through the recorded reverse edges.
//! * **Queries** are pure functions of `(kind, argument)`.  A query reads other
//!   nodes through its [`Ctx`]; every read is recorded as a dependency.  The
//!   dependency list is rebuilt on every recomputation, so dynamic dependencies
//!   (a query that reads different nodes depending on a value) are exact.
//! * **Content queries** are keyed by the content hash of their argument value
//!   and are shared by every caller that passes an equal argument.
//! * **Fine-grained invalidation with early cutoff.**  A stale node first
//!   *verifies*: if every dependency is unchanged since the node was last
//!   verified it is revalidated without running its function.  A node that is
//!   recomputed to an equal content hash keeps its `changed_at` revision, so its
//!   dependents are not recomputed.
//! * **Evaluation is demand-driven and restart-based.**  A query function that
//!   asks for a dependency that is not up to date gets `Err(Interrupt::Missing)`;
//!   the scheduler evaluates the missing dependencies (in parallel) and re-runs
//!   the function.  Query functions therefore never block, which makes parallel
//!   evaluation on a work-stealing pool deadlock free, makes cancellation clean
//!   (no half-written state) and lets kernel functions be called as plain
//!   `inputs -> output` functions with no call back into the engine.
//! * **Priorities** (`Visible`, `Nearby`, `Background`) are evaluated as ordered
//!   phases sharing one cancellation token; **cancellation** is cooperative and
//!   leaves the database consistent.
//! * **Explanation**: every node remembers the dependency whose change caused its
//!   last recomputation, so [`Db::explain_recompute`] can answer "why did this
//!   recompute?" back to the edited input.

use crate::hash::{ContentHash, ContentHasher, Hash128};
use std::collections::HashMap;
use std::fmt;
use std::hash::{BuildHasherDefault, Hasher};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};
use std::time::{Duration, Instant};

/// Database revision.  Advances by one for every edit batch that changes an input.
pub type Rev = u32;

/// Values stored in the graph.
pub trait Value: ContentHash + Clone + Send + Sync + 'static {}
impl<T: ContentHash + Clone + Send + Sync + 'static> Value for T {}

/// Handle of a node (input or query result).
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug, PartialOrd, Ord)]
pub struct NodeId(pub u32);

/// Handle of a registered kind (a named input table or query function).
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct Kind(pub u16);

/// Scheduling class of a demand.  Classes are evaluated in this order.
#[derive(Clone, Copy, PartialEq, Eq, Debug, PartialOrd, Ord, Hash)]
pub enum Priority {
    Visible = 0,
    Nearby = 1,
    Background = 2,
}

/// Why a query function stopped before producing a value.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Interrupt {
    /// A dependency is not up to date; the scheduler will evaluate it and re-run the function.
    Missing,
    /// Cooperative cancellation was requested.
    Cancelled,
}

/// Errors from [`Db::evaluate_with`].
#[derive(Clone, PartialEq, Eq, Debug)]
pub enum EvalError {
    Cancelled,
    Panic { node: String, message: String },
    Cycle(Vec<String>),
    MissingInput(String),
    Stalled(String),
}

impl fmt::Display for EvalError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            EvalError::Cancelled => write!(f, "evaluation cancelled"),
            EvalError::Panic { node, message } => write!(f, "query {node} panicked: {message}"),
            EvalError::Cycle(path) => write!(f, "dependency cycle: {}", path.join(" -> ")),
            EvalError::MissingInput(n) => write!(f, "input {n} was never created"),
            EvalError::Stalled(s) => write!(f, "evaluation stalled: {s}"),
        }
    }
}

impl std::error::Error for EvalError {}

/// Shared cooperative cancellation flag.
#[derive(Clone, Debug, Default)]
pub struct CancelToken(Arc<AtomicBool>);

impl CancelToken {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn cancel(&self) {
        self.0.store(true, Ordering::Release);
    }
    pub fn is_cancelled(&self) -> bool {
        self.0.load(Ordering::Acquire)
    }
    pub fn reset(&self) {
        self.0.store(false, Ordering::Release);
    }
}

/// Engine configuration.
#[derive(Clone, Debug)]
pub struct Config {
    /// Worker threads of the evaluation pool.  `1` evaluates on the calling thread only.
    pub threads: usize,
    /// Upper bound on the number of nodes (inputs plus query results).
    pub max_nodes: usize,
    /// Number of node steps the calling thread runs serially before the remaining
    /// work is handed to the pool.  Small edits therefore never pay for a thread wake-up.
    pub serial_budget: usize,
    /// A worker whose local stack exceeds this many ready nodes splits off the older half.
    pub split_threshold: usize,
}

impl Default for Config {
    fn default() -> Self {
        Self { threads: 1, max_nodes: 1 << 16, serial_budget: 512, split_threshold: 32 }
    }
}

/// Options of one evaluation call.
#[derive(Clone, Default)]
pub struct EvalOptions<'a> {
    pub cancel: Option<&'a CancelToken>,
    /// Override of [`Config::serial_budget`].
    pub serial_budget: Option<usize>,
    /// Never use the pool (useful for the baseline comparison).
    pub force_serial: bool,
}

/// Summary of one evaluation call.
#[derive(Clone, Debug, Default)]
pub struct EvalReport {
    /// Scheduler steps (one attempt to advance a node).
    pub steps: u64,
    /// Query functions that ran to completion and stored a value.
    pub computed: u64,
    /// Stale nodes revalidated without running their function.
    pub verified: u64,
    /// Recomputations whose result was equal to the old value (dependents are not invalidated).
    pub cutoffs: u64,
    /// Function runs that stopped with `Missing` and were re-run later.
    pub restarts: u64,
    /// Nodes that were claimed for evaluation.
    pub claimed: u64,
    pub elapsed: Duration,
    /// Time at which each priority class was complete.
    pub class_done: [Option<Duration>; 3],
    /// Whether the pool was used.
    pub parallel: bool,
}

/// Summary of one edit transaction.
#[derive(Clone, Debug, Default)]
pub struct EditReport {
    pub rev: Rev,
    pub changed: usize,
    pub marked_stale: usize,
    pub elapsed: Duration,
}

// ------------------------------------------------------------------------------------------------
// Storage
// ------------------------------------------------------------------------------------------------

const CHUNK_BITS: u32 = 10;
const CHUNK: usize = 1 << CHUNK_BITS;
const CHUNK_MASK: u32 = (1 << CHUNK_BITS) - 1;
const NONE: u32 = u32::MAX;
const CREATING: u32 = u32::MAX - 1;
const SHARDS: usize = 64;
const ROOT_CHUNK: usize = 256;

#[inline]
fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(std::sync::PoisonError::into_inner)
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum State {
    /// Never computed.
    Fresh,
    /// Computed before, a transitive dependency has been edited since.
    Stale,
    /// Up to date.
    Clean,
    /// Claimed by the scheduler.
    Running,
    /// Waiting for dependencies.
    Parked,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
enum Cause {
    #[default]
    None,
    Set,
    Initial,
    Dep(NodeId),
}

struct Inner<V> {
    state: State,
    resume: State,
    kind: u16,
    arg: u64,
    changed_at: Rev,
    verified_at: Rev,
    computed_at: Rev,
    computes: u32,
    hash: Hash128,
    prev_hash: u64,
    value: Option<V>,
    arg_value: Option<Box<V>>,
    deps: Vec<NodeId>,
    rdeps: Vec<NodeId>,
    waiters: Vec<NodeId>,
    cause: Cause,
    trigger: Cause,
}

struct Node<V> {
    pending: AtomicU32,
    inner: Mutex<Inner<V>>,
}

impl<V> Node<V> {
    fn new() -> Self {
        Self {
            pending: AtomicU32::new(0),
            inner: Mutex::new(Inner {
                state: State::Fresh,
                resume: State::Fresh,
                kind: 0,
                arg: 0,
                changed_at: 0,
                verified_at: 0,
                computed_at: 0,
                computes: 0,
                hash: Hash128::ZERO,
                prev_hash: 0,
                value: None,
                arg_value: None,
                deps: Vec::new(),
                rdeps: Vec::new(),
                waiters: Vec::new(),
                cause: Cause::None,
                trigger: Cause::None,
            }),
        }
    }
}

struct Arena<V> {
    chunks: Box<[OnceLock<Box<[Node<V>]>>]>,
    len: AtomicU32,
    cap: u32,
}

impl<V> Arena<V> {
    fn new(max_nodes: usize) -> Self {
        let n_chunks = max_nodes.div_ceil(CHUNK).max(1);
        let cap = u32::try_from(n_chunks * CHUNK).expect("max_nodes fits in u32");
        Self { chunks: (0..n_chunks).map(|_| OnceLock::new()).collect(), len: AtomicU32::new(0), cap }
    }

    fn alloc(&self) -> NodeId {
        let i = self.len.fetch_add(1, Ordering::Relaxed);
        assert!(i < self.cap, "node arena exhausted: raise Config::max_nodes");
        self.chunks[(i >> CHUNK_BITS) as usize].get_or_init(|| (0..CHUNK).map(|_| Node::new()).collect());
        NodeId(i)
    }

    #[inline]
    fn get(&self, id: NodeId) -> &Node<V> {
        let chunk = self.chunks[(id.0 >> CHUNK_BITS) as usize].get().expect("node chunk allocated");
        &chunk[(id.0 & CHUNK_MASK) as usize]
    }

    fn len(&self) -> usize {
        (self.len.load(Ordering::Relaxed) as usize).min(self.cap as usize)
    }
}

#[derive(Default)]
struct IdHasher(u64);

impl Hasher for IdHasher {
    fn finish(&self) -> u64 {
        self.0
    }
    fn write(&mut self, bytes: &[u8]) {
        for &b in bytes {
            self.write_u64(u64::from(b));
        }
    }
    fn write_u64(&mut self, x: u64) {
        self.0 = (self.0.rotate_left(5) ^ x).wrapping_mul(0x9E37_79B9_7F4A_7C15);
    }
}

type IdBuild = BuildHasherDefault<IdHasher>;
type ShardMap<K> = Box<[Mutex<HashMap<K, NodeId, IdBuild>>]>;

fn new_shards<K>() -> ShardMap<K> {
    (0..SHARDS).map(|_| Mutex::new(HashMap::with_hasher(IdBuild::default()))).collect()
}

enum Index {
    Dense(Box<[AtomicU32]>),
    Sparse(ShardMap<u64>),
    Content(ShardMap<Hash128>),
}

/// Whether a kind is an input table, an id-keyed query or a content-keyed query.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum KindClass {
    Input,
    Query,
    Content,
}

/// A query function.  It must be pure: the result may depend only on its argument
/// and on the values it reads through the context.
pub type ComputeFn<V> = Arc<dyn for<'a> Fn(&mut Ctx<'a, V>) -> Result<V, Interrupt> + Send + Sync>;

struct KindInfo<V: Value> {
    name: String,
    class: KindClass,
    index: Index,
    compute: Option<ComputeFn<V>>,
}

// ------------------------------------------------------------------------------------------------
// Evaluation context
// ------------------------------------------------------------------------------------------------

#[derive(Default)]
struct Local {
    claimed: Vec<NodeId>,
    steps: u64,
    computed: u64,
    verified: u64,
    cutoffs: u64,
    restarts: u64,
}

#[derive(Default)]
struct Totals {
    steps: u64,
    computed: u64,
    verified: u64,
    cutoffs: u64,
    restarts: u64,
    claimed: u64,
}

struct Eval<'a, V: Value> {
    db: &'a Db<V>,
    rev: Rev,
    token: Option<&'a CancelToken>,
    internal_stop: AtomicBool,
    aborted: AtomicBool,
    error: Mutex<Option<EvalError>>,
    journal: Mutex<Vec<NodeId>>,
    totals: Mutex<Totals>,
    serial_budget: usize,
    force_serial: bool,
}

impl<V: Value> Eval<'_, V> {
    #[inline]
    fn should_stop(&self) -> bool {
        self.internal_stop.load(Ordering::Relaxed) || self.token.is_some_and(CancelToken::is_cancelled)
    }

    /// True (and remembered) when evaluation has to stop; used at the points where work is abandoned.
    #[inline]
    fn stop_seen(&self) -> bool {
        if self.should_stop() {
            self.aborted.store(true, Ordering::Relaxed);
            true
        } else {
            false
        }
    }

    fn fail(&self, e: EvalError) {
        let mut g = lock(&self.error);
        if g.is_none() {
            *g = Some(e);
        }
        drop(g);
        self.internal_stop.store(true, Ordering::Relaxed);
        self.aborted.store(true, Ordering::Relaxed);
    }

    fn merge(&self, ls: Local) {
        let mut t = lock(&self.totals);
        t.steps += ls.steps;
        t.computed += ls.computed;
        t.verified += ls.verified;
        t.cutoffs += ls.cutoffs;
        t.restarts += ls.restarts;
        t.claimed += ls.claimed.len() as u64;
        drop(t);
        if !ls.claimed.is_empty() {
            lock(&self.journal).extend(ls.claimed);
        }
    }
}

/// The view of the database a query function gets.  Every successful read is
/// recorded as a dependency of the node being computed.
pub struct Ctx<'a, V: Value> {
    ev: &'a Eval<'a, V>,
    arg: u64,
    arg_value: Option<V>,
    deps: Vec<NodeId>,
    dep_changed: Vec<Rev>,
    missing: Vec<NodeId>,
}

impl<V: Value> Ctx<'_, V> {
    /// The integer argument of the node being computed.
    pub fn arg(&self) -> u64 {
        self.arg
    }

    /// The argument value of a content-keyed node.
    pub fn arg_value(&self) -> Option<&V> {
        self.arg_value.as_ref()
    }

    /// Finds or creates the node `(kind, arg)`.  Creating a node does not evaluate it.
    pub fn node(&self, kind: Kind, arg: u64) -> NodeId {
        self.ev.db.node(kind, arg)
    }

    /// Cooperative cancellation point for long-running query functions.
    pub fn check_cancel(&self) -> Result<(), Interrupt> {
        if self.ev.should_stop() {
            Err(Interrupt::Cancelled)
        } else {
            Ok(())
        }
    }

    /// Reads a dependency (clones its value).  Returns `Err(Interrupt::Missing)` when the
    /// dependency is not up to date; propagate it with `?` or keep requesting other
    /// dependencies first (they will all be evaluated before the function is re-run).
    pub fn get(&mut self, id: NodeId) -> Result<V, Interrupt> {
        self.with(id, V::clone)
    }

    /// Reads a dependency without cloning; `f` runs while the node is locked, keep it short.
    pub fn with<R>(&mut self, id: NodeId, f: impl FnOnce(&V) -> R) -> Result<R, Interrupt> {
        let g = lock(&self.ev.db.arena.get(id).inner);
        if g.state == State::Clean {
            let r = f(g.value.as_ref().expect("clean node holds a value"));
            let ch = g.changed_at;
            drop(g);
            self.deps.push(id);
            self.dep_changed.push(ch);
            Ok(r)
        } else {
            drop(g);
            self.missing.push(id);
            Err(Interrupt::Missing)
        }
    }

    /// Convenience: `get(node(kind, arg))`.
    pub fn read(&mut self, kind: Kind, arg: u64) -> Result<V, Interrupt> {
        let id = self.node(kind, arg);
        self.get(id)
    }

    /// Requests all `ids` at once (so that the scheduler can evaluate every missing one in
    /// parallel) and returns their values.
    pub fn get_all(&mut self, ids: &[NodeId]) -> Result<Vec<V>, Interrupt> {
        let mut out = Vec::with_capacity(ids.len());
        let mut ok = true;
        for &id in ids {
            match self.get(id) {
                Ok(v) => out.push(v),
                Err(_) => ok = false,
            }
        }
        if ok {
            Ok(out)
        } else {
            Err(Interrupt::Missing)
        }
    }

    /// Requests all `ids` at once and visits the values without cloning them.
    pub fn visit_all(&mut self, ids: &[NodeId], mut f: impl FnMut(usize, &V)) -> Result<(), Interrupt> {
        let mut ok = true;
        for (i, &id) in ids.iter().enumerate() {
            if self.with(id, |v| f(i, v)).is_err() {
                ok = false;
            }
        }
        if ok {
            Ok(())
        } else {
            Err(Interrupt::Missing)
        }
    }

    /// Content-keyed memoized call: the result of `kind` for exactly this argument value,
    /// shared by every caller that passes an equal argument.
    pub fn content(&mut self, kind: Kind, arg: &V) -> Result<V, Interrupt> {
        let id = self.ev.db.content_node(kind, arg);
        self.get(id)
    }
}

// ------------------------------------------------------------------------------------------------
// Explanation types
// ------------------------------------------------------------------------------------------------

/// Printable identity of a node.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NodeLabel {
    pub id: NodeId,
    pub kind: String,
    pub arg: u64,
}

impl fmt::Display for NodeLabel {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}[{}]", self.kind, self.arg)
    }
}

/// One link of a causal chain.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Why {
    InputSet { rev: Rev, label: Option<String> },
    FirstEvaluation { rev: Rev },
    DependencyChanged { rev: Rev, dep: NodeLabel, dep_changed_at: Rev },
    NoChangedDependency { rev: Rev },
    NeverEvaluated,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ExplainStep {
    pub node: NodeLabel,
    pub why: Why,
    /// Whether the recomputation produced a different value (false means early cutoff).
    pub value_changed: bool,
}

/// Answer to "why did this recompute?" (or "why did it not?").
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Explanation {
    pub headline: String,
    pub steps: Vec<ExplainStep>,
}

impl fmt::Display for Explanation {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "{}", self.headline)?;
        for (i, s) in self.steps.iter().enumerate() {
            let mark = if s.value_changed { "changed" } else { "unchanged (early cutoff)" };
            match &s.why {
                Why::InputSet { rev, label } => {
                    let l = label.as_deref().unwrap_or("edit");
                    writeln!(f, "  {i}. {} was set at r{rev} by \"{l}\"", s.node)?;
                }
                Why::FirstEvaluation { rev } => {
                    writeln!(f, "  {i}. {} was evaluated for the first time at r{rev}", s.node)?;
                }
                Why::DependencyChanged { rev, dep, dep_changed_at } => {
                    writeln!(
                        f,
                        "  {i}. {} recomputed at r{rev} ({mark}) because {dep} changed at r{dep_changed_at}",
                        s.node
                    )?;
                }
                Why::NoChangedDependency { rev } => {
                    writeln!(f, "  {i}. {} recomputed at r{rev} ({mark}); no recorded changed dependency", s.node)?;
                }
                Why::NeverEvaluated => writeln!(f, "  {i}. {} has never been evaluated", s.node)?,
            }
        }
        Ok(())
    }
}

/// Read-only description of a node for tests and tooling.
#[derive(Clone, Debug)]
pub struct NodeInfo {
    pub label: NodeLabel,
    pub state: &'static str,
    pub computes: u32,
    pub changed_at: Rev,
    pub verified_at: Rev,
    pub computed_at: Rev,
    pub hash: Hash128,
    pub deps: Vec<NodeId>,
    pub n_rdeps: usize,
}

/// Canonical, id-independent record of a node (the same for every thread count).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NodeRecord {
    pub kind: String,
    pub key: u64,
    pub state: &'static str,
    pub hash: Hash128,
    pub changed_at: Rev,
    pub verified_at: Rev,
    pub computed_at: Rev,
    pub computes: u32,
    pub deps: Vec<(String, u64)>,
}

fn state_name(s: State) -> &'static str {
    match s {
        State::Fresh => "fresh",
        State::Stale => "stale",
        State::Clean => "clean",
        State::Running => "running",
        State::Parked => "parked",
    }
}

// ------------------------------------------------------------------------------------------------
// The database
// ------------------------------------------------------------------------------------------------

/// The incremental database.
pub struct Db<V: Value> {
    arena: Arena<V>,
    kinds: Vec<KindInfo<V>>,
    rev: Rev,
    rev_log: Vec<(Rev, String)>,
    pool: Option<rayon::ThreadPool>,
    cfg: Config,
    eval_lock: Mutex<()>,
}

struct Commit<V> {
    value: V,
    deps: Vec<NodeId>,
    dep_changed: Vec<Rev>,
    old_deps: Vec<NodeId>,
    prev_verified: Rev,
    first: bool,
}

/// An edit transaction: several input changes that share one revision.
pub struct Edit<'a, V: Value> {
    db: &'a mut Db<V>,
    changed: Vec<NodeId>,
    next: Rev,
}

impl<V: Value> Edit<'_, V> {
    /// Sets an input.  Returns false (and changes nothing) when the content hash is unchanged.
    pub fn set(&mut self, id: NodeId, value: V) -> bool {
        let h = value.hash128();
        let node = self.db.arena.get(id);
        let mut g = lock(&node.inner);
        assert!(
            self.db.kinds[g.kind as usize].class == KindClass::Input,
            "set() on a node that is not an input"
        );
        if g.value.is_some() && g.hash == h {
            return false;
        }
        let already = g.changed_at == self.next && g.value.is_some();
        g.prev_hash = g.hash.lo;
        g.value = Some(value);
        g.hash = h;
        g.changed_at = self.next;
        g.verified_at = self.next;
        g.computed_at = self.next;
        g.state = State::Clean;
        g.cause = Cause::Set;
        g.trigger = Cause::Set;
        drop(g);
        if !already {
            self.changed.push(id);
        }
        true
    }

    /// The revision this transaction will have if it changes anything.
    pub fn revision(&self) -> Rev {
        self.next
    }
}

impl<V: Value> Db<V> {
    pub fn new(cfg: Config) -> Self {
        let pool = if cfg.threads > 1 {
            Some(
                rayon::ThreadPoolBuilder::new()
                    .num_threads(cfg.threads)
                    .thread_name(|i| format!("dino8-graph-{i}"))
                    .build()
                    .expect("rayon pool"),
            )
        } else {
            None
        };
        Self {
            arena: Arena::new(cfg.max_nodes),
            kinds: Vec::new(),
            rev: 1,
            rev_log: vec![(1, "initial".to_string())],
            pool,
            cfg,
            eval_lock: Mutex::new(()),
        }
    }

    pub fn config(&self) -> &Config {
        &self.cfg
    }

    pub fn rev(&self) -> Rev {
        self.rev
    }

    pub fn node_count(&self) -> usize {
        self.arena.len()
    }

    /// Approximate size of one node record in bytes (without heap-allocated edges and values).
    pub fn node_record_size() -> usize {
        std::mem::size_of::<Node<V>>()
    }

    // ---- registration -------------------------------------------------------------------------

    fn register(&mut self, name: &str, class: KindClass, capacity: Option<usize>, compute: Option<ComputeFn<V>>) -> Kind {
        let index = match (class, capacity) {
            (KindClass::Content, _) => Index::Content(new_shards()),
            (_, Some(n)) => Index::Dense((0..n).map(|_| AtomicU32::new(NONE)).collect()),
            (_, None) => Index::Sparse(new_shards()),
        };
        let id = u16::try_from(self.kinds.len()).expect("too many kinds");
        self.kinds.push(KindInfo { name: name.to_string(), class, index, compute });
        Kind(id)
    }

    /// Registers an input table.  `capacity` makes the table dense (arguments `0..capacity`).
    pub fn register_input(&mut self, name: &str, capacity: Option<usize>) -> Kind {
        self.register(name, KindClass::Input, capacity, None)
    }

    /// Registers an id-keyed query.
    pub fn register_query(
        &mut self,
        name: &str,
        capacity: Option<usize>,
        f: impl for<'a> Fn(&mut Ctx<'a, V>) -> Result<V, Interrupt> + Send + Sync + 'static,
    ) -> Kind {
        self.register(name, KindClass::Query, capacity, Some(Arc::new(f)))
    }

    /// Registers a content-keyed query (memoized by the content hash of its argument value).
    pub fn register_content_query(
        &mut self,
        name: &str,
        f: impl for<'a> Fn(&mut Ctx<'a, V>) -> Result<V, Interrupt> + Send + Sync + 'static,
    ) -> Kind {
        self.register(name, KindClass::Content, None, Some(Arc::new(f)))
    }

    pub fn kind_name(&self, kind: Kind) -> &str {
        &self.kinds[kind.0 as usize].name
    }

    // ---- node lookup --------------------------------------------------------------------------

    fn alloc_node(&self, kind: Kind, arg: u64, arg_value: Option<Box<V>>) -> NodeId {
        let id = self.arena.alloc();
        let mut g = lock(&self.arena.get(id).inner);
        g.kind = kind.0;
        g.arg = arg;
        g.arg_value = arg_value;
        id
    }

    #[inline]
    fn shard_u64(key: u64) -> usize {
        (key.wrapping_mul(0x9E37_79B9_7F4A_7C15) >> 58) as usize % SHARDS
    }

    /// Finds or creates the node `(kind, arg)`.
    pub fn node(&self, kind: Kind, arg: u64) -> NodeId {
        let info = &self.kinds[kind.0 as usize];
        match &info.index {
            Index::Dense(table) => {
                let slot = &table[usize::try_from(arg).expect("dense argument")];
                loop {
                    let cur = slot.load(Ordering::Acquire);
                    if cur < CREATING {
                        return NodeId(cur);
                    }
                    if cur == NONE {
                        if slot.compare_exchange(NONE, CREATING, Ordering::AcqRel, Ordering::Acquire).is_ok() {
                            let id = self.alloc_node(kind, arg, None);
                            slot.store(id.0, Ordering::Release);
                            return id;
                        }
                    } else {
                        std::hint::spin_loop();
                    }
                }
            }
            Index::Sparse(shards) => {
                let mut m = lock(&shards[Self::shard_u64(arg)]);
                *m.entry(arg).or_insert_with(|| self.alloc_node(kind, arg, None))
            }
            Index::Content(_) => panic!("kind {} is content-keyed; use content_node", info.name),
        }
    }

    /// Finds a node without creating it.
    pub fn find(&self, kind: Kind, arg: u64) -> Option<NodeId> {
        match &self.kinds[kind.0 as usize].index {
            Index::Dense(table) => {
                let cur = table.get(usize::try_from(arg).ok()?)?.load(Ordering::Acquire);
                (cur < CREATING).then_some(NodeId(cur))
            }
            Index::Sparse(shards) => lock(&shards[Self::shard_u64(arg)]).get(&arg).copied(),
            Index::Content(_) => None,
        }
    }

    /// Finds or creates the content-keyed node of `kind` for the argument value `arg`.
    pub fn content_node(&self, kind: Kind, arg: &V) -> NodeId {
        let info = &self.kinds[kind.0 as usize];
        let Index::Content(shards) = &info.index else {
            panic!("kind {} is not content-keyed", info.name);
        };
        let mut h = ContentHasher::with_domain(u64::from(kind.0));
        arg.content_hash(&mut h);
        let key = h.finish();
        let mut m = lock(&shards[Self::shard_u64(key.hi)]);
        *m.entry(key).or_insert_with(|| self.alloc_node(kind, key.lo, Some(Box::new(arg.clone()))))
    }

    /// Creates an input and gives it its first value (does not advance the revision).
    pub fn create_input(&mut self, kind: Kind, arg: u64, value: V) -> NodeId {
        assert!(self.kinds[kind.0 as usize].class == KindClass::Input, "not an input kind");
        let id = self.node(kind, arg);
        let h = value.hash128();
        let rev = self.rev;
        let mut g = lock(&self.arena.get(id).inner);
        assert!(g.value.is_none(), "input {}[{arg}] already created", self.kinds[kind.0 as usize].name);
        g.value = Some(value);
        g.hash = h;
        g.state = State::Clean;
        g.changed_at = rev;
        g.verified_at = rev;
        g.computed_at = rev;
        g.cause = Cause::Set;
        g.trigger = Cause::Set;
        id
    }

    // ---- edits and invalidation ---------------------------------------------------------------

    /// Runs an edit transaction.  All inputs changed inside `f` share one new revision; every
    /// transitive dependent of a changed input is marked stale before this returns.
    pub fn edit<R>(&mut self, label: &str, f: impl FnOnce(&mut Edit<'_, V>) -> R) -> (R, EditReport) {
        let t0 = Instant::now();
        let next = self.rev.checked_add(1).expect("revision overflow");
        let mut tx = Edit { db: self, changed: Vec::new(), next };
        let r = f(&mut tx);
        let Edit { changed, .. } = tx;
        let mut marked = 0;
        if !changed.is_empty() {
            self.rev = next;
            self.rev_log.push((next, label.to_string()));
            marked = self.mark_stale(&changed);
        }
        (r, EditReport { rev: self.rev, changed: changed.len(), marked_stale: marked, elapsed: t0.elapsed() })
    }

    /// Sets one input.  Returns whether anything changed.
    pub fn set_input(&mut self, id: NodeId, value: V) -> bool {
        let (changed, _) = self.edit("set_input", |tx| tx.set(id, value));
        changed
    }

    fn mark_stale(&self, changed: &[NodeId]) -> usize {
        let mut stack: Vec<NodeId> = changed.to_vec();
        let mut scratch: Vec<NodeId> = Vec::new();
        let mut marked = 0usize;
        while let Some(x) = stack.pop() {
            scratch.clear();
            scratch.extend_from_slice(&lock(&self.arena.get(x).inner).rdeps);
            for &r in &scratch {
                let mut g = lock(&self.arena.get(r).inner);
                if g.state == State::Clean {
                    g.state = State::Stale;
                    drop(g);
                    marked += 1;
                    stack.push(r);
                }
            }
        }
        marked
    }

    // ---- reading ------------------------------------------------------------------------------

    /// The value of a node if it is up to date.
    pub fn value(&self, id: NodeId) -> Option<V> {
        let g = lock(&self.arena.get(id).inner);
        if g.state == State::Clean {
            g.value.clone()
        } else {
            None
        }
    }

    /// The last computed value even if it is stale (never a value of a different node).
    pub fn peek(&self, id: NodeId) -> Option<V> {
        lock(&self.arena.get(id).inner).value.clone()
    }

    pub fn is_clean(&self, id: NodeId) -> bool {
        lock(&self.arena.get(id).inner).state == State::Clean
    }

    pub fn label(&self, id: NodeId) -> NodeLabel {
        let g = lock(&self.arena.get(id).inner);
        let arg = match (&g.arg_value, self.kinds[g.kind as usize].class) {
            (Some(v), KindClass::Content) => v.hash128().lo,
            _ => g.arg,
        };
        NodeLabel { id, kind: self.kinds[g.kind as usize].name.clone(), arg }
    }

    pub fn info(&self, id: NodeId) -> NodeInfo {
        let label = self.label(id);
        let g = lock(&self.arena.get(id).inner);
        NodeInfo {
            label,
            state: state_name(g.state),
            computes: g.computes,
            changed_at: g.changed_at,
            verified_at: g.verified_at,
            computed_at: g.computed_at,
            hash: g.hash,
            deps: g.deps.clone(),
            n_rdeps: g.rdeps.len(),
        }
    }

    /// Number of completed computations of one node.
    pub fn computes(&self, id: NodeId) -> u32 {
        lock(&self.arena.get(id).inner).computes
    }

    /// Total completed computations per kind name (O(nodes); for tests and reports).
    pub fn computes_by_kind(&self) -> Vec<(String, u64)> {
        let mut per = vec![0u64; self.kinds.len()];
        for i in 0..self.arena.len() {
            let g = lock(&self.arena.get(NodeId(i as u32)).inner);
            per[g.kind as usize] += u64::from(g.computes);
        }
        self.kinds.iter().zip(per).map(|(k, n)| (k.name.clone(), n)).collect()
    }

    /// Number of nodes in each state (fresh, stale, clean).
    pub fn state_counts(&self) -> (usize, usize, usize) {
        let (mut f, mut s, mut c) = (0, 0, 0);
        for i in 0..self.arena.len() {
            match lock(&self.arena.get(NodeId(i as u32)).inner).state {
                State::Fresh => f += 1,
                State::Stale => s += 1,
                State::Clean => c += 1,
                State::Running | State::Parked => {}
            }
        }
        (f, s, c)
    }

    /// Id-independent dump of every node, sorted canonically.  Equal databases (same edits,
    /// same demands) produce equal dumps for every thread count.
    pub fn canonical_dump(&self) -> Vec<NodeRecord> {
        let key_of = |g: &Inner<V>| -> (u16, u64) {
            match (&g.arg_value, self.kinds[g.kind as usize].class) {
                (Some(v), KindClass::Content) => (g.kind, v.hash128().lo),
                _ => (g.kind, g.arg),
            }
        };
        let mut out = Vec::with_capacity(self.arena.len());
        for i in 0..self.arena.len() {
            let g = lock(&self.arena.get(NodeId(i as u32)).inner);
            let (kind, key) = key_of(&g);
            let deps = g
                .deps
                .iter()
                .map(|&d| {
                    let dg = lock(&self.arena.get(d).inner);
                    let (dk, dkey) = key_of(&dg);
                    (self.kinds[dk as usize].name.clone(), dkey)
                })
                .collect();
            out.push(NodeRecord {
                kind: self.kinds[kind as usize].name.clone(),
                key,
                state: state_name(g.state),
                hash: g.hash,
                changed_at: g.changed_at,
                verified_at: g.verified_at,
                computed_at: g.computed_at,
                computes: g.computes,
                deps,
            });
        }
        out.sort_by(|a, b| (&a.kind, a.key).cmp(&(&b.kind, b.key)));
        out
    }

    /// Hash of the canonical dump.
    pub fn digest(&self) -> Hash128 {
        let mut h = ContentHasher::new();
        for r in self.canonical_dump() {
            r.kind.content_hash(&mut h);
            h.write_u64(r.key);
            r.state.content_hash(&mut h);
            r.hash.content_hash(&mut h);
            h.write_u32(r.changed_at);
            h.write_u32(r.verified_at);
            h.write_u32(r.computed_at);
            h.write_u32(r.computes);
            h.write_u64(r.deps.len() as u64);
            for (k, a) in &r.deps {
                k.content_hash(&mut h);
                h.write_u64(*a);
            }
        }
        h.finish()
    }

    // ---- explanation --------------------------------------------------------------------------

    fn why_of(&self, cause: Cause, rev: Rev, first: bool) -> Why {
        match cause {
            Cause::Set => Why::InputSet {
                rev,
                label: self.rev_log.iter().find(|(r, _)| *r == rev).map(|(_, l)| l.clone()),
            },
            Cause::Dep(d) => {
                let dep_changed_at = lock(&self.arena.get(d).inner).changed_at;
                Why::DependencyChanged { rev, dep: self.label(d), dep_changed_at }
            }
            Cause::Initial => Why::FirstEvaluation { rev },
            Cause::None => {
                if first {
                    Why::FirstEvaluation { rev }
                } else {
                    Why::NoChangedDependency { rev }
                }
            }
        }
    }

    /// "Why did this recompute?"  Follows the dependency whose change triggered the node's last
    /// computation, then the chain of changes below it, down to the edited input.  For a node
    /// that has not been recomputed since a later edit the headline says why it did not.
    pub fn explain_recompute(&self, id: NodeId) -> Explanation {
        let (headline, first_cause, computed_at, changed_at, has_value) = {
            let g = lock(&self.arena.get(id).inner);
            let label = format!("{}[{}]", self.kinds[g.kind as usize].name, g.arg);
            let head = if g.value.is_none() {
                format!("{label} has never been evaluated")
            } else if g.state == State::Stale {
                format!("{label} is stale: a dependency was edited at r{} and it has not been demanded since", self.rev)
            } else if g.verified_at > g.computed_at {
                let mut s = format!(
                    "{label} did NOT recompute at r{}: it was last computed at r{} and no dependency changed after that",
                    g.verified_at, g.computed_at
                );
                let cut: Vec<String> = g
                    .deps
                    .iter()
                    .filter_map(|&d| {
                        let dg = lock(&self.arena.get(d).inner);
                        (dg.computed_at > g.computed_at && dg.changed_at <= g.computed_at)
                            .then(|| format!("{}[{}]", self.kinds[dg.kind as usize].name, dg.arg))
                    })
                    .collect();
                if !cut.is_empty() {
                    s.push_str(&format!(
                        "; recomputed dependencies with an unchanged result (early cutoff): {}",
                        cut.join(", ")
                    ));
                }
                s
            } else {
                format!("{label} recomputed at r{}", g.computed_at)
            };
            (head, g.trigger, g.computed_at, g.changed_at, g.value.is_some())
        };
        let mut steps = Vec::new();
        if !has_value {
            steps.push(ExplainStep { node: self.label(id), why: Why::NeverEvaluated, value_changed: false });
            return Explanation { headline, steps };
        }
        steps.push(ExplainStep {
            node: self.label(id),
            why: self.why_of(first_cause, computed_at, false),
            value_changed: changed_at == computed_at,
        });
        let mut cur = first_cause;
        let mut guard = 0;
        while let Cause::Dep(d) = cur {
            guard += 1;
            if guard > 100_000 {
                break;
            }
            let (cause, changed_at) = {
                let g = lock(&self.arena.get(d).inner);
                (g.cause, g.changed_at)
            };
            steps.push(ExplainStep { node: self.label(d), why: self.why_of(cause, changed_at, false), value_changed: true });
            cur = cause;
        }
        Explanation { headline, steps }
    }

    // ---- evaluation ---------------------------------------------------------------------------

    /// Brings every node in `roots` up to date (visible priority).
    pub fn evaluate(&self, roots: &[NodeId]) -> Result<EvalReport, EvalError> {
        let reqs: Vec<(NodeId, Priority)> = roots.iter().map(|&r| (r, Priority::Visible)).collect();
        self.evaluate_with(&reqs, &EvalOptions::default())
    }

    /// Evaluates the demands in priority order (all `Visible` first, then `Nearby`, then
    /// `Background`).  Only the transitive dependencies of the demanded nodes are touched.
    pub fn evaluate_with(&self, reqs: &[(NodeId, Priority)], opts: &EvalOptions<'_>) -> Result<EvalReport, EvalError> {
        let _guard = lock(&self.eval_lock);
        let t0 = Instant::now();
        let ev = Eval {
            db: self,
            rev: self.rev,
            token: opts.cancel,
            internal_stop: AtomicBool::new(false),
            aborted: AtomicBool::new(false),
            error: Mutex::new(None),
            journal: Mutex::new(Vec::new()),
            totals: Mutex::new(Totals::default()),
            serial_budget: opts.serial_budget.unwrap_or(self.cfg.serial_budget),
            force_serial: opts.force_serial,
        };
        let mut class_done = [None; 3];
        let mut all_roots: Vec<NodeId> = Vec::new();
        let mut parallel = false;
        for class in [Priority::Visible, Priority::Nearby, Priority::Background] {
            let roots: Vec<NodeId> = reqs.iter().filter(|r| r.1 == class).map(|r| r.0).collect();
            if roots.is_empty() {
                continue;
            }
            if ev.stop_seen() {
                break;
            }
            parallel |= self.run_class(&ev, &roots);
            if ev.aborted.load(Ordering::Relaxed) {
                break;
            }
            class_done[class as usize] = Some(t0.elapsed());
            all_roots.extend(roots);
        }
        self.finish_eval(ev, &all_roots, t0, class_done, parallel)
    }

    fn finish_eval(
        &self,
        ev: Eval<'_, V>,
        roots: &[NodeId],
        t0: Instant,
        class_done: [Option<Duration>; 3],
        parallel: bool,
    ) -> Result<EvalReport, EvalError> {
        let aborted = ev.aborted.load(Ordering::Relaxed);
        let error = lock(&ev.error).take();
        let journal = std::mem::take(&mut *lock(&ev.journal));
        let mut stalled = false;
        if !aborted {
            stalled = roots.iter().any(|&r| lock(&self.arena.get(r).inner).state != State::Clean);
        }
        let mut cycle = None;
        if stalled {
            cycle = Some(self.diagnose(&journal));
        }
        if aborted || stalled {
            self.sweep(&journal);
        }
        if let Some(e) = error {
            return Err(e);
        }
        if aborted {
            return Err(EvalError::Cancelled);
        }
        if let Some(path) = cycle {
            return Err(if path.is_empty() {
                EvalError::Stalled("demanded nodes did not complete and no cycle was found".to_string())
            } else {
                EvalError::Cycle(path)
            });
        }
        let t = lock(&ev.totals);
        Ok(EvalReport {
            steps: t.steps,
            computed: t.computed,
            verified: t.verified,
            cutoffs: t.cutoffs,
            restarts: t.restarts,
            claimed: t.claimed,
            elapsed: t0.elapsed(),
            class_done,
            parallel,
        })
    }

    /// Returns claimed-but-unfinished nodes to the state they had before they were claimed.
    fn sweep(&self, journal: &[NodeId]) {
        for &id in journal {
            let node = self.arena.get(id);
            let mut g = lock(&node.inner);
            if matches!(g.state, State::Running | State::Parked) {
                g.state = g.resume;
                g.waiters.clear();
            }
            drop(g);
            node.pending.store(0, Ordering::Relaxed);
        }
    }

    /// Finds a dependency cycle among the parked nodes by asking each of them what it waits for.
    fn diagnose(&self, journal: &[NodeId]) -> Vec<String> {
        let mut waits: HashMap<NodeId, Vec<NodeId>> = HashMap::new();
        for &id in journal {
            let (state, deps, has_value, kind, arg, arg_value) = {
                let g = lock(&self.arena.get(id).inner);
                (g.state, g.deps.clone(), g.value.is_some(), g.kind, g.arg, g.arg_value.as_deref().cloned())
            };
            if state != State::Parked && state != State::Running {
                continue;
            }
            let mut missing: Vec<NodeId> = Vec::new();
            if has_value {
                for d in deps {
                    if lock(&self.arena.get(d).inner).state != State::Clean {
                        missing.push(d);
                    }
                }
            }
            if missing.is_empty() {
                if let Some(f) = self.kinds[kind as usize].compute.as_ref() {
                    let probe_ev = Eval {
                        db: self,
                        rev: self.rev,
                        token: None,
                        internal_stop: AtomicBool::new(false),
                        aborted: AtomicBool::new(false),
                        error: Mutex::new(None),
                        journal: Mutex::new(Vec::new()),
                        totals: Mutex::new(Totals::default()),
                        serial_budget: 0,
                        force_serial: true,
                    };
                    let mut ctx = Ctx { ev: &probe_ev, arg, arg_value, deps: Vec::new(), dep_changed: Vec::new(), missing: Vec::new() };
                    let _ = catch_unwind(AssertUnwindSafe(|| f(&mut ctx)));
                    missing = ctx.missing;
                }
            }
            waits.insert(id, missing);
        }
        // Depth-first search for a cycle.
        let mut color: HashMap<NodeId, u8> = HashMap::new();
        let mut path: Vec<NodeId> = Vec::new();
        let mut keys: Vec<NodeId> = waits.keys().copied().collect();
        keys.sort();
        for start in keys {
            if color.get(&start).copied().unwrap_or(0) != 0 {
                continue;
            }
            if let Some(cyc) = self.dfs_cycle(start, &waits, &mut color, &mut path) {
                return cyc.into_iter().map(|n| self.label(n).to_string()).collect();
            }
        }
        Vec::new()
    }

    fn dfs_cycle(
        &self,
        n: NodeId,
        waits: &HashMap<NodeId, Vec<NodeId>>,
        color: &mut HashMap<NodeId, u8>,
        path: &mut Vec<NodeId>,
    ) -> Option<Vec<NodeId>> {
        color.insert(n, 1);
        path.push(n);
        if let Some(next) = waits.get(&n) {
            for &m in next {
                match color.get(&m).copied().unwrap_or(0) {
                    1 => {
                        let pos = path.iter().position(|&x| x == m).unwrap_or(0);
                        let mut cyc: Vec<NodeId> = path[pos..].to_vec();
                        cyc.push(m);
                        return Some(cyc);
                    }
                    0 => {
                        if let Some(c) = self.dfs_cycle(m, waits, color, path) {
                            return Some(c);
                        }
                    }
                    _ => {}
                }
            }
        }
        path.pop();
        color.insert(n, 2);
        None
    }

    fn claim_root(&self, id: NodeId, ls: &mut Local) -> bool {
        let mut g = lock(&self.arena.get(id).inner);
        match g.state {
            State::Fresh | State::Stale => {
                g.resume = g.state;
                g.state = State::Running;
                drop(g);
                ls.claimed.push(id);
                true
            }
            State::Clean | State::Running | State::Parked => false,
        }
    }

    /// Runs one priority class.  Returns whether the pool was used.
    fn run_class(&self, ev: &Eval<'_, V>, roots: &[NodeId]) -> bool {
        let budget = ev.serial_budget;
        let pool = if ev.force_serial { None } else { self.pool.as_ref() };
        if let Some(pool) = pool {
            if roots.len() > budget {
                pool.scope(|s| {
                    for chunk in roots.chunks(ROOT_CHUNK) {
                        s.spawn(move |s| self.drive_roots(ev, s, chunk));
                    }
                });
                return true;
            }
        }
        let mut ls = Local::default();
        let mut stack: Vec<NodeId> = Vec::new();
        for &r in roots {
            if self.claim_root(r, &mut ls) {
                stack.push(r);
            }
        }
        let mut steps = 0usize;
        let mut used_pool = false;
        while let Some(n) = stack.pop() {
            if ev.stop_seen() {
                break;
            }
            self.step(ev, n, &mut stack, &mut ls);
            steps += 1;
            if let Some(pool) = pool {
                if steps >= budget && !stack.is_empty() {
                    let rest = std::mem::take(&mut stack);
                    pool.scope(|s| self.drive(ev, s, rest, Local::default()));
                    used_pool = true;
                    break;
                }
            }
        }
        ev.merge(ls);
        used_pool
    }

    fn drive_roots<'s>(&'s self, ev: &'s Eval<'s, V>, scope: &rayon::Scope<'s>, chunk: &[NodeId]) {
        let mut ls = Local::default();
        let mut stack = Vec::new();
        for &r in chunk {
            if self.claim_root(r, &mut ls) {
                stack.push(r);
            }
        }
        self.drive(ev, scope, stack, ls);
    }

    fn drive<'s>(&'s self, ev: &'s Eval<'s, V>, scope: &rayon::Scope<'s>, mut stack: Vec<NodeId>, mut ls: Local) {
        while let Some(n) = stack.pop() {
            if ev.stop_seen() {
                break;
            }
            self.step(ev, n, &mut stack, &mut ls);
            if stack.len() >= self.cfg.split_threshold {
                let half = stack.len() / 2;
                let tail: Vec<NodeId> = stack.drain(..half).collect();
                scope.spawn(move |s| self.drive(ev, s, tail, Local::default()));
            }
        }
        ev.merge(ls);
    }

    fn wake(&self, waiters: Vec<NodeId>, out: &mut Vec<NodeId>) {
        for w in waiters {
            if self.arena.get(w).pending.fetch_sub(1, Ordering::AcqRel) == 1 {
                out.push(w);
            }
        }
    }

    /// Advances one claimed node: verify it, or run its function once.  Newly runnable nodes
    /// (claimed dependencies, woken dependents) are appended to `out`.
    fn step(&self, ev: &Eval<'_, V>, n: NodeId, out: &mut Vec<NodeId>, ls: &mut Local) {
        ls.steps += 1;
        let node = self.arena.get(n);
        let (kind, arg, has_value, verified_at, old_deps, arg_value) = {
            let mut g = lock(&node.inner);
            g.state = State::Running;
            (g.kind, g.arg, g.value.is_some(), g.verified_at, std::mem::take(&mut g.deps), g.arg_value.as_deref().cloned())
        };
        let Some(compute) = self.kinds[kind as usize].compute.as_ref() else {
            lock(&node.inner).deps = old_deps;
            ev.fail(EvalError::MissingInput(self.label(n).to_string()));
            return;
        };

        // Verification: a node that has a value revalidates without running its function when
        // no dependency changed since it was last verified.  Every previous dependency is first
        // brought up to date (as one parallel batch) and only then are the revisions compared.
        // Deciding on the first changed dependency instead would make the set of evaluated nodes
        // depend on which dependencies happened to be clean at scan time, i.e. on scheduling.
        if has_value {
            let mut missing: Vec<NodeId> = Vec::new();
            let mut changed_dep = false;
            for &d in &old_deps {
                let g = lock(&self.arena.get(d).inner);
                if g.state == State::Clean {
                    changed_dep |= g.changed_at > verified_at;
                } else {
                    missing.push(d);
                }
            }
            if !missing.is_empty() {
                self.park(n, &missing, old_deps, out, ls);
                return;
            }
            if !changed_dep {
                let waiters = {
                    let mut g = lock(&node.inner);
                    g.deps = old_deps;
                    g.verified_at = ev.rev;
                    g.state = State::Clean;
                    std::mem::take(&mut g.waiters)
                };
                ls.verified += 1;
                self.wake(waiters, out);
                return;
            }
        }

        let mut ctx = Ctx {
            ev,
            arg,
            arg_value,
            deps: Vec::with_capacity(old_deps.len().max(2)),
            dep_changed: Vec::with_capacity(old_deps.len().max(2)),
            missing: Vec::new(),
        };
        let result = catch_unwind(AssertUnwindSafe(|| compute(&mut ctx)));
        match result {
            Ok(Ok(value)) if ctx.missing.is_empty() => {
                let Ctx { deps, dep_changed, .. } = ctx;
                let commit = Commit { value, deps, dep_changed, old_deps, prev_verified: verified_at, first: !has_value };
                self.finish(ev, n, commit, out, ls);
            }
            Ok(Ok(_)) | Ok(Err(Interrupt::Missing)) => {
                if ctx.missing.is_empty() {
                    lock(&node.inner).deps = old_deps;
                    ev.fail(EvalError::Stalled(format!("{} returned Missing without requesting a dependency", self.label(n))));
                    return;
                }
                ls.restarts += 1;
                let mut missing = std::mem::take(&mut ctx.missing);
                dedup_ids(&mut missing);
                self.park(n, &missing, old_deps, out, ls);
            }
            Ok(Err(Interrupt::Cancelled)) => {
                lock(&node.inner).deps = old_deps;
                ev.aborted.store(true, Ordering::Relaxed);
                ev.internal_stop.store(true, Ordering::Relaxed);
            }
            Err(payload) => {
                lock(&node.inner).deps = old_deps;
                let message = payload
                    .downcast_ref::<&str>()
                    .map(|s| (*s).to_string())
                    .or_else(|| payload.downcast_ref::<String>().cloned())
                    .unwrap_or_else(|| "non-string panic payload".to_string());
                ev.fail(EvalError::Panic { node: self.label(n).to_string(), message });
            }
        }
    }

    /// Stores the result of a completed computation, maintains the reverse edges and wakes dependents.
    fn finish(&self, ev: &Eval<'_, V>, n: NodeId, c: Commit<V>, out: &mut Vec<NodeId>, ls: &mut Local) {
        let Commit { value, mut deps, mut dep_changed, old_deps, prev_verified, first } = c;
        dedup_keep_first(&mut deps, &mut dep_changed);
        let new_hash = value.hash128();
        let trigger = if first {
            Cause::Initial
        } else {
            dep_changed
                .iter()
                .position(|&r| r > prev_verified)
                .map_or(Cause::None, |i| Cause::Dep(deps[i]))
        };
        let (added, removed) = diff_deps(&old_deps, &deps);
        let node = self.arena.get(n);
        let (waiters, changed) = {
            let mut g = lock(&node.inner);
            let changed = first || g.hash != new_hash;
            if changed {
                g.prev_hash = g.hash.lo;
                g.changed_at = ev.rev;
                g.cause = trigger;
            }
            g.trigger = trigger;
            g.hash = new_hash;
            g.value = Some(value);
            g.verified_at = ev.rev;
            g.computed_at = ev.rev;
            g.computes += 1;
            g.deps = deps;
            g.state = State::Clean;
            (std::mem::take(&mut g.waiters), changed)
        };
        for d in added {
            lock(&self.arena.get(d).inner).rdeps.push(n);
        }
        for d in removed {
            let mut g = lock(&self.arena.get(d).inner);
            if let Some(p) = g.rdeps.iter().position(|&x| x == n) {
                g.rdeps.swap_remove(p);
            }
        }
        ls.computed += 1;
        if !changed {
            ls.cutoffs += 1;
        }
        self.wake(waiters, out);
    }

    /// Parks `n` until every node in `missing` is complete; claims the ones nobody is working on.
    fn park(&self, n: NodeId, missing: &[NodeId], old_deps: Vec<NodeId>, out: &mut Vec<NodeId>, ls: &mut Local) {
        let node = self.arena.get(n);
        let k = missing.len() as u32;
        node.pending.store(k + 1, Ordering::Relaxed);
        {
            let mut g = lock(&node.inner);
            g.deps = old_deps;
            g.state = State::Parked;
        }
        let mut done = 1u32;
        for &m in missing {
            let mut g = lock(&self.arena.get(m).inner);
            match g.state {
                State::Clean => done += 1,
                State::Fresh | State::Stale => {
                    g.resume = g.state;
                    g.state = State::Running;
                    g.waiters.push(n);
                    drop(g);
                    ls.claimed.push(m);
                    out.push(m);
                }
                State::Running | State::Parked => g.waiters.push(n),
            }
        }
        if node.pending.fetch_sub(done, Ordering::AcqRel) == done {
            out.push(n);
        }
    }
}

fn dedup_ids(v: &mut Vec<NodeId>) {
    let mut seen: Vec<NodeId> = Vec::with_capacity(v.len());
    for &x in v.iter() {
        if !seen.contains(&x) {
            seen.push(x);
        }
    }
    *v = seen;
}

fn dedup_keep_first(deps: &mut Vec<NodeId>, changed: &mut Vec<Rev>) {
    if deps.len() < 2 {
        return;
    }
    let mut w = 0;
    if deps.len() <= 24 {
        for r in 0..deps.len() {
            let d = deps[r];
            if !deps[..w].contains(&d) {
                deps[w] = d;
                changed[w] = changed[r];
                w += 1;
            }
        }
    } else {
        let mut seen: std::collections::HashSet<u32, IdBuild> = std::collections::HashSet::with_hasher(IdBuild::default());
        for r in 0..deps.len() {
            let d = deps[r];
            if seen.insert(d.0) {
                deps[w] = d;
                changed[w] = changed[r];
                w += 1;
            }
        }
    }
    deps.truncate(w);
    changed.truncate(w);
}

fn diff_deps(old: &[NodeId], new: &[NodeId]) -> (Vec<NodeId>, Vec<NodeId>) {
    if old == new {
        return (Vec::new(), Vec::new());
    }
    if old.len() <= 24 && new.len() <= 24 {
        let added = new.iter().copied().filter(|d| !old.contains(d)).collect();
        let removed = old.iter().copied().filter(|d| !new.contains(d)).collect();
        (added, removed)
    } else {
        let mut o = old.to_vec();
        o.sort_unstable();
        let mut nw = new.to_vec();
        nw.sort_unstable();
        let added = new.iter().copied().filter(|d| o.binary_search(d).is_err()).collect();
        let removed = old.iter().copied().filter(|d| nw.binary_search(d).is_err()).collect();
        (added, removed)
    }
}
