// PlanarCurveBoolean: exact union / intersection / difference of two simple closed planar curves bounded by arcs, ellipses, lines.
//
// Independent oracles - closed forms, never the function's own output re-read through itself:
//   * two circles (radii r1, r2, centre distance d): lens area r1^2 a1 + r2^2 a2 - 1/2 sqrt((-d+r1+r2)(d+r1-r2)(d-r1+r2)(d+r1+r2))
//     with a1 = acos((d^2 + r1^2 - r2^2) / (2 d r1)) (a2 symmetric); lens perimeter 2 r1 a1 + 2 r2 a2; the crossings are at
//     x = (d^2 + r1^2 - r2^2) / (2 d), y = +-sqrt(r1^2 - x^2) from the first centre;
//   * a circle r = 2.5 about the centre of a 4 x 4 square: each side is a chord at distance h = 2, a circular segment
//     r^2 acos(h / r) - h sqrt(r^2 - h^2); the circle minus the square is FOUR separate caps, the square minus the circle FOUR corners;
//   * a circle r = 2 and an ellipse a = 3, b = 1.5 on one centre cross at y^2 = 5/3, x^2 = 7/3; the intersection is two circle
//     sectors of angle 2 phi (tan phi = y/x) and two ellipse sectors of eccentric angle pi - 2 t0 (cos t0 = x / a): the area is
//     2 r^2 phi + a b (pi - 2 t0);
//   * concentric circles: a ring (outer CCW + inner CW hole) of area pi (R^2 - r^2).
// Area and length of the OUTPUT loops are measured by dense sampling (shoelace / chord sum) - independent of the construction.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/planar_curve_boolean.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::CurveRegionOp;
using dino8::kernel::PlanarCurveBoolean;
using dino8::kernel::PlanarCurveBooleanResult;

constexpr double kPi = 3.14159265358979323846;

ON_Plane XY() { return ON_Plane::World_xy; }

// A plane whose x/y axes are rotated out of the world frame, normal (1, 2, 2) / 3 through (1, -2, 3).
ON_Plane Tilted() {
  const ON_3dVector n = ON_3dVector(1, 2, 2) / 3.0;
  ON_3dVector x = ON_3dVector(2, 1, -2) / 3.0;  // dot(n, x) = (2 + 2 - 4) / 9 = 0
  ON_3dVector y = ON_CrossProduct(n, x);
  return ON_Plane(ON_3dPoint(1, -2, 3), x, y);
}

ON_NurbsCurve Circle(const ON_Plane& pl, double cx, double cy, double r, bool clockwise = false) {
  ON_Plane p = pl;
  p.SetOrigin(pl.PointAt(cx, cy));
  ON_NurbsCurve c;
  ON_Circle(p, r).GetNurbForm(c);
  if (clockwise) c.Reverse();
  return c;
}

ON_NurbsCurve Ellipse(const ON_Plane& pl, double cx, double cy, double a, double b) {
  ON_Plane p = pl;
  p.SetOrigin(pl.PointAt(cx, cy));
  ON_NurbsCurve c;
  ON_Ellipse(p, a, b).GetNurbForm(c);
  return c;
}

ON_PolylineCurve Rect(const ON_Plane& pl, double x0, double y0, double x1, double y1) {
  ON_3dPointArray pts;
  pts.Append(pl.PointAt(x0, y0));
  pts.Append(pl.PointAt(x1, y0));
  pts.Append(pl.PointAt(x1, y1));
  pts.Append(pl.PointAt(x0, y1));
  pts.Append(pl.PointAt(x0, y0));
  return ON_PolylineCurve(pts);
}

// Sample points of a curve: every knot span separately, so a corner of the result (a span boundary where two pieces were joined)
// is itself a sample - a chord across a corner would lose length proportional to the sampling step.
std::vector<ON_3dPoint> SamplePoints(const ON_Curve& c, int per_span) {
  const int spans = c.SpanCount();
  std::vector<double> sv(spans + 1);
  c.GetSpanVector(sv.data());
  std::vector<ON_3dPoint> pts;
  for (int s = 0; s < spans; ++s)
    for (int k = 0; k < per_span; ++k) pts.push_back(c.PointAt(sv[s] + (sv[s + 1] - sv[s]) * k / per_span));
  pts.push_back(c.PointAt(sv[spans]));
  return pts;
}

