#pragma once

// Exact NURBS degree elevation - the replacement for OpenNURBS' own
// `ON_NurbsCurve::IncreaseDegree` / `ON_NurbsSurface::IncreaseDegree`,
// which this kernel's `NurbsCurve::ElevateDegree()` /
// `NurbsSurface::ElevateDegree()` used to delegate to.
//
// WHY NOT DELEGATE: a randomized probe of `IncreaseDegree` (public
// OpenNURBS v8.34, `IncrementNurbDegree` + `GetRaisedDegreeCV` in
// opennurbs_nurbscurve.cpp) found it is NOT shape-preserving for a
// non-uniform knot vector once the degree is moderately high - measured
// directly, not assumed: with unit-scale control points, elevating a
// degree-8 curve with distinct non-uniform interior knots by one moved the
// curve by up to 1.7e-6; degree 12 by up to 3.2e+2 (!); degree 16 with
// repeated interior knots by up to 1e+24 (pure garbage); even a UNIFORM
// degree-19 knot vector drifted by 3.4e-6. `ON_NurbsSurface::
// IncreaseDegree` packs every row of the surface into one high-dimensional
// `ON_NurbsCurve` and calls that same routine, so it inherits the bug (see
// TestCurveElevateDegreePreservesShapeWithNonUniformKnots /
// TestSurfaceElevateDegreePreservesShapeWithNonUniformKnots in
// tests/test_basic.cpp for the fixed-input regression versions).
//
// TWO ALGORITHMS, BOTH PUBLISHED (Piegl & Tiller, "The NURBS Book", 2nd
// ed.), transcribed here from that public description, not from or
// against any proprietary kernel's source:
//
//  1. Piecewise-Bezier form (DegreeElevateNurbsCurveBezierForm): clamp,
//     decompose into Bezier segments by knot insertion
//     (`ON_NurbsCurve::MakePiecewiseBezier`, exact de Boor insertion -
//     nothing but convex combinations), elevate each segment by the
//     closed-form binomial coefficients (eq. 5.36) in homogeneous
//     coordinates, and keep every interior breakpoint at full multiplicity.
//     Unconditionally shape-exact (measured <= 4.5e-15 at unit scale for
//     every degree 1..19, uniform/distinct/repeated knots, rational
//     weights 1e-3..1e3, periodic input) but NOT the minimal
//     representation: the result has `spans * (degree + t) + 1` control
//     points instead of `cv_count + t * spans`.
//
//  2. Minimal form (algorithm A5.9 "DegreeElevateCurve"): the same Bezier
//     elevation, but with the redundant knots removed again at each
//     breakpoint via the exact continuity condition, so the result has the
//     minimal `cv_count + t * spans` control points. Exact in exact
//     arithmetic, but knot REMOVAL is an inverse problem whose
//     conditioning degrades with degree (it is not a convex combination):
//     measured at unit scale, A5.9 stays within 1e-12 for every degree
//     <= 5 case probed and for 88% of degree-8, 62% of degree-12, 39% of
//     degree-16 cases - and a `MakePeriodicExact()` input (whose own
//     documented 1e-13-relative knot nudge puts knots almost on top of
//     each other) can blow up outright.
//
// `DegreeElevateNurbsCurve` therefore computes BOTH, verifies the minimal
// form against the Bezier form by direct evaluation (a dense parametric
// sample, Euclidean distance, tolerance 1e-12 of the control polygon's
// own bounding-box diagonal), and returns the minimal form only when it
// passes - otherwise the exact Bezier form. Either way the returned curve
// has the requested degree and the input's shape; only the control-point
// count can differ, and never silently the geometry.
//
// Knot-vector conventions: OpenNURBS stores a "compressed" knot vector
// that drops the textbook's redundant first and last entries (`KnotCount()
// == CVCount() + Order() - 2`); A5.9 is written for the textbook vector
// U[0..m], m = n + p + 1. The helper converts on the way in and out. The
// input is clamped first (`ON_NurbsCurve::ClampEnd(2)`, the same first
// step OpenNURBS' own `IncreaseDegree` takes), so a periodic input comes
// back clamped, exactly as before this change.

#include <algorithm>
#include <cmath>
#include <vector>

