#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::kernel {

class NurbsSurface;

// Corner treatment for the exact per-vertex polyline case of
// `NurbsCurve::OffsetInPlane()` (PARITY_MAP.md's offsetshell category,
// "Curve offset corner handling at kinks": "sharp (miter) only ... No
// Round/Chamfer/Smooth corner modes" - this closes the Round and Chamfer
// halves of that gap). `Sharp` (the default, and the only behavior before
// this enum existed) is the exact angle-bisector miter point every corner
// already used. `Round` and `Chamfer` only change a corner where that
// miter point would otherwise stick out PAST the simple per-edge offset
// distance on the expanding side of the turn - the "convex, fill-needed"
// case where a genuine gap opens between the two offset edges - `Round`
// replacing the sharp spike there with a circular arc of radius
// `|distance|` centered on the ORIGINAL vertex, tangent to both offset
// edges, and `Chamfer` replacing it with the single straight segment
// connecting those same two tangent points directly (the arc's own chord,
// never computing or returning to the original vertex at all). A corner on
// the contracting side of the turn (offsetting there pulls the two edges
// together rather than apart - e.g. a reflex vertex under an otherwise-
// outward offset) is NOT a "fill" at all - the two offset lines simply
// cross there, and both `Round` and `Chamfer` leave that exact crossing
// point alone, same as `Sharp`: cutting a corner that never had a gap to
// begin with is not standard behavior in any offset tool and isn't
// invented here either.
enum class CurveOffsetCornerStyle { Sharp, Round, Chamfer };

// What NurbsCurve::MatchEnd() reports about the match it just made,
// every number measured by evaluating both curves after the edit (not
// inferred from the construction) - the curve-end counterpart to
// NurbsSurface::MatchEdge()'s own `MatchEdgeReport`.
struct MatchEndReport {
  // 3D distance between this curve's own end point and the target's,
  // after the edit (0 for an exact match - a clamped curve's endpoint
  // is exactly its end control point, so Position match is always
  // exact up to floating-point rounding).
  double position_error = 0.0;
  // |this curve's end derivative + scale * target's end derivative|
  // (0 unless Tangent or Curvature was requested) - both derivatives
  // taken pointing INTO their own curve from the shared end, so a
  // perfect match makes them exactly antiparallel (scaled), the same
  // "both tangents pointing away from the joint" convention this
  // kernel's own GCon-style continuity analysis already uses.
  double tangent_error = 0.0;
  // |this curve's end second derivative - scale^2 * target's| (0
  // unless Curvature was requested).
  double curvature_error = 0.0;
  // The end-derivative scale factor used (see MatchEnd()).
  double scale = 1.0;
};

// What NurbsCurve::AnalyzeEndContinuity() reports about the continuity
// between the NEAREST pair of ends of two curves - the kernel-level,
// always-exact (never sampled) counterpart to the app's own `GCon`
// command, which until now duplicated this exact computation inline
// with no kernel API behind it at all.
struct EndContinuityReport {
  // 3D distance between the two curves' own nearest end points.
  double gap = 0.0;
  // Angle, in degrees, between the two curves' own tangent directions
  // at those nearest ends, each first flipped (if needed) to point
  // AWAY from the shared joint - 0 degrees means the two curves
  // continue perfectly straight across the joint (G1), the same "both
  // tangents pointing away from the joint, then compare" convention
  // `MatchEnd()`'s own doc comment above already uses for the
  // identical idea.
  double tangent_angle_degrees = 0.0;
  // The two curves' own curvature magnitudes (kappa = 1/radius, 0 for
  // a straight segment) at those same nearest ends - reported
  // separately, not just their difference, since a caller may want
  // the actual values, not merely how close they are.
  double curvature_a = 0.0;
  double curvature_b = 0.0;
  // |curvature_a - curvature_b| / max(1e-9, max(curvature_a,
  // curvature_b)) - exactly 0 when both curvatures are already
  // negligible (e.g. two straight lines), not a divide-by-near-zero
  // artifact.
  double curvature_relative_difference = 0.0;
};

// Wraps ON_NurbsCurve. Deliberately exposes the underlying ON_NurbsCurve
// (via raw()) rather than re-declaring every accessor OpenNURBS already
// has — later chunks (booleans, display) need the real object, not a
// facade that only covers what chunk 1 happened to need.
class NurbsCurve {
 public:
  // Builds a degree-`degree` NURBS curve interpolating a polyline through
  // `control_points` with uniform-ish knots. Not a general-purpose curve
  // fit — just enough to construct a testable curve without pulling in a
  // fitting algorithm this chunk doesn't own. Throws std::invalid_argument
  // if `degree < 1` or `control_points.size() < degree + 1` (a NURBS
  // curve needs at least `order = degree + 1` control points): before
  // that check, `ON_NurbsCurve::Create()`'s own refusal of such input was
  // silently ignored and an empty, never-allocated curve came back that
  // still evaluated - to (0, 0, 0) everywhere, with Length() 0 - rather
  // than failing (confirmed by a debug run).
  static NurbsCurve FromControlPoints(const std::vector<Point3d>& control_points,
                                       int degree);

  // Real global least-squares curve approximation (Piegl & Tiller, "The
  // NURBS Book", the standard technique behind any legitimate NURBS
  // refit-to-fewer-control-points algorithm - not a stub and not a
  // disguised interpolation): fits a degree-`degree` curve with exactly
  // `control_point_count` control points through `points` in a
  // least-squares sense, using chord-length parameterization and the
  // standard knot-averaging formula for approximation. The two end
  // points are interpolated exactly (P_0 = points.front(), P_last =
  // points.back()); every other point is fit by solving the normal
  // equations for the interior control points, so the returned curve is
  // the least-squares BEST FIT through `points` at that control point
  // count, not merely "some curve near them".
  //
  // Returns Result::Failed if `points.size() < 2`, `degree < 1`,
  // `control_point_count < degree + 1`, or `control_point_count >
  // points.size()` (least-squares needs at least as many data points as
  // unknowns - asking for more control points than data points is a
  // request this is not defined for, not silently upgraded to
  // interpolation). `out` is left unchanged on failure.
  static Result FitLeastSquares(const std::vector<Point3d>& points, int degree,
                                 int control_point_count, NurbsCurve& out);

  // 2D/3D POLYLINE-CORNER FILLET (Rhino Fillet, AutoCAD FILLET between two
  // lines; app's own FilletChamferCommand, cmd_curveedit.cpp:510, does
  // this with mesh-sampled corner geometry - this is the genuine kernel
  // API the app-only version doesn't have, per PARITY_MAP.md's own
  // "No kernel 2D fillet or chamfer API" note). Replaces the corner
  // `corner` between the two straight segments `p0`-`corner` and
  // `corner`-`p1` with a circular arc of the given `radius`, tangent to
  // both segments, and returns ONE continuous curve: the trimmed segment
  // `p0`-T0, the arc T0-T1, and the trimmed segment T1-`p1`, joined via
  // `Join()` (so `out`'s own domain runs monotonically start to end, C0
  // throughout with a tangent-continuous, not just position-continuous,
  // junction at T0 and T1 - see the construction below for why).
  //
  // THE CONSTRUCTION (the standard "common tangent circle to two rays"
  // result, the exact 2D analogue of `ChamferConvexEdge`'s own edge-level
  // law-of-tangents formula in this codebase, fillet.cpp):
  //   1. u = normalize(p0 - corner), v = normalize(p1 - corner) - unit
  //      vectors along the two rays from the corner. theta = the angle
  //      between them (acos(u . v), in (0, pi) for a genuine corner - see
  //      VALIDATION below).
  //   2. The tangent length from `corner` to each tangent point is
  //      d = radius / tan(theta / 2) (the same relationship
  //      `FilletConvexEdgeByDistanceFromEdge`'s own doc comment already
  //      uses in reverse, `radius = distance * tan(theta/2)`, for a 3D
  //      edge's dihedral half-angle) - so T0 = corner + d*u, T1 =
  //      corner + d*v.
  //   3. The circle's center C lies on the internal angle bisector,
  //      bis = normalize(u + v), at distance L = radius / sin(theta/2)
  //      from `corner` (C = corner + L*bis) - the standard fact that
  //      |T0 - C| = |T1 - C| = radius follows directly from the right
  //      triangle (corner, T0, C) with legs d and radius and hypotenuse
  //      L, checked directly below rather than merely asserted.
  //   4. The arc sweeps from T0 to T1 through angle (pi - theta) - built
  //      as an `ON_Arc` on the plane through C with `xaxis = normalize(T0
  //      - C)` and whichever of the two candidate `zaxis` directions
  //      (+/- normalize(u x v)) makes that sweep positive (checked
  //      directly against T1's own reconstructed position, not assumed
  //      from a fixed cross-product order - a corner where p0/corner/p1
  //      wind the opposite way needs the opposite sign).
  // Because both the incoming segment `p0`-T0 and the arc share the same
  // tangent direction at T0 (u, by construction - a circle's own radius
  // to a tangent point is perpendicular to the tangent line, so the
  // segment direction and the arc's own starting tangent coincide
  // exactly), and likewise at T1, `Join()`'s own "position-only, tangent
  // kink allowed" contract in fact produces a genuinely tangent (G1), not
  // merely positional (G0), junction here - verified directly by
  // `TestNurbsCurveFilletCornerObtuseAngleAndTangency`, not just argued
  // from the construction.
  //
  // VALIDATION: `radius` must be > 0. `p0`, `corner` and `p1` must be
  // distinct (checked via `Unitize()` failing on a zero-length ray) and
  // not collinear (theta within `1e-9` of 0 or pi throws - a straight
  // "corner" has no tangent circle of finite radius, the same "nothing to
  // fillet" case Rhino's own Fillet command refuses). Throws
  // `std::invalid_argument` for any of those. Returns `Result::Failed`,
  // `out` left unchanged, if `radius` is too large for the two segments -
  // `d` exceeding either `|p0 - corner|` or `|corner - p1|` would place a
  // tangent point past the segment's own far endpoint, the same
  // "radius/distance too large to fit" failure mode every 3D fillet/
  // chamfer function in fillet.cpp already reports for the analogous
  // condition - checked directly, not left to `Join()`'s own unrelated
  // tolerance check to catch incidentally.
  static Result FilletCorner(Point3d p0, Point3d corner, Point3d p1, double radius, NurbsCurve& out);

