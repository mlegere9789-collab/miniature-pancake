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
