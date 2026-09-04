#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::kernel {

class Mesh;

// The four scalar curvature values at one point on a surface, from
// classical differential geometry's first/second fundamental forms:
// `gaussian` = k1*k2 (positive on a dome/bowl-shaped point, negative on
// a saddle, zero on a flat or single-curved point like a cylinder),
// `mean` = (k1+k2)/2, and `k1`/`k2` are the two principal curvatures
// themselves (the max/min normal curvature over all tangent directions
// at that point) - `k1 >= k2` always. `mean`/`k1`/`k2`'s sign depends on
// which way the surface's own normal points (`NormalAt()`'s convention,
// i.e. `du x dv`): verified empirically, not just asserted from the
// formula, against a sphere built via `ON_Sphere::GetNurbForm` - its
// outward-pointing normal gives every point a *negative* mean curvature
// (`-1/radius`) and negative principal curvatures, i.e. the surface
// curves away from its own outward normal, toward the sphere's
// interior. `gaussian` has no such sign ambiguity (it's a product of two
// curvatures under the same sign convention, so the signs cancel): a
// sphere's Gaussian curvature is `+1/radius^2` regardless of which way
// its normal points.
struct SurfaceCurvature {
  double gaussian;
  double mean;
  double k1;
  double k2;
};

// A suggested u/v division count for TessellateGrid()/
// TessellateGridClippedExact(), returned by NurbsSurface::
// SuggestedDivisions().
struct SurfaceDivisions {
  int u;
  int v;
};

