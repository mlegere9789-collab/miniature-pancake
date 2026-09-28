#include "dino8/kernel/features.h"

#include <stdexcept>

#include "dino8/kernel/boolean.h"

namespace dino8::kernel {

namespace {

// An arbitrary orthonormal frame with `axis` (unitized) as its z-axis -
// a full 2*pi CylindricalFace sweep doesn't care where the angle-0 seam
// sits, so any perpendicular x/y pair works. Same manual field-setting +
// UpdateEquation() convention CylindricalFace's own producers already
// use (see dino8-kernel/tests/test_basic.cpp's BuildDrilledBoxInputs),
// generalized from an axis-aligned z to an arbitrary one via a plain
// Gram-Schmidt step.
ON_Plane MakeAxisFrame(Point3d origin, Vector3d axis) {
  Vector3d z = axis;
  if (!z.Unitize()) {
    throw std::invalid_argument("dino8::kernel::CounterboreHole: axis must be nonzero");
  }
  const Vector3d seed = (std::fabs(z.x) < 0.9) ? Vector3d(1, 0, 0) : Vector3d(0, 1, 0);
  Vector3d x = seed - z * ON_DotProduct(seed, z);
  x.Unitize();
  Vector3d y = ON_CrossProduct(z, x);
  y.Unitize();

  ON_Plane frame;
  frame.origin = origin;
  frame.xaxis = x;
  frame.yaxis = y;
  frame.zaxis = z;
  frame.UpdateEquation();
  return frame;
}

}  // namespace

Brep CounterboreHole(const Brep& solid, Point3d origin, Vector3d axis, double drill_radius, double drill_depth,
                      double counterbore_radius, double counterbore_depth) {
  if (!(drill_radius > 0.0)) {
    throw std::invalid_argument("dino8::kernel::CounterboreHole: drill_radius must be positive");
  }
  if (!(drill_depth > 0.0)) {
    throw std::invalid_argument("dino8::kernel::CounterboreHole: drill_depth must be positive");
  }
  if (!(counterbore_radius > drill_radius)) {
    throw std::invalid_argument(
        "dino8::kernel::CounterboreHole: counterbore_radius must be strictly greater than drill_radius");
  }
  if (!(counterbore_depth > 0.0)) {
    throw std::invalid_argument("dino8::kernel::CounterboreHole: counterbore_depth must be positive");
  }
  if (!(counterbore_depth < drill_depth)) {
    throw std::invalid_argument(
        "dino8::kernel::CounterboreHole: counterbore_depth must be strictly less than drill_depth - a counterbore "
        "needs an actual narrower bore beyond its own recess");
  }

  const ON_Plane frame = MakeAxisFrame(origin, axis);

  // A single compound cutter Brep with TWO CylindricalFaces covering
  // ADJACENT, NON-OVERLAPPING axial ranges - the wide counterbore wall
  // over [0, counterbore_depth], the narrow drill wall over
  // [counterbore_depth, drill_depth] - fed into ONE
  // BooleanCombineMixed(..., BooleanOp::Difference) pass against `solid`.
  //
  // This is deliberately NOT built as two full-length, axially-OVERLAPPING
  // cylinders (one for the whole bore, one for the recess) composed via
  // two sequential Difference passes against `solid`, nor via a prior
  // Union of two such overlapping bare cylinders: both were tried and
  // both fail. Two sequential Difference passes against `solid` (in
  // either order) eventually clip some face that already has a circular
  // boundary (either `solid`'s own starting face, hit by both the drill's
  // and the counterbore's circle in turn, or the counterbore's own
  // freshly-cut recess-bottom disc, itself circular, later re-clipped by
  // the drill's circle) against a SECOND, different circle -
  // ClipPolygonByCircle3d's own doc comment (boolean.h) explicitly
  // disclaims exactly this ("a once-already-clipped, non-rectangular poly
  // losing convexity for a second interacting circle" is out of scope),
  // confirmed directly: it throws std::invalid_argument in practice.
  // Unioning two full-length overlapping bare (uncapped) cylinders first -
  // the seemingly obvious alternative - was also tried and confirmed
  // directly to produce a wrong volume and a non-manifold result: that
  // pairing is not one of BooleanCombineMixed's own documented
  // CYLINDER/CYLINDER cases (those are all stated for a cylinder against
  // an otherwise-real solid, not two bare open tubes against each other).
  //
  // Adjacent, non-overlapping segments sidestep the whole issue: `solid`'s
  // own starting face is only ever crossed by the counterbore's own
  // (outer, wider) circle, `solid`'s own far face (if the drill reaches
  // it) is only ever crossed by the drill's own (inner, narrower) circle,
  // and the step between the two cutter radii is an entirely NEW face
  // this call itself synthesizes - not a second clip of any pre-existing
  // one. Confirmed directly (see this feature's own tests): the result is
  // `raw().IsValid()`, its tessellated volume matches the hand-derived
  // value, and `TessellateToClosedMeshConforming()`'s own mesh is a
  // genuine `IsClosedManifold()`.
  Brep::CylindricalFace counterbore_cf;
  counterbore_cf.frame = frame;
  counterbore_cf.radius = counterbore_radius;
  counterbore_cf.angle = 2.0 * ON_PI;
  counterbore_cf.length = counterbore_depth;

  Brep::CylindricalFace drill_cf;
  drill_cf.frame = frame;
  drill_cf.frame.origin = frame.PointAt(0, 0, counterbore_depth);
  drill_cf.radius = drill_radius;
  drill_cf.angle = 2.0 * ON_PI;
  drill_cf.length = drill_depth - counterbore_depth;

  const Brep tool = Brep::FromMixedFaces({}, {counterbore_cf, drill_cf});
  return BooleanCombineMixed(solid, tool, BooleanOp::Difference);
}

}  // namespace dino8::kernel
