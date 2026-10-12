#include <cmath>
#include <cstdio>
#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // NurbsSurface::GetOrientedBoundingBox() and Brep::GetOrientedBoundingBox(),
  // exercised standalone as a cross-check before trusting the real
  // registered tests (TestSurfaceGetOrientedBoundingBox,
  // TestBrepGetOrientedBoundingBox).

  // --- NurbsSurface::GetOrientedBoundingBox() ---
  {
    const std::vector<Point3d> grid = {
        Point3d(0, 0, 0),
        Point3d(0, 10, 0),
        Point3d(20, 0, 0),
        Point3d(20, 10, 0),
    };
    const NurbsSurface rect = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
    const NurbsSurface::OrientedBox box = rect.GetOrientedBoundingBox();
    std::printf("surface OBB center = (%.6f, %.6f, %.6f) (expect ~(10,5,0))\n", box.center.x, box.center.y,
                box.center.z);
    for (int k = 0; k < 3; ++k) {
      std::printf("  axis %d = (%.6f, %.6f, %.6f) half_extent=%.6f\n", k, box.axes[static_cast<size_t>(k)].x,
                  box.axes[static_cast<size_t>(k)].y, box.axes[static_cast<size_t>(k)].z,
                  box.half_extents[static_cast<size_t>(k)]);
    }
    // Safety check: every control point within the box along every axis.
    double max_overshoot = 0.0;
    for (int i = 0; i < rect.CVCountU(); ++i) {
      for (int j = 0; j < rect.CVCountV(); ++j) {
        const Vector3d rel = rect.ControlPointAt(i, j) - box.center;
        for (int k = 0; k < 3; ++k) {
          const double proj = std::fabs(ON_DotProduct(rel, box.axes[static_cast<size_t>(k)]));
          max_overshoot = std::max(max_overshoot, proj - box.half_extents[static_cast<size_t>(k)]);
        }
      }
    }
    std::printf("surface OBB max control-point overshoot = %.3e (expect <= ~0)\n", max_overshoot);
  }

  // --- Brep::GetOrientedBoundingBox() ---
  {
    const Brep box = Brep::Box(1, -2, 0.5, 4, 3, 7.5);
    const Brep::OrientedBox obb = box.GetOrientedBoundingBox();
    std::printf("brep OBB center = (%.6f, %.6f, %.6f) (expect ~(2.5, 0.5, 4.0))\n", obb.center.x, obb.center.y,
                obb.center.z);
    for (int k = 0; k < 3; ++k) {
      std::printf("  axis %d = (%.6f, %.6f, %.6f) half_extent=%.6f\n", k, obb.axes[static_cast<size_t>(k)].x,
                  obb.axes[static_cast<size_t>(k)].y, obb.axes[static_cast<size_t>(k)].z,
                  obb.half_extents[static_cast<size_t>(k)]);
    }
    double max_overshoot = 0.0;
    const ON_Brep& raw_box = box.raw();
    for (int fi = 0; fi < raw_box.m_F.Count(); ++fi) {
      const ON_Surface* srf = raw_box.m_F[fi].SurfaceOf();
      ON_NurbsSurface nurbs;
      const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(srf);
      if (ns) {
        nurbs = *ns;
      } else {
        srf->GetNurbForm(nurbs);
      }
      for (int i = 0; i < nurbs.CVCount(0); ++i) {
        for (int j = 0; j < nurbs.CVCount(1); ++j) {
          ON_3dPoint cv;
          nurbs.GetCV(i, j, cv);
          const Vector3d rel = Point3d(cv) - obb.center;
          for (int k = 0; k < 3; ++k) {
            const double proj = std::fabs(ON_DotProduct(rel, obb.axes[static_cast<size_t>(k)]));
            max_overshoot = std::max(max_overshoot, proj - obb.half_extents[static_cast<size_t>(k)]);
          }
        }
      }
    }
    std::printf("brep OBB max control-point overshoot = %.3e (expect <= ~0)\n", max_overshoot);

    Brep empty;
    bool threw = false;
    try {
      empty.GetOrientedBoundingBox();
    } catch (const std::runtime_error&) {
      threw = true;
    }
    std::printf("brep OBB empty-brep throws runtime_error: %d\n", threw);
  }

  ON::End();
  return 0;
}
