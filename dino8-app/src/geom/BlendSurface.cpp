#include "geom/BlendSurface.h"

#include <algorithm>
#include <cmath>

#include <opennurbs.h>

namespace dino8::app {

namespace {

double ClampImpl(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Builds the banded collocation system once (rows share params/knots) and
// solves it for however many scalar channels are passed in `channels`
// (each entry: n values, one per row). Same technique as
// SurfaceIntersect.cpp's InterpolateCubic.
bool SolveGlobalInterpolation(const std::vector<double>& params, int order, std::vector<std::vector<double>>& channels, std::vector<double>& knots) {
  const int n = static_cast<int>(params.size());
  if (n < 2 || order < 2) return false;
  const int p = order - 1;
  knots.assign(static_cast<size_t>(n + order), 0.0);
  for (int i = 0; i <= p; ++i) { knots[static_cast<size_t>(i)] = params.front(); knots[static_cast<size_t>(n + i)] = params.back(); }
  for (int j = 1; j <= n - p - 1; ++j) {
    double s = 0;
    for (int k = j; k < j + p; ++k) s += params[static_cast<size_t>(k)];
    knots[static_cast<size_t>(p + j)] = s / p;
  }
  std::vector<double> knot(knots.begin() + 1, knots.end() - 1);  // OpenNURBS-style (n+order-2 knots)
  const int bw = 2 * p + 1;
  std::vector<double> A(static_cast<size_t>(n) * bw, 0);
  auto at = [&](int r, int c) -> double& { return A[static_cast<size_t>(r) * bw + (c - r + p)]; };
  // See SurfaceIntersect.cpp's InterpolateCubic: ON_EvaluateNurbsBasis needs
  // an order*order scratch buffer, not just `order`, or it overflows.
  std::vector<double> N(static_cast<size_t>(order) * static_cast<size_t>(order));
  for (int k = 0; k < n; ++k) {
    const double t = params[static_cast<size_t>(k)];
    const int span = ON_NurbsSpanIndex(order, n, knot.data(), t, 0, 0);
    ON_EvaluateNurbsBasis(order, knot.data() + span, t, N.data());
    for (int j = 0; j < order; ++j) {
      const int c = span + j;
      if (c - k + p < 0 || c - k + p >= bw) continue;
      at(k, c) = N[static_cast<size_t>(j)];
    }
  }
  for (int k = 0; k < n; ++k) {
    const double piv = at(k, k);
    if (std::fabs(piv) < 1e-300) continue;
    for (int r = k + 1; r <= std::min(n - 1, k + p); ++r) {
      const double f = at(r, k) / piv;
      if (f == 0) continue;
      for (int c = k; c <= std::min(n - 1, k + p); ++c) at(r, c) -= f * at(k, c);
      for (auto& ch : channels) ch[static_cast<size_t>(r)] -= f * ch[static_cast<size_t>(k)];
    }
  }
  for (auto& ch : channels) {
    std::vector<double> sol(static_cast<size_t>(n), 0);
    for (int r = n - 1; r >= 0; --r) {
      double s = ch[static_cast<size_t>(r)];
      for (int c = r + 1; c <= std::min(n - 1, r + p); ++c) s -= at(r, c) * sol[static_cast<size_t>(c)];
      const double piv = at(r, r);
      sol[static_cast<size_t>(r)] = std::fabs(piv) < 1e-300 ? 0 : s / piv;
    }
    ch = sol;
  }
  knots = knot;
  return true;
}

}  // namespace

bool LoftRows(const std::vector<HomogeneousRow>& rows, const std::vector<double>& params_in, int u_order, ON_NurbsSurface& out) {
  const int nv = static_cast<int>(rows.size());
  if (nv < 2) return false;
  const int nu = static_cast<int>(rows.front().cv.size());
  for (const auto& r : rows) if (static_cast<int>(r.cv.size()) != nu) return false;
  std::vector<double> params = params_in;
  const int v_order = std::min(4, nv);
  std::vector<std::vector<double>> channels(static_cast<size_t>(nu) * 4, std::vector<double>(static_cast<size_t>(nv), 0.0));
  for (int j = 0; j < nv; ++j)
    for (int i = 0; i < nu; ++i) {
      const ON_4dPoint& cv = rows[static_cast<size_t>(j)].cv[static_cast<size_t>(i)];
      channels[static_cast<size_t>(i) * 4 + 0][static_cast<size_t>(j)] = cv.x;
      channels[static_cast<size_t>(i) * 4 + 1][static_cast<size_t>(j)] = cv.y;
      channels[static_cast<size_t>(i) * 4 + 2][static_cast<size_t>(j)] = cv.z;
      channels[static_cast<size_t>(i) * 4 + 3][static_cast<size_t>(j)] = cv.w;
    }
  std::vector<double> v_knots;
  if (!SolveGlobalInterpolation(params, v_order, channels, v_knots)) return false;
  out.Create(3, true, u_order, v_order, nu, nv);
  // u knots: a single clamped span per the (assumed Bezier) row representation.
  for (int i = 0; i < nu + u_order - 2; ++i) out.SetKnot(0, i, i < u_order - 1 ? 0.0 : 1.0);
  for (int i = 0; i < nv + v_order - 2; ++i) out.SetKnot(1, i, v_knots[static_cast<size_t>(i)]);
  for (int i = 0; i < nu; ++i)
    for (int j = 0; j < nv; ++j) {
      const double w = channels[static_cast<size_t>(i) * 4 + 3][static_cast<size_t>(j)];
      out.SetCV(i, j, ON_4dPoint(channels[static_cast<size_t>(i) * 4 + 0][static_cast<size_t>(j)], channels[static_cast<size_t>(i) * 4 + 1][static_cast<size_t>(j)], channels[static_cast<size_t>(i) * 4 + 2][static_cast<size_t>(j)], w));
    }
  return true;
}

Vector3d InteriorDirection3d(const ON_Surface& s, double u, double v) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  double duu = du.Mid() - u, dvv = dv.Mid() - v;
  const double len = std::hypot(duu, dvv);
  if (len < 1e-12) { duu = 1; dvv = 0; } else { duu /= len; dvv /= len; }
  const double step = 1e-3 * std::max(du.Length(), dv.Length());
  const Point3d p0 = s.PointAt(u, v);
  const Point3d p1 = s.PointAt(ClampImpl(u + duu * step, du.Min(), du.Max()), ClampImpl(v + dvv * step, dv.Min(), dv.Max()));
  Vector3d d = p1 - p0;
  if (!d.Unitize()) d = s.NormalAt(u, v);
  return d;
}

DirectionalDerivs OutwardDirectionalDerivs(const ON_Surface& s, double u, double v) {
  DirectionalDerivs r;
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  // Outward = away from the domain's own interior/centre - the direction a
  // blend row leaves this surface's edge (the negation of the classic
  // "into the face" probe direction).
  double a = u - du.Mid(), b = v - dv.Mid();
  const double len = std::hypot(a, b);
  if (len < 1e-12) { a = 1; b = 0; } else { a /= len; b /= len; }
  ON_3dPoint p;
  ON_3dVector su, sv, suu, suv, svv;
  if (!s.Ev2Der(u, v, p, su, sv, suu, suv, svv)) return r;
  r.d1 = su * a + sv * b;
  r.d2 = suu * (a * a) + suv * (2.0 * a * b) + svv * (b * b);
  r.dir = r.d1;
  if (!r.dir.Unitize()) r.dir = InteriorDirection3d(s, u, v) * -1.0;
  r.ok = true;
  return r;
}

Vector3d CurvatureVectorFromDerivs(const Vector3d& d1, const Vector3d& d2) {
  const double len2 = d1.LengthSquared();
  if (len2 < 1e-300) return Vector3d(0, 0, 0);
  const Vector3d perp = d2 - d1 * (ON_DotProduct(d1, d2) / len2);
  return perp / len2;
}

namespace {

// Cubic-Bezier (Hermite) row between pA (surface a) and pB (surface b),
// with out-of-face tangents scaled by `mag` (roughly a third of the
// corner gap, as is standard for a visually fair Hermite-to-Bezier
// conversion). This is the G1 path only - no second-derivative
// information is used or matched.
HomogeneousRow HermiteRowG1(Point3d pa, Vector3d out_a, Point3d pb, Vector3d out_b, double mag) {
  HomogeneousRow row;
  const Point3d c0 = pa, c3 = pb;
  const Point3d c1 = pa + out_a * mag, c2 = pb + out_b * mag;
  row.cv = {ON_4dPoint(c0.x, c0.y, c0.z, 1), ON_4dPoint(c1.x, c1.y, c1.z, 1), ON_4dPoint(c2.x, c2.y, c2.z, 1), ON_4dPoint(c3.x, c3.y, c3.z, 1)};
  return row;
}

// Quintic-Bezier row matching position, first AND second derivative at
// both ends. Given C'(0) = ta, C''(0) = ka (and similarly at t = 1):
//   P0 = pa
//   P1 = P0 + ta/5
//   P2 = P0 + 2*ta/5 + ka/20        (from C''(0) = 20*(P2 - 2*P1 + P0))
//   P5 = pb
//   P4 = P5 + tb/5
//   P3 = P5 + 2*tb/5 + kb/20        (mirrored: C''(1) = 20*(P5 - 2*P4 + P3))
// (ta/tb both point outward from their own surface, matching
// HermiteRowG1's sign convention for P1/P4.)
HomogeneousRow HermiteRowG2(Point3d pa, Vector3d ta, Vector3d ka, Point3d pb, Vector3d tb, Vector3d kb) {
  HomogeneousRow row;
  const Point3d p0 = pa;
  const Point3d p1 = p0 + ta / 5.0;
  const Point3d p2 = p0 + ta * (2.0 / 5.0) + ka / 20.0;
  const Point3d p5 = pb;
  const Point3d p4 = p5 + tb / 5.0;
  const Point3d p3 = p5 + tb * (2.0 / 5.0) + kb / 20.0;
  row.cv = {ON_4dPoint(p0.x, p0.y, p0.z, 1), ON_4dPoint(p1.x, p1.y, p1.z, 1), ON_4dPoint(p2.x, p2.y, p2.z, 1),
            ON_4dPoint(p3.x, p3.y, p3.z, 1), ON_4dPoint(p4.x, p4.y, p4.z, 1), ON_4dPoint(p5.x, p5.y, p5.z, 1)};
  return row;
}

}  // namespace

bool BuildBlendSurfaceG1(const ON_Curve& ea, const ON_Surface& sa, const std::function<ON_2dPoint(double)>& uv_a_at, const ON_Curve& eb, const ON_Surface& sb, const std::function<ON_2dPoint(double)>& uv_b_at, bool tangent_boost, int samples, ON_NurbsSurface& out) {
  std::vector<HomogeneousRow> rows;
  std::vector<double> params;
  const ON_Interval da = ea.Domain();
  for (int i = 0; i <= samples; ++i) {
    const double t = static_cast<double>(i) / samples;
    const double ta = da.ParameterAt(t);
    const Point3d pa = ea.PointAt(ta);
    const ON_2dPoint uva = uv_a_at(t);
    const ON_2dPoint uvb = uv_b_at(t);
    const Point3d pb = sb.PointAt(uvb.x, uvb.y);
    Vector3d out_a = InteriorDirection3d(sa, uva.x, uva.y) * -1.0;
    Vector3d out_b = InteriorDirection3d(sb, uvb.x, uvb.y) * -1.0;
    const double gap = pa.DistanceTo(pb);
    const double mag = gap * (tangent_boost ? 0.55 : 0.35);
    rows.push_back(HermiteRowG1(pa, out_a, pb, out_b, mag));
    params.push_back(t);
  }
  (void)eb;
  return LoftRows(rows, params, 4, out);
}

bool BuildBlendSurfaceG2(const ON_Curve& ea, const ON_Surface& sa, const std::function<ON_2dPoint(double)>& uv_a_at, const ON_Curve& eb, const ON_Surface& sb, const std::function<ON_2dPoint(double)>& uv_b_at, int samples, ON_NurbsSurface& out) {
  std::vector<HomogeneousRow> rows;
  std::vector<double> params;
  const ON_Interval da = ea.Domain();
  for (int i = 0; i <= samples; ++i) {
    const double t = static_cast<double>(i) / samples;
    const double ta = da.ParameterAt(t);
    const Point3d pa = ea.PointAt(ta);
    const ON_2dPoint uva = uv_a_at(t);
    const ON_2dPoint uvb = uv_b_at(t);
    const Point3d pb = sb.PointAt(uvb.x, uvb.y);
    const double gap = pa.DistanceTo(pb);
    const double mag = gap * 0.55;

    const DirectionalDerivs dda = OutwardDirectionalDerivs(sa, uva.x, uva.y);
    const DirectionalDerivs ddb = OutwardDirectionalDerivs(sb, uvb.x, uvb.y);

    // Scale the (d1, d2) pair by the same positive constant per side so
    // the tangent has the usual `mag` visual magnitude while the second
    // derivative is scaled consistently - CurvatureVectorFromDerivs's own
    // comment is exactly why this preserves the true curvature vector.
    // If a side's own d1 is degenerate (a singular parametrization point,
    // e.g. a surface pole), fall back to the plain unit outward direction
    // with zero second derivative (same as the G1 path at that one row) -
    // still G1 there, honestly not G2, rather than dividing by ~0.
    Vector3d ta_vec, ka_vec, tb_vec, kb_vec;
    if (dda.ok && dda.d1.Length() > 1e-9) {
      const double c = mag / dda.d1.Length();
      ta_vec = dda.d1 * c;
      ka_vec = dda.d2 * (c * c);
    } else {
      ta_vec = (InteriorDirection3d(sa, uva.x, uva.y) * -1.0) * mag;
      ka_vec = Vector3d(0, 0, 0);
    }
    if (ddb.ok && ddb.d1.Length() > 1e-9) {
      const double c = mag / ddb.d1.Length();
      tb_vec = ddb.d1 * c;
      kb_vec = ddb.d2 * (c * c);
    } else {
      tb_vec = (InteriorDirection3d(sb, uvb.x, uvb.y) * -1.0) * mag;
      kb_vec = Vector3d(0, 0, 0);
    }

    rows.push_back(HermiteRowG2(pa, ta_vec, ka_vec, pb, tb_vec, kb_vec));
    params.push_back(t);
  }
  (void)eb;
  return LoftRows(rows, params, 6, out);
}

}  // namespace dino8::app
