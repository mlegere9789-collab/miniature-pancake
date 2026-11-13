// QC: the outward-orientation safety net of the sweep factories on a small body FAR FROM THE ORIGIN.
//
// Every sweep factory ends with "tessellate, and if the signed volume of the mesh is negative flip the faces" (ExtrudeFace, Thicken,
// the welded siblings, Revolve ...). An ON_Mesh stores float32 vertices (relative precision 6e-8 of the COORDINATE): a 3 x 4 sheet
// thickened by 0.5e-3 and sitting 1e5 from the origin has no thickness left in the mesh (the volume reads exactly 0), the flip is
// skipped, and the factory returns an INWARD-oriented solid that is valid, IsSolid() and has a NEGATIVE Brep::Volume().
// Measured before the fix (Brep::Volume(), expected +6e-9 / +1.8e-8):
//   ThickenWelded of a 3e-3 x 4e-3 sheet by 0.5e-3 at 1e5:  -6.00000002e-09 (all 16 flip/sign/symmetric variants), at 1e7: -6.00000295e-09
//   ExtrudeFaceWelded of a bulged 3e-3 sheet by 2e-3 at 1e5: -1.8e-08 for two of the four variants
//   ThickenWelded of a 3 x 4 sheet by 0.5 symmetric at 1e7 (flipped sheet): -6
//   ShellRevolvedProfile of a can 5e-3 x 10e-3 at 1e5: refused "the revolved shell's exact volume -0.000000 differs from Green's integral"
// Oracles: the volume of a thickened flat sheet is area * thickness; of an extruded graph z = f(x, y) it is |d.z| * area (Cavalieri);
// the revolved can's by Green's integral pi * (R^2 H - r^2 h) of its meridian section.
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

std::string S(double v) {
  char b[48];
  std::snprintf(b, sizeof(b), "%.9g", v);
  return b;
}

Brep Bulged(double s, double ox, double oy, double oz, bool flip) {
  std::vector<Point3d> grid;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      const int a = flip ? j : i, b = flip ? i : j;
      grid.push_back(Point3d(ox + s * a, oy + s * b, oz + s * ((i == 2 && j == 2) ? 3.0 : 0.0)));
    }
  return Brep::FromSurface(NurbsSurface::FromControlGrid(grid, 4, 4, 3, 3));
}

Brep Flat(double s, double ox, double oy, double oz, bool flip) {
  std::vector<Point3d> g;
  for (int i = 0; i < 2; ++i)
    for (int j = 0; j < 2; ++j) {
      const int a = flip ? j : i, b = flip ? i : j;
      g.push_back(Point3d(ox + 3 * s * a, oy + 4 * s * b, oz));
    }
  return Brep::FromSurface(NurbsSurface::FromControlGrid(g, 2, 2, 1, 1));
}

// Volume() of a valid closed solid, checked against the closed form to `tol` relative; reports the exact numbers.
bool VolumeIs(const Brep& b, double expect, double tol, const std::string& tag) {
  double v = std::nan("");
  std::string why;
  try {
    v = b.Volume();
  } catch (const std::exception& e) {
    why = e.what();
  }
  const bool good = b.raw().IsValid() && b.raw().IsSolid() && std::fabs(v - expect) <= tol * expect;
  d8test::Check(good, (tag + ": valid closed solid with Volume() " + S(v) + " = closed form " + S(expect) + " " + why).c_str());
  return good;
}

}  // namespace

D8_EXTRA_TEST(QcThickenWeldedTinySheetFarFromTheOriginIsOutwardOriented) {
  for (const double s : {1e-3}) {
    for (const double off : {1e5, 1e7}) {
      for (const int flip : {0, 1}) {
        for (const double th : {0.5, -0.5}) {
          for (const int sym : {0, 1}) {
            const std::string tag = "ThickenWelded 3x4 sheet s=" + S(s) + " at " + S(off) + " flip=" + std::to_string(flip) + " t=" + S(th) + " sym=" + std::to_string(sym);
            try {
              const Brep o = Brep::ThickenWelded(Flat(s, off, -off, 0.5 * off, flip != 0), th * s, sym != 0);
              VolumeIs(o, 12 * s * s * 0.5 * s, 1e-4, tag);
            } catch (const std::exception& e) {
              d8test::Check(false, (tag + ": threw " + e.what()).c_str());
            }
          }
        }
      }
    }
  }
}

D8_EXTRA_TEST(QcThickenWeldedUnitSheetAtTenMillionIsOutwardOriented) {
  for (const int flip : {0, 1}) {
    for (const double th : {0.5, -0.5}) {
      for (const int sym : {0, 1}) {
        const std::string tag = "ThickenWelded 3x4 sheet at 1e7 flip=" + std::to_string(flip) + " t=" + S(th) + " sym=" + std::to_string(sym);
        try {
          const Brep o = Brep::ThickenWelded(Flat(1.0, 1e7, -1e7, 0.5e7, flip != 0), th, sym != 0);
          VolumeIs(o, 6.0, 1e-6, tag);
        } catch (const std::exception& e) {
          d8test::Check(false, (tag + ": threw " + e.what()).c_str());
        }
      }
    }
  }
}

D8_EXTRA_TEST(QcExtrudeFaceWeldedTinyBulgedSheetFarFromTheOriginIsOutwardOriented) {
  const double s = 1e-3;
  for (const double off : {1e5, 1e7}) {
    for (const int flip : {0, 1}) {
      for (const double h : {2.0, -2.0}) {
        const std::string tag = "ExtrudeFaceWelded bulged sheet s=" + S(s) + " at " + S(off) + " flip=" + std::to_string(flip) + " h=" + S(h);
        try {
          const Brep o = Brep::ExtrudeFaceWelded(Bulged(s, off, -off, 0.5 * off, flip != 0), 0, Vector3d(0, 0, h * s));
          VolumeIs(o, 9 * s * s * 2 * s, 1e-4, tag);
        } catch (const std::exception& e) {
          d8test::Check(false, (tag + ": threw " + e.what()).c_str());
        }
      }
    }
  }
}

D8_EXTRA_TEST(QcShellRevolvedProfileTinyCanFarFromTheOrigin) {
  const double s = 1e-3, off = 1e5, R = 5, H = 10;
  auto P = [&](double r, double z) { return Point3d(off + s * r, -off, 0.5 * off + s * z); };
  const NurbsCurve section = NurbsCurve::FromControlPoints({P(0, 0), P(R, 0), P(R, H), P(0, H), P(0, 0)}, 1);
  // thickness 1 on the bottom and the wall, the top (edge 2) removed, axis edge 0: cavity r < 4, z in [1, 10]
  Brep shell;
  std::string why;
  try {
    shell = dino8::kernel::ShellRevolvedProfile(section, P(0, 0), Vector3d(0, 0, 1), {1.0 * s, 1.0 * s, 0, 0});
  } catch (const std::exception& e) {
    why = e.what();
  }
  d8test::Check(why.empty(), ("ShellRevolvedProfile of a 5e-3 x 10e-3 can at 1e5 builds (" + why + ")").c_str());
  if (!why.empty()) return;
  VolumeIs(shell, (ON_PI * 25 * 10 - ON_PI * 16 * 9) * s * s * s, 1e-4, "shelled can s=1e-3 at 1e5");
}
