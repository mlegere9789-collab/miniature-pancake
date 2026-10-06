#include <cstdio>
#include <cmath>
#include "dino8/kernel/surface.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // A cone: a genuine apex singularity (the whole row of control points
  // collapses to one point), unlike a cylinder (no singularity at all) or
  // a sphere (two isolated poles, but the row still has nonzero radius
  // infinitesimally close to each pole).
  const ON_Plane base_plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1));
  const ON_Cone cone(base_plane, /*height=*/5.0, /*radius=*/2.0);
  ON_NurbsSurface cone_surface;
  const int rc = cone.GetNurbForm(cone_surface);
  std::printf("GetNurbForm rc=%d\n", rc);
  NurbsSurface wall;
  wall.raw() = cone_surface;

  const auto du = wall.Domain(0);
  const auto dv = wall.Domain(1);
  std::printf("u domain: [%f, %f]\n", du.min, du.max);
  std::printf("v domain: [%f, %f]\n", dv.min, dv.max);

  try {
    const auto divs = wall.SuggestedDivisions(0.01);
    std::printf("SuggestedDivisions: u=%d v=%d\n", divs.u, divs.v);
  } catch (const std::exception& e) {
    std::printf("SuggestedDivisions threw: %s\n", e.what());
  }

  try {
    const auto values_u = wall.SuggestedParameterValues(0, 0.01);
    std::printf("SuggestedParameterValues(0) count=%zu\n", values_u.size());
  } catch (const std::exception& e) {
    std::printf("SuggestedParameterValues(0) threw: %s\n", e.what());
  }
  try {
    const auto values_v = wall.SuggestedParameterValues(1, 0.01);
    std::printf("SuggestedParameterValues(1) count=%zu\n", values_v.size());
    for (double v : values_v) std::printf("  v=%f\n", v);
  } catch (const std::exception& e) {
    std::printf("SuggestedParameterValues(1) threw: %s\n", e.what());
  }

  try {
    const Mesh m = wall.TessellateGridAdaptive(0.01);
    std::printf("TessellateGridAdaptive face count: %d\n", m.FaceCount());
  } catch (const std::exception& e) {
    std::printf("TessellateGridAdaptive threw: %s\n", e.what());
  }

  try {
    const Mesh m = wall.TessellateGridNonUniformAdaptive(0.01);
    std::printf("TessellateGridNonUniformAdaptive face count: %d\n", m.FaceCount());
  } catch (const std::exception& e) {
    std::printf("TessellateGridNonUniformAdaptive threw: %s\n", e.what());
  }

  // Which end is the apex (v=domain.min or v=domain.max)? Check radius of
  // the isocurve at each end via two sampled points' distance apart.
  {
    const Point3d p0 = wall.PointAt(du.min, dv.min);
    const Point3d p1 = wall.PointAt(du.max * 0.5 + du.min * 0.5, dv.min);
    std::printf("v=min isocurve spread: %f\n", (p0 - p1).Length());
    const Point3d q0 = wall.PointAt(du.min, dv.max);
    const Point3d q1 = wall.PointAt(du.max * 0.5 + du.min * 0.5, dv.max);
    std::printf("v=max isocurve spread: %f\n", (q0 - q1).Length());
  }

  ON::End();
  return 0;
}
