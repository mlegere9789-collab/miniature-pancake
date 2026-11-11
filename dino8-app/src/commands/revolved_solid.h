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

// The meridian of a recognised sphere cap / torus segment (kernel::MeridianEdge, r measured along `ex`, z along `ax` from the
// flat face): a sphere is the flat disc (0,0)->(w,0), the arc up to the apex (0, zc+rho) and the axis back down; a torus segment is
// the arc from (R+w,0) over the tube to (R-w,0) and the flat chord back (no edge on the axis). w = sqrt(rho^2 - zc^2).
inline std::vector<kernel::MeridianEdge> ArcSolidMeridian(const ArcSolid& s) {
  const double w = std::sqrt(s.rho * s.rho - s.zc * s.zc);
  auto line = [](double r, double z) { kernel::MeridianEdge m; m.r = r; m.z = z; return m; };
  auto arc = [](double r, double z, double cr, double cz) { kernel::MeridianEdge m; m.r = r; m.z = z; m.arc = true; m.cr = cr; m.cz = cz; m.counter_clockwise = true; return m; };
  if (s.torus) return {arc(s.big + w, 0, s.big, s.zc), line(s.big - w, 0)};
  return {line(0, 0), arc(w, 0, 0, s.zc), line(0, s.zc + s.rho)};
}

// Whole-body offset by `d` (positive grows) of a recognised sphere cap / torus segment through kernel::OffsetRevolvedArcProfile.
inline std::optional<kernel::Brep> OffsetArcSolid(const ArcSolid& s, double d, std::string& why) {
  try {
    return kernel::OffsetRevolvedArcProfile(ArcSolidMeridian(s), kernel::Point3d(s.o.x, s.o.y, s.o.z), kernel::Vector3d(s.ax.x, s.ax.y, s.ax.z),
                                            kernel::Vector3d(s.ex.x, s.ex.y, s.ex.z), d);
  } catch (const std::exception& ex) {
    why = ex.what();
    return std::nullopt;
  }
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
  std::vector<kernel::MeridianEdge> e = ArcSolidMeridian(s);
  std::vector<double> th;
  if (s.torus) th = {thick(s.curved_face), thick(s.flat_face)};
  else th = {thick(s.flat_face), thick(s.curved_face), 0.0};
  try {
    return kernel::ShellRevolvedArcProfile(e, kernel::Point3d(s.o.x, s.o.y, s.o.z), kernel::Vector3d(s.ax.x, s.ax.y, s.ax.z),
                                           kernel::Vector3d(s.ex.x, s.ex.y, s.ex.z), th);
  } catch (const std::exception& ex) {
    why = ex.what();
    return std::nullopt;
  }
}

// ---------------------------------------------------------------------------------------------------------------------------
// A general SOLID OF REVOLUTION recognised from an ON_Brep whose faces are planes (perpendicular to the axis), coaxial cylinders,
// cones and spheres - a stepped shaft (two coaxial cylinders united), a capsule, a funnel, a cone on a cylinder - as its closed
// meridian: `edges` (kernel::MeridianEdge, r along `ex`, z along `ax` from `o`) and `face[i]`, the brep face of edge i (-1 for the
// closing edge along the axis). Every circular edge of the solid is a full circle about the axis; each face joins two such circles
// (or one circle and an axis point: a disc's centre, a cone's apex, a sphere's pole), and the faces chain into ONE path from an axis
// point to an axis point (or one closed cycle for a ring section). A sphere face with a single circle has an ambiguous pole; both
// are tried and the meridian whose Green volume matches the solid's own Brep::Volume() is kept, so a recognition that does not
// reproduce the solid is dropped rather than guessed. Tori are not chained (use RecognizeArcSolid for a torus segment).
struct RevMeridian {
  ON_3dPoint o;
  ON_3dVector ax, ex;
  std::vector<kernel::MeridianEdge> edges;
  std::vector<int> face;
};

