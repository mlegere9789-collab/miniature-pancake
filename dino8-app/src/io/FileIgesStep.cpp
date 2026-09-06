#include "io/FileIgesStep.h"

#include <opennurbs.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

namespace {

// ===========================================================================
// Shared helpers
// ===========================================================================

std::string Num(double v, int decimals = 9) {
  if (!std::isfinite(v)) v = 0.0;
  if (std::fabs(v) < 1e-13) v = 0.0;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*g", decimals, v);
  return buf;
}

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

double MillimetresPerUnit(const std::string& u) {
  if (u == "Inches") return 25.4;
  if (u == "Feet") return 304.8;
  if (u == "Centimeters") return 10.0;
  if (u == "Meters") return 1000.0;
  return 1.0;
}

bool CurveToKernel(const ON_Curve& c, kernel::NurbsCurve& out) {
  ON_NurbsCurve nc;
  if (c.GetNurbForm(nc) <= 0 || !nc.IsValid()) return false;
  out.raw() = nc;
  return true;
}

bool SurfaceToKernel(const ON_Surface& s, kernel::NurbsSurface& out) {
  ON_NurbsSurface ns;
  if (s.GetNurbForm(ns) <= 0 || !ns.IsValid()) return false;
  out.raw() = ns;
  return true;
}

// Full (clamped) knot vector: OpenNURBS drops the superfluous first and
// last knots, IGES and STEP carry them.
std::vector<double> FullKnots(const ON_NurbsCurve& c) {
  std::vector<double> k;
  const int n = c.KnotCount();
  if (n <= 0) return k;
  k.push_back(c.Knot(0));
  for (int i = 0; i < n; ++i) k.push_back(c.Knot(i));
  k.push_back(c.Knot(n - 1));
  return k;
}
std::vector<double> FullKnots(const ON_NurbsSurface& s, int dir) {
  std::vector<double> k;
  const int n = s.KnotCount(dir);
  if (n <= 0) return k;
  k.push_back(s.Knot(dir, 0));
  for (int i = 0; i < n; ++i) k.push_back(s.Knot(dir, i));
  k.push_back(s.Knot(dir, n - 1));
  return k;
}

// Builds an OpenNURBS curve from a full knot vector (cv_count + order
// knots), Euclidean control points and weights (empty = non-rational).
bool MakeNurbsCurve(int dim, int degree, const std::vector<ON_3dPoint>& cvs, const std::vector<double>& weights,
                    const std::vector<double>& full_knots, ON_NurbsCurve& out) {
  const int cv_count = static_cast<int>(cvs.size());
  if (degree < 1 || cv_count < degree + 1) return false;
  if (static_cast<int>(full_knots.size()) != cv_count + degree + 1) return false;
  const bool rational = !weights.empty();
  if (!out.Create(dim, rational, degree + 1, cv_count)) return false;
  for (int i = 0; i < out.KnotCount(); ++i) out.SetKnot(i, full_knots[static_cast<size_t>(i + 1)]);
  for (int i = 0; i < cv_count; ++i) {
    if (rational) {
      const double w = weights[static_cast<size_t>(i)];
      out.SetCV(i, ON_4dPoint(cvs[static_cast<size_t>(i)].x * w, cvs[static_cast<size_t>(i)].y * w, cvs[static_cast<size_t>(i)].z * w, w));
    } else {
      out.SetCV(i, cvs[static_cast<size_t>(i)]);
    }
  }
  return out.IsValid();
}

bool MakeNurbsSurface(int deg_u, int deg_v, int nu, int nv, const std::vector<ON_3dPoint>& cvs /* u fastest */,
                      const std::vector<double>& weights, const std::vector<double>& ku, const std::vector<double>& kv,
                      ON_NurbsSurface& out) {
  if (deg_u < 1 || deg_v < 1 || nu < deg_u + 1 || nv < deg_v + 1) return false;
  if (static_cast<int>(cvs.size()) != nu * nv) return false;
  if (static_cast<int>(ku.size()) != nu + deg_u + 1 || static_cast<int>(kv.size()) != nv + deg_v + 1) return false;
  const bool rational = !weights.empty();
  if (!out.Create(3, rational, deg_u + 1, deg_v + 1, nu, nv)) return false;
  for (int i = 0; i < out.KnotCount(0); ++i) out.SetKnot(0, i, ku[static_cast<size_t>(i + 1)]);
  for (int i = 0; i < out.KnotCount(1); ++i) out.SetKnot(1, i, kv[static_cast<size_t>(i + 1)]);
  for (int j = 0; j < nv; ++j) {
    for (int i = 0; i < nu; ++i) {
      const size_t k = static_cast<size_t>(j * nu + i);
      if (rational) {
        const double w = weights[k];
        out.SetCV(i, j, ON_4dPoint(cvs[k].x * w, cvs[k].y * w, cvs[k].z * w, w));
      } else {
        out.SetCV(i, j, cvs[k]);
      }
    }
  }
  return out.IsValid();
}

ON_3dPoint EuclideanCV(const ON_NurbsCurve& c, int i, double& w) {
  ON_4dPoint h;
  c.GetCV(i, h);
  w = c.IsRational() ? h.w : 1.0;
  if (c.IsRational() && std::fabs(h.w) > 1e-300) return ON_3dPoint(h.x / h.w, h.y / h.w, h.z / h.w);
  return ON_3dPoint(h.x, h.y, h.z);
}
ON_3dPoint EuclideanCV(const ON_NurbsSurface& s, int i, int j, double& w) {
  ON_4dPoint h;
  s.GetCV(i, j, h);
  w = s.IsRational() ? h.w : 1.0;
  if (s.IsRational() && std::fabs(h.w) > 1e-300) return ON_3dPoint(h.x / h.w, h.y / h.w, h.z / h.w);
  return ON_3dPoint(h.x, h.y, h.z);
}

// ---- closest points (the public OpenNURBS ships no implementation) --------

double ClosestParameter(const ON_Curve& c, ON_3dPoint P, double* dist_out = nullptr) {
  const ON_Interval d = c.Domain();
  int spans = 1;
  if (const ON_NurbsCurve* nc = ON_NurbsCurve::Cast(&c)) spans = std::max(1, nc->SpanCount() * nc->Degree());
  else spans = 8;
  const int n = std::clamp(spans * 8, 16, 512);
  double best_t = d.Min(), best_d = 1e300;
  for (int i = 0; i <= n; ++i) {
    const double t = d.ParameterAt(static_cast<double>(i) / n);
    const double dd = c.PointAt(t).DistanceTo(P);
    if (dd < best_d) { best_d = dd; best_t = t; }
  }
  // Newton on f(t) = (C(t)-P).C'(t).
  double t = best_t;
  for (int it = 0; it < 40; ++it) {
    ON_3dPoint C;
    ON_3dVector C1, C2;
    if (!c.Ev2Der(t, C, C1, C2)) break;
    const ON_3dVector r = C - P;
    const double f = ON_DotProduct(r, C1);
    const double fp = ON_DotProduct(C1, C1) + ON_DotProduct(r, C2);
    if (std::fabs(fp) < 1e-300) break;
    double tn = t - f / fp;
    tn = std::clamp(tn, d.Min(), d.Max());
    const double dn = c.PointAt(tn).DistanceTo(P);
    if (dn < best_d) { best_d = dn; best_t = tn; }
    if (std::fabs(tn - t) < 1e-12 * std::max(1.0, d.Length())) break;
    t = tn;
  }
  if (dist_out) *dist_out = best_d;
  return best_t;
}

struct UV { double u = 0, v = 0; };

bool NewtonUV(const ON_Surface& s, ON_3dPoint P, UV& uv, double& dist) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  dist = s.PointAt(uv.u, uv.v).DistanceTo(P);
  for (int it = 0; it < 40; ++it) {
    ON_3dPoint S;
    ON_3dVector Su, Sv, Suu, Suv, Svv;
    if (!s.Ev2Der(uv.u, uv.v, S, Su, Sv, Suu, Suv, Svv)) return false;
    const ON_3dVector r = S - P;
    const double f = ON_DotProduct(r, Su), g = ON_DotProduct(r, Sv);
    const double a = ON_DotProduct(Su, Su) + ON_DotProduct(r, Suu);
    const double b = ON_DotProduct(Su, Sv) + ON_DotProduct(r, Suv);
    const double c = ON_DotProduct(Sv, Sv) + ON_DotProduct(r, Svv);
    const double det = a * c - b * b;
    double su, sv;
    if (std::fabs(det) < 1e-300) {
      // Gradient step at a singularity (pole) - keep it small.
      const double gu = ON_DotProduct(Su, Su), gv = ON_DotProduct(Sv, Sv);
      su = gu > 1e-300 ? -f / gu : 0.0;
      sv = gv > 1e-300 ? -g / gv : 0.0;
    } else {
      su = -(c * f - b * g) / det;
      sv = -(a * g - b * f) / det;
    }
    UV n{std::clamp(uv.u + su, du.Min(), du.Max()), std::clamp(uv.v + sv, dv.Min(), dv.Max())};
    const double dn = s.PointAt(n.u, n.v).DistanceTo(P);
    if (dn > dist) {
      // Backtrack.
      bool improved = false;
      for (int k = 0; k < 6 && !improved; ++k) {
        su *= 0.5; sv *= 0.5;
        n = UV{std::clamp(uv.u + su, du.Min(), du.Max()), std::clamp(uv.v + sv, dv.Min(), dv.Max())};
        const double d2 = s.PointAt(n.u, n.v).DistanceTo(P);
        if (d2 < dist) { improved = true; dist = d2; uv = n; }
      }
      if (!improved) return true;
      continue;
    }
    const bool done = std::fabs(n.u - uv.u) < 1e-11 * std::max(1.0, du.Length()) && std::fabs(n.v - uv.v) < 1e-11 * std::max(1.0, dv.Length());
    uv = n;
    dist = dn;
    if (done) return true;
  }
  return true;
}

UV GridSeed(const ON_Surface& s, ON_3dPoint P, int n = 24) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  int nu = n, nv = n;
  if (const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(&s)) {
    nu = std::clamp(ns->SpanCount(0) * std::max(2, ns->Degree(0)) * 2, 8, 96);
    nv = std::clamp(ns->SpanCount(1) * std::max(2, ns->Degree(1)) * 2, 8, 96);
  }
  UV best;
  double best_d = 1e300;
  for (int i = 0; i <= nu; ++i) {
    for (int j = 0; j <= nv; ++j) {
      const UV uv{du.ParameterAt(static_cast<double>(i) / nu), dv.ParameterAt(static_cast<double>(j) / nv)};
      const double d = s.PointAt(uv.u, uv.v).DistanceTo(P);
      if (d < best_d) { best_d = d; best = uv; }
    }
  }
  return best;
}

// Closest surface point; `seed` (when given) is tried first so a walk along
// a curve stays on one side of a seam.
UV ClosestUV(const ON_Surface& s, ON_3dPoint P, const UV* seed, double tol, double* dist_out = nullptr) {
  UV best;
  double best_d = 1e300;
  if (seed) {
    UV uv = *seed;
    double d;
    if (NewtonUV(s, P, uv, d)) { best = uv; best_d = d; }
    if (best_d <= tol) { if (dist_out) *dist_out = best_d; return best; }
  }
  UV uv = GridSeed(s, P);
  double d;
  if (NewtonUV(s, P, uv, d) && d < best_d) { best = uv; best_d = d; }
  if (dist_out) *dist_out = best_d;
  return best;
}

// ---- face construction shared by both readers -----------------------------

// One trim of a loop: the 2D parameter-space curve (dimension 2) and the
// 3D model-space curve, both running in loop direction.
struct LoopSeg {
  ON_NurbsCurve c2;
  ON_NurbsCurve c3;
};

// True when the surface is an affine map of (u,v): a bilinear patch whose
// corners form a parallelogram. Then a 3D curve on it maps to an exact 2D
// curve by transforming its control points.
bool IsAffinePatch(const ON_NurbsSurface& s, ON_3dPoint& origin, ON_3dVector& du_axis, ON_3dVector& dv_axis, double tol) {
  if (s.Degree(0) != 1 || s.Degree(1) != 1 || s.CVCount(0) != 2 || s.CVCount(1) != 2 || s.IsRational()) return false;
  ON_3dPoint p00, p10, p01, p11;
  s.GetCV(0, 0, p00); s.GetCV(1, 0, p10); s.GetCV(0, 1, p01); s.GetCV(1, 1, p11);
  if ((p11 - p10 - (p01 - p00)).Length() > tol) return false;
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  if (du.Length() <= 0 || dv.Length() <= 0) return false;
  origin = p00;
  du_axis = (p10 - p00) / du.Length();
  dv_axis = (p01 - p00) / dv.Length();
  return du_axis.Length() > 1e-12 && dv_axis.Length() > 1e-12;
}

// Projects a 3D curve onto a surface, producing the 2D pcurve. Exact
// (control-point transform) on affine patches, otherwise a dense polyline
// fit through closest-point samples that stays continuous across seams.
bool ProjectToSurface(const ON_NurbsSurface& s, const ON_NurbsCurve& c3, double tol, ON_NurbsCurve& c2) {
  ON_3dPoint o;
  ON_3dVector ax, ay;
  if (IsAffinePatch(s, o, ax, ay, tol)) {
    const ON_Interval du = s.Domain(0), dv = s.Domain(1);
    // Solve the 2x2 least squares for each control point (exact when the
    // point lies in the plane).
    const double a = ON_DotProduct(ax, ax), b = ON_DotProduct(ax, ay), cc = ON_DotProduct(ay, ay);
    const double det = a * cc - b * b;
    if (std::fabs(det) < 1e-300) return false;
    c2 = c3;
    c2.ChangeDimension(2);
    for (int i = 0; i < c3.CVCount(); ++i) {
      double w;
      const ON_3dPoint p = EuclideanCV(c3, i, w);
      const ON_3dVector r = p - o;
      const double f = ON_DotProduct(r, ax), g = ON_DotProduct(r, ay);
      const double u = du.Min() + (cc * f - b * g) / det;
      const double v = dv.Min() + (a * g - b * f) / det;
      if (c2.IsRational()) c2.SetCV(i, ON_4dPoint(u * w, v * w, 0, w));
      else c2.SetCV(i, ON_3dPoint(u, v, 0));
    }
    return c2.IsValid();
  }
  const int spans = std::max(1, c3.SpanCount());
  int n = std::clamp(spans * std::max(2, c3.Degree()) * 6, 12, 256);
  if (c3.Degree() == 1 && c3.SpanCount() == 1) n = 4;
  const ON_Interval dom = c3.Domain();
  std::vector<ON_2dPoint> pts;
  pts.reserve(static_cast<size_t>(n) + 1);
  UV prev;
  bool have_prev = false;
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  const bool closed_u = s.IsClosed(0), closed_v = s.IsClosed(1);
  for (int i = 0; i <= n; ++i) {
    const ON_3dPoint p = c3.PointAt(dom.ParameterAt(static_cast<double>(i) / n));
    UV uv = ClosestUV(s, p, have_prev ? &prev : nullptr, tol);
    if (have_prev) {
      // Stay on the same side of a seam: a jump of more than half the
      // period means the closest point wrapped.
      if (closed_u && std::fabs(uv.u - prev.u) > 0.5 * du.Length()) uv.u = uv.u > prev.u ? uv.u - du.Length() : uv.u + du.Length();
      if (closed_v && std::fabs(uv.v - prev.v) > 0.5 * dv.Length()) uv.v = uv.v > prev.v ? uv.v - dv.Length() : uv.v + dv.Length();
    }
    pts.emplace_back(uv.u, uv.v);
    prev = uv;
    have_prev = true;
  }
  // A curve that starts on a seam may have been placed on the wrong edge of
  // the domain: shift the whole polyline back inside.
  auto fix_range = [&](bool closed, const ON_Interval& d, int comp) {
    if (!closed) return;
    double lo = 1e300, hi = -1e300;
    for (const ON_2dPoint& q : pts) { const double x = comp == 0 ? q.x : q.y; lo = std::min(lo, x); hi = std::max(hi, x); }
    double shift = 0;
    if (hi > d.Max() + 1e-9 && lo - d.Length() >= d.Min() - 1e-9) shift = -d.Length();
    else if (lo < d.Min() - 1e-9 && hi + d.Length() <= d.Max() + 1e-9) shift = d.Length();
    if (shift != 0) for (ON_2dPoint& q : pts) { if (comp == 0) q.x += shift; else q.y += shift; }
  };
  fix_range(closed_u, du, 0);
  fix_range(closed_v, dv, 1);
  for (ON_2dPoint& q : pts) { q.x = std::clamp(q.x, du.Min(), du.Max()); q.y = std::clamp(q.y, dv.Min(), dv.Max()); }
  // Straight in parameter space? Then two control points suffice.
  bool straight = true;
  for (size_t i = 1; i + 1 < pts.size() && straight; ++i) {
    const ON_2dPoint e = pts.front() + (pts.back() - pts.front()) * (static_cast<double>(i) / n);
    if (e.DistanceTo(pts[i]) > 1e-7 * (1 + du.Length() + dv.Length())) straight = false;
  }
  if (straight) pts = {pts.front(), pts.back()};
  if (!c2.Create(2, false, 2, static_cast<int>(pts.size()))) return false;
  const int m = static_cast<int>(pts.size());
  for (int i = 0; i < m; ++i) c2.SetCV(i, ON_3dPoint(pts[static_cast<size_t>(i)].x, pts[static_cast<size_t>(i)].y, 0));
  for (int i = 0; i < c2.KnotCount(); ++i) c2.SetKnot(i, dom.ParameterAt(static_cast<double>(i) / (m - 1)));
  return c2.IsValid();
}

