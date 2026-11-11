// Emboss / deboss with holes onto CURVED faces (EmbossOnCurvedFace). Expected volume changes are independent closed forms of "ring
// column of material between the reference plane and the curved face"; containment is probed with ContainsPointExact on points whose
// side is known from the construction.
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/boss.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::EmbossMode;
using dino8::kernel::EmbossOnCurvedFace;
using dino8::kernel::EmbossResult;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }
bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
bool Inside(const Brep& b, const Point3d& p) { return dino8::kernel::ContainsPointExact(p, b.raw()); }

NurbsCurve CircleCurve(const ON_3dPoint& c, const ON_3dVector& n, double r) {
  const ON_Circle circle(ON_Plane(c, n), r);
  ON_NurbsCurve nurbs;
  circle.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

bool Throws(const std::function<void()>& fn, const std::string& needle, bool* invalid = nullptr) {
  try {
    fn();
  } catch (const std::invalid_argument& e) {
    if (invalid) *invalid = true;
    return std::string(e.what()).find(needle) != std::string::npos;
  } catch (const std::exception& e) {
    if (invalid) *invalid = false;
    return std::string(e.what()).find(needle) != std::string::npos;
  }
  return false;
}

// Exact integral over a sphere-cap ring of the vertical gap between the plane z = zp and the sphere's top surface (R): the volume of
// {a < rho < b, sqrt(R^2 - rho^2) < z < zp} = pi zp (b^2 - a^2) - (2 pi / 3) ((R^2 - a^2)^(3/2) - (R^2 - b^2)^(3/2)).
double RingAboveSphere(double R, double a, double b, double zp) {
  return ON_PI * zp * (b * b - a * a) - (2.0 * ON_PI / 3.0) * (std::pow(R * R - a * a, 1.5) - std::pow(R * R - b * b, 1.5));
}

}  // namespace

// Ring (inner radius 1, outer radius 2) raised on a sphere (R = 5, pole axis): base plane z = 4 inside the ball, depth 1.5 grows to
// z = 5.5 outside it. The centre island stays flush with the sphere: nothing is added inside the hole.
D8_EXTRA_TEST(EmbossRingOnSphereMatchesClosedForm) {
  const double R = 5, a = 1, b = 2;
  const Brep sphere = Brep::Sphere(P(0, 0, 0), R);
  try {
    const EmbossResult e = EmbossOnCurvedFace(sphere, CircleCurve(ON_3dPoint(0, 0, 4), ON_3dVector(0, 0, 1), b),
                                              {CircleCurve(ON_3dPoint(0, 0, 4), ON_3dVector(0, 0, 1), a)}, Vector3d(0, 0, -1), 1.5, EmbossMode::Emboss);
    const double expect = RingAboveSphere(R, a, b, 5.5);
    d8test::Check(e.brep.raw().IsValid() && e.brep.raw().IsSolid(), "ring emboss on sphere: valid closed solid");
    d8test::Check(Near(e.volume_change, expect, 0.01 * expect), ("ring emboss on sphere: added " + std::to_string(e.volume_change) + " vs closed form " + std::to_string(expect) + " (bound 1%)").c_str());
    d8test::Check(Inside(e.brep, P(1.5, 0, 5.2)) && !Inside(e.brep, P(0, 0, 5.2)) && !Inside(e.brep, P(2.5, 0, 5.2)) && Inside(e.brep, P(0, 0, 4.8)),
                  "ring emboss on sphere: probes in the ring wall, in the hole above the sphere, outside the ring, in the island below the sphere's surface");
    d8test::Check(!Inside(sphere, P(1.5, 0, 5.2)), "ring emboss on sphere: the ring probe was outside the bare sphere (control)");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("ring emboss on sphere was refused: ") + e.what()).c_str());
  }
}