inline double RevMeridianGreenVolume(const std::vector<kernel::MeridianEdge>& e) {
  // pi * closed-integral of r^2 dz over the (r, z) boundary, lines exact and arcs sampled finely (used only to MATCH volumes).
  const size_t n = e.size();
  double g = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const kernel::MeridianEdge &a = e[i], &b = e[(i + 1) % n];
    if (!a.arc) {
      g += (a.r * a.r + a.r * b.r + b.r * b.r) / 3.0 * (b.z - a.z);
    } else {
      const double rad = std::hypot(a.r - a.cr, a.z - a.cz);
      const double t0 = std::atan2(a.z - a.cz, a.r - a.cr), t1 = std::atan2(b.z - a.cz, b.r - a.cr);
      double s = a.counter_clockwise ? t1 - t0 : t0 - t1;
      while (s <= 0.0) s += 2.0 * ON_PI;
      if (!a.counter_clockwise) s = -s;
      const int steps = 2000;
      for (int k = 0; k < steps; ++k) {
        const double ta = t0 + s * k / steps, tb = t0 + s * (k + 1) / steps;
        const double ra = a.cr + rad * std::cos(ta), za = a.cz + rad * std::sin(ta), rb = a.cr + rad * std::cos(tb), zb = a.cz + rad * std::sin(tb);
        g += (ra * ra + ra * rb + rb * rb) / 3.0 * (zb - za);
      }
    }
  }
  return ON_PI * std::fabs(g);
}

