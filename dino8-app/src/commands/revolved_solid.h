// Plain revolved solids (cone / frustum) as an exact (radius, height) profile: recognise one from
// its faces, offset the profile with sharp corners, and revolve a profile back into a closed
// Brep whose every face is an exact revolved surface (plane / cone / cylinder). Shared by
// cmd_surface.cpp (OffsetSrf, Shell) and cmd_fillet.cpp (ChamferEdge on a cone / frustum rim).
#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <optional>
#include <utility>
#include <vector>

#include "commands/cmd_common.h"

namespace dino8::app::revolved {

// Revolves the open polyline profile `prof` ((radius, height) pairs about the axis
// through c0 along ax, radial direction u) 360 degrees, one EXACT revolved face per
// segment (a segment parallel to the axis gives a cylinder wall, a radial one a
// flat disc / annulus), and joins the faces along their shared circles
// (Brep::JoinNakedEdges: the circles coincide exactly). A profile that starts and
// ends on the axis gives a closed solid (Shell uses this for an opened cup).
inline std::optional<kernel::Brep> RevolveProfileBrep(ON_3dPoint c0, ON_3dVector ax, ON_3dVector u, const std::vector<std::pair<double, double>>& prof) {
  ON_Brep all;
  for (size_t i = 0; i + 1 < prof.size(); ++i) {
    const ON_3dPoint a = c0 + prof[i].first * u + prof[i].second * ax, e = c0 + prof[i + 1].first * u + prof[i + 1].second * ax;
    ON_RevSurface* rev = new ON_RevSurface();
    rev->m_curve = new ON_LineCurve(a, e);
    rev->m_axis = ON_Line(c0, c0 + ax);
    rev->m_angle = ON_Interval(0.0, 2.0 * ON_PI);
    rev->m_t = rev->m_angle;
    rev->m_bTransposed = false;
    ON_Brep* piece = ON_BrepRevSurface(rev, false, false);
    if (!piece) return std::nullopt;
    all.Append(*piece);
    delete piece;
  }
  kernel::Brep k;
  k.raw() = all;
  try {
    k.JoinNakedEdges(1e-6);
    k.UnifyNormals();
  } catch (const std::exception&) {
    return std::nullopt;
  }
  if (!k.raw().IsSolid() || !k.raw().IsValid()) return std::nullopt;
  return k;
}

// A PLAIN CAPPED CONE or CONE FRUSTUM (what the Cone / TCone commands make): one
// wall that is a coaxial cone plus one flat cap (cone) or two parallel flat caps
// (frustum). `prof` is the closed convex (radius, height) profile, counter-clockwise,
// starting and ending on the axis; `face[k]` is the brep face of the profile edge
// prof[k] -> prof[k+1] (the closing axis edge has no face). A cylinder is NOT
// matched here (RecognizePlainCylinder handles it).
struct PlainRevolved {
  ON_3dPoint c0;
  ON_3dVector ax, u;
  std::vector<std::pair<double, double>> prof;
  std::vector<int> face;
};

inline bool RecognizeConeLike(const ON_Brep& b, PlainRevolved& pr) {
  const int nf = b.m_F.Count();
  if (!b.IsSolid() || (nf != 2 && nf != 3)) return false;
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double diag = bb.Diagonal().Length();
  const double tol = std::max(1e-9, diag * 1e-6);
  std::vector<int> caps;
  std::vector<ON_Plane> cp;
  int wall = -1;
  for (int f = 0; f < nf; ++f) {
    const ON_Surface* surf = b.m_F[f].SurfaceOf();
    if (!surf) return false;
    ON_Plane pl;
    if (surf->IsPlanar(&pl, tol)) { caps.push_back(f); cp.push_back(pl); }
    else if (wall < 0) wall = f;
    else return false;
  }
  if (wall < 0 || static_cast<int>(caps.size()) != nf - 1) return false;
  ON_3dVector ax = cp[0].zaxis;
  if (!ax.Unitize()) return false;
  if (caps.size() == 2 && std::fabs(cp[1].zaxis * ax) < 1.0 - 1e-9) return false;
  ON_BoundingBox cb;
  b.m_F[caps[0]].GetBoundingBox(cb);
  const ON_3dPoint c = cb.Center();
  // Sample the wall: for a coaxial cone the distance from the axis is linear in the height along it.
  const ON_Surface* ws = b.m_F[wall].SurfaceOf();
  const ON_Interval du = ws->Domain(0), dv = ws->Domain(1);
  double sh = 0, sr = 0, shh = 0, shr = 0, hmin = 1e300, hmax = -1e300;
  std::vector<std::pair<double, double>> hr;
  for (int i = 1; i <= 4; ++i) {
    for (int j = 1; j <= 4; ++j) {
      const ON_3dPoint P = ws->PointAt(du.ParameterAt(i / 5.0), dv.ParameterAt(j / 5.0));
      const ON_3dVector d = P - c;
      const double h = d * ax;
      const double r = (d - h * ax).Length();
      hr.emplace_back(h, r);
      sh += h; sr += r; shh += h * h; shr += h * r;
      hmin = std::min(hmin, h); hmax = std::max(hmax, h);
    }
  }
  const double n = static_cast<double>(hr.size());
  const double den = n * shh - sh * sh;
  if (!(hmax - hmin > tol) || std::fabs(den) < 1e-12) return false;
  const double bs = (n * shr - sh * sr) / den, as = (sr - bs * sh) / n;
  for (const auto& [h, r] : hr) if (std::fabs(as + bs * h - r) > diag * 1e-5) return false;
  if (std::fabs(bs) < 1e-4) return false;  // a cylinder
  std::vector<double> hc;
  for (const ON_Plane& pl : cp) hc.push_back((pl.origin - c) * ax);
  pr.ax = ax;
  pr.u = cp[0].xaxis;
  if (caps.size() == 2) {
    const int lo = hc[0] <= hc[1] ? 0 : 1, hi = 1 - lo;
    const double rlo = as + bs * hc[lo], rhi = as + bs * hc[hi], H = hc[hi] - hc[lo];
    if (!(rlo > tol) || !(rhi > tol) || !(H > tol)) return false;
    pr.c0 = c + hc[lo] * ax;
    pr.prof = {{0, 0}, {rlo, 0}, {rhi, H}, {0, H}};
    pr.face = {caps[lo], wall, caps[hi]};
    return true;
  }
  const double rc = as + bs * hc[0], ha = -as / bs;
  if (!(rc > tol) || std::fabs(ha - hc[0]) < tol) return false;
  if (ha > hc[0]) {
    pr.c0 = c + hc[0] * ax;
    pr.prof = {{0, 0}, {rc, 0}, {0, ha - hc[0]}};
    pr.face = {caps[0], wall};
  } else {
    pr.c0 = c + ha * ax;
    pr.prof = {{0, 0}, {rc, hc[0] - ha}, {0, hc[0] - ha}};
    pr.face = {wall, caps[0]};
  }
  return true;
}

// Offsets every non-axis edge of the closed profile `P` by d[k] along its outward normal
// (sharp corners, like OffsetSolidConvexPlanar does for planes) and re-intersects the
// neighbouring lines; the two end vertices stay on the axis. nullopt if the result is
// degenerate (an edge collapsed or flipped, a vertex crossed the axis).
inline std::optional<std::vector<std::pair<double, double>>> OffsetProfile(const std::vector<std::pair<double, double>>& P, const std::vector<double>& d) {
  const size_t n = P.size();
  if (n < 3 || d.size() != n - 1) return std::nullopt;
  struct Ln { double px, py, dx, dy; };
  std::vector<Ln> L;
  for (size_t k = 0; k + 1 < n; ++k) {
    const double dr = P[k + 1].first - P[k].first, dz = P[k + 1].second - P[k].second, len = std::hypot(dr, dz);
    if (!(len > 1e-12)) return std::nullopt;
    L.push_back({P[k].first + d[k] * dz / len, P[k].second - d[k] * dr / len, dr / len, dz / len});
  }
  auto isect = [](const Ln& a, const Ln& b, std::pair<double, double>& out) {
    const double cr = a.dx * b.dy - a.dy * b.dx;
    if (std::fabs(cr) < 1e-12) return false;
    const double t = ((b.px - a.px) * b.dy - (b.py - a.py) * b.dx) / cr;
    out = {a.px + t * a.dx, a.py + t * a.dy};
    return true;
  };
  const Ln axis{0, 0, 0, 1};
  std::vector<std::pair<double, double>> Q(n);
  if (!isect(L.front(), axis, Q.front()) || !isect(L.back(), axis, Q.back())) return std::nullopt;
  for (size_t k = 1; k + 1 < n; ++k) if (!isect(L[k - 1], L[k], Q[k])) return std::nullopt;
  for (size_t k = 0; k < L.size(); ++k) {
    const double dr = Q[k + 1].first - Q[k].first, dz = Q[k + 1].second - Q[k].second;
    if (dr * L[k].dx + dz * L[k].dy < 1e-9) return std::nullopt;
    if (k + 1 < n - 1 && !(Q[k + 1].first > 1e-9)) return std::nullopt;
  }
  Q.front().first = 0; Q.back().first = 0;
  return Q;
}

// RevolveProfileBrep, with the result forced to face outward.
inline std::optional<kernel::Brep> RevolveOutward(const PlainRevolved& pr, const std::vector<std::pair<double, double>>& prof) {
  std::optional<kernel::Brep> k = RevolveProfileBrep(pr.c0, pr.ax, pr.u, prof);
  if (!k) return std::nullopt;
  if (k->raw().SolidOrientation() < 0) k->raw().Flip();
  return k;
}

// Shell WITH face removal and/or per-face thickness of a plain revolved solid (cylinder, cone, frustum): the
// closed (radius, height) profile `prof` about the axis through c0 along ax (radial direction u), with
// `face[k]` the brep face of edge prof[k] -> prof[k+1] (the closing axis edge has none). Every face gets `t`,
// `overrides[face]` its own value, and each face in `removed` 0 (opened); then kernel::ShellRevolvedProfile
// builds it exactly (its cavity / rim / volume checks all run in the kernel). nullopt with the kernel's reason
// in `why` otherwise - including a face index that is not one of this solid's faces.
inline std::optional<kernel::Brep> ShellRevolvedMeridian(ON_3dPoint c0, ON_3dVector ax, ON_3dVector u,
                                                        const std::vector<std::pair<double, double>>& prof, const std::vector<int>& face,
                                                        const std::vector<int>& removed, const std::map<int, double>& overrides, double t,
                                                        std::string& why) {
  auto known = [&](int f) { return std::find(face.begin(), face.end(), f) != face.end(); };
  for (int f : removed) if (!known(f)) { why = "invalid face index"; return std::nullopt; }
  for (const auto& kv : overrides) if (!known(kv.first)) { why = "invalid face index"; return std::nullopt; }
  const size_t n = prof.size();
  if (n < 3 || face.size() + 1 != n) { why = "the revolved profile is not a closed meridian polyline"; return std::nullopt; }
  std::vector<double> th(n, 0.0);
  for (size_t k = 0; k + 1 < n; ++k) {
    const int f = face[k];
    if (std::find(removed.begin(), removed.end(), f) != removed.end()) th[k] = 0.0;
    else { const auto it = overrides.find(f); th[k] = it != overrides.end() ? it->second : t; }
  }
  std::vector<kernel::Point3d> pts;
  for (const auto& rz : prof) {
    const ON_3dPoint q = c0 + rz.first * u + rz.second * ax;
    pts.emplace_back(q.x, q.y, q.z);
  }
  pts.push_back(pts.front());
  try {
    return kernel::ShellRevolvedProfile(kernel::NurbsCurve::FromControlPoints(pts, 1), kernel::Point3d(c0.x, c0.y, c0.z),
                                        kernel::Vector3d(ax.x, ax.y, ax.z), th);
  } catch (const std::exception& e) {
    why = e.what();
    return std::nullopt;
  }
}

// A SPHERE CAP / DOME / BOWL or a TORUS SEGMENT recognised from an ON_Brep: exactly two faces, one flat and one a sphere
// (a ball cut by a plane: a bowl, a hemisphere or a dome) or a torus whose axis is the flat face's normal (a ring cut by a
// plane perpendicular to its axis). Both are bodies of revolution about the axis through `o` along `ax`, where `o` is the
// foot of the curved surface's centre on the flat face and `ax` points INTO the solid (opposite the flat face's outward
// normal). `zc` is the height of the arc's centre above the flat face along `ax` (negative below it); the meridian is the
// flat chord plus one circular arc (see kernel::ShellRevolvedArcProfile).
struct ArcSolid {
  ON_3dPoint o;
  ON_3dVector ax, ex;
  bool torus = false;
  double rho = 0;  // sphere radius, or the torus's minor radius
  double big = 0;  // torus major radius (distance of the arc centre from the axis)
  double zc = 0;
  int flat_face = -1, curved_face = -1;
};

inline bool RecognizeArcSolid(const ON_Brep& b, ArcSolid& s) {
  if (!b.IsSolid() || b.m_F.Count() != 2) return false;
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double tol = std::max(1e-9, bb.Diagonal().Length() * 1e-6);
  ON_Plane plane;
  ON_Sphere sph;
  ON_Torus tor;
  int flat = -1, curved = -1;
  bool is_torus = false;
  for (int f = 0; f < 2; ++f) {
    const ON_Surface* surf = b.m_F[f].SurfaceOf();
    if (!surf) return false;
    if (flat < 0 && surf->IsPlanar(&plane, tol)) flat = f;
    else if (curved < 0 && surf->IsSphere(&sph, tol)) curved = f;
    else if (curved < 0 && surf->IsTorus(&tor, tol)) { curved = f; is_torus = true; }
    else return false;
  }
  if (flat < 0 || curved < 0) return false;
  const ON_Interval du = b.m_F[flat].SurfaceOf()->Domain(0), dv = b.m_F[flat].SurfaceOf()->Domain(1);
  ON_3dVector n = b.m_F[flat].NormalAt(du.Mid(), dv.Mid());
  if (!n.Unitize()) return false;
  if (b.SolidOrientation() < 0) n = -n;  // outward normal of the flat face
  const ON_3dVector ax = -n;
  ON_3dPoint centre;
  if (is_torus) {
    ON_3dVector ta = tor.plane.zaxis;
    if (!ta.Unitize() || std::fabs(ta * ax) < 1.0 - 1e-9) return false;  // the torus axis must be the flat face's normal
    centre = tor.plane.origin;
    s.rho = tor.minor_radius;
    s.big = tor.major_radius;
  } else {
    centre = sph.Center();
    s.rho = sph.Radius();
    s.big = 0;
  }
  const ON_3dPoint o = centre - ((centre - plane.origin) * n) * n;  // foot of the centre on the flat face's plane
  s.o = o;
  s.ax = ax;
  s.ex = plane.xaxis;
  if (!s.ex.Unitize() || std::fabs(s.ex * ax) > 1e-9) return false;
  s.zc = (centre - o) * ax;
  s.torus = is_torus;
  s.flat_face = flat;
  s.curved_face = curved;
  if (!(s.rho > tol) || !(std::fabs(s.zc) < s.rho - tol)) return false;  // the plane must actually cut the ball / tube
  if (is_torus && !(s.big > s.rho + tol)) return false;
  return true;
}

// Shell WITH face removal / per-face thickness of a recognised sphere cap or torus segment through
// kernel::ShellRevolvedArcProfile (the flat face is `flat_face`, the curved one `curved_face`).
inline std::optional<kernel::Brep> ShellArcSolid(const ArcSolid& s, const std::vector<int>& removed, const std::map<int, double>& overrides,
                                                 double t, std::string& why) {
  for (int f : removed) if (f != s.flat_face && f != s.curved_face) { why = "invalid face index"; return std::nullopt; }
  for (const auto& kv : overrides) if (kv.first != s.flat_face && kv.first != s.curved_face) { why = "invalid face index"; return std::nullopt; }
  auto thick = [&](int f) {
    if (std::find(removed.begin(), removed.end(), f) != removed.end()) return 0.0;
    const auto it = overrides.find(f);
    return it != overrides.end() ? it->second : t;
  };
  const double w = std::sqrt(s.rho * s.rho - s.zc * s.zc);  // half-width of the flat face's cut through the ball / tube
  std::vector<kernel::MeridianEdge> e;
  std::vector<double> th;
  auto line = [](double r, double z) { kernel::MeridianEdge m; m.r = r; m.z = z; return m; };
  auto arc = [](double r, double z, double cr, double cz) { kernel::MeridianEdge m; m.r = r; m.z = z; m.arc = true; m.cr = cr; m.cz = cz; m.counter_clockwise = true; return m; };
  if (s.torus) {
    // chord (R - w, 0) -> (R + w, 0) is the flat face; the arc runs from (R + w, 0) over the tube to (R - w, 0).
    e = {arc(s.big + w, 0, s.big, s.zc), line(s.big - w, 0)};
    th = {thick(s.curved_face), thick(s.flat_face)};
  } else {
    // flat disc (0, 0) -> (w, 0), the arc up to the apex (0, zc + rho), then the axis back down.
    e = {line(0, 0), arc(w, 0, 0, s.zc), line(0, s.zc + s.rho)};
    th = {thick(s.flat_face), thick(s.curved_face), 0.0};
  }
  try {
    return kernel::ShellRevolvedArcProfile(e, kernel::Point3d(s.o.x, s.o.y, s.o.z), kernel::Vector3d(s.ax.x, s.ax.y, s.ax.z),
                                           kernel::Vector3d(s.ex.x, s.ex.y, s.ex.z), th);
  } catch (const std::exception& ex) {
    why = ex.what();
    return std::nullopt;
  }
}

}  // namespace dino8::app::revolved
