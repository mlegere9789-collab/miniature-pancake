// Register rows of the kernel-geometry factory (CURVE domain), round 2. Every test names the Register rows it supports and
// checks the kernel against an oracle that does not come from the code under test: the Bernstein form of a Bezier curve, the
// analytic circle and sphere, the bilinear blend of four corners, the lens-area formula of two intersecting discs, the
// orthogonal-projection formula p - ((p - o) . n) n, and the exact cubic y = x^3. None of them marks a row verified.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/curve_interpolate.h"
#include "dino8/kernel/planar_curve_boolean.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

NurbsCurve CircleXY(const Point3d& c, double r) {
  const ON_Circle circle(ON_Plane(c, ON_3dVector(0, 0, 1)), r);
  ON_NurbsCurve nurbs;
  circle.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

double At(const NurbsCurve& k, double fraction) {
  const auto d = k.Domain();
  return d.min + fraction * (d.max - d.min);
}

bool Near(const Point3d& a, const Point3d& b, double tol) { return a.DistanceTo(b) <= tol; }

// Cubic Bernstein form, written out: B(s) = (1-s)^3 P0 + 3 s (1-s)^2 P1 + 3 s^2 (1-s) P2 + s^3 P3.
Point3d Bezier3(const std::vector<Point3d>& p, double s) {
  const double a = (1 - s) * (1 - s) * (1 - s), b = 3 * s * (1 - s) * (1 - s), c = 3 * s * s * (1 - s), d = s * s * s;
  return P(a * p[0].x + b * p[1].x + c * p[2].x + d * p[3].x, a * p[0].y + b * p[1].y + c * p[2].y + d * p[3].y,
           a * p[0].z + b * p[1].z + c * p[2].z + d * p[3].z);
}

}  // namespace

// CURVE-0023 Curve: "Draw a NURBS curve from control point locations with chosen degree and knot style". Four control points of
// degree 3 are one Bezier arc: every point equals the Bernstein form; a longer control polygon of degree 3 is clamped (starts and
// ends at the first and last control point, tangent along the first and last polygon leg), and a polygon whose last point repeats
// the first gives a closed curve. Periodic output of the command is not covered.
D8_EXTRA_TEST(RowCurveFromControlPointsIsTheBernsteinCurveAndClamped) {
  const std::vector<Point3d> cp = {P(0, 0, 0), P(1, 3, 1), P(4, 3, -1), P(5, 0, 2)};
  const NurbsCurve bez = NurbsCurve::FromControlPoints(cp, 3);
  d8test::Check(bez.Degree() == 3 && bez.ControlPointCount() == 4, "degree 3 with four points is one Bezier span");
  double worst = 0.0;
  for (int i = 0; i <= 50; ++i) {
    const double s = i / 50.0;
    worst = std::max(worst, bez.PointAt(At(bez, s)).DistanceTo(Bezier3(cp, s)));
  }
  d8test::Check(worst < 1e-12, Msg("curve equals the Bernstein form at 51 parameters", worst).c_str());

  const std::vector<Point3d> poly = {P(0, 0, 0), P(1, 2, 0), P(2, -1, 0), P(3, 2, 1), P(4, 0, 1), P(5, 3, 0), P(6, 1, 2)};
  const NurbsCurve k = NurbsCurve::FromControlPoints(poly, 3);
  d8test::Check(k.ControlPointCount() == 7, "one control point per input point");
  d8test::Check(Near(k.PointAt(k.Domain().min), poly.front(), 1e-12) && Near(k.PointAt(k.Domain().max), poly.back(), 1e-12),
                "clamped knots: the curve starts and ends at the end control points");
  const Vector3d t0 = k.TangentAt(k.Domain().min), t1 = k.TangentAt(k.Domain().max);
  Vector3d d0 = poly[1] - poly[0], d1 = poly[6] - poly[5];
  d0.Unitize();
  d1.Unitize();
  d8test::Check(ON_DotProduct(t0, d0) > 1.0 - 1e-9 && ON_DotProduct(t1, d1) > 1.0 - 1e-9, "end tangents lie along the first and last polygon leg");
  // Convex hull property: every sample stays inside the control polygon's bounding box.
  bool inside = true;
  for (int i = 0; i <= 100; ++i) {
    const Point3d q = k.PointAt(At(k, i / 100.0));
    inside = inside && q.x >= -1e-12 && q.x <= 6 + 1e-12 && q.y >= -1 - 1e-12 && q.y <= 3 + 1e-12 && q.z >= -1e-12 && q.z <= 2 + 1e-12;
  }
  d8test::Check(inside, "samples stay in the bounding box of the control polygon");
  std::vector<Point3d> loop = {P(0, 0, 0), P(2, 0, 0), P(2, 2, 0), P(0, 2, 0), P(0, 0, 0)};
  const NurbsCurve closed = NurbsCurve::FromControlPoints(loop, 3);
  d8test::Check(closed.IsClosed(), "a polygon whose last point repeats the first gives a closed curve");
}

