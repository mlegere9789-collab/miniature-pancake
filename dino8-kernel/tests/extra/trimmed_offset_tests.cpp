// OffsetTrimmedFace (trimmed_offset.h): offset of ONE trimmed freeform face with its boundary following.
//
// Independent oracles (none of them is the routine's own construction):
//  * the exact parallel locus S(u,v) + d n(u,v) with n from CENTRAL FINITE DIFFERENCES of the original surface's own
//    PointAt (not NormalAt, not the offset code),
//  * an even-odd point-in-region test written here, on densely sampled 2D trim curves of the INPUT / OUTPUT bodies,
//  * a closest-point search onto the ORIGINAL surface: every point of an offset surface/boundary is at distance |d| from
//    it, with its foot at the (u, v) the trim says (parallel-surface property, independent of how the offset was built),
//  * exact closed forms for the analytic (planar) case,
//  * negative controls: each refusal must state its reason and leave the output untouched.
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/trimmed_offset.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::OffsetTrimmedFace;
using dino8::kernel::Result;
using dino8::kernel::TrimmedOffsetReport;
using d8test::Check;

typedef std::vector<ON_2dPoint> Poly;

struct Fixture {
  NurbsSurface s;
  NurbsSurface::PatchFitReport fit;
  Brep face;       // ellipse boundary, one circular hole
  Brep face_nohole;
  double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
  bool ok = false;
};

// A freeform bump: a bicubic least-squares patch through a 11 x 11 grid of z = f(x, y).
Fixture& Fix() {
  static Fixture f;
  static bool built = false;
  if (built) return f;
  built = true;
  std::vector<ON_3dPoint> grid;
  for (int i = 0; i < 11; ++i)
    for (int j = 0; j < 11; ++j) {
      const double x = -4.0 + 0.8 * i, y = -4.0 + 0.8 * j;
      grid.push_back(ON_3dPoint(x, y, 1.2 * std::sin(0.55 * x) * std::cos(0.45 * y) + 0.05 * x * y));
    }
  if (NurbsSurface::FitPatch({}, grid, 1e-4, f.s, &f.fit, 11) != Result::Ok) return f;
  f.u0 = f.s.Domain(0).min; f.u1 = f.s.Domain(0).max; f.v0 = f.s.Domain(1).min; f.v1 = f.s.Domain(1).max;
  const double cu = 0.5 * (f.u0 + f.u1), cv = 0.5 * (f.v0 + f.v1), eu = f.u1 - f.u0, ev = f.v1 - f.v0;
  auto ring = [&](double ccu, double ccv, double ru, double rv, double rot, int n) {
    std::vector<ON_3dPoint> cvs;
    for (int k = 0; k < n; ++k) {
      const double a = 2.0 * ON_PI * k / n;
      const double x = ru * std::cos(a), y = rv * std::sin(a);
      const double u = ccu + x * std::cos(rot) - y * std::sin(rot), v = ccv + x * std::sin(rot) + y * std::cos(rot);
      cvs.push_back(f.s.PointAt(u, v));
    }
    cvs.push_back(cvs.front());
    return NurbsCurve::FromControlPoints(cvs, 3);
  };
  const NurbsCurve outer = ring(cu, cv, 0.36 * eu, 0.28 * ev, 0.4, 24);
  const NurbsCurve hole = ring(cu + 0.05 * eu, cv - 0.02 * ev, 0.08 * eu, 0.08 * ev, 0.0, 12);
  f.face = Brep::TrimmedPatchFace(f.s, f.fit, outer, {hole});
  f.face_nohole = Brep::TrimmedPatchFace(f.s, f.fit, outer);
  f.ok = f.face.FaceCount() == 1 && f.face.raw().IsValid() && f.face_nohole.raw().IsValid();
  return f;
}

// Unit normal of `s` at (u, v) by central differences of PointAt: independent of NormalAt() and of any offset code.
ON_3dVector FdNormal(const NurbsSurface& s, double u, double v) {
  const double h = 1e-5;
  ON_3dVector su = s.PointAt(u + h, v) - s.PointAt(u - h, v);
  ON_3dVector sv = s.PointAt(u, v + h) - s.PointAt(u, v - h);
  ON_3dVector n = ON_CrossProduct(su, sv);
  n.Unitize();
  return n;
}