// The 3D curve of a 2D pcurve, evaluated through the surface (used when an
// IGES 142 carries only the parameter-space curve).
bool LiftToSurface(const ON_NurbsSurface& s, const ON_NurbsCurve& c2, ON_NurbsCurve& c3) {
  const ON_Interval dom = c2.Domain();
  ON_3dPoint o;
  ON_3dVector ax, ay;
  const bool affine = IsAffinePatch(s, o, ax, ay, 1e-9);
  if (affine && c2.Degree() == 1 && c2.SpanCount() == 1) {
    const ON_3dPoint a = c2.PointAtStart(), b = c2.PointAtEnd();
    ON_LineCurve lc(s.PointAt(a.x, a.y), s.PointAt(b.x, b.y));
    lc.SetDomain(dom.Min(), dom.Max());
    return lc.GetNurbForm(c3) > 0;
  }
  const int n = std::clamp(std::max(1, c2.SpanCount()) * std::max(2, c2.Degree()) * 8, 16, 256);
  ON_Polyline pl;
  for (int i = 0; i <= n; ++i) {
    const ON_3dPoint uv = c2.PointAt(dom.ParameterAt(static_cast<double>(i) / n));
    pl.Append(s.PointAt(uv.x, uv.y));
  }
  ON_PolylineCurve pc(pl);
  pc.SetDomain(dom.Min(), dom.Max());
  return pc.GetNurbForm(c3) > 0;
}

// Adds one face (surface + loops) to `brep`. Returns the face index or -1.
// Seam trims (two trims of the same face on coincident 3D curves) share an
// edge; degenerate 3D segments become singular trims.
int AddTrimmedFace(ON_Brep& brep, ON_NurbsSurface* srf, const std::vector<std::vector<LoopSeg>>& loops, double tol) {
  const int si = brep.AddSurface(srf);
  ON_BrepFace& face = brep.NewFace(si);
  const int fi = face.m_face_index;
  const int first_vertex = brep.m_V.Count();
  const int first_edge = brep.m_E.Count();
  auto find_or_add_vertex = [&](ON_3dPoint p) {
    for (int vi = first_vertex; vi < brep.m_V.Count(); ++vi) if (brep.m_V[vi].point.DistanceTo(p) <= tol) return vi;
    ON_BrepVertex& v = brep.NewVertex(p, 0.0);
    return v.m_vertex_index;
  };
  for (size_t k = 0; k < loops.size(); ++k) {
    const std::vector<LoopSeg>& segs = loops[k];
    if (segs.empty()) continue;
    ON_BrepLoop& loop = brep.NewLoop(k == 0 ? ON_BrepLoop::outer : ON_BrepLoop::inner, brep.m_F[fi]);
    const int li = loop.m_loop_index;
    for (size_t i = 0; i < segs.size(); ++i) {
      const LoopSeg& seg = segs[i];
      ON_NurbsCurve* c2 = new ON_NurbsCurve(seg.c2);
      c2->ChangeDimension(2);
      const int c2i = brep.AddTrimCurve(c2);
      const ON_3dPoint p0 = seg.c3.PointAtStart(), p1 = seg.c3.PointAtEnd();
      const ON_3dPoint pm = seg.c3.PointAt(seg.c3.Domain().Mid());
      const bool degenerate = p0.DistanceTo(p1) <= tol && pm.DistanceTo(p0) <= tol;
      const int v0 = find_or_add_vertex(p0);
      if (degenerate) {
        ON_BrepTrim& t = brep.NewSingularTrim(brep.m_V[v0], brep.m_L[li], ON_Surface::not_iso, c2i);
        t.m_tolerance[0] = t.m_tolerance[1] = 0.0;
        continue;
      }
      const int v1 = find_or_add_vertex(p1);
      // Seam / duplicate edge inside this face?
      int reuse = -1;
      bool rev = false;
      for (int ei = first_edge; ei < brep.m_E.Count() && reuse < 0; ++ei) {
        const ON_BrepEdge& e = brep.m_E[ei];
        if (e.m_ti.Count() != 1) continue;
        const ON_3dPoint em = e.PointAt(e.Domain().Mid());
        if (em.DistanceTo(pm) > tol) continue;
        if (e.m_vi[0] == v1 && e.m_vi[1] == v0) { reuse = ei; rev = true; }
        else if (e.m_vi[0] == v0 && e.m_vi[1] == v1 && v0 != v1) { reuse = ei; rev = false; }
        else if (v0 == v1 && e.m_vi[0] == v0 && e.m_vi[1] == v0) {
          reuse = ei;
          rev = ON_DotProduct(e.TangentAt(e.Domain().Min()), seg.c3.TangentAt(seg.c3.Domain().Min())) < 0;
        }
      }
      ON_BrepTrim* trim = nullptr;
      if (reuse >= 0) {
        trim = &brep.NewTrim(brep.m_E[reuse], rev, brep.m_L[li], c2i);
      } else {
        const int c3i = brep.AddEdgeCurve(new ON_NurbsCurve(seg.c3));
        ON_BrepEdge& e = brep.NewEdge(brep.m_V[v0], brep.m_V[v1], c3i);
        e.m_tolerance = tol;
        trim = &brep.NewTrim(e, false, brep.m_L[li], c2i);
      }
      trim->m_tolerance[0] = trim->m_tolerance[1] = 0.0;
      trim->m_type = ON_BrepTrim::boundary;
    }
  }
  return fi;
}

// Trim types from edge sharing, iso flags, boxes; then validity.
void FinishBrep(ON_Brep& b) {
  for (int ei = 0; ei < b.m_E.Count(); ++ei) {
    ON_BrepEdge& e = b.m_E[ei];
    if (e.m_edge_index < 0) continue;
    for (int k = 0; k < e.m_ti.Count(); ++k) {
      ON_BrepTrim& t = b.m_T[e.m_ti[k]];
      if (e.m_ti.Count() == 1) t.m_type = ON_BrepTrim::boundary;
      else {
        bool same_face = false;
        for (int j = 0; j < e.m_ti.Count(); ++j) if (j != k && b.m_T[e.m_ti[j]].FaceIndexOf() == t.FaceIndexOf()) same_face = true;
        t.m_type = same_face ? ON_BrepTrim::seam : ON_BrepTrim::mated;
      }
    }
  }
  b.SetTrimIsoFlags();
  b.SetTolerancesBoxesAndFlags();
}

// Joins coincident naked edges (a copy of the cmd_common.h helper - the io
// layer must not depend on the command layer) and orients faces.
int OrientFaces(ON_Brep& b) {
  std::vector<char> done(static_cast<size_t>(b.m_F.Count()), 0);
  int flipped = 0;
  for (int seed = 0; seed < b.m_F.Count(); ++seed) {
    if (done[static_cast<size_t>(seed)] || b.m_F[seed].m_face_index < 0) continue;
    std::vector<int> queue = {seed};
    done[static_cast<size_t>(seed)] = 1;
    while (!queue.empty()) {
      const int fi = queue.back();
      queue.pop_back();
      const ON_BrepFace& f = b.m_F[fi];
      for (int li = 0; li < f.m_li.Count(); ++li) {
        const ON_BrepLoop& loop = b.m_L[f.m_li[li]];
        for (int k = 0; k < loop.m_ti.Count(); ++k) {
          const int ti = loop.m_ti[k];
          const ON_BrepTrim& t = b.m_T[ti];
          if (t.m_ei < 0) continue;
          const ON_BrepEdge& e = b.m_E[t.m_ei];
          if (e.m_ti.Count() != 2) continue;
          const int oti = e.m_ti[0] == ti ? e.m_ti[1] : e.m_ti[0];
          const ON_BrepTrim& ot = b.m_T[oti];
          const int ofi = ot.FaceIndexOf();
          if (ofi < 0 || done[static_cast<size_t>(ofi)]) continue;
          ON_BrepFace& of = b.m_F[ofi];
          const bool cw0 = b.LoopDirection(loop) < 0, cw1 = b.LoopDirection(b.m_L[ot.m_li]) < 0;
          const bool d0 = (t.m_bRev3d != f.m_bRev) != cw0, d1 = (ot.m_bRev3d != of.m_bRev) != cw1;
          if (d0 == d1) { b.FlipFace(of); ++flipped; }
          done[static_cast<size_t>(ofi)] = 1;
          queue.push_back(ofi);
        }
      }
    }
  }
  return flipped;
}

int JoinEdges(ON_Brep& b, double tol) {
  int joined = 0;
  for (int i = 0; i < b.m_E.Count(); ++i) {
    ON_BrepEdge& e0 = b.m_E[i];
    if (e0.m_edge_index < 0 || e0.TrimCount() != 1) continue;
    ON_3dPoint a0 = e0.PointAtStart(), a1 = e0.PointAtEnd();
    for (int j = i + 1; j < b.m_E.Count(); ++j) {
      ON_BrepEdge& e1 = b.m_E[j];
      if (e1.m_edge_index < 0 || e1.TrimCount() != 1) continue;
      ON_3dPoint b0 = e1.PointAtStart(), b1 = e1.PointAtEnd();
      bool forward = a0.DistanceTo(b0) <= tol && a1.DistanceTo(b1) <= tol;
      bool reversed = !forward && a0.DistanceTo(b1) <= tol && a1.DistanceTo(b0) <= tol;
      if (forward && a0.DistanceTo(a1) <= tol) {
        forward = ON_DotProduct(e0.TangentAt(e0.Domain().Min()), e1.TangentAt(e1.Domain().Min())) > 0;
        reversed = !forward;
      }
      if (!forward && !reversed) continue;
      ON_3dPoint m0 = e0.PointAt(e0.Domain().Mid()), m1 = e1.PointAt(e1.Domain().Mid());
      if (m0.DistanceTo(m1) > tol * 10) continue;
      if (reversed && !e1.Reverse()) continue;
      for (int k = 0; k < 2; ++k) {
        if (e0.m_vi[k] == e1.m_vi[k]) continue;
        if (!b.CombineCoincidentVertices(b.m_V[e0.m_vi[k]], b.m_V[e1.m_vi[k]])) break;
      }
      if (b.CombineCoincidentEdges(e0, e1)) { ++joined; break; }
    }
  }
  if (joined) {
    OrientFaces(b);
    b.SetTolerancesBoxesAndFlags();
  }
  return joined;
}

// A face is exported through its surface's NURBS form; the face's trims are
// re-expressed on that form. Returns nullptr-free copies.
// Returns 0 (failed), 1 (same parameterisation) or 2 (the NURBS form is
// parameterised differently, so trims must be re-projected).
int FaceNurbsSurface(const ON_BrepFace& face, ON_NurbsSurface& out) {
  const ON_Surface* s = face.SurfaceOf();
  if (!s) return 0;
  const int rc = s->GetNurbForm(out);
  return rc > 0 && out.IsValid() ? rc : 0;
}

// The loops of a face expressed on `ns` (its NURBS form), in loop
// direction, with the face flip folded in: a reversed face is written with
// the transposed surface (which flips the normal) and mirrored, reversed
// loops, so the exported face has the same orientation as the original.
struct ExportFace {
  ON_NurbsSurface srf;
  std::vector<std::vector<LoopSeg>> loops;  // first = outer
};

bool PrepareExportFace(const ON_Brep& b, const ON_BrepFace& face, double tol, ExportFace& out);

Color ColorFromBytes(int r, int g, int b) { return Color::FromBytes(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255)); }

int ColorKey(const Color& c) {
  return (static_cast<int>(std::lround(c.r * 255)) << 16) | (static_cast<int>(std::lround(c.g * 255)) << 8) | static_cast<int>(std::lround(c.b * 255));
}

// Objects that take part in an export.
std::vector<const SceneObject*> ExportObjects(const Document& doc, bool selected_only) {
  std::vector<const SceneObject*> objs;
  for (const SceneObject& o : doc.Objects()) {
    if (selected_only && !o.selected) continue;
    if (!doc.IsObjectVisible(o)) continue;
    objs.push_back(&o);
  }
  return objs;
}

// A one-face brep for a free surface so surfaces and breps share one path.
bool SurfaceAsBrep(const kernel::NurbsSurface& s, ON_Brep& out) {
  ON_NurbsSurface* copy = new ON_NurbsSurface(s.raw());
  ON_BrepFace* f = out.NewFace(*copy);
  delete copy;
  if (!f) return false;
  out.SetTolerancesBoxesAndFlags();
  return true;
}

bool PrepareExportFace(const ON_Brep& b, const ON_BrepFace& face, double tol, ExportFace& out) {
  const int rc = FaceNurbsSurface(face, out.srf);
  if (rc == 0) return false;
  const ON_Surface* real = face.SurfaceOf();
  out.loops.clear();
  for (int li = 0; li < face.m_li.Count(); ++li) {
    const ON_BrepLoop& loop = b.m_L[face.m_li[li]];
    if (loop.m_type != ON_BrepLoop::outer && loop.m_type != ON_BrepLoop::inner) continue;
    std::vector<LoopSeg> segs;
    for (int k = 0; k < loop.m_ti.Count(); ++k) {
      const ON_BrepTrim& t = b.m_T[loop.m_ti[k]];
      LoopSeg seg;
      if (t.m_ei >= 0) {
        const ON_BrepEdge& e = b.m_E[t.m_ei];
        if (e.GetNurbForm(seg.c3) <= 0) continue;
        if (t.m_bRev3d) seg.c3.Reverse();
      }
      if (rc == 1 || t.m_ei < 0) {
        if (t.GetNurbForm(seg.c2) <= 0) continue;
        seg.c2.ChangeDimension(3);
        if (rc == 2 && real) {
          // Re-express the pcurve in the NURBS form's parameters (sampled).
          const ON_Interval dom = seg.c2.Domain();
          const int n = 32;
          ON_Polyline pl;
          for (int i = 0; i <= n; ++i) {
            const ON_3dPoint uv = seg.c2.PointAt(dom.ParameterAt(static_cast<double>(i) / n));
            double nu = uv.x, nv = uv.y;
            real->GetNurbFormParameterFromSurfaceParameter(uv.x, uv.y, &nu, &nv);
            pl.Append(ON_3dPoint(nu, nv, 0));
          }
          ON_PolylineCurve pc(pl);
          pc.SetDomain(dom.Min(), dom.Max());
          pc.GetNurbForm(seg.c2);
        }
        if (t.m_ei < 0) {
          const ON_3dPoint p = out.srf.PointAt(seg.c2.PointAtStart().x, seg.c2.PointAtStart().y);
          ON_LineCurve lc(p, p);
          lc.GetNurbForm(seg.c3);
        }
      } else {
        if (!ProjectToSurface(out.srf, seg.c3, tol, seg.c2)) continue;
        seg.c2.ChangeDimension(3);
      }
      segs.push_back(std::move(seg));
    }
    if (segs.empty()) continue;
    if (loop.m_type == ON_BrepLoop::outer) out.loops.insert(out.loops.begin(), std::move(segs));
    else out.loops.push_back(std::move(segs));
  }
  if (face.m_bRev) {
    out.srf.Transpose();
    for (std::vector<LoopSeg>& segs : out.loops) {
      std::reverse(segs.begin(), segs.end());
      for (LoopSeg& seg : segs) {
        seg.c3.Reverse();
        seg.c2.Reverse();
        seg.c2.SwapCoordinates(0, 1);
      }
    }
  }
  return !out.loops.empty();
}

}  // namespace

// ===========================================================================
// IGES writer
// ===========================================================================

namespace {

std::string HString(const std::string& s) { return std::to_string(s.size()) + "H" + s; }

class IgesWriter {
 public:
  // Adds an entity; returns its DE pointer (odd sequence number).
  int Add(int type, const std::string& params, int form, int level, int color_de, int xform_de, const char* status,
          const std::string& label) {
    const std::string text = std::to_string(type) + "," + params + ";";
    const int first_line = static_cast<int>(plines_.size()) + 1;
    const int de = static_cast<int>(dlines_.size()) + 1;
    std::vector<std::string> lines = SplitParams(text);
    for (const std::string& l : lines) {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "%-64s%8dP%7d", l.c_str(), de, static_cast<int>(plines_.size()) + 1);
      plines_.emplace_back(buf);
    }
    char d1[96], d2[96];
    std::snprintf(d1, sizeof(d1), "%8d%8d%8d%8d%8d%8d%8d%8d%8sD%7d", type, first_line, 0, 0, level, 0, xform_de, 0, status, de);
    std::string lab = label.substr(0, 8);
    std::snprintf(d2, sizeof(d2), "%8d%8d%8d%8d%8d%8s%8s%-8s%8dD%7d", type, 0, color_de, static_cast<int>(lines.size()), form, "", "", lab.c_str(), 0, de + 1);
    dlines_.emplace_back(d1);
    dlines_.emplace_back(d2);
    return de;
  }

  void Write(std::ostream& os, const std::string& product, const std::string& file_name, const std::string& units_name,
             int units_flag, double tolerance, double max_coord) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%-72sS%7d", "Dino 8 IGES 5.3 export", 1);
    os << buf << "\n";
    std::time_t now = std::time(nullptr);
    char date[32];
    std::strftime(date, sizeof(date), "%Y%m%d.%H%M%S", std::localtime(&now));
    std::vector<std::string> g = {"1H,", "1H;", HString(product), HString(file_name), HString("Dino 8"), HString("Dino 8 " DINO8_VERSION),
                                  "32", "38", "6", "308", "15", HString(product), "1.0", std::to_string(units_flag), HString(units_name), "1",
                                  "0.01", HString(date), Num(tolerance), Num(max_coord), HString("Dino 8"), HString(""), "11", "0", HString(date), ""};
    std::string text;
    for (size_t i = 0; i < g.size(); ++i) text += g[i] + (i + 1 < g.size() ? "," : ";");
    std::vector<std::string> glines = SplitParams(text, 72);
    for (size_t i = 0; i < glines.size(); ++i) {
      std::snprintf(buf, sizeof(buf), "%-72sG%7d", glines[i].c_str(), static_cast<int>(i) + 1);
      os << buf << "\n";
    }
    for (const std::string& d : dlines_) os << d << "\n";
    for (const std::string& p : plines_) os << p << "\n";
    std::snprintf(buf, sizeof(buf), "S%7dG%7dD%7dP%7d%40sT%7d", 1, static_cast<int>(glines.size()), static_cast<int>(dlines_.size()),
                  static_cast<int>(plines_.size()), "", 1);
    os << buf << "\n";
  }

  int EntityCount() const { return static_cast<int>(dlines_.size() / 2); }

 private:
  // Splits free-format parameter text into lines of at most `width`
  // characters, breaking after delimiters.
  static std::vector<std::string> SplitParams(const std::string& text, size_t width = 64) {
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < text.size()) {
      size_t j = i;
      // A token runs to and including the next delimiter, but H-strings
      // contain arbitrary characters, so honour their declared length.
      if (std::isdigit(static_cast<unsigned char>(text[i]))) {
        size_t k = i;
        while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k]))) ++k;
        if (k < text.size() && text[k] == 'H') {
          const size_t n = static_cast<size_t>(std::atol(text.substr(i, k - i).c_str()));
          j = std::min(text.size(), k + 1 + n);
        }
      }
      while (j < text.size() && text[j] != ',' && text[j] != ';') ++j;
      if (j < text.size()) ++j;
      std::string tok = text.substr(i, j - i);
      i = j;
      while (tok.size() > width) {  // a single over-long token: hard split
        if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
        lines.push_back(tok.substr(0, width));
        tok = tok.substr(width);
      }
      if (cur.size() + tok.size() > width) { lines.push_back(cur); cur.clear(); }
      cur += tok;
    }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);
    return lines;
  }

  std::vector<std::string> dlines_, plines_;
};

