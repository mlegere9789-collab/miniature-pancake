// Builds a small composite model - a "trophy" made of several
// primitives combined with real booleans - and exports it as .obj and
// .stl. This is deliberately not a unit test: every primitive/operation
// it uses is already covered by tests/test_basic.cpp with hand-derived
// exact numbers, but nothing there exercises them *together* in one
// pipeline (several different primitives, three chained boolean calls,
// both file formats). Running this end to end is the actual proof that
// the pieces compose - a bug in how, say, Cylinder()'s and Torus()'s
// outputs interact under BooleanCombine() wouldn't necessarily show up
// in either primitive's own isolated test.
#include <cstdio>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/mesh.h"

using dino8::kernel::BooleanCombine;
using dino8::kernel::BooleanOp;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::Vector3d;

int main() {
  const Vector3d up(0, 0, 1);

  // Base: a short, wide cylinder.
  const Mesh base = Mesh::Cylinder(Point3d(0, 0, 0), up, /*radius=*/3.0, /*height=*/1.0);

  // Stem: a tall, narrow cylinder rising from the base.
  const Mesh stem = Mesh::Cylinder(Point3d(0, 0, 1), up, /*radius=*/0.5, /*height=*/4.0);

  // Ring: a torus genuinely overlapping the stem partway up (major_radius
  // 0.5 puts the tube's center right on the stem's own radius, so the two
  // solids actually intersect rather than just sitting side by side).
  const Mesh ring = Mesh::Torus(Point3d(0, 0, 3), up, /*major_radius=*/0.5,
                                 /*minor_radius=*/0.3);

  // Cap: a cone on top of the stem.
  const Mesh cap = Mesh::Cone(Point3d(0, 0, 5), up, /*radius=*/1.5, /*height=*/2.0);

  Mesh model = BooleanCombine(base, stem, BooleanOp::Union);
  model = BooleanCombine(model, ring, BooleanOp::Union);
  model = BooleanCombine(model, cap, BooleanOp::Union);

  const auto bounds = model.GetBoundingBox();
  const auto centroid = model.GetCentroid();
  std::printf("Demo model: %d vertices, %d faces\n", model.VertexCount(), model.FaceCount());
  std::printf("Volume: %.6f\n", model.Volume());
  std::printf("Bounding box: (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)\n", bounds.min.x,
              bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z);
  std::printf("Centroid: (%.3f, %.3f, %.3f)\n", centroid.x, centroid.y, centroid.z);

  if (model.SaveObj("dino8_demo_trophy.obj") != Result::Ok) {
    std::fprintf(stderr, "Failed to write dino8_demo_trophy.obj\n");
    return 1;
  }
  if (model.SaveStl("dino8_demo_trophy.stl") != Result::Ok) {
    std::fprintf(stderr, "Failed to write dino8_demo_trophy.stl\n");
    return 1;
  }
  std::printf("Wrote dino8_demo_trophy.obj and dino8_demo_trophy.stl\n");
  return 0;
}
