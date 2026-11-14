# Spike C: incremental derived-data engine and kernel call contract

Branch `w0-spike-c`, crate `dino8-core/spikes/graph/` (standalone `Cargo.toml` and lockfile, no workspace root).
Plan: section 5.4 (ADR-4) and section 5.2 (C ABI); report chapter 12.2.3 ("no regeneration" engine).
Everything below was measured on this 4-core machine, which about 20 agents were using at the same time
(load average 11 to 27 during the runs); see "Measurement method" for how that was handled.

## 1. Verdict

**Recommendation: keep ADR-4 (own engine, Rust, rayon pool, C ABI to the kernel) and proceed to Wave 1 with the conditions in section 10.**  The architecture works: the engine is deterministic for 1 to 16 threads, cancels cleanly,
explains every recomputation, reproduces a global recompute bit for bit, and a kernel function can be registered through a
plain C function pointer.  The two numeric gates of the plan are only partly met, and the reasons are structural, not noise:

| gate (plan 5.4) | result | where it is missed |
|---|---|---|
| Edit propagation under 50 ms at 1 M elements, typical parametric edit | **Met.** 1 M walls, 2.55 M derived nodes, serial: moving, resizing or editing one wall reaches the visible views in 0.05 to 0.16 ms and a fully consistent model in 0.05 to 0.17 ms (0.15 to 0.22 ms with 5 us queries); a 100-wall batch takes 7.6 ms (3.9 ms to the visible views). | Missed for edits that touch 80,000 or more derived nodes at the engine's current cost of about 0.5 to 0.7 us per recomputed node: level elevation (127,382 nodes) 74 ms, wall type (336,384 nodes) 501 ms, tag style (532,020 nodes) 419 ms. Demanding only a 5 x 5 tile block first brings the visible part of the level edit to 12 ms. |
| 20 times speedup on partial edits against a global recompute | **Met for edits that touch up to about 4% of the derived data when queries are kernel class (5 us or more):** one wall 32,000 times, 100 walls 346 times, one view 10,000 times (25 us queries, 100,000 walls). | Missed at 5% and above: level elevation 18 to 20 times, wall type 7.7 times, tag style 8.2 times (25 us queries). Missed for everything beyond a handful of walls when queries cost less than about 1 us, because the engine spends about 0.65 us per node: with 0.03 us kernels the 100-wall batch is 2.4 times and a level edit 1.1 times. A 20 times speedup needs touched fraction f <= 5% x c / (c + 0.66 us), c being the mean query cost. |

Further results: cold evaluation by the engine costs 1.02 times (25 us queries) to 26 times (0.03 us queries) the staged
global recompute; the evaluator keeps 94% parallel efficiency at 4 threads and 87% at 8 on sleeping kernels; a flat
aggregate over 100,000 rows is 150 times slower per edit than a fan-in-64 reduction tree (a design rule); memory is
about 310 bytes per derived node (1.1 GB for 1 M walls).

## 2. What was built

| file | content |
|---|---|
| `src/hash.rs` | 128-bit content hash (`Hash128`, `ContentHasher`, `ContentHash`), deterministic across platforms |
| `src/engine.rs` | the engine: `Db<V>`, `Ctx`, revisions, invalidation, evaluation, cancellation, priorities, explanation (100% safe Rust, `#![deny(unsafe_code)]`) |
| `src/workload.rs` | synthetic Revit-like model, shared kernels, staged global-recompute baseline, edit generators |
| `src/abi.rs` | the C ABI sketch (the only `unsafe` in the library) |
| `src/bin/regen_bench.rs` | benchmark driver (options listed in section 11) |
| `tests/` | `invalidation.rs` (18), `model.rs` (5, differential and property), `cancel.rs` (6), `workload.rs` (7), `abi.rs` (7), plus 4 unit tests in `hash.rs`: 47 tests |
| `results/` | raw output of every benchmark run quoted here |

Engine semantics (all covered by tests):

* **Inputs are revision-numbered.** `Db::edit(label, |tx| ...)` applies a batch of input changes under one new revision. Setting an
  input to a value with the same content hash changes nothing (no revision, no invalidation).
* **Queries are pure functions** `fn(&mut Ctx) -> Result<V, Interrupt>` registered per kind and keyed by `(kind, argument)`;
  **content-keyed queries** are keyed by the content hash of an argument *value* and shared by every caller that passes an
  equal value (memoization by content hash).
* **Dependencies are recorded automatically**: every `ctx.get / read / with / get_all / visit_all / content` is an edge, and the list is
  rebuilt on every recomputation, so dependencies that depend on values are exact and abandoned edges are removed.
* **Fine-grained invalidation with early cutoff.** An edit marks the transitive dependents stale through reverse edges (cost:
  the dependent closure, not the model). A stale node first verifies: it is revalidated without running if no dependency changed after
  it was last verified; a node recomputed to an equal content hash keeps its `changed_at`, so its dependents are not recomputed.
* **Demand driven and lazy**: only the transitive dependencies of demanded roots are evaluated; edits to data that no demanded root
  reads cost nothing until someone asks.
