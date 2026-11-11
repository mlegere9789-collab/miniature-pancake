// Non-affine deformations (Twist / Bend / Taper space warps; DeformMesh / DeformCurve / DeformSurface). Every expected position
// is an independent closed form written out here (a rotation by an angle that is linear in z, the circle a bent line lies on, a
// linear scale of the offset from an axis) - never read back from the code under test, and never evaluated at the parameters
// the deformation itself refines on.
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/deform.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::DeformCurve;
using dino8::kernel::DeformMesh;
using dino8::kernel::DeformReport;
using dino8::kernel::DeformSurface;
using dino8::kernel::MakeBend;
using dino8::kernel::MakeTaper;
using dino8::kernel::MakeTwist;
using dino8::kernel::Mesh;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::PointMap;
using dino8::kernel::Result;

// A flat sheet x in [x0, x1], z in [z0, z1] at y = 0 (parameters (u, v) -> (x0 + u (x1-x0), 0, z0 + v (z1-z0))).
NurbsSurface SheetXZ(double x0, double x1, double z0, double z1) {
  std::vector<Point3d> grid = {Point3d(x0, 0, z0), Point3d(x0, 0, z1), Point3d(x1, 0, z0), Point3d(x1, 0, z1)};  // i (x) major
  return NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
}

// A flat sheet x in [0, w], y in [0, h] at z = 0.
NurbsSurface SheetXY(double w, double h) {
  std::vector<Point3d> grid = {Point3d(0, 0, 0), Point3d(0, h, 0), Point3d(w, 0, 0), Point3d(w, h, 0)};
  return NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
}

// 17 x 17 parameters that are NOT on a power-of-two lattice, so they never coincide with a refinement's own knots or midpoints.
double Param(int k, int n) { return (k + 0.37) / (n + 0.74); }

double WorstOver(const NurbsSurface& s, const NurbsSurface& src, const PointMap& truth) {
  double worst = 0.0;
  for (int i = 0; i < 17; ++i) {
    for (int j = 0; j < 17; ++j) {
      const double u = src.Domain(0).min + (src.Domain(0).max - src.Domain(0).min) * Param(i, 17);
      const double v = src.Domain(1).min + (src.Domain(1).max - src.Domain(1).min) * Param(j, 17);
      worst = std::max(worst, s.PointAt(u, v).DistanceTo(truth(src.PointAt(u, v))));
    }
  }
  return worst;
}

}  // namespace

