# PARITY_MAP — current status (lean snapshot)

**This file is the required reading for dispatch decisions.** `PARITY_MAP.md` in this directory is the full historical round-by-round narrative log (382 rounds, ~10,750 lines) — it is an archival record only; nothing requires reading it to pick up the next unit of work. See `/DINO8_PLAYBOOK.md` at the repo root for the process this file is part of.

As of commit `ab20e4ce` (last full re-score).

## Kernel — headline 78.1% (total weight 17.75, 17 categories)

| Category | Weight | Items | Present | Partial | Missing | % |
|---|---|---|---|---|---|---|
| Topology | 1.0 | 27 | 13 | 14 | 0 | 74.1% |
| Geometry representation | 1.0 | 29 | 23 | 6 | 0 | 89.7% |
| Boolean operations | 1.5 | 25 | 9 | 15 | 1 | 66.0% |
| Blending & chamfering | 1.5 | 24 | 6 | 18 | 0 | 62.5% |
| Sweeping/lofting/extruding/revolving | 1.0 | 29 | 7 | 22 | 0 | 62.1% |
| Offsetting & shelling | 1.0 | 27 | 3 | 24 | 0 | 55.6% |
| Local/direct-edit | 1.0 | 28 | 8 | 20 | 0 | 64.3% |
| Intersections & projections | 1.5 | 29 | 29 | 0 | 0 | 100% |
| Healing | 1.0 | 30 | 23 | 7 | 0 | 88.3% |
| Mass properties & spatial queries | 1.0 | 30 | 28 | 2 | 0 | 96.7% |
| Tessellation | 1.0 | 25 | 20 | 5 | 0 | 90.0% |
| Transformations/patterns/splitting | 0.5 | 22 | 12 | 10 | 0 | 77.3% |
| Kernel data exchange | 1.0 | 27 | 14 | 9 | 4 | 68.5% |
| Feature operations | 1.0 | 24 | 6 | 18 | 0 | 62.5% |
| Surface/Curve ops (curves) | 1.0 | 28 | 28 | 0 | 0 | 100% |
| Surface/Curve ops (surfaces) | 1.0 | 29 | 23 | 6 | 0 | 89.7% |
| SubD kernel support | 0.75 | 22 | 16 | 6 | 0 | 86.4% |

