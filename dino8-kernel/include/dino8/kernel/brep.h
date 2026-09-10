#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/surface.h"

namespace dino8::kernel {

class Mesh;

// Wraps ON_Brep.
//
// IMPORTANT CORRECTION to the original blueprint this kernel is built
// from: OpenNURBS ships zero boolean/CSG operations. There is no
// BooleanUnion, BooleanIntersection, or BooleanDifference anywhere in its
// public API, for B-reps or for meshes — verified directly against the
// v8.34 source, not assumed. OpenNURBS's own docs are explicit that it's
// a geometry *representation* library (curves, surfaces, breps, meshes,
// file I/O), not a modeling kernel with solid operations on top. Rhino's
// actual boolean engine is closed-source and lives outside OpenNURBS.
//
// What OpenNURBS *does* give us: exact tessellation of a Brep into
// ON_Mesh objects (Tessellate() below). That's the real foundation a
// boolean engine needs — an actual mesh-boolean algorithm (e.g. a
// vetted library like Manifold, or a from-scratch BSP/CSG
// implementation) is a distinct, substantial next chunk, not something
// "wrapping OpenNURBS" gets us for free.
//
// A real architectural fact, checked directly (`ON_Brep::IsValid()`), not
// assumed - narrowed here to name exactly which factories it still
// applies to, now that it's no longer all of them: `Box()`, `Sphere()`,
// `TrimmedPlanarFace()`, and `FromSurface()` all still build their face(s)
// via `ON_Brep::NewFace(int surface_index)` - the minimal, surface-only
// overload - rather than constructing genuine `ON_Brep` vertex/edge/trim/
// loop topology the way Rhino's own file format expects. `ON_Brep::
// IsValid()` checks exactly that topology, so it reports every Brep any
// of those four factories builds as invalid, even a perfectly good one
// like `Box()`. This doesn't stop a Brep built by one of them from being
// fully usable through this kernel's own pipeline, which never calls
// `IsValid()` and doesn't need the topology it checks for: `Tessellate()`
// reads each face's surface directly, and `TessellateToClosedMesh()`'s
// own vertex-welding step is what actually closes the seams between
// faces, not shared `ON_Brep` vertex/edge records. Still, a `.3dm` file
// saved via `Model::AddBrep()` from one of these four may not round-trip
// cleanly through other OpenNURBS-based tools that validate topology on
// load.
//
// `FromPlanarFaces()`/`FromMixedFaces()` (below) are the exception: they
// build genuine `ON_BrepVertex`/`ON_BrepEdge`/`ON_BrepLoop`/`ON_BrepTrim`
// topology (coincident loop points welded into shared vertices, a real
// edge created once and reused - never a third time - by whichever
// second face also walks it, one real outer loop and trim per face), so
// `ON_Brep::IsValid()` reports a Brep from either of them as valid - and,
// unlike the four factories above, their own `.3dm` round-trips carry
// real topology into other OpenNURBS-based readers, not just this
// kernel's own pipeline. Everything built ON TOP of `FromPlanarFaces()`/
// `FromMixedFaces()` inherits this for free: `BooleanCombinePlanar()`,
// `ShellConvexPlanar()`, and `FilletConvexEdge()` (see boolean.h/
// fillet.h) all assemble their result through one of these two, so their
// own results are genuinely `IsValid()`-clean too.
//
// FORMERLY a disclosed, narrow gap, now closed: `FilletConvexEdge()`'s
// own polygonal corner notch (where the filleted edge meets a face
// perpendicular to it - see fillet.h's own doc comment) used to be
// topologically SEPARATE from the fillet's own circular cap edge at that
// same corner (individually valid boundary trims, but not a literal
// shared edge, so that one corner reported as open/non-manifold). It now
// gets a LITERAL shared `ON_BrepEdge` with the cap - see `PlanarFace`'s
// own `notch_begin`/`notch_count` fields below and `FromMixedFaces()`'s
// own comment for exactly how: the cap's own true isocurve is built
// first (a reordered Pass 3/4 visits every `CylindricalFace` before any
// `PlanarFace`), and the notched face's own dense polygonal run is
// collapsed to just its own two endpoints for topology purposes -
// exactly the two points welded to the cap's own two corner vertices -
// so the second face to reach that vertex pair reuses the cap's own edge
// (and its own true arc curve) instead of building a new, unshared one,
// the same "second face reuses the first face's real edge" mechanism the
// straight-rail sharing above already relies on. The notched face's own
// 2D trim curve for that one segment is a genuine multi-point polyline
// through the original dense points (not a 2-point chord), so a fresh
// consumer deriving this Brep's own boundary purely from stored topology
// (a `.3dm` reload, say) reproduces the same shape this kernel's own
// side-table-driven `Tessellate()` already draws - not a different,
// silently-wrong one. `TestFilletConvexEdgeUnitCubeTopFrontCorner`'s own
// closed-box corner genuinely reports `IsManifold()`'s `has_boundary` as
// `false` and `IsSolid()` as `true` now, not just `IsValid()` (which
// already passed before this fix and was never, on its own, proof the
// gap was closed).
class Brep {
 public:
  // Builds a one-face B-rep whose face is exactly `surface` (untrimmed).
  static Brep FromSurface(const NurbsSurface& surface);

  // Builds a genuine closed solid: an axis-aligned box as six untrimmed
  // bilinear NURBS faces (min corner (x0,y0,z0), max corner (x1,y1,z1)),
  // each oriented so its tessellated triangles face outward. This is what
  // closes the gap the previous chunk's README called out: without it,
  // Brep only ever produced open surfaces, so nothing built through Brep
  // could feed BooleanCombine() (which requires a closed, watertight
  // mesh) - tests had to hand-build box meshes directly instead. Box() is
  // deliberately narrow (one primitive, no general solid construction);
  // it exists to prove the Brep -> Tessellate -> weld -> boolean pipeline
  // end to end, not to be a real primitive library.
  static Brep Box(double x0, double y0, double z0, double x1, double y1,
                   double z1);

  // Builds a genuine closed solid from a single curved face: a sphere,
  // via OpenNURBS' own exact rational-NURBS conversion (ON_Sphere::
  // GetNurbForm) rather than an approximation we'd have to derive
  // ourselves. Unlike Box() (six flat faces stitched at seams), this
  // is one face whose own tessellation seam and poles have to be welded
  // shut with itself - the specific case the previous chunk's README
  // flagged as unvalidated ("not yet validated against curved surfaces").
  static Brep Sphere(Point3d center, double radius);

