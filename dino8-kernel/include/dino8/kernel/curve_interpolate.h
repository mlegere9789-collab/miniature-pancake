#pragma once

#include <optional>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// General global curve interpolation through data points (Piegl & Tiller, "The NURBS Book", sec. 9.2): a clamped,
// non-rational B-spline of any degree p in [1, points - 1] that passes through every point, with the parameter style the
// caller chooses and optional end-tangent constraints. Until now the kernel's only exact curve interpolant was the cubic
// `InterpolateCubic()` (chord length, no end conditions, open/wrapped-closed only); `NurbsCurve::FitLeastSquares()` is an
// approximation, not an interpolation.

enum class InterpolationParameter {
  Chord,        // t_k proportional to the cumulative chord length, normalised to [0, 1]
  Centripetal,  // t_k proportional to the cumulative square root of the chord length, normalised to [0, 1]
  Uniform,      // t_k = k / (points - 1)
};

struct CurveInterpolationOptions {
  int degree = 3;
  InterpolationParameter parameter = InterpolationParameter::Chord;
  // Explicit data parameters, one per point, strictly increasing. When non-empty `parameter` is ignored and the curve's
  // domain is [explicit_parameters.front(), explicit_parameters.back()] instead of [0, 1].
  std::vector<double> explicit_parameters;
  // Optional end conditions (need degree >= 2). With `tangents_are_derivatives` false (the default) the vector is a
  // DIRECTION: its length is ignored and the end derivative is set to that direction times (total chord length / domain
  // length), the Piegl & Tiller recommendation, so the curve's speed at the end is comparable to its average speed. With it
  // true the vector is dC/dt at the end exactly, in the curve's own parameter.
  std::optional<Vector3d> start_tangent;
  std::optional<Vector3d> end_tangent;
  bool tangents_are_derivatives = false;
};

// What InterpolateCurve() measured on the curve it built (evaluated afterwards, not assumed from the construction).
struct CurveInterpolationReport {
  std::vector<double> parameters;       // the data parameters used, one per point
  double max_position_residual = 0.0;   // max |C(t_k) - Q_k|
  double start_tangent_residual = 0.0;  // |C'(domain start) - requested derivative| (0 when no start tangent requested)
  double end_tangent_residual = 0.0;
};

// Builds the interpolant into `out` (control point count = points + number of tangent constraints, clamped knot vector made of
// the averaged data parameters). Returns Result::Failed, leaving `out` unchanged, for: fewer than 2 points; a non-finite
// coordinate; degree < 1 or degree > (points - 1 + number of tangent constraints); coincident consecutive points or
// non-increasing explicit parameters (the collocation system would be singular); explicit_parameters of the wrong length; a
// tangent with degree 1, a non-finite tangent, or (direction mode) a zero tangent; or a singular linear system. At most
// 4000 points. `report`, when given, is filled on success.
Result InterpolateCurve(const std::vector<Point3d>& points, const CurveInterpolationOptions& options, NurbsCurve& out,
                        CurveInterpolationReport* report = nullptr);

}  // namespace dino8::kernel
