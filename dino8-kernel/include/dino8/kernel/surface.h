#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::kernel {

class Mesh;
class NurbsCurve;

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

// Continuity order NurbsSurface::MatchEdge() enforces along the shared
// edge: Position (G0), Tangent (G1), Curvature (G2).
enum class MatchContinuity { Position, Tangent, Curvature };

// What NurbsSurface::MatchEdge() reports about the match it just made,
// every number measured by evaluating both surfaces after the edit (not
// inferred from the construction).
struct MatchEdgeReport {
  // Max 3D distance between the two surfaces along the shared edge.
  double max_position_error = 0.0;
  // Max |S_cross + scale * T_cross| along the edge (0 unless Tangent or
  // Curvature was requested): how far this surface's cross-boundary
  // first derivative is from the (scaled, reversed) target's. Both
  // cross derivatives are taken along the direction pointing *into*
  // their own surface, i.e. the raw parametric derivative at a
  // domain-min edge and its negation at a domain-max edge.
  double max_tangent_error = 0.0;
  // Max |S_crosscross - scale^2 * T_crosscross| along the edge (0
  // unless Curvature was requested).
  double max_curvature_error = 0.0;
  // The cross-derivative scale factor used (see MatchEdge()).
  double scale = 1.0;
  // Whether the target edge was traversed in reverse to line up with
  // this surface's edge.
  bool target_edge_reversed = false;
};

// Which analytic developable primitive NurbsSurface::UnrollDevelopable()
// matched, written to its optional out_kind - useful for a caller (e.g.
// an app command) that wants to report which case applied, or confirm
// it wasn't a coincidental match.
enum class DevelopableKind { Plane, Cylinder, Cone };

// Wraps ON_NurbsSurface. Same rationale as NurbsCurve: expose raw()
// rather than mirror the whole OpenNURBS surface API.
class NurbsSurface {
 public:
  // Builds a bilinear-ish degree-(u_degree, v_degree) NURBS surface from a
  // u_count x v_count grid of control points. Doc/implementation
  // mismatch found and fixed while building `CoonsPatch()` below (which
  // got bitten by trusting the old wording): despite this comment
  // previously claiming "row-major (u varies fastest)", the actual
  // indexing (confirmed directly against the .cpp, not assumed) is
  // `idx = u * v_count + v` - v is the one that varies fastest for
  // consecutive `control_grid` entries, u the slow/outer index, i.e.
  // `control_grid[u * v_count + v]` becomes `CV(u, v)`. Only the words
  // were wrong; the indexing itself is unchanged (many existing callers
  // already rely on the real behavior), so this is a comment-only fix.
  // Throws std::invalid_argument if either degree is < 1, either count is
  // below its degree + 1, or `control_grid.size() != u_count * v_count` -
  // the same contract `NurbsCurve::FromControlPoints()` enforces, for the
  // same reason (an `ON_NurbsSurface::Create()` refusal used to be
  // silently ignored, handing back an empty surface whose `PointAt()`
  // segfaulted on its never-allocated knot array; a too-short grid was
  // read past its end).
  static NurbsSurface FromControlGrid(const std::vector<Point3d>& control_grid,
                                       int u_count, int v_count, int u_degree,
                                       int v_degree);

