#include <cmath>
#include <cstdio>
#include <vector>
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // NurbsSurface::ChangeSeam, exercised standalone: the full
  // dino8_kernel_tests binary aborts earlier (in the pre-existing,
  // disclosed BooleanCombineGeneral "3+ fragment loops" limitation,
  // which several early TestBooleanCombineGeneral* fixtures happen to
  // trip) before it ever reaches TestSurfaceChangeSeam, so this checks
  // the same logic that test exercises, directly, in isolation.
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 2.0);
  const ON_Cylinder cylinder(circle, 5.0);
  ON_NurbsSurface cylinder_surface;
  std::printf("GetNurbForm: %d\n", cylinder.GetNurbForm(cylinder_surface) != 0);
  NurbsSurface wall;
  wall.raw() = cylinder_surface;
  std::printf("IsClosed(0)=%d IsPeriodic(0)=%d IsClosed(1)=%d\n", wall.IsClosed(0), wall.IsPeriodic(0),
              wall.IsClosed(1));

  // V (open) direction must refuse.
  const Result v_result = wall.ChangeSeam(1, wall.Domain(1).min);
  std::printf("ChangeSeam(1, Domain(1).min) == Failed: %d\n", v_result == Result::Failed);

  const Interval u_dom = wall.Domain(0);
  const Interval v_dom_before = wall.Domain(1);
  const double seam_u = u_dom.min + 0.3 * (u_dom.max - u_dom.min);
  const double v_mid = v_dom_before.min + 0.5 * (v_dom_before.max - v_dom_before.min);
  const Point3d expected_start = wall.PointAt(seam_u, v_mid);

  const Result r = wall.ChangeSeam(0, seam_u);
  std::printf("ChangeSeam(0, seam_u) == Ok: %d\n", r == Result::Ok);
  const Interval new_u_dom = wall.Domain(0);
  std::printf("new Domain(0).min == seam_u: %d (%.12f vs %.12f)\n", new_u_dom.min == seam_u, new_u_dom.min, seam_u);
  const Interval v_dom_after = wall.Domain(1);
  std::printf("Domain(1) untouched: %d\n", v_dom_after.min == v_dom_before.min && v_dom_after.max == v_dom_before.max);
  const double start_err = wall.PointAt(new_u_dom.min, v_mid).DistanceTo(expected_start);
  std::printf("new start point matches old PointAt(seam_u, v_mid): err=%.3e\n", start_err);

  double max_radial_err = 0.0;
  for (int i = 0; i <= 8; ++i) {
    const double v = wall.Domain(1).min + (i / 8.0) * (wall.Domain(1).max - wall.Domain(1).min);
    for (int j = 0; j <= 8; ++j) {
      const double u = new_u_dom.min + (j / 8.0) * (new_u_dom.max - new_u_dom.min);
      const Point3d p = wall.PointAt(u, v);
      const double radial = std::sqrt(p.x * p.x + p.y * p.y);
      max_radial_err = std::max(max_radial_err, std::fabs(radial - 2.0));
    }
  }
  std::printf("max |radial - 2.0| over 9x9 grid after ChangeSeam: %.3e\n", max_radial_err);

  const Result out_of_range = wall.ChangeSeam(0, new_u_dom.max + 1.0);
  std::printf("ChangeSeam(0, out-of-range) == Failed: %d\n", out_of_range == Result::Failed);

  bool threw = false;
  try {
    wall.ChangeSeam(2, 0.0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  std::printf("ChangeSeam(2, ...) threw invalid_argument: %d\n", threw);

  // Flat (open-in-both-directions) surface: always refused.
  std::vector<Point3d> grid;
  for (int u = 0; u < 2; ++u)
    for (int v = 0; v < 2; ++v) grid.emplace_back(u, v, 0);
  NurbsSurface flat = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  const Result flat_result = flat.ChangeSeam(0, flat.Domain(0).min);
  std::printf("flat.ChangeSeam(0, ...) == Failed: %d\n", flat_result == Result::Failed);

  ON::End();
  return 0;
}