  // Builds a one-face B-rep whose face is `surface`, trimmed to
  // `trim_loop_uv`: a closed polygon in the surface's own (u, v)
  // parameter space. This is real (if simplified) B-rep trimming - the
  // gap every earlier primitive here sidestepped by only ever building
  // whole untrimmed or whole-closed surfaces. `trim_loop_uv` is stored
  // here rather than as genuine ON_Brep loop/trim/edge topology (a much
  // larger API surface - vertices, edges, 2D and 3D curve pairing,
  // orientation); what this closes is the tessellation-visible gap
  // ("no trimmed surfaces" meant no way to produce a trimmed *shape* at
  // all), not full topological B-rep validity. See NurbsSurface::
  // TessellateGrid's trim_polygon parameter for how the polygon is
  // actually applied.
  //
  // `exact_clip`, if true, tessellates via NurbsSurface::
  // TessellateGridClippedExact() instead of the default whole-cell
  // TessellateGrid() path - real boundary clipping instead of an
  // approximation that only improves with grid resolution. `trim_loop_uv`
  // may be convex or concave (see that method's own comment for how each
  // is handled). Defaults to false so existing whole-cell behavior (and
  // the exact vertex/triangle counts tests assert against it) doesn't
  // change under callers that don't ask for this.
  //
  // `hole_loops_uv`, if non-empty, are additional closed polygons
  // subtracted from the outer trim - an annulus/washer face (a square
  // with a smaller square hole, say). Only supported on the whole-cell
  // path: throws std::invalid_argument if combined with exact_clip=true,
  // since Sutherland-Hodgman clips against a single convex region and
  // doesn't have a "subtract another region" mode.
  //
  // Throws std::invalid_argument if `trim_loop_uv` has fewer than 3
  // points. This closes a genuine footgun found while validating
  // Mesh::Cylinder()/Cone(): before this check, an *empty* `trim_loop_uv`
  // wasn't rejected at all, and this kernel's own `Tessellate()` treats
  // an empty trim loop as "no trim at all" (see its own
  // `trim_loop.empty()` branch) - so a caller that accidentally built an
  // empty loop got the whole untrimmed surface silently, a real,
  // plausible-looking wrong result rather than an error. A 1- or 2-point
  // loop instead silently tessellated to nothing (confirmed by a debug
  // run, not assumed) - neither is a closed polygon, so both are now
  // rejected the same way.
  static Brep TrimmedPlanarFace(const NurbsSurface& surface,
                                 const std::vector<Point2d>& trim_loop_uv,
                                 bool exact_clip = false,
                                 std::vector<std::vector<Point2d>> hole_loops_uv = {});

  int FaceCount() const;

  // One planar face's boundary as a real 3D polygon plus its plane -
  // the representation an exact (non-tessellated) planar B-rep boolean
  // needs to work on directly, instead of a mesh approximation.
  struct PlanarFace {
    ON_Plane plane;             // outward-facing normal (plane.zaxis)
    std::vector<Point3d> loop;  // closed polygon, CCW as seen from outside

    // Optional, narrow extension consumed ONLY by FromMixedFaces()'s own
    // genuine-topology builder - every other producer/consumer of
    // PlanarFace (PlanarFaces(), ClipByHalfspace3d, ExactConvexHull,
    // BooleanCombinePlanar/ShellConvexPlanar) never reads these two
    // fields and is completely unaffected by their default values.
    //
    // Marks loop[notch_begin .. notch_begin+notch_count) (notch_count-1
    // consecutive segments, no wraparound) as a fine polygonal
    // approximation of ONE true circular arc that is known to coincide
    // exactly, in 3D, with an adjacent Brep::CylindricalFace's own
    // circular cap edge - precisely fillet.cpp's own NotchCornerAtVertex
    // corner-notch construction (see fillet.h's own doc comment).
    // notch_count == 0 (the default) means "no such run on this face";
    // FromMixedFaces() ignores these fields entirely in that case.
    //
    // When set, FromMixedFaces() gives that whole run ONE literal shared
    // ON_BrepEdge with the adjacent CylindricalFace's own cap (instead of
    // notch_count-1 short, unshared micro-edges that can never be
    // manifold - see brep.h's own class-level comment for why that used
    // to be a disclosed, not-yet-closed gap) while still preserving the
    // full dense polygon for TESSELLATION: this face's own visible
    // boundary (face_trim_loops_) is completely unaffected by this field
    // - only the real ON_Brep vertex/edge/trim/loop topology construction
    // changes, collapsing the run's own interior points out of the loop's
    // topology-only vertex list and instead threading them into that ONE
    // trim's own 2D curve as a genuine multi-point polyline (not a naive
    // 2-point chord) - the detail that lets the fine notch survive a
    // .3dm round trip (or any other consumer reading this Brep's own real
    // topology, e.g. ResolveFace()'s generic derive-from-topology path)
    // with the same shape/volume as this kernel's own side-table-driven
    // Tessellate(), not just look right in this kernel's own pipeline.
    int notch_begin = 0;
    int notch_count = 0;

    // Optional, narrow extension consumed ONLY by TessellateConforming()
    // (below) - every other producer/consumer of PlanarFace (PlanarFaces(),
    // FromMixedFaces() itself, ClipByHalfspace3d, ExactConvexHull,
    // BooleanCombinePlanar/ShellConvexPlanar, and Tessellate()/
    // TessellateAdaptive()/TessellateNonUniformAdaptive()) never reads
    // this field and is completely unaffected by its default (empty)
    // value - see TessellateConforming()'s own doc comment for the one
    // place this is actually used, and boolean.cpp's own
    // SplitMixedAgainstAllFaces (case (ii), right where
    // detail::ClipPolygonByCircle3d is already called) for the one place
    // it is actually populated.
    //
    // Marks a run of this loop's own vertices as a fine polygonal
    // approximation of ONE true circular arc that is known to coincide
    // exactly, in 3D, with an adjacent Brep::CylindricalFace's own
    // lateral-surface boundary at one end (v=0 or v=length) - precisely
    // the wedge-cap-vs-cylinder-wall shared boundary
    // detail::ClipPolygonByCircle3d's own doc comment discloses as a
    // known, disclosed mesh-watertightness gap (the wedge cap and the
    // cylindrical wall are each tessellated on their own independent
    // local grid today, so their tessellated vertices along that shared
    // boundary don't coincide except at a handful of explicit trim
    // vertices). `arc_runs` does not change that today - Tessellate()
    // ignores it entirely - it only records where the two would need to
    // agree, so TessellateConforming() can make them actually do so.
    struct ArcRun {
      // loop[begin], loop[(begin+1) % loop.size()], ...,
      // loop[(begin+count-1) % loop.size()] - i.e. `count` consecutive
      // points, WRAPPING around this loop's own start/end if
      // begin+count > loop.size(). Unlike notch_begin/notch_count above
      // (which deliberately never wrap), a wraparound run is the COMMON
      // case here: detail::ClipPolygonByCircle3d's own returned wedge
      // polygon always starts and ends AT an arc sample point (see that
      // function's own doc comment for its exact vertex ordering: the
      // very first point pushed is a circle sample, and the very last
      // several points pushed - right up to the implicit closing edge
      // back to that first point - are circle samples too), so the
      // run's own single contiguous stretch of arc vertices, walked in
      // this loop's own stored order, generically crosses the loop's
      // own index-0 seam rather than sitting neatly inside it.
      int begin = 0;
      int count = 0;  // number of points in the run; 0 means "no run"

      // The arc's own center and radius, and the two angles (radians, in
      // `plane_xaxis`/`plane_yaxis`'s own basis below - NOT a second,
      // independently-built basis; see PlanarFace::plane's own doc
      // comment and detail/arc_schedule3d.h's own top comment for why
      // that distinction is exactly what the prior, reverted attempt at
      // this fix got wrong) at loop[begin] and loop[(begin+count-1) %
      // loop.size()] respectively. `angle_end` may be LESS than
      // `angle_begin` (a decreasing sweep) - detail::ArcSchedule3d()
      // handles that directly, no reordering needed.
      Point3d center;
      double radius = 0.0;
      double angle_begin = 0.0;
      double angle_end = 0.0;