  // Builds the exact bilinearly-blended Coons patch through 4 boundary
  // curves (Parasolid/Rhino's NetworkSrf/EdgeSrf for exactly 4 curves) -
  // as real NURBS algebra on the curves' own control points, not by
  // sampling them into points and re-fitting a surface through the
  // samples the way dino8-app's existing `NetworkSrf` command does
  // (that command's own `SurfaceFromRows()` hands sampled points
  // straight to `FromControlGrid()`, which treats them *as* control
  // points - a B-spline generally does not pass through its own
  // control points, so that surface's boundary only approximates the
  // source curves, confirmed by measuring the gap in the tests here).
  // This method's own boundary isocurves instead reproduce `bottom`/
  // `top`/`left`/`right` exactly (verified in the tests: the residual
  // is at the level of the reparameterization/refinement steps'
  // floating-point rounding, not a fitting error).
  //
  // `bottom`/`top` run in the same direction (both start at the "left"
  // side and end at the "right" side); `left`/`right` likewise both run
  // from "bottom" to "top" - the standard Coons convention, though only
  // `bottom` actually needs to be handed in that orientation: `top`,
  // `left` and `right` are each tried both as given and reversed (8
  // combinations total) and whichever combination best closes all 4
  // corners is used, since a caller chaining arbitrarily-picked curves
  // (dino8-app's own NetworkSrf, for instance) has no way to guarantee
  // any of the other 3 curves' own stored directions already match.
  // `bottom` alone sets the reference orientation (it defines the two
  // "bottom" corners unambiguously; there is nothing to compare it
  // against). Classical
  // construction: each curve is reparameterized onto [0, 1]
  // (`SetDomain`, shape-preserving), `bottom`/`top` are brought to a
  // shared degree and knot vector (the higher of the two degrees,
  // degree-elevated; then each one's interior knots inserted into the
  // other - both shape-preserving, same technique `MatchEdge()` uses
  // for its own shared edge), `left`/`right` the same way, and the
  // patch is built as the classical sum of a ruled surface between
  // `bottom`/`top`, a ruled surface between `left`/`right`, and a
  // bilinear correction surface through the 4 corners
  // (`S = R_uv + R_vu - B`), all three brought to one shared (degree,
  // knot vector) pair in both directions (via `ElevateDegree`/
  // `InsertKnotAt`) so the sum is exact control-point (homogeneous, if
  // any input is rational) arithmetic, not an approximation. Every step
  // is a real, previously-tested primitive; nothing here is a new
  // approximation algorithm.
  //
  // Returns Result::Failed (with `out_corner_gap`, if non-null,
  // reporting the best achievable max corner gap across all 4
  // orientation combinations) if no orientation brings all 4 corners
  // within `tolerance` of each other, or if the resulting blend would
  // need a non-positive weight anywhere (checked, same guard
  // `MatchEdge()` uses) - never ships a patch that doesn't actually
  // meet its own boundary curves. Self-checks its own result the same
  // way: evaluates the built surface's own 4 boundary isocurves against
  // the (reparameterized, orientation-corrected) input curves and rolls
  // back to Result::Failed if the residual exceeds a tight tolerance.
  static Result CoonsPatch(const NurbsCurve& bottom, const NurbsCurve& top, const NurbsCurve& left,
                            const NurbsCurve& right, NurbsSurface& out, double tolerance = 1e-6,
                            double* out_corner_gap = nullptr);

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

  // Same reasoning as `NurbsCurve::KnotCount()`/`KnotAt()`/
  // `SetKnotAt()` - see there for the full explanation of each one's
  // safety profile (`Knot()` unchecked, `SetKnot()` already
  // bounds-checked). `direction` is 0 for U, 1 for V, the same
  // convention `Domain(direction)` uses.
  int KnotCount(int direction) const;
  double KnotAt(int direction, int i) const;
  Result SetKnotAt(int direction, int i, double value);

  // Same reasoning as `NurbsCurve::InsertKnotAt()` - see there for the
  // full explanation, including the real floating-point caveat that
  // exact bit-for-bit equality does NOT hold before/after (only equal
  // to a tight numerical tolerance, confirmed by testing). `direction`
  // is 0 for U, 1 for V, the same convention `Domain(direction)` uses.
  // Validates `knot_value` is strictly interior to `Domain(direction)`
  // and `multiplicity` is between 1 and the degree in that direction,
  // throwing std::invalid_argument otherwise.
  Result InsertKnotAt(int direction, double knot_value, int multiplicity = 1);

  // Same reasoning and guarantee as `NurbsCurve::MakeRational()` - see
  // there. Delegates to `ON_NurbsSurface::MakeRational()`.
  Result MakeRational();

  // Same reasoning as `NurbsCurve::MakeNonRational()`, and the same real
  // surprising finding cross-checked independently here on a genuine
  // sphere rather than assumed to generalize from the circle case: this
  // does NOT preserve the surface's own shape unless every weight was
  // already equal (forcing uniform weighting onto now-Euclidean-correct
  // control points blends them with ordinary polynomial basis functions
  // instead of the surface's own rational ones). Delegates to
  // `ON_NurbsSurface::MakeNonRational()`.
  Result MakeNonRational();

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
  // of the surface, measured as a representative isocurve's own true arc
  // length in each direction (an isocurve at the OTHER direction's own
  // domain midpoint, sliced via `ON_Surface::IsoCurve` - the same pattern
  // `SuggestedDivisions()`/`SuggestedParameterValues()` already use - then
  // measured with `NurbsCurve::Length()`'s own convergent polyline
  // sampling). For a straight/flat surface this is exact (no curvature to
  // approximate); for a genuinely curved one it converges to the true
  // size as `NurbsCurve::Length()`'s own sample count does. This FIXES a
  // previously-documented, previously-real gap: this method used to
  // delegate straight to `ON_NurbsSurface::GetSurfaceSize` (by its own
  // source comment `// TODO - get lengths of polygon`), which returns
  // each direction's *control polygon length* instead - confirmed to
  // overstate a unit-radius cylinder wall's true circumference
  // substantially (8.0 vs the true 2*pi ~ 6.28). Falls back to that same
  // control-polygon estimate, per direction, only if the isocurve can't
  // be cast to `ON_NurbsCurve` (shouldn't happen for a genuine NURBS
  // surface).
  SurfaceSize GetApproximateSize() const;

