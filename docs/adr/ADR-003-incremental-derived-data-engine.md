# ADR-003: Incremental derived-data engine (own engine in Rust)

| | |
|---|---|
| Status | Proposed. Spike C is complete; the decision belongs to the owner. Gates are met only in bounded form (section 4); a re-stated gate is proposed. |
| Date | 2026-11-14 (UTC) |
| Plan | Master Plan Rev 2, 5.4 ("ADR-4") and 5.2 (threads bullet); Appendix B (edit propagation under 50 ms at 1 M elements) |
| Evidence | branch `w0-spike-c` @ `e188cc98`, `dino8-core/spikes/graph/RESULTS.md`; kernel audit from branch `w0-kthreads` @ `e5bc8421` (ADR-005) |

## 1. Context

Plan 5.4: a small own engine in Rust (pure query functions keyed by content hash, revision-numbered inputs, dependency recording, parallel
evaluation on a work-stealing pool, cancellation, visible-first priority), not a general library dependency, to keep control of cancellation
and of calls into the C++ kernel. Order of adoption: tessellation, mass properties and analysis queries, section and elevation views, schedules
and quantities, then Flow evaluation and analysis meshes. Each adopted item keeps its existing tests and adds an invalidation-correctness test
(editing an unrelated object must not recompute it, editing a dependency must). Gates: edit propagation under 50 ms at 1 M elements, and a 20 times
speedup on partial edits in a synthetic regeneration workload. If the kernel cannot be called concurrently the engine runs kernel calls serialized and the
speedup target is re-stated before Wave 1 closes.

## 2. Decision

1. Keep ADR-4: build the production engine from the Spike C design (`d8-engine` crate, `rayon` pool, 100% safe Rust except the FFI module), as the first
   user of the ADR-001 boundary.
2. Semantics (each covered by tests in the spike): revision-numbered inputs with batch edits under one revision; an input set to a value with the same
   content hash changes nothing; pure queries keyed by `(kind, argument)` and content-keyed queries shared by equal values; dependencies recorded
   automatically and rebuilt on each recomputation; fine-grained invalidation with early cutoff; demand-driven lazy evaluation; restart-based parallel
   evaluation (a query that needs a stale dependency gets `Interrupt::Missing`, the scheduler evaluates the missing set as a parallel batch and re-runs it, so
   query functions never block and a work-stealing pool cannot deadlock); priorities `Visible`, `Nearby`, `Background` as ordered phases;
   cooperative cancellation; explanation ("why did this recompute / not recompute"); cycle, panic and missing-input errors that leave the database usable.
3. The kernel is reached only through function pointers registered over the C ABI (`D8gKernelDesc`, ABI version 1 in the spike, sketch in RESULTS.md section 8). Each
   kernel is registered `D8G_THREAD_SAFE` or `D8G_SERIALIZED` (exactly one required). Which kernel functions are which is ADR-005. The kernel never calls back into
   the engine except to poll cancellation; the engine plans dependencies through a cheap `deps` callback and calls the kernel exactly once.
4. Design rules for adopters (hard constraints, from measured findings):
   - one node per kernel-class unit of work (5 us and up): never a node per vertex, face or cheap arithmetic step;
   - every aggregate (schedules, view roots, quantity roll-ups, sheet indexes) is a fan-in-64 reduction tree of kernel-class chunks;
   - roots demanded by the UI are tiles or chunks, never whole documents;
   - geometry kernels declare value-dependent dependencies through the planning callback to avoid the 25% double execution of the restart protocol.
5. Cancellation of a running B-rep boolean is not possible (ADR-005); the engine discards stale results instead.
6. Gates re-stated as in section 4 (proposal). The plan's original figures stay as stretch goals for the parallel build on owner hardware.

## 3. Measured evidence

