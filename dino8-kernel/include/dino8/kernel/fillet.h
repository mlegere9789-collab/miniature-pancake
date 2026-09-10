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
//      so this one corner is the sole place this function's VISIBLE
//      geometry isn't exact to floating-point precision by construction
//      of that representation, not by approximation of the fillet math
//      itself - the sampling is fine enough that this is far below any
//      reasonable volume tolerance). This notched run is also given a
//      LITERAL shared ON_BrepEdge with the fillet's own true-arc cap at
//      that same corner (see PlanarFace::notch_begin/notch_count's own
//      doc comment and Brep::FromMixedFaces' own comment for exactly how)
//      - so, unlike the geometry, the TOPOLOGY at this corner is exact:
//      IsManifold() reports no free boundary there and IsSolid() is true
//      for a fillet on an otherwise-closed solid, not merely IsValid().
//      A face at that vertex whose plane is NOT
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

// LINEAR-TAPER generalization of FilletConvexEdge: rolls a ball of
// radius r(t) = radius0 + m*t (t = arc length along the edge from
// edge_p0, m = (radius1 - radius0) / |edge_p1 - edge_p0|) instead of a
// single constant radius. The worked derivation (see also
// Brep::ConicalFace's own doc comment for the resulting surface type):
//
//   1. The tangency construction that places the rolling ball's own
//      center C(t) = edge_p0 + t*e - bis*r(t)/cosb (e, bis, cosb exactly
//      as FilletConvexEdge's own doc comment derives them) never assumed
//      r was constant - it holds for ANY r(t), so the two rail curves
//      rail_i(t) = C(t) + n_i*r(t), rail_j(t) = C(t) + n_j*r(t) are
//      EXACT for a general r(t), always lying exactly in face i's/face
//      j's own plane respectively. This part needs no new theory at all.
//
//   2. For the INTERIOR of the swept patch, the general theory of canal/
//      pipe surfaces (Peternell & Pottmann, "Computing rational
//      parametrizations of canal surfaces," J. Symbolic Computation
//      23(2-3), 1997) says the envelope of a one-parameter sphere family
//      is, for a general r(t), NOT a rational surface at all (a genuine
//      free-form canal surface) - UNLESS r(t) is linear, in which case a
//      much simpler classical fact applies directly: since r(t) is
//      linear, C(t) is ALSO exactly a straight line (C'(t) is constant -
//      a direct, checked consequence of the tangency formula above, not
//      assumed), and the envelope of spheres of linearly-varying radius
//      centered along a straight line is EXACTLY a right circular cone
//      (apex where the linear extrapolation of r(t) hits zero) - the
//      same "spheres inscribed in a cone" fact used to derive a cone's
//      own inscribed-sphere family in elementary solid geometry, here
//      verified directly against the standard canal-surface
//      characteristic-circle formula (not merely asserted): both the
//      characteristic circle's own distance-from-apex-along-axis and its
//      own radius come out exactly linear in t with a COMMON zero at the
//      same apex parameter, so their ratio (tan of the cone's own
//      half-angle) is provably t-independent. See dino8-kernel's own
//      verification tests for the closed-form volume/rail checks this
//      derivation was validated against, sample point by sample point,
//      before being trusted here.
//
//   3. The rolling ball's own radius r(t) is NOT the same number as the
//      cone's own true cross-sectional radius at the matching point -
//      see Brep::ConicalFace's own doc comment for why (they differ by a
//      fixed scale factor whenever m != 0) - this function computes that
//      distinction internally; callers only ever see the physical
//      rolling-ball radii radius0/radius1 in this function's own
//      signature, exactly as FilletConvexEdge's single `radius` is a
//      physical ball radius, not a raw cone parameter.
//
//   4. Each adjacent face is re-trimmed exactly as FilletConvexEdge's own
//      step 3 describes, generalized only in that the cut plane's own
//      in-plane normal is now perpendicular to the TILTED rail direction
//      (which is still, provably, a single straight line per face - see
//      point 1 - just no longer parallel to e once m != 0) instead of
//      perpendicular to e itself.
//
// If `radius1` is within a small relative tolerance of `radius0` (m is
// negligible), this function DISPATCHES to today's FilletConvexEdge
// unchanged, called with radius0 - a genuine code-path dispatch (the
// exact m=0 case is never run through the cone construction as a
// very-flat approximation of it; the cone construction is mathematically
// exact for any m!=0, but "is m exactly/negligibly zero" is a real,
// separate branch, not a numerical-stability workaround).
//
// SCOPE, narrower even than FilletConvexEdge's own already-disclosed one,
// stated plainly rather than silently narrowed: this function does NOT
// attempt the corner-notch construction FilletConvexEdge's own
// NotchCornerAtVertex performs for a third face perpendicular to the
// EDGE at edge_p0/edge_p1. A genuine new finding, not merely an
// unimplemented convenience: once m != 0, the cone's own axis direction
// u = e - (m/cosb)*bis is NOT parallel to e (a real, checked-directly
// consequence of the derivation above: u . n_i = -m != 0, whereas e is
// perpendicular to both n_i and n_j by construction) - so a box-style
// end face that IS perpendicular to e is NOT perpendicular to the cone's
// own axis, meaning its true cross-section there is a planar ELLIPSE
// (the cone sliced by a plane oblique to its own axis), not the fixed
// circular-arc formula NotchCornerAtVertex hardcodes. Reusing that
// machinery unchanged for the tapered case would be silently WRONG, not
// merely unimplemented - so this function explicitly leaves any such
// third face's sharp corner UNTOUCHED, exactly matching FilletConvexEdge's
// own already-established "disclosed, narrower scope" pattern for an
// oblique end face. This is a genuine, honest regression relative to
// FilletConvexEdge's own corner-notch closure for the constant-radius
// case - see this codebase's own verification tests for an explicit,
// documented assertion of this scope limit, not a silently non-manifold
// result. A follow-on increment could close it by generalizing
// NotchCornerAtVertex to splice a sampled ELLIPSE instead of a circle -
// real, bounded future work, deliberately not attempted here.
//
// Also matches FilletConvexEdge's own scope otherwise: a straight edge
// between exactly two PLANAR faces, convex dihedral only, radius0 and
// radius1 both strictly positive (throws std::invalid_argument
// otherwise - a radius reaching exactly zero partway along the edge
// would mean the swept patch's own apex falls INSIDE the trimmed region,
// a genuinely different, degenerate topology this function does not
// attempt), and any end face at edge_p0/edge_p1 is either absent,
// oblique (already a disclosed FilletConvexEdge gap), or perpendicular
// to e (now ALSO left untouched here - see above - rather than
// incorrectly notched). A piecewise-linear multi-segment taper and the
// fully general free-form-radius canal-surface case (point 2 above) are
// both explicitly out of scope for this function.
Brep FilletConvexEdgeTapered(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double radius0,
                              double radius1);

}  // namespace dino8::kernel