struct IgesAttr {
  int level = 0;
  int color_de = 0;
  std::string label;
};

const char* kIndependent = "00000000";
const char* kDependent = "00010000";
const char* kDependent2D = "00010500";

std::string PointParams(const ON_3dPoint& p) { return Num(p.x) + "," + Num(p.y) + "," + Num(p.z); }

// 126 rational B-spline curve. `two_d` writes the curve as a parameter-space
// curve (z = 0, entity use flag 5).
int WriteIges126(IgesWriter& w, const ON_NurbsCurve& c, const IgesAttr& a, const char* status, bool two_d) {
  const int K = c.CVCount() - 1, M = c.Degree();
  std::string p = std::to_string(K) + "," + std::to_string(M) + ",";
  ON_Plane plane;
  const bool planar = two_d || c.IsPlanar(&plane, 1e-6);
  p += std::string(planar ? "1" : "0") + "," + (c.IsClosed() ? "1" : "0") + "," + (c.IsRational() ? "0" : "1") + ",0,";
  for (double k : FullKnots(c)) p += Num(k) + ",";
  std::vector<double> weights;
  std::vector<ON_3dPoint> pts;
  for (int i = 0; i <= K; ++i) {
    double wgt;
    pts.push_back(EuclideanCV(c, i, wgt));
    weights.push_back(wgt);
  }
  for (double wgt : weights) p += Num(wgt) + ",";
  for (const ON_3dPoint& q : pts) p += Num(q.x) + "," + Num(q.y) + "," + (two_d ? "0" : Num(q.z)) + ",";
  p += Num(c.Domain().Min()) + "," + Num(c.Domain().Max()) + ",";
  if (two_d) p += "0,0,1";
  else if (planar) p += Num(plane.zaxis.x) + "," + Num(plane.zaxis.y) + "," + Num(plane.zaxis.z);
  else p += "0,0,0";
  return w.Add(126, p, 0, a.level, a.color_de, 0, status, a.label);
}

int WriteIges124(IgesWriter& w, const ON_Plane& pl, const IgesAttr& a) {
  std::string p;
  const ON_3dVector cols[3] = {pl.xaxis, pl.yaxis, pl.zaxis};
  const double t[3] = {pl.origin.x, pl.origin.y, pl.origin.z};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) p += Num(cols[c][r]) + ",";
    p += Num(t[r]) + (r < 2 ? "," : "");
  }
  return w.Add(124, p, 0, a.level, 0, 0, kDependent, "");
}

// A free (model-space) curve: 110 for lines, 100 (+124) for arcs, else 126.
int WriteIgesCurve(IgesWriter& w, const ON_NurbsCurve& c, const IgesAttr& a, const char* status) {
  if (c.IsLinear(1e-9)) {
    return w.Add(110, PointParams(c.PointAtStart()) + "," + PointParams(c.PointAtEnd()), 0, a.level, a.color_de, 0, status, a.label);
  }
  ON_Arc arc;
  if (c.IsArc(nullptr, &arc, 1e-9)) {
    ON_Plane pl = arc.plane;
    pl.origin = arc.Center();
    const bool world_xy = std::fabs(pl.zaxis.z - 1.0) < 1e-12 && std::fabs(pl.xaxis.x - 1.0) < 1e-12;
    const double r = arc.radius, a0 = arc.DomainRadians().Min(), a1 = arc.DomainRadians().Max();
    const bool full = arc.IsCircle();
    const ON_2dPoint s(r * std::cos(a0), r * std::sin(a0));
    const ON_2dPoint e = full ? s : ON_2dPoint(r * std::cos(a1), r * std::sin(a1));
    if (world_xy) {
      const std::string p = Num(pl.origin.z) + "," + Num(pl.origin.x) + "," + Num(pl.origin.y) + "," + Num(pl.origin.x + s.x) + "," +
                            Num(pl.origin.y + s.y) + "," + Num(pl.origin.x + e.x) + "," + Num(pl.origin.y + e.y);
      return w.Add(100, p, 0, a.level, a.color_de, 0, status, a.label);
    }
    const int xf = WriteIges124(w, pl, a);
    const std::string p = "0,0,0," + Num(s.x) + "," + Num(s.y) + "," + Num(e.x) + "," + Num(e.y);
    return w.Add(100, p, 0, a.level, a.color_de, xf, status, a.label);
  }
  return WriteIges126(w, c, a, status, false);
}

int WriteIges128(IgesWriter& w, const ON_NurbsSurface& s, const IgesAttr& a, const char* status) {
  const int nu = s.CVCount(0), nv = s.CVCount(1);
  std::string p = std::to_string(nu - 1) + "," + std::to_string(nv - 1) + "," + std::to_string(s.Degree(0)) + "," + std::to_string(s.Degree(1)) + ",";
  p += std::string(s.IsClosed(0) ? "1" : "0") + "," + (s.IsClosed(1) ? "1" : "0") + "," + (s.IsRational() ? "0" : "1") + ",0,0,";
  for (double k : FullKnots(s, 0)) p += Num(k) + ",";
  for (double k : FullKnots(s, 1)) p += Num(k) + ",";
  std::vector<double> weights;
  std::vector<ON_3dPoint> pts;
  for (int j = 0; j < nv; ++j) {
    for (int i = 0; i < nu; ++i) {
      double wgt;
      pts.push_back(EuclideanCV(s, i, j, wgt));
      weights.push_back(wgt);
    }
  }
  for (double wgt : weights) p += Num(wgt) + ",";
  for (const ON_3dPoint& q : pts) p += PointParams(q) + ",";
  p += Num(s.Domain(0).Min()) + "," + Num(s.Domain(0).Max()) + "," + Num(s.Domain(1).Min()) + "," + Num(s.Domain(1).Max());
  return w.Add(128, p, 0, a.level, a.color_de, 0, status, a.label);
}

// 144 trimmed surface over 142 curves-on-surface (102 composites of 126
// curves in both parameter and model space).
int WriteIgesTrimmedFace(IgesWriter& w, const ExportFace& f, const IgesAttr& a, const char* status) {
  const int srf = WriteIges128(w, f.srf, a, kDependent);
  IgesAttr sub = a;
  sub.label.clear();
  std::vector<int> boundaries;
  for (const std::vector<LoopSeg>& loop : f.loops) {
    std::string pc, mc;
    int n = 0;
    for (const LoopSeg& seg : loop) {
      const int b = WriteIges126(w, seg.c2, sub, kDependent2D, true);
      const int m = WriteIgesCurve(w, seg.c3, sub, kDependent);
      pc += "," + std::to_string(b);
      mc += "," + std::to_string(m);
      ++n;
    }
    const int comp2 = w.Add(102, std::to_string(n) + pc, 0, a.level, 0, 0, kDependent2D, "");
    const int comp3 = w.Add(102, std::to_string(n) + mc, 0, a.level, 0, 0, kDependent, "");
    boundaries.push_back(w.Add(142, "0," + std::to_string(srf) + "," + std::to_string(comp2) + "," + std::to_string(comp3) + ",1", 0, a.level, 0, 0, kDependent, ""));
  }
  std::string p = std::to_string(srf) + ",1," + std::to_string(boundaries.size() - 1);
  for (int b : boundaries) p += "," + std::to_string(b);
  return w.Add(144, p, 0, a.level, a.color_de, 0, status, a.label);
}

}  // namespace

bool ExportIges(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  std::vector<const SceneObject*> objs = ExportObjects(doc, selected_only);
  if (objs.empty()) { error = "Nothing to export"; return false; }
  std::ofstream os(path, std::ios::binary);
  if (!os) { error = "Could not write " + path; return false; }
  IgesWriter w;
  const double tol = doc.Settings().absolute_tolerance > 0 ? doc.Settings().absolute_tolerance : 0.001;

  // Level names (406 form 3, one per layer in use).
  std::set<int> levels_used;
  for (const SceneObject* o : objs) levels_used.insert(std::max(0, o->layer_index));
  for (int li : levels_used) {
    if (li >= static_cast<int>(doc.Layers().size())) continue;
    w.Add(406, "2," + std::to_string(li) + "," + HString(doc.LayerFullPath(li)), 3, li, 0, 0, kIndependent, "");
  }
  std::map<int, int> colors;  // rgb key -> 314 DE
  auto color_de = [&](const Color& c) {
    const int key = ColorKey(c);
    auto it = colors.find(key);
    if (it != colors.end()) return it->second;
    const std::string p = Num(c.r * 100.0, 5) + "," + Num(c.g * 100.0, 5) + "," + Num(c.b * 100.0, 5) + ",";
    const int de = w.Add(314, p, 0, 0, 0, 0, kIndependent, "");
    colors[key] = de;
    return de;
  };
  auto grow = [](double& m, const ON_BoundingBox& bb) {
    if (!bb.IsValid()) return;
    m = std::max({m, std::fabs(bb.m_min.x), std::fabs(bb.m_max.x), std::fabs(bb.m_min.y), std::fabs(bb.m_max.y), std::fabs(bb.m_min.z), std::fabs(bb.m_max.z)});
  };

  int written = 0;
  double max_coord = 1.0;
  std::vector<std::string> notes;
  for (const SceneObject* o : objs) {
    IgesAttr a;
    a.level = std::max(0, o->layer_index);
    a.color_de = -color_de(doc.EffectiveColor(*o));
    a.label = o->name;
    switch (o->kind) {
      case ObjectKind::Point:
        w.Add(116, PointParams(o->point) + ",0", 0, a.level, a.color_de, 0, kIndependent, a.label);
        max_coord = std::max({max_coord, std::fabs(o->point.x), std::fabs(o->point.y), std::fabs(o->point.z)});
        ++written;
        break;
      case ObjectKind::Curve:
        if (!o->curve) break;
        WriteIgesCurve(w, o->curve->raw(), a, kIndependent);
        grow(max_coord, o->curve->raw().BoundingBox());
        ++written;
        break;
      case ObjectKind::Surface:
        if (!o->surface) break;
        WriteIges128(w, o->surface->raw(), a, kIndependent);
        grow(max_coord, o->surface->raw().BoundingBox());
        ++written;
        break;
      case ObjectKind::Brep: {
        if (!o->brep) break;
        const ON_Brep& b = o->brep->raw();
        std::vector<int> faces;
        for (int fi = 0; fi < b.m_F.Count(); ++fi) {
          if (b.m_F[fi].m_face_index < 0) continue;
          ExportFace f;
          if (!PrepareExportFace(b, b.m_F[fi], tol, f)) continue;
          faces.push_back(WriteIgesTrimmedFace(w, f, a, kIndependent));
        }
        if (faces.size() > 1) {
          std::string p = std::to_string(faces.size());
          for (int de : faces) p += "," + std::to_string(de);
          w.Add(402, p, 7, a.level, 0, 0, kIndependent, a.label);
        }
        grow(max_coord, b.BoundingBox());
        if (!faces.empty()) ++written;
        break;
      }
      case ObjectKind::Mesh:
      case ObjectKind::SubD: {
        std::optional<kernel::Mesh> km;
        if (o->kind == ObjectKind::Mesh && o->mesh) km = *o->mesh;
        else if (o->subd) km = o->subd->ToApproximateMesh();
        if (!km) break;
        const ON_Mesh& m = km->raw();
        if (m.FaceCount() > 2000) {
          notes.push_back((o->name.empty() ? std::string("a mesh") : o->name) + " with " + std::to_string(m.FaceCount()) +
                          " faces was skipped (IGES has no mesh entity; meshes up to 2000 faces are written as bilinear surfaces)");
          break;
        }
        std::vector<int> faces;
        for (int fi = 0; fi < m.FaceCount(); ++fi) {
          const ON_MeshFace& f = m.m_F[fi];
          ON_3dPoint p0 = m.m_V[f.vi[0]], p1 = m.m_V[f.vi[1]], p2 = m.m_V[f.vi[2]], p3 = m.m_V[f.vi[3]];
          ON_NurbsSurface s;
          s.Create(3, false, 2, 2, 2, 2);
          s.SetKnot(0, 0, 0); s.SetKnot(0, 1, 1); s.SetKnot(1, 0, 0); s.SetKnot(1, 1, 1);
          s.SetCV(0, 0, p0); s.SetCV(1, 0, p1); s.SetCV(0, 1, p3); s.SetCV(1, 1, p2);
          faces.push_back(WriteIges128(w, s, a, kIndependent));
        }
        if (faces.size() > 1) {
          std::string p = std::to_string(faces.size());
          for (int de : faces) p += "," + std::to_string(de);
          w.Add(402, p, 7, a.level, 0, 0, kIndependent, a.label);
        }
        grow(max_coord, m.BoundingBox());
        if (!faces.empty()) ++written;
        break;
      }
    }
  }
  const std::string& u = doc.Settings().unit_system;
  int flag = 2;
  std::string uname = "MM";
  if (u == "Inches") { flag = 1; uname = "IN"; } else if (u == "Feet") { flag = 4; uname = "FT"; } else if (u == "Meters") { flag = 6; uname = "M"; } else if (u == "Centimeters") { flag = 10; uname = "CM"; }
  w.Write(os, doc.Settings().title.empty() ? std::filesystem::path(path).stem().string() : doc.Settings().title,
          std::filesystem::path(path).filename().string(), uname, flag, tol, max_coord);
  if (!os) { error = "Could not write " + path; return false; }
  if (written == 0) { error = "Nothing exportable in the selection"; return false; }
  error.clear();
  for (const std::string& n : notes) error += (error.empty() ? "" : "; ") + n;
  return true;
}

// ===========================================================================
// IGES reader
// ===========================================================================

namespace {

struct IgesRawEntity {
  int de = 0;           // directory entry pointer (1-based, odd)
  int type = 0;
  int form = 0;
  int level = 0;
  int color = 0;         // >0 = DE of a 314; <0 = -ACI (a handful of standard colours)
  int xform = 0;         // DE of a 124, or 0
  std::string label;
  std::vector<std::string> params;  // free-format fields, in order
};

// Splits IGES free-format parameter text on the given delimiters, honouring
// Hollerith strings (nHtext) which may themselves contain delimiters.
std::vector<std::string> SplitIgesParams(const std::string& text, char pd, char rd) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= text.size()) {
    size_t j = i;
    if (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) {
      size_t k = j;
      while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k]))) ++k;
      if (k < text.size() && text[k] == 'H') {
        const size_t n = static_cast<size_t>(std::atol(text.substr(j, k - j).c_str()));
        j = std::min(text.size(), k + 1 + n);
        out.push_back(text.substr(i, j - i));
        if (j < text.size() && (text[j] == pd || text[j] == rd)) ++j;
        i = j;
        if (j > text.size() || (j == text.size())) break;
        continue;
      }
    }
    while (j < text.size() && text[j] != pd && text[j] != rd) ++j;
    out.push_back(text.substr(i, j - i));
    if (j >= text.size()) break;
    if (text[j] == rd) { i = text.size() + 1; break; }
    i = j + 1;
  }
  return out;
}

double PNum(const std::vector<std::string>& p, size_t i, double def = 0.0) {
  if (i >= p.size() || p[i].empty()) return def;
  std::string s = p[i];
  // IGES allows a bare 'D' exponent (Fortran-style); normalise to 'E'.
  for (char& c : s) if (c == 'D' || c == 'd') c = 'E';
  return std::atof(s.c_str());
}
int PInt(const std::vector<std::string>& p, size_t i, int def = 0) {
  if (i >= p.size() || p[i].empty()) return def;
  return std::atoi(p[i].c_str());
}
std::string PStr(const std::vector<std::string>& p, size_t i) {
  if (i >= p.size()) return "";
  const std::string& s = p[i];
  const size_t h = s.find('H');
  if (h != std::string::npos) return s.substr(h + 1);
  return s;
}

struct IgesImportStats {
  int curves = 0, points = 0, surfaces = 0, breps = 0, faces_trimmed = 0, faces_untrimmed_fallback = 0, meshes = 0, groups = 0, skipped = 0, layers = 0;
};

class IgesImporter {
 public:
  IgesImporter(Document& doc, IgesImportStats& stats, double tol) : doc_(doc), stats_(stats), tol_(tol) {}

  void SetLevelName(int level, const std::string& name) { level_names_[level] = name; }

  int LayerFor(int level) {
    auto it = layer_map_.find(level);
    if (it != layer_map_.end()) return it->second;
    std::string name = level_names_.count(level) ? level_names_[level] : ("Level " + std::to_string(level));
    int idx = doc_.FindLayer(name);
    if (idx < 0) { idx = doc_.AddLayer(name); ++stats_.layers; }
    layer_map_[level] = idx;
    return idx;
  }

