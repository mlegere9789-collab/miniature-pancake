// Brep::Pipe / Brep::PipeVariable adaptive mode (tolerance > 0): the tube is bisected until it is within `tolerance` of the
// exact tube. Every oracle here is a closed form that shares no code with the kernel:
//   * a half circle rail of radius R: the tube is the torus segment, (hypot(x, y) - R)^2 + z^2 = r^2 for every wall point;
//   * the same rail with a piecewise-linear radius schedule: the exact points C(phi) + r(f) (cos(th) rho + sin(th) z) are compared
//     with the wall by a dense nearest-point search (a parameter grid and a pattern search, as in the other tests);
//   * a helix rail: every wall point of a tube around a curve is at distance exactly r from the curve (r below the radius of
//     curvature), so |distance to the helix - r| is the deviation, with the helix as its own closed form;
//   * volumes by Pappus: pi r^2 (pi R) for the half torus; pi^2 R * integral r(f)^2 df (the sections are the meridian planes, each
//     a circle of radius r(f) centred at distance R from the axis) for the variable tube, open and closed.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/types.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;
using Sched = std::vector<std::pair<double, double>>;

const ON_NurbsSurface& WallOf(const Brep& b) { return *ON_NurbsSurface::Cast(b.raw().m_F[0].SurfaceOf()); }

// Circle arc rail of radius R in the x-y plane from (R, 0, 0) heading +y, `sweep` radians (2 pi: the closed ring).
NurbsCurve ArcRail(double R, double sweep) {
  ON_NurbsCurve nurbs;
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0)), R);
  if (sweep >= 2.0 * ON_PI - 1e-12) circle.GetNurbForm(nurbs); else ON_Arc(circle, ON_Interval(0.0, sweep)).GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

// Nearest-point distance from `p` to the surface: a dense parameter grid, then a pattern search from its best three local minima.
double NearestDistance(const ON_NurbsSurface& s, const ON_3dPoint& p) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  const int nu = 32, nv = std::min(std::max(2 * s.CVCount(1), 64), 400);
  std::vector<double> g(static_cast<size_t>(nu + 1) * static_cast<size_t>(nv + 1));
  auto U = [&](int i) { return du.ParameterAt(static_cast<double>(i) / nu); };
  auto V = [&](int j) { return dv.ParameterAt(static_cast<double>(j) / nv); };
  for (int i = 0; i <= nu; ++i) {
    for (int j = 0; j <= nv; ++j) g[static_cast<size_t>(i) * (nv + 1) + j] = s.PointAt(U(i), V(j)).DistanceTo(p);
  }
  std::vector<std::pair<double, std::pair<int, int>>> mins;
  for (int i = 0; i <= nu; ++i) {
    for (int j = 0; j <= nv; ++j) {
      const double d = g[static_cast<size_t>(i) * (nv + 1) + j];
      bool is_min = true;
      for (int a = -1; a <= 1 && is_min; ++a) {
        for (int c = -1; c <= 1; ++c) {
          const int ii = i + a, jj = j + c;
          if ((!a && !c) || ii < 0 || ii > nu || jj < 0 || jj > nv) continue;
          if (g[static_cast<size_t>(ii) * (nv + 1) + jj] < d) {
            is_min = false;
            break;
          }
        }
      }
      if (is_min) mins.push_back({d, {i, j}});
    }
  }
  std::sort(mins.begin(), mins.end());
  double best = 1e300;
  for (size_t m = 0; m < mins.size() && m < 3; ++m) {
    double u = U(mins[m].second.first), v = V(mins[m].second.second), d = mins[m].first;
    double su = du.Length() / nu, sv = dv.Length() / nv;
    for (int it = 0; it < 3000 && su > 1e-13 * du.Length(); ++it) {
      bool improved = false;
      for (int a = -1; a <= 1; ++a) {
        for (int c = -1; c <= 1; ++c) {
          if (!a && !c) continue;
          const double uu = std::min(std::max(u + a * su, du.Min()), du.Max()), vv = std::min(std::max(v + c * sv, dv.Min()), dv.Max());
          const double dd = s.PointAt(uu, vv).DistanceTo(p);
          if (dd < d) {
            d = dd;
            u = uu;
            v = vv;
            improved = true;
          }
        }
      }
      if (!improved) {
        su *= 0.5;
        sv *= 0.5;
      }
    }
    best = std::min(best, d);
  }
  return best;
}

