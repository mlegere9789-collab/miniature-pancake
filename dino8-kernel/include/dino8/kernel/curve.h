#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::kernel {

// Wraps ON_NurbsCurve. Deliberately exposes the underlying ON_NurbsCurve
// (via raw()) rather than re-declaring every accessor OpenNURBS already
// has — later chunks (booleans, display) need the real object, not a
// facade that only covers what chunk 1 happened to need.
class NurbsCurve {
 public:
  // Builds a degree-`degree` NURBS curve interpolating a polyline through
  // `control_points` with uniform-ish knots. Not a general-purpose curve
  // fit — just enough to construct a testable curve without pulling in a
  // fitting algorithm this chunk doesn't own.
  static NurbsCurve FromControlPoints(const std::vector<Point3d>& control_points,
                                       int degree);

  int Degree() const;
  int ControlPointCount() const;

  // Elevates the curve's degree in place. Returns NoOpAlreadySatisfied if
  // `new_degree <= Degree()`.
  Result ElevateDegree(int new_degree);

  // Whether the curve's start and end points coincide - either because
  // it's genuinely periodic (its own knot vector wraps) or because a
  // clamped curve's own two endpoints just happen to be the same point
  // (e.g. `FromControlPoints()` given a control point list whose first
  // and last entries match). The surface-level counterpart to
  // `NurbsSurface::IsClosed()`. Delegates to `ON_NurbsCurve::IsClosed`
  // after verifying it's a real implementation (falls back to an actual
  // endpoint-coincidence check for a non-periodic curve, not a stub).
  // Confirmed by testing, not just reading the source: `ON_NurbsCurve::
  // IsClosed` unconditionally requires at least 4 control points before
  // it even looks at endpoint positions, so a 3-point coincident
  // -endpoint polyline (a perfectly valid, genuinely closed-looking
  // triangle-wedge shape) reports false here - a real, narrower
  // guarantee than "any coincident-endpoint curve reports closed".
  bool IsClosed() const;

  // Whether the curve's own knot vector is genuinely periodic - a
  // stronger condition than IsClosed() (every periodic curve is closed,
  // but a clamped curve can be closed - matching endpoints - without
  // being periodic at all). Delegates to `ON_NurbsCurve::IsPeriodic`
  // after the same stub-vs-real verification.
  bool IsPeriodic() const;

  // Reverses the curve's parameterization in place: what was
  // `PointAt(domain.Min())` becomes `PointAt(domain.Max())` and vice
  // versa (the curve's own 3D shape is unchanged - same points, opposite
  // direction of travel), so `TangentAt()` at any point flips sign too.
  // Confirmed by testing, not assumed: the domain interval's own
  // min/max *values* aren't necessarily preserved (a `[0, 1]` domain
  // came back as `[-1, 0]` in one verified case) - callers walking the
  // curve by parameter must re-fetch `raw().Domain()` after calling this
  // rather than reusing a domain captured beforehand. A real gap nothing
  // here could answer before: nothing in this file could flip a curve's
  // own direction without discarding it and rebuilding from reversed
  // control points (losing any degree elevation or other in-place edits
  // already applied). Delegates to `ON_NurbsCurve::Reverse` after
  // verifying it's a real implementation (reverses both the knot vector
  // and control point list, not a stub). Returns Result::Failed if
  // OpenNURBS' own call fails.
  Result Reverse();

  // Shortens the curve in place to just the sub-domain `[t0, t1]`
  // (`t0 < t1`, both within the curve's current domain) - the curve's
  // own shape outside that range is discarded, not just hidden, and its
  // new domain becomes exactly `[t0, t1]`. A real gap nothing here could
  // answer before: nothing in this file could cut a curve down to part
  // of itself without re-sampling points and rebuilding a brand new
  // curve through them (an approximation, not the exact same underlying
  // curve restricted to a smaller range). Delegates to
  // `ON_NurbsCurve::Trim` after verifying it's a real implementation (a
  // genuine de Boor knot-insertion algorithm, not a stub). Returns
  // Result::Failed if `t0 >= t1` or OpenNURBS' own call fails.
  Result Trim(double t0, double t1);

  // Splits the curve at parameter `t` (strictly inside the curve's
  // current domain, not at either end) into two independent curves
  // written to `out_left`/`out_right` - `out_left` covering the original
  // domain's start up to `t`, `out_right` covering `t` to the original
  // end - without modifying `*this`. The complement to Trim(): Trim()
  // keeps one sub-range and discards the rest, Split() keeps both
  // halves as separate curves. Delegates to `ON_NurbsCurve::Split` after
  // verifying it's a real implementation (genuine knot insertion at `t`
  // for each half, the same underlying algorithm Trim() uses, not a
  // stub). Returns Result::Failed if `t` isn't strictly inside the
  // curve's domain or if OpenNURBS' own call fails.
  Result Split(double t, NurbsCurve& out_left, NurbsCurve& out_right) const;