// Signed area (counterclockwise about plane.zaxis = positive) and length of a closed loop by dense sampling.
void Measure(const ON_Curve& c, const ON_Plane& pl, double& signed_area, double& length, int per_span = 10000) {
  signed_area = 0;
  length = 0;
  const std::vector<ON_3dPoint> pts = SamplePoints(c, per_span);
  for (size_t i = 1; i < pts.size(); ++i) {
    double u0, v0, u1, v1;
    pl.ClosestPointTo(pts[i - 1], &u0, &v0);
    pl.ClosestPointTo(pts[i], &u1, &v1);
    signed_area += 0.5 * (u0 * v1 - u1 * v0);
    length += pts[i - 1].DistanceTo(pts[i]);
  }
}

struct Totals {
  double area = 0;    // sum of signed loop areas (outer positive, hole negative)
  double length = 0;  // sum of loop lengths
  int outer = 0, holes = 0;
  bool orientation_matches_flags = true;
};

Totals Sum(const PlanarCurveBooleanResult& r, const ON_Plane& pl) {
  Totals t;
  for (size_t i = 0; i < r.loops.size(); ++i) {
    double a, l;
    Measure(r.loops[i], pl, a, l);
    t.area += a;
    t.length += l;
    if (r.loop_is_hole[i]) {
      ++t.holes;
      if (a >= 0) t.orientation_matches_flags = false;
    } else {
      ++t.outer;
      if (a <= 0) t.orientation_matches_flags = false;
    }
  }
  return t;
}

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

void CheckLoopsClosedAndOn(const PlanarCurveBooleanResult& r, const ON_Plane& pl, const char* tag) {
  bool closed = true;
  double worst_off_plane = 0;
  for (const ON_NurbsCurve& c : r.loops) {
    if (c.PointAtStart().DistanceTo(c.PointAtEnd()) > 1e-6) closed = false;
    for (int i = 0; i <= 50; ++i) worst_off_plane = std::max(worst_off_plane, std::fabs(pl.DistanceTo(c.PointAt(c.Domain().ParameterAt(i / 50.0)))));
  }
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%s: every loop closes up (end = start within 1e-6)", tag);
  d8test::Check(closed, buf);
  std::snprintf(buf, sizeof(buf), "%s: every loop stays in the plane (worst %.2g)", tag, worst_off_plane);
  d8test::Check(worst_off_plane < 1e-6, buf);
}

double MinDistToLoops(const PlanarCurveBooleanResult& r, const ON_3dPoint& p) {
  double best = 1e300;
  for (const ON_NurbsCurve& c : r.loops)
    for (const ON_3dPoint& q : SamplePoints(c, 2000)) best = std::min(best, q.DistanceTo(p));
  return best;
}

struct Lens {
  double area, perimeter, a1, a2, x, y;
};
Lens LensOf(double r1, double r2, double d) {
  Lens l;
  l.a1 = std::acos((d * d + r1 * r1 - r2 * r2) / (2 * d * r1));
  l.a2 = std::acos((d * d + r2 * r2 - r1 * r1) / (2 * d * r2));
  l.area = r1 * r1 * l.a1 + r2 * r2 * l.a2 - 0.5 * std::sqrt((-d + r1 + r2) * (d + r1 - r2) * (d - r1 + r2) * (d + r1 + r2));
  l.perimeter = 2 * r1 * l.a1 + 2 * r2 * l.a2;
  l.x = (d * d + r1 * r1 - r2 * r2) / (2 * d);
  l.y = std::sqrt(r1 * r1 - l.x * l.x);
  return l;
}