Run conditions: this 4-core machine used by about 20 agents at the same time (load average 11 to 27 during the runs). Because wall-clock times of
compute-bound work were inflated and noisy, the headline tables are thread CPU time (`CLOCK_THREAD_CPUTIME_ID`) of the single evaluating thread (`--threads 1 --clock cpu`),
median of the repetitions (minimum in brackets in the raw tables). Workload: N walls in rooms of four on 20 levels, 16 wall types; derived nodes per wall (geometry,
join, tag on every second wall), per (view, tile) over a spatial index that is itself an input, one plan view per level, and a wall schedule (leaf chunks of 64 plus a fan-in-64 tree).
100,000 walls give 254,989 derived nodes; 1,000,000 walls give 2,547,895. Queries cost the kernel arithmetic plus an optimizer-proof synthetic delay of 0, 5 or 25 us (25 us stands for a kernel call
such as a tessellation). The baseline is a deliberately fast staged global recompute of the same kernels (no memoization, no tracking). After the first edit of every scenario and in 24-edit tests the engine
output is compared bit for bit with the baseline.

1,000,000 walls, serial, thread CPU, kernel cost about 0.04 us (bare engine overhead): cold build 1,691 ms engine against 112 to 115 ms global recompute (15 times); 3,579,952 nodes, RSS 1,088 MB (312 B per derived node):

| edit | nodes recomputed | to visible ms | to fully consistent ms | speedup (consistent) |
|---|---:|---:|---:|---:|
| move one wall (on screen) | 12 | 0.159 | 0.161 | 713 |
| height of one wall | 10 | 0.085 | 0.087 | 1,327 |
| cosmetic edit | 1 | 0.051 | 0.052 | 2,191 |
| batch of 100 walls | 883 | 3.85 | 7.55 | 15.2 |
| view cut | 1,601 | 8.25 | 8.25 | 13.9 |
| level elevation | 127,382 | 73.6 | 73.6 | 1.56 |
| wall type | 336,384 | 264 | 501 | 0.23 |
| tag style | 532,020 | 125 | 419 | 0.27 |

1,000,000 walls, kernel cost 5 us (geometry, join) and 2.5 us (tag): move one wall 0.172 ms visible (62,610 times); batch of 100 4.10 ms (1,249 times); view cut 7.50 ms (1,540 times); level elevation 649 to 659 ms to a fully consistent state
(17.8 and 17.5 times; 304 ms to visible in the off-screen direction); wall type 691 to 1,944 ms (5.94 times).

100,000 walls, kernel 25 us (geometry, join) and 12.5 us (tag): cold 5,719 ms against 5,583 ms (1.02 times); one wall 0.171 ms (32,552 times); 100 walls 16.1 ms (346 times); one view 0.557 ms (10,008 times); level elevation 293 to 305 ms (18.3 to 19.1 times);
wall type 728 ms (7.66 times); tag style 679 ms (8.21 times). Viewport-first demand (5 x 5 tiles first): 100,000 walls at 25 us, level elevation, 31.3 ms to visible and 198 ms to fully consistent (19.8 times); 1,000,000 walls at 0.04 us, 12.3 ms to visible and 58.8 ms consistent.
Viewport-first at 1 M walls with 5 us or 25 us kernels was NOT measured.

Engine cost per recomputed node: about 0.64 us (100,000 walls) to 0.66 us (1 M) on a cold build and 0.47 us on the large incremental edits; about 2 million node recomputations per second serial. 25% of nodes run their function twice
(restart protocol). Model derived from it: speedup(f) is about (c + 0.03 us) / (f x (c + 0.66 us)) for mean query cost c and touched fraction f, so a 20 times speedup needs f <= 5% x c / (c + 0.66 us): f <= 4.9% at 25 us, 4.4% at 5 us, 3.1% at 1 us, 0.4% at 0.03 us.
Flat aggregate over 100,000 rows: 6.8 ms per single-wall edit (0.8 times the recompute), against 0.047 ms as a fan-in-64 tree (about 125 times faster).

Parallelism: sleeping-kernel scaling (512 leaf queries of 1 ms) 1.95 / 3.76 / 6.97 / 11.76 times on 2 / 4 / 8 / 16 threads (97 / 94 / 87 / 73% efficiency). Total process CPU of 4-thread runs is 0.81 to 1.33 times the serial thread CPU, so an idle 4-core machine could reach
about 3 times (inferred from CPU totals, NOT measured: compute-bound wall speedup could not be measured on a saturated machine). Wall-clock 4-thread runs (load 15 to 27) are shown in RESULTS.md 6.4 and are inflated.

Memory: 224-byte node record plus edges and value; 312 B per derived node measured (1.09 GB RSS at 1 M walls, 3.58 M nodes). Target of a production layout 64 to 96 B per node: not built.

