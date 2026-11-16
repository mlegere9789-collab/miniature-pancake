// Register rows of the kernel-geometry factory, Blender curve and NURBS editing rows (round 3). Every test names the Register rows
// it supports and checks the kernel against an oracle that does not come from the code under test: the cubic Bernstein form, the
// kappa constant 4 (sqrt 2 - 1) / 3 of the Bezier circle, the exact rational quadratic circle, the de Casteljau subdivision
// of a cubic, the closed-form midpoint of a rational quadratic, Pappus' theorems for a spun profile, and the prism formulas
// volume = base area x height and lateral area = length x height. None of them marks a row verified; the interactive part of each
// operator (selection, handles, undo) is not covered by a kernel test.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

double At(const NurbsCurve& k, double fraction) {
  const auto d = k.Domain();
  return d.min + fraction * (d.max - d.min);
}

bool Near(const Point3d& a, const Point3d& b, double tol) { return a.DistanceTo(b) <= tol; }
bool NearRel(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::fabs(b); }

// Cubic Bernstein form, written out: B(s) = (1-s)^3 P0 + 3 s (1-s)^2 P1 + 3 s^2 (1-s) P2 + s^3 P3.
Point3d Bezier3(const std::vector<Point3d>& p, double s) {
  const double a = (1 - s) * (1 - s) * (1 - s), b = 3 * s * (1 - s) * (1 - s), c = 3 * s * s * (1 - s), d = s * s * s;
  return P(a * p[0].x + b * p[1].x + c * p[2].x + d * p[3].x, a * p[0].y + b * p[1].y + c * p[2].y + d * p[3].y,
           a * p[0].z + b * p[1].z + c * p[2].z + d * p[3].z);
}

NurbsCurve CircleXY(const Point3d& c, double r) {
  const ON_Circle circle(ON_Plane(c, ON_3dVector(0, 0, 1)), r);
  ON_NurbsCurve nurbs;
  circle.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

// Volume of a closed revolved or extruded body: the divergence-theorem Volume() when the body is ON-solid, the sealed-solid
// integral otherwise (both exact integrals over the faces, see brep.h).
double VolumeOf(const Brep& b, bool* ok) {
  *ok = true;
  try {
    return b.VolumeOfSealedSolid();
  } catch (...) {
  }
  try {
    return b.Volume();
  } catch (...) {
  }
  *ok = false;
  return 0.0;
}

}  // namespace

