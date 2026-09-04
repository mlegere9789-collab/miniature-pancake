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

  // Whether the surface wraps seamlessly onto itself in `direction`
  // (0 = U, 1 = V) - the boundary curves at the two ends of that
  // parameter coincide exactly, either because the surface is periodic
  // (its own knot vector wraps, e.g. a full cylinder or sphere built via
  // ON_Cylinder::GetNurbForm/ON_Sphere::GetNurbForm) or because a clamped
  // surface's own two edge curves just happen to be coincident. A real
  // gap nothing here could answer before: `TessellateGrid()`'s own
  // regular-grid tessellation has no way to know a periodic surface's
  // `u=0` and `u=2pi` boundaries are the same curve, so a caller building
  // a cylindrical/spherical Brep face by hand (see `Brep::Sphere()`) has
  // to know this independently of anything this wrapper exposed until
  // now. Delegates to `ON_NurbsSurface::IsClosed` after verifying it's a
  // real implementation (checks the knot vector and actual coincident
  // control points, not a stub).
  bool IsClosed(int direction) const;

  // Whether the surface's own knot vector in `direction` (0 = U, 1 = V)
  // is genuinely periodic - a stronger condition than IsClosed()
  // (every periodic surface is closed, but a clamped surface can be
  // closed - matching end curves - without being periodic at all).
  // Delegates to `ON_NurbsSurface::IsPeriodic` after the same
  // stub-vs-real verification.
  bool IsPeriodic(int direction) const;

  // Reverses the surface's parameterization in `direction` (0 = U,
  // 1 = V) in place: same 3D shape, but that direction now runs the
  // opposite way, which flips the surface's own outward normal (since
  // `u_dir x v_dir` negates when either direction reverses) - the
  // surface-level counterpart to `NurbsCurve::Reverse()`, with the same
  // caveat confirmed there: the domain interval's own min/max values
  // aren't necessarily preserved, so re-fetch `raw().Domain(direction)`
  // afterward rather than reusing one captured before calling this.
  // Delegates to `ON_NurbsSurface::Reverse` after verifying it's a real
  // implementation. Returns Result::Failed if OpenNURBS' own call fails.
  Result Reverse(int direction);

  // Swaps the surface's U and V parameterizations in place: what was
  // `PointAt(u, v)` becomes `PointAt(v, u)`. Also flips the outward
  // normal (`u_dir x v_dir` becomes `v_dir x u_dir = -(u_dir x v_dir)`),
  // same as Reverse(). Delegates to `ON_NurbsSurface::Transpose` after
  // verifying it's a real implementation (swaps the actual control point
  // grid and knot vectors, not a stub). Always succeeds (matching
  // `ON_NurbsSurface::Transpose`'s own unconditional `true`), so returns
  // void rather than a `Result` a caller would never see fail.
  void Transpose();

  // Shortens the surface in place to the sub-range `[t0, t1]` in
  // `direction` (0 = U, 1 = V), leaving the other direction's domain
  // unchanged - the surface-level counterpart to `NurbsCurve::Trim()`,
  // same underlying idea (a genuine restriction of the existing surface,
  // not a re-sampled approximation). Delegates to `ON_NurbsSurface::Trim`
  // after verifying it's a real implementation (converts that direction
  // to an isocurve, trims it via the same algorithm `NurbsCurve::Trim()`
  // uses, and writes the result back - not a stub). Returns
  // Result::Failed if `t0 >= t1` or OpenNURBS' own call fails.
  Result Trim(int direction, double t0, double t1);

  // Splits the surface at parameter `t` in `direction` (0 = U, 1 = V)
  // into two independent surfaces written to `out_west_or_south` (the
  // west/south side, i.e. the sub-range below `t`) and
  // `out_east_or_north` (the east/north side, above `t`) - the
  // surface-level counterpart to `NurbsCurve::Split()`, same "keep both
  // halves instead of discarding one" idea `Trim()` doesn't offer.
  // Delegates to `ON_Surface::Split` through its old-style `ON_Surface*&`
  // output-parameter API, casting back to `ON_NurbsSurface`. The other
  // direction's domain is left unchanged in both halves, same as
  // `Trim()`. Returns Result::Failed if `t` doesn't strictly split the
  // domain (e.g. it sits at an endpoint) or OpenNURBS' own call fails or
  // doesn't hand back genuine NURBS surfaces.
  Result Split(int direction, double t, NurbsSurface& out_west_or_south,
               NurbsSurface& out_east_or_north) const;

  Point3d PointAt(double u, double v) const;

  // Unit surface normal at parameter (u, v): `d/du x d/dv`, normalized.
  // Delegates to `ON_Surface::EvNormal` - verified as a real
  // implementation (computes the cross product of the two partial
  // derivatives from `Ev1Der`, not a stub) before relying on it, the same
  // discipline this file's own TessellateGrid() comment already applies
  // to `ON_Brep::CreateMesh`/`ON_Surface::CreateMesh`. Throws
  // std::runtime_error if OpenNURBS itself can't evaluate a normal there
  // (e.g. a genuinely singular point, where the two partials are
  // parallel or one is zero) rather than returning a meaningless
  // placeholder vector.
  Vector3d NormalAt(double u, double v) const;

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
  // pieces. `trim_polygon` must be a simple (non-self-intersecting)
  // polygon - checked (dino8::kernel::detail::IsSimplePolygon), throwing
  // std::invalid_argument otherwise, since a self-intersecting trim isn't
  // decomposable into a well-defined "inside" at all. That check only
  // catches genuine edge-edge crossings, not every possible degeneracy
  // (e.g. an edge passing exactly through a non-adjacent vertex). The
  // concave path is newer and more narrowly tested than the convex one;
  // like PointInPolygon's own documented boundary caveat, a
  // `trim_polygon` vertex landing exactly on a grid line, or a cell
  // boundary crossed an unusual number of times by a highly irregular
  // concave shape, are known-unhardened corners of it.
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
