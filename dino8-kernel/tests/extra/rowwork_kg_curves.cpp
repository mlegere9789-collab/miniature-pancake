// Register rows of the kernel-geometry factory (CURVE domain), checked against oracles that do not come from the code
// under test: an analytic circle (ON_Circle frame and closed-form arc lengths), the Bernstein form of a cubic Bezier,
// a reversed control-point list built by hand, the Moebius map written out, and polynomial reproduction of global
// interpolation. Every test names the Register row it supports; none of them marks a row verified.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/curve_interpolate.h"
#include "test_registry.h"

namespace {

using dino8::kernel::CurveInterpolationOptions;
using dino8::kernel::CurveInterpolationReport;
using dino8::kernel::InterpolationParameter;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

// A circle of radius r about c in a plane parallel to XY, as openNURBS' own rational quadratic form: counter-clockwise, domain
// from angle 0 to 2 pi, the four quarter points sit at the knots. The oracle for every point is the closed form
// c + r (cos a, sin a, 0), not this curve.
NurbsCurve CircleXY(const Point3d& c, double r) {
  const ON_Circle circle(ON_Plane(c, ON_3dVector(0, 0, 1)), r);
  ON_NurbsCurve nurbs;
  circle.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

Point3d OnCircle(const Point3d& c, double r, double angle) { return P(c.x + r * std::cos(angle), c.y + r * std::sin(angle), c.z); }

double At(const NurbsCurve& k, double fraction) {
  const auto d = k.Domain();
  return d.min + fraction * (d.max - d.min);
}

bool Near(const Point3d& a, const Point3d& b, double tol) { return a.DistanceTo(b) <= tol; }

}  // namespace

// CURVE-0020 CrvSeam: the seam of a closed curve moves to the picked place and the shape stays the circle.
D8_EXTRA_TEST(RowCrvSeamMovesTheSeamOfAClosedCircleAndKeepsEveryPointOnIt) {
  const Point3d c = P(1.0, 2.0, 3.0);
  const double r = 3.0;
  // (fraction of the domain, angle of that parameter): the quarter knots and a parameter strictly inside a span.
  for (double f : {0.25, 0.5, 0.75, 0.125}) {
    NurbsCurve k = CircleXY(c, r);
    const double t = At(k, f);
    const Result res = k.ChangeSeam(t);
    d8test::Check(res == Result::Ok, "ChangeSeam on a closed circle succeeds");
    d8test::Check(k.IsClosed(), "the circle is still closed after the seam move");
    const double angle = 2.0 * kPi * f;  // the circle's domain is the angle 0 .. 2 pi, uniform in the angle at the quarter knots
    if (f == 0.25 || f == 0.5 || f == 0.75) {
      const Point3d expect = OnCircle(c, r, angle);
      d8test::Check(Near(k.PointAt(k.Domain().min), expect, 1e-9), Msg("new seam is the analytic circle point at the picked angle", k.PointAt(k.Domain().min).DistanceTo(expect)).c_str());
      d8test::Check(Near(k.PointAt(k.Domain().max), expect, 1e-9), "the end point coincides with the new start point");
    }
    double worst = 0.0;
    for (int i = 0; i <= 200; ++i) {
      const Point3d q = k.PointAt(At(k, i / 200.0));
      worst = std::max(worst, std::fabs(std::hypot(q.x - c.x, q.y - c.y) - r));
      worst = std::max(worst, std::fabs(q.z - c.z));
    }
    d8test::Check(worst < 1e-9, Msg("every sample is at the exact radius in the exact plane", worst).c_str());
    d8test::Check(std::fabs(k.LengthToTolerance(1e-9) - 2.0 * kPi * r) < 1e-7, "the circumference is unchanged, 2 pi r");
  }
}

// CURVE-0055 Flip: the reversed curve is the same locus run the other way: C_rev(f) = C(1 - f). The oracle reverses the
// control-point list by hand into a fresh curve.
D8_EXTRA_TEST(RowFlipReversesACubicToTheHandReversedControlPolygon) {
  const std::vector<Point3d> cps = {P(0, 0, 0), P(1, 3, 0.5), P(2.5, -1, 1), P(4, 2, 0), P(6, 0.5, -1), P(7, 3, 2)};
  NurbsCurve flipped = NurbsCurve::FromControlPoints(cps, 3);
  const NurbsCurve original = NurbsCurve::FromControlPoints(cps, 3);
  d8test::Check(flipped.Reverse() == Result::Ok, "Reverse succeeds");
  std::vector<Point3d> rev(cps.rbegin(), cps.rend());
  const NurbsCurve by_hand = NurbsCurve::FromControlPoints(rev, 3);
  double worst = 0.0, worst_vs_original = 0.0;
  for (int i = 0; i <= 100; ++i) {
    const double f = i / 100.0;
    worst = std::max(worst, flipped.PointAt(At(flipped, f)).DistanceTo(by_hand.PointAt(At(by_hand, f))));
    worst_vs_original = std::max(worst_vs_original, flipped.PointAt(At(flipped, f)).DistanceTo(original.PointAt(At(original, 1.0 - f))));
  }
  d8test::Check(worst < 1e-12, Msg("reversed curve equals the curve of the hand-reversed control polygon", worst).c_str());
  d8test::Check(worst_vs_original < 1e-12, Msg("flipped(f) equals original(1 - f)", worst_vs_original).c_str());
  d8test::Check(Near(flipped.PointAt(flipped.Domain().min), cps.back(), 1e-12) && Near(flipped.PointAt(flipped.Domain().max), cps.front(), 1e-12),
                "start and end are swapped");
  // The direction of travel flips: a circle's tangent at the same point is negated.
  const NurbsCurve circle = CircleXY(P(0, 0, 0), 2.0);
  NurbsCurve rc = circle;
  rc.Reverse();
  const Vector3d t0 = circle.TangentAt(At(circle, 0.25));
  const Vector3d t1 = rc.TangentAt(At(rc, 0.75));  // the same point (0, 2, 0) of the circle
  d8test::Check(Near(circle.PointAt(At(circle, 0.25)), rc.PointAt(At(rc, 0.75)), 1e-9), "the quarter point is the same point on the reversed circle");
  d8test::Check(ON_DotProduct(t0, t1) < -0.999999, "the unit tangent at that point is negated by the flip");
}

// CURVE-0116 SubCrv: keeping the part between two points leaves the exact sub-arc: the half circle between the quarter points.
D8_EXTRA_TEST(RowSubCrvKeepsTheExactHalfCircleBetweenTwoPoints) {
  const Point3d c = P(-2.0, 1.0, 0.5);
  const double r = 4.0;
  NurbsCurve k = CircleXY(c, r);
  const double t0 = At(k, 0.25), t1 = At(k, 0.75);
  d8test::Check(k.Trim(t0, t1) == Result::Ok, "Trim to the quarter points succeeds");
  d8test::Check(std::fabs(k.Domain().min - t0) < 1e-12 && std::fabs(k.Domain().max - t1) < 1e-12, "the new domain is exactly the kept interval");
  d8test::Check(Near(k.PointAt(k.Domain().min), OnCircle(c, r, kPi / 2), 1e-9), "start is the analytic point at 90 degrees");
  d8test::Check(Near(k.PointAt(k.Domain().max), OnCircle(c, r, 1.5 * kPi), 1e-9), "end is the analytic point at 270 degrees");
  d8test::Check(!k.IsClosed(), "the half circle is open");
  const double len = k.LengthToTolerance(1e-9);
  d8test::Check(std::fabs(len - kPi * r) < 1e-7, Msg("length of the kept arc is pi r", len - kPi * r).c_str());
  double worst = 0.0;
  for (int i = 0; i <= 100; ++i) {
    const Point3d q = k.PointAt(At(k, i / 100.0));
    worst = std::max(worst, std::fabs(std::hypot(q.x - c.x, q.y - c.y) - r));
    // The kept half is the one through angle 180 degrees: every point has x <= centre.
    worst = std::max(worst, std::max(0.0, q.x - c.x - 1e-9));
  }
  d8test::Check(worst < 1e-9, Msg("kept points lie on the circle and on the 90..270 degree side", worst).c_str());
}

// CURVE-0028 DeleteSubCrv: removing the span between two places leaves the two outer pieces intact.
D8_EXTRA_TEST(RowDeleteSubCrvLeavesTheTwoOuterPiecesOfTheCircleIntact) {
  const Point3d c = P(0.0, 0.0, 0.0);
  const double r = 2.5;
  const NurbsCurve k = CircleXY(c, r);
  NurbsCurve head, rest, middle, tail;
  d8test::Check(k.Split(At(k, 0.25), head, rest) == Result::Ok, "first split");
  d8test::Check(rest.Split(At(k, 0.75), middle, tail) == Result::Ok, "second split, in the right piece");
  const double lh = head.LengthToTolerance(1e-9), lm = middle.LengthToTolerance(1e-9), lt = tail.LengthToTolerance(1e-9);
  d8test::Check(std::fabs(lh - kPi * r / 2) < 1e-7, "head is a quarter circle, pi r / 2");
  d8test::Check(std::fabs(lm - kPi * r) < 1e-7, "the deleted middle is a half circle, pi r");
  d8test::Check(std::fabs(lt - kPi * r / 2) < 1e-7, "tail is a quarter circle, pi r / 2");
  d8test::Check(std::fabs(lh + lt - kPi * r) < 1e-7, "what remains has the closed-form length 2 pi r minus pi r");
  d8test::Check(Near(head.PointAt(head.Domain().min), OnCircle(c, r, 0), 1e-9) && Near(head.PointAt(head.Domain().max), OnCircle(c, r, kPi / 2), 1e-9),
                "head runs from 0 to 90 degrees");
  d8test::Check(Near(tail.PointAt(tail.Domain().min), OnCircle(c, r, 1.5 * kPi), 1e-9) && Near(tail.PointAt(tail.Domain().max), OnCircle(c, r, 2 * kPi), 1e-9),
                "tail runs from 270 to 360 degrees");
  d8test::Check(Near(middle.PointAt(middle.Domain().min), head.PointAt(head.Domain().max), 1e-12), "the pieces share the cut points exactly");
}

// CURVE-0061 InsertControlPoint: one more control point, same curve. Oracle: the cubic Bezier in Bernstein form.
D8_EXTRA_TEST(RowInsertControlPointKeepsACubicBezierEqualToItsBernsteinForm) {
  const Point3d b[4] = {P(0, 0, 0), P(1, 2, 0), P(3, 2, 1), P(4, 0, 2)};
  NurbsCurve k = NurbsCurve::FromControlPoints(std::vector<Point3d>(b, b + 4), 3);
  const auto bern = [&](double u) {
    const double v = 1.0 - u;
    const double w[4] = {v * v * v, 3 * u * v * v, 3 * u * u * v, u * u * u};
    Point3d s(0, 0, 0);
    for (int i = 0; i < 4; ++i) s += w[i] * b[i];
    return s;
  };
  const int before = k.ControlPointCount();
  const double d0 = k.Domain().min, d1 = k.Domain().max;
  d8test::Check(k.InsertKnotAt(d0 + 0.3 * (d1 - d0)) == Result::Ok, "a knot insertion succeeds");
  d8test::Check(k.ControlPointCount() == before + 1, "exactly one control point is added");
  double worst = 0.0;
  for (int i = 0; i <= 200; ++i) {
    const double u = i / 200.0;
    worst = std::max(worst, k.PointAt(k.Domain().min + u * (k.Domain().max - k.Domain().min)).DistanceTo(bern(u)));
  }
  d8test::Check(worst < 1e-12, Msg("curve after insertion equals the Bernstein sum of the original four points", worst).c_str());
  // A second insertion at another place adds one more and still matches.
  d8test::Check(k.InsertKnotAt(k.Domain().min + 0.8 * (k.Domain().max - k.Domain().min)) == Result::Ok, "a second insertion succeeds");
  d8test::Check(k.ControlPointCount() == before + 2, "two insertions, two more control points");
  worst = 0.0;
  for (int i = 0; i <= 200; ++i) {
    const double u = i / 200.0;
    worst = std::max(worst, k.PointAt(k.Domain().min + u * (k.Domain().max - k.Domain().min)).DistanceTo(bern(u)));
  }
  d8test::Check(worst < 1e-12, Msg("still equal to the Bernstein sum after two insertions", worst).c_str());
}

// CURVE-0109 RemoveMultiKnot: a knot made double by insertion is removed again and the control polygon returns to the original
// one; a real kink stays unless the tolerance allows it.
D8_EXTRA_TEST(RowRemoveMultiKnotRestoresTheOriginalControlPolygon) {
  const std::vector<Point3d> cps = {P(0, 0, 0), P(1, 2, 0), P(2, -1, 1), P(3, 1, 0), P(4, 3, -1), P(5, 0, 0), P(6, 2, 2), P(7, -1, 1)};
  const NurbsCurve original = NurbsCurve::FromControlPoints(cps, 3);
  NurbsCurve k = original;
  const double d0 = k.Domain().min, d1 = k.Domain().max;
  d8test::Check(k.InsertKnotAt(d0 + 0.37 * (d1 - d0), 2) == Result::Ok, "insert a double knot at a non-knot parameter");
  d8test::Check(k.ControlPointCount() == original.ControlPointCount() + 2, "two control points are added");
  double dev = -1.0;
  const Result res = k.RemoveMultiKnots(1e-9, &dev);
  d8test::Check(res == Result::Ok, "RemoveMultiKnots lowers the inserted double knot");
  d8test::Check(k.ControlPointCount() == original.ControlPointCount() + 1, "the double knot became a single knot: one control point left over");
  int multi = 0;
  for (int i = 1; i < k.KnotCount(); ++i) {
    if (k.KnotAt(i) == k.KnotAt(i - 1) && k.KnotAt(i) > d0 && k.KnotAt(i) < d1) ++multi;
  }
  d8test::Check(multi == 0, "no interior knot of multiplicity 2 or more remains");
  d8test::Check(dev >= 0.0 && dev <= 1e-9, "the reported deviation is within the tolerance");
  double shape = 0.0;
  for (int i = 0; i <= 200; ++i) {
    const double f = i / 200.0;
    shape = std::max(shape, k.PointAt(At(k, f)).DistanceTo(original.PointAt(At(original, f))));
  }
  d8test::Check(shape < 1e-9, Msg("shape is unchanged by the knot removal", shape).c_str());
  // Removing the remaining simple knot too returns the hand-listed polygon.
  int idx = -1;
  for (int i = 0; i < k.KnotCount(); ++i)
    if (std::fabs(k.KnotAt(i) - (d0 + 0.37 * (d1 - d0))) < 1e-9) idx = i;
  d8test::Check(idx >= 0, "the inserted knot value is still in the knot vector");
  d8test::Check(idx >= 0 && k.RemoveKnotAt(idx, 1e-9) == Result::Ok, "the remaining single knot removes within 1e-9");
  d8test::Check(k.ControlPointCount() == original.ControlPointCount(), "the control point count is back to the original");
  double worst = 0.0;
  for (int i = 0; i < k.ControlPointCount() && i < static_cast<int>(cps.size()); ++i) worst = std::max(worst, k.ControlPointAt(i).DistanceTo(cps[i]));
  d8test::Check(worst < 1e-9, Msg("control points equal the hand-listed original polygon", worst).c_str());
  // A curve with a genuine kink (double knot with a moved control point): refused at a tight tolerance, accepted at a loose one.
  NurbsCurve kinked = original;
  kinked.InsertKnotAt(d0 + 0.5 * (d1 - d0), 3);  // multiplicity 3 = degree: a position-only break, shape still the original
  d8test::Check(kinked.SetControlPointAt(kinked.ControlPointCount() / 2, kinked.ControlPointAt(kinked.ControlPointCount() / 2) + Vector3d(0, 0.5, 0)) != Result::Failed,
                "move the control point at the kink by 0.5");
  const NurbsCurve kinked_before = kinked;
  double dev2 = 0.0;
  d8test::Check(kinked.RemoveMultiKnots(1e-6, &dev2) == Result::Failed, "a real kink is not removed at 1e-6");
  d8test::Check(kinked.ControlPointCount() == kinked_before.ControlPointCount(), "the refused curve keeps its control points");
}

// SURF-0082 Reparameterize (curve): a Moebius reparameterization keeps every point of the locus and moves the parameter exactly as
// u = k s / (1 + (k - 1) s). Oracle: a straight line (closed form for the map) and a circle (radius).
D8_EXTRA_TEST(RowReparameterizeRationalMovesTheParameterByTheClosedFormMoebiusMap) {
  const NurbsCurve line = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(10, 0, 0)}, 1);
  for (double kf : {0.25, 2.0, 5.0}) {
    NurbsCurve out;
    const Result r = line.ReparameterizeRational(kf, out);
    d8test::Check(r == Result::Ok, "ReparameterizeRational succeeds");
    // The reparameterized curve at normalised parameter s is the old curve at the Moebius image u(s) = kf s / (1 + (kf - 1) s)
    // (u(0) = 0, u(1) = 1, strictly increasing), so on the line x(s) = 10 u(s).
    double worst = 0.0;
    for (int i = 0; i <= 100; ++i) {
      const double s = i / 100.0;
      const double u = kf * s / (1.0 + (kf - 1.0) * s);
      const Point3d q = out.PointAt(At(out, s));
      worst = std::max(worst, std::fabs(q.x - 10.0 * u) + std::fabs(q.y) + std::fabs(q.z));
    }
    d8test::Check(worst < 1e-9, Msg("line point at the new parameter equals the closed-form position", worst).c_str());
  }
  const NurbsCurve circle = CircleXY(P(1, 1, 0), 3.0);
  NurbsCurve out;
  d8test::Check(circle.ReparameterizeRational(3.0, out) == Result::Ok, "a rational circle reparameterizes");
  double worst = 0.0;
  for (int i = 0; i <= 200; ++i) {
    const Point3d q = out.PointAt(At(out, i / 200.0));
    worst = std::max(worst, std::fabs(std::hypot(q.x - 1, q.y - 1) - 3.0));
  }
  d8test::Check(worst < 1e-9, Msg("the reparameterized circle keeps radius 3 at every sample", worst).c_str());
}