  // Just the tangent ARC of `FilletCorner` above - T0 to T1, none of its
  // leg assembly. `FilletCorner` itself is built on top of this (computes
  // the arc via this function, then joins a `p0`-T0 leg before it and a
  // T1-`p1` leg after), so the two share one implementation of the actual
  // hard part (tangent length, bisector center placement, and picking the
  // sweep direction that actually reaches T1 rather than the long way
  // round) - the same construction documented on `FilletCorner` above
  // applies here unchanged.
  //
  // This exists for a caller that needs the arc ALONE as its own curve
  // object rather than fused to fresh straight legs - a two-curve pick-
  // and-fillet command whose "legs" are pieces already trimmed from two
  // independently picked input curves (not fresh rays from `FilletCorner`
  // itself), or a polyline fillet rounding several corners along one
  // curve, where the straight run between two consecutive rounded corners
  // is one shared segment built once, not two independent legs glued on
  // by each corner separately.
  //
  // `p0`/`p1` still only fix the two ray DIRECTIONS from `corner` (the
  // arc's own geometry never depends on their distance, only on
  // normalize(p0 - corner)/normalize(p1 - corner)); that distance is used
  // solely for the same "does the radius fit" check `FilletCorner` makes,
  // so a caller whose own real fit is already bounded elsewhere (e.g. a
  // polyline corner's own shared-edge availability check, which must
  // halve the margin `FilletCorner`'s single-corner check does not know
  // to) may pass a synthetic point along the ray, placed farther out than
  // the true tangent length, purely to satisfy this check without it
  // rejecting a fit this function alone can't see is actually fine.
  //
  // Returns `Result::Ok` with `arc_out` running from the tangent point on
  // the `p0` side (`arc_out.PointAt(arc_out.Domain().min)`) to the tangent
  // point on the `p1` side (`arc_out.PointAt(arc_out.Domain().max)`) -
  // both readable directly off the returned curve, not recomputed by the
  // caller from the same radius/angle formula a second time. Same
  // VALIDATION as `FilletCorner`:
  // throws `std::invalid_argument` for a non-positive `radius`, `p0`/`p1`
  // coincident with `corner`, or `p0`/corner/`p1` collinear; returns
  // `Result::Failed`, `arc_out` left unchanged, if the tangent length
  // reaches past `p0` or `p1`.
  static Result FilletCornerArc(Point3d p0, Point3d corner, Point3d p1, double radius, NurbsCurve& arc_out);

  // 2D/3D POLYLINE-CORNER CHAMFER (Rhino Chamfer, AutoCAD CHAMFER between
  // two lines) - the flat-cut sibling of `FilletCorner` above, the exact
  // 2D analogue of `ChamferConvexEdge`'s own two-independent-distance
  // form in this codebase (fillet.cpp/fillet.h): replaces the corner with
  // a single straight segment T0-T1, where T0 = corner + distance0 *
  // normalize(p0 - corner) and T1 = corner + distance1 * normalize(p1 -
  // corner) - two INDEPENDENT distances, not a single shared one (Rhino's
  // own Chamfer command takes two distances for exactly this reason: a
  // chamfer, unlike a fillet, has no single "radius" that determines both
  // setbacks from the corner's own geometry alone).
  //
  // Unlike `FilletCorner` (which needs `Join()` to splice an arc between
  // two straight segments), the whole result - `p0`, T0, T1, `p1` - is
  // ALREADY one honest 4-point polyline, so this is built directly as a
  // single degree-1 `FromControlPoints()` curve (the same "a clamped
  // degree-1 B-spline through N control points IS the polyline connecting
  // them, exactly" fact this file already relies on for every straight
  // segment it builds elsewhere) rather than three separately-joined
  // pieces - simpler, and with no junction-tangent question to raise at
  // all (a chamfer is deliberately NOT tangent to either original
  // segment; that flat kink is the whole feature, the same way
  // `ChamferConvexEdge`'s own 3D chamfer facet is deliberately not
  // tangent to its two adjacent faces).
  //
  // VALIDATION: `distance0`/`distance1` must both be > 0. `p0`, `corner`
  // and `p1` must be distinct (same check `FilletCorner` uses). Throws
  // `std::invalid_argument` for either. Returns `Result::Failed`, `out`
  // left unchanged, if `distance0 >= |p0 - corner|` or `distance1 >=
  // |corner - p1|` (the chamfer point would reach past, or land exactly
  // on, the segment's own far endpoint) - the same "too large to fit"
  // convention `FilletCorner` above uses. Unlike `FilletCorner`, `p0`/
  // `corner`/`p1` being exactly collinear is NOT an error here (chamfering
  // a straight "corner" degenerates to trimming a notch out of a straight
  // line, which is still a well-defined 4-point polyline - just an odd
  // one to ask for); it is left to the caller to decide whether that
  // makes sense for their own use case.
  static Result ChamferCorner(Point3d p0, Point3d corner, Point3d p1, double distance0, double distance1,
                               NurbsCurve& out);

  // CURVE-TO-CURVE BLEND with a caller-chosen continuity order (Rhino/
  // AutoCAD BlendCrv/Blend between two independently picked curves) -
  // PARITY_MAP.md's own "Curve-to-curve blend" gap: today this codebase's
  // only such construction is app-only (`BlendCrvCommand`, cmd_curves2.cpp:
  // G1 tangent cubic or G2 curvature-continuous quintic Hermite), with "No
  // G3+ and no kernel API" named explicitly as the remaining gap. This is
  // the genuine kernel API that was missing, generalized to any
  // `continuity` in {1, 2, 3} (G1/G2/G3), not just the two cases the app
  // command happens to hardcode.
  //
  // THE CONSTRUCTION - EXACT Hermite interpolation, not a fit: builds the
  // unique Bezier-basis NURBS curve of degree `d = 2*continuity + 1` on
  // the parameter domain [0, 1] whose position and derivatives up to
  // order `continuity` match `curve0`'s own (at `t0`) at u=0 and
  // `curve1`'s own (at `t1`) at u=1. This is possible in closed form,
  // with no least-squares solve at all, because a Bezier's own k-th
  // derivative at u=0 depends ONLY on control points P_0..P_k (the
  // standard forward-difference identity B^(k)(0) = d!/(d-k)! * Delta^k
  // P_0), and its k-th derivative at u=1 depends ONLY on P_(d-k)..P_d
  // (the mirror backward-difference identity) - so P_0..P_continuity
  // solve directly from curve0's own end data, P_(d-continuity)..P_d
  // solve directly from curve1's own, and the two sets never overlap
  // (continuity < d - continuity for any continuity >= 1, since d =
  // 2*continuity + 1), together filling all d + 1 control points exactly.
  // `continuity` = 1 gives a cubic (order 4 - the same "G1 tangent cubic"
  // this file's own doc comments elsewhere already attribute to
  // BlendCrv), 2 a quintic ("G2 curvature-continuous quintic", matching
  // Blend), and 3 (the genuine new capability beyond what either app
  // command builds) a septic.
  //
  // `reverse0`/`reverse1` each pick which DIRECTION along that curve's
  // own parametrization the blend continues in, i.e. which of the two
  // possible tangent senses at the picked end the blend should match -
  // the real choice a two-curve pick-and-blend command needs from
  // whichever end of each curve the caller actually selected. `false`
  // means the blend's own derivative at that end equals the curve's OWN
  // derivative there unchanged (the blend proceeds AWAY from the picked
  // point in the curve's existing +t direction, as if inserted seamlessly
  // mid-curve); `true` negates every ODD-order derivative first (`d^k P
  // / dt^k` under t -> -t: position and even-order derivatives are
  // unaffected, odd-order ones flip sign - elementary chain rule, the
  // same reparametrization identity `Reverse()`'s own doc comment already
  // relies on for a whole-curve reversal, applied here to one end's local
  // derivative data instead).
  //
  // VALIDATION: `continuity` must be 1, 2 or 3 - only these three are
  // implemented (a G4+ generalization is the identical construction at a
  // higher degree, not attempted here since neither this kernel's own
  // prior art nor the app commands this replaces go past G2 today, and
  // PARITY_MAP.md's own gap is phrased as "G3+", not any specific higher
  // bound). `t0` must lie within `curve0.Domain()`, `t1` within
  // `curve1.Domain()`. Throws `std::invalid_argument` for either.
  // Returns `Result::Failed`, `out` left unchanged, if curve0's position
  // at `t0` coincides (within a relative tolerance of the two points'
  // own scale) with curve1's position at `t1` (a zero-length blend has
  // no well-defined tangent direction to solve for), or if the
  // underlying `ON_Curve::Evaluate` fails to return `continuity`
  // derivatives at either parameter (e.g. a degenerate/invalid curve).
  static Result BlendCurves(const NurbsCurve& curve0, double t0, bool reverse0, const NurbsCurve& curve1, double t1,
                             bool reverse1, int continuity, NurbsCurve& out);

  // Edits this curve, near the end named by `at_min`, so it meets
  // `target`'s own end (named by `target_at_min`) with the requested
  // continuity - the curve-level counterpart to `NurbsSurface::
  // MatchEdge()` (surface_edit.cpp), closing this kernel's own
  // disclosed gap that curve end-continuity matching only ever existed
  // as an app-level heuristic (`MatchCommand`, cmd_curves2.cpp,
  // position/tangent only). Genuinely simpler than the surface version:
  // matching one curve's single end point against another's needs no
  // knot-vector-compatibility reconciliation at all (that whole step
  // exists for `MatchEdge()` only because a shared EDGE is itself a
  // curve with its own parameterization that must line up point-for-
  // point along its full length) - this only ever touches this curve's
  // own first 1-3 control points near `at_min`, using exactly the same
  // clamped-B-spline end-derivative formulas `MatchEdge()`'s own per-row
  // construction already applies, here evaluated once (a single point,
  // not a row indexed along an edge) instead of per control point along
  // a shared edge.
  //
  // `continuity` selects how many of this curve's own end control
  // points move: `Position` (G0) moves only the very end one, to
  // `target`'s own end point exactly (a clamped curve's endpoint
  // literally IS its end control point, so this is always exact, up to
  // rounding). `Tangent` (G1) also adjusts the next one in so this
  // curve's own end derivative becomes `-scale` times the target's
  // (antiparallel - both pointing away from the shared joint, the same
  // sign convention this kernel's GCon-style continuity analysis
  // already uses), where `scale` is this curve's own pre-edit end speed
  // over the target's (so the edit changes direction, not magnitude)
  // unless a positive `end_derivative_scale` is passed to force a
  // specific value instead - matching `MatchEdge()`'s own
  // `cross_scale` parameter. `Curvature` (G2) additionally adjusts a
  // third control point so the end second derivative matches `scale^2`
  // times the target's, the same scale-squared relationship a genuine
  // reparameterization-invariant curvature match requires (and
  // `MatchEdge()`'s own S_vv/B relationship already derives).
  // Transparently elevates this curve's own degree (via
  // `ElevateDegree()`, already shape-preserving) and inserts an
  // interior knot if there aren't enough control points to satisfy the
  // requested continuity without disturbing the curve's OTHER end - the
  // same two preconditions `MatchEdge()` enforces on its own surface's
  // cross direction. Both curves must be clamped (`IsClamped(2)`);
  // throws std::invalid_argument if `target.Degree() < ` the derivative
  // order `continuity` needs. Self-checks the result by evaluating both
  // curves' own ends after the edit (not just trusting the construction)
  // and refuses (`Result::Failed`, curve left completely unchanged) if
  // that check or `IsValid()` fails - the same discipline `MatchEdge()`
  // already applies. `report`, if non-null, always receives the
  // self-check's own real measured errors.
  Result MatchEnd(bool at_min, const NurbsCurve& target, bool target_at_min, MatchContinuity continuity,
                   MatchEndReport* report = nullptr, double end_derivative_scale = 0.0);

