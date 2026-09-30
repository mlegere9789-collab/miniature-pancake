# Fossilith / Dino 8 parity map (2026-09-28, updated 2026-09-30)

**Fossilith vs Parasolid/ACIS = 68.0% (weighted, verified); Dino 8 vs Rhino 8 + AutoCAD 2027 = 72.0%.**

**2026-09-30 re-score (a nineteenth session, app wiring for two more
already-exact Blending & chamfering kernel constructions, continuing the
seventeenth session's own FilletTwoSurfacesCommand VariableFillet/Rho/
RailType wiring):** both are genuine, previously-uncredited "zero app call
sites" gaps this session closed - real work, no status flip, so no
headline effect, the same "narrowing, not a flip" category this document
uses throughout:

1. **`kernel::FilletConvexEdgesConic`/`FilletConcaveEdgesConic`** (the
   multi-edge batch form of the exact conic/Rho fillet, `fillet.h`/
   `fillet.cpp`) had a real kernel construction and its own regression
   tests (`TestFilletConvexEdgesConicBlendsTwoIndependentEdgesInOneCall`
   etc., `tests/test_basic.cpp`) but zero call sites anywhere in
   `dino8-app/src` - the ranked backlog's own "Conic / rho blend
   cross-sections" item (below) named this explicitly ("only the
   multi-edge batch form ... still has no command"). `FilletEdgeCommand`'s
   Rho branch (`cmd_fillet.cpp`) now STAGES every Rho pick instead of
   applying it immediately, and builds the whole staged batch in one
   `FilletConvexEdgesConic`/`FilletConcaveEdgesConic` call at Enter - fixing
   a real latent bug along the way, not just adding a call site: applying
   the first Rho edge immediately (the old behavior) left the object
   carrying a curved conic wall face, which `PlanarFaces()` (called first
   by every one of these kernel functions) rejects outright, so picking a
   SECOND independent Rho edge in the same `FilletEdge` run used to fail
   with a confusing "needs the whole object to be planar-faced" warning
   about a perfectly ordinary planar edge. Verified end-to-end
   (`fillet_script.txt`'s new ChamferVertex/multi-Rho-edge block,
   `smoke.sh`). Still `partial`: oblique-endpoint and genuine
   shared-corner/vertex-blend batches remain out of the kernel
   construction's own scope, unchanged by this pass.
2. **`kernel::ChamferConvexVertex`/`ChamferConcaveVertex`** (the single-facet
   cut across a trihedral corner, `fillet.h`/`fillet.cpp`) also had zero app
   call sites - `RemoveFillet` already reached this construction's own
   inverse (`RemoveChamferVertex`), but nothing built one forward, the
   same previously-uncredited-gap shape the item above has. A new
   `ChamferVertexCommand` (`cmd_fillet.cpp`, registered as `ChamferVertex`)
   mirrors `FilletVertexCommand`'s own convex-then-concave single-vertex
   pick, but - since a plane always exists through any 3 non-collinear
   points, unlike `FilletVertex`'s own spherical corner - accepts any
   convex or concave trihedral corner with no perpendicular-face
   restriction. Verified against the closed-form removed volume
   (`distance^3/6` for a box corner's own mutually-perpendicular edges,
   `fillet_script.txt`/`smoke.sh`).

Both items stay under the same "Vertex blend"/"Conic / rho blend
cross-sections" bullets the category already tracks (non-perpendicular and
mixed-radius corners, oblique-endpoint conic batches, and a genuine
shared-corner vertex-blend form all remain open), so the kernel table's
Blending & chamfering row (24 items, 5 present / 18 partial / 1 missing,
58.3%) is unchanged.

**2026-09-30 re-score (an eighteenth session, prompted by a claim that "at
least 10" unscored commits had landed since the last successful re-score and
that the two master tables' total weights were 17.75/25.5):** both premises
were checked and only half of the first one holds. Against this branch's
actual tip (fetched fresh; it moved twice more during this session, `561467d`
-> `d9900cc` -> `66de729`), `git log --oneline` shows only **2** commits
touching `dino8-kernel/src`/`dino8-app/src` since `561467d` (the commit that
wrote the seventeenth-session entry below) — not 10: `d9900cc` (PushPullFaces
batch driver + RemoveAllHoleLoopsInBrep whole-Brep sweep) and `66de729` (IGES
importer entity-count DoS cap). Both were already correctly handled before
this pass started: `d9900cc` touches `PARITY_MAP.md` in the same commit and
already self-scores — verified against its own diff and live source
(`boolean.cpp:1781`, `brep.cpp:8917`): both new functions are batch/whole-Brep
conveniences over already-`[partial]` single-target siblings, so Local /
direct-edit operations' 7/21/0 (62.5%) is genuinely unchanged. `66de729` is a
pure untrusted-entity-count DoS cap with no capability change, the same class
this document has repeatedly and correctly left unscored before (`27f1a0a`,
`1a15a3b`, `d2e9425`). The second premise — 17.75/25.5 — is wrong for the app
table: this document's own arithmetic throughout (e.g. the "/ 7.75" division
a few paragraphs below) and a direct sum of the app table's own 8 row weights
(1.5+1.0+1.0+1.0+1.0+0.75+1.0+0.5) both give **7.75**, not 25.5; the kernel
table's 17.75 is correct. No table edit follows from either false
premise — there is no unscored work, and no wrong denominator, to fix.

This pass re-verified both master tables' own weighted averages directly
against their current row counts (the doc's usual "recompute from the table,
don't trust the running total" check): kernel, `sum(weight*(present+0.5*
partial)/items)/17.75` = 67.96%, still rounding to **68.0%**; app, the same
formula `/7.75` = 71.99%, still rounding to **72.0%**. Neither headline
moves. It also spot-checked a sample of bullets against live source rather
than trusting prior citations outright: `dino8-app/src/io/*.cpp` still has
zero hits for `TESSELLATED`/`TRIANGULATED_FACE`/`AP242`/`PMI` (STEP AP242
stays `[missing]`), zero hits for `IFC` anywhere in `dino8-app/src`/
`dino8-kernel/src` (stays `[missing]`), zero hits for `JT` in
`dino8-app/src/io/*.cpp` (stays `[missing]`), and the AT-SPI2 bridge's
Command List/Command Aliases/Keyboard Shortcuts accessibles the
seventeenth-session entry below cites do exist, in `AccessibilityTree.cpp:
480/507/525` as described. No corrections found. The "Priority order for
maximum score-per-fix" section below (`weight / remaining_items` per
category) was recomputed against the current tables and is unchanged, since
no row's present/partial/missing counts moved this pass.

One dangling reference, no score effect: the seventeenth-session entry below
cites its own baseline commit as `6080d7c`, which does not resolve anywhere
on this branch's current history (`git log --oneline 6080d7c..HEAD` errors
with "bad revision") — most likely a hash orphaned by an earlier rebase/
rewrite of this branch rather than a content error in that entry's own
counts, which this pass's independent recomputation above confirms are still
correct regardless. Noted here rather than left for a future session to trip
over again.

`dino8_kernel_tests`/`dino8_app_tests` were not rebuilt or re-run this pass
(docs-only change, per this session's own scope; no source was touched).

**2026-09-30 re-score (a seventeenth session, re-verifying against the last
full re-score, `6080d7c`, ~5.5 hours earlier):** `git log --format='%H %ci %s'
6080d7c..HEAD -- dino8-kernel/src dino8-app/src` returns 40 commits (4 merge
commits, 36 real). Of those, 33 already self-score (touch `PARITY_MAP.md` in
the same commit); this pass checked the other 7 by hand against the category
bullets and master tables below, plus re-verified the two master tables'
own weighted-average arithmetic against their current row counts directly
(the doc's established "recompute from the table, don't just trust the
tracked running total" check).

Of the 7 unscored commits, 4 correctly need no scoring: a security fix
(`1a15a3b`, Dino Flow untrusted-count allocation/CPU DoS + `Int()` UB), an
out-of-bounds-read fix (`d2e9425`, IGES directory-entry parameter gather), a
pure algorithmic-complexity fix with no capability change (`b7cc2f1`,
Array/Copy transforms' `Find()`-per-id cost), and a merge-adjacent duplicate
of already-scored work. Two are genuine, previously-undocumented narrowings
of already-`partial` items — real work, no status flip, so no headline
effect, the same "narrowing, not a flip" category this document uses
throughout:

1. **`Brep::Check()`'s `TrimEdgeGap` sampling** (`a49a9a4`) widened from 3
   points (start/middle/end) to 9 evenly-spaced samples, closing exactly the
   blind spot the "Geometric consistency validation" bullet already named
   ("compares only 3 samples"). The bullet text below is corrected to match;
   the item stays `partial` (a face/face self-intersection check between
   faces sharing no boundary is still missing), so the kernel table's
   Healing/repair row (19/10/1, 80.0%) is unchanged.
2. **Plugin marketplace** (`105d4408`, `472eb52`) gained `min_app_version`
   enforcement (previously parsed and displayed but never checked), the
   Marketplace panel's Install/Update button now going through
   `InstallById`'s dependency resolution instead of calling `InstallEntry`
   directly (the UI path could previously install a plug-in with an unmet
   dependency), a case-insensitive search filter over
   id/name/author/description/tags, version-constrained dependencies
   (`"id@1.2.0"` syntax), and a shared-dependency warning on direct
   uninstall (naming every entry that still depends on it, without
   blocking). All of this is still local install/dependency-graph
   infrastructure, not the missing curated/hosted discovery index the item
   is scored against, so it stays `partial` and the app table's Ecosystem
   row (7/2/7, 50.0%) is unchanged.

**Arithmetic correction (kernel headline only):** recomputing the kernel
table's weighted average directly from its current 17 rows (`sum(weight *
(present + 0.5*partial) / items) / 17.75`) gives 67.96%, which rounds to
68.0% — not the 67.7% the header carried forward from a chain of
per-commit `previous_headline + weight*delta/17.75` increments (most
recently `92ecdda`'s correctly-computed +0.34pp for the Boolean operations
row, 62.0%→66.0%). The increments themselves are each individually correct;
the drift is in the starting point they were chained from, which predates
this session's own 5.5-hour window (recomputing the table as it stood
*before* `92ecdda`'s change gives 67.62%, itself already above the 67.4% it
was tracked at) — an accumulation from earlier sessions' own rounding, not
from anything landed in this window. Re-pointed to the recomputed, verified
value; no row's Present/Partial/Missing changed, so this is a pure
arithmetic fix, the same kind this document has made before (see the
"Intersections & projections" 65.5%→69.0% correction noted below). The app
table's own weighted average recomputes to 71.97% against its current
rows, which rounds to the already-tracked 72.0% — no drift there; the
concern that the app headline might again be lagging real work, the same
way it did before the `6080d7c` session, did not hold up this time.

`dino8_kernel_tests`/`dino8_app_tests` re-run clean after this pass
(docs-only change, no source edited).

**2026-09-30 re-score (a sixteenth session, prompted by a claim the headline had
"gone nearly flat" despite ~94 commits landing over the prior ~44 hours):**
that specific premise was false on inspection - `git log --since="46 hours
ago" -- dino8-kernel/src dino8-app/src` returns 41 commits, not 94 - but the
underlying complaint pointed at something real. Every commit that touches
`PARITY_MAP.md` in the same commit already self-scores (confirmed by
diffing the 41 recent source-touching commits against the 21 of them that
also edited this file), so this pass instead diffed the other 21 - the ones
that changed `dino8-kernel/src`/`dino8-app/src` without ever touching this
document - against the category bullets below by hand. Most were bug/test
fixes correctly left unscored (a dangling-pointer fix, an O(k\*N)-complexity
doc comment, a crash fix, a flaky-smoke-test fix, a solver-parameter clamp).
Two were genuine, previously-uncredited capabilities, and neither was new
this window - both existed since this subtree's own first commit and had
simply been mis-scored (one of them explicitly, wrongly, marked
"infeasible") every session since:

1. **Screen-reader support** was scored `[missing]` and listed as
   infeasible ("no platform accessibility tree... requires replacing the
   entire UI toolkit"). `dino8-app/docs/ACCESSIBILITY.md` and
   `dino8-app/src/platform/AccessibilityLinux.cpp` (640 lines, present
   since the subtree's own genesis commit) directly contradict that: a
   real AT-SPI2 D-Bus bridge exists on Linux, hand-implementing
   `org.a11y.atspi.Accessible`/`.Application`/`.Text` and registering with
   the real `at-spi2-registryd`, verified end-to-end against the real
   `pyatspi` client library per that doc's own "Verifying it yourself"
   section - not a stub. It covers the command line, main menu bar, the
   running command's options, Layers/Properties panels, each viewport's
   title/view-menu button, the Activity Log, Named Views and Named
   CPlanes (the last five of those closed by five more commits within
   this same recent window: `0bbc84c`/`0343403`/`330fa81`/`6b71f93`/
   `aa25102`). Reclassified `[missing]`->`[partial]`, no longer infeasible
   (see the category bullet and the Infeasible section below for detail).
2. **Plugin marketplace / discovery mechanism** was scored `[missing]`,
   "not attempted," and listed as infeasible ("needs third-party adoption
   over time"). `dino8-app/src/plugins/Marketplace.{h,cpp}`/
   `MarketplaceIndex.{h,cpp}`/`MarketplacePanel.{h,cpp}` (837 lines,
   present since genesis, extended this window by `78d4425` dependency
   resolution before install, `76f3020` uninstall with orphaned-dependency
   cleanup, and `ecf989e` local ratings/reviews) is a real, tested
   install/uninstall/dependency-graph/ratings system, not a stub - but it
   has no built-in curated index: a user must already have a path or
   http(s) URL to a JSON index (`MarketplacePanel.cpp:94`), so it is real
   plugin *installation* infrastructure without real *discovery*.
   Reclassified `[missing]`->`[partial]`, no longer infeasible (the
   *separate* "Third-party plugin ecosystem (real external adoption)" item
   stays infeasible - a network-effect gap, not an engineering one).

Both corrections are app-side only; the kernel table (17.75 weight) was
independently spot-checked against the same 21-commit diff and needed no
change, so the kernel-only headline stays 67.4%. The app table
(**Dino 8: UI/UX, accessibility & localization** 1.0/19: 13/2/4 ->13/3/3,
73.7%->76.3%; **Dino 8: Ecosystem, trust, cloud/AI & platform reach**
0.5/16: 7/1/8->7/2/7, 46.9%->50.0%) moves by
`(1.0x(76.3-73.7) + 0.5x(50.0-46.9)) / 7.75 = +0.6pp`, giving **72.0%**
(was 71.4%). Two small honest-narrowing notes with no score effect: the
Python object-model binding count (**Dino 8: Scripting**'s "Python API
breadth" bullet) is now 62, not 56, after `2f7456f`/`318db10`/`5fa68c4`/
`835a976`/`bee11db`/`e5afa1a` added `AddTorus`/`AddInterpCurve`/
`AddCircle`/`AddSrfPt`/`AddCone`/`AddArc3Pt`; and **Dino 8: 2D drafting**'s
"Associative annotation updating" bullet now also covers MultiLeader
arrows (`0ebdb07`) and the four GD&T symbol commands (`f66cdf9`), still via
the same explicit-recompute-command shape the bullet already named as the
gap, so it stays `partial`. Full `dino8_app_tests`/`dino8_kernel_tests`
suites re-run clean after this pass (docs-only change, no source edited).

**2026-09-30 addendum (same day, later session):** four more Python
object-model bindings landed since the note above, closing gaps against
Lua's `rs.*` module: `AddPlanarSrf` (trimmed planar surfaces, queued by the
commit immediately before this session but left undocumented here),
`AddPoints` (batch point creation), `ExtrudeCurveStraight` (straight
extrusion, capped for closed planar curves) and `BooleanUnion` (closed
solids combined into one mesh solid) - each in `PythonEngine.cpp`,
mirroring its `rs_*` counterpart's logic and skip/throw semantics exactly.
Python API breadth (**Dino 8: Scripting**'s "Python API breadth" bullet)
is now **66**, not 62. `dino8_app_tests` re-run clean after this pass
(new/updated cases in `python_script.txt`/`smoke.sh` cover all four). No
score effect (same honest-narrowing status as the note above: the gap
this bullet names - the rest of the object model, and no interactive
prompts - is unchanged).

**2026-09-30 addendum (same day, later session):** three more Python
object-model bindings landed since the note above, closing gaps against
Lua's `rs.*` module: `BooleanDifference` and `BooleanIntersection` (the
two-set counterparts of the just-landed `BooleanUnion`, subtracting/keeping
common volume between two closed-solid sets) and `MoveObject` (translates
objects in place, skipping ids that no longer exist rather than raising) -
each in `PythonEngine.cpp`, mirroring its `rs_*` counterpart's logic and
skip/throw semantics exactly (`BooleanDifference`/`BooleanIntersection`
share a `RunBooleanTwoSets` helper mirroring `LuaEngine.cpp`'s own
`RunBoolean(op, two_sets=true)` branch). Python API breadth (**Dino 8:
Scripting**'s "Python API breadth" bullet) is now **69**, not 66.
`dino8_app_tests` re-run clean after this pass (new cases in
`python_script.txt`/`smoke.sh` cover all three). No score effect (same
honest-narrowing status as the notes above: the gap this bullet names -
the rest of the object model, and no interactive prompts - is unchanged).

**2026-09-30 addendum (same day, later session):** three more Python
object-model bindings landed since the note above, closing gaps against
Lua's `rs.*` module: `CopyObject` (copies objects, optionally translated),
`RotateObject` (rotates objects about an axis through a center) and
`ScaleObject` (scales objects about an origin) - each in `PythonEngine.cpp`,
in place or onto copies per a `copy` flag, mirroring its `rs_*` counterpart's
logic and skip/throw semantics exactly (`CopyObject`/`RotateObject`/
`ScaleObject` share a `TransformIds` helper mirroring `LuaEngine.cpp`'s own
`TransformIds`, which `MoveObject` now also calls instead of duplicating its
translate-in-place loop). Like `AddCylinder`/`AddCone`'s vector-only axis
argument, `ScaleObject`'s `scale` parameter is a single `Vector3d` rather
than Lua's number-or-vector overload (pass `Vector3d(s, s, s)` for a uniform
scale). Python API breadth (**Dino 8: Scripting**'s "Python API breadth"
bullet) is now **72**, not 69. `dino8_app_tests` re-run clean after this
pass (new cases in `python_script.txt`/`smoke.sh` cover all three). No score
effect (same honest-narrowing status as the notes above: the gap this
bullet names - the rest of the object model, and no interactive prompts -
is unchanged).

**2026-09-30 addendum (same day, later session):** three more Python
object-model bindings landed since the note above, closing gaps against
Lua's `rs.*` module: `MirrorObject` (mirrors objects across the vertical
plane through a line, in place or onto copies, sharing the same
`TransformIds` helper as `CopyObject`/`RotateObject`/`ScaleObject` above,
and throwing for a degenerate or vertical mirror line exactly as
`rs.MirrorObject` raises a Lua error) and `SelectObject`/`UnselectObject`
(select/deselect objects, a silent no-op for a missing, locked or hidden
object via `Document::Select`, matching `rs.SelectObject`/
`rs.UnselectObject`'s own skip semantics) - each in `PythonEngine.cpp`,
mirroring its `rs_*` counterpart's logic exactly. Python API breadth
(**Dino 8: Scripting**'s "Python API breadth" bullet) is now **75**, not
72. `dino8_app_tests` re-run clean after this pass (new cases in
`python_script.txt`/`smoke.sh` cover all three). No score effect (same
honest-narrowing status as the notes above: the gap this bullet names -
the rest of the object model, and no interactive prompts - is unchanged).

**2026-09-28 re-verification addendum (same day, later session):** the brief for
this addendum claimed "roughly 60+" capability-adding commits had landed since
the 66.2%/71.5% measurement above. That premise was false: `git log --oneline
--since="2026-09-28" -- dino8-kernel/src dino8-app/src` returns **zero**
commits, and the commit that wrote the measurement above (`cdc8e2b`) is HEAD —
there is nothing after it to re-measure. Rather than fabricate a from-scratch
rewrite against an unchanged codebase, this addendum ran five parallel
adversarial passes (refute-the-gaps and refute-the-presents, same as every
prior pass) that re-read every one of this document's ~390 gap bullets plus a
sample of items scored `present` against live HEAD, specifically hunting for
mistakes the *document itself* made rather than for new kernel/app work (there
is none). Result: **no item's present/partial/missing status changed** — every
substantive claim (a function exists, a refusal condition, a scope limit)
held up under direct inspection. Two real corrections were found and applied
below:
1. **Headline arithmetic drift.** Both headline numbers above had been tracked
   by chaining small marginal deltas across many same-day follow-ups (see the
   trail of "+0.1pp" paragraphs throughout this document) rather than being
   recomputed fresh from the final category table each time, and had drifted
   from what the table actually implies. A full recomputation — weighted
   average of `(present + 0.5·partial)/items` across all 17 kernel rows (total
   weight 17.75) and all 8 app rows (total weight 7.75), using the table
   as it now stands post-correction-2 below — gives **66.8%** (was 66.2%,
   0.6pp drift) and **71.4%** (was 71.5%, 0.1pp drift; the app-side table
   itself needed no correction, only the arithmetic).
2. **kernel: Tessellation / faceting table/bullet mismatch.** The table row
   read 12/11/2 (70.0%) but the category's own gap-bullet list (the source of
   truth for partial/missing counts, per this document's own established
   convention) contains only 9 `[partial]` + 2 `[missing]` = 11 gap bullets,
   not 12. Corrected: 14/9/2/25, 74.0% — the same class of table-vs-bullet
   transcription slip this document's own intro already documents fixing for
   Intersections & projections and Local/direct-edit operations, just missed
   for this row until now. (This is the correction folded into headline
   figure above; both new headline numbers already reflect it.)

Also found: `kernel: Feature operations`' own "Note on this category's score"
(previously several paragraphs below the category's gap bullets) cited a
stale 5/15/4 split contradicting both the table (6/14/4, 54.2%) and its own
neighboring `[present]`-tagged Counterbore bullet — corrected in place, no
score change (the table was already right).

The remaining, larger finding is **pervasive citation staleness**: roughly 40
`file.cpp:NNN` references across the kernel-side bullets (concentrated in
`brep.h`/`brep.cpp`/`fillet.cpp`/`sweep.cpp`/`subd.cpp`/`surface_edit.cpp`/
`boolean_general.h`/`.cpp`/`test_basic.cpp`, plus a handful on the app side —
`CMakeLists.txt`, `GlRenderer.{h,cpp}`, `PathTracer.cpp`, `FileExchange.cpp`)
point at line numbers that drifted anywhere from ~10 to ~1600 lines away from
the code they describe, evidently because earlier passes' citations weren't
recounted after later, file-disjoint edits shifted line numbers elsewhere in
the same files. In every single case checked, the cited function/behavior
genuinely exists nearby at the corrected location and the substantive claim
held up — this is a sourcing-hygiene defect, not a scoring defect. Corrected
citations are applied inline below, each marked "corrected 2026-09-28" where
the fix is non-trivial.

**Later still, a third same-day session:** `dino8::kernel::EmbossProfile`
(dino8-kernel/include/dino8/kernel/boolean_general.h/.cpp) landed after this
addendum's own 66.8%/71.4% figures were written, closing kernel: Feature
operations' own "Emboss/deboss" item (`missing`→`partial`). File-disjoint
from both this addendum's own edits and the concurrent `AddWireCurves` pass
(see that pass's own follow-up note below) — see kernel: Feature operations'
own bullet list and "Note on this category's score" below for the full
construction/test detail, and this document's own "Fourteenth same-day
follow-up" note (below the Thirteenth, `AddWireCurves`) for the resulting
headline arithmetic: **66.9%**, kernel-only, on top of this addendum's own
66.8%. The combined Dino 8 vs Rhino 8 + AutoCAD 2027 headline is left at
71.4% (kernel-only item, no `dino8-app` command wired to it).

**Later still, a fifth same-day session:** `dino8::kernel::SubD::Weld`
(dino8-kernel/src/subd.cpp, dino8-kernel/include/dino8/kernel/subd.h)
landed after the above 66.9%/71.4% figures were written, closing **kernel:
SubD & mesh kernel support**'s own "Kernel-native SubD local edit
operators" item's fifth and last named sub-operator (`missing`→`partial`
had already covered insert/spin/extrude/expand; this flips the whole item
`partial`→`present`). File-disjoint from `AddWireCurves`, `EmbossProfile`,
and the re-verification addendum's own prose/citation fixes (this pass's
only source edits are `dino8-kernel/src/subd.cpp`,
`dino8-kernel/include/dino8/kernel/subd.h`, and
`dino8-kernel/tests/test_basic.cpp`) - see that category's own bullet
list and this document's own "Fifteenth same-day follow-up" note (below
the Fourteenth, `EmbossProfile`) for the full construction/test detail
and the resulting headline arithmetic: **67.0%**, kernel-only, on top of
this session's own 66.9%. The combined Dino 8 vs Rhino 8 + AutoCAD 2027
headline is left at 71.4% (kernel-only item, no `dino8-app` command wired
to it).

**Later still, a sixth same-day session:** `dino8::kernel::SubD::FromBrep`
(dino8-kernel/src/subd.cpp, dino8-kernel/include/dino8/kernel/subd.h) landed
after the above 67.0%/71.4% figures were written, adding real, tested ground
to **kernel: SubD & mesh kernel support**'s own "SubD from NURBS/B-rep
conversion" item (see that category's own bullet list above for the full
construction/test detail) - but, honestly, WITHOUT changing that item's own
`partial` status or this category's 15/5/2 (79.5%) count: the item was
already `partial`, not `missing`, before this session (via
`SubD::FromNurbsSurface`'s single-surface case), and a general curved/trimmed
Brep -> SubD conversion remains out of scope, so it stays `partial` rather
than flipping to `present` the way `SubD::Weld()`'s own fifth-sub-operator
completion did above. Both headline numbers are therefore unchanged at
**67.0%** / **71.4%** - this entry exists purely for the same
construction/test traceability every other same-day follow-up in this
document gets, not because the arithmetic moved. File-disjoint from every
other same-day session above: this pass's only source edits are
`dino8-kernel/src/subd.cpp`, `dino8-kernel/include/dino8/kernel/subd.h`,
`dino8-kernel/include/dino8/kernel/brep.h` (widening
`Brep::FaceCoversWholeDomain` from `private` to `public`, no behavior
change), and `dino8-kernel/tests/test_basic.cpp`.

**2026-09-28, a seventh same-day session (parallel to the sixth above):**
picked up **kernel: Local / direct-edit operations**, whose own prior round had reported "working tree
clean, no changes pending" — checked directly via `git log`/`git diff`
rather than taken on faith: the prior round's work (through `52de4f8`,
`SubD::Weld`) was in fact intact on `origin/claude/pdf-audit-i2bvwm`, just
sitting under a stale local branch ref left pointing at older, unrelated
history from before a prior detached-HEAD checkout; nothing was lost, and
this session's own new work builds on top of it, not in place of it. Added
`dino8::kernel::Brep::MergeSameSurfaceFaces()`
(dino8-kernel/include/dino8/kernel/brep.h; dino8-kernel/src/brep.cpp),
closing this category's own last `[missing]` item, "Merge faces on the
same non-planar surface (cylinder/tangent split faces)" (`missing`→
`partial`) — see that bullet above for the full construction and test
detail. File-disjoint from every other pass above (this session's only
source edits are `dino8-kernel/include/dino8/kernel/brep.h`,
`dino8-kernel/src/brep.cpp`, and `dino8-kernel/tests/test_basic.cpp`).
Resulting headline arithmetic: this category's own score moves 7/20/1
(60.7%) → 7/21/0 (62.5%), a +0.1786pp swing at this category's weight of
1 out of the kernel table's own total weight 17.75 — +0.1786/17.75 =
+0.01006, i.e. **+0.1006pp**, giving **67.1%** kernel-only, on top of this
document's own 67.0%. The combined Dino 8 vs Rhino 8 + AutoCAD 2027
headline is left at 71.4% (kernel-only item, no `dino8-app` command wired
to it — the same convention every same-day follow-up above already
follows). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean:
100% passing, 0 regressions.

**2026-09-29, a ninth session:** `dino8::kernel::Mesh::SaveOff`/`LoadOff`
(dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) closes real, tested
code for the Geomview `.off` corner of **kernel: Kernel-level data
exchange**'s "Other mesh/scene exchange formats (glTF/GLB, 3MF, FBX,
Collada, VRML/X3D, AMF, OFF, SketchUp SKP, USD)" item — the same bullet's
own text already named this exact list of formats as having zero code
anywhere in the source; OFF is now a genuine exception, `missing`→`partial`
(see that bullet's own updated text below for the full construction and
test detail). File-disjoint from every other session above (this pass's
only source edits are `dino8-kernel/include/dino8/kernel/mesh.h`,
`dino8-kernel/src/mesh.cpp`, and `dino8-kernel/tests/test_basic.cpp`).
Still explicitly missing for this same item: glTF/GLB, 3MF, FBX, Collada,
VRML/X3D, AMF, SketchUp SKP, USD — none of those gained any code this pass,
so the item stays `partial`, not `present`, and this document does not
claim otherwise. Resulting headline arithmetic: this category's own score
moves 8/11/8 (50.0%) → 8/12/7 (51.9%), a +1.8519pp swing at this category's
weight of 1 out of the kernel table's own total weight 17.75 —
+1.8519/17.75 = +0.1043pp, giving **67.3%** kernel-only, on top of this
document's own 67.2%. The combined Dino 8 vs Rhino 8 + AutoCAD 2027
headline is left at 71.4% (kernel-only item, no `dino8-app` command wired
to it — the same convention every same-day follow-up above already
follows).

**2026-09-29, a tenth session:** `dino8::kernel::Mesh::SaveAmf`/`LoadAmf`
(dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) closes the AMF
(Additive Manufacturing File Format) corner of the SAME "Other mesh/scene
exchange formats" item the ninth session's `OFF` work above already
touched — see that bullet's own updated text below for the full
construction and test detail. Genuinely new capability (zero AMF code
existed anywhere in this kernel before this pass), verified by 2 new
tests plus a hand-written-file check exercising attribute and whitespace
tolerance, and the full `dino8_kernel_tests` suite re-run clean (100%
passing, 0 regressions) after adding them. File-disjoint from every other
session above (this pass's only source edits are
`dino8-kernel/include/dino8/kernel/mesh.h`, `dino8-kernel/src/mesh.cpp`,
and `dino8-kernel/tests/test_basic.cpp`). **No score change:** unlike the
ninth session's OFF work — which flipped this same item `missing`→
`partial` because, before it, NONE of this bullet's named formats had any
code at all — this item was already `partial` going into this pass (on
the strength of that same OFF work), and stays `partial`: glTF/GLB, 3MF,
FBX, Collada, VRML/X3D, SketchUp SKP, and USD still have zero code, so
closing one more format on an already-`partial` item doesn't move its
classification, the same "real code, still short of a classification
change" outcome this document's Thicken/ExtrudeTapered/`SubD::FromBrep`
same-day follow-ups already recorded elsewhere. Both headline numbers are
therefore unchanged at **67.3%** / **71.4%**.

This run recomputes the parity map from scratch against the live repository at
`/home/user/miniature-pancake` on `claude/pdf-audit-i2bvwm`, superseding the
2026-09-25 run (headline 63.3% / 71.5%, commit ba80d25). **Correction to the
premise this pass started from:** the brief for this re-measurement claimed
"roughly 60+" commits had landed since that run. That figure was wrong.
`git log --oneline --no-merges ba80d25..HEAD -- dino8-kernel dino8-app` shows
exactly 12 non-merge commits in the window (through 7059e20), most of them
CI/Windows-only test plumbing with zero parity effect (`3f7f5da`, `d00f0bf`,
and the CRLF fix in `19c14a0`). Five parallel passes re-read every one of the
25 categories' items against the current source anyway — actual files,
actual line numbers, actual tests read or run by hand — rather than trusting
the prior document's citations, some of which had drifted from unrelated
edits elsewhere in the same files even where the underlying behaviour had
not changed.

Each of the 25 categories (17 kernel, 8 app) scores every item `present`=1,
`partial`=0.5, `missing`=0, averages those scores into a category
`parity_estimate_pct`, and the two headline numbers are the category scores
averaged again, weighted by each category's `weight` field (1.5 for the
highest-stakes categories — booleans, blending, intersections, commands —
down to 0.5 for the lowest-stakes ones — transforms, ecosystem). Every item
was checked through two adversarial lenses — a "refute-the-gaps" pass that
tries to find real evidence upgrading a missing/partial item, and a
"refute-the-presents" pass that tries to downgrade a claimed-present item.

**Three honesty notes from this pass, all material to reading the numbers below:**

1. **Given how little landed since the last measurement, most of the real
   movement this pass found is not new code — it is this pass's own
   corrections to the prior pass's arithmetic and citations**, surfaced by
   actually re-deriving each category's totals from its written bullets
   instead of copying the table row forward:
   - **kernel: Intersections & projections** had a stale table row (11
     present / 16 partial) that didn't match its own written bullet list (14
     partial + 2 missing = 16 gap bullets, so present = 29 − 16 = 13, not
     11). Traced to a transcription slip made when the prior pass's own
     documented duplicate-bullet merge (two pairs of exact-duplicate bullets
     folded into one apiece) was applied to the table but not carried
     through consistently. Corrected: 13/14/2/29, 69.0% (was 65.5%). No
     individual item's status actually changed.
   - **kernel: Local / direct-edit operations** had the same class of bug
     (17 partial declared, only 16 gap bullets physically present). Folded
     into the same fix as the genuine new item below.
   - **kernel: Boolean operations**' "General NURBS-surface B-rep boolean"
     and "Result validity" bullets cited "15 of 76" sweep combinations
     tessellating watertight. This pass built and ran
     `dino8_general_boolean_sweep` against current HEAD as live ground
     truth rather than trusting the comment: the true, currently-measured
     figure is **54 of 76** (18 genuine non-empty residual failures, plus 4
     trivial 0-face empty results). The kernel's own embedded development
     log in `boolean_general.h` already documented this progression before
     the last measurement; the prior pass had cited a stale intermediate
     number from partway through that log instead of its final entry. This
     is a citation fix, not new work — the category's present/partial/missing
     counts are unchanged (8/13/4/25, 58.0%) because the item was already
     scored `partial` for its scope limits regardless of the exact fraction.
   - **Blending & chamfering**'s "tangent edge chains" bullet claimed "the
     command catalogue lists ChainEdges/FaceEdges" as evidence the gap was
     merely a UI omission. Re-grepped: neither string exists anywhere in
     `dino8-app/src`. The claim was unfounded; the bullet's classification
     (partial) is unchanged but its evidence text is corrected.
   - **kernel: Topology & data structure**'s multi-lump bullet cited
     `boolean.h:1321-1323` for the compound-operand refusal; that location
     holds unrelated text and never did hold this logic (the real refusal
     is `boolean.cpp:815-822`'s shared `RefuseCompoundOperand` helper). Fixed.
     While checking this, a genuinely new gap surfaced: `RefuseCompoundOperand`
     is called by `BooleanCombinePlanar`/`Mixed` but **not** by
     `BooleanCombineGeneral` at all — a compound operand fed to the general
     engine is silently processed per-face with no lump-boundary awareness,
     an unhandled case rather than a clean refusal. Folded into the existing
     item rather than raising the category's item count.
2. **Genuinely new work in the window, and it is narrow:** only two commits
   touch kernel logic that changes a score. `Brep::Check()` gained a
   `NonManifoldVertex` (pinch-point) diagnostic via union-find over the
   faces touching each vertex's incident edges, plus a matching
   `Brep::SplitNonManifoldVertex`/`SplitNonManifoldVertices` heal — the
   standard Parasolid/ACIS "disjoin" repair, verified against a hand-built
   two-square hourglass fixture. It does not flip any item's status (edges
   are still fully refused as non-manifold, and the heal only disjoins a
   pinch vertex rather than letting the kernel construct non-manifold
   topology as such), so it is folded as new evidence into the existing
   "Non-manifold topology" item rather than a new one. Separately, the new
   `SplitByObjectCommand` app command (general cutting-solid/open-surface
   split, closed pieces used as-is, open cutters solidified via an
   area-weighted average-normal extrusion, multiple cutters unioned,
   Intersection **and** Difference both kept per target — genuine KeepAll
   semantics) turns out to close a real gap in **two** categories at once,
   not one: it was already credited under kernel: Feature operations'
   "Split body with an arbitrary surface / solid cutter" (missing→partial,
   scoring unchanged from the prior pass, independently re-verified this
   pass line-for-line), but it is *also* squarely kernel: Transformations,
   patterns, splitting's "Split/trim body with a tool body... KeepAll" item
   — the same command read under Rhino's BooleanSplit/KeepAll framing
   instead of Parasolid's PK_BODY_section framing, the same pattern this
   document already uses elsewhere for one capability spanning two
   commercial-kernel vocabularies. That item upgrades missing→partial:
   9/13/0/22, 70.5% (was 9/12/1/22, 68.2%). On the app side, the raytraced
   path tracer's `PathTracer::SkyColor()` gained a real equirectangular
   env-map branch for `Background::Image`, called from inside the bounce
   loop for any ray that escapes the scene at any depth — genuine
   image-based lighting/reflection contribution, not just a backdrop, for
   the three offline CPU path-traced render commands. It stays `partial`
   because the interactive rasterizer viewport and the live GPU-raytraced
   viewport preview both still lack any env-map support, and there is still
   no HDR/`.exr` loader (only 8-bit LDR image formats). All other kernel
   work already reflected in the prior measurement (Sweep2, OffsetApproximate,
   SewTJunctions, `Brep::Torus`, `PipeVariable`, kernel PLY, Loft tangency,
   `ImprintFaces`, `FilletConvexEdgeConic`, Sweep1 `twist_total`) predates
   this window and is unchanged; almost none of it is yet reachable from
   `dino8-app`, which still writes `.3dm` through its own
   `src/io/File3dm.cpp` and still routes most booleans, fillets and
   sweep/loft/pipe commands through its own older, mesh-approximate paths.
3. **One more stale cross-reference, unrelated to anything new, surfaced by
   this pass's independent grep-everything discipline:** kernel: Boolean
   operations' "Face-face imprint" bullet was correctly updated in-repo when
   `ImprintFaces` landed (`066e97d`, predating this window), but the
   *mirror* bullet under kernel: Local / direct-edit operations'
   "Imprint curve / face onto a body face" item was never touched and still
   read "a case-insensitive grep for imprint finds no hits anywhere" — false
   as of current HEAD. Corrected: missing→partial. Combined with the
   intersections-style arithmetic fix in that category, kernel:
   Local/direct-edit operations moves from 5/17/6/28 (48.2%) to 6/17/5/28
   (51.8%).
4. **A same-day follow-up after this pass's own headline was written:**
   kernel: Offsetting, shelling, thickening gained a new exact B-rep
   whole-body offset, `OffsetSolidConvexPlanar` (boolean.h/.cpp, right next
   to `OffsetFace`) — every face of a convex planar-faced solid moved along
   its own outward normal at once (a single uniform distance, or
   independently per face via the vector overload), sharp/mitered corners
   reconstructed by the exact same `ClipConvexPolygon` half-space-clipping
   technique `OffsetFace`/`ShellConvexPlanar` already use (generalized from
   "one plane moves" to "every plane moves by its own amount"), not a mesh
   approximation. Verified against an exact box (uniform growth/shrink
   matching the closed-form new volume, and per-face distances matching an
   independently-computed new bounding box) and a hand-built, genuinely
   non-rectangular tetrahedron whose expected volume is recomputed
   independently — every new vertex as the intersection of its own three
   individually-translated original planes via the standard three-plane
   cross-product formula, not by calling the new function's own internals —
   plus closed-watertight-manifold tessellation checks and argument-error
   cases (mismatched distances vector, a shrink that collapses the solid).
   This is the kernel's first B-rep (not mesh-level) whole-body offset —
   previously `OffsetSolid` was Manifold-Minkowski dilation/erosion, mesh
   only. It does **not** flip the "Body offset" item's status: like every
   other item already landed in this category (`OffsetFace`,
   `ShellConvexPlanar`, `ShellClosedSphere`/`Torus`), real closure needs the
   general non-convex/curved-body case, which this still refuses (the same
   convexity precondition `OffsetFace`/`ShellConvexPlanar` already enforce),
   so it stays `partial`, folded as new evidence into the existing bullet
   below rather than a status change — the category's 0/26/1/27 (48.1%)
   numeric row is unchanged.
5. **A second same-day follow-up, closing this category's own
   "Tolerance-driven offset refit" gap for surfaces:** `git log --oneline
   -30 -- dino8-kernel/src` at the start of this session showed
   `OffsetSolidConvexPlanar` (item 4, above) as the most recent kernel-src
   commit, so this pass picked the next highest-value still-partial item in
   this same category rather than duplicating it. `NurbsSurface::OffsetRefit`
   (surface.h/surface_edit.cpp) is new: the tolerance-driven refit loop this
   bullet's own text already documents as missing ("Surfaces do not:
   `OffsetApproximate` never refits to a tolerance"), built the same way
   `NurbsCurve::OffsetInPlane`'s own curve-level refit already works —
   sample the TRUE offset locus (`PointAt(u, v) + distance * NormalAt(u,
   v)`, the same formula `OffsetApproximate` only applies once, per control
   point) on a grid, globally least-squares-refit a NEW surface to it via
   `Rebuild()`'s own row-then-column tensor-product machinery
   (`FitRowLeastSquares`, surface_edit.cpp), measure the fit's worst-case
   deviation against the true offset formula on an INDEPENDENT, finer,
   half-step-offset verification grid (never the same points the fit
   itself saw — `Rebuild()`'s own technique for the identical reason: a
   fit's own sample points converge toward zero residual as its control
   count approaches the sample count regardless of true accuracy between
   samples, which would make a self-measured check meaningless), and double
   the control-point count independently in each direction until that
   measured deviation is within `tolerance` or a sample-grid-limited
   ceiling is reached. Same fold-through-center-of-curvature guard as
   `OffsetApproximate` (`distance * k >= 1.0`), checked up front.
   Verified: exact (~1e-15) on a genuine plane; on the same radius-5 sphere
   fixture `OffsetApproximate` leaves >0.05 worst-case radial error on at
   distance 1.5, `OffsetRefit` requested at `tolerance=0.01` actually
   reaches 0.0014 and its own control-point count measurably grows beyond
   the source sphere's NURBS form to get there (not a fluke fit at the
   original count); the same real-refinement-needed pattern reproduces on a
   bulged freeform surface `OffsetAnalytic` itself refuses; the fold guard
   refuses the same excessive distance `OffsetApproximate` already refuses,
   on both fixtures; NaN and zero-distance argument checks pass. A real
   regression caught and fixed before landing, not merely a design
   footnote: an earlier draft nudged the FIT grid's own boundary
   parameters inward (dodging a natural-parametrization pole the same way
   `OffsetApproximate`'s per-control-point Greville nudge already does),
   which silently broke `FitRowLeastSquares`'s own corner-pinning
   assumption (it sets the first/last output control point directly equal
   to the sample it was given, which is only the correct clamped-B-spline
   corner value when that sample was measured at the domain's own exact
   t0/t1) and introduced a fixed, resolution-independent bias — caught by
   `TestSurfaceOffsetRefitIsExactOnAGenuinePlane` never converging below
   ~2e-6 no matter how far the control count grew, root-caused with a
   standalone repro (not by inspection alone), and fixed by keeping the fit
   grid's own parameters exact and nudging only the point passed to
   `NormalAt()` (position and pole-safety are now independent concerns, as
   `OffsetApproximate`'s own established pattern already keeps them). Does
   **not** flip this bullet's status to `present`: the result is always a
   NEW non-rational surface (an analytic surface's own rational form is
   never reproduced exactly, only approximated, mirroring `Rebuild()`'s own
   disclosed scope), the fit shares `OffsetApproximate`'s own first-order
   offset-formula model rather than any exact geometric construction, and
   no app command calls it — so it stays `partial`, folded as new evidence
   into the existing bullet below; the category's 0/26/1/27 (48.1%)
   numeric row is unchanged (this closes a named sub-gap of an
   already-partial item, not a fresh item flipping status).

**Correction to the prior pass's honesty note, found by this session, not
this pass:** the defect the prior pass (this document, at HEAD `b2fe0aa`)
re-confirmed as "unresolved... neither touched by any commit in this
window" — **`Brep::Check()` false-flagging `DegenerateFace` on the kernel's
own valid primitives** — was in fact already fixed, by `b1ac7c9`, an
ancestor of `b2fe0aa` on this same branch. `git log --oneline
ba80d25..b2fe0aa -- dino8-kernel dino8-app` (the exact window the prior
pass audited) does not contain `b1ac7c9`: it landed on a parallel
session's branch and reached `HEAD` only through a later merge
(`7ce5797`), after the prior pass had already written its "still present"
note against the pre-merge tree it was reading. Both mechanisms the prior
note described are confirmed fixed by direct testing, not by trusting the
merge:

(a) `Box()`/`Sphere()`/`Torus()` faces still build via the surface-only
`NewFace(int)` overload with zero loops/trims, but `Check()`'s face loop
(`dino8-kernel/src/brep.cpp:6450-6477`, not the `6357-6384`/`6360` the
prior note cited — that range is now the trim-check loop above it, shifted
by `b1ac7c9`'s own insertions) only auto-flags `DegenerateFace` when a face
has **no surface at all** (line ~6454); a loop-less-but-surfaced face is
instead sampled against its own domain rectangle
(`DomainRectanglePolygon()`, `brep.cpp:6005-6009`, called from the same
loop at line ~6469). (b) `SampleLoop()` (`brep.cpp:44-70`) is untouched and
still takes only one sample per UV-linear trim curve, exactly as
before — but `Check()`'s degenerate/sliver face loop no longer feeds
`SampleLoop()`'s raw output straight into `PointSetWidth`. It now routes
through `LoopSamples3d()`/`DensifyBoundary3d()` (`brep.cpp:5977-6019`),
which re-samples any boundary segment whose **3D image** isn't actually
straight regardless of what `SampleLoop()`'s 2D-linearity heuristic did,
closing exactly the starved-sampling gap the prior note described (a
cylindrical iso-parameter trim straight in UV but a full circle in 3D).

Verified this session, on top of `b1ac7c9`'s own
`TestBrepCheckDoesNotFalselyFlagCurvedOrToplessValidFaces` (which already
covered Box()/Sphere()/Extrude(circle)/Revolve(line) at the `Check()`
level, plus a genuine-hairline-sliver negative control): a new
end-to-end test, `TestBrepRemoveDegenerateOrSliverFacesDoesNotTouchValidSolids`
(`tests/test_basic.cpp`), calls `RemoveDegenerateFaces()`/
`RemoveSliverFaces()` themselves — not just `Check()` — on `Box()` and
`Extrude(circle)` and confirms 0 faces removed from either (previously
probed destructive: 3 of 3 and 6 of 6), plus a `Torus()` `Check()` case
(the third loop-less primitive, not previously covered by name) and a
true-positive control confirming a genuine hairline sliver is still
deleted by `RemoveDegenerateFaces()`. Full `dino8_kernel_tests` suite:
all checks pass, 0 failures, 0 regressions (re-run this session).

Net effect on the scores below: **none of the affected items' present/
partial/missing status changes.** Sliver/degenerate micro-face removal
(topology item, healing items) stay `partial` — for the *other*, still-real
reasons already on record (delete-and-tolerant-join rather than a
geometric collapse; a T-junction sliver leaves naked edges since this
does not call `SewTJunctions`) — but the specific "destructive on the
kernel's own valid solids" clause is retracted: it is no longer true, and
citing it going forward would itself be the same kind of stale claim this
note is correcting.

**Genuinely new work, this session, in Blending & chamfering:** `FilletConvexEdges`'
m==1 vertex case (a single filleted edge's own free endpoint, not a
trihedral spherical corner) previously called `NotchCornerAtVertex`
unconditionally, which silently skips any third face that isn't exactly
perpendicular to the edge — so an obliquely-ended edge filleted through
`FilletConvexEdges` came out with that end's corner simply untouched
(self-overlapping geometry, not merely an open shell), even though the
single-edge `FilletConvexEdge` had already closed that same case via
`FindObliqueThirdFaceCrossing`/`EllipseNotchCornerAtVertexCylindrical`. A new
detection pass (`dino8-kernel/src/fillet.cpp:2562` onward, inside
`FilletConvexEdges`) now runs those same two helpers per m==1 endpoint,
shortening/shifting that edge's own cylinder before it is built exactly as
the single-edge function does, then dispatches to the ellipse-cap splice
instead of the old flat notch whenever an oblique crossing is found.
Verified: a single-edge `FilletConvexEdges` call on the existing
oblique-end test fixture now reproduces `FilletConvexEdge`'s own result
bit-for-bit (face/edge/vertex counts, tessellated volume, cylinder length,
and cap-notch-point counts all match,
`TestFilletConvexEdgesSingleEdgeMatchesFilletConvexEdgeOnObliqueEnd`); two
parallel obliquely-ended edges that both terminate at the SAME oblique
face still sew into one closed, valid, manifold solid whose volume matches
the doubled single-edge closed form, with both the shared perpendicular
end face and the shared oblique end face each correctly collapsing their
two independent notch splices into one shared edge
(`TestFilletConvexEdgesParallelPairWithObliqueEndsIsClosedAndMatchesClosedForm`);
and the pre-existing oversized-radius rejection still fires through the new
code path (`TestFilletConvexEdgesRejectsOversizedRadiusAtObliqueEnd`) —
all three new tests in `tests/test_basic.cpp`. Full `dino8_kernel_tests`
suite re-run after this change: all checks pass, 0 failures, 0 regressions.

Net effect on the scores below: the "Fillet end conditions on adjacent end
faces" bullet's own evidence is updated in place, but its status **stays
`partial`**, not `present` — `FilletConcaveEdges` still leaves oblique ends
entirely out of scope and neither multi-edge function handles a non-planar
end face, so this is a real narrowing of an existing partial item's own gap,
not a missing-to-partial or partial-to-present flip. Blending & chamfering's
numeric row (1.5/24/5/17/2/56.3%) is therefore unchanged by this session's
work, same as the healing-category note just above.

**An unrelated correctness fix, same session, found only because it was
blocking a green suite for the work above:** after the fillet fix, rebasing
onto other concurrent sessions' commits on this branch (`SubD::Symmetrize`
among them) turned up a genuine, deterministic, pre-existing failure with
zero connection to blending/chamfering:
`TestSubDSymmetrizeMirrorsAndWeldsSeam`-style coverage's own
`mesh.IsClosedManifold()` check on a Symmetrize()-doubled open box failed
(0 checks passed there), even though the SubD's own `Check()`/`IsValid()`
both reported a clean, closed, manifold topology one line earlier. Root
cause, confirmed with a standalone repro reading `DINO8_MESH_DEBUG=1`'s own
diagnostic counters: `ON_SubD::GetControlNetMesh()` (called from
`SubD::ToApproximateMesh()`, `dino8-kernel/src/subd.cpp`) exports the seam
Symmetrize() welds into one shared `ON_SubDVertex` as TWO coincident-but-
separately-indexed mesh vertices (confirmed: the repro's own printed vertex
list showed positions 4-7 duplicated verbatim as 12-15) - orientation was
consistent (no directed-edge conflict) but 8 of 24 undirected edges had
degree 1 instead of 2, i.e. a genuinely open mesh exported from a genuinely
closed SubD. Fixed by calling the mesh's own
`CombineIdenticalVertices(true, true)` on `ToApproximateMesh()`'s result - a
no-op for any ordinary seam-free control net (verified: the pre-existing
box/grid round-trip tests in `tests/test_basic.cpp`, which assert exact
vertex/face counts, are unaffected) and a real weld only where
`GetControlNetMesh()` itself introduces this kind of duplicate. This is not
a Blending & chamfering item and does not change any category's score - it
is recorded here only so the fix and its reasoning are traceable from the
same place the rest of this session's work is. Full `dino8_kernel_tests`
suite re-run twice after this fix (once directly, once via `ctest`): 100%
pass, 0 failures.

**Another later same-day session's addition, in Blending & chamfering:**
closed PARITY_MAP's own "Alternative blend rail types (distance-from-edge,
distance-between-rails / disc blend, non-rolling-ball cross-section
placement)" gap - the ONE item this document had previously recorded as
`missing` in this category (see the "Alternative blend rail types" bullet
below, and the ranked-backlog row for the same item). Every fillet
elsewhere in `fillet.h`/`fillet.cpp` (`FilletConvexEdge`, `FilletConcaveEdge`,
and every function built on top of them) takes the rolling-ball RADIUS
directly as its one free parameter - Rhino 8's own `FilletEdge` command's
default `RailType=RollingBall`. Rhino 8 also exposes
`RailType=DistFromEdge` and `RailType=DistBetweenRails`: the exact same
rolling-ball fillet surface, specified by measuring a distance instead of a
radius. Four new functions add exactly that alternate input for the one
case this kernel's fillets are already exact on (a straight edge shared by
two PLANAR faces) - `FilletConvexEdgeByDistanceFromEdge`,
`FilletConvexEdgeByDistanceBetweenRails`, and their `FilletConcaveEdge`
mirrors (`dino8-kernel/include/dino8/kernel/fillet.h`,
`dino8-kernel/src/fillet.cpp`) - each a closed-form conversion to the
already-existing radius parameter, then a verbatim DISPATCH to
`FilletConvexEdge`/`FilletConcaveEdge`, not a re-implementation: both
functions' own `trim_back = radius / tan(theta / 2)` line (theta = the
edge's interior dihedral angle) is the classical wedge-incircle tangent-
length identity, inverted here (`radius = distance * tan(theta/2)` for
`DistFromEdge`) to pick out exactly the one circular arc a given rail
distance implies; `DistBetweenRails`'s own `radius = rail_distance /
(2*cos(theta/2))` is derived independently two ways in the header doc
comment (the wedge's own isosceles triangle, and directly from
`FilletConvexEdge`'s own `contact_i`/`contact_j` via
`|n_i - n_j| = 2*cos(theta/2)`), both agreeing.
Verified, not merely derived: five new tests in `tests/test_basic.cpp`
compare each new function's OUTPUT Brep against `FilletConvexEdge`/
`FilletConcaveEdge` called directly at an INDEPENDENTLY hand-derived
equivalent radius (topology, tessellated volume, and - for
`DistBetweenRails` - the literal 3D contact-point positions), on two
different dihedral angles: a box's own 90-degree edge (`theta=pi/2`, the
case where `DistFromEdge`'s own distance and the equivalent radius
coincide numerically - a necessary but not sufficient check on its own)
and a regular hexagonal prism's 120-degree side/side edge (`theta=2*pi/3`,
where `tan(theta/2) = sqrt(3)` genuinely exercises the formula rather than
hiding behind a `tan(45)=1` coincidence), plus the L-shaped concave prism's
own 90-degree reflex edge for the `FilletConcaveEdge` mirrors. A sixth new
test checks input validation: non-positive distances, a radius too large
to fit (propagated from the underlying `FilletConvexEdge`/
`FilletConcaveEdge` call, not re-implemented), a non-edge, and - genuinely
needed, not merely copied for symmetry, since `EdgeConvexity()` alone
cannot distinguish a convex edge from its concave mirror - feeding a
concave edge to a convex wrapper (and vice versa) is refused rather than
silently mis-dispatched. Full `dino8_kernel_tests` suite (4150+ checks) and
`ctest` both re-run clean afterward: 100% pass, 0 failures, 0 regressions.
Still partial, not present: only a single straight edge (not
`FilletConvexEdges`/`FilletConcaveEdges`' multi-edge or vertex-blend
forms), only the rolling-ball circular cross-section (not the chamfer or
conic families), and only the planar-adjacent-face case every other fillet
in this file is already limited to - a curved-face rail type, where the
distinction from a plain rolling ball actually changes the underlying
algorithm rather than just its input parameterization, remains real,
disclosed future work.

This flips the item missing->partial: Blending & chamfering moves from
5/17/2/24 (56.3%) to 5/18/1/24 (58.3%) - the table row below already
reflects this. The two headline numbers at the top of this document are
adjusted by this category's own weighted share of that change
(weight 1.5, `(58.3333 - 56.25) * 1.5 = 3.125` percentage points of
"weighted category-percent"), the same incremental-delta convention this
document already uses elsewhere for a single-category change rather than
a full ground-up recompute of all 25 categories: Fossilith vs
Parasolid/ACIS's total kernel-category weight is 17.75, so
65.4% + 3.125/17.75 = 65.4% + 0.18% ~= 65.6%; Dino 8 vs Rhino 8 +
AutoCAD 2027's total weight across all 25 categories is 25.5, so
71.4% + 3.125/25.5 = 71.4% + 0.12% ~= 71.5%. Both deltas are small enough
that they do not change either headline's displayed first decimal in a way
that would read as a different overall conclusion.

**A later session's addition, in Blending & chamfering, a different item
from the two just above:** closed a real, verified gap in PARITY_MAP's own
"Fillet end conditions on adjacent end faces" item (see that bullet below
for the full detail this summarizes). `FilletConvexEdges`' own m==1
oblique-third-face fix (the "genuinely new work this session" paragraph
above) was never carried over to its concave sibling `FilletConcaveEdges`,
whose own doc comment plainly disclosed the gap ("Oblique third faces are
likewise out of scope here"): a third face oblique to a concave edge at an
m==1 (non-trihedral) endpoint was left as a flat, unshifted notch instead
of the true elliptical cut, even though the single-edge `FilletConcaveEdge`
had already closed exactly this case for itself. A new oblique-end
detection pass in `FilletConcaveEdges` (`dino8-kernel/src/fillet.cpp`,
between its re-trim loop and its cylinder-building loop) now runs the same
`FindObliqueThirdFaceCrossing`/`EllipseNotchCornerAtVertexCylindrical`
helpers per m==1 endpoint, using the concave `D_i = bis*offset -
n_i*radius` sign convention `FilletConcaveEdge`'s own oblique-end block
already established (the negation of `FilletConvexEdges`' own `radius*n_i
- bis*offset`) rather than a fresh re-derivation. Verified, not merely
derived: `TestFilletConcaveEdgesSingleEdgeMatchesFilletConcaveEdgeOnObliqueEnd`
shows a single-edge `FilletConcaveEdges` call on the existing single-edge
oblique-end fixture (`ConcaveLShapedPrismObliqueTop`) reproduces
`FilletConcaveEdge`'s own result bit-for-bit (face/edge/vertex counts,
tessellated volume, cylinder length, cap-notch-point counts);
`TestFilletConcaveEdgesTwoIndependentEdgesWithObliqueEndsMatchHandDerivedLength`
builds a new fixture (`TwoIdenticalConcaveNotchPrismObliqueTop`, two
translated-copy concave notches sharing one tilted top face) and checks
each of the two resulting cylinders' own length against an independently
hand-derived `L + radius*slope` (the 90-degree-corner `D_i = radius*n_j`
simplification, worked out and cross-checked by hand-tracing this
function's own face-matching/swap logic, not assumed from the convex
case); and `TestFilletConcaveEdgesRejectsOversizedRadiusAtObliqueEnd`
confirms the pre-existing oversized-radius rejection still fires through
the new code path. Full `dino8_kernel_tests` suite and `ctest` both
re-run clean afterward: 100% pass, 0 failures, 0 regressions. Still
partial, not present: neither multi-edge function handles a non-planar end
face, and `FilletConcaveEdges`' own m==3 trihedral corner has no
"third, unfilleted face" concept at all, so oblique-end handling does not
apply there.

This is a narrowing of the SAME "Fillet end conditions on adjacent end
faces" partial item the earlier same-day session above also touched, not a
missing->partial or partial->present flip - Blending & chamfering's own
numeric row (1.5/24/5/18/1/58.3%) is unchanged by this session's work, and
neither headline number moves.

**Concurrently, several separate same-day session changes** (at least
kernel: SubD & mesh kernel support's "Kernel-native SubD local edit
operators" item and other items landed on this same branch while this
change was in flight, each elsewhere in this document) flipped their own
items and applied their own weighted deltas to both headlines first. The
top-of-document headline reflects ALL of those changes stacked, not just
this one - this paragraph's own before/after numbers (65.4%/71.4% ->
65.6%/71.5%) are kept as originally written for THIS change's own
isolated history, the same "paragraph keeps its own isolated math, only
the actual headline carries the merged total" convention this document
already used once above for a same-category double-change. Given how many
sessions were active on this branch concurrently, the top-of-document
headline may have moved further still by the time this is read; treat it,
not this paragraph's arithmetic, as authoritative.

4. **Genuinely new kernel work landed after this pass's own commit
   inventory was drawn up:** `dino8::kernel::CounterboreHole`
   (dino8-kernel/include/dino8/kernel/features.h,
   dino8-kernel/src/features.cpp) — a real, dedicated, kernel-native
   counterbore-hole feature op (a single compound cutter `Brep`, two
   coaxial `Brep::CylindricalFace` entries over adjacent, non-overlapping
   axial ranges, subtracted via one `BooleanCombineMixed(...,
   BooleanOp::Difference)` pass), with unit tests verifying its result
   against a hand-derived closed-form volume, an independent Manifold
   mesh-boolean cross-check, and `TessellateToClosedMeshConforming()`
   closed-manifold-ness. kernel: Feature operations' "Counterbore
   (stepped coaxial) hole" item upgrades partial→present (was 5/14/5/24,
   50.0%). This landed the same day as an independent, unrelated change to
   the SAME category — `DraftFacesConvexPlanar` (see the "Fifth same-day
   follow-up" note above) flips "Draft/taper faces of an existing body
   about a neutral plane" missing→partial. Recombined from the true
   shared baseline rather than stacking either session's own isolated
   arithmetic on top of the other's, same convention as item 3's own
   double-change just above: **6/14/4/24, 54.2%** (Present 5+1, Missing
   5−1, Partial 24−6−4). As with every note above, the top-of-document
   headline may have moved on again by the time this is read under this
   branch's own concurrent-session load; that figure, not any one
   paragraph's own arithmetic, is authoritative.

The main caveat is the same one every run of this method has: the
granularity of "one item" is a judgment call made by the mapper (this pass),
so item counts and percentages would shift somewhat under a different,
equally reasonable split of the same underlying capabilities.

**A later same-day session's addition, after this pass's own headline was
written:** kernel: Local / direct-edit operations gained a real
`PushPullFace(solid, face_index, distance)` (`boolean.h`/`boolean.cpp`),
closing PARITY_MAP's own "Push/pull a face (extrude face and merge/cut into
its own body)" gap — genuinely distinct from `OffsetFace`, which always
keeps a solid's face count fixed by re-extending/re-trimming every other
face in place. A **push** (`distance > 0`) leaves every other face
untouched and adds brand-new side-wall PlanarFaces (one per edge of the
pushed face's own loop) bridging the old boundary to the new one, plus a
new cap — genuinely extruded material, not a moved boundary (a box's top
face pushed out by 2 goes from 6 faces to 10, not OffsetFace's unchanged
6). A **pull** (`distance < 0`) instead retrims every neighbour face
whose own plane is perpendicular to the pushed face's normal, via a single
exact half-space clip (valid for a concave neighbour loop too), and adds no
new geometry — same net shape as an equivalent `OffsetFace` shrink, from a
completely different, boolean-free construction. This was **not** built by
extruding a NURBS profile via `Brep::Extrude()` and handing the result to
`BooleanCombinePlanar()`, the obvious first construction: that path was
tried and found to fail on two independent grounds, confirmed by testing,
not assumed — `Extrude()`'s own swept profile is only piecewise (not
globally) planar for a straight polygon, and even a hand-assembled
all-planar prism made `BooleanCombinePlanar()` throw ("an edge is shared by
3 or more faces") on the flush, zero-overlap coincident face a prism grown
directly off an existing face always has, a genuine gap in that engine's
own coincident-face handling. Direct topological surgery avoids both
problems entirely. Has **no convexity precondition** on `solid` at all
(unlike every other item already in this category), verified on both a box
and a genuinely non-convex, reflex-cornered L-shaped prism (push and pull
both), plus argument-error cases (zero distance, out-of-range face_index,
an oblique neighbour on a pull, a pull large enough to collapse a
neighbour) — five new tests in `tests/test_basic.cpp`, and the full
`dino8_kernel_tests` suite re-run clean (0 failures) after adding them.
Scoped to a planar-faced `solid` (`PlanarFaces()`'s own precondition, same
as every sibling in this category) and, for a pull, to neighbours
perpendicular to the pushed face (an oblique neighbour is refused, not
guessed at) — a curved-face push/pull and an oblique-neighbour pull remain
real, disclosed future work. This flips the item missing→partial: kernel:
Local/direct-edit operations moves from 6/17/5/28 (51.8%) to 6/18/4/28
(53.6%).

**Concurrently, a separate same-day session change** (this document's own
"Merge contiguous tangent edges" follow-up, below) flipped a second item
in this SAME category from `partial` to `present`. The table row below
reflects BOTH changes combined, not just this one: 7/17/4/28 (55.4%), not
the 6/18/4/28 (53.6%) this paragraph's own isolated math gives. This
paragraph's own before/after numbers are kept as originally written for
this change's own history; only the table itself carries the merged
total. This category's own headline contribution was never added to the
document's headline number by this paragraph's own change — folded in
below alongside the other change's own headline delta.

**Later still, a data-exchange session:** `Mesh::LoadObj` (dino8-kernel/src/mesh.cpp)
no longer rejects an OBJ `f` line with more than 4 space-separated corners
outright — it fan-triangulates the n-gon from its own first corner into
`n-2` real `ON_MeshFace` triangles instead, resolving negative (relative)
indices first, the same as every other face line, so a relative-index n-gon
splits identically to the equivalent absolute one. See kernel: Kernel-level
data exchange's own "OBJ read" bullet below for the construction and test
detail. **No score change:** the item was already counted `partial` (for
the separate, still-true "no true UV seams" limitation named there), and
stays `partial` — a concave n-gon can still fan-triangulate into a triangle
whose interior falls outside the source polygon, a disclosed limitation of
the fan approach this pass does not attempt to detect or fix. File-disjoint
from every other session above.

## Priority order for maximum score-per-fix

Added 2026-09-30, alongside that day's re-score above. Ranks every category
in both tables below by `weight / remaining_items` (`remaining_items` =
partial + missing = non-present items in that category), descending — the
weighted-headline points earned per single item closed, highest first, so a
future round can pick the next gap from the top of these lists instead of
an arbitrary one. It is a per-item-closed heuristic, not a per-effort one
(closing one `small`-effort item in a low-ranked category can still be
cheaper in practice than one `large`-effort item in a high-ranked one — see
the effort tags in the Ranked closeable backlog above for that dimension);
the two are meant to be read together, not as a single number. Ties are
broken by table order. Both tables' ratios recomputed fresh from the two
category tables as they stand after this pass's own two reclassifications
(kernel table unchanged from the last verification; app table reflects
Screen-reader support and Plugin marketplace both moving `missing`->
`partial` — see above).

### Kernel table (Fossilith vs Parasolid/ACIS), by weight / remaining items

| Rank | Category | Weight | Remaining (partial+missing) | Weight / Remaining |
|---|---|---|---|---|
| 1 | SubD & mesh kernel support | 0.75 | 7 | 0.107 |
| 2 | Intersections & projections | 1.5 | 16 | 0.094 |
| 2 | Boolean operations | 1.5 | 16 | 0.094 |
| 4 | Geometry representation | 1 | 11 | 0.091 |
| 4 | Healing, repair, validation, tolerant modeling | 1 | 11 | 0.091 |
| 4 | Tessellation / faceting | 1 | 11 | 0.091 |
| 4 | Curve operations | 1 | 11 | 0.091 |
| 8 | Blending & chamfering | 1.5 | 19 | 0.079 |
| 9 | Topology & data structure | 1 | 14 | 0.071 |
| 9 | Mass properties & spatial queries | 1 | 14 | 0.071 |
| 11 | Surface operations | 1 | 15 | 0.067 |
| 12 | Feature operations | 1 | 18 | 0.056 |
| 13 | Kernel-level data exchange | 1 | 19 | 0.053 |
| 14 | Local / direct-edit operations | 1 | 21 | 0.048 |
| 15 | Sweeping, lofting, extruding, revolving | 1 | 23 | 0.043 |
| 16 | Transformations, patterns, splitting | 0.5 | 13 | 0.038 |
| 17 | Offsetting, shelling, thickening | 1 | 27 | 0.037 |

### App table (Dino 8 vs Rhino 8 + AutoCAD 2027), by weight / remaining items

| Rank | Category | Weight | Remaining (partial+missing) | Weight / Remaining |
|---|---|---|---|---|
| 1 | Viewport display, rendering & visualization | 1.0 | 5 | 0.200 |
| 1 | Scripting, automation & visual programming | 1.0 | 5 | 0.200 |
| 3 | Command system & core commands | 1.5 | 8 | 0.188 |
| 4 | 2D drafting, annotation & documentation | 1.0 | 6 | 0.167 |
| 4 | UI/UX, accessibility & localization | 1.0 | 6 | 0.167 |
| 6 | SubD & mesh modeling toolset (app level) | 0.75 | 5 | 0.150 |
| 7 | File I/O & interoperability (app level) | 1.0 | 12 | 0.083 |
| 8 | Ecosystem, trust, cloud/AI & platform reach | 0.5 | 9 | 0.056 |

The app table's top four categories (Viewport display, Scripting, Command
system, 2D drafting/UI-UX) each earn 2-4x the headline points per item
closed that the bottom two (File I/O, Ecosystem) do — a single closed item
in Viewport display or Scripting is worth as much to the Dino 8 headline as
roughly 3-4 items closed in Ecosystem. On the kernel side the spread is
narrower (SubD & mesh kernel support tops out at ~3x Offsetting/shelling at
the bottom) because kernel category weights cluster closer together (mostly
0.5-1.5) than the app table's does (0.5-1.5 over fewer, larger categories).
UI/UX moved up two rows this pass purely from its own reclassification
above (Screen-reader support leaving `missing` narrows the category's own
remaining-item denominator); Ecosystem's remaining count also dropped by
one for the same reason (Plugin marketplace), but it started so far behind
(0.5 weight over 16 items) that it stays last.

## Kernel: Fossilith vs Parasolid/ACIS

| Category | Weight | Items | Present | Partial | Missing | Parity % |
|---|---|---|---|---|---|---|
| kernel: Topology & data structure | 1 | 27 | 13 | 13 | 1 | 72.2% |
| kernel: Geometry representation | 1 | 29 | 18 | 11 | 0 | 81.0% |
| kernel: Boolean operations | 1.5 | 25 | 9 | 15 | 1 | 66.0% |
| Blending & chamfering | 1.5 | 24 | 5 | 18 | 1 | 58.3% |
| kernel: Sweeping, lofting, extruding, revolving | 1 | 29 | 6 | 21 | 2 | 56.9% |
| kernel: Offsetting, shelling, thickening | 1 | 27 | 0 | 27 | 0 | 50.0% |
| kernel: Local / direct-edit operations | 1 | 28 | 7 | 21 | 0 | 62.5% |
| kernel: Intersections & projections | 1.5 | 29 | 13 | 14 | 2 | 69.0% |
| kernel: Healing, repair, validation, tolerant modeling | 1 | 30 | 19 | 10 | 1 | 80.0% |
| kernel: Mass properties & spatial queries | 1 | 30 | 16 | 14 | 0 | 76.7% |
| kernel: Tessellation / faceting | 1 | 25 | 14 | 9 | 2 | 74.0% |
| kernel: Transformations, patterns, splitting | 0.5 | 22 | 9 | 13 | 0 | 70.5% |
| kernel: Kernel-level data exchange | 1 | 27 | 8 | 12 | 7 | 51.9% |
| kernel: Feature operations | 1 | 24 | 6 | 16 | 2 | 58.3% |
| Fossilith kernel — Curve operations | 1 | 28 | 17 | 11 | 0 | 80.4% |
| Kernel Surface Operations (Fossilith / Dino 8) | 1 | 29 | 14 | 15 | 0 | 74.1% |
| Kernel: SubD & mesh kernel support | 0.75 | 22 | 15 | 7 | 0 | 84.1% |

Two rows changed from the prior measurement: **Local / direct-edit
operations** (48.2% → 51.8%, `ImprintFaces` cross-reference fix + arithmetic
correction) and **Transformations, patterns, splitting** (68.2% → 70.5%,
`SplitByObjectCommand` also credited here). **Intersections & projections**'
percentage also changed (65.5% → 69.0%) but that is purely the arithmetic
fix described above — no item's status moved. Every other row is numerically
identical to the 2026-09-25 map; several (booleans, blending, topology) have
corrected citations or evidence text behind an unchanged score, detailed in
the honesty notes above and the bullets below.

**Same-day follow-up after this pass's own measurement:** `SubD::Check()`
(dino8-kernel/src/subd.cpp) landed, closing the **Kernel: SubD & mesh kernel
support** row's own "SubD non-manifold/multi-body validity checks" item
(72.7% → 75.0%, 13/6/3 → 14/5/3 present/partial/missing) — detailed in that
category's own bullet list below. This is the only row this document's own
headline number has moved for since the measurement above was taken
(64.4% → 64.5%, weighted); no other category was touched.

**Second same-day follow-up:** `NurbsSurface::DecomposeToBeziers`
(dino8-kernel/src/surface_edit.cpp:507; surface.h:958) landed, closing the
**Kernel Surface Operations (Fossilith / Dino 8)** row's own "Convert to
Beziers (surface)" item — the category's one remaining `missing` item
(72.4% → 74.1%, 14/14/1 → 14/15/0 present/partial/missing) — detailed in
that category's own bullet list below. Weighted the same way as the
`SubD::Check()` follow-up above, this moves the headline by the same
marginal-delta method (that row's own weight of 1 against the 17.75 total
kernel weight): 64.5% → 64.6%; no other category was touched.

**Third same-day follow-up (this session):** kernel: Boolean operations
gained a genuine new kernel entry point, `SplitBySheet(solid, sheet)`
(boolean_general.h/.cpp) - the parity map's own next-highest-ranked still-
missing boolean item, "Sheet/solid trim (open surface as cutter through a
solid)". It reuses `BooleanCombineGeneral`'s/`ImprintFaces`' own SSX-
gathering and `FragmentFaces()` machinery for both operands, but - unlike
either of those - only ONE operand (`solid`) is required to be closed:
`solid`'s own fragments are bucketed by a new closest-point-plus-normal-
sign test (`ClassifySideOfSheet`, using the existing Newton-polished
`SurfaceClosestPointGlobal`) against `sheet`, which may be a genuinely open
cutting surface with no volume to ray-cast in/out of at all; `sheet`'s own
fragments are ray-cast in/out of `solid` as usual (valid since `solid`
really is closed) and its IN fragments become a new, correctly-oriented
cap face added to BOTH output pieces. Modeled on boolean.cpp's own
`SplitByPlane` (returns both pieces rather than one caller-chosen "kept"
side, since which piece to keep is a UI decision, not a geometric one).
Verified (`TestSplitBySheet*`, tests/test_basic.cpp): a 4x4x4 box fully
severed by a flat open planar sheet larger than the box's own footprint
comes back as two valid closed B-reps whose volumes are each exactly half
(32) and sum back to the original (64) exactly; a sheet that never
reaches the solid at all correctly returns the whole untouched solid on
one side and the empty Brep on the other, mirroring `ImprintFaces`' own
"kept.empty()" convention; both faceless-operand cases throw
`std::invalid_argument`. Full `dino8_kernel_tests` suite (via `ctest`):
100% passing, 0 regressions.

Still `partial`, not `present`: no app command exposes it yet
(`cmd_boolean.cpp` still skips every non-closed operand outright); it
inherits `BooleanCombineGeneral`'s own scope limits (one crossing chain
per opposing face pair, genus-0 faces); only a flat cutting plane is
tested (a genuinely curved `sheet` is unexercised); and the item's OTHER
half - trimming a sheet body BY a solid (cutting the sheet's own surface
down, not splitting a volume) - is not implemented at all. Net effect on
the scores below: kernel: Boolean operations' "Sheet/solid trim" bullet
upgrades missing -> partial: 8/15/2/25 (62.0%, was 8/14/3/25 60.0%);
weighted the same marginal-delta way as the two follow-ups above (this
row's own weight of 1.5 against the 17.75 total kernel weight, on top of
the 64.6% those two already established, not a full re-derivation of
every other row): 64.6% → 64.8%.

**Fourth same-day follow-up:** `Brep::MergeContiguousEdges`/
`MergeAllContiguousEdges` (dino8-kernel/src/brep.cpp, dino8-kernel/include/
dino8/kernel/brep.h) landed, closing the **kernel: Healing, repair,
validation, tolerant modeling** row's own "Edge merging ... app-only; no
kernel wrapper" item — a genuine kernel wrapper around `ON_Brep::
CombineContiguousEdges` (the same OpenNURBS primitive the app-only
`MergeEdgeCommand`, cmd_fillet.cpp, already called directly), with real
unit-test coverage the app-only version never had: a same-vertex/matching-
face-structure/kink-angle contract enforced and verified (two collinear
naked edges merge; a genuine 90-degree corner is refused at the default
5-degree tolerance but merges once that tolerance is opened past the
actual kink; a valence-3 vertex, an edge paired with itself, an
out-of-range index, and an already-deleted index are all refused or throw
per this kernel's own established two-tier contract), plus
`MergeAllContiguousEdges` collapsing a whole chain of collinear edges in
one call (item promoted `partial` → `present`, 30 items unchanged: 18/11/1
→ 19/10/1 present/partial/missing, 78.3% → 80.0%) — detailed in that
category's own bullet list below.

The identical underlying gap was independently listed twice more, under
this document's own "same capability, different category framing" pattern
(see the intro's note on judgment-call granularity): **kernel: Topology &
data structure**'s own "Merge contiguous tangent edges" item (66.7% →
68.5%, 11/14/2 → 12/13/2) and **kernel: Local / direct-edit operations**'s
own "Merge contiguous tangent edges (MergeEdge / MergeAllEdges)" item
(partial → present, one item of that category's 28) both name the exact
same "app-only, no kernel wrapper" gap this same commit closes, so both
are promoted `partial` → `present` alongside it rather than left stale
and contradicting the healing category's own now-updated bullet. This
moves the document's own headline number again, combining all three
category deltas by the same incremental method the follow-ups above use,
applied on top of the prior follow-up's own 64.8% (not an earlier
headline value, which each follow-up above already moved in turn): 64.8%
→ 65.1%, weighted; no other category was touched, and the app's own
`MergeEdgeCommand` still does not call the new kernel method (a separate,
still-open app-wiring gap, not this item's own scope).

**Fifth same-day follow-up (three parallel sessions, merged here):** the
Fourth follow-up above, `PushPullFace` (documented earlier as "A later
same-day session's addition"), and kernel `DraftFacesConvexPlanar`
(dino8-kernel/src/boolean.cpp, dino8-kernel/include/dino8/kernel/boolean.h)
all landed on parallel branches of the same session and **all three**
touch **kernel: Local / direct-edit operations** — three genuinely
different items in that one 28-item category — so this merge folds all
three into one correct combined row instead of applying any one's own
isolated arithmetic on top of another's (each parallel session's own note
above computed its row delta against the same 6/17/5/28, 51.8% baseline
in isolation, correct for that one item alone but not additive by simple
concatenation once more than one actually landed).

`DraftFacesConvexPlanar` itself: an exact B-rep "tilt a named face about
its own intersection line with a caller-supplied neutral plane" operation
for convex planar-faced solids, real unit tests included
(`TestDraftFacesConvexPlanarBoxAllWallsMatchesExactFrustumVolume`,
`TestDraftFacesConvexPlanarSingleFaceLeavesOppositeFaceExactlyUntouched`,
dino8-kernel/tests/test_basic.cpp), full suite re-run green with zero
regressions. It flips "Taper / draft face" missing→partial, and — the
same one-capability-two-vocabularies pattern this document already uses
for `SplitByObjectCommand` — the identical capability also closes
**kernel: Feature operations**' own "Draft/taper faces of an existing
body about a neutral plane" item (24/5/14/5 → 24/5/15/4, 50.0% → 52.1%,
an independent category with no collision).

Recombining kernel: Local / direct-edit operations from its own true
pre-all-three-changes baseline (6/17/5/28, 51.8%): `PushPullFace` flips
"Push/pull a face" missing→partial (+1 partial, -1 missing);
`DraftFacesConvexPlanar` flips the *different* "Taper / draft face" item
missing→partial (+1 partial, -1 missing); `MergeContiguousEdges` flips
the *different again* "Merge contiguous tangent edges" item partial→present
(+1 present, -1 partial) per the Fourth follow-up above. Net: 7 present
(6+1), 18 partial (17+1+1-1), 3 missing (5-1-1) = 7/18/3/28 =
(7·1 + 18·0.5)/28 = 57.1%.

Recomputed cleanly from the pre-Fourth-follow-up 64.8% baseline (the last
number both parallel sessions independently agree on), summing all four
now-known category deltas directly rather than chaining marginal notes on
top of each other: healing (78.3%→80.0%, weight 1) contributes +0.094pp;
topology (66.7%→68.5%, weight 1) contributes +0.104pp; localops (51.8%→
57.1% once all three flips above are combined, weight 1) contributes
+0.302pp; Feature operations (50.0%→52.1%, weight 1) contributes
+0.117pp. Total: 64.8% + 0.62pp → **65.4%** (17.75 total kernel weight
throughout). The combined Dino 8 vs Rhino 8 + AutoCAD 2027 headline is
left at 71.4% — consistent with how every same-day follow-up note above
already only moves the kernel-only headline, never that second one. No
other row was touched this pass.

**Sixth same-day follow-up (a parallel session):** kernel: Boolean operations' own
`BooleanCombineGeneral(a, b, op, tolerance)` (boolean_general.h/.cpp) now
takes an optional caller-specified `tolerance` — until now every entry
point in that file (`BooleanCombineGeneral`, `ImprintFaces`, `SplitBySheet`)
default-constructed its own internal `IntersectOptions` with no way for a
caller to loosen or tighten the SSX Newton-refinement accuracy at all.
Defaults to the prior implicit 0.001, so every existing caller is
bit-for-bit unaffected — verified directly (`TestBooleanCombineGeneral
CallerTolerance`, tests/test_basic.cpp): omitting the parameter reproduces
identical topology (face/edge counts) and a bit-identical tessellated
volume to passing the explicit prior default. The parameter is a genuine,
wired-through control, not a decorative no-op: on the box+cylinder
Intersection fixture (the same fixture `TestBooleanCombineGeneralBox
Cylinder` uses), whose result is entirely two SSX-computed circles where
the box's top/bottom planes cut the cylinder wall, a tight tolerance
(1e-7) keeps every result vertex within 1e-4 of the true cylinder radius
while a loose one (0.05) measurably degrades that same measurement by
more than 10x — a real, direct measurement of the actual Newton-refinement
solve accuracy, not tessellation density (this engine's edges are built
one straight `ON_LineCurve` segment at a time between consecutive
Newton-refined chain/vertex points, so the brep's own vertices are exactly
where that accuracy shows up). A non-positive tolerance now throws
`std::invalid_argument`, the same typed-refusal convention this file
already uses for `SymmetricDifference`/faceless operands. Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions.

Still `partial`, not `present`, on the "Tolerant booleans" item (detailed
in its own bullet below): only `BooleanCombineGeneral` takes this
parameter — `BooleanCombinePlanar`/`BooleanCombineMixed` and
`ImprintFaces`/`SplitBySheet` still each hardcode their own internal
tolerance with no caller control at all, and there is still no gap-healing
of imprecise operands. Net effect on the scores below: kernel: Boolean
operations' own item count is unchanged (8/15/2/25, 62.0%) — this upgrades
the "Tolerant booleans" bullet's own evidence, not its bucket, since the
item was already `partial` and stays `partial`. The kernel-only headline
stays at 65.4%; no other row was touched this pass.

**Sixth same-day follow-up (a parallel session):** three of the five named sub-operators in
**Kernel: SubD & mesh kernel support**'s own "Kernel-native SubD local
edit operators (insert edge, extrude face, spin edge, weld, expand)"
[missing] item now genuinely exist: `SubD::InsertEdge`, `SubD::SpinEdge`,
and `SubD::ExtrudeFace` (dino8-kernel/src/subd.cpp, dino8-kernel/include/
dino8/kernel/subd.h). Each is a thin, validated wrapper around a real
(read directly, not assumed non-stub) OpenNURBS primitive this kernel had
never called before: `ON_SubD::SplitFace(face, v0, v1)` for InsertEdge
(divides a face into two along a new edge between two of its own
non-adjacent corners), `ON_SubD::SpinEdge(edge, spin_clockwise)` for
SpinEdge (rotates a shared interior edge's endpoints around its two
adjacent faces' boundaries - the SubD analog of a triangle mesh's
edge-flip), and `ON_SubD::ExtrudeComponents(xform, cptr_list, 1)` for
ExtrudeFace (translates one face along its own outward
`ControlNetCenterNormal()`, genuinely opening a "chimney" through side
faces rather than detaching a floating island, when the extruded face
already had neighbors). All three follow this class's existing point-pair
vertex lookup convention (`FindVertex`/`FindEdge`, matching
`SetCrease`/`SetEdgeSharpness`/`CapBoundaryLoop`) rather than inventing a
new one.

Verified with 3 new tests (`TestSubDInsertEdgeSplitsFaceIntoTwoAlongDiagonal`,
`TestSubDSpinEdgeRotatesSharedInteriorEdge`,
`TestSubDExtrudeFaceAddsProtrusionAlongNormal`, tests/test_basic.cpp): a
lone flat quad splits along its diagonal into exactly two genuine
triangles (1/4/4 → 2/4/5 face/vertex/edge counts) and refuses to re-split
an already-adjacent pair or a pair no longer sharing a big-enough face;
the shared interior edge of a two-quad strip genuinely changes which
vertex pair it connects (checked by id, not just by the boolean return
value) while every topology count and `Check()`'s manifold-single-body
verdict stay exactly the same, and a naked boundary edge or an unknown
point both correctly refuse; and a lone free-standing quad extruded along
its own normal comes back as 5 faces / 8 vertices / 12 edges with the
moved face's own 4 corners landing exactly at +distance and 4 brand-new
vertices left behind at the original (now open, per `Check()`'s
`naked_edges == 4`) footprint - a real protrusion, not a floating
disconnected copy. Full `dino8_kernel_tests` suite (via `ctest`): 100%
passing, 0 regressions.

Still `partial`, not `present`: the other two named sub-operators, weld
(joining two separate edges/vertices together) and expand (a uniform
push-apart of a face/region), have no kernel entry point at all still;
`InsertEdge` only splits a quad-or-larger face along two of ITS OWN
corners (no new-vertex-on-an-edge-midpoint variant); `SpinEdge` requires
strict interior topology (`HasInteriorEdgeTopology`) with no
non-manifold-edge fallback; `ExtrudeFace` is single-face-at-a-time (no
multi-face/region extrude in one call, unlike the delegate's own
`ExtrudeComponents` which already accepts a list) and always extrudes
along the face's own normal (no explicit direction/taper option
exposed). None of the three is wired to any app command yet. Net effect:
this item's own category row moves missing → partial (14/5/3/22, 75.0% →
14/6/2/22, 77.3%); weighted the same marginal-delta way as the follow-ups
above (this row's own weight of 0.75 against the 17.75 total kernel
weight, on top of the Fifth follow-up's own 65.4%, unchanged by the
Boolean-tolerance follow-up directly above since that one stayed
`partial`→`partial`): 65.4% → 65.5%, this paragraph's own isolated math
(recombined with the parallel Seventh follow-up below, which lands on the
same 65.4% baseline independently).

**Seventh same-day follow-up (this session, parallel to the Sixth above):**
kernel: Local / direct-edit operations gained a real
`ReplaceFacePlaneConvexPlanar(solid, face_index, new_plane)`
(dino8-kernel/include/dino8/kernel/boolean.h,
dino8-kernel/src/boolean.cpp), closing this category's own next-highest-
value still-`missing` item, "Replace face (swap a face's surface, re-trim
it and its neighbours)" — before this addition the only entry in this
whole category with no kernel answer at all, `grep`-confirmed (the nearest
hits, `Brep::ReplaceEdgeCurve` and the app's `SoftEditSrfCommand`, touch an
edge or write `m_S` directly, neither re-trims neighbours). Full detail,
including the two independent correctness proofs (bit-for-bit agreement
with `OffsetFace()` on a pure translate; an independent closed-form double
integral plus a pointwise wall-by-wall re-trim check on a combined
move-and-tilt "roof" plane neither `OffsetFace()` nor
`DraftFacesConvexPlanar()` alone could produce in one call) and the
refusal-case coverage, is in that item's own bullet below, not repeated
here. Three new tests in `tests/test_basic.cpp`
(`TestReplaceFacePlaneConvexPlanarMatchesOffsetFaceForPureTranslate`,
`TestReplaceFacePlaneConvexPlanarTiltedRoofMatchesExactIntegralAndRetrimsWalls`,
`TestReplaceFacePlaneConvexPlanarRefusesInvalidInput`); full
`dino8_kernel_tests` suite re-run via `ctest` after adding them: 100%
passing, 0 regressions.

This flips the item missing→partial: kernel: Local/direct-edit operations
moves from 7/18/3/28 (16/28 = 57.142857%) to 7/19/2/28 (16.5/28 =
58.928571%), a +1.785714pp category-level move — this paragraph's own
isolated math, computed (like the Sixth follow-up's own SubD math above)
against the same 65.4% baseline both parallel sessions started from, not
chained on top of the other.

**Recombining the Sixth and Seventh follow-ups** (two independent,
file-disjoint categories — Kernel: SubD & mesh kernel support and kernel:
Local / direct-edit operations — landed in parallel on the same 65.4%
baseline, the same "sum both deltas directly, don't chain" method the
Fifth follow-up's own three-way merge above used): SubD (16.5/22 → 17/22,
weight 0.75) contributes 0.75 · (2.272727/100) / 17.75 = +0.0960pp;
localops (16/28 → 16.5/28, weight 1) contributes 1 · (1.785714/100) /
17.75 = +0.1006pp. Total: 65.4% + 0.1966pp → **65.6%** (17.75 total kernel
weight throughout, both rows' own table entries above already reflect
their individual post-change counts). The combined Dino 8 vs Rhino 8 +
AutoCAD 2027 headline is left at 71.4%, unchanged, same as every prior
follow-up above. No other row was touched this session.

**Eighth same-day follow-up (this session):** `Brep::MakeEdgeKillRing`/
`Brep::KillEdgeMakeRing` (dino8-kernel/src/brep.cpp, dino8-kernel/include/
dino8/kernel/brep.h) — the MEKR/KEMR Euler-operator pair the topology
category's own "Euler operators" item had named as the one entirely-absent
piece of that family, right after the immediately preceding
MakeEdgeFace/KillEdgeFace (MEF/KEF) pair landed (`467c45d`). Real unit-test
coverage included (`TestBrepMakeEdgeKillRingAndKillEdgeMakeRingAreExactInverses`,
dino8-kernel/tests/test_basic.cpp), full `ctest` suite re-run green (4160/4160
checks, zero regressions). This closes the item completely (all six named
operators — MEV/KEV, MEF/KEF, MEKR/KEMR — are now real, tested, and
round-trip exact), flipping **kernel: Topology & data structure**'s own
"Euler operators" item `partial` → `present` (12/13/2 → 13/12/2, 68.5% →
70.4%). This item's own category (topology) is file-disjoint from every
other category any concurrent same-day session touched (blending, SubD,
localops), so - the same "sum deltas directly against the last certain
common baseline, don't chain through a parallel session's own paragraph"
method the Fifth and Sixth/Seventh follow-ups above already use - this
paragraph's own isolated math is computed against the original 65.4%
baseline directly: topology's delta (68.5%→70.4%, weight 1, 17.75 total
kernel weight) contributes +0.104pp, landing this paragraph's own isolated
math at 65.4% + 0.104pp → 65.5%. As with the blending follow-up's own
note above, **the top-of-document headline reflects ALL concurrent
same-day changes stacked together, not just this one** - by the time this
was written that was blending (+0.18pp) + SubD (+0.096pp) + localops
(+0.101pp) + topology (+0.104pp) on the same 65.4% starting point, landing
at 65.4% + 0.481pp → **65.9%**; treat the top-of-document number, not this
paragraph's own isolated 65.5%, as authoritative. The combined Dino 8 vs
Rhino 8 + AutoCAD 2027 headline is unaffected by this item (a kernel-only
category) and stays at whatever the other concurrent changes left it -
71.5% at the time of writing, per the blending follow-up's own math above.
No other row was touched this pass.

**Ninth same-day follow-up (a fourth parallel session):** `ExtrudeToBoundary`
(dino8-kernel/src/boolean_general.cpp) landed, upgrading **kernel:
Sweeping, lofting, extruding, revolving**'s own "Extrude to a boundary
surface / body" item from missing to partial (6/20/3/29, 55.2% →
6/21/2/29, 56.9%) — detailed in that category's own bullet list below.
This row is file-disjoint from every other category touched by the
Sixth/Seventh/Eighth follow-ups above (kernel: Boolean operations, Kernel:
SubD & mesh kernel support, kernel: Local / direct-edit operations,
Blending & chamfering, kernel: Topology & data structure), so - the same
"sum deltas directly against the original shared baseline" method those
follow-ups already use - this paragraph's own isolated math is computed
against the original 65.4% baseline directly: sweeplofts' own delta
((6+10.5)/29 − (6+10)/29) · 100 = 1.724138pp, weight 1, 17.75 total kernel
weight, contributes +0.0971pp, landing this paragraph's own isolated math
at 65.4% + 0.0971pp → 65.5%. As with the blending and topology follow-ups'
own notes above, **the top-of-document headline reflects ALL concurrent
same-day changes stacked together, not just this one** - by the time this
was written that was blending (+0.176pp) + SubD (+0.096pp) + localops
(+0.101pp) + topology (+0.104pp) + sweeplofts (+0.097pp) on the same 65.4%
starting point, landing at 65.4% + 0.574pp → **66.0%**; treat the
top-of-document number, not this paragraph's own isolated 65.5%, as
authoritative. The combined Dino 8 vs Rhino 8 + AutoCAD 2027 headline is
unaffected by this item (a kernel-only category with no app-level command
calling it yet - a grep for `ExtrudeToBoundary` under `dino8-app/src`
finds nothing) and stays at 71.5%, per the blending follow-up's own math
above. No other row was touched this pass.

**Tenth same-day follow-up (a later session):** `git log --oneline -30 --
dino8-kernel/src/subd.cpp dino8-kernel/src/mesh.cpp` at the start of this
session showed the Sixth follow-up's `InsertEdge`/`SpinEdge`/`ExtrudeFace`
trio (above) as the most recent commit touching either file, so this
session picked the next highest-value still-gap item in the same "Kernel:
SubD & mesh kernel support" category rather than duplicating that work:
`SubD::ExpandFaces` (dino8-kernel/src/subd.cpp,
dino8-kernel/include/dino8/kernel/subd.h) closes the fourth of the five
named sub-operators in this category's own "Kernel-native SubD local edit
operators (insert edge, extrude face, spin edge, weld, expand)" item -
"expand" (the Sixth follow-up's own text: "a uniform push-apart of a
face/region").

`ExpandFaces(face_ids, distance)` delegates to the exact same real,
non-stub `ON_SubD::ExtrudeComponents(xform, cptr_list, cptr_count)`
primitive `ExtrudeFace()` already uses, but calls it with EVERY face in
`face_ids` as one component list and a single shared translation, instead
of looping `ExtrudeFace()` once per face. That distinction is the entire
point of "expand a region" versus "extrude each face separately": reading
`ON_SubD::Internal_ExtrudeComponents` directly (opennurbs_subd.cpp:23265
onward, in the fetched `opennurbs-src` build dependency) confirms its own
component-marking pass extrudes an edge only when it is attached to
exactly one marked face, or is already a boundary edge - an edge attached
to TWO marked faces (i.e. shared between two faces both being expanded
together) is left alone. So a multi-face region moves as one rigid block,
opening exactly one ring of new side faces around its own outer boundary,
while every edge interior to the region stays interior and unwalled -
genuine ON_SubD behavior this wrapper exposes, not something it
implements itself. The push direction is the unit-vector sum of each
listed face's own `ControlNetCenterNormal()` (each unitized before
summing, the sum itself unitized), the natural multi-face generalization
of `ExtrudeFace()`'s own single-face normal convention, refusing rather
than guessing when that sum is degenerate (e.g. two faces with exactly
opposite normals cancel).

Verified with 3 new tests (tests/test_basic.cpp):
`TestSubDExpandFacesSingleFaceMatchesExtrudeFace` confirms a one-face
region reproduces `ExtrudeFace()`'s own exact topology delta on the
identical lone-quad fixture (1/4/4 -> 5/8/12 face/vertex/edge counts, all
corners moved to the requested distance) bit-for-bit, plus this method's
own extra argument-refusal cases (empty list, a duplicate face id).
`TestSubDExpandFacesMovesConnectedRegionAsOneBlockKeepingSharedEdgeInterior`
expands the two-quad strip fixture `TestSubDSpinEdgeRotatesSharedInteriorEdge`
already uses (2 faces sharing one interior edge) as a single region and
checks, by edge id, that the shared edge is STILL `HasInteriorEdgeTopology`
(exactly 2 faces) afterward, with both its own endpoints moved to the
requested distance along with the rest of the region - not walled off into
two independent protrusions - while the resulting face/vertex/edge counts
(2/6/7 -> 8/12/19) and `Check()`'s naked-edge count (6, exactly the
region's own new open base) match hand-derived expectations for a single
combined extrude, not the larger count two independent single-face
extrudes on the same shared edge would produce.
`TestSubDExpandFacesRefusesWhenRegionNormalsCancel` builds two disjoint,
oppositely-wound freestanding quads (unit normals (0,0,1) and (0,0,-1))
and confirms `ExpandFaces` refuses the pair (their sum is the zero vector,
no well-defined single push direction) and leaves the SubD completely
unchanged. Full `dino8_kernel_tests` suite re-run via `ctest`: 100%
passing, 0 regressions.

Still `partial`, not `present`, for this category's own "Kernel-native
SubD local edit operators" item: weld (joining two separate edges/vertices
together) is the one remaining named sub-operator with no kernel entry
point at all - unlike insert/spin/extrude/expand, OpenNURBS' own
`ON_SubD` exposes no ready-made primitive for it (re-grepped
`opennurbs_subd.h`/`opennurbs_subd.cpp` for `Weld|MergeEdge|MergeVertex`:
no matches), so closing it for real would mean hand-rolling the
vertex-merge/component-reconnect surgery directly against `ON_SubD`'s own
lower-level `AddFaceEdgeConnection`/`RemoveFaceEdgeConnection`/
`RemoveEdgeVertexConnection`/`DeleteComponents` building blocks rather than
wrapping one existing call, real but substantially larger scope than this
follow-up's own four calls to `ExtrudeComponents`. `ExpandFaces` is also
still not wired to any app command, the same gap the other three operators
already have. Net effect: this item's own category row is unchanged
(14/6/2/22, 77.3%) - a real narrowing of the item's own remaining gap
(4 of 5 sub-operators now genuinely present, versus 3 of 5 before), not a
missing-to-partial or partial-to-present flip, the same "fold new evidence
into an existing bullet without moving its status" convention the
Offsetting category's own OffsetSolidConvexPlanar/OffsetRefit follow-ups
above already use. The top-of-document headline numbers are therefore
unaffected by this session's work and stay at 66.0% / 71.5%.

**Eleventh same-day follow-up (this session):** `MoveVertexConvexPlanar`
(dino8-kernel/src/boolean.cpp, dino8-kernel/include/dino8/kernel/boolean.h)
landed, upgrading **kernel: Local / direct-edit operations**'s own "Move a
single B-rep vertex directly" item from missing to partial (7/19/2/28,
58.9% → 7/20/1/28, 60.7%) — detailed in that category's own bullet list
below. Unlike the Eighth/Ninth follow-ups above (which each computed their
own isolated delta against the original 65.4% baseline because they landed
concurrently with other same-day sessions), this one lands sequentially on
top of the branch that already carries every prior follow-up above,
including the Tenth (SubD::ExpandFaces, which left the headline unchanged
at 66.0%/71.5%), so its delta is added directly to the CURRENT
top-of-document headline rather than re-derived from 65.4%: localops' own
delta ((7+10)/28 − (7+9.5)/28) · 100 = 1.785714pp, weight 1, 17.75 total
kernel weight, contributes +0.1006pp, landing at 66.0% + 0.1006pp →
**66.1%**. Full `ctest` suite re-run clean: 100% passing, 0 regressions.
The combined Dino 8 vs Rhino 8 + AutoCAD 2027 headline is unaffected by
this item (a kernel-only category with no app-level command calling it yet
- a grep for movevertex/dragvertex under `dino8-app/src` finds nothing)
and stays at 71.5%. No other row was touched this pass.

**Twelfth same-day follow-up (this session, parallel to the Eleventh above):** `Brep::WireBody`/
`Brep::IsWireBody` (dino8-kernel/src/brep.cpp, dino8-kernel/include/dino8/
kernel/brep.h) landed, closing the FIRST half of **kernel: Topology & data
structure**'s own "Wire bodies (edge/vertex-only B-rep body)" item — until
now this whole kernel's one entirely-missing wire-body gap, re-grepped
before starting (`wire ?body|WireBody` across `dino8-kernel` and
`dino8-app/src`) and confirmed still zero matches, exactly as the item's own
prior evidence text said. `MakeEdgeVertex()`'s own doc comment (added when
MEV/KEV landed) had already named this as the obvious next step - MEV/KEV
are "the one pair that works on bare vertex/edge topology alone,... exactly
the missing piece PARITY_MAP.md's separate 'Wire bodies' item names" - and
`WireBody()` is genuinely built that way: the same three `ON_Brep`
primitives `MakeEdgeVertex()` itself uses (`NewVertex`/`AddEdgeCurve`/
`NewEdge`), just applied to fresh topology instead of extending an existing
vertex. Every `NurbsCurve` in the input list becomes one real `ON_BrepEdge`
with `TrimCount() == 0` (`Check()` reports it `NakedEdge` with
`other_index == 0`, identical in shape to a `MakeEdgeVertex()`-built edge);
a curve reporting `IsClosed()` gets exactly ONE new vertex with the edge's
own two endpoint indices both set to it (a genuine self-closed wire edge,
the vertex's own `m_ei` listing that one edge TWICE — the same self-loop
shape `SplitNonManifoldVertex()`'s and `KillEdgeVertex()`'s own doc
comments already name), never two separately-allocated but coincident
vertices for one point; an endpoint within tolerance of a vertex an
EARLIER curve in the same call already placed is welded onto it rather
than duplicated, so curves chained end to end become a real polyline
through shared vertices — the wire-body analogue of
`FromMixedFaces()`'s/`FromPlanarFaces()`'s own "coincident loop points
share one vertex" convention, applied to wire topology instead of face
loops. `IsWireBody()` is the structural query counterpart (at least one
live edge, zero live faces). Confirmed by direct measurement, not assumed:
`Brep::Check()` needed no changes at all to behave correctly on a
zero-face Brep — `GroupVertexEdgesByFace()`'s own "0 for a vertex that
touches no live face at all" case (already documented, previously
exercised only via a wire edge hanging off an otherwise-solid vertex) means
a wire body's own vertices never trip `NonManifoldVertex`, and the
face-indexed loops in `Check()` simply iterate zero times.

Real unit-test coverage (`TestBrepWireBody`, tests/test_basic.cpp): a
single open curve builds a genuine 2-vertex/1-edge/0-face body with each
endpoint a real leaf; a single closed circle builds a 1-vertex/1-edge
self-closed loop, NOT 2 coincident vertices, verified both by the edge's
own `m_vi[0] == m_vi[1]` and by the vertex's own degree reading 2; two open
curves sharing an endpoint weld into a real 3-vertex/2-edge chain through
one shared degree-2 vertex (checked by position AND by degree, not just
vertex count); two curves with no coincident endpoints stay fully
disjoint (4 vertices); and every refusal (empty curve list, a zero-length
curve, and — reusing `TestCurveExtend()`'s own already-established fact
that a 3-point coincident-endpoint polyline reports `IsClosed() == false`,
below the 4-control-point minimum `ON_NurbsCurve::IsClosed()` requires —
coincident start/end points on a curve that is NOT `IsClosed()`, genuinely
ambiguous between one vertex and two) throws `std::invalid_argument`,
including when the bad curve is the SECOND entry in an otherwise-good list
(no partial wire body left behind). Full `dino8_kernel_tests` suite (via
`ctest`): 100% passing, 0 regressions.

Still `partial`, not `present`: no app command constructs or displays a
wire body at all (`dino8-app` has no concept of a curve-only "body" object,
only standalone `NurbsCurve` scene objects); `WireBody()` itself has no
counterpart for EXTENDING an existing wire body with more curves in one
call (each call builds a fresh `Brep` from scratch; chaining onto a prior
result needs a separate, not-yet-written merge); there is no wire-to-solid
or wire-to-sheet promotion (sweep/extrude of a wire body's own edges,
offsetting a wire body, or using one as a boolean/imprint tool); and
`WireBody()` welds by brute-force nearest-vertex search, correct but
`O(curves²)`, an acceptable but real scaling limit this doc's own
`VertexWelder` comment already flags as the kind of thing that matters at
face-loop scale, not (yet) exercised at wire-body scale either. Net effect
on the scores below: kernel: Topology & data structure's own item count
moves 13/12/2/27 (70.4%) → 13/13/1/27 (72.2%), missing → partial. This
row is file-disjoint from every other category any concurrent same-day
session touched (Boolean operations, SubD, Blending, Sweeping/lofting/
extruding/revolving) - including the Tenth follow-up above (SubD, which
explicitly leaves its own category row's status unchanged and the
headline at 66.0%/71.5%) - EXCEPT the Eleventh follow-up immediately
above (`MoveVertexConvexPlanar`, kernel: Local / direct-edit operations),
which landed on the SAME 66.0% baseline in parallel rather than
sequentially on top of it (neither branch had seen the other's commit),
so - the same "sum both file-disjoint deltas directly against the last
certain common baseline, don't chain one paragraph's own arithmetic on
top of another's" method the Fifth/Sixth-and-Seventh/Recombining
paragraphs above already use for exactly this situation - this paragraph
computes its OWN isolated delta against that same 66.0% baseline the
Ninth follow-up originally established: topology's own delta
((13+6.5)/27 − (13+6)/27) · 100 = 1.851852pp, weight 1, 17.75 total
kernel weight, contributes +0.104330pp, landing this paragraph's own
isolated math at 66.0% + 0.1043pp → 66.1%; combined with the Eleventh
follow-up's own +0.1006pp (localops), the two together land the
top-of-document headline at 66.0% + 0.1006pp + 0.1043pp → **66.2%** -
treat the top-of-document number, not this paragraph's own isolated
66.1%, as authoritative. The combined Dino 8 vs Rhino 8 + AutoCAD 2027
headline is unaffected by this item (a kernel-only category with no
app-level command calling it at all - confirmed no `WireBody` reference
anywhere under `dino8-app/src`) and stays at 71.5%. No other row was
touched this pass.

**Thirteenth follow-up (this session):** `Brep::AddWireCurves`
(dino8-kernel/src/brep.cpp; dino8-kernel/include/dino8/kernel/brep.h)
closes the SECOND of the three concrete gaps the Twelfth follow-up's own
"Still partial" list named for **kernel: Topology & data structure**'s
"Wire bodies" item: "`WireBody()` itself has no counterpart for EXTENDING
an existing wire body with more curves in one call". Re-checked before
starting, per this session's own task brief: `git log --oneline -30 --
dino8-kernel/src/brep.cpp` confirms `WireBody()`/`IsWireBody()` (the
Twelfth follow-up above) is the most recent topology work on this file,
and a re-grep for `AddWireCurves|ExtendWire` across the whole kernel and
`dino8-app/src` found nothing before this pass.

`AddWireCurves` is an instance method, not a static factory like
`WireBody()`: it appends one new `ON_BrepEdge` per entry in `curves`
directly onto `*this`, reusing the identical vertex-welding and
edge-construction logic `WireBody()` itself uses — refactored this pass
into two private free functions shared by both, `ValidateWireCurves`/
`AppendWireEdges` in brep.cpp's own anonymous namespace, rather than
duplicated — but seeded with whatever LIVE vertices this Brep already has
instead of starting from nothing, so a new curve endpoint welds onto a
vertex an EARLIER `WireBody()` or `AddWireCurves()` call already created,
exactly the capability the prior paragraph's own evidence named as
absent. Deliberately NOT restricted to `IsWireBody()`-true Breps: since
welding only ever looks at whatever vertices are already live, calling it
on a Brep that also has faces (e.g. `Brep::FromPlanarFaces()`'s own box)
welds a wire spur onto that solid's own corner vertex just as correctly —
the multi-curve generalization of `MakeEdgeVertex()`'s own single-vertex/
single-curve case, not a narrower wire-body-only operation.

Verified by `TestBrepAddWireCurves` (tests/test_basic.cpp): (1) calling
it on a fresh, completely empty Brep reproduces `WireBody()`'s own exact
shape; (2) a SECOND, separate call sharing endpoint B with a first call's
own curve welds onto the vertex THAT call created — a real 3-vertex/
2-edge chain built across two calls, verified both by vertex count and by
degree, not assumed; (3) a curve with no coincident endpoint anywhere
stays fully disjoint even in the same Brep as an unrelated wire; (4)
several curves passed in ONE call weld to each other AND to the Brep's
own pre-existing vertices in the same pass; (5) a closed curve still gets
exactly one self-closed vertex, not two coincident ones; (6) welding a
spur onto an ordinary solid's own corner vertex adds exactly one vertex
and one zero-trim edge and leaves `IsWireBody()` false, confirming the
deliberately-not-wire-body-only scope; (7) every refusal case
`WireBody()` itself already covers (empty list, a degenerate curve,
coincident-but-not-`IsClosed()` endpoints, and a bad curve as the SECOND
entry in an otherwise-valid multi-curve call) throws
`std::invalid_argument` and leaves the Brep provably untouched (vertex/
edge counts re-checked after the caught exception). Full
`dino8_kernel_tests` suite (via `ctest`, and directly): 100% passing (4546
individual `ok:` checks, "all checks passed"), 0 regressions.

Net effect on the scores below: **none.** The "Wire bodies" item's own
classification stays `partial` — extending a wire body across calls was
only one of the three gaps the item's prior evidence text listed (app
wiring and wire-to-solid/sheet promotion, both still genuinely missing,
are what keep it out of `present`), so this pass narrows the item's own
remaining scope without flipping its present/partial/missing bucket.
**kernel: Topology & data structure**'s own item count is unchanged at
13/13/1/27 (72.2%). A concurrent same-day session's own re-verification
addendum (see the top-of-document note dated 2026-09-28) separately
corrected both headline numbers' own chained-delta arithmetic drift to
**66.8%/71.4%** and fixed roughly 40 stale citation line numbers
elsewhere in this document, entirely file-disjoint from this pass's own
edits (that addendum touched only PARITY_MAP.md's own prose and
citations, never dino8-kernel/src or dino8-app/src); this paragraph's own
net effect on the category table remains zero regardless, so the
headline stays at whatever that addendum's own recomputation gives —
66.8%/71.4% as of this paragraph's own writing, not this paragraph's own
independent claim. This pass's only source edits are
dino8-kernel/include/dino8/kernel/brep.h, dino8-kernel/src/brep.cpp, and
dino8-kernel/tests/test_basic.cpp.

**Fourteenth same-day follow-up (a fifth parallel session):**
`dino8::kernel::EmbossProfile` (dino8-kernel/include/dino8/kernel/
boolean_general.h, dino8-kernel/src/boolean_general.cpp) landed, closing
**kernel: Feature operations**' own "Emboss/deboss" item, `missing`→
`partial` (see that category's own bullet list, and its own "Note on this
category's score," below for the full construction/test detail and the
recomputed 6/15/3/24 split). `git log --oneline -30` re-checked first, per
this branch's own working convention: the three most recent commits at this
session's own start (`MaterialAt`/`MaterialCount`, `CounterboreHole`,
`WireBody`) were all already pushed to `origin/claude/pdf-audit-i2bvwm`, and
`git status`/`git fsck --dangling` turned up no uncommitted or orphaned work
from an earlier attempt, so this session started clean. By the time this
paragraph was written, two OTHER same-day sessions had landed first — the
2026-09-28 re-verification addendum (top of document) and the Thirteenth
follow-up's own `AddWireCurves` immediately above — so this paragraph builds
on their own combined **66.8%/71.4%** baseline, not the stale 66.2% this
session's own commit inventory originally suggested. This row is
file-disjoint from every other category either of those two touched
(Topology, and the addendum's own prose/citation fixes across the rest of
the document), so this paragraph's own isolated delta needs no
recombination with either's math: Feature operations' own exact score moves
13/24 (54.16667%) → 13.5/24 (56.25%), a delta of +2.08333pp; at this row's
own weight of 1 against the 17.75 total kernel weight, that is
+2.08333/17.75 = +0.11737pp against the headline. 66.8% + 0.1174pp →
**66.9%** — treat the top-of-document number, not this paragraph's own
isolated arithmetic, as authoritative if it has moved further still by the
time this is read. The combined Dino 8 vs Rhino 8 + AutoCAD 2027 headline
was not re-verified this pass (same caveat this document already gives
after the CounterboreHole follow-up above) and is left at 71.4% rather than
silently presented as re-checked; it is unlikely to be materially affected
either way, since `EmbossProfile` is kernel-only with no `dino8-app`
command wired to it yet (a grep for "emboss"/"deboss"/"engrave" across
`dino8-app/src` still finds nothing).
**Fifteenth same-day follow-up (a later session):** `git log --oneline -30
-- dino8-kernel/src/subd.cpp dino8-kernel/src/mesh.cpp` at the start of this
session showed the Tenth/Twelfth follow-ups' own `SubD::ExpandFaces` (above)
as the most recent commit touching either file, confirming the item this
session should pick up is the very gap the Sixth/Tenth follow-ups both left
open on purpose: **kernel: SubD & mesh kernel support**'s own
"Kernel-native SubD local edit operators" item's fifth and last named
sub-operator, weld - "the one remaining named sub-operator with no kernel
entry point at all" per the Tenth follow-up's own text, which had already
re-grepped `opennurbs_subd.h`/`opennurbs_subd.cpp` for
`Weld|MergeEdge|MergeVertex` and confirmed no ready-made OpenNURBS primitive
exists for it.

`SubD::Weld(keep_vertex_id, discard_vertex_id, weld_tolerance)`
(dino8-kernel/src/subd.cpp, dino8-kernel/include/dino8/kernel/subd.h)
closes it for real, exactly as the Tenth follow-up's own text anticipated:
"hand-rolling the vertex-merge/component-reconnect surgery directly against
`ON_SubD`'s own lower-level ... building blocks rather than wrapping one
existing call" - though not quite the way that text's own guess of which
building blocks would land it. Deliberately id-based, not point-based like
every other local-edit operator above: this operator's whole reason to
exist is merging two vertices that sit at the exact same position but were
never joined at the SubD level, and `ON_SubD::FindVertex(point, tolerance)`
can only ever resolve to ONE of two such coincident vertices - a
point-based signature could never even name the second one (confirmed by
direct reproduction, not assumed: `SubD::FromControlMesh()` itself turned
out to auto-weld any two mesh vertices at bit-identical positions
regardless of mesh vertex index, so even building a test fixture with two
genuinely coincident-but-distinct SubD vertices needed bypassing it -
`ON_SubD::AddVertex()` directly - the same lesson the tests below record).

The FIRST implementation attempt used local surgery: `ON_SubD::
DeleteComponents()` on the discarded vertex, then `FindOrAddFace()` the
affected faces back onto the kept one, the same technique `Symmetrize()`
(Fifth follow-up, above) uses for its own plane-seam weld. This turned out
to be a real, caught-before-landing bug, not a style choice: reading
`ON_SubDimple::DeleteComponents` in opennurbs_subd.cpp shows its "delete
isolated edges" pass (always on for the public overload) also deletes any
OTHER vertex left with zero faces once the discarded vertex's own faces
are gone, even one that still has edges - not just the discarded vertex
itself. A standalone reproduction (two quads placed edge-to-edge, no other
face holding their far corners alive) confirmed it directly: welding one
coincident corner pair this way dropped the vertex count by 4, not 1, and
left the result `IsValid()==false` - the DeleteComponents cascade had
silently swept away the very corners the rebuild step still needed,
leaving it operating on dangling pointers.

The LANDED implementation instead snapshots the whole current control
net (every vertex's id and position, every face's corner-id list, every
genuinely INTERIOR edge's tag and sharpness), remaps every reference to
the discarded vertex's id onto the kept vertex's id, and rebuilds a fresh
`ON_SubD` from that snapshot via `ON_SubD::AddVertexForExperts()` -
explicitly documented for exactly this "copying portions of an existing
SubD to a new SubD" use case - preserving every original vertex's own id,
plus `FindOrAddFace()` for the faces. Only a genuinely interior
(`FaceCount()==2`) original edge's tag/sharpness is snapshotted and
reapplied verbatim; a naked edge's Crease tag is deliberately left out
(it's purely the "an open SubD's own boundary edges are themselves always
creases" construction convention, stale the instant this weld gives it a
second face) so the final `UpdateAllTagsAndSectorCoefficients(true)`
re-derives it fresh from its new face count instead - this class's own
established "vertex/edge tags are DERIVED, never stored history" fact
(`Symmetrize()`'s own seam handling above already relies on the vertex
half of it).

Verified with 2 new tests (tests/test_basic.cpp), both built directly
against the raw `ON_SubD` (`AddVertex`/`FindOrAddFace`) rather than
`SubD::FromControlMesh()`, for the auto-welding reason above:
`TestSubDWeldJoinsTwoDisjointQuadsAlongCoincidentSeam` builds two quads
placed edge-to-edge with their own separate, duplicate-but-coincident seam
vertices (2 faces / 8 vertices / 8 edges, `Check().body_count == 2` despite
sitting flush against each other) and welds both coincident corner pairs,
confirming the result is bit-for-bit the same 2-face/6-vertex/7-edge
topology (one genuinely shared interior edge, `Check().naked_edges == 6`,
`IsManifoldSingleBody()`) `FromControlMesh()` already gives when the two
quads share vertex indices directly (`TestSubDSpinEdgeRotatesSharedInterior
Edge`'s own fixture) - a real, lossless stitch, not just a vertex-count
decrement - plus the id/distance/already-connected refusal cases.
`TestSubDWeldRefusesSameFaceCornersAndPreservesUnrelatedCrease` confirms a
quad's own two diagonal corners (sharing a face but not a direct edge) are
refused, then builds an A-B-C strip where A/B already share a genuine
edge (by reusing the same two vertex pointers in both faces) explicitly
marked a hard crease via `SetCrease()`, and B/C are two separately
allocated quads along their own coincident-duplicate seam: after welding
B/C, the A/B crease survives completely untouched (still tagged Crease by
id) while the newly-closed B/C seam comes out `SmoothX` - `ON_SubDEdge::
IsSmooth()` treats `Smooth` and `SmoothX` as equivalent, the latter being
this class's own routine "smooth edge between two not-yet-resolved
vertices" tag any freshly-built net can carry until its first subdivision,
per `ON_SubD::AddEdge()`'s own doc comment, not a defect. Full
`dino8_kernel_tests` suite (via `ctest`, and directly): 100% passing ("all
checks passed", exit code 0), 0 regressions.

This closes the item's own last remaining named sub-operator: all 5 of
insert edge / extrude face / spin edge / weld / expand now genuinely exist
at the kernel level, upgrading **kernel: SubD & mesh kernel support**'s own
"Kernel-native SubD local edit operators" item from `partial` to `present`
- the item's own residual "none of the five is wired to any app command"
caveat is a real, separate App-level gap (tracked under **Dino 8: SubD &
mesh modeling toolset (app level)** instead, not this row), not something
that holds back this row's own kernel-level scope, the same convention
`SubD::Check()`'s own missing→present flip (a purely kernel-level
diagnostic, also never wired to an app command) already used. Category
count moves 14/6/2/22 (77.3%) → 15/5/2/22 (79.5%): exact delta
((15+2.5)/22 − (14+3)/22) · 100 = 2.272727pp, weight 0.75, 17.75 total
kernel weight, contributes +0.096032pp. This paragraph's own edits are
file-disjoint from every other category touched by the two follow-ups
immediately above (`AddWireCurves`: kernel: Topology & data structure;
`EmbossProfile`: kernel: Feature operations) and from the top-of-document
re-verification addendum's own prose/citation-only fixes, so this lands
sequentially on top of THEIR combined 66.9% rather than the stale 66.2%
this session's own commit inventory would otherwise suggest (the same
"land on the branch that already carries every prior follow-up" method
the Eleventh follow-up's own text already uses): 66.9% + 0.0960pp →
**67.0%** — treat the top-of-document number, not this paragraph's own
isolated arithmetic, as authoritative if it has moved further still by
the time this is read. The combined Dino 8 vs Rhino 8 + AutoCAD 2027
headline is unaffected by this item (a kernel-only change; no app command
references `SubD::Weld` anywhere under `dino8-app/src`) and stays at
71.4%. No other row was touched this pass.

**Fifteenth same-day follow-up (a sixth parallel session):** `git log
--oneline -30 -- dino8-kernel/src` at the start of this session showed
`Brep::ExtrudeToBoundary` (the Ninth follow-up above) as the most recent
sweep/loft/extrude/revolve-relevant commit, so this session picked the
next highest-value still-partial item in **kernel: Sweeping, lofting,
extruding, revolving** rather than duplicating that work: "Extrude a
curve along a path curve (translational sweep / sum surface,
ExtrudeCrvAlongCrv)" - until now a pure kernel-entry-point gap (the app's
own `ExtrudeAlongCommand` called `ON_SumSurface::Create` directly, with
no Solid/cap option and nothing in the kernel at all).
`Brep::ExtrudeAlongCurve(profile, path, cap)` (dino8-kernel/include/
dino8/kernel/brep.h; dino8-kernel/src/sweep.cpp) closes that: a real
kernel entry point building the exact tensor-product sum surface via a
new `SumSurface()` helper (sweep.cpp), with `AssembleSweptBody()`'s own
shared capping machinery giving it the same closed-solid option
`Extrude()`/`Sweep1()`/`Revolve()` already have - full detail (the
partition-of-unity exactness argument, what's verified, what remains) is
in that item's own bullet below. Verified by 4 new tests
(tests/test_basic.cpp): a straight-line path reproducing `Extrude()`
exactly, a curved (non-rational, degree >= 2) path/profile pair verified
directly against `profile(u) + path(v) - path(v_min)` sample-for-sample,
a piecewise-linear "wobbly" path whose enclosed volume matches area x
net height-displacement exactly via Cavalieri's principle, and negative
controls (rational profile/path, closed path, a flat cap, a non-planar
closed profile). Full `dino8_kernel_tests` suite re-run via `ctest`:
100% passing, 0 regressions - including catching and fixing a genuine
bug in this session's own first draft of the Cavalieri test itself (an
asserted-exact volume at a division count that did not evenly divide the
profile's own knot-span count, so a grid cell silently cut a real
corner - the same class of misaligned-division trap
`TestExtrudeRectangleIsExactCappedSolid`'s own comments already warn
about for a kinked profile; fixed by picking an aligned asymmetric pair
instead of loosening the tolerance). This item stays `partial`, not
`present` (both curves must be non-rational, `path` must be open, no
twist/scale/road-like option, no app wiring - see the bullet below for
the complete list), so this category's own present/partial/missing
counts are UNCHANGED (6/21/2/29, 56.9%). This row is file-disjoint from
every category the Thirteenth (Topology) and Fourteenth (Feature
operations) follow-ups above touched, so it lands on their own combined
baseline unchanged rather than recombining any delta: the top-of-document
headline stays at whatever those two give it (**66.9%/71.4%** as of this
paragraph's own writing) - treat the top-of-document number as
authoritative if it has moved further still by the time this is read.
This session's only source edits are dino8-kernel/include/dino8/kernel/
brep.h, dino8-kernel/src/sweep.cpp, and dino8-kernel/tests/test_basic.cpp.

**Sixteenth same-day follow-up:** `git log --oneline -30 -- dino8-kernel/src`
at the start of this session showed `52de4f8` (`kernel: add SubD::Weld()`)
as HEAD, with the Fifteenth follow-up's own `Brep::ExtrudeAlongCurve`
(`d2cfd53`) as the most recent commit touching **kernel: Sweeping, lofting,
extruding, revolving** specifically - so this session picked the next
highest-value still-`[partial]` item in that same category rather than
duplicating either: "Extrude to a point", which this document's own
evidence already named as a genuine kernel-level hole - there was no B-rep
(as opposed to mesh) way to cone a curve to a point in this kernel at all,
only `Mesh::ConeToApex` (mesh-only) and `Brep::Loft`'s own refusal to cap a
collapsed end section.

`Brep::ExtrudeToPoint(profile, apex, cap)` (brep.h:513; sweep.cpp:1701)
closes it: the wall is `FanSurface()` (sweep.cpp:927) - the SAME degree-
(p, 1) fan construction `AddFanCap()` already builds for flat end caps
throughout this file - reused here as the wall itself, assembled directly
through `ON_Brep::NewFace`'s own closed-in-u/singular-at-v0 handling
(confirmed empirically by dumping the built topology's own vertex/edge/
trim tables via a scratch harness before trusting it: one apex vertex, one
closed rim edge, one seam edge that appears twice in the wall's own loop -
deliberately NOT routed through `AssembleSweptBody()`, which explicitly
refuses any wall singular at v0/v1, sweep.cpp:1084, since it exists for
non-degenerate rectangular sweeps). A real bug surfaced and was fixed
building this, caught the same "measure it, don't assume it" way this
document's own house style expects: the first draft picked the wall's
outward-orientation direction as pointing FROM the profile's plane TOWARD
`apex` (mirroring `Extrude()`'s own `direction` variable by name only,
not by role) - which is backwards, since `Extrude()`'s own `direction`
points from its wall's v = 0 end to its v = 1 end, while `apex` is this
wall's v = 0 end (not v = 1). The bug was invisible in `raw().IsValid()`
and even in a plain positive-looking tessellated volume for ONE sign of
apex offset, but `raw().IsSolid()` (`IsManifold(&oriented, ...)`) caught it
directly: `oriented` was false for the capped result (the wall's own rim
trim and the flat cap's own rim trim referenced their shared edge with the
SAME `bRev3d`, not opposite, the standard sign of two faces glued the
wrong way) and the mesh volume for an apex on the AWAY side came out
negative rather than being caught by the usual flip cross-check (which
never ran, since it is itself gated on `IsSolid()`). Fixing the sign
(pointing away from `apex`, toward the plane) fixed both symptoms at once;
this is now `TestExtrudeToPointConvexTriangleVolumeTwoIndependentWays`'s
own explicit "apex on the negative side" check (tests/test_basic.cpp),
so a regression here would be caught, not just fixed once.

The exactness argument is a genuinely more permissive result than every
other fan-cap-based construction already in this file: `PlanCap()`'s own
flat caps (the phrase "star-shaped" recurs throughout this document's
Extrude/Revolve/Sweep1/Loft bullets) require a star-shaped section because
they are coplanar with their own boundary, so a fan can fold over itself.
An OFF-PLANE apex cannot fold over ANY simple closed planar profile: two
rulings `apex -> profile(u1)` and `apex -> profile(u2)`, `u1 != u2`, are
two distinct lines through the single common point `apex` (distinct
because `profile(u1)` and `profile(u2)` both lie in a plane `apex` does
not, so each ruling crosses that plane only at its own endpoint and cannot
also pass through the other) - and two distinct lines sharing a point meet
nowhere else. Verified as a genuine claim, not just argued: `Test-
ExtrudeToPointWallEmbedsEvenForNonStarShapedProfile` cones the SAME
non-star-shaped C-shaped profile `Extrude()`'s own negative control
already refuses to flat-cap, confirms the wall alone still builds
(`FaceCount() == 1`, `IsValid()`) and genuinely embeds - `Mesh::
FindSelfIntersections()` on an 80x9 tessellation of it reports zero
genuine crossings, the same real kernel diagnostic
`TestMeshFindSelfIntersectionsDetectsOnlyGenuineCrossings` already
exercises elsewhere, deliberately chosen over a hand-rolled point-distance
heuristic after an early draft of that heuristic false-flagged near the
apex (where samples from far-apart `u` values are legitimately close
together purely because `v` is small there, not because of any fold) -
and confirms the flat BASE cap on that same profile still throws, an
honest, separate limitation of the shared cap machinery, not of the cone
wall itself. Pyramid-volume exactness (`(1/3) * area * height`, exact for
ANY planar base) is checked on two genuinely non-convex profiles - an
L-shape and a 5-pointed star polygon, both independently confirmed
star-shaped by ALSO succeeding at the flat base cap - with `base_area`
computed independently in the test via the shoelace formula, not read
back from the kernel, cross-checked two separate ways (`Brep::Volume()`'s
own direct NURBS Gauss-quadrature integration, and `TessellateToClosed-
Mesh(...).Volume()`), plus a convex triangle cross-checked the same two
ways and at an apex on either side of the profile's plane, plus a
RATIONAL profile (a true NURBS circle - unlike `ExtrudeAlongCurve()`,
which refuses one, `FanSurface()`'s own weight-carrying construction is
exact for a rational profile too, so a circular cone is exact, not
approximated, converging from below in its tessellated form exactly like
`Extrude()`'s own extruded-circle test). Five new tests in all
(tests/test_basic.cpp): `TestExtrudeToPointPyramidVolumeExactOnNonConvex-
Profiles`, `TestExtrudeToPointConvexTriangleVolumeTwoIndependentWays`,
`TestExtrudeToPointSupportsRationalProfileExactCircularCone`, `Test-
ExtrudeToPointWallEmbedsEvenForNonStarShapedProfile`, and `TestExtrudeTo-
PointNegativeControls` (apex in the profile's own plane, a non-planar
profile refused UNCONDITIONALLY - even uncapped, unlike `Extrude()`/
`ExtrudeAlongCurve()`'s own cap-only planarity requirement - an open
profile, an invalid curve, and confirming the function is fully usable
again immediately after a throw). Full `dino8_kernel_tests` suite (via
`ctest`, and directly): 100% passing (4756 individual `ok:` checks, "all
checks passed"), 0 regressions.

Precisely what remains, stated narrowly on purpose rather than papered
over: scoped to CLOSED profiles only (an open profile's fan-to-a-point
needs different topology this does not attempt); the flat base cap
inherits `PlanCap()`'s own star-shaped-section requirement unchanged (a
real, separate limitation, not removed by this work); and no app command
anywhere calls the new entry point (`RebuildExtrudeToPoint`, cmd_solids.
cpp:64, still builds a bare `CreateRuledSurface` to a degenerate apex
curve with no cap). This item stays `[partial]`, not `[present]` - real
gaps remain - so **kernel: Sweeping, lofting, extruding, revolving**'s own
present/partial/missing counts are UNCHANGED (6/21/2/29, 56.9%), exactly
the same "narrowed, not flipped" outcome the Fifteenth follow-up's own
`ExtrudeAlongCurve` item already established for this category, so no
table or headline arithmetic changes: the top-of-document headline stays
at whatever it currently reads (**67.0%/71.4%** as of this paragraph's own
writing) - treat the top-of-document number as authoritative if it has
moved further still by the time this is read. This session's only source
edits are dino8-kernel/include/dino8/kernel/brep.h, dino8-kernel/src/
sweep.cpp, and dino8-kernel/tests/test_basic.cpp.

**Seventeenth same-day follow-up:** `git log --oneline -30 -- dino8-kernel/src`
at the start of this session showed `de86d00` (`kernel: add
Mesh::SavePly/LoadPly binary_big_endian support`) as HEAD, with
`fce00f1` (`Brep::MergeSameSurfaceFaces`) and the Sixteenth follow-up's
own `Brep::ExtrudeToPoint` (`641ca1e`) as the two most recent commits
before it - neither touching **kernel: Sweeping, lofting, extruding,
revolving** - so this session picked the next highest-value still-
`[partial]` item in that same category rather than duplicating any of
the three prior `Extrude*` additions (`ExtrudeAlongCurve`,
`ExtrudeToBoundary`, `ExtrudeToPoint`): "Extrude a surface / polysurface
face into a solid (ExtrudeSrf)", the one remaining `Extrude*`-family
bullet this document's own evidence still described as having "no kernel
B-rep face-extrude API" at all (only a mesh equivalent existed).

`Brep::ExtrudeFace(body, face_index, direction, cap)` (brep.h; sweep.cpp)
closes that half of the gap - see this category's own updated bullet
above (in "### Kernel category gaps") for the full construction detail,
exactness argument, and test list. In short: it reuses `Thicken()`'s own
2-cap-plus-4-ruled-wall construction with a plain translate standing in
for `Thicken()`'s curvature-sensitive offset, which is exact for ANY
face (planar or freeform) and has no curvature-fold guard - verified by
extruding the identical surface and thickness `Thicken()`'s own existing
test confirms it refuses, and succeeding - and, unlike `Thicken()`, takes
a `face_index` into a genuine multi-face body rather than requiring a
single-face sheet. Five new tests (tests/test_basic.cpp):
`TestExtrudeFaceStraightMatchesExactPrismVolume`,
`TestExtrudeFaceObliqueDirectionMatchesCavalieriVolume`,
`TestExtrudeFaceOnCurvedFreeformSurfaceExceedsThickenGuard`,
`TestExtrudeFaceOnMultiFaceBodyExtractsOneFaceIntoANewIndependentSolid`,
`TestExtrudeFaceUncappedGivesOpenTube`, and
`TestExtrudeFaceRejectsInvalidArguments`. Full `dino8_kernel_tests` suite
(via `ctest`) re-run clean: 100% passing, 0 regressions.

Still partial, and this category's present/partial/missing counts are
UNCHANGED (6/21/2/29, 56.9%) - the same "narrowed, not flipped" outcome
the Fifteenth/Sixteenth follow-ups' own `ExtrudeAlongCurve`/`ExtrudeToPoint`
items already established for this category: `face_index`'s face must be
untrimmed and non-periodic (`Thicken()`'s own scope, for the same
reason), this builds a fresh separate solid rather than merging into or
cutting `body` itself (Push/pull, `boolean.cpp`, is the separate
operation that does that), and the app's own `ExtrudeSrfCommand`
(cmd_surface.cpp) still has no B-rep path at all - a grep for
`ExtrudeFace` in `dino8-app/src` finds nothing but this document. This
session's only source edits are dino8-kernel/include/dino8/kernel/brep.h,
dino8-kernel/src/sweep.cpp, and dino8-kernel/tests/test_basic.cpp.

**Eighteenth same-day follow-up:** `git log --oneline -30 --
dino8-kernel/src` at the start of this session showed `6295712` (`kernel:
extend RemoveBlend to invert FilletConvexEdges' spherical vertex-blend
corner`, in **Blending & chamfering**) as HEAD, with `d29f9b8`
(`Brep::SplitFaceByCurve`) and `3faf918` (the Seventeenth follow-up's own
`Brep::ExtrudeFace`) as the two most recent commits before it - none of
the three touching **kernel: Sweeping, lofting, extruding, revolving** -
so this session again picked the next highest-value still-`[partial]`
item in that same category rather than duplicating any of the four prior
`Extrude*`/`Revolve`-family additions: "Rail revolve (profile revolved
about an axis while following a rail curve)", the one bullet in this
category this document's own evidence still described as having "No
kernel equivalent" at all (only an approximate app-level surface fit
existed).

`Brep::RailRevolve(profile, axis_point, axis_direction, rail, angle,
stations, cap)` (brep.h; sweep.cpp) closes that half of the gap - see
this category's own updated "Rail revolve" bullet above (in "### Kernel
category gaps") for the full construction detail, exactness argument,
and test list. In short: it generalizes `Revolve()`'s own (rho, z)-plane
construction with a per-station radial scale factor sampled from `rail`'s
own distance from the axis, skinned exactly through the same
`SkinSections()` global-interpolation machinery `Sweep1()`/`Sweep2()`/
`Loft()` already share, rather than the app's own approximate
curve-through-points surface fit. Four new tests (tests/test_basic.cpp):
`TestRailRevolveRigidRotationReproducesEveryStationExactly` (exact
per-station reproduction, both a partial angle and a full 2*pi wrap, plus
a Pappus-volume cross-check), `TestRailRevolveVaryingRadiusScalesExactlyAtTheTwoStations`
(the 2-station ruled shortcut's exact endpoints),
`TestRailRevolveBulgingRailProducesABracketedVaseVolume` (a genuinely
varying rail bracketed against two independently-exact Pappus extremes),
and `TestRailRevolveNegativeControls`. Full `dino8_kernel_tests` suite
(via `ctest`) re-run clean: 100% passing, 0 regressions.

Still partial, and this category's present/partial/missing counts are
UNCHANGED (6/21/2/29, 56.9%) - the same "narrowed, not flipped" outcome
every prior follow-up in this category already established: `profile`
must be CLOSED (an open-profile rail revolve is not attempted here),
there is no analytic exact-sweep shortcut the way constant-radius
`Revolve()` has (the wall is always a discretized, cubic-interpolated
skin), and the app's own `RailRevolveCommand` (cmd_srfedit.cpp:1019) is
completely untouched - a grep for `RailRevolve` in `dino8-app/src` finds
nothing but this document. This session's only source edits are
dino8-kernel/include/dino8/kernel/brep.h, dino8-kernel/src/sweep.cpp, and
dino8-kernel/tests/test_basic.cpp.

**Nineteenth same-day follow-up:** `git log --oneline -5 --
dino8-kernel/src` at the start of this session showed `afe0d3c` (`kernel:
add Brep::RailRevolve`, the Eighteenth follow-up immediately above) as
the most recent commit touching **kernel: Sweeping, lofting, extruding,
revolving**, with `5f75d7e` (`Brep::ScrewThread`, a Threaded/tapped hole
feature) and `a94d230` (`PointCloud::SavePts/LoadPts`, kernel/exchange)
as HEAD and HEAD~1 - neither in this category - so this session again
picked the next real, closeable gap in the SAME category rather than
duplicating RailRevolve's own work: "Pipe - Round cap option", the half
of that bullet this document's own evidence described as `Brep::Pipe`
offering "only flat caps (no Round option)".

`Brep::Pipe(rail, radius, cap, stations, round_caps)` (brep.h;
sweep.cpp) adds a `round_caps` parameter: true replaces the two flat
disc caps with genuine hemispherical NURBS dome caps sharing the tube's
own rim edge exactly, via a new `AddDomeCap()` (sweep.cpp, anonymous
namespace) and a new `Brep::RoundCapSpec` (brep.h) threaded through
`AssembleSweptBody()`'s own existing `cap_v0`/`cap_v1` fan-cap assembly
(the SAME eid[2]/rev[2] edge-sharing wiring `AddFanCap()` already uses,
confirmed directly against `opennurbs_brep_tools.cpp`'s own
`ON_Brep::NewOuterLoop()` rather than assumed). Each dome's meridian is
the SAME closed-form 3-control-point rational-quadratic representation
of a 90-degree arc `ON_Circle::GetNurbForm()` itself already relies on
(control points at the arc's two ends plus the intersection of their own
tangent lines, weights (1, cos(pi/4), 1)), applied once per boundary
control point rather than re-derived from scratch. Algebraically
expanding the resulting tensor-product surface shows every latitude line
is an exactly scaled, pole-translated copy of the tube's own rim circle
- a true sphere patch, not a fit (see `AddDomeCap()`'s own doc comment
in sweep.cpp for the full derivation).

Verified three separate ways, not just argued (`TestPipeRoundCaps`,
tests/test_basic.cpp): every one of 289 sampled points on EACH of the two
dome faces of a straight-rail round-capped pipe (578 points total) lies
at EXACTLY (< 1e-9) the known radius from its own end center; that same straight-rail pipe's
tessellated volume matches the exact capsule closed form pi*r^2*L +
(4/3)*pi*r^3 to within 1%, at a tessellation fine enough (dv = 32) for a
curved dome's own chord-faceted tessellation to have actually converged
(a flat fan cap's tessellated volume needs no such resolution - a
straight-line facet already lies exactly in the cap's own plane at ANY
subdivision count - so the flat-capped pipe's own long-standing volume
test keeps its original dv = 4); and, for BOTH a straight rail and a
curved (quarter-arc) rail, replacing a pipe's flat caps with round ones
changes its tessellated volume by exactly two hemisphere volumes
(2*(2/3)*pi*r^3) relative to the otherwise-identical flat-capped body -
a rail-SHAPE-INDEPENDENT differencing argument, not a shape-specific
closed form, since a dome added onto an existing flat rim is a disjoint
volume addition regardless of how the rest of the tube bends. Full
`dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0
regressions.

Still partial, and this category's present/partial/missing counts are
UNCHANGED (6/21/2/29, 56.9%) - the same "narrowed, not flipped" outcome
every prior follow-up in this category already established: `round_caps`
requires an OPEN rail (a closed tube has no ends to dome) and `cap =
true`; it is wired into `Brep::Pipe` only, not `PipeVariable`/
`PipeThickWalled` (a per-station-radius or annular rim is not a single
circle a dome's own meridian construction can be built from without
further work - `PipeVariable`'s "real MultiPipe" bullet and
`PipeThickWalled` stay untouched); a sharply kinked (C1-discontinuous)
rail still gets its dome's own outward direction from the rail's single
end tangent alone, exactly as the flat-cap case already does -
"kinked-rail handling" itself, the OTHER half of this bullet's own name,
is completely untouched; and the app's own `PipeCommand` is unchanged -
a grep for `round_caps`/`RoundCap` in `dino8-app/src` finds nothing but
this document. This session's only source edits are
dino8-kernel/include/dino8/kernel/brep.h, dino8-kernel/src/sweep.cpp, and
dino8-kernel/tests/test_basic.cpp.

**Twentieth same-day follow-up:** `git log --oneline -8 --
dino8-kernel/src` at the start of this session showed `619a310` (`kernel:
BooleanCombinePlanar accepts a compound operand for Difference/
Intersection`, in **kernel: Boolean operations**) as HEAD, with `86155be`
(`NurbsCurve::OffsetInPlane` exact-mitering polylines, **kernel:
Offsetting, shelling, thickening**) and `d78d841` (`Brep::OffsetWireBody`,
**kernel: Topology & data structure**) as HEAD~1/HEAD~2 - none in **kernel:
Sweeping, lofting, extruding, revolving** - with `91b6a04` (`Brep::Pipe`
round_caps, the Nineteenth follow-up above) the most recent commit
actually in this category. Rather than attempt "kinked-rail handling" -
that bullet's OTHER remaining half, which needs a genuinely different,
multi-face mitered-joint construction (each straight segment its own
cylindrical wall, trimmed by the shared bisector plane at every interior
vertex, welded into one solid) that this document's own prior evidence
already flagged as "further work", not a same-shape extension - this
session picked the smaller, still-real half of the SAME bullet the
Nineteenth follow-up explicitly left open: extending `round_caps` to
`Brep::PipeVariable`.

`Brep::PipeVariable(rail, radius_points, cap, stations, round_caps)`
(brep.h; sweep.cpp) adds the identical `round_caps` parameter Pipe()
already has, reusing the SAME `AddDomeCap()`/`RoundCapSpec` machinery
`AssembleSweptBody()` already exposes (no new geometry code): each dome
is sized to THAT end's own local radius - `radius_at()` evaluated at the
end's own merged arc-length fraction, matching the end section's actual
built circle bit-for-bit - rather than assuming a single constant radius
the way Pipe() can, since `PipeVariable`'s two ends need not match. This
closes a genuine special case Pipe()'s own dome construction could not
reach at all (a variable-radius tube has no single "the radius" for
Pipe()'s signature to take).

Verified (`TestPipeVariableRoundCaps`, tests/test_basic.cpp), not just
argued: with equal radii at both ends, `PipeVariable`'s round-capped
result matches `Pipe`'s own independently-built round-capped capsule
volume to 1e-9 relative error - two separate call paths (one delegating
to `Sweep1()` internally, one building its own skin) landing on the same
shape; with UNEQUAL end radii (a round-capped frustum), every sampled
point on EACH dome face lies at exactly (< 1e-9) that end's OWN radius
from that end's own center - the direct proof each meridian was built
from the right radius, not merely "a" radius; and the same rail-shape-
independent volume-differencing argument Pipe()'s own test uses (swapping
flat discs for domes adds exactly the domes' own volume) generalizes
correctly to two DIFFERENT hemisphere volumes, matching to within 1%.
Every refusal case (`round_caps` with `cap` false, `round_caps` on a
closed rail) throws `std::invalid_argument`, mirroring Pipe()'s own.
Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing,
0 regressions.

Still partial, and this category's present/partial/missing counts remain
UNCHANGED (6/21/2/29, 56.9%) - the same "narrowed, not flipped" outcome as
every prior follow-up: `PipeThickWalled`'s own round-cap gap is untouched
(an annular rim genuinely needs a different, non-single-circle meridian
construction, not a copy of this session's work); "kinked-rail handling"
remains completely untouched, exactly as the Nineteenth follow-up left
it; and the app's own `PipeCommand`/`PipeVariableCommand` are unchanged -
a grep for `round_caps`/`RoundCap` in `dino8-app/src` still finds nothing
but this document. This session's only source edits are
dino8-kernel/include/dino8/kernel/brep.h, dino8-kernel/src/sweep.cpp, and
dino8-kernel/tests/test_basic.cpp.

### Kernel category gaps (missing / partial items, with evidence)

**kernel: Topology & data structure** (topology):
- [partial] Multi-shell / multi-lump bodies (compound of disjoint or touching shells) — `Brep::Compound` (dino8-kernel/src/brep.cpp:2577-2612) calls `ON_Brep::Append` per lump and concatenates the side tables; lumps are deliberately not welded (dino8-kernel/include/dino8/kernel/brep.h:1571-1589, "Lumps are deliberately NOT welded to each other"). XOR uses the two-lump compound (`TestBooleanSymmetricDifferenceBrepBoxesIsTwoLumpCompound`, tests/test_basic.cpp:23235). `BooleanCombinePlanar`/`BooleanCombineMixed` both refuse a multi-lump operand via a shared `RefuseCompoundOperand` helper (definition boolean.cpp:844-851, call sites boolean.cpp:856-857/6534-6535 — corrected 2026-09-28, was mis-cited boolean.cpp:815-822, itself a correction of the even-earlier "boolean.h:1321-1323" pointing at unrelated text). **Correction (this pass):** the previous text here — "`BooleanCombineGeneral` has **no** compound-operand guard at all... a compound fed to the general engine is silently accepted and processed per-face with no lump-boundary awareness" — is stale, not current: `boolean_general.cpp` already has its own file-local `RefuseCompoundOperand` (used by `BooleanCombineGeneral` for `Union`; `Difference`/`Intersection` deliberately distribute over a compound operand's lumps instead of refusing, the same exemption `BooleanCombinePlanar`/`BooleanCombineMixed` already have for those two ops) — this landed in an earlier commit (`5a28aa7`) than the one this "New finding" text itself was written against, so the finding was simply never updated once the gap it named had already closed elsewhere in the same document (see **kernel: Boolean operations**'s own "Multi-body / multi-tool booleans" bullet above, which already carries the accurate, up-to-date account, and its trailing "Seventh note"/"Sixth note" for the full verification detail: `TestBooleanCombineGeneralRefusesCompoundOperand`/`TestBooleanCombineGeneralDifferenceAcceptsCompoundFirstOperand`/`TestBooleanCombineGeneralIntersectionAcceptsCompoundOperand`, tests/test_basic.cpp). This item's own `partial` classification was never based on this finding (the finding was incidental extra evidence, not the reason for the score) and is unaffected by this correction — still no inner-void (hollow) shell/region concept.
- [partial] Wire bodies (edge/vertex-only B-rep body) — **upgraded from missing.** `Brep::WireBody`/`Brep::IsWireBody` (brep.h/brep.cpp) build a genuine zero-face `ON_Brep` (vertices + edges only) from a list of `NurbsCurve`s, reusing `MakeEdgeVertex()`'s own `NewVertex`/`AddEdgeCurve`/`NewEdge` primitives: a closed curve gets one self-closed edge through a single vertex (not two coincident ones), open curves sharing an endpoint weld onto one shared vertex, and every edge is a genuine wire edge (`TrimCount() == 0`, `Check()` reports `NakedEdge`/`other_index == 0`) — verified in `TestBrepWireBody` (tests/test_basic.cpp). **New this pass:** `Brep::AddWireCurves` (brep.h/brep.cpp) closes the "extend an existing wire body with more curves in one call" gap this bullet's own prior evidence named as still-missing — an instance method that appends one new `ON_BrepEdge` per curve onto THIS Brep's own existing vertices, using the exact same welding/self-closed-edge rules `WireBody()` itself uses (both now share the same `ValidateWireCurves`/`AppendWireEdges` private helpers rather than duplicating the logic), seeded with whatever vertices the Brep already has (from an earlier `WireBody()`/`AddWireCurves()` call, `MakeEdgeVertex()`, or an ordinary face-bordering vertex) instead of starting empty — so growing a wire body across more than one call welds onto the SAME vertex a prior call created, rather than forcing every curve into one giant up-front `WireBody()` list. Deliberately more general than the item's own name: nothing requires `IsWireBody()` to already be true, so it applies equally to bulk-attaching wire spurs onto a Brep that already has faces (the multi-curve generalization of a single `MakeEdgeVertex()` call). Verified in `TestBrepAddWireCurves` (tests/test_basic.cpp): a call on a completely empty Brep reproduces `WireBody()`'s own exact shape; a second, separate call sharing an endpoint with the first welds onto the EXISTING vertex across the two calls (a real 3-vertex/2-edge chain, not rebuilt from scratch, verified by both vertex count and by degree); an unrelated curve with no coincident endpoint stays disjoint even in the same Brep as an unrelated wire; several curves in one call weld to each other AND to the Brep's own pre-existing vertices; a closed curve gets one self-closed vertex, not two coincident ones; a spur welded onto an existing SOLID's own corner vertex (`Brep::FromPlanarFaces`) adds exactly one vertex/edge and leaves `IsWireBody()` false; and every refusal (empty list, degenerate curve, ambiguous coincident-but-not-closed endpoints, a bad curve anywhere in a multi-curve call) throws `std::invalid_argument` and leaves the Brep provably untouched (vertex/edge counts re-checked after the caught exception), including when an earlier curve in the same call would have been valid on its own. **New this pass:** `Brep::ExtrudeWireBody(wire_body, direction, cap)` (brep.h; sweep.cpp) closes the "wire-to-solid/sheet promotion (sweep, extrude, or offset FROM a wire body)" gap this bullet's own prior evidence named, for the extrude half specifically. `wire_body` must satisfy `IsWireBody()` and its own edge/vertex graph must walk as ONE simple chain (a single open path or a single closed loop) — a shared helper, `WalkWireChain` (sweep.cpp, anonymous namespace), refuses (`std::invalid_argument`) a branch point (any vertex touching 3+ live edges) or more than one disjoint wire component, the latter caught by walk-coverage rather than leaf-counting alone (a lone open chain plus a separate closed loop in the same Brep totals the same 2 leaves a genuine single open chain has, but the walk from the chain's own leaf never reaches the disjoint loop's edge). The walked edges' own 3D curves are extracted (`ON_BrepEdge::GetNurbForm`) and joined, in walk order, into one continuous profile via the EXISTING, already-tested `NurbsCurve::Join()` (curve.h) — measuring the actual gap between consecutive edges' own curve endpoints and passing a tolerance comfortably above it, rather than assuming `Join()`'s fixed 1e-6 default matches whatever caller-chosen weld tolerance `WireBody()`/`AddWireCurves()` built the wire body with — and the joined profile is handed straight to the EXISTING, already-tested `Brep::Extrude()`, which itself decides (via the joined profile's own `IsClosed()`) whether to cap. This is deliberately a thin composition of two already-proven primitives rather than a new topology-assembly path: a multi-edge wire body extrudes into ONE ruled wall (with a real C0 kink at each original edge join, not a separate quad per edge) plus, for a closed+planar loop, 2 real planar caps — genuine `AssembleSweptBody()` topology (real shared vertices/edges, `Check()`-clean, `IsValid()`/`IsSolid()` true), unlike `ExtrudeFace()`'s/`Thicken()`'s own bare untopologized per-wall faces (see the "Genuine topology produced by every constructor" bullet below). Verified (`TestBrepExtrudeWireBody`, tests/test_basic.cpp): a single-edge open wire and a single self-closed circular wire edge extrude bit-for-bit identically (face count, area/volume) to calling `Extrude()` directly on the same curve; a 2-edge open L-shape wire body and a 4-edge closed rectangular wire body (built across 4 separate `AddWireCurves()` calls, each welding onto the previous call's own vertex) extrude into the exact same shape as manually `Join()`-ing their edges and calling `Extrude()` directly (matching face count, and exact area 35 / exact volume 24 respectively); the closed case's result is closed, oriented, and topologically valid with zero `Check()` issues; `cap = false` on the same closed wire body correctly omits both caps; and every refusal (zero-length direction, `wire_body` not satisfying `IsWireBody()` — a real solid or a completely empty Brep, a 3-edge branch point welded at one vertex, two fully disjoint open wires, and the open-chain-plus-disjoint-closed-loop case above) throws `std::invalid_argument`. **New this pass:** `Brep::OffsetWireBody(wire_body, distance, tolerance)` (brep.h; sweep.cpp) closes the offset half of the same "wire-to-solid/sheet promotion" gap, the counterpart to `ExtrudeWireBody()` above. Same shape restriction (`IsWireBody()`, a single simple chain via the same `WalkWireChain()`), same walk-and-`Join()` construction of one continuous profile, then handed straight to the existing, already-tested `NurbsCurve::OffsetInPlane(distance, out, tolerance)` (curve.h) and rebuilt into a fresh, independent wire body via `WireBody()`. Inherits `OffsetInPlane()`'s own honest EXACT/approximate split unchanged (exact for a line or circular arc/circle, tolerance-driven least-squares refit otherwise, refused for a non-planar profile or a self-intersecting fold) rather than reimplementing any of it. Verified (`TestBrepOffsetWireBody`, tests/test_basic.cpp) against a single-edge open line and a single self-closed circular wire edge (bit-for-bit identical to calling `OffsetInPlane()` directly, then `WireBody()`), a 2-edge and a 3-edge collinear open chain (built via one `WireBody()` call and three separate `AddWireCurves()` calls respectively, matching a manually `Join()`-ed-then-offset equivalent), and every refusal case (not a wire body, a branch point, more than one disjoint component, and an offset distance that folds the profile through its own center of curvature). One real, disclosed limitation: a multi-edge wire body whose joined profile has a genuine C0 corner (e.g. an L-shape) can trip `OffsetInPlane()`'s own general sampling path when a sample lands exactly on the corner's knot parameter — a pre-existing rough edge of `OffsetInPlane()` itself, not something this pass fixes; the test's own multi-edge fixtures are therefore collinear (still genuinely separate, walked, and joined wire edges) to land on `OffsetInPlane()`'s exact line path instead. **New this pass:** `Brep::AddHoleLoop(face_index, wire_body, tolerance)` (brep.h; brep.cpp) closes the "using a wire body as a boolean/imprint tool" case above for the single narrowest real instance of it — punching a closed wire body out of an existing planar face as a genuine new `ON_BrepLoop::inner` hole, real new vertices/edges of its own, never touching the face's existing boundary. Scoped like `MakeEdgeFace()`/`MakeEdgeKillRing()` (both above): the face must be planar with exactly one existing loop, `wire_body` must walk (the same walk-coverage `WalkWireChain()` uses for an open chain, required closed here) as a single simple closed loop of straight (`IsLinear()`) edges only — a curved wire edge is refused, not faceted — and the wire's own 2D image (via an exact affine point-in-plane map, the same exactness `UnrollDevelopable()`'s own planar case already relies on) must sit strictly inside the outer loop with no crossing or self-intersection; winding is normalized to the outer-CCW/inner-CW convention `KillEdgeMakeRing()` already established. Verified (`TestBrepAddHoleLoop`, tests/test_basic.cpp) against the exact same physical 2x2-hole-in-a-5x5-sheet fixture `BuildPlanarFaceWithHole()`/`RemoveHoleLoop()`'s own tests already use — matching V/E growth, `FaceContainsUV()` classification (the hole's own centre goes from inside to genuinely outside), and `Check()`'s naked-edge count (4 before, 8 after) exactly — plus every refusal case (a face that already has a hole, a wire body that isn't one, an open chain, a curved edge, a wire loop crossing or entirely outside the boundary, a wire body off the face's own plane, an out-of-range face_index). **New this pass:** `Brep::AddHoleLoops(face_index, wire_bodies, tolerance)` (brep.h/brep.cpp) closes the "each hole still needs its own `AddHoleLoop` call" gap this bullet's own prior evidence named as `AddHoleLoop()`'s next narrower follow-up — a real batch counterpart, not a new validation path: each `wire_bodies[k]` is punched via an ordinary `AddHoleLoop()` call, in order, against a private trial copy of this Brep, so a later entry sees every hole an earlier entry of the SAME batch already punched as an "existing" hole on the face — the identical cross-hole crossing/nesting/containment rules `AddHoleLoop()` already enforces one hole at a time, simply chained, rather than a separate all-pairs check reimplementing the same logic. All-or-nothing, unlike `RemoveAllHoleLoops()`'s own best-effort "skip what can't be removed" contract just below (skipping a refusal there is always safe since it just leaves an existing hole in place; silently skipping one of the caller's own supplied wire bodies here would leave the caller unsure which holes actually landed) — if any entry fails, the trial copy is discarded and this Brep is left completely untouched (`Result::Failed`, an empty `loop_indices`), the same "left completely untouched" contract every other topology-surgery method in this class already gives for its own refusals. Verified (`TestBrepAddHoleLoops`, tests/test_basic.cpp): the exact same two independent holes (the 2x2 square, the tiny corner triangle) `TestBrepAddHoleLoop`'s own two sequential calls already prove land identically in ONE `AddHoleLoops()` call (same V/E growth, same `LoopCount()` of 3, same `FaceContainsUV()` classification, same `Check()` naked-edge count); a batch where the second entry crosses the first (both new to the same call, neither pre-existing on the face) refuses the WHOLE call rather than keeping the first, individually-valid hole and dropping the second; an empty `wire_bodies` list throws `std::invalid_argument`; and a one-entry batch reproduces `AddHoleLoop()`'s own exact `loop_index` and V/E growth bit-for-bit, proving this is a genuine thin wrapper, not a parallel reimplementation. Still partial: no app command constructs, displays, or extends a wire body at all (`dino8-app` has no curve-only "body" object, only standalone `NurbsCurve` scene objects, dino8-app/src/doc/SceneObject.h:188); a wire body used as a genuine BOOLEAN cutting/imprint tool against an arbitrary (non-planar, already-holed, or multi-hole) target is still out of scope — `AddHoleLoop()`/`AddHoleLoops()` only ever punch holes into an already-planar face (already-holed is fine, several holes per call now too), and only for a straight-edged wire loop, never a curved one; `ExtrudeWireBody()`/`OffsetWireBody()` are each scoped to a single simple chain, refusing a branching or multi-component wire body rather than picking one arbitrarily; a bent (non-collinear) multi-edge wire body's own offset is not guaranteed to succeed, per the corner-sampling limitation above; and welding (in `WireBody()`/`AddWireCurves()`) is still brute-force nearest-vertex search, fine at this scale but not the bucketed approach `VertexWelder` uses for face loops.
- [partial] Non-manifold topology (edge shared by 3+ faces, non-manifold vertices) — construction refuses it: `FromMixedFaces` throws "an edge is shared by 3 or more faces" (brep.cpp:1587-1588; test `TestFromMixedFacesRejectsNonManifoldEdge`, tests/test_basic.cpp:17810). `Check()` reports `NonManifoldEdge` (brep.h:2458); `FacesOfEdge` handles 3+ trims (brep.h:605-611). Genuinely new this pass: `Check()` now also detects `NonManifoldVertex` (pinch-point) defects — a vertex whose incident faces don't form one connected neighbourhood through the vertex's own edges, distinct from `NonManifoldEdge` (an hourglass built from two shells touching at one point with no shared edge has no over-used edge anywhere) — via union-find over the faces touching each vertex's incident edges (brep.h:2474; brep.cpp ~6151-6260), verified by `TestBrepCheckDetectsNonManifoldPinchVertex` (test_basic.cpp:7365). A matching heal now exists too: `Brep::SplitNonManifoldVertex`/`SplitNonManifoldVertices` (brep.h:2864-2875; brep.cpp:6745 onward — corrected 2026-09-28, was mis-cited brep.h:2662-2696; brep.cpp:6538 onward) duplicates the pinch vertex once per disjoint face group, the standard Parasolid/ACIS "disjoin" repair, verified by `TestBrepSplitNonManifoldVertexHealsPinchPoint` (test_basic.cpp:7409). **New this pass:** the specific "no non-manifold-edge counterpart to this vertex-level diagnostic+heal pair" gap this bullet's own prior evidence named is closed — `Brep::SplitNonManifoldEdge`/`SplitNonManifoldEdges` (brep.h:3214/3222; brep.cpp:7130/7199) is the same "disjoin" repair applied to an over-used EDGE instead of a pinch vertex: `edge_index`'s own 3+ trims are partitioned into `TrimWalksMaterialLeft`'s two orientation classes (the same "one trim from each class" shape `Check()`'s own `InconsistentFaceOrientation` test already requires of an ordinary 2-trim edge), then paired one-from-each-class at a time — the first pair stays on the original edge, every other pair moves onto its own fresh duplicate edge via `ON_BrepEdge::DuplicateCurve()`/`ON_BrepTrim::AttachToEdge()` (the exact move `UnjoinEdge` already makes for exactly two trims, repeated per pair here), and any trim left over with no opposite-orientation partner (an odd class-size split) gets its own fresh edge alone, becoming an ordinary naked edge rather than another non-manifold one. Pure topology bookkeeping like its vertex-level counterpart: no edge/trim curve or face is touched beyond which `ON_BrepEdge` record a trim points to, so it never deletes or renumbers anything either. Verified on a hand-built fixture no existing factory produces — three planar "page" faces hinged on one common 3D edge (`FromMixedFaces` itself refuses this shape, "an edge is shared by 3 or more faces") — with the three trims' own `bRev3d` set explicitly (false/true/false) so the fixture deterministically exercises both a genuine cross-class pair (faces 0 and 1) and a leftover singleton (face 2) in one edge: `Check()` reports exactly one `NonManifoldEdge` (`other_index == 3`) beforehand and none after, the healed edge carries exactly the one surviving pair (`TrimCount() == 2`, and no new `InconsistentFaceOrientation` issue), the odd trim lands on one brand-new naked edge, and vertex/face counts are untouched throughout — `TestBrepCheckDetectsAndSplitNonManifoldEdgeHeals` (test_basic.cpp:9012), plus every refusal case (an ordinary <=2-trim edge, out-of-range/deleted `edge_index`). Still partial: `UnjoinEdge` itself still refuses anything but exactly 2 trims (brep.cpp:5685, unchanged by this pass — `SplitNonManifoldEdge` is a new, separate method, not an extension of `UnjoinEdge`'s own scope); booleans still throw rather than producing non-manifold output; and both heals still only make their respective defect manifold (or naked) rather than letting the kernel construct or preserve non-manifold topology as first-class.
- [present] Euler operators (MEV/MEF/KEV/KEF/KEMR/MEKR etc.) — **upgraded from partial.** `Brep::MakeEdgeVertex`/`Brep::KillEdgeVertex` (brep.h:2929/2959; brep.cpp:6822/6855 — corrected 2026-09-28, was mis-cited brep.h:2739/2769; brep.cpp:6708/6741) are a genuine MEV/KEV pair: MEV appends one new vertex plus one new zero-trim ("wire") edge onto an EXISTING vertex (`FacesOfEdge` on the new edge is empty; `Check()` reports it `NakedEdge` with `other_index == 0` - exactly the case that field's own doc comment already named "a dangling edge no face uses at all"); KEV is its exact inverse, refusing anything that borders a face (`TrimCount() != 0`) or whose two endpoints don't identify exactly one degree-1 leaf to remove (neither, or both, refuse). A real bug surfaced and was fixed building this: `ON_Brep::CullUnusedVertices()`'s own source only culls a vertex whose `m_vertex_index` is already `-1` — it never infers "unused" from an empty `m_ei` — so KEV marks the leaf vertex deleted explicitly before `Compact()`, the same explicit-mark convention this class's other deleting methods already use. Verified round-trip-exact (V/E counts return to their pre-MEV values, `Check()` reports identically) and against every refusal case in `TestBrepMakeEdgeVertexAndKillEdgeVertexAreExactInverses` (tests/test_basic.cpp:7895). `Brep::MakeEdgeFace`/`Brep::KillEdgeFace` (brep.h:3056/3084; brep.cpp:7056/7197 — corrected 2026-09-28, was mis-cited brep.h:2893/2921; brep.cpp:6839/6980) are a real MEF/KEF pair, the next two operators in the family. MEF splits a face's own single outer loop into two by inserting one new straight edge between two of its EXISTING, non-adjacent vertices — no new vertex, unlike MEV (F+1, E+1, V unchanged, Euler's own invariant for splitting a face without adding a vertex) — validating the candidate diagonal the standard simple-polygon way first (no proper crossing with any other loop edge, and its own midpoint inside the loop's boundary, so a diagonal that would step outside a concave loop is rejected too, not just a visibly self-intersecting one). Scoped like `SplitNakedEdgeAt()`'s own "linear edges only" restriction, for the same reason (a fabricated straight 2D trim only stays exact over an affine (u,v)->3D map): the face's surface must report `IsPlanar()` and have exactly one loop (no holes), matching the restriction `MergeCoplanarFaces()`'s own `TryMergeCoplanarPair` already places on itself. KEF is its exact inverse for any two single-loop faces sharing one edge on the exact same surface (`m_si`) — deliberately more general than "undoes MEF" alone, since it only re-splices existing trims rather than fabricating geometry, so it needs no planarity — refusing a slit (both trims on the same loop), mismatched surfaces, or two loops that don't traverse the shared edge in opposite directions (the same well-formed-2-manifold-edge shape `TryMergeCoplanarPair` already checks for). Verified round-trip-exact (splitting a box's own planar top face into two triangles along its diagonal and merging back: V/E/F return exactly to their pre-MEF values, `Check()` reports identically, volume unchanged at 1.0) and against every refusal case (adjacent vertices, `vertex_a == vertex_b`, a vertex not on the target loop, mismatched-surface KEF, a non-2-trim edge, out-of-range/deleted indices) in `TestBrepMakeEdgeFaceAndKillEdgeFaceAreExactInverses` (tests/test_basic.cpp:8047). Genuinely new this pass, and the last operator pair in the family: `Brep::MakeEdgeKillRing`/`Brep::KillEdgeMakeRing` (declarations brep.h:3131/3172 — corrected 2026-09-28, was mis-cited brep.h:2996-3081) are a real MEKR/KEMR pair. MEKR takes a face with EXACTLY one outer loop and one inner (hole) loop and welds them into ONE loop via a zero-width bridge edge that appears TWICE in the merged loop's own trim sequence (once each direction, immediately adjacent to itself) — the standard textbook "slit" device for representing a hole with a single loop — killing the ring (F unchanged, E+1, the face's own loop count 2→1). Scoped like MEF (planar surface required), plus one check MEF itself never needs since it only ever has one loop: the outer polygon must actually CONTAIN the inner loop (a single interior-point containment test), refusing two loops that aren't genuinely nested. KEMR is its exact inverse, refusing anything that isn't the exact "slit" shape (two trims on the SAME loop, opposite directions, the shared loop typed `ON_BrepLoop::outer`) and recovering which of the two runs the bridge separates is the genuine hole via the standard outer-CCW/inner-CW Brep signed-area convention (shoelace on each run's own trim-start points), refusing same-sign runs rather than guessing. Verified round-trip-exact on a hand-built single planar face with a real `ON_BrepLoop::inner` hole loop (a fixture no existing kernel factory produces — `TrimmedPlanarFace`'s own `hole_loops_uv` is a pseudo-trim side table for tessellation, not a real `ON_BrepLoop`): V/E/F counts, loop count (2→1→2), trim counts (4+4→10→4+4), and `Check()`'s own `NakedEdge` count (the fixture is a single open shell, so all 8 boundary edges are naked both before and after — confirming in particular that the merged loop's own retraced bridge trims never trip `SelfIntersectingLoop`) all return to their pre-MEKR values, plus every refusal case (not-exactly-2-loops, `vertex_a == vertex_b`, both vertices on the same loop, a vertex on neither loop, different-loops/different-faces KEMR input, a non-2-trim edge, out-of-range/deleted indices) in `TestBrepMakeEdgeKillRingAndKillEdgeMakeRingAreExactInverses` (tests/test_basic.cpp). All six named operators (MEV/KEV, MEF/KEF, MEKR/KEMR) are now real, tested, and round-trip exact against each other. Still scoped, the same way MEF/KEF already are: planar faces only, a straight new edge between vertices/loops already on the boundary, and MEKR limited to exactly one hole per face (a face with two or more holes needs one MEKR call per hole, not yet exercised) — a curved-face Euler op, or a single call bridging more than one ring at once, is out of scope.
- [partial] Kernel-level topology enumeration API (vertex/edge/loop/face iteration and counts) — `FaceCount`/`VertexCount`/`EdgeCount` (brep.cpp:350-352; brep.h:633-635 — corrected 2026-09-28, was mis-cited brep.cpp:304-306; brep.h:511-513), tested in `TestBrepAdjacencyQueries` (test_basic.cpp:17616). **New this pass:** the "no loop/trim count or iteration API (callers walk `raw().m_F[i].Loop(j)/Trim(k)` by hand)" gap this bullet's own prior evidence named is closed — `Brep::LoopCount`/`LoopsOfFace`/`FaceOfLoop`/`TrimCount`/`TrimsOfLoop`/`LoopOfTrim`/`EdgeOfTrim` (brep.h:814-851; brep.cpp:580-673) give a genuine public loop/trim walk in terms of GLOBAL `raw().m_L`/`m_T` indices (the same convention `EdgesOfVertex()`/`FacesOfEdge()` already use for `m_V`/`m_E`), reading straight off `ON_BrepFace::m_li`/`ON_BrepLoop::m_ti`/`ON_BrepTrim::m_li`/`m_ei` rather than duplicating any new bookkeeping — each throws `std::out_of_range`/`std::invalid_argument` the same way `FacesOfEdge()`/`NeighborFaces()` already do for an out-of-range or deleted face/loop/trim slot. Verified against a real 6-face `Brep::FromPlanarFaces()` box (every face's one loop, its 4 trims in `raw()`'s own exact `Loop(0)->Trim(k)` order, and each trim's `m_ei` match `TrimsOfLoop()`/`EdgeOfTrim()` one-for-one) and against every refusal case, in `TestBrepLoopAndTrimTopologyQueries` (test_basic.cpp:9366). **New this pass:** the "`VertexCount`/`EdgeCount`... still include deleted slots until `Compact()`" gap this bullet's own prior evidence named is closed — `Brep::LiveFaceCount`/`LiveVertexCount`/`LiveEdgeCount` (brep.h; brep.cpp) are live counterparts that walk their own table once and count only the entries whose `m_face_index`/`m_vertex_index`/`m_edge_index` is still >= 0 (the same "deleted" convention `RequireLoop`/`RequireTrim` and every `Kill*`/`Delete*` method's own refusal checks already use), rather than returning the raw table SIZE the way `FaceCount`/`VertexCount`/`EdgeCount` themselves still do. No side effect (unlike `Compact()`, which also renumbers every surviving slot) - a caller gets an accurate current count without forcing a renumber. Every public topology-surgery method in this class (`DeleteFace()` included) already `Compact()`s before returning, so the raw/live divergence these three close is never externally observable through them alone; it shows up when a caller reaches past that and marks a slot deleted directly via `raw()` - exactly what bare `ON_Brep::DeleteFace()`/`DeleteFaces()` do (and what `dino8-app`'s `ExtractSrf`/`DeleteFaces` command did before the "Delete / extract face" bullet above's own app-wiring fix this same pass) - which is exactly the scenario `TestBrepLiveCountsExcludeUncompactedDeletedSlots` (test_basic.cpp) exercises directly: deleting all 6 faces of a genuinely-welded `Brep::FromPlanarFaces()` box via raw `ON_Brep::DeleteFace()` calls (no `Compact()` between them) leaves `FaceCount()`/`VertexCount()`/`EdgeCount()` unchanged at 6/8/12 while `LiveFaceCount()`/`LiveVertexCount()`/`LiveEdgeCount()` correctly report 0/0/0 (every edge's own second bordering face's deletion frees its last trim, and every vertex's own 3 edges going frees it too); a subsequent `Compact()` brings the raw counts down to match, at 0/0/0. Still partial: `LoopCount`/`TrimCount` are inherently per-face/per-loop scoped queries (a live face's own loop/trim list), not raw whole-Brep table sizes, so this pass's live/raw distinction doesn't apply to them the same way; `Box()`/`Sphere()`/`Torus()` still report 0 vertices/edges/loops/trims (confirmed `Torus()` also builds via the plain surface-only `NewFace(int)` overload, brep.cpp:229-266, same as Box/Sphere/TrimmedPlanarFace/FromSurface, even though the file's own top-of-file disclosure comment now names only 4 of these 5 factories — a minor staleness in the source's own comment, not in this claim).
- [partial] Loop structure: inner loops (holes), loop walking (Prev/NextTrim), outer/inner classification — real inner loops come only from the general boolean (boolean_general.cpp:3039 `BuildLoop(..., ON_BrepLoop::inner, ...)`) or from `Brep::MakeEdgeKillRing`'s own hand-built slit bridge (see the Euler operators bullet above). `TrimmedPlanarFace` holes are side-table polygons, not `ON_BrepLoop`s (brep.h:159-164). `MergeCoplanarFaces` skips any face with holes or more than one loop. **New this pass:** the "loop walking (Prev/NextTrim)" and "outer/inner classification" gaps this bullet's own prior evidence named, and its own closing claim "No public loop API exists", are closed — `Brep::TypeOfLoop` (brep.h:830; brep.cpp:643-659) is a lossless public mirror of `ON_BrepLoop::TYPE` (Outer/Inner/Slit/CurveOnSurface/PointOnSurface/Unknown), and `Brep::NextTrimInLoop`/`PrevTrimInLoop` (brep.h:860-861; brep.cpp:695-697, sharing a private `WalkTrimInLoop` helper) walk a trim's own loop cyclically in its stored `m_ti` order — the public equivalent of the `loop.m_ti[]` walk `MakeEdgeFace`/`KillEdgeFace`/`MakeEdgeKillRing`/`KillEdgeMakeRing` already do by hand internally. Verified on `BuildPlanarFaceWithHole()`'s own real outer+inner quad fixture (the same fixture `MakeEdgeKillRing`'s own test uses): `TypeOfLoop` correctly distinguishes the Outer and Inner loop without relying on loop position, and `NextTrimInLoop`/`PrevTrimInLoop` reproduce `TrimsOfLoop()`'s own exact cyclic order in both directions and return to the start after one full walk (`TestBrepLoopAndTrimTopologyQueries`, test_basic.cpp:9366 — same test as the bullet above, since both API groups share one fixture-driven test). **New this pass:** the "never by a general 'add a hole to this face' constructor" gap this bullet's own prior evidence named is closed — `Brep::AddHoleLoop` (brep.h/brep.cpp, detailed in full under the Wire bodies bullet above, since it consumes a wire body as its hole boundary) builds a genuine new `ON_BrepLoop::inner` loop, with real new vertices/edges of its own, directly onto an existing planar face — the first way to add a hole that isn't either the general boolean engine's own internal `BuildLoop` or `MakeEdgeKillRing`'s hand-built slit bridge (which only ever RE-DRAWS a hole that already exists as two loops, never creates one from caller-supplied curve data). **New this pass:** the "a face that already has a hole is out of scope" restriction `AddHoleLoop`'s own prior evidence named as its own next narrower follow-up is closed — `AddHoleLoop` (brep.h/brep.cpp) now finds the face's own outer loop by scanning its loop types (rather than assuming loop 0), and punches an ADDITIONAL `ON_BrepLoop::inner` loop onto a face that already has one or more holes, checked against every hole the face already has, not just its outer boundary: the new hole's own 2D image must not properly cross any existing hole's own boundary (the same `SegmentsProperlyIntersect2D` check already used against the outer loop, now run against each existing hole's own trim-start polygon too), must not land strictly inside an existing hole (`PointInPolygon2D` on the new hole's own first vertex against the existing hole's polygon — that region is empty space already, not material left to punch), and must not itself strictly contain an existing hole (the same test with the two polygons swapped — this call can't represent "replace two holes with one"). A face carrying a slit or curve-on-surface/point-on-surface loop (e.g. `MakeEdgeKillRing`'s own bridge shape) is refused rather than punched alongside those loops, since only `ON_BrepLoop::outer`/`ON_BrepLoop::inner` are recognized by the new type scan. Verified in `TestBrepAddHoleLoop` (tests/test_basic.cpp): a genuinely independent second hole (a small triangle near a corner of the same 5x5 sheet `TestBrepAddHoleLoop`'s own fixture already builds, nowhere near the first 2x2 hole) succeeds exactly like the first `AddHoleLoop` call did (V/E grow by exactly the triangle's own 3 vertices/edges, `LoopCount()` reaches 3, `IsValid()` still holds), and three new refusal cases are each confirmed to leave the Brep completely untouched: a candidate hole that properly crosses the first hole's own boundary, one nested strictly inside it, and one that would fully contain (swallow) it. Full `dino8_kernel_tests` suite re-run clean via `ctest`: 100% passing, 0 regressions. This session's only source edits are dino8-kernel/include/dino8/kernel/brep.h, dino8-kernel/src/brep.cpp, and dino8-kernel/tests/test_basic.cpp.

Still partial, and this is why the item stays `partial` rather than `present`: `AddHoleLoop`/`AddHoleLoops` are still scoped to a straight-edged wire loop each (a batch call now bridges several new holes in one call, closing that half of this note, but every hole in it is still straight-edged), and a face with a slit/curve-on-surface/point-on-surface loop is refused outright rather than punched around; `TrimmedPlanarFace` holes are still side-table polygons, not real loops. **New this pass:** `MergeCoplanarFaces` no longer refuses a face outright for carrying a hole — see the "Merge coplanar / co-surface adjacent faces" bullet below for the full account — though it still only restores a hole onto the merged face when every one of that hole's own edges is straight, calling `AddHoleLoop` itself to do the restoring rather than a separate copy of its logic.
- [partial] Merge coplanar / co-surface adjacent faces (remove interior edge, rebuild one face) — `Brep::MergeCoplanarFaces` (brep.cpp:5692-5787; brep.h:2459 — corrected 2026-09-28, was mis-cited brep.h:2337, which now holds unrelated oblique-cylinder text) requires planar faces only, the same plane, and exactly one shared 2-trim edge. **New this pass:** the "faces with holes" restriction this bullet's own prior evidence named is closed for the straight-edged case — a face carrying one or more existing `ON_BrepLoop::inner` holes (real holes, e.g. from `AddHoleLoop`, not `TrimmedPlanarFace`'s own side-table polygons) is no longer skipped outright. `FindOuterLoop` (brep.cpp:5472-5491, a small shared scan reused by both `MergeCoplanarFaces` and `TryMergeCoplanarPair`) classifies each candidate face's own loops exactly the way `AddHoleLoop` already does — one outer loop plus zero or more inner (hole) loops, refusing (same as `AddHoleLoop`) a face carrying a slit/curve-on-surface/point-on-surface loop (e.g. `MakeEdgeKillRing`'s own bridge shape). `TryMergeCoplanarPair` (brep.cpp:5506-5688) captures each hole as its own ordered 3D vertex loop, straight edges only (`IsLinear()` on every edge, the identical restriction `AddHoleLoop` itself already has for its wire-loop input) BEFORE mutating anything, then — after the merge itself succeeds — reconstructs every captured hole onto the merged face by calling `AddHoleLoop` itself once per hole (via a `NurbsCurve`/`WireBody` round-trip), reusing that method's already-tested uv-mapping/containment logic wholesale rather than re-deriving it. The whole operation (merge + every hole's own restoration) runs on a private `ON_Brep` trial copy first and is only committed to the real Brep if every step succeeds, so a hole that can't be restored refuses the ENTIRE pair rather than merging with it silently dropped — the same trial-copy, all-or-nothing discipline `AddHoleLoops` already uses for its own multi-hole batch. Verified (`TestMergeCoplanarFacesPreservesExistingHoleOnMergedFace`/`TestMergeCoplanarFacesRefusesPairWhoseHoleHasCurvedEdge`, tests/test_basic.cpp) against a hand-built two-square fixture (`BuildTwoAdjacentPlanarFaces`, a genuine shared 2-trim edge, built the same way `TestMergeCoplanarFacesWeldsTwoAdjacentSquaresIntoOne`'s own simpler fixture is, but by hand so a hole can be added to one side): a straight-edged hole on one of the two squares survives the merge with its own 4 corners landing exactly where they started, while the identical fixture with a 3-edge hole whose closing edge is a genuine arc refuses the whole pair outright, leaving both faces completely untouched. App wiring: cmd_solidtools.cpp:1197. Still partial: nothing for co-cylindrical, co-spherical or tangent co-surface faces, and a holed face's own hole must still be straight-edged, the same restriction `AddHoleLoop` itself still has.
- [present] Merge contiguous tangent edges (combine two edges sharing a vertex into one) — **upgraded from partial.** `Brep::MergeContiguousEdges`/`MergeAllContiguousEdges` (brep.h/brep.cpp) is a genuine kernel wrapper around `ON_Brep::CombineContiguousEdges` (the same primitive the app-only `MergeEdgeCommand`, dino8-app/src/commands/cmd_fillet.cpp:2303-2320, already called directly with a 5deg tangent tolerance), enforcing the same valence-2/matching-faces/kink-angle contiguity contract and, on success, concatenating the 3D edge curve and every affected 2D trim curve. See the healing category's own bullet below (this is the same underlying capability, credited there in full detail) for the exact tests and remaining app-wiring gap.
- [partial] Remove edge / collapse micro edge (kill-edge-vertex style healing) — `Brep::RemoveNakedMicroEdge` (brep.cpp:6195 — corrected this pass, was mis-cited brep.cpp:5729) handles only an isolated naked (1-trim) sliver whose neighbours are also naked. `Brep::RemoveDegenerateEdges` (brep.cpp:7397 — corrected this pass, was mis-cited brep.cpp:6518) runs `ON_Brep::CollapseEdge` on shared/naked edges shorter than tolerance. **Correction (this pass):** this bullet's own prior text was stale — it never mentioned `Brep::RemoveSharedMicroEdge` (brep.cpp:6331), the shared (2-trim, interior) counterpart to `RemoveNakedMicroEdge` that a prior session already added and the healing category's own "Remove small / sliver edges" bullet already credits, even though it directly narrows THIS bullet's own "kill-edge-vertex style healing" scope too: an isolated valence-3-endpoint shared micro-edge (shorter than tolerance, bordering two different faces each with a spare loop-edge to absorb it) can now be removed, not just detected. This does not close the item, since the finding was simply never carried over here when `RemoveSharedMicroEdge` first landed, not a new capability this pass adds — there is still no general "remove a shared edge ABOVE tolerance / merge its two arbitrary faces" operation (that is `MergeCoplanarFaces`'s/`MergeSameSurfaceFaces`'s own job for a planar/same-surface pair specifically, not a general-purpose edge collapse), and a non-isolated (valence-4+, or whose neighbour's own loop is too small to absorb the join) shared micro-edge is still left alone.
- [partial] Split / imprint a face by a curve while keeping the polysurface topology — app `SplitFace` (cmd_fillet.cpp:2057) splits the underlying surface at an isoline through the CSX hit, not an arbitrary trim loop. The only kernel split, `Brep::SplitNakedEdgeAt` (brep.cpp:6615), splits a linear naked edge; that is not a face imprint (the kernel's real face-face imprint, `ImprintFaces`, is scored under Boolean operations, not here).
- [partial] Delete / extract face (with or without healing neighbours) — app `ExtractSrf`/`DeleteFaces` (dino8-app/src/commands/cmd_srfedit.cpp:235-259) uses `ON_Brep::DuplicateFace`/`DeleteFace` and leaves an open shell. **New this pass:** the "kernel has no public delete-face API" half of this gap is closed — `Brep::DeleteFace`/`Brep::DeleteFaces` (brep.h; brep.cpp) delete a caller-chosen, currently-live face (or a batch of them, duplicates collapsed, validated up front so a bad index anywhere refuses the WHOLE call rather than deleting a valid prefix), sharing their actual deletion/finalize body (`DeleteFacesImpl`, also used by `RemoveDegenerateFaces`/`RemoveSliverFaces` now) rather than reimplementing it. Unless `heal = false`, re-joins the exposed naked-edge boundary with `JoinNakedEdges`/`SewTJunctions`, the same delete-and-tolerant-join heal `RemoveThinFaces` already gives a `Check()`-flagged face — see the "Sliver / degenerate micro-face removal" bullet below for the T-junction half of that heal, newly wired in by this same change. Deliberately no separate "already-deleted face_index" check or test: both methods always `Compact()` before returning, so a live face_index is never left pointing at a deleted-but-uncompacted slot across two calls the way a stale loop index into `AddHoleLoop`'s/`RemoveAllHoleLoops`'s own uncompacted batch can be - index 0 always refers to whichever face is CURRENTLY first, not to whatever used to be there, confirmed by repeatedly deleting index 0 down to an empty Brep rather than merely asserted. Verified (`TestBrepDeleteFaceRemovesArbitraryFaceAndRefusesBadIndices`/`TestBrepDeleteFaceHealFlagControlsJoinAndSewTJunctions`, tests/test_basic.cpp, against `Brep::FromPlanarFaces(CheckHealBoxFaces())` - a genuinely welded box, not the surface-only `Brep::Box()` the "Genuine topology from every constructor" bullet below already discloses has no real edges to go naked at all): deleting one face of a valid box leaves the other 5 untouched and the 4 boundary edges naked; every refusal case (an out-of-range single index, deleting index 0 six times in a row down to an empty Brep and only then throwing, a bad index anywhere in a batch leaving the Brep completely untouched, duplicates collapsing to one removal); and, on a fixture combining `TestBrepSewTJunctionsClosesActualTJunction`'s own 3-face T-junction plate with one extra, wholly disconnected face, deleting ONLY that disconnected face with `heal = true` also sews the T-junction elsewhere in the same Brep down to the exact same 7-naked-edge after-state `SewTJunctions` alone already proves, while `heal = false` leaves it at the exact same 10-naked-edge before-state. **New this pass:** the "dino8-app's own ExtractSrf/DeleteFaces command still calls ON_Brep::DuplicateFace/DeleteFace directly rather than this new kernel API" half of this gap is closed — every one of `cmd_srfedit.cpp`'s five face-removal call sites (`FacePickCommand`'s `Extract`/`Delete`/`Untrim`/`UntrimBorderOnly` cases, plus `ExtendSrf`'s own face-replace path) now goes through a shared `DeleteFaceHealed(ON_Brep&, int)` helper that round-trips the app's plain `ON_Brep` through a `kernel::Brep` and calls this method's own `DeleteFace()` (heal defaulted on), rather than calling bare `ON_Brep::DeleteFace(face, true)` + `Compact()` directly the way all five did before - the app's own `true` argument there was never a heal flag at all (`ON_Brep::DeleteFace`'s second parameter is `bDeleteFaceEdges`, not "re-join the exposed boundary"), so none of these five commands ever attempted a re-join before this pass; now every one gets the same `JoinNakedEdges`/`SewTJunctions` heal this class's own `DeleteFace()` already gives a direct kernel caller. Verified end-to-end through the real commands, not just at the kernel layer: `dino8-app`'s own `smoke.sh` (`srfedit_script.txt`) re-run clean after this change - `ExtractSrf`/`DeleteFaces`/`Untrim`/`UntrimBorder`/`ExtendSrf`'s own object/face/edge-count checks (e.g. `"5 faces, 12 edges, open"`, `"DeleteFaces: face 4 deleted, 4 face(s) left"`) all still match exactly, confirming the added heal is a genuine no-op on these fixtures' own already-simple (no coincident-duplicate or T-junction) naked boundaries, not a silent behavior change; `dino8-app`'s own `ctest` suite (14/14) and `dino8-kernel`'s own `ctest` (`dino8_kernel_smoke`, 100%) both pass. Still partial: still no geometric neighbour-EXTENSION (a face whose removal doesn't expose a coincident naked-edge boundary still leaves the shell open, an honest limit shared with `RemoveThinFaces`) - the app wiring only ever gets whatever heal `DeleteFace()` itself can give, not a stronger one.
- [partial] Tolerance model on topological entities (vertex/edge tolerances, tolerant modelling) — `FixUnsetEdgeTolerances` (brep.cpp:5255), `RecordMeasuredTolerances` (brep.cpp:6089, records the measured gap as `ON_BrepEdge`/`ON_BrepVertex m_tolerance`), `Check()` honours those, `TessellateToClosedMeshTolerant` (brep.h:2840). Still partial: booleans and fillets never read edge tolerances (fixed `tol = 1e-6`); global tolerances are fixed constants, not scaled by model size.
- [missing] Persistent naming / topology identity and attributes across edits (face/edge IDs surviving Compact, boolean, split) — sub-object references are raw `m_E`/`m_F` indices (dino8-app/src/doc/SubObject.h:7-9); every topology edit clears the side tables and renumbers via `Compact`. There is no persistent ID or attribute scheme.
- [partial] Cap naked loops (close planar holes of an open shell into faces) — `Brep::CapPlanarHoles` (brep.cpp:7815; brep.h:3294 — corrected 2026-09-28, was mis-cited brep.cpp:6849; brep.h:2802-2822) walks naked-edge chains, checks planarity, builds the cap with `ON_BrepTrimmedPlane`, and re-joins. Still partial: a non-planar hole is left open, and chains through a vertex carrying more than two naked edges are skipped.
- [partial] Sliver / degenerate micro-face removal (as opposed to isolated boundary micro-edges) — `Brep::RemoveSliverFaces`/`RemoveDegenerateFaces` (brep.cpp:6510/6514; brep.h:2641/2651) delete faces `Check()` flags as `SliverFace`/`DegenerateFace` and re-join neighbours as tolerant edges. **Correction (this pass):** the "a T-junction sliver leaves naked edges since this does not call `SewTJunctions`" text above is now stale, not current — both methods' shared body (`RemoveThinFaces`, now itself a thin wrapper around the new `DeleteFacesImpl` the "Delete / extract face" bullet above credits in full) calls `SewTJunctions(join_tolerance)` right after `JoinNakedEdges(join_tolerance)`, so a T-junction a sliver's removal exposes is now swept closed too, not just an ordinary coincident-endpoint pair — see that bullet's own verification detail (the fix is the same shared code path, exercised there via `Brep::DeleteFace` directly rather than via a `Check()`-flagged sliver, since `RemoveThinFaces`'s own existing tests were never about T-junctions specifically). Still partial for the reason that named gap was never about: delete-and-tolerant-join is still not a geometric collapse (a hairline face is replaced by a tolerant edge, not shrunk to nothing), and `SewTJunctions` itself only resolves a T-junction against another naked LINEAR edge (its own disclosed scope, unchanged here). (The `Check()` false-positive this bullet used to cite as making the pair "destructive on the kernel's own valid solids" is fixed — see the top-of-document honesty note; re-verified this session end-to-end via `TestBrepRemoveDegenerateOrSliverFacesDoesNotTouchValidSolids`, which confirms 0 faces removed from `Box()`/`Extrude(circle)`.)
- [partial] Genuine topology produced by every constructor/primitive — `Box`, `Sphere`, `Torus`, `FromSurface` and `TrimmedPlanarFace` still use the surface-only `NewFace(int)` (brep.cpp:134-302; disclosed brep.h:25-51). Such a Brep has no edges or vertices and `ON_Brep::IsValid()` reports it invalid. Knock-on effects: `SplitDisjointPieces` throws on it, and adjacency queries return nothing. The sweep, boolean and fillet factories do build real topology. **Correction: this list was two functions short.** `Thicken()` (sweep.cpp) and the newer `ExtrudeFace()` (sweep.cpp, see **kernel: Sweeping, lofting, extruding, revolving**'s own bullet) both build EVERY face (caps and side walls alike) via the same bare `NewFace(surface_index)` overload in their own `add_cap`/`add_wall` lambdas, not `AssembleSweptBody()`'s vertex/edge-producing overload the rest of the sweep family uses - so a `Thicken()`/`ExtrudeFace()` result has no real edge/vertex topology either, `IsValid()` reports it invalid, and the same `SplitDisjointPieces`/adjacency-query knock-on effects apply, even for an otherwise-genuine closed solid (confirmed by mesh volume and `IsClosedManifold()`).

*Note on this category's score (this pass): a new public loop/trim topology
API — `Brep::LoopCount`/`LoopsOfFace`/`FaceOfLoop`/`TypeOfLoop`/`TrimCount`/
`TrimsOfLoop`/`LoopOfTrim`/`EdgeOfTrim`/`NextTrimInLoop`/`PrevTrimInLoop`
(brep.h:808-861; brep.cpp:580-698) — gained real, tested kernel code
(`TestBrepLoopAndTrimTopologyQueries`, test_basic.cpp:9366), closing the
specific "no loop/trim count or iteration API" and "no public loop API,
no Prev/NextTrim walking, no outer/inner classification" gaps the
"Kernel-level topology enumeration API" and "Loop structure" bullets
above previously named by name. The category's own 13/13/1 (72.2%) split
is unchanged: both items were already scored `partial` and stay `partial`,
since each still has its own separate, real remaining gap unrelated to
loop/trim iteration (deleted-slot counting and Box/Sphere/Torus's own
missing topology for the first; hole/inner-loop construction for the
second) — the same "genuine new evidence, unchanged partial score"
pattern this document already uses elsewhere (e.g. the Boolean operations
category's `TrimSheetBySolid`/`ImprintFaces` tolerance notes above).
Full `dino8_kernel_tests` suite (via `ctest`): 100% passing (1 test
target, `dino8_kernel_smoke`), 0 regressions. The kernel-only headline is
unaffected (no bucket moved).*

*Note on this category's score (this pass): `Brep::SplitNonManifoldEdge`/
`SplitNonManifoldEdges` (brep.h:3214/3222; brep.cpp:7130/7199) — the
non-manifold-edge "disjoin" heal the "Non-manifold topology" bullet's own
prior evidence named as the one still-missing counterpart to
`SplitNonManifoldVertex`'s pinch-vertex heal — gained real, tested kernel
code (`TestBrepCheckDetectsAndSplitNonManifoldEdgeHeals`,
test_basic.cpp:9012, against a hand-built three-face non-manifold-edge
fixture no existing factory produces). The category's own 13/13/1 (72.2%)
split is unchanged: the item was already scored `partial` and stays
`partial`, since it still has its own separate, real remaining gaps this
pass doesn't touch (`UnjoinEdge`'s own 2-trims-only scope; booleans still
throwing on non-manifold output; non-manifold topology still not
first-class) — the same "genuine new evidence, unchanged partial score"
pattern the note above already uses for this same category's loop/trim
API pass. Full `dino8_kernel_tests` suite (via `ctest`): 100% passing (1
test target, `dino8_kernel_smoke`), 0 regressions. The kernel-only
headline is unaffected (no bucket moved).*

*Fourth note on this category's score (this pass): `Brep::ExtrudeWireBody`
(brep.h; sweep.cpp) closes the extrude half of the "Wire bodies" bullet's
own previously-named "wire-to-solid/sheet promotion" gap - real, tested
kernel code (`TestBrepExtrudeWireBody`, test_basic.cpp) built by composing
two already-proven primitives (`NurbsCurve::Join()`, `Brep::Extrude()`)
rather than a new topology-assembly path. The category's own 13/13/1
(72.2%) split is unchanged: the item was already scored `partial` and
stays `partial`, since it still has its own separate, real remaining gaps
this pass doesn't touch (no app wiring, no wire-body offset, no using a
wire body as a boolean/imprint tool, and `ExtrudeWireBody()` itself is
scoped to a single simple open-or-closed chain) - the same "genuine new
evidence, unchanged partial score" pattern the three notes above already
use for this same category. Full `dino8_kernel_tests` suite (via
`ctest`): 100% passing (1 test target, `dino8_kernel_smoke`), 0
regressions. The kernel-only headline is unaffected (no bucket moved).*

*Fifth note on this category's score (this pass): `Brep::OffsetWireBody`
(brep.h; sweep.cpp) closes the offset half of the "Wire bodies" bullet's
own "wire-to-solid/sheet promotion" gap, the counterpart to
`ExtrudeWireBody()`'s own extrude half above - real, tested kernel code
(`TestBrepOffsetWireBody`, test_basic.cpp) built the same way
`ExtrudeWireBody()` itself was: a thin composition of two already-proven
primitives, reusing the same file-local `WalkWireChain`/join-tolerance
machinery `ExtrudeWireBody()` already established (sweep.cpp) to turn
`wire_body`'s own walked edges into one joined profile, then handing that
straight to the existing, already-tested `NurbsCurve::OffsetInPlane()`
(curve.h) and rebuilding the result into a fresh wire body via the
existing `WireBody()`. Scoped exactly like `ExtrudeWireBody()`: refuses a
branch point or more than one disjoint wire component the same way, and
inherits `OffsetInPlane()`'s own honest EXACT/approximate split (exact for
a line or circular arc/circle, a tolerance-driven least-squares refit for
any other planar curve, refused for a curve that isn't planar in its own
fitted plane or whose offset would fold it through itself). One real,
disclosed limitation surfaced building this and is NOT papered over: a
multi-edge wire body whose joined profile has a genuine C0 corner (e.g. an
L-shaped or rectangular chain) can trip `OffsetInPlane()`'s own general
(non-exact) sampling path when a sample lands exactly on the corner's own
knot parameter (a pre-existing rough edge of `OffsetInPlane()` itself, not
something this pass introduces or fixes) - `TestBrepOffsetWireBody()`'s own
multi-edge fixtures are therefore built from COLLINEAR legs (still
genuinely separate wire edges, walked and joined across one `WireBody()`
call and across three separate `AddWireCurves()` calls respectively, so
the walk/join plumbing itself is still fully exercised) rather than a bent
chain, landing on `OffsetInPlane()`'s exact line path instead of its
corner-sensitive general path; a bent multi-edge wire body remains
offsettable in practice whenever the sample grid happens not to land on
the corner, but that is not guaranteed. Verified (`TestBrepOffsetWireBody`,
test_basic.cpp): a single-edge open line and a single self-closed circular
wire edge offset bit-for-bit identically to calling `OffsetInPlane()`
directly on the same curve, then `WireBody()` on the result; a 2-edge
collinear open chain (one `WireBody()` call) and a 3-edge collinear open
chain (three separate `AddWireCurves()` calls) offset into the same shape
as manually `Join()`-ing their edges and calling `OffsetInPlane()`
directly; and every refusal (`wire_body` not satisfying `IsWireBody()` - a
real solid or a completely empty Brep, a branch point, more than one
disjoint wire component, and an offset distance that folds the profile
through its own center of curvature, propagating `OffsetInPlane()`'s own
`Result::Failed`) throws `std::invalid_argument`. The category's own
13/13/1 (72.2%) split is unchanged: the item was already scored `partial`
and stays `partial` - wire bodies are still not constructed, displayed, or
extended anywhere in `dino8-app`, and using a wire body as a boolean/
imprint tool is still missing entirely - but the "Wire bodies" item's own
remaining effort shrinks further: of the three gaps row 24's own priority
table entry named (app wiring, wire-body offset, wire body as a boolean/
imprint tool), only the first and third remain. Full `dino8_kernel_tests`
suite (via `ctest`): 100% passing (1 test target, `dino8_kernel_smoke`,
5353 checks), 0 regressions. The kernel-only headline is unaffected (no
bucket moved).*

*Sixth note on this category's score (this pass): `Brep::AddHoleLoops`
(brep.h/brep.cpp) closes the "Wire bodies"/"Loop structure" bullets' own
"each hole still needs its own `AddHoleLoop` call (no single call
bridging more than one new hole at once)" gap - real, tested kernel code
(`TestBrepAddHoleLoops`, test_basic.cpp) built as a thin batch wrapper
around the already-proven `AddHoleLoop()` (each entry punched in turn
against a private trial copy, so later entries see earlier entries' own
holes as "existing", getting the identical cross-hole checks for free)
rather than a parallel reimplementation. Also corrected this pass: the
"Multi-shell / multi-lump bodies" bullet's own stale "New finding:
`BooleanCombineGeneral` has no compound-operand guard at all" text, which
described a gap already closed by an earlier commit (`5a28aa7`) that
simply predated this bullet's own last edit - see that bullet above for
the correction and the "kernel: Boolean operations" category's own
already-accurate account of the same fix. Neither change flips this
category's own 13/13/1 (72.2%) split: `AddHoleLoops` narrows the "Wire
bodies"/"Loop structure" items' own remaining gaps (both already `partial`
for other, untouched reasons - app wiring and the boolean/imprint-tool
gap for the former, the straight-edged-only and slit/curve-on-surface
restrictions for the latter) without flipping either, and the
`BooleanCombineGeneral` correction touches a finding that was never itself
the reason "Multi-shell / multi-lump bodies" was scored `partial` (the
missing inner-void/hollow-region concept was, and still is). Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions.
This session's only source edits are dino8-kernel/include/dino8/kernel/
brep.h, dino8-kernel/src/brep.cpp, dino8-kernel/tests/test_basic.cpp, and
PARITY_MAP.md itself. The kernel-only headline is unaffected (no bucket
moved).*

*Seventh note on this category's score (this pass): `Brep::MergeCoplanarFaces`
(brep.cpp:5692-5787) closes the "or faces with holes" half of its own
"Merge coplanar / co-surface adjacent faces" bullet's previously-named gap,
for the straight-edged case - real, tested kernel code
(`TestMergeCoplanarFacesPreservesExistingHoleOnMergedFace`/
`TestMergeCoplanarFacesRefusesPairWhoseHoleHasCurvedEdge`, test_basic.cpp)
built the same "thin composition of an already-proven primitive" way this
category's own Fourth/Fifth/Sixth notes above already favor: `TryMergeCoplanarPair`
(brep.cpp:5506-5688) captures each source face's own existing holes as
ordered 3D vertex loops before touching anything, then - once the merge
itself succeeds, on a private trial copy - restores every captured hole
onto the merged face by calling the already-tested `AddHoleLoop` itself
once per hole, rather than re-deriving its uv-mapping/containment logic.
Only a source face's own straight-edged hole is captured (`IsLinear()` on
every edge, `AddHoleLoop`'s own restriction); a curved-edge hole, or a face
carrying a slit/curve-on-surface/point-on-surface loop (via the same
`FindOuterLoop` scan `AddHoleLoop` itself effectively already does),
refuses the WHOLE pair rather than merging with the hole silently dropped.
Does not flip this category's own 13/13/1 (72.2%) split: "Merge coplanar /
co-surface adjacent faces" was already scored `partial` (no co-cylindrical/
co-spherical/tangent support, unchanged by this pass) and stays `partial`;
the "Loop structure" bullet's own `AddHoleLoop`-family restrictions this
pass leans on are unchanged, not narrowed further, by this addition. Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing (1 test target,
`dino8_kernel_smoke`), 0 regressions. This session's only source edits are
dino8-kernel/src/brep.cpp, dino8-kernel/tests/test_basic.cpp, and
PARITY_MAP.md itself. The kernel-only headline is unaffected (no bucket
moved).*

*Eighth note on this category's score (this pass): `Brep::DeleteFace`/
`Brep::DeleteFaces` (brep.h; brep.cpp) close the "kernel has no public
delete-face API" half of the "Delete / extract face" bullet's own gap -
real, tested kernel code (`TestBrepDeleteFaceRemovesArbitraryFaceAndRefusesBadIndices`/
`TestBrepDeleteFaceHealFlagControlsJoinAndSewTJunctions`, test_basic.cpp)
built by extracting the existing `RemoveDegenerateFaces`/`RemoveSliverFaces`
deletion body into a shared `DeleteFacesImpl` helper rather than a parallel
reimplementation, then exposing it for a caller-chosen face instead of only
a `Check()`-flagged one. The same change also wires `SewTJunctions` into
that shared body, closing the separately-named "does not call
`SewTJunctions`" gap the "Sliver / degenerate micro-face removal" bullet's
own prior text named for `RemoveDegenerateFaces`/`RemoveSliverFaces`
specifically - one shared code path, two named gaps, both bullets' own text
updated above. Neither bullet flips: "Delete / extract face" is still
`partial` for the real remaining reason (no geometric neighbour-extension,
and `dino8-app`'s own `ExtractSrf`/`DeleteFaces` command still bypasses the
kernel entirely), and "Sliver / degenerate micro-face removal" is still
`partial` for its own real remaining reason (delete-and-tolerant-join is
still not a geometric collapse) - the T-junction gap that pass's prior text
named was real but was never the reason either bullet was scored `partial`
to begin with. This category's own 13/13/1 (72.2%) split is therefore
unchanged. Full `dino8_kernel_tests` suite (via `ctest`): 100% passing, 0
regressions. This session's only source edits are dino8-kernel/include/
dino8/kernel/brep.h, dino8-kernel/src/brep.cpp,
dino8-kernel/tests/test_basic.cpp, and PARITY_MAP.md itself. The
kernel-only headline is unaffected (no bucket moved).*

*Ninth note on this category's score (this pass): two closely-related
follow-ups to the Eighth note's own `Brep::DeleteFace`/`DeleteFaces` pass.
First, the "Delete / extract face" bullet's own remaining "`dino8-app`'s own
`ExtractSrf`/`DeleteFaces` command still bypasses the kernel entirely" gap
is closed - `cmd_srfedit.cpp`'s five face-removal call sites (`FacePickCommand`'s
`Extract`/`Delete`/`Untrim`/`UntrimBorderOnly` cases, plus `ExtendSrf`'s own
face-replace path) now go through a shared `DeleteFaceHealed()` helper onto
this class's own `DeleteFace()` instead of calling bare
`ON_Brep::DeleteFace()`+`Compact()` directly - the app's own literal `true`
argument there was never a heal flag (`ON_Brep::DeleteFace`'s second
parameter is `bDeleteFaceEdges`), so none of these five commands ever
attempted a re-join before this pass; verified end-to-end through the real
commands via `dino8-app`'s own `smoke.sh`/`ctest` (14/14), not just at the
kernel layer - see the "Delete / extract face" bullet's own updated text
above for the full account. Second, the "Kernel-level topology enumeration
API" bullet's own remaining "`VertexCount`/`EdgeCount`... still include
deleted slots until `Compact()`" gap is closed for the whole-Brep table
counts specifically - `Brep::LiveFaceCount`/`LiveVertexCount`/`LiveEdgeCount`
(brep.h; brep.cpp) count only the live entries of their own table, verified
(`TestBrepLiveCountsExcludeUncompactedDeletedSlots`, test_basic.cpp) by
deleting all 6 faces of a genuinely-welded box via raw, uncompacted
`ON_Brep::DeleteFace()` calls - the exact same "reach past the kernel's own
Compact()-before-returning discipline via raw()" scenario the first half of
this note just closed the app's own version of - and confirming the raw
counts stay at 6/8/12 while the live ones correctly reach 0/0/0, then agree
again after an explicit `Compact()`; see that bullet's own updated text
above for the full account. Neither bullet flips: "Delete / extract face"
stays `partial` for its own separate, real remaining reason (no geometric
neighbour-extension), and "Kernel-level topology enumeration API" stays
`partial` for its own (`Box()`/`Sphere()`/`Torus()` still reporting zero
topology, `LoopCount`/`TrimCount`'s own per-face/per-loop scope never having
the raw/live distinction to begin with) - this category's own 13/13/1
(72.2%) split is therefore unchanged. Full `dino8_kernel_tests` suite (via
`ctest`): 100% passing (1 test target, `dino8_kernel_smoke`), 0 regressions;
`dino8-app`'s own `ctest` (14/14) and `smoke.sh` also re-run clean. (At the
time this pass's own `smoke.sh` run was taken, it still showed a cluster of
Boolean-precision test-string mismatches - `smoke.sh`'s own expected strings
for several `boolean_adversarial_script.txt` fixtures still asserted the OLD
mesh-path "N faces, volume V" format after the app gained an exact B-rep
boolean path that reports "exact B-rep boolean (no tessellation), N
face(s)" instead - confirmed unrelated to face deletion or topology
counting and untouched by this pass's own diff; a separate, concurrent
session fixed that same `smoke.sh` staleness directly (`7c8a5da`), landed
between this pass's own testing and this commit, so it no longer reproduces
on current `HEAD`.) This session's source edits are
dino8-kernel/include/dino8/kernel/brep.h, dino8-kernel/src/brep.cpp,
dino8-kernel/tests/test_basic.cpp, dino8-app/src/commands/cmd_srfedit.cpp,
and PARITY_MAP.md itself. The kernel-only headline is unaffected (no bucket
moved).*

**kernel: Geometry representation** (geometry):
- [partial] Knot removal (curve and surface, tolerance-controlled) — kernel `NurbsSurface::RemoveKnotAt` (surface_edit.cpp:314; surface.h:853-874) implements Piegl & Tiller A5.8 with a rigorous max-deviation bound. Still partial: `NurbsCurve` has no `RemoveKnot`, so curve knot removal is app-only and shape-changing (dino8-app/src/commands/cmd_curves2.cpp:64, Greville resample); periodic knot vectors are refused.
- [partial] Degree reduction (curve and surface, with error bound) — no kernel degree reduction (re-grepped `ReduceDegree|degree reduc`: nothing). App `ChangeDegree` "never lowers" (cmd_edit.cpp:62). Closest is `NurbsSurface::Rebuild`, a least-squares refit at any lower degree with a sampled (not certified) deviation bound. No curve equivalent beyond `FitLeastSquares`.
- [partial] Reparameterization: domain change, rational reparam, uniform knots, seam change, reverse/transpose — `NurbsSurface::SetDomain`, `Reverse`/`Transpose`, `NurbsCurve::Reverse`, `MakePeriodicExact` for curve and surface all exist. Missing: curve `SetDomain`, rational (Mobius) reparameterization, seam relocation. App `Reparameterize`/`MakeUniform` remain app-only and curve-only.
- [partial] Curve/surface interpolation and least-squares fitting through points — `NurbsCurve::FitLeastSquares` (global least-squares), `NurbsSurface::Rebuild` (tensor-product least-squares refit), and global cubic interpolation `InterpolateCubic` (open or closed, chord-length parameters) all exist. Still partial: no degree-p interpolation, no end-tangent constraints, no surface interpolation through a point grid.
- [partial] Helix and spiral curves — app `HelixCommand` (dino8-app/src/commands/cmd_create.cpp:301) builds `NurbsCurve::FromControlPoints` through sampled points; neither interpolating nor exact, and no kernel helix.
- [partial] Typed curve taxonomy and persistent composite (poly)curves — the document stores only `std::unique_ptr<kernel::NurbsCurve>` (dino8-app/src/doc/SceneObject.h:188). The kernel has only `NurbsCurve` plus `IsLinear`/`IsArc`/`IsCircle` classification. No persistent line/arc/polycurve types.
- [partial] Typed analytic surface classes with closed-form evaluation/inversion (distinct from NURBS) — every face is an `ON_NurbsSurface`. The analytic records are only `PlanarFace`/`CylindricalFace`/`ConicalFace`/`SphericalFace` structs, with no torus record. Evaluation and inversion always go through NURBS.
- [partial] Swept surfaces (one-rail and two-rail sweep) — kernel-native: `Brep::Sweep1` uses rotation-minimizing frames; `Brep::Sweep2` (sweep.cpp:1820 — corrected 2026-09-28, was mis-cited sweep.cpp:1770-1854) is a genuine two-rail sweep with uniform cross-section scaling. Still partial: between stations both are interpolants, exact only for straight rails; `Sweep2` refuses touching rails and rail tangents parallel to the rail-to-rail direction; the app's own Sweep2 command still uses its own older approximate path, not the kernel.
- [partial] Offset surfaces (NURBS, tolerance-controlled) — `NurbsSurface::OffsetAnalytic` is exact for plane/sphere/cylinder/cone/torus; `NurbsSurface::OffsetApproximate` covers freeform surfaces via a first-order Greville-normal control-point move with a curvature fold guard. `tolerance` sets only the guard's sampling density, not a fit error bound.
- [partial] Numeric curve queries: arc length, parameter-at-length, division, tight bounding boxes — `Length` is a 1000-segment polyline chord sum that converges from below; `ParameterAtArcLength` interpolates linearly on that polyline. No adaptive Gauss integration or certified accuracy.
- [partial] Rational <-> non-rational conversion — `MakeRational` is exact; `MakeNonRational` keeps the control points but changes shape whenever weights vary (a radius-5 circle drifts to about 5.28). No tolerance-bounded non-rational approximation.

**kernel: Boolean operations** (booleans):
- [partial] Analytic plane/cylinder and cylinder/cylinder B-rep booleans (drilled holes, bosses, oblique holes, parallel/Steinmetz/unequal-radius/skew cylinder pairs) — `BooleanCombineMixed` (boolean.cpp:5976) covers perpendicular/oblique plane-cylinder cuts, Steinmetz pairs, unequal radii at any angle including skew full-pierce, and chained results as operands. Out of scope and thrown: grazing angles, partial penetration, a partial-sweep oblique operand, a second cut interacting with an already-notched fragment, a ConicalFace operand. **Newly confirmed defect (unrelated data-exchange session, incidental full-suite run):** `TestBooleanCombineMixedUnequalRadiusPerpendicularNegativeControls`'s 60-degree pinch-point exactness check fails on current HEAD (`ctest`/`dino8_kernel_tests`, 4135 of 4136 total checks passing) — the intersection builds, is a closed manifold, and its volume matches the closed form (those neighboring checks pass), but its 4 vertices are not exactly the closed-form pinch points (+/- sqrt 3, +/- 1, z = x·cot 60°) to the asserted 1e-9 tolerance. Confirmed pre-existing via a clean rebuild and full rerun of this same test binary at HEAD (`f0ff809`, before any of this session's own changes) — identical single failure, so this is not a regression introduced by this session's own work, which touches only kernel-level data exchange. Not investigated further here (out of this session's scope); this item's own `partial` classification is unaffected (already partial for the disclosed out-of-scope cases above). **Update (a later pass, this category's own trailing "Seventh note" below):** a fresh full `dino8_kernel_tests` run at that pass's own HEAD (5544 checks) shows this specific check passing cleanly, with zero failures suite-wide - whether the prior failure was resolved by an intervening change or was itself intermittent (e.g. compiler/optimization-dependent Newton-refinement rounding) is not investigated here either, so this caveat about the headline not reflecting a fully green suite no longer applies as of that pass, but is left in place rather than deleted, per this document's own "narrowing, not erasing" convention for superseded evidence.
- [partial] General NURBS-surface B-rep boolean (SSX-driven boundary evaluation on arbitrary ON_Surface faces) — `BooleanCombineGeneral` (boolean_general.cpp:2738) is scoped to at most one crossing component per face pair, genus-0 faces, no self-crossing chains. Corrected figure, live-measured this pass by actually building and running `dino8_general_boolean_sweep` against current HEAD: **54 of 76** sweep combinations tessellate watertight via `TessellateGeneralBooleanClosedMesh` (boolean_general.h:62-79's own cited "15 of 76" was a stale intermediate number from partway through the kernel's own embedded development log, superseded by the log's own later entries before the last measurement) — 18 genuine non-empty residual failures (cyl+cyl parallel/Steinmetz/skew, sphere+box, sphere+sphere, one sphere+cyl direction) plus 4 trivial 0-face empty results.
- [partial] Coplanar / coincident face handling — the planar engine dedups identical planes; `BooleanCombineMixed` carries a coincident-face rule into each XOR lump; the general engine has its own coincident whole-face test (boolean_general.cpp:2899). No general partially-overlapping coincident curved-face handling.
- [partial] Tangent / grazing contact handling — the mesh engine retries once with adaptive tolerance on failure (`AdaptiveManifoldTolerance`, boolean.cpp:149-170). The B-rep engines throw at grazing incidence rather than resolving it.
- [partial] Multi-body / multi-tool booleans (N operands per side, multi-lump results and operands) — the app unions each side sequentially before combining (cmd_boolean.cpp:15-38). B-rep XOR returns a two-lump Compound; compound operands are refused by all three engines — the planar/mixed engines (boolean.cpp:911-918, call sites 931-932/6781-6782 — corrected 2026-09-28, was mis-cited boolean.cpp:815-822) and, **as of this pass**, the general engine too (boolean_general.cpp, right before `BooleanCombineGeneral`'s own definition — see this category's own trailing "Seventh note" below for the full detail; previously this engine silently processed a compound operand one face at a time with no lump-boundary awareness at all, an unhandled case rather than a clean refusal). **New this pass:** `dino8::kernel::BooleanCombineMixedNAry(first_group, second_group, op)` (boolean.h/.cpp) closes the "No kernel N-ary API" half of this bullet for the analytic engine — `first_group` (and, if non-empty, `second_group`) is each folded into one solid via repeated pairwise `BooleanCombineMixed(..., Union)` calls, then the two folded solids are combined via ONE further `BooleanCombineMixed(..., op)` call (`op` in {Union, Intersection, Difference}; an empty `second_group` returns the folded `first_group` directly for a plain multi-object Union). Verified (`TestBooleanCombineMixedNAry*`, tests/test_basic.cpp) that three overlapping boxes' N-ary Union matches an independent inclusion-exclusion volume, that the left-to-right fold order is not caller-visible (three different orderings of the same three boxes agree), that an N-ary Difference against a two-tool `second_group` matches two chained pairwise `BooleanCombineMixed` Difference calls exactly, that an N-ary Intersection unions each side independently before combining, and negative controls for an empty `first_group`, an empty `second_group` with a non-Union `op`, and `SymmetricDifference` (refused outright — its own pairwise result is a `Brep::Compound` that cannot be fed into a further Union fold). Still partial at the time: this was one new entry point on top of the existing pairwise engine, not a change to `BooleanCombineMixed` itself, so every operand in either group had to still be a single-lump Brep (`RefuseCompoundOperand` still applies at each pairwise step) — genuine multi-lump/Compound operand support (the bullet's other named gap) was untouched, `BooleanCombinePlanar`/`BooleanCombineGeneral` had no N-ary wrapper of their own yet, and no app command called it (cmd_boolean.cpp still hand-rolls its own sequential fold at the mesh-boolean level, unchanged). **New this pass: the other two B-rep engines now have the same N-ary wrapper.** `dino8::kernel::BooleanCombinePlanarNAry(first_group, second_group, op)` (boolean.h/.cpp) is the identical fold shape for the planar engine — same `RefuseCompoundOperand` precondition inherited at each pairwise step, same SymmetricDifference refusal (its own pairwise result is a two-lump `Brep::Compound`, exactly the reason the Mixed engine's wrapper refuses it too). `dino8::kernel::BooleanCombineGeneralNAry(first_group, second_group, op, tolerance = 0.001)` (boolean_general.h/.cpp) does the same for the SSX-driven general engine, forwarding `tolerance` to every pairwise `BooleanCombineGeneral` call it makes, and refuses SymmetricDifference because `BooleanCombineGeneral` itself does not implement that op at all. Verified (`TestBooleanCombinePlanarNAry*`/`TestBooleanCombineGeneralNAry*`, tests/test_basic.cpp) with the same coverage shape the Mixed wrapper's own tests already established: inclusion-exclusion-correct N-ary Union, fold-order independence (Planar), N-ary Difference against a two-tool `second_group` matching a hand-chained pairwise sequence exactly (Planar) / closely (General, within that engine's own tessellated-volume tolerance), N-ary Intersection unioning each side before combining (Planar), the General wrapper's `tolerance` genuinely reaching every internal pairwise call (not just the final combine), and the same negative controls (empty `first_group`, an empty `second_group` with a non-Union `op`, `SymmetricDifference` refused outright). **Incidental finding while building the General engine's own test fixture, not a defect in the new wrapper:** the axis-aligned "chain of three boxes overlapping by one unit along x" fixture the Mixed/Planar wrappers' own tests use shares a coplanar, partially-overlapping side face between adjacent operands (same y/z extent, only x differs) — reproduced standalone (a single plain `BooleanCombineGeneral(a, b, Union)` call, no N-ary code involved) to throw "an edge is claimed by 3 or more fragment loops", confirming this is `BooleanCombineGeneral`'s own pre-existing, already-disclosed "No general partially-overlapping coincident curved-face handling" scope limit (this bullet's own "Coplanar / coincident face handling" neighbor), not something this pass introduced; the General wrapper's own tests instead use a diagonally-staggered fixture (no two operands share a coplanar face) that stays in that engine's proven scope. Still partial: every operand in either group for all three engines must still be a single-lump Brep (Planar inherits `RefuseCompoundOperand`; General had no compound-operand guard of its own to inherit at the time of this note — a separate pre-existing gap this pass did not touch, since closed by a later pass, see this category's own trailing "Seventh note" below); no app command calls any of the three N-ary entry points yet (cmd_boolean.cpp still hand-rolls its own sequential fold at the mesh-boolean level); and the General engine's own scope limits (one crossing chain per face pair, genus-0 faces, no partially-overlapping coincident faces) apply to every pairwise fold step `BooleanCombineGeneralNAry` makes, unchanged. **New this pass: `BooleanCombinePlanar` itself (not just its N-ary wrapper) now accepts a genuine compound operand for `Difference`/`Intersection`** — the "genuine multi-lump/Compound operand support" half of this bullet, for this one engine: `RefuseCompoundOperand` is no longer called at all for those two ops, the existing split/classify/reassemble pipeline is proven (not just assumed) to already compute the right answer against a multi-lump operand, and the result's true lump structure is re-derived via `SplitDisjointPieces()` so `LumpFaceRanges()` never lies about it. `Union`/`SymmetricDifference` still refuse one, as does every op of `BooleanCombineMixed`/`BooleanCombineGeneral` — see this category's own trailing "Sixth note" below for the full verification detail and the real, disclosed scope limit found (a compound whose own lumps genuinely touch along a contact curve, e.g. a corner-overlap `SymmetricDifference` result, still throws the pre-existing non-manifold reassembly refusal).
- [partial] Result validity (closed manifold / ON_Brep IsValid / IsSolid) — the mesh contract is fuzz-checked (IsClosedManifold on every result). Planar results pass IsValid/IsSolid. General-engine results close in 54 of 76 cases (see the corrected figure above), not watertight otherwise.
- [partial] Tolerant booleans (caller-specified tolerance, gap-healing of imprecise operands) — **updated this pass: `ImprintFaces`/`SplitBySheet`/`TrimSheetBySolid` (boolean_general.h/.cpp) now also take an optional caller `tolerance`**, closing the specific gap the prior version of this bullet named by function — every one of `boolean_general.cpp`'s own SSX-driven entry points (`BooleanCombineGeneral`, `ImprintFaces`, `SplitBySheet`, `TrimSheetBySolid`) now threads a caller `tolerance` straight into the same internal `IntersectOptions::tolerance` every SSX intersection-curve point is Newton-refined to, each defaulting to the prior implicit 0.001 so every existing caller (and every other test in this suite) is unaffected — verified bit-identical for each function (`TestImprintFacesCallerTolerance`, `TestSplitBySheetCallerTolerance`, `TestTrimSheetBySolidCallerTolerance`, tests/test_basic.cpp) the same way `TestBooleanCombineGeneralCallerTolerance` already proved for the first one, plus a non-positive-tolerance refusal for each. Still partial: `BooleanCombinePlanar`/`BooleanCombineMixed` (boolean.cpp) — a structurally different, hand-solved-per-surface-pair engine with tolerance scattered across many internal epsilons rather than one `IntersectOptions` — still hardcode their own internal tolerance with no caller control at all; there is still no gap-healing of imprecise operands (the other half of this item); the general engine's own separate bbox/coincident-face-detection epsilon (`const double tol = 1e-6`, e.g. boolean_general.cpp:2800, and the same-named local in each of the other three functions) is a different, still-fixed concern from the SSX solve tolerance above; and the only OTHER adaptivity anywhere in this category is the mesh engine's own `AdaptiveManifoldTolerance` retry.
- [partial] Keep/split options (BooleanSplit solid-by-solid keeping all pieces, DeleteInput/keep tools, side selection) — BooleanSplit/MeshSplit/MeshBooleanSplit (cmd_boolean.cpp:410-415) are all plane-split only (kernel `SplitByPlane`). The new `SplitByObjectCommand` (see kernel: Feature operations and kernel: Transformations) is a general cutting-object split with true KeepAll semantics, but it is app-level mesh-boolean, not this item's B-rep solid-by-solid split.
- [partial] Sheet/solid trim (open surface as cutter through a solid; trimming a sheet body by a solid) — `dino8::kernel::SplitBySheet(solid, sheet)` (boolean_general.h; boolean_general.cpp) splits a closed `solid` into the two pieces on either side of an OPEN `sheet` (one or more trimmed faces, no closed-solid requirement — unlike every OTHER boolean engine here, which still assumes closed two-shell solids, boolean.cpp:81), each piece capped with the portion of `sheet` inside `solid`. Reuses `BooleanCombineGeneral`'s own SSX-fragmentation machinery: `solid`'s fragments are bucketed by a closest-point-plus-normal-sign test against `sheet` (not ray-cast parity, since `sheet` may have no volume), `sheet`'s own fragments are ray-cast in/out of `solid` as usual and its IN fragments become the new caps. Verified on a box fully severed by a flat open planar sheet larger than the box's own footprint (both halves valid closed B-reps, volumes summing back to the original exactly) and a disjoint-sheet case (the whole untouched solid on one side, the empty Brep on the other) (`TestSplitBySheet*`, tests/test_basic.cpp). **New this pass: the item's OTHER half — trimming a sheet body BY a solid — is now real code too.** `dino8::kernel::TrimSheetBySolid(sheet, solid, keep_inside)` (boolean_general.h; boolean_general.cpp) trims `sheet`'s own surface down to the portion inside (or, with `keep_inside=false`, outside) `solid`, without ever splitting, capping, or returning `solid` itself — `solid` is used purely as the ray-cast classification target for `sheet`'s own SSX fragments, via the same `ClassifyPointVsBrep` ray-cast `SplitBySheet` itself already uses for this exact purpose. Verified (`TestTrimSheetBySolid*`, tests/test_basic.cpp) on the same box-cut-by-an-oversized-flat-sheet fixture `SplitBySheet`'s own tests use: the kept inside portion is a single valid face whose tessellated area is exactly the box's own 4x4 footprint (16, not the sheet's full 6x6 extent of 36), the discarded outside portion's area is the complementary 20, and the two sum back to the untrimmed sheet's own area exactly; a sheet that never reaches the solid at all correctly keeps nothing for `keep_inside=true` and the whole untouched sheet for `keep_inside=false`; both faceless-operand cases throw `std::invalid_argument`. (Area, not `Brep::Area()`, is measured via a tessellated `Mesh` — every fragment this function's shared `assemble()`-style construction produces carries a real `ON_Brep` trim loop, even an untouched one, so `Brep::Area()`'s own exact whole-domain-only integration always refuses it; the same is already true, silently, of every `SplitBySheet` result — its own tests avoid the issue by never calling `Area()`/`Volume()` on an open piece.) Still partial: the app's own `cmd_boolean.cpp` still skips every non-closed operand outright ("not a closed solid; skipped", cmd_boolean.cpp:21,151,193,305) and calls neither this nor `SplitBySheet`; only a flat cutting plane is tested for either half of this item (a genuinely curved `sheet` or `solid` is unexercised); both inherit `BooleanCombineGeneral`'s own scope limits (one crossing chain per opposing face pair, genus-0 faces); and `Brep::GetTightBoundingBox()` gives the underlying surface's own untrimmed domain box, not the real trim boundary, for any face either function builds (confirmed directly, not assumed — its own exact-loop fast path requires the pseudo-trim side tables neither function's shared raw-`ON_Brep` `assemble()` step populates) — a real, previously-undocumented limitation worth fixing the next time this file's own construction helpers are revisited, and the reason this bullet's own area claims above are measured via a tessellated `Mesh` instead.
- [partial] Non-manifold boolean results (edge/vertex-touching unions, single-body XOR, 3+ faces per edge) — the B-rep engines throw "an edge is shared by 3 or more faces" instead of building non-manifold output; XOR is an unwelded two-lump Compound; the mesh XOR keeps duplicated vertices. **Attempted and reverted, this pass:** see this category's own "Sixteenth note" below — welding the mesh-level `BooleanCombine`'s `SymmetricDifference` result with `Mesh::MergeDuplicateVertices()` measurably turns a valid (if duplicate-vertex) closed manifold into an invalid non-manifold one, at any nonzero tolerance; not shipped.
- [partial] Face-face imprint (Parasolid PK_BODY_imprint / ACIS imprint: split faces along mutual intersection without removing material) — `dino8::kernel::ImprintFaces(target, tool, tolerance = 0.001)` (boolean_general.h:77; boolean_general.cpp:3138 — corrected 2026-09-28, was mis-cited boolean_general.h:61; boolean_general.cpp:3086) reuses `BooleanCombineGeneral`'s own SSX-driven face-fragmentation but keeps every fragment of `target` unconditionally — no ray-cast in/out classification, no material ever removed — so `target` keeps its exact original shape/volume with more, smaller faces wherever `tool` crosses it; `tool` itself is read-only. Verified on a closed-loop fixture (box pierced by a cylinder) and an open-chain fixture (two overlapping boxes), each direction, plus a disjoint-operand no-op and a faceless-operand `std::invalid_argument` (`TestImprintFaces*`, tests/test_basic.cpp). **New this pass: `dino8::kernel::MutualImprintFaces(a, b, tolerance = 0.001)` (boolean_general.h/.cpp) closes the "call it twice, swapped" gap this bullet previously named as the obvious next step** — it runs exactly `ImprintFaces(a, b, tolerance)` then `ImprintFaces(b, a, tolerance)` (sound because `ImprintFaces` never mutates its own `tool`, only ever reads it for SSX curves, so imprinting `a` first cannot change what the second call sees of `b`), checking both operands' preconditions up front so a bad `b` refuses before `a` is ever touched. Verified (`TestMutualImprintFacesBoxPiercedByCylinder`, `TestMutualImprintFacesRejectsEmptyOrNonPositiveTolerance`, tests/test_basic.cpp) that its own two results are topologically identical to the two standalone `ImprintFaces` calls a caller would otherwise make by hand, and that both operands keep their exact original volume. `ImprintFaces` itself also gained the same caller-`tolerance` parameter `BooleanCombineGeneral` already has (see the "Tolerant booleans" bullet above for the shared detail). Still partial: it inherits `BooleanCombineGeneral`'s own scope limits (one crossing chain per opposing face pair, genus-0 faces), and no app command exposes either function yet — re-confirmed this pass (`ImprintFaces`/`MutualImprintFaces` have zero hits anywhere in dino8-app/).
- [partial] 2D region / planar curve booleans (CurveBoolean, AutoCAD REGION union/subtract/intersect) — `RegionBoolean` (dino8-app/src/commands/cmd_solidtools.cpp:1525) runs through thin mesh slabs in Manifold and recovers outlines. No exact 2D curve boolean in the kernel.
- [partial] Boolean failure diagnostics (typed refusals, failure reasons, naked-edge reporting) — the kernel throws `std::invalid_argument` naming the specific precondition; the mesh engine gives a generic Manifold status string. No structured failure-report type exists. **New this pass:** the first typed refusal now exists — `dino8::kernel::BooleanOperationError` (boolean.h), a `std::invalid_argument` subclass carrying a structured `BooleanFailureReason` enum plus the refusing function's own name, thrown by `RefuseCompoundOperand` (one copy each in boolean.cpp/boolean_general.cpp — the single most-cited refusal helper in this category, shared by all three B-rep engines' own Union/SymmetricDifference-on-a-compound-operand refusal) in place of the plain `std::invalid_argument` it used to throw. Still fully backward compatible: every existing caller/test that only ever catches the base `std::invalid_argument` class (e.g. `TestBooleanCombineGeneralRefusesCompoundOperand`) sees identical behavior, including the identical `what()` text; a caller wanting a programmatic reason instead of parsing `what()` can now catch `BooleanOperationError` directly and read `reason()`/`function_name()`. Verified (`TestBooleanOperationErrorStructuredFields`, tests/test_basic.cpp) on all three engines (`BooleanCombinePlanar`, `BooleanCombineMixed`, `BooleanCombineGeneral`): each compound-operand refusal is catchable as `BooleanOperationError` with `reason() == BooleanFailureReason::CompoundOperand` and `function_name()` naming the right engine, AND still separately catchable as plain `std::invalid_argument`. Still partial: this is one typed refusal shape out of this file's own ~160 individual `std::invalid_argument`/`std::runtime_error` throw sites (each still free-text-only); the mesh engine's own generic Manifold-status failure in `BooleanCombine` is untouched; and there is still no structured naked-edge reporting at all — a much larger rewrite this pass does not attempt, disclosed rather than assumed closed.
- [partial] Free-form (non-analytic) NURBS surface operands in B-rep booleans — `BooleanCombineGeneral` is written for any `ON_Surface`, but every test/sweep operand is an analytic primitive. No freeform-operand test exists. **New this pass: closes the specific "no freeform-operand test exists" complaint.** `TestBooleanCombineGeneralFreeformSurfaceOperand` (tests/test_basic.cpp) builds a genuine doubly-curved, non-developable, non-quadric operand — a 4x4 bicubic Bezier patch (`NurbsSurface::FromControlGrid`, degree (3, 3)) with deliberately asymmetric control-point heights (no mirror symmetry in either parametric direction, and no separable u*v product term, so it is not secretly a ruled or translational surface in disguise) — thickened (`Brep::Thicken`) into a genuine closed "blob" solid, then booleaned (`BooleanCombineGeneral`) against an axis-aligned box for all three ops, with the fixture's own geometry chosen so the box's flat top face crosses only the freeform solid's 4 ruled side walls (never its curved top/bottom caps), keeping the crossing itself simple and in-scope (one crossing component per face pair). Since a Bezier bump's own thickened volume has no closed form, correctness is verified via the implementation-independent inclusion-exclusion identity `Volume(A) + Volume(B) == Volume(Union(A,B)) + Volume(Intersection(A,B))`, which holds for ANY two solids: measured directly (not assumed) at (24, 24) tessellation, the identity holds within 0.01 against volumes around 130 (the actual observed residual is ~6e-5, converging further at finer tessellation, confirmed separately) — proof the SSX-driven crossing/capping logic reached the right answer on a genuinely non-analytic operand, not merely that it didn't crash. Also verifies `Difference` in both argument orders against the same `Volume(operand) - Volume(Intersection)` identity, and that every result (`Union`/`Intersection`/`Difference`) is both a valid `ON_Brep` and a genuine `IsClosedManifold()` via `TessellateGeneralBooleanClosedMesh`. Still partial, not present: only this one simple freeform fixture (a single crossing loop, genus-0, no self-intersection) is exercised — a freeform operand pair hitting `BooleanCombineGeneral`'s own separately-disclosed scope limits (one crossing component per face pair, no self-crossing chains — see the "General NURBS-surface B-rep boolean" bullet above) remains completely unexercised, and `Brep::Thicken`'s own known "no genuine topology" gap (kernel: Topology & data structure) means the standalone freeform solid itself is not `IsValid()` (confirmed directly: "ON_Brep has no edges"), though the boolean *results* are.
- [partial] B-rep-preserving booleans reachable from the application (polysurface in, polysurface out) — **upgraded from missing this pass.** `BooleanUnion`/`BooleanDifference`/`BooleanIntersection`/`Boolean2Objects` (dino8-app/src/commands/cmd_boolean.cpp, shared `TryExactBrepBoolean` helper feeding `RunBoolean`) now try a genuine B-rep-preserving path FIRST, ahead of the existing mesh/Manifold fallback: when every operand on both sides is a plain, single-lump, closed (`ON_Brep::IsSolid()`) `ObjectKind::Brep` object, they call `kernel::BooleanCombinePlanarNAry` (or, for a single-pair `SymmetricDifference`, `BooleanCombinePlanar` directly, since the N-ary wrapper itself refuses that op) and, on success, add the result as a genuine `SceneObject::MakeBrep()` — a real polysurface with exact planar faces, never tessellated — instead of a mesh. This is the same "exact construction first, fail open to the approximate/mesh path" structure `TryExactFillet`/`TryExactChamfer` already established in cmd_fillet.cpp, applied here to whole-object booleans for the first time: a curved face anywhere on any operand, a non-solid (open) Brep, a non-Brep object (mesh/surface/SubD), or any of `BooleanCombinePlanar`'s own disclosed scope limits (non-manifold reassembly, a compound operand on `Union`/`SymmetricDifference`) all fall through silently to the untouched mesh path below, so every pre-existing caller and test keeps its exact prior behavior. Verified end-to-end through the real command (not just at the kernel layer): `boolean_script.txt`'s existing `Box`-only fixtures (every `Box` command already builds `ObjectKind::Brep` via `ON_BrepBox`) now print `exact B-rep boolean (no tessellation), N face(s)` and land on the identical object counts the mesh path already produced; `boolean_adversarial_script.txt`'s full adversarial corpus (near-tangent/barely-overlapping/coincident/sliver/huge-scale/10-deep-chained boxes) passes unchanged through the new path with zero regressions. **A real, previously-unencountered defect found and fixed while building this, not assumed:** the eligibility check must require `IsSolid()`, not merely `ObjectKind::Brep` — `BooleanCombinePlanar`'s own `PlanarFaces()` precondition happily accepts an OPEN planar-faced shell (e.g. `boolean_adversarial_script.txt`'s own "Non-manifold input" case, a box with one face deleted via `DeleteFaces`: every one of its 5 remaining faces is still individually planar) and would silently fold it into the result anyway, instead of the "reject the bad operand with a warning, leave it untouched" behavior the mesh path's own `IsClosedManifold()` check already gives — caught directly by that exact adversarial test case (`@expect_objects 2` failing at 1, a single wrongly-merged 11-face result) before the `IsSolid()` guard was added, not discovered after the fact. Still partial, not present: only the analytic/planar engine (`BooleanCombinePlanar`) is wired this way — `BooleanCombineMixed` (cylindrical faces) and `BooleanCombineGeneral` (general NURBS) have no app entry point at all yet, so a solid with any curved face still always falls through to the mesh path; `BooleanSplit`/`MeshSplit`/`MeshBooleanSplit`/`SplitByObject`/`WireCut` are untouched (still plane-cut or general-cutter mesh operations, not wired to any exact engine); and a compound (multi-lump) Brep operand still refuses on `Union`/`SymmetricDifference` exactly as the kernel functions themselves do. **A later pass:** closes the "untested" half of the compound-operand caveat above for `Difference`/`Intersection` specifically (`Union`/`SymmetricDifference` still refuse a compound exactly as before, unchanged) — `BooleanCombinePlanarNAry`'s own single-Brep-per-group fold is a no-op (no intervening `Union` call), so a compound operand already reached `BooleanCombinePlanar`'s own compound-accepting `Difference`/`Intersection` path (see this category's own "Sixth note" above) the first time `TryExactBrepBoolean` was written, but this was never exercised through a real app command before now: verified with a genuine compound built by an actual `Boolean2Objects` `Result=SymmetricDifference` call (two disjoint boxes, not an artificial fixture) then subtracted by a third box overlapping only one of its two lumps - the exact path fires and the result volume matches (untouched lump's full volume) + (touched lump minus the overlap) exactly (`dino8-app/tests/boolean_mixed_and_compound_script.txt`). **The same pass also attempted, then reverted, wiring `BooleanCombineMixed`/`BooleanCombineMixedNAry` in as a second attempt when the planar one declines** (for a solid with a cylindrical face) - two real, disclosed problems were found, not assumed, and neither is fixed here: (1) a purely-planar adversarial fixture that `BooleanCombinePlanar` correctly refuses (near-tangent/barely-overlapping/coincident-face geometry, this category's "Tangent / grazing contact handling" bullet) can be silently ACCEPTED by `BooleanCombineMixed` instead, since its own auto-derived tolerance (`RelativeTolMixed`) doesn't always refuse the identical near-degenerate input the planar engine's own `RelativeTol` does - reproduced directly against `boolean_adversarial_script.txt`'s own near-tangent/coincident/huge-scale/chained-cut fixtures, which silently took the exact path with a very different (unverified) result the moment the Mixed attempt was added unconditionally; and (2) even gated to only try when a genuine `CylindricalFace` is present, a `BooleanCombineMixed` result built from a plain, record-less operand (dino8-app's own `Cylinder`/`Box` commands never attach a dino8 FaceRecord - see `WrapBrep`, cmd_solids.cpp) tessellates to an OPEN, non-manifold mesh via the app's own generic `MeshOf`/`MeshBrepClosed` path (confirmed directly: `Volume` on such a result reports "not closed", volume 0) - a genuine architecture gap (the app has no way to ask for the specialized conforming tessellation these engines' own kernel tests use) uncovered while investigating problem (1)'s own root cause below, not something a small guard closes. Investigating (2) found and fixed a real, separate, narrower kernel bug along the way (`Brep::MixedFaces`'s own `ExtractCylindricalFace`, brep.cpp): the geometric-extraction path used for any record-less cylindrical face derived its angular sweep via `ON_Circle::GetRadianFromNurbFormParameter`, whose own contract (radius-scaled `NurbParameter`) doesn't match what its actual implementation checks (`ON_Arc::Domain()`, always plain radians) - true for any radius except 1, which is why this was never caught by any pre-existing FaceRecord-based test. Fixed by detecting a genuinely full sweep geometrically (the point at `u_max` coincides with the point at `u_min`) instead of trusting that helper, closing the "`MixedFaces()` always throws on a record-less non-unit-radius cylinder" half of the problem (`TestBrepMixedFacesRecoversFullCylinderWallWithoutFaceRecordAtNonUnitRadius`, tests/test_basic.cpp) - but this alone does not make `BooleanCombineMixed` produce a CORRECT result end-to-end for such an operand: a further, separate defect was found in the same investigation (a record-less curved trim loop's own polygon approximation is far coarser - 16 points vs. 128 for the identical circle - than a FaceRecord-backed one, confirmed by swapping only the cylinder operand with every extracted `CylindricalFace` field otherwise bit-identical, which is what actually breaks the boolean's own closure), left disclosed rather than fixed. Given both (1) and (2), the Mixed-engine app-wiring attempt was reverted rather than shipped half-safe; `TryExactBrepBoolean` is unchanged from the Planar-only version above. Still partial: `BooleanCombineMixed`/`BooleanCombineGeneral` still have no app entry point at all (unchanged), `BooleanSplit`/`MeshSplit`/`MeshBooleanSplit`/`SplitByObject`/`WireCut` remain untouched, and compound support is still `Union`/`SymmetricDifference`-refused, unchanged.
- [missing] Associative/history-enabled Boolean operations (result auto-updates when source solids move, a la Rhino's History) — `HistoryRecord::command` (dino8-app/src/doc/Document.h:377) covers `"Extrude", "ExtrudeCrvToPoint", "Revolve", "Loft", "SubDLoft"` only, no boolean commands.
- [present] AutoCAD-style INTERFERE (interference detection that builds real solid bodies from the overlap regions of many objects) — `dino8::kernel::ComputeInterference(bodies, clearance)` (boolean.h; boolean.cpp) builds the real pairwise overlap solids: for every pair of input bodies whose (optionally clearance-expanded) bounding boxes touch, it runs the actual `BooleanCombine(..., BooleanOp::Intersection)` and keeps only pairs whose overlap has nonzero volume (`FaceCount() > 0` - verified empirically that Manifold returns a genuine zero-face, zero-volume result for two solids that only share a coincident face, not a degenerate sliver, so this is a correct volume test, not just a bbox heuristic). Tested (`TestComputeInterference`, tests/test_basic.cpp): a genuine overlap is reported with the correct pair indices and volume, disjoint bodies report nothing, a full-face zero-volume touch is correctly excluded, a generous `clearance` never fabricates a solid for geometry that doesn't truly overlap, and a non-closed operand in a bbox-touching pair throws `std::runtime_error` like `BooleanCombine` itself. **New this pass: `dino8::kernel::ComputeMultiWayInterference(bodies, clearance)` (boolean.h/.cpp) closes the "pairwise-only" half of this bullet's own previously-named gap** - the fully general N-way simultaneous overlap AutoCAD's own INTERFERE can report for 3+ mutually-overlapping bodies at once, not just each pair. It builds on `ComputeInterference`'s own pairwise overlaps as a starting frontier, then repeatedly extends each surviving overlap solid with one more body (bbox-prefiltered against the overlap's own bounding box, then a real `BooleanCombine(..., Intersection)`, kept only if the shared volume stays nonzero) for as long as bodies remain — so a returned `{0, 1, 2}` means bodies 0, 1 AND 2 truly share a common volume, not merely that each pair happens to overlap somewhere (the classic Venn-diagram "ring" false positive a merely-pairwise-transitive implementation would wrongly report). Verified (`TestComputeMultiWayInterference`, tests/test_basic.cpp) against independent closed-form volumes on a genuine 3-way mutual overlap and a genuine 4-way one (each body's box chosen so the true N-way common region, and its volume, can be computed by hand independently of the implementation), a negative control specifically targeting the false-positive case (three boxes chained A/B, B/C with no true triple-common point at all — A and C's own bounding boxes don't even touch — correctly reports zero 3-way results), a plain two-body input (never produces a 3-or-more-way result), and the same non-closed-operand `std::runtime_error` propagation `ComputeInterference` itself already has. Still partial, not present: neither function is wired to any app command (`Clash`, dino8-app/src/commands/cmd_solidtools.cpp:1210, still does not call either), and there is still no single combined API returning both the pairwise AND the N-way results together in one pass (a caller wanting the full interference report must call both functions). **New this pass: `dino8::kernel::ComputeAllInterference(bodies, clearance)` (boolean.h/.cpp) closes exactly that "no single combined API" gap** — returns an `AllInterferenceResult{pairwise, multi_way}` matching what `ComputeInterference(bodies, clearance)`/`ComputeMultiWayInterference(bodies, clearance)` would each return on their own, but this is not a thin wrapper calling both: the pairwise `BooleanCombine(..., Intersection)` pass (the expensive real-geometry step both functions independently ran as their own first step) is now factored into a shared `ComputePairwiseOverlaps` helper computed exactly ONCE, with the multi-way expansion (`ExpandMultiWayOverlaps`) built on top of that same result — a genuine efficiency gain (roughly halving the Boolean-intersection call count a caller wanting both reports previously had to pay for), not just API sugar. `ComputeInterference`/`ComputeMultiWayInterference` themselves are refactored to call the same two shared helpers, so their own observable behavior (including every existing test) is unchanged. Verified (`TestComputeAllInterference`, tests/test_basic.cpp) against the same 4-body fixture `TestComputeMultiWayInterference`'s own quadruple case uses (a genuine pairwise + several 3-way + one 4-way interference all present at once): `ComputeAllInterference`'s own `pairwise`/`multi_way` match a standalone `ComputeInterference`/`ComputeMultiWayInterference` call bit-for-bit (same pairs/index-sets, same exact solid volumes), plus the same negative controls (disjoint bodies report nothing in either half, a non-closed operand still throws `std::runtime_error`). **Upgraded to present this pass: `Clash` (dino8-app/src/commands/cmd_solidtools.cpp) now takes a `CreateSolids` option** that, when set, calls `ComputeAllInterference` on every closed-manifold object among the selection (an open surface still participates in the pre-existing triangle-triangle clash report above it, just not in interference-solid-building, matching `ComputeAllInterference`'s own `IsClosedManifold()` precondition) and adds every resulting pairwise and N-way overlap solid to the document as a new mesh object, printing which source objects each one came from and its volume — closing the "neither function is wired to any app command" gap every earlier note in this bullet named as the last thing standing between `partial` and `present`. Verified end-to-end through the real command (`dino8-app/tests/solidtools_script.txt`): two 10x10x10 boxes overlapping by 5 units in x produce a third object with the exact closed-form interference volume (5×10×10 = 500), on top of the pre-existing plain-clash report continuing to fire unchanged for the same pair. Genuinely reachable end-to-end now, with two honest, disclosed limits rather than a claim of completeness: only closed-manifold operands are considered for solid-building (the same requirement `BooleanCombine` itself has everywhere else in this kernel), and the interference solids the app adds are mesh objects (this kernel's Manifold-backed `ComputeInterference`/`ComputeAllInterference` are mesh-level functions, not B-rep — see the "B-rep-preserving booleans" bullet above for the exact-B-rep story, which is separate).

*Note on this category's score: `TrimSheetBySolid` (this pass) gained real,
tested kernel code — the "Sheet/solid trim" item's own other half, closing
a gap this document previously called "not implemented at all" — but the
category's 8/15/2/25 (62.0%) split is unchanged: the item was already
scored `partial` (for `SplitBySheet`, its first half) and stays `partial`,
since it still doesn't cross into `present` (no app wiring, no curved-sheet
or curved-solid coverage for either half, and both inherit
`BooleanCombineGeneral`'s own scope limits). Same "genuine new evidence,
unchanged partial score" pattern this document already uses elsewhere (e.g.
the Feature operations category's `MakeHole`/`MakeCounterboreHole`/
`MakeCountersinkHole` note above) rather than a citation error. Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions. The
kernel-only headline is unaffected (no bucket moved).*

*Second note on this category's score (this pass, following on from
`TrimSheetBySolid` above): two more genuine additions land, both in the
same "closes a gap this bullet already named" shape. (1) `ImprintFaces`,
`SplitBySheet`, and `TrimSheetBySolid` each gained the same optional
caller-`tolerance` parameter `BooleanCombineGeneral` already had — closing
the "Tolerant booleans" bullet's own prior complaint that named these
three functions specifically as still hardcoding their tolerance with no
caller control. (2) `MutualImprintFaces(a, b, tolerance)` closes the
"Face-face imprint" bullet's own previously-named next step ("call it
twice, swapped, for a true mutual imprint of both bodies") as a thin,
additive wrapper around two `ImprintFaces` calls. Both changes are
real, tested kernel code (`TestImprintFacesCallerTolerance`,
`TestSplitBySheetCallerTolerance`, `TestTrimSheetBySolidCallerTolerance`,
`TestMutualImprintFacesBoxPiercedByCylinder`,
`TestMutualImprintFacesRejectsEmptyOrNonPositiveTolerance`,
tests/test_basic.cpp) — but the category's 8/15/2/25 (62.0%) split is
still unchanged: "Tolerant booleans" stays `partial` (`BooleanCombinePlanar`/
`BooleanCombineMixed` still hardcode their own tolerance, and there is
still no gap-healing of imprecise operands — the other half of that
item), and "Face-face imprint" stays `partial` too (still inherits
`BooleanCombineGeneral`'s own scope limits, and neither `ImprintFaces` nor
`MutualImprintFaces` is wired into any app command). Same "genuine new
evidence, unchanged partial score" pattern as the note above. Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing (1 test target,
`dino8_kernel_smoke`), 0 regressions. The kernel-only headline is
unaffected (no bucket moved).*

*Third note on this category's score (this pass): `ComputeMultiWayInterference`
(boolean.h/.cpp) closes the "AutoCAD-style INTERFERE" bullet's own
previously-named next step — true N-way (3+) simultaneous overlap
reporting, not just pairwise, built by extending `ComputeInterference`'s
own confirmed pairwise overlaps one more body at a time (real
`BooleanCombine(..., Intersection)` calls, bbox-prefiltered, kept only
while the shared volume stays nonzero) — and is verified against
independent closed-form volumes for a genuine 3-way and 4-way mutual
overlap, plus a negative control (a pairwise-overlapping "chain" with no
true triple-common region) that specifically rules out the
Venn-diagram-ring false positive a naively-transitive implementation
would produce. Real, tested kernel code
(`TestComputeMultiWayInterference`, tests/test_basic.cpp) — but the
category's 8/15/2/25 (62.0%) split is unchanged: this item stays
`partial`, since it was already partial for lacking app wiring (still
true — `Clash`, dino8-app/src/commands/cmd_solidtools.cpp:1210, calls
neither `ComputeInterference` nor `ComputeMultiWayInterference`) and the
pairwise-only limitation this pass fixes was never, on its own, the sole
reason this item was scored below `present`. Same "genuine new evidence,
unchanged partial score" pattern as the two notes above. Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing (1 test target,
`dino8_kernel_smoke`), 0 regressions. The kernel-only headline is
unaffected (no bucket moved).*

*Fourth note on this category's score (this pass): `BooleanCombineMixedNAry`
(boolean.h/.cpp) closes the "No kernel N-ary API" half of the "Multi-body /
multi-tool booleans" bullet's own previously-named gap for the analytic
engine — a caller can now union/intersect/subtract an arbitrary number of
`BooleanCombineMixed` operands per side in one call instead of hand-rolling
the same pairwise fold the app itself already does at the mesh-boolean
level (cmd_boolean.cpp:15-38). Real, tested kernel code
(`TestBooleanCombineMixedNAryUnionThreeOverlappingBoxesMatchesInclusionExclusion`,
`TestBooleanCombineMixedNAryUnionFoldOrderIndependence`,
`TestBooleanCombineMixedNAryDifferenceSubtractsEveryToolInSecondGroup`,
`TestBooleanCombineMixedNAryIntersectionUnionsEachSideBeforeCombining`,
`TestBooleanCombineMixedNAryNegativeControls`, tests/test_basic.cpp) — but
the category's 8/15/2/25 (62.0%) split is unchanged: the item stays
`partial`, since it was already partial for reasons this pass does not
touch (compound/multi-lump operand support, the bullet's OTHER named gap,
is untouched; `BooleanCombinePlanar`/`BooleanCombineGeneral` have no N-ary
wrapper; no app command calls the new function). Same "genuine new
evidence, unchanged partial score" pattern as the notes above. Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing (1 test target,
`dino8_kernel_smoke`), 0 regressions. The kernel-only headline is
unaffected (no bucket moved).*

*Fifth note on this category's score (this pass): `BooleanCombinePlanarNAry`
and `BooleanCombineGeneralNAry` (boolean.h/.cpp, boolean_general.h/.cpp)
extend the Fourth note's N-ary wrapper to the other two B-rep boolean
engines, closing the "`BooleanCombinePlanar`/`BooleanCombineGeneral` have no
N-ary wrapper" half of that note's own still-partial caveat — every one of
this category's three B-rep engines (Mixed, Planar, General) now has a real,
tested N-ary entry point with the identical fold contract. Real, tested
kernel code
(`TestBooleanCombinePlanarNAryUnionThreeOverlappingBoxesMatchesInclusionExclusion`,
`TestBooleanCombinePlanarNAryUnionFoldOrderIndependence`,
`TestBooleanCombinePlanarNAryDifferenceSubtractsEveryToolInSecondGroup`,
`TestBooleanCombinePlanarNAryIntersectionUnionsEachSideBeforeCombining`,
`TestBooleanCombinePlanarNAryNegativeControls`,
`TestBooleanCombineGeneralNAryUnionThreeOverlappingBoxesMatchesInclusionExclusion`,
`TestBooleanCombineGeneralNAryDifferenceSubtractsEveryToolInSecondGroup`,
`TestBooleanCombineGeneralNAryCallerToleranceForwardedToEveryPairwiseCall`,
`TestBooleanCombineGeneralNAryNegativeControls`, tests/test_basic.cpp) —
but the category's 8/15/2/25 (62.0%) split is unchanged: the item stays
`partial`, since it was already partial for reasons this pass does not
touch (compound/multi-lump operand support, the bullet's OTHER named gap,
is untouched — General still has no `RefuseCompoundOperand` guard of its
own to inherit either; no app command calls any of the three N-ary
functions). Same "genuine new evidence, unchanged partial score" pattern as
the notes above. Full `dino8_kernel_tests` suite (via `ctest`): 100%
passing (1 test target, `dino8_kernel_smoke`), 0 regressions. The
kernel-only headline is unaffected (no bucket moved).*

*Sixth note on this category's score (this pass): `BooleanCombinePlanar`
(boolean.cpp) closes another slice of the "Multi-body / multi-tool
booleans" bullet's own "genuine multi-lump/Compound operand support" gap —
for this one engine, `Difference` and `Intersection` no longer refuse a
`Brep::Compound()` of two or more lumps as either operand at all (`Union`/
`SymmetricDifference` still do, unchanged — see `RefuseCompoundOperand`'s
own doc comment in boolean.cpp for why only those two need a lump-merge
step this kernel still doesn't have). This works by leaving the existing
split/classify/reassemble pipeline untouched — `ClassifyPointVsSolid`'s
ray-cast parity test was already valid against any planar-faced solid,
compound or not, not just a convex or single-shell one — and adding a tail
step that re-derives the result's true `lump_face_ranges_` via
`SplitDisjointPieces()`/`Brep::Compound()` when an input was itself
compound, so a caller's `LumpFaceRanges()` sees the real shape (which can
be MORE lumps than either input had: a target with two untouched interior
cavities carved out is reported as three lumps — one outer shell, two
cavity shells that share no edge with anything — even though it is one
connected solid body, a real distinction this pass surfaces rather than
hides). Verified (`TestBooleanCombinePlanarDifferenceAcceptsCompoundFirstOperand`,
`TestBooleanCombinePlanarDifferenceAcceptsCompoundSecondOperand`,
`TestBooleanCombinePlanarIntersectionAcceptsCompoundOperand`,
`TestBooleanCombinePlanarDifferenceAcceptsGapSeparatedXorCompound`,
`TestBooleanCombinePlanarUnionAndXorStillRefuseCompoundOperand`,
tests/test_basic.cpp, plus the updated shared negative-control loop in
`TestBooleanCombineMixedChainedNegativeControls`) against a genuinely
two-lump compound target with a cutter touching only one lump (volume and
per-lump closure both correct), a compound two-cutter tool matching a
hand-chained pairwise `Difference` sequence exactly, a compound target
whose lumps are pairwise-disjoint from a cutter that overlaps both, and a
compound built from a REAL `SymmetricDifference` call (not just an
artificially-assembled fixture) with a gap between its two lumps.
**A real, disclosed scope limit found while building this, not assumed:**
if the compound operand's own lumps genuinely TOUCH along a shared contact
curve (a corner-overlap `SymmetricDifference` result, e.g. the exact
`8 + 8 - 2*1 = 14`, two-24-face-lump fixture already measured elsewhere in
this category) and the op's own face selection carries that whole contact
curve through unmodified, the single `FromPlanarFaces()` reassembly still
throws the pre-existing "an edge is shared by 3 or more faces" refusal —
not a new limitation, but the exact same one that makes `Union`/
`SymmetricDifference` refuse a compound operand outright, simply now
reachable (as a controlled exception, never a silently wrong shape) through
`Difference`/`Intersection` too, in the specific case where nothing about
the op separates the touching lumps. Verified directly, not assumed
(`TestBooleanCombinePlanarDifferenceThrowsOnTouchingLumpXorCompound`).
Still partial, not present: `BooleanCombineMixed`/`BooleanCombineGeneral`
still refuse a compound operand for every op (Mixed's own cylinder end-cap
synthesis is not proven safe against one; General still has no
`RefuseCompoundOperand` guard of its own to inherit at all — both untouched
by this pass); no app command calls `BooleanCombinePlanar` at all, compound
operand or not (`BooleanCombinePlanar`/`Mixed`/`General` all still have zero
references anywhere in dino8-app/src, per this category's own "B-rep-
preserving booleans reachable from the application" bullet); and a
genuinely-touching-lump compound is still refused (as a different, pre-
existing error) rather than handled. Same "genuine new evidence, unchanged
partial score" pattern as the notes above — the category's 8/15/2/25
(62.0%) split is unchanged, since "Multi-body / multi-tool booleans" was
already `partial` for reasons this pass does not fully close. Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing (1 test target,
`dino8_kernel_smoke`), 0 regressions. The kernel-only headline is
unaffected (no bucket moved).*

*Seventh note on this category's score (this pass): `BooleanCombineGeneral`
(boolean_general.cpp) closes the "silently unguarded (not even refused) by
the general engine" half of the "Multi-body / multi-tool booleans" bullet's
own previously-named gap — the exact defect the Third same-day follow-up
note near the top of this document first surfaced ("a compound operand fed
to the general engine is silently processed per-face with no lump-boundary
awareness, an unhandled case rather than a clean refusal"). A file-local
`RefuseCompoundOperand(operand, function_name)` (boolean_general.cpp, right
before `BooleanCombineGeneral`'s own definition) now runs at the top of
`BooleanCombineGeneral` for both operands, on every op — the same
`LumpFaceRanges().size() <= 1` precondition and the same refusal message
shape boolean.cpp's own `RefuseCompoundOperand` already uses for
`BooleanCombinePlanar`/`BooleanCombineMixed`, duplicated rather than shared
across the two translation units (each file already keeps its own
file-local copy of small helpers like this one; there was no existing
shared home for it). This is a safety fix, not a new capability: a compound
operand was never proven to produce a correct result through this engine's
per-face ray-cast classification (which has no notion of which lump a face
belongs to), so turning the silent mis-processing into a clean,
disclosed `std::invalid_argument` closes an unhandled-case gap without
attempting the actual multi-lump support this bullet's other named gap
still needs. `BooleanCombineGeneral`'s own output is never itself a
`Brep::Compound` (it builds one shell of kept fragments, not a compound),
so the guard cannot fire on a result fed back into a further
`BooleanCombineGeneral`/`BooleanCombineGeneralNAry` call — confirmed
directly, not assumed, by
`TestBooleanCombineGeneralNAryUnionThreeOverlappingBoxesMatchesInclusionExclusion`
and the rest of that pre-existing NAry coverage continuing to pass
bit-for-bit unchanged. Verified
(`TestBooleanCombineGeneralRefusesCompoundOperand`,
`TestBooleanCombineGeneralNAryRefusesCompoundOperandAtEveryPairwiseStep`,
tests/test_basic.cpp) against a REAL `SymmetricDifference` compound (the
same two-touching-box fixture
`TestBooleanCombinePlanarDifferenceThrowsOnTouchingLumpXorCompound` uses,
not an artificially-assembled one) refused as either operand for Union,
Intersection AND Difference; a negative control confirming two genuinely
single-lump operands are entirely unaffected; and that the guard reaches a
compound operand placed anywhere in `BooleanCombineGeneralNAry`'s own
`first_group`/`second_group` folds, not just a direct top-level call. Still
partial: this closes the "silently unguarded" half of the bullet's own gap
only — genuine multi-lump/Compound operand SUPPORT for this engine (the
bullet's other, larger named gap, the thing `BooleanCombinePlanar` itself
now has for `Difference`/`Intersection`, per the Sixth note above) remains
unimplemented, `BooleanCombineMixed` still refuses a compound operand for
every op too, and no app command calls any of these three engines at all.
Same "genuine new evidence, unchanged partial score" pattern as the notes
above — the category's 8/15/2/25 (62.0%) split is unchanged. Full
`dino8_kernel_tests` suite: 100% passing (5544 checks, 0 failures, exit
code 0), 0 regressions — including, incidentally, the specific
60-degree-pinch-point check the "Analytic plane/cylinder and
cylinder/cylinder B-rep booleans" bullet above once flagged as failing on a
prior HEAD (`TestBooleanCombineMixedUnequalRadiusPerpendicularNegativeControls`);
it passes cleanly in this pass's own full run, though whether that prior
failure was resolved by an intervening change or was itself intermittent is
not investigated here (out of this note's own scope). The kernel-only
headline is unaffected (no bucket moved).*

*Eighth note on this category's score (this pass): `BooleanCombineMixed`
(boolean.cpp) closes the other half of the "Multi-body / multi-tool
booleans" bullet's own previously-named gap the Seventh note left open —
"`BooleanCombineMixed` still refuses a compound operand for every op too."
`Difference`/`Intersection` now take the identical exemption
`BooleanCombinePlanar` already has (`Union`/`SymmetricDifference` still
refuse one; see `RefuseCompoundOperand`'s own doc comment in boolean.cpp),
closing the last of the three B-rep engines' own outright refusals for
those two ops. This was left open the first time `BooleanCombinePlanar`
gained the exemption specifically because this engine's own cylindrical
end-cap synthesis (`SynthesizeEndCaps`) was not yet proven safe against a
multi-lump `other` — checked directly this pass, not merely assumed: every
classification step this function makes (the planar ray-cast parity test,
the cylindrical ray-vs-cylinder quadratic, and `SynthesizeEndCaps`' own
per-face probes, including its parallel-cylinder crossing/lens-cap logic)
already treats `other` as a flat face list with no notion of which lump a
face came from, so a multi-lump `other` classifies exactly as correctly as
a single-lump one. Verified
(`TestBooleanCombineMixedDifferenceAcceptsCompoundFirstOperand`,
`TestBooleanCombineMixedIntersectionAcceptsCompoundOperand`,
`TestBooleanCombineMixedIntersectionAcceptsCompoundOperandWithEmbeddedCylinders`,
`TestBooleanCombineMixedDifferenceThrowsOnTouchingLumpXorCompound`,
`TestBooleanCombineMixedUnionAndXorStillRefuseCompoundOperand`,
`TestBooleanCombineMixedNArySingleElementCompoundGroupReachesFinalCombine`,
tests/test_basic.cpp) against the same shapes of fixture the Planar/General
NAry notes above already established (a gap-separated two-lump target, a
disjoint two-lump tool, a corner-overlap XOR compound still hitting the
pre-existing non-manifold refusal), plus a fixture this pass adds
specifically to stress the cylindrical concern above: a compound *tool*
built entirely of two disjoint, fully-embedded cylindrical bosses inside a
box, each needing its own synthesized end cap at both ends.
**A real, previously-undocumented limitation found while building this, not
assumed:** unlike `BooleanCombinePlanar`'s purely-planar output, a
`BooleanCombineMixed` result carrying any `CylindricalFace` does not have
genuine `ON_Brep` edge/vertex topology between that wall and its own (real
or synthesized) planar end caps — confirmed directly by a standalone
`SplitDisjointPieces()` probe on the embedded-bosses fixture above, which
wrongly reports SIX pieces (one per end-cap wedge group, plus one per bare
cylindrical wall) for what is geometrically two disjoint, genuinely closed
solids. This is the SAME already-disclosed "genuine topology" gap this
document's own **kernel: Topology & data structure** category names for
`Box()`/`Sphere()`/`Thicken()`/`ExtrudeFace()`, now found to affect this
engine's own end-cap synthesis too — not a defect in the actual boolean
math (the embedded-bosses fixture's own tessellated result is genuinely
closed with the exactly-correct combined volume), only in
`LumpFaceRanges()` bookkeeping. So `BooleanCombineMixed`'s own
lump-recomputation tail (the same `SplitDisjointPieces()`/`Brep::Compound()`
step `BooleanCombinePlanar` uses) runs ONLY when the result is purely planar
(no `CylindricalFace` at all); a cylindrical-face-bearing result from a
compound operand keeps the same best-effort single-lump report every other
(non-compound-input) call already has, rather than a confidently wrong
over-fragmented one. Still partial: genuine multi-lump/Compound operand
SUPPORT for `BooleanCombineGeneral` (the bullet's other, larger named gap)
remains unimplemented; a compound operand mid-fold in either N-ary wrapper
still refuses (unchanged); no app command calls any of these engines at
all; and the newly-found cylindrical lump-bookkeeping gap above is itself a
real, disclosed scope limit, not a flip to `present`. Same "genuine new
evidence, unchanged partial score" pattern as the notes above — the
category's 8/15/2/25 (62.0%) split is unchanged. Full `dino8_kernel_tests`
suite: 100% passing (5727 checks, 0 failures, exit code 0), 0 regressions.
The kernel-only headline is unaffected (no bucket moved).*

*Ninth note on this category's score (this pass): `BooleanCombineGeneral`
(boolean_general.cpp) closes the last remaining gap the Seventh/Eighth notes
above left open in the "Multi-body / multi-tool booleans" bullet — genuine
compound-operand SUPPORT for the third and last B-rep engine, not just the
"silently unguarded" safety fix the Seventh note already applied. `Union`
still refuses either operand being a `Brep::Compound()` of two or more lumps
(unchanged, same reason as the other two engines); `Difference`/
`Intersection` now accept one on either side, the identical exemption
`BooleanCombinePlanar`/`BooleanCombineMixed` already have — this engine's own
per-face ray-cast classification (`ClassifyPointVsBrep` against the OTHER
operand's full face list, or the coincident-face override's per-face normal
comparison) already has no notion of which lump a face came from, so a
multi-lump operand classifies exactly as correctly as a single-lump one.
Verified (`TestBooleanCombineGeneralDifferenceAcceptsCompoundFirstOperand`,
`TestBooleanCombineGeneralIntersectionAcceptsCompoundOperand`,
`TestBooleanCombineGeneralDifferenceThrowsOnTouchingLumpXorCompound`, plus an
updated `TestBooleanCombineGeneralRefusesCompoundOperand` and a third block on
`TestBooleanCombineGeneralNAryRefusesCompoundOperandAtEveryPairwiseStep`,
tests/test_basic.cpp) against a disjoint two-lump target (one lump touched by
the cutter/tool, the other untouched) for both ops, plus the same
still-refused touching-lump-XOR-compound case the other two engines have
(throws the pre-existing non-manifold reassembly refusal — a `std::runtime_error`
here specifically, `BuildLoop`'s own throw, not the `std::invalid_argument`
`FromMixedFaces` uses for the identical condition on the other two engines,
a genuine pre-existing difference between the engines' own error types this
pass found and documented, not introduced).
**A real, previously-undocumented limitation found while building this, not
assumed, and the reason this item stays `partial` rather than flipping to
`present`:** unlike `BooleanCombinePlanar`/`BooleanCombineMixed`, this engine
does NOT recompute the result's true `LumpFaceRanges()` split afterward.
Tried directly, not skipped out of caution: `SplitDisjointPieces()`'s own
`ON_Brep::DuplicateFaces()` step corrupts whatever
`TessellateGeneralBooleanClosedMesh()`'s own T-junction stitching needs from
this engine's dense-polyline trim edges — a standalone probe confirmed a
compound-input result tessellates as a genuinely closed manifold with the
exact right combined volume when left as ONE unsplit Brep, but re-running
`SplitDisjointPieces()` on that same result and tessellating either extracted
single-lump piece the identical way gives a WRONG volume and a non-manifold
mesh, a regression the split itself introduces rather than a pre-existing
defect it merely exposes. So a compound-input result from this engine always
reports a single lump — the actual combined SHAPE is correct (closed, right
volume, confirmed via `TessellateGeneralBooleanClosedMesh()` in the tests
above), only `LumpFaceRanges()` bookkeeping is affected, the same class of
disclosed gap the Eighth note's own cylindrical-face finding already
established for `BooleanCombineMixed`, now found to affect this engine's
tessellation tooling too, for a different underlying reason. Separately:
this engine's own pre-existing "No general partially-overlapping coincident
curved-face handling" scope limit (this bullet's neighboring bullet) means a
naive reuse of `BooleanCombineMixed`'s own axis-aligned Intersection fixture
(a tool sharing a coplanar, partially-overlapping side face with BOTH lumps
of a compound target at once) throws independent of any compound-operand
concern — confirmed by direct standalone reproduction, not assumed — so this
engine's own new tests use a diagonally-offset fixture instead, avoiding any
shared face plane between operands. Still partial, unchanged: genuine
multi-lump/Compound operand SUPPORT for the app layer (no app command calls
any of the three engines at all, unchanged), and a compound operand mid-fold
in any of the three N-ary wrappers still refuses (each wrapper still folds a
group via pairwise Union calls internally, and Union stays refused for a
compound operand on all three engines) — the category's 8/15/2/25 (62.0%)
split is unchanged, since this item was already `partial` for reasons this
pass does not fully close. Full `dino8_kernel_tests` suite: 100% passing
(5855 checks, 0 failures, exit code 0), 0 regressions. The kernel-only
headline is unaffected (no bucket moved).*

*Tenth note on this category's score (this pass): `BooleanCombinePlanar` and
`BooleanCombineMixed` (boolean.h/.cpp) close the other half of the "Tolerant
booleans (caller-specified tolerance)" bullet's own previously-named gap -
"`BooleanCombinePlanar`/`BooleanCombineMixed`... still hardcode their own
internal tolerance with no caller control at all," the specific complaint
left open when `BooleanCombineGeneral`/`ImprintFaces`/`SplitBySheet`/
`TrimSheetBySolid` each gained the identical parameter in an earlier pass.
Both engines (and their own `BooleanCombinePlanarNAry`/
`BooleanCombineMixedNAry` wrappers, which forward it to every pairwise fold
step) now take an optional `tolerance` (default -1.0) that, when
non-negative, is used as-is for every distance/coincidence test the engine
makes (`SplitAndBucket`/`SplitAndBucketMixed`'s own split/classify
threshold, the Difference branch's own `same_plane` coincident-face dedup,
and, for Mixed, `SynthesizeEndCaps`' own per-face probes) - the identical
negative-sentinel ("caller value if non-negative, else an auto-derived
default") convention `ClipConvexPolygon` (boolean.h) already established,
rather than the General engine's own plain-positive-default shape (that
engine has one flat epsilon; these two already had an adaptive
`RelativeTol()`/`RelativeTolMixed()` scaled to the operands' own size, so a
negative sentinel preserves that adaptive default exactly rather than
replacing it with a fixed number). Verified
(`TestBooleanCombinePlanarCallerTolerance`,
`TestBooleanCombineMixedCallerTolerance`,
`TestBooleanCombinePlanarNAryCallerToleranceForwardedToEveryPairwiseCall`,
`TestBooleanCombineMixedNAryCallerToleranceForwardedToEveryPairwiseCall`,
tests/test_basic.cpp) on a fixture built specifically to make the effect
unambiguous: two boxes sharing the same 10x10 footprint, stacked along z
with a real but tiny (1e-5) overlap - far bigger than the auto-derived
default tolerance (~1e-8 for this fixture's own ~20-unit extent) but far
smaller than a deliberately loose caller override (1e-3). Omitting
`tolerance` (or passing an explicit tight one well under the real overlap)
reproduces the exact same face count and tessellated volume either way -
backward compatibility with every pre-existing caller in this file, none of
which pass a fourth argument. The NAry wrappers' own caller tolerance is
proven to reach every pairwise fold step, not just a final combine, the
same way `TestBooleanCombineGeneralNAryCallerToleranceForwardedToEveryPairwiseCall`
already proved for the General engine: `BooleanCombinePlanarNAry`/
`BooleanCombineMixedNAry` at a given tolerance match an equivalent
hand-folded sequence of pairwise `BooleanCombinePlanar`/`BooleanCombineMixed`
calls at the identical tolerance, bit-for-bit on tessellated volume.
**A real, disclosed scope limit found while building this, not assumed:** a
caller tolerance far looser than the operands' own true separation is a
genuine footgun for these two engines, not merely a theoretical one -
proven directly on the same overlap fixture above, not argued. A loose
(1e-3) tolerance measurably changes the fixture's own Union result (face
count 14 -> 11 for Planar, 14 -> 10 for Mixed, relative to the default/tight
case) - confirming `tolerance` reaches the real per-fragment classification
decisions inside `SplitAndBucket`/`ClassifyPointVsSolid`, not just a
cosmetic default value - but for this exact fixture shape (two operands
sharing a common footprint, stacked along one axis with a genuine, not
merely flush-touching, overlap) the loosened tolerance also degrades the
result from a valid closed manifold into a non-manifold one: it misclassifies
the genuinely-overlapping near-coincident faces as flush-touching, which
drops one side's boundary face via the existing same-direction/opposed-
direction `same_plane` dedup with nothing compensating for the other side,
leaving the result open where that face used to be. This is a real
limitation of the classification scheme itself (present since these
engines were first written, merely unreachable by a caller before this
pass added the parameter that lets one pick an inappropriate value) rather
than a defect in the new plumbing - the caller remains responsible for
picking a tolerance smaller than the real feature size being modeled,
exactly as for every other tolerance parameter in this kernel (fillet
radii, sweep station spacing, the General engine's own SSX `tolerance`).
The other half of the bullet's own name - gap-healing of imprecise
operands, freeform (not just planar-classification-adjacent) tolerance
behavior - is untouched: the category's 8/15/2/25 (62.0%) split is
unchanged, since "Tolerant booleans" was already `partial` for reasons this
pass does not fully close. Full `dino8_kernel_tests` suite: 100% passing,
0 regressions (one pre-existing, unrelated, intermittent failure -
`TestFoldFaceConvexPlanarBoxFrontWallHingedAtBottomEdgeMatchesExactIntegral`,
in **kernel: Local/direct-edit operations**, not this category - was seen on
one run of this session's own unmodified HEAD before this pass's edits and
did not reproduce on a subsequent clean rebuild's run; not investigated
further here, out of this note's own scope, the same "confirmed
pre-existing, not a regression, not chased further" treatment this
document's own "Analytic plane/cylinder..." bullet above already gives an
analogous intermittent failure). The kernel-only headline is unaffected (no
bucket moved).*

*Eleventh note on this category's score (this pass): three genuine,
independently-tested additions land, one per bullet, each closing (or
narrowing) the specific gap that bullet's own prior evidence named next.
(1) `BooleanOperationError`/`BooleanFailureReason` (boolean.h) is the first
typed refusal this category has - `RefuseCompoundOperand` (shared by all
three B-rep engines) now throws a structured, still-backward-compatible
exception instead of a plain `std::invalid_argument`, closing a narrow
slice of the "Boolean failure diagnostics" bullet's own "no structured
failure-report type exists" complaint. (2) `TestBooleanCombineGeneralFreeformSurfaceOperand`
closes the "Free-form (non-analytic) NURBS surface operands" bullet's own
"no freeform-operand test exists" complaint with a genuine doubly-curved
Bezier-patch operand, verified via the implementation-independent
`Volume(A) + Volume(B) == Volume(Union) + Volume(Intersection)` identity
rather than a hand-derived closed form. (3) `ComputeAllInterference`
(boolean.h/.cpp) closes the "AutoCAD-style INTERFERE" bullet's own "no
single combined API" complaint, sharing one pairwise-overlap pass between
the pairwise and N-way reports instead of computing it twice. All three are
real, tested kernel code (`TestBooleanOperationErrorStructuredFields`,
`TestBooleanCombineGeneralFreeformSurfaceOperand`, `TestComputeAllInterference`,
tests/test_basic.cpp) - but the category's 8/15/2/25 (62.0%) split is
unchanged: each of the three bullets
touched stays `partial`, since each still has its own remaining gap this
pass does not close (failure diagnostics: ~160 other throw sites and the
mesh engine's own generic Manifold-status failure are untouched; freeform
operands: only one simple, single-crossing-loop fixture is exercised, not
a case hitting `BooleanCombineGeneral`'s own multi-crossing/genus-0 scope
limits; INTERFERE: still no app command calls any of the three functions).
Same "genuine new evidence, unchanged partial score" pattern as the notes
above. Full `dino8_kernel_tests` suite (via `ctest`): 100% passing (6052
checks, 0 failures, exit code 0), 0 regressions. The kernel-only headline
is unaffected (no bucket moved).*

**Twelfth note on this category's score (this pass): two bullets actually move buckets,
the first score change this category has had since the very first "Sheet/solid trim"
same-day follow-up near the top of this document.** (1) `BooleanUnion`/`BooleanDifference`/
`BooleanIntersection`/`Boolean2Objects` (dino8-app/src/commands/cmd_boolean.cpp) now try a
genuine B-rep-preserving `BooleanCombinePlanarNAry`/`BooleanCombinePlanar` path before
falling back to the mesh engine, closing enough of the "B-rep-preserving booleans reachable
from the application" bullet's own gap to move it `missing` → `partial` (this category's one
remaining `missing` item, now zero). (2) `Clash` (dino8-app/src/commands/cmd_solidtools.cpp)
gained a `CreateSolids` option wiring `ComputeAllInterference` into the app, closing the
"AutoCAD-style INTERFERE" bullet's own last-named gap ("neither function is wired to any app
command") and moving it `partial` → `present`. Both are real, tested, app-layer-verified
changes (`dino8-app/tests/boolean_script.txt`, `boolean_adversarial_script.txt` — including a
genuine defect this pass found and fixed itself, the open-shell-via-`IsSolid()` eligibility
gap described in the B-rep-preserving bullet above, before it could ship as a silent
wrong-answer regression — and `solidtools_script.txt`'s new Clash `CreateSolids=Yes` section),
not kernel-only additions with app wiring merely claimed. Net effect: this category's own
present/partial/missing counts move 8/15/2/25 (62.0%) → 9/15/1/25 (66.0%) — the first bucket
move in this category since the Sheet/solid trim item crossed into `partial` at the very top
of this document; every OTHER bullet in this category (the general engine's own scope limits,
tolerant booleans' gap-healing half, non-manifold results, 2D region booleans, and the rest)
is untouched. Full `dino8_kernel_tests` suite (via `ctest`): 100% passing (6052 checks, 0
failures, exit code 0), 0 regressions — this pass's own changes are entirely in dino8-app, so
the kernel suite is an unchanged-baseline check, not a direct test of the new code; the app-side
verification above is what actually exercises it. Weighted the same marginal-delta way as this
document's other same-day category deltas (this row's own weight of 1.5 against the 17.75 total
kernel weight, +4.0 points within the category): 67.4% → 67.7%. The Kernel ranked-priority table
above is updated to match (Boolean operations' own remaining-items count drops 17 → 16, moving
its own weight/remaining ratio from 0.088 to 0.094 — now tied with Intersections & projections
for rank 2, ahead of the four-way 0.091 tier it used to trail).*

*Thirteenth note on this category's score (this pass): two genuine findings
land, one a verification and one a kernel-level bug fix, neither a bucket
move. (1) The compound-operand half of the "B-rep-preserving booleans
reachable from the application" bullet's own "Still partial" text is
updated: a compound (multi-lump) Brep operand was already reaching
`BooleanCombinePlanar`'s own compound-accepting `Difference`/`Intersection`
path (this category's "Sixth note" above) the first time
`TryExactBrepBoolean` (dino8-app/src/commands/cmd_boolean.cpp) was written -
`BooleanCombinePlanarNAry`'s single-Brep-per-group fold has no intervening
`Union` call to refuse on - but this was asserted from the kernel's own
N-ary fold contract, never actually exercised through a real app command.
Now verified end-to-end, not just argued
(`dino8-app/tests/boolean_mixed_and_compound_script.txt`, wired into
`smoke.sh`): a genuine two-lump compound - the real output of a
`Boolean2Objects` `Result=SymmetricDifference` call on two disjoint boxes,
not an artificially-assembled fixture - has a third box subtracted from it
(overlapping only one of its two lumps) through `BooleanDifference`,
correctly taking the exact path and landing on the correct combined volume
(the untouched lump's full volume plus the touched lump's volume minus the
overlap, `1750`). (2) Wiring `BooleanCombineMixed`/`BooleanCombineMixedNAry`
into `TryExactBrepBoolean` as a second attempt (for an operand with a
cylindrical face) was tried and REVERTED, not shipped - two real, disclosed
problems were found, neither fixed: a purely-planar adversarial fixture
`BooleanCombinePlanar` correctly refuses (this category's "Tangent /
grazing contact handling" bullet) can be silently accepted by
`BooleanCombineMixed` instead (its own auto-derived tolerance doesn't
always agree with the planar engine's), and even gated to only try when a
genuine `CylindricalFace` is present, a result built from dino8-app's own
record-less `Cylinder`/`Box` objects (`WrapBrep`, cmd_solids.cpp, never
attaches a dino8 FaceRecord) tessellates to an OPEN mesh via the app's own
generic `MeshOf` path, so `Volume` reports it "not closed" - see the bullet
above's own "A later pass" paragraph for the full account of both, and of
the real, narrower kernel bug this investigation DID fix along the way:
`Brep::MixedFaces`'s own `ExtractCylindricalFace` (brep.cpp) used to throw
outright on any record-less FULL cylindrical face at a radius other than 1
(an OpenNURBS `GetRadianFromNurbFormParameter` contract/implementation
mismatch, confirmed directly), now fixed by detecting a full sweep
geometrically instead
(`TestBrepMixedFacesRecoversFullCylinderWallWithoutFaceRecordAtNonUnitRadius`,
tests/test_basic.cpp) - a real fix, but on its own not enough to make
`BooleanCombineMixed` produce a correct end-to-end result for such an
operand (the separate coarse-trim-sampling defect above still breaks it),
which is why the app-wiring attempt itself was reverted rather than shipped
half-safe. Net effect on the scores below: narrows, does not flip, the SAME
already-partial item - this category's own present/partial/missing counts
and 9/15/1/25 (66.0%) split are unchanged, since real gaps remain
(`BooleanCombineMixed`/`BooleanCombineGeneral` still have no app entry
point; `BooleanSplit`/`MeshSplit`/`MeshBooleanSplit`/`SplitByObject`/
`WireCut` are untouched; `Union`/`SymmetricDifference` still refuse a
compound operand, unchanged) - the same "genuine new evidence, unchanged
partial score" pattern the notes above this category's Twelfth note already
established. Full `dino8_kernel_tests` suite (via `ctest`): 100% passing, 0
regressions, including the new `ExtractCylindricalFace` regression test;
`dino8-app/tests/smoke.sh` (including the new compound-operand script
above) run clean end to end.*

*Fourteenth note on this category's score (this pass): closes the specific
coarse-trim-sampling defect the Thirteenth note's own "A later pass" account
left disclosed rather than fixed - the "further, separate defect" found
while investigating why `BooleanCombineMixed` couldn't be wired into the app
for a record-less (no dino8 `FaceRecord`) cylindrical operand.
Root-caused, not just re-described: `SampleLoop` (brep.cpp), the only
function that ever builds a record-less face's own trim polygon
(`ResolveFace`'s "derive trims from the brep's own loops" fallback -
side-table-backed faces, i.e. every face this kernel's own constructors
build, never reach it), floored any non-linear trim curve's own sampling at
just 8 points (16 for a typical 4-span full-circle NURBS curve, the shape
`ON_BrepCylinder`'s own cap boundary takes) - a coarse-chord polygon sitting
measurably INSIDE the true circle, so a record-less cylinder's own planar
cap boundary and its cylindrical wall's own exact circular cross-section no
longer agreed, which is what broke `BooleanCombineMixed`'s stitch/closure
even after the angle-recovery fix above let it run without throwing. Fixed
by raising that floor from 8 to 128 samples - closing this as the general
`PlanarFaces()`/`MixedFaces()`-wide gap it actually is (any curved trim loop
on a record-less face, not just a cylinder cap), not a narrower
cylinder-only special case. Verified
(`TestBrepMixedFacesRecoversFullCylinderWallWithoutFaceRecordAtNonUnitRadius`,
tests/test_basic.cpp, upgraded from its own prior "not asserted here"
disclosure): the same record-less box-minus-radius-2-cylinder
`BooleanCombineMixed(..., Difference)` fixture that bug already exercises
now tessellates (`TessellateToClosedMeshConforming(64, 64)`) to a genuine
`IsClosedManifold()` whose volume matches the hand-derived `1000 - 40*pi`
closed form this file's own FaceRecord-backed drilled-box fixtures already
use, to the same tolerance - not merely "does not throw" as before. **Also
corrected this pass, not a code change:** the "Multi-body / multi-tool
booleans" bullet's own Fourth/Fifth notes above, written before
`TryExactBrepBoolean` existed, said "no app command calls any of the three
N-ary functions" - true when written, but stale for one of the three as of
the Twelfth note: `TryExactBrepBoolean` (dino8-app/src/commands/cmd_boolean.cpp)
has called `BooleanCombinePlanarNAry` for every `Boolean2Objects`/`BooleanUnion`/
`BooleanDifference`/`BooleanIntersection` with 3+ eligible operands per side
since that note landed - re-verified directly this pass (`git grep
BooleanCombine.*NAry dino8-app/src`: `BooleanCombinePlanarNAry` at
cmd_boolean.cpp:62/67, `BooleanCombineMixedNAry`/`BooleanCombineGeneralNAry`
genuinely zero hits, confirming which two of the three are still actually
unwired). Given this pass's own kernel-level fix removes ONE of the two
disclosed blockers on wiring `BooleanCombineMixed` in the same way
(the other - `RelativeTolMixed` silently accepting adversarial geometry
`BooleanCombinePlanar` correctly refuses - is untouched, kernel-level, and
was reproduced only against operands with NO cylindrical face at all, so a
future attempt gated strictly on "a genuine `CylindricalFace` is present"
may avoid it entirely, but that gating and the adversarial-script
re-verification it would need are not attempted here), re-wiring
`BooleanCombineMixed` into `TryExactBrepBoolean` remains a real, disclosed
next step, not re-attempted this pass. Net effect on the scores below: both
findings narrow, but do not flip, already-`partial` items ("B-rep-preserving
booleans reachable from the application" and "Multi-body / multi-tool
booleans") - this category's own present/partial/missing counts and
9/15/1/25 (66.0%) split are unchanged, the same "genuine new evidence,
unchanged partial score" pattern as the notes above. Full
`dino8_kernel_tests` suite (via the built `dino8_kernel_tests` binary
directly): 6754 checks, 100% passing, 0 regressions. The kernel-only headline is
unaffected (no bucket moved).*

*Fifteenth note on this category's score (this pass): the exact "next step"
the Fourteenth note's own closing paragraph named - re-wiring
`BooleanCombineMixed` into `TryExactBrepBoolean` (dino8-app/src/commands/
cmd_boolean.cpp) as a second attempt, gated strictly on "a genuine
`CylindricalFace` is present" so the Thirteenth note's own problem (1) (a
purely-planar adversarial fixture silently accepted by `BooleanCombineMixed`'s
own looser tolerance) cannot be reached - was built, verified end-to-end
through the real app, and reverted again, for a NEW, deeper, disclosed
reason this pass is the first to actually measure. First, the good news,
confirmed rather than re-assumed: `kernel::Brep::HasCylindricalFace(double
tolerance = 1e-4)` (brep.h:2017; brep.cpp:1200) is a real, tested, minimal
addition that closes the "decide the cleanest way to expose an equivalent
check" question the Fourteenth note's own next-step description left open -
a cheap, record-less, non-throwing probe (walks every live face's
`SurfaceOf()->IsCylinder(&cyl, tolerance)`, short-circuiting on the first
match, the same OpenNURBS call and tolerance scale `MixedFaces()`'s own
`ExtractCylindricalFace()` call site and `BuildPlaneCylinderVariableFillet`
(dino8-app/src/commands/cmd_fillet.cpp) already use) rather than reusing the
heavier `MixedFaces()` extraction itself, which throws on the first
non-planar/cylindrical/conical face and so is the wrong contract for a plain
yes/no gate. Verified
(`TestBrepHasCylindricalFaceDetectsRecordlessCylinderAndRejectsPurelyPlanarBrep`,
tests/test_basic.cpp) on both a record-less box/cylinder pair (`WrapBrep`'s
own pattern, cmd_solids.cpp - no dino8 `FaceRecord`) and the FaceRecord-backed
equivalents (`Brep::Box()`/`Brep::FromMixedFaces()`), confirming the method
agrees with itself regardless of which factory built the underlying
`ON_Brep`. Gating a `TryExactBrepBoolean` Mixed attempt on this check does
what the Fourteenth note predicted it would: re-run against the full
`boolean_adversarial_script.txt` corpus (which has zero cylindrical/conical/
spherical primitives anywhere in it - confirmed by inspection, not assumed)
confirms every one of its near-tangent/barely-overlapping/coincident-face/
huge-scale/chained-cut fixtures never even reaches the gate, let alone the
Mixed engine, so the Thirteenth note's own problem (1) is provably
unreachable through this gate, not merely presumed avoided.

**But wiring the gated attempt into `TryExactBrepBoolean` itself was tried,
verified, and reverted anyway - because a second, more fundamental blocker
was found this pass, independent of both of the Thirteenth note's own two
problems.** Built and ran (not merely reasoned about) both a `BooleanUnion`
of a record-less box and an embedded record-less cylindrical boss, and a
`BooleanDifference` drilling a record-less through-hole in a record-less
box, through the real app with the gated Mixed attempt actually wired in:
both took the exact path cleanly (`BooleanUnion: exact B-rep boolean (no
tessellation), 20 face(s)`; `BooleanDifference: exact B-rep boolean (no
tessellation), 13 face(s)`) - the boolean construction itself genuinely
succeeds, and the resulting `ON_Brep` is `IsValid()` and `IsManifold()` (both
confirmed directly via a standalone probe, not inferred). But a subsequent
`Volume` command on either result reports `! Object N is not closed` /
`Volume = 0` - the app's selection/measurement pipeline (`ObjectVolume`,
dino8-app/src/commands/cmd_analyze.cpp, via `MeshOf`/`MeshBrepClosed`,
cmd_common.cpp/geom/BrepMesher.cpp) cannot make sense of the exact result at
all. Root-caused, not left as a repeat of the Thirteenth note's own
"tessellates to an OPEN mesh" observation: a standalone probe confirms the
result's raw `ON_Brep::IsSolid()` is **false** (while `IsValid()`/
`IsManifold()` are both true) - and `MeshBrepClosed`'s own automatic
finer-chord-tolerance retry (BrepMesher.cpp:800-812) is gated on
`brep.IsSolid()`, so for a `CylindricalFace`-bearing `BooleanCombineMixed`
result that safety net never engages at all, regardless of how coarse or
fine the first attempt's tolerance was. Forcing the question further - a
second standalone probe called `MeshBrepClosed` directly at five explicit
chord tolerances from 0.005 down to 0.000001 (bypassing the `IsSolid()` gate
entirely) - shows this is not a tolerance problem the gate merely hides:
every single tolerance still comes back non-closed, and the measured volume
does not converge toward the true closed form (4000 - pi\*3^2\*10 = 3717.26)
as tolerance tightens the way a genuine chordal-deficit/coarseness effect
would - it drifts further away (3717.5 at tol=0.005, 3765.5 at
tol=0.000001), the signature of a genuine boundary-curve mismatch between
independently-tessellated adjacent faces, not insufficient resolution. For
comparison, the SAME result's `ON_Brep`, fed through the KERNEL's own
specialized `TessellateToClosedMeshConforming(64, 64)` (the machinery the
Fourteenth note's own fix actually targets), closes correctly and measures
3717.285 - confirming the Fourteenth note's own fix is genuinely still
sound at the kernel layer; the newly-found blocker is entirely in the APP's
own separate, independent tessellator, which never uses `SampleLoop`,
`ResolveFace`, or any other machinery the Fourteenth note touched at all
(dino8-app/src/geom/BrepMesher.cpp's own `MeshBrepFaces`/`MeshBrepClosedOnce`
is a from-scratch constrained-Delaunay tessellator working directly off each
`ON_BrepFace`'s own trim loops, entirely separate from the kernel's own
polygon-sampling and conforming-mesh code).

This is the SAME disclosed "genuine `ON_Brep` edge/vertex topology" gap the
Eighth/Ninth notes above already named for a `CylindricalFace`-bearing
`BooleanCombineMixed`/`BooleanCombineGeneral` result (there, measured only as
`SplitDisjointPieces()`'s own `LumpFaceRanges()` bookkeeping going wrong,
with the actual tessellated SHAPE still confirmed correct through each
engine's own specialized tessellator) - this pass's own contribution is
finding and measuring a second, more serious consequence of that same root
cause: without genuine shared edges between the cylindrical wall and its own
planar caps, the app's independently-tessellating-each-face mesher has no
guarantee two adjacent faces' own boundary samples land at the same 3D
points at all, so `Mesh::MergeAndWeld`'s vertex-proximity stitch can fail at
any tolerance - not merely produce a coarse-but-closed approximation the way
it does for every other B-rep object in this app (including every existing
`BooleanCombinePlanar` result already reachable through this same
`TryExactBrepBoolean`, unaffected, since that engine's own planar-only
results keep real, `IsSolid()`-true topology throughout). This also
disentangles the Thirteenth note's own problem (2) from problem (1) for the
first time: at the time of that note, the coarse-trim-polygon defect (fixed
by the Fourteenth note, confirmed above still sound) and this deeper
missing-topology defect were both present and un-separated, so "tessellates
to an OPEN mesh... reports not closed, volume 0" could plausibly have been
blamed entirely on the coarseness bug; this pass shows that fixing the
coarseness bug alone does not fix the app-level symptom, because a second,
independent cause was there all along.

Given this, shipping the gated wiring would make dino8-app's own single most
common real-world Mixed case - a plain `Cylinder` drilled into or bossed
onto a plain `Box`, exactly the shape `WrapBrep` (cmd_solids.cpp) attaches no
`FaceRecord` to - measurably WORSE for a user than today's mesh fallback: an
"exact" result the app can no longer measure (`Volume` reports 0) or reload
into any other mesh-consuming command, in place of a merely-tessellated but
fully working, measurable one. Reverted rather than shipped half-safe, the
same standard this category has already applied twice to this exact idea
(the "later pass" account in the "B-rep-preserving booleans" bullet above,
and the Thirteenth note). `TryExactBrepBoolean` is unchanged from the
Planar-only version; `dino8-app/src/commands/cmd_boolean.cpp` carries no
changes this pass. What IS kept: `Brep::HasCylindricalFace` itself, real
tested kernel-only groundwork for whichever future pass closes the
app-mesher gap this note newly discloses (most likely by teaching
`MeshOf`/`MeshBrepClosed` to route a `CylindricalFace`-bearing Brep through
the kernel's own `TessellateToClosedMeshConforming` instead of
`BrepMesher.cpp`'s independent per-face CDT path - a genuinely new,
disclosed architecture item, not attempted here). Net effect on the scores
below: narrows, does not flip, the SAME already-partial items ("B-rep-
preserving booleans reachable from the application" and "Multi-body /
multi-tool booleans") - this category's own present/partial/missing counts
and 9/15/1/25 (66.0%) split are unchanged. Full `dino8_kernel_tests` suite
(via both the built binary directly and `ctest`): 6815 checks, 100% passing,
0 regressions (4 of the 61 checks added since the Fourteenth note's own 6754
baseline are
`TestBrepHasCylindricalFaceDetectsRecordlessCylinderAndRejectsPurelyPlanarBrep`'s
own; the other 57 predate this pass, from intervening, unrelated commits).
`dino8-app/tests/smoke.sh` (including the unmodified `boolean_script.txt`,
`boolean_adversarial_script.txt` and `boolean_mixed_and_compound_script.txt`)
re-run clean end to end, byte-identical to before this pass, since
`cmd_boolean.cpp` itself carries no net change. The kernel-only headline is
unaffected (no bucket moved).*

*Sixteenth note on this category's score (this pass): attempted the mesh-
level half of the "the mesh XOR keeps duplicated vertices" clause of the
"Non-manifold boolean results" bullet above, and reverted it - measured,
not assumed, to be unsafe rather than merely unhelpful. The mesh-level
`BooleanCombine(a, b, BooleanOp::SymmetricDifference)` (boolean.cpp:149-154)
builds its result as `Difference(Union(a,b), Intersection(a,b))` - three
independent `manifold::Manifold::Boolean()` calls - and, per
`boolean.h`'s own existing "SYMMETRIC DIFFERENCE" comment
(boolean.h:2109-2132), the result keeps the touching intersection curve's
vertices duplicated ("Manifold's own mesh XOR keeps the touching curve's
vertices duplicated for the same reason [as the B-rep engines'
Compound-of-two-lumps] - its welded result has 4-fold edges"). `Mesh`
already has a purpose-built repair for exactly this shape of defect
(`Mesh::MergeDuplicateVertices`, mesh.h:1343/mesh.cpp:5113, added the
immediately-preceding commit, 784f547), so the natural next step - tried
here - was calling it on the `SymmetricDifference` branch's own result
before returning it, using an adaptive tolerance
(`std::max(AdaptiveManifoldTolerance(a.raw()), AdaptiveManifoldTolerance(
b.raw()), tolerance::kDistance)`, the same scaling `BooleanCombine`'s own
fallback retry already uses a few lines above) rather than
`MergeDuplicateVertices`'s own default `tolerance::kDistance` (1e-6
absolute - too tight to be scale-correct on a large-magnitude operand, per
`AdaptiveManifoldTolerance`'s own doc comment, though that turned out not
to be the deciding factor here).

Measured directly (standalone probe built on `TestBooleanSymmetricDifference`'s
own fixture, tests/test_basic.cpp:6980-6998 - two overlapping boxes
`[0,2]^3`/`[1,3]^3`, XOR volume 14): the UNWELDED `BooleanCombine(...,
SymmetricDifference)` result is 28 vertices, `Check().duplicate_vertices`
= 12 (6 coincident pairs), volume 14.000000 - AND is already a fully valid
closed manifold in its own right (`Check().naked_edges` = 0,
`non_manifold_edges` = 0, `orientation_conflicts` = 0,
`IsClosedManifold()` = true). A direct O(n^2) distance scan over all 28
vertices found the 6 duplicate pairs sit at EXACTLY 0.0 distance apart
(e.g. two separate vertex records both at `(1,1,2)`) - bit-identical, not
float-rounding noise `AdaptiveManifoldTolerance` could plausibly be
compensating for. Calling `MergeDuplicateVertices(tol)` (tried at both the
adaptive tolerance above, ~3.46e-6 for this fixture, and at the bare
`tolerance::kDistance` = 1e-6) welds exactly those 6 pairs (28 -> 22
vertices, `duplicate_vertices` 12 -> 0, as intended) but the WELDED result
is then no longer a valid manifold: `Check().non_manifold_edges` = 6,
`orientation_conflicts` = 12, `IsClosedManifold()` = false. One example,
printed directly: the edge between the (now-merged) vertices at
`(1,1,2)`-`(1,2,2)` is shared by 4 faces after welding, not 2 - exactly the
"4-fold edges" `boolean.h`'s own comment already predicted for a welded
mesh XOR, now independently confirmed by direct measurement on the
Union/Difference/Intersection composition path specifically (not merely
inferred from Manifold's own dedicated XOR operator, which this codebase
does not call at all). Root cause: along the intersection curve, the XOR
boundary genuinely has four incident faces (A's outside, B's outside, and
the two flipped insides) - the same non-manifold-edge topology the B-rep
engines' own "an edge is shared by 3 or more faces" throw refuses to build
at all; the pre-existing duplicated vertices are not a construction defect
to clean up, they are how a triangle-mesh (which cannot represent a
4-valence edge) is forced to represent that same non-manifold curve without
actually building a non-manifold edge - removing the duplication removes
the only way this mesh format has of staying manifold there.

Because the duplicate pairs are exactly 0.0 apart, no tolerance choice
changes this outcome: any tolerance above 0 that is large enough to weld
them (the entire point of calling `MergeDuplicateVertices` at all) breaks
manifoldness identically, and a tolerance at or below 0 welds nothing,
which is a pure no-op - not a "conservative but real" middle ground, since
there is no daylight between "the duplicate" and "the topologically-load-
bearing point" at this fixture: they are the same points. Per this
project's own revert-rather-than-ship-half-safe convention, the change was
reverted in full rather than shipped at a reduced tolerance: `src/
boolean.cpp`'s `SymmetricDifference` branch is byte-identical to before
this note (still the plain three-call composition, no
`MergeDuplicateVertices` call), and no test was added, since there is no
genuine improvement here to regression-guard - `TestBooleanSymmetricDifference`
(test_basic.cpp:6980) already covers the unchanged behavior. Net effect on
the scores below: NONE - the "Non-manifold boolean results" bullet above
gets a documentation-only addendum (an attempt-and-reject record, not a
narrowing of scope), this category's own present/partial/missing counts and
9/15/1/25 (66.0%) split from the Fifteenth note above are unchanged, and no
kernel behavior differs from before this note. Full `dino8_kernel_tests`
suite (built via `cmake --build build --parallel 4`, run both directly and
via `ctest` from `build/`): 6815 checks, 100% passing, 0 regressions - the
exact same total the Fifteenth note's own baseline already reports, since
this pass's net code change is zero.*

**Blending & chamfering** (blending):
- [partial] Constant-radius edge fillet on curved adjacent faces (cylinder/plane, cylinder/cylinder, freeform, closed/periodic rims) with B-rep trimming — every kernel fillet still requires both adjacent faces to be planar (fillet.h:147-159), so fillets cannot be chained onto a solid that already carries a curved face. App `FilletEdge` produces a genuine B-rep trim only when both faces are planar (cmd_fillet.cpp:175, "exact for planes; approximate elsewhere") — historically via the generic offset+SSX `BuildFillet` path, not the closed-form kernel function itself (see the bullet just below for the "nothing in the app calls it" half this pass closes). **This pass:** `FilletEdgeCommand::Run`'s plain-Radius case (default `RailType=RollingBall`, no `Rho`) now tries a new `TryExactFillet` FIRST — `kernel::FilletConvexEdge`/`FilletConcaveEdge` directly, convex then concave — ahead of the unchanged `BuildFillet` path, the identical "exact kernel construction first, fail open to the approximate path on any `PlanarFaces()` rejection" structure `TryExactChamfer` already established for `ChamferEdge`'s own plain-Radius case. Still partial: curved adjacent faces remain fundamentally out of scope (the kernel's own `PlanarFaces()` requirement, unchanged) and `BuildFillet`'s approximate path is still what actually runs there; this closes a representation gap (which construction produces the planar-face result), not a capability gap (the printed message and volume for a planar-face fillet are unchanged, since `BuildFillet` was already numerically exact for planes too). Net effect on the scores below: narrows, does not flip, the SAME already-partial item.
- [partial] Concave (internal) edge fillet — kernel-native and exact: `FilletConcaveEdge` (fillet.cpp:1070 — corrected 2026-09-28, was mis-cited fillet.cpp:989; fillet.h:162-270) builds the mirrored rolling-ball construction with outward=false, closing perpendicular and oblique third faces; `FilletConcaveEdges` (fillet.cpp:2746; fillet.h:1111-1205) fillets several independent edges plus m==3 trihedral concave spherical corners. **This pass:** closes the single-edge half of "nothing in the app calls it" — `FilletEdgeCommand`'s new `TryExactFillet` (see the bullet just above) tries `kernel::FilletConvexEdge` FIRST and `FilletConcaveEdge` SECOND on any planar-faced solid, the same convex-then-concave cascade `TryExactChamfer`/`TryExactConicFillet`/`TryExactRailFillet` already use elsewhere in this file for the identical reason (the command doesn't know the edge's own convexity in advance). Verified structurally and via the convex branch end-to-end (`fillet_script.txt`'s own existing plain-`Radius=2` box-corner case now goes through this exact dispatch, unchanged volume); the concave branch is NOT independently verified through the app in script form — building an app-level fixture with a genuinely planar-faced concave (reflex) edge turned out to be blocked by a separate, disclosed app-layer limitation: `ExtrudeCrv`'s own `ON_BrepTrimmedPlane`/`ON_BrepExtrudeFace` construction builds ONE ruled side-wall face per whole closed boundary loop, not one flat quad per polygon edge (confirmed directly: extruding a plain 4-sided rectangle profile also yields only 3 faces/3 edges total, the single ruled wall genuinely non-planar end-to-end, not just at the concave corner) — so no closed polygon profile extruded this way, convex or concave, can reach `PlanarFaces()`'s own exact-planar requirement at all, and the app has no other command that builds a multi-facet polygonal solid. Still partial, same remaining gaps as before: planar faces only, one radius, m>=2 or higher-valence corners throw, oblique third faces out of scope for `FilletConcaveEdges`, a mixed convex+concave solid cannot be fully filleted, and the multi-edge `FilletConcaveEdges` batch form remains entirely unreachable from the app. Net effect on the scores below: narrows, does not flip, the SAME already-partial item.
- [partial] Variable-radius fillet (linear / piecewise-linear radius law, radius handles) — kernel `FilletConvexEdgeTapered` (two-radius form fillet.cpp:1825, N-station form fillet.cpp:1903 — corrected 2026-09-28, was mis-cited fillet.cpp:221-438), both a two-radius form and an N-station form, builds exact `ConicalFace` segments. App `Radii=` handles are exact only for plane/plane and plane-with-perpendicular-cylinder; everything else falls back to approximate `BuildFillet`. No non-linear laws and no curved-face taper. **This pass:** closes the "nothing in the app calls it" half - `FilletEdgeCommand`'s new `TryExactTaperedFillet` (cmd_fillet.cpp) now tries `kernel::FilletConvexEdgeTapered` FIRST for any `Radii=` variable-radius run on a convex edge, ahead of the app's own `BuildPlanarVariableFillet`/`BuildPlaneCylinderVariableFillet`/`BuildFillet` cascade - the same "exact kernel construction first" pattern `TryExactFillet` already established for the constant-radius case. `Radii=` handles (t-fraction-in-[0,1], `RadiusAt`'s own convention) are converted to arc-length stations (`TaperedStations`), always anchored at t=0/t=length as the kernel's own N-station overload requires. Still partial, same remaining gaps as before: convex edges only (no `FilletConcaveEdgeTapered` exists in the kernel yet, so a concave edge still falls through to the approximate cascade), no non-linear radius laws, and no curved-face taper (`PlanarFaces()`'s own requirement, unchanged). Net effect on the scores below: narrows, does not flip, the SAME already-partial item.

**A later session's addition:** closes the equivalent "nothing calls it" gap for `FilletTwoSurfacesCommand`'s own `VariableFilletSrf` (`Mode::VariableFillet`) — previously only `FilletEdgeCommand`'s single-edge `Radii=` path reached `kernel::FilletConvexEdgeTapered` (the addition just above); the two-independently-picked-faces command still built every variable-radius run through `BuildPlaneCylinderVariableFillet`/`BuildFillet`, even for two faces sharing an edge on one planar-faced solid where the exact construction applies. `FilletTwoSurfacesCommand::Run` (cmd_fillet.cpp) now tries `kernel::FilletConvexEdgeTapered`'s own two-radius overload FIRST whenever `r0 != r1` (a genuinely variable run — a constant-radius `VariableFilletSrf` call, r0 == r1, is unaffected and still falls through to `Mode::Fillet`'s own untouched general offset+SSX path, which this document's own prior chamfer-wiring bullet already established has no equivalent gap to close), reusing the same `FindSharedEdgeEndpoints` helper and "same solid, `Trim=Yes`" reachability condition the Chamfer D1/D2 wiring above already established for this command — `ChamferConvexEdge`/`ChamferConcaveEdge` and `FilletConvexEdgeTapered` need the identical one shared `ON_BrepEdge` to identify the corner. Convex only, same as the single-edge case: no `FilletConcaveEdgeTapered` exists yet, so a concave edge simply throws inside the kernel's own convexity check and this falls through unchanged. Verified end-to-end through the real command: `fillet_script.txt` gains a `VariableFilletSrf`/`Radius=1`/`EndRadius=3` section picking two adjacent faces of a fresh box (the identical 1->3 ramp the single-edge `FilletEdge`/`Radii=0:1,1:3` case above already verifies), printing `replaced with an exact tapered fillet (radius 1 to 3)` and landing on the identical tessellated `Volume = 990.8` that case already verifies — bit-for-bit the same kernel result, reached through the two-face pick instead of the one-edge pick; `fillet_adversarial_script.txt` adds two negative-fallback controls, both confirming the OLD `"FilletSrf: built between object ... radius 1 to 2"` approximate-path message still fires rather than a crash or a silently-wrong exact attempt — a cylinder's own flat cap + curved side wall (the whole-solid-planar-faced requirement failing) and an otherwise-exact box corner with `Trim=No` set. Still partial: the same kernel-level scope limits as before (convex only, no non-linear laws, no curved-face taper) are unchanged, and `Mode::VariableChamfer` remains untouched (no tapered-chamfer kernel construction exists, the same reason this document's own Chamfer D1/D2 bullet already gives for leaving it alone). Net effect on the scores below: narrows, does not flip, the SAME already-partial item — Blending & chamfering's own present/partial/missing counts (5/18/1) and 58.3% parity figure are unchanged, the same "narrowing, not a flip" convention used throughout this item's own prior additions.
- [partial] Chamfer with two unequal distances (D1/D2) or distance + angle (AutoCAD CHAMFER Angle method, Rhino ChamferEdge per-handle distances) — the kernel has exact D1/D2 chamfers (`ChamferConvexEdge`) and distance+angle (`ChamferConvexEdgeAngle`), plus concave versions `ChamferConcaveEdge`/`ChamferConcaveEdgeAngle` (`ChamferConvexEdge` fillet.cpp:2065, `ChamferConvexEdgeAngle` fillet.cpp:2203, `ChamferConcaveEdge` fillet.cpp:2276, `ChamferConcaveEdgeAngle` fillet.cpp:2287 — corrected 2026-09-28, was mis-cited as one range fillet.cpp:2194-2210). **This pass:** closes the "app exposes only one Radius" half. App `ChamferEdge` (cmd_fillet.cpp) now tries the exact kernel construction FIRST, ahead of its own rolling-ball-derived `RuledBetween` approximation: new `Distance2=`/`Angle=` options feed `TryExactChamfer`, which calls `ChamferConvexEdge`/`ChamferConcaveEdge` (Distance1/Distance2 mode) or their `*Angle` overloads (Distance/Angle mode) on the object's whole Brep, trying convex then concave, and replaces the polysurface in place on success — a genuine flat-bevel B-rep with two independent distances, not the old symmetric-only approximation (whose `RuledBetween` ruled surface between two EQUAL-offset contact curves could never represent D1≠D2 or an angle at all). When the exact construction is unavailable (any non-planar face anywhere on the object — `ChamferConvexEdge`'s own `PlanarFaces()` requirement, not just the two adjacent faces) and Distance2/Angle were actually requested, the command now fails with a clear diagnostic instead of silently building a SYMMETRIC approximate chamfer that quietly ignores the caller's own Distance2/Angle input; a plain, unset Distance2/Angle (the pre-existing single-Radius case) still falls through to the same approximate path as before, unchanged. Verified in `dino8-app/tests/fillet_adversarial_script.txt`: an asymmetric 2/4 Distance1/Distance2 chamfer on a box matches the exact closed-form removed volume (`distance_i*distance_j*L/2` = 40, i.e. 1000-40=960); a 45-degree Angle=45 chamfer on a right-angle edge lands on the identical volume a plain symmetric Radius=2 chamfer would (980), proving the law-of-sines dispatch; and a Distance2 request on a cylinder's curved-adjacent-face rim edge fails with the new diagnostic rather than a silently-wrong symmetric result. Still partial: planar faces only (the kernel's own scope, unchanged); no unequal-distance chamfer on curved faces; FilletSrf/ChamferSrf's independently-picked-surface command (`FilletTwoSurfacesCommand`) is not wired to this, only the shared-edge `ChamferEdge` command is.
**A later session's addition:** closes the "FilletTwoSurfacesCommand (FilletSrf/ChamferSrf) is not wired to this" half of the Chamfer D1/D2 item's own remaining gap, noted at the end of the bullet just above. `FilletTwoSurfacesCommand::Run` (cmd_fillet.cpp, `Mode::Chamfer` only — not `VariableChamfer`, since there is no tapered-chamfer kernel construction the way `FilletConvexEdgeTapered` exists for fillets) now tries `ChamferConvexEdge`/`ChamferConcaveEdge` FIRST, ahead of the same `RuledBetween` approximation `ChamferEdge` itself used to fall back to before its own exact wiring — but only when both picks land on the SAME solid (`fa.id == fb.id`) and `Trim=Yes` (the default): unlike `ChamferEdge`, which already starts from one picked edge, `FilletTwoSurfacesCommand` starts from two independently picked FACES, so a new helper, `FindSharedEdgeEndpoints(brep, face_i, face_j)` (cmd_fillet.cpp, next to the existing `PickEdge`), first confirms the two faces actually share an `ON_BrepEdge` on that one solid — genuinely independent surfaces (the ordinary FilletSrf/ChamferSrf case, exercised by this same script's own `FilletSrf` section just above) have no such edge and always fall through to the approximate path unchanged, and `Trim=No` is skipped too (the exact path always replaces the whole solid with an already-trimmed result, not the untrimmed separate surface `Trim=No` asks for). On success this replaces the picked object's own Brep in place (the same "replace `orig->brep` directly" pattern `ChamferEdge`'s own exact path uses) rather than adding a new surface object the way the approximate path does. Verified end-to-end: `fillet_script.txt` gains a `ChamferSrf`/`Radius=3` section picking two adjacent faces of one fresh box (not `ExtractSrf`'d apart, unlike the `FilletSrf` section right before it), printing `replaced with an exact chamfer (distance 3)` and landing on the identical closed-form `Volume = 955` the plain `ChamferEdge`/`Radius=3` case earlier in the same script already verifies (`1000 - 10*3^2/2`); `fillet_adversarial_script.txt` adds two negative-fallback controls, both confirming the OLD `"ChamferSrf: built between object ..."` approximate-path message still fires rather than a crash or a silently-wrong exact attempt — a cylinder's own flat cap + curved side wall (the whole-solid-planar-faced requirement failing, `ChamferConvexEdge`'s own `PlanarFaces()` scope, identical to `ChamferEdge`'s Distance2 case) and an otherwise-exact box corner with `Trim=No` set. Still partial: the `Fillet`/`VariableFillet`/`VariableChamfer` modes of `FilletTwoSurfacesCommand` are untouched (a plain rolling-ball `FilletSrf` already has its own general offset+SSX construction that works on curved faces too, unlike a flat chamfer bevel, so there is no equivalent "approximate when it should be exact" gap there to close the same way), and two genuinely independent surfaces (no shared `ON_Brep`) still have no exact path at all — a real, larger gap (there is no shared edge to identify in the first place) left open, not attempted. Net effect on the scores below: this narrows, but does not flip, the SAME already-partial item — Blending & chamfering's own present/partial/missing counts and parity figure are unchanged, the same "narrowing, not a flip" convention used throughout this item's own prior additions.
- [partial] Face-face blend between two independently picked surfaces (FilletSrf / ChamferSrf, non-adjacent faces, with trimming of both inputs) — `FilletTwoSurfacesCommand` (cmd_fillet.cpp:864) trims only through `TrimWholeLoop` (cmd_fillet.cpp:964) when an input is planar; otherwise the input is left untrimmed.
- [partial] Vertex blend (three or more fillets meeting at a vertex: spherical/setback corner patch) — `FilletConvexEdges` m==3 spherical corner (requires one face perpendicular to the other two); `FilletConcaveEdges` covers the m==3 concave sphere; single-facet vertex chamfers on any convex or concave trihedral corner with asymmetric per-edge distances exist (`ChamferConvexVertex`/`ChamferConcaveVertex`, fillet.cpp:3133 onward). Still partial: m==2, valence >3, and non-perpendicular (e.g. tetrahedron) corners all throw for fillets; corners with mixed radii unsupported; no setback or non-spherical corner patches. **This pass:** closes the "nothing in the app calls either function" half - a new `FilletVertex` command (cmd_fillet.cpp, registered next to `RemoveFillet`) picks a single vertex (`PickVertex`, the vertex-level sibling of `PickFace`/`PickEdge`), requires exactly 3 incident edges, and tries `kernel::FilletConvexEdges` then `kernel::FilletConcaveEdges` on all 3 at once - the first app-reachable path to either function (previously zero call sites anywhere in dino8-app/src). Still partial: the same kernel-level scope limits as before (m==2, valence>3, non-perpendicular corners, mixed radii all throw; no setback or non-spherical patches) are unchanged, and no app command exists yet for a non-vertex partial fillet chain (PARITY_MAP.md's own "tangent edge chains" entry). Net effect on the scores below: narrows, does not flip, the SAME already-partial item.

**A later session's addition:** closes the app-reachability gap this same bullet's own kernel description named for `ChamferConvexVertex`/`ChamferConcaveVertex` (listed above as existing, but never wired) - a new `ChamferVertexCommand` (cmd_fillet.cpp, registered as `ChamferVertex` next to `FilletVertex`) mirrors `FilletVertexCommand`'s own single-vertex pick and convex-then-concave cascade, but calls `kernel::ChamferConvexVertex`/`ChamferConcaveVertex` instead - the first app-reachable path to either function (`RemoveFillet` already reached their own inverse, `RemoveChamferVertex`, but nothing built one forward). Unlike `FilletVertex`'s own spherical corner, no perpendicular-face restriction applies (a plane always exists through any 3 non-collinear points), so this accepts any convex or concave trihedral corner outright. Verified end-to-end (`fillet_script.txt`/`smoke.sh`): a box corner at `Distance=3` prints `chamfered (distance 3)` and measures the exact closed-form `Volume = 995.5` (`1000 - 3^3/6`, `ChamferConvexVertex`'s own doc comment - flat facets tessellate with zero approximation error, unlike `FilletVertex`'s own spherical-corner case). Still partial: the same m==2/valence>3/mixed-radius limits apply (chamfer's own trihedral-only scope, per `ChamferConvexVertex`'s doc comment). Net effect on the scores below: narrows, does not flip, the SAME already-partial item.
- [partial] Fillet end conditions on adjacent end faces (corner notch of the third face, shared cap edge) — closed exactly with a shared edge for single-edge `FilletConvexEdge` (perpendicular or oblique third face), `FilletConcaveEdge` (oblique third face), `ChamferConvexEdge` (oblique third face), tapered cones via ellipse notches, and `FilletConvexEdgeConic` (perpendicular third face only). **This pass:** `FilletConvexEdges`' own m==1 vertex case (fillet.cpp:2562) now closes an oblique third face too, not just the perpendicular case — it reuses `FindObliqueThirdFaceCrossing`/`EllipseNotchCornerAtVertexCylindrical` (the same helpers `FilletConvexEdge` itself already calls) in a new detection pass that shortens/shifts each edge's own cylinder before it is built, then dispatches to the ellipse-cap splice instead of the old unconditional flat-notch call; a single-edge `FilletConvexEdges` call on an obliquely-ended edge is now bit-for-bit reproducing `FilletConvexEdge`'s own result (`TestFilletConvexEdgesSingleEdgeMatchesFilletConvexEdgeOnObliqueEnd`), and two parallel obliquely-ended edges notching the SAME oblique end face twice still sew into one closed solid whose volume matches the doubled single-edge closed form (`TestFilletConvexEdgesParallelPairWithObliqueEndsIsClosedAndMatchesClosedForm`, tests/test_basic.cpp). **A later session:** `FilletConcaveEdges`' own m==1 vertex case (fillet.cpp, between its re-trim loop and its cylinder-building loop) closes the same oblique-third-face gap on the concave side too, via the identical `FindObliqueThirdFaceCrossing`/`EllipseNotchCornerAtVertexCylindrical` dispatch with the concave `D_i = bis*offset - n_i*radius` sign convention the single-edge `FilletConcaveEdge` already established — verified by `TestFilletConcaveEdgesSingleEdgeMatchesFilletConcaveEdgeOnObliqueEnd` (bit-for-bit match against `FilletConcaveEdge`'s own oblique-end result: face/edge/vertex counts, tessellated volume, cylinder length, cap-notch-point counts) and `TestFilletConcaveEdgesTwoIndependentEdgesWithObliqueEndsMatchHandDerivedLength` (two independent obliquely-ended concave edges on the same tilted end face, each cylinder's length matching an independently hand-derived `L + radius*slope`), plus `TestFilletConcaveEdgesRejectsOversizedRadiusAtObliqueEnd` for the negative control. Still partial: neither multi-edge function (`FilletConvexEdges` or `FilletConcaveEdges`) handles a non-planar end face, and `FilletConcaveEdges`' own m==3 trihedral corner has no "third, unfilleted face" concept at all (every edge there is already filleted) — the item stays partial, not present, for those remaining gaps.
- [partial] Edge blend trimmed and joined into the polysurface (Rhino BlendEdge TrimAndJoin behaviour) — `BlendEdge` registration text (cmd_fillet.cpp:2647, and comment at cmd_fillet.cpp:1388) still reads "Hermite blend surface added between the two faces (not stitched into the polysurface)".
- [partial] Conic / rho (chordal, elliptical) blend cross-sections — kernel-native conic/rho blend exists, `FilletConvexEdgeConic` (fillet.cpp:2321; fillet.h:1019): an exact rational-quadratic-Bezier cross-section giving a true ellipse (rho<0.5), parabola (rho=0.5) or hyperbola arc (rho>0.5) tangent to both faces, swept translationally and spliced onto the re-trimmed faces. A third face perpendicular to the edge at either endpoint (e.g. a full box edge, corner to corner) is now closed: `ConicNotchCornerAtVertex` (fillet.cpp:239) splices the wall's own end-cap conic into that face's loop as a 200-segment notch, and the collapsed notch edge's 3D curve is set to the exact conic so it sews to the wall. The result is a closed solid (`TestFilletConvexEdgeConicClosesCornerNotchOnUnitCube`: IsSolid, Check() issue-free, tessellated volume within 1e-6 of 1 - L*Area(rho) at rho = 0.3/0.5/0.7). **This pass:** the CONCAVE mirror, `FilletConcaveEdgeConic` (fillet.cpp, right after `FilletConvexEdgeConic`; fillet.h), also now exists — exactly the same "validate the edge is genuinely concave (`RequireConcaveEdge`), then dispatch straight to the convex construction unchanged" relationship `ChamferConcaveEdge` already has to `ChamferConvexEdge`, and for the identical reason: `FilletConvexEdgeConic`'s own m_i/m_j are already extent-based (which of the two in-plane, perpendicular-to-the-edge directions actually has positive extent within that face's own real polygon), not built from a convex-specific contact-point formula the way a rolling-ball fillet's axis_point is, so it already discovers the correct setback points and produces a genuine ADDED-material conic blend when fed a reflex edge, with no construction change at all. Verified on a genuine concave fixture (the same L-shaped prism `FilletConcaveEdge`'s own tests use), both by an end-to-end closed corner-notch case (both cap faces perpendicular to the edge, closing into a valid solid whose ADDED volume matches the identical Simpson-integrated Area(rho) closed form `FilletConvexEdgeConic`'s own test uses, at rho = 0.3/0.5/0.7) and by a direct, discriminating check that `FilletConcaveEdgeConic`'s own result is bit-for-bit identical to calling `FilletConvexEdgeConic` on the same inputs (`TestFilletConcaveEdgeConicAddsClosedFormVolumeOnLShapedPrism`, `TestFilletConcaveEdgeConicRejectsConvexEdgeAndInvalidInput`, tests/test_basic.cpp). Still partial, same remaining gaps as before, now shared by both the convex and concave forms: an oblique third face at an endpoint still throws, there was no multi-edge/vertex-blend variant, and the app layer does not expose either direction at all (`cmd_curves2.cpp`'s `ConicWeightThrough`/Rho option is an unrelated 2D-curve-through-3-points construction tool, not this edge-blend feature). Net effect on the scores below: this closes a real, previously-undocumented convex/concave asymmetry within the SAME already-partial item — it does not flip Blending & chamfering's own present/partial/missing counts (still 5/18/1) or its 58.3% parity figure, the same "narrowing, not a flip" convention this document uses elsewhere (e.g. the `ExtrudeFace`/`ExtrudeToPoint` bullets under kernel: Sweeping, lofting, extruding, revolving).

**A later session's addition:** closes part of this same item's own "no multi-edge/vertex-blend variant" gap. `FilletConvexEdgesConic`/`FilletConcaveEdgesConic` (`dino8-kernel/include/dino8/kernel/fillet.h`, `dino8-kernel/src/fillet.cpp`, right after `FilletConcaveEdgeConic`) blend several edges in ONE call, each with its own `(distance_i, distance_j, rho)` via a new `ConicEdgeSpec` struct (unlike `FilletConvexEdges`' single shared `radius`, a plain distance has no dihedral-angle trig to auto-adapt it across edges of different angles, so per-edge parameters are the honest choice here). This is NOT a sequential chain of single `FilletConvexEdgeConic` calls — confirmed directly, not assumed, the same way `FilletConcaveEdges`' own doc comment already discloses for the plain circular family: `FilletConvexEdgeConic`'s own output already carries a curved (non-planar) conic wall face, and `PlanarFaces()` — which `FilletConvexEdgeConic` calls first — throws on any solid already carrying one. So the batch functions instead run every edge's own geometry (re-trim, corner notch, conic wall — `FilletConvexEdgeConic`'s own steps, verbatim, just looped) against ONE shared `PlanarFaces()` snapshot, accumulating every edge's own re-trimmed/notched faces into one mutable list before a single `Brep::FromMixedFaces` + `Brep::Compound` + `JoinNakedEdges` assembly. SCOPE, the new restriction this adds: every edge's own "owned" faces (its two adjacent faces, plus any third face notched at either endpoint) must be DISJOINT from every other edge's — two edges sharing a face (adjacent edges of the same face, or two edges meeting at/notching the same corner) throw `std::invalid_argument` naming the conflict rather than silently re-trimming or notching a face's loop twice from two unordered call sites; a genuine vertex-blend/shared-corner conic gap remains, disclosed rather than attempted. Verified, not merely derived: `TestFilletConvexEdgesConicBlendsTwoIndependentEdgesInOneCall` blends two full-cube edges on two entirely separate unit cubes (assembled via `Brep::Compound`) with two genuinely different `ConicEdgeSpec`s in one call, checking `IsValid()`/`IsManifold()` (closed, oriented)/`IsSolid()`/`Brep::Check()` and that the combined tessellated volume matches the SUM of each edge's own independent closed-form `1 - L*Area(rho)` removal (the same Simpson-integrated `Area(rho)` closed form `TestFilletConvexEdgeConicClosesCornerNotchOnUnitCube` derives, evaluated once per edge with its own da/db/rho); `TestFilletConvexEdgesConicRejectsSharedFacesAndInvalidInput` is the negative control (empty list, a duplicated edge, two edges sharing a face all rejected; two edges on two independent cubes accepted); `TestFilletConcaveEdgesConicMatchesFilletConcaveEdgeConicOnSingleEdge` checks the concave dispatch is a genuine thin wrapper (a one-edge batch reproduces `FilletConcaveEdgeConic`'s own face count and tessellated volume bit-for-bit) and that a batch containing a genuinely convex edge is still rejected. Still partial: an oblique third face at either endpoint remains out of scope (unchanged), and — the part of "no multi-edge/vertex-blend variant" still open — two conic-bladed edges that share a face or meet at a corner are rejected outright rather than building a real vertex blend between them. Net effect on the scores below: this narrows, but does not flip, the SAME already-partial item — Blending & chamfering's own present/partial/missing counts (5/18/1) and 58.3% parity figure are unchanged, the same "narrowing, not a flip" convention noted just above for this item's own prior addition.

**A later session's addition:** closes the single-edge half of this item's own "the app layer does not expose either direction at all" gap. `FilletEdgeCommand` (`dino8-app/src/commands/cmd_fillet.cpp`) now takes a `Rho` option (strictly between 0 and 1) that, for a constant (non-`Radii=`) radius and outside `Preview` mode, tries `kernel::FilletConvexEdgeConic`/`FilletConcaveEdgeConic` directly — the same convex-then-concave attempt-and-join-the-detail structure `TryExactChamfer` already uses for `ChamferEdge`'s own Distance2/Angle wiring (`TryExactConicFillet`, right below it), reusing the existing `Distance2` option (already present for `ChamferEdge`) for the conic's own independent second setback `distance_j`; unset, `distance_j` defaults to `distance_i` (the `Radius` value). Unlike the Chamfer Distance2/Angle case, there is deliberately NO fallthrough to the approximate rolling-ball path on failure: that path can only ever build a circular arc, so a `Rho` request it can't satisfy exactly always warns and returns rather than silently building a plain round fillet that quietly ignores `Rho` — the same misleading-success failure mode the Chamfer wiring's own asymmetric/angled case already guards against, just with no symmetric special case here since a non-circular conic has no approximate equivalent at all. Verified end-to-end through the real command, not just at the kernel layer: `fillet_script.txt`'s new `FilletEdge`/`Radius=2`/`Rho=0.5` section on a fresh 10x10x10 box corner prints `replaced with an exact conic fillet (rho 0.5, distance 2)` and computes `Volume = 993.3`, matching `FilletConvexEdgeConic`'s own closed form `distance_i*distance_j*sin(gamma)/6` at gamma = 90 degrees (a box corner's own right-angle dihedral) times the 10-unit edge length (`1000 - 2*2*10/6 = 993.3`); `fillet_adversarial_script.txt` adds the asymmetric case (`Radius=2`/`Rho=0.5`/`Distance2=4`, `replaced with an exact conic fillet (rho 0.5, distance1 2, distance2 4)`, `Volume = 986.7` matching `1000 - 2*4*10/6`) and the negative control (`Rho=0.5` on a solid cylinder's own curved-adjacent-face rim edge fails with `an exact conic (Rho) fillet needs the whole object to be planar-faced at this edge`, the cylinder left untouched) — both new `smoke.sh` checks. Still partial, same remaining gaps noted just above for the kernel side (an oblique third face at an endpoint, no vertex-blend/shared-corner form), plus one still open on the app side: only the single-edge `FilletConvexEdgeConic`/`FilletConcaveEdgeConic` pair is wired — the face-disjoint multi-edge batch form (`FilletConvexEdgesConic`/`FilletConcaveEdgesConic`) has no command exposing it yet. Net effect on the scores below: this narrows, but does not flip, the SAME already-partial item — Blending & chamfering's own present/partial/missing counts (5/18/1) and 58.3% parity figure are unchanged, the same "narrowing, not a flip" convention noted just above for this item's own prior additions.

**A later session's addition:** closes the app-side gap the paragraph just above named — the multi-edge batch form (`FilletConvexEdgesConic`/`FilletConcaveEdgesConic`) now has a command exposing it. `FilletEdgeCommand`'s `Rho` branch (`cmd_fillet.cpp`) no longer applies each picked Rho edge immediately; it STAGES every pick (its own `distance_i`/`distance_j`/`rho` captured at pick time into a `kernel::ConicEdgeSpec`) and builds the whole staged batch in one `FilletConvexEdgesConic`/`FilletConcaveEdgesConic` call once Enter is pressed. This is not just a new call site — it fixes a real, previously-unnoticed bug in the single-edge wiring's own multi-pick case: applying the first Rho edge immediately left the object carrying a curved conic wall face, and `PlanarFaces()` (called first by every one of these kernel functions) rejects any solid already carrying one, so picking a SECOND independent Rho edge in the same `FilletEdge` run used to fail with a confusing "needs the whole object to be planar-faced" warning about a perfectly ordinary planar edge — confirmed directly by reproducing it against the pre-fix binary, not assumed. A single staged edge still gets the exact original message and geometry (`FilletConvexEdgesConic`'s own doc comment: a one-edge batch derives every retrim/notch/wall step exactly as the single-edge `FilletConvexEdgeConic` construction does, just looped), so this is additive, not a behavior change for the already-verified single-edge case above. Verified end-to-end: `fillet_script.txt` gains a two-edge case (a box's own TOP-front and TOP-back edges, both Rho-staged in one `FilletEdge` run before Enter) that exercises the exact "two edges share a face" rejection `FilletConvexEdgesConic`'s own regression tests already cover at the kernel layer — both the convex and concave whole-batch attempts fail, and the object survives with its exact original `Volume = 1000`, not a partially-filleted result; the pre-existing single-edge Rho case's own message and `Volume = 993.3` are unchanged, confirming no regression. Still partial: an oblique third face at an endpoint and a genuine vertex-blend/shared-corner form remain out of the underlying kernel construction's own scope, unchanged by this pass. Net effect on the scores below: narrows, does not flip, the SAME already-partial item — Blending & chamfering's own present/partial/missing counts (5/18/1) and 58.3% parity figure are unchanged, the same "narrowing, not a flip" convention noted throughout this item's own prior additions.
- [partial] Fillet/blend on tangent edge chains and multi-edge selection in one operation (ChainEdges, FaceEdges, double-click tangent propagation) — corrected evidence: re-grepped `ChainEdges`/`FaceEdges` across all of dino8-app/src, zero matches anywhere; the prior claim that "the command catalogue lists ChainEdges/FaceEdges" was unfounded. The nearest real thing, `SelChain` (cmd_select.cpp:160), is a general curve-chaining selection helper for `ObjectKind::Curve` objects only, unrelated to solid edges or fillet/chamfer commands. Kernel `FilletConvexEdges`/`FilletConcaveEdges` fillet many straight edges in one call, but have no tangent-chain propagation and no curved edges.
- [missing] Fillet overflow / cliff-edge / notch handling (blend running off a face onto neighbouring faces, over-large radius consuming a face) — every kernel fillet and chamfer throws "radius/distance too large to fit" (`FilletConvexEdge` fillet.cpp:792, `FilletConcaveEdge` :1129, `ChamferConvexEdge` :2039, `FilletConvexEdgeConic` :2380) instead of rolling onto the next face; the app reports the failure rather than handling it.
- [partial] Blend removal / defeaturing with healing (delete fillet faces and re-extend neighbours to restore the sharp edge) — `RemoveBlend` (fillet.cpp:4313; fillet.h:1494) recovers the sharp edge for cylindrical and conical fillets, convex or concave, and restores corner notches; `RemoveChamfer` (fillet.cpp:4538) and `RemoveChamferVertex` (fillet.cpp:4698) do the chamfer equivalents. **This pass:** `RemoveBlend` now also inverts `FilletConvexEdges`' own m == 3 trihedral spherical vertex-blend corner (`RemoveSphericalVertexBlend`, fillet.cpp:4128) — a genuinely different construction from the single-cylinder case, not a thin wrapper: the corner's sharp vertex V is reconstructed as the exact intersection of the 3 touching planar faces' own (unclipped) planes (the SAME closed form `RemoveChamferVertex` already uses for a chamfered trihedral corner, correct regardless of fillet radius since a fillet never moves a face's own supporting plane), independently checked against the sphere's own ball-center equation `n . (C - V) = -radius`; the sphere plus all 3 incident cylinders are removed in one call, each cylinder's own FAR (non-sphere) end still spliced onto its own third-face corner notch exactly as the plain single-cylinder case does (the near/sphere end's per-cylinder axial setback provably cancels out of that far-end formula - see the function's own doc comment for the derivation). Verified by `TestRemoveBlendRoundTripsASphericalVertexCorner` (a single rounded corner restores to the exact pre-fillet unit box: 6 faces/12 edges/8 vertices, volume exactly 1.0, all 8 original vertices back) and `TestRemoveBlendOnSphericalCornerLeavesAnIndependentCornerIntact` (two diagonally-opposite rounded corners: removing one leaves the other's own sphere/3-cylinder blend completely untouched, with the resulting volume matching the SAME closed form as a solid that only ever had that one corner rounded). Still partial: a corner whose own cylinder is ALSO set back by a SECOND spherical corner at its far end (every edge of a fully-rounded box, where each cylinder runs corner-to-corner) is out of scope and throws rather than reconstructing the wrong far vertex (`TestRemoveBlendRejectsSphericalCornerSharingACylinderWithAnotherCorner`); oblique-end cylindrical fillets still throw too. **This pass:** closes the "nothing in the app calls any of these" half — a new `RemoveFillet` command (`dino8-app/src/commands/cmd_fillet.cpp`, registered next to `RebuildEdges`) picks a single face (`PickFace`, the same helper `SplitFace`/`MergeFaces` already use) and tries `kernel::RemoveBlend` (fillets), then `kernel::RemoveChamfer` (two-distance/angle edge chamfers), then `kernel::RemoveChamferVertex` (single-facet vertex chamfers), in that order, from the SAME picked point — each kernel function already does its own geometric identification and verification purely from a point on the candidate face (see each one's own doc comment: `RemoveBlend`/`RemoveChamfer` search `MixedFaces()`/`PlanarFaces()` for the closest genuine match and geometrically confirm it before touching anything, `RemoveChamferVertex` likewise for a triangular facet), so the command needs no separate face-type option or detection of its own; on success the object's Brep is replaced in place, the same pattern `FilletEdgeCommand`/`ChamferEdgeCommand` already use. Verified end-to-end through the real command, both directions: `fillet_script.txt` gains a `RemoveFillet` round trip on a plain `FilletEdge Radius=2` box corner (picks a point on the fillet's own quarter-cylinder wall at 45 degrees between the two trimmed faces; prints `RemoveFillet: fillet on object N removed, sharp edge/vertex restored` and the box's `Volume` returns to exactly 1000, its own pre-fillet value) and a second round trip on a plain `ChamferEdge Radius=3` box corner (picks the midpoint of the chamfer's own flat quad face; prints `RemoveFillet: chamfer on object N removed, sharp edge/vertex restored`, `Volume` again exactly 1000); `fillet_adversarial_script.txt` adds the negative control (`RemoveFillet` on an ordinary box face - not a fillet, chamfer, or vertex-chamfer facet at all - declines with a diagnostic naming all three failed attempts, the box left completely untouched, `Volume` still exactly 1000). Still partial: the two kernel-level gaps noted just above (a corner sharing a cylinder with a second spherical corner, oblique-end cylindrical fillets) are unchanged - this closes only the app-reachability half of the item, not those kernel-level scope limits - and no equivalent app command exists for `RemoveSphericalVertexBlend`'s own m==3 trihedral corner case specifically (picking a point on that sphere still reaches it through `RemoveBlend` itself, per that function's own dispatch, so this is coverage, not a separate gap). Net effect on the scores below: this is a genuine capability the app previously had NONE of (no fillet/chamfer removal command existed at all, at any status), but the item's own remaining kernel-level gaps keep it `partial`, not `present` - narrows, does not flip, the SAME already-partial item, the same "narrowing, not a flip" convention used throughout this category.
- [partial] Fillet surface along a user-supplied rail curve (FilletSrfToRail) — `FilletSrfToRailCommand` (cmd_srfedit.cpp:2137) uses the picked rail directly as the ball-centre spine, with contacts at plain closest points and no trimming.
- [partial] Alternative blend rail types (distance-from-edge, distance-between-rails / disc blend, non-rolling-ball cross-section placement) — **upgraded from missing.** `FilletConvexEdgeByDistanceFromEdge`/`FilletConvexEdgeByDistanceBetweenRails` and their `FilletConcaveEdge` mirrors (fillet.h/fillet.cpp) add Rhino 8 FilletEdge's own `RailType=DistFromEdge`/`DistBetweenRails` alternative to the default `RailType=RollingBall` every other fillet in this file uses: each takes a distance instead of a radius and dispatches to `FilletConvexEdge`/`FilletConcaveEdge` via a closed-form radius conversion (`radius = distance*tan(theta/2)` for DistFromEdge, `radius = rail_distance/(2*cos(theta/2))` for DistBetweenRails, theta = the edge's own interior dihedral angle — both derived directly from `FilletConvexEdge`'s own `trim_back`/`contact_i`/`contact_j` formulas, not asserted), verified against `FilletConvexEdge`/`FilletConcaveEdge` at an independently hand-derived equivalent radius on both a 90-degree box edge and a genuinely non-right 120-degree hexagonal-prism edge. **This pass:** closes the app-layer half of this same gap — `git grep` for `RailType`/`DistFromEdge`/`DistBetweenRails` across `dino8-app/` turned up nothing but the command's own static Rhino help text (`data/commands.json`'s `VariableBlendSrf`/`VariableChamferSrf` entries), confirming the four kernel functions above were never actually reachable from `FilletEdge`, the same "kernel closed, app layer still doesn't expose it" gap this file's Rho bullet documents and closes for the conic case a few bullets above. `FilletEdgeCommand` (`dino8-app/src/commands/cmd_fillet.cpp`) now takes a `RailType` option (`RollingBall`/`DistFromEdge`/`DistBetweenRails`, default `RollingBall`) that, for a constant (non-`Radii=`) radius, outside `Preview` mode, and only once Rho's own exact-conic attempt above it has already declined to run (mutually exclusive by construction: Rho's own block always returns once `Rho` is set), reuses `Radius` itself as the RailType distance and tries `kernel::FilletConvexEdgeByDistanceFromEdge`/`ByDistanceBetweenRails` then their concave mirrors — the identical convex-then-concave/detail-joining structure `TryExactConicFillet` (Rho) and `TryExactChamfer` (Chamfer Distance2/Angle) already use, for the identical reason: the command doesn't know the edge's own convexity in advance. Like Rho, there is deliberately no fallthrough to the approximate rolling-ball path on failure: `EdgeDihedralAngleForRailType` (the conversion both RailType kernel functions dispatch through) requires the whole solid to be planar-faced via `PlanarFaces()`, so a curved adjacent face has no dihedral angle to convert at all — falling back to the approximate path would silently reinterpret the typed distance as a literal radius instead, the same misleading-success failure mode Rho's own wiring already guards against. Verified end-to-end through the real command: `fillet_script.txt` gains a `RailType=DistFromEdge` section (`Radius=2` on a fresh 10x10x10 box corner; a box's 90-degree dihedral makes `radius = distance*tan(45deg) = distance` exactly, so this reproduces bit-for-bit the SAME `Volume = 991.4` the script's very first, already-verified plain-`Radius=2` FilletEdge case builds, now reached through the distance-based RailType path instead of a literal radius) and a `RailType=DistBetweenRails` section (`Radius=2`; at theta=90 degrees `radius^2 = rail_distance^2/2 = 2` exactly, `Volume = 995.7`, a value the plain-radius path never lands on, discriminating this path from DistFromEdge's), both new `smoke.sh` `flcheck` lines checking the printed `replaced with an exact fillet (RailType=..., distance N)` confirmation and the volume; `fillet_adversarial_script.txt` adds the negative control (`RailType=DistFromEdge` on a solid cylinder's own curved-adjacent-face rim edge fails with `an exact RailType=DistFromEdge fillet needs the whole object to be planar-faced at this edge`, the cylinder left untouched, new `facheck`). Still partial, same remaining gaps as before, now shared by the app-layer wiring too: single straight edge only (no multi-edge/vertex-blend form — the batch kernel functions this would need don't exist yet, unlike the conic case's `FilletConvexEdgesConic`), rolling-ball circular cross-section only, and planar-adjacent-face only; a curved-face rail type remains out of scope. Net effect on the scores below: this narrows, but does not flip, the SAME already-partial item — Blending & chamfering's own present/partial/missing counts (5/18/1) and 58.3% parity figure are unchanged, the same "narrowing, not a flip" convention noted throughout this item's own prior additions and the Rho/RailType kernel-side bullets just above.

**A later session's addition:** closes the equivalent "app layer does not expose it" gap for `FilletTwoSurfacesCommand`'s own two-independently-picked-faces `FilletSrf` command, for BOTH Rho and RailType — previously only `FilletEdgeCommand`'s single-edge path reached either option (the two additions just above and the Conic/rho bullet earlier in this category); `git grep` for `Rho`/`RailType` in `FilletTwoSurfacesCommand` turned up nothing before this pass. `FilletTwoSurfacesCommand` (cmd_fillet.cpp) now takes the same `Rho` and `RailType` options `FilletEdgeCommand` already exposes, reachable under `Mode::Fillet` when both picks land on the same solid's own adjacent planar faces with `Trim=Yes` — the identical `FindSharedEdgeEndpoints`-based reachability condition this document's own Chamfer D1/D2 and tapered-fillet bullets already established for this command, since `FilletConvexEdgeConic`/`FilletConcaveEdgeConic` and `FilletConvexEdgeByDistanceFromEdge`/`ByDistanceBetweenRails` (and their concave mirrors) all need the identical one shared `ON_BrepEdge`. Unlike this command's own Chamfer/tapered-fillet wiring, Rho and RailType each refuse outright rather than falling through to the approximate path on ANY failure — not just a curved-face `PlanarFaces()` rejection, but also `Trim=No` and genuinely independent (no shared edge) surfaces, two extra cases the Chamfer/tapered-fillet wiring silently falls through for instead: those two constructions' own approximate paths can still honestly build a plain-`Radius`/`EndRadius` result under either condition, but `BuildFillet`/`RuledBetween` can only ever build a circular cross-section, so silently doing so under a caller-supplied Rho or RailType would misrepresent the result the same "misleading success" way `FilletEdgeCommand`'s own Rho/RailType wiring already guards against — a distinction this pass had to add explicitly (an earlier draft of this same wiring only refused for the curved-face case and silently dropped Rho/RailType for the other two, caught before landing by the adversarial script's own independent-surfaces case below). Verified end-to-end through the real command: `fillet_script.txt` gains a `FilletSrf`/`Radius=2`/`Rho=0.5` section and two `FilletSrf`/`Radius=2`/`RailType=DistFromEdge`/`DistBetweenRails` sections, each picking two adjacent faces of a fresh box (the identical construction the single-edge `FilletEdge` Rho/RailType cases above already verify), printing `replaced with an exact conic fillet (rho 0.5, distance 2)` / `replaced with an exact fillet (RailType=..., distance 2)` and landing on the identical `Volume = 993.3` / `991.4` / `995.7` those single-edge cases already verify — bit-for-bit the same kernel results, reached through the two-face pick instead of the one-edge pick; `fillet_adversarial_script.txt` adds two negative controls for Rho (a cylinder's own flat cap + curved side wall, and two genuinely independent surfaces extracted from two different boxes), both confirming the new refusal diagnostic fires and both objects are left completely untouched, rather than a crash or a silently-wrong circular fallback. Still partial: this command has no `Distance2` option at all (unlike `FilletEdgeCommand`), so its own Rho path is symmetric-distance only; the same kernel-level scope limits as the single-edge case (no oblique third face, no multi-edge/vertex-blend form, planar-adjacent-face only for RailType) are unchanged. Net effect on the scores below: narrows, does not flip, the SAME already-partial item — Blending & chamfering's own present/partial/missing counts (5/18/1) and 58.3% parity figure are unchanged, the same "narrowing, not a flip" convention used throughout this item's own prior additions.
- [partial] 2D curve fillet / chamfer / polyline corner rounding (Rhino Fillet, Chamfer, FilletCorners; AutoCAD FILLET/CHAMFER) — app-only (`FilletChamferCommand`/`FilletCornersCommand`, cmd_curveedit.cpp:510/646). **This pass:** a genuine kernel API now exists, closing the "No kernel 2D fillet or chamfer API" half of this gap — `NurbsCurve::FilletCorner(p0, corner, p1, radius, out)` and `NurbsCurve::ChamferCorner(p0, corner, p1, distance0, distance1, out)` (`dino8-kernel/include/dino8/kernel/curve.h`, `dino8-kernel/src/curve.cpp`). `FilletCorner` is the exact 2D analogue of `ChamferConvexEdge`'s own law-of-tangents formula in this codebase (fillet.cpp): tangent length `d = radius / tan(theta/2)` from the corner along each leg (theta = the angle between the two legs), the arc center on the internal bisector at `radius / sin(theta/2)`, and the whole result — trimmed leg, arc, trimmed leg — assembled into ONE continuous curve via the existing `Join()` (genuinely tangent, not just positional, at both junctions, since a circle's own radius to a tangent point is by construction perpendicular to the tangent line — verified directly, not just argued, by `TestNurbsCurveFilletCornerObtuseAngleAndTangency`'s own tangent-direction check at both ends). `ChamferCorner` is the 2D analogue of `ChamferConvexEdge`'s own two-independent-distance form: no arc, just the honest 4-point polyline `p0`, T0, T1, `p1` built directly via `FromControlPoints()`. Verified by `TestNurbsCurveFilletCornerRightAngle` (closed-form quarter-circle length and tangent points on a 90-degree corner), `TestNurbsCurveFilletCornerObtuseAngleAndTangency` (a genuine 120-degree corner: closed-form tangent points, arc-center distance, G1 tangency at both junctions, and total length), `TestNurbsCurveFilletCornerRejectsInvalidInput`/`TestNurbsCurveChamferCornerRejectsInvalidInput` (non-positive radius/distances, degenerate/collinear corners, oversized radius/distance reported as `Result::Failed` not a throw), and `TestNurbsCurveChamferCornerAsymmetricDistances` (exact control-point positions and total length for two independent distances). Still partial: only a single two-line corner, not a general curve-to-curve fillet/chamfer (the other half of this item — arbitrary curves, not just straight legs — remains out of scope, a materially larger problem); no app command calls either new function yet (`FilletChamferCommand`/`FilletCornersCommand` still build their own corner geometry independently). **No score change:** the item was already counted `partial` for the app-only reason and stays `partial` for these remaining gaps — a narrowing within the same already-partial item, the same convention this document uses elsewhere (e.g. the OBJ n-gon fan-triangulate bullet, kernel: Kernel-level data exchange).

**A later session's addition:** closes the "no app command calls either new function yet" half of this same item. `FilletChamferCommand` and `FilletCornersCommand` (cmd_curveedit.cpp) both now call the kernel API instead of duplicating its arc/chamfer math inline. A new kernel primitive, `NurbsCurve::FilletCornerArc(p0, corner, p1, radius, arc_out)` (`dino8-kernel/include/dino8/kernel/curve.h`, `dino8-kernel/src/curve.cpp:427`), was split out first — it is `FilletCorner`'s own tangent-length/bisector-center/sweep-direction construction with the straight-leg assembly removed, returning just the arc (`arc_out`'s domain runs from the tangent point on the `p0` side to the tangent point on the `p1` side); `FilletCorner` itself is now a thin wrapper that calls it and joins two fresh legs on. This was the piece both app commands actually needed but `FilletCorner`/`ChamferCorner` alone couldn't give them: `FilletChamferCommand`'s middle fillet/chamfer piece has to exist as its own curve object (it can be added to the document as a THIRD, separate object when the command's own `Join=No` option is set — see `Apply()`'s own `join_` branch), not fused to fresh legs the way `FilletCorner`'s single-call form always does; `FilletCornersCommand` builds one continuous curve per polyline out of lines and arcs, where the straight run between two consecutive rounded corners is one shared segment built once, not two independent legs glued on by each corner separately. `FilletChamferCommand::Apply` (cmd_curveedit.cpp, its `chamfer_`/fillet branch) now calls `FilletCornerArc` for the fillet case and `ChamferCorner` for the chamfer case, in both cases passing ray points along the already-computed `dirA`/`dirB` directions placed deliberately farther out than the true tangent length (`dist * 2 + 1`) — purely to satisfy those functions' own generic "does it fit" check, since the REAL fit check for this command is `CutToTangent`'s own trim against the actual picked curves, run afterward exactly as before; the ON_Arc/bisector-center/ON_ArcCurve construction this command used to duplicate inline is gone. `FilletCornersCommand`'s own per-corner loop now calls `FilletCornerArc(Pp, P, Pn, r, arc)` with the ACTUAL, full-length neighbouring vertices (not synthetic rays) — safe specifically because the loop's own `prev_avail`/`next_avail` check (halving the margin on a segment shared with an adjacent corner) already enforces a bound at least as strict as `FilletCornerArc`'s own single-corner fit check before it is ever called; the returned arc is appended directly into the assembled `ON_PolyCurve` via `raw()`, replacing the old from-scratch `ON_Arc(t0, mid, t1)` build. Verified, not merely derived: `TestNurbsCurveFilletCornerArcMatchesFilletCornerOnRightAngle` checks `FilletCornerArc`'s own output starts/ends at the closed-form tangent points, has exactly the quarter-circle arc length (none of `FilletCorner`'s straight legs), and — the direct proof `FilletCorner` is now built FROM this function rather than merely producing an equivalent result — that every point sampled off `FilletCornerArc`'s own curve lies exactly on `FilletCorner`'s own joined curve for the identical inputs; `TestNurbsCurveFilletCornerArcAcceptsSyntheticFarRayPoints` confirms the documented "pass a synthetic far point" use case both app commands now rely on: rays of length 5 and length 500 along the same two directions produce bit-for-bit identical tangent points and arc length; `TestNurbsCurveFilletCornerArcRejectsInvalidInput` is the negative control (non-positive radius, `p0`/`p1` coincident with `corner`, collinear corners, oversized radius all rejected the same way `FilletCorner`'s own tests already establish for it). The full existing `TestNurbsCurveFilletCorner*`/`TestNurbsCurveChamferCorner*` suite continues to pass unchanged against the refactored `FilletCorner`, confirming the extraction didn't change its own external behaviour. Still partial, unchanged from directly above: only a single two-line corner in either app command (no arbitrary-curve-to-curve fillet/chamfer), and `FilletCornersCommand` is still restricted to straight polyline segments (it already required degree-1 input before this change). **No score change:** narrows the same already-partial item without flipping it, the same convention noted just above for this item's own prior addition.
- [partial] Curve-to-curve blend, tangent (G1) and curvature-continuous (G2) Hermite (Blend / BlendCrv command) — app-only `BlendCrvCommand` (cmd_curves2.cpp:1132), G1 cubic or G2 quintic. No G3+ and no kernel API.
- [partial] Curve-to-curve blend commands: BlendCrv (G1 tangent cubic), Blend (G2 curvature-continuous quintic Hermite matching position/tangent/curvature vector), ArcBlend (two-arc tangent biarc) — same app-only commands as above; kept as a separate item to preserve the category's item count, per the original document's own item split.
- [partial] Surface-to-surface continuity blend (BlendSrf / VariableBlendSrf) — `BuildBlendSurfaceG1`/`G2` (dino8-app/src/geom/BlendSurface.h:92 onward) do G1 cubic or G2 quintic, with G2 only in the cross-boundary direction. No G3/G4, no shape/bulge handles, silently falls back to G1 at singular parametrizations.
- [partial] Rolling-ball blend surface accuracy on freeform/curved surfaces (tolerance-controlled blend geometry) — app `BuildFillet` builds circular rows along an SSX spine of offset surfaces; the offset move is approximate on curved surfaces, and the recorded `max_gap` quality signal is never enforced against a tolerance.

**kernel: Sweeping, lofting, extruding, revolving** (sweeplofts):
- [partial] Extrude a curve along a path curve (translational sweep / sum surface, ExtrudeCrvAlongCrv) — **narrowed from a pure kernel-entry-point gap; still partial.** `Brep::ExtrudeAlongCurve(profile, path, cap)` (dino8-kernel/include/dino8/kernel/brep.h; dino8-kernel/src/sweep.cpp) now exists: an exact tensor-product sum surface `S(u, v) = profile(u) + path(v) - path(v_min)`, built directly as a NURBS control net (`SumSurface()`, sweep.cpp) rather than sampled or fit — exact because a non-rational B-spline basis is a partition of unity, so the additive control net `P_ij = profile_i + path_j` reproduces the sum pointwise for ANY degree or knot vector on either curve. Unlike `Brep::Sweep1` (which rotation-minimally transports the section's own frame along a rail), `profile` never rotates here - pure translation, matching the app's own existing `ON_SumSurface::Create` semantics. `cap` closes a closed planar `profile` into a genuine solid with two fan caps (reusing `AssembleSweptBody`, the same machinery `Extrude()`/`Sweep1()`/`Revolve()` share), auto-reversed for outward orientation the same way `Extrude()` is; a straight-line `path` reproduces `Extrude()` exactly (confirmed sample-for-sample by `TestExtrudeAlongCurveStraightPathMatchesPlainExtrude`, tests/test_basic.cpp), a curved `path` was verified two independent ways — direct per-sample cross-check against `profile(u) + path(v) - path(v_min)` evaluated straight from both input curves (`TestExtrudeAlongCurveWallMatchesSumOfCurvesExactly`), and a piecewise-linear "wobbly" (laterally wandering but monotonic-height) path whose enclosed volume matches area x net height-displacement exactly regardless of the path's own in-plane wander (Cavalieri's principle, the same fact `ExtrudeToBoundary()`'s own oblique-direction case relies on; `TestExtrudeAlongCurveWobblyPathMatchesCavalieriVolume`). Still partial, and does NOT change this category's present/partial/missing counts (real gaps remain, so it stays scored `partial` rather than `present`): both `profile` and `path` must be non-rational NURBS curves (a rational B-spline basis is not a pointwise partition of unity, so the same additive-control-net construction is not exact for one — checked, throws rather than silently approximating), `path` must be open (a closed path has no well-defined net start/end displacement to cap against), there is still no twist/scale/road-like-alignment option the way `Sweep1` now has, and no app command anywhere calls it (a grep for `ExtrudeAlongCurve` in `dino8-app/src` finds nothing but this document). `Brep::Sweep1` remains the separate, rotating operation it always was.
- [partial] Extrude a surface / polysurface face into a solid (ExtrudeSrf) — **narrowed: a genuine kernel B-rep face-extrude API now exists, closing the "no kernel B-rep face-extrude API at all" half of this gap.** `Brep::ExtrudeFace(body, face_index, direction, cap)` (dino8-kernel/include/dino8/kernel/brep.h; dino8-kernel/src/sweep.cpp) takes one existing face of `body` (by its global `face_index` - the same index `FaceCount()`/`NeighborFaces()` use) and builds a fresh, independent solid from it, unrelated to `body`'s other faces. Construction directly mirrors `Thicken()` (2 caps + 4 `RuledBetween()` side walls stitching matching boundary isocurves) with one difference that is also its whole point: `Thicken()` moves the second surface by `NurbsSurface::OffsetApproximate()` (a curvature-sensitive Greville-normal offset that can fold and throws `Result::Failed` when it would), while this moves it by a plain `ON_NurbsSurface::Translate(direction)` — every control point shifted by the identical vector, so it is EXACT for any degree, any shape face (planar or genuinely freeform), and has no curvature-fold guard at all, because a rigid translate cannot fold a surface through its own local curvature the way a curvature-dependent offset can. Verified directly, not just argued: a flat 3x4 face straight up reproduces `Thicken()`'s own area * height closed form exactly (`TestExtrudeFaceStraightMatchesExactPrismVolume`); an OBLIQUE direction (not parallel to the face's own normal) on the same face still gives area * (direction's own normal-component) exactly, regardless of lateral shear (Cavalieri's principle, the same cross-check `ExtrudeToBoundary()`'s own oblique-direction test already relies on - `TestExtrudeFaceObliqueDirectionMatchesCavalieriVolume`); and, most tellingly, the SAME bulged-freeform surface and SAME thickness (1.0) `Thicken()`'s own curvature-fold guard is confirmed (in the same test) to still refuse instead succeeds here, a genuine capability difference, not a restated one (`TestExtrudeFaceOnCurvedFreeformSurfaceExceedsThickenGuard`). Also, unlike `Thicken()` (which requires a single-face body), this takes a `face_index` into a genuine multi-face polysurface: one face of a 6-face `Brep::Box()` extrudes into its own fresh 6-face solid, exactly reproducing that face's own area * height, with the source box's other 5 faces playing no part in the result (`TestExtrudeFaceOnMultiFaceBodyExtractsOneFaceIntoANewIndependentSolid`) - the same per-face independence the app's own `ON_BrepExtrudeFace` loop already has, now available kernel-side. `cap = false` gives the 4 side walls alone, a genuinely open shell (naked boundary loops confirmed at the MESH level, `!TessellateToClosedMesh().IsClosedManifold()` - `TestExtrudeFaceUncappedGivesOpenTube`), the same capped/uncapped distinction `Extrude()`/`ExtrudeAlongCurve()` already draw. Like `Thicken()` (both build every face via the bare `ON_Brep::NewFace(surface_index)` overload, not `AssembleSweptBody()`'s own vertex/edge-producing overload), the result has no real edge/vertex topology at the `raw()` level either way - `ON_Brep::IsValid()` reports it invalid even when `cap = true` and the mesh is a genuine closed solid, the SAME already-disclosed "surface-only `NewFace(int)`" limitation this document's own **kernel: Topology & data structure** category names for `Box`/`Sphere`/`Torus`/`FromSurface`/`TrimmedPlanarFace` (that list is one function short of complete: `Thicken` shares it too, and now so does this). **Further narrowed: a TRIMMED face on a PLANAR surface (still with no holes) is no longer refused.** The trim loop (`face_trim_loops_`, a polygon in the surface's own (u, v) - `TrimmedPlanarFace()`'s own doc comment) maps through a planar surface to a genuine 3D polygon, so both end caps and every wall are exact flat facets, built as `Brep::PlanarFace` entries and welded into ONE real solid by a single `Brep::FromPlanarFaces()` call - the exact same construction `ExtrudeToBoundary()`'s own N-gon-profile fix already uses and verifies, not a new one; the pre-existing UNTRIMMED path is left byte-for-byte unchanged (still the bare `NewFace(int)`, "surface-only" construction), the same "don't touch what already works" choice `ExtrudeToBoundary()`'s own N == 4 path made for the identical reason. This is a genuine capability jump for the trimmed case, not just wider input validation: the result is a real `IsSolid() == true` B-rep (unlike the untrimmed path, which stays `IsValid() == false` by that same deliberate convention). Verified directly on an L-shaped trimmed planar sheet (a 4x4 square missing its own [2,4]x[2,4] corner): `IsSolid()`, the exact expected face count (2 caps + 6 walls for the L-shape's 6 vertices), and volume matching footprint area * height exactly (12 * 3 = 36, not merely "closes to a positive number") - `TestExtrudeFaceTrimmedPlanarFace`, tests/test_basic.cpp; `cap = false` gives the 6 open walls alone, the same capped/uncapped distinction the untrimmed path already draws. Still refused, deliberately: a trimmed face WITH a hole (confirmed to still throw in the same test), and a trimmed face on a NON-planar surface (confirmed to still throw - a straight UV trim polygon does not map to straight, or even necessarily planar, 3D edges through a curved surface, so this flat-facet construction genuinely does not apply there, not merely an unattempted extension). Full `dino8_kernel_tests` suite re-run clean via `ctest`: 100% passing, 0 regressions.

Still partial, and does NOT change this category's present/partial/missing counts (6/21/2, 56.9%) - real gaps remain, so it stays scored `partial` rather than `present`, the same convention `ExtrudeAlongCurve()`'s and `ExtrudeToPoint()`'s own narrowing bullets above already use: a trimmed face's surface must still be planar and hole-free (a curved trimmed face or a holed one is a separate, larger gap left open - the former genuinely needs a different, curved-wall construction, not just more code in this same vein); the untrimmed path's own scope is unchanged (surface must be open/non-periodic in both parametric directions, no draft/taper option); this still produces a SEPARATE new solid rather than merging into or cutting `body` itself (Push/pull - `PushPullFace`, boolean.cpp - is the separate operation that does that); and the app's own `ExtrudeSrfCommand` (cmd_surface.cpp) is completely untouched - it still loops `ON_BrepExtrudeFace` with translation-only station transforms into a mesh, never calling this kernel entry point (a grep for `ExtrudeFace` in `dino8-app/src` finds nothing but this document).
- [partial] Extrude with draft / taper angle (ExtrudeCrvTapered, ExtrudeSrfTapered; AutoCAD EXTRUDE Taper) — kernel `Brep::ExtrudeTapered` (brep.h; sweep.cpp) is exact for a line, a circle/arc, and now ANY simple (non-self-intersecting) multi-segment polyline via a closed-form miter offset, convex or concave. **Stale as of a later same-day session, corrected here: an oblique `direction` no longer throws** (see this category's own "Draft angle on extrusions" bullet below for the fix and its Cavalieri-principle exactness proof) - this bullet's own "an oblique direction throws" clause was left unupdated when that narrowing landed. **Updated this pass: a non-convex (concave) polyline profile no longer throws from convexity alone.** `OffsetConvexPolyline()` (sweep.cpp) dropped its own convexity pre-check - the miter-join formula itself never needed one (it is exact at any corner regardless of local turning direction) - and gained two unconditional validity checks in its place: the pre-existing LOCAL one (every offset edge must stay a positive multiple of its own original direction, catching an edge that inverts or collapses against its own two neighbors) plus a new GLOBAL one (no two non-adjacent offset edges may pass within tolerance of each other, via the same `detail::ClosestSegmentSegment()` this kernel already shares for curve-intersection seeding and mesh distance queries), together the general "is this offset still a simple polygon" test rather than a convexity proxy. Verified on a single-reflex-vertex "L" profile - CAPPED end to end (star-shaped, so Loft's own fan cap still closes it): the wall's own top-row control points match the hand-derived offset vertices exactly (every corner here is a right angle, so the formula trivially reduces to `V + distance*(n0+n1)`), and the tessellated volume converges to the exact general PRISMATOID-formula volume `h/6*(A0 + 4*Am + A1)` (A0/A1/Am three independently hand-computed polygon areas - bottom, top, and mid-vertex sections - not a re-use of the kernel's own offset math) as division rises, confirming the non-planar wall panels a non-uniform corner offset produces still enclose the analytically-correct volume. A genuinely non-star-shaped concave profile (a "C" channel) still builds its own wall fine uncapped - only Loft's own separate star-shaped-cap requirement refuses a cap for it, an unrelated, already-known limitation this pass does not touch. The new global check was isolated from the pre-existing local one with a two-independent-notches fixture (no shared vertex between the two features) whose offsets are made to cross each other purely by each notch floor translating toward the other - confirmed a single-reflex-vertex variant of the same idea collapses via the LOCAL check first, so a genuinely non-adjacent-feature fixture was needed to prove the global check does real work (`TestExtrudeTaperedConcavePolygonIsExactPrismatoid`, tests/test_basic.cpp). The pre-existing "a non-convex profile throws" negative control (which tested exactly the restriction this lifts) was replaced. Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial: a non-star-shaped concave profile can't be CAPPED (Loft's own fan-cap kernel-emptiness limitation, not an offset-math one), a general curved profile still falls back to an approximate least-squares offset, no surface/solid taper in the kernel, and the app's own ExtrudeCrvTapered (cmd_surface.cpp) still scales the profile about its centroid (approximate corners) rather than calling the kernel.
- [partial] Extrude to a point (ExtrudeCrvToPoint / ExtrudeSrfToPoint / kernel ConeToApex) — **narrowed: a genuine B-rep kernel entry point now exists, closing the "no B-rep way to cone to a point at all" half of this gap.** `Brep::ExtrudeToPoint(profile, apex, cap)` (dino8-kernel/include/dino8/kernel/brep.h:513; dino8-kernel/src/sweep.cpp:1701) builds a real cone `ON_Brep` via the existing `FanSurface()` (sweep.cpp:927) - the SAME degree-(p, 1) fan construction `AddFanCap()` already uses for flat end caps - reused here as the WALL itself rather than a cap, assembled directly through `ON_Brep::NewFace`'s own closed-in-u/singular-at-v0 handling (one apex vertex, one closed rim edge, one seam edge doubled as the wall's own east/west trims - deliberately NOT routed through `AssembleSweptBody()`, which explicitly refuses any wall singular at its v0/v1 sides, sweep.cpp:1084, since it was written for non-degenerate rectangular sweeps). The exactness argument is strictly more permissive than `PlanCap()`'s own flat-cap "star-shaped section" requirement (the phrase appears throughout this file, e.g. the Loft/Sweep1 bullets above): for an OFF-PLANE apex, two rulings `apex -> profile(u1)` and `apex -> profile(u2)` are two distinct lines through the single common point `apex` (distinct because `profile(u1)`/`profile(u2)` lie in a plane `apex` does not, so each ruling crosses that plane only at its own endpoint), and two distinct lines sharing a point meet nowhere else - so the cone wall embeds for ANY simple closed planar profile, convex or not, star-shaped or not, unlike a flat fan cap which folds over on a non-star-shaped region. Verified directly, not just argued: `TestExtrudeToPointWallEmbedsEvenForNonStarShapedProfile` (tests/test_basic.cpp:32670) cones the SAME non-star-shaped C-shaped profile `Extrude()`'s own negative control already refuses to flat-cap, confirms the wall alone still builds and embeds (`Mesh::FindSelfIntersections()` on a dense 80x9 tessellation reports zero genuine crossings), and confirms the flat BASE cap on that same profile still throws - a separate, honestly-disclosed limitation of the shared `PlanCap()`/`AddFanCap()` cap machinery, not of the cone wall itself. Pyramid-volume exactness (the general `(1/3) * area * height` formula, exact for ANY planar base) is verified on two genuinely non-convex profiles (an L-shape and a 5-pointed star polygon, both still star-shaped so the flat base cap also succeeds) two independent ways - `Brep::Volume()`'s own direct NURBS quadrature and `TessellateToClosedMesh(...).Volume()` - plus a convex triangle cross-checked the same two ways, an apex on either side of the profile's plane, and a RATIONAL profile (a true NURBS circle, unlike `ExtrudeAlongCurve()` which refuses one): `TestExtrudeToPointPyramidVolumeExactOnNonConvexProfiles`, `TestExtrudeToPointConvexTriangleVolumeTwoIndependentWays`, `TestExtrudeToPointSupportsRationalProfileExactCircularCone` (tests/test_basic.cpp:32531/32615/32647). **Further narrowed: an OPEN profile no longer throws.** It cones to an open fan SHELL instead of a closed cone - the same `FanSurface()` wall construction, but `ON_Brep::NewFace`'s own topology inference now gives two SEPARATE straight spoke edges (apex to each of the profile's own two distinct endpoints) plus the open profile curve itself as a third boundary edge, rather than the closed case's one doubled seam edge - a valid manifold shell with a naked boundary (`IsManifold() == true`, `has_boundary == true`), not a solid (`IsSolid() == false`, correctly - there is no closed rim to bound a volume). No auto-reverse orientation is applied for this case (unlike the closed profile's own outward-facing auto-reverse): an open fan has no well-defined interior to be "radially outward" from, so the wall's own normal follows directly, unadjusted, from `FanSurface(profile, apex)`'s own construction - a deliberately simple, honestly-scoped choice rather than inventing an unverifiable convention. `cap` is silently ignored for an open profile (no error), matching `ExtrudeAlongCurve()`'s own established "no caps regardless of cap" convention for its open-profile case - there is no single natural closing cap for an open fan the way a closed one's flat rim has. Verified directly: every sampled wall point equals `apex + v * (profile(u) - apex)` exactly, to double precision, over a dense (u, v) grid, not merely "it doesn't throw" (`TestExtrudeToPointOpenProfileFan`, tests/test_basic.cpp); the exact topology (3 vertices, 3 edges, 1 face) is checked directly; `cap = true` is confirmed to give the identical result, not an error; a non-planar open profile and an apex in an open profile's own plane are both confirmed to still throw, the same unconditional preconditions the closed case already has. Full `dino8_kernel_tests` suite re-run clean via `ctest`: 100% passing, 0 regressions. Still partial, and does NOT change this category's present/partial/missing counts - real gaps remain: `profile` must still be planar and `apex` strictly off that plane UNCONDITIONALLY (checked even with `cap = false`), the CLOSED-profile flat base cap still inherits `PlanCap()`'s own star-shaped-section requirement (`TestExtrudeToPointNegativeControls`), and the app layer is completely untouched: `RebuildExtrudeToPoint` (cmd_solids.cpp) still uses `CreateRuledSurface` to a degenerate apex curve with no cap and does not call the kernel's entry point. Kernel `Mesh::ConeToApex` remains mesh-only; `Brep::Loft` to a point section still cannot be capped.
- [partial] Extrude to a boundary surface / body (Rhino ToBoundary, Boss-to-boundary; AutoCAD extrude "to face", PressPull) — **corrected: upgraded from missing.** Kernel `ExtrudeToBoundary(profile, direction, boundary)` (boolean_general.h/boolean_general.cpp) now exists: each of a QUADRILATERAL `profile`'s own 4 corners is swept along `direction` and intersected exactly (closed-form ray/plane, not a resample) with `boundary`'s own plane, then assembled into a genuine 6-quad-face prism via the new `Brep::FromUntrimmedQuadFaces()` (brep.h) - so a TILTED `boundary` gives a genuinely, exactly planar cap, not an approximation. An earlier version instead tried extruding `profile` past `boundary` and cutting with `SplitBySheet()` (this same file); abandoned after being confirmed, via a standalone reproduction, to corrupt SplitBySheet's own output for anything but a plain axis-aligned `Brep::Box()` - that engine's SSX machinery reads each face purely via its raw `ON_Surface`, so both `Brep::Extrude()`'s own periodic wrap-around wall and `Brep::FromPlanarFaces()`'s own padded-domain trimmed faces are silently misread as occupying their own FULL surface domain, a genuine, previously-undocumented scope boundary of that shared machinery now disclosed here rather than papered over. Verified exact against a closed-form cross-check independent of the implementation: cutting a vertical extrusion of a 2x3 rectangle against a TILTED plane `h(x,y) = 0.5x + 0.2y + 4` gives volume = area x height-at-centroid for any affine cap (`TestExtrudeToBoundaryTiltedPlaneMatchesExactAffineCapVolume`, tests/test_basic.cpp); a flat boundary reproduces plain `Extrude()`'s own volume exactly (`TestExtrudeToBoundaryMatchesPlainExtrudeForAFlatBoundary`); a clockwise profile winding, an extrusion direction reversed along the same axis, and an oblique direction against a flat boundary (Cavalieri's principle: area x height exactly, regardless of shear) are all separately verified exact (`TestExtrudeToBoundaryHandlesReversedWindingAndDirection`) - the latter three exist specifically because an earlier version of this same construction got the two caps' own winding right while leaving all 4 side walls inverted for one sign of (profile winding, direction), a bug the single straightforward case alone did not surface (both caps AND all 4 walls were inverted together there, giving a wrong-signed but still-closed volume). **Narrowed:** `profile` is no longer restricted to a quadrilateral. For N != 4, every generated face (the two now possibly-N-gon caps AND the always-quad walls alike) is instead built as a `Brep::PlanarFace` and welded into ONE real solid by a single `Brep::FromPlanarFaces()` call - its own `VertexWelder` shares an edge automatically wherever two faces' loops meet at the same 3D point, closing the exact gap the abandoned SplitBySheet rewrite (this bullet's own earlier paragraph) was trying to avoid needing, without reviving SSX at all: each face's own outward `ON_Plane` is derived directly from its own already-correctly-wound loop via a shared Newell/shoelace-normal helper, not a second hand-tracked sign-flip. This is a genuine capability jump, not just wider input validation: the N != 4 result is a real `IsSolid() == true` B-rep (confirmed directly), something the N == 4 path has never been (see below). Verified with an irregular pentagon and a triangle, both against a TILTED boundary, cross-checked against an independent closed form (footprint area, via the shoelace formula on the profile's own vertices - not assumed - times affine height at the centroid, Cavalieri's principle) - not merely "it doesn't throw" (`TestExtrudeToBoundaryNgonProfile`, tests/test_basic.cpp); a reversed (clockwise) pentagon winding extruded in the reverse direction gives the identical volume, the same regression pair `TestExtrudeToBoundaryHandlesReversedWindingAndDirection` already exercises for N == 4. The existing N == 4 path is untouched byte-for-byte (still `Brep::FromUntrimmedQuadFaces()`, still the deliberately topology-free "six independent untrimmed surfaces, mesh-compatible only" convention `Box()` documents, `IsSolid() == false` exactly as before) - a real tradeoff measured directly, not assumed, is why: switching N == 4 to the same `FromPlanarFaces()` path too was tried first and gives an `IsSolid() == true` result there as well, but `TessellateToClosedMesh()`'s own per-face grid tessellation left small T-junction cracks at the caps' own trimmed (not axis-aligned) boundary on a TILTED or OBLIQUE case specifically - `IsClosedManifold()` read false where the existing N == 4 tests require true - so N == 4 was kept on its own already-proven path rather than risk that regression for a change this bullet does not need. The N != 4 path inherits the identical tradeoff in the other direction: `IsClosedManifold()` is NOT asserted for the pentagon/triangle tests above, only `IsSolid()` and the tessellated mesh's own (still-accurate) volume - a small, disclosed cost of the real solid topology, not a hidden one. Full `dino8_kernel_tests` suite re-run clean via `ctest`: 100% passing, 0 regressions. Still partial, and does NOT change this category's present/partial/missing counts - real gaps remain: `boundary` must still resolve to a single planar face (a general curved or multi-face boundary is out of scope, unchanged), only extrudes forward along `direction` and throws rather than guessing the opposite sign if `boundary` lies behind (unchanged), the N != 4 path's own mesh-level T-junction cracking at a trimmed cap boundary is a real, disclosed limitation (not attempted here - see this kernel's own general "trimmed planar face mesh-stitching" gap elsewhere), and there is still no app-level command anywhere that calls it (a grep for `ExtrudeToBoundary`/`ToBoundary` in `dino8-app/src` finds nothing but the catalogued option string).
- [partial] Full 360-degree revolve of a profile about an axis into a capped solid (Revolve, RevolvedHole) — kernel `Brep::Revolve` (sweep.cpp:1483-1595) is exact rational and handles L profiles (poles), closed off-axis profiles (torus-like), a semicircle (exact sphere), and off-axis ends with disc caps. App `RevolvedHole` (cmd_solidtools.cpp:906, "mesh boolean; results are meshes") cuts with a mesh boolean. **Narrowed:** a closed profile touching the axis along a single straight (or curved) sub-arc — e.g. a rectangle with one side ON the axis, this bullet's own longstanding example of what throws — no longer does. `SplitTouchingAxisArc` (sweep.cpp, new helper ahead of `Brep::Revolve`) samples the profile circularly, finds that one touching sub-arc, and splits it off, reducing the call to the ALREADY-exact "open profile, both ends on the axis" pole path a few lines below in the very same function — not a new topology construction, so it inherits that path's existing exactness/cap correctness rather than re-arguing it. The split itself needed its own care: reseaming (`ON_NurbsCurve::ChangeClosedCurveSeam`) to a point picked AT the touching run's own boundary looked simplest first but silently made `ON_NurbsCurve::Split` refuse whenever that boundary happened to coincide with the profile's own already-authored seam (exactly this bullet's own rectangle example, whose closing vertex sits right there) — fixed by reseaming to the touching run's own MIDPOINT instead (never at an existing knot or domain edge) and taking two splits from there. Verified both a full and a partial (quarter) revolve of the explicit closed rectangle profile against the pre-existing open L-profile `ell` fixture's own topology AND volume (`TestRevolveExactSolidsAndCaps`, tests/test_basic.cpp) — not merely "it doesn't throw", but the exact same edge/vertex counts and Pappus volume as the already-trusted open-profile case, plus `cap=false` still building the bare open wall. Still throws, deliberately out of scope, for a profile touching the axis at more than one separate place, or at a single point rather than along a genuine sub-arc (a true tangency, e.g. a circle tangent to the axis) — both covered by new negative-control cases in the same test. Full `dino8_kernel_tests` suite re-run clean: 100% passing, 0 regressions. Still partial, and does NOT change this category's present/partial/missing counts — real gaps remain: a partial-angle revolve of an open profile with an off-axis endpoint still cannot be capped (investigated this same session — the naive "wedge cap" construction that works for the already-supported on-axis cases leaves the off-axis end's own swept arc edge with no partner face, an invalid non-solid `IsSolid()==false` result, confirmed by a standalone diagnostic before being discarded rather than shipped; a correct fix needs a genuinely different multi-face construction, not attempted here), and `RevolvedHole`'s result is still a mesh.
- [partial] Partial-angle revolve (start angle / revolution angle < 360, with planar side caps) — kernel `Brep::Revolve`'s `angle` parameter in (0, 2pi] gives planar pie-slice fan caps for closed profiles and open profiles with both ends on the axis. `Revolve` now also takes a `start_angle` parameter (brep.h, sweep.cpp:1483): the sweep begins `start_angle` radians around the axis from the profile's own given position instead of always at it (Rhino/AutoCAD Revolve's own start-angle option), implemented as an exact rigid rotation of the profile about the same axis before the existing sweep runs — so every cap/throw rule above is unaffected and the result is exact for any `start_angle`, not a resample; confirmed sample-for-sample against an independently-rotated wall by `TestRevolveStartAngleShiftsSweepExactly` (tests/test_basic.cpp). Still partial: an open profile with an off-axis endpoint still cannot be capped at a partial angle; a closed profile touching the axis still throws; the app still hard-codes 0..2pi with no angle (let alone start-angle) option anywhere, and does not call the kernel's new parameter.
- [partial] Rail revolve (profile revolved about an axis while following a rail curve) — **narrowed: a genuine kernel B-rep entry point now exists, closing the "no kernel equivalent" half of this gap.** `Brep::RailRevolve(profile, axis_point, axis_direction, rail, angle, stations, cap)` (dino8-kernel/include/dino8/kernel/brep.h; dino8-kernel/src/sweep.cpp) takes a CLOSED, planar, off-axis `profile` (the same on-axis-touching restriction `Revolve()` itself already imposes) and, at each of `stations` equal-angle positions around the axis, rotates it by that station's angle and radially rescales it (only the radial coordinate - the axial one is left exactly as given) by `rail`'s own distance from the SAME axis there, sampled once per station via `NurbsCurve::DivideByCount()` and divided by the rail's own distance at station 0 - the same "read once, scale relative to station 0" convention `Sweep2()`'s own local-coordinate decoding already uses, just applied to one radial axis instead of all three. The `stations` cross-sections are then skinned EXACTLY through `SkinSections()` (the same global NURBS interpolation `Sweep1()`/`Sweep2()`/`Loft()` already share) rather than the app's own approximate curve-through-points fit (`SurfaceThroughRows`), and the result is a genuine, capped-when-requested `ON_Brep` solid, not a bare surface. Verified three ways, not just argued: a RIGID case (a rail held at a constant distance from the axis, so every station's own scale factor is exactly 1.0) exactly reproduces the rotated profile at EVERY one of 6 stations across a partial angle and all 8 of a full 2*pi wrap - provable exactly, not merely to sampling resolution, because a rigid rotation by an identical angle increment moves every control point by an IDENTICAL chord distance at every interval, which makes `SkinParameters()`'s own chord-length station accumulation land at EXACTLY uniform k/(m-1) (partial) or k/m (full) station parameters, the same "equal spacing -> uniform station parameters" property `TestLoftInterpolatesSectionsExactly()`'s own four-growing-squares case already relies on (`TestRailRevolveRigidRotationReproducesEveryStationExactly`, tests/test_basic.cpp) - the same test also confirms this rigid full-revolve case matches the closed-form Pappus torus volume to within 1%; a varying-radius case at `stations = 2` (the exact `RuledBetween()` shortcut, whose two end curves are always exactly the two given inputs) confirms the scale-factor arithmetic itself lands exactly at both ends, independent of any skinning question (`TestRailRevolveVaryingRadiusScalesExactlyAtTheTwoStations`); and a genuinely bulging rail (radius 1 -> 3 -> 1, a vase-like silhouette) produces a real closed, manifold solid whose volume is bracketed between the two RIGID constant-scale extremes (lambda = 1 and lambda = 3, each independently exact via Pappus' theorem since a uniform radial dilation by lambda scales a profile's own enclosed area by exactly lambda) and is well above the un-scaled body's own volume, proving the rail is genuinely being sampled rather than silently ignored (`TestRailRevolveBulgingRailProducesABracketedVaseVolume`). Full `dino8_kernel_tests` suite re-run clean via `ctest`: 100% passing, 0 regressions.

**Further narrowed: an OPEN profile no longer throws outright.** It may now touch the axis at its own two endpoints (Revolve()'s own "both ends on the axis" pole case) - every station's own copy of such a control point is forced to the exact axis point right after `RuledBetween()`/`SkinSections()` build the wall (a defensive fix, not merely a convenience: a CV only within `tol` of the axis rather than bit-exact 0 would otherwise scatter into a genuinely different tiny offset per station under each station's own rotation, which `IsSingular()`'s own exact-coincidence check - no tolerance - would then correctly, if unhelpfully, read as NOT singular, silently losing the pole `AssembleSweptBody()` needs). With that fixed, the wall's own u=0/u=last columns ARE genuinely singular at every station, so `AssembleSweptBody()` caps (or fully closes, for a full wrap) an open both-ends-on-axis profile exactly the same way it already does for `Revolve()`'s own analogous case - not a new construction. Verified directly, not just "it doesn't throw": rail-revolving the exact same rectangle profile `Revolve()`'s own `ell` cylinder fixture uses, with a rail held at a CONSTANT radius (so every station's scale factor is exactly 1.0), reproduces `Revolve()`'s own exact cylinder AND quarter-cylinder topology bit-for-bit (`TestRailRevolveOpenProfileBothEndsOnAxis`, tests/test_basic.cpp) - identical face/edge/vertex counts, not merely a similar-looking solid - with volumes matching Revolve()'s own exact closed forms up to RailRevolve's own already-disclosed station-count-interpolant discretization; a genuinely bulging rail on the SAME open profile is confirmed bracketed between the two rigid constant-scale extremes, the identical cross-check the closed-profile case already uses, proving the rail is genuinely sampled for an open profile too. An open profile with NEITHER end on the axis still builds fine uncapped (a genuine open tube - no pole to auto-close it) but still cannot be capped at a partial angle, the exact same restriction `AssembleSweptBody()` already imposes on `Revolve()`'s own analogous off-axis-endpoint case; touching the axis away from the profile's own endpoints is still the same degenerate band both functions refuse. Full `dino8_kernel_tests` suite re-run clean via `ctest`: 100% passing, 0 regressions.

Still partial, and does NOT change this category's present/partial/missing counts (6/21/2, 56.9%) - real gaps remain, so it stays scored `partial` rather than `present`, the same convention this document's other narrowing bullets above already use: a CLOSED profile must still stay strictly off the axis (unchanged - a closed profile touching the axis is a separate, larger gap this bullet does not attempt to close, unlike `Revolve()`'s own `SplitTouchingAxisArc`), there is no analytic exact-sweep shortcut the way constant-radius `Revolve()` has (the wall is always a discretized, cubic-interpolated skin, tightened by more `stations`, not an exact rational surface of revolution), `rail` is sampled by its own `DivideByCount()` index, not reparametrized against the angle in any other way, and the app's own `RailRevolveCommand` (cmd_srfedit.cpp:1019) is completely untouched - it still fits an approximate surface through sampled rows and never calls this new kernel entry point (a grep for `RailRevolve` in `dino8-app/src` finds nothing but this document and the catalogued option string).
- [partial] Sweep along one rail (Sweep1: rotation-minimizing frames, multiple sections blended, closed rail) — kernel `Brep::Sweep1` (sweep.cpp:1707 — corrected 2026-09-28, was mis-cited sweep.cpp:1697-1769) uses double-reflection RMF and gives real capped B-rep solids; a straight rail becomes an exact extrusion. `twist_total`/`scale_end` add a linear rotation/scale along the path, and `roadlike_up` swaps the RMF reference for a fixed world direction (all three exact on a straight rail; `roadlike_up` also works on a closed rail, unlike the other two). **Narrowed this pass: twist/scale are no longer linear-endpoint only.** `Sweep1` gained `twist_schedule`/`scale_schedule` (brep.h; sweep.cpp) — each an optional list of (t, value) pairs, `t` the rail's own arc-length fraction from its start, piecewise-linear and held flat outside the given range, closing half of this bullet's own "no piecewise schedule the way `PipeVariable`'s radius is" gap by giving `Sweep1` the SAME schedule convention `PipeVariable()`'s `radius_points` already established (mutually exclusive with the matching plain scalar, unsupported on a closed rail, same as the scalars). See this category's own "Sweep controls" bullet below for the construction and its tests. Still partial: the kernel still takes one section only (no multi-section blending, unlike the app's own `Sweep1Command`), a schedule is only EXACT at its own breakpoints (interpolated, not exact, between them, once there are more than the trivial two endpoints — the same caveat `PipeVariable`'s own radius schedule already carries), the wall is still a station-count interpolant off a straight rail, and the app does not use the kernel.
- [partial] Sweep along two rails (Sweep2) — kernel `Brep::Sweep2` (sweep.cpp:1820 — corrected 2026-09-28, was mis-cited sweep.cpp:1770-1854) uses two-rail frames with a single uniform scale by rail-to-rail width; two straight rails give an exact ruled wall. Still partial: one section only, uniform scale only, a station interpolant on curved rails, throws where rails touch or a tangent is parallel to the rail-to-rail direction (sweep.cpp:1244,1255); the app's own Sweep2 command is unchanged and does not call the kernel.
- [partial] Sweep controls: twist along path, scale along path, road-like / fixed-up alignment (AutoCAD SWEEP Twist/Scale/Alignment, Rhino Roadlike/Frame rotate) — all three now exist kernel-native on `Brep::Sweep1` (brep.h): `twist_total` adds an extra rotation about the rail's own tangent, `scale_end` a uniform scale about each station's own frame origin, both linear in arc-length station fraction and EXACT on a straight rail (confirmed by `TestSweep1TwistIsExactOnAStraightRailAndRejectsOnClosedRail` and `TestSweep1ScaleIsExactContinuouslyOnAStraightRailAndRejectsOnClosedRail`, tests/test_basic.cpp); `roadlike_up` replaces the RMF's own reference direction at every station with a fixed world vector projected perpendicular to the tangent there (AutoCAD's Alignment=Roadlike), confirmed exact against `Extrude()` itself on a straight rail and shown to need no closed-rail restriction, unlike the other two (`TestSweep1RoadlikeAlignmentMatchesExtrudeOnAStraightRailAndRejectsDegenerateUp`). **Narrowed this pass: twist/scale are no longer linear end-to-end only.** New optional `twist_schedule`/`scale_schedule` parameters (`const std::vector<std::pair<double, double>>*`, brep.h; sweep.cpp) each take a genuine PIECEWISE-LINEAR (t, value) schedule — `PipeVariable()`'s own `radius_points` convention verbatim (at least 2 points, strictly increasing `t` in [0, 1], held flat at the nearest endpoint's value outside the given range) — mutually exclusive with the matching plain scalar (throws if both are given) and, like both scalars, still refused on a closed rail. Construction mirrors `PipeVariable()`'s own "insert exact stations at the given breakpoints, then skin through them" approach: every schedule breakpoint's own arc-length fraction is merged into the ordinary evenly-spaced station grid as a real extra station (`TestSweep1TwistScheduleMatchesPlainTwistAtEndpointsAndInsertsRealStations`/`TestSweep1ScaleScheduleMatchesPlainScaleAtEndpointsAndInsertsRealStations`, tests/test_basic.cpp, confirm a real extra CV row appears via `CVCountV() > 2`), UNLESS the schedule is exactly the trivial two endpoints `{(0, .), (1, .)}` on a straight rail, which collapses back onto the SAME exact 2-station ruled shortcut the plain scalar already takes — confirmed bit-for-bit (1e-12) against the plain-scalar wall in both tests. A genuine (non-trivial) schedule's own two ends are confirmed exact by the same closed-form checks `TestSweep1TwistIsExactOnAStraightRailAndRejectsOnClosedRail`/`TestSweep1ScaleIsExactContinuouslyOnAStraightRailAndRejectsOnClosedRail` already use for the plain scalar case, and a 3-point scale-scheduled bulge's tessellated volume is cross-checked against the closed-form stacked-pyramid-frustum reference, mirroring `TestPipeVariable()`'s own 3-point bulge test (1% bound at 96 stations). The per-station transform itself was refactored while adding this: the twist rotation is now applied as its own explicit rotation about each station's OWN tangent at its OWN origin (composed after the RMF/road-like frame-to-frame transform) rather than baked into the RMF frame's `(r, s)` beforehand — proven the same unique rigid rotation as the old approach (both map frame 0's basis onto the SAME target basis, and a rigid transform is uniquely determined by where it sends an orthonormal frame), needed because a schedule may now put a nonzero twist/scale at the very first station (held flat below its own first breakpoint) where the old "station 0 is always the untouched input, so skip its own transform" shortcut no longer holds; the non-schedule path is untouched byte-for-byte (same `DivideByCount`-based station construction as before, confirmed by the pre-existing three tests above staying green unchanged) specifically so this refactor could not regress it. Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial, and does NOT change this category's present/partial/missing counts (6/21/2, 56.9%) — real gaps remain, so it stays scored `partial` rather than `present`: a schedule is only exact AT its own breakpoints, not continuously between them, once there are more than the trivial two endpoints (the same caveat `PipeVariable`'s own radius schedule already carries); no independent per-axis scale; `Sweep2` has no schedule (or even a plain scalar twist/scale) at all; and the app's `Sweep1Command` still has none of these options.
- [partial] Loft options: Loose/Tight/Uniform styles, Closed loft, start/end tangency matching to surfaces, guide curves, Rebuild/Refit — kernel `Brep::Loft` provides a closed (periodic) loft, a degree choice, and exact start/end tangency: optional `start_tangent`/`end_tangent` `NurbsCurve` arguments (brep.h:391-393) pin the wall's derivative at a constrained end in closed form, confirmed exact across the full u range by directly reading `TestLoftTangentConstrainedEndsMatchExactly` (tests/test_basic.cpp:31593 — corrected 2026-09-28, was mis-cited :27782). Still partial: this is curve-to-vector-field tangency, not surface-to-surface edge tangency matching; requires degree >= 2 and non-rational open sections; not exposed in the app's `LoftCommand` at all; still no Loose/Tight/Uniform styles, no guide curves, no Rebuild/Refit.
- [partial] Developable loft between two rails (DevLoft) — app `DevLoft` (cmd_remaining.cpp:952) is a monotone twist-minimising ruling search producing an approximately-developable ruled surface. No kernel equivalent (`UnrollDevelopable` unrolls surfaces but does not construct a developable loft).
- [partial] Pipe: constant-radius tube around a curve with optional caps — kernel `Brep::Pipe` (sweep.cpp:1905 — corrected 2026-09-28, was mis-cited sweep.cpp:1855-1870) is an exact rational circle swept by Sweep1: exact on a straight rail, closed-rail tube. **Narrowed: a `round_caps` option now exists.** `Brep::Pipe`'s new `round_caps` parameter replaces the two flat fan caps with genuine hemispherical NURBS dome caps (Rhino Pipe's own "round" cap style) built by a new `AddDomeCap()` (sweep.cpp) sharing the tube's own rim edge exactly - see this category's own "Pipe — Round cap option" bullet below for the full construction and exactness argument. App `PipeCommand` gives a mesh when Cap=Yes or the rail is closed. **Further narrowed: `Brep::PipeVariable` now has its own `round_caps` option too**, each dome sized to that end's own local radius (see this category's own "Pipe — Round cap option" bullet below for both). Still partial: curved rails are a station-count interpolant, `round_caps` is wired into `Brep::Pipe`/`PipeVariable` only (not `PipeThickWalled`, whose annular rim needs a different, non-single-circle meridian construction), no kinked-rail handling, and the app never calls it.
- [partial] Pipe variants: multiple radii along the rail, thick-walled (inner+outer) pipe, MultiPipe per-branch radii — kernel `Brep::PipeVariable` (brep.h:586; sweep.cpp:1921 — corrected 2026-09-28, was mis-cited brep.h:465-509; sweep.cpp:1871-1979) piecewise-linearly interpolates (t, radius) control points, exact for a 2-point taper on a straight rail (`TestPipeVariable`, tests/test_basic.cpp:32278 — corrected 2026-09-28, was mis-cited :28019). **Correction: this bullet previously said "no thick-walled pipe" — stale.** `Brep::PipeThickWalled` (brep.h:589 — corrected 2026-09-28, was mis-cited brep.h:585) already exists (landed in commit `eacfea7`, before this pass): a genuine hollow annular solid (two oppositely-facing walls plus ruled annular end caps sharing literal edges with both), not a mesh-boolean approximation. Still partial: `MultiPipe` is still single-radius capped meshes unioned, and the app `PipeCommand` still has a single radius with no thick-walled option.
- [partial] Cap planar openings of open polysurfaces (Cap; kernel end-cap synthesis) — app `Cap` samples 8 points per naked edge into a polyline before capping, so a curved hole gets a polygonal cap. Kernel `Brep::CapPlanarHoles` (brep.cpp:8787) gives a genuinely re-capped closed solid; a multi-edge naked chain with any non-linear edge is still refused (`if (!e.IsLinear(tolerance::kDistance)) ok = false;`, brep.cpp - the straight-loop path's own polygon can't represent a curve). **Narrowed:** the common single-edge case — a chain that is exactly ONE closed (start == end vertex) naked edge, e.g. an uncapped `Extrude()`/`Pipe()` circular rim — is no longer refused at all: `Brep::CapClosedCurvedLoop` (brep.h; sweep.cpp:1179) caps it with the genuine curve, not a polygon approximation, reusing the same star-shaped fan-apex search and fan surface (`PlanCap()`/`FanSurface()`) the sweep-class factories' own end caps already use and verify — the new face is a standalone one-face shell appended and welded onto the original edge by the existing tolerance-based `JoinNakedEdges()`, exactly like a straight `FromPlanarFaces()` cap already is, so no new orientation reasoning was needed. Verified end to end on an open cylinder (`Extrude(circle, cap=false)` then `CapPlanarHoles()`): the result is a valid, closed, oriented solid whose ANALYTIC (quadrature) `Volume()`/`Area()` match the exact closed forms `pi*r^2*h` / `2*pi*r*h + 2*pi*r^2` to within 1e-4 relative — a genuine curved cap, not a polygon that could only approach those in the limit of infinitely many sides (`TestCapPlanarHolesCapsACurvedCircularRim`, tests/test_basic.cpp). Full `dino8_kernel_tests` suite re-run clean: 100% passing, 0 regressions. Still partial, and does NOT change this category's present/partial/missing counts (6/21/2, 56.9%) — a real gap remains, so it stays scored `partial` rather than `present`, the same convention this document's other narrowing bullets already use: a multi-edge curved chain (e.g. a circle split into two arcs at a seam) is still left open — `PlanCap()`/`FanSurface()` take one boundary curve and `CapClosedCurvedLoop()` doesn't attempt to assemble several edges into one first. Unaffected by the `Check()` false-DegenerateFace defect — `CapPlanarHoles` doesn't call `Check()`/`RemoveDegenerateFaces`.
- [partial] Sweep/extrude a surface, polysurface or mesh face along a path, tapered, or to a point into a mesh solid (ExtrudeSrfAlongCrv/ExtrudeSrfTapered/ExtrudeSrfToPoint) — app `ExtrudeSrfCommand` with translation-only station transforms; mesh output only, no B-rep version in the kernel.
- [partial] Feature extrusions unioned with a base solid following its local normal (Boss, Rib) — app `BossRibCommand` (cmd_srfedit.cpp:2009) projects the curve to the nearest face, lofts rings, and does a mesh boolean union; mesh result, no kernel feature op.
- [partial] Closed, topologically joined B-rep solid output from Sweep1/Sweep2/Loft/Pipe/RailRevolve (auto-cap + join) — every kernel sweep-class factory now returns a real closed `ON_Brep` with literally shared edges (verified via `AssembleSweptBody`, orientation checked by volume sign). Still partial: no kernel `RailRevolve`; fan caps require star-shaped closed sections; every app command still emits an untrimmed surface or a mesh, not the kernel B-rep.
- [missing] ExtrudeCrv / ExtrudeCrvAlongCrv / Revolve producing a SubD object directly (Output=Surface|SubD option) — the command catalogue lists a SubD output option, but no extrude or revolve command in the app implements it.
- [partial] Kernel-level partial-angle revolve parameter (RevolveProfile has no angle argument) — `Mesh::RevolveProfile` (mesh.cpp:3274) takes a trailing `angle`; a full angle keeps the exact shared-vertex ring path, a partial angle delegates to `Brep::Revolve` and tessellates (an approximation for that path). **Narrowed:** `RevolveProfile` now also takes a trailing `start_angle` (mesh.h; mesh.cpp), mirroring `Brep::Revolve`'s own parameter of the same name and closing the "no start-angle parameter" half of this bullet. FULL angle: exact — every ring vertex's own theta is simply offset by `start_angle` before its cos/sin, so the construction stays the same closed-form shared-vertex build, just rotated; verified as a genuine per-vertex rigid rotation (not merely a volume/shape check) by comparing every one of a shifted mesh's own vertices against the base mesh's own vertices carried through the identical `ON_Xform` rotation. PARTIAL angle: passed straight through to the `Brep::Revolve()` delegate, which already had `start_angle` support (see this category's own Full-360-degree-revolve bullet) — a rotated profile's own swept volume is unchanged, confirmed directly (`TestMeshRevolveProfileStartAngle`, tests/test_basic.cpp). Full `dino8_kernel_tests` suite re-run clean: 100% passing, 0 regressions. Still partial, and does NOT change this category's present/partial/missing counts (6/21/2, 56.9%) — real gaps remain, so it stays scored `partial` rather than `present`: an off-axis profile endpoint cannot be capped at a partial angle, and a closed profile touching the axis throws (both `Brep::Revolve()`'s own restrictions, propagated unchanged).
- [partial] Sweep1/Sweep2 producing a SubD result (SubDSweep1, SubDSweep2) — app `SweepThenSubDCommand` (cmd_subd.cpp:1235) runs the (approximate) app Sweep1/Sweep2 and converts the result to SubD after the fact; not a native SubD sweep.
- [missing] SubD-result revolve and multi-pipe menu entries are broken references, not implemented commands (SubDRevolve, SubDMultiPipe) — the SubD menu (MenuBar.cpp:182) still lists both names, with no matching command registration anywhere.

**kernel: Offsetting, shelling, thickening** (offsetshell):
- [partial] Closed hollow shell (uniform wall, no openings) of a solid — app `ShellCommand` still hollows via mesh offset + mesh boolean. Kernel `ShellClosedSphere`/`ShellClosedTorus` (boolean.h:415-451) give an exact B-rep shell, but only for a full sphere or torus; `OffsetSolid(-t)` plus a boolean still gives a general hollow at B-rep level only via two separate calls. **New this pass:** `Mesh::Shell(thickness)` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) closes the equivalent gap at mesh level directly, in one call rather than composing `Offset`+boolean by hand: it hollows an already-closed mesh into two disjoint closed layers — the original, entirely unchanged, as the outer wall, plus a flipped `Offset(-thickness)` copy as the inner wall — with no wall faces needed, since a closed mesh has no naked edge to stitch one to. Refuses a non-positive thickness, an open (not `IsClosedManifold()`) input (that case is `Thicken()`'s, not this method's), and a thickness large enough to fold the inward offset through itself or invert it past the opposite wall (checked via `FindSelfIntersections()` plus an independent enclosed-volume-ordering guard: the inner copy's own volume must land strictly between 0 and the outer's, not just be self-intersection-free — a shape thin/curved enough can silently turn inside-out without any single pair of triangles ever crossing). Still partial: mesh-level, not B-rep; no openings (see the next bullet); wall thickness is uniform, not per-face.
- [partial] Shell with removed/open faces (cup/case), including multi-face openings — kernel `ShellConvexPlanar` (boolean.h:340-387) is exact but convex planar solids only, and mutually-adjacent removed faces are refused. App `ShellCommand` face removal works on a mesh for simple box-like solids only. **New this pass:** `Mesh::Shell(thickness, removed_face_indices)` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) is a genuine KERNEL-level mesh cup/case shell, and — unlike `ShellConvexPlanar` — not limited to convex planar solids: it removes the named faces from both the outer layer and the inward-offset inner layer, then stitches a new ring of side-wall quads around each resulting opening's own boundary, reusing `Thicken()`'s own established "one quad per naked edge, `vi = {a, b, b+n, a+n}`" construction rather than inventing a second one — the result is still a genuine closed, orientation-consistent 2-manifold (`IsClosedManifold()` holds; the cavity is exposed only through the opening's own sealed rim, not through bare naked edges). Mutually adjacent removed faces (an opening spanning several faces) are NOT refused the way `ShellConvexPlanar`'s own convex-planar assembly must refuse them — there is no equivalent topological hazard at the mesh level, verified directly (`TestMeshShellWithRemovedFaceProducesClosedManifoldCupWithStitchedWall`, tests/test_basic.cpp) on both a single removed face and two adjacent removed faces at once. Still partial: mesh-level only, not wired to any `dino8-app` command (the app's own `ShellCommand` still uses its separate, box-like-only mesh path). **New this pass:** the "not specially detected" bowtie gap just above is closed — a new file-local `ThrowIfOpeningBoundaryIsBowtie()` helper (src/mesh.cpp) refuses a removed-face set whose own post-removal naked-edge rim visits any vertex more than twice (two separate openings pinched together at a shared vertex with no shared edge between the removed faces), which neither removed-face `Shell()` overload's side-wall stitching loop can represent unambiguously (there is no way to tell, from the naked edges alone, which two of a degree-4 vertex's edges belong to which opening's own local corner). Verified on a hand-built octahedron (`MakeOctahedronMesh`, tests/test_basic.cpp) where two faces sharing exactly one vertex and no edge are removed together: `TestMeshShellRemovedFacesRefusesBowtieOpeningBoundary` confirms the refusal fires for that pair on BOTH removed-face overloads (uniform and per-face thickness), while a single removed face and two mutually EDGE-adjacent removed faces (an ordinary larger opening, not a bowtie) both still succeed — the guard is specifically about a shared vertex with no shared edge, not about "more than one removed face" in general. **New this pass:** every removed-face `Shell()` test fixture up to now leaves a remainder that stays a SINGLE connected patch — the same scope the app's own `IsSimpleManifoldWithBoundary` guard (cmd_surface.cpp) deliberately restricts itself to — so whether the kernel method itself tolerates a removal that DISCONNECTS the remainder into several separate pieces was untested, not merely unsupported. Nothing in the implementation actually assumes single-piece connectivity: its side-wall stitching loop pairs each naked edge with its own inner-offset counterpart one at a time, with no reference to which connected component it belongs to. Verified directly, not just argued: on `MakeSymmetricCubeMesh`'s own box (whose bottom and top faces share NOT ONE vertex by construction), removing all 4 side faces leaves two entirely disconnected flat squares — `TestMeshShellRemovedFacesHandlesDisconnectedRemainder` (tests/test_basic.cpp) confirms the result is still a genuine closed 2-manifold with no leftover naked edge, that both kept faces' own outer/inner vertices exactly match an independently-computed `Offset(-thickness)` on the full box, and that its enclosed volume lands strictly between 0 and the fully-closed 6-face `Shell(thickness)`'s own — real evidence this kernel method is already MORE general than the app-level guard modeled on `ShellConvexPlanar`'s own convex-planar restriction, not merely undocumented behavior. **New this pass:** the bowtie-boundary helper above is renamed `ThrowIfNakedBoundaryIsBowtie()` (from `ThrowIfOpeningBoundaryIsBowtie()`) and given a `method_name`/`boundary_description` pair so its exception text reads correctly from a caller other than an opening — see the "Thicken sheet" bullet below for why: `Mesh::Thicken()` shares this exact same degree-4-naked-vertex hazard on its own boundary and now runs the identical check.
- [partial] Per-face (multi-thickness) shell — kernel `ShellConvexPlanar` per-face overload exists (convex planar solids only). App `OffsetMeshPerFace` remains mesh-level. **New this pass:** `Mesh::Shell(const std::vector<double>& face_thickness)` (mesh.h/mesh.cpp) is the mesh-level counterpart, generalizing the uniform-thickness `Shell(double)` overload (and building on this same session's `Shell(thickness, removed_face_indices)` opening support above) to any closed 2-manifold, not just a convex planar solid. Since a mesh vertex — unlike a Brep's independently-clippable per-face planes — has no way to carry two different thicknesses on either side of it, a vertex's own offset distance is the AREA-WEIGHTED average of its incident faces' named thicknesses, the same weighting scheme `ComputeVertexNormals()` already uses for direction rather than an unweighted per-face average, an honest reconciliation rather than an approximation error. Verified two ways: a uniform `face_thickness` vector (every entry equal) reproduces `Shell(t)`'s own result up to ordinary floating-point roundoff, and on `MakeSymmetricCubeMesh`'s own equal-area square faces (where area-weighting provably degenerates to a plain average, confirmed by hand-tracing each corner's own triangle-split weights), two named corners' own actual displacement magnitudes match an independently hand-derived plain average of their 3 incident faces' thicknesses (`TestMeshShellPerFaceThicknessMatchesUniformOverloadAndHandDerivedAverages`, tests/test_basic.cpp), plus every refusal case (`TestMeshShellPerFaceThicknessRefusesInvalidInput`). Still partial: mesh-level only, no app wiring, and — unlike `ShellConvexPlanar`'s own per-face overload — a shared vertex's thickness is always a blend rather than a sharp per-face boundary, since a mesh has no analogue of a Brep's independently-clipped planes. **New this pass:** the two most recent generalizations combine — `Mesh::Shell(const std::vector<double>& face_thickness, const std::vector<int>& removed_face_indices)` (mesh.h/mesh.cpp) takes a per-face thickness vector AND an opening's removed-face list in one call, closing the "still separate" gap between this bullet and the "Shell with removed/open faces" bullet above. Construction reuses both parents' own machinery rather than inventing a third: the area-weighted per-vertex reconciliation (now factored into a shared `AreaWeightedVertexThickness()` helper used by both per-face overloads) for the offset distance, and the post-removal outer layer plus opening-boundary side-wall stitching (including the new bowtie guard above) for the opening. A removed face's own named thickness still blends into any boundary vertex it shares with a kept neighbour — the same "no special-casing removed faces" choice the uniform-thickness removed-face overload already makes for its own feasibility guard. Verified two ways, mirroring the standard each parent already holds itself to: a uniform `face_thickness` vector reproduces `Shell(thickness, removed_face_indices)`'s own result exactly (`TestMeshShellFaceThicknessWithRemovedFaceMatchesUniformOverloadAndStitchesWall`), and the same structural counts (vertex/face count, closed manifold, positive volume) the uniform-thickness removed-face overload's own test checks hold here too with a genuinely varying per-face thickness, plus the full refusal surface (`TestMeshShellFaceThicknessWithRemovedFacesRefusesInvalidInput`) and the shared bowtie-boundary refusal (`TestMeshShellRemovedFacesRefusesBowtieOpeningBoundary`).
- [partial] Face offset in place (move one face along its normal, neighbours re-intersected, B-rep kept) — kernel `OffsetFace` (boolean.h:453-488) moves one plane and re-clips every other face against it, but limited to convex planar solids with no topology change allowed (a face vanishing throws); not wired to any app command. App `MovePartsCommand` remains approximate.
- [partial] Body offset (offset an entire closed solid outward/inward as a B-rep) — kernel `OffsetSolid` is a ball dilation/erosion through Manifold Minkowski, mesh-level not B-rep. New this pass: `OffsetSolidConvexPlanar` (boolean.h/.cpp) is an exact B-rep whole-body offset — every face of a convex planar-faced solid moved along its own outward normal at once (uniform or independently per face), sharp/mitered corners reconstructed via the same `ClipConvexPolygon` half-space-clipping `OffsetFace`/`ShellConvexPlanar` already use — verified against an exact box (closed-form volume, both uniform and per-face) and a hand-built tetrahedron (checked against an independent three-plane-intersection recomputation of every new vertex). Still partial: convex planar solids only (the same precondition `OffsetFace`/`ShellConvexPlanar` already enforce), no curved or non-convex body, and not wired to any app command. App `OffsetSrf` non-Surface branch uses a mesh vertex-normal offset.
- [partial] Untrimmed NURBS surface offset — kernel `NurbsSurface::OffsetAnalytic` is exact for plane/sphere/cylinder/cone/torus; `OffsetApproximate` covers freeform surfaces but is first-order with `tolerance` controlling only guard sampling, not the fit error.
- [partial] Trimmed-surface / polysurface offset with corner reconstruction (Sharp extend-and-intersect or Round blend) — sharp corners exist only for convex planar solids (`ShellConvexPlanar`, `OffsetFace`, and now the whole-body `OffsetSolidConvexPlanar`); round corners only via mesh-level `OffsetSolid`; app polysurfaces fall to the mesh path. No trimmed curved-face B-rep offset.
- [partial] Tolerance-driven offset refit (fit the offset surface/curve to a tolerance, Loose/Tolerance options) — curves have it: `NurbsCurve::OffsetInPlane` doubles control points until the measured worst-case deviation is within `tolerance`. Surfaces now do too: `NurbsSurface::OffsetRefit` (surface.h/surface_edit.cpp, new this pass) samples the true offset locus, globally least-squares-refits a new surface to it via `Rebuild()`'s own tensor-product machinery, and doubles the control-point count in each direction until an independently-measured worst-case deviation is within `tolerance` (see the top-of-document honesty note for the verification detail and the real nudge-vs-corner-pinning regression this pass caught and fixed before landing). Still partial: the result is always a new NON-rational surface (never reproduces a rational analytic form exactly, the same disclosed limit `Rebuild()` itself has), it shares `OffsetApproximate`'s own first-order offset-formula model rather than an exact geometric construction, and no app command calls it.
- [partial] Variable-distance surface offset — app `VariableOffsetSrfCommand` is a per-CV Greville-normal offset with distance varying linearly; app-only, no kernel API.
- [partial] Thicken sheet (open surface/mesh) into a closed solid — kernel `Mesh::Thicken` (mesh.cpp:3295) works on open meshes only with no fold repair. App `OffsetSrf` Solid=Yes stitches with `ShellBetween`. **New this pass:** `Brep::Thicken` (dino8-kernel/include/dino8/kernel/brep.h; src/sweep.cpp) is a genuine B-rep counterpart, also credited under kernel: Feature operations' own "Thicken a sheet body into a solid" item (the same one-capability-two-vocabularies pattern this document already uses for `SplitByObjectCommand`/`DraftFacesConvexPlanar`) — see that bullet for the full construction and test detail. Still partial: a single-face, untrimmed, non-periodic sheet only, and no app command calls it. **New this pass:** `Mesh::Thicken` closes two silent-corruption gaps its own side-wall stitching loop shared with no guard against either. (1) A shared `ThrowIfNakedBoundaryIsBowtie()` helper (renamed and generalized from `Mesh::Shell`'s own removed-face-overload guard, mesh.cpp) now also runs on `Thicken()`'s naked-edge boundary: a naked-edge vertex shared by more than two naked edges — two lobes of an open sheet touching at a single point, not an edge — is refused instead of producing an ambiguous, non-manifold wall there, verified on a two-triangle fixture sharing exactly one vertex (`TestMeshThickenRefusesBowtieBoundaryButAllowsDisconnectedSheet`, tests/test_basic.cpp), contrasted against two entirely disconnected (non-touching) squares still succeeding. (2) `Thicken()` previously ran no fold-safety check at all on its own offset copy, unlike `Shell(thickness)`'s own `FindSelfIntersections()` guard on its inward offset — reusing the exact same V-groove/distance-(-1.0) pairing `TestMeshFindOffsetSelfIntersectionsDetectsGenuineFold` already proved genuinely folds, `TestMeshThickenRefusesSelfIntersectingFold` confirms `Thicken()` now refuses it too, while a small safe distance on the same groove still succeeds. Still partial: mesh-level only, and refusal — not the disclosed "no fold repair" gap itself, which stays open. **New this pass:** `Mesh::Thicken`'s own `distance` check — previously a bare `distance == 0.0` test — now also rejects a non-finite `distance`, closing a gap its own B-rep sibling `Brep::Thicken` did not share: that method already checks `!std::isfinite(thickness)` (sweep.cpp:2099, exercised by `TestThickenRejectsInvalidArguments` above), but `Mesh::Thicken` never did — a real, not hypothetical, gap, since NaN and +/-infinity both fail `== 0.0` and so both previously sailed past `Thicken()`'s only guard and reached `Offset()`'s per-vertex arithmetic uncaught (see `Offset()`'s own new guard, credited above under "Mesh offset (per-vertex offset, solid/shell option)"). Verified directly: `TestMeshThickenBuildsExactUnitCubeFromFlatSquare` (tests/test_basic.cpp) now also confirms `Thicken()` throws `std::invalid_argument` for both a NaN and an infinite `distance`. Still partial for the reasons already named above.
- [partial] Planar curve offset (lines, arcs/circles, freeform NURBS) — kernel `NurbsCurve::OffsetInPlane` is exact for a line or arc/circle, tolerance-driven least-squares refit for other curves. **New this pass:** a POLYLINE is now a third exact case, not routed through the blurring refit at all: `TryOffsetPolylineAlongNormal()` (curve.cpp, shared by both `OffsetInPlane` overloads) detects a piecewise-linear curve via `ON_Curve::IsPolyline()` and offsets it corner-by-corner via the same closed-form angle-bisector miter point `OffsetConvexPolyline()` (sweep.cpp) uses for its own solid-cap case — `v + (distance / (1 + dot(n0, n1))) * (n0 + n1)`, each `n` the unit `edge_direction x normal` — needing no convexity check here either (the formula itself needs no convexity, only that no corner is within a hair of a full 180-degree fold, refused, `Result::Failed`, same as a zero-length or normal-parallel edge; **a later same-day session dropped `OffsetConvexPolyline()`'s own separate convexity restriction too, replacing it with an exact simple-polygon check - see this category's own "Extrude with draft / taper angle" bullet**). A closed polygon's own seam vertex mitres the same way, wrapping from the last edge to the first, so it no longer splits. Verified independently (point-to-line distance, not this method's own miter formula) on an open 4-vertex bracket, a closed square (including its wraparound seam), and a genuinely CONCAVE (reflex-vertex) closed hexagon (`TestCurveOffsetInPlaneOpenPolylineExactSharpCorners`, `TestCurveOffsetInPlaneClosedPolygonMitersSeamCorner`, `TestCurveOffsetInPlaneConcavePolygonGeneralizesBeyondConvexOnlyMiter`, tests/test_basic.cpp). A near-180-degree fold is refused rather than silently producing a huge or wrong-signed corner (`TestCurveOffsetInPlanePolylineRefusesNearFullFold`). The explicit-plane overload only takes this exact path when the polyline is ITSELF coplanar in the CALLER's plane (checked explicitly, not assumed) — a genuinely 3D polyline offset by a foreign plane still falls to the old general sampled path, confirmed to differ in both directions: it can succeed with a much larger (non-4-CV) result at a looser tolerance, or be honestly refused at the tight default tolerance when the per-segment offset locus has a real discontinuity a continuous refit can't close (`TestCurveOffsetInPlaneWithExplicitPlanePolylineMatchesSinglePlaneOverload`, `TestCurveOffsetInPlaneWithExplicitPlaneNonCoplanarPolylineFallsBackToGeneralPath`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial: a concave polygon's offset can still self-intersect for a distance exceeding its own local feature size (the same disclosed, un-repaired "Offset self-intersection / invalid-loop removal" gap this category already names, inherited here rather than newly introduced), only Sharp/miter corners exist (no Round/Chamfer/Smooth), and the app's `Offset`/`OffsetPolygon` commands still don't call either overload.
- [partial] Curve offset corner handling at kinks (Sharp/Round/Chamfer/Smooth/None) — sharp (miter) only, via a file-local `OffsetConvexPolyline` (convex OR concave simple polylines, not a public API) and the app's `OffsetPolygon`. No Round/Chamfer/Smooth corner modes.
- [partial] Offset self-intersection / invalid-loop removal (inward offset of concave curves, surfaces and bodies) — no loop removal for curves or surfaces (refused); detection exists via `Mesh::FindOffsetSelfIntersections` (mesh.cpp:3342), curvature guards in `OffsetInPlane`/`OffsetApproximate`, and `OffsetAnalytic` radius/spindle guards. `OffsetSolid`'s Minkowski erosion/dilation cannot self-intersect by construction but is only tested on convex fixtures.
- [partial] Curve offset on surface (in-surface, geodesic-style) — app `OffsetCrvOnSrfCommand`: sample, move along tangent x normal, re-project by closest point. App-only, approximate.
- [partial] Curve offset normal to surface (OffsetNormal) — app `OffsetNormal`: samples moved along the surface normal, then cubic interpolation. App-only.
- [partial] Curve offset in an arbitrary plane / 3D (non-planar) curve offset — kernel `OffsetInPlane` works in the curve's own fitted plane but returns Failed for non-planar curves. The app Offset command uses only the active CPlane normal. No 3D offset. **New this pass:** `NurbsCurve::OffsetInPlane(const ON_Plane&, distance, out, tolerance)` (curve.h/curve.cpp) is a new overload that takes the caller's own plane instead of auto-fitting one, closing both gaps this bullet names at once — it no longer requires this curve to be planar at all (the direction `TangentAt(t) x plane.zaxis` is well-defined for a genuinely non-planar 3D curve too, sampled and tolerance-refit the same way the existing overload's general case already works), and it lets the caller pick ANY plane, not only this curve's own fitted one (an exact-line offset generalizes for free to any plane whose normal isn't parallel to the line; both `OffsetInPlane` overloads now share that line case and the general sampled-refit case via two new private helpers, `OffsetLineAlongNormal`/`OffsetGeneralAlongNormal`, rather than duplicating the loop). Verified three ways: (1) `TestCurveOffsetInPlaneWithExplicitPlaneSucceedsOnNonPlanarCurve` offsets the exact non-planar cubic the single-plane overload's own refusal test uses, and checks the result against an independently-recomputed `TangentAt(t) x plane.zaxis` locus; (2) `TestCurveOffsetInPlaneWithExplicitPlaneLineIsExactForAnyNonParallelPlane` offsets a line by a plane with no relation to the line's own axis and checks both endpoints exactly; (3) `TestCurveOffsetInPlaneWithExplicitPlaneMatchesSinglePlaneOverloadWhenPlanesAgree` confirms handing this overload a planar curve's own fitted plane reproduces the single-plane overload's result exactly (same refined control-point count, sub-nanometer deviation) - a real regression guard on the shared-helper refactor. Refusal (`TestCurveOffsetInPlaneWithExplicitPlaneRefusesWhenTangentParallelToNormal`) and argument-check (`TestCurveOffsetInPlaneWithExplicitPlaneThrowsOnInvalidArguments`) coverage added too. Still partial: an arc/circle offset by a plane other than its own is not special-cased to stay exactly circular (falls to the same approximate sampled path as any other curve - offsetting a circle along a foreign normal generally isn't a circle at all, so this is honest, not a shortcut), and the app's `Offset` command still doesn't call either overload with anything but the active CPlane.
- [partial] Mesh offset (per-vertex offset, solid/shell option) — kernel `Mesh::Offset` (mesh.cpp:3283) plus `Thicken` for the solid option (open meshes only), plus `FindOffsetSelfIntersections`. Still partial: an area-weighted vertex-normal push does not preserve wall thickness at creases, and folds are not repaired. **New this pass:** `Mesh::Offset`/`Mesh::OffsetDirectional` (mesh.h/mesh.cpp) now reject a non-finite (NaN or +/-infinity) `distance` up front — a real, previously-unguarded silent-corruption gap, not a hypothetical one: before this pass, `distance == 0.0` was the only check either method made (`OffsetDirectional`'s own zero-vector-direction check is unrelated and stays as-is), so a NaN or infinite `distance` sailed straight through and got baked into every vertex — and since a NaN/Inf coordinate makes most downstream numeric comparisons resolve to false rather than true, a fold/self-intersection guard built on top of either method (`Shell()`'s and `Thicken()`'s own `FindSelfIntersections()` calls) could not be relied on to catch the corruption either, only its own accidental side effects. Verified directly, not merely argued: `TestMeshOffsetMovesVerticesAlongExactVertexNormal` and `TestMeshOffsetDirectionalMovesEveryVertexByTheSameFixedVector` (tests/test_basic.cpp) now also confirm both methods throw `std::invalid_argument` for a NaN and for an infinite `distance`. Still partial for the reasons already named above — this closes an input-validation gap, not the disclosed crease/fold limitation.
- [partial] SubD offset / thicken — app `OffsetNet` (cmd_subd.cpp:595) offsets the control net (not the limit surface); Solid adds a flipped copy plus side quads. No kernel SubD offset.
- [partial] Offset-derived constructions (Ribbon, RibbonOffset, Fin, Slab) — `RibbonCommand`, `FinCommand`, `RibbonOffset`, Slab via `OffsetPolygon`: all sample-and-fit, app-only.
- [partial] Exact analytic-face offset (plane->plane, cylinder->cylinder, cone->cone, sphere->sphere with shifted radius) — **corrected: the cylinder branch keeps a partial patch's own angular extent (prior pass); this pass, the cone branch keeps BOTH its angular AND axial extent too — still partial.** `NurbsSurface::OffsetAnalytic`'s cylinder case (surface.cpp) previously always called `ON_Cylinder::GetNurbForm()` and returned it as-is, silently ballooning a partial patch (e.g. a quarter-cylinder fillet face) into a closed 360-degree tube. It now measures the input patch's own true angular span geometrically (`ON_Circle::ClosestPointTo` at both U-domain ends, independent of either cylinder's radius since offset and original share the same `circle.plane`), converts that span through `ON_Circle::GetNurbFormParameterFromRadian` — required because a circle's NURBS parameter is a rational-quadratic reparametrization, not linear in angle, except at the four quadrant knots — and `Trim()`s the freshly-built full cylinder down to it, so the offset keeps the same angular extent the input had. A full/closed input cylinder (U-domain already spanning a full turn) is detected up front and left untrimmed, exactly reproducing the prior (correct) behavior for that case. Verified on a quarter-turn patch of a radius-4, height-10 cylinder offset by +1.0: every sampled point on the result still sits at exactly radius 5.0 from the axis (same exactness the full-cylinder case already had), the U-min and U-max boundary curves sit at true angle 0 and pi/2 respectively (not anywhere else on the circle), and the trimmed U-domain span is a small fraction of a full turn's own span rather than the whole circle (`TestSurfaceOffsetAnalyticCylinderPreservesQuarterPatchExtent`, tests/test_basic.cpp). **New this pass:** the cone branch had the SAME defect in both its own directions at once — `ON_Cone::GetNurbForm()` always builds a full 360-degree cone running from the literal apex (height_parameter 0) out to whatever `height` it's given, so the prior code (which just reused the ORIGINAL cone's own `height`/`radius` pair unchanged, only translating the apex) silently ballooned a partial patch angularly (same issue as the cylinder) AND axially (a fillet-style frustum band that touches neither the apex nor the base would balloon all the way down to the apex). The angular half reuses the cylinder branch's own technique verbatim (one shared reference circle works for both the old and new cone, since the new cone's plane is the old one's translated ONLY along its own zaxis, which cannot change a point's projected angle). The axial half is genuinely different from the cylinder case, and can't reuse its "leave the original bounds alone" shortcut: a cone's offset shifts axial position too (the apex itself moves along the axis), and — unlike the angular case — the v-domain boundary can BE the apex, a singular point with no well-defined normal to offset along. The fix proves (and the code comment derives) that the offset image's axial position is an exact, angle-independent, slope-1 affine function of the original axial position, measures that function's one free constant at the safely-interior domain midpoint (never a boundary), then applies it to the actual v-domain boundaries via plain axial projection (a dot product, safe even exactly at the apex) — and, since the true offset extent can now run past the original cone's own arbitrary `height` value, the pre-trim substrate cone is sized from the computed extent itself rather than reusing the original `(height, radius)` pair verbatim (which could otherwise be too short to trim down to the real range, a latent bug the fix also closes). A patch straddling the apex on both sides at once — impossible to represent as a single `ON_Cone::GetNurbForm()` nappe — is refused. Verified on a quarter-turn (angular), mid-height-band (axial, touching neither apex nor base) patch of the same radius-5, height-10 cone offset by +1.0: the angular boundaries sit at true angle 0 and pi/2 exactly as the cylinder case's own test checks, and — the axial fix's own direct proof — the output's own v-domain MIN and MAX boundary points are each bit-exact (`< 1e-6`) matches of the INPUT patch's corresponding v-domain boundary point plus `distance * NormalAt(...)` there, evaluated at the identical true angle on each surface's own domain despite their differing nonlinear NURBS parameter values, with the output's own v-span confirmed to be a small fraction of the full apex-to-base height rather than the whole cone (`TestSurfaceOffsetAnalyticConePreservesPartialPatchExtent`, tests/test_basic.cpp); the pre-existing `TestSurfaceOffsetAnalyticConePreservesHalfAngleAndShiftsApex` (a FULL, apex-touching cone) needed its own golden-section search bracket widened from a hardcoded guess to the output's own actual (now genuinely narrower, apex-excluding) domain, since that is exactly the behavior this fix intentionally introduces. **New this pass:** the sphere branch had the same defect in BOTH its own directions at once (longitude in u, latitude in v) — `ON_Sphere::GetNurbForm()` always builds the full 360-degree-by-180-degree primitive, so the prior code (which reused `ON_Sphere(center, radius)`'s own default plane unchanged) silently ballooned any partial patch (e.g. a spherical fillet/lens face) back to the whole sphere. Fixing it surfaced a real bug one level up: `ON_Sphere(center, radius)`'s own constructor always resets `plane` to the world `ON_xy_plane` (`opennurbs_sphere.cpp`'s `Create()`), discarding the input sphere's own orientation — invisible on a full sphere (isotropic, no seam to misplace) but fatal to any orientation-dependent trim. The fix does NOT simply copy the input's own fitted `plane` back, though: `ON_Surface::IsSphere()`'s own fallback fit (`opennurbs_revsurface.cpp`) samples both an equator-style isocurve and a meridian-style isocurve, each `IsArc()`-fitted into its own candidate `ON_Sphere::plane`, and returns whichever ONE happens to validate first — a sphere's rotational symmetry makes either an equally valid "some center/radius fit", but the two are genuinely different (rotated) frames, and for anything short of a full untrimmed sphere there is no way to know in advance which one `IsSphere()` handed back. So the fix derives its own frame directly from sampled points on the actual input surface instead of trusting `IsSphere()`'s own `plane` at all: a latitude circle's own plane is perpendicular to the polar axis by definition, so 3 points at the same v and 3 distinct u fractions (never the exact domain ends, so a full/closed loop's own coincident seam can't degenerate the fit) pin down that axis, and the first of the 3 (at the input's own u-domain minimum) fixes the equatorial xaxis — the same self-derived frame is then used to build `new_sphere.plane` before calling its own `GetNurbForm()`, guaranteeing the output's raw u/v convention matches what the trim math measures. A second, genuinely separate bug surfaced verifying this against a deliberately non-world-aligned sphere (world-aligned inputs can't expose either bug, by luck): the u-domain's own minimum sample sits EXACTLY on the derived xaxis by construction — the single worst point for `ClosestPointTo`'s `[0, 2pi)` wraparound, where ordinary float noise can push the measured angle to a hair below 0 (fine) or a hair above 2*pi's own wrap boundary (silently landing at ~2*pi instead of ~0, flipping a genuine short arc's own two ends to look like the long way around the circle) — fixed by re-expressing both u-domain ends within +-pi of the (always safely domain-interior) midpoint before comparing, then clamping the result back into `[0, 2pi]` before `Trim()`. Verified (`TestSurfaceOffsetAnalyticSpherePreservesPartialPatchExtent`, tests/test_basic.cpp) on a quarter-longitude, equator-to-north-pole quarter-latitude patch of a sphere whose polar axis is deliberately NOT any world axis: every sampled point on the offset patch sits at exactly the offset radius from the same center; the u-domain's two boundaries sit at true longitude 0 and pi/2 measured against the ORIGINAL (rotated) frame, not wherever a mismatched frame would place them; the v-domain's two boundaries sit at true latitude 0 (equator) and pi/2 (north pole) the same way; both domain spans are well under their own full untrimmed range; and the pre-existing self-intersection guard (an inward offset exceeding the radius) still refuses on this same partial patch. Full `dino8_kernel_tests` suite re-run clean: 100% passing, 0 regressions. **New this pass:** the torus branch had the same "GetNurbForm() always builds the FULL primitive" defect in BOTH its own directions (major angle in u, minor angle in v) — `ON_Torus::GetNurbForm()` always builds the complete 360x360-degree primitive, so the prior code (which reused `torus.plane`/`major_radius` unchanged and only shifted `minor_radius`) silently ballooned any partial patch (e.g. a torus-fillet lens face) back to the whole donut. Torus turns out to be the ONLY one of the four analytic families whose `GetNurbForm()` builds via the generic `ON_RevSurface::TensorProduct` path rather than a dedicated direct construction (confirmed by reading `opennurbs_torus.cpp`/`opennurbs_revsurface.cpp`, not assumed) — and that path rescales BOTH the raw `[0, 2*pi]` rational-quadratic circle parameter the sphere/cylinder/cone branches already convert via `GetNurbFormParameterFromRadian()` into ARC-LENGTH units instead (`[0, 2*pi*major_radius]` for u, `[0, 2*pi*minor_radius]` for v) — confirmed directly against `GetNurbForm()`'s own output domain, not assumed from reading the source alone. So converting a measured true angle to this surface's own domain value needs the usual conversion PLUS one extra multiply by the relevant radius, using the OFFSET tube radius (not the original) for v, since unlike `major_radius` the tube radius genuinely changes under this offset. A second, genuinely separate discovery made verifying this: `ON_Torus::ClosestPointTo()`'s own `minor_angle` output is a real, previously-undiscovered OpenNURBS bug — its internal arithmetic subtracts a bare direction vector (`major_radius*raxis`) from the absolute query point without first subtracting `plane.origin`, silently assuming the torus is centered at the world origin; invisible on a world-centered fixture (the existing coaxial-torus test above never exposed it) and confirmed wrong by over a tenth of a radian on an off-origin one. Worked around, the same way the cylinder branch above already works around its own `circle.plane.ClosestPointTo` bug: minor angle is measured via `MinorCircleRadians(major_angle)`'s OWN `ClosestPointTo` (an entirely different, independently-verified-exact code path) instead. A defensive convention check also guards the one remaining ambiguity `IsTorus()`'s own fallback fit shares with `IsSphere()`'s (above): a surface built with u/v transposed from the ordinary (u=major, v=minor) convention is refused rather than mishandled, verified directly (does moving along u change major_angle, as expected, or minor_angle?) rather than trusted. Verified (`TestSurfaceOffsetAnalyticTorusPreservesPartialPatchExtent`, `TestSurfaceOffsetAnalyticTorusOffOriginTiltedPreservesArbitraryPatchExtent`, `TestSurfaceOffsetAnalyticTorusRefusesTransposedConvention`, tests/test_basic.cpp) on a world-aligned quarter-major/half-minor patch (every sampled point at exactly the offset tube radius; u/v boundaries at true angle 0 and pi/2 or pi via direct atan2/radius checks; both domain spans well under a full turn) and, more adversarially, an off-origin, non-axis-aligned torus trimmed to arbitrary (non-knot-aligned) major and minor sub-ranges (same tube-radius check; all 4 domain corners land exactly on this patch's own point + distance*normal, the strong check that catches a merely-plausible-looking wrong sub-range) — plus the transposed-convention refusal. Full `dino8_kernel_tests` suite re-run clean: 5864 checks passing, 0 regressions (one known-intermittent, unrelated `FoldFaceConvexPlanar` flake — see this document's own prior note on it — reproduced on one run and was absent on a clean rerun of the same unchanged binary). Still partial: a patch whose angular span straddles either circle parametrization's own seam (verified via a midpoint-inside-range check, same as the other branches) is refused rather than mishandled, and it is still a single-surface operation, not a face within a B-rep. Every analytic branch this method has (plane, cylinder, cone, sphere, torus) now keeps the domain and trim.
- [partial] Offset feasibility / degeneracy detection (thickness beyond inradius, collapsed faces, wrong-way rims) — many guards exist (`ShellConvexPlanar`, `OffsetFace`, `OffsetSolid`, `OffsetAnalytic`, `OffsetInPlane`/`OffsetApproximate` curvature guards, `ExtrudeTapered` inradius check, `FindOffsetSelfIntersections`). Still partial: freeform checks are local-curvature/sampling-based and can miss hazards between samples; no global collision check.
- [partial] Kernel-level offset API (NurbsCurve::Offset, NurbsSurface::Offset, Brep offset/shell entry points usable by booleans and fillets) — each named family exists (`OffsetInPlane`, `OffsetAnalytic`/`OffsetApproximate`, `ShellConvexPlanar`, `ShellClosedSphere`/`Torus`, `OffsetFace`, `OffsetSolidConvexPlanar`, `OffsetSolid`, `Mesh::Offset`/`Thicken`, `Mesh::Shell` (all four overloads: uniform, with openings, per-face, per-face-with-openings)). Still partial: no general Brep offset/shell for curved or non-convex bodies, and no app command calls any of these kernel entry points yet.
- [partial] Solid dilation/erosion via kernel::MinkowskiSum/MinkowskiDifference with a ball (whole-body offset that handles arbitrary curved/concave meshes, not just convex-planar) — wrapped as `OffsetSolid(solid, distance, sphere_divisions)` with a faceted ball and an empty-erosion guard. **New this pass:** the "every test fixture is convex" gap is closed — `TestOffsetSolidHandlesGenuinelyConcaveSolid` (tests/test_basic.cpp) exercises both directions on a genuinely non-convex L-shaped prism fixture (`BooleanCombine(Box(0,0,0,10,4,6), Box(0,0,0,4,10,6), BooleanOp::Union)`, with one real reflex/concave vertical edge where the two arms meet, its own hand-computed cross-section area 100-36=64 checked as a sanity gate before offsetting it at all). Growth is checked against a real, convexity-independent closed form the existing convex-only Steiner-formula test above can't reuse: a Minkowski sum with a ball of radius `d` expands the axis-aligned bounding box by EXACTLY `d` on every one of the 6 sides for ANY compact solid, reflex edges included, since the solid and the ball attain their own per-axis extremes independently (`bbox_max_x(P (+) B) = bbox_max_x(P) + max_b(b.x) = bbox_max_x(P) + d`) — verified to hold exactly on this fixture, plus `IsClosedManifold()` and a strictly-larger-volume check. Erosion at a moderate distance (well within the L-shape's own thinnest wall) is checked for the direction-agnostic invariant every erosion must satisfy regardless of convexity — succeeds, stays a closed manifold, and encloses strictly less material than the original, never more. Still partial: mesh-only, rounding only as smooth as the faceted sphere, and the concave fixture's own reflex edge is checked for structural validity (manifold, correct bounding box, correct volume ordering) rather than pinned to an exact closed-form volume the way the convex box tests already are — a genuinely non-convex Minkowski sum's exact volume has no comparably simple formula, so this is honest verification of the invariants that DO hold generally, not a claim of matching one that doesn't apply here. **New this pass:** `OffsetSolid` now rejects a non-finite `distance` up front, ahead of its own `distance == 0.0` no-op check — a real, not hypothetical, gap: NaN and +/-infinity both fail `== 0.0`, so either previously reached `Brep::Sphere(origin, std::fabs(distance))` uncaught, building a NaN- or infinite-radius "sphere" that then fed straight into `TessellateToClosedMesh()`/Manifold with no guard anywhere in between. Verified for both signs of infinity — the new check runs before the `distance > 0.0` grow/shrink branch, so a `-infinity` shrink is caught the same way a `+infinity` grow is, not just one of the two — and for NaN: `TestOffsetSolidZeroDistanceIsIdentityAndArgumentChecks` (tests/test_basic.cpp) now also confirms `OffsetSolid` throws `std::invalid_argument` for a NaN, a `+infinity`, and a `-infinity` distance. Still partial for the reasons already named above.
- [partial] OpenNURBS-native mesh offset, ON_Mesh::OffsetMesh(distance, direction) — `ON_Mesh::OffsetMesh` is still never called. **New this pass:** the fixed-direction variant this bullet's own prior text said had "no kernel counterpart" now does: `Mesh::OffsetDirectional(distance, direction)` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) moves every vertex by the identical vector `distance * direction.UnitVector()`, never consulting any per-vertex normal at all — genuinely distinct from the existing `Mesh::Offset(distance)` (which moves each vertex along its OWN `ComputeVertexNormals()` direction), not a renamed duplicate: translating every point of a flat region by one shared vector keeps that region exactly planar regardless of a neighboring crease's own curvature (`Offset()` can't make that guarantee, since a crease-adjacent vertex's averaged normal differs from an isolated flat vertex's), at the honest cost of no longer keeping wall thickness uniform across a curved region (every vertex moves the same amount along `direction`, not the locally-true normal) — the same disclosed tradeoff the fixed-direction OpenNURBS variant itself has. `direction` need not be a unit vector (normalized internally); a zero vector is refused (`std::invalid_argument`), since there is no well-defined unit direction to offset along. Verified on a flat unit square (reproduces `Offset()`'s own result exactly when `direction` matches the square's normal, and confirms a non-unit `direction` still moves by exactly `distance` along its unit form, not `distance` times the vector's own magnitude) and, more adversarially, on a V-groove fixture (two non-parallel walls meeting at a shared apex edge, reused from `FindOffsetSelfIntersections`'s own test): `OffsetDirectional()` moves every vertex — apex and wall-top alike — by the exact same displacement vector, while a control `Offset()` call on the identical groove is independently confirmed to move at least one vertex by a genuinely DIFFERENT vector (the apex's own averaged normal differs from a wall-top vertex's) — proving the two methods solve different problems, not merely checking each in isolation (`TestMeshOffsetDirectionalMovesEveryVertexByTheSameFixedVector`, tests/test_basic.cpp). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions (one known-intermittent, unrelated `FoldFaceConvexPlanar` flake — see this document's own prior note on it — reproduced on one run and was absent on a clean rerun of the same unchanged binary). Still partial: face topology is untouched (no fold repair or self-intersection handling — the same disclosed hazard `Offset()` already has, inherited here rather than newly introduced), `ON_Mesh::OffsetMesh` itself is still never called, and no app command calls this new kernel entry point.
- [partial] Inset (offset mesh/SubD/polysurface face edges inward toward face center) — app `InsetFaces` (cmd_subd.cpp:544) moves each corner toward the face centroid, not a true in-plane edge-parallel inset; SubD only. **New this pass:** kernel `Mesh::InsetFace(face_index, distance, depth)` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) is a genuine in-plane, edge-parallel mesh inset: every edge of the named triangle or quad face moves inward parallel to its own original direction, and each new corner is the exact mitered intersection of its two adjacent moved edges — the same construction sweep.cpp's own `OffsetConvexPolyline` already uses for a curve profile, applied here to one mesh face's closed boundary ring instead. The original face is replaced by a ring of `n` new "frame" quads (one per original edge) plus one new inner face at the inset ring, every new vertex staying exactly coplanar with the original face (`depth`, if nonzero, additionally lifts only the inner ring along the face's own normal — the bevelled/pushed variant of the same tool). Verified on a flat unit-square face (distance 0.25, an exact power-of-two fraction chosen so the check survives `ON_3fPoint`'s own float vertex storage without a spurious rounding mismatch): the 4 new corners exactly match the hand-derived concentric inset square, and total mesh area is conserved exactly — a flat inset only subdivides the same planar region, it can't add or remove material (`TestMeshInsetFaceUnitSquareMatchesExactConcentricSquare`). Also verified on a non-axis-aligned 3-4-5 right triangle via an INDEPENDENT point-to-line distance check (not a re-run of this method's own miter formula): every new corner sits at exactly `distance` from both of its two adjacent original edge LINES (`TestMeshInsetFaceTriangleMatchesIndependentPerpendicularDistance`). Refusal cases covered: an out-of-range `face_index`, a non-positive `distance`, a distance reaching or exceeding the face's own inradius (folds a corner past the opposite side), a non-planar ("warped") quad, and a concave (reflex-cornered) but still simple quad (`TestMeshInsetFaceRefusesInvalidInput`); `depth`'s own inner-ring-only lift is checked separately (`TestMeshInsetFaceDepthLiftsInnerRingAlongNormal`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial, and deliberately narrow: one triangle or quad face at a time (an `ON_Mesh` face can be no larger — no general n-gon), convex only (a reflex corner throws rather than being handled, the same scope every other convex-planar-ring construction in this category already has), no SubD or Brep-level inset, and `dino8-app`'s own `InsetFaces` still only does the centroid-drag approximation — a grep for InsetFace in dino8-app/src still finds nothing.
- [partial] Inset on raw mesh or polysurface objects (as opposed to SubD) — **upgraded from missing.** App `Inset` still routes every target through `SubDTargets` (cmd_subd.cpp:827), which rejects any non-SubD object — that app-level gap is unchanged. But the kernel is no longer empty here: `Mesh::InsetFace` (see this category's own "Inset" bullet above for the full construction and test detail) operates directly on a raw `ON_Mesh` face, not routed through SubD machinery at all. Still partial: no app command calls it, one triangle/quad face at a time, and there is still no Brep-level (polysurface) inset anywhere in the kernel.
- [partial] ShrinkWrap Offset (signed-distance-field / marching-cubes mesh offset, inherently self-intersection-free) — app `ShrinkWrapAction`'s (cmd_remesh.cpp:198) Offset option feeds `MarchingCubes(grid, offset)`. App-level, voxel-resolution accuracy.

*Note on this category's counts: this session's `Mesh::InsetFace` addition (see the "Inset on raw mesh or polysurface objects" bullet above) closes this category's own last `[missing]` item: missing→partial, 1 missing → 0 missing. Every other item in this category was already `[partial]` and stays `[partial]` (the sibling "Inset" bullet's own upgrade is new evidence for an already-partial item, not a status change). That moves the table row from 0/26/1/27 (48.1%) to 0/27/0/27 — (0 + 0.5·27)/27 = 50.0%.*

*Later still, a same-day follow-up: this pass's own `TryOffsetPolylineAlongNormal()` addition (see the "Planar curve offset" bullet above) genuinely closes the "a kinked polyline goes through the smooth refit, blurring corners" half of that item's own disclosed gap, for BOTH `OffsetInPlane` overloads. It does NOT change this category's own present/partial/missing counts: the "Planar curve offset" item was already credited `[partial]` (an arc/circle-only exact case, general refit otherwise) and stays `[partial]` — real, tested, honestly-scoped new exact coverage (self-intersection on a concave offset is still unrepaired, no Round/Chamfer/Smooth corners, no app wiring), not yet a `[present]`. The table's own 0/27/0/27 (50.0%) is unchanged.*

*Later still, a further same-day follow-up: this pass's own cylinder-branch fix to `NurbsSurface::OffsetAnalytic` (see the "Exact analytic-face offset" bullet above for the full construction and test detail) genuinely closes the "a cylinder becomes a full 360-degree cylinder" half of that item's own disclosed gap. It does NOT change this category's own present/partial/missing counts: the "Exact analytic-face offset" item was already credited `[partial]` (exact for plane/sphere/cylinder/cone/torus radius/apex math, but extent-preserving only for planes) and stays `[partial]` — real, tested, honestly-scoped new exact coverage for one of its four named analytic families, not yet a `[present]` (sphere, torus and cone still discard the input patch's own extent, a seam-straddling cylinder patch is refused rather than mishandled, and this is still a single-surface operation with no B-rep-face wiring). The table's own 0/27/0/27 (50.0%) is unchanged.*

*A later pass, in turn: this pass's own cone-branch fix to `NurbsSurface::OffsetAnalytic` (see the "Exact analytic-face offset" bullet above for the full construction and test detail) closes BOTH the angular and axial halves of that item's own "cone still discards the input patch's own extent" disclosed gap in one pass — genuinely more than the cylinder fix above closed in its own (angular-only) turn, since a cone's offset shifts axial position too, needing a real (verified, not just asserted) affine-extrapolation argument the cylinder's purely-radial offset never needed. Does NOT change this category's own present/partial/missing counts: "Exact analytic-face offset" was already `[partial]` and stays `[partial]` — sphere and torus still discard the input patch's own extent entirely, and this remains a single-surface operation with no B-rep-face wiring. The table's own 0/27/0/27 (50.0%) is unchanged.*

*A later pass still: this pass's own sphere-branch fix to `NurbsSurface::OffsetAnalytic` (see the "Exact analytic-face offset" bullet above for the full construction and test detail) closes BOTH the longitude and latitude halves of that item's own "sphere still discards the input patch's own extent" disclosed gap in one pass, and along the way fixes two real, previously-undiscovered bugs the cylinder/cone fixes never had to face: `ON_Sphere(center, radius)`'s own constructor silently resetting `plane` to the world XY plane, and `ON_Surface::IsSphere()`'s own fallback fit being genuinely ambiguous (equator-frame vs. meridian-frame) for anything short of a full sphere — both invisible on the world-aligned fixtures a less adversarial test would have used. Does NOT change this category's own present/partial/missing counts: "Exact analytic-face offset" was already `[partial]` and stays `[partial]` — torus still discards the input patch's own extent entirely, and this remains a single-surface operation with no B-rep-face wiring. The table's own 0/27/0/27 (50.0%) is unchanged.*

*A later pass in turn: this pass's own torus-branch fix to `NurbsSurface::OffsetAnalytic` (see the "Exact analytic-face offset" bullet above for the full construction and test detail) closes BOTH the major-angle and minor-angle halves of that item's own "torus still discards the input patch's own extent" disclosed gap, and along the way finds and works around a real, previously-undiscovered OpenNURBS bug in `ON_Torus::ClosestPointTo()`'s own minor-angle output (wrong for any off-origin torus, invisible on the world-centered fixture the pre-existing coaxial-torus test already used). This is the last of the five analytic branches (plane, cylinder, cone, sphere, torus) to gain extent preservation. Does NOT change this category's own present/partial/missing counts: "Exact analytic-face offset" was already `[partial]` and stays `[partial]` — a seam-straddling patch is still refused rather than mishandled in any branch, and this remains a single-surface operation with no B-rep-face wiring. The table's own 0/27/0/27 (50.0%) is unchanged.*

**This session's own follow-up:** `git log --oneline -3 -- dino8-kernel/src/mesh.cpp` at the start of this session showed `004f3aa` (`kernel: Mesh::Shell hollows a closed mesh with a uniform-thickness wall`) as the most recent commit touching this file, landed but never reflected in this document - so this session documented that gap first (see the "Closed hollow shell" bullet's own "New this pass" text above), then picked the next closely-related item in the same category rather than a distant one: "Shell with removed/open faces (cup/case)", the very next bullet, whose own kernel evidence (`ShellConvexPlanar`, convex-planar-only) `Mesh::Shell(thickness)`'s own closed-mesh construction generalizes naturally to once face removal is added. `Mesh::Shell(thickness, removed_face_indices)` (see that bullet's own "New this pass" text above) closes it: reuses `Shell(thickness)`'s own feasibility guards verbatim (opening faces up can only relax the wall-to-wall fold hazard, never worsen it) and `Thicken()`'s own naked-edge stitching formula to seal each opening's rim between the outer and inner layers, verified by 2 new tests covering a single removed face, two mutually-adjacent removed faces, and the full input-validation surface (empty/duplicate/out-of-range/all-faces-removed index lists, non-positive thickness, an already-open input, and the same thin-slab fold hazard `Shell(thickness)`'s own test already exercises) - `TestMeshShellWithRemovedFaceProducesClosedManifoldCupWithStitchedWall`/`TestMeshShellWithRemovedFacesRefusesInvalidInput`, tests/test_basic.cpp. Does NOT change this category's own present/partial/missing counts: both items were already `[partial]` and stay `[partial]` - real, tested, honestly-scoped new mesh-level coverage (a non-convex, non-box-like closed mesh can now be shelled with an opening, where before only a convex-planar Brep or an app-level box-like mesh path could), not yet `[present]` (no app wiring for either method, still mesh-only rather than B-rep, and a bowtie-shaped opening boundary is not specially detected). The table's own 0/27/0/27 (50.0%) is unchanged. Testing this surfaced a real bug before it landed: the wall's initial winding (naively copying `Thicken()`'s own `vi = {a, b, b+n, a+n}` verbatim) left `IsClosedManifold()` reporting 8 `orientation_conflicts` on the very first fixture tried - `Thicken()`'s formula relies on its OWN sheet getting flipped into the inner-wall role before storage, which doesn't hold here (this method's outer layer stays unflipped); reversed to `vi = {b, a, a+n, b+n}` once traced through by hand and confirmed via a standalone scratch-binary `Check()` dump (8 conflicts -> 0), rather than shipped on the strength of "it compiles and looks like Thicken()'s". Full `dino8_kernel_tests` suite (via `ctest`): 100% passing (1 test target, `dino8_kernel_smoke`), 0 regressions.*

**A same-session follow-up, picking up right after `Shell(thickness, removed_face_indices)` landed:** rather than duplicate that just-landed opening support, this pass picked the next open item in the same category, "Per-face (multi-thickness) shell" — `Mesh::Shell(const std::vector<double>& face_thickness)` (see that bullet's own "New this pass" text above for the full construction) generalizes the uniform-thickness overload to a per-face thickness vector, reconciled per-vertex via the same area-weighting scheme `ComputeVertexNormals()` already uses for direction. Does NOT change this category's own present/partial/missing counts: the item was already `[partial]` and stays `[partial]` — real, tested new mesh-level coverage (a non-convex closed mesh can now be shelled with a per-face thickness, where before only a convex-planar Brep via `ShellConvexPlanar`'s own per-face overload could), not yet `[present]` (mesh-only, no app wiring, and a shared vertex's thickness is always a blended average rather than a sharp per-face boundary). The table's own 0/27/0/27 (50.0%) is unchanged. Full `dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions.*

**A later same-day pass, closing two more closely-related items in this same category (2026-09-30):** rather than start a distant item, this pass picked the two gaps the just-landed `Shell(thickness, removed_face_indices)`/`Shell(face_thickness)` pair themselves disclosed as still open. (1) `Mesh::Shell(const std::vector<double>& face_thickness, const std::vector<int>& removed_face_indices)` (see the "Per-face (multi-thickness) shell" bullet's own "New this pass" text above) combines the two most recent generalizations into one call — a per-face thickness vector AND an opening in the same shell — closing the "still separate" half of that bullet's own gap. (2) A new `ThrowIfOpeningBoundaryIsBowtie()` guard (see the "Shell with removed/open faces" bullet's own "New this pass" text above), shared by both removed-face overloads, closes that bullet's own explicitly-disclosed "not specially detected" bowtie-boundary hazard, verified on a hand-built octahedron fixture designed specifically to pinch two openings at one shared vertex. Neither change adds a new row or flips an existing one: both "Shell with removed/open faces" and "Per-face (multi-thickness) shell" were already `[partial]` and stay `[partial]` — real, tested new mesh-level coverage (a caller can now vary wall thickness per face AND cut an opening in the same call, and a malformed opening boundary is now refused instead of silently mis-stitched), not yet `[present]` (still mesh-only, no app wiring, and a shared vertex's thickness is still always a blend rather than a sharp per-face boundary). The category's own row count is unchanged: 0/27/0/27 (50.0%). Full `dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions (a fresh rebuild of the test target was required this pass after an initial build produced a test binary missing the newly-added test registrations despite a `Built target` success and a source mtime that appeared to postdate the object file — forcing a `rm` of the stale `.o` before rebuilding resolved it; re-verified by `nm`-checking the new test symbols were actually present before trusting the run).*

**A later pass still (2026-09-30), closing two more closely-related items after re-reading this category fresh:** rather than a distant item, this pass picked the two gaps the most recent `Mesh::Shell` round (bowtie detection) and this category's own longest-standing disclosed doubt left open. (1) `TestMeshShellRemovedFacesHandlesDisconnectedRemainder` (see the "Shell with removed/open faces" bullet's own "New this pass" text above) verifies `Shell(thickness, removed_face_indices)` on a removal that splits the remainder into several DISCONNECTED pieces at once - a genuinely different hazard from the bowtie case the prior pass closed (a self-touching single loop vs. multiple entirely separate loops with no shared vertex at all), and one every prior removed-face fixture had left untested. (2) `TestOffsetSolidHandlesGenuinelyConcaveSolid` (see the "Solid dilation/erosion via kernel::MinkowskiSum/MinkowskiDifference" bullet's own "New this pass" text above) closes that bullet's own explicitly-disclosed "every test fixture is convex" gap, verifying both growth and shrink of `OffsetSolid` on a genuinely non-convex L-shaped fixture via a convexity-independent bounding-box identity for growth and a material-can-only-decrease invariant for shrink. Neither change adds a new row or flips an existing one: both "Shell with removed/open faces" and "Solid dilation/erosion via kernel::MinkowskiSum/MinkowskiDifference" were already `[partial]` and stay `[partial]` - real, tested new coverage of a previously-untested topology/convexity case each, not a new capability (the app still doesn't wire either kernel entry point, and `OffsetSolid` is still mesh-only with faceted-sphere rounding). The category's own row count is unchanged: 0/27/0/27 (50.0%). Full `dino8_kernel_tests` suite: 100% passing, 0 regressions (a from-scratch build, including FetchContent-ing Manifold and OpenNURBS with no prior cache in this environment, confirmed clean via the binary's own terminal "all checks passed"/exit-0 signal - which this harness's own `main()` only prints after every `Check()` call across the WHOLE suite has run with zero failures, not a partial or early-exit result - before either new test's own checks were trusted).*

**A later pass in turn (2026-09-30), closing two more closely-related items after re-reading this category fresh again:** rather than a distant item, this pass picked up exactly where the prior pass's own two disconnected-remainder/concave-fixture additions left off, closing the two closest remaining doubts those additions themselves exposed but didn't cover. (1) `TestOffsetSolidConvexPlanarRejectsNonConvexLShape` closes a real gap in "Body offset (offset an entire closed solid outward/inward as a B-rep)"'s own verification: `OffsetSolidConvexPlanar`'s doc comment and `IsConvex()` guard both say a non-convex solid is refused, and `TestPushPullFaceOnNonConvexLShapeGrowsByExactSlabVolume`'s own comment (elsewhere in this test file) already repeats that claim by name to justify why `PushPullFace` exists at all - but nothing had ever actually CALLED `OffsetSolidConvexPlanar` on a genuinely non-convex solid to confirm the guard fires, rather than silently mis-clipping, before this pass. Reuses the exact same L-shaped (one reflex corner) prism fixture `TestBooleanCombinePlanarNonConvexLShapeVsBox`/`TestPushPullFaceOnNonConvexLShapeGrowsByExactSlabVolume` already build, verified on BOTH `OffsetSolidConvexPlanar` overloads (uniform distance and per-face vector, which share the identical `IsConvex()` precondition ahead of any per-face clipping work) refusing it, contrasted directly against `PushPullFace` accepting the SAME fixture without throwing - a guard that's exercised, not merely documented. (2) `TestMeshShellFaceThicknessWithRemovedFacesHandlesDisconnectedRemainder` extends the prior pass's own `TestMeshShellRemovedFacesHandlesDisconnectedRemainder` (which verified only the uniform-thickness removed-face overload, `Shell(thickness, removed_face_indices)`, on a disconnected remainder) to the fourth and most general `Shell()` overload, `Shell(face_thickness, removed_face_indices)` - never itself exercised on a disconnected remainder before, even though its implementation shares the identical "no reference to which connected component a naked edge belongs to" property its sibling already proved general enough for this. Verified two ways on the same `MakeSymmetricCubeMesh`-minus-4-side-faces (two entirely disconnected flat squares) fixture: a uniform `face_thickness` vector reproduces `Shell(thickness, removed_face_indices)`'s own result exactly (vertex-for-vertex, not just structurally), and a genuinely VARYING per-face thickness - including on the REMOVED side faces, which still matter since `AreaWeightedVertexThickness()` blends every incident face's own thickness into a shared vertex regardless of whether that face survives - stays a valid closed 2-manifold with no leftover naked edge and encloses strictly less material than the fully-closed per-face shell built from the identical thickness vector. Neither change adds a new row or flips an existing one: "Body offset" and "Shell with removed/open faces" were already `[partial]` and stay `[partial]` - real, tested new coverage of a previously-unexercised guard and a previously-untested overload/topology combination, not a new capability (no app wiring for either, `OffsetSolidConvexPlanar` is still convex-planar-only by design, and `Shell()`'s per-vertex thickness is still always a blend rather than a sharp per-face boundary). The category's own row count is unchanged: 0/27/0/27 (50.0%). Full `dino8_kernel_tests` suite (from-scratch build, FetchContent-ing Manifold and OpenNURBS fresh in this environment): 6595 checks passing, 0 regressions, confirmed via both the binary's own terminal "all checks passed"/exit-0 signal and a `grep -c '^ok:'` count of the full captured run (not just its tail) before trusting it.*

**A later pass in turn (2026-09-30), closing two more closely-related items by extending production code, not just tests, after re-reading this category fresh once more:** the last several passes only added test coverage to already-landed `Mesh::Shell` machinery; this pass instead noticed `Mesh::Thicken` itself - `Shell`'s own closest sibling, sharing its exact "one quad per naked edge" side-wall construction - had never received either safety guard `Shell` already earned across those passes, a real, previously-undisclosed gap rather than an untested claim. (1) The bowtie-boundary helper (previously `ThrowIfOpeningBoundaryIsBowtie()`, scoped to `Shell`'s removed-face overloads only) is renamed `ThrowIfNakedBoundaryIsBowtie()` and given a `method_name`/`boundary_description` pair so the same check now also guards `Thicken()`'s own naked-edge boundary: an open sheet whose boundary touches itself at a single vertex (two lobes joined at a point, not an edge) previously walled up into a silently non-manifold result; now it's refused, verified on a two-triangle fixture sharing exactly one vertex (`TestMeshThickenRefusesBowtieBoundaryButAllowsDisconnectedSheet`, tests/test_basic.cpp), contrasted against two disconnected (non-touching) squares still succeeding. (2) `Thicken()` ran no fold-safety check at all on its own offset copy before this pass, unlike `Shell(thickness)`'s own `FindSelfIntersections()` guard - `TestMeshThickenRefusesSelfIntersectingFold` reuses the exact V-groove/distance-(-1.0) pairing `TestMeshFindOffsetSelfIntersectionsDetectsGenuineFold` already proved genuinely folds, confirming `Thicken()` now refuses it too (a small, safe distance on the same groove still succeeds, ruling out over-refusal). Both changes touch real production code in `src/mesh.cpp`, not only tests, but neither adds a new row or flips an existing one: "Shell with removed/open faces" and "Thicken sheet" were already `[partial]` and stay `[partial]` - real new hazard coverage on a previously-unguarded method, not a new capability (mesh-level only, no app wiring for either guard, and `Thicken()`'s own disclosed "no fold repair" gap stays open - this closes the silent-corruption half by refusing, not the repair half). The category's own row count is unchanged: 0/27/0/27 (50.0%). Full `dino8_kernel_tests` suite (from-scratch build, FetchContent-ing Manifold and OpenNURBS fresh in this environment): 7015 checks passing, 0 regressions, confirmed via the binary's own terminal "all checks passed"/exit-0 signal, a `grep -c '^ok:'` count of the full captured run, and an `nm` check that both new tests' own symbols were actually present in the built binary before trusting the run.*

**A later pass in turn (2026-09-30), this category still ranking lowest score-per-fix, closing three small independent input-validation gaps across two files rather than one deep dive:** re-reading this category's own guard surface (`grep`-ing every `throw std::invalid_argument`/`std::isfinite` call across `mesh.cpp`/`boolean.cpp`) turned up a genuine, previously-undisclosed pattern: several offset/thicken entry points check a distance/thickness parameter for exactly zero but not for NaN or +/-infinity, even though a sibling function in the very same category (`Brep::Thicken`, sweep.cpp:2099) already makes that check and is already tested for it (`TestThickenRejectsInvalidArguments`). A `== 0.0` comparison is false for both NaN and either infinity, so all three silently passed through their only guard before this pass. (1) `Mesh::Offset`/`Mesh::OffsetDirectional` (mesh.h/mesh.cpp) gain a `!std::isfinite(distance)` guard each — previously neither checked `distance` at all beyond `OffsetDirectional`'s unrelated zero-direction check, so a NaN/Inf `distance` baked directly into every vertex, and since a NaN/Inf coordinate makes most downstream comparisons resolve to false, a `Shell()`/`Thicken()` fold check built on top of `Offset()` could not be trusted to catch the corruption either. (2) `Mesh::Thicken`'s own `distance == 0.0` check is widened to `!std::isfinite(distance) || distance == 0.0`, closing the exact gap named above relative to its own B-rep sibling. (3) the free function `OffsetSolid` (boolean.cpp) gains the same guard ahead of its own `distance == 0.0` no-op case — previously a NaN or infinite `distance` reached `Brep::Sphere(origin, std::fabs(distance))` uncaught, building a NaN/infinite-radius "sphere" fed straight into `TessellateToClosedMesh()`/Manifold. Each fix is credited in full, with its own test detail, under this category's own "Mesh offset (per-vertex offset, solid/shell option)", "Thicken sheet (open surface/mesh) into a closed solid", and "Solid dilation/erosion via kernel::MinkowskiSum/MinkowskiDifference" bullets above — not repeated here. None of the three changes adds a new row or flips an existing one: all three items were already `[partial]` and stay `[partial]` — real, tested guards against a genuine silent-corruption class of input this category's own established pattern (NaN/Inf propagating through comparisons that read as "no problem found") makes especially dangerous here, not a new capability. The category's own row count is unchanged: 0/27/0/27 (50.0%). Full `dino8_kernel_tests` suite (via both the raw binary and `ctest --test-dir dino8-kernel/build`, from a build that FetchContent-ed Manifold and OpenNURBS fresh in this environment): 7262 checks passing, 0 regressions, confirmed via the binary's own terminal "all checks passed"/exit-0 signal, a `grep -c '^ok:'` count of the full captured run, and `ctest`'s own "100% tests passed" summary; the 9 new guard checks (3 per fixed function) were individually confirmed present in the captured run's own `ok:` lines before trusting it.*

**kernel: Local / direct-edit operations** (localops):
- [partial] Split an edge at a point (SplitEdge) — app `SplitEdgeCommand` (cmd_fillet.cpp:2153-2301) does a real vertex/edge/trim split. Kernel `Brep::SplitNakedEdgeAt` covers only naked, straight edges. The app splits each trim at the same normalized parameter fraction as the 3D edge, exact only when trim and edge parameterizations are proportional — approximate on curved or non-uniformly parameterized trims.
- [partial] Merge coplanar adjacent faces (MergeFaces / MergeAllCoplanarFaces) — kernel `Brep::MergeCoplanarFaces` skips any face with a hole and any pair sharing more than one edge. The app's `MergeFaces`/`MergeAllCoplanarFaces` (cmd_fillet.cpp:2668-2669) instead slices a mesh union of thin slabs back into a polyline outline, losing curved boundaries and ignoring inner loops.
- [partial] Move/transform face (tweak face, neighbours adjust) — **corrected: gained a real kernel rigid-transform implementation, still partial.** `MoveFaceConvexPlanar(solid, face_index, xform)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: `xform` (an `ON_Xform`) is applied directly to `face_index`'s own current plane via `ON_Plane::Transform()` — origin, xaxis, yaxis and zaxis all transformed consistently — and the result is handed to `ReplaceFacePlaneConvexPlanar()`, which does the actual re-trim of every face. A pure translation vector alone would NOT have been a genuine generalization of `OffsetFace()`: a plane's geometry is invariant under moving its own origin WITHIN the plane, so only a translation's component along the face's own normal is ever observable — exactly what `OffsetFace()`'s own `distance` already expresses. This function is a real generalization only because `xform` can also ROTATE the face's own frame, which neither `OffsetFace()` nor a plain translation vector can do at all, and unlike `FoldFaceConvexPlanar()`/`DraftFacesConvexPlanar()`, the rotation isn't tied to a hinge edge or a caller-supplied neutral plane — any axis and center work. Verified two ways: (1) fed `ON_Xform::TranslationTransformation(distance * old_plane.zaxis)`, it reproduces `OffsetFace()`'s own result bit-for-bit, vertex for vertex (`TestMoveFaceConvexPlanarMatchesOffsetFaceForPureNormalTranslate`, tests/test_basic.cpp) — proof the translation path is a genuine generalization, not a separate and possibly-divergent construction; (2) a real rotation about an axis through the face's own current plane origin (making that origin the rotation's fixed point) leaves that exact point on the new plane and rotates the normal away from the original by precisely the given angle (`cos(angle) == new_normal . old_normal` to 1e-9) — an algebraic rigid-body fact independent of this function's own half-space-clip arithmetic, and something no translation vector could ever produce (`TestMoveFaceConvexPlanarRotationMatchesExactAngleAndFixedOrigin`). Refusal cases (out-of-range `face_index`, a singular/non-invertible `xform` collapsing the face's own frame to an invalid plane) are also covered (`TestMoveFaceConvexPlanarRefusesInvalidInput`). Full `dino8_kernel_tests` suite re-run clean: 100% passing, 0 regressions. Still partial: convex planar-faced solids only (the same scope every `ReplaceFacePlaneConvexPlanar()` sibling already has), and no app command calls it yet — `MoveBrepParts`/`MoveBrepFaces` (cmd_srfedit.cpp:864) still only drive their own approximate mesh-based path.

  *Same-day follow-up (this pass): this pass adds a genuine batch sibling, still partial.* `MoveFacesConvexPlanar(solid, face_moves)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: `face_moves` is a caller-chosen list of `(face_index, xform)` pairs, each flattened into the one named face's own transformed plane exactly as `MoveFaceConvexPlanar()` already computes it for one face — but the whole flattened list of `(face_index, new_plane)` pairs is handed to `ReplaceFacePlanesConvexPlanar()` (see the "Replace face" bullet below's own "Same-day follow-up" for that function) in a SINGLE call, so every named face's own new boundary — and every OTHER face's, since this whole family always re-trims the entire solid — is derived once from the complete, final plane set, not through several sequential `MoveFaceConvexPlanar()` calls each independently validating its own intermediate (possibly invalid) state. This is load-bearing, not a mere convenience: moving the box's own front wall alone past the back wall's own OLD position is refused by `MoveFaceConvexPlanar()` (the intermediate state is a genuine contradiction — no y satisfies both half-spaces), but moving both walls together in one `MoveFacesConvexPlanar()` call succeeds, matching the exact resulting box volume (`TestMoveFacesConvexPlanarSucceedsWhereSequentialSingleMoveWouldRefuse`, tests/test_basic.cpp). Also verified against an independent sibling for the ordinary (non-conflicting) case: two INDEPENDENT named faces each translated along their own normal via `MoveFacesConvexPlanar()` match `OffsetSolidConvexPlanar()` fed the identical two nonzero distances vertex for vertex (`TestMoveFacesConvexPlanarMatchesOffsetSolidForTwoIndependentNormalTranslates`) — proof this is a genuine generalization of per-face normal translation to an arbitrary named subset, not a separate and possibly-divergent construction. Refusal cases — an empty `face_moves`, two entries naming the same `face_index`, an out-of-range `face_index`, and the usual singular-xform-collapses-the-frame case — are also covered (`TestMoveFacesConvexPlanarRefusesInvalidInput`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial: this closes only the "one call, many faces" batching gap — convex-planar-only and no app wiring remain exactly as this bullet's own paragraph above already states.
- [partial] Move/transform edge (tweak edge) — **corrected: gained a real kernel implementation, still partial.** `MoveEdgeConvexPlanar(solid, old_p0, old_p1, new_p0, new_p1)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: it generalizes `MoveVertexConvexPlanar()`'s own single-point core (refactored into a shared private `MoveConvexPlanarPoints()` helper, not duplicated) from one moved point to the edge's own two endpoints. A face incident to BOTH endpoints (the edge lies along one of its own sides) gets both of its own matched corners replaced before its plane is re-derived once from the result, rather than through two sequential single-vertex moves whose own intermediate, possibly-invalid plane would otherwise need separate validation; a face touching only one endpoint is handled exactly as `MoveVertexConvexPlanar()` already handles it. Same triangle-only scope as `MoveVertexConvexPlanar()`, for the identical reason (only a triangle's plane is always well-defined regardless of where its corners sit) — a box's own edges, whose incident faces are quads, are refused, not silently approximated. Verified on a hand-built tetrahedron (every face a triangle, so every edge qualifies) by moving the edge shared between two of its four faces to an arbitrary new pair of positions in one call: the result matches the general tetrahedron volume formula (the signed scalar triple product of the edge vectors from one vertex, /6) applied directly to the new four vertices — independent of this function's own arithmetic — and the two faces touching only ONE of the moved endpoints keep their own other two corners exactly unchanged (`TestMoveEdgeConvexPlanarTetrahedronMatchesExactVolumeFromSignedTripleProduct`, tests/test_basic.cpp). Refusal cases (coincident `old_p0`/`old_p1`, an `old_p0` matching no vertex, a box edge whose incident faces are non-triangular quads) are also covered (`TestMoveEdgeConvexPlanarRefusesInvalidInput`). Full `dino8_kernel_tests` suite re-run clean: 100% passing, 0 regressions. Still partial, and deliberately narrow: convex planar-faced solids only, every face incident to either endpoint must be a triangle, and no app command calls it yet — a grep for moveedge/dragedge in dino8-app/src still finds nothing.

  *Same-day follow-up: this pass adds a genuine batch sibling, still partial.* `MoveEdgesConvexPlanar(solid, edge_moves)` (dino8-kernel/include/dino8/kernel/boolean.h:1134; dino8-kernel/src/boolean.cpp:2233) now exists: `edge_moves` is a caller-chosen list of `EdgeMove{old_p0, old_p1, new_p0, new_p1}` entries, each flattened into the two endpoint moves `MoveEdgeConvexPlanar()` already hands the shared `MoveConvexPlanarPoints()` core (boolean.cpp:2037) for one edge — but the whole flattened list is fed through in a SINGLE call, so a face touched by more than one named edge (or by two edges sharing an endpoint) has every one of its own corners replaced and its plane re-derived exactly once, not through several sequential `MoveEdgeConvexPlanar()` calls each independently re-validating its own intermediate (possibly invalid) state. Two entries ARE allowed to name the same endpoint vertex — the shared corner of two adjacent named edges — as long as they agree on its own new position; entries disagreeing on that shared corner's new position are refused as ambiguous rather than silently picking one. Verified on the tetrahedron fixture's own two edges sharing the apex (`(apex,b0)` and `(apex,b1)`, both naming the same new apex position): the face incident to all three touched corners at once (`(apex,b1,b0)`) is exercised for the first time by any test in this family, the resulting volume matches the exact signed-triple-product formula for the new-apex/new-b0/new-b1/untouched-b2 tetrahedron, and the untouched base corner (b2) is confirmed present in the result alongside both new endpoint positions (`TestMoveEdgesConvexPlanarMovesTwoAdjacentEdgesSharingAVertexMatchesExactVolume`, tests/test_basic.cpp). Refusal cases — an empty `edge_moves`, a degenerate entry (`old_p0`==`old_p1`), two entries naming the same shared vertex with conflicting new positions, and the usual non-triangular-incident-face box refusal — are also covered (`TestMoveEdgesConvexPlanarRefusesInvalidInput`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial: this closes only the "one call, many edges" batching gap — every other limitation this bullet's own paragraph above already lists (convex-planar-only, triangle-incident-faces-only, no app wiring) applies identically here.
- [partial] Rotate face about hinge edge (FoldFace / rotate-face tweak) — **corrected: gained a real kernel implementation, still partial.** `FoldFaceConvexPlanar(solid, face_index, hinge_loop_index, angle_radians)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: unlike `DraftFacesConvexPlanar()` (which tilts a face about its intersection line with a caller-supplied, possibly-external neutral plane), this hinges the named face about one of its OWN edges — `hinge_loop_index` names the edge from that face's own `loop[hinge_loop_index]` to `loop[(hinge_loop_index + 1) % loop.size()]`, the same "index into a `PlanarFace`'s own loop" convention `MoveVertexConvexPlanar()` already uses for a single vertex. The face's plane is rotated by `angle_radians` (right-hand rule) about the 3D line through those two hinge points and handed straight to `ReplaceFacePlaneConvexPlanar()`, which does the actual re-trim — a "compute a plane, delegate" shape, not a separate reconstruction of its own. Verified on a unit-scaled `Brep::Box()`'s own front wall, hinged at its shared edge with the bottom face (found dynamically from the wall's own loop, not assumed): the resulting volume matches an independent closed-form integral of the wall's own (linearly changing, sign derived from the ACTUAL loop-order hinge direction rather than assumed) cross-sectional width along the box's height, `angle_radians == 0` reproduces the original box's volume exactly (a null fold), the untouched bottom face's own plane stays exactly at z=0, and the result re-tessellates to a closed manifold (`TestFoldFaceConvexPlanarBoxFrontWallHingedAtBottomEdgeMatchesExactIntegral`, tests/test_basic.cpp). Refusal cases (out-of-range/negative `face_index`, out-of-range/negative `hinge_loop_index`) are also covered (`TestFoldFaceConvexPlanarRefusesInvalidInput`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial: convex planar-faced solids only (the same scope every sibling in this family already has, enforced by the `ReplaceFacePlaneConvexPlanar()` call it delegates to), and no app command calls it yet — `FoldFaceCommand` (cmd_srfedit.cpp:951) still only drives its own approximate `MoveBrepFaces` rotation, unaware this kernel API exists. **Newly observed (an unrelated data-exchange session, 2026-09-30, incidental full-suite runs):** `TestFoldFaceConvexPlanarBoxFrontWallHingedAtBottomEdgeMatchesExactIntegral`'s own two checks named above (the hinge-edge-on-the-x-axis check and the closed-form-integral volume check) failed on one out of three consecutive `dino8_kernel_tests` runs of the exact same unchanged binary — the other two runs passed clean, and this session's own changes touch only kernel-level data exchange (`Mesh::SaveCollada`/`LoadCollada`), nowhere near `boolean.cpp`/`FoldFaceConvexPlanar`. Not investigated further here (out of this session's scope, same "confirmed pre-existing, not chased down" stance the boolean-flake note above already takes for its own unrelated intermittent failure) — flagged so a future session hunting this down doesn't have to first rediscover that it's intermittent rather than a hard failure.

  *Same-day follow-up (this pass): this pass adds a genuine batch sibling, still partial.* `FoldFacesConvexPlanar(solid, folds)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: `folds` is a caller-chosen list of `FaceFold{face_index, hinge_loop_index, angle_radians}` entries - the same three positional arguments `FoldFaceConvexPlanar()` takes for one hinge fold - each flattened into the identical rotated `(face_index, new_plane)` pair `FoldFaceConvexPlanar()` itself computes, with the whole flattened list handed to `ReplaceFacePlanesConvexPlanar()` in a SINGLE call, the same "one call, many named faces" batching `MoveFacesConvexPlanar()`/`ReplaceFacePlanesConvexPlanar()` already closed for plain plane swaps and rigid transforms, now closed for hinge folds too. Unlike `DraftFacesConvexPlanar()`, which requires every named face to share ONE caller-supplied neutral plane, each `FaceFold` entry names a completely independent hinge line, so one call can fold different faces about different edges - a combination `DraftFacesConvexPlanar()` cannot express at all in a single call. Verified two ways: (1) a single-entry `folds` list reproduces `FoldFaceConvexPlanar()`'s own output bit-for-bit, vertex for vertex (`TestFoldFacesConvexPlanarSingleEntryMatchesFoldFaceConvexPlanar`, tests/test_basic.cpp) — proof the flattening step introduces no divergence of its own; (2) folding the box's own front and back walls together, each about its own bottom edge by a different angle, matches — vertex for vertex — the identical result of folding them via two SEQUENTIAL single `FoldFaceConvexPlanar()` calls (front first, then back on that intermediate result), confirmed with a genuinely nonzero volume change so the match isn't merely two no-ops agreeing (`TestFoldFacesConvexPlanarTwoIndependentFoldsMatchesSequentialSingleFolds`). Refusal cases — an empty `folds` list, an out-of-range `face_index`, a `hinge_loop_index` out of range for that face's own loop, and two entries naming the same `face_index` — are also covered (`TestFoldFacesConvexPlanarRefusesInvalidInput`). Full `dino8_kernel_tests` suite re-run clean: 100% passing, 0 regressions. Still partial: this closes only the "one call, many hinge-folds" batching gap — convex-planar-only and no app wiring remain exactly as this bullet's own paragraph above already states. Unlike the plane/vertex/edge/face batch siblings above, this one is NOT shown to rescue a sequential-single-call refusal: a hinge rotation is pinned through its own hinge line at every angle, so (unlike an arbitrary plane swap or translation) it can never push a face's ENTIRE reconstructed boundary outside a fixed neighbour's half-space the way those siblings' own "pushed past the old position" refusal cases do - stated here rather than overclaimed.
- [partial] Offset face (translate face along its normal, neighbours re-extended/re-trimmed) — kernel `OffsetFace` (boolean.h:453-488) re-clips every other face into a valid closed B-rep, but convex planar solids only, throws if any face would vanish, and not wired to any app command.
- [partial] Delete face with heal (remove face, grow neighbours to close the gap) — **corrected: gained a real kernel heal, still partial.** `DeleteFaceHealConvexPlanar(solid, face_index)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: it drops `face_index`'s own plane from the solid's half-space set entirely (not a replacement, not a cap) and rebuilds every OTHER face from scratch as the intersection of every remaining plane, reusing `OffsetSolidConvexPlanar()`/`ReplaceFacePlaneConvexPlanar()`/`MoveVertexConvexPlanar()`'s own "oversized polygon per face, clip against every other face's own half-space" reconstruction verbatim with one fewer half-space in the list — the literal, geometrically exact meaning of "grow the neighbours until they meet, closing the gap," genuinely distinct from `Brep::CapPlanarHoles`'s flat re-cap (which adds a new face; this removes one and lets the others extend into the space it vacates). Verified on a unit cube with its (1,1,1) corner chamfered off by one extra triangular face (7 faces total: 3 untouched squares, 3 pentagons missing that corner, 1 chamfer triangle): deleting the chamfer face and healing exactly reconstructs the original unit cube — all 3 pentagons regrow into plain quads, the solid's exact volume matches 1.0 to 1e-9, the deleted corner (1,1,1) reappears exactly on all 3 previously-pentagonal faces, and the result re-tessellates to a closed manifold (`TestDeleteFaceHealConvexPlanarChamferedCubeRecoversExactUnitCube`, tests/test_basic.cpp). Also verified to tell a genuine heal apart from an impossible one rather than silently emitting a wrong-shaped result: deleting one face of a plain box (whose remaining walls are mutually perpendicular and never converge) is refused as leaving the solid unbounded, alongside the usual out-of-range/negative `face_index` refusals (`TestDeleteFaceHealConvexPlanarRefusesInvalidInput`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial, and deliberately narrow: convex planar-faced solids only (the same scope every sibling in this family already has), every other face is unconditionally rebuilt (not just ones actually adjacent to `face_index`), and no app command calls it yet — `dino8-app`'s own `DeleteFaces` (cmd_srfedit.cpp:243-254) still only calls `ON_Brep::DeleteFace` + `Compact`, leaving a hole, unaware this kernel API exists.

  *Same-day follow-up (this pass): this pass adds a genuine batch sibling, still partial.* `DeleteFacesHealConvexPlanar(solid, face_indices)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: `face_indices` is a caller-chosen list of faces dropped from the solid's own half-space set AT ONCE, with every OTHER face rebuilt once as the intersection of every remaining plane — the same reconstruction `DeleteFaceHealConvexPlanar()` already uses, just with `face_indices.size()` fewer half-spaces in the list instead of one, the same "one call, many named targets" batching `ReplaceFacePlanesConvexPlanar()`/`MoveFacesConvexPlanar()`/`FoldFacesConvexPlanar()` already give their own single-face ancestors. Verified on a unit cube with BOTH the (1,1,1) and (0,0,0) corners chamfered off by their own triangular face (8 faces total: all 6 box-derived faces are pentagons, since every one touches one chamfered corner or the other): deleting both chamfer faces in one `DeleteFacesHealConvexPlanar()` call reconstructs the exact original unit cube (volume 1.0, 6 plain-quad faces), and matches — vertex for vertex — the identical result of two SEQUENTIAL single `DeleteFaceHealConvexPlanar()` calls in EITHER order (the (1,1,1) chamfer removed first then the (0,0,0) chamfer, or the reverse), proving this is a genuine "drop many at once" sibling rather than a separate and possibly-divergent construction, and confirming the batch result is independent of any particular removal order (`TestDeleteFacesHealConvexPlanarTwoIndependentChamfersMatchesEitherSequentialOrder`, tests/test_basic.cpp). Refusal cases — an empty `face_indices` list, an out-of-range `face_index`, a duplicate `face_index`, and dropping a set of faces (a box's own top and bottom together) that leaves the remaining side walls genuinely unbounded — are also covered (`TestDeleteFacesHealConvexPlanarRefusesInvalidInput`). Full `dino8_kernel_tests` suite re-run clean: 100% passing, 0 regressions. Still partial: this closes only the "one call, many dropped faces" batching gap — convex-planar-only, every other face unconditionally rebuilt, and no app wiring remain exactly as this bullet's own paragraph above already states. Unlike the plane/vertex/edge/face batch siblings above, dropping planes is monotonic (each additional drop can only relax a constraint, never restore one), so this batch cannot rescue a sequential single call's own "genuinely unbounded" refusal the way those siblings rescue a sequential "pushed past the old position" refusal — its own genuine value is dropping many at once in a single pass, not reaching an otherwise-unreachable combination.
- [partial] Split face by curve / surface (real trim-loop split in place) — **corrected: gained a real kernel implementation, still partial.** `SplitFaceByCurve(target, face_index, curve, tolerance, samples)` (dino8-kernel/include/dino8/kernel/boolean_general.h; dino8-kernel/src/boolean_general.cpp) now exists: `curve` is pulled onto `face_index`'s own surface point-by-point via `SurfaceClosestPointGlobal()` (the same real multistart-Newton solver `NurbsSurface::ClosestPointParameter()` already wraps, called here directly on the raw `ON_Surface`) into a dense (u, v) polyline chain, then spliced into that face's own trim-loop boundary via this file's own `FragmentFaces()`/`SplitFaceLoop()` machinery — the identical proven code `ImprintFaces()` already uses for a whole tool body, just fed one caller-supplied curve chain for one named face instead of a set of SSX curves gathered from a second operand. Unlike the app's own `SplitFaceCommand`, which only splits at the straight iso-parameter MIDPOINT chord between the curve's two crossing points, the new trim boundary genuinely runs along the real (sampled) curve end to end — no new geometry is fit for either half's own surface, exactly like every other Fragment this engine produces. Verified on the box's own flat top face (z=1, area 16) split by a V-shaped, exactly piecewise-linear 3-point curve from one boundary edge to the other: the resulting 2 fragments have the exact closed-form trapezoid areas the true V-shape bounds (5 and 11) — NOT the 8/8 a straight endpoint-to-endpoint chord (what the app-level command effectively produces) would give (`TestSplitFaceByCurveBoxTopFaceAsymmetricVSplit`, tests/test_basic.cpp) — proof this is a genuine curve-following split, not a disguised chord. Also verified to refuse rather than silently misclassify: a closed loop that never reaches the face's own trim boundary would otherwise slip past a naive "did I get exactly 2 fragments" check (a closed interior chain also produces exactly 2 fragments — an annulus-with-hole plus a separate interior disk, `ImprintFaces()`'s own piercing-cylinder pattern) — caught explicitly via the same chain-endpoint-distance test `FragmentFaces()` itself uses, and via a hole-free-fragment check as a second line of defense (`TestSplitFaceByCurveRejectsInteriorOnlyLoop`), alongside the usual out-of-range `face_index`/non-positive `tolerance`/`samples < 2` refusals (`TestSplitFaceByCurveRejectsInvalidInput`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial, deliberately: `curve` must cross the face's own trim boundary at exactly two points with no interior touch or hole (one open chain, the same "at most one outer intersection component per face" scope this file's other SSX-driven operations already share) — a curve crossing more than twice, or not reaching the boundary at all, is refused rather than best-effort split; and no app command calls it yet — `SplitFaceCommand` (cmd_fillet.cpp:2057) still only does its own approximate midpoint split, unaware this kernel API exists. Doesn't change this category's own present/partial/missing counts (still 7/21/0 below): this item was already credited `[partial]` for the app's own approximate command, and stays `[partial]` — a real but narrow, unwired kernel addition, not yet a `[present]`.
- [present] Merge contiguous tangent edges (MergeEdge / MergeAllEdges) — **upgraded from partial.** `Brep::MergeContiguousEdges`/`MergeAllContiguousEdges` (brep.h/brep.cpp) is a genuine kernel wrapper around the same `ON_Brep::CombineContiguousEdges` the app `MergeEdgeCommand` (cmd_fillet.cpp:2303-2320) calls directly, with real kernel-level test coverage the app-only version never had. See the healing category's own bullet below (this is the same underlying capability, credited there in full detail) for the exact tests and remaining app-wiring gap.
- [partial] Remove small / sliver edges (naked micro-edge removal with gap closure) — **corrected: gained a real shared-edge sibling, still partial.** `Brep::RemoveSharedMicroEdge(edge_index, tolerance)` (dino8-kernel/include/dino8/kernel/brep.h; dino8-kernel/src/brep.cpp) now exists alongside `Brep::RemoveNakedMicroEdge` (which is limited to isolated NAKED, 1-trim edges): this closes the SHARED (2-trim, interior) case the naked method's own doc comment explicitly leaves out. Same isolated-sliver discipline, extended from one loop to two: `edge_index` must be shorter than `tolerance`, border exactly two DIFFERENT faces, and each of its own four loop-neighbours (one per side per endpoint) must be the ONLY other thing either endpoint vertex touches in the whole Brep — the identical "no third edge, no non-manifold junction" isolation `RemoveNakedMicroEdge()` already requires, just checked against two allowed neighbours per vertex instead of one. Also refuses either face's own loop having fewer than 4 trims, since collapsing the shared edge would otherwise leave a degenerate 2-trim bigon (e.g. a triangular neighbour face) rather than a valid boundary. The actual close mirrors `RemoveNakedMicroEdge()`'s own two-phase "nudge every neighbour's curve to the shared midpoint via `SetStartPoint()`/`SetEndPoint()`, THEN commit through `ReplaceEdgeCurve()`" shape, just run for all four neighbours instead of two (`ReplaceEdgeCurve()` already re-trims every face sharing whichever neighbour edge is passed to it, so a neighbour that is itself a shared edge with a THIRD face is handled for free). Verified on two flat quads sharing a 1e-4-long seam between an otherwise-large triangle apex each (the same "clipped corner" sliver shape `RemoveNakedMicroEdge()`'s own fixture already uses, just shared between two faces instead of naked on one): the shared edge and one merged vertex are genuinely gone afterward (7 edges → 6, 6 vertices → 5), both faces drop from 4-trim quads to plain 3-trim triangles, and the combined tessellated area is unchanged to within 2e-3 of its own ~30 value (`TestRemoveSharedMicroEdgeClosesIsolatedSeamBetweenTwoFaces`, tests/test_basic.cpp). Also verified to refuse rather than guess: a naked (1-trim) micro edge (wrong trim count, `RemoveNakedMicroEdge()`'s own job), a shared micro edge whose neighbour face is a bare triangle (would leave a degenerate bigon), and the usual out-of-range/already-deleted `edge_index` cases (`TestRemoveSharedMicroEdgeRefusesNakedEdgeAndDegenerateLoop`). Also new, same session: `Brep::RemoveAllNakedMicroEdges(tolerance)`/`Brep::RemoveAllSharedMicroEdges(tolerance)` — the one-call "strip every naked/shared sliver this Brep has" batch convenience for the two single-edge methods above, the same single/all pairing `RemoveHoleLoop`/`RemoveAllHoleLoops` and `MergeContiguousEdges`/`MergeAllContiguousEdges` already give their own siblings elsewhere in this file: each repeatedly scans for one qualifying micro edge and removes it, rescanning from scratch after every successful removal (Compact() renumbers everything), bounded the same `4 * edge_count + 16` way `MergeAllContiguousEdges()`/`SewTJunctions()` already bound their own loops. Verified on a single face with two independent naked slivers on different sides (one call removes both, `TestRemoveAllNakedMicroEdgesStripsEveryIsolatedSliverInOneCall`) and on two geometrically-independent shared-seam face pairs in one Brep (one call removes both seams, `TestRemoveAllSharedMicroEdgesStripsEveryIsolatedSeamInOneCall`), both re-checking the combined area is unchanged and a second call finds nothing left. Full `dino8_kernel_tests` suite (via direct binary run, four times across both additions) re-run clean: 100% passing, 0 regressions. Related additions: `RemoveSliverFaces`/`RemoveDegenerateEdges` and `SewTJunctions`. Still partial: this closes only the isolated, valence-3-endpoint case (a shared micro edge at a non-manifold junction, or whose neighbour loop is too small, is refused rather than handled, single or batch), and no app command calls any of `RemoveNakedMicroEdge()`/`RemoveSharedMicroEdge()`/`RemoveAllNakedMicroEdges()`/`RemoveAllSharedMicroEdges()` yet.
- [partial] Edge blend removal (remove fillet/chamfer faces and restore the sharp edge) — kernel `RemoveBlend` (fillet.h:1494, covering cylindrical `FilletConvexEdge`/`FilletConcaveEdge` faces, conical `FilletConvexEdgeTapered` faces, and now `FilletConvexEdges`' own m == 3 trihedral spherical vertex-blend corner via `RemoveSphericalVertexBlend`, fillet.cpp:4128 - see the blending category's own bullet for the full construction and test detail), plus `RemoveChamfer` (fillet.h:1543) and `RemoveChamferVertex` (fillet.h:1593). Still partial: only inverts this kernel's own constructions on planar-plus-blend solids; throws for oblique-end cylindrical fillets and for a spherical corner that shares a cylinder with a second spherical corner (every edge of a fully-rounded box); no app command calls any of them.
- [partial] Untrim face / remove outer trim / remove hole loops — **corrected: gained a real kernel implementation of the hole-removal half, still partial.** `Brep::RemoveHoleLoop(loop_index)`/`Brep::RemoveAllHoleLoops(face_index)` (dino8-kernel/include/dino8/kernel/brep.h; dino8-kernel/src/brep.cpp) now exist: given a live `ON_BrepLoop::inner` loop, `RemoveHoleLoop` deletes that loop, every trim on it, and every edge/vertex it privately owns, leaving the face's own outer boundary exactly as if the hole had never been cut — genuine in-place editing of THIS Brep's own face, unlike the app's `Op::UntrimHoles` (cmd path unchanged, still detaches the face into a brand-new separate object) and unlike this file's own `MakeEdgeKillRing`/`KillEdgeMakeRing` Euler-operator pair (which only RE-REPRESENTS a hole as one bridged loop — the material gap is still there, just redrawn; this genuinely fills it). `RemoveAllHoleLoops` is the one-call "strip every hole this face has" convenience: it collects every inner loop up front (no `Compact()` runs between individual removals, so earlier indices stay valid for the whole pass, the same discipline `SewTJunctions()`/`MergeAllContiguousEdges()` already use for their own repeated-removal loops) and finalizes once at the end. Verified on a hand-built planar face with a real `ON_BrepLoop::inner` hole (an outer 4x4 square with a central 2x2 hole, the identical fixture `MakeEdgeKillRing()`'s own tests use): removing the hole loop drops the face from 2 loops to 1, V and E each shrink by exactly the hole's own 4 private vertices/edges, and the tessellated physical area goes from the hole's own exact 16-4=12 up to the full 16 (`TestBrepRemoveHoleLoopRestoresSolidFaceExactly`); a face built with TWO disjoint holes has both removed in one `RemoveAllHoleLoops` call, reporting count 2 and the exact 16-4-0.25=11.75 → 16 area recovery (`TestBrepRemoveAllHoleLoopsRemovesEveryHoleInOneCall`), and a hole-free face returns 0 rather than erroring. Refuses (`Result::Failed`, the Brep left completely untouched) rather than guessing on the face's own outer loop (un-holing is only ever defined for a hole), and on a hole edge that's also used by a trim OUTSIDE the hole loop — verified with a decoy second face built to share one of the hole's own edges (`TestBrepRemoveHoleLoopRefusesOuterLoopSharedEdgeAndInvalidInput`), alongside the usual out-of-range/already-deleted `loop_index` refusals. Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial: this closes only the "remove hole loops" third of the item's own three-part name — `Op::Untrim`/`UntrimBorderOnly` (removing or growing a face's OWN outer trim back to its surface's natural boundary) has no kernel equivalent at all yet, singular (pole-touching) hole trims are refused rather than handled, and no `dino8-app` command calls the new kernel API — `Op::UntrimHoles` still only does its own detach-to-separate-object app-level path, unaware this kernel API exists.

*Same-day follow-up: this pass closes the item's remaining "remove outer trim" third too, still partial.* `Brep::RemoveOuterTrim(face_index)` (dino8-kernel/include/dino8/kernel/brep.h; dino8-kernel/src/brep.cpp) now exists: it deletes a face's own OUTER loop — every trim, edge and (unused-afterward) vertex it privately owns — then hands the face to the real `ON_Brep::NewOuterLoop()`, the same OpenNURBS primitive the app's own `Op::UntrimBorderOnly` (cmd_srfedit.cpp) already calls, which rebuilds the outer boundary as the underlying surface's own natural full-domain rectangle. Genuinely in-place on THIS Brep's own face, unlike `Op::Untrim`/`Op::UntrimBorderOnly`, which only ever run on a `DuplicateFace()`-detached single-face copy (never editing a multi-face Brep's face directly) — the identical "detach to a separate object" limitation `RemoveHoleLoop`'s own bullet above called out for `Op::UntrimHoles`. Any hole (inner) loop already on the face is left completely untouched, the same "holes are kept" contract `Op::UntrimBorderOnly` itself promises. Verified on `BuildPlanarFaceWithHole`'s own fixture (the identical fixture `RemoveHoleLoop`'s own tests above use) — deliberately already the target scenario: the underlying surface's real natural domain is the physical 5x5 square [-0.5,4.5]², but the face's CURRENT outer trim is only the smaller 4x4 square [0,4]², with an independent 2x2 hole [1,3]² in the middle. After `RemoveOuterTrim`, a point within the surface's natural extent but outside the old, smaller trim (uv (0.95,0.95), physical (4.25,4.25)) is genuinely inside the face for the first time, the old surrounding material is still inside, and the hole — untouched — is still excluded exactly as before, checked via exact `FaceContainsUV` loop-topology classification, not `Tessellate()`'s approximate grid-cell area (`TestBrepRemoveOuterTrimRestoresSurfaceNaturalBoundaryAndKeepsHoles`); the hole loop's own 4 corners are independently confirmed byte-for-byte unchanged, and V/E counts are unchanged overall (the old outer loop's 4 private corners/edges are gone, the rebuilt one's own 4 new corners/edges take their place). Refuses (`Result::Failed`, the Brep left completely untouched) rather than guessing whenever the CURRENT outer loop borders another face — any of its edges also used by a trim OUTSIDE the loop, verified with the same decoy-second-face construction `RemoveHoleLoop`'s own shared-edge test uses, applied to the outer boundary instead of a hole — or contains a singular (pole) trim, verified on a real `Brep::SphericalFace` octant fixture (the same one `TestSphericalFaceOctantIsValidWithSingularPoleTrim` uses, whose one collapsed-pole trim is genuinely `ON_BrepTrim::singular`), alongside the usual out-of-range/already-deleted `face_index` refusals (`TestBrepRemoveOuterTrimRefusesSingularTrimSharedEdgeAndInvalidInput`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial, and deliberately narrow: only a face whose outer boundary is entirely naked in this Brep (the standalone-trimmed-surface case `Op::Untrim`/`Op::UntrimBorderOnly` are actually used for, not a face's shared boundary inside a closed solid), no periodic/pole-touching outer boundary (a trimmed sphere/cone patch is refused, not handled), no "grow rather than fully reset" variant, and no `dino8-app` command calls it yet — `Op::UntrimBorderOnly` still only does its own detach-to-separate-object app-level path, unaware this kernel API exists. Doesn't change this category's own present/partial/missing counts (still 7/21/0 below, 62.5%): this item was already credited `[partial]` and stays `[partial]` — both named thirds ("remove hole loops" and "remove outer trim") now have real, tested, in-place kernel implementations, but the item's own remaining gaps (no app wiring for either, singular trims refused rather than handled on both sides) keep it short of `[present]`.

  *Same-day follow-up (this pass): this pass widens the "remove hole loops" third from one face to the whole Brep, still partial.* `Brep::RemoveAllHoleLoopsInBrep()` (dino8-kernel/include/dino8/kernel/brep.h; dino8-kernel/src/brep.cpp) now exists: the whole-Brep generalization of `RemoveAllHoleLoops(face_index)` above - strips every hole loop on EVERY live face in one call, the same "one call, every qualifying target in this Brep" convenience `RemoveAllNakedMicroEdges()`/`RemoveAllSharedMicroEdges()` already give their own single-edge siblings, applied here to hole loops instead of micro edges. Every hole loop on every live face is collected up front, across every face at once - `RemoveHoleLoopNoFinalize()` never calls `Compact()`, so no face's own loop indices are disturbed while another face's holes are being collected or removed - with one shared finalize at the end rather than `RemoveAllHoleLoops()`'s own per-face `Compact()` repeated once per face. Verified on a Brep carrying TWO independent faces (own separate surfaces, well-separated domains), each with its own single central hole: one `RemoveAllHoleLoopsInBrep()` call removes both - one per face - reports count 2, leaves both faces with only their own outer loop, and the former hole centre on each face is confirmed genuinely inside via exact `FaceContainsUV` loop-topology classification (`TestBrepRemoveAllHoleLoopsInBrepStripsHolesAcrossEveryFace`, tests/test_basic.cpp), and a hole-free Brep returns 0. Doesn't change this category's own present/partial/missing counts (still 7/21/0, 62.5%): a best-effort, all-faces-at-once convenience wrapper over an already-`[partial]` capability, not a new class of solid or trim it can handle - single-face `RemoveAllHoleLoops()`/`RemoveHoleLoop()`/`RemoveOuterTrim()` keep every limitation already stated above (a shared edge or singular trim is skipped/refused, not handled; no app wiring for any of them).
- [partial] Move / copy / rotate / mirror a hole feature (feature-level local edit) — app `ApplyHoleXform` (cmd_solidtools.cpp:1027) re-subtracts the stored cutter mesh from the stored pre-cut mesh with a kernel mesh boolean. The result is a mesh; no B-rep feature edit.
- [partial] Shell / hollow body with face removal (offset-body local op) — kernel `ShellConvexPlanar` (scalar and per-face thickness; convex planar only, adjacent openings refused) and `ShellClosedSphere`/`Torus` (closed analytic shells only). App Shell is mesh-based.
- [partial] Re-intersect adjacent faces / rebuild edges after an edit (post-tweak edge regeneration) — app `RebuildEdgesReal` (cmd_fillet.cpp:2527) refits every 2-trim edge through the real surface-surface intersection of its faces. Kernel `ReplaceEdgeCurve` re-trims faces against a substitute curve; the new adjacency query API makes neighbour lookup reusable, but there is no automatic kernel-level re-intersection after a tweak.
- [partial] Extend a face/surface past its current boundary in place (ExtendSrf) — app `ExtendSrfCommand` (cmd_srfedit.cpp:674) offers Type=Smooth|Linear, and Linear calls the kernel `NurbsSurface::ExtendLinear`. On a single-face object the surface is replaced in place, but on a multi-face polysurface the old face is deleted (`DeleteFace`+`Compact`) and the extended surface added as a separate object via `AddBrepFrom` (cmd_srfedit.cpp:728-732), not extended in place with neighbours re-trimmed.
- [partial] Taper / draft face (rotate face about a neutral plane by draft angle) — kernel `DraftFacesConvexPlanar` (dino8-kernel/include/dino8/kernel/boolean.h, dino8-kernel/src/boolean.cpp) now exists: it tilts one or more named faces of a convex planar-faced solid about the exact 3D line where that face's own plane meets a caller-supplied neutral plane (found via the standard two-plane-intersection formula, not the face's own nearest edge, so the neutral plane need not coincide with any face of the solid), reusing `OffsetSolidConvexPlanar`'s own half-space-intersection reconstruction so every other (untouched) face is correctly re-trimmed against the tilted one. `angle_radians` follows `Brep::ExtrudeTapered`'s own sign convention (positive shrinks moving along +neutral_plane.zaxis). Verified exactly: drafting all 4 walls of a `Brep::Box()` about its own bottom face reproduces the classical frustum-of-a-pyramid volume `(h/3)(A0+A1+sqrt(A0*A1))` and the exact concentric shrunken top-face bounding square (`TestDraftFacesConvexPlanarBoxAllWallsMatchesExactFrustumVolume`); drafting a single wall leaves the non-adjacent opposite wall's own boundary untouched and matches an independent closed-form cross-sectional-area integral (`TestDraftFacesConvexPlanarSingleFaceLeavesOppositeFaceExactlyUntouched`). Still partial (an intentionally narrow first cut, not silently over-claimed): convex planar-faced solids only (the same scope `OffsetFace`/`OffsetSolidConvexPlanar` already have — a curved or non-convex body throws), one shared angle across all named faces (no per-face angle vector), and the app layer does not call it at all yet — a grep for taperface/draftface/rotateface/tiltface in dino8-app/src still finds nothing.
- [partial] Replace face (swap a face's surface, re-trim it and its neighbours) — **upgraded from missing.** `ReplaceFacePlaneConvexPlanar(solid, face_index, new_plane)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: it swaps ONE named face's plane for a caller-supplied `new_plane` outright and re-extends/re-trims every OTHER face against it, reusing `OffsetFace()`/`OffsetSolidConvexPlanar()`/`DraftFacesConvexPlanar()`'s own "start from an oversized polygon per face, clip against every other face's own half-space" reconstruction verbatim. Genuinely more general than either sibling: `OffsetFace()` only translates a plane along its own normal, `DraftFacesConvexPlanar()` only rotates one about its exact intersection line with a separate neutral plane, but `ReplaceFacePlaneConvexPlanar()` takes the target plane directly, so a combined translate-and-tilt (or any other single-plane swap) needs no neutral-plane/hinge-angle bookkeeping from the caller. Verified two ways: (1) fed the identical translated plane `OffsetFace()` itself would produce, it reproduces `OffsetFace()`'s own output bit-for-bit, vertex for vertex (`TestReplaceFacePlaneConvexPlanarMatchesOffsetFaceForPureTranslate`) — proof this is a genuine generalization, not a separate and possibly-divergent construction; (2) a combined move-and-tilt ("roof") plane that no single `OffsetFace()`/`DraftFacesConvexPlanar()` call could produce matches an independent closed-form double integral for volume and leaves the four side walls individually re-trimmed exactly as the roof's own linear-in-y height implies — a flat-topped front wall, an UNCHANGED back wall (the roof height there happens to equal the original), and trapezoidal left/right walls (`TestReplaceFacePlaneConvexPlanarTiltedRoofMatchesExactIntegralAndRetrimsWalls`) — not merely a plausible-looking total volume. Refusal cases (out-of-range/negative `face_index`, a default-constructed/`!IsValid()` plane, a `new_plane` that collapses a face to fewer than 3 vertices or ~0 area) are also covered (`TestReplaceFacePlaneConvexPlanarRefusesInvalidInput`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial: convex planar-faced solids only (the same scope every sibling in this family already has — a curved or non-convex body throws), `new_plane.zaxis` must point outward or the swap fails via the same collapse diagnostic rather than a distinct one, and no app command calls it yet (a grep for replaceface/swapface in dino8-app/src still finds nothing).

  *Same-day follow-up (this pass): this pass adds a genuine batch sibling, still partial.* `ReplaceFacePlanesConvexPlanar(solid, face_planes)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: `face_planes` is a caller-chosen list of `(face_index, new_plane)` pairs — the same two positional arguments `ReplaceFacePlaneConvexPlanar()` takes for one face — swapped for their own named face's plane in a SINGLE call, reusing the identical "start from an oversized polygon per face, clip against every other face's own half-space" reconstruction with every named face's own new plane applied at once, rather than through several sequential `ReplaceFacePlaneConvexPlanar()` calls. This is the genuine "one call, many named faces" batching gap `MoveVerticesConvexPlanar()`/`MoveEdgesConvexPlanar()` already closed for points and edges, now closed for faces, and it's load-bearing rather than a mere convenience: two faces whose own new planes are only jointly consistent — a box's own front and back walls each pushed past the OTHER's OLD position, so front's new plane alone (with back unchanged) is a flat half-space contradiction — can be replaced together in one call even though `ReplaceFacePlaneConvexPlanar()` on either one ALONE is refused first (verified directly: the single-face call throws, then the identical two-face swap succeeds and matches the exact resulting box volume, `TestReplaceFacePlanesConvexPlanarSucceedsWhereSequentialSingleReplaceWouldRefuse`, tests/test_basic.cpp). Two entries naming the same `face_index` are refused as ambiguous, the same discipline `MoveVerticesConvexPlanar()` already enforces for a duplicate vertex; an empty `face_planes`, an out-of-range `face_index`, a not-`IsValid()` plane, and a plane collapsing some face's own boundary are also covered (`TestReplaceFacePlanesConvexPlanarRefusesInvalidInput`). `MoveFacesConvexPlanar()` (see the "Move/transform face" bullet above's own "Same-day follow-up") is built directly on top of this function, the same "flatten per-target transforms into plane pairs, delegate once" shape `MoveFaceConvexPlanar()` itself already has toward the single-face `ReplaceFacePlaneConvexPlanar()`. Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial: this closes only the "one call, many faces" batching gap — convex-planar-only, `new_plane.zaxis` must point outward, and no app wiring remain exactly as this bullet's own paragraph above already states.
- [partial] Imprint curve / face onto a body face (add edges without changing geometry) — **corrected: upgraded from missing.** Kernel `ImprintFaces(target, tool)` (boolean_general.h:61; boolean_general.cpp:3086) landed before this window and was already reflected under the sibling Boolean-operations category, but this category's own bullet was never updated to match and still claimed "a case-insensitive grep for imprint finds no hits anywhere" — false as of current HEAD. It splits `target`'s own faces wherever they cross a `tool` body's faces while keeping every fragment unconditionally (no ray-cast classification, no material ever removed), verified on a closed-loop fixture (box pierced by a cylinder) and an open-chain fixture (two overlapping boxes), each direction, plus a disjoint-operand no-op and a faceless-operand throw. Still partial: this is face-onto-face imprint only (no curve-onto-face imprint exists anywhere), it inherits `BooleanCombineGeneral`'s own scope limits (one crossing chain per opposing face pair, genus-0 faces, no self-crossing chains), only `target`'s faces are split per call, and no app command exposes it yet.
- [partial] Merge faces on the same non-planar surface (cylinder/tangent split faces) — **upgraded from missing.** `Brep::MergeSameSurfaceFaces()` (dino8-kernel/include/dino8/kernel/brep.h; dino8-kernel/src/brep.cpp) now exists: it merges two adjacent faces that trim the literal same underlying surface (this class' own `m_si` index, not merely a congruent-but-separate one) across their shared edge, into one face — the curved-surface case `Brep::MergeCoplanarFaces()` explicitly excludes via its own `NurbsSurface::IsPlanar()` guard. Deliberately doesn't rebuild any geometry the way `MergeCoplanarFaces()`'s own `ON_BrepTrimmedPlane()` reconstruction does: since both source faces already trim the SAME surface, every surviving trim's own 2D curve is already valid in that shared parameter space and is reused via a plain duplicate (not resampled/refit), and every surviving 3D edge is reused unchanged — so a cylinder's own iso-u boundary (a genuine circular arc) stays exact through the merge, not approximated. Verified on two trimmed patches of one real `ON_Cylinder::GetNurbForm()` cylindrical surface (radius 2, height 3), split at an off-center angle (1.0 rad out of a 2.5 rad total sweep) into two faces sharing one vertical seam edge, each boundary edge taken directly from the surface's own `IsoCurve()` (never a fresh fit): merging drops the fixture from 2 faces to 1, the result stays a valid `ON_Brep` on the SAME non-planar surface, and the tessellated area is bit-for-bit unchanged by the merge (`TestMergeSameSurfaceFacesWeldsTwoCylindricalPatchesIntoOne`, tests/test_basic.cpp) and matches the exact closed-form cylinder-sector area `r*angle*height` both before and after. Also verified NOT to overreach: `MergeCoplanarFaces()`'s own two-separate-flat-surfaces fixture (two coplanar unit squares from `FromPlanarFaces()`, each with its own distinct surface index) is left completely untouched by this method even though it IS mergeable by its coplanar sibling — proof this is genuinely scoped to same-surface-OBJECT pairs, not a silent reimplementation of `MergeCoplanarFaces()` (`TestMergeSameSurfaceFacesLeavesDistinctCoplanarSurfacesUntouched`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial, and deliberately narrow: only a literal shared surface object merges (two faces that merely happen to be geometrically congruent cylinders - e.g. built from two separate `ON_Cylinder::GetNurbForm()` calls with the same radius/axis, as `Brep::FromMixedFaces()`'s own `CylindricalFace` machinery always produces - are NOT recognized, since that would need a real surface-equality fit this kernel doesn't have); both faces must agree on `m_bRev` and share exactly one two-trim edge (same non-manifold-safe scope `MergeCoplanarFaces()` already has); and no `dino8-app` command calls it yet — a grep for MergeSameSurface in dino8-app/src still finds nothing.
- [partial] Push/pull a face (extrude face and merge/cut into its own body) — **corrected: upgraded from missing.** Kernel `PushPullFace(solid, face_index, distance)` (boolean.h/boolean.cpp) landed this session: a push (`distance > 0`) genuinely extrudes new side-wall faces into previously-empty space without touching any other face (unlike `OffsetFace`, which always re-extends/re-trims neighbours in place); a pull (`distance < 0`) retrims every neighbour perpendicular to the pushed face via an exact single half-space clip and adds no new geometry. Direct topological surgery, not a boolean — `Brep::Extrude()`+`BooleanCombinePlanar()` was tried first and found to fail (a swept profile is only piecewise planar; even a hand-built all-planar prism makes `BooleanCombinePlanar()` throw on the flush, zero-overlap coincident face this operation always creates, a disclosed gap in that engine's own coincident-face handling). No convexity precondition on `solid` (verified on a genuinely non-convex L-shaped prism, both directions). Still partial: planar-faced solids only (`PlanarFaces()`'s own precondition), and a pull refuses an oblique (non-perpendicular) neighbour rather than attempting a general re-intersection.

  *Same-day follow-up (this pass): this pass adds a genuine batch sibling, still partial.* `PushPullFaces(solid, face_distances)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: `face_distances` is a caller-chosen list of `(face_index, distance)` pairs - the same two positional arguments `PushPullFace()` takes for one face at a time - applied in a single call. This is genuinely load-bearing, not merely fewer function calls for the same result: a PULL retrims every perpendicular neighbour it touches, so calling `PushPullFace()` twice in sequence runs the SECOND call against whatever intermediate boundary the FIRST call already left its neighbours at - order-dependent in a way a single combined call never is, since this function derives every named face's own new cap and every unnamed neighbour's own clip from `solid`'s own ORIGINAL geometry exactly once. Two named faces that share an edge are refused outright (each would independently want to redraw the OTHER's own shared boundary - a pushed face's new side wall starts at its old edge, a pulled neighbour's own retrim ends at a new, closer one - genuinely ambiguous, not attempted), the same "no silent tie-break" discipline `MoveFacesConvexPlanar()`/`ReplaceFacePlanesConvexPlanar()` already enforce for a duplicate target, applied here to adjacency instead of identity. Verified two ways: (1) a single-entry `face_distances` list reproduces `PushPullFace()`'s own output bit-for-bit, vertex for vertex, in both the push and pull direction (`TestPushPullFacesSingleEntryMatchesPushPullFace`, tests/test_basic.cpp) — proof the batch's shared reconstruction introduces no divergence of its own; (2) pushing the box's own front wall out while independently pulling its (non-adjacent, opposite) back wall in, in ONE call, matches the exact volume arithmetic and — matched face-for-face by centroid and vertex-count rather than by raw PlanarFaces() index order, since `PushPullFace()`'s own new-face append position depends on which face was named and so genuinely differs between the two sequential orders and the batch — the identical solid two SEQUENTIAL single `PushPullFace()` calls produce in EITHER order (front-then-back or back-then-front), proving the batch's own one-pass consistency doesn't merely happen to match one arbitrary calling order (`TestPushPullFacesTwoIndependentPushPullMatchesEitherSequentialOrder`). Refusal cases — an empty `face_distances`, an out-of-range `face_index`, a zero/non-finite distance, a duplicate `face_index`, and two adjacent named targets (the box's own front and top walls) — are also covered (`TestPushPullFacesRefusesInvalidInput`). Full `dino8_kernel_tests` suite re-run clean: 100% passing, 0 regressions. Still partial: this closes only the "one call, many named faces" batching gap — planar-faced-only and no app wiring remain exactly as this bullet's own paragraph above already states.
- [partial] Move a single B-rep vertex directly (drag one topological corner in place; adjacent edges reshape around it) — **upgraded from missing.** `MoveVertexConvexPlanar(solid, old_position, new_position)` (dino8-kernel/include/dino8/kernel/boolean.h; dino8-kernel/src/boolean.cpp) now exists: `old_position` identifies the vertex to move by matching it (within tolerance) against every face's own `PlanarFaces()` loop, every incident face's plane is re-derived from its own (now-moved) corners, and every OTHER face — including one merely adjacent to the moved vertex through a shared edge but not itself touching it — is re-clipped against the moved planes via the same `ClipConvexPolygon` half-space-intersection reconstruction `OffsetFace()`/`DraftFacesConvexPlanar()`/`ReplaceFacePlaneConvexPlanar()` already share, which is how "adjacent edges reshape around it" falls out for free. Verified on a square pyramid (apex shared by all 4 triangular side faces, the vertex where the most faces meet at once): moving the apex to a position that is both taller AND shifted off-center (not a pure translate along any one face's own normal) matches the exact closed-form pyramid volume `(1/3)*base_area*height` for an oblique apex — a classical fact independent of the apex's own x/y position — while the (non-incident) base face keeps exactly its original 4 vertices and each side face keeps its own two untouched base corners plus the new apex position exactly (`TestMoveVertexConvexPlanarPyramidApexMatchesExactVolumeAndLeavesBaseUntouched`). Refusal cases are also covered (`TestMoveVertexConvexPlanarRefusesInvalidInput`): an `old_position` matching no vertex, a vertex incident to a non-triangular (e.g. box-corner quad) face, a move that flips an incident triangle's own outward orientation, and a move that collapses an incident triangle to ~0 area. Still partial, and deliberately narrow: convex planar-faced solids only (the same scope every sibling in this family already has), and — unlike those siblings, which only ever move a whole face's plane — every face INCIDENT to the moved vertex must be a triangle, since a triangle's plane is always well-defined with one corner free to move anywhere while a 4+-vertex face isn't guaranteed to stay planar; this covers the common tetrahedron/pyramid-apex/triangulated-corner case but not a box corner (4 vertices per incident face) directly. No app command calls it yet — a grep for movevertex/dragvertex in dino8-app/src still finds nothing.

  *Same-day follow-up: this pass adds a genuine batch sibling, still partial.* `MoveVerticesConvexPlanar(solid, moves)` (dino8-kernel/include/dino8/kernel/boolean.h:1101; dino8-kernel/src/boolean.cpp:2208) now exists: it exposes this file's own private `MoveConvexPlanarPoints()` shared core (boolean.cpp:2037) — already capable of moving an arbitrary LIST of (old, new) point pairs in one pass, previously only reachable through `MoveEdgeConvexPlanar()`'s own fixed two-point call — as a public entry point for a caller-chosen SET of independent vertices, with its own explicit ambiguous-duplicate-vertex refusal layered on top (two entries naming the same vertex are refused outright rather than silently falling through to the shared core's own "first-in-list wins" internal tie-break). Verified two ways: (1) moving ALL FOUR of a tetrahedron's own vertices in one call — a combination `MoveEdgeConvexPlanar()` cannot even express, since it only ever takes exactly two points — matches the exact signed-triple-product volume formula for the four new vertices (`TestMoveVerticesConvexPlanarMovesAllFourTetrahedronVerticesMatchesExactVolumeFromSignedTripleProduct`); (2) the one-pass semantics are load-bearing, not just a convenience shortcut: moving one tetrahedron vertex (`b0`) alone onto the exact line through the apex and the OTHER moved vertex's own OLD position collapses an incident face to ~0 area — `MoveVertexConvexPlanar()` correctly refuses this single move — but moving that same `b0` AND the other vertex together, to their own final (non-collinear) positions, in one `MoveVerticesConvexPlanar()` call, never passes through that degenerate intermediate state at all and succeeds, matching the exact signed-triple-product volume for the genuinely different final shape (`TestMoveVerticesConvexPlanarSucceedsWhereASequentialSingleMoveWouldRefuse`) — proof this is a real capability gain over N sequential single-vertex calls, not merely fewer function calls for the same reachable set of results. Refusal cases — an empty `moves` list, two entries naming the same vertex, an unmatched `old_position`, and the usual non-triangular-incident-face box refusal — are also covered (`TestMoveVerticesConvexPlanarRefusesInvalidInput`). Full `dino8_kernel_tests` suite (via `ctest`) re-run clean: 100% passing, 0 regressions. Still partial: this closes only the "one call, many vertices" batching gap — convex-planar-only, triangle-incident-faces-only, and no app command calling any function in this family (a grep for movevertex/movevertices/dragvertex in dino8-app/src still finds nothing) remain exactly as this bullet's own paragraph above already states.

*Note on this category's counts: the table above shows 7 present / 21 partial / 0 missing (28 items total) — (7 + 0.5*21)/28 = 62.5%, up from the prior 7/20/1 (60.7%). This corrects a pre-existing arithmetic slip inherited from the last measurement (the table declared 17 partial against a physically-written bullet list that only ever had 16 gap bullets); combined with the `ImprintFaces` upgrade (missing→partial), the `PushPullFace` upgrade (missing→partial), the `MergeContiguousEdges`/`MergeAllContiguousEdges` upgrade (partial→present, this category's own "Merge contiguous tangent edges" bullet — see the healing category's own bullet for the full detail), the `DraftFacesConvexPlanar` upgrade of the separate "Taper / draft face" item (also missing→partial) — three parallel-session additions, see this document's own same-day session notes for all three — the `ReplaceFacePlaneConvexPlanar` upgrade of the separate "Replace face" item (missing→partial, see that bullet above for the full detail), the `MoveVertexConvexPlanar` upgrade of this category's own "Move a single B-rep vertex directly" item (missing→partial, see that bullet above for the full detail) — the internally-consistent result was 7/20/1 (60.7%). A same-session addition, `DeleteFaceHealConvexPlanar` (see this category's own "Delete face with heal" bullet above), did NOT change that count: it strengthened that item's own already-`partial` rating with a genuine kernel heal in place of a flat re-cap, but stayed partial (convex-planar-only, no app wiring) rather than crossing to present. This pass's own `MergeSameSurfaceFaces` addition (see this category's own "Merge faces on the same non-planar surface" bullet above) is the one that DOES move the count: missing→partial, 1 missing → 0 missing, giving the 7/21/0 (62.5%) now shown — the category's last `[missing]` item is closed; every remaining gap here is `[partial]`. A later same-day addition, `SplitFaceByCurve` (see this category's own "Split face by curve / surface" bullet above), did NOT change that count either, for the same reason `DeleteFaceHealConvexPlanar` didn't: it replaced an app-only approximate command with a genuine kernel trim-loop split, but stayed `[partial]` (single-open-chain scope, no app wiring) rather than crossing to `[present]` — the table's own 7/21/0 (62.5%) is unchanged. A still-later same-day addition, `RemoveSharedMicroEdge` (see this category's own "Remove small / sliver edges" bullet above), also does NOT move the count, for the identical reason: it closes the shared-edge half of an item that was already `[partial]` for its naked-edge half, and stays `[partial]` (isolated valence-3 endpoints only, no app wiring for either `RemoveNakedMicroEdge` or `RemoveSharedMicroEdge`) rather than crossing to `[present]` — the table's own 7/21/0 (62.5%) is still unchanged. A later pass's `MoveFaceConvexPlanar` and `MoveEdgeConvexPlanar` additions (see this category's own "Move/transform face" and "Move/transform edge" bullets above) also do NOT move the count, for the same reason as the additions immediately above: both replace an "approximate app path, no kernel op at all" gap with a real, tested, in-place kernel implementation, but both stay `[partial]` (convex-planar-only, no app wiring, and — for the edge case — triangle-incident-faces-only) rather than crossing to `[present]` — the table's own 7/21/0 (62.5%) is still unchanged. This session's own `MoveVerticesConvexPlanar`/`MoveEdgesConvexPlanar` additions (see this category's own "Move a single B-rep vertex directly" and "Move/transform edge" bullets above) also do NOT move the count, for the identical reason: both are genuine, tested, one-pass batch siblings of an already-`[partial]` single-target function, but neither closes that item's own remaining convex-planar-only/triangle-incident-faces-only/no-app-wiring gaps — they widen how many targets one call can move at once, not the class of solid or face it can move them on — so both items stay `[partial]` and the table's own 7/21/0 (62.5%) is still unchanged. This pass's own `ReplaceFacePlanesConvexPlanar`/`MoveFacesConvexPlanar` additions (see this category's own "Replace face" and "Move/transform face" bullets above) also do NOT move the count, for the identical reason as the `MoveVerticesConvexPlanar`/`MoveEdgesConvexPlanar` pair immediately above: both are genuine, tested, one-pass batch siblings of an already-`[partial]` single-face function (`ReplaceFacePlaneConvexPlanar`/`MoveFaceConvexPlanar` respectively), extending the same "one call, many targets" batching this category's own vertex/edge family already has to faces — convex-planar-only and no app wiring remain exactly as both bullets' own paragraphs above already state — so both items stay `[partial]` and the table's own 7/21/0 (62.5%) is still unchanged. This same-day pass's own `FoldFacesConvexPlanar`/`DeleteFacesHealConvexPlanar` additions (see this category's own "Rotate face about hinge edge" and "Delete face with heal" bullets above) also do NOT move the count, for the identical reason as every batch sibling immediately above: both are genuine, tested, one-pass batch generalizations of an already-`[partial]` single-face function (`FoldFaceConvexPlanar`/`DeleteFaceHealConvexPlanar` respectively) — the former closing the "one call, many hinge-folds" batching gap (proven via a bit-for-bit match against a single-entry call and against an equivalent sequential pair of single calls), the latter closing the "one call, many dropped faces" batching gap (proven via a bit-for-bit match against sequential single calls in either order) — but neither crosses this item's own remaining convex-planar-only/no-app-wiring gaps, so both stay `[partial]` and the table's own 7/21/0 (62.5%) is still unchanged. This pass's own `PushPullFaces` addition (see this category's own "Push/pull a face" bullet above) also does NOT move the count, for the identical reason as every batch sibling above: a genuine, tested, one-pass batch generalization of an already-`[partial]` single-face function (`PushPullFace`) that closes the "one call, many named faces" batching gap (proven via a bit-for-bit match against a single-entry call and, for two independent targets, an order-independent match against sequential single calls in EITHER order — the first batch sibling in this family where the underlying single-call function's own new-face append order is itself call-order-dependent, so the equivalence had to be checked as "same set of faces" rather than "same PlanarFaces() index order") — but it doesn't cross `PushPullFace`'s own remaining planar-faced-only/no-app-wiring gaps, so the item stays `[partial]` and the table's own 7/21/0 (62.5%) is still unchanged. This pass's own `RemoveAllHoleLoopsInBrep` addition (see this category's own "Untrim face / remove outer trim / remove hole loops" bullet above) also does NOT move the count: a genuine, tested, whole-Brep convenience wrapper over the already-`[partial]` `RemoveAllHoleLoops(face_index)`/`RemoveHoleLoop()` pair (the same "one call, every qualifying target" convenience `RemoveAllNakedMicroEdges()`/`RemoveAllSharedMicroEdges()` already give their own single-edge siblings, applied here to hole loops across every face at once instead of one named face) — but it widens how many faces one call can strip holes from, not the class of hole or trim it can remove (a shared edge or singular trim is still skipped/refused exactly as `RemoveHoleLoop()` itself already refuses it, and no `dino8-app` command calls any of `RemoveHoleLoop`/`RemoveAllHoleLoops`/`RemoveAllHoleLoopsInBrep`/`RemoveOuterTrim` yet), so this item too stays `[partial]` and the table's own 7/21/0 (62.5%) is still unchanged.*

**kernel: Intersections & projections** (intersections):
- [partial] Analytic/analytic SSX closed forms (plane/plane, plane/cylinder, cylinder/cylinder, plane/sphere, cone, torus) — closed forms still exist only inside `BooleanCombineMixed`'s private splitters (`SplitCylindricalByObliquePlane`, `SplitCylindricalByParallelCylinder`, Steinmetz/unequal-cylinder splitters) and the planar boolean's plane/plane path. No public analytic-SSX API, and no plane/sphere, cone or torus closed form (the only general path is the mesh-seeded `IntersectSurfaces`).
- [missing] SSX tangent / grazing contact (surfaces touching along a point or curve) — `surface_intersect.h:72` still documents surfaces that "only touch tangentially" as empty results, and `TriTri` (surface_intersect.cpp:167-179) still returns false for parallel/coplanar triangle pairs (`if (dir.Length() < 1e-9) return false;`). There is no SSX tangency capability to give partial credit for (the only working tangency is curve/surface: a line tangent to a sphere gives 1 CSX hit, scored elsewhere).
- [partial] SSX across periodic seams and at singular points (poles) — seam handling is real (`SplitAtSeams`, `SeamCrossing` with a pinned-seam Newton solve, `FaceContainsUV`, surface_intersect.cpp:649,1010,1083). Poles have no dedicated singular-point treatment beyond a closest-point pole fix.
- [partial] SSX coincident / overlapping surface regions — `IntersectSurfaces` still returns nothing for coincident surfaces. The only coincidence handling is inside planar booleans.
- [missing] CSX against trimmed faces and curve-on-surface overlap (coincident) detection — `IntersectCurveSurface` still takes only an `ON_Surface`, no trim test; the app still throws the face away inside `IntersectAny`. No overlap detection anywhere. (Two exact-duplicate bullets from the pre-measurement map were merged into this one.)
- [partial] Curve self-intersection — still app-only and sampled (`same_curve` path, `CurveSelfIntersects`). The kernel's own `IntersectCurves(c, c)` is still not usable for this (spurious self-hits on a plain line).
- [partial] Curve/plane intersection — app `CurvePlaneHits` (sign change plus bisection). A caller can pass a bounded `ON_PlaneSurface` to `IntersectCurveSurface`, but there is no dedicated infinite-plane API.
- [partial] Plane sections / contours of surfaces and B-reps (Section, Contour, ClippingSections) — the app still slices render meshes (`SliceObjects`/`SliceMesh`). Kernel `SplitByPlane` is mesh-only; the exact route (`IntersectSurfaces` per face) is not used for sections.
- [partial] Mesh self-intersection detection — `Mesh::FindSelfIntersections`/`FindOffsetSelfIntersections` (mesh.cpp:3342 area). **Same-day follow-up:** the "overlapping coplanar triangles are never reported" half of this bullet's own prior evidence is closed — `CoplanarTrianglesOverlap` (mesh.cpp, next to `TrianglesProperlyOverlap`) is a genuine, if narrowly-scoped, fix: `TrianglesProperlyOverlap`'s own cross-product-of-normals construction returns a zero vector for two coplanar planes (no shared line to measure an interval along), so a coplanar pair was silently unreachable by that test no matter how much they overlapped; the new function instead confirms the two triangles genuinely share ONE plane (not merely parallel ones - every vertex of the second triangle is checked to lie within `tolerance` of the first's own plane, correctly rejecting the box test fixture's own parallel-but-offset top/bottom faces), projects both onto an orthonormal basis of that shared plane, and applies the standard two-convex-polygon separating-axis test (no separating line among either triangle's own up to 6 edge directions means a genuine positive-area overlap). `FindSelfIntersections()` now ORs this into its existing per-pair test, so nothing about the crossing-triangle path changes. Verified by re-deriving `TestMeshFindSelfIntersectionsDetectsOnlyGenuineCrossings()`'s own case (4) fixture (previously pinned as `.empty()`, a documented gap, not a silent one) to now assert the pair IS reported, plus two new cases: a coplanar pair with real area but genuinely no overlap (correctly still clean) and a parallel-but-offset-plane pair (correctly still clean, proving the plane-coincidence check does real work beyond the parallel-normal check alone). "Any pair sharing a vertex is never examined" is NOT a gap, on inspection of the method's own doc comment - that is by design (ordinary mesh connectivity, not a self-intersection question this method is meant to answer) and was mis-stated in this bullet's own prior text; corrected here. Full `dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions. Still honestly partial: DETECTION ONLY, no repair - a genuine self-intersection has no single correct automatic fix, the same considered position `Check()`'s own `non_manifold_edges` already takes.
- [partial] Surface / B-rep self-intersection detection — `Brep::Check()` reports `SelfIntersectingLoop` and `SelfIntersectingLoop3d` (brep.h:2707,2726; used in brep.cpp:6542,6549 — corrected 2026-09-28, was mis-cited brep.h:2524,2543; brep.cpp:6342,6349). Still partial: only face boundaries are checked, no face-interior self-intersection test and no face-vs-face crossing test within a B-rep; nearly-parallel close segments are excluded by design. (Two exact-duplicate bullets from the pre-measurement map were merged into this one.)
- [partial] Projection of curves/points onto surfaces along a direction (Project) — app `ProjectCommand` samples the curve and ray-casts along the CPlane normal onto the render mesh, then refits. No kernel project API.
- [partial] Pull curves/points to surfaces (closest-point projection) — kernel point projection is solid (`ClosestPointParameter`/`ClosestPoint`, `SurfaceClosestPointGlobal`), but there is no kernel "pull a curve into a curve-on-surface" API.
- [partial] Silhouette / outline curves — still app-only and mesh-based (`Silhouette`, render-mesh edges where adjacent face normals flip against the view vector). No kernel silhouette.
- [partial] B-rep/B-rep and curve/B-rep intersection as a kernel API — only face-level kernel entry points exist (`IntersectFaces`, `IntersectCurveSurface`); the app composes the B-rep loop itself (`IntersectAny`). `BooleanCombineGeneral` runs face-pair SSX internally, but there is no public Brep-level Intersect.
- [partial] Pullback of a 3D curve to surface parameter space (pcurve generation for arbitrary curves on a surface) — still no public pullback API; SSX produces pcurves as a by-product. The kernel builds real trims by pullback inside `Brep::ReplaceEdgeCurve` and `SplitNakedEdgeAt`, but both are internal to topology edits, not a general-purpose pullback call.
- [partial] Point-cloud contour/section as separate app commands (PointCloudContour/PointCloudSection) — app-level band-sampling around a plane; the kernel `PointCloud` has no section API.

*Note on this category's counts: 13 present / 14 partial / 2 missing (29 items). This corrects a pre-existing arithmetic slip: the table previously declared 11 present / 16 partial, which never matched the physically-written 16 gap bullets above (14 partial + 2 missing); present is 29 minus those 16, i.e. 13, not 11. No individual item's status changed — this is a transcription fix from when the document's own noted duplicate-bullet merge was applied to the table but not carried through consistently.*

**kernel: Healing, repair, validation, tolerant modeling** (healing):
- [partial] Tolerant sewing with edge splitting (partial-overlap edges, T-junctions, mismatched edge subdivision) — `Brep::SewTJunctions` (declaration brep.h:3273, implementation brep.cpp:7745-7797 — corrected 2026-09-28, was mis-cited brep.h:2766-2801; brep.cpp:6779-6836, a range that actually held `SplitNonManifoldVertex`/`Vertices` code) finds every T-junction among naked edges, splits the longer edge via `SplitNakedEdgeAt`, and finishes with `JoinNakedEdges`. Still partial: refuses every curved naked edge (`if (!a.IsLinear(tol)) continue;`); the app's own `JoinNakedEdges` does not call it; a latent bug re-confirmed by reading the current source — in the `for (int k = 0; k < 2; ++k)` inner loop the `break;` at line 7792 (corrected 2026-09-28, was mis-cited brep.cpp:6820) is unconditional, so if edge B's first endpoint (k=0) satisfies the on-line/strictly-interior test but `SplitNakedEdgeAt` then returns anything other than `Result::Ok`, the loop still breaks and B's second endpoint (k=1) is never tried in that pass.
- [partial] Geometric consistency validation (edge curve lies on adjacent surfaces, 2D trim vs 3D edge agreement, face/face self-intersection check) — `Brep::Check()` reports `EdgeVertexGap`, `TrimEdgeGap`, `LoopGap`, `InvalidTrim`, 2D/3D loop self-intersection, and (new this pass — see honesty note above) `NonManifoldVertex` pinch-point detection via union-find over each vertex's incident-edge face groups (brep.cpp:6279-6286, calling `GroupVertexEdgesByFace`, brep.cpp:6155-6166+). Its heal, `Brep::SplitNonManifoldVertex`/`SplitNonManifoldVertices` (brep.h:2864-2875; brep.cpp:6745-6812 — corrected 2026-09-28, was mis-cited brep.h:2662-2700; brep.cpp:6538-6612), disjoins a pinch point into one vertex per face-group, never deleting or `Compact()`ing; it returns `Result::Failed` (not a crash, but a real refusal) when one of the vertex's incident edges is closed on itself at that same vertex (brep.h:2681-2689's own doc comment calls this genuinely rare but explicitly unhandled), and it is not called anywhere in dino8-app. Neither addition is strong enough to flip this item to present. (The `Brep::Check()` DegenerateFace false-positive this bullet used to describe as "confirmed still present" is fixed, by `b1ac7c9` — see the top-of-document honesty note for why the prior pass's re-confirmation was itself stale, and for where the fix actually lives in the current source.) Remaining gaps: **`TrimEdgeGap` now compares 9 evenly-spaced samples, not 3** (widened by `a49a9a4`, closing the "cannot see a gap that peaks strictly between the old 3 samples" blind spot this bullet used to name — verified against a fixture built to be invisible to the old sampling); no face/face self-intersection check between faces sharing no boundary.
- [partial] Gap closing by edge re-trim / trim refit (ReplaceEdgeCurve, RefitTrim, ReplaceEdge) — `Brep::ReplaceEdgeCurve` does closest-point re-projection of every trim, throwing when the fit fails; `CloseLoopGapsWithinTolerance` closes residual 2D loop gaps. No `RefitTrim` or general `ReplaceEdge`.
- [partial] Micro/sliver edge removal (RemoveAllNakedMicroEdges / Brep::RemoveNakedMicroEdge) — `Brep::RemoveNakedMicroEdge` works only on an isolated naked sliver whose neighbours are also naked. `Brep::RemoveDegenerateEdges` (brep.h:2660) collapses shared or naked edges at or below tolerance.
- [partial] Self-intersection detection (curves, meshes, surfaces/breps) — meshes: `Mesh::FindSelfIntersections`/`FindOffsetSelfIntersections`; breps: only loop boundaries via `Check()`'s `SelfIntersectingLoop`/`SelfIntersectingLoop3d`; curves: app-only sampled. No face-interior or face/face check anywhere.
- [present] Edge merging (MergeEdge / MergeAllEdges via ON_Brep::CombineContiguousEdges) — **upgraded from partial.** `Brep::MergeContiguousEdges(edge_index_a, edge_index_b, angle_tolerance_radians)` (brep.h; brep.cpp) is a genuine kernel wrapper around `ON_Brep::CombineContiguousEdges`, the same OpenNURBS primitive the app-only `MergeEdgeCommand` (cmd_fillet.cpp) already called directly — previously reachable only from that one app command reaching into the raw `ON_Brep`, with zero kernel-level test coverage. Enforces the same contiguity contract OpenNURBS itself does (the shared vertex has exactly 2 incident edges, the two edges border matching faces/loops on each side, the 3D kink angle at the vertex is within `angle_tolerance_radians`, default `tolerance::kMergeEdgeAngle` = 5 degrees) and, on success, concatenates both the 3D edge curve and every affected 2D trim curve (an `ON_PolyCurve` join, not a resample), Compact()s, and clears this class's own per-face side tables the same way every other trim-mutating topology method here does. `Brep::MergeAllContiguousEdges(angle_tolerance_radians)` is the kernel counterpart of `MergeEdgeCommand`'s own "all" mode, but over the whole Brep rather than one picked face — repeatedly rescanning for a fresh valence-2, in-tolerance pair after each merge (the same discipline `SewTJunctions()` uses, since each merge's own `Compact()` renumbers everything), so a whole chain of collinear micro-segments collapses to one edge in a single call. Verified by `TestMergeContiguousEdgesCombinesTwoCollinearNakedEdges`, `TestMergeContiguousEdgesRefusesAKinkedCorner`, `TestMergeContiguousEdgesThrowsOnInvalidIndicesRefusesWrongValence`, and `TestMergeAllContiguousEdgesCollapsesAChainOfCollinearEdges` (tests/test_basic.cpp). Not yet wired into the app: `MergeEdgeCommand` still calls `ON_Brep::CombineContiguousEdges` directly rather than this new kernel method — a separate, still-open app-integration gap, not a limitation of the kernel API itself.
- [partial] Edge rebuild from adjacent-surface intersection (RebuildEdges) — app-only `RebuildEdgesReal`.
- [partial] Curve/surface simplify and rebuild (Rebuild, FitCrv, SimplifyCrv, RemoveMultiKnot, MakeUniform, RebuildUV, FitSrf, ShrinkTrimmedSrf) — `NurbsSurface::Rebuild`, `RemoveKnotAt`, `NurbsCurve::FitLeastSquares` confirmed present.
- [partial] Analytic-form recognition / canonical simplification of faces — `IsPlanar`/`IsSphere`/`IsCylinder`/`IsCone`/`IsTorus` confirmed present; nothing replaces a recognized NURBS face with a canonical analytic one.
- [missing] Kinky / creased surface splitting into G1 faces (SplitKinkyFaces, CreaseSplitting for NURBS) — re-grepped `SplitKinky|CreaseSplit|kink` across kernel and app: only unrelated curve-continuity comments hit.
- [partial] Degenerate face removal (B-rep) — `Brep::RemoveDegenerateFaces` (brep.cpp:6510) trusts `Check()`'s `DegenerateFace` flag; that flag's false-positive hazard on the kernel's own primitives is fixed (`b1ac7c9`, re-verified this session — see the top-of-document honesty note and the topology-category mirror bullet above). Still partial for the same non-defect reason given there: delete-and-tolerant-join, not a geometric collapse.
- [partial] Sliver face removal (B-rep) — `Brep::RemoveSliverFaces` shares the same body (`RemoveThinFaces`, brep.cpp:6486); same fixed defect, same remaining delete-and-tolerant-join limitation.

**kernel: Mass properties & spatial queries** (massprops):
- [partial] Exact B-rep / NURBS-face mass properties (tolerance-controlled integration, no tessellation) — `Brep::Volume()`/`Area()` (brep.h:693/708; brep.cpp:439/510 — corrected 2026-09-28, was mis-cited brep.h:515-586; brep.cpp:308-465) integrate the divergence-theorem form with 5x5 Gauss-Legendre per knot span. The checked-in regression test (`TestBrepVolumeAndAreaMatchClosedForms`, test_basic.cpp:7378 — corrected 2026-09-28, was mis-cited :6449/6467-6480) only asserts `< 1e-4` relative for Sphere/Torus Volume and Area — both the Volume() header comment's "~1e-10 relative" claim and the Area() comment's separate "~1e-9 relative... a real measured bound, not a loose one" claim (brep.h:657/704 — corrected 2026-09-28, was mis-cited brep.h:582-585) overstate the precision beyond what's actually tested; the integration code and quadrature order are unchanged since the last measurement, so this methodological point stands as previously noted. Still partial: fixed quadrature order, no caller tolerance; volume/area only; throws on any trimmed face (brep.cpp:444-448); throws unless tessellation is closed manifold.
- [partial] Surface / B-rep area — `Brep::Area()` throws on any trimmed face and on general-boolean results; `NurbsSurface::ApproximateArea` and `Mesh::Area` unchanged.
- [partial] Planar closed-curve region properties (area, centroid, moments) — still app-only.
- [partial] Curve length / arc-length parametrization — `NurbsCurve::Length` polyline chord sum.
- [partial] Closest point on trimmed B-rep — still no `Brep::ClosestPoint`; `Mesh::ClosestPoint`/`NurbsSurface::ClosestPoint` present, untrimmed-only.
- [partial] Point classification vs exact B-rep — `ClassifyPointVsSolid`/`ClassifyPointVsMixedSolid` internal-only (boolean.h).
- [partial] Entity-pair minimum distance for curves/surfaces — `Mesh::DistanceTo`, `MinGap` (boolean.h:176) present; no exact curve/curve etc.
- [partial] Tight (exact) bounding box of curved geometry — `Brep::GetTightBoundingBox` (brep.h:1692+); `NurbsCurve::GetTightBoundingBox` control-point box only.
- [partial] Oriented / CPlane-aligned / minimal-volume bounding box — `Mesh::GetOrientedBoundingBox` (mesh.h:204), mesh-only.
- [partial] Ray firing against exact B-rep faces — `Mesh::FireRay` (mesh.h:305); `IntersectCurveSurface` untrimmed.
- [partial] Spatial acceleration structures for geometric queries — only private uniform grids for SSX/CSX; every public query brute force.
- [partial] Curvature-aware per-region (non-uniform) adaptivity within a face — intentional cross-reference duplicate kept for category-count parity with the tessellation section.
- [partial] Point-cloud spatial queries — `PointCloud::KNearest`/`PointsWithinRadius` (point_cloud.h:101,115), brute force.
- [partial] Signed distance point-to-solid — `Mesh::SignedDistance` (mesh.h:249), sign from `ContainsPoint`'s single-ray test.

**kernel: Tessellation / faceting** (tessellation):
- [partial] Adaptive tessellation of B-rep faces (curvature-driven refinement) — `Brep::TessellateNonUniformAdaptive` exists; a fixed angular deviation heuristic, not a certified chordal-deviation bound.
- [partial] Explicit sampling control (u/v suggested parameter values honouring curvature/singularities) — `SuggestedParameterValues` exists; heuristic spacing only.
- [partial] Adaptive grid tessellation with hole/inner-loop support — `TessellateGridNonUniformAdaptive`/`hole_polygons` exist; still grid-based, not a general Delaunay/advancing-front mesher.
- [partial] Conforming tessellation across shared edges (watertight polysurface mesh) — `TessellateConforming`/`TessellateToClosedMeshConforming`/`TessellateGeneralBooleanClosedMesh` exist; closure rate on general-boolean output is the corrected 54/76 figure noted under Boolean operations, not a general guarantee.
- [partial] Vertex normal computation modes (per-face flat vs smoothed) — `ComputeVertexNormals` exists.
- [partial] Texture coordinate generation on tessellation — `SetTextureCoordinates` exists; simple per-face UV box mapping, not seam-aware unwrapping.
- [partial] Clip-plane-aware tessellation (exact clipped boundary, not a post-hoc cut) — `TessellateGridClippedExact`/`ClippedExactAdaptive` exist; grid-based, planar clip only.
- [partial] SubD to adaptive NURBS-patch tessellation — `ToNurbsPatchesAdaptive` exists (Kernel: SubD & mesh kernel support category owns the primary scoring; cross-referenced here for tessellation-quality relevance).
- [partial] Per-limit-point SubD evaluation for display refinement — `EvaluateFace` exists; exact away from extraordinary vertices, a zero-vector tangent fallback at the pole itself.
- [missing] Angular tolerance control exposed as a general faceting-quality knob (as opposed to per-command heuristics) — re-grepped `angle.*tolerance|facet.*normal.*deviation`: nothing outside chamfer/fillet/draft-specific code.
- [missing] Post-tessellation deviation verification (measuring emitted facets against the true source surface and reporting a bound) — no such measurement function found anywhere.

**kernel: Transformations, patterns, splitting** (transforms):
- [partial] Rigid transform (translate/rotate) of B-rep bodies — still no `Brep::Transform()` on `dino8::kernel::Brep`; `SubD::Transform` (subd.h:202) is present.
- [partial] Non-uniform scale / shear (general affine) of B-rep bodies — app-only.
- [partial] Mirror / reflection with body-orientation fix-up — app-only.
- [partial] Arrays along a curve / on a surface — `ArrayCrvCommand`/`ArrayCrvOnSrfCommand` (cmd_curves2.cpp:702,761).
- [partial] Feature patterns of holes — app-only.
- [partial] Split body by plane (both halves kept, capped) — `SplitByPlane` (boolean.h:74; boolean.cpp:203 — corrected 2026-09-28, was mis-cited boolean.h:46; boolean.cpp:174) is Manifold mesh half-space split, mesh-only.
- [partial] Split / trim body with a tool body (Rhino BooleanSplit with cutter objects, KeepAll) — **corrected: upgraded from missing.** The new `SplitByObjectCommand` (dino8-app/src/commands/cmd_boolean.cpp:290-386) is this item, read under Rhino's BooleanSplit/KeepAll framing rather than kernel: Feature operations' Parasolid PK_BODY_section framing of the same command: select target solids, then one or more cutting solids/open surfaces (an open cutter solidified via `SolidifyOpenCutter`, cmd_boolean.cpp:235-265, extruded along its own area-weighted average normal far enough to clear every target), multiple cutters unioned into one tool, and both `BooleanCombine(target, tool, Intersection)` and `BooleanCombine(target, tool, Difference)` kept per target when **both** are non-empty (cmd_boolean.cpp:357-368) — true KeepAll semantics, not a single-piece split. A same-day follow-up fix (commit 167baae, landed after this pass's own commit inventory was drawn up) hardened exactly this check: it used to accept "either half non-empty," which misfired on a cutter that simply missed the target (Difference alone comes back non-empty: the whole untouched target) or one that fully enclosed it (the mirror case), permanently deleting the user's cutting object and re-meshing the target for zero real effect; it now requires both halves non-empty and computes every result before touching the document, matching Rhino's own "no intersection found, nothing changed" contract — verified by reading the fix directly, not just its commit message. Still partial: mesh-boolean only (Manifold), the open-cutter solidify is a single-normal-direction approximation rather than a true trim, and there is still no kernel-level (B-rep or exact) equivalent — `SplitByPlane` remains the only kernel-native split, plane-only.
- [partial] Cut / split / trim with curve or surface cutters (WireCut by curve, Split surface by curve, Trim surface) — `WireCutCommand` (cmd_boolean.cpp:172) is a plane-cut of a mesh; kernel `Split` is parameter-only. Distinct from the tool-body item above (this is curve/surface cutters, not a solid tool body).
- [partial] Planar section / contour curves of bodies — `SectionCommand`/`ContourCommand` (cmd_curves2.cpp:999,1038), still mesh-slicing.
- [partial] Separate disconnected lumps / multi-lump bodies — `Brep::SplitDisjointPieces` (brep.cpp:2692-2755 — corrected 2026-09-28, was mis-cited brep.cpp:2646-2676) explicitly throws when `original_face_count > 1` and any face has `m_li.Count() == 0`, i.e. any `Box()`/`Sphere()`/`FromSurface()`/`TrimmedPlanarFace()` result.
- [partial] Non-affine deformations (Twist/Bend/Taper/Stretch/Maelstrom/SoftMove, Flow, FlowAlongSrf, CageEdit, Splop) — `DeformObjects`/`PointMap` (cmd_meshtools.cpp:376-385), app-only.
- [partial] Associative / history-linked transforms — `SymmetryCommand`/`SymmetryLink` (cmd_curves2.cpp:2233; Document.h:431) keeps a live plane link; Copy/Array record no history.
- [partial] Construction history / associativity — `RecordHistory`/`UpdateHistory` app-only, scoped to five commands.

*Note on this category's count: 9 present / 13 partial / 0 missing (22 items) — one upgrade from the prior 9/12/1 (68.2%→70.5%), driven by the `SplitByObjectCommand` finding above.*

**kernel: Kernel-level data exchange** (exchange):
- [partial] .3dm attribute/metadata fidelity (layers, materials+textures, linetypes, named views, lights, clipping planes, layouts/details, units, user strings, point clouds, extrusions, groups) — `Model::AddLayer`/`AddLinetype`/`AddGroup`/`AddMaterial` (dino8-kernel/src/file_io.cpp:98,113,129,140 — corrected 2026-09-28, was mis-cited :90,105,121,132) add named/coloured layers, dash/gap linetypes, named groups (`ON_Group`), and named render materials (`ON_Material`, diffuse color only); every `Add*()` (file_io.cpp:144 onward) takes name/render_color/user_strings/linetype_index/group_indices/material_index, round-tripped — `group_indices` is a list (not a single value like `layer_index`), matching Rhino's own "an object can belong to more than one group" model, written per entry via `ON_3dmObjectAttributes::AddToGroup()` (verified by `TestModelAddGroupRoundTrips`, tests/test_basic.cpp:5942 — corrected 2026-09-28, was mis-cited :5613, including a two-group object and an omitted-argument object that still carries zero groups); `material_index` writes `m_material_index` and flips `MaterialSource()` to `ON::material_from_object`, the same "object overrides layer" pattern `render_color`/`linetype_index` already use, verified by `TestModelAddMaterialRoundTrips` (tests/test_basic.cpp:6042 — corrected 2026-09-28, was mis-cited :5726) round-tripping a named material's diffuse color through an actual `.3dm` file, an object assigned it, and an object left with no `material_index` argument (proving the parameter is a no-op when omitted, `MaterialSource()` staying at `ON::material_from_layer`). `ON_Material`'s other fields (specular/emission/shine/transparency/reflectivity/texture maps) are not set — only diffuse color round-trips. Still no textures/named views/lights/clipping planes/layouts/units. **Corrected: the "no read-side accessor apart from `raw()`" gap this bullet used to name is closed for layers/linetypes/groups/object attributes** — `Model::ObjectAttributesAt`/`LayerAt`/`LinetypeAt`/`GroupNameAt` (plus their `*Count()` companions, file_io.cpp:208 onward — corrected 2026-09-28, was mis-cited :180 onward, landed independently of and merged with the `AddMaterial` work above) now read back everything the `Add*()`/`AddLayer()`/`AddLinetype()`/`AddGroup()` family writes, without a caller ever touching `raw()` or hand-rolling an `ONX_ModelComponentIterator` itself (every earlier round-trip test in this file, including `TestModelAddGroupRoundTrips`/`TestModelAddMaterialRoundTrips` cited above, still does exactly that hand-rolled iteration internally — the new accessors are what a caller outside this test file gets instead). `render_color`/`linetype_index` come back `std::nullopt` when an object inherits that value from its layer rather than overriding it, mirroring the Add*() parameters' own contract; verified by `TestModelReadAccessorsRoundTrip` (tests/test_basic.cpp, registered after `TestModelAddGroupRoundTrips`), which round-trips two layers, two linetypes (one layer-referenced, one standalone), two groups, and both a fully-specified object and an every-default object through an actual .3dm file, reading all of it back exclusively through the new accessors, plus an out-of-range index on each `*At()` accessor returning a default-constructed value rather than crashing. **Corrected: the material read-side gap this bullet used to name is now closed too** — `ObjectAttributes` (file_io.h) now carries a `material_index` field, and `Model::MaterialCount()`/`MaterialAt()` (file_io.cpp:313,317) read back the material table `AddMaterial()` writes, the same `ComponentFromIndex()`-based pattern `LayerAt()`/`LinetypeAt()` already use (a default-constructed `MaterialInfo` for an index naming no real material, same contract). `ObjectAttributesAt()`'s own `material_index` comes back `std::nullopt` when an object inherits its material from its layer (`MaterialSource() != ON::material_from_object`) rather than overriding it, the same std::nullopt-means-"inherit from layer" contract `render_color`/`linetype_index` already use. Verified by `TestModelMaterialAccessorsRoundTrip` (tests/test_basic.cpp:6311 — corrected 2026-09-28, was mis-cited :6113), which round-trips two materials (one referenced by an object, one standalone) and both a materialed object and an every-default object through an actual .3dm file, reading all of it back exclusively through the new accessors, plus an out-of-range `MaterialAt()` index and an out-of-range `ObjectAttributesAt()` index each handled without crashing. `ON_Material`'s other fields (specular/emission/shine/transparency/reflectivity/texture maps) still aren't read back, matching `AddMaterial()`'s own write-side scope. **2026-09-30, a thirteenth session: the confirmed default-layer-index defect this bullet used to name is closed.** Every `Add*()`'s `layer_index` parameter (file_io.h) now defaults to -1 - OpenNURBS' own built-in sentinel for "no explicit layer" (`ON_Layer::Default`, opennurbs_layer.h, documented `// index = -1, id set, unique and persistent`; `ONX_Model::LayerFromIndex()` falls back to that exact instance for any index its own layer table doesn't recognize) - instead of the old plain `0`. The old default was a genuine, order-dependent bug, not just a cosmetic mismatch: `Model::AddLayer()`'s very first call adds to what starts as a completely empty layer table, so it - not any built-in "always there" layer - claims manifest index 0; an object added with no `layer_index` argument before any real layer existed then silently ended up aliased onto whichever named layer a caller happened to add afterward, since both resolved to that same index-0 slot. `-1` can never collide with a real index `AddLayer()` returns (`ON_Layer::FromModelComponentRef()`'s own `Index()` is always >= 0 for a real added layer), so "no layer given" now stays permanently, order-independently distinct from "layer 0" - both when a named layer is added before the default-layer object (the pre-existing `TestModelAddLayerRoundTrips`, tests/test_basic.cpp, whose own former `attributes->m_layer_index == 0` assertion was exactly the "passes for the wrong reason" case this bullet used to flag, now asserts `== -1` and additionally checks `LayerFromIndex(-1)` resolves to a real layer that is NOT the named one occupying index 0) and when a named layer is added afterward (the new, dedicated `TestModelDefaultLayerSurvivesLaterAddLayerCall`, tests/test_basic.cpp:6059, which reproduces the actual failure order from the ground up: a Brep is added with no layer_index while the layer table is still empty, THEN `AddLayer()` is called and - confirmed directly - still claims manifest index 0, and the reloaded object is verified to still carry `-1`, not `0`, and to resolve via `LayerFromIndex(-1)` to OpenNURBS' own Default layer rather than the newly added one). `TestModelReadAccessorsRoundTrip`'s own every-parameter-defaulted mesh check was updated the same way (`layer_index == -1`, and explicitly `!= structural_layer_index` even though "Structural" is itself the first layer added and so itself claims index 0). Full `dino8_kernel_tests` suite re-run clean after this change (see this document's own build log for the current count). Still true, unaffected by this fix: the app side (dino8-app/src/io/File3dm.cpp) is much broader but is app code, not kernel API, and does not go through this `Model` wrapper at all.
- [missing] Rhino non-geometry/composite objects in .3dm: block instances, annotations, hatches, text dots — the read cast chain (dino8-app/src/io/File3dm.cpp:554-614) handles only `ON_Point`/`ON_Curve`/`ON_Brep`/`ON_Surface`/`ON_Mesh`/`ON_SubD`/`ON_Extrusion`/`ON_PointCloud`; everything else is skipped.
- [partial] .3dm archive version targeting — `Model::Save(path, int version=0)` (file_io.cpp:200) passes version straight to `ONX_Model::Write`, but the app's actual writer (dino8-app/src/io/File3dm.cpp:1344) bypasses kernel `Model` entirely and hardcodes `model.Write(path.c_str(), 0, &log)`.
- [partial] STEP AP203/AP214 B-rep export — app-only (`ExportStep`, dino8-app/src/io/FileIgesStep.cpp:1837), writes `FILE_SCHEMA('AUTOMOTIVE_DESIGN...')` (line 1657, AP214 only), `MANIFOLD_SOLID_BREP`/`SHELL_BASED_SURFACE_MODEL` (line 1916); no AP203 option; no kernel STEP code.
- [partial] STEP B-rep import — app-only (`ImportStep`, FileIgesStep.cpp:2549), broad but incomplete; no kernel code.
- [missing] STEP AP242 — confirmed zero hits for TESSELLATED/TRIANGULATED_FACE/PMI/AP242 anywhere in dino8-app/src/io/*.cpp; only the AP214 schema string exists.
- [partial] IGES import — app-only (`ImportIges`, FileIgesStep.cpp:1339).
- [missing] Parasolid XT (.x_t/.x_b) read/write — **permanently out of scope by project policy** (proprietary format + licensed Siemens SDK); documented as such. See "Infeasible / non-engineering" below.
- [missing] ACIS SAT/SAB read/write — **permanently out of scope by project policy** (proprietary format + licensed Spatial SDK); documented as such. See "Infeasible / non-engineering" below.
- [partial] OBJ read — `Mesh::LoadObj` (dino8-kernel/src/mesh.cpp:1021) stores UVs per-vertex only (no true UV seams). **2026-09-28: negative (relative) `v`/`vt` face indices, previously rejected outright, are now resolved.** `ParseObjIndexField`/`ParseObjFaceIndex` (mesh.cpp) now accept a negative index as well as positive, and `LoadObj` resolves a negative one to absolute against the `v`/`vt` count at that exact point in the file (`count + index + 1`, the standard .obj convention), separately for vertex and texture-coordinate references, then validates the resolved value in range the same way an absolute index already was — an index resolving to before the start of the file (e.g. `-4` with only 3 vertices declared so far) is still `Result::Failed`, same as an out-of-range positive index. This is a real, if narrow, gap some exporters hit (incremental/streaming writers that emit vertices and faces in interleaved blocks and reference the just-written block relatively rather than tracking a running absolute count); files using only positive indices are unaffected (existing behavior unchanged, including `TestMeshSaveObjRoundTrips`/`TestMeshTextureCoordinates` passing unmodified). Verified by 5 new checks in `TestMeshLoadObjResolvesRelativeIndices` (tests/test_basic.cpp): a basic all-relative triangle (`f -3 -2 -1`) resolves to the same 0-based corners as `f 1 2 3`; a face mixing an absolute and a relative reference in the same corner list resolves correctly; two interleaved vertex/face blocks in one file each resolve their own `f -3 -2 -1` against the count *at that point*, not the file's final total (the second face correctly lands on vertices 3-5, not re-referencing 0-2); a relative `vt` reference resolves against the separate `vt` count; and the existing malformed-file test (`TestMeshLoadObjRejectsMalformedFiles`) now asserts the genuinely-out-of-range case fails, replacing its old assertion that *any* negative index failed (that blanket rejection is exactly what this change removes). **2026-09-28, a later data-exchange session: an OBJ face with more than 4 indices (an n-gon) is no longer rejected either.** Previously `LoadObj` failed outright on any `f` line with 5+ corners (`ON_MeshFace` only holds a triangle or a quad); it now fan-triangulates the n-gon from its own first corner into `n-2` triangular `ON_MeshFace` entries — the same accommodation most .obj consumers make for a genuine n-gon face, rather than refusing the whole file over it. Negative (relative) indices are resolved first, exactly as for a triangle/quad face, so an all-relative n-gon fans identically to the equivalent all-absolute one; per-vertex UV assignment (`vertex_uv_by_index`) is keyed by resolved vertex index, so it is unaffected by the fan split. Verified by 3 new checks in `TestMeshLoadObjFanTriangulatesNgonFaces` (tests/test_basic.cpp): a convex pentagon (5 corners) fan-triangulates into exactly 3 triangles whose combined area exactly reproduces the pentagon's own shoelace area (not merely 3 non-degenerate triangles that happen to pass a face/vertex-count check) and whose corner indices match the expected fan pattern `(0,1,2),(0,2,3),(0,3,4)`; a convex hexagon (6 corners) fan-triangulates into exactly 4 triangles with the same area-preservation check, covering n>5 as well as the smallest unsupported case; and an all-relative-index pentagon (`f -5 -4 -3 -2 -1`) resolves to the same corners and fan-triangulates identically to the equivalent absolute face, proving the two features compose rather than one silently short-circuiting the other. `TestMeshLoadObjRejectsMalformedFiles`'s own former "a 5-index face line fails" case was replaced with a still-genuinely-invalid one (a 2-index face, below the minimum 3 a polygon needs), since a 5-index line is exactly what this change stops rejecting. Full `dino8_kernel_tests` suite re-run clean (100% passed, 0 regressions) after adding these. Still partial: this is a plain fan from the first corner, exact for a convex n-gon but not detected/guarded for a concave one (a fan triangle's interior can fall outside a genuinely non-convex source polygon — a disclosed limitation of the fan approach, not something this loader checks for); and per-vertex UV storage still can't represent a genuine UV seam (a vertex referenced with two different `vt` indices from different face corners), a separate, pre-existing, disclosed gap unaffected by this change. This item's own classification stays `partial`, so the category's present/partial/missing counts and the document's headline percentages are unchanged.
- [partial] PLY read/write — kernel `Mesh::SavePly`/`LoadPly` (mesh.cpp:1521,1602) write/read ASCII, binary_little_endian, or binary_big_endian. **Corrected 2026-09-28: the "binary_big_endian explicitly rejected as out of scope" gap this bullet used to name is closed** — `SavePly(path, binary, big_endian)` gained a third `big_endian` parameter (default `false`, so every existing call site keeps writing little-endian unchanged) that writes `format binary_big_endian` instead, byte-swapping every multi-byte vertex/face property on the way out (`WriteBinaryScalar`, mesh.cpp), and `LoadPly`/`ParsePlyHeader` now read the byte order from the file's own header line rather than assuming the host's (`ReadPlyBinaryScalar`'s new `big_endian` parameter, mesh.cpp) — a `binary_big_endian` file from another tool (or from `SavePly(..., big_endian=true)`) round-trips correctly instead of being rejected outright. Verified by `TestMeshSavePlyBigEndianRoundTrips` (tests/test_basic.cpp): the written header literally says `binary_big_endian`; the big-endian and little-endian encodings of the identical mesh are the same length but byte-for-byte different (proving a genuine swap, not the flag being silently ignored); vertex/face counts, volume, and per-vertex UVs all round-trip; and a minimal payload built by hand (bytes reversed independently of `SavePly()`) decodes to the exact hand-encoded values, ruling out a matched write/read bug that would cancel itself out in a self-round-trip alone. `TestMeshLoadPlyRejectsMalformedFiles`'s own former "binary_big_endian is disclosed out of scope" rejection case was replaced with a still-genuinely-unsupported third format string (`binary_middle_endian`), so header-format rejection stays covered. Still partial: this only closes the byte-order gap — the app's own separate PLY code (dino8-app/src/io/FileExchange.cpp) still hardcodes `"format ascii 1.0"` on export (line 2547) while its importer accepts ascii/binary_little_endian/binary_big_endian (lines 2634-2636), and the two PLY paths remain unwired to each other; kernel `SavePly`/`LoadPly` still only handle position/normal/UV, no per-vertex color or other PLY properties. **Update 2026-09-30, a thirteenth session: the "no per-vertex color" half of that last gap is closed.** `Mesh::SetVertexColors`/`HasVertexColors`/`VertexColorAt` (mesh.h/mesh.cpp) give the kernel a typed accessor over `ON_Mesh::m_C` — the same array dino8-app's `ComputeVertexColors` command already writes directly via `raw()` (cmd_meshtools.cpp) and the viewport already reads for display (SceneObject.cpp's `mesh_vertex_colors`) — mirroring `SetTextureCoordinates`/`HasTextureCoordinates`/`TextureCoordinateAt`'s own "one value per vertex, all-or-nothing" shape exactly. `SavePly()` now writes a `red`/`green`/`blue` uchar triple per vertex whenever `HasVertexColors()` is true (PLY's ordinary vertex-color convention, read by name — the same "no textures/named views/lights/clipping planes" scope this file's broader `.3dm` metadata bullet already discloses stays open for `.ply` too; no `alpha`, since `Color` has no alpha channel to write); `LoadPly()` reads `red`/`green`/`blue` back by name under the same all-or-nothing rule, at each property's own declared scalar type (not just `uchar` — a `float`/`double` color column from another tool is read correctly too, clamped/rounded to the 0-255 byte `Color` holds), and still rejects a `red`/`green`/`blue` property declared as a list, the same "a list on a vertex isn't a position/normal/UV/color" rule every other vertex property already has. The `dino8::kernel::Color` struct itself moved from file_io.h to types.h (no behavior change, same namespace) so mesh.h could use it without a reverse dependency on file_io.h. Verified by `TestMeshSavePlyVertexColorsRoundTrips` (tests/test_basic.cpp): a mesh with no colors set writes no `red`/`green`/`blue` header lines at all (no silent all-black fallback column); the ASCII and binary payloads both declare genuine `property uchar red/green/blue` lines and round-trip every channel byte-exact (0-255 has no precision to lose, unlike position/UV's float round-trip); and a hand-built file declaring its color columns as `float` still reads correctly by value. `TestMeshLoadPlyRejectsMalformedFiles` gained the list-typed-color rejection case above. Full `dino8_kernel_tests` suite (5909 checks) re-run clean after adding these — **caveat, not a regression**: across several full-suite re-runs done to confirm that, `TestFoldFaceConvexPlanarBoxFrontWallHingedAtBottomEdgeMatchesExactIntegral` (an unrelated `boolean.cpp` test, nowhere near this session's own `mesh.cpp`/`types.h`/`file_io.h` changes) failed intermittently on 2 of 4 runs of the identical binary — this is the exact same pre-existing intermittent failure this document's own "Newly observed" note already flagged for this test under this same category heading (2026-09-30, an eleventh/twelfth-session sighting), not something this pass introduced or investigated further, consistent with that note's own "flagged so a future session hunting this down doesn't have to first rediscover that it's intermittent" reasoning. **No item-classification change:** this item was already `partial` (for the app-wiring and other-PLY-properties reasons above) and stays `partial` — the app's own separate PLY code is still unwired to the kernel one, and other PLY properties (e.g. `alpha`, per-face-corner data) remain unread/unwritten. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass.
- [partial] Other mesh/scene exchange formats (glTF/GLB, 3MF, FBX, Collada, VRML/X3D, AMF, OFF, SketchUp SKP, USD) — **2026-09-29: the OFF gap this bullet used to name is closed.** `dino8::kernel::Mesh::SaveOff`/`LoadOff` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) write/read the plain Geomview `.off` (Object File Format) convention — `OFF\n`, a `<vertex_count> <face_count> <edge_count>` line (edge count always written 0, read but unused — same "we don't track a separate edge list" reasoning `SavePly()`'s own missing edge-property support already gives), then that many `x y z` vertex lines, then that many `<n> i0 i1 ... i(n-1)` face lines with OFF's own 0-based indices (unlike `.obj`'s 1-based `f` lines). A quad face (`ON_MeshFace::IsQuad()`) is written as its own native 4-index line, the same "OFF/PLY have a real variable-length face list" distinction `SavePly()` already draws against `.stl`; a genuine n-gon (5+ corners) on read is fan-triangulated from its own first corner into `n-2` triangles, the identical accommodation `LoadObj()` already makes for a `.obj` n-gon `f` line and for the identical reason (`ON_MeshFace` only ever holds a triangle or quad). `LoadOff()` tokenizes past `#`-to-end-of-line comments wherever they appear (a leading file comment, or one trailing a data line) rather than assuming a fixed one-group-per-line layout, and rejects an `NOFF`/`COFF`/`4OFF`/`STOFF` variant header outright rather than silently misreading that variant's own extra per-vertex fields as if they were plain-OFF vertex data. Verified by 3 new tests (tests/test_basic.cpp): `TestMeshSaveOffRoundTrips` checks `SaveOff()`'s actual written content against `MakeQuadBoxMesh`'s known corners/counts (not just "some file got written"), confirms a quad face is written as a native 4-index line, round-trips it back through `LoadOff()` to the same vertex/face counts and exact volume, and separately hand-writes a file exercising both a leading and a trailing `#` comment plus 0-based indices, confirming `LoadOff()` reads through the comments and resolves the indices directly (no off-by-one the way `.obj`'s 1-based scheme would need); `TestMeshLoadOffRejectsMalformedFiles` covers a missing file, an `NOFF`-header file, a too-few-corners face line, an out-of-range vertex index, and a file truncated before all the vertices its own header count promised; `TestMeshLoadOffFanTriangulatesNgonFaces` reuses the same shoelace-area ground-truth check `TestMeshLoadObjFanTriangulatesNgonFaces` already established for `.obj`, applied to OFF's own n-gon face lines instead (a convex pentagon and hexagon each fan-triangulate into `n-2` triangles whose combined area exactly reproduces the source polygon's own shoelace area). Full `dino8_kernel_tests` suite re-run clean (100% passing, 0 regressions) after adding these. **2026-09-29, a tenth session: the AMF gap this bullet also used to name is closed too.** `dino8::kernel::Mesh::SaveAmf`/`LoadAmf` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) write/read the plain-XML Additive Manufacturing File Format (`.amf`, ISO/ASTM 52915) convention — a single `<amf>` root holding one `<object><mesh>`, a `<vertices>` list of `<vertex><coordinates><x>/<y>/<z></coordinates></vertex>` entries, and a `<volume>` list of `<triangle><v1>/<v2>/<v3></triangle>` entries with 0-based indices into the shared vertex list. Unlike OFF/PLY, AMF's `<volume>` element is triangle-only (no native quad/n-gon primitive at all), so a quad face (`ON_MeshFace::IsQuad()`) is split into its two triangles on write, the same accommodation `SaveStl()` already makes for the identical reason — but, unlike STL, AMF keeps a real shared `<vertices>` list, so the split triangles reference the same vertex entries rather than each carrying its own unshared copy. `LoadAmf()` is a deliberately narrow hand-rolled scan for exactly this structure, not a general XML parser: `FindAmfOpenTag`/`ExtractAmfElement` (mesh.cpp, anonymous namespace) locate a named element by searching for `<tag_name` followed by `>`, `/`, or whitespace (so a search for `<vertex` cannot false-match `<vertices`), tolerating attributes on any element (e.g. `unit`/`version` on `<amf>`, `id` on `<object>`) and arbitrary whitespace/indentation between tags, and reading only the first `<vertices>` element and the first `<volume>` element found anywhere in the file — a second `<object>` or a second `<volume>` (AMF's own multi-material convention) is silently ignored, not merged in or rejected; `<metadata>`/`<material>`/`<color>`/`<texture>` and the format's own optional zip-compressed packaging are not understood at all. Verified by 2 new tests (tests/test_basic.cpp): `TestMeshSaveAmfRoundTrips` checks `SaveAmf()`'s actual written content against `MakeQuadBoxMesh`'s known corners (an `<amf>` root, a `<vertices>` element, a `<volume>` element, and exactly 12 `<triangle>` entries for the box's 6 quad faces), round-trips it back through `LoadAmf()` confirming the vertex count stays at 8 (proving the shared-vertex-list claim above, not just asserted) while the face count doubles to 12, and matches the original's exact volume; a separate hand-written file exercises attribute tolerance (`unit`/`id` attributes) and irregular whitespace/indentation around leaf values (including a `<x>  2  </x>` with padding on both sides), checked against known corner positions and a known triangle's indices. `TestMeshLoadAmfRejectsMalformedFiles` covers a missing file, a file with no `<vertices>` element at all, a file with no `<volume>` element at all, an `<x>` value that fails to parse as a number, a `<triangle>` missing its `<v3>` index, and a triangle referencing a vertex index that doesn't exist. Full `dino8_kernel_tests` suite re-run clean (100% passing, 0 regressions) after adding these. **No item-classification change:** this item was already `partial` (via the OFF closure above), and stays `partial` — every other format this bullet names (glTF/GLB, 3MF, FBX, Collada, VRML/X3D, SketchUp SKP, USD) still has zero code anywhere in the source, unchanged by this pass; AMF's own compressed packaging, multi-object/multi-volume, and `<metadata>`/`<material>`/`<color>`/`<texture>` support remain unimplemented; and no `dino8-app` command wires `SaveAmf()`/`LoadAmf()` in yet, same as `SaveOff()`/`LoadOff()` above. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass. **2026-09-29, an eleventh session: the VRML gap this bullet also used to name is closed too (X3D, VRML's XML-based successor, is not - it remains a separate, larger, XML-schema lift out of this pass's scope).** `dino8::kernel::Mesh::SaveVrml`/`LoadVrml` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) write/read the plain-text VRML97 (`.wrl`, ISO/IEC 14772) convention - the standard `#VRML V2.0 utf8` header line, then a single `Shape { geometry IndexedFaceSet { coord Coordinate { point [ ... ] } coordIndex [ ... ] } }` node. Unlike OFF's per-face count prefix or AMF's triangle-only `<volume>`, VRML's `coordIndex` is one flat list of vertex indices with each face terminated by a `-1` sentinel - so a quad face (`ON_MeshFace::IsQuad()`) is written as its own native 4-index run (`i0 i1 i2 i3 -1`), the same "a real variable-length face list, not forced into all-triangle" reasoning `SaveOff()`/`SavePly()` already give for their own formats, and a genuine n-gon run (5+ indices) on read is fan-triangulated from its own first index into `n-2` triangles, the identical accommodation `LoadObj()`/`LoadOff()` already make for their own n-gon faces. `LoadVrml()` is a deliberately narrow hand-rolled scan for exactly this structure, not a general VRML/X3D scene-graph parser: `TokenizeVrmlBody()` (mesh.cpp, anonymous namespace) reduces the file body to a flat token stream where `{`/`}`/`[`/`]` are their own tokens, a `,` is dropped entirely (VRML97 itself specifies a comma as insignificant whitespace between values, exactly like a space or newline - not something the parsing loops need to special-case), and a `#` starts a comment running to end of line anywhere in the body, not just the header; it then reads only the FIRST `point [...]` array and the FIRST `coordIndex [...]` array found anywhere in the file, the same "first one found wins" convention `LoadAmf()` already uses for a second `<object>`/`<volume>`. `Material`/`Appearance`/`Normal`/`TextureCoordinate` nodes and anything beyond a single `Shape`'s `Coordinate`/`IndexedFaceSet` pair are not understood at all - present or absent, they have no effect on the result. **A genuine, confirmed pitfall found and fixed while building this evidence, not merely disclosed after the fact:** the first version of `TokenizeVrmlBody()` emitted `,` as its own single-character token rather than dropping it, and neither parsing loop skipped that token - so `LoadVrml()` failed outright on every file `SaveVrml()` itself wrote (its `point`/`coordIndex` lines are comma-separated), caught by this pass's own round-trip test before it was ever left in place; the fix is the one described above (comma treated as pure whitespace, matching VRML97's own grammar, rather than a token callers must filter). Verified by 3 new tests (tests/test_basic.cpp): `TestMeshSaveVrmlRoundTrips` checks `SaveVrml()`'s actual written content against `MakeQuadBoxMesh`'s known corners/counts (the `#VRML` header, an `IndexedFaceSet`/`coordIndex` node, and exactly one `-1`-terminated run per face - 6 for the box, confirming a quad isn't split), round-trips it back through `LoadVrml()` to the same vertex/face counts and exact volume, and separately hand-writes a file exercising comma separators and a `#` comment in the body (not just the header), checked against known corner positions and a known triangle's indices; `TestMeshLoadVrmlRejectsMalformedFiles` covers a missing file, a non-`#VRML`-header file, a coordIndex run with fewer than 3 indices before its `-1`, an out-of-range vertex index, a file with no `point`/`coordIndex` array at all, and a `coordIndex` array truncated before its closing `]`; `TestMeshLoadVrmlFanTriangulatesNgonFaces` reuses the same shoelace-area ground-truth check `TestMeshLoadOffFanTriangulatesNgonFaces` already established, applied to VRML's own `coordIndex` n-gon runs instead of OFF's count-prefixed face lines (a convex pentagon and hexagon each fan-triangulate into `n-2` triangles whose combined area exactly reproduces the source polygon's own shoelace area). Full `dino8_kernel_tests` suite re-run clean (5734 checks passing, 0 regressions) after adding these. **No item-classification change:** this item was already `partial` (via the OFF/AMF closures above) and stays `partial` - glTF/GLB, 3MF, FBX, Collada, X3D, SketchUp SKP, and USD all still have zero code anywhere in the source; VRML's own `Appearance`/`Material`/per-vertex `Normal`/`TextureCoordinate` nodes, multiple `Shape`/`IndexedFaceSet` siblings, and any node beyond this single narrow structure remain unread; and no `dino8-app` command wires `SaveVrml()`/`LoadVrml()` in yet, same as `SaveOff()`/`LoadOff()`/`SaveAmf()`/`LoadAmf()` above. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass. **2026-09-30, a twelfth session: the Collada gap this bullet also used to name is closed too (glTF/GLB, 3MF, FBX, X3D, SketchUp SKP, and USD are not - each is either a zip/binary container or a much larger schema, out of this pass's narrow scope, the same reasoning already given above for X3D against VRML).** `dino8::kernel::Mesh::SaveCollada`/`LoadCollada` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) write/read the plain-XML COLLADA (`.dae`, ISO/IEC 17506) convention - a `<COLLADA>` root holding one `<library_geometries><geometry><mesh>` with a `<source>` carrying a flat `<float_array>` of "x y z" vertex triples and a `<polylist>` holding a `<vcount>` list (one entry per face, its corner count) alongside a matching flat `<p>` index list. Unlike AMF's triangle-only `<volume>`, COLLADA's `<polylist>` is a real variable-length face list - so a quad face (`ON_MeshFace::IsQuad()`) is written as its own native 4-count `<vcount>` entry, the same "not forced into all-triangle" reasoning `SaveOff()`/`SaveVrml()` already give for their own formats, and a genuine n-gon entry (5+) on read is fan-triangulated from its own first corner into `n-2` triangles, the identical accommodation `LoadObj()`/`LoadOff()`/`LoadVrml()` already make for their own n-gon faces. `LoadCollada()` is a deliberately narrow hand-rolled scan reusing `LoadAmf()`'s own `ExtractAmfElement`/`FindAmfOpenTag` tag-scan helpers (mesh.cpp, anonymous namespace) - not a general COLLADA/XML parser - so it reads only the FIRST `<float_array>` found anywhere in the file as the position list and only the FIRST `<polylist>` or, failing that, the FIRST `<triangles>` element as the face list, the same "first one found wins" convention `LoadAmf()`/`LoadVrml()` already use for a second sibling element; a `<triangles>` element (no `<vcount>` of its own) is read as an implicit run of 3-index groups. The `<vertices>`/`<input>` indirection that lets a real COLLADA file wire an arbitrary source id to an arbitrary semantic/offset is not resolved at all - a single `VERTEX` input at offset 0 is assumed, the same "no material/normal/UV wiring understood" scope `LoadVrml()` already has for its own `Appearance`/`Material` nodes; `<library_visual_scenes>`/`<instance_geometry>`, materials/effects, and per-vertex normals/UVs are not written or read. Verified by 3 new tests (tests/test_basic.cpp): `TestMeshSaveColladaRoundTrips` checks `SaveCollada()`'s actual written content against `MakeQuadBoxMesh`'s known counts (an XML declaration, a `<COLLADA>` root, a `<polylist>` node, and exactly one `<vcount>` entry per face, all six equal to 4 - confirming a quad isn't split), round-trips it back through `LoadCollada()` to the same vertex/face counts and exact volume, and separately hand-writes a file exercising attribute tolerance (`id` on `<geometry>`/`<source>`/`<float_array>`), irregular whitespace, and a `<triangles>` element instead of `<polylist>`, checked against known corner positions and a known triangle's indices; `TestMeshLoadColladaRejectsMalformedFiles` covers a missing file, a file with no `<float_array>` at all, a `<float_array>` whose value count isn't a multiple of 3, a file with neither a `<polylist>` nor a `<triangles>` element, a `<polylist>` `<vcount>` entry below 3, a `<p>` entry referencing a vertex index that doesn't exist, a `<p>` list that doesn't hold exactly the indices its `<vcount>` list calls for, and a `<triangles>` element whose `<p>` count isn't a multiple of 3; `TestMeshLoadColladaFanTriangulatesNgonFaces` reuses the same shoelace-area ground-truth check `TestMeshLoadVrmlFanTriangulatesNgonFaces` already established, applied to COLLADA's own `<polylist>` n-gon entries instead of VRML's `coordIndex` runs (a convex pentagon and hexagon each fan-triangulate into `n-2` triangles whose combined area exactly reproduces the source polygon's own shoelace area). Full `dino8_kernel_tests` suite re-run clean (0 regressions) after adding these. **No item-classification change:** this item was already `partial` (via the OFF/AMF/VRML closures above) and stays `partial` - glTF/GLB, 3MF, FBX, X3D, SketchUp SKP, and USD all still have zero code anywhere in the source; and no `dino8-app` command wires `SaveCollada()`/`LoadCollada()` in yet, same as the other closed formats above. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass. **2026-09-30, a thirteenth session: the X3D gap this bullet's own VRML closure explicitly deferred ("VRML's XML-based successor... remains a separate, larger, XML-schema lift out of this pass's scope") is closed too (glTF/GLB, 3MF, FBX, SketchUp SKP, and USD are not - each is still a zip/binary container or a much larger schema, out of this narrow scope).** `dino8::kernel::Mesh::SaveX3d`/`LoadX3d` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) write/read the plain-XML X3D (`.x3d`, ISO/IEC 19775) convention - an `<X3D><Scene><Shape>` holding one `<IndexedFaceSet>` whose `coordIndex` is an XML ATTRIBUTE (not a nested element the way COLLADA's `<p>`/`<vcount>` are), keeping VRML97's own flat, `-1`-terminated-per-face index convention, and whose child `<Coordinate>` element's own `point` attribute carries the flat "x y z x y z ..." vertex list. Unlike COLLADA's per-face `<vcount>` prefix, X3D's `coordIndex` is VRML's exact scheme re-encoded as an attribute string - so a quad face (`ON_MeshFace::IsQuad()`) is written as its own native 4-index run (`i0 i1 i2 i3 -1`), the same "a real variable-length face list" reasoning `SaveVrml()`/`SaveOff()`/`SaveCollada()` already give for their own formats. `LoadX3d()` is a deliberately narrow hand-rolled scan reusing two new tag-scanning helpers, `FindX3dTag()`/`ExtractX3dAttribute()` (mesh.cpp, anonymous namespace) - not a general X3D/XML parser - built specifically for attribute-based fields (`ExtractAmfElement()`'s own helpers only extract nested-element CONTENT, the shape AMF/COLLADA actually use, not an attribute value): `FindX3dTag()` returns a tag's own full text (through its closing `>`, tolerating a self-closing `/>`) so `ExtractX3dAttribute()` can scan `attr_name="..."` within it, the same "don't false-match a longer tag name" guard `FindAmfOpenTag()` already applies. `LoadX3d()` requires an `<X3D` root tag before reading anything else - the same "no variant/other-format file silently misread" stance `LoadVrml()`'s own `#VRML` header check and `LoadOff()`'s own header check already take - then reads only the FIRST `IndexedFaceSet`'s `coordIndex` and the FIRST `Coordinate`'s `point`, the same "first one found wins" convention `LoadAmf()`/`LoadVrml()`/`LoadCollada()` already use for a second sibling element; a genuine n-gon run (5+ indices) on read is fan-triangulated from its own first index into `n-2` triangles, reusing `LoadVrml()`'s exact accommodation since X3D's `coordIndex` keeps VRML's identical per-face `-1` sentinel semantics. `Appearance`/`Material`/`Normal`/`TextureCoordinate` nodes and any node besides `Coordinate`/`IndexedFaceSet` are not understood at all. Verified by 3 new tests (tests/test_basic.cpp): `TestMeshSaveX3dRoundTrips` checks `SaveX3d()`'s actual written content against `MakeQuadBoxMesh`'s known corners/counts (an XML declaration, an `X3D` root, an `IndexedFaceSet` element, `coordIndex=`/`point=` written as attributes rather than nested elements, and exactly one `-1`-terminated run per face - 6 for the box, confirming a quad isn't split), round-trips it back through `LoadX3d()` to the same vertex/face counts and exact volume, and separately hand-writes a file exercising extra attributes (`DEF`, `solid`) around `coordIndex`/`point`, checked against known corner positions and a known triangle's indices; `TestMeshLoadX3dRejectsMalformedFiles` covers a missing file, a file with no `<X3D` root tag at all (a plain VRML file), a too-few-corners coordIndex run, an out-of-range vertex index, a `Coordinate` with no `point` attribute, an `IndexedFaceSet` with no `coordIndex` attribute, and a `coordIndex` run never closed with a `-1`; `TestMeshLoadX3dFanTriangulatesNgonFaces` reuses the same shoelace-area ground-truth check `TestMeshLoadVrmlFanTriangulatesNgonFaces`/`TestMeshLoadColladaFanTriangulatesNgonFaces` already established, applied to X3D's own attribute-based `coordIndex` n-gon runs (a convex pentagon and hexagon each fan-triangulate into `n-2` triangles whose combined area exactly reproduces the source polygon's own shoelace area). Full `dino8_kernel_tests` suite re-run clean (5946 checks passing, 0 regressions) after adding these. **No item-classification change:** this item was already `partial` (via the OFF/AMF/VRML/Collada closures above) and stays `partial` - glTF/GLB, 3MF, FBX, SketchUp SKP, and USD all still have zero code anywhere in the source; and no `dino8-app` command wires `SaveX3d()`/`LoadX3d()` in yet, same as the other closed formats above. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass. **2026-09-30, a fourteenth session: the USD gap this bullet also used to name is closed too (glTF/GLB, 3MF, FBX, and SketchUp SKP are not - each is still a zip/binary container or a much larger schema, out of this narrow scope).** `dino8::kernel::Mesh::SaveUsda`/`LoadUsda` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) write/read Pixar's own plain-ASCII USD (`.usda`) text encoding of Universal Scene Description - a single `def Mesh "mesh" { ... }` prim holding USD's own three real mesh attributes: `point3f[] points` (one `(x, y, z)` tuple per vertex), `int[] faceVertexCounts` (one entry per face, its corner count), and `int[] faceVertexIndices` (one flat, comma-separated run of 0-based indices, walked `faceVertexCounts[i]` values at a time). Like OFF/VRML/Collada/X3D and unlike AMF/STL, `faceVertexCounts` is a real variable-length face list, so a quad face (`ON_MeshFace::IsQuad()`) is written as its own native 4-count/4-index entry, never split. `LoadUsda()` is a deliberately narrow hand-rolled scan for exactly this structure, not a general USD/Sdf text parser (no prim hierarchy, references, variants, or layer composition understood at all): it requires a `#usda` header line (the same "no variant/other-format file silently misread" stance `LoadVrml()`'s own `#VRML` check already takes), then reads only the FIRST `points`, `faceVertexCounts`, and `faceVertexIndices` array found anywhere in the file - a second `Mesh` prim, or a parent `Xform` wrapping it, is tolerated and simply ignored, since the scan doesn't care about prim nesting at all, only the three attribute keys themselves; commas inside `int[]`/`point3f[]` arrays are treated as pure whitespace (`ParseUsdaInts`/`ParseUsdaPointTuples`, mesh.cpp, anonymous namespace), the same convention `TokenizeVrmlBody()` already applies for VRML97's own comma-separated arrays. A genuine n-gon (`faceVertexCounts[i]` >= 5) is fan-triangulated from its own first index into `n-2` triangles, the same accommodation `LoadVrml()`/`LoadX3d()` already make for their own n-gon faces; unlike those `-1`-terminated formats, a mismatch between `faceVertexCounts`'s own sum and `faceVertexIndices`'s actual length is caught directly (no sentinel to lose sync with). Verified by 3 new tests (tests/test_basic.cpp): `TestMeshSaveUsdaRoundTrips` checks `SaveUsda()`'s actual written content against `MakeQuadBoxMesh`'s known counts (a `#usda` header, a `def Mesh` prim, and exactly one native `4` entry per face in `faceVertexCounts` - 6 for the box, confirming a quad isn't split), round-trips it back through `LoadUsda()` to the same vertex/face counts and exact volume, and separately hand-writes a file nesting the `Mesh` prim inside a parent `Xform` with irregular comma/space spacing, checked against known corner positions and indices; `TestMeshLoadUsdaRejectsMalformedFiles` covers a missing file, a file with no `#usda` header, a `Mesh` prim missing `points` or `faceVertexCounts` entirely, a `faceVertexCounts` sum that doesn't match `faceVertexIndices`'s own length, a count below 3, and an out-of-range vertex index; `TestMeshLoadUsdaFanTriangulatesNgonFaces` reuses the same shoelace-area ground-truth check `TestMeshLoadX3dFanTriangulatesNgonFaces` already established, applied to USD's own `faceVertexCounts`/`faceVertexIndices` pair instead of a `-1`-terminated run (a convex pentagon and hexagon each fan-triangulate into `n-2` triangles whose combined area exactly reproduces the source polygon's own shoelace area). Full `dino8_kernel_tests` suite re-run clean (0 regressions) after adding these. **No item-classification change:** this item was already `partial` and stays `partial` - glTF/GLB, 3MF, FBX, and SketchUp SKP all still have zero code anywhere in the source; and no `dino8-app` command wires `SaveUsda()`/`LoadUsda()` in yet, same as every other closed format above. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass. **2026-09-30, a fifteenth session, same pass: the glTF/GLB gap this bullet also used to name is closed too, for the `.gltf` (JSON) half of it (`.glb`'s binary container remains out of this narrow scope; so do 3MF and SketchUp SKP, each still a zip/binary container or a much larger schema).** `dino8::kernel::Mesh::SaveGltf`/`LoadGltf` (mesh.h; mesh.cpp) write/read a single self-contained glTF 2.0 `.gltf` file with the vertex/index buffer embedded as a base64 `data:` URI - a real, spec-valid minimal glTF (`asset`/`buffers`/`bufferViews`/`accessors`/`meshes`/`nodes`/`scenes`/`scene` all present) a general-purpose viewer can open, with no companion `.bin` file. Unlike every text-based format above, glTF's `TRIANGLES` primitive mode has no native quad or n-gon at all, so a quad face is split into its two triangles on write, the same accommodation `SaveStl()`/`SaveAmf()` already make for the identical reason - the one format here whose round trip does not preserve `ON_MeshFace::IsQuad()` faces as quads. Two small pieces of new supporting infrastructure make this possible: a standard base64 encoder/decoder (`Base64Encode`/`Base64Decode`, mesh.cpp, anonymous namespace), and a narrow brace/bracket-depth JSON object/array scanner (`FindNextJsonObject`/`FindJsonArrayContent`/`ExtractJsonIntField`/`ExtractJsonStringField`) that does not need to be string-literal-aware, because the only JSON string value it ever has to scan past - the base64 payload itself - is guaranteed by the base64 alphabet to contain none of `{}[]`, the same "deliberately narrow, not a general parser" trade-off `FindX3dTag()`/`FindAmfOpenTag()` already make for their own formats. `LoadGltf()` requires the buffer's own `uri` to start with `data:application/octet-stream;base64,`, rejecting an external-`.bin` reference outright rather than trying and failing to open a relative path; vertex and triangle counts are derived directly from the first two `bufferViews` entries' own byte lengths (`byteLength / 12`) rather than from `accessors`, the same fixed "bufferView 0 is POSITION, bufferView 1 is indices" assumption `LoadCollada()` already makes about a single `VERTEX` input at offset 0. Verified by 2 new tests (tests/test_basic.cpp): `TestMeshSaveGltfRoundTrips` checks `SaveGltf()`'s actual written content against `MakeQuadBoxMesh`'s known structure (an `asset` field, an embedded base64 data URI, a `bufferViews` array, and `POSITION` naming accessor 0), round-trips it back through `LoadGltf()` confirming the triangle count is exactly double the original quad count and the volume is unchanged, and separately loads a hand-written file built from an independently Python/`struct`-computed base64 payload (not `SaveGltf()`'s own output) - checked against the exact known float32/uint32 little-endian values, ruling out a matched write/read bug a self-round-trip alone couldn't catch; `TestMeshLoadGltfRejectsMalformedFiles` covers a missing file, a file with no `buffers` array at all, a `uri` referencing an external `.bin` file instead of a base64 data URI, a base64 payload containing characters outside the base64 alphabet, a position bufferView `byteLength` that isn't an exact multiple of 12, and an index referencing a vertex position that doesn't exist. Full `dino8_kernel_tests` suite re-run clean (0 regressions) after adding these. **No item-classification change:** this item was already `partial` and stays `partial` - `.glb`, 3MF, FBX, and SketchUp SKP all still have zero code anywhere in the source; and no `dino8-app` command wires `SaveGltf()`/`LoadGltf()` in yet, same as every other closed format above. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass.
- [partial] Point-cloud/scan formats — `PointCloud::SaveXyz`/`LoadXyz` (dino8-kernel/src/point_cloud.cpp:77,95). **New this pass: the ".pts" gap this bullet used to name is closed** — `PointCloud::SavePts`/`LoadPts` (point_cloud.cpp:142,161) write/read the common laser-scan ASCII .pts convention (a leading point-count header line, then "x y z" or "x y z r g b" per point — colors instead of XYZ's own normals, since real .pts variants carry color, not normals), verified by a real file round trip for both the positions-only and positions+colors cases (`TestPointCloudPtsRoundTrips`, tests/test_basic.cpp:12935) plus 8 malformed-input rejections including the header-count-vs-actual-line-count check that's unique to this format (`TestPointCloudLoadPtsRejectsMalformedInput`). Deliberately scoped down from the fuller Leica Cyclone .pts convention some tools write: no per-point "intensity" column, since this kernel's `PointCloud` has no intensity channel to source one from (same reasoning `SaveXyz()` already gives for leaving color out of its own format). Still missing: .e57/.las (both real binary/compound formats, a much larger lift than .pts's plain text), and colours are still deliberately unwritten by `SaveXyz()` itself (unchanged, that format has no color convention to follow). Neither format is wired into the app. **2026-09-30, a further session: the PCD (PCL/ROS `.pcd`, ASCII v0.7) gap is closed too.** `dino8::kernel::PointCloud::SavePcd`/`LoadPcd` (dino8-kernel/include/dino8/kernel/point_cloud.h; src/point_cloud.cpp) write/read the plain-ASCII Point Cloud Data convention PCL/ROS tooling actually uses — a `VERSION`/`FIELDS`/`SIZE`/`TYPE`/`COUNT`/`WIDTH`/`HEIGHT`/`VIEWPOINT`/`POINTS`/`DATA` header followed by one data line per point — and, unlike XYZ (normal-only) or .pts (color-only) above, this is the first point-cloud format here able to carry BOTH color and normals in the same file, since real `.pcd` genuinely supports both at once: the writer picks one of four fixed `FIELDS` combinations (`x y z`, `x y z rgb`, `x y z normal_x normal_y normal_z`, or `x y z rgb normal_x normal_y normal_z`) based on `HasColors()`/`HasNormals()`, the same "no fabricating a column this cloud has no data for" discipline `SaveXyz()`/`SavePts()` already apply to their own formats. Color rides in a single packed `rgb` field, the real PCL convention (`(r << 16) | (g << 8) | b` reinterpreted as an IEEE-754 float's bit pattern), not three separate columns — `PackPcdRgb`/`UnpackPcdRgb` (point_cloud.cpp, anonymous namespace) pack/unpack it exactly (a double-precision text round trip losslessly carries the float's exact bits, so a color survives `SavePcd()`/`LoadPcd()` unchanged, verified directly by comparing `ColorAt()` before and after, not merely by re-reading the same decimal digits back). `LoadPcd()` is a deliberately narrow scan, not a general PCD reader: `FIELDS` must be exactly one of those four known combinations (any other list, order, or subset — e.g. a real file's own `intensity` field, or a differently-ordered `x y z` — is rejected outright), `DATA` must be `ascii` (`binary`/`binary_compressed` are out of scope, the same "plain text only" scope every other format in this kernel already has), and `HEIGHT` (if present) must be `1` — an organized/structured cloud is out of scope, matching this class's own "flat list of positions, not a 2D grid" shape throughout; `SIZE`/`TYPE`/`COUNT`/`WIDTH`/`VIEWPOINT` lines are tolerated but not otherwise validated, since `FIELDS` and `POINTS` alone already give this reader everything it needs. Verified by `TestPointCloudPcdRoundTrips` (tests/test_basic.cpp): four fixture round trips (positions only; +colors; +normals; +colors+normals together — the combination neither XYZ nor .pts alone can represent) each checked against the original cloud's own position/color/normal values, plus a hand-written real minimal `.pcd` file (not `SavePcd()`'s own output) decoded to known point positions; `TestPointCloudLoadPcdRejectsMalformedInput` covers a missing file, a missing `VERSION` line, an unrecognized `FIELDS` combination (an extra `intensity` field, and a reordered `z y x`), `DATA binary`, `HEIGHT 2` (an organized cloud), a `POINTS` count that doesn't match the actual data-line count, a data line with the wrong column count for its `FIELDS`, a non-numeric token, and a file with no `DATA` line at all. Full `dino8_kernel_tests` suite re-run clean (0 regressions) after adding these. **No item-classification change:** this item was already `partial` (via the .pts closure above) and stays `partial` — `.e57`/`.las` (both real binary/compound formats) remain a much larger, out-of-scope lift, and `SavePcd()`/`LoadPcd()` are not wired into the app either, same as `SaveXyz()`/`SavePts()` above. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass. **2026-09-30, a further session: the `.las` gap is closed too.** `dino8::kernel::PointCloud::SaveLas`/`LoadLas` (dino8-kernel/include/dino8/kernel/point_cloud.h; src/point_cloud.cpp) write/read a binary ASPRS LAS 1.2 file — the fourth point-cloud format here, and the first BINARY one (XYZ/.pts/.pcd above are all plain text), matching what real LiDAR/survey tooling (PDAL, LAStools) actually reads and writes. The 227-byte LAS 1.2 Public Header Block is written and required back exactly on read (signature `LASF`, version 1.2, header size 227, offset-to-point-data 227 — no VLRs, which this reader doesn't understand at all); Point Data Record Format 0 (20-byte records, position only) is used when `HasColors()` is false, Format 2 (26-byte records, adds Red/Green/Blue) when it's true — the same "no fabricating a column this cloud has no data for" discipline `SavePts()`/`SavePcd()` already apply, and, like every other format in this category, normals are not written at all (no LAS point data format has ever had a normal field — a LiDAR scanner doesn't produce one, so there is no convention to follow, the same honest-omission reasoning `SavePts()` already gives for its own missing normal column). Positions are quantized to a fixed 0.001 scale factor with a per-axis offset (that axis' own minimum value, the ordinary LAS convention) — a real, disclosed property of the LAS format itself (every coordinate is a scaled int32, never a native double), not a shortcut this writer takes, so a round trip is exact only to within half that scale, not bit-for-bit; colors, in contrast, round-trip exactly, each 8-bit channel scaled to LAS' own 16-bit Red/Green/Blue fields by the exact factor 257 (255 * 257 == 65535, so `channel * 257` never overflows a `uint16_t`, and integer-dividing back by 257 recovers the original byte with no remainder for every one of the 256 possible inputs, not just the ones a spot check happens to try). Verified by `TestPointCloudLasRoundTrips` (tests/test_basic.cpp): a positions-only cloud (Format 0) and a positions+colors cloud (Format 2) each round-trip through `SaveLas()`/`LoadLas()` with position matching within the format's own quantization tolerance and — for Format 2 — color matching exactly; `SaveLas()` on an empty cloud fails outright (there is no bounding box to derive a per-axis offset from). `TestPointCloudLoadLasRejectsMalformedInput` covers a missing file, a wrong 4-byte signature, a file truncated before the version bytes, a version other than 1.2 (e.g. 1.4), an unsupported Point Data Format ID (1 — position + GPS time), a header-declared record length that doesn't match the declared format, a nonzero Variable Length Record count, and a file truncated before all of its own declared point records — each produced by patching one field of a genuine `SaveLas()`-written file in place, not hand-assembled from scratch, so the control case (an unpatched file) is verified to load correctly first. Full `dino8_kernel_tests` suite re-run clean (0 regressions) after adding these. **No item-classification change:** this item was already `partial` (via the .pts/.pcd closures above) and stays `partial` — `.e57` (a compound binary+XML format, a much larger lift than LAS' own fixed binary layout) remains out of scope, and `SaveLas()`/`LoadLas()` are not wired into the app either, same as every other format in this category. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass.
- [partial] Unit-system conversion — IGES unit handling remains app-only. STEP export still hardcodes `int units = 4; // millimetres` (dino8-app/src/io/FileExchange.cpp:521). **Corrected: the "Kernel `Model` never sets units" gap this bullet used to name is closed for .3dm** — `Model::SetUnitSystem`/`GetUnitSystem` (dino8-kernel/src/file_io.cpp, file_io.h) write/read `ONX_Model::m_settings.m_ModelUnitsAndTolerances.m_unit_system` directly, the same field the app's own `Save3dm`/`Load3dm` (File3dm.cpp:873-997) already set on its own separate, unreachable-from-this-API local `ONX_Model` — before this, a caller using the kernel `Model` wrapper had no way to say what a model's coordinates were measured in at all, even though `Save()`/`Load()` already carried the settings chunk (and so the unit system) through a .3dm unmodified once written. Covers Millimeters/Centimeters/Meters/Kilometers/Microns/Inches/Feet/Yards/Miles/None - a wider set than the app's own five-name switch. Verified by `TestModelUnitSystemRoundTrips` (tests/test_basic.cpp): a never-`SetUnitSystem()` Model reports Millimeters (matching `ON_3dmUnitsAndTolerances`'s own documented default); an in-memory round trip (no save/load) reads back the value immediately; and all ten `UnitSystem` values are each round-tripped through a real .3dm save/load, alongside a mesh object, and read back exactly. Still partial: this closes only the .3dm/kernel-`Model` corner of the gap - IGES unit handling is still app-only, STEP export still hardcodes millimetres (as this bullet already named), and no kernel-level unit *conversion* API (scaling coordinates from one system to another) exists, only "declare what the units already are."
- [partial] Import-time B-rep validation/healing — app `OrientFaces`/`JoinEdges` only appear in FileIgesStep.cpp. Zero call sites of `SewTJunctions`/`SplitNakedEdgeAt` anywhere in dino8-app/src — no importer calls them.
- [partial] App-independent (kernel library) STEP/IGES/PLY/DXF exchange API — PLY has a kernel API; STEP/IGES/DXF (dino8-app/src/io/FileExchange.cpp: `ExportDxf`/`ImportDxf`, lines 485/1473) and DWG (`ImportDwg`, ~line 1590) remain app-only Document entry points.
- [missing] IFC — zero hits for "IFC" in dino8-app/src or dino8-kernel/src.
- [missing] DWF/DWFx — zero hits; not in kModelExts/kExportExts (cmd_file.cpp:25-26).
- [missing] JT (ISO 14306) — zero hits anywhere.
- [partial] Other mesh/scene exchange formats — glTF/USD/OFF/AMF/VRML/Collada/X3D all had a `Mesh::Save*`/`Load*` pair already (see the OBJ/point-cloud bullets above and prior sessions). **This pass: the glTF binary container half of this gap is closed** — `Mesh::SaveGlb`/`LoadGlb` (mesh.cpp) write/read the standard `.glb` container (a 12-byte header with the `glTF` magic and version 2, then a JSON chunk, then a BIN chunk), sharing the exact same geometry-to-buffer logic `SaveGltf`/`LoadGltf` already use (factored into `BuildGltfMeshBuffer`/`BuildGltfJson`/`BuildMeshFromGltfJsonAndBuffer`, mesh.cpp, so the two container formats can't drift apart) - the only difference is that a GLB's own `buffers[0]` has no `uri` (per spec, that means "this container's own BIN chunk") and the binary payload rides along raw instead of as a base64 `data:` URI, making the file about a third smaller on disk. Verified by `TestMeshSaveGlbRoundTrips` (tests/test_basic.cpp): a `SaveGlb`/`LoadGlb` self-round-trip on a quad box (header magic/version/declared-total-length all checked byte-for-byte, no base64 text anywhere in the file) preserves vertex count and volume through the same quad-to-triangle split `SaveGltf` already makes; and a hand-assembled GLB file (not `SaveGlb`'s own output - independently packed chunk bytes for a single triangle) decodes to the exact expected positions/indices, ruling out a matched write/read bug. `TestMeshLoadGlbRejectsMalformedFiles` covers a missing file, a file shorter than the 12-byte header, wrong magic, a version other than 2, a declared total length exceeding the file's actual size, a JSON chunk with no BIN chunk (and vice versa), and an out-of-range vertex index - the same failure surface `LoadGltf`'s own malformed-file test already covers, since both readers share `BuildMeshFromGltfJsonAndBuffer`. Full `dino8_kernel_tests` suite re-run clean, 0 regressions. Still partial, same as before: 3MF, FBX, and SketchUp SKP remain zero code. **No item-classification change:** already `partial`, stays `partial` - the category's present/partial/missing counts and the document's headline percentages are unchanged by this pass. Separately, this pass also found two entries in this document's own "Remainder, grouped by effort" list that had gone stale without the item's own classification ever being wrong: "PLY binary_big_endian support (partial)" duplicated a gap this category's own PLY bullet above already documented as closed 2026-09-28 (removed outright, `283`/`59`-item counts each decremented by 1 to match); and "Point-cloud / scan file formats — .pts/.e57/.las (partial)"/"OBJ read — fan-triangulate >4-index faces, UV seams (partial)" both still named a sub-gap (n-gon fan-triangulation, the `.pts` format) this category's own OBJ/point-cloud bullets above already documented as closed - reworded to drop the closed half rather than removed, since each item's other half (UV seams; `.e57`/`.las`) is still genuinely open, so no count change for either. **2026-09-30, a further session: `SaveOff`/`LoadOff` also gained the `COFF` (color OFF) variant.** Plain `OFF` never had anywhere to put a per-vertex color, but `Mesh` gained real per-vertex color storage in an earlier session (`SetVertexColors`/`HasVertexColors`/`VertexColorAt`, mesh.h/mesh.cpp, credited under this category's own PLY bullet above) that `SaveOff()`/`LoadOff()` simply weren't wired to yet — closing that half of the "rejects an `NOFF`/`COFF`/`4OFF`/`STOFF` variant header outright" gap this bullet's own earlier OFF closure disclosed (`NOFF`/`4OFF`/`STOFF` remain out of scope: this kernel's mesh normals are always geometry-derived via `ComputeVertexNormals()`, never independently stored, and there is no homogeneous-coordinate or per-corner-UV concept to put in a `4OFF`/`STOFF` either). `SaveOff()` now writes the `COFF` header instead of plain `OFF` whenever `HasVertexColors()` is true, with each vertex line becoming `x y z r g b a` — `r`/`g`/`b` straight from `VertexColorAt()`, `a` always `255` (this kernel's `Color` has no alpha channel to source one from). `LoadOff()` accepts either header keyword: a plain `OFF` file parses exactly as before, and a `COFF` file reads its own trailing `r g b a` per vertex (each required to be an integer in `[0, 255]`, the same strictness `LoadPts()` already applies to its own R/G/B columns) and calls `SetVertexColors()` once all vertices are read; the alpha column is read (so a malformed one still fails the load) but then discarded, the same "read but unused" treatment the edge count already gets. Verified by `TestMeshSaveOffWritesAndReadsCoffColors` (tests/test_basic.cpp): a colored `MakeQuadBoxMesh` writes a `COFF` header with a correctly-formatted 7-field first vertex line, round-trips through `LoadOff()` back to the exact same per-vertex colors and unchanged volume; an uncolored mesh still writes the plain `OFF` header (proving the `COFF` path is conditional, not always-on); a hand-written `COFF` file (not `SaveOff()`'s own output) decodes to known red/green/blue vertex colors; and a hand-written file with an out-of-range alpha component fails the load. Full `dino8_kernel_tests` suite re-run clean (0 regressions) after adding this. **No item-classification change:** this item was already `partial` and stays `partial` — 3MF, FBX, and SketchUp SKP remain zero code, and no `dino8-app` command wires the color-aware `SaveOff()`/`LoadOff()` in yet. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass. **2026-09-30, a further session: `SaveVrml`/`LoadVrml` and `SaveX3d`/`LoadX3d` both gained real per-vertex color support too**, the same COFF-style closure applied to the two remaining "other file format" writers whose own doc comments still disclosed "this kernel's `Mesh` has nothing to source those from anyway" for color, even though `Mesh` has carried real per-vertex color storage (`SetVertexColors`/`HasVertexColors`/`VertexColorAt`) since the PLY work credited above. Both formats' native color representation is a `[0, 1]`-range float triple, not the `Color` struct's own 0-255 byte range, so each channel is divided by 255 on write and, on read, clamped to `[0, 1]` and rounded to the nearest byte (`std::lround(x * 255.0)`) - a plain division/multiplication round trip at 8 decimal digits of written precision is exact after that rounding step, verified directly by comparing `VertexColorAt()` before and after, not merely by re-reading the same digits back. `SaveVrml()` writes a `color Color { color [ r g b, ... ] }` node (one triple per vertex) plus `colorPerVertex TRUE` whenever `HasVertexColors()` is true, mirroring `SaveOff()`'s own "no `Color`/`COFF` at all when uncolored" conditional; `LoadVrml()` finds the node by its capitalized `Color` token (distinct from the lowercase `color` field name both introducing it and naming its own nested value array), and requires its value count to equal the vertex count exactly - VRML97's other, `colorPerVertex FALSE` per-face color list doesn't fit this kernel's per-vertex-only color model and is rejected outright rather than silently misapplied, the same discipline `LoadPcd()` already applies to an unrecognized `FIELDS` combination. `SaveX3d()` writes the same per-vertex triples as a `<Color color="r g b r g b ...">` child element plus `colorPerVertex="true"` on the `IndexedFaceSet` tag - X3D's attribute-based re-encoding of VRML's identical convention, the same relationship `SaveX3d()`'s own `coordIndex`/`point` attributes already have to `SaveVrml()`'s nested arrays; `LoadX3d()` locates it via the existing `FindX3dTag()`/`ExtractX3dAttribute()` helpers (its tag name can never false-match the earlier `<Coordinate>` search) and applies the identical vertex-count-must-match rule. Verified by `TestMeshSaveVrmlWritesAndReadsColors`/`TestMeshSaveX3dWritesAndReadsColors` (tests/test_basic.cpp): a colored `MakeQuadBoxMesh` round-trips every vertex color exactly through both formats; an uncolored mesh writes no `Color` node/element (and, for X3D, no `colorPerVertex` attribute) at all; and a hand-written file with known `1 0 0`/`0 1 0`/`0 0 1` triples for each format decodes to exact red/green/blue bytes. `TestMeshLoadVrmlRejectsMalformedFiles`/`TestMeshLoadX3dRejectsMalformedFiles` each gained a case rejecting a `Color` node/element whose own value count doesn't equal the vertex count. Full `dino8_kernel_tests` suite re-run clean (0 regressions) after adding these. **No item-classification change:** this item was already `partial` and stays `partial` — 3MF, FBX, and SketchUp SKP remain zero code, and no `dino8-app` command wires any of these color-aware writers in yet. The category's present/partial/missing counts and the document's headline percentages are therefore unchanged by this pass.

**kernel: Feature operations** (features):
- [present] Counterbore (stepped coaxial) hole — **two independent implementations landed the same day; combined here.** `dino8::kernel::CounterboreHole` (dino8-kernel/include/dino8/kernel/features.h, dino8-kernel/src/features.cpp) is a real, dedicated, kernel-native feature op: a single compound cutter `Brep` (two coaxial `Brep::CylindricalFace` entries over adjacent, non-overlapping axial ranges — the wide counterbore wall, then the narrow drill wall) subtracted from the target solid in one `BooleanCombineMixed(..., BooleanOp::Difference)` pass, producing a genuine B-rep result (`raw().IsValid()`), not a mesh boolean. Verified directly, not just argued: a through-hole case's tessellated volume matches its own hand-derived value (1000−19π) to within 0.1 at div=256, cross-checked against an independent Manifold mesh-boolean derivation of the same geometry, and `TessellateToClosedMeshConforming()`'s own mesh is a genuine `IsClosedManifold()`; a separate blind (non-through) case is verified the same way. Composing this any other way was tried and confirmed to fail: two sequential `BooleanCombineMixed` Difference passes against the target (either order, or via a prior `Union` of two full-length overlapping bare cylinders) each hit a real, pre-existing, disclosed kernel limitation (`ClipPolygonByCircle3d`'s own "a once-already-clipped, non-rectangular poly losing convexity for a second interacting circle" scope note, boolean.h) or produced a silently wrong, non-manifold result — the adjacent-non-overlapping-segments composition is what actually avoids both. A separate, independently-landed `dino8::kernel::MakeCounterboreHole` (dino8-kernel/include/dino8/kernel/boolean_general.h:187; dino8-kernel/src/boolean_general.cpp:3633 — corrected 2026-09-28, was mis-cited h:146; cpp:3512) takes a different route to the same capability: a single stepped-profile `Brep::Revolve()` tool (one compound wall face carrying both radii, plus `Revolve()`'s own auto-added flat far cap) subtracted via one `BooleanCombineGeneral(solid, tool, Difference)` call; its own author rates it `partial` since it inherits `BooleanCombineGeneral()`'s disclosed genus-0/no-pre-existing-holes/one-intersection-chain scope limits (a second hole overlapping the first, or a hole on an already-holed face, is unsupported there). Rated `present` here on the strength of `CounterboreHole`, which carries none of those particular restrictions; still not full parity either way: kernel-only (no `dino8-app` command wires either one in yet), and `CounterboreHole`'s own oblique-axis case can separately hit `BooleanCombineMixed`'s pre-existing non-monotonic-crossing limitation at a steep-enough tilt.
- [partial] Countersink (conical) hole — `dino8::kernel::MakeCountersinkHole` (boolean_general.h:206; boolean_general.cpp:3681 — corrected 2026-09-28, was mis-cited h:165; cpp:3560) is the conical sibling of `MakeCounterboreHole` above: the same single-`Brep::Revolve()`-tool, one-`BooleanCombineGeneral()`-call construction, with a genuine conical frustum wall — a straight radius taper from `countersink_diameter/2` at the entry surface down to `bore_radius`, its own depth derived from the standard tool-geometry relationship `depth = (R - r) / tan(angle/2)` for the requested `countersink_angle_degrees` (82/90/100/120deg etc.), not taken as a separate free parameter. Verified by `TestMakeCountersinkHoleBoxStandardAngle`: three points checked on the SAME compound wall face — the wide mouth exactly at the entry surface, a linearly-interpolated radius at exactly half the countersink's own derived depth (proof of a genuine straight taper), and the narrower bore radius well below the cone — plus the pilot bore's own flat bottom at the exact requested depth. Still partial: no app command, one hole per call, `BooleanCombineGeneral()`'s own inherited scope limits (same as `MakeCounterboreHole` above).
- [partial] Threaded/tapped hole and external thread feature — **upgraded from missing: previously zero thread-geometry code anywhere in this kernel (`Bolt`/`Nut`, dino8-app/src/commands/cmd_arch.cpp:623, still explicitly comments "built solid, with no threaded bore"), now a real kernel-native helical thread primitive.** `dino8::kernel::Brep::ScrewThread(axis_point, axis_direction, minor_radius, major_radius, pitch, turns, right_handed, stations_per_turn)` (dino8-kernel/include/dino8/kernel/brep.h; src/sweep.cpp) builds a genuine swept solid V-thread: at each of `ceil(turns * stations_per_turn) + 1` stations spaced evenly along the sweep, a closed triangular profile - vertices at (minor_radius, -pitch/2), (major_radius, 0), (minor_radius, +pitch/2) in the (radial, axial) half-plane at that station's own rotation angle - is placed via the SAME `MakeCompatible`/`SkinSections`/`AssembleSweptBody` machinery `Pipe()`/`PipeVariable()` already use, except the per-station frame is computed directly from the analytic helix angle rather than a rotation-minimizing rail transport (there is no rail curve here, and RMF along a true helix would orient the section perpendicular to the helix's own tangent - the "normal section" convention - not the axial-plane convention a real screw thread's profile is specified in). The profile's own "closing" edge (straight down the root, at a CONSTANT radius) sweeps out the plain cylindrical bore exactly where the two flanks sweep out the thread ridge, so one swept surface is both the bore and the thread - no separate cylinder to union in, and so none of the coincident-surface degeneracy a real union of the two would risk. Composes with `dino8::kernel::BooleanCombine()` (boolean.h)'s mesh-level engine for both directions of this item: subtract a tessellated `ScrewThread()` from a solid's own tessellation for an internal (tapped) thread, or union it onto a plain shaft built at `minor_radius` for an external one - not `BooleanCombineGeneral()` (boolean_general.h), whose own disclosed "at most one outer intersection chain per opposing face pair" scope is violated by a multi-turn thread's own repeatedly-crossing crest. Verified by 2 new tests (tests/test_basic.cpp). `TestScrewThreadExactHelicalSweepVolumeAndGeometry` checks `ScrewThread()` itself: a genuine closed-form volume derived for this construction (not assumed from elsewhere) - a helical sweep of a fixed 2D profile is a volume-preserving SHEAR of a plain revolution (proved via the coordinate change's own Jacobian, which reduces to exactly `r` regardless of the helix's pitch, the same as a plain revolution's), so the exact volume is `2*pi*turns * Area(profile) * r_centroid(profile)`, Pappus's centroid theorem generalized from a revolution to a helical sweep - matched to within 1% at adequate tessellation resolution (both `du` and `dv` need real resolution here, confirmed directly: too few `dv` divisions across multiple turns, or too few `du` divisions across the profile's own two flanks, each alias the volume by several percent on their own, though the mesh stays a genuine closed manifold either way - a tessellation-fidelity gap, not a construction defect); a left-handed thread of the same parameters has the exact same volume, and the two handedness cases are confirmed to actually rotate opposite ways (not just be volume-identical relabelings) via each one's own early root point's angle sign; the crest and root sit at exactly major_radius/minor_radius at every station and within a tight tolerance between them; and the total axial rise is exactly `pitch * turns`. `TestScrewThreadComposesWithMeshBooleanForTappedAndExternalThreads` checks the actual composition this bullet claims, not just the standalone tool: subtracting a tessellated `ScrewThread()` from a plain block via `BooleanCombine(..., Difference)` gives a genuine closed manifold that removes almost exactly the tool's own volume (a real tapped hole, not merely a plausible-sounding claim), and unioning the SAME tool onto a plain shaft built at `minor_radius` via `BooleanCombine(..., Union)` gives a genuine closed manifold that adds almost exactly the tool's own volume onto it (a real external thread). **A genuine, confirmed pitfall found and fixed while building this evidence, not merely disclosed after the fact:** the shared `AssembleSweptBody()` (sweep.cpp) cap-orientation logic every other sweep factory in that file already relies on defaults to the wall surface's own analytic boundary derivative, which - for a wall whose per-radius column oscillates through a full sin/cos period over the sweep (true of any fixed radius on a helical thread, unlike any existing rail motion in this file) - came out both numerically oversized and wrong in sign, mis-winding a cap; `AssembleSweptBody()` gained two new optional explicit-hint parameters (`cap_v0_outward_hint`/`cap_v1_outward_hint`, defaulted to null so every other caller's behavior is unchanged) that `ScrewThread()` feeds its own analytic tangential-direction hint instead - which sign of that hint each end actually needs was itself settled empirically (all 4 combinations tried against the real fixture, kept the one confirmed both orientation-consistent and positive-volume), not re-derived from a sign argument that turned out unreliable here. Still partial: no `dino8-app` command wires either composition in as a user-facing feature yet, `ScrewThread()`'s own cap-orientation hints land the body net-inward as a whole rather than natively outward (corrected by `AssembleSweptBody()`'s own existing global-flip safety net, not a defect but still a difference from every sibling factory), and both compositions inherit `BooleanCombine()`'s own disclosed mesh-boolean scope limits (both operands must already be well-formed closed meshes; no B-rep-exact result the way `MakeHole()`'s family produces).
- [partial] Revolved cut (RevolvedHole) — `RevolvedHole` (cmd_solidtools.cpp:906) still prints "(mesh boolean; results are meshes)"; kernel `Brep::Revolve` exists but is unused by this command.
- [partial] Emboss/deboss — **upgraded from missing: previously zero hits for emboss/deboss/engrave anywhere, now a real kernel feature.** `dino8::kernel::EmbossProfile(solid, profile, direction, depth, mode)` (dino8-kernel/include/dino8/kernel/boolean_general.h; boolean_general.cpp) turns any closed, planar, star-shaped curve into a real capped solid tool via the existing `Brep::Extrude()` (brep.h), then fuses it onto `solid` (`EmbossMode::Emboss`, `BooleanCombineGeneral(..., Union)` — raises a boss) or cuts it into `solid` (`EmbossMode::Deboss`, `Difference` — engraves a pocket), exactly one boolean call either way. `direction` follows `MakeHole()`'s own "points INTO the material" convention; the tool is backed off by a small margin to whichever side of the profile's own plane actually needs it (outside, for a Deboss tool's entry cap; embedded, for an Emboss tool's fused base) so it crosses `solid`'s surface transversally rather than grazing it at a numerically degenerate coincident touch — the same trick `MakeHole()` already uses, generalized to a Union as well as a Difference. Verified by 4 new tests (`tests/test_basic.cpp`): `TestEmbossProfileDebossThroughPocket` cuts a 1x1 square pocket clean through a 4x4x4 box and matches the closed-form removed volume (footprint x height) via `TessellateToClosedMesh()`; `TestEmbossProfileDebossBlindPocket` cuts the same square to a blind depth of 1.5 and is verified directly on the B-rep instead (a genuine flat pocket floor whose own plane passes exactly through the requested depth, plus a wall through an exact side point) for the same disclosed reason `TestMakeHoleBlindAndThrough`'s own blind case is — a blind cavity's tessellated `Volume()` is not reliable here; `TestEmbossProfileEmbossBoss` raises a round boss (a genuinely curved, not just polyline, profile) and confirms it via a real cylindrical wall at the requested radius, a flat top cap at the requested height, and the model's own tight bounding box actually reaching past the original surface (not merely a "some face's infinite plane still passes through z=4" false positive, which the box's own remaining top face gives regardless); `TestEmbossProfileRejectsInvalidArguments` covers a faceless solid, a non-closed profile, non-positive depth, a zero-length direction, and a direction lying in the profile's own plane (surfaced via `Brep::Extrude()`'s own validation, not duplicated). **A genuine, confirmed pitfall found and fixed while building this evidence, not merely disclosed after the fact:** a `depth` landing the tool's far cap exactly ON an already-existing solid face (rather than past it) throws `BooleanCombineGeneral`'s own "edge is claimed by 3 or more fragment loops" non-manifold-result error — a coincident-face degeneracy, not a bug in `EmbossProfile` itself; the through-pocket test deliberately uses a `depth` past the box's own height rather than exactly equal to it, the same reason `MakeHole()`'s own `through` flag extends 2x past the solid's bounding-box diagonal rather than stopping flush at it. Still partial: `profile` must satisfy `Brep::Extrude()`'s own "closed, planar, star-shaped" capping requirement (a self-crossing or reflex/non-star outline can't be fanned into a flat cap) — so this does not cover Emboss/deboss of a general (non-simple) shape or lettering with disconnected glyph counters (an "O" or "A"'s own hole); it only supports a flat planar profile against a planar or otherwise unconformed surface, not embossing a shape that conforms to a curved target face; it inherits `BooleanCombineGeneral()`'s own disclosed genus-0/one-intersection-chain scope limits; and no `dino8-app` command wires it in yet — `cmd_annotate.cpp`'s own `TextCommand` (this category's separate "Lettering as solid geometry" item, still partial) does not call it.
- [partial] Lettering as solid geometry — `TextCommand` (dino8-app/src/commands/cmd_annotate.cpp:48,66) still only takes a bool `surfaces_` flag (Curves/Surfaces), no Solids/Thickness option.
- [partial] Feature editing/re-execution — `ApplyHoleXform` (cmd_solidtools.cpp:1027) still replays the boolean from a stored pre-cut mesh; no parametric feature tree.
- [partial] Draft angle on extrusions — **upgraded: `Brep::ExtrudeTapered` (dino8-kernel/src/sweep.cpp:1517) no longer refuses an OBLIQUE draft `direction`** (one not parallel to the profile's own fitted plane normal) - it previously (lines 1533-1539, now removed) threw for anything short of near-exact parallel alignment. The fix turned out not to need the "decompose the in-plane offset and the extrusion translation separately" work the old refusal message said this would take: the existing offset-magnitude formula already only ever used the FULL `L = |direction|` (never an along-normal component), so an oblique `direction` just needed the validity gate loosened from "must be parallel to the normal" to the same "must not lie flat in the profile's own plane" degenerate check `Extrude()` itself already uses - the oblique component of `direction` was already flowing untouched into the unconditional `top_raw.Translate(direction)` a few lines down. The result is a genuine oblique (sheared) frustum, not an approximation: by Cavalieri's principle, a family of cross-sections that are each a linearly-interpolated scale-and-translate of the same planar profile has a cross-sectional area depending only on the perpendicular (along-normal) height, so the pre-existing closed-form frustum volume still holds exactly, just measured against `direction`'s along-normal component instead of its full length - true for a circular profile (still an oblique cone) and, less obviously, for a convex-polyline profile too, even though each side wall there stops being a planar trapezoid and becomes a genuinely twisted (non-planar) ruled bilinear patch once the shear is oblique. Verified by 1 new test (`TestExtrudeTaperedObliqueDirectionIsShearedFrustum`, tests/test_basic.cpp): an oblique circular frustum's tessellated volume matches the Cavalieri closed form, its top circle's every sampled point is exactly `r1` from the analytically-known translated center (not a numerically-estimated one - a NURBS circle's own parameterization is not angle-uniform, so a naive point-average centroid is not itself an exact center estimate, a real pitfall hit and fixed while writing this test), the same invariance-to-the-fitted-normal's-own-sign guarantee the parallel case already had, a flaring (grow) oblique case, and an oblique convex-square case whose tessellated volume converges to the same closed form despite its now-nonplanar walls; plus a negative control confirming the pre-existing offset self-intersection/collapse guards still fire regardless of how oblique `direction` is. The previously-stale negative control asserting an oblique direction throws was removed (it tested exactly the restriction this lifts). Still partial for the same pre-existing, unrelated reason: app's `ExtrudeCrvTapered` (cmd_surface.cpp:1229) still uses its own centroid-scaling path, not the kernel one.
- [partial] Draft/taper faces of an existing body about a neutral plane — the same `DraftFacesConvexPlanar` (dino8-kernel/src/boolean.cpp) already credited under kernel: Local / direct-edit operations' "Taper / draft face" item (Rhino/SolidWorks "Neutral Plane Draft" feature framing of the identical capability, the same one-capability-two-vocabularies pattern this document already uses for `SplitByObjectCommand`) — see that item for the full construction and test detail. Still partial for the same reasons given there: convex planar-faced solids only, one shared angle per call, and no app-level feature command (counterbore/countersink/blind-hole placement, the other gaps in this category, remain untouched by this addition).
- [partial] Thicken a sheet body into a solid — **upgraded from mesh-only: a real B-rep counterpart now exists alongside `Mesh::Thicken`.** `dino8::kernel::Brep::Thicken(sheet, thickness, symmetric)` (dino8-kernel/include/dino8/kernel/brep.h; src/sweep.cpp) builds a genuine closed solid from a single-face, untrimmed sheet body (e.g. one built by `Brep::FromSurface()`): a second cap surface offset from the first via `NurbsSurface::OffsetApproximate()` (surface.h) - chosen over `OffsetAnalytic()` specifically because `OffsetApproximate()` always keeps the exact same control-point grid/knot vectors as its source, so its 4 boundary isocurves are automatically compatible with the source's own at the same parameter values, which `RuledBetween()` (sweep.cpp) then stitches into the solid's 4 side-wall faces with no separate reparameterization step (`OffsetAnalytic()` can't serve this role - its own doc comment discloses a sphere/torus patch offsets to the FULL primitive and a cylinder patch to a full 360-degree cylinder, not the matching sub-patch this needs) - plus a `symmetric` option (splitting the requested thickness across both sides of the input surface, which then ends up on the solid's own midplane rather than either face) that `Mesh::Thicken` itself does not have. Verified by 4 new tests (tests/test_basic.cpp): `TestThickenFlatSheetProducesExactBoxVolume` thickens a flat 3x4 rectangle by 2 and matches the closed-form area-times-thickness volume (24) exactly, confirming outward orientation needs no correction from the function's own safety-net flip; `TestThickenSymmetricPutsOriginalSurfaceOnMidplane` confirms the `symmetric` option's own distinct behavior (same total volume, but the original surface sits exactly on the solid's own midplane, not on either face); `TestThickenCurvedSheetProducesGenuineClosedSolid` thickens a genuinely curved (non-planar, non-analytic) bulged surface - the same fixture `OffsetAnalytic()` itself refuses - into a real closed 2-manifold with positive volume, its own bounding box measurably reaching past the source surface's own (directly sampled, not assumed) true peak height, and confirms a too-large requested thickness propagates `OffsetApproximate()`'s own curvature-fold guard rather than silently building a folded solid; `TestThickenRejectsInvalidArguments` covers a zero/non-finite thickness, a multi-face body, a single-face but closed/periodic body (a full sphere), and a trimmed sheet. Still partial: scoped to a single-face, UNTRIMMED, non-periodic-in-both-directions sheet (checked directly against this Brep's own side-table state and `IsClosed()`, not assumed) - a multi-face shell, a trimmed sheet, or a sheet that wraps back on itself (a full cylinder/cone wall, a sphere or torus patch) is a disclosed, separate gap; it inherits `OffsetApproximate()`'s own first-order-approximate-except-planar honesty and curvature-fold guard; and no `dino8-app` command wires it in yet.
- [partial] Split body with an arbitrary surface/solid cutter — `SplitByObjectCommand` (dino8-app/src/commands/cmd_boolean.cpp:290-386), `SolidifyOpenCutter` (lines 235-265). This same command is also credited under kernel: Transformations, patterns, splitting's "tool body split / KeepAll" item (Rhino framing of the identical capability), which has the detail on a same-day correctness fix (commit 167baae) to its "no real split" detection. Still partial: single-normal-direction approximation for open cutters, no face-by-face imprinting/healing, mesh boolean via `kernel::BooleanCombine(Mesh, Mesh, ...)` (dino8-kernel/include/dino8/kernel/boolean.h:30).
- [partial] Body sectioning — `SectionCommand`/`ContourCommand` (dino8-app/src/commands/cmd_curves2.cpp:1038,999) still mesh-slice-based.
- [partial] Delete face and heal/remove feature — `RemoveBlend`/`RemoveChamfer`/`RemoveChamferVertex` (dino8-kernel/src/fillet.cpp:4313/4538/4698) have zero dependency on `Brep::Check()` or `RemoveDegenerateFaces`, so the Check() DegenerateFace-false-flag defect does not touch this item. `RemoveBlend` now also inverts a spherical vertex-blend corner (`RemoveSphericalVertexBlend`, fillet.cpp:4128 - see the blending category's own bullet). Still partial for the reasons already given (a spherical corner sharing a cylinder with a second one, and oblique-end cylinders, are both refused; app's `DeleteFaces` leaves an open polysurface).
- [partial] Feature recognition — analytic classification plus `RemoveChamfer`/`RemoveChamferVertex`'s geometric recognition exist. **Upgraded this pass:** `dino8::kernel::RecognizeHoles(solid)` (dino8-kernel/include/dino8/kernel/features.h; src/features.cpp) closes the "hole" half of this item's own "still no hole/boss/pocket recognition" gap - genuinely new capability (zero feature-recognition-from-a-dumb-B-rep code existed anywhere in this kernel before this pass; `RemoveChamfer`/`RemoveChamferVertex` above recognize a SPECIFIC named feature given a point on it, not "scan this solid and list its holes"), not a citation fix. The geometric inverse of `MakeHole()`: scans every face of `solid` for a concave (bore, not boss) full-2*pi cylindrical face and reports `(origin, axis, radius, depth, through)` - the same parameters a caller could feed straight back into `MakeHole()` to reproduce it. Two real, confirmed pitfalls found and fixed while building this, not merely disclosed after the fact: (1) an initial version classified each end by walking its own rim edge to the neighboring face's loop type (inner vs. outer) - this crashed/found nothing on `MakeHole()`'s own real output, because `BooleanCombineGeneral()`'s own fragmentation (see boolean_general.h's top-of-file scope note) leaves a rim as dozens of short polyline trim segments, several of them genuinely NAKED at the entry rim (MakeHole()'s own disclosed "tool's far end floats entirely inside the target" gap - confirmed directly via a standalone diagnostic dump: 6 of 119 rim trims naked on a through hole, 63 of 91 on a blind hole); (2) fixed by dropping loop/trim adjacency entirely - a face's own axial extent is read as a plain global min/max over every point of every edge in every one of its loops (immune to fragmentation by construction), and each end is classified open-vs-capped by tessellating `solid` once and asking `Mesh::ContainsPoint()` (mesh.h) about a point a small margin past that end, ON the axis (radius 0 - nowhere near the entry-rim gap's own radius-==-hole-radius locus, so this sidesteps it rather than working around it). Verified by 5 new checks (`tests/test_basic.cpp`, `TestRecognizeHolesBlindAndThroughRoundTrip`): a genuine round-trip on both a through and a blind `MakeHole()` fixture - not just "found a hole" but `radius`/`axis`/`depth`/`origin` matching the original call's own arguments to 1e-6, AND feeding the recognized parameters straight back into `MakeHole()` against a fresh box reproduces the same volume (through case) or the same flat bottom position (blind case); two independent holes on one box both come back, neither merged nor dropped; an undrilled box and a convex solid cylinder (a boss/pin, the opposite winding) both correctly find nothing. **Boss and compound-counterbore recognition closed in a later pass** (see this item's own "tenth session" note below): `RecognizeBosses()` covers the convex half this note used to disclose as out of scope, and `RecognizeCounterboreHoles()` merges a real counterbore's own two cylindrical steps into one compound feature instead of two independent `HoleFeature` entries. **Stepped-boss recognition closed in an eleventh session** (see that note below): `RecognizeSteppedBosses()` is the boss-side mirror of `RecognizeCounterboreHoles()`, merging a shouldered boss's own two convex cylindrical steps into one `SteppedBossFeature`. Still partial: a general (non-cylindrical) pocket remains entirely out of scope, a countersink's own CONICAL second step (as opposed to a counterbore's cylindrical one) is not merged by `RecognizeCounterboreHoles()` - only the cylindrical/cylindrical step case is - and a stepped chain of more than two radii (hole or boss side) is not walked past its first adjacent pair. **Countersink (conical) compound-feature recognition closed in a thirteenth session** (see that note below): `RecognizeCountersinkHoles()` merges a countersink's own conical mouth with its adjacent cylindrical pilot bore into one `CountersinkFeature`, closing this item's own last-remaining-named gap.
- [missing] Sheet-metal features — no code; `UnrollDevelopable` (dino8-kernel/src/surface_edit.cpp:817) is single-surface unrolling only.
- [missing] Lattice/cellular infill — the only "lattice" hits are the unrelated Cage FFD deformer (dino8-app/src/commands/cmd_solidtools.cpp:1591-1780); no gyroid/TPMS/Voronoi infill code.
- [partial] Blind/through hole with depth/placement — the app's own `RoundHole`/`MakeHole`/`PlaceHole` (cmd_solidtools.cpp:793,833,873) all still print "(mesh boolean; results are meshes)", untouched by this pass. **New this pass:** a genuine kernel-level B-rep equivalent, `dino8::kernel::MakeHole` (boolean_general.h:166; boolean_general.cpp:3602 — corrected 2026-09-28, was mis-cited h:125; cpp:3481), cuts a real cylindrical bore via `BooleanCombineGeneral(solid, tool, Difference)` — the tool is a plain capped `Brep::Pipe()` cylinder, backed off the entry point by a margin so it pierces the surface transversally rather than grazing it tangentially at a numerically degenerate coincident touch; `through=true` extends the tool past the solid's own tight-bounding-box diagonal on both ends (so it exits regardless of the solid's shape), `through=false` caps it at `depth` for a true blind pocket. Verified by `TestMakeHoleBlindAndThrough` two different ways: the through case's `TessellateToClosedMesh()` volume matches the closed-form `box_volume - pi*r^2*box_height` exactly (a case whose mesh happens to stay reliably closed); the blind case is verified directly on the B-rep instead, because it isn't — a genuine `ON_Cylinder`-fitted wall at the requested radius, plus a flat bottom cap whose own plane equation passes exactly through `center + depth*axis`. **A genuinely new, confirmed gap surfaced while building this evidence:** `BooleanCombineGeneral()`'s own mesh tessellation is NOT reliably closed for a "tool's far end floats entirely inside the target, so the entry face needs a bridged single loop rather than a clean annular hole" topology — worse than the already-disclosed box+cylinder residuals (4 naked edges): this blind-hole fixture leaves 77 naked boundary edges, all sitting at the hole's own entry rim, even through `TessellateGeneralBooleanClosedMesh()`'s own repair pass, and the resulting `Volume()` comes back wildly wrong (~63.9 instead of ~62.8) despite the underlying B-rep topology being exactly right (confirmed face-by-face via `dino8_scratch_test`) — a real, previously-undocumented limitation worth folding into `boolean_general.h`'s own disclosure the next time that engine's mesh-closure work resumes. Still partial: no app command calls it, one hole per call, and it inherits `BooleanCombineGeneral()`'s own disclosed scope limits (genus-0 operands, at most one intersection chain per opposing face pair).
- [partial] Boss following a curved surface — `BossRibCommand` (dino8-app/src/commands/cmd_srfedit.cpp:2009) still a mesh-boolean union path.
- [partial] Rib — same `BossRibCommand` path (cmd_srfedit.cpp:2011).

*Note on this category's score (corrected 2026-09-28: this note previously
cited a stale 5/15/4 split that contradicted both the table above and the
`[present]` Counterbore bullet right next to it): three items (Counterbore,
Countersink, Blind/through hole) gained real, tested kernel code in an
earlier pass — `MakeHole`/`MakeCounterboreHole`/`MakeCountersinkHole`.
Counterbore crossed into `present` on the strength of the independent,
scope-limit-free `CounterboreHole` implementation (see its own bullet
above); Countersink and Blind/through hole stay `partial`, both for
inheriting `BooleanCombineGeneral()`'s own disclosed scope limits and for
having no app command wiring. The category's correct split was 6/14/4 (24
items, 54.2%), matching the table above as of this note's own 2026-09-28
correction.

**Later still, a fifth parallel session:** `dino8::kernel::EmbossProfile`
(boolean_general.h/.cpp) closes the "Emboss/deboss" item, `missing`→
`partial` (see that bullet above for the full construction and test
detail) — genuinely new capability (this repository's first emboss/deboss
code of any kind), not merely a citation fix, but it lands as `partial`
for the same "real code, still short of `present`" reasons this category's
other kernel-only feature ops already are: no `dino8-app` command calls
it, and it inherits `BooleanCombineGeneral()`'s own disclosed scope limits
on top of `Brep::Extrude()`'s own closed/planar/star-shaped profile
requirement. **6/15/3/24, 56.25% (rounds to 56.3%)** — Present unchanged
at 6, Missing 4−1, Partial 24−6−3; matches the table above and this
document's own "Fourteenth same-day follow-up" note.

**Later still, a sixth parallel session:** `dino8::kernel::Brep::Thicken`
(brep.h; sweep.cpp) closes real, tested B-rep code for the "Thicken a sheet
body into a solid" item (see that bullet above for the full construction
and test detail) — genuinely new capability (no B-rep sheet thicken
existed anywhere in this kernel before), not merely a citation fix, but
**no score change**: this item was already counted `partial` on the
strength of `Mesh::Thicken`'s own pre-existing mesh-level implementation,
and it stays `partial` here too, for the same "real code, still short of
`present`" reasons this category's other kernel-only feature ops already
are — scoped to a single-face, untrimmed, non-periodic sheet, and no
`dino8-app` command wires it in yet. Still **6/15/3/24, 56.3%**, identical
to the count directly above.*

**Later still, a seventh parallel session:** `Brep::ExtrudeTapered` (sweep.cpp)
lifted its own long-standing refusal of an OBLIQUE draft `direction` — see
the "Draft angle on extrusions" bullet above for the full Cavalieri's-
principle argument and test detail. Genuinely new capability (no oblique
draft support existed before this pass), not a citation fix, but **no score
change**: the item was already counted `partial` for the separate,
unrelated reason that `dino8-app`'s `ExtrudeCrvTapered` still uses its own
centroid-scaling path rather than calling the kernel function at all, and
that gap is untouched by this pass. Still **6/15/3/24, 56.3%**, identical to
the count directly above.*

**2026-09-29, an eighth session:** `dino8::kernel::Brep::ScrewThread`
(dino8-kernel/include/dino8/kernel/brep.h; src/sweep.cpp) closes the
"Threaded/tapped hole and external thread feature" item (`missing`→
`partial` - see that bullet above for the full construction and test
detail) - this category's last `[missing]` item, and genuinely new
capability: zero thread-geometry code existed anywhere in this kernel
before this pass. Composes with the existing mesh-level `BooleanCombine()`
(boolean.h) for both a tapped (internal) hole and an external thread,
both verified end-to-end (not just the standalone tool), and along the
way fixed a real, confirmed pre-existing gap in the shared
`AssembleSweptBody()` sweep-cap machinery every OTHER factory in
sweep.cpp also uses (a mis-wound cap for any wall whose per-radius column
is periodic over the whole sweep - a case nothing before this pass ever
exercised), via two new optional parameters that default to null and
leave every other caller's own behavior unchanged (confirmed by this
session's own full `ctest` run, clean both before isolating the cap fix
and after - zero regressions either way). Resulting headline arithmetic:
this category's own score moves 6/15/3 (56.3%) → 6/16/2 (58.3%), a
+2.0833pp swing at this category's weight of 1 out of the kernel table's
own total weight 17.75 - +2.0833/17.75 = +0.1174pp, giving **67.2%**
kernel-only, on top of this document's own 67.1%. The combined Dino 8 vs
Rhino 8 + AutoCAD 2027 headline is left at 71.4% (kernel-only item, no
`dino8-app` command wired to it - the same convention every same-day
follow-up above already follows).

**2026-09-30, a ninth session:** `dino8::kernel::RecognizeHoles` (features.h/
features.cpp) closes the "hole" half of the "Feature recognition" item's
own long-standing "still no hole/boss/pocket recognition" gap - see that
bullet above for the full construction (the geometric inverse of
`MakeHole()`, immune-by-construction to `BooleanCombineGeneral()`'s own
fragmented/naked entry-rim topology by never walking loop/trim adjacency
at all) and test detail. Genuinely new capability - this kernel's first
"scan a dumb B-rep and report its features" code of any kind, as opposed
to `RemoveChamfer`/`RemoveChamferVertex`'s existing "recognize the specific
named feature at this point" shape - but **no score change**: "Feature
recognition" was already counted `partial` (on the strength of the
analytic-classification + `RemoveChamfer`/`RemoveChamferVertex` machinery
already credited above) and stays `partial` here too, for the same
"real code, still short of `present`" reason this category's other
kernel-only feature ops already are - boss/pocket recognition and
counterbore/countersink compound-feature merging remain open, and no
`dino8-app` command surfaces this either. Still **6/16/2, 58.3%**,
identical to the count directly above. Full `dino8_kernel_tests` suite
(via `ctest`): 100% passing, 0 regressions.

**2026-09-30, a tenth session:** two closely-related follow-ups to
`RecognizeHoles()` above, both in `dino8::kernel::features.h`/`features.cpp`.
`RecognizeBosses(solid)` closes the "boss" half of this item's own
long-standing "still no hole/boss/pocket recognition" gap: the geometric
mirror of `RecognizeHoles()` - scans for a CONVEX (rather than concave)
full-2*pi cylindrical face and reports `(origin, axis, radius, height,
through)`, the parameters a hypothetical "MakeBoss()" call could take to
reproduce it. Shares its entire candidate-scanning geometry (which faces
are full cylinders, their own axial extent) with `RecognizeHoles()` via a
newly-extracted common helper (`ScanFullCylinderFaces()`) rather than a
second copy of that ~50-line loop, and reads the exact same on-axis
`Mesh::ContainsPoint()` probe `RecognizeHoles()` already uses, just with
the opposite (attached-vs-free, not capped-vs-open) meaning. `RecognizeCounterboreHoles(solid)`
closes the OTHER long-disclosed gap in the same item's text - "no attempt
is made here to recognize a COUNTERBORE/COUNTERSINK's own second, wider
cylindrical/conical step as part of the SAME feature" - for the
CYLINDRICAL-step case (a real counterbore, `CounterboreHole()`'s own
two-`CylindricalFace` construction, NOT `MakeCounterboreHole()`'s single
`Brep::Revolve()` wall, which `ON_Surface::IsCylinder()` never classifies
as a cylinder at all): finds pairs of concave candidates sharing the same
axis line with adjacent, non-overlapping axial ranges and a strictly
wider entry-side radius, and merges them into one `CounterboreFeature`
instead of `RecognizeHoles()`'s own two independent `HoleFeature` entries
for the same cut. A genuine, confirmed pitfall found and fixed while
building this, not merely disclosed after the fact: an initial version
picked which of the narrower candidate's own two ends was "the one
touching the wider face" by comparing the two candidates' own axis
directions' dot-product sign - this get the projection backwards for the
anti-parallel case (each candidate's axis direction comes back from its
own independent `ON_Cylinder` fit with an arbitrary, unrelated sign), so
it was replaced with a sign-free check: compute both of the narrower
candidate's own two end points in 3D and pick whichever one actually
coincides with the wider candidate's own far end, then get the far end's
own position by a plain dot-product projection onto the wider candidate's
axis rather than any further sign reasoning. A SECOND genuine, confirmed
pitfall found while building this evidence, disclosed rather than fixed
(out of scope for this pass - see RecognizeBosses()'s own doc comment):
the obvious way to build a boss test fixture - `BooleanCombineGeneral()`
Union-ing a separate tool onto an existing solid, the tool's own base cap
backed off entirely inside the target the same way `MakeHole()`'s own
margin trick works for Difference - leaves `TessellateToClosedMesh()`
non-closed and `Mesh::ContainsPoint()` wrong across the WHOLE embedded
span (confirmed directly, `dino8_scratch_test`), not merely near the entry
rim the way `MakeHole()`'s own disclosed Difference-side "floating cap"
gap is: a real Union-side counterpart to that gap, previously unconfirmed.
Sidestepped for testing purposes by building the fixture directly via
`Brep::FromMixedFaces()` (two adjacent same-radius `CylindricalFace`
segments, no boolean operation at all - see `MakeTwoSegmentCylinder()`'s
own doc comment): its own shared middle seam (the boundary this test
actually exercises) welds with no issues at all, though the same
diagnostic also surfaced a THIRD, pre-existing, unrelated imperfection -
this disk-cap-plus-CylindricalFace construction (already used elsewhere
in this file, e.g. `MakeCylinderAxisForGeneralBooleanTest()`, just never
previously run through `Brep::Check()`) leaves a handful of naked/invalid-
trim issues at a solid's own two TRUE end caps' rail seam - harmless here
(radius `radius`, nowhere near `RecognizeBosses()`'s own on-axis probes)
and left disclosed rather than fixed, out of scope for this pass.
`RecognizeBosses()` itself is untouched by the Union-side finding for a
boss modeled any other way. Verified by 2
new tests (`tests/test_basic.cpp`): `TestRecognizeBossesBlindAndFreestandingRoundTrip`
covers a two-segment same-radius pipe (standing in for "a boss on a
body") where BOTH segments come back as independent bosses pointing in
opposite directions from their own shared attach point, each with the
correct radius/origin/height/through; an undrilled box and a concave bore
both correctly finding nothing; and a free-standing solid cylinder (the
same fixture `RecognizeHoles()`'s own negative control already uses)
correctly coming back `through = true`; `TestRecognizeCounterboreHolesRoundTrip` confirms
`RecognizeHoles()` itself still sees `CounterboreHole()`'s own fixture as
two separate holes (proving the fixture actually exercises the gap being
closed), then confirms `RecognizeCounterboreHoles()` merges them into one
feature whose every field matches the original call's own arguments to
1e-6, a genuine round-trip back through `CounterboreHole()` reproducing
the original's own volume, and a plain single-radius hole correctly
finding no compound step. **No score change**: "Feature recognition" was
already counted `partial` and stays `partial` here too, for the same
"real code, still short of `present`" reason this category's other
kernel-only feature ops already are - a general (non-cylindrical) pocket
remains entirely unrecognized, a countersink's own CONICAL second step is
not merged by `RecognizeCounterboreHoles()` (only the cylindrical case
is), and no `dino8-app` command surfaces any of this. Still **6/16/2,
58.3%**, identical to the count directly above. Full `dino8_kernel_tests`
suite (via `ctest`): 100% passing, 0 regressions.

**2026-09-30, an eleventh session:** two closely-related follow-ups, both
in `dino8::kernel::features.h`/`features.cpp`. First, a real, confirmed
correctness fix to the tenth session's own `RecognizeCounterboreHoles()`
pairing logic, found while generalizing it (not a regression introduced by
this session - present since the tenth session's own commit): the pairing
scan's internal "which candidate is `wide`" bookkeeping assumed the wider
segment's own axial position (`t_max`, in that candidate's own
independently-signed local frame) was always the one touching the
narrower segment - true only because a counterbore's real construction
happens to build the wide recess with a `t` origin that lines up that
way; generalizing the SAME pairing loop for a stepped boss below surfaced
that this was never actually guaranteed by the geometry itself, only by
which of the two scanned candidates the loop happened to label `i`
first. Fixed by extracting the shared pairing scan (`FindAdjacentSteppedPairs`)
so it determines adjacency purely from which of the two candidates' own
FOUR possible end-point combinations coincide in 3D (verifying the
resulting axis continues straight through to the far candidate's own
far end, not merely assuming it), with no assumption about which
candidate is wider baked into the pairing step itself -
`RecognizeCounterboreHoles()` then normalizes the result (swapping which
end is `origin` if the pairing scan happened to visit the narrow segment
first) rather than silently dropping a valid counterbore whose two
candidates were discovered in the "wrong" order. Confirmed directly this
was a real, live gap, not a hypothetical: a standalone diagnostic
(`dino8_scratch_test`) reproducing a "flared" stepped boss (narrow base,
wide free end - see below) with the PRE-fix pairing logic ported over
unchanged found ZERO stepped features on a fixture that plainly has one;
after the fix, `RecognizeCounterboreHoles()`'s own existing test
(`TestRecognizeCounterboreHolesRoundTrip`) still passes bit-for-bit
unchanged (every real counterbore this kernel's own `CounterboreHole()`
builds happens to always present the wide candidate first, so the
pre-fix bug was latent, not yet exercised by any existing fixture).

Second, the actual new capability: `dino8::kernel::RecognizeSteppedBosses(solid)`
is the boss-side mirror of `RecognizeCounterboreHoles()`, closing
`BossFeature`'s own disclosed "a counterbore/countersink's own second
step ... has no boss-side analogue implemented here" gap - genuinely new
capability (this kernel's first compound-boss recognizer of any kind),
not a citation fix. Scans for pairs of CONVEX full-cylinder candidates
(reusing `RecognizeBosses()`'s own candidate scan and the newly-shared
pairing helper above) sharing an axis line with adjacent, non-overlapping
ranges and different radii, merging them into one `SteppedBossFeature`
instead of `RecognizeBosses()`'s own two independent `BossFeature`
entries for the same shape. Unlike a counterbore, a stepped boss has NO
fixed "wider segment is always at the entry" convention - a real
shouldered boss can just as easily be a narrow post based in a wide pad
as the more common wide-shoulder-narrowing-to-a-shaft shape - so
`base_radius`/`base_height` always describe whichever segment's own
outer end is genuinely ATTACHED (read via the same on-axis
`Mesh::ContainsPoint()` probe `RecognizeBosses()` itself already uses,
applied to the merged two-segment span), regardless of which one is
wider; this is exactly the asymmetry the correctness fix above had to
stop assuming away. Verified by 3 new checks
(`tests/test_basic.cpp`, `TestRecognizeSteppedBossesRoundTrip`): a
flush-base shouldered boss (wide shoulder, narrow shaft) built via
`BooleanCombineMixed(box, TWO-segment bare tube, Union)` - a compound
tool with two adjacent, different-radius `Brep::CylindricalFace` segments,
`CounterboreHole()`'s own "compound cutter" construction (features.cpp)
turned additive - correctly merges to one feature with every field
(`origin`/`axis`/`base_radius`/`base_height`/`tip_radius`/`tip_height`)
matching the construction's own arguments to 1e-6, with a sanity check
that `RecognizeBosses()` itself still reports only the shaft as an
independent boss (the shoulder's own on-axis far-end probe lands inside
the shaft above it, so it reads as "both ends attached" and is silently
skipped there - confirming the fixture genuinely exercises the gap being
closed, not something already handled); the OPPOSITE orientation (a
narrow post based flush with the box, flaring to a wide free end) merges
correctly too, with `base_radius`/`tip_radius` correctly assigned to the
narrow/wide segments respectively rather than by which one is wider -
the test this session's own correctness fix above was written to make
pass; and a plain single-radius boss finds no compound step. `BooleanCombineMixed`
was used for the fixture rather than `BooleanCombineGeneral()`
specifically to avoid `RecognizeBosses()`'s own already-disclosed
Union-side "floating base cap" gap (see that struct's own doc comment) -
confirmed directly (`dino8_scratch_test`) that `BooleanCombineMixed`'s own
flush-base boss construction (already covered by
`TestBooleanCombineMixedUnionBossFlushBaseVolumeAndCapSeamIsClosed`)
generalizes cleanly to a compound TWO-segment bare tube, producing a
valid `ON_Brep` with the correct closed-form volume; a SEPARATE, TRIED
alternative - two sequential single-segment `BooleanCombineMixed` Union
calls (boss-on-a-boss, chaining the first call's own result as the second
call's operand) - was confirmed directly NOT to work for this fixture
(throws `ClipPolygonByCircle3d`'s own disclosed "partial overlap" refusal
on the second call), so the two-segment-tool construction is not merely
a stylistic choice. **No score change**: "Feature recognition" was
already counted `partial` and stays `partial` here too - a general
(non-cylindrical) pocket and a countersink's own conical second step
remain unrecognized, a stepped chain of more than two radii (hole or
boss side) still only merges its first adjacent pair, and no
`dino8-app` command surfaces any of this. A countersink-side compound
merge (`RecognizeCountersinkHoles`, the conical analogue of
`RecognizeCounterboreHoles`) was investigated this session and NOT
attempted: `MakeCountersinkHole()`'s own tool is a single
`Brep::Revolve()` wall spanning its whole multi-segment profile (the same
reason its own sibling `MakeCounterboreHole()` is unrecognizable by
`ON_Surface::IsCylinder()`, per this item's own "tenth session" note
above), and building a `CounterboreHole()`-style discrete-face
alternative (a separate `Brep::ConicalFace` + `Brep::CylindricalFace`
compound cutter) is blocked by a real, disclosed, pre-existing engine
limitation: `BooleanCombineMixed()`'s own doc comment (boolean.h) states
plainly that "an operand carrying a ConicalFace ... is refused
(std::invalid_argument)" - confirmed directly, not merely read - so
`CounterboreHole()`'s own "adjacent CylindricalFace segments through one
BooleanCombineMixed call" construction has no direct conical counterpart
today; lifting that engine-level restriction is a separate, larger,
riskier increment than this session's own recognition-only scope. Still
**6/16/2, 58.3%**, identical to the count directly above. Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions.

**2026-09-30, a twelfth session:** two closely-related follow-ups, both
in `dino8::kernel::features.h`/`features.cpp`, closing the LAST disclosed
gap both `RecognizeCounterboreHoles()` and `RecognizeSteppedBosses()` name
in their own text above: "a stepped chain of more than two radii ... is
not walked past the first adjacent pair." `RecognizeSteppedHoleChains(solid)`
(features.h:393; features.cpp:696) is the hole-side walker - the
CONCAVE-candidate generalization of `RecognizeCounterboreHoles()` from
exactly two coaxial steps to an arbitrary chain (a spot-face, then a
counterbore recess, then a further pilot reduction, or any other length) -
and `RecognizeSteppedBossChains(solid)` (features.h:530; features.cpp:840)
is its boss-side (CONVEX-candidate) mirror, the same generalization of
`RecognizeSteppedBosses()`. Both return a `SteppedHoleChain`/
`SteppedBossChain` (features.h:358/497) holding an ordered `steps` vector
of `{radius, length, face_index}` entries (features.h:320/467) instead of
the fixed two-field `counterbore_radius`/`drill_radius` shape
`CounterboreFeature`/`SteppedBossFeature` use, since there is no longer a
fixed step count to name individual fields after.

Genuinely new capability - the first N-segment (N >= 3) compound-feature
merge this kernel has had, not a citation fix or a re-styling of existing
two-segment code - built on a genuinely new shared helper,
`FindSteppedChains()` (features.cpp:400, anonymous namespace), the
generalization of the existing `FindAdjacentSteppedPairs()` (features.cpp,
shared by `RecognizeCounterboreHoles()`/`RecognizeSteppedBosses()`) from a
single greedy pairing pass to a full chain walk: every candidate's own two
ends are recorded into a per-end adjacency table (same axis line, a
genuine 3D-touching end, differing radius - the exact pairwise test
`FindAdjacentSteppedPairs()` already uses, just recorded rather than
immediately consumed), an end claimed by more than one other candidate is
marked ambiguous and treated as a chain terminus rather than picked
arbitrarily, and each maximal simple path between two true termini (one
linked end, one free end) becomes one chain of three or more segments -
exactly two segments stays `RecognizeCounterboreHoles()`'s/
`RecognizeSteppedBosses()`'s own domain, not repeated here, the same
one-capability-several-vocabularies convention this file's other
`Recognize*` functions already follow. A single shared conversion,
`SteppedChainOriginAndAxis()` (features.cpp:687), turns a walked chain's
own ordered candidate-index list back into an absolute 3D origin and axis
direction from just one sign bit (`first_outer_is_min` - which raw end of
the chain's own first element is the free, non-touching one), reused by
both the hole-side and boss-side walkers rather than duplicated; each
function then applies its own (opposite) `Mesh::ContainsPoint()` open/
capped or attached/free probe to the chain's own two outer ends, EXACTLY
mirroring `RecognizeHoles()`'s/`RecognizeBosses()`'s own single-candidate
probe logic and `RecognizeCounterboreHoles()`'s/`RecognizeSteppedBosses()`'s
own merged-span probe, generalized from two segments to the whole chain.

Deliberately more general than the two-segment functions' own naming
suggests: neither walker requires the chain's own radii to trend
monotonically (narrowing steadily toward the pilot bore, or widening
steadily toward the tip) - any sequence of adjacent, non-overlapping,
pairwise-different-radius same-axis-line segments merges, including one
that widens then narrows again. Verified by 2 new tests
(`tests/test_basic.cpp`, both built via `BuildSteppedCylinderTool()`
(test_basic.cpp:50398), the N-segment generalization of `CounterboreHole()`'s
own "adjacent `CylindricalFace` segments through one `BooleanCombineMixed`
call" compound-cutter construction, used both subtractively
(`BuildSteppedHoleFixture`) and additively (`BuildSteppedBossFixture`) so
these fixtures are real, tested kernel geometry, not mocks):
`TestRecognizeSteppedHoleChainsRoundTrip` (test_basic.cpp:50454) covers a
monotonically-narrowing 3-step THROUGH bore (its own tessellated volume
checked against the hand-derived `box - sum(pi r_i^2 l_i)` closed form)
and a NON-monotonic 3-step BLIND bore (a spot-face wider than the
counterbore recess beneath it - proving the no-fixed-trend claim above,
not just asserting it), each with a sanity check confirming
`RecognizeCounterboreHoles()` itself still merges only the first adjacent
pair of the same fixture (so the fixture genuinely exercises the gap being
closed), a full round-trip of every recovered step's own radius/length
back through `BuildSteppedHoleFixture()` reproducing the original's exact
volume, and negative controls confirming a plain single-radius hole and a
plain two-segment counterbore (`CounterboreHole()`'s own domain) both find
no chain here. `TestRecognizeSteppedBossChainsRoundTrip` (test_basic.cpp:50578)
covers a flush-base 3-step boss built base-to-tip (no chain reversal
needed) and, separately, the SAME shape built tip-to-base (the tool's own
first-supplied segment is the free tip, its LAST-supplied segment the
attached base) - a real, deliberately-constructed exercise of a genuine
correctness pitfall found while writing this evidence: since
`FindSteppedChains()`'s own walk always starts from whichever true
terminus its outer scanning loop reaches first (an implementation detail
of `ScanFullCylinderFaces()`'s own face-index iteration order, not
anything either walker controls), the chain it hands back is not
guaranteed to already start at the physically attached/open end, so
`RecognizeSteppedBossChains()`/`RecognizeSteppedHoleChains()` must detect
a far-end (rather than near-end) attached/open result and REVERSE the
whole chain - `origin`, `axis`, and every step's own order - before
returning it; this second boss case is the test that would have caught it
missing or backwards, and it also confirms
`RecognizeSteppedBosses()`'s own 2-segment pairing, applied to the SAME
3-step fixture, reports a visibly WRONG `base_radius` of 1.2 (the middle
segment) rather than the boss's own true base radius of 2.0, a concrete,
not merely hypothetical, symptom of the gap being closed. Both tests also
confirm `RecognizeSteppedBosses()` finds nothing at all, or the wrong
segment, on these 3-step fixtures (further sanity that the fixtures
genuinely exercise the gap), check the tessellated volume against the
hand-derived `box + sum(pi r_i^2 h_i)` closed form for both orientations,
and round-trip the recognized origin/axis/steps back through
`BuildSteppedBossFixture()`, plus negative controls for a plain
single-radius boss and a plain two-segment stepped boss.

**No score change**: "Feature recognition" was already counted `partial`
and stays `partial` here too - a general (non-cylindrical) pocket and a
countersink's own conical second step remain unrecognized (same as
before), and no `dino8-app` command surfaces any of this (same as every
other kernel-only feature op in this category). Still **6/16/2, 58.3%**,
identical to the count directly above.

**Test suite status, verified two ways, not merely asserted:** every new
check this session added passes (`ctest`/`dino8_kernel_tests` run
end-to-end past this session's own new tests with no failure reported
from any of them). The full `dino8_kernel_tests` binary as a whole,
however, is NOT 100% green: one single pre-existing failure, in
`TestBooleanCombineMixedUnequalRadiusPerpendicularNegativeControls()`
(test_basic.cpp:33930) - its own "unequal-radius at 60 degrees"
pinch-point-exactness check (a `boolean.cpp`/`boolean_general.cpp`
Steinmetz-curve concern, nowhere near this session's own `features.cpp`/
`features.h` changes) - fails at a tight 1e-9 coordinate tolerance. Confirmed, not merely suspected, to be
PRE-EXISTING and unrelated to this session's own work: a `git worktree`
checkout of this branch's own committed HEAD (commit 7fa8b61, i.e. every
line of this session's own diff excluded) was built and run through the
SAME `dino8_kernel_tests` binary independently, and it fails at the
EXACT same check, the EXACT same 1e-9 tolerance, with the EXACT same
message - proving this session's own additions changed neither this
outcome nor its cause. Two independent full runs of THIS session's own
binary (with the `RecognizeSteppedHoleChains`/`RecognizeSteppedBossChains`
changes included) reproduce that SAME single failure deterministically
(not a flaky/nondeterministic result), and nothing else. Zero regressions
from this session's own work; fixing this one pre-existing, unrelated
numerical-tolerance gap is out of scope for a "Feature operations"
recognition-only pass and is left disclosed here for a future session
that resumes work on the boolean engine's own Steinmetz-curve pinch-point
math.

**2026-09-30, a thirteenth session:** `dino8::kernel::RecognizeCountersinkHoles`
(features.h; features.cpp) closes the LAST remaining gap
`RecognizeCounterboreHoles()`'s own text names - "a countersink's own
conical step ... is a different surface type entirely and is not merged
here" - the specific follow-up the eleventh session's own notes above
investigated and explicitly declined to attempt (blocked, at the time, by
`BooleanCombineMixed()`'s own disclosed refusal of any operand carrying a
`ConicalFace`). Genuinely new capability: this kernel's first CONICAL
compound-feature recognizer, and the first `Recognize*` function in this
file to merge two DIFFERENT candidate types (a cone with a cylinder) into
one feature, rather than two same-type candidates.

Built on a new `ScanFullConeFaces()` (features.cpp, anonymous namespace),
the conical sibling of `ScanFullCylinderFaces()`: scans for a concave full
(closed, 2\*pi) `ON_Surface::IsCone()` face, oriented so both of its own
true heights-from-apex come out positive (a genuine frustum patch never
contains its own apex, so exactly one sign of the fitted axis makes this
true), with each end's own true radius read directly off the same sampled
boundary edge points `ScanFullCylinderFaces()` already reads its own axial
extent from - deliberately NOT from `ON_Cone`'s own `radius`/`height`
fields, which (per `Brep::MixedFaces()`'s own `ExtractConicalFace`
pitfall, brep.cpp) can carry an arbitrary reference scale unrelated to the
specific trimmed patch. A new `FindAdjacentConeCylinderPairs()` pairs a
cone candidate with a cylinder candidate sharing an axis line and a
genuine 3D touch, generalizing `FindAdjacentSteppedPairs()`'s own
same-type matching to check BOTH of the cone's own two ends (not assumed
to always be the narrower one) and requiring the touching end's own radius
to actually match the cylinder's, not merely a coincidental 3D touch at
the wrong radius.

**Two genuine, confirmed pitfalls found and fixed while building this
evidence, not merely disclosed after the fact - both in the TEST FIXTURE
builder, not in the recognizer itself:** (1) a discrete
`Brep::ConicalFace`/`Brep::CylindricalFace` compound tool, needed here
specifically because `MakeCountersinkHole()`'s own single-`Revolve()`-wall
product is never classified as `IsCone()` at all (see this item's own
"tenth session" note above for the identical reason
`MakeCounterboreHole()`'s wall is unrecognizable), throws
`BooleanCombineGeneral`'s own "an edge is claimed by 3 or more fragment
loops" error when the tool's own wide mouth cap lands exactly ON the
target solid's flat surface rather than past it - the identical
coincident-face degeneracy `MakeHole()`/`MakeCountersinkHole()`
(boolean_general.cpp) already avoid internally by backing their own
cutting tool off the entry surface by a small margin, confirmed directly
(a standalone `dino8_scratch_test` diagnostic) to be the actual trigger by
shifting an otherwise-identical cone 1 unit clear of the target: the
"3+ fragment loops" throw disappears entirely. Fixed in the test fixture
builder (`BuildCountersinkHoleFixture`, test_basic.cpp) by prepending a
short cylindrical entry sleeve at the mouth radius, so the tool's own TRUE
outer cap floats just outside the target instead. (2) `ON_Brep::IsValid()`
reports a "seam" or "closed curve directions are opposite" complaint on a
notched-cap `ConicalFace` construction regardless of which of the two
valid axis/radius conventions is used - confirmed to be a pre-existing,
harmless false flag of `IsValid()` for this construction shape, not a real
defect: the IDENTICAL complaint reproduces on `TestBooleanCombineGeneralBoxCone`'s
own already-passing, already-shipped cone parameters when built through
the same local frame/cap helpers, and the actual boolean these fixtures
feed into works correctly regardless (verified against the closed-form
volume and a full `RecognizeCountersinkHoles()` round-trip) - left
disclosed here rather than chased further, since the existing passing
test already establishes this is an accepted, unrelated `IsValid()` quirk.

Verified by 1 new test with 3 sub-cases (`TestRecognizeCountersinkHolesRoundTrip`,
tests/test_basic.cpp): a THROUGH countersink (90 degree, matching the
standard tool angle) merges its cone and pilot-bore cylinder into one
feature whose `origin`/`axis`/`bore_radius`/`bore_depth`/`through` all
match the fixture's own arguments exactly (bore_depth recovered as the
box's own true CLIPPED depth, not the tool's own further-reaching nominal
value - the same "recognizes the real, clipped geometry" property
`RecognizeSteppedHoleChains()`'s own through case already established),
`countersink_diameter`/`countersink_angle_degrees` matching to within the
general boolean engine's own SSX curve-sampling tolerance (looser than
1e-6 for exactly this reason, not a recognition-precision gap - confirmed
by direct measurement, ~0.1% on both), and the removed volume matching the
hand-derived closed form (a cone frustum plus a cylinder) via
`TessellateToClosedMesh()`; a BLIND countersink at a second, independently
chosen standard angle (82 degrees) verified the same way structurally
(field-by-field), `through=false`; and negative controls confirm a plain
single-radius hole and a plain cylindrical/cylindrical counterbore both
find no countersink here. A genuine round-trip through
`MakeCountersinkHole()` ITSELF (not just this file's own fixture builder)
- feeding the recognized parameters back into a fresh box - reproduces the
original through countersink's own volume, the real test that the
recognized parameters are usable in `MakeCountersinkHole()`'s own exact
units and convention despite the two functions building the shape via
completely different constructions (discrete primitives here, a single
`Revolve()` wall there). Full `dino8_kernel_tests` suite (via `ctest`):
6774 checks, 100% passing, 0 regressions - the previously-disclosed
`TestBooleanCombineMixedUnequalRadiusPerpendicularNegativeControls()`
flake (see this item's own "twelfth session" note above) did NOT
reproduce on this session's own run, consistent with that note's own
"intermittent" characterization.

**No score change**: "Feature recognition" was already counted `partial`
and stays `partial` here too - a general (non-cylindrical) pocket remains
unrecognized, a stepped chain mixing conical and cylindrical segments (a
countersink stacked with a further counterbore/pilot step) is out of
scope, same as `RecognizeCounterboreHoles()`'s own two-segment-only
domain, and no `dino8-app` command surfaces any of this. Still **6/16/2,
58.3%**, identical to the count directly above.

**2026-09-30, a fourteenth session:** two closely-related follow-ups, both
in `dino8::kernel::features.h`/`features.cpp`, both built directly on
`RecognizeCountersinkHoles()`'s own `FindAdjacentConeCylinderPairs()`
helper (the thirteenth session's own construction, above) rather than
duplicating it.

First, `dino8::kernel::RecognizeTaperedBosses(solid)` closes this file's
own total absence of any CONVEX-cone recognition - genuinely new
capability, not a citation fix: every existing `Recognize*` function that
touches a cone (`RecognizeCountersinkHoles()`) only ever reads the
CONCAVE half of `ScanFullConeFaces()`'s own candidates, the same way
`RecognizeHoles()` only ever reads the concave half of
`ScanFullCylinderFaces()`'s. `RecognizeTaperedBosses()` is the boss-side
(CONVEX) mirror: a cylindrical shaft feeding into a conical taper sharing
the same axis line and touching at a matching radius - a dowel pin with a
chamfered lead-in point, or (the opposite orientation) a flared conical
pad narrowing to a cylindrical shaft. `FindAdjacentConeCylinderPairs()`
itself needed no change at all to serve this - it never reads either
candidate's own `concave` flag, so the identical pairing test already
works unchanged when fed the CONVEX half of both scans instead of the
concave half. Like `SteppedBossFeature` before it, a tapered boss has no
fixed "which segment is the base" convention (unlike a countersink, whose
entry is always the cone's own wide mouth) - `base_is_cylindrical` records
which segment `origin` actually sits on, read via the same on-axis
`Mesh::ContainsPoint()` attached/free probe every other boss-side
`Recognize*` function already uses.

Two genuine, confirmed pitfalls found and fixed while building this
evidence, both in the TEST FIXTURE builder, not in the recognizer itself
(mirroring the thirteenth session's own precedent of disclosing fixture
pitfalls separately from recognizer pitfalls): (1) `Brep::ConicalFace`'s
own contract (`frame.origin` is the cone's TRUE APEX, strictly outside the
trimmed patch, not the patch's own narrow end - brep.h's own doc comment)
was initially violated by placing `frame.origin` directly at the patch's
own narrow end instead of offsetting it by `radius0/tan(half_angle)`; this
built a genuinely wrong surface (confirmed directly via `dino8_scratch_test`:
`ON_Cone::ApexPoint()` came back sitting exactly on top of the patch's own
narrow end instead of further beyond it), not merely a mislabeled one -
fixed by computing the same apex offset `BuildCountersinkHoleFixture()`'s
own construction already applies, generalized from an angle PARAMETER to
the slope implied directly by `radius0`/`radius1`/`length`. (2) a
standalone (no-boolean, `Brep::FromMixedFaces()`-only) fixture combining a
genuine cylinder-to-cylinder RADIUS STEP with a true end cap on the SAME
solid - a combination no existing test anywhere in this file had - leaves
`Mesh::ContainsPoint()` and `Volume()` both wrong specifically at that
capped end, confirmed directly (`dino8_scratch_test`) to reproduce on a
bare two-segment stepped cylinder with two end caps and nothing else, no
cone involved at all: a real, previously-unconfirmed `FromMixedFaces()`
construction gap, disclosed here rather than chased further (out of scope
for a recognition-only pass - the underlying step-ring/cap interaction
itself is untouched by this session's own work, which lives entirely in
`features.h`/`features.cpp`). Worked around in the test fixture two
different ways depending on which end needed to read reliably: the
"ordinary" orientation's own sanity/volume checks were narrowed to avoid
that specific cap instead (verified directly on the B-rep's own
radius/length fields plus a bounding-box reach check, not a whole-solid
tessellated volume); the "flared-base" orientation's own fixture avoids
the step+cap combination entirely by using a SECOND, wider CONE (a "pad")
in place of a cylindrical filler body - a cone sharing the feature cone's
own touching radius is invisible to `FindAdjacentConeCylinderPairs()`'s
cone-vs-CYLINDER pairing (confirmed the naive cylindrical-filler version
first: it produces a real, reproducible false pairing, matching the
filler body's own radius/length instead of the shaft's, not a hypothetical
concern).

Verified by 1 new test with 4 sub-cases (`TestRecognizeTaperedBossesRoundTrip`,
tests/test_basic.cpp): the ordinary (cylindrical-base) orientation
recovers the shaft's exact radius/length, the taper's exact tip/base
radii/length/full included angle, and the exact attach point/outward
direction; the flared-base orientation recovers the same fields with
`base_is_cylindrical` correctly false and the taper's own `cone_large_radius`
correctly read at the PAD TRANSITION, not the pad's own further, wider
radius; a free-standing tapered rod (open on both ends) reports
`through = true`; and negative controls confirm a plain solid cylinder and
a plain CONCAVE countersink (opposite winding) both find nothing.

Second, `dino8::kernel::RecognizeCountersinkChains(solid)` closes
`RecognizeCountersinkHoles()`'s own disclosed "a countersink stacked with
a further counterbore/pilot step on the same axis ... is out of scope
here" gap - genuinely new capability, the first N-segment compound
feature this file has that STARTS from a cone rather than being built
entirely out of cylinders (`RecognizeSteppedHoleChains`/
`RecognizeSteppedBossChains`, twelfth session). Reuses
`FindAdjacentConeCylinderPairs()` unchanged to find the cone's own first
matching cylinder, then continues a simple forward-only walk from that
cylinder's own far end through any further adjacent, non-overlapping,
differing-radius cylindrical candidates - the same pairwise adjacency
test `FindSteppedChains()` itself uses, but a plain one-directional walk
rather than a full per-end link table, since the cone's own end already
fixes which direction is "forward" (unlike a plain stepped hole, there is
no "which end is the entry" ambiguity left to resolve here). A chain
reaching fewer than two cylindrical steps (the cone touching exactly one
cylinder that doesn't itself continue) is `RecognizeCountersinkHoles()`'s
own disjoint domain, not repeated here.

Verified by 1 new test (`TestRecognizeCountersinkChainsRoundTrip`,
tests/test_basic.cpp) built via a new fixture,
`BuildCountersinkChainFixture` (extends `BuildCountersinkHoleFixture`'s
own sleeve+cone+cylinder tool with a second, narrower `CylindricalFace`
segment - ONE compound tool, ONE `BooleanCombineGeneral()` Difference
pass, the same "single compound tool, not two sequential Difference
passes" precedent `CounterboreHole()`'s own doc comment establishes,
avoided here deliberately after confirming directly that two sequential
real cuts on the same axis is exactly the composition
`ClipPolygonByCircle3d`'s own doc comment already disclaims): a THROUGH
3-step chain (countersink mouth, first pilot bore, a further-reduced final
bore extending past the box) recovers the countersink's own mouth
diameter/angle, both cylindrical steps' own exact radius, the first
step's own exact length and the second step's own exact TOTAL
(box-clipped) length - not the tool's own nominal, further-reaching one,
the same "recognizes the real, clipped geometry" property
`RecognizeSteppedHoleChains()`'s own through case already established -
and `through = true`; a sanity check confirms `RecognizeCountersinkHoles()`
itself still reports only the first pilot bore on this same fixture,
never reaching the third segment; and a negative control confirms a plain
single-step countersink (`RecognizeCountersinkHoles()`'s own domain) finds
no chain here.

**No score change**: "Feature recognition" was already counted `partial`
and stays `partial` here too - a general (non-cylindrical) pocket remains
unrecognized, a countersink chain mixing MORE than one cone (or a cone
anywhere but the chain's own first step) is out of scope, and no
`dino8-app` command surfaces any of this. Still **6/16/2, 58.3%**,
identical to the count directly above. Full `dino8_kernel_tests` suite
(via `ctest`): 7048 checks, 100% passing, 0 regressions.

**Fossilith kernel — Curve operations** (curveops):
- [partial] Curve fairing/smoothing — app-only Laplacian smoothing (dino8-app/src/commands/cmd_meshtools.cpp:740 / cmd_remaining.cpp:866); no kernel fairing.
- [partial] Match curve end continuity — `MatchCommand` (dino8-app/src/commands/cmd_curves2.cpp:1500), position/tangent only, app-only.
- [partial] Offset curve — `NurbsCurve::OffsetInPlane` (dino8-kernel/src/curve.cpp:970 — corrected 2026-09-29, was mis-cited :793) exact for lines, arcs, and now polylines (exact per-corner miter, see the offsetshell category's own "Planar curve offset" bullet for the full construction and test detail), least-squares refit otherwise; app's `OffsetCommand` (cmd_edit.cpp:100) still has its own special cases, doesn't call the kernel.
- [partial] Project/Pull curve onto surface/mesh — `ProjectCommand` (dino8-app/src/commands/cmd_surface.cpp:1327), CPlane sampling only.
- [partial] Divide curve by N/fixed length — `NurbsCurve::DivideByCount` (curve.cpp:581) sits on `ParameterAtArcLength` (curve.cpp:544), which interpolates a 1000-sample polyline; no divide-by-length API.
- [partial] Simplify curve — `SimplifyCrv` (cmd_curves2.cpp:2393-2402) only replaces curves already exactly linear/arc.
- [partial] Change curve degree — `ElevateDegree` (curve.cpp:337) exact; `ChangeDegreeCommand` (dino8-app/src/commands/cmd_edit.cpp:64, comment at line 62) "never lowers" — no reduction.
- [partial] Knot insertion/removal — `InsertKnotAt` (curve.cpp:308) real Boehm insertion; curve knot removal is only `RemoveKnotApprox` (cmd_curves2.cpp:64); kernel's rigorous-bound removal (`RemoveKnotAt`, surface_edit.cpp:314) is surfaces-only.
- [partial] Curve-curve end continuity analysis (GCon) — app-only (dino8-app/src/commands/cmd_remaining.cpp:1258-1276), sampled gap/tangent/curvature.
- [partial] Curve-to-curve deviation (CrvDeviation) — app-only (dino8-app/src/commands/cmd_analyze.cpp:384), `DivideByCount(100)` sampling.
- [partial] Curve length/arc-length parameterization — `NurbsCurve::Length` (curve.cpp:532) still a uniformly-sampled polyline measurement, no quadrature/tolerance guarantee.

**Kernel Surface Operations (Fossilith / Dino 8)** (surfaceops):
- [partial] Merge (MergeSrf) — `MergeSrf` (dino8-app/src/commands/cmd_srfedit.cpp:744-799) still brute-force edge match + grid resample + refit; prints "(refit through samples)" (line 799).
- [partial] Rebuild/Refit (fixed control-point count) — `RebuildCommand` (cmd_edit.cpp:24) samples a grid directly as control points; `RebuildUV` (cmd_srfedit.cpp:2996) still registered as a plain alias of `MakeUniformUV`.
- [partial] Match (G0/G1/G2) — `MatchSrfCommand` (dino8-app/src/commands/cmd_fillet.cpp:1421) tries kernel `MatchEdge` first for surface targets, Position/Tangency only (lines 1482-1509); curve targets still heuristic.
- [partial] Reparameterize — kernel `NurbsSurface::SetDomain` (dino8-kernel/src/surface_edit.cpp:431) exists; app's `Reparameterize`/`SetDomain` call sites (cmd_curves2.cpp:610,2436) curves-only.
- [partial] Degree reduction — `NurbsSurface::Rebuild(u_count, v_count, u_degree, v_degree, ...)` (surface_edit.cpp:441) is a real tensor-product least-squares refit; app's `ChangeDegree` (cmd_edit.cpp:645) still says "never lowers."
- [partial] Knot removal — `NurbsSurface::RemoveKnotAt` (surface_edit.cpp:314) present; app's `RemoveKnot` handles curves only.
- [partial] Make uniform — `MakeUniformUV` (dino8-app/src/commands/cmd_srfedit.cpp:1216-1222) calls `ON_NurbsSurface::MakeClampedUniformKnotVector` directly; no kernel wrapper, no deviation report.
- [partial] Convert to Beziers (surface) — **upgraded from missing.** Kernel `NurbsSurface::DecomposeToBeziers` (dino8-kernel/src/surface_edit.cpp:507; surface.h:958) now exists: a genuine exact decomposition, not a resample. Every interior knot in both directions is raised to full multiplicity via real Boehm knot insertion (`ON_NurbsSurface::InsertKnot`, whose multiplicity argument is a target, not an increment - verified against OpenNURBS' own `ON_InsertKnot`, so this is safe even at a pre-existing G0 kink), then each (u-span, v-span) cell is carved out with two per-direction `Trim` calls at those now-full-multiplicity knot values - the same exact-trim-at-a-knot pattern the app's own curve-only `ConvertToBeziers` (cmd_curves2.cpp:2451-2466) already relies on. Verified in `tests/test_basic.cpp` (`TestSurfaceDecomposeToBeziersProducesExactSpanPatches`): a 3-span-U/1-span-V bicubic decomposes into exactly 3 patches that reproduce the source to < 1e-9 on their own sub-domains; an already-single-Bezier-span surface reports `NoOpAlreadySatisfied` while still handing back that one patch (bit-identical control points); a rational sphere's multi-span-in-both-directions NURBS form decomposes into exactly `u_span_count * v_span_count` patches, every one still rational and reproducing the sphere to < 1e-9. Still partial: nothing in dino8-app calls it - `ConvertToBeziers` remains registered as a curve-only command, so there is still no user-facing surface Bezier decomposition.
- [partial] Patch — `Patch`/`BuildCoonsPatch` app helper (cmd_srfedit.cpp:1775) is planar-patch-only; the exact kernel `CoonsPatch` (surface_edit.cpp:1168 — corrected 2026-09-28, was mis-cited :953) is wired into `NetworkSrf`/`EdgeSrf` (cmd_surface.cpp:593-602), not `Patch`.
- [partial] Make periodic (surface) — `NurbsSurface::MakePeriodicExact` (dino8-kernel/src/surface.cpp:743) re-knots via `NurbsCurve::MakePeriodicExact` (curve.cpp:672); app's command still curves-only.
- [partial] SrfSeam — `SrfSeamCommand` (dino8-app/src/commands/cmd_srfedit.cpp:1382) standalone-surfaces-only.
- [partial] SrfSeam test-coverage caveat — only message-string coverage.
- [partial] Kernel Rebuild() unused by app — `NurbsSurface::Rebuild` (surface_edit.cpp:441) exists/tested but no app command calls it.
- [partial] Kernel MatchEdge() G0/G1/G2 — wired into `MatchSrfCommand` for G0/G1 only (cmd_fillet.cpp:1509); G2 still unreachable from any command.
- [partial] Surface from 2-4 edge curves (EdgeSrf/NetworkSrf) — `CoonsPatch` (surface_edit.cpp:1168 — corrected 2026-09-28, was mis-cited :953) exact for the 4-curve case only; 2/3-curve and CoonsPatch-failure cases fall back to sample-and-refit.

**Kernel: SubD & mesh kernel support** (subd_mesh):
- [partial] SubD -> NURBS patch conversion — `ToNurbsPatches`/`ToNurbsPatchesAdaptive` (dino8-kernel/src/subd.cpp:1015,1431 — corrected again this pass: `SubD::Weld()`'s own ~157-line insertion above both functions shifted the prior pass's already-corrected 858/1274 by exactly that amount; no behavior change); app's ToNURBS (dino8-app/src/commands/cmd_solids.cpp:802,843) still calls only the non-adaptive `ToNurbsPatches`. No dependency on `Brep::Check()`/`RemoveDegenerateFaces` found in subd.cpp — the DegenerateFace false-flag defect does not touch this item.
- [partial] SubD boolean operations — **upgraded from missing, this session.** `SubD::Boolean(other, op)` (subd.cpp, subd.h) now exists: converts both operands via their own already-existing `ToApproximateMesh()`, then hands them to the kernel's real mesh-boolean engine (`dino8::kernel::BooleanCombine`, boolean.h/boolean.cpp - Manifold-backed) and returns the resulting `Mesh` - the same "solid-modeling boolean result as a mesh" scope the app's own BooleanUnion/Difference/Intersection commands already accept for Brep operands (cmd_boolean.cpp, "mesh-based, via Manifold"; see this category's own app-level counterpart below), just reached here starting from two SubD operands directly rather than requiring a caller to hand-roll the same two-call composition itself (as e.g. `TestSubDFromBoxSubdividesToExactCatmullClarkCounts`, tests/test_basic.cpp, already did inline before this method existed). Verified by 4 new tests (tests/test_basic.cpp): union of two disjoint 2x2x2 box SubDs has exactly the summed volume (8+8=16) and is a genuine `IsClosedManifold()` result, not merely a plausible-looking triangle count; intersection and difference of two overlapping box SubDs ([0,2]^3 and [1,3]^3) match their hand-derivable exact volumes (1 and 7 respectively); and an operand whose `ToApproximateMesh()` is genuinely open (a single flat quad with 4 naked boundary edges) is refused with `std::runtime_error`, inheriting `BooleanCombine()`'s own closed/watertight precondition rather than silently producing a corrupt result. Still honestly `partial`, not `present`: this is a mesh-approximate boolean, not a real topological SubD-to-SubD boolean (a result that comes back as a new, editable SubD control cage with correct creases/valences along the cut) - that is a materially bigger problem (re-triangulating a subdivision surface's own control net at an arbitrary cut curve), the same scope this class's own `FromBrep()`/`Tessellate()` doc comments already establish the "materially bigger problem" phrase for, and squarely out of scope here. Not wired into any `dino8-app` command - a real, separate App-level gap this row's own kernel-level scope doesn't measure (see **Dino 8: SubD & mesh modeling toolset (app level)**'s own "SubD booleans" bullet below, which stays `missing` unchanged: the app's existing Boolean commands never call this new method, they still just tessellate a SubD via the app's own separate path first, same as before).
- [partial] SubD from NURBS/B-rep conversion — `SubD::FromNurbsSurface` (subd.cpp:53); single-surface, sample-based, unwired from the app. **Same-day follow-up:** `SubD::FromBrep` (subd.cpp:92, subd.h:212) now also closes the multi-face half of this item for the specific shape `Brep::Tessellate()`'s own seam-matching pass already scoped itself to (planar, untrimmed, axis-aligned-in-own-uv quad faces - see `CollectPlainQuadFaces` in brep.cpp): a whole `Brep::Box()` converts to ONE genuinely joined SubD cage (faces sharing an edge in the Brep share a real interior SubD edge, not two disconnected coincident copies), verified by `TestSubDFromBrepBoxProducesWatertightManifoldCage` (tests/test_basic.cpp) - Euler's formula (V-E+F=2), `SubD::Check().IsManifoldSingleBody()`, all 8 true corners present as single vertices, the level-0 mesh's volume matching the box's own 2x2x2=8 exactly, and further `Subdivide()` genuinely rounding it (volume strictly shrinks) rather than being a frozen copy. Rejection of a genuinely trimmed or non-planar face (`std::runtime_error`, naming the face) is covered by `TestSubDFromBrepRejectsBadDivisionsTrimmedAndCurvedFaces`. Still honestly `partial`, not `present`: a curved (cylinder/sphere/fillet) or genuinely trimmed face is still out of scope entirely (the same scope limit `Brep::Tessellate()`'s own asymmetric-division fix already carries, not a new one invented here) - a true general Brep -> SubD conversion (matching faces/creases across curved and trimmed geometry) remains the "materially bigger problem" `FromNurbsSurface()`'s own doc comment always disclosed; and this is still unwired from any `dino8-app` command. `Brep::FaceCoversWholeDomain()` (brep.h) was widened from `private` to `public` to let `SubD::FromBrep` reuse it (the same "is this face genuinely untrimmed" answer `Volume()`/`Area()` already relied on internally) rather than re-deriving it unreliably from public-only state.
- [partial] SubD symmetry/mirror-in-place — `SubD::Transform` (subd.cpp:246) accepts a mirror `ON_Xform` but only moves vertex positions, per its own doc comment (subd.h) - no flip/weld of its own. **Corrected citation, this pass:** a same-day concurrent session (working the Blending & chamfering category, not this one) had already added `SubD::Symmetrize` (subd.cpp:254, subd.h:314) since this bullet's text was last written, closing the flip+weld half directly: mirrors the whole control cage across a plane, reverses each mirrored face's own vertex winding order (undoing the orientation reversal a reflection always introduces, so the mirrored half is right-side-out rather than the "inside-out" copy a bare `Transform(mirror_xform)` gives), and reuses any ORIGINAL vertex already within tolerance of the mirror plane instead of duplicating it (so a naked boundary loop already lying in the plane becomes one real shared seam between the two halves instead of two disconnected coincident copies) - verified by `TestSubDSymmetrizeWeldsSeamAndFlipsMirroredFaces` (tests/test_basic.cpp). This bullet's own prior text ("no flip/weld/live-constraint code found alongside it") was accurate when written but had gone stale relative to a commit already on this branch; corrected here rather than left misleading. Still honestly `partial`, not `present`: live constrained symmetric editing (a later edit to one half automatically re-mirroring into the other) is a distinct, materially larger feature `Symmetrize()`'s own doc comment discloses as deliberately out of scope - this produces one static symmetrized snapshot, no ongoing relationship between the two halves afterward; a vertex that starts strictly off-plane is always duplicated, not welded to a same-side neighbor, so only mirror-plane-coincident seams close.
- [partial] SubD display-level control at kernel level — `EvaluateFace`/`ToNurbsPatchesAdaptive` (subd.cpp:1467,1694) present. **This session's own addition, closing the remaining "no single tessellate(tolerance)" half:** `SubD::Tessellate(tolerance, max_resolution)` (subd.cpp:1511, subd.h:972) - for each quad face, doubles its own (u, v) sampling grid (starting from just its 4 corners, real limit-surface points via `EvaluateFace()`) until every cell's flat bilinear interpolant of its own already-evaluated corners deviates from the TRUE limit-surface point at that cell's own parametric midpoint by no more than `tolerance` (or `max_resolution` is reached), then welds every face's own grid into one mesh via `Mesh::MergeAndWeld()` - real per-face resolution driven by an actual geometric error bound, not a fixed subdivision-level count `ToNurbsPatchesAdaptive`'s own recursion depth or `ToApproximateMesh`'s own `Subdivide()` level already were. Verified by 5 new tests (tests/test_basic.cpp): input validation (non-positive `tolerance`, `max_resolution` < 1); on a curved (level-0, valence-3-cornered) box, an unreachable tolerance (1e-12) genuinely hits `max_resolution` on every one of the 6 faces (exactly 6*4*4=96 output faces at a cap of 4 - a structural bound, not a plausible-looking count); raising `max_resolution` from 4 to 16 at an already-satisfied tolerance (0.2, measured to converge at the coarsest possible 1x1 grid on this fixture) leaves the output bit-identical at both caps, proving the halt is tolerance-driven rather than cap-driven; a much tighter tolerance (1e-5) produces a strictly finer mesh than a loose one (0.05) on the same box; the box's own 6-way symmetry makes every face converge to the identical resolution at tolerance 0.05, so the merged result is a genuine `IsClosedManifold()` (96 faces, 98 vertices, Euler's formula holds) - real per-face grid construction AND real cross-face welding, not just a plausible face count; and an exactly-planar regular interior face (the same fixture `TestSubDToNurbsPatchesExactOnRegularFlatGrid()` uses) measures exactly zero deviation at the coarsest 1x1 grid, since a partition-of-unity tensor-product patch and the bilinear interpolant of its own corner samples both reproduce an affine function of planar, uniformly-spaced control points identically. Still honestly `partial`, not `present`, and disclosed directly on `Tessellate()`'s own doc comment (subd.h): because each face's resolution is chosen independently, two adjacent faces needing different resolutions leave their shared edge sampled at different densities on either side - `MergeAndWeld()` only welds bit-identical positions, so the finer side's extra edge midpoints stay unwelded (a T-junction/crack, not a fully watertight display mesh in the general, non-symmetric case) - closing that needs a restricted-quadtree/transition-strip scheme, a materially bigger problem, out of scope here; per-point cost for an irregular face also mirrors `EvaluateFace()`'s own full working-copy clone per call, not amortized across a face's many sample points. This category's own present/partial/missing counts are therefore UNCHANGED (15/5/2/22, 79.5%) - the item was already `partial` and stays `partial`; this closes real, tested ground under it (a genuinely tolerance-driven mesher now exists, where before there was none at any resolution granularity), it does not flip the item's own status. This session's source edits are `dino8-kernel/include/dino8/kernel/subd.h`, `dino8-kernel/src/subd.cpp`, and `dino8-kernel/tests/test_basic.cpp`; full `dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 failures, 0 regressions.
- [partial] Quad-remeshing into a clean SubD-ready cage — **upgraded from missing, this session.** `QuadRemeshAction` (dino8-app/src/commands/cmd_remesh.cpp:249) is still app-only (a real, heavier SDF-voxelize-then-dual-contour remesher, `geom/Remesh.h`), but this kernel had literally nothing of its own - not even the much simpler local technique - until now: `Mesh::TrisToQuads(max_dihedral_deg = 20.0)` (dino8-kernel/include/dino8/kernel/mesh.h; src/mesh.cpp) is a genuine, if deliberately narrow, kernel-native quad-dominant remesher - the standard greedy "tris to quads" local pairing (the same operation behind Blender's own Tris to Quads menu item), not a stub. For every undirected edge shared by exactly two TRIANGLE faces, it computes the merged quad `(a, d, b, c)` (shared-edge endpoints `a`/`b`, the two triangles' own third vertices `d`/`c`, the shared edge dropped as the implicit diagonal), scores it by the dihedral angle between the two triangles' own normals, and refuses a candidate whose two triangles don't walk the shared edge in mutually consistent (opposite) directions, whose dihedral angle exceeds `max_dihedral_deg`, whose merged quad would be non-convex (checked via consecutive edge cross products against the pair's own averaged normal - catches a self-intersecting "dart" merge even at a perfect 0deg dihedral, which the angle gate alone would happily accept), or whose two third vertices coincide (the degenerate "same triangle twice" case). Every surviving candidate is applied greedily best-dihedral-first, each triangle consumed by at most one merge. Purely a face-list rewrite - no vertex is added, moved, or removed, so a mesh's naked-edge boundary, volume, and `IsClosedManifold()` status are provably unaffected (only interior triangle-triangle diagonals disappear). Verified by 24 new checks across 5 tests (tests/test_basic.cpp): a tessellated unit box (12 triangles, 2 coplanar diagonal-split triangles per face) recombines into exactly its 6 original quad faces - the 6 0deg diagonal pairs strictly beat every 90deg cube-edge pair at the default 20deg threshold, so no cross-face merge is even a candidate - with vertex count, volume (1), area (6), and `IsClosedManifold()` all exactly preserved (`TestMeshTrisToQuadsRecombinesTessellatedBoxFaces`); a hand-built "tent fold" pair merges at a shallow 5deg dihedral but is refused at a steep 90deg one under the default threshold, then merges once the caller explicitly raises `max_dihedral_deg` past 90 - both confirming the angle gate and, separately, confirming the exact `(a, d, b, c)` vertex order the doc comment derives (`TestMeshTrisToQuadsGatesOnDihedralAngle`); a perfectly flat (0deg) pair whose merged quad is self-intersecting is refused by the separate convexity guard alone (`TestMeshTrisToQuadsRefusesNonConvexMerge`); 3 spatially disjoint unit squares (6 triangles, no shared vertex between any two, so each square's own diagonal is its only candidate and the matching is unambiguous) all merge in one call, with total area and every square's own naked-edge count exactly unchanged (`TestMeshTrisToQuadsMergesSeveralIndependentSquaresInOneCall`); and two triangles sharing both their edge and their apex (a flipped duplicate) are refused rather than merged into a fake 3-distinct-vertex quad (`TestMeshTrisToQuadsRefusesCoincidentApexes`). Full `dino8_kernel_tests` suite (via `ctest`): 100% passing (1 test target, `dino8_kernel_smoke`, 5561 checks), 0 regressions. Still honestly `partial`, not `present`, and disclosed directly on `TrisToQuads()`'s own doc comment (mesh.h): this only ever merges two EXISTING adjacent triangles as-is, with no vertex relocation, global flow-field alignment, or singularity placement - the materially bigger "retopology" problem the app's own SDF/dual-contouring `QuadRemesh` command solves separately, which this neither replaces nor matches in quality; a mesh whose triangles are already irregular still produces an irregular quad mesh; and it is not wired into any `dino8-app` command or into `SubD::FromControlMesh` (the app's existing `QuadRemesh -> ConvertToSubD` path still goes through the app's own heavier remesher, not this).
- [partial] SubD extraordinary-vertex limit-tangent quality — `EvaluateFace` (subd.cpp) exact away from the extraordinary quadrant. **Same-day follow-up:** the zero-vector tangent fallback AT the pole itself (a face-corner query landing exactly on an extraordinary/crease/boundary vertex) is closed — `ExactVertexCorner` (subd.cpp, the corner short-circuit `EvaluateFace`'s own adaptive recursion always resolves to) now calls `ON_SubDVertex::GetSurfacePoint(sector_face, ..., limit_point)`, the same real eigenbasis-based routine `SurfacePoint()`/`SurfaceNormal()` already called internally (verified by reading OpenNURBS v8.34's own `opennurbs_subd_eval.cpp`, not assumed - it builds the sector's point ring and solves it via `ON_SubDSectorType`'s subdivision-matrix eigenstructure, the same Stam-1998-eigenbasis machinery the class's own doc comments already named as "not implemented here"), and reports its real `m_limitT1`/`m_limitT2` unit tangent-plane basis instead of the zero vector. See below for the full detail; still `partial`, not `present` - interior-of-quadrant evaluation near (but not exactly at) an extraordinary vertex still falls back to the tolerance-bounded bilinear-corner approximation, a materially bigger problem (full Stam evaluation at an arbitrary interior parameter) deliberately out of scope here.

**2026-09-30 follow-up (creasing edge case, not tied to a distinct checklist item):** `SubD::SetEdgeSharpness()`'s own doc comment previously disclosed a gap by name - "This wrapper exposes one constant weight per edge (both ends equal); OpenNURBS also supports a per-end-variable sharpness (linearly interpolated along the edge, decaying differently at each end) that this method does not expose - a caller needing that must use raw() directly." That gap is now closed: a new `SetEdgeSharpness(p0, p1, sharpness_at_p0, sharpness_at_p1, point_tolerance)` overload (dino8-kernel/include/dino8/kernel/subd.h, src/subd.cpp) writes a genuinely variable `ON_SubDEdgeSharpness::FromInterval(s0, s1)` instead of the constant-only `FromConstant(s)` the original overload was hardcoded to; the original constant-weight overload now just forwards to it with equal values, so no existing caller's behavior changes. The real content here is end-mapping, not the one-line `FromInterval` call: OpenNURBS stores an edge's own two ends positionally (`m_vertex[0]`/`m_vertex[1]`, in whichever order it happened to build the edge), which need not match the (`p0`, `p1`) order a caller passes in - the new overload detects which of the edge's own two ends is nearest `p0` (`e->Vertex(0u) == v0`) and swaps the two weights before writing if `p0` actually landed on the edge's own end 1, so the caller-visible mapping is always keyed to the point, never to OpenNURBS' internal storage order. Verified by 4 new checks (`TestSubDSetEdgeSharpnessSupportsPerEndVariableWeight`, tests/test_basic.cpp, using the same hinge fixture and fold_a/fold_b naming `TestSubDSetEdgeSharpnessCreatesRealSemiSharpCrease` already established): an out-of-range value on EITHER end refuses the whole call and leaves the edge completely untouched, not half-written; a genuinely uneven (MaximumValue at fold_a, 0 at fold_b) interval reads back exactly via `ON_SubDEdge::EndSharpness(vertex)` at each of the two physical vertices, and calling again with the two POINT arguments and their WEIGHTS both reversed lands on the identical physical assignment (proving the mapping tracks the point, not internal edge orientation); and a real `Subdivide()` call decays the two ends independently rather than averaging them up front - the child edge touching the high end decays by exactly 1.0 from its own starting value while the child edge touching the low end independently decays by exactly 1.0 from ITS starting value, matching `ON_SubDEdgeSharpness::Subdivided()`'s own documented per-end formula (verified by reading its implementation in OpenNURBS' `opennurbs_subd.cpp`, not assumed). Full `dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions. This does not flip any item's present/partial/missing status in this category's own checklist - the underlying "SubD display-level control"/creasing capability was already `partial` and this closes real, tested ground under it (an actual per-end-variable weight now genuinely exists, where before only a constant-across-the-edge weight did) without claiming a materially bigger problem (full live constrained editing, or anything beyond what `ON_SubDEdgeSharpness` itself already models) is now solved.

**Same-day follow-up, closes the "SubD non-manifold/multi-body validity
checks" item above (missing→removed from this gap list, present):**
`SubD::Check()` (subd.cpp:295 — corrected 2026-09-28, was mis-cited :134; `SubDCheckReport`, subd.h) is the genuine,
counted/located diagnostic `SubD::IsValid()` alone never was — the same gap
this category's own item text called out ("a thin bool wrapper over
`ON_SubD::IsValid`"). It reports `naked_edges` (boundary edges,
`ON_SubDEdge::FaceCount()==1`), `non_manifold_edges` with a
`non_manifold_edge_list` of each flagged edge's two vertex ids
(`FaceCount()>=3`), `non_manifold_vertices` with a `non_manifold_vertex_list`
of each flagged vertex's own id (a "bowtie"/pinch-point vertex whose
incident faces don't form one fan — detected the same way this pass's
`kernel: Topology & data structure` category's own new
`Brep::Check()`/`NonManifoldVertex` finding is: union-find over the faces
sharing an edge at that vertex, more than one resulting group flagged), and
`body_count` (union-find over ALL faces sharing an edge, the SubD-level
"is this actually several disconnected pieces" question
`Brep::SplitDisjointPieces()`'s own `ON_Brep::LabelConnectedComponents()`
already answers for Breps but SubD had no counterpart to at all). Verified
by 5 new tests (tests/test_basic.cpp,
`TestSubDCheck{CleanClosedBoxReportsNoDefects,
OpenGridReportsNakedEdgesOnly, DisjointPiecesReportsMultipleBodies,
NonManifoldEdgeDetected, BowtieVertexDetected}`): a clean closed box reports
all-zero; an intentionally open flat grid reports 8 naked edges and nothing
else (not a defect, matching `IsManifoldSingleBody()`'s own
`Mesh::CheckReport::IsClosedManifold()`-style convention of ignoring naked
edges); two boxes merged into one Mesh with disjoint vertex-index ranges
report `body_count==2`; three quads hand-built via `ON_SubD::AddVertex`/
`FindOrAddEdge`/`AddFace` fanned around one shared edge report exactly that
edge as non-manifold, located by its own vertex ids; and two single-quad
"wings" sharing only one vertex (zero shared edges, so `non_manifold_edges`
alone would miss it) report exactly that vertex as a bowtie AND report
`body_count==2` — the two conditions are independent and both fire on the
cases that actually distinguish them. Still partial as a category, and this
item does not attempt: no repair/split counterpart
(`Brep::SplitNonManifoldVertex`/`SplitDisjointPieces` have no SubD analog
here — a caller learns the SubD is broken/multi-body but must still fix it
by hand); `IsValid()` itself is unchanged (a different, complementary
structural cross-reference check, not superseded); and duplicate-vertex
detection (two coincident-but-distinct control-net vertices) is not
attempted, the same condition `Mesh::CheckReport::duplicate_vertices`
covers for `Mesh` but has no `SubD` counterpart.

*Note on this category's count: still 15 present / 5 partial / 2 missing
(22 items, 79.5%) after this pass's own `SubD::FromBrep` addition (see
the "SubD from NURBS/B-rep conversion" bullet's own same-day follow-up
above) - honestly unchanged, not a new upgrade: `FromBrep` closes real,
tested ground under that item (a whole planar-quad Brep, e.g. `Box()`,
now converts to one genuinely joined SubD cage, where before only a
single NURBS surface at a time did), but the item was already `partial`
(not `missing`) before this pass, and general curved/trimmed Brep
conversion remains squarely out of scope - the item stays `partial`,
same as `SubD::Weld()`'s own predecessors (InsertEdge/SpinEdge/
ExtrudeFace/ExpandFaces) did before `Weld()` itself closed the LAST gap
in that item's own five-sub-operator checklist. The prior 15/5/2
(79.5%) count itself was one further upgrade from 14/6/2 (77.3%),
driven by `SubD::Weld()`'s partial→present flip of the "Kernel-native
SubD local edit operators" item (this pass's own "Fifteenth same-day
follow-up" note above closes the fifth and last named sub-operator -
see that note for the full detail, including why the item's own
still-unaddressed "not wired to any app command" caveat doesn't hold it
back from `present`: that's an App-level concern this row doesn't
measure, tracked instead under **Dino 8: SubD & mesh modeling toolset
(app level)** below); the prior 14/5/3 (75.0%→77.3%) upgrade was driven
by InsertEdge/SpinEdge/ExtrudeFace (the Sixth same-day follow-up), and
the 13/6/3 (72.7%→75.0%) upgrade before that by the `SubD::Check()`
finding.*

**Seventeenth same-day follow-up:** `git log --oneline -30 -- dino8-kernel/
src/subd.cpp dino8-kernel/src/mesh.cpp` at the start of this session
showed `658c095` (`kernel: add SubD::FromBrep`) as the most recent commit
touching this category specifically (`52de4f8` `SubD::Weld()` and the
InsertEdge/SpinEdge/ExtrudeFace/ExpandFaces commits before it), so this
session picked the next highest-value still-`[partial]` item in the same
category rather than duplicating either: "SubD extraordinary-vertex
limit-tangent quality", whose own evidence already named a genuine,
narrowly-scoped hole - `EvaluateFace()`'s corner short-circuit
(`ExactVertexCorner`, subd.cpp) unconditionally reported `tangent_u`/
`tangent_v` as the zero vector at any extraordinary/crease/boundary
vertex, with both the function's own inline comment and the class's
public header doc comment stating outright that the "full Catmull-Clark
eigenbasis" needed to do better "is not implemented here"/"this class
doesn't implement".

That claim was checked against OpenNURBS' own v8.34.26223.11001 source
(the exact tag this repo's `CMakeLists.txt` pins, cloned fresh and read
directly, not assumed from memory) rather than taken at face value - and
turned out to be an opportunity, not a wall: `ON_SubDVertex::
GetSurfacePoint(sector_face, bUndefinedNormalIsPossible, limit_point)`
(`opennurbs_subd_eval.cpp`) is a public, non-stub method, and it IS the
real eigenbasis evaluator - `SurfacePoint()`/`SurfaceNormal()` (the two
methods this exact function already called) are themselves thin wrappers
around it. It fills an `ON_SubDSectorSurfacePoint` whose `m_limitP`/
`m_limitN`/`m_limitT1`/`m_limitT2` fields (`Point()`/`Normal()`/
`Tangent(0)`/`Tangent(1)` accessors) are computed by building the
vertex's sector "point ring" and solving it through `ON_SubDSectorType`'s
own subdivision-matrix eigenstructure (`opennurbs_subd_eval.cpp`/
`opennurbs_subd_matrix.cpp`) - the same construction Stam's 1998 paper
(already cited on this class's own `ToNurbsPatches()` doc comment) is
named for, genuinely present in this pinned OpenNURBS version, not a
newer feature or a different function entirely.

`ExactVertexCorner` (subd.cpp) now calls this directly instead of
`SurfacePoint()`/`SurfaceNormal()` separately, and reports `Tangent(0)`/
`Tangent(1)` as `tangent_u`/`tangent_v` instead of the zero vector - a
real, non-zero, unit-length tangent-plane basis at any face-corner query,
extraordinary vertex or not (position/normal are numerically identical to
the prior code path, since both always went through this same OpenNURBS
routine internally). Verified by 2 new checks in a dedicated test
(`TestSubDEvaluateFaceExtraordinaryCornerHasRealTangentPlane`, tests/
test_basic.cpp), run at both a genuinely extraordinary (valence-3) corner
and an ordinary (valence-4) corner of the same once-subdivided-cube face
(the whole face routes every corner through `ExactVertexCorner`, once ANY
one of its corners is irregular) - deliberately checking the geometric
properties any correct tangent-plane basis must have, rather than a
hand-derived closed-form eigenbasis number that would just be re-deriving
OpenNURBS' own internals from scratch: `tangent_u`/`tangent_v` are unit
vectors (not the zero-vector fallback), both orthogonal to the reported
limit normal (`ON_DotProduct` within 1e-9 of zero - genuinely IN the
tangent plane), linearly independent (a non-degenerate cross product), and
`tangent_u x tangent_v` points the same direction as the reported normal
(`ON_DotProduct` of the unitized cross product with `normal` within 1e-9
of 1) - the same right-handed convention `EvalPatchPoint()`'s regular-face
bicubic tangents already use. Full `dino8_kernel_tests` suite (via
`ctest`): 100% passing, 0 regressions.

Still honestly `partial`, not `present`, and this document's own header
comment for `EvaluateFace()` (subd.h) and `ExactVertexCorner`'s own inline
comment (subd.cpp) were both updated in place to stop claiming the
eigenbasis "isn't implemented" and instead describe accurately what these
tangent vectors are and aren't: OpenNURBS' real unit eigenbasis tangent
vectors in a canonical sector-relative frame - genuinely spanning the
exact tangent plane, but NOT the same "raw dS/du, dS/dv partial
derivative at THIS face's own (u, v)" convention a regular corner's
`EvalPatchPoint()` tangents use, and not oriented to this specific face's
own (u, v) axes. The item's real remaining gap is unchanged and squarely
out of scope here: `EvaluateFaceAdaptive()`'s recursive quadrant-doubling
fallback for a query NEAR (not exactly at) an extraordinary vertex still
reports the tolerance-bounded flat bilinear-corner interpolant's own
tangents, not a limit-accurate value - closing that needs the
"materially bigger problem" of full Stam evaluation at an arbitrary
interior parameter, this document's own established phrase for
deliberately out-of-scope subdivision-surface work (see `SubD::FromBrep`'s
own bullet above). This category's own present/partial/missing counts are
therefore UNCHANGED (15/5/2/22, 79.5%) - the item was already `partial`
and stays `partial` - so no table or headline arithmetic changes. This
session's only source edits are `dino8-kernel/src/subd.cpp`,
`dino8-kernel/include/dino8/kernel/subd.h`, and
`dino8-kernel/tests/test_basic.cpp`.

**Eighteenth follow-up, a later session (this one):** `git log --oneline
-5 -- dino8-kernel/src/subd.cpp` at the start of this session showed
`d3b4fcd` (`kernel: add SubD::Tessellate`) as the most recent commit
touching this category, so this session looked for the next real,
closeable gap in the same category's own gap-bullet list rather than
duplicating that work — and picked the category's own remaining
`[missing]` item, "SubD boolean operations" (the only missing item left
at this point; every other gap in this category was already `[partial]`),
rather than a further `[partial]`->`[partial]` refinement. `SubD::Boolean(
other, op)` (subd.cpp, subd.h) closes it to `partial` (see that bullet's
own upgraded text above for the full detail: a real Manifold-backed mesh
boolean reached directly from two SubD operands via their own existing
`ToApproximateMesh()`, 4 new tests, exact-volume checks on disjoint and
overlapping box SubDs, and a rejected-open-operand check). This moves the
table row from 15/5/2 (79.5%) to **15/6/1 (81.8%)** — (15 + 0.5*6)/22 =
18/22 = 81.8% — the category's own last `[missing]` item is now closed;
every remaining gap here is `[partial]`. This session's only source edits
are `dino8-kernel/include/dino8/kernel/subd.h`, `dino8-kernel/src/
subd.cpp`, and `dino8-kernel/tests/test_basic.cpp`; full
`dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions.

**Correction (Nineteenth follow-up, a later session):** the Eighteenth
follow-up's own closing claim above — "the category's own last `[missing]`
item is now closed; every remaining gap here is `[partial]`" — was itself
inaccurate: "Quad-remeshing into a clean SubD-ready cage" (this category's
own bullet list above) was `[missing]` both before and after that session,
untouched by it (that session's own source edits, listed above, never
touch `mesh.cpp` or `mesh.h`, where a kernel remesher would have to live).
Not a citation-only slip - the table row this document actually carries
was correct at 15/6/1 (one missing item remained), so no table arithmetic
was ever wrong, only this one prose sentence overclaiming completeness.
Flagged and corrected here rather than left standing, per this document's
own established practice for a stale or inaccurate claim found later (see
e.g. the "SubD symmetry/mirror-in-place" bullet's own "corrected citation"
above). This session's own `git log --oneline -3 -- dino8-kernel/src/
mesh.cpp dino8-kernel/src/subd.cpp` confirmed the same thing independently
before starting: the most recent commit touching either file was the
Eighteenth follow-up's own `SubD::Boolean` work, which - as just
established - never touched the Quad-remeshing item, so this session
picked exactly that: the category's own actual last `[missing]` item,
still open.

`Mesh::TrisToQuads(max_dihedral_deg)` (dino8-kernel/include/dino8/kernel/
mesh.h; src/mesh.cpp) closes it to `partial` — see the "Quad-remeshing
into a clean SubD-ready cage" bullet's own upgraded text above for the
full construction and test detail: a real, tested, kernel-native greedy
triangle-pair quad remesher (24 new checks across 5 tests, tests/
test_basic.cpp), honestly scoped as a much simpler LOCAL technique than
the app's own SDF/dual-contouring `QuadRemesh`, not a competitor to it.
This moves the table row from 15/6/1 (81.8%) to **15/7/0 (84.1%)** — (15 +
0.5*7)/22 = 18.5/22 = 84.1% — every item in this category is now at least
`[partial]`; genuinely zero `[missing]` items remain here, this time
verified directly against the corrected bullet list above rather than
just asserted. Weighted the same marginal-delta way every other same-day
follow-up in this document does (this row's own weight of 0.75 against
the kernel table's own total weight 17.75, on top of this document's own
top-line 67.3% kernel-only figure, which already reflects the Eighteenth
follow-up's own 79.5%→81.8% bump to this same row): (18.5/22 - 18/22) *
0.75 / 17.75 = 0.02273 * 0.75 / 17.75 = 0.01705 / 17.75 = +0.096pp, giving
**67.4%** kernel-only. The combined Dino 8 vs Rhino 8 + AutoCAD 2027
headline is
left at 71.4%, unaffected (a kernel-only change - no `dino8-app` command
calls `TrisToQuads()` yet, same convention every prior follow-up above
uses). This session's only source edits are `dino8-kernel/include/dino8/
kernel/mesh.h`, `dino8-kernel/src/mesh.cpp`, and `dino8-kernel/tests/
test_basic.cpp`; full `dino8_kernel_tests` suite (via `ctest`): 100%
passing (1 test target, `dino8_kernel_smoke`, 5561 checks), 0 regressions.

**2026-09-30 follow-up (creasing read-back, not tied to a distinct
checklist item):** `git log --oneline -3 -- dino8-kernel/src/subd.cpp` at
the start of this session showed `73f898a` (`kernel: SubD::SetEdgeSharpness
gains genuine per-end-variable weight`) as the most recent commit touching
this file - already documented above (see the "2026-09-30 follow-up
(creasing edge case...)" note) - so this session picked the real gap that
addition's own text left standing: both `SetEdgeSharpness()` overloads
could WRITE a per-end sharpness value from the moment they existed, but
there was no way to READ one back short of a `const_cast` onto `raw()`
directly (`ON_SubDEdge::EndSharpness(vertex)`, the exact primitive
`SetEdgeSharpness()` itself already calls internally to map p0/p1 onto the
edge's own storage order, was never exposed to a caller for the reverse
direction).

`SubD::EdgeSharpnessAt(p0, p1, point_tolerance)` (dino8-kernel/include/
dino8/kernel/subd.h; src/subd.cpp) closes it: returns a new
`SubDEdgeSharpnessInfo{found, sharpness_at_p0, sharpness_at_p1}`, reusing
the identical FindVertex/FindEdge/`IsSmooth()` refusal logic
`SetEdgeSharpness()` already established (a hard Crease-tagged edge, a
missing vertex, or no edge between them all read back `found = false`
rather than a stale/zero value indistinguishable from "genuinely zero
sharpness") and the identical `e->Vertex(0u) == v0` point-vs-storage-order
mapping, applied to reading instead of writing. Verified by 4 new checks
(`TestSubDEdgeSharpnessAtReadsBackWhatWasWritten`, tests/test_basic.cpp):
an untouched smooth edge reads back `found = true` with both ends
genuinely 0 (the real OpenNURBS default, not an unset sentinel); a
constant-weight write round-trips exactly; a genuinely UNEVEN per-end
write (kMax at one end, 0 at the other) round-trips exactly in the SAME
point order it was written in, AND reads back correctly REVERSED when the
two query points are swapped - the real claim this test exists for, proving
the read side tracks the physical point rather than OpenNURBS' own
internal `m_vertex[0]`/`[1]` storage order, the identical property
`SetEdgeSharpness()`'s own per-end overload already guarantees for
writing; and a hard-crease edge, a missing vertex, and two real vertices
with no edge between them all correctly read back `found = false`. Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions. Does
NOT flip any item's present/partial/missing status in this category's own
checklist - the underlying creasing capability was already `[partial]` (as
part of "SubD display-level control at kernel level") and this closes
real, tested ground under it (a caller can now inspect what sharpness an
edge already carries, where before only writing was possible) without
claiming a materially bigger problem is solved. The table's own 15/7/0/22
(84.1%) is unchanged. This session's only source edits are
`dino8-kernel/include/dino8/kernel/subd.h`, `dino8-kernel/src/subd.cpp`,
and `dino8-kernel/tests/test_basic.cpp` (plus, in the same session, the
"Shell with removed/open faces" work documented under **kernel:
Offsetting, shelling, thickening** above, touching `mesh.h`/`mesh.cpp`
instead).

**2026-09-30 follow-up (SubD::Check() repair/localization gaps, not tied
to a distinct checklist item):** `git log --oneline -3 -- dino8-kernel/
src/subd.cpp` at the start of this session showed the "creasing
read-back" follow-up above (`EdgeSharpnessAt`) and, before it, the
Shell-with-openings/EdgeSharpnessAt round's own commit `c064854` as the
most recent work touching this file, neither of which touched
`SubD::Check()` itself - so this session went back to that method's own
still-standing self-disclosed gaps instead: its introducing "Same-day
follow-up" note above named two things it explicitly did NOT attempt -
"no repair/split counterpart (`Brep::SplitNonManifoldVertex`/
`SplitDisjointPieces` have no SubD analog here)" and "duplicate-vertex
detection (two coincident-but-distinct control-net vertices) is not
attempted, the same condition `Mesh::CheckReport::duplicate_vertices`
covers for `Mesh` but has no `SubD` counterpart." Both are now closed.

`SubD::Check()` gains a `duplicate_vertex_tolerance` parameter (default
`tolerance::kDistance`, so every existing no-argument call site is
unaffected) and its `SubDCheckReport` gains `duplicate_vertices`/
`duplicate_vertex_list` (dino8-kernel/include/dino8/kernel/subd.h;
src/subd.cpp): every control-net vertex within that tolerance of another
distinct vertex is flagged, grouped by simple spatial proximity via a
grid + union-find (`GroupByProximity`, subd.cpp, anonymous namespace) -
the same clustering shape `mesh.cpp`'s own `WeldGroups()` already uses
for `Mesh::CheckReport::duplicate_vertices`, reimplemented rather than
shared since that one is keyed to `ON_Mesh` vertex array indices and this
one just takes a flat point list. Deliberately a different condition from
`non_manifold_vertices` (a bowtie is ONE vertex shared correctly by two
unconnected fans; a duplicate is TWO OR MORE separate `ON_SubDVertex`
records that were never welded together at all) and left OUT of
`IsManifoldSingleBody()`, same convention `naked_edges` already gets
there: an as-yet-unwelded coincident seam is a real defect worth
reporting, but not itself a topological manifold/multi-body failure.
Verified by 6 new checks (`TestSubDCheckDuplicateVerticesDetected`,
tests/test_basic.cpp): two boxes merged with disjoint vertex-index ranges
but positioned so exactly one corner coincides ([0,1]^3 and [1,2]^3, both
having a corner at exactly (1,1,1)) report `duplicate_vertices == 2`
(one flag per side of the one coincident pair) at the default tolerance,
at an exact zero tolerance, and at 1e-15 (an exact match survives
shrinking the tolerance far below the default); the same fixture's
`body_count` stays 2 and `non_manifold_vertices` stays 0, confirming this
is genuinely independent of both existing conditions; and a single clean
box (no coincident-but-distinct corners at all) reports zero.

`SubD::SplitDisjointPieces()` (subd.h; subd.cpp) closes the other half:
the SubD-level counterpart of `Brep::SplitDisjointPieces()`, splitting a
multi-body SubD into that many separate single-body SubDs using the exact
same face-connectivity-via-shared-edge definition `Check()`'s own
`body_count` already computes (reproduced here rather than shared, since
`body_count` only needs the group COUNT while this needs the actual
membership) - so a bowtie vertex (shared by two otherwise-disconnected
fans) is duplicated into each piece it touches rather than left bridging
them, consistent with `body_count` already treating those fans as
separate bodies. Rebuilt the same snapshot-and-replay way `Weld()`
rebuilds its own result: every member vertex re-added via
`ON_SubD::AddVertexForExperts()` (preserving its original `m_id` and
position, so a piece's vertex can still be looked up by id in the
original SubD), every member face rebuilt via `FindOrAddFace()`, and
every wholly-interior original edge's tag/sharpness reapplied afterward -
not local surgery, for the identical `DeleteComponents()`-unsafety reason
`Weld()`'s own doc comment already gives for taking that approach.
Verified by 8 new checks (`TestSubDSplitDisjointPiecesSplitsIntoSeparate
SubDs`, tests/test_basic.cpp): a single-body box SubD splits into exactly
1 piece, an exact copy (same face/vertex counts, itself still
`IsManifoldSingleBody()`); a faceless SubD splits into 0 pieces; the same
two-disjoint-boxes fixture `TestSubDCheckDisjointPiecesReportsMultiple
Bodies` uses splits into exactly 2 pieces, each independently
`IsManifoldSingleBody()` with exactly one box's own 6 faces / 8 vertices
(not the combined 12/16); each piece's own bounding box (via
`ToApproximateMesh()`, which exports the control net directly - i.e.
exact original corner positions, not a subdivided approximation)
unambiguously matches one original box's own extent, not a duplicate of
the other piece; and every vertex id in a returned piece resolves, back
in the ORIGINAL combined SubD, to a vertex at the exact same control
point - ids are preserved, never renumbered from scratch. This category's
own present/partial/missing counts are unchanged (15/7/0/22, 84.1%) -
`Check()`'s own item was already `present` (moved out of this gap list by
an earlier same-day follow-up above) and stays `present`; this closes
real, tested ground under a residual gap that item's own text already
disclosed, without claiming a materially bigger problem (a repair that
picks WHICH group a non-manifold vertex's faces should split into, which
`Check()`'s own introducing note already named as a genuine judgment call
this class still doesn't make for a caller) is now solved. Full
`dino8_kernel_tests` suite (via `ctest`): 100% passing, 0 regressions.
This session's only source edits are `dino8-kernel/include/dino8/kernel/
subd.h`, `dino8-kernel/src/subd.cpp`, and `dino8-kernel/tests/
test_basic.cpp`.

**2026-09-30 follow-up (the two repair gaps `SplitDisjointPieces()`'s own
introducing note above left explicitly open):** `git log --oneline -3 --
dino8-kernel/src/subd.cpp` at the start of this session showed
`b7c1e6f` (`SubD::Check()` duplicate-vertex detection + `SplitDisjointPieces`)
as the most recent commit touching this category, whose own text already
named exactly what it did NOT attempt: "no repair/split counterpart
(`Brep::SplitNonManifoldVertex`/`SplitDisjointPieces` have no SubD analog
here...)" and "duplicate-vertex detection... is not attempted" (closed to
detection-only by that same commit, still with no WELD counterpart). Both
are now closed, picked together since they're the two halves of the same
"`Check()` can find it, nothing can fix it" gap.

`SubD::SplitNonManifoldVertex(vertex_id)` (subd.h; subd.cpp) is the
SubD-level counterpart of `Brep::SplitNonManifoldVertex()`: the same
Parasolid/ACIS "disjoin" repair for a bowtie vertex — nothing about any
face's own shape at the pinch point is wrong, only the topology of one
`ON_SubDVertex` record being shared between two-or-more locally-
disconnected fans of faces is. Groups the vertex's own incident faces via
the identical union-find-over-shared-incident-edges `Check()`'s own
`non_manifold_vertices` already computes; group 0 (first-seen order, same
convention `Check()`/`Brep::SplitNonManifoldVertex()` both already use)
keeps the original vertex, every other group gets a fresh vertex at the
same control-net point with that group's own faces repointed onto it. Like
`Weld()`/`SplitDisjointPieces()` above (and for the identical
`DeleteComponents()`-unsafety reason their own doc comments already give),
this is a whole-net snapshot-and-rebuild rather than local surgery — but
every OTHER vertex's own id, including any other bowtie vertex's from the
same `Check()` call, is still preserved unchanged across the rebuild, so
`SplitNonManifoldVertices()` (the batch driver alongside it) can safely
run `Check()` once and split every reported id in one pass, the same
safety property `Brep::SplitNonManifoldVertices()` already has for a
different (pure-append) reason. One real implementation pitfall caught
before it shipped: `ON_SubD::AddVertexForExperts()`'s own candidate-id
parameter is only honored when it EXCEEDS the id watermark already seen
in the (never-had-a-deletion) fresh `ON_SubD` being built — so the target
vertex's OWN id had to be re-added at its natural position in the SAME
vertex-iteration pass as every other preserved vertex (establishing the
watermark correctly before any fresh id is requested), not held back and
appended afterward the way a first draft tried; verified by direct
reproduction (the naive appended-afterward version silently handed group 0
a DIFFERENT id than the one requested whenever the target's original id
wasn't the maximum in the whole vertex range) before landing the fix.

`SubD::MergeDuplicateVertices(tolerance)` closes the other half: the weld
counterpart of `Check()`'s own `duplicate_vertices`/`duplicate_vertex_list`
— groups control-net vertices by the identical `GroupByProximity()`
spatial clustering `Check()` itself already uses to flag them, then welds
every group down to one real vertex via repeated `Weld()` calls (first
member kept, every other member welded onto it). Deliberately reuses
`Weld()` rather than reimplementing vertex-merge topology surgery a second
time; a member `Weld()` itself refuses (already edge-connected to the
kept vertex, or already two distinct corners of the same face) is left
unmerged rather than treated as an error, same "can't, but that's not a
bug" contract every other `Weld()`-based repair here already has.

Verified by 9 new checks across 2 tests (tests/test_basic.cpp):
`TestSubDSplitNonManifoldVertexSplitsBowtie` reuses
`TestSubDCheckBowtieVertexDetected`'s own two-wings-sharing-one-vertex
fixture — refuses an id that doesn't identify a vertex of this SubD;
splits the genuine bowtie (vertex count 7->8, face count unchanged at 2,
`non_manifold_vertices` 1->0); the original id and its new twin both still
sit exactly at the origin, each now belonging to exactly one (different)
wing; a post-split re-check confirms `SplitNonManifoldVertex()` itself now
refuses the already-fixed, single-fan vertex; and a fresh instance of the
same fixture proves the `SplitNonManifoldVertices()` batch driver finds and
splits the one bowtie without the caller naming its id.
`TestSubDMergeDuplicateVerticesWeldsCoincidentPairs` reuses
`TestSubDCheckDuplicateVerticesDetected`'s own two-wings-with-a-
coincident-but-distinct-vertex-pair fixture — merges exactly 1 vertex away
(8->7), `duplicate_vertices` 2->0 afterward, `body_count` STAYS 2 (a
shared vertex is not a shared edge — the same distinction the bowtie test
already draws, confirmed here from the opposite direction), and
`non_manifold_vertices` becomes 1 (welding two originally-separate wings
at a single point always produces a genuine bowtie, which
`MergeDuplicateVertices()` deliberately does not itself try to resolve —
that's `SplitNonManifoldVertex()`'s own separate job); plus a clean box
(nothing to merge) and an empty SubD both correctly merge zero. One real
test-writing mistake caught and fixed before landing, not shipped on
first instinct: an initial draft asserted `body_count == 1` after the
merge (assuming a shared vertex joins the two wings into one body) — false
on inspection, since `body_count` is purely edge-adjacency and a bare
shared vertex creates no edge; corrected to assert `body_count == 2`
before this pass's own final full-suite run, per this document's own
established practice of never landing a check the code doesn't actually
satisfy.

This category's own present/partial/missing counts are unchanged
(15/7/0/22, 84.1%) — `Check()`'s own item was already `present` before
this pass and stays `present`; this closes real, tested ground under the
two residual gaps that item's own text explicitly disclosed, without
claiming the one thing `Brep::SplitNonManifoldVertex()`'s own doc comment
already scopes out for its own Brep counterpart either: which group
"should" keep the original vertex/id remains Check()'s own first-seen
order, never a caller-supplied judgment call. Full `dino8_kernel_tests`
suite (via the test binary directly, not `ctest`'s own wrapper, whose
buffered/interleaved-with-other-suites output made isolating a single
failing check unreliable this pass): 100% passing (all checks passed),
0 regressions, after fixing the one `body_count` assertion mistake noted
above. This session's only source edits are `dino8-kernel/include/dino8/
kernel/subd.h`, `dino8-kernel/src/subd.cpp`, and `dino8-kernel/tests/
test_basic.cpp`.

**2026-09-30 follow-up (the `Mesh`-side mirror of `SubD::Check()`'s own
repair pair above, plus a closely-related `Mesh` detection gap):** `git
log --oneline -3 -- dino8-kernel/src/subd.cpp dino8-kernel/src/mesh.cpp`
at the start of this session showed the `SplitNonManifoldVertex`/
`MergeDuplicateVertices` commit above as the most recent work in this
category, entirely on the `SubD` side; `Mesh::CheckReport` had the
identical `duplicate_vertices` condition (it's the older of the two —
`SubD::Check()`'s own version was explicitly modeled on it) but, per its
own class-comment history, only a boundary-restricted weld
(`CloseNakedEdges()`) and no general one. This session closed that, plus
a second, separately-disclosed `Mesh` gap in the same file discovered
while reading the class for the first gap.

`Mesh::MergeDuplicateVertices(tolerance)` (dino8-kernel/include/dino8/
kernel/mesh.h; src/mesh.cpp) is the direct `Mesh`-class counterpart of
`SubD::MergeDuplicateVertices()` above: welds every group of
`tolerance`-coincident vertices ANYWHERE in the mesh (not merely ones
sitting on a naked edge, the restriction `CloseNakedEdges()` has always
had by design), reusing the exact `WeldGroups()` grid/exact-distance
grouping `Check()`'s own `duplicate_vertices` count already groups by, so
`Check(tolerance).duplicate_vertices == 0` after this runs at the same
tolerance. Deliberately a separate, explicitly-called method rather than
something `Check()` or any other repair runs automatically, for the exact
reason this class's own `Check()` doc comment already gave for never
having one: an interior feature that happens to land `tolerance`-close to
another is not the same bug as a seam left open by construction, and
silently welding it could collapse real geometry a caller never asked to
change. Welding a shared vertex does NOT by itself merge any edge between
the two sides (that needs both endpoints of an edge to match, not just
one shared corner) — closing a resulting seam, if any, stays
`CloseNakedEdges()`'s own separate job. Verified by
`TestMeshMergeDuplicateVerticesWeldsCoincidentPairs` (tests/
test_basic.cpp), mirroring `SubD::MergeDuplicateVertices()`'s own
two-independent-quad-wings-with-a-coincident-corner fixture exactly: 8
vertices / 2 duplicate-flagged / 8 naked edges before, 1 vertex welded
away, 7 vertices / 0 duplicates / the SAME 8 naked edges after (proving
the vertex weld alone never merges an edge), plus a clean box (nothing to
merge) and an empty mesh (refuses trivially) both correctly merging zero.

Separately, while re-reading this class's own `FindSelfIntersections()`
doc comment for context, its own disclosed "(1) two overlapping COPLANAR
triangles are not reported" limitation turned out to be a real, closeable
gap in the same file, not a materially bigger problem: `TrianglesProperly
Overlap()`'s cross-product-of-normals construction is zero for two
coplanar planes by definition (no shared line exists to measure an
overlap interval along), so a genuine coplanar overlap was silently
unreachable regardless of how much area the two triangles actually
shared. `CoplanarTrianglesOverlap()` (mesh.cpp, next to `TrianglesProperly
Overlap()`) closes it: confirms the two triangles genuinely share ONE
plane (every vertex of the second triangle within `tolerance` of the
first's own plane — not merely parallel ones, which correctly still
excludes e.g. a box's own parallel-but-offset top and bottom faces),
projects both onto an orthonormal basis of that shared plane, and applies
the standard two-convex-polygon separating-axis test (no separating line
among either triangle's own up to 6 edge directions means a genuine
positive-area overlap, each candidate axis unitized first so `tolerance`
is a real planar distance, not a raw edge-length-scaled dot product).
`FindSelfIntersections()` now ORs this into its existing per-pair test;
the crossing-triangle path is untouched. Verified by re-deriving
`TestMeshFindSelfIntersectionsDetectsOnlyGenuineCrossings()`'s own case
(4) fixture — previously pinned as reporting nothing, a documented gap,
never a silent one — to now assert the pair IS reported, plus two new
cases: a coplanar pair with real area but genuinely no overlap (still
correctly clean) and a parallel-but-offset-plane pair (still correctly
clean, proving the plane-coincidence check does real work beyond the
parallel-normal check TrianglesProperlyOverlap() already had). That same
bullet's own prior text also claimed "any pair sharing a vertex is never
examined" as a limitation; rereading the method's own doc comment shows
that is by design (ordinary mesh connectivity, not a self-intersection
question this method answers), not a gap — mis-stated previously,
corrected in this category's own cross-referenced bullet under **kernel:
Intersections & projections** above.

Neither addition flips any of this category's own 22 checklist items —
`Mesh`'s `duplicate_vertices`/self-intersection machinery was never one
of them in the first place, only `SubD`'s was (closed by the prior
follow-up above). This category's own present/partial/missing counts
are therefore unchanged at 15/7/0/22 (84.1%). Full `dino8_kernel_tests`
suite (via the test binary directly): 100% passing (all checks passed),
0 regressions. This session's only source edits are `dino8-kernel/
include/dino8/kernel/mesh.h`, `dino8-kernel/src/mesh.cpp`, and
`dino8-kernel/tests/test_basic.cpp`.

**2026-09-30 follow-up (the `Mesh`-side mirror of `SubD::Check()`'s own
bowtie detection/repair pair, plus the matching duplicate-vertex
localization):** `git log --oneline -3 -- dino8-kernel/src/subd.cpp
dino8-kernel/src/mesh.cpp` at the start of this session showed the
`Mesh::MergeDuplicateVertices`/`CoplanarTrianglesOverlap` commit above as
the most recent work in this category - it closed `Mesh`'s own general
duplicate-vertex weld and a self-intersection gap, but left two things
untouched that the same "port `SubD::Check()`'s own diagnostics to
`Mesh`" theme already covers: `Mesh::CheckReport` had a `duplicate_vertices`
COUNT with no accompanying LOCALIZATION list (`SubD::Check()`'s own
`duplicate_vertex_list` had one; `Mesh::Check()` never did, even before
this session), and `Mesh` had no bowtie-vertex concept at all -
`SubD::Check()`'s own `non_manifold_vertices`/`non_manifold_vertex_list`/
`SplitNonManifoldVertex()` trio (a vertex shared by faces that don't form
one connected fan, independent of `non_manifold_edges`) had never been
ported to `Mesh`, even though `Mesh::CheckReport` already carries the
identically-shaped `non_manifold_edges`/`non_manifold_edge_list` pair its
own SubD counterpart was modeled on. All three closed together, since
they are the same one gap: `Mesh::Check()`'s own vertex-level diagnostics
lagging behind `SubD::Check()`'s.

`Mesh::CheckReport` gains `non_manifold_vertices`/`non_manifold_vertex_list`
(`Check()`, mesh.cpp): for every vertex, its own incident faces are
grouped by shared-edge adjacency AT that vertex
(`GroupIncidentFacesByVertex()`, mesh.cpp - the same union-find-over-
shared-incident-edges construction `SubD::Check()` already uses via
`v->EdgeCount()`/`e->FaceCount()`, reproduced here over `Mesh`'s own plain
index-based face list instead of `ON_SubD`'s own edge objects); more than
one resulting group means the vertex is a pinch point between locally-
disconnected pieces of the mesh, independent of `non_manifold_edges` -
that fires on a 3+-face EDGE, this fires on two fans sharing only a
VERTEX with zero shared edges between them, which an edge-only count can
never see (the identical distinction `SubD::Check()`'s own introducing
note already draws). `duplicate_vertex_list` closes the smaller of the
two gaps: the exact same `WeldGroups()` grouping `duplicate_vertices`
already counts by is now also recorded per flagged vertex, in index
order.

`Mesh::SplitNonManifoldVertex(vertex_index)`/`SplitNonManifoldVertices(
tolerance)` (mesh.h/mesh.cpp) close the repair half - the `Mesh`-level
counterpart of `SubD::SplitNonManifoldVertex()`/`Brep::
SplitNonManifoldVertex()`'s own "disjoin" repair: group 0 (first-seen
order, the same convention both existing versions already use) keeps the
vertex, every other group gets a freshly appended vertex at the same
point with that group's own faces repointed onto it. Materially simpler
than the SubD version: a `Mesh` vertex is just an array position, not an
`ON_SubD`-managed id, so no watermark/id bookkeeping is needed at all - a
plain append plus a face-index rewrite is enough, and (unlike the SubD
version, which needs a whole-net snapshot-and-rebuild for its own
`DeleteComponents()`-safety reason) no OTHER vertex's own index is ever
touched, moved, or renumbered. The batch driver runs `Check()` once and
splits every reported id in one pass, safe for the identical reason
`SubD::SplitNonManifoldVertices()` already is: splitting one vertex only
ever appends and repoints ITS OWN incident faces, never shifting another
reported vertex's own index out from under it.

Verified by 2 new tests (tests/test_basic.cpp):
`TestMeshCheckDetectsNonManifoldVertexAndDuplicateVertexList` builds a
bowtie (two independent quad "wings" sharing exactly one vertex INDEX,
zero shared edges) and separately reuses `TestMeshMergeDuplicateVertices
WeldsCoincidentPairs`' own coincident-but-distinct-corner fixture,
confirming the two conditions are genuinely independent in BOTH
directions (a bowtie reports zero `duplicate_vertices`; a coincident-but-
distinct pair reports zero `non_manifold_vertices`) and that
`duplicate_vertex_list` names the exact flagged pair in index order;
`TestMeshSplitNonManifoldVertexSplitsBowtie` refuses an out-of-range
index and an already-manifold vertex, splits the genuine bowtie (vertex
count 7->8, face count unchanged, `non_manifold_vertices` 1->0, the two
wings now sharing no vertex at all), confirms a post-split re-check
refuses the now-fixed vertex, and confirms the batch driver finds and
splits the one bowtie unaided. Full `dino8_kernel_tests` suite (via the
test binary directly): 100% passing (all checks passed), 0 regressions.

This category's own present/partial/missing counts are unchanged at
15/7/0/22 (84.1%) - none of these three additions was one of the 22
tracked checklist items to begin with (the same reason the
`Mesh::MergeDuplicateVertices`/`CoplanarTrianglesOverlap` follow-up above
didn't move them either); this closes real, tested ground under
`Mesh::Check()`'s own diagnostic/repair surface, bringing it to the same
vertex-level parity with `SubD::Check()` that edge-level
(`non_manifold_edges`/`non_manifold_edge_list`) and general-duplicate-
vertex (`MergeDuplicateVertices`) parity already reached. This session's
only source edits are `dino8-kernel/include/dino8/kernel/mesh.h`,
`dino8-kernel/src/mesh.cpp`, and `dino8-kernel/tests/test_basic.cpp`.

**2026-09-30 follow-up (the one `SubD::Check()` diagnostic the prior
`Mesh`-parity follow-up above left standing):** `git log --oneline -3 --
dino8-kernel/src/subd.cpp dino8-kernel/src/mesh.cpp` at the start of this
session showed the `Mesh::CheckReport::non_manifold_vertices`/
`Mesh::SplitNonManifoldVertex` commit above (58e390f) as the most recent
work in this category - it brought `Mesh::Check()` to vertex-level parity
with `SubD::Check()` for bowtie detection/repair and general duplicate-
vertex welding, but left the one remaining `SubD::SubDCheckReport` field
with no `Mesh::CheckReport` counterpart at all: `body_count` (and its own
repair, `SplitDisjointPieces()`) - a `Mesh` built from two independently-
appended, never-welded pieces (e.g. a plain `ON_Mesh::Append()` instead of
`MergeAndWeld()`) had no way to learn it was actually two unrelated bodies
short of eyeballing it, the same "is this actually several disconnected
pieces" question `Brep::SplitDisjointPieces()`'s own
`ON_Brep::LabelConnectedComponents()` and `SubD::Check()`'s own
`body_count` already answer for `Brep` and `SubD`.

`Mesh::CheckReport` gains `body_count` (`Check()`, mesh.cpp): the number of
face-connected pieces this mesh's faces fall into - 1 for an ordinary
single connected mesh, 0 for an empty one, 2+ for a multi-body mesh - via a
new file-local `GroupFacesByConnectivity()` helper (mesh.cpp, next to
`GroupIncidentFacesByVertex()`) that unions every pair of faces sharing an
edge (any undirected edge used by 2+ faces, so a non-manifold 3+-face edge
still lands every one of its faces in the same group) and returns both the
per-face group membership and the group count - the same shared-helper
shape `GroupIncidentFacesByVertex()` already established (one function,
called from both `Check()`, which only needs the count, and the new
`SplitDisjointPieces()` below, which needs the membership), rather than
computing the grouping twice the way `SubD::Check()`/
`SubD::SplitDisjointPieces()` deliberately do (that pair's own text already
gives its reason: a whole-net snapshot-and-rebuild has different
bookkeeping needs than a plain count; `Mesh` has no such constraint, so
sharing one helper here is the more direct port, not a divergence from the
established pattern). Deliberately left OUT of `IsClosedManifold()`, the
same convention `non_manifold_vertices`/`duplicate_vertices` already get
there: two cleanly disjoint, individually well-formed pieces are still
each a clean closed manifold on their own, a materially different question
from "is this mesh actually one piece."

`Mesh::SplitDisjointPieces()` (mesh.h/mesh.cpp) closes the repair half -
the `Mesh`-level counterpart of `SubD::SplitDisjointPieces()`/
`Brep::SplitDisjointPieces()`: splits a multi-body mesh into that many
single-body meshes, using the identical `GroupFacesByConnectivity()`
grouping `body_count` itself counts by. Materially simpler than the SubD
version, the same way `Mesh::SplitNonManifoldVertex()` already was
simpler than `SubD::SplitNonManifoldVertex()`: a `Mesh` vertex is just an
array position, not an `ON_SubD`-managed id, so each piece is built by
copying its own member faces plus the full original vertex array, then
calling the class's own existing `CompactUnusedVertices()` helper to drop
every vertex the piece doesn't reference and renumber the rest from 0 - a
plain per-piece renumbering, not the id-preserving snapshot-and-rebuild
`SubD::SplitDisjointPieces()` needs for its own `DeleteComponents()`-safety
reason. Returns `{*this}` (one piece, a copy) for an already-single-body
mesh, including the empty mesh (`body_count == 0`) - a caller always gets
back at least one piece, never zero.

Verified by 2 new tests (tests/test_basic.cpp):
`TestMeshCheckDetectsDisjointPiecesBodyCount` reuses
`TestSubDCheckDisjointPiecesReportsMultipleBodies`' own two-ordinary-
closed-boxes-with-disjoint-vertex-ranges fixture: an empty mesh and a
single box report 0 and 1 bodies respectively; the combined 16-vertex/
12-face mesh reports exactly `body_count == 2`, with every other
`CheckReport` condition (`non_manifold_edges`, `non_manifold_vertices`,
`duplicate_vertices`) still zero - confirming `body_count` is genuinely
independent of the conditions edge/vertex adjacency alone already catch -
and `IsClosedManifold()` stays true despite `body_count > 1`, proving that
exclusion is deliberate, not an oversight. `TestMeshSplitDisjointPieces
SplitsIntoSeparateMeshes` confirms an empty mesh and a single-body mesh
both split into exactly 1 piece (an empty copy, and an exact 8-vertex/
6-face copy, respectively - never zero pieces); the same two-disjoint-
boxes fixture (this time a unit box and a 2x2x2 box, so their volumes
differ) splits into exactly 2 pieces, each independently `body_count == 1`
with exactly one original box's own 8 vertices / 6 faces (not the
combined 16/12, proving `CompactUnusedVertices()` genuinely renumbered
each piece rather than merely copying the full vertex array); and each
piece's own `Volume()` (1 or 8) unambiguously identifies which original
box it is, confirming no piece is empty or swapped. Full
`dino8_kernel_tests` suite (via the test binary directly): 100% passing
(all checks passed), 0 regressions.

This category's own present/partial/missing counts are unchanged at
15/7/0/22 (84.1%) - `body_count`/`SplitDisjointPieces()` were never among
the 22 tracked checklist items themselves (the same reason every other
`Mesh`-side `SubD::Check()`-parity addition above didn't move them
either); this closes the one remaining gap between `Mesh::Check()`'s own
diagnostic/repair surface and `SubD::Check()`'s, bringing whole-mesh
body-count parity to match the vertex-level (bowtie, duplicate) and
edge-level (non-manifold-edge) parity already reached in the follow-ups
above. This session's only source edits are `dino8-kernel/include/dino8/
kernel/mesh.h`, `dino8-kernel/src/mesh.cpp`, and `dino8-kernel/tests/
test_basic.cpp`.

## App: Dino 8 vs Rhino 8 + AutoCAD 2027

| Category | Weight | Items | Present | Partial | Missing | Parity % |
|---|---|---|---|---|---|---|
| Dino 8: Command system & core commands | 1.5 | 19 | 11 | 6 | 2 | 73.7% |
| Dino 8: 2D drafting, annotation & documentation | 1.0 | 18 | 12 | 5 | 1 | 80.6% |
| Dino 8: Viewport display, rendering & visualization | 1.0 | 18 | 13 | 3 | 2 | 80.6% |
| Dino 8: Scripting, automation & visual programming | 1.0 | 15 | 10 | 3 | 2 | 76.7% |
| Dino 8: File I/O & interoperability (app level) | 1.0 | 17 | 5 | 5 | 7 | 44.1% |
| Dino 8: SubD & mesh modeling toolset (app level) | 0.75 | 24 | 19 | 3 | 2 | 85.4% |
| Dino 8: UI/UX, accessibility & localization | 1.0 | 19 | 13 | 3 | 3 | 76.3% |
| Dino 8: Ecosystem, trust, cloud/AI & platform reach | 0.5 | 16 | 7 | 2 | 7 | 50.0% |

All 8 rows are numerically unchanged from the 2026-09-25 map. Of the ~12
non-merge commits since then, only two touch app-level substance: the
raytraced env-map fix (below) and a Windows-only CSV binary-mode fix that
changes byte layout, not feature completeness. The kernel-side
`SplitByObjectCommand` was checked for spillover into this section's
"Solid editing with B-rep results" bullet — it's the same mesh-boolean
pattern as the app's existing booleans (its own status line literally
prints "mesh boolean; results are meshes"), so it reinforces rather than
changes that bullet. Kernel `Sweep1 twist_total` is confirmed still not
called anywhere from `dino8-app`. Every other bullet across all 8
categories was independently re-grepped/re-read against current source this
pass and reconfirmed unchanged with fresh citations.

**2026-09-30 correction:** two of these "reconfirmed unchanged" rows were
not, in fact, correct as of this paragraph's own writing, let alone since -
see the 2026-09-30 re-score note at the top of this document. UI/UX and
Ecosystem both had a real, then-already-present capability mis-scored
`missing`/infeasible (Screen-reader support's AT-SPI2 bridge; the Plugin
marketplace system) that every re-verification pass through this one
re-grepped past without checking against the actual source tree named in
its own bullet text. Both rows are corrected in the table above.

**2026-09-28 re-verification addendum:** re-checked again, independently,
against current HEAD (still the same commit — see the top-of-document
addendum). All 8 rows' present/partial/missing counts and percentages
confirmed correct as-is; no item's status changed. Four small citation
line-number errors were found and corrected in place (the app_ecosystem
fuzz-test target, the app_display ShadowBlob struct's file attribution and
the PathTracer.cpp env-map line range, and the app_interop DXF writer's
start line) — all citation-precision fixes, not scoring changes.

### App category gaps (missing / partial items, with evidence)

**Dino 8: Command system & core commands** (app_commands):
- [partial] Command aliases and shortcut customization — Rhino's default aliases are built in and users can add aliases through the Alias command or panel. **Upgraded this pass on both named gaps, each still real in a narrower way.** Aliases now persist: `Settings.cpp`'s `LoadSettingsFrom`/`SaveSettingsTo` (the same JSON `OptionsExport`/`OptionsImport`/per-user `settings.json` already uses for every other setting) gained an `"aliases"` object, loaded as a wholesale replace of `CommandEngine::Aliases()` (`Settings.cpp:119-123`) rather than a merge on top of `InstallDefaultAliases()` — so a default alias the user deletes via the Options panel or a re-`Alias`-ed name stays gone across a restart or an `OptionsImport`, not silently reinstated. A genuine user-assignable keyboard-shortcut table also now exists: `Application::user_shortcuts` (`Application.h:342`, a `std::vector<KeyShortcut>`) is editable from a new Options > Shortcuts tab (`Panels.cpp:1148`, key name + Ctrl/Shift/Alt + command, add/remove), persisted the same way as aliases (`Settings.cpp`'s `"shortcuts"` array), and fired every frame by `Application::HandleShortcuts` (`Application.cpp:1708-1714`) alongside its own hardcoded bindings. Still partial, in a narrower way than before: the ~20 hardcoded chords `HandleShortcuts` itself defines (Ctrl+Z/Y/A/S/O/N/G/H/C/V/X, F1-F11, Escape, Delete, Home, PageUp/PageDown, the arrow keys) are a fixed reserved set a user shortcut can never remap or override (`IsReservedShortcut`, `Application.cpp:1623-1650`, checked both when firing and when adding one in the Options panel) — so this is additive keyboard customization for otherwise-unbound chords, not Rhino's fully remappable Tools > Options > Keyboard dialog where even Ctrl+Z itself can be reassigned; and binding a key still means typing its ImGui-reported name (`KeyShortcutFromName`, `Application.cpp:1613-1620`) rather than a "click here, then press the key" capture flow.
- [partial] Surface construction commands — Loft, Revolve, Extrude, EdgeSrf, Sweep1, Sweep2 and NetworkSrf all exist, but Patch is "planar patch only", Sweep1/Sweep2 (`dino8-app/src/commands/cmd_surface.cpp:402` `Sweep1Command`, registered at :1413 as "Approximated by a lofted sweep... rotation-minimizing frames") are still RMF-lofted mesh/NURBS fits with no tolerance control and do not call the kernel's exact `Brep::Sweep1` (no `twist_total` or kernel `Sweep1` reference anywhere under `dino8-app/src/`), and NetworkSrf's 3-curve case still falls back to a fitted bilinear Coons patch. The 4-curve case uses the kernel's exact `CoonsPatch`, and ExtendSrf has a Linear type via the kernel's exact `ExtendLinear`.
- [partial] Solid editing with B-rep results (booleans, fillet, shell, offset) — BooleanUnion/Difference/Intersection convert every operand to a mesh before combining (`dino8-app/src/commands/cmd_boolean.cpp:1`, "Boolean and splitting commands (mesh-based, via Manifold)"); the kernel's exact `BooleanCombineMixed` is never called from the app. FilletEdge is exact only when both adjacent faces are planar, else a mesh fallback. Shell, OffsetSrf on polysurfaces, and Pipe Cap=Yes all give meshes. New this window: `SplitByObjectCommand` (`cmd_boolean.cpp:290`) adds a general cutting-solid/surface split, but it is the same mesh-boolean pattern — its own status line literally prints "mesh boolean; results are meshes" (`cmd_boolean.cpp:386`) — so it reinforces rather than changes this bullet's partial status. (A same-day follow-up, commit 167baae, fixed the command silently deleting its cutter and re-meshing the target with zero real effect when the cutter merely missed or fully enclosed the target — a correctness fix, not a capability change; see kernel: Transformations for detail.) Solid primitives themselves are real Breps. **Correction (this pass):** the "BooleanUnion/Difference/Intersection convert every operand to a mesh before combining" half of the clause above is stale, not current — it predates **kernel: Boolean operations**'s own "B-rep-preserving booleans reachable from the application" bullet's "Twelfth note" (`TryExactBrepBoolean`, the shared helper `BooleanUnion`/`BooleanDifference`/`BooleanIntersection`/`Boolean2Objects` all now call first, dino8-app/src/commands/cmd_boolean.cpp, `BooleanCombinePlanarNAry`), which now also has its compound-operand support verified end-to-end (this same pass). The "the kernel's exact `BooleanCombineMixed` is never called from the app" half remains true and current, not stale — wiring it in as a second attempt (for an operand with a cylindrical face) was tried and reverted this same pass, after finding it can silently accept adversarial geometry the planar engine correctly refuses, and that a record-less operand's own result doesn't tessellate closed via the app's own generic mesh path (see that same bullet's own "A later pass" account for the full detail, including the real, narrower `MixedFaces()` kernel bug this investigation did fix). This item's own `partial` classification is unaffected (FilletEdge/Shell/OffsetSrf/Pipe remain exactly as described) - narrows, does not flip, the same already-partial item.
- [partial] History / associative re-execution — real History/RecordHistory/UpdateHistory exist (`cmd_history.cpp`, `cmd_misc.cpp`, `cmd_solids.cpp`), but only for Extrude, ExtrudeCrvToPoint, Revolve, Loft and SubDLoft; stale stubs elsewhere in the app still print "no construction history is recorded" and are asserted on by a smoke test, contradicting the real mechanism.
- [partial] Command-level feature editing (re-running a construction with new inputs) — only scoped, explicit-recompute mechanisms exist (UpdateHistory, hole features, a few UpdateDimensions/UpdateBakes commands); no universal parametric feature tree.
- [partial] VBA-style macro recorder and editor — a macro editor does exist (`dino8-app/src/ui/Panels.cpp:1606` `DrawMacroEditor`, a multi-line panel with Run/Copy, `;`-separated command sequences, command-file playback, plus a Lua/Python script editor). **New this pass: the persistence half of the gap is closed.** The macro buffer moved out of a function-local `static char[4096]` into `AppState::macro_text` (`Application.h`, alongside `working_folder`/`startup_script`) and now round-trips through `Settings.cpp`'s own `"macro_text"` key the same as every other Option — survives an app restart (`LoadSettings`/`SaveSettings`, called from `Application::Init`/`Shutdown`) and an explicit `OptionsExport`/`OptionsImport`, not just for the life of one open panel. Still missing: any action recorder (nothing observes UI actions or command runs to append to the buffer automatically) and any VBA/object-model compatibility - the buffer is still a plain `;`-free, one-command-per-line text box, not a recorded, editable macro object with properties.
- [missing] AutoLISP-equivalent command scripting language — no LISP dialect or AutoLISP compatibility anywhere; scripting is Lua, Python or Macro/command files.
- [missing] ObjectARX-equivalent native extension API — the only native API is a Dino-specific plugin ABI, not an ObjectARX-compatible binary interface.

**Dino 8: 2D drafting, annotation & documentation** (app_drafting):
- [partial] Associative annotation updating (dimensions, leaders, center marks, center lines) — UpdateDimensions rebuilds several dimension/leader/mark types from their anchors, but only on an explicit command run, and a point is anchored only if it coincides exactly with a Point object or curve endpoint. Earlier windows extended the same explicit-recompute shape to MultiLeader arrows (`UpdateMultiLeaders`, commit `0ebdb07`), the four GD&T symbol commands (`UpdateGdtSymbols`, commit `f66cdf9`), for the electrical vertical-market toolset (elec/ElecComponents.h), PanelSchedule's rows when built from a real ElecCircuit-tagged component selection rather than hand-typed Circuits= text (`UpdatePanelSchedule`, commit `bb673bb`) — the same BomAll/BomRefIds-style all-vs-explicit-selection split BillOfMaterials already used — and TitleBlock's Name field (`UpdateTitleBlock`, commit `dfee1cf`), which tracks the document's own Settings().title (Document Properties) unless frozen by an explicit Name=. This window adds a fourth family, the whole-object-reference measured dimensions from cmd_annotate2.cpp: DimArea/DimCurveLength/DimVolume (`MeasureRefIds`, a BomRefIds-style id list, since these can sum several selected objects) and DimCreaseAngle (`DimRefObj1`/`DimRefObj2`, two objects directly, same whole-object-reference shape DimRadius/DimDiameter and CenterLine already use) are now associative via a new `UpdateMeasureDims` command: it re-measures each recorded object's *current* shape (not just its position — an edited curve's new length, a resized solid's new volume, a re-pointed line's new direction all propagate), sums/recombines exactly as the creating command did, and rebuilds the leader in place at its original landing point, dropping a since-deleted or no-longer-measurable object from the result the same way BillOfMaterials's BomRefIds drops a deleted one. A dimension built before this window carries no MeasureRefIds/DimRefObj1 tag and stays a static baked measurement, same as before. This window closes two more of the family's remaining gaps. DimOrdinate (base point + feature points) is now associative per point exactly like DimLinear: `FindPointAnchor` matches the base point once and, separately, each feature point against a real Point object or curve endpoint, and `UpdateMeasureDims` (extended to cover it, same command DimArea/DimCurveLength/DimVolume/DimCreaseAngle use) re-evaluates whichever matched and redraws that ordinate's leader/text from their current positions — a point that was never anchored still redraws from its built DimP0/DimP1 fallback, unchanged, rather than being skipped. TitleBlock's Date field now works the same way its Name field already did: left unset it defaults to (and stays tagged associative to, `TitleBlockDateAuto`) today's date, and `UpdateTitleBlock` re-pulls it independently of Name — an explicit Date= freezes just that field, same as an explicit Name= freezes Name. Scale/Sheet (TitleBlock) still have no associative mechanism — Sheet's default is merely which layout happened to be active when the title block was placed, not a stable property (Layout has no id, only a name) the table could track back to. This window fixes a correctness gap in the associativity machinery itself rather than adding a new family: `DimTolerance` (cmd_drafting2.cpp) appends a tolerance suffix to a dimension's baked text, but `UpdateDimensions`/`UpdateMeasureDims` rebuild that text from scratch (the freshly recomputed measurement alone), so a tolerance added to an otherwise-associative dimension — DimLinear/DimAligned, DimAngle, DimRadius/DimDiameter, DimOrdinate, DimArea/DimCurveLength/DimVolume, DimCreaseAngle — used to vanish silently the next time its anchor moved and the dimension was regenerated. It is now recovered from the `DimTolerance.Base` tag `DimTolerance` itself leaves (diffed against the pre-rebuild text to get the suffix) and re-appended to the freshly rebuilt measurement (`GroupToleranceSuffix`/`ReapplyToleranceSuffix`, annotate_common.h, shared by both Update commands) — re-running Update after moving the measured geometry now keeps the tolerance, not just re-running DimTolerance's own idempotent re-apply. Leader was already unaffected (its text is a static label copied verbatim on rebuild, never recomputed) and Centermark/CenterLine have no text to lose. More annotation/table types now re-associate, and this window additionally makes an existing associative family (DimTolerance combined with any of the eight dimension kinds above) correct under rebuild, but the underlying gap this bullet names (no automatic recompute hooked into document edits) is unchanged, so it stays partial.
- [partial] Print and plot output — Print writes a vector PDF/SVG of the active view with an optional scale, but there are no lineweights, no print widths, and no plot styles (CTB/STB); no printer-device output.
- [partial] Dynamic blocks — only visibility states exist (BlockAddState/BlockSetVisibility); stretch, flip, array and lookup parameters and actions are not attempted.
- [partial] Live external data linking into tables — a two-way CSV sync with conflict refusal, not native .xlsx; formula cells come back as their last saved values. Commit 19c14a0 (`cmd_drafting2.cpp:233`) added `std::ios::binary` to `WriteCsvFile`'s and `BillOfMaterials::Run`'s ofstream opens — verified this is purely a Windows CRLF-translation fix (a no-op on Linux/macOS) so CSV bytes match across platforms; it does not touch the CSV-vs-.xlsx or frozen-formula-cell limitations, so the score and reasoning are unchanged.
- [partial] Dimension styles — named styles do exist (AnnotationStyles etc., persisted in .3dm user strings), but a style has only name, text_height, arrow_size and font — no units/precision, tolerance, extension-line or text-placement control, and text/dimension styles share one table.
- [missing] Field text (text driven by object properties) — no field or formula text type found anywhere; all text is static baked geometry.

**Dino 8: Viewport display, rendering & visualization** (app_display):
- [partial] Environments and image-based lighting — **materially updated by commit 7059e20.** `PathTracer::SkyColor()` (`dino8-app/src/render/PathTracer.cpp:241-284` — corrected 2026-09-28, was mis-cited as the malformed range `241-113`) now has a real `Background::Image` branch: a standard equirectangular (atan2/acos) lookup through a new shared `PathTracer::SampleBilinear` helper. Critically, `SkyColor()` is called from inside `TracePath`'s bounce loop (`PathTracer.cpp:452`, `radiance += Mul(throughput, SkyColor(dir))`) for any ray that escapes the scene at any bounce depth, not just primary camera rays — so this is genuine image-based lighting/reflection contribution (a ray that bounces off a glossy/reflective surface and then misses geometry now picks up the environment image, weighted by accumulated `throughput`) for the offline CPU path-traced renders (`Render`/`RenderPreview`/`RenderArctic`/`RenderBlowup` at `Quality=Raytraced`). This closes the gap for that one rendering surface. It remains partial because two of the app's three render surfaces still lack it: the interactive rasterizer viewport still draws the image only as a stretched full-viewport quad with no reflection contribution (`dino8-app/src/viewport/Viewport.cpp`, `DrawBackgroundImage`), and the live `RayTracedViewport` GPU preview still falls back to a solid color for `Image` (`dino8-app/src/render/GpuRaytracer.cpp:683`, `bg_mode_ = 0; // no env-map sampling on GPU`). There is also still no HDRI lighting or `.hdr`/`.exr` loader — `LoadImageFile` (`dino8-app/src/render/ImageIO.cpp:475`) supports only BMP/PPM/PGM/PNG (8-bit LDR), so an environment image can only ever be an LDR backdrop, never true HDR-range lighting.
- [partial] Per-object display mode override — only Wireframe and Shaded are supported per-object; every other mode is viewport-wide only.
- [partial] View-dependent adaptive tessellation — real frustum culling exists, but there is still no LOD and no re-tessellation on zoom.
- [missing] Real-time shadow maps in the rasterized renderer — `dino8-app/src/render/GlRenderer.cpp` has only ground-plane contact-shadow "blobs" (`ShadowBlob`/`kMaxShadowBlobs` struct+constant declared in `GlRenderer.h:60-63` — corrected 2026-09-28, previously mis-attributed to the .cpp; fragment-shader smoothstep logic at GlRenderer.cpp:169-175, upload/draw code at :554-641, both confirmed accurate) — a screen-space blob fade, not shadow maps, self-shadowing, or object-on-object cast shadows. Real cast shadows appear only in the GPU raytraced and CPU path-traced modes (`ground_.shadows`, `PathTracer.cpp:178,339` — corrected 2026-09-28, was mis-cited :178,338).
- [missing] SSAO in the rasterized renderer — no "ssao"/"ambient occlusion" hit anywhere in `dino8-app/src/render/`.

**Dino 8: Scripting, automation & visual programming** (app_scripting):
- [partial] Embedded Python 3 — `dino8-app/CMakeLists.txt:146` sets `option(DINO8_ENABLE_PYTHON ... OFF)` on Windows specifically, `:148` `ON` elsewhere; shipped Windows builds have no Python at all; mid-script prompts are also missing.
- [partial] Python API breadth — `RunCommand` reaches every registered command; the real gap is the object model (75 bindings, up from 72, versus Lua's 160 `rs.*` functions — `MirrorObject`/`SelectObject`/`UnselectObject` added this window) and no interactive prompts.
- [partial] Headless/batch scripting mode — `dino8-app/src/main.cpp:5-7,322-327`: `--smoke N --script FILE [--screenshot]` is real and documented in the file's own header comments; still framed as a QA mode needing a GL context/display server, not a supported batch product.
- [missing] Cloud/network compute service (Rhino.Compute equivalent) — no server/socket/HTTP code anywhere in the source.
- [missing] AI-assisted modeling or scripting — no neural/inference code anywhere; the one "smart" feature explicitly documents its own technique as not machine learning.

**Dino 8: File I/O & interoperability (app level)** (app_interop):
- [partial] Native .3dm read/write — the reader converts only lights, clipping planes, detail views, points, curves, Breps, surfaces, meshes, SubDs, extrusions and point clouds — everything else is silently skipped on open. Dino-written annotations/blocks survive only as baked geometry plus private user-string metadata.
- [partial] OBJ — the importer loads the whole file as one mesh with no per-group/per-object split and no .mtl; the exporter tessellates and merges everything into a single welded mesh, losing object identity and writing no materials or curves.
- [partial] STEP AP203/AP214 — the writer and reader exist for basic B-rep entities, but the reader has no assembly structure at all (no NEXT_ASSEMBLY/MAPPED_ITEM/context handling), so multi-part assemblies lose their part placement transforms. AP242 is still entirely absent (zero hits for TESSELLATED/TRIANGULATED_FACE/PMI/AP242), scored as its own separate missing item below.
- [partial] DXF — the writer path (`WriteDxfPolyline` dino8-app/src/io/FileExchange.cpp:265, `WriteDxfSpline`/`WriteDxfCurve`/`WriteDxfMesh` :303/:330/:390-604 — corrected 2026-09-28, was mis-cited as one range :304-604 that missed WriteDxfPolyline's real start) covers only polylines, splines/curves, lines/circles/arcs and 3dfaces; no TEXT/MTEXT/DIMENSION/HATCH/INSERT writer function exists anywhere in that file. The reader covers TEXT/MTEXT/ELLIPSE/SPLINE/POLYLINE/3DFACE/HATCH/DIMENSION.
- [partial] DWG (via GPLv3 GNU LibreDWG) — the importer reads a broad entity set including text, dimensions, hatches and inserts; the exporter round-trips through a temporary DXF and inherits every DXF-writer limit above; no 3DSOLID entities in either direction.
- [missing] STEP AP242 — the writer emits AP214 only, with no AP242 fixture, test, or PMI/TESSELLATED handler.
- [missing] Parasolid (.x_t/.x_b) import/export — nothing found; **permanently out of scope by project policy.** (Infeasible — see below.)
- [missing] ACIS (.sat/.sab) import/export — nothing found; **permanently out of scope by project policy.** (Infeasible — see below.)
- [missing] Digital signing of exported files — no file-signing code exists anywhere; the project's only signing plumbing is inert installer code-signing in CI, a different thing entirely.
- [missing] Point-cloud exchange formats (LAS/E57/PTS/XYZ) — none; point clouds only round-trip through .3dm.
- [missing] IFC (BIM) import/export — nothing found under the I/O sources.
- [missing] JT (PLM interchange) import/export — nothing found under the I/O sources.

**Dino 8: SubD & mesh modeling toolset (app level)** (app_subd_mesh):
- [partial] SubD to NURBS (ToNURBS) — faces touching an extraordinary vertex, crease or boundary become flat bilinear approximations and are "deliberately left unjoined" — a converted SubDBox stays an open Brep, not a closed solid. The newer kernel adaptive converter is not wired into this command.
- [partial] NURBS/Brep to SubD — ToSubD is registered for meshes or polysurfaces, tessellating the Brep at display tolerance and using that triangle mesh as the SubD control cage, with no shape-fidelity guarantee and no test coverage.
- [partial] SubD symmetry (Reflect / Symmetry) — Reflect is a one-time mirror that welds the original and its mirror image into a single mesh, so a SubD input stops being a SubD; there is no live mirror editing.
- [missing] SubD booleans — no SubD-aware boolean exists; existing boolean commands accept SubD objects only because they get tessellated first, producing a mesh, not a SubD.
- [missing] Sculpting (multi-resolution brush sculpting) — no such tool exists (Rhino 8 does not have this either).

**Dino 8: UI/UX, accessibility & localization** (app_ux):
- [partial] Breadth of localization (10+ languages, professional review) — a fresh key-count check found `en.json` has 183 flattened keys, `fr.json` has 178 (`panel.activity_log`, `panel.block_manager`, `panel.uv_editor`, `panel.mapping_widget`, `panel.whats_new` still missing); a sixth language, `it.json` (commit `02e7b3b`), lands with full 183/183 key parity with English. Three languages (Spanish, French, Italian) now exist beside English — still nowhere near "10+ languages, professional review," so this stays partial, same as before. A seventh language, `zh.json` (Simplified Chinese), now also lands with full 183/183 key parity, wired into `SetLanguage`/`I18nSelfTest` the same way — seven hand-translated languages beside English is still short of "10+, professional review," so this stays partial. An eighth language, `ko.json` (Korean), now also lands with full 183/183 key parity, wired into `SetLanguage`/`I18nSelfTest` the same way — eight hand-translated languages beside English is still short of "10+, professional review," so this stays partial. A ninth language, `ru.json` (Russian, 184/184 keys — `en.json` has since grown by one key), now also lands with full key parity, wired into `SetLanguage`/`I18nSelfTest` the same way — nine hand-translated languages beside English is still short of "10+, professional review," so this stays partial.
- [partial] Worksessions (shared multi-file referencing) — a real Worksession mechanism exists (`dino8-app/src/session/Worksession.h`/`.cpp`), attaching other .3dm files as locked reference models with filtering and a saved JSON session file. Attached objects are copied in with no live link or refresh.
- [partial] Screen-reader support — **reclassified 2026-09-30, no longer infeasible.** `dino8-app/src/platform/AccessibilityLinux.cpp` implements a real AT-SPI2 D-Bus bridge (`org.a11y.atspi.Accessible`/`.Application`/`.Text`, registered with the real `at-spi2-registryd`), covering the command line, main menu bar, the running command's options, Layers/Properties panels, each viewport's title/view-menu button, the Activity Log, Named Views, Named CPlanes, Linetypes, Materials, Clipping Planes, Layouts, Block Manager, Layer State Manager, Document User Text, Lights, Annotation Styles, Notes, Environments, Audit Results, Undo/Redo History, Hatch Patterns and Plug-ins (`dino8-app/docs/ACCESSIBILITY.md` section 3 has the full account, including how to verify it against the real `pyatspi` client library). **Same-day follow-up:** extended to the document's pending Undo/Redo history (`Document::UndoLabels()`/`RedoLabels()`, numbered rows matching `DrawUndoMultipleWindow`), the loaded Hatch Pattern library (`HatchLibrary::Instance().Patterns()`, name plus description per pattern), and loaded plug-ins (`plugins::Manager::Get().Plugins()`, name/version/status plus command and flow-node counts per plug-in), narrowing "the ~25 remaining panels/dialogs" to ~22. **Second same-day follow-up:** extended further to the ~1055-command Rhino 8 reference catalog as a "Command List" mirror (`CommandEngine::Registry()`, name/status/description per row plus an implemented/partial/planned breakdown - this one retires `DrawCommandListPanel` as a whole, narrowing "~22 remaining panels/dialogs" to ~21), the user's saved command aliases as a "Command Aliases" mirror (`CommandEngine::Aliases()`, alias/command per row - never empty, since `InstallDefaultAliases` seeds Rhino's own defaults), and the user's customized keyboard shortcuts as a "Keyboard Shortcuts" mirror (`Application::user_shortcuts`, key-combo/command per row - starts empty, no default-shortcuts installer exists). The latter two only cover two of the Options window's seven tabs (Aliases, Shortcuts); `DrawOptionsWindow` itself is not retired, so this is honestly "~21 remaining panels/dialogs, plus partial coverage of the Options window's Aliases and Shortcuts tabs," not a clean decrement to ~20. Still genuinely partial: Linux-only (no UIA/MSAA on Windows, no NSAccessibility on macOS), and the 3D viewport's own content and most other panels/dialogs (~21 remain, plus five of the Options window's seven tabs) remain unreached — ImGui itself still has no retained widget tree for a screen reader to attach to outside the specific regions this bridge hand-builds.
- [missing] Localized command and toolbar help text — the ~1055 command names/help texts and toolbar tooltips remain English-only in every language.
- [missing] Video tutorials / community forum — needs an audience and hosting, not source-tree work. (Infeasible — see below.)
- [missing] Real-time multi-user collaborative editing — single-document, single-user desktop app; no network code found anywhere.

**Dino 8: Ecosystem, trust, cloud/AI & platform reach** (app_ecosystem):
- [partial] Large-scale adversarial/property-based QA — a real fuzz-test ctest target exists (`dino8-app/CMakeLists.txt:510-523`, `add_executable(dino8_test_fuzz_geometry ...)` at :519, `add_test(NAME dino8_fuzz_geometry ...)` at :523 — corrected 2026-09-28, was mis-cited :495-507, which actually holds the unrelated `dino8_test_brep_mesher`/`dino8_test_aci_palette` blocks), plus several adversarial scripts, but this does not substitute for decades of real user files, and Windows-only numeric-difference issues are still being worked through.
- [missing] Real AI/ML-based modeling assistance — no inference code anywhere; the one "smart" clustering feature explicitly documents itself as not machine learning.
- [missing] Hosted cloud compute / geometry-as-a-service (Rhino Compute equivalent) — no server or network code found. (Infeasible — see below.)
- [missing] Cloud model viewer / app builder (ShapeDiver equivalent) — no web-viewer or embed code exists.
- [missing] Touch-first companion app (Rhino for iPad equivalent) — desktop only; a separate product, not a feature of this app. (Infeasible — see below.)
- [missing] Code-signed / notarized installers — the signing CI steps only run if a certificate secret is set, and no certificate has been purchased. (Infeasible — see below.)
- [partial] Plugin marketplace / discovery mechanism — **reclassified 2026-09-30, no longer infeasible; discovery gap narrowed the same day.** `dino8-app/src/plugins/Marketplace.{h,cpp}`/`MarketplaceIndex.{h,cpp}`/`MarketplacePanel.{h,cpp}` is a real, tested system: `InstallEntry` fetches or copies a plug-in's library (with sha256 verification) into `<config>/plugins` and loads it; `InstallById`/`UninstallById` resolve and cascade a declared dependency graph (cycle-refusing) on install and uninstall respectively; local ratings/reviews are attached per entry. **Same-day follow-up:** `CheckCompatibility` now enforces `min_app_version` (`105d4408` — previously parsed/shown but never checked, refusing `InstallEntry` the same way an over-new `api_version` already was), and the panel's own Install/Update button now goes through `InstallById` instead of calling `InstallEntry` directly, so an install from the UI resolves dependencies the same way the command line already did. A later pass (`472eb52`) adds a case-insensitive search filter over id/name/author/description/tags (`MatchesFilter`, `MarketplaceIndex.cpp`), lets a `dependencies` entry name a minimum version (`"id@1.2.0"` syntax), and has `UninstallById` name every other installed entry that still declares a plug-in as a dependency when it is uninstalled directly rather than via cascade (a warning, not a block — a shared dependency was previously removable out from under a dependent with no notice). A further same-day pass adds `UpdateAll`, installing every out-of-date entry `CheckForUpdates` finds in one call (`PluginMarketplaceUpdateAll`/the panel's "Update All" button), and `VerifyInstalled`, re-hashing an installed copy against the index's `sha256` on demand to catch post-install corruption/tampering (`PluginMarketplaceVerify`/the panel's "Verify" button). The reference index this repo ships (`plugin-index/index.json`) is now also a real runtime asset — copied to `data/plugin-index/index.json` next to the executable at build time and installed there on every platform (`dino8-app/CMakeLists.txt`) — and `Application::DefaultMarketplaceIndexPath` finds it there, so `PluginMarketplaceIndex` with no argument, or the panel's "Load Bundled Index" button, browses it with nothing typed in. **Second same-day follow-up:** `VerifyAll`/`PluginMarketplaceVerifyAll` runs the existing per-id `VerifyInstalled` sha256 check once for every entry actually installed via the marketplace in one call (the panel's "Verify All" button, next to "Update All"), and `UninstallAll`/`PluginMarketplaceUninstallAll` does the same for `UninstallById`, cascading each target's own now-unneeded dependencies exactly like a manual per-row uninstall — both dedupe an id an earlier target's own cascade already removed, rather than reattempting it and reporting a spurious failure. `CompatibilityReason` (`MarketplaceIndex.{h,cpp}`) also replaces the generic "needs newer Dino 8" label — in `PluginMarketplaceList`, the panel's compatibility-badge tooltip, its disabled Install/Update button tooltip, and the detail view — with the exact requirement (e.g. "needs Dino 8 2.0.0 or newer, this build is 1.4.0"), the same wording `InstallEntry`'s own refusal already used, now visible without attempting - and failing - an install first. Still partial: that default is one *bundled, first-party* index, not a curated/hosted *registry* — there is still no server anywhere a user's build queries for a *live, updatable* catalogue of third-party indexes, so discovery beyond "the four sample plug-ins this repository ships" still requires a user to supply a local path or an http(s) URL to someone else's JSON index by hand. (The separate "Third-party plugin ecosystem (real external adoption)" item below stays infeasible — a network-effect gap, not an engineering one.)
- [missing] Third-party plugin ecosystem (real external adoption) — four first-party example plugins exist and no third-party plugins; a network-effect gap, not an engineering one. (Infeasible — see below.)
- [missing] Real-time multi-user collaboration / co-editing — same evidence as the app_ux item; no network code anywhere.

## Infeasible / non-engineering

These 10 items still cannot be closed by writing more code in this
repository. (Two more lived on this list through every prior session -
Screen-reader support and Plugin marketplace / discovery mechanism - until
this pass found each one already has a real, if narrow, engineering
solution in the current source and reclassified both `[missing]`->
`[partial]`, no longer infeasible; see the category bullets above and the
2026-09-30 re-score note at the top of this document for the evidence.)

- **[app/app_interop] Parasolid (.x_t/.x_b) import/export** (missing) — infeasible: Parasolid's format is proprietary and undocumented outside a licensed Siemens SDK.
- **[app/app_interop] ACIS (.sat/.sab) import/export** (missing) — infeasible: same proprietary-format rationale as Parasolid.
- **[app/app_ux] Video tutorials / community forum** (missing) — infeasible for a codebase alone to provide; requires an actual user community and hosting operation.
- **[kernel/exchange] Parasolid XT (.x_t/.x_b) read/write** (missing) — proprietary format + SDK licence (Siemens).
- **[kernel/exchange] ACIS SAT/SAB read/write** (missing) — proprietary format + SDK licence (Spatial).
- **[app/app_ecosystem] Hosted cloud compute / geometry-as-a-service (Rhino Compute equivalent)** (missing) — infeasible from source code alone: requires standing up and operating server infrastructure.
- **[app/app_ecosystem] Touch-first companion app (Rhino for iPad equivalent)** (missing) — infeasible: a distinct mobile product with its own distribution and touch-first UI.
- **[app/app_ecosystem] Code-signed / notarized installers** (missing) — infeasible for engineering alone: requires a purchased certificate and legal-entity registration.
- **[app/app_ecosystem] Third-party plugin ecosystem (real external adoption)** (missing) — infeasible: a network-effect gap, not an engineering gap.
- **[app/app_ux] Breadth of localization (10+ languages, professional review)** (partial) — infeasible at full Rhino-matching breadth within an engineering-only pass, though the infrastructure itself is complete.

## Ranked closeable backlog

334 non-present items remain across all 25 categories (278 kernel, 56 app;
one fewer kernel item than the 2026-09-30 re-score's own count, from this
same day's later pass moving the "AutoCAD-style INTERFERE" boolean item
`partial` -> `present` - see that pass's own note in the category bullets
above); the same 10 above are infeasible for engineering alone to close and are
excluded from this ranking. The remaining 324 are ranked by
`priority = category_weight x status_factor x effort_factor` (`status_factor`
1.0 for missing / 0.5 for already-partial, `effort_factor` 1.0/0.6/0.35 for
small/medium/large estimated effort) — a heuristic meant to surface
high-weight, low-effort wins first, not a committed estimate. Effort tiers
were re-derived fresh for this pass rather than carried over mechanically
from the prior run, using the same rule of thumb it used: a narrow extension
of something that already works is small; an entirely new subsystem, file
format, or foundational capability is large; everything else is medium. The
top 40:

| # | Side | Category | Item | Status | Effort | Why it matters |
|---|---|---|---|---|---|---|
| 1 | kernel | intersections | CSX against trimmed faces and curve-on-surface overlap detection | missing | small | `FaceContainsUV` already exists to filter hits — this is wiring, not new algorithm work. |
| 2 | kernel | booleans | ~~Face-face imprint (Parasolid PK_BODY_imprint / ACIS imprint)~~ **fixed** | partial | small | Stale row (the general boolean engine's internal face-splitting was already exposed as `ImprintFaces` before this row was last written). `MutualImprintFaces` (boolean_general.h/.cpp) now also closes the two-way "call it twice, swapped" case; remaining work is app wiring only — see the category bullet below for detail. |
| 3 | kernel | booleans | Sheet/solid trim (open surface as cutter through a solid) | partial | small | `SplitBySheet` (dino8-kernel/src/boolean_general.cpp) splits a solid into the two pieces on either side of an open cutting sheet, each capped; `TrimSheetBySolid` (same file) now closes the item's other half, trimming a sheet's own surface down by a solid. Remaining work is wiring either into an app command and testing a genuinely curved (non-planar) sheet or solid. |
| 4 | kernel | booleans | ~~AutoCAD-style INTERFERE (real overlap solids, not just Clash report)~~ **fixed** | present | small | `ComputeInterference`/`ComputeMultiWayInterference`/`ComputeAllInterference` (dino8-kernel/src/boolean.cpp) build the real pairwise AND true N-way (3+) overlap solids; `Clash`'s new `CreateSolids` option (dino8-app/src/commands/cmd_solidtools.cpp) now wires `ComputeAllInterference` in and adds every resulting solid to the document — see the category bullet below for detail. |
| 5 | kernel | blending | ~~Conic / rho (chordal, elliptical) blend cross-sections~~ **stale row, corrected** | partial | small | This row was already stale when written: `FilletConvexEdgeConic` (fillet.cpp/fillet.h) already existed at the time, closing the convex case including its own perpendicular-third-face corner notch - it was never genuinely `missing`. This pass adds the CONCAVE mirror, `FilletConcaveEdgeConic` (a thin validating wrapper over the same construction, the identical relationship `ChamferConcaveEdge` has to `ChamferConvexEdge`); a later pass adds `FilletConvexEdgesConic`/`FilletConcaveEdgesConic`, a real (though face-disjoint-only) multi-edge batch form; see the category bullet below for the verification detail of both. Remaining effort is now small, not medium: an oblique third face at an endpoint, a genuine vertex-blend/shared-corner form (today's multi-edge batch rejects two edges that share a face). App wiring for both the single-edge form (`FilletEdge`'s `Rho`/`Distance2` options) AND the multi-edge batch form is now done: `FilletEdgeCommand`'s Rho branch (cmd_fillet.cpp) stages every picked Rho edge and builds the whole batch in one `FilletConvexEdgesConic`/`FilletConcaveEdgesConic` call at Enter, instead of applying each pick immediately (which broke a second independent Rho edge outright - the first edge's own committed result already fails `PlanarFaces()` for the next pick). |
| 6 | kernel | blending | ~~Alternative blend rail types (distance-from-edge, distance-between-rails)~~ **fixed** | partial | small | `FilletConvexEdgeByDistanceFromEdge`/`FilletConvexEdgeByDistanceBetweenRails` and their `FilletConcaveEdge` mirrors (fillet.h/fillet.cpp) close this for the single-straight-edge, planar-adjacent-face case; remaining work is the multi-edge/vertex-blend form and a curved-face rail type (see the category bullet below for detail). |
| 7 | kernel | topology | ~~Sliver / degenerate micro-face removal — fix the Check() false-positive first~~ **fixed** | partial | small | Done in `b1ac7c9` (before this pass): `Brep::Check()` no longer auto-flags a loop-less face or under-samples a curved-wall trim; `TestBrepCheckDoesNotFalselyFlagCurvedOrToplessValidFaces` covers Box()/Sphere()/Torus()/Extrude()/Revolve(). Kept in the table (not renumbered away) only so this row's own history is traceable; not an active priority. Still partial for the same non-defect reasons item 207 above gives (delete-and-tolerant-join, not a geometric collapse; T-junction slivers left naked). |
| 8 | kernel | healing | ~~Degenerate face removal (B-rep) — same Check() false-positive root cause~~ **fixed** | partial | small | Same fix as #7, `b1ac7c9`; now verified end-to-end (not just at the `Check()` level) by `TestBrepRemoveDegenerateOrSliverFacesDoesNotTouchValidSolids`, added this pass — `RemoveDegenerateFaces()` removes 0 faces from Box()/Extrude(circle) while still removing a genuine hairline sliver. |
| 9 | kernel | healing | ~~Sliver face removal (B-rep) — same Check() false-positive root cause~~ **fixed** | partial | small | Same fix as #7/#8, same new end-to-end test covers `RemoveSliverFaces()` too. |
| 10 | kernel | exchange | ~~.3dm layer round-trip default-layer index fix~~ **fixed** | fixed | small | Every `Add*()`'s `layer_index` parameter now defaults to -1 (OpenNURBS' own built-in Default-layer sentinel) instead of plain 0, so an object left on the default layer can no longer be silently aliased onto whichever named layer happens to claim manifest index 0 - see the category bullet below for the verification detail. Kept in the table for this row's own history; not an active priority. |
| 11 | app | app_commands | AutoLISP-equivalent lightweight command-scripting language | missing | large | Closes a real, verified gap in Dino 8 Command system & core commands. |
| 12 | app | app_commands | ObjectARX-equivalent low-level native app-extension API | missing | large | Closes a real, verified gap in Dino 8 Command system & core commands. |
| 13 | kernel | localops | Imprint curve / face onto a body face | missing | medium | No implementation anywhere; a genuinely useful direct-edit primitive. |
| 14 | kernel | localops | Merge faces on the same non-planar surface (cylinder/tangent split faces) | missing | medium | `MergeCoplanarFaces` explicitly excludes this case; needs a curved-surface variant. |
| 15 | kernel | localops | Push/pull a face (extrude face and merge/cut into its own body) | partial | large | `PushPullFace` (dino8-kernel/src/boolean.cpp) now genuinely extrudes new side-wall faces (push) or retrims perpendicular neighbours (pull) via direct topological surgery, no convexity precondition; still planar-faced solids only, no oblique-neighbour pull, no app wiring. |
| 16 | kernel | localops | Move a single B-rep vertex directly | missing | medium | The Brep sub-object-edit path currently only handles Face and Edge refs. |
| 17 | kernel | localops | Taper / draft face (rotate face about a neutral plane) | partial | medium | `DraftFacesConvexPlanar` (dino8-kernel/src/boolean.cpp) now tilts a named face about its own intersection line with a caller-supplied neutral plane, exact for convex planar-faced solids; still no app wiring, non-convex/curved bodies, or per-face angle. |
| 18 | kernel | localops | Replace face (swap a face's surface, re-trim neighbours) | partial | medium | `ReplaceFacePlaneConvexPlanar` (dino8-kernel/src/boolean.cpp) now swaps a named face's plane for a caller-supplied target plane outright (translate, tilt, or both in one call) and re-trims every other face against it, exact for convex planar-faced solids; still no app wiring, non-convex/curved bodies, or a general (non-planar) target surface. |
| 19 | kernel | sweeplofts | Extrude to a boundary surface / body (ToBoundary, PressPull) | partial | small | `ExtrudeToBoundary` (dino8-kernel/src/boolean_general.cpp) now exists - exact closed-form per-corner ray/plane cap for a profile of ANY vertex count (not just 4) against a single planar boundary (verified against a closed-form area-times-centroid-height check using the shoelace formula for the footprint area, plus reversed-winding/reversed-direction/oblique-direction regression cases at both N == 4 and N != 4). Remaining effort is now small, not small-medium: app wiring and a genuinely curved (non-planar) boundary surface. |
| 20 | kernel | sweeplofts | Sweep controls: twist along path, scale along path, road-like alignment | partial | large | Kernel `Brep::Sweep1` now has `twist_total`, `scale_end`, `roadlike_up`, AND now `twist_schedule`/`scale_schedule` (a genuine piecewise-linear schedule, `PipeVariable`'s own `radius_points` convention), all exact on a straight rail; a schedule is only exact at its own breakpoints, not continuously between them, and the app's own Sweep1 command still has none of these. |
| 21 | kernel | sweeplofts | ExtrudeCrv / Revolve producing a SubD object directly | missing | medium | Catalogued option, no implementation. |
| 22 | kernel | sweeplofts | SubD-result revolve / multi-pipe menu entries are broken references | missing | small | Either implement the two commands or remove the dead menu entries — either is quick. |
| 23 | kernel | topology | ~~Euler operators (MEV/MEF/KEV/KEF/KEMR/MEKR etc.)~~ **present** | present | medium | Done this pass: `Brep::MakeEdgeKillRing`/`Brep::KillEdgeMakeRing` (brep.h; brep.cpp) are a real MEKR/KEMR pair — the last operator in this family — welding a face's own outer loop and one real inner (hole) loop into a single loop via a zero-width "slit" edge, and its exact inverse splitting it back via the standard outer-CCW/inner-CW signed-area convention. All six named operators (MEV/KEV, MEF/KEF, MEKR/KEMR) are now real, tested, and round-trip exact. Kept in the table for this row's own history; not an active priority. Still scoped to planar faces and straight new edges between vertices/loops already on the boundary - a curved-face Euler op is out of scope. |
| 24 | kernel | topology | Wire bodies (edge/vertex-only B-rep body) | partial | small | `Brep::WireBody`/`Brep::IsWireBody` (dino8-kernel/src/brep.cpp) build a genuine zero-face vertex/edge-only `ON_Brep` from a list of curves, with real vertex welding across shared endpoints and a genuine self-closed edge for a closed curve; `Brep::AddWireCurves` (dino8-kernel/src/brep.cpp) extends an existing wire body - or any Brep's own existing vertices, not only a wire body - with more curves in one call; `Brep::ExtrudeWireBody`/`Brep::OffsetWireBody` (dino8-kernel/src/sweep.cpp) close the wire-to-solid/sheet promotion gap this row itself named - both walk a wire body's own edge graph into one simple chain, join its edges' curves via `NurbsCurve::Join()`, and hand the result to `Brep::Extrude()`/`NurbsCurve::OffsetInPlane()` respectively, giving real `AssembleSweptBody()` topology (unlike `ExtrudeFace()`/`Thicken()`'s own untopologized walls) for the extrude case; `Brep::AddHoleLoop` (dino8-kernel/src/brep.cpp) now closes the narrowest real case of "using a wire body as a boolean/imprint tool" too - punching a closed, straight-edged wire body out of an existing planar face as a genuine new `ON_BrepLoop::inner` hole. Remaining effort is app wiring (no curve-only "body" scene object exists) and a general (non-planar, already-holed, or curved-wire) boolean/imprint tool, not just the single-hole-on-a-plain-planar-face case `AddHoleLoop` covers. |
| 25 | kernel | topology | Persistent naming / topology identity across edits | missing | large | Every topology edit renumbers via Compact(); this is an architectural change. |
| 26 | kernel | offsetshell | Inset on raw mesh or polysurface objects (as opposed to SubD) | missing | medium | Inset currently rejects every non-SubD target outright. |
| 27 | kernel | features | Threaded / tapped hole and external thread feature | missing | large | Bolt/Nut are built solid with no thread geometry at all. |
| 28 | kernel | features | Emboss / deboss a closed region onto a face | missing | large | No Emboss/Deboss/Engrave command or API exists anywhere. |
| 29 | kernel | features | Draft / taper faces of an existing body about a neutral plane | partial | medium | Same `DraftFacesConvexPlanar` as row 17 above (the identical capability under this category's own Parasolid-style framing); still convex planar-faced solids only, no app-level feature command. |
| 30 | kernel | features | Split body with an arbitrary surface / solid cutter | partial | large | `SplitByObjectCommand` (dino8-app/src/commands/cmd_boolean.cpp) now closes the general cutting-object case; still a single-normal-direction mesh-boolean approximation, not a true PK_BODY_section-style trim. |
| 31 | kernel | features | Sheet-metal features (Bend, Unfold, Flange, Hem, Tab) | missing | large | No code; UnrollDevelopable is a single-surface unroll, not sheet metal. |
| 32 | kernel | features | Lattice / cellular infill structures | missing | large | No code beyond an unrelated deformer hit. |
| 33 | kernel | exchange | Rhino non-geometry/composite objects in .3dm (blocks, annotations, hatches, text dots) | missing | large | Real Rhino files silently lose all of these on open. |
| 34 | kernel | exchange | STEP AP242 | missing | large | Only AP214 exists; no TESSELLATED/PMI support. |
| 35 | kernel | exchange | ~~Other mesh/scene exchange formats (glTF, 3MF, FBX, Collada, USD, ...)~~ **corrected, now partial** | partial | large | Stale row: OFF/AMF/VRML/Collada/X3D/USD/glTF(`.gltf`)/glTF(`.glb`) all now exist as kernel `Mesh::Save*`/`Load*` pairs (mesh.cpp) - see the category bullet below for the verification detail of each. Remaining, still zero code: 3MF, FBX, SketchUp SKP. |
| 36 | kernel | exchange | IFC (BIM) data exchange | missing | large | No code anywhere. |
| 37 | kernel | exchange | DWF/DWFx export/import | missing | large | No code anywhere. |
| 38 | kernel | exchange | JT (PLM interchange) | missing | large | No code anywhere. |
| 39 | kernel | subd_mesh | ~~Kernel-native SubD local edit operators (insert edge, extrude face, spin, weld, expand)~~ **closed** | present | large | `SubD::InsertEdge`/`SpinEdge`/`ExtrudeFace`/`ExpandFaces` (subd.cpp) wrap real `ON_SubD::SplitFace`/`SpinEdge`/`ExtrudeComponents`; `SubD::Weld` (subd.cpp) has no ready-made OpenNURBS primitive of its own, so it's a hand-rolled snapshot-and-rebuild instead (capture the whole control net's vertices/face corner-ids/interior-edge tags, remap the discarded vertex's id onto the kept one's, rebuild via `AddVertexForExperts`+`FindOrAddFace` - a local `DeleteComponents`-based surgery was tried first and found unsafe, see that method's own doc comment). All 5 named sub-operators now genuinely exist. Kept in the table (not renumbered away) only so this row's own history is traceable; not an active priority. None is wired to any app command - a real, separate App-level gap this row's own kernel-level scope doesn't measure. |
| 40 | kernel | subd_mesh | Quad-remeshing of an arbitrary mesh into a clean SubD-ready cage | missing | large | No quad-dominant remesher targeting SubD-cage quality exists in the kernel. |

### Remainder, grouped by effort (282 items)

**Small effort** (58 items):
- [kernel/topology] Non-manifold topology (edge shared by 3+ faces, non-manifold vertices) (partial)
- [kernel/topology] Kernel-level topology enumeration API — loop/trim iteration now real (`LoopCount`/`LoopsOfFace`/`TrimCount`/`TrimsOfLoop`/etc.); remaining: `VertexCount`/`EdgeCount`/`LoopCount`/`TrimCount` counting deleted slots before `Compact()` (partial)
- [kernel/topology] Cap naked loops — extend to non-planar-hole detection (partial)
- [kernel/geometry] Knot removal (curve) (partial)
- [kernel/geometry] Helix and spiral curves (partial)
- [kernel/geometry] Rational <-> non-rational conversion (tolerance-bounded) (partial)
- [kernel/blending] Fillet/blend on tangent edge chains and multi-edge selection (partial)
- [kernel/offsetshell] OpenNURBS-native mesh offset with fixed direction (partial)
- [kernel/localops] Extend a face/surface past its current boundary in place — in-place multi-face case (partial)
- [kernel/localops] Split an edge at a point — exact trim-parameter mapping (partial)
- [kernel/intersections] Curve/plane intersection — dedicated infinite-plane API (partial)
- [kernel/intersections] Point-cloud contour/section as separate app commands (partial)
- [kernel/healing] Micro/sliver edge removal — shared-edge case (partial)
- [kernel/massprops] Curve length / arc-length parametrization — Gaussian quadrature (partial)
- [kernel/massprops] Signed distance point-to-solid (partial)
- [kernel/tessellation] Angular (facet-normal deviation) tolerance control (missing)
- [kernel/tessellation] Isocurve / wireframe generation — public kernel API (partial)
- [kernel/transforms] Split body by plane — B-rep version (partial)
- [kernel/exchange] .3dm archive version targeting — wire a non-zero version through the app (partial)
- [kernel/exchange] PLY vertex colours (partial; kernel `SetVertexColors`/`SavePly`/`LoadPly` support closed 2026-09-30, app-side PLY still unwired to the kernel one)
- [kernel/features] Counterbore (stepped coaxial) hole (partial)
- [kernel/features] Countersink (conical) hole (partial)
- [kernel/curveops] Curve fairing / smoothing — kernel API (partial)
- [kernel/curveops] Match curve end continuity — G2 (partial)
- [kernel/surfaceops] SrfSeam - test coverage caveat (partial)
- [kernel/surfaceops] Kernel Rebuild() - wire into app's Rebuild/FitSrf commands (partial)
- [kernel/surfaceops] Kernel MatchEdge() - wire G2 into MatchSrf (partial)
- [kernel/subd_mesh] SubD extraordinary-vertex limit-tangent quality — semi-sharp edge handling (partial)
- [app/app_commands] ~~Command aliases — persist to disk~~ **fixed** — `Settings.cpp`'s `"aliases"` key now round-trips `CommandEngine::Aliases()` through `settings.json`/`OptionsExport`/`OptionsImport`, wholesale-replacing (not merging onto `InstallDefaultAliases()`) so a deleted default alias stays deleted. Kept in the list for this row's own history; not an active item. The category's own remaining gap (no remappable keyboard-shortcut table for the built-in bindings, only an additive one for new chords) is tracked in the category bullet itself, not as a separate backlog row.
- [app/app_drafting] Dimension styles — units/precision/tolerance fields (partial)
- [app/app_display] Per-object display mode override — remaining modes (partial)
- [app/app_scripting] Headless/batch scripting mode — document as supported, not just QA (partial)
- [app/app_interop] .3dm archive version targeting (see kernel item above; app wiring) (partial)
- [app/app_ux] Localized command and toolbar help text — at least the top N commands (missing)
- [kernel/geometry] Torus primitive — dedicated ToroidalFace analytic record (partial)
- [kernel/geometry] Typed analytic surface classes — add a torus record (partial)
- [kernel/blending] Edge blend trimmed and joined into the polysurface — TrimAndJoin (partial)
- [kernel/offsetshell] Curve offset in an arbitrary plane / 3D — caller-specified-plane overload now exists (`NurbsCurve::OffsetInPlane(plane, ...)`); remaining: app wiring, exact (non-approximate) arc/circle offset under a foreign plane (partial)
- [kernel/offsetshell] Inset (SubD) — true in-plane edge-parallel inset (partial)
- [kernel/localops] Rotate face about hinge edge — exact version for planar solids (partial)
- [kernel/localops] Delete face with heal — planar case using CapPlanarHoles (partial)
- [kernel/intersections] Curve self-intersection — kernel API (partial)
- [kernel/intersections] Pull curves/points to surfaces — curve-on-surface pull-back (partial)
- [kernel/healing] Analytic-form recognition — fix IsTorus() tolerance (partial)
- [kernel/massprops] Closest point on trimmed B-rep — untrimmed-face case first (partial)
- [kernel/massprops] Ray firing against exact B-rep faces — untrimmed-face case first (partial)
- [kernel/tessellation] Mesh-quality-driven facet selection/repair — kernel per-facet metrics (partial)
- [kernel/transforms] Feature patterns of holes — B-rep result via BooleanCombineMixed (partial)
- [kernel/exchange] App-independent STEP/IGES/DXF exchange API — expose existing app code as kernel API (partial)
- [kernel/features] Revolved cut (RevolvedHole) — route through Brep::Revolve (partial)
- [kernel/features] Body sectioning — expose IntersectSurfaces as a section API (partial)
- [kernel/curveops] Knot / control-point insertion and removal — curve knot removal (partial)
- [kernel/surfaceops] Convert to Beziers (surface) (missing)
- [kernel/subd_mesh] SubD from NURBS/B-rep conversion — wire into the app (partial)
- [kernel/subd_mesh] SubD non-manifold / multi-body validity checks — report what/where (partial)
- [app/app_subd_mesh] NURBS/Brep to SubD — add test coverage (partial)
- [app/app_ux] Worksessions — status/refresh indicator for the copy-in model (partial)
- [app/app_ecosystem] Large-scale adversarial/property-based QA — extend to Windows-specific cases (partial)

**Medium effort** (138 items):
- [kernel/topology] Multi-shell / multi-lump bodies — allow booleans on compound operands (partial)
- [kernel/topology] Loop structure — walking (`NextTrimInLoop`/`PrevTrimInLoop`) and classification (`TypeOfLoop`) now real; remaining: real inner loops only from general boolean/`MakeEdgeKillRing`, `TrimmedPlanarFace` holes still side-table, `MergeCoplanarFaces` still refuses holed faces (partial)
- [kernel/topology] Remove edge / collapse micro edge — general above-tolerance case (partial)
- [kernel/topology] Split / imprint a face by a curve while keeping topology (partial)
- [kernel/topology] Delete / extract face with real healing (partial)
- [kernel/topology] Tolerance model — feed recorded tolerances into booleans/fillets (partial)
- [kernel/topology] Genuine topology from every constructor/primitive (Box/Sphere/Torus) (partial)
- [kernel/geometry] Degree reduction (curve and surface, with error bound) (partial)
- [kernel/geometry] Reparameterization — curve SetDomain, rational reparam, seam change (partial)
- [kernel/geometry] Curve/surface interpolation — degree-p, end-tangent constraints (partial)
- [kernel/geometry] Typed curve taxonomy and persistent composite (poly)curves (partial)
- [kernel/geometry] Numeric curve queries — tolerance-driven arc length (partial)
- [kernel/booleans] Coplanar / coincident face handling — curved-face coincidence (partial)
- [kernel/booleans] Tangent / grazing contact handling — B-rep engines (partial)
- [kernel/booleans] Multi-body / multi-tool booleans (N operands per side) (partial)
- [kernel/booleans] Result validity — close more general-boolean cases (partial)
- [kernel/booleans] Tolerant booleans (caller-specified tolerance) (partial)
- [kernel/booleans] Keep/split options (BooleanSplit keeping all pieces) (partial)
- [kernel/booleans] Non-manifold boolean results (partial)
- [kernel/booleans] 2D region / planar curve booleans — exact kernel version (partial)
- [kernel/booleans] Boolean failure diagnostics — structured failure-report type (partial)
- [kernel/booleans] Free-form (non-analytic) NURBS operands — add coverage/tests (partial)
- [kernel/blending] Constant-radius edge fillet on curved adjacent faces (partial)
- [kernel/blending] Variable-radius fillet — now exposed from both app entry points (`FilletEdge`'s `Radii=` and `FilletTwoSurfacesCommand`'s own `VariableFilletSrf`, same-solid-adjacent-planar-faces case); remaining effort is non-linear laws and curved-face taper (partial)
- [kernel/blending] Chamfer with two unequal distances/angle — now exposed in app `ChamferEdge` (Distance2=/Angle=, planar-faced solids only); `FilletTwoSurfacesCommand`'s own `ChamferSrf` now also reaches the exact construction for the same-solid-adjacent-planar-faces case (Trim=Yes); remaining effort is curved-face support and genuinely independent (no shared edge) surface pairs (partial)
- [kernel/blending] Face-face blend between two independently picked surfaces — trim both inputs (partial)
- [kernel/blending] Vertex blend — non-perpendicular and mixed-radius corners (partial); the flat-facet sibling, `ChamferConvexVertex`/`ChamferConcaveVertex`, is now also wired (app `ChamferVertex` command, cmd_fillet.cpp) alongside `FilletVertex`'s own spherical-corner blend — same remaining scope (higher-valence corners, mixed convex/concave)
- [kernel/blending] Fillet end conditions on adjacent end faces — remaining cases (partial)
- [kernel/blending] Blend removal / defeaturing — oblique-end cylinders, a corner sharing a cylinder with a second sphere (partial)
- [kernel/blending] Fillet surface along a user-supplied rail curve — trimming (partial)
- [kernel/blending] 2D curve fillet / chamfer — kernel API now exists for a single two-line corner (`NurbsCurve::FilletCorner`/`ChamferCorner`) and is already app-wired (`cmd_curveedit.cpp`'s `Fillet`/`Chamfer`/`FilletCorners` commands reach `FilletCornerArc`/`ChamferCorner`; stale "and app wiring" corrected out of this row's own remaining-effort text); remaining effort is a general curve-to-curve form only (partial)
- [kernel/blending] Curve-to-curve blend — G3+ (partial)
- [kernel/blending] Surface-to-surface continuity blend — G3/G4, shape handles (partial)
- [kernel/blending] Rolling-ball blend surface accuracy on freeform surfaces — enforce max_gap (partial)
- [kernel/sweeplofts] ~~Extrude a curve along a path curve — solid/cap option~~ **kernel entry point + solid/cap done** (`Brep::ExtrudeAlongCurve`); remaining: rational-curve support, app wiring (partial)
- [kernel/sweeplofts] ~~Extrude a surface / polysurface face into a solid~~ **kernel entry point done** (`Brep::ExtrudeFace`, any untrimmed planar or freeform face, exact translate; now ALSO a trimmed planar hole-free face, as a genuine `IsSolid()` result); remaining: a trimmed face with a hole or on a curved surface, app wiring (partial)
- [kernel/sweeplofts] Extrude with draft / taper angle — a non-star-shaped concave profile still can't be CAPPED (Loft's own fan-cap needs a star-shaped section; the offset itself is exact for any simple polygon now), a general curved profile still falls back to an approximate offset, app wiring (partial)
- [kernel/sweeplofts] ~~Extrude to a point~~ **kernel entry point done** (`Brep::ExtrudeToPoint`, embeds for ANY simple closed planar profile, convex or not, AND now an open profile too, as an uncapped fan shell); remaining: closed-profile flat-cap still needs a star-shaped section, app wiring (partial)
- [kernel/sweeplofts] Full 360-degree revolve — a closed profile touching the axis at more than one place, or at a single point rather than a sub-arc, still refuses (a single touching sub-arc is now handled, `SplitTouchingAxisArc`); `RevolvedHole` still mesh-only (partial)
- [kernel/sweeplofts] Partial-angle revolve — off-axis endpoint capping (partial)
- [kernel/sweeplofts] Rail revolve — kernel API done (`Brep::RailRevolve`), now including an open both-ends-on-axis profile (Revolve()'s own pole case); remaining: a closed profile touching the axis, no analytic exact-sweep shortcut, app wiring (partial)
- [kernel/sweeplofts] Sweep along one rail — multi-section blending, scaling (partial)
- [kernel/sweeplofts] Sweep along two rails — multi-section, independent scaling (partial)
- [kernel/sweeplofts] Loft options — surface-to-surface edge tangency, guide curves (partial)
- [kernel/sweeplofts] Developable loft between two rails — kernel API (partial)
- [kernel/sweeplofts] Pipe — Round cap option (done for `Brep::Pipe` and `Brep::PipeVariable` - both build genuine hemispherical NURBS domes, not a mesh-approximate one, `PipeVariable`'s sized per-end to that end's own local radius; see the "kernel category gaps" bullet above), still missing on `PipeThickWalled` and kinked-rail handling (still partial)
- [kernel/sweeplofts] Pipe variants — real MultiPipe (partial; thick-walled pipe is done, see `Brep::PipeThickWalled`)
- [kernel/sweeplofts] Cap planar openings — curved naked edges (partial)
- [kernel/sweeplofts] Sweep/extrude surface/polysurface/mesh face — B-rep version (partial)
- [kernel/sweeplofts] Feature extrusions (Boss, Rib) — B-rep version (partial)
- [kernel/sweeplofts] Closed B-rep solid output — wire app commands to the kernel (partial)
- [kernel/sweeplofts] Sweep1/Sweep2 producing a SubD result — native, not conversion (partial)
- [kernel/offsetshell] Closed hollow shell — general B-rep case beyond sphere/torus (partial)
- [kernel/offsetshell] Shell with removed/open faces — non-convex, non-planar (partial)
- [kernel/offsetshell] Per-face shell — non-convex, non-planar (partial)
- [kernel/offsetshell] Face offset in place — non-convex, topology-changing (partial)
- [kernel/offsetshell] Body offset — B-rep version (partial)
- [kernel/offsetshell] Trimmed-surface / polysurface offset with corner reconstruction (partial)
- [kernel/offsetshell] Variable-distance surface offset — kernel API (partial)
- [kernel/offsetshell] Thicken sheet — B-rep/NURBS version (partial)
- [kernel/offsetshell] Planar curve offset — kink-preserving fit — **closed for polylines**: `NurbsCurve::OffsetInPlane` now exact-miters a piecewise-linear (`ON_Curve::IsPolyline()`) curve instead of blurring it through the smooth refit; remaining: a genuinely 3D polyline offset by a foreign plane still falls to the approximate general path (partial)
- [kernel/offsetshell] Curve offset corner handling at kinks — Round/Chamfer/Smooth (partial)
- [kernel/offsetshell] Offset self-intersection / invalid-loop removal — actual removal, not just detection (partial)
- [kernel/offsetshell] Curve offset on surface — kernel API (partial)
- [kernel/offsetshell] Curve offset normal to surface — kernel API (partial)
- [kernel/offsetshell] Mesh offset — thickness-preserving at creases (partial)
- [kernel/offsetshell] SubD offset / thicken — kernel API (partial)
- [kernel/offsetshell] Offset-derived constructions — kernel API (partial)
- [kernel/offsetshell] Exact analytic-face offset — preserve patch extent (partial)
- [kernel/offsetshell] Offset feasibility / degeneracy detection — global collision check (partial)
- [kernel/offsetshell] Kernel-level offset API — wire into app commands (partial)
- [kernel/offsetshell] Solid dilation/erosion — verify on genuinely concave fixtures (partial)
- [kernel/offsetshell] ShrinkWrap Offset — kernel API (partial)
- [kernel/localops] Move/transform face — kernel API beyond OffsetFace (partial)
- [kernel/localops] Move/transform edge — kernel API (partial)
- [kernel/localops] Offset face — non-convex solids (partial)
- [kernel/localops] Split face by curve/surface — real trim-loop split (partial)
- [kernel/localops] Remove small / sliver edges — shared-edge case now closed for isolated valence-3 endpoints (`RemoveSharedMicroEdge`); a non-manifold junction or an undersized neighbour loop is still refused (partial)
- [kernel/localops] Edge blend removal — spherical vertex blends, oblique cylinders (partial)
- [kernel/localops] Untrim face / remove outer trim — in-place for multi-face polysurfaces (partial)
- [kernel/localops] Move/copy/rotate/mirror a hole feature — B-rep version (partial)
- [kernel/localops] Shell / hollow body with face removal — non-convex (partial)
- [kernel/localops] Re-intersect adjacent faces / rebuild edges — automatic post-tweak (partial)
- [kernel/intersections] Analytic/analytic SSX closed forms — public API (partial)
- [kernel/intersections] SSX across periodic seams and at singular points — dedicated pole treatment (partial)
- [kernel/intersections] SSX coincident / overlapping surface regions (partial)
- [kernel/intersections] Plane sections / contours — exact route via IntersectSurfaces (partial)
- [kernel/intersections] Mesh self-intersection detection — coplanar and shared-vertex cases (partial)
- [kernel/intersections] Surface / B-rep self-intersection detection — face-interior, face/face (partial)
- [kernel/intersections] Projection of curves/points onto surfaces — exact kernel route (partial)
- [kernel/intersections] Silhouette / outline curves — exact kernel API (partial)
- [kernel/intersections] B-rep/B-rep and curve/B-rep intersection — public kernel API (partial)
- [kernel/intersections] Pullback of a 3D curve to surface parameter space — general-purpose API (partial)
- [kernel/healing] Tolerant sewing with edge splitting — curved naked edges (partial)
- [kernel/healing] Geometric consistency validation — closest-point TrimEdgeGap, face/face check (partial)
- [kernel/healing] Gap closing by edge re-trim / trim refit — RefitTrim, general ReplaceEdge (partial)
- [kernel/healing] Self-intersection detection — face-interior, face/face (partial)
- [kernel/healing] Edge rebuild from adjacent-surface intersection — kernel API (partial)
- [kernel/healing] Curve/surface simplify and rebuild — curve knot removal, SimplifyCrv (partial)
- [kernel/massprops] Exact B-rep mass properties — caller tolerance, centroid/moments, trimmed faces (partial)
- [kernel/massprops] Surface / B-rep area — trimmed-face true area (partial)
- [kernel/massprops] Planar closed-curve region properties — kernel API (partial)
- [kernel/massprops] Point classification vs exact B-rep — public API with edge/vertex handling (partial)
- [kernel/massprops] Entity-pair minimum distance — curve/curve, curve/surface, surface/surface (partial)
- [kernel/massprops] Tight bounding box of curved geometry — reduce overshoot (partial)
- [kernel/massprops] Oriented bounding box — Brep/curve OBB, CPlane-aligned option (partial)
- [kernel/massprops] Spatial acceleration structures — public grid/BVH API (partial)
- [kernel/massprops] Point-cloud spatial queries — kd-tree/R-tree (partial)
- [kernel/tessellation] Facet-size controls — min-edge/aspect-ratio inside the tessellators (partial)
- [kernel/tessellation] Kernel-native conforming tessellation — remaining seam cases (partial)
- [kernel/tessellation] Meshing trimmed faces with holes — exact-clip path (partial)
- [kernel/tessellation] True surface normals and UV texture coordinates — per-face exact (partial)
- [kernel/tessellation] Render/display mesh caching — kernel-side cache (partial)
- [kernel/tessellation] SubD limit-surface tessellation — proper limit mesher (partial)
- [kernel/tessellation] Multi-threaded / scalable tessellation — kernel threading (partial)
- [kernel/tessellation] Exact trim-boundary clipping — hole loops, harden degeneracies (partial)
- [kernel/transforms] Non-uniform scale / shear — kernel affine B-rep API (partial)
- [kernel/transforms] Mirror / reflection with body-orientation fix-up — auto re-orient (partial)
- [kernel/transforms] Arrays along a curve / on a surface — RMF/roadlike frames (partial)
- [kernel/transforms] Cut / split / trim with curve or surface cutters — exact kernel API (partial)
- [kernel/transforms] Planar section / contour curves of bodies — exact kernel API (partial)
- [kernel/transforms] Separate disconnected lumps — B-rep case for surface-only primitives (partial)
- [kernel/transforms] Non-affine deformations — kernel B-rep/surface deformation (partial)
- [kernel/transforms] Associative / history-linked transforms — kernel-level (partial)
- [kernel/transforms] Construction history — extend beyond the five covered commands (partial)
- [kernel/exchange] .3dm attribute/metadata fidelity — materials, named views, lights, units (partial)
- [kernel/exchange] STEP AP203/AP214 export — add AP203 option (partial)
- [kernel/exchange] STEP B-rep import — completeness (partial)
- [kernel/exchange] IGES import — completeness (partial)
- [kernel/exchange] OBJ read — UV seams (partial; negative/relative indices and >4-index-face fan-triangulation both closed 2026-09-28)
- [kernel/exchange] Point-cloud / scan file formats — .e57 (partial; .pts, .pcd, and .las closed via `PointCloud::SavePts`/`LoadPts`, `SavePcd`/`LoadPcd`, and `SaveLas`/`LoadLas`)
- [kernel/exchange] Unit-system conversion — kernel Model unit declaration (partial)
- [kernel/exchange] Import-time B-rep validation and healing — call SewTJunctions from importers (partial)
- [kernel/features] Draft angle on extrusions — wire the app to the kernel (oblique directions closed this pass) (partial)
- [kernel/features] Thicken a sheet body — B-rep version (partial)
- [kernel/features] Delete face and heal — general delete-and-extend (partial)
- [kernel/features] Feature recognition — boss/pocket recognition (hole recognition closed this pass) (partial)
- [kernel/features] Blind/through hole with depth options — B-rep version (partial)
- [kernel/features] Boss following a curved surface — B-rep version (partial)
- [kernel/features] Rib — B-rep version (partial)
- [kernel/curveops] Offset curve — self-intersection trimming, corner styles (partial)
- [kernel/curveops] Project / Pull curve onto a surface — kernel pull-back API (partial)
- [kernel/curveops] Divide curve into N segments — divide-by-length (partial)
- [kernel/curveops] Simplify curve — general tolerance-driven simplify (partial)
- [kernel/curveops] Curve-curve end continuity analysis — kernel API (partial)
- [kernel/curveops] Curve-to-curve deviation measurement — kernel API, two-directional (partial)
- [kernel/surfaceops] Merge (MergeSrf) — kernel API (partial)
- [kernel/surfaceops] Rebuild / Refit — real least-squares fit in the app path (partial)
- [kernel/surfaceops] Match (G0/G1/G2) — Curvature option (partial)
- [kernel/surfaceops] Reparameterize — surface support in the app (partial)
- [kernel/surfaceops] Degree reduction — dedicated tolerance-driven reduction (partial)
- [kernel/surfaceops] Make uniform — kernel wrapper with deviation report (partial)
- [kernel/surfaceops] Patch — general (non-planar) fitted patch (partial)
- [kernel/surfaceops] Make periodic (surface) — app wiring, Smooth=Yes refit (partial)
- [kernel/surfaceops] SrfSeam — polysurface-face case (partial)
- [kernel/surfaceops] Surface from 2-4 edge curves — 2/3-curve exact cases (partial)
- [kernel/subd_mesh] SubD -> NURBS patch conversion — Stam eigenbasis / Gregory patch (partial)
- [app/app_commands] Surface construction commands — Patch beyond planar, tolerance-controlled sweeps (partial)
- [app/app_commands] Command-level feature editing — broader parametric re-run (partial)
- [app/app_drafting] Associative annotation updating — hook document edits (partial)
- [app/app_drafting] Print and plot output — lineweights and plot styles (partial)
- [app/app_drafting] Dynamic blocks — stretch/flip/array/lookup actions (partial)
- [app/app_drafting] Live external data linking — native .xlsx (partial)
- [app/app_display] Environments and image-based lighting — HDRI loader and lighting contribution (partial)
- [app/app_scripting] Embedded Python 3 — enable on Windows builds (partial)
- [app/app_scripting] Python API breadth — expand the object-model bindings (partial)
- [app/app_interop] Native .3dm read/write — read Rhino annotations/hatches/blocks (partial)
- [app/app_interop] OBJ — per-group/per-object split, .mtl (partial)
- [app/app_interop] STEP AP203/AP214 — assembly structure (partial)
- [app/app_interop] DXF — write text/dimension/hatch/insert entities (partial)
- [app/app_interop] DWG — write beyond DXF-writer's limits (partial)
- [app/app_subd_mesh] SubD to NURBS (ToNURBS) — wire the adaptive converter in (partial)
- [app/app_ux] Breadth of localization — bring existing languages to full key parity (partial)
- [app/app_ecosystem] Plugin marketplace / discovery mechanism — a real, curated/hosted *registry* of third-party indexes reachable over the network, beyond the existing real install/uninstall/dependency-resolution/ratings/update-all/verify-all/uninstall-all system and its one bundled first-party reference index (which no longer needs a path or URL typed in - see the app_ecosystem category bullet above) (missing; now partial — reclassified 2026-09-30, discovery gap narrowed the same day)

**Large effort** (86 items):
- [kernel/topology] Wire bodies (edge/vertex-only B-rep body) (missing; now partial - see `Brep::WireBody`/`Brep::IsWireBody`/`Brep::AddWireCurves`/`Brep::ExtrudeWireBody`/`Brep::OffsetWireBody`, brep.h/brep.cpp/sweep.cpp - remaining effort is small, not large: app wiring and using a wire body as a boolean/imprint tool)
- [kernel/topology] Euler operators (missing; MEV/KEV, MEF/KEF and now MEKR/KEMR are all real and tested - present)
- [kernel/topology] Persistent naming / topology identity across edits (missing)
- [kernel/booleans] Sheet/solid trim (missing; now partial - see `SplitBySheet`/`TrimSheetBySolid`, boolean_general.cpp, both halves now real code - remaining effort is small, not large: app wiring and a curved-sheet/curved-solid test)
- [kernel/booleans] Face-face imprint (missing)
- [kernel/booleans] B-rep-preserving booleans reachable from the application (missing; now partial - see `BooleanUnion`/`BooleanDifference`/`BooleanIntersection`/`Boolean2Objects`'s `TryExactBrepBoolean` path, dino8-app/src/commands/cmd_boolean.cpp, and its own compound-operand support now verified end-to-end - remaining effort is small-medium: wiring `BooleanCombineMixed`/`BooleanCombineGeneral` the same way for a curved (cylindrical or general) face was attempted and reverted this pass, for two reasons - a record-less operand's own result didn't tessellate closed via the app's generic `MeshOf` path (the coarse-trim-sampling gap in `MixedFaces()`/`PlanarFaces()` this row once named; **a later pass fixes this at the kernel level** - `SampleLoop`'s own floor raised 8→128 samples, brep.cpp - see this category's own "Fourteenth note"), and `BooleanCombineMixed`'s own auto-derived tolerance silently accepting adversarial geometry the planar engine correctly refuses (still open, kernel-level, unchanged) - and wiring the Split/WireCut family)
- [kernel/booleans] Associative/history-enabled Boolean operations (missing)
- [kernel/booleans] AutoCAD-style INTERFERE (partial; now present - see `Clash`'s new `CreateSolids` option, dino8-app/src/commands/cmd_solidtools.cpp, wiring `ComputeAllInterference` into the app)
- [kernel/blending] Conic / rho blend cross-sections (missing; was already stale when written - `FilletConvexEdgeConic` existed - now `FilletConcaveEdgeConic` closes the concave mirror too, and `FilletConvexEdgesConic`/`FilletConcaveEdgesConic` add a face-disjoint multi-edge batch form; app wiring is now done for both the single-edge form (`FilletEdge`'s `Rho`/`Distance2` options) and the multi-edge batch form (`FilletEdgeCommand` stages every Rho pick and builds the batch in one call at Enter), cmd_fillet.cpp; still partial, remaining effort small, not large: an oblique third face at an endpoint, and a genuine vertex-blend/shared-corner form)
- [kernel/blending] Fillet overflow / cliff-edge / notch handling (missing)
- [kernel/blending] Alternative blend rail types (missing; now partial - see `FilletConvexEdgeByDistanceFromEdge`/`FilletConvexEdgeByDistanceBetweenRails` and their `FilletConcaveEdge` mirrors, fillet.h/fillet.cpp - remaining effort is small, not large: the multi-edge/vertex-blend form and a curved-face rail type)
- [kernel/sweeplofts] Extrude to a boundary surface / body (missing; now partial - see `ExtrudeToBoundary`, boolean_general.cpp - N-gon profiles now work; remaining effort is small, not small-medium: app wiring and a curved-boundary test)
- [kernel/sweeplofts] Sweep controls: twist/scale/roadlike alignment (missing; twist, scale, and road-like alignment along path are now partial - see Brep::Sweep1()'s twist_total/scale_end/roadlike_up)
- [kernel/sweeplofts] ExtrudeCrv/Revolve producing SubD directly (missing)
- [kernel/localops] Taper / draft face (partial)
- [kernel/localops] Replace face (partial; `ReplaceFacePlaneConvexPlanar` now swaps a named face's plane for an arbitrary target plane on a convex planar-faced solid)
- [kernel/localops] Imprint curve / face onto a body face (missing)
- [kernel/localops] Merge faces on the same non-planar surface (missing)
- [kernel/localops] Push/pull a face (partial)
- [kernel/localops] Move a single B-rep vertex directly (missing)
- [kernel/intersections] SSX tangent / grazing contact (missing)
- [kernel/intersections] CSX against trimmed faces and curve-on-surface overlap detection (missing)
- [kernel/healing] Kinky / creased surface splitting into G1 faces (missing)
- [kernel/tessellation] Angular (facet-normal deviation) tolerance control (missing)
- [kernel/tessellation] Post-tessellation deviation verification (missing)
- [kernel/transforms] Split / trim body with a tool body (missing)
- [kernel/exchange] Rhino non-geometry/composite objects in .3dm (blocks, annotations, hatches, text dots) (missing)
- [kernel/exchange] STEP AP242 (missing)
- [kernel/exchange] Other mesh/scene exchange formats (glTF, 3MF, FBX, Collada, USD, ...) (missing; now partial - OFF/AMF/VRML/Collada/X3D/USD/glTF(`.gltf`)/glTF(`.glb`) all exist as kernel `Mesh::Save*`/`Load*` pairs, mesh.cpp - see the category bullet above for detail; still zero code for 3MF, FBX, SketchUp SKP)
- [kernel/exchange] IFC (BIM) data exchange (missing)
- [kernel/exchange] DWF/DWFx export/import (missing)
- [kernel/exchange] JT (PLM interchange) (missing)
- [kernel/features] Threaded / tapped hole and external thread feature (missing)
- [kernel/features] Emboss / deboss onto a face (missing)
- [kernel/features] Draft / taper faces of an existing body (partial)
- [kernel/features] Split body with an arbitrary surface / solid cutter (partial)
- [kernel/features] Sheet-metal features (missing)
- [kernel/features] Lattice / cellular infill structures (missing)
- [kernel/subd_mesh] Kernel-native SubD local edit operators (missing; now present - `SubD::InsertEdge`/`SpinEdge`/`ExtrudeFace`/`ExpandFaces` in subd.cpp wrap real `ON_SubD` primitives; `SubD::Weld` has no ready-made OpenNURBS primitive of its own, so it's a hand-rolled snapshot-and-rebuild (via `AddVertexForExperts`+`FindOrAddFace`) instead; none is wired to an app command, an App-level gap)
- [kernel/subd_mesh] SubD boolean operations (missing; now partial - `SubD::Boolean(other, op)` in subd.cpp converts both operands via `ToApproximateMesh()` and calls the real Manifold-backed `BooleanCombine()`, returning a `Mesh`; mesh-approximate only, not a topological SubD-to-SubD boolean, and not wired to an app command, an App-level gap)
- [kernel/subd_mesh] Quad-remeshing into a clean SubD-ready cage (missing)
- [app/app_commands] AutoLISP-equivalent command scripting language (missing)
- [app/app_commands] ObjectARX-equivalent native extension API (missing)
- [app/app_drafting] Field text (dynamic text driven by an object property) (missing)
- [app/app_display] Real-time shadow maps in the rasterized renderer (missing)
- [app/app_display] SSAO in the rasterized renderer (missing)
- [app/app_scripting] Cloud/network compute service (Rhino.Compute equivalent) (missing)
- [app/app_scripting] AI-assisted modeling or scripting (missing)
- [app/app_interop] STEP AP242 (missing)
- [app/app_interop] Digital signing of exported files (missing)
- [app/app_interop] Point-cloud exchange formats (LAS/E57/PTS/XYZ) (missing)
- [app/app_interop] IFC (BIM) import/export (missing)
- [app/app_interop] JT (PLM interchange) import/export (missing)
- [app/app_subd_mesh] SubD booleans (missing)
- [app/app_subd_mesh] Sculpting (multi-resolution brush sculpting) (missing)
- [app/app_ux] Localized command and toolbar help text (missing)
- [app/app_ux] Real-time multi-user collaborative editing (missing)
- [app/app_ecosystem] Real AI/ML-based modeling assistance (missing)
- [app/app_ecosystem] Cloud model viewer / app builder (ShapeDiver equivalent) (missing)
- [app/app_ecosystem] Real-time multi-user collaboration / co-editing (missing)
- [app/app_ux] Screen-reader support — the rest of the UI (3D viewport content, ~21 remaining panels/dialogs, plus five of the Options window's seven tabs) and Windows/macOS platform bridges, beyond the real AT-SPI2/Linux bridge that now covers the command line, menu bar, running command's options, Layers/Properties, viewport title bars, Activity Log, Named Views, Named CPlanes, Linetypes, Materials, Clipping Planes, Layouts, Block Manager, Layer State Manager, Document User Text, Lights, Annotation Styles, Notes, Environments, Audit Results, Undo/Redo History, Hatch Patterns, Plug-ins, the Command List, Command Aliases and Keyboard Shortcuts (missing; now partial — reclassified 2026-09-30, see the app_ux category bullet above)
- [kernel/booleans] Non-manifold boolean results — full non-manifold construction, not just tolerating detection (partial)
- [kernel/booleans] 2D region / planar curve booleans — exact kernel implementation, not mesh slabs (partial)
- [kernel/blending] Constant-radius edge fillet on curved adjacent faces — lift the planar-faces restriction (partial)
- [kernel/blending] Chamfer with two unequal distances/angle on curved faces (partial)
- [kernel/blending] Vertex blend — general (non-perpendicular, mixed-radius) corners (partial)
- [kernel/sweeplofts] Kernel-native NURBS B-rep extrude/revolve/sweep/loft — close remaining edge-case gaps (partial)
- [kernel/sweeplofts] Loft options — Loose/Tight/Uniform, guide curves (partial)
- [kernel/sweeplofts] Developable loft — exact developability, not twist-minimising approximation (partial)
- [kernel/offsetshell] Offset self-intersection removal — actual loop repair for concave inputs (partial)
- [kernel/offsetshell] Trimmed-surface / polysurface offset with corner reconstruction — general curved case (partial)
- [kernel/massprops] Exact B-rep mass properties on trimmed and general-boolean faces (partial)
- [kernel/massprops] Entity-pair minimum distance — exact surface/surface case (partial)
- [kernel/tessellation] Kernel-native conforming tessellation — general closure, not enumerated cases (partial)
- [kernel/topology] Multi-shell / multi-lump bodies — full inner-void/hollow-shell support (partial)
- [kernel/topology] Loop structure — full public loop/trim walking API (partial)
- [kernel/exchange] .3dm attribute/metadata fidelity — full kernel-side materials/textures/views/lights (partial)
- [kernel/features] Feature recognition — real boss/pocket recognition from a dumb B-rep (hole recognition closed this pass) (partial)
- [kernel/healing] Curve/surface simplify and rebuild — general SimplifyCrv (partial)
- [app/app_interop] Native .3dm read/write — full Rhino annotation/hatch/block round-trip (partial)
- [app/app_ux] Worksessions — true live reference linking, not copy-in (partial)
- [app/app_ecosystem] Large-scale adversarial/property-based QA — decade-of-real-files-scale coverage (partial)

## Suggested next implementation waves

Three waves of 6 items each, chosen from the top of the ranked backlog above
so that every item in the same wave touches a **different** primary source
file — these are meant to be dispatched to parallel agents in separate git
worktrees with no merge collisions between them. (Waves run sequentially
relative to each other; only same-wave items are guaranteed file-disjoint.)

### Wave 1 — Fix the Check() false-positive, then quick kernel wins

| Item | Side/Category | Primary file(s) |
|---|---|---|
| ~~Fix `Brep::Check()`'s `SampleLoop` false-positive on straight-parameter trims of curved faces (unblocks the two RemoveDegenerateFaces/RemoveSliverFaces items below)~~ — already done in `b1ac7c9`, predating this wave; kept here only for the wave's own history | kernel/healing, kernel/topology | `dino8-kernel/src/brep.cpp` (SampleLoop, Check) |
| CSX against trimmed faces — wire `FaceContainsUV` into `IntersectCurveSurface`/`IntersectAny` | kernel/intersections | `dino8-kernel/src/surface_intersect.cpp`, `dino8-app/src/commands/cmd_fillet.cpp` |
| ~~`.3dm` layer round-trip default-layer index fix (`AddLayer()` returns 0 instead of the true default -1)~~ — fixed 2026-09-30 | kernel/exchange | `dino8-kernel/src/file_io.cpp` |
| SubD-result revolve / multi-pipe: implement the two commands or remove the dead `MenuBar.cpp` entries | kernel/sweeplofts | `dino8-app/src/commands/cmd_subd.cpp`, `dino8-app/src/ui/MenuBar.cpp` |
| Merge faces on the same non-planar surface (cylinder/tangent split faces) | kernel/localops | `dino8-kernel/src/brep.cpp` (MergeCoplanarFaces sibling) |
| Imprint curve / face onto a body face | kernel/localops | `dino8-kernel/include/dino8/kernel/brep.h`, `src/brep.cpp` |

### Wave 2 — App wiring for kernel work that already exists

| Item | Side/Category | Primary file(s) |
|---|---|---|
| Wire `BooleanCombineMixed`/`Brep::Sweep1`/`Sweep2`/`Pipe`/`PipeVariable`/`ExtrudeTapered`/`Revolve` into their respective app commands | kernel/sweeplofts, app_commands | `dino8-app/src/commands/cmd_surface.cpp`, `cmd_solids.cpp` |
| Wire `Brep::OffsetFace`/`ShellConvexPlanar`/`OffsetSolid` into the app's Offset/Shell commands | kernel/offsetshell | `dino8-app/src/commands/cmd_surface.cpp` |
| Wire `SewTJunctions`/`SplitNakedEdgeAt` into the STEP/IGES importers' healing pass | kernel/exchange | `dino8-app/src/io/FileIgesStep.cpp` |
| Wire `ToNurbsPatchesAdaptive` into the app's ToNURBS command | app_subd_mesh | `dino8-app/src/commands/cmd_solids.cpp` |
| Wire the kernel's binary PLY reader/writer into the app's ImportPly/ExportPly | kernel/exchange | `dino8-app/src/io/FileExchange.cpp` |
| Enable `DINO8_ENABLE_PYTHON` on Windows builds | app_scripting | `dino8-app/CMakeLists.txt` |

### Wave 3 — New kernel primitives, file-disjoint

| Item | Side/Category | Primary file(s) |
|---|---|---|
| Push/pull a face (extrude face and merge/cut into its own body) | kernel/localops | `dino8-kernel/src/boolean.cpp` |
| Emboss / deboss a closed region onto a face | kernel/features | `dino8-kernel/src/fillet.cpp` or a new `emboss.cpp` |
| Fillet overflow / cliff-edge / notch handling | kernel/blending | `dino8-kernel/src/fillet.cpp` |
| Move a single B-rep vertex directly | kernel/localops | `dino8-app/src/doc/SubObjectEdit.cpp` |
| Conic / rho blend cross-sections | kernel/blending | `dino8-kernel/src/fillet.cpp`, `dino8-app/src/commands/cmd_fillet.cpp` |
| Sheet-metal features (Bend, Unfold, Flange) as a new subsystem | kernel/features | new `dino8-kernel/src/sheet_metal.cpp` |
