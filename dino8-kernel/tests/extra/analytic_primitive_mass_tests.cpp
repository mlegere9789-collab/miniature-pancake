// Exact volume / area / centroid of the application's analytic primitives (ON_BrepSphere / ON_BrepCylinder / ON_BrepCone /
// ON_BrepTorus wrapped as a Brep, exactly as the app's AddBrep does).
//
// Before: VolumeOfSealedSolid() of a capped cylinder was 0.38% low and of a cone 1.14% low (a silent wrong answer, worse than the
// 0.005-tolerance display mesh the app measures with) because each planar cap, trimmed by a CIRCLE, was integrated over the chord
// polygon of its (u, v) trim; Area() and VolumeMassProperties() refused every analytic-surface face ("not backed by an
// ON_NurbsSurface"). Every expected value here is a closed form worked by hand, never read back from the kernel:
//   sphere   V = 4/3 pi r^3          A = 4 pi r^2                    centroid = centre
//   cylinder V = pi r^2 h            A = 2 pi r h + 2 pi r^2         centroid = base + axis * h / 2     Izz(centroid) = V r^2 / 2
//   cone     V = pi r^2 h / 3        A = pi r s + pi r^2, s = sqrt(r^2 + h^2)     centroid = base + axis * h / 4 (apex side)
//   torus    V = 2 pi^2 R r^2        A = 4 pi^2 R r                  centroid = centre
// Each is also checked on a translated copy, a tilted (rotated about an oblique axis) copy and a scaled copy.
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::MassProperties;

constexpr double kPi = 3.14159265358979323846;

Brep Wrap(ON_Brep* b) {
  Brep k;
  k.raw() = *b;
  delete b;
  return k;
}

struct Case {
  const char* name;
  Brep brep;
  double volume;
  double area;
  ON_3dPoint centroid;
};

bool Rel(double got, double want, double tol) { return std::fabs(got - want) <= tol * std::fabs(want); }

// Moves the case by `xf`: the brep, and its centroid. Volume / area scale with s^3 / s^2 (uniform scale only).
Case Moved(const Case& c, const ON_Xform& xf, double s, const char* suffix) {
  Case m{c.name, c.brep.Transform(xf), c.volume * s * s * s, c.area * s * s, c.centroid};
  m.centroid.Transform(xf);
  static std::string names[64];
  static int n = 0;
  names[n % 64] = std::string(c.name) + " " + suffix;
  m.name = names[n++ % 64].c_str();
  return m;
}

void CheckVolume(const Case& c, double tol) {
  char what[256];
  try {
    const double v = c.brep.VolumeOfSealedSolid();
    std::snprintf(what, sizeof what, "%s: VolumeOfSealedSolid %.9g vs closed form %.9g (rel %.2e <= %.0e)", c.name, v, c.volume,
                  (v - c.volume) / c.volume, tol);
    d8test::Check(Rel(v, c.volume, tol), what);
  } catch (const std::exception& e) {
    std::snprintf(what, sizeof what, "%s: VolumeOfSealedSolid threw: %.120s", c.name, e.what());
    d8test::Check(false, what);
  }
}

}  // namespace

D8_EXTRA_TEST(AnalyticPrimitiveSealedVolumeMatchesClosedForms) {
  const ON_Plane base(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1));
  ON_Cone cone;
  cone.Create(base, 10.0, 5.0);
  Case cases[] = {
      {"sphere r=10", Wrap(ON_BrepSphere(ON_Sphere(ON_3dPoint(0, 0, 0), 10.0))), 4.0 / 3.0 * kPi * 1000.0, 4.0 * kPi * 100.0, ON_3dPoint(0, 0, 0)},
      {"cylinder r=5 h=20", Wrap(ON_BrepCylinder(ON_Cylinder(ON_Circle(base, 5.0), 20.0), true, true)), kPi * 25.0 * 20.0,
       2.0 * kPi * 5.0 * 20.0 + 2.0 * kPi * 25.0, ON_3dPoint(0, 0, 10)},
      {"cone r=5 h=10", Wrap(ON_BrepCone(cone, true)), kPi * 25.0 * 10.0 / 3.0, kPi * 5.0 * std::sqrt(125.0) + kPi * 25.0, ON_3dPoint(0, 0, 0)},
      {"torus R=6 r=1.5", Wrap(ON_BrepTorus(ON_Torus(base, 6.0, 1.5))), 2.0 * kPi * kPi * 6.0 * 1.5 * 1.5, 4.0 * kPi * kPi * 6.0 * 1.5,
       ON_3dPoint(0, 0, 0)},
  };
  ON_Xform move = ON_Xform::TranslationTransformation(7.0, -3.0, 11.0);
  ON_Xform tilt;
  tilt.Rotation(37.0 * kPi / 180.0, ON_3dVector(1, 2, 3), ON_3dPoint(0.5, -0.25, 1.0));
  tilt = ON_Xform::TranslationTransformation(-2.0, 4.0, 1.5) * tilt;
  const ON_Xform small = ON_Xform::ScaleTransformation(ON_3dPoint(0, 0, 0), 0.1);
  const ON_Xform large = ON_Xform::ScaleTransformation(ON_3dPoint(0, 0, 0), 20.0);
  for (const Case& c : cases) {
    CheckVolume(c, 1e-6);
    CheckVolume(Moved(c, move, 1.0, "translated"), 1e-6);
    CheckVolume(Moved(c, tilt, 1.0, "tilted"), 1e-6);
    CheckVolume(Moved(c, small, 0.1, "scaled x0.1"), 1e-6);
    CheckVolume(Moved(c, large, 20.0, "scaled x20"), 1e-6);
  }
}
