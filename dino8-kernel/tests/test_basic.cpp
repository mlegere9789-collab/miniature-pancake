// Minimal assert-based smoke tests for chunk 1's exit criteria. Not pulling
// in a test framework dependency for four checks.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/file_io.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "dino8/kernel/surface.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++g_failures;
  } else {
    std::printf("ok: %s\n", what);
  }
}

void TestCurveDegreeElevation() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  std::vector<Point3d> pts = {
      Point3d(0, 0, 0),
      Point3d(1, 2, 0),
      Point3d(2, 0, 0),
      Point3d(3, 2, 0),
  };
  NurbsCurve curve = NurbsCurve::FromControlPoints(pts, /*degree=*/3);
  Check(curve.Degree() == 3, "curve constructed at requested degree");

  const auto result = curve.ElevateDegree(5);
  Check(result == dino8::kernel::Result::Ok, "degree elevation succeeded");
  Check(curve.Degree() == 5, "curve degree increased to 5");
}

void TestCurveLength() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // A degree-1 (straight-line) curve has no curvature for polyline
  // sampling to approximate away - Length() should be exact (the true
  // 3-4-5 distance, 5.0) at any sample count, not just a large one.
  const std::vector<Point3d> line_pts = {Point3d(0, 0, 0), Point3d(3, 4, 0)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(line_pts, /*degree=*/1);
  Check(std::abs(line.Length(4) - 5.0) < 1e-9,
        "a straight-line curve's length is exact (5.0, the 3-4-5 "
        "distance) even at a small sample count");
  Check(std::abs(line.Length(1000) - 5.0) < 1e-9,
        "...and stays exact at a large sample count too");

  // A genuinely curved case: measure convergence directly rather than
  // assuming it, the same discipline used for SubD's volume-shrink
  // measurements. A polyline's chords always understate a smooth curve's
  // true length, so Length() should increase monotonically (not
  // decrease, not oscillate) as sample count grows, and the increments
  // should shrink (approaching some limit), not diverge.
  const std::vector<Point3d> curved_pts = {
      Point3d(0, 0, 0),
      Point3d(1, 3, 0),
      Point3d(2, -3, 0),
      Point3d(3, 0, 0),
  };
  const NurbsCurve curved = NurbsCurve::FromControlPoints(curved_pts, /*degree=*/3);
  const double length_10 = curved.Length(10);
  const double length_100 = curved.Length(100);
  const double length_1000 = curved.Length(1000);
  Check(length_10 <= length_100 + 1e-12 && length_100 <= length_1000 + 1e-12,
        "a curved curve's approximated length increases monotonically "
        "with sample count (chords underestimate the true arc length)");
  Check((length_1000 - length_100) < (length_100 - length_10),
        "the increase per 10x more samples shrinks - converging toward a "
        "limit, not diverging");
}

void TestCurveParameterAtArcLength() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // Same 3-4-5 line as TestCurveLength(), total length exactly 5.0.
  // Uniform speed along a straight line makes ParameterAtArcLength()
  // exact at any sample count - confirmed by a debug run before
  // finalizing: half the arc length (2.5) lands exactly at t=0.5, the
  // line's own exact midpoint (1.5, 2, 0).
  const std::vector<Point3d> line_pts = {Point3d(0, 0, 0), Point3d(3, 4, 0)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(line_pts, /*degree=*/1);
  const double t_half = line.ParameterAtArcLength(2.5);
  Check(std::abs(t_half - 0.5) < 1e-9, "half the line's arc length lands exactly at t=0.5");
  const Point3d p_half = line.PointAt(t_half);
  Check(std::abs(p_half.x - 1.5) < 1e-9 && std::abs(p_half.y - 2.0) < 1e-9,
        "...which is exactly the line's own midpoint (1.5, 2, 0)");

  Check(line.ParameterAtArcLength(0.0) == 0.0,
        "an arc length of exactly 0 returns exactly the domain's own start");
  Check(std::abs(line.ParameterAtArcLength(5.0) - 1.0) < 1e-9,
        "an arc length of exactly the curve's own total length returns "
        "exactly the domain's own end");
  Check(line.ParameterAtArcLength(-1.0) == 0.0,
        "a negative arc length clamps to the domain's own start rather "
        "than extrapolating past it");
  Check(std::abs(line.ParameterAtArcLength(100.0) - 1.0) < 1e-9,
        "an arc length past the curve's own total length clamps to the "
        "domain's own end rather than extrapolating past it");

  // A full circle of known radius: its own quarter-arc-length point
  // (circumference/4) must land exactly on the geometric quarter point
  // (0, radius, 0) for a circle centered at the origin starting at
  // (radius, 0, 0) - hand-derivable exact, confirmed by a debug run.
  const double radius = 5.0;
  const ON_Circle on_circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), radius);
  ON_NurbsCurve nurbs_form;
  Check(on_circle.GetNurbForm(nurbs_form) != 0, "ON_Circle::GetNurbForm succeeds");
  NurbsCurve circle;
  circle.raw() = nurbs_form;
  const double circumference = circle.Length();
  const Point3d p_quarter = circle.PointAt(circle.ParameterAtArcLength(circumference / 4.0));
  Check(std::abs(p_quarter.x) < 1e-6 && std::abs(p_quarter.y - radius) < 1e-6,
        "a quarter of the circle's own arc length lands exactly on its "
        "geometric quarter point (0, radius, 0)");
}

void TestCurveTangentAt() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // A straight-line curve's tangent is exactly the line's own unit
  // direction at every parameter value - no curvature to introduce any
  // variation, so this is hand-derivable exact rather than approximate.
  const std::vector<Point3d> line_pts = {Point3d(0, 0, 0), Point3d(3, 4, 0)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(line_pts, /*degree=*/1);
  const ON_Interval line_domain = line.raw().Domain();
  const Vector3d expected_direction(3.0 / 5.0, 4.0 / 5.0, 0.0);
  for (const double normalized_t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    const double t = line_domain.ParameterAt(normalized_t);
    const Vector3d tangent = line.TangentAt(t);
    Check(std::abs(tangent.x - expected_direction.x) < 1e-9 &&
              std::abs(tangent.y - expected_direction.y) < 1e-9 &&
              std::abs(tangent.z - expected_direction.z) < 1e-9,
          "a straight-line curve's tangent is exactly its own unit "
          "direction (3/5, 4/5, 0) at every parameter value");
  }

  // A genuinely curved case: TangentAt() should point the same way as a
  // central-finite-difference approximation of the derivative at the
  // same parameter - measured agreement, not just "it returns a unit
  // vector."
  const std::vector<Point3d> curved_pts = {
      Point3d(0, 0, 0),
      Point3d(1, 3, 0),
      Point3d(2, -3, 0),
      Point3d(3, 0, 0),
  };
  const NurbsCurve curved = NurbsCurve::FromControlPoints(curved_pts, /*degree=*/3);
  const ON_Interval curved_domain = curved.raw().Domain();
  constexpr double kFiniteDifferenceStep = 1e-5;
  for (const double normalized_t : {0.2, 0.4, 0.6, 0.8}) {
    const double t = curved_domain.ParameterAt(normalized_t);
    const Vector3d tangent = curved.TangentAt(t);
    Check(std::abs(tangent.Length() - 1.0) < 1e-9, "TangentAt() returns a unit vector");

    const Point3d before = curved.PointAt(t - kFiniteDifferenceStep);
    const Point3d after = curved.PointAt(t + kFiniteDifferenceStep);
    Vector3d finite_difference = after - before;
    finite_difference.Unitize();
    const double alignment = ON_DotProduct(tangent, finite_difference);
    Check(alignment > 1.0 - 1e-6,
          "TangentAt() points the same way as a central-finite-difference "
          "approximation of the curve's own derivative");
  }
}

void TestCurveGetTightBoundingBox() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // A straight-line curve's tight bounding box is exactly its two
  // endpoints' min/max - hand-derivable exact, no curvature involved.
  const std::vector<Point3d> line_pts = {Point3d(-1, 5, 2), Point3d(3, -2, 7)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(line_pts, /*degree=*/1);
  const auto line_bounds = line.GetTightBoundingBox();
  Check(line_bounds.min.x == -1.0 && line_bounds.min.y == -2.0 && line_bounds.min.z == 2.0,
        "a straight line's tight bounding box min corner is exactly its "
        "own low endpoint coordinates");
  Check(line_bounds.max.x == 3.0 && line_bounds.max.y == 5.0 && line_bounds.max.z == 7.0,
        "a straight line's tight bounding box max corner is exactly its "
        "own high endpoint coordinates");

  // A genuinely curved case, and a real discovery: a quadratic
  // Bezier-equivalent NURBS curve through (0,0,0), (1,1,0), (2,0,0) has
  // P(t) = (1-t)^2*P0 + 2t(1-t)*P1 + t^2*P2, so its *true* y-extent is
  // exactly [0, 0.5] (dy/dt = 2-4t = 0 at t=0.5, y(0.5) = 0.5 -
  // confirmed directly via PointAt() below, not just algebra). Despite
  // its name, `ON_Curve::GetTightBoundingBox`'s public-build
  // implementation does *not* compute that: reading the source
  // (opennurbs_bezier.cpp) shows `ON_BezierCurve::GetTightBoundingBox`
  // literally calls `ON_GetPointListBoundingBox` - its own comment says
  // "good enough for file IO needs in the public source code version" -
  // i.e. the *control-point* bounding box, not a real extremum search.
  // So this returns y_max = 1.0 (the middle control point's own y),
  // exactly double the curve's true 0.5 - the same "declared for Rhino,
  // degraded in the public build" pattern this codebase has found
  // before (`ON_Brep::CreateMesh`, `ON_SubD::BrepForm`), just less
  // total than those: still a real, valid (if not minimal) bound, never
  // wrong in the sense of excluding part of the curve, just measurably
  // not "tight" for a curve whose extremum isn't a control point.
  const std::vector<Point3d> bulge_pts = {Point3d(0, 0, 0), Point3d(1, 1, 0), Point3d(2, 0, 0)};
  const NurbsCurve bulge = NurbsCurve::FromControlPoints(bulge_pts, /*degree=*/2);
  const Point3d true_midpoint = bulge.PointAt(bulge.raw().Domain().ParameterAt(0.5));
  Check(std::abs(true_midpoint.y - 0.5) < 1e-9,
        "the quadratic curve's own true midpoint y-coordinate is exactly "
        "0.5, confirmed directly via PointAt() (not just the algebra)");
  const auto bulge_bounds = bulge.GetTightBoundingBox();
  Check(std::abs(bulge_bounds.max.y - 1.0) < 1e-9,
        "GetTightBoundingBox()'s public-build implementation returns the "
        "*control-point* bound (y=1.0, the middle control point's own "
        "y), not the curve's true tight extremum (0.5) - a real, "
        "documented degradation in the public OpenNURBS build, verified "
        "by testing rather than assumed from the method's name");
  Check(std::abs(bulge_bounds.min.x - 0.0) < 1e-9 && std::abs(bulge_bounds.max.x - 2.0) < 1e-9,
        "the same curve's x-extent (2t, monotonic) is still exactly "
        "[0, 2] either way, since the endpoints already bound it exactly");
}

void TestCurveIsClosed() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  const std::vector<Point3d> open_pts = {Point3d(0, 0, 0), Point3d(1, 1, 0), Point3d(2, 0, 0)};
  const NurbsCurve open_curve = NurbsCurve::FromControlPoints(open_pts, 2);
  Check(!open_curve.IsClosed() && !open_curve.IsPeriodic(),
        "a curve whose endpoints differ is neither closed nor periodic");

  // Same shape, but the control point list's first and last entries
  // coincide - closed via ordinary endpoint coincidence, not a periodic
  // knot vector (FromControlPoints() always builds a clamped knot
  // vector). Confirmed by testing, not assumed: IsClosed() is true while
  // IsPeriodic() stays false, the same "closed without being periodic"
  // distinction NurbsSurface::IsClosed()/IsPeriodic() already
  // demonstrated for a cylinder wall.
  const std::vector<Point3d> closed_pts = {Point3d(0, 0, 0), Point3d(1, 1, 0), Point3d(2, 0, 0),
                                            Point3d(0, 0, 0)};
  const NurbsCurve closed_curve = NurbsCurve::FromControlPoints(closed_pts, 2);
  Check(closed_curve.IsClosed(),
        "a curve whose first and last control points coincide is closed");
  Check(!closed_curve.IsPeriodic(),
        "...but not periodic, since FromControlPoints() always builds a "
        "clamped (not periodic) knot vector - IsClosed() and "
        "IsPeriodic() really do answer different questions here too");
}

void TestCurveIsPlanar() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // All 4 control points lie in the z=0 plane - a genuinely planar
  // curve, confirmed by a debug run before finalizing this assertion.
  const std::vector<Point3d> planar_pts = {Point3d(0, 0, 0), Point3d(1, 1, 0), Point3d(2, 0, 0),
                                            Point3d(3, 2, 0)};
  const NurbsCurve planar = NurbsCurve::FromControlPoints(planar_pts, /*degree=*/3);
  Check(planar.IsPlanar(), "a curve whose control points all share z=0 reports planar");

  // These 4 control points are genuinely non-coplanar (no single plane
  // passes through all of them) - reports non-planar at a tight
  // tolerance, but planar once the tolerance is generous enough to
  // swallow the deviation (a large but finite tolerance, not something
  // that would be true for literally any curve).
  const std::vector<Point3d> skew_pts = {Point3d(0, 0, 0), Point3d(1, 0, 1), Point3d(2, 1, 0),
                                          Point3d(0, 2, 3)};
  const NurbsCurve skew = NurbsCurve::FromControlPoints(skew_pts, /*degree=*/3);
  Check(!skew.IsPlanar(1e-9), "a genuinely non-coplanar curve reports non-planar at a tight tolerance");
  Check(skew.IsPlanar(100.0),
        "...but reports planar once the tolerance is generous enough to "
        "swallow its actual (much smaller) deviation from some plane");

  // A straight line is trivially planar - any plane containing it works
  // - verified directly, not assumed.
  const std::vector<Point3d> line_pts = {Point3d(0, 0, 0), Point3d(1, 2, 3)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(line_pts, /*degree=*/1);
  Check(line.IsPlanar(), "a straight line reports planar at the default tolerance");
}

void TestCurveIsLinear() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // A degree-1 curve is trivially linear - confirmed directly, not
  // assumed.
  const std::vector<Point3d> line_pts = {Point3d(0, 0, 0), Point3d(1, 2, 3)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(line_pts, /*degree=*/1);
  Check(line.IsLinear(), "a straight line reports linear at the default tolerance");

  // Same quadratic bulge curve as TestCurveGetTightBoundingBox: it
  // genuinely deviates from the straight line between its own endpoints
  // (0,0,0) and (2,0,0) - confirmed by a debug run before finalizing
  // these assertions: reports non-linear at a tight tolerance, but
  // linear once the tolerance is generous enough to swallow that
  // deviation.
  const std::vector<Point3d> curved_pts = {Point3d(0, 0, 0), Point3d(1, 1, 0), Point3d(2, 0, 0)};
  const NurbsCurve curved = NurbsCurve::FromControlPoints(curved_pts, /*degree=*/2);
  Check(!curved.IsLinear(1e-9), "a genuinely curved curve reports non-linear at a tight tolerance");
  Check(curved.IsLinear(100.0),
        "...but reports linear once the tolerance is generous enough to "
        "swallow its actual (much smaller) deviation from the "
        "endpoint-to-endpoint line");
}

void TestCurveIsArcAndIsCircle() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // A genuine full circle via ON_Circle::GetNurbForm - both IsArc() and
  // the stronger IsCircle() should report true. Confirmed by a debug
  // run before finalizing these assertions.
  const ON_Circle on_circle(ON_Plane(ON_3dPoint(1, 2, 0), ON_3dVector(0, 0, 1)), 5.0);
  ON_NurbsCurve full_circle_nurbs;
  Check(on_circle.GetNurbForm(full_circle_nurbs) != 0, "ON_Circle::GetNurbForm succeeds");
  NurbsCurve full_circle;
  full_circle.raw() = full_circle_nurbs;
  Check(full_circle.IsArc(), "a genuine full circle reports IsArc() true");
  Check(full_circle.IsCircle(), "...and also reports the stronger IsCircle() true");

  // A quarter arc of the identical circle: still an arc, but NOT a full
  // circle - this is the real distinguishing case proving IsCircle()
  // isn't just IsArc() under a different name.
  const ON_Arc on_arc(on_circle, ON_PI / 2.0);
  ON_NurbsCurve partial_arc_nurbs;
  Check(on_arc.GetNurbForm(partial_arc_nurbs) != 0, "ON_Arc::GetNurbForm succeeds");
  NurbsCurve partial_arc;
  partial_arc.raw() = partial_arc_nurbs;
  Check(partial_arc.IsArc(), "a quarter arc of the same circle still reports IsArc() true");
  Check(!partial_arc.IsCircle(),
        "...but correctly reports IsCircle() false, since its own angle "
        "isn't the full 2*pi");

  // A straight line is neither.
  const std::vector<Point3d> line_pts = {Point3d(0, 0, 0), Point3d(1, 2, 3)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(line_pts, /*degree=*/1);
  Check(!line.IsArc() && !line.IsCircle(), "a straight line reports both IsArc() and IsCircle() false");
}

void TestCurveReverse() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;
  using dino8::kernel::Result;
  using dino8::kernel::Vector3d;

  const std::vector<Point3d> pts = {Point3d(0, 0, 0), Point3d(1, 3, 0), Point3d(3, 4, 0)};
  NurbsCurve curve = NurbsCurve::FromControlPoints(pts, /*degree=*/2);
  const ON_Interval domain = curve.raw().Domain();

  // Sample a handful of points and tangents before reversing.
  std::vector<Point3d> points_before;
  std::vector<Vector3d> tangents_before;
  for (const double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    const double param = domain.ParameterAt(t);
    points_before.push_back(curve.PointAt(param));
    tangents_before.push_back(curve.TangentAt(param));
  }

  Check(curve.Reverse() == Result::Ok, "NurbsCurve::Reverse() succeeds");
  // Reverse() doesn't necessarily preserve the domain interval itself
  // (confirmed by testing: [0,1] became [-1,0] here) - only the
  // normalized position within it corresponds to the original curve's
  // mirrored position, so re-fetch the domain fresh rather than reusing
  // the pre-reversal one.
  const ON_Interval domain_after = curve.raw().Domain();

  // PointAt(t) after reversing must equal PointAt(1-t) before reversing -
  // same 3D points, opposite direction of travel - and the tangent at
  // that same point must point exactly the opposite way.
  for (size_t i = 0; i < points_before.size(); ++i) {
    const double t = static_cast<double>(i) / 4.0;
    const double reversed_param = domain_after.ParameterAt(t);
    const Point3d point_after = curve.PointAt(reversed_param);
    const Point3d& expected_point = points_before[points_before.size() - 1 - i];
    Check((point_after - expected_point).Length() < 1e-9,
          "after Reverse(), the point at parameter t exactly matches the "
          "original curve's point at parameter (1-t)");

    const Vector3d tangent_after = curve.TangentAt(reversed_param);
    const Vector3d& expected_tangent = tangents_before[tangents_before.size() - 1 - i];
    Check((tangent_after + expected_tangent).Length() < 1e-9,
          "...and the tangent there is exactly the negation of the "
          "original curve's tangent at parameter (1-t)");
  }
}

void TestCurveTrim() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;
  using dino8::kernel::Result;

  // A straight line from (0,0,0) to (10,0,0) with domain [0,1]:
  // P(t) = (10t, 0, 0), so trimming to [0.2, 0.7] should keep exactly
  // the sub-segment from (2,0,0) to (7,0,0) - hand-derivable exact,
  // since a line has no curvature for a knot-insertion-based trim to
  // approximate away.
  const std::vector<Point3d> pts = {Point3d(0, 0, 0), Point3d(10, 0, 0)};
  NurbsCurve line = NurbsCurve::FromControlPoints(pts, /*degree=*/1);
  Check(line.Trim(0.2, 0.7) == Result::Ok, "NurbsCurve::Trim() succeeds");

  // Confirmed by testing, not assumed: the new domain is exactly the
  // trimmed interval [0.2, 0.7], not, say, renormalized back to [0,1].
  const ON_Interval domain_after = line.raw().Domain();
  Check(std::abs(domain_after.Min() - 0.2) < 1e-9 && std::abs(domain_after.Max() - 0.7) < 1e-9,
        "the trimmed curve's own domain is exactly [0.2, 0.7], the "
        "interval it was trimmed to");

  const Point3d start = line.PointAt(domain_after.Min());
  const Point3d end = line.PointAt(domain_after.Max());
  Check(std::abs(start.x - 2.0) < 1e-9 && std::abs(start.y) < 1e-9 && std::abs(start.z) < 1e-9,
        "the trimmed line's start point is exactly (2,0,0)");
  Check(std::abs(end.x - 7.0) < 1e-9 && std::abs(end.y) < 1e-9 && std::abs(end.z) < 1e-9,
        "the trimmed line's end point is exactly (7,0,0)");
  Check(std::abs(line.Length() - 5.0) < 1e-9,
        "the trimmed line's own length is exactly 5.0 (7-2), not the "
        "original untrimmed length of 10");

  bool threw_or_failed = false;
  NurbsCurve backwards = NurbsCurve::FromControlPoints(pts, 1);
  if (backwards.Trim(0.7, 0.2) == Result::Failed) {
    threw_or_failed = true;
  }
  Check(threw_or_failed,
        "Trim() fails on a backwards interval (t0 >= t1) rather than "
        "silently doing something undefined");
}

void TestCurveSplit() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;
  using dino8::kernel::Result;

  // Same line as TestCurveTrim(): (0,0,0) to (10,0,0), domain [0,1],
  // P(t) = (10t, 0, 0). Splitting at t=0.4 should give a left half
  // covering [0, 0.4] -> (0,0,0)-(4,0,0) and a right half covering
  // [0.4, 1] -> (4,0,0)-(10,0,0), sharing the exact split point - all
  // hand-derivable exact since a line has no curvature to approximate.
  const std::vector<Point3d> pts = {Point3d(0, 0, 0), Point3d(10, 0, 0)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(pts, /*degree=*/1);
  NurbsCurve left, right;
  Check(line.Split(0.4, left, right) == Result::Ok, "NurbsCurve::Split() succeeds");

  const ON_Interval left_domain = left.raw().Domain();
  const ON_Interval right_domain = right.raw().Domain();
  Check(std::abs(left_domain.Min() - 0.0) < 1e-9 && std::abs(left_domain.Max() - 0.4) < 1e-9,
        "the left half's domain is exactly [0, 0.4]");
  Check(std::abs(right_domain.Min() - 0.4) < 1e-9 && std::abs(right_domain.Max() - 1.0) < 1e-9,
        "the right half's domain is exactly [0.4, 1]");

  const Point3d left_start = left.PointAt(left_domain.Min());
  const Point3d left_end = left.PointAt(left_domain.Max());
  const Point3d right_start = right.PointAt(right_domain.Min());
  const Point3d right_end = right.PointAt(right_domain.Max());
  Check(std::abs(left_start.x) < 1e-9 && std::abs(left_end.x - 4.0) < 1e-9,
        "the left half runs exactly from (0,0,0) to (4,0,0)");
  Check(std::abs(right_start.x - 4.0) < 1e-9 && std::abs(right_end.x - 10.0) < 1e-9,
        "the right half runs exactly from (4,0,0) to (10,0,0)");
  Check((left_end - right_start).Length() < 1e-9,
        "the two halves share the exact same split point, with no gap "
        "or overlap");
  Check(std::abs(left.Length() + right.Length() - line.Length()) < 1e-9,
        "the two halves' lengths sum back to exactly the original "
        "line's own length");

  Check(line.Split(0.0, left, right) == Result::Failed,
        "Split() fails when t is at the domain's own start rather than "
        "strictly inside it");
  Check(line.Split(1.0, left, right) == Result::Failed,
        "Split() fails when t is at the domain's own end rather than "
        "strictly inside it");
}

void TestCurveExtend() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;
  using dino8::kernel::Result;

  // Same line as TestCurveSplit(): (0,0,0) to (10,0,0), domain [0,1],
  // P(t) = (10t, 0, 0). Extending to [-0.5, 1.5] should analytically
  // extrapolate the same straight line rather than approximate it, so
  // the extended curve's own evaluated endpoints land exactly on the
  // line's own equation - confirmed by a debug run before finalizing
  // these assertions, not assumed.
  const std::vector<Point3d> pts = {Point3d(0, 0, 0), Point3d(10, 0, 0)};
  NurbsCurve line = NurbsCurve::FromControlPoints(pts, /*degree=*/1);
  Check(line.Extend(-0.5, 1.5) == Result::Ok, "NurbsCurve::Extend() succeeds");

  const ON_Interval domain_after = line.raw().Domain();
  Check(std::abs(domain_after.Min() - (-0.5)) < 1e-9 && std::abs(domain_after.Max() - 1.5) < 1e-9,
        "the extended curve's own domain is exactly [-0.5, 1.5]");
  const Point3d p_lo = line.PointAt(domain_after.Min());
  const Point3d p_hi = line.PointAt(domain_after.Max());
  Check(std::abs(p_lo.x - (-5.0)) < 1e-9 && std::abs(p_lo.y) < 1e-9 && std::abs(p_lo.z) < 1e-9,
        "the extended curve's new start point is exactly (-5,0,0), the "
        "same line P(t)=(10t,0,0) extrapolated to t=-0.5, not a "
        "different curve or a clamped-at-the-original-endpoint result");
  Check(std::abs(p_hi.x - 15.0) < 1e-9 && std::abs(p_hi.y) < 1e-9 && std::abs(p_hi.z) < 1e-9,
        "the extended curve's new end point is exactly (15,0,0), the "
        "same line extrapolated to t=1.5");

  // A request already contained within the current domain is a no-op,
  // not an error - the curve is not modified and OpenNURBS' own Extend
  // (which would otherwise indistinguishably return false for this case
  // and for a genuine failure) is never even called.
  NurbsCurve unchanged = NurbsCurve::FromControlPoints(pts, /*degree=*/1);
  Check(unchanged.Extend(0.2, 0.8) == Result::NoOpAlreadySatisfied,
        "Extend() to a sub-range already inside the current domain "
        "reports NoOpAlreadySatisfied rather than Ok or Failed");
  Check(std::abs(unchanged.raw().Domain().Min() - 0.0) < 1e-9 &&
            std::abs(unchanged.raw().Domain().Max() - 1.0) < 1e-9,
        "and leaves the curve's own domain genuinely untouched at [0,1]");

  Check(line.Extend(1.5, 0.5) == Result::Failed,
        "Extend() fails on a backwards interval (t0 >= t1) rather than "
        "silently doing something undefined");

  // ON_NurbsCurve::IsClosed() requires at least 4 control points
  // (confirmed by reading opennurbs_nurbscurve.cpp, then by testing: a
  // 3-point coincident-endpoint polyline reported IsClosed() false
  // regardless of the coincidence, since it fails that minimum-CV-count
  // check before ever looking at endpoint positions) - so this needs a
  // 4-point closed triangle path instead to genuinely exercise IsClosed().
  NurbsCurve loop = NurbsCurve::FromControlPoints(
      {Point3d(0, 0, 0), Point3d(1, 0, 0), Point3d(0, 1, 0), Point3d(0, 0, 0)}, /*degree=*/1);
  Check(loop.IsClosed(), "the 4-point coincident-endpoint triangle path is genuinely closed");
  Check(loop.Extend(-1.0, 2.0) == Result::Failed,
        "Extend() fails on a closed curve, matching ON_NurbsCurve::"
        "Extend()'s own documented restriction");
}