* **Restart-based parallel evaluation.** A query that asks for a dependency that is not up to date gets `Interrupt::Missing`; the
  scheduler evaluates all missing dependencies as a parallel batch and re-runs the function. Query functions never block, so a
  work-stealing pool cannot deadlock (the usual blocking design can deadlock with work stealing when a waiting worker steals a task that
  needs a node the same worker holds lower on its own stack; not reproduced here, avoided by construction). Small evaluations run on the calling thread (`serial_budget`) and
  never pay for a thread wake-up; large ones are split over the rayon pool.
* **Priorities** `Visible`, `Nearby`, `Background` are evaluated as ordered phases of one evaluation (a background node never starts
  before every visible node has finished); `EvalReport::class_done` gives the time at which each class was ready.
* **Cooperative cancellation** (`CancelToken`, `ctx.check_cancel()`): claimed but unfinished nodes return to the state they had, no
  partial value is stored, completed work is kept, and a later evaluation finishes the rest (every node completes exactly once overall).
* **Explanation**: `Db::explain_recompute(node)` answers "why did this recompute?" with the chain back to the edited input (and the
  edit label), and "why did it not?" with the early-cutoff dependencies. Sample from the 100,000-wall run, after the edit "move_wall_on":

```
tile[11] recomputed at r2
  0. tile[11] recomputed at r2 (changed) because join[412] changed at r2
  1. join[412] recomputed at r2 (changed) because geom[413] changed at r2
  2. geom[413] recomputed at r2 (changed) because wall[413] changed at r2
  3. wall[413] was set at r2 by "move_wall_on"
rnode[25] did NOT recompute at r2: it was last computed at r1 and no dependency changed after that; recomputed dependencies with an unchanged result (early cutoff): rnode[0]
```

* Errors: a cycle returns `EvalError::Cycle` with the path (no hang), a panicking query returns `EvalError::Panic` and leaves the database
  usable, a read of a never-created input returns `EvalError::MissingInput`.

## 3. Findings that change the plan or the design

1. **The first version depended on the evaluation order, found by the differential test.** Verification stopped at the first changed
   dependency, so whether the *other* stale dependencies were evaluated depended on which of them happened to be clean at scan time:
   the set of evaluated nodes (never the values) depended on the evaluation order, which differs between thread counts and runs. Verification now brings all previous
   dependencies up to date as one batch and then compares revisions. After the fix the full database state (values, revisions, dependency
   order, number of computations per node) is bit identical for 1 to 16 threads, and equal to a sequential reference model after every
   evaluation of random DAGs.
2. **Bounded fan-in is a design rule.** One schedule node over 100,000 rows costs 6.8 ms per single-wall edit and is slower than the global
   recompute (0.8 times); the same schedule as a fan-in-64 reduction tree costs 0.047 ms (about 125 times faster than the recompute).
   Every aggregate (schedules, view roots, quantity roll-ups, sheet indexes) must be a tree of kernel-class chunks. At 1 M walls the 1,600-tile
   view root already costs about 0.1 ms per edit; it should be two levels.
3. **Node granularity must be kernel class.** The engine costs about 0.65 us of CPU per recomputed node (locks, hashing, edge maintenance,
   25% of nodes run their function twice because of the restart protocol). That is about 3% of a 25 us query and 20 times a 0.03 us
   query. Do not create a node per vertex, face or cheap arithmetic step; create one per tessellation, section cut, join, tag, schedule chunk.
4. **Memory:** the node record is 224 bytes plus edges and the value; 312 bytes per derived node measured (1.09 GB RSS at 1 M walls, 3.58 M
   nodes). A production layout needs hot and cold fields split, edges in index arenas and small values inline (target 64 to 96 bytes per node).
5. **The restart protocol is why the kernel boundary is clean.** A kernel never calls back into the engine; the engine plans dependencies with a
   cheap callback, resolves them in parallel and calls the kernel exactly once (section 8).
6. **Parallel efficiency of the evaluator is high** (sleeping kernels, 512 leaf queries of 1 ms): see section 6.5. Compute-bound parallel
   speedup could not be measured on this machine (see "Measurement method").

## 4. Tests

`cargo test` runs 47 tests; `cargo clippy --all-targets -- -D warnings` is clean; the whole suite passed 25 consecutive runs under a machine load
of 12 to 20, and 15 more consecutive runs (load 5 to 13) were re-run at the end with the original timing bounds (cancel latency 250 ms, poll test 500 ms, ABI
poll test 1 s); none failed.  The three cancellation timing tests are wall-clock tests and could fail on a machine that is saturated for longer than the bound.