inline bool RecognizeRevolvedChain(const ON_Brep& b, RevMeridian& out) {
  const int nf = b.m_F.Count();
  if (!b.IsSolid() || nf < 2 || nf > 16) return false;
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double diag = std::max(1e-9, bb.Diagonal().Length());
  const double tol = diag * 1e-6;
  // Axis: a coaxial cylinder's own axis, else (a sphere + a plane) the plane's normal through the sphere's centre.
  struct Circ { double r, z; };
  ON_3dPoint o;
  ON_3dVector ax;
  bool have_axis = false;
  for (int f = 0; f < nf && !have_axis; ++f) {
    const ON_Surface* s = b.m_F[f].SurfaceOf();
    ON_Cylinder cyl;
    if (s && s->IsCylinder(&cyl, tol)) {
      o = cyl.circle.plane.origin;
      ax = cyl.Axis();
      if (!ax.Unitize()) return false;
      have_axis = true;
    }
  }
  for (int f = 0; f < nf && !have_axis; ++f) {
    const ON_Surface* s = b.m_F[f].SurfaceOf();
    ON_Sphere sph;
    if (!s || !s->IsSphere(&sph, tol)) continue;
    for (int g = 0; g < nf; ++g) {
      ON_Plane pl;
      const ON_Surface* sg = b.m_F[g].SurfaceOf();
      if (sg && sg->IsPlanar(&pl, tol)) {
        o = sph.Center();
        ax = pl.zaxis;
        if (!ax.Unitize()) return false;
        have_axis = true;
        break;
      }
    }
  }
  if (!have_axis) return false;
  // Circles (r, z) = the clusters of each face's boundary vertices (a pole / apex is a vertex with r = 0).
  std::vector<Circ> circles;
  std::vector<std::vector<int>> face_circles(static_cast<size_t>(nf));
  const double kv = std::max(tol * 10, diag * 2e-3);
  for (int f = 0; f < nf; ++f) {
    for (int li = 0; li < b.m_F[f].m_li.Count(); ++li) {
      const ON_BrepLoop& lp = b.m_L[b.m_F[f].m_li[li]];
      for (int ti = 0; ti < lp.m_ti.Count(); ++ti) {
        const ON_BrepTrim& t = b.m_T[lp.m_ti[ti]];
        for (int k = 0; k < 2; ++k) {
          const int vi = t.m_vi[k];
          if (vi < 0) continue;
          const ON_3dVector d = b.m_V[vi].point - o;
          const double z = d * ax;
          double r = (d - z * ax).Length();
          if (r < kv) r = 0.0;
          int idx = -1;
          for (size_t c = 0; c < circles.size(); ++c) if (std::fabs(circles[c].r - r) <= kv && std::fabs(circles[c].z - z) <= kv) { idx = static_cast<int>(c); break; }
          if (idx < 0) { circles.push_back({r, z}); idx = static_cast<int>(circles.size() - 1); }
          std::vector<int>& fc = face_circles[static_cast<size_t>(f)];
          if (std::find(fc.begin(), fc.end(), idx) == fc.end()) fc.push_back(idx);
        }
      }
    }
  }
  if (circles.empty()) return false;

  // Per face: its type and the circles it touches.
  enum class Kind { Plane, Cylinder, Cone, Sphere };
  struct FaceInfo { Kind kind; std::vector<int> circ; double zc = 0; double rad = 0; double apex = 0; bool apex_ok = false; };
  std::vector<FaceInfo> fi(static_cast<size_t>(nf));
  for (int f = 0; f < nf; ++f) {
    const ON_Surface* s = b.m_F[f].SurfaceOf();
    if (!s) return false;
    ON_Plane pl;
    ON_Cylinder cyl;
    ON_Sphere sph;
    FaceInfo& info = fi[static_cast<size_t>(f)];
    if (s->IsPlanar(&pl, tol)) {
      if (std::fabs(pl.zaxis * ax) < 1.0 - 1e-9) return false;
      info.kind = Kind::Plane;
      info.zc = (pl.origin - o) * ax;
    } else if (s->IsCylinder(&cyl, tol)) {
      ON_3dVector ca = cyl.Axis();
      if (!ca.Unitize() || std::fabs(ca * ax) < 1.0 - 1e-9) return false;
      const ON_3dVector d = cyl.circle.plane.origin - o;
      if ((d - (d * ax) * ax).Length() > tol * 10) return false;
      info.kind = Kind::Cylinder;
      info.rad = cyl.circle.radius;
    } else if (s->IsSphere(&sph, tol)) {
      const ON_3dVector d = sph.Center() - o;
      if ((d - (d * ax) * ax).Length() > tol * 10) return false;
      info.kind = Kind::Sphere;
      info.zc = d * ax;
      info.rad = sph.Radius();
    } else {
      // cone: the distance from the axis is linear in the height along it
      const ON_Interval du = s->Domain(0), dv = s->Domain(1);
      double sh = 0, sr = 0, shh = 0, shr = 0;
      int cnt = 0;
      std::vector<std::pair<double, double>> hr;
      for (int i = 1; i <= 4; ++i) {
        for (int j = 1; j <= 4; ++j) {
          const ON_3dVector d = s->PointAt(du.ParameterAt(i / 5.0), dv.ParameterAt(j / 5.0)) - o;
          const double h = d * ax, r = (d - h * ax).Length();
          hr.emplace_back(h, r);
          sh += h; sr += r; shh += h * h; shr += h * r; ++cnt;
        }
      }
      const double den = cnt * shh - sh * sh;
      if (std::fabs(den) < 1e-12) return false;
      const double bs = (cnt * shr - sh * sr) / den, as = (sr - bs * sh) / cnt;
      for (const auto& q : hr) if (std::fabs(as + bs * q.first - q.second) > diag * 1e-5) return false;
      if (std::fabs(bs) < 1e-4) return false;
      info.kind = Kind::Cone;
      info.apex = -as / bs;
      info.apex_ok = true;
    }
  }
  for (int f = 0; f < nf; ++f) fi[static_cast<size_t>(f)].circ = face_circles[static_cast<size_t>(f)];
  // A seam or keyhole leaves extra boundary vertices part-way along a face (a cylinder's mid-height seam vertex): keep the two EXTREME
  // circles of a face (lowest / highest z, or smallest / largest r for a plane); the volume match below rejects a wrong reduction.
  for (FaceInfo& info : fi) {
    if (info.circ.empty()) return false;
    while (info.circ.size() > 2) {
      auto key = [&](int c) { return info.kind == Kind::Plane ? circles[static_cast<size_t>(c)].r : circles[static_cast<size_t>(c)].z; };
      size_t lo = 0, hi = 0;
      for (size_t i = 1; i < info.circ.size(); ++i) {
        if (key(info.circ[i]) < key(info.circ[lo])) lo = i;
        if (key(info.circ[i]) > key(info.circ[hi])) hi = i;
      }
      std::vector<int> keep = {info.circ[lo], info.circ[hi]};
      info.circ = keep;
    }
  }

  // Points of the chain: circles (r > 0) and axis points keyed by z; try both poles of a one-circle sphere.
  const double kz = tol * 10;
  std::vector<int> amb;  // faces whose pole is ambiguous
  for (int f = 0; f < nf; ++f) if (fi[static_cast<size_t>(f)].kind == Kind::Sphere && fi[static_cast<size_t>(f)].circ.size() == 1) amb.push_back(f);
  if (amb.size() > 2) return false;
  const double target = [&] {
    kernel::Brep k;
    k.raw() = b;
    try { return k.Volume(); } catch (const std::exception&) {}
    try { return k.VolumeOfSealedSolid(); } catch (const std::exception&) {}
    return -1.0;
  }();
  for (unsigned mask = 0; mask < (1u << amb.size()); ++mask) {
    struct Pt { double r, z; };
    std::vector<Pt> pts;
    auto point_index = [&](double r, double zz) {
      for (size_t i = 0; i < pts.size(); ++i) if (std::fabs(pts[i].r - r) <= kz && std::fabs(pts[i].z - zz) <= kz) return static_cast<int>(i);
      pts.push_back({r, zz});
      return static_cast<int>(pts.size() - 1);
    };
    struct FE { int f, a, c; };  // face, point a, point c
    std::vector<FE> fe;
    bool ok = true;
    for (int f = 0; f < nf && ok; ++f) {
      const FaceInfo& info = fi[static_cast<size_t>(f)];
      std::vector<int> ends;
      for (int ci : info.circ) ends.push_back(point_index(circles[static_cast<size_t>(ci)].r, circles[static_cast<size_t>(ci)].z));
      if (ends.size() == 1) {
        double zz = 0;
        if (info.kind == Kind::Plane) zz = circles[static_cast<size_t>(info.circ[0])].z;
        else if (info.kind == Kind::Cone) { if (!info.apex_ok) { ok = false; break; } zz = info.apex; }
        else if (info.kind == Kind::Sphere) {
          size_t which = 0;
          for (size_t q = 0; q < amb.size(); ++q) if (amb[q] == f) which = q;
          zz = info.zc + (((mask >> which) & 1u) ? info.rad : -info.rad);
        } else { ok = false; break; }
        ends.push_back(point_index(0.0, zz));
      }
      fe.push_back({f, ends[0], ends[1]});
    }
    if (!ok) continue;
    // degrees: every circle point has degree 2, axis points degree 1 (path) - or all degree 2 (ring)
    std::vector<int> deg(pts.size(), 0);
    for (const FE& e : fe) { ++deg[static_cast<size_t>(e.a)]; ++deg[static_cast<size_t>(e.c)]; }
    int start = -1, ones = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
      if (deg[i] > 2) { ok = false; break; }
      if (deg[i] == 1) { ++ones; if (pts[i].r > kz) ok = false; if (start < 0) start = static_cast<int>(i); }
    }
    if (!ok || !(ones == 0 || ones == 2)) continue;
    const bool path = ones == 2;
    if (!path) start = 0;
    // walk
    std::vector<kernel::MeridianEdge> edges;
    std::vector<int> faces;
    std::vector<bool> used(fe.size(), false);
    int cur = start;
    for (size_t step = 0; step < fe.size(); ++step) {
      int pick = -1;
      for (size_t i = 0; i < fe.size(); ++i) if (!used[i] && (fe[i].a == cur || fe[i].c == cur)) { pick = static_cast<int>(i); break; }
      if (pick < 0) { ok = false; break; }
      used[static_cast<size_t>(pick)] = true;
      const int nxt = fe[static_cast<size_t>(pick)].a == cur ? fe[static_cast<size_t>(pick)].c : fe[static_cast<size_t>(pick)].a;
      const FaceInfo& info = fi[static_cast<size_t>(fe[static_cast<size_t>(pick)].f)];
      kernel::MeridianEdge m;
      m.r = pts[static_cast<size_t>(cur)].r;
      m.z = pts[static_cast<size_t>(cur)].z;
      if (info.kind == Kind::Sphere) {
        m.arc = true;
        m.cr = 0.0;
        m.cz = info.zc;
        const double ar = m.r, az = m.z - info.zc, br = pts[static_cast<size_t>(nxt)].r, bz = pts[static_cast<size_t>(nxt)].z - info.zc;
        m.counter_clockwise = ar * bz - az * br > 0.0;  // arcs of a sphere zone sweep less than pi
        if (std::fabs(std::hypot(ar, az) - info.rad) > tol * 100 || std::fabs(std::hypot(br, bz) - info.rad) > tol * 100) { ok = false; break; }
      }
      edges.push_back(m);
      faces.push_back(fe[static_cast<size_t>(pick)].f);
      cur = nxt;
    }
    if (!ok || edges.size() != fe.size()) continue;
    if (path) {  // closing edge along the axis from the last point back to the start
      kernel::MeridianEdge m;
      m.r = pts[static_cast<size_t>(cur)].r;
      m.z = pts[static_cast<size_t>(cur)].z;
      edges.push_back(m);
      faces.push_back(-1);
    } else if (cur != start) {
      continue;
    }
    if (target > 0.0) {
      const double g = RevMeridianGreenVolume(edges);
      if (std::fabs(g - target) > 1e-3 * target) continue;
    } else if (!amb.empty()) {
      continue;  // cannot resolve an ambiguous pole without the solid's own volume
    }
    out.o = o;
    out.ax = ax;
    ON_3dVector ex = std::fabs(ax.z) < 0.9 ? ON_CrossProduct(ON_3dVector(0, 0, 1), ax) : ON_CrossProduct(ON_3dVector(1, 0, 0), ax);
    ex.Unitize();
    out.ex = ex;
    out.edges = edges;
    out.face = faces;
    return true;
  }
  return false;
}

