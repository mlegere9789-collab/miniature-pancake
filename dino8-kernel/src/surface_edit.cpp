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

#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
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

namespace {

// Degree-elevates `a`/`b` to their shared max degree, then inserts each
// one's interior knots into the other (skipping a value already present
// within `tol`) so both end up with identical degree and knot vector -
// the curve-level twin of MatchEdge()'s own edge-unification step.
// Returns false if either OpenNURBS call fails.
bool UnifyCurves(ON_NurbsCurve& a, ON_NurbsCurve& b, double tol) {
  const int degree = std::max(a.Degree(), b.Degree());
  if (a.Degree() < degree && !a.IncreaseDegree(degree)) return false;
  if (b.Degree() < degree && !b.IncreaseDegree(degree)) return false;
  auto merge = [&](ON_NurbsCurve& into, const ON_NurbsCurve& from) {
    const ON_Interval dom = into.Domain();
    int k = 0;
    while (k < from.KnotCount()) {
      const double value = from.Knot(k);
      int mult = 1;
      while (k + mult < from.KnotCount() && from.Knot(k + mult) == value) ++mult;
      if (value > dom.Min() && value < dom.Max()) {
        double target = value;
        for (int i = 0; i < into.KnotCount(); ++i) {
          if (std::abs(into.Knot(i) - value) <= tol) { target = into.Knot(i); break; }
        }
        if (!into.InsertKnot(target, mult)) return false;
      }
      k += mult;
    }
    return true;
  };
  if (!merge(a, b) || !merge(b, a)) return false;
  return a.CVCount() == b.CVCount() && a.KnotCount() == b.KnotCount();
}

// Brings NurbsSurface `s`'s `direction` to `target_degree`/`target_knots`
// via the already-tested ElevateDegree()/InsertKnotAt() wrapper methods -
// reused verbatim, no new low-level NURBS algebra here.
bool BringDirectionTo(NurbsSurface& s, int direction, int target_degree, const std::vector<double>& target_knots,
                      double tol) {
  if (s.raw().Degree(direction) < target_degree && s.ElevateDegree(direction, target_degree) == Result::Failed) return false;
  int k = 0;
  while (k < static_cast<int>(target_knots.size())) {
    const double value = target_knots[static_cast<size_t>(k)];
    int mult = 1;
    while (k + mult < static_cast<int>(target_knots.size()) && target_knots[static_cast<size_t>(k + mult)] == value) ++mult;
    const Interval dom = s.Domain(direction);
    if (value > dom.min && value < dom.max) {
      bool present = false;
      for (int i = 0; i < s.KnotCount(direction); ++i) {
        if (std::abs(s.KnotAt(direction, i) - value) <= tol) { present = true; break; }
      }
      if (!present) {
        const Result r = s.InsertKnotAt(direction, value, mult);
        if (r == Result::Failed) return false;
      }
    }
    k += mult;
  }
  return s.KnotCount(direction) == static_cast<int>(target_knots.size());
}

// The `NurbsSurface` built by ruling directly between two curves that
// already share a degree and knot vector: CV(i, 0) = a's CVi, CV(i, 1) =
// b's CVi (homogeneous, so a rational input carries its weights), a
// plain 2-knot clamped-linear structure in the ruled direction.
NurbsSurface RuleBetween(const ON_NurbsCurve& a, const ON_NurbsCurve& b, int ruled_direction) {
  const bool rational = a.IsRational() || b.IsRational();
  const int n = a.CVCount();
  NurbsSurface out;
  ON_NurbsSurface& s = out.raw();
  if (ruled_direction == 1) {
    s.Create(3, rational, a.Order(), 2, n, 2);
    for (int k = 0; k < s.KnotCount(0); ++k) s.SetKnot(0, k, a.Knot(k));
    s.SetKnot(1, 0, 0.0);
    s.SetKnot(1, 1, 1.0);
  } else {
    s.Create(3, rational, 2, a.Order(), 2, n);
    s.SetKnot(0, 0, 0.0);
    s.SetKnot(0, 1, 1.0);
    for (int k = 0; k < s.KnotCount(1); ++k) s.SetKnot(1, k, a.Knot(k));
  }
  for (int i = 0; i < n; ++i) {
    ON_4dPoint pa, pb;
    a.GetCV(i, pa);
    b.GetCV(i, pb);
    if (!a.IsRational()) pa.w = 1.0;
    if (!b.IsRational()) pb.w = 1.0;
    if (ruled_direction == 1) {
      if (rational) { s.SetCV(i, 0, pa); s.SetCV(i, 1, pb); } else { s.SetCV(i, 0, ON_3dPoint(pa.x, pa.y, pa.z)); s.SetCV(i, 1, ON_3dPoint(pb.x, pb.y, pb.z)); }
    } else {
      if (rational) { s.SetCV(0, i, pa); s.SetCV(1, i, pb); } else { s.SetCV(0, i, ON_3dPoint(pa.x, pa.y, pa.z)); s.SetCV(1, i, ON_3dPoint(pb.x, pb.y, pb.z)); }
    }
  }
  return out;
}

}  // namespace

