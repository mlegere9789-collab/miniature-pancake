// Exact rim blends (fillet / chamfer) on a round hole of a right-prism plate whose wall is a STEPPED PROFILE of revolution -
// a countersunk hole (cone + cylinder), a counterbored hole (cylinder in cylinder, with its annular floor), or any chain of
// cylinders, cones and annular steps with a monotone depth (no undercut) - as CountersinkHole / CounterboreHole make them.
// A plate with such a hole is NOT a solid of revolution (its outline is a polygon), so the kernel's revolve-rim blends do
// not apply; the hole itself is a surface of revolution about one axis though, and every vertex of its (radius, depth)
// profile is a circular edge that a fillet / chamfer replaces by an exact torus / cone patch.
//
// Recognition reads the exact FACES (planes, cylinders, cones, the side planes' ring), never the sewn loops, and is
// cross-checked against the solid's own volume. The result is assembled from exact pieces (the extruded outline, the two
// end faces with real inner loops, one revolved patch per profile segment) joined along their shared circles.
#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "commands/prism_solid.h"

namespace dino8::app::prism {

struct PPt { double r = 0, d = 0; };  // radius from the hole axis, depth below the entry face

// One piece of the hole's wall profile in (radius, depth): a straight line or a circular arc (centre cr, cd; radius rad).
struct PSeg {
  bool arc = false;
  double r0 = 0, d0 = 0, r1 = 0, d1 = 0;
  double cr = 0, cd = 0, rad = 0;
};

struct ProfiledHole {
  Prism pr;                  // outer outline (straight edges) and caps; origin on the lower cap, axis n
  V2 hc;                     // hole axis position in the profile plane
  bool from_top = true;      // the entry face is the cap at the top of `pr` (height H)
  bool blind = false;        // ends in a flat floor (last point (0, depth)) instead of the far cap
  double lateral = 0;        // smallest distance from the axis to the outline
  std::vector<PPt> pts;      // entry rim ... last point (through: on the far cap; blind: on the axis at the floor)
};

// Joins loose exact pieces along their shared circles / lines and orients the shell outward.
inline bool JoinPieces(const ON_Brep& all, ON_Brep& out, std::string& why) {
  kernel::Brep res;
  res.raw() = all;
  try {
    res.JoinNakedEdges(1e-6);
    res.UnifyNormals();
  } catch (const std::exception& ex) { why = ex.what(); return false; }
  if (!res.raw().IsSolid() && res.raw().IsManifold() && res.raw().IsValid()) {
    ON_Brep& rb = res.raw();
    const int nf = rb.m_F.Count();
    bool fixed = false;
    for (int pop = 1; pop <= nf && !fixed && nf <= 14; ++pop) {
      for (unsigned mask = 1; mask < (1u << nf) && !fixed; ++mask) {
        int bits = 0;
        for (unsigned m = mask; m; m &= m - 1) ++bits;
        if (bits != pop) continue;
        ON_Brep t = rb;
        for (int i = 0; i < nf; ++i) if (mask & (1u << i)) t.FlipFace(t.m_F[i]);
        if (t.SolidOrientation() != 0) { rb = t; fixed = true; }
      }
    }
    if (fixed && rb.SolidOrientation() < 0) rb.Flip();
  }
  if (!res.raw().IsSolid() || !res.raw().IsValid()) { why = "the assembled pieces did not form a valid closed solid"; return false; }
  out = res.raw();
  return true;
}

// Volume of the region swept by the profile about the axis: pi * integral of r^2 d(depth) (steps contribute nothing).
inline double ProfileRevolvedVolume(const std::vector<PSeg>& segs) {
  double v = 0;
  for (const PSeg& g : segs) {
    const int n = g.arc ? 4000 : 1;
    double pr = g.r0, pd = g.d0;
    const double a0 = g.arc ? std::atan2(g.d0 - g.cd, g.r0 - g.cr) : 0.0;
    double sweep = 0;
    if (g.arc) {
      double a1 = std::atan2(g.d1 - g.cd, g.r1 - g.cr);
      sweep = a1 - a0;
      while (sweep > ON_PI) sweep -= 2 * ON_PI;
      while (sweep < -ON_PI) sweep += 2 * ON_PI;
    }
    for (int k = 1; k <= n; ++k) {
      double qr = g.r1, qd = g.d1;
      if (g.arc && k < n) { qr = g.cr + g.rad * std::cos(a0 + sweep * k / n); qd = g.cd + g.rad * std::sin(a0 + sweep * k / n); }
      v += ON_PI * (qd - pd) * (pr * pr + pr * qr + qr * qr) / 3.0;  // exact for a straight piece (a cone / cylinder)
      pr = qr;
      pd = qd;
    }
  }
  return v;
}

inline std::vector<PSeg> LinesOf(const std::vector<PPt>& pts) {
  std::vector<PSeg> s;
  for (size_t i = 0; i + 1 < pts.size(); ++i) { PSeg g; g.r0 = pts[i].r; g.d0 = pts[i].d; g.r1 = pts[i + 1].r; g.d1 = pts[i + 1].d; s.push_back(g); }
  return s;
}

// The plate with the hole whose wall is `segs` (in order from the entry face). Through: the last piece ends on the far cap.
// Blind: the last piece ends on the axis (the floor disc). Every face is exact (plane, cylinder, cone, torus-like revolve).
inline std::optional<ON_Brep> BuildHoleFromSegs(const Prism& pr, V2 hc, bool from_top, const std::vector<PSeg>& segs, bool blind, std::string& why) {
  const double H = pr.H;
  auto zOf = [&](double depth) { return from_top ? H - depth : depth; };
  auto plane_at = [&](double z) { return ON_Plane(To3(pr, hc, z), pr.X, pr.Y); };
  auto make_face = [&](double z, double hole_r) -> ON_Brep* {
    ON_SimpleArray<ON_Curve*> bnd;
    for (const Seg& g : pr.prof) {
      ON_Curve* c = SegCurve(pr, g, z);
      if (!c) { for (int i = 0; i < bnd.Count(); ++i) delete bnd[i]; return nullptr; }
      bnd.Append(c);
    }
    ON_Plane pl(To3(pr, V2{0, 0}, z), pr.X, pr.Y);
    ON_Brep* f = ON_BrepTrimmedPlane(pl, bnd, false);
    if (!f) { for (int i = 0; i < bnd.Count(); ++i) delete bnd[i]; return nullptr; }
    if (!(hole_r > 0)) { f->Compact(); return f; }
    ON_ArcCurve* hcv = new ON_ArcCurve(ON_Circle(plane_at(z), hole_r));
    double outer_area = 0;
    if (const ON_BrepLoop* ol = f->m_F[0].OuterLoop()) {
      for (int ti = 0; ti < ol->TrimCount(); ++ti) {
        const ON_BrepTrim* t = ol->Trim(ti);
        ON_3dPoint a = t->Edge()->PointAtStart(), c = t->Edge()->PointAtEnd();
        if (t->m_bRev3d) std::swap(a, c);
        outer_area += ON_CrossProduct(a - pl.origin, c - pl.origin) * pr.n;
      }
    }
    if (outer_area > 0) hcv->Reverse();
    ON_SimpleArray<ON_Curve*> loop;
    loop.Append(hcv);
    if (!f->NewPlanarFaceLoop(0, ON_BrepLoop::inner, loop, false)) { delete hcv; delete f; return nullptr; }
    f->Compact();
    return f;
  };
  auto make_annulus = [&](double z, double ro, double ri) -> ON_Brep* {
    const ON_Plane pl = plane_at(z);
    ON_Brep* f = ON_BrepTrimmedPlane(pl, ON_ArcCurve(ON_Circle(pl, ro)));
    if (!f) return nullptr;
    if (!(ri > 0)) return f;  // a disc (the floor of a blind hole)
    ON_ArcCurve* hcv = new ON_ArcCurve(ON_Circle(pl, ri));
    hcv->Reverse();
    ON_SimpleArray<ON_Curve*> loop;
    loop.Append(hcv);
    if (!f->NewPlanarFaceLoop(0, ON_BrepLoop::inner, loop, false)) { delete hcv; delete f; return nullptr; }
    f->Compact();
    return f;
  };
  auto revolve = [&](ON_Curve* crv) -> ON_Brep* {
    ON_RevSurface* rev = new ON_RevSurface();
    rev->m_curve = crv;
    rev->m_axis = ON_Line(To3(pr, hc, 0), To3(pr, hc, 0) + pr.n);
    rev->m_angle = ON_Interval(0.0, 2.0 * ON_PI);
    rev->m_t = rev->m_angle;
    rev->m_bTransposed = false;
    return ON_BrepRevSurface(rev, false, false);
  };
  if (segs.empty()) { why = "empty hole profile"; return std::nullopt; }
  ON_Brep* base = make_face(0, 0);
  if (!base) { why = "could not build the plate's end face"; return std::nullopt; }
  {
    ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + H * pr.n));
    if (ON_BrepExtrudeFace(*base, 0, path, true) < 0) { delete base; why = "could not extrude the plate"; return std::nullopt; }
  }
  kernel::Brep k;
  k.raw() = *base;
  delete base;
  std::vector<int> drop;
  for (int f = 0; f < k.raw().m_F.Count(); ++f) {
    ON_Plane pl;
    if (k.raw().m_F[f].SurfaceOf()->IsPlanar(&pl, 1e-7) && std::fabs(pl.zaxis * pr.n) > 1.0 - 1e-9) drop.push_back(f);
  }
  std::sort(drop.rbegin(), drop.rend());
  try { for (int f : drop) k.DeleteFace(f, false); } catch (const std::exception& ex) { why = ex.what(); return std::nullopt; }
  ON_Brep all = k.raw();
  auto add = [&](ON_Brep* p) -> bool {
    if (!p) return false;
    all.Append(*p);
    delete p;
    return true;
  };
  const double r_first = segs.front().r0, r_last = segs.back().r1;
  if (!add(make_face(zOf(0), r_first)) || !add(make_face(zOf(blind ? H : segs.back().d1), blind ? 0.0 : r_last))) { why = "could not build the end faces with their holes"; return std::nullopt; }
  const double eps = 1e-12;
  for (const PSeg& g : segs) {
    const double za = zOf(g.d0), zb = zOf(g.d1);
    if (g.arc) {
      const double a0 = std::atan2(g.d0 - g.cd, g.r0 - g.cr);
      double sweep = std::atan2(g.d1 - g.cd, g.r1 - g.cr) - a0;
      while (sweep > ON_PI) sweep -= 2 * ON_PI;
      while (sweep < -ON_PI) sweep += 2 * ON_PI;
      const double am = a0 + 0.5 * sweep;
      const double mr = g.cr + g.rad * std::cos(am), md = g.cd + g.rad * std::sin(am);
      ON_Arc arc(To3(pr, V2{hc.x + g.r0, hc.y}, za), To3(pr, V2{hc.x + mr, hc.y}, zOf(md)), To3(pr, V2{hc.x + g.r1, hc.y}, zb));
      if (!arc.IsValid() || !add(revolve(new ON_ArcCurve(arc)))) { why = "could not build a blend patch"; return std::nullopt; }
    } else if (std::fabs(g.r0 - g.r1) < eps) {
      ON_Cylinder cyl(ON_Circle(ON_Plane(To3(pr, hc, std::min(za, zb)), pr.X, pr.Y), g.r0), std::fabs(zb - za));
      if (!add(ON_BrepCylinder(cyl, false, false))) { why = "could not build a cylindrical wall"; return std::nullopt; }
    } else if (std::fabs(g.d0 - g.d1) < eps) {
      if (!add(make_annulus(za, std::max(g.r0, g.r1), std::min(g.r0, g.r1)))) { why = "could not build an annular step"; return std::nullopt; }
    } else {
      if (!add(revolve(new ON_LineCurve(To3(pr, V2{hc.x + g.r0, hc.y}, za), To3(pr, V2{hc.x + g.r1, hc.y}, zb))))) { why = "could not build the conical wall"; return std::nullopt; }
    }
  }
  ON_Brep out;
  if (!JoinPieces(all, out, why)) return std::nullopt;
  return out;
}