void TestCurveClosestPoint() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // Line from (0,0,0) to (10,0,0), domain [0,1], P(t)=(10t,0,0). Query
  // point (3,4,0)'s closest point on the line is exactly its
  // perpendicular projection (3,0,0) at t=0.3, distance 4 -
  // hand-derivable exact, confirmed by a debug run before finalizing
  // these assertions (the numeric search converged to within ~4e-8 of
  // the exact answer, well inside the 1e-6 tolerance used here).
  const std::vector<Point3d> pts = {Point3d(0, 0, 0), Point3d(10, 0, 0)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(pts, /*degree=*/1);
  const double t = line.ClosestPointParameter(Point3d(3, 4, 0));
  Check(std::abs(t - 0.3) < 1e-6, "ClosestPointParameter finds t=0.3 for query point (3,4,0)");
  const Point3d p = line.ClosestPoint(Point3d(3, 4, 0));
  Check(std::abs(p.x - 3.0) < 1e-6 && std::abs(p.y) < 1e-6 && std::abs(p.z) < 1e-6,
        "ClosestPoint returns exactly (3,0,0), the perpendicular "
        "projection of (3,4,0) onto the line");
  Check(std::abs((p - Point3d(3, 4, 0)).Length() - 4.0) < 1e-6,
        "the distance from the query point to its closest point is "
        "exactly 4, matching the hand-derivable perpendicular distance");

  // A query point already sitting exactly on the curve should return
  // itself (distance 0), the degenerate case of the same search.
  const Point3d on_curve = line.ClosestPoint(Point3d(7, 0, 0));
  Check((on_curve - Point3d(7, 0, 0)).Length() < 1e-6,
        "a query point already on the curve is returned as its own "
        "closest point");
}

void TestCurveCurvature() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // A NURBS circle of known center and radius via ON_Circle::GetNurbForm
  // (the same real, non-approximate construction TestSurfaceIsClosed's
  // cylinder test already relies on): its curvature is hand-derivable
  // exactly - magnitude 1/radius everywhere, always pointing toward the
  // known center. Confirmed by a debug run before finalizing these
  // assertions.
  const Point3d center(2, 3, 0);
  const double radius = 5.0;
  const ON_Circle on_circle(ON_Plane(center, ON_3dVector(0, 0, 1)), radius);
  ON_NurbsCurve nurbs_form;
  Check(on_circle.GetNurbForm(nurbs_form) != 0, "ON_Circle::GetNurbForm succeeds");
  NurbsCurve circle;
  circle.raw() = nurbs_form;

  const ON_Interval domain = circle.raw().Domain();
  bool all_kappa_exact = true;
  bool all_centers_match = true;
  for (double frac : {0.0, 0.25, 0.5, 0.75}) {
    const double t = domain.ParameterAt(frac);
    const Point3d point = circle.PointAt(t);
    const Vector3d k = circle.CurvatureAt(t);
    if (std::abs(k.Length() - 1.0 / radius) > 1e-9) {
      all_kappa_exact = false;
    }
    // Standard way to recover the osculating circle's center from a
    // nonzero curvature vector: offset the point by R = 1/kappa along
    // the curvature direction, i.e. by k / |k|^2.
    const Point3d recovered_center = point + k / k.LengthSquared();
    if ((recovered_center - center).Length() > 1e-9) {
      all_centers_match = false;
    }
  }
  Check(all_kappa_exact,
        "the circle's curvature vector has magnitude exactly 1/radius "
        "(0.2) at every parameter tested");
  Check(all_centers_match,
        "the osculating circle's center, recovered from the curvature "
        "vector at each point, matches the known center (2,3,0) exactly "
        "at every parameter tested");

  // A straight line has zero curvature everywhere - no local center of
  // curvature to speak of.
  const NurbsCurve line =
      NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(10, 0, 0)}, /*degree=*/1);
  const Vector3d line_k = line.CurvatureAt(0.5);
  Check(line_k.Length() < 1e-9, "a straight line's curvature vector is exactly zero");
}

void TestCurveSuggestedSamples() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // A full circle of known radius is the one case where this method's
  // own "assume the whole curve turns at the tightest radius found"
  // approximation is exact, not just conservative - curvature really is
  // constant everywhere on a circle. That makes the expected sample
  // count independently computable from the same chord-height formula
  // (with the circle's own exact total turning angle, 2*pi, rather than
  // the method's Length()/radius approximation of it - which for a full
  // circle is itself exact, since Length() converges to the true
  // circumference 2*pi*radius) - confirmed to match exactly by a debug
  // run before finalizing this assertion.
  const double radius = 5.0;
  const ON_Circle on_circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), radius);
  ON_NurbsCurve nurbs_form;
  Check(on_circle.GetNurbForm(nurbs_form) != 0, "ON_Circle::GetNurbForm succeeds");
  NurbsCurve circle;
  circle.raw() = nurbs_form;

  const double chord_tolerance = 0.01;
  const int suggested = circle.SuggestedSamples(chord_tolerance);
  const double expected_angle_step = 2.0 * std::acos(1.0 - chord_tolerance / radius);
  const int expected = static_cast<int>(std::ceil((2.0 * ON_PI) / expected_angle_step));
  Check(suggested == expected,
        "SuggestedSamples for a full circle exactly matches the "
        "independently hand-computed chord-height formula");

  // A straight line has zero curvature everywhere, so one segment always
  // suffices regardless of the requested tolerance.
  const NurbsCurve line =
      NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(10, 0, 0)}, /*degree=*/1);
  Check(line.SuggestedSamples(chord_tolerance) == 1,
        "SuggestedSamples for a straight line is exactly 1");

  bool threw = false;
  try {
    circle.SuggestedSamples(-1.0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw, "SuggestedSamples throws std::invalid_argument on a non-positive chord_tolerance");
}

void TestCurveSuggestedParameterValues() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // A straight line needs no bisection at all: the midpoint of any
  // [t0, t1] sub-range lands exactly on the chord between its endpoints
  // (zero deviation), so the very first flatness check already passes -
  // exactly 2 values, the domain's own min and max. Confirmed by a
  // debug run before finalizing.
  const NurbsCurve line =
      NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(10, 0, 0)}, /*degree=*/1);
  const auto line_values = line.SuggestedParameterValues(0.01);
  Check(line_values.size() == 2 && line_values[0] == 0.0 && line_values[1] == 1.0,
        "SuggestedParameterValues for a straight line is exactly [0, 1] "
        "- no bisection needed at all");

  // A full circle has constant curvature everywhere, so the recursive
  // bisection lands on a genuinely uniform spacing (confirmed below,
  // not assumed) - and since it always bisects a segment exactly in
  // half rather than choosing an arbitrary split point, the final
  // segment count is always a power of 2: the smallest one at or above
  // SuggestedSamples()'s own independently-computed minimum-segments
  // threshold (50, from TestCurveSuggestedSamples), i.e. 2^ceil(log2(50))
  // = 64 - confirmed to match exactly by a debug run before finalizing,
  // not assumed from the formula alone.
  const double radius = 5.0;
  const ON_Circle on_circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), radius);
  ON_NurbsCurve nurbs_form;
  Check(on_circle.GetNurbForm(nurbs_form) != 0, "ON_Circle::GetNurbForm succeeds");
  NurbsCurve circle;
  circle.raw() = nurbs_form;
  const double chord_tolerance = 0.01;
  const auto circle_values = circle.SuggestedParameterValues(chord_tolerance);
  const int suggested_samples = circle.SuggestedSamples(chord_tolerance);
  const int expected_segments =
      static_cast<int>(std::pow(2.0, std::ceil(std::log2(static_cast<double>(suggested_samples)))));
  Check(static_cast<int>(circle_values.size()) - 1 == expected_segments,
        "the circle's own segment count is exactly the smallest power of "
        "2 at or above SuggestedSamples()'s independently-computed "
        "minimum threshold");

  bool all_deltas_equal = true;
  const double first_delta = circle_values[1] - circle_values[0];
  for (size_t i = 1; i < circle_values.size(); ++i) {
    if (std::abs((circle_values[i] - circle_values[i - 1]) - first_delta) > 1e-9) {
      all_deltas_equal = false;
      break;
    }
  }
  Check(all_deltas_equal,
        "the circle's own breakpoints are genuinely uniformly spaced, "
        "matching its constant curvature - real adaptivity naturally "
        "degenerates to uniform spacing when there's nothing to adapt to");

  bool threw = false;
  try {
    circle.SuggestedParameterValues(-1.0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw,
        "SuggestedParameterValues throws std::invalid_argument on a "
        "non-positive chord_tolerance");
}

void TestSurfaceNormalAt() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // A flat unit-square surface with P(u, v) = (u, v, 0) exactly (bilinear
  // identity for these control points, same construction
  // TestExactClippingHandlesNonConvexTrim already relies on): d/du =
  // (1,0,0), d/dv = (0,1,0), so the normal is exactly (1,0,0)x(0,1,0) =
  // (0,0,1) everywhere - a hand-derivable exact case, not approximate.
  const std::vector<Point3d> flat_grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface flat =
      NurbsSurface::FromControlGrid(flat_grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  for (const auto& uv : {std::pair(0.0, 0.0), std::pair(0.5, 0.5), std::pair(1.0, 0.0),
                          std::pair(0.25, 0.9)}) {
    const Vector3d normal = flat.NormalAt(uv.first, uv.second);
    Check(std::abs(normal.x) < 1e-9 && std::abs(normal.y) < 1e-9 &&
              std::abs(normal.z - 1.0) < 1e-9,
          "a flat P(u,v)=(u,v,0) surface's normal is exactly (0,0,1) at "
          "every (u,v) tested");
  }

  // A genuinely curved surface (z varies with both u and v): NormalAt()
  // should agree with a finite-difference cross product of the surface's
  // own partial derivatives - measured agreement, not just "returns a
  // unit vector."
  std::vector<Point3d> curved_grid;
  for (int u = 0; u < 4; ++u) {
    for (int v = 0; v < 4; ++v) {
      const double x = u;
      const double y = v;
      const double z = std::sin(0.7 * u) * std::cos(0.5 * v);
      curved_grid.emplace_back(x, y, z);
    }
  }
  const NurbsSurface curved =
      NurbsSurface::FromControlGrid(curved_grid, 4, 4, /*u_degree=*/3, /*v_degree=*/3);
  const ON_Interval u_domain = curved.raw().Domain(0);
  const ON_Interval v_domain = curved.raw().Domain(1);
  constexpr double kFiniteDifferenceStep = 1e-5;
  for (const auto& normalized_uv :
       {std::pair(0.3, 0.3), std::pair(0.6, 0.4), std::pair(0.5, 0.8)}) {
    const double u = u_domain.ParameterAt(normalized_uv.first);
    const double v = v_domain.ParameterAt(normalized_uv.second);
    const Vector3d normal = curved.NormalAt(u, v);
    Check(std::abs(normal.Length() - 1.0) < 1e-9, "NormalAt() returns a unit vector");

    Vector3d du = curved.PointAt(u + kFiniteDifferenceStep, v) -
                  curved.PointAt(u - kFiniteDifferenceStep, v);
    Vector3d dv = curved.PointAt(u, v + kFiniteDifferenceStep) -
                  curved.PointAt(u, v - kFiniteDifferenceStep);
    Vector3d finite_difference_normal = ON_CrossProduct(du, dv);
    finite_difference_normal.Unitize();
    const double alignment = std::abs(ON_DotProduct(normal, finite_difference_normal));
    Check(alignment > 1.0 - 1e-6,
          "NormalAt() agrees (up to sign) with a finite-difference cross "
          "product of the surface's own partial derivatives");
  }
}

void TestSurfaceDegreeElevation() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  std::vector<Point3d> grid;
  for (int u = 0; u < 4; ++u) {
    for (int v = 0; v < 4; ++v) {
      grid.emplace_back(u, v, 0);
    }
  }
  NurbsSurface surf =
      NurbsSurface::FromControlGrid(grid, /*u_count=*/4, /*v_count=*/4,
                                     /*u_degree=*/3, /*v_degree=*/3);
  Check(surf.DegreeU() == 3 && surf.DegreeV() == 3,
        "surface constructed at requested degree");

  const auto result = surf.ElevateDegree(/*direction=*/0, /*new_degree=*/4);
  Check(result == dino8::kernel::Result::Ok, "surface U-degree elevation succeeded");
  Check(surf.DegreeU() == 4, "surface U degree increased to 4");
}

void TestSurfaceIsClosed() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  // A flat bilinear surface is open in both directions - no wraparound
  // at all.
  std::vector<Point3d> grid;
  for (int u = 0; u < 2; ++u) {
    for (int v = 0; v < 2; ++v) {
      grid.emplace_back(u, v, 0);
    }
  }
  const NurbsSurface flat = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  Check(!flat.IsClosed(0) && !flat.IsClosed(1),
        "a flat bilinear surface is open in both U and V");
  Check(!flat.IsPeriodic(0) && !flat.IsPeriodic(1),
        "...and not periodic in either direction either");

  // A real cylinder wall via ON_Cylinder::GetNurbForm: closed in U (the
  // circular direction wraps back onto itself), open in V (height).
  // Confirmed by testing, not assumed: this closed-in-U surface is
  // *clamped*, not periodic (IsPeriodic(0) is false) - exactly the
  // "closed without being periodic" distinction this wrapper's own doc
  // comment describes, not a hypothetical.
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  Check(cylinder.GetNurbForm(cylinder_surface) != 0,
        "ON_Cylinder::GetNurbForm succeeds building the wall surface");
  NurbsSurface wall;
  wall.raw() = cylinder_surface;
  Check(wall.IsClosed(0) && !wall.IsClosed(1),
        "a cylinder wall surface is closed in U (wraps around the "
        "circle) and open in V (the height direction has two distinct "
        "ends)");
  Check(!wall.IsPeriodic(0),
        "the cylinder wall's U closure is via a clamped knot vector "
        "with coincident end curves, not a genuinely periodic knot "
        "vector - IsClosed() and IsPeriodic() really do answer different "
        "questions, not just two names for the same thing");
}

void TestSurfaceIsPlanar() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  const std::vector<Point3d> flat_grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface flat = NurbsSurface::FromControlGrid(flat_grid, 2, 2, 1, 1);
  Check(flat.IsPlanar(), "a genuinely flat surface reports planar at the default tolerance");

  // Same doubly-curved bicubic bulge surface as
  // TestBrepGetTightBoundingBoxOvershootsInteriorExtremum: a 3x3 control
  // grid, all z=0 except the center control point at z=peak_height.
  // NOT planar-at-tolerance-peak_height as a naive guess might assume:
  // IsPlanar() fits its plane through the surface's own *evaluated*
  // point at the domain center (z=0.25*peak_height, confirmed
  // separately via PointAt() in that other test), not through z=0, so
  // the real threshold - confirmed empirically via a debug run before
  // finalizing these assertions, not assumed - is each control point's
  // distance to *that* plane: 0.75*peak_height for the peak control
  // point (5 - 1.25 = 3.75 here), the larger of the two distances
  // actually checked.
  const double peak_height = 5.0;
  std::vector<Point3d> bulge_grid;
  for (int u = 0; u < 3; ++u) {
    for (int v = 0; v < 3; ++v) {
      bulge_grid.emplace_back(u, v, (u == 1 && v == 1) ? peak_height : 0.0);
    }
  }
  const NurbsSurface bulge = NurbsSurface::FromControlGrid(bulge_grid, 3, 3, 2, 2);
  Check(!bulge.IsPlanar(1e-6), "the bulge surface is not planar at a tight tolerance");
  Check(!bulge.IsPlanar(0.75 * peak_height - 0.01),
        "the bulge surface is still not planar just below the real "
        "threshold (0.75*peak_height), not the naively-guessed "
        "peak_height");
  Check(bulge.IsPlanar(0.75 * peak_height + 0.01),
        "the bulge surface is planar just above that real threshold");
}

void TestSurfaceIsSphere() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  // A genuine sphere via ON_Sphere::GetNurbForm (the same real
  // construction Brep::Sphere() uses) - confirmed by a debug run before
  // finalizing these assertions.
  const double radius = 3.0;
  const ON_Sphere on_sphere(ON_3dPoint(1, -2, 0.5), radius);
  ON_NurbsSurface sphere_surface;
  Check(on_sphere.GetNurbForm(sphere_surface) != 0, "ON_Sphere::GetNurbForm succeeds");
  NurbsSurface sphere;
  sphere.raw() = sphere_surface;
  Check(sphere.IsSphere(), "a genuine sphere surface reports IsSphere() true");

  const std::vector<Point3d> flat_grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface flat = NurbsSurface::FromControlGrid(flat_grid, 2, 2, 1, 1);
  Check(!flat.IsSphere(), "a flat surface reports IsSphere() false");

  // A cylinder wall is curved in one direction but flat in the other -
  // a real, non-spherical shape this classification must correctly
  // reject, not just "anything curved reports true".
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  Check(cylinder.GetNurbForm(cylinder_surface) != 0, "ON_Cylinder::GetNurbForm succeeds");
  NurbsSurface wall;
  wall.raw() = cylinder_surface;
  Check(!wall.IsSphere(), "a cylinder wall (curved in only one direction) reports IsSphere() false");
}

void TestSurfaceIsCylinder() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  // Same cylinder wall as TestSurfaceIsSphere()'s own negative case -
  // now the positive case here, and vice versa for the sphere below:
  // each method's test is the other's negative, together showing this
  // is a real distinguishing classification. Confirmed by a debug run
  // before finalizing these assertions.
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  Check(cylinder.GetNurbForm(cylinder_surface) != 0, "ON_Cylinder::GetNurbForm succeeds");
  NurbsSurface wall;
  wall.raw() = cylinder_surface;
  Check(wall.IsCylinder(), "a genuine cylinder wall surface reports IsCylinder() true");

  const double radius = 3.0;
  const ON_Sphere on_sphere(ON_3dPoint(1, -2, 0.5), radius);
  ON_NurbsSurface sphere_surface;
  Check(on_sphere.GetNurbForm(sphere_surface) != 0, "ON_Sphere::GetNurbForm succeeds");
  NurbsSurface sphere;
  sphere.raw() = sphere_surface;
  Check(!sphere.IsCylinder(), "a sphere reports IsCylinder() false");

  const std::vector<Point3d> flat_grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface flat = NurbsSurface::FromControlGrid(flat_grid, 2, 2, 1, 1);
  Check(!flat.IsCylinder(), "a flat surface reports IsCylinder() false");
}

void TestSurfaceIsCone() {
  using dino8::kernel::NurbsSurface;

  // A genuine right circular cone via ON_Cone::GetNurbForm - confirmed
  // by a debug run before finalizing these assertions.
  const ON_Cone on_cone(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), /*height=*/2.0,
                        /*radius=*/1.0);
  ON_NurbsSurface cone_surface;
  Check(on_cone.GetNurbForm(cone_surface) != 0, "ON_Cone::GetNurbForm succeeds");
  NurbsSurface cone;
  cone.raw() = cone_surface;
  Check(cone.IsCone(), "a genuine cone surface reports IsCone() true");

  // A cylinder's line isocurves are parallel, never converging to an
  // apex the way a cone's do - the real distinguishing case between the
  // two structurally-similar checks.
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  Check(cylinder.GetNurbForm(cylinder_surface) != 0, "ON_Cylinder::GetNurbForm succeeds");
  NurbsSurface wall;
  wall.raw() = cylinder_surface;
  Check(!wall.IsCone(), "a cylinder wall (parallel, not converging, line isocurves) reports IsCone() false");

  const ON_Sphere on_sphere(ON_3dPoint(1, -2, 0.5), 3.0);
  ON_NurbsSurface sphere_surface;
  Check(on_sphere.GetNurbForm(sphere_surface) != 0, "ON_Sphere::GetNurbForm succeeds");
  NurbsSurface sphere;
  sphere.raw() = sphere_surface;
  Check(!sphere.IsCone(), "a sphere (no straight-line isocurve at all) reports IsCone() false");
}

void TestSurfaceIsTorus() {
  using dino8::kernel::NurbsSurface;

  // A genuine torus via ON_Torus::GetNurbForm. A real discovery here,
  // confirmed by a debug run rather than assumed: at the *default*
  // tolerance (ON_ZERO_TOLERANCE, ~2.3e-10) this reports false - the
  // rational biquadratic NURBS form's own floating-point round-off from
  // GetNurbForm's construction is just outside that extremely tight
  // bound for ON_Curve::IsArc's internal fit-check, unlike the sphere/
  // cylinder/cone cases above which all passed at the default tolerance.
  // A still-tight but slightly looser 1e-6 tolerance reports true, which
  // is the tolerance used below - not a workaround for a wrong
  // implementation, just the real precision this construction needs.
  const ON_Torus on_torus(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), /*major_radius=*/5.0,
                          /*minor_radius=*/1.0);
  ON_NurbsSurface torus_surface;
  Check(on_torus.GetNurbForm(torus_surface) != 0, "ON_Torus::GetNurbForm succeeds");
  NurbsSurface torus;
  torus.raw() = torus_surface;
  Check(!torus.IsTorus(),
        "a genuine torus surface reports IsTorus() false at the "
        "default (extremely tight) tolerance, due to GetNurbForm's own "
        "floating-point round-off - not assumed, discovered by testing");
  Check(torus.IsTorus(1e-6),
        "...but reports true at a still-tight 1e-6 tolerance, which "
        "comfortably covers that real round-off");

  const ON_Sphere on_sphere(ON_3dPoint(1, -2, 0.5), 3.0);
  ON_NurbsSurface sphere_surface;
  Check(on_sphere.GetNurbForm(sphere_surface) != 0, "ON_Sphere::GetNurbForm succeeds");
  NurbsSurface sphere;
  sphere.raw() = sphere_surface;
  Check(!sphere.IsTorus(1e-6), "a sphere reports IsTorus() false");

  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  Check(cylinder.GetNurbForm(cylinder_surface) != 0, "ON_Cylinder::GetNurbForm succeeds");
  NurbsSurface wall;
  wall.raw() = cylinder_surface;
  Check(!wall.IsTorus(1e-6), "a cylinder wall reports IsTorus() false");

  const ON_Cone on_cone(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 2.0, 1.0);
  ON_NurbsSurface cone_surface;
  Check(on_cone.GetNurbForm(cone_surface) != 0, "ON_Cone::GetNurbForm succeeds");
  NurbsSurface cone;
  cone.raw() = cone_surface;
  Check(!cone.IsTorus(1e-6), "a cone reports IsTorus() false");
}

void TestSurfaceGetApproximateSize() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  // Flat P(u,v)=(u,v,0) surface over domain [0,3]x[0,2] (control grid at
  // integer spacing) - no curvature at all, so the control-polygon
  // -length approximation is exact here: hand-derivable width=3,
  // height=2, confirmed by a debug run before finalizing.
  std::vector<Point3d> grid;
  for (int u = 0; u <= 3; ++u) {
    for (int v = 0; v <= 2; ++v) {
      grid.emplace_back(u, v, 0);
    }
  }
  const NurbsSurface flat = NurbsSurface::FromControlGrid(grid, 4, 3, 1, 1);
  const auto flat_size = flat.GetApproximateSize();
  Check(std::abs(flat_size.width - 3.0) < 1e-9 && std::abs(flat_size.height - 2.0) < 1e-9,
        "a flat surface's approximate size is exactly its true size "
        "(3 x 2), since there's no curvature for the control-polygon "
        "approximation to overstate");

  // Cylinder wall, radius 1: U wraps the unit circle (true circumference
  // 2*pi ~ 6.283), V is the straight height (1.0, exact - a line has no
  // curvature either). Confirmed by the same debug run: this overstates
  // the true circumference substantially (8.0, not merely a rounding
  // difference from 6.283), the real, non-negligible gap this
  // control-polygon approximation has for a genuinely curved direction.
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  Check(cylinder.GetNurbForm(cylinder_surface) != 0, "ON_Cylinder::GetNurbForm succeeds");
  NurbsSurface wall;
  wall.raw() = cylinder_surface;
  const auto wall_size = wall.GetApproximateSize();
  Check(wall_size.width > 2.0 * ON_PI,
        "the cylinder wall's approximate width overstates the true "
        "circumference (2*pi), matching the control-polygon "
        "approximation's own documented direction of error");
  Check(std::abs(wall_size.height - 1.0) < 1e-9,
        "the cylinder wall's approximate height is exactly 1.0, the "
        "true straight-line height");
}

void TestSurfaceTessellateGridNonUniform() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface surface = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);

  // Equivalence check: evenly-spaced u_values/v_values should reproduce
  // TessellateGrid()'s own output exactly - confirmed by a debug run
  // before finalizing (byte-for-byte matching vertex/face counts and
  // area).
  const std::vector<double> even_u = {0.0, 0.25, 0.5, 0.75, 1.0};
  const std::vector<double> even_v = {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0};
  const auto uniform_via_new = surface.TessellateGridNonUniform(even_u, even_v);
  const auto uniform_via_old = surface.TessellateGrid(4, 3);
  Check(uniform_via_new.VertexCount() == uniform_via_old.VertexCount() &&
            uniform_via_new.FaceCount() == uniform_via_old.FaceCount(),
        "TessellateGridNonUniform with evenly-spaced values matches "
        "TessellateGrid's own vertex/face counts exactly");
  Check(std::abs(uniform_via_new.Area() - uniform_via_old.Area()) < 1e-9,
        "...and matches its area exactly too");

  // Genuinely non-uniform values on the same flat surface: area should
  // still be (up to float32 vertex precision) exactly 1.0 - the
  // identity-mapped unit square's true area never depends on where the
  // grid lines fall, only on the domain's own outer extent.
  const std::vector<double> non_uniform_u = {0.0, 0.05, 0.1, 0.5, 0.9, 0.95, 1.0};
  const std::vector<double> non_uniform_v = {0.0, 0.5, 1.0};
  const auto non_uniform_mesh = surface.TessellateGridNonUniform(non_uniform_u, non_uniform_v);
  Check(std::abs(non_uniform_mesh.Area() - 1.0) < 1e-6,
        "a genuinely non-uniform grid on the flat unit-square surface "
        "still measures the exact true area (1.0), regardless of where "
        "the (uneven) grid lines fall");
  Check(non_uniform_mesh.VertexCount() == 21 && non_uniform_mesh.FaceCount() == 24,
        "the non-uniform mesh's own vertex/face counts exactly match "
        "its 7x3 grid of parameter values (21 vertices, "
        "6x2 cells x 2 triangles = 24 faces)");

  bool threw_too_few = false;
  try {
    surface.TessellateGridNonUniform({0.0}, {0.0, 1.0});
  } catch (const std::invalid_argument&) {
    threw_too_few = true;
  }
  Check(threw_too_few,
        "TessellateGridNonUniform throws std::invalid_argument when "
        "u_values has fewer than 2 entries");

  bool threw_not_increasing = false;
  try {
    surface.TessellateGridNonUniform({0.0, 0.5, 0.3, 1.0}, {0.0, 1.0});
  } catch (const std::invalid_argument&) {
    threw_not_increasing = true;
  }
  Check(threw_not_increasing,
        "TessellateGridNonUniform throws std::invalid_argument when "
        "u_values isn't strictly increasing");
}

