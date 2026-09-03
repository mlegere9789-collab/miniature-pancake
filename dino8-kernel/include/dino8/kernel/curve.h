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

  const ON_NurbsCurve& raw() const { return curve_; }
  ON_NurbsCurve& raw() { return curve_; }

 private:
  ON_NurbsCurve curve_;
};

}  // namespace dino8::kernel
