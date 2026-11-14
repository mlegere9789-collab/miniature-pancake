#pragma once

#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// What ComputeLscmUVs() measured about its own solve, for callers (and tests) that want to know it converged.
struct LscmReport {
  int vertex_count = 0;
  int triangle_count = 0;      // quads count as two triangles (split on the 0-2 diagonal)
  int boundary_edge_count = 0;
  int pinned_a = -1;           // the two boundary vertices held fixed (a at the UV origin, b on the +u axis)
  int pinned_b = -1;
  int iterations = 0;          // conjugate-gradient iterations used
  double residual = 0.0;       // final ||A^T r|| / ||A^T b|| of the normal equations (0 when the system is exactly consistent)
  bool converged = false;
};

// Least-squares conformal map (LSCM; Levy, Petitjean, Ray, Maillot, "Least Squares Conformal Maps for Automatic Texture Atlas
// Generation", SIGGRAPH 2002) of an OPEN, connected, consistently-oriented triangle/quad mesh to the plane: one (u, v) per
// vertex that minimises the total deviation from a conformal (angle-preserving) map. Where the mesh is developable (a cylinder
// strip, a cone sector, anything flat) the deviation is exactly zero and the UVs are the true unrolled layout up to a rigid
// motion and one global scale; where it is not (a sphere cap) the angle distortion is the smallest any map can have.
//
// This is the distortion-minimising unwrap that the box / planar / cylindrical / spherical projections of Mesh
// (ComputeBoxMappingUVs() and friends) are not: those are fixed projections that stretch wherever the surface turns away from
// the projection axis, and none of them can unroll a strip.
//
// Construction: every triangle is flattened isometrically into its own plane; the conformal condition on the per-triangle
// complex weights gives two real equations per triangle, solved in the least-squares sense for the 2 n unknowns with two
// boundary vertices pinned (the farthest-apart pair of boundary vertices: the first at the origin, the second at (d, 0) where d
// is their 3D distance, so the output is in model units and is deterministic - the same mesh always gives the same UVs). The
// solve is a Jacobi-preconditioned conjugate gradient on the normal equations (the matrix is never formed).
//
// Throws std::invalid_argument for a mesh LSCM is not defined on: no faces, a face index out of range, a vertex no face uses,
// a zero-area face, an edge used by three or more faces, two faces traversing an edge in the same direction (inconsistent
// orientation), more than one connected component, or no boundary edge (a closed mesh has to be cut open first - it has no
// conformal map to a plane). Throws std::runtime_error if the conjugate-gradient solve does not converge.
std::vector<Point2d> ComputeLscmUVs(const Mesh& mesh, LscmReport* report = nullptr);

}  // namespace dino8::kernel
