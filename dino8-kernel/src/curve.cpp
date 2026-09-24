#include "dino8/kernel/curve.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "dino8/kernel/tolerance.h"

#include "dino8/kernel/detail/degree_elevate.h"

namespace dino8::kernel {

namespace {

// Which of the two possible in-plane perpendicular directions `dir`
// belongs to, relative to a geometrically-known "true outward" direction
// at the same point - same reasoning as NurbsSurface::OffsetAnalytic()'s
// own OffsetNormalSign() (surface.cpp), duplicated here rather than
// shared since the two files have no common detail header for it.
double OffsetSignAlong(const Vector3d& dir, const Vector3d& true_outward) {
  return ON_DotProduct(dir, true_outward) >= 0.0 ? 1.0 : -1.0;
}

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

// Value of basis function N_i(t) for the clamped B-spline defined by
// `knot` (ON's own compressed convention), `cv_count` control points and
// `order` = degree + 1. Used to build the least-squares normal equations
// in FitLeastSquares() below - real Cox-de Boor evaluation via
// OpenNURBS' own ON_NurbsSpanIndex/ON_EvaluateNurbsBasis, not an
// approximation of it.
double BasisValue(const std::vector<double>& knot, int cv_count, int order, int i, double t) {
  const int span = ON_NurbsSpanIndex(order, cv_count, knot.data(), t, 0, 0);
  if (i < span || i > span + order - 1) return 0.0;
  // ON_EvaluateNurbsBasis writes a full order-by-order triangular table
  // (every degree up to `degree`, not just the final row) - its own doc
  // comment spells out "If N were declared as double N[order][order]".
  // Passing a buffer of only `order` doubles here (an earlier draft's
  // bug, caught by the corruption it caused) overruns it; the degree-d
  // values this function actually wants are the first `order` entries.
  std::vector<double> B(static_cast<size_t>(order) * static_cast<size_t>(order));
  ON_EvaluateNurbsBasis(order, knot.data() + span, t, B.data());
  return B[static_cast<size_t>(i - span)];
}

// Solves the dense linear system A*X = B in place via Gaussian
// elimination with partial pivoting. A is `size` x `size` (row-major),
// B is `size` x `rhs_count` (row-major) and is overwritten with the
// solution X. Returns false if A is (numerically) singular.
bool SolveLinearSystem(std::vector<double>& a, std::vector<double>& b, int size, int rhs_count) {
  for (int col = 0; col < size; ++col) {
    int pivot_row = col;
    double pivot_val = std::abs(a[static_cast<size_t>(col * size + col)]);
    for (int row = col + 1; row < size; ++row) {
      const double v = std::abs(a[static_cast<size_t>(row * size + col)]);
      if (v > pivot_val) { pivot_val = v; pivot_row = row; }
    }
    if (pivot_val < 1e-14) return false;
    if (pivot_row != col) {
      for (int k = 0; k < size; ++k) std::swap(a[static_cast<size_t>(col * size + k)], a[static_cast<size_t>(pivot_row * size + k)]);
      for (int k = 0; k < rhs_count; ++k) std::swap(b[static_cast<size_t>(col * rhs_count + k)], b[static_cast<size_t>(pivot_row * rhs_count + k)]);
    }
    const double diag = a[static_cast<size_t>(col * size + col)];
    for (int row = 0; row < size; ++row) {
      if (row == col) continue;
      const double factor = a[static_cast<size_t>(row * size + col)] / diag;
      if (factor == 0.0) continue;
      for (int k = col; k < size; ++k) a[static_cast<size_t>(row * size + k)] -= factor * a[static_cast<size_t>(col * size + k)];
      for (int k = 0; k < rhs_count; ++k) b[static_cast<size_t>(row * rhs_count + k)] -= factor * b[static_cast<size_t>(col * rhs_count + k)];
    }
  }
  for (int row = 0; row < size; ++row) {
    const double diag = a[static_cast<size_t>(row * size + row)];
    for (int k = 0; k < rhs_count; ++k) b[static_cast<size_t>(row * rhs_count + k)] /= diag;
  }
  return true;
}

}  // namespace

Result NurbsCurve::FitLeastSquares(const std::vector<Point3d>& points, int degree, int control_point_count,
                                    NurbsCurve& out) {
  const int m_plus_1 = static_cast<int>(points.size());
  const int p = degree;
  const int order = p + 1;
  const int n_plus_1 = control_point_count;
  const int n = n_plus_1 - 1;
  const int m = m_plus_1 - 1;
  if (m_plus_1 < 2 || p < 1 || n_plus_1 < order || n_plus_1 > m_plus_1) {
    return Result::Failed;
  }

  // Chord-length parameterization, u_bar[0..m] in [0, 1].
  std::vector<double> u_bar(static_cast<size_t>(m_plus_1));
  {
    double total = 0.0;
    std::vector<double> seg(static_cast<size_t>(m_plus_1), 0.0);
    for (int k = 1; k <= m; ++k) {
      seg[static_cast<size_t>(k)] = (points[static_cast<size_t>(k)] - points[static_cast<size_t>(k - 1)]).Length();
      total += seg[static_cast<size_t>(k)];
    }
    u_bar[0] = 0.0;
    u_bar[static_cast<size_t>(m)] = 1.0;
    if (total <= 0.0) {
      for (int k = 1; k < m; ++k) u_bar[static_cast<size_t>(k)] = static_cast<double>(k) / m;
    } else {
      double acc = 0.0;
      for (int k = 1; k < m; ++k) {
        acc += seg[static_cast<size_t>(k)];
        u_bar[static_cast<size_t>(k)] = acc / total;
      }
    }
  }

  // Knot vector via the standard approximation knot-averaging formula
  // (Piegl & Tiller eq. 9.68/9.69): textbook (uncompressed) convention
  // first, length n+p+2, then converted to ON's compressed storage.
  std::vector<double> u_full(static_cast<size_t>(n + p + 2));
  for (int i = 0; i <= p; ++i) u_full[static_cast<size_t>(i)] = 0.0;
  for (int i = 0; i <= p; ++i) u_full[static_cast<size_t>(n + 1 + i)] = 1.0;
  const int h = n - p;
  if (h > 0) {
    const double d = static_cast<double>(m_plus_1) / static_cast<double>(n - p + 1);
    for (int j = 1; j <= h; ++j) {
      const double jd = j * d;
      int i = static_cast<int>(jd);
      const double alpha = jd - i;
      if (i < 1) i = 1;
      if (i > m) i = m;
      const double u_im1 = u_bar[static_cast<size_t>(i - 1)];
      const double u_i = u_bar[static_cast<size_t>(i)];
      u_full[static_cast<size_t>(p + j)] = (1.0 - alpha) * u_im1 + alpha * u_i;
    }
  }
  // ON's compressed knot array drops the redundant first/last textbook
  // entries: compressed[k] = u_full[k + 1], length n + p.
  std::vector<double> knot(static_cast<size_t>(n + p));
  for (int k = 0; k < n + p; ++k) knot[static_cast<size_t>(k)] = u_full[static_cast<size_t>(k + 1)];

  const Point3d q0 = points.front();
  const Point3d qm = points.back();

  if (n == 1) {
    // Only the two (fixed) endpoints - no interior unknowns to solve for.
    NurbsCurve result;
    result.curve_.Create(3, false, order, n_plus_1);
    result.curve_.SetCV(0, q0);
    result.curve_.SetCV(1, qm);
    for (int k = 0; k < n + p; ++k) result.curve_.SetKnot(k, knot[static_cast<size_t>(k)]);
    out = result;
    return Result::Ok;
  }

  // Normal equations for the (n-1) interior control points P_1..P_{n-1}.
  const int unknowns = n - 1;
  std::vector<double> a(static_cast<size_t>(unknowns) * static_cast<size_t>(unknowns), 0.0);
  std::vector<double> rhs(static_cast<size_t>(unknowns) * 3, 0.0);
  for (int k = 1; k < m; ++k) {
    const double t = u_bar[static_cast<size_t>(k)];
    const double n0 = BasisValue(knot, n_plus_1, order, 0, t);
    const double nn = BasisValue(knot, n_plus_1, order, n, t);
    const Point3d rk((points[static_cast<size_t>(k)].x - n0 * q0.x - nn * qm.x),
                      (points[static_cast<size_t>(k)].y - n0 * q0.y - nn * qm.y),
                      (points[static_cast<size_t>(k)].z - n0 * q0.z - nn * qm.z));
    std::vector<double> ni(static_cast<size_t>(unknowns));
    for (int i = 1; i <= unknowns; ++i) ni[static_cast<size_t>(i - 1)] = BasisValue(knot, n_plus_1, order, i, t);
    for (int i = 0; i < unknowns; ++i) {
      if (ni[static_cast<size_t>(i)] == 0.0) continue;
      rhs[static_cast<size_t>(i * 3 + 0)] += ni[static_cast<size_t>(i)] * rk.x;
      rhs[static_cast<size_t>(i * 3 + 1)] += ni[static_cast<size_t>(i)] * rk.y;
      rhs[static_cast<size_t>(i * 3 + 2)] += ni[static_cast<size_t>(i)] * rk.z;
      for (int j = 0; j < unknowns; ++j) {
        a[static_cast<size_t>(i * unknowns + j)] += ni[static_cast<size_t>(i)] * ni[static_cast<size_t>(j)];
      }
    }
  }
  if (!SolveLinearSystem(a, rhs, unknowns, 3)) {
    return Result::Failed;
  }

  NurbsCurve result;
  result.curve_.Create(3, false, order, n_plus_1);
  result.curve_.SetCV(0, q0);
  for (int i = 1; i < n; ++i) {
    const Point3d p_i(rhs[static_cast<size_t>((i - 1) * 3 + 0)], rhs[static_cast<size_t>((i - 1) * 3 + 1)],
                       rhs[static_cast<size_t>((i - 1) * 3 + 2)]);
    result.curve_.SetCV(i, p_i);
  }
  result.curve_.SetCV(n, qm);
  for (int k = 0; k < n + p; ++k) result.curve_.SetKnot(k, knot[static_cast<size_t>(k)]);
  out = result;
  return Result::Ok;
}

NurbsCurve NurbsCurve::FromControlPoints(const std::vector<Point3d>& control_points,
                                          int degree) {
  // ON_NurbsCurve::Create() refuses order < 2 or cv_count < order by
  // returning false BEFORE it sets m_order/m_cv_count or allocates m_cv,
  // and the SetCV()/MakeClampedUniformKnotVector() calls below then
  // silently no-op against that never-allocated curve. Without this
  // check the result was a completely empty curve that still looked
  // usable - Degree() 0, ControlPointCount() 0, KnotCount() -2,
  // Domain() == [ON_UNSET_VALUE, ON_UNSET_VALUE], PointAt() == (0, 0, 0)
  // and Length() == 0 for every input (confirmed by a debug run, not
  // assumed) - i.e. a silently wrong result rather than a failure, and
  // reachable from any caller forwarding a user-supplied degree without
  // first clamping it to the point count (the app's Python AddCurve does).
  if (degree < 1) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::FromControlPoints: degree must be at least 1");
  }
  const int order = degree + 1;
  const int cv_count = static_cast<int>(control_points.size());
  if (cv_count < order) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::FromControlPoints: a degree-" + std::to_string(degree) +
        " curve needs at least " + std::to_string(order) + " control points, got " +
        std::to_string(cv_count));
  }
  NurbsCurve result;
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