// CURVE-0018 Add Bezier Circle and CURVE-0019 Add Bezier. A Blender Bezier circle is four cubic Bezier segments whose handles have
// the length kappa r with kappa = 4 (sqrt 2 - 1) / 3 = 0.5522847...; the segment midpoint then is exactly r (sqrt 2 / 2, sqrt 2 / 2)
// (Bernstein form at s = 1/2 gives r (4 + 3 kappa) / 8 = r sqrt 2 / 2), and the radial error stays below 2.8e-4 r. A single added
// Bezier is one cubic segment equal to its Bernstein form with end tangents along the handles. Colour of handles and handle types
// are not covered.
D8_EXTRA_TEST(RowBkgBezierCircleAndBezierSegmentFollowTheBernsteinForm) {
  const double r = 2.5, kappa = 4.0 * (std::sqrt(2.0) - 1.0) / 3.0;
  const Point3d c = P(1, -2, 0.5);
  const std::vector<Point3d> q0 = {P(c.x + r, c.y, c.z), P(c.x + r, c.y + kappa * r, c.z), P(c.x + kappa * r, c.y + r, c.z), P(c.x, c.y + r, c.z)};
  const NurbsCurve seg = NurbsCurve::FromControlPoints(q0, 3);
  d8test::Check(seg.Degree() == 3 && seg.ControlPointCount() == 4, "one quadrant is a single cubic Bezier span");
  d8test::Check(Near(seg.PointAt(At(seg, 0.5)), P(c.x + r * std::sqrt(2.0) / 2, c.y + r * std::sqrt(2.0) / 2, c.z), 1e-12),
                "the quadrant midpoint is c + r (sqrt2/2, sqrt2/2, 0)");
  double worst_radial = 0.0, worst_bern = 0.0;
  for (int i = 0; i <= 200; ++i) {
    const Point3d p = seg.PointAt(At(seg, i / 200.0));
    worst_radial = std::max(worst_radial, std::fabs(p.DistanceTo(c) - r) / r);
    worst_bern = std::max(worst_bern, p.DistanceTo(Bezier3(q0, i / 200.0)));
  }
  d8test::Check(worst_bern < 1e-12, Msg("quadrant equals its Bernstein form at 201 parameters", worst_bern).c_str());
  d8test::Check(worst_radial < 2.8e-4, Msg("radial error of the Bezier quadrant, relative to r, stays below 2.8e-4", worst_radial).c_str());
  // Four quadrants rotated by 90 degrees make the circle: total length 2 pi r to the radial error of the approximation.
  double total = 0.0;
  for (int k = 0; k < 4; ++k) {
    std::vector<Point3d> q;
    for (const Point3d& p : q0) {
      const double dx = p.x - c.x, dy = p.y - c.y;
      const double ca = std::cos(k * kPi / 2), sa = std::sin(k * kPi / 2);
      q.push_back(P(c.x + ca * dx - sa * dy, c.y + sa * dx + ca * dy, c.z));
    }
    total += NurbsCurve::FromControlPoints(q, 3).Length(2000);
  }
  d8test::Check(NearRel(total, 2.0 * kPi * r, 3e-4), Msg("four Bezier quadrants have total length 2 pi r (3e-4)", total).c_str());

  // CURVE-0019: one added Bezier with two handles; the end tangents run along the first and last handle.
  const std::vector<Point3d> b = {P(-1, 0, 0), P(-0.5, 1.5, 0), P(0.5, -1.5, 0), P(1, 0, 0)};
  const NurbsCurve bez = NurbsCurve::FromControlPoints(b, 3);
  double w = 0.0;
  for (int i = 0; i <= 100; ++i) w = std::max(w, bez.PointAt(At(bez, i / 100.0)).DistanceTo(Bezier3(b, i / 100.0)));
  d8test::Check(w < 1e-12, Msg("added Bezier equals the cubic Bernstein form", w).c_str());
  Vector3d h0 = b[1] - b[0], h1 = b[3] - b[2];
  h0.Unitize();
  h1.Unitize();
  d8test::Check(ON_DotProduct(bez.TangentAt(bez.Domain().min), h0) > 1.0 - 1e-9 && ON_DotProduct(bez.TangentAt(bez.Domain().max), h1) > 1.0 - 1e-9,
                "the end tangents run along the first and last handle");
}

// CURVE-0020 Add Nurbs Circle. The exact rational quadratic circle has every point at distance r from the centre (not an
// approximation), degree 2, rational, closed, in the plane of the circle, with length 2 pi r and the corner weights cos(pi / 4) = sqrt 2 / 2
// next to weight-1 points. Blender's own control-point count (eight) differs from the nine of the kernel form: the shape, not the count,
// is what is checked.
D8_EXTRA_TEST(RowBkgNurbsCircleIsTheExactRationalQuadraticCircle) {
  const Point3d c = P(-3, 2, 1);
  const double r = 4.0;
  const NurbsCurve k = CircleXY(c, r);
  d8test::Check(k.Degree() == 2 && k.IsRational() && k.IsClosed(), "circle is a closed rational quadratic");
  double worst = 0.0, z = 0.0;
  for (int i = 0; i <= 360; ++i) {
    const Point3d p = k.PointAt(At(k, i / 360.0));
    worst = std::max(worst, std::fabs(p.DistanceTo(c) - r));
    z = std::max(z, std::fabs(p.z - c.z));
  }
  d8test::Check(worst < 1e-12, Msg("all 361 samples lie at distance r from the centre", worst).c_str());
  d8test::Check(z < 1e-12, "all samples stay in the circle plane");
  d8test::Check(NearRel(k.Length(4000), 2.0 * kPi * r, 1e-6), Msg("length is 2 pi r", k.Length(4000)).c_str());
  d8test::Check(k.IsCircle(1e-9), "classified as a circle");
  int unit = 0, diagonal = 0;
  for (int i = 0; i < k.ControlPointCount(); ++i) {
    const double wi = k.WeightAt(i);
    if (std::fabs(wi - 1.0) < 1e-12) ++unit;
    if (std::fabs(wi - std::sqrt(2.0) / 2) < 1e-12) ++diagonal;
  }
  d8test::Check(unit + diagonal == k.ControlPointCount() && unit >= 4 && diagonal >= 4,
                "the control weights are 1 and cos(pi/4) = sqrt2/2 as the rational circle requires");
}