| group | what it proves |
|---|---|
| invalidation (18) | editing an unrelated input does not recompute (1 and 4 threads); editing a dependency recomputes exactly its dependents; early cutoff stops propagation; dynamic dependencies switch and the abandoned edge is removed; no-op edit keeps the revision; laziness (edits to undemanded data cost nothing until demanded); memoization; content-keyed sharing (3 callers, 2 computations); batch edits share one revision; explanation chain and "did not recompute" text; cycle and self-cycle errors; panic isolation; missing input; sequential and parallel priority order |
| model (5) | proptest (48 random DAGs, 6 thread configurations from 1 to 16 threads, random edit batches and demands, four operator kinds including value-dependent dependencies) and four larger random graphs: the engine equals a sequential reference model on every node after every evaluation (state, value hash, three revisions, number of computations, dependency order); values equal a from-scratch evaluation; an input outside the dependencies of the roots never causes work; `identical_results_for_one_to_sixteen_threads` compares a 3,000-node database digest for all 16 thread counts |
| cancel (6) | pre-cancelled token does nothing; cancellation at 12 different points for 1, 2, 4 and 8 threads leaves no claimed or half-written node, keeps completed work, and a resume completes every node exactly once; cancel after an edit; cancel latency (256 queries of 5 ms, cancelled by another thread once 8 have run: returns within the 250 ms bound and keeps the 8+ completed results); a query polling `ctx.check_cancel()`; cancel in the visible phase never starts the background phase |
| workload (7) | engine output equals the global recompute (serial and parallel) bit for bit after 24 mixed edits (move, cross-tile move, height, cosmetic, wall type, level, view cut, tag style, batches) for 1 and 3 threads; a single-wall edit recomputes a tiny fraction; a view-cut edit touches only that view; a cosmetic edit is cut off at the geometry; off-screen edits cost nothing until demanded; a schedule edit recomputes one node per tree level |
| abi (7) | a kernel registered through C function pointers is incremental; value-dependent dependencies through the planning callback; a `D8G_SERIALIZED` kernel is never entered concurrently; kernel failure becomes a status and an error record; cancellation polled through the ABI; registration without a thread-safety declaration is rejected; a kernel-supplied content hash drives early cutoff |

Not run: ThreadSanitizer, Miri and loom (stable toolchain only on this machine). The engine and the workload contain no `unsafe` (`#![deny(unsafe_code)]`);
`unsafe` appears in the FFI module `abi.rs` and in two clock calls of the benchmark binary. These are items for the Wave 1 sanitizer job.

## 5. Measurement method

* **Workload.** `N` walls in rooms of four on `L = 20` levels, 16 wall types. Derived: `geom` per wall, `join` per wall (4 geometries), `tag` for
  every second wall, `tile` per (view, tile) over a spatial index that is itself an input (so a move across a tile boundary changes
  dependencies), one plan `view` per level, and a wall schedule (leaf chunks of 64 geometries plus a fan-in-64 tree). 100,000 walls give
  254,989 derived nodes; 1,000,000 walls give 2,547,895. Queries cost the kernel arithmetic plus a synthetic, optimizer-proof delay of
  0, 5 or 25 us (geom and join; tag half of that); the 25 us case stands for a kernel call such as a tessellation.
* **Baseline ("global recompute").** The same kernels over plain arrays in five stages (geometries, joins and tags, tiles, views, schedule tree), no memoization,
  no dependency tracking, serial or rayon. It is deliberately the fastest possible global recompute, so engine overhead is not hidden.
* **Equality.** After the first edit of every scenario, and in 24-edit tests, the engine output (all views and the schedule) is compared bit for bit with the baseline.
* **Speedup** = global recompute time / (edit time + evaluation time) to a fully consistent state (all views and the schedule); "visible" = to the two visible
  views and the schedule panel only (views 3 to 20 are nearby or background).
* **Clock.** The machine had load average 11 to 27 on 4 cores, so wall-clock times of compute-bound work are inflated and noisy (parallel runs most of all).
  The headline tables therefore use **thread CPU time** (`CLOCK_THREAD_CPUTIME_ID`) of the single evaluating thread (`--threads 1 --clock cpu`), which does
  not depend on what other processes do. Each number is the median over the repetitions, minimum in brackets. Wall-clock 4-thread runs and process-CPU
  runs are in `results/` and summarized in 6.4. The kernel-serialization fallback of the plan does not change the speedup gates: the ratios are
  measured with serial evaluation of both sides.

## 6. Results

### 6.1 100,000 walls, kernel cost about 0.03 us (bare engine overhead), serial, thread CPU time

```
model: 100000 walls, 20 levels, 16 types, 13x13 tiles per level, schedule tree depth 3 (leaf 64, fan-in 64), engine threads 1
cold: engine 162.1 ms (254989 computed, 319715 steps, 64726 restarts, parallel=false), global recompute 6.2 ms parallel / 6.2 ms serial; engine/baseline = 26.36x parallel, 26.35x serial
graph: 358426 nodes (254989 queries), node record 224 B, RSS 112 MB (+71 MB for derived data = 292 B per derived node)
clock: thread CPU time (load independent); global recompute (all views, tags, joins, schedule): 6.15 ms with 1 thread(s), 6.15 ms serial
```

