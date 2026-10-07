#include <cstdio>
#include <cmath>
#include <vector>
#include "dino8/kernel/surface.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // 1) Flat surface: MeasureMeshTessellationDeviation should be ~0.
  {
    const std::vector<Point3d> flat_grid = {
        Point3d(0, 0, 0), Point3d(0, 10, 0), Point3d(10, 0, 0), Point3d(10, 10, 0)};
    NurbsSurface flat = NurbsSurface::FromControlGrid(flat_grid, 2, 2, 1, 1);
    Mesh flat_mesh = flat.TessellateGrid(3, 3);
    const double dev = flat.MeasureMeshTessellationDeviation(flat_mesh);
    std::printf("[1] flat mesh deviation = %g (expect ~0)\n", dev);
  }

  // 2) Cylinder wall cross-check: grid-based vs mesh-based on same mesh.
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 1.0);
  const ON_Cylinder cylinder(circle, 1.0);
  ON_NurbsSurface cylinder_surface;
  cylinder.GetNurbForm(cylinder_surface);
  NurbsSurface wall;
  wall.raw() = cylinder_surface;
  {
    Mesh grid_mesh = wall.TessellateGrid(4, 1);
    const double grid_based = wall.MeasureGridTessellationDeviation(4, 1);
    const double mesh_based = wall.MeasureMeshTessellationDeviation(grid_mesh);
    std::printf("[2] grid_based=%g mesh_based=%g (mesh_based should be <= grid_based, close)\n",
                grid_based, mesh_based);
  }

  // 3) Trimmed curved wedge: certified exact-clip path.
  {
    const auto du = wall.Domain(0);
    const auto dv = wall.Domain(1);
    const double u0 = du.min + 0.25 * (du.max - du.min);
    const double u1 = du.min + 0.75 * (du.max - du.min);
    const double v0 = dv.min + 0.1 * (dv.max - dv.min);
    const double v1 = dv.min + 0.9 * (dv.max - dv.min);
    const std::vector<Point2d> trim = {Point2d(u0, v0), Point2d(u1, v0), Point2d(u1, v1),
                                        Point2d(u0, v1)};
    for (double tol : {0.1, 0.01, 0.001}) {
      try {
        double achieved = -1.0;
        Mesh m = wall.TessellateGridClippedExactCertifiedAdaptive(tol, trim, 8, &achieved);
        std::printf("[3] tol=%g faces=%d achieved=%g (<=tol? %d)\n", tol, m.FaceCount(), achieved,
                    achieved <= tol);
      } catch (const std::exception& e) {
        std::printf("[3] tol=%g threw: %s\n", tol, e.what());
      }
    }
  }

  // 4) Sphere (singular poles) via general non-uniform certified path, untrimmed.
  const ON_Sphere sphere(ON_3dPoint(0, 0, 0), 1.0);
  ON_NurbsSurface sphere_surface;
  sphere.GetNurbForm(sphere_surface);
  NurbsSurface s;
  s.raw() = sphere_surface;
  {
    for (double tol : {0.1, 0.01, 0.001}) {
      try {
        double achieved = -1.0;
        Mesh m = s.TessellateGridNonUniformCertifiedAdaptive(tol, nullptr, nullptr, 8, &achieved);
        std::printf("[4] sphere untrimmed tol=%g faces=%d achieved=%g (<=tol? %d)\n", tol,
                    m.FaceCount(), achieved, achieved <= tol);
      } catch (const std::exception& e) {
        std::printf("[4] sphere untrimmed tol=%g threw: %s\n", tol, e.what());
      }
    }
  }

  // 5) Sphere with a hole: general non-uniform certified path.
  {
    const auto du = s.Domain(0);
    const auto dv = s.Domain(1);
    const std::vector<Point2d> outer = {Point2d(du.min, dv.min), Point2d(du.max, dv.min),
                                         Point2d(du.max, dv.max), Point2d(du.min, dv.max)};
    const double hu0 = du.min + 0.4 * (du.max - du.min);
    const double hu1 = du.min + 0.6 * (du.max - du.min);
    const double hv0 = dv.min + 0.45 * (dv.max - dv.min);
    const double hv1 = dv.min + 0.55 * (dv.max - dv.min);
    const std::vector<std::vector<Point2d>> holes = {
        {Point2d(hu0, hv0), Point2d(hu1, hv0), Point2d(hu1, hv1), Point2d(hu0, hv1)}};
    for (double tol : {0.1, 0.01}) {
      try {
        double achieved = -1.0;
        Mesh m = s.TessellateGridNonUniformCertifiedAdaptive(tol, &outer, &holes, 8, &achieved);
        std::printf("[5] sphere holed tol=%g faces=%d achieved=%g (<=tol? %d)\n", tol,
                    m.FaceCount(), achieved, achieved <= tol);
      } catch (const std::exception& e) {
        std::printf("[5] sphere holed tol=%g threw: %s\n", tol, e.what());
      }
    }
  }

  // 6) Brep-level: TrimmedPlanarFace wedge should now report certified=true.
  {
    const auto du = wall.Domain(0);
    const auto dv = wall.Domain(1);
    const double u0 = du.min + 0.25 * (du.max - du.min);
    const double u1 = du.min + 0.75 * (du.max - du.min);
    const double v0 = dv.min + 0.1 * (dv.max - dv.min);
    const double v1 = dv.min + 0.9 * (dv.max - dv.min);
    const std::vector<Point2d> trim = {Point2d(u0, v0), Point2d(u1, v0), Point2d(u1, v1),
                                        Point2d(u0, v1)};
    Brep trimmed_wall = Brep::TrimmedPlanarFace(wall, trim, /*exact_clip=*/true);
    std::vector<bool> certified;
    try {
      auto faces = trimmed_wall.TessellateCertifiedAdaptive(0.01, 8, &certified);
      std::printf("[6] faces=%zu certified.size=%zu certified[0]=%d facecount[0]=%d\n",
                  faces.size(), certified.size(), certified.empty() ? -1 : (int)certified[0],
                  faces.empty() ? -1 : faces[0].FaceCount());
    } catch (const std::exception& e) {
      std::printf("[6] threw: %s\n", e.what());
    }
  }

  // 7) Brep::Sphere() (whole-domain, untrimmed, but goes through the
  // normal fg.outer.empty() -> TessellateGridCertifiedAdaptive branch) and
  // Brep::Torus() exercised through TessellateCertifiedAdaptive for a
  // broader sanity check across existing primitives.
  {
    Brep sph = Brep::Sphere(Point3d(0, 0, 0), 2.0);
    std::vector<bool> certified;
    try {
      auto faces = sph.TessellateCertifiedAdaptive(0.05, 8, &certified);
      bool all_true = true;
      for (bool c : certified) all_true = all_true && c;
      std::printf("[7] Brep::Sphere faces=%zu all_certified=%d\n", faces.size(), all_true);
    } catch (const std::exception& e) {
      std::printf("[7] Brep::Sphere threw: %s\n", e.what());
    }
  }

  ON::End();
  return 0;
}