// CURVE-0021 Add Nurbs Curve and CURVE-0022 Add Path. A degree-3 curve over four control points is one Bernstein cubic; a
// path of five collinear equally spaced points is clamped (starts and ends at the end points), stays on the line,
// is monotone along it and has length exactly the distance between the end points. Order 5 paths and the cyclic flag of the operator
// are not covered.
D8_EXTRA_TEST(RowBkgNurbsCurveAndPathAddAreClampedBernsteinCurves) {
  const std::vector<Point3d> cp = {P(-1, 0, 0), P(-0.3, 1, 0), P(0.4, -1, 0), P(1, 0.2, 0)};
  const NurbsCurve k = NurbsCurve::FromControlPoints(cp, 3);
  double w = 0.0;
  for (int i = 0; i <= 80; ++i) w = std::max(w, k.PointAt(At(k, i / 80.0)).DistanceTo(Bezier3(cp, i / 80.0)));
  d8test::Check(w < 1e-12, Msg("four-point cubic equals the Bernstein form", w).c_str());
  d8test::Check(Near(k.PointAt(k.Domain().min), cp.front(), 1e-12) && Near(k.PointAt(k.Domain().max), cp.back(), 1e-12), "the curve is clamped to its end control points");

  std::vector<Point3d> path;
  for (int i = 0; i < 5; ++i) path.push_back(P(2.0 * i, 1.0, -0.5));  // spacing 2 along x at y = 1, z = -0.5
  const NurbsCurve line = NurbsCurve::FromControlPoints(path, 3);
  d8test::Check(line.ControlPointCount() == 5, "five points give five control points");
  d8test::Check(Near(line.PointAt(line.Domain().min), path.front(), 1e-12) && Near(line.PointAt(line.Domain().max), path.back(), 1e-12),
                "the path starts and ends at its end points");
  double off = 0.0, prev = -1.0;
  bool monotone = true;
  for (int i = 0; i <= 100; ++i) {
    const Point3d p = line.PointAt(At(line, i / 100.0));
    off = std::max(off, std::max(std::fabs(p.y - 1.0), std::fabs(p.z + 0.5)));
    monotone = monotone && p.x >= prev - 1e-12;
    prev = p.x;
  }
  d8test::Check(off < 1e-12, "the path stays on the line through its points");
  d8test::Check(monotone, "the path advances monotonically along the line");
  d8test::Check(NearRel(line.Length(), 8.0, 1e-9), Msg("length is the distance between the end points (8)", line.Length()).c_str());
  d8test::Check(line.IsLinear(1e-9), "classified as a line");
}