double ScheduleAt(const Sched& s, double f) {
  if (f <= s.front().first) return s.front().second;
  for (size_t i = 1; i < s.size(); ++i) {
    if (f <= s[i].first) return s[i - 1].second + (f - s[i - 1].first) / (s[i].first - s[i - 1].first) * (s[i].second - s[i - 1].second);
  }
  return s.back().second;
}

// Exact point of the variable torus tube at rail fraction f (the rail an arc of `sweep` radians, radius R) and angle th.
ON_3dPoint VariableTorusPoint(double R, double sweep, const Sched& radius, double f, double th) {
  const double phi = sweep * f, r = ScheduleAt(radius, f);
  const ON_3dVector rho(std::cos(phi), std::sin(phi), 0.0);
  return ON_3dPoint(R * rho.x, R * rho.y, 0.0) + rho * (r * std::cos(th)) + ON_3dVector(0, 0, r * std::sin(th));
}

// Worst distance of exact tube points (irrational-step fractions plus the radius breakpoints and a point either side of each,
// 24 angles each) to the wall.
double ExactPointsToWall(const Brep& body, double R, double sweep, const Sched& radius) {
  std::vector<double> fs;
  for (int i = 0; i < 60; ++i) fs.push_back(std::fmod(0.0131 + i * 0.6180339887, 1.0));
  for (const auto& rp : radius) {
    for (const double d : {-1e-3, 0.0, 1e-3}) fs.push_back(rp.first + d);
  }
  double worst = 0.0;
  for (const double f : fs) {
    if (f < 0.0 || f > 1.0) continue;
    for (int j = 0; j < 24; ++j) worst = std::max(worst, NearestDistance(WallOf(body), VariableTorusPoint(R, sweep, radius, f, 2.0 * ON_PI * (j + 0.37) / 24.0)));
  }
  return worst;
}

// Worst |hypot(hypot(x, y) - R, z) - r| over a dense grid of the wall (the constant-radius torus tube).
double TorusResidual(const ON_NurbsSurface& w, double R, double r) {
  const ON_Interval du = w.Domain(0), dv = w.Domain(1);
  double worst = 0.0;
  for (int i = 0; i < 48; ++i) {
    for (int j = 0; j <= 400; ++j) {
      const ON_3dPoint p = w.PointAt(du.ParameterAt((i + 0.3) / 48.0), dv.ParameterAt(static_cast<double>(j) / 400.0));
      worst = std::max(worst, std::fabs(std::hypot(std::hypot(p.x, p.y) - R, p.z) - r));
    }
  }
  return worst;
}

}  // namespace

D8_EXTRA_TEST(PipeAdaptiveHalfTorusMeetsToleranceAgainstTheTorusEquation) {
  const double R = 8.0, r = 1.2;
  const NurbsCurve rail = ArcRail(R, ON_PI);
  const Brep fixed = Brep::Pipe(rail, r, false, 8);
  const double fixed_dev = TorusResidual(WallOf(fixed), R, r);
  char what[300];
  std::snprintf(what, sizeof what, "Pipe on a half circle at 8 stations (non-adaptive): the torus equation sees the interpolation error (%.3g > 1e-3), so the oracle can see an error", fixed_dev);
  d8test::Check(fixed_dev > 1e-3, what);
  int prev_stations = 0;
  for (const double tol : {1e-3, 1e-4, 1e-5, 1e-6}) {
    Brep::Sweep1Report rep;
    const Brep body = Brep::Pipe(rail, r, false, 8, false, tol, 1024, &rep);
    const double dev = TorusResidual(WallOf(body), R, r);
    std::printf("  [pipe half torus] tol %.0e: %d stations (%d rounds), kernel max %.3g, torus equation %.3g\n", tol, rep.stations, rep.rounds, rep.max_deviation, dev);
    std::snprintf(what, sizeof what, "Pipe adaptive on a half circle, tolerance %.0e: met, kernel max %.3g <= tol, wall within tol of the torus equation (%.3g), valid", tol, rep.max_deviation, dev);
    d8test::Check(rep.met && rep.max_deviation <= tol && dev <= tol && body.raw().IsValid(), what);
    d8test::Check(rep.stations >= prev_stations, "Pipe adaptive on a half circle: the station count never falls as the tolerance tightens");
    prev_stations = rep.stations;
  }
}