void TestSurfaceSuggestedParameterValuesAndTessellateGridNonUniformAdaptive() {
  using dino8::kernel::NurbsSurface;

  // Same cylinder wall used throughout this file: U is the circular
  // direction (real curvature everywhere), V is the straight height
  // (zero curvature). Confirmed by a debug run before finalizing:
  // direction 0 needs real (non-trivial) bisection - its own segment
  // count (32) is, as with the earlier circle-curve test, the smallest
  // power of 2 at or above the independently-computed SuggestedDivisions
  // count (23) - while direction 1 needs none at all, landing on exactly
  // its domain's own 2 endpoints [0, 1], matching SuggestedDivisions.v's
  // own value of 1.
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  Check(cylinder.GetNurbForm(cylinder_surface) != 0, "ON_Cylinder::GetNurbForm succeeds");
  NurbsSurface wall;
  wall.raw() = cylinder_surface;

  const double chord_tolerance = 0.01;
  const auto u_values = wall.SuggestedParameterValues(0, chord_tolerance);
  const auto v_values = wall.SuggestedParameterValues(1, chord_tolerance);
  const auto divisions = wall.SuggestedDivisions(chord_tolerance);
  const int expected_u_segments = static_cast<int>(
      std::pow(2.0, std::ceil(std::log2(static_cast<double>(divisions.u)))));
  Check(static_cast<int>(u_values.size()) - 1 == expected_u_segments,
        "direction 0's own segment count matches the smallest power of "
        "2 at or above SuggestedDivisions()'s independently-computed "
        "minimum threshold, the same relationship the curve-level test "
        "already established");
  Check(v_values.size() == 2 && v_values.front() == 0.0 && v_values.back() == 1.0,
        "direction 1 (zero curvature) needs no bisection at all - "
        "exactly its domain's own [0, 1] endpoints");

  // Wiring check: TessellateGridNonUniformAdaptive() must produce
  // exactly the same mesh as calling SuggestedParameterValues() for
  // both directions and TessellateGridNonUniform() by hand.
  const auto adaptive_mesh = wall.TessellateGridNonUniformAdaptive(chord_tolerance);
  const auto manual_mesh = wall.TessellateGridNonUniform(u_values, v_values);
  Check(adaptive_mesh.VertexCount() == manual_mesh.VertexCount() &&
            adaptive_mesh.FaceCount() == manual_mesh.FaceCount(),
        "TessellateGridNonUniformAdaptive produces the exact same mesh "
        "as calling SuggestedParameterValues() (both directions) then "
        "TessellateGridNonUniform() by hand");

  bool threw = false;
  try {
    wall.SuggestedParameterValues(0, -1.0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw,
        "SuggestedParameterValues throws std::invalid_argument on a "
        "non-positive chord_tolerance");
}

void TestSurfaceReverseAndTranspose() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // Same flat P(u,v)=(u,v,0) surface TestSurfaceNormalAt() already
  // established has normal exactly (0,0,1) everywhere.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };

  // Reverse(0) (U) flips the outward normal exactly, since u_dir x v_dir
  // negates when u_dir reverses direction - confirmed here rather than
  // just asserted from the cross-product algebra.
  NurbsSurface reversed = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  Check(reversed.Reverse(0) == dino8::kernel::Result::Ok, "NurbsSurface::Reverse(0) succeeds");
  // Same domain-not-preserved caveat NurbsCurve::Reverse() has - a [0,1]
  // domain came back as [-1,0] here too - so re-fetch fresh rather than
  // reusing a captured one.
  const ON_Interval u_after = reversed.raw().Domain(0);
  const ON_Interval v_after = reversed.raw().Domain(1);
  const Vector3d normal_after_reverse =
      reversed.NormalAt(u_after.ParameterAt(0.5), v_after.ParameterAt(0.5));
  Check(std::abs(normal_after_reverse.x) < 1e-9 && std::abs(normal_after_reverse.y) < 1e-9 &&
            std::abs(normal_after_reverse.z - (-1.0)) < 1e-9,
        "Reverse(0) flips the flat surface's normal from (0,0,1) to "
        "exactly (0,0,-1)");

  // Transpose() swaps U and V entirely, which has the same normal-
  // flipping effect (v_dir x u_dir = -(u_dir x v_dir)) as Reverse() -
  // independently confirmed, not assumed to behave the same way just
  // because both involve "reversing something".
  NurbsSurface transposed = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  transposed.Transpose();
  const ON_Interval tu = transposed.raw().Domain(0);
  const ON_Interval tv = transposed.raw().Domain(1);
  const Vector3d normal_after_transpose =
      transposed.NormalAt(tu.ParameterAt(0.5), tv.ParameterAt(0.5));
  Check(std::abs(normal_after_transpose.x) < 1e-9 && std::abs(normal_after_transpose.y) < 1e-9 &&
            std::abs(normal_after_transpose.z - (-1.0)) < 1e-9,
        "Transpose() also flips the flat surface's normal to exactly "
        "(0,0,-1)");
}

void TestSurfaceTrim() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;
  using dino8::kernel::Result;

  // Same flat P(u,v)=(u,v,0) surface as TestSurfaceReverseAndTranspose().
  // Trimming only the U direction to [0.2, 0.7] should leave V's domain
  // [0,1] untouched and, since the surface is an identity mapping, should
  // make the new U-domain's own endpoints land exactly at u=0.2 and
  // u=0.7 - confirmed by a debug run before writing these assertions,
  // not assumed.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  NurbsSurface surface = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  Check(surface.Trim(0, 0.2, 0.7) == Result::Ok, "NurbsSurface::Trim(0, ...) succeeds");

  const ON_Interval u_after = surface.raw().Domain(0);
  const ON_Interval v_after = surface.raw().Domain(1);
  Check(std::abs(u_after.Min() - 0.2) < 1e-9 && std::abs(u_after.Max() - 0.7) < 1e-9,
        "trimming direction 0 sets that direction's domain to exactly "
        "[0.2, 0.7]");
  Check(std::abs(v_after.Min() - 0.0) < 1e-9 && std::abs(v_after.Max() - 1.0) < 1e-9,
        "trimming direction 0 leaves direction 1's domain [0,1] unchanged");

  const Point3d p_lo = surface.PointAt(u_after.Min(), v_after.Min());
  const Point3d p_hi = surface.PointAt(u_after.Max(), v_after.Max());
  Check(std::abs(p_lo.x - 0.2) < 1e-9 && std::abs(p_lo.y) < 1e-9 && std::abs(p_lo.z) < 1e-9,
        "PointAt the trimmed domain's low corner is exactly (0.2, 0, 0)");
  Check(std::abs(p_hi.x - 0.7) < 1e-9 && std::abs(p_hi.y - 1.0) < 1e-9 && std::abs(p_hi.z) < 1e-9,
        "PointAt the trimmed domain's high corner is exactly (0.7, 1, 0)");

  bool failed = false;
  NurbsSurface backwards = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  if (backwards.Trim(0, 0.7, 0.2) == Result::Failed) {
    failed = true;
  }
  Check(failed,
        "Trim() fails on a backwards interval (t0 >= t1) rather than "
        "silently doing something undefined");
}

void TestSurfaceSplit() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;
  using dino8::kernel::Result;

  // Same flat P(u,v)=(u,v,0) surface as TestSurfaceTrim(). Splitting
  // direction 0 (U) at t=0.4 should give a west half covering u in
  // [0, 0.4] and an east half covering [0.4, 1], both sharing v's domain
  // [0,1] unchanged, and the two halves should meet exactly at u=0.4 -
  // confirmed by a debug run before finalizing these assertions.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  NurbsSurface surface = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  NurbsSurface west, east;
  Check(surface.Split(0, 0.4, west, east) == Result::Ok, "NurbsSurface::Split(0, ...) succeeds");

  const ON_Interval wu = west.raw().Domain(0);
  const ON_Interval wv = west.raw().Domain(1);
  const ON_Interval eu = east.raw().Domain(0);
  const ON_Interval ev = east.raw().Domain(1);
  Check(std::abs(wu.Min() - 0.0) < 1e-9 && std::abs(wu.Max() - 0.4) < 1e-9,
        "the west half's own domain(0) is exactly [0, 0.4]");
  Check(std::abs(eu.Min() - 0.4) < 1e-9 && std::abs(eu.Max() - 1.0) < 1e-9,
        "the east half's own domain(0) is exactly [0.4, 1]");
  Check(std::abs(wv.Min()) < 1e-9 && std::abs(wv.Max() - 1.0) < 1e-9 &&
            std::abs(ev.Min()) < 1e-9 && std::abs(ev.Max() - 1.0) < 1e-9,
        "both halves keep direction 1's domain [0,1] unchanged");

  const Point3d w_hi = west.PointAt(wu.Max(), wv.Min());
  const Point3d e_lo = east.PointAt(eu.Min(), ev.Min());
  Check(std::abs(w_hi.x - 0.4) < 1e-9 && std::abs(w_hi.y) < 1e-9 && std::abs(w_hi.z) < 1e-9,
        "the west half's own u_max edge lands exactly at (0.4, 0, 0)");
  Check(std::abs(e_lo.x - 0.4) < 1e-9 && std::abs(e_lo.y) < 1e-9 && std::abs(e_lo.z) < 1e-9,
        "the east half's own u_min edge lands at the same exact point "
        "(0.4, 0, 0), so the two halves share the split line with no "
        "gap or overlap");

  bool failed = false;
  NurbsSurface endpoint_source = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  NurbsSurface endpoint_west, endpoint_east;
  if (endpoint_source.Split(0, 0.0, endpoint_west, endpoint_east) == Result::Failed) {
    failed = true;
  }
  Check(failed,
        "Split() fails when t sits exactly at a domain endpoint rather "
        "than strictly inside it");
}

void TestSurfaceExtend() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;
  using dino8::kernel::Result;

  // Same flat P(u,v)=(u,v,0) surface as TestSurfaceSplit(). Extending
  // direction 0 to [-0.5, 1.0] should analytically extrapolate the
  // identity mapping rather than approximate it, leaving direction 1's
  // domain untouched - confirmed by a debug run before finalizing these
  // assertions.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  NurbsSurface surface = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  Check(surface.Extend(0, -0.5, 1.0) == Result::Ok, "NurbsSurface::Extend(0, ...) succeeds");

  const ON_Interval u_after = surface.raw().Domain(0);
  const ON_Interval v_after = surface.raw().Domain(1);
  Check(std::abs(u_after.Min() - (-0.5)) < 1e-9 && std::abs(u_after.Max() - 1.0) < 1e-9,
        "extending direction 0 sets that direction's domain to exactly "
        "[-0.5, 1.0]");
  Check(std::abs(v_after.Min()) < 1e-9 && std::abs(v_after.Max() - 1.0) < 1e-9,
        "extending direction 0 leaves direction 1's domain [0,1] unchanged");

  const Point3d p = surface.PointAt(u_after.Min(), 0.5);
  Check(std::abs(p.x - (-0.5)) < 1e-9 && std::abs(p.y - 0.5) < 1e-9 && std::abs(p.z) < 1e-9,
        "PointAt the extended domain's new u_min edge lands exactly on "
        "(-0.5, 0.5, 0), the same identity mapping extrapolated, not a "
        "different surface");

  NurbsSurface unchanged = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  Check(unchanged.Extend(0, 0.2, 0.8) == Result::NoOpAlreadySatisfied,
        "Extend() to a sub-range already inside the current domain "
        "reports NoOpAlreadySatisfied rather than Ok or Failed");

  NurbsSurface backwards = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  Check(backwards.Extend(0, 1.0, -0.5) == Result::Failed,
        "Extend() fails on a backwards interval (t0 >= t1) rather than "
        "silently doing something undefined");
}

void TestSurfaceClosestPoint() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  // Flat P(u,v)=(u,v,0) surface, domain [0,1]x[0,1]. Query point
  // (0.37, 0.62, 5)'s closest point on the plane is exactly its vertical
  // projection (0.37, 0.62, 0), distance 5 - hand-derivable exact,
  // confirmed by a debug run before finalizing (converged to within
  // ~4e-5 of the exact answer, well inside the 1e-3 tolerance used here
  // for the coarser 2D grid search).
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface surface = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  const Point3d p = surface.ClosestPoint(Point3d(0.37, 0.62, 5));
  Check(std::abs(p.x - 0.37) < 1e-3 && std::abs(p.y - 0.62) < 1e-3 && std::abs(p.z) < 1e-3,
        "ClosestPoint returns approximately (0.37, 0.62, 0), the "
        "vertical projection of the query point onto the plane");
  Check(std::abs((p - Point3d(0.37, 0.62, 5)).Length() - 5.0) < 1e-3,
        "the distance from the query point to its closest point is "
        "approximately 5, matching the hand-derivable vertical distance");

  // A query point outside the domain entirely (both u and v beyond
  // [0,1]) has its closest point clamp to the surface's own boundary
  // corner (1,1,0), not extrapolate past the domain - confirmed by the
  // same debug run, not assumed.
  const Point3d p2 = surface.ClosestPoint(Point3d(5, 5, 0));
  Check(std::abs(p2.x - 1.0) < 1e-6 && std::abs(p2.y - 1.0) < 1e-6 && std::abs(p2.z) < 1e-6,
        "a query point far outside the domain clamps to exactly the "
        "surface's own boundary corner (1,1,0), not an extrapolation "
        "past its domain");
}

void TestSurfaceCurvature() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  // Flat plane: zero curvature everywhere - hand-derivable exact.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface plane = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  const auto plane_k = plane.CurvatureAt(0.5, 0.5);
  Check(std::abs(plane_k.gaussian) < 1e-9 && std::abs(plane_k.mean) < 1e-9 &&
            std::abs(plane_k.k1) < 1e-9 && std::abs(plane_k.k2) < 1e-9,
        "a flat plane's curvature (gaussian, mean, k1, k2) is exactly "
        "zero everywhere");

  // Sphere of known radius, via ON_Sphere::GetNurbForm (the same real
  // construction Brep::Sphere() uses). Every point on a sphere is an
  // umbilic (k1 == k2), so this is hand-derivable exact: Gaussian
  // curvature is exactly 1/radius^2 (sign-unambiguous - a product of two
  // curvatures with the same sign convention, so signs cancel), and mean
  // curvature and both principal curvatures are exactly -1/radius given
  // this surface's outward-pointing normal - confirmed by a debug run
  // before finalizing these assertions, not assumed from the formula's
  // sign in the abstract.
  const double radius = 3.0;
  const ON_Sphere on_sphere(ON_3dPoint(0, 0, 0), radius);
  ON_NurbsSurface nurbs_form;
  Check(on_sphere.GetNurbForm(nurbs_form) != 0, "ON_Sphere::GetNurbForm succeeds");
  NurbsSurface sphere;
  sphere.raw() = nurbs_form;
  const ON_Interval u_domain = sphere.raw().Domain(0);
  const ON_Interval v_domain = sphere.raw().Domain(1);

  bool all_gaussian_exact = true;
  bool all_mean_exact = true;
  bool all_umbilic = true;
  for (double u_frac : {0.25, 0.5, 0.75}) {
    for (double v_frac : {0.25, 0.5, 0.75}) {
      const double u = u_domain.ParameterAt(u_frac);
      const double v = v_domain.ParameterAt(v_frac);
      const auto k = sphere.CurvatureAt(u, v);
      if (std::abs(k.gaussian - 1.0 / (radius * radius)) > 1e-6) {
        all_gaussian_exact = false;
      }
      if (std::abs(k.mean - (-1.0 / radius)) > 1e-6) {
        all_mean_exact = false;
      }
      if (std::abs(k.k1 - k.k2) > 1e-5 || std::abs(k.k1 - (-1.0 / radius)) > 1e-5) {
        all_umbilic = false;
      }
    }
  }
  Check(all_gaussian_exact,
        "a sphere's Gaussian curvature is exactly 1/radius^2 at every "
        "point tested, sign-unambiguous regardless of normal direction");
  Check(all_mean_exact,
        "a sphere's mean curvature is exactly -1/radius at every point "
        "tested, given this surface's outward-pointing normal");
  Check(all_umbilic,
        "every tested point on the sphere is an umbilic (k1 == k2 == "
        "-1/radius), matching the fact that every point on a sphere has "
        "the same curvature in every direction");
}

void TestSurfaceSuggestedDivisions() {
  using dino8::kernel::NurbsSurface;

  // Same cylinder wall as TestSurfaceIsClosed(): U is the circular
  // direction (radius 1), V is the straight height direction. Every
  // U-isocurve is the exact same unit circle regardless of which V it's
  // sampled at (so this is hand-derivable exact, same as
  // TestCurveSuggestedSamples()'s full-circle case), and every
  // V-isocurve is a straight vertical line (zero curvature) - confirmed
  // by a debug run before finalizing these assertions.
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  Check(cylinder.GetNurbForm(cylinder_surface) != 0, "ON_Cylinder::GetNurbForm succeeds");
  NurbsSurface wall;
  wall.raw() = cylinder_surface;

  const double chord_tolerance = 0.01;
  const auto divisions = wall.SuggestedDivisions(chord_tolerance);
  const double expected_angle_step = 2.0 * std::acos(1.0 - chord_tolerance / 1.0);
  const int expected_u = static_cast<int>(std::ceil((2.0 * ON_PI) / expected_angle_step));
  Check(divisions.u == expected_u,
        "SuggestedDivisions' U count for the cylinder wall exactly "
        "matches the independently hand-computed chord-height formula "
        "for its unit-radius circular cross-section");
  Check(divisions.v == 1,
        "SuggestedDivisions' V count is exactly 1, since every V-isocurve "
        "is a straight vertical line with zero curvature");

  bool threw = false;
  try {
    wall.SuggestedDivisions(0.0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw,
        "SuggestedDivisions throws std::invalid_argument on a "
        "non-positive chord_tolerance");
}

void TestSurfaceTessellateGridAdaptive() {
  using dino8::kernel::NurbsSurface;

  // Same cylinder wall as TestSurfaceSuggestedDivisions(). This is a
  // thin, deterministic composition of two already-verified pieces
  // (SuggestedDivisions() then TessellateGrid()), so the test just
  // confirms it actually wires them together rather than using some
  // fixed default: the untrimmed TessellateGrid() path always emits
  // exactly u_divisions * v_divisions * 2 triangles, so if
  // TessellateGridAdaptive() truly used SuggestedDivisions()'s own
  // return values, calling both separately and comparing face counts
  // must agree exactly.
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  cylinder.GetNurbForm(cylinder_surface);
  NurbsSurface wall;
  wall.raw() = cylinder_surface;

  const double chord_tolerance = 0.01;
  const auto divisions = wall.SuggestedDivisions(chord_tolerance);
  const auto adaptive_mesh = wall.TessellateGridAdaptive(chord_tolerance);
  Check(adaptive_mesh.FaceCount() == divisions.u * divisions.v * 2,
        "TessellateGridAdaptive's own face count exactly matches "
        "u_divisions * v_divisions * 2 for the same SuggestedDivisions() "
        "result computed independently");

  const auto manual_mesh = wall.TessellateGrid(divisions.u, divisions.v);
  Check(adaptive_mesh.VertexCount() == manual_mesh.VertexCount() &&
            std::abs(adaptive_mesh.Area() - manual_mesh.Area()) < 1e-9,
        "TessellateGridAdaptive produces the exact same mesh as calling "
        "SuggestedDivisions() then TessellateGrid() by hand");
}

void TestSurfaceTessellateGridClippedExactAdaptive() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;

  // Same 10x10 flat surface and [0.15,0.85]^2 trim
  // TestExactClippingMatchesAreaButNotCellCounts uses - true trim area is
  // exactly (0.85-0.15)^2 * 100 = 49, independent of tessellation
  // resolution since exact clipping measures the real boundary rather
  // than approximating it with the grid. This is a thin, deterministic
  // composition of two already-verified pieces (SuggestedDivisions()
  // then TessellateGridClippedExact()), so the test confirms both that
  // it wires them together (matches the manual two-call equivalent
  // exactly) and that the result is still the true trim area regardless
  // of which divisions SuggestedDivisions() happens to pick for a flat
  // surface with zero curvature.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 10, 0),
      Point3d(10, 0, 0),
      Point3d(10, 10, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const std::vector<Point2d> trim_loop = {
      Point2d(0.15, 0.15),
      Point2d(0.85, 0.15),
      Point2d(0.85, 0.85),
      Point2d(0.15, 0.85),
  };

  const double chord_tolerance = 0.05;
  const auto adaptive_mesh = surface.TessellateGridClippedExactAdaptive(chord_tolerance, trim_loop);
  Check(std::abs(adaptive_mesh.Area() - 49.0) < 1e-9,
        "TessellateGridClippedExactAdaptive's own area is exactly 49, "
        "the true trim area, regardless of which divisions "
        "SuggestedDivisions() picked for this flat (zero-curvature) "
        "surface");

  const auto divisions = surface.SuggestedDivisions(chord_tolerance);
  const auto manual_mesh = surface.TessellateGridClippedExact(divisions.u, divisions.v, trim_loop);
  Check(adaptive_mesh.VertexCount() == manual_mesh.VertexCount() &&
            adaptive_mesh.FaceCount() == manual_mesh.FaceCount(),
        "TessellateGridClippedExactAdaptive produces the exact same mesh "
        "as calling SuggestedDivisions() then TessellateGridClippedExact() "
        "by hand");
}

void TestBrepTessellateAdaptive() {
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // Box(): every face is flat (zero curvature everywhere), so
  // SuggestedDivisions() should pick the minimum 1x1 division for every
  // face regardless of chord_tolerance - exactly 6 face meshes, 12
  // triangles total (2 per face), and the closed, welded volume exactly
  // 8.0 (a 2x2x2 box) - hand-derivable exact, confirmed by a debug run
  // before finalizing these assertions.
  const Brep box = Brep::Box(0, 0, 0, 2, 2, 2);
  const auto box_faces = box.TessellateAdaptive(0.01);
  int box_total_faces = 0;
  for (const auto& m : box_faces) {
    box_total_faces += m.FaceCount();
  }
  Check(box_faces.size() == 6, "TessellateAdaptive returns one mesh per Box() face (6)");
  Check(box_total_faces == 12,
        "each flat Box() face needs only the minimum 1x1 division "
        "(2 triangles) regardless of chord_tolerance, 12 triangles total");
  const Mesh box_closed = box.TessellateToClosedMeshAdaptive(0.01);
  Check(std::abs(box_closed.Volume() - 8.0) < 1e-9,
        "TessellateToClosedMeshAdaptive's own volume is exactly 8.0 for "
        "a 2x2x2 box");

  // Sphere(): real curvature everywhere, so a tighter chord_tolerance
  // must produce meaningfully more triangles and a volume meaningfully
  // closer to the true analytic value than a loose one - this is the
  // actual point of curvature-based adaptation, not just "it runs
  // without crashing". Confirmed by a debug run: loose (0.5) tolerance
  // gave 36 faces / volume ~70 (far from the true ~113.1), tight (0.01)
  // gave 1520 faces / volume ~111.9 (within ~1% of true) - a real,
  // substantial improvement, not a coincidence of rounding.
  const double radius = 3.0;
  const Brep sphere = Brep::Sphere(Point3d(0, 0, 0), radius);
  const Mesh loose_sphere = sphere.TessellateToClosedMeshAdaptive(0.5);
  const Mesh tight_sphere = sphere.TessellateToClosedMeshAdaptive(0.01);
  const double expected_volume = (4.0 / 3.0) * ON_PI * radius * radius * radius;
  Check(tight_sphere.FaceCount() > loose_sphere.FaceCount() * 10,
        "a tighter chord_tolerance produces substantially more triangles "
        "for a genuinely curved surface");
  Check(std::abs(tight_sphere.Volume() - expected_volume) <
            std::abs(loose_sphere.Volume() - expected_volume),
        "the tighter chord_tolerance's volume is meaningfully closer to "
        "the true analytic sphere volume than the loose one's");
  Check(std::abs(tight_sphere.Volume() - expected_volume) / expected_volume < 0.02,
        "the tight-tolerance sphere's volume is within 2% of the true "
        "analytic value (4/3 * pi * r^3)");
}

void TestBrepTessellateNonUniformAdaptive() {
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // Box(): every face flat, same exact result as the uniform adaptive
  // path - 6 face meshes, 12 triangles total, exact volume 8.0.
  // Confirmed by a debug run before finalizing.
  const Brep box = Brep::Box(0, 0, 0, 2, 2, 2);
  const auto box_faces = box.TessellateNonUniformAdaptive(0.01);
  int box_total_faces = 0;
  for (const auto& m : box_faces) {
    box_total_faces += m.FaceCount();
  }
  Check(box_faces.size() == 6, "TessellateNonUniformAdaptive returns one mesh per Box() face (6)");
  Check(box_total_faces == 12,
        "each flat Box() face still needs only the minimum division "
        "(12 triangles total) with the non-uniform path too");
  const Mesh box_closed = box.TessellateToClosedMeshNonUniformAdaptive(0.01);
  Check(std::abs(box_closed.Volume() - 8.0) < 1e-9,
        "TessellateToClosedMeshNonUniformAdaptive's own volume is "
        "exactly 8.0 for a 2x2x2 box");

  // Sphere(): real curvature, isotropic in every direction. A genuine,
  // worth-documenting nuance found by testing, not assumed: the
  // recursive-bisection path's power-of-2 segment counts are less
  // efficient than the uniform path's directly-computed count for this
  // *isotropic* case (4096 faces here vs. the uniform adaptive test's
  // own 1520 at the same tolerance) - non-uniform adaptivity pays off
  // when curvature genuinely varies across a face (the cylinder-wall
  // case), not when it's the same everywhere. Still hits the same real
  // accuracy target.
  const double radius = 3.0;
  const Brep sphere = Brep::Sphere(Point3d(0, 0, 0), radius);
  const Mesh tight_sphere = sphere.TessellateToClosedMeshNonUniformAdaptive(0.01);
  const double expected_volume = (4.0 / 3.0) * ON_PI * radius * radius * radius;
  Check(std::abs(tight_sphere.Volume() - expected_volume) / expected_volume < 0.02,
        "the non-uniform adaptive sphere's volume is within 2% of the "
        "true analytic value (4/3 * pi * r^3), the same accuracy target "
        "the uniform adaptive path hits");
}

void TestFileRoundTrip() {
  using dino8::kernel::Brep;
  using dino8::kernel::Model;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  std::vector<Point3d> grid;
  for (int u = 0; u < 3; ++u) {
    for (int v = 0; v < 3; ++v) {
      grid.emplace_back(u, v, (u == 1 && v == 1) ? 1.0 : 0.0);
    }
  }
  NurbsSurface surf = NurbsSurface::FromControlGrid(grid, 3, 3, 2, 2);
  Brep brep = Brep::FromSurface(surf);
  Check(brep.FaceCount() == 1, "brep has one face");

  Model model;
  model.AddBrep(brep);
  Check(model.ObjectCount() == 1, "model has one object before save");

  const std::string path = "dino8_kernel_roundtrip_test.3dm";
  const auto save_result = model.Save(path);
  Check(save_result == dino8::kernel::Result::Ok, ".3dm save succeeded");

  Model loaded;
  const auto load_result = Model::Load(path, loaded);
  Check(load_result == dino8::kernel::Result::Ok, ".3dm load succeeded");
  Check(loaded.ObjectCount() == 1, "round-tripped model has one object");

  std::remove(path.c_str());
}

// Builds a closed, consistently-oriented (CCW from outside) axis-aligned
// box mesh directly - not via NurbsSurface::TessellateGrid, since that
// only tessellates a single open surface, not a closed solid. Booleans
// need actual watertight input.
dino8::kernel::Mesh MakeBox(double x0, double y0, double z0, double x1,
                             double y1, double z1) {
  dino8::kernel::Mesh mesh;
  ON_Mesh& raw = mesh.raw();

  raw.m_V.Append(ON_3fPoint(x0, y0, z0));  // 0
  raw.m_V.Append(ON_3fPoint(x1, y0, z0));  // 1
  raw.m_V.Append(ON_3fPoint(x1, y1, z0));  // 2
  raw.m_V.Append(ON_3fPoint(x0, y1, z0));  // 3
  raw.m_V.Append(ON_3fPoint(x0, y0, z1));  // 4
  raw.m_V.Append(ON_3fPoint(x1, y0, z1));  // 5
  raw.m_V.Append(ON_3fPoint(x1, y1, z1));  // 6
  raw.m_V.Append(ON_3fPoint(x0, y1, z1));  // 7

  auto add_tri = [&raw](int a, int b, int c) {
    ON_MeshFace face;
    face.vi[0] = a;
    face.vi[1] = b;
    face.vi[2] = c;
    face.vi[3] = c;
    raw.m_F.Append(face);
  };

  add_tri(0, 3, 2);
  add_tri(0, 2, 1);  // bottom (-z)
  add_tri(4, 5, 6);
  add_tri(4, 6, 7);  // top (+z)
  add_tri(0, 1, 5);
  add_tri(0, 5, 4);  // front (-y)
  add_tri(3, 7, 6);
  add_tri(3, 6, 2);  // back (+y)
  add_tri(0, 4, 7);
  add_tri(0, 7, 3);  // left (-x)
  add_tri(1, 2, 6);
  add_tri(1, 6, 5);  // right (+x)

  return mesh;
}

// Same box as MakeBox(), but as 6 genuine quad faces rather than 12
// triangles - each quad below is the same pair of MakeBox() triangles
// merged along their shared diagonal (e.g. bottom's (0,3,2)+(0,2,1)
// becomes the quad 0,3,2,1), so it has the identical outward-normal
// winding, just needed for SubD tests: Catmull-Clark subdivision's
// vertex/face-count growth has a clean, hand-derivable formula on an
// all-quad control net (V_new = V+E+F, F_new = 4x once every face is a
// quad), which a triangulated box wouldn't give.
dino8::kernel::Mesh MakeQuadBoxMesh(double x0, double y0, double z0, double x1, double y1,
                                     double z1) {
  dino8::kernel::Mesh mesh;
  ON_Mesh& raw = mesh.raw();

  raw.m_V.Append(ON_3fPoint(x0, y0, z0));  // 0
  raw.m_V.Append(ON_3fPoint(x1, y0, z0));  // 1
  raw.m_V.Append(ON_3fPoint(x1, y1, z0));  // 2
  raw.m_V.Append(ON_3fPoint(x0, y1, z0));  // 3
  raw.m_V.Append(ON_3fPoint(x0, y0, z1));  // 4
  raw.m_V.Append(ON_3fPoint(x1, y0, z1));  // 5
  raw.m_V.Append(ON_3fPoint(x1, y1, z1));  // 6
  raw.m_V.Append(ON_3fPoint(x0, y1, z1));  // 7

  auto add_quad = [&raw](int a, int b, int c, int d) {
    ON_MeshFace face;
    face.vi[0] = a;
    face.vi[1] = b;
    face.vi[2] = c;
    face.vi[3] = d;
    raw.m_F.Append(face);
  };

  add_quad(0, 3, 2, 1);  // bottom (-z)
  add_quad(4, 5, 6, 7);  // top (+z)
  add_quad(0, 1, 5, 4);  // front (-y)
  add_quad(3, 7, 6, 2);  // back (+y)
  add_quad(0, 4, 7, 3);  // left (-x)
  add_quad(1, 2, 6, 5);  // right (+x)

  return mesh;
}

void TestModelAddMeshRoundTrips() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Model;
  using dino8::kernel::Result;

  // AddMesh() is the missing counterpart to AddCurve()/AddBrep(): every
  // closed-solid primitive/boolean result here is a Mesh, but until now
  // there was no way to put one into a .3dm at all.
  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 3, 4);
  Model model;
  model.AddMesh(box);
  Check(model.ObjectCount() == 1, "model has one object after AddMesh()");

  const std::string path = "dino8_kernel_mesh_roundtrip_test.3dm";
  Check(model.Save(path) == Result::Ok, ".3dm save with a mesh object succeeded");

  Model loaded;
  Check(Model::Load(path, loaded) == Result::Ok, ".3dm load succeeded");
  Check(loaded.ObjectCount() == 1, "round-tripped model has one object");

  // Not just "an object exists" - dig out the actual mesh geometry and
  // check its vertex/face counts and volume genuinely survived the
  // round trip, not just some object of some type.
  ONX_ModelComponentIterator iterator(loaded.raw(), ON_ModelComponent::Type::ModelGeometry);
  bool found_mesh = false;
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const auto* mesh_geometry = dynamic_cast<const ON_Mesh*>(geometry_component->Geometry(nullptr));
    if (mesh_geometry == nullptr) {
      continue;
    }
    found_mesh = true;
    Check(mesh_geometry->m_V.Count() == box.VertexCount(),
          "the round-tripped mesh object has the original's vertex count (8)");
    Check(mesh_geometry->m_F.Count() == box.FaceCount(),
          "the round-tripped mesh object has the original's face count (6)");
    Mesh reloaded_mesh;
    reloaded_mesh.raw() = *mesh_geometry;
    Check(std::abs(reloaded_mesh.Volume() - box.Volume()) < 1e-9,
          "the round-tripped mesh's volume exactly matches the original "
          "(quad faces preserved, not reinterpreted)");
  }
  Check(found_mesh, "the .3dm file's model geometry actually contains a mesh object");

  std::remove(path.c_str());
}

