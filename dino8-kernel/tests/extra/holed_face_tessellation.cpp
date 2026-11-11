// Brep::Tessellate() of a face that carries a REAL inner loop (TrimmedPlanarFaceWelded with a hole), at many division counts.
//
// Oracle independent of the tessellator: a flat 10 x 10 face with the trim square (0.15..0.85)^2 has area 7 x 7 = 49 and the hole
// square (0.4..0.6)^2 removes 2 x 2 = 4, so the material is exactly 45 (the slit that joins the hole to the outline is a few
// 1e-7 wide, so the tessellated area is 45 to about 1e-5). The old whole-cell clipping of a holed face kept or dropped each grid
// cell as a whole, so its area depended on the division count; a keyhole polygon through the exact clip must not count cells
// twice at any division count.
#include <cmath>
#include <cstdio>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point2d;
using dino8::kernel::Point3d;

NurbsSurface Square10() {
  const std::vector<Point3d> grid = {Point3d(0, 0, 0), Point3d(0, 10, 0), Point3d(10, 0, 0), Point3d(10, 10, 0)};
  return NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
}

const std::vector<Point2d>& Trim() {
  static const std::vector<Point2d> t = {Point2d(0.15, 0.15), Point2d(0.85, 0.15), Point2d(0.85, 0.85), Point2d(0.15, 0.85)};
  return t;
}

}  // namespace

D8_EXTRA_TEST(HoledFaceTessellationAreaIsIndependentOfTheDivisionCount) {
  const std::vector<Point2d> hole = {Point2d(0.4, 0.4), Point2d(0.6, 0.4), Point2d(0.6, 0.6), Point2d(0.4, 0.6)};
  const Brep holed = Brep::TrimmedPlanarFaceWelded(Square10(), Trim(), {hole});
  for (const int n : {3, 4, 5, 8, 10, 16, 20}) {
    const auto meshes = holed.Tessellate(n, n);
    const double area = meshes.size() == 1 ? meshes.front().Area() : -1.0;
    char msg[160];
    std::snprintf(msg, sizeof msg, "holed face, Tessellate(%d, %d): area %.6f is 49 - 4 = 45 (to 1e-3)", n, n, area);
    d8test::Check(std::fabs(area - 45.0) < 1e-3, msg);
  }
  const auto adaptive = holed.TessellateAdaptive(0.05);
  d8test::Check(adaptive.size() == 1 && std::fabs(adaptive.front().Area() - 45.0) < 1e-3, "holed face, TessellateAdaptive: area 45");
  const auto rect = holed.Tessellate(6, 9);
  d8test::Check(rect.size() == 1 && std::fabs(rect.front().Area() - 45.0) < 1e-3, "holed face, Tessellate(6, 9) (u != v): area 45");
}

D8_EXTRA_TEST(HoledFaceTessellationTwoHolesAndAnOffCentreHole) {
  // two holes 1 x 1 and 2 x 1 physical units: material 49 - 1 - 2 = 46
  const std::vector<Point2d> h1 = {Point2d(0.25, 0.3), Point2d(0.35, 0.3), Point2d(0.35, 0.4), Point2d(0.25, 0.4)};
  const std::vector<Point2d> h2 = {Point2d(0.55, 0.6), Point2d(0.75, 0.6), Point2d(0.75, 0.7), Point2d(0.55, 0.7)};
  const Brep two = Brep::TrimmedPlanarFaceWelded(Square10(), Trim(), {h1, h2});
  for (const int n : {4, 7, 10, 16}) {
    const auto meshes = two.Tessellate(n, n);
    const double area = meshes.size() == 1 ? meshes.front().Area() : -1.0;
    char msg[160];
    std::snprintf(msg, sizeof msg, "two-hole face, Tessellate(%d, %d): area %.6f is 49 - 1 - 2 = 46 (to 1e-3)", n, n, area);
    d8test::Check(std::fabs(area - 46.0) < 1e-3, msg);
  }
}

D8_EXTRA_TEST(UnholedFaceTessellationIsUnchanged) {
  const Brep face = Brep::TrimmedPlanarFaceWelded(Square10(), Trim());
  for (const int n : {4, 10}) {
    const auto meshes = face.Tessellate(n, n);
    d8test::Check(meshes.size() == 1 && std::fabs(meshes.front().Area() - 49.0) < 1e-6, "un-holed face: area exactly 49");
  }
}
