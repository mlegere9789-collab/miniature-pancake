// NurbsSurface::UnrollDevelopable on a GENERAL cylinder: an extrusion of a profile curve that is not a circle (here the parabola y = x^2 / 4).
//
// It used to be refused (only a plane, a circular cylinder and a cone were recognized), so UnrollSrf of an extruded spline fell back to the app's
// distance-preserving triangulation heuristic, which reports a nonzero distortion. Such a surface is developable with a closed-form unroll.
//
// Closed forms, not the construction's own numbers:
//   * arc length of y = a x^2 from 0 to x: L(x) = x sqrt(1 + 4 a^2 x^2) / 2 + asinh(2 a x) / (4 a). With a = 1/4 the whole profile x in [-2, 2] has
//     length 2 L(2) = 2 (sqrt(2) + asinh(1)) = 4.591165...
//   * a straight (perpendicular) extrusion by 3: the flat strip is 4.591165 wide and 3 high, area 13.7735;
//   * an OBLIQUE extrusion along d = (0, 1, 3): the surface area is the integral of |C' x d| = |(1.5 x, -3, 1)| = sqrt(2.25 x^2 + 10) over x in [-2, 2]
//     = 2 F(2), F(x) = x sqrt(2.25 x^2 + 10) / 2 + 10 asinh(1.5 x / sqrt(10)) / (2 * 1.5); an isometry preserves it, and the flat mesh tiles exactly.
// Also: the unroll is an isometry along the rulings (flat distance between two points of one ruling = their 3D distance), the transposed surface (rulings
// along u) unrolls the same, and a bilinear z = x y (ruled but not developable) and a cone-like ruled surface over the parabola are still refused.
#include <algorithm>
#include <cmath>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::DevelopableKind;
using dino8::kernel::Mesh;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

NurbsSurface ParabolaExtrusion(double dx, double dy, double dz) {
  // quadratic Bezier (-2,1), (0,-1), (2,1) is y = x^2 / 4; CV(i, j) = grid[i * 2 + j], j = 1 is the extruded copy.
  const std::vector<Point3d> g = {Point3d(-2, 1, 0), Point3d(-2 + dx, 1 + dy, dz), Point3d(0, -1, 0), Point3d(dx, -1 + dy, dz), Point3d(2, 1, 0), Point3d(2 + dx, 1 + dy, dz)};
  return NurbsSurface::FromControlGrid(g, 3, 2, 2, 1);
}

double ParabolaLength() { return 2.0 * (std::sqrt(2.0) + std::asinh(1.0)); }

double ObliqueArea() {
  auto F = [](double x) { return x * std::sqrt(2.25 * x * x + 10.0) / 2.0 + 10.0 * std::asinh(1.5 * x / std::sqrt(10.0)) / (2.0 * 1.5); };
  return 2.0 * F(2.0);
}

D8_EXTRA_TEST(UnrollGeneralCylinderPerpendicularExtrusion) {
  const NurbsSurface s = ParabolaExtrusion(0, 0, 3);
  Mesh flat;
  double area = 0;
  DevelopableKind kind = DevelopableKind::Plane;
  d8test::Check(s.UnrollDevelopable(16, 4, flat, &area, &kind) == Result::Ok, "a parabola extruded straight up unrolls (it used to be refused)");
  d8test::Check(kind == DevelopableKind::GeneralCylinder, "it is reported as a general cylinder");
  double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
  for (int i = 0; i < flat.raw().VertexCount(); ++i) {
    const ON_3fPoint v = flat.raw().m_V[i];
    minx = std::min<double>(minx, v.x);
    maxx = std::max<double>(maxx, v.x);
    miny = std::min<double>(miny, v.y);
    maxy = std::max<double>(maxy, v.y);
  }
  d8test::Check(std::abs((maxx - minx) - ParabolaLength()) < 1e-5, "the flat strip is exactly the parabola's closed-form arc length 4.591165 wide");
  d8test::Check(std::abs((maxy - miny) - 3.0) < 1e-5, "...and 3 high");
  d8test::Check(std::abs(area - ParabolaLength() * 3.0) < 1e-4, "the flat area is arc length * height = 13.7735");
  Mesh coarse;
  double coarse_area = 0;
  d8test::Check(s.UnrollDevelopable(2, 1, coarse, &coarse_area) == Result::Ok && std::abs(coarse_area - ParabolaLength() * 3.0) < 1e-4,
                "a deliberately coarse 2 x 1 mesh has the same exact area (rectangles tile exactly, not convergence)");
}

D8_EXTRA_TEST(UnrollGeneralCylinderObliqueExtrusion) {
  const NurbsSurface s = ParabolaExtrusion(0, 1, 3);
  Mesh flat;
  double area = 0;
  DevelopableKind kind = DevelopableKind::Plane;
  d8test::Check(s.UnrollDevelopable(24, 6, flat, &area, &kind) == Result::Ok && kind == DevelopableKind::GeneralCylinder, "an oblique extrusion unrolls as a general cylinder");
  d8test::Check(std::abs(area - ObliqueArea()) < 1e-4 * ObliqueArea(), "the flat area is the closed-form surface area, the integral of |C' x d|");
  // isometry along a ruling: vertices (i, 0) and (i, 6) are the two ends of one ruling, 3D distance |d| = sqrt(10)
  double worst = 0;
  for (int i = 0; i <= 24; ++i) {
    const ON_3fPoint a = flat.raw().m_V[i * 7], b = flat.raw().m_V[i * 7 + 6];
    worst = std::max(worst, std::abs(std::hypot(static_cast<double>(a.x - b.x), static_cast<double>(a.y - b.y)) - std::sqrt(10.0)));
  }
  d8test::Check(worst < 1e-5, "every ruling keeps its length |d| = sqrt(10) in the flat mesh");
}

D8_EXTRA_TEST(UnrollGeneralCylinderTransposedAndRefusals) {
  NurbsSurface t = ParabolaExtrusion(0, 0, 3);
  t.raw().Transpose();  // rulings now along u
  Mesh flat;
  double area = 0;
  DevelopableKind kind = DevelopableKind::Plane;
  d8test::Check(t.UnrollDevelopable(4, 16, flat, &area, &kind) == Result::Ok && kind == DevelopableKind::GeneralCylinder, "the transposed surface (rulings along u) unrolls");
  d8test::Check(std::abs(area - ParabolaLength() * 3.0) < 1e-4, "...to the same exact area");
  // z = x y: bilinear, ruled in both directions, NOT developable (Gaussian curvature < 0)
  const std::vector<Point3d> hp = {Point3d(0, 0, 0), Point3d(1, 0, 0), Point3d(0, 1, 0), Point3d(1, 1, 1)};
  Mesh f2;
  d8test::Check(NurbsSurface::FromControlGrid(hp, 2, 2, 1, 1).UnrollDevelopable(4, 4, f2) == Result::Failed, "a bilinear z = x y (ruled, not developable) is still refused");
  // a cone-like ruled surface over the parabola: the top copy is scaled by 0.5 instead of translated
  const std::vector<Point3d> cg = {Point3d(-2, 1, 0), Point3d(-1, 0.5, 3), Point3d(0, -1, 0), Point3d(0, -0.5, 3), Point3d(2, 1, 0), Point3d(1, 0.5, 3)};
  d8test::Check(NurbsSurface::FromControlGrid(cg, 3, 2, 2, 1).UnrollDevelopable(4, 4, f2) == Result::Failed, "a non-translation ruled surface over the parabola is refused");
}

}  // namespace
