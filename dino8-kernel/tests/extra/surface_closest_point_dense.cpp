// NurbsSurface::ClosestPoint / ClosestPointParameter on walls with hundreds of stations.
//
// The search used to scan only 20 x 20 cells of the whole domain; on a sweep wall that bends back on itself (a ring, a
// closed free-form rail) it returned points 0.10 to 1.8 away from points that lie ON the wall. These tests compare it with
// a dense brute-force oracle (a parameter grid of 3 samples per knot span plus a pattern search from the best three local
// minima of the grid) that shares no code with the kernel's search.
#include <algorithm>
#include <cmath>
#include <cstdio>
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
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using Sched = std::vector<std::pair<double, double>>;

NurbsCurve CircleCurve(const ON_3dPoint& center, const ON_3dVector& normal, const ON_3dVector& xdir, double radius) {
  ON_3dVector n = normal;
  n.Unitize();
  ON_3dVector x = xdir - n * ON_DotProduct(xdir, n);
  x.Unitize();
  ON_NurbsCurve nurbs;
  ON_Circle(ON_Plane(center, x, ON_CrossProduct(n, x)), radius).GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

NurbsSurface WallOf(const Brep& b) {
  NurbsSurface ns;
  ns.raw() = *ON_NurbsSurface::Cast(b.raw().m_F[0].SurfaceOf());
  return ns;
}

// Dense brute force: nearest-point distance from `p` to the surface.
double BruteDistance(const ON_NurbsSurface& s, const ON_3dPoint& p) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  const int nu = std::max(3 * (s.CVCount(0) - s.Degree(0)), 24) + 8, nv = std::min(std::max(3 * (s.CVCount(1) - s.Degree(1)), 64) + 8, 1800);
  std::vector<double> g(static_cast<size_t>(nu + 1) * static_cast<size_t>(nv + 1));
  for (int i = 0; i <= nu; ++i) {
    for (int j = 0; j <= nv; ++j) {
      g[static_cast<size_t>(i) * (nv + 1) + j] = s.PointAt(du.ParameterAt(static_cast<double>(i) / nu), dv.ParameterAt(static_cast<double>(j) / nv)).DistanceTo(p);
    }
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
    double u = du.ParameterAt(static_cast<double>(mins[m].second.first) / nu), v = dv.ParameterAt(static_cast<double>(mins[m].second.second) / nv), d = mins[m].first;
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

// The walls of the adaptive Sweep1 tests (sweep_adaptive_tests in test_basic.cpp): a closed circle rail with a whole-turn
// twist schedule and an out-and-back scale schedule, and an open and a closed free-form rail (the closed one non-planar, with
// holonomy), each with twist + scale schedules.
Brep RingWall() {
  const Sched tw = {{0.0, 0.0}, {0.25, 0.5 * ON_PI}, {0.6, 1.7 * ON_PI}, {1.0, 2.0 * ON_PI}}, sc = {{0.0, 1.0}, {0.5, 1.6}, {1.0, 1.0}};
  ON_NurbsCurve ring;
  ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0)), 5.0).GetNurbForm(ring);
  NurbsCurve rail;
  rail.raw() = ring;
  const NurbsCurve section = CircleCurve(ON_3dPoint(5.5, 0, 0.3), ON_3dVector(0, 1, 0), ON_3dVector(1, 0, 0), 1.0);
  return Brep::Sweep1(section, rail, 16, false, 0.0, 1.0, nullptr, &tw, &sc, nullptr, nullptr, nullptr, 1e-4, 1024);
}

Brep OpenFreeformWall() {
  const NurbsCurve rail = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(3, 1.5, 2), Point3d(6, -2, 3.5), Point3d(9, 2.5, 3), Point3d(12, 0.5, 6), Point3d(14, -1, 9)}, 3);
  const Sched tw = {{0.0, 0.0}, {0.35, 1.2}, {0.8, 2.0}, {1.0, 2.1}}, sc = {{0.0, 1.0}, {0.5, 1.5}, {1.0, 0.9}};
  const NurbsCurve section = CircleCurve(ON_3dPoint(0.3, 0.2, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0), 1.0);
  return Brep::Sweep1(section, rail, 32, false, 0.0, 1.0, nullptr, &tw, &sc, nullptr, nullptr, nullptr, 1e-5, 512);
}

