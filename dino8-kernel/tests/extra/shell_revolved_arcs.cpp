// ShellRevolvedArcProfile(): shell WITH face removal of a solid of revolution whose meridian has circular arcs
// (sphere bowl, sphere-cap dome, half torus, a can with a rounded bottom).
//
// Oracles are independent of the construction (which intersects moved curves and integrates Green's theorem):
//  - hemisphere bowl: (2/3) pi (R^3 - (R-t)^3);
//  - dome (sphere cap on a flat base, base removed): difference of two spherical-cap volumes pi h^2 (3 rho - h)/3;
//  - half torus cut along its equator plane: Pappus (the section is symmetric about r = 4, so V = 2 pi * 4 * area);
//  - rounded-bottom can: cylinder ring + hemisphere shell closed forms (a TANGENT line/arc junction);
//  - point membership of the tessellation against the same shell written in sphere / torus coordinates.
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::MeridianEdge;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::ShellRevolvedArcProfile;
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

Brep Build(const std::vector<MeridianEdge>& e, const std::vector<double>& th, std::string* what = nullptr, bool* threw = nullptr) {
  Brep b;
  try {
    b = ShellRevolvedArcProfile(e, Point3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), th);
    if (threw) *threw = false;
  } catch (const std::exception& ex) {
    if (what) *what = ex.what();
    if (threw) *threw = true;
  }
  return b;
}

// Builds, then checks validity / exact volume / tessellated volume / membership against `material(x, y, z)` (-1 = skip).
void CheckShell(const std::string& tag, const std::vector<MeridianEdge>& e, const std::vector<double>& th, double expected,
                const std::function<int(double, double, double)>& material, double box) {
  std::string what;
  bool threw = false;
  const Brep shell = Build(e, th, &what, &threw);
  d8test::Check(!threw, (tag + ": builds (" + what + ")").c_str());
  if (threw) return;
  d8test::Check(shell.raw().IsValid() && shell.raw().IsSolid(), (tag + ": IsValid() and IsSolid()").c_str());
  const double vol = shell.Volume();
  // Brep::Volume() is a numerical quadrature on the rational faces: measured 1.8e-6 relative off on the hemisphere bowl.
  d8test::Check(std::fabs(vol - expected) <= 1e-5 * expected,
                (tag + ": exact Brep::Volume() " + std::to_string(vol) + " = independent closed form " + std::to_string(expected)).c_str());
  const Mesh mesh = shell.TessellateToClosedMesh(64, 96);
  const double mv = mesh.Volume();
  d8test::Check(mv < expected * 1.0005 && mv > 0.985 * expected,
                (tag + ": tessellated closed-mesh volume within 1.5% of the closed form (" + std::to_string(mv) + ")").c_str());
  int judged = 0, bad = 0;
  const int n = 30;
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      for (int k = 0; k < n; ++k) {
        const double x = -box + 2 * box * (i + 0.37) / n, y = -box + 2 * box * (j + 0.41) / n, z = -box + 2 * box * (k + 0.29) / n;
        const int want = material(x, y, z);
        if (want < 0) continue;
        ++judged;
        if (mesh.ContainsPoint(Point3d(x, y, z)) != (want == 1)) ++bad;
      }
    }
  }
  d8test::Check(judged > 3000 && bad == 0,
                (tag + ": point membership agrees with the sphere/torus coordinate form at " + std::to_string(judged) + " points, mismatches " + std::to_string(bad)).c_str());
}

// 1 inside [lo, hi], 0 outside, -1 within `m` of either bound.
int Band(double q, double lo, double hi, double m) {
  if (std::fabs(q - lo) < m || std::fabs(q - hi) < m) return -1;
  return q > lo && q < hi ? 1 : 0;
}

}  // namespace

D8_EXTRA_TEST(ShellRevolvedArcProfileHemisphereBowlAndDome) {
  {  // hemisphere R=5, flat top removed
    const double R = 5, t = 1;
    // e0 outer arc (5,0) -> (0,-5) about the origin (clockwise), e1 axis, e2 flat top (removed)
    const std::vector<MeridianEdge> e = {A(R, 0, 0, 0, false), L(0, -R), L(0, 0)};
    CheckShell("hemisphere bowl R5 t1", e, {t, 0, 0}, 2.0 / 3.0 * ON_PI * (R * R * R - (R - t) * (R - t) * (R - t)),
               [&](double x, double y, double z) {
                 const double rho = std::sqrt(x * x + y * y + z * z);
                 if (std::fabs(z) < 0.12) return -1;
                 if (z > 0) return 0;
                 return Band(rho, R - t, R, 0.12);
               },
               R + 1);
  }
  {  // dome: sphere radius 5 centred (0,-3); base disc at z=0 (r=4) removed
    const double R = 5, cz = -3, t = 1;
    const std::vector<MeridianEdge> e = {L(0, 0), A(4, 0, 0, cz, true), L(0, 2)};  // base, arc up to the apex (0,2), axis down
    // base edge is e0 -> (4,0); arc e1 (4,0)->(0,2); axis e2 (0,2)->(0,0)
    auto cap = [](double rho, double h) { return ON_PI * h * h * (3 * rho - h) / 3.0; };
    const double expected = cap(R, R + cz) - cap(R - t, R - t + cz);
    CheckShell("dome (sphere cap on a flat base, base removed) R5 t1", e, {0, t, 0}, expected,
               [&](double x, double y, double z) {
                 const double rho = std::sqrt(x * x + y * y + (z - cz) * (z - cz));
                 if (std::fabs(z) < 0.12) return -1;
                 if (z < 0) return 0;
                 return Band(rho, R - t, R, 0.12);
               },
               R + 1);
  }
}

