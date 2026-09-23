// General boundary-evaluation boolean engine.
//
// Unlike boolean.cpp's BooleanCombineMixed (which is hand-solved,
// closed-form geometry enumerated per surface-TYPE-pair: plane+plane,
// plane+cylinder, cylinder+cylinder, ...), BooleanCombineGeneral works on
// ANY pair of ON_Surface-based faces by actually running the general
// surface/surface intersector (dino8/kernel/surface_intersect.h) between
// every (bbox-overlapping) face pair, splitting each face's own trim loop
// along the resulting intersection curves, classifying each fragment
// in/out of the other solid by ray-casting (via the general curve/surface
// intersector, not hand-solved ray-vs-plane/ray-vs-cylinder formulas), and
// reassembling the kept fragments into a genuine ON_Brep (real
// ON_BrepVertex/ON_BrepEdge/ON_BrepLoop/ON_BrepTrim topology, coincident
// points welded into shared vertices/edges - the same identity mechanism
// Brep::FromMixedFaces() already uses, just generalized to an arbitrary
// ON_Surface instead of only a plane/cylinder/cone).
//
// Scope/limitations (see boolean_general.cpp's own top comment for the
// full disclosure): a face is expected to carry at most one "outer"
// intersection component per opposing face pair that either (a) closes
// entirely inside the face's own trim (a closed loop - becomes a hole in
// the untouched fragment plus a separate interior fragment), or (b) meets
// the face's own trim boundary at exactly two points (an open arc - splits
// that trim boundary into two fragments there). Faces are assumed genus-0
// (no pre-existing holes) two-shell solids. Multiple non-interacting
// chains on the same face are supported (each is spliced in turn); chains
// that cross EACH OTHER on the same face are not.
#pragma once

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

Brep BooleanCombineGeneral(const Brep& a, const Brep& b, BooleanOp op);

