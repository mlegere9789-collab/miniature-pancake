#pragma once

#include <utility>
#include <vector>

#include "dino8/kernel/brep.h"

namespace dino8::kernel {

// One station of a piecewise-linear rolling-ball taper profile along a
// FilletConvexEdgeTapered edge: rolling-ball radius `radius` at arc
// length `t` from edge_p0 (see the N-station FilletConvexEdgeTapered
// overload below for the full construction). `t` is measured exactly the
// same way FilletConvexEdge/the two-radius FilletConvexEdgeTapered
// already measure it internally (arc length along e = normalize(edge_p1
// - edge_p0)), just now exposed to the caller as an explicit station
// list instead of always implicitly {0, L}.
struct FilletRadiusStation {
  double t = 0.0;
  double radius = 0.0;
};

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
//
//      A face at that vertex whose plane is OBLIQUE to the edge (NOW
//      CLOSED, not left untouched): where the perpendicular case's cap is
//      a plain circular arc, an oblique third face cuts the fillet's own
//      circular CYLINDER in a true ELLIPSE (fillet.cpp's own
//      FindObliqueThirdFaceCrossing/EllipseNotchCornerAtVertexCylindrical,
//      reusing detail/ellipse_clip3d.h's own ComputeEllipseFrame3d -
//      already exact and tested for exactly this: an oblique plane's true
//      intersection with a circular cylinder). The construction: each of
//      the fillet's two straight rail lines (radius offset from the edge
//      into face i's/face j's own plane) crosses the oblique face's plane
//      at a single point, generally at TWO DIFFERENT heights along the
//      edge (a linear solve per rail - see FindObliqueThirdFaceCrossing's
//      own doc comment); throws std::invalid_argument if either crossing
//      falls beyond the oblique face's own real extent (the fillet
//      overruns it) or if the oblique plane is asymptotically parallel to
//      the edge. The face-i-side crossing becomes the cylinder's own new
//      end (its frame/length are shifted so this crossing is exactly the
//      flat v=0 or v=length corner, matching
//      Brep::CylindricalFace::cap0_notch_points' own "the first point is
//      always the flat angle-0 corner" contract - inert, a bit-identical
//      no-op, whenever neither end is oblique); the face-j-side crossing
//      becomes that cap's own genuinely SLOPED back point, the same
//      "sloped cut chain" shape that field's own doc comment already
//      anticipates for an unrelated producer (the unequal-radius
//      cylinder/cylinder split), just reached here from a different
//      direction. The dense ellipse sample is spliced into BOTH the
//      oblique face's own notched corner and the CylindricalFace's own
//      cap0_notch_points/cap1_notch_points - a literal shared boundary,
//      not two independently-plausible approximations of the same curve,
//      mirroring FilletConvexEdgeTapered's own already-established
//      principle for its cone case. A face with NO matching trihedral
//      third face at all (a free boundary) is unaffected, exactly as
//      before.
//
// The result is exactly `solid` with those two faces re-trimmed, any
// perpendicular OR oblique end faces at edge_p0/edge_p1 corner-notched as
// described above, and the new CylindricalFace inserted - assembled via
// Brep::FromMixedFaces, so every other face of `solid` comes through
// unchanged. Also throws std::invalid_argument if `radius` isn't strictly
// positive.
//
// SCOPE, stated plainly rather than silently narrowed: a straight edge
// between exactly two PLANAR faces of a solid already known to be
// well-formed enough for PlanarFaces() to describe (see that method's own
// doc comment for what it requires - in particular, a solid already
// carrying a curved face from an earlier fillet is out of scope, since
// PlanarFaces() itself rejects it), with any end faces at the edge's own
// two endpoints either absent, exactly perpendicular to the edge, or
// oblique to it (all three closed exactly, as described above). A curved
// adjacent face, a non-convex edge, or a variable radius along the edge
// are all real, out-of-scope future work - matching the same "this is
// deliberately narrow, and says so" pattern boolean.h's own
// BooleanIntersectConvexPlanar/BooleanCombinePlanar use for their own
// convex/non-convex scoping.
Brep FilletConvexEdge(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double radius);

// CONCAVE (reflex) edge fillet - the rolling ball's OTHER case: ADDS a
// smooth quarter-round to a concave (interior dihedral angle > pi)
// straight edge between two planar faces, instead of cutting a convex one
// away (Parasolid/Rhino would call this the same "fillet edge" operation
// FilletConvexEdge performs, just applied to a reflex rather than a
// convex edge - the two share no dihedral-angle overlap, so they are
// genuinely two functions, not one dispatching on sign).
//
// THE CONSTRUCTION is the exact geometric mirror of FilletConvexEdge's
// own (see that function's own doc comment for the full derivation this
// one flips): the ball sits OUTSIDE the material, in the empty wedge the
// concave edge notches out of it, tangent to both adjacent faces from
// that side. Concretely, with n_i/n_j the two faces' own outward normals,
// bis = normalize(n_i + n_j), cosb = bis . n_i, offset = radius / cosb:
//   axis_point(p) = p + bis*offset (FilletConvexEdge: p - bis*offset) -
//     the ball center moves INTO the empty wedge, not into the material.
//   contact_i(p)/contact_j(p) = axis_point(p) - n_i/n_j * radius
//     (FilletConvexEdge: +) - the tangent point on each face's own plane,
//     reached from the externally-located ball center back toward it.
//   theta = pi - psi (psi = arccos(n_i . n_j)) is UNCHANGED in form from
//     FilletConvexEdge, and still the correct wedge angle for trim_back =
//     radius/tan(theta/2): for a convex edge theta IS the interior
//     material angle; for a concave edge the interior material angle is
//     pi + psi, so the EMPTY wedge being filled here is 2*pi - (pi + psi)
//     = pi - psi - the same expression, a genuine algebraic identity, not
//     a coincidence of any one test angle.
//   the cylinder's own frame uses xaxis = -n_i (FilletConvexEdge: +n_i)
//     so frame.origin + radius*xaxis lands exactly on contact_i; yaxis =
//     e x xaxis, the same construction FilletConvexEdge's own frame uses.
//     `outward = false` (see Brep::CylindricalFace's own doc comment) is
//     then the one remaining bit telling FromMixedFaces() this patch
//     bounds material from the concave side, so its presented normal
//     points radially INWARD, toward the ball center - a genuine
//     boundary-representation outward normal for material that is now
//     OUTSIDE the swept circle rather than inside it.
// The corner-notch end condition (a third face exactly PERPENDICULAR to
// the edge at edge_p0/edge_p1) reuses FilletConvexEdge's own
// NotchCornerAtVertex UNCHANGED: it is already a generic "splice this
// vertex into an arc around axis_pt, from radius*xaxis to
// radius*(cos(sweep_angle)*xaxis + sin(sweep_angle)*yaxis)" operation
// with no convex-specific assumption baked in, so passing this function's
// own (negated) frame.xaxis/frame.yaxis produces the correct OUTWARD-
// bulging notch (ADDING, not cutting, that face's own corner)
// automatically, from the same code the convex case uses to cut one.
//
// An OBLIQUE third face at edge_p0/edge_p1 (not perpendicular to the
// edge) is ALSO closed, reusing FilletConvexEdge's own
// FindObliqueThirdFaceCrossing/EllipseNotchCornerAtVertexCylindrical
// UNCHANGED - both are already generic in `radius`/`frame`/the D_i/D_j
// rail-offset vectors, with no convex-specific assumption baked into
// either. The ONLY thing that needed re-deriving is D_i/D_j themselves:
// contact_i(p) - p is a fixed vector (independent of p, the same fact
// FilletConvexEdge's own doc comment relies on), and for THIS function's
// own contact_i(p) = axis_point(p) - n_i*radius = p + bis*offset -
// n_i*radius, so D_i = bis*offset - n_i*radius - the NEGATION of
// FilletConvexEdge's own D_i = radius*n_i - bis*offset, confirmed by
// direct substitution rather than assumed from the sign pattern
// elsewhere in this derivation. Verified against a genuine oblique
// fixture (the same L-shaped footprint, capped by an oblique plane
// instead of a flat one): the hand-derived crossing height t_i =
// radius*slope (for a cap tilted by `slope` in the direction
// perpendicular to the corner's own bisector) matches the function's own
// computed CylindricalFace::length to floating-point precision, and the
// result is a valid, closed, manifold solid.
//
// VALIDATION: `radius` > 0; the two-face shared-boundary-edge topology
// FilletConvexEdge itself requires; and, genuinely new here, an explicit
// check that the edge really IS concave, not convex. This check is
// necessary, not decorative: arccos(n_i . n_j) alone (always in [0, pi])
// is IDENTICAL for a convex edge and its "mirror" concave edge (the same
// two face planes, material on the opposite side of the shared edge) -
// confirmed directly, not assumed: feeding FilletConvexEdge a genuine
// concave fixture left its own "theta in (0, pi)" check passing
// unchanged, and it was only the unrelated, confusingly-worded "radius
// too large to fit" extent check downstream that happened to reject it,
// for every tested radius - an accidental side effect of its convex-only
// sign-fix math picking the wrong in-face direction for a concave input,
// not a principled safeguard this function relies on. The real check
// instead samples the OTHER vertices of face i's own loop against face
// j's plane (the sign of that distance is the one fact that actually
// distinguishes "material on the intersection side" from "material on
// the union side" of the two half-spaces, information the two planes'
// normals alone cannot carry) and throws std::invalid_argument, with a
// message pointing at FilletConvexEdge instead, if the edge turns out to
// be convex (or degenerate/ambiguous cases are let through to the
// existing downstream checks, which still catch a truly flat or near-
// 180-degree edge).
//
// SCOPE: a straight edge between exactly two PLANAR faces (same
// PlanarFaces() precondition as FilletConvexEdge), one constant radius,
// with any third face at either endpoint either a free boundary or
// PERPENDICULAR or OBLIQUE to the edge - matching FilletConvexEdge's own
// scope exactly, now that the oblique case is closed here too (see
// above). Multi-edge propagation and vertex blends at concave (or mixed
// convex/concave) corners remain out of scope for this increment,
// matching how FilletConvexEdge itself started before FilletConvexEdges
// generalized it - the one genuine gap still left disclosed rather than
// silently approximated.
//
// CLOSED FORM this was checked against (dino8-kernel's own regression
// tests): for a concave edge of length L with interior dihedral angle
// 3*pi/2 (a 90-degree notch, e.g. the reflex edge of an L-shaped solid
// built by unioning two boxes), the fillet ADDS exactly L * radius^2 *
// (1 - pi/4) of volume - the same magnitude FilletConvexEdges' own
// Steiner-formula cross term uses for a 90-degree CONVEX corner's own
// REMOVED area, here added instead of removed, by the same "square minus
// quarter-disk" cross-section a rolling ball of that radius traces
// filling a 90-degree notch.
Brep FilletConcaveEdge(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double radius);

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
// separate branch, not a numerical-stability workaround). This whole
// two-segment construction is itself now one call into the N-station
// overload below with stations = {{0, radius0}, {L, radius1}} - see that
// overload's own doc comment for the multi-station generalization this
// section's derivation is the N=2 special case of.
//
// SCOPE, narrower even than FilletConvexEdge's own already-disclosed one
// in v1, NOW CLOSED for the one case that matters here: this function DOES
// attempt the corner-notch construction, generalized (via
// EllipseNotchCornerAtVertex, fillet.cpp) from FilletConvexEdge's own
// NotchCornerAtVertex for a third face perpendicular to the EDGE at
// edge_p0/edge_p1. v1's own genuine finding stands, and is exactly why
// this needs its own construction rather than reusing NotchCornerAtVertex
// unchanged: once m != 0, the cone's own axis direction u = e -
// (m/cosb)*bis is NOT parallel to e (u . n_i = -m != 0, whereas e is
// perpendicular to both n_i and n_j by construction) - so a box-style end
// face that IS perpendicular to e is NOT perpendicular to the cone's own
// axis, meaning its true cross-section there is a planar ELLIPSE (the
// cone sliced by a plane oblique to its own axis), not the fixed
// circular-arc formula NotchCornerAtVertex hardcodes.
//
// The closed form (worked out here, not hand-waved): parametrize the
// cone by axial height-from-apex h and true angle phi (angle 0 at
// frame.xaxis, matching every other angle on ConicalFace),
//   P(h, phi) = apex + h*g(phi),   g(phi) := u_hat + tanb*(cos(phi)*xaxis
//                                             + sin(phi)*yaxis)
// (u_hat/xaxis/yaxis the SAME cone frame this function already builds;
// tanb = (radius1_true - radius0_true) / length_true, the cone's own
// tan-half-angle already recomputed identically in Brep::FromMixedFaces -
// reused via ConicalFace's own already-finalized fields, not re-derived
// independently, so the two constructions stay provably consistent).
// g(phi) is the cone's own (unnormalized) ruling direction at angle phi -
// note it does NOT depend on h. The cutting plane through `vertex`
// (edge_p0 or edge_p1) perpendicular to e is {X : (X-vertex).e == 0};
// substituting P(h,phi) and solving the resulting LINEAR-in-h equation
// gives the closed form directly:
//   h(phi) = ((vertex - apex) . e) / (g(phi) . e)
// - a genuine Mobius (linear-fractional) function of (cos phi, sin phi),
// the standard shape a plane-vs-cone intersection takes in the cone's own
// natural angular coordinate. The 3D point at angle phi is then
// P(phi) = apex + h(phi)*g(phi). Two properties, both proven (not merely
// observed) and both load-bearing for what follows:
//   - h(0) and h(sweep_angle) come out EXACTLY equal to the cone's own
//     v0 (or v1, at the other endpoint) - i.e. the ellipse passes exactly
//     through this patch's own two rail corners at that end - because
//     both rail points satisfy (rail-vertex).e == 0 (k_i.e == k_j.e == 0,
//     already used elsewhere in this derivation) AND lie on the cone by
//     construction, so they're forced to coincide with the unique
//     solution of the linear-in-h plane equation at their own angle.
//   - g(phi).e can, for a steep enough taper, change sign somewhere
//     inside the swept range (the cutting plane becomes asymptotically
//     parallel to that one ruling) - a real, checked-directly degeneracy
//     EllipseNotchCornerAtVertex verifies against directly (throws
//     std::runtime_error rather than silently dividing by ~0), not
//     present in the m=0 case (where g(phi).e is the CONSTANT 1, since
//     u_hat -> e and xaxis.e, yaxis.e -> 0 as m -> 0 - confirming this
//     construction is the genuine generalization of NotchCornerAtVertex's
//     circle, collapsing to it exactly at zero taper, not a different
//     construction that merely resembles it).
//
// A genuine, checked-directly finding from building this (see
// EllipseNotchCornerAtVertex's own doc comment and this feature's own
// verification tests for the numeric fixture this was validated against):
// the resulting ellipse and the cone's own PLAIN v=v0/v=v1 cap (a circle,
// perpendicular to u_hat, sharing only the two rail-corner points with the
// ellipse) are genuinely DIFFERENT curves - for one tested fixture,
// diverging by up to ~6% of the local radius at the curve's own
// mid-sweep point, a divergence that does NOT shrink with finer sampling
// (it is not a discretization error). Splicing the ellipse into only the
// third PlanarFace's own notch while leaving the ConicalFace's own cap as
// the plain circle would therefore produce a Brep that reports a
// manifold, closed boundary there (IsManifold()'s has_boundary == false)
// while its two "sharing" faces trace measurably different 3D curves at
// that shared boundary - silently wrong, not merely approximate. So this
// function does the more thorough fix instead: the ConicalFace's OWN cap
// at a notched end is ALSO re-trimmed to the same dense ellipse sample
// points (ConicalFace::cap0_notch_points/cap1_notch_points - see that
// struct's own doc comment), giving the fillet's own true patch and the
// notched third face a LITERAL shared boundary curve, not two
// independently-plausible approximations of different curves. Unlike the
// circular case (whose shared cap edge is the EXACT isocurve), there is
// no simple isocurve family for a general ellipse in the cone's own (u,
// v) domain, so this shared boundary is itself a dense polygonal
// approximation on BOTH sides now (not just the planar side, as in the
// circular case) - the one place this construction isn't exact to
// floating-point precision by construction of the representation, exactly
// mirroring FilletConvexEdge's own already-disclosed tradeoff for the
// circular case, just now also true of the curved side (see
// ConicalFace::cap0_notch_tolerance/cap1_notch_tolerance for the
// genuinely computed, not guessed, bound this carries).
//
// A THIRD face that is NOT perpendicular to e (an oblique end condition)
// is still left untouched, exactly matching FilletConvexEdge's own
// already-established scope for that harder case (see
// EllipseNotchCornerAtVertex's own doc comment - it silently no-ops when
// no matching perpendicular third face is found, same as
// NotchCornerAtVertex).
//
// Also matches FilletConvexEdge's own scope otherwise: a straight edge
// between exactly two PLANAR faces, convex dihedral only, radius0 and
// radius1 both strictly positive (throws std::invalid_argument
// otherwise - a radius reaching exactly zero partway along the edge
// would mean the swept patch's own apex falls INSIDE the trimmed region,
// a genuinely different, degenerate topology this function does not
// attempt). The fully general free-form-radius canal-surface case (point
// 2 above) remains out of scope for this function.
//
// THIN WRAPPER, not a parallel implementation: this two-radius overload
// delegates to the N-station overload below with stations = {{0,
// radius0}, {edge_p0.DistanceTo(edge_p1), radius1}} - every claim in this
// doc comment (the rail exactness, the cone derivation, the corner-notch
// ellipse) is really a claim about that N=2 special case, verified
// bit-for-bit identical to this overload's own former standalone
// implementation (see dino8-kernel's own regression tests). Kept as its
// own overload purely for caller convenience/back-compat, not because it
// does anything the N-station overload can't.
Brep FilletConvexEdgeTapered(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double radius0,
                              double radius1);

// PIECEWISE-LINEAR MULTI-STATION generalization of the two-radius
// FilletConvexEdgeTapered above: rolls a ball whose radius r(t) is
// piecewise-linear in arc length t along the edge, interpolating
// `stations` (which must be sorted by strictly increasing `t`, with
// `stations.front().t == 0` and `stations.back().t ==
// edge_p0.DistanceTo(edge_p1)` - i.e. the profile spans the WHOLE edge,
// exactly as the two-radius overload's implicit {0, L} pair always has)
// instead of a single linear r(t) = radius0 + m*t.
//
// THE PER-SEGMENT MATH IS EXACTLY THE TWO-RADIUS OVERLOAD'S OWN
// DERIVATION, RE-APPLIED VERBATIM TO EACH [stations[k].t,
// stations[k+1].t] SUB-INTERVAL - nothing in that derivation (see above)
// assumed the taper covered the WHOLE edge, only that r(t) is linear on
// the interval considered, which every segment of a piecewise-linear
// profile is by definition. Concretely: for segment k, re-origin locally
// (seg_p0 = edge_p0 + stations[k].t*e, local length Lseg =
// stations[k+1].t - stations[k].t, local radii (stations[k].radius,
// stations[k+1].radius)) and apply that same construction - same apex/
// axis/frame formulas, same true-radius/true-length/true-sweep-angle
// closed forms - producing one genuine Brep::ConicalFace per segment
// (dino8-kernel's own BuildTaperedConeSegment helper, fillet.cpp, is
// this shared math, factored out and used by BOTH this overload and the
// two-radius one above for exactly this reason). k_i/k_j (the two rail
// direction vectors) do not depend on the segment, and r(t) is
// continuous by the profile's own definition, so rail_i(t)/rail_j(t) =
// edge_p0 + t*e + r(t)*k_i (or k_j) are GLOBALLY continuous piecewise-
// linear curves across every station, independent of any join treatment
// - confirmed directly (not merely asserted), including at every
// interior station, by dino8-kernel's own regression tests.
//
// Faces i/j (the two original PLANAR faces sharing the edge) are
// re-trimmed by replacing their own single shared edge (edge_p0 to
// edge_p1) with the FULL piecewise rail_i(t)/rail_j(t) polyline through
// EVERY station (not just the two outer endpoints): each station-to-
// station sub-run of that polyline is an ordinary straight loop edge
// that coincides exactly (to floating-point precision) with the matching
// ConicalFace segment's own straight rail side, so it welds into the
// SAME real ON_BrepEdge automatically, with no new topology machinery
// needed beyond FromMixedFaces()'s own existing coincident-point vertex
// welding - genuinely the same mechanism the two-radius overload's own
// single straight re-trim already relies on, just applied once per
// segment instead of once for the whole edge. (A real correction versus
// an earlier draft of this feature's own design: a SINGLE half-space cut
// across the whole edge, using only one segment's own tilted rail
// direction, does NOT work once there is more than one segment - once
// adjacent segments have different slopes, rail_i(t) genuinely BENDS at
// each interior station, so no single plane contains the whole rail; the
// per-station polyline splice above is the correct generalization, not a
// simplification of it.)
//
// THE INTERIOR-STATION JOIN - the one genuinely new piece of geometry
// this overload needs beyond "N independent segments side by side" - is
// where two adjacent segments' own swept cone patches meet at a shared
// interior station. The two rail corners there (rail_i(t)/rail_j(t) at
// that station) are shared exactly, by the rail continuity above, but a
// segment's own natural v1 (or v0) CAP CIRCLE is NOT, in general, the
// same curve as its neighbor's - checked directly (not assumed), because
// the cone's own axis direction u_hat genuinely differs between adjacent
// segments whenever their slopes differ (the same reason this
// construction is only C0, not C1, at an interior station - see below).
// For a representative monotonic three-station profile, the two
// segments' own natural caps at the shared station were measured to
// diverge by a few percent of the local radius at mid-sweep - both
// points independently confirmed to lie exactly on the SAME sphere (the
// rolling ball's own position at that station), i.e. a real curve
// separation, not sampling noise, and (unlike the corner-notch ellipse's
// own sagitta error) one that does NOT shrink as the notch sampling gets
// finer - a genuinely non-vanishing approximation, the first of its kind
// in this kernel, honestly disclosed rather than silently tolerated (see
// Brep::ConicalFace::cap0_surface_fit_tolerance/
// cap1_surface_fit_tolerance's own doc comment for the bound this is
// given, and dino8-kernel's own regression tests for the numeric fixture
// this was measured against).
//
// The construction: treat the EARLIER segment's own natural cap circle
// as canonical (an arbitrary but simple, principled choice - "the
// earlier segment's own cap is authoritative") and give it verbatim to
// the LATER segment's own cap0_notch_points, exactly mirroring
// EllipseNotchCornerAtVertex's own "one face's true boundary curve
// becomes a LITERAL shared boundary, not two independently-plausible
// approximations of two different curves" principle (see that function's
// own doc comment) - just borrowed from a NEIGHBORING CONE instead of
// derived from a third PLANAR face's own cutting plane. Because the
// borrowed points do not lie exactly on the later segment's own cone,
// this also carries a genuinely computed cap0_surface_fit_tolerance (or
// cap1_surface_fit_tolerance, for the earlier segment's own side, kept
// honest for whichever ordering ends up owning the shared ON_BrepEdge's
// own m_tolerance) instead of the near-zero bound every other notch in
// this kernel gets.
//
// C0-BUT-NOT-C1 AT INTERIOR STATIONS, AND WHY THAT IS CORRECT: because
// the cone's own axis direction genuinely rotates between adjacent
// segments whenever their slopes differ, the swept surface has a REAL
// tangent-plane discontinuity - a visible crease - at every interior
// station where the taper rate changes. This is the expected, correct
// behavior of a genuinely PIECEWISE-LINEAR (not spline) radius profile,
// exactly matching what a real rolling ball does when its own radius
// growth rate changes abruptly - not a defect of this construction, and
// not something a smoother radius law would avoid without becoming a
// genuinely different (non-piecewise-linear) profile, out of scope here
// exactly as it already was for the two-radius overload above.
//
// CORNER-NOTCH: applied only at the two OUTER endpoints (edge_p0,
// edge_p1), using only the FIRST segment's own cone parameters at
// edge_p0 and only the LAST segment's own cone parameters at edge_p1 -
// EllipseNotchCornerAtVertex itself needs no changes at all, since it
// only ever needs ONE cone's own apex/axis/frame/sweep, the same
// signature it already has. Interior stations get zero corner-notch
// calls - they have no third face there at all (an interior station is
// purely an internal seam between two ConicalFace segments, not a
// vertex/vertex-adjacent-face boundary of `solid`).
//
// VALIDATION, checked directly rather than assumed safe: `stations` must
// have at least 2 entries, start at t=0 and end at t=edge length (throws
// std::invalid_argument otherwise), have strictly increasing `t` and
// strictly positive `radius` throughout, and have radius MONOTONIC
// across the WHOLE vector (non-decreasing or non-increasing throughout,
// not merely consecutive-pair by consecutive-pair) - an interior local
// radius extremum is rejected outright, not silently mishandled: it was
// measured, during this feature's own development, to make the interior-
// join divergence balloon well past the monotonic case's own already-
// disclosed few-percent figure, and to flip the sign of the two
// neighboring cones' own apex placement along the edge (an "hourglass"
// pairing this construction does not attempt). For more than 2 stations,
// two CONSECUTIVE stations whose radii differ by less than this
// function's own radius tolerance are also rejected (a locally-flat sub-
// segment would need a CylindricalFace, not a degenerate ConicalFace,
// mixed into the middle of the run - a real, larger increment of its
// own, out of scope here); the ONLY flat case this function supports is
// the top-level `stations.size() == 2` profile with near-equal radii,
// which dispatches straight to FilletConvexEdge exactly as the
// two-radius overload's own m~=0 dispatch already does - a genuine,
// deliberate special case kept OUTSIDE the general per-segment cone loop
// (a flat single segment needs a CylindricalFace, which
// Brep::FromMixedFaces explicitly rejects being built as a degenerate
// ConicalFace instead), disclosed here rather than silently narrowed.
//
// Also matches every other scope note above: convex dihedral only
// between exactly two PLANAR faces, every station's radius strictly
// positive, `Brep::MixedFaces()` recovering each segment's own frame/
// radius/length/angle exactly but NOT round-tripping either a corner-
// notch's or an interior-join's own dense sample shape (same disclosed
// limit `cap0_notch_points`/`cap1_notch_points` already carry, see that
// field's own doc comment), and the fully general free-form (non-
// piecewise-linear) radius law remaining out of scope.
Brep FilletConvexEdgeTapered(const Brep& solid, Point3d edge_p0, Point3d edge_p1,
                              const std::vector<FilletRadiusStation>& stations);


// Exact kernel-level CHAMFER of ONE straight, convex edge shared by two
// PLANAR faces of `solid`: the classical two-distance chamfer (Parasolid
// "chamfer by two ranges", Rhino's ChamferEdge with Distance1/Distance2),
// the planar sibling of FilletConvexEdge above. Where a rolling ball
// produces a circular-cylinder patch, a chamfer produces a PLANAR strip -
// so unlike every fillet in this file there is no curved surface at all,
// and the whole result is exact to floating-point precision by
// construction of the representation itself (every face is a
// Brep::PlanarFace, every edge a straight ON_LineCurve): no dense
// polygonal notch, no sagitta tolerance, no chordal error anywhere.
//
// `edge_p0`/`edge_p1` identify the edge exactly as FilletConvexEdge's own
// doc comment requires (two endpoints of one consecutive vertex pair
// shared, in opposite walking directions, by exactly two of
// `solid.PlanarFaces()`'s loops; face i walks edge_p0 -> edge_p1, face j
// walks edge_p1 -> edge_p0). `distance_i` is the chamfer's setback
// measured IN face i's own plane, perpendicular to the edge, from the
// original sharp edge to the new chamfer rail; `distance_j` likewise in
// face j's plane. Both must be strictly positive (throws
// std::invalid_argument otherwise, as for a non-shared edge or a
// non-convex dihedral).
//
// The construction:
//   1. n_i, n_j, e, theta (interior dihedral angle) exactly as
//      FilletConvexEdge's own step 1; m_i = normalize(n_i x e) and m_j =
//      normalize(n_j x -e) are each face's own in-plane, perpendicular-
//      to-the-edge, INTO-MATERIAL direction (the loop's own interior is
//      to the left of a CCW-outward walk - checked directly against each
//      face's own vertex extent rather than assumed).
//   2. The two chamfer RAILS are the lines {edge_p0 + distance_i*m_i +
//      t*e} in face i's plane and {edge_p0 + distance_j*m_j + t*e} in
//      face j's plane - always parallel to the edge, for the same reason
//      FilletConvexEdge's contact lines are (a constant offset along a
//      straight edge between two planes).
//   3. Faces i and j are re-trimmed by one half-space clip each, at their
//      own rail (the same detail::ClipByHalfspace3d primitive
//      FilletConvexEdge uses), leaving the rail as the loop's new boundary
//      edge. Throws std::invalid_argument if either distance exceeds that
//      face's own extent from the edge (the chamfer doesn't fit).
//   4. The chamfer face is the planar quad spanned by the two rails,
//      oriented CCW-outward (its outward normal lies strictly between n_i
//      and n_j for a convex edge - verified by construction, not assumed).
//   5. END CONDITIONS - a genuine, CHECKED generalization over
//      FilletConvexEdge's own perpendicular-end-face-only corner notch:
//      at each of edge_p0/edge_p1, any THIRD face whose loop has a vertex
//      there with its two loop neighbours on face i's and face j's own
//      planes (the ordinary trihedral corner) has that sharp corner
//      replaced by the two points where the two rails pierce that face's
//      own plane, Q_i = rail_i /\ plane_k and Q_j = rail_j /\ plane_k -
//      both of which lie exactly on that face's own two existing boundary
//      lines (rail_i lies in plane i, so rail_i /\ plane_k is on the line
//      plane_i /\ plane_k, which IS face k's edge shared with face i; same
//      for Q_j). The chamfer quad's own end edge at that vertex is then
//      the straight segment Q_i Q_j, which lies in plane_k by construction
//      - so the chamfer face, face k, and the two re-trimmed faces meet
//      EXACTLY there whether face k is perpendicular to the edge (Q_i =
//      edge_p0 + distance_i*m_i exactly, the box case) or OBLIQUE to it
//      (Q_i slides along face k's edge by distance_i*(m_i.n_k)/(e.n_k) -
//      the case FilletConvexEdge still leaves untouched, because a
//      cylinder's oblique section is an ellipse while a plane's is just
//      another line). The chamfer quad's own four corners are therefore
//      Q_i(edge_p0), Q_i(edge_p1), Q_j(edge_p1), Q_j(edge_p0), and faces
//      i/j are re-trimmed by the rail LINES (not segments), so their new
//      corners are those same Q points automatically. Throws
//      std::invalid_argument if a Q point would fall beyond the far end
//      of face k's own edge (the chamfer overruns the third face), or if
//      face k's plane is parallel to the edge (no finite Q; impossible
//      at a manifold trihedral vertex, checked anyway). A Q point landing
//      EXACTLY on that far vertex is legitimate (the chamfer's end edge
//      terminates at an existing vertex, which becomes valence-4) and is
//      spliced without duplicating it - the case two equal-setback
//      chamfers meeting at a box corner produce, see the chained-chamfer
//      regression test.
//      If NO face other than i/j touches an endpoint at all, that end of
//      the chamfer is honestly left as a free boundary (an open shell,
//      exactly as FilletConvexEdge's own free-boundary case). If faces DO
//      touch it but none matches the trihedral pattern (four or more
//      faces at the vertex, or a non-planar neighbour), this function
//      throws std::invalid_argument rather than returning a solid whose
//      chamfer end floats unattached - a genuinely different (vertex-
//      blend) problem, disclosed rather than silently mis-built.
//
// The result is `solid` with faces i/j re-trimmed, any matching third
// faces re-cornered, and the new chamfer PlanarFace inserted, assembled
// via Brep::FromMixedFaces: for a closed input its topology is a genuine
// closed 2-manifold (IsSolid() == true) with every shared edge a single
// real ON_BrepEdge, and its volume is exactly the input's minus the
// chamfer prism's (see dino8-kernel's own regression tests for the
// closed forms this was checked against, including the oblique-end case).
//
// SCOPE: one straight edge between exactly two PLANAR faces, convex
// dihedral only, of a solid PlanarFaces() can describe (an input already
// carrying a curved face from an earlier fillet is rejected by
// PlanarFaces() itself - see that method's own doc comment). A curved
// adjacent face or a concave edge remain out of scope, disclosed exactly
// as FilletConvexEdge discloses them.
Brep ChamferConvexEdge(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double distance_i,
                        double distance_j);

// DISTANCE + ANGLE form of ChamferConvexEdge (Parasolid "chamfer by range
// and angle", Rhino's ChamferEdge Distance/Angle mode): `distance_i` is
// the setback in face i's plane exactly as above, and `angle_from_i`
// (radians) is the angle between face i's plane and the chamfer plane,
// measured inside the removed material. The second distance follows
// exactly from the law of sines in the chamfer's own triangular cross-
// section (sides distance_i, distance_j, included angle theta = the
// interior dihedral angle; the angle opposite distance_j is angle_from_i
// and the angle opposite distance_i is pi - theta - angle_from_i):
//   distance_j = distance_i * sin(angle_from_i) / sin(theta + angle_from_i)
// - then this overload DISPATCHES to the two-distance form above with
// that value, so every claim in that doc comment holds verbatim. Throws
// std::invalid_argument unless 0 < angle_from_i < pi - theta (the chamfer
// plane must actually reach face j); at angle_from_i = (pi - theta)/2
// (45 degrees for a right-angle edge) distance_j == distance_i exactly,
// the symmetric chamfer - checked directly by dino8-kernel's own tests.
Brep ChamferConvexEdgeAngle(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double distance_i,
                             double angle_from_i);

// CONCAVE (reflex) edge chamfer - the flat-bevel MIRROR of
// ChamferConvexEdge, for a concave (interior dihedral > pi) straight
// edge: fills the notch with a flat bevel instead of cutting a convex
// corner's own wedge away (the chamfer's counterpart to how
// FilletConcaveEdge relates to FilletConvexEdge).
//
// UNLIKE FilletConcaveEdge (which needs a genuinely mirrored axis_point/
// contact_i/contact_j derivation - see that function's own doc comment
// for why), ChamferConvexEdge's own rail construction ALREADY works
// correctly for a concave edge, completely unchanged: its m_i/m_j sign-
// fix is extent-based (picks whichever of the two in-plane, perpendicular-
// to-the-edge directions actually has POSITIVE extent within that face's
// own real polygon - see ChamferConvexEdge's own doc comment/body), not
// built from a convex-specific contact-point formula the way
// FilletConvexEdge's own axis_point is - so it already discovers the
// correct "into this face's own material" direction regardless of which
// side of the two half-spaces is material. The chamfer triangle's own
// vertex angle at the corner (what the law-of-sines dispatch below needs)
// is, by the same token, the angle BETWEEN m_i and m_j - and this equals
// `pi - psi` (psi = arccos(n_i . n_j)) in BOTH the convex and the concave
// case: for convex it's theta_material itself (already < pi, so
// arccos(m_i . m_j) recovers it directly); for concave, theta_material =
// pi + psi (> pi, a genuine reflex angle no two-vector arccos() can ever
// return), but arccos(m_i . m_j) recovers its ACUTE complement 2*pi -
// theta_material = pi - psi instead - the same expression, so the
// EXISTING `theta = pi - acos(n_i . n_j)` formula (already used
// unmodified throughout this file) is the correct angle for the concave
// triangle too, with no re-derivation needed.
// Confirmed directly, not assumed from the algebra alone: fed a genuine
// concave fixture (the L-shaped prism FilletConcaveEdge's own tests use),
// ChamferConvexEdge produces a valid, closed, manifold solid whose added
// volume matches the exact closed form (distance_i * distance_j / 2 per
// unit length - the right-triangle cross-section filling the notch) to
// floating-point precision, with NO code change to that function at all.
//
// This function and ChamferConcaveEdgeAngle below are therefore thin,
// VALIDATING wrappers: each checks the edge is genuinely CONCAVE (the
// same EdgeConvexity check FilletConcaveEdge uses - arccos(n_i . n_j)
// alone cannot distinguish a convex edge from its "mirror" concave edge,
// see FilletConcaveEdge's own doc comment for why) and then dispatches to
// ChamferConvexEdge's own construction verbatim - the same "two names,
// one shared construction" shape ChamferConvexEdgeAngle already uses for
// its own dispatch to the two-distance form. A failure INSIDE that
// shared construction (a distance too large to fit, a degenerate vertex,
// an unsupported third-face pattern) is reported with ChamferConvexEdge's
// own name in the exception message, not this function's - disclosed
// here rather than hidden, since re-threading every message through a
// caller-name parameter was judged not worth the added surface area for
// what is, underneath, genuinely the same code path being reused, not
// duplicated.
Brep ChamferConcaveEdge(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double distance_i, double distance_j);

// DISTANCE + ANGLE form of ChamferConcaveEdge, exactly mirroring
// ChamferConvexEdgeAngle's own relationship to ChamferConvexEdge (see
// both those doc comments): validates the edge is concave, then applies
// the SAME law-of-sines formula (see ChamferConcaveEdge's own doc
// comment for why no re-derivation is needed for the concave case) to
// get distance_j, and dispatches to ChamferConcaveEdge.
Brep ChamferConcaveEdgeAngle(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double distance_i,
                             double angle_from_i);


// MULTI-EDGE constant-radius rolling-ball fillet with genuine SPHERICAL
// VERTEX BLENDS - the piece of Parasolid's blend class that turns
// "round one edge" into "round this solid": every edge in `edges` (each a
// (p0, p1) endpoint pair identifying a straight, convex edge between two
// PLANAR faces of `solid`, in either order, exactly as FilletConvexEdge
// requires) is rounded with the SAME ball radius `radius`, and every
// vertex where THREE filleted edges of a trihedral corner meet gets the
// exact spherical corner patch (a Brep::SphericalFace) the rolling ball
// leaves there - not FilletConvexEdge's flat corner notch, which is only
// the right shape when the edge fillet runs all the way into a sharp
// perpendicular end face.
//
// THE CONSTRUCTION, per element:
//   Edges. Each edge k reuses FilletConvexEdge's own step 1-2 geometry
//   verbatim: n_i, n_j, e, bis, cosb, contact rails rail_i(t) = P(t) +
//   radius*(n_i - bis/cosb), and one Brep::CylindricalFace with frame
//   (xaxis n_i, zaxis e), radius, sweep pi - theta. The only new degree
//   of freedom is that the cylinder no longer necessarily spans the whole
//   edge: at a spherical corner its end is SET BACK to t_k = (C - V).e,
//   where C is that corner's own ball center (below), so its cap circle
//   there is centered at C - i.e. it is a GREAT circle of the corner
//   sphere, which is exactly why the sphere patch can share it as a
//   literal ON_BrepEdge.
//   Faces. Each planar face is re-trimmed by one half-space clip per
//   filleted edge it carries (the same rail-line cut FilletConvexEdge
//   makes, applied once per edge): a face with two filleted edges meeting
//   at a corner is thereby inset at that corner to the single point where
//   its two rails cross, which is provably C + radius*n_f (the point of
//   the face at distance `radius` from a point equidistant from all three
//   planes) - so the planar face's own new corner IS the sphere's rail
//   corner, with no notch and no extra construction.
//   Vertices. For each endpoint V of a filleted edge, with m = the number
//   of filleted edges incident to V and valence = the number of faces of
//   `solid` touching V:
//     m == 1: FilletConvexEdge's own corner notch, verbatim (a third face
//       perpendicular to the edge is notched with the fillet's true cap
//       arc and shares ONE real edge with it - now also when the same
//       third face is notched at SEVERAL corners, via PlanarFace::
//       notch_runs; an oblique or absent third face is left untouched,
//       the same disclosed limit FilletConvexEdge has).
//     m == 3 and valence == 3 (all three edges of a trihedral corner are
//       filleted): the ball center C is the unique point at distance
//       `radius` inside all three face planes (a 3x3 linear solve, det =
//       n_a.(n_b x n_c) != 0 for a genuine trihedral corner). C lies on
//       all three edge fillets' own axes by construction (checked, not
//       assumed: each axis IS the locus of inside points at distance
//       `radius` from its two planes); the three cylinders are set back
//       to the planes through C perpendicular to their own edges, and the
//       corner is closed by the spherical triangle of the sphere (C,
//       radius) with vertices C + radius*n_a, C + radius*n_b, C +
//       radius*n_c, whose three sides are the three great-circle arcs the
//       three set-back caps trace. Represented as a Brep::SphericalFace
//       latitude/longitude rectangle with one pole: this REQUIRES one of
//       the three faces (the "pole" face, n_c) to be perpendicular to the
//       other two (n_c.n_a == n_c.n_b == 0 within 1e-9), so that the arcs
//       n_a->n_c and n_b->n_c are meridians and n_a->n_b the equator arc
//       - true of every box corner and of every corner of a prism whose
//       caps are perpendicular to its side faces (any polygon cross-
//       section, any side-face dihedral), false for e.g. a general
//       tetrahedron corner, which throws std::invalid_argument (a general
//       spherical triangle needs a non-isocurve boundary, a genuine
//       future increment, disclosed here rather than approximated). The
//       sphere's frame is chosen so its equator arc has the IDENTICAL
//       start direction and orientation as the n_a->n_b cylinder's own
//       cap (xaxis = that cylinder's frame.xaxis), so the two faces
//       evaluate the shared arc through the same NURBS parameterization
//       and Brep::FromMixedFaces' arc-identity check welds them as one
//       edge; the two meridian arcs are quadrants, symmetric under
//       reversal, so their identity holds whichever face walks them
//       first. Whether the pole is the north or the south one follows
//       from the handedness x cross y vs. n_c.
//     anything else (m == 2 at a trihedral vertex, m == 3 at a vertex of
//       valence > 3, m >= 4): throws std::invalid_argument. Two fillets
//       meeting at a corner whose third edge stays sharp need the two
//       cylinders' own mutual intersection curve plus a non-spherical
//       corner patch - a real, harder vertex-blend problem this function
//       does not attempt, disclosed rather than mis-built.
//   The set-back caps at a spherical corner are the cylinders' plain
//   isocurve caps (no notch points anywhere in this construction), so
//   for an all-edges-filleted convex solid EVERY edge of the result is an
//   exact curve: straight rails, exact circular arcs, and the topology
//   is a genuine closed 2-manifold (IsSolid() == true).
//
// VALIDATION: `radius` > 0; every edge a genuine shared convex edge (see
// FilletConvexEdge for the exact topology test and the fit check
// against each face's own extent); no edge listed twice; and, after all
// set-backs, every cylinder keeps a strictly positive length (two
// spherical corners on a short edge would otherwise overlap - thrown,
// not clipped). A face left with fewer than 3 vertices by its clips
// also throws.
//
// CLOSED FORMS this was checked against (dino8-kernel's own regression
// tests): for a convex polyhedron with EVERY edge filleted and every
// vertex a supported trihedral corner, the result is exactly the
// Minkowski sum of the inner offset body K (the solid with every face
// pushed in by `radius`) with a ball of radius `radius`, so by Steiner's
// formula V = V(K) + S(K)*r + r^2 * sum_edges L_e*(pi - theta_e)/2 +
// (4/3)*pi*r^3; for the unit box with r = 0.2 that is (1-2r)^3 +
// 6(1-2r)^2 r + 3(1-2r) pi r^2 + (4/3) pi r^3 = 0.907705..., and a
// regular hexagonal prism with all 18 edges filleted is checked the same
// way with its 120-degree side dihedrals.
//
// SCOPE, stated plainly: straight convex edges between PLANAR faces of a
// solid PlanarFaces() can describe (an input already carrying a curved
// face is rejected by PlanarFaces() itself), one radius for all edges
// (a corner where the three incident fillets have different radii is
// not a sphere at all), spherical corners only where one face is
// perpendicular to the other two, and the m == 1 end condition exactly
// as FilletConvexEdge already has it. Concave edges, curved adjacent
// faces, and variable radii remain out of scope for this function.
Brep FilletConvexEdges(const Brep& solid, const std::vector<std::pair<Point3d, Point3d>>& edges, double radius);

// MULTI-EDGE concave-edge fillet - FilletConcaveEdge's own counterpart to
// FilletConvexEdges, letting several INDEPENDENT concave edges of the
// same solid be filleted in one call (chaining single FilletConcaveEdge
// calls is not possible at all here: the first call's own output already
// carries a curved CylindricalFace, and PlanarFaces() - which every one
// of these functions calls first - rejects any solid already carrying
// one, exactly as it does for FilletConvexEdge; confirmed directly, not
// assumed, while developing this function).
//
// Every per-edge quantity (bis/cosb/offset/contact_i/contact_j/the
// hand-sign frame fix/trim_back) is FilletConcaveEdge's own, verbatim,
// just computed once per edge in a loop instead of once - see that
// function's own doc comment for every derivation this reuses.
//
// SCOPE, stated plainly rather than silently narrowed: every edge must be
// a genuine shared concave boundary edge (the same EdgeConvexity check
// FilletConcaveEdge itself uses, applied per edge); one radius for all
// edges; no edge listed twice; and - the one deliberate limit this first
// multi-edge increment carries, matching where FilletConvexEdges ITSELF
// started before its own trihedral spherical-corner support was added -
// every filleted edge's own two endpoints must have EXACTLY ONE filleted
// edge incident (m == 1): two or more concave edges meeting at a shared
// vertex is a genuine vertex-blend problem (and, for concave corners, one
// this codebase has not attempted at all yet - not even the m == 3
// trihedral case FilletConvexEdges already closes for the convex side)
// and throws std::invalid_argument rather than guessing at a shape.
// Oblique third faces are likewise out of scope here (unlike the single-
// edge FilletConcaveEdge, which already closes that case) - only a free
// boundary or a third face exactly PERPENDICULAR to the edge is closed,
// via the same NotchCornerAtVertex splice FilletConvexEdges' own m == 1
// case uses. Both gaps are genuine, disclosed future increments for this
// function specifically.
//
// CLOSED FORM this was checked against (dino8-kernel's own regression
// tests): two INDEPENDENT 90-degree concave notches (no shared vertex) on
// the same prism, each filleted with the same radius r, together ADD
// exactly 2 * r^2 * (1 - pi/4) of volume - the same per-notch closed form
// FilletConcaveEdge's own single-edge tests check, simply summed, since
// the two notches share no geometry to interact through.
Brep FilletConcaveEdges(const Brep& solid, const std::vector<std::pair<Point3d, Point3d>>& edges, double radius);


// BLEND REMOVAL: the inverse of FilletConvexEdge - restores the original
// sharp edge a constant-radius, planar/planar fillet rounded off, purely
// from the FILLETED solid's own geometry (no separate history/provenance
// is stored anywhere in a Brep, so this genuinely RECOVERS the original
// shape rather than replaying a recorded operation - the same "read it
// back out of the geometry" spirit MixedFaces() already uses for
// Brep::CylindricalFace/ConicalFace/SphericalFace recognition).
//
// `point_on_fillet` identifies which CylindricalFace to remove: the
// closest of `solid.MixedFaces().cylindrical` to that point (by distance
// to the trimmed cylindrical surface itself, not just its infinite
// extension) - mirroring how dino8-app's own edge-pick commands identify
// a face by a clicked point rather than an index. Throws
// std::invalid_argument if no cylindrical face is within a reasonable
// tolerance of the point.
//
// The construction, the genuine inverse of FilletConvexEdge's own steps:
//   1. Recover face i/j: the two PlanarFace records whose own loop has an
//      edge exactly matching the cylinder's own two straight rails (its
//      v=0/v=length corners at angle 0, and separately at angle `angle`)
//      - the SAME rail-sharing fact FilletConvexEdge's own doc comment
//      relies on to weld them in the first place.
//   2. Recover the two faces' own outward normals n_i/n_j (read directly
//      off their own PlanarFace::plane, no fitting needed) and, from
//      those plus the cylinder's own `radius`, the SAME bis/cosb/offset
//      FilletConvexEdge's own construction used. Before trusting this,
//      each end is checked against every SphericalFace of `solid`: a
//      FilletConvexEdges corner cylinder is set back so its own end rail
//      corners sit EXACTLY on a corner sphere's own surface, at that
//      sphere's own radius (see FilletConvexEdges' own doc comment) - an
//      end matching this is a spherical vertex blend, not a plain
//      corner-notch or free boundary, and throws std::invalid_argument
//      rather than silently restoring the wrong shape there (a plain
//      m==1 end is unaffected either way, since FilletConvexEdge and
//      FilletConvexEdges use IDENTICAL math for that case).
//   3. The restored sharp edge's own two endpoints follow directly:
//      edge_p0 = frame.origin +/- bis*offset, edge_p1 = edge_p0 +
//      length*frame.zaxis - the exact algebraic inverse of
//      FilletConvexEdge's own axis_point(p) = p - bis*offset (the "+"
//      sign) or FilletConcaveEdge's own axis_point(p) = p + bis*offset
//      (the "-" sign) - `CylindricalFace::outward` (true for
//      FilletConvexEdge, false for FilletConcaveEdge - see that field's
//      own doc comment) is exactly the bit that tells this step which
//      construction built the patch, so which sign to invert with; `bis`
//      and `offset` themselves are symmetric in n_i/n_j and need no
//      change either way.
//   4. Face i's and face j's own loops are re-trimmed by replacing their
//      shared rail edge with the restored sharp edge - literally
//      splicing (edge_p0, edge_p1) in place of the rail's own two
//      corner points, in whichever direction each face's own loop
//      already walks that edge.
//   5. Any THIRD face notched by FilletConvexEdge's own corner-notch
//      construction (a dense polygonal run between the SAME two rail
//      corners at one end - see NotchCornerAtVertex's own doc comment)
//      is found the same way (a run of more than 2 consecutive loop
//      points between those two corners) and collapsed back to the
//      single vertex edge_p0 or edge_p1 - the genuine inverse splice.
//      A face with NO notch there (an untouched sharp corner, or a free
//      boundary) needs no change and gets none.
//   6. The one CylindricalFace is dropped; every other face of `solid`
//      (including any OTHER fillet's own CylindricalFace/ConicalFace/
//      SphericalFace, for a solid with several independent fillets) is
//      carried through unchanged via Brep::FromMixedFaces.
//
// A CONICAL FACE (a FilletConvexEdgeTapered-built taper, or one segment
// of an N-station one) is inverted the same way, in closed form, WITHOUT
// separately recovering the taper's own apex/axis construction at all:
// the rolling-ball radii r_lo/r_hi at the segment's own two ends follow
// directly from the cone's own true radii (radius0 = r_lo*c, radius1 =
// r_hi*c, where c = 1/sqrt(1 + tan_half_angle^2) and tan_half_angle =
// (radius1 - radius0)/length are already known, no unknowns), and the
// cone's own rail corner at (v0, angle 0) is EXACTLY edge_p0 + r_lo*k_i
// (FilletConvexEdgeTapered's own rail_i(0), k_i = n_i - bis/cosb a fixed
// vector once n_i/n_j are recovered) - so edge_p0/edge_p1 follow directly
// by subtraction, with the SAME reconstruction from face j's own k_j
// (k_i != k_j, so this is a genuinely discriminating checked invariant,
// not a vacuous one) required to agree. Unlike the cylindrical case, a
// notched end here needs NO separate oblique-rejection branch: EVERY
// corner-notch a ConicalFace ever carries is already the dense-ellipse-
// run kind (`EllipseNotchCornerAtVertex`, fillet.cpp - even for a
// perpendicular third face, since a tapered cone's own perpendicular
// cross-section is generally an ellipse, not a circle), and
// CollapseNotchRun's own splice works purely by matching 3D points,
// agnostic to which curve family produced the run.
//
// `point_on_fillet` is matched against BOTH `solid.MixedFaces().
// cylindrical` and `.conical`, and whichever face's own trimmed surface
// is closer wins - so this one function removes either kind of fillet
// patch a caller might have clicked on.
//
// SCOPE, stated plainly: this reverses exactly what FilletConvexEdge and
// FilletConvexEdgeTapered themselves can build - a patch whose two ends
// are each either a free boundary or a plain corner-notch (NOT an
// oblique-end CYLINDRICAL fillet's own sloped ellipse notch, which is a
// genuinely different, not-yet-inverted construction - see
// FilletConvexEdge's own doc comment for why that case's cylinder is
// shifted/set back in a way this function does not attempt to undo, and
// NOT a spherical vertex-blend corner from FilletConvexEdges) - throwing
// std::invalid_argument for any of those harder cases rather than
// silently restoring the wrong shape. Removing one segment of an
// N-station tapered profile restores only that segment's own straight
// span, leaving any adjacent segments' own cones in place with a short
// straight edge spliced between them - well-defined, if partial,
// behavior, not a bug; removing every segment of a profile in turn fully
// restores the original straight edge.
Brep RemoveBlend(const Brep& solid, Point3d point_on_fillet);


// BLEND REMOVAL for a CHAMFER: the inverse of ChamferConvexEdge, restoring
// the original sharp edge a two-distance (or distance+angle) chamfer
// replaced with a flat strip - purely from the chamfered solid's own
// geometry, the same "read it back out" spirit RemoveBlend's own doc
// comment describes for the fillet case.
//
// UNLIKE a fillet, a chamfer face is an ORDINARY Brep::PlanarFace - there
// is no dedicated face type MixedFaces() can hand back to identify it, so
// `point_on_chamfer` is matched against `solid.PlanarFaces()` directly
// (the closest face, measured by distance to its own trimmed polygon,
// wins) and this function must GEOMETRICALLY verify the candidate face
// really is a chamfer before touching anything - never assumed from
// being merely the nearest quad.
//
// The construction:
//   1. The candidate face must be a quad (exactly 4 loop points) -
//      throws std::invalid_argument otherwise (a chamfer is always a
//      quad, by ChamferConvexEdge's own construction).
//   2. A chamfer quad's own two RAIL edges (shared with the two faces
//      the chamfer was built between) are a pair of OPPOSITE edges of
//      the quad - the other opposite pair are the two END conditions.
//      There are only two ways to split a quad's 4 edges into opposite
//      pairs; this function tries BOTH and requires EXACTLY ONE to
//      reconstruct successfully (see step 3), throwing
//      std::invalid_argument if zero or both do - the discriminating
//      check is genuine, not a coin flip: for the WRONG pairing, the two
//      candidate "rail" edges' own neighbouring faces are frequently
//      PARALLEL to each other (e.g. treating a box chamfer's own two END
//      edges, bordering the box's own parallel side faces, as if they
//      were rails), which makes plane_a x plane_b degenerate and fails
//      immediately; when it does not fail outright, the deeper check in
//      step 3 (both candidate rail corners projecting to the SAME
//      restored edge endpoint) still catches it. The neighbouring-face
//      search itself excludes the candidate quad's OWN index - each of
//      its 4 edges trivially "matches itself" otherwise (the quad's own
//      loop obviously contains its own edges), which on a solid with a
//      second, independently-built chamfer elsewhere can shift the quad
//      to an array index earlier than its genuine neighbour and produce
//      a spurious self-match instead of ever reaching the real one;
//      caught by a two-chamfer regression, not assumed from the single-
//      chamfer case alone.
//   3. For a candidate rail pair with neighbouring faces a/b: e =
//      normalize(n_a x n_b) (throws if degenerate - parallel candidate
//      faces, an immediate sign the pairing is wrong); a point on the
//      two planes' own intersection line follows the standard closed
//      form P0 = ((d_a*n_b - d_b*n_a) x e) / |e|^2 (d_a = n_a . plane_a.
//      origin, d_b likewise); each of the pair's own two quad corners is
//      then projected onto that line (P0 + ((corner - P0).e)*e) to
//      recover a candidate edge_p0/edge_p1 - and the OTHER pair's own
//      two corners (the ones NOT used for the projection) must
//      independently project to the SAME two points, within tolerance,
//      for this pairing to be accepted; a genuinely discriminating cross-
//      check (not vacuous - the two projections use different starting
//      corners on different edges of the quad).
//   4. Face a's and face b's own loops are re-trimmed by replacing their
//      shared rail edge with the restored sharp edge - the exact inverse
//      of ChamferConvexEdge's own half-space re-trim.
//   5. Either END of the chamfer, if it met a third face there (see
//      ChamferEndAtVertex's own doc comment: a chamfer's own end
//      condition splices exactly two adjacent points into that face's
//      loop, never a dense polyline - the flat, purely planar sibling of
//      a fillet's own arc notch), has that two-point edge collapsed back
//      to the single restored vertex, via the same CollapseNotchRun this
//      file's own fillet-removal path uses (now also handling a plain
//      2-point run, not only a dense one). A free boundary end needs no
//      change and gets none.
//   6. The chamfer face itself is dropped; every other face of `solid`
//      is carried through unchanged via Brep::FromMixedFaces.
//
// SCOPE: reverses exactly what ChamferConvexEdge/ChamferConvexEdgeAngle
// themselves can build (both dispatch to the same two-distance
// construction, so this one inverse covers both). A solid already
// carrying a curved face is out of scope, since PlanarFaces() itself
// rejects it.
Brep RemoveChamfer(const Brep& solid, Point3d point_on_chamfer);

}  // namespace dino8::kernel