// Reads a plate with ONE round hole whose wall is a stepped profile of cylinders, cones and annular steps (see the top of the
// file). `why` carries the reason when the solid is not that.
inline bool RecognizeProfiledHole(const ON_Brep& b, ProfiledHole& ph, std::string& why) {
  if (!b.IsSolid()) { why = "not a closed solid"; return false; }
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double diag = std::max(1e-9, bb.Diagonal().Length());
  const double tol = std::max(1e-6, diag * 1e-6);
  // 1. The axis, from the first curved face.
  bool have_axis = false;
  ON_3dPoint ao;
  ON_3dVector ax;
  for (int f = 0; f < b.m_F.Count() && !have_axis; ++f) {
    const ON_Surface* s = b.m_F[f].SurfaceOf();
    if (!s) { why = "face without a surface"; return false; }
    ON_Plane pl;
    if (s->IsPlanar(&pl, tol)) continue;
    ON_Cylinder c;
    ON_Cone cn;
    if (s->IsCylinder(&c, tol)) { ao = c.circle.plane.origin; ax = c.Axis(); have_axis = true; }
    else if (s->IsCone(&cn, tol)) { ao = cn.plane.origin; ax = cn.plane.zaxis; have_axis = true; }
    else if (const ON_RevSurface* rv = dynamic_cast<const ON_RevSurface*>(s)) {
      // a revolved line is a cone / cylinder; a revolved arc is a blend an earlier operation put on a rim (refused below)
      if (!rv->m_curve || !rv->m_curve->IsLinear(tol)) { why = "a curved blend patch is already on the hole (one blend per rim set is supported; it is not re-read)"; return false; }
      ao = rv->m_axis.from;
      ax = rv->m_axis.Direction();
      have_axis = true;
    } else { why = "a face is neither planar, cylindrical nor conical"; return false; }
  }
  if (!have_axis || !ax.Unitize()) { why = "no round hole wall"; return false; }
  auto hOf = [&](const ON_3dPoint& p) { return (p - ao) * ax; };
  auto rOf = [&](const ON_3dPoint& p) { ON_3dVector v = p - ao; v -= (v * ax) * ax; return v.Length(); };
  // 2. Faces: wall pieces, planes perpendicular to the axis (end caps, annular steps, floor), side planes.
  struct Piece { double r0, h0, r1, h1; };
  std::vector<Piece> pieces;
  std::vector<int> cap_faces, sides;
  for (int f = 0; f < b.m_F.Count(); ++f) {
    const ON_Surface* s = b.m_F[f].SurfaceOf();
    ON_Plane pl;
    if (s->IsPlanar(&pl, tol)) {
      const double dn = std::fabs(pl.zaxis * ax);
      if (dn < 1e-9) { sides.push_back(f); continue; }
      if (dn < 1.0 - 1e-9) { why = "a planar face is neither perpendicular nor parallel to the hole axis"; return false; }
      // outer boundary: a single circle about the axis -> annular step / floor disc; otherwise a plate cap
      const ON_BrepLoop* ol = b.m_F[f].OuterLoop();
      bool circ = ol && ol->TrimCount() == 1;
      if (circ) {
        const ON_BrepEdge* e = ol->Trim(0)->Edge();
        ON_Arc arc;
        circ = e && e->IsArc(nullptr, &arc, tol) && arc.IsCircle();
        if (circ && e) {
          const double ro = arc.Radius(), h = hOf(arc.Center());
          if (rOf(arc.Center()) > 10 * tol) { why = "a circular face is not centred on the hole axis"; return false; }
          double ri = 0;
          bool single_inner = true;
          int inner = 0;
          for (int li = 0; li < b.m_F[f].LoopCount(); ++li) {
            const ON_BrepLoop* lp = b.Loop(b.m_F[f].m_li[li]);
            if (!lp || lp->m_type != ON_BrepLoop::inner) continue;
            ++inner;
            if (lp->TrimCount() != 1) { single_inner = false; break; }
            ON_Arc ia;
            const ON_BrepEdge* ie = lp->Trim(0)->Edge();
            if (!ie || !ie->IsArc(nullptr, &ia, tol)) { single_inner = false; break; }
            ri = ia.Radius();
          }
          if (!single_inner || inner > 1) { why = "an annular step with more than one inner boundary"; return false; }
          pieces.push_back({ro, h, ri, h});
          continue;
        }
      }
      cap_faces.push_back(f);
    } else {
      // wall piece: the four parameter corners give the two end circles of the generator (a line in radius / height)
      const ON_Interval du = s->Domain(0), dv = s->Domain(1);
      std::vector<std::pair<double, double>> rh;
      for (int iu = 0; iu < 2; ++iu) for (int iv = 0; iv < 2; ++iv) {
        const ON_3dPoint p = s->PointAt(du.ParameterAt(iu), dv.ParameterAt(iv));
        const std::pair<double, double> q{rOf(p), hOf(p)};
        bool dup = false;
        for (const auto& e : rh) if (std::fabs(e.first - q.first) < 10 * tol && std::fabs(e.second - q.second) < 10 * tol) dup = true;
        if (!dup) rh.push_back(q);
      }
      if (rh.size() != 2) { why = "a curved face is not a cone / cylinder piece about the hole axis"; return false; }
      for (int iu = 0; iu <= 4; ++iu) for (int iv = 0; iv <= 4; ++iv) {
        const ON_3dPoint p = s->PointAt(du.ParameterAt(iu / 4.0), dv.ParameterAt(iv / 4.0));
        const double dh = rh[1].second - rh[0].second, dr = rh[1].first - rh[0].first;
        const double len = std::hypot(dh, dr);
        // distance from (r, h) to the generator segment must be ~0 (a straight-line revolve)
        const double t = len > 0 ? ((rOf(p) - rh[0].first) * dr + (hOf(p) - rh[0].second) * dh) / (len * len) : 0.0;
        if (t < -1e-6 || t > 1 + 1e-6 || std::fabs((rOf(p) - rh[0].first) * dh - (hOf(p) - rh[0].second) * dr) / std::max(len, 1e-12) > 100 * tol) { why = "a curved face is not a straight-generator surface of revolution"; return false; }
      }
      pieces.push_back({rh[0].first, rh[0].second, rh[1].first, rh[1].second});
    }
  }
  if (cap_faces.size() != 2 || sides.size() < 3) { why = "not a prism with two plane caps, at least three side faces and one round hole"; return false; }
  if (pieces.empty()) { why = "no hole wall"; return false; }
  // 3. The caps, their heights and inner loops (the hole's rim circle on each).
  struct Cap { int f; double h; double ri; bool has; };
  Cap cap[2];
  for (int k = 0; k < 2; ++k) {
    ON_Plane pl;
    b.m_F[cap_faces[static_cast<size_t>(k)]].SurfaceOf()->IsPlanar(&pl, tol);
    cap[k] = {cap_faces[static_cast<size_t>(k)], (pl.origin - ao) * ax, 0, false};
    int inner = 0;
    for (int li = 0; li < b.m_F[cap[k].f].LoopCount(); ++li) {
      const ON_BrepLoop* lp = b.Loop(b.m_F[cap[k].f].m_li[li]);
      if (!lp || lp->m_type != ON_BrepLoop::inner) continue;
      ++inner;
      ON_Arc ia;
      const ON_BrepEdge* ie = lp->TrimCount() == 1 ? lp->Trim(0)->Edge() : nullptr;
      if (!ie || !ie->IsArc(nullptr, &ia, tol) || !ia.IsCircle()) { why = "a plate cap has an inner boundary that is not one circle"; return false; }
      if (rOf(ia.Center()) > 10 * tol) { why = "the hole is not centred on the axis"; return false; }
      cap[k].ri = ia.Radius();
      cap[k].has = true;
    }
    if (inner > 1) { why = "a plate cap has more than one hole"; return false; }
  }
  if (cap[0].h > cap[1].h) std::swap(cap[0], cap[1]);
  const double h0 = cap[0].h, h1 = cap[1].h;
  if (!(h1 - h0 > tol)) { why = "degenerate plate thickness"; return false; }
  const bool blind = !(cap[0].has && cap[1].has);
  if (!cap[0].has && !cap[1].has) { why = "no hole reaches a plate cap"; return false; }
  const bool entry_top = blind ? cap[1].has : true;  // through: enter from the upper cap (larger h along the axis)
  const Cap& entry = entry_top ? cap[1] : cap[0];
  // 4. Chain the pieces from the entry rim circle (r_e, entry.h).
  std::vector<bool> used(pieces.size(), false);
  std::vector<PPt> pts;
  double cr = entry.ri, ch = entry.h;
  pts.push_back({cr, 0});
  for (size_t guard = 0; guard <= pieces.size(); ++guard) {
    int pick = -1;
    bool flip = false;
    for (size_t i = 0; i < pieces.size(); ++i) {
      if (used[i]) continue;
      if (std::fabs(pieces[i].r0 - cr) < 10 * tol && std::fabs(pieces[i].h0 - ch) < 10 * tol) { pick = static_cast<int>(i); flip = false; break; }
      if (std::fabs(pieces[i].r1 - cr) < 10 * tol && std::fabs(pieces[i].h1 - ch) < 10 * tol) { pick = static_cast<int>(i); flip = true; break; }
    }
    if (pick < 0) break;
    used[static_cast<size_t>(pick)] = true;
    const Piece& p = pieces[static_cast<size_t>(pick)];
    cr = flip ? p.r0 : p.r1;
    ch = flip ? p.h0 : p.h1;
    pts.push_back({cr, std::fabs(ch - entry.h)});
  }
  for (bool u : used) if (!u) { why = "the hole's wall faces do not form one chain from the plate face (a second hole or an undercut)"; return false; }
  // 5. The chain must end at the far cap's rim (through) or on the axis at the floor (blind), and be monotone in depth.
  if (blind) {
    if (!(pts.back().r < 10 * tol)) { why = "a blind hole whose floor is not a disc on the axis"; return false; }
  } else {
    const Cap& far = entry_top ? cap[0] : cap[1];
    if (std::fabs(ch - far.h) > 10 * tol || std::fabs(cr - far.ri) > 10 * tol) { why = "the hole wall does not end on the far cap's rim"; return false; }
  }
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    if (pts[i + 1].d < pts[i].d - 10 * tol) { why = "the hole profile has an undercut"; return false; }
  }
  // 6. The outer outline: the side planes' ring, intersected with the lower cap plane.
  std::map<int, std::set<int>> adj;
  for (int ei = 0; ei < b.m_E.Count(); ++ei) {
    const ON_BrepEdge& ee = b.m_E[ei];
    if (ee.m_edge_index < 0 || ee.TrimCount() != 2) continue;
    const int x = b.m_T[ee.m_ti[0]].FaceIndexOf(), y = b.m_T[ee.m_ti[1]].FaceIndexOf();
    if (x == y || std::find(sides.begin(), sides.end(), x) == sides.end() || std::find(sides.begin(), sides.end(), y) == sides.end()) continue;
    adj[x].insert(y);
    adj[y].insert(x);
  }
  for (int sfi : sides) if (adj[sfi].size() != 2) { why = "the side faces do not form one ring of flat walls"; return false; }
  std::vector<int> ring{sides[0]};
  int prev = -1, cur = sides[0];
  while (true) {
    int nxt = -1;
    for (int cand : adj[cur]) if (cand != prev) { nxt = cand; break; }
    if (nxt < 0 || nxt == sides[0]) break;
    if (ring.size() > sides.size()) { why = "the side faces do not close a ring"; return false; }
    ring.push_back(nxt);
    prev = cur;
    cur = nxt;
  }
  if (ring.size() != sides.size()) { why = "the side faces do not close one ring"; return false; }
  const ON_3dPoint c0 = ao + h0 * ax;
  const ON_Plane lower_plane(c0, ax);
  std::vector<ON_3dPoint> poly3;
  for (size_t i = 0; i < ring.size(); ++i) {
    ON_Plane pa, pb;
    b.m_F[ring[i]].SurfaceOf()->IsPlanar(&pa, tol);
    b.m_F[ring[(i + 1) % ring.size()]].SurfaceOf()->IsPlanar(&pb, tol);
    ON_3dPoint corner;
    if (!ON_Intersect(pa, pb, lower_plane, corner)) { why = "adjacent side faces do not meet in a corner"; return false; }
    poly3.push_back(corner);
  }
  ON_3dVector X(1, 0, 0);
  X = X - (X * ax) * ax;
  if (!X.Unitize()) { X = ON_3dVector(0, 1, 0); X = X - (X * ax) * ax; X.Unitize(); }
  ph.pr.n = ax;
  ph.pr.O = c0;
  ph.pr.X = X;
  ph.pr.Y = ON_CrossProduct(ax, X);
  ph.pr.H = h1 - h0;
  auto to2 = [&](const ON_3dPoint& p) { const ON_3dVector v = p - ph.pr.O; return V2{v * ph.pr.X, v * ph.pr.Y}; };
  std::vector<V2> poly;
  for (const ON_3dPoint& p : poly3) poly.push_back(to2(p));
  double area = 0;
  for (size_t i = 0; i < poly.size(); ++i) area += 0.5 * Cross(poly[i], poly[(i + 1) % poly.size()]);
  if (area < 0) { std::reverse(poly.begin(), poly.end()); area = -area; }
  ph.pr.prof.clear();
  for (size_t i = 0; i < poly.size(); ++i) { Seg g; g.a = poly[i]; g.b = poly[(i + 1) % poly.size()]; ph.pr.prof.push_back(g); }
  ph.hc = to2(ao - ((ao - c0) * ax) * ax);
  ph.from_top = entry_top;
  ph.blind = blind;
  ph.pts = pts;
  double lat = 1e300;
  for (const Seg& g : ph.pr.prof) lat = std::min(lat, DistToSeg(g, ph.hc));
  ph.lateral = lat;
  double rmax = 0;
  for (const PPt& p : pts) rmax = std::max(rmax, p.r);
  if (!(lat > rmax + 10 * tol)) { why = "the hole reaches the plate's outline"; return false; }
  // 7. Volume cross-check against the closed form.
  BrepMeshOptions opt;
  opt.chord_tolerance = std::max(1e-6, diag * 2e-4);
  double vol = 0;
  try { vol = std::fabs(MeshBrepClosed(b, opt).Volume()); } catch (const std::exception&) { vol = 0; }
  const double expect = area * ph.pr.H - ProfileRevolvedVolume(LinesOf(pts));
  if (!(expect > 0) || !(std::fabs(vol - expect) <= 5e-3 * expect)) { why = "the solid is not a right prism with one stepped round hole (volume check failed)"; return false; }
  return true;
}

