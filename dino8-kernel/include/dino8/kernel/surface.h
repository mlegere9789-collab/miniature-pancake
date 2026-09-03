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
  //
  // `hole_polygons`, if non-null, is a list of additional closed polygons
  // (same parameter space) subtracted from `trim_polygon` - a cell is
  // emitted only if all four corners are inside `trim_polygon` and
  // outside every hole polygon, giving an annulus/washer-shaped face
  // (still whole-cell approximated, same as the outer boundary).
  // Meaningless if `trim_polygon` is null.
  Mesh TessellateGrid(int u_divisions, int v_divisions,
                       const std::vector<Point2d>* trim_polygon = nullptr,
                       const std::vector<std::vector<Point2d>>* hole_polygons = nullptr) const;

  // Real boundary clipping, unlike TessellateGrid()'s whole-cell in/out:
  // each grid cell is clipped against `trim_polygon` rather than kept or
  // dropped wholesale, so a cell straddling the trim boundary contributes
  // its actual clipped sub-area, evaluated at the true intersection
  // points - not approximated by the grid resolution. This is why
  // Mesh::Cylinder() needed 200 divisions for 2% volume accuracy with
  // TessellateGrid()'s trim_polygon, and would need far fewer here.
  //
  // `trim_polygon` may now be concave (even self-crossing the cell
  // boundary in a way that splits one cell into several disjoint
  // sub-regions) - an earlier version of this method rejected any
  // non-convex trim outright. Internally, a convex trim_polygon still
  // goes through the original, long-proven Sutherland-Hodgman clipping +
  // triangle-fan path (what Mesh::Cylinder()'s circular trim and every
  // other existing caller exercises); a concave one falls back to a
  // general (Greiner-Hormann-style) polygon intersection with
  // ear-clipping triangulation for the (possibly non-convex) clipped
  // pieces. `trim_polygon` must still be a simple (non-self-intersecting)
  // polygon - this isn't validated here, since a self-intersecting trim
  // isn't decomposable into a well-defined "inside" at all. The concave
  // path is newer and more narrowly tested than the convex one; like
  // PointInPolygon's own documented boundary caveat, a `trim_polygon`
  // vertex landing exactly on a grid line, or a cell boundary crossed an
  // unusual number of times by a highly irregular concave shape, are
  // known-unhardened corners of it.
  //
  // The returned mesh is already welded (via Mesh::MergeAndWeld) since
  // adjacent cells independently compute the same boundary-intersection
  // points as separate vertices that need collapsing to form a single
  // consistent mesh.
  Mesh TessellateGridClippedExact(int u_divisions, int v_divisions,
                                   const std::vector<Point2d>& trim_polygon) const;

  const ON_NurbsSurface& raw() const { return surface_; }
  ON_NurbsSurface& raw() { return surface_; }

 private:
  ON_NurbsSurface surface_;
};

}  // namespace dino8::kernel
