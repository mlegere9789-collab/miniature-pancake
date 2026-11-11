#include "dino8/kernel/curve_interpolate.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dino8::kernel {

namespace {

// Non-zero B-spline basis functions N_{span-p..span, p}(t) (Piegl & Tiller A2.2) for a clamped knot vector `knot` with
// `cv_count` control points.
int FindSpan(const std::vector<double>& knot, int cv_count, int p, double t) {
  const int n = cv_count - 1;
  if (t >= knot[static_cast<size_t>(n + 1)]) return n;
  if (t <= knot[static_cast<size_t>(p)]) return p;
  int lo = p;
  int hi = n + 1;
  int mid = (lo + hi) / 2;
  while (t < knot[static_cast<size_t>(mid)] || t >= knot[static_cast<size_t>(mid + 1)]) {
    if (t < knot[static_cast<size_t>(mid)]) hi = mid; else lo = mid;
    mid = (lo + hi) / 2;
  }
  return mid;
}

void BasisFunctions(const std::vector<double>& knot, int span, int p, double t, std::vector<double>& N) {
  N.assign(static_cast<size_t>(p + 1), 0.0);
  std::vector<double> left(static_cast<size_t>(p + 1)), right(static_cast<size_t>(p + 1));
  N[0] = 1.0;
  for (int j = 1; j <= p; ++j) {
    left[static_cast<size_t>(j)] = t - knot[static_cast<size_t>(span + 1 - j)];
    right[static_cast<size_t>(j)] = knot[static_cast<size_t>(span + j)] - t;
    double saved = 0.0;
    for (int r = 0; r < j; ++r) {
      const double denom = right[static_cast<size_t>(r + 1)] + left[static_cast<size_t>(j - r)];
      const double temp = denom == 0.0 ? 0.0 : N[static_cast<size_t>(r)] / denom;
      N[static_cast<size_t>(r)] = saved + right[static_cast<size_t>(r + 1)] * temp;
      saved = left[static_cast<size_t>(j - r)] * temp;
    }
    N[static_cast<size_t>(j)] = saved;
  }
}

// Solves A X = B (A is size x size, row-major; B is size x 3) in place by Gaussian elimination with partial pivoting.
// `band` bounds how far from the diagonal a non-zero can sit after pivoting, so the work is O(size * band^2).
bool SolveBanded(std::vector<double>& a, std::vector<double>& b, int size, int band) {
  auto A = [&](int i, int j) -> double& { return a[static_cast<size_t>(i) * static_cast<size_t>(size) + static_cast<size_t>(j)]; };
  for (int k = 0; k < size; ++k) {
    const int row_hi = std::min(size - 1, k + band);
    int piv = k;
    for (int i = k + 1; i <= row_hi; ++i) {
      if (std::fabs(A(i, k)) > std::fabs(A(piv, k))) piv = i;
    }
    if (std::fabs(A(piv, k)) < 1e-300) return false;
    const int col_hi = std::min(size - 1, k + 2 * band);
    if (piv != k) {
      for (int j = k; j <= col_hi; ++j) std::swap(A(k, j), A(piv, j));
      for (int c = 0; c < 3; ++c) std::swap(b[static_cast<size_t>(k) * 3 + c], b[static_cast<size_t>(piv) * 3 + c]);
    }
    for (int i = k + 1; i <= row_hi; ++i) {
      const double f = A(i, k) / A(k, k);
      if (f == 0.0) continue;
      for (int j = k; j <= col_hi; ++j) A(i, j) -= f * A(k, j);
      for (int c = 0; c < 3; ++c) b[static_cast<size_t>(i) * 3 + c] -= f * b[static_cast<size_t>(k) * 3 + c];
    }
  }
  for (int k = size - 1; k >= 0; --k) {
    const int col_hi = std::min(size - 1, k + 2 * band);
    for (int c = 0; c < 3; ++c) {
      double s = b[static_cast<size_t>(k) * 3 + c];
      for (int j = k + 1; j <= col_hi; ++j) s -= A(k, j) * b[static_cast<size_t>(j) * 3 + c];
      b[static_cast<size_t>(k) * 3 + c] = s / A(k, k);
    }
  }
  return true;
}

bool Finite(const Point3d& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

}  // namespace

Result InterpolateCurve(const std::vector<Point3d>& points, const CurveInterpolationOptions& options, NurbsCurve& out,
                        CurveInterpolationReport* report) {
  constexpr int kMaxPoints = 4000;
  const int count = static_cast<int>(points.size());
  const int p = options.degree;
  const bool tan0 = options.start_tangent.has_value();
  const bool tan1 = options.end_tangent.has_value();
  const int extra = (tan0 ? 1 : 0) + (tan1 ? 1 : 0);
  if (count < 2 || count > kMaxPoints || p < 1 || p > count - 1 + extra) return Result::Failed;
  if ((tan0 || tan1) && p < 2) return Result::Failed;
  for (const Point3d& q : points) {
    if (!Finite(q)) return Result::Failed;
  }
  const int n = count - 1;

  // Data parameters.
  std::vector<double> t(static_cast<size_t>(count));
  double total_chord = 0.0;
  for (int k = 1; k < count; ++k) total_chord += (points[static_cast<size_t>(k)] - points[static_cast<size_t>(k - 1)]).Length();
  if (!options.explicit_parameters.empty()) {
    if (static_cast<int>(options.explicit_parameters.size()) != count) return Result::Failed;
    t = options.explicit_parameters;
    for (int k = 0; k < count; ++k) {
      if (!std::isfinite(t[static_cast<size_t>(k)])) return Result::Failed;
      if (k > 0 && !(t[static_cast<size_t>(k)] > t[static_cast<size_t>(k - 1)])) return Result::Failed;
    }
  } else {
    t[0] = 0.0;
    if (options.parameter == InterpolationParameter::Uniform) {
      for (int k = 0; k < count; ++k) t[static_cast<size_t>(k)] = static_cast<double>(k) / n;
    } else {
      const bool sq = options.parameter == InterpolationParameter::Centripetal;
      std::vector<double> cum(static_cast<size_t>(count), 0.0);
      for (int k = 1; k < count; ++k) {
        const double d = (points[static_cast<size_t>(k)] - points[static_cast<size_t>(k - 1)]).Length();
        if (!(d > 0.0)) return Result::Failed;
        cum[static_cast<size_t>(k)] = cum[static_cast<size_t>(k - 1)] + (sq ? std::sqrt(d) : d);
      }
      for (int k = 0; k < count; ++k) t[static_cast<size_t>(k)] = cum[static_cast<size_t>(k)] / cum.back();
      t.back() = 1.0;
    }
  }
  const double a = t.front();
  const double b = t.back();

  // Averaged knot vector (P&T 9.2.1; 9.2.2 for the both-ends tangent case, the one-sided cases shift the window so every
  // window stays inside the data).
  const int cv_count = count + extra;
  const int interior = cv_count - p - 1;
  const int first_window = tan0 ? 0 : 1;
  std::vector<double> knot(static_cast<size_t>(cv_count + p + 1));
  for (int i = 0; i <= p; ++i) {
    knot[static_cast<size_t>(i)] = a;
    knot[static_cast<size_t>(cv_count + i)] = b;
  }
  for (int i = 0; i < interior; ++i) {
    const int s = first_window + i;
    if (s < 0 || s + p - 1 > n) return Result::Failed;
    double sum = 0.0;
    for (int j = s; j < s + p; ++j) sum += t[static_cast<size_t>(j)];
    knot[static_cast<size_t>(p + 1 + i)] = sum / p;
  }

  // End derivative requested at each end, in the curve's own parameter.
  const double domain = b - a;
  auto end_derivative = [&](const Vector3d& v) -> std::optional<Vector3d> {
    if (!Finite(Point3d(v.x, v.y, v.z))) return std::nullopt;
    if (options.tangents_are_derivatives) return v;
    const double len = v.Length();
    if (!(len > 0.0)) return std::nullopt;
    return v * (total_chord / domain / len);
  };
  Vector3d d0(0, 0, 0), d1(0, 0, 0);
  if (tan0) {
    const auto d = end_derivative(*options.start_tangent);
    if (!d) return Result::Failed;
    d0 = *d;
  }
  if (tan1) {
    const auto d = end_derivative(*options.end_tangent);
    if (!d) return Result::Failed;
    d1 = *d;
  }

  // Collocation system.
  const int size = cv_count;
  std::vector<double> A(static_cast<size_t>(size) * static_cast<size_t>(size), 0.0);
  std::vector<double> B(static_cast<size_t>(size) * 3, 0.0);
  auto put_rhs = [&](int row, const Point3d& v) {
    B[static_cast<size_t>(row) * 3 + 0] = v.x;
    B[static_cast<size_t>(row) * 3 + 1] = v.y;
    B[static_cast<size_t>(row) * 3 + 2] = v.z;
  };
  int row = 0;
  std::vector<double> N;
  for (int k = 0; k <= n; ++k) {
    if (k == n && tan1) {
      // P_last - P_{last-1} = (b - knot[cv_count - 1]... clamped end: C'(b) = p/(b - u_{m-p-1}) (P_last - P_{last-1}).
      const double span = b - knot[static_cast<size_t>(cv_count - 1)];
      A[static_cast<size_t>(row) * size + (cv_count - 1)] = 1.0;
      A[static_cast<size_t>(row) * size + (cv_count - 2)] = -1.0;
      put_rhs(row, Point3d(d1.x * span / p, d1.y * span / p, d1.z * span / p));
      ++row;
    }
    const int span = FindSpan(knot, cv_count, p, t[static_cast<size_t>(k)]);
    BasisFunctions(knot, span, p, t[static_cast<size_t>(k)], N);
    for (int j = 0; j <= p; ++j) A[static_cast<size_t>(row) * size + (span - p + j)] = N[static_cast<size_t>(j)];
    put_rhs(row, points[static_cast<size_t>(k)]);
    ++row;
    if (k == 0 && tan0) {
      // C'(a) = p/(u_{p+1} - a) (P_1 - P_0).
      const double sp = knot[static_cast<size_t>(p + 1)] - a;
      A[static_cast<size_t>(row) * size + 1] = 1.0;
      A[static_cast<size_t>(row) * size + 0] = -1.0;
      put_rhs(row, Point3d(d0.x * sp / p, d0.y * sp / p, d0.z * sp / p));
      ++row;
    }
  }
  if (row != size) return Result::Failed;
  if (!SolveBanded(A, B, size, p + 2)) return Result::Failed;

  NurbsCurve result = NurbsCurve::FromControlPoints(std::vector<Point3d>(static_cast<size_t>(cv_count), Point3d(0, 0, 0)), p);
  ON_NurbsCurve& raw = result.raw();
  for (int i = 0; i < cv_count; ++i) {
    raw.SetCV(i, Point3d(B[static_cast<size_t>(i) * 3], B[static_cast<size_t>(i) * 3 + 1], B[static_cast<size_t>(i) * 3 + 2]));
  }
  for (int i = 0; i < cv_count + p - 1; ++i) raw.SetKnot(i, knot[static_cast<size_t>(i + 1)]);
  if (!raw.IsValid()) return Result::Failed;

  if (report) {
    report->parameters = t;
    double worst = 0.0;
    for (int k = 0; k <= n; ++k) {
      worst = std::max(worst, (raw.PointAt(t[static_cast<size_t>(k)]) - points[static_cast<size_t>(k)]).Length());
    }
    report->max_position_residual = worst;
    report->start_tangent_residual = 0.0;
    report->end_tangent_residual = 0.0;
    Point3d pt;
    Vector3d d;
    if (tan0 && raw.Ev1Der(a, pt, d)) report->start_tangent_residual = (d - d0).Length();
    if (tan1 && raw.Ev1Der(b, pt, d, -1)) report->end_tangent_residual = (d - d1).Length();
  }
  out = result;
  return Result::Ok;
}

}  // namespace dino8::kernel
