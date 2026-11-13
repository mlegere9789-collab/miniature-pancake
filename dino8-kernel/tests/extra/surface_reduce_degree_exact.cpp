// NurbsSurface::ReduceDegree(): a surface that IS a degree-elevated lower-degree surface comes back exactly, per direction.
//
// Oracles are independent of the reducer:
//   * a polynomial surface (u, v, f(u, v)) with f = sum c_ab u^a v^b, a <= 2, b <= 3, written as explicit monomial
//     coefficients; its Bezier net follows by hand from the power -> Bernstein conversion
//     b_i = sum_{a <= i} C(i, a) / C(n, a) c_a applied in both directions. Elevated to degree (5, 5) and reduced back in V and
//     then U: equal to the polynomial at 21 x 21 parameters (1e-12), with the original 3 x 4 net;
//   * a hand-built non-uniform spline surface of degree (3, 2), 6 x 5 control points: elevated by 1 in U and 2 in V and reduced
//     back: the same knot vectors, the same net (1e-9), the same shape (1e-11);
//   * a rational biquadratic-by-linear quarter cylinder (circle weights 1, sqrt(2)/2, 1): elevated to (3, 3) and reduced back,
//     every point at distance r from the axis (1e-12) and the original net;
//   * negative: a genuine wavy bicubic is refused at degree 2 within 1e-9, untouched, with a measured deviation.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

NurbsSurface Make(int pu, int pv, int nu, int nv, const std::vector<double>& ku, const std::vector<double>& kv, const std::vector<Point3d>& cv,
                  const std::vector<double>* w = nullptr) {
  // ku / kv: textbook knot arrays (nu + pu + 1 and nv + pv + 1 entries); cv is u-major: cv[i * nv + j].
  NurbsSurface s;
  ON_NurbsSurface& r = s.raw();
  r.Create(3, w != nullptr, pu + 1, pv + 1, nu, nv);
  for (int k = 0; k < r.KnotCount(0); ++k) r.SetKnot(0, k, ku[static_cast<size_t>(k + 1)]);
  for (int k = 0; k < r.KnotCount(1); ++k) r.SetKnot(1, k, kv[static_cast<size_t>(k + 1)]);
  for (int i = 0; i < nu; ++i) {
    for (int j = 0; j < nv; ++j) {
      const Point3d& p = cv[static_cast<size_t>(i * nv + j)];
      if (w) {
        const double ww = (*w)[static_cast<size_t>(i * nv + j)];
        r.SetCV(i, j, ON_4dPoint(p.x * ww, p.y * ww, p.z * ww, ww));
      } else {
        r.SetCV(i, j, p);
      }
    }
  }
  return s;
}

double MaxShapeDiff(const NurbsSurface& a, const NurbsSurface& b) {
  double worst = 0.0;
  const ON_Interval du = a.raw().Domain(0), dv = a.raw().Domain(1);
  for (int i = 0; i <= 30; ++i) {
    for (int j = 0; j <= 30; ++j) {
      const double u = du.ParameterAt(i / 30.0), v = dv.ParameterAt(j / 30.0);
      worst = std::max(worst, a.raw().PointAt(u, v).DistanceTo(b.raw().PointAt(u, v)));
    }
  }
  return worst;
}

bool SameNet(const NurbsSurface& a, const NurbsSurface& b, double tol) {
  const ON_NurbsSurface &x = a.raw(), &y = b.raw();
  if (x.CVCount(0) != y.CVCount(0) || x.CVCount(1) != y.CVCount(1) || x.Degree(0) != y.Degree(0) || x.Degree(1) != y.Degree(1)) return false;
  for (int d = 0; d < 2; ++d) {
    for (int k = 0; k < x.KnotCount(d); ++k) {
      if (std::fabs(x.Knot(d, k) - y.Knot(d, k)) > tol) return false;
    }
  }
  for (int i = 0; i < x.CVCount(0); ++i) {
    for (int j = 0; j < x.CVCount(1); ++j) {
      ON_3dPoint p, q;
      x.GetCV(i, j, p);
      y.GetCV(i, j, q);
      if (p.DistanceTo(q) > tol) return false;
    }
  }
  return true;
}

