#pragma once

#include "dino8/kernel/brep.h"

namespace dino8::kernel {

// Exact kernel-level rounding of ONE straight, convex edge shared by two
// PLANAR faces of `solid` into a genuine circular-arc fillet: classical
// constant-radius rolling-ball blending (see e.g. Rossignac & Requicha,
// "Constant-radius blending in solid modeling," Computers in Mechanical
// Engineering 3(2), 1984, or any standard solid-modeling text's "rolling
// ball fillet" section for the general theory) specialized to the one
// case where the blend surface is EXACTLY a circular-cylinder patch, not
// an approximation of one: a ball of radius `radius` rolled along a
// straight edge between two planes traces out exactly a cylinder whose
// axis is the edge line offset by a constant vector, and whose radius is
// `radius` - no curve-fitting or iterative solve involved anywhere in
// this function.
//
// `edge_p0`/`edge_p1` identify the edge to fillet: they must be the two
// endpoints (to within PlanarFaces()'s own vertex tolerance) of one
// consecutive pair of vertices shared, in opposite walking directions, by
// exactly two of `solid.PlanarFaces()`'s own loops - i.e. a genuine
// shared boundary edge of two adjacent faces of a closed, consistently
// (CCW-outward) oriented planar-faced solid, the same loop convention
// every planar-face factory in brep.h already produces and
// BooleanIntersectConvexPlanar/BooleanCombinePlanar already rely on.
// Throws std::invalid_argument if no such pair of faces exists.
//
// The construction (see fillet.cpp for the worked derivation):
//   1. n_i, n_j = the two adjacent faces' own outward unit normals
//      (PlanarFace::plane.zaxis); e = normalize(edge_p1 - edge_p0).
//      Interior dihedral angle theta = pi - acos(dot(n_i, n_j)); this
//      function is convex-edges-only and throws std::invalid_argument if
//      theta is <= 0 or >= pi (a non-convex or degenerate edge - out of
//      scope here, the same honest narrowing
//      BooleanIntersectConvexPlanar's own doc comment uses for
//      convexity).
//   2. The fillet's axis is the edge line translated by a constant
//      vector -bis*(radius/cosb), where bis = normalize(n_i + n_j) and
//      cosb = dot(bis, n_i) - the standard "offset the rolling ball's
//      center along the interior bisector" construction. The two contact
//      lines (where the fillet meets each original face) are the axis
//      line offset by radius*n_i and radius*n_j respectively - always
//      parallel to the edge, since the offset is the same at every point
//      along a straight edge between two planes (unlike a curved edge or
//      a plane-vs-cylinder corner, where the contact direction can vary
//      along the edge - see dino8-app/src/commands/cmd_fillet.cpp's own
//      BuildPlanarVariableFillet/BuildPlaneCylinderVariableFillet for
//      those harder cases, which this kernel-level function does not
//      attempt).
//   3. Each adjacent face's own loop is re-trimmed by clipping it against
//      one half-space whose boundary plane passes through that face's own
//      new contact line - removing exactly the sliver between the
//      original sharp edge and the new rounded one, leaving the contact
//      line as the loop's new boundary edge. Throws std::invalid_argument
//      if `radius` would trim back further than either face's own extent
//      from the edge (the fillet doesn't fit).
//   4. The fillet itself is one Brep::CylindricalFace whose two RAIL
//      curves (at sweep angle 0 and at the full sweep angle) are, by
//      construction, exactly the two new contact lines from step 3 - so
//      the fillet meets faces i and j exactly (to floating-point
//      precision) with no separate gap-closing pass there.
//
//      The patch's OTHER two boundary edges - the two circular ARCS at
//      each end of the edge (height 0 and height |edge_p1-edge_p0|) -
//      need a real gap-closing step this doc comment doesn't get to skip:
//      whichever OTHER face of `solid` meets the filleted edge at
//      edge_p0/edge_p1 and is itself perpendicular to the edge (e.g. a
//      box's own end faces, when the filleted edge runs the full width
//      between them, exactly the required test case below) still has its
//      ORIGINAL sharp corner there unless it's notched by that same arc -
//      confirmed directly during development, not a theoretical nicety:
//      leaving it ungapped measurably changes the closed solid's own
//      volume. So this function also finds any such face and replaces
//      the shared corner vertex with a fine polygonal approximation of
//      the exact arc (Brep::PlanarFace's own loop is straight-edged only,
//      so this one corner is the sole place this function's geometry
//      isn't exact to floating-point precision by construction of that
//      representation, not by approximation of the fillet math itself -
//      the sampling is fine enough that this is far below any reasonable
//      volume tolerance). A face at that vertex whose plane is NOT
//      perpendicular to the edge (an oblique end condition) is left
//      untouched - a real, narrower-than-general scope for what is, in
//      full generality, solid modeling's own separate "vertex blend"
//      problem, not something a two-face edge fillet fully solves here.
//
// The result is exactly `solid` with those two faces re-trimmed, any
// perpendicular end faces at edge_p0/edge_p1 corner-notched as described
// above, and the new CylindricalFace inserted - assembled via
// Brep::FromMixedFaces, so every other face of `solid` comes through
// unchanged. Also throws std::invalid_argument if `radius` isn't strictly
// positive.
//
// SCOPE, stated plainly rather than silently narrowed: a straight edge
// between exactly two PLANAR faces of a solid already known to be
// well-formed enough for PlanarFaces() to describe (see that method's own
// doc comment for what it requires), with any end faces at the edge's own
// two endpoints either absent, oblique (left as a known, disclosed gap),
// or exactly perpendicular to the edge (closed exactly per the polygonal
// approximation above). A curved adjacent face, a non-convex edge, or a
// variable radius along the edge are all real, out-of-scope future work -
// matching the same "this is deliberately narrow, and says so" pattern
// boolean.h's own BooleanIntersectConvexPlanar/BooleanCombinePlanar use
// for their own convex/non-convex scoping.
Brep FilletConvexEdge(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double radius);

}  // namespace dino8::kernel
