// Exact offset / shell of a RIGHT PRISM: a solid with two parallel flat caps and a wall that is
// everything the profile curve sweeps along the prism axis - the shape of an extruded polyline
// (an L, a U, a plus), of a box with one or more PARALLEL edges rounded by FilletEdge (planar
// faces plus cylindrical strips), and of a rounded slab. The construction is the analytic one:
// every wall face is moved by the offset distance (plane -> parallel plane, cylinder ->
// concentric cylinder), the new joints between neighbouring wall faces are the exact
// intersections of the offset faces (plane/plane -> line, tangent plane/cylinder and
// cylinder/cylinder joints stay tangent), and the flat caps move along the axis. All of that
// happens on the 2D profile (lines and circular arcs), which is then extruded back.
//
// The profile is read from the bottom cap's outer boundary curves; the wall faces are only
// checked (normals perpendicular to the axis, sampled) and the whole recognition is
// cross-checked against the solid's own volume (profile area * height). The offset is refused,
// with a reason, whenever a profile edge collapses or reverses, a curved edge's radius reaches
// zero, the offset profile would touch or cross itself or come closer than |d| to the original,
// or a joint is not one of the supported analytic pairs (line/line miter, any tangent joint).
#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "commands/cmd_common.h"

namespace dino8::app::prism {

struct V2 { double x = 0, y = 0; };
inline V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
inline V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
inline V2 operator*(double k, V2 a) { return {k * a.x, k * a.y}; }
inline double Dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
inline double Cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
inline double Len(V2 a) { return std::sqrt(Dot(a, a)); }
inline V2 Unit(V2 a) { const double l = Len(a); return l > 0 ? V2{a.x / l, a.y / l} : a; }
inline V2 Right(V2 t) { return {t.y, -t.x}; }

// One profile edge, in the profile plane. Arcs: centre c, radius r, s = +1 when the profile
// walks it counter-clockwise (the centre lies on the material side: a convex round), -1 when
// clockwise (a concave round, the centre outside the material).
struct Seg {
  bool arc = false;
  V2 a, b, c;
  double r = 0;
  int s = 1;
};

inline double Sweep(const Seg& g) {
  const double p0 = std::atan2(g.a.y - g.c.y, g.a.x - g.c.x), p1 = std::atan2(g.b.y - g.c.y, g.b.x - g.c.x);
  double d = g.s > 0 ? p1 - p0 : p0 - p1;
  while (d <= 0) d += 2 * ON_PI;
  while (d > 2 * ON_PI) d -= 2 * ON_PI;
  return d;
}
inline V2 StartTangent(const Seg& g) {
  if (!g.arc) return Unit(g.b - g.a);
  const V2 q = g.a - g.c;
  return g.s > 0 ? Unit(V2{-q.y, q.x}) : Unit(V2{q.y, -q.x});
}
inline V2 EndTangent(const Seg& g) {
  if (!g.arc) return Unit(g.b - g.a);
  const V2 q = g.b - g.c;
  return g.s > 0 ? Unit(V2{-q.y, q.x}) : Unit(V2{q.y, -q.x});
}

// Exact signed area of the closed profile (counter-clockwise positive).
inline double SignedArea(const std::vector<Seg>& P) {
  double A = 0;
  for (const Seg& g : P) {
    A += 0.5 * Cross(g.a, g.b);
    if (g.arc) { const double th = Sweep(g); A += g.s * 0.5 * g.r * g.r * (th - std::sin(th)); }
  }
  return A;
}
inline double Perimeter(const std::vector<Seg>& P) {
  double L = 0;
  for (const Seg& g : P) L += g.arc ? g.r * Sweep(g) : Len(g.b - g.a);
  return L;
}

inline std::vector<Seg> ReverseLoop(std::vector<Seg> L) {
  std::reverse(L.begin(), L.end());
  for (Seg& g : L) { std::swap(g.a, g.b); g.s = -g.s; }
  return L;
}

inline std::vector<V2> Polyline(const Seg& g, int per_circle = 96) {
  std::vector<V2> pts{g.a};
  if (g.arc) {
    const double th = Sweep(g);
    const int n = std::max(4, static_cast<int>(std::ceil(per_circle * th / (2 * ON_PI))));
    const double p0 = std::atan2(g.a.y - g.c.y, g.a.x - g.c.x);
    for (int k = 1; k < n; ++k) { const double p = p0 + g.s * th * k / n; pts.push_back(g.c + V2{g.r * std::cos(p), g.r * std::sin(p)}); }
  }
  pts.push_back(g.b);
  return pts;
}

inline double DistToSeg(const Seg& g, V2 p) {
  if (!g.arc) {
    const V2 e = g.b - g.a;
    const double l2 = Dot(e, e);
    const double t = l2 > 0 ? std::clamp(Dot(p - g.a, e) / l2, 0.0, 1.0) : 0.0;
    return Len(p - (g.a + t * e));
  }
  const double th = Sweep(g);
  const double p0 = std::atan2(g.a.y - g.c.y, g.a.x - g.c.x), pp = std::atan2(p.y - g.c.y, p.x - g.c.x);
  double u = g.s > 0 ? pp - p0 : p0 - pp;
  while (u < 0) u += 2 * ON_PI;
  while (u >= 2 * ON_PI) u -= 2 * ON_PI;
  if (u <= th) return std::fabs(Len(p - g.c) - g.r);
  return std::min(Len(p - g.a), Len(p - g.b));
}

inline bool SegsCross(V2 p, V2 q, V2 r, V2 s) {
  const double d1 = Cross(q - p, r - p), d2 = Cross(q - p, s - p), d3 = Cross(s - r, p - r), d4 = Cross(s - r, q - r);
  return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

// Offsets the closed counter-clockwise profile outward by d (negative = inward), sharp corners.
inline std::optional<std::vector<Seg>> OffsetLoop(const std::vector<Seg>& P, double d, double scale, std::string& why) {
  const size_t n = P.size();
  const double tol = scale * 1e-9;
  std::vector<V2> J(n);
  for (size_t i = 0; i < n; ++i) {
    const Seg& prev = P[(i + n - 1) % n];
    const Seg& cur = P[i];
    const V2 t1 = EndTangent(prev), t2 = StartTangent(cur);
    const V2 pt = cur.a;
    if (std::fabs(Cross(t1, t2)) < 1e-7 && Dot(t1, t2) > 0) { J[i] = pt + d * Right(t2); continue; }
    if (prev.arc || cur.arc) { why = "a sharp corner next to a curved edge has no exact offset (only line/line corners and tangent joints are supported)"; return std::nullopt; }
    if (Dot(t1, t2) < -1 + 1e-9) { why = "a cusp (a 180-degree reversal) in the profile"; return std::nullopt; }
    const V2 n1 = Right(t1), n2 = Right(t2);
    J[i] = pt + (d / (1 + Dot(n1, n2))) * (n1 + n2);
  }
  std::vector<Seg> Q(n);
  for (size_t i = 0; i < n; ++i) {
    const Seg& g = P[i];
    Seg h = g;
    h.a = J[i];
    h.b = J[(i + 1) % n];
    if (g.arc) {
      h.r = g.r + g.s * d;
      if (!(h.r > scale * 1e-6)) { why = "a rounded edge would collapse (its radius reaches zero at that distance)"; return std::nullopt; }
      const double th = Sweep(g), th2 = Sweep(h);
      if (std::fabs(th - th2) > 1e-6 || std::fabs(Len(h.a - h.c) - h.r) > scale * 1e-7 || std::fabs(Len(h.b - h.c) - h.r) > scale * 1e-7) {
        why = "a rounded edge would not stay a concentric arc at that distance";
        return std::nullopt;
      }
    } else {
      const V2 t = Unit(g.b - g.a);
      const double len = Dot(h.b - h.a, t);
      if (!(len > scale * 1e-6)) { why = "a flat edge of the profile would collapse or reverse at that distance"; return std::nullopt; }
      if (std::fabs(Cross(h.b - h.a, t)) > scale * 1e-7) { why = "an offset flat edge lost its direction"; return std::nullopt; }
    }
    Q[i] = h;
  }
  // Independent geometric validation of the offset loop against the original one.
  std::vector<V2> poly;
  std::vector<size_t> owner;
  for (size_t i = 0; i < n; ++i) {
    std::vector<V2> pts = Polyline(Q[i], 128);
    for (size_t k = 0; k + 1 < pts.size(); ++k) { poly.push_back(pts[k]); owner.push_back(i); }
  }
  const size_t m = poly.size();
  for (size_t i = 0; i < m; ++i) {
    for (size_t j = i + 2; j < m; ++j) {
      if (i == 0 && j == m - 1) continue;
      if (SegsCross(poly[i], poly[(i + 1) % m], poly[j], poly[(j + 1) % m])) { why = "the offset profile would cross itself at that distance"; return std::nullopt; }
    }
  }
  const double ad = std::fabs(d);
  for (const V2& p : poly) {
    double best = 1e300;
    for (const Seg& g : P) best = std::min(best, DistToSeg(g, p));
    if (best < ad * (1 - 1e-6) - scale * 1e-9) { why = "the offset profile would come closer than the offset distance to the original (a feature thinner than the offset)"; return std::nullopt; }
  }
  if (SignedArea(Q) <= 0) { why = "the offset profile has no area left"; return std::nullopt; }
  (void)tol;
  return Q;
}

struct Prism {
  ON_3dPoint O;               // origin of the profile plane (on the bottom cap)
  ON_3dVector n, X, Y;        // axis (bottom -> top) and the profile plane's in-plane axes, X x Y = n
  double H = 0;               // height between the caps
  std::vector<Seg> prof;      // outer profile, counter-clockwise about n
};

inline ON_3dPoint To3(const Prism& pr, V2 p, double h) { return pr.O + p.x * pr.X + p.y * pr.Y + h * pr.n; }

// Splits an edge curve into straight lines and circular arcs (pieces of the 3D curve), in the
// profile plane's 2D coordinates. false if any piece is neither.
inline bool Explode(const ON_Curve& c, const Prism& pr, double tol, std::vector<Seg>& out) {
  auto to2 = [&](const ON_3dPoint& p) { const ON_3dVector v = p - pr.O; return V2{v * pr.X, v * pr.Y}; };
  if (c.IsLinear(tol)) {
    Seg g;
    g.a = to2(c.PointAtStart());
    g.b = to2(c.PointAtEnd());
    out.push_back(g);
    return true;
  }
  ON_Arc arc;
  if (c.IsArc(nullptr, &arc, tol)) {
    Seg g;
    g.arc = true;
    g.a = to2(c.PointAtStart());
    g.b = to2(c.PointAtEnd());
    g.c = to2(arc.Center());
    g.r = arc.Radius();
    const V2 m1 = to2(c.PointAt(c.Domain().ParameterAt(1.0 / 3.0))), m2 = to2(c.PointAt(c.Domain().ParameterAt(2.0 / 3.0)));
    g.s = Cross(m1 - g.a, m2 - m1) > 0 ? 1 : -1;
    out.push_back(g);
    return true;
  }
  ON_NurbsCurve nc;
  if (!c.GetNurbForm(nc) || nc.m_dim < 2) return false;
  if (nc.m_order == 2 && !nc.IsRational()) {
    for (int i = 0; i + 1 < nc.m_cv_count; ++i) {
      ON_3dPoint p, q;
      nc.GetCV(i, p);
      nc.GetCV(i + 1, q);
      if (p.DistanceTo(q) <= tol) continue;
      Seg g;
      g.a = to2(p);
      g.b = to2(q);
      out.push_back(g);
    }
    return !out.empty();
  }
  // Split at the interior knots and classify each span.
  std::vector<double> ks;
  const int kc = nc.KnotCount();
  for (int i = nc.m_order - 1; i < kc - nc.m_order + 1; ++i) {
    const double k = nc.m_knot[i];
    if (ks.empty() || k > ks.back() + 1e-12) ks.push_back(k);
  }
  if (ks.size() < 3) return false;
  for (size_t i = 0; i + 1 < ks.size(); ++i) {
    std::unique_ptr<ON_Curve> piece(nc.DuplicateCurve());
    if (!piece->Trim(ON_Interval(ks[i], ks[i + 1]))) return false;
    if (piece->IsLinear(tol)) {
      Seg g;
      g.a = to2(piece->PointAtStart());
      g.b = to2(piece->PointAtEnd());
      out.push_back(g);
    } else if (ON_Arc a2; piece->IsArc(nullptr, &a2, tol)) {
      Seg g;
      g.arc = true;
      g.a = to2(piece->PointAtStart());
      g.b = to2(piece->PointAtEnd());
      g.c = to2(a2.Center());
      g.r = a2.Radius();
      const V2 m1 = to2(piece->PointAt(piece->Domain().ParameterAt(1.0 / 3.0))), m2 = to2(piece->PointAt(piece->Domain().ParameterAt(2.0 / 3.0)));
      g.s = Cross(m1 - g.a, m2 - m1) > 0 ? 1 : -1;
      out.push_back(g);
    } else {
      return false;
    }
  }
  return true;
}

// Recognises a right prism with ONE outer profile. On success `pr` holds the profile; otherwise
// `why` says what is not a prism (and `not_prism` tells a plain "this is not that shape").
inline bool Recognize(const ON_Brep& b, Prism& pr, std::string& why, bool& not_prism) {
  not_prism = true;
  if (!b.IsSolid()) { why = "the polysurface is not a closed solid"; return false; }
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double diag = std::max(1e-9, bb.Diagonal().Length());
  const double tol = diag * 1e-6;
  const int nf = b.m_F.Count();
  if (nf < 3) { why = "not a prism"; return false; }
  std::vector<int> pf;
  std::vector<ON_Plane> pl;
  for (int f = 0; f < nf; ++f) {
    const ON_Surface* s = b.m_F[f].SurfaceOf();
    ON_Plane p;
    if (s && s->IsPlanar(&p, tol)) { pf.push_back(f); pl.push_back(p); }
  }
  for (size_t i = 0; i < pf.size(); ++i) {
    for (size_t j = i + 1; j < pf.size(); ++j) {
      ON_3dVector ni = pl[i].zaxis, nj = pl[j].zaxis;
      if (std::fabs(ni * nj) < 1.0 - 1e-9) continue;
      const double sep = (pl[j].origin - pl[i].origin) * ni;
      if (std::fabs(sep) < tol * 10) continue;
      // Every other face must run along ni: its normal is perpendicular to the axis everywhere.
      bool ok = true;
      for (int f = 0; f < nf && ok; ++f) {
        if (f == pf[i] || f == pf[j]) continue;
        const ON_Surface* s = b.m_F[f].SurfaceOf();
        if (!s) { ok = false; break; }
        const ON_Interval du = s->Domain(0), dv = s->Domain(1);
        for (int a = 0; a <= 6 && ok; ++a) {
          for (int c = 0; c <= 6 && ok; ++c) {
            const ON_3dVector nrm = s->NormalAt(du.ParameterAt(0.02 + 0.96 * a / 6.0), dv.ParameterAt(0.02 + 0.96 * c / 6.0));
            if (nrm.Length() < 0.5 || std::fabs(nrm * ni) > 1e-6) ok = false;
          }
        }
      }
      if (!ok) continue;
      // bottom = the cap with the smaller coordinate along ni.
      int bot = pf[i], top = pf[j];
      ON_Plane pb = pl[i];
      double H = sep;
      if (sep < 0) { std::swap(bot, top); pb = pl[j]; H = -sep; }
      pr.n = ni;
      pr.O = pb.origin;
      pr.H = H;
      pr.X = pb.xaxis;
      pr.Y = pb.yaxis;
      if ((ON_CrossProduct(pr.X, pr.Y) * pr.n) < 0) pr.Y = -pr.Y;
      if (b.m_F[bot].LoopCount() != 1 || b.m_F[top].LoopCount() != 1) {
        why = "a flat cap of this prism has a hole in it (or a keyhole loop, as a Boolean leaves)";
        not_prism = false;
        return false;
      }
      double area = 0;
      int outer_count = 0;
      for (int li = 0; li < b.m_F[bot].LoopCount(); ++li) {
        const ON_BrepLoop& lp = b.m_L[b.m_F[bot].m_li[li]];
        std::vector<Seg> segs;
        for (int k = 0; k < lp.m_ti.Count(); ++k) {
          const ON_BrepTrim& tr = b.m_T[lp.m_ti[k]];
          const ON_BrepEdge& e = b.m_E[tr.m_ei];
          const ON_Curve* c3 = b.m_C3[e.m_c3i];
          if (!c3) { why = "an edge of the cap has no 3D curve"; return false; }
          std::unique_ptr<ON_Curve> ec(c3->DuplicateCurve());
          if (!ec->Trim(e.ProxyCurveDomain())) { why = "could not isolate a cap edge"; return false; }
          if (e.ProxyCurveIsReversed()) ec->Reverse();
          if (tr.m_bRev3d) ec->Reverse();
          std::vector<Seg> part;
          if (!Explode(*ec, pr, tol, part)) { why = "a cap edge is neither a straight line nor a circular arc"; not_prism = false; return false; }
          for (const Seg& g : part) segs.push_back(g);
        }
        if (segs.size() < 3) { why = "a cap boundary loop has too few edges"; not_prism = false; return false; }
        for (size_t k = 0; k < segs.size(); ++k) {
          const Seg& g = segs[k];
          const Seg& h = segs[(k + 1) % segs.size()];
          if (Len(g.b - h.a) > tol * 50) { why = "the cap boundary is not one clean closed loop of straight lines and circular arcs (a Boolean result carries sampled, keyhole-joined loops)"; not_prism = false; return false; }
        }
        const bool is_outer = lp.m_type == ON_BrepLoop::outer;
        if ((SignedArea(segs) > 0) != is_outer) segs = ReverseLoop(segs);
        area += SignedArea(segs);
        if (is_outer) { pr.prof = segs; ++outer_count; }
      }
      if (outer_count != 1) { why = "the cap does not have exactly one outer boundary loop"; not_prism = false; return false; }
      // Whole-solid cross-check: volume == profile area * height.
      BrepMeshOptions opt;
      opt.chord_tolerance = std::max(1e-6, diag * 2e-4);
      const double vol = std::fabs(MeshBrepClosed(b, opt).Volume());
      const double expect = area * H;
      if (!(expect > 0) || !(std::fabs(vol - expect) <= 5e-3 * expect)) {
        why = "the solid is not a right prism over its cap boundary (volume check failed)";
        not_prism = false;
        return false;
      }
      not_prism = false;
      return true;
    }
  }
  why = "not a right prism (needs two parallel flat caps and a wall running straight between them)";
  return false;
}

// The 3D curve of one profile edge at height h0 (a line or an arc through three points).
inline ON_Curve* SegCurve(const Prism& pr, const Seg& g, double h0) {
  if (!g.arc) return new ON_LineCurve(To3(pr, g.a, h0), To3(pr, g.b, h0));
  const double th = Sweep(g);
  const double p0 = std::atan2(g.a.y - g.c.y, g.a.x - g.c.x);
  const double pm = p0 + g.s * th * 0.5;
  const V2 mid = g.c + V2{g.r * std::cos(pm), g.r * std::sin(pm)};
  ON_Arc arc(To3(pr, g.a, h0), To3(pr, mid, h0), To3(pr, g.b, h0));
  return arc.IsValid() ? new ON_ArcCurve(arc) : nullptr;
}

// A circular through-hole of the extrusion: centre in the profile plane, radius.
struct Hole2 { V2 c; double r = 0; };

// Builds the closed solid: the profile `prof` extruded from height h0 to h0 + H along the axis;
// with `hole`, the end faces carry that circular hole and the extrusion adds its cylindrical wall.
inline std::optional<kernel::Brep> Build(const Prism& pr, const std::vector<Seg>& prof, double h0, double H,
                                         const std::optional<Hole2>& hole = std::nullopt) {
  if (!(H > 0)) return std::nullopt;
  ON_SimpleArray<ON_Curve*> bnd;
  for (const Seg& g : prof) {
    ON_Curve* c = SegCurve(pr, g, h0);
    if (!c) { for (int i = 0; i < bnd.Count(); ++i) delete bnd[i]; return std::nullopt; }
    bnd.Append(c);
  }
  ON_Plane plane(To3(pr, V2{0, 0}, h0), pr.X, pr.Y);
  ON_Brep* br = ON_BrepTrimmedPlane(plane, bnd, false);
  if (!br) { for (int i = 0; i < bnd.Count(); ++i) delete bnd[i]; return std::nullopt; }
  if (hole) {
    ON_Plane hp(To3(pr, hole->c, h0), pr.X, pr.Y);
    ON_ArcCurve* hc = new ON_ArcCurve(ON_Circle(hp, hole->r));
    double outer_area = 0;  // the hole runs opposite to the outer boundary
    if (const ON_BrepLoop* ol = br->m_F[0].OuterLoop()) {
      for (int ti = 0; ti < ol->TrimCount(); ++ti) {
        const ON_BrepTrim* t = ol->Trim(ti);
        ON_3dPoint a = t->Edge()->PointAtStart(), c = t->Edge()->PointAtEnd();
        if (t->m_bRev3d) std::swap(a, c);
        outer_area += ON_CrossProduct(a - plane.origin, c - plane.origin) * pr.n;
      }
    }
    if (outer_area > 0) hc->Reverse();
    ON_SimpleArray<ON_Curve*> loop;
    loop.Append(hc);
    if (!br->NewPlanarFaceLoop(0, ON_BrepLoop::inner, loop, false)) { delete hc; delete br; return std::nullopt; }
    br->Compact();
  }
  ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + H * pr.n));
  if (ON_BrepExtrudeFace(*br, 0, path, true) < 0) { delete br; return std::nullopt; }
  kernel::Brep k;
  k.raw() = *br;
  delete br;
  if (!k.raw().IsSolid() || !k.raw().IsValid()) return std::nullopt;
  BrepMeshOptions opt;
  opt.chord_tolerance = 0.05;
  if (MeshBrepClosed(k.raw(), opt).Volume() < 0) k.raw().Flip();
  if (!k.raw().IsSolid() || !k.raw().IsValid()) return std::nullopt;
  return k;
}

