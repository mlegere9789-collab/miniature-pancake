#include <cmath>
#include <cstdio>
#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface_intersect.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // SignedDistanceToBrep(), exercised standalone before trusting it in the
  // real suite (the full dino8_kernel_tests binary now runs to completion
  // but takes several minutes, so an isolated probe is faster to iterate on).

  const Brep sphere = Brep::Sphere(Point3d(0, 0, 0), 5);
  std::printf("sphere center: %.9f (expect -5)\n", SignedDistanceToBrep(Point3d(0, 0, 0), sphere.raw()));
  std::printf("sphere interior (0,0,3): %.9f (expect -2)\n", SignedDistanceToBrep(Point3d(0, 0, 3), sphere.raw()));
  std::printf("sphere exterior (8,0,0): %.9f (expect +3)\n", SignedDistanceToBrep(Point3d(8, 0, 0), sphere.raw()));
  std::printf("sphere on surface (5,0,0): %.9f (expect ~0)\n", SignedDistanceToBrep(Point3d(5, 0, 0), sphere.raw()));
  std::printf("sphere on surface (-5,0,0): %.9f (expect ~0)\n", SignedDistanceToBrep(Point3d(-5, 0, 0), sphere.raw()));
  std::printf("sphere on surface (0,5,0): %.9f (expect ~0)\n", SignedDistanceToBrep(Point3d(0, 5, 0), sphere.raw()));

  const Brep box = Brep::Box(0, 0, 0, 2, 2, 2);
  std::printf("box center (1,1,1): %.9f (expect -1)\n", SignedDistanceToBrep(Point3d(1, 1, 1), box.raw()));
  std::printf("box exterior (5,1,1): %.9f (expect +3)\n", SignedDistanceToBrep(Point3d(5, 1, 1), box.raw()));
  std::printf("box exterior corner (-1,-1,-1): %.9f (expect +sqrt(3)=%.9f)\n",
              SignedDistanceToBrep(Point3d(-1, -1, -1), box.raw()), std::sqrt(3.0));

  bool threw_empty = false;
  try {
    ON_Brep empty;
    SignedDistanceToBrep(Point3d(0, 0, 0), empty);
  } catch (const std::invalid_argument&) {
    threw_empty = true;
  }
  std::printf("empty brep throws invalid_argument: %d (expect 1)\n", threw_empty);

  ON::End();
  return 0;
}
