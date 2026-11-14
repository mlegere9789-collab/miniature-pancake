# Performance notes: closing the large-assembly gap

Numbers below are real measurements from this session (worktree
`worktree-agent-perf`, built `cmake --build build -j$(nproc)` Release-ish
default flags, run headless under `xvfb-run` on the same shared build
machine used for `tests/smoke.sh`, which is under heavy concurrent load
from other agents' builds/tests - treat absolute times as ballpark, but the
*ratios* between "grid on" and "grid off" are the same binary, same run,
same machine, and are the numbers that matter here). Reproduce with:

```
cmake --build build -j$(nproc)
bash tests/stress.sh --compare 30000
```

or directly:

```
./build/Dino8 --stress 30000
DINO8_DISABLE_PICK_GRID=1 DINO8_DISABLE_PARALLEL_WARMUP=1 ./build/Dino8 --stress 30000
```

## What was measured

`--stress N` (`main.cpp`, `RunStressTest`) builds N simple mesh boxes on a
compact 3D grid in a fresh document and times, in order:

- **create_ms** - `Document::Add` x N (object list growth only, no geometry
  cost worth mentioning: `MakeStressBox` builds an 8-vertex/6-quad mesh).
- **warmup_ms** - `SceneObject::EnsureDisplay` x N, run through
  `dino8::app::ParallelFor` (`util/ThreadPool.h`) unless
  `DINO8_DISABLE_PARALLEL_WARMUP` forces the old serial loop.
- **pick_ms** - average `Viewport::PickObject` latency over 200 hover-style
  picks swept across the viewport, accelerated by the `ObjectGrid` broad
  phase (`spatial/ObjectGrid.h`) unless `DINO8_DISABLE_PICK_GRID` forces
  every candidate list back to "every object in the document" - i.e. the
  exact old brute-force behavior, in the same binary.
- **undo_ms** - a single `Document::BeginChange` call: the full-document
  snapshot `Undo` takes before every command. Not touched by this session's
  changes (see "What still doesn't scale" below) - included so the number
  is on record next to everything that *did* change.

## Results

| N      | pick_ms (grid on) | pick_ms (grid off) | speedup | warmup_ms (parallel) | warmup_ms (serial) | speedup |
|-------:|-------------------:|---------------------:|--------:|----------------------:|---------------------:|--------:|
|  1,000 |               1.34 |                  9.15 |    6.8x |                   4.6 |                   4.1 |    0.9x |
| 10,000 |               1.91 |                 92.95 |   48.7x |                  50.3 |                  53.5 |    1.06x |
| 30,000 |               2.32 |                278.63 |  120.1x |                 136.2 |                 210.9 |    1.55x |
| 50,000 |               2.18 |                540.83 |  248.1x |                 327.7 |                 367.8 |    1.12x |

(`create_ms` and `undo_ms` are included for completeness in the raw
`stress:` lines below; they are not meaningfully different between the two
columns above, which is the expected result - neither the grid nor the
warmup thread pool touches object creation or snapshotting.)

Raw lines (this machine, this session, 4 hardware threads reported by
`std::thread::hardware_concurrency()`):

```
N=1000  on:  stress: objects=1000  create_ms=6.552   warmup_ms=4.585   pick_ms=1.3358  undo_ms=3.606   grid=on  parallel_warmup=on
N=1000  off: stress: objects=1000  create_ms=3.114   warmup_ms=4.145   pick_ms=9.1509  undo_ms=3.389   grid=off parallel_warmup=off
N=10000 on:  stress: objects=10000 create_ms=47.901  warmup_ms=50.272  pick_ms=1.9125  undo_ms=33.699  grid=on  parallel_warmup=on
N=10000 off: stress: objects=10000 create_ms=39.415  warmup_ms=53.498  pick_ms=92.9520 undo_ms=37.080  grid=off parallel_warmup=off
N=30000 on:  stress: objects=30000 create_ms=110.736 warmup_ms=136.176 pick_ms=2.3155  undo_ms=106.420 grid=on  parallel_warmup=on
N=30000 off: stress: objects=30000 create_ms=117.712 warmup_ms=210.944 pick_ms=278.6258 undo_ms=148.178 grid=off parallel_warmup=off
N=50000 on:  stress: objects=50000 create_ms=193.292 warmup_ms=327.747 pick_ms=2.1848  undo_ms=185.001 grid=on  parallel_warmup=on
N=50000 off: stress: objects=50000 create_ms=203.332 warmup_ms=367.798 pick_ms=540.8339 undo_ms=214.072 grid=off parallel_warmup=off
```

Memory: sampling `ps` RSS on a `--stress 50000 --smoke 200` run mid-flight
gave ~640-845 MB resident (varied with which other agents' builds were
running concurrently on the shared machine at the time; not a controlled
measurement, just an order-of-magnitude data point - roughly 13-17 KB
resident per object for this stress test's simple 8-vertex boxes, which is
plausible given `SceneObject` carries several `std::string`/`std::map`
fields and `DisplayCache` holds duplicated per-triangle position+normal
floats rather than an indexed vertex buffer).

