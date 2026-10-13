#include <cstdio>
#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface_intersect.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // BrepClosestPointIndex::FireRay(), cross-validated against
  // FireRayAtBrep()'s own brute-force answer, before trusting the real
  // registered test (TestBrepClosestPointIndexFireRay).

  IntersectOptions opt;
  opt.tolerance = 1e-6;
  opt.mesh_tolerance = 0.05;

  const Brep box = Brep::Box(0, 0, 0, 2, 2, 2);
  const BrepClosestPointIndex index(box.raw());

  auto compare = [&](const Point3d& origin, const Vector3d& dir, double max_distance, const char* label) {
    const std::vector<RayBrepHit> brute = FireRayAtBrep(origin, dir, max_distance, box.raw(), opt);
    const std::vector<RayBrepHit> accel = index.FireRay(origin, dir, max_distance, opt);
    std::printf("%s: brute=%zu accel=%zu\n", label, brute.size(), accel.size());
    if (brute.size() != accel.size()) {
      std::printf("  MISMATCH in hit count!\n");
      return;
    }
    for (size_t i = 0; i < brute.size(); ++i) {
      const double dd = std::fabs(brute[i].distance - accel[i].distance);
      const double pd = brute[i].point.DistanceTo(accel[i].point);
      const bool fi_match = brute[i].face_index == accel[i].face_index;
      std::printf("  [%zu] face_match=%d dist_diff=%.9f point_diff=%.9f\n", i, fi_match ? 1 : 0, dd, pd);
    }
  };

  compare(Point3d(1, 1, -5), Vector3d(0, 0, 1), 20.0, "through-shot (2 hits expected)");
  compare(Point3d(1, 1, -5), Vector3d(0, 0, 1), 6.0, "short max_distance (1 hit expected)");
  compare(Point3d(10, 10, 10), Vector3d(0, 0, 1), 5.0, "parallel-miss (0 hits expected)");
  compare(Point3d(1, 1, 1), Vector3d(1, 0.3, 0.2), 10.0, "starts-inside oblique");
  compare(Point3d(-10, 1, 1), Vector3d(1, 0, 0), 100.0, "far-away long ray");

  // Zero-vector direction throws.
  try {
    index.FireRay(Point3d(0, 0, 0), Vector3d(0, 0, 0), 10.0, opt);
    std::printf("ERROR: expected throw for zero direction\n");
  } catch (const std::invalid_argument&) {
    std::printf("OK: zero direction throws invalid_argument\n");
  }

  return 0;
}
