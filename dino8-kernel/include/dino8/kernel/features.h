#pragma once

#include "dino8/kernel/brep.h"

namespace dino8::kernel {

// A counterbore hole: a coaxial pair of cylindrical drilling cutters - a
// narrow DRILL cylinder (the hole's own through/blind bore) plus a wider,
// shallower COUNTERBORE cylinder recessed at the starting face, sharing
// the same axis and starting point, e.g. the socket a hex-head bolt sits
// in above its own shaft hole.
//
// Not a new boolean engine: composed as a single compound cutter Brep -
// two coaxial Brep::CylindricalFace entries covering ADJACENT,
// non-overlapping axial ranges (the wide counterbore wall over
// [0, counterbore_depth], the narrow drill wall over
// [counterbore_depth, drill_depth]) - fed into ONE
// BooleanCombineMixed(..., BooleanOp::Difference) pass against `solid`
// (see boolean.h's own BooleanCombineMixed doc comment for that
// pipeline). Deliberately NOT two full-length, axially-overlapping
// cylinders composed via two sequential Difference passes (in either
// order) or via a prior Union of two overlapping bare cylinders - both
// were tried and both fail (see features.cpp's own doc comment on
// CounterboreHole for exactly how); adjacent, non-overlapping segments
// sidestep the issue entirely, since `solid`'s own starting face is then
// crossed by only the counterbore's own (outer) circle, `solid`'s own far
// face (if the drill reaches it) by only the drill's own (inner) circle,
// and the step between the two cutter radii is a brand-new face this
// call synthesizes, not a second clip of an already-circular one.
//
// `origin` is the point on `solid`'s own surface where the hole starts;
// `axis` points INTO the material (the direction the hole is drilled) -
// it need not be a unit vector, but must be nonzero. `drill_radius`/
// `drill_depth` describe the narrow bore exactly like a plain single-
// cylinder hole (pass a `drill_depth` that reaches all the way through
// `solid` for a through hole, or less for a blind one); `counterbore_radius`/
// `counterbore_depth` describe the wider recess at the starting face.
//
// `axis` need not be perpendicular to `solid`'s own surface at `origin` -
// an oblique hole is built the same way - but a steep-enough tilt
// relative to `counterbore_radius`/`drill_radius` can still hit
// BooleanCombineMixed's own pre-existing, disclosed "oblique plane's own
// intersection with a cylindrical face enters/exits across only part of
// the swept angle" non-monotonic-crossing limitation (confirmed directly:
// this is not specific to this feature's own composition, and margin
// past the surface does not avoid it), in which case this throws
// whatever BooleanCombineMixed itself throws for that case.
//
// Throws std::invalid_argument if `axis` is zero-length, if either
// radius or depth is not strictly positive, if `counterbore_radius` is
// not strictly greater than `drill_radius` (otherwise the "wider" cutter
// does not actually widen anything, so this would not be a counterbore),
// or if `counterbore_depth` is not strictly less than `drill_depth` (a
// counterbore needs an actual narrower bore beyond its own recess).
Brep CounterboreHole(const Brep& solid, Point3d origin, Vector3d axis, double drill_radius, double drill_depth,
                      double counterbore_radius, double counterbore_depth);

}  // namespace dino8::kernel
