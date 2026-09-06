// Volumetric remeshing tools shared by ShrinkWrap, QuadRemesh, ReduceMesh
// and Drape (cmd_remesh.cpp):
//
//   * BuildSdf: a signed-distance voxelisation of a set of sources
//     (triangle meshes, polylines, points). Distances come from a BVH
//     closest-primitive query limited to a narrow band; the sign of a closed
//     mesh (or of an open mesh whose holes are to be filled) is decided by
//     parity voting over six axis rays, so small holes and coincident faces
//     of several bodies do not break the field. Several sources are unioned
//     (min of the signed distances).
//   * MarchingCubes: the classic 256-case algorithm. The case table is
//     generated at first use by walking the crossing edges around each cube
//     face (face ambiguities resolved with a fixed rule shared by the two
//     cubes on either side), so the result is a closed, edge-manifold
//     triangle mesh with shared vertices.
//   * DualContouring: one vertex per cell placed by a quadric-error
//     minimisation of the Hermite data (crossing points + gradients), one
//     quad per crossing edge - an all-quad, feature-preserving mesh.
//   * Taubin smoothing, projection back onto the iso-surface or onto the
//     original primitives.
//   * Garland-Heckbert quadric edge-collapse decimation with a boundary
//     constraint and a deviation report.
#pragma once

#include <array>
#include <memory>
#include <vector>

#include "dino8/kernel/mesh.h"

namespace dino8::app::remesh {

using kernel::Point3d;
using kernel::Vector3d;

// A distance source: triangles, segments (a polyline's pieces) or points.
struct Primitive {
  Point3d p[3];
  int kind = 0;  // 0 triangle, 1 segment (p[0]-p[1]), 2 point (p[0])
  // Pseudonormals (Baerentzen & Aanaes) for signed-distance sign tests,
  // valid only for kind==0 triangles belonging to a closed Source: the
  // face normal, the three edge pseudonormals (edge_n[i] for the edge
  // p[i]-p[(i+1)%3], the averaged normal of its one or two incident
  // faces) and the three vertex pseudonormals (angle-weighted average of
  // every incident face normal).
  Vector3d face_n;
  Vector3d edge_n[3];
  Vector3d vert_n[3];
};

struct Source {
  std::vector<Primitive> prims;
  bool closed = false;     // triangle mesh without naked edges
  bool has_faces = false;  // any triangles at all (else curve/point cloud)
};

// Welds a mesh, triangulates it and reports whether it is closed.
Source SourceFromMesh(const kernel::Mesh& m, double weld_tol);
Source SourceFromPolyline(const std::vector<Point3d>& pts);
Source SourceFromPoint(Point3d p);
double SourceArea(const Source& s);
void SourceBounds(const std::vector<Source>& s, Point3d& lo, Point3d& hi);

struct SdfGrid {
  int nx = 0, ny = 0, nz = 0;
  Point3d origin;  // position of node (0,0,0)
  double h = 1;    // voxel edge length
  std::vector<float> d;
  size_t Index(int i, int j, int k) const { return (static_cast<size_t>(k) * ny + j) * nx + i; }
  float At(int i, int j, int k) const { return d[Index(i, j, k)]; }
  float& At(int i, int j, int k) { return d[Index(i, j, k)]; }
  Point3d Pos(int i, int j, int k) const { return Point3d(origin.x + i * h, origin.y + j * h, origin.z + k * h); }
  double Sample(Point3d p) const;     // trilinear
  Vector3d Gradient(Point3d p) const;  // central differences of Sample
};

struct SdfOptions {
  double voxel = 0;             // 0 = longest bbox side / 100
  int max_voxels = 2000000;     // grid budget; the voxel size grows to fit
  double offset = 0;            // inflate (positive) / deflate the result
  bool fill_holes = true;       // open meshes: fill by ray-parity voting when possible
  double shell = 0;             // thickness for open meshes/curves/points (0 = 1.5 voxels)
  bool symmetric_grid = true;   // centre the grid on the bounding box (SymmetryAxis)
};

struct SdfReport {
  int nx = 0, ny = 0, nz = 0;
  double voxel = 0, requested_voxel = 0;
  bool capped = false;
  int shells = 0;  // sources treated as thin shells (unsigned distance)
};

SdfGrid BuildSdf(const std::vector<Source>& sources, const SdfOptions& opt, SdfReport& report);
// d(x) = min(d(x), d(mirror x)) about the grid's mid-plane normal to `axis` (0..2).
void MirrorSdf(SdfGrid& g, int axis);

kernel::Mesh MarchingCubes(const SdfGrid& g, double iso = 0);
// `features` (optional) receives a per-vertex flag for vertices placed on a
// sharp feature by the QEF (kept fixed by later smoothing).
kernel::Mesh DualContouring(const SdfGrid& g, double iso, bool sharp, std::vector<char>* features = nullptr);

// Taubin lambda/mu smoothing (no shrinkage). `fixed` vertices are kept.
void SmoothTaubin(kernel::Mesh& m, int iterations, const std::vector<char>* fixed = nullptr);
void ProjectToIso(kernel::Mesh& m, const SdfGrid& g, double iso, const std::vector<char>* fixed = nullptr);
void ProjectToSources(kernel::Mesh& m, const std::vector<Source>& sources, const std::vector<char>* fixed = nullptr);

// Quadric error metric edge collapse to `target_faces` triangles.
kernel::Mesh Decimate(const kernel::Mesh& m, int target_faces, bool preserve_boundary);
// Max distance from the vertices of each mesh to the other mesh's surface.
double MaxDeviation(const kernel::Mesh& a, const kernel::Mesh& b);

// Closest-primitive queries (also used by Drape's ray casting).
class Bvh {
 public:
  explicit Bvh(std::vector<Primitive> prims);
  ~Bvh();
  Bvh(Bvh&&) noexcept;
  Bvh& operator=(Bvh&&) noexcept;
  Bvh(const Bvh&) = delete;
  Bvh& operator=(const Bvh&) = delete;
  // Nearest point within `max_dist`; returns false if none is closer.
  // `prim_index`, if given, receives the index into the vector passed to
  // the constructor (so the caller can look up per-primitive data such as
  // the pseudonormals above).
  bool Closest(Point3d p, double max_dist, Point3d& out, double& dist, size_t* prim_index = nullptr) const;
  // First hit of the ray p + t*dir (t >= 0), triangles only.
  bool Ray(Point3d p, Vector3d dir, double& t) const;
  size_t Size() const;
  const Primitive& PrimAt(size_t i) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace dino8::app::remesh