Tests: `cargo test` 47 tests (18 invalidation, 5 model with proptest over 48 random DAGs and 1 to 16 threads, 6 cancel, 7 workload, 7 ABI, 4 hash); clippy `-D warnings` clean; the suite passed 25 consecutive runs at machine load 12 to 20 and 15 more at the end with the original timing bounds. The
three cancellation timing tests are wall-clock tests and could fail on a machine saturated for longer than the bound (250 ms, 500 ms, 1 s). After the fix of the differential-test finding (verification depended on evaluation order), the full database state (values, revisions, dependency order, computation counts) is bit-identical for 1 to 16 threads. A kernel registered through C function pointers is incremental; a `D8G_SERIALIZED` kernel is
never entered concurrently (tested); registration without a thread-safety declaration is rejected.

Cross-check with the kernel audit (ADR-005, different spike, `-O1` probe, not production flags): CPU per call at 1 thread: tessellate 13.3 ms (input dependent), mass properties 4.2 ms, mixed boolean 17.0 ms, planar compound boolean 7.6 ms, mesh boolean 12.4 ms,
general boolean 197.7 ms. These are about 6,400 times (mass properties) to 300,000 times (general boolean) the 0.66 us engine overhead per node, so real kernel queries are "kernel class" in the sense of the design rule; this is my inference from two separate measurements, not a measurement of the engine with the real kernel.

## 4. Gates

| Gate (plan 5.4 / Appendix B) | Verdict | Detail |
|---|---|---|
| Edit propagation under 50 ms at 1 M elements | **Met for the typical parametric edit, bounded.** 1 M walls (2.55 M derived nodes), serial: move, resize or edit one wall reaches the visible views in 0.05 to 0.16 ms and a fully consistent model in 0.05 to 0.17 ms (0.15 to 0.22 ms with 5 us queries); a 100-wall batch 7.6 ms (3.9 ms visible). **Missed** for edits that touch 80,000 or more derived nodes at 0.5 to 0.7 us per node: level elevation (127,382 nodes) 74 ms, wall type (336,384) 501 ms, tag style (532,020) 419 ms, with trivial kernels. With 5 us kernels, level elevation takes 649 to 659 ms to a consistent state. | bounded; see below |
| 20 times speedup on partial edits against a global recompute | **Met for edits that touch up to about 4% of the derived data when queries are kernel class (5 us and up):** one wall 32,000 times, 100 walls 346 times, one view 10,000 times (25 us queries, 100,000 walls). **Missed at 5% and above:** level elevation 18 to 20 times, wall type 7.7 times, tag style 8.2 times (25 us). **Missed for everything beyond a handful of walls when queries cost under about 1 us** (0.03 us kernels: 100-wall batch 2.4 times, level edit 1.1 times). | bounded |
| Invalidation correctness (unrelated edit does not recompute, dependency edit does) | Met in the spike (tests) | |
| Kernel cannot be called concurrently, so speedup target re-stated | Not triggered: ADR-005 finds tessellation, mass properties, booleans, fillets, sweeps and hulls safe to call concurrently; SubD and file I/O stay serialized | the serialized-fallback ratios were measured serially on both sides anyway |
| Parallel speedup of the evaluator on a compute-bound workload | **Not measured** (machine saturated); 94% efficiency at 4 threads on sleeping kernels only | |
| Memory within 1.5 times incumbent | **Not measured** (no incumbent comparison; 312 B per node) | |
| ThreadSanitizer, Miri, loom on the engine and `abi.rs` | **Not run** (stable toolchain only); engine and workload have `#![deny(unsafe_code)]`, `unsafe` is in `abi.rs` and two clock calls of the benchmark | |
| Cost of crossing the C ABI per node | **Not measured separately**: the tables are for Rust callbacks (a function-pointer call and a few small allocations per node are the expected extra) | |

My arithmetic, labelled as such (not a spike result): the node count that fits 50 ms is 50 ms / (c + 0.66 us). For c = 5 us that is about 8,800 nodes serial; for c = 25 us about 1,950; for a 13 ms tessellation about 4. The "80,000 nodes" figure holds
only for trivial kernels. So the re-stated gate must be stated in kernel time on the visible closure, not in node counts, and edit-to-visible for large edits depends on viewport-first demand and on the parallel pool, neither of which is measured with realistic kernels.