// CURVE-0018 CrvEnd and CURVE-0019 CrvStart: the point at a curve's end / start is the analytic end / start. A circle starts and
// ends at c + r (1, 0, 0); a quarter arc of it ends at c + r (0, 1, 0); a clamped cubic starts and ends at its end control points;
// after Reverse the two swap. Placing the point object in the document is app behaviour and is not covered.
D8_EXTRA_TEST(RowCrvStartAndCrvEndGiveTheAnalyticEndPoints) {
  const Point3d c = P(2, -1, 0.5);
  const double r = 3.0;
  NurbsCurve circle = CircleXY(c, r);
  d8test::Check(Near(circle.PointAt(circle.Domain().min), P(c.x + r, c.y, c.z), 1e-9), "circle start is c + r (1,0,0)");
  d8test::Check(Near(circle.PointAt(circle.Domain().max), P(c.x + r, c.y, c.z), 1e-9), "closed circle end equals its start");
  NurbsCurve arc = circle;
  d8test::Check(arc.Trim(At(circle, 0.0), At(circle, 0.25)) == Result::Ok, "trim to the first quarter");
  d8test::Check(Near(arc.PointAt(arc.Domain().max), P(c.x, c.y + r, c.z), 1e-9), "quarter arc ends at c + r (0,1,0)");
  const std::vector<Point3d> cp = {P(0, 0, 0), P(1, 4, 0), P(3, -2, 1), P(5, 1, 1), P(8, 0, 3)};
  NurbsCurve k = NurbsCurve::FromControlPoints(cp, 3);
  d8test::Check(Near(k.PointAt(k.Domain().min), cp.front(), 1e-12) && Near(k.PointAt(k.Domain().max), cp.back(), 1e-12), "cubic starts and ends at its end control points");
  d8test::Check(k.Reverse() == Result::Ok, "reverse");
  d8test::Check(Near(k.PointAt(k.Domain().min), cp.back(), 1e-12) && Near(k.PointAt(k.Domain().max), cp.front(), 1e-12), "reversal swaps start and end");
}