| scenario | nodes recomputed | to visible ms (min) | to fully consistent ms (min) | speedup vs global recompute (consistent) | speedup (visible only) |
|---|---:|---:|---:|---:|---:|
| move_wall_on | 11 | 0.038 (0.030) | 0.042 (0.031) | 146 | 162 |
| move_wall_off | 10 | 0.022 (0.016) | 0.044 (0.037) | 140 | 285 |
| move_wall_tile | 9 | 0.041 (0.036) | 0.042 (0.037) | 147 | 151 |
| height_wall_on | 9 | 0.035 (0.022) | 0.036 (0.023) | 172 | 177 |
| cosmetic_on | 1 | 0.017 (0.012) | 0.018 (0.013) | 349 | 371 |
| batch100 | 813 | 1.49 (1.42) | 2.59 (2.49) | 2.38 | 4.12 |
| view_cut | 170 | 0.518 (0.498) | 0.520 (0.499) | 11.8 | 11.9 |
| type_edit | 33,637 | 25.0 (24.8) | 47.6 (47.0) | 0.13 | 0.25 |
| level_off | 12,748 | 3.32 (3.11) | 5.68 (5.49) | 1.08 | 1.85 |
| level_on | 12,748 | 5.57 (5.52) | 5.58 (5.52) | 1.10 | 1.10 |
| tag_style | 53,400 | 11.3 (10.8) | 40.6 (40.2) | 0.15 | 0.54 |
| noop | 0 | 0.001 (0.001) | 0.002 (0.002) | 2,756 | 4,660 |

### 6.2 100,000 walls, kernel cost 25 us (geom and join) and 12.5 us (tag), serial, thread CPU time

```
model: 100000 walls, 20 levels, 16 types, 13x13 tiles per level, schedule tree depth 3 (leaf 64, fan-in 64), engine threads 1
cold: engine 5719.2 ms (254989 computed, 319715 steps, 64726 restarts, parallel=false), global recompute 5583.2 ms parallel / 5577.5 ms serial; engine/baseline = 1.02x parallel, 1.03x serial
graph: 358426 nodes (254989 queries), node record 224 B, RSS 112 MB (+71 MB for derived data = 292 B per derived node)
clock: thread CPU time (load independent); global recompute (all views, tags, joins, schedule): 5583.16 ms with 1 thread(s), 5577.55 ms serial
```

| scenario | nodes recomputed | to visible ms (min) | to fully consistent ms (min) | speedup vs global recompute (consistent) | speedup (visible only) |
|---|---:|---:|---:|---:|---:|
| move_wall_on | 11 | 0.170 (0.150) | 0.171 (0.151) | 32,552 | 32,766 |
| move_wall_off | 10 | 0.043 (0.036) | 0.166 (0.153) | 33,661 | 129,423 |
| move_wall_tile | 9 | 0.166 (0.156) | 0.167 (0.157) | 33,441 | 33,676 |
| height_wall_on | 9 | 0.173 (0.143) | 0.174 (0.144) | 32,045 | 32,288 |
| cosmetic_on | 1 | 0.045 (0.037) | 0.046 (0.038) | 121,680 | 124,518 |
| batch100 | 813 | 5.21 (4.59) | 16.1 (15.4) | 346 | 1,071 |
| view_cut | 170 | 0.556 (0.462) | 0.557 (0.464) | 10,008 | 10,043 |
| type_edit | 33,637 | 230 (229) | 728 (721) | 7.66 | 24.3 |
| level_off | 12,748 | 136 (124) | 305 (277) | 18.3 | 41.0 |
| level_on | 12,748 | 293 (292) | 293 (292) | 19.1 | 19.1 |
| tag_style | 53,400 | 75.5 (75.3) | 679 (674) | 8.21 | 74.0 |
| noop | 0 | 0.001 (0.001) | 0.002 (0.002) | 2,427,130 | 4,255,457 |

### 6.3 1,000,000 walls, serial, thread CPU time

Kernel cost about 0.04 us (bare engine overhead):

```
model: 1000000 walls, 20 levels, 16 types, 40x40 tiles per level, schedule tree depth 4 (leaf 64, fan-in 64), engine threads 1
cold: engine 1691.1 ms (2547895 computed, 3179745 steps, 631850 restarts, parallel=false), global recompute 112.2 ms parallel / 114.8 ms serial; engine/baseline = 15.08x parallel, 14.73x serial
graph: 3579952 nodes (2547895 queries), node record 224 B, RSS 1088 MB (+759 MB for derived data = 312 B per derived node)
clock: thread CPU time (load independent); global recompute (all views, tags, joins, schedule): 112.17 ms with 1 thread(s), 114.81 ms serial
```

| scenario | nodes recomputed | to visible ms (min) | to fully consistent ms (min) | speedup vs global recompute (consistent) | speedup (visible only) |
|---|---:|---:|---:|---:|---:|
| move_wall_on | 12 | 0.159 (0.131) | 0.161 (0.132) | 713 | 705 |
| move_wall_off | 12 | 0.051 (0.024) | 0.171 (0.151) | 672 | 2,206 |
| move_wall_tile | 9 | 0.153 (0.130) | 0.154 (0.131) | 747 | 735 |
| height_wall_on | 10 | 0.085 (0.069) | 0.087 (0.070) | 1,327 | 1,313 |
| cosmetic_on | 1 | 0.051 (0.049) | 0.052 (0.050) | 2,191 | 2,183 |
| batch100 | 883 | 3.85 (3.64) | 7.55 (7.09) | 15.2 | 29.1 |
| view_cut | 1,601 | 8.25 (8.16) | 8.25 (8.16) | 13.9 | 13.6 |
| type_edit | 336,384 | 264 (262) | 501 (496) | 0.23 | 0.43 |
| level_off | 127,382 | 44.2 (43.9) | 73.9 (73.0) | 1.55 | 2.60 |
| level_on | 127,382 | 73.6 (73.1) | 73.6 (73.1) | 1.56 | 1.56 |
| tag_style | 532,020 | 125 (124) | 419 (418) | 0.27 | 0.92 |
| noop | 0 | 0.002 (0.001) | 0.003 (0.002) | 36,059 | 62,176 |

