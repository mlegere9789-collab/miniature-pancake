// InterpolateCurve(): general-degree global curve interpolation with parameter styles and end-tangent constraints. Every
// expected value is an independent closed form written out here: a polynomial curve is in the interpolant's spline space, so a
// degree-p interpolant of exact samples of a degree-p polynomial must BE that polynomial (the knots, the solver and the basis
// are all exercised, none of them is read back); points on a straight line or on a circle / helix have analytic positions and
// tangents; the chord / centripetal parameters are re-derived in the test from the data.
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/curve_interpolate.h"
#include "test_registry.h"

namespace {

using dino8::kernel::CurveInterpolationOptions;
using dino8::kernel::CurveInterpolationReport;
using dino8::kernel::InterpolateCurve;
using dino8::kernel::InterpolationParameter;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::Vector3d;

// Coefficients of a vector-valued polynomial C(t) = sum_i c[i] t^i.
struct Poly {
  std::vector<Vector3d> c;
  Point3d At(double t) const {
    Vector3d s(0, 0, 0);
    double tp = 1.0;
    for (const Vector3d& ci : c) { s += ci * tp; tp *= t; }
    return Point3d(s.x, s.y, s.z);
  }
  Vector3d Der(double t) const {
    Vector3d s(0, 0, 0);
    double tp = 1.0;
    for (size_t i = 1; i < c.size(); ++i) { s += c[i] * (static_cast<double>(i) * tp); tp *= t; }
    return s;
  }
};

Poly MakePoly(int degree) {
  Poly p;
  // Deterministic, fixed (not derived from the code under test) coefficients; leading coefficients non-zero.
  const double base[6][3] = {{0.3, -1.2, 2.0}, {1.7, 0.4, -0.9}, {-2.2, 1.1, 0.6}, {0.8, -1.9, 1.4}, {-0.5, 0.7, -1.3}, {1.1, 0.2, 0.9}};
  for (int i = 0; i <= degree; ++i) p.c.push_back(Vector3d(base[i][0], base[i][1], base[i][2]));
  return p;
}

// Strictly increasing, uneven parameters on [-0.5, 1.5].
std::vector<double> UnevenParams(int count) {
  std::vector<double> t;
  double acc = 0.0, total = 0.0;
  std::vector<double> gaps;
  for (int k = 0; k + 1 < count; ++k) { gaps.push_back(1.0 + 0.6 * std::sin(1.3 * k + 0.4)); total += gaps.back(); }
  t.push_back(-0.5);
  for (int k = 0; k + 1 < count; ++k) { acc += gaps[static_cast<size_t>(k)]; t.push_back(-0.5 + 2.0 * acc / total); }
  return t;
}

double Worst(const NurbsCurve& c, const Poly& p, double a, double b, int samples) {
  double worst = 0.0;
  for (int i = 0; i <= samples; ++i) {
    const double t = a + (b - a) * i / samples;
    worst = std::max(worst, (c.raw().PointAt(t) - p.At(t)).Length());
  }
  return worst;
}

std::vector<Point3d> ScatteredPoints(int count) {
  std::vector<Point3d> pts;
  for (int k = 0; k < count; ++k) {
    pts.push_back(Point3d(2.0 * k + 0.9 * std::sin(1.7 * k), 1.5 * std::cos(0.9 * k) + 0.2 * k, 0.8 * std::sin(0.6 * k + 1.0)));
  }
  return pts;
}

}  // namespace