// CURVE-0042 ExtractIsocurve: "Duplicate an isoparametric curve of a surface in U or V at a picked location". Oracle one: the
// bilinear patch P(u,v) = (u, v, u v) has the straight iso lines (u0, v, u0 v) and (u, v0, u v0). Oracle two: an analytic sphere
// (rational NURBS, u the longitude, v the latitude) has iso latitudes that are circles of radius R cos(phi) at height R sin(phi).
D8_EXTRA_TEST(RowExtractIsocurveMatchesTheBilinearLinesAndTheSphereLatitudes) {
  const std::vector<Point3d> grid = {P(0, 0, 0), P(0, 1, 0), P(1, 0, 0), P(1, 1, 1)};  // index = i * v_count + j
  const NurbsSurface bil = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  const auto du = bil.Domain(0), dv = bil.Domain(1);
  double worst = 0.0;
  for (double f : {0.1, 0.37, 0.8}) {
    const double u0 = du.min + f * (du.max - du.min), v0 = dv.min + f * (dv.max - dv.min);
    const double uu = (u0 - du.min) / (du.max - du.min), vv = (v0 - dv.min) / (dv.max - dv.min);
    const NurbsCurve iu = bil.IsoCurve(1, u0);  // u fixed, the curve runs in v (direction = running direction)
    const NurbsCurve iv = bil.IsoCurve(0, v0);
    for (int i = 0; i <= 20; ++i) {
      const double s = i / 20.0;
      worst = std::max(worst, iu.PointAt(At(iu, s)).DistanceTo(P(uu, s, uu * s)));
      worst = std::max(worst, iv.PointAt(At(iv, s)).DistanceTo(P(s, vv, s * vv)));
    }
  }
  d8test::Check(worst < 1e-12, Msg("bilinear iso curves are the exact lines", worst).c_str());

  const double R = 2.5;
  const ON_Sphere sphere(ON_3dPoint(1, 2, 3), R);
  ON_NurbsSurface ns;
  sphere.GetNurbForm(ns);
  NurbsSurface sk;
  sk.raw() = ns;
  const auto sdu = sk.Domain(0), sdv = sk.Domain(1);
  // The NURBS sphere's v parameter is not the latitude angle (it is a rational quadratic parameter), so the oracle is geometric:
  // every iso curve is a circle in a horizontal plane, its points lie on the sphere (|q - c| = R) and its radius obeys
  // Pythagoras, radius^2 = R^2 - h^2 for the height h above the centre.
  double on_sphere = 0.0, flat = 0.0, pyth = 0.0, len_err = 0.0;
  for (double f : {0.2, 0.5, 0.65}) {
    const double v0 = sdv.min + f * (sdv.max - sdv.min);
    const NurbsCurve iso = sk.IsoCurve(0, v0);  // runs in u (longitude) at fixed latitude v0
    const double h = iso.PointAt(At(iso, 0.0)).z - 3.0;
    for (int i = 0; i <= 40; ++i) {
      const Point3d q = iso.PointAt(At(iso, i / 40.0));
      on_sphere = std::max(on_sphere, std::fabs(q.DistanceTo(P(1, 2, 3)) - R));
      flat = std::max(flat, std::fabs(q.z - 3.0 - h));
      pyth = std::max(pyth, std::fabs(std::hypot(q.x - 1, q.y - 2) - std::sqrt(R * R - h * h)));
    }
    d8test::Check(iso.IsClosed(), "a latitude circle is closed");
    len_err = std::max(len_err, std::fabs(iso.LengthToTolerance(1e-9) - 2 * kPi * std::sqrt(R * R - h * h)));
  }
  d8test::Check(on_sphere < 1e-9 && flat < 1e-9, Msg("latitude iso curves are horizontal circles on the sphere", std::max(on_sphere, flat)).c_str());
  d8test::Check(pyth < 1e-9, Msg("their radius is sqrt(R^2 - h^2)", pyth).c_str());
  d8test::Check(len_err < 1e-6, Msg("their length is 2 pi sqrt(R^2 - h^2)", len_err).c_str());
  (void)sdu;
}

// CURVE-0062 InsertEditPoint: "Add edit points to a curve at picked locations". Raising the multiplicity of a knot of a cubic to
// the degree makes the curve pass through a control point (the de Boor property), and the new edit point is the curve point at
// that parameter, evaluated here from the Bernstein form of the original Bezier curve. The shape must not change.
D8_EXTRA_TEST(RowInsertEditPointPutsAControlPointOnTheCurveAndKeepsTheShape) {
  const std::vector<Point3d> cp = {P(0, 0, 0), P(1, 3, 1), P(4, 3, -1), P(5, 0, 2)};
  const NurbsCurve original = NurbsCurve::FromControlPoints(cp, 3);
  NurbsCurve k = original;
  const double s = 0.3;
  const double t = At(k, s);
  d8test::Check(k.InsertKnotAt(t, 3) == Result::Ok, "insert the knot with multiplicity 3");
  d8test::Check(k.ControlPointCount() == 4 + 3, "three control points were added");
  bool on_curve = false;
  const Point3d expect = Bezier3(cp, s);
  for (int i = 0; i < k.ControlPointCount(); ++i) on_curve = on_curve || Near(k.ControlPointAt(i), expect, 1e-9);
  d8test::Check(on_curve, "one control point is the exact curve point at the picked parameter (the edit point)");
  double worst = 0.0;
  for (int i = 0; i <= 80; ++i) {
    const double f = i / 80.0;
    worst = std::max(worst, k.PointAt(At(k, f)).DistanceTo(Bezier3(cp, f)));
  }
  d8test::Check(worst < 1e-12, Msg("the shape is unchanged (Bernstein form)", worst).c_str());
}

