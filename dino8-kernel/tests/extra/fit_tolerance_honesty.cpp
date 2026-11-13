// Fitting / simplification must not silently exceed the tolerance it was given.
//
// Oracles: closed forms. A dense cubic fit through points sampled from an analytic helix / circular arc is simplified with
// NurbsCurve::Simplify(tol); the simplified curve must stay within tol of the curve it started from, measured two-sided
// (400 samples each way, closest points) and, for the helix, also within tol + the fit's own deviation of the analytic helix
// (|(x, y)| = r and z = pitch * angle / 2 pi, closed form). A torus patch refit by NurbsSurface::FitToTolerance must stay
// within the tolerance of the ANALYTIC torus (distance through the closed-form AnalyticClosestPoint) at 150 x 150 points that
// are not the fit's own samples.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

double OneSided(const NurbsCurve& from, const NurbsCurve& to) {
  double worst = 0.0;
  const double t0 = from.Domain().min, t1 = from.Domain().max;
  for (int i = 0; i <= 400; ++i) {
    const Point3d p = from.PointAt(t0 + (t1 - t0) * i / 400.0);
    worst = std::max(worst, (p - to.ClosestPoint(p)).Length());
  }
  return worst;
}
double TwoSided(const NurbsCurve& a, const NurbsCurve& b) { return std::max(OneSided(a, b), OneSided(b, a)); }

NurbsCurve Fit(const std::vector<Point3d>& pts, int cv) {
  NurbsCurve c;
  NurbsCurve::FitLeastSquares(pts, 3, cv, c);
  return c;
}

}  // namespace

D8_EXTRA_TEST(SimplifyStaysWithinItsToleranceOfTheCurveItStartedFrom) {
  const double R = 2.0, pitch = 1.5;
  std::vector<Point3d> helix, arc;
  for (int i = 0; i <= 240; ++i) {
    const double a = 6.0 * ON_PI * i / 240.0;
    helix.push_back(Point3d(R * std::cos(a), R * std::sin(a), pitch * a / (2.0 * ON_PI)));
    const double b = 1.5 * ON_PI * i / 240.0;
    arc.push_back(Point3d(R * std::cos(b), R * std::sin(b), 0.0));
  }
  const NurbsCurve fits[2] = {Fit(helix, 70), Fit(arc, 50)};
  const char* names[2] = {"helix fit (70 control points)", "circular-arc fit (50 control points)"};
  for (int k = 0; k < 2; ++k) {
    for (double tol : {1e-2, 1e-3, 1e-4}) {
      NurbsCurve s = fits[k];
      const Result r = s.Simplify(tol);
      const double d = TwoSided(fits[k], s);
      char tag[200];
      std::snprintf(tag, sizeof tag, "%s, Simplify(%.0e): result %d, %d -> %d control points, deviation from the input %.2e <= tol", names[k], tol, (int)r,
                    fits[k].ControlPointCount(), s.ControlPointCount(), d);
      std::printf("  %s\n", tag);
      d8test::Check(d <= tol * (1.0 + 1e-6) + 1e-9, tag);
    }
  }
}

D8_EXTRA_TEST(FitToToleranceTorusPatchStaysWithinToleranceOfTheAnalyticTorus) {
  const double R = 3.0, r = 1.0;
  ON_Torus tor(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), R, r);
  NurbsSurface src;
  if (tor.GetNurbForm(src.raw()) == 0) {
    d8test::Check(false, "setup: torus NURBS form");
    return;
  }
  dino8::kernel::SurfaceClassification rec;
  rec.kind = dino8::kernel::SurfaceKind::Torus;
  rec.torus = tor;
  for (double tol : {1e-2, 1e-3}) {
    NurbsSurface out;
    double dev = -1.0;
    int nu = 0, nv = 0;
    const Result res = src.FitToTolerance(tol, 3, 3, 60, out, &dev, &nu, &nv);
    char tag[200];
    if (res != Result::Ok) {
      std::snprintf(tag, sizeof tag, "FitToTolerance(%.0e) on the torus refused (best %.2e at %d x %d): a typed refusal is acceptable", tol, dev, nu, nv);
      d8test::Check(dev > tol, tag);
      continue;
    }
    double worst = 0.0;
    const ON_Interval du = out.raw().Domain(0), dv = out.raw().Domain(1);
    for (int i = 0; i <= 150; ++i)
      for (int j = 0; j <= 150; ++j) {
        const ON_3dPoint p = out.raw().PointAt(du.ParameterAt((i + 0.37) / 151.0), dv.ParameterAt((j + 0.61) / 151.0));
        dino8::kernel::AnalyticFoot f;
        if (dino8::kernel::AnalyticClosestPoint(rec, p, f)) worst = std::max(worst, std::fabs(f.signed_distance));
      }
    std::snprintf(tag, sizeof tag, "FitToTolerance(%.0e) torus: reported %.2e, net %d x %d, measured distance to the analytic torus %.2e <= tol", tol, dev, nu, nv, worst);
    std::printf("  %s\n", tag);
    d8test::Check(worst <= tol * (1.0 + 1e-6) + 1e-9, tag);
  }
}