// The polygon of one loop of `b`, each trim sampled densely in (u, v).
Poly LoopPoly(const ON_Brep& b, int li) {
  Poly p;
  const ON_BrepLoop& loop = b.m_L[li];
  for (int k = 0; k < loop.m_ti.Count(); ++k) {
    const ON_BrepTrim& trim = b.m_T[loop.m_ti[k]];
    for (int i = 0; i < 400; ++i) {
      const ON_3dPoint q = trim.PointAt(trim.Domain().ParameterAt(i / 400.0));
      p.push_back(ON_2dPoint(q.x, q.y));
    }
  }
  return p;
}

bool InPoly(const Poly& poly, double x, double y) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    if (((poly[i].y > y) != (poly[j].y > y)) &&
        (x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)) {
      in = !in;
    }
  }
  return in;
}

// Even-odd over every loop of the body: inside the outer loop and outside every hole.
bool InFace(const ON_Brep& b, const std::vector<Poly>& loops, double x, double y) {
  (void)b;
  int crossings = 0;
  for (const Poly& p : loops) crossings += InPoly(p, x, y) ? 1 : 0;
  return (crossings % 2) == 1;
}

std::vector<Poly> AllLoops(const ON_Brep& b) {
  std::vector<Poly> v;
  for (int li = 0; li < b.m_L.Count(); ++li) v.push_back(LoopPoly(b, li));
  return v;
}