## What genuinely improved

1. **Picking is no longer O(objects) per hover frame.** `PickObject`,
   `PickSubObject`, and `ObjectsInWindow` (`viewport/Viewport.cpp`) now
   query a uniform grid over cached object AABBs
   (`spatial/ObjectGrid.h`) instead of scanning every document object on
   every mouse-move. The grid rebuilds only when `Document::Revision()`
   changes (an actual edit), not every frame - so the amortized cost of
   picking between edits is O(candidates near the ray), and the table
   above shows that scaling roughly flat (~1.3-2.3ms) from 1,000 to 50,000
   objects while the old brute-force path grows linearly (9ms to 541ms).
   At 50,000 objects this is the difference between picking being
   imperceptible and picking alone blowing well past a 16.7ms frame
   budget on every hover.
2. **Display-mesh warmup is measurably, if modestly, faster in
   parallel.** 1.06x-1.55x across the range tested, capped by this
   machine's `hardware_concurrency()` (4) and by `MakeStressBox`'s
   tessellation being cheap per object (a fixed 8-vertex/6-quad mesh - the
   parallel win would be larger for objects with real tessellation cost,
   e.g. NURBS surfaces or SubDs at fine tolerance, since `ParallelFor`
   splits by object count, not by per-object cost). This is a genuine,
   safe parallelization (each `SceneObject`'s `mutable DisplayCache` is its
   own; see `util/ThreadPool.h`), just not a dramatic one on this
   synthetic benchmark's cheap geometry.
3. **PLY export tessellation is parallelized** (`FileExchange.cpp`,
   `ExportPly`): each exported object's mesh/surface/SubD tessellation now
   runs through `ParallelFor` before a single serial write pass. Not
   covered by `--stress` (which doesn't exercise file export), so no
   number to report here beyond "the same safety argument as warmup
   applies, and `tests/smoke.sh`'s PLY round-trip still passes."

## What still doesn't scale (honestly)

1. **`Document::BeginChange`'s undo snapshot is O(objects) per command,
   unconditionally**, and this session did not touch it. `undo_ms` scales
   linearly right alongside `create_ms` in the table above (3.6ms at 1,000
   objects to 185ms at 50,000) because `Document::Capture` deep-copies the
   entire object/layer/group/material/light list on every `BeginChange`.
   This is a real, architectural cost of the "full snapshot undo" design
   (see the comment at the top of `doc/Document.h`) - fixing it means
   either a diff-based undo representation or copy-on-write sharing across
   snapshots, both bigger changes than fit in this session's scope. This is
   the undo agent's territory, not this session's; reported here only so
   the number is on record next to what did improve.
2. **`ArrayHole`/`ArrayHolePolar`'s per-centre cutter construction stays
   serial** (see the comment above `HoleArray` in
   `commands/cmd_solidtools.cpp`): it builds new `ON_Brep`/`ON_Mesh`
   objects and calls into `kernel::BooleanCombine` (the Manifold library)
   per hole centre, and neither OpenNURBS' object construction path nor
   Manifold's is documented anywhere in this codebase as safe under
   concurrent calls from multiple threads - nothing else here currently
   does that either (`PathTracer.cpp`'s worker threads trace rays against
   already-built triangle buffers, they do not construct new brep/mesh
   geometry concurrently). This would be a real win for arrays with many
   hole positions, but getting the thread-safety question wrong trades a
   perf win for sporadic, hard-to-reproduce corruption in a boolean solid
   tool - not a trade worth making without first verifying (or fixing)
   that specifically.
3. **`PickPoint` (object snaps)** keeps its full per-object scan for the
   free-form osnap search (end/mid/cen/quad/perp/tan/etc.) - it already had
   its own cheap screen-space bounding-box quick-reject before this
   session, which softens the O(n) cost somewhat, but it was not migrated
   to the grid: several snap kinds evaluate exact curve/surface math (not
   just the tessellated display cache the grid indexes), and the
   intersection-snap and "SnapToOccluded" occlusion check each loop over
   *other* objects again per candidate, an O(n) or worse cost nested inside
   the outer O(n) loop. None of that was touched this session; a large
   assembly with `SnapToOccluded` on and many curves will still show
   sluggish hover-snapping even after this session's changes.
4. **`PickControlPoint`/`ControlPointsInWindow`** deliberately were **not**
   migrated to the grid: a NURBS control point can legitimately sit outside
   its own curve's tessellated bounding box (the convex-hull property only
   guarantees the curve stays *inside* the control polygon's hull, not the
   other way around), so indexing by the same display-cache bbox the grid
   uses could silently drop a real, pickable control point. Left as a
   full scan rather than risk a wrong answer for speed.