      // The owning PlanarFace's own `plane.xaxis`/`plane.yaxis` AT THE
      // TIME this run was recorded, copied here directly rather than
      // re-derived later: `angle_begin`/`angle_end` above are only
      // meaningful relative to this EXACT basis, and a Brep built via
      // FromMixedFaces() does not otherwise persist any per-face
      // ON_Plane once construction has consumed one (see
      // FromMixedFaces()'s own planar-face loop: `plane` is used to
      // build that face's bilinear surface and then discarded) - so
      // TessellateConforming() needs its own copy to convert
      // `angle_begin`/`angle_end` back into 3D points or into another
      // face's own frame at all. A narrow but deliberate widening beyond
      // "pure bookkeeping of already-computed scalars": `plane.xaxis`/
      // `plane.yaxis` are themselves already-computed values (the same
      // ones ClipPolygonByCircle3d's own caller already has in hand),
      // just not otherwise threaded through to where they're needed.
      Vector3d plane_xaxis;
      Vector3d plane_yaxis;
    };
    std::vector<ArcRun> arc_runs;
  };

  // Extracts every face of this Brep as a PlanarFace: the face's own
  // (u, v) domain rectangle (or trim loop, for a TrimmedPlanarFace()
  // face) walked in increasing-parameter order and mapped through the
  // surface's PointAt - which, per every planar-face factory here's own
  // "u_dir x v_dir points outward" convention (see Box()'s comment),
  // already winds each loop CCW as seen from outside. The plane itself
  // is computed directly from that same loop via Newell's method (not
  // from ON_Surface::IsPlanar's own plane, whose sign isn't guaranteed
  // to agree with the loop's winding), so the two are always mutually
  // consistent by construction. Throws std::invalid_argument if any face
  // is not planar (checked via NurbsSurface::IsPlanar) - this is
  // deliberately narrow: a genuine curved-face B-rep boolean is a much
  // larger undertaking (NURBS-NURBS surface intersection + re-trimming,
  // see IntersectSurfaces in dino8-app's own geom layer for the
  // intersection-curve half of that, which this doesn't yet use) and is
  // not attempted here.
  std::vector<PlanarFace> PlanarFaces() const;

  // One curved fillet face's exact geometry: a circular-cylinder patch,
  // the shape a constant-radius rolling-ball fillet along a STRAIGHT edge
  // always is (see FilletConvexEdge in dino8/kernel/fillet.h for the one
  // place this gets built). Deliberately a sibling of PlanarFace, not an
  // overload of it - PlanarFace::plane is a genuine supporting plane the
  // face lies in, which has no meaning for a curved patch.
  //
  // `frame` is reused purely as a local coordinate frame, not a
  // supporting plane: `origin` is a point on the fillet's axis (the
  // cylinder's own axis line), `zaxis` is the axis direction (unit,
  // parallel to the filleted edge), and `xaxis` is the reference
  // direction the sweep's `angle` is measured from (so `frame.xaxis` is
  // exactly the patch's own rail at angle 0). `radius` is the fillet
  // radius; `angle` (radians, in (0, pi) for the convex edges this is
  // built for) is the total angle swept from `xaxis`; `length` is the
  // patch's extent along `zaxis`, starting at `frame.origin`.
  // `outward`, if false, flips the patch's presented (tessellated) normal
  // to point radially INWARD instead of the frame/radius/angle/length
  // geometry's own natural radially-outward direction - needed so a
  // cylindrical face can bound material from the CONCAVE side (e.g. the
  // wall of a drilled hole, whose outward-from-material direction points
  // toward the axis, not away from it), the same role FlipFace() already
  // plays for a PlanarFace via reversing its loop/plane. Unlike
  // PlanarFace (whose loop winding and plane.zaxis directly encode
  // orientation, so no separate flag is needed), a CylindricalFace's own
  // frame/radius/angle/length always describe the SAME physical patch
  // regardless of which way it's meant to face - `outward` is the one bit
  // of information that's otherwise missing, translated by
  // FromMixedFaces() into the underlying ON_BrepFace::m_bRev flag
  // Tessellate()/TessellateAdaptive()/TessellateNonUniformAdaptive() all
  // already respect generically (see their own `m_bRev` check). Defaults
  // to true so every existing caller (FilletConvexEdge's own always-
  // outward fillet patch) is unaffected.
  struct CylindricalFace {
    ON_Plane frame;
    double radius = 0.0;
    double angle = 0.0;
    double length = 0.0;
    bool outward = true;
  };

  // One curved LINEAR-TAPER fillet face's exact geometry: a trimmed
  // right-circular-CONE patch - the shape a rolling ball of LINEARLY
  // varying radius r(t) = r0 + m*t traces along a straight edge between
  // two planes (see FilletConvexEdgeTapered in dino8/kernel/fillet.h for
  // the one place this gets built, and that function's own doc comment
  // for the worked derivation this relies on). A sibling of
  // CylindricalFace, deliberately NOT unified with it into one struct:
  // a cone has no single supporting plane (CylindricalFace::frame.origin
  // sits ON the swept patch; a cone's own natural frame.origin is its
  // APEX, which is always strictly OUTSIDE the trimmed patch whenever
  // both radii are positive - see below) and no single radius, so
  // reusing CylindricalFace's fields would be a lossy fit, not a genuine
  // generalization.
  //
  // The exactness argument (see FilletConvexEdgeTapered's own doc
  // comment for the full derivation, and dino8-kernel's worked-example
  // verification tests for the closed-form checks): for a straight spine
  // C(t) (the rolling ball's own center, itself a straight line whenever
  // r(t) is linear - a real, checked-directly consequence of the
  // tangency construction, not assumed) and LINEARLY varying radius
  // r(t) = r0 + m*t, the classical canal-surface characteristic-circle
  // formula (Peternell & Pottmann, "Computing rational parametrizations
  // of canal surfaces," J. Symbolic Computation 23(2-3), 1997) reduces
  // to: every characteristic circle lies in a plane perpendicular to a
  // FIXED axis direction u (not the edge direction e itself, except in
  // the m=0 special case CylindricalFace already covers - see below),
  // centered on a FIXED line through that same axis, with radius growing
  // LINEARLY from zero at a single apex point A - i.e., a genuine right
  // circular cone, verified directly (not merely asserted) against the
  // standard formula: both the circle's distance-from-apex-along-axis
  // and its own radius are exactly linear in t with a COMMON zero at
  // apex parameter t* = -r0/m, so their ratio (tan of the cone's own
  // half-angle) is a t-independent constant.
  //
  // A subtlety the m=0 (CylindricalFace) case has no analogue of: the
  // rolling ball's own radius r(t) is NOT the same number as the cone's
  // own true cross-sectional radius at the corresponding point - they
  // differ by a fixed scale factor c = sqrt(1 - (m/|u|)^2) (the same
  // "does the characteristic circle degenerate" factor the general
  // canal-surface formula always carries, here a genuine constant since
  // the spine is straight and the radius law is linear), because the
  // ball's own CENTER does not sit on the cone's own axis whenever m!=0
  // (only the axis-projected point of each characteristic circle does).
  // `radius0`/`radius1` below are that TRUE cone cross-section radius
  // (r(t)*c at the patch's own two ends), not the raw rolling-ball
  // radius FilletConvexEdgeTapered's own radius0/radius1 PARAMETERS
  // name - the same names are reused for both because they play the
  // same "radius at this end of the patch" role, but they are genuinely
  // different numbers whenever the taper is nonzero; this struct always
  // stores the former (what OpenNURBS' own ON_Cone construction needs).
  //
  // `frame.origin` is the cone's own APEX A - a point strictly outside
  // the trimmed patch whenever both radii are positive (the physical
  // radius extrapolates to exactly zero there, i.e. the rolling ball
  // shrinks to a point sitting exactly ON the original sharp edge's own
  // infinite line - a genuinely checked, not assumed, consequence of the
  // tangency construction, since the bisector-offset term in the ball's
  // own center formula is proportional to r(t) and so vanishes exactly
  // where r(t) does). `frame.zaxis` is the cone's own unit axis
  // direction u/|u| (NOT generally parallel to the edge direction e once
  // m!=0 - see FilletConvexEdgeTapered's own doc comment for why this,
  // not e, is what a THIRD face perpendicular to e can no longer share
  // this patch's own end arc with, exactly, without going through a
  // separate elliptical-cross-section generalization v1 does not
  // attempt). `frame.xaxis` is the angle-0 reference direction, playing
  // the same role CylindricalFace::frame.xaxis does. `radius0`/`radius1`
  // are the TRUE cone cross-section radii (see above) at the patch's own
  // two ends; `length` is the TRUE axial distance between those two ends
  // measured along `frame.zaxis` from wherever the patch actually starts
  // (NOT from the apex - the apex itself sits `radius0/tan(half_angle)`
  // further back along `frame.zaxis`, a value fully recoverable from
  // radius0/radius1/length alone via similar triangles, so no separate
  // "distance from apex to patch start" field is needed: tan(half_angle)
  // = (radius1-radius0)/length, and the apex-to-start distance is
  // radius0/tan(half_angle)). `angle`/`outward` play the same role
  // CylindricalFace's own fields do.
  struct ConicalFace {
    ON_Plane frame;
    double radius0 = 0.0;
    double radius1 = 0.0;
    double angle = 0.0;
    double length = 0.0;
    bool outward = true;