D8_EXTRA_TEST(PipeAdaptiveCappedHalfTorusVolumeIsPappus) {
  const double R = 8.0, r = 1.2;
  const NurbsCurve rail = ArcRail(R, ON_PI);
  const Brep body = Brep::Pipe(rail, r, true, 8, false, 1e-5, 1024);
  const double exact = ON_PI * r * r * (ON_PI * R);
  double got = 0.0;
  try {
    got = body.Volume();
  } catch (const std::exception& e) {
    std::printf("  [pipe half torus volume] Volume() threw: %s\n", e.what());
  }
  char what[300];
  std::snprintf(what, sizeof what, "Pipe adaptive (tol 1e-5, capped) on a half circle: volume %.6f is Pappus pi r^2 (pi R) = %.6f to 1e-4 relative; solid with 3 faces", got, exact);
  d8test::Check(std::fabs(got - exact) <= 1e-4 * exact && body.raw().IsValid() && body.raw().m_F.Count() == 3, what);
}

D8_EXTRA_TEST(PipeVariableAdaptiveHalfTorusAgainstExactPointsAndPappus) {
  const double R = 8.0;
  const Sched radius = {{0.0, 1.0}, {0.5, 1.6}, {1.0, 0.8}};
  const NurbsCurve rail = ArcRail(R, ON_PI);
  const Brep fixed = Brep::PipeVariable(rail, radius, false, 8);
  const double fixed_dev = ExactPointsToWall(fixed, R, ON_PI, radius);
  char what[300];
  std::snprintf(what, sizeof what, "PipeVariable on a half circle at 8 stations (non-adaptive): exact tube points are %.3g from the wall (> 1e-4), so the oracle can see an error", fixed_dev);
  d8test::Check(fixed_dev > 1e-4, what);
  for (const double tol : {1e-3, 1e-4, 1e-5}) {
    Brep::Sweep1Report rep;
    const Brep body = Brep::PipeVariable(rail, radius, false, 8, false, tol, 1024, &rep);
    const double dev = ExactPointsToWall(body, R, ON_PI, radius);
    std::printf("  [pipe variable half torus] tol %.0e: %d stations (%d rounds, %d C0), kernel max %.3g, exact points %.3g\n", tol, rep.stations, rep.rounds, rep.corner_stations, rep.max_deviation, dev);
    std::snprintf(what, sizeof what, "PipeVariable adaptive (radius 1, 1.6, 0.8), tolerance %.0e: met (kernel max %.3g), exact tube points within tol of the wall (%.3g), skin C0 at the one slope change", tol, rep.max_deviation, dev);
    d8test::Check(rep.met && rep.max_deviation <= tol && dev <= tol + 1e-9 && rep.corner_stations == 1 && body.raw().IsValid(), what);
  }
  // Volume: pi^2 R integral r(f)^2 df, the integral being 0.86 + 0.746667 for this schedule.
  const Brep solid = Brep::PipeVariable(rail, radius, true, 8, false, 1e-5, 1024);
  const double exact = ON_PI * ON_PI * R * (0.86 + 2.56 * (1.0 - 0.125) / 3.0);
  double got = 0.0;
  try {
    got = solid.Volume();
  } catch (const std::exception& e) {
    std::printf("  [pipe variable volume] Volume() threw: %s\n", e.what());
  }
  std::snprintf(what, sizeof what, "PipeVariable adaptive (tol 1e-5, capped): volume %.6f is pi^2 R int r^2 df = %.6f to 2e-4 relative", got, exact);
  d8test::Check(std::fabs(got - exact) <= 2e-4 * exact, what);
}