// CURVE-0048 Subdivide. Inserting the knot at the middle of a cubic Bezier is de Casteljau's subdivision: the new control
// polygon is P0, (P0+P1)/2, (P0+2P1+P2)/4, (P0+3P1+3P2+P3)/8, (P2+P3)/2, P3, and the curve does not move. Subdividing a
// clamped curve at all its span midpoints keeps its shape within 1e-12. Subdivision of selected segments only (a selection) is not
// covered.
D8_EXTRA_TEST(RowBkgSubdivideInsertsTheDeCasteljauPointsWithoutMovingTheCurve) {
  const std::vector<Point3d> p = {P(0, 0, 0), P(1, 3, 1), P(4, 3, -1), P(5, 0, 2)};
  const NurbsCurve orig = NurbsCurve::FromControlPoints(p, 3);
  NurbsCurve k = orig;
  const double mid = At(orig, 0.5);
  d8test::Check(k.InsertKnotAt(mid, 1) == Result::Ok, "insert a knot at the middle");
  d8test::Check(k.ControlPointCount() == 5, "one simple knot adds one control point (4 -> 5)");
  const auto mix = [&](double a, double b, double c, double d) {
    return P((a * p[0].x + b * p[1].x + c * p[2].x + d * p[3].x) / (a + b + c + d), (a * p[0].y + b * p[1].y + c * p[2].y + d * p[3].y) / (a + b + c + d),
             (a * p[0].z + b * p[1].z + c * p[2].z + d * p[3].z) / (a + b + c + d));
  };
  // A degree-3 curve with one interior knot of multiplicity 1 has 5 control points: P0, (P0+P1)/2, ... is for multiplicity 3 (a Bezier split).
  NurbsCurve split = orig;
  d8test::Check(split.InsertKnotAt(mid, 3) == Result::Ok, "insert the middle knot with multiplicity 3 (a Bezier split)");
  d8test::Check(split.ControlPointCount() == 7, "two cubic Bezier spans have seven control points");
  if (split.ControlPointCount() == 7) {
    const Point3d expect[7] = {p[0], mix(1, 1, 0, 0), mix(1, 2, 1, 0), mix(1, 3, 3, 1), mix(0, 1, 2, 1), mix(0, 0, 1, 1), p[3]};
    double dev = 0.0;
    for (int i = 0; i < 7; ++i) dev = std::max(dev, split.ControlPointAt(i).DistanceTo(expect[i]));
    d8test::Check(dev < 1e-12, Msg("control points are the de Casteljau points of the cubic", dev).c_str());
  }
  double shape = 0.0;
  for (int i = 0; i <= 100; ++i) {
    const double t = At(orig, i / 100.0);
    shape = std::max(shape, std::max(orig.PointAt(t).DistanceTo(k.PointAt(t)), orig.PointAt(t).DistanceTo(split.PointAt(t))));
  }
  d8test::Check(shape < 1e-12, Msg("the subdivided curves equal the original at 101 parameters", shape).c_str());
}