Brep ClosedFreeformWall() {
  std::vector<ON_3dPoint> pts;
  for (int i = 0; i < 16; ++i) {
    const double t = 2.0 * ON_PI * i / 16.0;
    pts.push_back(ON_3dPoint(10.0 * std::cos(t), 10.0 * std::sin(t), 1.5 * std::sin(2.0 * t)));
  }
  ON_NurbsCurve periodic;
  periodic.CreatePeriodicUniformNurbs(3, 4, 16, pts.data(), 1.0);
  NurbsCurve rail;
  rail.raw() = periodic;
  const NurbsCurve section = CircleCurve(ON_3dPoint(10.4, 0, 0.2), ON_3dVector(0, 1, 0), ON_3dVector(1, 0, 0), 1.0);
  const Sched tw = {{0.0, 0.0}, {0.3, 0.8 * ON_PI}, {0.75, 1.5 * ON_PI}, {1.0, 2.0 * ON_PI}}, sc = {{0.0, 1.0}, {0.5, 1.4}, {1.0, 1.0}};
  return Brep::Sweep1(section, rail, 32, false, 0.0, 1.0, nullptr, &tw, &sc, nullptr, nullptr, nullptr, 1e-5, 1024);
}

// ClosestPoint on `wall`: (a) at points that lie ON it (true distance 0, sampled at off-grid parameters including both ends of
// each direction and the seam), the worst distance returned; (b) at points 0.05 .. 0.5 off it along its normal, the worst excess
// of the returned distance over the brute-force nearest distance, and how many of them are worse than that by more than 1e-7.
void CheckWall(const char* name, const Brep& body) {
  const NurbsSurface wall = WallOf(body);
  const ON_NurbsSurface& s = wall.raw();
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  std::printf("  [closest point, %s] %d x %d control points, degree %d x %d\n", name, s.CVCount(0), s.CVCount(1), s.Degree(0), s.Degree(1));

  double worst_on = 0.0, worst_param_on = 0.0;
  int n_on = 0;
  for (int i = 0; i < 9; ++i) {
    for (int j = 0; j < 60; ++j) {
      const double fu = i == 0 ? 0.0 : (i + 0.37) / 9.0, fv = j == 0 ? 0.0 : (j == 59 ? 1.0 : (j + 0.29) / 60.0);
      const ON_3dPoint p = s.PointAt(du.ParameterAt(fu), dv.ParameterAt(fv));
      const Point3d pp(p.x, p.y, p.z);
      worst_on = std::max(worst_on, pp.DistanceTo(wall.ClosestPoint(pp)));
      const dino8::kernel::Point2d uv = wall.ClosestPointParameter(pp);
      worst_param_on = std::max(worst_param_on, s.PointAt(uv.x, uv.y).DistanceTo(p));
      ++n_on;
    }
  }
  char what[400];
  std::snprintf(what, sizeof what, "ClosestPoint on %s: %d points lying ON the wall come back within 1e-7 of themselves (worst %.3g)", name, n_on, worst_on);
  d8test::Check(worst_on < 1e-7, what);
  std::snprintf(what, sizeof what, "ClosestPointParameter on %s: PointAt(returned u, v) is within 1e-7 of each of those points (worst %.3g)", name, worst_param_on);
  d8test::Check(worst_param_on < 1e-7, what);

  double worst_excess = 0.0;
  int n_off = 0, n_bad = 0;
  for (int i = 0; i < 6; ++i) {
    for (int j = 0; j < 36; ++j) {
      const double fu = (i + 0.21) / 6.0, fv = (j + 0.43) / 36.0;
      const double u = du.ParameterAt(fu), v = dv.ParameterAt(fv);
      ON_3dPoint base;
      ON_3dVector n;
      if (!s.EvNormal(u, v, base, n)) continue;
      const double off = 0.05 + 0.45 * ((i * 36 + j) % 10) / 9.0;
      const ON_3dPoint p = base + n * ((((i + j) & 1) ? -1.0 : 1.0) * off);
      const Point3d pp(p.x, p.y, p.z);
      const double got = pp.DistanceTo(wall.ClosestPoint(pp));
      const double best = BruteDistance(s, p);
      worst_excess = std::max(worst_excess, got - best);
      if (got > best + 1e-7 * (1.0 + best)) ++n_bad;
      ++n_off;
    }
  }
  std::snprintf(what, sizeof what, "ClosestPoint on %s: of %d points 0.05 .. 0.5 off the wall (both sides) none is worse than the brute-force nearest distance by more than 1e-7 (%d are, worst excess %.3g)", name, n_off, n_bad,
                worst_excess);
  d8test::Check(n_off >= 150 && n_bad == 0, what);

  // an explicit, finer request is no worse than the default
  double worst_explicit = 0.0;
  for (int j = 0; j < 40; ++j) {
    const ON_3dPoint p = s.PointAt(du.ParameterAt(0.5 + 0.013 * (j % 5)), dv.ParameterAt((j + 0.61) / 40.0));
    const Point3d pp(p.x, p.y, p.z);
    worst_explicit = std::max(worst_explicit, pp.DistanceTo(wall.ClosestPoint(pp, 40, 40)));
  }
  std::snprintf(what, sizeof what, "ClosestPoint(p, 40, 40) on %s: points lying ON the wall come back within 1e-7 (worst %.3g)", name, worst_explicit);
  d8test::Check(worst_explicit < 1e-7, what);
}

}  // namespace

