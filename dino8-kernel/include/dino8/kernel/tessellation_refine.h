// Curvature-aware per-region tessellation and triangle-mesh quality metrics.
//
// The grid tessellators on NurbsSurface (TessellateGridAdaptive,
// TessellateGridNonUniformAdaptive, ...) refine along whole isocurves: one
// small tight-curvature feature forces a fine row/column of cells across the
// entire face. TessellateSurfaceRegionAdaptive() refines per REGION instead:
// it starts from a coarse, crease-aware, aspect-balanced lattice and bisects
// (newest-vertex / longest-edge style, conforming, no T-junctions) only the
// triangles whose measured facet deviation, facet-normal turn or edge length
// exceeds the requested control, worst region first, until every triangle
// meets every control or a depth / triangle budget is reached.
//
// MeasureMeshQuality() is the matching metric: triangle angles, Robinson
// aspect ratio, areas, edge lengths and the largest normal turn across a
// shared edge, for any Mesh.
#pragma once

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// Quality statistics of a triangle mesh (a quad face counts as the two
// triangles (0,1,2) and (0,2,3)). Zero-area triangles are counted in
// `degenerate_count` and excluded from every other statistic.
struct MeshQuality {
  int triangle_count = 0;
  int degenerate_count = 0;
  double min_angle = 0.0;  // radians, smallest interior angle of any triangle
  double max_angle = 0.0;  // radians, largest interior angle of any triangle
  // Robinson aspect ratio R / (2 r) = a*b*c*s / (8 * A^2) (R circumradius, r
  // inradius, s semi-perimeter, A area): exactly 1 for an equilateral
  // triangle, grows without bound for slivers.
  double max_aspect_ratio = 0.0;
  double mean_aspect_ratio = 0.0;
  double min_area = 0.0;
  double max_area = 0.0;
  double total_area = 0.0;
  double min_edge_length = 0.0;
  double max_edge_length = 0.0;
  // Largest angle (radians) between the unit normals of two triangles that
  // share an edge (by vertex index); 0 when no edge has two triangles.
  double max_adjacent_normal_angle = 0.0;
};

MeshQuality MeasureMeshQuality(const Mesh& mesh);

struct RegionRefineOptions {
  // Largest allowed distance from the true surface to a triangle's plane, at
  // sampled interior and edge points (must be > 0).
  double chord_tolerance = 0.01;
  // If > 0 (radians, <= pi): every triangle's normal must be within
  // angle_tolerance / 2 of the surface normal at each of its corners, so the
  // normals of any two triangles sharing an edge differ by at most
  // angle_tolerance. 0 disables the control.
  double angle_tolerance = 0.0;
  // If > 0: no triangle edge longer than this (model units).
  double max_edge_length = 0.0;
  // The starting lattice is subdivided until its cells' 3D aspect ratio
  // (longer side / shorter side) is at most this (>= 1).
  double max_aspect_ratio = 3.0;
  // Starting lattice divisions per smooth span, per direction (>= 1; a closed
  // direction uses at least 3 in total).
  int min_divisions = 4;
  // Most bisection levels any triangle may be refined to; each level halves a
  // triangle's parameter-space area (>= 0).
  int max_levels = 24;
  // If > 0: refinement stops once this many triangles exist (the worst
  // regions have been refined first). The result can exceed it by the few
  // triangles a final conforming split adds.
  int max_triangles = 0;
  // Interior/edge sample density: each triangle is tested at the lattice
  // points i/k, j/k of its barycentric coordinates (k >= 2).
  int samples = 4;
};

struct RegionRefineReport {
  int base_triangle_count = 0;
  int triangle_count = 0;
  int max_level_reached = 0;
  double achieved_deviation = 0.0;     // worst sampled plane distance in the result
  double achieved_normal_angle = 0.0;  // worst facet-normal vs corner surface-normal angle
  double longest_edge = 0.0;
  bool controls_met = false;       // every triangle satisfies every enabled control
  bool budget_exhausted = false;   // stopped by max_triangles with a control unmet
  bool depth_limited = false;      // some triangle needed more than max_levels
};

// Tessellates the whole (untrimmed) parameter domain of `surface` into a
// welded triangle mesh: seams of a closed direction and degenerate (pole /
// apex) rows are merged and zero-area triangles dropped, so a closed surface
// (sphere, torus) returns a closed manifold. Winding matches TessellateGrid().
// Throws std::invalid_argument for out-of-range options.
Mesh TessellateSurfaceRegionAdaptive(const NurbsSurface& surface, const RegionRefineOptions& options = {},
                                     RegionRefineReport* report = nullptr);

}  // namespace dino8::kernel