void RunOffsetChecks(const Brep& in, double d, const char* tag) {
  Fixture& fx = Fix();
  const std::string t = std::string(tag) + ": ";
  Brep out;
  TrimmedOffsetReport rep;
  const Result r = OffsetTrimmedFace(in, d, out, 1e-4, &rep);
  Check(r == Result::Ok, (t + "the offset of the trimmed freeform face succeeds (reason: " + rep.refusal_reason + ")").c_str());
  if (r != Result::Ok) return;
  const ON_Brep& ob = out.raw();
  Check(out.FaceCount() == 1 && ob.IsValid() && ob.m_L.Count() == in.raw().m_L.Count() &&
            ob.m_E.Count() == in.raw().m_E.Count() && ob.m_T.Count() == in.raw().m_T.Count(),
        (t + "one face, valid ON_Brep, the same number of loops / edges / trims as the input").c_str());
  Check(!rep.surface_exact && rep.surface_deviation <= rep.tolerance_used && rep.max_edge_deviation <= rep.tolerance_used &&
            rep.tolerance_used == 1e-4,
        (t + "reported honestly as a tolerance-bounded fit (not exact), with surface and edge deviations within the tolerance").c_str());
  Check(rep.edges_rebuilt == in.raw().m_E.Count() && rep.vertices_moved == in.raw().m_V.Count(),
        (t + "every edge was rebuilt and every vertex moved").c_str());

  // 2D trims are unchanged: same control nets.
  bool trims_same = true;
  for (int i = 0; i < ob.m_T.Count() && i < in.raw().m_T.Count(); ++i) {
    for (int k = 0; k <= 50; ++k) {
      const ON_3dPoint a = ob.m_T[i].PointAt(ob.m_T[i].Domain().ParameterAt(k / 50.0));
      const ON_3dPoint b = in.raw().m_T[i].PointAt(in.raw().m_T[i].Domain().ParameterAt(k / 50.0));
      trims_same = trims_same && a.DistanceTo(b) < 1e-12;
    }
  }
  Check(trims_same, (t + "every 2D trim curve is carried over unchanged (the offset face covers the same (u, v) region)").c_str());

  NurbsSurface res;
  res.raw() = *ON_NurbsSurface::Cast(ob.m_S[ob.m_F[0].m_si]);
  const std::vector<Poly> in_loops = AllLoops(in.raw());
  const std::vector<Poly> out_loops = AllLoops(ob);

  // (1) Interior: sample (u, v) inside / outside the input region; compare P + d n with the offset result.
  int n_in = 0, n_out = 0, bad_param = 0, bad_dist = 0, bad_foot_in = 0, bad_foot_out = 0;
  double worst_param = 0.0, worst_dist = 0.0;
  for (int i = 0; i < 40; ++i) {
    for (int j = 0; j < 40; ++j) {
      const double u = fx.u0 + (fx.u1 - fx.u0) * (0.04 + 0.92 * i / 39.0);
      const double v = fx.v0 + (fx.v1 - fx.v0) * (0.04 + 0.92 * j / 39.0);
      const bool inside = InFace(in.raw(), in_loops, u, v);
      const ON_3dPoint p = fx.s.PointAt(u, v);
      const ON_3dVector n = FdNormal(fx.s, u, v);
      const ON_3dPoint q = p + d * n;  // the offset sample
      const double by_param = q.DistanceTo(res.PointAt(u, v));
      worst_param = std::max(worst_param, by_param);
      if (by_param > 2e-4) ++bad_param;
      const ON_2dPoint foot = ON_2dPoint(res.ClosestPointParameter(q).x, res.ClosestPointParameter(q).y);
      const double dist = q.DistanceTo(res.PointAt(foot.x, foot.y));
      worst_dist = std::max(worst_dist, dist);
      if (dist > 2e-4) ++bad_dist;
      // The closest-point foot on the OFFSET result lies in the face iff the sample's (u, v) was in the input face
      // (away from the boundary, where the answer is legitimately ambiguous: skip samples within 0.12 of it).
      double nearest = 1e300;
      for (const Poly& lp : in_loops)
        for (const ON_2dPoint& c : lp) nearest = std::min(nearest, std::hypot(c.x - u, c.y - v));
      if (nearest < 0.12) continue;
      if (inside) {
        ++n_in;
        if (!InFace(ob, out_loops, foot.x, foot.y)) ++bad_foot_in;
      } else {
        ++n_out;
        if (InFace(ob, out_loops, foot.x, foot.y)) ++bad_foot_out;
      }
    }
  }
  Check(n_in > 200 && n_out > 200, (t + "the sample grid covers both the face interior and the trimmed-away region").c_str());
  Check(bad_param == 0, (t + "1600 samples: S(u,v) + d n(u,v) equals the offset surface at the same (u,v) to 2e-4 (worst " +
                        std::to_string(worst_param) + ")").c_str());
  Check(bad_dist == 0, (t + "1600 samples: every offset sample lies ON the result surface (closest-point distance <= 2e-4, worst " +
                        std::to_string(worst_dist) + ")").c_str());
  Check(bad_foot_in == 0, (t + "offset samples of interior points land inside the result face's trim region").c_str());
  Check(bad_foot_out == 0, (t + "offset samples of trimmed-away points land OUTSIDE the result face's trim region").c_str());

  // (2) Boundary: every rebuilt edge follows the exact locus, is |d| from the original surface, with its foot on the trim.
  double worst_locus = 0.0, worst_gap = 0.0, worst_foot = 0.0;
  int boundary_samples = 0;
  for (int ei = 0; ei < ob.m_E.Count(); ++ei) {
    const ON_BrepEdge& e = ob.m_E[ei];
    const ON_BrepTrim& trim = ob.m_T[e.m_ti[0]];
    for (int k = 0; k < 120; ++k) {
      const double f = (k + 0.37) / 120.0;
      const ON_3dPoint q = e.PointAt(e.Domain().ParameterAt(f));
      const ON_3dPoint uv = trim.PointAt(trim.Domain().ParameterAt(trim.m_bRev3d ? 1.0 - f : f));
      const ON_3dPoint locus = fx.s.PointAt(uv.x, uv.y) + d * FdNormal(fx.s, uv.x, uv.y);
      worst_locus = std::max(worst_locus, q.DistanceTo(locus));
      const ON_2dPoint foot(fx.s.ClosestPointParameter(q).x, fx.s.ClosestPointParameter(q).y);
      worst_gap = std::max(worst_gap, std::fabs(q.DistanceTo(fx.s.PointAt(foot.x, foot.y)) - std::fabs(d)));
      worst_foot = std::max(worst_foot, std::hypot(foot.x - uv.x, foot.y - uv.y));
      ++boundary_samples;
    }
  }
  Check(boundary_samples == 120 * ob.m_E.Count() && worst_locus < 3e-4,
        (t + "every boundary edge point equals S(c2(t)) + d n(c2(t)) of the ORIGINAL surface to 3e-4 (worst " +
         std::to_string(worst_locus) + ")").c_str());
  Check(worst_gap < 5e-4,
        (t + "every boundary point is |d| from the original surface (parallel-surface property; worst gap " + std::to_string(worst_gap) + ")").c_str());
  Check(worst_foot < 2e-3,
        (t + "the foot of each boundary point on the ORIGINAL surface is the trim's own (u,v) to 2e-3 (worst " + std::to_string(worst_foot) + ")").c_str());
  Check(rep.max_edge_locus_deviation < 3e-4 && std::fabs(rep.max_edge_locus_deviation - worst_locus) < 3e-4,
        (t + "the report's own max_edge_locus_deviation agrees with the independent measurement").c_str());
  // The surface really moved by |d|: the offset surface's centre is |d| away from the original's.
  const double cu = 0.5 * (fx.u0 + fx.u1), cv = 0.5 * (fx.v0 + fx.v1);
  Check(std::fabs(res.PointAt(cu, cv).DistanceTo(fx.s.PointAt(cu, cv)) - std::fabs(d)) < 3e-4 &&
            (res.PointAt(cu, cv) - fx.s.PointAt(cu, cv)) * FdNormal(fx.s, cu, cv) * d > 0.0,
        (t + "the offset surface sits |d| from the original at its centre, on the requested side of the normal").c_str());
}

}  // namespace