Re-stated gate proposal (Spike C section 10 plus the correction above; owner to approve):
1. 20 times speedup for edits that touch up to 4% of the derived data, for kernel-class queries (5 us and up).
2. Edit-to-visible under 50 ms at 1 M elements for edits whose visible closure needs at most 50 ms of serial kernel time (engine overhead included at 0.66 us per node), and for any edit through viewport-first demand; the visible-first figure for realistic kernels is to be measured on the first adopter (tessellation).
3. The original figures (50 ms for every edit, 20 times on any partial edit) remain stretch goals for the parallel build on owner hardware.

## 5. Consequences

- The engine design is proven on a synthetic Revit-like workload; its value is in typical edits (10 to 900 nodes), where, at 1 M walls, it is 713 times (0.04 us kernels) to 62,610 times (5 us kernels) faster than a global recompute for one moved wall.
- Cold evaluation is a one-off at document open and costs 1.02 times (25 us queries) to 26 times (0.03 us queries) the staged recompute; a persistent content-addressed result store would turn it into a validation (not built; this also touches the 1 GB open gate).
- Adopters must follow the design rules (section 2.4). In particular, a flat schedule over many rows is slower than the global recompute.
- Production hardening is required before Wave 2: compact node layout (about 3 times smaller), index-arena edges, lock-free reads of clean values, garbage collection of content-keyed nodes (never freed in the spike), failure as a cached value (the spike surfaces an error), evaluation from several threads at once (the spike takes a lock: one evaluation at a time per database and exclusive edits: cancel, join, edit, evaluate), a two-level view root (at 1 M walls the 1,600-tile view root costs about 0.1 ms per edit), loom and ThreadSanitizer jobs on the engine and `abi.rs`, Miri on `abi.rs`.
- B-rep results travel as an 8-byte handle id plus a canonical hash from `d8g_out_set_hash`, with a `release` callback in `D8gKernelDesc` and the shared reference-counted handle table of plan 5.2; this is designed in RESULTS.md but not built.

## 6. Risks

- The speedups compare against a fast global recompute of synthetic kernels, not the real kernel; real kernel costs, real model shapes and real parallel speedup are open.
- Engine overhead (0.66 us per node) makes cheap-kernel workloads (Flow arithmetic nodes, tag text) slower than a global recompute: node granularity discipline is a social rule that needs a lint or review gate.
- Memory at 312 B per node: 1.09 GB at 1 M walls would exceed a laptop budget if kept.
- No cooperative cancellation inside the B-rep kernel: a long boolean already running cannot be stopped.
- The off-origin sphere tessellation defect (ADR-005 section on kernel defects) makes the first adopter (tessellation) unsafe for some inputs.
- Priorities are phases, not preemption: a running query is not interrupted except through its own cancellation polling.
- The restart protocol runs 25% of nodes twice unless the kernel supplies a planning callback.

## 7. Open items

1. Measure compute-bound parallel speedup and viewport-first latency with real kernels on owner hardware (the performance CI job).
2. Measure the cost of the C ABI crossing per node.
3. Decide the production node layout and the persistent result store.
4. Sanitizer, loom and Miri jobs for the engine and its ABI module.
5. Align the content hash (`Hash128`) with the oplog's BLAKE3 hash and the ids (INDEX.md, open conflicts).
6. Owner decision on the re-stated gate.

## 8. What Wave 1 must build first

1. `d8-engine` crate from the spike's `hash.rs`, `engine.rs` and `abi.rs`, with the compact node layout work scheduled but not blocking the first adopter; `abi.rs` moves into `d8-ffi` (one staticlib, ADR-001).
2. Kernel registration glue on the C++ side: thread-safe lane for tessellation and mass properties; the serialized lane for SubD and file I/O (ADR-005).
3. First adopter: tessellation as a content-keyed query per object, with the invalidation-correctness test (unrelated edit does not recompute, dependency edit does) and the existing tessellation tests unchanged, plus the measured edit-to-visible figure with the real kernel on a 10,000-object scene.
4. The publish step that warms each published B-rep once (`BoundingBox()`) before it becomes visible to other threads (ADR-005 rule).
5. Second adopter: mass properties; third: section views built as fan-in-64 trees.