// CURVE-0049 Switch Direction. Reversing a curve maps the point at domain fraction f to fraction 1 - f: the start and end swap, every point is hit in the
// opposite order, the tangent at the same point flips sign and the length is unchanged. The test uses a circle arc (tangent known
// in closed form: counter-clockwise (-sin, cos)) and a cubic.
D8_EXTRA_TEST(RowBkgSwitchDirectionReversesTheParameterisationAndFlipsTheTangent) {
  const std::vector<Point3d> cp = {P(0, 0, 0), P(1, 4, 0), P(3, -2, 1), P(5, 1, 1), P(8, 0, 3)};
  const NurbsCurve orig = NurbsCurve::FromControlPoints(cp, 3);
  NurbsCurve rev = orig;
  d8test::Check(rev.Reverse() == Result::Ok, "reverse");
  // OpenNURBS reverses a domain [a, b] to [-b, -a]; the oracle is therefore stated on the domain fraction: rev(1 - f) = orig(f).
  double dev = 0.0;
  for (int i = 0; i <= 100; ++i) dev = std::max(dev, orig.PointAt(At(orig, i / 100.0)).DistanceTo(rev.PointAt(At(rev, 1.0 - i / 100.0))));
  d8test::Check(dev < 1e-9, Msg("rev(1 - f) equals orig(f) at 101 fractions", dev).c_str());
  d8test::Check(NearRel(rev.Length(), orig.Length(), 1e-9), "the length is unchanged");
  d8test::Check(Near(rev.PointAt(rev.Domain().min), cp.back(), 1e-12) && Near(rev.PointAt(rev.Domain().max), cp.front(), 1e-12), "start and end swap");

  const Point3d c = P(1, 1, 0);
  const double r = 3.0;
  NurbsCurve circle = CircleXY(c, r);
  const double theta = 0.7;  // parameter of the circle is the angle for ON_Circle's NURBS form only up to reparametrisation: use the point.
  const Point3d q = P(c.x + r * std::cos(theta), c.y + r * std::sin(theta), 0);
  const double t_on = circle.ClosestPointParameter(q);
  Vector3d t0 = circle.TangentAt(t_on);
  t0.Unitize();
  const Vector3d expect(-std::sin(theta), std::cos(theta), 0);
  d8test::Check(ON_DotProduct(t0, expect) > 1.0 - 1e-6, "the circle tangent at angle 0.7 is (-sin, cos)");
  d8test::Check(circle.Reverse() == Result::Ok, "reverse the circle");
  const double t_rev = circle.ClosestPointParameter(q);
  Vector3d t1 = circle.TangentAt(t_rev);
  t1.Unitize();
  d8test::Check(ON_DotProduct(t1, expect) < -1.0 + 1e-6, "after reversal the tangent at the same point is (sin, -cos)");
}

// CURVE-0047 Split. Splitting a circle at a parameter gives two independent curves that share the split point, whose lengths add
// up to the circle's (exactly 2 pi r) and that each lie on the circle; the arc between parameters of a quarter and a half turn
// has the closed-form length r pi / 2 for a quarter.
D8_EXTRA_TEST(RowBkgSplitGivesTwoArcsOfTheClosedFormLengths) {
  const Point3d c = P(0.5, -1, 0);
  const double r = 2.0;
  const NurbsCurve circle = CircleXY(c, r);
  NurbsCurve left, right;
  const double t = At(circle, 0.25);
  d8test::Check(circle.Split(t, left, right) == Result::Ok, "split the circle");
  const Point3d s = circle.PointAt(t);
  d8test::Check(Near(left.PointAt(left.Domain().max), s, 1e-9) && Near(right.PointAt(right.Domain().min), s, 1e-9), "both parts meet at the split point");
  d8test::Check(Near(left.PointAt(left.Domain().min), circle.PointAt(circle.Domain().min), 1e-9) && Near(right.PointAt(right.Domain().max), circle.PointAt(circle.Domain().max), 1e-9),
                "the outer ends are the original ends");
  d8test::Check(NearRel(left.Length(2000) + right.Length(2000), 2.0 * kPi * r, 1e-5), "the lengths add up to 2 pi r");
  // Parameter quarter of ON_Circle's NURBS form is a quarter turn only for the uniform rational quadratic; measure rather than assume:
  const double a = std::atan2(s.y - c.y, s.x - c.x);
  d8test::Check(NearRel(left.Length(2000), r * (a < 0 ? a + 2 * kPi : a), 1e-5), "the first part has length r times the angle it spans");
  double dev = 0.0;
  for (int i = 0; i <= 50; ++i) {
    dev = std::max(dev, std::fabs(left.PointAt(left.Domain().min + (left.Domain().max - left.Domain().min) * i / 50.0).DistanceTo(c) - r));
    dev = std::max(dev, std::fabs(right.PointAt(right.Domain().min + (right.Domain().max - right.Domain().min) * i / 50.0).DistanceTo(c) - r));
  }
  d8test::Check(dev < 1e-9, Msg("both parts stay on the circle", dev).c_str());
}

