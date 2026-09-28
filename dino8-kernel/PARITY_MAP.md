# Fossilith / Dino 8 parity map (2026-09-28)

**Fossilith vs Parasolid/ACIS = 64.4% (weighted, verified); Dino 8 vs Rhino 8 + AutoCAD 2027 = 71.4%.**

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

The main caveat is the same one every run of this method has: the
granularity of "one item" is a judgment call made by the mapper (this pass),
so item counts and percentages would shift somewhat under a different,
equally reasonable split of the same underlying capabilities.

## Kernel: Fossilith vs Parasolid/ACIS

| Category | Weight | Items | Present | Partial | Missing | Parity % |
|---|---|---|---|---|---|---|
| kernel: Topology & data structure | 1 | 27 | 11 | 13 | 3 | 64.8% |
| kernel: Geometry representation | 1 | 29 | 18 | 11 | 0 | 81.0% |
| kernel: Boolean operations | 1.5 | 25 | 8 | 14 | 3 | 60.0% |
| Blending & chamfering | 1.5 | 24 | 5 | 17 | 2 | 56.3% |
| kernel: Sweeping, lofting, extruding, revolving | 1 | 29 | 6 | 20 | 3 | 55.2% |
| kernel: Offsetting, shelling, thickening | 1 | 27 | 0 | 26 | 1 | 48.1% |
| kernel: Local / direct-edit operations | 1 | 28 | 6 | 17 | 5 | 51.8% |
| kernel: Intersections & projections | 1.5 | 29 | 13 | 14 | 2 | 69.0% |
| kernel: Healing, repair, validation, tolerant modeling | 1 | 30 | 18 | 11 | 1 | 78.3% |
| kernel: Mass properties & spatial queries | 1 | 30 | 16 | 14 | 0 | 76.7% |
| kernel: Tessellation / faceting | 1 | 25 | 12 | 11 | 2 | 70.0% |
| kernel: Transformations, patterns, splitting | 0.5 | 22 | 9 | 13 | 0 | 70.5% |
| kernel: Kernel-level data exchange | 1 | 27 | 8 | 11 | 8 | 50.0% |
| kernel: Feature operations | 1 | 24 | 5 | 14 | 5 | 50.0% |
| Fossilith kernel — Curve operations | 1 | 28 | 17 | 11 | 0 | 80.4% |
| Kernel Surface Operations (Fossilith / Dino 8) | 1 | 29 | 14 | 14 | 1 | 72.4% |
| Kernel: SubD & mesh kernel support | 0.75 | 22 | 13 | 6 | 3 | 72.7% |

Two rows changed from the prior measurement: **Local / direct-edit
operations** (48.2% → 51.8%, `ImprintFaces` cross-reference fix + arithmetic
correction) and **Transformations, patterns, splitting** (68.2% → 70.5%,
`SplitByObjectCommand` also credited here). **Intersections & projections**'
percentage also changed (65.5% → 69.0%) but that is purely the arithmetic
fix described above — no item's status moved. Every other row is numerically
identical to the 2026-09-25 map; several (booleans, blending, topology) have
corrected citations or evidence text behind an unchanged score, detailed in
the honesty notes above and the bullets below.

### Kernel category gaps (missing / partial items, with evidence)

