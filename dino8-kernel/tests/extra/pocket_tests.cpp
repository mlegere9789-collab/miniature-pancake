// Pocket following a curved face (MakePocket). Expected removed volumes are independent closed forms / 2-D quadratures of "host
// material inside the swept column"; containment is probed with ContainsPointExact on points whose side is known from the construction.
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <string>
#include <vector>

#include "dino8/kernel/boss.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::MakePocket;
using dino8::kernel::NurbsCurve;
using dino8::kernel::PocketResult;
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

// Midpoint rule over the disc of radius r: sum of f(rho cos a, rho sin a) dA.
double OverDisc(double r, const std::function<double(double, double)>& f) {
  double acc = 0;
  const int nr = 600, nt = 600;
  for (int i = 0; i < nr; ++i) {
    const double rho = (i + 0.5) * r / nr;
    for (int j = 0; j < nt; ++j) {
      const double a = 2.0 * ON_PI * (j + 0.5) / nt;
      acc += f(rho * std::cos(a), rho * std::sin(a)) * rho * (r / nr) * (2.0 * ON_PI / nt);
    }
  }
  return acc;
}

}  // namespace

// Round pocket in a sphere (R = 5), axis down -z, entry plane z = 5.5 (clear of the ball), floor z = 4: the part of the ball inside the
// column is the cap above the floor, removed = integral over the disc of (sqrt(R^2 - rho^2) - 4) = (2 pi / 3)(R^3 - (R^2 - r^2)^(3/2)) - 4 pi r^2.
D8_EXTRA_TEST(PocketRoundInSphereMatchesClosedForm) {
  const double R = 5, r = 1.5;
  const Brep sphere = Brep::Sphere(P(0, 0, 0), R);
  const PocketResult p = MakePocket(sphere, CircleCurve(ON_3dPoint(0, 0, 5.5), ON_3dVector(0, 0, 1), r), Vector3d(0, 0, -1), 1.5);
  const double expect = (2.0 * ON_PI / 3.0) * (R * R * R - std::pow(R * R - r * r, 1.5)) - 4.0 * ON_PI * r * r;
  d8test::Check(p.brep.raw().IsValid() && p.brep.raw().IsSolid(), "round pocket in sphere: valid closed solid");
  d8test::Check(Near(p.removed_volume, expect, 0.01 * expect), ("round pocket in sphere: removed " + std::to_string(p.removed_volume) + " vs closed form " + std::to_string(expect) + " (bound 1%)").c_str());
  d8test::Check(!Inside(p.brep, P(0, 0, 4.5)) && Inside(p.brep, P(0, 0, 3.8)) && Inside(p.brep, P(1.8, 0, 4.4)) && !Inside(p.brep, P(0, 0, 5.2)),
                "round pocket in sphere: probes in the pocket, under its floor, beside it, above the ball");
  d8test::Check(Inside(sphere, P(0, 0, 4.5)), "round pocket in sphere: the pocket probe was inside the bare sphere (control)");
}

// Rectangular pocket in a cylinder wall (R = 3, axis z, 0..6): entry plane x = 3.5, floor x = 2, y in [-1, 1], z in [1, 5].
// removed = 4 * integral_{-1}^{1} (sqrt(9 - y^2) - 2) dy.
D8_EXTRA_TEST(PocketRectangleInCylinderWallMatchesQuadrature) {
  const double R = 3;
  const Brep cyl = Brep::Extrude(CircleCurve(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), R), Vector3d(0, 0, 6));
  const PocketResult p = MakePocket(cyl, std::vector<Point3d>{P(3.5, -1, 1), P(3.5, 1, 1), P(3.5, 1, 5), P(3.5, -1, 5)}, Vector3d(-1, 0, 0), 1.5);
  double expect = 0;
  const int n = 4000;
  for (int i = 0; i < n; ++i) {
    const double y = -1.0 + (i + 0.5) * 2.0 / n;
    expect += (std::sqrt(R * R - y * y) - 2.0) * 4.0 * 2.0 / n;
  }
  d8test::Check(p.brep.raw().IsValid() && p.brep.raw().IsSolid(), "rect pocket in cylinder wall: valid closed solid");
  d8test::Check(Near(p.removed_volume, expect, 2e-3 * expect), ("rect pocket in cylinder wall: removed " + std::to_string(p.removed_volume) + " vs quadrature " + std::to_string(expect)).c_str());
  d8test::Check(!Inside(p.brep, P(2.5, 0, 3)) && Inside(p.brep, P(1.5, 0, 3)) && Inside(p.brep, P(2.5, 1.5, 3)) && !Inside(p.brep, P(3.2, 0, 3)),
                "rect pocket in cylinder wall: probes in the pocket, under its floor, beside it, outside the wall");
}