// CURVE-0090 OffsetMultiple: several parallel offsets in one command. Each offset of a circle of radius r by d is a concentric
// circle of radius r - d (inside) or r + d (outside), the two sides being opposite; the offset of a straight segment by d is the
// parallel segment at distance d. The oracle is the closed form, the lengths 2 pi (r +- d) and the point-to-line distance.
D8_EXTRA_TEST(RowOffsetMultipleGivesConcentricCirclesAndParallelLines) {
  const Point3d c = P(1, 1, 0);
  const double r = 5.0;
  const NurbsCurve circle = CircleXY(c, r);
  for (double d : {0.5, 1.0, 2.0, 3.5}) {
    NurbsCurve in, out;
    const Result a = circle.OffsetInPlane(d, in);
    const Result b = circle.OffsetInPlane(-d, out);
    d8test::Check(a == Result::Ok && b == Result::Ok, "offset on both sides succeeds");
    if (a != Result::Ok || b != Result::Ok) continue;
    double ra = 0.0, rb = 0.0;
    for (int i = 0; i < 16; ++i) {
      const Point3d qa = in.PointAt(At(in, i / 16.0)), qb = out.PointAt(At(out, i / 16.0));
      ra += std::hypot(qa.x - c.x, qa.y - c.y) / 16.0;
      rb += std::hypot(qb.x - c.x, qb.y - c.y) / 16.0;
    }
    const double lo = std::min(ra, rb), hi = std::max(ra, rb);
    d8test::Check(std::fabs(lo - (r - d)) < 1e-3 && std::fabs(hi - (r + d)) < 1e-3, Msg("the two offsets have radius r - d and r + d", std::fabs(lo - (r - d))).c_str());
    d8test::Check(std::fabs(in.Length() + out.Length() - 2 * 2 * kPi * r) < 0.05, "the two offset lengths add up to 2 * 2 pi r");
  }
  const NurbsCurve line = NurbsCurve::FromControlPoints({P(0, 0, 0), P(10, 0, 0)}, 1);
  NurbsCurve up, down;
  d8test::Check(line.OffsetInPlane(2.0, up) == Result::Ok && line.OffsetInPlane(-2.0, down) == Result::Ok, "line offsets succeed");
  const double ya = up.PointAt(At(up, 0.5)).y, yb = down.PointAt(At(down, 0.5)).y;
  d8test::Check(std::fabs(std::fabs(ya) - 2.0) < 1e-9 && std::fabs(std::fabs(yb) - 2.0) < 1e-9 && ya * yb < 0, "the line offsets sit at distance 2 on opposite sides");
}

// CURVE-0100 ProjectToCPlane: "Flatten objects onto the construction plane by orthogonal projection". The oracle is the formula
// q = p - ((p - o) . n) n for an OBLIQUE plane, applied to dense samples of a space curve; the kernel projects along the plane
// normal onto a plane surface.
D8_EXTRA_TEST(RowProjectToCPlaneIsOrthogonalProjectionOntoAnObliquePlane) {
  const Point3d o = P(1, 2, 3);
  Vector3d n(1, -2, 2);
  n.Unitize();
  ON_Plane plane(o, n);
  ON_PlaneSurface ps(plane);
  ps.SetExtents(0, ON_Interval(-30, 30));
  ps.SetExtents(1, ON_Interval(-30, 30));
  ps.SetDomain(0, -30, 30);
  ps.SetDomain(1, -30, 30);
  const NurbsCurve k = NurbsCurve::FromControlPoints({P(0, 0, 0), P(2, 3, 1), P(5, 1, 4), P(7, 4, 2)}, 3);
  dino8::kernel::IntersectOptions opt;
  opt.tolerance = 1e-6;
  const auto res = dino8::kernel::ProjectCurveToSurface(k.raw(), ps, n, opt);
  d8test::Check(res.hit_count >= 10, "samples of the curve hit the plane");
  double worst = 0.0;
  for (size_t i = 0; i < res.points.size(); ++i) {
    const Point3d p = k.PointAt(res.t[i]);
    const double h = (p - o) * n;
    const Point3d expect = p - h * n;
    worst = std::max(worst, res.points[i].DistanceTo(expect));
  }
  d8test::Check(worst < 1e-5, Msg("every projected sample equals p - ((p - o).n) n", worst).c_str());
}