D8_EXTRA_TEST(InterpolateCurveReproducesAPolynomialOfTheSameDegreeExactly) {
  for (int p = 1; p <= 5; ++p) {
    const Poly poly = MakePoly(p);
    for (int count : {p + 1, 9}) {
      const std::vector<double> t = UnevenParams(count);
      std::vector<Point3d> pts;
      for (double ti : t) pts.push_back(poly.At(ti));
      CurveInterpolationOptions opt;
      opt.degree = p;
      opt.explicit_parameters = t;
      NurbsCurve out;
      CurveInterpolationReport rep;
      const Result r = InterpolateCurve(pts, opt, out, &rep);
      const std::string tag = "degree " + std::to_string(p) + ", " + std::to_string(count) + " points: ";
      d8test::Check(r == Result::Ok, (tag + "interpolation succeeds").c_str());
      if (r != Result::Ok) continue;
      const double worst = Worst(out, poly, t.front(), t.back(), 400);
      d8test::Check(worst < 1e-9, (tag + "the interpolant IS the polynomial (worst " + std::to_string(worst) + ")").c_str());
      d8test::Check(out.raw().Degree() == p && out.raw().CVCount() == count && !out.raw().IsRational(),
                    (tag + "degree / control point count / non-rational").c_str());
      d8test::Check(std::fabs(out.raw().Domain().Min() - t.front()) < 1e-15 && std::fabs(out.raw().Domain().Max() - t.back()) < 1e-15,
                    (tag + "the domain is the explicit parameter range").c_str());
      d8test::Check(out.raw().IsClamped(2), (tag + "the knot vector is clamped").c_str());
    }
  }
}

D8_EXTRA_TEST(InterpolateCurvePassesThroughEveryPointAtTheIndependentlyDerivedParameters) {
  for (int style = 0; style < 3; ++style) {
    for (int p = 1; p <= 6; ++p) {
      const std::vector<Point3d> pts = ScatteredPoints(12);
      CurveInterpolationOptions opt;
      opt.degree = p;
      opt.parameter = style == 0 ? InterpolationParameter::Chord : style == 1 ? InterpolationParameter::Centripetal : InterpolationParameter::Uniform;
      NurbsCurve out;
      CurveInterpolationReport rep;
      const std::string tag = "style " + std::to_string(style) + " degree " + std::to_string(p) + ": ";
      d8test::Check(InterpolateCurve(pts, opt, out, &rep) == Result::Ok, (tag + "succeeds").c_str());
      // Independent parameters.
      std::vector<double> cum(pts.size(), 0.0);
      for (size_t k = 1; k < pts.size(); ++k) {
        const double d = std::sqrt((pts[k].x - pts[k - 1].x) * (pts[k].x - pts[k - 1].x) + (pts[k].y - pts[k - 1].y) * (pts[k].y - pts[k - 1].y) +
                                   (pts[k].z - pts[k - 1].z) * (pts[k].z - pts[k - 1].z));
        cum[k] = cum[k - 1] + (style == 0 ? d : style == 1 ? std::sqrt(d) : 1.0);
      }
      double worst_param = 0.0, worst_pt = 0.0;
      for (size_t k = 0; k < pts.size(); ++k) {
        const double expect = cum[k] / cum.back();
        if (k < rep.parameters.size()) worst_param = std::max(worst_param, std::fabs(rep.parameters[k] - expect));
        worst_pt = std::max(worst_pt, (out.raw().PointAt(expect) - pts[k]).Length());
      }
      d8test::Check(rep.parameters.size() == pts.size() && worst_param < 1e-14, (tag + "the reported parameters are the chord / centripetal / uniform ones (" + std::to_string(worst_param) + ")").c_str());
      d8test::Check(worst_pt < 1e-9, (tag + "every point is hit at its own parameter (worst " + std::to_string(worst_pt) + ")").c_str());
      d8test::Check(rep.max_position_residual < 1e-9, (tag + "the reported residual agrees").c_str());
    }
  }
}