**Former blocker, now fixed:** the fragment-loop test-suite-abort crash (root cause commit `878ecdb`) is root-caused and fixed, with a permanent regression test — `TestBooleanCombineGeneralFlatSidedBossCrashRegression` (`dino8-kernel/tests/test_basic.cpp`), which reproduces the exact previously-crashing fixture (a flat-sided box boss flush on a bigger box's face) unwrapped and confirms it no longer throws "an edge is claimed by 3 or more fragment loops," now asserting the exact closed-form volume for every op. Confirmed to fail on pre-fix code and pass on post-fix code via a standalone harness using the real test scaffold, not assumed. Root cause: `CoincidentOverlapBoundaryCurves()` (`surface_intersect.cpp`, added by `878ecdb`) silently dropped the sample at a detected coincident region's own corner whenever that corner sat exactly on the other face's own domain boundary — `SurfaceClosestPoint()`'s Newton solve requires the full unconstrained gradient to vanish, which a genuine boundary-clamped closest point can never satisfy — undermeasuring the region `BooleanCombineGeneral`'s own whole-face coincidence override relies on. Fixed with a new, narrowly-scoped `ClosestPointAllowingDomainBoundary()` helper used only at that one function's own 4 rectangle corners — not a tolerance widening, not a try/catch, and the shared `SurfaceClosestPoint()`/`SurfaceClosestPointGlobal()` every other caller in the file depends on is left untouched. Two broader first-draft versions of this fix (one editing the shared closest-point function directly, one widening `IntersectSurfacesOverlap()`'s own region-acceptance without accounting for periodic/pole surface directions) were each caught regressing a different, previously-passing test by a full-suite rebuild-and-rerun, and were narrowed before landing — not shipped assumed-safe. Full kernel suite: 11050/11050 checks passing, 0 failures (pre-fix baseline: 11040/11040, 0 failures; the +10 is this new test's own checks, nothing else changed). **Separately-disclosed scope, unchanged by this fix:** curved-face coincidence and multi-boss/oblique flat-sided configurations remain unexercised — proven on the canonical single-flat-boss fixture, not asserted for every shape; item counts below are unchanged (9/15/1/25) since this closes a blocking crash, not a new feature item, and the "Coplanar / coincident face handling" bullet's own broader scope stays `[partial]`.

**Follow-up (this pass): two of that disclosure's three named gaps are now closed, confirmed broken first.** A standalone repro (linked against `libdino8_kernel.a`, not assumed from the historical description) confirmed, before any code change: a cylindrical boss's circular cap flush on a box's top face gave a grossly wrong Union volume (44.39 instead of the correct ~65.77); a box boss rotated 31° about Z, otherwise identical to the already-fixed axis-aligned fixture, reproduced the exact same "an edge is claimed by 3 or more fragment loops" crash on Union and wrong volumes on Intersection/Difference; two separate box bosses on the same bigger face already worked correctly even before this pass (no code change needed, only a permanent test). Root cause of the first two (confirmed by reading `CoincidentOverlapBoundaryCurves()`'s own doc comment, not assumed): it reports the coincident region as only its enclosing AXIS-ALIGNED rectangle in the walked face's own (u, v) domain, never the true outline — correct for an axis-aligned box footprint (rectangle == its own bbox) but wrong for a circle or a rotated rectangle, both strictly smaller than their own circumscribing box. Fixed in `BooleanCombineGeneral` (`boolean_general.cpp`), not in the shared `CoincidentOverlapBoundaryCurves()` itself: an exact polygon-containment test (does one face's own real trim-boundary loop lie entirely inside the other's, via `PointInPolygon()` on both loops projected into a shared 2D plane frame) now runs first, and when it reaches a clean asymmetric verdict, the smaller face's own exact boundary loop — not a re-derived approximation — becomes the imprint curve, each point reprojected onto the bigger face via `ClosestPointAllowingDomainBoundary()` (393e32fc's own fix, exported and reused verbatim, not reimplemented). Guarded off (falls through to the pre-existing bbox path, unaffected) whenever either face has a singular surface side — found and confirmed by a full-suite rebuild-and-rerun regressing `TestBooleanCombineGeneralAsymmetricCoincidentSubFace` (a `Brep::Extrude()`-built boss with a FAN-capped bottom face: one side of its trim loop is a degenerate apex point with two radial chords, not a simple boundary polygon, which corrupted the imprint curve into a self-touching "keyhole" before this guard was added). New permanent regression tests: `TestBooleanCombineGeneralCircularFootprintBossCoincidence`, `TestBooleanCombineGeneralObliqueFlatSidedBossCoincidence`, `TestBooleanCombineGeneralMultiBossFlatSidedCoincidence` (`dino8-kernel/tests/test_basic.cpp`), each checking real closed-form volumes (not just "doesn't throw"). Full kernel suite: 11073/11073 checks passing, 0 failures (pre-fix baseline: 11050/11050, 0 failures; the +23 are these three new tests' own checks, nothing else changed — confirmed via two independent full-suite runs, the first of which caught the FAN-cap regression above before it landed). **Still disclosed, not reached by this pass:** true curved-SURFACE coincidence (e.g. two cylindrical walls overlapping over a 2D area, not just a flat cap with a curved outline) — `surface_intersect.cpp`'s own `IntersectSurfacesOverlap()`/`CoincidentOverlapBoundaryCurves()` are explicitly documented as reliable for flat/coplanar surfaces only, "NOT for curved," and this pass did not touch that; a face built from a FAN cap (Extrude/Revolve/Pipe's flat end caps) never takes the new exact-polygon path at all (the singular-side guard above), so it still relies on the older, narrower bbox path for any coincidence it's involved in; and subtracting one boss back out of a result where TWO bosses already share the same face (so that face now has two independent holes) still throws the pre-existing "3 or more fragment loops" refusal — confirmed directly while building the multi-boss test, a different, deeper limitation this pass does not reach, so not asserted in that test.

## App — headline 84.3% (total weight 7.75, 8 categories)

| Category | Weight | Items | Present | Partial | Missing | % |
|---|---|---|---|---|---|---|
| Command system | 1.5 | 19 | 12 | 5 | 2 | 76.3% |
| 2D drafting & annotation | 1.0 | 18 | 18 | 0 | 0 | 100% |
| Viewport display/rendering | 1.0 | 18 | 16 | 2 | 0 | 94.4% |
| Scripting/automation | 1.0 | 15 | 12 | 3 | 0 | 90.0% |
| File I/O | 1.0 | 17 | 11 | 3 | 3 | 73.5% |
| SubD app-toolset | 0.75 | 24 | 19 | 4 | 1 | 87.5% |
| UI/UX | 1.0 | 19 | 14 | 4 | 1 | 84.2% |
| Ecosystem | 0.5 | 16 | 7 | 6 | 3 | 62.5% |

## Update protocol

Do not hand-edit the percentages above without clearing the gate in `/DINO8_PLAYBOOK.md` §3 and §5 for the specific item(s) moved. Note the commit(s) and test name(s) that justified any row change in the commit message, not in prose here — this file stays a table, not a log.
