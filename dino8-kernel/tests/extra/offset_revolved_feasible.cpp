// MaxInwardOffsetRevolvedArcProfile(): the largest inward offset of a solid of revolution that still builds, against closed-form
// collapse distances.
//  - cylinder R, H: min(R, H/2);   - cone r, h: the axial inradius r h / (r + sqrt(r^2 + h^2)) of its meridian triangle;
//  - sphere cap on a base (centre z = zc, radius rho): the cap height rho + zc shrinks by 2d, so it collapses at (rho + zc)/2;
//  - hemisphere R: R/2;   - half torus (arc radius a on the chord plane through its centre): a - d = d, i.e. a/2;
//  - stepped shaft (r5 z 0..4, r3 z 4..8): the lower cylinder's height 4 - 2d reaches 0 at d = 2 (before the upper radius 3 - d does).
// Each is also checked from both sides: the offset just below the answer builds, just above it is refused.
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::MaxInwardOffsetRevolvedArcProfile;
using dino8::kernel::MeridianEdge;
using dino8::kernel::OffsetRevolvedArcProfile;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

MeridianEdge L(double r, double z) {
  MeridianEdge e;
  e.r = r;
  e.z = z;
  return e;
}
MeridianEdge A(double r, double z, double cr, double cz, bool ccw) {
  MeridianEdge e;
  e.r = r;
  e.z = z;
  e.arc = true;
  e.cr = cr;
  e.cz = cz;
  e.counter_clockwise = ccw;
  return e;
}

void CheckLimit(const std::string& tag, const std::vector<MeridianEdge>& e, double expected) {
  const Point3d o(0, 0, 0);
  const Vector3d ax(0, 0, 1), ex(1, 0, 0);
  double got = -1;
  bool threw = false;
  try {
    got = MaxInwardOffsetRevolvedArcProfile(e, o, ax, ex);
  } catch (const std::exception&) {
    threw = true;
  }
  d8test::Check(!threw && std::fabs(got - expected) <= 1e-4 * expected,
                (tag + ": largest feasible inward offset " + std::to_string(got) + " = closed-form collapse distance " + std::to_string(expected)).c_str());
  if (threw) return;
  auto builds = [&](double d) {
    try {
      OffsetRevolvedArcProfile(e, o, ax, ex, -d);
      return true;
    } catch (const std::exception&) {
      return false;
    }
  };
  // Geometric feasibility is what is measured; the revolve of a body only 0.1% from collapse can itself fail numerically, so the "builds"
  // side is checked a little further from the limit (1%) and the refused side just past it.
  d8test::Check(builds(expected * 0.99) && !builds(expected * 1.001), (tag + ": the offset 1% below the limit builds and just above it is refused").c_str());
}

}  // namespace

D8_EXTRA_TEST(OffsetRevolvedFeasibleRangeMatchesClosedFormCollapse) {
  CheckLimit("cylinder R5 H6", {L(0, 0), L(5, 0), L(5, 6), L(0, 6)}, 3.0);
  CheckLimit("cylinder R2 H10 (the radius collapses first)", {L(0, 0), L(2, 0), L(2, 10), L(0, 10)}, 2.0);
  const double r = 5, h = 10;
  CheckLimit("cone r5 h10 (axial inradius)", {L(0, 0), L(r, 0), L(0, h)}, r * h / (r + std::hypot(r, h)));
  {  // dome: sphere radius 5 centred z = -2, flat base at z = 0 (cap height 3)
    const double cz = -2, R = 5, w = std::sqrt(R * R - cz * cz);
    CheckLimit("dome R5 cap height 3", {L(0, 0), A(w, 0, 0, cz, true), L(0, cz + R)}, (R + cz) / 2.0);
  }
  CheckLimit("hemisphere R5", {L(0, 0), A(5, 0, 0, 0, true), L(0, 5)}, 2.5);
  CheckLimit("half torus major 4 minor 2", {A(6, 0, 4, 0, true), L(2, 0)}, 1.0);
  CheckLimit("stepped shaft r5 z0..4 + r3 z4..8", {L(0, 0), L(5, 0), L(5, 4), L(3, 4), L(3, 8), L(0, 8)}, 2.0);
}

D8_EXTRA_TEST(OffsetRevolvedFeasibleRangeRefusesAnInvalidMeridian) {
  const Point3d o(0, 0, 0);
  const Vector3d ax(0, 0, 1), ex(1, 0, 0);
  d8test::Check(
      [&] {
        try {
          MaxInwardOffsetRevolvedArcProfile({L(0, 0), L(5, 0)}, o, ax, ex);
        } catch (const std::exception&) {
          return true;
        }
        return false;
      }(),
      "a two-edge degenerate meridian (a line and back) is refused");
  d8test::Check(
      [&] {
        try {
          MaxInwardOffsetRevolvedArcProfile({L(0, 0), L(-1, 0), L(-1, 1), L(0, 1)}, o, ax, Vector3d(0, 1, 0));
          return false;
        } catch (const std::exception&) {
          return true;
        }
      }(),
      "a meridian on the wrong side of the axis (r < 0) is refused");
}
