#include <cmath>
#include <cstdio>
#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // Probe for ShellConvexPlanar's own adjacent-openings generalization:
  // removing faces 1 (top, +z) and 2 (front, -y) of a unit-scaled box at
  // once, previously a hard refusal.
  const double s = 10.0, t = 1.0;
  const Brep cube = Brep::Box(0, 0, 0, s, s, s);
  try {
    const Brep shell = ShellConvexPlanar(cube, {1, 2}, t);
    std::printf("FaceCount=%d\n", shell.FaceCount());

    // Exact volume via the same tessellate-and-sum the existing tests
    // use (PlanarBrepVolumeExact isn't visible here, so just tessellate
    // and sum signed tetra volumes by hand via the mesh).
    const auto mesh = shell.TessellateToClosedMesh(1, 1);
    std::printf("IsClosedManifold=%d\n", mesh.IsClosedManifold() ? 1 : 0);
    std::printf("Volume=%.6f (expected 352.0)\n", mesh.Volume());
  } catch (const std::exception& e) {
    std::printf("THREW: %s\n", e.what());
  }

  return 0;
}