  // Extends the curve in place so its domain includes `[t0, t1]` -
  // Trim()'s opposite: instead of cutting the curve down, this
  // analytically extrapolates it outward past whichever end(s) of
  // `[t0, t1]` fall outside the curve's current domain, leaving the
  // curve's own existing shape over its original domain completely
  // unchanged (this is the curve's own documented guarantee, not just an
  // assumption: `ON_NurbsCurve::Extend` only moves the affected end's
  // knots/control points via a De Boor extrapolation, the same
  // underlying curve-representation machinery `Trim()`/`Split()` use).
  // Only extends whichever end(s) `[t0, t1]` actually reach past - if it
  // already sits entirely within the current domain, returns
  // NoOpAlreadySatisfied rather than calling into OpenNURBS at all.
  // Returns Result::Failed if `t0 >= t1`, the curve is closed (extending
  // a closed curve is undefined - matches `ON_NurbsCurve::Extend`'s own
  // documented restriction), or OpenNURBS' own call fails.
  Result Extend(double t0, double t1);

  Point3d PointAt(double t) const;

  // Finds the parameter along the curve's own domain whose PointAt() is
  // closest to `point`: a coarse `samples`-point scan of the domain to
  // bracket the nearest sample, then a golden-section refinement of the
  // distance-squared function within that bracket. This is a from-scratch
  // numeric search, not a wrapper - verified directly against the v8.34
  // source that OpenNURBS' public `ON_Curve` API has no
  // `GetClosestPoint()`/closest-point method at all (grepped the whole
  // source tree, not just this class), the same "declared for Rhino, not
  // present in the public build" gap this file already found for
  // `Length()`'s own arc-length method. Not a guaranteed global minimum
  // for a pathological multi-modal distance function (e.g. a curve
  // passing near `point` at more than one well-separated parameter) -
  // refines only around whichever coarse sample happens to be nearest,
  // the same "approximate, not exhaustive" honesty `Length()`'s own
  // polyline sampling already documents. Increasing `samples` narrows the
  // risk of missing a closer, separate local minimum, at proportional
  // cost.
  double ClosestPointParameter(Point3d point, int samples = 200) const;

  // The actual closest point: `PointAt(ClosestPointParameter(point,
  // samples))`. The curve-level counterpart to `Mesh::ClosestPoint()`.
  Point3d ClosestPoint(Point3d point, int samples = 200) const;

  // Delegates to `ON_Curve::GetTightBoundingBox`. DESPITE THE NAME, this
  // is *not* a genuine tight/exact bound for a general curve in the
  // public OpenNURBS build - verified by reading the source
  // (opennurbs_bezier.cpp): `ON_BezierCurve::GetTightBoundingBox`
  // literally calls `ON_GetPointListBoundingBox` (its own comment says
  // "good enough for file IO needs in the public source code version"),
  // i.e. each Bezier span's own *control-point* bounding box, not a real
  // extremum search. Confirmed by testing, not just reading: a quadratic
  // curve whose true extremum (0.5) lies strictly inside its parameter
  // domain gets bounded by its control point's coordinate (1.0) instead -
  // a real, valid (never excludes part of the curve), but not minimal,
  // bound. Exact only when the curve's true extremum happens to coincide
  // with a control point or an endpoint (a straight line; certain
  // standard rational-conic constructions, e.g. a NURBS circle whose
  // control points sit on-curve at the cardinal angles). Throws
  // std::runtime_error if OpenNURBS' own call fails.
  BoundingBox GetTightBoundingBox() const;

  // Approximate arc length via polyline sampling: evaluates `samples + 1`
  // points evenly across the curve's own parameter domain and sums the
  // straight-line distance between consecutive ones. This is a
  // from-scratch implementation, not a wrapper - verified directly
  // against the v8.34 source that OpenNURBS' public `ON_Curve` API has no
  // `GetLength()`/arc-length method at all (grepped the whole source
  // tree, not just this class), the same "declared for Rhino, not present
  // in the public build" pattern chunk 2 found for `ON_Brep::CreateMesh`
  // and this chunk found for `ON_SubD::BrepForm`. A polyline's chord
  // length always understates a smooth curve's true arc length, so this
  // converges to the true length from below as `samples` increases - for
  // a straight-line curve (no curvature to approximate) it's exact at any
  // sample count.
  double Length(int samples = 1000) const;

  // Unit tangent direction at parameter `t` - the direction of travel
  // along the curve, not a raw (unnormalized) derivative. Delegates to
  // `ON_Curve::TangentAt` - verified as a real implementation (calls
  // through to `Ev1Der`/`EvTangent`, not a stub like `ON_Brep::CreateMesh`
  // or `ON_SubD::BrepForm`) before relying on it, same standing discipline
  // this file already applied to `Length()`.
  Vector3d TangentAt(double t) const;

  const ON_NurbsCurve& raw() const { return curve_; }
  ON_NurbsCurve& raw() { return curve_; }

 private:
  ON_NurbsCurve curve_;
};

}  // namespace dino8::kernel