  // Approximate surface area: tessellates via `TessellateGrid()` at
  // `u_divisions x v_divisions` and sums the resulting mesh's own
  // triangle areas (`Mesh::Area()`) - not a from-scratch numeric
  // integration of the first fundamental form, since the tessellator
  // already exists and a flat-triangle approximation of a smooth surface
  // converges to the true area from below as the grid refines (the same
  // "understates via straight-line/flat-facet approximation" direction
  // every polyline/polygon approximation in this file has) - genuinely
  // exact only where the surface has no curvature for a flat facet to
  // fall short of (a flat plane). Throws std::invalid_argument if either
  // division count is less than 1 (`TessellateGrid()`'s own validation -
  // see there).
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
  // `ClosestPointParameter()` documents for the curve case. In a direction
  // where `IsClosed()` is true (e.g. a sphere/cylinder/cone/revolved
  // surface's own periodic parameter), the shrinking bracket wraps across
  // the seam instead of clamping there - fixes a real, confirmed bug where
  // a coarse sample landing exactly on the seam could never explore the
  // physically-adjacent region just past the domain's other end, silently
  // snapping to the seam point instead of the true closest point (up to a
  // seam-wide margin off - verified on a radius-3 sphere, an ~8.7-degree-
  // off-seam query used to return a point ~0.26 units from the true
  // answer, about 8.7% of the radius).
  //
  // `u_divisions`/`v_divisions` above this method's own default (20) only
  // add per-level SAMPLING precision, not extra ability to correct a bad
  // early guess: the window-narrowing step between levels is internally
  // capped as if at most 24 divisions were requested (see surface.cpp),
  // fixing a real, confirmed bug where a caller-requested finer grid
  // (fewer levels needed to reach floating-point precision, but each
  // level's window shrinks by ~2/divisions) could leave the search unable
  // to travel far enough from an early level's best sample to reach a true
  // nearby minimum - i.e. a FINER grid converging to a WORSE answer than a
  // coarser one on the exact same surface and query, the opposite of the
  // expected trend. That's on top of, not instead of, this method's own
  // pre-existing "not a guaranteed global minimum" caveat above: a
  // pathological multi-modal distance landscape can still make two
  // different grid resolutions land in two different, genuinely separate
  // local minima from the very first level, each one converged to
  // correctly - that residual is inherent to any finite-sampling search
  // and isn't fixable by adjusting the narrowing step.
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

  // ---- Surface editing (implemented in src/surface_edit.cpp) ----

  // Removes one multiplicity of the interior knot at ON-convention index
  // `knot_index` (0 <= knot_index < KnotCount(direction); any index in a
  // multiple knot's run selects that whole knot value) in `direction`
  // (0 = U, 1 = V) - the exact inverse of `InsertKnotAt()`, i.e. Tiller's
  // knot-removal algorithm (Piegl & Tiller, "The NURBS Book", A5.8),
  // applied to every row/column of the control net at once. Knot removal
  // is only shape-preserving when the surface genuinely has the extra
  // continuity at that knot (e.g. a knot `InsertKnotAt()` itself added);
  // otherwise the best-fitting reduced net is an approximation. This
  // method never silently ships that approximation: it computes a
  // rigorous upper bound on the resulting max 3D deviation from the
  // original surface over the whole domain (the algorithm's own control-
  // net discrepancy, which bounds the surface error because B-spline
  // basis functions are non-negative and sum to 1; on a rational surface
  // the discrepancy is measured on the homogeneous control points and
  // converted to a Euclidean bound via Piegl & Tiller eq. 5.30, a looser
  // but still rigorous bound) and only commits the removal if that bound
  // is <= `tolerance`. Otherwise returns Result::Failed and leaves the
  // surface untouched. `out_max_deviation`, if non-null, always receives
  // the bound (also on failure, so a caller can report how far off the
  // removal would have been). Requires the knot vector to be clamped in
  // `direction` (an unclamped/periodic knot vector's wrapped control
  // points would need matching edits this doesn't do) - returns
  // Result::Failed otherwise. Throws std::invalid_argument if `direction`
  // isn't 0/1, `knot_index` is out of range, or the knot isn't strictly
  // inside the domain (the domain's own end knots can't be removed).
  Result RemoveKnotAt(int direction, int knot_index, double tolerance,
                      double* out_max_deviation = nullptr);