D8_EXTRA_TEST(DeformTwistedSheetMatchesTheRotationByAnAngleLinearInZ) {
  const NurbsSurface sheet = SheetXZ(0, 4, 0, 6);  // (4u, 0, 6v)
  const double theta = ON_PI / 2.0;
  const PointMap twist = MakeTwist(Point3d(0, 0, 0), Point3d(0, 0, 6), theta);
  const double tol = 1e-3;
  NurbsSurface out;
  DeformReport rep;
  const Result r = DeformSurface(sheet, twist, tol, out, &rep);
  d8test::Check(r == Result::Ok, "twisted sheet: the deformation reaches 1e-3");
  d8test::Check(rep.refinements > 0 && out.CVCountU() > sheet.CVCountU() && out.CVCountV() > sheet.CVCountV(),
                "twisted sheet: the 2 x 2 net had to be refined (a twist is not representable by moving its 4 control points)");
  d8test::Check(rep.achieved_deviation <= tol, "twisted sheet: the reported deviation is within the tolerance");
  // independent closed form at 17 x 17 off-lattice parameters: x = 4u, z = 6v, rotated by (pi/2) z / 6 = (pi/2) v about the z axis
  double worst = 0.0, worst_radius = 0.0, worst_z = 0.0;
  for (int i = 0; i < 17; ++i) {
    for (int j = 0; j < 17; ++j) {
      const double u = Param(i, 17), v = Param(j, 17);
      const double phi = theta * v;
      const Point3d want(4 * u * std::cos(phi), 4 * u * std::sin(phi), 6 * v);
      const Point3d got = out.PointAt(u, v);
      worst = std::max(worst, got.DistanceTo(want));
      worst_radius = std::max(worst_radius, std::fabs(std::hypot(got.x, got.y) - 4 * u));
      worst_z = std::max(worst_z, std::fabs(got.z - 6 * v));
    }
  }
  d8test::Check(worst <= 2 * tol, ("twisted sheet: 289 independent points within 2e-3 of (4u cos, 4u sin, 6v) (worst " + std::to_string(worst) + ")").c_str());
  d8test::Check(worst_radius <= 2 * tol, "twisted sheet: the distance from the axis is kept (a twist is a rotation)");
  d8test::Check(worst_z <= 1e-9, "twisted sheet: z is unchanged");
  // negative control: the unrefined net is far from the twist, and with no refinements allowed the call says so
  NurbsSurface none;
  DeformReport rep0;
  d8test::Check(DeformSurface(sheet, twist, tol, none, &rep0, 0) == Result::Failed && rep0.achieved_deviation > 0.1,
                "negative control: with 0 refinements the call refuses and reports a deviation far above 1e-3");
  // a tighter tolerance really costs a finer net
  NurbsSurface fine;
  DeformReport rep_fine;
  const Result r_fine = DeformSurface(sheet, twist, 1e-4, fine, &rep_fine, 8);
  d8test::Check(r_fine == Result::Ok && rep_fine.u_count > rep.u_count,
                ("a 10x tighter tolerance (1e-4) gives a strictly finer net (result " + std::to_string(static_cast<int>(r_fine)) + ", " + std::to_string(rep.u_count) + " -> " +
                 std::to_string(rep_fine.u_count) + " control points per direction, worst " + std::to_string(rep_fine.achieved_deviation) + ")").c_str());
}

D8_EXTRA_TEST(DeformBentSheetLiesOnTheClosedFormCircle) {
  // spine (0,0,0)-(8,0,0) bent through (4,0,4): the circle through the origin tangent to the x axis and through (4,0,4) has
  // centre (0,0,R) with 16 + (4 - R)^2 = R^2, i.e. R = 4 (a quarter circle: the end of the spine, s = 8, lands at (4,0,4)).
  const NurbsSurface sheet = SheetXY(8, 4);  // (8u, 4v, 0)
  const double R = 4.0, theta = ON_PI / 2.0;
  const PointMap bend = MakeBend(Point3d(0, 0, 0), Point3d(8, 0, 0), Point3d(4, 0, 4));
  const double tol = 1e-3;
  NurbsSurface out;
  DeformReport rep;
  d8test::Check(DeformSurface(sheet, bend, tol, out, &rep) == Result::Ok, "bent sheet: the deformation reaches 1e-3");
  double worst = 0.0, worst_circle = 0.0, worst_y = 0.0;
  for (int i = 0; i < 17; ++i) {
    for (int j = 0; j < 17; ++j) {
      const double u = Param(i, 17), v = Param(j, 17);
      const double phi = theta * u;  // the angle is linear in the spine coordinate s = 8u: (pi/2) s / 8
      const Point3d want(R * std::sin(phi), 4 * v, R - R * std::cos(phi));
      const Point3d got = out.PointAt(u, v);
      worst = std::max(worst, got.DistanceTo(want));
      worst_circle = std::max(worst_circle, std::fabs(std::hypot(got.x, got.z - R) - R));  // on the circle of radius 4 about (0, y, 4)
      worst_y = std::max(worst_y, std::fabs(got.y - 4 * v));
    }
  }
  d8test::Check(worst <= 2 * tol, ("bent sheet: 289 independent points within 2e-3 of (R sin(phi), y, R (1 - cos(phi))) (worst " + std::to_string(worst) + ")").c_str());
  d8test::Check(worst_circle <= 2 * tol, "bent sheet: every point is at distance 4 from the bend axis (0, y, 4) - the strip became a cylinder");
  d8test::Check(worst_y <= 1e-9, "bent sheet: the coordinate along the bend axis is unchanged");
  const auto end = out.PointAt(1.0, 0.0);
  d8test::Check(end.DistanceTo(Point3d(4, 0, 4)) <= 2 * tol, "bent sheet: the far end of the spine lands at (4, 0, 4)");
  // the corner at the start is fixed (s <= 0 is untouched)
  d8test::Check(out.PointAt(0.0, 1.0).DistanceTo(Point3d(0, 4, 0)) <= 1e-9, "bent sheet: the start edge does not move");
  // the arc of the strip's edge is longer than the chord across it: it really is curved (negative control against a no-op)
  d8test::Check(end.DistanceTo(Point3d(8, 0, 0)) > 4.0, "negative control: the far end moved by more than 4 from where the flat strip had it");
}