void TwoCircles(const ON_Plane& pl, const char* tag, bool a_clockwise, bool b_clockwise) {
  const double r1 = 3, r2 = 2, d = 4;
  const Lens L = LensOf(r1, r2, d);
  const ON_NurbsCurve A = Circle(pl, 0, 0, r1, a_clockwise), B = Circle(pl, d, 0, r2, b_clockwise);
  const double areaA = kPi * r1 * r1, areaB = kPi * r2 * r2;
  char buf[200];

  const PlanarCurveBooleanResult inter = PlanarCurveBoolean(A, B, pl, CurveRegionOp::Intersection);
  std::snprintf(buf, sizeof(buf), "%s: intersection accepted (%s)", tag, inter.refusal_reason.c_str());
  d8test::Check(inter.ok, buf);
  d8test::Check(inter.crossings == 2, "two overlapping circles cross exactly twice");
  d8test::Check(inter.loops.size() == 1, "lens: one loop");
  const Totals ti = Sum(inter, pl);
  std::snprintf(buf, sizeof(buf), "%s: lens area %.6f vs closed form %.6f", tag, ti.area, L.area);
  d8test::Check(Near(ti.area, L.area, 1e-5), buf);
  std::snprintf(buf, sizeof(buf), "%s: lens perimeter %.6f vs closed form %.6f", tag, ti.length, L.perimeter);
  d8test::Check(Near(ti.length, L.perimeter, 1e-5), buf);
  d8test::Check(ti.outer == 1 && ti.holes == 0 && ti.orientation_matches_flags, "lens is one counterclockwise outer loop");
  CheckLoopsClosedAndOn(inter, pl, tag);
  d8test::Check(MinDistToLoops(inter, pl.PointAt(L.x, L.y)) < 1e-5 && MinDistToLoops(inter, pl.PointAt(L.x, -L.y)) < 1e-5,
                "lens corners are the closed-form crossing points (2.625, +-1.4524)");

  const PlanarCurveBooleanResult uni = PlanarCurveBoolean(A, B, pl, CurveRegionOp::Union);
  d8test::Check(uni.ok && uni.loops.size() == 1, "union of two overlapping circles is one loop");
  const Totals tu = Sum(uni, pl);
  std::snprintf(buf, sizeof(buf), "%s: union area %.6f vs %.6f", tag, tu.area, areaA + areaB - L.area);
  d8test::Check(Near(tu.area, areaA + areaB - L.area, 1e-5), buf);
  const double union_perimeter = 2 * kPi * r1 - 2 * r1 * L.a1 + 2 * kPi * r2 - 2 * r2 * L.a2;
  std::snprintf(buf, sizeof(buf), "%s: union perimeter %.6f vs %.6f", tag, tu.length, union_perimeter);
  d8test::Check(Near(tu.length, union_perimeter, 1e-5), buf);

  const PlanarCurveBooleanResult amb = PlanarCurveBoolean(A, B, pl, CurveRegionOp::Difference);
  const Totals ta = Sum(amb, pl);
  d8test::Check(amb.ok && amb.loops.size() == 1 && ta.orientation_matches_flags, "A - B (a crescent) is one outer loop");
  std::snprintf(buf, sizeof(buf), "%s: A - B area %.6f vs %.6f", tag, ta.area, areaA - L.area);
  d8test::Check(Near(ta.area, areaA - L.area, 1e-5), buf);
  const double amb_perimeter = 2 * r1 * (kPi - L.a1) + 2 * r2 * L.a2;
  d8test::Check(Near(ta.length, amb_perimeter, 1e-5), "A - B perimeter: outer arc of A + inner arc of B");

  const PlanarCurveBooleanResult bma = PlanarCurveBoolean(B, A, pl, CurveRegionOp::Difference);
  const Totals tb = Sum(bma, pl);
  d8test::Check(bma.ok && bma.loops.size() == 1, "B - A is one loop");
  std::snprintf(buf, sizeof(buf), "%s: B - A area %.6f vs %.6f", tag, tb.area, areaB - L.area);
  d8test::Check(Near(tb.area, areaB - L.area, 1e-5), buf);
}

}  // namespace

D8_EXTRA_TEST(PlanarCurveBooleanTwoCirclesLensWorldXY) { TwoCircles(XY(), "xy", false, false); }

D8_EXTRA_TEST(PlanarCurveBooleanTwoCirclesInTiltedPlaneAndReversedInputs) {
  TwoCircles(Tilted(), "tilted, one input clockwise", true, false);
  TwoCircles(Tilted(), "tilted, both inputs clockwise", true, true);
}

