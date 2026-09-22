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
Mesh TessellateGeneralBooleanClosedMesh(const Brep& result, int u_divisions = 8, int v_divisions = 8);

}  // namespace dino8::kernel
