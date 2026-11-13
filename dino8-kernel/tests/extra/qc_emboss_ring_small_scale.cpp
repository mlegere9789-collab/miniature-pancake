// QC: EmbossOnCurvedFace's ring tool is cut with BooleanCombineGeneralSealed(tool, hole, Difference) at the engine's default ABSOLUTE
// tolerance 0.001 (the same defect as MakeRib at small scale, see qc_rib_small_scale.cpp). The ring-on-a-sphere closed forms of
// emboss_curved_tests.cpp at 1/10 and 1/100 scale (R = 0.05 / 0.5, ring radii 0.01 / 0.02) must scale by s^3.
#include <cmath>
#include <cstdio>
#include <string>

#include <opennurbs.h>

#include "dino8/kernel/boss.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::EmbossMode;
using dino8::kernel::EmbossOnCurvedFace;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

NurbsCurve CircleCurve(const ON_3dPoint& c, double r) {
  const ON_Circle circle(ON_Plane(c, ON_3dVector(0, 0, 1)), r);
  ON_NurbsCurve nurbs;
  circle.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

double RingAboveSphere(double R, double a, double b, double zp) {
  return ON_PI * zp * (b * b - a * a) - (2.0 * ON_PI / 3.0) * (std::pow(R * R - a * a, 1.5) - std::pow(R * R - b * b, 1.5));
}

}  // namespace

D8_EXTRA_TEST(QcEmbossRingOnSphereSmallScaleMatchesClosedForm) {
  for (const double s : {1.0, 0.1, 0.01}) {
    const std::string tag = "ring emboss on a sphere, scale " + std::to_string(s) + ": ";
    const double R = 5 * s, a = 1 * s, b = 2 * s;
    try {
      const auto e = EmbossOnCurvedFace(Brep::Sphere(Point3d(0, 0, 0), R), CircleCurve(ON_3dPoint(0, 0, 4 * s), b), {CircleCurve(ON_3dPoint(0, 0, 4 * s), a)},
                                        Vector3d(0, 0, -1), 1.5 * s, EmbossMode::Emboss);
      const double expect = RingAboveSphere(R, a, b, 5.5 * s);
      d8test::Check(e.brep.raw().IsValid() && e.brep.raw().IsSolid(), (tag + "valid closed solid").c_str());
      d8test::Check(std::fabs(e.volume_change - expect) <= 0.01 * expect, (tag + "added " + std::to_string(e.volume_change) + " vs closed form " + std::to_string(expect)).c_str());
    } catch (const std::exception& e) {
      d8test::Check(false, (tag + "refused: " + e.what()).c_str());
    }
  }
}
