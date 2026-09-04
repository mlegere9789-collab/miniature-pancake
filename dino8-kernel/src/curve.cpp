#include "dino8/kernel/curve.h"

#include <stdexcept>

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

BoundingBox NurbsCurve::GetTightBoundingBox() const {
  ON_BoundingBox box;
  if (!curve_.GetTightBoundingBox(box)) {
    throw std::runtime_error(
        "dino8::kernel::NurbsCurve::GetTightBoundingBox: ON_Curve::"
        "GetTightBoundingBox failed");
  }
  return BoundingBox{box.Min(), box.Max()};
}

double NurbsCurve::Length(int samples) const {
  const ON_Interval domain = curve_.Domain();
  Point3d previous = PointAt(domain.ParameterAt(0.0));
  double length = 0.0;
  for (int i = 1; i <= samples; ++i) {
    const Point3d current = PointAt(domain.ParameterAt(static_cast<double>(i) / samples));
    length += (current - previous).Length();
    previous = current;
  }
  return length;
}

Vector3d NurbsCurve::TangentAt(double t) const { return curve_.TangentAt(t); }

bool NurbsCurve::IsClosed() const { return curve_.IsClosed(); }

bool NurbsCurve::IsPeriodic() const { return curve_.IsPeriodic(); }

Result NurbsCurve::Reverse() { return curve_.Reverse() ? Result::Ok : Result::Failed; }

Result NurbsCurve::Trim(double t0, double t1) {
  if (t0 >= t1) {
    return Result::Failed;
  }
  return curve_.Trim(ON_Interval(t0, t1)) ? Result::Ok : Result::Failed;
}

}  // namespace dino8::kernel