  ON_Xform TransformOf(const IgesRawEntity& e, const std::map<int, IgesRawEntity>& des) {
    if (e.xform <= 0) return ON_Xform::IdentityTransformation;
    auto it = des.find(e.xform);
    if (it == des.end() || it->second.type != 124) return ON_Xform::IdentityTransformation;
    const std::vector<std::string>& p = it->second.params;
    ON_Xform x = ON_Xform::IdentityTransformation;
    int k = 0;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) x[r][c] = PNum(p, static_cast<size_t>(k++));
    for (int r = 0; r < 3; ++r) x[r][3] = PNum(p, static_cast<size_t>(k++));
    // Compose with the referenced transform's own xform, if any.
    ON_Xform parent = TransformOf(it->second, des);
    return parent * x;
  }

  Color ColorOf(const IgesRawEntity& e, const std::map<int, IgesRawEntity>& des, bool& has_color) {
    has_color = false;
    if (e.color > 0) {
      auto it = des.find(e.color);
      if (it != des.end() && it->second.type == 314) {
        has_color = true;
        return ColorFromBytes(static_cast<int>(std::lround(PNum(it->second.params, 0) * 2.55)),
                              static_cast<int>(std::lround(PNum(it->second.params, 1) * 2.55)),
                              static_cast<int>(std::lround(PNum(it->second.params, 2) * 2.55)));
      }
    } else if (e.color < 0) {
      static const std::array<std::array<int, 3>, 9> kAci = {{{0, 0, 0}, {0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}}};
      const int idx = -e.color;
      if (idx >= 1 && idx <= 8) { has_color = true; return ColorFromBytes(kAci[idx][0], kAci[idx][1], kAci[idx][2]); }
    }
    return Color::FromBytes(0, 0, 0);
  }

  void ApplyAttrs(SceneObject& o, const IgesRawEntity& e, const std::map<int, IgesRawEntity>& des) {
    o.layer_index = LayerFor(e.level);
    o.name = e.label;
    bool has_color;
    Color c = ColorOf(e, des, has_color);
    if (has_color) { o.color_by_layer = false; o.color = c; }
  }

  void AddCurveObj(kernel::NurbsCurve k, const IgesRawEntity& e, const std::map<int, IgesRawEntity>& des) {
    const ON_Xform x = TransformOf(e, des);
    if (x != ON_Xform::IdentityTransformation) k.raw().Transform(x);
    SceneObject o = SceneObject::MakeCurve(k);
    ApplyAttrs(o, e, des);
    doc_.Add(std::move(o));
    ++stats_.curves;
  }

  void AddPoint(const IgesRawEntity& e, const std::map<int, IgesRawEntity>& des) {
    ON_3dPoint p(PNum(e.params, 0), PNum(e.params, 1), PNum(e.params, 2));
    const ON_Xform x = TransformOf(e, des);
    p = x * p;
    SceneObject o = SceneObject::MakePoint(p);
    ApplyAttrs(o, e, des);
    doc_.Add(std::move(o));
    ++stats_.points;
  }

  void AddSurfaceObj(ON_NurbsSurface s, const IgesRawEntity& e, const std::map<int, IgesRawEntity>& des) {
    const ON_Xform x = TransformOf(e, des);
    if (x != ON_Xform::IdentityTransformation) s.Transform(x);
    kernel::NurbsSurface k;
    k.raw() = s;
    SceneObject o = SceneObject::MakeSurface(k);
    ApplyAttrs(o, e, des);
    doc_.Add(std::move(o));
    ++stats_.surfaces;
  }

  // A 144 (or 143) trimmed surface, or an untrimmed 128 surface used as a
  // brep face on its own.
  void AddFaceAsBrep(ON_Brep* face_only, const IgesRawEntity& e, const std::map<int, IgesRawEntity>& des, int group_de = 0) {
    const ON_Xform x = TransformOf(e, des);
    if (x != ON_Xform::IdentityTransformation) face_only->Transform(x);
    FinishBrep(*face_only);
    if (group_de) {
      group_faces_[group_de].push_back(std::unique_ptr<ON_Brep>(face_only));
      group_attr_[group_de] = &e;
      return;
    }
    kernel::Brep k;
    k.raw() = *face_only;
    delete face_only;
    SceneObject o = SceneObject::MakeBrep(k);
    ApplyAttrs(o, e, des);
    doc_.Add(std::move(o));
    ++stats_.breps;
  }

  // 402 group: join every member face into one brep.
  void FlushGroups(const std::map<int, IgesRawEntity>& des) {
    for (auto& [gde, faces] : group_faces_) {
      if (faces.empty()) continue;
      ON_Brep merged;
      for (auto& f : faces) merged.Append(*f);
      JoinEdges(merged, tol_ * 10);
      FinishBrep(merged);
      kernel::Brep k;
      k.raw() = merged;
      SceneObject o = SceneObject::MakeBrep(k);
      if (group_attr_.count(gde)) ApplyAttrs(o, *group_attr_[gde], des);
      doc_.Add(std::move(o));
      ++stats_.breps;
      ++stats_.groups;
    }
  }

  void AddMesh(kernel::Mesh m, const IgesRawEntity& e, const std::map<int, IgesRawEntity>& des) {
    SceneObject o = SceneObject::MakeMesh(std::move(m));
    ApplyAttrs(o, e, des);
    doc_.Add(std::move(o));
    ++stats_.meshes;
  }

  void Skip() { ++stats_.skipped; }

  Document& doc_;
  IgesImportStats& stats_;
  double tol_;
  std::map<int, int> layer_map_;
  std::map<int, std::string> level_names_;
  std::map<int, std::vector<std::unique_ptr<ON_Brep>>> group_faces_;
  std::map<int, const IgesRawEntity*> group_attr_;
};

// Builds a curve entity (100/104/106/110/112/126) as a kernel-ready NURBS
// curve, in model-space or parameter-space per `two_d`.
bool BuildIgesCurve(const IgesRawEntity& e, const std::map<int, IgesRawEntity>& des, ON_NurbsCurve& out) {
  const std::vector<std::string>& p = e.params;
  switch (e.type) {
    case 110: {  // line
      const ON_3dPoint p0(PNum(p, 0), PNum(p, 1), PNum(p, 2)), p1(PNum(p, 3), PNum(p, 4), PNum(p, 5));
      return MakeNurbsCurve(3, 1, {p0, p1}, {}, {0.0, 0.0, 1.0, 1.0}, out);
    }
    case 100: {  // circular arc, in a plane z = ZT
      const double zt = PNum(p, 0), cx = PNum(p, 1), cy = PNum(p, 2), sx = PNum(p, 3), sy = PNum(p, 4), ex = PNum(p, 5), ey = PNum(p, 6);
      const ON_3dPoint c(cx, cy, zt);
      const double r = std::hypot(sx - cx, sy - cy);
      if (r < 1e-12) return false;
      double a0 = std::atan2(sy - cy, sx - cx), a1 = std::atan2(ey - cy, ex - cx);
      const bool full = std::fabs(sx - ex) < 1e-9 && std::fabs(sy - ey) < 1e-9;
      while (a1 <= a0 + 1e-12) a1 += 2 * ON_PI;
      ON_Circle circ(ON_Plane(c, ON_xaxis, ON_yaxis), r);
      ON_ArcCurve ac(full ? ON_Arc(circ, ON_Interval(0, 2 * ON_PI)) : ON_Arc(circ, ON_Interval(a0, a1)));
      return ac.GetNurbForm(out) > 0;
    }
    case 104: {  // conic (ellipse arc, when in general position); build as a
                 // rational NURBS ellipse arc, which covers the practical
                 // (elliptical) case this importer targets.
      const double a = PNum(p, 0), b = PNum(p, 1), c = PNum(p, 2), d = PNum(p, 3), ee = PNum(p, 4), f = PNum(p, 5);
      const double zt = PNum(p, 6), x1 = PNum(p, 7), y1 = PNum(p, 8), x2 = PNum(p, 9), y2 = PNum(p, 10);
      // A x^2 + B xy + C y^2 + D x + E y + F = 0, axis-aligned after
      // centring (handles the common case of B == 0; skewed conics fall
      // back to a coarse polyline through the two endpoints, which callers
      // treat as unsupported by checking IsValid on the result curve).
      if (std::fabs(b) > 1e-9) return false;
      if (std::fabs(a) < 1e-12 || std::fabs(c) < 1e-12) return false;
      const double cx = -d / (2 * a), cy = -ee / (2 * c);
      const double rhs = a * cx * cx + c * cy * cy - f;
      if (rhs <= 0) return false;
      const double rx = std::sqrt(rhs / a), ry = std::sqrt(rhs / c);
      if (!(rx > 0) || !(ry > 0)) return false;
      ON_Ellipse el(ON_Plane(ON_3dPoint(cx, cy, zt), ON_xaxis, ON_yaxis), rx, ry);
      const double t1 = std::atan2((y1 - cy) / ry, (x1 - cx) / rx), t2 = std::atan2((y2 - cy) / ry, (x2 - cx) / rx);
      const bool full = std::fabs(x1 - x2) < 1e-6 && std::fabs(y1 - y2) < 1e-6;
      ON_NurbsCurve nc;
      if (!el.GetNurbForm(nc)) return false;
      if (!full) {
        double tt1 = t1, tt2 = t2;
        while (tt2 <= tt1 + 1e-12) tt2 += 2 * ON_PI;
        // Trim by remapping through an ON_ArcCurve isn't available for
        // ellipses; sample instead.
        const int n = 64;
        ON_Polyline pl;
        for (int i = 0; i <= n; ++i) {
          const double t = tt1 + (tt2 - tt1) * i / n;
          pl.Append(el.PointAt(t));
        }
        ON_PolylineCurve pc(pl);
        return pc.GetNurbForm(out) > 0;
      }
      out = nc;
      return out.IsValid();
    }
    case 106: {  // copious data (polyline / polygon), forms 11/12/63 (and 2/1)
      const int ip = PInt(p, 0);
      size_t idx = 1;
      std::vector<ON_3dPoint> pts;
      if (e.form == 11 || e.form == 63) {
        const int n = PInt(p, idx++);
        const double z = ip == 1 ? PNum(p, idx++) : 0.0;
        for (int i = 0; i < n; ++i) { const double x = PNum(p, idx++), y = PNum(p, idx++); pts.emplace_back(x, y, ip == 1 ? z : 0.0); }
      } else if (e.form == 12 || e.form == 1) {
        const int n = PInt(p, idx++);
        for (int i = 0; i < n; ++i) { const double x = PNum(p, idx++), y = PNum(p, idx++), z = PNum(p, idx++); pts.emplace_back(x, y, z); }
      } else {
        return false;
      }
      if (pts.size() < 2) return false;
      ON_Polyline pl;
      for (const ON_3dPoint& q : pts) pl.Append(q);
      if (e.form == 63 && pl.Count() > 1 && pl[0].DistanceTo(pl[pl.Count() - 1]) > 1e-9) pl.Append(pl[0]);
      ON_PolylineCurve pc(pl);
      return pc.GetNurbForm(out) > 0;
    }
    case 112: {  // parametric spline curve -> sample and fit a NURBS (cubic
                 // Hermite per segment is exact for cubic C-type; sampling
                 // keeps the reader simple and robust across all C-types).
      const int ctype = PInt(p, 0), ndim = PInt(p, 2), nseg = PInt(p, 3);
      size_t idx = 4;
      std::vector<double> tvals(static_cast<size_t>(nseg) + 1);
      for (int i = 0; i <= nseg; ++i) tvals[static_cast<size_t>(i)] = PNum(p, idx++);
      std::vector<std::array<std::array<double, 4>, 3>> coef(static_cast<size_t>(nseg));
      for (int s = 0; s < nseg; ++s) {
        for (int d = 0; d < ndim && d < 3; ++d) for (int k = 0; k < 4; ++k) coef[static_cast<size_t>(s)][static_cast<size_t>(d)][static_cast<size_t>(k)] = PNum(p, idx++);
        for (int d = ndim; d < 3; ++d) for (int k = 0; k < 4; ++k) idx++;
        if (ndim > 3) idx += static_cast<size_t>(ndim - 3) * 4;
      }
      (void)ctype;
      ON_Polyline pl;
      for (int s = 0; s < nseg; ++s) {
        const int n = 16;
        for (int i = (s == 0 ? 0 : 1); i <= n; ++i) {
          const double tt = tvals[static_cast<size_t>(s)] + (tvals[static_cast<size_t>(s) + 1] - tvals[static_cast<size_t>(s)]) * i / n;
          const double u = tt - tvals[static_cast<size_t>(s)];
          double xyz[3] = {0, 0, 0};
          for (int d = 0; d < 3; ++d) { const auto& cf = coef[static_cast<size_t>(s)][static_cast<size_t>(d)]; xyz[d] = cf[0] + u * (cf[1] + u * (cf[2] + u * cf[3])); }
          pl.Append(ON_3dPoint(xyz[0], xyz[1], xyz[2]));
        }
      }
      if (pl.Count() < 2) return false;
      ON_PolylineCurve pc(pl);
      return pc.GetNurbForm(out) > 0;
    }
    case 126: {  // rational B-spline curve
      const int K = PInt(p, 0), M = PInt(p, 1);
      const int n = K + 1, degree = M;
      size_t idx = 6;
      std::vector<double> knots(static_cast<size_t>(n + degree + 1));
      for (auto& k : knots) k = PNum(p, idx++);
      std::vector<double> weights(static_cast<size_t>(n));
      for (auto& wv : weights) wv = PNum(p, idx++);
      const bool all_one = std::all_of(weights.begin(), weights.end(), [](double w) { return std::fabs(w - 1.0) < 1e-9; });
      std::vector<ON_3dPoint> cvs(static_cast<size_t>(n));
      for (auto& q : cvs) { q.x = PNum(p, idx++); q.y = PNum(p, idx++); q.z = PNum(p, idx++); }
      return MakeNurbsCurve(3, degree, cvs, all_one ? std::vector<double>() : weights, knots, out);
    }
    case 102: {  // composite curve: N child curve DE pointers, joined end to end
      const int n = PInt(p, 0);
      if (n <= 0) return false;
      std::vector<ON_NurbsCurve> segs;
      segs.reserve(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i) {
        const int cde = PInt(p, static_cast<size_t>(1 + i));
        auto it = des.find(cde);
        if (it == des.end()) return false;
        ON_NurbsCurve seg;
        if (!BuildIgesCurve(it->second, des, seg) || !seg.IsValid()) return false;
        segs.push_back(std::move(seg));
      }
      if (segs.size() == 1) { out = segs[0]; return true; }
      ON_PolyCurve poly;
      for (const ON_NurbsCurve& s : segs) poly.Append(new ON_NurbsCurve(s));
      if (poly.GetNurbForm(out) > 0 && out.IsValid()) return true;
      // Segments differ in degree (can't join into one exact NURBS form):
      // sample each segment into a single polyline instead.
      ON_Polyline pl;
      for (size_t i = 0; i < segs.size(); ++i) {
        const ON_Interval dom = segs[i].Domain();
        const int m = 16;
        for (int k = (i == 0 ? 0 : 1); k <= m; ++k) pl.Append(segs[i].PointAt(dom.ParameterAt(static_cast<double>(k) / m)));
      }
      if (pl.Count() < 2) return false;
      ON_PolylineCurve plc(pl);
      return plc.GetNurbForm(out) > 0;
    }
    default:
      return false;
  }
}

bool BuildIgesSurface(const IgesRawEntity& e, ON_NurbsSurface& out) {
  const std::vector<std::string>& p = e.params;
  switch (e.type) {
    case 128: {
      const int K1 = PInt(p, 0), K2 = PInt(p, 1), M1 = PInt(p, 2), M2 = PInt(p, 3);
      const int n1 = K1 + 1, n2 = K2 + 1;
      // Header is K1,K2,M1,M2,PROP1..PROP5 = 9 scalars (indices 0-8); the
      // U knot vector starts at index 9, not 10.
      size_t idx = 9;
      std::vector<double> ku(static_cast<size_t>(n1 + M1 + 1)), kv(static_cast<size_t>(n2 + M2 + 1));
      for (auto& k : ku) k = PNum(p, idx++);
      for (auto& k : kv) k = PNum(p, idx++);
      std::vector<double> weights(static_cast<size_t>(n1 * n2));
      for (auto& wv : weights) wv = PNum(p, idx++);
      const bool all_one = std::all_of(weights.begin(), weights.end(), [](double w) { return std::fabs(w - 1.0) < 1e-9; });
      std::vector<ON_3dPoint> cvs(static_cast<size_t>(n1 * n2));
      for (auto& q : cvs) { q.x = PNum(p, idx++); q.y = PNum(p, idx++); q.z = PNum(p, idx++); }
      return MakeNurbsSurface(M1, M2, n1, n2, cvs, all_one ? std::vector<double>() : weights, ku, kv, out);
    }
    case 118: {  // ruled surface between two curves (by DE pointer, resolved by caller)
      return false;  // handled specially (needs the entity map)
    }
    default:
      return false;
  }
}

}  // namespace

