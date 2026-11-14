#pragma once

// Sculpt-mode data operators on a polygon mesh: brush falloff and the Draw brush dab, the sculpt mask (grow / shrink), Expand selection by
// topology / diagonal topology / sphere, and the pivot position (bpy.ops.sculpt.brush_stroke, mask_filter, expand, set_pivot_position).
//
// These work on a PolygonMesh plus per-vertex arrays (mask values in 0..1) and are independent of any viewport, tablet or stroke-spacing
// logic: a "dab" is one evaluation of the brush at one centre with one radius, which is what a stroke repeats along its path.

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <vector>

#include "dino8/kernel/polygon_mesh.h"

namespace dino8::kernel::sculpt {

// Brush falloff presets (the curve of Brush > Falloff). With t = 1 - d / r (t = 1 at the centre of the brush, 0 on its rim), the strength is
// Smooth 3 t^2 - 2 t^3, Sphere sqrt(2 t - t^2), Root sqrt(t), Sharp t^2, Linear t, Constant 1; it is 0 for d >= r.
enum class Falloff { Smooth, Sphere, Root, Sharp, Linear, Constant };

inline double FalloffStrength(Falloff f, double distance, double radius) {
  if (!(radius > 0.0)) throw std::invalid_argument("FalloffStrength: radius must be positive");
  if (!(distance < radius)) return 0.0;
  const double t = 1.0 - std::max(0.0, distance) / radius;
  switch (f) {
    case Falloff::Smooth: return 3.0 * t * t - 2.0 * t * t * t;
    case Falloff::Sphere: return std::sqrt(std::max(0.0, 2.0 * t - t * t));
    case Falloff::Root: return std::sqrt(t);
    case Falloff::Sharp: return t * t;
    case Falloff::Linear: return t;
    case Falloff::Constant: return 1.0;
  }
  return 0.0;
}

// One Draw-brush dab: every vertex within `radius` of `center` moves along `direction` by strength * FalloffStrength(distance), in model
// units. With a zero `direction` the area normal is used: the unit sum of the vertex normals of the vertices inside the brush. A vertex
// whose mask value is m moves by (1 - m) times that. Returns the number of vertices moved.
inline int DrawDab(PolygonMesh& mesh, const Point3d& center, double radius, double strength, Falloff falloff, Vector3d direction = Vector3d(0, 0, 0),
                   const std::vector<double>* mask = nullptr) {
  const std::vector<Vector3d> normals = mesh.VertexNormals();
  if (direction.Length() == 0.0) {
    Vector3d sum(0, 0, 0);
    for (int i = 0; i < mesh.VertexCount(); ++i)
      if (mesh.vertices[i].DistanceTo(center) < radius) sum += normals[i];
    sum.Unitize();
    direction = sum;
  } else {
    direction.Unitize();
  }
  int moved = 0;
  for (int i = 0; i < mesh.VertexCount(); ++i) {
    const double d = mesh.vertices[i].DistanceTo(center);
    const double f = FalloffStrength(falloff, d, radius);
    if (f <= 0.0) continue;
    const double keep = mask ? 1.0 - (*mask)[static_cast<size_t>(i)] : 1.0;
    mesh.vertices[i] += direction * (strength * f * keep);
    ++moved;
  }
  return moved;
}

enum class MaskFilter { Grow, Shrink };

// sculpt.mask_filter: Grow replaces every vertex' mask value by the largest value among the vertex itself and the vertices it shares an
// edge with; Shrink by the smallest. `iterations` repeats the step. A binary mask therefore grows (or shrinks) by one ring of edges per
// iteration.
inline std::vector<double> FilterMask(const PolygonMesh& mesh, const std::vector<double>& mask, MaskFilter type, int iterations = 1) {
  if (static_cast<int>(mask.size()) != mesh.VertexCount()) throw std::invalid_argument("FilterMask: mask size does not match the vertex count");
  const auto nb = mesh.VertexNeighbors();
  std::vector<double> cur = mask;
  for (int it = 0; it < iterations; ++it) {
    std::vector<double> next = cur;
    for (size_t v = 0; v < cur.size(); ++v)
      for (int n : nb[v]) next[v] = (type == MaskFilter::Grow) ? std::max(next[v], cur[static_cast<size_t>(n)]) : std::min(next[v], cur[static_cast<size_t>(n)]);
    cur = std::move(next);
  }
  return cur;
}

// sculpt.expand with topology falloff: the vertices whose number of edges to `start` is at most `steps`. With `diagonals` the step also
// crosses a face (any two vertices that share a face are one step apart: "Topology diagonals"). Returns the vertex indices in ascending order.
inline std::vector<int> ExpandTopology(const PolygonMesh& mesh, int start, int steps, bool diagonals = false) {
  if (start < 0 || start >= mesh.VertexCount()) throw std::out_of_range("ExpandTopology: start vertex out of range");
  std::vector<std::vector<int>> nb;
  if (!diagonals) {
    nb = mesh.VertexNeighbors();
  } else {
    nb.assign(static_cast<size_t>(mesh.VertexCount()), {});
    for (const auto& f : mesh.faces)
      for (int a : f)
        for (int b : f)
          if (a != b) nb[static_cast<size_t>(a)].push_back(b);
  }
  std::vector<int> dist(static_cast<size_t>(mesh.VertexCount()), std::numeric_limits<int>::max());
  std::deque<int> q;
  dist[static_cast<size_t>(start)] = 0;
  q.push_back(start);
  while (!q.empty()) {
    const int v = q.front();
    q.pop_front();
    if (dist[static_cast<size_t>(v)] >= steps) continue;
    for (int n : nb[static_cast<size_t>(v)])
      if (dist[static_cast<size_t>(n)] == std::numeric_limits<int>::max()) {
        dist[static_cast<size_t>(n)] = dist[static_cast<size_t>(v)] + 1;
        q.push_back(n);
      }
  }
  std::vector<int> r;
  for (int v = 0; v < mesh.VertexCount(); ++v)
    if (dist[static_cast<size_t>(v)] <= steps) r.push_back(v);
  return r;
}

// sculpt.expand with spherical falloff: the vertices within straight-line distance `radius` (inclusive, with `eps` slack) of `start`.
inline std::vector<int> ExpandSphere(const PolygonMesh& mesh, int start, double radius, double eps = 1e-9) {
  if (start < 0 || start >= mesh.VertexCount()) throw std::out_of_range("ExpandSphere: start vertex out of range");
  std::vector<int> r;
  for (int v = 0; v < mesh.VertexCount(); ++v)
    if (mesh.vertices[static_cast<size_t>(v)].DistanceTo(mesh.vertices[static_cast<size_t>(start)]) <= radius + eps) r.push_back(v);
  return r;
}

enum class PivotMode { Origin, Unmasked, Active };

// sculpt.set_pivot_position: Origin is the world origin; Unmasked is the average of the vertex positions weighted by (1 - mask), so
// fully masked vertices do not count; Active is the position of vertex `active`. Throws std::invalid_argument when every vertex is
// fully masked (Unmasked has no weight to average).
inline Point3d PivotPosition(const PolygonMesh& mesh, const std::vector<double>& mask, PivotMode mode, int active = -1) {
  switch (mode) {
    case PivotMode::Origin: return Point3d(0, 0, 0);
    case PivotMode::Active:
      if (active < 0 || active >= mesh.VertexCount()) throw std::out_of_range("PivotPosition: active vertex out of range");
      return mesh.vertices[static_cast<size_t>(active)];
    case PivotMode::Unmasked: {
      if (static_cast<int>(mask.size()) != mesh.VertexCount()) throw std::invalid_argument("PivotPosition: mask size does not match");
      double w = 0.0;
      Vector3d s(0, 0, 0);
      for (int v = 0; v < mesh.VertexCount(); ++v) {
        const double k = 1.0 - mask[static_cast<size_t>(v)];
        w += k;
        s += Vector3d(mesh.vertices[static_cast<size_t>(v)]) * k;
      }
      if (!(w > 0.0)) throw std::invalid_argument("PivotPosition: every vertex is masked");
      return Point3d(s.x / w, s.y / w, s.z / w);
    }
  }
  return Point3d(0, 0, 0);
}

}  // namespace dino8::kernel::sculpt
