#include "dino8/kernel/curve.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dino8::kernel {

namespace {

void SubdivideForFlatness(const NurbsCurve& curve, double t0, double t1, double chord_tolerance,
                           int depth, int max_depth, std::vector<double>& out) {
  const Point3d p0 = curve.PointAt(t0);
  const Point3d p1 = curve.PointAt(t1);
  const double tm = 0.5 * (t0 + t1);
  const Point3d pm = curve.PointAt(tm);
  const Point3d chord_mid((p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5, (p0.z + p1.z) * 0.5);
  const double deviation = (pm - chord_mid).Length();
  if (deviation > chord_tolerance && depth < max_depth) {
    SubdivideForFlatness(curve, t0, tm, chord_tolerance, depth + 1, max_depth, out);
    SubdivideForFlatness(curve, tm, t1, chord_tolerance, depth + 1, max_depth, out);
  } else {
    out.push_back(t1);
  }
}

}  // namespace

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

bool NurbsCurve::IsRational() const { return curve_.IsRational(); }

double NurbsCurve::WeightAt(int i) const { return curve_.Weight(i); }

Result NurbsCurve::ElevateDegree(int new_degree) {
  if (new_degree <= Degree()) {
    return Result::NoOpAlreadySatisfied;
  }
  const bool ok = curve_.IncreaseDegree(new_degree);
  return ok ? Result::Ok : Result::Failed;
}

Interval NurbsCurve::Domain() const {
  const ON_Interval domain = curve_.Domain();
  return Interval{domain.Min(), domain.Max()};
}

Point3d NurbsCurve::PointAt(double t) const {
  ON_3dPoint pt;
  curve_.EvPoint(t, pt);
  return pt;
}

Vector3d NurbsCurve::CurvatureAt(double t) const { return curve_.CurvatureAt(t); }

int NurbsCurve::SuggestedSamples(double chord_tolerance, int curvature_samples) const {
  if (chord_tolerance <= 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::SuggestedSamples: chord_tolerance must "
        "be positive");
  }

  const ON_Interval domain = curve_.Domain();
  double max_kappa = 0.0;
  for (int i = 0; i <= curvature_samples; ++i) {
    const double t = domain.ParameterAt(static_cast<double>(i) / curvature_samples);
    max_kappa = std::max(max_kappa, CurvatureAt(t).Length());
  }

  if (max_kappa < 1e-12) {
    return 1;  // negligible curvature everywhere - a straight line needs one segment
  }

  const double radius = 1.0 / max_kappa;
  // Chord-height (sagitta) formula for a circular arc of radius R:
  // sagitta = R * (1 - cos(half_angle)). Solve for the largest angular
  // step whose sagitta stays within chord_tolerance.
  const double clamped_ratio = std::min(1.0, chord_tolerance / radius);
  const double max_angle_step = 2.0 * std::acos(1.0 - clamped_ratio);
  // Total turning angle assuming the whole curve turns at the tightest
  // radius found - the conservative approximation this method documents.
  const double total_angle = Length() / radius;
  return std::max(1, static_cast<int>(std::ceil(total_angle / max_angle_step)));
}

std::vector<double> NurbsCurve::SuggestedParameterValues(double chord_tolerance,
                                                          int max_depth) const {
  if (chord_tolerance <= 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::SuggestedParameterValues: chord_tolerance "
        "must be positive");
  }

  const ON_Interval domain = curve_.Domain();
  std::vector<double> out;
  out.push_back(domain.Min());
  SubdivideForFlatness(*this, domain.Min(), domain.Max(), chord_tolerance, 0, max_depth, out);
  return out;
}

double NurbsCurve::ClosestPointParameter(Point3d point, int samples) const {
  const ON_Interval domain = curve_.Domain();
  auto distance_squared = [&](double t) { return (PointAt(t) - point).LengthSquared(); };

  double best_t = domain.Min();
  double best_d2 = distance_squared(best_t);
  for (int i = 1; i <= samples; ++i) {
    const double t = domain.ParameterAt(static_cast<double>(i) / samples);
    const double d2 = distance_squared(t);
    if (d2 < best_d2) {
      best_d2 = d2;
      best_t = t;
    }
  }

  const double step = domain.Length() / samples;
  double lo = std::max(domain.Min(), best_t - step);
  double hi = std::min(domain.Max(), best_t + step);

  const double golden_ratio = (std::sqrt(5.0) - 1.0) / 2.0;
  double c = hi - golden_ratio * (hi - lo);
  double d = lo + golden_ratio * (hi - lo);
  for (int iter = 0; iter < 100 && (hi - lo) > 1e-13; ++iter) {
    if (distance_squared(c) < distance_squared(d)) {
      hi = d;
    } else {
      lo = c;
    }
    c = hi - golden_ratio * (hi - lo);
    d = lo + golden_ratio * (hi - lo);
  }
  return (lo + hi) / 2.0;
}