  // Analyzes end continuity (G0/G1/G2) between this curve and `other`,
  // automatically picking whichever pair of ends - this curve's own
  // `Domain().min`/`Domain().max` against `other`'s own - land closest
  // together in 3D, the same "nearest ends" semantics the app's own
  // `GCon` command already used before this method existed (moved
  // here, not duplicated, so a future caller doesn't have to re-derive
  // the same "which end, which sign flip" logic by hand - see
  // `MatchEnd()`'s own analogous normalization above). Every number
  // comes from real evaluation at the curves' own exact domain
  // endpoints (`PointAt()`/`TangentAt()`/`CurvatureAt()`) - never
  // sampled or interpolated, unlike a full-edge continuity check over
  // many interior points (a materially different, larger problem this
  // method does not attempt).
  EndContinuityReport AnalyzeEndContinuity(const NurbsCurve& other) const;

  // Curve-to-curve deviation (PARITY_MAP.md's Curve operations category,
  // "Curve-to-curve deviation (CrvDeviation)"): the real minimum and
  // maximum closest-point distance from this curve to `other`, sampled
  // across this curve's own parameter domain. Unlike the app's old
  // CrvDeviation command (a fixed 100-sample `DivideByCount` with no
  // tolerance guarantee at all), this doubles the sample count - starting
  // from `SuggestedSamples(tolerance)` - until two successive refinements'
  // own min AND max both agree to within `tolerance`, the same
  // "numerically-robust estimate from comparing successive refinements"
  // tier `LengthToTolerance()` already uses (no closed-form error bound
  // exists for an arbitrary NURBS curve's closest-point distance either,
  // so this is honestly not a formally certified bound). Returns
  // `Result::Failed` (leaving `out_min`/`out_max` at whatever the last
  // attempted refinement measured) if convergence isn't reached within 20
  // doublings - a pathological case (e.g. the two curves osculating at a
  // point where closest-point distance varies non-smoothly) rather than
  // silently reporting an unconverged estimate as final. `out_samples`,
  // if non-null, receives the sample count the converged estimate actually
  // settled on. Throws `std::invalid_argument` if `tolerance` isn't
  // positive.
  Result DeviationTo(const NurbsCurve& other, double tolerance, double& out_min, double& out_max,
                      int* out_samples = nullptr) const;

  int Degree() const;
  int ControlPointCount() const;

  // Whether the curve has per-control-point weights that aren't all
  // identical (a genuine NURBS curve, not just a polynomial B-spline in
  // disguise) - e.g. a circle built via `ON_Circle::GetNurbForm()` is
  // rational (its control points need non-uniform weights to trace a
  // true circular arc), while `FromControlPoints()`'s own construction
  // never is (`Create()` is always called with `is_rational=false`
  // there). Delegates to `ON_NurbsCurve::IsRational()`.
  bool IsRational() const;

  // The homogeneous weight of control point `i` - 1.0 for every control
  // point on a non-rational curve (`IsRational()` false), and whatever
  // real per-point value was set on a rational one. Delegates to
  // `ON_NurbsCurve::Weight(i)`, whose own source (verified by reading it,
  // not assumed) short-circuits to a hardcoded 1.0 on a non-rational
  // curve without ever indexing `i` at all - so an out-of-range `i` is
  // safe there, but is a genuine unchecked out-of-bounds read on a
  // rational one (`m_cv[i * stride + dim]`, no bounds check).
  double WeightAt(int i) const;

  // Sets control point `i`'s homogeneous weight, promoting a non-rational
  // curve to a genuinely rational one on demand if needed - this is the
  // actual construction-side counterpart to `WeightAt()`/`IsRational()`
  // above, the real gap those two read-only accessors left: without it,
  // this kernel could only ever *read* a rational curve someone else
  // built (e.g. via `ON_Circle::GetNurbForm()`), never build one of its
  // own directly through this API. Delegates to `ON_NurbsCurve::
  // SetWeight(i, w)`, whose own source (verified, not assumed) calls
  // `MakeRational()` automatically the first time a weight other than
  // 1.0 is set on a non-rational curve - `FromControlPoints()`'s curves
  // start non-rational, but aren't stuck that way.
  //
  // IMPORTANT, verified by debug run rather than assumed: this does NOT
  // rescale the control point's own stored coordinates to compensate, so
  // it also moves that control point's own represented position (its
  // `PointAt`-style Euclidean location if it were degree 1 alone), not
  // just its blending influence - OpenNURBS' internal representation is
  // literally (x, y, z, w) evaluated as (x, y, z) / w, and `SetWeight`
  // only touches the `w` component. Concretely, raising control point
  // `i`'s weight from 1.0 to `w` moves that control point's own position
  // to `original_position / w` (its raw x/y/z are untouched) - confirmed
  // with a hand-derived exact rational-quadratic-Bezier midpoint
  // (0.5, 0.25, 0), not the naive weighted-average intuition of "same
  // position, more influence" would suggest. A caller wanting to keep a
  // control point's position fixed while changing only its influence
  // must also rescale its raw coordinates accordingly - this method
  // alone doesn't do that.
  //
  // Returns Result::Failed if `i` is out of range (checked directly
  // against `ControlPointCount()` here, rather than relying on
  // `ON_NurbsCurve::SetWeight`'s own bounds check - that check happens
  // deep inside OpenNURBS' rational-conversion path, so calling
  // `WeightAt(i)` first to detect a no-op, as this method does, would
  // otherwise hit `WeightAt()`'s own documented unchecked out-of-bounds
  // read on a rational curve; a real bug this method's own first draft
  // had, caught by testing the out-of-range case directly, not assumed
  // safe). Returns Result::NoOpAlreadySatisfied if `weight` already
  // equals `WeightAt(i)`.
  Result SetWeightAt(int i, double weight);

  // Control point `i`'s actual Euclidean position - the real gap this
  // file's own `CVCountU()`/`CVCountV()` comment (on the surface side)
  // already flagged existed, and `SetWeightAt()` makes sharper: after
  // raising a weight, `WeightAt()` alone can't tell a caller where that
  // control point now actually sits. Delegates to `ON_NurbsCurve::
  // GetCV(i, ON_3dPoint&)`, whose own source (verified, not assumed)
  // divides out the weight on a rational curve - i.e. this returns the
  // real (x, y, z) location, not the raw, weight-entangled stored
  // coordinate `SetWeightAt()`'s own doc comment describes. Throws
  // std::out_of_range if `i` is outside `[0, ControlPointCount())`
  // (checked directly here, the same discipline `SetWeightAt()`'s own
  // fix established, rather than trusting `GetCV()`'s bool return alone
  // - `ON_NurbsCurve::CV(i)`'s own indexing has no bounds check).
  Point3d ControlPointAt(int i) const;

  // Sets control point `i`'s Euclidean position directly. A real,
  // verified caveat found by testing this against `SetWeightAt()`
  // rather than assumed independent of it: `ON_NurbsCurve::SetCV(i,
  // const ON_3dPoint&)`'s own documentation says a rational curve's
  // weight at `i` gets reset to 1.0 as a side effect - so calling this
  // after `SetWeightAt()` on the same control point undoes the weight
  // change, it doesn't compose with it the way one might assume two
  // independent setters would. Throws std::out_of_range under the same
  // condition as `ControlPointAt()`.
  Result SetControlPointAt(int i, Point3d point);

  // Number of entries in the curve's own knot vector - not the same as
  // `ControlPointCount()`: a clamped knot vector of `order = degree + 1`
  // has `ControlPointCount() + degree - 1` knots (the standard NURBS
  // relationship), confirmed by a debug run rather than assumed, since
  // this file's own knot vectors are always clamped-uniform
  // (`MakeClampedUniformKnotVector()`) but a general knot vector's exact
  // count still follows the same formula regardless. Delegates to
  // `ON_NurbsCurve::KnotCount()`.
  int KnotCount() const;

  // Knot value at `i`. Delegates to `ON_NurbsCurve::Knot(i)`, whose own
  // source (verified, not assumed) has the *opposite* safety profile
  // from `Weight(i)`'s: `Knot(i)` indexes `m_knot[i]` directly with no
  // bounds check at all (regardless of rational/non-rational), a genuine
  // unchecked out-of-bounds read for any out-of-range `i` - so this
  // method validates `i` against `KnotCount()` itself first, throwing
  // std::out_of_range, the same discipline `ControlPointAt()` already
  // established for the identical class of gap.
  double KnotAt(int i) const;

  // Sets knot `i` to `value` directly - a real, deliberately narrow
  // capability: this does NOT re-validate that the resulting knot vector
  // is still non-decreasing (a NURBS requirement `ON_NurbsCurve::
  // SetKnot()` itself doesn't enforce either, confirmed by reading its
  // source), so a caller reordering knots into a decreasing sequence
  // gets undefined evaluation behavior from OpenNURBS itself, not a
  // thrown error from this wrapper. Unlike `Knot(i)`, `ON_NurbsCurve::
  // SetKnot()` already bounds-checks `i` internally and returns false
  // rather than indexing out of bounds (confirmed, not assumed) -
  // returns Result::Failed in that case, or Result::NoOpAlreadySatisfied
  // if `value` already equals `KnotAt(i)`.
  Result SetKnotAt(int i, double value);

  // Inserts a new knot at `knot_value` with the given `multiplicity`,
  // via real Boehm's-algorithm knot refinement (`ON_NurbsCurve::
  // InsertKnot`) - genuinely adds control points without changing the
  // curve's own shape at all: same `PointAt(t)` for every `t` before and
  // after (to a tight numerical tolerance - confirmed by testing several
  // parameter values, not just trusting the documented "does not change
  // parameterization or locus" guarantee; a real floating-point wrinkle
  // found in that testing is that exact bit-for-bit equality does NOT
  // hold, since the refined control net evaluates the same true shape
  // through a different arithmetic path, rounding differently in the
  // last couple of ULPs). `knot_value` must be strictly interior to the
  // curve's own domain (`Domain().min < knot_value < Domain().max`,
  // OpenNURBS' own documented requirement - a value at or outside either
  // end isn't a valid insertion point since the end knots already have
  // full multiplicity), and `multiplicity` must be between 1 and
  // `Degree()` inclusive (inserting more than `Degree()` copies would
  // exceed the maximum multiplicity a knot can have and introduce a
  // discontinuity this method doesn't exist to create). Throws
  // std::invalid_argument outside those ranges - checked directly here
  // rather than relying on `InsertKnot()`'s own bool return, so a
  // caller gets a clear reason rather than an unexplained
  // Result::Failed. Returns Result::Failed if OpenNURBS' own call fails
  // for some other reason.
  //
  // A real, easy-to-misread API nuance, confirmed by testing rather
  // than assumed from the parameter name alone: `multiplicity` means
  // "ensure `knot_value` ends up with at least this multiplicity," not
  // "always insert this many new copies." If `knot_value` already
  // exists in the knot vector with multiplicity >= the requested value,
  // this is a genuine no-op - no new control points or knots are added
  // - but it still returns Result::Ok (OpenNURBS' own `InsertKnot`
  // returns true, since the postcondition is already satisfied), not
  // Result::NoOpAlreadySatisfied, since detecting that case in advance
  // would require duplicating OpenNURBS' own knot-multiplicity search.
  Result InsertKnotAt(double knot_value, int multiplicity = 1);