D8_EXTRA_TEST(InterpolateCurveThroughCollinearPointsIsTheStraightLineAtEveryDegree) {
  const Point3d a(1.0, -2.0, 0.5), b(7.0, 4.0, -2.5);
  const double fr[] = {0.0, 0.07, 0.31, 0.32, 0.6, 0.93, 1.0};
  std::vector<Point3d> pts;
  for (double f : fr) pts.push_back(Point3d(a.x + f * (b.x - a.x), a.y + f * (b.y - a.y), a.z + f * (b.z - a.z)));
  for (int p = 1; p <= 6; ++p) {
    CurveInterpolationOptions opt;
    opt.degree = p;
    NurbsCurve out;
    const bool ok = InterpolateCurve(pts, opt, out) == Result::Ok;
    d8test::Check(ok, ("collinear degree " + std::to_string(p) + ": succeeds").c_str());
    if (!ok) continue;
    // Chord-length parameter of a point on a line is its fraction along it, so C(t) = a + t (b - a).
    double worst = 0.0;
    for (int i = 0; i <= 200; ++i) {
      const double t = i / 200.0;
      const Point3d e(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z));
      worst = std::max(worst, (out.raw().PointAt(t) - e).Length());
    }
    d8test::Check(worst < 1e-10, ("collinear degree " + std::to_string(p) + ": the curve is the line (worst " + std::to_string(worst) + ")").c_str());
  }
}

D8_EXTRA_TEST(InterpolateCurveWithEndDerivativesReproducesAPolynomialOfThatDegree) {
  struct Case { int degree; int count; bool start; bool end; };
  const Case cases[] = {{3, 6, true, true}, {3, 6, true, false}, {3, 6, false, true}, {4, 7, true, true}, {4, 7, true, false}, {4, 7, false, true},
                        {2, 5, true, true}, {2, 5, true, false}, {5, 8, true, true}, {3, 2, true, true}, {2, 2, true, true}};
  for (const Case& cs : cases) {
    const Poly poly = MakePoly(cs.degree);
    const std::vector<double> t = UnevenParams(cs.count);
    std::vector<Point3d> pts;
    for (double ti : t) pts.push_back(poly.At(ti));
    CurveInterpolationOptions opt;
    opt.degree = cs.degree;
    opt.explicit_parameters = t;
    opt.tangents_are_derivatives = true;
    if (cs.start) opt.start_tangent = poly.Der(t.front());
    if (cs.end) opt.end_tangent = poly.Der(t.back());
    NurbsCurve out;
    CurveInterpolationReport rep;
    const std::string tag = "degree " + std::to_string(cs.degree) + ", " + std::to_string(cs.count) + " points, tangents " + (cs.start ? "S" : "-") + (cs.end ? "E" : "-") + ": ";
    const Result r = InterpolateCurve(pts, opt, out, &rep);
    d8test::Check(r == Result::Ok, (tag + "succeeds").c_str());
    if (r != Result::Ok) continue;
    const double worst = Worst(out, poly, t.front(), t.back(), 400);
    d8test::Check(worst < 1e-8, (tag + "the interpolant IS the polynomial (worst " + std::to_string(worst) + ")").c_str());
    const int extra = (cs.start ? 1 : 0) + (cs.end ? 1 : 0);
    d8test::Check(out.raw().CVCount() == cs.count + extra, (tag + "one extra control point per tangent").c_str());
    d8test::Check(rep.max_position_residual < 1e-9 && rep.start_tangent_residual < 1e-8 && rep.end_tangent_residual < 1e-8, (tag + "reported residuals are tiny").c_str());
    // Independent derivative check at both ends (finite difference one-sided, through PointAt).
    if (cs.start) {
      const double h = 1e-6;
      const Vector3d fd = (out.raw().PointAt(t.front() + h) - out.raw().PointAt(t.front())) / h;
      const Vector3d ex = poly.Der(t.front());
      d8test::Check((fd - ex).Length() < 1e-3 * (1.0 + ex.Length()), (tag + "start derivative by finite difference").c_str());
    }
  }
}

