// OrientOnSurfaceTransform / SurfaceNormalFrameAt (Orient on surface, Splop) and FlowPointAlongSurface (FlowAlongSrf), closed forms:
//   * a 2 x 2 x 4 box standing on the frame at its bottom centre, oriented onto the sphere r 5 at P = 5 (sin 30 cos 40, sin 30 sin 40, cos 30):
//     the base centre lands on P, the box z axis on the radial direction P / 5 and its x / y axes in the tangent plane, orthonormal; the box
//     centroid (2 above the base) is at distance r + h / 2 = 7 from the sphere centre along P; volume 16; a point NOT on the sphere (radius
//     8.5, and radius 2) picks the same closest point and so the same result,
//   * a plane patch u in [0, 10 pi] along x, v in [0, 4] along y flowed onto the cylinder r 5, height 4 about z (u is the angle, v the height):
//     a line on the patch from x = 0 to a quarter / a half of the width lands on a quarter / half circle, of length 5 pi / 2 and 5 pi (the
//     length is preserved: the patch width 10 pi is the circumference), every image at distance 5 from the axis; the same line lifted by
//     d = 1 off the patch lands at radius 5 +- 1 (the normal offset is kept) with length radius * angle.
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/pattern.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {
using dino8::kernel::Brep;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

NurbsSurface Sphere(double r) {
  ON_NurbsSurface n;
  ON_Sphere(ON_3dPoint(0, 0, 0), r).GetNurbForm(n);
  NurbsSurface s;
  s.raw() = n;
  return s;
}

void CheckOrient(const std::string& label, const NurbsSurface& sphere, Point3d at, const ON_3dPoint& p_on, double r) {
  const Brep box = Brep::BoxWelded(0, 0, 0, 2, 2, 4);
  const ON_Plane from(ON_3dPoint(1, 1, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0));
  try {
    const ON_Xform xf = dino8::kernel::OrientOnSurfaceTransform(sphere, from, at);
    const ON_3dPoint base = xf * ON_3dPoint(1, 1, 0);
    const ON_3dPoint ex = xf * ON_3dPoint(2, 1, 0), ey = xf * ON_3dPoint(1, 2, 0), ez = xf * ON_3dPoint(1, 1, 1);
    const ON_3dVector a = ex - base, b = ey - base, c = ez - base;
    d8test::Check(base.DistanceTo(p_on) < 1e-6, (label + ": the base centre lands on the sphere point P").c_str());
    d8test::Check(std::fabs(a.Length() - 1) < 1e-9 && std::fabs(b.Length() - 1) < 1e-9 && std::fabs(c.Length() - 1) < 1e-9 && std::fabs(a * b) < 1e-9 &&
                      std::fabs(a * c) < 1e-9 && std::fabs(b * c) < 1e-9 && ON_CrossProduct(a, b) * c > 0.999999,
                  (label + ": the image frame is orthonormal and right-handed").c_str());
    ON_3dVector radial(p_on.x, p_on.y, p_on.z);
    radial.Unitize();
    d8test::Check((c - radial).Length() < 1e-6, (label + ": the box z axis is the sphere normal (radial, outward)").c_str());
    d8test::Check(std::fabs(a * radial) < 1e-6 && std::fabs(b * radial) < 1e-6, (label + ": the box x / y axes lie in the tangent plane").c_str());
    const ON_3dPoint centroid = xf * ON_3dPoint(1, 1, 2);
    d8test::Check(std::fabs(centroid.DistanceTo(ON_3dPoint(0, 0, 0)) - (r + 2.0)) < 1e-6 && (centroid - ON_3dPoint(0, 0, 0)) / (r + 2.0) * radial > 0.999999,
                  (label + ": the centroid is at distance r + h / 2 = 7 along the radial direction").c_str());
    const Brep placed = dino8::kernel::OrientBrepOnSurface(box, sphere, from, at);
    d8test::Check(std::fabs(placed.Volume() - 16.0) < 1e-6, (label + ": the oriented box keeps volume 16").c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (label + ": threw " + e.what()).c_str());
  }
}

double PolylineLength(const std::vector<ON_3dPoint>& pts) {
  double len = 0;
  for (size_t i = 1; i < pts.size(); ++i) len += pts[i - 1].DistanceTo(pts[i]);
  return len;
}
}  // namespace

D8_EXTRA_TEST(OrientBoxOntoSphere) {
  const NurbsSurface sphere = Sphere(5.0);
  const double a = 30.0 * ON_PI / 180.0, b = 40.0 * ON_PI / 180.0;
  const ON_3dPoint p(5.0 * std::sin(a) * std::cos(b), 5.0 * std::sin(a) * std::sin(b), 5.0 * std::cos(a));
  CheckOrient("box onto the sphere r 5 at a surface point", sphere, Point3d(p.x, p.y, p.z), p, 5.0);
  CheckOrient("same, the picked point 8.5 from the centre", sphere, Point3d(1.7 * p.x, 1.7 * p.y, 1.7 * p.z), p, 5.0);
  CheckOrient("same, the picked point 2 from the centre", sphere, Point3d(0.4 * p.x, 0.4 * p.y, 0.4 * p.z), p, 5.0);
}