// The same ring engraved: entry plane z = 5.5 outside the ball, depth 1.5, floor z = 4. The island stays untouched.
D8_EXTRA_TEST(DebossRingOnSphereMatchesClosedForm) {
  const double R = 5, a = 1, b = 2;
  const Brep sphere = Brep::Sphere(P(0, 0, 0), R);
  try {
    const EmbossResult e = EmbossOnCurvedFace(sphere, CircleCurve(ON_3dPoint(0, 0, 5.5), ON_3dVector(0, 0, 1), b),
                                              {CircleCurve(ON_3dPoint(0, 0, 5.5), ON_3dVector(0, 0, 1), a)}, Vector3d(0, 0, -1), 1.5, EmbossMode::Deboss);
    const double expect = -((2.0 * ON_PI / 3.0) * (std::pow(R * R - a * a, 1.5) - std::pow(R * R - b * b, 1.5)) - 4.0 * ON_PI * (b * b - a * a));
    d8test::Check(e.brep.raw().IsValid() && e.brep.raw().IsSolid(), "ring deboss on sphere: valid closed solid");
    d8test::Check(Near(e.volume_change, expect, 0.01 * std::fabs(expect)), ("ring deboss on sphere: change " + std::to_string(e.volume_change) + " vs closed form " + std::to_string(expect) + " (bound 1%)").c_str());
    d8test::Check(!Inside(e.brep, P(1.5, 0, 4.5)) && Inside(e.brep, P(0, 0, 4.5)) && Inside(e.brep, P(1.5, 0, 3.8)) && Inside(e.brep, P(2.5, 0, 4.0)),
                  "ring deboss on sphere: probes in the groove, on the island, under the floor, outside the ring");
    d8test::Check(Inside(sphere, P(1.5, 0, 4.5)), "ring deboss on sphere: the groove probe was inside the bare sphere (control)");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("ring deboss on sphere was refused: ") + e.what()).c_str());
  }
}

// Round ring (outer radius 1.2, inner radius 0.5, in the plane x = 2, centre (2, 0, 3)) raised on a cylinder wall (R = 3, axis z, 0..6):
// the base is inside the wall, depth 1.6 reaches x = 3.6 outside it. The column's height above the wall at lateral position y is
// 3.6 - sqrt(9 - y^2) and does not depend on z, so added = integral over y of 2 * (chord(y; 1.2) - chord(y; 0.5)) * (3.6 - sqrt(9 - y^2))
// with chord(y; r) = sqrt(r^2 - y^2) (midpoint rule, 200000 steps: independent of the kernel).
double RingOnCylinder(double r_out, double r_in, const std::function<double(double)>& height) {
  double acc = 0;
  const int n = 200000;
  for (int i = 0; i < n; ++i) {
    const double y = -r_out + (i + 0.5) * 2.0 * r_out / n;
    const double co = std::sqrt(std::max(0.0, r_out * r_out - y * y));
    const double ci = std::fabs(y) < r_in ? std::sqrt(r_in * r_in - y * y) : 0.0;
    acc += 2.0 * (co - ci) * height(y) * 2.0 * r_out / n;
  }
  return acc;
}

D8_EXTRA_TEST(EmbossRingOnCylinderWallMatchesQuadrature) {
  const double R = 3;
  const Brep cyl = Brep::Extrude(CircleCurve(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), R), Vector3d(0, 0, 6));
  const double expect = RingOnCylinder(1.2, 0.5, [&](double y) { return 3.6 - std::sqrt(R * R - y * y); });
  try {
    const EmbossResult e = EmbossOnCurvedFace(cyl, CircleCurve(ON_3dPoint(2, 0, 3), ON_3dVector(1, 0, 0), 1.2), {CircleCurve(ON_3dPoint(2, 0, 3), ON_3dVector(1, 0, 0), 0.5)},
                                              Vector3d(-1, 0, 0), 1.6, EmbossMode::Emboss);
    d8test::Check(e.brep.raw().IsValid() && e.brep.raw().IsSolid(), "ring emboss on cylinder wall: valid closed solid");
    d8test::Check(Near(e.volume_change, expect, 1e-2 * expect), ("ring emboss on cylinder wall: added " + std::to_string(e.volume_change) + " vs quadrature " + std::to_string(expect) + " (bound 1%)").c_str());
    // Composition: the ring is the 1.2 disc boss minus the 0.5 disc boss. The engine's polyline approximation of the wall/wall
    // intersection leaves each disc ~0.6% high (measured: the plain boss has the same error), so this ties the ring to the discs
    // far more tightly than the quadrature can: the hole must remove exactly the small disc's own added volume.
    {
      const auto disc = [&](double r) { return dino8::kernel::MakeBoss(cyl, CircleCurve(ON_3dPoint(2, 0, 3), ON_3dVector(1, 0, 0), r), Vector3d(1, 0, 0), 1.6).added_volume; };
      const double composed = disc(1.2) - disc(0.5);
      d8test::Check(Near(e.volume_change, composed, 1e-3 * composed), ("ring emboss on cylinder wall: added " + std::to_string(e.volume_change) + " vs disc(1.2) - disc(0.5) = " + std::to_string(composed) + " (bound 0.1%)").c_str());
    }
    d8test::Check(Inside(e.brep, P(3.3, 0.8, 3)) && !Inside(e.brep, P(3.3, 0, 3)) && !Inside(e.brep, P(3.3, 1.6, 3)) && Inside(e.brep, P(2.5, 0, 3)),
                  "ring emboss on cylinder wall: probes in the ring, in the hole above the wall, outside the ring, in the wall under the hole");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("ring emboss on cylinder wall was refused: ") + e.what()).c_str());
  }
}

