// ProjectCurveToSurface (along a direction) and PullbackCurveToSurface (to the nearest point) on curved targets, against closed-form arc
// lengths computed here by independent quadrature of the analytic curve (never read off the code under test):
//   * a circle r 3 projected straight down onto a plane stays a circle: 2 pi 3; onto the tilted plane z = x / 2 it is the ellipse
//     (3 cos t, 3 sin t, 1.5 cos t), length = int 3 sqrt(1 + 0.25 cos^2 t) dt; onto a sphere r 5 about the origin it is the circle r 3 at
//     z = 4 (2 pi 3), onto a cylinder r 5 about the y axis (rays along -x) it is (sqrt(25 - 9 sin^2 t), 3 cos t, 3 sin t),
//   * a circle r 3 pulled onto a cylinder r 1 about the same axis is the circle r 1 (2 pi), and a helix r 3, pitch 2, 1.5 turns pulled onto it
//     is the helix r 1 of the same pitch: 1.5 sqrt((2 pi)^2 + 2^2); pulled onto a sphere r 1 about the origin a circle r 3 in the z = 0 plane is
//     the equator (2 pi).
// Every returned point must also lie on the target (distance to the analytic surface < 1e-6) and, for the pull, on the nearest-point ray.
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {
using dino8::kernel::IntersectOptions;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

double Simpson(double (*f)(double), double a, double b, int n = 4000) {
  const double h = (b - a) / n;
  double s = f(a) + f(b);
  for (int i = 1; i < n; ++i) s += f(a + i * h) * (i % 2 ? 4.0 : 2.0);
  return s * h / 3.0;
}

double Length(const ON_Curve& c) {
  const ON_Interval d = c.Domain();
  double len = 0;
  ON_3dPoint prev = c.PointAt(d.Min());
  for (int i = 1; i <= 4000; ++i) {
    const ON_3dPoint p = c.PointAt(d.ParameterAt(i / 4000.0));
    len += prev.DistanceTo(p);
    prev = p;
  }
  return len;
}

ON_NurbsCurve Circle(const ON_3dPoint& c, const ON_3dVector& normal, double r) {
  ON_NurbsCurve n;
  ON_Circle(ON_Plane(c, normal), r).GetNurbForm(n);
  return n;
}

ON_NurbsSurface SphereSurface(double r) {
  ON_NurbsSurface n;
  ON_Sphere(ON_3dPoint(0, 0, 0), r).GetNurbForm(n);
  return n;
}

ON_NurbsSurface CylinderSurface(const ON_3dPoint& base, const ON_3dVector& axis, double r, double h0, double h1) {
  ON_NurbsSurface n;
  ON_Plane p(base, axis);
  ON_Cylinder cyl(ON_Circle(p, r), h1 - h0);
  cyl.height[0] = h0;
  cyl.height[1] = h1;
  cyl.GetNurbForm(n);
  return n;
}

double EllipseIntegrand(double t) { return 3.0 * std::sqrt(1.0 + 0.25 * std::cos(t) * std::cos(t)); }
double CylProjIntegrand(double t) {
  const double s = std::sin(t), c = std::cos(t);
  const double x = std::sqrt(25.0 - 9.0 * s * s);
  const double dx = -9.0 * s * c / x;
  return std::sqrt(dx * dx + 9.0 * s * s + 9.0 * c * c);
}

IntersectOptions Opt() {
  IntersectOptions o;
  o.tolerance = 1e-8;
  o.mesh_tolerance = 0.02;
  return o;
}

void CheckProject(const std::string& label, const ON_Curve& c, const ON_Surface& s, Vector3d dir, double want_len) {
  try {
    const auto r = dino8::kernel::ProjectCurveToSurface(c, s, dir, Opt());
    d8test::Check(r.sample_count >= 8 && r.hit_count == r.sample_count, (label + ": every sample hits the surface").c_str());
    if (r.hit_count < 4 || !r.projected_curve.IsValid()) {
      d8test::Check(false, (label + ": no projected curve").c_str());
      return;
    }
    double worst = 0;
    for (size_t i = 0; i < r.points.size(); ++i) worst = std::max(worst, r.points[i].DistanceTo(s.PointAt(r.uv[i].x, r.uv[i].y)));
    d8test::Check(worst < 1e-6, (label + ": every projected point is S(u, v) of its own (u, v)").c_str());
    const double len = Length(r.projected_curve);
    char buf[160];
    std::snprintf(buf, sizeof buf, ": arc length %.6f, closed form %.6f", len, want_len);
    d8test::Check(std::fabs(len - want_len) <= 2e-3 * want_len, (label + buf).c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (label + ": threw " + e.what()).c_str());
  }
}
}  // namespace