// An approximate physical width/height of a surface's own domain,
// returned by NurbsSurface::GetApproximateSize().
struct SurfaceSize {
  double width;   // approximate size in the U direction
  double height;  // approximate size in the V direction
};

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

  // Number of control points in each direction - the surface-level
  // counterpart to `NurbsCurve::ControlPointCount()`, a real gap this
  // file had never filled (only `DegreeU()`/`DegreeV()` existed; nothing
  // exposed the control grid's own dimensions, e.g. to iterate
  // `ControlPointAt(i, j)`-style access via `raw()` without reaching in
  // just to find out how far `i`/`j` may range).
  int CVCountU() const;
  int CVCountV() const;

  // Same reasoning as `NurbsCurve::IsRational()` - whether the surface's
  // control points carry non-uniform weights (e.g. a genuine torus/
  // sphere/cylinder/cone NURBS form, all built via rational
  // constructions elsewhere in this kernel), as opposed to
  // `FromControlGrid()`'s own always-non-rational construction.
  // Delegates to `ON_NurbsSurface::IsRational()`.
  bool IsRational() const;

  // Same reasoning as `NurbsCurve::WeightAt()` above, for control point
  // (i, j) - delegates to `ON_NurbsSurface::Weight(i, j)`, whose own
  // source (verified, not assumed) has the same "safe 1.0 on a
  // non-rational surface regardless of i/j, unchecked out-of-bounds read
  // on a rational one" behavior.
  double WeightAt(int i, int j) const;

  // Same reasoning, same "moves that control point's own position, not
  // just its influence" caveat, and same out-of-range-`i`/`j` safety
  // fix as `NurbsCurve::SetWeightAt()` - see there for the full
  // explanation (verified with a hand-derived exact rational-Bezier
  // point, not assumed). Delegates to `ON_NurbsSurface::SetWeight(i, j,
  // w)`. Returns Result::Failed if `i`/`j` is out of range (checked
  // against `CVCountU()`/`CVCountV()` directly, for the same reason
  // `NurbsCurve::SetWeightAt()` checks its own bound rather than relying
  // on OpenNURBS' own deeper check) or Result::NoOpAlreadySatisfied if
  // `weight` already equals `WeightAt(i, j)`.
  Result SetWeightAt(int i, int j, double weight);

  // Same reasoning as `NurbsCurve::ControlPointAt()` - control point
  // (i, j)'s actual Euclidean position, weight already divided out on a
  // rational surface. Delegates to `ON_NurbsSurface::GetCV(i, j,
  // ON_3dPoint&)`. Throws std::out_of_range if `i`/`j` is outside
  // `[0, CVCountU())`/`[0, CVCountV())` (checked directly here, not left
  // to `GetCV()`'s own unchecked indexing).
  Point3d ControlPointAt(int i, int j) const;

  // Same reasoning, and the same real weight-reset caveat, as
  // `NurbsCurve::SetControlPointAt()` - see there. Throws
  // std::out_of_range under the same condition as `ControlPointAt()`.
  Result SetControlPointAt(int i, int j, Point3d point);

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

  // Whether the surface's entire shape lies within `tolerance` of some
  // plane. Delegates to `ON_NurbsSurface::IsPlanar` after verifying it's
  // a real implementation (fits a plane through the surface's own
  // normal at its domain center, then checks every control point's
  // distance to that plane - not a stub, and not just a bounding-box
  // heuristic: since a non-rational NURBS surface always lies within the
  // convex hull of its own control points, "every control point is
  // within `tolerance` of the plane" genuinely guarantees the whole
  // surface is too, not merely a plausible-looking approximation).
  // Defaults to `ON_ZERO_TOLERANCE` (OpenNURBS' own default). Verified
  // against a doubly-curved bicubic bulge surface (the same one
  // `TestBrepGetTightBoundingBoxOvershootsInteriorExtremum` uses, whose
  // single non-zero-z control point sits `peak_height` above the rest):
  // NOT planar-at-tolerance-`peak_height` as a naive guess might assume
  // - the fitted plane passes through the *surface's own evaluated
  // point* at the domain center (`0.25*peak_height`, not `0`), so the
  // real threshold, confirmed empirically rather than assumed, is each
  // control point's distance to *that* plane: `0.75*peak_height` for the
  // peak control point (the largest of the two distances actually
  // checked). Reports non-planar just below that threshold and planar
  // just above it.
  bool IsPlanar(double tolerance = ON_ZERO_TOLERANCE) const;

  // Whether the surface is (a portion of) a sphere within `tolerance`.
  // Delegates to `ON_Surface::IsSphere` - `ON_NurbsSurface` doesn't
  // override this, so it inherits the base class's own real
  // implementation (verified by reading opennurbs_revsurface.cpp, not
  // assumed): takes two isocurves through the domain's own midlines,
  // checks each is genuinely a circular arc (`ON_Curve::IsArc`), then
  // verifies both arcs' fitted spheres agree with each other and with
  // sampled points elsewhere on the surface - a real geometric
  // classification, not a stub or a name-based guess. Verified against
  // `Brep::Sphere()`'s own underlying surface (reports true, matching
  // its known construction) and against both a flat surface and a
  // cylinder wall (report false - neither is spherical).
  bool IsSphere(double tolerance = ON_ZERO_TOLERANCE) const;

  // Whether the surface is (a portion of) a right circular cylinder
  // within `tolerance`. Same inheritance situation as IsSphere():
  // `ON_NurbsSurface` doesn't override `ON_Surface::IsCylinder`, so this
  // is the base class's own real implementation (verified by reading
  // opennurbs_revsurface.cpp) - one isocurve direction must be a
  // circular arc and the other a straight line (or vice versa), with
  // that arc's circle consistent along the line. Verified against the
  // same cylinder wall `IsSphere()`'s own test already builds via
  // `ON_Cylinder::GetNurbForm` (reports true here, correctly false
  // there) - the two methods' tests are each other's negative case,
  // together showing this is a real distinguishing classification, not
  // "any curved surface reports true for everything".
  bool IsCylinder(double tolerance = ON_ZERO_TOLERANCE) const;

  // Whether the surface is (a portion of) a right circular cone within
  // `tolerance`. Same inheritance situation as IsSphere()/IsCylinder():
  // `ON_NurbsSurface` doesn't override `ON_Surface::IsCone`, so this is
  // the base class's own real implementation (verified by reading
  // opennurbs_revsurface.cpp) - structurally almost identical to
  // IsCylinder()'s own check (one isocurve direction a circular arc, the
  // other a straight line), but a cone's line isocurves converge toward
  // a single apex point rather than staying parallel, which is what
  // actually distinguishes the two shapes. Verified against a genuine
  // cone via `ON_Cone::GetNurbForm` (reports true), and against the
  // existing cylinder wall and sphere (both correctly report false -
  // a cylinder's parallel line isocurves never converge to an apex, and
  // a sphere has no straight-line isocurve in either direction at all).
  bool IsCone(double tolerance = ON_ZERO_TOLERANCE) const;

  // Whether the surface is (a portion of) a torus within `tolerance` -
  // the fourth and last of this quadric-classification family alongside
  // IsSphere()/IsCylinder()/IsCone(), same inheritance situation
  // (`ON_NurbsSurface` inherits `ON_Surface::IsTorus`'s real base
  // implementation, verified by reading opennurbs_revsurface.cpp). Both
  // isocurve directions must be circular arcs (unlike IsCone()/
  // IsCylinder(), which need one arc and one line) whose fitted tori
  // agree with each other - the same "two isocurves, cross-check the
  // fitted shape" structure IsSphere() uses, but requiring the second
  // arc's plane to sit offset from the first rather than coincide with
  // it (a sphere's two great-circle arcs share one center; a torus's
  // do not). A real discovery, not assumed: a genuine torus via
  // `ON_Torus::GetNurbForm` reports IsTorus() *false* at the default
  // `tolerance` (`ON_ZERO_TOLERANCE`, ~2.3e-10) - `GetNurbForm`'s own
  // rational biquadratic NURBS construction has floating-point round-off
  // just outside that extremely tight bound - but reports true at a
  // still-tight 1e-6 tolerance; this is a real precision requirement of
  // that specific construction, not a bug in `IsTorus()` itself (the
  // sphere/cylinder/cone cases above all pass at the default tolerance,
  // so this isn't a general problem with the whole classification
  // family). Verified against the existing sphere, cylinder wall, and
  // cone too (all correctly report false at the same 1e-6 tolerance).
  bool IsTorus(double tolerance = ON_ZERO_TOLERANCE) const;

  // An approximate physical width (U direction) and height (V direction)
  // of the surface. Delegates to `ON_NurbsSurface::GetSurfaceSize` after
  // verifying it's a real implementation - but a genuinely approximate
  // one, by the method's own source comment (`// TODO - get lengths of
  // polygon`): it returns each direction's *control polygon length*
  // (the sum of straight-line distances between consecutive control
  // points), not the true arc length of an isocurve through that
  // direction. For a straight/flat surface these coincide exactly (no
  // curvature for a polyline-through-control-points to overstate); for
  // a genuinely curved surface, this overstates the true size, the same
  // direction of error `NurbsCurve::Length()`'s own polyline-sampling
  // approximation has, but from a single coarse 2-point-per-span
  // estimate rather than a convergent fine sampling. Verified exactly
  // on a flat identity-mapped surface, and confirmed to overstate (not
  // understate) a cylinder wall's true circumference (8.0 vs. the
  // true 2*pi ~ 6.28 for a unit-radius circle) - a real, substantial gap
  // from the control polygon's own coarser vertex count, not a rounding
  // artifact.
  SurfaceSize GetApproximateSize() const;

  // Approximate surface area: tessellates via `TessellateGrid()` at
  // `u_divisions x v_divisions` and sums the resulting mesh's own
  // triangle areas (`Mesh::Area()`) - not a from-scratch numeric
  // integration of the first fundamental form, since the tessellator
  // already exists and a flat-triangle approximation of a smooth surface
  // converges to the true area from below as the grid refines (same
  // "understates via straight-line/flat-facet approximation" direction
  // every polyline/polygon approximation in this file has), the mirror
  // image of `GetApproximateSize()`'s "overstates via the control
  // polygon" error direction above - genuinely exact only where the
  // surface has no curvature for a flat facet to fall short of (a flat
  // plane). Throws std::invalid_argument if either division count is
  // less than 1 (`TessellateGrid()`'s own validation - see there).
  double ApproximateArea(int u_divisions = 50, int v_divisions = 50) const;

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

  // Extends the surface in place in `direction` (0 = U, 1 = V) so that
  // direction's domain includes `[t0, t1]` - the surface-level
  // counterpart to `NurbsCurve::Extend()`, `Trim()`'s opposite. Only
  // extends whichever end(s) of `[t0, t1]` actually fall outside the
  // current domain in that direction; the other direction's domain is
  // always left unchanged. Delegates to `ON_NurbsSurface::Extend` after
  // verifying it's a real implementation (converts that direction to an
  // isocurve, extends it via the same `ON_NurbsCurve::Extend` extrapolation
  // `NurbsCurve::Extend()` uses, and writes the result back - not a
  // stub). If `[t0, t1]` already sits entirely within the current domain
  // in `direction`, returns NoOpAlreadySatisfied rather than calling into
  // OpenNURBS at all. Returns Result::Failed if `t0 >= t1`, the surface
  // is closed in `direction` (matches `ON_NurbsSurface::Extend`'s own
  // documented restriction), or OpenNURBS' own call fails.
  Result Extend(int direction, double t0, double t1);

  // The surface's own parameter domain [min, max] in `direction` (0 for
  // u, 1 for v) - the valid range for that argument to `PointAt(u, v)`
  // and every other by-parameter method below. Not necessarily [0, 1] -
  // same caveat as `NurbsCurve::Domain()`.
  Interval Domain(int direction) const;

  Point3d PointAt(double u, double v) const;

  // Finds the (u, v) parameter whose PointAt() is closest to `point`: a
  // coarse `u_divisions` x `v_divisions` grid scan of the full domain,
  // then several rounds of re-scanning a shrinking bracket around the
  // best point found so far - a from-scratch multi-level grid search, the
  // surface-level counterpart to `NurbsCurve::ClosestPointParameter()`'s
  // golden-section search (2D makes the curve's own bracket-and-refine
  // approach awkward, so this uses repeated grid refinement instead, same
  // underlying idea of "sample coarsely, then narrow around the best
  // sample"). Not a guaranteed global minimum for a pathological
  // multi-modal distance function, same honesty
  // `ClosestPointParameter()` documents for the curve case.
  Point2d ClosestPointParameter(Point3d point, int u_divisions = 20, int v_divisions = 20) const;

  // The actual closest point: `PointAt(ClosestPointParameter(point,
  // u_divisions, v_divisions))`. The surface-level counterpart to
  // `Mesh::ClosestPoint()`.
  Point3d ClosestPoint(Point3d point, int u_divisions = 20, int v_divisions = 20) const;

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

  // The four scalar curvature values (see SurfaceCurvature) at (u, v).
  // A from-scratch computation, not a wrapper: verified directly against
  // the v8.34 source that OpenNURBS' public API has no function that
  // computes an ON_SurfaceCurvature from a surface's own derivatives -
  // `ON_SurfaceCurvature` itself is just a plain data holder (a "Create"
  // factory from already-known principal curvature values, comparison
  // operators, etc.), not a curvature evaluator - the same "declared for
  // Rhino data interchange, not present as a public computation" pattern
  // this file already found for `ON_Brep::CreateMesh`. Computed from
  // `ON_Surface::Ev2Der` (verified real: unpacks `ON_NurbsSurface::
  // Evaluate`'s own genuine de Boor evaluation with der_count=2, not a
  // stub) via the classical first/second fundamental form formulas:
  // E=du.du, F=du.dv, G=dv.dv; e=duu.n, f=duv.n, g=dvv.n (n the unit
  // normal); Gaussian K=(eg-f^2)/(EG-F^2), mean H=(eG-2fF+gE)/(2(EG-F^2)),
  // principal curvatures k1,k2 = H +/- sqrt(H^2-K). Throws
  // std::runtime_error if `Ev2Der` fails or the point is singular (zero
  // or parallel partial derivatives, same condition `NormalAt()` already
  // throws on).
  SurfaceCurvature CurvatureAt(double u, double v) const;

  // Suggests u/v division counts for TessellateGrid()/
  // TessellateGridClippedExact() that keep chord deviation under
  // `chord_tolerance` in each direction - the surface-level counterpart
  // to `NurbsCurve::SuggestedSamples()`, and, like it, a first, modest
  // step toward this kernel's own flagged "adaptive/curvature-aware
  // meshing" gap rather than a full per-region adaptive tessellator
  // (still one number per direction for the whole surface, not a
  // varying density across it). Computes each direction independently
  // via `ON_Surface::IsoCurve` (verified real - `ON_NurbsSurface::
  // IsoCurve` builds a genuine `ON_NurbsCurve` by slicing the control net
  // at the given isoparameter, not a stub): samples `isocurve_samples`
  // isocurves running in the *other* direction, wraps each as a
  // `NurbsCurve`, and takes the largest `SuggestedSamples()` result found
  // - since the tightest curvature anywhere along any sampled isocurve in
  // that direction needs to be accounted for. Throws std::invalid_argument
  // if `chord_tolerance <= 0`.
  SurfaceDivisions SuggestedDivisions(double chord_tolerance, int isocurve_samples = 5) const;

  // Returns a genuinely non-uniform, curvature-adaptive set of
  // parameter values in `direction` (0 = U, 1 = V), for use with
  // TessellateGridNonUniform() - real per-region adaptivity in that one
  // direction, unlike SuggestedDivisions()'s single global count.
  // Samples `isocurve_samples` isocurves running in `direction` (each at
  // a different fixed value of the *other* direction, same sampling
  // `SuggestedDivisions()` already does) via `ON_Surface::IsoCurve`,
  // wraps each as a `NurbsCurve`, calls its own
  // `SuggestedParameterValues(chord_tolerance)`, and keeps whichever
  // isocurve produced the most breakpoints - the same "worst case wins"
  // philosophy `SuggestedDivisions()` uses, since the tightest curvature
  // anywhere along any sampled isocurve in that direction needs to be
  // accounted for, and every isocurve in a given direction shares that
  // direction's own domain, so one isocurve's breakpoints are always
  // valid parameter values for every other row/column. Throws
  // std::invalid_argument if `chord_tolerance <= 0`.
  std::vector<double> SuggestedParameterValues(int direction, double chord_tolerance,
                                                int isocurve_samples = 5) const;

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
  //
  // Throws std::invalid_argument if either division count is less than
  // 1 - a real gap this method used to have, found while adding
  // `ApproximateArea()`: without this check, a `0` division count
  // silently produced `NaN` parameter values via an unguarded `0/0`
  // division (confirmed by reading the old implementation) instead of
  // failing loudly. Also throws if `trim_polygon` is non-null but has
  // fewer than 3 points - a similar real gap: `PointInPolygon()` treats
  // a too-short polygon as containing nothing, so this used to silently
  // tessellate to a fully empty mesh (confirmed by a debug run) instead
  // of failing loudly. Same check applies to every polygon in
  // `hole_polygons`, for the same reason - a too-short hole polygon used
  // to be silently ignored entirely (every point reported "outside" it,
  // so it excluded nothing) rather than failing loudly.
  Mesh TessellateGrid(int u_divisions, int v_divisions,
                       const std::vector<Point2d>* trim_polygon = nullptr,
                       const std::vector<std::vector<Point2d>>* hole_polygons = nullptr) const;

  // TessellateGrid(), generalized to an explicit, not-necessarily
  // -uniform set of `u_values`/`v_values` parameter values instead of an
  // even division count - the genuine per-region-adaptive tessellation
  // primitive this kernel's own flagged "adaptive/curvature-aware
  // meshing" gap was missing: a caller (or SuggestedParameterValues()
  // below) can pass denser breakpoints only where the surface actually
  // needs them in each direction, while the result is still a complete
  // tensor-product grid - no T-junctions or cracks, since every row
  // shares the same `u_values` and every column the same `v_values`.
  // `TessellateGrid(u_divisions, v_divisions, ...)` is exactly the
  // special case where both arrays happen to be evenly spaced - verified
  // to produce byte-for-byte the same mesh as calling this directly with
  // the equivalent evenly-spaced arrays. Throws std::invalid_argument if
  // either array has fewer than 2 entries or isn't strictly increasing,
  // or if a non-null `trim_polygon` has fewer than 3 points (see
  // TessellateGrid()'s own comment on that check - both delegate to the
  // same shared helper and so share this validation).
  Mesh TessellateGridNonUniform(const std::vector<double>& u_values,
                                 const std::vector<double>& v_values,
                                 const std::vector<Point2d>* trim_polygon = nullptr,
                                 const std::vector<std::vector<Point2d>>* hole_polygons = nullptr) const;

  // TessellateGrid(), but picking u_divisions/v_divisions via
  // SuggestedDivisions(chord_tolerance) instead of the caller choosing
  // them by hand - the one-call path this kernel's own flagged
  // "adaptive/curvature-aware meshing" gap has been missing until now.
  // Still not a true adaptive mesher (SuggestedDivisions() itself is one
  // division count per direction for the whole surface, not a
  // per-region-varying one), but it's the difference between a caller
  // having to know how to call SuggestedDivisions() at all and just
  // asking for a tolerance directly.
  Mesh TessellateGridAdaptive(double chord_tolerance,
                               const std::vector<Point2d>* trim_polygon = nullptr,
                               const std::vector<std::vector<Point2d>>* hole_polygons = nullptr) const;

  // TessellateGridNonUniform(), but picking each direction's own
  // parameter values via SuggestedParameterValues(direction,
  // chord_tolerance) instead of the caller choosing them by hand - this
  // kernel's actual genuine per-region-adaptive one-call tessellation
  // path (unlike TessellateGridAdaptive()'s single division count per
  // direction, this one has denser breakpoints wherever the surface
  // itself bends more in each direction, while remaining a crack-free
  // tensor grid).
  Mesh TessellateGridNonUniformAdaptive(
      double chord_tolerance, const std::vector<Point2d>* trim_polygon = nullptr,
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
  // Also throws std::invalid_argument if `trim_polygon` has fewer than 3
  // points - checked before the simplicity check above, since
  // `IsSimplePolygon()` itself passes a too-short polygon vacuously
  // (nothing to find a crossing between). This isn't just a stricter
  // rule for its own sake: a debug run showed an *empty* `trim_polygon`
  // actually segfaulted, not merely tessellated wrong - the concave
  // clipping path's own "no boundary crossings at all" fallback
  // dereferences `clip[0]` unconditionally, an out-of-bounds access on
  // an empty vector.
  //
  // The returned mesh is already welded (via Mesh::MergeAndWeld) since
  // adjacent cells independently compute the same boundary-intersection
  // points as separate vertices that need collapsing to form a single
  // consistent mesh.
  //
  // Throws std::invalid_argument if either division count is less than
  // 1 - the same real gap `TessellateGrid()` used to have (a `0`
  // division count would otherwise reach `ParameterAt()` and the
  // concave-path grid-width computation as an unguarded `0/0`), fixed
  // here directly rather than left latent.
  Mesh TessellateGridClippedExact(int u_divisions, int v_divisions,
                                   const std::vector<Point2d>& trim_polygon) const;

  // TessellateGridClippedExact(), but picking u_divisions/v_divisions via
  // SuggestedDivisions(chord_tolerance) instead of the caller choosing
  // them by hand - the exact-clip counterpart to
  // TessellateGridAdaptive(), same "one call instead of two" convenience.
  Mesh TessellateGridClippedExactAdaptive(double chord_tolerance,
                                           const std::vector<Point2d>& trim_polygon) const;

  const ON_NurbsSurface& raw() const { return surface_; }
  ON_NurbsSurface& raw() { return surface_; }

 private:
  ON_NurbsSurface surface_;
};

}  // namespace dino8::kernel