    // Optional, narrow extension mirroring PlanarFace::notch_begin/
    // notch_count (see its own doc comment) but for THIS face's own end
    // cap: dense, ordered sample points of the TRUE boundary curve where a
    // third face's own cutting plane intersects this cone - generally an
    // ELLIPSE once the fillet is tapered (the intersection of a plane with
    // a cone, when the plane is not perpendicular to the cone's own axis -
    // see FilletConvexEdgeTapered's own doc comment for the closed-form
    // h(phi) derivation), not the fixed-height CIRCLE this patch's own
    // angle-swept v=v0 (or v=v1) edge otherwise traces. Empty (the
    // default) means "this end is the plain natural circular cap, no
    // notch" - every existing caller/producer of ConicalFace (including
    // every call this kernel itself ever made before this field existed)
    // is completely unaffected.
    //
    // `cap0_notch_points` is for the v0 (near-apex) end, `cap1_notch_points`
    // for the v1 (far) end. When set, a field's own first and last points
    // MUST be exactly the same two points this patch's own rail corners
    // already are at that end (angle 0 and angle `angle` respectively, at
    // v0 for cap0 / v1 for cap1) - FromMixedFaces() checks this directly
    // rather than trusting it blindly. Every point is ordered by
    // INCREASING angle (0 -> `angle`, the same angle=0-at-frame.xaxis
    // convention every other angle on this struct uses) for BOTH fields -
    // a single fixed convention regardless of which end, NOT reversed to
    // match the raw trim loop's own v1-side u_max->0 walk direction, which
    // FromMixedFaces() itself accounts for internally.
    //
    // FromMixedFaces() gives the notched end's whole run ONE literal
    // shared ON_BrepEdge with whichever adjacent PlanarFace's own matching
    // corner-notch reaches the same two welded endpoint vertices - the
    // exact same "curved face visited first, builds the true boundary
    // curve; the second visitor reuses it" two-pass mechanism already
    // documented on this class' own top-level comment and on
    // PlanarFace::notch_begin/notch_count, just applied here to a curved
    // face's own cap instead of only ever being the FIRST visitor for a
    // straight-line rail or an exact circular cap. Unlike the circular
    // case (where the shared edge's own 3D curve is the exact isocurve),
    // there is no simple isocurve family for a general ellipse in this
    // surface's own (u, v) domain, so the shared edge here is instead a
    // genuine polyline through these same dense points - the one place a
    // notched ConicalFace's own boundary isn't exact to floating-point
    // precision by construction of the representation, matching (not
    // exceeding) the same disclosed tradeoff PlanarFace's own notch
    // machinery already accepts for the circular case's PLANAR side, now
    // also true of the curved side here (see `cap0_notch_tolerance`/
    // `cap1_notch_tolerance` below for the genuinely computed, not
    // guessed, bound on that approximation).
    //
    // Also changes this struct's own contract: once an end is notched,
    // `radius0`/`radius1` describe the TRUE cone cross-section radius only
    // at that end's own two RAIL corners (where the notch curve is
    // anchored), NOT uniformly across that whole end's boundary anymore -
    // every other point of the notch curve generally sits at a different
    // true height-from-apex than v0/v1 (see FilletConvexEdgeTapered's own
    // doc comment: h(phi) == v0 exactly only at the two corners, by proof,
    // not merely by construction - everywhere strictly between them it
    // differs, which is exactly why this notch machinery is needed instead
    // of just reusing radius0/radius1 unchanged).
    //
    // A gap this USED to imply, now closed: `Brep::MixedFaces()`'s own
    // cone-recovery (ExtractConicalFace, brep.cpp) previously derived a
    // notched end's own v_min/v_max from a plain global min/max over every
    // point of the dense visible trim polygon - which no longer coincides
    // with that end's true v0/v1 once the notch dips the boundary toward
    // the apex between the two rail corners, since the scan picked up the
    // dip instead. That silently returned a WRONG radius0/radius1 for the
    // notched end(s) (confirmed, not theorized: for a fully closed box
    // filleted at a corner where BOTH ends get notched, radius0 came back
    // off by roughly 2% and radius1 by roughly 1%, not just one end as an
    // earlier version of this comment claimed - a single notched vertex is
    // enough to corrupt that end's own radius; the other end is only
    // "correct" when it genuinely has no notch of its own).
    //
    // Fixed by restricting ExtractConicalFace()'s v_min/v_max scan to only
    // the trim polygon's points that sit exactly at the trim's own u_min or
    // u_max: FromMixedFaces() never subdivides the two straight rail
    // segments (u == 0 and u == u_max in its own trim rectangle, above),
    // and a notch's own interior samples always have u strictly between
    // u_min and u_max (EllipseNotchCornerAtVertex's monotone-in-phi
    // sampling, fillet.cpp) - so those two u-values are always, and only,
    // the genuine rail corners regardless of whether either cap is
    // notched. This depends only on the trim curve's own (u, v) values, so
    // it works identically whether MixedFaces() reads a face straight out
    // of this Brep's own side table or from a genuinely resolved
    // ON_Brep loop (e.g. after a .3dm round trip) - see MixedFacesResult's
    // own doc comment below for the details of what's now recovered
    // exactly and what (round-tripping the notch's own dense polyline
    // shape back into cap0_notch_points/cap1_notch_points) remains
    // out of scope.
    std::vector<Point3d> cap0_notch_points;
    std::vector<Point3d> cap1_notch_points;

