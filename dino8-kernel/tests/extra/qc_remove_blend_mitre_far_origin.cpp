// QC: RemoveBlend of one cylinder of a FilletConvexEdgePairMitre pair on a SMALL box FAR FROM THE ORIGIN.
//
// The fillet functions took their weld/plane tolerance from fillet.cpp's RelativeTol(): 1e-9 of the largest |coordinate| - a distance from
// the ORIGIN, not the solid's size. A box 1e5 from the origin got a tolerance of 1e-4 (x 100 in RemoveBlend = 1e-2), larger than a
// whole 7e-4 fillet radius, and RemoveBlend of the equal-radius mitred pair threw "the face holding both mitred edges is not in the
// expected cut shape - not supported" for the 8e-3 x 6e-3 x 5e-3 box at (1e5, -1e5, 5e4) while the identical box at the origin and the
// unit-scale box at 1e5 were fine. Oracle (independent of the construction): removing the blend of edge A from the mitred pair leaves
// the box with ONE fillet, on edge B: V = V_box - (1 - pi/4) r^2 L_B; and the mitred pair itself V = V_box - a r^2 (L_A + L_B) + r^3 (5/3 - pi/2).
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/fillet.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::FilletConvexEdgePairMitre;
using dino8::kernel::Point3d;
using dino8::kernel::RemoveBlend;

std::string S(double v) {
  char b[48];
  std::snprintf(b, sizeof(b), "%.9g", v);
  return b;
}

void RunCase(double s, double off, double r) {
  const double Lx = 8, Ly = 6, Lz = 5;
  const std::string tag = "box 8x6x5 scale " + S(s) + " at (" + S(off) + ", " + S(-off) + ", " + S(0.5 * off) + "), r=" + S(r) + ": ";
  auto P = [&](double x, double y, double z) { return Point3d(off + s * x, -off + s * y, 0.5 * off + s * z); };
  const Brep box = Brep::Box(off, -off, 0.5 * off, off + s * Lx, -off + s * Ly, 0.5 * off + s * Lz);
  const double a = 1.0 - ON_PI / 4.0;
  Brep pair;
  try {
    pair = FilletConvexEdgePairMitre(box, P(0, 0, Lz), P(0, 0, 0), P(Lx, 0, Lz), r * s, r * s);
  } catch (const std::exception& e) {
    d8test::Check(false, (tag + "FilletConvexEdgePairMitre threw " + e.what()).c_str());
    return;
  }
  const double want_pair = s * s * s * (Lx * Ly * Lz - a * r * r * (Lz + Lx) + r * r * r * (5.0 / 3.0 - ON_PI / 2.0));
  d8test::Check(std::fabs(pair.Volume() - want_pair) < 1e-5 * want_pair, (tag + "mitred pair volume " + S(pair.Volume()) + " vs closed form " + S(want_pair)).c_str());
  // a point on the cylinder of edge A (axis (r, r)), halfway along it
  const double c = r * (1.0 - std::sqrt(0.5));
  Brep one;
  try {
    one = RemoveBlend(pair, P(c, c, 0.5 * Lz));
  } catch (const std::exception& e) {
    d8test::Check(false, (tag + "RemoveBlend of the mitred pair's edge-A blend threw " + e.what()).c_str());
    return;
  }
  const double want_one = s * s * s * (Lx * Ly * Lz - a * r * r * Lx);
  d8test::Check(one.raw().IsValid() && one.raw().IsSolid() && std::fabs(one.Volume() - want_one) < 1e-5 * want_one,
                (tag + "after RemoveBlend only edge B stays rounded: volume " + S(one.Volume()) + " vs closed form " + S(want_one)).c_str());
}

}  // namespace

D8_EXTRA_TEST(QcRemoveBlendMitrePairSmallBoxFarFromTheOrigin) {
  RunCase(1.0, 0.0, 0.7);      // control
  RunCase(1.0, 1e5, 0.7);      // control: unit scale, far
  RunCase(1e-3, 0.0, 0.7);     // control: small, at the origin
  RunCase(1e-3, 1e5, 0.7);     // the failing one
  RunCase(1e-3, 1e5, 4.9);     // radius almost the whole 5e-3 edge
}