D8_EXTRA_TEST(FlowLineFromPlaneOntoCylinder) {
  const double L = 10.0 * ON_PI;
  const NurbsSurface base = NurbsSurface::FromControlGrid({Point3d(0, 0, 0), Point3d(0, 4, 0), Point3d(L, 0, 0), Point3d(L, 4, 0)}, 2, 2, 1, 1);
  ON_NurbsSurface cn;
  ON_Cylinder cyl(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 5.0), 4.0);
  cyl.GetNurbForm(cn);
  NurbsSurface target;
  target.raw() = cn;
  struct Case {
    const char* label;
    double fraction, lift;
  };
  for (const Case& c : {Case{"quarter, on the patch", 0.25, 0.0}, Case{"half, on the patch", 0.5, 0.0}, Case{"quarter, lifted by 1", 0.25, 1.0}}) {
    std::vector<ON_3dPoint> img;
    double rmin = 1e9, rmax = -1e9;
    try {
      for (int i = 0; i <= 2000; ++i) {
        const Point3d q = dino8::kernel::FlowPointAlongSurface(base, target, Point3d(c.fraction * L * i / 2000.0, 2.0, c.lift));
        img.emplace_back(q.x, q.y, q.z);
        const double rad = std::hypot(q.x, q.y);
        rmin = std::min(rmin, rad);
        rmax = std::max(rmax, rad);
      }
    } catch (const std::exception& e) {
      d8test::Check(false, (std::string(c.label) + ": threw " + e.what()).c_str());
      continue;
    }
    const std::string l = c.label;
    d8test::Check(rmax - rmin < 1e-6, (l + ": every image point is at one distance from the axis").c_str());
    const double rad = 0.5 * (rmin + rmax);
    const double want_rad = c.lift == 0.0 ? 5.0 : (std::fabs(rad - 6.0) < 1e-6 ? 6.0 : 4.0);
    d8test::Check(std::fabs(rad - want_rad) < 1e-6, (l + ": the distance from the axis is 5 on the patch and 5 +- 1 for the lifted line").c_str());
    const double angle = c.fraction * 2.0 * ON_PI;
    const double len = PolylineLength(img);
    char buf[120];
    std::snprintf(buf, sizeof buf, ": length %.6f, closed form radius * angle = %.6f", len, rad * angle);
    d8test::Check(std::fabs(len - rad * angle) < 1e-3, (l + buf).c_str());
    d8test::Check(std::fabs(img.front().z - 2.0) < 1e-9 && std::fabs(img.back().z - 2.0) < 1e-6, (l + ": the line stays at height 2 (v is the height)").c_str());
  }
}

// Arc-length fractions, not parameter fractions: the rational circle's parameter is not proportional to arc length between its knots, so a
// line over a NON-knot interval of the patch must still land on an arc of the SAME length. x from 0.07 L to 0.19 L (L = 10 pi = the circumference):
// length 0.12 L = 3.769911 = 5 * (0.12 * 2 pi); every point x = f L lands at the angle f * 2 pi, and a point lifted by d keeps arc fractions too.
D8_EXTRA_TEST(FlowPreservesArcLengthBetweenKnots) {
  const double L = 10.0 * ON_PI;
  const NurbsSurface base = NurbsSurface::FromControlGrid({Point3d(0, 0, 0), Point3d(0, 4, 0), Point3d(L, 0, 0), Point3d(L, 4, 0)}, 2, 2, 1, 1);
  ON_NurbsSurface cn;
  ON_Cylinder cyl(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 5.0), 4.0);
  cyl.GetNurbForm(cn);
  NurbsSurface target;
  target.raw() = cn;
  std::vector<ON_3dPoint> img;
  double worst_angle = 0;
  for (int i = 0; i <= 2000; ++i) {
    const double f = 0.07 + 0.12 * i / 2000.0;
    const Point3d q = dino8::kernel::FlowPointAlongSurface(base, target, Point3d(f * L, 2.0, 0.0));
    img.emplace_back(q.x, q.y, q.z);
    double ang = std::atan2(q.y, q.x);
    if (ang < 0) ang += 2.0 * ON_PI;
    worst_angle = std::max(worst_angle, std::fabs(ang - f * 2.0 * ON_PI));
  }
  const double len = PolylineLength(img);
  char buf[140];
  std::snprintf(buf, sizeof buf, "a line over the non-knot span [0.07 L, 0.19 L]: length %.8f, closed form 5 * 0.12 * 2 pi = %.8f", len, 5.0 * 0.12 * 2.0 * ON_PI);
  d8test::Check(std::fabs(len - 5.0 * 0.12 * 2.0 * ON_PI) < 1e-6, buf);
  d8test::Check(worst_angle < 1e-6, "every point x = f L lands at the angle f * 2 pi (arc-length fraction f) to 1e-6");
  // lifted by 1: radius 6 or 4, the same angles, so length = radius * 0.12 * 2 pi
  std::vector<ON_3dPoint> lifted;
  for (int i = 0; i <= 2000; ++i) {
    const Point3d q = dino8::kernel::FlowPointAlongSurface(base, target, Point3d((0.07 + 0.12 * i / 2000.0) * L, 2.0, 1.0));
    lifted.emplace_back(q.x, q.y, q.z);
  }
  const double rad = std::hypot(lifted.front().x, lifted.front().y);
  d8test::Check(std::fabs(PolylineLength(lifted) - rad * 0.12 * 2.0 * ON_PI) < 1e-6, "the lifted line keeps the angles: length radius * 0.12 * 2 pi to 1e-6");
}