Kernel cost 5 us (geom, join) and 2.5 us (tag):

```
model: 1000000 walls, 20 levels, 16 types, 40x40 tiles per level, schedule tree depth 4 (leaf 64, fan-in 64), engine threads 1
cold: engine 13130.3 ms (2547895 computed, 3179745 steps, 631850 restarts, parallel=false), global recompute 11621.0 ms parallel / 11548.4 ms serial; engine/baseline = 1.13x parallel, 1.14x serial
graph: 3579952 nodes (2547895 queries), node record 224 B, RSS 1088 MB (+759 MB for derived data = 312 B per derived node)
clock: thread CPU time (load independent); global recompute (all views, tags, joins, schedule): 11621.01 ms with 1 thread(s), 11548.42 ms serial
```

| scenario | nodes recomputed | to visible ms (min) | to fully consistent ms (min) | speedup vs global recompute (consistent) | speedup (visible only) |
|---|---:|---:|---:|---:|---:|
| move_wall_on | 12 | 0.172 (0.150) | 0.184 (0.151) | 62,610 | 67,483 |
| move_wall_off | 12 | 0.059 (0.041) | 0.220 (0.200) | 52,541 | 198,094 |
| move_wall_tile | 10 | 0.156 (0.152) | 0.157 (0.153) | 73,578 | 74,546 |
| batch100 | 881 | 4.10 (3.70) | 9.24 (8.69) | 1,249 | 2,831 |
| view_cut | 1,601 | 7.50 (7.17) | 7.50 (7.17) | 1,540 | 1,550 |
| type_edit | 336,384 | 691 (688) | 1944 (1915) | 5.94 | 16.8 |
| level_off | 127,382 | 304 (293) | 649 (644) | 17.8 | 38.2 |
| level_on | 127,382 | 659 (647) | 659 (647) | 17.5 | 17.6 |
| noop | 0 | 0.002 (0.001) | 0.003 (0.002) | 4,115,617 | 7,098,965 |

Viewport-limited demand (only a 5 x 5 block of tiles of the edited view is demanded first, everything else at background priority; "to visible" is the time
until those 25 tiles are ready, "to fully consistent" until all views and the schedule are):

| model | scenario | to visible ms | to fully consistent ms | speedup (consistent) |
|---|---|---:|---:|---:|
| 100,000 walls, 0.03 us queries | level elevation, whole view demanded | 5.92 | 5.92 | 0.99 |
| 100,000 walls, 0.03 us queries | level elevation, 5 x 5 tiles demanded first | 1.47 | 5.01 | 1.17 |
| 100,000 walls, 25 us queries | level elevation, whole view demanded | 198 | 198 | 19.8 |
| 100,000 walls, 25 us queries | level elevation, 5 x 5 tiles demanded first | 31.3 | 198 | 19.8 |
| 1,000,000 walls, 0.04 us queries | level elevation, whole view demanded | 62.6 | 62.7 | 1.73 |
| 1,000,000 walls, 0.04 us queries | level elevation, 5 x 5 tiles demanded first | 12.3 | 58.8 | 1.85 |

### 6.4 Parallel runs (4 threads)

Wall clock, 4 threads, machine at load average 15 to 27 (every number is inflated by the other agents; shown for completeness):

| run | move_wall_on | batch100 | view_cut | type_edit | level_on | tag_style |
|---|---:|---:|---:|---:|---:|---:|
| 100,000 walls, 0.03 us | 0.040 ms (633x) | 2.38 ms (10.7x) | 0.529 ms (48.1x) | 66.5 ms (0.38x) | 9.53 ms (2.67x) | 60.2 ms (0.42x) |
| 100,000 walls, 5 us | 0.069 ms (28,070x) | 8.54 ms (228x) | 0.670 ms (2,905x) | 361 ms (5.39x) | 88.0 ms (22.1x) | 264 ms (7.36x) |
| 100,000 walls, 25 us | 0.181 ms (55,172x) | 36.2 ms (276x) | 0.726 ms (13,781x) | 1571 ms (6.37x) | 415 ms (24.1x) | 1050 ms (9.52x) |
| 1,000,000 walls, 0.04 us | 0.124 ms (1,300x) | 10.4 ms (15.5x) | 14.1 ms (11.4x) | 705 ms (0.23x) | 111 ms (1.44x) | 608 ms (0.26x) |

Process CPU time of the 4-thread run (summed over all threads) against thread CPU time of the serial run, same edit, to a fully consistent state. A ratio near 1 means the parallel
evaluator adds almost no work, so an idle 4-core machine would need about a quarter of the CPU time as wall time at best:

| run | cold evaluation ms (4 thr / serial, ratio) | move_wall_on | batch100 | type_edit | level_on | tag_style |
|---|---|---:|---:|---:|---:|---:|
| 100,000 walls, 0.03 us | 185 / 162 (1.14) | 0.040 / 0.042 (0.95) | 2.10 / 2.59 (0.81) | 39.8 / 47.6 (0.84) | 7.44 / 5.58 (1.33) | 36.2 / 40.6 (0.89) |
| 100,000 walls, 25 us | 6131 / 5719 (1.07) | 0.187 / 0.171 (1.09) | 16.9 / 16.1 (1.05) | 806 / 728 (1.11) | 298 / 293 (1.02) | 716 / 679 (1.05) |
| 1,000,000 walls, 0.04 us | 1787 / 1691 (1.06) | 0.170 / 0.161 (1.06) | 6.93 / 7.55 (0.92) | 510 / 501 (1.02) | 78.8 / 73.6 (1.07) | 441 / 419 (1.05) |

### 6.5 Scheduler scaling with sleeping kernels (independent of machine load)

```
# parallel scaling of the evaluator (sleeping kernels: 512 leaf queries of 1 ms, 16 middle nodes, 1 root)
 threads      wall ms    speedup   efficiency
       1        547.3       1.00         100%
       2        281.3       1.95          97%
       4        145.5       3.76          94%
       8         78.5       6.97          87%
      16         46.5      11.76          73%
```

### 6.6 Schedule aggregation shape (100,000 walls, serial CPU time)

```
== leaf=64 fan_in=64
model: 100000 walls, 20 levels, 16 types, 13x13 tiles per level, schedule tree depth 3 (leaf 64, fan-in 64), engine threads 1
cold: engine 170.6 ms (254989 computed, 319715 steps, 64726 restarts, parallel=false), global recompute 5.9 ms parallel / 5.9 ms serial; engine/baseline = 28.86x parallel, 29.05x serial
move_wall_on      15 |       11       11        1       3 |        2.9   0.046(0.032)   0.047(0.033) |      125.8      125.0 |      128.8
move_wall_off     15 |       11       10        1       1 |        3.0   0.023(0.017)   0.049(0.036) |      121.9      121.1 |      262.2
height_wall_on    15 |       11        9        2       4 |        3.0   0.041(0.030)   0.042(0.031) |      140.4      139.5 |      144.8
== leaf=100000 fan_in=64
model: 100000 walls, 20 levels, 16 types, 13x13 tiles per level, schedule tree depth 1 (leaf 100000, fan-in 64), engine threads 1
cold: engine 150.5 ms (253401 computed, 318101 steps, 64700 restarts, parallel=false), global recompute 5.2 ms parallel / 5.2 ms serial; engine/baseline = 28.67x parallel, 28.74x serial
move_wall_on      15 |        9        9        0       3 |        3.6   6.949(6.564)   6.951(6.567) |        0.8        0.8 |        0.8
move_wall_off     15 |        9        9        0       1 |        3.9   6.815(6.480)   6.853(6.514) |        0.8        0.8 |        0.8
height_wall_on    15 |        9        7        2       4 |        3.9   6.807(6.478)   6.810(6.480) |        0.8        0.8 |        0.8
```

## 7. Where the targets are missed, and what would move them

Let `c` be the mean cost of a derived node's kernel and `f` the fraction of derived nodes an edit recomputes. Measured engine overhead is
0.64 us (100,000 walls) to 0.66 us (1 M walls) per node on a cold build and 0.47 us per node on the large incremental edits, so

    speedup(f) ~ (c + 0.03 us) / (f x (c + 0.66 us))        20 times needs   f <= 5% x c / (c + 0.66 us)

* 25 us queries: f <= 4.9%; measured: level elevation f = 5.0% gives 18.3 to 19.8 times. 5 us queries: f <= 4.4%. 1 us: f <= 3.1%. 0.03 us: f <= 0.4%.
* 50 ms at 1 M elements, serial: the engine handles about 2 million node recomputations per second, so edits that touch up to about 80,000 nodes
  fit. The typical parametric edit touches 10 to 900 nodes. What moves the large cases: (a) demanding only the on-screen tiles first (12 ms for
  the 127,382-node level edit, measured), (b) parallel evaluation (the 4-thread runs use 1.0 to 1.3 times the CPU of the serial runs, so an idle 4-core machine could reach about 3 times; not
  measurable here, the machine was saturated by other agents), (c) a cheaper node (section 3, item 4), (d) fewer, coarser nodes where kernels are cheap.
* Cold evaluation is a one-off (document open); with persisted content-addressed results it is replaced by validation.

## 8. Kernel call contract and C ABI sketch

The Rust engine never links against the C++ kernel: it calls function pointers. The working subset is `src/abi.rs`; the header below is what
cbindgen would emit (ABI version 1). Ownership: the engine owns every blob and output buffer; the kernel sees inputs as read-only views valid
for the call; the host copies results out into buffers it allocates; each side frees only what it allocated. Every function returns a status
and fills a caller-owned error record; Rust panics are caught at the boundary; kernel callbacks must not unwind (C++: catch everything and return a status).