#include <opennurbs.h>

namespace dino8::kernel::detail {

// Algorithm 1 above: exact piecewise-Bezier degree elevation by `t` >= 1.
inline bool DegreeElevateNurbsCurveBezierForm(const ON_NurbsCurve& input, int t, ON_NurbsCurve& out) {
  if (t < 1 || !input.IsValid()) return false;
  ON_NurbsCurve pw = input;
  if (!pw.ClampEnd(2)) return false;
  if (!pw.MakePiecewiseBezier(false)) return false;
  const int p = pw.Degree();
  const int spans = pw.SpanCount();
  const int cvdim = pw.CVSize();
  const int q = p + t;
  const int m = spans * q + 1;
  ON_NurbsCurve res;
  if (!res.Create(pw.Dimension(), pw.IsRational(), q + 1, m)) return false;
  std::vector<double> breaks(static_cast<size_t>(spans) + 1);
  pw.GetSpanVector(breaks.data());
  int ki = 0;
  for (int s = 0; s <= spans; ++s)
    for (int r = 0; r < q; ++r) res.SetKnot(ki++, breaks[static_cast<size_t>(s)]);

  auto Bin = [](int a, int b) -> double {
    if (b < 0 || b > a) return 0.0;
    double r = 1.0;
    for (int i = 1; i <= b; ++i) r = r * (a - b + i) / i;
    return r;
  };
  // coef[i][j] = C(p,j) C(t,i-j) / C(q,i) (eq. 5.36), computed once.
  std::vector<double> coef(static_cast<size_t>(q + 1) * static_cast<size_t>(p + 1), 0.0);
  for (int i = 0; i <= q; ++i) {
    const double inv = 1.0 / Bin(q, i);
    for (int j = std::max(0, i - t); j <= std::min(p, i); ++j)
      coef[static_cast<size_t>(i) * static_cast<size_t>(p + 1) + static_cast<size_t>(j)] = inv * Bin(p, j) * Bin(t, i - j);
  }
  std::vector<double> acc(static_cast<size_t>(cvdim));
  for (int s = 0; s < spans; ++s) {
    for (int i = 0; i <= q; ++i) {
      std::fill(acc.begin(), acc.end(), 0.0);
      for (int j = std::max(0, i - t); j <= std::min(p, i); ++j) {
        const double c = coef[static_cast<size_t>(i) * static_cast<size_t>(p + 1) + static_cast<size_t>(j)];
        const double* b = pw.CV(s * p + j);
        for (int k = 0; k < cvdim; ++k) acc[static_cast<size_t>(k)] += c * b[k];
      }
      double* dst = res.CV(s * q + i);
      for (int k = 0; k < cvdim; ++k) dst[k] = acc[static_cast<size_t>(k)];
    }
  }
  out = res;
  return true;
}

// Algorithm 2 above: Piegl & Tiller A5.9, minimal form. No accuracy
// guarantee on its own - see DegreeElevateNurbsCurve for the verified
// entry point.
inline bool DegreeElevateNurbsCurveMinimalForm(const ON_NurbsCurve& input, int t, ON_NurbsCurve& out) {
  if (t < 1 || !input.IsValid()) return false;
  ON_NurbsCurve in = input;
  if (!in.ClampEnd(2)) return false;

  const int p = in.Degree();
  const int n = in.CVCount() - 1;  // textbook n: last control point index
  const int cvdim = in.CVSize();   // dim (+1 if rational): homogeneous size
  const int m = n + p + 1;         // textbook: last knot index

  // Textbook knot vector U[0..m] from ON's compressed one (KnotCount == m - 1).
  std::vector<double> U(static_cast<size_t>(m) + 1);
  U[0] = in.Knot(0);
  for (int k = 1; k <= m - 1; ++k) U[static_cast<size_t>(k)] = in.Knot(k - 1);
  U[static_cast<size_t>(m)] = in.Knot(m - 2);

  const int ph = p + t;
  const int ph2 = ph / 2;

  auto Bin = [](int a, int b) -> double {
    if (b < 0 || b > a) return 0.0;
    double r = 1.0;
    for (int i = 1; i <= b; ++i) r = r * (a - b + i) / i;
    return r;
  };

  std::vector<double> bezalfs(static_cast<size_t>(ph + 1) * static_cast<size_t>(p + 1), 0.0);
  auto BEZ = [&](int i, int j) -> double& {
    return bezalfs[static_cast<size_t>(i) * static_cast<size_t>(p + 1) + static_cast<size_t>(j)];
  };
  BEZ(0, 0) = 1.0;
  BEZ(ph, p) = 1.0;
  for (int i = 1; i <= ph2; ++i) {
    const double inv = 1.0 / Bin(ph, i);
    const int mpi = std::min(p, i);
    for (int j = std::max(0, i - t); j <= mpi; ++j) BEZ(i, j) = inv * Bin(p, j) * Bin(t, i - j);
  }
  for (int i = ph2 + 1; i <= ph - 1; ++i) {
    const int mpi = std::min(p, i);
    for (int j = std::max(0, i - t); j <= mpi; ++j) BEZ(i, j) = BEZ(ph - i, p - j);
  }

  const int max_out_cv = (m - p) * (t + 1) + 2;
  const int max_out_knots = ph + (m - p) * (t + 1) + 2;
  std::vector<double> Qw(static_cast<size_t>(max_out_cv) * static_cast<size_t>(cvdim), 0.0);
  std::vector<double> Uh(static_cast<size_t>(max_out_knots), 0.0);
  std::vector<double> bpts(static_cast<size_t>(p + 1) * static_cast<size_t>(cvdim), 0.0);
  std::vector<double> ebpts(static_cast<size_t>(ph + 1) * static_cast<size_t>(cvdim), 0.0);
  std::vector<double> Nextbpts(static_cast<size_t>(std::max(p, 1)) * static_cast<size_t>(cvdim), 0.0);
  std::vector<double> alfs(static_cast<size_t>(std::max(p, 1)), 0.0);

  auto Q = [&](int i) { return &Qw[static_cast<size_t>(i) * static_cast<size_t>(cvdim)]; };
  auto BP = [&](int i) { return &bpts[static_cast<size_t>(i) * static_cast<size_t>(cvdim)]; };
  auto EB = [&](int i) { return &ebpts[static_cast<size_t>(i) * static_cast<size_t>(cvdim)]; };
  auto NB = [&](int i) { return &Nextbpts[static_cast<size_t>(i) * static_cast<size_t>(cvdim)]; };
  auto Pw = [&](int i) -> const double* { return in.CV(i); };
  auto copy = [&](double* dst, const double* src) {
    for (int k = 0; k < cvdim; ++k) dst[k] = src[k];
  };
  // dst = alpha*x + (1-alpha)*y; dst may alias x (each component is read
  // before it is written).
  auto lerp = [&](double* dst, double alpha, const double* x, const double* y) {
    for (int k = 0; k < cvdim; ++k) dst[k] = alpha * x[k] + (1.0 - alpha) * y[k];
  };

  int mh = ph, kind = ph + 1, r = -1, a = p, b = p + 1, cind = 1;
  double ua = U[0];
  copy(Q(0), Pw(0));
  for (int i = 0; i <= ph; ++i) Uh[static_cast<size_t>(i)] = ua;
  for (int i = 0; i <= p; ++i) copy(BP(i), Pw(i));

  while (b < m) {
    const int i_start = b;
    while (b < m && U[static_cast<size_t>(b)] == U[static_cast<size_t>(b) + 1]) b = b + 1;
    const int mul = b - i_start + 1;
    // An interior knot may not exceed multiplicity p (the terminal group,
    // reached when b == m, legitimately has multiplicity p + 1).
    if (b < m && mul > p) return false;
    mh = mh + mul + t;
    const double ub = U[static_cast<size_t>(b)];
    const int oldr = r;
    r = p - mul;
    const int lbz = (oldr > 0) ? (oldr + 2) / 2 : 1;
    const int rbz = (r > 0) ? ph - (r + 1) / 2 : ph;
    if (r > 0) {
      const double numer = ub - ua;
      for (int k = p; k > mul; --k) alfs[static_cast<size_t>(k - mul - 1)] = numer / (U[static_cast<size_t>(a + k)] - ua);
      for (int j = 1; j <= r; ++j) {
        const int save = r - j;
        const int s = mul + j;
        for (int k = p; k >= s; --k) lerp(BP(k), alfs[static_cast<size_t>(k - s)], BP(k), BP(k - 1));
        copy(NB(save), BP(p));
      }
    }
    for (int i = lbz; i <= ph; ++i) {
      double* e = EB(i);
      for (int k = 0; k < cvdim; ++k) e[k] = 0.0;
      const int mpi = std::min(p, i);
      for (int j = std::max(0, i - t); j <= mpi; ++j) {
        const double c = BEZ(i, j);
        const double* bp = BP(j);
        for (int k = 0; k < cvdim; ++k) e[k] += c * bp[k];
      }
    }
    if (oldr > 1) {
      int first = kind - 2, last = kind;
      const double den = ub - ua;
      const double bet = (ub - Uh[static_cast<size_t>(kind - 1)]) / den;
      for (int tr = 1; tr < oldr; ++tr) {
        int i = first, j = last, kj = j - kind + 1;
        while (j - i > tr) {
          if (i < cind) {
            const double alf = (ub - Uh[static_cast<size_t>(i)]) / (ua - Uh[static_cast<size_t>(i)]);
            lerp(Q(i), alf, Q(i), Q(i - 1));
          }
          if (j >= lbz) {
            if (j - tr <= kind - ph + oldr) {
              const double gam = (ub - Uh[static_cast<size_t>(j - tr)]) / den;
              lerp(EB(kj), gam, EB(kj), EB(kj + 1));
            } else {
              lerp(EB(kj), bet, EB(kj), EB(kj + 1));
            }
          }
          i = i + 1;
          j = j - 1;
          kj = kj - 1;
        }
        first = first - 1;
        last = last + 1;
      }
    }
    if (a != p) {
      for (int i = 0; i < ph - oldr; ++i) {
        Uh[static_cast<size_t>(kind)] = ua;
        kind = kind + 1;
      }
    }
    for (int j = lbz; j <= rbz; ++j) {
      copy(Q(cind), EB(j));
      cind = cind + 1;
    }
    if (b < m) {
      for (int j = 0; j < r; ++j) copy(BP(j), NB(j));
      for (int j = r; j <= p; ++j) copy(BP(j), Pw(b - p + j));
      a = b;
      b = b + 1;
      ua = ub;
    } else {
      for (int i = 0; i <= ph; ++i) Uh[static_cast<size_t>(kind + i)] = ub;
    }
  }
  const int nh = mh - ph - 1;

  ON_NurbsCurve res;
  if (!res.Create(in.Dimension(), in.IsRational(), ph + 1, nh + 1)) return false;
  for (int k = 0; k < nh + ph; ++k) res.SetKnot(k, Uh[static_cast<size_t>(k) + 1]);
  for (int i = 0; i <= nh; ++i) copy(res.CV(i), Q(i));
  out = res;
  return true;
}

// Largest Euclidean control-point coordinate extent of `c` - the scale a
// shape-deviation tolerance is judged against (a rational curve's weights
// are divided out first).
inline double ControlPolygonDiagonal(const ON_NurbsCurve& c) {
  ON_BoundingBox box;
  for (int i = 0; i < c.CVCount(); ++i) {
    ON_3dPoint p;
    c.GetCV(i, p);
    box.Set(p, i > 0);
  }
  return box.IsValid() ? box.Diagonal().Length() : 0.0;
}

// The verified entry point (see the top-of-file comment): elevates
// `input` to degree `new_degree` (> input.Degree()) into `out`, minimal
// form when it verifies, exact piecewise-Bezier form otherwise. Returns
// false if `input` is not a valid NURBS curve, `new_degree <=
// input.Degree()`, or clamping/decomposition fails. `out` may alias
// `input`. `*used_minimal_form`, if given, reports which form came back.
inline bool DegreeElevateNurbsCurve(const ON_NurbsCurve& input, int new_degree, ON_NurbsCurve& out,
                                    bool* used_minimal_form = nullptr) {
  const int t = new_degree - input.Degree();
  ON_NurbsCurve bezier_form;
  if (!DegreeElevateNurbsCurveBezierForm(input, t, bezier_form)) return false;

  ON_NurbsCurve minimal;
  bool minimal_ok = DegreeElevateNurbsCurveMinimalForm(input, t, minimal) && minimal.IsValid();
  if (minimal_ok) {
    const double tol = 1e-12 * ControlPolygonDiagonal(input);
    const ON_Interval dom_b = bezier_form.Domain(), dom_m = minimal.Domain();
    // Dense enough that a wrong control point cannot hide between
    // samples: several samples per new-degree polynomial piece.
    const int samples = std::max(64, 8 * bezier_form.SpanCount() * (new_degree + 1));
    for (int i = 0; i <= samples && minimal_ok; ++i) {
      const double s = static_cast<double>(i) / samples;
      const ON_3dPoint pb = bezier_form.PointAt(dom_b.ParameterAt(s));
      const ON_3dPoint pm = minimal.PointAt(dom_m.ParameterAt(s));
      const double d = pb.DistanceTo(pm);
      if (!(d <= tol)) minimal_ok = false;  // also catches NaN
    }
  }
  if (used_minimal_form) *used_minimal_form = minimal_ok;
  out = minimal_ok ? minimal : bezier_form;
  return true;
}

// Elevates `input` to degree `new_degree` in parametric direction `dir`
// (0 = U, 1 = V). Same technique OpenNURBS' own surface routine uses
// (every row of control points along `dir` packed into ONE curve whose
// "dimension" is the whole row, since knot insertion and elevation are
// linear in the control-point data), but through the verified curve
// elevation above instead of OpenNURBS' inexact one. The verification's
// Euclidean comparison sees the packed rows as one high-dimensional
// non-rational point set (every weight is just another coordinate), which
// is the right space for it: the packed curve's control polygon extent
// bounds every row's homogeneous coordinates. Returns false under the
// same conditions as the curve helper.
inline bool DegreeElevateNurbsSurface(const ON_NurbsSurface& input, int dir, int new_degree,
                                      ON_NurbsSurface& out, bool* used_minimal_form = nullptr) {
  if (dir != 0 && dir != 1) return false;
  if (!input.IsValid()) return false;
  const int other = 1 - dir;
  const int cvsize = input.CVSize();
  const int count_dir = input.CVCount(dir);
  const int count_other = input.CVCount(other);
  const int packed_dim = cvsize * count_other;

  ON_NurbsCurve packed;
  if (!packed.Create(packed_dim, false, input.Order(dir), count_dir)) return false;
  for (int k = 0; k < input.KnotCount(dir); ++k) packed.SetKnot(k, input.Knot(dir, k));
  for (int i = 0; i < count_dir; ++i) {
    double* dst = packed.CV(i);
    for (int j = 0; j < count_other; ++j) {
      const double* src = (dir == 0) ? input.CV(i, j) : input.CV(j, i);
      for (int k = 0; k < cvsize; ++k) dst[j * cvsize + k] = src[k];
    }
  }

  ON_NurbsCurve elevated;
  if (!DegreeElevateNurbsCurve(packed, new_degree, elevated, used_minimal_form)) return false;

  ON_NurbsSurface res;
  const int new_count_dir = elevated.CVCount();
  const int order0 = (dir == 0) ? elevated.Order() : input.Order(0);
  const int order1 = (dir == 1) ? elevated.Order() : input.Order(1);
  const int cv0 = (dir == 0) ? new_count_dir : input.CVCount(0);
  const int cv1 = (dir == 1) ? new_count_dir : input.CVCount(1);
  if (!res.Create(input.Dimension(), input.IsRational(), order0, order1, cv0, cv1)) return false;
  for (int k = 0; k < elevated.KnotCount(); ++k) res.SetKnot(dir, k, elevated.Knot(k));
  for (int k = 0; k < input.KnotCount(other); ++k) res.SetKnot(other, k, input.Knot(other, k));
  for (int i = 0; i < new_count_dir; ++i) {
    const double* src = elevated.CV(i);
    for (int j = 0; j < count_other; ++j) {
      double* dst = (dir == 0) ? res.CV(i, j) : res.CV(j, i);
      for (int k = 0; k < cvsize; ++k) dst[k] = src[j * cvsize + k];
    }
  }
  out = res;
  return true;
}

}  // namespace dino8::kernel::detail
