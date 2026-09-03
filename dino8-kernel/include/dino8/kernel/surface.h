#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::kernel {

class Mesh;

// Wraps ON_NurbsSurface. Same rationale as NurbsCurve: expose raw()
// rather than mirror the whole OpenNURBS surface API.
class NurbsSurface {
 public:
  // Builds a bilinear-ish degree-(u_degree, v_degree) NURBS surface from a
  // u_count x v_count grid of control points, row-major (u varies fastest).
  static NurbsSurface FromControlGrid(const std::vector<Point3d>& control_grid,
                                       int u_count, int v_count, int u_degree,
                                       int v_degree);

  int DegreeU() const;
  int DegreeV() const;

  // Elevates degree in the given direction (0 = U, 1 = V). Returns
  // NoOpAlreadySatisfied if the surface is already at or above that degree.
  Result ElevateDegree(int direction, int new_degree);

  Point3d PointAt(double u, double v) const;

  // Tessellates the surface into a triangle mesh by evaluating a
  // u_divisions x v_divisions grid of points across its parameter domain
  // and triangulating each grid cell. This is a from-scratch tessellator,
  // not OpenNURBS': ON_Brep::CreateMesh / ON_Surface::CreateMesh are
  // declared in OpenNURBS' public headers but have no implementation in
  // the public source (verified against v8.34) — they're stubs for
  // Rhino's closed-source mesher. A real product needs a proper adaptive
  // mesher (curvature-aware, trim-aware); this grid version exists to
  // unblock chunk 2's mesh-boolean work, not as the final mesher.
  //
  // `trim_polygon`, if non-null, is a closed polygon in this surface's own
  // (u, v) parameter space (not normalized 0..1 - actual surface
  // parameter values). A grid cell is emitted only if all four of its
  // corners fall inside the polygon; cells straddling the boundary are
  // dropped rather than clipped, so the trimmed edge is only as accurate
  // as the grid resolution - a real trim-aware mesher would clip the
  // boundary cells to the actual curve instead of discarding them.
  // Vertices that end up unused (entirely outside the trim) are not
  // included in the output mesh.
  Mesh TessellateGrid(int u_divisions, int v_divisions,
                       const std::vector<Point2d>* trim_polygon = nullptr) const;

  const ON_NurbsSurface& raw() const { return surface_; }
  ON_NurbsSurface& raw() { return surface_; }

 private:
  ON_NurbsSurface surface_;
};

}  // namespace dino8::kernel
