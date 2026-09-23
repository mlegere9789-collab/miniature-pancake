// NurbsSurface editing operations (knot removal, refit, matching, ...):
// the "surface editing" class of operations a Parasolid/ACIS-style kernel
// exposes. Kept in their own translation unit rather than surface.cpp so
// they can grow without churning that file's evaluation/tessellation
// code.

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "dino8/kernel/surface.h"

namespace dino8::kernel {

namespace {

// Control rows below are handled in *textbook* form: homogeneous 4D
// control points Pw[0..n] (w == 1 everywhere on a non-rational surface)
// with the full knot vector U[0..m], m = n + p + 1, i.e. including the
// two redundant end knots OpenNURBS' compressed storage drops (ON's
// Knot(k) == U[k + 1]).

ON_4dPoint Scale4(const ON_4dPoint& a, double s) { return ON_4dPoint(a.x * s, a.y * s, a.z * s, a.w * s); }
ON_4dPoint Add4(const ON_4dPoint& a, const ON_4dPoint& b) {
  return ON_4dPoint(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}
ON_4dPoint Sub4(const ON_4dPoint& a, const ON_4dPoint& b) {
  return ON_4dPoint(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}
double Norm4(const ON_4dPoint& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w); }

// Tiller's single knot removal (Piegl & Tiller, "The NURBS Book",
// Algorithm A5.8 with num = 1) on one textbook-form control row.
// `u_full` is the full knot vector U[0..m], `r` the textbook index of the
// *last* occurrence of the knot value u = U[r], `s` its multiplicity.
// Always produces the reduced row (n control points instead of n + 1)
// and the algorithm's own control-net discrepancy `distance`: zero
// (up to rounding) iff the knot is exactly removable, and otherwise a
// bound on the resulting curve error because the discrepancy is the
// coefficient of a single basis function with 0 <= N <= 1 (P&T's own
// reasoning in section 5.4). The knot vector shrink is the caller's job
// (it's shared across every row of a surface).
double RemoveKnotOnceRow(int p, const std::vector<double>& u_full, std::vector<ON_4dPoint>& pw, int r, int s) {
  const int n = static_cast<int>(pw.size()) - 1;
  const double u = u_full[static_cast<size_t>(r)];
  const int ord = p + 1;
  const int first = r - p;
  const int last = r - s;
  const int off = first - 1;
  std::vector<ON_4dPoint> temp(static_cast<size_t>(2 * p + 2));
  temp[0] = pw[static_cast<size_t>(off)];
  temp[static_cast<size_t>(last + 1 - off)] = pw[static_cast<size_t>(last + 1)];
  int i = first, j = last, ii = 1, jj = last - off;
  while (j - i > 0) {
    const double alfi = (u - u_full[static_cast<size_t>(i)]) / (u_full[static_cast<size_t>(i + ord)] - u_full[static_cast<size_t>(i)]);
    const double alfj = (u - u_full[static_cast<size_t>(j)]) / (u_full[static_cast<size_t>(j + ord)] - u_full[static_cast<size_t>(j)]);
    temp[static_cast<size_t>(ii)] = Scale4(Sub4(pw[static_cast<size_t>(i)], Scale4(temp[static_cast<size_t>(ii - 1)], 1.0 - alfi)), 1.0 / alfi);
    temp[static_cast<size_t>(jj)] = Scale4(Sub4(pw[static_cast<size_t>(j)], Scale4(temp[static_cast<size_t>(jj + 1)], alfj)), 1.0 / (1.0 - alfj));
    ++i; ++ii; --j; --jj;
  }
  double distance = 0.0;
  if (j - i < 0) {
    distance = Norm4(Sub4(temp[static_cast<size_t>(ii - 1)], temp[static_cast<size_t>(jj + 1)]));
  } else {
    const double alfi = (u - u_full[static_cast<size_t>(i)]) / (u_full[static_cast<size_t>(i + ord)] - u_full[static_cast<size_t>(i)]);
    const ON_4dPoint interp = Add4(Scale4(temp[static_cast<size_t>(ii + 1)], alfi), Scale4(temp[static_cast<size_t>(ii - 1)], 1.0 - alfi));
    distance = Norm4(Sub4(pw[static_cast<size_t>(i)], interp));
  }
  // Commit the recomputed points (P&T's "remflag" branch), then drop the
  // now-redundant control point at fout.
  i = first; j = last;
  while (j - i > 0) {
    pw[static_cast<size_t>(i)] = temp[static_cast<size_t>(i - off)];
    pw[static_cast<size_t>(j)] = temp[static_cast<size_t>(j - off)];
    ++i; --j;
  }
  const int fout = (2 * r - s - p) / 2;
  for (int k = fout + 1; k <= n; ++k) pw[static_cast<size_t>(k - 1)] = pw[static_cast<size_t>(k)];
  pw.pop_back();
  return distance;
}

// Value of the clamped B-spline basis function N_i(t) for the knot vector
// `knot` in ON's compressed convention, `cv_count` control points and
// `order` = degree + 1 - real Cox-de Boor evaluation via OpenNURBS' own
// ON_NurbsSpanIndex/ON_EvaluateNurbsBasis (the same pairing
// NurbsCurve::FitLeastSquares uses; ON_EvaluateNurbsBasis needs an
// order*order scratch buffer, not just `order` doubles).
double BasisValue(const std::vector<double>& knot, int cv_count, int order, int i, double t) {
  const int span = ON_NurbsSpanIndex(order, cv_count, knot.data(), t, 0, 0);
  if (i < span || i > span + order - 1) return 0.0;
  std::vector<double> B(static_cast<size_t>(order) * static_cast<size_t>(order));
  ON_EvaluateNurbsBasis(order, knot.data() + span, t, B.data());
  return B[static_cast<size_t>(i - span)];
}

// Solves the dense symmetric-positive-definite system A * X = B in place
// (A `size` x `size` row-major, B `size` x `rhs_count` row-major,
// overwritten with X) by Gaussian elimination with partial pivoting.
// Returns false if A is numerically singular.
bool SolveDense(std::vector<double>& a, std::vector<double>& b, int size, int rhs_count) {
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

// Clamped uniform knot vector over [t0, t1] in ON's compressed
// convention for `cv_count` control points of the given `order`.
std::vector<double> ClampedUniformKnots(int cv_count, int order, double t0, double t1) {
  const int degree = order - 1;
  const int knot_count = cv_count + order - 2;
  const int spans = cv_count - degree;
  std::vector<double> knot(static_cast<size_t>(knot_count));
  for (int k = 0; k < knot_count; ++k) {
    int span_index = k - degree + 1;
    if (span_index < 0) span_index = 0;
    if (span_index > spans) span_index = spans;
    knot[static_cast<size_t>(k)] = t0 + (t1 - t0) * span_index / spans;
  }
  return knot;
}

// Least-squares fits `data` (one point per parameter in `params`, both
// of size m + 1) with an `n + 1`-control-point B-spline of the given
// order and compressed knot vector, the two end control points pinned to
// data.front()/data.back() (Piegl & Tiller eq. 9.63-9.67), writing the
// n + 1 control points to `out`. Returns false if the normal equations
// are singular (fewer than n - 1 interior samples, or samples piled up
// so some basis function sees none).
bool FitRowLeastSquares(const std::vector<ON_3dPoint>& data, const std::vector<double>& params,
                        const std::vector<double>& knot, int order, int n_plus_1, std::vector<ON_3dPoint>& out) {
  const int m = static_cast<int>(data.size()) - 1;
  const int n = n_plus_1 - 1;
  out.assign(static_cast<size_t>(n_plus_1), ON_3dPoint::Origin);
  out.front() = data.front();
  out.back() = data.back();
  const int interior = n - 1;
  if (interior <= 0) return true;
  // R_k = P_k - N_0(t_k) P_0 - N_n(t_k) P_m for the interior samples.
  std::vector<double> ntn(static_cast<size_t>(interior) * static_cast<size_t>(interior), 0.0);
  std::vector<double> rhs(static_cast<size_t>(interior) * 3, 0.0);
  std::vector<double> basis(static_cast<size_t>(n_plus_1));
  for (int k = 1; k < m; ++k) {
    const double t = params[static_cast<size_t>(k)];
    for (int i = 0; i <= n; ++i) basis[static_cast<size_t>(i)] = BasisValue(knot, n_plus_1, order, i, t);
    const ON_3dPoint r = data[static_cast<size_t>(k)] - basis[0] * data.front() - basis[static_cast<size_t>(n)] * data.back();
    for (int i = 1; i < n; ++i) {
      const double ni = basis[static_cast<size_t>(i)];
      if (ni == 0.0) continue;
      for (int j = 1; j < n; ++j) ntn[static_cast<size_t>((i - 1) * interior + (j - 1))] += ni * basis[static_cast<size_t>(j)];
      rhs[static_cast<size_t>((i - 1) * 3 + 0)] += ni * r.x;
      rhs[static_cast<size_t>((i - 1) * 3 + 1)] += ni * r.y;
      rhs[static_cast<size_t>((i - 1) * 3 + 2)] += ni * r.z;
    }
  }
  if (!SolveDense(ntn, rhs, interior, 3)) return false;
  for (int i = 1; i < n; ++i) {
    out[static_cast<size_t>(i)] = ON_3dPoint(rhs[static_cast<size_t>((i - 1) * 3 + 0)], rhs[static_cast<size_t>((i - 1) * 3 + 1)],
                                             rhs[static_cast<size_t>((i - 1) * 3 + 2)]);
  }
  return true;
}

}  // namespace

Result NurbsSurface::RemoveKnotAt(int direction, int knot_index, double tolerance, double* out_max_deviation) {
  if (direction != 0 && direction != 1) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::RemoveKnotAt: direction must be 0 (U) or 1 (V)");
  }
  const int knot_count = surface_.KnotCount(direction);
  if (knot_index < 0 || knot_index >= knot_count) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::RemoveKnotAt: knot_index out of range");
  }
  const double u = surface_.Knot(direction, knot_index);
  const Interval domain = Domain(direction);
  if (!(u > domain.min && u < domain.max)) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::RemoveKnotAt: the knot must be strictly inside the domain in that direction");
  }
  if (out_max_deviation) *out_max_deviation = std::numeric_limits<double>::infinity();
  if (!surface_.IsClamped(direction, 2)) return Result::Failed;