// The same ring engraved: entry plane x = 3.6 outside the wall, depth 1.6, floor x = 2. The island inside the ring stays.
D8_EXTRA_TEST(DebossRingOnCylinderWallMatchesQuadrature) {
  const double R = 3;
  const Brep cyl = Brep::Extrude(CircleCurve(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), R), Vector3d(0, 0, 6));
  const double expect = -RingOnCylinder(1.2, 0.5, [&](double y) { return std::sqrt(R * R - y * y) - 2.0; });
  try {
    const EmbossResult e = EmbossOnCurvedFace(cyl, CircleCurve(ON_3dPoint(3.6, 0, 3), ON_3dVector(1, 0, 0), 1.2), {CircleCurve(ON_3dPoint(3.6, 0, 3), ON_3dVector(1, 0, 0), 0.5)},
                                              Vector3d(-1, 0, 0), 1.6, EmbossMode::Deboss);
    d8test::Check(e.brep.raw().IsValid() && e.brep.raw().IsSolid(), "ring deboss on cylinder wall: valid closed solid");
    d8test::Check(Near(e.volume_change, expect, 1e-2 * std::fabs(expect)), ("ring deboss on cylinder wall: change " + std::to_string(e.volume_change) + " vs quadrature " + std::to_string(expect) + " (bound 1%)").c_str());
    d8test::Check(!Inside(e.brep, P(2.5, 0.8, 3)) && Inside(e.brep, P(2.5, 0, 3)) && Inside(e.brep, P(2.5, 1.6, 3)) && Inside(e.brep, P(1.5, 0.8, 3)),
                  "ring deboss on cylinder wall: probes in the groove, on the island, outside the ring, under the floor");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("ring deboss on cylinder wall was refused: ") + e.what()).c_str());
  }
}

// A RECTANGULAR ring (planar-face tool with a window) is a measured limit: the sealed Difference of two planar prisms is a valid closed
// ring tool, but fusing it onto / cutting it from a curved host is refused by the engine ("3 or more fragment loops" on the sphere,
// an orientation failure on the cylinder wall). The contract this test pins is the honest one: either the EXACT volume, or a refusal
// that names the engine's cause - never a degraded body.
D8_EXTRA_TEST(EmbossRectangularRingOnCurvedHostIsExactOrRefused) {
  const Brep sphere = Brep::Sphere(P(0, 0, 0), 5.0);
  const auto rect = [](double z, double hx, double hy) {
    ON_Polyline pl;
    pl.Append(ON_3dPoint(-hx, -hy, z));
    pl.Append(ON_3dPoint(hx, -hy, z));
    pl.Append(ON_3dPoint(hx, hy, z));
    pl.Append(ON_3dPoint(-hx, hy, z));
    pl.Append(ON_3dPoint(-hx, -hy, z));
    ON_NurbsCurve nurbs;
    ON_PolylineCurve(pl).GetNurbForm(nurbs);
    NurbsCurve k;
    k.raw() = nurbs;
    return k;
  };
  try {
    const EmbossResult e = EmbossOnCurvedFace(sphere, rect(4, 1.5, 1.0), {rect(4, 0.5, 0.4)}, Vector3d(0, 0, -1), 1.5, EmbossMode::Emboss);
    // Exact answer by 2-D quadrature of (5.5 - sqrt(25 - x^2 - y^2)) over the frame.
    double expect = 0;
    const int n = 1500;
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        const double x = -1.5 + (i + 0.5) * 3.0 / n, y = -1.0 + (j + 0.5) * 2.0 / n;
        if (std::fabs(x) < 0.5 && std::fabs(y) < 0.4) continue;
        expect += (5.5 - std::sqrt(25.0 - x * x - y * y)) * (3.0 / n) * (2.0 / n);
      }
    }
    d8test::Check(e.brep.raw().IsValid() && e.brep.raw().IsSolid() && Near(e.volume_change, expect, 0.01 * expect),
                  ("rectangular ring on a sphere returned a body: added " + std::to_string(e.volume_change) + " vs quadrature " + std::to_string(expect)).c_str());
  } catch (const std::runtime_error& e) {
    d8test::Check(std::string(e.what()).find("was refused") != std::string::npos, (std::string("rectangular ring on a sphere: honest refusal (engine cause named): ") + std::string(e.what()).substr(0, 160)).c_str());
  }
}