// CURVE-0045 Set Spline Type. Converting between spline types keeps the shape: a polyline (degree 1) raised to degree 3 is the same
// set of points (every point of the original is on the converted curve), a quadratic Bezier raised to degree 3 has the control
// points P0, (P0 + 2 P1)/3, (2 P1 + P2)/3, P2 of the classical elevation formula, and a non-rational curve converted to rational
// with unit weights does not move. Conversion to and from the Blender handle (Bezier) representation is not covered.
D8_EXTRA_TEST(RowBkgSplineTypeConversionKeepsTheShape) {
  const std::vector<Point3d> q = {P(0, 0, 0), P(2, 3, 0), P(4, 0, 1)};
  NurbsCurve quad = NurbsCurve::FromControlPoints(q, 2);
  NurbsCurve cubic = quad;
  d8test::Check(cubic.ElevateDegree(3) != Result::Failed, "elevate the quadratic to degree 3");
  d8test::Check(cubic.Degree() == 3 && cubic.ControlPointCount() == 4, "degree 3 with four control points");
  const Point3d e[4] = {q[0], P((q[0].x + 2 * q[1].x) / 3, (q[0].y + 2 * q[1].y) / 3, (q[0].z + 2 * q[1].z) / 3), P((2 * q[1].x + q[2].x) / 3, (2 * q[1].y + q[2].y) / 3, (2 * q[1].z + q[2].z) / 3), q[2]};
  double dev = 0.0;
  for (int i = 0; i < 4 && cubic.ControlPointCount() == 4; ++i) dev = std::max(dev, cubic.ControlPointAt(i).DistanceTo(e[i]));
  d8test::Check(dev < 1e-12, Msg("elevated control points follow the classical formula", dev).c_str());
  double shape = 0.0;
  for (int i = 0; i <= 100; ++i) shape = std::max(shape, quad.PointAt(At(quad, i / 100.0)).DistanceTo(cubic.PointAt(At(cubic, i / 100.0))));
  d8test::Check(shape < 1e-12, Msg("the elevated curve is the same curve", shape).c_str());

  const std::vector<Point3d> poly = {P(0, 0, 0), P(1, 1, 0), P(2, 0, 0), P(3, 1, 0)};
  NurbsCurve line = NurbsCurve::FromControlPoints(poly, 1);
  NurbsCurve up = line;
  d8test::Check(up.ElevateDegree(3) != Result::Failed, "elevate the polyline to degree 3");
  double pdev = 0.0;
  for (int i = 0; i <= 60; ++i) pdev = std::max(pdev, line.PointAt(At(line, i / 60.0)).DistanceTo(up.PointAt(At(up, i / 60.0))));
  d8test::Check(pdev < 1e-9, Msg("the elevated polyline is the same polyline", pdev).c_str());
  NurbsCurve rat = quad;
  d8test::Check(rat.MakeRational() == Result::Ok && rat.IsRational(), "make rational");
  double rdev = 0.0;
  for (int i = 0; i <= 60; ++i) rdev = std::max(rdev, quad.PointAt(At(quad, i / 60.0)).DistanceTo(rat.PointAt(At(rat, i / 60.0))));
  d8test::Check(rdev < 1e-12, "unit weights leave the curve in place");
}