// The profile vertex whose circle passes through `p` (a point of the picked rim edge); -1 if none. The blind floor's
// centre point is never a rim.
inline int FindRimVertex(const ProfiledHole& ph, const ON_3dPoint& p, double tol) {
  const ON_3dVector v = p - ph.pr.O;
  const double h = v * ph.pr.n;
  const ON_3dVector rad = v - h * ph.pr.n;
  const double depth = ph.from_top ? ph.pr.H - h : h;
  const V2 c = ph.hc;
  const double r = Len(V2{rad * ph.pr.X, rad * ph.pr.Y} - c);
  int best = -1;
  for (size_t i = 0; i < ph.pts.size(); ++i) {
    if (ph.blind && i + 1 == ph.pts.size()) continue;
    if (std::fabs(ph.pts[i].r - r) <= 20 * tol && std::fabs(ph.pts[i].d - depth) <= 20 * tol) best = static_cast<int>(i);
  }
  return best;
}

// True when the plain "cylindrical hole in a prism" blend path (ClassifyHolePlateRim) already owns this rim: a through-hole
// with exactly ONE cylindrical piece, the rim being where that cylinder meets a plate cap. Every other rim of a stepped hole
// (a cone's rim, a step corner, a counterbore's walls) is handled here.
inline bool PlainHoleOwnsRim(const ProfiledHole& ph, int v) {
  if (ph.blind || ph.pts.size() < 2) return false;
  int cyl = 0;
  for (size_t i = 0; i + 1 < ph.pts.size(); ++i)
    if (std::fabs(ph.pts[i].r - ph.pts[i + 1].r) < 1e-9 && std::fabs(ph.pts[i].d - ph.pts[i + 1].d) > 1e-9) ++cyl;
  if (cyl != 1) return false;
  const size_t last = ph.pts.size() - 1;
  const auto is_cyl = [&](size_t a) { return std::fabs(ph.pts[a].r - ph.pts[a + 1].r) < 1e-9 && std::fabs(ph.pts[a].d - ph.pts[a + 1].d) > 1e-9; };
  return (v == 0 && is_cyl(0)) || (static_cast<size_t>(v) == last && is_cyl(last - 1));
}