bool ImportIges(Document& doc, const std::string& path, std::string& summary) {
  summary.clear();
  std::ifstream is(path, std::ios::binary);
  if (!is) { summary = "Could not open " + path; return false; }
  std::string line;
  std::vector<std::string> slines, glines, dlines, plines;
  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 73) line.resize(73, ' ');
    const char sect = line[72];
    const std::string body = line.substr(0, 72);
    if (sect == 'S') slines.push_back(body);
    else if (sect == 'G') glines.push_back(body);
    else if (sect == 'D') dlines.push_back(body);
    // A Parameter Data line's columns 1-64 are the free-format data;
    // columns 65-72 carry the entity's DE back-pointer (right-justified),
    // not data - keeping them would splice that number onto whatever
    // token follows at the start of the next continuation line.
    else if (sect == 'P') plines.push_back(line.substr(0, 64));
    else if (sect == 'T') break;
  }
  if (dlines.empty() && slines.empty()) { summary = "Not an IGES file: " + path; return false; }

  char pd = ',', rd = ';';
  {
    std::string g;
    for (const std::string& l : glines) g += l;
    if (g.size() >= 2 && g[1] == 'H') { pd = g[2]; /* find rd right after */
      // "1H,,1H;;" style: parse first two hollerith params explicitly
    }
    // Robust: locate "1H" + pd-char pattern for the parameter delimiter,
    // then the record delimiter follows the same way after it.
    size_t i = 0;
    if (g.rfind("1H", 0) == 0 && g.size() > 2) { pd = g[2]; i = 3; if (g[i] == pd) ++i; if (i < g.size() && g.compare(i, 2, "1H") == 0 && g.size() > i + 2) rd = g[i + 2]; }
  }

  // Directory entries: two 80-char (72 usable) lines each.
  std::map<int, IgesRawEntity> entities;
  for (size_t i = 0; i + 1 < dlines.size(); i += 2) {
    IgesRawEntity e;
    e.de = static_cast<int>(i) + 1;
    auto field = [&](const std::string& l, int col) { return Trim(l.substr(static_cast<size_t>(col) * 8, 8)); };
    e.type = std::atoi(field(dlines[i], 0).c_str());
    const int pd_ptr = std::atoi(field(dlines[i], 1).c_str());
    e.level = std::atoi(field(dlines[i], 4).c_str());
    e.xform = std::atoi(field(dlines[i], 6).c_str());
    const std::string cf = field(dlines[i + 1], 2);
    e.color = std::atoi(cf.c_str());
    e.form = std::atoi(field(dlines[i + 1], 4).c_str());
    e.label = field(dlines[i + 1], 7);
    // Gather this entity's parameter lines by their declared start and
    // count (columns 2 and 4 of the second DE line, 1-based).
    const int line_count = std::max(1, std::atoi(field(dlines[i + 1], 3).c_str()));
    std::string text;
    for (int k = 0; k < line_count && pd_ptr - 1 + k < static_cast<int>(plines.size()); ++k) text += plines[static_cast<size_t>(pd_ptr - 1 + k)];
    // Drop the leading "type," field.
    const size_t comma = text.find(pd);
    std::vector<std::string> all = SplitIgesParams(comma == std::string::npos ? text : text.substr(comma + 1), pd, rd);
    e.params = all;
    entities[e.de] = std::move(e);
  }

  const double tol = doc.Settings().absolute_tolerance > 0 ? doc.Settings().absolute_tolerance : 0.001;
  IgesImportStats stats;
  IgesImporter importer(doc, stats, tol);

  for (const auto& [de, e] : entities) if (e.type == 406 && e.form == 3) importer.SetLevelName(PInt(e.params, 1), PStr(e.params, 2));

  // Which DE pointers are referenced by a 402 group / 308 subfigure, so
  // their top-level entity isn't also added standalone.
  std::map<int, int> face_owner;  // DE of the face entity -> DE of its group
  for (const auto& [de, e] : entities) {
    if (e.type != 402) continue;
    const int n = PInt(e.params, 0);
    for (int i = 0; i < n; ++i) face_owner[PInt(e.params, static_cast<size_t>(1 + i))] = de;
  }

  for (const auto& [de, e] : entities) {
    if (e.type == 314 || e.type == 124 || e.type == 406 || e.type == 402 || e.type == 142 || e.type == 102) continue;  // support entities
    switch (e.type) {
      case 116: importer.AddPoint(e, entities); break;
      case 100: case 104: case 106: case 110: case 112: case 126: {
        ON_NurbsCurve nc;
        if (BuildIgesCurve(e, entities, nc) && nc.IsValid()) { kernel::NurbsCurve k; k.raw() = nc; importer.AddCurveObj(k, e, entities); }
        else importer.Skip();
        break;
      }
      case 118: {  // ruled surface between two curve DEs
        const int c1 = PInt(e.params, 0), c2 = PInt(e.params, 1);
        auto it1 = entities.find(c1), it2 = entities.find(c2);
        ON_NurbsCurve a, b;
        if (it1 != entities.end() && it2 != entities.end() && BuildIgesCurve(it1->second, entities, a) && BuildIgesCurve(it2->second, entities, b)) {
          ON_NurbsSurface ns;
          if (ns.CreateRuledSurface(a, b) > 0 && ns.IsValid()) { importer.AddSurfaceObj(ns, e, entities); break; }
        }
        importer.Skip();
        break;
      }
      case 120: {  // surface of revolution: axis line DE, generatrix curve DE, angles
        const int lde = PInt(e.params, 0), cde = PInt(e.params, 1);
        auto itl = entities.find(lde), itc = entities.find(cde);
        if (itl != entities.end() && itc != entities.end()) {
          ON_NurbsCurve axisc, gen;
          if (BuildIgesCurve(itl->second, entities, axisc) && BuildIgesCurve(itc->second, entities, gen)) {
            ON_Line axis(axisc.PointAtStart(), axisc.PointAtEnd());
            const double a0 = PNum(e.params, 2), a1 = PNum(e.params, 3);
            ON_RevSurface* rs = ON_RevSurface::New();
            rs->m_curve = new ON_NurbsCurve(gen);
            rs->m_axis = axis;
            rs->m_angle = ON_Interval(a0, a1);
            rs->m_t = rs->m_angle;
            ON_NurbsSurface ns;
            if (rs->GetNurbForm(ns) > 0 && ns.IsValid()) { delete rs; importer.AddSurfaceObj(ns, e, entities); break; }
            delete rs;
          }
        }
        importer.Skip();
        break;
      }
      case 122: {  // tabulated cylinder: directrix curve DE + terminate point
        const int cde = PInt(e.params, 0);
        auto itc = entities.find(cde);
        if (itc != entities.end()) {
          ON_NurbsCurve dir;
          if (BuildIgesCurve(itc->second, entities, dir)) {
            const ON_3dPoint term(PNum(e.params, 1), PNum(e.params, 2), PNum(e.params, 3));
            const ON_3dVector v = term - dir.PointAtStart();
            ON_SumSurface ss;
            if (ss.Create(dir, v)) {
              ON_NurbsSurface ns;
              if (ss.GetNurbForm(ns) > 0 && ns.IsValid()) { importer.AddSurfaceObj(ns, e, entities); break; }
            }
          }
        }
        importer.Skip();
        break;
      }
      case 128: {
        ON_NurbsSurface ns;
        if (BuildIgesSurface(e, ns) && ns.IsValid()) {
          if (face_owner.count(de)) {
            // Standalone 128 used as a group member: an untrimmed face.
            ON_Brep* b = new ON_Brep();
            b->NewFace(ns);
            importer.AddFaceAsBrep(b, e, entities, face_owner[de]);
          } else {
            importer.AddSurfaceObj(ns, e, entities);
          }
        } else importer.Skip();
        break;
      }
      case 140: case 141: case 143: case 144: {
        // Trimmed surface: params[0]=surface DE (140/143: plane-based
        // forms are rare; this reader focuses on 144 over a 128, the form
        // the writer and most CAD tools use). Layout: PTS, N1, N2, [PTO if N1=1],
        // then N2 inner boundary (142) pointers. N1=0 means the surface's
        // own natural edge is the outer boundary (no PTO in the file);
        // N1=1 means PTO gives an explicit outer boundary curve.
        const int srf_de = PInt(e.params, 0);
        const int n1 = PInt(e.params, 1);
        const int n2 = PInt(e.params, 2);
        size_t idx = 3;
        int outer_142 = 0;
        if (n1 == 1) outer_142 = PInt(e.params, idx++);
        std::vector<int> inner;
        for (int i = 0; i < n2; ++i) inner.push_back(PInt(e.params, idx++));
        auto its = entities.find(srf_de);
        if (its == entities.end()) { importer.Skip(); break; }
        ON_NurbsSurface ns;
        if (!BuildIgesSurface(its->second, ns) || !ns.IsValid()) { importer.Skip(); break; }

        // A 142 gives a curve-on-surface: pointers to the surface, the 2D
        // (parameter space) curve and/or the 3D (model space) curve. Either
        // pointer may itself be a 102 composite curve stitching several
        // segments end to end; each segment must become its own LoopSeg
        // (and so its own ON_Brep edge) so adjacent faces can share edges -
        // collapsing the composite into one joined curve here would give
        // every face a single edge that never matches its neighbours'.
        auto expand_composite = [&](int de) -> std::vector<int> {
          std::vector<int> out;
          if (!de) return out;
          auto it = entities.find(de);
          if (it == entities.end()) return out;
          if (it->second.type != 102) { out.push_back(de); return out; }
          const int n = PInt(it->second.params, 0);
          for (int i = 0; i < n; ++i) out.push_back(PInt(it->second.params, static_cast<size_t>(1 + i)));
          return out;
        };
        auto build_loop = [&](int b142, bool is_outer, std::vector<LoopSeg>& out_segs) -> bool {
          auto it142 = entities.find(b142);
          if (it142 == entities.end() || it142->second.type != 142) return false;
          // 142 params: CRTN(0), SPTR surface(1), BPTR 2D/param-space curve(2), CPTR 3D/model-space curve(3), PREF(4).
          const int c2_de = PInt(it142->second.params, 2), c3_de = PInt(it142->second.params, 3);
          const int pref = PInt(it142->second.params, 4);  // 1: use pcurve, 2: use 3D curve, 3: either
          (void)pref; (void)is_outer;
          const std::vector<int> c2_list = expand_composite(c2_de), c3_list = expand_composite(c3_de);
          const size_t n = std::max(c2_list.size(), c3_list.size());
          if (n == 0) return false;
          for (size_t i = 0; i < n; ++i) {
            ON_NurbsCurve c2, c3;
            bool have2 = false, have3 = false;
            if (i < c2_list.size()) { auto it = entities.find(c2_list[i]); if (it != entities.end() && BuildIgesCurve(it->second, entities, c2)) have2 = true; }
            if (i < c3_list.size()) { auto it = entities.find(c3_list[i]); if (it != entities.end() && BuildIgesCurve(it->second, entities, c3)) have3 = true; }
            if (!have2 && !have3) continue;
            if (!have2) { if (!ProjectToSurface(ns, c3, tol, c2)) continue; }
            if (!have3) { if (!LiftToSurface(ns, c2, c3)) continue; }
            LoopSeg seg;
            seg.c2 = c2; seg.c2.ChangeDimension(3);
            seg.c3 = c3;
            out_segs.push_back(std::move(seg));
          }
          return !out_segs.empty();
        };

        std::vector<std::vector<LoopSeg>> loops;
        std::vector<LoopSeg> outer_segs;
        bool have_outer = false;
        if (outer_142) have_outer = build_loop(outer_142, true, outer_segs);
        if (have_outer) loops.push_back(std::move(outer_segs));
        for (int b : inner) { std::vector<LoopSeg> segs; if (build_loop(b, false, segs)) loops.push_back(std::move(segs)); }
        ON_Brep* b = new ON_Brep();
        if (!loops.empty()) {
          const int fi = AddTrimmedFace(*b, new ON_NurbsSurface(ns), loops, tol);
          // IsValid() checks that each trim's m_iso flag matches its actual
          // direction on the surface; that flag is only computed by
          // SetTrimIsoFlags(), so it must run before the validity check or
          // every axis-aligned trim (the common case for planar/bilinear
          // patches) reads as invalid and forces the untrimmed fallback.
          if (fi >= 0) b->SetTrimIsoFlags();
          if (fi >= 0 && b->IsValid()) {
            ++stats.faces_trimmed;
            importer.AddFaceAsBrep(b, e, entities, face_owner.count(de) ? face_owner[de] : 0);
            break;
          }
          delete b;
          b = new ON_Brep();
        }
        // Fall back to the untrimmed surface.
        b->NewFace(ns);
        ++stats.faces_untrimmed_fallback;
        importer.AddFaceAsBrep(b, e, entities, face_owner.count(de) ? face_owner[de] : 0);
        break;
      }
      case 308: break;  // subfigure definitions have no geometry of their own
      case 408: {  // subfigure instance: exploded copy of the referenced 308's members
        const int def_de = PInt(e.params, 0);
        auto itdef = entities.find(def_de);
        if (itdef == entities.end()) { importer.Skip(); break; }
        // Members are the DEs listed in the 308's own parameter list.
        const int n = PInt(itdef->second.params, 2);
        for (int i = 0; i < n; ++i) {
          const int mde = PInt(itdef->second.params, static_cast<size_t>(3 + i));
          auto itm = entities.find(mde);
          if (itm == entities.end()) continue;
          IgesRawEntity copy = itm->second;
          copy.xform = e.xform ? e.xform : copy.xform;
          ON_NurbsCurve nc;
          if (BuildIgesCurve(copy, entities, nc) && nc.IsValid()) { kernel::NurbsCurve k; k.raw() = nc; importer.AddCurveObj(k, copy, entities); }
        }
        break;
      }
      default:
        importer.Skip();
        break;
    }
  }
  importer.FlushGroups(entities);

  std::ostringstream ss;
  ss << "IGES: " << stats.curves << " curve" << (stats.curves == 1 ? "" : "s") << ", " << stats.points << " point" << (stats.points == 1 ? "" : "s")
     << ", " << stats.surfaces << " surface" << (stats.surfaces == 1 ? "" : "s") << ", " << stats.breps << " brep" << (stats.breps == 1 ? "" : "s")
     << " (" << stats.faces_trimmed << " trimmed face" << (stats.faces_trimmed == 1 ? "" : "s") << ")";
  if (stats.faces_untrimmed_fallback) ss << ", " << stats.faces_untrimmed_fallback << " face" << (stats.faces_untrimmed_fallback == 1 ? "" : "s") << " fell back to untrimmed";
  if (stats.meshes) ss << ", " << stats.meshes << " mesh" << (stats.meshes == 1 ? "" : "es");
  if (stats.layers) ss << ", " << stats.layers << " new layer" << (stats.layers == 1 ? "" : "s");
  if (stats.skipped) ss << "; " << stats.skipped << " unsupported entit" << (stats.skipped == 1 ? "y" : "ies") << " skipped";
  summary = ss.str();
  if (stats.curves + stats.points + stats.surfaces + stats.breps == 0) {
    if (entities.empty()) summary = "No entities found in " + path;
    return false;
  }
  return true;
}

// ===========================================================================
// STEP writer (ISO-10303-21, AP214)
// ===========================================================================

namespace {

class StepWriter {
 public:
  // Adds a simple entity `#id = KEYWORD(args);` and returns its id.
  int Add(const std::string& keyword, const std::string& args) {
    const int id = next_++;
    lines_.push_back("#" + std::to_string(id) + "=" + keyword + "(" + args + ");");
    return id;
  }
  // Adds a complex entity: several keyword(args) groups wrapped together,
  // e.g. RATIONAL_B_SPLINE_SURFACE over B_SPLINE_SURFACE_WITH_KNOTS.
  int AddComplex(const std::vector<std::pair<std::string, std::string>>& parts) {
    const int id = next_++;
    std::string s = "#" + std::to_string(id) + "=(";
    for (const auto& [kw, args] : parts) s += kw + "(" + args + ")";
    s += ");";
    lines_.push_back(s);
    return id;
  }
  int Ref(int id) const { return id; }

  void Write(std::ostream& os, const std::string& product, const std::string& file_name, const std::string& units_name) {
    std::time_t now = std::time(nullptr);
    char date[32];
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
    os << "ISO-10303-21;\n";
    os << "HEADER;\n";
    os << "FILE_DESCRIPTION((''),'2;1');\n";
    os << "FILE_NAME('" << StepEscape(file_name) << "','" << date << "',('Dino 8 user'),(''),'Dino 8 " << DINO8_VERSION << "','Dino 8','');\n";
    os << "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 3 1 1 }'));\n";
    os << "ENDSEC;\n";
    os << "DATA;\n";
    (void)units_name;
    (void)product;
    for (const std::string& l : lines_) os << l << "\n";
    os << "ENDSEC;\n";
    os << "END-ISO-10303-21;\n";
  }

  static std::string StepEscape(const std::string& s) {
    std::string out;
    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
    return out;
  }

  int entity_count() const { return next_ - 1; }