namespace {

// Homogeneous control-point row of `s` at index `k` in `fixed_direction`
// (0 = U: row k = CV(k, *); 1 = V: row k = CV(*, k)), w forced to 1 on
// a non-rational surface.
std::vector<ON_4dPoint> HomogeneousRow(const ON_NurbsSurface& s, int fixed_direction, int k) {
  const int n = s.CVCount(1 - fixed_direction);
  std::vector<ON_4dPoint> row(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    ON_4dPoint cv;
    if (fixed_direction == 0) s.GetCV(k, i, cv); else s.GetCV(i, k, cv);
    if (!s.IsRational()) cv.w = 1.0;
    row[static_cast<size_t>(i)] = cv;
  }
  return row;
}

void SetHomogeneousRow(ON_NurbsSurface& s, int fixed_direction, int k, const std::vector<ON_4dPoint>& row) {
  for (int i = 0; i < static_cast<int>(row.size()); ++i) {
    const ON_4dPoint& cv = row[static_cast<size_t>(i)];
    const int ii = fixed_direction == 0 ? k : i;
    const int jj = fixed_direction == 0 ? i : k;
    if (s.IsRational()) s.SetCV(ii, jj, cv);
    else s.SetCV(ii, jj, ON_3dPoint(cv.x, cv.y, cv.z));
  }
}

// Textbook knot V_k (k >= 1) of `s` in `direction`: ON's compressed
// Knot(k - 1).
double TextbookKnot(const ON_NurbsSurface& s, int direction, int k) { return s.Knot(direction, k - 1); }

// Mean magnitude of the cross-boundary first derivative of `s` along
// its edge at `fixed_direction` == domain min (after any Reverse()).
double MeanCrossSpeedAtMin(const ON_NurbsSurface& s, int fixed_direction, int samples) {
  const ON_Interval edge = s.Domain(1 - fixed_direction);
  const double at = s.Domain(fixed_direction).Min();
  double sum = 0.0;
  for (int k = 0; k < samples; ++k) {
    const double e = edge.ParameterAt((k + 0.5) / samples);
    ON_3dPoint pt;
    ON_3dVector du, dv;
    if (fixed_direction == 0) s.Ev1Der(at, e, pt, du, dv); else s.Ev1Der(e, at, pt, du, dv);
    sum += (fixed_direction == 0 ? du : dv).Length();
  }
  return sum / samples;
}

}  // namespace