D8_EXTRA_TEST(TrimmedOffsetFreeformFaceInterior_and_Boundary) {
  Fixture& fx = Fix();
  Check(fx.ok, "fixture: a freeform bump patch trimmed by an ellipse with a circular hole is a valid one-face body");
  if (!fx.ok) return;
  Check(fx.face.raw().m_L.Count() == 2 && fx.face.raw().m_E.Count() == 2, "fixture: one outer loop and one inner loop, one closed edge each");
  RunOffsetChecks(fx.face, 0.35, "outward 0.35, face with a hole");
  RunOffsetChecks(fx.face, -0.35, "inward 0.35, face with a hole");
  RunOffsetChecks(fx.face_nohole, 0.2, "outward 0.2, no hole");
}

D8_EXTRA_TEST(TrimmedOffsetFlippedFaceUsesItsOutwardNormal) {
  Fixture& fx = Fix();
  if (!fx.ok) { Check(false, "fixture failed"); return; }
  Brep flipped = fx.face_nohole;
  flipped.raw().FlipFace(flipped.raw().m_F[0]);
  Check(flipped.raw().m_F[0].m_bRev, "fixture: the flipped copy has m_bRev set");
  Brep up, down;
  TrimmedOffsetReport r1, r2;
  const bool both_ok = OffsetTrimmedFace(fx.face_nohole, 0.3, up, 1e-4, &r1) == Result::Ok &&
                       OffsetTrimmedFace(flipped, 0.3, down, 1e-4, &r2) == Result::Ok;
  Check(both_ok, "a flipped and an unflipped copy both offset by +0.3");
  if (!both_ok) return;
  const ON_BrepVertex& vu = up.raw().m_V[0];
  const ON_BrepVertex& vd = down.raw().m_V[0];
  const ON_3dPoint v0 = fx.face_nohole.raw().m_V[0].point;
  const ON_3dVector n = FdNormal(fx.s, 0.5 * (fx.u0 + fx.u1), 0.5 * (fx.v0 + fx.v1));
  Check((vu.point - v0) * n > 0.0 && (vd.point - v0) * n < 0.0,
        "positive distance follows the FACE's outward normal: the flipped face moves to the other side of the surface normal");
}