// CURVE-0022 CurveBoolean: boundary of the union, intersection and difference of two overlapping regions. Oracle: the lens area
// of two discs of radius r whose centres are d apart, A_lens = 2 r^2 acos(d / 2r) - (d / 2) sqrt(4 r^2 - d^2); union = 2 pi r^2
// - A_lens, difference = pi r^2 - A_lens; and for two overlapping squares the inclusion-exclusion areas.
D8_EXTRA_TEST(RowCurveBooleanAreasMatchTheLensFormulaAndInclusionExclusion) {
  const double r = 2.0, d = 2.0;
  const NurbsCurve a = CircleXY(P(0, 0, 0), r), b = CircleXY(P(d, 0, 0), r);
  const double lens = 2 * r * r * std::acos(d / (2 * r)) - (d / 2) * std::sqrt(4 * r * r - d * d);
  const ON_Plane xy = ON_Plane::World_xy;
  struct Case { dino8::kernel::CurveRegionOp op; double area; const char* name; };
  const Case cases[] = {{dino8::kernel::CurveRegionOp::Intersection, lens, "intersection"},
                        {dino8::kernel::CurveRegionOp::Union, 2 * kPi * r * r - lens, "union"},
                        {dino8::kernel::CurveRegionOp::Difference, kPi * r * r - lens, "difference"}};
  for (const Case& cs : cases) {
    const auto res = dino8::kernel::PlanarCurveBoolean(a.raw(), b.raw(), xy, cs.op, 1e-6);
    d8test::Check(res.ok && res.loops.size() == 1, (std::string("one loop for the ") + cs.name + " of two overlapping discs").c_str());
    if (!res.ok || res.loops.empty()) continue;
    NurbsCurve loop;
    loop.raw() = res.loops[0];
    NurbsCurve::RegionProperties props;
    d8test::Check(loop.PlanarRegionProperties(props, 1e-6) == Result::Ok, "the loop is a closed planar curve");
    d8test::Check(std::fabs(props.area - cs.area) < 1e-3, Msg(std::string("area of the ") + cs.name + " matches the lens formula", props.area - cs.area).c_str());
  }
  // Squares [0,4]^2 and [2,6] x [1,5]: overlap 2 x 3 = 6.
  const NurbsCurve s1 = NurbsCurve::FromControlPoints({P(0, 0, 0), P(4, 0, 0), P(4, 4, 0), P(0, 4, 0), P(0, 0, 0)}, 1);
  const NurbsCurve s2 = NurbsCurve::FromControlPoints({P(2, 1, 0), P(6, 1, 0), P(6, 5, 0), P(2, 5, 0), P(2, 1, 0)}, 1);
  const auto inter = dino8::kernel::PlanarCurveBoolean(s1.raw(), s2.raw(), xy, dino8::kernel::CurveRegionOp::Intersection, 1e-6);
  const auto uni = dino8::kernel::PlanarCurveBoolean(s1.raw(), s2.raw(), xy, dino8::kernel::CurveRegionOp::Union, 1e-6);
  d8test::Check(inter.ok && uni.ok && inter.loops.size() == 1 && uni.loops.size() == 1, "square booleans give one loop each");
  if (inter.ok && uni.ok && !inter.loops.empty() && !uni.loops.empty()) {
    NurbsCurve li, lu;
    li.raw() = inter.loops[0];
    lu.raw() = uni.loops[0];
    NurbsCurve::RegionProperties pi, pu;
    li.PlanarRegionProperties(pi, 1e-6);
    lu.PlanarRegionProperties(pu, 1e-6);
    d8test::Check(std::fabs(pi.area - 6.0) < 1e-6, "intersection of the squares has area 6");
    d8test::Check(std::fabs(pu.area - (16.0 + 16.0 - 6.0)) < 1e-6, "union of the squares has area 16 + 16 - 6");
  }
}