D8_EXTRA_TEST(DeformTaperedSheetIsExactWithTheOriginalNetAndScalesTheOffset) {
  // x in [-2, 2], z in [0, 6]; the offset from the z axis is scaled by (1 - t) + t * 2/4 = 1 - z/12 (d0 = 4, d1 = 2, t = z/6).
  // x' = x (1 - z/12) is bilinear in (u, v), so the bilinear 2 x 2 net maps EXACTLY: zero refinements.
  const NurbsSurface sheet = SheetXZ(-2, 2, 0, 6);
  const PointMap taper = MakeTaper(Point3d(0, 0, 0), Point3d(0, 0, 6), 4, 2);
  NurbsSurface out;
  DeformReport rep;
  d8test::Check(DeformSurface(sheet, taper, 1e-9, out, &rep) == Result::Ok, "tapered sheet: reaches 1e-9");
  d8test::Check(rep.refinements == 0 && out.CVCountU() == 2 && out.CVCountV() == 2,
                "tapered sheet: the image of a bilinear sheet under a z-linear taper is bilinear: no refinement is needed");
  double worst = 0.0;
  for (int i = 0; i < 17; ++i) {
    for (int j = 0; j < 17; ++j) {
      const double u = Param(i, 17), v = Param(j, 17);
      const double x = -2 + 4 * u, z = 6 * v;
      worst = std::max(worst, out.PointAt(u, v).DistanceTo(Point3d(x * (1 - z / 12.0), 0, z)));
    }
  }
  d8test::Check(worst <= 1e-12, "tapered sheet: x' = x (1 - z/12) at 289 points to 1e-12");
  d8test::Check(out.PointAt(0.5, 1.0).DistanceTo(Point3d(0, 0, 6)) <= 1e-12 && out.PointAt(1.0, 1.0).DistanceTo(Point3d(1, 0, 6)) <= 1e-12,
                "tapered sheet: the top edge, 4 wide, is 2 wide (x = +-1)");
}

D8_EXTRA_TEST(DeformTwistedRationalSphereKeepsRadiusAndHeightAndRotatesByTheLinearAngle) {
  ON_Sphere sphere(ON_3dPoint(0, 0, 0), 2.0);
  ON_NurbsSurface raw;
  sphere.GetNurbForm(raw);
  NurbsSurface s;
  s.raw() = raw;
  d8test::Check(s.IsRational(), "sphere fixture: the NURBS sphere is rational");
  const double theta = ON_PI;
  const PointMap twist = MakeTwist(Point3d(0, 0, -2), Point3d(0, 0, 2), theta);  // angle = pi (z + 2) / 4
  const double tol = 2e-3;
  NurbsSurface out;
  DeformReport rep;
  const Result r = DeformSurface(s, twist, tol, out, &rep, 7);
  d8test::Check(r == Result::Ok, ("twisted sphere: reaches 2e-3 (refinements " + std::to_string(rep.refinements) + ", worst " + std::to_string(rep.achieved_deviation) + ")").c_str());
  if (r != Result::Ok) return;
  double worst_radius = 0.0, worst_z = 0.0, worst_angle = 0.0;
  int measured = 0;
  const auto d0 = s.Domain(0), d1 = s.Domain(1);
  for (int i = 0; i < 17; ++i) {
    for (int j = 0; j < 17; ++j) {
      const double u = d0.min + (d0.max - d0.min) * Param(i, 17), v = d1.min + (d1.max - d1.min) * Param(j, 17);
      const Point3d p = s.PointAt(u, v), q = out.PointAt(u, v);
      worst_radius = std::max(worst_radius, std::fabs(std::hypot(q.x, q.y) - std::hypot(p.x, p.y)));
      worst_z = std::max(worst_z, std::fabs(q.z - p.z));
      if (std::hypot(p.x, p.y) > 0.4) {  // the azimuth is meaningless at a pole
        double d = std::atan2(q.y, q.x) - std::atan2(p.y, p.x) - theta * (p.z + 2.0) / 4.0;
        d = std::remainder(d, 2.0 * ON_PI);
        worst_angle = std::max(worst_angle, std::fabs(d));
        ++measured;
      }
    }
  }
  d8test::Check(worst_radius <= 2 * tol, "twisted sphere: the distance from the axis of every point is unchanged");
  d8test::Check(worst_z <= 2 * tol, "twisted sphere: the height of every point is unchanged");
  d8test::Check(measured > 150 && worst_angle <= 0.02, ("twisted sphere: the azimuth turned by pi (z + 2) / 4 at " + std::to_string(measured) + " points (worst " + std::to_string(worst_angle) + " rad)").c_str());
  d8test::Check(out.IsRational(), "twisted sphere: the result stays rational (weights kept)");
}