 private:
  int next_ = 1;
  std::vector<std::string> lines_;
};

std::string StepStr(const std::string& s) { return "'" + StepWriter::StepEscape(s) + "'"; }
std::string StepPt(const ON_3dPoint& p) { return "(" + Num(p.x) + "," + Num(p.y) + "," + Num(p.z) + ")"; }
std::string StepDir(const ON_3dVector& v) {
  ON_3dVector u = v;
  u.Unitize();
  return "(" + Num(u.x) + "," + Num(u.y) + "," + Num(u.z) + ")";
}

int WriteCartesianPoint(StepWriter& w, const ON_3dPoint& p) { return w.Add("CARTESIAN_POINT", "''," + StepPt(p)); }
int WriteDirection(StepWriter& w, const ON_3dVector& v) { return w.Add("DIRECTION", "''," + StepDir(v)); }
int WriteAxis2Placement3D(StepWriter& w, const ON_Plane& pl) {
  const int p = WriteCartesianPoint(w, pl.origin);
  const int z = WriteDirection(w, pl.zaxis);
  const int x = WriteDirection(w, pl.xaxis);
  return w.Add("AXIS2_PLACEMENT_3D", "'',#" + std::to_string(p) + ",#" + std::to_string(z) + ",#" + std::to_string(x));
}
int WriteVertexPoint(StepWriter& w, const ON_3dPoint& p) {
  const int cp = WriteCartesianPoint(w, p);
  return w.Add("VERTEX_POINT", "'',#" + std::to_string(cp));
}

// Writes a curve as LINE / CIRCLE / B_SPLINE_CURVE_WITH_KNOTS (+ rational).
int WriteStepCurve(StepWriter& w, const ON_NurbsCurve& c) {
  if (c.IsLinear(1e-9)) {
    const ON_3dPoint p0 = c.PointAtStart();
    ON_3dVector dir = c.PointAtEnd() - p0;
    const double len = dir.Length();
    dir.Unitize();
    const int pt = WriteCartesianPoint(w, p0);
    const int dir_id = WriteDirection(w, dir);
    const int vec = w.Add("VECTOR", "'',#" + std::to_string(dir_id) + "," + Num(std::max(len, 1e-9)));
    return w.Add("LINE", "'',#" + std::to_string(pt) + ",#" + std::to_string(vec));
  }
  ON_Arc arc;
  if (c.IsArc(nullptr, &arc, 1e-9) && arc.IsCircle()) {
    ON_Plane pl = arc.plane;
    pl.origin = arc.Center();
    const int ax = WriteAxis2Placement3D(w, pl);
    return w.Add("CIRCLE", "'',#" + std::to_string(ax) + "," + Num(arc.radius));
  }
  const int K = c.CVCount() - 1, M = c.Degree();
  std::vector<double> full = FullKnots(c);
  // Compress to (knot, multiplicity) pairs, as STEP's B_SPLINE_CURVE_WITH_KNOTS requires.
  std::vector<double> uknots;
  std::vector<int> mult;
  for (double k : full) {
    if (!uknots.empty() && std::fabs(k - uknots.back()) < 1e-9) ++mult.back();
    else { uknots.push_back(k); mult.push_back(1); }
  }
  std::string cvs = "(", wts = "(";
  bool rational = false;
  for (int i = 0; i <= K; ++i) {
    double wgt;
    const ON_3dPoint p = EuclideanCV(c, i, wgt);
    cvs += "#" + std::to_string(WriteCartesianPoint(w, p)) + (i < K ? "," : "");
    wts += Num(wgt) + (i < K ? "," : "");
    if (std::fabs(wgt - 1.0) > 1e-9) rational = true;
  }
  cvs += ")"; wts += ")";
  std::string kn = "(", mu = "(";
  for (size_t i = 0; i < uknots.size(); ++i) { kn += Num(uknots[i]) + (i + 1 < uknots.size() ? "," : ""); mu += std::to_string(mult[i]) + (i + 1 < uknots.size() ? "," : ""); }
  kn += ")"; mu += ")";
  const std::string args = "''," + std::to_string(M) + "," + cvs + ",.UNSPECIFIED.,.F.,.F.,.F.," + mu + "," + kn + ",.UNSPECIFIED.";
  if (!rational) return w.Add("B_SPLINE_CURVE_WITH_KNOTS", args);
  return w.AddComplex({{"BOUNDED_CURVE", ""}, {"B_SPLINE_CURVE", ""}, {"B_SPLINE_CURVE_WITH_KNOTS", args.substr(2)}, {"CURVE", ""},
                       {"GEOMETRIC_REPRESENTATION_ITEM", ""}, {"RATIONAL_B_SPLINE_CURVE", wts}, {"REPRESENTATION_ITEM", "''"}});
}

// Writes a surface as PLANE / CYLINDRICAL_SURFACE / B_SPLINE_SURFACE_WITH_KNOTS (+ rational).
int WriteStepSurface(StepWriter& w, const ON_NurbsSurface& srf) {
  ON_Plane pl;
  ON_Cylinder cyl;
  if (srf.IsPlanar(&pl, 1e-7)) {
    const int ax = WriteAxis2Placement3D(w, pl);
    return w.Add("PLANE", "'',#" + std::to_string(ax));
  }
  if (srf.IsCylinder(&cyl, 1e-6)) {
    ON_Plane cp(cyl.circle.plane);
    cp.origin = cyl.circle.Center();
    const int ax = WriteAxis2Placement3D(w, cp);
    return w.Add("CYLINDRICAL_SURFACE", "'',#" + std::to_string(ax) + "," + Num(cyl.circle.radius));
  }
  const int nu = srf.CVCount(0), nv = srf.CVCount(1);
  std::vector<double> ku = FullKnots(srf, 0), kv = FullKnots(srf, 1);
  auto compress = [](const std::vector<double>& full, std::vector<double>& uknots, std::vector<int>& mult) {
    for (double k : full) { if (!uknots.empty() && std::fabs(k - uknots.back()) < 1e-9) ++mult.back(); else { uknots.push_back(k); mult.push_back(1); } }
  };
  std::vector<double> uk_u, uk_v;
  std::vector<int> mu_u, mu_v;
  compress(ku, uk_u, mu_u);
  compress(kv, uk_v, mu_v);
  std::string cvs = "(";
  std::string wts = "(";
  bool rational = false;
  for (int i = 0; i < nu; ++i) {
    cvs += "(";
    wts += "(";
    for (int j = 0; j < nv; ++j) {
      double wgt;
      const ON_3dPoint p = EuclideanCV(srf, i, j, wgt);
      cvs += "#" + std::to_string(WriteCartesianPoint(w, p)) + (j + 1 < nv ? "," : "");
      wts += Num(wgt) + (j + 1 < nv ? "," : "");
      if (std::fabs(wgt - 1.0) > 1e-9) rational = true;
    }
    cvs += ")" + std::string(i + 1 < nu ? "," : "");
    wts += ")" + std::string(i + 1 < nu ? "," : "");
  }
  cvs += ")"; wts += ")";
  std::string ku_s = "(", mu_u_s = "(", kv_s = "(", mu_v_s = "(";
  for (size_t i = 0; i < uk_u.size(); ++i) { ku_s += Num(uk_u[i]) + (i + 1 < uk_u.size() ? "," : ""); mu_u_s += std::to_string(mu_u[i]) + (i + 1 < uk_u.size() ? "," : ""); }
  for (size_t i = 0; i < uk_v.size(); ++i) { kv_s += Num(uk_v[i]) + (i + 1 < uk_v.size() ? "," : ""); mu_v_s += std::to_string(mu_v[i]) + (i + 1 < uk_v.size() ? "," : ""); }
  ku_s += ")"; mu_u_s += ")"; kv_s += ")"; mu_v_s += ")";
  const std::string args = "''," + std::to_string(srf.Degree(0)) + "," + std::to_string(srf.Degree(1)) + "," + cvs +
                           ",.UNSPECIFIED.,.F.,.F.,.F.," + mu_u_s + "," + mu_v_s + "," + ku_s + "," + kv_s + ",.UNSPECIFIED.";
  if (!rational) return w.Add("B_SPLINE_SURFACE_WITH_KNOTS", args);
  return w.AddComplex({{"BOUNDED_SURFACE", ""}, {"B_SPLINE_SURFACE", ""}, {"B_SPLINE_SURFACE_WITH_KNOTS", args.substr(2)},
                       {"GEOMETRIC_REPRESENTATION_ITEM", ""}, {"RATIONAL_B_SPLINE_SURFACE", wts}, {"REPRESENTATION_ITEM", "''"}, {"SURFACE", ""}});
}

// One edge, shared between the two faces on either side of it (STEP models
// an oriented edge as a reference to one shared EDGE_CURVE).
struct StepEdgeKey {
  int v0, v1;  // vertex STEP ids, order-independent lookup below
};

int WriteStepFace(StepWriter& w, const ON_Brep& b, const ON_BrepFace& face, double tol) {
  ExportFace f;
  if (!PrepareExportFace(b, face, tol, f)) return -1;
  const int srf_id = WriteStepSurface(w, f.srf);
  std::vector<int> bound_ids;
  for (size_t li = 0; li < f.loops.size(); ++li) {
    const std::vector<LoopSeg>& segs = f.loops[li];
    std::vector<int> oriented;
    for (const LoopSeg& seg : segs) {
      const ON_3dPoint p0 = seg.c3.PointAtStart(), p1 = seg.c3.PointAtEnd();
      const int v0 = WriteVertexPoint(w, p0), v1 = WriteVertexPoint(w, p1);
      const int c3 = WriteStepCurve(w, seg.c3);
      const int ec = w.Add("EDGE_CURVE", "'',#" + std::to_string(v0) + ",#" + std::to_string(v1) + ",#" + std::to_string(c3) + ",.T.");
      const int oe = w.Add("ORIENTED_EDGE", "'',*,*,#" + std::to_string(ec) + ",.T.");
      oriented.push_back(oe);
    }
    std::string refs = "(";
    for (size_t i = 0; i < oriented.size(); ++i) refs += "#" + std::to_string(oriented[i]) + (i + 1 < oriented.size() ? "," : "");
    refs += ")";
    const int loop_id = w.Add("EDGE_LOOP", "''," + refs);
    const int bound_kw = li == 0 ? 0 : 1;
    const int fb = w.Add(bound_kw == 0 ? "FACE_OUTER_BOUND" : "FACE_BOUND", "'',#" + std::to_string(loop_id) + ",.T.");
    bound_ids.push_back(fb);
  }
  std::string refs = "(";
  for (size_t i = 0; i < bound_ids.size(); ++i) refs += "#" + std::to_string(bound_ids[i]) + (i + 1 < bound_ids.size() ? "," : "");
  refs += ")";
  return w.Add("ADVANCED_FACE", "''," + refs + ",#" + std::to_string(srf_id) + ",." + (face.m_bRev ? std::string("F") : std::string("T")) + ".");
}

}  // namespace

bool ExportStep(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  std::vector<const SceneObject*> objs = ExportObjects(doc, selected_only);
  if (objs.empty()) { error = "Nothing to export"; return false; }
  std::ofstream os(path, std::ios::binary);
  if (!os) { error = "Could not write " + path; return false; }
  StepWriter w;
  const double tol = doc.Settings().absolute_tolerance > 0 ? doc.Settings().absolute_tolerance : 0.001;
  const double mm = MillimetresPerUnit(doc.Settings().unit_system);

  // A minimal, valid AP214 product structure: one SI_UNIT (millimetre)
  // representation context that every shape references indirectly via the
  // header, plus a PRODUCT / PRODUCT_DEFINITION pair so FreeCAD, Onshape
  // and other AP214 readers recognise the file as a single part.
  const int si_mm = w.Add("SI_UNIT", "$,.MILLI.,.METRE.");
  const int uncertainty = w.Add("UNCERTAINTY_MEASURE_WITH_UNIT", "LENGTH_MEASURE(" + Num(tol) + "),#" + std::to_string(si_mm) + "," + StepStr("distance") + "," + StepStr("model tolerance"));
  const int geom_ctx = w.AddComplex({{"GEOMETRIC_REPRESENTATION_CONTEXT", "3"},
                                      {"GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT", "(#" + std::to_string(uncertainty) + ")"},
                                      {"GLOBAL_UNIT_ASSIGNED_CONTEXT", "(#" + std::to_string(si_mm) + ")"},
                                      {"REPRESENTATION_CONTEXT", "'','3D'"}});
  (void)geom_ctx;
  const std::string product_name = doc.Settings().title.empty() ? "Dino 8 export" : doc.Settings().title;
  const int product = w.Add("PRODUCT", StepStr(product_name) + "," + StepStr(product_name) + ",''" + ",()");
  const int pfc = w.Add("PRODUCT_FORMATION_CONTEXT", "'','',''");
  (void)pfc;
  const int pdf = w.Add("PRODUCT_DEFINITION_FORMATION", "'','',#" + std::to_string(product));
  const int pdc = w.Add("PRODUCT_DEFINITION_CONTEXT", StepStr("part definition") + ",#0,''");
  (void)pdc;
  const int pd = w.Add("PRODUCT_DEFINITION", "'','',#" + std::to_string(pdf) + ",#0");
  (void)pd;

  int written = 0;
  std::vector<std::string> notes;
  int shape_count = 0;
  for (const SceneObject* o : objs) {
    switch (o->kind) {
      case ObjectKind::Point: {
        const int pt = WriteCartesianPoint(w, o->point);
        w.Add("GEOMETRIC_CURVE_SET", StepStr(o->name.empty() ? "Point" : o->name) + ",(#" + std::to_string(pt) + ")");
        ++written; ++shape_count;
        break;
      }
      case ObjectKind::Curve: {
        if (!o->curve) break;
        const int c = WriteStepCurve(w, o->curve->raw());
        w.Add("GEOMETRIC_CURVE_SET", StepStr(o->name.empty() ? "Curve" : o->name) + ",(#" + std::to_string(c) + ")");
        ++written; ++shape_count;
        break;
      }
      case ObjectKind::Surface: {
        if (!o->surface) break;
        ON_Brep tmp;
        if (!SurfaceAsBrep(*o->surface, tmp)) break;
        std::vector<int> faces;
        for (int fi = 0; fi < tmp.m_F.Count(); ++fi) { const int f = WriteStepFace(w, tmp, tmp.m_F[fi], tol); if (f >= 0) faces.push_back(f); }
        if (faces.empty()) break;
        std::string refs = "(";
        for (size_t i = 0; i < faces.size(); ++i) refs += "#" + std::to_string(faces[i]) + (i + 1 < faces.size() ? "," : "");
        refs += ")";
        const int shell = w.Add("OPEN_SHELL", "''," + refs);
        w.Add("SHELL_BASED_SURFACE_MODEL", StepStr(o->name.empty() ? "Surface" : o->name) + ",(#" + std::to_string(shell) + ")");
        ++written; ++shape_count;
        break;
      }
      case ObjectKind::Brep: {
        if (!o->brep) break;
        const ON_Brep& b = o->brep->raw();
        std::vector<int> faces;
        for (int fi = 0; fi < b.m_F.Count(); ++fi) {
          if (b.m_F[fi].m_face_index < 0) continue;
          const int f = WriteStepFace(w, b, b.m_F[fi], tol);
          if (f >= 0) faces.push_back(f);
        }
        if (faces.empty()) break;
        std::string refs = "(";
        for (size_t i = 0; i < faces.size(); ++i) refs += "#" + std::to_string(faces[i]) + (i + 1 < faces.size() ? "," : "");
        refs += ")";
        const bool closed = b.IsSolid();
        const int shell = w.Add(closed ? "CLOSED_SHELL" : "OPEN_SHELL", "''," + refs);
        if (closed) {
          const int solid = w.Add("MANIFOLD_SOLID_BREP", StepStr(o->name.empty() ? "Solid" : o->name) + ",#" + std::to_string(shell));
          (void)solid;
        } else {
          w.Add("SHELL_BASED_SURFACE_MODEL", StepStr(o->name.empty() ? "Surface" : o->name) + ",(#" + std::to_string(shell) + ")");
        }
        ++written; ++shape_count;
        break;
      }
      case ObjectKind::Mesh:
      case ObjectKind::SubD: {
        std::optional<kernel::Mesh> km;
        if (o->kind == ObjectKind::Mesh && o->mesh) km = *o->mesh;
        else if (o->subd) km = o->subd->ToApproximateMesh();
        if (!km) break;
        const ON_Mesh& m = km->raw();
        std::vector<int> verts(static_cast<size_t>(m.VertexCount()));
        for (int i = 0; i < m.VertexCount(); ++i) verts[static_cast<size_t>(i)] = WriteCartesianPoint(w, m.m_V[i]);
        std::vector<int> faces;
        for (int fi = 0; fi < m.FaceCount(); ++fi) {
          const ON_MeshFace& mf = m.m_F[fi];
          const int n = mf.IsTriangle() ? 3 : 4;
          std::string refs = "(";
          for (int k = 0; k < n; ++k) refs += "#" + std::to_string(verts[static_cast<size_t>(mf.vi[k])]) + (k + 1 < n ? "," : "");
          refs += ")";
          const int loop = w.Add("POLY_LOOP", "''," + refs);
          const int bound = w.Add("FACE_OUTER_BOUND", "'',#" + std::to_string(loop) + ",.T.");
          faces.push_back(w.Add("FACE", "'',(#" + std::to_string(bound) + ")"));  // FACETED_BREP faces carry no separate surface entity
        }
        std::string refs = "(";
        for (size_t i = 0; i < faces.size(); ++i) refs += "#" + std::to_string(faces[i]) + (i + 1 < faces.size() ? "," : "");
        refs += ")";
        const int shell = w.Add("CLOSED_SHELL", "''," + refs);
        w.Add("FACETED_BREP", StepStr(o->name.empty() ? "Mesh" : o->name) + ",#" + std::to_string(shell));
        ++written; ++shape_count;
        break;
      }
    }
  }
  w.Write(os, doc.Settings().title.empty() ? std::filesystem::path(path).stem().string() : doc.Settings().title,
          std::filesystem::path(path).filename().string(), "MILLIMETRE");
  if (!os) { error = "Could not write " + path; return false; }
  if (written == 0) { error = "Nothing exportable in the selection"; return false; }
  error.clear();
  return true;
}

// ===========================================================================
// STEP reader (ISO-10303-21)
// ===========================================================================

namespace {

struct StepPart {
  std::string keyword;
  std::vector<std::string> args;  // top-level comma-separated, raw text
};

struct StepEntity {
  bool complex = false;
  std::vector<StepPart> parts;  // one part for a simple entity, several for a complex one
  const StepPart* Find(const std::string& kw) const {
    for (const StepPart& p : parts) if (p.keyword == kw) return &p;
    return nullptr;
  }
};

// Splits `text` on `sep` at paren/quote depth 0.
std::vector<std::string> SplitTop(const std::string& text, char sep) {
  std::vector<std::string> out;
  int depth = 0;
  bool q = false;
  std::string cur;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (q) {
      cur += c;
      if (c == '\'') { if (i + 1 < text.size() && text[i + 1] == '\'') { cur += text[++i]; } else q = false; }
      continue;
    }
    if (c == '\'') { q = true; cur += c; continue; }
    if (c == '(') { ++depth; cur += c; continue; }
    if (c == ')') { --depth; cur += c; continue; }
    if (c == sep && depth == 0) { out.push_back(cur); cur.clear(); continue; }
    cur += c;
  }
  out.push_back(cur);
  return out;
}

std::string Unparen(std::string s) {
  s = Trim(s);
  if (s.size() >= 2 && s.front() == '(' && s.back() == ')') return s.substr(1, s.size() - 2);
  return s;
}

// Parses one record's right-hand side (after "="): either "KW(args)" or a
// complex "(KW1(args1)KW2(args2)...)".
StepEntity ParseEntity(const std::string& rhs) {
  StepEntity e;
  std::string s = Trim(rhs);
  if (!s.empty() && s.front() == '(') {
    // Complex: repeated KEYWORD(...) groups inside the outer parens.
    e.complex = true;
    std::string inner = s.substr(1, s.size() >= 2 ? s.size() - 2 : 0);
    size_t i = 0;
    while (i < inner.size()) {
      while (i < inner.size() && std::isspace(static_cast<unsigned char>(inner[i]))) ++i;
      size_t k = i;
      while (k < inner.size() && inner[k] != '(') ++k;
      if (k >= inner.size()) break;
      const std::string kw = Trim(inner.substr(i, k - i));
      int depth = 0;
      size_t j = k;
      bool q = false;
      for (; j < inner.size(); ++j) {
        const char c = inner[j];
        if (q) { if (c == '\'') { if (j + 1 < inner.size() && inner[j + 1] == '\'') ++j; else q = false; } continue; }
        if (c == '\'') { q = true; continue; }
        if (c == '(') ++depth;
        else if (c == ')') { --depth; if (depth == 0) { ++j; break; } }
      }
      const std::string args_text = inner.substr(k + 1, (j > k + 1) ? (j - k - 2) : 0);
      StepPart part;
      part.keyword = kw;
      part.args = args_text.empty() ? std::vector<std::string>() : SplitTop(args_text, ',');
      e.parts.push_back(std::move(part));
      i = j;
    }
    return e;
  }
  const size_t paren = s.find('(');
  if (paren == std::string::npos) { StepPart p; p.keyword = s; e.parts.push_back(p); return e; }
  StepPart p;
  p.keyword = Trim(s.substr(0, paren));
  const size_t last = s.rfind(')');
  const std::string args_text = last > paren ? s.substr(paren + 1, last - paren - 1) : "";
  p.args = args_text.empty() ? std::vector<std::string>() : SplitTop(args_text, ',');
  e.parts.push_back(p);
  return e;
}

