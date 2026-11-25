#pragma once

// The kernel's own ANALYSIS fan for a planar sweep cap (implementation
// plumbing shared by sweep.cpp, which builds it, and brep.cpp, which
// consumes it - not part of the public dino8::kernel API).
//
// A Loft()'s end cap is ONE planar trimmed face in the B-rep (a plane
// patch whose single outer loop is the section curve mated to the wall's
// boundary edge: sweep.cpp's AddPlanarTrimmedCap()). The kernel's own
// uniform-lattice tessellators and exact integrators, however, want a
// surface whose (u, v) domain IS the face - they have no notion of a
// curved trim that must share its sample points with the neighbouring
// wall's lattice at EVERY (u_divisions, v_divisions) pair, which is the
// property (a closed manifold at any division pair) the older fan caps
// gave them. So for such a cap they derive the fan on the fly, from the
// cap's own boundary curve, exactly as the sweep factories always built it
// (apex in the section's kernel, degree-(p, 1) surface collapsing to it),
// and use it for meshing and quadrature only - never stored, so it cannot
// go stale under a transform or a face edit.

#include <memory>

#include <opennurbs.h>

namespace dino8::kernel::detail {

// The fan surface to the star-shaped planar region bounded by the closed
// curve `boundary`, with its u x v normal agreeing with `outward_normal`
// (the boundary is reversed, keeping its domain, if the native fan faces
// the other way). Returns nullptr when `boundary` is not closed, not
// planar, or its region is not star-shaped (no fan exists).
std::unique_ptr<ON_NurbsSurface> PlanarCapFanSurface(const ON_NurbsCurve& boundary, const ON_3dVector& outward_normal);

}  // namespace dino8::kernel::detail
