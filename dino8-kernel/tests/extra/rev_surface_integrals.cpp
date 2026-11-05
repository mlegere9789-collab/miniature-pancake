// Volume() / Area() / VolumeMassProperties() on bodies whose faces are ON_RevSurface (what ON_BrepSphere / ON_BrepTorus /
// ON_BrepCylinder / ON_BrepCone produce, and what the application's primitives carry). They used to throw "face 0 is
// not backed by an ON_NurbsSurface". Oracles are closed forms of the solids, independent of the integrator:
//   sphere   V = 4/3 pi r^3, A = 4 pi r^2, centroidal I = 2/5 V r^2 about any axis;
//   torus    V = 2 pi^2 R r^2, A = 4 pi^2 R r, I_axis = V (R^2 + 3/4 r^2), I_diameter = V (R^2/2 + 5/8 r^2);
//   cylinder V = pi r^2 h, A = 2 pi r (r + h), I_axis = V r^2 / 2, I_transverse = V (r^2/4 + h^2/12);
//   cone     V = pi r^2 h / 3, A = pi r (r + slant), centroid at h/4 from the base,
//            I_axis = 3/10 V r^2, I_transverse(centroid) = V (3/20 r^2 + 3/80 h^2).
#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>

#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::MassProperties;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

constexpr double kPi = 3.14159265358979323846;

bool Near(double a, double b, double rel) { return std::abs(a - b) <= rel * std::abs(b); }

template <class F>
bool Runs(F f, const char* what) {
  try {
    f();
    return true;
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string(what) + " threw: " + e.what()).c_str());
    return false;
  }
}

}  // namespace

D8_EXTRA_TEST(RevSurfaceSphereIntegrals) {
  using d8test::Check;
  const double r = 2.0;
  const Point3d c(1, 2, 3);
  const Brep s = Brep::SphereWelded(c, r);
  const double V = 4.0 / 3.0 * kPi * r * r * r;
  double v = 0, a = 0;
  MassProperties m;
  if (Runs([&] { v = s.Volume(); }, "sphere Volume()")) Check(Near(v, V, 1e-9), "sphere Volume() == 4/3 pi r^3");
  if (Runs([&] { a = s.Area(); }, "sphere Area()")) Check(Near(a, 4.0 * kPi * r * r, 1e-9), "sphere Area() == 4 pi r^2");
  if (Runs([&] { m = s.VolumeMassProperties(); }, "sphere VolumeMassProperties()")) {
    Check(Near(m.volume, V, 1e-9), "sphere mass properties: volume");
    Check(std::abs(m.centroid.x - c.x) < 1e-9 && std::abs(m.centroid.y - c.y) < 1e-9 && std::abs(m.centroid.z - c.z) < 1e-9,
          "sphere mass properties: centroid is the centre");
    const double I = 0.4 * V * r * r;
    Check(Near(m.ixx, I, 1e-8) && Near(m.iyy, I, 1e-8) && Near(m.izz, I, 1e-8), "sphere mass properties: centroidal I = 2/5 V r^2");
  }
}

D8_EXTRA_TEST(RevSurfaceTorusIntegrals) {
  using d8test::Check;
  const double R = 3.0, r = 1.0;
  const Brep t = Brep::TorusWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), R, r);
  const double V = 2.0 * kPi * kPi * R * r * r;
  double v = 0, a = 0;
  MassProperties m;
  if (Runs([&] { v = t.Volume(); }, "torus Volume()")) Check(Near(v, V, 1e-9), "torus Volume() == 2 pi^2 R r^2");
  if (Runs([&] { a = t.Area(); }, "torus Area()")) Check(Near(a, 4.0 * kPi * kPi * R * r, 1e-9), "torus Area() == 4 pi^2 R r");
  if (Runs([&] { m = t.VolumeMassProperties(); }, "torus VolumeMassProperties()")) {
    Check(Near(m.volume, V, 1e-9), "torus mass properties: volume");
    Check(std::abs(m.centroid.x) < 1e-9 && std::abs(m.centroid.y) < 1e-9 && std::abs(m.centroid.z) < 1e-9, "torus mass properties: centroid at the centre");
    Check(Near(m.izz, V * (R * R + 0.75 * r * r), 1e-8), "torus mass properties: I about the axis = V (R^2 + 3/4 r^2)");
    Check(Near(m.ixx, V * (0.5 * R * R + 0.625 * r * r), 1e-8) && Near(m.iyy, m.ixx, 1e-8), "torus mass properties: I about a diameter = V (R^2/2 + 5/8 r^2)");
  }
}