Result NurbsCurve::SetWeightAt(int i, double weight) {
  if (i < 0 || i >= ControlPointCount()) {
    return Result::Failed;
  }
  if (WeightAt(i) == weight) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.SetWeight(i, weight) ? Result::Ok : Result::Failed;
}

Point3d NurbsCurve::ControlPointAt(int i) const {
  if (i < 0 || i >= ControlPointCount()) {
    throw std::out_of_range(
        "dino8::kernel::NurbsCurve::ControlPointAt: i out of range");
  }
  ON_3dPoint point;
  curve_.GetCV(i, point);
  return point;
}

Result NurbsCurve::SetControlPointAt(int i, Point3d point) {
  if (i < 0 || i >= ControlPointCount()) {
    throw std::out_of_range(
        "dino8::kernel::NurbsCurve::SetControlPointAt: i out of range");
  }
  if (ControlPointAt(i) == point) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.SetCV(i, point) ? Result::Ok : Result::Failed;
}

int NurbsCurve::KnotCount() const { return curve_.KnotCount(); }

double NurbsCurve::KnotAt(int i) const {
  if (i < 0 || i >= KnotCount()) {
    throw std::out_of_range("dino8::kernel::NurbsCurve::KnotAt: i out of range");
  }
  return curve_.Knot(i);
}

