// NurbsSurface::MatchEdge when the edited surface (or the target) is PERIODIC along the matched closed edge: a cylinder wall made periodic around its circle
// with MakePeriodicExact(), matched along its top rim.
//
// It used to fail closed ("periodic edge"): the row algebra needs clamped knot vectors, and the clamped form of a MakePeriodicExact surface ends in a
// sliver - NurbsCurve::MakePeriodicExact() spreads the last p knots a relative 1e-13 of the period apart (OpenNURBS' rational evaluator cannot evaluate a
// zero-width span at the domain end), so ClampEnd() leaves knots a hair before the domain end and one dead control point per sliver knot. The match now runs
// on the clamped equivalent, with the sliver snapped onto the domain end and the dead control points dropped (measured against the input, refused if off).
//
// Closed forms, not the construction's own residuals:
//   A. wall x^2 + y^2 = 16, z in [-3, 0], periodic; target the torus R = 3, r = 1 (clamped, or periodic in both directions): its outer equator is the circle
//      of radius 4 at z = 0, where the torus normal is the radial direction (cos u, sin u, 0) - the wall's own normal, so G1 changes nothing in position or normal -
//      and its Gaussian curvature is cos(v) / (r (R + r cos v)) = 1/4 there. G2 must give the matched wall the torus' second fundamental form along the rim:
//      Gaussian curvature 1/4 (the wall was 0).
//   B. the same wall matched at G2 to a FLAT ring z = 0, 4 <= r <= 7 (revolved line): normal +-z along the rim and Gaussian curvature 0.
// For both: the rim stays the circle of radius 4 at z = 0 to 1e-9, and the normal at the start of the rim loop equals the one at its end (the seam).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/surface.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::MatchContinuity;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Result;

void Chk(bool ok, const std::string& m) { d8test::Check(ok, m.c_str()); }

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

NurbsSurface FlatRing() {
  ON_LineCurve line(ON_3dPoint(4, 0, 0), ON_3dPoint(7, 0, 0));
  ON_RevSurface rev;
  rev.m_curve = line.DuplicateCurve();
  rev.m_axis = ON_Line(ON_3dPoint(0, 0, 0), ON_3dPoint(0, 0, 1));
  rev.m_angle = ON_Interval(0.0, 2.0 * ON_PI);
  rev.m_t = rev.m_angle;
  NurbsSurface s;
  rev.GetNurbForm(s.raw());
  return s;
}

// The direction d whose edge at its minimum is the circle of radius 4 at z = 0 (-1 if none).
int RimDirection(const NurbsSurface& s) {
  for (int d = 0; d < 2; ++d) {
    const ON_Interval along = s.raw().Domain(1 - d);
    bool all = true;
    for (int k = 1; k <= 7 && all; ++k) {
      const double e = along.ParameterAt(k / 8.0);
      const ON_3dPoint p = d == 0 ? s.raw().PointAt(s.raw().Domain(0).Min(), e) : s.raw().PointAt(e, s.raw().Domain(1).Min());
      all = std::abs(std::hypot(p.x, p.y) - 4.0) < 1e-9 && std::abs(p.z) < 1e-9;
    }
    if (all) return d;
  }
  return -1;
}

void Run(const std::string& name, NurbsSurface target, bool periodic_target, MatchContinuity c, double expected_k, bool check_k) {
  NurbsSurface wall = Wall();
  Chk(wall.MakePeriodicExact(0) == Result::Ok && wall.IsPeriodic(0), name + ": fixture - the wall is genuinely periodic around the circle");
  if (periodic_target) {
    Chk(target.MakePeriodicExact(0) != Result::Failed && target.MakePeriodicExact(1) != Result::Failed && (target.IsPeriodic(0) || target.IsPeriodic(1)),
        name + ": fixture - the target is made periodic");
  }
  const int fixed = RimDirection(target);
  Chk(fixed >= 0, name + ": fixture - the target has the radius-4 circle at z = 0 as an edge");
  if (fixed < 0) return;
  const Result r = wall.MatchEdge(1, false, target, fixed, true, c);
  Chk(r == Result::Ok, name + ": MatchEdge along a periodic closed rim now runs");
  if (r != Result::Ok) return;
  Chk(wall.raw().IsClamped(0, 2) && wall.raw().IsClamped(1, 2), name + ": the edited surface comes back clamped, the same shape");
  double rad = 0, height = 0, kk = 0;
  const ON_Interval du = wall.raw().Domain(0), dv = wall.raw().Domain(1);
  // the matched edge is whichever v end now lies on the rim circle
  const double ve = std::abs(wall.raw().PointAt(du.ParameterAt(0.3), dv.Min()).z) < 1e-9 ? dv.Min() : dv.Max();
  ON_3dVector n_first, n_last;
  for (int k = 0; k <= 96; ++k) {
    const double u = du.ParameterAt(k / 96.0);
    const ON_3dPoint p = wall.raw().PointAt(u, ve);
    rad = std::max(rad, std::abs(std::hypot(p.x, p.y) - 4.0));
    height = std::max(height, std::abs(p.z));
    ON_3dPoint q;
    ON_3dVector su, sv;
    wall.raw().Ev1Der(u, ve, q, su, sv);
    ON_3dVector n = ON_CrossProduct(su, sv);
    n.Unitize();
    if (k == 0) n_first = n;
    if (k == 96) n_last = n;
    kk = std::max(kk, std::abs(wall.CurvatureAt(u, ve).gaussian - expected_k));
  }
  Chk(rad < 1e-9 && height < 1e-9, name + ": the matched rim is the target's edge circle (radius 4, z = 0) to 1e-9");
  if (check_k) Chk(kk < 1e-6, name + ": the wall's own Gaussian curvature along the rim is the closed form " + std::to_string(expected_k));
  if (c != MatchContinuity::Position) Chk((n_first - n_last).Length() < 1e-9, name + ": the normal at the start of the rim loop equals the one at its end (the seam)");
}

D8_EXTRA_TEST(MatchEdgePeriodicWallToTorusG0) { Run("G0 wall->torus", Torus(), false, MatchContinuity::Position, 0.0, false); }
D8_EXTRA_TEST(MatchEdgePeriodicWallToTorusG1) { Run("G1 wall->torus", Torus(), false, MatchContinuity::Tangent, 0.0, false); }
D8_EXTRA_TEST(MatchEdgePeriodicWallToTorusG2) { Run("G2 wall->torus", Torus(), false, MatchContinuity::Curvature, 0.25, true); }
D8_EXTRA_TEST(MatchEdgePeriodicWallToPeriodicTorusG2) { Run("G2 wall->periodic torus", Torus(), true, MatchContinuity::Curvature, 0.25, true); }
D8_EXTRA_TEST(MatchEdgePeriodicWallToFlatRingG2) { Run("G2 wall->flat ring", FlatRing(), false, MatchContinuity::Curvature, 0.0, true); }

}  // namespace
