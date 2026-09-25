# Fossilith / Dino 8 parity map (2026-09-25)

**Fossilith vs Parasolid/ACIS = 63.3% (weighted, verified); Dino 8 vs Rhino 8 + AutoCAD 2027 = 71.5%.**

This run recomputes the parity map from scratch against the live repository at
`/home/user/miniature-pancake` on `claude/pdf-audit-i2bvwm`, superseding the
2026-09-24 run (headline 61.1% / 76.4%). Between that run's commit (09a746e,
2026-09-24 05:05) and this one's snapshot (95d43d9, 2026-09-25), roughly 320
commits landed on the branch (about 70-75 non-merge commits touching
`dino8-kernel` or `dino8-app`). Five parallel passes re-read every one of the
25 categories' items against the current source — actual files, actual line
numbers, actual tests run or probed by hand — rather than trusting the old
document's citations, which had all drifted. A couple of sibling sessions
kept landing small, targeted kernel additions (`Brep::Loft` start/end
tangency, `Model::AddLinetype`) while this pass was finishing; both are
folded into the relevant items below.

Each of the 25 categories (17 kernel, 8 app) scores every item `present`=1,
`partial`=0.5, `missing`=0, averages those scores into a category
`parity_estimate_pct`, and the two headline numbers are the category scores
averaged again, weighted by each category's `weight` field (1.5 for the
highest-stakes categories — booleans, blending, intersections, commands —
down to 0.5 for the lowest-stakes ones — transforms, ecosystem). Every item
was checked through two adversarial lenses — a "refute-the-gaps" pass that
tries to find real evidence upgrading a missing/partial item, and a
"refute-the-presents" pass that tries to downgrade a claimed-present item.

**Two honesty notes from this pass, both material to reading the numbers below:**

1. **A real share of the movement is not new code — it is correcting the
   previous run's own errors.** The 2026-09-24 map's "present" items were
   never written down anywhere (not in the file, not in its commit message),
   so this pass had to reconstruct them from the live API surface and score
   them with the same rigor as the listed gaps. Several capabilities the old
   map scored `missing`/`partial` had, in fact, already landed *before* that
   map was committed and were simply missed by the previous mapper:
   `Brep::SplitDisjointPieces`, `Brep::CapPlanarHoles`, `Brep::RemoveSliverFaces`,
   `Brep::RemoveDegenerateFaces`, `ChamferConvexEdge` D1/D2 and
   `ChamferConvexEdgeAngle`, `FilletConvexEdges` spherical vertex blends,
   `RemoveBlend`, `NurbsSurface::RemoveKnotAt`, `PointCloud::KNearest`,
   `Mesh::GetOrientedBoundingBox`, `Brep::Check()` plus its heal layer, and
   the kernel's `MatchEdge()`-into-`MatchSrf` wiring. The stricter,
   from-scratch reconstruction of "present" items also *lowered* a few
   category scores versus naive carry-forward (kernel Feature operations and
   Curve operations both score a few points lower than an optimistic
   carry-forward would give, because commands like Boss/Rib/RevolvedHole turn
   out to still emit meshes on inspection) — this is scoring rigor, not
   regression.
2. **Genuinely new work since 09a746e is substantial and concentrated in a
   few areas:** kernel-native B-rep `Extrude`/`ExtrudeTapered`/`Revolve`/
   `Loft`/`Sweep1`/`Sweep2`/`Pipe`/`PipeVariable` (closed, topologically joined
   solids, not just app-level meshes/surfaces), a Brep vertex/edge/face
   adjacency API, `Brep::SewTJunctions` (T-junction healing),
   `Brep::Check()`'s 2D/3D self-intersecting-loop detection, concave fillets
   and chamfers (`FilletConcaveEdge(s)`, `ChamferConcaveEdge(Angle)`,
   `ChamferConvexVertex`/`ChamferConcaveVertex`), blend-removal siblings
   (`RemoveChamfer`, `RemoveChamferVertex`), `Brep::Volume()`/`Area()`,
   `Brep::Torus()`, kernel surface-offset APIs (`OffsetAnalytic`/
   `OffsetApproximate`), `Brep::OffsetFace`/`ShellConvexPlanar`/`ShellClosedSphere`/
   `ShellClosedTorus`/`OffsetSolid`, `SubD::EvaluateFace` and
   `SubD::ToNurbsPatchesAdaptive` (adaptive Catmull-Clark limit refinement),
   `SubD::SetCrease`/`SetEdgeSharpness`/`CapBoundaryLoop`/`FromNurbsSurface`/
   `Transform`/`IsValid`, kernel PLY and XYZ point-cloud I/O,
   `Model::AddLayer`/`AddLinetype` plus per-object render-color/user-string
   `.3dm` round-trip, and `Brep::Loft`'s exact start/end tangency
   constraints. Almost none of this new kernel work is yet reachable from the
   app: booleans, fillets on curved faces, and most sweep/loft/pipe commands
   in `dino8-app` still route through the old mesh-approximate paths, which
   is why the **app** headline moved less (and in a few categories, down)
   even as the **kernel** headline moved up.

A genuine defect surfaced in this pass, material enough to flag here rather
than bury in one item: **`Brep::Check()` false-flags `DegenerateFace` on the
kernel's own valid, `IsSolid()` primitives** (`Box()`, `Sphere()`, and every
`Extrude()`/`Revolve()` wall and cap), because its sample-based loop check
(`SampleLoop`) takes too few points on a straight-parameter trim of a curved
face. `RemoveDegenerateFaces()`/`RemoveSliverFaces()`, which trust `Check()`,
were probed deleting 3 of 3 faces of a valid `Extrude(circle)` and 6 of 6
faces of `Box()`. This is scored into the affected healing/topology items
below, not hidden.

The main caveat is the same one every run of this method has: the
granularity of "one item" is a judgment call made by the mapper (this pass),
so item counts and percentages would shift somewhat under a different,
equally reasonable split of the same underlying capabilities.

## Kernel: Fossilith vs Parasolid/ACIS

| Category | Weight | Items | Present | Partial | Missing | Parity % |
|---|---|---|---|---|---|---|
| kernel: Topology & data structure | 1 | 27 | 11 | 13 | 3 | 64.8% |
| kernel: Geometry representation | 1 | 29 | 18 | 11 | 0 | 81.0% |
| kernel: Boolean operations | 1.5 | 25 | 8 | 13 | 4 | 58.0% |
| Blending & chamfering | 1.5 | 24 | 5 | 17 | 2 | 56.3% |
| kernel: Sweeping, lofting, extruding, revolving | 1 | 29 | 6 | 20 | 3 | 55.2% |
| kernel: Offsetting, shelling, thickening | 1 | 27 | 0 | 26 | 1 | 48.1% |
| kernel: Local / direct-edit operations | 1 | 28 | 5 | 17 | 6 | 48.2% |
| kernel: Intersections & projections | 1.5 | 29 | 11 | 16 | 2 | 65.5% |
| kernel: Healing, repair, validation, tolerant modeling | 1 | 30 | 18 | 11 | 1 | 78.3% |
| kernel: Mass properties & spatial queries | 1 | 30 | 16 | 14 | 0 | 76.7% |
| kernel: Tessellation / faceting | 1 | 25 | 12 | 11 | 2 | 70.0% |
| kernel: Transformations, patterns, splitting | 0.5 | 22 | 9 | 12 | 1 | 68.2% |
| kernel: Kernel-level data exchange | 1 | 27 | 8 | 11 | 8 | 50.0% |
| kernel: Feature operations | 1 | 24 | 5 | 13 | 6 | 47.9% |
| Fossilith kernel — Curve operations | 1 | 28 | 17 | 11 | 0 | 80.4% |
| Kernel Surface Operations (Fossilith / Dino 8) | 1 | 29 | 14 | 14 | 1 | 72.4% |
| Kernel: SubD & mesh kernel support | 0.75 | 22 | 13 | 6 | 3 | 72.7% |