void TestModelAddSubDRoundTrips() {
  using dino8::kernel::Model;
  using dino8::kernel::Result;
  using dino8::kernel::SubD;

  // AddSubD() closes the same "no way to put this into a .3dm" gap
  // AddMesh() closed, for SubD instead of Mesh.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 3, 4);
  const auto subd = SubD::FromControlMesh(quad_box);
  Model model;
  model.AddSubD(subd);
  Check(model.ObjectCount() == 1, "model has one object after AddSubD()");

  const std::string path = "dino8_kernel_subd_roundtrip_test.3dm";
  Check(model.Save(path) == Result::Ok, ".3dm save with a SubD object succeeded");

  Model loaded;
  Check(Model::Load(path, loaded) == Result::Ok, ".3dm load succeeded");
  Check(loaded.ObjectCount() == 1, "round-tripped model has one object");

  ONX_ModelComponentIterator iterator(loaded.raw(), ON_ModelComponent::Type::ModelGeometry);
  bool found_subd = false;
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const auto* subd_geometry = dynamic_cast<const ON_SubD*>(geometry_component->Geometry(nullptr));
    if (subd_geometry == nullptr) {
      continue;
    }
    found_subd = true;
    Check(static_cast<int>(subd_geometry->VertexCount()) == subd.VertexCount(),
          "the round-tripped SubD object has the original's vertex count");
    Check(static_cast<int>(subd_geometry->FaceCount()) == subd.FaceCount(),
          "the round-tripped SubD object has the original's face count (6)");
  }
  Check(found_subd, "the .3dm file's model geometry actually contains a SubD object");

  std::remove(path.c_str());
}

void TestBoxVolume() {
  const auto box = MakeBox(0, 0, 0, 2, 2, 2);
  Check(std::abs(box.Volume() - 8.0) < 1e-9, "unit-scaled box volume is correct");
}

void TestBooleanUnion() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;

  const auto a = MakeBox(0, 0, 0, 2, 2, 2);   // volume 8
  const auto b = MakeBox(1, 1, 1, 3, 3, 3);   // volume 8, overlaps a in [1,2]^3 (volume 1)

  const auto result = BooleanCombine(a, b, BooleanOp::Union);
  Check(std::abs(result.Volume() - 15.0) < 1e-6,
        "union volume equals 8 + 8 - 1 overlap");
}

void TestBooleanIntersection() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;

  const auto a = MakeBox(0, 0, 0, 2, 2, 2);
  const auto b = MakeBox(1, 1, 1, 3, 3, 3);

  const auto result = BooleanCombine(a, b, BooleanOp::Intersection);
  Check(std::abs(result.Volume() - 1.0) < 1e-6,
        "intersection volume equals the 1x1x1 overlap");
}

void TestBooleanDifference() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;

  const auto a = MakeBox(0, 0, 0, 2, 2, 2);
  const auto b = MakeBox(1, 1, 1, 3, 3, 3);

  const auto result = BooleanCombine(a, b, BooleanOp::Difference);
  Check(std::abs(result.Volume() - 7.0) < 1e-6,
        "difference volume equals 8 - 1 overlap");
}

void TestBooleanSymmetricDifference() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;

  // Same two boxes as the other boolean tests (volume 8 each, 1x1x1 = 1
  // overlap). Manifold has no direct XOR op, so SymmetricDifference is
  // computed as Union - Intersection (two extra Manifold calls) - checked
  // against the equally-valid alternate formula (A-B)+(B-A) = 7+7=14, not
  // just internal self-consistency with the same implementation this test
  // is verifying.
  const auto a = MakeBox(0, 0, 0, 2, 2, 2);
  const auto b = MakeBox(1, 1, 1, 3, 3, 3);

  const auto result = BooleanCombine(a, b, BooleanOp::SymmetricDifference);
  Check(std::abs(result.Volume() - 14.0) < 1e-6,
        "symmetric difference volume equals (8-1) + (8-1) = 14, matching "
        "the union-minus-intersection formula against the independent "
        "(A-B)+(B-A) one");
}

void TestSplitByPlane() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::SplitByPlane;
  using dino8::kernel::Vector3d;

  // A symmetric box split exactly down its own midplane: both halves
  // must have exactly half the original volume, and (confirmed
  // empirically, not assumed from the doc alone) the first result is on
  // the side plane_normal points toward (x >= 1), the second on the
  // opposite side (x <= 1).
  const auto box = MakeBox(0, 0, 0, 2, 2, 2);
  const auto halves = SplitByPlane(box, Vector3d(1, 0, 0), 1.0);
  Check(std::abs(halves.first.Volume() - 4.0) < 1e-9 &&
            std::abs(halves.second.Volume() - 4.0) < 1e-9,
        "splitting a 2x2x2 box down its own midplane gives two exact "
        "volume-4 halves");
  Check(std::abs(halves.first.Volume() + halves.second.Volume() - box.Volume()) < 1e-9,
        "the two halves' volumes sum back to exactly the original box's volume");

  const auto first_bounds = halves.first.GetBoundingBox();
  Check(std::abs(first_bounds.min.x - 1.0) < 1e-9 && std::abs(first_bounds.max.x - 2.0) < 1e-9,
        "the first result is on the side plane_normal points toward "
        "(x in [1, 2], the +normal side)");
  const auto second_bounds = halves.second.GetBoundingBox();
  Check(std::abs(second_bounds.min.x - 0.0) < 1e-9 && std::abs(second_bounds.max.x - 1.0) < 1e-9,
        "the second result is on the opposite side (x in [0, 1])");

  // Both halves are genuine closed solids, not open shells needing a
  // separate capping step - real proof via Manifold's own watertightness
  // check (same pattern every other closed-solid primitive here uses),
  // not just "the volume number looked plausible."
  const auto disjoint_box = Mesh::Cylinder(Point3d(10, 10, 10), Vector3d(0, 0, 1), 0.5, 1.0);
  const auto union_with_first =
      dino8::kernel::BooleanCombine(halves.first, disjoint_box, dino8::kernel::BooleanOp::Union);
  Check(std::abs(union_with_first.Volume() - (halves.first.Volume() + disjoint_box.Volume())) <
            1e-6,
        "the first half is watertight: union with a disjoint cylinder "
        "equals the sum of both volumes");
  const auto union_with_second =
      dino8::kernel::BooleanCombine(halves.second, disjoint_box, dino8::kernel::BooleanOp::Union);
  Check(std::abs(union_with_second.Volume() - (halves.second.Volume() + disjoint_box.Volume())) <
            1e-6,
        "the second half is watertight too: union with a disjoint "
        "cylinder equals the sum of both volumes");

  // An off-center plane through a non-symmetric axis, to rule out this
  // only working for a plane through a shape's own center of symmetry:
  // a 4x2x2 box (total volume 16) split at x=3 gives a width-1 slab
  // (x in [3,4], volume 1*2*2=4) and a width-3 slab (x in [0,3],
  // volume 3*2*2=12), not an even 8/8 split.
  const auto tall_box = MakeBox(0, 0, 0, 4, 2, 2);
  const auto off_center_halves = SplitByPlane(tall_box, Vector3d(1, 0, 0), 3.0);
  Check(std::abs(off_center_halves.first.Volume() - 4.0) < 1e-9 &&
            std::abs(off_center_halves.second.Volume() - 12.0) < 1e-9,
        "splitting a 4x2x2 box at x=3 (not its midpoint) gives volumes "
        "4 and 12, not an assumed even split");
}

void TestConvexHull() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::ConvexHull;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // The hull of exactly a cube's own 8 corners must be that same cube -
  // hand-derivable exact volume, and a real watertight solid (not just a
  // triangle soup that happens to have the right volume number),
  // verified the same way every other closed-solid primitive here is.
  const std::vector<Point3d> cube_corners = {
      Point3d(0, 0, 0), Point3d(2, 0, 0), Point3d(2, 2, 0), Point3d(0, 2, 0),
      Point3d(0, 0, 2), Point3d(2, 0, 2), Point3d(2, 2, 2), Point3d(0, 2, 2),
  };
  const auto hull = ConvexHull(cube_corners);
  Check(std::abs(hull.Volume() - 8.0) < 1e-9,
        "the convex hull of a cube's 8 corners has exactly volume 8, the "
        "cube's own volume");
  const auto disjoint_box = Mesh::Cylinder(Point3d(10, 10, 10), Vector3d(0, 0, 1), 0.5, 1.0);
  const auto union_result = BooleanCombine(hull, disjoint_box, BooleanOp::Union);
  Check(std::abs(union_result.Volume() - (hull.Volume() + disjoint_box.Volume())) < 1e-6,
        "the cube hull is watertight: union with a disjoint cylinder "
        "equals the sum of both volumes");

  // Adding points strictly inside the hull of the others (the cube's own
  // center, and a point on one face's own interior) must not change the
  // result at all - only points that are themselves hull vertices affect
  // a convex hull, exactly the property that makes "hull of everything"
  // a useful bounding operation without pre-filtering the input first.
  std::vector<Point3d> with_interior_points = cube_corners;
  with_interior_points.push_back(Point3d(1, 1, 1));  // cube's own center
  with_interior_points.push_back(Point3d(1, 1, 0));  // center of the z=0 face
  const auto hull_with_interior = ConvexHull(with_interior_points);
  Check(std::abs(hull_with_interior.Volume() - 8.0) < 1e-9,
        "adding points strictly inside the cube's own hull doesn't "
        "change the resulting hull's volume at all");

  bool threw_too_few = false;
  try {
    ConvexHull({Point3d(0, 0, 0), Point3d(1, 0, 0), Point3d(0, 1, 0)});
  } catch (const std::invalid_argument&) {
    threw_too_few = true;
  }
  Check(threw_too_few,
        "ConvexHull throws on fewer than 4 points (can't bound a "
        "nonzero 3D volume)");
}

void TestSimplify() {
  using dino8::kernel::Brep;
  using dino8::kernel::Simplify;

  // A box tessellated at 20x20 per face - each face is still exactly
  // flat (a bilinear surface tessellated finely is still planar, just
  // redundantly so), giving thousands of coplanar triangles that carry
  // no actual shape information beyond the original 12. Simplify() with
  // a tight tolerance should collapse it back down to exactly that
  // minimal representation, and volume must survive exactly (not just
  // "close"), since the true surface really is flat - there's no
  // approximation error a real decimation algorithm should introduce
  // here.
  const auto fine_box = Brep::Box(0, 0, 0, 2, 2, 2).TessellateToClosedMesh(20, 20);
  Check(fine_box.FaceCount() == 4800,
        "the 20x20-per-face tessellated box has 4800 triangles (6 faces "
        "x 20x20 cells x 2 triangles) before simplification");
  const auto simplified = Simplify(fine_box, 1e-6);
  Check(simplified.VertexCount() == 8 && simplified.FaceCount() == 12,
        "Simplify() collapses the over-tessellated flat box down to "
        "exactly its minimal 8-vertex, 12-triangle representation");
  Check(std::abs(simplified.Volume() - fine_box.Volume()) < 1e-9,
        "Simplify() preserves the (exactly flat) box's volume exactly, "
        "not just approximately");
}

void TestMinkowskiSum() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Mesh;
  using dino8::kernel::MinkowskiSum;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // The Minkowski sum of two axis-aligned boxes is exactly a third box
  // whose min/max corners are each input's own corners added
  // component-wise - a hand-derivable exact case straight from the
  // definition A+B = {a+b : a in A, b in B}, not something needing
  // Manifold-specific knowledge to predict. [0,2]x[0,3]x[0,4] + [0,1]^3 =
  // [0,3]x[0,4]x[0,5], volume 3*4*5=60 (not 2*3*4 + 1 = 25, the wrong
  // answer a "just add the volumes" guess would give).
  const auto a = MakeBox(0, 0, 0, 2, 3, 4);
  const auto b = MakeBox(0, 0, 0, 1, 1, 1);
  const auto sum = MinkowskiSum(a, b);
  const auto bounds = sum.GetBoundingBox();
  Check(bounds.min.x == 0.0 && bounds.min.y == 0.0 && bounds.min.z == 0.0,
        "the Minkowski sum's min corner is exactly (0,0,0) (both inputs' "
        "own min corners, both already at the origin)");
  Check(bounds.max.x == 3.0 && bounds.max.y == 4.0 && bounds.max.z == 5.0,
        "the Minkowski sum's max corner is exactly the component-wise "
        "sum of both inputs' own max corners (2+1, 3+1, 4+1)");
  Check(std::abs(sum.Volume() - 60.0) < 1e-9,
        "the Minkowski sum's volume is exactly 3*4*5=60, not the wrong "
        "'sum of the two volumes' answer (25)");

  // MinkowskiDifference() is the complement operation - shrinking rather
  // than growing. Summing A with B and then taking the difference with B
  // again is a real round-trip check, not just "it runs without
  // throwing" - but empirically (checked directly, not assumed from the
  // name) Manifold's erosion recovers a box *congruent* to A (exactly
  // A's own 2x3x4 dimensions and volume 24) translated by B's own max
  // corner (1,1,1), not literally repositioned back to A's exact
  // original location - a real, non-obvious detail of how erosion is
  // defined for a B that isn't itself centered on the origin, not a
  // limitation of this wrapper. Asserting the size/volume invariant
  // (robust regardless of that translation) rather than an absolute
  // position this class's own comment can't derive from first
  // principles.
  const auto shrunk_back = dino8::kernel::MinkowskiDifference(sum, b);
  const auto shrunk_bounds = shrunk_back.GetBoundingBox();
  const Vector3d shrunk_extent = shrunk_bounds.max - shrunk_bounds.min;
  Check(std::abs(shrunk_extent.x - 2.0) < 1e-6 && std::abs(shrunk_extent.y - 3.0) < 1e-6 &&
            std::abs(shrunk_extent.z - 4.0) < 1e-6,
        "MinkowskiSum() followed by MinkowskiDifference() with the same "
        "shape recovers a box with exactly A's own 2x3x4 dimensions");
  Check(std::abs(shrunk_back.Volume() - 24.0) < 1e-6,
        "...and exactly A's own volume (24), confirming a real "
        "size round-trip even though the erosion translates the result");
}

void TestDecompose() {
  using dino8::kernel::Decompose;
  using dino8::kernel::Mesh;

  // MergeAndWeld() concatenates several meshes into one with no way to
  // tell the pieces apart again afterward - two disjoint (non-touching)
  // boxes, each with a different, individually hand-known volume, so
  // Decompose() splitting them back apart (rather than merging them into
  // one connected piece, since they don't overlap or touch at all) is
  // directly checkable.
  const auto box_a = MakeBox(0, 0, 0, 2, 2, 2);          // volume 8
  const auto box_b = MakeBox(10, 10, 10, 11, 12, 13);    // volume 1*2*3=6
  const auto combined = Mesh::MergeAndWeld({box_a, box_b});

  const auto pieces = Decompose(combined);
  Check(pieces.size() == 2,
        "decomposing two disjoint boxes merged into one mesh gives back "
        "exactly 2 disconnected pieces");

  // Order isn't specified, so match by volume rather than index.
  bool found_a = false;
  bool found_b = false;
  for (const auto& piece : pieces) {
    if (std::abs(piece.Volume() - 8.0) < 1e-9) {
      found_a = true;
    } else if (std::abs(piece.Volume() - 6.0) < 1e-9) {
      found_b = true;
    }
  }
  Check(found_a && found_b,
        "the two decomposed pieces have exactly the two original boxes' "
        "own volumes (8 and 6), not merged or corrupted");
}

void TestMinGap() {
  using dino8::kernel::MinGap;

  // Two boxes separated by a known, hand-derivable gap along X: box A
  // spans x in [0,2], box B spans x in [5,7] (same y/z range, so the
  // true minimum gap is exactly the x-axis separation, 5-2=3).
  const auto a = MakeBox(0, 0, 0, 2, 2, 2);
  const auto b = MakeBox(5, 0, 0, 7, 2, 2);
  Check(std::abs(MinGap(a, b, /*search_length=*/10.0) - 3.0) < 1e-6,
        "the minimum gap between two boxes separated by exactly 3 units "
        "along X is exactly 3.0");

  // Overlapping boxes: gap is exactly 0, checked via a real intersection
  // test (Manifold::MinGap's own short-circuit), not a coincidentally
  // small search result.
  const auto c = MakeBox(1, 1, 1, 3, 3, 3);  // overlaps `a` in [1,2]^3
  Check(MinGap(a, c, /*search_length=*/10.0) == 0.0,
        "the minimum gap between two overlapping boxes is exactly 0.0");

  // Touching (but not overlapping) boxes: gap is also exactly 0 - boxes
  // sharing a boundary face count as touching, not "a tiny positive gap."
  const auto d = MakeBox(2, 0, 0, 4, 2, 2);  // shares the x=2 face with `a`
  Check(MinGap(a, d, /*search_length=*/10.0) == 0.0,
        "the minimum gap between two boxes sharing a boundary face is "
        "exactly 0.0");
}

void TestRefineToLength() {
  using dino8::kernel::RefineToLength;

  // A 2x2x2 box's own edges are all length 2 - well above a 0.5 target,
  // so every face must be subdivided into smaller triangles. The shape
  // is exactly flat everywhere, so - unlike Simplify()'s test, which
  // collapses detail without losing accuracy on a flat shape - this is
  // the opposite direction (adding detail) but the same invariant: exact
  // volume preservation, since refining a flat face into more triangles
  // can't change what region it covers.
  const auto box = MakeBox(0, 0, 0, 2, 2, 2);
  const auto refined = RefineToLength(box, 0.5);
  Check(refined.FaceCount() > box.FaceCount(),
        "RefineToLength() with a target well below the box's own 2-unit "
        "edge length increases the triangle count");
  Check(std::abs(refined.Volume() - box.Volume()) < 1e-9,
        "RefineToLength() preserves the (exactly flat) box's volume "
        "exactly, since subdividing a flat face doesn't change the "
        "region it covers");
}

void TestSmoothAndRefine() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::ConvexHull;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::SmoothAndRefine;
  using dino8::kernel::Vector3d;

  // A regular octahedron (ConvexHull of the 6 unit-axis points) has
  // exact volume 4/3 (two unit-height square pyramids, base area 2,
  // glued base to base: 2*(1/3)*2*1). Its vertex normals are already
  // exactly radial (pointing straight out from the origin through each
  // vertex), so smoothing with every edge forced smooth (min_sharp_angle
  // = 180, well past the octahedron's own ~109.5-degree dihedral angle,
  // which the *default* angle would instead leave faceted) should bulge
  // the surface strictly outward from the flat facets - a real,
  // measurable volume increase, not a no-op.
  const std::vector<Point3d> octahedron_points = {
      Point3d(1, 0, 0),  Point3d(-1, 0, 0), Point3d(0, 1, 0),
      Point3d(0, -1, 0), Point3d(0, 0, 1),  Point3d(0, 0, -1),
  };
  const auto octahedron = ConvexHull(octahedron_points);
  Check(std::abs(octahedron.Volume() - 4.0 / 3.0) < 1e-9,
        "the octahedron's own volume is exactly 4/3, the hand-derivable "
        "two-pyramid formula");

  const auto smoothed =
      SmoothAndRefine(octahedron, /*target_length=*/0.05, /*min_sharp_angle=*/180.0);
  Check(smoothed.Volume() > octahedron.Volume() + 0.1,
        "smoothing and refining the octahedron with every edge forced "
        "smooth measurably increases its volume - the surface actually "
        "bulges outward, not a no-op that just adds triangles");
  // Sanity upper bound: every original vertex is exactly 1 unit from the
  // origin, so the smoothed surface (which only bulges between existing
  // vertices, never past them) can't exceed the volume of the unit
  // sphere those vertices sit on.
  Check(smoothed.Volume() < (4.0 / 3.0) * ON_PI,
        "the smoothed octahedron's volume stays below the circumscribing "
        "unit sphere's volume (4/3*pi), consistent with bulging only "
        "between the original vertices rather than past them");

  // Still a genuine watertight solid, not just a plausible volume number
  // - proven the same way every other closed-solid operation here is.
  const auto disjoint_box = Mesh::Cylinder(Point3d(10, 10, 10), Vector3d(0, 0, 1), 0.5, 1.0);
  const auto union_result = BooleanCombine(smoothed, disjoint_box, BooleanOp::Union);
  Check(std::abs(union_result.Volume() - (smoothed.Volume() + disjoint_box.Volume())) < 1e-6,
        "the smoothed-and-refined octahedron is watertight: union with a "
        "disjoint cylinder equals the sum of both volumes");
}