D8_EXTRA_TEST(DeformTwistedLineBecomesTheClosedFormHelix) {
  const NurbsCurve line = NurbsCurve::FromControlPoints({Point3d(2, 0, 0), Point3d(2, 0, 6)}, 1);
  const PointMap twist = MakeTwist(Point3d(0, 0, 0), Point3d(0, 0, 6), 2.0 * ON_PI);  // one full turn over the line
  NurbsCurve out;
  DeformReport rep;
  const double tol = 1e-3;
  d8test::Check(DeformCurve(line, twist, tol, out, &rep) == Result::Ok, "helix: the deformation reaches 1e-3");
  d8test::Check(rep.refinements > 0 && out.ControlPointCount() > 2, "helix: the 2-point line had to be refined");
  double worst = 0.0, worst_r = 0.0;
  for (int k = 0; k < 400; ++k) {
    const double t = (k + 0.37) / 400.74;  // the line's parameter is z / 6
    const double phi = 2.0 * ON_PI * t;
    const Point3d got = out.PointAt(out.Domain().min + (out.Domain().max - out.Domain().min) * t);
    worst = std::max(worst, got.DistanceTo(Point3d(2 * std::cos(phi), 2 * std::sin(phi), 6 * t)));
    worst_r = std::max(worst_r, std::fabs(std::hypot(got.x, got.y) - 2.0));
  }
  d8test::Check(worst <= 2 * tol, ("helix: 400 independent points within 2e-3 of (2 cos, 2 sin, z) (worst " + std::to_string(worst) + ")").c_str());
  d8test::Check(worst_r <= 2 * tol, "helix: the radius stays 2");
  // the helix is longer than the straight line by the closed form sqrt(6^2 + (2 pi 2)^2) (one turn of radius 2 over 6)
  const double helix_length = std::sqrt(36.0 + (2 * ON_PI * 2.0) * (2 * ON_PI * 2.0));
  d8test::Check(std::fabs(out.Length() - helix_length) < 0.02 * helix_length, ("helix: length " + std::to_string(out.Length()) + " matches sqrt(6^2 + (4 pi)^2) = " + std::to_string(helix_length) + " to 2%").c_str());
  NurbsCurve none;
  DeformReport r0;
  d8test::Check(DeformCurve(line, twist, tol, none, &r0, 0) == Result::Failed && r0.achieved_deviation > 1.0,
                "negative control: 0 refinements refuses and reports a deviation above 1");
}