// CURVE-0066 InterpCrv: global interpolation through points. A cubic interpolant through points sampled from a cubic polynomial
// at the uniform parameters IS that polynomial (the cubic spline space contains it and the interpolant is unique).
D8_EXTRA_TEST(RowInterpCrvReproducesAPolynomialCurveAtUniformParameters) {
  const int n = 8;
  const auto poly = [](double t) { return P(t, t * t * t - 0.5 * t, 2.0 * t * t); };
  std::vector<Point3d> pts;
  for (int i = 0; i <= n; ++i) pts.push_back(poly(i / double(n)));
  CurveInterpolationOptions opt;
  opt.degree = 3;
  opt.parameter = InterpolationParameter::Uniform;
  NurbsCurve k;
  CurveInterpolationReport rep;
  d8test::Check(dino8::kernel::InterpolateCurve(pts, opt, k, &rep) == Result::Ok, "InterpolateCurve succeeds");
  double worst = 0.0;
  for (int i = 0; i <= 200; ++i) {
    const double t = i / 200.0;
    worst = std::max(worst, k.PointAt(k.Domain().min + t * (k.Domain().max - k.Domain().min)).DistanceTo(poly(t)));
  }
  d8test::Check(worst < 1e-9, Msg("interpolant equals the generating cubic polynomial everywhere", worst).c_str());
  // Requested end derivative (exact dC/dt): the closed-form derivative of the polynomial at 0 and 1.
  CurveInterpolationOptions opt2 = opt;
  opt2.start_tangent = Vector3d(1.0, -0.5, 0.0);          // d/dt poly at 0
  opt2.end_tangent = Vector3d(1.0, 3.0 - 0.5, 4.0);       // d/dt poly at 1
  opt2.tangents_are_derivatives = true;
  NurbsCurve k2;
  d8test::Check(dino8::kernel::InterpolateCurve(pts, opt2, k2) == Result::Ok, "interpolation with end derivatives succeeds");
  worst = 0.0;
  for (int i = 0; i <= 200; ++i) {
    const double t = i / 200.0;
    worst = std::max(worst, k2.PointAt(k2.Domain().min + t * (k2.Domain().max - k2.Domain().min)).DistanceTo(poly(t)));
  }
  d8test::Check(worst < 1e-9, Msg("with the true end derivatives the interpolant is still the same polynomial", worst).c_str());
  // Degree 1 is exactly the polyline through the points.
  CurveInterpolationOptions lin;
  lin.degree = 1;
  lin.parameter = InterpolationParameter::Uniform;
  NurbsCurve k1;
  d8test::Check(dino8::kernel::InterpolateCurve(pts, lin, k1) == Result::Ok, "degree 1 interpolation succeeds");
  const Point3d mid = k1.PointAt(k1.Domain().min + (0.5 / n) * (k1.Domain().max - k1.Domain().min));
  d8test::Check(Near(mid, 0.5 * (pts[0] + pts[1]), 1e-12), "degree 1 interpolant runs straight between neighbours (midpoint of the first segment)");
}

