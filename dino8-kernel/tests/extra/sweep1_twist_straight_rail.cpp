// Brep::Sweep1 with a twist along a STRAIGHT rail: the swept wall of a radial segment twisted through phi_total about the rail is a
// helicoid, S(rho, z) = (rho cos(k z), rho sin(k z), z), k = phi_total / H, with |S_rho x S_z| = sqrt(1 + k^2 rho^2) and area
//   H * int_{r0}^{r1} sqrt(1 + k^2 rho^2) d rho = H / (2k) * [k rho sqrt(1 + k^2 rho^2) + asinh(k rho)]_{r0}^{r1}.
// (Independent of the kernel's frames: closed form only.)
//
// The default (non-adaptive) call keeps its 2-station "exact extrusion" shortcut on a straight rail even with a twist; that wall is a
// flat bowtie between the untwisted and the fully twisted section and CUTS the helicoid (area 6 for the 180 degree case below, against
// 17.56 for the helicoid) - the shortcut is exact for the two ends only. The adaptive mode (tolerance > 0) measures the wall against the
// exact swept section and refines, which is what the app's Twist option uses. This test pins both: the adaptive wall is the
// helicoid, and the un-measured default is not (a negative control that documents the limit, not an endorsement of it).
#include <cmath>
#include <cstdio>
#include <string>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;

constexpr double kPi = 3.14159265358979323846;

double WallArea(const Brep& b) {
  const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(b.raw().m_S[0]);
  if (ns == nullptr) return -1.0;
  const ON_Interval du = ns->Domain(0), dv = ns->Domain(1);
  const int nu = 200, nv = 4000;
  double area = 0.0;
  for (int i = 0; i < nu; ++i) {
    for (int j = 0; j < nv; ++j) {
      ON_3dPoint p;
      ON_3dVector su, sv;
      ns->Ev1Der(du.ParameterAt((i + 0.5) / nu), dv.ParameterAt((j + 0.5) / nv), p, su, sv);
      area += ON_CrossProduct(su, sv).Length() * du.Length() / nu * dv.Length() / nv;
    }
  }
  return area;
}

double Helicoid(double H, double phi, double r0, double r1) {
  const double k = phi / H;
  auto f = [&](double r) { return (k * r * std::sqrt(1.0 + k * k * r * r) + std::asinh(k * r)) / (2.0 * k); };
  return H * (f(r1) - f(r0));
}

}  // namespace

D8_EXTRA_TEST(Sweep1TwistStraightRailAdaptiveIsTheHelicoid) {
  const NurbsCurve rail = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(0, 0, 6)}, 1);
  const NurbsCurve section = NurbsCurve::FromControlPoints({Point3d(1, 0, 0), Point3d(3, 0, 0)}, 1);
  for (const double deg : {180.0, -180.0, 90.0, 360.0}) {
    const double phi = deg * kPi / 180.0;
    bool built = true;
    double area = 0.0;
    std::string why;
    try {
      const Brep b = Brep::Sweep1(section, rail, 32, false, phi, 1.0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 1e-4 * 6.0);
      area = WallArea(b);
    } catch (const std::exception& e) {
      built = false;
      why = e.what();
    }
    const double exact = Helicoid(6.0, std::fabs(phi), 1.0, 3.0);
    const double rel = built ? std::fabs(area - exact) / exact : 1.0;
    std::printf("  [sweep1 twist] %+.0f deg: adaptive wall area %.6f, helicoid %.6f, rel %.3g %s\n", deg, area, exact, rel, why.c_str());
    d8test::Check(built, ("twist " + std::to_string(deg) + " deg adaptive sweep builds: " + why).c_str());
    d8test::Check(rel < 2e-3, ("twist " + std::to_string(deg) + " deg: the adaptive wall area is the helicoid's closed form to 2e-3").c_str());
  }
}

D8_EXTRA_TEST(Sweep1TwistStraightRailDefaultShortcutIsNotTheHelicoid) {
  const NurbsCurve rail = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(0, 0, 6)}, 1);
  const NurbsCurve section = NurbsCurve::FromControlPoints({Point3d(1, 0, 0), Point3d(3, 0, 0)}, 1);
  const Brep b = Brep::Sweep1(section, rail, 32, false, kPi);
  const double area = WallArea(b);
  const double exact = Helicoid(6.0, kPi, 1.0, 3.0);
  std::printf("  [sweep1 twist] default (2-station) wall area %.6f, helicoid %.6f\n", area, exact);
  d8test::Check(std::fabs(area - exact) > 0.1 * exact, "negative control: the default 2-station ruled shortcut with a 180 degree twist is NOT the helicoid (documented limit)");
  const Brep zero = Brep::Sweep1(section, rail, 32, false, 0.0);
  d8test::Check(std::fabs(WallArea(zero) - 12.0) < 1e-6, "zero twist: the wall is the 2 x 6 rectangle, area 12");
}