  // Rigorous, deviation-bounded knot removal - the curve-level
  // counterpart to `NurbsSurface::RemoveKnotAt` (surface_edit.cpp),
  // closing this kernel's own disclosed gap that curve knot removal
  // only ever had an app-level *approximate* heuristic
  // (`RemoveKnotApprox`, cmd_curves2.cpp), nothing in the kernel itself
  // with a real, checked error bound. Removes one occurrence of the
  // knot at `knot_index` (if its multiplicity is more than one, one
  // copy of it, same as `InsertKnotAt()`'s own per-call granularity) via
  // Piegl & Tiller's standard knot-removal construction (their
  // Algorithm A5.8, the same one `NurbsSurface::RemoveKnotAt` already
  // applies row-by-row to a surface - duplicated here, not shared
  // across translation units, to keep this file self-contained the same
  // way every other curve-editing routine in it already is): computes
  // the new control polygon in homogeneous coordinates, measures the
  // worst-case Euclidean deviation the removal would introduce (via the
  // same rational/non-rational distance bound the surface version
  // uses), and only commits the removal if that deviation is within
  // `tolerance` - otherwise leaves the curve completely untouched and
  // returns Result::Failed. `knot_index` must name a knot strictly
  // inside the curve's own domain (throws std::out_of_range if out of
  // `[0, KnotCount())`, std::invalid_argument if not strictly interior),
  // and the curve must be clamped (an unclamped/periodic curve's ends
  // have no single well-defined knot-removal case this handles).
  // `out_max_deviation`, if non-null, always receives the measured
  // deviation bound - even on a refused removal (as positive infinity
  // before any measurement is possible, e.g. an unclamped curve), so a
  // caller can see exactly how close a refused removal came.
  Result RemoveKnotAt(int knot_index, double tolerance, double* out_max_deviation = nullptr);

  // Promotes the curve to rational (every control point gets an
  // explicit weight of 1.0) if it isn't already - delegates to
  // `ON_NurbsCurve::MakeRational()`. Genuinely shape-preserving: giving
  // every control point weight 1.0 is exactly the implicit weighting a
  // non-rational curve already had, confirmed by testing rather than
  // assumed (`PointAt(t)` identical before and after, not just close).
  // Returns Result::NoOpAlreadySatisfied if `IsRational()` is already
  // true.
  Result MakeRational();

  // Demotes the curve to non-rational, dividing each control point's
  // own raw coordinates by its weight to preserve that CONTROL POINT's
  // own Euclidean position - delegates to `ON_NurbsCurve::
  // MakeNonRational()`. A real, significant, surprising finding from
  // testing this against a genuine circle (not assumed safe just
  // because each individual control point ends up in the geometrically
  // "correct" place): this does NOT preserve the CURVE's own shape
  // unless every weight was already equal. Forcing uniform (1.0)
  // weighting onto now-Euclidean-correct control points blends them
  // with ordinary polynomial (not rational) basis functions, which is a
  // mathematically different curve whenever the original weights
  // varied - confirmed by measuring a genuine radius-5 circle's own
  // radius after this call actually varying (5.0 to ~5.28) rather than
  // staying constant, i.e. it stops being a circle at all, not just a
  // slightly-off approximation of one. Only genuinely shape-preserving
  // when every control point already shared the same weight (the
  // mirror-image condition of `MakeRational()`'s own guarantee).
  // Returns Result::NoOpAlreadySatisfied if `IsRational()` is already
  // false.
  Result MakeNonRational();

  // Tolerance-bounded overload of `MakeNonRational()` above: performs the
  // identical conversion, then measures the actual max 3D deviation the
  // conversion introduced (dense uniform sampling over `Domain()`, at
  // least 200 points or 20 per control point, whichever is larger,
  // comparing this curve's own `PointAt(t)` before and after) and only
  // keeps the conversion if that measured deviation is <= `tolerance` -
  // otherwise restores the curve to its pre-call rational state and
  // returns Result::Failed. `out_max_deviation`, if non-null, always
  // receives the measured value, including on failure (zero if the
  // curve was already non-rational, in which case this is the same
  // Result::NoOpAlreadySatisfied no-op as the untoleranced overload).
  // Unlike `NurbsSurface::RemoveKnotAt`'s rigorous analytic bound, this
  // is a SAMPLED measurement, not a certified one - the same honesty
  // tier `NurbsSurface::Rebuild()`'s own deviation bound already uses in
  // this kernel, since there is no known closed-form error formula for
  // this particular conversion (unlike knot removal's Piegl & Tiller eq.
  // 5.30) to certify it analytically instead.
  Result MakeNonRational(double tolerance, double* out_max_deviation);

  // Elevates the curve's degree in place, preserving its shape exactly
  // (to floating-point precision) - `PointAt(t)` is the same for every
  // `t` before and after. Returns NoOpAlreadySatisfied if `new_degree <=
  // Degree()`, Result::Failed if the curve is not a valid NURBS curve.
  //
  // NOT a wrapper around `ON_NurbsCurve::IncreaseDegree` any more: a
  // randomized probe found that routine silently CORRUPTS the shape of a
  // curve with a non-uniform knot vector at moderate-to-high degree
  // (measured at unit scale: up to 1.7e-6 off at degree 8, 3.2e+2 at
  // degree 12, 1e+24 - garbage - at degree 16 with repeated knots; even a
  // uniform degree-19 curve drifted 3.4e-6). Implemented instead via
  // detail/degree_elevate.h: Piegl & Tiller's published algorithm A5.9
  // (Bezier decomposition, closed-form per-segment elevation, exact knot
  // removal) for the minimal `ControlPointCount() + t * spans` result,
  // VERIFIED against an unconditionally-exact piecewise-Bezier elevation
  // by direct evaluation, and falling back to that exact Bezier form
  // (same degree and shape, more control points - full-multiplicity
  // interior knots) whenever knot removal's own high-degree ill-
  // conditioning would have moved the curve by more than 1e-12 of its
  // control polygon's bounding-box diagonal. So the control-point count
  // of the result is minimal in the common case (every degree <= 5 case
  // probed; most degree <= 8 ones) but not guaranteed; the shape always
  // is. A periodic curve comes back clamped (same as before).
  Result ElevateDegree(int new_degree);

  // Degree REDUCTION - the direction `ElevateDegree()` above does not
  // attempt (per its own doc comment, "never lowers"), closing
  // PARITY_MAP's own disclosed "Change curve degree ... no reduction"
  // gap. Unlike `ElevateDegree()`'s exact, shape-preserving Bezier
  // construction, there is no exact closed form for lowering a general
  // NURBS curve's degree while keeping its shape - a genuine least-
  // squares APPROXIMATION is the honest answer here, the same tier this
  // class's own `OffsetInPlane()` general-curve case and
  // `MakeNonRational(tolerance, ...)` already use: sample this curve
  // densely (`max(200, 20 * ControlPointCount())` points), then refit at
  // `target_degree` via the already-tested `FitLeastSquares()`, starting
  // from the minimum possible control-point count (`target_degree + 1`,
  // a single Bezier segment) and doubling it - exactly `OffsetInPlane()`'s
  // own "start small, double, measure, stop once the real worst-case
  // deviation (each sample's `ClosestPoint()` on the fresh fit, not the
  // least-squares residual `FitLeastSquares()` itself minimizes) is at or
  // under `tolerance`" search, capped at this curve's own current
  // `ControlPointCount()` (more control points at a LOWER degree than
  // the original already had at its higher one defeats the entire point
  // of reducing degree, so this refuses rather than silently returning a
  // "reduced" curve that is not actually smaller). Mutates this curve in
  // place only on success; returns `Result::Failed` (curve left
  // untouched) if `tolerance` still isn't met at that ceiling, and
  // `Result::NoOpAlreadySatisfied` if `target_degree >= Degree()` (there
  // is nothing to reduce - mirroring `ElevateDegree()`'s own convention
  // for the symmetric case, rather than silently no-op-ing a request this
  // method was never asked to do). Throws std::invalid_argument if
  // `target_degree < 1` or `tolerance` is not positive. `out_max_deviation`,
  // if non-null, receives the achieved (or, on failure, the best
  // attempted) worst-case deviation - a sampled, not formally certified,
  // bound, the same honesty this file's other sampling-based deviation
  // checks already disclose.
  Result ReduceDegree(int target_degree, double tolerance, double* out_max_deviation = nullptr);

  // Curve FAIRING (Rhino's own Fair command) - closes PARITY_MAP's own
  // disclosed "Curve fairing/smoothing ... no kernel fairing" gap: the
  // existing Laplacian smoothing (`dino8-app/src/commands/cmd_meshtools.cpp`/
  // `cmd_remaining.cpp`) operates on a TESSELLATED MESH approximation of a
  // curve, with no tolerance guarantee at all; this is a genuine kernel-
  // native NURBS operation, smoothing the actual control polygon, not a
  // sampled proxy of it. Every INTERIOR control point (index 1 through
  // `ControlPointCount() - 2`; the two endpoints, index 0 and the last,
  // are never touched, so the curve's own start/end position is always
  // preserved exactly) is relaxed `iterations` times toward the midpoint
  // of its own two immediate neighbors - `cv[i] = (1 - factor) * cv[i] +
  // factor * 0.5 * (cv[i-1] + cv[i+1])`, the standard explicit Laplacian
  // smoothing step, applied to every interior point SIMULTANEOUSLY each
  // iteration (not sequentially, which would bias later-indexed points
  // toward whatever their already-moved neighbor just became). This is a
  // real curvature-variation reducer - repeatedly averaging a sharp kink's
  // neighbors pulls it toward their shared line - but, like
  // `ReduceDegree()`/`MakeNonRational(tolerance, ...)`, an honestly-
  // disclosed APPROXIMATION with no formally certified bound. What this
  // method does NOT skip, unlike a naive "just smooth it" implementation:
  // the actual resulting shape change is MEASURED (dense sampling, `PointAt`
  // before vs. after at the same parameter values - valid directly, since
  // relaxing control points never touches the knot vector or domain) and
  // the whole operation is refused (`Result::Failed`, curve left
  // untouched) if that measured deviation exceeds `tolerance`, exactly the
  // same "sampled, not certified, but genuinely checked" tier this class's
  // other approximate reshaping methods already use - a caller never gets
  // a silently-over-smoothed curve dressed up as satisfying a tolerance it
  // doesn't meet. Returns `Result::NoOpAlreadySatisfied` if
  // `ControlPointCount() < 3` (no interior control point exists to move at
  // all - a line or single-span Bezier with only 2 control points has
  // nothing this method could do). Throws `std::invalid_argument` if
  // `tolerance` isn't positive, `iterations < 1`, or `factor` isn't in
  // `(0, 1]` (0 would move nothing; a `factor` of exactly 1 fully replaces
  // each interior point with its neighbor midpoint every iteration, the
  // strongest smoothing this method allows in one step). `out_max_deviation`,
  // if non-null, receives the achieved (or, on refusal, the attempted)
  // worst-case deviation.
  Result Fair(double tolerance, int iterations = 10, double factor = 0.5,
              double* out_max_deviation = nullptr);