void TestCountDegenerateTriangles() {
  using dino8::kernel::CountDegenerateTriangles;
  using dino8::kernel::Mesh;

  // A normal, cleanly-constructed box (both quad- and triangle-faced)
  // has no degenerate triangles - the baseline every one of this
  // kernel's own primitives should meet.
  Check(CountDegenerateTriangles(MakeQuadBoxMesh(0, 0, 0, 2, 2, 2)) == 0,
        "a normal quad-faced box has 0 degenerate triangles");
  Check(CountDegenerateTriangles(MakeBox(0, 0, 0, 2, 2, 2)) == 0,
        "a normal triangle-faced box has 0 degenerate triangles");

  // Deliberately collapsing one triangle to a straight line (moving a
  // shared vertex onto the line between two others of the same
  // triangle) doesn't actually produce a nonzero count here, checked
  // directly rather than assumed: Manifold's own mesh construction
  // "attempts to remove all of these" (per its own doc comment) as part
  // of building the Manifold in the first place, so a straightforward
  // collapsed triangle like this gets cleaned up before
  // NumDegenerateTris() is ever asked about it. This is consistent with
  // its own documented purpose - reporting a degeneracy the library
  // *couldn't* clean up, which a simple single-collapsed-triangle case
  // isn't - rather than every degeneracy that was ever fed in.
  Mesh degenerate_box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  degenerate_box.raw().m_V[1] = ON_3fPoint(1, 1, 0);  // collapses one bottom-face triangle
  Check(CountDegenerateTriangles(degenerate_box) == 0,
        "Manifold's own construction removes a straightforwardly "
        "collapsed triangle before CountDegenerateTriangles() sees it - "
        "confirmed directly, not assumed from the doc comment alone");
}

void TestBrepTessellation() {
  using dino8::kernel::Brep;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  std::vector<Point3d> grid;
  for (int u = 0; u < 4; ++u) {
    for (int v = 0; v < 4; ++v) {
      grid.emplace_back(u, v, 0.0);
    }
  }
  NurbsSurface surf = NurbsSurface::FromControlGrid(grid, 4, 4, 3, 3);
  Brep brep = Brep::FromSurface(surf);

  const auto meshes = brep.Tessellate(/*u_divisions=*/4, /*v_divisions=*/4);
  Check(meshes.size() == 1, "tessellation produced one mesh per face");
  Check(!meshes.empty() && meshes.front().VertexCount() == 5 * 5,
        "tessellated mesh has the expected (divisions+1)^2 vertex count");
  Check(!meshes.empty() && meshes.front().FaceCount() == 4 * 4 * 2,
        "tessellated mesh has the expected 2 triangles per grid cell");
}

void TestBrepBoxIsClosedAndWatertight() {
  using dino8::kernel::Brep;

  const Brep box = Brep::Box(0, 0, 0, 2, 2, 2);
  Check(box.FaceCount() == 6, "Brep::Box has six faces");

  // u_divisions = v_divisions = 1 means each face is exactly its 2
  // corner-to-corner triangles (no interior subdivision), so welding
  // should collapse the 6 faces * 4 corners = 24 raw vertices down to
  // exactly the box's 8 unique corners.
  const auto mesh = box.TessellateToClosedMesh(/*u_divisions=*/1, /*v_divisions=*/1);
  Check(mesh.VertexCount() == 8,
        "welding a tessellated Brep::Box collapses shared-edge vertices to 8 corners");
  Check(mesh.FaceCount() == 12, "welded box mesh has 12 triangles (2 per face x 6 faces)");
  Check(std::abs(mesh.Volume() - 8.0) < 1e-6,
        "Brep::Box -> Tessellate -> weld volume matches the box's true volume");
}

void TestBrepLacksFullOpenNurbsTopologyButStillUsable() {
  using dino8::kernel::Brep;

  // A real, previously-undocumented architectural fact, checked directly
  // rather than assumed: this kernel's own Brep-building factories
  // (Box(), Sphere(), TrimmedPlanarFace()) call ON_Brep::NewFace(int) -
  // the minimal, surface-only overload - rather than building genuine
  // ON_Brep vertex/edge/trim/loop topology the way Rhino's own file
  // format expects. ON_Brep::IsValid() checks exactly that topology, so
  // it reports every Brep this kernel builds as invalid, even a
  // perfectly good one like Box().
  const Brep box = Brep::Box(0, 0, 0, 2, 2, 2);
  ON_TextLog discard_log;
  Check(!box.raw().IsValid(&discard_log),
        "ON_Brep::IsValid() reports Brep::Box() as invalid, since this "
        "kernel builds faces via the minimal NewFace(surface) overload "
        "rather than genuine vertex/edge/trim/loop topology - a real, "
        "checked fact, not a bug being newly introduced here");

  // That doesn't stop it from being fully usable through this kernel's
  // own pipeline, which never calls ON_Brep::IsValid() and doesn't need
  // the topology it checks for - Tessellate() reads each face's surface
  // directly, and TessellateToClosedMesh()'s own welding step is what
  // actually closes the seams, not shared ON_Brep vertex/edge records.
  const auto mesh = box.TessellateToClosedMesh(1, 1);
  Check(std::abs(mesh.Volume() - 8.0) < 1e-9,
        "despite IsValid()==false, the same Brep tessellates and welds "
        "into a genuinely correct, watertight solid through this "
        "kernel's own pipeline - the missing topology only matters to "
        "ON_Brep::IsValid() itself, not to how this kernel actually uses "
        "a Brep");
}

void TestBrepGetTightBoundingBox() {
  using dino8::kernel::Brep;
  using dino8::kernel::Point3d;

  // Box(): six flat faces, so the tight bounding box is exactly the box's
  // own corners - hand-derivable exact, no tessellation involved at all.
  const Brep box = Brep::Box(1, -2, 0.5, 4, 3, 7.5);
  const auto box_bounds = box.GetTightBoundingBox();
  Check(box_bounds.min.x == 1.0 && box_bounds.min.y == -2.0 && box_bounds.min.z == 0.5,
        "Brep::Box's tight bounding box min corner matches its known low corner exactly");
  Check(box_bounds.max.x == 4.0 && box_bounds.max.y == 3.0 && box_bounds.max.z == 7.5,
        "Brep::Box's tight bounding box max corner matches its known high corner exactly");

  // Sphere(): a curved surface, so this actually exercises the "tight",
  // not just control-point, bounding box - a sphere's own control net
  // (the NURBS control polygon) extends well outside the true surface
  // (it has to, to represent a circle with a rational NURBS curve), so a
  // naive control-point bbox would overshoot. The true tight bbox is
  // exactly [-r, r] on every axis around the center, since a full sphere
  // touches its own bounding box on every face.
  const double radius = 3.0;
  const Point3d center(10, -5, 2);
  const Brep sphere = Brep::Sphere(center, radius);
  const auto sphere_bounds = sphere.GetTightBoundingBox();
  Check(std::abs(sphere_bounds.min.x - (center.x - radius)) < 1e-9 &&
            std::abs(sphere_bounds.min.y - (center.y - radius)) < 1e-9 &&
            std::abs(sphere_bounds.min.z - (center.z - radius)) < 1e-9,
        "Brep::Sphere's tight bounding box min corner is exactly center - radius "
        "on every axis, not overshot by the NURBS control net");
  Check(std::abs(sphere_bounds.max.x - (center.x + radius)) < 1e-9 &&
            std::abs(sphere_bounds.max.y - (center.y + radius)) < 1e-9 &&
            std::abs(sphere_bounds.max.z - (center.z + radius)) < 1e-9,
        "Brep::Sphere's tight bounding box max corner is exactly center + radius "
        "on every axis");

  // A genuine discovery, not assumed from the method's name: a
  // doubly-curved bicubic surface whose true peak lies at its own
  // interior center - not on any boundary or Greville-abscissa isocurve
  // GetTightBoundingBox() actually samples - comes back overshot rather
  // than exact. Tensor-product quadratic bump: z(u,v) = [2u(1-u)] *
  // [2v(1-v)] * peak_height (each direction independently contributes
  // its own 1D quadratic-Bezier bump, same shape as
  // TestCurveGetTightBoundingBox's curve). True max at u=v=0.5:
  // 0.5 * 0.5 * peak_height = 0.25 * peak_height - confirmed directly
  // via NurbsSurface::PointAt(), not just algebra.
  const double peak_height = 5.0;
  std::vector<Point3d> bulge_grid;
  for (int u = 0; u < 3; ++u) {
    for (int v = 0; v < 3; ++v) {
      bulge_grid.emplace_back(u, v, (u == 1 && v == 1) ? peak_height : 0.0);
    }
  }
  const auto bulge_surface =
      dino8::kernel::NurbsSurface::FromControlGrid(bulge_grid, 3, 3, 2, 2);
  const Point3d true_peak = bulge_surface.PointAt(0.5, 0.5);
  Check(std::abs(true_peak.z - 0.25 * peak_height) < 1e-9,
        "the bicubic bulge surface's own true interior peak z-coordinate "
        "is exactly 0.25*peak_height, confirmed directly via PointAt()");
  const auto bulge_brep = Brep::FromSurface(bulge_surface);
  const auto bulge_bounds = bulge_brep.GetTightBoundingBox();
  Check(std::abs(bulge_bounds.max.z - 0.5 * peak_height) < 1e-6,
        "GetTightBoundingBox() overshoots this bulge's true peak "
        "(0.25*peak_height) to 0.5*peak_height instead - it only samples "
        "boundary/Greville isocurves, never the genuine 2D interior "
        "extremum, the same real public-build limitation "
        "TestCurveGetTightBoundingBox found for a curve");
}

void TestBrepBooleanEndToEnd() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;

  // Same scenario as TestBooleanUnion/Intersection/Difference above, but
  // built through the real Brep -> Tessellate -> weld pipeline instead
  // of MakeBox()'s hand-authored mesh - this is the gap the previous
  // chunk's README flagged: "Brep only constructs untrimmed, open
  // surfaces... can't feed BooleanCombine() yet." Box() + TessellateToClosedMesh()
  // close it.
  const auto a = Brep::Box(0, 0, 0, 2, 2, 2).TessellateToClosedMesh(1, 1);
  const auto b = Brep::Box(1, 1, 1, 3, 3, 3).TessellateToClosedMesh(1, 1);

  const auto union_result = BooleanCombine(a, b, BooleanOp::Union);
  Check(std::abs(union_result.Volume() - 15.0) < 1e-6,
        "Brep-built union volume equals 8 + 8 - 1 overlap");

  const auto intersection_result = BooleanCombine(a, b, BooleanOp::Intersection);
  Check(std::abs(intersection_result.Volume() - 1.0) < 1e-6,
        "Brep-built intersection volume equals the 1x1x1 overlap");

  const auto difference_result = BooleanCombine(a, b, BooleanOp::Difference);
  Check(std::abs(difference_result.Volume() - 7.0) < 1e-6,
        "Brep-built difference volume equals 8 - 1 overlap");
}

void TestBrepSphereIsClosedAndWatertight() {
  using dino8::kernel::Brep;
  using dino8::kernel::Point3d;

  const double radius = 2.0;
  const Brep sphere = Brep::Sphere(Point3d(0, 0, 0), radius);
  Check(sphere.FaceCount() == 1, "Brep::Sphere is a single curved face");

  // A genuinely curved case, unlike Box(): the sphere's own u-seam
  // (u=0 and u=2*pi are the same meridian) and its two poles (every u
  // value at v_min/v_max collapses to one physical point) both have to
  // be welded shut against *themselves*, not just against a neighboring
  // face - exactly what the previous chunk's README flagged as
  // unvalidated ("not yet validated against curved surfaces").
  const int divisions = 32;
  const auto mesh = sphere.TessellateToClosedMesh(divisions, divisions);

  const int raw_vertex_count = (divisions + 1) * (divisions + 1);
  Check(mesh.VertexCount() < raw_vertex_count,
        "welding the sphere's own seam and poles reduces its vertex count");

  const double exact_volume = (4.0 / 3.0) * M_PI * radius * radius * radius;
  const double relative_error = std::abs(mesh.Volume() - exact_volume) / exact_volume;
  Check(relative_error < 0.01,
        "tessellated+welded sphere volume is within 1% of the exact 4/3*pi*r^3");
}

void TestBrepSphereBooleanEndToEnd() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Point3d;

  // Proves the weld pipeline's output is actually usable by Manifold, not
  // just internally self-consistent: two overlapping spheres, same radius,
  // centers offset by the radius along X. Exact spherical-cap overlap
  // volume for two radius-r spheres with center distance d = r is a closed
  // form (from the standard sphere-sphere intersection formula), so this
  // checks against real geometry, not just "didn't crash."
  const double r = 2.0;
  const double d = r;
  const auto a = Brep::Sphere(Point3d(0, 0, 0), r).TessellateToClosedMesh(32, 32);
  const auto b = Brep::Sphere(Point3d(d, 0, 0), r).TessellateToClosedMesh(32, 32);

  // Standard two-equal-sphere lens-volume formula:
  // V = (pi * (4r + d) * (2r - d)^2) / 12
  const double exact_lens = (M_PI * (4 * r + d) * (2 * r - d) * (2 * r - d)) / 12.0;

  const auto intersection_result = BooleanCombine(a, b, BooleanOp::Intersection);
  const double relative_error =
      std::abs(intersection_result.Volume() - exact_lens) / exact_lens;
  Check(relative_error < 0.03,
        "sphere-sphere boolean intersection volume is within 3% of the exact lens formula");
}

void TestBrepTrimmedPlanarFace() {
  using dino8::kernel::Brep;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;

  // A 10x10 physical square, built the same bilinear way Box()'s faces
  // are (FromControlGrid always gives a [0,1]x[0,1] parameter domain),
  // trimmed to the inner square [0.15,0.85]^2 in UV. That boundary is
  // deliberately off the grid lines (grid lines land on multiples of
  // 0.1) so no grid point sits exactly on the trim edge - point-in-polygon
  // is well-defined here, not dependent on floating-point tie-breaking at
  // a boundary.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 10, 0),
      Point3d(10, 0, 0),
      Point3d(10, 10, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, /*u_count=*/2, /*v_count=*/2,
                                     /*u_degree=*/1, /*v_degree=*/1);

  const std::vector<Point2d> trim_loop = {
      Point2d(0.15, 0.15),
      Point2d(0.85, 0.15),
      Point2d(0.85, 0.85),
      Point2d(0.15, 0.85),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, trim_loop);
  Check(face.FaceCount() == 1, "TrimmedPlanarFace is a single face");

  // Grid points strictly inside (0.15, 0.85) at divisions=10 are
  // u,v in {0.2, 0.3, ..., 0.8} - 7 values per axis, so 7x7=49 vertices
  // and a 6x6 grid of fully-inside cells (12 divisions -> 72 triangles),
  // hand-derived, not measured after the fact.
  const auto meshes = face.Tessellate(/*u_divisions=*/10, /*v_divisions=*/10);
  Check(meshes.size() == 1, "trimmed face tessellates to one mesh");
  Check(meshes.front().VertexCount() == 49,
        "trimming excludes vertices outside the trim loop, keeping exactly the interior grid");
  Check(meshes.front().FaceCount() == 72,
        "trimming keeps exactly the fully-inside grid cells (6x6x2 triangles)");

  // Physical area: the bilinear map scales the unit param square to a
  // 10x10 physical one uniformly, so trimmed param area 0.6x0.6=0.36
  // maps to physical area 0.36*100=36 exactly.
  Check(std::abs(meshes.front().Area() - 36.0) < 1e-9,
        "trimmed face's physical area matches the exact scaled trim-loop area");
}

void TestWeldAcrossIndependentlyParameterizedSurfaces() {
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;
  using dino8::kernel::Result;

  // Every weld test so far (Box, Sphere) welds vertices that come from
  // literally the same double-precision Point3d values, or from one
  // surface's own self-intersection with itself - not the general case
  // this README flags as still open: two *independently constructed*
  // surfaces whose shared boundary is only geometrically coincident, not
  // parametrically identical. This gets closer: two adjacent unit
  // squares sharing the edge x=1, built as separate single-face Breps
  // (so Tessellate() and MergeAndWeld() see them exactly as if they'd
  // come from unrelated parts of a model), where the second square's
  // surface is degree-elevated (bilinear -> bicubic) after construction.
  // Degree elevation is mathematically shape-preserving but re-derives
  // the control points/knot vector through real floating-point
  // arithmetic, so evaluating its shared edge no longer goes through the
  // same computation path as the first square's - a real, not
  // artificial, test of whether MergeAndWeld's tolerance is doing its
  // job rather than merely matching identical bit patterns.
  const std::vector<Point3d> grid_a = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const Brep face_a = Brep::FromSurface(
      NurbsSurface::FromControlGrid(grid_a, 2, 2, /*u_degree=*/1, /*v_degree=*/1));

  const std::vector<Point3d> grid_b = {
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
      Point3d(2, 0, 0),
      Point3d(2, 1, 0),
  };
  NurbsSurface surface_b =
      NurbsSurface::FromControlGrid(grid_b, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const Result elevate_result = surface_b.ElevateDegree(/*direction=*/0, /*new_degree=*/3);
  Check(elevate_result == Result::Ok, "surface B's degree elevation succeeded");
  const Brep face_b = Brep::FromSurface(surface_b);

  auto meshes_a = face_a.Tessellate(/*u_divisions=*/4, /*v_divisions=*/4);
  auto meshes_b = face_b.Tessellate(/*u_divisions=*/4, /*v_divisions=*/4);
  std::vector<Mesh> combined;
  combined.insert(combined.end(), meshes_a.begin(), meshes_a.end());
  combined.insert(combined.end(), meshes_b.begin(), meshes_b.end());

  const auto welded = Mesh::MergeAndWeld(combined);

  // Each face's own 4x4 grid has 25 vertices; the shared edge (5 points)
  // is duplicated between them before welding (50 raw), so a correct
  // weld collapses exactly those 5 shared points, leaving 45.
  Check(welded.VertexCount() == 45,
        "welding two independently-parameterized adjacent faces "
        "(one degree-elevated after construction) collapses exactly the shared edge");
  Check(std::abs(welded.Area() - 2.0) < 1e-9,
        "welded two-square area is exactly 2.0 despite the degree elevation");
}

void TestExtrudeUntrimmedFaceIntoSolid() {
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // A 2x2 square in the z=0 plane, built the same way TrimmedPlanarFace's
  // test built its base face - u_dir x v_dir gives an outward +Z normal
  // (see the corner-order derivation comment in Brep::Box).
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 2, 0),
      Point3d(2, 0, 0),
      Point3d(2, 2, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const Brep face = Brep::FromSurface(surface);
  const auto cap = face.Tessellate(/*u_divisions=*/3, /*v_divisions=*/3).front();

  // Extrude downward (into -Z, away from the cap's own +Z normal) by 3 -
  // the solid should occupy z in [-3, 0], volume 2*2*3 = 12.
  const auto solid = Mesh::ExtrudeCappedSolid(cap, Vector3d(0, 0, -3));

  Check(solid.VertexCount() == 2 * cap.VertexCount(),
        "extrusion doubles the cap's vertex count exactly (no welding needed - "
        "near/far ends and walls all reuse the cap's own vertex positions)");
  Check(std::abs(solid.Volume() - 12.0) < 1e-9,
        "extruded solid's volume matches base area (4) x height (3) exactly");
}

void TestExtrudeTrimmedFaceFeedsBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // Same trimmed face as TestBrepTrimmedPlanarFace (exact area 36), now
  // extruded into an actual closed solid - proving Brep::TrimmedPlanarFace()
  // can feed BooleanCombine() after all, closing the gap the previous
  // chunk's README flagged ("a trimmed face can't feed BooleanCombine()
  // yet"). ExtrudeCappedSolid()'s boundary-edge extraction has to cope
  // with the trim's jagged/staircased boundary here, not a clean polygon -
  // this is the real test of it, not the flat-untrimmed-square case above.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 10, 0),
      Point3d(10, 0, 0),
      Point3d(10, 10, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const std::vector<Point2d> trim_loop = {
      Point2d(0.15, 0.15),
      Point2d(0.85, 0.15),
      Point2d(0.85, 0.85),
      Point2d(0.15, 0.85),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, trim_loop);
  const auto cap = face.Tessellate(/*u_divisions=*/10, /*v_divisions=*/10).front();

  const auto solid = Mesh::ExtrudeCappedSolid(cap, Vector3d(0, 0, -1));
  Check(std::abs(solid.Volume() - 36.0) < 1e-9,
        "extruded trimmed-face solid's volume matches trim area (36) x height (1) exactly");

  // Union with a disjoint box far away: if the extruded solid weren't
  // genuinely closed/watertight, Manifold::Status() would reject it and
  // BooleanCombine() would throw rather than return a result.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(solid, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - 37.0) < 1e-9,
        "union of the extruded trimmed solid with a disjoint unit box equals 36 + 1");
}

void TestCylinderVolumeAndBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const double radius = 2.0;
  const double height = 5.0;
  // Measured, not guessed, and directly comparable to the whole-cell
  // numbers this replaced: Cylinder() now tessellates its disk cap via
  // real boundary clipping (TessellateGridClippedExact), not whole-cell
  // in/out. At the SAME 48/48 divisions that measured a 7% volume error
  // with whole-cell trimming, exact clipping measures well under 1%; at
  // just 32/32 it measures ~0.64%. 1% here is a real, tight check on
  // that improvement, not a rubber stamp.
  const auto cylinder =
      Mesh::Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), radius, height,
                     /*circle_segments=*/32, /*grid_divisions=*/32);

  const double exact_volume = ON_PI * radius * radius * height;
  const double relative_error = std::abs(cylinder.Volume() - exact_volume) / exact_volume;
  Check(relative_error < 0.01,
        "cylinder volume (now via exact boundary clipping) is within 1% of pi*r^2*h");

  // Real proof of watertightness, same as the trimmed-face extrusion test:
  // Manifold would reject a non-manifold mesh outright rather than return
  // a plausible-looking wrong answer.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(cylinder, box, BooleanOp::Union);
  const double expected_union = cylinder.Volume() + 1.0;
  Check(std::abs(result.Volume() - expected_union) < 1e-6,
        "union of the cylinder with a disjoint unit box equals cylinder volume + 1");
}

void TestConeVolumeAndBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const double radius = 2.0;
  const double height = 5.0;
  // Same shape/resolution as TestCylinderVolumeAndBoolean, so the 1%
  // tolerance is directly comparable: ConeToApex() collapses
  // ExtrudeCappedSolid()'s wall geometry to a single triangle per
  // boundary edge instead of two, sharing the exact same disk-cap
  // construction (BuildCircularDiskCap) and boundary-edge validation
  // (ExtractValidatedBoundaryEdges) as Cylinder().
  const auto cone = Mesh::Cone(Point3d(0, 0, 0), Vector3d(0, 0, 1), radius, height,
                                /*circle_segments=*/32, /*grid_divisions=*/32);

  const double exact_volume = ON_PI * radius * radius * height / 3.0;
  const double relative_error = std::abs(cone.Volume() - exact_volume) / exact_volume;
  Check(relative_error < 0.01, "cone volume is within 1% of (1/3)*pi*r^2*h");

  // Real proof of watertightness, same as the cylinder test: Manifold
  // would reject a non-manifold mesh outright rather than return a
  // plausible-looking wrong answer.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(cone, box, BooleanOp::Union);
  const double expected_union = cone.Volume() + 1.0;
  Check(std::abs(result.Volume() - expected_union) < 1e-6,
        "union of the cone with a disjoint unit box equals cone volume + 1");
}

void TestRevolveProfileBiconeVolumeAndBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const double radius = 2.0;
  const double half_height = 5.0;
  // A "bicone"/football: on-axis apex, out to max radius at the
  // mid-height, back to an on-axis apex - i.e. two cones glued base to
  // base. Exact volume is exactly twice one cone's (1/3)*pi*r^2*h, a
  // closed form independent of RevolveProfile()'s own implementation
  // (unlike Cone(), which this doesn't reuse - RevolveProfile() builds
  // its bands and end fans directly from the profile).
  const std::vector<Point2d> profile = {
      Point2d(0.0, -half_height),
      Point2d(radius, 0.0),
      Point2d(0.0, half_height),
  };
  const auto bicone = Mesh::RevolveProfile(profile, Point3d(0, 0, 0), Vector3d(0, 0, 1),
                                            /*revolve_segments=*/32);

  const double exact_volume = 2.0 * (ON_PI * radius * radius * half_height / 3.0);
  const double relative_error = std::abs(bicone.Volume() - exact_volume) / exact_volume;
  Check(relative_error < 0.01,
        "revolved bicone volume is within 1% of 2*(1/3)*pi*r^2*half_height");

  // Real proof of watertightness, same as Cylinder()/Cone(): Manifold
  // would reject a non-manifold mesh outright rather than return a
  // plausible-looking wrong answer.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(bicone, box, BooleanOp::Union);
  const double expected_union = bicone.Volume() + 1.0;
  Check(std::abs(result.Volume() - expected_union) < 1e-6,
        "union of the revolved bicone with a disjoint unit box equals bicone volume + 1");
}

void TestRevolveProfileRejectsTooShortProfile() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  bool threw_too_short = false;
  try {
    const std::vector<Point2d> too_short = {Point2d(0.0, -1.0)};
    Mesh::RevolveProfile(too_short, Point3d(0, 0, 0), Vector3d(0, 0, 1), 16);
  } catch (const std::invalid_argument&) {
    threw_too_short = true;
  }
  Check(threw_too_short,
        "RevolveProfile throws on a 1-point profile (nothing to revolve into "
        "a solid)");
}

