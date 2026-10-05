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
// continuous) only, never G2. `width_frac_at`, when non-null, is sampled
// at each row's own t in [0, 1] (the same t the row is built at) and
// REPLACES the fixed 0.35/0.55 tangent-magnitude fraction of the local
// gap with its own return value - this is the hook VariableBlendSrf uses
// (cmd_fillet.cpp) to make the blend's cross-section width genuinely vary
// along the rail, independent of any rolling-ball radius machinery.
// Returns false if the edges have too few usable samples.
bool BuildBlendSurfaceG1(const ON_Curve& ea, const ON_Surface& sa, const std::function<ON_2dPoint(double)>& uv_a_at,
                          const ON_Curve& eb, const ON_Surface& sb, const std::function<ON_2dPoint(double)>& uv_b_at,
                          bool tangent_boost, int samples, ON_NurbsSurface& out,
                          const std::function<double(double)>& width_frac_at = nullptr);

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
// this comment's claim rests on. `width_frac_at`, when non-null, works
// exactly as in BuildBlendSurfaceG1: sampled per-row at that row's own t
// and substituted for the fixed 0.55 fraction, scaling BOTH the tangent
// (d1) and, because CurvatureVectorFromDerivs is scale-invariant only
// under matched (d1, d2) scaling (see HermiteRowG2's own comment), the
// second-derivative term consistently, so a varying width still keeps the
// exact per-row curvature match - the width and the G2 matching are
// independent knobs. Returns false if the edges have too few usable
// samples, mirroring BuildBlendSurfaceG1.
bool BuildBlendSurfaceG2(const ON_Curve& ea, const ON_Surface& sa, const std::function<ON_2dPoint(double)>& uv_a_at,
                          const ON_Curve& eb, const ON_Surface& sb, const std::function<ON_2dPoint(double)>& uv_b_at,
                          int samples, ON_NurbsSurface& out,
                          const std::function<double(double)>& width_frac_at = nullptr);

// Max Euclidean distance between `a` and `b` sampled at the same
// (grid_n+1) x (grid_n+1) grid of (u, v) parameters, one per domain
// fraction i/grid_n, j/grid_n (both surfaces' own domains are always
// exactly [0,1]x[0,1] by construction - see LoftRows' own knot-setting
// code in BlendSurface.cpp - so evaluating both at the SAME (u, v) is
// meaningful without any reparametrization or closest-point search).
// Shared by BuildBlendSurfaceG1Adaptive/BuildBlendSurfaceG2Adaptive below
// to measure how much a blend surface's own shape still changes when its
// row sample count is doubled - the practical stand-in for "distance to
// the true rolling-ball envelope" a general two-freeform-surface blend has
// no closed form for at all (see BuildBlendSurfaceG2's own doc comment for
// why: a canal surface over a non-linear radius/rail law is not rational
// in general), the same convergence-based bound any adaptive
// discretization of an unknown-closed-form surface has to fall back on.
double MaxSurfaceGap(const ON_NurbsSurface& a, const ON_NurbsSurface& b, int grid_n);

// ADAPTIVE, TOLERANCE-ENFORCING wrapper around BuildBlendSurfaceG1: builds
// at `min_samples`, then repeatedly DOUBLES the row sample count and
// measures MaxSurfaceGap between the last two resolutions (see that
// function's own doc comment) until the gap is at most `max_gap`, at which
// point `out` is set to the finer (converged) build and this returns true;
// or until doubling would exceed `max_samples`, at which point `out` is
// set to the last successfully built (coarser) surface and this returns
// false - max_gap could not be certified within the given sample budget,
// disclosed via the return value rather than silently accepted.
// `achieved_gap_out`, when non-null, always receives the LAST measured gap
// (the one between the final pair of resolutions tried), on both success
// and failure, so a caller can report how close a failed attempt actually
// got. Returns false immediately, without measuring any gap (and leaving
// `*achieved_gap_out` at +infinity, if requested), if the very first
// BuildBlendSurfaceG1 call itself fails (too few usable samples) - the
// same "not enough input to build anything at all" failure that function
// already reports, now surfaced through this one too rather than treated
// as gap = 0.
bool BuildBlendSurfaceG1Adaptive(const ON_Curve& ea, const ON_Surface& sa, const std::function<ON_2dPoint(double)>& uv_a_at,
                                  const ON_Curve& eb, const ON_Surface& sb, const std::function<ON_2dPoint(double)>& uv_b_at,
                                  bool tangent_boost, double max_gap, int min_samples, int max_samples,
                                  ON_NurbsSurface& out, double* achieved_gap_out = nullptr,
                                  const std::function<double(double)>& width_frac_at = nullptr);

// Exactly BuildBlendSurfaceG1Adaptive's own construction, wrapping
// BuildBlendSurfaceG2 instead - see that function's own doc comment for
// every claim here, mirrored.
bool BuildBlendSurfaceG2Adaptive(const ON_Curve& ea, const ON_Surface& sa, const std::function<ON_2dPoint(double)>& uv_a_at,
                                  const ON_Curve& eb, const ON_Surface& sb, const std::function<ON_2dPoint(double)>& uv_b_at,
                                  double max_gap, int min_samples, int max_samples, ON_NurbsSurface& out,
                                  double* achieved_gap_out = nullptr,
                                  const std::function<double(double)>& width_frac_at = nullptr);

// ADAPTIVE wrapper around kernel::NurbsSurface::BlendSurfaces() - the exact
// Hermite-skin kernel construction (surface.h), reachable only when both
// edges are genuinely isoparametric boundaries of their own NurbsSurface
// (BlendSrfCommand's/VariableBlendSrfCommand's own boundary-iso-curve pick,
// cmd_fillet.cpp, always is). Unlike BuildBlendSurfaceG1/G2 above, which
// build from a generic picked edge curve plus a uv_at projection, this one
// takes the fixed-direction/at-max description BlendSurfaces() itself needs
// directly (dir0/at_max0 for srf0, dir1/at_max1 for srf1 - see that
// function's own doc comment for exactly what these mean), and reaches
// continuity 3 (G3), not just G1/G2.
//
// Mirrors BuildBlendSurfaceG1Adaptive's own convergence/budget contract
// exactly (same shared AdaptiveRefine() driver, BlendSurface.cpp): builds at
// `min_rows`, then repeatedly doubles the row count and measures
// MaxSurfaceGap between the last two resolutions until the gap is at most
// `max_gap` (returns true, `out` holds the finer build) or doubling would
// exceed `max_rows` (returns false, `out` holds the best attempt,
// `*achieved_gap_out` the last measured gap). Returns false immediately,
// `out` untouched, `*achieved_gap_out` left at +infinity, if the very first
// kernel::NurbsSurface::BlendSurfaces call itself returns Result::Failed
// (e.g. the two rail points at t=0 coincide, or continuity/dir0/dir1 are
// invalid - though a caller is expected to have already validated those).
bool BuildBlendSurfaceKernelAdaptive(int dir0, bool at_max0, const dino8::kernel::NurbsSurface& srf0, int dir1,
                                      bool at_max1, const dino8::kernel::NurbsSurface& srf1, bool reverse_rail1,
                                      int continuity, double max_gap, int min_rows, int max_rows,
                                      ON_NurbsSurface& out, double* achieved_gap_out = nullptr);

}  // namespace dino8::app