double Binom(int n, int k) {
  double r = 1.0;
  for (int i = 1; i <= k; ++i) r = r * (n - k + i) / i;
  return r;
}

}  // namespace

D8_EXTRA_TEST(SurfaceReduceDegreeRecoversAPolynomialSurface) {
  const int pu = 2, pv = 3;
  double c[3][4];  // f = sum c[a][b] u^a v^b
  for (int a = 0; a <= pu; ++a) {
    for (int b = 0; b <= pv; ++b) c[a][b] = std::sin(1.3 * a + 0.7 * b + 0.4) * (1.0 + 0.3 * a - 0.2 * b);
  }
  auto f = [&](double u, double v) {
    double s = 0.0;
    for (int a = 0; a <= pu; ++a)
      for (int b = 0; b <= pv; ++b) s += c[a][b] * std::pow(u, a) * std::pow(v, b);
    return s;
  };
  std::vector<Point3d> cv;
  for (int i = 0; i <= pu; ++i) {
    for (int j = 0; j <= pv; ++j) {
      double z = 0.0;
      for (int a = 0; a <= i; ++a)
        for (int b = 0; b <= j; ++b) z += Binom(i, a) / Binom(pu, a) * Binom(j, b) / Binom(pv, b) * c[a][b];
      cv.push_back(Point3d(static_cast<double>(i) / pu, static_cast<double>(j) / pv, z));
    }
  }
  NurbsSurface s = Make(pu, pv, pu + 1, pv + 1, {0, 0, 0, 1, 1, 1}, {0, 0, 0, 0, 1, 1, 1, 1}, cv);
  auto dev_poly = [&](const NurbsSurface& q) {
    double worst = 0.0;
    for (int i = 0; i <= 20; ++i)
      for (int j = 0; j <= 20; ++j) {
        const double u = i / 20.0, v = j / 20.0;
        worst = std::max(worst, q.raw().PointAt(q.raw().Domain(0).ParameterAt(u), q.raw().Domain(1).ParameterAt(v)).DistanceTo(Point3d(u, v, f(u, v))));
      }
    return worst;
  };
  d8test::Check(dev_poly(s) < 1e-12, "setup: the hand-derived Bezier net equals the monomial polynomial surface");
  d8test::Check(s.ElevateDegree(0, 5) == Result::Ok && s.ElevateDegree(1, 5) == Result::Ok && s.DegreeU() == 5 && s.DegreeV() == 5, "setup: elevated to (5, 5)");
  double dv_ = -1.0, du_ = -1.0;
  const Result rv = s.ReduceDegree(1, 3, 1e-9, &dv_);
  d8test::Check(rv == Result::Ok && s.DegreeV() == 3 && s.CVCountV() == pv + 1, "ReduceDegree in V (5 -> 3) succeeds with the original 4 control columns");
  const Result ru = s.ReduceDegree(0, 2, 1e-9, &du_);
  d8test::Check(ru == Result::Ok && s.DegreeU() == 2 && s.CVCountU() == pu + 1, "ReduceDegree in U (5 -> 2) succeeds with the original 3 control rows");
  const double worst = dev_poly(s);
  std::printf("  polynomial surface after elevate (2,3) -> (5,5) and reduce back: %.3e (reported %.3e, %.3e)\n", worst, dv_, du_);
  d8test::Check(worst < 1e-12, "the reduced surface equals the polynomial at 21 x 21 parameters to 1e-12");
  d8test::Check(dv_ >= 0.0 && dv_ < 1e-12 && du_ >= 0.0 && du_ < 1e-12, "both reported deviations are ~0");
}