D8_EXTRA_TEST(TrimmedOffsetPlanarFaceIsExactAndVerticesMoveByDTimesNormal) {
  // An analytic plane through the same code: exact closed forms (vertex + d * n), exact flag, edges straight.
  const std::vector<ON_3dPoint> poly = {ON_3dPoint(0, 0, 1), ON_3dPoint(4, 0, 1), ON_3dPoint(5, 3, 1), ON_3dPoint(2, 4, 1), ON_3dPoint(-1, 2, 1)};
  Brep::PlanarFace pf;
  pf.loop = poly;
  pf.plane = ON_Plane(poly[0], ON_3dVector(0, 0, 1));
  const Brep plate = Brep::FromPlanarFaces({pf});
  Brep out;
  TrimmedOffsetReport rep;
  const Result r = OffsetTrimmedFace(plate, 0.75, out, -1.0, &rep);
  Check(r == Result::Ok, ("planar pentagon: offset succeeds (reason: " + rep.refusal_reason + ")").c_str());
  if (r != Result::Ok) return;
  Check(rep.surface_exact && out.raw().IsValid() && out.raw().m_E.Count() == 5 && out.raw().m_V.Count() == 5,
        "planar pentagon: the plane offset is the exact closed form, 5 edges / 5 vertices, valid");
  double worst = 0.0;
  for (int i = 0; i < out.raw().m_V.Count(); ++i) {
    bool matched = false;
    for (const ON_3dPoint& p : poly) matched = matched || out.raw().m_V[i].point.DistanceTo(ON_3dPoint(p.x, p.y, p.z + 0.75)) < 1e-9;
    if (!matched) worst = 1.0;
  }
  Check(worst == 0.0, "planar pentagon: every vertex is an input vertex moved by exactly d * n = (0, 0, 0.75)");
  double straight = 0.0;
  for (int ei = 0; ei < out.raw().m_E.Count(); ++ei) {
    const ON_BrepEdge& e = out.raw().m_E[ei];
    const ON_3dPoint a = e.PointAtStart(), z = e.PointAtEnd();
    for (int k = 1; k < 20; ++k) {
      const ON_3dPoint q = e.PointAt(e.Domain().ParameterAt(k / 20.0));
      const ON_Line ln(a, z);
      straight = std::max(straight, q.DistanceTo(ln.ClosestPointTo(q)));
      straight = std::max(straight, std::fabs(q.z - 1.75));
    }
  }
  Check(straight < 1e-9, "planar pentagon: every rebuilt edge is a straight segment in the plane z = 1.75");
  Brep back;
  Check(OffsetTrimmedFace(out, -0.75, back, -1.0, nullptr) == Result::Ok && std::fabs(back.raw().m_V[0].point.z - 1.0) < 1e-9,
        "planar pentagon: offsets compose - offsetting back by -0.75 returns to z = 1");
}

