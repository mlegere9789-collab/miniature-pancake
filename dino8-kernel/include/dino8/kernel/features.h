#pragma once

#include <vector>

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

// One plain round hole recognized on an existing solid (parity-map
// "Feature recognition" - the "hole" half of its own "still no
// hole/boss/pocket recognition" gap): the geometric inverse of
// MakeHole()/CounterboreHole() - given a Brep that already HAS a
// cylindrical bore cut into it (by any means - this reads back only the
// finished shape, not any feature history), recovers the same
// (origin, axis, radius, depth, through) parameters a caller could feed
// straight back into MakeHole() to reproduce it.
//
// `origin` is the point on the axis at the hole's own entry rim (where it
// meets the surface it was drilled from) - the same point MakeHole()'s own
// `center` parameter describes, not the axis's arbitrary base point some
// other representation might use. `axis` is a unit vector pointing INTO
// the material, matching MakeHole()'s own "points into the material"
// convention. `depth` is the axial distance from `origin` to the hole's
// own far end - the flat bottom of a blind hole, or the exit rim of a
// through hole (still a finite, measured number in the through case too,
// unlike MakeHole()'s own `through=true`, which ignores its `depth`
// argument entirely and drives the cutting tool arbitrarily far past
// `solid`'s bounding box instead).
struct HoleFeature {
  Point3d origin;
  Vector3d axis;
  double radius = 0.0;
  double depth = 0.0;
  bool through = false;

  // Index into `solid.raw().m_F` (the ON_Brep this was recognized from) of
  // the cylindrical face this feature came from - lets a caller correlate
  // a returned HoleFeature back to the specific face of its own input,
  // e.g. to select it in a UI.
  int face_index = -1;
};

// Scans every face of `solid` for a genuine round hole: a face whose
// underlying surface fits a full (closed, 2*pi) cylinder (`ON_Surface::
// IsCylinder()` - so a PARTIAL cylindrical patch, e.g. a fillet's own
// rolling-ball wall, is never reported: it isn't itself a hole, whatever
// solid it happens to be attached to) AND whose face orientation is
// CONCAVE - its own outward-facing normal (`ON_Surface::NormalAt()`,
// flipped per `ON_BrepFace::m_bRev` exactly like every other
// orientation-aware read in this kernel, e.g. BossRibCommand's own
// ProjectToBase in dino8-app) points TOWARD the cylinder's own axis rather
// than away from it. A convex full cylinder (a boss/pin sticking OUT of
// `solid`, the opposite winding) is deliberately not reported here - see
// this function's own "Still partial" note below.
//
// For each recognized cylindrical face, its own axial extent [t_min,
// t_max] is read directly off every point of every edge in every one of
// its loops (a plain global min/max along the axis - no loop/trim
// ADJACENCY reasoning at all, so this is immune to BooleanCombineGeneral's
// own disclosed fragmentation of a rim into many short polyline segments,
// or even a genuinely naked one - see MakeHole()'s own doc comment on the
// "tool's far end floats entirely inside the target" entry-rim gap). Each
// of the face's own two ends is then classified OPEN (the bore continues
// past it, into open air) or CAPPED (more of `solid`'s own material blocks
// it) by tessellating `solid` once (`Brep::TessellateToClosedMesh()`) and
// asking `Mesh::ContainsPoint()` about a point a small margin PAST that
// end, ON the cylinder's own axis - deliberately at radius 0 from the
// bore, nowhere near the specific radius-== -hole-radius locus the
// entry-rim gap above sits at, so this sidesteps that disclosed gap
// entirely rather than working around it. A hole with one open end and
// one capped end is `through = false`, `depth` the axial distance from the
// open end to the capped one. A hole with BOTH ends open is `through =
// true`, `depth` the axial distance end-to-end (a real, finite
// measurement, even though MakeHole() itself never needs one for a
// through hole - see HoleFeature's own doc comment). A hole found with
// BOTH ends capped (an entirely enclosed cylindrical cavity, never
// reaching any outer face of `solid` at all) is not a hole feature in the
// Rhino/SolidWorks "Hole" sense - nothing drilled it from outside - and is
// silently skipped, the same way a convex/boss cylinder is.
//
// Every recognized face_index is independent: a solid with several holes
// (through, blind, or a mix) is reported as several HoleFeature entries,
// in face-index order, not merged or deduplicated.
//
// Still partial, not a full "hole/boss/pocket" recognizer: a genuinely
// convex cylindrical boss/pin and a general (non-cylindrical) pocket are
// both out of scope here entirely - see this function's own "concave
// cylinder only" filter above; no attempt is made here to recognize a
// COUNTERBORE/COUNTERSINK's own second, wider cylindrical/conical step as
// part of the SAME feature - each cylindrical wall segment of a stepped
// hole comes back as its own separate HoleFeature; and the open/capped
// classification inherits Mesh::ContainsPoint()'s own disclosed "closed,
// consistently-oriented mesh" precondition (mesh.h) - a `solid` whose
// tessellation is itself unreliably closed somewhere ELSE (unrelated to
// the hole being recognized) could in principle still misclassify, though
// no such case is known.
std::vector<HoleFeature> RecognizeHoles(const Brep& solid);

}  // namespace dino8::kernel