  // Whether the curve's start and end points coincide - either because
  // it's genuinely periodic (its own knot vector wraps) or because a
  // clamped curve's own two endpoints just happen to be the same point
  // (e.g. `FromControlPoints()` given a control point list whose first
  // and last entries match). The surface-level counterpart to
  // `NurbsSurface::IsClosed()`. Delegates to `ON_NurbsCurve::IsClosed`
  // after verifying it's a real implementation (falls back to an actual
  // endpoint-coincidence check for a non-periodic curve, not a stub).
  // Confirmed by testing, not just reading the source: `ON_NurbsCurve::
  // IsClosed` unconditionally requires at least 4 control points before
  // it even looks at endpoint positions, so a 3-point coincident
  // -endpoint polyline (a perfectly valid, genuinely closed-looking
  // triangle-wedge shape) reports false here - a real, narrower
  // guarantee than "any coincident-endpoint curve reports closed".
  bool IsClosed() const;

  // Whether the curve's own knot vector is genuinely periodic - a
  // stronger condition than IsClosed() (every periodic curve is closed,
  // but a clamped curve can be closed - matching endpoints - without
  // being periodic at all). Delegates to `ON_NurbsCurve::IsPeriodic`
  // after the same stub-vs-real verification.
  bool IsPeriodic() const;

  // Converts a closed (but not periodic) curve into a genuinely periodic
  // one WITHOUT refitting or approximating: this is the exact-shape
  // -preserving re-knot (Rhino's MakePeriodic Smooth=No), as distinct
  // from a periodic-uniform refit (Smooth=Yes, which relaxes the shape
  // to smooth the seam). Requires `IsClosed()` and `Degree() >= 2`
  // (returns Result::Failed otherwise; a degree-1 curve has no periodic
  // form by convention - see `ON_IsKnotVectorPeriodic`'s own comment).
  // A no-op (Result::NoOpAlreadySatisfied) if already periodic.
  //
  // Algorithm (the standard technique the caller asked for - real, not a
  // fit): first Bezier-decompose the curve (bring every interior knot up
  // to full multiplicity `Degree()`, via the same real, verified
  // shape-preserving `InsertKnotAt` this class already exposes - so every
  // former breakpoint, including the two ex-clamped ends, has an
  // identical, uniform multiplicity structure). Then drop the duplicated
  // closing control point and re-attach the first `Degree()` control
  // points at the tail (the periodic wraparound), extending the knot
  // vector past the old domain end by continuing its own interior knot
  // spacing (not a new pattern - literally the curve's own next knot
  // deltas, shifted by one period) so the control polygon and knot
  // vector both wrap using the curve's own degree, exactly as asked.
  //
  // This reproduces the original curve's PointAt(t) for every t in the
  // original domain to within machine-precision-scale error (empirically
  // 1e-15 to a few 1e-11, comfortably inside the 1e-9 exactness this
  // command promises) - NOT bit-exact, for one specific, narrow, verified
  // reason: the direct construction above places a genuinely zero-width
  // knot span exactly at the new domain's own end (an unavoidable
  // consequence of needing `Degree()` full-multiplicity copies of the
  // end knot to represent the wraparound while an ordinary clamped curve
  // already spends all `Degree()` of its own copies getting there - the
  // two encodings can't both fit in the same array position without
  // this). Evaluating a rational curve exactly AT a zero-width knot span
  // is not numerically robust in OpenNURBS itself (confirmed directly:
  // it returns literal Inf/garbage there, not just an approximate value)
  // - so this nudges that one run of duplicate knots apart by a relative
  // 1e-13 of the domain length, trading an unmeasurable, bounded amount
  // of exactness for an evaluator that actually returns a number
  // everywhere in the domain, including exactly at its own two ends.
  Result MakePeriodicExact();

  // Whether the curve's entire shape lies within `tolerance` of some
  // plane - the curve-level counterpart to `NurbsSurface::IsPlanar()`.
  // Delegates to `ON_NurbsCurve::IsPlanar` after verifying it's a real
  // implementation (checks `IsLinear()` first, then fits/verifies an
  // actual plane through the curve's own tangent and sampled points -
  // not a stub). Defaults to `ON_ZERO_TOLERANCE` (OpenNURBS' own
  // default). A straight line is planar (trivially, any plane containing
  // it works) - verified directly, not assumed.
  bool IsPlanar(double tolerance = ON_ZERO_TOLERANCE) const;

  // Whether the curve's entire shape lies within `tolerance` of the
  // straight line through its own two endpoints - a stronger condition
  // than IsPlanar() (every linear curve is planar, but a planar curve
  // - an arc, say - need not be linear). Delegates to `ON_Curve::
  // IsLinear` (already relied on internally by `IsPlanar()`'s own real
  // implementation, verified there, not a stub) after the same
  // stub-vs-real verification. Defaults to `ON_ZERO_TOLERANCE`. Useful
  // for checking whether a curve degenerated to a straight line after
  // an operation like Trim()/Extend() on what started as a curved
  // segment, without needing to inspect control points by hand.
  bool IsLinear(double tolerance = ON_ZERO_TOLERANCE) const;

  // Whether the curve's entire shape is (a portion of) a circular arc
  // within `tolerance`. Delegates to `ON_Curve::IsArc` after verifying
  // it's a real implementation (fits an actual plane and circle through
  // sampled points, not a stub - `ON_NurbsCurve::IsArc` falls back to
  // this same base method for the general case). Defaults to
  // `ON_ZERO_TOLERANCE`.
  bool IsArc(double tolerance = ON_ZERO_TOLERANCE) const;

  // A stronger condition than IsArc(): whether the curve is not just an
  // arc but a *full* circle (an arc whose own angle is exactly 2*pi -
  // `ON_Arc::IsCircle()`'s own definition). Delegates to `IsArc()`'s
  // same real underlying fit, then checks the fitted arc's angle -
  // verified against both a genuine full circle (true) and a partial
  // arc built from the identical circle (false), so this isn't just
  // "IsArc() under a different name".
  bool IsCircle(double tolerance = ON_ZERO_TOLERANCE) const;

  // Reverses the curve's parameterization in place: what was
  // `PointAt(domain.Min())` becomes `PointAt(domain.Max())` and vice
  // versa (the curve's own 3D shape is unchanged - same points, opposite
  // direction of travel), so `TangentAt()` at any point flips sign too.
  // Confirmed by testing, not assumed: the domain interval's own
  // min/max *values* aren't necessarily preserved (a `[0, 1]` domain
  // came back as `[-1, 0]` in one verified case) - callers walking the
  // curve by parameter must re-fetch `raw().Domain()` after calling this
  // rather than reusing a domain captured beforehand. A real gap nothing
  // here could answer before: nothing in this file could flip a curve's
  // own direction without discarding it and rebuilding from reversed
  // control points (losing any degree elevation or other in-place edits
  // already applied). Delegates to `ON_NurbsCurve::Reverse` after
  // verifying it's a real implementation (reverses both the knot vector
  // and control point list, not a stub). Returns Result::Failed if
  // OpenNURBS' own call fails.
  Result Reverse();

  // Shortens the curve in place to just the sub-domain `[t0, t1]`
  // (`t0 < t1`, both within the curve's current domain) - the curve's
  // own shape outside that range is discarded, not just hidden, and its
  // new domain becomes exactly `[t0, t1]`. A real gap nothing here could
  // answer before: nothing in this file could cut a curve down to part
  // of itself without re-sampling points and rebuilding a brand new
  // curve through them (an approximation, not the exact same underlying
  // curve restricted to a smaller range). Delegates to
  // `ON_NurbsCurve::Trim` after verifying it's a real implementation (a
  // genuine de Boor knot-insertion algorithm, not a stub). Returns
  // Result::Failed if `t0 >= t1` or OpenNURBS' own call fails.
  Result Trim(double t0, double t1);

  // Splits the curve at parameter `t` (strictly inside the curve's
  // current domain, not at either end) into two independent curves
  // written to `out_left`/`out_right` - `out_left` covering the original
  // domain's start up to `t`, `out_right` covering `t` to the original
  // end - without modifying `*this`. The complement to Trim(): Trim()
  // keeps one sub-range and discards the rest, Split() keeps both
  // halves as separate curves. Delegates to `ON_NurbsCurve::Split` after
  // verifying it's a real implementation (genuine knot insertion at `t`
  // for each half, the same underlying algorithm Trim() uses, not a
  // stub). Returns Result::Failed if `t` isn't strictly inside the
  // curve's domain or if OpenNURBS' own call fails.
  Result Split(double t, NurbsCurve& out_left, NurbsCurve& out_right) const;

  // Extends the curve in place so its domain includes `[t0, t1]` -
  // Trim()'s opposite: instead of cutting the curve down, this
  // analytically extrapolates it outward past whichever end(s) of
  // `[t0, t1]` fall outside the curve's current domain, leaving the
  // curve's own existing shape over its original domain completely
  // unchanged (this is the curve's own documented guarantee, not just an
  // assumption: `ON_NurbsCurve::Extend` only moves the affected end's
  // knots/control points via a De Boor extrapolation, the same
  // underlying curve-representation machinery `Trim()`/`Split()` use).
  // Only extends whichever end(s) `[t0, t1]` actually reach past - if it
  // already sits entirely within the current domain, returns
  // NoOpAlreadySatisfied rather than calling into OpenNURBS at all.
  // Returns Result::Failed if `t0 >= t1`, the curve is closed (extending
  // a closed curve is undefined - matches `ON_NurbsCurve::Extend`'s own
  // documented restriction), or OpenNURBS' own call fails.
  Result Extend(double t0, double t1);

  // Joins `other` onto the end of this curve in place, producing ONE
  // continuous NURBS curve - the first curve-combining operation here
  // (every earlier method cuts a curve down or reshapes one in place;
  // nothing could chain two curves into a single one, the "Join" every
  // modeler has). Exact, not a re-fit: delegates to
  // `ON_NurbsCurve::Append` after verifying by reading its source that
  // it's a real implementation - it degree-elevates the lower-degree
  // operand, makes both rational if either is, clamps the ends, then
  // splices the two knot vectors with `other`'s knots shifted so its
  // domain continues from this curve's end - so both operands' own
  // shapes survive to floating-point round-off (degree elevation and
  // clamping are shape-preserving, the same machinery `ElevateDegree()`
  // and `Trim()` already rely on).
  //
  // A real `Append` behavior, found by reading rather than assumed, that
  // this wrapper exists to guard: `Append` never checks that the two
  // curves actually meet - it simply DISCARDS `other`'s first control
  // point in favour of this curve's last one (its control-point copy
  // loop starts at index 1), so appending a curve that doesn't start
  // where this one ends would silently snap the junction shut and
  // distort `other`'s first span rather than fail. So this checks first:
  // `other`'s start must lie within `tolerance` of this curve's end - or,
  // as a convenience matching Rhino's own Join, `other`'s END may
  // instead, in which case a reversed copy of `other` is what gets
  // appended. Throws std::invalid_argument if neither end meets (the
  // message reports both measured gaps) or if this curve is closed
  // (nothing can be appended to a closed loop). `tolerance` defaults to
  // the same 1e-6 `Mesh::MergeAndWeld()` uses for coincident points.
  //
  // Continuity at the junction is exactly what the two curves had
  // geometrically: always C0 (position), a tangent kink if their
  // tangents differ there - no smoothing is applied. The result's domain
  // is this curve's domain extended by `other`'s domain length, with the
  // junction at exactly this curve's previous `Domain().max`, so a
  // parameter `s` on `other` maps to `old_max + (s - other_min)` on the
  // result. Returns Result::Failed if either curve has fewer than 2
  // control points or OpenNURBS' own Append fails.
  Result Join(const NurbsCurve& other, double tolerance = 1e-6);

