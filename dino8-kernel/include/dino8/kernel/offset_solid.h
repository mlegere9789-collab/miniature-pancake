// Exact mitre offset of a CLOSED SOLID BOUNDED BY PLANAR FACES, convex or not - the multi-face case OffsetSolidConvexPlanar()
// (clip-against-every-plane, convex only) cannot do: an L, a stepped or notched block, any simple-polygon-faced polyhedron.
// Implemented in src/offset_solid.cpp.
//
// Construction: every face plane moves by `distance` along its outward normal; every vertex moves to where the offset planes of
// the faces meeting at it intersect (corner reconstruction). With exactly three independent planes that point is unique; with
// more (a valence-4+ vertex, collinear extra vertices) the offset planes must still be concurrent - this is MEASURED (worst
// |n_i . delta - distance| over every vertex and face) and the call refuses, with the number, when they are not. Edges stay
// straight and faces stay planar by construction; the corners are sharp (mitred, extend-and-intersect), never rounded.
//
// Honest scope (each refusal is a measured reason in `refusal_reason`, the output is untouched):
//   * every face must be planar and a simple polygon without holes (Brep::PlanarFaces()); a curved face is refused with
//     Brep::PlanarFaces()'s own message, a face with an inner loop is not representable;
//   * the faces must close up: every directed edge needs exactly one reverse partner (reported otherwise: unpaired count);
//   * the solid must be outward-oriented (positive volume);
//   * valence-4+ vertices whose offset planes are not concurrent are refused with the residual;
//   * topology never changes: an edge that shrinks to nothing or reverses (a wall or a gap closing), or a face
//     whose boundary crosses itself (a dovetail mouth closing), is refused with the measured edge length ratio / the crossing edges;
//   * the result is meshed and checked with Mesh::FindSelfIntersections(): any crossing of non-adjacent faces (two arms of a
//     C closing on each other, a slot narrower than twice the distance) is refused with the pair count;
//   * curved faces (cylinders, cones, spheres, NURBS) are NOT handled here.
#pragma once

#include <string>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

struct PlanarSolidOffsetReport {
  int faces = 0;
  int vertices = 0;
  int edges = 0;
  // Vertices where more than three faces meet (their offset planes were checked for concurrency) and vertices with fewer than
  // three independent plane directions (the minimum-norm displacement was used).
  int vertices_over_three_faces = 0;
  int rank_deficient_vertices = 0;
  // Worst |n_i . delta - distance| over every vertex and every face meeting there (0 = every offset plane passes through its
  // vertices' new positions exactly).
  double max_plane_residual = 0.0;
  // Smallest (new edge length) / (old edge length) over all edges; > 0 on success.
  double min_edge_length_ratio = 0.0;
  // Exact signed volume of the input and of the result (divergence theorem over the polygons, independent of any mesh).
  double volume_before = 0.0;
  double volume_after = 0.0;
  // Mesh::FindSelfIntersections() pairs on the result's tessellation (0 on success; -1 = not reached).
  int self_intersections = -1;
  std::string refusal_reason;
};

// Result::Ok with `out` set, or Result::Failed with `report->refusal_reason` (never throws for a geometric refusal).
// Positive `distance` grows the solid, negative shrinks it; 0 is returned unchanged.
Result OffsetSolidPlanar(const Brep& solid, double distance, Brep& out, PlanarSolidOffsetReport* report = nullptr);

}  // namespace dino8::kernel