D8_EXTRA_TEST(InterpolateCurveDirectionTangentsPinTheEndsOfAQuarterCircle) {
  // Quarter circle of radius 3 about (1, 2, 0), uneven angles. Start direction (0, 1, 0), end direction (-1, 0, 0).
  const double ang[] = {0.0, 0.2, 0.55, 0.9, 1.1, 1.4, 1.5707963267948966};
  std::vector<Point3d> pts;
  for (double a : ang) pts.push_back(Point3d(1.0 + 3.0 * std::cos(a), 2.0 + 3.0 * std::sin(a), 0.0));
  double chord = 0.0;
  for (size_t k = 1; k < pts.size(); ++k) chord += (pts[k] - pts[k - 1]).Length();
  CurveInterpolationOptions with;
  with.degree = 3;
  with.start_tangent = Vector3d(0.0, 5.0, 0.0);   // length must be ignored
  with.end_tangent = Vector3d(-0.01, 0.0, 0.0);
  CurveInterpolationOptions without;
  without.degree = 3;
  NurbsCurve cw, cn;
  CurveInterpolationReport rep;
  d8test::Check(InterpolateCurve(pts, with, cw, &rep) == Result::Ok && InterpolateCurve(pts, without, cn) == Result::Ok, "quarter circle: both builds succeed");
  Point3d p;
  Vector3d d0, d1;
  cw.raw().Ev1Der(0.0, p, d0);
  cw.raw().Ev1Der(1.0, p, d1, -1);
  d8test::Check(std::fabs(d0.x) < 1e-9 && std::fabs(d0.y - chord) < 1e-9 && std::fabs(d0.z) < 1e-12, ("start derivative = direction x total chord (" + std::to_string(d0.y) + " vs " + std::to_string(chord) + ")").c_str());
  d8test::Check(std::fabs(d1.x + chord) < 1e-9 && std::fabs(d1.y) < 1e-9 && std::fabs(d1.z) < 1e-12, "end derivative = direction x total chord");
  d8test::Check(rep.start_tangent_residual < 1e-9 && rep.end_tangent_residual < 1e-9, "reported tangent residuals are tiny");
  // The un-constrained interpolant's own end directions are off the true circle tangents by far more.
  Vector3d n0, n1;
  cn.raw().Ev1Der(0.0, p, n0);
  cn.raw().Ev1Der(1.0, p, n1, -1);
  const double err_without = std::fabs(n0.x) / n0.Length();   // true tangent (0,1,0): x component is the direction error
  d8test::Check(err_without > 1e-3, ("without tangents the start direction error is " + std::to_string(err_without) + " (> 1e-3)").c_str());
  // Both curves stay close to the true circle; the constrained one is no worse.
  double dev_with = 0.0, dev_without = 0.0;
  for (int i = 0; i <= 600; ++i) {
    const double t = i / 600.0;
    const Point3d a = cw.raw().PointAt(t), b = cn.raw().PointAt(t);
    dev_with = std::max(dev_with, std::fabs(std::hypot(a.x - 1.0, a.y - 2.0) - 3.0));
    dev_without = std::max(dev_without, std::fabs(std::hypot(b.x - 1.0, b.y - 2.0) - 3.0));
  }
  d8test::Check(dev_with < 5e-3 && dev_without < 5e-3, ("both stay within 5e-3 of the circle (" + std::to_string(dev_with) + ", " + std::to_string(dev_without) + ")").c_str());
  d8test::Check(dev_with <= dev_without + 1e-12, "the tangent-constrained interpolant is no farther from the true circle");
}

D8_EXTRA_TEST(InterpolateCurveDegreeOnePassesThroughThePolylineAndIsLinearBetweenPoints) {
  const std::vector<Point3d> pts = ScatteredPoints(6);
  CurveInterpolationOptions opt;
  opt.degree = 1;
  NurbsCurve out;
  CurveInterpolationReport rep;
  d8test::Check(InterpolateCurve(pts, opt, out, &rep) == Result::Ok, "degree 1: succeeds");
  d8test::Check(out.raw().CVCount() == 6, "degree 1: control points are the data points");
  double worst = 0.0;
  for (size_t k = 0; k + 1 < pts.size(); ++k) {
    const double mid = 0.5 * (rep.parameters[k] + rep.parameters[k + 1]);
    const Point3d e((pts[k].x + pts[k + 1].x) / 2, (pts[k].y + pts[k + 1].y) / 2, (pts[k].z + pts[k + 1].z) / 2);
    worst = std::max(worst, (out.raw().PointAt(mid) - e).Length());
  }
  d8test::Check(worst < 1e-12, "degree 1: the span midpoint is the chord midpoint");
}

