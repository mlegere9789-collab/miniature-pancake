// Edge-consistent polygon mesher for ON_Brep solids.
//
// Every brep edge is sampled once and those 3D samples are reused by both
// faces that meet at the edge, so seams, poles and cap boundaries share
// identical vertices. Each face is then triangulated in its (u, v) domain
// with a constrained Delaunay triangulation of the trim loops, refined by
// midpoint insertion, and welded into one mesh. That is what makes
// booleans, volume and STL export work on spheres, cylinders and revolved
// solids, not only on boxes.
#pragma once

#include <vector>

#include "dino8/kernel/mesh.h"

class ON_Brep;

namespace dino8::app {

struct BrepMeshOptions {
  double chord_tolerance = 0.01;  // model units
  int max_triangles_per_face = 12000;
  double min_edge_samples = 2;
};

// Meshes each face separately (for display: per-face normals / colours).
std::vector<kernel::Mesh> MeshBrepFaces(const ON_Brep& brep, const BrepMeshOptions& options);

// One welded mesh; closed and consistently oriented for a valid closed brep.
kernel::Mesh MeshBrepClosed(const ON_Brep& brep, const BrepMeshOptions& options);

}  // namespace dino8::app