// An off-axis profile end used to be rejected outright; RevolveProfile()
// now closes it with a flat disc cap instead (see the header comment).
// This checks that capability against three independent closed-form
// volumes, using the smallest possible profile (m=2, no interior rings)
// so each test isolates exactly the new end-cap code path plus one band.
void TestRevolveProfileFlatEndCaps() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const Point3d origin(0, 0, 0);
  const Vector3d up(0, 0, 1);

  // Cone built base-first (off-axis flat-capped base tapering to an
  // on-axis apex) - the reverse construction order from Mesh::Cone(), so
  // this is a genuine independent check of the new cap's orientation, not
  // just a call-through. Exact volume: (1/3)*pi*r^2*h.
  {
    const double radius = 3.0;
    const double height = 5.0;
    const std::vector<Point2d> profile = {Point2d(radius, 0.0), Point2d(0.0, height)};
    double previous_error = 1e9;
    for (const int segments : {8, 32, 128}) {
      const auto cone = Mesh::RevolveProfile(profile, origin, up, segments);
      const double exact_volume = ON_PI * radius * radius * height / 3.0;
      const double error = std::abs(cone.Volume() - exact_volume);
      Check(error < previous_error || error < 1e-6,
            "base-first flat-capped cone volume error shrinks as "
            "revolve_segments increases");
      previous_error = error;
    }
    const auto cone = Mesh::RevolveProfile(profile, origin, up, 64);
    const double exact_volume = ON_PI * radius * radius * height / 3.0;
    Check(std::abs(cone.Volume() - exact_volume) / exact_volume < 0.01,
          "base-first flat-capped cone volume is within 1% of (1/3)*pi*r^2*h");

    // Watertightness proof, same pattern as every other primitive here:
    // Manifold rejects a non-manifold mesh (an unclosed cap would leave a
    // hole) rather than silently returning a wrong-but-plausible answer.
    const auto box =
        Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
    const auto result = BooleanCombine(cone, box, BooleanOp::Union);
    Check(std::abs(result.Volume() - (cone.Volume() + 1.0)) < 1e-6,
          "union of the base-first flat-capped cone with a disjoint unit box "
          "equals cone volume + 1");
  }

  // Frustum: both ends off-axis and at different radii, so both get flat
  // disc caps. Exact volume: (pi*h/3)*(r1^2 + r1*r2 + r2^2).
  {
    const double r1 = 2.0;
    const double r2 = 5.0;
    const double height = 4.0;
    const std::vector<Point2d> profile = {Point2d(r1, 0.0), Point2d(r2, height)};
    const auto frustum = Mesh::RevolveProfile(profile, origin, up, 64);
    const double exact_volume =
        ON_PI * height * (r1 * r1 + r1 * r2 + r2 * r2) / 3.0;
    Check(std::abs(frustum.Volume() - exact_volume) / exact_volume < 0.01,
          "flat-double-capped frustum volume is within 1% of "
          "(pi*h/3)*(r1^2+r1*r2+r2^2)");
  }

  // Degenerate frustum with r1 == r2 is just a cylinder: cross-check
  // against Mesh::Cylinder()'s own (independently implemented) volume,
  // not just a closed form, since the two build caps completely
  // differently (ExtrudeCappedSolid()'s NURBS-surface-trimmed disc vs.
  // this end's plain center-vertex fan).
  {
    const double radius = 2.5;
    const double height = 6.0;
    const std::vector<Point2d> profile = {Point2d(radius, 0.0), Point2d(radius, height)};
    const auto via_revolve = Mesh::RevolveProfile(profile, origin, up, 64);
    const auto via_cylinder = Mesh::Cylinder(origin, up, radius, height, 64, 8);
    const double relative_diff =
        std::abs(via_revolve.Volume() - via_cylinder.Volume()) / via_cylinder.Volume();
    Check(relative_diff < 1e-3,
          "flat-double-capped cylinder-shaped revolve matches Mesh::Cylinder()'s "
          "volume to within 0.1%");
  }
}

void TestLoftClosedRingsSquareFrustumExactVolumeAndBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // A frustum between a small square (half-side 1, side 2, area 4) at
  // z=0 and a larger square (half-side 3, side 6, area 36) at z=3,
  // centered on and scaled uniformly about the same (0,0,z) axis - the
  // straight-line connection between corresponding vertices is then
  // exactly a frustum of a real pyramid (every lateral edge, extended,
  // meets at a single apex below z=0), not an approximation the way
  // Cylinder()/Cone()'s circular trims are. Both rings list vertices in
  // the same CCW-as-seen-from-ahead order (matching this file's
  // u_dir x v_dir = outward normal convention): (s,-s),(s,s),(-s,s),
  // (-s,-s) has positive standard-2D signed area for s > 0, i.e. is CCW
  // when viewed from +z looking down -z, per LoftClosedRings()'s own
  // documented convention.
  const std::vector<Point3d> bottom = {
      Point3d(1, -1, 0),
      Point3d(1, 1, 0),
      Point3d(-1, 1, 0),
      Point3d(-1, -1, 0),
  };
  const std::vector<Point3d> top = {
      Point3d(3, -3, 3),
      Point3d(3, 3, 3),
      Point3d(-3, 3, 3),
      Point3d(-3, -3, 3),
  };
  const auto frustum = Mesh::LoftClosedRings({bottom, top});

  // Exact frustum-of-a-pyramid volume formula: (h/3)*(A1+A2+sqrt(A1*A2)).
  const double height = 3.0;
  const double area1 = 4.0;
  const double area2 = 36.0;
  const double exact_volume = (height / 3.0) * (area1 + area2 + std::sqrt(area1 * area2));
  Check(std::abs(exact_volume - 52.0) < 1e-9,
        "sanity: the hand-derived frustum formula itself evaluates to 52");
  Check(std::abs(frustum.Volume() - exact_volume) < 1e-9,
        "lofted square frustum's volume exactly matches the closed-form "
        "pyramid-frustum formula (straight edges between only 2 rings - no "
        "circular approximation involved, unlike Cylinder()/Cone())");

  // Real proof of watertightness, same as every other solid here: Manifold
  // would reject a non-manifold mesh outright rather than return a
  // plausible-looking wrong answer.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(frustum, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (frustum.Volume() + 1.0)) < 1e-9,
        "union of the lofted frustum with a disjoint unit box equals frustum "
        "volume + 1");
}

void TestLoftClosedRingsRejectsTooFewRingsAndMismatchedCounts() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  bool threw_too_few = false;
  try {
    const std::vector<Point3d> only_ring = {Point3d(0, 0, 0), Point3d(1, 0, 0),
                                             Point3d(0, 1, 0)};
    Mesh::LoftClosedRings({only_ring});
  } catch (const std::invalid_argument&) {
    threw_too_few = true;
  }
  Check(threw_too_few, "LoftClosedRings throws with fewer than 2 rings");

  bool threw_mismatched = false;
  try {
    const std::vector<Point3d> triangle = {Point3d(0, 0, 0), Point3d(1, 0, 0),
                                            Point3d(0, 1, 0)};
    const std::vector<Point3d> square = {Point3d(0, 0, 1), Point3d(1, 0, 1), Point3d(1, 1, 1),
                                          Point3d(0, 1, 1)};
    Mesh::LoftClosedRings({triangle, square});
  } catch (const std::invalid_argument&) {
    threw_mismatched = true;
  }
  Check(threw_mismatched,
        "LoftClosedRings throws when rings have different vertex counts "
        "rather than silently misaligning bands");

  bool threw_self_intersecting = false;
  try {
    // A bowtie quadrilateral (corners in crossed order), planar in z=0 -
    // same self-intersection shape as
    // TestExactClippingRejectsSelfIntersectingTrim, just as a 3D ring.
    const std::vector<Point3d> bowtie = {
        Point3d(0, 0, 0),
        Point3d(1, 1, 0),
        Point3d(1, 0, 0),
        Point3d(0, 1, 0),
    };
    const std::vector<Point3d> square = {Point3d(0, 0, 1), Point3d(1, 0, 1), Point3d(1, 1, 1),
                                          Point3d(0, 1, 1)};
    Mesh::LoftClosedRings({bowtie, square});
  } catch (const std::invalid_argument&) {
    threw_self_intersecting = true;
  }
  Check(threw_self_intersecting,
        "LoftClosedRings throws when the first ring is self-intersecting "
        "(a bowtie), since it can't be closed into a well-defined end cap");

  bool threw_non_planar = false;
  try {
    // Same square as above, but with one corner pulled well out of the
    // z=0 plane - relative to the ring's own ~1.4-unit diagonal, 0.3 is
    // far past the check's 1e-6-relative tolerance, not a borderline case.
    const std::vector<Point3d> warped_square = {Point3d(0, 0, 0), Point3d(1, 0, 0),
                                                 Point3d(1, 1, 0.3), Point3d(0, 1, 0)};
    const std::vector<Point3d> square = {Point3d(0, 0, 1), Point3d(1, 0, 1), Point3d(1, 1, 1),
                                          Point3d(0, 1, 1)};
    Mesh::LoftClosedRings({warped_square, square});
  } catch (const std::invalid_argument&) {
    threw_non_planar = true;
  }
  Check(threw_non_planar,
        "LoftClosedRings throws when the first ring is non-planar, since its "
        "cap triangulation (projected onto a single plane) isn't well-defined");
}

void TestLoftClosedRingsConcaveEndCapsExactPrismVolume() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // Concave dart cross-section (same shape/coordinates as the exact-clip
  // dart test - shoelace area 0.404), extruded straight up by 1 as two
  // identical rings 1 apart. Since both rings are congruent and simply
  // translated (not rotated or scaled), this is an exact prism regardless
  // of the cross-section's shape - convex or concave - so its volume must
  // equal area x height exactly. A real, hand-derivable check on the new
  // ear-clipping end caps' correctness on a concave ring, not just proof
  // that Manifold didn't reject the result (the earlier frustum test only
  // exercised a convex ring).
  const std::vector<Point3d> bottom = {
      Point3d(0.1, 0.1, 0), Point3d(0.9, 0.1, 0), Point3d(0.9, 0.9, 0),
      Point3d(0.52, 0.31, 0), Point3d(0.1, 0.9, 0),
  };
  std::vector<Point3d> top;
  for (const auto& p : bottom) {
    top.emplace_back(p.x, p.y, p.z + 1.0);
  }

  const auto prism = Mesh::LoftClosedRings({bottom, top});
  Check(std::abs(prism.Volume() - 0.404) < 1e-6,
        "lofting two identical concave dart rings 1 apart gives an exact "
        "prism whose volume matches the dart's shoelace area (0.404) times "
        "height (1)");

  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(prism, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (prism.Volume() + 1.0)) < 1e-9,
        "union of the concave-cross-section lofted prism with a disjoint "
        "unit box equals prism volume + 1");
}

void TestTorusVolumeAndBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const double major_radius = 3.0;
  const double minor_radius = 1.0;
  // A shape neither RevolveProfile() (profile must touch the axis) nor
  // any earlier primitive can build - a genuinely new case, not a
  // reparameterization of one already tested. Its winding was derived
  // independently (see Torus()'s own header comment), so this needs its
  // own real verification, not a "should be fine, it's similar to X."
  const auto torus = Mesh::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), major_radius, minor_radius,
                                  /*major_segments=*/48, /*minor_segments=*/32);

  const double exact_volume = 2.0 * ON_PI * ON_PI * major_radius * minor_radius * minor_radius;
  const double relative_error = std::abs(torus.Volume() - exact_volume) / exact_volume;
  Check(relative_error < 0.01,
        "torus volume is within 1% of the exact 2*pi^2*major_radius*minor_radius^2");

  // Real proof of watertightness, same standard as every other solid
  // here: Manifold would reject a non-manifold mesh outright rather than
  // return a plausible-looking wrong answer - a meaningful check for a
  // grid that wraps in both directions with no explicit end caps at all.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(torus, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (torus.Volume() + 1.0)) < 1e-6,
        "union of the torus with a disjoint unit box equals torus volume + 1");
}

void TestMeshGetBoundingBox() {
  using dino8::kernel::Mesh;

  // MakeQuadBoxMesh's 8 corners span exactly [x0,x1]x[y0,y1]x[z0,z1] - an
  // asymmetric box (different extents per axis, not a cube) so a bug
  // that mixed up which axis fed which component would be caught.
  const auto box = MakeQuadBoxMesh(1, -2, 0.5, 4, 3, 7.5);
  const auto bounds = box.GetBoundingBox();
  Check(bounds.min.x == 1.0 && bounds.min.y == -2.0 && bounds.min.z == 0.5,
        "GetBoundingBox's min corner matches the box's known low corner exactly");
  Check(bounds.max.x == 4.0 && bounds.max.y == 3.0 && bounds.max.z == 7.5,
        "GetBoundingBox's max corner matches the box's known high corner exactly");

  bool threw = false;
  try {
    const Mesh empty;
    empty.GetBoundingBox();
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw, "GetBoundingBox throws on a mesh with no vertices rather than "
               "returning a misleading all-zero box");
}

void TestMeshGetCentroid() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // Same asymmetric box as TestMeshGetBoundingBox (different extents per
  // axis) - its centroid is exactly the midpoint of each axis's extent,
  // by symmetry, giving a clean hand-derivable exact check.
  const auto box = MakeQuadBoxMesh(1, -2, 0.5, 4, 3, 7.5);
  const Point3d centroid = box.GetCentroid();
  Check(std::abs(centroid.x - 2.5) < 1e-9 && std::abs(centroid.y - 0.5) < 1e-9 &&
            std::abs(centroid.z - 4.0) < 1e-9,
        "GetCentroid of an asymmetric box is exactly its per-axis midpoint "
        "(2.5, 0.5, 4.0)");

  bool threw = false;
  try {
    const Mesh empty;
    empty.GetCentroid();
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw, "GetCentroid throws on a mesh with (near) zero volume rather "
               "than dividing by it");
}

void TestMeshTransform() {
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);

  // Translation: the bounding box should shift by exactly the offset,
  // volume unchanged.
  const auto translated =
      box.Transform(ON_Xform::TranslationTransformation(Vector3d(5, -3, 10)));
  const auto translated_bounds = translated.GetBoundingBox();
  Check(translated_bounds.min.x == 5.0 && translated_bounds.min.y == -3.0 &&
            translated_bounds.min.z == 10.0 && translated_bounds.max.x == 7.0 &&
            translated_bounds.max.y == -1.0 && translated_bounds.max.z == 12.0,
        "translating the box shifts its bounding box by exactly the offset");
  Check(std::abs(translated.Volume() - 8.0) < 1e-9,
        "translation doesn't change the box's volume");

  // Uniform scale by 2 about the origin: bounding box doubles, volume
  // scales by 2^3 = 8 exactly (both hand-derivable, not approximate).
  const auto scaled = box.Transform(ON_Xform::ScaleTransformation(Point3d(0, 0, 0), 2.0));
  const auto scaled_bounds = scaled.GetBoundingBox();
  Check(scaled_bounds.max.x == 4.0 && scaled_bounds.max.y == 4.0 && scaled_bounds.max.z == 4.0,
        "scaling the box by 2 about the origin doubles its bounding box");
  Check(std::abs(scaled.Volume() - 64.0) < 1e-9,
        "scaling the box by 2 multiplies its volume by 2^3 = 8, giving 64");

  // Rotation is volume-preserving regardless of angle/axis/center - a
  // real invariant, not a coincidence of this particular box.
  Vector3d rotation_axis(0.3, 0.6, 0.74162);
  rotation_axis.Unitize();
  ON_Xform rotation;
  rotation.Rotation(/*angle_radians=*/0.7, rotation_axis, Point3d(0.5, -1.0, 2.0));
  const auto rotated = box.Transform(rotation);
  Check(std::abs(rotated.Volume() - 8.0) < 1e-6,
        "rotating the box about an arbitrary axis/center preserves its volume");
}

void TestMeshFlipNormals() {
  using dino8::kernel::Mesh;

  // Mix of quad faces (MakeQuadBoxMesh) and triangle faces (MakeBox, a
  // pre-existing helper that triangulates each side) so both of
  // FlipNormals()'s branches (IsQuad() true/false) get exercised, not
  // just one.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 3, 4);
  const auto tri_box = MakeBox(0, 0, 0, 2, 3, 4);

  for (const auto& box : {quad_box, tri_box}) {
    const double original_volume = box.Volume();
    const double original_area = box.Area();
    const auto flipped = box.FlipNormals();

    // Reversing every face's winding flips which side Volume()'s
    // divergence-theorem sum treats as "outward" - the exact negative of
    // the original, not just "a different number."
    Check(std::abs(flipped.Volume() + original_volume) < 1e-9,
          "FlipNormals() exactly negates the mesh's volume");
    // Area doesn't care about winding direction, only magnitude - it
    // should be completely unaffected.
    Check(std::abs(flipped.Area() - original_area) < 1e-9,
          "FlipNormals() doesn't change the mesh's area");
    Check(flipped.VertexCount() == box.VertexCount() && flipped.FaceCount() == box.FaceCount(),
          "FlipNormals() doesn't add or remove vertices/faces");

    // An exact involution: flipping twice must reproduce the original
    // volume exactly (not just "close"), since it's the same vertex
    // indices reversed back to their original order.
    const auto double_flipped = flipped.FlipNormals();
    Check(double_flipped.Volume() == original_volume,
          "FlipNormals() applied twice exactly reproduces the original "
          "volume (an exact involution, not merely an equivalent mesh)");
  }
}

void TestMeshIsClosedManifold() {
  using dino8::kernel::Mesh;

  // Closed, well-formed meshes - both quad-faced and triangle-faced, so
  // both of IsClosedManifold()'s edge-extraction branches get exercised -
  // must report true.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  Check(quad_box.IsClosedManifold(), "a closed quad-faced box is a closed manifold");
  const auto tri_box = MakeBox(0, 0, 0, 2, 2, 2);
  Check(tri_box.IsClosedManifold(), "a closed triangle-faced box is a closed manifold");

  // Delete one face from an otherwise-closed box: its 4 edges now each
  // border only 1 face instead of 2 - a real hole, not a manifold defect
  // of a different kind, so this specifically exercises the "count != 2"
  // (boundary edge) rejection path.
  {
    Mesh open_box = quad_box;
    ON_Mesh& raw = open_box.raw();
    raw.m_F.Remove(0);
    Check(!open_box.IsClosedManifold(),
          "a box with one face removed (an open hole) is not a closed manifold");
  }

  // Reverse a single face's own winding (the same per-face reversal
  // FlipNormals() does, but applied to only one face instead of all of
  // them) rather than the whole mesh: every edge that face shares with a
  // neighbor now gets walked the same direction by both faces instead of
  // opposite directions - every edge still borders exactly 2 faces (still
  // "closed" by that count), so this specifically exercises the
  // orientation-consistency check, not the edge-count one.
  {
    Mesh inconsistent = quad_box;
    ON_Mesh& raw = inconsistent.raw();
    ON_MeshFace& f = raw.m_F[0];
    std::swap(f.vi[0], f.vi[3]);
    std::swap(f.vi[1], f.vi[2]);
    Check(!inconsistent.IsClosedManifold(),
          "a box with a single face's winding reversed (inconsistent "
          "orientation with its neighbors) is not a closed manifold, even "
          "though every edge still borders exactly 2 faces");
  }

  // Flipping *every* face's winding (FlipNormals(), not just one) keeps
  // every neighbor pair pointing opposite ways relative to each other,
  // same as before the flip - still a closed manifold, just globally
  // reversed (inside out), which IsClosedManifold() can't and shouldn't
  // distinguish from "right side out" (Volume()'s sign is what carries
  // that information).
  Check(quad_box.FlipNormals().IsClosedManifold(),
        "flipping every face's winding still leaves a closed manifold "
        "(globally inside-out, not orientation-inconsistent)");
}

void TestMeshContainsPoint() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // Both quad-faced and triangle-faced boxes, so both of
  // ContainsPoint()'s per-face branches (IsQuad() true/false) get
  // exercised, not just one.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  const auto tri_box = MakeBox(0, 0, 0, 2, 2, 2);
  // Off-center, off-diagonal coordinates throughout (never y == z == the
  // box's own mid-value): the box's +X/-X faces are each split into two
  // triangles along a diagonal that passes exactly through the face's
  // center, so a +X-direction ray from a point whose (y, z) sits exactly
  // at that center would hit precisely the shared edge between the two
  // triangles - the documented, unhandled degenerate case - rather than
  // cleanly testing the ordinary crossing-count logic this test means to
  // check.
  for (const auto& box : {quad_box, tri_box}) {
    Check(box.ContainsPoint(Point3d(0.7, 1.3, 0.9)), "a point inside the box is inside it");
    Check(box.ContainsPoint(Point3d(0.2, 0.15, 0.3)),
          "a point just inside a corner is inside the box");
    Check(!box.ContainsPoint(Point3d(3, 1.3, 0.9)),
          "a point clearly outside on the +X side is not inside the box");
    Check(!box.ContainsPoint(Point3d(-1, 1.3, 0.9)),
          "a point clearly outside on the -X side is not inside the box "
          "(exercises a ray that starts behind every face along +X, not "
          "just one that starts already past some of them)");
    Check(!box.ContainsPoint(Point3d(0.7, 1.3, 5)),
          "a point far outside along a different axis (+Z) is not inside the box");
  }

  // A shape with a genuine hole (not just a convex solid): a box with a
  // narrower box subtracted out its middle via a real boolean, so a point
  // in the hollowed-out cavity must read as outside despite being well
  // inside the *outer* box's own bounding box - a real test of the
  // ray-cast actually counting crossings through both the outer wall and
  // the inner cavity wall, not just "is this near the object."
  const auto outer = MakeBox(0, 0, 0, 4, 4, 4);
  const auto inner = MakeBox(1, 1, 1, 3, 3, 3);
  const auto hollow = BooleanCombine(outer, inner, BooleanOp::Difference);
  Check(hollow.ContainsPoint(Point3d(0.5, 1.7, 2.3)),
        "a point in the hollow box's solid wall is inside it");
  Check(!hollow.ContainsPoint(Point3d(2, 1.7, 2.3)),
        "a point in the hollow box's empty cavity is not inside it, even "
        "though it's well within the outer box's own bounding box");
  Check(!hollow.ContainsPoint(Point3d(10, 10, 10)),
        "a point far outside the hollow box entirely is not inside it");
}

void TestMeshClosestPoint() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // Both quad- and triangle-faced boxes, so both of ClosestPoint()'s
  // per-face branches get exercised.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  const auto tri_box = MakeBox(0, 0, 0, 2, 2, 2);
  for (const auto& box : {quad_box, tri_box}) {
    // Interior-face-region case: a point directly "above" the +Z face's
    // interior (not near any edge) projects straight down onto it -
    // hand-derivable exact.
    const Point3d above_face(1, 1, 5);
    const Point3d closest_to_face = box.ClosestPoint(above_face);
    Check(std::abs(closest_to_face.x - 1.0) < 1e-9 && std::abs(closest_to_face.y - 1.0) < 1e-9 &&
              std::abs(closest_to_face.z - 2.0) < 1e-9,
          "closest point to a query directly above a face's interior is "
          "exactly the straight-down projection onto that face");

    // Vertex-region case: a point beyond a corner's own "outward cone"
    // has that corner itself as its closest point, not anything on an
    // adjacent edge or face.
    const Point3d beyond_corner(5, 5, 5);
    const Point3d closest_to_corner = box.ClosestPoint(beyond_corner);
    Check(std::abs(closest_to_corner.x - 2.0) < 1e-9 &&
              std::abs(closest_to_corner.y - 2.0) < 1e-9 &&
              std::abs(closest_to_corner.z - 2.0) < 1e-9,
          "closest point to a query beyond a corner is exactly that "
          "corner (2,2,2)");

    // Edge-region case: a point beyond the midpoint of a top edge (both
    // faces meeting there are equally far, but nothing on either face's
    // interior is closer than the edge itself) has that edge's midpoint
    // as its closest point.
    const Point3d beyond_edge(1, 5, 5);
    const Point3d closest_to_edge = box.ClosestPoint(beyond_edge);
    Check(std::abs(closest_to_edge.x - 1.0) < 1e-9 && std::abs(closest_to_edge.y - 2.0) < 1e-9 &&
              std::abs(closest_to_edge.z - 2.0) < 1e-9,
          "closest point to a query beyond an edge's midpoint is exactly "
          "that point on the edge (1,2,2)");

    // Distance cross-check, independent of which exact point comes back:
    // a query point inside the box must have distance exactly 1 to its
    // closest point, since (1,1,1) is the box's own center and every
    // face is exactly 1 unit away - whichever face/point the algorithm
    // picks, the distance is a hand-derivable invariant even though the
    // specific closest point isn't unique here.
    const Point3d center(1, 1, 1);
    const Point3d closest_to_center = box.ClosestPoint(center);
    const double distance = (closest_to_center - center).Length();
    Check(std::abs(distance - 1.0) < 1e-9,
          "closest point to the box's own center is exactly 1 unit away "
          "(every face is equidistant from the center of a 2x2x2 cube)");
  }
}

void TestMeshSignedDistance() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);

  // Outside: positive, and exactly the same magnitude ClosestPoint()
  // would give directly - this is a combination of two already-verified
  // primitives, not independent new geometry math, so the cross-check is
  // against those, not a fresh hand derivation.
  const Point3d outside(1, 1, 5);
  Check(std::abs(box.SignedDistance(outside) - 3.0) < 1e-9,
        "a point 3 units above the box's +Z face has signed distance "
        "exactly +3.0");

  // Inside: negative, same magnitude as the nearest-face distance. Off-
  // center coordinates (not the box's own exact center, and not on the
  // +X face's diagonal split - see TestMeshContainsPoint's own comment
  // on that degeneracy, which SignedDistance() inherits via
  // ContainsPoint()): distances to the 6 faces are 0.7, 1.3, 1.3, 0.7,
  // 0.9, 1.1 - minimum 0.7, from the x=0 and y=2 faces (a tie).
  const Point3d inside(0.7, 1.3, 0.9);
  Check(std::abs(box.SignedDistance(inside) - (-0.7)) < 1e-9,
        "an interior point 0.7 units from its nearest face(s) has signed "
        "distance exactly -0.7 (negative, since it's inside)");

  // Sign flips exactly at the boundary between inside and outside for
  // points straddling a face along its own normal - not just "some
  // positive number outside, some negative number inside" but the same
  // magnitude decreasing to (near) zero as the query approaches the
  // surface from either side.
  Check(box.SignedDistance(Point3d(1, 1, 1.9)) < 0.0,
        "just inside the +Z face (z=1.9 of 2.0) is still negative");
  Check(box.SignedDistance(Point3d(1, 1, 2.1)) > 0.0,
        "just outside the +Z face (z=2.1 of 2.0) is positive");
}

void TestMeshAreaCountsBothQuadTriangles() {
  using dino8::kernel::Mesh;

  // A single flat 2x3 quad face (not two triangles): Area() previously
  // computed only the first triangle (vi[0],vi[1],vi[2]) and silently
  // ignored vi[3] entirely for a real (non-degenerate) quad, returning
  // exactly half the true area for a case like this one, where both
  // triangles have equal area (half of 6.0 = 3.0, not the true 6.0) -
  // found while building a SubD test that needed Area() to work
  // correctly on SubD::ToApproximateMesh()'s genuinely-quad output,
  // which no earlier test here exercised (every tessellator in this file
  // emits triangles only).
  Mesh quad;
  ON_Mesh& raw = quad.raw();
  raw.m_V.Append(ON_3fPoint(0, 0, 0));
  raw.m_V.Append(ON_3fPoint(3, 0, 0));
  raw.m_V.Append(ON_3fPoint(3, 2, 0));
  raw.m_V.Append(ON_3fPoint(0, 2, 0));
  ON_MeshFace face;
  face.vi[0] = 0;
  face.vi[1] = 1;
  face.vi[2] = 2;
  face.vi[3] = 3;
  raw.m_F.Append(face);

  Check(std::abs(quad.Area() - 6.0) < 1e-9,
        "Area() of a single 3x2 quad face is the true 6.0, not half of it "
        "(the bug: only the first of the quad's two triangles was counted)");
}

