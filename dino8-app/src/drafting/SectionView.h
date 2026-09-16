// Live hatched section views: slice the visible geometry with a plane,
// chain the segments into loops, and hatch the closed ones. Shared by the
// SectionView command and UpdateSectionViews (cmd_drafting2.cpp).
#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/types.h"

namespace dino8::app::drafting {

// Segments of every triangle `mesh` crosses `plane` on, each within `tol`
// chained into polylines (open runs and closed loops alike).
std::vector<std::vector<kernel::Point3d>> SliceMeshToChains(const ON_Mesh& mesh, const ON_Plane& plane, double tol);

}  // namespace dino8::app::drafting