Result NurbsCurve::SetKnotAt(int i, double value) {
  if (i < 0 || i >= KnotCount()) {
    return Result::Failed;
  }
  if (curve_.Knot(i) == value) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.SetKnot(i, value) ? Result::Ok : Result::Failed;
}

Result NurbsCurve::InsertKnotAt(double knot_value, int multiplicity) {
  const Interval domain = Domain();
  if (knot_value <= domain.min || knot_value >= domain.max) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::InsertKnotAt: knot_value must be "
        "strictly inside the curve's own domain");
  }
  if (multiplicity < 1 || multiplicity > Degree()) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::InsertKnotAt: multiplicity must be "
        "between 1 and Degree() inclusive");
  }
  return curve_.InsertKnot(knot_value, multiplicity) ? Result::Ok : Result::Failed;
}

Result NurbsCurve::MakeRational() {
  if (curve_.IsRational()) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.MakeRational() ? Result::Ok : Result::Failed;
}

Result NurbsCurve::MakeNonRational() {
  if (!curve_.IsRational()) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.MakeNonRational() ? Result::Ok : Result::Failed;
}

Result NurbsCurve::ElevateDegree(int new_degree) {
  if (new_degree <= Degree()) {
    return Result::NoOpAlreadySatisfied;
  }
  // Deliberately NOT `ON_NurbsCurve::IncreaseDegree` - see
  // detail/degree_elevate.h for the measured, shape-corrupting inaccuracy
  // that routine has on non-uniform knot vectors.
  ON_NurbsCurve elevated;
  if (!detail::DegreeElevateNurbsCurve(curve_, new_degree, elevated)) {
    return Result::Failed;
  }
  curve_ = elevated;
  return Result::Ok;
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
  const bool closed = curve_.IsClosed() != 0;
  auto distance_squared = [&](double t) { return (PointAt(t) - point).LengthSquared(); };
  // Maps a parameter that may have wandered outside `domain` back into it
  // by wrapping around the seam, for a closed curve only - see the
  // wrap-vs-clamp discussion below.
  auto wrap = [&](double t) {
    if (!closed) return t;
    const double len = domain.Length();
    if (len <= 0.0) return t;
    double r = std::fmod(t - domain.Min(), len);
    if (r < 0.0) r += len;
    return domain.Min() + r;
  };
  auto distance_squared_wrapped = [&](double t) { return distance_squared(wrap(t)); };

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
  // For an open curve, the refinement window is clamped to the domain -
  // the true closest point can never lie outside it. For a closed curve,
  // clamping is wrong whenever the coarse sample above happens to land
  // near `domain.Min()` or `domain.Max()`: the true nearest point may
  // sit just past that boundary, on the far side of the same physical
  // seam point, and clamping would wall the golden-section search off
  // from ever reaching it. Letting the window extend past the domain and
  // evaluating through `wrap()` lets the search cross the seam like any
  // other point on the curve.
  double lo = closed ? (best_t - step) : std::max(domain.Min(), best_t - step);
  double hi = closed ? (best_t + step) : std::min(domain.Max(), best_t + step);

  // Golden-section search assumes the distance function is unimodal on
  // the window it polishes, and a window that straddles a C0 kink
  // violates that: a closed-but-not-periodic curve's own seam
  // (FromControlPoints() with matching first/last points is only C0
  // there), or any interior knot of full multiplicity, puts two different
  // polynomial pieces in one window, each with its own local minimum. A
  // real, reproduced case (TestCurveClosestPointAcrossClampedSeamKink):
  // with the true nearest point INSIDE the window on the far side of the
  // seam, a single golden section over the whole window walked to the
  // near side's own local minimum instead - 2.9x farther away - and a
  // plain sub-sampling pass did not cure it (both basins' minima sat
  // within one sub-step of the seam sample). The distance function can
  // only kink at a knot, so the window is split at every knot inside it
  // (wrapped copies too, for a closed curve, since the window may extend
  // past the seam) and each piece is polished on its own; a sub-sampled
  // scan of the window additionally localizes any smooth wiggle at the
  // sample scale, and the answer is the best of every polished piece and
  // every sample seen - so it is never worse than the coarse scan,
  // instead of "whichever side the golden section happened to pick".
  auto golden_section = [&](double a, double b) {
    const double golden_ratio = (std::sqrt(5.0) - 1.0) / 2.0;
    double c = b - golden_ratio * (b - a);
    double d = a + golden_ratio * (b - a);
    for (int iter = 0; iter < 100 && (b - a) > 1e-13; ++iter) {
      if (distance_squared_wrapped(c) < distance_squared_wrapped(d)) {
        b = d;
      } else {
        a = c;
      }
      c = b - golden_ratio * (b - a);
      d = a + golden_ratio * (b - a);
    }
    return (a + b) / 2.0;
  };
  auto consider = [&](double t) {
    const double d2 = distance_squared_wrapped(t);
    if (d2 < best_d2) {
      best_d2 = d2;
      best_t = t;
    }
  };

  constexpr int kSubSamples = 32;
  for (int i = 0; i <= kSubSamples; ++i) {
    consider(lo + (hi - lo) * static_cast<double>(i) / kSubSamples);
  }
  const double sub_step = (hi - lo) / kSubSamples;
  const double sub_lo = closed ? (best_t - sub_step) : std::max(domain.Min(), best_t - sub_step);
  const double sub_hi = closed ? (best_t + sub_step) : std::min(domain.Max(), best_t + sub_step);

  std::vector<double> breaks = {lo, hi, sub_lo, sub_hi};
  const double period = domain.Length();
  for (int i = 0; i < curve_.KnotCount(); ++i) {
    const double knot = curve_.Knot(i);
    for (int shift = -1; shift <= 1; ++shift) {
      if (shift != 0 && !closed) continue;
      const double shifted = knot + shift * period;
      if (shifted > lo && shifted < hi) breaks.push_back(shifted);
    }
  }
  std::sort(breaks.begin(), breaks.end());
  for (size_t i = 0; i + 1 < breaks.size(); ++i) {
    if (breaks[i + 1] - breaks[i] > 1e-13) consider(golden_section(breaks[i], breaks[i + 1]));
  }
  return wrap(best_t);
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
      // Clamped to [0, 1]: `cumulative_length` is the ROUNDED sum
      // `previous_length + segment_length`, so `target_length -
      // previous_length` can exceed `segment_length` by a rounding ulp of
      // the (possibly huge) cumulative length. Unclamped, that put the
      // result past `t` - for `target_length == Length(samples)` exactly,
      // a few 1e-12 PAST `Domain().max` (reproduced on a curve whose first
      // polyline segment dwarfs its last; see
      // TestCurveParameterAtArcLengthStaysInsideDomain), breaking this
      // method's own "clamps to Domain().Min()/Max()" promise.
      const double fraction = std::min(1.0, std::max(0.0, (target_length - previous_length) / segment_length));
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

Result NurbsCurve::Join(const NurbsCurve& other, double tolerance) {
  if (curve_.CVCount() < 2 || other.curve_.CVCount() < 2) {
    return Result::Failed;
  }
  if (curve_.IsClosed()) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::Join: this curve is closed - nothing can be "
        "appended to a closed loop");
  }
  // ON_NurbsCurve::Append discards `other`'s first control point without
  // ever checking it coincides with this curve's last one (see the
  // header comment), so the meeting condition is enforced here.
  const Point3d end = curve_.PointAtEnd();
  const double gap_to_start = end.DistanceTo(other.curve_.PointAtStart());
  const double gap_to_end = end.DistanceTo(other.curve_.PointAtEnd());
  ON_NurbsCurve tail = other.curve_;
  if (gap_to_start > tolerance) {
    if (gap_to_end > tolerance) {
      throw std::invalid_argument(
          "dino8::kernel::NurbsCurve::Join: neither end of `other` meets this "
          "curve's end within tolerance " +
          std::to_string(tolerance) + " (gap to other's start " + std::to_string(gap_to_start) +
          ", gap to other's end " + std::to_string(gap_to_end) + ")");
    }
    if (!tail.Reverse()) {
      return Result::Failed;
    }
  }
  return curve_.Append(tail) ? Result::Ok : Result::Failed;
}