void TestSubDFromBoxSubdividesToExactCatmullClarkCounts() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::SubD;

  // 6-quad closed box (MakeQuadBoxMesh(), not the triangulated MakeBox()):
  // Catmull-Clark's vertex/face-count growth has a simple, hand-derivable
  // rule on an all-quad control net - a new vertex per old vertex, edge
  // midpoint, and face center (V_new = V+E+F), and every face splits into
  // (its side count) quads, so F_new = 4x once the mesh is all-quad
  // (true from level 1 on, and this box already starts all-quad).
  // Level 0: V=8, E=12, F=6 (Euler: 8-12+6=2, genus 0, checks out).
  // Level 1: V=8+12+6=26, F=6*4=24, E=2*F=48 for a closed all-quad mesh
  // (each of 4 edges shared by 2 faces) - 26-48+24=2, checks out.
  // Level 2: V=26+48+24=98, F=24*4=96.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  auto subd = SubD::FromControlMesh(quad_box);
  Check(subd.VertexCount() == 8 && subd.EdgeCount() == 12 && subd.FaceCount() == 6,
        "SubD box at level 0 (before any subdivision) has the cube's own "
        "exact topology counts (V=8, E=12, F=6)");
  subd.Subdivide(2);

  Check(subd.VertexCount() == 98,
        "SubD box after 2 global Catmull-Clark subdivisions has the "
        "hand-derived exact vertex count (98)");
  Check(subd.FaceCount() == 96,
        "SubD box after 2 global Catmull-Clark subdivisions has the "
        "hand-derived exact face count (96)");
  Check(subd.EdgeCount() == 192,
        "SubD box after 2 global Catmull-Clark subdivisions has the "
        "hand-derived exact edge count (192, matching this comment's own "
        "E=2*F rule for a closed all-quad mesh)");
  Check(subd.VertexCount() - subd.EdgeCount() + subd.FaceCount() == 2,
        "Euler's formula V - E + F = 2 holds for the subdivided box's "
        "own reported topology counts, confirming EdgeCount() reports "
        "real edge topology rather than some other count");

  const auto approx = subd.ToApproximateMesh();
  Check(approx.VertexCount() == 98 && approx.FaceCount() == 96,
        "ToApproximateMesh()'s control-net mesh matches the SubD's own "
        "vertex/face counts");

  // Catmull-Clark subdivision pulls a cube's limit surface substantially
  // inward - a cube's 8 corners are valence-3 extraordinary vertices,
  // which Catmull-Clark weights heavily toward the interior. Measured,
  // not guessed: probing levels 1 through 5 showed volume dropping
  // 8 -> 3.5 -> 2.80 -> 2.66 -> 2.63 -> 2.62, converging (not diverging
  // or going negative) toward roughly a third of the cube's volume - real
  // subdivision behavior, confirmed by the monotonic, stabilizing trend,
  // not a symptom of a winding or topology bug (which the volume/face/
  // vertex-count and Manifold checks around this one already rule out).
  const double volume = approx.Volume();
  Check(std::abs(volume - 2.802131075637103) < 1e-6,
        "subdivided box volume matches the measured level-2 Catmull-Clark "
        "value (a real, substantial shrink from the cube's volume of 8, "
        "not left flat)");

  // Real proof of watertightness, same standard as every other solid
  // here: Manifold would reject a non-manifold mesh outright rather than
  // return a plausible-looking wrong answer.
  const auto other_box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(approx, other_box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (volume + 1.0)) < 1e-6,
        "union of the subdivided SubD box with a disjoint unit box equals "
        "its volume + 1");
}

void TestSubDFromControlMeshRejectsEmptyMesh() {
  using dino8::kernel::Mesh;
  using dino8::kernel::SubD;

  const Mesh empty;
  bool threw = false;
  try {
    SubD::FromControlMesh(empty);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  Check(threw, "SubD::FromControlMesh throws on a mesh with no faces");
}

// Builds two 1x1 quads hinged along the segment from (0,0,0) to (1,0,0):
// quad A in the y=0 plane (extending in +z), quad B in the z=0 plane
// (extending in +y). Quad B's two hinge-edge vertices are separate array
// entries from quad A's, at the identical two locations - a genuine
// "mesh double edge" (ON_SubDFromMeshParameters::InteriorCreaseOption::
// AtMeshDoubleEdge's own definition), the construction
// SubD::FromControlMesh(mesh, /*crease_at_double_edges=*/true)'s own
// documentation asks for.
dino8::kernel::Mesh MakeHingedDoubleEdgeMesh() {
  dino8::kernel::Mesh hinge;
  ON_Mesh& raw = hinge.raw();
  raw.m_V.Append(ON_3fPoint(0, 0, 0));  // 0
  raw.m_V.Append(ON_3fPoint(1, 0, 0));  // 1
  raw.m_V.Append(ON_3fPoint(1, 0, 1));  // 2
  raw.m_V.Append(ON_3fPoint(0, 0, 1));  // 3
  raw.m_V.Append(ON_3fPoint(0, 0, 0));  // 4: duplicate of 0's location
  raw.m_V.Append(ON_3fPoint(1, 0, 0));  // 5: duplicate of 1's location
  raw.m_V.Append(ON_3fPoint(1, 1, 0));  // 6
  raw.m_V.Append(ON_3fPoint(0, 1, 0));  // 7
  auto add_quad = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a;
    f.vi[1] = b;
    f.vi[2] = c;
    f.vi[3] = d;
    raw.m_F.Append(f);
  };
  add_quad(0, 1, 2, 3);
  add_quad(4, 5, 6, 7);
  return hinge;
}

void TestSubDCreaseAtDoubleEdgeKeepsFoldStraight() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::SubD;

  const auto hinge = MakeHingedDoubleEdgeMesh();

  // Both faces' double-edge vertices (distinct indices, same locations)
  // still weld to the same 6 SubD vertices either way - the
  // crease_at_double_edges flag only changes that edge's tag (smooth vs.
  // creased), not whether the coincident points are recognized as one
  // topological vertex.
  auto smooth = SubD::FromControlMesh(hinge, /*crease_at_double_edges=*/false);
  auto creased = SubD::FromControlMesh(hinge, /*crease_at_double_edges=*/true);
  Check(smooth.VertexCount() == 6 && creased.VertexCount() == 6,
        "both the smooth and creased SubDs weld the double-edge's "
        "coincident-but-distinct-indexed vertices into 6 shared ones");

  // Confirmed by a debug run before finalizing, not assumed: an open
  // SubD's own boundary edges are themselves always creases (a standard
  // Catmull-Clark convention, not something crease_at_double_edges
  // controls) - both hinge quads' 6 outer boundary edges are creased
  // either way. The two quads share the fold as their only interior
  // edge (2 quads x 4 edges - 1 shared = 7 total edges), so
  // crease_at_double_edges only changes whether *that one* edge is
  // creased too: 6 creases (boundary only) without it, all 7 (boundary
  // + fold) with it.
  Check(smooth.EdgeCount() == 7 && creased.EdgeCount() == 7,
        "both SubDs have the same 7 total edges (2 quads sharing 1 "
        "interior fold edge) - crease_at_double_edges doesn't change "
        "the topology, only which edges are tagged as creases");
  Check(smooth.CreaseEdgeCount() == 6,
        "without crease_at_double_edges, only the mesh's 6 boundary "
        "edges are creases - the interior fold edge is smooth");
  Check(creased.CreaseEdgeCount() == 7,
        "with crease_at_double_edges, all 7 edges are creases - the "
        "same 6 boundary edges plus the now-creased interior fold edge");

  smooth.Subdivide(1);
  creased.Subdivide(1);

  // After one subdivision, the fold edge (0,0,0)-(1,0,0) gets a new
  // subdivision point at its midpoint. For a genuine crease, that point
  // must land exactly on the fold's original straight line - the same
  // "boundary/crease edges subdivide to stay exactly on their own line"
  // rule already verified for actual mesh boundaries. For a smooth edge,
  // Catmull-Clark instead pulls it toward the two adjacent faces' interior
  // (both faces here are perpendicular to each other), rounding the fold -
  // provably NOT landing on that same point.
  auto closest_to_fold_midpoint = [](const Mesh& mesh) {
    const Point3d target(0.5, 0, 0);
    double best_dist = 1e30;
    for (int i = 0; i < mesh.raw().m_V.Count(); ++i) {
      const double dist = (Point3d(mesh.raw().m_V[i]) - target).Length();
      best_dist = std::min(best_dist, dist);
    }
    return best_dist;
  };

  Check(closest_to_fold_midpoint(creased.ToApproximateMesh()) < 1e-6,
        "with crease_at_double_edges, the fold gets a real subdivision "
        "point exactly at its straight-line midpoint (0.5, 0, 0)");
  Check(closest_to_fold_midpoint(smooth.ToApproximateMesh()) > 0.05,
        "without it, the same edge is treated as smooth and its "
        "subdivision point is measurably pulled off that line instead - "
        "proving the crease flag does something real, not a no-op");
}

void TestSubDFlatQuadGridStaysFlatAndAreaExact() {
  using dino8::kernel::Mesh;
  using dino8::kernel::SubD;

  // A flat 2x2 grid of quads (3x3 vertices, (0,0,0) to (2,2,0), z=0
  // everywhere) - unlike the box, this control net has no extraordinary
  // *interior* vertex: its one interior vertex has valence 4 (regular for
  // a quad mesh). Worth verifying directly rather than assuming it
  // carries over from the box test, and the actual measured result is
  // more nuanced than a first guess: every vertex stays exactly on the
  // z=0 plane (regular-valence interior subdivision and a straight
  // boundary edge's own subdivision rule both keep points exactly
  // in-plane/on-line - verified below), but the *area* still measurably
  // shrinks (to 3.6875 from 4.0, not preserved) - the 4 boundary corners
  // are themselves a kind of extraordinary vertex (valence 2, not a
  // regular interior 4), and Catmull-Clark's smooth corner rule pulls
  // them inward along the boundary, the same qualitative effect that
  // shrank the box's volume, just far smaller here since only 4 vertices
  // are affected instead of every vertex neighboring one of 8 corners.
  Mesh grid;
  ON_Mesh& raw = grid.raw();
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      raw.m_V.Append(ON_3fPoint(static_cast<double>(i), static_cast<double>(j), 0.0));
    }
  }
  auto idx = [](int i, int j) { return i * 3 + j; };
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      ON_MeshFace face;
      face.vi[0] = idx(i, j);
      face.vi[1] = idx(i + 1, j);
      face.vi[2] = idx(i + 1, j + 1);
      face.vi[3] = idx(i, j + 1);
      raw.m_F.Append(face);
    }
  }

  Check(std::abs(grid.Area() - 4.0) < 1e-9,
        "sanity: the flat 2x2 quad grid's own area is exactly 4 before any subdivision");

  auto subd = SubD::FromControlMesh(grid);
  subd.Subdivide(2);
  const auto approx = subd.ToApproximateMesh();

  bool all_flat = true;
  for (int i = 0; i < approx.raw().m_V.Count(); ++i) {
    if (std::abs(static_cast<double>(approx.raw().m_V[i].z)) > 1e-6) {
      all_flat = false;
      break;
    }
  }
  Check(all_flat,
        "subdividing a flat, all-regular-valence quad grid keeps every vertex "
        "exactly on the z=0 plane (no shrinkage/warping the way the box's "
        "extraordinary corners caused)");
  Check(std::abs(approx.Area() - 3.6875) < 1e-6,
        "the flat grid's area matches the measured post-subdivision value "
        "(3.6875, not the naively-assumed exact 4) - corner-vertex shrinkage "
        "on a much smaller scale than the box's, not a bug");
}

void TestMeshComputeVertexNormals() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Vector3d;

  // MakeQuadBoxMesh's vertex 0 = (0,0,0) is shared by exactly 3 faces:
  // bottom (-z), front (-y), left (-x) - each a single unit-square quad,
  // so each contributes an equal-magnitude unit normal along its own
  // axis. Their area-weighted sum, normalized, is exactly
  // (-1,-1,-1)/sqrt(3) - a hand-derivable exact value, not just "some
  // vector that looks plausible."
  const auto box = MakeQuadBoxMesh(0, 0, 0, 1, 1, 1);
  const std::vector<Vector3d> normals = box.ComputeVertexNormals();
  Check(static_cast<int>(normals.size()) == box.VertexCount(),
        "ComputeVertexNormals returns exactly one normal per vertex");

  const double expected = -1.0 / std::sqrt(3.0);
  const Vector3d& n0 = normals[0];
  Check(std::abs(n0.x - expected) < 1e-9 && std::abs(n0.y - expected) < 1e-9 &&
            std::abs(n0.z - expected) < 1e-9,
        "vertex 0's normal is exactly (-1,-1,-1)/sqrt(3), the area-weighted "
        "average of its 3 adjacent unit-square faces' normals");
  Check(std::abs(n0.Length() - 1.0) < 1e-9, "vertex 0's normal is unit length");

  // A flat single quad: every corner's normal must equal the quad's own
  // single flat normal exactly - no neighbors to average against, so
  // area-weighting can't change anything here.
  Mesh flat;
  ON_Mesh& raw = flat.raw();
  raw.m_V.Append(ON_3fPoint(0, 0, 0));
  raw.m_V.Append(ON_3fPoint(1, 0, 0));
  raw.m_V.Append(ON_3fPoint(1, 1, 0));
  raw.m_V.Append(ON_3fPoint(0, 1, 0));
  ON_MeshFace face;
  face.vi[0] = 0;
  face.vi[1] = 1;
  face.vi[2] = 2;
  face.vi[3] = 3;
  raw.m_F.Append(face);
  const std::vector<Vector3d> flat_normals = flat.ComputeVertexNormals();
  for (const Vector3d& n : flat_normals) {
    Check(std::abs(n.x) < 1e-9 && std::abs(n.y) < 1e-9 && std::abs(n.z - 1.0) < 1e-9,
          "every corner of a single flat quad in the z=0 plane (CCW from "
          "+z) gets exactly the normal (0,0,1)");
  }
}

void TestMeshSaveObjRoundTrips() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Result;

  // MakeQuadBoxMesh (defined above): 8 vertices, 6 quad faces, known exact
  // corner coordinates - lets this test check actual written content
  // (not just line counts) against hand-known values.
  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  const std::string path = "dino8_kernel_mesh_obj_test.obj";
  Check(box.SaveObj(path) == Result::Ok, "Mesh::SaveObj succeeds");

  std::ifstream in(path);
  Check(static_cast<bool>(in), "the .obj file SaveObj wrote can be reopened for reading");

  int vertex_lines = 0;
  int normal_lines = 0;
  int face_lines = 0;
  bool saw_quad_face = false;
  bool saw_slash_slash_reference = false;
  double first_vertex[3] = {0, 0, 0};
  bool got_first_vertex = false;
  std::string line;
  while (std::getline(in, line)) {
    if (line.size() >= 2 && line[0] == 'v' && line[1] == ' ') {
      if (!got_first_vertex) {
        std::sscanf(line.c_str(), "v %lf %lf %lf", &first_vertex[0], &first_vertex[1],
                    &first_vertex[2]);
        got_first_vertex = true;
      }
      ++vertex_lines;
    } else if (line.size() >= 3 && line[0] == 'v' && line[1] == 'n' && line[2] == ' ') {
      ++normal_lines;
    } else if (line.size() >= 2 && line[0] == 'f' && line[1] == ' ') {
      ++face_lines;
      // Count whitespace-separated tokens after "f " to distinguish a
      // written quad (5 tokens: "f" + 4 indices) from a triangle (4).
      int token_count = 0;
      std::istringstream tokens(line);
      std::string token;
      while (tokens >> token) {
        ++token_count;
      }
      if (token_count == 5) {
        saw_quad_face = true;
      }
      if (line.find("//") != std::string::npos) {
        saw_slash_slash_reference = true;
      }
    }
  }

  Check(vertex_lines == box.VertexCount(),
        "the .obj file has exactly as many 'v' lines as the mesh has vertices (8)");
  Check(normal_lines == box.VertexCount(),
        "the .obj file has exactly as many 'vn' lines as the mesh has vertices (8)");
  Check(face_lines == box.FaceCount(),
        "the .obj file has exactly as many 'f' lines as the mesh has faces (6)");
  Check(saw_quad_face,
        "at least one face line has 4 indices - quad faces are written as one "
        "quad, not split into two triangles");
  Check(saw_slash_slash_reference,
        "face lines reference a normal via 'v//vn' form, not just bare "
        "vertex indices");
  Check(got_first_vertex && first_vertex[0] == 0.0 && first_vertex[1] == 0.0 &&
            first_vertex[2] == 0.0,
        "the first written vertex line matches MakeQuadBoxMesh's known first "
        "corner (0,0,0)");

  // Full round trip: LoadObj() the file SaveObj() just wrote and check the
  // result is geometrically the same solid, not just "some mesh with the
  // right counts" - same vertex/face counts AND the same exact volume
  // (quad faces preserved as quads, not reinterpreted as triangles, would
  // break Volume()'s IsQuad() handling if LoadObj() got that wrong).
  Mesh reloaded;
  Check(Mesh::LoadObj(path, reloaded) == Result::Ok, "Mesh::LoadObj succeeds on SaveObj()'s own output");
  Check(reloaded.VertexCount() == box.VertexCount() && reloaded.FaceCount() == box.FaceCount(),
        "the reloaded mesh has the same vertex/face counts as the original");
  Check(std::abs(reloaded.Volume() - box.Volume()) < 1e-9,
        "the reloaded mesh's volume exactly matches the original (quad faces "
        "round-tripped as quads, not silently reinterpreted)");
}

void TestMeshTextureCoordinates() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point2d;
  using dino8::kernel::Result;

  // Same 8-vertex box MakeQuadBoxMesh()'s own SaveObj() test uses.
  // Assigns each vertex a distinct, hand-known (u, v) so a full
  // SaveObj()/LoadObj() round trip can be checked against exact expected
  // values, not just "some texture coordinate came back".
  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  Mesh with_uvs = box;
  Check(!with_uvs.HasTextureCoordinates(),
        "a mesh has no texture coordinates until SetTextureCoordinates() "
        "is called");

  std::vector<Point2d> uvs;
  for (int i = 0; i < with_uvs.VertexCount(); ++i) {
    uvs.push_back(Point2d(static_cast<double>(i) * 0.1, static_cast<double>(i) * 0.2));
  }
  Check(with_uvs.SetTextureCoordinates(uvs) == Result::Ok, "SetTextureCoordinates succeeds");
  Check(with_uvs.HasTextureCoordinates(),
        "HasTextureCoordinates() is true once every vertex has one set");
  const Point2d uv3 = with_uvs.TextureCoordinateAt(3);
  Check(std::abs(uv3.x - 0.3) < 1e-12 && std::abs(uv3.y - 0.6) < 1e-12,
        "TextureCoordinateAt(3) returns exactly the (0.3, 0.6) just set");

  Check(with_uvs.SetTextureCoordinates({Point2d(0, 0)}) == Result::Failed,
        "SetTextureCoordinates fails when given the wrong number of "
        "entries (1 instead of the mesh's 8 vertices) rather than "
        "silently truncating or leaving the rest unset");

  // Confirmed by a debug run before finalizing: SaveObj() writes exactly
  // one 'vt' line per vertex and switches face lines to the 'v/vt/vn'
  // form (no bare '//' left), and LoadObj() reads that back into an
  // identical texture coordinate for every vertex.
  const std::string path = "dino8_kernel_mesh_obj_uv_test.obj";
  Check(with_uvs.SaveObj(path) == Result::Ok, "SaveObj succeeds on a mesh with texture coordinates");

  std::ifstream in(path);
  std::string line;
  int vt_lines = 0;
  bool saw_bare_double_slash = false;
  while (std::getline(in, line)) {
    if (line.size() >= 3 && line[0] == 'v' && line[1] == 't' && line[2] == ' ') {
      ++vt_lines;
    }
    if (line.size() >= 2 && line[0] == 'f' && line[1] == ' ' && line.find("//") != std::string::npos) {
      saw_bare_double_slash = true;
    }
  }
  Check(vt_lines == with_uvs.VertexCount(),
        "the .obj file has exactly one 'vt' line per vertex (8)");
  Check(!saw_bare_double_slash,
        "face lines use the full 'v/vt/vn' form, not the no-texture "
        "'v//vn' form, once the mesh has texture coordinates");

  Mesh reloaded;
  Check(Mesh::LoadObj(path, reloaded) == Result::Ok,
        "LoadObj succeeds on a .obj file with texture coordinates");
  Check(reloaded.HasTextureCoordinates(),
        "the reloaded mesh reports having texture coordinates");
  bool all_match = true;
  for (int i = 0; i < reloaded.VertexCount(); ++i) {
    const Point2d original = with_uvs.TextureCoordinateAt(i);
    const Point2d loaded_uv = reloaded.TextureCoordinateAt(i);
    if (std::abs(original.x - loaded_uv.x) > 1e-9 || std::abs(original.y - loaded_uv.y) > 1e-9) {
      all_match = false;
      break;
    }
  }
  Check(all_match,
        "every reloaded vertex's texture coordinate exactly matches what "
        "was originally set, round-tripped through the file");
  std::remove(path.c_str());

  // A file with only some vertices referenced via 'vt' (a legitimate,
  // if unusual, partial-coverage .obj) doesn't get texture coordinates
  // at all on load - ON_Mesh's own "all vertices or none" convention
  // (see HasTextureCoordinates()) has no way to represent partial
  // coverage, so it's discarded rather than guessed at.
  const std::string partial_path = "dino8_kernel_mesh_obj_uv_partial_test.obj";
  {
    std::ofstream out(partial_path);
    out << "v 0 0 0\nv 1 0 0\nv 1 1 0\n";
    out << "vt 0.1 0.2\n";
    // Only the first two corners reference a vt; the third doesn't.
    out << "f 1/1 2/1 3\n";
  }
  Mesh partial;
  Check(Mesh::LoadObj(partial_path, partial) == Result::Ok,
        "LoadObj still succeeds on a file with partial vt coverage");
  Check(!partial.HasTextureCoordinates(),
        "but the reloaded mesh reports no texture coordinates at all, "
        "since not every vertex got one");
  std::remove(partial_path.c_str());
}

void TestMeshLoadObjRejectsMalformedFiles() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Result;

  const std::string missing_path = "dino8_kernel_mesh_obj_test_does_not_exist.obj";
  Mesh out;
  Check(Mesh::LoadObj(missing_path, out) == Result::Failed,
        "LoadObj fails on a file that doesn't exist");

  const std::string forward_ref_path = "dino8_kernel_mesh_obj_test_forward_ref.obj";
  {
    std::ofstream bad(forward_ref_path);
    // References vertex 2 before it's ever defined - not a valid .obj.
    bad << "v 0 0 0\nf 1 2 3\n";
  }
  Check(Mesh::LoadObj(forward_ref_path, out) == Result::Failed,
        "LoadObj fails on a face referencing a vertex index that doesn't exist");

  const std::string pentagon_path = "dino8_kernel_mesh_obj_test_pentagon.obj";
  {
    std::ofstream bad(pentagon_path);
    bad << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0.5 2 0\nf 1 2 3 4 5\n";
  }
  Check(Mesh::LoadObj(pentagon_path, out) == Result::Failed,
        "LoadObj fails on a 5-index face line rather than silently "
        "misinterpreting it (ON_MeshFace only holds a triangle or quad)");

  std::remove(forward_ref_path.c_str());
  std::remove(pentagon_path.c_str());
}

void TestMeshSaveStlSplitsQuadsAndComputesNormals() {
  using dino8::kernel::Result;

  // MakeQuadBoxMesh: 6 quad faces. STL is triangle-only, so SaveStl must
  // split each quad into 2 triangles - 12 facets total, not 6 - and
  // compute a real per-facet normal (not the placeholder "0 0 0" the
  // format technically allows). The first face (bottom, quad
  // (0,3,2,1)) has known outward normal (0,0,-1), matching this file's
  // box-face-orientation convention used everywhere else (Box(), etc.).
  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  const std::string path = "dino8_kernel_mesh_stl_test.stl";
  Check(box.SaveStl(path) == Result::Ok, "Mesh::SaveStl succeeds");

  std::ifstream in(path);
  Check(static_cast<bool>(in), "the .stl file SaveStl wrote can be reopened for reading");

  int facet_count = 0;
  double first_normal[3] = {0, 0, 0};
  bool got_first_normal = false;
  std::string line;
  while (std::getline(in, line)) {
    if (line.compare(0, 12, "facet normal") == 0) {
      if (!got_first_normal) {
        std::sscanf(line.c_str(), "facet normal %lf %lf %lf", &first_normal[0], &first_normal[1],
                    &first_normal[2]);
        got_first_normal = true;
      }
      ++facet_count;
    }
  }

  Check(facet_count == box.FaceCount() * 2,
        "SaveStl splits each of the box's 6 quad faces into 2 triangle "
        "facets (12 total), not one facet per quad (which the format "
        "doesn't support)");
  Check(got_first_normal && std::abs(first_normal[0]) < 1e-6 && std::abs(first_normal[1]) < 1e-6 &&
            std::abs(first_normal[2] - (-1.0)) < 1e-6,
        "the bottom face's first facet has the correct computed outward "
        "normal (0,0,-1), not a placeholder");

  std::remove(path.c_str());
}

void TestMeshLoadStlRoundTrips() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Result;

  // SaveStl() splits each of MakeQuadBoxMesh's 6 quads into 2 triangles
  // (12 facets), each with its own 3 unshared vertices (36 raw vertices
  // total) - LoadStl() should read that back faithfully, not weld
  // anything, so the reloaded mesh's own vertex/face counts reflect the
  // file's actual structure rather than the original pre-export mesh's.
  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 3, 4);
  const std::string path = "dino8_kernel_mesh_stl_load_test.stl";
  Check(box.SaveStl(path) == Result::Ok, "Mesh::SaveStl succeeds");

  Mesh loaded;
  Check(Mesh::LoadStl(path, loaded) == Result::Ok, "Mesh::LoadStl succeeds on SaveStl()'s own output");
  Check(loaded.FaceCount() == box.FaceCount() * 2,
        "the loaded mesh has 12 triangle faces (2 per original quad), "
        "matching what SaveStl() actually wrote");
  Check(loaded.VertexCount() == loaded.FaceCount() * 3,
        "the loaded mesh has exactly 3 unshared vertices per facet (36 "
        "total) - STL's own 'no shared vertex list' structure, not "
        "deduplicated");
  Check(std::abs(loaded.Volume() - box.Volume()) < 1e-6,
        "the loaded mesh's volume exactly matches the original despite "
        "having unshared vertices - Volume() doesn't care about vertex "
        "sharing");

  // Welding it back with MergeAndWeld() should collapse the 36 unshared
  // vertices down to the original 8 unique corners, same as any other
  // independently-tessellated-then-welded mesh here.
  const auto welded = Mesh::MergeAndWeld({loaded});
  Check(welded.VertexCount() == 8,
        "welding the loaded mesh collapses its 36 unshared vertices back "
        "down to the box's 8 unique corners");

  std::remove(path.c_str());

  Mesh missing;
  Check(Mesh::LoadStl("dino8_kernel_mesh_stl_load_test_does_not_exist.stl", missing) ==
            Result::Failed,
        "LoadStl fails on a file that doesn't exist");

  const std::string malformed_path = "dino8_kernel_mesh_stl_load_test_malformed.stl";
  {
    std::ofstream out(malformed_path);
    out << "solid dino8\n";
    out << "facet normal 0 0 1\n";
    out << "outer loop\n";
    out << "vertex 0 0 0\n";
    out << "vertex 1 0 0\n";
    // Missing the third vertex - only 2 for this facet.
    out << "endloop\n";
    out << "endfacet\n";
    out << "endsolid dino8\n";
  }
  Mesh malformed;
  Check(Mesh::LoadStl(malformed_path, malformed) == Result::Failed,
        "LoadStl fails on a facet with fewer than 3 vertices rather than "
        "silently misinterpreting it");
  std::remove(malformed_path.c_str());
}