int StepRef(const std::string& a) {
  const std::string s = Trim(a);
  if (s.size() < 2 || s[0] != '#') return 0;
  return std::atoi(s.c_str() + 1);
}
bool IsRef(const std::string& a) { return !Trim(a).empty() && Trim(a)[0] == '#'; }
double StepReal(const std::string& a) {
  std::string s = Trim(a);
  for (char& c : s) if (c == 'D' || c == 'd') c = 'E';
  return std::atof(s.c_str());
}
std::string StepString(const std::string& a) {
  std::string s = Trim(a);
  if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
    s = s.substr(1, s.size() - 2);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '\'' && i + 1 < s.size() && s[i + 1] == '\'') { out += '\''; ++i; } else out += s[i]; }
    return out;
  }
  return "";
}
std::vector<int> StepRefList(const std::string& a) {
  std::vector<int> out;
  for (const std::string& t : SplitTop(Unparen(a), ',')) if (IsRef(t)) out.push_back(StepRef(t));
  return out;
}
std::vector<double> StepRealList(const std::string& a) {
  std::vector<double> out;
  for (const std::string& t : SplitTop(Unparen(a), ',')) out.push_back(StepReal(t));
  return out;
}
std::vector<int> StepIntList(const std::string& a) {
  std::vector<int> out;
  for (const std::string& t : SplitTop(Unparen(a), ',')) out.push_back(std::atoi(Trim(t).c_str()));
  return out;
}

class StepModel {
 public:
  std::map<int, StepEntity> entities;

  const StepEntity* Get(int id) const { auto it = entities.find(id); return it == entities.end() ? nullptr : &it->second; }

  ON_3dPoint Point(int id) {
    auto it = pt_cache_.find(id);
    if (it != pt_cache_.end()) return it->second;
    ON_3dPoint p = ON_3dPoint::Origin;
    if (const StepEntity* e = Get(id)) if (const StepPart* pp = e->Find("CARTESIAN_POINT")) {
      const std::vector<double> c = StepRealList(pp->args.size() > 1 ? pp->args[1] : "");
      if (c.size() >= 3) p = ON_3dPoint(c[0], c[1], c[2]);
      else if (c.size() == 2) p = ON_3dPoint(c[0], c[1], 0);
    }
    pt_cache_[id] = p;
    return p;
  }

  ON_3dVector Direction(int id) {
    ON_3dVector v(0, 0, 1);
    if (const StepEntity* e = Get(id)) if (const StepPart* pp = e->Find("DIRECTION")) {
      const std::vector<double> c = StepRealList(pp->args.size() > 1 ? pp->args[1] : "");
      if (c.size() >= 3) v = ON_3dVector(c[0], c[1], c[2]);
      else if (c.size() == 2) v = ON_3dVector(c[0], c[1], 0);
    }
    v.Unitize();
    return v;
  }

  ON_Plane Placement(int id) {
    ON_Plane pl(ON_origin, ON_xaxis, ON_yaxis);
    const StepEntity* e = Get(id);
    if (!e) return pl;
    if (const StepPart* pp = e->Find("AXIS2_PLACEMENT_3D")) {
      if (pp->args.size() > 1 && IsRef(pp->args[1])) pl.origin = Point(StepRef(pp->args[1]));
      ON_3dVector z(0, 0, 1), x(1, 0, 0);
      if (pp->args.size() > 2 && IsRef(pp->args[2])) z = Direction(StepRef(pp->args[2]));
      if (pp->args.size() > 3 && IsRef(pp->args[3])) x = Direction(StepRef(pp->args[3]));
      x = x - z * ON_DotProduct(x, z);
      if (!x.Unitize()) x = ON_Plane(ON_origin, z).xaxis;
      pl = ON_Plane(pl.origin, x, ON_CrossProduct(z, x));
    }
    return pl;
  }

  // A curve entity as a full ON_NurbsCurve (own natural domain).
  bool Curve(int id, ON_NurbsCurve& out) {
    auto it = curve_cache_.find(id);
    if (it != curve_cache_.end()) { out = it->second; return out.IsValid(); }
    bool ok = BuildCurve(id, out);
    curve_cache_[id] = ok ? out : ON_NurbsCurve();
    return ok;
  }

  bool Surface(int id, ON_NurbsSurface& out) {
    auto it = surf_cache_.find(id);
    if (it != surf_cache_.end()) { out = it->second; return out.IsValid(); }
    bool ok = BuildSurface(id, out);
    surf_cache_[id] = ok ? out : ON_NurbsSurface();
    return ok;
  }

  ON_3dPoint Vertex(int id) {
    if (const StepEntity* e = Get(id)) if (const StepPart* pp = e->Find("VERTEX_POINT")) if (pp->args.size() > 1 && IsRef(pp->args[1])) return Point(StepRef(pp->args[1]));
    return ON_3dPoint::Origin;
  }

  // Builds the 3D curve of one EDGE_CURVE between its own vertices (used as
  // a loop segment's model-space curve).
  bool EdgeCurve3D(int edge_de, bool same_sense, ON_NurbsCurve& out) {
    const StepEntity* e = Get(edge_de);
    const StepPart* pp = e ? e->Find("EDGE_CURVE") : nullptr;
    if (!pp || pp->args.size() < 4) return false;
    const ON_3dPoint v0 = IsRef(pp->args[1]) ? Vertex(StepRef(pp->args[1])) : ON_3dPoint::Origin;
    const ON_3dPoint v1 = IsRef(pp->args[2]) ? Vertex(StepRef(pp->args[2])) : ON_3dPoint::Origin;
    const bool edge_same_sense = pp->args.size() > 4 ? Trim(pp->args[4]) == ".T." : true;
    const int cde = IsRef(pp->args[3]) ? StepRef(pp->args[3]) : 0;
    const StepEntity* ce = cde ? Get(cde) : nullptr;
    bool built = false;
    if (ce) {
      if (const StepPart* lp = ce->Find("LINE")) {
        (void)lp;
        ON_LineCurve lc(v0, v1);
        built = lc.GetNurbForm(out) > 0;
      } else if (const StepPart* cp = ce->Find("CIRCLE")) {
        ON_Plane pl = Placement(IsRef(cp->args[1]) ? StepRef(cp->args[1]) : 0);
        const double r = StepReal(cp->args.size() > 2 ? cp->args[2] : "0");
        ON_Circle circ(pl, r);
        const bool full = v0.DistanceTo(v1) < 1e-7;
        if (full) {
          ON_ArcCurve ac(circ);
          built = ac.GetNurbForm(out) > 0;
        } else {
          ON_3dVector r0 = v0 - pl.origin, r1 = v1 - pl.origin;
          const double a0 = std::atan2(ON_DotProduct(r0, pl.yaxis), ON_DotProduct(r0, pl.xaxis));
          double a1 = std::atan2(ON_DotProduct(r1, pl.yaxis), ON_DotProduct(r1, pl.xaxis));
          while (a1 <= a0 + 1e-12) a1 += 2 * ON_PI;
          ON_ArcCurve ac(ON_Arc(circ, ON_Interval(a0, a1)));
          built = ac.GetNurbForm(out) > 0;
        }
      } else {
        built = Curve(cde, out);
      }
    }
    if (!built) return false;
    if (!edge_same_sense) out.Reverse();
    if (!same_sense) out.Reverse();
    return out.IsValid();
  }

  Color ColourOfItem(int item_id) {
    auto it = colour_cache_.find(item_id);
    if (it != colour_cache_.end()) return it->second;
    Color c = Color::FromBytes(0, 0, 0);
    auto sit = item_colour_.find(item_id);
    if (sit != item_colour_.end()) c = sit->second;
    colour_cache_[item_id] = c;
    return c;
  }
  bool HasColour(int item_id) { return item_colour_.count(item_id) != 0; }

  std::map<int, Color> item_colour_;  // STYLED_ITEM's target -> resolved colour

 private:
  bool BuildCurve(int id, ON_NurbsCurve& out) {
    const StepEntity* e = Get(id);
    if (!e) return false;
    if (const StepPart* p = e->Find("LINE")) {
      if (p->args.size() < 3 || !IsRef(p->args[1]) || !IsRef(p->args[2])) return false;
      const ON_3dPoint p0 = Point(StepRef(p->args[1]));
      const StepEntity* ve = Get(StepRef(p->args[2]));
      const StepPart* vp = ve ? ve->Find("VECTOR") : nullptr;
      ON_3dVector dir(1, 0, 0);
      double mag = 1.0;
      if (vp && vp->args.size() > 2) { if (IsRef(vp->args[1])) dir = Direction(StepRef(vp->args[1])); mag = StepReal(vp->args[2]); }
      ON_LineCurve lc(p0, p0 + dir * mag);
      return lc.GetNurbForm(out) > 0;
    }
    if (const StepPart* p = e->Find("CIRCLE")) {
      if (p->args.size() < 3) return false;
      ON_Plane pl = Placement(IsRef(p->args[1]) ? StepRef(p->args[1]) : 0);
      ON_Circle circ(pl, StepReal(p->args[2]));
      ON_ArcCurve ac(circ);
      return ac.GetNurbForm(out) > 0;
    }
    if (const StepPart* p = e->Find("ELLIPSE")) {
      if (p->args.size() < 4) return false;
      ON_Plane pl = Placement(IsRef(p->args[1]) ? StepRef(p->args[1]) : 0);
      ON_Ellipse el(pl, StepReal(p->args[2]), StepReal(p->args[3]));
      ON_NurbsCurve nc;
      if (!el.GetNurbForm(nc)) return false;
      out = nc;
      return out.IsValid();
    }
    if (const StepPart* p = e->Find("B_SPLINE_CURVE_WITH_KNOTS")) {
      if (p->args.size() < 8) return false;
      const int degree = std::atoi(Trim(p->args[1]).c_str());
      const std::vector<int> cv_refs = StepRefList(p->args[2]);
      const std::vector<int> mult = StepIntList(p->args[6]);
      const std::vector<double> uknots = StepRealList(p->args[7]);
      std::vector<double> full;
      for (size_t i = 0; i < uknots.size() && i < mult.size(); ++i) for (int k = 0; k < mult[i]; ++k) full.push_back(uknots[i]);
      std::vector<ON_3dPoint> cvs;
      for (int r : cv_refs) cvs.push_back(Point(r));
      std::vector<double> weights;
      if (const StepPart* rp = e->Find("RATIONAL_B_SPLINE_CURVE")) { if (!rp->args.empty()) weights = StepRealList(rp->args[0]); }
      return MakeNurbsCurve(3, degree, cvs, weights, full, out);
    }
    if (const StepPart* p = e->Find("TRIMMED_CURVE")) {
      if (p->args.size() < 4 || !IsRef(p->args[1])) return false;
      ON_NurbsCurve base;
      if (!Curve(StepRef(p->args[1]), base)) return false;
      auto param_of = [&](const std::string& sel) -> std::optional<double> {
        for (const std::string& t : SplitTop(Unparen(sel), ',')) {
          const std::string s = Trim(t);
          if (!s.empty() && s.front() != '#') return StepReal(s);
        }
        return std::nullopt;
      };
      std::optional<double> t1 = param_of(p->args[2]), t2 = param_of(p->args[3]);
      if (t1 && t2) {
        double a = *t1, b = *t2;
        if (b < a) b += base.Domain().Length() > 0 && base.IsClosed() ? 2 * ON_PI : 0;
        ON_Interval want(std::min(a, b), std::max(a, b));
        want.Intersection(base.Domain());
        ON_NurbsCurve trimmed = base;
        if (trimmed.Trim(want)) { out = trimmed; return out.IsValid(); }
      }
      out = base;
      return out.IsValid();
    }
    if (const StepPart* p = e->Find("COMPOSITE_CURVE")) {
      if (p->args.size() < 2) return false;
      const std::vector<int> segs = StepRefList(p->args[1]);
      ON_Polyline pl;
      bool first = true;
      for (int sde : segs) {
        const StepEntity* se = Get(sde);
        const StepPart* sp = se ? se->Find("COMPOSITE_CURVE_SEGMENT") : nullptr;
        if (!sp || sp->args.size() < 3 || !IsRef(sp->args[2])) continue;
        ON_NurbsCurve sc;
        if (!Curve(StepRef(sp->args[2]), sc)) continue;
        const int n = std::max(2, sc.SpanCount() * std::max(2, sc.Degree()) * 6);
        for (int i = (first ? 0 : 1); i <= n; ++i) pl.Append(sc.PointAt(sc.Domain().ParameterAt(static_cast<double>(i) / n)));
        first = false;
      }
      if (pl.Count() < 2) return false;
      ON_PolylineCurve pc(pl);
      return pc.GetNurbForm(out) > 0;
    }
    if (const StepPart* p = e->Find("POLYLINE")) {
      if (p->args.size() < 2) return false;
      const std::vector<int> refs = StepRefList(p->args[1]);
      if (refs.size() < 2) return false;
      ON_Polyline pl;
      for (int r : refs) pl.Append(Point(r));
      ON_PolylineCurve pc(pl);
      return pc.GetNurbForm(out) > 0;
    }
    return false;
  }

  bool BuildSurface(int id, ON_NurbsSurface& out) {
    const StepEntity* e = Get(id);
    if (!e) return false;
    if (const StepPart* p = e->Find("PLANE")) {
      ON_Plane pl = Placement(p->args.size() > 1 && IsRef(p->args[1]) ? StepRef(p->args[1]) : 0);
      ON_PlaneSurface ps(pl);
      ps.SetExtents(0, ON_Interval(-1e5, 1e5));
      ps.SetExtents(1, ON_Interval(-1e5, 1e5));
      return ps.GetNurbForm(out) > 0;
    }
    if (const StepPart* p = e->Find("CYLINDRICAL_SURFACE")) {
      if (p->args.size() < 3) return false;
      ON_Plane pl = Placement(IsRef(p->args[1]) ? StepRef(p->args[1]) : 0);
      ON_Cylinder cyl(ON_Circle(pl, StepReal(p->args[2])));
      cyl.height[0] = -1e5; cyl.height[1] = 1e5;
      return cyl.GetNurbForm(out) > 0;
    }
    if (const StepPart* p = e->Find("CONICAL_SURFACE")) {
      if (p->args.size() < 4) return false;
      ON_Plane pl = Placement(IsRef(p->args[1]) ? StepRef(p->args[1]) : 0);
      const double r = StepReal(p->args[2]), half_angle = StepReal(p->args[3]);
      ON_Cone cone;
      pl.origin = pl.origin - pl.zaxis * (r / std::max(1e-9, std::tan(half_angle)));
      cone.Create(pl, 2e5, r + 2e5 * std::tan(half_angle));
      return cone.GetNurbForm(out) > 0;
    }
    if (const StepPart* p = e->Find("SPHERICAL_SURFACE")) {
      if (p->args.size() < 3) return false;
      ON_Plane pl = Placement(IsRef(p->args[1]) ? StepRef(p->args[1]) : 0);
      ON_Sphere sph(pl.origin, StepReal(p->args[2]));
      return sph.GetNurbForm(out) > 0;
    }
    if (const StepPart* p = e->Find("TOROIDAL_SURFACE")) {
      if (p->args.size() < 4) return false;
      ON_Plane pl = Placement(IsRef(p->args[1]) ? StepRef(p->args[1]) : 0);
      ON_Torus tor(pl, StepReal(p->args[2]), StepReal(p->args[3]));
      return tor.GetNurbForm(out) > 0;
    }
    if (const StepPart* p = e->Find("SURFACE_OF_REVOLUTION")) {
      if (p->args.size() < 3 || !IsRef(p->args[1]) || !IsRef(p->args[2])) return false;
      ON_NurbsCurve gen;
      if (!Curve(StepRef(p->args[1]), gen)) return false;
      const StepEntity* ae = Get(StepRef(p->args[2]));
      const StepPart* ap = ae ? ae->Find("AXIS1_PLACEMENT") : nullptr;
      ON_3dPoint origin = ON_origin;
      ON_3dVector axis(0, 0, 1);
      if (ap) { if (ap->args.size() > 1 && IsRef(ap->args[1])) origin = Point(StepRef(ap->args[1])); if (ap->args.size() > 2 && IsRef(ap->args[2])) axis = Direction(StepRef(ap->args[2])); }
      ON_RevSurface* rs = ON_RevSurface::New();
      rs->m_curve = new ON_NurbsCurve(gen);
      rs->m_axis = ON_Line(origin, origin + axis);
      rs->m_angle = ON_Interval(0, 2 * ON_PI);
      rs->m_t = rs->m_angle;
      const bool ok = rs->GetNurbForm(out) > 0;
      delete rs;
      return ok;
    }
    if (const StepPart* p = e->Find("SURFACE_OF_LINEAR_EXTRUSION")) {
      if (p->args.size() < 3 || !IsRef(p->args[1]) || !IsRef(p->args[2])) return false;
      ON_NurbsCurve gen;
      if (!Curve(StepRef(p->args[1]), gen)) return false;
      const ON_3dVector v = Direction(StepRef(p->args[2])) * 1e5;
      ON_SumSurface ss;
      if (!ss.Create(gen, v)) return false;
      return ss.GetNurbForm(out) > 0;
    }
    if (const StepPart* p = e->Find("B_SPLINE_SURFACE_WITH_KNOTS")) {
      if (p->args.size() < 12) return false;
      const int du = std::atoi(Trim(p->args[1]).c_str()), dv = std::atoi(Trim(p->args[2]).c_str());
      // Control point list is a list of rows (u), each a list of columns (v).
      std::vector<std::vector<int>> rows;
      for (const std::string& row : SplitTop(Unparen(p->args[3]), ',')) {}  // placeholder, real split below
      // The row split above breaks on inner commas too (each row is itself
      // "(#a,#b,...)"), so split rows at the outer level explicitly.
      rows.clear();
      {
        const std::string body = Unparen(p->args[3]);
        int depth = 0;
        std::string cur;
        for (char c : body) {
          if (c == '(') { if (depth > 0) cur += c; ++depth; continue; }
          if (c == ')') { --depth; if (depth > 0) cur += c; else { rows.push_back(StepRefList("(" + cur + ")")); cur.clear(); } continue; }
          if (depth > 0) cur += c;
        }
      }
      if (rows.empty()) return false;
      const int nu = static_cast<int>(rows.size()), nv = static_cast<int>(rows.front().size());
      std::vector<ON_3dPoint> cvs(static_cast<size_t>(nu * nv));
      for (int i = 0; i < nu; ++i) for (int j = 0; j < nv && j < static_cast<int>(rows[static_cast<size_t>(i)].size()); ++j) cvs[static_cast<size_t>(j * nu + i)] = Point(rows[static_cast<size_t>(i)][static_cast<size_t>(j)]);
      const std::vector<int> mu = StepIntList(p->args[8]);
      const std::vector<int> mv = StepIntList(p->args[9]);
      const std::vector<double> ku = StepRealList(p->args[10]);
      const std::vector<double> kv = StepRealList(p->args[11]);
      std::vector<double> full_u, full_v;
      for (size_t i = 0; i < ku.size() && i < mu.size(); ++i) for (int k = 0; k < mu[i]; ++k) full_u.push_back(ku[i]);
      for (size_t i = 0; i < kv.size() && i < mv.size(); ++i) for (int k = 0; k < mv[i]; ++k) full_v.push_back(kv[i]);
      std::vector<double> weights;
      if (const StepPart* rp = e->Find("RATIONAL_B_SPLINE_SURFACE")) {
        if (!rp->args.empty()) {
          std::vector<std::vector<double>> wrows;
          const std::string body = Unparen(rp->args[0]);
          int depth = 0;
          std::string cur;
          for (char c : body) {
            if (c == '(') { if (depth > 0) cur += c; ++depth; continue; }
            if (c == ')') { --depth; if (depth > 0) cur += c; else { wrows.push_back(StepRealList("(" + cur + ")")); cur.clear(); } continue; }
            if (depth > 0) cur += c;
          }
          weights.assign(static_cast<size_t>(nu * nv), 1.0);
          for (int i = 0; i < nu && i < static_cast<int>(wrows.size()); ++i) for (int j = 0; j < nv && j < static_cast<int>(wrows[static_cast<size_t>(i)].size()); ++j) weights[static_cast<size_t>(j * nu + i)] = wrows[static_cast<size_t>(i)][static_cast<size_t>(j)];
        }
      }
      return MakeNurbsSurface(du, dv, nu, nv, cvs, weights, full_u, full_v, out);
    }
    return false;
  }