D8_EXTRA_TEST(SurfaceReduceDegreeRecoversASplineSurface) {
  const int nu = 6, nv = 5;
  std::vector<Point3d> cv;
  for (int i = 0; i < nu; ++i)
    for (int j = 0; j < nv; ++j) cv.push_back(Point3d(1.3 * i + 0.2 * std::sin(j), 1.1 * j + 0.3 * std::cos(2.0 * i), std::sin(0.8 * i + 1.1 * j) + 0.1 * i * j));
  const NurbsSurface orig = Make(3, 2, nu, nv, {0, 0, 0, 0, 1.0, 2.3, 4, 4, 4, 4}, {0, 0, 0, 1.1, 2.5, 3, 3, 3}, cv);
  d8test::Check(orig.raw().IsValid(), "setup: valid (3, 2) spline surface with non-uniform knots");
  NurbsSurface s = orig;
  d8test::Check(s.ElevateDegree(0, 4) == Result::Ok && s.ElevateDegree(1, 4) == Result::Ok, "setup: elevated to (4, 4)");
  double dv_ = -1.0, du_ = -1.0;
  d8test::Check(s.ReduceDegree(1, 2, 1e-9, &dv_) == Result::Ok, "ReduceDegree in V (4 -> 2) succeeds");
  d8test::Check(s.ReduceDegree(0, 3, 1e-9, &du_) == Result::Ok, "ReduceDegree in U (4 -> 3) succeeds");
  d8test::Check(SameNet(s, orig, 1e-9), "the knot vectors, degrees and control net are the original's (1e-9)");
  const double sd = MaxShapeDiff(s, orig);
  std::printf("  spline surface: shape deviation %.3e (reported %.3e, %.3e)\n", sd, dv_, du_);
  d8test::Check(sd < 1e-11 && dv_ < 1e-9 && du_ < 1e-9, "the shape equals the original to 1e-11");
}

D8_EXTRA_TEST(SurfaceReduceDegreeRationalCylinderAndRefusal) {
  const double s2 = std::sqrt(0.5), r = 2.5, h = 4.0;
  // u: quarter circle (3 control points, weights 1, sqrt(2)/2, 1); v: a straight line of length h (2 control points).
  const double cx[3] = {r, r, 0.0}, cy[3] = {0.0, r, r}, cw[3] = {1.0, s2, 1.0};
  std::vector<Point3d> cv;
  std::vector<double> w;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 2; ++j) {
      cv.push_back(Point3d(cx[i], cy[i], j * h));
      w.push_back(cw[i]);
    }
  const NurbsSurface quarter = Make(2, 1, 3, 2, {0, 0, 0, 1, 1, 1}, {0, 0, 1, 1}, cv, &w);
  NurbsSurface t = quarter;
  d8test::Check(t.ElevateDegree(0, 3) == Result::Ok && t.ElevateDegree(1, 3) == Result::Ok && t.IsRational(), "setup: the rational quarter cylinder elevated to (3, 3)");
  double d1 = -1.0, d0 = -1.0;
  d8test::Check(t.ReduceDegree(1, 1, 1e-9, &d1) == Result::Ok && t.ReduceDegree(0, 2, 1e-9, &d0) == Result::Ok, "reduced back to (2, 1)");
  double worst = 0.0;
  for (int i = 0; i <= 30; ++i)
    for (int j = 0; j <= 30; ++j) {
      const ON_3dPoint p = t.raw().PointAt(t.raw().Domain(0).ParameterAt(i / 30.0), t.raw().Domain(1).ParameterAt(j / 30.0));
      worst = std::max(worst, std::fabs(std::hypot(p.x, p.y) - r));
    }
  std::printf("  rational quarter cylinder: max |radius error| %.3e\n", worst);
  d8test::Check(worst < 1e-12 && t.CVCountU() == 3 && t.CVCountV() == 2, "every point is at distance r from the axis (1e-12) with the original 3 x 2 net");
  d8test::Check(SameNet(t, quarter, 1e-9), "and the net is the original's");

  // Negative: a genuine wavy bicubic does not fit degree 2 in U within 1e-9.
  std::vector<Point3d> g;
  for (int i = 0; i < 6; ++i)
    for (int j = 0; j < 5; ++j) g.push_back(Point3d(i, j, std::sin(1.7 * i + 0.9 * j) * 1.5));
  NurbsSurface wavy = NurbsSurface::FromControlGrid(g, 6, 5, 3, 3);
  const NurbsSurface before = wavy;
  double dev = -1.0;
  d8test::Check(wavy.ReduceDegree(0, 2, 1e-9, &dev) == Result::Failed, "a genuine wavy bicubic is refused at degree 2 in U within 1e-9");
  d8test::Check(dev > 1e-6 && SameNet(wavy, before, 0.0), "the refusal reports a real deviation and leaves the surface bit-identical");
}