void TestMeshLoadStlBinary() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Result;

  // Hand-writes a minimal 2-triangle binary STL file byte-for-byte per
  // the format's own spec (80-byte header, little-endian uint32 triangle
  // count, then per-triangle: 3 floats normal (discarded by the reader),
  // 3x3 floats vertices, a 2-byte attribute count) - not produced via any
  // library, so this is a genuine test of LoadStl()'s own binary parsing
  // and its size-based binary/ASCII auto-detection, not a round-trip
  // through code under test on both ends.
  const std::string path = "dino8_kernel_mesh_stl_binary_test.stl";
  {
    std::ofstream out(path, std::ios::binary);
    char header[80] = {0};
    out.write(header, sizeof(header));
    const uint32_t triangle_count = 2;
    out.write(reinterpret_cast<const char*>(&triangle_count), sizeof(triangle_count));

    auto write_triangle = [&](float nx, float ny, float nz, float ax, float ay, float az,
                               float bx, float by, float bz, float cx, float cy, float cz) {
      const float normal[3] = {nx, ny, nz};
      out.write(reinterpret_cast<const char*>(normal), sizeof(normal));
      const float a[3] = {ax, ay, az};
      out.write(reinterpret_cast<const char*>(a), sizeof(a));
      const float b[3] = {bx, by, bz};
      out.write(reinterpret_cast<const char*>(b), sizeof(b));
      const float c[3] = {cx, cy, cz};
      out.write(reinterpret_cast<const char*>(c), sizeof(c));
      const uint16_t attribute_byte_count = 0;
      out.write(reinterpret_cast<const char*>(&attribute_byte_count),
                sizeof(attribute_byte_count));
    };
    // Two triangles forming the unit square [0,1]x[0,1] in the z=0 plane
    // (same diagonal split TestMeshFlipNormals()/others already use) -
    // total area exactly 1.0, hand-derivable.
    write_triangle(0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0);
    write_triangle(0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0);
  }

  Mesh loaded;
  Check(Mesh::LoadStl(path, loaded) == Result::Ok,
        "Mesh::LoadStl succeeds on a hand-written binary STL file");
  Check(loaded.FaceCount() == 2, "the loaded binary mesh has exactly the 2 written triangles");
  Check(loaded.VertexCount() == 6,
        "the loaded binary mesh has 3 unshared vertices per facet (6 "
        "total), same 'no shared vertex list' structure as the ASCII path");
  Check(std::abs(loaded.Area() - 1.0) < 1e-6,
        "the loaded binary mesh's own area is exactly 1.0, the unit "
        "square the hand-written triangles describe");
  std::remove(path.c_str());

  // A binary-STL-shaped header (80-byte header + uint32 count) whose
  // claimed triangle count doesn't match the file's actual remaining
  // size fails outright: it isn't a well-formed binary STL by the
  // size-based detection LoadStl() uses, and it also isn't valid ASCII
  // (no "solid"/"vertex"/"endfacet" tokens at all), so the ASCII
  // fallback parser finds nothing byte-for-byte matching those tokens
  // and returns an empty mesh rather than failing - documented here as
  // the real, narrower guarantee rather than assumed to fail outright.
  const std::string truncated_path = "dino8_kernel_mesh_stl_binary_truncated_test.stl";
  {
    std::ofstream out(truncated_path, std::ios::binary);
    char header[80] = {0};
    out.write(header, sizeof(header));
    const uint32_t triangle_count = 5;  // claims 5 triangles, writes 0
    out.write(reinterpret_cast<const char*>(&triangle_count), sizeof(triangle_count));
  }
  Mesh truncated;
  const Result truncated_result = Mesh::LoadStl(truncated_path, truncated);
  Check(truncated_result == Result::Ok && truncated.FaceCount() == 0,
        "a file whose header claims more binary triangles than it "
        "actually contains falls back to the ASCII parser (since its "
        "size doesn't match the binary formula), which finds no "
        "recognizable ASCII tokens in the raw header bytes and returns "
        "an empty mesh rather than crashing or misreading");
  std::remove(truncated_path.c_str());
}

void TestMeshSaveStlBinaryRoundTrips() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Result;

  // Same MakeQuadBoxMesh() SaveStl()'s own ASCII round-trip test uses:
  // 6 quads, 12 triangles once split, 36 unshared vertices, hand-known
  // volume. Writing it via SaveStlBinary() and reading it back through
  // Mesh::LoadStl() (the same reader TestMeshLoadStlBinary() already
  // proved against a hand-written file) exercises the writer and the
  // reader's own binary/ASCII auto-detection together on a real
  // (non-hand-written) binary file for the first time.
  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 3, 4);
  const std::string path = "dino8_kernel_mesh_stl_save_binary_test.stl";
  Check(box.SaveStlBinary(path) == Result::Ok, "Mesh::SaveStlBinary succeeds");

  // Verify the file's own exact byte size independently of LoadStl(),
  // since LoadStl()'s binary/ASCII detection itself depends on this
  // formula - checking it here directly (not just trusting a successful
  // round-trip) confirms SaveStlBinary() actually wrote the real binary
  // layout, not something that merely happens to parse.
  std::ifstream size_check(path, std::ios::binary | std::ios::ate);
  const std::streamoff file_size = size_check.tellg();
  const std::streamoff expected_size = 80 + 4 + static_cast<std::streamoff>(12) * 50;
  Check(file_size == expected_size,
        "the binary file's own exact size matches 80 + 4 + 12*50 bytes "
        "for its 12 triangles, the real binary STL layout, not merely "
        "something LoadStl() happens to accept");

  Mesh loaded;
  Check(Mesh::LoadStl(path, loaded) == Result::Ok,
        "Mesh::LoadStl succeeds on SaveStlBinary()'s own output, auto-"
        "detecting it as binary rather than falling back to ASCII");
  Check(loaded.FaceCount() == box.FaceCount() * 2,
        "the loaded binary mesh has 12 triangle faces (2 per original "
        "quad), matching what SaveStlBinary() actually wrote");
  Check(loaded.VertexCount() == loaded.FaceCount() * 3,
        "the loaded binary mesh has exactly 3 unshared vertices per "
        "facet (36 total), STL's own structure round-tripped through "
        "the binary path");
  Check(std::abs(loaded.Volume() - box.Volume()) < 1e-6,
        "the loaded binary mesh's volume exactly matches the original");
  std::remove(path.c_str());
}

void TestExactClippingMatchesAreaButNotCellCounts() {
  using dino8::kernel::Brep;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;

  // Same 10x10 surface and same [0.15,0.85]^2 trim as
  // TestBrepTrimmedPlanarFace, but with exact_clip=true. The TRUE trim
  // area is (0.85-0.15)^2 * 100 = 49 - that's what exact clipping should
  // measure. Whole-cell trimming's exact area of 36 (see the other test)
  // is a different, smaller number: it only ever keeps cells fully
  // inside the nominal boundary, so its output is really the retained
  // *grid-snapped* sub-square [0.2,0.8]^2, not the true [0.15,0.85]^2
  // trim - 36 was that algorithm's systematic under-count landing on a
  // clean number by construction, not the actual trim area. Getting 49
  // here (not 36) is exact clipping's whole point.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 10, 0),
      Point3d(10, 0, 0),
      Point3d(10, 10, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const std::vector<Point2d> trim_loop = {
      Point2d(0.15, 0.15),
      Point2d(0.85, 0.15),
      Point2d(0.85, 0.85),
      Point2d(0.15, 0.85),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, trim_loop, /*exact_clip=*/true);
  const auto mesh = face.Tessellate(/*u_divisions=*/10, /*v_divisions=*/10).front();

  Check(std::abs(mesh.Area() - 49.0) < 1e-9,
        "exact-clipped area matches the true trim area (49), not whole-cell's 36");
  Check(mesh.VertexCount() != 49 || mesh.FaceCount() != 72,
        "exact clipping's vertex/triangle counts differ from whole-cell's "
        "(boundary cells are clipped, not dropped or kept whole)");
}

void TestExactClippingHandlesNonConvexTrim() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // A unit square surface: P(u, v) = (u, v, 0) exactly (bilinear identity
  // for these control points), so a trim polygon's area in (u, v) is
  // exactly the tessellated face's area in 3D too.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);

  // A dart/arrowhead shape - concave at (0.52, 0.31). Every vertex is
  // deliberately off the 8-division grid's lines (multiples of 0.125,
  // i.e. 0, 0.125, 0.25, ...) - an earlier version of this test used
  // (0.5, 0.3), and 0.5 sits exactly on a grid line, which corrupted the
  // clipped boundary (a real alignment edge case, caught by
  // ExtrudeCappedSolid's own boundary validation rather than silently
  // producing broken geometry). Exact area by the shoelace formula: 0.404
  // (not a whole-cell approximation - hand-derived independently of the
  // tessellator).
  const std::vector<Point2d> non_convex_trim = {
      Point2d(0.1, 0.1),
      Point2d(0.9, 0.1),
      Point2d(0.9, 0.9),
      Point2d(0.52, 0.31),
      Point2d(0.1, 0.9),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, non_convex_trim, /*exact_clip=*/true);
  const auto mesh = face.Tessellate(/*u_divisions=*/8, /*v_divisions=*/8).front();

  // Tolerance is 1e-6, not this file's usual 1e-9: unlike the other exact-
  // area tests here, 0.52/0.31 aren't exactly representable in binary
  // floating point (the other tests' trim coordinates - 0.15, 0.85, 10,
  // etc. - are), so ON_Mesh's single-precision vertex storage (ON_3fPoint)
  // introduces real, expected rounding at that scale - not an algorithm
  // defect.
  Check(std::abs(mesh.Area() - 0.404) < 1e-6,
        "exact clipping (Greiner-Hormann + ear-clipping) measures the "
        "dart's true concave area instead of rejecting it");

  // Prove the per-cell triangulation is actually valid geometry - not
  // just a coincidentally-correct area sum - by extruding it and
  // requiring both ExtrudeCappedSolid's own boundary-loop validation and
  // Manifold's independent watertightness check to accept the result.
  // Extrude into -Z, away from the cap's own +Z normal (u_dir x v_dir for
  // this CCW-in-(u,v), identity-mapped surface) - same convention
  // TestExtrudeUntrimmedFaceIntoSolid documents: the offset must point
  // away from the cap's own outward normal, or the resulting solid comes
  // out consistently wound "inside out" (still a valid closed manifold,
  // which is why Manifold still accepts it below, but with negated
  // volume).
  const auto solid = Mesh::ExtrudeCappedSolid(mesh, Vector3d(0, 0, -1));
  Check(std::abs(solid.Volume() - 0.404) < 1e-6,
        "the dart-shaped solid's volume equals its cap area times unit height");

  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(solid, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (solid.Volume() + 1.0)) < 1e-9,
        "Manifold accepts the concave-trim solid as watertight: union with "
        "a disjoint unit box equals solid volume + 1");
}

void TestExactClippingHandlesTrimVertexOnGridLine() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // The exact case TestExactClippingHandlesNonConvexTrim's own comment
  // used to flag as broken and work around by moving off the grid: the
  // same dart shape, but with its reflex vertex's u coordinate (0.5)
  // exactly on one of the 8-division grid's own lines (multiples of
  // 0.125). ClipPolygon's crossing detection now nudges a trim vertex off
  // an exact grid line before clipping (see TessellateGridClippedExact's
  // own comment on why), so this should measure the dart's true area
  // instead of producing corrupted boundary geometry.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);

  // Shoelace area (hand-derived, independent of the tessellator): 0.40.
  const std::vector<Point2d> on_grid_line_trim = {
      Point2d(0.1, 0.1),
      Point2d(0.9, 0.1),
      Point2d(0.9, 0.9),
      Point2d(0.5, 0.3),  // u=0.5 is exactly on the 8-division grid's u=0.5 line
      Point2d(0.1, 0.9),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, on_grid_line_trim, /*exact_clip=*/true);
  const auto mesh = face.Tessellate(/*u_divisions=*/8, /*v_divisions=*/8).front();

  Check(std::abs(mesh.Area() - 0.40) < 1e-6,
        "exact clipping measures the true area (0.40) of a dart whose "
        "reflex vertex sits exactly on a tessellation grid line, instead "
        "of producing corrupted boundary geometry");

  const auto solid = Mesh::ExtrudeCappedSolid(mesh, Vector3d(0, 0, -1));
  Check(std::abs(solid.Volume() - 0.40) < 1e-6,
        "the on-grid-line dart's extruded solid volume equals its cap "
        "area times unit height");

  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(solid, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (solid.Volume() + 1.0)) < 1e-9,
        "Manifold accepts the on-grid-line dart solid as watertight: union "
        "with a disjoint unit box equals solid volume + 1");
}

void TestExactClippingHandlesManyReflexVertexComb() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // A genuinely pathological concave trim the README's own "what's still
  // not done" section flagged as unexercised: a "comb" with many reflex
  // vertices (2 per tooth, 12 total for 6 teeth), whose tooth/gap widths
  // are only a few tessellation cells wide - not just one dart's single
  // reflex vertex. Built parametrically (not one hand-typed vertex list)
  // so its area can be derived from the same tooth_count/tooth_width/
  // base_height/tooth_top values that generate the vertices, rather than
  // computed by hand off the coordinates and risking a transcription
  // error - the same "trust the formula, not arithmetic on hardcoded
  // numbers" approach the annulus test elsewhere in this file already
  // uses (outer area minus inner area, computed programmatically).
  const int tooth_count = 6;
  const double base_height = 0.15;
  const double tooth_top = 0.9;
  const double tooth_width = 0.09;
  const double total_tooth_width = tooth_count * tooth_width;
  const double gap_width = (1.0 - total_tooth_width) / (tooth_count + 1);

  std::vector<Point2d> comb = {Point2d(0.0, 0.0), Point2d(1.0, 0.0), Point2d(1.0, base_height)};
  double x = 1.0 - gap_width;
  for (int i = 0; i < tooth_count; ++i) {
    const double tooth_right = x;
    const double tooth_left = tooth_right - tooth_width;
    comb.push_back(Point2d(tooth_right, base_height));
    comb.push_back(Point2d(tooth_right, tooth_top));
    comb.push_back(Point2d(tooth_left, tooth_top));
    comb.push_back(Point2d(tooth_left, base_height));
    x = tooth_left - gap_width;
  }
  comb.push_back(Point2d(0.0, base_height));

  const double expected_area =
      base_height * 1.0 + static_cast<double>(tooth_count) * tooth_width * (tooth_top - base_height);

  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const Brep face = Brep::TrimmedPlanarFace(surface, comb, /*exact_clip=*/true);
  const auto mesh = face.Tessellate(/*u_divisions=*/32, /*v_divisions=*/32).front();

  Check(std::abs(mesh.Area() - expected_area) < 1e-6,
        "exact clipping measures a many-reflex-vertex comb's true area "
        "exactly, even with tooth/gap widths only a few tessellation "
        "cells wide");

  const auto solid = Mesh::ExtrudeCappedSolid(mesh, Vector3d(0, 0, -1));
  Check(std::abs(solid.Volume() - expected_area) < 1e-6,
        "the comb solid's volume equals its cap area times unit height");

  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(solid, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (solid.Volume() + 1.0)) < 1e-9,
        "Manifold accepts the comb solid as watertight even with its many "
        "reflex vertices and narrow teeth: union with a disjoint unit "
        "box equals solid volume + 1");
}

void TestExactClippingRejectsSelfIntersectingTrim() {
  using dino8::kernel::Brep;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;

  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);

  // A "bowtie" quadrilateral: listing the 4 corners of a square in
  // crossed order (0,0)->(1,1)->(1,0)->(0,1) makes edges 0 and 2 cross
  // through the middle - a self-intersecting "polygon" with no
  // well-defined inside, which TessellateGridClippedExact() now detects
  // via dino8::kernel::detail::IsSimplePolygon() and rejects outright,
  // rather than producing whatever ClipPolygon/ClipConvex happens to
  // compute against an ill-formed input.
  const std::vector<Point2d> bowtie_trim = {
      Point2d(0.1, 0.1),
      Point2d(0.9, 0.9),
      Point2d(0.9, 0.1),
      Point2d(0.1, 0.9),
  };

  bool threw = false;
  try {
    Brep::TrimmedPlanarFace(surface, bowtie_trim, /*exact_clip=*/true).Tessellate(8, 8);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw,
        "TessellateGridClippedExact throws on a self-intersecting (bowtie) "
        "trim_polygon instead of silently clipping against it");
}

void TestAnnulusFaceExtrudesToWatertightTube() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // Same 10x10 surface and outer [0.15,0.85]^2 trim as
  // TestBrepTrimmedPlanarFace (whole-cell area 36 there), now with a
  // hole at [0.35,0.65]^2 - also deliberately off grid lines (0.35/0.65
  // aren't multiples of 0.1) so there's no boundary-point ambiguity at
  // the hole either.
  //
  // Hand-derived (not measured after the fact): outer-inside grid values
  // are u,v in {0.2,...,0.8} (7 each, as before); hole-inside grid values
  // are u,v in {0.4,0.5,0.6} (3 each). A cell is dropped if EITHER its
  // outer-corner test fails OR any one of its 4 corners falls inside the
  // hole - which happens for exactly the (i,j) in {3,4,5,6}^2 cells (16
  // of them), since every such cell has a corner landing on a hole-inside
  // grid point in both u and v. Of the 36 outer-retained cells (i,j in
  // {2..7}), that leaves 36-16=20 retained cells (40 triangles), and
  // 49-9=40 retained vertices (7x7 outer grid points minus the 3x3 that
  // are also inside the hole). Physical area = 20 cells x (0.1*10)^2 = 20.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 10, 0),
      Point3d(10, 0, 0),
      Point3d(10, 10, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const std::vector<Point2d> outer_loop = {
      Point2d(0.15, 0.15),
      Point2d(0.85, 0.15),
      Point2d(0.85, 0.85),
      Point2d(0.15, 0.85),
  };
  const std::vector<Point2d> hole_loop = {
      Point2d(0.35, 0.35),
      Point2d(0.65, 0.35),
      Point2d(0.65, 0.65),
      Point2d(0.35, 0.65),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, outer_loop, /*exact_clip=*/false,
                                             {hole_loop});
  const auto cap = face.Tessellate(/*u_divisions=*/10, /*v_divisions=*/10).front();

  Check(cap.VertexCount() == 40, "annulus face keeps exactly the 40 outer-grid-minus-hole vertices");
  Check(cap.FaceCount() == 40, "annulus face keeps exactly the 20 retained cells (40 triangles)");
  Check(std::abs(cap.Area() - 20.0) < 1e-9, "annulus face's area matches the hand-derived 20 exactly");

  // ExtrudeCappedSolid()'s boundary-edge extraction was documented as
  // working on "any cap shape" via triangle adjacency alone, without
  // ever having been tried on a cap with TWO independent boundary loops
  // (outer + hole) - this is that test. If it silently only walled one
  // loop, the result wouldn't be closed and BooleanCombine() would throw.
  const double height = 2.0;
  const auto tube = Mesh::ExtrudeCappedSolid(cap, Vector3d(0, 0, -height));
  Check(std::abs(tube.Volume() - cap.Area() * height) < 1e-9,
        "extruded annulus tube's volume matches area x height exactly");

  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(tube, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (tube.Volume() + 1.0)) < 1e-9,
        "union of the extruded annulus tube with a disjoint unit box equals tube volume + 1 "
        "(both the outer and inner walls were genuinely closed)");
}

void TestExtrudeRejectsAlreadyClosedCap() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Vector3d;

  // MakeBox() (defined above) is already a closed, boundary-free mesh -
  // ExtrudeCappedSolid() has nothing to sweep into walls and should say
  // so rather than silently producing two disconnected shells.
  const auto box = MakeBox(0, 0, 0, 1, 1, 1);

  bool threw = false;
  try {
    Mesh::ExtrudeCappedSolid(box, Vector3d(0, 0, 1));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw, "ExtrudeCappedSolid throws on a cap with no boundary (already closed)");
}

void TestExtrudeRejectsBowtieBoundary() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Vector3d;

  // Two triangles sharing exactly one vertex (index 0) and no edges - a
  // "bowtie": two boundary loops that touch at a single point rather than
  // being disjoint. Vertex 0 ends up with two outgoing and two incoming
  // boundary edges, which ExtrudeCappedSolid's validation should reject
  // rather than emit overlapping wall geometry through that shared point.
  Mesh bowtie;
  ON_Mesh& raw = bowtie.raw();
  raw.m_V.Append(ON_3fPoint(0, 0, 0));   // 0: shared vertex
  raw.m_V.Append(ON_3fPoint(1, 0, 0));   // 1
  raw.m_V.Append(ON_3fPoint(0, 1, 0));   // 2
  raw.m_V.Append(ON_3fPoint(-1, 0, 0));  // 3
  raw.m_V.Append(ON_3fPoint(0, -1, 0));  // 4

  auto add_tri = [&raw](int a, int b, int c) {
    ON_MeshFace face;
    face.vi[0] = a;
    face.vi[1] = b;
    face.vi[2] = c;
    face.vi[3] = c;
    raw.m_F.Append(face);
  };
  add_tri(0, 1, 2);
  add_tri(0, 3, 4);

  bool threw = false;
  try {
    Mesh::ExtrudeCappedSolid(bowtie, Vector3d(0, 0, 1));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw,
        "ExtrudeCappedSolid throws on a bowtie boundary (a vertex with more "
        "than one boundary edge) instead of emitting broken wall geometry");
}

void TestConeToApexSharesBoundaryValidation() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // ConeToApex() shares ExtrudeCappedSolid()'s boundary-edge extraction
  // and validation (Mesh::ExtractValidatedBoundaryEdges) rather than
  // duplicating it - this is a check on that wiring, not a re-test of the
  // validation logic itself (already covered by
  // TestExtrudeRejectsAlreadyClosedCap/BowtieBoundary above): an
  // already-closed cap (MakeBox()) has nothing to cone to an apex either.
  const auto box = MakeBox(0, 0, 0, 1, 1, 1);
  bool threw = false;
  try {
    Mesh::ConeToApex(box, Point3d(0.5, 0.5, 2));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw, "ConeToApex throws on a cap with no boundary (already closed), "
               "same validation as ExtrudeCappedSolid");
}

}  // namespace

int main() {
  ON::Begin();

  TestCurveDegreeElevation();
  TestCurveLength();
  TestCurveParameterAtArcLength();
  TestCurveTangentAt();
  TestCurveGetTightBoundingBox();
  TestCurveIsClosed();
  TestCurveIsPlanar();
  TestCurveIsLinear();
  TestCurveIsArcAndIsCircle();
  TestCurveReverse();
  TestCurveTrim();
  TestCurveSplit();
  TestCurveExtend();
  TestCurveClosestPoint();
  TestCurveCurvature();
  TestCurveSuggestedSamples();
  TestCurveSuggestedParameterValues();
  TestSurfaceNormalAt();
  TestSurfaceDegreeElevation();
  TestSurfaceIsClosed();
  TestSurfaceIsPlanar();
  TestSurfaceIsSphere();
  TestSurfaceIsCylinder();
  TestSurfaceIsCone();
  TestSurfaceIsTorus();
  TestSurfaceGetApproximateSize();
  TestSurfaceTessellateGridNonUniform();
  TestSurfaceSuggestedParameterValuesAndTessellateGridNonUniformAdaptive();
  TestSurfaceReverseAndTranspose();
  TestSurfaceTrim();
  TestSurfaceSplit();
  TestSurfaceExtend();
  TestSurfaceClosestPoint();
  TestSurfaceCurvature();
  TestSurfaceSuggestedDivisions();
  TestSurfaceTessellateGridAdaptive();
  TestSurfaceTessellateGridClippedExactAdaptive();
  TestBrepTessellateAdaptive();
  TestBrepTessellateNonUniformAdaptive();
  TestFileRoundTrip();
  TestModelAddMeshRoundTrips();
  TestModelAddSubDRoundTrips();
  TestSplitByPlane();
  TestConvexHull();
  TestSimplify();
  TestMinkowskiSum();
  TestDecompose();
  TestMinGap();
  TestRefineToLength();
  TestSmoothAndRefine();
  TestCountDegenerateTriangles();
  TestBrepTessellation();
  TestBoxVolume();
  TestBooleanUnion();
  TestBooleanIntersection();
  TestBooleanDifference();
  TestBooleanSymmetricDifference();
  TestBrepBoxIsClosedAndWatertight();
  TestBrepLacksFullOpenNurbsTopologyButStillUsable();
  TestBrepGetTightBoundingBox();
  TestBrepBooleanEndToEnd();
  TestBrepSphereIsClosedAndWatertight();
  TestBrepSphereBooleanEndToEnd();
  TestBrepTrimmedPlanarFace();
  TestWeldAcrossIndependentlyParameterizedSurfaces();
  TestExtrudeUntrimmedFaceIntoSolid();
  TestExtrudeTrimmedFaceFeedsBoolean();
  TestCylinderVolumeAndBoolean();
  TestConeVolumeAndBoolean();
  TestRevolveProfileBiconeVolumeAndBoolean();
  TestRevolveProfileRejectsTooShortProfile();
  TestRevolveProfileFlatEndCaps();
  TestLoftClosedRingsSquareFrustumExactVolumeAndBoolean();
  TestLoftClosedRingsRejectsTooFewRingsAndMismatchedCounts();
  TestLoftClosedRingsConcaveEndCapsExactPrismVolume();
  TestTorusVolumeAndBoolean();
  TestMeshGetBoundingBox();
  TestMeshGetCentroid();
  TestMeshTransform();
  TestMeshFlipNormals();
  TestMeshIsClosedManifold();
  TestMeshContainsPoint();
  TestMeshClosestPoint();
  TestMeshSignedDistance();
  TestMeshAreaCountsBothQuadTriangles();
  TestSubDFromBoxSubdividesToExactCatmullClarkCounts();
  TestSubDFromControlMeshRejectsEmptyMesh();
  TestSubDCreaseAtDoubleEdgeKeepsFoldStraight();
  TestSubDFlatQuadGridStaysFlatAndAreaExact();
  TestMeshComputeVertexNormals();
  TestMeshSaveObjRoundTrips();
  TestMeshTextureCoordinates();
  TestMeshLoadObjRejectsMalformedFiles();
  TestMeshSaveStlSplitsQuadsAndComputesNormals();
  TestMeshLoadStlRoundTrips();
  TestMeshLoadStlBinary();
  TestMeshSaveStlBinaryRoundTrips();
  TestExactClippingMatchesAreaButNotCellCounts();
  TestExactClippingHandlesNonConvexTrim();
  TestExactClippingHandlesTrimVertexOnGridLine();
  TestExactClippingHandlesManyReflexVertexComb();
  TestExactClippingRejectsSelfIntersectingTrim();
  TestAnnulusFaceExtrudesToWatertightTube();
  TestExtrudeRejectsAlreadyClosedCap();
  TestExtrudeRejectsBowtieBoundary();
  TestConeToApexSharesBoundaryValidation();

  ON::End();

  if (g_failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}