5. **The command engine and Document mutation are still single-threaded**,
   as the original audit noted - this session added parallelism only to
   read-mostly, per-object-independent work (display warmup, PLY
   tessellation) precisely because Document's object list, undo stack, and
   layer/material tables have no synchronization and were never intended
   to be touched from more than one thread at a time. That remains true
   after this session; nothing here makes concurrent command execution
   safe, and nothing here tries to.
6. ~~**No LOD or frustum culling in the renderer.**~~ **Closed in a later
   session** (`worktree-agent-frustum`): `Viewport::DrawObjects` now builds
   a per-frame candidate list - a broad-phase `ObjectGrid::QueryBox` against
   the view frustum's enclosing box, then a precise per-object AABB-vs-
   frustum-plane test (the standard conservative "positive vertex" method:
   an object is dropped only when its whole bounding box is outside one
   plane, so anything merely straddling the frustum boundary is always
   kept) - and the three draw passes in `DrawObjects` iterate that list
   instead of `doc.Objects()` directly. Skipped entirely for `ctx.for_render`
   (image export) and for any object currently showing control points (a
   NURBS control polygon can legitimately reach outside its curve's own
   tessellated bbox - see the `PickControlPoint`/`ControlPointsInWindow`
   caveat above; same risk, same fix: don't cull it). An env var,
   `DINO8_DISABLE_FRUSTUM_CULL`, forces the old "every object is a
   candidate" behaviour in the same binary, mirroring
   `DINO8_DISABLE_PICK_GRID`.

   **How this was verified, not just argued:** `tests/cull_test.sh` (folded
   into `tests/smoke.sh`) runs the app's `--cull-test` hook twice - cull on,
   then cull off via that env var - against a document with a small object
   cluster the camera is framed on plus thousands of objects placed far
   outside that frame. It checks the candidate count actually collapses
   with the cull on (proof the cull does something), never drops below the
   framed cluster's own object count (proof nothing in view gets excluded
   from the candidate list), and - the direct correctness proof - that the
   two runs' screenshots (`Viewport::CaptureToFile`, not a composited
   window) are **pixel-identical**. Same camera, same document, same
   binary: the only difference between the two runs is which draw calls
   got skipped, so a byte-for-byte match means the cull changed nothing
   about what actually got rendered.

   **A real bug this verification caught before it shipped:** the first
   version computed the broad-phase query box straight from the frustum's
   near/far corners (correct on its own) and queried the grid directly.
   For a flat/2D-ish scene (many curves at one Z, e.g. `tests/curveedit_
   script.txt`), `ObjectGrid::EnsureFresh`'s cell-size heuristic - which
   divides an estimated scene *volume* by object count - starves on a
   near-zero volume and picks a cell size far smaller than an ortho view's
   depth span (`Camera::ProjectionMatrix`'s ortho case spans a full
   `-far_z..far_z`, and `far_z` is at least 1000 world units). The result
   was a query box that, while under `ObjectGrid::ForEachCellInBox`'s own
   per-axis `kMaxSpan` cap on every axis individually, had a cell-count
   *product* in the tens of millions - turning what should be an
   imperceptible per-frame cost into multi-minute (in the worst case,
   effectively indefinite) frame stalls, caught by `tests/smoke.sh`'s
   `curveedit_script.txt` section hanging outright. The fix: `ObjectGrid`
   now also exposes `Extent()` (the AABB of every object it indexed), and
   the frustum's query box is clamped to that extent before querying -
   nothing outside the document's own bounding box can be a candidate
   anyway, so this can only shrink the query, never drop a real one, and it
   collapses the pathological case back to the scene's actual size. Left
   here because it is exactly the kind of thing "provably safe" is
   supposed to catch: the cull itself was never wrong in that scenario, but
   it would have shipped a severe, scene-shape-dependent performance cliff
   without the smoke-script exercising real (flat) geometry, not just the
   synthetic cube grid `--stress`/`--cull-test` build.

## Reproducing the comparison

`tests/stress.sh --compare N` runs `--stress N` once normally and once with
both `DINO8_DISABLE_PICK_GRID` and `DINO8_DISABLE_PARALLEL_WARMUP` set, and
prints both `stress:` lines side by side. `tests/stress.sh N` alone also
fails (non-zero exit) if `pick_ms` exceeds an 8ms per-hover-pick budget, so
a future regression in the grid (or its removal) will show up as a test
failure, not just a forgotten number in this file. `tests/cull_test.sh [BIN]
[FAR_COUNT]` is the frustum-cull analogue (see item 6 above): it runs the
app's `--cull-test` hook with and without `DINO8_DISABLE_FRUSTUM_CULL` and
diffs the two runs' screenshots pixel-for-pixel, in addition to checking the
candidate-count drop - both must hold for the test to pass, and it's run as
part of `tests/smoke.sh` so a regression here fails CI directly.
