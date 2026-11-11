// NurbsCurve::ReduceDegree(): a curve that IS a degree-elevated lower-degree curve comes back exactly.
//
// Oracles are independent of the reducer:
//   * a polynomial curve given by explicit monomial coefficients written in this file (one Bezier span, degree 3), built as
//     the cubic Bezier whose control points follow from those coefficients, elevated to degree 6 and reduced to 3: the
//     reduced curve must equal the polynomial at 400 parameters (1e-12) with 4 control points;
//   * a spline built by hand from control points and a non-uniform knot vector at degree 3 (and 2), elevated by one and
//     two degrees, reduced back: same knots, same control net (1e-10), same shape (1e-12), same control count;
//   * a rational quadratic quarter circle (weight sqrt(2)/2), elevated to 3 and reduced to 2: every point at distance r
//     from the centre (1e-12) and the 3 original control points;
//   * a C0 corner (interior knot of multiplicity = degree) survives the round trip;
//   * negatives: a genuine cubic spline cannot go to degree 2 within 1e-9 (refused, curve untouched, measured deviation
//     reported), and the old behaviour for a degree-1 target of a straight line stays.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

NurbsCurve Spline(int p, const std::vector<Point3d>& cv, const std::vector<double>& std_knots, const std::vector<double>* w = nullptr) {
  // std_knots: textbook array of cv.size() + p + 1 entries; ON stores it without the first and last.
  NurbsCurve c;
  ON_NurbsCurve& r = c.raw();
  r.Create(3, w != nullptr, p + 1, static_cast<int>(cv.size()));
  for (int k = 0; k < r.KnotCount(); ++k) r.SetKnot(k, std_knots[static_cast<size_t>(k + 1)]);
  for (size_t i = 0; i < cv.size(); ++i) {
    if (w) r.SetCV(static_cast<int>(i), ON_4dPoint(cv[i].x * (*w)[i], cv[i].y * (*w)[i], cv[i].z * (*w)[i], (*w)[i]));
    else r.SetCV(static_cast<int>(i), cv[i]);
  }
  return c;
}

double MaxShapeDiff(const NurbsCurve& a, const NurbsCurve& b) {
  double worst = 0.0;
  const double t0 = a.raw().Domain().Min(), t1 = a.raw().Domain().Max();
  for (int i = 0; i <= 400; ++i) {
    const double t = t0 + (t1 - t0) * i / 400.0;
    worst = std::max(worst, a.raw().PointAt(t).DistanceTo(b.raw().PointAt(t)));
  }
  return worst;
}

bool SameNet(const NurbsCurve& a, const NurbsCurve& b, double tol) {
  if (a.raw().CVCount() != b.raw().CVCount() || a.raw().KnotCount() != b.raw().KnotCount() || a.raw().Degree() != b.raw().Degree()) return false;
  for (int k = 0; k < a.raw().KnotCount(); ++k) {
    if (std::fabs(a.raw().Knot(k) - b.raw().Knot(k)) > tol) return false;
  }
  for (int i = 0; i < a.raw().CVCount(); ++i) {
    ON_3dPoint x, y;
    a.raw().GetCV(i, x);
    b.raw().GetCV(i, y);
    if (x.DistanceTo(y) > tol) return false;
  }
  return true;
}

}  // namespace

D8_EXTRA_TEST(CurveReduceDegreeRecoversAPolynomialFromItsElevation) {
  // C(t) = sum a_j t^j, t in [0, 1], cubic: the Bezier control points follow from the monomial coefficients.
  const double a[3][4] = {{0.3, 2.0, -1.5, 0.9}, {-0.8, 0.4, 3.0, -2.2}, {1.2, -1.0, 0.5, 1.7}};
  auto poly = [&](double t) { return Point3d(a[0][0] + t * (a[0][1] + t * (a[0][2] + t * a[0][3])), a[1][0] + t * (a[1][1] + t * (a[1][2] + t * a[1][3])), a[2][0] + t * (a[2][1] + t * (a[2][2] + t * a[2][3]))); };
  // Bezier from power basis: P0 = a0, P1 = a0 + a1/3, P2 = a0 + 2 a1 / 3 + a2 / 3, P3 = a0 + a1 + a2 + a3.
  std::vector<Point3d> cv(4);
  for (int k = 0; k < 3; ++k) {
    const double p0 = a[k][0], p1 = a[k][0] + a[k][1] / 3.0, p2 = a[k][0] + 2.0 * a[k][1] / 3.0 + a[k][2] / 3.0, p3 = a[k][0] + a[k][1] + a[k][2] + a[k][3];
    cv[0][k] = p0;
    cv[1][k] = p1;
    cv[2][k] = p2;
    cv[3][k] = p3;
  }
  NurbsCurve c = Spline(3, cv, {0, 0, 0, 0, 1, 1, 1, 1});
  double worst0 = 0.0;
  for (int i = 0; i <= 400; ++i) worst0 = std::max(worst0, c.raw().PointAt(i / 400.0).DistanceTo(poly(i / 400.0)));
  d8test::Check(worst0 < 1e-12, "setup: the hand-derived cubic Bezier equals the monomial polynomial");
  d8test::Check(c.ElevateDegree(6) == Result::Ok && c.Degree() == 6, "setup: elevated to degree 6");
  double dev = -1.0;
  const Result r = c.ReduceDegree(3, 1e-9, &dev);
  d8test::Check(r == Result::Ok && c.Degree() == 3, "ReduceDegree(3) succeeds on the degree-6 elevation of a cubic");
  d8test::Check(c.ControlPointCount() == 4, "the result has the cubic's 4 control points, not a refit's larger net");
  double worst = 0.0;
  for (int i = 0; i <= 400; ++i) worst = std::max(worst, c.raw().PointAt(c.raw().Domain().ParameterAt(i / 400.0)).DistanceTo(poly(i / 400.0)));
  std::printf("  polynomial deviation after elevate 3 -> 6 and reduce 6 -> 3: %.3e (reported %.3e)\n", worst, dev);
  d8test::Check(worst < 1e-12, "the reduced curve equals the original polynomial at 400 parameters to 1e-12");
  d8test::Check(dev >= 0.0 && dev < 1e-12, "the reported deviation is ~0");
}