Result NurbsSurface::MatchEdge(int fixed_direction, bool at_min, const NurbsSurface& target, int target_fixed_direction,
                               bool target_at_min, MatchContinuity continuity, MatchEdgeReport* report,
                               double cross_scale) {
  if ((fixed_direction != 0 && fixed_direction != 1) || (target_fixed_direction != 0 && target_fixed_direction != 1)) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::MatchEdge: directions must be 0 (U) or 1 (V)");
  }
  const int rows_needed = continuity == MatchContinuity::Position ? 1 : continuity == MatchContinuity::Tangent ? 2 : 3;
  const int edge_dir = 1 - fixed_direction;
  const int t_edge_dir = 1 - target_fixed_direction;
  if (!surface_.IsClamped(0, 2) || !surface_.IsClamped(1, 2)) return Result::Failed;
  if (!target.surface_.IsClamped(t_edge_dir, 2) || !target.surface_.IsClamped(target_fixed_direction, 2)) return Result::Failed;
  if (target.surface_.Degree(target_fixed_direction) < rows_needed - 1) return Result::Failed;

  const ON_NurbsSurface backup = surface_;
  ON_NurbsSurface& s = surface_;
  ON_NurbsSurface t = target.surface_;

  // Normalize both to "edge at the cross direction's min" so one set of
  // end-derivative formulas applies; Reverse() is shape-preserving.
  if (!at_min && !s.Reverse(fixed_direction)) { surface_ = backup; return Result::Failed; }
  if (!target_at_min && !t.Reverse(target_fixed_direction)) { surface_ = backup; return Result::Failed; }

  // Orient the target edge the same way as this edge (corner distances).
  const ON_Interval s_edge = s.Domain(edge_dir), t_edge = t.Domain(t_edge_dir);
  auto s_corner = [&](double e) { return fixed_direction == 0 ? s.PointAt(s.Domain(0).Min(), e) : s.PointAt(e, s.Domain(1).Min()); };
  auto t_corner = [&](double e) { return target_fixed_direction == 0 ? t.PointAt(t.Domain(0).Min(), e) : t.PointAt(e, t.Domain(1).Min()); };
  const double straight = s_corner(s_edge.Min()).DistanceTo(t_corner(t_edge.Min())) + s_corner(s_edge.Max()).DistanceTo(t_corner(t_edge.Max()));
  const double crossed = s_corner(s_edge.Min()).DistanceTo(t_corner(t_edge.Max())) + s_corner(s_edge.Max()).DistanceTo(t_corner(t_edge.Min()));
  const bool reversed = crossed < straight;
  if (reversed && !t.Reverse(t_edge_dir)) { surface_ = backup; return Result::Failed; }
  if (!t.SetDomain(t_edge_dir, s_edge.Min(), s_edge.Max())) { surface_ = backup; return Result::Failed; }

  // Scale: this surface's mean cross speed over the target's, measured
  // before any edit.
  double scale = cross_scale;
  if (!(scale > 0.0)) {
    const double s_speed = MeanCrossSpeedAtMin(s, fixed_direction, 32);
    const double t_speed = MeanCrossSpeedAtMin(t, target_fixed_direction, 32);
    scale = (s_speed > 0.0 && t_speed > 0.0) ? s_speed / t_speed : 1.0;
  }

  // Compatible edge bases: rationality, degree, knots.
  if (t.IsRational() && !s.IsRational()) s.MakeRational();
  if (s.IsRational() && !t.IsRational()) t.MakeRational();
  const int edge_degree = std::max(s.Degree(edge_dir), t.Degree(t_edge_dir));
  if (s.Degree(edge_dir) < edge_degree && !s.IncreaseDegree(edge_dir, edge_degree)) { surface_ = backup; return Result::Failed; }
  if (t.Degree(t_edge_dir) < edge_degree && !t.IncreaseDegree(t_edge_dir, edge_degree)) { surface_ = backup; return Result::Failed; }
  auto merge_knots = [&](ON_NurbsSurface& into, int into_dir, const ON_NurbsSurface& from, int from_dir) {
    const ON_Interval dom = into.Domain(into_dir);
    int k = 0;
    while (k < from.KnotCount(from_dir)) {
      const double value = from.Knot(from_dir, k);
      int mult = 1;
      while (k + mult < from.KnotCount(from_dir) && from.Knot(from_dir, k + mult) == value) ++mult;
      if (value > dom.Min() && value < dom.Max()) {
        // Snap to an existing knot within a tiny relative tolerance so a
        // rounding-different copy of the same knot doesn't get inserted
        // as a second, nearly-coincident knot.
        double target_value = value;
        for (int i = 0; i < into.KnotCount(into_dir); ++i) {
          if (std::abs(into.Knot(into_dir, i) - value) <= 1e-12 * dom.Length()) { target_value = into.Knot(into_dir, i); break; }
        }
        if (!into.InsertKnot(into_dir, target_value, mult)) return false;
      }
      k += mult;
    }
    return true;
  };
  if (!merge_knots(s, edge_dir, t, t_edge_dir) || !merge_knots(t, t_edge_dir, s, edge_dir)) { surface_ = backup; return Result::Failed; }
  if (s.CVCount(edge_dir) != t.CVCount(t_edge_dir) || s.KnotCount(edge_dir) != t.KnotCount(t_edge_dir)) { surface_ = backup; return Result::Failed; }
  for (int k = 0; k < s.KnotCount(edge_dir); ++k) {
    if (std::abs(s.Knot(edge_dir, k) - t.Knot(t_edge_dir, k)) > 1e-12 * s_edge.Length()) { surface_ = backup; return Result::Failed; }
  }

  // Enough rows in this surface's cross direction to leave the far edge alone.
  if (s.Degree(fixed_direction) < rows_needed && !s.IncreaseDegree(fixed_direction, rows_needed)) { surface_ = backup; return Result::Failed; }
  if (s.CVCount(fixed_direction) <= rows_needed && !s.InsertKnot(fixed_direction, s.Domain(fixed_direction).Mid(), 1)) { surface_ = backup; return Result::Failed; }
  if (s.CVCount(fixed_direction) <= rows_needed) { surface_ = backup; return Result::Failed; }

  // Rows and end-derivative coefficients (textbook knots; both surfaces
  // are clamped so V_1 is the domain start).
  const int p = s.Degree(fixed_direction), q = t.Degree(target_fixed_direction);
  const std::vector<ON_4dPoint> t0 = HomogeneousRow(t, target_fixed_direction, 0);
  std::vector<ON_4dPoint> r0 = t0, r1, r2;
  const int n = static_cast<int>(t0.size());
  if (rows_needed >= 2) {
    const std::vector<ON_4dPoint> t1 = HomogeneousRow(t, target_fixed_direction, 1);
    const double dt1 = TextbookKnot(t, target_fixed_direction, q + 1) - TextbookKnot(t, target_fixed_direction, 1);
    const double ds1 = TextbookKnot(s, fixed_direction, p + 1) - TextbookKnot(s, fixed_direction, 1);
    // A = q / dt1 * (T1 - T0); R1 = R0 + ds1 / p * (-scale * A).
    r1.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
      const ON_4dPoint a = Scale4(Sub4(t1[static_cast<size_t>(i)], t0[static_cast<size_t>(i)]), q / dt1);
      r1[static_cast<size_t>(i)] = Add4(r0[static_cast<size_t>(i)], Scale4(a, -scale * ds1 / p));
    }
    if (rows_needed >= 3) {
      const std::vector<ON_4dPoint> t2 = HomogeneousRow(t, target_fixed_direction, 2);
      const double dt2 = TextbookKnot(t, target_fixed_direction, q + 2) - TextbookKnot(t, target_fixed_direction, 2);
      const double ds2 = TextbookKnot(s, fixed_direction, p + 2) - TextbookKnot(s, fixed_direction, 2);
      // B = q (q-1) / dt1 * [ (T2 - T1) / dt2 - (T1 - T0) / dt1 ];
      // S_vv = p (p-1) / ds1 * [ (R2 - R1) / ds2 - (R1 - R0) / ds1 ] = scale^2 B.
      r2.resize(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i) {
        const ON_4dPoint b = Scale4(Sub4(Scale4(Sub4(t2[static_cast<size_t>(i)], t1[static_cast<size_t>(i)]), 1.0 / dt2),
                                         Scale4(Sub4(t1[static_cast<size_t>(i)], t0[static_cast<size_t>(i)]), 1.0 / dt1)),
                                    q * (q - 1.0) / dt1);
        const ON_4dPoint d1 = Scale4(Sub4(r1[static_cast<size_t>(i)], r0[static_cast<size_t>(i)]), 1.0 / ds1);
        const ON_4dPoint inner = Add4(Scale4(b, scale * scale * ds1 / (p * (p - 1.0))), d1);
        r2[static_cast<size_t>(i)] = Add4(r1[static_cast<size_t>(i)], Scale4(inner, ds2));
      }
    }
  }
  if (s.IsRational()) {
    for (const auto* row : {&r0, &r1, &r2}) {
      for (const ON_4dPoint& cv : *row) {
        if (!(cv.w > 0.0)) { surface_ = backup; return Result::Failed; }
      }
    }
  }
  SetHomogeneousRow(s, fixed_direction, 0, r0);
  if (rows_needed >= 2) SetHomogeneousRow(s, fixed_direction, 1, r1);
  if (rows_needed >= 3) SetHomogeneousRow(s, fixed_direction, 2, r2);

  // Self-check by evaluation along the edge, then undo the normalizing
  // reversal.
  MatchEdgeReport local;
  local.scale = scale;
  local.target_edge_reversed = reversed;
  double size = 0.0;
  {
    ON_BoundingBox bb;
    s.GetBoundingBox(bb, false);
    ON_BoundingBox tb;
    t.GetBoundingBox(tb, false);
    size = std::max({1.0, bb.Diagonal().Length(), tb.Diagonal().Length()});
  }
  const int samples = 64;
  for (int k = 0; k <= samples; ++k) {
    const double e = s_edge.ParameterAt(static_cast<double>(k) / samples);
    ON_3dPoint sp, tp;
    ON_3dVector su, sv, suu, suv, svv, tu, tv, tuu, tuv, tvv;
    if (fixed_direction == 0) s.Ev2Der(s.Domain(0).Min(), e, sp, su, sv, suu, suv, svv);
    else s.Ev2Der(e, s.Domain(1).Min(), sp, su, sv, suu, suv, svv);
    if (target_fixed_direction == 0) t.Ev2Der(t.Domain(0).Min(), e, tp, tu, tv, tuu, tuv, tvv);
    else t.Ev2Der(e, t.Domain(1).Min(), tp, tu, tv, tuu, tuv, tvv);
    const ON_3dVector s_cross = fixed_direction == 0 ? su : sv;
    const ON_3dVector t_cross = target_fixed_direction == 0 ? tu : tv;
    const ON_3dVector s_cc = fixed_direction == 0 ? suu : svv;
    const ON_3dVector t_cc = target_fixed_direction == 0 ? tuu : tvv;
    local.max_position_error = std::max(local.max_position_error, sp.DistanceTo(tp));
    if (rows_needed >= 2) local.max_tangent_error = std::max(local.max_tangent_error, (s_cross + scale * t_cross).Length());
    if (rows_needed >= 3) local.max_curvature_error = std::max(local.max_curvature_error, (s_cc - scale * scale * t_cc).Length());
  }
  if (!at_min && !s.Reverse(fixed_direction)) { surface_ = backup; return Result::Failed; }
  if (report) *report = local;
  const double tol = 1e-9 * size;
  if (!(local.max_position_error <= tol) || !(local.max_tangent_error <= tol * std::max(1.0, scale)) ||
      !(local.max_curvature_error <= tol * std::max(1.0, scale * scale) * 10.0) || !s.IsValid()) {
    surface_ = backup;
    return Result::Failed;
  }
  return Result::Ok;
}

