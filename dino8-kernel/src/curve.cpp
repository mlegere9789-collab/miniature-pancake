#include "dino8/kernel/curve.h"

namespace dino8::kernel {

NurbsCurve NurbsCurve::FromControlPoints(const std::vector<Point3d>& control_points,
                                          int degree) {
  NurbsCurve result;
  const int order = degree + 1;
  const int cv_count = static_cast<int>(control_points.size());
  result.curve_.Create(/*dimension=*/3, /*is_rational=*/false, order, cv_count);

  for (int i = 0; i < cv_count; ++i) {
    result.curve_.SetCV(i, control_points[static_cast<size_t>(i)]);
  }

  // Clamped, uniform knot vector — matches the "straightforward
  // construction" this chunk promises; a real curve-fit/knot-spacing
  // strategy belongs to whichever later chunk actually needs it.
  result.curve_.MakeClampedUniformKnotVector();

  return result;
}

int NurbsCurve::Degree() const { return curve_.Degree(); }

int NurbsCurve::ControlPointCount() const { return curve_.CVCount(); }

Result NurbsCurve::ElevateDegree(int new_degree) {
  if (new_degree <= Degree()) {
    return Result::NoOpAlreadySatisfied;
  }
  const bool ok = curve_.IncreaseDegree(new_degree);
  return ok ? Result::Ok : Result::Failed;
}

Point3d NurbsCurve::PointAt(double t) const {
  ON_3dPoint pt;
  curve_.EvPoint(t, pt);
  return pt;
}

}  // namespace dino8::kernel
