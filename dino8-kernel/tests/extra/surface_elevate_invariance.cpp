// NurbsSurface::ElevateDegree(): shape invariance. Evidence test (no new code): elevating either direction by 1..3 degrees
// leaves the surface unchanged. Oracles: the monomial polynomial surface (u, v, f) with a hand power -> Bernstein net (exact
// values at 25 x 25 parameters, 1e-12), the rational quarter cylinder (every point at distance r from the axis, 1e-12); each elevated net has the expected degree.
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

double Binom(int n, int k) {
  double r = 1.0;
  for (int i = 1; i <= k; ++i) r = r * (n - k + i) / i;
  return r;
}
}  // namespace

D8_EXTRA_TEST(SurfaceElevateDegreeKeepsTheShape) {
  // polynomial surface of degree (2, 3)
  const int pu = 2, pv = 3;
  double c[3][4];
  for (int a = 0; a <= pu; ++a)
    for (int b = 0; b <= pv; ++b) c[a][b] = std::cos(0.9 * a + 1.7 * b) * (1.0 + 0.2 * a * b);
  auto f = [&](double u, double v) {
    double s = 0;
    for (int a = 0; a <= pu; ++a)
      for (int b = 0; b <= pv; ++b) s += c[a][b] * std::pow(u, a) * std::pow(v, b);
    return s;
  };
  std::vector<Point3d> cv;
  for (int i = 0; i <= pu; ++i)
    for (int j = 0; j <= pv; ++j) {
      double z = 0;
      for (int a = 0; a <= i; ++a)
        for (int b = 0; b <= j; ++b) z += Binom(i, a) / Binom(pu, a) * Binom(j, b) / Binom(pv, b) * c[a][b];
      cv.push_back(Point3d(double(i) / pu, double(j) / pv, z));
    }
  for (int du = 0; du <= 3; ++du)
    for (int dv = 0; dv <= 3; ++dv) {
      NurbsSurface s;
      ON_NurbsSurface& r = s.raw();
      r.Create(3, false, pu + 1, pv + 1, pu + 1, pv + 1);
      const double ku[6] = {0, 0, 0, 1, 1, 1}, kv[8] = {0, 0, 0, 0, 1, 1, 1, 1};
      for (int k = 0; k < r.KnotCount(0); ++k) r.SetKnot(0, k, ku[k + 1]);
      for (int k = 0; k < r.KnotCount(1); ++k) r.SetKnot(1, k, kv[k + 1]);
      for (int i = 0; i <= pu; ++i)
        for (int j = 0; j <= pv; ++j) r.SetCV(i, j, cv[size_t(i * (pv + 1) + j)]);
      bool ok = true;
      if (du) ok = ok && s.ElevateDegree(0, pu + du) == Result::Ok;
      if (dv) ok = ok && s.ElevateDegree(1, pv + dv) == Result::Ok;
      double w = 0;
      for (int i = 0; i <= 25; ++i)
        for (int j = 0; j <= 25; ++j)
          w = std::max(w, s.raw().PointAt(i / 25.0, j / 25.0).DistanceTo(Point3d(i / 25.0, j / 25.0, f(i / 25.0, j / 25.0))));
      char tag[96];
      std::snprintf(tag, sizeof tag, "polynomial surface elevated by (%d, %d): degrees (%d, %d) and equal to f to 1e-12 (%.1e)", du, dv, pu + du, pv + dv, w);
      d8test::Check(ok && s.DegreeU() == pu + du && s.DegreeV() == pv + dv && w < 1e-12, tag);
    }
  // rational quarter cylinder (u quadratic arc, v line)
  const double s2 = std::sqrt(0.5), R = 2.5;
  const double cx[3] = {R, R, 0}, cy[3] = {0, R, R}, cw[3] = {1, s2, 1};
  for (int du = 1; du <= 3; ++du) {
    NurbsSurface s;
    ON_NurbsSurface& r = s.raw();
    r.Create(3, true, 3, 2, 3, 2);
    const double ku[6] = {0, 0, 0, 1, 1, 1}, kv[4] = {0, 0, 1, 1};
    for (int k = 0; k < r.KnotCount(0); ++k) r.SetKnot(0, k, ku[k + 1]);
    for (int k = 0; k < r.KnotCount(1); ++k) r.SetKnot(1, k, kv[k + 1]);
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 2; ++j) r.SetCV(i, j, ON_4dPoint(cx[i] * cw[i], cy[i] * cw[i], 4.0 * j * cw[i], cw[i]));
    d8test::Check(s.ElevateDegree(0, 2 + du) == Result::Ok && s.ElevateDegree(1, 1 + du) == Result::Ok, "setup: rational cylinder elevated");
    double w = 0;
    for (int i = 0; i <= 30; ++i)
      for (int j = 0; j <= 30; ++j) {
        const ON_3dPoint p = s.raw().PointAt(s.raw().Domain(0).ParameterAt(i / 30.0), s.raw().Domain(1).ParameterAt(j / 30.0));
        w = std::max(w, std::fabs(std::hypot(p.x, p.y) - R));
      }
    char tag[96];
    std::snprintf(tag, sizeof tag, "rational quarter cylinder elevated by %d: radius error %.1e < 1e-12", du, w);
    d8test::Check(w < 1e-12 && s.IsRational(), tag);
  }
}
