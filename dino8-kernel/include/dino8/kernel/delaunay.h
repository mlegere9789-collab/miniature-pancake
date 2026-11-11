#pragma once

#include <array>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// A general planar triangulator: the constrained Delaunay triangulation of a polygon with holes, optionally with interior
// (Steiner) points. Unlike the grid tessellators of NurbsSurface (a lattice of (u, v) values clipped to the trim loops), the
// triangles here follow the boundary vertices exactly and satisfy the Delaunay empty-circle condition everywhere a constraint
// edge does not separate the points, which is what keeps the triangles well shaped near a trim curve.
//
// Construction: the holes are bridged into the outer loop and the resulting weakly simple polygon is ear-clipped; the Steiner
// points are then inserted by splitting the triangle that contains them; finally Lawson edge flips (never across a polygon edge)
// run until every non-constraint edge is locally Delaunay. A triangulation in which every non-constraint edge is locally Delaunay
// IS the constrained Delaunay triangulation of its vertex set and constraint edges.
struct Triangulation2D {
  std::vector<Point2d> points;               // outer loop first, then every hole loop, then the accepted Steiner points
  std::vector<std::array<int, 3>> triangles;  // counter-clockwise, indices into `points`
  int outer_count = 0;                       // points[0, outer_count) are the outer loop
  std::vector<int> hole_sizes;               // the sizes of the hole loops that follow
  int steiner_accepted = 0;                  // Steiner points inserted (the rest were on an edge or on a vertex and are skipped)
  int flips = 0;                             // Lawson flips performed
};

// `outer` is a simple closed polygon (either orientation, the closing edge implied, no repeated first point); every hole is a
// simple polygon strictly inside it, disjoint from it and from every other hole; `steiner` are interior points (a point outside
// the domain, or within 1e-9 of the domain's extent of a vertex or an edge, is skipped and not counted in `steiner_accepted`).
// Throws std::invalid_argument for a loop with fewer than 3 points, a zero-area loop, a self-intersection, a loop crossing or
// touching another, or a hole not inside the outer loop; std::runtime_error if the triangulation cannot be completed.
Triangulation2D TriangulateConstrainedDelaunay(const std::vector<Point2d>& outer, const std::vector<std::vector<Point2d>>& holes = {},
                                               const std::vector<Point2d>& steiner = {});

// A trimmed region of a surface as a Delaunay mesh: `outer_uv` and `hole_uv` are the trim loops in the surface's (u, v) domain
// (as for NurbsSurface::TessellateGrid's trim_polygon / hole_polygons); the interior points are the lattice u = u0 + i du,
// v = v0 + j dv with u_divisions x v_divisions cells over the loops' own bounding box, skipping any lattice point closer than
// `0.3 * min(du, dv)` to a loop vertex or edge (a point that close would only make a sliver). Every mesh vertex is the surface
// evaluated at its (u, v), the boundary vertices are exactly the loop vertices, and the face winding follows the loops'
// counter-clockwise (u, v) order, i.e. the surface normal. Throws what TriangulateConstrainedDelaunay() throws, and
// std::invalid_argument for fewer than 1 division in either direction.
Mesh TessellateTrimmedDelaunay(const NurbsSurface& surface, const std::vector<Point2d>& outer_uv,
                               const std::vector<std::vector<Point2d>>& hole_uv, int u_divisions, int v_divisions);

}  // namespace dino8::kernel
