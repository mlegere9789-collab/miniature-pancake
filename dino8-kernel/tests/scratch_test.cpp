#include <cmath>
#include <cstdio>
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // NurbsCurve::MinDistanceTo(NurbsSurface) and NurbsSurface::MinDistanceTo
  // (NurbsSurface), exercised standalone: the full dino8_kernel_tests
  // binary aborts earlier (in the pre-existing, disclosed
  // BooleanCombineGeneral "3+ fragment loops" limitation, which several
  // early TestBooleanCombineGeneral* fixtures happen to trip) before it
  // ever reaches TestCurveMinDistanceToSurface/TestSurfaceMinDistanceTo,
  // so this checks the same logic those tests exercise, directly, in
  // isolation.

  // --- NurbsCurve::MinDistanceTo(NurbsSurface) ---
  {
    const std::vector<Point3d> plane_grid = {
        Point3d(0, 0, 0),
        Point3d(0, 10, 0),
        Point3d(10, 0, 0),
        Point3d(10, 10, 0),
    };
    const NurbsSurface plane = NurbsSurface::FromControlGrid(plane_grid, 2, 2, 1, 1);
    const NurbsCurve line = NurbsCurve::FromControlPoints({Point3d(2, 2, 5), Point3d(8, 8, 5)}, 1);
    double dist = -1;
    Point2d uv(-1, -1);
    double t = -1;
    const Result r = line.MinDistanceTo(plane, 0.001, dist, &t, &uv);
    std::printf("curve/surface parallel: Ok=%d dist=%.6f (expect ~5)\n", r == Result::Ok, dist);
    std::printf("  closest-pair consistency: err=%.3e (expect ~0, i.e. the two points really are `dist` apart)\n",
                std::fabs(line.PointAt(t).DistanceTo(plane.PointAt(uv.x, uv.y)) - dist));

    const NurbsCurve piercing = NurbsCurve::FromControlPoints({Point3d(5, 5, -3), Point3d(5, 5, 3)}, 1);
    double dist2 = -1;
    const Result r2 = piercing.MinDistanceTo(plane, 0.001, dist2);
    std::printf("curve/surface piercing: Ok=%d dist=%.6f (expect ~0)\n", r2 == Result::Ok, dist2);

    bool threw = false;
    try {
      line.MinDistanceTo(plane, 0.0, dist);
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    std::printf("curve/surface non-positive tolerance throws: %d\n", threw);
  }

  // --- NurbsSurface::MinDistanceTo(NurbsSurface) ---
  {
    const std::vector<Point3d> grid_a = {
        Point3d(0, 0, 0),
        Point3d(0, 10, 0),
        Point3d(10, 0, 0),
        Point3d(10, 10, 0),
    };
    const std::vector<Point3d> grid_b = {
        Point3d(0, 0, 4),
        Point3d(0, 10, 4),
        Point3d(10, 0, 4),
        Point3d(10, 10, 4),
    };
    const NurbsSurface a = NurbsSurface::FromControlGrid(grid_a, 2, 2, 1, 1);
    const NurbsSurface b = NurbsSurface::FromControlGrid(grid_b, 2, 2, 1, 1);
    double dist = -1;
    Point2d uv_a(-1, -1), uv_b(-1, -1);
    const Result r = a.MinDistanceTo(b, 0.001, dist, &uv_a, &uv_b);
    std::printf("surface/surface parallel planes: Ok=%d dist=%.6f (expect ~4)\n", r == Result::Ok, dist);
    std::printf("  closest-pair consistency: err=%.3e (expect ~0, i.e. the two points really are `dist` apart)\n",
                std::fabs(a.PointAt(uv_a.x, uv_a.y).DistanceTo(b.PointAt(uv_b.x, uv_b.y)) - dist));

    const Point3d center(1, -2, 0.5);
    const double r1 = 3.0, r2v = 5.0;
    const ON_Sphere s1(center, r1);
    const ON_Sphere s2(center, r2v);
    ON_NurbsSurface ns1, ns2;
    const bool nurb_ok = s1.GetNurbForm(ns1) != 0 && s2.GetNurbForm(ns2) != 0;
    std::printf("sphere fixtures build real NURBS form: %d\n", nurb_ok);
    NurbsSurface sa, sb;
    sa.raw() = ns1;
    sb.raw() = ns2;
    double sdist = -1;
    const Result sr = sa.MinDistanceTo(sb, 0.01, sdist);
    std::printf("surface/surface concentric spheres: Ok=%d dist=%.6f (expect ~%.6f)\n", sr == Result::Ok, sdist,
                r2v - r1);

    bool threw2 = false;
    try {
      a.MinDistanceTo(a, 0.0, dist);
    } catch (const std::invalid_argument&) {
      threw2 = true;
    }
    std::printf("surface/surface non-positive tolerance throws: %d\n", threw2);
  }

  ON::End();
  return 0;
}