  // The curve's own parameter domain [min, max] - the valid range for
  // `t` in `PointAt(t)`, `TangentAt(t)`, `CurvatureAt(t)`, and every
  // other by-parameter method below. Not necessarily [0, 1]: e.g.
  // `FromControlPoints()`'s clamped uniform knot vector gives a
  // `cv_count - degree`-wide domain, not a normalized one - confirmed by
  // testing, not assumed. Every doc comment in this file that references
  // `Domain().Min()`/`Domain().Max()` means this method's own
  // `.min`/`.max`, not a method on the return value.
  Interval Domain() const;

  // Reparameterizes the curve so `Domain()` becomes `[t0, t1]`, with every
  // existing knot and evaluated point mapped by the same affine stretch
  // (shape, control points and weights are untouched - only the parameter
  // values change). The surface-level counterpart to `NurbsSurface::
  // SetDomain(direction, t0, t1)`, minus the direction argument a curve
  // doesn't have. Delegates to `ON_NurbsCurve::SetDomain`, the same real
  // (non-stub) implementation `MakeCompatible()` (sweep.cpp) already
  // relies on internally to normalize loft/sweep sections to `[0, 1]`
  // before comparing their knot vectors. Returns Result::Failed if `t0 <
  // t1` doesn't hold or OpenNURBS' own call fails, or
  // Result::NoOpAlreadySatisfied if `[t0, t1]` already equals `Domain()`.
  Result SetDomain(double t0, double t1);

  Point3d PointAt(double t) const;

  // Finds the parameter along the curve's own domain whose PointAt() is
  // closest to `point`: a coarse `samples`-point scan of the domain to
  // bracket the nearest sample, then a golden-section refinement of the
  // distance-squared function within that bracket. This is a from-scratch
  // numeric search, not a wrapper - verified directly against the v8.34
  // source that OpenNURBS' public `ON_Curve` API has no
  // `GetClosestPoint()`/closest-point method at all (grepped the whole
  // source tree, not just this class), the same "declared for Rhino, not
  // present in the public build" gap this file already found for
  // `Length()`'s own arc-length method. Not a guaranteed global minimum
  // for a pathological multi-modal distance function (e.g. a curve
  // passing near `point` at more than one well-separated parameter) -
  // refines only around whichever coarse sample happens to be nearest,
  // the same "approximate, not exhaustive" honesty `Length()`'s own
  // polyline sampling already documents. Increasing `samples` narrows the
  // risk of missing a closer, separate local minimum, at proportional
  // cost. For a closed curve (`ON_Curve::IsClosed()`), the golden-section
  // refinement window wraps across the seam instead of clamping to the
  // domain boundary - the same seam-wrap fix `NurbsSurface::
  // ClosestPointParameter()` needed, and for the identical reason: a
  // coarse sample landing near the domain boundary must be able to
  // explore just past the seam, on the physically-adjacent far side of
  // the same domain edge, or the search can converge to a wrong point
  // near, but not at, the true nearest point across the seam.
  double ClosestPointParameter(Point3d point, int samples = 200) const;

  // The actual closest point: `PointAt(ClosestPointParameter(point,
  // samples))`. The curve-level counterpart to `Mesh::ClosestPoint()`.
  Point3d ClosestPoint(Point3d point, int samples = 200) const;

  // Delegates to `ON_Curve::GetTightBoundingBox`. DESPITE THE NAME, this
  // is *not* a genuine tight/exact bound for a general curve in the
  // public OpenNURBS build - verified by reading the source
  // (opennurbs_bezier.cpp): `ON_BezierCurve::GetTightBoundingBox`
  // literally calls `ON_GetPointListBoundingBox` (its own comment says
  // "good enough for file IO needs in the public source code version"),
  // i.e. each Bezier span's own *control-point* bounding box, not a real
  // extremum search. Confirmed by testing, not just reading: a quadratic
  // curve whose true extremum (0.5) lies strictly inside its parameter
  // domain gets bounded by its control point's coordinate (1.0) instead -
  // a real, valid (never excludes part of the curve), but not minimal,
  // bound. Exact only when the curve's true extremum happens to coincide
  // with a control point or an endpoint (a straight line; certain
  // standard rational-conic constructions, e.g. a NURBS circle whose
  // control points sit on-curve at the cardinal angles). Throws
  // std::runtime_error if OpenNURBS' own call fails.
  BoundingBox GetTightBoundingBox() const;

  // Approximate arc length via polyline sampling: evaluates `samples + 1`
  // points evenly across the curve's own parameter domain and sums the
  // straight-line distance between consecutive ones. This is a
  // from-scratch implementation, not a wrapper - verified directly
  // against the v8.34 source that OpenNURBS' public `ON_Curve` API has no
  // `GetLength()`/arc-length method at all (grepped the whole source
  // tree, not just this class), the same "declared for Rhino, not present
  // in the public build" pattern chunk 2 found for `ON_Brep::CreateMesh`
  // and this chunk found for `ON_SubD::BrepForm`. A polyline's chord
  // length always understates a smooth curve's true arc length, so this
  // converges to the true length from below as `samples` increases - for
  // a straight-line curve (no curvature to approximate) it's exact at any
  // sample count.
  double Length(int samples = 1000) const;

  // Arc length via adaptive 5-point Gauss-Legendre quadrature of the
  // curve's own speed `|C'(t)|` (the real OpenNURBS first derivative,
  // `ON_Curve::Ev1Der`, not the unit `TangentAt()`), instead of
  // `Length()`'s own polyline chord sum. Starts from one Gauss estimate
  // over the whole domain, then recursively bisects any sub-interval
  // whose own 5-point estimate disagrees with the sum of its two half-
  // interval estimates by more than that sub-interval's own share of
  // `tolerance` (the share halving at each recursion level, so every
  // leaf sub-interval's worst-case total error budget still sums to at
  // most `tolerance` overall) - the standard adaptive-quadrature error
  // indicator (comparing a quadrature rule against its own refinement),
  // capped at 20 recursion levels (2^20 leaf intervals) as a safety
  // bound against runaway recursion on a pathological curve. This is a
  // genuine accuracy IMPROVEMENT over `Length()` - 5th-order per
  // interval instead of a 1st-order chord sum, and adaptive where
  // `Length()` is uniform - but, like `NurbsSurface::Rebuild()`'s own
  // deviation bound, the resulting accuracy is a numerically-robust
  // estimate from comparing successive refinements, not a formally
  // certified worst-case bound the way `RemoveKnotAt()`'s Piegl & Tiller
  // eq. 5.30 bound is (there is no analogous closed-form error formula
  // for Gauss-Legendre quadrature of an arbitrary NURBS curve's speed).
  // `out_subintervals`, if non-null, receives the number of leaf
  // intervals the adaptive refinement actually settled on - a cheap way
  // for a caller to see how much work a given `tolerance` demanded.
  // Throws std::invalid_argument if `tolerance` isn't positive.
  double LengthToTolerance(double tolerance, int* out_subintervals = nullptr) const;

  // Finds the parameter `t` at which the curve has traveled
  // `target_length` of its own arc length from `Domain().Min()` -
  // the inverse of `Length()`: walking the same `samples`-point polyline
  // `Length()` itself builds, finding which polyline segment contains
  // `target_length`, and linearly interpolating `t` within that segment
  // (the standard approach for arc-length reparametrization when only a
  // polyline approximation - not a closed-form arc-length function - is
  // available, which is the real situation here per `Length()`'s own
  // documented "no such method in the public build" gap). Clamps to
  // `Domain().Min()`/`Domain().Max()` for a `target_length` outside
  // `[0, Length(samples)]` rather than extrapolating past the curve.
  // Exact for a straight line (uniform speed makes the linear
  // interpolation exact at any sample count) - verified directly, not
  // assumed.
  double ParameterAtArcLength(double target_length, int samples = 1000) const;

  // Divides the curve into `count` sub-segments of exactly equal arc
  // length, returning the `count + 1` parameter values at the
  // boundaries between them (starting at `Domain().Min()`, ending at
  // `Domain().Max()`) - "arc-length parametrization" in the sense
  // that's actually useful for evenly spacing points/objects along a
  // curve, unlike the curve's own raw parameter (which a non-arc-length
  // -parametrized NURBS curve, e.g. one built with non-uniform knots,
  // does not travel at constant speed along). Built directly on
  // `ParameterAtArcLength()` - calls it `count - 1` times at
  // `Length(samples) * i / count` for each interior boundary. Verified
  // exact on a straight line (uniform speed makes this identical to
  // dividing the parameter domain itself evenly) and on a full circle
  // (equal arc-length divisions land at equal angular spacing, checked
  // via equal consecutive-point chord lengths, not assumed from the
  // formula). Throws std::invalid_argument if `count <= 0`.
  std::vector<double> DivideByCount(int count, int samples = 1000) const;

  // The "by fixed length" half of this kernel's "Divide curve by N/fixed
  // length" gap - `DivideByCount()` above only ever covers "by N" (a
  // caller-chosen segment count, whatever length each segment happens to
  // come out to); this divides into sub-segments of a caller-chosen
  // `length` instead, whatever count that happens to produce. Returns
  // the parameter values at each full-`length` boundary, always starting
  // at `Domain().Min()` and always ending at `Domain().Max()` - built the
  // same way `DivideByCount()` is, directly on `ParameterAtArcLength()`:
  // for a curve of `Length(samples) == L`, walks `length`, `2 * length`,
  // `3 * length`, ... and calls `ParameterAtArcLength()` at each value
  // that still leaves a non-negligible remainder before `L`, stopping
  // one boundary short of the end so the final `Domain().Max()` pushed
  // below is never duplicated by a last boundary landing within a
  // rounding-scale sliver of `L` (the same "ulp past the end" hazard
  // `ParameterAtArcLength()`'s own doc comment already discloses, here
  // guarded with a relative tolerance instead of relying on that
  // method's own clamp). The curve's own total length need not be an
  // exact multiple of `length` - the final sub-segment is simply
  // whatever is left over, which may be shorter than `length` (or, for
  // a `length` greater than or equal to the whole curve, there is no
  // interior boundary at all and this returns exactly `{Domain().Min(),
  // Domain().Max()}`, the single-segment case). Throws
  // std::invalid_argument if `length` isn't positive.
  std::vector<double> DivideByLength(double length, int samples = 1000) const;

  // Unit tangent direction at parameter `t` - the direction of travel
  // along the curve, not a raw (unnormalized) derivative. Delegates to
  // `ON_Curve::TangentAt` - verified as a real implementation (calls
  // through to `Ev1Der`/`EvTangent`, not a stub like `ON_Brep::CreateMesh`
  // or `ON_SubD::BrepForm`) before relying on it, same standing discipline
  // this file already applied to `Length()`.
  Vector3d TangentAt(double t) const;

