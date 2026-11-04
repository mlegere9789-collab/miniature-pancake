// NurbsSurface editing operations (knot removal, refit, matching, ...):
// the "surface editing" class of operations a Parasolid/ACIS-style kernel
// exposes. Kept in their own translation unit rather than surface.cpp so
// they can grow without churning that file's evaluation/tessellation
// code.

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/surface_intersect.h"
#include "dino8/kernel/tolerance.h"

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

// Extends ON_NurbsSurface `s` in `direction` at one end (at_max: the
// domain-max end if true, domain-min if false) by world-parameter
// `delta` (> 0), in place, via the arithmetic-progression construction
// documented on NurbsSurface::ExtendLinear() - `degree` new control
// points per cross-index row, homogeneous throughout. Returns false if
// `s` isn't clamped in `direction` (an unclamped/periodic end has no
// single well-defined "boundary control point and end derivative" this
// construction reads from).
bool ExtendOneEndLinear(ON_NurbsSurface& s, int direction, bool at_max, double delta) {
  if (!s.IsClamped(direction, 2)) return false;
  const int p = s.Degree(direction);
  const int old_count = s.CVCount(direction);
  const int other_count = s.CVCount(1 - direction);
  const bool rational = s.IsRational();

  // Textbook (uncompressed) knot vector U[0..m] in `direction`, same
  // convention RemoveKnotAt() above already uses: ON's compressed
  // Knot(k) is U[k + 1], with the two clamped-end copies filled in.
  const int old_knot_count = s.KnotCount(direction);
  std::vector<double> u_full(static_cast<size_t>(old_knot_count) + 2);
  for (int k = 0; k < old_knot_count; ++k) u_full[static_cast<size_t>(k + 1)] = s.Knot(direction, k);
  u_full[0] = u_full[1];
  u_full[static_cast<size_t>(old_knot_count + 1)] = u_full[static_cast<size_t>(old_knot_count)];

  // Existing end derivative D per row, in homogeneous coordinates - the
  // same clamped-B-spline end-derivative quantity MatchEdge() computes,
  // read here rather than chosen: D = p / (U[p+1] - U[1]) * (Pw_1 -
  // Pw_0) at the min end, or its symmetric counterpart at the max end.
  std::vector<ON_4dPoint> boundary(static_cast<size_t>(other_count)), deriv(static_cast<size_t>(other_count));
  for (int row = 0; row < other_count; ++row) {
    ON_4dPoint p0, p1;
    if (at_max) {
      s.GetCV(direction == 0 ? old_count - 1 : row, direction == 0 ? row : old_count - 1, p0);
      s.GetCV(direction == 0 ? old_count - 2 : row, direction == 0 ? row : old_count - 2, p1);
    } else {
      s.GetCV(direction == 0 ? 0 : row, direction == 0 ? row : 0, p0);
      s.GetCV(direction == 0 ? 1 : row, direction == 0 ? row : 1, p1);
    }
    if (!rational) { p0.w = 1.0; p1.w = 1.0; }
    const double span = at_max ? u_full[static_cast<size_t>(old_knot_count)] - u_full[static_cast<size_t>(old_knot_count - p)]
                                : u_full[static_cast<size_t>(p + 1)] - u_full[static_cast<size_t>(1)];
    const double scale = p / span;
    boundary[static_cast<size_t>(row)] = p0;
    deriv[static_cast<size_t>(row)] = at_max ? Scale4(Sub4(p0, p1), scale) : Scale4(Sub4(p1, p0), scale);
  }

  // New knot vector: the join (the *old* boundary value) is left
  // completely untouched at its existing multiplicity p + 1 (still a
  // fully clamped end from the "before" side's own perspective) - this
  // is a genuine independent Bezier-like span glued on, not a knot
  // refinement of the existing curve, so the "before" shape needs no
  // adjustment at all. p + 1 new copies of the *new* boundary value are
  // appended (max end) or prepended (min end), giving that far end the
  // same clamped multiplicity p + 1 every other end in this codebase
  // has. Net: p + 1 new control points (indices old_count..old_count+p
  // at the max end; the new CV at the shared/join index coincides
  // exactly with the old boundary CV, by construction, not by knot-
  // vector magic - this file's other constructions duplicate a shared
  // boundary point the same way, e.g. RuleBetween()'s CV(i, 0)/CV(i, 1)
  // rows in CoonsPatch above).
  // OpenNURBS caps a *interior* knot's legal multiplicity at `degree`
  // (multiplicity degree + 1 is reserved exclusively for the curve's
  // two true clamped ends - confirmed directly against IsValid()'s own
  // rejection message on a first attempt that kept the old boundary's
  // full p + 1 multiplicity unchanged and just appended a new clamped
  // end after it, which IsValid() correctly refused as an illegal
  // "degree + 1 run in the interior"). So the join's multiplicity drops
  // by exactly one, from p + 1 to p, by dropping a single (redundant -
  // all copies hold the same value) occurrence of the old boundary
  // value from the textbook vector before appending the new end's own
  // p + 1 copies - verified in the tests to still reproduce the
  // original shape on the untouched side exactly (the removed copy
  // was one of p + 1 *identical* values, not a distinct knot, so no
  // information about the original curve's own shape is lost).
  const int new_count = old_count + p;
  ON_NurbsSurface out;
  const bool ok = direction == 0 ? out.Create(3, rational, p + 1, s.Order(1), new_count, other_count)
                                 : out.Create(3, rational, s.Order(0), p + 1, other_count, new_count);
  if (!ok) return false;
  const double new_boundary = at_max ? u_full[static_cast<size_t>(old_knot_count)] + delta : u_full[1] - delta;
  std::vector<double> new_full(u_full);
  if (at_max) { new_full.pop_back(); new_full.insert(new_full.end(), static_cast<size_t>(p + 1), new_boundary); }
  else { new_full.erase(new_full.begin()); new_full.insert(new_full.begin(), static_cast<size_t>(p + 1), new_boundary); }
  for (int k = 0; k < out.KnotCount(direction); ++k) out.SetKnot(direction, k, new_full[static_cast<size_t>(k + 1)]);
  for (int k = 0; k < s.KnotCount(1 - direction); ++k) out.SetKnot(1 - direction, k, s.Knot(1 - direction, k));

  for (int row = 0; row < other_count; ++row) {
    const double step = delta / p;
    for (int i = 0; i < old_count; ++i) {
      ON_4dPoint cv;
      s.GetCV(direction == 0 ? i : row, direction == 0 ? row : i, cv);
      if (!rational) cv.w = 1.0;
      const int ii = direction == 0 ? (at_max ? i : i + p) : row;
      const int jj = direction == 0 ? row : (at_max ? i : i + p);
      if (rational) out.SetCV(ii, jj, cv); else out.SetCV(ii, jj, ON_3dPoint(cv.x, cv.y, cv.z));
    }
    for (int k = 1; k <= p; ++k) {
      const ON_4dPoint cv = Add4(boundary[static_cast<size_t>(row)], Scale4(deriv[static_cast<size_t>(row)], k * step * (at_max ? 1.0 : -1.0)));
      if (rational && !(cv.w > 0.0)) return false;
      const int idx = at_max ? old_count - 1 + k : p - k;
      const int ii = direction == 0 ? idx : row;
      const int jj = direction == 0 ? row : idx;
      if (rational) out.SetCV(ii, jj, cv); else out.SetCV(ii, jj, ON_3dPoint(cv.x, cv.y, cv.z));
    }
  }
  {
    ON_TextLog log(stderr);
    bool valid = out.IsValid(&log);
    if (!valid) {
      std::fprintf(stderr, "DEBUG knots: ");
      for (int k = 0; k < out.KnotCount(direction); ++k) std::fprintf(stderr, "%g ", out.Knot(direction,k));
      std::fprintf(stderr, "\ncvcount=%d order=%d\n", out.CVCount(direction), out.Order(direction));
    }
  }
  if (!out.IsValid()) return false;
  s = out;
  return true;
}


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

Result NurbsSurface::ChangeSeam(int direction, double t) {
  if (direction != 0 && direction != 1) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::ChangeSeam: direction must be 0 (U) or 1 (V)");
  }
  if (!IsClosed(direction)) return Result::Failed;
  const Interval dom = Domain(direction);
  if (!(t >= dom.min && t <= dom.max)) return Result::Failed;
  return surface_.ChangeSurfaceSeam(direction, t) ? Result::Ok : Result::Failed;
}

Result NurbsSurface::ReparameterizeRational(int direction, double shape_factor, NurbsSurface& out) const {
  if (direction != 0 && direction != 1) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::ReparameterizeRational: direction must be 0 (U) or 1 (V)");
  }
  if (!ON_IsValid(shape_factor) || !(shape_factor > 0.0)) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::ReparameterizeRational: shape_factor must "
        "be a finite, positive number");
  }
  if (shape_factor == 1.0) {
    out = *this;
    return Result::NoOpAlreadySatisfied;
  }
  if (surface_.IsPeriodic(direction)) return Result::Failed;

  ON_NurbsSurface ns = surface_;
  const int p = ns.Degree(direction);

  // Decompose `direction` to full (degree p) multiplicity at every
  // interior knot - the surface-level equivalent of
  // ON_NurbsCurve::MakePiecewiseBezier(), which has no direct
  // ON_NurbsSurface counterpart. Collect every distinct knot value and
  // its own current multiplicity FIRST (inserting by VALUE, like
  // InsertKnotAt() above already does, is safe regardless of visit
  // order - each insertion only shifts the INDEX of later knots, never
  // their value).
  {
    std::vector<double> distinct;
    std::vector<int> mult;
    const int kc = ns.KnotCount(direction);
    int i = 0;
    while (i < kc) {
      const double v = ns.Knot(direction, i);
      int m = 1;
      while (i + m < kc && ns.Knot(direction, i + m) == v) ++m;
      distinct.push_back(v);
      mult.push_back(m);
      i += m;
    }
    for (size_t k = 1; k + 1 < distinct.size(); ++k) {
      // ON_NurbsSurface::InsertKnot()'s own `knot_multiplicity` is the
      // TOTAL target multiplicity at that knot value, not how many MORE
      // to add on top of what is already there (confirmed directly
      // against OpenNURBS' own `ON_InsertKnot()` source, which computes
      // the actual insert count as `knot_multiplicity - <existing
      // multiplicity found at that value>` - passing `p - mult[k]`
      // here, as if it meant "how many more", silently inserted far
      // fewer knots than intended on a real multi-span surface,
      // caught by a direct before/after knot-array dump, not assumed
      // correct from the header's own ambiguous comment).
      if (mult[static_cast<size_t>(k)] < p && !ns.InsertKnot(direction, distinct[k], p)) return Result::Failed;
    }
  }

  const int spans = ns.SpanCount(direction);
  const double T0 = ns.Domain(direction)[0];
  const double T1 = ns.Domain(direction)[1];
  if (spans < 1 || !(T1 > T0)) return Result::Failed;

  const int cv_dir = ns.CVCount(direction);
  const int cv_other = ns.CVCount(1 - direction);

  // Pure multiplicative weight SCALE per index along `direction` (not
  // an absolute weight, since a surface's existing weight varies with
  // the OTHER direction too) - see this method's own doc comment
  // (surface.h) for why the identical per-span derivation
  // `NurbsCurve::ReparameterizeRational()` already uses applies
  // uniformly across every row/column of the other direction.
  std::vector<double> scale(static_cast<size_t>(cv_dir), 1.0);
  std::vector<double> breakpoint(static_cast<size_t>(spans + 1));
  breakpoint[0] = T0;

  const double c = shape_factor;
  for (int s = 0; s < spans; ++s) {
    const double t0 = ns.Knot(direction, s * p);
    const double t1 = ns.Knot(direction, (s + 1) * p);
    const double u_a = (t0 - T0) / (T1 - T0);
    const double u_b = (t1 - T0) / (T1 - T0);
    const double v_a = u_a / (c - (c - 1.0) * u_a);
    const double v_b = u_b / (c - (c - 1.0) * u_b);
    if (!(v_b > v_a)) return Result::Failed;  // shouldn't happen for a valid span and c > 0
    const double denom_a = 1.0 + (c - 1.0) * v_a;
    const double dudv_at_va = c / (denom_a * denom_a);
    const double c_local = dudv_at_va * (v_b - v_a) / (u_b - u_a);

    const int cv_base = s * p;
    std::vector<double> raw_power(static_cast<size_t>(p + 1));
    double power = 1.0;
    for (int j = 0; j <= p; ++j) {
      raw_power[static_cast<size_t>(j)] = power;
      power *= c_local;
    }
    // Every span after the first shares its own first control index
    // with the previous span's own last one - its own SCALE (not any
    // one row's weight, which differs per row) must agree from both
    // sides, so rescale this whole span uniformly to match.
    double r_s = 1.0;
    if (s > 0) r_s = scale[static_cast<size_t>(cv_base)] / raw_power[0];
    for (int j = 0; j <= p; ++j) {
      scale[static_cast<size_t>(cv_base + j)] = raw_power[static_cast<size_t>(j)] * r_s;
    }
    breakpoint[static_cast<size_t>(s + 1)] = T0 + v_b * (T1 - T0);
  }

  if (!ns.IsRational()) {
    if (!ns.MakeRational()) return Result::Failed;
  }
  for (int k = 0; k < cv_dir; ++k) {
    const double sc = scale[static_cast<size_t>(k)];
    for (int o = 0; o < cv_other; ++o) {
      const int i = (direction == 0) ? k : o;
      const int j = (direction == 0) ? o : k;
      ON_3dPoint pt;
      ns.GetCV(i, j, pt);
      const double w = ns.Weight(i, j) * sc;
      const ON_4dPoint hpt(pt.x * w, pt.y * w, pt.z * w, w);
      if (!ns.SetCV(i, j, hpt)) return Result::Failed;
    }
  }
  for (int b = 0; b <= spans; ++b) {
    for (int k = 0; k < p; ++k) {
      if (!ns.SetKnot(direction, b * p + k, breakpoint[static_cast<size_t>(b)])) return Result::Failed;
    }
  }

  if (!ns.IsValid()) return Result::Failed;
  out.raw() = ns;
  return Result::Ok;
}

Result NurbsSurface::MakeUniform(int direction, double tolerance, double* out_max_deviation) {
  if (direction != 0 && direction != 1) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::MakeUniform: direction must be 0 (U) or 1 (V)");
  }
  if (!ON_IsValid(tolerance) || !(tolerance > 0.0)) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::MakeUniform: tolerance must be a finite, positive number");
  }

  const int order = surface_.Order(direction);
  const int cv_count = surface_.CVCount(direction);
  if (ON_IsKnotVectorClamped(order, cv_count, surface_.Knot(direction)) &&
      ON_IsKnotVectorUniform(order, cv_count, surface_.Knot(direction))) {
    if (out_max_deviation) *out_max_deviation = 0.0;
    return Result::NoOpAlreadySatisfied;
  }

  NurbsSurface candidate = *this;
  if (!candidate.raw().MakeClampedUniformKnotVector(direction, 1.0)) return Result::Failed;
  const Interval dom = Domain(direction);
  if (candidate.SetDomain(direction, dom.min, dom.max) == Result::Failed) return Result::Failed;

  const double deviation = MaxSampledDeviationFrom(candidate);
  if (out_max_deviation) *out_max_deviation = deviation;
  if (deviation > tolerance) return Result::Failed;

  *this = candidate;
  return Result::Ok;
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

Result NurbsSurface::ReduceDegree(int direction, int target_degree, double tolerance,
                                   double* out_max_deviation) {
  if (direction != 0 && direction != 1) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::ReduceDegree: direction must be 0 or 1");
  }
  if (target_degree < 1) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::ReduceDegree: target_degree must be at least 1");
  }
  if (!(tolerance > 0.0)) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::ReduceDegree: tolerance must be positive");
  }
  if (out_max_deviation) *out_max_deviation = 0.0;

  const int current_degree = direction == 0 ? DegreeU() : DegreeV();
  if (target_degree >= current_degree) {
    return Result::NoOpAlreadySatisfied;
  }

  const int original_u_count = CVCountU();
  const int original_v_count = CVCountV();
  const int original_u_degree = DegreeU();
  const int original_v_degree = DegreeV();
  const int original_count = direction == 0 ? original_u_count : original_v_count;

  std::vector<int> candidate_counts;
  for (int cv = target_degree + 1; cv < original_count; cv *= 2) candidate_counts.push_back(cv);
  if (candidate_counts.empty() || candidate_counts.back() != original_count) {
    candidate_counts.push_back(original_count);
  }

  double last_worst = 0.0;
  for (int cv_count : candidate_counts) {
    const int u_count = direction == 0 ? cv_count : original_u_count;
    const int v_count = direction == 1 ? cv_count : original_v_count;
    const int u_degree = direction == 0 ? target_degree : original_u_degree;
    const int v_degree = direction == 1 ? target_degree : original_v_degree;
    const int u_samples = std::max(64, u_count);
    const int v_samples = std::max(64, v_count);

    NurbsSurface candidate;
    double worst = 0.0;
    if (Rebuild(u_count, v_count, u_degree, v_degree, candidate, &worst, u_samples, v_samples) != Result::Ok) {
      continue;
    }
    last_worst = worst;
    if (worst <= tolerance) {
      surface_ = candidate.raw();
      if (out_max_deviation) *out_max_deviation = worst;
      return Result::Ok;
    }
  }
  if (out_max_deviation) *out_max_deviation = last_worst;
  return Result::Failed;
}