  const int p = surface_.Degree(direction);
  const int cv_count = surface_.CVCount(direction);
  const int other_count = surface_.CVCount(1 - direction);
  const bool rational = surface_.IsRational();

  // Textbook knot vector U[0..m]: ON's compressed Knot(k) is U[k + 1];
  // the clamped ends supply the two dropped copies.
  std::vector<double> u_full(static_cast<size_t>(knot_count + 2));
  for (int k = 0; k < knot_count; ++k) u_full[static_cast<size_t>(k + 1)] = surface_.Knot(direction, k);
  u_full[0] = u_full[1];
  u_full[static_cast<size_t>(knot_count + 1)] = u_full[static_cast<size_t>(knot_count)];
  // Last textbook index of this knot value, and its multiplicity.
  int r = knot_index + 1;
  while (r + 1 <= knot_count && u_full[static_cast<size_t>(r + 1)] == u) ++r;
  int s = 0;
  for (int k = r; k >= 0 && u_full[static_cast<size_t>(k)] == u; --k) ++s;
  if (s > p) return Result::Failed;  // a knot of multiplicity > degree is a C^-1 break, not a B-spline knot removal case

  // Run the removal on every row/column independently, in homogeneous
  // coordinates (w == 1 on a non-rational surface, so the 4D distance
  // is the plain 3D one there).
  std::vector<std::vector<ON_4dPoint>> rows(static_cast<size_t>(other_count));
  double max_distance = 0.0;
  for (int k = 0; k < other_count; ++k) {
    std::vector<ON_4dPoint>& pw = rows[static_cast<size_t>(k)];
    pw.resize(static_cast<size_t>(cv_count));
    for (int i = 0; i < cv_count; ++i) {
      ON_4dPoint cv;
      if (direction == 0) surface_.GetCV(i, k, cv); else surface_.GetCV(k, i, cv);
      if (!rational) cv.w = 1.0;
      pw[static_cast<size_t>(i)] = cv;
    }
    max_distance = std::max(max_distance, RemoveKnotOnceRow(p, u_full, pw, r, s));
  }