  std::map<int, ON_3dPoint> pt_cache_;
  std::map<int, ON_NurbsCurve> curve_cache_;
  std::map<int, ON_NurbsSurface> surf_cache_;
  std::map<int, Color> colour_cache_;
};

struct StepImportStats {
  int curves = 0, points = 0, breps = 0, faces_trimmed = 0, faces_untrimmed_fallback = 0, meshes = 0, skipped = 0, layers = 0;
};

// Reads all ADVANCED_FACE ids inside a shell's own face list.
std::vector<int> ShellFaces(const StepModel& m, int shell_id) {
  const StepEntity* e = m.Get(shell_id);
  if (!e) return {};
  if (const StepPart* p = e->Find("CLOSED_SHELL")) return StepRefList(p->args.size() > 1 ? p->args[1] : "");
  if (const StepPart* p = e->Find("OPEN_SHELL")) return StepRefList(p->args.size() > 1 ? p->args[1] : "");
  return {};
}

bool BuildFaceInto(StepModel& m, ON_Brep& brep, int face_id, double tol, StepImportStats& stats) {
  const StepEntity* e = m.Get(face_id);
  const StepPart* fp = e ? e->Find("ADVANCED_FACE") : nullptr;
  if (!fp || fp->args.size() < 4) return false;
  const std::vector<int> bounds = StepRefList(fp->args[1]);
  const bool same_sense = Trim(fp->args[3]) == ".T.";
  ON_NurbsSurface srf;
  if (!(IsRef(fp->args[2]) && m.Surface(StepRef(fp->args[2]), srf))) return false;

  std::vector<std::vector<LoopSeg>> loops;
  for (int bde : bounds) {
    const StepEntity* be = m.Get(bde);
    const StepPart* bp = be ? (be->Find("FACE_OUTER_BOUND") ? be->Find("FACE_OUTER_BOUND") : be->Find("FACE_BOUND")) : nullptr;
    if (!bp || bp->args.size() < 2 || !IsRef(bp->args[1])) continue;
    const bool bound_orient = bp->args.size() > 2 ? Trim(bp->args[2]) == ".T." : true;
    const StepEntity* le = m.Get(StepRef(bp->args[1]));
    const StepPart* lp = le ? le->Find("EDGE_LOOP") : nullptr;
    if (!lp || lp->args.size() < 2) continue;
    const std::vector<std::string> oedges = SplitTop(Unparen(lp->args[1]), ',');
    std::vector<LoopSeg> segs;
    for (const std::string& oref : oedges) {
      if (!IsRef(oref)) continue;
      const StepEntity* oe = m.Get(StepRef(oref));
      const StepPart* op = oe ? oe->Find("ORIENTED_EDGE") : nullptr;
      if (!op || op->args.size() < 5 || !IsRef(op->args[3])) continue;
      const bool orient = Trim(op->args[4]) == ".T.";
      ON_NurbsCurve c3;
      if (!m.EdgeCurve3D(StepRef(op->args[3]), orient == bound_orient, c3)) continue;
      LoopSeg seg;
      seg.c3 = c3;
      if (!ProjectToSurface(srf, c3, tol, seg.c2)) continue;
      seg.c2.ChangeDimension(3);
      segs.push_back(std::move(seg));
    }
    if (!segs.empty()) loops.push_back(std::move(segs));
  }
  if (loops.empty()) { brep.NewFace(srf); ++stats.faces_untrimmed_fallback; return true; }
  ON_Brep test;
  const int fi = AddTrimmedFace(test, new ON_NurbsSurface(srf), loops, tol);
  // Must run before IsValid(): see the matching comment in the IGES 144
  // handler above.
  if (fi >= 0) test.SetTrimIsoFlags();
  if (fi < 0 || !test.IsValid()) {
    ON_Brep fresh;
    fresh.NewFace(srf);
    brep.Append(fresh);
    ++stats.faces_untrimmed_fallback;
    return true;
  }
  if (!same_sense) test.FlipFace(test.m_F[fi]);
  brep.Append(test);
  ++stats.faces_trimmed;
  return true;
}

std::vector<ON_MeshFace> unused_;

bool BuildMeshFromFacetedBrep(StepModel& m, int shell_id, kernel::Mesh& out_mesh) {
  const std::vector<int> face_ids = ShellFaces(m, shell_id);
  if (face_ids.empty()) return false;
  ON_Mesh& mesh = out_mesh.raw();
  std::map<int, int> vid_map;
  for (int fde : face_ids) {
    const StepEntity* fe = m.Get(fde);
    const StepPart* fp = fe ? fe->Find("FACE") : (fe ? fe->Find("ADVANCED_FACE") : nullptr);
    if (!fp) continue;
    const std::vector<int> bounds = StepRefList(fp->args.size() > 1 ? fp->args[1] : "");
    for (int bde : bounds) {
      const StepEntity* be = m.Get(bde);
      const StepPart* bp = be ? (be->Find("FACE_OUTER_BOUND") ? be->Find("FACE_OUTER_BOUND") : be->Find("FACE_BOUND")) : nullptr;
      if (!bp || bp->args.size() < 2 || !IsRef(bp->args[1])) continue;
      const StepEntity* le = m.Get(StepRef(bp->args[1]));
      const StepPart* lp = le ? le->Find("POLY_LOOP") : nullptr;
      if (!lp || lp->args.size() < 2) continue;
      const std::vector<int> pts = StepRefList(lp->args[1]);
      if (pts.size() < 3) continue;
      std::vector<int> vi;
      for (int pid : pts) {
        auto it = vid_map.find(pid);
        if (it != vid_map.end()) { vi.push_back(it->second); continue; }
        const int idx = mesh.VertexCount();
        mesh.SetVertex(idx, m.Point(pid));
        vid_map[pid] = idx;
        vi.push_back(idx);
      }
      const int fi = mesh.FaceCount();
      if (vi.size() == 3) mesh.SetTriangle(fi, vi[0], vi[1], vi[2]);
      else mesh.SetQuad(fi, vi[0], vi[1], vi[2], vi[static_cast<size_t>(std::min<size_t>(3, vi.size() - 1))]);
    }
  }
  if (mesh.FaceCount() == 0) return false;
  mesh.ComputeFaceNormals();
  mesh.ComputeVertexNormals();
  return true;
}

}  // namespace

bool ImportStep(Document& doc, const std::string& path, std::string& summary) {
  summary.clear();
  std::ifstream is(path, std::ios::binary);
  if (!is) { summary = "Could not open " + path; return false; }
  std::ostringstream buf;
  buf << is.rdbuf();
  std::string text = buf.str();
  if (text.find("ISO-10303-21") == std::string::npos) { summary = "Not a STEP file: " + path; return false; }

  const size_t data_pos = text.find("DATA;");
  const size_t end_pos = text.rfind("ENDSEC;");
  if (data_pos == std::string::npos) { summary = "No DATA section in " + path; return false; }
  std::string data = text.substr(data_pos + 5, (end_pos != std::string::npos && end_pos > data_pos ? end_pos - data_pos - 5 : std::string::npos));

  // Strip comments, then split into ';'-terminated records (quote-aware).
  {
    std::string stripped;
    stripped.reserve(data.size());
    bool q = false;
    for (size_t i = 0; i < data.size(); ++i) {
      if (!q && data[i] == '/' && i + 1 < data.size() && data[i + 1] == '*') { i += 2; while (i + 1 < data.size() && !(data[i] == '*' && data[i + 1] == '/')) ++i; ++i; continue; }
      if (data[i] == '\'') { if (q && i + 1 < data.size() && data[i + 1] == '\'') { stripped += "''"; ++i; continue; } q = !q; }
      stripped += data[i];
    }
    data = stripped;
  }
  std::vector<std::string> records = SplitTop(data, ';');

  StepModel model;
  for (const std::string& raw : records) {
    const std::string r = Trim(raw);
    if (r.empty() || r[0] != '#') continue;
    const size_t eq = r.find('=');
    if (eq == std::string::npos) continue;
    const int id = std::atoi(r.c_str() + 1);
    if (id <= 0) continue;
    model.entities[id] = ParseEntity(r.substr(eq + 1));
  }
  if (model.entities.empty()) { summary = "No entities found in " + path; return false; }

  // Colours: for every STYLED_ITEM, walk its style tree (bounded depth) for
  // a COLOUR_RGB and remember it against the item it decorates.
  for (const auto& [id, e] : model.entities) {
    const StepPart* sp = e.Find("STYLED_ITEM");
    if (!sp || sp->args.size() < 3) continue;
    const int item = IsRef(sp->args[2]) ? StepRef(sp->args[2]) : 0;
    if (!item) continue;
    std::vector<int> frontier = StepRefList(sp->args[1]);
    std::set<int> seen;
    Color found;
    bool have = false;
    for (int depth = 0; depth < 6 && !have && !frontier.empty(); ++depth) {
      std::vector<int> next;
      for (int fid : frontier) {
        if (!seen.insert(fid).second) continue;
        const StepEntity* fe = model.Get(fid);
        if (!fe) continue;
        if (const StepPart* cp = fe->Find("COLOUR_RGB")) {
          if (cp->args.size() >= 4) { found = ColorFromBytes(static_cast<int>(std::lround(StepReal(cp->args[1]) * 255)), static_cast<int>(std::lround(StepReal(cp->args[2]) * 255)), static_cast<int>(std::lround(StepReal(cp->args[3]) * 255))); have = true; break; }
        }
        for (const StepPart& part : fe->parts) for (const std::string& a : part.args) if (IsRef(a)) next.push_back(StepRef(a));
      }
      frontier = next;
    }
    if (have) model.item_colour_[item] = found;
  }

  const double tol = doc.Settings().absolute_tolerance > 0 ? doc.Settings().absolute_tolerance : 0.001;
  StepImportStats stats;
  auto name_of = [&](const StepEntity& e, const char* kw) -> std::string {
    const StepPart* p = e.Find(kw);
    return p && !p->args.empty() ? StepString(p->args[0]) : "";
  };
  auto add_layer_default = [&]() { return -1; };
  (void)add_layer_default;

  // Track ids consumed as members of a shell/curve-set so their own
  // top-level entities aren't also read standalone.
  std::set<int> consumed;
  for (const auto& [id, e] : model.entities) {
    for (const std::string& kw : {std::string("MANIFOLD_SOLID_BREP"), std::string("SHELL_BASED_SURFACE_MODEL")}) {
      const StepPart* p = e.Find(kw);
      if (!p) continue;
      std::vector<int> shells;
      if (kw == "MANIFOLD_SOLID_BREP" && p->args.size() > 1 && IsRef(p->args[1])) shells.push_back(StepRef(p->args[1]));
      else if (kw == "SHELL_BASED_SURFACE_MODEL" && p->args.size() > 1) shells = StepRefList(p->args[1]);
      for (int sid : shells) for (int fid : ShellFaces(model, sid)) consumed.insert(fid);
    }
    if (const StepPart* p = e.Find("FACETED_BREP")) if (p->args.size() > 1 && IsRef(p->args[1])) for (int fid : ShellFaces(model, StepRef(p->args[1]))) consumed.insert(fid);
    if (const StepPart* p = e.Find("GEOMETRIC_CURVE_SET")) if (p->args.size() > 1) for (int r : StepRefList(p->args[1])) consumed.insert(r);
  }

  for (const auto& [id, e] : model.entities) {
    if (e.Find("MANIFOLD_SOLID_BREP")) {
      const StepPart* p = e.Find("MANIFOLD_SOLID_BREP");
      if (p->args.size() < 2 || !IsRef(p->args[1])) continue;
      ON_Brep brep;
      for (int fid : ShellFaces(model, StepRef(p->args[1]))) BuildFaceInto(model, brep, fid, tol, stats);
      JoinEdges(brep, tol * 10);
      FinishBrep(brep);
      if (brep.m_F.Count() == 0) continue;
      kernel::Brep k;
      k.raw() = brep;
      SceneObject o = SceneObject::MakeBrep(k);
      o.name = name_of(e, "MANIFOLD_SOLID_BREP");
      if (model.HasColour(id)) { o.color_by_layer = false; o.color = model.ColourOfItem(id); }
      doc.Add(std::move(o));
      ++stats.breps;
    } else if (e.Find("SHELL_BASED_SURFACE_MODEL")) {
      const StepPart* p = e.Find("SHELL_BASED_SURFACE_MODEL");
      if (p->args.size() < 2) continue;
      for (int sid : StepRefList(p->args[1])) {
        ON_Brep brep;
        for (int fid : ShellFaces(model, sid)) BuildFaceInto(model, brep, fid, tol, stats);
        if (brep.m_F.Count() == 0) continue;
        FinishBrep(brep);
        kernel::Brep k;
        k.raw() = brep;
        SceneObject o = SceneObject::MakeBrep(k);
        o.name = name_of(e, "SHELL_BASED_SURFACE_MODEL");
        if (model.HasColour(id)) { o.color_by_layer = false; o.color = model.ColourOfItem(id); }
        doc.Add(std::move(o));
        ++stats.breps;
      }
    } else if (e.Find("FACETED_BREP")) {
      const StepPart* p = e.Find("FACETED_BREP");
      if (p->args.size() < 2 || !IsRef(p->args[1])) continue;
      kernel::Mesh mesh;
      if (BuildMeshFromFacetedBrep(model, StepRef(p->args[1]), mesh)) {
        SceneObject o = SceneObject::MakeMesh(mesh);
        o.name = name_of(e, "FACETED_BREP");
        if (model.HasColour(id)) { o.color_by_layer = false; o.color = model.ColourOfItem(id); }
        doc.Add(std::move(o));
        ++stats.meshes;
      }
    } else if (const StepPart* p = e.Find("GEOMETRIC_CURVE_SET")) {
      if (p->args.size() < 2) continue;
      for (int r : StepRefList(p->args[1])) {
        const StepEntity* re = model.Get(r);
        if (!re) continue;
        if (re->Find("CARTESIAN_POINT")) {
          SceneObject o = SceneObject::MakePoint(model.Point(r));
          o.name = name_of(e, "GEOMETRIC_CURVE_SET");
          doc.Add(std::move(o));
          ++stats.points;
        } else {
          ON_NurbsCurve nc;
          if (model.Curve(r, nc)) {
            kernel::NurbsCurve k;
            k.raw() = nc;
            SceneObject o = SceneObject::MakeCurve(k);
            o.name = name_of(e, "GEOMETRIC_CURVE_SET");
            doc.Add(std::move(o));
            ++stats.curves;
          } else {
            ++stats.skipped;
          }
        }
      }
    }
  }

  // Any advanced face never claimed by a shell (a lone ADVANCED_FACE some
  // exporters emit outside a shell) becomes its own single-face brep.
  for (const auto& [id, e] : model.entities) {
    if (!e.Find("ADVANCED_FACE") || consumed.count(id)) continue;
    ON_Brep brep;
    if (BuildFaceInto(model, brep, id, tol, stats)) {
      FinishBrep(brep);
      kernel::Brep k;
      k.raw() = brep;
      doc.Add(SceneObject::MakeBrep(k));
      ++stats.breps;
    }
  }

  std::ostringstream ss;
  ss << "STEP: " << stats.breps << " brep" << (stats.breps == 1 ? "" : "s") << " (" << stats.faces_trimmed << " trimmed face" << (stats.faces_trimmed == 1 ? "" : "s") << ")"
     << ", " << stats.curves << " curve" << (stats.curves == 1 ? "" : "s") << ", " << stats.points << " point" << (stats.points == 1 ? "" : "s");
  if (stats.meshes) ss << ", " << stats.meshes << " mesh" << (stats.meshes == 1 ? "" : "es");
  if (stats.faces_untrimmed_fallback) ss << ", " << stats.faces_untrimmed_fallback << " face" << (stats.faces_untrimmed_fallback == 1 ? "" : "s") << " fell back to untrimmed";
  if (stats.skipped) ss << "; " << stats.skipped << " unsupported entit" << (stats.skipped == 1 ? "y" : "ies") << " skipped";
  summary = ss.str();
  if (stats.breps + stats.curves + stats.points + stats.meshes == 0) { summary = "No usable geometry found in " + path; return false; }
  return true;
}

}  // namespace dino8::app