  // Curvature vector at parameter `t`: points from the curve toward its
  // local center of curvature, with magnitude `kappa = 1/R` (R the local
  // radius of curvature) - the zero vector wherever the curve is locally
  // straight (a line segment; an inflection point). Delegates to
  // `ON_Curve::CurvatureAt` after verifying it's a real implementation
  // (calls through to `EvCurvature`/`Ev2Der`, an actual second-derivative
  // computation, not a stub) - same standing discipline this file already
  // applies to `Length()`/`TangentAt()`. Verified against a NURBS circle
  // built via `ON_Circle::GetNurbForm`: at every parameter tested, the
  // curvature vector's own magnitude equals exactly `1/radius`, and
  // `point + curvature_vector / curvature_vector.LengthSquared()`
  // (the point offset by `R` along the curvature direction - the
  // standard way to recover the osculating circle's center from a
  // nonzero curvature vector) lands exactly on the circle's known center.
  Vector3d CurvatureAt(double t) const;

  // Suggests how many polyline samples a sampler like Length() would
  // need to keep each sampled chord's deviation from the true curve
  // under `chord_tolerance` - a first, modest, curvature-informed step
  // toward this kernel's own flagged "adaptive/curvature-aware meshing"
  // gap (see README), not a full adaptive re-tessellation (this returns
  // one number for the whole curve, not a per-region sample density).
  // Samples CurvatureAt() at `curvature_samples` points across the
  // domain, takes the single largest curvature magnitude found (the
  // tightest local radius of curvature, `R = 1/kappa_max`), and applies
  // the standard circular-arc chord-height (sagitta) formula assuming
  // the *entire* curve turns at that tightest radius - a deliberately
  // conservative (never under-samples a genuinely varying curve) but not
  // tight estimate, exact only when curvature really is constant
  // (verified on a full circle, whose known circumference/radius gives
  // an exact expected turning angle to compare against, not an
  // approximation on both sides of the check). A curve with negligible
  // curvature everywhere (a straight line) returns 1. Throws
  // std::invalid_argument if `chord_tolerance <= 0`.
  int SuggestedSamples(double chord_tolerance, int curvature_samples = 50) const;

  // Returns a genuinely non-uniform, curvature-adaptive set of
  // parameter values across the domain (always starting at
  // `Domain().Min()`, ending at `Domain().Max()`) - denser where the
  // curve actually bends more, sparser where it's flatter, rather than
  // `SuggestedSamples()`'s single global count spread uniformly. This
  // is real per-region adaptivity (unlike `SuggestedSamples()`'s
  // "assume the whole curve is as tight as its worst point"
  // conservative estimate), a further step toward this kernel's own
  // flagged "adaptive/curvature-aware meshing" gap. Implemented via
  // recursive chord-height (flatness) testing - the standard curve
  // -flattening technique (the same idea browser/font rendering engines
  // use to turn a Bezier into line segments): bisect `[t0, t1]` if its
  // midpoint deviates from the straight chord between `PointAt(t0)` and
  // `PointAt(t1)` by more than `chord_tolerance`, recursing up to
  // `max_depth` levels (a safety cap against pathological curves, not
  // expected to bind in ordinary use). A straight line needs no
  // bisection at all and returns exactly `[Domain().Min(),
  // Domain().Max()]` (2 values) - confirmed directly, not assumed.
  // Throws std::invalid_argument if `chord_tolerance <= 0`.
  std::vector<double> SuggestedParameterValues(double chord_tolerance, int max_depth = 12) const;

  // Angle-based counterpart to SuggestedSamples(): instead of back-solving
  // a maximum per-segment turning angle FROM a linear chord_tolerance via
  // the sagitta formula, this takes that angular bound directly as
  // `angle_tolerance` (radians) - PARITY_MAP's own disclosed "Angular
  // tolerance control exposed as a general faceting-quality knob (as
  // opposed to per-command heuristics)" gap: before this, the only places
  // this kernel ever reasoned about facet/tangent angular deviation were
  // ad hoc, buried inside chamfer/fillet/draft-specific code, with no
  // general tessellation-quality entry point that takes an angle at all.
  // Same curvature sampling and "assume the whole curve turns at its
  // tightest sampled radius" conservative estimate SuggestedSamples()
  // already uses, just skipping the chord_tolerance -> angle conversion
  // and using `angle_tolerance` as the per-segment turning bound directly.
  // Throws std::invalid_argument if `angle_tolerance` is not in (0, pi].
  int SuggestedSamplesByAngle(double angle_tolerance, int curvature_samples = 50) const;

  // Angle-based counterpart to SuggestedParameterValues(): the same
  // recursive subdivision structure, but the per-interval test compares
  // the tangent direction change across `[t0, t1]` (the angle between
  // `TangentAt(t0)` and `TangentAt(t1)`) against `angle_tolerance`,
  // instead of comparing the midpoint's distance from the chord against a
  // linear chord_tolerance - a genuinely different quality criterion, not
  // just the same number in different units: a tight loop whose chord
  // stays short even as it turns sharply fails the angle test long before
  // it would fail a chord-height one. Throws std::invalid_argument if
  // `angle_tolerance` is not in (0, pi].
  std::vector<double> SuggestedParameterValuesByAngle(double angle_tolerance,
                                                       int max_depth = 12) const;

  // Offsets this curve, in its own fitted plane, by `distance` along the
  // in-plane direction `TangentAt(t) x plane.zaxis` (a consistent
  // "right of travel, as seen from +plane.zaxis" side at every
  // parameter) - the curve-level counterpart to `NurbsSurface::
  // OffsetAnalytic()`, with the analogous honesty split: EXACT for the
  // two shapes whose true offset (the literal locus of points at
  // `distance` along that direction, not an approximation of it) is
  // itself the same closed-form type, and an explicitly-approximate
  // least-squares refit (via this class's own real `FitLeastSquares()`,
  // not a stub) for everything else - never silently presented as exact.
  //
  //  - A LINE offsets to an exact parallel line (`FromControlPoints()`
  //    of the two translated endpoints).
  //  - A POLYLINE (3 or more straight segments, detected via
  //    `ON_Curve::IsPolyline()`) offsets EXACTLY, corner by corner: each
  //    edge gets its own offset line, and a shared vertex lands at the
  //    exact intersection of its two adjacent edges' offset lines (the
  //    standard angle-bisector miter point, refused - `Result::Failed` -
  //    only at a genuine near-180-degree fold, a zero-length edge, or an
  //    edge parallel to `plane.zaxis`). A closed polygon's own seam
  //    vertex is mitered the same way, wrapping around - it does not
  //    split. This REPLACES what used to happen here: falling to the
  //    general sampled-refit case below, which smooths every corner into
  //    a blurred curve instead of keeping it sharp.
  //  - A CIRCULAR ARC (or full circle) offsets to an exact CONCENTRIC
  //    arc/circle of the SAME plane, center, and angular span
  //    (`DomainRadians()`), radius
  //    `radius +/- distance` - trivial but exact, since every point on a
  //    circle moves radially by exactly `distance`. The +/- sign is
  //    resolved the same way `NurbsSurface::OffsetAnalytic()` resolves
  //    it for a sphere/cylinder/torus (see there for why it can't be
  //    assumed fixed): AT RUNTIME, by comparing `TangentAt(t) x
  //    plane.zaxis` against the independently-known true outward radial
  //    direction `point - center` at one sample point, rather than
  //    trusted from `ON_Arc`'s own parametrization convention - so
  //    `distance > 0` always GROWS the arc/circle here, whichever way
  //    this particular curve's own tangent happens to wind. Refused
  //    (`Result::Failed`, `out` untouched) if `radius +/- distance <= 0`
  //    - the exact curve counterpart of `OffsetAnalytic()`'s sphere/
  //    cylinder self-intersection guard: the offset distance exceeds
  //    this arc's own (constant) radius of curvature and folds it
  //    through its own center.
  //  - Any other curve (not a line, polyline, or circular arc) is
  //    treated as a general planar curve: sampled
  //    uniformly across `Domain()` at `max(SuggestedSamples(chord_tol),
  //    4 * ControlPointCount()) + 1` points (`SuggestedSamples()`'s own
  //    curvature-informed count, floored so `FitLeastSquares()` below
  //    always has comfortably more samples than unknowns; `chord_tol` =
  //    `tolerance::RelativeDistance()` of this curve's own
  //    `GetTightBoundingBox()` diagonal), each moved by `distance` along
  //    its own `TangentAt(t) x plane.zaxis` (this curve-level `plane`'s
  //    own fitted zaxis, one FIXED but otherwise arbitrary sign for the
  //    whole curve - a real, disclosed limitation: unlike the arc case
  //    above, a general curve has no independently-known "outward" to
  //    check the sign against, so which side is positive is whatever
  //    `IsPlanar()`'s own fit happens to assign, not chosen by the
  //    caller beyond the sign of `distance` itself), then refit via
  //    `FitLeastSquares()` at this curve's own `Degree()` - an honestly
  //    APPROXIMATE result (the true offset of a general curve is
  //    generally not itself an exact NURBS curve of the same degree at
  //    all), unlike the two exact cases above, but not an UNBOUNDED one:
  //    this is `tolerance`-DRIVEN, the real "Loose/Tolerance refit" option
  //    Rhino's own Offset command exposes and this method previously
  //    lacked entirely (it fit once, at this curve's own
  //    `ControlPointCount()`, with no check of how far off that fit
  //    actually was - a real, silent accuracy gap in this method's own
  //    first version, closed here rather than left as a known issue).
  //    Starting from `ControlPointCount()`, the control-point count is
  //    doubled and refit, each time measuring the actual worst-case
  //    deviation between the sampled offset points above and their own
  //    `ClosestPoint()` on the freshly-fitted curve (not the fit
  //    residual `FitLeastSquares()` itself minimizes, which is an
  //    aggregate least-squares quantity, not a guaranteed per-point
  //    bound), until that worst case is at or under `tolerance` or the
  //    count would exceed the number of sampled points (the most
  //    `FitLeastSquares()` is defined for) - whichever comes first. If
  //    `tolerance` still isn't met at that ceiling, this returns
  //    `Result::Failed` rather than silently handing back the best
  //    attempt dressed up as satisfying a tolerance it doesn't meet.
  //    Before refitting, every sample is checked
  //    against `CurvatureAt(t)`: wherever `distance` moved it TOWARD that
  //    point's own center of curvature by at least that point's own
  //    local radius (`1 / kappa`), the offset would fold the curve
  //    through itself there, and this returns `Result::Failed` (`out`
  //    untouched) instead of silently building a self-intersecting
  //    result - the direct curve analogue of `OffsetAnalytic()`'s cone
  //    guard, and the real hazard a plain per-point translate-and-refit
  //    would otherwise hide. This sampling-based check, like `Length()`'s
  //    own sampling, can miss a hazard strictly between two samples on a
  //    pathologically fast-varying curve - not an exhaustive proof, the
  //    same honesty this file's other sampling-based methods already
  //    disclose.
  //
  // Returns `Result::Failed`, `out` left unchanged, if this curve isn't
  // planar within `tolerance` at all (a genuinely non-planar 3D offset -
  // e.g. sweeping a curve's own Frenet frame - is a different, harder
  // operation this method does not attempt), or if `FitLeastSquares()`
  // itself fails in the general case (fewer than `Degree() + 1` samples,
  // which does not happen at this method's own default sample count, but
  // could if a caller passed a very small `samples`).
  //
  // `tolerance` defaults (`<= 0`) to `tolerance::DistanceForSize()` of
  // this curve's own `GetTightBoundingBox()` diagonal - `IsPlanar()`/
  // `IsLinear()`/`IsArc()`'s own `ON_ZERO_TOLERANCE` default is, as
  // `NurbsSurface::OffsetAnalytic()`'s own doc comment already found for
  // the surface case, far tighter than anything but a hand-built exact
  // primitive tolerates.
  //
  // `corner_style` (default `Sharp`, every prior caller's unchanged
  // behavior) only affects the POLYLINE case above: see
  // `CurveOffsetCornerStyle`'s own doc comment for exactly which corners
  // `Round`/`Chamfer` change and why a reflex-under-outward-offset corner
  // is left alone either way. A genuine bonus of `Round`/`Chamfer` beyond
  // cosmetics: a convex corner whose turn is close enough to a full 180
  // degrees that `Sharp`'s own finite-miter check (`denom <= 1e-9` below)
  // would refuse it outright now succeeds instead, landing a
  // correspondingly near-semicircular arc or long chamfer chord - neither
  // style ever NEEDS the miter point at a fill corner at all, only the
  // two tangent directions, so the same near-flat turn that has no finite
  // sharp corner still has a perfectly well-defined round or chamfered
  // one. For a CLOSED polyline, every style also refuses
  // (`Result::Failed`) rather than silently returning a self-crossing
  // loop when the exact `Sharp` miter polygon itself is not simple
  // (`detail::IsSimplePolygon()`) - a detection-only guard for the
  // long-disclosed "inward offset of a concave curve can self-intersect"
  // gap, not a repair: a distance that is genuinely safe still succeeds.
  Result OffsetInPlane(double distance, NurbsCurve& out, double tolerance = -1.0,
                       CurveOffsetCornerStyle corner_style = CurveOffsetCornerStyle::Sharp) const;