// Recognised sphere cap / torus segment as a RevMeridian (so the generic routes below also serve it).
inline RevMeridian ArcSolidAsMeridian(const ArcSolid& s) {
  RevMeridian m;
  m.o = s.o;
  m.ax = s.ax;
  m.ex = s.ex;
  m.edges = ArcSolidMeridian(s);
  m.face = s.torus ? std::vector<int>{s.curved_face, s.flat_face} : std::vector<int>{s.flat_face, s.curved_face, -1};
  return m;
}

// Shell WITH face removal and/or per-face thickness through kernel::ShellRevolvedArcProfile; a face absent from the meridian is refused.
inline std::optional<kernel::Brep> ShellRevMeridian(const RevMeridian& m, const std::vector<int>& removed, const std::map<int, double>& overrides,
                                                    double t, std::string& why) {
  auto known = [&](int f) { return std::find(m.face.begin(), m.face.end(), f) != m.face.end(); };
  for (int f : removed) if (!known(f)) { why = "invalid face index"; return std::nullopt; }
  for (const auto& kv : overrides) if (!known(kv.first)) { why = "invalid face index"; return std::nullopt; }
  std::vector<double> th(m.edges.size(), 0.0);
  for (size_t i = 0; i < m.edges.size(); ++i) {
    const int f = m.face[i];
    if (f < 0) continue;
    if (std::find(removed.begin(), removed.end(), f) != removed.end()) th[i] = 0.0;
    else { const auto it = overrides.find(f); th[i] = it != overrides.end() ? it->second : t; }
  }
  try {
    return kernel::ShellRevolvedArcProfile(m.edges, kernel::Point3d(m.o.x, m.o.y, m.o.z), kernel::Vector3d(m.ax.x, m.ax.y, m.ax.z),
                                           kernel::Vector3d(m.ex.x, m.ex.y, m.ex.z), th);
  } catch (const std::exception& e) {
    why = e.what();
    return std::nullopt;
  }
}

// Whole-body offset by `d` (positive grows) through kernel::OffsetRevolvedArcProfile.
inline std::optional<kernel::Brep> OffsetRevMeridian(const RevMeridian& m, double d, std::string& why) {
  try {
    return kernel::OffsetRevolvedArcProfile(m.edges, kernel::Point3d(m.o.x, m.o.y, m.o.z), kernel::Vector3d(m.ax.x, m.ax.y, m.ax.z),
                                            kernel::Vector3d(m.ex.x, m.ex.y, m.ex.z), d);
  } catch (const std::exception& e) {
    why = e.what();
    return std::nullopt;
  }
}

}  // namespace dino8::app::revolved