// Round pocket in a torus (R = 4, r = 1.2) at the outer equator, azimuth 0: the footprint straddles the seam CORNER of the torus's (u, v)
// domain, the case the host re-seaming retry exists for. Entry x = 6, floor x = 4.6, radius 0.5: removed = integral over the disc of
// (sqrt((R + sqrt(r^2 - z^2))^2 - y^2) - 4.6).
D8_EXTRA_TEST(PocketRoundInTorusMatchesQuadrature) {
  const double R = 4, r = 1.2;
  const Brep torus = Brep::Torus(P(0, 0, 0), Vector3d(0, 0, 1), R, r);
  const double expect = OverDisc(0.5, [&](double y, double z) {
    const double s = std::sqrt(r * r - z * z);
    return std::sqrt((R + s) * (R + s) - y * y) - 4.6;
  });
  try {
    const PocketResult p = MakePocket(torus, CircleCurve(ON_3dPoint(6.0, 0, 0), ON_3dVector(1, 0, 0), 0.5), Vector3d(-1, 0, 0), 1.4);
    d8test::Check(p.brep.raw().IsValid() && p.brep.raw().IsSolid(), "round pocket in torus: valid closed solid");
    d8test::Check(Near(p.removed_volume, expect, 0.02 * expect), ("round pocket in torus: removed " + std::to_string(p.removed_volume) + " vs quadrature " + std::to_string(expect) + " (bound 2%)").c_str());
    d8test::Check(!Inside(p.brep, P(5.0, 0, 0)) && Inside(p.brep, P(4.3, 0, 0)) && Inside(p.brep, P(5.0, 0.8, 0)),
                  "round pocket in torus: probes in the pocket, under its floor, beside it");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("round pocket in torus was refused: ") + e.what()).c_str());
  }
}

// Rectangular pocket in the planar cap of a loose-face cylinder (no volume of its own): x in [-1, 1], y in [-0.5, 0.5], from the cap z = 6
// down to z = 5, entry plane z = 6.5, depth 1.5: removed = 2 x 1 x 1 = 2 exactly.
D8_EXTRA_TEST(PocketInPlanarCapOfLooseFaceHostIsExact) {
  Brep::CylindricalFace cf;
  cf.frame = ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1));
  cf.radius = 3.0;
  cf.angle = 2.0 * ON_PI;
  cf.length = 6.0;
  auto disk = [&](double z, bool flip) {
    Brep::PlanarFace pf;
    pf.plane = ON_Plane(ON_3dPoint(0, 0, z), flip ? ON_3dVector(0, 0, -1) : ON_3dVector(0, 0, 1));
    for (int i = 0; i < 128; ++i) {
      const double a = flip ? -2.0 * ON_PI * i / 128 : 2.0 * ON_PI * i / 128;
      pf.loop.push_back(P(3.0 * std::cos(a), 3.0 * std::sin(a), z));
    }
    pf.notch_begin = 0;
    pf.notch_count = 128;
    return pf;
  };
  const Brep host = Brep::FromMixedFaces({disk(0.0, true), disk(6.0, false)}, {cf});
  try {
    const PocketResult p = MakePocket(host, std::vector<Point3d>{P(-1, -0.5, 6.5), P(1, -0.5, 6.5), P(1, 0.5, 6.5), P(-1, 0.5, 6.5)}, Vector3d(0, 0, -1), 1.5);
    d8test::Check(p.brep.raw().IsValid() && p.brep.raw().IsSolid(), "pocket in loose-face host: valid closed solid");
    d8test::Check(Near(p.removed_volume, 2.0, 1e-6), ("pocket in loose-face host: removed " + std::to_string(p.removed_volume) + " vs closed form 2").c_str());
    d8test::Check(!Inside(p.brep, P(0, 0, 5.5)) && Inside(p.brep, P(0, 0, 4.5)) && Inside(p.brep, P(1.5, 0, 5.5)),
                  "pocket in loose-face host: probes in the pocket, under its floor, beside it");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("pocket in loose-face host was refused: ") + e.what()).c_str());
  }
}

D8_EXTRA_TEST(PocketRefusesWithMeasuredReasons) {
  const Brep sphere = Brep::Sphere(P(0, 0, 0), 5.0);
  const NurbsCurve ok = CircleCurve(ON_3dPoint(0, 0, 5.5), ON_3dVector(0, 0, 1), 1.5);
  const Vector3d down(0, 0, -1);
  bool inv = false;
  d8test::Check(Throws([&] { MakePocket(Brep(), ok, down, 1.5); }, "no faces", &inv) && inv, "empty solid is an invalid argument");
  d8test::Check(Throws([&] { MakePocket(sphere, ok, down, 0.0); }, "height", &inv) && inv, "zero depth is an invalid argument");
  d8test::Check(Throws([&] { MakePocket(sphere, ok, Vector3d(1, 0, 0), 1.5); }, "lies in the profile plane", &inv) && inv, "a direction in the profile plane is refused");
  // Entry inside the material (profile plane at z = 4.0: the whole footprint is inside the ball).
  d8test::Check(Throws([&] { MakePocket(sphere, CircleCurve(ON_3dPoint(0, 0, 4.0), ON_3dVector(0, 0, 1), 1.5), down, 1.0); }, "entry is not outside", &inv) && !inv,
                "an entry plane inside the solid is refused naming the entry point");
  // Floor outside the material: depth 0.6 from z = 5.5 ends at z = 4.9, where the radius-1.5 rim is at |p| = 5.12 > 5 (the bounding boxes
  // still overlap, so the refusal is the measured floor one; depth 0.1 would be refused earlier, the boxes being disjoint).
  d8test::Check(Throws([&] { MakePocket(sphere, ok, down, 0.6); }, "floor is not inside", &inv) && !inv, "a floor above the solid is refused naming the floor point");
  // Entirely clear of the solid.
  d8test::Check(Throws([&] { MakePocket(sphere, CircleCurve(ON_3dPoint(0, 0, 9.0), ON_3dVector(0, 0, 1), 1.0), down, 1.0); }, "bounding box", &inv) && !inv,
                "a pocket far from the solid is refused naming the disjoint bounding boxes");
}