D8_EXTRA_TEST(TrimmedOffsetRefusalsStateTheirReasonAndLeaveOutputUntouched) {
  Fixture& fx = Fix();
  if (!fx.ok) { Check(false, "fixture failed"); return; }
  Brep out = fx.face_nohole;
  const int before_vertices = out.raw().m_V.Count();
  TrimmedOffsetReport rep;
  // A distance beyond the surface's radius of curvature folds: refused by the surface offset, reason stated.
  const Result big = OffsetTrimmedFace(fx.face_nohole, 40.0, out, 1e-4, &rep);
  Check(big == Result::Failed && !rep.refusal_reason.empty() && rep.refusal_reason.find("surface offset was refused") != std::string::npos,
        ("a 40-unit offset of a bump with curvature radius ~ 3 is refused with the surface's reason: " + rep.refusal_reason).c_str());
  Check(out.raw().m_V.Count() == before_vertices && out.raw().m_V[0].point.DistanceTo(fx.face_nohole.raw().m_V[0].point) == 0.0,
        "...and the output body is left exactly as it was");
  auto throws = [&](const std::function<void()>& fn) {
    try { fn(); } catch (const std::invalid_argument&) { return true; } catch (...) { return false; }
    return false;
  };
  Brep o2;
  Check(throws([&] { OffsetTrimmedFace(fx.face_nohole, NAN, o2); }), "a NaN distance throws std::invalid_argument");
  Check(throws([&] { OffsetTrimmedFace(fx.face_nohole, 0.1, o2, INFINITY); }), "an infinite tolerance throws std::invalid_argument");
  Check(throws([&] { OffsetTrimmedFace(Brep::Box(0, 0, 0, 1, 1, 1), 0.1, o2); }), "a six-face box (several faces) throws: corner reconstruction is not done here");
  Check(OffsetTrimmedFace(fx.face_nohole, 0.0, o2) == Result::Ok && o2.raw().m_V[0].point.DistanceTo(fx.face_nohole.raw().m_V[0].point) == 0.0,
        "distance 0 returns an exact copy");
  // A boundary with a corner inside ONE edge cannot be followed by a cubic: honest refusal, not a rounded corner.
  std::vector<ON_3dPoint> cvs;
  const double cu = 0.5 * (fx.u0 + fx.u1), cv = 0.5 * (fx.v0 + fx.v1), hu = 0.25 * (fx.u1 - fx.u0), hv = 0.25 * (fx.v1 - fx.v0);
  cvs.push_back(fx.s.PointAt(cu - hu, cv - hv));
  cvs.push_back(fx.s.PointAt(cu + hu, cv - hv));
  cvs.push_back(fx.s.PointAt(cu + hu, cv + hv));
  cvs.push_back(fx.s.PointAt(cu - hu, cv + hv));
  cvs.push_back(cvs.front());
  const Brep square = Brep::TrimmedPatchFace(fx.s, fx.fit, NurbsCurve::FromControlPoints(cvs, 1));
  Brep o3;
  TrimmedOffsetReport rep3;
  const Result r3 = OffsetTrimmedFace(square, 0.2, o3, 1e-4, &rep3);
  const std::string label3 = "a square boundary stored as ONE edge (4 corners) is refused naming the tangent break: " + rep3.refusal_reason;
  Check(r3 == Result::Failed && rep3.refusal_reason.find("tangent break") != std::string::npos, label3.c_str());
}

D8_EXTRA_TEST(TrimmedOffsetOfATrimmedPlaneKeepsItsCircleExact) {
  // PlanarSrf's object: ON_BrepTrimmedPlane of a circle. The plane offsets by a translation, so the edge must stay the EXACT circle
  // (radius 5 to 1e-12, centre moved by d n), not a cubic fitted through samples.
  ON_Plane plane(ON_3dPoint(1, 2, 0), ON_3dVector(0, 0, 1));
  ON_NurbsCurve circle;
  ON_Circle(plane, 5.0).GetNurbForm(circle);
  ON_Brep* raw = ON_BrepTrimmedPlane(plane, circle);
  Check(raw != nullptr, "fixture: a trimmed plane bounded by a circle");
  if (!raw) return;
  Brep disc;
  disc.raw() = *raw;
  delete raw;
  Brep out;
  TrimmedOffsetReport rep;
  const Result r = OffsetTrimmedFace(disc, 2.0, out, -1.0, &rep);
  const std::string label = "trimmed plane: offset succeeds (reason: " + rep.refusal_reason + ")";
  Check(r == Result::Ok, label.c_str());
  if (r != Result::Ok) return;
  Check(rep.surface_exact && rep.edges_rebuilt == 1 && rep.edges_translated == 1, "trimmed plane: exact surface, its one edge translated (not fitted)");
  const ON_Brep& ob = out.raw();
  Check(ob.IsValid() && ob.m_E.Count() == 1, "trimmed plane: valid, one edge");
  double worst_radius = 0.0, worst_height = 0.0;
  const ON_BrepEdge& e = ob.m_E[0];
  for (int k = 0; k < 200; ++k) {
    const ON_3dPoint q = e.PointAt(e.Domain().ParameterAt((k + 0.31) / 200.0));
    worst_radius = std::max(worst_radius, std::fabs(std::hypot(q.x - 1.0, q.y - 2.0) - 5.0));
    worst_height = std::max(worst_height, std::fabs(q.z - 2.0));
  }
  const std::string label2 = "trimmed plane: every edge point is at radius 5 about (1, 2) and height 2 to 1e-12 (radius " + std::to_string(worst_radius) +
                             ", height " + std::to_string(worst_height) + ")";
  Check(worst_radius < 1e-12 && worst_height < 1e-12, label2.c_str());
  // The unchanged 2D trim still bounds the disc: shoelace over the trim sampled densely.
  const ON_BrepTrim& t = ob.m_T[0];
  double a2 = 0.0;
  for (int k = 0; k < 4000; ++k) {
    const ON_3dPoint p = t.PointAt(t.Domain().ParameterAt(k / 4000.0)), q = t.PointAt(t.Domain().ParameterAt((k + 1) / 4000.0));
    a2 += p.x * q.y - q.x * p.y;
  }
  Check(std::fabs(std::fabs(0.5 * a2) - ON_PI * 25.0) < 1e-3, "trimmed plane: the unchanged trim still bounds a disc of area 25 pi");
}