    // Genuine, directly-computed sagitta-style upper bound on the notch
    // polyline's own deviation from the TRUE continuous curve it
    // approximates (see EllipseNotchCornerAtVertex in fillet.cpp for how
    // this is actually measured - the max distance, over every sample
    // segment, between that segment's own straight-line midpoint and the
    // true curve's own point at the matching angle) - used as the shared
    // edge's own `m_tolerance` instead of the 0.0 every other edge
    // FromMixedFaces() builds gets (see BuildFaceLoop's own comment for
    // why 0.0 is an honest claim everywhere else but not here). Meaningless
    // (left at its default 0.0) when the corresponding cap*_notch_points
    // field is empty.
    double cap0_notch_tolerance = 0.0;
    double cap1_notch_tolerance = 0.0;
  };

  // The general sibling of PlanarFaces() that also recognizes a
  // cylindrical face rather than throwing on it - the extraction half of
  // what BooleanCombineMixed (see boolean.h) needs to get a
  // CylindricalFace back OUT of an arbitrary Brep (PlanarFaces() itself
  // deliberately can't - see its own doc comment). A planar face is
  // extracted exactly as PlanarFaces() does (same code, not a second
  // copy); a non-planar face is checked via `raw().IsCylinder(&cyl, tol)`
  // - the same real OpenNURBS API dino8-app's own
  // BuildPlaneCylinderVariableFillet (cmd_fillet.cpp) already calls to
  // recognize a cylindrical face - and, if that succeeds, its
  // frame/radius/angle/length are recovered as the direct inverse of what
  // FromMixedFaces() built:
  //   - `radius` is ON_Cylinder's own fitted circle radius.
  //   - `frame.zaxis` is ON_Cylinder::Axis(); `frame.origin` is found by
  //     evaluating the REAL surface (not trusting whatever arbitrary
  //     reference direction IsCylinder()'s own internal curve-fit happens
  //     to pick for its returned circle) at the face's own trim
  //     rectangle's (u_min, v_min) corner and projecting that point onto
  //     the fitted axis LINE - this is what makes the extraction correct
  //     for ANY trim rectangle's own u_min (not just one that happens to
  //     start at the raw surface's own u=0), a real generalization beyond
  //     the narrower assumption this method could have gotten away with,
  //     given every CylindricalFace this kernel itself ever builds via
  //     FromMixedFaces does start its own trim at u=0.
  //   - `frame.xaxis` is the unit vector from that same projected point to
  //     the actual corner point - i.e., exactly the patch's own rail at
  //     the trim's own u_min, matching CylindricalFace's own doc comment
  //     ("frame.xaxis is exactly the patch's own rail at angle 0") with
  //     "angle 0" now meaning this face's own u_min rather than
  //     necessarily the underlying surface's u=0.
  //   - `length` is the trim rectangle's true v-extent (v_max - v_min,
  //     already true axial distance per FromMixedFaces' own comment).
  //   - `angle` is the true radian sweep between the trim's own u_min and
  //     u_max, via ON_Circle::GetRadianFromNurbFormParameter (the
  //     documented inverse of GetNurbFormParameterFromRadian
  //     FromMixedFaces uses to go the other way) - valid to call on the
  //     FITTED circle even though that circle's own xaxis has no relation
  //     to this face's own frame.xaxis above, since that conversion is
  //     intrinsic to the standard 4-span rational NURBS circle's own
  //     canonical parameterization, not to any particular circle
  //     instance's plane.
  //
  // A non-planar, non-cylindrical face is next checked via
  // `raw().IsCone(&cone, tol)` (the direct sibling of the IsCylinder()
  // check above, mirrored the same way ON_Surface::IsCone mirrors
  // ON_Surface::IsCylinder - both confirmed present in the vendored
  // OpenNURBS source, not merely assumed from the spec text this method
  // was built from) and, if that succeeds, recovered as the inverse of
  // what FromMixedFaces() built for a ConicalFace:
  //   - `frame.zaxis` is ON_Cone::Axis(); `frame.origin` is the fitted
  //     cone's own ApexPoint() directly (unlike CylindricalFace's own
  //     axis-projection recovery, a cone's apex is already a single,
  //     unambiguous point - no projection needed).
  //   - `frame.xaxis` is recovered the same way CylindricalFace's own
  //     frame.xaxis is: evaluate the REAL surface at the trim rectangle's
  //     own (u_min, v_min) corner, then take the unit vector from that
  //     corner's own axis-projected point (not the apex) to the corner
  //     itself - i.e. exactly the patch's own rail at the trim's own
  //     u_min, correct for any trim rectangle's own u_min the same way
  //     CylindricalFace's own recovery is.
  //   - `v_min`/`v_max` (and hence the corner points `radius0`/`radius1`
  //     below are read from) are NOT a plain global min/max over every
  //     point of the trim polygon - that would also sweep in a notched
  //     cap's own dense ellipse splice (ConicalFace::cap0_notch_points/
  //     cap1_notch_points' own doc comment), whose interior samples sit at
  //     a true height-from-apex strictly different from v0/v1 by
  //     construction. Restricted instead to only the points that sit
  //     exactly at the trim's own u_min or u_max: FromMixedFaces() never
  //     subdivides the two straight rail segments, and a notch's own
  //     interior samples always have u strictly between u_min and u_max
  //     (EllipseNotchCornerAtVertex's monotone-in-phi sampling,
  //     fillet.cpp), so this recovers the true rail-corner v-range exactly
  //     whether or not either cap is notched.
  //   - `radius0`/`radius1` are recovered from the corner point (and the
  //     analogous far corner at v_max) via the SAME apex-relative
  //     similar-triangles relationship FromMixedFaces() itself uses to go
  //     the other way (radius = tan(half_angle) * distance-from-apex, a
  //     direct read of ON_Cone::PointAt's own construction, not an
  //     independent formula) - `length` is the true axial distance
  //     between those two corners' own axis-projected points. Exact for a
  //     notched end too, now that v_min/v_max above are (see
  //     TestMixedFacesRoundTripsNotchedConicalFace in test_basic.cpp for
  //     the falsifiable check). What is NOT recovered: the notch's own
  //     dense polyline shape itself - MixedFacesResult's ConicalFace comes
  //     back with cap0_notch_points/cap1_notch_points empty regardless of
  //     whether the source face was notched, so a full round trip through
  //     FromMixedFaces() would rebuild that end as a plain flat circular
  //     cap at the (now-exact) radius0/radius1, not the original ellipse
  //     boundary - a real, disclosed, still out-of-scope gap, narrower
  //     than the one this fix closes (frame/radius0/radius1/length/angle
  //     are all exact; only the notch geometry itself doesn't round-trip).
  //   - `angle` is recovered exactly as CylindricalFace's own `angle` is:
  //     the true radian sweep between the trim's own u_min/u_max via
  //     ON_Circle::GetRadianFromNurbFormParameter, confirmed directly
  //     (not assumed) to apply unchanged to a cone's own NURBS
  //     parameterization - ON_Cone::GetNurbForm builds its own u-knots by
  //     literally copying an ON_Circle::GetNurbForm curve's own knots
  //     (verified against the vendored opennurbs_cone.cpp source), so a
  //     cone's u-parameter is the exact same non-radian NURBS-circle
  //     parameterization a cylinder's is, needing the exact same
  //     angle<->parameter conversion, not a different one.
  //
  // Throws std::invalid_argument if a face is neither planar, cylindrical,
  // nor conical (a genuinely free-form face - out of scope, the same
  // honest narrowing PlanarFaces() uses for a non-planar face) or
  // std::runtime_error if a trim corner's recovered geometry doesn't
  // match the fitted primitive within tolerance (should not happen for a
  // face this kernel itself built, but is checked rather than silently
  // returning a wrong frame).
  struct MixedFacesResult {
    std::vector<PlanarFace> planar;
    std::vector<CylindricalFace> cylindrical;
    std::vector<ConicalFace> conical;
  };
  MixedFacesResult MixedFaces() const;