D8_EXTRA_TEST(CurveReduceDegreeRecoversSplinesAndCorners) {
  const std::vector<Point3d> cv = {Point3d(0, 0, 0), Point3d(1, 2, 0.5), Point3d(2.5, 2.2, -0.4), Point3d(3.5, 0.3, 1.0), Point3d(5, -1, 0.2), Point3d(6.2, 1.1, -0.7), Point3d(7, 2.4, 0.9)};
  struct Case {
    int p;
    std::vector<double> knots;  // textbook array for cv.size() = 7 control points
    int up;                     // elevate by this many degrees
    const char* what;
  };
  const Case cases[] = {
      {3, {0, 0, 0, 0, 1.0, 1.7, 3.1, 4.0, 4.0, 4.0, 4.0}, 1, "cubic, non-uniform simple knots, elevate 1"},
      {3, {0, 0, 0, 0, 1.0, 1.7, 3.1, 4.0, 4.0, 4.0, 4.0}, 2, "cubic, non-uniform simple knots, elevate 2"},
      {2, {0, 0, 0, 1.0, 1.9, 2.4, 3.3, 4.0, 4.0, 4.0}, 1, "quadratic, non-uniform simple knots, elevate 1"},
      {3, {0, 0, 0, 0, 2.0, 2.0, 2.0, 4.0, 4.0, 4.0, 4.0}, 1, "cubic with a C0 corner (knot 2 of multiplicity 3)"},
  };
  for (const Case& cs : cases) {
    const NurbsCurve orig = Spline(cs.p, cv, cs.knots);
    const std::string tag = std::string(cs.what) + ": ";
    d8test::Check(orig.raw().IsValid(), (tag + "setup: valid curve").c_str());
    NurbsCurve c = orig;
    d8test::Check(c.ElevateDegree(cs.p + cs.up) == Result::Ok, (tag + "setup: elevated").c_str());
    const int elevated_cv = c.ControlPointCount();
    double dev = -1.0;
    const Result r = c.ReduceDegree(cs.p, 1e-9, &dev);
    d8test::Check(r == Result::Ok && c.Degree() == cs.p, (tag + "ReduceDegree back to the original degree succeeds").c_str());
    d8test::Check(c.ControlPointCount() == orig.ControlPointCount() && c.ControlPointCount() < elevated_cv, (tag + "control count is the original's, below the elevated count").c_str());
    d8test::Check(SameNet(c, orig, 1e-9), (tag + "knots and control points are the original's").c_str());
    const double sd = MaxShapeDiff(c, orig);
    std::printf("  %s shape deviation %.3e (reported %.3e)\n", cs.what, sd, dev);
    d8test::Check(sd < 1e-11 && dev < 1e-9, (tag + "shape equals the original to 1e-11").c_str());
  }
}

D8_EXTRA_TEST(CurveReduceDegreeRationalCircleAndRefusals) {
  const double s = std::sqrt(0.5), r = 2.5;
  const std::vector<Point3d> cv = {Point3d(r, 0, 0), Point3d(r, r, 0), Point3d(0, r, 0)};
  const std::vector<double> w = {1.0, s, 1.0};
  const NurbsCurve quarter = Spline(2, cv, {0, 0, 0, 1, 1, 1}, &w);
  NurbsCurve c = quarter;
  d8test::Check(c.ElevateDegree(3) == Result::Ok && c.IsRational(), "setup: the rational quarter circle elevated to degree 3");
  double dev = -1.0;
  const Result res = c.ReduceDegree(2, 1e-9, &dev);
  d8test::Check(res == Result::Ok && c.Degree() == 2 && c.ControlPointCount() == 3, "ReduceDegree(2) of the elevated rational arc gives back 3 control points");
  double worst = 0.0;
  for (int i = 0; i <= 400; ++i) {
    const double t = c.raw().Domain().ParameterAt(i / 400.0);
    worst = std::max(worst, std::fabs(c.raw().PointAt(t).DistanceTo(Point3d(0, 0, 0)) - r));
  }
  std::printf("  rational arc: max |radius error| %.3e\n", worst);
  d8test::Check(worst < 1e-12, "every point of the recovered arc is at distance r from the centre (1e-12)");
  d8test::Check(SameNet(c, quarter, 1e-9), "and the net is the original quarter circle's");

  // Negative: a genuine cubic spline does not fit degree 2 at 1e-9; refused, untouched, measured deviation reported.
  const std::vector<Point3d> g = {Point3d(0, 0, 0), Point3d(1, 2, 0.5), Point3d(2.5, 2.2, -0.4), Point3d(3.5, 0.3, 1.0), Point3d(5, -1, 0.2), Point3d(6.2, 1.1, -0.7), Point3d(7, 2.4, 0.9)};
  NurbsCurve cubic = Spline(3, g, {0, 0, 0, 0, 1.0, 1.7, 3.1, 4.0, 4.0, 4.0, 4.0});
  const NurbsCurve before = cubic;
  double d2 = -1.0;
  d8test::Check(cubic.ReduceDegree(2, 1e-9, &d2) == Result::Failed, "a genuine cubic spline is refused at degree 2 within 1e-9");
  d8test::Check(d2 > 1e-6 && SameNet(cubic, before, 0.0), "the refusal reports a real deviation and leaves the curve bit-identical");
}
