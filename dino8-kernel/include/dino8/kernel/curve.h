#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::kernel {

// Wraps ON_NurbsCurve. Deliberately exposes the underlying ON_NurbsCurve
// (via raw()) rather than re-declaring every accessor OpenNURBS already
// has — later chunks (booleans, display) need the real object, not a
// facade that only covers what chunk 1 happened to need.
class NurbsCurve {
 public:
  // Builds a degree-`degree` NURBS curve interpolating a polyline through
  // `control_points` with uniform-ish knots. Not a general-purpose curve
  // fit — just enough to construct a testable curve without pulling in a
  // fitting algorithm this chunk doesn't own.
  static NurbsCurve FromControlPoints(const std::vector<Point3d>& control_points,
                                       int degree);

  int Degree() const;
  int ControlPointCount() const;

  // Elevates the curve's degree in place. Returns NoOpAlreadySatisfied if
  // `new_degree <= Degree()`.
  Result ElevateDegree(int new_degree);

  Point3d PointAt(double t) const;

  // Approximate arc length via polyline sampling: evaluates `samples + 1`
  // points evenly across the curve's own parameter domain and sums the
  // straight-line distance between consecutive ones. This is a
  // from-scratch implementation, not a wrapper - verified directly
  // against the v8.34 source that OpenNURBS' public `ON_Curve` API has no
  // `GetLength()`/arc-length method at all (grepped the whole source
  // tree, not just this class), the same "declared for Rhino, not present
  // in the public build" pattern chunk 2 found for `ON_Brep::CreateMesh`
  // and this chunk found for `ON_SubD::BrepForm`. A polyline's chord
  // length always understates a smooth curve's true arc length, so this
  // converges to the true length from below as `samples` increases - for
  // a straight-line curve (no curvature to approximate) it's exact at any
  // sample count.
  double Length(int samples = 1000) const;

  const ON_NurbsCurve& raw() const { return curve_; }
  ON_NurbsCurve& raw() { return curve_; }

 private:
  ON_NurbsCurve curve_;
};

}  // namespace dino8::kernel