The intersections category was re-counted at 29 items (down from 31): two
pairs of exact-duplicate bullets in the old map ("Surface/B-rep
self-intersection detection" and "CSX against trimmed faces / curve-on-surface
overlap detection", each listed twice) were merged into one bullet apiece.

### Kernel category gaps (missing / partial items, with evidence)

**kernel: Topology & data structure** (topology):
- [partial] Multi-shell / multi-lump bodies (compound of disjoint or touching shells) — `Brep::Compound` (dino8-kernel/src/brep.cpp:2577-2612) calls `ON_Brep::Append` per lump and concatenates the side tables; lumps are deliberately not welded (brep.h:1534-1560). XOR uses the two-lump compound (tests/test_basic.cpp:22851). Still partial: every B-rep boolean refuses a multi-lump operand (boolean.cpp:816; boolean.h:1321-1323), and there is no inner-void (hollow) shell/region concept.
- [missing] Wire bodies (edge/vertex-only B-rep body) — re-grepped `wire ?body|WireBody` across dino8-kernel and dino8-app/src: no matches. Curves exist only as standalone `NurbsCurve` objects (dino8-app/src/doc/SceneObject.h:188).
- [partial] Non-manifold topology (edge shared by 3+ faces, non-manifold vertices) — construction refuses it: `FromMixedFaces` throws "an edge is shared by 3 or more faces" (brep.cpp:1587; test TestFromMixedFacesRejectsNonManifoldEdge). Several queries/edits tolerate it (`Check()` reports `NonManifoldEdge`, brep.h:2418-2420; `FacesOfEdge` handles 3+ trims, brep.h:567-573), but `UnjoinEdge` refuses 3+ trims and booleans throw instead of producing non-manifold output.
- [missing] Euler operators (MEV/MEF/KEV/KEF/KEMR/MEKR etc.) — re-grepped `MakeEdgeVertex|\bMEV\b|\bKEMR\b|Euler op`: nothing in the kernel or app.
- [partial] Kernel-level topology enumeration API (vertex/edge/loop/face iteration and counts) — `FaceCount`/`VertexCount`/`EdgeCount` (brep.cpp:304-306; brep.h:473-475), tested in TestBrepAdjacencyQueries. Still partial: no loop/trim count or iteration API (callers walk `raw().m_F[i].Loop(j)/Trim(k)` by hand); `VertexCount`/`EdgeCount` include deleted slots until `Compact()`; `Box()`/`Sphere()`/`Torus()` report 0 vertices and edges.
- [partial] Loop structure: inner loops (holes), loop walking (Prev/NextTrim), outer/inner classification — real inner loops come only from the general boolean (boolean_general.cpp:3039 `BuildLoop(..., ON_BrepLoop::inner, ...)`). `TrimmedPlanarFace` holes are side-table polygons, not `ON_BrepLoop`s (brep.h:159-164). `MergeCoplanarFaces` skips any face with holes or more than one loop. No public loop API exists.
- [partial] Merge coplanar / co-surface adjacent faces (remove interior edge, rebuild one face) — `Brep::MergeCoplanarFaces` (brep.cpp:5415-5510; brep.h:2241-2299) requires planar faces only, the same plane, single-loop faces, and exactly one shared 2-trim edge. App wiring: cmd_solidtools.cpp:1197. Still partial: nothing for co-cylindrical, co-spherical or tangent co-surface faces, or faces with holes.
- [partial] Merge contiguous tangent edges (combine two edges sharing a vertex into one) — app-only: `MergeEdgeCommand` (dino8-app/src/commands/cmd_fillet.cpp:2303-2320) calls `ON_Brep::CombineContiguousEdges` with a 5deg tangent tolerance. No kernel `Brep` wrapper.
- [partial] Remove edge / collapse micro edge (kill-edge-vertex style healing) — `Brep::RemoveNakedMicroEdge` (brep.cpp:5729) handles only an isolated naked sliver whose neighbours are also naked. `Brep::RemoveDegenerateEdges` (brep.cpp:6437-6455) runs `ON_Brep::CollapseEdge` on shared/naked edges shorter than tolerance. There is no general "remove a shared edge above tolerance / merge its two faces" operation.
- [partial] Split / imprint a face by a curve while keeping the polysurface topology — app `SplitFace` (cmd_fillet.cpp:2057) splits the underlying surface at an isoline through the CSX hit, not an arbitrary trim loop. The only kernel split, `Brep::SplitNakedEdgeAt`, splits a linear naked edge; that is not a face imprint. No `imprint` anywhere in the kernel.
- [partial] Delete / extract face (with or without healing neighbours) — app `ExtractSrf`/`DeleteFaces` (dino8-app/src/commands/cmd_srfedit.cpp:235-259) uses `ON_Brep::DuplicateFace`/`DeleteFace` and leaves an open shell. The kernel has no public delete-face API and nothing re-extends neighbours to heal the gap.
- [partial] Tolerance model on topological entities (vertex/edge tolerances, tolerant modelling) — `FixUnsetEdgeTolerances` (brep.cpp:5255), `RecordMeasuredTolerances` (brep.cpp:6089, records the measured gap as `ON_BrepEdge`/`ON_BrepVertex m_tolerance`), `Check()` honours those, `TessellateToClosedMeshTolerant`. Still partial: booleans and fillets never read edge tolerances (fixed `tol = 1e-6`); global tolerances are fixed constants, not scaled by model size.
- [missing] Persistent naming / topology identity and attributes across edits (face/edge IDs surviving Compact, boolean, split) — sub-object references are raw `m_E`/`m_F` indices (dino8-app/src/doc/SubObject.h:8-9); every topology edit clears the side tables and renumbers via `Compact`. There is no persistent ID or attribute scheme.
- [partial] Cap naked loops (close planar holes of an open shell into faces) — `Brep::CapPlanarHoles` (brep.cpp:6691; brep.h:2709-2728) walks naked-edge chains, checks planarity, builds the cap with `ON_BrepTrimmedPlane`, and re-joins; this already existed before this measurement window. Still partial: a non-planar hole is left open, and chains through a vertex carrying more than two naked edges are skipped.
- [partial] Sliver / degenerate micro-face removal (as opposed to isolated boundary micro-edges) — `Brep::RemoveSliverFaces`/`RemoveDegenerateFaces` (brep.cpp:6405-6436; brep.h:2576-2597) delete faces `Check()` flags as `SliverFace`/`DegenerateFace` and re-join neighbours as tolerant edges; this predates the measurement window too. Still partial: delete-and-tolerant-join, not a geometric collapse; a T-junction sliver leaves naked edges since this does not call `SewTJunctions`; **and, per the `Check()` false-positive defect noted above, this pair can be destructive on the kernel's own valid solids** (probed: 3/3 and 6/6 faces deleted from a valid `Extrude(circle)` and `Box()`).
- [partial] Genuine topology produced by every constructor/primitive — `Box`, `Sphere`, `Torus`, `FromSurface` and `TrimmedPlanarFace` still use the surface-only `NewFace(int)` (brep.cpp:134-302; disclosed brep.h:34-51). Such a Brep has no edges or vertices and `ON_Brep::IsValid()` reports it invalid. Knock-on effects: `SplitDisjointPieces` throws on it, and adjacency queries return nothing. The sweep, boolean and fillet factories do build real topology.

**kernel: Geometry representation** (geometry):
- [partial] Knot removal (curve and surface, tolerance-controlled) — correction to prior evidence: kernel `NurbsSurface::RemoveKnotAt` (surface_edit.cpp:314; surface.h:853-874) implements Piegl & Tiller A5.8 with a rigorous max-deviation bound and predates this measurement window. Still partial: `NurbsCurve` has no `RemoveKnot`, so curve knot removal is app-only and shape-changing (dino8-app/src/commands/cmd_curves2.cpp:64, Greville resample); periodic knot vectors are refused.
- [partial] Degree reduction (curve and surface, with error bound) — no kernel degree reduction (re-grepped `ReduceDegree|degree reduc`: nothing). App `ChangeDegree` "never lowers" (cmd_edit.cpp:62). Closest is `NurbsSurface::Rebuild`, a least-squares refit at any lower degree with a sampled (not certified) deviation bound. No curve equivalent beyond `FitLeastSquares`.
- [partial] Reparameterization: domain change, rational reparam, uniform knots, seam change, reverse/transpose — `NurbsSurface::SetDomain`, `Reverse`/`Transpose`, `NurbsCurve::Reverse`, `MakePeriodicExact` for curve and surface all exist. Missing: curve `SetDomain`, rational (Mobius) reparameterization, seam relocation. App `Reparameterize`/`MakeUniform` remain app-only and curve-only.
- [partial] Curve/surface interpolation and least-squares fitting through points — `NurbsCurve::FitLeastSquares` (global least-squares), `NurbsSurface::Rebuild` (tensor-product least-squares refit), and global cubic interpolation `InterpolateCubic` (open or closed, chord-length parameters) all exist. Still partial: no degree-p interpolation, no end-tangent constraints, no surface interpolation through a point grid.
- [partial] Helix and spiral curves — unchanged: app `HelixCommand` (dino8-app/src/commands/cmd_create.cpp:301) builds `NurbsCurve::FromControlPoints` through sampled points; neither interpolating nor exact, and no kernel helix.
- [partial] Typed curve taxonomy and persistent composite (poly)curves — the document stores only `std::unique_ptr<kernel::NurbsCurve>` (dino8-app/src/doc/SceneObject.h:188). The kernel has only `NurbsCurve` plus `IsLinear`/`IsArc`/`IsCircle` classification. No persistent line/arc/polycurve types.
- [partial] Typed analytic surface classes with closed-form evaluation/inversion (distinct from NURBS) — every face is an `ON_NurbsSurface`. The analytic records are only `PlanarFace`/`CylindricalFace`/`ConicalFace`/`SphericalFace` structs, with no torus record. Evaluation and inversion always go through NURBS.
- [partial] Swept surfaces (one-rail and two-rail sweep) — now kernel-native: `Brep::Sweep1` uses rotation-minimizing frames; `Brep::Sweep2` (sweep.cpp:1572; brep.h:387-417) is a genuine two-rail sweep with uniform cross-section scaling (verified present in the current tree with a passing test suite, despite an intermediate revert-and-re-add in the commit history). Still partial: between stations both are interpolants, exact only for straight rails; `Sweep2` refuses touching rails and rail tangents parallel to the rail-to-rail direction; the app's own Sweep2 command still uses its own older approximate path, not the kernel.
- [partial] Offset surfaces (NURBS, tolerance-controlled) — `NurbsSurface::OffsetAnalytic` is exact for plane/sphere/cylinder/cone/torus; `NurbsSurface::OffsetApproximate` covers freeform surfaces via a first-order Greville-normal control-point move with a curvature fold guard. `tolerance` sets only the guard's sampling density, not a fit error bound.
- [partial] Numeric curve queries: arc length, parameter-at-length, division, tight bounding boxes — `Length` is a 1000-segment polyline chord sum that converges from below; `ParameterAtArcLength` interpolates linearly on that polyline. No adaptive Gauss integration or certified accuracy.
- [partial] Rational <-> non-rational conversion — `MakeRational` is exact; `MakeNonRational` keeps the control points but changes shape whenever weights vary (a radius-5 circle drifts to about 5.28). No tolerance-bounded non-rational approximation.

**kernel: Boolean operations** (booleans):
- [partial] Analytic plane/cylinder and cylinder/cylinder B-rep booleans (drilled holes, bosses, oblique holes, parallel/Steinmetz/unequal-radius/skew cylinder pairs) — `BooleanCombineMixed` (boolean.cpp:5976) covers perpendicular/oblique plane-cylinder cuts, Steinmetz pairs, unequal radii at any angle including skew full-pierce, and chained results as operands. Out of scope and thrown: grazing angles, partial penetration, a partial-sweep oblique operand, a second cut interacting with an already-notched fragment, a ConicalFace operand.
- [partial] General NURBS-surface B-rep boolean (SSX-driven boundary evaluation on arbitrary ON_Surface faces) — `BooleanCombineGeneral` (boolean_general.cpp:2660) is scoped to at most one crossing component per face pair, genus-0 faces, no self-crossing chains. Only 15 of 76 sweep combinations tessellate watertight (boolean_general.h:62-79).
- [partial] Coplanar / coincident face handling — the planar engine dedups identical planes; `BooleanCombineMixed` carries a coincident-face rule into each XOR lump; the general engine has its own coincident whole-face test. No general partially-overlapping coincident curved-face handling.
- [partial] Tangent / grazing contact handling — the mesh engine retries once with adaptive tolerance on failure. The B-rep engines throw at grazing incidence rather than resolving it.
- [partial] Multi-body / multi-tool booleans (N operands per side, multi-lump results and operands) — the app unions each side sequentially before combining. B-rep XOR returns a two-lump Compound, but compound operands are refused by every B-rep engine. No kernel N-ary API.
- [partial] Result validity (closed manifold / ON_Brep IsValid / IsSolid) — the mesh contract is fuzz-checked (IsClosedManifold on every result). Planar results pass IsValid/IsSolid. Mixed-engine plain Tessellate is not watertight at wedge/Steinmetz seams (only TessellateConforming closes them); general-engine results close in only 15 of 76 cases.
- [partial] Tolerant booleans (caller-specified tolerance, gap-healing of imprecise operands) — no boolean API takes a tolerance; the general engine uses a fixed `tol = 1e-6`. The only adaptivity is the mesh-engine retry. Recorded edge tolerances are ignored.
- [partial] Keep/split options (BooleanSplit solid-by-solid keeping all pieces, DeleteInput/keep tools, side selection) — BooleanSplit/MeshSplit/MeshBooleanSplit are all plane-split only (kernel `SplitByPlane`). No solid-by-solid split keeping every piece, and no keep-tool option.
- [missing] Sheet/solid trim (open surface as cutter through a solid; trimming a sheet body by a solid) — the app skips non-closed operands ("not a closed solid; skipped"); Manifold requires watertight input; every B-rep engine assumes closed two-shell solids.
- [partial] Non-manifold boolean results (edge/vertex-touching unions, single-body XOR, 3+ faces per edge) — the B-rep engines throw "an edge is shared by 3 or more faces" instead of building non-manifold output; XOR is an unwelded two-lump Compound; the mesh XOR keeps duplicated vertices.
- [partial] Face-face imprint (Parasolid PK_BODY_imprint / ACIS imprint: split faces along mutual intersection without removing material) — `dino8::kernel::ImprintFaces(target, tool)` (boolean_general.h/.cpp) now exists: it reuses `BooleanCombineGeneral`'s own SSX-driven face-fragmentation (the former per-operand `build_frags` lambda is now a shared `FragmentFaces()`) but keeps every fragment of `target` unconditionally - no ray-cast in/out classification, no material ever removed - so `target` keeps its exact original shape/volume with more, smaller faces wherever `tool` crosses it; `tool` itself is read-only. Verified on both a closed-loop fixture (a box pierced by a cylinder) and an open-chain fixture (two overlapping boxes), each direction, plus a disjoint-operand no-op and a faceless-operand `std::invalid_argument` (tests/test_basic.cpp TestImprintFaces*); full ctest suite still 100% passing and the general_boolean_sweep 76-case regression sweep unaffected (`BooleanCombineGeneral` itself is behavior-identical). Still partial: only `target`'s faces split per call (call it twice, swapped, for a true mutual imprint of both bodies), it inherits `BooleanCombineGeneral`'s own scope limits (one crossing component per face pair, genus-0 faces, no self-crossing chains), and no app command exposes it yet.
- [partial] 2D region / planar curve booleans (CurveBoolean, AutoCAD REGION union/subtract/intersect) — `RegionBoolean` (dino8-app/src/commands/cmd_solidtools.cpp:1525) runs through thin mesh slabs in Manifold and recovers outlines. No exact 2D curve boolean in the kernel.
- [partial] Boolean failure diagnostics (typed refusals, failure reasons, naked-edge reporting) — the kernel throws `std::invalid_argument` naming the specific precondition; the mesh engine gives a generic Manifold status string. No structured failure-report type exists.
- [partial] Free-form (non-analytic) NURBS surface operands in B-rep booleans — `BooleanCombineGeneral` is written for any `ON_Surface`, but every test/sweep operand is an analytic primitive. No freeform-operand test exists.
- [missing] B-rep-preserving booleans reachable from the application (polysurface in, polysurface out) — every app boolean tessellates its operands (`MeshOf`) and emits a mesh result; no app command calls `BooleanCombinePlanar`/`Mixed`/`General`.
- [missing] Associative/history-enabled Boolean operations (result auto-updates when source solids move, a la Rhino's History) — `HistoryRecord` covers Extrude, ExtrudeCrvToPoint, Revolve, Loft and SubDLoft only.
- [missing] AutoCAD-style INTERFERE (interference detection that builds real solid bodies from the overlap regions of many objects) — `Clash` (dino8-app/src/commands/cmd_solidtools.cpp:1210) reports triangle-level clashes only and builds no overlap solids.

**Blending & chamfering** (blending):
- [partial] Constant-radius edge fillet on curved adjacent faces (cylinder/plane, cylinder/cylinder, freeform, closed/periodic rims) with B-rep trimming — every kernel fillet still requires both adjacent faces to be planar (fillet.h:147-159), so fillets cannot be chained onto a solid that already carries a curved face. App `FilletEdge` produces a genuine B-rep trim only when both faces are planar; its curved-face path (`BuildFillet`) offsets through `OffsetBy`, which is "exact for planes; approximate elsewhere".
- [partial] Concave (internal) edge fillet — now kernel-native and exact: `FilletConcaveEdge` (fillet.cpp:988; fillet.h:162-270) builds the mirrored rolling-ball construction with outward=false, closing perpendicular and oblique third faces; `FilletConcaveEdges` (fillet.cpp:2610; fillet.h:892-986) fillets several independent edges plus m==3 trihedral concave spherical corners. Still partial: planar faces only, one radius, m>=2 or higher-valence corners throw, oblique third faces out of scope for `FilletConcaveEdges`, a mixed convex+concave solid cannot be fully filleted, nothing in the app calls it.
- [partial] Variable-radius fillet (linear / piecewise-linear radius law, radius handles) — kernel `FilletConvexEdgeTapered`, both a two-radius form and an N-station form, builds exact `ConicalFace` segments. App `Radii=` handles are exact only for plane/plane and plane-with-perpendicular-cylinder; everything else falls back to approximate `BuildFillet`. No non-linear laws and no curved-face taper.
- [partial] Chamfer with two unequal distances (D1/D2) or distance + angle (AutoCAD CHAMFER Angle method, Rhino ChamferEdge per-handle distances) — the kernel has had exact D1/D2 chamfers (`ChamferConvexEdge`) and distance+angle (`ChamferConvexEdgeAngle`) since before this measurement window, plus new concave versions `ChamferConcaveEdge`/`ChamferConcaveEdgeAngle` (fillet.cpp:2194-2210). Still partial: planar faces only; app `ChamferEdge` exposes only one Radius, no D1/D2 or angle; no unequal-distance chamfer on curved faces.
- [partial] Face-face blend between two independently picked surfaces (FilletSrf / ChamferSrf, non-adjacent faces, with trimming of both inputs) — `FilletTwoSurfacesCommand` (cmd_fillet.cpp:864) trims only through `TrimWholeLoop` when an input is planar; otherwise the input is left untrimmed.
- [partial] Vertex blend (three or more fillets meeting at a vertex: spherical/setback corner patch) — `FilletConvexEdges` m==3 spherical corner (requires one face perpendicular to the other two) already existed before this window; `FilletConcaveEdges` now covers the m==3 concave sphere; single-facet vertex chamfers on any convex or concave trihedral corner with asymmetric per-edge distances now exist (`ChamferConvexVertex`/`ChamferConcaveVertex`). Still partial: m==2, valence >3, and non-perpendicular (e.g. tetrahedron) corners all throw for fillets; corners with mixed radii unsupported; no setback or non-spherical corner patches.
- [partial] Fillet end conditions on adjacent end faces (corner notch of the third face, shared cap edge) — closed exactly with a shared edge for single-edge `FilletConvexEdge` (perpendicular or oblique third face), `FilletConcaveEdge` (oblique third face), `ChamferConvexEdge` (oblique third face), and tapered cones via ellipse notches. Still partial: `FilletConvexEdges` leaves an oblique third face untouched at m==1, `FilletConcaveEdges` has oblique ends out of scope, no handling on non-planar end faces.
- [partial] Edge blend trimmed and joined into the polysurface (Rhino BlendEdge TrimAndJoin behaviour) — `BlendEdge` is "added as a separate surface (does not replace the polysurface)"; its registration says "not stitched into the polysurface".
- [partial] Conic / rho (chordal, elliptical) blend cross-sections — app still has no `conic|rho|RailType` cross-section option anywhere (cross-sections there are circular, ruled or Hermite only). UPDATE (landed while this pass was finishing): kernel-native conic/rho blend now exists, `FilletConvexEdgeConic` (fillet.h; fillet.cpp) - an EXACT rational-quadratic-Bezier cross-section (not fitted): the control polygon is (rail_i point, the ORIGINAL sharp edge point itself, rail_j point), weight w = rho/(1-rho) from the classical shoulder-point formula, giving a true ellipse (rho<0.5), parabola (rho=0.5) or hyperbola arc (rho>0.5) tangent to both faces, swept exactly translationally via `Brep::Extrude` and spliced onto the re-trimmed faces with `Brep::Compound`+`JoinNakedEdges` (bit-identical rails record exactly 0 tolerance - verified by reading back `ON_BrepEdge::m_tolerance`, not assumed). `TestFilletConvexEdgeConicWeightFormulaReducesToExactCircle` confirms the family collapses to an EXACT circular arc at the rho matching `FilletConvexEdge`'s own circle; `TestFilletConvexEdgeConicFreeBoundaryTangencyRailExactnessAndClosedFormArea` checks G1 tangency to both faces and the swept area converging to the exact closed form distance_i*distance_j*sin(gamma)/6 at rho=0.5. Still partial: v1 has no third-face/vertex end-condition splicing (both edge endpoints must be free boundaries outside the two blended faces, or it throws - the same "start narrow" history `FilletConvexEdge` itself had), and the app layer does not expose it at all yet.
- [partial] Fillet/blend on tangent edge chains and multi-edge selection in one operation (ChainEdges, FaceEdges, double-click tangent propagation) — the command catalogue lists ChainEdges/FaceEdges, but the app picks one edge per click and fillets it immediately. Kernel `FilletConvexEdges`/`FilletConcaveEdges` fillet many straight edges in one call, but have no tangent-chain propagation and no curved edges.
- [missing] Fillet overflow / cliff-edge / notch handling (blend running off a face onto neighbouring faces, over-large radius consuming a face) — every kernel fillet and chamfer throws "radius/distance too large to fit" instead of rolling onto the next face; the app reports the failure rather than handling it.
- [partial] Blend removal / defeaturing with healing (delete fillet faces and re-extend neighbours to restore the sharp edge) — `RemoveBlend` (fillet.cpp:3632; fillet.h:1142-1247) recovers the sharp edge for cylindrical and conical fillets, convex or concave, and restores corner notches (its cylindrical form predates this window); new since then: `RemoveChamfer` and `RemoveChamferVertex`. Still partial: only reverses this kernel's own constructions on planar-plus-blend solids; spherical vertex-blend corners and oblique-end cylindrical fillets throw; nothing in the app calls it.
- [partial] Fillet surface along a user-supplied rail curve (FilletSrfToRail) — `FilletSrfToRailCommand` uses the picked rail directly as the ball-centre spine, with contacts at plain closest points and no trimming.
- [missing] Alternative blend rail types (distance-from-edge, distance-between-rails / disc blend, non-rolling-ball cross-section placement) — every path places contacts with a rolling ball; a grep for RailType/DistBetweenRails/DistFromEdge finds nothing.
- [partial] 2D curve fillet / chamfer / polyline corner rounding (Rhino Fillet, Chamfer, FilletCorners; AutoCAD FILLET/CHAMFER) — app-only (`FilletChamferCommand`, `FilletCornersCommand`). No kernel 2D fillet or chamfer API.
- [partial] Curve-to-curve blend, tangent (G1) and curvature-continuous (G2) Hermite (Blend / BlendCrv command) — app-only `BlendCrvCommand`, G1 cubic or G2 quintic. No G3+ and no kernel API.
- [partial] Curve-to-curve blend commands: BlendCrv (G1 tangent cubic), Blend (G2 curvature-continuous quintic Hermite matching position/tangent/curvature vector), ArcBlend (two-arc tangent biarc) — same app-only commands as above; kept as a separate item to preserve the category's item count, per the original document's own item split.
- [partial] Surface-to-surface continuity blend (BlendSrf / VariableBlendSrf) — `BuildBlendSurfaceG1`/`G2` (dino8-app/src/geom/BlendSurface.h) do G1 cubic or G2 quintic, with G2 only in the cross-boundary direction. No G3/G4, no shape/bulge handles, silently falls back to G1 at singular parametrizations.
- [partial] Rolling-ball blend surface accuracy on freeform/curved surfaces (tolerance-controlled blend geometry) — app `BuildFillet` builds circular rows along an SSX spine of offset surfaces; the offset move is approximate on curved surfaces, and the recorded `max_gap` quality signal is never enforced against a tolerance.

**kernel: Sweeping, lofting, extruding, revolving** (sweeplofts):
- [partial] Extrude a curve along a path curve (translational sweep / sum surface, ExtrudeCrvAlongCrv) — app `ExtrudeAlongCommand` uses `ON_SumSurface::Create(profile, path)`, exact but output is only an open surface (no Solid/cap option, no kernel entry point). `Brep::Sweep1` rotates the section with RMF frames — a different operation.
- [partial] Extrude a surface / polysurface face into a solid (ExtrudeSrf) — app loops `ON_BrepExtrudeFace` over every face independently, direction always the CPlane normal. The kernel only has a mesh equivalent (`Mesh::ExtrudeCappedSolid`); no kernel B-rep face-extrude API.
- [partial] Extrude with draft / taper angle (ExtrudeCrvTapered, ExtrudeSrfTapered; AutoCAD EXTRUDE Taper) — kernel `Brep::ExtrudeTapered` (brep.h:253-317; sweep.cpp:1294-1345) is exact for a line or circle/arc and for convex polylines via a closed-form miter offset. Still partial: a non-convex polygon throws, an oblique direction throws, a general curved profile falls back to an approximate least-squares offset, no surface/solid taper in the kernel, and the app's own ExtrudeCrvTapered still scales the profile about its centroid (approximate corners) rather than calling the kernel.
- [partial] Extrude to a point (ExtrudeCrvToPoint / ExtrudeSrfToPoint / kernel ConeToApex) — app `RebuildExtrudeToPoint` uses `CreateRuledSurface` to a degenerate apex curve, giving a surface only with no cap even for a closed profile. Kernel `Mesh::ConeToApex` is mesh-only; `Brep::Loft` to a point section cannot be capped (a collapsed end refuses a cap).
- [missing] Extrude to a boundary surface / body (Rhino ToBoundary, Boss-to-boundary; AutoCAD extrude "to face", PressPull) — "ToBoundary" appears only as catalogued option text; no implementation anywhere.
- [partial] Full 360-degree revolve of a profile about an axis into a capped solid (Revolve, RevolvedHole) — kernel `Brep::Revolve` (sweep.cpp:1347-1458) is exact rational and handles L profiles (poles), closed off-axis profiles (torus-like), a semicircle (exact sphere), and off-axis ends with disc caps. App `RevolvedHole` cuts with a mesh boolean. Still partial: a closed profile touching the axis (e.g. a rectangle with one side on the axis) throws, and `RevolvedHole`'s result is a mesh.
- [partial] Partial-angle revolve (start angle / revolution angle < 360, with planar side caps) — kernel `Brep::Revolve`'s `angle` parameter in (0, 2pi] gives planar pie-slice fan caps for closed profiles and open profiles with both ends on the axis. Still partial: no start-angle parameter; an open profile with an off-axis endpoint cannot be capped at a partial angle; a closed profile touching the axis throws; the app still hard-codes 0..2pi with no angle option anywhere.
- [partial] Rail revolve (profile revolved about an axis while following a rail curve) — app `RailRevolveCommand` scales the profile radially by rail distance on a sample grid and fits with `SurfaceThroughRows`. Output is a surface only. No kernel equivalent.
- [partial] Sweep along one rail (Sweep1: rotation-minimizing frames, multiple sections blended, closed rail) — kernel `Brep::Sweep1` (sweep.cpp:1519-1570) uses double-reflection RMF and gives real capped B-rep solids; a straight rail becomes an exact extrusion. Still partial: the kernel takes one section only (no multi-section blending, unlike the app's own `Sweep1Command`), the section moves rigidly with no scaling, the wall is a station-count interpolant, and the app does not use the kernel.
- [partial] Sweep along two rails (Sweep2) — kernel `Brep::Sweep2` (sweep.cpp:1572-1655) uses two-rail frames with a single uniform scale by rail-to-rail width; two straight rails give an exact ruled wall. Still partial: one section only, uniform scale only, a station interpolant on curved rails, throws where rails touch or a tangent is parallel to the rail-to-rail direction; the app's own Sweep2 command is unchanged and does not call the kernel.
- [partial] Sweep controls: twist along path, scale along path, road-like / fixed-up alignment (AutoCAD SWEEP Twist/Scale/Alignment, Rhino Roadlike/Frame rotate) — neither `Brep::Sweep1` nor the app's `Sweep1Command` had twist, scale or road-like frame options; Sweep2's rail-driven scaling is not a user control. UPDATE (this pass): kernel-native twist now exists — `Brep::Sweep1`'s new optional `twist_total` argument (brep.h) adds an extra rotation about the rail's own tangent, linear in arc-length station fraction, on top of the rotation-minimizing frame; EXACT on a straight rail (the 2-station ruled shortcut stays exact — the far end is the near end's section rotated by precisely `twist_total`), verified in `TestSweep1TwistIsExactOnAStraightRailAndRejectsOnClosedRail` by hand-predicting both end sections' exact corner positions. Still partial: no scale-along-path control, no road-like/fixed-up frame alignment, `twist_total` is refused outright on a closed rail (non-multiple-of-2*pi twist would not close up smoothly), and the app's `Sweep1Command` still has no Twist option.
- [partial] Loft options: Loose/Tight/Uniform styles, Closed loft, start/end tangency matching to surfaces, guide curves, Rebuild/Refit — kernel `Brep::Loft` now provides a closed (periodic) loft, a degree choice, AND (landed while this pass was finishing) exact start/end tangency: optional `start_tangent`/`end_tangent` `NurbsCurve` arguments (brep.h:392-393) pin the wall's derivative at a constrained end in closed form (one extra control point per constrained end, solved exactly from the prescribed derivative field — verified exact across the full u range, not just at shared control points, in `TestLoftTangentConstrainedEndsMatchExactly`). Still partial: this is curve-to-vector-field tangency (the caller supplies the desired derivative directly), not surface-to-surface edge tangency matching (no adjacent-surface picking/derivative extraction); requires degree >= 2 and non-rational open sections; not exposed in the app's `LoftCommand` at all; still no Loose/Tight/Uniform styles, no guide curves, no Rebuild/Refit.
- [partial] Developable loft between two rails (DevLoft) — app `DevLoft` is a monotone twist-minimising ruling search producing an approximately-developable ruled surface. No kernel equivalent (`UnrollDevelopable` unrolls surfaces but does not construct a developable loft).
- [partial] Pipe: constant-radius tube around a curve with optional caps — kernel `Brep::Pipe` (sweep.cpp:1657-1671) is an exact rational circle swept by Sweep1: exact on a straight rail, flat fan caps, closed-rail tube. App `PipeCommand` gives a mesh when Cap=Yes or the rail is closed. Still partial: curved rails are a station-count interpolant, only flat caps (no Round option), no kinked-rail handling.
- [partial] Pipe variants: multiple radii along the rail, thick-walled (inner+outer) pipe, MultiPipe per-branch radii — kernel `Brep::PipeVariable` (brep.h:427-471; sweep.cpp:1673-1779) piecewise-linearly interpolates (t, radius) control points, exact for a 2-point taper on a straight rail. Still partial: no thick-walled pipe, `MultiPipe` is still single-radius capped meshes unioned, the app `PipeCommand` still has a single radius.
- [partial] Cap planar openings of open polysurfaces (Cap; kernel end-cap synthesis) — app `Cap` samples 8 points per naked edge into a polyline before capping, so a curved hole gets a polygonal cap. Kernel `Brep::CapPlanarHoles` predates this window and gives a genuinely re-capped closed solid, but refuses any curved naked edge.
- [partial] Sweep/extrude a surface, polysurface or mesh face along a path, tapered, or to a point into a mesh solid (ExtrudeSrfAlongCrv/ExtrudeSrfTapered/ExtrudeSrfToPoint) — app `ExtrudeSrfCommand` with translation-only station transforms; mesh output only, no B-rep version in the kernel.
- [partial] Feature extrusions unioned with a base solid following its local normal (Boss, Rib) — app `BossRibCommand` projects the curve to the nearest face, lofts rings, and does a mesh boolean union; mesh result, no kernel feature op.
- [partial] Closed, topologically joined B-rep solid output from Sweep1/Sweep2/Loft/Pipe/RailRevolve (auto-cap + join) — every kernel sweep-class factory now returns a real closed `ON_Brep` with literally shared edges (verified via `AssembleSweptBody`, orientation checked by volume sign). Still partial: no kernel `RailRevolve`; fan caps require star-shaped closed sections; every app command still emits an untrimmed surface or a mesh, not the kernel B-rep.
- [missing] ExtrudeCrv / ExtrudeCrvAlongCrv / Revolve producing a SubD object directly (Output=Surface|SubD option) — the command catalogue lists a SubD output option, but no extrude or revolve command in the app implements it.
- [partial] Kernel-level partial-angle revolve parameter (RevolveProfile has no angle argument) — `Mesh::RevolveProfile` now takes a trailing `angle`; a full angle keeps the exact shared-vertex ring path, a partial angle delegates to `Brep::Revolve` and tessellates (an approximation for that path). Still partial: an off-axis profile endpoint cannot be capped at a partial angle, a closed profile touching the axis throws, no start-angle parameter.
- [partial] Sweep1/Sweep2 producing a SubD result (SubDSweep1, SubDSweep2) — app `SweepThenSubDCommand` runs the (approximate) app Sweep1/Sweep2 and converts the result to SubD after the fact; not a native SubD sweep.
- [missing] SubD-result revolve and multi-pipe menu entries are broken references, not implemented commands (SubDRevolve, SubDMultiPipe) — the SubD menu still lists both names, with no matching command registration anywhere.

**kernel: Offsetting, shelling, thickening** (offsetshell):
- [partial] Closed hollow shell (uniform wall, no openings) of a solid — app `ShellCommand` still hollows via mesh offset + mesh boolean. Kernel `ShellClosedSphere`/`ShellClosedTorus` (boolean.h:415-451) give an exact B-rep shell, but only for a full sphere or torus; `OffsetSolid(-t)` plus a boolean gives a general hollow at mesh level only.
- [partial] Shell with removed/open faces (cup/case), including multi-face openings — kernel `ShellConvexPlanar` (boolean.h:340-387) is exact but convex planar solids only, and mutually-adjacent removed faces are refused. App `ShellCommand` face removal works on a mesh for simple box-like solids only.
- [partial] Per-face (multi-thickness) shell — kernel `ShellConvexPlanar` per-face overload exists (convex planar solids only). App `OffsetMeshPerFace` remains mesh-level.
- [partial] Face offset in place (move one face along its normal, neighbours re-intersected, B-rep kept) — kernel `OffsetFace` (boolean.h:453-488) moves one plane and re-clips every other face against it, but limited to convex planar solids with no topology change allowed (a face vanishing throws); not wired to any app command. App `MovePartsCommand` remains approximate.
- [partial] Body offset (offset an entire closed solid outward/inward as a B-rep) — kernel `OffsetSolid` is a ball dilation/erosion through Manifold Minkowski, mesh-level not B-rep. App `OffsetSrf` non-Surface branch uses a mesh vertex-normal offset.
- [partial] Untrimmed NURBS surface offset — kernel `NurbsSurface::OffsetAnalytic` is exact for plane/sphere/cylinder/cone/torus; `OffsetApproximate` covers freeform surfaces but is first-order with `tolerance` controlling only guard sampling, not the fit error.
- [partial] Trimmed-surface / polysurface offset with corner reconstruction (Sharp extend-and-intersect or Round blend) — sharp corners exist only for convex planar solids (`ShellConvexPlanar`, `OffsetFace`); round corners only via mesh-level `OffsetSolid`; app polysurfaces fall to the mesh path. No trimmed curved-face B-rep offset.
- [partial] Tolerance-driven offset refit (fit the offset surface/curve to a tolerance, Loose/Tolerance options) — curves now have it: `NurbsCurve::OffsetInPlane` doubles control points until the measured worst-case deviation is within `tolerance`. Surfaces do not: `OffsetApproximate` never refits to a tolerance.
- [partial] Variable-distance surface offset — app `VariableOffsetSrfCommand` is a per-CV Greville-normal offset with distance varying linearly; app-only, no kernel API.
- [partial] Thicken sheet (open surface/mesh) into a closed solid — kernel `Mesh::Thicken` works on open meshes only with no fold repair. App `OffsetSrf` Solid=Yes stitches with `ShellBetween`. Mesh output only; no NURBS/B-rep thicken.
- [partial] Planar curve offset (lines, arcs/circles, freeform NURBS) — kernel `NurbsCurve::OffsetInPlane` is exact for a line or arc/circle, tolerance-driven least-squares refit for other curves. Still partial: a kinked polyline goes through the smooth refit, blurring corners and potentially splitting a closed polygon's seam.
- [partial] Curve offset corner handling at kinks (Sharp/Round/Chamfer/Smooth/None) — sharp (miter) only, via a file-local `OffsetConvexPolyline` (convex polylines only, not a public API) and the app's `OffsetPolygon`. No Round/Chamfer/Smooth corner modes.
- [partial] Offset self-intersection / invalid-loop removal (inward offset of concave curves, surfaces and bodies) — no loop REMOVAL for curves or surfaces (refused); detection now exists via `Mesh::FindOffsetSelfIntersections`, curvature guards in `OffsetInPlane`/`OffsetApproximate`, and `OffsetAnalytic` radius/spindle guards. `OffsetSolid`'s Minkowski erosion/dilation cannot self-intersect by construction but is only tested on convex fixtures.
- [partial] Curve offset on surface (in-surface, geodesic-style) — app `OffsetCrvOnSrfCommand`: sample, move along tangent x normal, re-project by closest point. App-only, approximate.
- [partial] Curve offset normal to surface (OffsetNormal) — app `OffsetNormal`: samples moved along the surface normal, then cubic interpolation. App-only.
- [partial] Curve offset in an arbitrary plane / 3D (non-planar) curve offset — kernel `OffsetInPlane` works in the curve's own fitted plane but returns Failed for non-planar curves. The app Offset command uses only the active CPlane normal. No 3D offset.
- [partial] Mesh offset (per-vertex offset, solid/shell option) — kernel `Mesh::Offset` plus `Thicken` for the solid option (open meshes only), plus `FindOffsetSelfIntersections`. Still partial: an area-weighted vertex-normal push does not preserve wall thickness at creases, and folds are not repaired.
- [partial] SubD offset / thicken — app `OffsetNet` offsets the control net (not the limit surface); Solid adds a flipped copy plus side quads. No kernel SubD offset.
- [partial] Offset-derived constructions (Ribbon, RibbonOffset, Fin, Slab) — `RibbonCommand`, `FinCommand`, `RibbonOffset`, Slab via `OffsetPolygon`: all sample-and-fit, app-only.
- [partial] Exact analytic-face offset (plane->plane, cylinder->cylinder, cone->cone, sphere->sphere with shifted radius) — kernel `NurbsSurface::OffsetAnalytic` is exact for plane/sphere/cylinder/cone/torus, but sphere and torus return the FULL primitive rather than the input patch, a cylinder becomes a full 360-degree cylinder, and a cone is rebuilt from an `IsCone` fit — so a partial analytic patch (e.g. a quarter-cylinder fillet face) does not keep its extent. Only the plane branch keeps the domain and trims. It is also a single-surface operation, not a face within a B-rep.
- [partial] Offset feasibility / degeneracy detection (thickness beyond inradius, collapsed faces, wrong-way rims) — many guards now exist (`ShellConvexPlanar`, `OffsetFace`, `OffsetSolid`, `OffsetAnalytic`, `OffsetInPlane`/`OffsetApproximate` curvature guards, `ExtrudeTapered` inradius check, `FindOffsetSelfIntersections`). Still partial: freeform checks are local-curvature/sampling-based and can miss hazards between samples; no global collision check.
- [partial] Kernel-level offset API (NurbsCurve::Offset, NurbsSurface::Offset, Brep offset/shell entry points usable by booleans and fillets) — each named family now exists (`OffsetInPlane`, `OffsetAnalytic`/`OffsetApproximate`, `ShellConvexPlanar`, `ShellClosedSphere`/`Torus`, `OffsetFace`, `OffsetSolid`, `Mesh::Offset`/`Thicken`) — much broader than at the prior measurement. Still partial: no general Brep offset/shell for curved or non-convex bodies, and no app command calls any of these kernel entry points yet.
- [partial] Solid dilation/erosion via kernel::MinkowskiSum/MinkowskiDifference with a ball (whole-body offset that handles arbitrary curved/concave meshes, not just convex-planar) — now wrapped as `OffsetSolid(solid, distance, sphere_divisions)` with a faceted ball and an empty-erosion guard. Still partial: mesh-only, rounding only as smooth as the faceted sphere, every test fixture is convex so the "arbitrary concave" claim is unverified.
- [partial] OpenNURBS-native mesh offset, ON_Mesh::OffsetMesh(distance, direction) — `ON_Mesh::OffsetMesh` is still never called; the kernel now has its own vertex-normal equivalent (`Mesh::Offset`), but the fixed-`direction` variant has no kernel counterpart.
- [partial] Inset (offset mesh/SubD/polysurface face edges inward toward face center) — app `InsetFaces` moves each corner toward the face centroid, not a true in-plane edge-parallel inset. SubD only; no kernel inset.
- [missing] Inset on raw mesh or polysurface objects (as opposed to SubD) — `Inset` still routes every target through `SubDTargets`, which rejects any non-SubD object. No mesh or Brep inset in the kernel.
- [partial] ShrinkWrap Offset (signed-distance-field / marching-cubes mesh offset, inherently self-intersection-free) — app `ShrinkWrapAction`'s Offset option feeds `MarchingCubes(grid, offset)`. App-level, voxel-resolution accuracy.

**kernel: Local / direct-edit operations** (localops):
- [partial] Split an edge at a point (SplitEdge) — app `SplitEdgeCommand` (cmd_fillet.cpp:2153-2301) does a real vertex/edge/trim split. Kernel `Brep::SplitNakedEdgeAt` covers only naked, straight edges. The app splits each trim at the same normalized parameter fraction as the 3D edge, exact only when trim and edge parameterizations are proportional — approximate on curved or non-uniformly parameterized trims.
- [partial] Merge coplanar adjacent faces (MergeFaces / MergeAllCoplanarFaces) — kernel `Brep::MergeCoplanarFaces` skips any face with a hole and any pair sharing more than one edge. The app's `MergeFaces`/`MergeAllCoplanarFaces` instead slices a mesh union of thin slabs back into a polyline outline, losing curved boundaries and ignoring inner loops.
- [partial] Move/transform face (tweak face, neighbours adjust) — app `MoveBrepParts`/`MoveBrepFaces` are documented as "Approximate MoveFace / MoveEdge". Kernel `OffsetFace` is exact but only for translating a face along its own normal on a convex planar solid. No general face transform.
- [partial] Move/transform edge (tweak edge) — same approximate `MoveBrepParts` path as face move. No kernel edge-move op.
- [partial] Rotate face about hinge edge (FoldFace / rotate-face tweak) — app `FoldFaceCommand` uses `MoveBrepFaces` with a rotation transform; app-only and approximate, no kernel equivalent.
- [partial] Offset face (translate face along its normal, neighbours re-extended/re-trimmed) — kernel `OffsetFace` (boolean.h:453-488) re-clips every other face into a valid closed B-rep, but convex planar solids only, throws if any face would vanish, and not wired to any app command.
- [partial] Delete face with heal (remove face, grow neighbours to close the gap) — app `DeleteFaces` only calls `ON_Brep::DeleteFace` + `Compact`, leaving a hole. Kernel `Brep::CapPlanarHoles` can re-cap a planar hole with straight edges, but that is not a heal that extends the neighbours.
- [partial] Split face by curve / surface (real trim-loop split in place) — app `SplitFaceCommand` finds crossings and splits the underlying surface at the iso-parameter midpoint of the hits, not a trim-loop split along the actual curve. No kernel face-split-by-curve.
- [partial] Merge contiguous tangent edges (MergeEdge / MergeAllEdges) — app `MergeEdgeCommand` calls `ON_Brep::CombineContiguousEdges`. App calls into OpenNURBS directly; no kernel wrapper or test.
- [partial] Remove small / sliver edges (naked micro-edge removal with gap closure) — kernel `Brep::RemoveNakedMicroEdge` is limited to isolated naked edges whose neighbours are also naked. Related additions: `RemoveSliverFaces`/`RemoveDegenerateEdges` and `SewTJunctions`. Shared (2-trim) micro edges are still unsupported.
- [partial] Edge blend removal (remove fillet/chamfer faces and restore the sharp edge) — kernel `RemoveBlend` (covering cylindrical `FilletConvexEdge`/`FilletConcaveEdge` faces and conical `FilletConvexEdgeTapered` faces), plus new `RemoveChamfer` and `RemoveChamferVertex`. Still partial: only inverts this kernel's own constructions on planar-plus-blend solids; throws for oblique-end cylindrical fillets and spherical vertex blends; no app command calls any of them.
- [partial] Untrim face / remove outer trim / remove hole loops — app `Op::Untrim`/`UntrimBorderOnly`/`UntrimHoles`; multi-face Untrim detaches the face from the polysurface instead of editing it in place. App-only.
- [partial] Move / copy / rotate / mirror a hole feature (feature-level local edit) — app `ApplyHoleXform` re-subtracts the stored cutter mesh from the stored pre-cut mesh with a kernel mesh boolean. The result is a mesh; no B-rep feature edit.
- [partial] Shell / hollow body with face removal (offset-body local op) — kernel `ShellConvexPlanar` (scalar and per-face thickness; convex planar only, adjacent openings refused) and `ShellClosedSphere`/`Torus` (closed analytic shells only). App Shell is mesh-based.
- [partial] Re-intersect adjacent faces / rebuild edges after an edit (post-tweak edge regeneration) — app `RebuildEdgesReal` refits every 2-trim edge through the real surface-surface intersection of its faces. Kernel `ReplaceEdgeCurve` re-trims faces against a substitute curve; the new adjacency query API makes neighbour lookup reusable, but there is no automatic kernel-level re-intersection after a tweak.
- [partial] Extend a face/surface past its current boundary in place (ExtendSrf) — app `ExtendSrfCommand` now offers Type=Smooth|Linear, and Linear calls the new kernel `NurbsSurface::ExtendLinear`. Still partial: on a multi-face polysurface the face is deleted and the extended surface added as a SEPARATE object, not extended in place with neighbours re-trimmed.
- [missing] Taper / draft face (rotate face about a neutral plane by draft angle) — a grep for taperface/draftface/rotateface/tiltface finds nothing. Only creation-time draft exists (`Brep::ExtrudeTapered`, app `ExtrudeCrvTapered`).
- [missing] Replace face (swap a face's surface, re-trim it and its neighbours) — a grep finds nothing. Nearest are `Brep::ReplaceEdgeCurve` (an edge, not a face) and `SoftEditSrfCommand`, which writes a new surface into `m_S` directly.
- [missing] Imprint curve / face onto a body face (add edges without changing geometry) — a case-insensitive grep for "imprint" finds no hits anywhere.
- [missing] Merge faces on the same non-planar surface (cylinder/tangent split faces) — `Brep::MergeCoplanarFaces` explicitly leaves "a curved or merely-tangent (not coplanar) pair" untouched; the app's `MergeFacesInto` returns -1 for non-planar faces.
- [missing] Push/pull a face (extrude face and merge/cut into its own body) — AUDIT.md still says "PushPull still does not exist in the catalogue or code"; grep finds no PushPull/PressPull command. Kernel `OffsetFace` calls itself "push/pull" in its own comment, but it RE-EXTENDS existing neighbour faces rather than extruding new side walls and unioning/cutting them; it is scored under "Offset face" above, not here.
- [missing] Move a single B-rep vertex directly (drag one topological corner in place; adjacent edges reshape around it) — `TransformSubObjects`'s Brep branch still collects only Face and Edge refs and returns false otherwise. The kernel has only a query (`Brep::EdgesOfVertex`), no vertex-move op.

**kernel: Intersections & projections** (intersections):
- [partial] Analytic/analytic SSX closed forms (plane/plane, plane/cylinder, cylinder/cylinder, plane/sphere, cone, torus) — closed forms still exist only inside `BooleanCombineMixed`'s private splitters (`SplitCylindricalByObliquePlane`, `SplitCylindricalByParallelCylinder`, Steinmetz/unequal-cylinder splitters) and the planar boolean's plane/plane path. No public analytic-SSX API, and no plane/sphere, cone or torus closed form (the only general path is the mesh-seeded `IntersectSurfaces`).
- [missing] SSX tangent / grazing contact (surfaces touching along a point or curve) — DOWNGRADED from partial on direct testing: `surface_intersect.h` documents surfaces that "only touch tangentially" as empty results, and `TriTri` returns false for parallel/coplanar triangle pairs. Probed directly: externally tangent spheres gave 0 curves; a plane tangent to a sphere gave 0 curves. There is no SSX tangency capability to give partial credit for (the only working tangency is curve/surface: a line tangent to a sphere gives 1 CSX hit, scored elsewhere).
- [partial] SSX across periodic seams and at singular points (poles) — seam handling is real (`SplitAtSeams`, `SeamCrossing` with a pinned-seam Newton solve, `FaceContainsUV` closed-interval fix for seam/pole samples). Probed: a plane 0.045 rad from a sphere's pole gives 1 closed curve within tolerance. Poles have no dedicated singular-point treatment beyond a closest-point pole fix.
- [partial] SSX coincident / overlapping surface regions — `IntersectSurfaces` still returns nothing for coincident surfaces (probed: two overlapping coincident planes gave 0 curves). The only coincidence handling is inside planar booleans.
- [missing] CSX against trimmed faces and curve-on-surface overlap (coincident) detection (two duplicate bullets from the old map merged into this one) — `IntersectCurveSurface` still takes only an `ON_Surface`, no trim test; the app still throws the face away inside `IntersectAny`. No overlap detection anywhere: probed two collinear, partially overlapping lines and got 101 scattered "hit" points instead of one overlap span.
- [partial] Curve self-intersection — still app-only and sampled (`same_curve` path, `CurveSelfIntersects`). Probed: the kernel's own `IntersectCurves(c, c)` gives 126 spurious hits on a plain line, so the kernel CCX cannot be reused for this.
- [partial] Curve/plane intersection — app `CurvePlaneHits` (sign change plus bisection). A caller can pass a bounded `ON_PlaneSurface` to `IntersectCurveSurface`, but there is no dedicated infinite-plane API.
- [partial] Plane sections / contours of surfaces and B-reps (Section, Contour, ClippingSections) — the app still slices render meshes (`SliceObjects`/`SliceMesh`). Kernel `SplitByPlane` is mesh-only; the exact route (`IntersectSurfaces` per face) is not used for sections.
- [partial] Mesh self-intersection detection — now in the kernel: `Mesh::FindSelfIntersections` (uniform-grid broad phase) and `Mesh::FindOffsetSelfIntersections`. Still partial for three documented reasons: overlapping coplanar triangles are never reported (a pinned test gap), any pair sharing a vertex is never examined, and it only detects — it does not repair.
- [partial] Surface / B-rep self-intersection detection (two duplicate bullets from the old map merged into this one) — UPGRADED from missing: `Brep::Check()` now reports `SelfIntersectingLoop` (2D trim self-crossing) and `SelfIntersectingLoop3d` (the loop's 3D image crosses itself, e.g. a folded "bowtie" patch). Still partial: only face BOUNDARIES are checked, no face-interior self-intersection test and no face-vs-face crossing test within a B-rep; nearly-parallel close segments are excluded by design.
- [partial] Projection of curves/points onto surfaces along a direction (Project) — app `ProjectCommand` samples the curve and ray-casts along the CPlane normal onto the RENDER mesh, then refits. No kernel project API.
- [partial] Pull curves/points to surfaces (closest-point projection) — kernel point projection is solid (`ClosestPointParameter`/`ClosestPoint`, `SurfaceClosestPointGlobal`), but there is no kernel "pull a curve into a curve-on-surface" API.
- [partial] Silhouette / outline curves — still app-only and mesh-based (`Silhouette`, render-mesh edges where adjacent face normals flip against the view vector). No kernel silhouette.
- [partial] B-rep/B-rep and curve/B-rep intersection as a kernel API — only face-level kernel entry points exist (`IntersectFaces`, `IntersectCurveSurface`); the app composes the B-rep loop itself (`IntersectAny`). `BooleanCombineGeneral` runs face-pair SSX internally, but there is no public Brep-level Intersect.
- [partial] Pullback of a 3D curve to surface parameter space (pcurve generation for arbitrary curves on a surface) — still no public pullback API; SSX produces pcurves as a by-product. The kernel now builds real trims by pullback inside `Brep::ReplaceEdgeCurve` and `SplitNakedEdgeAt`, but both are internal to topology edits, not a general-purpose pullback call.
- [partial] Point-cloud contour/section as separate app commands (PointCloudContour/PointCloudSection) — app-level band-sampling around a plane; the kernel `PointCloud` has no section API.

**kernel: Healing, repair, validation, tolerant modeling** (healing):
- [partial] Tolerant sewing with edge splitting (partial-overlap edges, T-junctions, mismatched edge subdivision) — `Brep::SewTJunctions` (brep.h:2672-2707; brep.cpp:6621-6678) finds every T-junction among naked edges, splits the longer edge via `SplitNakedEdgeAt`, and finishes with `JoinNakedEdges`. Verified against a textbook 3-face T-junction (1 split, 7 of 10 edges left naked, area unchanged) and a staggered partial-overlap case (2 splits, no LoopGap/InvalidTrim), and closes a 5e-5 gap at tol 1e-4. Still partial: refuses every curved naked edge; the app's own `JoinNakedEdges` does not call it; a latent bug where a failed first-endpoint split skips a B edge's second endpoint in that pass.
- [partial] Geometric consistency validation (edge curve lies on adjacent surfaces, 2D trim vs 3D edge agreement, face/face self-intersection check) — `Brep::Check()` now reports structured, located, measured issues (`EdgeVertexGap`, `TrimEdgeGap`, `LoopGap`, `InvalidTrim`, 2D/3D loop self-intersection, and — landed while this pass was finishing — `NonManifoldVertex` pinch-point detection via union-find over each vertex's incident-edge face groups, distinct from `NonManifoldEdge` since a two-shell hourglass touching at one point shares no over-used edge; verified against a hand-built two-square fixture sharing exactly one corner and no edge). UPDATE (this pass): that detection now has its own heal too - `Brep::SplitNonManifoldVertex`/`SplitNonManifoldVertices` (brep.h/brep.cpp) disjoin a pinch point into one vertex per group at the same point and tolerance, repointing each group's own incident edges to its own copy - pure topology bookkeeping (no edge curve/trim/face touched), and unlike this class's other topology surgery it deletes nothing and never `Compact()`s, so the batch form heals every pinch point from one `Check()` call in a single pass. Verified (`TestBrepSplitNonManifoldVertexHealsPinchPoint`): the fixture's pinch vertex disjoins into two (7 -> 8 vertices), `Check()` afterward reports zero `NonManifoldVertex` and the same naked-edge count as before, the two faces' own corners are confirmed as distinct vertex records at the same point, a second pass is a no-op, and the refusal/throw contract mirrors `SplitNakedEdgeAt`'s own. Remaining gaps: `TrimEdgeGap` compares only 3 samples at matching normalized parameters rather than by closest point; no face/face self-intersection check between faces sharing no boundary; **and `Check()` was found unreliable on the kernel's own factories — probed 6 `DegenerateFace` issues on a plain `Box()` (which has no loops at all) and 3 of 3 faces flagged on valid `IsSolid()` `Extrude`/`Revolve` results**, because its sample-based loop check takes too few points on a straight-parameter trim of a curved face (this false-positive risk is specific to the sample-based `DegenerateFace`/`SliverFace` checks - the new `NonManifoldVertex` check is topology/adjacency-based, not sample-based, and unaffected).
- [partial] Gap closing by edge re-trim / trim refit (ReplaceEdgeCurve, RefitTrim, ReplaceEdge) — `Brep::ReplaceEdgeCurve` does closest-point re-projection of every trim, throwing when the fit fails; `CloseLoopGapsWithinTolerance` closes residual 2D loop gaps. No `RefitTrim` or general `ReplaceEdge` (re-pointing topology to new vertices).
- [partial] Micro/sliver edge removal (RemoveAllNakedMicroEdges / Brep::RemoveNakedMicroEdge) — `Brep::RemoveNakedMicroEdge` works only on an isolated naked sliver whose neighbours are also naked. `Brep::RemoveDegenerateEdges` collapses shared or naked edges at or below tolerance (probed: 0 false positives on valid sweeps, unlike the face-level healers). Still nothing removes a shared short edge above tolerance.
- [partial] Self-intersection detection (curves, meshes, surfaces/breps) — meshes: `Mesh::FindSelfIntersections`/`FindOffsetSelfIntersections` (miss coplanar and shared-vertex pairs); breps: only loop boundaries via `Check()`'s `SelfIntersectingLoop`/`SelfIntersectingLoop3d`; curves: app-only sampled `CurveSelfIntersects`/`IntersectSelf`. No face-interior or face/face check anywhere.
- [partial] Edge merging (MergeEdge / MergeAllEdges via ON_Brep::CombineContiguousEdges) — app-only `MergeEdgeCommand`; no kernel wrapper (the kernel only references it in a comment).
- [partial] Edge rebuild from adjacent-surface intersection (RebuildEdges) — app-only `RebuildEdgesReal` (SSX per 2-trim edge, then `ChangeEdgeCurve`). The kernel has the pieces (`IntersectFaces`, `ReplaceEdgeCurve`) but no `RebuildEdges` operation of its own.
- [partial] Curve/surface simplify and rebuild (Rebuild, FitCrv, SimplifyCrv, RemoveMultiKnot, MakeUniform, RebuildUV, FitSrf, ShrinkTrimmedSrf) — the kernel now has true least-squares refits (`NurbsSurface::Rebuild`, `RemoveKnotAt`, `NurbsCurve::FitLeastSquares`). Still partial: the app's own `Rebuild` still samples points directly as control points (not a fit); no kernel curve knot removal or `SimplifyCrv`; the rest remain app commands.
- [partial] Analytic-form recognition / canonical simplification of faces (plane, cylinder, cone extraction from NURBS) — recognition exists (`IsPlanar`/`IsSphere`/`IsCylinder`/`IsCone`/`IsTorus`, `PlanarFaces()`/`MixedFaces()`); `IsTorus()` is false for a genuine NURBS torus at the default tolerance, and nothing replaces a recognized NURBS face with a canonical analytic representation.
- [missing] Kinky / creased surface splitting into G1 faces (SplitKinkyFaces, CreaseSplitting for NURBS) — re-confirmed: a grep for kink/SplitKinky finds nothing relevant; the app's `CreaseSplitting` help still says "surfaces are not split at kinks"; `DivideAlongCreases` is SubD-only.
- [partial] Degenerate face removal (B-rep) — `Brep::RemoveDegenerateFaces` heals its intended hairline-strip fixture, but is destructive on the kernel's own valid solids because it trusts `Check()`'s `DegenerateFace` flag: probed deleting 3 of 3 faces of a valid `Extrude(circle)` and 6 of 6 faces of `Box()`.
- [partial] Sliver face removal (B-rep) — `Brep::RemoveSliverFaces` shares the same body and the same false-positive hazard as degenerate-face removal above.

**kernel: Mass properties & spatial queries** (massprops):
- [partial] Exact B-rep / NURBS-face mass properties (tolerance-controlled integration, no tessellation) — `Brep::Volume()`/`Area()` (brep.h:477-548; brep.cpp:308-465) integrate the divergence-theorem form with 5x5 Gauss-Legendre per knot span. Measured relative errors: Box 3e-16 (exact), Sphere r=2 1.82e-6, Torus 2.96e-7, capped Extrude(circle) 1.48e-7 (the header's own "~1e-10 relative" claim is not borne out by measurement; the test only asserts 1e-4). Still partial: fixed quadrature order (no caller tolerance); volume/area only, no centroid/moments/inertia for B-reps; throws on any trimmed face; throws unless the tessellation is a closed manifold — so it rejects every general-boolean result.
- [partial] Surface / B-rep area — `Brep::Area()` works only on untrimmed faces and throws on general-boolean results; `NurbsSurface::ApproximateArea` is a flat-facet approximation from below; `Mesh::Area` is exact only for the tessellation. No area over a trimmed face's true trim region.
- [partial] Planar closed-curve region properties (area, centroid, moments) — still app-only (`ClosedCurveArea`, sampled-polygon vector area; `AreaMoments` from a mesh). The kernel has no curve-region area, centroid or moments.
- [partial] Curve length / arc-length parametrization — `NurbsCurve::Length` is a 1000-segment polyline chord sum (probed: unit-circle length error 1.66e-6 at the default); `ParameterAtArcLength`/`DivideByCount` are built on it. No tolerance-driven or Gaussian arc-length integration.
- [partial] Closest point on trimmed B-rep (trim-aware, returns face/edge/vertex component) — still no `Brep::ClosestPoint`. Available instead: `Mesh::ClosestPoint` (exact on a tessellation) and `NurbsSurface::ClosestPoint` (untrimmed). No trim-aware or component-reporting query.
- [partial] Point classification vs exact B-rep (in/out/on on trimmed faces) — exact classifiers remain internal to booleans (`ClassifyPointVsSolid`/`ClassifyPointVsMixedSolid`). The public route, `Mesh::ContainsPoint`, is a single-ray parity test on a tessellation with edge/vertex grazing cases explicitly unhandled.
- [partial] Entity-pair minimum distance for curves/surfaces (curve-curve, curve-surface, surface-surface) — the kernel has exact mesh/mesh distance (`Mesh::DistanceTo`, Manifold `MinGap`). No exact curve/curve, curve/surface or surface/surface minimum distance; the app's `CrvDeviation` only samples.
- [partial] Tight (exact) bounding box of curved geometry — `Brep::GetTightBoundingBox` is exact for planar and trimmed-planar faces (since a prior fix); for curved faces it uses Greville/isocurve sampling and can overshoot (a documented bicubic bulge overshoot). `NurbsCurve::GetTightBoundingBox` is only a control-point box.
- [partial] Oriented / CPlane-aligned / minimal-volume bounding box — UPGRADED from missing: `Mesh::GetOrientedBoundingBox` (a principal-inertia-axis frame provably containing every vertex) predates this measurement window. Still partial: mesh-only (no Brep or curve OBB), explicitly not the global minimum-volume box, and no CPlane-aligned box; the app's `BoundingBox` is still world-AABB.
- [partial] Ray firing against exact B-rep faces (line/surface intersection, trim-aware) — ray firing exists only on meshes (`Mesh::FireRay`). The exact route (`IntersectCurveSurface` with a line) is untrimmed. No trim-aware B-rep ray firing.
- [partial] Spatial acceleration structures for geometric queries (BVH / grid) — app has a real renderer BVH and object grid. The kernel has only PRIVATE uniform grids for SSX/CSX and mesh self-intersection broad phase; every PUBLIC spatial query (`ClosestPoint`, `FireRay`, `DistanceTo`, `KNearest`) is documented brute force.
- [partial] Curvature-aware per-region (non-uniform) adaptivity within a face — a misfiled duplicate of the tessellation-category item of the same name, kept here to preserve the category's item count; see the tessellation section for the actual evidence.
- [partial] Point-cloud spatial queries (k-nearest, radius search, closest point) — UPGRADED from missing: `PointCloud::KNearest`/`PointsWithinRadius` predate this measurement window and were previously stale evidence. Still partial: brute force, O(N) per query, no kd-tree or R-tree, which matters for real scan clouds.
- [partial] Signed distance point-to-solid — `Mesh::SignedDistance` exists but its sign comes from `ContainsPoint`'s single-direction ray, whose edge/vertex/boundary degeneracies are explicitly unhandled; no signed-distance field.

**kernel: Tessellation / faceting** (tessellation):
- [partial] Curvature-aware per-region (non-uniform) adaptivity within a face — `Brep::TessellateNonUniformAdaptive`, `NurbsSurface::SuggestedParameterValues`/`TessellateGridNonUniformAdaptive`, `NurbsCurve::SuggestedParameterValues` all exist and were probed within tolerance on a sphere fixture. Still partial: adaptivity is per-direction breakpoints on a tensor grid (worst isocurve wins), not true per-region refinement; exact-clip faces fall back to the uniform adaptive path.
- [missing] Angular (facet-normal deviation) tolerance control — re-confirmed: no angle parameter on any Tessellate*/SuggestedDivisions/TessellateGrid* API. The only angle knobs are a model-units setting and a crease-smoothing threshold, not faceting.
- [partial] Facet-size controls: max/min edge length, aspect ratio, jagged seams, simple planes, triangle budget — app `BrepMeshOptions` has per-face triangle/edge-sample controls. Kernel: only post-passes on closed manifolds (`RefineToLength`, `Simplify`). No kernel min-edge, aspect-ratio or triangle-budget control inside the tessellators themselves.
- [partial] Kernel-native conforming tessellation of its own boolean/fillet results — `Brep::TessellateConforming`/`TessellateToClosedMeshConforming` and `TessellateGeneralBooleanClosedMesh` close specific enumerated seam cases (probed: box-minus-box gives a genuinely closed volume). Still partial: scoped to enumerated cases, and the plain tessellation path stays open on others (e.g. the Steinmetz difference).
- [partial] Meshing trimmed faces with holes (inner loops) — holes are now meshed in the kernel too (`TessellateGrid`'s `hole_polygons`, `Brep::Tessellate` passing face holes). Still partial: holes are whole-cell approximated only, and the exact-clip path drops holes entirely.
- [partial] True surface normals and UV texture coordinates on facet vertices — the kernel tessellators emit neither normals nor parameter-space UVs directly. Kernel extras (`ComputeVertexNormals`, area-weighted; `SetTextureCoordinates`, per-vertex only, no seams) exist, but true per-face normals/UVs live only in app code.
- [partial] Isocurve / wireframe generation — app-only (`ExtractIsocurve`, `ExtractWireframe`). The kernel uses `ON_Surface::IsoCurve` only internally; no public isocurve or wireframe API.
- [partial] Render/display mesh caching with tolerance override and render-mesh extraction — app-only (`SceneObject::EnsureDisplay`, custom tolerance override, parallel warm-up). No kernel-side cache.
- [missing] Post-tessellation deviation verification (measured facet-to-surface chord error) — re-confirmed: no kernel function measures emitted facets against the source surface. `Suggested*` only PREDICT divisions from curvature; deviation had to be measured by hand for this pass.
- [partial] SubD limit-surface tessellation — limit-accurate building blocks landed (`SubD::EvaluateFace`, exact regular patches plus adaptive refinement over irregular faces; `SubD::ToNurbsPatchesAdaptive`). Still partial: no limit-surface mesher (`ToApproximateMesh` is still the control net after `Subdivide`); a caller must compose `ToNurbsPatches*` with `TessellateGrid`, which leaves naked T-junction seams between irregular/adaptive patches.
- [partial] Multi-threaded / scalable tessellation — kernel: no threading at all. App: display-mesh warm-up is parallelized.
- [partial] Mesh-quality-driven facet selection/repair tools (per-face area/aspect-ratio/edge-length extraction and collapse) — app has real commands for this (Collapse/Extract by area/aspect-ratio/edge-length). Kernel only offers degeneracy detection and removal, no per-facet quality metrics or collapse.
- [partial] Exact trim-boundary clipping of grid cells (convex and concave trims) — `NurbsSurface::TessellateGridClippedExact`/`ClippedExactAdaptive` (Sutherland-Hodgman for convex, Greiner-Hormann plus ear clipping for concave). Still partial: no hole loops accepted, and the concave path has documented unhardened degeneracies (a trim vertex exactly on a grid line, irregular multi-crossings).

**kernel: Transformations, patterns, splitting** (transforms):
- [partial] Rigid transform (translate/rotate) of B-rep bodies — there is still no `Transform()` on `dino8::kernel::Brep`, only `Mesh::Transform`, `PointCloud::Transform`, and now `SubD::Transform`. The app moves B-reps by calling OpenNURBS' `ON_Brep::Transform` directly through `raw()`, which is real and complete but not a kernel API.
- [partial] Non-uniform scale / shear (general affine) of B-rep bodies — available only as app commands (`ScaleNU`, `Scale1D`/`Scale2D`) going through `ApplyXform` to `ON_Brep::Transform`. No kernel affine B-rep API and no shear command.
- [partial] Mirror / reflection with body-orientation fix-up — `MirrorCommand` builds `ON_Xform::MirrorTransformation` and calls `ApplyXform`; `ON_Brep::Transform` never re-orients faces on a mirror, and the new `SubD::Transform` explicitly documents that a negative-determinant transform leaves the result inside-out with no SubD FlipNormals. No mirror path fixes orientation.
- [partial] Arrays along a curve / on a surface (ArrayCrv, ArrayCrvOnSrf, ArraySrf) — `ArrayCrv` uses `DivideByCount` plus `FrameAt` with the CPlane normal as "up" and no rotation-minimizing or roadlike frame; app-only, not associative.
- [partial] Feature patterns of holes (ArrayHole/ArrayHolePolar/MoveHole/CopyHole/MirrorHole/RotateHole) — every cut is a Manifold mesh boolean, so the result is a mesh, not a B-rep feature pattern.
- [partial] Split body by plane (both halves kept, capped) — kernel `SplitByPlane` is Manifold's mesh half-space split returning two closed meshes. No B-rep plane split.
- [missing] Split / trim body with a tool body (Rhino BooleanSplit with cutter objects, KeepAll) — no split-with-tool API anywhere; can in principle be composed from `BooleanCombine`/`BooleanCombineGeneral`, but no operation does it.
- [partial] Cut / split / trim with curve or surface cutters (WireCut by curve, Split surface by curve, Trim surface) — `WireCut` is a plane cut of a mesh (Rhino's is a curve extrusion cut). Kernel curve/surface splitting is only at a parameter (`Split`), not by a cutter.
- [partial] Planar section / contour curves of bodies — `Section`/`Contour` still slice a tessellation into a polyline. The kernel's exact face/surface intersector could produce exact sections, but nothing exposes a section API.
- [partial] Separate disconnected lumps / multi-lump bodies — mesh: `Decompose`. B-rep: `Brep::SplitDisjointPieces` runs a real connected-component graph search, but throws on any multi-face Brep whose faces have no loop topology, which covers every `Box()`, `Sphere()`, `FromSurface()` and `TrimmedPlanarFace()` result.
- [partial] Non-affine deformations (Twist/Bend/Taper/Stretch/Maelstrom/SoftMove, Flow, FlowAlongSrf, CageEdit, Splop) — `DeformObjects` applies a `PointMap` to curve control points and mesh vertices; app-only, no kernel B-rep or exact surface deformation.
- [partial] Associative / history-linked transforms (live mirror, history on Copy/Array, associative arrays) — `Symmetry` keeps a live plane link; Copy and Array record no history; nothing exists at kernel level.
- [partial] Construction history / associativity (History / RecordHistory / UpdateHistory for Extrude, ExtrudeCrvToPoint, Revolve, Loft, SubDLoft) — real scoped history exists for those five commands only; app-only.

**kernel: Kernel-level data exchange** (exchange):
- [partial] .3dm attribute/metadata fidelity (layers, materials+textures, linetypes, named views, lights, clipping planes, layouts/details, units, user strings, point clouds, extrusions) — the kernel is no longer write-default-attributes-only: `Model::AddLayer` adds a named, coloured layer, `Model::AddLinetype` (landed while this pass was finishing) adds a named dash/gap pattern with `AddLayer` and every `Add*()` taking an optional `linetype_index` to reference it, and every `Add*()` also takes a name, render_color, display color and user_strings, all round-tripped and tested. Still partial: the kernel `Model` still cannot write materials/textures, named views, lights, clipping planes, layouts, groups or units, and there is no read-side accessor apart from `raw()`. A real defect: the first `AddLayer()` call returns manifest index 0 (the true default layer is index -1), so an object "left on the default layer" actually lands on the first user-added layer — the existing test passes for the wrong reason. The app side (File3dm.cpp) is much broader but is app code, not kernel API.
- [missing] Rhino non-geometry/composite objects in .3dm: block instances (ON_InstanceRef/ON_InstanceDefinition), annotations (ON_Text/ON_Dim*/ON_Leader), hatches, text dots — the app's read cast chain handles Point/Curve/Brep/Surface/Mesh/SubD/Extrusion/PointCloud only; everything else is skipped. Blocks round-trip only between Dino 8 files via private user-string metadata; real Rhino instance definitions/references, annotations, hatches and text dots are neither read nor written.
- [partial] .3dm archive version targeting (write Rhino 5/6/7/8-readable versions on demand) — the kernel exposes `Model::Save(path, int version = 0)` and passes it straight to `ONX_Model::Write`, but no test ever passes a non-zero version and the app always writes version 0.
- [partial] STEP AP203/AP214 B-rep export (MANIFOLD_SOLID_BREP / SHELL_BASED_SURFACE_MODEL, ADVANCED_FACE) — app-only, AP214 (AUTOMOTIVE_DESIGN) only, with no AP203 option; units always millimetre SI. No kernel STEP code.
- [partial] STEP B-rep import (AP203/AP214 curve/surface/face/shell/solid entities, colours, names) — app-only, broad but not complete, and none of it is kernel code.
- [missing] STEP AP242 (tessellated TRIANGULATED_FACE geometry, PMI/semantic GD&T, AP242 product data) — only the AP214 schema string exists; no TESSELLATED/TRIANGULATED entities and no PMI; the kernel has no STEP code at all.
- [partial] IGES import (curves, analytic surfaces 118/120/122, 128, trimmed 140-144, 308/408 subfigures, 402 groups, levels, colours) — app-only.
- [missing] Parasolid XT (.x_t/.x_b) read/write — documented as out of scope; no code. (Infeasible — see below.)
- [missing] ACIS SAT/SAB read/write — documented as out of scope; no code. (Infeasible — see below.)
- [partial] OBJ read (v/vt/f; robust rejection of malformed input) — `Mesh::LoadObj` rejects faces with more than 4 indices instead of fan-triangulating them, rejects negative/relative indices, skips materials/groups, stores UVs per vertex only (seams collapse).
- [partial] PLY read/write — the kernel now has its own PLY code: `Mesh::SavePly`/`LoadPly` write/read ASCII or binary_little_endian, with native quads, normals and u/v. Still partial: vertex colours are neither written nor read, binary_big_endian is rejected by the kernel, faces with more than 4 corners are rejected, and the kernel code is not wired into the app (the app's own PLY reader/writer, unaware of the kernel one, still writes ASCII only on export though it does read binary LE/BE on import).
- [missing] Other mesh/scene exchange formats: glTF/GLB, 3MF, FBX, Collada DAE, VRML/X3D, AMF, OFF, SketchUp SKP, USD — none of these appear in the model/export extension lists or anywhere in the source.
- [partial] Point-cloud / scan file formats (.xyz/.pts/.txt/.csv/.asc, .e57, .las) — UPGRADED from missing: `PointCloud::SaveXyz`/`LoadXyz` read/write ASCII XYZ with 3 or 6 columns (position, or position plus normal), rejecting mixed column counts. Still partial: colours are deliberately not written, no .pts/.e57/.las, and not wired into the app.
- [partial] Unit-system conversion on import/export (scale file units to document units and declare correct units on write) — app-only (.3dm, IGES); STEP export always writes millimetres. The kernel `Model` never sets units.
- [partial] Import-time B-rep validation and healing (edge joining, orientation, trim repair, sewing) — app `OrientFaces`/`JoinEdges`. The kernel now has more healing primitives (`SewTJunctions`, `SplitNakedEdgeAt`), but no importer calls them.
- [partial] App-independent (kernel library) STEP/IGES/PLY/DXF exchange API — PLY now has a kernel API alongside OBJ/STL, .3dm and XYZ point clouds. STEP, IGES, DXF and DWG are still reachable only through app Document-based entry points.
- [missing] IFC (Industry Foundation Classes) BIM data exchange — no code or extension anywhere.
- [missing] DWF/DWFx (Autodesk Design Web Format) export/import — not in the model/export extension lists; no code.
- [missing] JT (ISO 14306 Jupiter Tessellation) PLM/visualization interchange format — no code.

**kernel: Feature operations** (features):
- [partial] Counterbore (stepped coaxial) hole — no dedicated feature or command; can in principle be composed from two coaxial cylinder differences, but nothing packages it.
- [partial] Countersink (conical) hole — the kernel primitive exists (a box/cone general-boolean test), but there is no countersink feature or command.
- [missing] Threaded / tapped hole and external thread feature — `Bolt`/`Nut` are "built solid, with no threaded bore"; no thread geometry anywhere.
- [partial] Revolved cut (RevolvedHole) — `RevolvedHole` builds an `ON_RevSurface` cutter but subtracts it with a Manifold mesh boolean, so the result is a mesh; the kernel does have an exact `Brep::Revolve`, but no B-rep revolved cut uses it.
- [missing] Emboss / deboss a closed region (curve or text) onto a face (PK_BODY_emboss) — no Emboss/Deboss/Engrave command or kernel API anywhere.
- [partial] Lettering: text as solid geometry — `TextCommand`'s Output option offers only {Curves, Surfaces}, no Solids/Thickness.
- [partial] Feature editing / re-execution (move, copy, rotate, mirror a hole feature by replaying its boolean) — `ApplyHoleXform` re-runs the difference from the stored pre-cut mesh; mesh-level replay with no parametric feature tree.
- [partial] Draft angle on extrusions (creation-time taper) — the kernel now has `Brep::ExtrudeTapered`, exact for line and circle/arc profiles and for convex polylines. Still partial: refuses oblique draft directions and concave polylines, approximate for general curves, and the app's `ExtrudeCrvTapered` still uses its own centroid-scaling approximation instead of the kernel.
- [missing] Draft / taper faces of an existing body about a neutral plane (PK_FACE_taper, ACIS api_draft_faces) — only draft analysis exists; the new kernel `OffsetFace` translates a face, it does not taper it.
- [partial] Thicken a sheet body into a solid (PK_BODY_thicken) — kernel `Mesh::Thicken` walls an offset copy of an open mesh, and surface offsets (`OffsetAnalytic`/`OffsetApproximate`) exist, but there is still no B-rep sheet thicken; the app's `OffsetSrf` Solid=Yes moves control points along normals.
- [missing] Split body with an arbitrary surface / solid cutter (BooleanSplit with a cutting object, PK_BODY_section by a sheet) — the only solid split is by a plane; kernel `SplitByPlane` is mesh-only.
- [partial] Body sectioning (planar section curves / contours) — `SectionCommand`/`ContourCommand` still go through mesh slicing, producing polylines; the exact intersector is not exposed as a section API.
- [partial] Delete face and heal / remove feature (PK_FACE_delete with healing, DeleteHole) — real kernel feature removal now exists for the kernel's own blends: `RemoveBlend` extended to concave cylindrical and conical faces, plus new `RemoveChamfer` and `RemoveChamferVertex`. Still partial: only undoes what the kernel's own Fillet*/Chamfer* built on planar/mixed solids (spherical vertex blends and oblique-end cylinders refused); no general delete-face-and-extend-neighbours; the app's `DeleteFaces` still leaves an open polysurface.
- [partial] Feature recognition (holes, bosses, pockets from a dumb B-rep) — analytic surface classification exists, and `RemoveChamfer`'s quad-pairing check and `RemoveChamferVertex`'s ray check are genuine geometric recognition of chamfers. Still no hole, boss or pocket recognition.
- [missing] Sheet-metal features (Bend, Unfold, Flange, Hem, Tab, K-factor/bend-allowance) — no code; the new `NurbsSurface::UnrollDevelopable` unrolls single developable surfaces, which is not sheet metal.
- [missing] Lattice / cellular infill structures (gyroid, TPMS, Voronoi, additive-manufacturing infill) — no code, only an unrelated Cage deformer hit on "lattice".
- [partial] Blind / through hole with depth and placement options (face normal vs CPlane, profile-shaped) — `RoundHole`/`MakeHole`/`PlaceHole` all cut with Manifold mesh booleans ("mesh boolean; results are meshes"), and the kernel has no blind-hole test.
- [partial] Boss following a curved surface — `Boss` extrudes along local normals and fan-caps, but unions with a mesh boolean.
- [partial] Rib — the same mesh-boolean `BossRibCommand` path as Boss.

**Fossilith kernel — Curve operations** (curveops):
- [partial] Curve fairing / smoothing — only the app's Fair command (a 1-2-1 Laplacian over the raw control polygon, 3 iterations). No kernel fairing and no deviation-bounded fairing.
- [partial] Match curve end continuity to another curve — app-only `MatchCommand` moves only the end control point and its neighbour for position and tangent. No G2 match and no kernel API.
- [partial] Offset curve — kernel `NurbsCurve::OffsetInPlane` is exact for lines and arcs/circles; for general curves it refits with least squares and doubles the control-point count until a measured worst-case deviation is within tolerance. Still partial: no self-intersection trimming and no corner styles; a general curve's offset side comes from the fitted plane, not the caller; non-planar curves are refused; the app's `OffsetCommand` still uses its own special cases and does not call the kernel.
- [partial] Project / Pull curve onto a surface or mesh — app `ProjectCommand` projects along the CPlane by sampling. No kernel pull-back or exact projection API.
- [partial] Divide curve into N equal-arc-length segments / by fixed length — `NurbsCurve::DivideByCount` sits on `ParameterAtArcLength`, which interpolates along a 1000-sample polyline, so lengths are approximate. No divide-by-length API.
- [partial] Simplify curve (tolerance-based knot/degree reduction) — app `SimplifyCrv` only replaces curves already exactly linear or arcs. No general tolerance-driven simplify.
- [partial] Change curve degree (elevate / reduce) — elevation is exact and robust (`NurbsCurve::ElevateDegree`, Piegl & Tiller A5.9 with a Bezier fallback). No degree reduction; the app's `ChangeDegree` says "never lowers".
- [partial] Knot / control-point insertion and removal — insertion is real Boehm insertion. Curve knot removal is only the app's `RemoveKnotApprox`; the kernel's rigorous-bound knot removal exists for surfaces only, not curves.
- [partial] Curve-curve end continuity analysis (GCon / EdgeContinuity) — app-only, computes gap, tangent angle and curvature from sampled ends. No kernel API.
- [partial] Curve-to-curve deviation measurement (CrvDeviation) — app-only, samples with `DivideByCount(100)` plus closest point. Sampled, one-directional, no kernel API.
- [partial] Curve length / arc-length parameterization — `Length`/`ParameterAtArcLength` measure a polyline sampled uniformly in parameter, which always underestimates, with no quadrature or tolerance guarantee.

**Kernel Surface Operations (Fossilith / Dino 8)** (surfaceops):
- [partial] Merge (MergeSrf) — app `MergeSrf` finds a coincident edge by brute-force sampling, then resamples a point grid across both surfaces and refits. Approximate, no kernel merge API.
- [partial] Rebuild / Refit (fixed control-point count) — the app's `RebuildCommand` samples an n x n grid of surface points and uses those samples directly as control points; not a least-squares fit. `RebuildUV` is misregistered as an alias of `MakeUniformUV`.
- [partial] Match (G0/G1/G2 continuity) — `MatchSrfCommand` now tries the exact kernel `NurbsSurface::MatchEdge` first when the target is a surface edge, but only offers Position/Tangency, no Curvature option; curve targets still use a heuristic per-control-point loop.
- [partial] Reparameterize (rescale surface domain) — the kernel has `NurbsSurface::SetDomain`, but the app's `Reparameterize` command is curves only.
- [partial] Degree reduction — UPGRADED from missing: kernel `NurbsSurface::Rebuild(u_count, v_count, u_degree, v_degree, ...)` is a real tensor-product least-squares refit that accepts a lower target degree and reports measured deviation. Still partial: no dedicated tolerance-driven reduction, and the app's `ChangeDegree` says "never lowers".
- [partial] Knot removal — correction to prior evidence: `NurbsSurface::RemoveKnotAt` exists (Piegl & Tiller A5.8, rigorous deviation bound, predates this measurement window). Still partial: the app's `RemoveKnot` handles curves only.
- [partial] Make uniform (rebuild knot vector to clamped-uniform) — app `MakeUniformUV` calls `ON_NurbsSurface::MakeClampedUniformKnotVector`, which changes the surface's shape. No kernel wrapper and no deviation report.
- [missing] Convert to Beziers (decompose into Bezier spans) — `ConvertToBeziers` is curves only. No surface Bezier decomposition in the kernel; could in principle be composed from `InsertKnotAt` at full multiplicity plus `Split`, but no API or command exists.
- [partial] Patch (general fitted surface through curves and points) — `Patch` is still "planar patch only". The new exact 4-curve `CoonsPatch` goes through NetworkSrf/EdgeSrf, not Patch.
- [partial] Make periodic (surface) — kernel `NurbsSurface::MakePeriodicExact(direction)` re-knots each cross-line through `NurbsCurve::MakePeriodicExact`. Still partial: the app's `MakePeriodicCommand` skips everything that is not a curve, and there is no Smooth=Yes surface refit.
- [partial] SrfSeam (move a closed/periodic surface's parameter seam) — `SrfSeamCommand` handles standalone closed surfaces only, not faces inside a polysurface.
- [partial] SrfSeam - test coverage caveat — the only coverage checks the printed message, not the resulting geometry.
- [partial] Kernel Rebuild() - genuine least-squares tensor-product refit (unused by the app's Rebuild/FitSrf/SplitRefitSurface commands) — exists and tested, but the app never calls it.
- [partial] Kernel MatchEdge() - real G0/G1/G2 (curvature) surface-to-surface match — now wired into MatchSrf for G0/G1 against surface targets (this wiring predates the measurement window). G2 is still unreachable from any command.
- [partial] Surface from 2-4 edge curves (EdgeSrf / NetworkSrf) — kernel `NurbsSurface::CoonsPatch` reproduces all four boundaries exactly and orients them automatically, wired into NetworkSrf (EdgeSrf is an alias). Partial because only the 4-curve case is exact: 2- and 3-curve cases, and 4-curve cases where CoonsPatch fails, fall back to sample-and-refit.

**Kernel: SubD & mesh kernel support** (subd_mesh):
- [partial] SubD -> NURBS patch conversion — `ToNurbsPatches` gives an exact bicubic limit patch on regular faces and a flat bilinear patch on irregular ones. `ToNurbsPatchesAdaptive(max_adaptive_levels)` splits irregular faces recursively, turning 3 of 4 quadrants exact per level and shrinking the remaining flat quadrant around the extraordinary vertex. Still partial: no Stam eigenbasis or Gregory construction (a flat patch always remains at the extraordinary vertex), adaptive siblings leave T-junction naked edges, level-0 n-gon faces are skipped, and faces next to a semi-sharp-creased edge are treated as regular and wrongly flagged exact=true. The app's ToNURBS still uses only the non-adaptive `ToNurbsPatches`.
- [missing] Kernel-native SubD local edit operators (insert edge, extrude face, spin edge, weld, expand) — `kernel::SubD` still has none of these; they remain app-only commands.
- [missing] SubD boolean operations — no SubD code path in the kernel's boolean sources at all.
- [partial] SubD from NURBS/B-rep conversion (reverse of ToNurbsPatches) — UPGRADED from missing: `SubD::FromNurbsSurface(surface, u_div, v_div)` samples a single untrimmed surface into a grid of quad control points. Still partial: it is an approximation (control points are surface samples, the limit surface does not interpolate them), handles one surface only (no trims, no Brep, no crease/face matching), and is not wired into the app.
- [partial] SubD symmetry / mirror-in-place — UPGRADED from missing: `SubD::Transform` accepts a mirror transform, but by its own documentation leaves the result inside-out, with no SubD flip, no seam weld and no live symmetry constraint.
- [partial] SubD non-manifold / multi-body validity checks — UPGRADED from missing: `SubD::IsValid` calls OpenNURBS' structural `ON_SubD::IsValid`, but returns a bool only with no report of what is wrong or where, and no specific non-manifold or multi-body check; only tested on an empty and a freshly-built-valid SubD, not genuinely corrupt input.
- [partial] SubD display-level control at kernel level (adaptive limit detail) — UPGRADED from missing: `EvaluateFace(face_id, u, v, max_adaptive_levels)` and `ToNurbsPatchesAdaptive` give kernel-level adaptive limit refinement concentrated around extraordinary vertices. Still partial: no single SubD tessellate(tolerance) or view-dependent API, and each refinement level clones and globally subdivides the whole net.
- [missing] Quad-remeshing of an arbitrary mesh into a clean SubD-ready cage — the kernel has no quad-dominant remesher; the app's QuadRemesh is app-only.
- [partial] SubD extraordinary-vertex limit-tangent quality (creases at poles) — limit positions and per-sector normals at vertices are exact, and `EvaluateFace` gives exact face-interior values away from the extraordinary quadrant with monotone convergence toward it. Still partial: at the extraordinary vertex itself, tangent_u/tangent_v are returned as the zero vector (no eigenbasis tangent plane); points near the extraordinary vertex fall back to the flat interpolant once the level budget is exhausted; convergence is checked against a deeper adaptive reference, not a closed-form limit; semi-sharp edges are ignored by the exact paths.

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

Only 17 non-merge commits touch `dino8-app` in this window, and only four add
user-facing behaviour (exact `CoonsPatch` in NetworkSrf and `UnrollDevelopable`
in UnrollSrf; `ExtendSrf` Type=Linear; a typed-Box-height input fix; a Windows
Redo-crash fix). The rest is CI/test plumbing. Nearly every score change below
therefore comes from re-checking code that was already there against a
stricter, from-scratch reconstruction of the category's "present" items — the
2026-09-24 map, like the kernel side, never wrote its present items down. None
of the new kernel-side capabilities (Model::AddLayer/AddLinetype,
Brep::Sweep2, OffsetApproximate, SewTJunctions, Brep::Torus, PipeVariable,
kernel PLY, Loft tangency) are yet called from `dino8-app`; the app still
writes `.3dm` through its own `src/io/File3dm.cpp` and still runs booleans,
most fillets and most sweep/loft/pipe commands through its own older,
mesh-approximate paths.

### App category gaps (missing / partial items, with evidence)

**Dino 8: Command system & core commands** (app_commands):
- [partial] Command aliases and shortcut customization — Rhino's default aliases are built in and users can add aliases through the Alias command or panel, but user aliases live only in an in-memory map with no persistence to disk, and there is no user-assignable keyboard-shortcut table.
- [partial] Surface construction commands — Loft, Revolve, Extrude, EdgeSrf, Sweep1, Sweep2 and NetworkSrf all exist, but Patch is "planar patch only", Sweep1/Sweep2 are "approximated" RMF-lofted fits with no tolerance control, and NetworkSrf's 3-curve case still falls back to a fitted bilinear Coons patch. The 4-curve case now uses the kernel's exact `CoonsPatch`, and ExtendSrf now has a Linear type via the kernel's exact `ExtendLinear`.
- [partial] Solid editing with B-rep results (booleans, fillet, shell, offset) — BooleanUnion/Difference/Intersection convert every operand, NURBS Breps included, to a mesh before combining; the kernel's exact `BooleanCombineMixed` is never called from the app. FilletEdge is exact only when both adjacent faces are planar, else a mesh fallback. Shell, OffsetSrf on polysurfaces, and Pipe Cap=Yes all give meshes. Solid primitives themselves are real Breps.
- [partial] History / associative re-execution — real History/RecordHistory/UpdateHistory exist, but only for Extrude, ExtrudeCrvToPoint, Revolve, Loft and SubDLoft; stale stubs elsewhere in the app still print "no construction history is recorded" and are asserted on by a smoke test, contradicting the real mechanism.
- [partial] Command-level feature editing (re-running a construction with new inputs) — only scoped, explicit-recompute mechanisms exist (UpdateHistory, hole features, a few UpdateDimensions/UpdateBakes commands); no universal parametric feature tree.
- [partial] VBA-style macro recorder and editor — a macro editor does exist (a multi-line panel with Run/Copy, `;`-separated command sequences, command-file playback, plus a Lua script editor), corrected from a prior "missing" finding. Still missing: any action recorder, any VBA/object-model compatibility, and persistence (the macro buffer is a static in-memory array).
- [missing] AutoLISP-equivalent command scripting language — no LISP dialect or AutoLISP compatibility anywhere; scripting is Lua, Python or Macro/command files.
- [missing] ObjectARX-equivalent native extension API — the only native API is a Dino-specific plugin ABI, not an ObjectARX-compatible binary interface.

**Dino 8: 2D drafting, annotation & documentation** (app_drafting):
- [partial] Associative annotation updating (dimensions, leaders, center marks, center lines) — UpdateDimensions rebuilds several dimension/leader/mark types from their anchors, but only on an explicit command run (nothing hooks document edits), and a point is anchored only if it coincides exactly with a Point object or curve endpoint.
- [partial] Print and plot output — Print writes a vector PDF/SVG of the active view with an optional scale, but there are no lineweights, no print widths, and no plot styles (CTB/STB); no printer-device output.
- [partial] Dynamic blocks — only visibility states exist (BlockAddState/BlockSetVisibility); stretch, flip, array and lookup parameters and actions are not attempted.
- [partial] Live external data linking into tables — a two-way CSV sync with conflict refusal, not native .xlsx; formula cells come back as their last saved values.
- [partial] Dimension styles — correction to prior evidence: named styles do exist (AnnotationStyles etc., persisted in .3dm user strings), but a style has only name, text_height, arrow_size and font — no units/precision, tolerance, extension-line or text-placement control, and text/dimension styles share one table.
- [missing] Field text (text driven by object properties) — no field or formula text type found anywhere; all text is static baked geometry.

**Dino 8: Viewport display, rendering & visualization** (app_display):
- [partial] Environments and image-based lighting — Solid/Gradient/Sky/Image backgrounds all exist, but the image background is only a full-viewport texture with no reflection/lighting contribution, and there is no HDRI lighting or .hdr/.exr loader.
- [partial] Per-object display mode override — only Wireframe and Shaded are supported per-object; every other mode is viewport-wide only.
- [partial] View-dependent adaptive tessellation — real frustum culling exists, but there is still no LOD and no re-tessellation on zoom.
- [missing] Real-time shadow maps in the rasterized renderer — correction to prior evidence: the rasterizer does have shadow-related code, but it is only ground-plane contact-shadow blobs, not shadow maps, self-shadowing, or object-on-object cast shadows. Real cast shadows appear only in the GPU raytraced and CPU path-traced modes.
- [missing] SSAO in the rasterized renderer — no ambient-occlusion code anywhere in the rasterizer.

**Dino 8: Scripting, automation & visual programming** (app_scripting):
- [partial] Embedded Python 3 — refute-the-presents finding: the Python binding module exists, but `DINO8_ENABLE_PYTHON` defaults OFF on Windows builds, so shipped Windows builds have no Python at all; mid-script prompts are also missing.
- [partial] Python API breadth — correction to prior evidence: `RunCommand` does reach every registered command; the real gap is the object model (56 bindings versus Lua's 160 `rs.*` functions) and no interactive prompts.
- [partial] Headless/batch scripting mode — UPGRADED from missing: correction to prior evidence — `main.cpp` does support a documented `--smoke N --script FILE [--screenshot]` headless mode that runs a script file and exits. Still partial: it still needs a GL context and display server, and is framed as a QA mode, not a supported batch product.
- [missing] Cloud/network compute service (Rhino.Compute equivalent) — no server/socket/HTTP code anywhere in the source.
- [missing] AI-assisted modeling or scripting — no neural/inference code anywhere; the one "smart" feature explicitly documents its own technique as not machine learning.

**Dino 8: File I/O & interoperability (app level)** (app_interop):
- [partial] Native .3dm read/write — refute-the-presents finding: the reader converts only lights, clipping planes, detail views, points, curves, Breps, surfaces, meshes, SubDs, extrusions and point clouds — everything else, including every annotation, hatch, text dot and block instance from a real Rhino file, is silently skipped on open. Dino-written annotations/blocks survive only as baked geometry plus private user-string metadata.
- [partial] OBJ — the importer loads the whole file as one mesh with no per-group/per-object split and no .mtl; the exporter tessellates and merges everything into a single welded mesh, losing object identity and writing no materials or curves.
- [partial] STEP AP203/AP214 — the writer and reader exist for basic B-rep entities, but the reader has no assembly structure at all (no NEXT_ASSEMBLY/MAPPED_ITEM/context handling), so multi-part assemblies lose their part placement transforms.
- [partial] DXF — the reader covers TEXT/MTEXT/ELLIPSE/SPLINE/POLYLINE/3DFACE/HATCH/DIMENSION, but the writer emits only polylines, splines, lines, circles, arcs, 3dfaces and points — no TEXT, MTEXT, DIMENSION, HATCH, INSERT or layout entities, so Dino-authored drawings reach AutoCAD as plain curves.
- [partial] DWG (via GPLv3 GNU LibreDWG) — the importer reads a broad entity set including text, dimensions, hatches and inserts (more than the project's own docs currently claim), but the exporter round-trips through a temporary DXF and inherits every DXF-writer limit above; no 3DSOLID entities in either direction.
- [missing] STEP AP242 — the writer emits AP214 only, with no AP242 fixture, test, or PMI/TESSELLATED handler.
- [missing] Parasolid (.x_t/.x_b) import/export — nothing found; documented as out of scope. (Infeasible — see below.)
- [missing] ACIS (.sat/.sab) import/export — nothing found; documented as out of scope. (Infeasible — see below.)
- [missing] Digital signing of exported files — DOWNGRADED from partial: no file-signing code exists anywhere; the project's only signing plumbing is inert installer code-signing in CI, a different thing entirely.
- [missing] Point-cloud exchange formats (LAS/E57/PTS/XYZ) — none; point clouds only round-trip through .3dm.
- [missing] IFC (BIM) import/export — nothing found under the I/O sources.
- [missing] JT (PLM interchange) import/export — nothing found under the I/O sources.

**Dino 8: SubD & mesh modeling toolset (app level)** (app_subd_mesh):
- [partial] SubD to NURBS (ToNURBS) — refute-the-presents finding: faces touching an extraordinary vertex, crease or boundary become flat bilinear approximations and are "deliberately left unjoined" — a converted SubDBox stays an open Brep, not a closed solid. The newer kernel adaptive converter is not wired into this command.
- [partial] NURBS/Brep to SubD — UPGRADED from missing (the prior evidence only checked one file): ToSubD is registered for meshes or polysurfaces, tessellating the Brep at display tolerance and using that triangle mesh as the SubD control cage, with no shape-fidelity guarantee and no test coverage.
- [partial] SubD symmetry (Reflect / Symmetry) — Reflect is a one-time mirror that welds the original and its mirror image into a single mesh, so a SubD input stops being a SubD; there is no live mirror editing.
- [missing] SubD booleans — no SubD-aware boolean exists; existing boolean commands accept SubD objects only because they get tessellated first, producing a mesh, not a SubD.
- [missing] Sculpting (multi-resolution brush sculpting) — no such tool exists (Rhino 8 does not have this either).

**Dino 8: UI/UX, accessibility & localization** (app_ux):
- [partial] Breadth of localization (10+ languages, professional review) — only Spanish and French exist beside English, and a fresh key-count check found French is now 178/183 keys (not the complete 176/176 the project's own audit still claims); several newer UI-chrome keys are untranslated in both languages.
- [partial] Worksessions (shared multi-file referencing) — UPGRADED from missing (the prior evidence was simply wrong): a real Worksession mechanism exists, attaching other .3dm files as locked reference models with filtering and a saved JSON session file. Still partial: attached objects are copied in with no live link or refresh, unlike Rhino's worksessions.
- [missing] Screen-reader support — still explicitly documented as not implemented; the UI toolkit exposes no platform accessibility tree. (Infeasible — see below.)
- [missing] Localized command and toolbar help text — the ~1055 command names/help texts and toolbar tooltips remain English-only in every language.
- [missing] Video tutorials / community forum — needs an audience and hosting, not source-tree work. (Infeasible — see below.)
- [missing] Real-time multi-user collaborative editing — single-document, single-user desktop app; no network code found anywhere.

**Dino 8: Ecosystem, trust, cloud/AI & platform reach** (app_ecosystem):
- [partial] Large-scale adversarial/property-based QA — a real fuzz-test ctest target and several adversarial scripts exist, but this does not substitute for decades of real user files, and Windows-only numeric-difference issues are still being worked through.
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
| 4 | kernel | booleans | AutoCAD-style INTERFERE (real overlap solids, not just Clash report) | missing | medium | Clash's triangle-triangle detection already exists; needs solid construction from the overlap. |
| 5 | kernel | blending | Conic / rho (chordal, elliptical) blend cross-sections | missing | medium | Closes a real, verified gap in Blending & chamfering. |
| 6 | kernel | blending | Alternative blend rail types (distance-from-edge, distance-between-rails) | missing | medium | Closes a real, verified gap in Blending & chamfering. |
| 7 | kernel | topology | Sliver / degenerate micro-face removal — fix the Check() false-positive first | partial | small | `Brep::Check()` currently deletes valid faces on the kernel's own primitives; fixing the sampling bug in `SampleLoop` closes both this and the healing-category items below. |
| 8 | kernel | healing | Degenerate face removal (B-rep) — same Check() false-positive root cause | partial | small | Same underlying fix as #7; currently destructive on Box()/Extrude()/Revolve() results. |
| 9 | kernel | healing | Sliver face removal (B-rep) — same Check() false-positive root cause | partial | small | Same underlying fix as #7. |
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
| 30 | kernel | features | Split body with an arbitrary surface / solid cutter | missing | large | The only solid split is by a plane. |
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
- [kernel/booleans] AutoCAD-style INTERFERE (missing)
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
- [kernel/features] Split body with an arbitrary surface / solid cutter (missing)
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
| Fix `Brep::Check()`'s `SampleLoop` false-positive on straight-parameter trims of curved faces (unblocks the two RemoveDegenerateFaces/RemoveSliverFaces items below) | kernel/healing, kernel/topology | `dino8-kernel/src/brep.cpp` (SampleLoop, Check) |
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