Result NurbsSurface::InterpolateThroughGrid(const std::vector<Point3d>& grid, int u_count, int v_count,
                                             NurbsSurface& out) {
  if (u_count < 2 || v_count < 2) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::InterpolateThroughGrid: u_count and v_count must each be >= 2");
  }
  const size_t expected = static_cast<size_t>(u_count) * static_cast<size_t>(v_count);
  if (grid.size() != expected) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::InterpolateThroughGrid: grid must have exactly u_count * v_count = " +
        std::to_string(expected) + " entries, got " + std::to_string(grid.size()));
  }
  auto at = [&](int u, int v) -> ON_3dPoint {
    return grid[static_cast<size_t>(u) * static_cast<size_t>(v_count) + static_cast<size_t>(v)];
  };

  // Shared U params: average every row's (fixed v) own [0, 1]-normalized
  // chord-length parameterization - Piegl & Tiller's own prescribed
  // method for surface interpolation, so every row is interpolated at
  // the identical parameter values (see this method's own doc comment
  // in surface.h for why that's what makes the two passes below combine
  // into one consistent tensor-product surface at all).
  std::vector<double> u_params(static_cast<size_t>(u_count), 0.0);
  for (int v = 0; v < v_count; ++v) {
    std::vector<ON_3dPoint> row(static_cast<size_t>(u_count));
    for (int u = 0; u < u_count; ++u) row[static_cast<size_t>(u)] = at(u, v);
    const std::vector<double> cp = ChordParams(row, /*closed=*/false);
    const double total = cp.back();
    if (!(total > 0.0)) {
      throw std::invalid_argument("dino8::kernel::NurbsSurface::InterpolateThroughGrid: row v=" +
                                   std::to_string(v) + " is degenerate (every point coincides)");
    }
    for (int u = 0; u < u_count; ++u) u_params[static_cast<size_t>(u)] += cp[static_cast<size_t>(u)] / total;
  }
  for (double& p : u_params) p /= v_count;

  // Shared V params: the same averaging, over every column (fixed u).
  std::vector<double> v_params(static_cast<size_t>(v_count), 0.0);
  for (int u = 0; u < u_count; ++u) {
    std::vector<ON_3dPoint> col(static_cast<size_t>(v_count));
    for (int v = 0; v < v_count; ++v) col[static_cast<size_t>(v)] = at(u, v);
    const std::vector<double> cp = ChordParams(col, /*closed=*/false);
    const double total = cp.back();
    if (!(total > 0.0)) {
      throw std::invalid_argument("dino8::kernel::NurbsSurface::InterpolateThroughGrid: column u=" +
                                   std::to_string(u) + " is degenerate (every point coincides)");
    }
    for (int v = 0; v < v_count; ++v) v_params[static_cast<size_t>(v)] += cp[static_cast<size_t>(v)] / total;
  }
  for (double& p : v_params) p /= u_count;

  // Pass 1: interpolate every row (fixed v) along U -> intermediate[v][u].
  std::vector<std::vector<ON_3dPoint>> intermediate(static_cast<size_t>(v_count));
  ON_NurbsCurve u_curve;
  for (int v = 0; v < v_count; ++v) {
    std::vector<ON_3dPoint> row(static_cast<size_t>(u_count));
    for (int u = 0; u < u_count; ++u) row[static_cast<size_t>(u)] = at(u, v);
    const ON_NurbsCurve c = InterpolateCubic(row, u_params, /*closed=*/false, /*dim=*/3);
    if (!c.IsValid() || c.CVCount() != u_count) return Result::Failed;
    if (v == 0) u_curve = c;
    intermediate[static_cast<size_t>(v)].resize(static_cast<size_t>(u_count));
    for (int u = 0; u < u_count; ++u) c.GetCV(u, intermediate[static_cast<size_t>(v)][static_cast<size_t>(u)]);
  }

  // Pass 2: interpolate every column (fixed u) of the intermediate
  // control points along V -> final_cv[u][v].
  ON_NurbsCurve v_curve;
  std::vector<std::vector<ON_3dPoint>> final_cv(static_cast<size_t>(u_count));
  for (int u = 0; u < u_count; ++u) {
    std::vector<ON_3dPoint> col(static_cast<size_t>(v_count));
    for (int v = 0; v < v_count; ++v) col[static_cast<size_t>(v)] = intermediate[static_cast<size_t>(v)][static_cast<size_t>(u)];
    const ON_NurbsCurve c = InterpolateCubic(col, v_params, /*closed=*/false, /*dim=*/3);
    if (!c.IsValid() || c.CVCount() != v_count) return Result::Failed;
    if (u == 0) v_curve = c;
    final_cv[static_cast<size_t>(u)].resize(static_cast<size_t>(v_count));
    for (int v = 0; v < v_count; ++v) c.GetCV(v, final_cv[static_cast<size_t>(u)][static_cast<size_t>(v)]);
  }

  ON_NurbsSurface result;
  if (!result.Create(3, false, u_curve.Order(), v_curve.Order(), u_count, v_count)) return Result::Failed;
  for (int k = 0; k < result.KnotCount(0); ++k) result.SetKnot(0, k, u_curve.Knot(k));
  for (int k = 0; k < result.KnotCount(1); ++k) result.SetKnot(1, k, v_curve.Knot(k));
  for (int u = 0; u < u_count; ++u) {
    for (int v = 0; v < v_count; ++v) result.SetCV(u, v, final_cv[static_cast<size_t>(u)][static_cast<size_t>(v)]);
  }
  if (!result.IsValid()) return Result::Failed;
  out.surface_ = result;
  return Result::Ok;
}

Result NurbsSurface::DecomposeToBeziers(std::vector<NurbsSurface>& out_patches) const {
  out_patches.clear();

  ON_NurbsSurface s = surface_;
  bool already_bezier = true;
  for (int dir = 0; dir < 2; ++dir) {
    const int degree = s.Degree(dir);
    const int span_count = s.SpanCount(dir);
    if (span_count > 1) already_bezier = false;
    std::vector<double> span_vector(static_cast<size_t>(span_count) + 1);
    if (!s.GetSpanVector(dir, span_vector.data())) {
      return Result::Failed;
    }
    // Every interior breakpoint gets raised to full multiplicity
    // `degree` - InsertKnot()'s multiplicity argument is the *target*
    // multiplicity at that value (verified against OpenNURBS' own
    // ON_InsertKnot: `m = knot_multiplicity - <count already there>`),
    // so this is correct even if the breakpoint already has multiplicity
    // > 1 (e.g. a G0 kink) - it only ever adds the knots still missing.
    for (int i = 1; i < span_count; ++i) {
      if (!s.InsertKnot(dir, span_vector[static_cast<size_t>(i)], degree)) {
        return Result::Failed;
      }
    }
  }

  const int u_span_count = s.SpanCount(0);
  const int v_span_count = s.SpanCount(1);
  std::vector<double> u_span_vector(static_cast<size_t>(u_span_count) + 1);
  std::vector<double> v_span_vector(static_cast<size_t>(v_span_count) + 1);
  if (!s.GetSpanVector(0, u_span_vector.data()) || !s.GetSpanVector(1, v_span_vector.data())) {
    return Result::Failed;
  }

  out_patches.reserve(static_cast<size_t>(u_span_count) * static_cast<size_t>(v_span_count));
  for (int vi = 0; vi < v_span_count; ++vi) {
    for (int ui = 0; ui < u_span_count; ++ui) {
      ON_NurbsSurface patch = s;
      // Both cuts land exactly on knot values already present at full
      // multiplicity from the insertion pass above, so `Trim()` (de Boor
      // evaluation at an existing knot) reproduces that cell exactly -
      // the same exact-trim-at-a-knot pattern the app's own
      // ConvertToBeziers command already relies on for curves.
      if (!patch.Trim(0, ON_Interval(u_span_vector[static_cast<size_t>(ui)], u_span_vector[static_cast<size_t>(ui) + 1]))) {
        return Result::Failed;
      }
      if (!patch.Trim(1, ON_Interval(v_span_vector[static_cast<size_t>(vi)], v_span_vector[static_cast<size_t>(vi) + 1]))) {
        return Result::Failed;
      }
      NurbsSurface piece;
      piece.raw() = patch;
      out_patches.push_back(std::move(piece));
    }
  }

  return already_bezier ? Result::NoOpAlreadySatisfied : Result::Ok;
}