// Independent chord-length / centripetal parameter values, computed here from the points.
static std::vector<double> ParamsFor(const std::vector<Point3d>& pts, bool centripetal) {
  std::vector<double> t(pts.size(), 0.0);
  for (size_t i = 1; i < pts.size(); ++i) {
    const double d = pts[i].DistanceTo(pts[i - 1]);
    t[i] = t[i - 1] + (centripetal ? std::sqrt(d) : d);
  }
  for (size_t i = 1; i < pts.size(); ++i) t[i] /= t.back();
  return t;
}

// CURVE-0025 CurveThroughPolyline and CURVE-0026 CurveThroughPt: the fitted curve passes through every vertex, in picked order, at
// the chord-length (or centripetal) parameter the test computes itself.
D8_EXTRA_TEST(RowCurveThroughPolylineAndPointsPassesThroughEveryVertexAtItsChordParameter) {
  const std::vector<Point3d> pts = {P(0, 0, 0), P(2, 1, 0), P(3, 3, 1), P(5, 3.5, 0.5), P(6, 1, 2), P(8, 0, 1), P(9, 2, 0)};
  for (bool centripetal : {false, true}) {
    CurveInterpolationOptions opt;
    opt.degree = 3;
    opt.parameter = centripetal ? InterpolationParameter::Centripetal : InterpolationParameter::Chord;
    NurbsCurve k;
    d8test::Check(dino8::kernel::InterpolateCurve(pts, opt, k) == Result::Ok, "interpolation through the polyline vertices succeeds");
    const std::vector<double> t = ParamsFor(pts, centripetal);
    double worst = 0.0;
    for (size_t i = 0; i < pts.size(); ++i)
      worst = std::max(worst, k.PointAt(k.Domain().min + t[i] * (k.Domain().max - k.Domain().min)).DistanceTo(pts[i]));
    d8test::Check(worst < 1e-9, Msg(centripetal ? "centripetal: every vertex is hit at its own parameter" : "chord length: every vertex is hit at its own parameter", worst).c_str());
    d8test::Check(k.Degree() == 3 && k.ControlPointCount() == static_cast<int>(pts.size()), "a cubic with one control point per vertex, no extra knots");
  }
  // Closed-form check of the chord-length rule on a planar polyline of known step lengths 3, 4, 3: parameters 0, 3/10, 7/10, 1.
  const std::vector<Point3d> q = {P(0, 0, 0), P(3, 0, 0), P(3, 4, 0), P(6, 4, 0)};
  CurveInterpolationOptions opt;
  opt.degree = 2;
  NurbsCurve k;
  CurveInterpolationReport rep;
  d8test::Check(dino8::kernel::InterpolateCurve(q, opt, k, &rep) == Result::Ok, "degree 2 interpolation succeeds");
  const double total = 3.0 + 4.0 + 3.0;
  d8test::Check(rep.parameters.size() == 4 && std::fabs(rep.parameters[1] - 3.0 / total) < 1e-12 && std::fabs(rep.parameters[2] - 7.0 / total) < 1e-12 &&
                    std::fabs(rep.parameters[3] - 1.0) < 1e-12,
                "chord-length parameters are the cumulative distances 3, 7, 10 over 10");
}