// A purely additive, opt-in sibling of Brep::TessellateToClosedMesh()/
// TessellateToClosedMeshConforming(), scoped ONLY to BooleanCombineGeneral's
// own results, that closes the mesh-watertightness gap this file's own
// top-of-file doc comment discloses: `result`'s own faces are tessellated
// via Brep::Tessellate(u_divisions, v_divisions) - the exact same shared,
// unmodified grid-clip tessellator BooleanCombineMixed also depends on, not
// touched by this function at all - then, before welding, every boundary
// edge of one face's own tessellation that another face's tessellation
// happens to have an extra, un-partnered vertex sitting exactly on (this
// engine's own dense straight-segment polyline edges - see this file's own
// top comment - get resampled at different densities by the two adjacent
// faces' independent (u, v) grids, since neither Tessellate() nor
// TessellateConforming() has ever matched a general trimmed face's own
// polyline boundary the way TessellateConforming()'s existing analytic-
// curve/plain-quad passes match theirs) is re-triangulated as a fan through
// that extra vertex, so the two sides' boundary vertex sets agree exactly
// before Mesh::MergeAndWeld() runs. Also drops any resulting zero-area
// (degenerate, repeated-vertex) triangle - a separate, pre-existing
// grid-clip artifact (near a surface's own singular point, e.g. a
// sphere's pole, but also - confirmed directly - at an ordinary planar
// trim corner where two cut boundaries meet) that otherwise leaves
// spurious zero-length "edges" behind. This drop runs BOTH before and
// after the T-junction stitching pass above, not only after: dropping a
// boundary-line sliver strands its two OTHER edges - which the sliver
// had "claimed" as internal, so the stitcher never offered them a
// cross-face partner - as fresh, unmatched boundary edges once the
// sliver disappears, unless it is gone before the stitcher ever sees it.
// tests/general_boolean_sweep.cpp's own 76-combination measurement
// (BooleanCombineGeneral over 19 primitive-pair cases x 4 ops, at
// u_divisions=32/v_divisions=128) went from 0/76 to 14/76 genuinely
// Mesh::IsClosedManifold() from this reordering alone - every
// axis-aligned-planar case (box+box, disjoint/touching/rotated box
// pairs) now closes at any division count tried (8x8 through 32x128).
// The other 62 all pair a curved face (cylinder/cone/sphere/torus)
// against another face along a curved or skew intersection: each side's
// own grid-clip tessellation approximates that shared curve with its own
// independently-sampled dense polyline (not shared sample points, unlike
// a straight box edge, where both sides' clip points genuinely coincide
// once the sliver above stops hiding them) - a materially larger gap.
//
// FOLLOW-UP SESSION: real-edge-topology-conforming reconciliation, the
// technique TessellateToClosedMeshConforming() already uses for
// BooleanCombineMixed/Planar, adapted to this engine (see
// ReconcileEdgeTopology() in boolean_general.cpp's own implementation
// comments for the full root-cause/fix writeup). Walks `result`'s own
// genuine ON_BrepEdge topology and, for every interior (two-face) edge,
// reconciles its two adjacent faces' raw tessellation boundaries to one
// shared, chord-snapped point set BEFORE the plain point-matching pass
// above (StitchTJunctionsOnce(), kept unmodified as its fallback). Went
// from 14/76 to 15/76 - box+box (second box rotated 30deg about z)
// Intersection newly closes, box+box's own axis-aligned Union/A-B/B-A and
// every previously-closing case stay closed and volume-correct. The
// dominant remaining curved-vs-curved gap (box+cylinder etc.) is now
// root-caused two levels deep, both still open:
//   (1) NurbsSurface::TessellateGridClippedExact() can silently DROP a
//       genuine trim-polygon vertex outright under certain grid-alignment
//       degeneracies (confirmed: a shared cut circle sitting exactly on a
//       v-grid line loses roughly half its polyline vertices on the
//       curved side's own raw tessellation, not merely mis-sampling it).
//       An experimental same-session fix (inserting the missing vertex by
//       splitting whichever existing boundary edge it lies on) measurably
//       helped (box+cylinder Union: 1327 -> 808 unmatched boundary edges)
//       and passed the FULL existing test suite with zero failures, but
//       added enough runtime cost (an O(this face's own vertex count)
//       repair scan, worst-case per failed edge) that it was reverted
//       rather than shipped without a confirmed, complete 76-case
//       re-measurement and a cheaper repair-lookup - a concrete, laid-out
//       next increment, not a dead end.
//
//       ROOT-CAUSED (a later session, surface.cpp): the drop is
//       ClipConvex's (Sutherland-Hodgman) own strict `>= 0.0` half-plane
//       test losing a coin-flip to ordinary floating-point noise, over
//       and over. A trim_polygon boundary that is genuinely straight in
//       (u, v) (the common case here: a curved face cut by a planar face,
//       so the cut is dead straight since v is literally height) still
//       arrives as MANY near-duplicate collinear vertices, not one clip
//       edge - BuildLoop() (boolean_general.cpp) resamples every original
//       chain segment at up to ~samples_per_edge points regardless of
//       curvature, and each point is only Newton-refined to the
//       intersecting surfaces' own convergence tolerance (confirmed
//       directly: up to ~1e-11 (u, v)-unit jitter between neighbors on
//       this exact case, box+cylinder Union's cylinder-wall trim). A grid
//       cell corner sitting exactly on that line then gets tested against
//       dozens of near-duplicate copies of essentially the same infinite
//       line in a row, each with its own independent jitter; about half
//       of those redundant tests land the point a hair on the wrong side
//       by pure noise, and ONE wrong verdict anywhere in the sequence
//       drops the point for good (Sutherland-Hodgman only ever narrows
//       the clipped result). Confirmed by direct reproduction: the
//       cylinder wall's own cut-boundary trim_polygon at u_divisions=8/
//       v_divisions=32 (box+cylinder Union) carries 104 vertices for what
//       is geometrically a 4-corner rectangle.
//
//       A first fix attempt loosened ClipConvex's own inside test to a
//       small (u, v)-distance tolerance instead - REJECTED after direct
//       measurement: it also papers over a genuinely different, and
//       genuinely degenerate, case (two DIFFERENT real boundaries, e.g. a
//       cut landing exactly on a face's own UNTOUCHED domain edge, not
//       redundant copies of the SAME boundary) by manufacturing a
//       sliver's worth of real extra area there, which showed up as new
//       NONMANIFOLD (not boundary) mesh edges and broke previously-exact,
//       purely-planar box+box Intersection/A-B/B-A (15/76 -> 12/76).
//       Shipped fix instead: SimplifyCollinearRuns(), a single O(trim
//       size) pass (once per TessellateGridClippedExact call, not per
//       grid cell) that collapses a run of consecutive trim_polygon
//       vertices collinear with their own immediate original neighbors
//       (within this file's existing kDuplicatePointEpsilon-scale (u, v)
//       floor) down to that run's own two endpoints - removing the
//       REDUNDANCY that causes the noise-driven coin flip, rather than
//       loosening the test itself, so it cannot manufacture new area at
//       an unrelated two-boundary coincidence. Confirmed: collapses that
//       same 104-vertex trim down to its true 4 corners; full 76-case
//       sweep and full ctest suite both green, zero regressions anywhere
//       (including the box+box cases the first attempt broke).
//
//       Measured impact is real but SMALL, not the hoped-for large one:
//       box+cylinder Union/Intersection/Difference's own naked-boundary-
//       edge count (TessellateGeneralBooleanClosedMesh, DiagnoseManifold
//       in scratch_test.cpp) each drop by ~1% (380->378, 234->231,
//       252->249); the sweep's own aggregate closedmesh count does not
//       move (still 15/76). Root-caused why, not just observed, by
//       instrumenting ReconcileEdgeTopology (boolean_general.cpp)
//       directly: every one of the 224 short polyline segments making up
//       box+cylinder Union's own z=-1 cut circle fails its WALL-side
//       match (`ok_b=0`) while its box-face side matches fine (`ok_a=1`).
//       The wall's own GridClippedExact tessellation only ever places
//       boundary vertices at u_divisions grid-corner resolution along
//       that straight cut (now, correctly, just as many as the geometry
//       needs - see the fix above); the box face's own boundary is built
//       from the ORIGINAL, much finer BuildLoop() polyline. Reconcile-
//       EdgeTopology processes the shared boundary one ORIGINAL fine
//       segment (one ON_BrepEdge) at a time and can only RELOCATE an
//       EXISTING boundary vertex on each side to a shared chord fraction
//       - it cannot manufacture a wall-side vertex that TessellateGrid-
//       ClippedExact's own coarser grid never produced in the first
//       place, so a fine segment whose endpoints fall between two of the
//       wall's (far sparser) grid corners has no matching run on that
//       side and is left for StitchTJunctionsOnce()'s coarser fallback,
//       which the wall's curvature-bowed-off-chord geometry (this file's
//       own earlier session) already defeats. This is a SEPARATE,
//       deeper gap than (1) - a resolution mismatch between two
//       independently-chosen sampling densities, not a vertex being
//       dropped - one level up from TessellateGridClippedExact, in
//       either ReconcileEdgeTopology's own per-edge walk (boolean_
//       general.cpp) or in giving TessellateGridClippedExact a way to
//       honor a denser trim boundary's own intermediate points along a
//       straight cut, not just its two endpoints, when the grid is
//       coarser than the trim. A concrete next-increment target, not
//       explored further this session.
//   (2) A SEPARATE, larger structural gap, found while isolating (1)'s
//       own residual: an "untouched" operand face BooleanCombineGeneral
//       keeps wholesale (no intersection curve touches it at all, e.g. a
//       cylinder's own end cap once the cut only touches its wall) did
//       NOT get welded through the same VertexWelder/BuildLoop() identity
//       mechanism a freshly-cut NEIGHBORING fragment does - so the two
//       shared no real ON_BrepEdge at all, even though they meet at
//       identical 3D points (this file's own "friendless cylindrical
//       band" notch precedent, brep.cpp/brep.h, is the same shape of gap
//       one level up).
//
//       CLOSED (a later session): root-caused to FaceBoundaryLoop()
//       (boolean_general.cpp) resampling each face's own trim/edge curve
//       at a fixed `samples_per_edge` FRACTION of ITS OWN parameter
//       domain, independently per face - for a boundary two faces
//       genuinely share (e.g. a solid cylinder's disk cap and its own
//       wall, built by Brep::FromMixedFaces() from the SAME dense point
//       ring, confirmed directly), each side's own trim has a DIFFERENT
//       parameterization (the cap's a dense polyline indexed by vertex
//       count, the wall's a plain 2D line whose 3D image is the true
//       isocurve circle indexed by angle), so sampling both at the same
//       `i / samples_per_edge` fraction lands at a different physical
//       angle on each side past the shared endpoint. Added
//       ReconcileFragmentBoundaries(), a NEW pass at fragment-assembly
//       time (BEFORE the real VertexWelder/BuildLoop() pass, not
//       ReconcileEdgeTopology's own tessellation-time one): it welds a
//       throwaway detector over every kept fragment's own loop to find
//       "anchor" positions already coincident with some OTHER fragment,
//       splits each loop into anchor-to-anchor runs (including the
//       degenerate but real "one single anchor, the whole loop is one
//       run back to itself" and "two DIFFERENT positions on one loop that
//       share one anchor vertex, because that loop continues past it
//       toward a completely different neighbor" cases - both measured
//       directly on box+cylinder's own cap/wall boundary), and whenever
//       exactly two runs from two DIFFERENT fragments share an anchor
//       pair AND a multi-probe geometric vote confirms they trace the
//       SAME physical curve (rejecting a same-corner-only false match,
//       also measured directly to occur elsewhere in the sweep), reuses
//       the denser run's own points VERBATIM on the sparser side - so the
//       real welder below is guaranteed to merge them into one shared
//       vertex per point, giving BuildLoop() a genuine, TrimCount()==2
//       ON_BrepEdge to hand ReconcileEdgeTopology afterward, exactly as a
//       chain-cut edge already gets.
//
//       Measured directly on box+cylinder Union (the disclosed fixture
//       above): the result's own naked (TrimCount()==1) ON_BrepEdge count
//       dropped from 120 to 20 - the cap/wall boundary itself now fully
//       shared (0 naked, down from 52 combined) - ON_Brep::IsValid() and
//       the tessellated volume unaffected (both already correct before
//       and after, confirmed by direct measurement, not merely inferred
//       from the edge count). The sweep's own aggregate closedmesh count
//       did NOT move (still 15/76, identical case set, no reshuffling):
//       for box+cylinder specifically, ReconcileEdgeTopology's own
//       DINO8_RECONCILE_DEBUG trace shows it never even walks these now-
//       real cap/wall edges (their own tessellated boundaries already
//       agree with no T-junction to insert - the topology fix genuinely
//       worked), yet DiagnoseManifold's own mesh-boundary count still
//       shows most of its residual sitting exactly at the rim (z = the
//       wall's own v=0/v=length grid lines) - consistent with, not a new
//       instance of, gap (1) above (TessellateGridClippedExact's own
//       grid-alignment vertex-dropping bug, EXPLICITLY out of scope for
//       this session, is worst exactly on a v=const grid line, which a
//       rim by construction always is). This fix stands on its own
//       (confirmed correct in isolation) but needs (1) fixed too, on some
//       later session, before its effect can show up in the aggregate
//       closedmesh count for a curved-vs-curved case like this one.
//
//   (1), continued (a LATER session): the resolution-mismatch gap above -
//       ReconcileEdgeTopology's own per-edge walk cannot manufacture a
//       wall-side vertex that TessellateGridClippedExact's own coarser
//       grid never produced - is closed at the SOURCE instead of at
//       ReconcileEdgeTopology's own layer (the doc comment above laid out
//       both directions; this session investigated both before choosing).
//       Approach (1) - extending ReconcileEdgeTopology's own per-edge walk
//       to INSERT a missing point on the coarser side - turns out not to
//       be viable AS WRITTEN: it processes ONE original fine ON_BrepEdge
//       at a time (a short span between two consecutive BuildLoop()
//       points), and its own walk requires an EXISTING vertex near BOTH
//       endpoints on BOTH sides before it can even start (NearestBoundary-
//       Start). For box+cylinder Union's own 224 fine edges along the cut,
//       the wall's own u_divisions-resolution grid corners are far sparser
//       than the box's fine edges, so MOST fine edges have NEITHER
//       endpoint anywhere near an existing wall vertex - the walk fails
//       to even START, not merely fails to find a clean run. Grouping
//       consecutive fine edges into one coarser reconciliation instead
//       would mean approximating a REAL multi-edge span of the true curve
//       with one much longer chord (a materially worse approximation than
//       the existing per-fine-edge chord), and - more fundamentally -
//       would mean this pass genuinely rewriting real ON_BrepEdge
//       topology mid-tessellation, not just patching a mesh in place.
//       Approach (2) - TessellateGridClippedExact learning about a denser
//       neighbor's own intermediate points along a shared straight cut -
//       turned out to need NO actual cross-face communication at all:
//       Brep::Tessellate()'s own ResolveFace()/SampleLoop() already builds
//       `trim_polygon` by walking EVERY real ON_BrepTrim of a face's own
//       loop and taking one sample per trim (BuildLoop() gives each
//       original fine polyline segment its own trim, and SampleLoop()
//       takes `samples=1` for a linear trim) - so `trim_polygon`, BEFORE
//       SimplifyCollinearRuns() ever runs, ALREADY carries the wall's own
//       full BuildLoop-fine resolution, matching the box side's exactly
//       (confirmed directly: box+cylinder Union's wall trim carries 104
//       points for a 4-corner rectangle, same as 413c0ae's own earlier
//       measurement). SimplifyCollinearRuns() is precisely what erases
//       them again, to protect ClipConvex's own inside test (see its own
//       doc comment above) - the fix is to give TessellateGridClippedExact
//       a way to remember what it erased and put it back afterward,
//       without ever handing ClipConvex the redundant, noise-prone
//       version.
//
//       Shipped: SimplifyCollinearRuns() now also returns every point it
//       dropped, each tagged with the two SURVIVING simplified-trim
//       vertices its own collapsed run sat between (RemovedTrimPoint,
//       surface.cpp) - not just its bare (u, v) position. TessellateGrid-
//       ClippedExact buckets these by which grid cell's own (u, v)
//       rectangle contains each one (O(1) lookup per cell, not O(cells x
//       removed points) - the same performance discipline the earlier-
//       rejected EnsureBoundaryVertex repair was rejected for missing),
//       then a new InsertForcedPointsIntoTriangulation() fans each
//       relevant cell's own forced points into whichever triangle
//       EarClipTriangulate() already gave that boundary span - the SAME
//       "replace one owning triangle with a fan through its apex"
//       technique ReconcileChainToChord (boolean_general.cpp) already
//       uses, just one layer earlier (on a single grid cell's own small
//       polygon, before mesh assembly, not on the whole assembled mesh
//       after it). EarClipTriangulate() itself is deliberately never
//       handed the grown point set - only the cell's own bare
//       grid-clip boundary, however many forced points get fanned in
//       afterward - see that function's own doc comment for why (a real,
//       measured performance regression: feeding EarClipTriangulate() the
//       augmented polygon directly roughly DOUBLED the 76-case sweep's
//       own wall-clock time, 63s -> 125s, before this fix; restored to
//       ~79s with the fan-insertion approach instead - still a real,
//       bounded ~25% increase over the pre-fix baseline, from genuinely
//       reinserting ~15,600 real boundary points across the sweep's own
//       76 cases, not from any remaining algorithmic blowup).
//
//       ONE REAL BUG found and fixed before this was safe to ship: a
//       forced point's bare (u, v) position alone cannot tell "this is
//       the trim's own cut boundary" apart from "an ordinary interior
//       grid-line edge that merely happens to run the SAME direction" -
//       for an AXIS-ALIGNED cut (the common, previously-rock-solid
//       box+box case), the cut's own direction routinely coincides
//       exactly with a plain u=const or v=const cell edge direction. An
//       early version tested only "is this forced point collinear with
//       the candidate cell edge", which happily matched an ordinary
//       shared grid-line edge between two cells - fanning a point into
//       ONE cell's copy of that edge while its untouched neighbor cell
//       kept the un-subdivided original, opening a small crack. Caught by
//       this repo's own ctest suite (dino8_kernel_smoke), NOT the sweep:
//       box+box Union and Difference's own previously-Mesh::IsClosedManifold()
//       TessellateGeneralBooleanClosedMesh() results broke. Fixed by
//       requiring BOTH of the candidate cell edge's own endpoints - not
//       just the forced point itself - to sit on the SAME originating
//       trim edge's own line (RemovedTrimPoint's own `edge_t0`/`edge_t1`,
//       carried from SimplifyCollinearRuns() through bucketing to the
//       fan-insertion check itself): a genuine trim-cut edge segment lies
//       ON that exact line by construction; an ordinary cell edge that
//       merely runs parallel to it does not (unless the cut happens to
//       sit exactly on a grid line, the pre-existing, separately-disclosed
//       degeneracy above - a narrower, real edge case, not this bug's
//       broad failure mode). A second, unrelated attempt at raising the
//       box+cylinder Union nonmanifold-edge count back down (a FOURTH
//       degenerate-triangle drop pass after the final cross-face weld,
//       targeting a handful of weld-time near-duplicate-vertex artifacts)
//       was tried and REVERTED for the same reason as the box+box
//       regression above: it is the LAST pass with nothing after it to
//       re-stitch whatever it strands, so it reopened box+box Union/
//       Difference again (caught the same way, by ctest, not the sweep).
//       Not shipped; see this file's own next-increment note below.
//
//       MEASURED, not assumed: tests/general_boolean_sweep.cpp's own
//       76-case sweep: 15/76 -> 16/76 (box+box, second box rotated
//       30deg, Union newly closes - the sweep's OWN case set, no
//       reshuffling of any previously-closing case). box+cylinder Union's
//       own naked-boundary-edge count (DiagnoseManifold, scratch_test.cpp,
//       this file's own disclosed fixture, u_divisions=8/v_divisions=32):
//       378 -> 334, an honest ~12% reduction, not the full close this
//       gap's own root cause would suggest - see the next-increment note
//       below for exactly what's left. Its own non-manifold-edge count
//       moved 4 -> 10, a real, disclosed, NOT-fixed-this-session side
//       effect - all 6 new ones sit at an UNRELATED location (near the
//       cylinder's own z rim/cap seam, not this fix's own target cut
//       boundary), same general shape (a near-duplicate vertex pair that
//       StitchTJunctionsOnce's own chain insertion leaves a hair's width
//       apart) as the 4 that were ALREADY there before this session,
//       just reshuffled by this fix's own upstream effect on which edges
//       ReconcileEdgeTopology reconciles first. Full dino8-kernel ctest
//       suite (dino8_kernel_smoke, 1663 checks): 100% pass, 147.35s wall
//       clock - matches this suite's normal ~150s+ runtime, no
//       regression (the sweep's own ~25% slowdown above is confined to
//       tests/general_boolean_sweep.cpp, which is deliberately NOT
//       registered with ctest - see its own top comment).
//
//       NEXT INCREMENT (CLOSED, a later session): the weld-time near-
//       duplicate-vertex coincidence above got its real fix at its own
//       source, in StitchTJunctionsOnce's own chain-insertion (boolean_
//       general.cpp), not a triangle drop after the fact - see that
//       function's own doc comment for the full mechanism. DIAGNOSIS
//       (DINO8_RECONCILE_DEBUG plus direct instrumentation of Stitch-
//       TJunctionsOnce itself): box+cylinder Union's z rim (where the
//       cylinder's own wall meets its own cap - a fully PERIODIC boundary)
//       falls entirely to StitchTJunctionsOnce's own fallback, never
//       ReconcileEdgeTopology (confirmed: NearestBoundaryStart fails on
//       the cap side for every one of that rim's real edges - the cap's
//       own genuinely-curved-in-(u,v) grid-clip boundary lands 0.0003-
//       0.001 away from the wall's real edge vertices there, a SEPARATE,
//       not-closed-this-session instance of the resolution-mismatch gap
//       above, this time on a curved rather than straight trim). Because
//       StitchTJunctionsOnce is fed EVERY other face's own vertex as a
//       hit candidate for a boundary edge (not just the true topological
//       neighbor), the cap's denser sampling there routinely contributes
//       several genuinely-distinct-but-mutually-adjacent hits (confirmed:
//       up to 3 within ~0.0003 of each other) that each individually pass
//       PointStrictlyOnSegment (which is blind to the other hits found
//       for the same segment) - fanning them all in as separate vertices
//       produces slivers thin enough that the two faces' boundaries no
//       longer agree which vertex is "the" corner there: a nonmanifold
//       edge, not a mere T-junction.
//
//       FIX: widen StitchTJunctionsOnce's own existing hit-vs-hit de-dup
//       (previously a tiny, fixed `tol`) to the SAME scale-aware distance
//       PointStrictlyOnSegment already uses for its own perpendicular-
//       distance acceptance (floored at `tol`, else a fraction of the
//       segment's own length), at 2x that formula's own coefficient -
//       scoped DELIBERATELY to hit-vs-PRIOR-HIT only, never hit-vs-the-
//       segment's-own-endpoint: an equivalent endpoint-relative version
//       was tried FIRST and REJECTED - even a hit genuinely close to a
//       real endpoint is a normal, often NECESSARY case elsewhere in this
//       engine (this file's own resolution-mismatch forced-point
//       mechanism routinely places one there), and rejecting it broke
//       box+box (second box rotated 30deg about z)'s own previously-
//       closing B-A case in the 76-case sweep at every coefficient tried,
//       including ones far too small to help box+cylinder at all. An
//       equivalent triangle-area-ratio formulation of the same
//       endpoint-relative idea was also tried and also rejected the same
//       way - confirmed directly, on this same fixture, that no single
//       distance or area-ratio threshold cleanly separates "duplicate"
//       from "legitimate" once endpoints are included, since their own
//       scales genuinely overlap. Hit-vs-prior-hit alone has no such
//       conflict and was measured clean up to 20x its own coefficient
//       with no further benefit and no new regression either.
//
//       MEASURED: box+cylinder Union's own nonmanifold-edge count
//       (DiagnoseManifold, scratch_test.cpp, u_divisions=8/v_divisions=
//       32): 10 -> 6 (the 965ee6b session's own 4-edge PRE-regression
//       baseline is not quite reached - of the remaining 6, 4 (at z=-1 and
//       z=1, the wall/box CUT boundary itself) are the SAME 4 edges
//       965ee6b's own disclosure already named as pre-existing there -
//       gap (1)'s own resolution-mismatch residual, not this rim's
//       periodic-seam defect, and UNCHANGED by this session's fix, as
//       expected; the other 2 (still at the z rim) are one more
//       occurrence of this SAME rim defect that hit-vs-prior-hit
//       clustering alone cannot reach, since it is a single isolated hit
//       near a segment's own endpoint, not a mutually-close cluster - the
//       curved-trim resolution-mismatch gap above is this residual's own
//       real next increment, not a further StitchTJunctionsOnce tweak).
//       tests/general_boolean_sweep.cpp's own 76-case sweep: unchanged at
//       16/76, byte-for-byte the same case set (diffed directly) - zero
//       reshuffling. Full dino8-kernel ctest suite (dino8_kernel_smoke,
//       1663 checks): 100% pass, 155.26s wall clock, matching this
//       suite's normal runtime - no regression.
//
//   NEW LEAD (a later session, after the 76-case sweep's own boolean-
//       CORRECTNESS gap - volume/ON_Brep::IsValid()/nonsimple-trim - was
//       separately closed to 76/76): re-measured the closedmesh gap
//       itself (now 17/76) and found every prior fix in this file's own
//       history above targets exactly one failure signature -
//       Mesh::IsClosedManifold()'s own undirected-edge-count check
//       (count != 2: a naked or nonmanifold edge) - never its OTHER,
//       independent check: `orientation_consistent` (a directed edge
//       walked twice - two triangles both claiming the same edge in the
//       SAME winding direction). Added a DINO8_MESH_DEBUG diagnostic to
//       IsClosedManifold() itself (mesh.cpp) and confirmed directly: box+
//       cylinder Union/Intersection/A-B/B-A ALL report
//       orientation_consistent=0, while every currently-CLOSED case
//       (box+box) reports orientation_consistent=1 - this check is
//       genuinely meaningful here, not a chronic false positive.
//
//       Tried the same "insert a bracketing vertex" idea (2) above landed
//       on conceptually, generalized to ReconcileEdgeTopology's own
//       vertex-proximity walk (a new FindBracketingBoundaryEdge/
//       InsertBracketedSpan pair, splitting a coarse boundary edge's own
//       owning triangle when NEITHER of a finer neighbor's shared edge
//       endpoints sits near any vertex on the coarse side at all - the
//       genuinely different case (2)'s own fix above didn't reach, per
//       (1)'s "next-increment" note earlier in this comment). Measured,
//       not shipped: it triggered 16 times on box+cylinder Union but the
//       full 76-case sweep's own output was byte-for-byte UNCHANGED, and
//       box+cylinder Union's own naked-edge count went UP slightly (1177
//       -> 1183) rather than down - reverted rather than ship a change
//       with no verified benefit.
//
//       ISOLATED FURTHER: instrumented TessellateGeneralBooleanClosedMesh
//       itself to merge-and-check `result.Tessellate()`'s own RAW per-face
//       output BEFORE any of this file's own StitchTJunctionsOnce/
//       ReconcileEdgeTopology/ReconcileChainToChord passes run at all.
//       That RAW merge is ALREADY orientation_consistent=0 for box+
//       cylinder Union (bad-edge-count=2696), and stays orientation_
//       consistent=0 after every reconciliation pass runs (bad-edge-count
//       drops to 1177 - real, substantial progress on the naked-edge
//       axis, exactly matching this file's own long history above - but
//       the orientation flag itself never moves). This rules out
//       StitchTJunctionsOnce/ReconcileChainToChord/ReconcileEdgeTopology
//       as the SOURCE of the orientation conflict (their own fan-
//       insertion was independently re-checked by hand for winding
//       preservation and found consistent: every fan triangle is built
//       as {apex, chain[k], chain[k+1]} in the same cyclic order the
//       original triangle's own directed edge already carried) - the
//       true source is upstream, in Brep::Tessellate()'s own per-face
//       generation (a face-level m_bRev/FlipNormals defect on some
//       specific face of a BooleanCombineGeneral result?) or in the raw,
//       topology-blind Mesh::MergeAndWeld() itself. NOT diagnosed further
//       this session - a genuinely fresh, precisely-scoped next-increment
//       target, orthogonal to every naked-edge-count fix documented
//       above.
Mesh TessellateGeneralBooleanClosedMesh(const Brep& result, int u_divisions = 8, int v_divisions = 8);

}  // namespace dino8::kernel
