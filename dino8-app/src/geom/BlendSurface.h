// Hermite blend-surface construction shared by BlendEdge/BlendSrf
// (cmd_fillet.cpp) and by tests/test_g2_blend.cpp, which numerically
// verifies the claim made in BuildBlendSurfaceG2's own comment below.
//
// Two constructions live here:
//
//  - BuildBlendSurfaceG1: the original degree-3x3 per-row Hermite blend.
//    Its `tangent_boost` flag is exactly the old "Continuity=Curvature"
//    behaviour - a larger tangent magnitude, cosmetic only. It does NOT
//    match any second derivative and should not be described as G2.
//
//  - BuildBlendSurfaceG2: a degree-5x3 quintic-Hermite blend whose
//    cross-boundary second derivative at each boundary row is the EXACT
//    analytic directional second derivative (via ON_Surface::Ev2Der - no
//    finite differencing) of the adjacent surface, in the same outward
//    parameter direction as the tangent. See its own comment for exactly
//    what continuity guarantee this gives and does not give.
#pragma once

#include <functional>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"

class ON_Surface;
class ON_Curve;
class ON_NurbsSurface;

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

// Homogeneous "skinning" row - see BlendSurface.cpp's file banner.
struct HomogeneousRow {
  std::vector<ON_4dPoint> cv;  // already homogeneous: (w*x, w*y, w*z, w)
};

// rows[j] is the j-th row (same CV count `nu`); params gives each row's
// v-parameter. Produces a rational ON_NurbsSurface of order (u_order,
// min(4,nv)); every row keeps its own order/knot structure exactly (only
// the v-direction is a genuine global interpolation). Shared by the
// rolling-ball fillet arcs (cmd_fillet.cpp) and the Hermite blend rows
// below.
bool LoftRows(const std::vector<HomogeneousRow>& rows, const std::vector<double>& params_in, int u_order, ON_NurbsSurface& out);

// Interior parameter-space direction from (u,v) towards the surface's
// domain centre, as a 3D vector (a robust, if approximate away from
// rectangular domains, "into the face" probe) - used for the G1 path's
// tangent direction only.
Vector3d InteriorDirection3d(const ON_Surface& s, double u, double v);

// Exact analytic first/second derivative of surface `s` at (u, v) along
// the OUTWARD parameter direction (away from the domain's own interior -
// the direction a blend row leaves this surface's edge), via Ev2Der's
// exact partials. No finite differencing, so this stays exact even when
// (u, v) sits exactly on the domain boundary, which it does here (these
// are edge samples): d1 = a*Su + b*Sv, d2 = a^2*Suu + 2ab*Suv + b^2*Svv
// for the outward unit parameter direction (a, b).
struct DirectionalDerivs {
  Vector3d dir;    // unit 3D direction of d1 (falls back to the surface normal if d1 is ~0)
  Vector3d d1;     // exact d/dt of S(u + a*t, v + b*t) at t = 0
  Vector3d d2;     // exact d^2/dt^2 of the same
  bool ok = false;
};
DirectionalDerivs OutwardDirectionalDerivs(const ON_Surface& s, double u, double v);

// Curvature vector (dT/ds - the vector, not just its magnitude) of a
// curve with the given first/second derivative at a point, i.e.
// (d2 - (d1.d2 / |d1|^2) * d1) / |d1|^2. This is invariant under any
// positive rescaling of the curve's own parameter (reparametrizing
// t -> c*t for a constant c > 0 leaves it unchanged), which is exactly
// why matching (d1, d2) up to a shared positive scale, as
// BuildBlendSurfaceG2 does, reproduces the true curvature vector exactly
// rather than only approximately.
Vector3d CurvatureVectorFromDerivs(const Vector3d& d1, const Vector3d& d2);

// Builds a degree-3 x 3 Hermite blend surface between edge curves `ea`
// (on surface `sa`, whose face-normal-consistent uv is `uv_a_at`) and
// `eb`/`sb`/`uv_b_at` similarly. `tangent_boost` scales the tangent
// magnitude up - this is the pre-existing "Continuity=Curvature"
// approximation: it changes how far the blend leans out before turning,
// but does NOT match any second derivative, so it stays G1 (tangent-plane
// continuous) only, never G2. Returns false if the edges have too few
// usable samples.
bool BuildBlendSurfaceG1(const ON_Curve& ea, const ON_Surface& sa, const std::function<ON_2dPoint(double)>& uv_a_at,
                          const ON_Curve& eb, const ON_Surface& sb, const std::function<ON_2dPoint(double)>& uv_b_at,
                          bool tangent_boost, int samples, ON_NurbsSurface& out);

// Builds a degree-5 x 3 quintic-Hermite blend surface between the same
// inputs, with each boundary row's cross-boundary first AND second
// derivative set from OutwardDirectionalDerivs of the adjacent surface at
// that sample (both scaled by the same positive constant per boundary, so
// per CurvatureVectorFromDerivs's own comment the blend's curvature
// vector at every sampled boundary point matches the adjacent surface's
// curvature vector EXACTLY, in the specific cross-boundary parameter
// direction sampled - a real, numerically-verifiable G2 (curvature
// continuous) match in that direction, not the tangent-magnitude
// approximation BuildBlendSurfaceG1's `tangent_boost` gives.
//
// Scope, honestly: this matches curvature along the cross-boundary
// direction only (the direction the blend actually travels away from
// each edge), which is what a Rhino-style edge/surface blend needs and is
// what "the blend is G2 with its neighbours" means in that context. It is
// NOT a claim that every tangent-plane direction's curvature agrees (full
// second-fundamental-form / twist-term matching), which no single-patch
// blend of this kind attempts. See tests/test_g2_blend.cpp for the
// numerical check (both a flat and a genuinely curved adjacent surface)
// this comment's claim rests on. Returns false if the edges have too few
// usable samples, mirroring BuildBlendSurfaceG1.
bool BuildBlendSurfaceG2(const ON_Curve& ea, const ON_Surface& sa, const std::function<ON_2dPoint(double)>& uv_a_at,
                          const ON_Curve& eb, const ON_Surface& sb, const std::function<ON_2dPoint(double)>& uv_b_at,
                          int samples, ON_NurbsSurface& out);

}  // namespace dino8::app