  // Max 3D distance between `PointAt(u, v)` on this surface and on
  // `other` over a `u_samples` x `v_samples` grid of (u, v) values spread
  // across *this surface's* domain (both surfaces are evaluated at the
  // same numeric (u, v), so this only means anything when the two share a
  // parameterization - e.g. one is a knot-refined/removed, degree-
  // elevated or refit copy of the other). A sampled measurement, so a
  // lower bound on the true max deviation, not an upper bound; the
  // sample count sets how fine. Throws std::invalid_argument if either
  // sample count is < 2.
  double MaxSampledDeviationFrom(const NurbsSurface& other, int u_samples = 64,
                                 int v_samples = 64) const;

  // Reparameterizes `direction` (0 = U, 1 = V) in place so its domain
  // becomes exactly [t0, t1], leaving the 3D shape bit-for-bit untouched:
  // an affine rescale of that direction's knot vector, nothing else
  // (delegates to `ON_NurbsSurface::SetDomain`, verified as a real
  // implementation that maps every knot linearly from the old domain
  // onto the new one). Afterward `PointAt()` at the same *normalized*
  // parameter returns the same point as before. Returns
  // Result::NoOpAlreadySatisfied if the domain already is [t0, t1],
  // Result::Failed if `t0 >= t1` or OpenNURBS' own call fails. Throws
  // std::invalid_argument if `direction` isn't 0/1.
  Result SetDomain(int direction, double t0, double t1);

  // Rebuilds (refits) this surface as a new non-rational NURBS surface
  // with exactly `u_count` x `v_count` control points of degree
  // `u_degree` x `v_degree` and clamped uniform knots over the *same*
  // domain as this surface - Rhino's Rebuild / Parasolid's refit. The
  // fit is the global tensor-product least-squares solution (Piegl &
  // Tiller A9.7: the source is sampled on a `u_samples` x `v_samples`
  // parameter grid, every sample row is least-squares fit in U with the
  // two end control points pinned to the row's end samples, then every
  // column of those intermediate control points is fit in V the same
  // way; with gridded parameters and one shared knot vector per
  // direction this row-then-column solve *is* the full tensor-product
  // least-squares solution, not a heuristic). Consequences: the four
  // corners are interpolated exactly, and the result reproduces this
  // surface's own parameterization (evaluate both at the same (u, v) to
  // compare), so `out_max_deviation`, if non-null, receives the max 3D
  // deviation measured on a grid twice as fine as the fit samples,
  // offset by half a step so it never lands on the fit samples
  // themselves - a sampled lower bound on the true deviation, stated as
  // such rather than claimed exact. A refit is an approximation
  // whenever the source isn't already representable with the requested
  // net (e.g. a rational sphere or a higher-degree/denser source) and
  // exact (deviation ~1e-12) whenever it is - both verified in the
  // tests. Returns Result::Failed if the normal equations are singular.
  // Throws std::invalid_argument if a degree is < 1, a count is <=
  // its degree, or a sample count is < the corresponding control count
  // (an underdetermined fit).
  Result Rebuild(int u_count, int v_count, int u_degree, int v_degree, NurbsSurface& out,
                 double* out_max_deviation = nullptr, int u_samples = 64, int v_samples = 64) const;