D8_EXTRA_TEST(DeformMeshMovesEveryVertexByTheMapAndKeepsTheTopology) {
  const NurbsSurface sheet = SheetXZ(0, 4, 0, 6);
  const Mesh m = sheet.TessellateGrid(4, 6);
  const PointMap twist = MakeTwist(Point3d(0, 0, 0), Point3d(0, 0, 6), ON_PI / 2.0);
  const Mesh d = DeformMesh(m, twist);
  d8test::Check(d.raw().m_V.Count() == m.raw().m_V.Count() && d.raw().m_F.Count() == m.raw().m_F.Count(), "deformed mesh: same vertex and face counts");
  double worst = 0.0;
  bool faces_same = true;
  for (int i = 0; i < m.raw().m_V.Count(); ++i) {
    const ON_3fPoint& v = m.raw().m_V[i];
    const double phi = (ON_PI / 2.0) * (v.z / 6.0);  // independent rotation about z
    const Point3d want(v.x * std::cos(phi) - v.y * std::sin(phi), v.x * std::sin(phi) + v.y * std::cos(phi), v.z);
    const ON_3fPoint& g = d.raw().m_V[i];
    worst = std::max(worst, Point3d(g.x, g.y, g.z).DistanceTo(want));
  }
  for (int f = 0; f < m.raw().m_F.Count(); ++f) {
    for (int k = 0; k < 4; ++k) faces_same = faces_same && m.raw().m_F[f].vi[k] == d.raw().m_F[f].vi[k];
  }
  d8test::Check(worst <= 1e-5, "deformed mesh: every vertex is at the closed-form rotation (float precision)");
  d8test::Check(faces_same, "deformed mesh: the face index lists are unchanged");
  const int last = m.raw().m_V.Count() - 1;  // a vertex at z = 6: the (4, 0, 6) corner turns a quarter circle, to (0, 4, 6)
  d8test::Check(std::fabs(m.raw().m_V[last].x - d.raw().m_V[last].x) + std::fabs(m.raw().m_V[last].y - d.raw().m_V[last].y) > 1.0, "negative control: the vertices did move");
}

D8_EXTRA_TEST(DeformArgumentChecksAndRefusals) {
  const auto throws = [](const std::function<void()>& f) {
    try {
      f();
    } catch (const std::invalid_argument&) {
      return true;
    }
    return false;
  };
  d8test::Check(throws([] { MakeTwist(Point3d(0, 0, 0), Point3d(0, 0, 0), 1.0); }), "MakeTwist: a zero-length axis is refused");
  d8test::Check(throws([] { MakeTwist(Point3d(0, 0, 0), Point3d(0, 0, 1), std::numeric_limits<double>::quiet_NaN()); }), "MakeTwist: a NaN angle is refused");
  d8test::Check(throws([] { MakeBend(Point3d(0, 0, 0), Point3d(8, 0, 0), Point3d(4, 0, 0)); }), "MakeBend: a bend point on the spine is refused");
  d8test::Check(throws([] { MakeBend(Point3d(0, 0, 0), Point3d(8, 0, 0), Point3d(-4, 0, 4)); }), "MakeBend: a bend point behind the spine start is refused");
  d8test::Check(throws([] { MakeTaper(Point3d(0, 0, 0), Point3d(0, 0, 1), 0.0, 1.0); }), "MakeTaper: d0 = 0 is refused");
  const NurbsSurface sheet = SheetXZ(0, 4, 0, 6);
  NurbsSurface out;
  const PointMap id = [](Point3d p) { return p; };
  d8test::Check(throws([&] { DeformSurface(sheet, id, 0.0, out); }), "DeformSurface: a zero tolerance is refused");
  d8test::Check(throws([&] { DeformSurface(sheet, id, 1e-3, out, nullptr, -1); }), "DeformSurface: a negative refinement budget is refused");
  d8test::Check(throws([&] { DeformSurface(sheet, [](Point3d) { return Point3d(std::numeric_limits<double>::quiet_NaN(), 0, 0); }, 1e-3, out); }),
                "DeformSurface: a map that returns NaN is refused, not returned as geometry");
  DeformReport rep;
  d8test::Check(DeformSurface(sheet, id, 1e-9, out, &rep) == Result::Ok && rep.refinements == 0 && rep.achieved_deviation <= 1e-12,
                "the identity map needs no refinement and deviates by 0");
  d8test::Check(out.PointAt(0.3, 0.7).DistanceTo(sheet.PointAt(0.3, 0.7)) <= 1e-12, "the identity deformation reproduces the sheet");
}