D8_EXTRA_TEST(TrimmedOffsetOfAFlatPatchWithAHoleIsAnExactTranslation) {
  // The app's Patch of two concentric flat rings (r = 5 and r = 2, z = 0) is a freeform-typed trimmed face whose fitted surface is
  // flat: its offset by d is the translation by d n, so every edge point must equal the original edge point + (0, 0, +-d) to
  // 1e-9, and both loops must survive.
  auto ring = [](double r, int n) {
    std::vector<ON_3dPoint> cvs;
    for (int k = 0; k < n; ++k) cvs.push_back(ON_3dPoint(r * std::cos(2.0 * ON_PI * k / n), r * std::sin(2.0 * ON_PI * k / n), 0.0));
    cvs.push_back(cvs.front());
    return NurbsCurve::FromControlPoints(cvs, 3);
  };
  const NurbsCurve outer = ring(5.0, 24), hole = ring(2.0, 12);
  std::vector<ON_3dPoint> boundary;
  for (const NurbsCurve* c : {&outer, &hole})
    for (int k = 0; k < 96; ++k) boundary.push_back(c->PointAt(c->Domain().min + (c->Domain().max - c->Domain().min) * k / 96.0));
  NurbsSurface s;
  NurbsSurface::PatchFitReport fit;
  if (NurbsSurface::FitPatch(boundary, {}, 1e-4, s, &fit) != Result::Ok) { Check(false, "fixture: FitPatch of two flat rings"); return; }
  const Brep face = Brep::TrimmedPatchFace(s, fit, outer, {hole});
  Check(face.raw().IsValid() && face.raw().m_L.Count() == 2, "fixture: a flat patch trimmed by an outer ring and a hole ring (two loops)");
  for (double d : {1.5, -1.5}) {
    Brep out;
    TrimmedOffsetReport rep;
    const Result r = OffsetTrimmedFace(face, d, out, 1e-4, &rep);
    const std::string label = "flat patch d=" + std::to_string(d) + ": offset succeeds (reason: " + rep.refusal_reason + ")";
    Check(r == Result::Ok, label.c_str());
    if (r != Result::Ok) continue;
    Check(out.raw().IsValid() && out.raw().m_L.Count() == 2 && out.raw().m_E.Count() == 2, "...valid, both loops and both edges kept");
    double worst_xy = 0.0, worst_z = 0.0, zsign = 0.0;
    for (int ei = 0; ei < 2; ++ei) {
      const ON_BrepEdge& e0 = face.raw().m_E[ei];
      const ON_BrepEdge& e1 = out.raw().m_E[ei];
      for (int k = 0; k < 100; ++k) {
        const double f = (k + 0.37) / 100.0;
        const ON_3dPoint p = e0.PointAt(e0.Domain().ParameterAt(f)), q = e1.PointAt(e1.Domain().ParameterAt(f));
        worst_xy = std::max(worst_xy, std::hypot(p.x - q.x, p.y - q.y));
        worst_z = std::max(worst_z, std::fabs(std::fabs(q.z) - 1.5));
        zsign = q.z;
      }
    }
    Check(worst_xy < 1e-9 && worst_z < 1e-9, "...every edge point is the original one moved by exactly 1.5 along z (xy unchanged)");
    Check(rep.surface_exact || rep.surface_deviation <= 1e-4, "...the surface is exact or within the tolerance");
    (void)zsign;
  }
}