// No holes forwards to MakeBoss / MakePocket with EmbossProfile's direction convention (INTO the material).
D8_EXTRA_TEST(EmbossOnCurvedFaceWithoutHolesIsBossAndPocket) {
  const double R = 5, b = 2;
  const Brep sphere = Brep::Sphere(P(0, 0, 0), R);
  try {
    const EmbossResult up = EmbossOnCurvedFace(sphere, CircleCurve(ON_3dPoint(0, 0, 4), ON_3dVector(0, 0, 1), b), {}, Vector3d(0, 0, -1), 1.5, EmbossMode::Emboss);
    const EmbossResult dn = EmbossOnCurvedFace(sphere, CircleCurve(ON_3dPoint(0, 0, 5.5), ON_3dVector(0, 0, 1), b), {}, Vector3d(0, 0, -1), 1.5, EmbossMode::Deboss);
    const double add = RingAboveSphere(R, 0, b, 5.5);
    const double rem = (2.0 * ON_PI / 3.0) * (R * R * R - std::pow(R * R - b * b, 1.5)) - 4.0 * ON_PI * b * b;
    d8test::Check(Near(up.volume_change, add, 0.01 * add), ("disc emboss: added " + std::to_string(up.volume_change) + " vs " + std::to_string(add)).c_str());
    d8test::Check(Near(dn.volume_change, -rem, 0.01 * rem), ("disc deboss: change " + std::to_string(dn.volume_change) + " vs " + std::to_string(-rem)).c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("disc emboss/deboss was refused: ") + e.what()).c_str());
  }
}

D8_EXTRA_TEST(EmbossOnCurvedFaceRefusesWithMeasuredReasons) {
  const Brep sphere = Brep::Sphere(P(0, 0, 0), 5.0);
  const NurbsCurve outer = CircleCurve(ON_3dPoint(0, 0, 4), ON_3dVector(0, 0, 1), 2.0);
  const NurbsCurve hole = CircleCurve(ON_3dPoint(0, 0, 4), ON_3dVector(0, 0, 1), 1.0);
  const Vector3d in(0, 0, -1);
  bool inv = false;
  d8test::Check(Throws([&] { EmbossOnCurvedFace(sphere, outer, {hole}, in, 0.0, EmbossMode::Emboss); }, "height", &inv) && inv, "zero depth is an invalid argument");
  d8test::Check(Throws([&] { EmbossOnCurvedFace(sphere, outer, {CircleCurve(ON_3dPoint(0, 0, 4.5), ON_3dVector(0, 0, 1), 1.0)}, in, 1.5, EmbossMode::Emboss); },
                       "not in the plane", &inv) && inv, "a hole off the outer profile's plane is refused");
  d8test::Check(Throws([&] { EmbossOnCurvedFace(sphere, outer, {CircleCurve(ON_3dPoint(1.9, 0, 4), ON_3dVector(0, 0, 1), 0.5)}, in, 1.5, EmbossMode::Emboss); },
                       "not inside the outer profile", &inv) && inv, "a hole sticking out of the outer profile is refused");
  d8test::Check(Throws([&] { EmbossOnCurvedFace(sphere, outer, {CircleCurve(ON_3dPoint(0.5, 0, 4), ON_3dVector(0, 0, 1), 0.4), CircleCurve(ON_3dPoint(0.9, 0, 4), ON_3dVector(0, 0, 1), 0.4)}, in, 1.5, EmbossMode::Emboss); },
                       "overlap", &inv) && inv, "overlapping holes are refused");
  // A base plane that is not buried: the footprint (radius 4.8 at z = 4) reaches outside the ball (|p| = 6.2 > 5).
  d8test::Check(Throws([&] { EmbossOnCurvedFace(sphere, CircleCurve(ON_3dPoint(0, 0, 4), ON_3dVector(0, 0, 1), 4.8), {hole}, in, 1.5, EmbossMode::Emboss); },
                       "not embedded", &inv) && !inv, "an emboss whose base reaches outside the solid is refused naming the base point");
  // A deboss whose entry plane is inside the material.
  d8test::Check(Throws([&] { EmbossOnCurvedFace(sphere, outer, {hole}, in, 1.0, EmbossMode::Deboss); }, "entry is not outside", &inv) && !inv,
                "a deboss whose entry plane is inside the solid is refused naming the entry point");
}