  // Same construction as `OffsetInPlane(double, ...)` above, but the
  // sweep direction at every parameter is `TangentAt(t) x plane.zaxis`
  // for a CALLER-SUPPLIED `plane`, not this curve's own `IsPlanar()` fit.
  // Closes the real gap the other overload's doc comment names as out of
  // scope: that one refuses (`Result::Failed`) any curve that isn't
  // planar at all, and even for a planar curve always uses that curve's
  // OWN fitted plane, never a plane the caller picks (the same
  // limitation the app's `Offset` command has, per PARITY_MAP.md's
  // offsetshell category: "kernel `OffsetInPlane` works in the curve's
  // own fitted plane ... returns Failed for non-planar curves ... The
  // app Offset command uses only the active CPlane normal. No 3D
  // offset."). This overload accepts ANY `plane` (its normal need not
  // relate to the curve's own shape at all) and ANY curve, planar or
  // genuinely non-planar in 3D - the per-parameter direction
  // `TangentAt(t) x plane.zaxis` is well-defined regardless, exactly the
  // "offset using an explicit CPlane" behavior the app command already
  // wants. The result curve is generally NOT itself planar when this
  // curve is non-planar or `plane` isn't the curve's own fitted plane -
  // that's the whole point of a caller-chosen plane, not a defect.
  //
  //  - A LINE still offsets exactly (`FromControlPoints()` of the two
  //    translated endpoints, translated by `distance` along
  //    `line_direction x plane.zaxis`) - this generalizes for free: a
  //    line's own offset direction is exact for ANY plane whose normal
  //    isn't parallel to the line, not only the line's own default
  //    plane.
  //  - A POLYLINE (3 or more straight segments) that is ITSELF coplanar
  //    in `plane` (every vertex within `tolerance` of `plane`, checked
  //    explicitly - not assumed) offsets exactly via the same
  //    corner-by-corner miter the single-plane overload above uses. A
  //    polyline that is NOT coplanar in `plane` (a genuinely 3D
  //    polyline, offset along a plane unrelated to its own shape) falls
  //    to the same general sampled path as any other curve below - the
  //    exact miter formula only lands on both adjacent offset lines when
  //    every edge is perpendicular to `plane.zaxis`, which a foreign
  //    plane doesn't guarantee.
  //  - Every other curve (including a circular arc, and including a
  //    genuinely non-planar curve) falls to the same sampled,
  //    tolerance-driven `FitLeastSquares()` refit the other overload's
  //    "general planar curve" case uses, with `plane.zaxis` in place of
  //    this curve's own fitted `zaxis` at every sample, and the same
  //    curvature fold guard (`CurvatureAt(t)` against `distance`) at
  //    each sample. An arc is not special-cased exactly here: offsetting
  //    a circle along a foreign plane's normal is not itself a circle
  //    in general, so it takes the same approximate path as any other
  //    curve.
  //
  // Returns `Result::Failed`, `out` left unchanged, if `TangentAt(t) x
  // plane.zaxis` is degenerate (zero) at any sample - the curve's own
  // tangent runs parallel to `plane`'s normal there, so no offset
  // direction exists - or if the curvature fold guard trips, or if the
  // tolerance-driven refit can't reach `tolerance` even at its own
  // maximum feasible control-point count. Throws `std::invalid_argument`
  // if `distance` isn't finite or `plane` isn't `ON_Plane::IsValid()`.
  //
  // `corner_style` - see the other overload's own doc comment - applies
  // the same way to this overload's own coplanar-polyline case.
  Result OffsetInPlane(const ON_Plane& plane, double distance, NurbsCurve& out, double tolerance = -1.0,
                       CurveOffsetCornerStyle corner_style = CurveOffsetCornerStyle::Sharp) const;

  // Offsets this curve - assumed to (approximately) lie on `surface` -
  // along `surface`'s own normal, sampled and refit: the kernel
  // counterpart to PARITY_MAP's "Curve offset normal to surface
  // (OffsetNormal)" gap ("app `OffsetNormal`: samples moved along the
  // surface normal, then cubic interpolation. App-only"). Identical
  // technique dino8-app's own OffsetNormal command previously implemented
  // entirely by hand (cmd_remaining.cpp): this curve is sampled at
  // `sample_count` parameter values, each sample's closest point on
  // `surface` (`NurbsSurface::ClosestPointParameter()`) gives the normal
  // to move it along by `distance`, and a new curve is refit through the
  // offset points - now a real kernel entry point, usable by anything
  // else that needs a surface-normal curve offset, not reimplemented ad
  // hoc at the app layer.
  //
  // This is explicitly NOT the separate, harder "Curve offset on surface"
  // PARITY_MAP item (an in-surface, geodesic-style offset that stays
  // within the surface): this method moves along the surface's normal,
  // off the surface entirely, exactly as the app technique it replaces
  // always did.
  //
  // An OPEN curve of degree 1 is rebuilt as an exact degree-1 polyline
  // through the offset samples (`FromControlPoints(..., 1)`); any other
  // curve (higher degree, or closed) is refit as a global chord-length
  // cubic interpolant through the offset samples (`InterpolateCubic()`,
  // closed-aware) - the same "don't oversmooth a polyline into a cubic"
  // choice the app technique already made, not a new claim of exactness
  // (a straight segment offset along a genuinely varying surface normal
  // is not literally straight anymore either way).
  //
  // `sample_count` (default `<= 0`, meaning 40 for an open curve or 48
  // for a closed one, this method's own prior app-level constants) sets
  // how densely this curve is sampled before offsetting and refitting.
  //
  // Throws std::invalid_argument if `distance` isn't finite, or if
  // `sample_count` is positive but less than 2 (`sample_count <= 0` means
  // "use the default" instead, same convention `tolerance <= 0` uses
  // elsewhere in this class). Returns `Result::Failed` if fewer than 2
  // samples produce a usable (unitizable) surface normal - not enough
  // points left to build a curve from.
  Result OffsetOnSurfaceNormal(const NurbsSurface& surface, double distance, NurbsCurve& out,
                                int sample_count = -1) const;

  // Offsets this curve - assumed to (approximately) lie on `surface` -
  // IN the surface, staying on it rather than moving off along its
  // normal: the kernel counterpart to the separate, harder
  // "Curve offset on surface (in-surface, geodesic-style)" PARITY_MAP
  // item `OffsetOnSurfaceNormal()`'s own doc comment above explicitly
  // excludes. Identical technique dino8-app's own `OffsetCrvOnSrfCommand`
  // previously implemented entirely by hand (cmd_curves2.cpp): this
  // curve is sampled at `sample_count` parameter values; at each sample,
  // the closest point on `surface` (`ClosestPointParameter()`) gives a
  // local frame there (`NormalAt()` for the surface normal, `TangentAt(t)
  // x normal` for the in-surface sideways direction perpendicular to both
  // this curve's own tangent and the surface's normal); the sample moves
  // by `distance` along that sideways direction and is immediately
  // re-projected back onto `surface` via a second `ClosestPointParameter`
  // + `PointAt()` round trip - the step that keeps the result genuinely
  // ON the surface rather than merely near it, since moving a flat
  // distance in a straight line along a tangent-plane direction leaves a
  // curved surface the instant the direction itself curves away - and a
  // new curve is refit through the re-projected points.
  //
  // Calling this a "geodesic-style" offset, not a true geodesic one, is
  // deliberate honesty, inherited unchanged from the app technique this
  // replaces: a true geodesic offset would move each point along the
  // surface's own geodesic normal to the curve at that point (a curve
  // confined to stay perpendicular to the curve's own in-surface
  // tangent, measured intrinsically); this instead moves a fixed
  // DISTANCE along `TangentAt(t) x surface_normal`, extrinsically
  // straight, then snaps back to the surface - a good approximation
  // wherever the surface's curvature is mild relative to `distance`, the
  // same approximation Rhino's own OffsetCrvOnSrf documentation itself
  // describes, not a new limitation introduced here.
  //
  // An OPEN curve of degree 1 is rebuilt as an exact degree-1 polyline
  // through the offset samples; any other curve (higher degree, or
  // closed) is refit as a global chord-length cubic interpolant - the
  // same convention `OffsetOnSurfaceNormal()` above already uses, for
  // the same "don't oversmooth a polyline into a cubic" reason.
  //
  // `sample_count` (default `<= 0`, meaning 40 for an open curve or 48
  // for a closed one, the same defaults `OffsetOnSurfaceNormal()` uses)
  // sets how densely this curve is sampled before offsetting and
  // refitting.
  //
  // Throws std::invalid_argument if `distance` isn't finite, or if
  // `sample_count` is positive but less than 2. Returns `Result::Failed`
  // if fewer than 2 samples produce a usable local frame - either the
  // surface normal or `TangentAt(t) x normal` fails to unitize there (the
  // latter exactly when this curve's own tangent runs parallel to the
  // surface's normal at that sample, the in-surface analogue of
  // `OffsetInPlane()`'s own degenerate-tangent refusal) - not enough
  // points left to build a curve from.
  Result OffsetInSurface(const NurbsSurface& surface, double distance, NurbsCurve& out,
                          int sample_count = -1) const;

  const ON_NurbsCurve& raw() const { return curve_; }
  ON_NurbsCurve& raw() { return curve_; }

 private:
  ON_NurbsCurve curve_;
};

}  // namespace dino8::kernel