**kernel: Topology & data structure** (topology):
- [partial] Multi-shell / multi-lump bodies (compound of disjoint or touching shells) — `Brep::Compound` (dino8-kernel/src/brep.cpp:2577-2612) calls `ON_Brep::Append` per lump and concatenates the side tables; lumps are deliberately not welded (dino8-kernel/include/dino8/kernel/brep.h:1571-1589, "Lumps are deliberately NOT welded to each other"). XOR uses the two-lump compound (`TestBooleanSymmetricDifferenceBrepBoxesIsTwoLumpCompound`, tests/test_basic.cpp:23235). `BooleanCombinePlanar`/`BooleanCombineMixed` both refuse a multi-lump operand via a shared `RefuseCompoundOperand` helper (boolean.cpp:815-822 — corrected citation; the prior "boolean.h:1321-1323" pointed at unrelated text). New finding: `BooleanCombineGeneral` has **no** compound-operand guard at all (zero references to `RefuseCompoundOperand`/`Compound`/`lump` in boolean_general.cpp) — a compound fed to the general engine is silently accepted and processed per-face with no lump-boundary awareness, an unhandled case rather than a clean refusal like the other two engines give. Still no inner-void (hollow) shell/region concept.
- [missing] Wire bodies (edge/vertex-only B-rep body) — re-grepped `wire ?body|WireBody` across dino8-kernel and dino8-app/src: no matches. Curves exist only as standalone `NurbsCurve` objects (dino8-app/src/doc/SceneObject.h:188).
- [partial] Non-manifold topology (edge shared by 3+ faces, non-manifold vertices) — construction refuses it: `FromMixedFaces` throws "an edge is shared by 3 or more faces" (brep.cpp:1587-1588; test `TestFromMixedFacesRejectsNonManifoldEdge`, tests/test_basic.cpp:17810). `Check()` reports `NonManifoldEdge` (brep.h:2458); `FacesOfEdge` handles 3+ trims (brep.h:605-611). Genuinely new this pass: `Check()` now also detects `NonManifoldVertex` (pinch-point) defects — a vertex whose incident faces don't form one connected neighbourhood through the vertex's own edges, distinct from `NonManifoldEdge` (an hourglass built from two shells touching at one point with no shared edge has no over-used edge anywhere) — via union-find over the faces touching each vertex's incident edges (brep.h:2474; brep.cpp ~6151-6260), verified by `TestBrepCheckDetectsNonManifoldPinchVertex` (test_basic.cpp:7365). A matching heal now exists too: `Brep::SplitNonManifoldVertex`/`SplitNonManifoldVertices` (brep.h:2662-2696; brep.cpp:6538 onward) duplicates the pinch vertex once per disjoint face group, the standard Parasolid/ACIS "disjoin" repair, verified by `TestBrepSplitNonManifoldVertexHealsPinchPoint` (test_basic.cpp:7409). Still partial: `UnjoinEdge` still refuses anything but exactly 2 trims (brep.cpp:5685); booleans still throw rather than producing non-manifold output; the new vertex heal only makes a pinch vertex manifold (splits it in two) rather than letting the kernel construct or preserve non-manifold topology as first-class, and there is still no non-manifold-edge counterpart to this vertex-level diagnostic+heal pair.
- [missing] Euler operators (MEV/MEF/KEV/KEF/KEMR/MEKR etc.) — re-grepped `MakeEdgeVertex|\bMEV\b|\bKEMR\b|Euler op`: nothing in the kernel or app.
- [partial] Kernel-level topology enumeration API (vertex/edge/loop/face iteration and counts) — `FaceCount`/`VertexCount`/`EdgeCount` (brep.cpp:304-306; brep.h:511-513), tested in `TestBrepAdjacencyQueries` (test_basic.cpp:17616). Still partial: no loop/trim count or iteration API (callers walk `raw().m_F[i].Loop(j)/Trim(k)` by hand); `VertexCount`/`EdgeCount` include deleted slots until `Compact()`; `Box()`/`Sphere()`/`Torus()` report 0 vertices and edges (confirmed `Torus()` also builds via the plain surface-only `NewFace(int)` overload, brep.cpp:229-266, same as Box/Sphere/TrimmedPlanarFace/FromSurface, even though the file's own top-of-file disclosure comment now names only 4 of these 5 factories — a minor staleness in the source's own comment, not in this claim).
- [partial] Loop structure: inner loops (holes), loop walking (Prev/NextTrim), outer/inner classification — real inner loops come only from the general boolean (boolean_general.cpp:3039 `BuildLoop(..., ON_BrepLoop::inner, ...)`). `TrimmedPlanarFace` holes are side-table polygons, not `ON_BrepLoop`s (brep.h:159-164). `MergeCoplanarFaces` skips any face with holes or more than one loop. No public loop API exists.
- [partial] Merge coplanar / co-surface adjacent faces (remove interior edge, rebuild one face) — `Brep::MergeCoplanarFaces` (brep.cpp:5415-5510; brep.h:2337) requires planar faces only, the same plane, single-loop faces, and exactly one shared 2-trim edge. App wiring: cmd_solidtools.cpp:1197. Still partial: nothing for co-cylindrical, co-spherical or tangent co-surface faces, or faces with holes.
- [partial] Merge contiguous tangent edges (combine two edges sharing a vertex into one) — app-only: `MergeEdgeCommand` (dino8-app/src/commands/cmd_fillet.cpp:2303-2320) calls `ON_Brep::CombineContiguousEdges` with a 5deg tangent tolerance. No kernel `Brep` wrapper.
- [partial] Remove edge / collapse micro edge (kill-edge-vertex style healing) — `Brep::RemoveNakedMicroEdge` (brep.cpp:5729) handles only an isolated naked sliver whose neighbours are also naked. `Brep::RemoveDegenerateEdges` (brep.cpp:6518) runs `ON_Brep::CollapseEdge` on shared/naked edges shorter than tolerance. There is no general "remove a shared edge above tolerance / merge its two faces" operation.
- [partial] Split / imprint a face by a curve while keeping the polysurface topology — app `SplitFace` (cmd_fillet.cpp:2057) splits the underlying surface at an isoline through the CSX hit, not an arbitrary trim loop. The only kernel split, `Brep::SplitNakedEdgeAt` (brep.cpp:6615), splits a linear naked edge; that is not a face imprint (the kernel's real face-face imprint, `ImprintFaces`, is scored under Boolean operations, not here).
- [partial] Delete / extract face (with or without healing neighbours) — app `ExtractSrf`/`DeleteFaces` (dino8-app/src/commands/cmd_srfedit.cpp:235-259) uses `ON_Brep::DuplicateFace`/`DeleteFace` and leaves an open shell. The kernel has no public delete-face API and nothing re-extends neighbours to heal the gap.
- [partial] Tolerance model on topological entities (vertex/edge tolerances, tolerant modelling) — `FixUnsetEdgeTolerances` (brep.cpp:5255), `RecordMeasuredTolerances` (brep.cpp:6089, records the measured gap as `ON_BrepEdge`/`ON_BrepVertex m_tolerance`), `Check()` honours those, `TessellateToClosedMeshTolerant` (brep.h:2840). Still partial: booleans and fillets never read edge tolerances (fixed `tol = 1e-6`); global tolerances are fixed constants, not scaled by model size.
- [missing] Persistent naming / topology identity and attributes across edits (face/edge IDs surviving Compact, boolean, split) — sub-object references are raw `m_E`/`m_F` indices (dino8-app/src/doc/SubObject.h:7-9); every topology edit clears the side tables and renumbers via `Compact`. There is no persistent ID or attribute scheme.
- [partial] Cap naked loops (close planar holes of an open shell into faces) — `Brep::CapPlanarHoles` (brep.cpp:6849; brep.h:2802-2822) walks naked-edge chains, checks planarity, builds the cap with `ON_BrepTrimmedPlane`, and re-joins. Still partial: a non-planar hole is left open, and chains through a vertex carrying more than two naked edges are skipped.
- [partial] Sliver / degenerate micro-face removal (as opposed to isolated boundary micro-edges) — `Brep::RemoveSliverFaces`/`RemoveDegenerateFaces` (brep.cpp:6510/6514; brep.h:2641/2651) delete faces `Check()` flags as `SliverFace`/`DegenerateFace` and re-join neighbours as tolerant edges. Still partial: delete-and-tolerant-join, not a geometric collapse; a T-junction sliver leaves naked edges since this does not call `SewTJunctions`. (The `Check()` false-positive this bullet used to cite as making the pair "destructive on the kernel's own valid solids" is fixed — see the top-of-document honesty note; re-verified this session end-to-end via `TestBrepRemoveDegenerateOrSliverFacesDoesNotTouchValidSolids`, which confirms 0 faces removed from `Box()`/`Extrude(circle)`.)
- [partial] Genuine topology produced by every constructor/primitive — `Box`, `Sphere`, `Torus`, `FromSurface` and `TrimmedPlanarFace` still use the surface-only `NewFace(int)` (brep.cpp:134-302; disclosed brep.h:25-51). Such a Brep has no edges or vertices and `ON_Brep::IsValid()` reports it invalid. Knock-on effects: `SplitDisjointPieces` throws on it, and adjacency queries return nothing. The sweep, boolean and fillet factories do build real topology.

**kernel: Geometry representation** (geometry):
- [partial] Knot removal (curve and surface, tolerance-controlled) — kernel `NurbsSurface::RemoveKnotAt` (surface_edit.cpp:314; surface.h:853-874) implements Piegl & Tiller A5.8 with a rigorous max-deviation bound. Still partial: `NurbsCurve` has no `RemoveKnot`, so curve knot removal is app-only and shape-changing (dino8-app/src/commands/cmd_curves2.cpp:64, Greville resample); periodic knot vectors are refused.
- [partial] Degree reduction (curve and surface, with error bound) — no kernel degree reduction (re-grepped `ReduceDegree|degree reduc`: nothing). App `ChangeDegree` "never lowers" (cmd_edit.cpp:62). Closest is `NurbsSurface::Rebuild`, a least-squares refit at any lower degree with a sampled (not certified) deviation bound. No curve equivalent beyond `FitLeastSquares`.
- [partial] Reparameterization: domain change, rational reparam, uniform knots, seam change, reverse/transpose — `NurbsSurface::SetDomain`, `Reverse`/`Transpose`, `NurbsCurve::Reverse`, `MakePeriodicExact` for curve and surface all exist. Missing: curve `SetDomain`, rational (Mobius) reparameterization, seam relocation. App `Reparameterize`/`MakeUniform` remain app-only and curve-only.
- [partial] Curve/surface interpolation and least-squares fitting through points — `NurbsCurve::FitLeastSquares` (global least-squares), `NurbsSurface::Rebuild` (tensor-product least-squares refit), and global cubic interpolation `InterpolateCubic` (open or closed, chord-length parameters) all exist. Still partial: no degree-p interpolation, no end-tangent constraints, no surface interpolation through a point grid.
- [partial] Helix and spiral curves — app `HelixCommand` (dino8-app/src/commands/cmd_create.cpp:301) builds `NurbsCurve::FromControlPoints` through sampled points; neither interpolating nor exact, and no kernel helix.
- [partial] Typed curve taxonomy and persistent composite (poly)curves — the document stores only `std::unique_ptr<kernel::NurbsCurve>` (dino8-app/src/doc/SceneObject.h:188). The kernel has only `NurbsCurve` plus `IsLinear`/`IsArc`/`IsCircle` classification. No persistent line/arc/polycurve types.
- [partial] Typed analytic surface classes with closed-form evaluation/inversion (distinct from NURBS) — every face is an `ON_NurbsSurface`. The analytic records are only `PlanarFace`/`CylindricalFace`/`ConicalFace`/`SphericalFace` structs, with no torus record. Evaluation and inversion always go through NURBS.
- [partial] Swept surfaces (one-rail and two-rail sweep) — kernel-native: `Brep::Sweep1` uses rotation-minimizing frames; `Brep::Sweep2` (sweep.cpp:1770-1854) is a genuine two-rail sweep with uniform cross-section scaling. Still partial: between stations both are interpolants, exact only for straight rails; `Sweep2` refuses touching rails and rail tangents parallel to the rail-to-rail direction; the app's own Sweep2 command still uses its own older approximate path, not the kernel.
- [partial] Offset surfaces (NURBS, tolerance-controlled) — `NurbsSurface::OffsetAnalytic` is exact for plane/sphere/cylinder/cone/torus; `NurbsSurface::OffsetApproximate` covers freeform surfaces via a first-order Greville-normal control-point move with a curvature fold guard. `tolerance` sets only the guard's sampling density, not a fit error bound.
- [partial] Numeric curve queries: arc length, parameter-at-length, division, tight bounding boxes — `Length` is a 1000-segment polyline chord sum that converges from below; `ParameterAtArcLength` interpolates linearly on that polyline. No adaptive Gauss integration or certified accuracy.
- [partial] Rational <-> non-rational conversion — `MakeRational` is exact; `MakeNonRational` keeps the control points but changes shape whenever weights vary (a radius-5 circle drifts to about 5.28). No tolerance-bounded non-rational approximation.

**kernel: Boolean operations** (booleans):
- [partial] Analytic plane/cylinder and cylinder/cylinder B-rep booleans (drilled holes, bosses, oblique holes, parallel/Steinmetz/unequal-radius/skew cylinder pairs) — `BooleanCombineMixed` (boolean.cpp:5976) covers perpendicular/oblique plane-cylinder cuts, Steinmetz pairs, unequal radii at any angle including skew full-pierce, and chained results as operands. Out of scope and thrown: grazing angles, partial penetration, a partial-sweep oblique operand, a second cut interacting with an already-notched fragment, a ConicalFace operand.
- [partial] General NURBS-surface B-rep boolean (SSX-driven boundary evaluation on arbitrary ON_Surface faces) — `BooleanCombineGeneral` (boolean_general.cpp:2738) is scoped to at most one crossing component per face pair, genus-0 faces, no self-crossing chains. Corrected figure, live-measured this pass by actually building and running `dino8_general_boolean_sweep` against current HEAD: **54 of 76** sweep combinations tessellate watertight via `TessellateGeneralBooleanClosedMesh` (boolean_general.h:62-79's own cited "15 of 76" was a stale intermediate number from partway through the kernel's own embedded development log, superseded by the log's own later entries before the last measurement) — 18 genuine non-empty residual failures (cyl+cyl parallel/Steinmetz/skew, sphere+box, sphere+sphere, one sphere+cyl direction) plus 4 trivial 0-face empty results.
- [partial] Coplanar / coincident face handling — the planar engine dedups identical planes; `BooleanCombineMixed` carries a coincident-face rule into each XOR lump; the general engine has its own coincident whole-face test (boolean_general.cpp:2899). No general partially-overlapping coincident curved-face handling.
- [partial] Tangent / grazing contact handling — the mesh engine retries once with adaptive tolerance on failure (`AdaptiveManifoldTolerance`, boolean.cpp:149-170). The B-rep engines throw at grazing incidence rather than resolving it.
- [partial] Multi-body / multi-tool booleans (N operands per side, multi-lump results and operands) — the app unions each side sequentially before combining (cmd_boolean.cpp:15-38). B-rep XOR returns a two-lump Compound, but compound operands are refused by the planar/mixed engines (boolean.cpp:815-822) and silently unguarded (not even refused) by the general engine. No kernel N-ary API.
- [partial] Result validity (closed manifold / ON_Brep IsValid / IsSolid) — the mesh contract is fuzz-checked (IsClosedManifold on every result). Planar results pass IsValid/IsSolid. General-engine results close in 54 of 76 cases (see the corrected figure above), not watertight otherwise.
- [partial] Tolerant booleans (caller-specified tolerance, gap-healing of imprecise operands) — no boolean API takes a tolerance; the general engine uses a fixed `tol = 1e-6` (boolean_general.cpp:678). The only adaptivity is the mesh-engine retry. Recorded edge tolerances are written (e.g. boolean_general.cpp:2250 sets `edge.m_tolerance = 0.0`) but never read back as an input.
- [partial] Keep/split options (BooleanSplit solid-by-solid keeping all pieces, DeleteInput/keep tools, side selection) — BooleanSplit/MeshSplit/MeshBooleanSplit (cmd_boolean.cpp:410-415) are all plane-split only (kernel `SplitByPlane`). The new `SplitByObjectCommand` (see kernel: Feature operations and kernel: Transformations) is a general cutting-object split with true KeepAll semantics, but it is app-level mesh-boolean, not this item's B-rep solid-by-solid split.
- [missing] Sheet/solid trim (open surface as cutter through a solid; trimming a sheet body by a solid) — the app skips non-closed operands ("not a closed solid; skipped", cmd_boolean.cpp:21,151,193,305). Every B-rep engine assumes closed two-shell solids (boolean.cpp:81).
- [partial] Non-manifold boolean results (edge/vertex-touching unions, single-body XOR, 3+ faces per edge) — the B-rep engines throw "an edge is shared by 3 or more faces" instead of building non-manifold output; XOR is an unwelded two-lump Compound; the mesh XOR keeps duplicated vertices.
- [partial] Face-face imprint (Parasolid PK_BODY_imprint / ACIS imprint: split faces along mutual intersection without removing material) — `dino8::kernel::ImprintFaces(target, tool)` (boolean_general.h:61; boolean_general.cpp:3086) reuses `BooleanCombineGeneral`'s own SSX-driven face-fragmentation but keeps every fragment of `target` unconditionally — no ray-cast in/out classification, no material ever removed — so `target` keeps its exact original shape/volume with more, smaller faces wherever `tool` crosses it; `tool` itself is read-only. Verified on a closed-loop fixture (box pierced by a cylinder) and an open-chain fixture (two overlapping boxes), each direction, plus a disjoint-operand no-op and a faceless-operand `std::invalid_argument` (`TestImprintFaces*`, tests/test_basic.cpp:3214-3353). Still partial: only `target`'s faces split per call (call it twice, swapped, for a true mutual imprint of both bodies), it inherits `BooleanCombineGeneral`'s own scope limits, and no app command exposes it yet — re-confirmed this pass (`ImprintFaces` has zero hits anywhere in dino8-app/).
- [partial] 2D region / planar curve booleans (CurveBoolean, AutoCAD REGION union/subtract/intersect) — `RegionBoolean` (dino8-app/src/commands/cmd_solidtools.cpp:1525) runs through thin mesh slabs in Manifold and recovers outlines. No exact 2D curve boolean in the kernel.
- [partial] Boolean failure diagnostics (typed refusals, failure reasons, naked-edge reporting) — the kernel throws `std::invalid_argument` naming the specific precondition; the mesh engine gives a generic Manifold status string. No structured failure-report type exists.
- [partial] Free-form (non-analytic) NURBS surface operands in B-rep booleans — `BooleanCombineGeneral` is written for any `ON_Surface`, but every test/sweep operand is an analytic primitive. No freeform-operand test exists.
- [missing] B-rep-preserving booleans reachable from the application (polysurface in, polysurface out) — every app boolean tessellates its operands (`MeshOf`) and emits a mesh result; zero references to `BooleanCombinePlanar`/`Mixed`/`General` anywhere in dino8-app/src.
- [missing] Associative/history-enabled Boolean operations (result auto-updates when source solids move, a la Rhino's History) — `HistoryRecord::command` (dino8-app/src/doc/Document.h:377) covers `"Extrude", "ExtrudeCrvToPoint", "Revolve", "Loft", "SubDLoft"` only, no boolean commands.
- [partial] AutoCAD-style INTERFERE (interference detection that builds real solid bodies from the overlap regions of many objects) — `dino8::kernel::ComputeInterference(bodies, clearance)` (boolean.h; boolean.cpp) now builds the real overlap solids: for every pair of input bodies whose (optionally clearance-expanded) bounding boxes touch, it runs the actual `BooleanCombine(..., BooleanOp::Intersection)` and keeps only pairs whose overlap has nonzero volume (`FaceCount() > 0` - verified empirically that Manifold returns a genuine zero-face, zero-volume result for two solids that only share a coincident face, not a degenerate sliver, so this is a correct volume test, not just a bbox heuristic). Covers N bodies pairwise, each returned `InterferenceResult` carrying the real closed intersection `Mesh`. Tested (`TestComputeInterference`, tests/test_basic.cpp): a genuine overlap is reported with the correct pair indices and volume, disjoint bodies report nothing, a full-face zero-volume touch is correctly excluded, a generous `clearance` never fabricates a solid for geometry that doesn't truly overlap, and a non-closed operand in a bbox-touching pair throws `std::runtime_error` like `BooleanCombine` itself. Still partial, not present: `Clash` (dino8-app/src/commands/cmd_solidtools.cpp:1210) is not yet wired to call this - no app command exposes real INTERFERE solids to the user yet, and this is pairwise-only, not the fully general N-way simultaneous overlap AutoCAD's INTERFERE can report for 3+ mutually-overlapping bodies at once.

**Blending & chamfering** (blending):
- [partial] Constant-radius edge fillet on curved adjacent faces (cylinder/plane, cylinder/cylinder, freeform, closed/periodic rims) with B-rep trimming — every kernel fillet still requires both adjacent faces to be planar (fillet.h:147-159), so fillets cannot be chained onto a solid that already carries a curved face. App `FilletEdge` produces a genuine B-rep trim only when both faces are planar (cmd_fillet.cpp:175, "exact for planes; approximate elsewhere").
- [partial] Concave (internal) edge fillet — kernel-native and exact: `FilletConcaveEdge` (fillet.cpp:989; fillet.h:162-270) builds the mirrored rolling-ball construction with outward=false, closing perpendicular and oblique third faces; `FilletConcaveEdges` (fillet.cpp:2746; fillet.h:1111-1205) fillets several independent edges plus m==3 trihedral concave spherical corners. Still partial: planar faces only, one radius, m>=2 or higher-valence corners throw, oblique third faces out of scope for `FilletConcaveEdges`, a mixed convex+concave solid cannot be fully filleted, nothing in the app calls it.
- [partial] Variable-radius fillet (linear / piecewise-linear radius law, radius handles) — kernel `FilletConvexEdgeTapered` (fillet.cpp:221-438), both a two-radius form and an N-station form, builds exact `ConicalFace` segments. App `Radii=` handles are exact only for plane/plane and plane-with-perpendicular-cylinder; everything else falls back to approximate `BuildFillet`. No non-linear laws and no curved-face taper.
- [partial] Chamfer with two unequal distances (D1/D2) or distance + angle (AutoCAD CHAMFER Angle method, Rhino ChamferEdge per-handle distances) — the kernel has exact D1/D2 chamfers (`ChamferConvexEdge`) and distance+angle (`ChamferConvexEdgeAngle`), plus concave versions `ChamferConcaveEdge`/`ChamferConcaveEdgeAngle` (fillet.cpp:2194-2210). Still partial: planar faces only; app `ChamferEdge` exposes only one Radius, no D1/D2 or angle; no unequal-distance chamfer on curved faces.
- [partial] Face-face blend between two independently picked surfaces (FilletSrf / ChamferSrf, non-adjacent faces, with trimming of both inputs) — `FilletTwoSurfacesCommand` (cmd_fillet.cpp:864) trims only through `TrimWholeLoop` (cmd_fillet.cpp:964) when an input is planar; otherwise the input is left untrimmed.
- [partial] Vertex blend (three or more fillets meeting at a vertex: spherical/setback corner patch) — `FilletConvexEdges` m==3 spherical corner (requires one face perpendicular to the other two); `FilletConcaveEdges` covers the m==3 concave sphere; single-facet vertex chamfers on any convex or concave trihedral corner with asymmetric per-edge distances exist (`ChamferConvexVertex`/`ChamferConcaveVertex`, fillet.cpp:3133 onward). Still partial: m==2, valence >3, and non-perpendicular (e.g. tetrahedron) corners all throw for fillets; corners with mixed radii unsupported; no setback or non-spherical corner patches.
- [partial] Fillet end conditions on adjacent end faces (corner notch of the third face, shared cap edge) — closed exactly with a shared edge for single-edge `FilletConvexEdge` (perpendicular or oblique third face), `FilletConcaveEdge` (oblique third face), `ChamferConvexEdge` (oblique third face), and tapered cones via ellipse notches. Still partial: `FilletConvexEdges` leaves an oblique third face untouched at m==1, `FilletConcaveEdges` has oblique ends out of scope, no handling on non-planar end faces.
- [partial] Edge blend trimmed and joined into the polysurface (Rhino BlendEdge TrimAndJoin behaviour) — `BlendEdge` registration text (cmd_fillet.cpp:2647, and comment at cmd_fillet.cpp:1388) still reads "Hermite blend surface added between the two faces (not stitched into the polysurface)".
- [partial] Conic / rho (chordal, elliptical) blend cross-sections — kernel-native conic/rho blend exists, `FilletConvexEdgeConic` (fillet.cpp:2240; fillet.h:995): an exact rational-quadratic-Bezier cross-section giving a true ellipse (rho<0.5), parabola (rho=0.5) or hyperbola arc (rho>0.5) tangent to both faces, swept translationally and spliced onto the re-trimmed faces. Still partial: v1 has no third-face/vertex end-condition splicing, and — re-checked specifically this pass — the app layer does not expose it at all: `cmd_curves2.cpp`'s `ConicWeightThrough`/Rho option is an unrelated 2D-curve-through-3-points construction tool, not this edge-blend feature.
- [partial] Fillet/blend on tangent edge chains and multi-edge selection in one operation (ChainEdges, FaceEdges, double-click tangent propagation) — corrected evidence: re-grepped `ChainEdges`/`FaceEdges` across all of dino8-app/src, zero matches anywhere; the prior claim that "the command catalogue lists ChainEdges/FaceEdges" was unfounded. The nearest real thing, `SelChain` (cmd_select.cpp:160), is a general curve-chaining selection helper for `ObjectKind::Curve` objects only, unrelated to solid edges or fillet/chamfer commands. Kernel `FilletConvexEdges`/`FilletConcaveEdges` fillet many straight edges in one call, but have no tangent-chain propagation and no curved edges.
- [missing] Fillet overflow / cliff-edge / notch handling (blend running off a face onto neighbouring faces, over-large radius consuming a face) — every kernel fillet and chamfer throws "radius/distance too large to fit" (`FilletConvexEdge` fillet.cpp:792, `FilletConcaveEdge` :1129, `ChamferConvexEdge` :2039, `FilletConvexEdgeConic` :2299) instead of rolling onto the next face; the app reports the failure rather than handling it.
- [partial] Blend removal / defeaturing with healing (delete fillet faces and re-extend neighbours to restore the sharp edge) — `RemoveBlend` (fillet.cpp:3768; fillet.h:1466) recovers the sharp edge for cylindrical and conical fillets, convex or concave, and restores corner notches; `RemoveChamfer` (fillet.cpp:3952) and `RemoveChamferVertex` (fillet.cpp:4112) do the chamfer equivalents. Still partial: only reverses this kernel's own constructions on planar-plus-blend solids; spherical vertex-blend corners and oblique-end cylindrical fillets throw; nothing in the app calls any of them.
- [partial] Fillet surface along a user-supplied rail curve (FilletSrfToRail) — `FilletSrfToRailCommand` (cmd_srfedit.cpp:2137) uses the picked rail directly as the ball-centre spine, with contacts at plain closest points and no trimming.
- [missing] Alternative blend rail types (distance-from-edge, distance-between-rails / disc blend, non-rolling-ball cross-section placement) — every path places contacts with a rolling ball; a grep for RailType/DistBetweenRails/DistFromEdge finds nothing.
- [partial] 2D curve fillet / chamfer / polyline corner rounding (Rhino Fillet, Chamfer, FilletCorners; AutoCAD FILLET/CHAMFER) — app-only (`FilletChamferCommand`/`FilletCornersCommand`, cmd_curveedit.cpp:510/646). No kernel 2D fillet or chamfer API.
- [partial] Curve-to-curve blend, tangent (G1) and curvature-continuous (G2) Hermite (Blend / BlendCrv command) — app-only `BlendCrvCommand` (cmd_curves2.cpp:1132), G1 cubic or G2 quintic. No G3+ and no kernel API.
- [partial] Curve-to-curve blend commands: BlendCrv (G1 tangent cubic), Blend (G2 curvature-continuous quintic Hermite matching position/tangent/curvature vector), ArcBlend (two-arc tangent biarc) — same app-only commands as above; kept as a separate item to preserve the category's item count, per the original document's own item split.
- [partial] Surface-to-surface continuity blend (BlendSrf / VariableBlendSrf) — `BuildBlendSurfaceG1`/`G2` (dino8-app/src/geom/BlendSurface.h:92 onward) do G1 cubic or G2 quintic, with G2 only in the cross-boundary direction. No G3/G4, no shape/bulge handles, silently falls back to G1 at singular parametrizations.
- [partial] Rolling-ball blend surface accuracy on freeform/curved surfaces (tolerance-controlled blend geometry) — app `BuildFillet` builds circular rows along an SSX spine of offset surfaces; the offset move is approximate on curved surfaces, and the recorded `max_gap` quality signal is never enforced against a tolerance.

**kernel: Sweeping, lofting, extruding, revolving** (sweeplofts):
- [partial] Extrude a curve along a path curve (translational sweep / sum surface, ExtrudeCrvAlongCrv) — app `ExtrudeAlongCommand` (cmd_surface.cpp:1180) uses `ON_SumSurface::Create(profile, path)`, exact but output is only an open surface (no Solid/cap option, no kernel entry point). `Brep::Sweep1` rotates the section with RMF frames — a different operation.
- [partial] Extrude a surface / polysurface face into a solid (ExtrudeSrf) — app loops `ON_BrepExtrudeFace` over every face independently, direction always the CPlane normal. The kernel only has a mesh equivalent (`Mesh::ExtrudeCappedSolid`); no kernel B-rep face-extrude API.
- [partial] Extrude with draft / taper angle (ExtrudeCrvTapered, ExtrudeSrfTapered; AutoCAD EXTRUDE Taper) — kernel `Brep::ExtrudeTapered` (brep.h:253-317; sweep.cpp:1430-1482) is exact for a line or circle/arc and for convex polylines via a closed-form miter offset. Still partial: a non-convex polygon throws, an oblique direction throws (confirmed sweep.cpp:1448-1452), a general curved profile falls back to an approximate least-squares offset, no surface/solid taper in the kernel, and the app's own ExtrudeCrvTapered (cmd_surface.cpp:1229) still scales the profile about its centroid (approximate corners) rather than calling the kernel.
- [partial] Extrude to a point (ExtrudeCrvToPoint / ExtrudeSrfToPoint / kernel ConeToApex) — app `RebuildExtrudeToPoint` (cmd_solids.cpp:64) uses `CreateRuledSurface` to a degenerate apex curve, giving a surface only with no cap even for a closed profile. Kernel `Mesh::ConeToApex` is mesh-only; `Brep::Loft` to a point section cannot be capped (a collapsed end refuses a cap).
- [missing] Extrude to a boundary surface / body (Rhino ToBoundary, Boss-to-boundary; AutoCAD extrude "to face", PressPull) — "ToBoundary" appears only as catalogued option text; no implementation anywhere.
- [partial] Full 360-degree revolve of a profile about an axis into a capped solid (Revolve, RevolvedHole) — kernel `Brep::Revolve` (sweep.cpp:1483-1595) is exact rational and handles L profiles (poles), closed off-axis profiles (torus-like), a semicircle (exact sphere), and off-axis ends with disc caps. App `RevolvedHole` (cmd_solidtools.cpp:906, "mesh boolean; results are meshes") cuts with a mesh boolean. Still partial: a closed profile touching the axis (e.g. a rectangle with one side on the axis) throws, and `RevolvedHole`'s result is a mesh.
- [partial] Partial-angle revolve (start angle / revolution angle < 360, with planar side caps) — kernel `Brep::Revolve`'s `angle` parameter in (0, 2pi] gives planar pie-slice fan caps for closed profiles and open profiles with both ends on the axis. Still partial: no start-angle parameter; an open profile with an off-axis endpoint cannot be capped at a partial angle; a closed profile touching the axis throws; the app still hard-codes 0..2pi with no angle option anywhere.
- [partial] Rail revolve (profile revolved about an axis while following a rail curve) — app `RailRevolveCommand` (cmd_srfedit.cpp:1019) scales the profile radially by rail distance on a sample grid and fits with `SurfaceThroughRows`. Output is a surface only. No kernel equivalent.
- [partial] Sweep along one rail (Sweep1: rotation-minimizing frames, multiple sections blended, closed rail) — kernel `Brep::Sweep1` (sweep.cpp:1697-1769) uses double-reflection RMF and gives real capped B-rep solids; a straight rail becomes an exact extrusion. Still partial: the kernel takes one section only (no multi-section blending, unlike the app's own `Sweep1Command`), the section moves rigidly with no scaling, the wall is a station-count interpolant, and the app does not use the kernel.
- [partial] Sweep along two rails (Sweep2) — kernel `Brep::Sweep2` (sweep.cpp:1770-1854) uses two-rail frames with a single uniform scale by rail-to-rail width; two straight rails give an exact ruled wall. Still partial: one section only, uniform scale only, a station interpolant on curved rails, throws where rails touch or a tangent is parallel to the rail-to-rail direction (sweep.cpp:1244,1255); the app's own Sweep2 command is unchanged and does not call the kernel.
- [partial] Sweep controls: twist along path, scale along path, road-like / fixed-up alignment (AutoCAD SWEEP Twist/Scale/Alignment, Rhino Roadlike/Frame rotate) — kernel-native twist exists: `Brep::Sweep1`'s optional `twist_total` argument (brep.h:422) adds an extra rotation about the rail's own tangent, linear in arc-length station fraction, on top of the rotation-minimizing frame; EXACT on a straight rail, confirmed by directly reading `TestSweep1TwistIsExactOnAStraightRailAndRejectsOnClosedRail` (tests/test_basic.cpp:27968), which hand-predicts both end sections' exact corner positions and confirms `twist_total` is refused on a closed rail. Still partial: no scale-along-path control, no road-like/fixed-up frame alignment, and the app's `Sweep1Command` still has no Twist option.
- [partial] Loft options: Loose/Tight/Uniform styles, Closed loft, start/end tangency matching to surfaces, guide curves, Rebuild/Refit — kernel `Brep::Loft` provides a closed (periodic) loft, a degree choice, and exact start/end tangency: optional `start_tangent`/`end_tangent` `NurbsCurve` arguments (brep.h:391-393) pin the wall's derivative at a constrained end in closed form, confirmed exact across the full u range by directly reading `TestLoftTangentConstrainedEndsMatchExactly` (tests/test_basic.cpp:27782). Still partial: this is curve-to-vector-field tangency, not surface-to-surface edge tangency matching; requires degree >= 2 and non-rational open sections; not exposed in the app's `LoftCommand` at all; still no Loose/Tight/Uniform styles, no guide curves, no Rebuild/Refit.
- [partial] Developable loft between two rails (DevLoft) — app `DevLoft` (cmd_remaining.cpp:952) is a monotone twist-minimising ruling search producing an approximately-developable ruled surface. No kernel equivalent (`UnrollDevelopable` unrolls surfaces but does not construct a developable loft).
- [partial] Pipe: constant-radius tube around a curve with optional caps — kernel `Brep::Pipe` (sweep.cpp:1855-1870) is an exact rational circle swept by Sweep1: exact on a straight rail, flat fan caps, closed-rail tube. App `PipeCommand` gives a mesh when Cap=Yes or the rail is closed. Still partial: curved rails are a station-count interpolant, only flat caps (no Round option), no kinked-rail handling.
- [partial] Pipe variants: multiple radii along the rail, thick-walled (inner+outer) pipe, MultiPipe per-branch radii — kernel `Brep::PipeVariable` (brep.h:465-509; sweep.cpp:1871-1979) piecewise-linearly interpolates (t, radius) control points, exact for a 2-point taper on a straight rail (`TestPipeVariable`, tests/test_basic.cpp:28019). Still partial: no thick-walled pipe, `MultiPipe` is still single-radius capped meshes unioned, the app `PipeCommand` still has a single radius.
- [partial] Cap planar openings of open polysurfaces (Cap; kernel end-cap synthesis) — app `Cap` samples 8 points per naked edge into a polyline before capping, so a curved hole gets a polygonal cap. Kernel `Brep::CapPlanarHoles` (brep.cpp:6849) gives a genuinely re-capped closed solid, but refuses any curved naked edge (`if (!e.IsLinear(tolerance::kDistance)) ok = false;`, brep.cpp:6886). Unaffected by the `Check()` false-DegenerateFace defect — `CapPlanarHoles` doesn't call `Check()`/`RemoveDegenerateFaces`.
- [partial] Sweep/extrude a surface, polysurface or mesh face along a path, tapered, or to a point into a mesh solid (ExtrudeSrfAlongCrv/ExtrudeSrfTapered/ExtrudeSrfToPoint) — app `ExtrudeSrfCommand` with translation-only station transforms; mesh output only, no B-rep version in the kernel.
- [partial] Feature extrusions unioned with a base solid following its local normal (Boss, Rib) — app `BossRibCommand` (cmd_srfedit.cpp:2009) projects the curve to the nearest face, lofts rings, and does a mesh boolean union; mesh result, no kernel feature op.
- [partial] Closed, topologically joined B-rep solid output from Sweep1/Sweep2/Loft/Pipe/RailRevolve (auto-cap + join) — every kernel sweep-class factory now returns a real closed `ON_Brep` with literally shared edges (verified via `AssembleSweptBody`, orientation checked by volume sign). Still partial: no kernel `RailRevolve`; fan caps require star-shaped closed sections; every app command still emits an untrimmed surface or a mesh, not the kernel B-rep.
- [missing] ExtrudeCrv / ExtrudeCrvAlongCrv / Revolve producing a SubD object directly (Output=Surface|SubD option) — the command catalogue lists a SubD output option, but no extrude or revolve command in the app implements it.
- [partial] Kernel-level partial-angle revolve parameter (RevolveProfile has no angle argument) — `Mesh::RevolveProfile` (mesh.cpp:2250) takes a trailing `angle`; a full angle keeps the exact shared-vertex ring path, a partial angle delegates to `Brep::Revolve` and tessellates (an approximation for that path). Still partial: an off-axis profile endpoint cannot be capped at a partial angle, a closed profile touching the axis throws, no start-angle parameter.
- [partial] Sweep1/Sweep2 producing a SubD result (SubDSweep1, SubDSweep2) — app `SweepThenSubDCommand` (cmd_subd.cpp:1235) runs the (approximate) app Sweep1/Sweep2 and converts the result to SubD after the fact; not a native SubD sweep.
- [missing] SubD-result revolve and multi-pipe menu entries are broken references, not implemented commands (SubDRevolve, SubDMultiPipe) — the SubD menu (MenuBar.cpp:182) still lists both names, with no matching command registration anywhere.

**kernel: Offsetting, shelling, thickening** (offsetshell):
- [partial] Closed hollow shell (uniform wall, no openings) of a solid — app `ShellCommand` still hollows via mesh offset + mesh boolean. Kernel `ShellClosedSphere`/`ShellClosedTorus` (boolean.h:415-451) give an exact B-rep shell, but only for a full sphere or torus; `OffsetSolid(-t)` plus a boolean gives a general hollow at mesh level only.
- [partial] Shell with removed/open faces (cup/case), including multi-face openings — kernel `ShellConvexPlanar` (boolean.h:340-387) is exact but convex planar solids only, and mutually-adjacent removed faces are refused. App `ShellCommand` face removal works on a mesh for simple box-like solids only.
- [partial] Per-face (multi-thickness) shell — kernel `ShellConvexPlanar` per-face overload exists (convex planar solids only). App `OffsetMeshPerFace` remains mesh-level.
- [partial] Face offset in place (move one face along its normal, neighbours re-intersected, B-rep kept) — kernel `OffsetFace` (boolean.h:453-488) moves one plane and re-clips every other face against it, but limited to convex planar solids with no topology change allowed (a face vanishing throws); not wired to any app command. App `MovePartsCommand` remains approximate.
- [partial] Body offset (offset an entire closed solid outward/inward as a B-rep) — kernel `OffsetSolid` is a ball dilation/erosion through Manifold Minkowski, mesh-level not B-rep. New this pass: `OffsetSolidConvexPlanar` (boolean.h/.cpp) is an exact B-rep whole-body offset — every face of a convex planar-faced solid moved along its own outward normal at once (uniform or independently per face), sharp/mitered corners reconstructed via the same `ClipConvexPolygon` half-space-clipping `OffsetFace`/`ShellConvexPlanar` already use — verified against an exact box (closed-form volume, both uniform and per-face) and a hand-built tetrahedron (checked against an independent three-plane-intersection recomputation of every new vertex). Still partial: convex planar solids only (the same precondition `OffsetFace`/`ShellConvexPlanar` already enforce), no curved or non-convex body, and not wired to any app command. App `OffsetSrf` non-Surface branch uses a mesh vertex-normal offset.
- [partial] Untrimmed NURBS surface offset — kernel `NurbsSurface::OffsetAnalytic` is exact for plane/sphere/cylinder/cone/torus; `OffsetApproximate` covers freeform surfaces but is first-order with `tolerance` controlling only guard sampling, not the fit error.
- [partial] Trimmed-surface / polysurface offset with corner reconstruction (Sharp extend-and-intersect or Round blend) — sharp corners exist only for convex planar solids (`ShellConvexPlanar`, `OffsetFace`, and now the whole-body `OffsetSolidConvexPlanar`); round corners only via mesh-level `OffsetSolid`; app polysurfaces fall to the mesh path. No trimmed curved-face B-rep offset.
- [partial] Tolerance-driven offset refit (fit the offset surface/curve to a tolerance, Loose/Tolerance options) — curves have it: `NurbsCurve::OffsetInPlane` doubles control points until the measured worst-case deviation is within `tolerance`. Surfaces do not: `OffsetApproximate` never refits to a tolerance.
- [partial] Variable-distance surface offset — app `VariableOffsetSrfCommand` is a per-CV Greville-normal offset with distance varying linearly; app-only, no kernel API.
- [partial] Thicken sheet (open surface/mesh) into a closed solid — kernel `Mesh::Thicken` (mesh.cpp:3295) works on open meshes only with no fold repair. App `OffsetSrf` Solid=Yes stitches with `ShellBetween`. Mesh output only; no NURBS/B-rep thicken.
- [partial] Planar curve offset (lines, arcs/circles, freeform NURBS) — kernel `NurbsCurve::OffsetInPlane` is exact for a line or arc/circle, tolerance-driven least-squares refit for other curves. Still partial: a kinked polyline goes through the smooth refit, blurring corners and potentially splitting a closed polygon's seam.
- [partial] Curve offset corner handling at kinks (Sharp/Round/Chamfer/Smooth/None) — sharp (miter) only, via a file-local `OffsetConvexPolyline` (convex polylines only, not a public API) and the app's `OffsetPolygon`. No Round/Chamfer/Smooth corner modes.
- [partial] Offset self-intersection / invalid-loop removal (inward offset of concave curves, surfaces and bodies) — no loop removal for curves or surfaces (refused); detection exists via `Mesh::FindOffsetSelfIntersections` (mesh.cpp:3342), curvature guards in `OffsetInPlane`/`OffsetApproximate`, and `OffsetAnalytic` radius/spindle guards. `OffsetSolid`'s Minkowski erosion/dilation cannot self-intersect by construction but is only tested on convex fixtures.
- [partial] Curve offset on surface (in-surface, geodesic-style) — app `OffsetCrvOnSrfCommand`: sample, move along tangent x normal, re-project by closest point. App-only, approximate.
- [partial] Curve offset normal to surface (OffsetNormal) — app `OffsetNormal`: samples moved along the surface normal, then cubic interpolation. App-only.
- [partial] Curve offset in an arbitrary plane / 3D (non-planar) curve offset — kernel `OffsetInPlane` works in the curve's own fitted plane but returns Failed for non-planar curves. The app Offset command uses only the active CPlane normal. No 3D offset.
- [partial] Mesh offset (per-vertex offset, solid/shell option) — kernel `Mesh::Offset` (mesh.cpp:3283) plus `Thicken` for the solid option (open meshes only), plus `FindOffsetSelfIntersections`. Still partial: an area-weighted vertex-normal push does not preserve wall thickness at creases, and folds are not repaired.
- [partial] SubD offset / thicken — app `OffsetNet` (cmd_subd.cpp:595) offsets the control net (not the limit surface); Solid adds a flipped copy plus side quads. No kernel SubD offset.
- [partial] Offset-derived constructions (Ribbon, RibbonOffset, Fin, Slab) — `RibbonCommand`, `FinCommand`, `RibbonOffset`, Slab via `OffsetPolygon`: all sample-and-fit, app-only.
- [partial] Exact analytic-face offset (plane->plane, cylinder->cylinder, cone->cone, sphere->sphere with shifted radius) — kernel `NurbsSurface::OffsetAnalytic` is exact for plane/sphere/cylinder/cone/torus, but sphere and torus return the full primitive rather than the input patch, a cylinder becomes a full 360-degree cylinder, and a cone is rebuilt from an `IsCone` fit — so a partial analytic patch (e.g. a quarter-cylinder fillet face) does not keep its extent. Only the plane branch keeps the domain and trims. It is also a single-surface operation, not a face within a B-rep.
- [partial] Offset feasibility / degeneracy detection (thickness beyond inradius, collapsed faces, wrong-way rims) — many guards exist (`ShellConvexPlanar`, `OffsetFace`, `OffsetSolid`, `OffsetAnalytic`, `OffsetInPlane`/`OffsetApproximate` curvature guards, `ExtrudeTapered` inradius check, `FindOffsetSelfIntersections`). Still partial: freeform checks are local-curvature/sampling-based and can miss hazards between samples; no global collision check.
- [partial] Kernel-level offset API (NurbsCurve::Offset, NurbsSurface::Offset, Brep offset/shell entry points usable by booleans and fillets) — each named family exists (`OffsetInPlane`, `OffsetAnalytic`/`OffsetApproximate`, `ShellConvexPlanar`, `ShellClosedSphere`/`Torus`, `OffsetFace`, `OffsetSolidConvexPlanar`, `OffsetSolid`, `Mesh::Offset`/`Thicken`). Still partial: no general Brep offset/shell for curved or non-convex bodies, and no app command calls any of these kernel entry points yet.
- [partial] Solid dilation/erosion via kernel::MinkowskiSum/MinkowskiDifference with a ball (whole-body offset that handles arbitrary curved/concave meshes, not just convex-planar) — wrapped as `OffsetSolid(solid, distance, sphere_divisions)` with a faceted ball and an empty-erosion guard. Still partial: mesh-only, rounding only as smooth as the faceted sphere, every test fixture is convex so the "arbitrary concave" claim is unverified.
- [partial] OpenNURBS-native mesh offset, ON_Mesh::OffsetMesh(distance, direction) — `ON_Mesh::OffsetMesh` is still never called; the kernel has its own vertex-normal equivalent (`Mesh::Offset`), but the fixed-`direction` variant has no kernel counterpart.
- [partial] Inset (offset mesh/SubD/polysurface face edges inward toward face center) — app `InsetFaces` (cmd_subd.cpp:544) moves each corner toward the face centroid, not a true in-plane edge-parallel inset. SubD only; no kernel inset.
- [missing] Inset on raw mesh or polysurface objects (as opposed to SubD) — `Inset` still routes every target through `SubDTargets` (cmd_subd.cpp:827), which rejects any non-SubD object. No mesh or Brep inset in the kernel.
- [partial] ShrinkWrap Offset (signed-distance-field / marching-cubes mesh offset, inherently self-intersection-free) — app `ShrinkWrapAction`'s (cmd_remesh.cpp:198) Offset option feeds `MarchingCubes(grid, offset)`. App-level, voxel-resolution accuracy.

**kernel: Local / direct-edit operations** (localops):
- [partial] Split an edge at a point (SplitEdge) — app `SplitEdgeCommand` (cmd_fillet.cpp:2153-2301) does a real vertex/edge/trim split. Kernel `Brep::SplitNakedEdgeAt` covers only naked, straight edges. The app splits each trim at the same normalized parameter fraction as the 3D edge, exact only when trim and edge parameterizations are proportional — approximate on curved or non-uniformly parameterized trims.
- [partial] Merge coplanar adjacent faces (MergeFaces / MergeAllCoplanarFaces) — kernel `Brep::MergeCoplanarFaces` skips any face with a hole and any pair sharing more than one edge. The app's `MergeFaces`/`MergeAllCoplanarFaces` (cmd_fillet.cpp:2668-2669) instead slices a mesh union of thin slabs back into a polyline outline, losing curved boundaries and ignoring inner loops.
- [partial] Move/transform face (tweak face, neighbours adjust) — app `MoveBrepParts`/`MoveBrepFaces` (cmd_srfedit.cpp:864) are documented as "Approximate MoveFace / MoveEdge". Kernel `OffsetFace` is exact but only for translating a face along its own normal on a convex planar solid. No general face transform.
- [partial] Move/transform edge (tweak edge) — same approximate `MoveBrepParts` path as face move. No kernel edge-move op.
- [partial] Rotate face about hinge edge (FoldFace / rotate-face tweak) — app `FoldFaceCommand` (cmd_srfedit.cpp:951) uses `MoveBrepFaces` with a rotation transform; app-only and approximate, no kernel equivalent.
- [partial] Offset face (translate face along its normal, neighbours re-extended/re-trimmed) — kernel `OffsetFace` (boolean.h:453-488) re-clips every other face into a valid closed B-rep, but convex planar solids only, throws if any face would vanish, and not wired to any app command.
- [partial] Delete face with heal (remove face, grow neighbours to close the gap) — app `DeleteFaces` (cmd_srfedit.cpp:243-254) only calls `ON_Brep::DeleteFace` + `Compact`, leaving a hole. Kernel `Brep::CapPlanarHoles` can re-cap a planar hole with straight edges, but that is not a heal that extends the neighbours.
- [partial] Split face by curve / surface (real trim-loop split in place) — app `SplitFaceCommand` (cmd_fillet.cpp:2057) finds crossings and splits the underlying surface at the iso-parameter midpoint of the hits, not a trim-loop split along the actual curve. No kernel face-split-by-curve.
- [partial] Merge contiguous tangent edges (MergeEdge / MergeAllEdges) — app `MergeEdgeCommand` (cmd_fillet.cpp:2303-2320) calls `ON_Brep::CombineContiguousEdges`. App calls into OpenNURBS directly; no kernel wrapper or test.
- [partial] Remove small / sliver edges (naked micro-edge removal with gap closure) — kernel `Brep::RemoveNakedMicroEdge` is limited to isolated naked edges whose neighbours are also naked. Related additions: `RemoveSliverFaces`/`RemoveDegenerateEdges` and `SewTJunctions`. Shared (2-trim) micro edges are still unsupported.
- [partial] Edge blend removal (remove fillet/chamfer faces and restore the sharp edge) — kernel `RemoveBlend` (fillet.h:1466, covering cylindrical `FilletConvexEdge`/`FilletConcaveEdge` faces and conical `FilletConvexEdgeTapered` faces), plus `RemoveChamfer` (fillet.h:1543) and `RemoveChamferVertex` (fillet.h:1593). Still partial: only inverts this kernel's own constructions on planar-plus-blend solids; throws for oblique-end cylindrical fillets and spherical vertex blends; no app command calls any of them.
- [partial] Untrim face / remove outer trim / remove hole loops — app `Op::Untrim`/`UntrimBorderOnly`/`UntrimHoles`; multi-face Untrim detaches the face from the polysurface instead of editing it in place. App-only.
- [partial] Move / copy / rotate / mirror a hole feature (feature-level local edit) — app `ApplyHoleXform` (cmd_solidtools.cpp:1027) re-subtracts the stored cutter mesh from the stored pre-cut mesh with a kernel mesh boolean. The result is a mesh; no B-rep feature edit.
- [partial] Shell / hollow body with face removal (offset-body local op) — kernel `ShellConvexPlanar` (scalar and per-face thickness; convex planar only, adjacent openings refused) and `ShellClosedSphere`/`Torus` (closed analytic shells only). App Shell is mesh-based.
- [partial] Re-intersect adjacent faces / rebuild edges after an edit (post-tweak edge regeneration) — app `RebuildEdgesReal` (cmd_fillet.cpp:2527) refits every 2-trim edge through the real surface-surface intersection of its faces. Kernel `ReplaceEdgeCurve` re-trims faces against a substitute curve; the new adjacency query API makes neighbour lookup reusable, but there is no automatic kernel-level re-intersection after a tweak.
- [partial] Extend a face/surface past its current boundary in place (ExtendSrf) — app `ExtendSrfCommand` (cmd_srfedit.cpp:674) offers Type=Smooth|Linear, and Linear calls the kernel `NurbsSurface::ExtendLinear`. On a single-face object the surface is replaced in place, but on a multi-face polysurface the old face is deleted (`DeleteFace`+`Compact`) and the extended surface added as a separate object via `AddBrepFrom` (cmd_srfedit.cpp:728-732), not extended in place with neighbours re-trimmed.
- [missing] Taper / draft face (rotate face about a neutral plane by draft angle) — a grep for taperface/draftface/rotateface/tiltface still finds nothing. Only creation-time draft exists (`Brep::ExtrudeTapered`, app `ExtrudeCrvTapered`).
- [missing] Replace face (swap a face's surface, re-trim it and its neighbours) — a grep finds nothing. Nearest are `Brep::ReplaceEdgeCurve` (an edge, not a face) and `SoftEditSrfCommand`, which writes a new surface into `m_S` directly.
- [partial] Imprint curve / face onto a body face (add edges without changing geometry) — **corrected: upgraded from missing.** Kernel `ImprintFaces(target, tool)` (boolean_general.h:61; boolean_general.cpp:3086) landed before this window and was already reflected under the sibling Boolean-operations category, but this category's own bullet was never updated to match and still claimed "a case-insensitive grep for imprint finds no hits anywhere" — false as of current HEAD. It splits `target`'s own faces wherever they cross a `tool` body's faces while keeping every fragment unconditionally (no ray-cast classification, no material ever removed), verified on a closed-loop fixture (box pierced by a cylinder) and an open-chain fixture (two overlapping boxes), each direction, plus a disjoint-operand no-op and a faceless-operand throw. Still partial: this is face-onto-face imprint only (no curve-onto-face imprint exists anywhere), it inherits `BooleanCombineGeneral`'s own scope limits (one crossing chain per opposing face pair, genus-0 faces, no self-crossing chains), only `target`'s faces are split per call, and no app command exposes it yet.
- [missing] Merge faces on the same non-planar surface (cylinder/tangent split faces) — `Brep::MergeCoplanarFaces` explicitly leaves a curved or merely-tangent (not coplanar) pair untouched; the app's `MergeFacesInto` returns -1 for non-planar faces.
- [missing] Push/pull a face (extrude face and merge/cut into its own body) — grep still finds no PushPull/PressPull command anywhere. Kernel `OffsetFace` calls itself "push/pull" in its own comment, but it re-extends existing neighbour faces rather than extruding new side walls and unioning/cutting them; it is scored under "Offset face" above, not here.
- [missing] Move a single B-rep vertex directly (drag one topological corner in place; adjacent edges reshape around it) — `TransformSubObjects`'s Brep branch (SubObjectEdit.cpp:547-556) still collects only `Face` and `Edge` refs and returns false otherwise. The kernel has only a query (`Brep::EdgesOfVertex`), no vertex-move op.

*Note on this category's counts: the table above shows 6 present / 17 partial / 5 missing (28 items total). This corrects a pre-existing arithmetic slip inherited from the last measurement (the table declared 17 partial against a physically-written bullet list that only ever had 16 gap bullets); combined with the `ImprintFaces` upgrade above (missing→partial), the internally-consistent result is 6/17/5.*

**kernel: Intersections & projections** (intersections):
- [partial] Analytic/analytic SSX closed forms (plane/plane, plane/cylinder, cylinder/cylinder, plane/sphere, cone, torus) — closed forms still exist only inside `BooleanCombineMixed`'s private splitters (`SplitCylindricalByObliquePlane`, `SplitCylindricalByParallelCylinder`, Steinmetz/unequal-cylinder splitters) and the planar boolean's plane/plane path. No public analytic-SSX API, and no plane/sphere, cone or torus closed form (the only general path is the mesh-seeded `IntersectSurfaces`).
- [missing] SSX tangent / grazing contact (surfaces touching along a point or curve) — `surface_intersect.h:72` still documents surfaces that "only touch tangentially" as empty results, and `TriTri` (surface_intersect.cpp:167-179) still returns false for parallel/coplanar triangle pairs (`if (dir.Length() < 1e-9) return false;`). There is no SSX tangency capability to give partial credit for (the only working tangency is curve/surface: a line tangent to a sphere gives 1 CSX hit, scored elsewhere).
- [partial] SSX across periodic seams and at singular points (poles) — seam handling is real (`SplitAtSeams`, `SeamCrossing` with a pinned-seam Newton solve, `FaceContainsUV`, surface_intersect.cpp:649,1010,1083). Poles have no dedicated singular-point treatment beyond a closest-point pole fix.
- [partial] SSX coincident / overlapping surface regions — `IntersectSurfaces` still returns nothing for coincident surfaces. The only coincidence handling is inside planar booleans.
- [missing] CSX against trimmed faces and curve-on-surface overlap (coincident) detection — `IntersectCurveSurface` still takes only an `ON_Surface`, no trim test; the app still throws the face away inside `IntersectAny`. No overlap detection anywhere. (Two exact-duplicate bullets from the pre-measurement map were merged into this one.)
- [partial] Curve self-intersection — still app-only and sampled (`same_curve` path, `CurveSelfIntersects`). The kernel's own `IntersectCurves(c, c)` is still not usable for this (spurious self-hits on a plain line).
- [partial] Curve/plane intersection — app `CurvePlaneHits` (sign change plus bisection). A caller can pass a bounded `ON_PlaneSurface` to `IntersectCurveSurface`, but there is no dedicated infinite-plane API.
- [partial] Plane sections / contours of surfaces and B-reps (Section, Contour, ClippingSections) — the app still slices render meshes (`SliceObjects`/`SliceMesh`). Kernel `SplitByPlane` is mesh-only; the exact route (`IntersectSurfaces` per face) is not used for sections.
- [partial] Mesh self-intersection detection — `Mesh::FindSelfIntersections`/`FindOffsetSelfIntersections` (mesh.cpp:3342 area). Still partial: overlapping coplanar triangles are never reported, any pair sharing a vertex is never examined, and it only detects — it does not repair.
- [partial] Surface / B-rep self-intersection detection — `Brep::Check()` reports `SelfIntersectingLoop` and `SelfIntersectingLoop3d` (brep.h:2524,2543; used in brep.cpp:6342,6349). Still partial: only face boundaries are checked, no face-interior self-intersection test and no face-vs-face crossing test within a B-rep; nearly-parallel close segments are excluded by design. (Two exact-duplicate bullets from the pre-measurement map were merged into this one.)
- [partial] Projection of curves/points onto surfaces along a direction (Project) — app `ProjectCommand` samples the curve and ray-casts along the CPlane normal onto the render mesh, then refits. No kernel project API.
- [partial] Pull curves/points to surfaces (closest-point projection) — kernel point projection is solid (`ClosestPointParameter`/`ClosestPoint`, `SurfaceClosestPointGlobal`), but there is no kernel "pull a curve into a curve-on-surface" API.
- [partial] Silhouette / outline curves — still app-only and mesh-based (`Silhouette`, render-mesh edges where adjacent face normals flip against the view vector). No kernel silhouette.
- [partial] B-rep/B-rep and curve/B-rep intersection as a kernel API — only face-level kernel entry points exist (`IntersectFaces`, `IntersectCurveSurface`); the app composes the B-rep loop itself (`IntersectAny`). `BooleanCombineGeneral` runs face-pair SSX internally, but there is no public Brep-level Intersect.
- [partial] Pullback of a 3D curve to surface parameter space (pcurve generation for arbitrary curves on a surface) — still no public pullback API; SSX produces pcurves as a by-product. The kernel builds real trims by pullback inside `Brep::ReplaceEdgeCurve` and `SplitNakedEdgeAt`, but both are internal to topology edits, not a general-purpose pullback call.
- [partial] Point-cloud contour/section as separate app commands (PointCloudContour/PointCloudSection) — app-level band-sampling around a plane; the kernel `PointCloud` has no section API.

*Note on this category's counts: 13 present / 14 partial / 2 missing (29 items). This corrects a pre-existing arithmetic slip: the table previously declared 11 present / 16 partial, which never matched the physically-written 16 gap bullets above (14 partial + 2 missing); present is 29 minus those 16, i.e. 13, not 11. No individual item's status changed — this is a transcription fix from when the document's own noted duplicate-bullet merge was applied to the table but not carried through consistently.*

**kernel: Healing, repair, validation, tolerant modeling** (healing):
- [partial] Tolerant sewing with edge splitting (partial-overlap edges, T-junctions, mismatched edge subdivision) — `Brep::SewTJunctions` (brep.h:2766-2801; brep.cpp:6779-6836) finds every T-junction among naked edges, splits the longer edge via `SplitNakedEdgeAt`, and finishes with `JoinNakedEdges`. Still partial: refuses every curved naked edge (`if (!a.IsLinear(tol)) continue;`, brep.cpp:6797); the app's own `JoinNakedEdges` does not call it; a latent bug re-confirmed by reading the current source — in the `for (int k = 0; k < 2; ++k)` inner loop (brep.cpp:6798-6821) the `break;` at line 6820 is unconditional, so if edge B's first endpoint (k=0) satisfies the on-line/strictly-interior test but `SplitNakedEdgeAt` then returns anything other than `Result::Ok`, the loop still breaks and B's second endpoint (k=1) is never tried in that pass.
- [partial] Geometric consistency validation (edge curve lies on adjacent surfaces, 2D trim vs 3D edge agreement, face/face self-intersection check) — `Brep::Check()` reports `EdgeVertexGap`, `TrimEdgeGap`, `LoopGap`, `InvalidTrim`, 2D/3D loop self-intersection, and (new this pass — see honesty note above) `NonManifoldVertex` pinch-point detection via union-find over each vertex's incident-edge face groups (brep.cpp:6279-6286, calling `GroupVertexEdgesByFace`, brep.cpp:6155-6166+). Its heal, `Brep::SplitNonManifoldVertex`/`SplitNonManifoldVertices` (brep.h:2662-2700; brep.cpp:6538-6612), disjoins a pinch point into one vertex per face-group, never deleting or `Compact()`ing; it returns `Result::Failed` (not a crash, but a real refusal) when one of the vertex's incident edges is closed on itself at that same vertex (brep.h:2681-2689's own doc comment calls this genuinely rare but explicitly unhandled), and it is not called anywhere in dino8-app. Neither addition is strong enough to flip this item to present. (The `Brep::Check()` DegenerateFace false-positive this bullet used to describe as "confirmed still present" is fixed, by `b1ac7c9` — see the top-of-document honesty note for why the prior pass's re-confirmation was itself stale, and for where the fix actually lives in the current source.) Remaining gaps: `TrimEdgeGap` compares only 3 samples at matching normalized parameters; no face/face self-intersection check between faces sharing no boundary.
- [partial] Gap closing by edge re-trim / trim refit (ReplaceEdgeCurve, RefitTrim, ReplaceEdge) — `Brep::ReplaceEdgeCurve` does closest-point re-projection of every trim, throwing when the fit fails; `CloseLoopGapsWithinTolerance` closes residual 2D loop gaps. No `RefitTrim` or general `ReplaceEdge`.
- [partial] Micro/sliver edge removal (RemoveAllNakedMicroEdges / Brep::RemoveNakedMicroEdge) — `Brep::RemoveNakedMicroEdge` works only on an isolated naked sliver whose neighbours are also naked. `Brep::RemoveDegenerateEdges` (brep.h:2660) collapses shared or naked edges at or below tolerance.
- [partial] Self-intersection detection (curves, meshes, surfaces/breps) — meshes: `Mesh::FindSelfIntersections`/`FindOffsetSelfIntersections`; breps: only loop boundaries via `Check()`'s `SelfIntersectingLoop`/`SelfIntersectingLoop3d`; curves: app-only sampled. No face-interior or face/face check anywhere.
- [partial] Edge merging (MergeEdge / MergeAllEdges via ON_Brep::CombineContiguousEdges) — app-only `MergeEdgeCommand`; no kernel wrapper.
- [partial] Edge rebuild from adjacent-surface intersection (RebuildEdges) — app-only `RebuildEdgesReal`.
- [partial] Curve/surface simplify and rebuild (Rebuild, FitCrv, SimplifyCrv, RemoveMultiKnot, MakeUniform, RebuildUV, FitSrf, ShrinkTrimmedSrf) — `NurbsSurface::Rebuild`, `RemoveKnotAt`, `NurbsCurve::FitLeastSquares` confirmed present.
- [partial] Analytic-form recognition / canonical simplification of faces — `IsPlanar`/`IsSphere`/`IsCylinder`/`IsCone`/`IsTorus` confirmed present; nothing replaces a recognized NURBS face with a canonical analytic one.
- [missing] Kinky / creased surface splitting into G1 faces (SplitKinkyFaces, CreaseSplitting for NURBS) — re-grepped `SplitKinky|CreaseSplit|kink` across kernel and app: only unrelated curve-continuity comments hit.
- [partial] Degenerate face removal (B-rep) — `Brep::RemoveDegenerateFaces` (brep.cpp:6510) trusts `Check()`'s `DegenerateFace` flag; that flag's false-positive hazard on the kernel's own primitives is fixed (`b1ac7c9`, re-verified this session — see the top-of-document honesty note and the topology-category mirror bullet above). Still partial for the same non-defect reason given there: delete-and-tolerant-join, not a geometric collapse.
- [partial] Sliver face removal (B-rep) — `Brep::RemoveSliverFaces` shares the same body (`RemoveThinFaces`, brep.cpp:6486); same fixed defect, same remaining delete-and-tolerant-join limitation.

**kernel: Mass properties & spatial queries** (massprops):
- [partial] Exact B-rep / NURBS-face mass properties (tolerance-controlled integration, no tessellation) — `Brep::Volume()`/`Area()` (brep.h:515-586; brep.cpp:308-465) integrate the divergence-theorem form with 5x5 Gauss-Legendre per knot span. The checked-in regression test (`TestBrepVolumeAndAreaMatchClosedForms`, test_basic.cpp:6449) only asserts `< 1e-4` relative for Sphere/Torus Volume and Area (lines 6467-6480) — both the Volume() header comment's "~1e-10 relative" claim and the Area() comment's separate "~1e-9 relative... a real measured bound, not a loose one" claim (brep.h:582-585) overstate the precision beyond what's actually tested; the integration code and quadrature order are unchanged since the last measurement, so this methodological point stands as previously noted. Still partial: fixed quadrature order, no caller tolerance; volume/area only; throws on any trimmed face (brep.cpp:444-448); throws unless tessellation is closed manifold.
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
- [partial] Split body by plane (both halves kept, capped) — `SplitByPlane` (boolean.h:46; boolean.cpp:174) is Manifold mesh half-space split, mesh-only.
- [partial] Split / trim body with a tool body (Rhino BooleanSplit with cutter objects, KeepAll) — **corrected: upgraded from missing.** The new `SplitByObjectCommand` (dino8-app/src/commands/cmd_boolean.cpp:290-386) is this item, read under Rhino's BooleanSplit/KeepAll framing rather than kernel: Feature operations' Parasolid PK_BODY_section framing of the same command: select target solids, then one or more cutting solids/open surfaces (an open cutter solidified via `SolidifyOpenCutter`, cmd_boolean.cpp:235-265, extruded along its own area-weighted average normal far enough to clear every target), multiple cutters unioned into one tool, and both `BooleanCombine(target, tool, Intersection)` and `BooleanCombine(target, tool, Difference)` kept per target when **both** are non-empty (cmd_boolean.cpp:357-368) — true KeepAll semantics, not a single-piece split. A same-day follow-up fix (commit 167baae, landed after this pass's own commit inventory was drawn up) hardened exactly this check: it used to accept "either half non-empty," which misfired on a cutter that simply missed the target (Difference alone comes back non-empty: the whole untouched target) or one that fully enclosed it (the mirror case), permanently deleting the user's cutting object and re-meshing the target for zero real effect; it now requires both halves non-empty and computes every result before touching the document, matching Rhino's own "no intersection found, nothing changed" contract — verified by reading the fix directly, not just its commit message. Still partial: mesh-boolean only (Manifold), the open-cutter solidify is a single-normal-direction approximation rather than a true trim, and there is still no kernel-level (B-rep or exact) equivalent — `SplitByPlane` remains the only kernel-native split, plane-only.
- [partial] Cut / split / trim with curve or surface cutters (WireCut by curve, Split surface by curve, Trim surface) — `WireCutCommand` (cmd_boolean.cpp:172) is a plane-cut of a mesh; kernel `Split` is parameter-only. Distinct from the tool-body item above (this is curve/surface cutters, not a solid tool body).
- [partial] Planar section / contour curves of bodies — `SectionCommand`/`ContourCommand` (cmd_curves2.cpp:999,1038), still mesh-slicing.
- [partial] Separate disconnected lumps / multi-lump bodies — `Brep::SplitDisjointPieces` (brep.cpp:2646-2676) explicitly throws when `original_face_count > 1` and any face has `m_li.Count() == 0`, i.e. any `Box()`/`Sphere()`/`FromSurface()`/`TrimmedPlanarFace()` result.
- [partial] Non-affine deformations (Twist/Bend/Taper/Stretch/Maelstrom/SoftMove, Flow, FlowAlongSrf, CageEdit, Splop) — `DeformObjects`/`PointMap` (cmd_meshtools.cpp:376-385), app-only.
- [partial] Associative / history-linked transforms — `SymmetryCommand`/`SymmetryLink` (cmd_curves2.cpp:2233; Document.h:431) keeps a live plane link; Copy/Array record no history.
- [partial] Construction history / associativity — `RecordHistory`/`UpdateHistory` app-only, scoped to five commands.

*Note on this category's count: 9 present / 13 partial / 0 missing (22 items) — one upgrade from the prior 9/12/1 (68.2%→70.5%), driven by the `SplitByObjectCommand` finding above.*

**kernel: Kernel-level data exchange** (exchange):
- [partial] .3dm attribute/metadata fidelity (layers, materials+textures, linetypes, named views, lights, clipping planes, layouts/details, units, user strings, point clouds, extrusions) — `Model::AddLayer`/`AddLinetype` (dino8-kernel/src/file_io.cpp:77,92) add named/coloured layers and dash/gap linetypes; every `Add*()` (file_io.cpp:108 onward) takes name/render_color/user_strings/linetype_index, round-tripped. Still no materials/textures/named views/lights/clipping planes/layouts/groups/units, and no read-side accessor apart from `raw()` (file_io.h:232). Confirmed defect still present: `dino8-kernel/tests/test_basic.cpp:5290` asserts `attributes->m_layer_index == 0` for an object left on the default layer — the true OpenNURBS default layer index is -1, so this passes for the wrong reason (first `AddLayer()` call takes index 0). The app side (dino8-app/src/io/File3dm.cpp) is much broader but is app code, not kernel API.
- [missing] Rhino non-geometry/composite objects in .3dm: block instances, annotations, hatches, text dots — the read cast chain (dino8-app/src/io/File3dm.cpp:554-614) handles only `ON_Point`/`ON_Curve`/`ON_Brep`/`ON_Surface`/`ON_Mesh`/`ON_SubD`/`ON_Extrusion`/`ON_PointCloud`; everything else is skipped.
- [partial] .3dm archive version targeting — `Model::Save(path, int version=0)` (file_io.cpp:158) passes version straight to `ONX_Model::Write`, but the app's actual writer (dino8-app/src/io/File3dm.cpp:1344) bypasses kernel `Model` entirely and hardcodes `model.Write(path.c_str(), 0, &log)`.
- [partial] STEP AP203/AP214 B-rep export — app-only (`ExportStep`, dino8-app/src/io/FileIgesStep.cpp:1837), writes `FILE_SCHEMA('AUTOMOTIVE_DESIGN...')` (line 1657, AP214 only), `MANIFOLD_SOLID_BREP`/`SHELL_BASED_SURFACE_MODEL` (line 1916); no AP203 option; no kernel STEP code.
- [partial] STEP B-rep import — app-only (`ImportStep`, FileIgesStep.cpp:2549), broad but incomplete; no kernel code.
- [missing] STEP AP242 — confirmed zero hits for TESSELLATED/TRIANGULATED_FACE/PMI/AP242 anywhere in dino8-app/src/io/*.cpp; only the AP214 schema string exists.
- [partial] IGES import — app-only (`ImportIges`, FileIgesStep.cpp:1339).
- [missing] Parasolid XT (.x_t/.x_b) read/write — **permanently out of scope by project policy** (proprietary format + licensed Siemens SDK); documented as such. See "Infeasible / non-engineering" below.
- [missing] ACIS SAT/SAB read/write — **permanently out of scope by project policy** (proprietary format + licensed Spatial SDK); documented as such. See "Infeasible / non-engineering" below.
- [partial] OBJ read — `Mesh::LoadObj` (dino8-kernel/src/mesh.cpp:1021) rejects >4-index faces / negative indices, per-vertex-only UV storage (line ~1034).
- [partial] PLY read/write — kernel `Mesh::SavePly`/`LoadPly` (mesh.cpp:1490,1570) write/read ASCII or binary LE; binary_big_endian explicitly rejected as "out of scope" (mesh.cpp:1410,1435). The app's own separate PLY code (dino8-app/src/io/FileExchange.cpp) still hardcodes `"format ascii 1.0"` on export (line 2547) while its importer accepts ascii/binary_little_endian/binary_big_endian (lines 2634-2636) — the two PLY paths remain unwired to each other.
- [missing] Other mesh/scene exchange formats (glTF/GLB, 3MF, FBX, Collada, VRML/X3D, AMF, OFF, SketchUp SKP, USD) — zero hits for any of these formats anywhere in the source.
- [partial] Point-cloud/scan formats — `PointCloud::SaveXyz`/`LoadXyz` (dino8-kernel/src/point_cloud.cpp:77,95); still no .pts/.e57/.las, colours deliberately unwritten, not wired into the app.
- [partial] Unit-system conversion — app-only (.3dm at File3dm.cpp:873-997; IGES). STEP export still hardcodes `int units = 4; // millimetres` (dino8-app/src/io/FileExchange.cpp:521). Kernel `Model` never sets units.
- [partial] Import-time B-rep validation/healing — app `OrientFaces`/`JoinEdges` only appear in FileIgesStep.cpp. Zero call sites of `SewTJunctions`/`SplitNakedEdgeAt` anywhere in dino8-app/src — no importer calls them.
- [partial] App-independent (kernel library) STEP/IGES/PLY/DXF exchange API — PLY has a kernel API; STEP/IGES/DXF (dino8-app/src/io/FileExchange.cpp: `ExportDxf`/`ImportDxf`, lines 485/1473) and DWG (`ImportDwg`, ~line 1590) remain app-only Document entry points.
- [missing] IFC — zero hits for "IFC" in dino8-app/src or dino8-kernel/src.
- [missing] DWF/DWFx — zero hits; not in kModelExts/kExportExts (cmd_file.cpp:25-26).
- [missing] JT (ISO 14306) — zero hits anywhere.

**kernel: Feature operations** (features):
- [partial] Counterbore (stepped coaxial) hole — no "counterbore" hit anywhere in dino8-app/src or dino8-kernel/src; still composable-in-principle only.
- [partial] Countersink (conical) hole — no "countersink" hit anywhere; no dedicated feature/command.
- [missing] Threaded/tapped hole and external thread feature — `Bolt`/`Nut` (dino8-app/src/commands/cmd_arch.cpp:623) explicitly comments "built solid, with no threaded bore"; no thread-geometry code found.
- [partial] Revolved cut (RevolvedHole) — `RevolvedHole` (cmd_solidtools.cpp:906) still prints "(mesh boolean; results are meshes)"; kernel `Brep::Revolve` exists but is unused by this command.
- [missing] Emboss/deboss — zero hits for emboss/deboss/engrave anywhere.
- [partial] Lettering as solid geometry — `TextCommand` (dino8-app/src/commands/cmd_annotate.cpp:48,66) still only takes a bool `surfaces_` flag (Curves/Surfaces), no Solids/Thickness option.
- [partial] Feature editing/re-execution — `ApplyHoleXform` (cmd_solidtools.cpp:1027) still replays the boolean from a stored pre-cut mesh; no parametric feature tree.
- [partial] Draft angle on extrusions — `Brep::ExtrudeTapered` (dino8-kernel/src/sweep.cpp:1430) refuses oblique draft directions (lines 1448-1452), exact for line/circle/arc/convex-polyline profiles, approximate otherwise; app's `ExtrudeCrvTapered` (cmd_surface.cpp:1229) still uses its own centroid-scaling path, not the kernel one.
- [missing] Draft/taper faces of an existing body about a neutral plane — only draft analysis exists; `OffsetFace` translates, does not taper.
- [partial] Thicken a sheet body into a solid — `Mesh::Thicken` walls an offset copy; no B-rep sheet thicken.
- [partial] Split body with an arbitrary surface/solid cutter — `SplitByObjectCommand` (dino8-app/src/commands/cmd_boolean.cpp:290-386), `SolidifyOpenCutter` (lines 235-265). This same command is also credited under kernel: Transformations, patterns, splitting's "tool body split / KeepAll" item (Rhino framing of the identical capability), which has the detail on a same-day correctness fix (commit 167baae) to its "no real split" detection. Still partial: single-normal-direction approximation for open cutters, no face-by-face imprinting/healing, mesh boolean via `kernel::BooleanCombine(Mesh, Mesh, ...)` (dino8-kernel/include/dino8/kernel/boolean.h:30).
- [partial] Body sectioning — `SectionCommand`/`ContourCommand` (dino8-app/src/commands/cmd_curves2.cpp:1038,999) still mesh-slice-based.
- [partial] Delete face and heal/remove feature — `RemoveBlend`/`RemoveChamfer`/`RemoveChamferVertex` (dino8-kernel/src/fillet.cpp:3567-4167) have zero dependency on `Brep::Check()` or `RemoveDegenerateFaces`, so the Check() DegenerateFace-false-flag defect does not touch this item. Still partial for the reasons already given (spherical vertex blends and oblique-end cylinders refused; app's `DeleteFaces` leaves an open polysurface).
- [partial] Feature recognition — analytic classification plus `RemoveChamfer`/`RemoveChamferVertex`'s geometric recognition exist; still no hole/boss/pocket recognition.
- [missing] Sheet-metal features — no code; `UnrollDevelopable` (dino8-kernel/src/surface_edit.cpp:817) is single-surface unrolling only.
- [missing] Lattice/cellular infill — the only "lattice" hits are the unrelated Cage FFD deformer (dino8-app/src/commands/cmd_solidtools.cpp:1591-1780); no gyroid/TPMS/Voronoi infill code.
- [partial] Blind/through hole with depth/placement — `RoundHole`/`MakeHole`/`PlaceHole` (cmd_solidtools.cpp:793,833,873) all still print "(mesh boolean; results are meshes)".
- [partial] Boss following a curved surface — `BossRibCommand` (dino8-app/src/commands/cmd_srfedit.cpp:2009) still a mesh-boolean union path.
- [partial] Rib — same `BossRibCommand` path (cmd_srfedit.cpp:2011).

**Fossilith kernel — Curve operations** (curveops):
- [partial] Curve fairing/smoothing — app-only Laplacian smoothing (dino8-app/src/commands/cmd_meshtools.cpp:740 / cmd_remaining.cpp:866); no kernel fairing.
- [partial] Match curve end continuity — `MatchCommand` (dino8-app/src/commands/cmd_curves2.cpp:1500), position/tangent only, app-only.
- [partial] Offset curve — `NurbsCurve::OffsetInPlane` (dino8-kernel/src/curve.cpp:793) exact for lines/arcs, least-squares refit otherwise; app's `OffsetCommand` (cmd_edit.cpp:100) still has its own special cases, doesn't call the kernel.
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
- [missing] Convert to Beziers (surface) — `ConvertToBeziers` (cmd_curves2.cpp:2451-2466) curves-only; no surface decomposition anywhere.
- [partial] Patch — `Patch`/`BuildCoonsPatch` app helper (cmd_srfedit.cpp:1775) is planar-patch-only; the exact kernel `CoonsPatch` (surface_edit.cpp:953) is wired into `NetworkSrf`/`EdgeSrf` (cmd_surface.cpp:593-602), not `Patch`.
- [partial] Make periodic (surface) — `NurbsSurface::MakePeriodicExact` (dino8-kernel/src/surface.cpp:743) re-knots via `NurbsCurve::MakePeriodicExact` (curve.cpp:672); app's command still curves-only.
- [partial] SrfSeam — `SrfSeamCommand` (dino8-app/src/commands/cmd_srfedit.cpp:1382) standalone-surfaces-only.
- [partial] SrfSeam test-coverage caveat — only message-string coverage.
- [partial] Kernel Rebuild() unused by app — `NurbsSurface::Rebuild` (surface_edit.cpp:441) exists/tested but no app command calls it.
- [partial] Kernel MatchEdge() G0/G1/G2 — wired into `MatchSrfCommand` for G0/G1 only (cmd_fillet.cpp:1509); G2 still unreachable from any command.
- [partial] Surface from 2-4 edge curves (EdgeSrf/NetworkSrf) — `CoonsPatch` (surface_edit.cpp:953) exact for the 4-curve case only; 2/3-curve and CoonsPatch-failure cases fall back to sample-and-refit.

**Kernel: SubD & mesh kernel support** (subd_mesh):
- [partial] SubD -> NURBS patch conversion — `ToNurbsPatches`/`ToNurbsPatchesAdaptive` (dino8-kernel/src/subd.cpp:443,859); app's ToNURBS (dino8-app/src/commands/cmd_solids.cpp:802,843) still calls only the non-adaptive `ToNurbsPatches`. No dependency on `Brep::Check()`/`RemoveDegenerateFaces` found in subd.cpp — the DegenerateFace false-flag defect does not touch this item.
- [missing] Kernel-native SubD local edit operators (insert edge, extrude face, spin edge, weld, expand) — zero hits for these operators anywhere in dino8-kernel/src/subd.cpp or its header; still app-only.
- [missing] SubD boolean operations — zero "SubD" references in any dino8-kernel/src/boolean*.cpp file.
- [partial] SubD from NURBS/B-rep conversion — `SubD::FromNurbsSurface` (subd.cpp:22); single-surface, sample-based, unwired from the app.
- [partial] SubD symmetry/mirror-in-place — `SubD::Transform` (subd.cpp:83) accepts a mirror `ON_Xform`; no flip/weld/live-constraint code found alongside it.
- [partial] SubD non-manifold/multi-body validity checks — `SubD::IsValid` (subd.cpp:95) a thin bool wrapper over `ON_SubD::IsValid`.
- [partial] SubD display-level control at kernel level — `EvaluateFace`/`ToNurbsPatchesAdaptive` (subd.cpp:732,859) present; no single tessellate(tolerance)/view-dependent API.
- [missing] Quad-remeshing into a clean SubD-ready cage — `QuadRemeshAction` (dino8-app/src/commands/cmd_remesh.cpp:249) app-only; no kernel quad-dominant remesher.
- [partial] SubD extraordinary-vertex limit-tangent quality — `EvaluateFace` (subd.cpp:732) exact away from the extraordinary quadrant; zero-vector tangent fallback at the pole itself unchanged (no eigenbasis code found in subd.cpp).

## App: Dino 8 vs Rhino 8 + AutoCAD 2027

| Category | Weight | Items | Present | Partial | Missing | Parity % |
|---|---|---|---|---|---|---|
| Dino 8: Command system & core commands | 1.5 | 19 | 11 | 6 | 2 | 73.7% |
| Dino 8: 2D drafting, annotation & documentation | 1.0 | 18 | 12 | 5 | 1 | 80.6% |
| Dino 8: Viewport display, rendering & visualization | 1.0 | 18 | 13 | 3 | 2 | 80.6% |
| Dino 8: Scripting, automation & visual programming | 1.0 | 15 | 10 | 3 | 2 | 76.7% |
| Dino 8: File I/O & interoperability (app level) | 1.0 | 17 | 5 | 5 | 7 | 44.1% |
| Dino 8: SubD & mesh modeling toolset (app level) | 0.75 | 24 | 19 | 3 | 2 | 85.4% |
| Dino 8: UI/UX, accessibility & localization | 1.0 | 19 | 13 | 2 | 4 | 73.7% |
| Dino 8: Ecosystem, trust, cloud/AI & platform reach | 0.5 | 16 | 7 | 1 | 8 | 46.9% |

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

### App category gaps (missing / partial items, with evidence)

**Dino 8: Command system & core commands** (app_commands):
- [partial] Command aliases and shortcut customization — Rhino's default aliases are built in and users can add aliases through the Alias command or panel, but user aliases live only in an in-memory map with no persistence to disk, and there is no user-assignable keyboard-shortcut table.
- [partial] Surface construction commands — Loft, Revolve, Extrude, EdgeSrf, Sweep1, Sweep2 and NetworkSrf all exist, but Patch is "planar patch only", Sweep1/Sweep2 (`dino8-app/src/commands/cmd_surface.cpp:402` `Sweep1Command`, registered at :1413 as "Approximated by a lofted sweep... rotation-minimizing frames") are still RMF-lofted mesh/NURBS fits with no tolerance control and do not call the kernel's exact `Brep::Sweep1` (no `twist_total` or kernel `Sweep1` reference anywhere under `dino8-app/src/`), and NetworkSrf's 3-curve case still falls back to a fitted bilinear Coons patch. The 4-curve case uses the kernel's exact `CoonsPatch`, and ExtendSrf has a Linear type via the kernel's exact `ExtendLinear`.
- [partial] Solid editing with B-rep results (booleans, fillet, shell, offset) — BooleanUnion/Difference/Intersection convert every operand to a mesh before combining (`dino8-app/src/commands/cmd_boolean.cpp:1`, "Boolean and splitting commands (mesh-based, via Manifold)"); the kernel's exact `BooleanCombineMixed` is never called from the app. FilletEdge is exact only when both adjacent faces are planar, else a mesh fallback. Shell, OffsetSrf on polysurfaces, and Pipe Cap=Yes all give meshes. New this window: `SplitByObjectCommand` (`cmd_boolean.cpp:290`) adds a general cutting-solid/surface split, but it is the same mesh-boolean pattern — its own status line literally prints "mesh boolean; results are meshes" (`cmd_boolean.cpp:386`) — so it reinforces rather than changes this bullet's partial status. (A same-day follow-up, commit 167baae, fixed the command silently deleting its cutter and re-meshing the target with zero real effect when the cutter merely missed or fully enclosed the target — a correctness fix, not a capability change; see kernel: Transformations for detail.) Solid primitives themselves are real Breps.
- [partial] History / associative re-execution — real History/RecordHistory/UpdateHistory exist (`cmd_history.cpp`, `cmd_misc.cpp`, `cmd_solids.cpp`), but only for Extrude, ExtrudeCrvToPoint, Revolve, Loft and SubDLoft; stale stubs elsewhere in the app still print "no construction history is recorded" and are asserted on by a smoke test, contradicting the real mechanism.
- [partial] Command-level feature editing (re-running a construction with new inputs) — only scoped, explicit-recompute mechanisms exist (UpdateHistory, hole features, a few UpdateDimensions/UpdateBakes commands); no universal parametric feature tree.
- [partial] VBA-style macro recorder and editor — a macro editor does exist (`dino8-app/src/ui/Panels.cpp:1374` `DrawMacroEditor`, a multi-line panel with Run/Copy, `;`-separated command sequences, command-file playback, plus a Lua/Python script editor). Still missing: any action recorder, any VBA/object-model compatibility, and persistence (the macro buffer is in-memory only).
- [missing] AutoLISP-equivalent command scripting language — no LISP dialect or AutoLISP compatibility anywhere; scripting is Lua, Python or Macro/command files.
- [missing] ObjectARX-equivalent native extension API — the only native API is a Dino-specific plugin ABI, not an ObjectARX-compatible binary interface.

**Dino 8: 2D drafting, annotation & documentation** (app_drafting):
- [partial] Associative annotation updating (dimensions, leaders, center marks, center lines) — UpdateDimensions rebuilds several dimension/leader/mark types from their anchors, but only on an explicit command run, and a point is anchored only if it coincides exactly with a Point object or curve endpoint.
- [partial] Print and plot output — Print writes a vector PDF/SVG of the active view with an optional scale, but there are no lineweights, no print widths, and no plot styles (CTB/STB); no printer-device output.
- [partial] Dynamic blocks — only visibility states exist (BlockAddState/BlockSetVisibility); stretch, flip, array and lookup parameters and actions are not attempted.
- [partial] Live external data linking into tables — a two-way CSV sync with conflict refusal, not native .xlsx; formula cells come back as their last saved values. Commit 19c14a0 (`cmd_drafting2.cpp:233`) added `std::ios::binary` to `WriteCsvFile`'s and `BillOfMaterials::Run`'s ofstream opens — verified this is purely a Windows CRLF-translation fix (a no-op on Linux/macOS) so CSV bytes match across platforms; it does not touch the CSV-vs-.xlsx or frozen-formula-cell limitations, so the score and reasoning are unchanged.
- [partial] Dimension styles — named styles do exist (AnnotationStyles etc., persisted in .3dm user strings), but a style has only name, text_height, arrow_size and font — no units/precision, tolerance, extension-line or text-placement control, and text/dimension styles share one table.
- [missing] Field text (text driven by object properties) — no field or formula text type found anywhere; all text is static baked geometry.

**Dino 8: Viewport display, rendering & visualization** (app_display):
- [partial] Environments and image-based lighting — **materially updated by commit 7059e20.** `PathTracer::SkyColor()` (`dino8-app/src/render/PathTracer.cpp:241-113`) now has a real `Background::Image` branch: a standard equirectangular (atan2/acos) lookup through a new shared `PathTracer::SampleBilinear` helper. Critically, `SkyColor()` is called from inside `TracePath`'s bounce loop (`PathTracer.cpp:435-452`, `radiance += Mul(throughput, SkyColor(dir))`) for any ray that escapes the scene at any bounce depth, not just primary camera rays — so this is genuine image-based lighting/reflection contribution (a ray that bounces off a glossy/reflective surface and then misses geometry now picks up the environment image, weighted by accumulated `throughput`) for the offline CPU path-traced renders (`Render`/`RenderPreview`/`RenderArctic`/`RenderBlowup` at `Quality=Raytraced`). This closes the gap for that one rendering surface. It remains partial because two of the app's three render surfaces still lack it: the interactive rasterizer viewport still draws the image only as a stretched full-viewport quad with no reflection contribution (`dino8-app/src/viewport/Viewport.cpp`, `DrawBackgroundImage`), and the live `RayTracedViewport` GPU preview still falls back to a solid color for `Image` (`dino8-app/src/render/GpuRaytracer.cpp:683`, `bg_mode_ = 0; // no env-map sampling on GPU`). There is also still no HDRI lighting or `.hdr`/`.exr` loader — `LoadImageFile` (`dino8-app/src/render/ImageIO.cpp:475`) supports only BMP/PPM/PGM/PNG (8-bit LDR), so an environment image can only ever be an LDR backdrop, never true HDR-range lighting.
- [partial] Per-object display mode override — only Wireframe and Shaded are supported per-object; every other mode is viewport-wide only.
- [partial] View-dependent adaptive tessellation — real frustum culling exists, but there is still no LOD and no re-tessellation on zoom.
- [missing] Real-time shadow maps in the rasterized renderer — `dino8-app/src/render/GlRenderer.cpp` has only ground-plane contact-shadow "blobs" (`ShadowBlob`, `kMaxShadowBlobs`, lines ~169-175, ~554-641) — a screen-space blob fade, not shadow maps, self-shadowing, or object-on-object cast shadows. Real cast shadows appear only in the GPU raytraced and CPU path-traced modes (`ground_.shadows`, `PathTracer.cpp:178,338`).
- [missing] SSAO in the rasterized renderer — no "ssao"/"ambient occlusion" hit anywhere in `dino8-app/src/render/`.

**Dino 8: Scripting, automation & visual programming** (app_scripting):
- [partial] Embedded Python 3 — `dino8-app/CMakeLists.txt:146` sets `option(DINO8_ENABLE_PYTHON ... OFF)` on Windows specifically, `:148` `ON` elsewhere; shipped Windows builds have no Python at all; mid-script prompts are also missing.
- [partial] Python API breadth — `RunCommand` reaches every registered command; the real gap is the object model (56 bindings versus Lua's 160 `rs.*` functions) and no interactive prompts.
- [partial] Headless/batch scripting mode — `dino8-app/src/main.cpp:5-7,322-327`: `--smoke N --script FILE [--screenshot]` is real and documented in the file's own header comments; still framed as a QA mode needing a GL context/display server, not a supported batch product.
- [missing] Cloud/network compute service (Rhino.Compute equivalent) — no server/socket/HTTP code anywhere in the source.
- [missing] AI-assisted modeling or scripting — no neural/inference code anywhere; the one "smart" feature explicitly documents its own technique as not machine learning.

**Dino 8: File I/O & interoperability (app level)** (app_interop):
- [partial] Native .3dm read/write — the reader converts only lights, clipping planes, detail views, points, curves, Breps, surfaces, meshes, SubDs, extrusions and point clouds — everything else is silently skipped on open. Dino-written annotations/blocks survive only as baked geometry plus private user-string metadata.
- [partial] OBJ — the importer loads the whole file as one mesh with no per-group/per-object split and no .mtl; the exporter tessellates and merges everything into a single welded mesh, losing object identity and writing no materials or curves.
- [partial] STEP AP203/AP214 — the writer and reader exist for basic B-rep entities, but the reader has no assembly structure at all (no NEXT_ASSEMBLY/MAPPED_ITEM/context handling), so multi-part assemblies lose their part placement transforms. AP242 is still entirely absent (zero hits for TESSELLATED/TRIANGULATED_FACE/PMI/AP242), scored as its own separate missing item below.
- [partial] DXF — the writer path (`WriteDxfPolyline`/`WriteDxfSpline`/`WriteDxfCurve`/`WriteDxfMesh`, dino8-app/src/io/FileExchange.cpp:304-604) covers only polylines, splines/curves, lines/circles/arcs and 3dfaces; no TEXT/MTEXT/DIMENSION/HATCH/INSERT writer function exists anywhere in that file. The reader covers TEXT/MTEXT/ELLIPSE/SPLINE/POLYLINE/3DFACE/HATCH/DIMENSION.
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
- [partial] Breadth of localization (10+ languages, professional review) — a fresh key-count check found `en.json` has 183 flattened keys, `fr.json` has 178 (`panel.activity_log`, `panel.block_manager`, `panel.uv_editor`, `panel.mapping_widget`, `panel.whats_new` still missing); only Spanish and French exist beside English.
- [partial] Worksessions (shared multi-file referencing) — a real Worksession mechanism exists (`dino8-app/src/session/Worksession.h`/`.cpp`), attaching other .3dm files as locked reference models with filtering and a saved JSON session file. Attached objects are copied in with no live link or refresh.
- [missing] Screen-reader support — still explicitly documented as not implemented; the UI toolkit exposes no platform accessibility tree. (Infeasible — see below.)
- [missing] Localized command and toolbar help text — the ~1055 command names/help texts and toolbar tooltips remain English-only in every language.
- [missing] Video tutorials / community forum — needs an audience and hosting, not source-tree work. (Infeasible — see below.)
- [missing] Real-time multi-user collaborative editing — single-document, single-user desktop app; no network code found anywhere.

**Dino 8: Ecosystem, trust, cloud/AI & platform reach** (app_ecosystem):
- [partial] Large-scale adversarial/property-based QA — a real fuzz-test ctest target exists (`dino8-app/CMakeLists.txt:495-507`, `add_test(NAME dino8_fuzz_geometry ...)`), plus several adversarial scripts, but this does not substitute for decades of real user files, and Windows-only numeric-difference issues are still being worked through.
- [missing] Real AI/ML-based modeling assistance — no inference code anywhere; the one "smart" clustering feature explicitly documents itself as not machine learning.
- [missing] Hosted cloud compute / geometry-as-a-service (Rhino Compute equivalent) — no server or network code found. (Infeasible — see below.)
- [missing] Cloud model viewer / app builder (ShapeDiver equivalent) — no web-viewer or embed code exists.
- [missing] Touch-first companion app (Rhino for iPad equivalent) — desktop only; a separate product, not a feature of this app. (Infeasible — see below.)
- [missing] Code-signed / notarized installers — the signing CI steps only run if a certificate secret is set, and no certificate has been purchased. (Infeasible — see below.)
- [missing] Plugin marketplace / discovery mechanism — not attempted. (Infeasible — see below.)
- [missing] Third-party plugin ecosystem (real external adoption) — four first-party example plugins exist and no third-party plugins; a network-effect gap, not an engineering one. (Infeasible — see below.)
- [missing] Real-time multi-user collaboration / co-editing — same evidence as the app_ux item; no network code anywhere.

## Infeasible / non-engineering

These 12 items still cannot be closed by writing more code in this
repository — none of the commits in this window changed that, and none of
the five verification passes found reason to move any of them off this list.

- **[app/app_interop] Parasolid (.x_t/.x_b) import/export** (missing) — infeasible: Parasolid's format is proprietary and undocumented outside a licensed Siemens SDK.
- **[app/app_interop] ACIS (.sat/.sab) import/export** (missing) — infeasible: same proprietary-format rationale as Parasolid.
- **[app/app_ux] Screen-reader support** (missing) — infeasible without replacing the entire UI toolkit (Dear ImGui), a framework-migration-scale undertaking.
- **[app/app_ux] Video tutorials / community forum** (missing) — infeasible for a codebase alone to provide; requires an actual user community and hosting operation.
- **[kernel/exchange] Parasolid XT (.x_t/.x_b) read/write** (missing) — proprietary format + SDK licence (Siemens).
- **[kernel/exchange] ACIS SAT/SAB read/write** (missing) — proprietary format + SDK licence (Spatial).
- **[app/app_ecosystem] Hosted cloud compute / geometry-as-a-service (Rhino Compute equivalent)** (missing) — infeasible from source code alone: requires standing up and operating server infrastructure.
- **[app/app_ecosystem] Touch-first companion app (Rhino for iPad equivalent)** (missing) — infeasible: a distinct mobile product with its own distribution and touch-first UI.
- **[app/app_ecosystem] Code-signed / notarized installers** (missing) — infeasible for engineering alone: requires a purchased certificate and legal-entity registration.
- **[app/app_ecosystem] Plugin marketplace / discovery mechanism** (missing) — infeasible to close by engineering alone; needs third-party adoption over time.
- **[app/app_ecosystem] Third-party plugin ecosystem (real external adoption)** (missing) — infeasible: a network-effect gap, not an engineering gap.
- **[app/app_ux] Breadth of localization (10+ languages, professional review)** (partial) — infeasible at full Rhino-matching breadth within an engineering-only pass, though the infrastructure itself is complete.

## Ranked closeable backlog

335 non-present items were found across all 25 categories (279 kernel, 56
app); the same 12 above are infeasible for engineering alone to close and are
excluded from this ranking. The remaining 323 are ranked by
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
| 2 | kernel | booleans | Face-face imprint (Parasolid PK_BODY_imprint / ACIS imprint) | missing | medium | The general boolean engine's internal face-splitting already exists; needs exposing as its own operation. |
| 3 | kernel | booleans | Sheet/solid trim (open surface as cutter through a solid) | missing | medium | Closes a real, verified gap in kernel Boolean operations. |
| 4 | kernel | booleans | AutoCAD-style INTERFERE (real overlap solids, not just Clash report) | partial | small | `ComputeInterference` (dino8-kernel/src/boolean.cpp) now builds the real pairwise overlap solids; remaining work is wiring it into an app `Interfere` command and, optionally, true N-way simultaneous overlap reporting. |
| 5 | kernel | blending | Conic / rho (chordal, elliptical) blend cross-sections | missing | medium | Closes a real, verified gap in Blending & chamfering. |
| 6 | kernel | blending | Alternative blend rail types (distance-from-edge, distance-between-rails) | missing | medium | Closes a real, verified gap in Blending & chamfering. |
| 7 | kernel | topology | ~~Sliver / degenerate micro-face removal — fix the Check() false-positive first~~ **fixed** | partial | small | Done in `b1ac7c9` (before this pass): `Brep::Check()` no longer auto-flags a loop-less face or under-samples a curved-wall trim; `TestBrepCheckDoesNotFalselyFlagCurvedOrToplessValidFaces` covers Box()/Sphere()/Torus()/Extrude()/Revolve(). Kept in the table (not renumbered away) only so this row's own history is traceable; not an active priority. Still partial for the same non-defect reasons item 207 above gives (delete-and-tolerant-join, not a geometric collapse; T-junction slivers left naked). |
| 8 | kernel | healing | ~~Degenerate face removal (B-rep) — same Check() false-positive root cause~~ **fixed** | partial | small | Same fix as #7, `b1ac7c9`; now verified end-to-end (not just at the `Check()` level) by `TestBrepRemoveDegenerateOrSliverFacesDoesNotTouchValidSolids`, added this pass — `RemoveDegenerateFaces()` removes 0 faces from Box()/Extrude(circle) while still removing a genuine hairline sliver. |
| 9 | kernel | healing | ~~Sliver face removal (B-rep) — same Check() false-positive root cause~~ **fixed** | partial | small | Same fix as #7/#8, same new end-to-end test covers `RemoveSliverFaces()` too. |
| 10 | kernel | exchange | .3dm layer round-trip default-layer index fix | partial | small | `AddLayer()`'s first call returns manifest index 0 instead of true default (-1); a one-line semantic fix. |
| 11 | app | app_commands | AutoLISP-equivalent lightweight command-scripting language | missing | large | Closes a real, verified gap in Dino 8 Command system & core commands. |
| 12 | app | app_commands | ObjectARX-equivalent low-level native app-extension API | missing | large | Closes a real, verified gap in Dino 8 Command system & core commands. |
| 13 | kernel | localops | Imprint curve / face onto a body face | missing | medium | No implementation anywhere; a genuinely useful direct-edit primitive. |
| 14 | kernel | localops | Merge faces on the same non-planar surface (cylinder/tangent split faces) | missing | medium | `MergeCoplanarFaces` explicitly excludes this case; needs a curved-surface variant. |
| 15 | kernel | localops | Push/pull a face (extrude face and merge/cut into its own body) | missing | large | Still absent from the catalogue and code entirely. |
| 16 | kernel | localops | Move a single B-rep vertex directly | missing | medium | The Brep sub-object-edit path currently only handles Face and Edge refs. |
| 17 | kernel | localops | Taper / draft face (rotate face about a neutral plane) | missing | medium | Only creation-time draft exists; a direct-edit taper is a distinct, useful operation. |
| 18 | kernel | localops | Replace face (swap a face's surface, re-trim neighbours) | missing | medium | No implementation anywhere. |
| 19 | kernel | sweeplofts | Extrude to a boundary surface / body (ToBoundary, PressPull) | missing | large | Catalogued as an option string but never implemented. |
| 20 | kernel | sweeplofts | Sweep controls: twist along path, scale along path, road-like alignment | missing | large | Neither the kernel Sweep1 nor the app's own command has any of these. |
| 21 | kernel | sweeplofts | ExtrudeCrv / Revolve producing a SubD object directly | missing | medium | Catalogued option, no implementation. |
| 22 | kernel | sweeplofts | SubD-result revolve / multi-pipe menu entries are broken references | missing | small | Either implement the two commands or remove the dead menu entries — either is quick. |
| 23 | kernel | topology | Euler operators (MEV/MEF/KEV/KEF/KEMR/MEKR etc.) | missing | large | A foundational topology primitive family, entirely absent. |
| 24 | kernel | topology | Wire bodies (edge/vertex-only B-rep body) | missing | large | No wire-body concept exists anywhere in the topology model. |
| 25 | kernel | topology | Persistent naming / topology identity across edits | missing | large | Every topology edit renumbers via Compact(); this is an architectural change. |
| 26 | kernel | offsetshell | Inset on raw mesh or polysurface objects (as opposed to SubD) | missing | medium | Inset currently rejects every non-SubD target outright. |
| 27 | kernel | features | Threaded / tapped hole and external thread feature | missing | large | Bolt/Nut are built solid with no thread geometry at all. |
| 28 | kernel | features | Emboss / deboss a closed region onto a face | missing | large | No Emboss/Deboss/Engrave command or API exists anywhere. |
| 29 | kernel | features | Draft / taper faces of an existing body about a neutral plane | missing | medium | Only draft analysis/marking exists; OffsetFace translates but does not taper. |
| 30 | kernel | features | Split body with an arbitrary surface / solid cutter | partial | large | `SplitByObjectCommand` (dino8-app/src/commands/cmd_boolean.cpp) now closes the general cutting-object case; still a single-normal-direction mesh-boolean approximation, not a true PK_BODY_section-style trim. |
| 31 | kernel | features | Sheet-metal features (Bend, Unfold, Flange, Hem, Tab) | missing | large | No code; UnrollDevelopable is a single-surface unroll, not sheet metal. |
| 32 | kernel | features | Lattice / cellular infill structures | missing | large | No code beyond an unrelated deformer hit. |
| 33 | kernel | exchange | Rhino non-geometry/composite objects in .3dm (blocks, annotations, hatches, text dots) | missing | large | Real Rhino files silently lose all of these on open. |
| 34 | kernel | exchange | STEP AP242 | missing | large | Only AP214 exists; no TESSELLATED/PMI support. |
| 35 | kernel | exchange | Other mesh/scene exchange formats (glTF, 3MF, FBX, Collada, USD, ...) | missing | large | None of these common interchange formats exist at all. |
| 36 | kernel | exchange | IFC (BIM) data exchange | missing | large | No code anywhere. |
| 37 | kernel | exchange | DWF/DWFx export/import | missing | large | No code anywhere. |
| 38 | kernel | exchange | JT (PLM interchange) | missing | large | No code anywhere. |
| 39 | kernel | subd_mesh | Kernel-native SubD local edit operators (insert edge, extrude face, spin, weld, expand) | missing | large | All of these remain app-only; no kernel-level SubD edit API. |
| 40 | kernel | subd_mesh | Quad-remeshing of an arbitrary mesh into a clean SubD-ready cage | missing | large | No quad-dominant remesher targeting SubD-cage quality exists in the kernel. |

### Remainder, grouped by effort (283 items)

**Small effort** (61 items):
- [kernel/topology] Non-manifold topology (edge shared by 3+ faces, non-manifold vertices) (partial)
- [kernel/topology] Kernel-level topology enumeration API (loop/trim iteration) (partial)
- [kernel/topology] Merge contiguous tangent edges (partial)
- [kernel/topology] Cap naked loops — extend to non-planar-hole detection (partial)
- [kernel/geometry] Knot removal (curve) (partial)
- [kernel/geometry] Helix and spiral curves (partial)
- [kernel/geometry] Rational <-> non-rational conversion (tolerance-bounded) (partial)
- [kernel/blending] Fillet/blend on tangent edge chains and multi-edge selection (partial)
- [kernel/sweeplofts] Kernel-level partial-angle revolve parameter — start-angle option (partial)
- [kernel/offsetshell] Tolerance-driven offset refit (surface) (partial)
- [kernel/offsetshell] OpenNURBS-native mesh offset with fixed direction (partial)
- [kernel/localops] Extend a face/surface past its current boundary in place — in-place multi-face case (partial)
- [kernel/localops] Split an edge at a point — exact trim-parameter mapping (partial)
- [kernel/intersections] Curve/plane intersection — dedicated infinite-plane API (partial)
- [kernel/intersections] Point-cloud contour/section as separate app commands (partial)
- [kernel/healing] Micro/sliver edge removal — shared-edge case (partial)
- [kernel/healing] Edge merging — kernel wrapper for CombineContiguousEdges (partial)
- [kernel/massprops] Curve length / arc-length parametrization — Gaussian quadrature (partial)
- [kernel/massprops] Signed distance point-to-solid (partial)
- [kernel/tessellation] Angular (facet-normal deviation) tolerance control (missing)
- [kernel/tessellation] Isocurve / wireframe generation — public kernel API (partial)
- [kernel/transforms] Split body by plane — B-rep version (partial)
- [kernel/exchange] .3dm archive version targeting — wire a non-zero version through the app (partial)
- [kernel/exchange] PLY vertex colours (partial)
- [kernel/exchange] PLY binary_big_endian support (partial)
- [kernel/features] Counterbore (stepped coaxial) hole (partial)
- [kernel/features] Countersink (conical) hole (partial)
- [kernel/curveops] Curve fairing / smoothing — kernel API (partial)
- [kernel/curveops] Match curve end continuity — G2 (partial)
- [kernel/surfaceops] SrfSeam - test coverage caveat (partial)
- [kernel/surfaceops] Kernel Rebuild() - wire into app's Rebuild/FitSrf commands (partial)
- [kernel/surfaceops] Kernel MatchEdge() - wire G2 into MatchSrf (partial)
- [kernel/subd_mesh] SubD extraordinary-vertex limit-tangent quality — semi-sharp edge handling (partial)
- [app/app_commands] Command aliases — persist to disk (partial)
- [app/app_drafting] Dimension styles — units/precision/tolerance fields (partial)
- [app/app_display] Per-object display mode override — remaining modes (partial)
- [app/app_scripting] Headless/batch scripting mode — document as supported, not just QA (partial)
- [app/app_interop] .3dm archive version targeting (see kernel item above; app wiring) (partial)
- [app/app_ux] Localized command and toolbar help text — at least the top N commands (missing)
- [kernel/geometry] Torus primitive — dedicated ToroidalFace analytic record (partial)
- [kernel/geometry] Typed analytic surface classes — add a torus record (partial)
- [kernel/blending] Edge blend trimmed and joined into the polysurface — TrimAndJoin (partial)
- [kernel/offsetshell] Curve offset in an arbitrary plane / 3D — expose caller-specified plane (partial)
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

**Medium effort** (137 items):
- [kernel/topology] Multi-shell / multi-lump bodies — allow booleans on compound operands (partial)
- [kernel/topology] Loop structure: inner loops, loop walking, outer/inner classification (partial)
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
- [kernel/blending] Variable-radius fillet — non-linear laws, curved-face taper (partial)
- [kernel/blending] Chamfer with two unequal distances/angle — expose in app (partial)
- [kernel/blending] Face-face blend between two independently picked surfaces — trim both inputs (partial)
- [kernel/blending] Vertex blend — non-perpendicular and mixed-radius corners (partial)
- [kernel/blending] Fillet end conditions on adjacent end faces — remaining cases (partial)
- [kernel/blending] Blend removal / defeaturing — spherical vertex blends, oblique cylinders (partial)
- [kernel/blending] Fillet surface along a user-supplied rail curve — trimming (partial)
- [kernel/blending] 2D curve fillet / chamfer — kernel API (partial)
- [kernel/blending] Curve-to-curve blend — G3+ (partial)
- [kernel/blending] Surface-to-surface continuity blend — G3/G4, shape handles (partial)
- [kernel/blending] Rolling-ball blend surface accuracy on freeform surfaces — enforce max_gap (partial)
- [kernel/sweeplofts] Extrude a curve along a path curve — solid/cap option (partial)
- [kernel/sweeplofts] Extrude a surface / polysurface face into a solid — kernel B-rep API (partial)
- [kernel/sweeplofts] Extrude with draft / taper angle — oblique direction, concave polygons (partial)
- [kernel/sweeplofts] Extrude to a point — cap for closed profiles (partial)
- [kernel/sweeplofts] Full 360-degree revolve — closed profile touching the axis (partial)
- [kernel/sweeplofts] Partial-angle revolve — off-axis endpoint capping (partial)
- [kernel/sweeplofts] Rail revolve — kernel API (partial)
- [kernel/sweeplofts] Sweep along one rail — multi-section blending, scaling (partial)
- [kernel/sweeplofts] Sweep along two rails — multi-section, independent scaling (partial)
- [kernel/sweeplofts] Loft options — surface-to-surface edge tangency, guide curves (partial)
- [kernel/sweeplofts] Developable loft between two rails — kernel API (partial)
- [kernel/sweeplofts] Pipe — Round cap option, kinked-rail handling (partial)
- [kernel/sweeplofts] Pipe variants — thick-walled pipe, real MultiPipe (partial)
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
- [kernel/offsetshell] Planar curve offset — kink-preserving fit (partial)
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
- [kernel/localops] Merge contiguous tangent edges — kernel wrapper (partial)
- [kernel/localops] Remove small / sliver edges — shared-edge case (partial)
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
- [kernel/exchange] OBJ read — fan-triangulate >4-index faces, UV seams (partial)
- [kernel/exchange] Point-cloud / scan file formats — .pts/.e57/.las (partial)
- [kernel/exchange] Unit-system conversion — kernel Model unit declaration (partial)
- [kernel/exchange] Import-time B-rep validation and healing — call SewTJunctions from importers (partial)
- [kernel/features] Draft angle on extrusions — wire the app to the kernel, oblique directions (partial)
- [kernel/features] Thicken a sheet body — B-rep version (partial)
- [kernel/features] Delete face and heal — general delete-and-extend (partial)
- [kernel/features] Feature recognition — hole/boss/pocket recognition (partial)
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

**Large effort** (85 items):
- [kernel/topology] Wire bodies (edge/vertex-only B-rep body) (missing)
- [kernel/topology] Euler operators (missing)
- [kernel/topology] Persistent naming / topology identity across edits (missing)
- [kernel/booleans] Sheet/solid trim (missing)
- [kernel/booleans] Face-face imprint (missing)
- [kernel/booleans] B-rep-preserving booleans reachable from the application (missing)
- [kernel/booleans] Associative/history-enabled Boolean operations (missing)
- [kernel/booleans] AutoCAD-style INTERFERE (partial)
- [kernel/blending] Conic / rho blend cross-sections (missing)
- [kernel/blending] Fillet overflow / cliff-edge / notch handling (missing)
- [kernel/blending] Alternative blend rail types (missing)
- [kernel/sweeplofts] Extrude to a boundary surface / body (missing)
- [kernel/sweeplofts] Sweep controls: twist/scale/roadlike alignment (missing; twist along path is now partial - see Brep::Sweep1()'s twist_total)
- [kernel/sweeplofts] ExtrudeCrv/Revolve producing SubD directly (missing)
- [kernel/localops] Taper / draft face (missing)
- [kernel/localops] Replace face (missing)
- [kernel/localops] Imprint curve / face onto a body face (missing)
- [kernel/localops] Merge faces on the same non-planar surface (missing)
- [kernel/localops] Push/pull a face (missing)
- [kernel/localops] Move a single B-rep vertex directly (missing)
- [kernel/intersections] SSX tangent / grazing contact (missing)
- [kernel/intersections] CSX against trimmed faces and curve-on-surface overlap detection (missing)
- [kernel/healing] Kinky / creased surface splitting into G1 faces (missing)
- [kernel/tessellation] Angular (facet-normal deviation) tolerance control (missing)
- [kernel/tessellation] Post-tessellation deviation verification (missing)
- [kernel/transforms] Split / trim body with a tool body (missing)
- [kernel/exchange] Rhino non-geometry/composite objects in .3dm (blocks, annotations, hatches, text dots) (missing)
- [kernel/exchange] STEP AP242 (missing)
- [kernel/exchange] Other mesh/scene exchange formats (glTF, 3MF, FBX, Collada, USD, ...) (missing)
- [kernel/exchange] IFC (BIM) data exchange (missing)
- [kernel/exchange] DWF/DWFx export/import (missing)
- [kernel/exchange] JT (PLM interchange) (missing)
- [kernel/features] Threaded / tapped hole and external thread feature (missing)
- [kernel/features] Emboss / deboss onto a face (missing)
- [kernel/features] Draft / taper faces of an existing body (missing)
- [kernel/features] Split body with an arbitrary surface / solid cutter (partial)
- [kernel/features] Sheet-metal features (missing)
- [kernel/features] Lattice / cellular infill structures (missing)
- [kernel/subd_mesh] Kernel-native SubD local edit operators (missing)
- [kernel/subd_mesh] SubD boolean operations (missing)
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
- [kernel/features] Feature recognition — real hole/boss/pocket recognition from a dumb B-rep (partial)
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
| `.3dm` layer round-trip default-layer index fix (`AddLayer()` returns 0 instead of the true default -1) | kernel/exchange | `dino8-kernel/src/file_io.cpp` |
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