// CURVE-0085 MergeCrv: curves that meet end to end become ONE curve whose length is the sum and whose ends are the outer ends.
D8_EXTRA_TEST(RowMergeCrvJoinsEndToEndCurvesIntoOneCurveOfSummedLength) {
  const double r = 2.0;
  NurbsCurve arc = CircleXY(P(0, 0, 0), r);
  NurbsCurve quarter, rest;
  d8test::Check(arc.Split(At(arc, 0.25), quarter, rest) == Result::Ok, "cut a quarter arc from the circle");
  // quarter runs (r,0,0) -> (0,r,0); append a straight run of known length 5 from (0,r,0) to (3,r+4,0).
  NurbsCurve run = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, r, 0), P(3, r + 4, 0)}, 1);
  NurbsCurve merged = quarter;
  d8test::Check(merged.Join(run) == Result::Ok, "Join accepts the end-to-end pair");
  d8test::Check(!merged.IsClosed(), "the merged curve is open");
  const double len = merged.LengthToTolerance(1e-9);
  d8test::Check(std::fabs(len - (kPi * r / 2 + 5.0)) < 1e-6, Msg("merged length is pi r / 2 + 5", len - (kPi * r / 2 + 5.0)).c_str());
  d8test::Check(Near(merged.PointAt(merged.Domain().min), P(r, 0, 0), 1e-9) && Near(merged.PointAt(merged.Domain().max), P(3, r + 4, 0), 1e-9),
                "ends are the outer ends of the two pieces");
  // The seam between the two pieces is a point of the merged curve: some parameter evaluates there.
  const double tj = merged.ClosestPointParameter(P(0, r, 0), 2000);
  d8test::Check(Near(merged.PointAt(tj), P(0, r, 0), 1e-6), "the joint is on the merged curve");
  // Pieces that do not meet are refused and the first curve is not changed.
  NurbsCurve far_away = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(10, 10, 0), P(11, 11, 0)}, 1);
  NurbsCurve kept = quarter;
  const int cps = kept.ControlPointCount();
  bool refused = false;
  try {
    refused = kept.Join(far_away) == Result::Failed;
  } catch (const std::exception&) {
    refused = true;  // the kernel names the gap in an exception
  }
  d8test::Check(refused, "a gap larger than the tolerance is refused");
  d8test::Check(kept.ControlPointCount() == cps, "the refused curve is unchanged");
}