  // MatchSrf: edits this surface in place so its boundary edge where
  // parameter `fixed_direction` (0 = U, 1 = V) sits at its domain min
  // (`at_min` true) or max coincides with `target`'s boundary edge
  // selected the same way (`target_fixed_direction`/`target_at_min`),
  // with the requested continuity across the join - the Parasolid/Rhino
  // "match surface" operation, done as exact NURBS algebra rather than
  // by moving control points onto sampled target points:
  //
  //  1. The two edges' bases are made identical along the edge (the
  //     target edge is oriented to run the same way as this one -
  //     detected from the corner points, reported in
  //     `target_edge_reversed` - reparameterized onto this edge's domain
  //     via SetDomain, then both are degree-elevated to the higher of the
  //     two edge degrees and knot-refined to the union knot vector; all
  //     three are shape-preserving, so the target is never altered in
  //     3D and this surface only gains control points). If either side
  //     is rational the other is made rational too.
  //  2. This surface's control-point row on the edge is replaced by the
  //     target's edge row (homogeneous coordinates, so weights carry
  //     over) - the two boundary curves become the *same* NURBS curve,
  //     G0 exactly, not to a tolerance.
  //  3. Tangent: the next row is set so this surface's cross-boundary
  //     first derivative equals `-scale` times the target's, both taken
  //     along the direction pointing into their own surface (the minus
  //     sign because the two surfaces continue each other across the
  //     edge, the target's inward direction being this surface's
  //     outward one; in raw parametric derivatives that is `S_v(min) =
  //     -scale * T_t(min)` for a min/min pairing and `S_v(min) = +scale
  //     * T_t(max)` when the target edge sits at its domain max), using
  //     the clamped-B-spline end-derivative formula
  //     `p / (V_{p+1} - V_1) * (R_1 - R_0)`. Curvature: the third row is
  //     set so the second cross derivative equals `scale^2` times the
  //     target's. With a constant `scale` these are exactly C1/C2 in the
  //     reparameterization v' = scale * v, hence G1/G2, and the target's
  //     own Gaussian curvature is reproduced along the edge (checked in
  //     the tests). `scale` is the ratio of this surface's mean
  //     cross-derivative magnitude along the edge (before the edit) to
  //     the target's, so the match keeps this surface's own
  //     parameterization speed instead of adopting the target's; pass
  //     `cross_scale > 0` to force a value (1.0 = plain C1/C2 with the
  //     target's own speed).
  //
  // Rows beyond the ones rewritten (1 for Position, 2 for Tangent, 3
  // for Curvature) are untouched. To guarantee the far edge never moves,
  // the cross direction is first degree-elevated to at least 2 (Tangent)
  // / 3 (Curvature) and, if it still has no spare row, knot-refined at
  // its mid-parameter to add one - both shape-preserving. `report`, if
  // non-null, receives measured residuals (both surfaces evaluated along
  // the edge after the edit); the method itself checks them against
  // 1e-9 times the surfaces' size and returns Result::Failed, restoring
  // this surface, if they don't hold - a self-check, so a wrong result
  // can't be shipped silently. Also Result::Failed (surface untouched)
  // if either surface isn't clamped in the directions involved (a
  // periodic edge/cross direction isn't supported), if the requested
  // continuity exceeds the target's cross degree (a degree-1 target has
  // no curvature to match), or if a resulting rational weight would be
  // <= 0. Throws std::invalid_argument on a direction outside 0/1.
  Result MatchEdge(int fixed_direction, bool at_min, const NurbsSurface& target, int target_fixed_direction,
                   bool target_at_min, MatchContinuity continuity, MatchEdgeReport* report = nullptr,
                   double cross_scale = 0.0);

