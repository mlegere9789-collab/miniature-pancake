// NurbsSurface::MatchEdgeToCurve and MatchEdgeToCurveFollowing when the edited surface, the target curve (or the host) is PERIODIC along the matched
// closed edge (made periodic with MakePeriodicExact(), whose clamped form ends in a ~6e-13 sliver of knots: see ClampedEquivalent() in surface_edit.cpp).
// They used to refuse every periodic surface or curve; they now run on clamped equivalents and hand the surface back clamped, same shape.
//
// A. MatchEdgeToCurve, non-rational: a closed bicubic tube (8 ring points around radius ~4, 4 rows up to z = 0, the ring wavy and growing by row) made
//    periodic around the ring, matched along its top rim to a periodic closed cubic curve (a different wavy ring at z = 0.4). The oracles are INPUT values
//    read from the input objects by OpenNURBS' evaluator, never the construction's own report: the edited rim is the target curve point for point
//    (periodic evaluation at interior parameters), and the inward first and second derivatives along the rim are the INPUT surface's own (the default
//    "preserved" cross-derivative field) - read from the periodic input before the match.
// B. MatchEdgeToCurveFollowing, rational: the cylinder wall x^2 + y^2 = 16, z in [-3, 0], periodic, matched along its top rim to the circle of radius 4 at
//    z = 0 (periodic) following the torus R = 3, r = 1 (whose outer equator is exactly that circle). Closed forms: the torus normal there is radial, so G1
//    leaves the wall's normal radial; G2 gives the wall the torus' second fundamental form: Gaussian curvature cos(v)/(r (R + r cos v)) = 1/4 along the
//    rim (it was 0). Host clamped, and periodic in both directions.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::MatchContinuity;
using dino8::kernel::MatchFollowReport;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

void Chk(bool ok, const std::string& m) { d8test::Check(ok, m.c_str()); }

// ---- A ---------------------------------------------------------------------------------------------------------------------------------------------

NurbsSurface Tube() {
  std::vector<Point3d> g;
  for (int i = 0; i < 9; ++i) {
    const double a = 2.0 * ON_PI * (i % 8) / 8.0;
    for (int j = 0; j < 4; ++j) {
      const double r = 4.0 + 0.25 * j + 0.3 * std::sin(3.0 * a + j);
      g.push_back(Point3d(r * std::cos(a), r * std::sin(a), -3.0 + j));
    }
  }
  return NurbsSurface::FromControlGrid(g, 9, 4, 3, 3);
}

NurbsCurve TargetRing() {
  ON_NurbsCurve c;
  c.Create(3, false, 4, 9);
  c.MakeClampedUniformKnotVector();
  for (int i = 0; i < 9; ++i) {
    const double a = 2.0 * ON_PI * (i % 8) / 8.0;
    const double r = 4.2 + 0.2 * std::cos(2.0 * a);
    c.SetCV(i, ON_3dPoint(r * std::cos(a), r * std::sin(a), 0.4 + 0.1 * std::sin(a)));
  }
  NurbsCurve out;
  out.raw() = c;
  return out;
}

void RunA(MatchContinuity cont, const std::string& name) {
  NurbsSurface s = Tube();
  NurbsCurve target = TargetRing();
  Chk(s.MakePeriodicExact(0) == Result::Ok && s.IsPeriodic(0), name + ": fixture - the tube is made periodic around the ring");
  Chk(target.MakePeriodicExact() == Result::Ok && target.raw().IsPeriodic(), name + ": fixture - the target ring is made periodic");
  const NurbsSurface input = s;
  const NurbsCurve target_input = target;
  const Result r = s.MatchEdgeToCurve(1, false, target, cont);
  Chk(r == Result::Ok, name + ": MatchEdgeToCurve with a periodic surface and a periodic target curve now runs");
  if (r != Result::Ok) return;
  Chk(s.raw().IsClamped(0, 2) && s.raw().IsClamped(1, 2), name + ": the edited surface comes back clamped");
  const ON_Interval iu = input.raw().Domain(0), iv = input.raw().Domain(1), su = s.raw().Domain(0), sv = s.raw().Domain(1), cd = target_input.raw().Domain();
  double on_curve = 0, d1 = 0, d2 = 0;
  for (int k = 1; k < 48; ++k) {
    const double f = k / 48.0;
    const ON_3dPoint rim = s.raw().PointAt(su.ParameterAt(f), sv.Max());
    on_curve = std::max(on_curve, rim.DistanceTo(target_input.raw().PointAt(cd.ParameterAt(f))));
    ON_3dPoint p0, p1;
    ON_3dVector a0, b0, aa0, ab0, bb0, a1, b1, aa1, ab1, bb1;
    input.raw().Ev2Der(iu.ParameterAt(f), iv.Max(), p0, a0, b0, aa0, ab0, bb0);
    s.raw().Ev2Der(su.ParameterAt(f), sv.Max(), p1, a1, b1, aa1, ab1, bb1);
    d1 = std::max(d1, (b1 - b0).Length());
    d2 = std::max(d2, (bb1 - bb0).Length());
  }
  Chk(on_curve < 1e-9, name + ": the edited rim is the periodic target curve, point for point (1e-9)");
  if (cont != MatchContinuity::Position) Chk(d1 < 1e-9, name + ": the inward first derivative along the rim is the INPUT surface's own (preserved field)");
  if (cont == MatchContinuity::Curvature) Chk(d2 < 1e-9, name + ": the inward second derivative along the rim is the INPUT surface's own (preserved field)");
}