  // Euclidean deviation bound. Non-rational: the discrepancy itself.
  // Rational: Piegl & Tiller eq. 5.30 relates a homogeneous-space
  // tolerance TOL to a Euclidean one d via TOL = d * w_min / (1 +
  // |P|_max), so d = distance * (1 + |P|_max) / w_min.
  double bound = max_distance;
  if (rational) {
    double w_min = std::numeric_limits<double>::infinity();
    double p_max = 0.0;
    for (int i = 0; i < surface_.CVCount(0); ++i) {
      for (int j = 0; j < surface_.CVCount(1); ++j) {
        const double w = surface_.Weight(i, j);
        ON_3dPoint e;
        surface_.GetCV(i, j, e);
        w_min = std::min(w_min, w);
        p_max = std::max(p_max, e.DistanceTo(ON_3dPoint::Origin));
      }
    }
    if (!(w_min > 0.0)) return Result::Failed;
    bound = max_distance * (1.0 + p_max) / w_min;
  }
  if (out_max_deviation) *out_max_deviation = bound;
  if (!(bound <= tolerance)) return Result::Failed;

  // Commit: rebuild the surface with one fewer control point and knot in
  // `direction`. Textbook knots U[r + 1..m] shift down by one; ON's
  // compressed form drops U'[0] and U'[m - 1].
  std::vector<double> new_full(u_full);
  new_full.erase(new_full.begin() + r);
  ON_NurbsSurface out;
  const int new_count = cv_count - 1;
  const bool ok = direction == 0 ? out.Create(3, rational, p + 1, surface_.Order(1), new_count, other_count)
                                 : out.Create(3, rational, surface_.Order(0), p + 1, other_count, new_count);
  if (!ok) return Result::Failed;
  for (int k = 0; k < out.KnotCount(direction); ++k) out.SetKnot(direction, k, new_full[static_cast<size_t>(k + 1)]);
  for (int k = 0; k < surface_.KnotCount(1 - direction); ++k) out.SetKnot(1 - direction, k, surface_.Knot(1 - direction, k));
  for (int k = 0; k < other_count; ++k) {
    for (int i = 0; i < new_count; ++i) {
      const ON_4dPoint& cv = rows[static_cast<size_t>(k)][static_cast<size_t>(i)];
      const int ii = direction == 0 ? i : k;
      const int jj = direction == 0 ? k : i;
      if (rational) out.SetCV(ii, jj, cv);
      else out.SetCV(ii, jj, ON_3dPoint(cv.x, cv.y, cv.z));
    }
  }
  if (!out.IsValid()) return Result::Failed;
  surface_ = out;
  return Result::Ok;
}

