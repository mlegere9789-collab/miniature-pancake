// QC: MakeRib on a SMALL model returns a valid, closed, WRONG solid.
//
// MakeRib / MakeBoss / MakePocket fuse their tool with BooleanCombineGeneralSealed(solid, tool, op) and never passed a tolerance, so the
// engine's default 0.001 - an ABSOLUTE surface-surface Newton accuracy - applied at every scale. The rib test's own part (a 10 x 10 x 1
// plate, a 6 x 3.2 web 0.4 thick, closed form V = 100 + 6 * 3 * 0.4 = 107.2) at 1/100 scale (web 0.004 thick, tolerance 0.001 = a quarter of
// it) came back IsValid() and IsSolid() with VolumeOfSealedSolid() 1.0674e-4 against the closed form 1.072e-4 (0.43 percent short;
// 0.55 percent with the part moved 1e5 away), and at 1/1000 scale / 1/10 scale the engine refused the sealing outright (see the direct
// BooleanCombineGeneralSealed measurements in the commit message). Oracle: the closed form, and the same part at scale 1 (exact to 8e-11).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/rib.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::MakeRib;
using dino8::kernel::Point3d;
using dino8::kernel::RibResult;

std::string S(double v) {
  char b[48];
  std::snprintf(b, sizeof(b), "%.9g", v);
  return b;
}

// plate [0,10]x[0,10]x[0,1] * s, web in the plane y = 5 s: x 2..8, z 0.8..4, `thickness` thick (in units of s)
void RibCase(double s, double off, double thickness) {
  const std::string tag = "plate 10x10x1 scale " + S(s) + " moved " + S(off) + ", web " + S(thickness) + " thick: ";
  auto P = [&](double x, double y, double z) { return Point3d(off + s * x, -off + s * y, 0.5 * off + s * z); };
  const Brep plate = Brep::Box(off, -off, 0.5 * off, off + 10 * s, -off + 10 * s, 0.5 * off + 1 * s);
  RibResult r;
  try {
    r = MakeRib(plate, {P(2, 5, 0.8), P(8, 5, 0.8), P(8, 5, 4), P(2, 5, 4)}, thickness * s);
  } catch (const std::exception& e) {
    d8test::Check(false, (tag + "MakeRib threw " + e.what()).c_str());
    return;
  }
  const double want = (100.0 + 6.0 * 3.0 * thickness) * s * s * s;
  const double v = r.brep.VolumeOfSealedSolid();
  d8test::Check(r.brep.raw().IsValid() && r.brep.raw().IsSolid(), (tag + "valid closed solid").c_str());
  d8test::Check(std::fabs(v - want) < 1e-6 * want, (tag + "volume " + S(v) + " vs closed form " + S(want)).c_str());
  d8test::Check(std::fabs(r.added_volume - 6.0 * 3.0 * thickness * s * s * s) < 1e-4 * 18.0 * thickness * s * s * s,
                (tag + "added_volume " + S(r.added_volume) + " vs web above the plate " + S(18.0 * thickness * s * s * s)).c_str());
}

}  // namespace

D8_EXTRA_TEST(QcRibOnPlateSmallScaleMatchesClosedForm) {
  RibCase(1.0, 0.0, 0.4);     // control: the rib test's own part
  RibCase(0.1, 0.0, 0.4);
  RibCase(0.01, 0.0, 0.4);
  RibCase(0.01, 1e5, 0.4);
  RibCase(0.001, 0.0, 0.4);
}

D8_EXTRA_TEST(QcRibOnPlateThinWebAtUnitScaleMatchesClosedForm) {
  RibCase(1.0, 0.0, 0.004);   // a 4 mm web on a 10 m plate: the same ratio to the default tolerance as the 1/100 case
}
