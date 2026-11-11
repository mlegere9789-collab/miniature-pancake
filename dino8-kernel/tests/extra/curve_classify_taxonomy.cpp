// NurbsCurve::Classify() / NurbsCurve::Segments(): the typed view of a NURBS curve (line / arc / circle / ellipse /
// polyline / general) with its closed-form parameters, and the composite (poly)curve decomposition.
//
// Oracles are the closed forms the test builds the NURBS from by hand, not anything the classifier computes:
//   * a circle is the standard 9-control-point rational quadratic (weights 1, sqrt(2)/2, ...), an arc of angle a is the
//     3-control-point form with the middle point at r / cos(a/2) and weight cos(a/2) (Piegl & Tiller, 7.3);
//   * an ellipse is the affine image of that circle (an affine map commutes with rational B-spline evaluation);
//   * a polyline is a degree-1 curve through stated vertices; a line is any curve whose control points are collinear;
//   * a "line - quarter arc - line" outline is one degree-2 rational NURBS with C0 knots (multiplicity 2) at the joints.
// Negatives: the cubic Bezier quarter-circle (kappa = 0.5522847498, 2.7e-4 off the circle) and a parabola are General at
// 1e-9, and a circle with one weight changed is General.
#include <cmath>
#include <cstdio>
#include <vector>

#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::CurveClassification;
using dino8::kernel::CurveKind;
using dino8::kernel::CurveSegment;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;
const double kTol = 1e-9;

struct Frame {
  Point3d o;
  Vector3d x, y, z;
  Point3d At(double a, double b, double c = 0.0) const { return o + a * x + b * y + c * z; }
};

// An arbitrary right-handed orthonormal frame (not axis aligned).
Frame TiltedFrame() {
  Vector3d x(1.0, 2.0, -0.5), z(0.3, -0.4, 1.0);
  x.Unitize();
  z = z - ON_DotProduct(z, x) * x;
  z.Unitize();
  Vector3d y = ON_CrossProduct(z, x);
  return Frame{Point3d(1.5, -2.0, 0.75), x, y, z};
}

// Rational quadratic NURBS from control points / weights / knots (ON convention: count + order - 2 knots).
NurbsCurve Rational(const std::vector<Point3d>& cv, const std::vector<double>& w, const std::vector<double>& knots, int degree) {
  NurbsCurve c;
  ON_NurbsCurve& r = c.raw();
  r.Create(3, true, degree + 1, static_cast<int>(cv.size()));
  for (size_t i = 0; i < cv.size(); ++i) r.SetCV(static_cast<int>(i), ON_4dPoint(cv[i].x * w[i], cv[i].y * w[i], cv[i].z * w[i], w[i]));
  for (size_t i = 0; i < knots.size(); ++i) r.SetKnot(static_cast<int>(i), knots[i]);
  return c;
}

// Full circle of radius r in `f`, traced counter-clockwise about f.z, as 4 quadratic arcs, with x/y control scales (a, b)
// so that (1, 1) is the circle and (a, b) its affine image, an ellipse.
NurbsCurve CircleNet(const Frame& f, double rx, double ry, double w_override = -1.0) {
  const double s = std::sqrt(0.5);
  const double px[9] = {1, 1, 0, -1, -1, -1, 0, 1, 1};
  const double py[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
  std::vector<Point3d> cv;
  std::vector<double> w;
  for (int i = 0; i < 9; ++i) {
    cv.push_back(f.At(rx * px[i], ry * py[i]));
    w.push_back(i % 2 == 1 ? s : 1.0);
  }
  if (w_override > 0.0) w[3] = w_override;
  return Rational(cv, w, {0, 0, 1, 1, 2, 2, 3, 3, 4, 4}, 2);
}

// Arc of angle `a` from angle 0 about f.z, radius r: 3 control points, middle at r / cos(a/2), weight cos(a/2).
NurbsCurve ArcNet(const Frame& f, double r, double a) {
  const double h = std::cos(0.5 * a);
  const std::vector<Point3d> cv = {f.At(r, 0), f.At(r, r * std::tan(0.5 * a)), f.At(r * std::cos(a), r * std::sin(a))};
  return Rational(cv, {1.0, h, 1.0}, {0, 0, 1, 1}, 2);
}

bool Near(const Point3d& a, const Point3d& b, double t = 1e-9) { return a.DistanceTo(b) < t; }

}  // namespace