Result NurbsSurface::OffsetRefit(double distance, NurbsSurface& out, double tolerance, double* achieved_deviation,
                                 std::string* refusal_reason) const {
  if (!ON_IsValid(distance)) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::OffsetRefit: distance must be finite");
  }
  if (refusal_reason != nullptr) refusal_reason->clear();
  auto refuse = [&](const std::string& why) {
    if (refusal_reason != nullptr) *refusal_reason = why;
    return Result::Failed;
  };
  if (distance == 0.0) {
    out.surface_ = surface_;
    if (achieved_deviation != nullptr) *achieved_deviation = 0.0;
    return Result::Ok;
  }

  ON_BoundingBox bbox;
  surface_.GetBoundingBox(bbox, false);
  const double tol = tolerance > 0.0 ? tolerance : dino8::kernel::tolerance::DistanceForSize(bbox.Diagonal().Length());

  const Interval du = Domain(0);
  const Interval dv = Domain(1);
  const double u_range = du.max - du.min;
  const double v_range = dv.max - dv.min;

  // Fold-through-center-of-curvature guard - identical to
  // OffsetApproximate()'s own (surface.cpp), checked once up front: no
  // amount of refitting rescues a distance whose TRUE offset locus
  // already folds through itself.
  const SurfaceDivisions divs = SuggestedDivisions(tol);
  const int guard_nu = std::max({divs.u, 4 * CVCountU(), 1});
  const int guard_nv = std::max({divs.v, 4 * CVCountV(), 1});
  const std::string kDegenerateReason =
      "singular surface: the surface normal is undefined or direction-dependent (a collapsed edge whose normal "
      "differs along it, as at a cone apex, a zero-area fold, or a row/column of coincident control points), so "
      "the parallel surface is not a regular surface; no fitted substitute is returned";
  try {
    for (int i = 0; i < guard_nu; ++i) {
      const double u = du.min + u_range * (i + 0.5) / guard_nu;
      for (int j = 0; j < guard_nv; ++j) {
        const double v = dv.min + v_range * (j + 0.5) / guard_nv;
        const SurfaceCurvature sc = CurvatureAt(u, v);
        if (distance * sc.k1 >= 1.0 || distance * sc.k2 >= 1.0) {
          return refuse(
              "curvature fold: |distance| reaches the local radius of curvature, so the true offset locus folds "
              "through itself (no refit can represent it)");
        }
      }
    }
  } catch (const std::runtime_error&) {
    return refuse(kDegenerateReason);
  }

  // Ground-truth sample grid of the TRUE offset locus (the same formula
  // OffsetApproximate()'s own per-control-point translation approximates),
  // dense enough to resolve the chord tolerance requested and at least as
  // fine as this surface's own control net - the same density
  // OffsetApproximate()'s guard above already uses, so the grid that
  // measures deviation is at least as fine as the grid that already
  // proved there's no fold to worry about.
  //
  // `u_params`/`v_params` are the FIT grid's own parameter values and must
  // land EXACTLY on the domain boundary at the first/last index - unlike
  // OffsetApproximate()'s per-CV nudge, FitRowLeastSquares() (above) pins
  // its own first/last output control point directly to `data.front()`/
  // `data.back()`, which is only the correct clamped-B-spline corner value
  // when the sample it pins to was measured EXACTLY at the domain's own
  // t0/t1 (where N_0/N_n are exactly 1) - nudging these particular
  // parameters inward would silently re-introduce, at every control-point
  // count including the largest this method ever tries, a fixed
  // ~nudge-sized bias no amount of refinement removes (a real regression
  // caught by TestSurfaceOffsetRefitIsExactOnAGenuinePlane: a perfectly
  // flat plane's own offset is exactly affine and must fit to a
  // measured-zero residual, which a nudged corner never reaches). Only
  // the NORMAL lookup below is nudged (`nu`/`nv`, separate from `u`/`v`
  // themselves) to dodge a natural parametrization's own boundary pole,
  // the same hazard OffsetApproximate()'s own nudge exists for -
  // `PointAt()` itself is safe at an exact domain boundary (Rebuild()'s
  // own deviation measurement, above, already evaluates there directly).
  const int u_samples = guard_nu + 1;
  const int v_samples = guard_nv + 1;
  const double nudge_u = 1e-6 * u_range;
  const double nudge_v = 1e-6 * v_range;
  std::vector<double> u_params(static_cast<size_t>(u_samples)), v_params(static_cast<size_t>(v_samples));
  for (int i = 0; i < u_samples; ++i) u_params[static_cast<size_t>(i)] = du.min + u_range * i / (u_samples - 1.0);
  for (int j = 0; j < v_samples; ++j) v_params[static_cast<size_t>(j)] = dv.min + v_range * j / (v_samples - 1.0);
  std::vector<std::vector<ON_3dPoint>> offset_pts(static_cast<size_t>(u_samples),
                                                    std::vector<ON_3dPoint>(static_cast<size_t>(v_samples)));
  // A sample where the two partial derivatives vanish or are parallel has no
  // well-defined normal there. That is acceptable ONLY on a collapsed edge
  // (a pole) whose limiting normal is the same however the pole is
  // approached (a sphere/ellipsoid/dome pole): the nudged normals of every
  // coincident degenerate sample must then agree. A cone apex (normal
  // depends on the direction of approach), an interior zero-area point or a
  // wholly degenerate patch is refused: no regular parallel surface exists.
  struct PoleSample { Point3d p; ON_3dVector n; };
  std::vector<PoleSample> poles;
  try {
    for (int i = 0; i < u_samples; ++i) {
      for (int j = 0; j < v_samples; ++j) {
        const double u = u_params[static_cast<size_t>(i)];
        const double v = v_params[static_cast<size_t>(j)];
        const double nu = std::clamp(u, du.min + nudge_u, du.max - nudge_u);
        const double nv = std::clamp(v, dv.min + nudge_v, dv.max - nudge_v);
        ON_3dPoint pt;
        ON_3dVector su, sv;
        if (!surface_.Ev1Der(u, v, pt, su, sv)) return refuse(kDegenerateReason);
        const double e = su * su, g = sv * sv, f = su * sv;
        const double denom = e * g - f * f;
        const bool degenerate = !(e > 0.0) || !(g > 0.0) || !(denom > 1e-16 * e * g) ||
                                !(std::min(e, g) > 1e-16 * std::max(e, g));
        const ON_3dVector n = degenerate ? NormalAt(nu, nv) : NormalAt(u, v);
        if (degenerate) {
          const bool on_boundary = (i == 0 || i == u_samples - 1 || j == 0 || j == v_samples - 1);
          if (!on_boundary) return refuse(kDegenerateReason);
          for (const PoleSample& q : poles) {
            if (q.p.DistanceTo(pt) <= tol && ON_DotProduct(q.n, n) < 1.0 - 1e-6) return refuse(kDegenerateReason);
          }
          poles.push_back({pt, n});
        }
        offset_pts[static_cast<size_t>(i)][static_cast<size_t>(j)] = PointAt(u, v) + distance * n;
      }
    }
  } catch (const std::runtime_error&) {
    return refuse(kDegenerateReason);
  }

  // Seam closure. A surface closed in a direction (opposite domain edges
  // coincide, with the same normal and the same first derivative across the
  // seam) has a CLOSED offset locus too, but the clamped non-periodic
  // least-squares fit below treats the two seam rows independently, so
  // positions agree only to the fit error and the tangent not at all. When
  // the sampled locus is closed in a direction, the fitted control net is
  // forced closed there (end control points identical, and, when the base is
  // first-derivative continuous across the seam, the end tangents equal), and
  // the verification below measures the surface actually returned.
  //   level 0: not closed; 1: positions closed; 2: positions and tangent closed.
  auto seam_level = [&](int dir) {
    const int count = dir == 0 ? u_samples : v_samples;
    const int other = dir == 0 ? v_samples : u_samples;
    if (count < 3) return 0;
    bool pos_closed = true, tangent_closed = true;
    for (int k = 0; k < other && pos_closed; ++k) {
      const ON_3dPoint a = dir == 0 ? offset_pts.front()[static_cast<size_t>(k)] : offset_pts[static_cast<size_t>(k)].front();
      const ON_3dPoint b = dir == 0 ? offset_pts.back()[static_cast<size_t>(k)] : offset_pts[static_cast<size_t>(k)].back();
      if (a.DistanceTo(b) > 0.05 * tol) pos_closed = false;
    }
    if (!pos_closed) return 0;
    for (int k = 0; k < other && tangent_closed; ++k) {
      const double ou = dir == 0 ? du.min : u_params[static_cast<size_t>(k)];
      const double ov = dir == 0 ? v_params[static_cast<size_t>(k)] : dv.min;
      const double eu = dir == 0 ? du.max : u_params[static_cast<size_t>(k)];
      const double ev = dir == 0 ? v_params[static_cast<size_t>(k)] : dv.max;
      ON_3dPoint p0, p1;
      ON_3dVector a0, b0, a1, b1;
      if (!surface_.Ev1Der(ou, ov, p0, a0, b0) || !surface_.Ev1Der(eu, ev, p1, a1, b1)) return 1;
      const ON_3dVector t0 = dir == 0 ? a0 : b0;
      const ON_3dVector t1 = dir == 0 ? a1 : b1;
      const ON_3dVector n0 = dir == 0 ? b0 : a0;
      const ON_3dVector n1 = dir == 0 ? b1 : a1;
      const double scale = std::max({t0.Length(), t1.Length(), 1e-300});
      if ((t0 - t1).Length() > 1e-6 * scale) tangent_closed = false;
      // The cross-seam derivative (along the seam) must also match: it is
      // the same curve, so it does whenever positions agree; checked anyway.
      const double scale2 = std::max({n0.Length(), n1.Length(), 1e-300});
      if ((n0 - n1).Length() > 1e-6 * scale2) tangent_closed = false;
    }
    return tangent_closed ? 2 : 1;
  };
  const int seam_u = seam_level(0);
  const int seam_v = seam_level(1);
  auto close_seam = [&](ON_NurbsSurface& surf, int dir, int level) {
    const int count = dir == 0 ? surf.CVCount(0) : surf.CVCount(1);
    const int other = dir == 0 ? surf.CVCount(1) : surf.CVCount(0);
    if (level == 0 || count < 4) return;
    const bool tangent = level == 2 && (dir == 0 ? DegreeU() : DegreeV()) >= 2;
    for (int k = 0; k < other; ++k) {
      auto get = [&](int idx) {
        ON_3dPoint q;
        if (dir == 0) surf.GetCV(idx, k, q); else surf.GetCV(k, idx, q);
        return q;
      };
      auto set = [&](int idx, const ON_3dPoint& q) {
        if (dir == 0) surf.SetCV(idx, k, q); else surf.SetCV(k, idx, q);
      };
      const ON_3dPoint a = get(0), b = get(count - 1);
      const ON_3dPoint m = ON_3dPoint(0.5 * (a.x + b.x), 0.5 * (a.y + b.y), 0.5 * (a.z + b.z));
      set(0, m);
      set(count - 1, m);
      if (tangent) {
        const ON_3dPoint a1 = get(1), b1 = get(count - 2);
        const ON_3dVector t = 0.5 * ((a1 - a) + (b - b1));
        set(1, m + t);
        set(count - 2, m - t);
      }
    }
  };

  const int u_degree = DegreeU();
  const int v_degree = DegreeV();
  const int u_order = u_degree + 1;
  const int v_order = v_degree + 1;
  const int max_cv_u = u_samples;
  const int max_cv_v = v_samples;
  int cv_u = std::clamp(CVCountU(), u_degree + 1, max_cv_u);
  int cv_v = std::clamp(CVCountV(), v_degree + 1, max_cv_v);

  double best_worst = std::numeric_limits<double>::infinity();
  for (;;) {
    const std::vector<double> u_knot = ClampedUniformKnots(cv_u, u_order, du.min, du.max);
    const std::vector<double> v_knot = ClampedUniformKnots(cv_v, v_order, dv.min, dv.max);

    // Stage 1: fit every sample row (fixed v_j) in U -> temp[j][i].
    std::vector<std::vector<ON_3dPoint>> temp(static_cast<size_t>(v_samples));
    std::vector<ON_3dPoint> row(static_cast<size_t>(u_samples));
    bool ok = true;
    for (int j = 0; j < v_samples && ok; ++j) {
      for (int i = 0; i < u_samples; ++i) row[static_cast<size_t>(i)] = offset_pts[static_cast<size_t>(i)][static_cast<size_t>(j)];
      ok = FitRowLeastSquares(row, u_params, u_knot, u_order, cv_u, temp[static_cast<size_t>(j)]);
    }

    ON_NurbsSurface result;
    if (ok && result.Create(3, false, u_order, v_order, cv_u, cv_v)) {
      for (int k = 0; k < result.KnotCount(0); ++k) result.SetKnot(0, k, u_knot[static_cast<size_t>(k)]);
      for (int k = 0; k < result.KnotCount(1); ++k) result.SetKnot(1, k, v_knot[static_cast<size_t>(k)]);
      // Stage 2: fit every column of intermediate points (fixed i) in V.
      std::vector<ON_3dPoint> column(static_cast<size_t>(v_samples)), fitted;
      for (int i = 0; i < cv_u && ok; ++i) {
        for (int j = 0; j < v_samples; ++j) column[static_cast<size_t>(j)] = temp[static_cast<size_t>(j)][static_cast<size_t>(i)];
        ok = FitRowLeastSquares(column, v_params, v_knot, v_order, cv_v, fitted);
        if (ok) for (int j = 0; j < cv_v; ++j) result.SetCV(i, j, fitted[static_cast<size_t>(j)]);
      }
    } else {
      ok = false;
    }

    if (ok) {
      close_seam(result, 0, seam_u);
      close_seam(result, 1, seam_v);
    }

    if (ok && result.IsValid()) {
      // Measured on a grid TWICE as fine as the fit samples and offset by
      // half a step (Rebuild()'s own technique, surface_edit.cpp above),
      // deliberately never landing on a point the fit itself saw: with
      // `cv_u`/`cv_v` approaching `max_cv_u`/`max_cv_v` the row/column fit
      // increasingly INTERPOLATES the fit grid, which would make a
      // deviation check measured only at those same points trivially near
      // zero regardless of how well the fit actually resolves the true
      // offset locus BETWEEN samples - the exact self-referential mistake
      // this independent grid avoids. Each verification point's own
      // "true" value is recomputed directly from `PointAt`/`NormalAt` at
      // that point (not looked up in `offset_pts`, which only holds the
      // fit grid's own values), so this genuinely checks the fitted
      // surface against the true offset formula, not against the sample
      // grid.
      NurbsSurface fitted_surface;
      fitted_surface.surface_ = result;
      const int verify_u = 2 * u_samples;
      const int verify_v = 2 * v_samples;
      double worst = 0.0;
      for (int i = 0; i < verify_u; ++i) {
        const double u = std::clamp(du.min + u_range * (i + 0.5) / verify_u, du.min + nudge_u, du.max - nudge_u);
        for (int j = 0; j < verify_v; ++j) {
          const double v = std::clamp(dv.min + v_range * (j + 0.5) / verify_v, dv.min + nudge_v, dv.max - nudge_v);
          const Point3d expected = PointAt(u, v) + distance * NormalAt(u, v);
          worst = std::max(worst, fitted_surface.PointAt(u, v).DistanceTo(expected));
        }
      }
      if (worst <= tol) {
        out.surface_ = result;
        if (achieved_deviation != nullptr) *achieved_deviation = worst;
        return Result::Ok;
      }
      best_worst = std::min(best_worst, worst);
    }

    if (cv_u >= max_cv_u && cv_v >= max_cv_v) {
      return refuse("tolerance not reachable: the offset needs more control points than the sample-grid ceiling (" +
                    std::to_string(max_cv_u) + " x " + std::to_string(max_cv_v) +
                    ") can resolve at the requested tolerance " + std::to_string(tol) +
                    "; the best fit reached a deviation of " + std::to_string(best_worst) +
                    ". No offset is returned rather than an out-of-tolerance fit");
    }
    cv_u = std::min(cv_u * 2, max_cv_u);
    cv_v = std::min(cv_v * 2, max_cv_v);
  }
}

namespace {

// Brings `cand` - an exact analytic offset built on the primitive's own
// canonical parametrization - onto `base`'s parametrization: `base`'s
// domain in both directions, with each direction reversed if that makes
// cand(u,v) == base(u,v) + d * N_base(u,v). Returns true only when, on an
// interior probe grid, the worst distance from cand(u,v) to that locus is
// at most `tol` (otherwise `cand` is not a faithful offset of `base` at the
// same parameters - wrong frame, transposed roles, or a parametrization the
// primitive form does not share - and the caller must not call it exact).
bool AlignOffsetToBase(const NurbsSurface& base, double d, NurbsSurface& cand, double tol, double& measured) {
  measured = std::numeric_limits<double>::infinity();
  const Interval du = base.Domain(0);
  const Interval dv = base.Domain(1);
  if (cand.SetDomain(0, du.min, du.max) == Result::Failed) return false;
  if (cand.SetDomain(1, dv.min, dv.max) == Result::Failed) return false;

  constexpr int kN = 7;
  std::vector<Point3d> target;
  std::vector<double> us, vs;
  for (int i = 0; i < kN; ++i) {
    us.push_back(du.min + (du.max - du.min) * (0.06 + 0.88 * i / (kN - 1.0) + 0.003 * i));
    vs.push_back(dv.min + (dv.max - dv.min) * (0.06 + 0.88 * i / (kN - 1.0) + 0.002 * (kN - i)));
  }
  for (double u : us)
    for (double v : vs) target.push_back(base.PointAt(u, v) + d * base.NormalAt(u, v));

  auto deviation = [&](const NurbsSurface& s, bool ru, bool rv) {
    double worst = 0.0;
    size_t k = 0;
    for (double u : us) {
      for (double v : vs) {
        const double uu = ru ? du.min + du.max - u : u;
        const double vv = rv ? dv.min + dv.max - v : v;
        worst = std::max(worst, s.PointAt(uu, vv).DistanceTo(target[k++]));
      }
    }
    return worst;
  };

  double best = std::numeric_limits<double>::infinity();
  bool best_ru = false, best_rv = false;
  for (int m = 0; m < 4; ++m) {
    const bool ru = (m & 1) != 0, rv = (m & 2) != 0;
    const double w = deviation(cand, ru, rv);
    if (w < best) { best = w; best_ru = ru; best_rv = rv; }
  }
  if (!(best <= tol)) return false;
  // Reverse() negates the domain ([a,b] -> [-b,-a]); put it back on the
  // base's (a linear remap, so the reversed sense is kept).
  if (best_ru) {
    if (cand.Reverse(0) != Result::Ok || cand.SetDomain(0, du.min, du.max) == Result::Failed) return false;
  }
  if (best_rv) {
    if (cand.Reverse(1) != Result::Ok || cand.SetDomain(1, dv.min, dv.max) == Result::Failed) return false;
  }
  // Re-measured on the surface actually returned (not assumed), on a grid
  // far denser than the 7x7 orientation probe and offset from it: the
  // recognition of "this is a sphere/cylinder/..." is only within `tol`, so
  // the true deviation of the closed-form candidate from base + d*N_base is
  // whatever this grid finds, and that number is what the caller reports.
  // Half-cell offsets keep the grid off a parametrization's poles.
  const int nu = std::clamp(8 * std::max(base.CVCountU(), cand.CVCountU()), 65, 201);
  const int nv = std::clamp(8 * std::max(base.CVCountV(), cand.CVCountV()), 65, 201);
  double worst = 0.0;
  try {
    for (int i = 0; i < nu; ++i) {
      const double u = du.min + (du.max - du.min) * (i + 0.5) / nu;
      for (int j = 0; j < nv; ++j) {
        const double v = dv.min + (dv.max - dv.min) * (j + 0.5) / nv;
        worst = std::max(worst, cand.PointAt(u, v).DistanceTo(base.PointAt(u, v) + d * base.NormalAt(u, v)));
      }
    }
  } catch (const std::runtime_error&) {
    return false;
  }
  measured = worst;
  return worst <= tol;
}

}  // namespace

Result NurbsSurface::Offset(double distance, NurbsSurface& out, double tolerance, OffsetInfo* info) const {
  if (!ON_IsValid(distance)) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::Offset: distance must be finite");
  }
  OffsetInfo local;
  OffsetInfo& report = info != nullptr ? *info : local;
  report = OffsetInfo{};

  ON_BoundingBox bbox;
  surface_.GetBoundingBox(bbox, false);
  const double tol = tolerance > 0.0 ? tolerance : dino8::kernel::tolerance::DistanceForSize(bbox.Diagonal().Length());
  report.tolerance_used = tol;

  if (distance == 0.0) {
    out.surface_ = surface_;
    report.exact = true;
    return Result::Ok;
  }

  // The same five families OffsetAnalytic() constructs exactly, recognised
  // with the same tolerance it will use.
  const bool analytic = surface_.IsPlanar(nullptr, tol) || surface_.IsSphere(nullptr, tol) ||
                        surface_.IsCylinder(nullptr, tol) || surface_.IsCone(nullptr, tol) ||
                        surface_.IsTorus(nullptr, tol);
  bool analytic_built_but_unusable = false;
  if (analytic) {
    // IsSphere/IsCylinder/IsCone/IsTorus accept either isoparametric order,
    // but OffsetAnalytic() builds the primitive's own canonical form (u
    // around, v along the axis / latitude) with that form's own u/v
    // direction and domain. So the construction is tried on the surface as
    // given and on its transpose, and each candidate is brought onto the
    // INPUT's parametrization (domain, per-direction sense) and CHECKED
    // pointwise against base(u,v) + d*N_base(u,v). Only a candidate that
    // passes is returned as exact; the sign of `distance` therefore always
    // means "along the INPUT's own normal".
    for (int attempt = 0; attempt < 2; ++attempt) {
      NurbsSurface work = *this;
      if (attempt == 1) work.Transpose();
      NurbsSurface candidate;
      // Transposing flips the normal (N = Su x Sv), so "along the input's
      // normal" is along the transposed work copy's negated normal.
      const double work_distance = attempt == 1 ? -distance : distance;
      if (work.OffsetAnalytic(work_distance, candidate, tol) != Result::Ok) continue;
      analytic_built_but_unusable = true;
      double measured = 0.0;
      if (!AlignOffsetToBase(work, work_distance, candidate, tol, measured)) continue;
      if (attempt == 1) candidate.Transpose();
      out.surface_ = candidate.surface_;
      // Numerical noise of a rational closed form is not a deviation;
      // anything above it is the measured gap between the input and the
      // primitive it was recognised as, and is reported, not hidden.
      const double noise = 1e-11 * (bbox.Diagonal().Length() + std::fabs(distance));
      report.exact = measured <= noise;
      report.achieved_deviation = report.exact ? 0.0 : measured;
      return Result::Ok;
    }
    if (!analytic_built_but_unusable) {
      report.refusal_reason =
          "recognised plane/sphere/cylinder/cone/torus: the exact offset was refused (offset radius would "
          "reach zero or below, a torus would become a self-intersecting spindle, a cone patch straddles its "
          "apex, or a partial patch straddles a circle parametrization seam); no fitted substitute is returned "
          "for an analytic surface whose true offset folds through its own axis";
      return Result::Failed;
    }
    // The closed form was built but could not be matched to this surface's
    // own parametrization (one the canonical primitive form does not
    // share): fall through to the tolerance-controlled refit, which keeps
    // the input's parametrization and orientation by construction and
    // refuses a fold through the surface's own axis via its curvature
    // guard.
  }

  double deviation = 0.0;
  std::string why;
  if (OffsetRefit(distance, out, tol, &deviation, &why) == Result::Ok) {
    report.exact = false;
    report.achieved_deviation = deviation;
    return Result::Ok;
  }
  report.refusal_reason = "freeform offset refused: " + why;
  return Result::Failed;
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

