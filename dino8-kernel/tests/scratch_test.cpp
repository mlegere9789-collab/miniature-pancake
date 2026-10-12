#include <cstdio>
#include <cmath>
#include <vector>
#include "dino8/kernel/surface.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  auto build_tent = [](double h) {
    const std::vector<Point3d> grid = {
        Point3d(0, 0, 0), Point3d(0, 1, 0),
        Point3d(1, 0, h), Point3d(1, 1, h),
        Point3d(2, 0, 0), Point3d(2, 1, 0),
    };
    return NurbsSurface::FromControlGrid(grid, 3, 2, 1, 1);
  };

  {
    const NurbsSurface tent = build_tent(1.0);
    const auto pieces = tent.SplitAtCreases();
    std::printf("90deg ridge: pieces=%zu\n", pieces.size());
    for (const auto& p : pieces) {
      const auto d0 = p.Domain(0);
      std::printf("  piece domain(0)=[%g,%g] PointAt(min,0)=(%g,%g,%g) PointAt(max,0)=(%g,%g,%g)\n", d0.min, d0.max,
                  p.PointAt(d0.min, 0).x, p.PointAt(d0.min, 0).y, p.PointAt(d0.min, 0).z, p.PointAt(d0.max, 0).x,
                  p.PointAt(d0.max, 0).y, p.PointAt(d0.max, 0).z);
    }
  }

  {
    const NurbsSurface flat = build_tent(0.0);
    const auto pieces = flat.SplitAtCreases();
    std::printf("flat (h=0): pieces=%zu\n", pieces.size());
  }

  {
    const double h_small = std::tan(1.5 * 3.14159265358979323846 / 180.0);
    const NurbsSurface subtle = build_tent(h_small);
    const auto default_pieces = subtle.SplitAtCreases();
    std::printf("3deg ridge, default tol: pieces=%zu\n", default_pieces.size());
    const auto tight_pieces = subtle.SplitAtCreases(1.0 * 3.14159265358979323846 / 180.0);
    std::printf("3deg ridge, 1deg tol: pieces=%zu\n", tight_pieces.size());
  }

  {
    NurbsSurface tent_v = build_tent(1.0);
    tent_v.Transpose();
    const auto pieces = tent_v.SplitAtCreases();
    std::printf("90deg ridge in V (after Transpose): pieces=%zu\n", pieces.size());
  }

  ON::End();
  return 0;
}