Point3d NurbsCurve::ClosestPoint(Point3d point, int samples) const {
  return PointAt(ClosestPointParameter(point, samples));
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

double NurbsCurve::ParameterAtArcLength(double target_length, int samples) const {
  const ON_Interval domain = curve_.Domain();
  if (target_length <= 0.0) {
    return domain.Min();
  }

  double previous_length = 0.0;
  Point3d previous_point = PointAt(domain.Min());
  double previous_t = domain.Min();
  for (int i = 1; i <= samples; ++i) {
    const double t = domain.ParameterAt(static_cast<double>(i) / samples);
    const Point3d point = PointAt(t);
    const double segment_length = (point - previous_point).Length();
    const double cumulative_length = previous_length + segment_length;
    if (cumulative_length >= target_length) {
      if (segment_length < 1e-15) {
        return t;
      }
      const double fraction = (target_length - previous_length) / segment_length;
      return previous_t + fraction * (t - previous_t);
    }
    previous_length = cumulative_length;
    previous_point = point;
    previous_t = t;
  }
  return domain.Max();
}

std::vector<double> NurbsCurve::DivideByCount(int count, int samples) const {
  if (count <= 0) {
    throw std::invalid_argument("dino8::kernel::NurbsCurve::DivideByCount: count must be positive");
  }

  const ON_Interval domain = curve_.Domain();
  const double total_length = Length(samples);
  std::vector<double> values;
  values.reserve(static_cast<size_t>(count) + 1);
  values.push_back(domain.Min());
  for (int i = 1; i < count; ++i) {
    values.push_back(ParameterAtArcLength(total_length * static_cast<double>(i) / count, samples));
  }
  values.push_back(domain.Max());
  return values;
}

Vector3d NurbsCurve::TangentAt(double t) const { return curve_.TangentAt(t); }

bool NurbsCurve::IsClosed() const { return curve_.IsClosed(); }

bool NurbsCurve::IsPeriodic() const { return curve_.IsPeriodic(); }

bool NurbsCurve::IsPlanar(double tolerance) const { return curve_.IsPlanar(nullptr, tolerance); }

bool NurbsCurve::IsLinear(double tolerance) const { return curve_.IsLinear(tolerance); }

bool NurbsCurve::IsArc(double tolerance) const { return curve_.IsArc(nullptr, nullptr, tolerance); }

bool NurbsCurve::IsCircle(double tolerance) const {
  ON_Arc arc;
  if (!curve_.IsArc(nullptr, &arc, tolerance)) {
    return false;
  }
  return arc.IsCircle();
}

Result NurbsCurve::Reverse() { return curve_.Reverse() ? Result::Ok : Result::Failed; }

Result NurbsCurve::Trim(double t0, double t1) {
  if (t0 >= t1) {
    return Result::Failed;
  }
  return curve_.Trim(ON_Interval(t0, t1)) ? Result::Ok : Result::Failed;
}

Result NurbsCurve::Extend(double t0, double t1) {
  if (t0 >= t1) {
    return Result::Failed;
  }
  if (curve_.IsClosed()) {
    return Result::Failed;
  }
  const ON_Interval current = curve_.Domain();
  if (t0 >= current.Min() && t1 <= current.Max()) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.Extend(ON_Interval(t0, t1)) ? Result::Ok : Result::Failed;
}

Result NurbsCurve::Split(double t, NurbsCurve& out_left, NurbsCurve& out_right) const {
  ON_Curve* left_curve = nullptr;
  ON_Curve* right_curve = nullptr;
  const bool ok = curve_.Split(t, left_curve, right_curve);
  if (!ok) {
    delete left_curve;
    delete right_curve;
    return Result::Failed;
  }

  ON_NurbsCurve* left_nurbs = ON_NurbsCurve::Cast(left_curve);
  ON_NurbsCurve* right_nurbs = ON_NurbsCurve::Cast(right_curve);
  if (left_nurbs == nullptr || right_nurbs == nullptr) {
    delete left_curve;
    delete right_curve;
    return Result::Failed;
  }

  out_left.curve_ = *left_nurbs;
  out_right.curve_ = *right_nurbs;
  delete left_curve;
  delete right_curve;
  return Result::Ok;
}

}  // namespace dino8::kernel
