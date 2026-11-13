// SplitBrepByProjectedCurve() on non-box targets (cylinder, L-shaped prism) and on a partial cut.
//
// Closed forms, independent of the code under test:
//  * Cylinder r = 5, h = 10 cut by the vertical plane through its axis: each half is pi 25 / 2 * 10 = 125 pi.
//  * L prism (outer 10 x 10, legs 4 wide, height 5, area 64, volume 320) cut by the vertical plane x = 2: the x > 2 part has area 64 - 20 = 44
//    (volume 220), the x < 2 part 20 (volume 100). Cut by the line y = (4x + 15)/7 (through (-2,1) and (12,9)): the part below the line has
//    area 260/7 (half-plane polygon clipping of the L, done by hand: 37.142857...), volume 1300/7 = 185.714285..., the rest 134.285714...
//  * Cylinder cut OFF the axis by y = 2: the segment area is r^2 acos(d/r) - d sqrt(r^2 - d^2) = 25 acos(0.4) - 2 sqrt(21) = 19.8168; the engine's
//    pieces for this cut do NOT seal (measured: 37 unpaired cut edges), so only what is true of them is pinned here.
//  * A curve whose projection ends INSIDE the solid does not sever it: SplitBySheet refuses with a typed BooleanOperationError, pinned here.
#include <cmath>
#include <stdexcept>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::SplitBrepByProjectedCurve;
using dino8::kernel::Vector3d;

NurbsCurve Line(Point3d a, Point3d b) { return NurbsCurve::FromControlPoints({a, b}, 1); }

bool Sealed(const Brep& b) { return b.FaceCount() > 0 && b.raw().IsSolid() && b.raw().IsValid(); }
double Rel(double got, double want) { return std::abs(got - want) / std::abs(want); }

// Prism of height h over a counter-clockwise polygon in z = 0, from explicit planar faces (real topology).
Brep Prism(const std::vector<Point3d>& p, double h) {
  std::vector<Brep::PlanarFace> fs;
  const int n = static_cast<int>(p.size());
  Brep::PlanarFace bot;
  bot.plane = ON_Plane(Point3d(0, 0, 0), Vector3d(0, 0, -1));
  for (int i = n - 1; i >= 0; --i) bot.loop.push_back(p[static_cast<size_t>(i)]);
  fs.push_back(bot);
  Brep::PlanarFace top;
  top.plane = ON_Plane(Point3d(0, 0, h), Vector3d(0, 0, 1));
  for (int i = 0; i < n; ++i) top.loop.push_back(p[static_cast<size_t>(i)] + Vector3d(0, 0, h));
  fs.push_back(top);
  for (int i = 0; i < n; ++i) {
    const Point3d a = p[static_cast<size_t>(i)], b = p[static_cast<size_t>((i + 1) % n)];
    const Vector3d e = b - a;
    Brep::PlanarFace w;
    w.plane = ON_Plane(a, Vector3d(e.y, -e.x, 0));
    w.loop = {a, b, b + Vector3d(0, 0, h), a + Vector3d(0, 0, h)};
    fs.push_back(w);
  }
  return Brep::FromPlanarFaces(fs);
}

Brep LPrism() {
  return Prism({Point3d(0, 0, 0), Point3d(10, 0, 0), Point3d(10, 4, 0), Point3d(4, 4, 0), Point3d(4, 10, 0), Point3d(0, 10, 0)}, 5.0);
}

}  // namespace

D8_EXTRA_TEST(SplitProjectedCurveCylinderThroughAxis) {
  const Brep cyl = Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 5.0, 10.0);
  const auto [pos, neg] = SplitBrepByProjectedCurve(cyl, Line(Point3d(-8, 0, -3), Point3d(8, 0, -3)), Vector3d(0, 0, 1));
  d8test::Check(Sealed(pos) && Sealed(neg), "cylinder axis cut: both halves are closed, valid solids");
  const double want = 125.0 * 3.14159265358979323846;
  d8test::Check(Rel(pos.VolumeOfSealedSolid(), want) < 1e-5 && Rel(neg.VolumeOfSealedSolid(), want) < 1e-5,
                "cylinder axis cut: each half has volume 125 pi");
}

D8_EXTRA_TEST(SplitProjectedCurveLPrism) {
  const Brep l = LPrism();
  {
    const auto [pos, neg] = SplitBrepByProjectedCurve(l, Line(Point3d(2, -3, -3), Point3d(2, 13, -3)), Vector3d(0, 0, 1));
    d8test::Check(Sealed(pos) && Sealed(neg), "L cut x = 2: both pieces are closed, valid solids");
    // tangent (0,1,0) x z = (1,0,0): positive is x > 2
    d8test::Check(Rel(pos.VolumeOfSealedSolid(), 220.0) < 1e-6 && Rel(neg.VolumeOfSealedSolid(), 100.0) < 1e-6,
                  "L cut x = 2: volumes are 220 (x > 2) and 100 (x < 2)");
  }
  {
    const auto [pos, neg] = SplitBrepByProjectedCurve(l, Line(Point3d(-2, 1, -3), Point3d(12, 9, -3)), Vector3d(0, 0, 1));
    d8test::Check(Sealed(pos) && Sealed(neg), "L slanted cut: both pieces are closed, valid solids");
    d8test::Check(Rel(pos.VolumeOfSealedSolid(), 1300.0 / 7.0) < 1e-6 && Rel(neg.VolumeOfSealedSolid(), 940.0 / 7.0) < 1e-6,
                  "L slanted cut: volumes are 1300/7 (below the line) and 940/7");
  }
}

D8_EXTRA_TEST(SplitProjectedCurveCylinderOffAxisPinned) {
  const Brep cyl = Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 5.0, 10.0);
  const auto [pos, neg] = SplitBrepByProjectedCurve(cyl, Line(Point3d(-8, 2, -3), Point3d(8, 2, -3)), Vector3d(0, 0, 1));
  d8test::Check(pos.FaceCount() > 0 && neg.FaceCount() > 0 && pos.raw().IsValid() && neg.raw().IsValid(),
                "cylinder off-axis cut: both pieces are non-empty and valid (measured: they do not seal, so no exact volume is claimed)");
}

D8_EXTRA_TEST(SplitProjectedCurvePartialCutRefusedTyped) {
  const Brep box = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
  bool typed_both = false, typed_one = false;
  try {
    SplitBrepByProjectedCurve(box, Line(Point3d(3, 5, -3), Point3d(7, 5, -3)), Vector3d(0, 0, 1));
  } catch (const dino8::kernel::BooleanOperationError&) {
    typed_both = true;
  } catch (...) {
  }
  try {
    SplitBrepByProjectedCurve(box, Line(Point3d(-3, 5, -3), Point3d(5, 5, -3)), Vector3d(0, 0, 1));
  } catch (const dino8::kernel::BooleanOperationError&) {
    typed_one = true;
  } catch (...) {
  }
  d8test::Check(typed_both, "partial cut, both ends inside the solid: refused with a typed BooleanOperationError");
  d8test::Check(typed_one, "partial cut, one end inside the solid: refused with a typed BooleanOperationError");
}
