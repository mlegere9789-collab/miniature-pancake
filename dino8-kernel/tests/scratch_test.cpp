#include <cstdio>
#include <cmath>
#include <vector>
#include "dino8/kernel/surface.h"
#include "dino8/kernel/surface_intersect.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // 1) Box: query point outside, near one face -> closest point should
  // be the projection onto that face, distance = perpendicular offset.
  const Brep box = Brep::Box(0, 0, 0, 2, 2, 2);
  {
    const auto r = ClosestPointOnBrep(Point3d(1, 1, 5), box.raw());
    std::printf("[1] box query(1,1,5): face=%d dist=%g point=(%g,%g,%g) on_boundary=%d\n", r.face_index,
                r.distance, r.point.x, r.point.y, r.point.z, (int)r.on_trim_boundary);
  }
  // 2) Query point near a corner, outside the box diagonally - distance
  // should be hand-derivable (corner is (2,2,2); query (3,3,3) -> dist
  // sqrt(3)).
  {
    const auto r = ClosestPointOnBrep(Point3d(3, 3, 3), box.raw());
    std::printf("[2] box query(3,3,3) corner: face=%d dist=%g (expect %g) point=(%g,%g,%g)\n",
                r.face_index, r.distance, std::sqrt(3.0), r.point.x, r.point.y, r.point.z);
  }
  // 3) Query point inside the box: closest point should be on the
  // nearest face, distance = perpendicular distance to that face.
  {
    const auto r = ClosestPointOnBrep(Point3d(0.3, 1, 1), box.raw());
    std::printf("[3] box query(0.3,1,1) inside: face=%d dist=%g (expect 0.3) point=(%g,%g,%g)\n",
                r.face_index, r.distance, r.point.x, r.point.y, r.point.z);
  }

  // 4) Trimmed face (genuine topology via TrimmedPlanarFaceWelded): a
  // flat plane at z=2 over world [0,2]^2, trimmed to a small square in
  // (u,v) param space [0.25,0.75]^2 (world [0.5,1.5]^2). Query point
  // directly "above" a spot OUTSIDE the trim but within the full
  // surface's domain - closest point should land ON the trim boundary,
  // not at the unconstrained minimum (which would be directly below the
  // query, inside the full domain but outside the trim).
  const std::vector<Point3d> plane_grid = {Point3d(0, 0, 2), Point3d(0, 2, 2), Point3d(2, 0, 2),
                                            Point3d(2, 2, 2)};
  const NurbsSurface flat_plane = NurbsSurface::FromControlGrid(plane_grid, 2, 2, 1, 1);
  const std::vector<Point2d> small_trim = {Point2d(0.25, 0.25), Point2d(0.75, 0.25), Point2d(0.75, 0.75),
                                            Point2d(0.25, 0.75)};
  const Brep trimmed_top = Brep::TrimmedPlanarFaceWelded(flat_plane, small_trim);
  {
    const ON_BrepFace& f = trimmed_top.raw().m_F[0];
    std::printf("face has %d loops\n", f.m_li.Count());
    for (int li = 0; li < f.m_li.Count(); ++li) {
      const ON_BrepLoop& loop = trimmed_top.raw().m_L[f.m_li[li]];
      std::printf("  loop %d: type=%d, %d trims\n", li, (int)loop.m_type, loop.m_ti.Count());
      for (int k = 0; k < loop.m_ti.Count(); ++k) {
        const ON_BrepTrim& trim = trimmed_top.raw().m_T[loop.m_ti[k]];
        const ON_Interval d = trim.Domain();
        const ON_3dPoint p0 = trim.PointAt(d.Min());
        const ON_3dPoint p1 = trim.PointAt(d.Max());
        std::printf("    trim %d: iso=%d domain=[%g,%g] uv0=(%g,%g) uv1=(%g,%g)\n", k,
                    (int)trim.m_iso, d.Min(), d.Max(), p0.x, p0.y, p1.x, p1.y);
      }
    }
  }
  {
    // world (0.2, 0.2, 5) -> directly above param (0.1,0.1), outside trim.
    const auto r = ClosestPointOnBrep(Point3d(0.2, 0.2, 5), trimmed_top.raw());
    std::printf("[4] trimmed query(0.2,0.2,5) outside trim: dist=%g uv=(%g,%g) point=(%g,%g,%g) "
                "on_boundary=%d\n",
                r.distance, r.uv.x, r.uv.y, r.point.x, r.point.y, r.point.z, (int)r.on_trim_boundary);
    // The unconstrained minimum would be (0.2,0.2,2), distance 3. If the
    // boundary fallback worked, distance should be > 3 (forced away from
    // directly-below) and uv should sit on the trim's own boundary
    // (u or v very close to 0.25 or 0.75).
  }
  {
    // world (1, 1, 5) -> directly above param (0.5, 0.5), INSIDE trim -
    // unconstrained minimum should be used directly, distance exactly 3.
    const auto r = ClosestPointOnBrep(Point3d(1, 1, 5), trimmed_top.raw());
    std::printf("[5] trimmed query(1,1,5) inside trim: dist=%g (expect 3) uv=(%g,%g) on_boundary=%d\n",
                r.distance, r.uv.x, r.uv.y, (int)r.on_trim_boundary);
  }

  try {
    ON_Brep empty;
    ClosestPointOnBrep(Point3d(0, 0, 0), empty);
    std::printf("ERROR: expected throw for empty brep\n");
  } catch (const std::invalid_argument& e) {
    std::printf("OK threw for empty brep: %s\n", e.what());
  }

  ON::End();
  return 0;
}