Result NurbsSurface::MatchEdgeToCurve(int fixed_direction, bool at_min, const NurbsCurve& target,
                                      MatchContinuity continuity, const NurbsCurve* cross_derivative,
                                      const NurbsCurve* cross_second_derivative, MatchEdgeReport* report) {
  if (fixed_direction != 0 && fixed_direction != 1) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::MatchEdgeToCurve: direction must be 0 (U) or 1 (V)");
  }
  const int rows_needed = continuity == MatchContinuity::Position ? 1 : continuity == MatchContinuity::Tangent ? 2 : 3;
  const int edge_dir = 1 - fixed_direction;
  const ON_NurbsSurface& s = surface_;
  if (!s.IsClamped(0, 2) || !s.IsClamped(1, 2)) return Result::Failed;
  ON_NurbsCurve c = target.raw();
  if (c.Dimension() != 3 || c.Order() < 2 || c.CVCount() < c.Order() || !c.IsClamped(2)) return Result::Failed;
  // A prescribed 3D derivative field against a rational row would need
  // the field times the weight function as a spline (a polynomial
  // product) - not done here; fail closed (see the header).
  if (rows_needed >= 2 && (s.IsRational() || c.IsRational())) return Result::Failed;

  const ON_Interval c_dom = c.Domain();
  const ON_Interval s_edge = s.Domain(edge_dir);
  const double knot_tol = 1e-12 * std::max(1.0, s_edge.Length());

  // Explicit fields: 3D, non-rational, clamped, on the target's domain.
  ON_NurbsCurve d, e;
  bool have_d = false, have_e = false;
  auto take_field = [&](const NurbsCurve* field, ON_NurbsCurve& out) {
    const ON_NurbsCurve& f = field->raw();
    if (f.Dimension() != 3 || f.IsRational() || f.Order() < 2 || f.CVCount() < f.Order() || !f.IsClamped(2)) return false;
    const double dom_tol = 1e-12 * std::max(1.0, c_dom.Length());
    if (std::abs(f.Domain().Min() - c_dom.Min()) > dom_tol || std::abs(f.Domain().Max() - c_dom.Max()) > dom_tol) return false;
    out = f;
    return true;
  };
  if (rows_needed >= 2 && cross_derivative) { if (!take_field(cross_derivative, d)) return Result::Failed; have_d = true; }
  if (rows_needed >= 3 && cross_second_derivative) { if (!take_field(cross_second_derivative, e)) return Result::Failed; have_e = true; }

  // Orient the target to run the way this edge does (corner distances,
  // the same rule MatchEdge() applies to a surface edge), then put it
  // and the explicit fields on this edge's domain - one affine map for
  // all of them, so D(t)/E(t) keep pairing with C(t).
  const double at = at_min ? s.Domain(fixed_direction).Min() : s.Domain(fixed_direction).Max();
  auto s_corner = [&](double ee) { return fixed_direction == 0 ? s.PointAt(at, ee) : s.PointAt(ee, at); };
  const double straight = s_corner(s_edge.Min()).DistanceTo(c.PointAt(c_dom.Min())) + s_corner(s_edge.Max()).DistanceTo(c.PointAt(c_dom.Max()));
  const double crossed = s_corner(s_edge.Min()).DistanceTo(c.PointAt(c_dom.Max())) + s_corner(s_edge.Max()).DistanceTo(c.PointAt(c_dom.Min()));
  const bool reversed = crossed < straight;
  for (ON_NurbsCurve* f : {&c, have_d ? &d : nullptr, have_e ? &e : nullptr}) {
    if (!f) continue;
    if (reversed && !f->Reverse()) return Result::Failed;
    if (!f->SetDomain(s_edge.Min(), s_edge.Max())) return Result::Failed;
  }

  // Default fields: this surface's own pre-edit cross derivatives along
  // the edge, exact from its rows (textbook clamped end-derivative
  // formulas, as in MatchEdge()), as curves on the edge's own basis. The
  // cross direction is normalized to "edge at min" first so both are
  // the derivatives pointing into the surface.
  if (rows_needed >= 2 && (!have_d || (rows_needed >= 3 && !have_e))) {
    ON_NurbsSurface sn = s;
    if (!at_min && !sn.Reverse(fixed_direction)) return Result::Failed;
    const int p = sn.Degree(fixed_direction);
    const int n = sn.CVCount(edge_dir);
    const std::vector<ON_4dPoint> r0 = HomogeneousRow(sn, fixed_direction, 0), r1 = HomogeneousRow(sn, fixed_direction, 1);
    const double ds1 = TextbookKnot(sn, fixed_direction, p + 1) - TextbookKnot(sn, fixed_direction, 1);
    auto row_curve = [&](const std::vector<ON_4dPoint>& vals, ON_NurbsCurve& out) {
      out.Create(3, false, sn.Order(edge_dir), n);
      for (int k = 0; k < out.KnotCount(); ++k) out.SetKnot(k, sn.Knot(edge_dir, k));
      for (int i = 0; i < n; ++i) out.SetCV(i, ON_3dPoint(vals[static_cast<size_t>(i)].x, vals[static_cast<size_t>(i)].y, vals[static_cast<size_t>(i)].z));
    };
    if (!have_d) {
      std::vector<ON_4dPoint> dv(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i) dv[static_cast<size_t>(i)] = Scale4(Sub4(r1[static_cast<size_t>(i)], r0[static_cast<size_t>(i)]), p / ds1);
      row_curve(dv, d);
      have_d = true;
    }
    if (rows_needed >= 3 && !have_e) {
      std::vector<ON_4dPoint> ev(static_cast<size_t>(n), ON_4dPoint(0.0, 0.0, 0.0, 0.0));
      if (p >= 2) {
        const std::vector<ON_4dPoint> r2 = HomogeneousRow(sn, fixed_direction, 2);
        const double ds2 = TextbookKnot(sn, fixed_direction, p + 2) - TextbookKnot(sn, fixed_direction, 2);
        for (int i = 0; i < n; ++i) {
          const ON_4dPoint inner = Sub4(Scale4(Sub4(r2[static_cast<size_t>(i)], r1[static_cast<size_t>(i)]), 1.0 / ds2),
                                        Scale4(Sub4(r1[static_cast<size_t>(i)], r0[static_cast<size_t>(i)]), 1.0 / ds1));
          ev[static_cast<size_t>(i)] = Scale4(inner, p * (p - 1.0) / ds1);
        }
      }
      row_curve(ev, e);
      have_e = true;
    }
  }

  // One shared basis for the target and the fields in use.
  if (have_d && !UnifyCurves(c, d, knot_tol)) return Result::Failed;
  if (have_e && !UnifyCurves(c, e, knot_tol)) return Result::Failed;
  if (have_d && have_e && !UnifyCurves(c, d, knot_tol)) return Result::Failed;
  for (const ON_NurbsCurve* f : {have_d ? &d : nullptr, have_e ? &e : nullptr}) {
    if (!f) continue;
    if (f->Degree() != c.Degree() || f->CVCount() != c.CVCount() || f->KnotCount() != c.KnotCount()) return Result::Failed;
    for (int k = 0; k < c.KnotCount(); ++k) {
      if (std::abs(f->Knot(k) - c.Knot(k)) > knot_tol) return Result::Failed;
    }
  }

  // The ribbon T(e, t) = C(e) - t D(e) + t^2/2 E(e), t in [0, 1], degree 2
  // in t: rows C, C - D/2, C - D + E/2 (so T_t(0) = -D, T_tt = E). For
  // Position neither derivative row is read; a plain translate keeps the
  // ribbon non-degenerate (homogeneous, so a rational target's weights
  // carry through unchanged).
  NurbsSurface ribbon;
  ON_NurbsSurface& t = ribbon.raw();
  const int n = c.CVCount();
  const bool rational = c.IsRational();
  if (!t.Create(3, rational, c.Order(), 3, n, 3)) return Result::Failed;
  for (int k = 0; k < c.KnotCount(); ++k) t.SetKnot(0, k, c.Knot(k));
  t.SetKnot(1, 0, 0.0);
  t.SetKnot(1, 1, 0.0);
  t.SetKnot(1, 2, 1.0);
  t.SetKnot(1, 3, 1.0);
  ON_BoundingBox bb;
  s.GetBoundingBox(bb, false);
  const ON_3dVector offset(0.0, 0.0, std::max(1.0, bb.Diagonal().Length()));
  for (int i = 0; i < n; ++i) {
    ON_4dPoint c4;
    c.GetCV(i, c4);
    if (!rational) c4.w = 1.0;
    ON_4dPoint r1, r2;
    if (rows_needed >= 2) {
      ON_3dPoint di, ei(0.0, 0.0, 0.0);
      d.GetCV(i, di);
      if (rows_needed >= 3) e.GetCV(i, ei);
      r1 = ON_4dPoint(c4.x - 0.5 * di.x, c4.y - 0.5 * di.y, c4.z - 0.5 * di.z, 1.0);
      r2 = ON_4dPoint(c4.x - di.x + 0.5 * ei.x, c4.y - di.y + 0.5 * ei.y, c4.z - di.z + 0.5 * ei.z, 1.0);
    } else {
      r1 = ON_4dPoint(c4.x + c4.w * offset.x, c4.y + c4.w * offset.y, c4.z + c4.w * offset.z, c4.w);
      r2 = ON_4dPoint(c4.x + 2.0 * c4.w * offset.x, c4.y + 2.0 * c4.w * offset.y, c4.z + 2.0 * c4.w * offset.z, c4.w);
    }
    if (rational) {
      t.SetCV(i, 0, c4);
      t.SetCV(i, 1, r1);
      t.SetCV(i, 2, r2);
    } else {
      t.SetCV(i, 0, ON_3dPoint(c4.x, c4.y, c4.z));
      t.SetCV(i, 1, ON_3dPoint(r1.x, r1.y, r1.z));
      t.SetCV(i, 2, ON_3dPoint(r2.x, r2.y, r2.z));
    }
  }

  // Exact rows, far-edge protection and the evaluate-after-edit
  // self-check all come from MatchEdge(); scale 1 makes its "-scale *
  // T_t" exactly D and its "scale^2 * T_tt" exactly E. The ribbon was
  // oriented above, so MatchEdge() sees it straight; report the real
  // orientation decision.
  MatchEdgeReport local;
  const Result r = MatchEdge(fixed_direction, at_min, ribbon, 1, true, continuity, &local, 1.0);
  local.target_edge_reversed = reversed;
  if (report) *report = local;
  return r;
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
  ON_NurbsCurve d0_fwd = d0;
  ON_NurbsCurve c1_rev = c1_fwd, d0_rev = d0_fwd, d1_rev = d1_fwd;
  // ON_NurbsCurve::Reverse() does not preserve the [0, 1] domain just
  // set above (confirmed by a debug run: its own domain ends up
  // negated, e.g. [-1, 0]) - re-normalize immediately so PointAt(0)/
  // PointAt(1) below correctly mean "new start"/"new end".
  c1_rev.Reverse();
  c1_rev.SetDomain(0.0, 1.0);
  d0_rev.Reverse();
  d0_rev.SetDomain(0.0, 1.0);
  d1_rev.Reverse();
  d1_rev.SetDomain(0.0, 1.0);

  // Try all 8 orientations of (top, left, right) against the fixed
  // `bottom` reference (the one curve whose direction defines P00/P10
  // unambiguously) and keep whichever best closes all 4 corners. A
  // caller that chains 4 arbitrarily-picked curves into a loop (e.g.
  // dino8-app's own NetworkSrf) has no guarantee any of the other 3
  // curves' own stored directions happen to already match the bottom-
  // to-top / left-to-right convention this method documents.
  const ON_3dPoint p00 = c0.PointAt(0.0), p10 = c0.PointAt(1.0);
  double best_gap = std::numeric_limits<double>::infinity();
  int best_c1 = 0, best_d0 = 0, best_d1 = 0;  // 0 = forward, 1 = reversed
  for (int ci = 0; ci < 2; ++ci) {
    const ON_NurbsCurve& c1 = ci == 0 ? c1_fwd : c1_rev;
    for (int d0i = 0; d0i < 2; ++d0i) {
      const ON_NurbsCurve& d0_try = d0i == 0 ? d0_fwd : d0_rev;
      for (int d1i = 0; d1i < 2; ++d1i) {
        const ON_NurbsCurve& d1 = d1i == 0 ? d1_fwd : d1_rev;
        const double gap = d0_try.PointAt(0.0).DistanceTo(p00) + d1.PointAt(0.0).DistanceTo(p10) +
                            d0_try.PointAt(1.0).DistanceTo(c1.PointAt(0.0)) + d1.PointAt(1.0).DistanceTo(c1.PointAt(1.0));
        if (gap < best_gap) { best_gap = gap; best_c1 = ci; best_d0 = d0i; best_d1 = d1i; }
      }
    }
  }
  if (out_corner_gap) *out_corner_gap = best_gap;
  if (!(best_gap <= 4.0 * tolerance)) return Result::Failed;
  ON_NurbsCurve c1 = best_c1 == 0 ? c1_fwd : c1_rev;
  d0 = best_d0 == 0 ? d0_fwd : d0_rev;
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

Result NurbsSurface::CoonsPatchTriangular(const NurbsCurve& a, const NurbsCurve& b, const NurbsCurve& c, NurbsSurface& out,
                                          double tolerance, double* out_corner_gap) {
  // `a` = bottom (A -> B). The collapsed "left" edge sits at A, so the
  // curve of {b, c} that starts (or, reversed, ends) at A is "top" and the
  // one that meets B is "right". Which of b/c is which is unknown to the
  // caller, so try both and keep the better-closing success.
  const ON_3dPoint A = a.raw().PointAt(a.raw().Domain().Min());
  const NurbsCurve point_edge = NurbsCurve::FromControlPoints({Point3d(A.x, A.y, A.z), Point3d(A.x, A.y, A.z)}, 1);
  double best_gap = std::numeric_limits<double>::infinity();
  double best_success_gap = std::numeric_limits<double>::infinity();
  bool have = false;
  NurbsSurface best;
  for (int order = 0; order < 2; ++order) {
    const NurbsCurve& right = order == 0 ? b : c;
    const NurbsCurve& top = order == 0 ? c : b;
    NurbsSurface candidate;
    double gap = std::numeric_limits<double>::infinity();
    const Result r = CoonsPatch(a, top, point_edge, right, candidate, tolerance, &gap);
    best_gap = std::min(best_gap, gap);
    if (r == Result::Ok && gap < best_success_gap) {
      best_success_gap = gap;
      best = candidate;
      have = true;
    }
  }
  if (out_corner_gap) *out_corner_gap = best_gap;
  if (!have) return Result::Failed;
  out = best;
  return Result::Ok;
}

namespace {

namespace patch_fit_detail {

struct Plane3 {
  ON_3dPoint o;
  ON_3dVector x, y, z;
};

// Symmetric 3x3 eigen-decomposition by cyclic Jacobi rotations. Columns of
// `vec` are the eigenvectors, `val` ascending.
inline void Eigen3(double a[3][3], double val[3], double vec[3][3]) {
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) vec[i][j] = i == j ? 1.0 : 0.0;
  for (int sweep = 0; sweep < 60; ++sweep) {
    const double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
    if (off < 1e-300) break;
    for (int p = 0; p < 2; ++p) {
      for (int q = p + 1; q < 3; ++q) {
        if (std::fabs(a[p][q]) < 1e-300) continue;
        const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
        const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
        const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
        for (int k = 0; k < 3; ++k) {
          const double akp = a[k][p], akq = a[k][q];
          a[k][p] = c * akp - s * akq;
          a[k][q] = s * akp + c * akq;
        }
        for (int k = 0; k < 3; ++k) {
          const double apk = a[p][k], aqk = a[q][k];
          a[p][k] = c * apk - s * aqk;
          a[q][k] = s * apk + c * aqk;
        }
        for (int k = 0; k < 3; ++k) {
          const double vkp = vec[k][p], vkq = vec[k][q];
          vec[k][p] = c * vkp - s * vkq;
          vec[k][q] = s * vkp + c * vkq;
        }
      }
    }
  }
  int idx[3] = {0, 1, 2};
  std::sort(idx, idx + 3, [&](int i, int j) { return a[i][i] < a[j][j]; });
  double v2[3][3];
  for (int c = 0; c < 3; ++c) {
    val[c] = a[idx[c]][idx[c]];
    for (int r = 0; r < 3; ++r) v2[r][c] = vec[r][idx[c]];
  }
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) vec[r][c] = v2[r][c];
}

// Standard (n + p + 1)-long clamped uniform knot vector on [a, b].
inline std::vector<double> PatchFitKnots(int n, int p, double a, double b) {
  std::vector<double> t(static_cast<size_t>(n + p + 1));
  const int interior = n - p - 1;
  for (int i = 0; i <= p; ++i) {
    t[static_cast<size_t>(i)] = a;
    t[static_cast<size_t>(n + i)] = b;
  }
  for (int i = 1; i <= interior; ++i) t[static_cast<size_t>(p + i)] = a + (b - a) * i / (interior + 1);
  return t;
}