  // The inverse of PlanarFaces(), generalized to also place curved
  // circular-cylinder patches alongside the planar ones (PlanarFaces()
  // itself has no CylindricalFace counterpart to extract them back out
  // of, since it's deliberately planar-only - see its own doc comment).
  // Every PlanarFace becomes exactly what FromPlanarFaces() below already
  // built for it (a bilinear NurbsSurface spanning that polygon's own
  // bounding rectangle, trimmed to the polygon). Every CylindricalFace
  // becomes an exact rational-NURBS patch via ON_Cylinder(ON_Circle(
  // frame, radius), length).GetNurbForm() - the same exactness argument
  // Sphere() already relies on for ON_Sphere::GetNurbForm, generalized
  // from a whole untrimmed sphere to a trimmed cylindrical rectangle -
  // added via NewFace and trimmed to the sub-rectangle of the cylinder's
  // own (angle, height) domain covering [0, length] of height and the
  // true angles [0, angle] of sweep. Because a rational NURBS circle's
  // own curve parameter does NOT match true radian angle except at its
  // four quadrant knots (ON_Circle::GetNurbForm's own doc comment is
  // explicit about this - "the parameterization of NURBS curve does not
  // match circle's transcendental parameterization"), the trim boundary
  // at true angle `angle` is located via ON_Circle::
  // GetNurbFormParameterFromRadian(angle, ...) rather than by using
  // `angle` as a raw parameter value directly - the same real conversion
  // ON_Circle's own header points callers at, not an approximation of it.
  // Every `loop`/`frame` must satisfy the same preconditions PlanarFace's
  // and CylindricalFace's own doc comments describe; not re-validated
  // beyond what NewFace's own surface construction requires.
  //
  // Every ConicalFace becomes an exact rational-NURBS patch the same way,
  // via ON_Cone(ON_Plane(frame.origin, ..., frame.zaxis), height, radius)
  // .GetNurbForm() for a (height, radius) reference point chosen (from
  // whichever of the patch's own two ends is farther from the apex) so
  // the cone's own natural [0, height] (or [height, 0], per
  // ON_Cone::GetNurbForm's own sign convention for a negative height -
  // confirmed directly against the vendored source, not assumed) v-domain
  // fully contains both of the patch's own true end heights - trimmed to
  // the sub-rectangle of true angle [0, angle] (same
  // GetNurbFormParameterFromRadian conversion as a CylindricalFace, see
  // MixedFaces()'s own doc comment for why a cone needs the identical
  // correction) and true axial height [v0, v1] (NOT [0, length] - a
  // cone's own frame.origin is its apex, not the patch's own start, so
  // this sub-range is generally offset from the surface's own v=0,
  // unlike a CylindricalFace's own [0, length]).
  //
  // FromPlanarFaces(faces) is exactly FromMixedFaces(faces, {}, {}).
  static Brep FromMixedFaces(const std::vector<PlanarFace>& faces,
                              const std::vector<CylindricalFace>& cylindrical_faces,
                              const std::vector<ConicalFace>& conical_faces = {});

  // The inverse of PlanarFaces(): builds a new Brep with one
  // TrimmedPlanarFace()-equivalent face per PlanarFace, each an exact
  // bilinear NurbsSurface spanning that face's own polygon's bounding
  // rectangle in the plane's local (x, y) axes, trimmed to the polygon
  // itself. Every `loop` must have at least 3 points and lie exactly in
  // its own `plane` (within a small tolerance) - not re-validated here
  // beyond what TrimmedPlanarFace() itself checks (throws
  // std::invalid_argument on too few points). Equivalent to
  // FromMixedFaces(faces, {}) - kept as its own entry point since it's
  // the overwhelmingly common case and needs no CylindricalFace argument.
  static Brep FromPlanarFaces(const std::vector<PlanarFace>& faces);

  // Bounding box over the Brep's actual curved geometry, not just its
  // control points - a real gap nothing here could answer without
  // tessellating first (Mesh::GetBoundingBox() only sees a tessellation's
  // sampled vertices, an approximation of the true surface). Delegates to
  // ON_Brep::GetTightBoundingBox, which despite its name is NOT a
  // genuine tight/exact bound in the public OpenNURBS build for a face
  // whose true extremum lies strictly inside its parameter domain (it
  // only samples each face's boundary/Greville-abscissa isocurves and
  // control points, never searches the true 2D interior - verified by
  // testing: a doubly-curved bicubic bulge whose true peak is at its
  // center comes back overshot, at exactly half the peak control point's
  // height above its neighbors instead of the analytically exact value).
  // Still always a valid, safe bound (it can overshoot, never exclude
  // part of the surface) - exact for Box() (flat faces) and, more subtly,
  // Sphere() (the extrema of a standard rational-NURBS sphere's meridian
  // circles coincide exactly with points its isocurve sampling actually
  // evaluates, not because the underlying algorithm does a real 3D
  // extremum search). Throws std::runtime_error if OpenNURBS' own call
  // fails (e.g. a face with an invalid surface).
  BoundingBox GetTightBoundingBox() const;

  // Tessellates each face into a triangle mesh via NurbsSurface's grid
  // tessellator (see its comment for why this doesn't go through
  // OpenNURBS' own CreateMesh). One Mesh per face, in face order.
  // `u_divisions`/`v_divisions` apply to every face's own parameter
  // domain. Faces built by TrimmedPlanarFace() are tessellated against
  // their trim loop; every other face here is untrimmed.
  std::vector<Mesh> Tessellate(int u_divisions = 8, int v_divisions = 8) const;

  // Tessellate() followed by Mesh::MergeAndWeld() - the combination that
  // actually produces a single closed, boolean-ready mesh from a closed
  // Brep like Box(). Tessellate() alone leaves each face's tessellation
  // as a separate mesh with its own copy of shared-edge vertices; this
  // is what welds those seams shut.
  Mesh TessellateToClosedMesh(int u_divisions = 8, int v_divisions = 8) const;

  // Tessellate(), but picking each face's own u_divisions/v_divisions
  // via NurbsSurface::SuggestedDivisions(chord_tolerance) instead of one
  // fixed division count shared by every face - the Brep-level endpoint
  // of this kernel's curvature-based tessellation building blocks
  // (NurbsSurface::CurvatureAt/SuggestedDivisions/
  // TessellateGridAdaptive/TessellateGridClippedExactAdaptive). Each
  // face is tessellated at whatever resolution *that face's own
  // geometry* needs to hit `chord_tolerance` - a flat face and a tightly
  // curved face in the same Brep (e.g. Box() vs Sphere()) get
  // independently appropriate divisions, not the one-size-fits-all
  // count Tessellate() requires the caller to pick by hand. Still not a
  // per-region adaptive mesher within a single face (see
  // SuggestedDivisions()'s own doc comment).
  std::vector<Mesh> TessellateAdaptive(double chord_tolerance) const;