inline double Scale(const Prism& pr) {
  ON_BoundingBox bb;
  for (const Seg& g : pr.prof) { bb.Set(To3(pr, g.a, 0), true); bb.Set(To3(pr, g.b, 0), true); }
  return std::max({bb.Diagonal().Length(), pr.H, 1e-9});
}

// The outward offset of the whole prism by d (negative = inward): profile offset, caps moved.
inline std::optional<kernel::Brep> OffsetSolid(const Prism& pr, double d, std::string& why) {
  const double sc = Scale(pr);
  if (!(pr.H + 2 * d > sc * 1e-6)) { why = "the prism would collapse between its caps at that offset"; return std::nullopt; }
  std::optional<std::vector<Seg>> q = OffsetLoop(pr.prof, d, sc, why);
  if (!q) return std::nullopt;
  std::optional<kernel::Brep> k = Build(pr, *q, -d, pr.H + 2 * d);
  if (!k) why = "the offset prism did not assemble into a valid closed solid";
  return k;
}


// ---------------------------------------------------------------------------
// A right prism with ONE circular THROUGH-HOLE along its axis - what BooleanDifference of a box
// (or any straight-sided prism) and a cylinder gives. As with ClassifyHolePlateRim
// (cmd_fillet.cpp) the plan is read from the exact FACES - the cylinder, the two end planes and
// the side planes' ring - never from the sewn cap loop (a keyhole polyline whose vertices were
// snapped by the sewing tolerance). The offset is analytic: the outer polygon is offset like any
// prism profile, the hole stays concentric with radius R - d (the material grows into the hole),
// the caps move along the axis. The recognition is cross-checked against the solid's own volume.
// ---------------------------------------------------------------------------
struct HolePrism {
  Prism pr;        // the OUTER profile (straight edges) and the caps; the axis passes through the origin of the profile plane
  Hole2 hole;      // centre (0,0) and the radius
};