Result NurbsCurve::MakePeriodicExact() {
  if (curve_.IsPeriodic()) {
    return Result::NoOpAlreadySatisfied;
  }
  if (!IsClosed()) {
    return Result::Failed;
  }
  const int p = curve_.Degree();
  if (p < 2) {
    return Result::Failed;
  }

  // Work on a scratch copy so a failure partway through leaves curve_
  // untouched.
  ON_NurbsCurve c = curve_;
  const int order = c.Order();

  // Bezier-decompose: bring every interior knot up to full multiplicity
  // p, via real (already-verified-exact) Boehm knot insertion. After
  // this, every former breakpoint - including the two ends, which a
  // clamped curve already stores at multiplicity p - has the same,
  // uniform structure.
  {
    const int span_count = c.SpanCount();
    std::vector<double> span_vector(static_cast<size_t>(span_count) + 1);
    if (!c.GetSpanVector(span_vector.data())) {
      return Result::Failed;
    }
    for (int i = 1; i < span_count; ++i) {
      if (!c.InsertKnot(span_vector[static_cast<size_t>(i)], p)) {
        return Result::Failed;
      }
    }
  }

  const int n = c.CVCount() - 1;  // last CV index; CV[0] == CV[n] (closed)
  const int L = n;                // distinct CVs once the duplicate is dropped
  const int old_knot_count = c.KnotCount();
  std::vector<double> old_knots(static_cast<size_t>(old_knot_count));
  for (int i = 0; i < old_knot_count; ++i) old_knots[static_cast<size_t>(i)] = c.Knot(i);
  const double domain_min = old_knots[static_cast<size_t>(order - 2)];
  const double domain_max = old_knots[static_cast<size_t>(c.CVCount() - 1)];
  const double period = domain_max - domain_min;
  if (!(period > 0.0)) {
    return Result::Failed;
  }

  const int new_cv_count = L + p;
  const int new_knot_count = new_cv_count + order - 2;  // = old_knot_count + (p - 1)
  std::vector<double> new_knots(static_cast<size_t>(new_knot_count));
  for (int i = 0; i < old_knot_count; ++i) new_knots[static_cast<size_t>(i)] = old_knots[static_cast<size_t>(i)];
  // Extend past the old domain end by continuing the curve's own knot
  // spacing one period later - the periodic wraparound's own knots, not
  // a newly invented pattern. These land strictly outside [domain_min,
  // domain_max], so they take no part in reproducing the curve there.
  for (int j = 0; j < p - 1; ++j) {
    new_knots[static_cast<size_t>(old_knot_count + j)] = old_knots[static_cast<size_t>(p + j)] + period;
  }

  // See MakePeriodicExact()'s own doc comment: the last `p` knots (the
  // ex-clamped end's own full-multiplicity run, now landing at the new
  // domain's own end) collapse to a single, genuinely zero-width span at
  // the new domain boundary, which OpenNURBS' own rational evaluator
  // does not handle there (confirmed: it returns Inf/garbage, not an
  // approximate value). Spread that one run apart by a relative 1e-13 of
  // the period - the domain's own end point (last entry) is untouched,
  // so the reported domain does not move.
  constexpr double kRelativeSeparation = 1e-13;
  const double separation = kRelativeSeparation * period;
  const int domain_max_index = new_cv_count - 1;
  for (int k = 0; k < p - 1; ++k) {
    const int idx = domain_max_index - 1 - k;
    if (idx < 0) break;
    new_knots[static_cast<size_t>(idx)] = domain_max - static_cast<double>(k + 1) * separation;
  }

  ON_NurbsCurve pc;
  if (!pc.Create(3, c.IsRational(), order, new_cv_count)) {
    return Result::Failed;
  }
  for (int i = 0; i < L; ++i) {
    ON_4dPoint cv;
    c.GetCV(i, cv);
    pc.SetCV(i, cv);
  }
  for (int i = 0; i < p; ++i) {
    ON_4dPoint cv;
    c.GetCV(i, cv);
    pc.SetCV(L + i, cv);
  }
  for (int i = 0; i < new_knot_count; ++i) pc.SetKnot(i, new_knots[static_cast<size_t>(i)]);

  curve_ = pc;
  return Result::Ok;
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

Result NurbsCurve::OffsetInPlane(double distance, NurbsCurve& out, double tolerance) const {
  if (!ON_IsValid(distance)) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::OffsetInPlane: distance must be finite");
  }

  const BoundingBox bbox = GetTightBoundingBox();
  const double diag = (bbox.max - bbox.min).Length();
  const double tol = tolerance > 0.0 ? tolerance : dino8::kernel::tolerance::DistanceForSize(diag);

  ON_Plane plane;
  if (!curve_.IsPlanar(&plane, tol)) {
    return Result::Failed;
  }

  if (distance == 0.0) {
    out.curve_ = curve_;
    return Result::Ok;
  }

  const Interval dom = Domain();
  const double t_mid = 0.5 * (dom.min + dom.max);

  // --- Line ------------------------------------------------------------
  if (curve_.IsLinear(tol)) {
    const Point3d p0 = curve_.PointAtStart();
    const Point3d p1 = curve_.PointAtEnd();
    Vector3d dir = p1 - p0;
    if (!dir.Unitize()) return Result::Failed;
    Vector3d offset_dir = ON_CrossProduct(dir, plane.zaxis);
    if (!offset_dir.Unitize()) return Result::Failed;
    out = NurbsCurve::FromControlPoints({p0 + distance * offset_dir, p1 + distance * offset_dir}, 1);
    return Result::Ok;
  }

  // --- Circular arc / full circle ---------------------------------------
  {
    ON_Arc arc;
    if (curve_.IsArc(nullptr, &arc, tol)) {
      const Vector3d tangent = TangentAt(t_mid);
      Vector3d offset_dir = ON_CrossProduct(tangent, arc.plane.zaxis);
      Vector3d radial = PointAt(t_mid) - arc.Center();
      if (!offset_dir.Unitize() || !radial.Unitize()) return Result::Failed;
      const double sign = OffsetSignAlong(offset_dir, radial);
      const double new_radius = arc.radius + sign * distance;
      if (!(new_radius > 0.0)) return Result::Failed;
      const ON_Circle new_circle(arc.plane, new_radius);
      const ON_Arc new_arc(new_circle, arc.DomainRadians());
      ON_NurbsCurve nc;
      if (new_arc.GetNurbForm(nc) == 0) return Result::Failed;
      out.curve_ = nc;
      return Result::Ok;
    }
  }

  // --- General planar curve: approximate, curvature-checked ---------------
  const double chord_tol = dino8::kernel::tolerance::RelativeDistance(diag);
  const int n = std::max(SuggestedSamples(chord_tol), 4 * ControlPointCount());
  std::vector<Point3d> offset_points;
  offset_points.reserve(static_cast<size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) {
    const double t = dom.min + (dom.max - dom.min) * i / n;
    Vector3d offset_dir = ON_CrossProduct(TangentAt(t), plane.zaxis);
    if (!offset_dir.Unitize()) return Result::Failed;  // degenerate (zero) tangent

    const Vector3d kappa_vec = CurvatureAt(t);
    const double kappa = kappa_vec.Length();
    if (kappa > dino8::kernel::tolerance::kZeroVector) {
      Vector3d to_center = kappa_vec;
      to_center.Unitize();
      const double inward_component = distance * OffsetSignAlong(offset_dir, to_center);
      if (inward_component >= 1.0 / kappa) return Result::Failed;  // folds through its own center of curvature
    }

    offset_points.push_back(PointAt(t) + distance * offset_dir);
  }

  NurbsCurve fitted;
  if (NurbsCurve::FitLeastSquares(offset_points, Degree(), ControlPointCount(), fitted) != Result::Ok) {
    return Result::Failed;
  }
  out = fitted;
  return Result::Ok;
}

}  // namespace dino8::kernel