// All n basis values at x (Cox-de Boor), plus the first nonzero index.
inline void BasisValues(const std::vector<double>& t, int n, int p, double x, double* out, int* first) {
  int span = p;
  while (span < n - 1 && x >= t[static_cast<size_t>(span + 1)]) ++span;
  double left[16], right[16], N[16];
  N[0] = 1.0;
  for (int j = 1; j <= p; ++j) {
    left[j] = x - t[static_cast<size_t>(span + 1 - j)];
    right[j] = t[static_cast<size_t>(span + j)] - x;
    double saved = 0.0;
    for (int r = 0; r < j; ++r) {
      const double denom = right[r + 1] + left[j - r];
      const double temp = denom != 0.0 ? N[r] / denom : 0.0;
      N[r] = saved + right[r + 1] * temp;
      saved = left[j - r] * temp;
    }
    N[j] = saved;
  }
  for (int j = 0; j <= p; ++j) out[j] = N[j];
  *first = span - p;
}

// In-place Cholesky solve of the SPD system A x = b (A is n x n row-major);
// false if a pivot is not positive.
inline bool CholeskySolve(std::vector<double>& A, std::vector<double>& b, int n) {
  for (int j = 0; j < n; ++j) {
    double d = A[static_cast<size_t>(j) * n + j];
    for (int k = 0; k < j; ++k) d -= A[static_cast<size_t>(j) * n + k] * A[static_cast<size_t>(j) * n + k];
    if (!(d > 0.0)) return false;
    d = std::sqrt(d);
    A[static_cast<size_t>(j) * n + j] = d;
    for (int i = j + 1; i < n; ++i) {
      double s = A[static_cast<size_t>(i) * n + j];
      for (int k = 0; k < j; ++k) s -= A[static_cast<size_t>(i) * n + k] * A[static_cast<size_t>(j) * n + k];
      A[static_cast<size_t>(i) * n + j] = s / d;
    }
  }
  for (int i = 0; i < n; ++i) {
    double s = b[static_cast<size_t>(i)];
    for (int k = 0; k < i; ++k) s -= A[static_cast<size_t>(i) * n + k] * b[static_cast<size_t>(k)];
    b[static_cast<size_t>(i)] = s / A[static_cast<size_t>(i) * n + i];
  }
  for (int i = n - 1; i >= 0; --i) {
    double s = b[static_cast<size_t>(i)];
    for (int k = i + 1; k < n; ++k) s -= A[static_cast<size_t>(k) * n + i] * b[static_cast<size_t>(k)];
    b[static_cast<size_t>(i)] = s / A[static_cast<size_t>(i) * n + i];
  }
  return true;
}

// Basis values and first two derivatives at x (Piegl & Tiller A2.3):
// ders[k][j] is the k-th derivative of basis function first + j. Derivatives
// above the degree are zero.
inline void BasisDerivs(const std::vector<double>& t, int n, int p, double x, double ders[3][16], int* first) {
  int span = p;
  while (span < n - 1 && x >= t[static_cast<size_t>(span + 1)]) ++span;
  double ndu[5][5], a[2][5], left[5], right[5];
  ndu[0][0] = 1.0;
  for (int j = 1; j <= p; ++j) {
    left[j] = x - t[static_cast<size_t>(span + 1 - j)];
    right[j] = t[static_cast<size_t>(span + j)] - x;
    double saved = 0.0;
    for (int r = 0; r < j; ++r) {
      ndu[j][r] = right[r + 1] + left[j - r];
      const double temp = ndu[j][r] != 0.0 ? ndu[r][j - 1] / ndu[j][r] : 0.0;
      ndu[r][j] = saved + right[r + 1] * temp;
      saved = left[j - r] * temp;
    }
    ndu[j][j] = saved;
  }
  for (int k = 0; k < 3; ++k)
    for (int j = 0; j < 16; ++j) ders[k][j] = 0.0;
  for (int j = 0; j <= p; ++j) ders[0][j] = ndu[j][p];
  for (int r = 0; r <= p; ++r) {
    int s1 = 0, s2 = 1;
    a[0][0] = 1.0;
    for (int k = 1; k <= 2 && k <= p; ++k) {
      double d = 0.0;
      const int rk = r - k, pk = p - k;
      if (r >= k) {
        a[s2][0] = ndu[pk + 1][rk] != 0.0 ? a[s1][0] / ndu[pk + 1][rk] : 0.0;
        d = a[s2][0] * ndu[rk][pk];
      }
      const int j1 = rk >= -1 ? 1 : -rk;
      const int j2 = (r - 1 <= pk) ? k - 1 : p - r;
      for (int j = j1; j <= j2; ++j) {
        a[s2][j] = ndu[pk + 1][rk + j] != 0.0 ? (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][rk + j] : 0.0;
        d += a[s2][j] * ndu[rk + j][pk];
      }
      if (r <= pk) {
        a[s2][k] = ndu[pk + 1][r] != 0.0 ? -a[s1][k - 1] / ndu[pk + 1][r] : 0.0;
        d += a[s2][k] * ndu[r][pk];
      }
      ders[k][r] = d;
      std::swap(s1, s2);
    }
  }
  double factor = p;
  for (int k = 1; k <= 2 && k <= p; ++k) {
    for (int j = 0; j <= p; ++j) ders[k][j] *= factor;
    factor *= (p - k);
  }
  *first = span - p;
}

struct Datum {
  double x, y, h, w;
  bool boundary;
};

// A derivative of the height field prescribed at (x, y): kind 0 = h_x, 1 = h_y, 2 = h_xx, 3 = h_xy, 4 = h_yy.
struct DerivConstraint {
  double x, y, value, w;
  int kind;
};

struct FitResult {
  bool ok = false;
  int nu = 0, nv = 0;
  std::vector<double> c;  // control heights, c[i * nv + j]
  double max_dev = 0.0, rms_dev = 0.0, max_boundary_dev = 0.0;
  double grad_err = 0.0, hess_err = 0.0;  // worst residual of the first / second derivative constraints
};

// Least-squares height fit h(x, y) with a (nu x nv) cubic net over [x0,x1] x [y0,y1].
// `cons`, if given, adds weighted derivative rows (see DerivConstraint).
inline FitResult FitHeights(const std::vector<Datum>& data, double x0, double x1, double y0, double y1, int nu, int nv, double lambda,
                            const std::vector<DerivConstraint>* cons = nullptr) {
  FitResult r;
  r.nu = nu;
  r.nv = nv;
  const int pu = std::min(3, nu - 1), pv = std::min(3, nv - 1);
  const std::vector<double> tu = PatchFitKnots(nu, pu, x0, x1), tv = PatchFitKnots(nv, pv, y0, y1);
  const int N = nu * nv;
  std::vector<double> A(static_cast<size_t>(N) * N, 0.0), b(static_cast<size_t>(N), 0.0);
  double wsum = 0.0;
  double bu[16], bv[16];
  // Row coefficients of one derivative constraint: coef[a * 16 + c] on control (fu + a, fv + c).
  auto constraint_row = [&](const DerivConstraint& k, double* coef, int* fu, int* fv) {
    double du[3][16], dv[3][16];
    BasisDerivs(tu, nu, pu, k.x, du, fu);
    BasisDerivs(tv, nv, pv, k.y, dv, fv);
    const int iu = (k.kind == 0) ? 1 : (k.kind == 2) ? 2 : (k.kind == 3) ? 1 : 0;
    const int iv = (k.kind == 1) ? 1 : (k.kind == 4) ? 2 : (k.kind == 3) ? 1 : 0;
    for (int a = 0; a <= pu; ++a)
      for (int c = 0; c <= pv; ++c) coef[a * 16 + c] = du[iu][a] * dv[iv][c];
  };
  if (cons) {
    double coef[16 * 16];
    for (const DerivConstraint& k : *cons) {
      int fu = 0, fv = 0;
      constraint_row(k, coef, &fu, &fv);
      for (int a = 0; a <= pu; ++a) {
        for (int c = 0; c <= pv; ++c) {
          const int row = (fu + a) * nv + (fv + c);
          b[static_cast<size_t>(row)] += k.w * coef[a * 16 + c] * k.value;
          for (int a2 = 0; a2 <= pu; ++a2)
            for (int c2 = 0; c2 <= pv; ++c2) A[static_cast<size_t>(row) * N + (fu + a2) * nv + (fv + c2)] += k.w * coef[a * 16 + c] * coef[a2 * 16 + c2];
        }
      }
    }
  }
  for (const Datum& d : data) {
    int fu = 0, fv = 0;
    BasisValues(tu, nu, pu, d.x, bu, &fu);
    BasisValues(tv, nv, pv, d.y, bv, &fv);
    wsum += d.w;
    for (int a = 0; a <= pu; ++a) {
      for (int c = 0; c <= pv; ++c) {
        const int row = (fu + a) * nv + (fv + c);
        const double ba = bu[a] * bv[c];
        b[static_cast<size_t>(row)] += d.w * ba * d.h;
        for (int a2 = 0; a2 <= pu; ++a2) {
          for (int c2 = 0; c2 <= pv; ++c2) {
            A[static_cast<size_t>(row) * N + (fu + a2) * nv + (fv + c2)] += d.w * ba * bu[a2] * bv[c2];
          }
        }
      }
    }
  }
  // Stiffness regularizer: squared second differences of the control heights
  // along both directions plus the mixed (twist) difference - it only decides
  // the surface where the data leave it free.
  const double lam = lambda * wsum / std::max(1, N);
  auto add_stencil = [&](const int idx[4], const double coef[4], int count) {
    for (int a = 0; a < count; ++a)
      for (int c = 0; c < count; ++c) A[static_cast<size_t>(idx[a]) * N + idx[c]] += lam * coef[a] * coef[c];
  };
  for (int i = 0; i < nu; ++i) {
    for (int j = 0; j < nv; ++j) {
      if (i + 2 < nu) {
        const int idx[4] = {i * nv + j, (i + 1) * nv + j, (i + 2) * nv + j, 0};
        const double co[4] = {1, -2, 1, 0};
        add_stencil(idx, co, 3);
      }
      if (j + 2 < nv) {
        const int idx[4] = {i * nv + j, i * nv + j + 1, i * nv + j + 2, 0};
        const double co[4] = {1, -2, 1, 0};
        add_stencil(idx, co, 3);
      }
      if (i + 1 < nu && j + 1 < nv) {
        const int idx[4] = {i * nv + j, (i + 1) * nv + j, i * nv + j + 1, (i + 1) * nv + j + 1};
        const double co[4] = {1, -1, -1, 1};
        add_stencil(idx, co, 4);
      }
    }
  }
  for (int i = 0; i < N; ++i) A[static_cast<size_t>(i) * N + i] += 1e-12 * (wsum / std::max(1, N)) + 1e-300;
  std::vector<double> sol = b;
  if (!CholeskySolve(A, sol, N)) return r;
  r.c = sol;
  double sq = 0.0;
  for (const Datum& d : data) {
    int fu = 0, fv = 0;
    BasisValues(tu, nu, pu, d.x, bu, &fu);
    BasisValues(tv, nv, pv, d.y, bv, &fv);
    double h = 0.0;
    for (int a = 0; a <= pu; ++a)
      for (int c = 0; c <= pv; ++c) h += bu[a] * bv[c] * sol[static_cast<size_t>((fu + a) * nv + (fv + c))];
    const double e = std::fabs(h - d.h);
    r.max_dev = std::max(r.max_dev, e);
    if (d.boundary) r.max_boundary_dev = std::max(r.max_boundary_dev, e);
    sq += e * e;
  }
  r.rms_dev = std::sqrt(sq / data.size());
  if (cons) {
    double coef[16 * 16];
    for (const DerivConstraint& k : *cons) {
      int fu = 0, fv = 0;
      constraint_row(k, coef, &fu, &fv);
      double v = 0.0;
      for (int a = 0; a <= pu; ++a)
        for (int c = 0; c <= pv; ++c) v += coef[a * 16 + c] * sol[static_cast<size_t>((fu + a) * nv + (fv + c))];
      const double e = std::fabs(v - k.value);
      if (k.kind <= 1) r.grad_err = std::max(r.grad_err, e);
      else r.hess_err = std::max(r.hess_err, e);
    }
  }
  r.ok = true;
  return r;
}

}  // namespace patch_fit_detail

// ---------------------------------------------------------------------------
// Edge continuity (G0/G1/G2) helpers shared by MeasureEdgeContinuity(), the
// continuity-aware FitPatch()/FitCurveNetwork() and LoftMatched().
// ---------------------------------------------------------------------------
namespace continuity_detail {

struct Jet {
  ON_3dPoint P;
  ON_3dVector Su, Sv, Suu, Suv, Svv;
};

inline bool EvalJet(const ON_NurbsSurface& ns, double u, double v, Jet& j) {
  return ns.Ev2Der(u, v, j.P, j.Su, j.Sv, j.Suu, j.Suv, j.Svv);
}

// Closest point of `s` to `p`: the surface's own grid search, then Newton
// iterations on the squared distance (clamped to the domain).
inline bool Project(const NurbsSurface& s, const ON_3dPoint& p, Jet& j) {
  const ON_NurbsSurface& ns = s.raw();
  const Point2d uv0 = s.ClosestPointParameter(p, 40, 40);
  const ON_Interval dU = ns.Domain(0), dV = ns.Domain(1);
  double u = uv0.x, v = uv0.y;
  Jet t;
  for (int it = 0; it < 20; ++it) {
    if (!EvalJet(ns, u, v, t)) return false;
    const ON_3dVector r = p - t.P;
    double a11 = t.Su * t.Su, a12 = t.Su * t.Sv, a22 = t.Sv * t.Sv;
    const double f11 = a11 - r * t.Suu, f12 = a12 - r * t.Suv, f22 = a22 - r * t.Svv;
    if (f11 > 0.0 && f11 * f22 - f12 * f12 > 0.0) { a11 = f11; a12 = f12; a22 = f22; }
    const double b1 = r * t.Su, b2 = r * t.Sv;
    const double det = a11 * a22 - a12 * a12;
    if (!(std::fabs(det) > 1e-300)) break;
    const double du = (b1 * a22 - b2 * a12) / det, dv = (a11 * b2 - a12 * b1) / det;
    const double nu = std::min(dU.Max(), std::max(dU.Min(), u + du));
    const double nv = std::min(dV.Max(), std::max(dV.Min(), v + dv));
    const double step = std::fabs(nu - u) + std::fabs(nv - v);
    u = nu;
    v = nv;
    if (step < 1e-14 * (dU.Length() + dV.Length())) break;
  }
  return EvalJet(ns, u, v, j);
}

inline bool UnitNormal(const Jet& j, ON_3dVector& n) {
  n = ON_CrossProduct(j.Su, j.Sv);
  return n.Unitize();
}

struct Forms {
  double E, F, G, L, M, N;
};

inline Forms FormsOf(const Jet& j, const ON_3dVector& n) {
  return Forms{j.Su * j.Su, j.Su * j.Sv, j.Sv * j.Sv, j.Suu * n, j.Suv * n, j.Svv * n};
}

// Normal curvature (against `n`) along the unit tangent-plane direction nearest `w`.
inline double NormalCurvatureAlong(const Jet& j, const ON_3dVector& n, ON_3dVector w) {
  w -= (w * n) * n;
  if (!w.Unitize()) return std::numeric_limits<double>::quiet_NaN();
  const Forms f = FormsOf(j, n);
  const double det = f.E * f.G - f.F * f.F;
  if (!(std::fabs(det) > 1e-300)) return std::numeric_limits<double>::quiet_NaN();
  const double r1 = w * j.Su, r2 = w * j.Sv;
  const double a = (r1 * f.G - r2 * f.F) / det, b = (f.E * r2 - f.F * r1) / det;
  return a * a * f.L + 2.0 * a * b * f.M + b * b * f.N;
}

inline double GaussianOf(const Jet& j, const ON_3dVector& n) {
  const Forms f = FormsOf(j, n);
  return (f.L * f.N - f.M * f.M) / (f.E * f.G - f.F * f.F);
}

// Folds the continuity of `nw` against `adj` at the probe point `p` (near the
// shared edge, running along `edge_dir`) into `m`. False if either surface
// cannot be evaluated or is singular there.
inline bool MeasureAt(const NurbsSurface& nw, const NurbsSurface& adj, const ON_3dPoint& p, const ON_3dVector& edge_dir,
                      NurbsSurface::EdgeContinuityMeasure& m) {
  Jet ja, jn;
  if (!Project(adj, p, ja) || !Project(nw, p, jn)) return false;
  ON_3dVector na, nn;
  if (!UnitNormal(ja, na) || !UnitNormal(jn, nn)) return false;
  if (na * nn < 0.0) nn = -nn;  // compare tangent planes, not orientations
  const double angle = std::atan2(ON_CrossProduct(na, nn).Length(), na * nn);
  ON_3dVector w = ON_CrossProduct(na, edge_dir);
  if (!w.Unitize()) return false;
  const double ka = NormalCurvatureAlong(ja, na, w), kn = NormalCurvatureAlong(jn, nn, w);
  const double Ka = GaussianOf(ja, na), Kn = GaussianOf(jn, nn);
  if (!std::isfinite(ka) || !std::isfinite(kn) || !std::isfinite(Ka) || !std::isfinite(Kn)) return false;
  ++m.samples;
  m.max_position_gap = std::max(m.max_position_gap, ja.P.DistanceTo(jn.P));
  m.max_tangent_angle = std::max(m.max_tangent_angle, angle);
  m.max_cross_curvature_diff = std::max(m.max_cross_curvature_diff, std::fabs(ka - kn));
  m.max_gaussian_diff = std::max(m.max_gaussian_diff, std::fabs(Ka - Kn));
  return true;
}

struct PlaneFrame {
  ON_3dPoint c;
  ON_3dVector X, Y, Z;
};

// First (and optionally second) derivatives of the adjacent surface seen as a
// height field over the fit plane, at jet `j`: out = {h_x, h_y, h_xx, h_xy, h_yy}.
// Returns an empty string on success, else why it cannot be one.
inline std::string GraphDerivatives(const Jet& j, const PlaneFrame& f, bool second, double out[5]) {
  ON_3dVector n;
  if (!UnitNormal(j, n)) return "the adjacent surface is singular (no normal) at an edge sample";
  const double nz = n * f.Z;
  if (std::fabs(nz) < 0.2) {
    return "the adjacent surface's tangent plane is " + std::to_string(std::acos(std::min(1.0, std::fabs(nz))) * 180.0 / ON_PI) +
           " degrees from the patch's best-fit plane (limit 78.5): it cannot be continued by a height field over that plane";
  }
  const double xu = j.Su * f.X, xv = j.Sv * f.X, yu = j.Su * f.Y, yv = j.Sv * f.Y, hu = j.Su * f.Z, hv = j.Sv * f.Z;
  const double det = xu * yv - xv * yu;
  if (!(std::fabs(det) > 1e-300)) return "the adjacent surface's parameterization is singular at an edge sample";
  const double hx = (hu * yv - yu * hv) / det, hy = (xu * hv - xv * hu) / det;
  out[0] = hx;
  out[1] = hy;
  if (second) {
    const double quu = j.Suu * f.Z - hx * (j.Suu * f.X) - hy * (j.Suu * f.Y);
    const double quv = j.Suv * f.Z - hx * (j.Suv * f.X) - hy * (j.Suv * f.Y);
    const double qvv = j.Svv * f.Z - hx * (j.Svv * f.X) - hy * (j.Svv * f.Y);
    // J = [[xu, xv], [yu, yv]], Ji = J^-1; G = Ji^T Q Ji.
    const double i00 = yv / det, i01 = -xv / det, i10 = -yu / det, i11 = xu / det;
    const double g00 = i00 * (quu * i00 + quv * i10) + i10 * (quv * i00 + qvv * i10);
    const double g01 = i00 * (quu * i01 + quv * i11) + i10 * (quv * i01 + qvv * i11);
    const double g11 = i01 * (quu * i01 + quv * i11) + i11 * (quv * i01 + qvv * i11);
    out[2] = g00;
    out[3] = g01;
    out[4] = g11;
  }
  return std::string();
}

}  // namespace continuity_detail

}  // namespace