// `not_hole` is true when the solid is simply not that shape (the caller keeps its own message).
inline bool RecognizeHole(const ON_Brep& b, HolePrism& hp, std::string& why, bool& not_hole) {
  not_hole = true;
  if (!b.IsSolid()) { why = "the polysurface is not a closed solid"; return false; }
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double diag = std::max(1e-9, bb.Diagonal().Length());
  const double tol = std::max(1e-6, diag * 1e-6);
  int wall = -1;
  ON_Cylinder cyl;
  for (int f = 0; f < b.m_F.Count(); ++f) {
    const ON_Surface* surf = b.m_F[f].SurfaceOf();
    if (!surf) return false;
    ON_Cylinder c;
    ON_Plane pl;
    if (surf->IsPlanar(&pl, tol)) continue;
    if (surf->IsCylinder(&c, tol)) { if (wall >= 0) return false; wall = f; cyl = c; }
    else return false;
  }
  if (wall < 0) return false;
  ON_3dVector ax = cyl.Axis();
  if (!ax.Unitize()) return false;
  std::vector<int> ends, sides;
  for (int f = 0; f < b.m_F.Count(); ++f) {
    if (f == wall) continue;
    ON_Plane pl;
    b.m_F[f].SurfaceOf()->IsPlanar(&pl, tol);
    const double dn = std::fabs(pl.zaxis * ax);
    if (dn > 1.0 - 1e-9) ends.push_back(f);
    else if (dn < 1e-9) sides.push_back(f);
    else return false;
  }
  if (ends.size() != 2 || sides.size() < 3) return false;
  double h[2];
  bool touch[2] = {false, false};
  for (int k = 0; k < 2; ++k) {
    ON_Plane pl;
    b.m_F[ends[static_cast<size_t>(k)]].SurfaceOf()->IsPlanar(&pl, tol);
    h[k] = (pl.origin - cyl.circle.plane.origin) * ax;
    for (int ei = 0; ei < b.m_E.Count() && !touch[k]; ++ei) {
      const ON_BrepEdge& ee = b.m_E[ei];
      if (ee.m_edge_index < 0 || ee.TrimCount() != 2) continue;
      const int x = b.m_T[ee.m_ti[0]].FaceIndexOf(), y = b.m_T[ee.m_ti[1]].FaceIndexOf();
      touch[k] = (x == wall && y == ends[static_cast<size_t>(k)]) || (y == wall && x == ends[static_cast<size_t>(k)]);
    }
  }
  if (!touch[0] || !touch[1]) return false;  // blind hole or boss: not a through-hole
  const double h0 = std::min(h[0], h[1]), h1 = std::max(h[0], h[1]);
  if (!(h1 - h0 > tol)) return false;
  const ON_3dPoint c0 = cyl.circle.plane.origin + h0 * ax;
  // The side planes must form one closed ring (each meets exactly two others).
  std::map<int, std::set<int>> adj;
  for (int ei = 0; ei < b.m_E.Count(); ++ei) {
    const ON_BrepEdge& ee = b.m_E[ei];
    if (ee.m_edge_index < 0 || ee.TrimCount() != 2) continue;
    const int x = b.m_T[ee.m_ti[0]].FaceIndexOf(), y = b.m_T[ee.m_ti[1]].FaceIndexOf();
    if (x == y || std::find(sides.begin(), sides.end(), x) == sides.end() || std::find(sides.begin(), sides.end(), y) == sides.end()) continue;
    adj[x].insert(y);
    adj[y].insert(x);
  }
  for (int sfi : sides) if (adj[sfi].size() != 2) return false;
  std::vector<int> ring{sides[0]};
  int prev = -1, cur = sides[0];
  while (true) {
    int nxt = -1;
    for (int cand : adj[cur]) if (cand != prev) { nxt = cand; break; }
    if (nxt < 0 || nxt == sides[0]) break;
    if (ring.size() > sides.size()) return false;
    ring.push_back(nxt);
    prev = cur;
    cur = nxt;
  }
  if (ring.size() != sides.size()) return false;
  const ON_Plane lower_plane(c0, ax);
  std::vector<ON_3dPoint> poly3;
  for (size_t i = 0; i < ring.size(); ++i) {
    ON_Plane pa, pb;
    b.m_F[ring[i]].SurfaceOf()->IsPlanar(&pa, tol);
    b.m_F[ring[(i + 1) % ring.size()]].SurfaceOf()->IsPlanar(&pb, tol);
    ON_3dPoint corner;
    if (!ON_Intersect(pa, pb, lower_plane, corner)) return false;
    poly3.push_back(corner);
  }
  // From here on the solid IS a hole-plate candidate: failures carry a reason the caller prints.
  ON_3dVector X = cyl.circle.plane.xaxis;
  X = X - (X * ax) * ax;
  if (!X.Unitize()) return false;
  hp.pr.n = ax;
  hp.pr.O = c0;
  hp.pr.X = X;
  hp.pr.Y = ON_CrossProduct(ax, X);
  hp.pr.H = h1 - h0;
  const ON_3dPoint axis_pt = cyl.circle.plane.origin;
  auto to2 = [&](const ON_3dPoint& p) { const ON_3dVector v = p - hp.pr.O; return V2{v * hp.pr.X, v * hp.pr.Y}; };
  std::vector<V2> poly;
  for (const ON_3dPoint& p : poly3) poly.push_back(to2(p));
  double area = 0;
  for (size_t i = 0; i < poly.size(); ++i) area += 0.5 * Cross(poly[i], poly[(i + 1) % poly.size()]);
  if (area < 0) { std::reverse(poly.begin(), poly.end()); area = -area; }
  hp.pr.prof.clear();
  for (size_t i = 0; i < poly.size(); ++i) {
    Seg g;
    g.a = poly[i];
    g.b = poly[(i + 1) % poly.size()];
    hp.pr.prof.push_back(g);
  }
  const V2 axis2 = to2(axis_pt - ((axis_pt - c0) * ax) * ax);
  hp.hole.c = axis2;
  hp.hole.r = cyl.circle.radius;
  not_hole = false;
  for (const Seg& g : hp.pr.prof) {
    if (!(DistToSeg(g, hp.hole.c) > hp.hole.r + 10 * tol)) { why = "the hole is not inside the outer polygon"; return false; }
  }
  BrepMeshOptions opt;
  opt.chord_tolerance = std::max(1e-6, diag * 2e-4);
  double vol = 0;
  try { vol = std::fabs(MeshBrepClosed(b, opt).Volume()); } catch (const std::exception&) { vol = 0; }
  const double expect = (area - ON_PI * hp.hole.r * hp.hole.r) * hp.pr.H;
  if (!(expect > 0) || !(std::fabs(vol - expect) <= 5e-3 * expect)) {
    why = "the solid is not a right prism with one round through-hole (volume check failed)";
    return false;
  }
  return true;
}