D8_EXTRA_TEST(ProjectCircleToPlaneSphereCylinder) {
  const ON_NurbsCurve circle_z5 = Circle(ON_3dPoint(0, 0, 5), ON_3dVector(0, 0, 1), 3.0);
  ON_PlaneSurface ground(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)));
  ground.SetExtents(0, ON_Interval(-100, 100), true);
  ground.SetExtents(1, ON_Interval(-100, 100), true);
  CheckProject("circle onto the plane z = 0 along -z", circle_z5, ground, Vector3d(0, 0, -1), 6.0 * ON_PI);

  ON_Plane tilted(ON_3dPoint(0, 0, 0), ON_3dVector(-0.5, 0, 1));
  ON_PlaneSurface slope(tilted);
  slope.SetExtents(0, ON_Interval(-100, 100), true);
  slope.SetExtents(1, ON_Interval(-100, 100), true);
  CheckProject("circle onto the plane z = x / 2 along -z", circle_z5, slope, Vector3d(0, 0, -1), Simpson(EllipseIntegrand, 0, 2.0 * ON_PI));

  const ON_NurbsCurve circle_z10 = Circle(ON_3dPoint(0, 0, 10), ON_3dVector(0, 0, 1), 3.0);
  const ON_NurbsSurface sphere = SphereSurface(5.0);
  CheckProject("circle r 3 onto the sphere r 5 along -z (the circle r 3 at z = 4)", circle_z10, sphere, Vector3d(0, 0, -1), 6.0 * ON_PI);

  const ON_NurbsCurve circle_x10 = Circle(ON_3dPoint(10, 0, 0), ON_3dVector(1, 0, 0), 3.0);
  const ON_NurbsSurface cyl = CylinderSurface(ON_3dPoint(0, -20, 0), ON_3dVector(0, 1, 0), 5.0, 0.0, 40.0);
  CheckProject("circle r 3 onto the cylinder r 5 about the y axis along -x", circle_x10, cyl, Vector3d(-1, 0, 0), Simpson(CylProjIntegrand, 0, 2.0 * ON_PI));
}

namespace {
void CheckPull(const std::string& label, const ON_Curve& c, const ON_Surface& s, double want_len, double radial_check_r, bool sphere) {
  try {
    const auto r = dino8::kernel::PullbackCurveToSurface(c, s, Opt());
    if (!r.pulled_curve.IsValid()) {
      d8test::Check(false, (label + ": no pulled curve").c_str());
      return;
    }
    // the pulled curve lies on the target: radius from the axis (cylinder about z) or from the origin (sphere) equals the target's
    double worst = 0;
    for (int i = 0; i <= 400; ++i) {
      const ON_3dPoint p = r.pulled_curve.PointAt(r.pulled_curve.Domain().ParameterAt(i / 400.0));
      const double rad = sphere ? std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z) : std::sqrt(p.x * p.x + p.y * p.y);
      worst = std::max(worst, std::fabs(rad - radial_check_r));
    }
    d8test::Check(worst < 1e-4, (label + ": every point of the pulled curve is on the target (radius to 1e-4)").c_str());
    const double len = Length(r.pulled_curve);
    char buf[160];
    std::snprintf(buf, sizeof buf, ": arc length %.6f, closed form %.6f", len, want_len);
    d8test::Check(std::fabs(len - want_len) <= 2e-3 * want_len, (label + buf).c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (label + ": threw " + e.what()).c_str());
  }
}
}  // namespace

D8_EXTRA_TEST(PullCircleAndHelixToCylinderAndSphere) {
  const ON_NurbsSurface cyl1 = CylinderSurface(ON_3dPoint(0, 0, -5), ON_3dVector(0, 0, 1), 1.0, 0.0, 20.0);
  const ON_NurbsCurve circle3 = Circle(ON_3dPoint(0, 0, 2), ON_3dVector(0, 0, 1), 3.0);
  CheckPull("circle r 3 pulled onto the cylinder r 1", circle3, cyl1, 2.0 * ON_PI, 1.0, false);
  {
    // the circle winds once around the cylinder: its pcurve is the straight uv line (t, 7) from u = 0 to u = 2 pi (z = 2 is v = 7)
    const auto r = dino8::kernel::PullbackCurveToSurface(circle3, cyl1, Opt());
    const ON_3dPoint a = r.pcurve.PointAtStart(), b = r.pcurve.PointAtEnd();
    d8test::Check(std::fabs(a.y - 7.0) < 1e-6 && std::fabs(b.y - 7.0) < 1e-6 && std::fabs(std::fabs(b.x - a.x) - 2.0 * ON_PI) < 1e-6,
                  "the pcurve of the circle on the cylinder runs one full period in u at v = 7 (open in uv, closed in 3D)");
    d8test::Check(r.pulled_curve.IsClosed(), "...and the pulled curve is closed in 3D");
  }

  // helix r 3, pitch 2, 1.5 turns, z from 0 to 3: interpolated through 240 points
  std::vector<ON_3dPoint> pts;
  const int n = 240;
  for (int i = 0; i <= n; ++i) {
    const double t = 1.5 * 2.0 * ON_PI * i / n;
    pts.emplace_back(3.0 * std::cos(t), 3.0 * std::sin(t), 2.0 * t / (2.0 * ON_PI));
  }
  const ON_NurbsCurve helix = dino8::kernel::InterpolateCubic(pts, {}, false, 3);
  CheckPull("helix r 3 pitch 2 pulled onto the cylinder r 1 (the helix r 1, same pitch)", helix, cyl1, 1.5 * std::sqrt(4.0 * ON_PI * ON_PI + 4.0), 1.0, false);

  const ON_NurbsSurface sph1 = SphereSurface(1.0);
  const ON_NurbsCurve equator3 = Circle(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), 3.0);
  CheckPull("circle r 3 in z = 0 pulled onto the sphere r 1 (the equator)", equator3, sph1, 2.0 * ON_PI, 1.0, true);
}