Result NurbsSurface::FitPatch(const std::vector<Point3d>& boundary_points, const std::vector<Point3d>& interior_points,
                              double tolerance, NurbsSurface& out, PatchFitReport* report, int max_count) {
  return FitPatch(boundary_points, interior_points, tolerance, out, report, max_count, std::vector<FitEdgeContinuity>());
}

Result NurbsSurface::FitPatch(const std::vector<Point3d>& boundary_points_in, const std::vector<Point3d>& interior_points,
                              double tolerance, NurbsSurface& out, PatchFitReport* report, int max_count,
                              const std::vector<FitEdgeContinuity>& edges) {
  using namespace patch_fit_detail;
  namespace cd = continuity_detail;
  if (!(tolerance > 0.0) || !std::isfinite(tolerance)) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::FitPatch: tolerance must be positive and finite");
  }
  if (max_count < 2 || max_count > 32) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::FitPatch: max_count must be in [2, 32]");
  }
  if (!edges.empty() && report == nullptr) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::FitPatch: a report is required when edge continuity is requested");
  }
  auto refuse = [&](const std::string& why) {
    if (report) {
      *report = PatchFitReport{};
      report->reason = why;
    }
    return Result::Failed;
  };
  // Edge samples are fitted as boundary data too (G0), then constrained.
  std::vector<Point3d> boundary_points = boundary_points_in;
  for (size_t e = 0; e < edges.size(); ++e) {
    const std::string tag = "edge " + std::to_string(e + 1) + ": ";
    if (edges[e].adjacent == nullptr) return refuse(tag + "no adjacent surface given");
    if (edges[e].points.size() < 8) return refuse(tag + "needs at least 8 ordered edge samples, " + std::to_string(edges[e].points.size()) + " given");
    if (!(edges[e].angle_tolerance > 0.0) || !(edges[e].curvature_tolerance > 0.0)) {
      throw std::invalid_argument("dino8::kernel::NurbsSurface::FitPatch: continuity tolerances must be positive");
    }
    if (!edges[e].adjacent->raw().IsValid()) return refuse(tag + "the adjacent surface is not a valid surface");
    boundary_points.insert(boundary_points.end(), edges[e].points.begin(), edges[e].points.end());
  }
  std::vector<Point3d> all = boundary_points;
  all.insert(all.end(), interior_points.begin(), interior_points.end());
  for (const Point3d& p : all) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
      throw std::invalid_argument("dino8::kernel::NurbsSurface::FitPatch: a data point has a non-finite coordinate");
    }
  }
  if (all.size() < 3) return Result::Failed;

  // Principal-component plane: normal = smallest-variance axis, x = largest.
  ON_3dVector centroid_sum(0, 0, 0);
  for (const Point3d& p : all) centroid_sum += ON_3dVector(p);
  const ON_3dPoint centre(centroid_sum.x / all.size(), centroid_sum.y / all.size(), centroid_sum.z / all.size());
  double cov[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  for (const Point3d& p : all) {
    const double d[3] = {p.x - centre.x, p.y - centre.y, p.z - centre.z};
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) cov[i][j] += d[i] * d[j];
  }
  double val[3], vec[3][3];
  Eigen3(cov, val, vec);
  if (!(val[1] > 1e-12 * val[2])) return Result::Failed;  // collinear: no plane, no surface
  ON_3dVector zaxis(vec[0][0], vec[1][0], vec[2][0]), xaxis(vec[0][2], vec[1][2], vec[2][2]);
  if (!zaxis.Unitize() || !xaxis.Unitize()) return Result::Failed;
  ON_3dVector yaxis = ON_CrossProduct(zaxis, xaxis);
  if (!yaxis.Unitize()) return Result::Failed;

  std::vector<Datum> data;
  data.reserve(all.size());
  double x0 = std::numeric_limits<double>::max(), x1 = -x0, y0 = x0, y1 = -x0, flat = 0.0;
  const auto push = [&](const Point3d& p, double weight, bool boundary) {
    const ON_3dVector d = p - centre;
    const Datum k{ON_DotProduct(d, xaxis), ON_DotProduct(d, yaxis), ON_DotProduct(d, zaxis), weight, boundary};
    x0 = std::min(x0, k.x);
    x1 = std::max(x1, k.x);
    y0 = std::min(y0, k.y);
    y1 = std::max(y1, k.y);
    flat = std::max(flat, std::fabs(k.h));
    data.push_back(k);
  };
  for (const Point3d& p : boundary_points) push(p, 4.0, true);
  for (const Point3d& p : interior_points) push(p, 1.0, false);
  if (!(x1 - x0 > 1e-12) || !(y1 - y0 > 1e-12)) return Result::Failed;

  // Tangent-plane / curvature rows from the adjacent surfaces, in the fit plane's frame.
  std::vector<DerivConstraint> cons;
  double angle_scale = std::numeric_limits<double>::max(), curv_scale = std::numeric_limits<double>::max();
  if (!edges.empty()) {
    const cd::PlaneFrame frame{centre, xaxis, yaxis, zaxis};
    const double lref = 0.125 * ((x1 - x0) + (y1 - y0));
    const double w1 = 1e3 * lref * lref, w2 = 1e3 * lref * lref * lref * lref;
    for (size_t e = 0; e < edges.size(); ++e) {
      const std::string tag = "edge " + std::to_string(e + 1) + ": ";
      const FitEdgeContinuity& fe = edges[e];
      angle_scale = std::min(angle_scale, fe.angle_tolerance);
      const bool second = fe.continuity == MatchContinuity::Curvature;
      if (second) curv_scale = std::min(curv_scale, fe.curvature_tolerance);
      if (second && fe.adjacent->raw().Degree(0) < 2 && fe.adjacent->raw().Degree(1) < 2) {
        return refuse(tag + "curvature continuity requested but the adjacent surface is degree 1 (it has no curvature to continue)");
      }
      double worst_gap = 0.0;
      for (const Point3d& p : fe.points) {
        cd::Jet ja;
        if (!cd::Project(*fe.adjacent, p, ja)) return refuse(tag + "the adjacent surface could not be evaluated");
        worst_gap = std::max(worst_gap, ja.P.DistanceTo(p));
      }
      if (worst_gap > tolerance) {
        return refuse(tag + "the boundary samples lie up to " + std::to_string(worst_gap) + " from the adjacent surface, more than the tolerance " +
                      std::to_string(tolerance) + ": position (G0) continuity is not met, so tangent/curvature continuity is meaningless");
      }
      if (fe.continuity == MatchContinuity::Position) continue;
      for (const Point3d& p : fe.points) {
        cd::Jet ja;
        cd::Project(*fe.adjacent, p, ja);
        double g[5] = {0, 0, 0, 0, 0};
        const std::string why = cd::GraphDerivatives(ja, frame, second, g);
        if (!why.empty()) return refuse(tag + why);
        const ON_3dVector d = p - centre;
        const double px = d * xaxis, py = d * yaxis;
        cons.push_back({px, py, g[0], w1, 0});
        cons.push_back({px, py, g[1], w1, 1});
        if (second) {
          cons.push_back({px, py, g[2], w2, 2});
          cons.push_back({px, py, g[3], w2, 3});
          cons.push_back({px, py, g[4], w2, 4});
        }
      }
    }
  }

  FitResult best;
  double best_score = std::numeric_limits<double>::max();
  const double aspect = (y1 - y0) / (x1 - x0);
  for (int n = 2; n <= max_count; ++n) {
    int nu = n, nv = n;
    if (aspect >= 1.0) nv = std::min(max_count, std::max(2, static_cast<int>(std::lround(n * aspect))));
    else nu = std::min(max_count, std::max(2, static_cast<int>(std::lround(n / aspect))));
    if (static_cast<size_t>(nu) * static_cast<size_t>(nv) > data.size()) break;  // never underdetermined
    FitResult r = FitHeights(data, x0, x1, y0, y1, nu, nv, 1e-9, cons.empty() ? nullptr : &cons);
    if (!r.ok) continue;
    if (cons.empty()) {
      if (!best.ok || r.max_dev < best.max_dev) best = r;
      if (r.max_dev <= tolerance) break;
    } else {
      // Net growth is driven by the data deviation AND the derivative-row residuals.
      const double score = std::max({r.max_dev / tolerance, r.grad_err / angle_scale,
                                     curv_scale < std::numeric_limits<double>::max() ? r.hess_err / curv_scale : 0.0});
      if (!best.ok || score < best_score) { best = r; best_score = score; }
      if (score <= 0.25) break;
    }
  }
  if (!best.ok) return edges.empty() ? Result::Failed : refuse("the patch fit is singular");

  const int pu = std::min(3, best.nu - 1), pv = std::min(3, best.nv - 1);
  const std::vector<double> tu = PatchFitKnots(best.nu, pu, x0, x1), tv = PatchFitKnots(best.nv, pv, y0, y1);
  ON_NurbsSurface surface;
  if (!surface.Create(3, false, pu + 1, pv + 1, best.nu, best.nv)) return Result::Failed;
  // ON keeps the standard knot vector without its first and last entries.
  for (int k = 0; k < surface.KnotCount(0); ++k) surface.SetKnot(0, k, tu[static_cast<size_t>(k) + 1]);
  for (int k = 0; k < surface.KnotCount(1); ++k) surface.SetKnot(1, k, tv[static_cast<size_t>(k) + 1]);
  for (int i = 0; i < best.nu; ++i) {
    double gx = 0.0;
    for (int k = 1; k <= pu; ++k) gx += tu[static_cast<size_t>(i + k)];
    gx /= pu;
    for (int j = 0; j < best.nv; ++j) {
      double gy = 0.0;
      for (int k = 1; k <= pv; ++k) gy += tv[static_cast<size_t>(j + k)];
      gy /= pv;
      surface.SetCV(i, j, centre + xaxis * gx + yaxis * gy + zaxis * best.c[static_cast<size_t>(i * best.nv + j)]);
    }
  }
  if (!surface.IsValid()) return edges.empty() ? Result::Failed : refuse("the fitted surface is not valid");

  PatchFitReport rep;
  rep.u_count = best.nu;
  rep.v_count = best.nv;
  rep.data_points = static_cast<int>(data.size());
  rep.tolerance = tolerance;
  rep.met_tolerance = best.max_dev <= tolerance;
  rep.max_deviation = best.max_dev;
  rep.rms_deviation = best.rms_dev;
  rep.max_boundary_deviation = best.max_boundary_dev;
  rep.flat_deviation = flat;
  rep.plane_origin = centre;
  rep.plane_x = xaxis;
  rep.plane_y = yaxis;
  rep.plane_z = zaxis;

  if (!edges.empty()) {
    // Measure the finished surface at probes BETWEEN the constrained samples
    // (segment midpoints), against the adjacent surface itself.
    NurbsSurface candidate;
    candidate.raw() = surface;
    std::string why;
    for (size_t e = 0; e < edges.size() && why.empty(); ++e) {
      const FitEdgeContinuity& fe = edges[e];
      EdgeContinuityMeasure m;
      for (size_t k = 0; k + 1 < fe.points.size(); ++k) {
        const ON_3dPoint mid = (fe.points[k] + fe.points[k + 1]) * 0.5;
        const ON_3dVector dir = fe.points[k + 1] - fe.points[k];
        if (!(dir.Length() > 0.0)) continue;
        if (!cd::MeasureAt(candidate, *fe.adjacent, mid, dir, m)) {
          why = "edge " + std::to_string(e + 1) + ": a probe between the samples could not be evaluated on one of the surfaces";
          break;
        }
      }
      rep.edge_continuity.push_back(m);
      const std::string tag = "edge " + std::to_string(e + 1) + ": ";
      if (!why.empty()) break;
      if (m.max_position_gap > tolerance) {
        why = tag + "the finished surface is " + std::to_string(m.max_position_gap) + " from the adjacent surface at a probe, more than the tolerance " + std::to_string(tolerance);
      } else if (fe.continuity != MatchContinuity::Position && m.max_tangent_angle > fe.angle_tolerance) {
        why = tag + "tangent-plane deviation " + std::to_string(m.max_tangent_angle) + " rad exceeds the requested " + std::to_string(fe.angle_tolerance) +
              " rad (the data and the adjacent surface's tangent plane cannot both be met by a net of " + std::to_string(best.nu) + " x " + std::to_string(best.nv) + ")";
      } else if (fe.continuity == MatchContinuity::Curvature && m.max_cross_curvature_diff > fe.curvature_tolerance) {
        why = tag + "cross-edge curvature difference " + std::to_string(m.max_cross_curvature_diff) + " exceeds the requested " +
              std::to_string(fe.curvature_tolerance) + " (net of " + std::to_string(best.nu) + " x " + std::to_string(best.nv) + ")";
      }
    }
    if (why.empty() && !rep.met_tolerance) {
      why = "with the requested edge continuity the fit misses its data by " + std::to_string(best.max_dev) + ", more than the tolerance " +
            std::to_string(tolerance);
    }
    if (!why.empty()) {
      if (report) {
        *report = rep;
        report->reason = why;
      }
      return Result::Failed;
    }
  }

  out.raw() = surface;
  if (report) *report = rep;
  return Result::Ok;
}

