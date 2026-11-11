// SplitBrepByProjectedCurve(): a solid split by an open curve projected along a direction.
//
// Every expected number below is a closed form of the fixture, independent of the code under test:
//  * Box [0,10]^3 (volume 1000, area 600) cut straight down z by the line y = 0.3 x + 2 (its chord crosses x = 0 at y = 2 and x = 10 at y = 5):
//    the y-smaller piece is a prism with trapezoid section area (2 + 5) / 2 * 10 = 35, volume 350, centroid (200/35, 65/35, 5); the other piece
//    650. The cut face is the rectangle (length 10 sqrt(1 + 0.09)) x 10; the two pieces' surfaces total the box's 600 plus twice that face.
//  * The same box cut by a circular arc of radius 6 about the z axis (from -10 to 100 degrees, so both ends lie outside the box): the piece
//    inside the circle is a quarter-disc prism, volume (pi 36 / 4) 10 = 90 pi; the cut face is the arc inside the box (6 pi / 2) times 10 = 30 pi.
//  * An OBLIQUE projection direction (1, 0, 1) of the line x = 3 (z = 0) sweeps the plane x - z = 3: the piece with x - z > 3 is a wedge of
//    section area int_0^7 (7 - z) dz = 24.5 and volume 245, the other 755.
//
// Tolerances are what the cut can honestly deliver, not 1e-9: SplitBySheet() trims the pieces by dense-polyline cut edges (default tolerance
// 0.001), so a planar cut is exact only to ~1e-8 relative and the circular cut (a polyline standing in for the arc) to a few 1e-6.
#include <cmath>
#include <stdexcept>

#include <opennurbs.h>

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

constexpr double kPi = 3.14159265358979323846;

NurbsCurve Line(Point3d a, Point3d b) { return NurbsCurve::FromControlPoints({a, b}, 1); }

NurbsCurve ArcAboutZ(double r, double a0, double a1, double z = 0.0) {
  ON_Arc arc(ON_Circle(ON_Plane(Point3d(0, 0, z), Vector3d(0, 0, 1)), r), ON_Interval(a0, a1));
  ON_NurbsCurve n;
  arc.GetNurbForm(n);
  NurbsCurve k;
  k.raw() = n;
  return k;
}

bool Sealed(const Brep& b) { return b.FaceCount() > 0 && b.raw().IsSolid() && b.raw().IsValid(); }

double Rel(double got, double want) { return std::abs(got - want) / std::abs(want); }

}  // namespace

D8_EXTRA_TEST(SplitProjectedCurveStraightLineBox) {
  const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
  const auto [pos, neg] = SplitBrepByProjectedCurve(box, Line(Point3d(-2, 1.4, -3), Point3d(12, 5.6, -3)), Vector3d(0, 0, 1));
  d8test::Check(Sealed(pos) && Sealed(neg), "line cut: both pieces are non-empty, valid, closed solids");
  // tangent (1, .3, 0) x direction (0, 0, 1) = (0.3, -1, 0): positive side is the y-smaller piece
  const double vp = pos.VolumeOfSealedSolid(), vn = neg.VolumeOfSealedSolid();
  d8test::Check(Rel(vp, 350.0) < 1e-6, "line cut: positive (y-smaller) piece volume is the trapezoid prism 350");
  d8test::Check(Rel(vn, 650.0) < 1e-6, "line cut: negative piece volume is 650");
  d8test::Check(std::abs(vp + vn - 1000.0) < 1e-5, "line cut: piece volumes sum to the box volume 1000");
  const double cut_area = 10.0 * std::sqrt(1.0 + 0.09) * 10.0;
  d8test::Check(Rel(pos.Area() + neg.Area(), 600.0 + 2.0 * cut_area) < 1e-6, "line cut: total area is the box's 600 plus the cut face twice");
  // centroid of the y < 0.3 x + 2 prism: x = (0.1 * 1000 + 100) / 35 = 200 / 35, y = (130 / 2) / 35 = 65 / 35, z = 5 (a side test that also
  // pins the geometry)
  const ON_3dPoint cp = pos.VolumeMassProperties().centroid;
  d8test::Check(std::abs(cp.x - 200.0 / 35.0) < 1e-5 && std::abs(cp.y - 65.0 / 35.0) < 1e-5 && std::abs(cp.z - 5.0) < 1e-5,
                "line cut: positive piece is the y-smaller prism (tangent x direction) with centroid (200/35, 65/35, 5)");
}