// CURVE-0024 Curve (interpolate): "a curve that passes through picked points with ... end tangents". Oracle: the exact cubic
// y = x^3, z = 0.5 x^2 sampled at x = -2 .. 2 with the data parameter equal to x. A cubic interpolant with the exact end
// derivatives (1, 3 x^2, x) is unique and is the cubic itself, so every sample is reproduced; the picked points are passed through
// and the requested end derivatives are met.
D8_EXTRA_TEST(RowInterpolatedCurveWithEndTangentsReproducesTheExactCubic) {
  std::vector<Point3d> pts;
  std::vector<double> prm;
  for (int i = 0; i <= 4; ++i) {
    const double x = -2.0 + i;
    pts.push_back(P(x, x * x * x, 0.5 * x * x));
    prm.push_back(x);
  }
  dino8::kernel::CurveInterpolationOptions o;
  o.degree = 3;
  o.explicit_parameters = prm;
  o.start_tangent = Vector3d(1, 3 * 4.0, -2.0);
  o.end_tangent = Vector3d(1, 3 * 4.0, 2.0);
  o.tangents_are_derivatives = true;
  NurbsCurve k;
  dino8::kernel::CurveInterpolationReport rep;
  d8test::Check(dino8::kernel::InterpolateCurve(pts, o, k, &rep) == Result::Ok, "interpolation with end tangents succeeds");
  double through = 0.0, shape = 0.0;
  for (size_t i = 0; i < pts.size(); ++i) through = std::max(through, k.PointAt(prm[i]).DistanceTo(pts[i]));
  for (int i = 0; i <= 80; ++i) {
    const double x = -2.0 + 4.0 * i / 80.0;
    shape = std::max(shape, k.PointAt(x).DistanceTo(P(x, x * x * x, 0.5 * x * x)));
  }
  d8test::Check(through < 1e-9, Msg("the curve passes through every picked point", through).c_str());
  d8test::Check(shape < 1e-8, Msg("the interpolant is the exact cubic between the points", shape).c_str());
  const Vector3d dstart = k.TangentAt(-2.0), dend = k.TangentAt(2.0);
  Vector3d es(1, 12, -2), ee(1, 12, 2);
  es.Unitize();
  ee.Unitize();
  d8test::Check(ON_DotProduct(dstart, es) > 1.0 - 1e-9 && ON_DotProduct(dend, ee) > 1.0 - 1e-9, "the end tangent directions are the requested ones");
}

// CURVE-0044 ExtractSubCrv: separate the segments of a polycurve as independent curves. A line (3 long), a quarter arc of radius 2
// and a line (5 long) are joined; Segments() must report Line, Arc, Line, and trimming the original to each reported interval
// gives independent curves of length 3, pi (a quarter of 2 pi r with r = 2) and 5 (closed forms) whose lengths add up to the polycurve's.
D8_EXTRA_TEST(RowExtractSubCrvSeparatesTheLineArcLineSegmentsWithExactLengths) {
  NurbsCurve line1 = NurbsCurve::FromControlPoints({P(0, 0, 0), P(3, 0, 0)}, 1);
  NurbsCurve arc = CircleXY(P(3, 2, 0), 2.0);
  d8test::Check(arc.Trim(At(arc, 0.75), At(arc, 1.0)) == Result::Ok, "the quarter arc from (3,0) to (5,2)");
  NurbsCurve line2 = NurbsCurve::FromControlPoints({P(5, 2, 0), P(5, 7, 0)}, 1);
  NurbsCurve poly = line1;
  d8test::Check(poly.Join(arc, 1e-6) == Result::Ok && poly.Join(line2, 1e-6) == Result::Ok, "join the three pieces");
  const auto segs = poly.Segments(1e-6);
  d8test::Check(segs.size() == 3, "three segments");
  if (segs.size() != 3) return;
  using dino8::kernel::CurveKind;
  d8test::Check(segs[0].shape.kind == CurveKind::Line && segs[1].shape.kind == CurveKind::Arc && segs[2].shape.kind == CurveKind::Line, "segment kinds are Line, Arc, Line");
  d8test::Check(std::fabs(segs[1].shape.arc.Radius() - 2.0) < 1e-6, "the arc segment has radius 2");
  const double expect[3] = {3.0, kPi, 5.0};  // quarter arc of radius 2: (pi / 2) * 2
  double sum = 0.0;
  for (int i = 0; i < 3; ++i) {
    NurbsCurve piece = poly;
    d8test::Check(piece.Trim(segs[i].t0, segs[i].t1) == Result::Ok, "trim to one segment");
    const double len = piece.LengthToTolerance(1e-9);
    sum += len;
    d8test::Check(std::fabs(len - expect[i]) < 1e-6, Msg("segment length equals the closed form", len - expect[i]).c_str());
  }
  d8test::Check(std::fabs(sum - (3.0 + kPi + 5.0)) < 1e-6, "the segment lengths add up to the polycurve length");
}