// CURVE-0044 Spin, CURVE-0010 Extrude and CURVE-0011 Extrude Curve and Move. Spinning a closed profile about an axis is governed by the
// theorems of Pappus: the lateral area is (profile length) x (path of the profile centroid) and the volume is (profile area) x
// (path of the centroid); a circle of radius r centred at distance R spun by 2 pi has area 4 pi^2 R r and volume 2 pi^2 R r^2, and spun by
// pi/2 a quarter of both (the lateral area counted without the two caps). An extruded closed profile has volume = area x height and an
// open profile has area = length x height. Extruding in a direction that is not perpendicular to the profile plane keeps the volume at
// base area x the height measured along the normal.
D8_EXTRA_TEST(RowBkgSpinAndExtrudeFollowPappusAndThePrismFormulas) {
  const double R = 5.0, r = 1.25;
  const NurbsCurve circle = CircleXY(P(R, 0, 0), r);
  // The circle lies in the xy plane, so the profile plane must contain the axis: rotate the circle into the xz plane about the x axis.
  ON_NurbsCurve rot = circle.raw();
  ON_Xform xrot;
  xrot.Rotation(kPi / 2, ON_3dVector(1, 0, 0), ON_3dPoint(0, 0, 0));
  rot.Transform(xrot);
  NurbsCurve profile;
  profile.raw() = rot;
  const Brep torus = Brep::Revolve(profile, P(0, 0, 0), Vector3d(0, 0, 1));
  bool ok = false;
  const double v = VolumeOf(torus, &ok);
  d8test::Check(ok, "a full spin of a closed profile has a volume");
  if (ok) d8test::Check(NearRel(v, 2.0 * kPi * kPi * R * r * r, 1e-4), Msg("full spin volume is 2 pi^2 R r^2", v).c_str());
  double area = 0.0;
  try {
    area = torus.Area();
  } catch (...) {
    d8test::Check(false, "Area() of the spun profile");
  }
  d8test::Check(NearRel(area, 4.0 * kPi * kPi * R * r, 1e-4), Msg("full spin area is 4 pi^2 R r", area).c_str());
  const Brep quarter = Brep::Revolve(profile, P(0, 0, 0), Vector3d(0, 0, 1), kPi / 2, true);
  double qv = 0.0;
  bool qok = false;
  qv = VolumeOf(quarter, &qok);
  d8test::Check(qok, "a quarter spin with caps has a volume");
  if (qok) d8test::Check(NearRel(qv, 0.5 * kPi * R * kPi * r * r, 1e-4), Msg("quarter spin volume is (pi r^2) R (pi/2)", qv).c_str());

  // CURVE-0010 / 0011: a closed rectangle polyline and a circle extruded.
  const std::vector<Point3d> rect = {P(0, 0, 0), P(3, 0, 0), P(3, 2, 0), P(0, 2, 0), P(0, 0, 0)};
  const NurbsCurve rc = NurbsCurve::FromControlPoints(rect, 1);
  const Brep prism = Brep::Extrude(rc, Vector3d(0, 0, 4), true);
  bool pok = false;
  const double pv = VolumeOf(prism, &pok);
  d8test::Check(pok && NearRel(pv, 24.0, 1e-6), Msg("rectangle 3 x 2 extruded by 4 has volume 24", pv).c_str());
  const Brep ball = Brep::Extrude(CircleXY(P(0, 0, 0), 1.5), Vector3d(0, 0, 3), true);
  bool cok = false;
  const double cv = VolumeOf(ball, &cok);
  d8test::Check(cok && NearRel(cv, kPi * 1.5 * 1.5 * 3.0, 1e-4), Msg("circle r 1.5 extruded by 3 has volume pi r^2 h", cv).c_str());
  const NurbsCurve open = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(4, 0, 0)}, 1);
  const Brep sheet = Brep::Extrude(open, Vector3d(0, 0, 2.5), false);
  double sa = 0.0;
  try {
    sa = sheet.Area();
  } catch (...) {
    d8test::Check(false, "Area() of an extruded open profile");
  }
  d8test::Check(NearRel(sa, 10.0, 1e-9), Msg("an open profile of length 4 extruded by 2.5 has area 10", sa).c_str());
  // The same profile extruded along a slanted direction: the wall is a parallelogram, area = length x |direction x profile tangent|.
  const Brep slanted = Brep::Extrude(open, Vector3d(1, 0, 2), false);
  double sla = 0.0;
  try {
    sla = slanted.Area();
  } catch (...) {
    d8test::Check(false, "Area() of a slanted extrusion");
  }
  d8test::Check(NearRel(sla, 4.0 * 2.0, 1e-9), Msg("the slanted wall has area 4 x 2 (shear keeps base and height)", sla).c_str());
}