  // TessellateAdaptive() followed by Mesh::MergeAndWeld() - the
  // adaptive counterpart to TessellateToClosedMesh().
  Mesh TessellateToClosedMeshAdaptive(double chord_tolerance) const;

  // TessellateAdaptive(), but using NurbsSurface::
  // TessellateGridNonUniformAdaptive() per face instead of the uniform
  // TessellateGridAdaptive() - the Brep-level endpoint of this kernel's
  // genuine per-region tessellation building blocks (NurbsCurve::
  // SuggestedParameterValues/NurbsSurface::TessellateGridNonUniform/
  // SuggestedParameterValues/TessellateGridNonUniformAdaptive). Each
  // untrimmed or whole-cell-trimmed face gets independently
  // non-uniform, curvature-adaptive breakpoints in both directions
  // (denser only where that direction of that face actually bends more,
  // not a single resolution smeared across it - the gap
  // `TessellateAdaptive()`'s own doc comment still flags). A face built
  // with `exact_clip=true` has no non-uniform exact-clip tessellator yet
  // (`TessellateGridClippedExactAdaptive()`'s own uniform-grid algorithm
  // doesn't generalize to arbitrary breakpoints the way the whole-cell
  // path does), so those faces still fall back to the uniform
  // `TessellateGridClippedExactAdaptive()` - a real, narrower scope for
  // this method, not silently ignored.
  std::vector<Mesh> TessellateNonUniformAdaptive(double chord_tolerance) const;

  // TessellateNonUniformAdaptive() followed by Mesh::MergeAndWeld().
  Mesh TessellateToClosedMeshNonUniformAdaptive(double chord_tolerance) const;

  // A purely additive, opt-in sibling of Tessellate() that closes
  // BooleanCombineMixed's own disclosed mesh-watertightness gap (see
  // detail::ClipPolygonByCircle3d's own doc comment, and
  // TestBooleanCombineMixedDrilledBoxThroughHole's own comment in this
  // kernel's test file) WITHOUT touching Tessellate() itself, or any of
  // the boolean-classification-critical code
  // (detail::ClipPolygonByCircle3d, SplitMixedAgainstAllFaces,
  // ClassifyPointVsMixedSolid) that gap's own prior, reverted fix attempt
  // corrupted. Every existing caller of Tessellate()/
  // TessellateToClosedMesh() (and every other Tessellate*() variant) is
  // completely unaffected - this is a new, separate entry point, not a
  // new code path inside an existing one.
  //
  // For a face i with a non-empty PlanarFace::arc_runs entry (populated
  // ONLY by BooleanCombineMixed's own SplitMixedAgainstAllFaces case
  // (ii), i.e. a "wedge" cap fragment left over from punching a
  // perpendicular cylindrical hole through a planar face - see
  // PlanarFace::ArcRun's own doc comment) whose run's (center, radius,
  // plane-normal) matches some other face j's own CylindricalFace
  // geometry (recovered the same way MixedFaces() already recovers one,
  // within `tol` - see this class' own MixedFaces() doc comment) at one
  // of j's own two ends (v=0 or v=length): this method computes
  // `boundary_samples + 1` shared boundary points ONCE, via
  // detail::ArcSchedule3d() using the wedge's OWN angle_begin/angle_end/
  // plane_xaxis/plane_yaxis (not a second, independently-built basis),
  // and uses THAT SAME array of points - literally, not a second
  // independently-evaluated approximation of them - as BOTH: (a) the
  // wedge face's own tessellated boundary vertices along that run
  // (substituted in place of the run's own dense original samples,
  // triangulated via the existing, proven, boundary-only
  // detail::EarClipTriangulate), and (b) the cylindrical face's own
  // tessellated boundary vertices along the matching row of its own
  // (u, v) grid (the row's other, non-boundary breakpoints still come
  // from evaluating that face's real NURBS surface, exactly as
  // Tessellate() already does - only the shared-boundary row's vertices
  // are substituted). Converting the wedge's own angle values into the
  // cylindrical face's own frame uses detail::ConvertAngleBetweenFrames()
  // - the one isolated, independently unit-tested frame-to-frame
  // conversion this whole path needs (see detail/arc_schedule3d.h's own
  // top comment for why it is kept this narrow and this separate from
  // both this method and boolean.cpp).
  //
  // Because both sides' shared-boundary vertices are the exact same
  // Point3d values - not merely close, to within some tolerance - the
  // two faces' own tessellations share that boundary EXACTLY, closing
  // the gap Tessellate() cannot: two independently-parameterized
  // exact-clip faces (today) only ever share the small number of
  // explicit trim/corner vertices, never the dense interior samples a
  // curved shared boundary needs for genuine watertightness.
  //
  // A face with no matching arc_run/CylindricalFace pair (the
  // overwhelming majority of any Brep - every untouched side wall, every
  // Box(), every Sphere(), ...) falls through to EXACTLY today's
  // Tessellate() behavior for that face, at the same u_divisions/
  // v_divisions - this method's own new machinery is reached only for
  // the specific wedge-cap-vs-cylinder-wall pairing it targets, per
  // boolean.h's own BooleanCombineMixed doc comment.
  //
  // `boundary_samples`, if -1 (the default), is max(u_divisions,
  // v_divisions) - dense enough that a caller asking for a fine grid on
  // either axis also gets a fine shared boundary, without having to
  // reason about the two separately. A caller may pass an explicit value
  // instead (e.g. to deliberately under- or over-sample the shared
  // boundary relative to the rest of the grid).
  //
  // SECOND, separate matching pass, added after the arc-matching pass
  // above and targeting a DIFFERENT gap: TestBooleanCombineMixedDrilledBoxThroughHole's
  // own comment used to disclose that, even with the arc seam above
  // closed, the untouched side walls' own shared STRAIGHT edge with each
  // wedge cap was still open - both faces are ordinary, independently-
  // parameterized PLANAR patches there (no circular parameterization, no
  // handedness/angle-offset issue, just two straight-edge grids that
  // don't happen to land on the same sample points), a structurally
  // simpler but genuinely SEPARATE problem from the curved one above (it
  // needed no detail::ArcSchedule3d/ConvertAngleBetweenFrames-style
  // machinery, just plain linear interpolation of the wedge's own
  // already-evaluated corner points). For every wedge PlanarFace's own
  // straight (non-arc) trim-loop segment that lies exactly along one edge
  // of some OTHER resolved planar face whose own visible boundary is a
  // plain 4-corner quadrilateral (an untrimmed face, or - what
  // FromMixedFaces() actually builds even for an untouched input face -
  // an explicit 4-point trim; see this method's own implementation
  // comments for why matching directly against those 4 corners in 3D,
  // rather than either face's own real surface u/v domain, is needed: a
  // Box() wall's own real u/v assignment to physical x/y/z is NOT the
  // same for every wall, confirmed directly - Box()'s own front and back
  // walls assign x/z to u/v oppositely), this method computes the shared
  // boundary points ONCE via plain linear interpolation between the
  // wedge's own two segment endpoints, and uses that SAME array - again
  // literally, not a second independently-evaluated approximation -
  // as BOTH the wedge's own substituted boundary there AND the plain
  // quad face's own forced tensor-grid row/column at the matching
  // position, exactly mirroring the arc pass's own "share the literal
  // points" mechanism. A plain quad face with at least one such match is
  // tessellated via a dedicated bilinear-interpolation grid builder
  // (never through the real NURBS surface at all for that face - see
  // this method's own implementation for why bilinear interpolation of
  // the same 4 corners is the exact same physical shape to floating-
  // point precision for a genuinely planar quadrilateral, while making
  // literal forced-point injection tractable); a plain quad face with no
  // match falls through to exactly today's behavior, unaffected.
  //
  // Together, the two passes above give BooleanCombineMixed's own
  // drilled-box case a genuinely complete Mesh::IsClosedManifold() result
  // via TessellateToClosedMeshConforming() at any SYMMETRIC u_divisions/
  // v_divisions - see TestBooleanCombineMixedDrilledBoxThroughHole's own
  // comment for the exact claim.
  //
  // THIRD, separate matching pass, added after the two above and closing
  // the one gap they leave open: an unequal u_divisions/v_divisions pair
  // used to leave THIS method's OWN quad-vs-quad case (two adjacent plain
  // quads, NEITHER one a wedge - e.g. two untouched Box() wall faces the
  // drilling hole never reaches, or every edge of a plain, undrilled
  // Brep::Box(), which has zero wedges/cylinders anywhere to seed either
  // pass above) with an open shared edge, for the exact same "which
  // physical axis is u vs v differs per face" reason
  // ComputePlainQuadSeamForces's own doc comment (brep.cpp) discloses for
  // Tessellate()'s own, structurally identical fix. Closed here by
  // reusing that SAME machinery - CollectPlainQuadFaces() and
  // ComputePlainQuadSeamForces(), both already living in brep.cpp's own
  // anonymous namespace - directly, not a second, hand-adapted
  // reimplementation: every resolved face that is NOT already a key of
  // the arc-matching pass's `cyl_matches` or `wedge_subs` above (the
  // identical "not a wedge, not a matched cylinder" criterion the
  // straight-edge pass's own `quad_faces` collection already applies) is
  // collected, matched pairwise by shared 3D edge exactly as
  // Tessellate()'s own pass does, and the resulting forced points are
  // merged ADDITIVELY into the same `plain_forces` map the straight-edge
  // pass above already populates - filling only the edge slots that pass
  // left empty, never overwriting one it already claimed, so a face that
  // is both a straight-edge-pass target AND a quad-vs-quad-pass target
  // (its other, still-unclaimed edges) gets both sets of forces without
  // either pass corrupting the other's work. Gated on u_divisions !=
  // v_divisions for the identical reason Tessellate()'s own pass is (see
  // that method's own doc comment): a full structural bypass, not a
  // heuristic - when the two counts are equal every edge's natural
  // sample count already agrees on both sides, so this whole pass
  // computes and changes nothing, leaving every existing
  // TessellateConforming()/TessellateToClosedMeshConforming() caller in
  // this kernel's own test file (every one of which uses symmetric
  // divisions) bit-for-bit unaffected (verified directly: the full
  // ordered list of pre-existing test checks is byte-identical before and
  // after this pass was added, and
  // TestTessellateConformingSymmetricDivisionsUnaffectedByQuadQuadFix
  // gives that same claim its own in-file, falsifiable check).
  //
  // The result: Brep::Box().TessellateToClosedMeshConforming(u, v) (and
  // BooleanCombineMixed's own drilled-box case) is now a genuine, complete
  // Mesh::IsClosedManifold() at ANY u_divisions/v_divisions pair, not only
  // a symmetric one - see
  // TestTessellateConformingQuadQuadSeamPlainBoxIsClosedManifold and
  // TestTessellateConformingQuadQuadSeamDrilledBoxIsClosedManifold for the
  // exact falsifiable claims (the latter also re-confirms, at an
  // asymmetric pair, that this pass leaves the two pre-existing passes'
  // own wedge-arc/wedge-straight-edge seams exactly as closed as they
  // already were - see CountNonPerimeterBoundaryEdges's own doc comment
  // in this kernel's test file).
  //
  // Deliberately narrow in scope beyond that (see boolean.h's own
  // BooleanCombineMixed doc comment and this method's own implementation
  // comments for the exact matching rules): the arc pass targets exactly
  // the wedge-cap-vs-cylindrical-wall shared arc boundary
  // BooleanCombineMixed's own drilled-hole case produces; the straight-
  // edge pass generalizes one step further (ANY trimmed planar face's own
  // straight boundary segment against ANY plain-quad planar face, not
  // hardcoded to Box() walls specifically), but its SOURCE side still
  // only ever walks a wedge PlanarFace's own trim loop (i.e. a face with
  // a recorded PlanarFace::arc_runs entry); the quad-vs-quad pass
  // generalizes one step further still - NEITHER side needs to be a
  // wedge - but still only ever matches a "plain quad" face in
  // CollectPlainQuadFaces's own sense (planar, no holes, exactly 4
  // corners - explicit or the implicit domain rectangle - and
  // axis-aligned in its own (u, v) domain; see that function's own doc
  // comment in brep.cpp for the real, pre-existing, separate gap this
  // deliberately leaves for a face shaped otherwise, e.g. one quad face of
  // a hulled, arbitrarily-rotated box). A genuinely arbitrary pair of
  // adjacent PlanarFaces, neither one shaped like a plain quad, is still
  // not attempted by any of the three passes (e.g. ShellConvexPlanar's own
  // separately-disclosed planar/planar grid-mismatch note remains a real,
  // separate follow-up none of them attempt).
  std::vector<Mesh> TessellateConforming(int u_divisions = 8, int v_divisions = 8,
                                          int boundary_samples = -1) const;