D8_EXTRA_TEST(ShellRevolvedArcProfileHalfTorusPappus) {
  const double cr = 4, a = 2, t = 0.5;
  // half disc of radius 2 about (4, 0): upper arc (6,0) -> (2,0) counter-clockwise, then the chord (2,0) -> (6,0) removed
  const std::vector<MeridianEdge> e = {A(cr + a, 0, cr, 0, true), L(cr - a, 0)};
  const double area = ON_PI * (a * a - (a - t) * (a - t)) / 2.0;
  CheckShell("half torus (major 4, minor 2), flat face removed, t=0.5", e, {t, 0}, 2.0 * ON_PI * cr * area,
             [&](double x, double y, double z) {
               if (std::fabs(z) < 0.12) return -1;
               if (z < 0) return 0;
               const double d = std::sqrt((std::hypot(x, y) - cr) * (std::hypot(x, y) - cr) + z * z);
               return Band(d, a - t, a, 0.12);
             },
             7.0);
}

D8_EXTRA_TEST(ShellRevolvedArcProfileRoundedBottomCanTangentJunction) {
  const double R = 3, H = 6, t = 0.5;
  // wall (3,6)->(3,0), hemispherical bottom arc (3,0)->(0,-3) about the origin (clockwise; TANGENT to the wall), axis, open top
  const std::vector<MeridianEdge> e = {L(R, H), A(R, 0, 0, 0, false), L(0, -R), L(0, H)};
  const double expected = ON_PI * (R * R - (R - t) * (R - t)) * H + 2.0 / 3.0 * ON_PI * (R * R * R - (R - t) * (R - t) * (R - t));
  CheckShell("can with a hemispherical bottom (tangent wall/arc), top removed, t=0.5", e, {t, t, 0, 0}, expected,
             [&](double x, double y, double z) {
               const double r = std::hypot(x, y);
               if (std::fabs(z) < 0.12 || std::fabs(z - H) < 0.12) return -1;
               if (z > H) return 0;
               if (z >= 0) return Band(r, R - t, R, 0.12);
               return Band(std::sqrt(x * x + y * y + z * z), R - t, R, 0.12);
             },
             7.0);
}

D8_EXTRA_TEST(ShellRevolvedArcProfileRefusalsAreHonest) {
  std::string what;
  bool threw = false;
  const double R = 5;
  const std::vector<MeridianEdge> bowl = {A(R, 0, 0, 0, false), L(0, -R), L(0, 0)};
  Build(bowl, {1, 0, 0}, &what, &threw);
  d8test::Check(!threw, "baseline: the hemisphere bowl builds");
  Build(bowl, {5, 0, 0}, &what, &threw);
  d8test::Check(threw && what.find("collapses") != std::string::npos, "wall as thick as the sphere radius: refused, naming the collapse");
  Build(bowl, {7, 0, 0}, &what, &threw);
  d8test::Check(threw, "wall thicker than the sphere radius: refused");
  Build(bowl, {1, 1, 0}, &what, &threw);
  d8test::Check(threw && what.find("axis") != std::string::npos, "positive thickness on the axis edge: refused, naming the axis");
  Build(bowl, {1, 0, 1}, &what, &threw);
  d8test::Check(threw, "the flat top kept and the axis edge between kept faces: refused (the axis edge is a zero run of its own)");
  Build(bowl, {1, 0}, &what, &threw);
  d8test::Check(threw, "wrong-sized thickness vector refused");
  Build(bowl, {1, 1, 1}, &what, &threw);
  d8test::Check(threw, "nothing removed: refused (a closed hollow is two lumps)");
  Build({A(R, 0, 0, 0, false), L(0, -R), L(0, 0)}, {std::nan(""), 0, 0}, &what, &threw);
  d8test::Check(threw, "NaN thickness refused");
  // An arc whose ends are not on one circle.
  Build({A(R, 0, 0, 0, false), L(0, -4), L(0, 0)}, {1, 0, 0}, &what, &threw);
  d8test::Check(threw && what.find("one circle") != std::string::npos, "arc end points not on one circle: refused, naming it");
  // An arc crossing the axis.
  Build({A(2, 0, 0, 0, true), L(-2, 0)}, {0.5, 0}, &what, &threw);
  d8test::Check(threw, "an arc that crosses to r < 0: refused");
  // Capsule with different wall / bottom thickness: the moved line (r = 2.5) and the moved circle (radius 2) do not meet.
  const std::vector<MeridianEdge> can = {L(3, 6), A(3, 0, 0, 0, false), L(0, -3), L(0, 6)};
  Build(can, {0.5, 0.5, 0, 0}, &what, &threw);
  d8test::Check(!threw, "baseline: the rounded-bottom can builds at equal thickness");
  // Wall 0.5 / hemisphere 1.0 on a TANGENT junction: the moved line (r = 2.5) never reaches the moved circle (radius 2), so the
  // cavity steps along the common normal - covered by ShellRevolvedArcProfileSteppedThickness (it builds; it is not a refusal).
  // A NON-tangent corner whose moved curves do not meet has no honest step: refused. Dome (radius 5 about the origin) on a wall
  // r = 3 meeting it at (3, 4) at an angle; wall 0.1 moves the line to r = 2.9, dome 2.5 shrinks the circle to radius 2.5.
  Build({L(0, 0), L(3, 0), A(3, 4, 0, 0, true), L(0, 5)}, {0, 0.1, 2.5, 0}, &what, &threw);
  d8test::Check(threw && what.find("not smooth enough") != std::string::npos,
                "non-tangent corner whose moved curves do not meet: refused, naming the missing smooth junction");
  // Two separate removed runs.
  Build({L(3, 0), A(3, 0, 0, 0, true), L(0, 3), L(3, 0)}, {0, 1, 0, 1}, &what, &threw);
  d8test::Check(threw, "malformed or split input is refused, not built");
}