D8_EXTRA_TEST(CurveClassifyCircleAndArcRecoverTheirClosedFormParameters) {
  const Frame f = TiltedFrame();
  {
    const CurveClassification k = CircleNet(f, 2.5, 2.5).Classify(kTol);
    d8test::Check(k.kind == CurveKind::Circle, "the 9-point rational quadratic circle classifies as Circle");
    d8test::Check(Near(k.arc.Center(), f.o) && std::fabs(k.arc.Radius() - 2.5) < 1e-9, "circle: centre and radius are the ones it was built from");
    d8test::Check((k.arc.Plane().zaxis - f.z).Length() < 1e-9, "circle: plane normal is the direction it is traced counter-clockwise about");
    d8test::Check(std::fabs(k.arc.AngleRadians() - 2.0 * kPi) < 1e-9, "circle: swept angle is 2 pi");
  }
  for (double deg : {40.0, 90.0, 120.0, 200.0}) {
    const double a = deg * kPi / 180.0;
    const NurbsCurve c = ArcNet(f, 1.75, a);
    const CurveClassification k = c.Classify(kTol);
    char tag[96];
    std::snprintf(tag, sizeof tag, "%g degree arc classifies as Arc", deg);
    d8test::Check(k.kind == CurveKind::Arc, tag);
    if (k.kind != CurveKind::Arc) continue;
    std::snprintf(tag, sizeof tag, "%g degree arc: centre, radius, swept angle match the construction", deg);
    d8test::Check(Near(k.arc.Center(), f.o) && std::fabs(k.arc.Radius() - 1.75) < 1e-9 && std::fabs(k.arc.AngleRadians() - a) < 1e-9, tag);
    std::snprintf(tag, sizeof tag, "%g degree arc: start and end points are the closed-form points", deg);
    d8test::Check(Near(k.arc.StartPoint(), f.At(1.75, 0)) && Near(k.arc.EndPoint(), f.At(1.75 * std::cos(a), 1.75 * std::sin(a))), tag);
  }
}

D8_EXTRA_TEST(CurveClassifyLineEllipsePolylineAndGeneral) {
  const Frame f = TiltedFrame();
  {  // a degree-3 Bezier whose four control points are collinear and unevenly spaced is a line
    const Point3d p0(1, 2, 3), d(2, -1, 0.5);
    NurbsCurve c = NurbsCurve::FromControlPoints({p0, p0 + 0.2 * d, p0 + 0.9 * d, p0 + d}, 3);
    const CurveClassification k = c.Classify(kTol);
    d8test::Check(k.kind == CurveKind::Line && Near(k.line.from, p0) && Near(k.line.to, p0 + d), "collinear cubic control points: a Line from the first to the last point");
  }
  {  // the affine image of the circle is the ellipse
    const CurveClassification k = CircleNet(f, 3.0, 1.5).Classify(kTol);
    d8test::Check(k.kind == CurveKind::Ellipse, "the affine image of the circle (3 x 1.5) classifies as Ellipse, not Circle");
    if (k.kind == CurveKind::Ellipse) {
      const double hi = std::max(k.ellipse.radius[0], k.ellipse.radius[1]), lo = std::min(k.ellipse.radius[0], k.ellipse.radius[1]);
      d8test::Check(std::fabs(hi - 3.0) < 1e-9 && std::fabs(lo - 1.5) < 1e-9, "ellipse: the two radii are 3 and 1.5");
      d8test::Check(Near(k.ellipse.plane.origin, f.o), "ellipse: centre is the construction centre");
      const Vector3d major = k.ellipse.radius[0] > k.ellipse.radius[1] ? k.ellipse.plane.xaxis : k.ellipse.plane.yaxis;
      d8test::Check(std::fabs(std::fabs(ON_DotProduct(major, f.x)) - 1.0) < 1e-9, "ellipse: the major axis is the stretched frame axis");
    }
  }
  {  // polyline: degree 1, an exactly repeated vertex is dropped
    const std::vector<Point3d> v = {Point3d(0, 0, 0), Point3d(2, 0, 0), Point3d(2, 1, 0), Point3d(2, 1, 0), Point3d(3, 3, 1)};
    const CurveClassification k = NurbsCurve::FromControlPoints(v, 1).Classify(kTol);
    d8test::Check(k.kind == CurveKind::Polyline && k.vertices.size() == 4, "degree-1 curve through 5 points with one repeat: a Polyline of 4 vertices");
    if (k.kind == CurveKind::Polyline && k.vertices.size() == 4) {
      d8test::Check(Near(k.vertices[0], v[0]) && Near(k.vertices[1], v[1]) && Near(k.vertices[2], v[2]) && Near(k.vertices[3], v[4]), "polyline vertices are the data points in order");
    }
    d8test::Check(NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(1, 1, 1), Point3d(3, 3, 3)}, 1).Classify(kTol).kind == CurveKind::Line,
                  "a degree-1 curve through collinear points is a Line, not a Polyline");
  }
  {  // negatives
    const double kappa = 0.5522847498307936;
    NurbsCurve bez = NurbsCurve::FromControlPoints({Point3d(1, 0, 0), Point3d(1, kappa, 0), Point3d(kappa, 1, 0), Point3d(0, 1, 0)}, 3);
    d8test::Check(bez.Classify(kTol).kind == CurveKind::General, "the cubic Bezier quarter circle (2.7e-4 off a circle) is General at 1e-9");
    NurbsCurve parabola = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(1, 2, 0), Point3d(2, 0, 0)}, 2);
    d8test::Check(parabola.Classify(kTol).kind == CurveKind::General, "a parabola is General");
    d8test::Check(CircleNet(f, 2.0, 2.0, 0.6).Classify(kTol).kind == CurveKind::General, "a circle with one weight changed (0.6 for sqrt(1/2)) is General");
    d8test::Check(NurbsCurve().Classify(kTol).kind == CurveKind::General, "an empty curve is General and does not throw");
  }
}