D8_EXTRA_TEST(PipeVariableAdaptiveClosedRingWithPeriodicRadius) {
  const double R = 5.0;
  const Sched radius = {{0.0, 1.0}, {0.5, 1.5}, {1.0, 1.0}};
  const NurbsCurve rail = ArcRail(R, 2.0 * ON_PI);
  char what[300];
  for (const double tol : {1e-3, 1e-4}) {
    Brep::Sweep1Report rep;
    const Brep body = Brep::PipeVariable(rail, radius, true, 16, false, tol, 1024, &rep);
    const double dev = ExactPointsToWall(body, R, 2.0 * ON_PI, radius);
    std::printf("  [pipe variable ring] tol %.0e: %d stations (%d rounds), kernel max %.3g, exact points %.3g\n", tol, rep.stations, rep.rounds, rep.max_deviation, dev);
    std::snprintf(what, sizeof what, "PipeVariable adaptive on a closed circle with radius 1, 1.5, 1 (tolerance %.0e): met, exact tube points within tol of the wall (%.3g), wall closed in v", tol, dev);
    d8test::Check(rep.met && rep.max_deviation <= tol && dev <= tol + 1e-9 && WallOf(body).IsClosed(1), what);
  }
  const Brep body = Brep::PipeVariable(rail, radius, true, 16, false, 1e-4, 1024);
  const double exact = 2.0 * ON_PI * ON_PI * R * 1.5833333333333333;
  double got = 0.0;
  try {
    got = body.Volume();
  } catch (const std::exception& e) {
    std::printf("  [pipe variable ring volume] Volume() threw: %s\n", e.what());
  }
  std::snprintf(what, sizeof what, "PipeVariable adaptive closed ring (tol 1e-4): volume %.6f is 2 pi^2 R int r^2 df = %.6f to 3e-4 relative", got, exact);
  d8test::Check(std::fabs(got - exact) <= 3e-4 * exact, what);
}

D8_EXTRA_TEST(PipeAdaptiveHelixTubeIsAtDistanceRFromTheHelix) {
  // Every point of a tube around a curve is at distance r from the curve (r below the radius of curvature, here (a^2 + c^2) / a = 4.06 with c = pitch / 2 pi).
  const double a = 4.0, pitch = 3.0, turns = 2.0, r = 0.7;
  NurbsCurve rail;
  double fit_dev = 0.0;
  if (NurbsCurve::Helix(Point3d(0, 0, 0), Vector3d(0, 0, 1), a, pitch, turns, 1e-9, rail, &fit_dev) != dino8::kernel::Result::Ok) {
    d8test::Check(false, "helix rail: NurbsCurve::Helix failed");
    return;
  }
  const double c = pitch / (2.0 * ON_PI), tmax = 2.0 * ON_PI * turns;
  // (Helix() takes any orthonormal basis of the plane: read the phase of the start point off the rail, then the locus is closed form)
  const ON_3dPoint s0 = rail.raw().PointAtStart();
  const double phase = std::atan2(s0.y, s0.x);
  auto H = [&](double t) { return ON_3dPoint(a * std::cos(t + phase), a * std::sin(t + phase), c * t); };
  {
    const ON_3dPoint e = rail.raw().PointAtEnd();
    char w[200];
    std::snprintf(w, sizeof w, "helix rail: the start is at radius %.9f (a = 4) and the end %.3g from the closed-form helix end", std::hypot(s0.x, s0.y), e.DistanceTo(H(tmax)));
    d8test::Check(std::fabs(std::hypot(s0.x, s0.y) - a) < 1e-8 && e.DistanceTo(H(tmax)) < 1e-6, w);
  }
  auto helix_distance = [&](const ON_3dPoint& p) {
    double best = 1e300, bt = 0.0;
    const int n = 4000;
    for (int i = 0; i <= n; ++i) {
      const double t = tmax * i / n;
      const double d = H(t).DistanceTo(p);
      if (d < best) {
        best = d;
        bt = t;
      }
    }
    double lo = std::max(0.0, bt - tmax / n), hi = std::min(tmax, bt + tmax / n);
    for (int it = 0; it < 80; ++it) {
      const double m1 = lo + (hi - lo) * 0.382, m2 = lo + (hi - lo) * 0.618;
      const double d1 = H(m1).DistanceTo(p), d2 = H(m2).DistanceTo(p);
      if (d1 < d2) hi = m2; else lo = m1;
    }
    const double t = 0.5 * (lo + hi);
    return std::min(best, H(t).DistanceTo(p));
  };
  auto deviation = [&](const Brep& b) {
    const ON_NurbsSurface& w = WallOf(b);
    const ON_Interval du = w.Domain(0), dv = w.Domain(1);
    double worst = 0.0;
    for (int i = 0; i < 24; ++i) {
      for (int j = 0; j <= 160; ++j) {
        const ON_3dPoint p = w.PointAt(du.ParameterAt((i + 0.3) / 24.0), dv.ParameterAt(static_cast<double>(j) / 160.0));
        // (the ends of the rail carry the tube's own end circles: skip the first and last 2% of v, where the nearest helix point is the rail end)
        if (j < 4 || j > 156) continue;
        worst = std::max(worst, std::fabs(helix_distance(p) - r));
      }
    }
    return worst;
  };
  const Brep fixed = Brep::Pipe(rail, r, false, 12);
  const double fixed_dev = deviation(fixed);
  char what[300];
  std::snprintf(what, sizeof what, "Pipe on a 2-turn helix at 12 stations (non-adaptive): wall points are %.3g from distance r off the helix (> 1e-3), so the oracle can see an error", fixed_dev);
  d8test::Check(fixed_dev > 1e-3, what);
  for (const double tol : {1e-3, 1e-4, 1e-5}) {
    Brep::Sweep1Report rep;
    const Brep body = Brep::Pipe(rail, r, false, 12, false, tol, 1024, &rep);
    const double dev = deviation(body);
    std::printf("  [pipe helix] tol %.0e: %d stations (%d rounds), kernel max %.3g, |distance to helix - r| %.3g\n", tol, rep.stations, rep.rounds, rep.max_deviation, dev);
    std::snprintf(what, sizeof what, "Pipe adaptive on a 2-turn helix, tolerance %.0e: met, every sampled wall point is within tol of distance r from the helix (%.3g)", tol, dev);
    d8test::Check(rep.met && rep.max_deviation <= tol && dev <= tol + 2e-9, what);
  }
}