double NurbsSurface::MaxSampledDeviationFrom(const NurbsSurface& other, int u_samples, int v_samples) const {
  if (u_samples < 2 || v_samples < 2) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::MaxSampledDeviationFrom: sample counts must be >= 2");
  }
  const Interval du = Domain(0), dv = Domain(1);
  double worst = 0.0;
  for (int i = 0; i < u_samples; ++i) {
    const double u = du.min + (du.max - du.min) * i / (u_samples - 1.0);
    for (int j = 0; j < v_samples; ++j) {
      const double v = dv.min + (dv.max - dv.min) * j / (v_samples - 1.0);
      worst = std::max(worst, PointAt(u, v).DistanceTo(other.PointAt(u, v)));
    }
  }
  return worst;
}

Result NurbsSurface::SetDomain(int direction, double t0, double t1) {
  if (direction != 0 && direction != 1) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::SetDomain: direction must be 0 (U) or 1 (V)");
  }
  if (!(t0 < t1)) return Result::Failed;
  const Interval current = Domain(direction);
  if (current.min == t0 && current.max == t1) return Result::NoOpAlreadySatisfied;
  return surface_.SetDomain(direction, t0, t1) ? Result::Ok : Result::Failed;
}

Result NurbsSurface::Rebuild(int u_count, int v_count, int u_degree, int v_degree, NurbsSurface& out,
                             double* out_max_deviation, int u_samples, int v_samples) const {
  if (u_degree < 1 || v_degree < 1) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::Rebuild: degrees must be >= 1");
  }
  if (u_count <= u_degree || v_count <= v_degree) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::Rebuild: control counts must exceed their degree");
  }
  if (u_samples < u_count || v_samples < v_count) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::Rebuild: sample counts must be >= the control counts (underdetermined fit)");
  }
  const Interval du = Domain(0), dv = Domain(1);
  std::vector<double> u_params(static_cast<size_t>(u_samples)), v_params(static_cast<size_t>(v_samples));
  for (int i = 0; i < u_samples; ++i) u_params[static_cast<size_t>(i)] = du.min + (du.max - du.min) * i / (u_samples - 1.0);
  for (int j = 0; j < v_samples; ++j) v_params[static_cast<size_t>(j)] = dv.min + (dv.max - dv.min) * j / (v_samples - 1.0);
  const int u_order = u_degree + 1, v_order = v_degree + 1;
  const std::vector<double> u_knot = ClampedUniformKnots(u_count, u_order, du.min, du.max);
  const std::vector<double> v_knot = ClampedUniformKnots(v_count, v_order, dv.min, dv.max);

  // Stage 1: fit every sample row (fixed v_j) in U -> temp[j][i].
  std::vector<std::vector<ON_3dPoint>> temp(static_cast<size_t>(v_samples));
  std::vector<ON_3dPoint> row(static_cast<size_t>(u_samples));
  for (int j = 0; j < v_samples; ++j) {
    for (int i = 0; i < u_samples; ++i) row[static_cast<size_t>(i)] = PointAt(u_params[static_cast<size_t>(i)], v_params[static_cast<size_t>(j)]);
    if (!FitRowLeastSquares(row, u_params, u_knot, u_order, u_count, temp[static_cast<size_t>(j)])) return Result::Failed;
  }
  // Stage 2: fit every column of intermediate points (fixed i) in V.
  ON_NurbsSurface result;
  if (!result.Create(3, false, u_order, v_order, u_count, v_count)) return Result::Failed;
  for (int k = 0; k < result.KnotCount(0); ++k) result.SetKnot(0, k, u_knot[static_cast<size_t>(k)]);
  for (int k = 0; k < result.KnotCount(1); ++k) result.SetKnot(1, k, v_knot[static_cast<size_t>(k)]);
  std::vector<ON_3dPoint> column(static_cast<size_t>(v_samples)), fitted;
  for (int i = 0; i < u_count; ++i) {
    for (int j = 0; j < v_samples; ++j) column[static_cast<size_t>(j)] = temp[static_cast<size_t>(j)][static_cast<size_t>(i)];
    if (!FitRowLeastSquares(column, v_params, v_knot, v_order, v_count, fitted)) return Result::Failed;
    for (int j = 0; j < v_count; ++j) result.SetCV(i, j, fitted[static_cast<size_t>(j)]);
  }
  if (!result.IsValid()) return Result::Failed;
  out.surface_ = result;
  if (out_max_deviation) {
    // Twice the fit density, offset by half a fit step, so no measurement
    // point coincides with a sample the fit already saw.
    const int mu = 2 * u_samples, mv = 2 * v_samples;
    double worst = 0.0;
    for (int i = 0; i < mu; ++i) {
      const double u = du.min + (du.max - du.min) * (i + 0.5) / mu;
      for (int j = 0; j < mv; ++j) {
        const double v = dv.min + (dv.max - dv.min) * (j + 0.5) / mv;
        worst = std::max(worst, PointAt(u, v).DistanceTo(out.PointAt(u, v)));
      }
    }
    // Plus the boundary itself (the offsets above stay strictly interior).
    for (int i = 0; i <= mu; ++i) {
      const double u = du.min + (du.max - du.min) * i / mu;
      worst = std::max({worst, PointAt(u, dv.min).DistanceTo(out.PointAt(u, dv.min)), PointAt(u, dv.max).DistanceTo(out.PointAt(u, dv.max))});
    }
    for (int j = 0; j <= mv; ++j) {
      const double v = dv.min + (dv.max - dv.min) * j / mv;
      worst = std::max({worst, PointAt(du.min, v).DistanceTo(out.PointAt(du.min, v)), PointAt(du.max, v).DistanceTo(out.PointAt(du.max, v))});
    }
    *out_max_deviation = worst;
  }
  return Result::Ok;
}

}  // namespace dino8::kernel