// ---------------------------------------------------------------------------
// FitCurveNetwork (see surface.h): a crossing-curve network -> fitted surface.
// ---------------------------------------------------------------------------
namespace {
namespace curve_network_detail {

struct Meeting {
  bool met = false;
  double distance = std::numeric_limits<double>::infinity();  // minimum distance between the two curves
  double ta = 0.0, tb = 0.0;                                   // fractions [0,1] of each domain at the minimum
  Point3d mid;                                                  // midpoint of the closest pair
  bool multi = false;                                           // the curves come within tolerance in two separate places
  bool interior = false;                                        // at least one curve is crossed away from its own ends
};

double Fraction(const NurbsCurve& c, double t) {
  const auto d = c.Domain();
  return d.max > d.min ? (t - d.min) / (d.max - d.min) : 0.0;
}

// Alternating closest-point projection: a cheap local polish of a near-minimal pair (a(ta), b(tb)).
// Each step can only shrink the distance, so the result is never worse than the start.
void PolishPair(const NurbsCurve& a, const NurbsCurve& b, double& ta, double& tb, double& dist) {
  for (int it = 0; it < 12; ++it) {
    const double nb = b.ClosestPointParameter(a.PointAt(ta));
    const double na = a.ClosestPointParameter(b.PointAt(nb));
    const double d = a.PointAt(na).DistanceTo(b.PointAt(nb));
    if (d < dist - 1e-15) { dist = d; ta = na; tb = nb; } else break;
  }
}

Meeting Meet(const NurbsCurve& a, const NurbsCurve& b, double tol) {
  Meeting m;
  const auto da = a.Domain();
  // Sample curve a and measure each sample against b.  A coarse pass first: if no sample comes
  // within tol + (largest chord), the curves cannot meet (a point of a that touches b is within
  // half a chord of a sample), so the dense pass - the expensive part - is skipped.
  std::vector<double> d;
  double h = 0.0;
  auto scan = [&](int N) {
    d.assign(static_cast<size_t>(N) + 1, 0.0);
    h = 0.0;
    Point3d prev;
    for (int k = 0; k <= N; ++k) {
      const double t = da.min + (da.max - da.min) * k / N;
      const Point3d p = a.PointAt(t);
      if (k > 0) h = std::max(h, p.DistanceTo(prev));
      prev = p;
      d[static_cast<size_t>(k)] = p.DistanceTo(b.ClosestPoint(p));
    }
  };
  int N = 48;
  scan(N);
  if (*std::min_element(d.begin(), d.end()) > tol + h) {
    // Cannot meet.  Keep the sampled minimum as the distance, polished locally.
  } else {
    N = 240;
    scan(N);
  }
  int best_k = static_cast<int>(std::min_element(d.begin(), d.end()) - d.begin());
  double best = d[static_cast<size_t>(best_k)];
  double ta = da.min + (da.max - da.min) * best_k / N;
  double tb = b.ClosestPointParameter(a.PointAt(ta));
  PolishPair(a, b, ta, tb, best);
  m.distance = best;
  m.ta = Fraction(a, ta);
  m.tb = Fraction(b, tb);
  const Point3d pa3 = a.PointAt(ta), pb3 = b.PointAt(tb);
  m.mid = (pa3 + pb3) * 0.5;
  m.met = best <= tol;
  // Separate meeting places: every local minimum of the sampled distance that is plausibly a
  // meeting (within tol + one chord) is polished and counted when it really is within tol.
  // Two such places count as separate when they sit more than a few samples apart along a.
  if (m.met && N == 240) {
    struct Hit { double frac; Point3d pt; };
    std::vector<Hit> hits;
    for (int k = 0; k <= N; ++k) {
      const double dk = d[static_cast<size_t>(k)];
      if (dk > tol + h) continue;
      const bool left_ok = k == 0 || dk <= d[static_cast<size_t>(k) - 1];
      const bool right_ok = k == N || dk < d[static_cast<size_t>(k) + 1];
      if (!(left_ok && right_ok)) continue;
      double sa = da.min + (da.max - da.min) * k / N;
      double sb = b.ClosestPointParameter(a.PointAt(sa));
      double sd = dk;
      PolishPair(a, b, sa, sb, sd);
      if (sd > tol) continue;
      hits.push_back({Fraction(a, sa), a.PointAt(sa)});
    }
    const bool closed = a.IsClosed();
    int places = 0;
    std::vector<Hit> kept;
    for (const Hit& hit : hits) {
      bool same = false;
      for (const Hit& k : kept) {
        double df = std::fabs(hit.frac - k.frac);
        if (closed) df = std::min(df, 1.0 - df);
        if (df < 4.0 / N || hit.pt.DistanceTo(k.pt) <= 10.0 * tol) { same = true; break; }
      }
      if (!same) { kept.push_back(hit); ++places; }
    }
    m.multi = places > 1;
  }
  const double end_eps = std::max(2.0 * tol, 1e-6);
  auto interior_of = [&](const NurbsCurve& c) {
    return m.mid.DistanceTo(c.PointAt(c.Domain().min)) > end_eps && m.mid.DistanceTo(c.PointAt(c.Domain().max)) > end_eps;
  };
  m.interior = interior_of(a) || interior_of(b);
  return m;
}

}  // namespace curve_network_detail
}  // namespace

Result NurbsSurface::FitCurveNetwork(const std::vector<NurbsCurve>& curves_in, double tolerance, NurbsSurface& out,
                                     CurveNetworkReport& report) {
  return FitCurveNetwork(curves_in, tolerance, out, report, std::vector<NetworkEdgeContinuity>());
}

Result NurbsSurface::FitCurveNetwork(const std::vector<NurbsCurve>& curves_in, double tolerance, NurbsSurface& out,
                                     CurveNetworkReport& report, const std::vector<NetworkEdgeContinuity>& net_edges) {
  using namespace curve_network_detail;
  if (!(tolerance > 0.0) || !std::isfinite(tolerance)) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::FitCurveNetwork: tolerance must be positive and finite");
  }
  report = CurveNetworkReport{};
  report.tolerance = tolerance;
  const size_t n = curves_in.size();
  auto refuse = [&](const std::string& why) {
    report.reason = why;
    return Result::Failed;
  };
  auto name = [](size_t i) { return "curve " + std::to_string(i + 1); };
  if (n < 4) return refuse("a curve network needs at least 4 curves (2 in each direction), " + std::to_string(n) + " given");
  if (n > 40) return refuse("a curve network of more than 40 curves is not supported (" + std::to_string(n) + " given)");
  std::vector<NurbsCurve> curves = curves_in;
  for (size_t i = 0; i < n; ++i) {
    if (!curves[i].raw().IsValid()) return refuse(name(i) + " is not a valid curve");
    double len = 0.0;
    const auto dom = curves[i].Domain();
    Point3d prev = curves[i].PointAt(dom.min);
    for (int k = 1; k <= 64; ++k) {
      const Point3d p = curves[i].PointAt(dom.min + (dom.max - dom.min) * k / 64.0);
      len += p.DistanceTo(prev);
      prev = p;
    }
    if (!(len > 10.0 * tolerance)) return refuse(name(i) + " is shorter than ten times the tolerance");
  }

  // Continuity requests: 65 ordered samples of the named input curve each.
  std::vector<FitEdgeContinuity> fit_edges;
  for (size_t e = 0; e < net_edges.size(); ++e) {
    const NetworkEdgeContinuity& ne = net_edges[e];
    if (ne.curve_index < 0 || static_cast<size_t>(ne.curve_index) >= n) {
      return refuse("edge continuity " + std::to_string(e + 1) + " names curve " + std::to_string(ne.curve_index + 1) + ", but the network has " +
                    std::to_string(n) + " curves");
    }
    if (ne.adjacent == nullptr) return refuse("edge continuity " + std::to_string(e + 1) + " has no adjacent surface");
    FitEdgeContinuity fe;
    fe.adjacent = ne.adjacent;
    fe.continuity = ne.continuity;
    fe.angle_tolerance = ne.angle_tolerance;
    fe.curvature_tolerance = ne.curvature_tolerance;
    const NurbsCurve& c = curves[static_cast<size_t>(ne.curve_index)];
    const auto d = c.Domain();
    for (int k = 0; k <= 64; ++k) fe.points.push_back(c.PointAt(d.min + (d.max - d.min) * k / 64.0));
    fit_edges.push_back(std::move(fe));
  }

  // Meetings of every pair.
  std::vector<std::vector<Meeting>> M(n, std::vector<Meeting>(n));
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      M[i][j] = Meet(curves[i], curves[j], tolerance);
      M[j][i] = M[i][j];
      std::swap(M[j][i].ta, M[j][i].tb);
    }
  }
  double nearest_miss = std::numeric_limits<double>::infinity();
  size_t miss_i = 0, miss_j = 0;
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      if (M[i][j].met && M[i][j].interior) report.any_interior_meeting = true;
      if (!M[i][j].met && M[i][j].distance < nearest_miss) { nearest_miss = M[i][j].distance; miss_i = i; miss_j = j; }
    }
  }
  const std::string miss_note = std::isfinite(nearest_miss)
      ? " (the closest pair that does not meet is " + name(miss_i) + " and " + name(miss_j) + ", " + std::to_string(nearest_miss) + " apart)"
      : std::string();

  // Two-colour the meeting graph.
  std::vector<int> color(n, -1);
  size_t root = 0, root_deg = 0;  // start from the best-connected curve: a lone stray curve is then the one blamed
  for (size_t i = 0; i < n; ++i) {
    size_t deg = 0;
    for (size_t j = 0; j < n; ++j) if (j != i && M[i][j].met) ++deg;
    if (deg > root_deg) { root_deg = deg; root = i; }
  }
  color[root] = 0;
  std::vector<size_t> queue = {root};
  for (size_t qi = 0; qi < queue.size(); ++qi) {
    const size_t i = queue[qi];
    for (size_t j = 0; j < n; ++j) {
      if (j == i || !M[i][j].met) continue;
      if (color[j] < 0) { color[j] = 1 - color[i]; queue.push_back(j); }
      else if (color[j] == color[i]) {
        return refuse(name(i) + " and " + name(j) + " are in the same direction (both meet a common curve) but meet each other: not a network of two crossing directions" + miss_note);
      }
    }
  }
  for (size_t i = 0; i < n; ++i) {
    if (color[i] < 0) {
      double nearest = std::numeric_limits<double>::infinity();
      size_t with = i;
      for (size_t j = 0; j < n; ++j) {
        if (j != i && color[j] >= 0 && M[i][j].distance < nearest) { nearest = M[i][j].distance; with = j; }
      }
      return refuse(name(i) + " does not meet the rest of the network within the tolerance (nearest is " + name(with) + ", " + std::to_string(nearest) + " apart)");
    }
  }
  std::vector<size_t> g[2];
  for (size_t i = 0; i < n; ++i) g[color[i]].push_back(i);
  if (g[0].size() < 2 || g[1].size() < 2) {
    return refuse("each direction needs at least 2 curves; the network splits " + std::to_string(g[0].size()) + " + " + std::to_string(g[1].size()));
  }
  for (size_t a : g[0]) {
    for (size_t b : g[1]) {
      if (!M[a][b].met) {
        return refuse(name(a) + " (direction 1) and " + name(b) + " (direction 2) miss each other by " + std::to_string(M[a][b].distance) +
                      ", more than the tolerance " + std::to_string(tolerance) + ": every curve must meet every curve of the other direction");
      }
      if (M[a][b].multi) return refuse(name(a) + " and " + name(b) + " come within the tolerance in more than one place");
    }
  }

  // Order and orient. frac(a, b) = fraction along a where it meets b.
  auto frac = [&](size_t a, size_t b) { return M[a][b].ta; };
  std::vector<bool> rev(n, false);
  std::vector<size_t> ord[2];
  ord[0] = g[0];
  ord[1] = g[1];
  auto sort_by = [&](std::vector<size_t>& v, size_t ref) {
    std::sort(v.begin(), v.end(), [&](size_t x, size_t y) { return frac(ref, x) < frac(ref, y); });
  };
  // Direction-2 curves ordered by where they cross the first direction-1 curve (as drawn);
  // every direction-1 curve is then turned to cross them in that order.
  auto orient = [&](const std::vector<size_t>& group, const std::vector<size_t>& across, size_t first) -> std::string {
    for (size_t a : group) {
      const double mean_r = 0.5 * (static_cast<double>(across.size()) - 1.0);
      double mean_t = 0.0;
      for (size_t b : across) mean_t += frac(a, b);
      mean_t /= static_cast<double>(across.size());
      double corr = 0.0;
      for (size_t r = 0; r < across.size(); ++r) corr += (static_cast<double>(r) - mean_r) * (frac(a, across[r]) - mean_t);
      rev[a] = corr < 0.0;
      for (size_t r = 1; r < across.size(); ++r) {
        double t0 = frac(a, across[r - 1]), t1 = frac(a, across[r]);
        if (rev[a]) { t0 = 1.0 - t0; t1 = 1.0 - t1; }
        if (!(t1 > t0)) return name(a) + " crosses the other direction in a different order than " + name(first) + ": the network is not a regular grid";
      }
    }
    return std::string();
  };
  sort_by(ord[1], g[0][0]);
  if (std::string why = orient(g[0], ord[1], g[0][0]); !why.empty()) return refuse(why);
  sort_by(ord[0], ord[1][0]);
  if (std::string why = orient(g[1], ord[0], ord[1][0]); !why.empty()) return refuse(why);
  report.u_curves = static_cast<int>(g[0].size());
  report.v_curves = static_cast<int>(g[1].size());
  report.direction.assign(n, 0);
  report.order.assign(n, 0);
  report.reversed = rev;
  for (int dir = 0; dir < 2; ++dir) {
    for (size_t r = 0; r < ord[dir].size(); ++r) {
      report.direction[ord[dir][r]] = dir;
      report.order[ord[dir][r]] = static_cast<int>(r);
    }
  }

  // Meeting grid and the data to fit.
  std::vector<Point3d> boundary, interior;
  for (size_t a : g[0]) {
    for (size_t b : g[1]) {
      report.max_intersection_gap = std::max(report.max_intersection_gap, M[a][b].distance);
      if (M[a][b].interior) ++report.interior_crossings;
      ++report.intersections;
      interior.push_back(M[a][b].mid);
    }
  }
  for (size_t i = 0; i < n; ++i) {
    const auto d = curves[i].Domain();
    for (int k = 0; k <= 64; ++k) boundary.push_back(curves[i].PointAt(d.min + (d.max - d.min) * k / 64.0));
  }

  // Fit, then measure every input curve against the result.
  NurbsSurface best;
  bool have = false;
  double best_dev = std::numeric_limits<double>::infinity();
  std::vector<double> best_curve_dev;
  std::string continuity_reason;
  for (const int max_count : {16, 24}) {
    NurbsSurface patch;
    PatchFitReport rep;
    Result fr = Result::Failed;
    try {
      fr = fit_edges.empty() ? FitPatch(boundary, interior, tolerance, patch, &rep, max_count)
                             : FitPatch(boundary, interior, tolerance, patch, &rep, max_count, fit_edges);
    } catch (const std::exception& e) {
      return refuse(std::string("the patch fit threw: ") + e.what());
    }
    if (fr != Result::Ok) {
      if (!fit_edges.empty() && !rep.reason.empty()) {
        continuity_reason = rep.reason;
        if (!have) report.edge_continuity = rep.edge_continuity;
        continue;  // a finer net may still meet it
      }
      return refuse("the curves are (nearly) collinear or the patch fit is singular");
    }
    std::vector<double> dev(n, 0.0);
    double worst = 0.0;
    for (size_t i = 0; i < n; ++i) {
      const auto d = curves[i].Domain();
      for (int k = 0; k <= 100; ++k) {
        const Point3d p = curves[i].PointAt(d.min + (d.max - d.min) * k / 100.0);
        dev[i] = std::max(dev[i], p.DistanceTo(patch.ClosestPoint(p, 40, 40)));
      }
      worst = std::max(worst, dev[i]);
    }
    if (!have || worst < best_dev) {
      have = true;
      best_dev = worst;
      best = patch;
      best_curve_dev = dev;
      report.fit = rep;
      report.net_u = rep.u_count;
      report.net_v = rep.v_count;
      report.edge_continuity = rep.edge_continuity;
    }
    if (worst <= tolerance) break;
  }
  if (!have) return refuse(continuity_reason.empty() ? std::string("the patch fit failed") : continuity_reason);
  report.curve_deviation = best_curve_dev;
  report.max_deviation = best_dev;
  report.met_tolerance = best_dev <= tolerance && report.max_intersection_gap <= tolerance;
  if (!(best_dev <= tolerance)) {
    std::string why = "the best fitted surface (" + std::to_string(report.net_u) + " x " + std::to_string(report.net_v) + " net) misses an input curve by " +
                      std::to_string(best_dev) + ", more than the tolerance " + std::to_string(tolerance);
    if (!fit_edges.empty()) {
      why += ": the requested edge continuity pulls the surface away from the input curves (the curves and the adjacent surface's tangent plane/curvature are incompatible)";
    } else {
      why += ": the network is not a smooth height field over its best-fit plane or needs a finer net";
    }
    return refuse(why);
  }
  out = best;
  return Result::Ok;
}