D8_EXTRA_TEST(PlanarCurveBooleanCircleAndSquareGiveFourCapsAndFourCorners) {
  const ON_Plane pl = XY();
  const double r = 2.5, h = 2.0;
  const double segment = r * r * std::acos(h / r) - h * std::sqrt(r * r - h * h);
  const double inter_area = kPi * r * r - 4 * segment;
  const ON_NurbsCurve C = Circle(pl, 0, 0, r);
  const ON_PolylineCurve S = Rect(pl, -2, -2, 2, 2);
  char buf[200];

  const PlanarCurveBooleanResult inter = PlanarCurveBoolean(C, S, pl, CurveRegionOp::Intersection);
  d8test::Check(inter.ok && inter.crossings == 8 && inter.loops.size() == 1, "circle x square: 8 crossings, intersection is one loop");
  const Totals ti = Sum(inter, pl);
  std::snprintf(buf, sizeof(buf), "circle x square intersection area %.6f vs %.6f", ti.area, inter_area);
  d8test::Check(Near(ti.area, inter_area, 1e-5), buf);

  const PlanarCurveBooleanResult uni = PlanarCurveBoolean(C, S, pl, CurveRegionOp::Union);
  d8test::Check(uni.ok && uni.loops.size() == 1, "circle x square union is one loop");
  d8test::Check(Near(Sum(uni, pl).area, kPi * r * r + 16 - inter_area, 1e-5), "union area = circle + square - intersection");

  const PlanarCurveBooleanResult caps = PlanarCurveBoolean(C, S, pl, CurveRegionOp::Difference);
  const Totals tc = Sum(caps, pl);
  std::snprintf(buf, sizeof(buf), "circle - square: %zu loops (want 4 caps)", caps.loops.size());
  d8test::Check(caps.ok && caps.loops.size() == 4 && tc.holes == 0, buf);
  d8test::Check(Near(tc.area, 4 * segment, 1e-5), "circle - square area = 4 circular segments");
  // Each cap is bounded by one straight side (length 2 sqrt(r^2 - h^2) = 3) and one circular arc of angle 2 acos(h / r).
  d8test::Check(Near(tc.length, 4 * (2 * std::sqrt(r * r - h * h)) + 4 * r * 2 * std::acos(h / r), 1e-4),
                "circle - square perimeter = 4 chords + 4 arcs");

  const PlanarCurveBooleanResult corners = PlanarCurveBoolean(S, C, pl, CurveRegionOp::Difference);
  const Totals tk = Sum(corners, pl);
  d8test::Check(corners.ok && corners.loops.size() == 4 && tk.holes == 0, "square - circle: four corner pieces");
  d8test::Check(Near(tk.area, 16 - inter_area, 1e-5), "square - circle area = 16 - intersection");
}

D8_EXTRA_TEST(PlanarCurveBooleanCircleAndEllipseOnOneCentre) {
  const ON_Plane pl = XY();
  const double r = 2, a = 3, b = 1.5;
  const double x = std::sqrt(7.0 / 3.0), y = std::sqrt(5.0 / 3.0);
  const double phi = std::atan2(y, x), t0 = std::acos(x / a);
  const double area = 2 * r * r * phi + a * b * (kPi - 2 * t0);
  const ON_NurbsCurve C = Circle(pl, 0, 0, r), E = Ellipse(pl, 0, 0, a, b);
  char buf[200];
  const PlanarCurveBooleanResult inter = PlanarCurveBoolean(C, E, pl, CurveRegionOp::Intersection);
  d8test::Check(inter.ok && inter.crossings == 4 && inter.loops.size() == 1, "circle x ellipse: 4 crossings, one intersection loop");
  const Totals ti = Sum(inter, pl);
  std::snprintf(buf, sizeof(buf), "circle x ellipse intersection area %.6f vs closed form %.6f", ti.area, area);
  d8test::Check(Near(ti.area, area, 1e-5), buf);
  const PlanarCurveBooleanResult uni = PlanarCurveBoolean(C, E, pl, CurveRegionOp::Union);
  d8test::Check(uni.ok && Near(Sum(uni, pl).area, kPi * r * r + kPi * a * b - area, 1e-5), "union area = pi r^2 + pi a b - intersection");
  // C - E leaves two caps of the circle poking out past the ellipse's short axis (top and bottom).
  const PlanarCurveBooleanResult cme = PlanarCurveBoolean(C, E, pl, CurveRegionOp::Difference);
  d8test::Check(cme.ok && cme.loops.size() == 2 && Near(Sum(cme, pl).area, kPi * r * r - area, 1e-5),
                "circle - ellipse: two caps, area pi r^2 - intersection");
  // E - C leaves two tips of the ellipse past the circle along its long axis.
  const PlanarCurveBooleanResult emc = PlanarCurveBoolean(E, C, pl, CurveRegionOp::Difference);
  d8test::Check(emc.ok && emc.loops.size() == 2 && Near(Sum(emc, pl).area, kPi * a * b - area, 1e-5),
                "ellipse - circle: two tips, area pi a b - intersection");
}

