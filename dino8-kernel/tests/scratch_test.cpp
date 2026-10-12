#include <cstdio>
#include <cmath>
#include <vector>
#include "dino8/kernel/surface_intersect.h"
#include "dino8/kernel/brep.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // 1) Box: a grid of points, some inside, some outside - cross-check
  // against the hand-known geometry (box occupies [0,2]^3).
  const Brep box = Brep::Box(0, 0, 0, 2, 2, 2);
  const std::vector<Point3d> probes = {
      Point3d(1, 1, 1),    // center, inside
      Point3d(0.01, 0.01, 0.01),  // just inside a corner
      Point3d(1.99, 1, 1), // just inside a face
      Point3d(2.01, 1, 1), // just outside a face
      Point3d(3, 3, 3),    // well outside, diagonal
      Point3d(1, 1, -5),   // well outside, below
      Point3d(1, 1, 5),    // well outside, above
  };
  for (const Point3d& p : probes) {
    const bool inside = ContainsPointExact(p, box.raw());
    std::printf("box query(%g,%g,%g): inside=%d\n", p.x, p.y, p.z, (int)inside);
  }

  // 2) Sphere, radius 5 centered at origin.
  const Brep sphere = Brep::Sphere(Point3d(0, 0, 0), 5);
  const std::vector<Point3d> sphere_probes = {
      Point3d(0, 0, 0),
      Point3d(4.99, 0, 0),
      Point3d(5.01, 0, 0),
      Point3d(3, 3, 3),  // distance sqrt(27)=5.196, just outside
      Point3d(2, 2, 2),  // distance sqrt(12)=3.46, inside
      Point3d(0, 0, 100),
  };
  for (const Point3d& p : sphere_probes) {
    const bool inside = ContainsPointExact(p, sphere.raw());
    const double dist_from_center = p.DistanceTo(Point3d(0, 0, 0));
    std::printf("sphere query(%g,%g,%g) dist=%g: inside=%d\n", p.x, p.y, p.z, dist_from_center, (int)inside);
  }

  // 3) The disclosed ON_Brep::BoundingBox() degenerate-y/z bug fixture,
  // straight from IntersectBrepByPlane()'s own doc comment.
  const Brep box3 = Brep::Box(0, 0, 0, 3, 3, 3);
  {
    ON_BoundingBox buggy = box3.raw().BoundingBox();
    std::printf("box3 buggy ON_Brep::BoundingBox(): min=(%g,%g,%g) max=(%g,%g,%g)\n", buggy.m_min.x, buggy.m_min.y,
                buggy.m_min.z, buggy.m_max.x, buggy.m_max.y, buggy.m_max.z);
  }
  const std::vector<Point3d> box3_probes = {
      Point3d(1.5, 1.5, 1.5),
      Point3d(1.5, 1.5, 2.99),
      Point3d(1.5, 1.5, 3.01),
      Point3d(1.5, 1.5, 50),
      Point3d(1.5, 50, 1.5),
  };
  for (const Point3d& p : box3_probes) {
    const bool inside = ContainsPointExact(p, box3.raw());
    std::printf("box3 query(%g,%g,%g): inside=%d\n", p.x, p.y, p.z, (int)inside);
  }

  try {
    ON_Brep empty;
    ContainsPointExact(Point3d(0, 0, 0), empty);
    std::printf("ERROR: expected throw for empty brep\n");
  } catch (const std::invalid_argument& e) {
    std::printf("OK threw for empty brep: %s\n", e.what());
  }

  ON::End();
  return 0;
}