```c
/* dino8_graph.h : incremental derived-data engine, C ABI v1 (sketch) */
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define D8G_ABI_VERSION 1
enum { D8G_OK = 0, D8G_NEED_DEPS = 1,                       /* NEED_DEPS: deps callback pushed more, call again */
       D8G_ERR_CANCELLED = -1, D8G_ERR_KERNEL = -2, D8G_ERR_PANIC = -3, D8G_ERR_BAD_ARG = -4,
       D8G_ERR_CYCLE = -5, D8G_ERR_MISSING_INPUT = -6, D8G_ERR_STALLED = -7 };
enum { D8G_THREAD_SAFE = 1,   /* audited: may be called concurrently from many threads            */
       D8G_SERIALIZED  = 2 }; /* engine takes one mutex around every call of every serialized kernel */

typedef struct D8gDb D8gDb;                    /* opaque database                                   */
typedef struct D8gOut D8gOut;                  /* opaque output builder (engine allocated)          */
typedef struct D8gDeps D8gDeps;                /* opaque dependency list under construction         */
typedef struct D8gCancel D8gCancel;            /* opaque, call-scoped view of the cancellation flag */
typedef struct D8gCancelToken D8gCancelToken;  /* opaque host-side token                            */

typedef struct { const uint8_t* data; size_t len; uint64_t hash_hi, hash_lo; } D8gBlobRef; /* read-only, valid during the call */
typedef struct { int32_t code; char message[256]; } D8gError;                              /* caller allocated */

/* Pure and deterministic: same (arg, inputs, user data) gives the same bytes and hash on every thread and call order. */
typedef int32_t (*D8gComputeFn)(void* user, uint64_t arg, const D8gBlobRef* inputs, uint32_t n_inputs,
                                D8gOut* out, const D8gCancel* cancel);
/* Cheap planning phase: push the (kind, arg) pairs needed; inputs holds those resolved so far, in request order.
   Return D8G_OK when the list is complete, D8G_NEED_DEPS after pushing more (dependencies that depend on values). */
typedef int32_t (*D8gDepsFn)(void* user, uint64_t arg, const D8gBlobRef* inputs, uint32_t n_inputs, D8gDeps* deps);

typedef struct {
  uint32_t abi_version;   /* D8G_ABI_VERSION                                                  */
  uint32_t flags;         /* D8G_THREAD_SAFE or D8G_SERIALIZED, exactly one is required         */
  const char* name;       /* stable name used in explanations and traces                       */
  D8gComputeFn compute;
  D8gDepsFn deps;         /* optional: NULL means the kernel is a pure function of arg alone    */
  void* user;             /* immutable cookie shared by all calls                               */
  uint32_t capacity;      /* arguments are 0..capacity (dense table); 0 selects a sparse table  */
} D8gKernelDesc;

/* ---- called by kernels ---- */
int32_t d8g_out_write(D8gOut*, const uint8_t* data, size_t len);        /* append result bytes (copied)        */
int32_t d8g_out_set_hash(D8gOut*, uint64_t hi, uint64_t lo);            /* canonical content hash of the result */
int32_t d8g_deps_push(D8gDeps*, uint32_t kind, uint64_t arg);
int32_t d8g_cancel_requested(const D8gCancel*);                         /* poll at least every ~1 ms of work    */

/* ---- called by the host (Rust or C++) ---- */
D8gDb*  d8g_db_new(uint32_t threads, uint32_t max_nodes);               /* threads == 1: calling thread only    */
void    d8g_db_free(D8gDb*);
int32_t d8g_register_input_kind(D8gDb*, const char* name, uint32_t capacity, uint32_t* out_kind);
int32_t d8g_register_kernel_query(D8gDb*, const D8gKernelDesc*, uint32_t* out_kind, D8gError*);
int32_t d8g_create_input(D8gDb*, uint32_t kind, uint64_t arg, const uint8_t* data, size_t len, uint32_t* out_node);
int32_t d8g_node(D8gDb*, uint32_t kind, uint64_t arg, uint32_t* out_node);
int32_t d8g_set_input(D8gDb*, uint32_t node, const uint8_t* data, size_t len, int32_t* changed);  /* one-input edit */
int32_t d8g_evaluate(D8gDb*, const uint32_t* nodes, size_t n, const D8gCancelToken*, D8gError*);
int32_t d8g_read(D8gDb*, uint32_t node, uint8_t* buf, size_t cap, size_t* len, uint64_t hash[2]);  /* copy out */
uint32_t d8g_computes(D8gDb*, uint32_t node);
D8gCancelToken* d8g_cancel_token_new(void);
void    d8g_cancel_token_request(D8gCancelToken*);                      /* any thread, while evaluating         */
void    d8g_cancel_token_free(D8gCancelToken*);
#ifdef __cplusplus
}
#endif
```

Not in the spike, needed for B-rep results: results that are kernel objects travel as an 8-byte handle id in the blob plus a canonical hash from
`d8g_out_set_hash`; the shared handle table with reference counts of plan 5.2 owns the object, and `D8gKernelDesc` gains a `release(user, handle)` callback
that the engine calls when the last blob holding the handle dies (callable from any thread). Batch edits (`d8g_edit_begin/set/commit`) and a `Priority`
array in `d8g_evaluate` are mechanical extensions of the Rust API.

