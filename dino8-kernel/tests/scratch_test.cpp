#include <cstdio>
#include <cmath>
#include <vector>
#include "dino8/kernel/surface.h"
#include "dino8/kernel/surface_intersect.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  IntersectOptions opt;
  opt.tolerance = 1e-6;
  opt.mesh_tolerance = 0.05;

  const Brep box_a = Brep::Box(0, 0, 0, 2, 2, 2);
  const auto hits = FireRayAtBrep(Point3d(1, 1, -5), Vector3d(0, 0, 1), 20.0, box_a.raw(), opt);
  std::printf("hits=%zu\n", hits.size());
  for (const auto& h : hits) {
    std::printf("  face=%d dist=%g point=(%g,%g,%g)\n", h.face_index, h.distance, h.point.x, h.point.y,
                h.point.z);
  }

  const auto short_hits = FireRayAtBrep(Point3d(1, 1, -5), Vector3d(0, 0, 1), 6.0, box_a.raw(), opt);
  std::printf("short_hits=%zu\n", short_hits.size());

  const auto miss_hits = FireRayAtBrep(Point3d(100, 100, -5), Vector3d(0, 0, 1), 20.0, box_a.raw(), opt);
  std::printf("miss_hits=%zu\n", miss_hits.size());

  const std::vector<Point3d> plane_grid = {Point3d(0, 0, 2), Point3d(0, 2, 2), Point3d(2, 0, 2),
                                            Point3d(2, 2, 2)};
  const NurbsSurface flat_plane = NurbsSurface::FromControlGrid(plane_grid, 2, 2, 1, 1);
  const auto dom_u = flat_plane.Domain(0);
  const auto dom_v = flat_plane.Domain(1);
  std::printf("flat_plane domain u=[%g,%g] v=[%g,%g]\n", dom_u.min, dom_u.max, dom_v.min, dom_v.max);
  std::printf("flat_plane PointAt(0.5,0.5) = (%g,%g,%g)\n", flat_plane.PointAt(0.5, 0.5).x,
              flat_plane.PointAt(0.5, 0.5).y, flat_plane.PointAt(0.5, 0.5).z);

  const std::vector<Point2d> small_trim = {Point2d(0.25, 0.25), Point2d(0.75, 0.25), Point2d(0.75, 0.75),
                                            Point2d(0.25, 0.75)};
  const Brep trimmed_top = Brep::TrimmedPlanarFace(flat_plane, small_trim, true);
  const auto inside_hits = FireRayAtBrep(Point3d(1.0, 1.0, 10), Vector3d(0, 0, -1), 20.0, trimmed_top.raw(), opt);
  const auto outside_hits = FireRayAtBrep(Point3d(0.2, 0.2, 10), Vector3d(0, 0, -1), 20.0, trimmed_top.raw(), opt);
  std::printf("inside_hits=%zu outside_hits=%zu\n", inside_hits.size(), outside_hits.size());

  try {
    FireRayAtBrep(Point3d(0, 0, 0), Vector3d(0, 0, 0), 20.0, box_a.raw(), opt);
    std::printf("ERROR: expected throw for zero direction\n");
  } catch (const std::invalid_argument& e) {
    std::printf("OK threw for zero direction: %s\n", e.what());
  }
  try {
    FireRayAtBrep(Point3d(0, 0, 0), Vector3d(0, 0, 1), 0.0, box_a.raw(), opt);
    std::printf("ERROR: expected throw for zero max_distance\n");
  } catch (const std::invalid_argument& e) {
    std::printf("OK threw for zero max_distance: %s\n", e.what());
  }

  ON::End();
  return 0;
}