  // TessellateConforming() followed by Mesh::MergeAndWeld() - mirrors
  // TessellateToClosedMesh()'s own composition over Tessellate().
  Mesh TessellateToClosedMeshConforming(int u_divisions = 8, int v_divisions = 8,
                                         int boundary_samples = -1) const;

  const ON_Brep& raw() const { return brep_; }
  ON_Brep& raw() { return brep_; }

 private:
  ON_Brep brep_;
  // Parallel to brep_.m_F: face_trim_loops_[i] is empty for an untrimmed
  // face, or the trim polygon for a face built by TrimmedPlanarFace().
  // Every face-adding factory must keep this in lockstep with brep_.m_F.
  std::vector<std::vector<Point2d>> face_trim_loops_;
  // Parallel to face_trim_loops_: whether that face's trim should be
  // tessellated via exact convex clipping rather than whole-cell in/out.
  // Meaningless (always false) for an empty trim loop.
  std::vector<bool> face_exact_clip_;
  // Parallel to face_trim_loops_: hole polygons for that face (empty for
  // every face except one built by TrimmedPlanarFace() with
  // hole_loops_uv). Meaningless for an empty trim loop.
  std::vector<std::vector<std::vector<Point2d>>> face_hole_loops_;
  // Parallel to face_trim_loops_: that face's own PlanarFace::arc_runs,
  // carried through unchanged from FromMixedFaces()'s own `faces`
  // argument (see PlanarFace::ArcRun's own doc comment) - empty for
  // every face except a planar one whose input PlanarFace had a non-empty
  // arc_runs. Empty (never populated) for every cylindrical/conical
  // face, and for every face built by any OTHER factory here (Box(),
  // Sphere(), FromSurface(), TrimmedPlanarFace()) - consumed ONLY by
  // TessellateConforming().
  std::vector<std::vector<PlanarFace::ArcRun>> face_arc_runs_;
};

}  // namespace dino8::kernel