**Contract a kernel function must satisfy to be registered `D8G_THREAD_SAFE`** (for the kernel thread-safety audit; any violation means `D8G_SERIALIZED`):

1. Called concurrently from up to N rayon worker threads, with different `arg` values and possibly the same `user` cookie; inputs are immutable and shared.
2. No global or static mutable state, no mutable function-local statics, no unsynchronized caches or lazily initialized singletons (initialize at registration), no
   thread-local state whose contents affect results.
3. Deterministic: bytes and hash depend only on `(arg, inputs, user)`, not on the thread, call order, time, addresses (no iteration of pointer-keyed
   containers), uninitialized memory or the floating-point environment (rounding mode, flush-to-zero are per thread).
4. No call back into the engine other than `d8g_cancel_requested`; no re-entrancy; no blocking I/O; no nested thread pools.
5. Allocation through a thread-safe allocator; objects handed out through handles are destroyed by a thread-safe `release`.
6. Polls `d8g_cancel_requested` at least every millisecond of work and returns `D8G_ERR_CANCELLED` without producing output.
7. Never throws or unwinds across the boundary.
8. The planning (`deps`) callback obeys the same rules (it is called concurrently for `D8G_THREAD_SAFE` kernels) and is cheap, microseconds. For
   `D8G_SERIALIZED` kernels the engine holds the kernel mutex around the planning callback as well as around `compute`.

If the audit finds the kernel unsafe to call concurrently, the whole kernel is registered `D8G_SERIALIZED`: kernel calls are serialized (one mutex, tested), the
engine keeps all its other behavior, the speedup gates above are unchanged (they were measured with serial evaluation on both sides), and only the parallel
speed-up of large recomputations is lost.

## 9. Known limits of the spike

* **The kernel thread-safety audit and the thread-sanitizer run required by plan 5.2 were not part of this spike's work and were not done.** What this spike delivers for them is the contract in section 8
  and the tested serialized fallback; whether the real kernel may be registered `D8G_THREAD_SAFE` is open.
* The two numeric gates are re-stated in section 10 rather than met as written (section 1); the speedup figures compare against a deliberately fast global recompute of
  *synthetic* kernels, not against the real kernel.

* One evaluation at a time per database (a lock), and edits need exclusive access: the protocol is cancel, join, edit, evaluate. Reads of clean values are concurrent.
* Priorities are ordered phases, not preemption: a running query is never interrupted except through its own `check_cancel` polling.
* Content-keyed nodes are never freed and nothing is persisted; a failing query surfaces as an error, not as a cached failure value.
* The node record is large (section 3, item 4), every get takes a per-node mutex, and 25% of nodes run their function twice (the restart protocol; kernels avoid this
  through the planning callback).
* The benchmark kernels are synthetic (arithmetic plus a calibrated delay); real kernel costs, real model shapes and real parallel speedup are open. The cost of
  crossing the C ABI (a function-pointer call and a few small allocations per node) was not measured separately; the engine overhead in the tables is for Rust callbacks.
* The 50 ms gate was measured with serial evaluation; the parallel gain is inferred from CPU totals, not measured.

## 10. Recommendation and next steps

1. **Adopt ADR-4.** Build the production engine from this design, behind the C ABI of section 8, as the first user of the Rust/C++ boundary of Spike R1.
2. **Re-state the Wave 1 gates** as: 20 times speedup for edits that touch up to 4% of the derived data of kernel-class queries (5 us and up);
   50 ms edit-to-visible at 1 M elements for edits that touch up to 80,000 nodes serially, and for any edit through viewport-first demand. Keep the original
   figures as stretch goals for the parallel build on owner hardware.
3. **Design rules for adopters** (tessellation first, then sections, schedules, Flow): one node per kernel-class unit of work (5 us and up); every aggregate is a
   fan-in-64 tree; roots demanded by the UI are tiles or chunks, not whole documents; geometry kernels declare dependencies through the planning callback.
4. **Production hardening before Wave 2**: compact node layout (about 3 times smaller), index-arena edges, lock-free reads of clean values, garbage
   collection of content-keyed nodes, failure as a cached value instead of an error, a persistent content-addressed result store (turns the cold build into a validation),
   evaluation from several threads at once, a two-level view root, loom and ThreadSanitizer jobs on the engine and on `abi.rs`, Miri on `abi.rs`, and re-measurement
   of compute-bound parallel speedup on the owner's hardware (the perf CI job).

## 11. Reproduce

```
cd dino8-core/spikes/graph
CARGO_BUILD_JOBS=2 cargo test                        # 47 tests
CARGO_BUILD_JOBS=2 cargo clippy --all-targets -- -D warnings
CARGO_BUILD_JOBS=2 cargo build --release --bin regen_bench
./target/release/regen_bench --walls 100000 --threads 1 --clock cpu --kernel-us 25 --reps 15 --baseline-reps 5
./target/release/regen_bench --walls 1000000 --threads 1 --clock cpu --kernel-us 0 --reps 9
./target/release/regen_bench --scaling
```

Options: `--walls --levels --types --threads --kernel-us --reps --baseline-reps --scenarios a,b --clock wall|cpu|proc --leaf --fan-in --verify-all --no-explain`.
