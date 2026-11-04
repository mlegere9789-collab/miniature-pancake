// Tunable forms of the edge-continuity constructions declared in surface.h (FitPatch / FitCurveNetwork with `edges`,
// LoftMatched). The members of NurbsSurface call these with their defaults; call these directly to see (or switch off) what
// the two refinements do:
//
//  * Derivative-row RE-WEIGHTING (FitPatch / FitCurveNetwork). The tangent-plane (and second-fundamental-form) rows are
//    weighted least-squares rows, so the tangent plane is met only as far as the base weight lets it be. After the first fit
//    the row weight is raised 100x at a time (up to `max_weight_boost`) while the fit still meets the data tolerance and the
//    gradient / Hessian residuals do not grow. `max_weight_boost` = 1 is the single-base-weight behaviour.
//  * Cross-derivative SCALE SEARCH (LoftMatched). NurbsSurface::MatchEdge() fixes the magnitude of the cross derivative with
//    one scale (the ratio of the two surfaces' mean cross speeds) although the adjacent surface only prescribes its
//    direction (the tangent plane); a wrong magnitude pulls the interior sections off their data. With `optimize_scale` each
//    matched end searches the scale (a scan over 2^-4 .. 2^4 times the default, then golden-section refinement) that
//    minimises the worst distance of the sections' samples from the rewritten surface.
#pragma once

#include <vector>

#include "dino8/kernel/surface.h"

namespace dino8::kernel {

// What the scale search did at each matched end of a LoftMatchedScaled() (0 = that end was not matched).
struct LoftMatchScaleReport {
  double start_scale = 0.0, end_scale = 0.0;
  // Worst distance of any section sample from the surface after MatchEdge() at ITS DEFAULT scale (before the search).
  double start_default_deviation = 0.0, end_default_deviation = 0.0;
};

// NurbsSurface::LoftMatched() with the scale search switchable. `scale_report` may be null.
Result LoftMatchedScaled(const std::vector<NurbsCurve>& sections, int degree, const NurbsSurface::LoftEndMatch* start,
                         const NurbsSurface::LoftEndMatch* end, double tolerance, NurbsSurface& out,
                         NurbsSurface::LoftMatchReport& report, double angle_tolerance, double curvature_tolerance, bool optimize_scale,
                         LoftMatchScaleReport* scale_report);

// NurbsSurface::FitPatch() (edges form) with the re-weighting limit given. `boost_used` (may be null) receives the row-weight
// factor of the returned fit (1 = base weight).
Result FitPatchReweighted(const std::vector<Point3d>& boundary_points, const std::vector<Point3d>& interior_points, double tolerance,
                          NurbsSurface& out, NurbsSurface::PatchFitReport* report, int max_count,
                          const std::vector<NurbsSurface::FitEdgeContinuity>& edges, double max_weight_boost, double* boost_used);

// NurbsSurface::FitCurveNetwork() (edges form) with the re-weighting limit given.
Result FitCurveNetworkReweighted(const std::vector<NurbsCurve>& curves, double tolerance, NurbsSurface& out,
                                 NurbsSurface::CurveNetworkReport& report, const std::vector<NurbsSurface::NetworkEdgeContinuity>& edges,
                                 double max_weight_boost, double* boost_used);

}  // namespace dino8::kernel