D8_EXTRA_TEST(PlanarCurveBooleanNestedAndDisjointOperands) {
  const ON_Plane pl = XY();
  const ON_NurbsCurve big = Circle(pl, 0, 0, 3), small = Circle(pl, 0.5, 0, 1), far_away = Circle(pl, 10, 0, 1);
  const double ring = kPi * (9 - 1);
  PlanarCurveBooleanResult r = PlanarCurveBoolean(big, small, pl, CurveRegionOp::Difference);
  Totals t = Sum(r, pl);
  d8test::Check(r.ok && r.crossings == 0 && r.loops.size() == 2 && t.outer == 1 && t.holes == 1, "disc - inner disc: outer loop plus a hole");
  d8test::Check(Near(t.area, ring, 1e-5), "ring area = pi (R^2 - r^2), the hole counted negative");
  r = PlanarCurveBoolean(small, big, pl, CurveRegionOp::Difference);
  d8test::Check(r.ok && r.loops.empty(), "inner disc - outer disc is empty");
  r = PlanarCurveBoolean(big, small, pl, CurveRegionOp::Union);
  d8test::Check(r.ok && r.loops.size() == 1 && Near(Sum(r, pl).area, kPi * 9, 1e-5), "union of nested discs is the outer disc");
  r = PlanarCurveBoolean(big, small, pl, CurveRegionOp::Intersection);
  d8test::Check(r.ok && r.loops.size() == 1 && Near(Sum(r, pl).area, kPi, 1e-5), "intersection of nested discs is the inner disc");
  r = PlanarCurveBoolean(small, big, pl, CurveRegionOp::Intersection);
  d8test::Check(r.ok && r.loops.size() == 1 && Near(Sum(r, pl).area, kPi, 1e-5), "intersection is symmetric in the nested case");
  r = PlanarCurveBoolean(big, far_away, pl, CurveRegionOp::Union);
  d8test::Check(r.ok && r.loops.size() == 2 && Near(Sum(r, pl).area, kPi * 10, 1e-5), "union of disjoint discs is two loops");
  r = PlanarCurveBoolean(big, far_away, pl, CurveRegionOp::Intersection);
  d8test::Check(r.ok && r.loops.empty(), "intersection of disjoint discs is empty");
  r = PlanarCurveBoolean(big, far_away, pl, CurveRegionOp::Difference);
  d8test::Check(r.ok && r.loops.size() == 1 && Near(Sum(r, pl).area, kPi * 9, 1e-5), "difference of disjoint discs is the first disc");
}

