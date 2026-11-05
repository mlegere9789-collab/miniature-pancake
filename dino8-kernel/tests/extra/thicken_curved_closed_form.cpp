// Brep::Thicken of curved analytic sheets against exact shell volumes (no app, no Revolve cap, no Volume read-back through
// the display mesh - the kernel's own Brep::Volume() of the thickened body, compared with closed forms):
//   cylinder wall r = 2, h = 3, sweep angle a:   outer  a/2 * h * ((r+t)^2 - r^2),  inner  a/2 * h * (r^2 - (r-t)^2)
//   spherical zone R = 3, polar angles 30..60 deg (revolved a full turn):
//                                              outer  2pi/3 * ((R+t)^3 - R^3) * (cos30 - cos60), inner with (R-t)
// A positive and a negative distance go to opposite sides of the sheet (the sign follows the surface's own normal, which is
// not assumed here), so the +t / -t pair must be {outer, inner} in either order; Symmetric = the (r+t/2)..(r-t/2) shell.
#include <cmath>
#include <cstdio>
#include <string>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsSurface;

constexpr double kPi = 3.14159265358979323846;

// Surface of revolution of `profile` about the z axis through `angle` radians, as an exact NURBS form.
NurbsSurface Revolved(const ON_Curve& profile, double angle) {
  ON_Line axis(ON_3dPoint(0, 0, 0), ON_3dPoint(0, 0, 1));
  ON_RevSurface* rev = ON_RevSurface::New();
  rev->m_curve = profile.DuplicateCurve();
  rev->m_axis = axis;
  rev->m_angle = ON_Interval(0.0, angle);
  rev->m_t = rev->m_angle;
  rev->m_bTransposed = false;
  ON_NurbsSurface ns;
  rev->GetNurbForm(ns);
  delete rev;
  NurbsSurface out;
  out.raw() = ns;
  return out;
}

double ThickVolume(const NurbsSurface& sheet, double t, bool symmetric, bool* built) {
  *built = true;
  try {
    return Brep::Thicken(Brep::FromSurface(sheet), t, symmetric).Volume();
  } catch (const std::exception& e) {
    std::printf("  Thicken(%g, sym=%d) threw: %s\n", t, symmetric ? 1 : 0, e.what());
    *built = false;
    return 0.0;
  }
}

bool Near(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::fabs(b); }

void CheckPair(const NurbsSurface& sheet, double out, double in, double sym, const std::string& tag, double rel) {
  bool b1, b2, b3;
  const double vp = ThickVolume(sheet, 0.2, false, &b1);
  const double vn = ThickVolume(sheet, -0.2, false, &b2);
  const double vs = ThickVolume(sheet, 0.4, true, &b3);
  std::printf("  [thicken curved] %s: +0.2 -> %.6f, -0.2 -> %.6f (outer %.6f, inner %.6f); symmetric 0.4 -> %.6f (%.6f)\n", tag.c_str(), vp, vn, out, in, vs, sym);
  d8test::Check(b1 && b2 && b3, (tag + ": +0.2, -0.2 and symmetric 0.4 all build").c_str());
  d8test::Check((Near(vp, out, rel) && Near(vn, in, rel)) || (Near(vp, in, rel) && Near(vn, out, rel)),
                (tag + ": +0.2 / -0.2 give the outer and inner closed-form shell volumes, one each").c_str());
  d8test::Check(Near(vs, sym, rel), (tag + ": symmetric 0.4 gives the closed-form (r+0.2)..(r-0.2) shell volume").c_str());
  d8test::Check(vp > 0.0 && vn > 0.0 && std::fabs(vp - vn) > 0.01 * vp, (tag + ": the two sides differ (not the filled body twice)").c_str());
}

}  // namespace

D8_EXTRA_TEST(ThickenCylinderWallClosedForm) {
  for (const double turns : {1.0, 0.75}) {
    const ON_Line wall_line(ON_3dPoint(2, 0, 0), ON_3dPoint(2, 0, 3));
    ON_LineCurve lc(wall_line);
    const NurbsSurface sheet = Revolved(lc, 2.0 * kPi * turns);
    const double out = turns * kPi * 3.0 * (2.2 * 2.2 - 4.0), in = turns * kPi * 3.0 * (4.0 - 1.8 * 1.8), sym = turns * kPi * 3.0 * (2.2 * 2.2 - 1.8 * 1.8);
    CheckPair(sheet, out, in, sym, turns == 1.0 ? "cylinder wall, full turn" : "cylinder wall, 270 degrees", 2e-3);
  }
}

D8_EXTRA_TEST(ThickenSphereZoneClosedForm) {
  const double a0 = 30.0 * kPi / 180.0, a1 = 60.0 * kPi / 180.0, R = 3.0;
  ON_Arc arc(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 0, 1)), R), ON_Interval(kPi / 2.0 - a1, kPi / 2.0 - a0));
  ON_ArcCurve ac(arc);
  const NurbsSurface sheet = Revolved(ac, 2.0 * kPi);
  const double c = std::cos(a0) - std::cos(a1);
  const double out = 2.0 * kPi / 3.0 * (3.2 * 3.2 * 3.2 - 27.0) * c, in = 2.0 * kPi / 3.0 * (27.0 - 2.8 * 2.8 * 2.8) * c,
               sym = 2.0 * kPi / 3.0 * (3.2 * 3.2 * 3.2 - 2.8 * 2.8 * 2.8) * c;
  CheckPair(sheet, out, in, sym, "spherical zone R=3, polar 30..60 degrees", 5e-3);
}