D8_EXTRA_TEST(CurveSegmentsDecomposeACompositeOutline) {
  const Frame f{Point3d(0, 0, 0), Vector3d(1, 0, 0), Vector3d(0, 1, 0), Vector3d(0, 0, 1)};
  // line (0,0)-(2,0), quarter arc about (2,1) r 1 to (3,1), line (3,1)-(3,4): one degree-2 rational curve, C0 knots.
  const double s = std::sqrt(0.5);
  const std::vector<Point3d> cv = {Point3d(0, 0, 0), Point3d(1, 0, 0), Point3d(2, 0, 0), Point3d(3, 0, 0), Point3d(3, 1, 0), Point3d(3, 2.5, 0), Point3d(3, 4, 0)};
  const NurbsCurve outline = Rational(cv, {1, 1, 1, s, 1, 1, 1}, {0, 0, 1, 1, 2, 2, 3, 3}, 2);
  d8test::Check(outline.raw().IsValid(), "setup: the line-arc-line outline is a valid NURBS curve");
  const std::vector<CurveSegment> seg = outline.Segments(kTol);
  d8test::Check(seg.size() == 3, "line-arc-line outline: 3 segments");
  if (seg.size() == 3) {
    d8test::Check(seg[0].shape.kind == CurveKind::Line && seg[1].shape.kind == CurveKind::Arc && seg[2].shape.kind == CurveKind::Line, "segment kinds are Line, Arc, Line");
    d8test::Check(std::fabs(seg[0].t0 - 0.0) < 1e-12 && std::fabs(seg[0].t1 - 1.0) < 1e-12 && std::fabs(seg[1].t1 - 2.0) < 1e-12 && std::fabs(seg[2].t1 - 3.0) < 1e-12,
                  "segment intervals tile the domain at the C0 knots 1 and 2");
    d8test::Check(Near(seg[0].shape.line.from, Point3d(0, 0, 0)) && Near(seg[0].shape.line.to, Point3d(2, 0, 0)), "first segment is the line (0,0)-(2,0)");
    d8test::Check(Near(seg[1].shape.arc.Center(), Point3d(2, 1, 0)) && std::fabs(seg[1].shape.arc.Radius() - 1.0) < 1e-9 && std::fabs(seg[1].shape.arc.AngleRadians() - 0.5 * kPi) < 1e-9,
                  "second segment is the quarter arc about (2,1), radius 1");
    d8test::Check(Near(seg[2].shape.line.from, Point3d(3, 1, 0)) && Near(seg[2].shape.line.to, Point3d(3, 4, 0)), "third segment is the line (3,1)-(3,4)");
  }
  // A circle recognized whole stays one segment although its knot vector has three C0 breaks.
  const std::vector<CurveSegment> ring = CircleNet(TiltedFrame(), 1.0, 1.0).Segments(kTol);
  d8test::Check(ring.size() == 1 && ring[0].shape.kind == CurveKind::Circle && std::fabs(ring[0].t0 - 0.0) < 1e-12 && std::fabs(ring[0].t1 - 4.0) < 1e-12,
                "the 4-arc circle is a single Circle segment over [0, 4]");
  // A degree-1 curve gives one Line per edge.
  const std::vector<CurveSegment> edges = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(1, 0, 0), Point3d(1, 2, 0), Point3d(4, 2, 1)}, 1).Segments(kTol);
  d8test::Check(edges.size() == 3 && edges[0].shape.kind == CurveKind::Line && edges[1].shape.kind == CurveKind::Line && edges[2].shape.kind == CurveKind::Line,
                "a 4-vertex polyline is 3 Line segments");
  // A smooth free-form curve is one General segment.
  const std::vector<CurveSegment> free = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(1, 2, 0), Point3d(3, -1, 1), Point3d(4, 1, 0), Point3d(6, 0, 0)}, 3).Segments(kTol);
  d8test::Check(free.size() == 1 && free[0].shape.kind == CurveKind::General, "a smooth free-form cubic is one General segment");
}