D8_EXTRA_TEST(RevSurfaceCylinderIntegrals) {
  using d8test::Check;
  const double r = 1.5, h = 4.0;
  const Brep c = Brep::CylinderWelded(Point3d(1, -2, 0.5), Vector3d(0, 0, 1), r, h);
  const double V = kPi * r * r * h;
  double v = 0, a = 0;
  MassProperties m;
  // The two cap discs are trimmed faces integrated at the trimmed-face tolerance (1e-6 by default for Volume()).
  if (Runs([&] { v = c.Volume(); }, "cylinder Volume()")) Check(Near(v, V, 1e-6), "cylinder Volume() == pi r^2 h");
  if (Runs([&] { a = c.Area(); }, "cylinder Area()")) Check(Near(a, 2.0 * kPi * r * (r + h), 1e-6), "cylinder Area() == 2 pi r (r + h)");
  if (Runs([&] { m = c.VolumeMassProperties(); }, "cylinder VolumeMassProperties()")) {
    Check(Near(m.volume, V, 1e-6), "cylinder mass properties: volume");
    Check(std::abs(m.centroid.x - 1) < 1e-6 && std::abs(m.centroid.y + 2) < 1e-6 && std::abs(m.centroid.z - (0.5 + 0.5 * h)) < 1e-6,
          "cylinder mass properties: centroid at mid-height on the axis");
    Check(Near(m.izz, 0.5 * V * r * r, 1e-5), "cylinder mass properties: I about the axis = V r^2 / 2");
    Check(Near(m.ixx, V * (r * r / 4.0 + h * h / 12.0), 1e-5) && Near(m.iyy, m.ixx, 1e-5),
          "cylinder mass properties: transverse I = V (r^2/4 + h^2/12)");
  }
}

D8_EXTRA_TEST(RevSurfaceConeIntegrals) {
  using d8test::Check;
  const double r = 2.0, h = 3.0;
  const Brep c = Brep::ConeWelded(Point3d(1, 2, 3), Vector3d(0, 0, 1), r, 0.0, h);
  const double V = kPi * r * r * h / 3.0;
  const double slant = std::sqrt(r * r + h * h);
  double v = 0, a = 0;
  MassProperties m;
  if (Runs([&] { v = c.Volume(); }, "cone Volume()")) Check(Near(v, V, 1e-6), "cone Volume() == pi r^2 h / 3");
  if (Runs([&] { a = c.Area(); }, "cone Area()")) Check(Near(a, kPi * r * (r + slant), 1e-6), "cone Area() == pi r (r + slant)");
  if (Runs([&] { m = c.VolumeMassProperties(); }, "cone VolumeMassProperties()")) {
    Check(Near(m.volume, V, 1e-6), "cone mass properties: volume");
    Check(std::abs(m.centroid.x - 1) < 1e-6 && std::abs(m.centroid.y - 2) < 1e-6 && std::abs(m.centroid.z - (3 + h / 4.0)) < 1e-6,
          "cone mass properties: centroid on the axis at h/4 above the base");
    Check(Near(m.izz, 0.3 * V * r * r, 1e-5), "cone mass properties: I about the axis = 3/10 V r^2");
    Check(Near(m.ixx, V * (0.15 * r * r + 3.0 / 80.0 * h * h), 1e-5) && Near(m.iyy, m.ixx, 1e-5),
          "cone mass properties: centroidal transverse I = V (3/20 r^2 + 3/80 h^2)");
  }
}

D8_EXTRA_TEST(RevSurfaceFrustumIntegrals) {
  using d8test::Check;
  const double r0 = 2.0, r1 = 0.75, h = 3.0;
  const Brep f = Brep::ConeWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), r0, r1, h);
  const double V = kPi * h / 3.0 * (r0 * r0 + r0 * r1 + r1 * r1);
  const double slant = std::sqrt((r0 - r1) * (r0 - r1) + h * h);
  const double A = kPi * (r0 + r1) * slant + kPi * (r0 * r0 + r1 * r1);
  const double zbar = h * (r0 * r0 + 2 * r0 * r1 + 3 * r1 * r1) / (4.0 * (r0 * r0 + r0 * r1 + r1 * r1));
  double v = 0, a = 0;
  MassProperties m;
  if (Runs([&] { v = f.Volume(); }, "frustum Volume()")) Check(Near(v, V, 1e-6), "frustum Volume() == pi h/3 (R0^2 + R0 R1 + R1^2)");
  if (Runs([&] { a = f.Area(); }, "frustum Area()")) Check(Near(a, A, 1e-6), "frustum Area() == pi (R0+R1) slant + pi (R0^2 + R1^2)");
  if (Runs([&] { m = f.VolumeMassProperties(); }, "frustum VolumeMassProperties()")) {
    Check(std::abs(m.centroid.z - zbar) < 1e-6 && std::abs(m.centroid.x) < 1e-6, "frustum mass properties: centroid height from the closed form");
    // I about the axis: integral of pi r(z)^4 / 2 dz along the linear profile = pi h (R0^5 - R1^5) / (10 (R0 - R1)).
    const double Iz = kPi * h * (std::pow(r0, 5) - std::pow(r1, 5)) / (10.0 * (r0 - r1));
    Check(Near(m.izz, Iz, 1e-5), "frustum mass properties: I about the axis from the closed form");
  }
}