Result NurbsSurface::ExtendLinear(int direction, double t0, double t1) {
  if (direction != 0 && direction != 1) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::ExtendLinear: direction must be 0 (U) or 1 (V)");
  }
  if (t0 >= t1) return Result::Failed;
  if (surface_.IsClosed(direction)) return Result::Failed;
  const ON_Interval current = surface_.Domain(direction);
  if (t0 >= current.Min() && t1 <= current.Max()) return Result::NoOpAlreadySatisfied;

  const ON_NurbsSurface backup = surface_;
  if (t0 < current.Min() && !ExtendOneEndLinear(surface_, direction, /*at_max=*/false, current.Min() - t0)) {
    surface_ = backup;
    return Result::Failed;
  }
  const ON_Interval mid = surface_.Domain(direction);
  if (t1 > current.Max() && !ExtendOneEndLinear(surface_, direction, /*at_max=*/true, t1 - mid.Max())) {
    surface_ = backup;
    return Result::Failed;
  }
  return Result::Ok;
}

namespace {

// One candidate "kinky" knot in `direction`: `t` is the knot value
// itself, `span_before`/`span_after` are the distances to the nearest
// OTHER knot (of any multiplicity) on each side - the widest a one-sided
// sampling offset around `t` may be without straying out of the two
// B-spline spans immediately adjacent to it.
struct CreaseCandidate {
  double t;
  double span_before;
  double span_after;
};

// Every interior knot in `direction` whose multiplicity equals this
// surface's own degree there - the necessary (not sufficient) condition
// for a "kinky" knot, since only at full multiplicity can a clamped
// B-spline be merely C0, not C1 or higher, across an interior knot.
// Mirrors RemoveKnotAt's own knot-vector reasoning above (exact `==`
// comparison between consecutive `Knot()` values to find each run's own
// multiplicity), just collecting every qualifying run instead of
// checking one caller-given index.
std::vector<CreaseCandidate> FullMultiplicityInteriorKnots(const ON_NurbsSurface& s, int direction) {
  std::vector<CreaseCandidate> result;
  const int degree = s.Degree(direction);
  const int knot_count = s.KnotCount(direction);
  const ON_Interval domain = s.Domain(direction);
  double prev_value = domain.Min();
  int i = 0;
  while (i < knot_count) {
    const double value = s.Knot(direction, i);
    int j = i;
    while (j < knot_count && s.Knot(direction, j) == value) ++j;
    const double next_value = (j < knot_count) ? s.Knot(direction, j) : domain.Max();
    if (j - i == degree && value > domain.Min() && value < domain.Max()) {
      result.push_back(CreaseCandidate{value, value - prev_value, next_value - value});
    }
    prev_value = value;
    i = j;
  }
  return result;
}

// Whether the surface's own one-sided tangent in `direction` genuinely
// disagrees across the candidate crease parameter `t` by more than
// `angle_tolerance_radians`, at one or more of several points sampled
// along the OTHER direction - a real geometric kink, not just a knot
// that happens to stay smooth. `eps` is the one-sided sampling offset;
// the caller sizes it to stay within the two spans immediately adjacent
// to `t` (see `CreaseCandidate` above), so the two evaluations below
// land in two different Bezier pieces, not the same one.
bool IsGenuineCrease(const ON_NurbsSurface& s, int direction, double t, double eps,
                      double angle_tolerance_radians) {
  const int other = 1 - direction;
  const ON_Interval other_domain = s.Domain(other);
  constexpr int kSamples = 7;
  for (int k = 0; k < kSamples; ++k) {
    // Interior-only cross samples: the two ends are where a singular or
    // otherwise degenerate derivative is most likely, and are not what
    // this is checking.
    const double frac = static_cast<double>(k + 1) / static_cast<double>(kSamples + 1);
    const double c = other_domain.ParameterAt(frac);
    const double u_minus = direction == 0 ? t - eps : c;
    const double v_minus = direction == 0 ? c : t - eps;
    const double u_plus = direction == 0 ? t + eps : c;
    const double v_plus = direction == 0 ? c : t + eps;

    ON_3dPoint p_minus, p_plus;
    ON_3dVector du_minus, dv_minus, du_plus, dv_plus;
    if (!s.Ev1Der(u_minus, v_minus, p_minus, du_minus, dv_minus)) continue;
    if (!s.Ev1Der(u_plus, v_plus, p_plus, du_plus, dv_plus)) continue;

    ON_3dVector tangent_minus = direction == 0 ? du_minus : dv_minus;
    ON_3dVector tangent_plus = direction == 0 ? du_plus : dv_plus;
    const double len_minus = tangent_minus.Length();
    const double len_plus = tangent_plus.Length();
    if (len_minus <= tolerance::kZeroVector || len_plus <= tolerance::kZeroVector) continue;
    tangent_minus /= len_minus;
    tangent_plus /= len_plus;

    const double dot = std::clamp(ON_DotProduct(tangent_minus, tangent_plus), -1.0, 1.0);
    if (std::acos(dot) > angle_tolerance_radians) return true;
  }
  return false;
}

}  // namespace

std::vector<NurbsSurface> NurbsSurface::SplitAtCreases(double angle_tolerance_radians) const {
  const double angle_tol = angle_tolerance_radians > 0.0 ? angle_tolerance_radians : tolerance::kMergeEdgeAngle;

  std::vector<NurbsSurface> pieces;
  std::vector<NurbsSurface> worklist{*this};
  while (!worklist.empty()) {
    NurbsSurface current = std::move(worklist.back());
    worklist.pop_back();

    bool split_done = false;
    for (int direction = 0; direction < 2 && !split_done; ++direction) {
      const std::vector<CreaseCandidate> candidates =
          FullMultiplicityInteriorKnots(current.surface_, direction);
      for (const CreaseCandidate& candidate : candidates) {
        const double eps = 0.25 * std::min(candidate.span_before, candidate.span_after);
        if (!(eps > 0.0)) continue;
        if (!IsGenuineCrease(current.surface_, direction, candidate.t, eps, angle_tol)) continue;

        NurbsSurface west_or_south, east_or_north;
        if (current.Split(direction, candidate.t, west_or_south, east_or_north) != Result::Ok) continue;
        worklist.push_back(std::move(west_or_south));
        worklist.push_back(std::move(east_or_north));
        split_done = true;
        break;
      }
    }
    if (!split_done) pieces.push_back(std::move(current));
  }
  return pieces;
}


// ---------------------------------------------------------------------------
// MeasureEdgeContinuity / LoftMatched (see surface.h).
// ---------------------------------------------------------------------------
Result NurbsSurface::MeasureEdgeContinuity(const NurbsSurface& adjacent, int adjacent_fixed_direction, bool adjacent_at_min, int samples,
                                           EdgeContinuityMeasure& out) const {
  namespace cd = continuity_detail;
  if (adjacent_fixed_direction != 0 && adjacent_fixed_direction != 1) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::MeasureEdgeContinuity: direction must be 0 (U) or 1 (V)");
  }
  if (samples < 2) throw std::invalid_argument("dino8::kernel::NurbsSurface::MeasureEdgeContinuity: samples must be at least 2");
  out = EdgeContinuityMeasure{};
  const ON_NurbsSurface& a = adjacent.raw();
  const ON_Interval fixed_dom = a.Domain(adjacent_fixed_direction), run_dom = a.Domain(1 - adjacent_fixed_direction);
  const double fixed = adjacent_at_min ? fixed_dom.Min() : fixed_dom.Max();
  for (int k = 0; k < samples; ++k) {
    const double t = run_dom.Min() + run_dom.Length() * k / (samples - 1);
    const double u = adjacent_fixed_direction == 0 ? fixed : t, v = adjacent_fixed_direction == 0 ? t : fixed;
    cd::Jet j;
    if (!cd::EvalJet(a, u, v, j)) return Result::Failed;
    const ON_3dVector along = adjacent_fixed_direction == 0 ? j.Sv : j.Su;
    if (!cd::MeasureAt(*this, adjacent, j.P, along, out)) return Result::Failed;
  }
  return Result::Ok;
}

Result NurbsSurface::LoftMatched(const std::vector<NurbsCurve>& sections, int degree, const LoftEndMatch* start, const LoftEndMatch* end,
                                 double tolerance, NurbsSurface& out, LoftMatchReport& report, double angle_tolerance,
                                 double curvature_tolerance) {
  namespace cd = continuity_detail;
  if (!(tolerance > 0.0) || !std::isfinite(tolerance) || !(angle_tolerance > 0.0) || !(curvature_tolerance > 0.0)) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::LoftMatched: tolerances must be positive and finite");
  }
  report = LoftMatchReport{};
  auto refuse = [&](const std::string& why) {
    report.reason = why;
    return Result::Failed;
  };
  if (sections.size() < 2) return refuse("a loft needs at least 2 sections");
  if (start == nullptr && end == nullptr) return refuse("no end continuity requested (use Brep::Loft for a plain loft)");
  for (const LoftEndMatch* m : {start, end}) {
    if (m == nullptr) continue;
    if (m->surface == nullptr || !m->surface->raw().IsValid()) return refuse("an adjacent surface is missing or invalid");
    if (m->fixed_direction != 0 && m->fixed_direction != 1) return refuse("an adjacent edge direction must be 0 or 1");
  }
  for (const NurbsCurve& s : sections) {
    if (s.IsClosed()) return refuse("a closed section has no loft end to continue an adjacent surface from (open sections only)");
  }

  NurbsSurface loft;
  try {
    Brep b = Brep::Loft(sections, degree, /*closed=*/false, /*cap=*/false);
    if (b.raw().m_F.Count() < 1) return refuse("the kernel loft produced no face");
    const ON_Surface* srf = b.raw().m_F[0].SurfaceOf();
    if (srf == nullptr || !srf->GetNurbForm(loft.raw())) return refuse("the kernel loft surface could not be read as a NURBS surface");
  } catch (const std::exception& e) {
    return refuse(std::string("the kernel loft refused: ") + e.what());
  }

  // The loft runs along v; its first section is the v-min edge, its last the v-max edge.
  struct End {
    const LoftEndMatch* m;
    bool at_min;
    const char* name;
  };
  NurbsSurface work = loft;
  const End ends[2] = {{start, true, "start"}, {end, false, "end"}};
  const ON_NurbsSurface& wn = loft.raw();
  for (const End& e : ends) {
    if (e.m == nullptr) continue;
    const ON_NurbsSurface& adj = e.m->surface->raw();
    // G0 precheck: the section's edge must lie on the adjacent surface and span the same range as its edge.
    const double vfix = e.at_min ? wn.Domain(1).Min() : wn.Domain(1).Max();
    double gap = 0.0;
    for (int k = 0; k <= 64; ++k) {
      const double u = wn.Domain(0).Min() + wn.Domain(0).Length() * k / 64.0;
      cd::Jet jl, ja;
      if (!cd::EvalJet(wn, u, vfix, jl) || !cd::Project(*e.m->surface, jl.P, ja)) return refuse(std::string(e.name) + ": evaluation failed");
      gap = std::max(gap, jl.P.DistanceTo(ja.P));
      // Side check: the loft's body must leave the edge AWAY from the adjacent surface. Matching forces the tangent to
      // continue the adjacent surface, so a loft heading back over it would fold; a distance test on the sections alone
      // cannot see that (the folded surface passes through the sections again further along).
      const ON_3dVector into_adjacent = (e.m->at_min ? 1.0 : -1.0) * (e.m->fixed_direction == 0 ? ja.Su : ja.Sv);
      const ON_3dVector body = e.at_min ? jl.Sv : -jl.Sv;
      if (e.m->continuity != MatchContinuity::Position && !(body * into_adjacent < 0.0)) {
        return refuse(std::string(e.name) + " section: the loft leaves the adjacent edge towards (or along) the adjacent surface, so tangent/curvature continuity would fold it back over that surface; "
                      "the sections must continue away from it");
      }
    }
    if (gap > tolerance) {
      return refuse(std::string(e.name) + " section lies up to " + std::to_string(gap) + " from the adjacent surface, more than the tolerance " +
                    std::to_string(tolerance) + ": position (G0) continuity is not met");
    }
    const int rd = 1 - e.m->fixed_direction;
    const double af = e.m->at_min ? adj.Domain(e.m->fixed_direction).Min() : adj.Domain(e.m->fixed_direction).Max();
    auto adj_pt = [&](bool at_run_min) {
      const double t = at_run_min ? adj.Domain(rd).Min() : adj.Domain(rd).Max();
      return e.m->fixed_direction == 0 ? adj.PointAt(af, t) : adj.PointAt(t, af);
    };
    const ON_3dPoint l0 = wn.PointAt(wn.Domain(0).Min(), vfix), l1 = wn.PointAt(wn.Domain(0).Max(), vfix);
    const double straight = std::max(l0.DistanceTo(adj_pt(true)), l1.DistanceTo(adj_pt(false)));
    const double flipped = std::max(l0.DistanceTo(adj_pt(false)), l1.DistanceTo(adj_pt(true)));
    if (std::min(straight, flipped) > tolerance) {
      return refuse(std::string(e.name) + " section's end points are " + std::to_string(std::min(straight, flipped)) +
                    " from the adjacent edge's end points: the section does not span that edge");
    }
    MatchEdgeReport mr;
    const Result r = work.MatchEdge(1, e.at_min, *e.m->surface, e.m->fixed_direction, e.m->at_min, e.m->continuity, &mr);
    if (r != Result::Ok) return refuse(std::string(e.name) + ": MatchEdge refused (non-clamped surface, adjacent surface of too low a degree, or a self-check failure)");
  }

  // Every section against the finished surface.
  report.section_deviation.assign(sections.size(), 0.0);
  for (size_t i = 0; i < sections.size(); ++i) {
    const auto d = sections[i].Domain();
    for (int k = 0; k <= 32; ++k) {
      const Point3d p = sections[i].PointAt(d.min + (d.max - d.min) * k / 32.0);
      cd::Jet j;
      if (!cd::Project(work, p, j)) return refuse("a section could not be projected onto the finished surface");
      report.section_deviation[i] = std::max(report.section_deviation[i], p.DistanceTo(j.P));
    }
    report.section_deviation_max = std::max(report.section_deviation_max, report.section_deviation[i]);
  }
  if (report.section_deviation_max > tolerance) {
    size_t worst = 0;
    for (size_t i = 0; i < sections.size(); ++i) if (report.section_deviation[i] > report.section_deviation[worst]) worst = i;
    return refuse("matching the adjacent surface moves section " + std::to_string(worst + 1) + " by " + std::to_string(report.section_deviation_max) +
                  ", more than the tolerance " + std::to_string(tolerance) +
                  ": the sections are incompatible with the adjacent surface's tangent plane/curvature (e.g. the loft heads back over the adjacent surface)");
  }

  // Independent measurement against the adjacent surfaces.
  for (const End& e : ends) {
    if (e.m == nullptr) continue;
    EdgeContinuityMeasure m;
    if (work.MeasureEdgeContinuity(*e.m->surface, e.m->fixed_direction, e.m->at_min, 97, m) != Result::Ok) {
      return refuse(std::string(e.name) + ": the continuity measurement could not be evaluated");
    }
    (e.at_min ? report.start : report.end) = m;
    (e.at_min ? report.start_matched : report.end_matched) = true;
    if (m.max_position_gap > tolerance) return refuse(std::string(e.name) + ": measured gap " + std::to_string(m.max_position_gap) + " exceeds the tolerance");
    if (e.m->continuity != MatchContinuity::Position && m.max_tangent_angle > angle_tolerance) {
      return refuse(std::string(e.name) + ": measured tangent-plane deviation " + std::to_string(m.max_tangent_angle) + " rad exceeds " +
                    std::to_string(angle_tolerance));
    }
    if (e.m->continuity == MatchContinuity::Curvature && m.max_cross_curvature_diff > curvature_tolerance) {
      return refuse(std::string(e.name) + ": measured cross-edge curvature difference " + std::to_string(m.max_cross_curvature_diff) + " exceeds " +
                    std::to_string(curvature_tolerance));
    }
  }
  out = work;
  return Result::Ok;
}

}  // namespace dino8::kernel