D8_EXTRA_TEST(MatchEdgeToCurvePeriodicTubeG0) { RunA(MatchContinuity::Position, "A G0"); }
D8_EXTRA_TEST(MatchEdgeToCurvePeriodicTubeG1) { RunA(MatchContinuity::Tangent, "A G1"); }
D8_EXTRA_TEST(MatchEdgeToCurvePeriodicTubeG2) { RunA(MatchContinuity::Curvature, "A G2"); }

// ---- B ---------------------------------------------------------------------------------------------------------------------------------------------

NurbsSurface Wall() {
  ON_Cylinder cyl(ON_Circle(ON_Plane::World_xy, 4.0));
  cyl.height[0] = -3.0;
  cyl.height[1] = 0.0;
  NurbsSurface s;
  cyl.GetNurbForm(s.raw());
  return s;
}

NurbsSurface Torus() {
  ON_Torus t(ON_Plane::World_xy, 3.0, 1.0);
  NurbsSurface s;
  t.GetNurbForm(s.raw());
  return s;
}

NurbsCurve RimCircle() {
  ON_NurbsCurve c;
  ON_Circle(ON_Plane::World_xy, 4.0).GetNurbForm(c);
  NurbsCurve out;
  out.raw() = c;
  return out;
}

void RunB(MatchContinuity cont, bool periodic_host, const std::string& name) {
  NurbsSurface wall = Wall();
  NurbsCurve circle = RimCircle();
  NurbsSurface host = Torus();
  Chk(wall.MakePeriodicExact(0) == Result::Ok && wall.IsPeriodic(0), name + ": fixture - the wall is periodic around the circle");
  Chk(circle.MakePeriodicExact() == Result::Ok && circle.raw().IsPeriodic(), name + ": fixture - the rim circle is periodic");
  if (periodic_host) {
    host.MakePeriodicExact(0);
    host.MakePeriodicExact(1);
    Chk(host.IsPeriodic(0) || host.IsPeriodic(1), name + ": fixture - the host torus is periodic");
  }
  MatchFollowReport rep;
  const Result r = wall.MatchEdgeToCurveFollowing(1, false, circle, host, cont, &rep);
  Chk(r == Result::Ok, name + ": MatchEdgeToCurveFollowing with a periodic surface, curve (and host) now runs" + (r == Result::Ok ? "" : " - " + rep.refusal_reason));
  if (r != Result::Ok) return;
  double rad = 0, height = 0, ncross = 0, kk = 0;
  const ON_Interval du = wall.raw().Domain(0), dv = wall.raw().Domain(1);
  for (int k = 0; k <= 96; ++k) {
    const double u = du.ParameterAt(k / 96.0);
    const ON_3dPoint p = wall.raw().PointAt(u, dv.Max());
    rad = std::max(rad, std::abs(std::hypot(p.x, p.y) - 4.0));
    height = std::max(height, std::abs(p.z));
    ON_3dPoint q;
    ON_3dVector a, b;
    wall.raw().Ev1Der(u, dv.Max(), q, a, b);
    ON_3dVector n = ON_CrossProduct(a, b);
    n.Unitize();
    ncross = std::max(ncross, ON_CrossProduct(n, ON_3dVector(p.x, p.y, 0.0) * (1.0 / 4.0)).Length());
    kk = std::max(kk, std::abs(wall.CurvatureAt(u, dv.Max()).gaussian - 0.25));
  }
  Chk(rad < 1e-9 && height < 1e-9, name + ": the matched rim is the circle of radius 4 at z = 0 (1e-9)");
  Chk(ncross < 1e-6, name + ": the wall's normal along the rim is the torus' radial normal");
  if (cont == MatchContinuity::Curvature) Chk(kk < 1e-6, name + ": the wall's own Gaussian curvature along the rim is the torus' closed form 1/4");
}

D8_EXTRA_TEST(MatchEdgeToCurveFollowingPeriodicG1) { RunB(MatchContinuity::Tangent, false, "B G1"); }
D8_EXTRA_TEST(MatchEdgeToCurveFollowingPeriodicG2) { RunB(MatchContinuity::Curvature, false, "B G2"); }
D8_EXTRA_TEST(MatchEdgeToCurveFollowingPeriodicG2PeriodicHost) { RunB(MatchContinuity::Curvature, true, "B G2 periodic host"); }

}  // namespace