// The prism-with-hole offset outward by d (negative = inward; d == 0 rebuilds it exactly):
// outer polygon offset, hole radius R - d, caps moved by d.
inline std::optional<kernel::Brep> OffsetHoleSolid(const HolePrism& hp, double d, std::string& why) {
  const Prism& pr = hp.pr;
  const double sc = Scale(pr);
  if (!(pr.H + 2 * d > sc * 1e-6)) { why = "the prism would collapse between its caps at that offset"; return std::nullopt; }
  const double r2 = hp.hole.r - d;
  if (!(r2 > sc * 1e-6)) { why = "the hole would vanish at that offset (its radius reaches zero)"; return std::nullopt; }
  std::vector<Seg> q = pr.prof;
  if (d != 0) {
    std::optional<std::vector<Seg>> o = OffsetLoop(pr.prof, d, sc, why);
    if (!o) return std::nullopt;
    q = *o;
  }
  for (const Seg& g : q) {
    if (!(DistToSeg(g, hp.hole.c) > r2 + sc * 1e-6)) { why = "the hole would reach the outer wall at that offset"; return std::nullopt; }
  }
  std::optional<kernel::Brep> k = Build(pr, q, -d, pr.H + 2 * d, Hole2{hp.hole.c, r2});
  if (!k) why = "the offset prism with its hole did not assemble into a valid closed solid";
  return k;
}

}  // namespace dino8::app::prism
