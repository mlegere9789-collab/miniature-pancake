#include <cstdio>
#include <cmath>
#include "dino8/kernel/surface.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  cylinder.GetNurbForm(cylinder_surface);
  NurbsSurface wall;
  wall.raw() = cylinder_surface;

  const auto du = wall.Domain(0);
  const auto dv = wall.Domain(1);
  std::printf("u domain: [%f, %f]\n", du.min, du.max);
  std::printf("v domain: [%f, %f]\n", dv.min, dv.max);

  const double u0 = du.min + 0.25 * (du.max - du.min);
  const double u1 = du.min + 0.75 * (du.max - du.min);
  const double v0 = dv.min + 0.1 * (dv.max - dv.min);
  const double v1 = dv.min + 0.9 * (dv.max - dv.min);
  std::printf("trim: u=[%f,%f] v=[%f,%f]\n", u0, u1, v0, v1);

  const std::vector<Point2d> trim = {
      Point2d(u0, v0),
      Point2d(u1, v0),
      Point2d(u1, v1),
      Point2d(u0, v1),
  };

  try {
    const Mesh m = wall.TessellateGridNonUniformAdaptive(0.01, &trim, nullptr);
    std::printf("TessellateGridNonUniformAdaptive face count: %d\n", m.FaceCount());
  } catch (const std::exception& e) {
    std::printf("threw: %s\n", e.what());
  }

  try {
    const Mesh m2 = wall.TessellateGridClippedExactAdaptive(0.01, trim);
    std::printf("TessellateGridClippedExactAdaptive face count: %d\n", m2.FaceCount());
  } catch (const std::exception& e) {
    std::printf("exact clip threw: %s\n", e.what());
  }

  const auto divs_u = wall.SuggestedParameterValues(0, 0.01);
  std::printf("u breakpoints count: %zu\n", divs_u.size());
  for (double v : divs_u) std::printf("  u=%f\n", v);
  const auto divs_v = wall.SuggestedParameterValues(1, 0.01);
  std::printf("v breakpoints count: %zu\n", divs_v.size());
  for (double v : divs_v) std::printf("  v=%f\n", v);

  ON::End();
  return 0;
}