struct RimBlendSpec {
  int v = -1;
  bool chamfer = false;
  double d1 = 0, d2 = 0;  // fillet: d1 = d2 = radius. Chamfer: d1 along the wall piece (or the earlier piece), d2 across the planar one (or the later piece)
};

// Rounds / bevels the listed vertices of the hole profile (all at once) and rebuilds the plate. Refuses, with the reason,
// a blend that does not fit on its two pieces, reaches the outline, or crosses the axis.
inline std::optional<ON_Brep> BuildBlendedProfiledHole(const ProfiledHole& ph, const std::vector<RimBlendSpec>& specs, std::vector<PSeg>* segs_out, std::string& why) {
  const size_t N = ph.pts.size();
  const double sc = std::max(ph.pr.H, ph.lateral);
  const double eps = sc * 1e-9;
  struct P2 { double x, y; };  // (radius, -depth): y up, the material on the left of the walking direction
  std::vector<P2> P(N);
  for (size_t i = 0; i < N; ++i) P[i] = {ph.pts[i].r, -ph.pts[i].d};
  auto dir = [&](size_t a, size_t b) { P2 d{P[b].x - P[a].x, P[b].y - P[a].y}; const double l = std::hypot(d.x, d.y); return P2{d.x / l, d.y / l}; };
  auto seglen = [&](size_t a) { return std::hypot(P[a + 1].x - P[a].x, P[a + 1].y - P[a].y); };
  struct Plan { bool on = false; bool chamfer = false; double s1 = 0, s2 = 0, rho = 0; P2 t1{}, t2{}, A{}, B{}, C{}; bool left = false; };
  std::vector<Plan> plan(N);
  for (const RimBlendSpec& sp : specs) {
    if (sp.v < 0 || static_cast<size_t>(sp.v) >= N) { why = "invalid rim"; return std::nullopt; }
    const size_t v = static_cast<size_t>(sp.v);
    if (plan[v].on) { why = "the same rim is blended twice"; return std::nullopt; }
    if (!(sp.d1 > 0 && sp.d2 > 0)) { why = "the blend size must be positive"; return std::nullopt; }
    Plan& q = plan[v];
    q.on = true;
    q.chamfer = sp.chamfer;
    q.t1 = v == 0 ? P2{-1, 0} : dir(v - 1, v);
    q.t2 = (v + 1 < N) ? dir(v, v + 1) : P2{1, 0};
    const bool in_plane = v == 0, out_plane = v + 1 == N;
    const double cr = q.t1.x * q.t2.y - q.t1.y * q.t2.x, dt = q.t1.x * q.t2.x + q.t1.y * q.t2.y;
    const double phi = std::atan2(std::fabs(cr), dt);
    if (!(phi > 1e-6)) { why = "that edge is not a corner (the two faces are tangent)"; return std::nullopt; }
    if (phi > ON_PI - 1e-3) { why = "that edge is a cusp"; return std::nullopt; }
    q.left = cr > 0;
    if (!sp.chamfer) {
      q.rho = sp.d1;
      q.s1 = q.s2 = sp.d1 * std::tan(0.5 * phi);
    } else {
      // d1 on the non-planar piece, d2 on the planar one; two walls: d1 on the earlier piece
      const bool second_planar = in_plane;  // vertex 0: incoming piece is the plane
      const bool first_planar = out_plane;
      if (second_planar) { q.s1 = sp.d2; q.s2 = sp.d1; }
      else if (first_planar) { q.s1 = sp.d1; q.s2 = sp.d2; }
      else {
        const bool step_in = std::fabs(P[v].y - P[v - 1].y) < eps, step_out = std::fabs(P[v + 1].y - P[v].y) < eps;
        if (step_in && !step_out) { q.s1 = sp.d2; q.s2 = sp.d1; }
        else if (step_out && !step_in) { q.s1 = sp.d1; q.s2 = sp.d2; }
        else { q.s1 = sp.d1; q.s2 = sp.d2; }
      }
    }
    q.A = {P[v].x - q.s1 * q.t1.x, P[v].y - q.s1 * q.t1.y};
    q.B = {P[v].x + q.s2 * q.t2.x, P[v].y + q.s2 * q.t2.y};
    if (!sp.chamfer) {
      const P2 nrm = q.left ? P2{-q.t1.y, q.t1.x} : P2{q.t1.y, -q.t1.x};
      q.C = {q.A.x + q.rho * nrm.x, q.A.y + q.rho * nrm.y};
    }
    if (v == 0 && !(q.A.x < ph.lateral - 10 * eps)) { why = "the blend would reach the plate's outline on the entry face"; return std::nullopt; }
    if (out_plane && !ph.blind && !(q.B.x < ph.lateral - 10 * eps)) { why = "the blend would reach the plate's outline on the far face"; return std::nullopt; }
    if (v == 0 && !(q.A.x > eps)) { why = "the blend collapses"; return std::nullopt; }
  }
  // room along each piece between consecutive vertices
  for (size_t i = 0; i + 1 < N; ++i) {
    const double used = (plan[i].on ? plan[i].s2 : 0.0) + (plan[i + 1].on ? plan[i + 1].s1 : 0.0);
    if (used > 0 && !(used < seglen(i) - 10 * eps)) {
      why = "the blend does not fit: it needs " + std::to_string(used) + " along a piece of the hole wall that is only " + std::to_string(seglen(i)) + " long";
      return std::nullopt;
    }
  }
  // the new path
  struct Node { double x, y; };
  std::vector<Node> nodes;
  std::vector<int> kind;  // kind[i] describes the piece from nodes[i] to nodes[i+1]: 0 line, 1 arc (vertex index in karc)
  std::vector<size_t> karc;
  for (size_t v = 0; v < N; ++v) {
    if (!plan[v].on) {
      nodes.push_back({P[v].x, P[v].y});
      if (v + 1 < N) kind.push_back(0);
    } else {
      nodes.push_back({plan[v].A.x, plan[v].A.y});
      kind.push_back(plan[v].chamfer ? 0 : 1);
      karc.push_back(v);
      nodes.push_back({plan[v].B.x, plan[v].B.y});
      if (v + 1 < N) kind.push_back(0);
    }
  }
  std::vector<PSeg> segs;
  size_t ai = 0;
  for (size_t i = 0; i + 1 < nodes.size(); ++i) {
    PSeg g;
    g.r0 = nodes[i].x; g.d0 = -nodes[i].y; g.r1 = nodes[i + 1].x; g.d1 = -nodes[i + 1].y;
    if (kind[i] == 1) {
      const Plan& q = plan[karc[ai++]];
      g.arc = true;
      g.cr = q.C.x; g.cd = -q.C.y; g.rad = q.rho;
      for (int k = 0; k <= 16; ++k) {  // the arc must stay off the axis
        const double a0 = std::atan2(g.d0 - g.cd, g.r0 - g.cr);
        double sw = std::atan2(g.d1 - g.cd, g.r1 - g.cr) - a0;
        while (sw > ON_PI) sw -= 2 * ON_PI;
        while (sw < -ON_PI) sw += 2 * ON_PI;
        if (!(g.cr + g.rad * std::cos(a0 + sw * k / 16.0) > eps)) { why = "the blend would cross the hole axis"; return std::nullopt; }
      }
    }
    if (!(g.r0 > -eps) || !(g.r1 > -eps)) { why = "the blend would cross the hole axis"; return std::nullopt; }
    segs.push_back(g);
  }
  if (segs_out) *segs_out = segs;
  std::optional<ON_Brep> out = BuildHoleFromSegs(ph.pr, ph.hc, ph.from_top, segs, ph.blind, why);
  if (!out) return std::nullopt;
  if (out->m_F.Count() == 0 || !out->IsSolid() || !out->IsValid()) { why = "the blended plate is not a valid closed solid"; return std::nullopt; }
  return out;
}

}  // namespace dino8::app::prism