  // Unrolls a *developable* surface (a plane, a cylinder, or a cone -
  // the only three shapes an ON_NurbsSurface can be per OpenNURBS'
  // IsPlanar/IsCylinder/IsCone, and the classical differential-geometry
  // fact that a surface unrolls to the plane without distortion iff its
  // Gaussian curvature is identically zero, which holds for exactly
  // these among the primitives this kernel builds - a cylinder/cone's
  // zero Gaussian curvature is itself confirmed elsewhere in this file's
  // own CurvatureAt() tests) into `out_flat`, a flat triangle mesh in
  // the world XY plane. Tries IsPlanar()/IsCylinder()/IsCone() in that
  // order (each already a real OpenNURBS geometric fit, not a name
  // check) and returns Result::Failed, `out_flat` left untouched, if
  // none matches - a curved (non-developable) surface like a sphere or
  // torus, or a generic freeform NURBS surface, is refused rather than
  // silently flattened with distortion (that's `TessellateGrid()` +
  // hand-rolled triangulation, what dino8-app's own Squish/Unroll
  // commands already do for the general case - this method is the exact
  // complement for the three shapes that admit a true isometry).
  //
  // Every one of the `(u_divisions + 1) x (v_divisions + 1)` flat
  // vertices is computed by mapping the *exact* 3D point directly
  // through the matched primitive's own closed-form inverse (no
  // tessellation error folded in): a cylinder's `ON_Cylinder::
  // ClosestPointTo()` gives the point's exact (angle, height), mapped
  // to flat `(radius * angle, height)`; a cone's gives (angle,
  // axial height), converted to the exact slant distance from the apex
  // `L = height / cos(halfAngle)` and mapped to flat
  // `(L * cos(angle * sin(halfAngle)), L * sin(angle * sin(halfAngle)))`
  // - the standard cone-unroll construction (a full lap around the cone,
  // true circumference `2*pi*L*sin(halfAngle)` at slant distance L,
  // becomes a `2*pi*sin(halfAngle)`-radian sector of a flat circle of
  // radius L, which has that same arc length); a plane's
  // `ON_Plane::ClosestPointTo()` gives the point's own in-plane (x, y)
  // directly - a rigid-body isometry with no scaling at all. Because
  // each vertex is placed by this direct formula rather than by
  // integrating or accumulating edge lengths across the mesh, two
  // invariants hold up to `ON_Mesh`'s own single-precision vertex
  // storage (~1e-6 relative, the same limit this kernel's other mesh-
  // based tests already account for), independent of
  // `u_divisions`/`v_divisions` - verified this way in the tests, not
  // just by eyeballing a rendered flat shape: every flat vertex at the
  // same `v` sits at exactly the same flat y (cylinder) or exactly the
  // same flat distance from the unrolled apex (cone) as its true 3D
  // counterpart's height/slant-distance, and the *total* flat width of
  // a full circumferential sweep equals exactly `radius * totalAngle`
  // (cylinder) or the exact sector formula (cone) - not merely
  // approaching it as the mesh gets finer. What does converge only in
  // the mesh-refinement sense (same honesty this kernel already applies
  // to `ApproximateArea()`) is any single flat *triangle's* area against
  // its true 3D counterpart's, since a straight mesh edge is a chord of
  // the true curve, not the curve itself - `out_area`, if non-null,
  // receives the flat mesh's own measured area (`Mesh::Area()`), which
  // is therefore an approximation of the true closed-form patch area
  // that improves with `u_divisions`/`v_divisions`, not an exact value.
  //
  // Throws std::invalid_argument if either division count is < 1.
  Result UnrollDevelopable(int u_divisions, int v_divisions, Mesh& out_flat, double* out_area = nullptr,
                           DevelopableKind* out_kind = nullptr) const;