D8_EXTRA_TEST(PipeAdaptiveRefusalsAndUnchangedDefault) {
  const NurbsCurve rail = ArcRail(8.0, ON_PI);
  auto refusal = [](const std::function<void()>& f) {
    try {
      f();
    } catch (const std::invalid_argument& e) {
      return std::string(e.what());
    }
    return std::string();
  };
  d8test::Check(!refusal([&] { Brep::Pipe(rail, 1.0, true, 8, false, -1e-3); }).empty() && !refusal([&] { Brep::Pipe(rail, 1.0, true, 8, false, std::nan("")); }).empty() &&
                    !refusal([&] { Brep::PipeVariable(rail, {{0.0, 1.0}, {1.0, 2.0}}, true, 8, false, -1.0); }).empty(),
                "Pipe / PipeVariable: a negative or NaN tolerance is refused");
  d8test::Check(refusal([&] { Brep::Pipe(rail, 1.0, true, 8, true, 1e-3); }).find("round_caps") != std::string::npos &&
                    refusal([&] { Brep::PipeVariable(rail, {{0.0, 1.0}, {1.0, 2.0}}, true, 8, true, 1e-3); }).find("round_caps") != std::string::npos,
                "Pipe / PipeVariable: tolerance with round_caps is refused, naming round_caps");
  Brep::Sweep1Report rep;
  const std::string why = refusal([&] { Brep::Pipe(rail, 1.0, false, 4, false, 1e-9, 8, &rep); });
  d8test::Check(!why.empty() && !rep.met && rep.max_deviation > 1e-9, "Pipe adaptive: a tolerance the station ceiling cannot reach is refused (no unverified tube), the report saying met = false");
  // tolerance 0: the default construction, control point for control point
  const Brep a = Brep::Pipe(rail, 1.0, false, 8), b = Brep::Pipe(rail, 1.0, false, 8, false, 0.0);
  const ON_NurbsSurface &wa = WallOf(a), &wb = WallOf(b);
  bool same = wa.CVCount(0) == wb.CVCount(0) && wa.CVCount(1) == wb.CVCount(1);
  for (int i = 0; same && i < wa.CVCount(0); ++i) {
    for (int j = 0; same && j < wa.CVCount(1); ++j) {
      ON_4dPoint pa, pb;
      wa.GetCV(i, j, pa);
      wb.GetCV(i, j, pb);
      same = pa.x == pb.x && pa.y == pb.y && pa.z == pb.z && pa.w == pb.w;
    }
  }
  d8test::Check(same, "Pipe with tolerance 0 is the unchanged construction (same control net as the call without it)");
}