Result NurbsSurface::UnrollDevelopable(int u_divisions, int v_divisions, Mesh& out_flat, double* out_area,
                                       DevelopableKind* out_kind) const {
  if (u_divisions < 1 || v_divisions < 1) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::UnrollDevelopable: division counts must be >= 1");
  }
  // A generous tolerance relative to the surface's own size: IsPlanar/
  // IsCylinder/IsCone's default ON_ZERO_TOLERANCE is an absolute 1e-12,
  // too tight for anything but a hand-built exact primitive (e.g. a real
  // NurbsSurface::FromControlGrid() plane already fails it once the grid
  // spans more than a few units, from ordinary floating-point rounding
  // in PointAt() - confirmed by a debug run). DistanceForSize()-style
  // relative scaling, same reasoning as the rest of this file's
  // tolerance choices.
  ON_BoundingBox bbox;
  surface_.GetBoundingBox(bbox, false);
  const double tol = std::max(1e-9, 1e-7 * bbox.Diagonal().Length());

  ON_Cylinder cyl;
  ON_Cone cone;
  ON_Plane plane;
  DevelopableKind kind;
  if (surface_.IsCylinder(&cyl, tol)) {
    kind = DevelopableKind::Cylinder;
  } else if (surface_.IsCone(&cone, tol)) {
    kind = DevelopableKind::Cone;
  } else if (surface_.IsPlanar(&plane, tol)) {
    kind = DevelopableKind::Plane;
  } else {
    return Result::Failed;
  }

  const Interval du = Domain(0), dv = Domain(1);
  const int nu = u_divisions, nv = v_divisions;
  ON_Mesh m;
  const double cos_half_angle = kind == DevelopableKind::Cone ? std::cos(cone.AngleInRadians()) : 0.0;
  const double sin_half_angle = kind == DevelopableKind::Cone ? std::sin(cone.AngleInRadians()) : 0.0;

  // Which parametric direction is the primitive's own circular one -
  // ON_Cylinder::ClosestPointTo()/ON_Cone::ClosestPointTo()'s "angular
  // parameter" wraps at +/-pi (ON's own atan2-based convention), so a
  // naive per-vertex angle lookup would tear a closed (full-circle)
  // sweep apart at that branch cut - one column's worth of vertices
  // would jump back by a full 2*pi instead of continuing smoothly,
  // producing a self-overlapping flat mesh at the seam. Real, not
  // hypothetical: confirmed by a debug run on a genuine 360-degree
  // ON_Cylinder::GetNurbForm() wall (see the "unwraps" tests). Fixed by
  // detecting which direction is circular (IsClosed() is unambiguous
  // when the sweep is a genuine full loop; GetNurbForm()'s own
  // convention - closed in U - is the fallback for a partial sweep,
  // where no wrap can occur but a direction still must be picked) and
  // unwrapping that direction's raw angle sequence to be continuous
  // (standard phase-unwrap: add/subtract 2*pi whenever a step's jump
  // exceeds pi) before it's ever turned into a flat coordinate.
  const int circular_dir = (surface_.IsClosed(1) && !surface_.IsClosed(0)) ? 1 : 0;

  // Pass 1: raw (angle-or-x, height-or-y) at every grid point, plus the
  // per-row/column continuous-unwrap correction along circular_dir.
  std::vector<std::vector<double>> a(static_cast<size_t>(nu) + 1, std::vector<double>(static_cast<size_t>(nv) + 1));
  std::vector<std::vector<double>> b(static_cast<size_t>(nu) + 1, std::vector<double>(static_cast<size_t>(nv) + 1));
  for (int i = 0; i <= nu; ++i) {
    const double u = du.min + (du.max - du.min) * i / nu;
    for (int j = 0; j <= nv; ++j) {
      const double v = dv.min + (dv.max - dv.min) * j / nv;
      const ON_3dPoint p = PointAt(u, v);
      switch (kind) {
        case DevelopableKind::Cylinder: cyl.ClosestPointTo(p, &a[static_cast<size_t>(i)][static_cast<size_t>(j)], &b[static_cast<size_t>(i)][static_cast<size_t>(j)]); break;
        case DevelopableKind::Cone: cone.ClosestPointTo(p, &a[static_cast<size_t>(i)][static_cast<size_t>(j)], &b[static_cast<size_t>(i)][static_cast<size_t>(j)]); break;
        case DevelopableKind::Plane: {
          double pu = 0.0, pv = 0.0;
          plane.ClosestPointTo(p, &pu, &pv);
          a[static_cast<size_t>(i)][static_cast<size_t>(j)] = pu;
          b[static_cast<size_t>(i)][static_cast<size_t>(j)] = pv;
          break;
        }
      }
    }
  }
  if (kind != DevelopableKind::Plane) {
    auto unwrap_line = [](std::vector<double>& line) {
      for (size_t k = 1; k < line.size(); ++k) {
        while (line[k] - line[k - 1] > ON_PI) line[k] -= 2.0 * ON_PI;
        while (line[k] - line[k - 1] < -ON_PI) line[k] += 2.0 * ON_PI;
      }
    };
    if (circular_dir == 0) {
      for (int j = 0; j <= nv; ++j) {
        std::vector<double> line(static_cast<size_t>(nu) + 1);
        for (int i = 0; i <= nu; ++i) line[static_cast<size_t>(i)] = a[static_cast<size_t>(i)][static_cast<size_t>(j)];
        unwrap_line(line);
        for (int i = 0; i <= nu; ++i) a[static_cast<size_t>(i)][static_cast<size_t>(j)] = line[static_cast<size_t>(i)];
      }
    } else {
      for (int i = 0; i <= nu; ++i) unwrap_line(a[static_cast<size_t>(i)]);
    }
  }

  // Pass 2: map the now-continuous (angle-or-x, height-or-y) grid to
  // flat coordinates and write the mesh.
  for (int i = 0; i <= nu; ++i) {
    for (int j = 0; j <= nv; ++j) {
      const double angle = a[static_cast<size_t>(i)][static_cast<size_t>(j)];
      const double lin = b[static_cast<size_t>(i)][static_cast<size_t>(j)];
      double fx = 0.0, fy = 0.0;
      switch (kind) {
        case DevelopableKind::Cylinder:
          fx = cyl.circle.radius * angle;
          fy = lin;
          break;
        case DevelopableKind::Cone: {
          const double slant = lin / cos_half_angle;
          const double flat_angle = angle * sin_half_angle;
          fx = slant * std::cos(flat_angle);
          fy = slant * std::sin(flat_angle);
          break;
        }
        case DevelopableKind::Plane:
          fx = angle;
          fy = lin;
          break;
      }
      m.SetVertex(i * (nv + 1) + j, ON_3dPoint(fx, fy, 0.0));
    }
  }
  for (int i = 0; i < nu; ++i) {
    for (int j = 0; j < nv; ++j) {
      m.SetQuad(i * nv + j, i * (nv + 1) + j, (i + 1) * (nv + 1) + j, (i + 1) * (nv + 1) + j + 1, i * (nv + 1) + j + 1);
    }
  }
  m.ComputeFaceNormals();
  out_flat.raw() = m;
  if (out_area) *out_area = out_flat.Area();
  if (out_kind) *out_kind = kind;
  return Result::Ok;
}