D8_EXTRA_TEST(ShellRevolvedArcProfileSteppedThickness) {
  // Tangent wall/arc junction with a THICKER hemisphere: the cavity steps from radius 2.5 (wall, t=0.5) to radius 2 (hemisphere,
  // t=1) along the common normal at z = 0, a ledge. Closed form: ring pi (9 - 6.25) * 6 plus hemisphere shell (2/3) pi (27 - 8).
  const double R = 3, H = 6, tw = 0.5, tb = 1.0;
  const std::vector<MeridianEdge> e = {L(R, H), A(R, 0, 0, 0, false), L(0, -R), L(0, H)};
  const double expected = ON_PI * (R * R - (R - tw) * (R - tw)) * H + 2.0 / 3.0 * ON_PI * (R * R * R - (R - tb) * (R - tb) * (R - tb));
  CheckShell("can with a 0.5 wall and a 1.0 hemispherical bottom (stepped ledge at the tangent junction)", e, {tw, tb, 0, 0}, expected,
             [&](double x, double y, double z) {
               const double r = std::hypot(x, y);
               if (std::fabs(z) < 0.12 || std::fabs(z - H) < 0.12) return -1;
               if (z > H) return 0;
               if (z >= 0) return Band(r, R - tw, R, 0.12);
               return Band(std::sqrt(x * x + y * y + z * z), R - tb, R, 0.12);
             },
             7.0);
  {  // the reverse (thick wall 1.0, thin hemisphere 0.5): the moved wall line (r = 2) DOES meet the moved circle (radius 2.5), at
     // z = -sqrt(2.5^2 - 2^2) = -1.5, so the 1.0 wall simply continues below the junction (extend-and-intersect corner, no step).
     // Cavity = cylinder r2 over z in [-1.5, 6] + the radius-2.5 sphere cap below z = -1.5 (depth 1): pi*4*7.5 + pi*1*(7.5-1)/3.
    const double outer = ON_PI * 9 * 6 + 2.0 / 3.0 * ON_PI * 27;
    const double cav = ON_PI * 4 * 7.5 + ON_PI * 1.0 * (3 * 2.5 - 1.0) / 3.0;
    CheckShell("can with a 1.0 wall and a 0.5 hemispherical bottom (extend-and-intersect corner at z = -1.5)", e, {1.0, 0.5, 0, 0}, outer - cav,
               [&](double x, double y, double z) {
                 const double r = std::hypot(x, y);
                 if (std::fabs(z) < 0.12 || std::fabs(z - H) < 0.12 || std::fabs(z + 1.5) < 0.12) return -1;
                 if (z > H) return 0;
                 if (z >= 0) return Band(r, 2.0, R, 0.12);
                 if (z > -1.5) return Band(r, 2.0, std::sqrt(R * R - z * z), 0.12);
                 return Band(std::sqrt(x * x + y * y + z * z), 2.5, R, 0.12);
               },
               7.0);
  }
  {  // the thicker hemisphere shell (stepped body) leaves more material than the equal 0.5 one
    const Brep stepped = Build(e, {tw, tb, 0, 0});
    const Brep even = Build(e, {tw, tw, 0, 0});
    d8test::Check(stepped.Volume() > even.Volume(), "the thicker hemisphere shell leaves more material than the equal 0.5 one");
  }
}
