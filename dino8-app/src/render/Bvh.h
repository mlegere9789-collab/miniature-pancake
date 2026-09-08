// A small binary BVH over triangles for the CPU path tracer
// (src/render/PathTracer.h). Built once per render with a binned
// surface-area-heuristic (SAH) split search (12 bins per axis), then
// traversed with a small explicit stack (no recursion) so shadow rays and
// closest-hit rays stay cheap under many worker threads.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "dino8/kernel/types.h"

namespace dino8::render {

// One shaded triangle: world-space positions/normals/uvs (already
// interpolated per vertex) plus the material/object it came from. `object`
// is the index into PathTracer's per-object list (ground-plane exclusion,
// self-shadow bias); `material` indexes PathTracer's material list.
struct BvhTriangle {
  kernel::Point3d v0, v1, v2;
  kernel::Vector3d n0, n1, n2;
  float uv0[2] = {0, 0}, uv1[2] = {0, 0}, uv2[2] = {0, 0};
  int material = -1;
  int object = -1;
};

struct BvhHit {
  float t = 0;
  float u = 0, v = 0;  // barycentric (v0 weight = 1-u-v)
  int tri = -1;
};

class Bvh {
 public:
  void Build(std::vector<BvhTriangle> triangles);
  bool Empty() const { return tris_.empty(); }
  const std::vector<BvhTriangle>& Triangles() const { return tris_; }

  // Closest hit within (tmin, tmax). Returns false when nothing is hit.
  bool Intersect(const kernel::Point3d& origin, const kernel::Vector3d& dir, float tmin, float tmax,
                 BvhHit& hit) const;
  // Any hit within (tmin, tmax) - for shadow rays. `ignore_object` skips
  // triangles belonging to that object (self-shadowing bias uses an offset
  // origin instead, so this is normally -1).
  bool IntersectAny(const kernel::Point3d& origin, const kernel::Vector3d& dir, float tmin, float tmax) const;

  // GPU export for GpuRaytracer (src/render/GpuRaytracer.h): flattens the
  // already-built tree into float arrays ready for glTexBuffer upload, one
  // vec4-triple per node / vec4-sextuple per triangle. Node count and
  // triangle count are `out.size()/12` and `out.size()/24` respectively.
  // Node layout (3 vec4 = 12 floats): (bmin.xyz, left) (bmax.xyz, count)
  // (start, 0, 0, 0) - mirrors `Node` exactly so the GLSL traversal matches
  // Intersect()/IntersectAny() above.
  void ExportGpuNodes(std::vector<float>& out) const;
  // Triangle layout (6 vec4 = 24 floats): (v0.xyz, material) (v1.xyz, 0)
  // (v2.xyz, 0) (n0.xyz, 0) (n1.xyz, 0) (n2.xyz, 0). UVs are not exported -
  // the GPU raytracer does not sample textures (see gpu_render_notes.md).
  void ExportGpuTriangles(std::vector<float>& out) const;

 private:
  struct Node {
    kernel::Point3d bmin{1e30, 1e30, 1e30}, bmax{-1e30, -1e30, -1e30};
    int left = -1;    // internal: index of the left child (right = left+1); leaf: -1
    int start = 0;    // leaf: first triangle index in `tris_` (after Build reorders them)
    int count = 0;    // leaf: triangle count; 0 for internal nodes
  };
  std::vector<BvhTriangle> tris_;
  std::vector<Node> nodes_;
  int BuildRange(int start, int count, int depth);
};

}  // namespace dino8::render