D8_EXTRA_TEST(SplitProjectedCurveArcCutsQuarterDisc) {
  const Brep box = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
  const double a0 = -10.0 * kPi / 180.0, a1 = 100.0 * kPi / 180.0;
  // the curve lies IN the box's bottom-face plane z = 0, which a naive sheet placement cannot cut (the engine refuses it)
  const auto [pos, neg] = SplitBrepByProjectedCurve(box, ArcAboutZ(6.0, a0, a1), Vector3d(0, 0, 1));
  d8test::Check(Sealed(pos) && Sealed(neg), "arc cut: both pieces are non-empty, valid, closed solids");
  // arc runs counterclockwise: tangent x (+z) points radially outward, so the outside of the circle is the positive side
  const double outside = pos.VolumeOfSealedSolid(), inside = neg.VolumeOfSealedSolid();
  d8test::Check(Rel(inside, 90.0 * kPi) < 2e-5, "arc cut: the piece inside the circle is the quarter-disc prism 90 pi");
  d8test::Check(std::abs(outside + inside - 1000.0) < 1e-2, "arc cut: piece volumes sum to the box volume 1000");
  d8test::Check(Rel(pos.Area() + neg.Area(), 600.0 + 2.0 * 30.0 * kPi) < 2e-4, "arc cut: total area is 600 plus the cylindrical cut face (30 pi) twice");
}

D8_EXTRA_TEST(SplitProjectedCurveObliqueDirection) {
  const Brep box = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
  const auto [pos, neg] = SplitBrepByProjectedCurve(box, Line(Point3d(3, -4, 0), Point3d(3, 14, 0)), Vector3d(1, 0, 1));
  d8test::Check(Sealed(pos) && Sealed(neg), "oblique cut: both pieces are non-empty, valid, closed solids");
  // tangent (0, 1, 0) x direction (1, 0, 1) = (1, 0, -1): positive side is x - z > 3
  const double vp = pos.VolumeOfSealedSolid(), vn = neg.VolumeOfSealedSolid();
  d8test::Check(Rel(vp, 245.0) < 1e-8, "oblique cut: the x - z > 3 wedge has volume 245");
  d8test::Check(std::abs(vp + vn - 1000.0) < 1e-6, "oblique cut: piece volumes sum to 1000");
}

D8_EXTRA_TEST(SplitProjectedCurveMissAndRefusals) {
  const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
  const auto [a, b] = SplitBrepByProjectedCurve(box, Line(Point3d(-2, 40, 0), Point3d(12, 40, 0)), Vector3d(0, 0, 1));
  d8test::Check((a.FaceCount() == 0) != (b.FaceCount() == 0), "a cut that misses the box leaves exactly one empty piece");
  const NurbsCurve ln = Line(Point3d(-2, 5, 0), Point3d(12, 5, 0));
  const auto throws = [&](const Brep& t, const NurbsCurve& c, Vector3d d, double tol) {
    try {
      SplitBrepByProjectedCurve(t, c, d, tol);
    } catch (const std::invalid_argument&) {
      return true;
    } catch (...) {
    }
    return false;
  };
  d8test::Check(throws(box, ln, Vector3d(0, 0, 0), 0.001), "zero direction throws std::invalid_argument");
  d8test::Check(throws(box, ln, Vector3d(1, 0, 0), 0.001), "direction parallel to a straight curve throws std::invalid_argument");
  d8test::Check(throws(box, ln, Vector3d(0, 0, 1), 0.0), "non-positive tolerance throws std::invalid_argument");
  d8test::Check(throws(Brep(), ln, Vector3d(0, 0, 1), 0.001), "faceless target throws std::invalid_argument");
  const NurbsCurve circle = ArcAboutZ(3.0, 0.0, 2.0 * kPi);
  d8test::Check(throws(box, circle, Vector3d(0, 0, 1), 0.001), "a closed curve throws std::invalid_argument");
}