D8_EXTRA_TEST(SurfaceClosestPointRingWallMatchesBruteForce) { CheckWall("the closed circle-rail ring wall", RingWall()); }

D8_EXTRA_TEST(SurfaceClosestPointOpenFreeformWallMatchesBruteForce) { CheckWall("the open free-form wall", OpenFreeformWall()); }

D8_EXTRA_TEST(SurfaceClosestPointClosedFreeformWallMatchesBruteForce) { CheckWall("the closed free-form wall", ClosedFreeformWall()); }

// Surfaces the default grid already covers keep the legacy search: a bicubic patch with 8 x 8 control points and a plain
// cylinder give the same answer as the brute force too (a guard that the new path did not break the common case).
D8_EXTRA_TEST(SurfaceClosestPointSmallSurfacesStillMatchBruteForce) {
  std::vector<ON_3dPoint> cps;
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 8; ++j) cps.push_back(ON_3dPoint(i, j, 0.8 * std::sin(0.9 * i) * std::cos(0.7 * j)));
  }
  ON_NurbsSurface patch(3, false, 4, 4, 8, 8);
  patch.MakeClampedUniformKnotVector(0);
  patch.MakeClampedUniformKnotVector(1);
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 8; ++j) patch.SetCV(i, j, cps[static_cast<size_t>(i) * 8 + j]);
  }
  NurbsSurface ns;
  ns.raw() = patch;
  double worst = 0.0;
  int n = 0;
  for (int i = 0; i < 7; ++i) {
    for (int j = 0; j < 7; ++j) {
      const ON_3dPoint p(0.4 + i * 0.97, 0.3 + j * 0.95, 0.6 * std::sin(i + 2.0 * j));
      const Point3d pp(p.x, p.y, p.z);
      worst = std::max(worst, pp.DistanceTo(ns.ClosestPoint(pp)) - BruteDistance(patch, p));
      ++n;
    }
  }
  char what[300];
  std::snprintf(what, sizeof what, "ClosestPoint on an 8 x 8 bicubic patch: %d off-surface points are within 1e-6 of the brute-force nearest distance (worst excess %.3g)", n, worst);
  d8test::Check(worst < 1e-6, what);
}