  // Computes the EXACT offset of this surface by `distance` along its own
  // NormalAt(u, v) direction, for the five analytic types whose true
  // offset (the literal "surface of points at distance `distance` along
  // the normal", not an approximation of it) is itself expressible in
  // the same closed form: a plane, sphere, cylinder, cone, or torus
  // (detected the same real, tolerance-based way IsPlanar()/IsSphere()/
  // IsCylinder()/IsCone()/IsTorus() and UnrollDevelopable() already do,
  // not a name check). This is the general-surface counterpart to
  // Parasolid's PK_BODY_offset computing an exact analytic offset face
  // rather than approximating it with a refit spline - the honest
  // "approximate elsewhere" freeform case this deliberately does NOT
  // attempt is refused outright (see the failure list below), not
  // silently approximated.
  //
  // What each case actually returns (every one verified algebraically,
  // not assumed from "offsetting shrinks/grows a round thing"):
  //  - Plane: this surface's OWN control points translated by
  //    `distance * NormalAt(u, v)` (the same vector everywhere a plane's
  //    normal is constant) - exact for ANY planar surface regardless of
  //    its actual shape/domain/control structure, unlike the four cases
  //    below, since a constant per-control-point translation moves every
  //    evaluated point by that same vector (NURBS partition of unity),
  //    which for a constant normal field is precisely what "offset by
  //    distance along the normal" means. Preserves this surface's exact
  //    domain and control/weight structure - a genuine caller-facing
  //    difference from the other four cases, which instead rebuild the
  //    matched primitive's own natural full extent from scratch via
  //    GetNurbForm() (a plane has no such single natural bounded form to
  //    rebuild from, since a plane itself is unbounded).
  //  - Sphere / Cylinder: a concentric sphere / coaxial cylinder with
  //    radius `radius +/- distance` (the sign resolved per surface, see
  //    below) - trivially exact, every point moves radially by exactly
  //    `distance`.
  //  - Cone: a coaxial cone with the exact SAME half-angle, apex shifted
  //    along the cone's own axis by `distance / sin(half_angle)` - the
  //    standard, non-obvious fact (verified here by direct algebraic
  //    derivation, not assumed) that a cone's offset is itself a cone
  //    with unchanged opening angle: parametrizing the cone as
  //    P(h, theta) = (h*tan(alpha)*cos(theta), h*tan(alpha)*sin(theta), h)
  //    (h = distance from apex along the axis, alpha = half-angle) and
  //    solving for the one apex position z_a such that
  //    P(h, theta) + distance * outward_normal(h, theta) lies exactly on
  //    a same-alpha cone with apex at z_a gives the closed form
  //    z_a = -distance / sin(alpha), independent of h and theta - i.e.
  //    this genuinely holds at every point of the surface, not just
  //    near where it was checked.
  //  - Torus: a coaxial torus with the SAME major (center-circle) radius
  //    and tube radius `minor_radius +/- distance` - by the same kind of
  //    exact derivation as the cone case (a torus is a tube of circles
  //    around its own center circle; offsetting that tube by a constant
  //    distance is exactly a tube of radius `minor_radius +/- distance`
  //    around the SAME center circle).
  //
  // The sign for each +/- above is resolved AT RUNTIME by comparing this
  // surface's own NormalAt() at a sample point against the fitted
  // primitive's independently-known true outward direction there (e.g.
  // `point - sphere.Center()` for a sphere) - deliberately not assumed
  // to be a fixed convention, since nothing guarantees every possible
  // NurbsSurface's own du x dv handedness agrees with "outward" (this
  // file's own IsSphere() doc comment already records EvNormal's sign
  // behaving surprisingly for at least one shape). This makes the result
  // correct for whichever of the two possible normal fields this
  // particular surface happens to have, instead of silently producing a
  // shrunk surface when a caller who read "distance > 0 means grow"
  // expected it to grow.
  //
  // A real, deliberately enforced correctness guard against a genuine
  // self-intersection hazard (the same one Parasolid's own offset is
  // documented to check for), not an omission: this returns
  // Result::Failed, `out` left unchanged, rather than silently building
  // an invalid or self-overlapping surface, when the requested offset:
  //  - would make a sphere/cylinder's radius, or a torus's tube radius,
  //    <= 0 (the offset distance exceeds that constant-curvature
  //    surface's own radius of curvature - it folds through its own
  //    axis/center);
  //  - would make a torus's tube radius >= its own major radius (a
  //    self-intersecting spindle torus, not merely a fatter one);
  //  - would, for a cone, shrink the radius below zero anywhere within
  //    this surface's own existing v-domain (checked at both v-domain
  //    ends, where a cone's monotonically-varying radius is smallest) -
  //    the direct cone analogue of "offset exceeds local radius of
  //    curvature", since a cone's local radius of curvature in the
  //    circumferential direction is exactly its distance from the axis
  //    at that point.
  //
  // Returns Result::Failed, `out` unchanged, if this surface isn't
  // (within `tolerance`) one of the five types above - a general
  // freeform surface's true offset is generally NOT itself expressible
  // as an exact NURBS surface at all (it's a genuinely different,
  // typically non-rational algebraic surface), so this deliberately
  // refuses rather than quietly returning an approximation dressed up as
  // an exact one; that harder, inherently-approximate case belongs to a
  // different, explicitly-approximate method this one is not. `tolerance`
  // defaults (`<= 0`) to `tolerance::DistanceForSize()` of this surface's
  // own bounding-box diagonal, the same reasoning UnrollDevelopable()
  // already applies: IsPlanar()/IsSphere()/IsCylinder()/IsCone()/
  // IsTorus()'s own `ON_ZERO_TOLERANCE` default is far too tight for
  // anything but a hand-built exact primitive.
  Result OffsetAnalytic(double distance, NurbsSurface& out, double tolerance = -1.0) const;

  const ON_NurbsSurface& raw() const { return surface_; }
  ON_NurbsSurface& raw() { return surface_; }

 private:
  ON_NurbsSurface surface_;
};

}  // namespace dino8::kernel