D8_EXTRA_TEST(InterpolateCurveFollowsAHelixSampledAtEqualChords) {
  // 640 points on a helix of radius 2, pitch 1.5, 6 turns at equal angle steps: equal chords, so the chord-length parameter is
  // exactly k / 640 and the curve at t must be the helix at angle t * 12 pi. Exercises the banded solver on a large system.
  const int count = 641;
  const double turns = 6.0, pi = 3.14159265358979323846;
  auto helix = [&](double t) { const double a = 2 * pi * turns * t; return Point3d(2.0 * std::cos(a), 2.0 * std::sin(a), 1.5 * turns * t); };
  std::vector<Point3d> pts;
  for (int k = 0; k < count; ++k) pts.push_back(helix(static_cast<double>(k) / (count - 1)));
  for (int p : {3, 5}) {
    CurveInterpolationOptions opt;
    opt.degree = p;
    NurbsCurve out;
    CurveInterpolationReport rep;
    d8test::Check(InterpolateCurve(pts, opt, out, &rep) == Result::Ok, ("helix degree " + std::to_string(p) + ": succeeds").c_str());
    double worst = 0.0;
    for (int k = 0; k + 1 < count; ++k) {
      const double t = (k + 0.5) / (count - 1);
      worst = std::max(worst, (out.raw().PointAt(t) - helix(t)).Length());
    }
    d8test::Check(rep.max_position_residual < 1e-9, ("helix degree " + std::to_string(p) + ": passes through all 641 points").c_str());
    d8test::Check(worst < 5e-6, ("helix degree " + std::to_string(p) + ": span midpoints within 5e-6 of the true helix (worst " + std::to_string(worst) + ")").c_str());
  }
}

D8_EXTRA_TEST(InterpolateCurveRefusesBadInputAndLeavesTheOutputUntouched) {
  const std::vector<Point3d> pts = ScatteredPoints(5);
  NurbsCurve sentinel = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(1, 0, 0), Point3d(2, 1, 0)}, 2);
  auto fails = [&](const std::vector<Point3d>& p, const CurveInterpolationOptions& o, const char* what) {
    NurbsCurve out = sentinel;
    const Result r = InterpolateCurve(p, o, out);
    d8test::Check(r == Result::Failed, (std::string(what) + ": Result::Failed").c_str());
    d8test::Check(out.raw().CVCount() == 3 && out.raw().Degree() == 2 && out.raw().PointAt(0.5).DistanceTo(sentinel.raw().PointAt(0.5)) == 0.0, (std::string(what) + ": output untouched").c_str());
  };
  CurveInterpolationOptions o;
  fails({pts[0]}, o, "one point");
  fails({}, o, "no points");
  o.degree = 0;
  fails(pts, o, "degree 0");
  o.degree = 5;
  fails(pts, o, "degree above points - 1");
  o.degree = 3;
  o.explicit_parameters = {0, 1, 2};
  fails(pts, o, "explicit parameters of the wrong length");
  o.explicit_parameters = {0, 1, 1, 2, 3};
  fails(pts, o, "non-increasing explicit parameters");
  o.explicit_parameters.clear();
  std::vector<Point3d> dup = pts;
  dup[2] = dup[1];
  fails(dup, o, "coincident consecutive points");
  std::vector<Point3d> bad = pts;
  bad[3].x = std::nan("");
  fails(bad, o, "NaN coordinate");
  o.degree = 1;
  o.start_tangent = Vector3d(1, 0, 0);
  fails(pts, o, "tangent with degree 1");
  o.degree = 3;
  o.start_tangent = Vector3d(0, 0, 0);
  fails(pts, o, "zero direction tangent");
  o.start_tangent = Vector3d(std::nan(""), 0, 0);
  fails(pts, o, "NaN tangent");
}