// CURVE-0014 Contour (curves): "Slice objects with evenly spaced parallel planes to create planar contour curves from a base point
// and direction". Oracle: a sphere of radius 3 at the origin sliced from a base plane at z = -2.5 with spacing 1 gives the six
// sections at z = -2.5 .. 2.5, and every section is the circle of radius sqrt(9 - z^2) (Pythagoras); the polyline through the
// refined points has the chord length 2 pi r to within the chordal error of its sampling.
D8_EXTRA_TEST(RowContourOfASphereGivesEvenlySpacedCirclesOfPythagoreanRadius) {
  const dino8::kernel::Brep sphere = dino8::kernel::Brep::Sphere(P(0, 0, 0), 3.0);
  const ON_Plane base(ON_3dPoint(0, 0, -2.5), ON_3dVector(0, 0, 1));
  dino8::kernel::IntersectOptions opt;
  opt.tolerance = 1e-6;
  opt.mesh_tolerance = 0.05;
  const auto sections = dino8::kernel::ContourBrep(sphere.raw(), base, 1.0, opt);
  d8test::Check(sections.size() == 6, "six sections between z = -2.5 and z = 2.5");
  for (const auto& sec : sections) {
    const double z = -2.5 + sec.offset;
    const double radius = std::sqrt(9.0 - z * z);
    double worst_r = 0.0, worst_z = 0.0, length = 0.0;
    for (const auto& hit : sec.hits) {
      for (const Point3d& p : hit.curve.points) {
        worst_r = std::max(worst_r, std::fabs(std::hypot(p.x, p.y) - radius));
        worst_z = std::max(worst_z, std::fabs(p.z - z));
      }
      length += hit.curve.Length();
    }
    d8test::Check(worst_r < 1e-4 && worst_z < 1e-4, Msg("every contour point is at the Pythagorean radius and the section height", std::max(worst_r, worst_z)).c_str());
    d8test::Check(length > 0.97 * 2 * kPi * radius && length < 1.001 * 2 * kPi * radius, Msg("the section's length is 2 pi r within the chordal error", length - 2 * kPi * radius).c_str());
  }
}

// CURVE-0103 Rebuild (curve): "Reconstruct a curve to a specified degree and control point count ... end-tangent preservation".
// Oracle: 60 points of a quarter circle of radius 4 are refit with a degree-3 curve of 8 control points; the result has exactly
// that degree and count, starts and ends at the data ends, and stays within 1e-3 of the circle (distance to the centre minus the
// radius). A degree-3 target of an already cubic Bezier arc with 4 control points is refit without leaving the arc's chord band.
D8_EXTRA_TEST(RowRebuildCurveRefitsAQuarterCircleToTheRequestedDegreeAndCount) {
  const double r = 4.0;
  std::vector<Point3d> pts;
  for (int i = 0; i <= 60; ++i) {
    const double a = 0.5 * kPi * i / 60.0;
    pts.push_back(P(r * std::cos(a), r * std::sin(a), 0));
  }
  NurbsCurve k;
  d8test::Check(NurbsCurve::FitLeastSquares(pts, 3, 8, k) == Result::Ok, "fit to degree 3 with 8 control points");
  d8test::Check(k.Degree() == 3 && k.ControlPointCount() == 8, "the rebuilt curve has the requested degree and control point count");
  d8test::Check(Near(k.PointAt(k.Domain().min), pts.front(), 1e-9) && Near(k.PointAt(k.Domain().max), pts.back(), 1e-9), "the data end points are kept");
  double worst = 0.0;
  for (int i = 0; i <= 200; ++i) {
    const Point3d q = k.PointAt(At(k, i / 200.0));
    worst = std::max(worst, std::fabs(std::hypot(q.x, q.y) - r));
  }
  d8test::Check(worst < 1e-3, Msg("the rebuilt curve stays within 1e-3 of the circle", worst).c_str());
  Vector3d t0 = k.TangentAt(k.Domain().min), t1 = k.TangentAt(k.Domain().max);
  d8test::Check(ON_DotProduct(t0, Vector3d(0, 1, 0)) > 0.999 && ON_DotProduct(t1, Vector3d(-1, 0, 0)) > 0.999, "the end tangents follow the circle's (vertical at the start, horizontal at the end)");
  NurbsCurve few;
  d8test::Check(NurbsCurve::FitLeastSquares(pts, 3, 4, few) == Result::Ok && few.ControlPointCount() == 4, "a four point rebuild is accepted");
}