Result NurbsSurface::CoonsPatch(const NurbsCurve& bottom, const NurbsCurve& top, const NurbsCurve& left,
                                const NurbsCurve& right, NurbsSurface& out, double tolerance,
                                double* out_corner_gap) {
  // Working copies, reparameterized onto [0, 1] (shape-preserving).
  ON_NurbsCurve c0 = bottom.raw(), c1_fwd = top.raw(), d0 = left.raw(), d1_fwd = right.raw();
  c0.SetDomain(0.0, 1.0);
  c1_fwd.SetDomain(0.0, 1.0);
  d0.SetDomain(0.0, 1.0);
  d1_fwd.SetDomain(0.0, 1.0);
  ON_NurbsCurve c1_rev = c1_fwd, d1_rev = d1_fwd;
  // ON_NurbsCurve::Reverse() does not preserve the [0, 1] domain just
  // set above (confirmed by a debug run: its own domain ends up
  // negated, e.g. [-1, 0]) - re-normalize immediately so PointAt(0)/
  // PointAt(1) below correctly mean "new start"/"new end".
  c1_rev.Reverse();
  c1_rev.SetDomain(0.0, 1.0);
  d1_rev.Reverse();
  d1_rev.SetDomain(0.0, 1.0);

  // Try all 4 orientations of (top, right) against the fixed (bottom,
  // left) reference and keep whichever best closes all 4 corners.
  const ON_3dPoint p00 = c0.PointAt(0.0), p10 = c0.PointAt(1.0);
  double best_gap = std::numeric_limits<double>::infinity();
  int best_c1 = 0, best_d1 = 0;  // 0 = forward, 1 = reversed
  for (int ci = 0; ci < 2; ++ci) {
    const ON_NurbsCurve& c1 = ci == 0 ? c1_fwd : c1_rev;
    for (int di = 0; di < 2; ++di) {
      const ON_NurbsCurve& d1 = di == 0 ? d1_fwd : d1_rev;
      const double gap = d0.PointAt(0.0).DistanceTo(p00) + d1.PointAt(0.0).DistanceTo(p10) +
                          d0.PointAt(1.0).DistanceTo(c1.PointAt(0.0)) + d1.PointAt(1.0).DistanceTo(c1.PointAt(1.0));
      if (gap < best_gap) { best_gap = gap; best_c1 = ci; best_d1 = di; }
    }
  }
  if (out_corner_gap) *out_corner_gap = best_gap;
  if (!(best_gap <= 4.0 * tolerance)) return Result::Failed;
  ON_NurbsCurve c1 = best_c1 == 0 ? c1_fwd : c1_rev;
  ON_NurbsCurve d1 = best_d1 == 0 ? d1_fwd : d1_rev;

  // Shared degree/knots within each curve pair (shape-preserving).
  if (!UnifyCurves(c0, c1, tolerance) || !UnifyCurves(d0, d1, tolerance)) return Result::Failed;
  std::vector<double> u_knots(static_cast<size_t>(c0.KnotCount()));
  for (int k = 0; k < c0.KnotCount(); ++k) u_knots[static_cast<size_t>(k)] = c0.Knot(k);
  std::vector<double> v_knots(static_cast<size_t>(d0.KnotCount()));
  for (int k = 0; k < d0.KnotCount(); ++k) v_knots[static_cast<size_t>(k)] = d0.Knot(k);
  const int pu = c0.Degree(), pv = d0.Degree();

  // R1 = ruled between c0 (v=0) and c1 (v=1); R2 = ruled between d0
  // (u=0) and d1 (u=1); B = bilinear corner patch (always non-rational
  // - the classical Coons construction's correction term is exact
  // corner *positions*, not weighted). Bring all three to the one
  // shared (pu, u_knots) x (pv, v_knots) structure.
  NurbsSurface r1 = RuleBetween(c0, c1, 1);
  NurbsSurface r2 = RuleBetween(d0, d1, 0);
  // FromControlGrid's control_grid is indexed idx = u * v_count + v (v
  // varies fastest) - confirmed directly against its implementation,
  // not its own doc comment (which says the opposite; see the fix in
  // this same commit) - so CV(0,0)=P00, CV(0,1)=P01, CV(1,0)=P10,
  // CV(1,1)=P11 needs this order, not [P00, P10, P01, P11].
  NurbsSurface b = FromControlGrid({c0.PointAt(0.0), c1.PointAt(0.0), c0.PointAt(1.0), c1.PointAt(1.0)}, 2, 2, 1, 1);
  if (!BringDirectionTo(r1, 1, pv, v_knots, tolerance) || !BringDirectionTo(r2, 0, pu, u_knots, tolerance) ||
      !BringDirectionTo(b, 0, pu, u_knots, tolerance) || !BringDirectionTo(b, 1, pv, v_knots, tolerance)) {
    return Result::Failed;
  }
  if (r1.CVCountU() != r2.CVCountU() || r1.CVCountU() != b.CVCountU() || r1.CVCountV() != r2.CVCountV() ||
      r1.CVCountV() != b.CVCountV()) {
    return Result::Failed;
  }

  const bool rational = r1.IsRational() || r2.IsRational();
  ON_NurbsSurface result;
  if (!result.Create(3, rational, pu + 1, pv + 1, r1.CVCountU(), r1.CVCountV())) return Result::Failed;
  for (int k = 0; k < result.KnotCount(0); ++k) result.SetKnot(0, k, r1.KnotAt(0, k));
  for (int k = 0; k < result.KnotCount(1); ++k) result.SetKnot(1, k, r1.KnotAt(1, k));
  for (int i = 0; i < r1.CVCountU(); ++i) {
    for (int j = 0; j < r1.CVCountV(); ++j) {
      ON_4dPoint a, c, e;
      r1.raw().GetCV(i, j, a);
      r2.raw().GetCV(i, j, c);
      b.raw().GetCV(i, j, e);
      if (!r1.IsRational()) a.w = 1.0;
      if (!r2.IsRational()) c.w = 1.0;
      e.w = 1.0;
      const ON_4dPoint cv = Add4(Sub4(a, e), c);
      if (rational) {
        if (!(cv.w > 0.0)) return Result::Failed;
        result.SetCV(i, j, cv);
      } else {
        result.SetCV(i, j, ON_3dPoint(cv.x, cv.y, cv.z));
      }
    }
  }
  if (!result.IsValid()) return Result::Failed;

  // Self-check: the built surface's own 4 boundary isocurves must
  // reproduce the (reparameterized, orientation-corrected) inputs.
  NurbsSurface candidate;
  candidate.raw() = result;
  double residual = 0.0;
  for (int k = 0; k <= 32; ++k) {
    const double t = k / 32.0;
    residual = std::max(residual, candidate.PointAt(t, 0.0).DistanceTo(c0.PointAt(t)));
    residual = std::max(residual, candidate.PointAt(t, 1.0).DistanceTo(c1.PointAt(t)));
    residual = std::max(residual, candidate.PointAt(0.0, t).DistanceTo(d0.PointAt(t)));
    residual = std::max(residual, candidate.PointAt(1.0, t).DistanceTo(d1.PointAt(t)));
  }
  if (!(residual <= 1e-6 * std::max(1.0, p00.DistanceTo(p10)))) return Result::Failed;

  out = candidate;
  return Result::Ok;
}

}  // namespace dino8::kernel