D8_EXTRA_TEST(PlanarCurveBooleanRefusesWhatItCannotClassify) {
  const ON_Plane pl = XY();
  const ON_NurbsCurve A = Circle(pl, 0, 0, 3);
  // Externally tangent (d = r1 + r2) and internally tangent (d = r1 - r2): a touch without a crossing.
  PlanarCurveBooleanResult r = PlanarCurveBoolean(A, Circle(pl, 5, 0, 2), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && r.loops.empty() && !r.refusal_reason.empty(), "externally tangent circles are refused with a reason");
  r = PlanarCurveBoolean(A, Circle(pl, 1, 0, 2), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && !r.refusal_reason.empty(), "internally tangent circles are refused with a reason");
  // A gap far below the tolerance: the boundaries "touch" without a crossing, and no crossing is ever reported for them.
  r = PlanarCurveBoolean(A, Circle(pl, 5.000005, 0, 2), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && r.crossings == 0 && r.refusal_reason.find("touch") != std::string::npos,
                "circles 5e-6 apart (inside 10 x tolerance) are refused as touching, not united as disjoint");
  // The same circle twice: a whole shared boundary.
  r = PlanarCurveBoolean(A, Circle(pl, 0, 0, 3), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && !r.refusal_reason.empty(), "coincident circles are refused with a reason");
  // Squares sharing a whole edge, a partial edge, and one corner.
  const ON_PolylineCurve sq = Rect(pl, 0, 0, 4, 4);
  r = PlanarCurveBoolean(sq, Rect(pl, 4, 0, 8, 4), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && !r.refusal_reason.empty(), "squares sharing a whole edge are refused with a reason");
  r = PlanarCurveBoolean(sq, Rect(pl, 4, 1, 8, 3), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && !r.refusal_reason.empty(), "squares sharing part of an edge are refused with a reason");
  r = PlanarCurveBoolean(sq, Rect(pl, 2, 0, 6, 4), pl, CurveRegionOp::Intersection);
  d8test::Check(!r.ok && !r.refusal_reason.empty(), "overlapping squares with a collinear edge are refused with a reason");
  r = PlanarCurveBoolean(sq, Rect(pl, 4, 4, 8, 8), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && !r.refusal_reason.empty(), "squares touching at one corner are refused with a reason");
  // Open curve.
  ON_NurbsCurve arc;
  ON_Arc(ON_Circle(pl, 3), ON_Interval(0, 2.0)).GetNurbForm(arc);
  r = PlanarCurveBoolean(arc, A, pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && r.refusal_reason.find("closed") != std::string::npos, "an open curve is refused naming 'closed'");
  // A circle not in the plane.
  ON_Plane lifted = pl;
  lifted.SetOrigin(ON_3dPoint(0, 0, 1));
  r = PlanarCurveBoolean(Circle(lifted, 0, 0, 3), Circle(pl, 1, 0, 2), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && r.refusal_reason.find("plane") != std::string::npos, "a curve off the plane is refused naming the plane");
  // A figure eight crosses itself.
  ON_3dPointArray pts;
  pts.Append(ON_3dPoint(0, 0, 0));
  pts.Append(ON_3dPoint(2, 2, 0));
  pts.Append(ON_3dPoint(2, 0, 0));
  pts.Append(ON_3dPoint(0, 2, 0));
  pts.Append(ON_3dPoint(0, 0, 0));
  r = PlanarCurveBoolean(ON_PolylineCurve(pts), Circle(pl, 10, 10, 1), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && r.refusal_reason.find("self") != std::string::npos, "a self-crossing operand is refused naming the self-crossing");
}

// A square inscribed in a circle: the boundaries meet at the four corners without crossing. Union (the circle) and intersection (the
// square) are unambiguous; the circle MINUS the square would be four separate segments that touch at the corners, which the chaining
// cannot tell apart from one ring plus one hole - refused naming the ambiguity instead of guessed.
D8_EXTRA_TEST(PlanarCurveBooleanInscribedSquareKeepsTheUnambiguousResults) {
  const ON_Plane pl = XY();
  const double R = std::sqrt(2.0);
  const ON_NurbsCurve C = Circle(pl, 0, 0, R);
  const ON_PolylineCurve S = Rect(pl, -1, -1, 1, 1);
  PlanarCurveBooleanResult r = PlanarCurveBoolean(C, S, pl, CurveRegionOp::Union);
  d8test::Check(r.ok && r.loops.size() == 1 && Near(Sum(r, pl).area, 2 * kPi, 1e-5), "circle U inscribed square = the circle (area 2 pi)");
  d8test::Check(Near(Sum(r, pl).length, 2 * kPi * R, 1e-4), "...and its boundary is the circle's circumference 2 pi sqrt 2, no square edge in it");
  r = PlanarCurveBoolean(C, S, pl, CurveRegionOp::Intersection);
  d8test::Check(r.ok && r.loops.size() == 1 && Near(Sum(r, pl).area, 4.0, 1e-5) && Near(Sum(r, pl).length, 8.0, 1e-6),
                "circle n inscribed square = the square (area 4, perimeter 8)");
  r = PlanarCurveBoolean(S, C, pl, CurveRegionOp::Difference);
  d8test::Check(r.ok && r.loops.empty(), "square - circumscribed circle is empty");
  r = PlanarCurveBoolean(C, S, pl, CurveRegionOp::Difference);
  d8test::Check(!r.ok && r.refusal_reason.find("ambiguous") != std::string::npos, "circle - inscribed square is refused naming the ambiguous junction");
}
