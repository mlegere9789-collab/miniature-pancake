#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/curve.h"
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
    // Further notch runs on the SAME face, with exactly the semantics of
    // notch_begin/notch_count above (begin index, point count, no
    // wraparound, each run an adjacent CylindricalFace's own true cap
    // arc). A face meeting several filleted edges - a box end face whose
    // two top corners are both rounded by FilletConvexEdges (fillet.h),
    // or two parallel FilletConvexEdge calls in sequence - needs one run
    // per notched corner; before this field existed a second notch simply
    // overwrote the first's notch_begin/notch_count, leaving that earlier
    // corner as ~200 unshared micro-edges (a free boundary in
    // IsManifold()). FromMixedFaces() treats {notch_begin, notch_count}
    // (when notch_count > 1) plus every entry here as one set of
    // non-overlapping runs and collapses each identically. Empty (the
    // default) for every face this kernel built before the field existed,
    // so nothing already built changes.
    std::vector<std::pair<int, int>> notch_runs;

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

      // When non-empty, this run is NOT a circular arc at all but a run
      // of LITERAL shared boundary points - exactly `count` of them, one
      // per loop index in [begin, begin+count) mod loop.size(), in this
      // loop's own walk order - that TessellateConforming() substitutes
      // verbatim for those loop vertices, never re-evaluating or
      // re-sampling them; `center`/`radius`/`angle_*`/`plane_*` above are
      // then unused and left default. The one producer today is
      // BooleanCombineMixed's oblique plane+cylinder case (boolean.cpp,
      // via detail::ClipPolygonByEllipse3d's `ellipse_runs` out-param):
      // the points are the SAME detail::EllipsePointAt samples the
      // adjoining cylindrical fragment carries in its own
      // cap0_notch_points/cap1_notch_points (see CylindricalFace), so the
      // planar wedge and the cylinder wall share a bit-identical seam
      // without either side ever evaluating "the same point" twice - the
      // planar-side counterpart of the cylinder's own literal notch rows.
      // A run must be either an arc (this vector empty) or literal (this
      // vector of size `count`); FlipFace (boolean.cpp) reverses the
      // vector alongside its `begin` remap, since the points walk the
      // loop's order.
      std::vector<Point3d> literal_points;
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

    // Optional, narrow extension mirroring ConicalFace::cap0_notch_points/
    // cap1_notch_points (see that struct's own doc comment for the shared
    // machinery this reuses) but for THIS patch's own v=0 (cap0) / v=length
    // (cap1) end: dense, ordered sample points of the TRUE boundary curve
    // where an OBLIQUE (non-perpendicular-axis) planar face's own cutting
    // plane intersects this cylinder - an ELLIPSE (see
    // dino8/kernel/detail/ellipse_clip3d.h's own top comment for the
    // closed-form P(phi) derivation this uses), not the fixed-height
    // CIRCLE this patch's angle-swept v=0/v=length edge otherwise traces.
    // Empty (the default) means "this end is the plain natural circular
    // cap, no notch" - every existing caller/producer of CylindricalFace
    // (including every call this kernel itself ever made before this field
    // existed) is completely unaffected.
    //
    // `cap0_notch_points` is for the v=0 end, `cap1_notch_points` for the
    // v=length end. When set, a field's own first and last points MUST be
    // exactly the same two points this patch's own rail corners already
    // are at that end (angle 0 and angle `angle`, at v=0 for cap0 / v=
    // length for cap1) - FromMixedFaces() checks this directly rather than
    // trusting it blindly. Every point is ordered by INCREASING angle (0
    // -> `angle`, the same angle=0-at-frame.xaxis convention every other
    // angle on this struct uses) for BOTH fields, matching
    // ConicalFace's own fixed convention.
    //
    // One admitted departure from "the flat corner": the LAST point must
    // sit at angle `angle`, but its HEIGHT may differ from the flat
    // corner's (v=0 for cap0, v=length for cap1) - a SLOPED cut chain, the
    // unequal-radius cylinder/cylinder boolean's helical cut across the
    // larger cylinder's plain pieces at a general axis angle (see
    // BooleanCombineMixed's doc comment in boolean.h), whose two ends are
    // at two different heights. That rail corner is then the chain's own
    // last point: FromMixedFaces() moves the trim rectangle's angle-
    // `angle` corner at that end to the chain's height, the rail between
    // the face's two angle-`angle` corners stays a straight edge, and the
    // rail-corner check is made against the moved corner. The first point
    // is always the flat angle-0 corner (a producer anchors the piece's
    // origin or length at the chain's first height). Gated on the height
    // differing by more than the 1e-6 the rail-corner check tolerates: a
    // chain ending within 1e-6 of the flat corner keeps the exact flat
    // trim, so nothing built before sloped chains existed changes.
    //
    // A real, CHECKED (not merely assumed) simplification versus
    // ConicalFace's own doc comment: when `angle` == 2*pi EXACTLY (a
    // full-circle drilled hole/boss - the only kind of CylindricalFace
    // this kernel's own BooleanCombineMixed pipeline builds today, per
    // ClipPolygonByCircle3d's own existing test fixtures), the SAME
    // unconditional "front matches the angle-0 rail corner, back matches
    // the angle-`angle` rail corner" check ConicalFace already uses turns
    // out to need NO separate branch: at a full 2*pi sweep the two rail
    // corners themselves already coincide (ON_Cylinder::GetNurbForm's own
    // NURBS-circle parameterization evaluates to the identical point at
    // u=0 and u=u_max for a closed full-circle base curve - confirmed
    // directly, not assumed), and this field's own front/back points are
    // both EllipsePointAt(ef, 0) / EllipsePointAt(ef, 2*pi) - two
    // evaluations of the same closed-form cosine/sine that agree to
    // floating-point precision even though 0 and 2*pi are not the same
    // literal double. So the general rail-corner check ALREADY accepts
    // the full-sweep case, with no widening of its own tolerance and no
    // "is this a full circle" branch - a genuine, verified simplification
    // over what a naive port of ConicalFace's own machinery would have
    // needed, not an unexamined assumption.
    //
    // A notch's INTERIOR points are NOT required to stay inside the
    // [0, length] band the two rail corners span, and in the one case
    // this kernel itself produces they never do: an oblique cut's ellipse
    // swings both above and below the single scalar height its two rail
    // corners are anchored at (SplitCylindricalByObliquePlane, boolean.cpp,
    // anchors both children at the ellipse's own height at angle 0), so a
    // cap0 notch generally dips below v=0 and a cap1 notch rises above
    // v=length - the kept region genuinely extends past the rail band
    // there. Only the two documented endpoint constraints above (on the
    // surface, at the rail corners) are required. FromMixedFaces() widens
    // the underlying cylinder surface's own v-domain from [0, length] to
    // [min(0, lowest notch height), max(length, highest notch height)] so
    // the tessellation grid covers that whole trimmed region; since v is
    // true axial height in this parameterization, no (u, v) coordinate of
    // any point changes - the rail corners stay at v=0/v=length - only
    // the domain the grid spans. An un-notched face's domain stays exactly
    // [0, length].
    //
    // A face may be notched at BOTH ends, and `length == 0` is legal
    // exactly when it is: a Steinmetz "eye" (see BooleanCombineMixed's
    // own doc comment in boolean.h) - the region of one cylinder's wall
    // inside an equal-radius crossing cylinder, bounded below by cap0's
    // half-ellipse and above by cap1's, the two curves meeting only at the
    // face's own two rail corners (the two pinch points), which then
    // coincide pairwise (the angle-0 corners at v=0 and v=length are one
    // point, likewise the angle-`angle` corners). FromMixedFaces() builds
    // such a face's loop from the two notched cap trims alone: a RAIL
    // whose two corners weld to the same vertex is a zero-length rail and
    // gets no trim and no edge (rails only - a full-sweep face's CAP
    // legitimately self-loops between its two coincident corners and is
    // always built); the surface's v-domain is [lowest, highest] notch
    // height, per the widening above; consecutive coincident points the
    // cap splice then leaves in the visible trim are collapsed; and two
    // notched caps joining the same two vertices are kept as two distinct
    // edges, told apart by the midpoint of their own point lists (see
    // BuildFaceLoop in brep.cpp) - which is also what lets four such
    // curves between the same two pinch vertices, across two cylinders,
    // each be shared by exactly the two faces bounded by it. A length == 0
    // face with fewer than two notches has no surface to build and is
    // not a supported shape.
    //
    // The unequal-radius cylinder/cylinder boolean (again see
    // BooleanCombineMixed's doc comment) produces two more doubly-
    // notched shapes, both built by the same machinery with nothing
    // added: the larger cylinder's PLUG - the eye shape at an angle
    // 2*asin(r_b/r_a) < pi (its two notch curves are the two arcs of the
    // loop where the smaller cylinder pierces this wall) - and the
    // smaller cylinder's MIDDLE band, notched at both ends with a
    // POSITIVE length (from the lower curve's pinch height to the upper
    // curve's; its two rails are real edges between the two pinch
    // vertices on each side, and its notch curves are the halves of the
    // two curves going once around this cylinder). Every notch chain of
    // both shapes starts exactly at its face's angle-0 rail corner at the
    // same v (a producer always anchors the piece's own origin or length
    // there); the departure above - the chain's LAST point landing at a
    // height other than the flat angle-`angle` corner's - is not limited
    // to one chain. It is guaranteed level (both rail corners at the flat
    // v) for INTERSECTING axes (any angle), by that pair's own symmetry.
    // For a genuinely SKEW pair whose smaller cylinder still fully
    // pierces the larger one (BooleanCombineMixed's own doc comment), it
    // generally is NOT: on the larger cylinder's wall a slab's two pinch
    // vertices (and so a plain piece's helix, already the general-angle
    // departure) can differ in height even at a right axis angle if the
    // pair is skew, and on the smaller cylinder's wall its own upper/
    // middle/lower bands pick up the same asymmetry on their angle-`angle`
    // rail regardless of axis angle - every one of those notch chains
    // still starts at the flat angle-0 corner and only its own last point
    // moves, so no new mechanism is needed, only the expectation that a
    // notched cylinder/cylinder fragment's far rail corner is level with
    // its near one no longer holds outside the intersecting-axis case.
    std::vector<Point3d> cap0_notch_points;
    std::vector<Point3d> cap1_notch_points;

    // Genuine, directly-computed sagitta-style upper bound on the notch
    // polyline's own deviation from the true continuous ellipse it
    // approximates - the same quantity ConicalFace::cap0_notch_tolerance/
    // cap1_notch_tolerance already documents, used as the shared edge's own
    // m_tolerance the same way. Meaningless (left at its default 0.0) when
    // the corresponding cap*_notch_points field is empty.
    double cap0_notch_tolerance = 0.0;
    double cap1_notch_tolerance = 0.0;

    // True when this fragment's own v=0 (end0)/v=length (end1) end is
    // STILL the original, never-split terminus of the whole input
    // CylindricalFace this fragment ultimately descends from - false when
    // that end was instead manufactured by splitting (boolean.cpp's own
    // SplitMixedAgainstAllFaces case (iii), perpendicular or oblique).
    // Defaults to true, so every existing caller/producer of
    // CylindricalFace (every call this kernel made before these two
    // fields existed, including a fresh Brep::FromMixedFaces({}, {cf})
    // operand) is completely unaffected: a freshly-built cylinder has
    // BOTH its ends still original, exactly what `true`/`true` means.
    //
    // The one place this is actually READ: BooleanCombineMixed's own new
    // end-cap synthesis step (boolean.cpp), which needs to tell "this end
    // is a genuine, possibly-exposed terminus of the input solid" (may
    // need a synthesized Brep::PlanarFace disc to close it - see that
    // function's own doc comment) apart from "this end is a boundary
    // this SAME pipeline already cut against the other operand's own
    // surface" (already sealed by that operand's own face, needs no cap -
    // see boolean.h's own BooleanCombineMixed doc comment for the worked
    // three-case argument). SplitMixedAgainstAllFaces' own case (iii)
    // branches (both the axis-aligned real split and the oblique
    // SplitCylindricalByObliquePlane) are the only two places that ever
    // set either field to false, each marking exactly the one end its own
    // split just manufactured while leaving the other end's flag
    // inherited from the input fragment - every other branch (a
    // no-interaction pass-through, or a planar/planar or planar/
    // cylindrical case that never touches a CylindricalFace's own fields
    // at all) leaves both flags untouched.
    //
    // Round-tripped through Brep::MixedFaces() verbatim for a face this
    // kernel's own FromMixedFaces() built (the stored face record - see
    // MixedFaces()'s own doc comment), and defaulted to true/true by the
    // geometric fallback (a face read back out of raw ON_Brep topology,
    // e.g. after a .3dm reload). Neither value is what decides a
    // boolean's behaviour for a face of a CLOSED operand, though:
    // BooleanCombineMixed's own ToMixed (boolean.cpp) overrides both
    // flags to false for every cylindrical face of an operand that is
    // not a bare tube (any planar face, or any notched cylindrical face
    // - the closed-operand rule in boolean.h's own doc comment), because
    // "does this end still need an implicit disk / a synthesized cap" is
    // a property of the OPERAND (does it bound a solid by itself?), not
    // of the face: a boss's base at z=10 inside a union result is a
    // still-original, never-split end of its input cylinder AND an open
    // passage into the box, and no per-face flag can express that. So
    // these flags only ever steer a boolean for the legacy bare-tube
    // operand FromMixedFaces({}, {cf}), whose fresh true/true they are
    // right for, and for the fragments a split manufactures inside one
    // BooleanCombineMixed call.
    bool end0_is_original = true;
    bool end1_is_original = true;
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

    // Extends the guarantee cap0_notch_points/cap1_notch_points normally
    // make (every listed point lies within notch_uv()'s own near-zero
    // on-surface check, brep.cpp, of THIS cone's own real surface) for
    // exactly one further, honestly-disclosed provenance:
    // FilletConvexEdgeTapered's own N-station overload (fillet.h) splices
    // an INTERIOR station join between two adjacent taper segments by
    // giving the later segment's cap0_notch_points the EARLIER segment's
    // own true v1 circle, borrowed verbatim rather than derived from this
    // cone's own geometry. Two cones meeting where the taper RATE changes
    // do not, in general, share a literal circle beyond their two common
    // rail corners - verified directly (fillet.cpp's own interior-join
    // construction, and dino8-kernel's own regression tests), not
    // assumed: for a representative monotonic three-station profile the
    // two segments' own natural caps at the shared station diverge by a
    // few percent of the local radius at mid-sweep, both points measured
    // on the SAME sphere (the rolling ball's own position at that
    // station) - i.e. a real curve separation, not sampling noise, and it
    // does NOT shrink as the notch sampling gets finer (unlike every
    // other notch tolerance in this kernel).
    //
    // Setting this above the ordinary near-zero on-surface bound tells
    // FromMixedFaces()'s own notch_uv() check to accept up to THIS
    // genuinely computed (not guessed) worst-case radial deviation
    // between the borrowed points and this cone's own true surface
    // instead of throwing. Left at its default 0.0 for every OTHER
    // ConicalFace this kernel ever builds - in particular every
    // corner-notch call (EllipseNotchCornerAtVertex never sets it: its
    // own points DO lie exactly on this cone, by construction of its
    // closed form, so the tight default stays the active bound there,
    // unchanged) and every non-notched cap.
    //
    // A real, structural difference from cap0_notch_tolerance/
    // cap1_notch_tolerance above: those bound a polygonal approximation's
    // deviation from a curve that DOES lie exactly on this surface; this
    // bounds this surface's own deviation from a curve that, at an
    // interior taper station, provably does NOT - the first tolerance in
    // this kernel that does not shrink to zero as sampling gets finer,
    // documented as such rather than treated as a defect. See fillet.h's
    // own multi-station doc comment for the full derivation.
    double cap0_surface_fit_tolerance = 0.0;
    double cap1_surface_fit_tolerance = 0.0;
  };

  // One spherical blend patch's exact geometry: the piece of the sphere
  // of radius `radius` centered at `frame.origin` covering longitude
  // [0, angle] (radians, measured from frame.xaxis toward frame.yaxis
  // about frame.zaxis - the same angle=0-at-xaxis convention every other
  // angle on CylindricalFace/ConicalFace uses) and latitude [lat0, lat1]
  // (radians, from the frame's own equator plane toward +frame.zaxis; the
  // south pole is -pi/2, the north pole +pi/2). This is exactly the shape
  // a constant-radius rolling-ball VERTEX blend takes at a convex corner
  // where the ball is simultaneously tangent to three faces (see
  // FilletConvexEdges in dino8/kernel/fillet.h, the one producer): the
  // spherical triangle bounded by the three great-circle arcs along which
  // the three incident edge fillets' own cylinders end - realized here as
  // a latitude/longitude rectangle whose `lat1` (or `lat0`) side has
  // collapsed to a pole, so its three genuine boundary curves are ALL
  // isocurves of the sphere's own parameterization: the equator arc at
  // v=lat0 (or lat1) and the two meridians at u=0 and u=`angle`. (A
  // spherical triangle whose three arcs are NOT two meridians plus one
  // latitude circle - a corner where fewer than two of the three dihedral
  // angles are right angles - cannot be written this way; see
  // FilletConvexEdges' own SCOPE note for that disclosed limit.)
  //
  // Exactness argument, the same one CylindricalFace/ConicalFace rely on:
  // FromMixedFaces() builds the surface via ON_Sphere::GetNurbForm (a
  // rational quadratic NURBS that is EXACTLY the sphere, the same
  // conversion Brep::Sphere() already trusts) and trims it to the
  // sub-rectangle of the sphere's own (u, v) domain given by `angle`,
  // `lat0`, `lat1` - converted from true radians to NURBS parameter via
  // ON_Circle::GetNurbFormParameterFromRadian for u (ON_Sphere's u-knots
  // are literally the base circle's, confirmed against the vendored
  // source), and via the same conversion on the meridian's own
  // south-pole-to-north-pole semicircle (knots -pi/2, 0, +pi/2 - a circle
  // knot vector shifted by -pi/2) for v. At the quadrant knots (0, +-pi/2,
  // pi, ...) NURBS parameter and radian agree exactly, so a box corner's
  // own octant is trimmed with no conversion error at all. The patch's
  // outward normal is the sphere's own radial direction; `outward ==
  // true` (the default, and the only value the vertex blend ever needs)
  // keeps it, `false` flips it via ON_BrepFace::m_bRev exactly as
  // CylindricalFace::outward does.
  //
  // A POLE (lat1 == +pi/2 or lat0 == -pi/2, within 1e-9) is legal and is
  // the normal case for a vertex blend: that side of the (u, v) rectangle
  // maps to a single 3D point, so FromMixedFaces() gives it an
  // ON_Brep SINGULAR trim (ON_Brep::NewSingularTrim - a trim with no
  // edge, the standard B-rep representation of a surface's own
  // degenerate boundary, exactly what a full Rhino sphere carries at its
  // two poles) rather than a zero-length edge, and the exact-clip
  // tessellator's pole-adjacent triangles collapse to zero 3D area and
  // are dropped by Mesh::MergeAndWeld, so TessellateToClosedMesh() of a
  // solid carrying such a patch is a genuine closed manifold with no
  // degenerate faces. Both lat0 and lat1 being poles (a full meridian
  // lune) is rejected: that is not a blend patch this kernel builds.
  //
  // The two rail corners at each latitude end - the points at (angle 0,
  // lat0), (angle `angle`, lat0), and their lat1 counterparts (which
  // coincide at a pole) - are exactly the points frame.origin +
  // radius*(cos(lat)*(cos(phi)*xaxis + sin(phi)*yaxis) + sin(lat)*zaxis)
  // for the matching (phi, lat), and FromMixedFaces() welds them into the
  // same global vertex space as every PlanarFace/CylindricalFace loop
  // point, so an adjacent fillet cylinder's own cap arc (the SAME great
  // circle, the same two corner points, the same NURBS parameterization
  // along it - see FilletConvexEdges' own frame convention) becomes ONE
  // literal shared ON_BrepEdge with this patch's own equator or meridian
  // edge, with no notch machinery at all.
  struct SphericalFace {
    ON_Plane frame;
    double radius = 0.0;
    double angle = 0.0;
    double lat0 = 0.0;
    double lat1 = 0.0;
    bool outward = true;
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
  //     This is a deliberate CONTRACT, not merely a gap: a ConicalFace is
  //     excluded from the verbatim face-record path described below, so
  //     a cone always comes back through this geometric extraction with
  //     empty notch lists. FilletConvexEdgeTapered's own closed-form
  //     tests (tests/test_basic.cpp, the multi-station frustum-formula
  //     test) rebuild "self-contained frustum" solids from extracted
  //     cones and rely on those ends coming back as plain circular caps.
  //
  // VERBATIM FACE RECORDS (planar and cylindrical faces only): every face
  // FromMixedFaces() builds from a PlanarFace or a CylindricalFace keeps
  // that input record, verbatim, in a side table parallel to the face
  // list (face_records_, kept in lockstep by every factory here exactly
  // as face_arc_runs_/face_notch_rows_ already are). MixedFaces() returns
  // the stored record for such a face instead of re-deriving it from the
  // NURBS surface, so everything the geometric extraction above cannot
  // see comes back bit-identical: a PlanarFace's arc_runs (circular and
  // literal), notch_begin/notch_count and its producer's own plane basis;
  // a CylindricalFace's cap0/cap1_notch_points and their tolerances, its
  // true frame.origin/length for a notched face (the extraction's own
  // trim bounding box reads a notch's dip as extra length), and its
  // end0/end1_is_original flags. This is what makes a BooleanCombineMixed
  // result a first-class operand of a second call (see boolean.h).
  // Before a record is used it is checked against the face's REAL
  // surface (three surface evaluations per face: a planar face's first
  // three trim vertices against the record's first three loop points, a
  // cylindrical face's two angle-0 rail corners against
  // frame.origin + radius*xaxis at v=0/v=length plus a mid-sweep point's
  // height and radius against the record's axis, and the face's own
  // m_bRev against `outward`), within 1e-6 of the face's own scale. A
  // record that no longer matches - a Brep whose raw() ON_Brep was
  // transformed or reassigned behind this class's back, e.g. dino8-app's
  // FlowData `b.raw().Transform(x)` - is stale and silently ignored; the
  // face then takes the geometric extraction above, exactly as every face
  // built by any other factory (Box(), Sphere(), FromSurface(),
  // TrimmedPlanarFace(), a raw()-assigned .3dm reload) always does.
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
    // Every SphericalFace comes back from its verbatim FromMixedFaces()
    // record (see FaceRecord), or, for a face with no record whose surface
    // ON_NurbsSurface::IsSphere accepts, from a geometric extraction that
    // reads the frame straight off the surface's own quadrant points.
    std::vector<SphericalFace> spherical;
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
  // For a CylindricalFace with a notched cap (cap0_notch_points/
  // cap1_notch_points) whose notch leaves the [0, length] band, the
  // cylinder's own height span - and hence the surface's v-domain, which
  // ON_Cylinder::GetNurbForm sets to exactly [height[0], height[1]] - is
  // widened to [min(0, lowest notch height), max(length, highest notch
  // height)] before the surface is built, so the grid tessellators (which
  // span the surface's own domain) cover the whole trimmed region rather
  // than silently dropping the out-of-band sliver; the trim rectangle's
  // own rails still run v=0..length and every (u, v) coordinate is
  // unchanged, v being true axial height. An un-notched face's domain is
  // exactly [0, length], as before. Throws std::invalid_argument if that
  // widened span is degenerate. A face notched at BOTH ends with
  // length == 0 (a Steinmetz eye or an unequal-radius plug - see
  // CylindricalFace's own doc comment) builds as a two-trim loop between
  // its two pinch vertices, its zero-length rails skipped and its two
  // notched caps kept as distinct edges even though they join the same
  // two vertices; one with a positive length (an unequal-radius middle
  // band) keeps its two straight rails, which are told apart from a
  // circular cap ARC between the same two vertices (another fragment's
  // cut at the same height) by the chord's own midpoint - an arc and its
  // chord are never one curve.
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
                              const std::vector<ConicalFace>& conical_faces = {},
                              const std::vector<SphericalFace>& spherical_faces = {});

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

  // One Brep holding several independent closed shells ("lumps"): every
  // lump's ON_Brep is appended (ON_Brep::Append - "appends a copy of brep
  // to this and updates indices ... Duplicates are not removed") and every
  // face-parallel side table is concatenated in the same order, so each
  // lump's faces keep their own trims, arc runs, notch rows and verbatim
  // face records. Lumps are deliberately NOT welded to each other: two
  // lumps that touch along a curve keep their own vertices and edges
  // there. That is the only manifold representation of a symmetric
  // difference (XOR) - along the intersection curve of A and B the XOR
  // boundary has FOUR incident faces (A's outside, B's outside, and the
  // two flipped insides), which no single FromMixedFaces() shell can
  // hold ("an edge is shared by 3 or more faces") - and it is exactly
  // how the Manifold mesh boolean represents its own XOR (a touching
  // curve's vertices duplicated; welding them back gives 4-fold edges).
  // ON_Brep::IsValid()/IsSolid() hold for a compound of valid solid lumps
  // (each lump is a closed shell of its own; Tessellate*() volumes add up
  // per face), but a MergeAndWeld of the whole tessellation is not a
  // closed manifold wherever two lumps touch - weld each lump's own face
  // range (LumpFaceRanges() below) separately for a per-lump closure
  // check. A lump with no faces at all (an empty boolean result) is
  // skipped: the compound of X with the empty set is X, so such a result
  // is a plain single-lump Brep. Every lump must have its side tables in
  // lockstep with its own faces (a Brep one of this class's own
  // factories built, or empty) - a raw()-assigned lump whose tables do
  // not cover its faces is refused (std::invalid_argument) rather than
  // letting a following lump's tables slide onto its faces.
  static Brep Compound(const std::vector<Brep>& lumps);

  // The [begin, end) face-index ranges of this Brep's lumps, in the order
  // Compound() received them (a nested compound is flattened). A Brep
  // built by anything other than Compound() is one lump, {0, FaceCount()}.
  // The recorded ranges are checked against the actual face count (they
  // must tile [0, FaceCount()) exactly); a stale record - a raw() ON_Brep
  // reassigned behind this class's back - falls back to the single full
  // range, the same self-check discipline MixedFaces()'s face records
  // use. BooleanCombineMixed/BooleanCombinePlanar refuse an operand with
  // more than one lump (see boolean.h).
  std::vector<std::pair<int, int>> LumpFaceRanges() const;

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
  //
  // A FOURTH gap, closed at SYMMETRIC divisions only (task #65): the
  // three passes above all implicitly assumed that whenever a
  // plain-quad face's two natural sample counts (per edge) already
  // agree, BOTH sides tessellate identically - true whenever both sides
  // dispatch through the same underlying algorithm, but NOT when the
  // straight-edge pass above has already forced exactly ONE side of a
  // seam (pinning that face to BuildConformingPlainQuadMesh's own
  // bilinear grid for its ENTIRE tessellation, including its other,
  // still-unclaimed edges) while the other side's face remains on its own
  // natural default - which, for a face BooleanCombineMixed reconstructed
  // via FromMixedFaces() (`exact_clip = true` even for an untouched input
  // face), is NurbsSurface::TessellateGridClippedExact(), a genuinely
  // different, margined algorithm. The first Brep in this kernel's own
  // test suite to expose this: a box with a bare CylindricalFace boss
  // Union'd on, whose base sits flush with (or embedded in) the box's TOP
  // face - the TOP z-cap gets wedge-split around the boss, but the BOTTOM
  // z-cap (the boss's z-range never reaches it) stays a single, untouched
  // plain quad. Every prior BuildDrilledBoxInputs-based test drills a hole
  // clean through BOTH z-caps, so a wedge-forced wall's only plain-quad
  // neighbor was always ALSO wedge-forced - this one-sided topology was
  // simply never built before. Closed by reusing the exact same
  // CollectPlainQuadFaces()/ComputePlainQuadSeamForces() machinery the
  // third pass above already uses (not a fourth, parallel mechanism): the
  // straight-edge pass's own `plain_forces` map is now ALSO passed to
  // ComputePlainQuadSeamForces as an `already_forced` lookup, so a pair is
  // forced not only on a genuine natural-count mismatch but also whenever
  // exactly one side is already a `plain_forces` key - reusing that
  // already-forced side's own OPPOSITE edge's point count (not a flat
  // max(u_divisions, v_divisions)) as the shared count, so
  // BuildConformingPlainQuadMesh's own tensor grid (which needs a quad's
  // two OPPOSITE edges internally consistent - see its own doc comment)
  // stays consistent on the already-forced side; see
  // ComputePlainQuadSeamForces's own doc comment in brep.cpp for the exact
  // mechanism and the direct measurement that surfaced both the original
  // gap and this consistency requirement.
  //
  // Deliberately restricted to u_divisions == v_divisions: investigated
  // directly, not merely assumed, an unrestricted version of this same
  // trigger made an ASYMMETRIC-divisions instance of this same one-sided
  // fixture genuinely WORSE (more open boundary edges than before the
  // fix, not merely still-open) - because at unequal divisions, which
  // physical axis a wall assigns to "u" vs "v" is not the same for every
  // wall (front and back walls assign it oppositely - see
  // ComputePlainQuadSeamForces's own doc comment), so two DIFFERENT
  // already-forced walls bordering the SAME untouched cap can legitimately
  // need that cap's own two OPPOSITE edges forced to two DIFFERENT counts,
  // a conflict no single per-pair choice can resolve. At EQUAL divisions
  // this conflict is structurally impossible (u_divisions and v_divisions
  // are then the same number, so every wall's own forced count agrees
  // regardless of axis convention) - confirmed directly, not merely
  // assumed. So a one-sided wedge/quad seam like the one above is
  // genuinely closed at any SYMMETRIC u_divisions/v_divisions by this
  // trigger alone; the combination of a one-sided wedge AND asymmetric
  // divisions was, for a time, a disclosed limitation of
  // BuildConformingPlainQuadMesh's own tensor-grid design, and is now
  // closed downstream of the forcing by the per-row plain-quad strip
  // mesher (the EIGHTH entry below) without touching this trigger's own
  // restriction - see
  // TestTessellateConformingOneSidedWedgeSymmetricDivisionsIsClosedManifold
  // and
  // TestTessellateConformingOneSidedWedgeAsymmetricDivisionsIsClosedManifold
  // for the exact falsifiable claims, both proven directly rather than
  // assumed.
  //
  // A FIFTH gap, closed here: the arc-matching pass above only ever
  // populates `cyl_matches` for a CylindricalFace fragment with an ArcRun
  // match on AT LEAST one of its own v=0/v=length ends (an ordinary
  // BuildEndCap cap or a lens cap - see boolean.h's own BooleanCombineMixed
  // disclosure). A fragment with NO match on EITHER end - a "friendless"
  // middle axial band, produced when SplitCylindricalByOtherCylinderAxialExtent
  // (boolean.cpp) axially splits one wedge into 3+ bands and only the
  // OUTER two bands keep a real terminus - used to fall through to
  // NurbsSurface::TessellateGrid's plain, raw-u-uniform grid: genuinely
  // different physical angular sample locations than an axially-adjacent,
  // ArcRun-matched sibling's own angle-uniform breakpoints along the
  // exact same physical circle, leaving a real, narrow non-manifold seam
  // at the band's own internal axial cut lines (not a boolean-topology
  // defect - purely this method's own mesh-density reconciliation gap).
  // Closed by giving such a face a fallback breakpoint schedule built NOT
  // from a fresh, independent resampling of its own [0, angle] sweep (a
  // uniform-in-true-angle recipe applied to the WHOLE sweep in one pass
  // is not the same schedule a capped sibling's own ArcRun match(es)
  // compute whenever that cap is itself split into several sub-arcs - e.g.
  // BuildEndCap's own "always 4 quadrant pieces" convention samples EACH
  // quadrant uniformly across only its own quarter, not the full sweep in
  // one pass, so an independently-resampled full-sweep schedule generally
  // lands on DIFFERENT breakpoints than the quadrant-based one even at
  // the same overall sample count - confirmed directly, not merely
  // theorized, during this fix's own development) but by directly REUSING
  // an axially-adjacent sibling's own already-computed `raw_u`
  // breakpoints, identified via SameWedgeAsCylinder (brep.cpp) - a
  // stricter test than the arc-matching pass's own SameCircleAsCylinder,
  // additionally requiring the SAME angular reference direction
  // (frame.xaxis) and sweep, not merely the same axis+radius, since two
  // DIFFERENT wedges of one physical cylinder share a circle but not an
  // angular reference. Every genuine axial sibling produced by
  // SplitCylindricalByOtherCylinderAxialExtent qualifies (that function
  // copies `frame`/`angle`/`radius` verbatim across axial siblings, only
  // ever trimming `length`/shifting `frame.origin` along the shared
  // axis), so reusing its raw_u values reproduces the identical
  // breakpoints, closing the seam. This fallback is GATED on the
  // fragment's own trim being a plain (u, v) rectangle with no holes
  // (IsRectangularTrimUv, brep.cpp): BuildConformingCylinderMesh grids
  // the trim's full bounding box and never clips to the polygon, so a
  // NOTCHED, cap-less fragment - the oblique path's own surviving
  // hole-wall, whose only ends are ellipse notches and which therefore
  // also has no ArcRun match on either end - keeps the trim-clipping
  // path exactly as Tessellate() treats it. Confirmed necessary, not
  // merely prudent: an ungated version of this fallback silently filled
  // such a fragment's notch back in (its area came out as the bounding
  // box's, not the closed form) - see
  // TestTessellateConformingNotchedUncappedCylinderHonorsTrim
  // (tests/test_basic.cpp). `CanonicalCylinderUBreakpoints`
  // (brep.cpp, an independent-resampling fallback) is kept only as a
  // defensive last resort for the (believed never to arise in practice)
  // case where no such sibling exists at all - see its own doc comment
  // for exactly why it is NOT equivalent to reusing a real sibling's own
  // breakpoints. See
  // TestTessellateConformingFriendlessMiddleBandSyntheticWedgeIsClosedManifold
  // (tests/test_basic.cpp) for the exact falsifiable claim, verified on a
  // synthetic 3-band single-wedge fixture built directly (bypassing
  // BooleanCombineMixed entirely) so the check isolates this ONE
  // mechanism from the separate, still-open gap described next.
  //
  // A SIXTH gap, closed by the per-row strip mesher
  // (BuildConformingCylinderStripMesh, brep.cpp): a SEPARATE density-
  // mismatch in the "one shared u-breakpoint list for both the v=0 and
  // v=length rows" design BuildConformingCylinderMesh's own
  // `breaks`/`filled` construction relies on - when a SINGLE
  // CylindricalFace fragment has REAL ArcRun matches at BOTH its own
  // ends, but those two caps have genuinely DIFFERENT internal structure
  // (e.g. an ordinary BuildEndCap cap, split into 4 quadrant sub-arcs, on
  // one end, and a single, un-split BuildLensEndCap arc spanning the
  // WHOLE sweep on the other - exactly the shape boolean.h's own
  // crossing-Difference fixture's INNER wedge bands have), the denser
  // cap's own quadrant-boundary breakpoints leaked, as extra UNFORCED
  // grid columns, into the sparser cap's own row too (since
  // `breaks`/`filled` was shared across both rows) - a genuine
  // T-junction between that row and the sparser cap's own simpler
  // boundary loop, NOT a breakpoint-VALUE mismatch (every one of the
  // sparser cap's own forced points was already bit-identical on both
  // sides). The fix is exactly the restructuring that diagnosis called
  // for: each of the two rows now carries its OWN breakpoint schedule -
  // the forced points of the ArcRun matches at THAT end only (plus the
  // two rails and the same uniform gap fill as before) - so the lens row
  // has exactly the lens loop's own vertices and the quadrant row exactly
  // the four quadrants' union, and the band between two such
  // differently-sampled rows is lofted by a stack-sweep triangulation of
  // the u-monotone polygon they bound (de Berg et al., Computational
  // Geometry, section 3.3 - not a naive two-pointer merge, which is only
  // valid for a CONVEX strip and can silently overlap when one row is a
  // curved notch). Interior rows are laid on the UNION of both rows'
  // breakpoints, so the strip's own interior stays a clean quad-like
  // lattice wherever the two rows agree and only the cells between two
  // genuinely different samplings pick up the extra fan triangles. The
  // SAME mesher also serves a NOTCHED cylindrical fragment (one built
  // with cap0_notch_points/cap1_notch_points): its notched row is the
  // literal notch polyline (the side table `face_notch_rows_` records the
  // input's own points AND the (u, v) FromMixedFaces() spliced into the
  // visible trim, so chain and trim agree exactly), which closes a
  // latent gap the earlier IsRectangularTrimUv gate only protected the
  // cap-LESS case against: a notched fragment WITH an ArcRun match on its
  // flat end used to reach the bounding-box tensor mesher and have its
  // notch silently filled back in (measured directly on a synthetic
  // shared-notch pair - see
  // TestTessellateConformingSharedNotchCylinderPairIsClosedManifold in
  // tests/test_basic.cpp - as a volume of ~159.2 against a true 125.7,
  // with every notch edge shared by 4 faces). Dispatch (see the loop at
  // the end of this method's body): a hole-free cylindrical fragment is
  // routed to the strip mesher iff it is notched OR it has matches at
  // BOTH ends whose EFFECTIVE per-row column sets (forced raw_u plus the
  // two rails, deduplicated within the mesher's own u tolerance) differ;
  // every other face - in particular every face that was already closed
  // under the shared-list tensor mesher - keeps its previous path
  // bit-for-bit. Comparing EFFECTIVE rather than raw forced sets is
  // essential: a full-sweep face whose two caps put the seam sample at
  // u=0 on one row and at u=u_max on the other has different raw sets
  // but identical effective columns, and comparing raw sets re-routed
  // ~15 already-closed faces in this file's own test suite (found and
  // fixed by tracing every dispatch decision across the whole suite,
  // which now re-routes exactly the crossing fixture's two inner bands
  // plus the notched oblique fragments). See
  // TestBooleanCombineMixedParallelCylinderDifferenceCrossingIsClosedManifold
  // and ...RowSchedulesDiffer (tests/test_basic.cpp) for the falsifiable
  // claims: the crossing fixture is a closed manifold at symmetric AND
  // asymmetric divisions, and the inner band's two rows carry 65 and 257
  // distinct vertices respectively - a count the shared-list design could
  // not produce. An oblique fragment's seam against its oblique PLANAR
  // cap is closed by the SEVENTH and EIGHTH entries below (the planar
  // side used to exact-clip over its own grid rather than mesh from the
  // shared literal notch points). The length-0 "eye" and half-band shapes a
  // Steinmetz (equal-radius crossing-axes) boolean produces (both rows
  // notched with the rails pinched to a shared vertex; one notch row plus
  // one cap-forced flat row) are verified end to end: every Steinmetz
  // Union/Intersection/Difference result is a closed manifold under this
  // method at 90 and 60 degrees (see BooleanCombineMixed's own Steinmetz
  // tessellation paragraph in boolean.h). One detail that only a
  // non-right axis angle exposes: a matched cap run's own endpoint sample
  // sitting exactly on a partial-sweep face's angle-0 rail can convert to
  // -epsilon in that face's frame and, normalized into [0, 2*pi), land at
  // 2*pi - epsilon - a raw u far beyond the face's own sweep that would
  // drag the flat row's gap-fill columns across the whole untrimmed far
  // side of the cylinder. Such a sample is snapped back to the seam
  // (angle 0) when it lies beyond the face's own sweep; a full-sweep face
  // is unaffected, both readings naming the same seam.
  //
  // A SEVENTH gap, closed here: the oblique plane+cylinder seam's PLANAR
  // side. BooleanCombineMixed's oblique case yields planar wedge pieces
  // whose loops contain the ellipse's literal EllipsePointAt samples -
  // the same values the notched cylindrical fragment carries as its
  // cap0/cap1_notch_points and meshes as its literal notch row under the
  // SIXTH entry above - but those pieces recorded no ArcRun at all, so
  // they fell through to TessellateGridClippedExact over their own
  // margined grid, which inserts a vertex wherever a grid line crosses
  // the ellipse polyline: a pure T-junction seam (measured on the
  // oblique-drilled box at 64/64: 446 open edges on the two ellipses
  // while every one of the cylinder's 402 row vertices was already
  // float== a planar vertex), and their straight perimeter segments were
  // never matched against the walls either (a further 1440 open edges on
  // the box perimeter, since the straight-edge pass only walks
  // `wedge_subs` sources). Closed by recording the ellipse stretch as a
  // LITERAL ArcRun (PlanarFace::ArcRun::literal_points, reported by
  // ClipPolygonByEllipse3d's own `ellipse_runs` out-param at the one
  // site that knows the run exactly), which the arc-matching pass
  // registers in `wedge_subs` verbatim - no cylinder match, no
  // ArcSchedule3d resampling - so the piece dispatches to
  // BuildConformingWedgeMesh (an ear-clip of its literal loop) and its
  // straight segments enter the straight-edge pass like any other
  // wedge's. One producer-side fix came with it: for the cap whose
  // outward normal opposes the cylinder's own phi sweep (a drilled box's
  // z=0 cap) ClipPolygonByEllipse3d's pieces were wound clockwise as seen
  // from outside (ON_Brep LoopDirection -1) - harmless to the grid
  // clippers, but it turned the ear-clipped pieces inside out; the
  // clipper now reverses such a piece, vertex set untouched.
  //
  // An EIGHTH gap, closed here - the planar twin of the SIXTH: with the
  // ellipse seam closed, 756 open edges remained at 64/64, all on the
  // box's two 20-long walls. The tilted hole's two cap ellipses pierce a
  // long wall's top and bottom edges at different positions (the seam's
  // offset between the caps is 10*tan(theta)), so the two edges carry
  // different forced t-sets, and BuildConformingPlainQuadMesh's tensor
  // grid unions both into one column list: the bottom edge's split
  // column appears on the top row as an unforced bilinear point, and
  // vice versa. Closed by BuildConformingPlainQuadStripMesh (see its own
  // doc comment): row 0 exactly the b=0 edge's chain, the last row
  // exactly the b=1 edge's, interior rows on the union, consecutive rows
  // joined by the same TriangulateStrip the cylindrical strips use.
  // Dispatch: a plain-forced quad takes the strip iff exactly one of its
  // two opposite-edge pairs has differing EFFECTIVE t-sets (transposed
  // when it is the b pair); a quad whose pairs both agree keeps the
  // tensor mesher bit-for-bit (every previously closed result
  // reproduces), and a quad mismatched on BOTH axes has no such rescue
  // and stays on the tensor mesher - disclosed, and hit by no fixture in
  // this kernel's suite. This also closes the asymmetric-divisions
  // one-sided-wedge case disclosed further above (measured 220/264/128
  // open edges at 8/11, 17/4, 12/20, now zero, volumes unchanged):
  // ComputePlainQuadSeamForces's own u == v restriction is untouched;
  // the count conflict it avoids simply no longer needs resolving, since
  // a quad's two opposite edges no longer have to agree. Verified end to
  // end: the oblique-drilled box is a closed manifold under this method
  // at 64/64, 12/20, 17/4 and 8/8 and at 5/15/30-degree tilts, the bare
  // oblique Union (box plus a protruding tilted cylinder, 22 faces)
  // likewise, with the volume matching the inscribed-200-gon closed form
  // within 1e-6 - see TestTessellateConformingObliqueDrilledBoxIsClosedManifold
  // and its siblings (tests/test_basic.cpp). Unchanged and unrelated to
  // mesh watertightness: the ON_Brep topology of an oblique result still
  // does not share the ellipse edges between the pieces and the
  // fragment (one ON_LineCurve edge per sample on each side; IsSolid()
  // stays false), exactly as before.
  std::vector<Mesh> TessellateConforming(int u_divisions = 8, int v_divisions = 8,
                                          int boundary_samples = -1) const;

  // TessellateConforming() followed by Mesh::MergeAndWeld() - mirrors
  // TessellateToClosedMesh()'s own composition over Tessellate().
  Mesh TessellateToClosedMeshConforming(int u_divisions = 8, int v_divisions = 8,
                                         int boundary_samples = -1) const;

  // Merges adjacent, coplanar-and-coincident faces of THIS Brep's own
  // real ON_Brep topology into fewer, larger faces, in place - the
  // classic "merge coplanar/tangent adjacent faces along a shared edge
  // into one face, dropping the now-interior edge" operation, done by
  // walking this Brep's own m_E/m_T/m_L adjacency directly, never through
  // a mesh boolean and never through Manifold (github.com/elalish/
  // manifold, this kernel's separate solid-boolean engine - see
  // boolean.h). This is the concrete rebuttal to the reasoning
  // NonmanifoldMerge used to be narrowed by (see cmd_solidtools.cpp's own
  // history): "no non-manifold representation to merge faces of" was only
  // ever true of Manifold's own mesh format, never of ON_Brep, which is
  // exactly what this class already wraps and which OpenNURBS itself
  // documents as supporting non-manifold topology (an edge referenced by
  // more than two trims).
  //
  // A candidate pair of faces (fa, fb) is merged only when ALL of the
  // following hold - each one a genuine "leave it alone, don't guess"
  // narrowing, not an oversight:
  //   - both are planar (NurbsSurface::IsPlanar) and lie in the SAME
  //     plane (coincident origin and parallel same-direction normal,
  //     within `tolerance`) - a curved or merely-tangent (not coplanar)
  //     pair is left untouched, exactly the "coplanar" half of the
  //     classic operation's own name.
  //   - both have exactly one loop (no inner/hole loops) - a v1
  //     narrowing; a face with a hole is left untouched rather than
  //     risking a wrong merge of its hole boundary.
  //   - they share EXACTLY ONE edge, and that edge has EXACTLY TWO trims
  //     (both belonging to fa and fb) - the "non-manifold-safe" condition
  //     the class comment above promises: an edge a THIRD face also
  //     touches is never removed, so merging never corrupts topology
  //     anywhere else in a non-manifold assembly (e.g. one NonmanifoldMerge
  //     produced by welding several solids' naked boundaries together
  //     first). A pair touching along more than one edge (a shape whose
  //     merge would not be a simple polygon) is left untouched too.
  //
  // The merge itself walks each face's own outer loop (via ON_BrepTrim::
  // Edge()/m_bRev3d, not a re-derived polygon) to build the two boundary
  // curve chains that remain once the shared edge is removed, splices
  // them (they always meet head-to-tail at the shared edge's own two
  // vertices, by the standard opposite-direction two-manifold-edge
  // convention) into one closed boundary, and rebuilds a single trimmed-
  // plane face from it via the same ON_BrepTrimmedPlane() OpenNURBS API
  // this kernel's app layer already uses for a coplanar-face merge
  // (cmd_fillet.cpp's MergeFacesInto) - the one piece of this operation
  // that isn't itself new, since building a trimmed plane from a 3D
  // boundary is a solved problem this codebase already relies on
  // elsewhere; what IS new is deriving that boundary from real B-rep
  // adjacency instead of from a mesh boolean of extruded slabs. Any
  // naked edge the merge exposes elsewhere on the two consumed faces
  // (a third, untouched neighbor's own edge) is re-welded onto the new
  // face's matching boundary edge, the same coincident-naked-edge join
  // this kernel's app layer already performs after a topology edit.
  //
  // Repeats until no more eligible pairs remain (merging fa/fb can expose
  // a new coplanar-adjacent pair). Returns the number of merges actually
  // performed (each merge reduces FaceCount() by exactly one) - 0 if none
  // of this Brep's faces qualify. Never throws: an ineligible face or
  // pair is simply left alone, not an error.
  int MergeCoplanarFaces(double tolerance = 1e-6);

  // Re-trims every face that shares edge `edge_index` against a
  // substitute 3D curve, replacing the edge's own geometry in place while
  // leaving the rest of this Brep's topology (every other face, edge,
  // vertex) untouched - closing the "no operation to re-trim a face
  // against a substitute edge curve while keeping the rest of the
  // polysurface intact" gap (see cmd_srfedit.cpp's own prior history).
  // Works for a naked (1-trim), ordinary shared (2-trim) or genuinely
  // non-manifold (3+-trim) edge alike, since it just walks
  // `edge.m_ti[]` - however many trims that is.
  //
  // `new_curve`'s own endpoints must land within `tolerance` of the
  // edge's EXISTING two vertices (in either direction; the curve is
  // reversed internally if it runs the other way) - ReplaceEdgeCurve
  // reshapes the edge between its own fixed endpoints, it does not
  // re-point the topology to new ones. For each trim on the edge, this
  // then samples `new_curve` and projects each sample onto that trim's
  // OWN face surface via NurbsSurface::ClosestPointParameter() - the same
  // closest-point projection this kernel's app layer already uses to
  // re-derive a trim after a topology edit invalidates its old one (see
  // cmd_fillet.cpp's SplitFace/SplitEdge) - to build that face's new 2D
  // trim curve directly through the projected (u, v) points.
  //
  // Throws std::invalid_argument if `edge_index` is out of range or
  // refers to a deleted edge, if `new_curve`'s endpoints don't land near
  // the edge's own two vertices (see above), or if a face's surface has
  // no NURBS form. Throws std::runtime_error - the "genuinely doesn't
  // fit" case this is deliberately not silent about - if any projected
  // sample lands more than `tolerance` (loosened by a fixed factor to
  // allow for ordinary projection/fit noise) from `new_curve`'s own point
  // there, or strays outside that face's surface domain: a substitute
  // curve of wildly different length or shape than the edge it's
  // replacing fails this way rather than silently producing a
  // self-intersecting or out-of-domain trim.
  void ReplaceEdgeCurve(int edge_index, const NurbsCurve& new_curve,
                        double tolerance = 1e-4);

  // Splits a shared (exactly two trims) edge into two coincident but
  // topologically distinct naked edges, in place, while leaving both
  // faces in THIS SAME Brep - Rhino's own UnjoinEdge semantics exactly
  // (as opposed to ExtractSrf, which pulls one of the two faces out into
  // a separate object entirely; see cmd_srfedit.cpp's own prior history
  // for the gap this closes). The edge's own 3D curve is duplicated into
  // a brand-new ON_BrepEdge sharing the same two vertices; the SECOND of
  // the original edge's two trims (edge.m_ti[1]) is then moved onto that
  // new edge via ON_BrepTrim::AttachToEdge() - the OpenNURBS "expert
  // user" API that correctly updates both edges' own m_ti[] bookkeeping,
  // rather than hand-editing it. The result: two edges, each with exactly
  // one trim - i.e. two naked edges where SelNakedEdges-style detection
  // (edge.TrimCount() == 1) previously found none, occupying the same
  // 3D location.
  //
  // Returns Result::Failed (not a thrown exception - this is an
  // ordinary, expected outcome, the same "can't, but that's not a bug"
  // contract MergeEdge's own ON_Brep::CombineContiguousEdges failure
  // already has in cmd_fillet.cpp) if `edge_index` refers to an edge that
  // is not shared by EXACTLY two trims (a naked edge has nothing to
  // unjoin; a non-manifold 3+-trim edge is out of scope for v1) or whose
  // curve could not be duplicated. Throws std::out_of_range if
  // `edge_index` itself is out of range, or std::invalid_argument if it
  // refers to an already-deleted edge - both genuine caller bugs, not
  // ordinary outcomes.
  Result UnjoinEdge(int edge_index);

  // Closes RemoveAllNakedMicroEdges' own real gap (cmd_srfedit.cpp used to
  // only detect these, never remove them): actually removes a naked
  // (1-trim) edge shorter than `tolerance`, by welding its two endpoint
  // vertices into one and re-trimming its two loop-adjacent edges (via
  // ReplaceEdgeCurve, above) to close over the resulting gap - not a
  // silent DeleteEdge, which would leave the loop's boundary open.
  //
  // Deliberately scoped to the one case this can close WITHOUT guessing at
  // unrelated topology: `edge_index` must be naked (TrimCount() == 1,
  // same detection the app layer already uses), its two loop-neighboring
  // edges (ON_Brep::PrevTrim()/NextTrim() on its own single trim) must
  // ALSO be naked, and each of the micro edge's own two vertices must
  // touch NOTHING ELSE in this Brep besides the micro edge and that one
  // neighbor - i.e. a genuine, isolated sliver on one face's own open
  // boundary (the common "bad trim left a hairline gap" case), never a
  // vertex a third edge, a second face's shared edge, or a non-manifold
  // junction also depends on. Returns Result::Failed - not a thrown
  // exception, the same "can't, but that's not a bug" contract
  // UnjoinEdge() above already has - for every case outside that scope,
  // or if closing the gap failed for a reason specific to this edge's own
  // geometry (see below); the edge is left exactly as it was.
  //
  // The actual close: each neighbor edge's own curve is duplicated and
  // nudged, via ON_Curve::SetStartPoint()/SetEndPoint() (whichever end
  // touches the micro edge), to reach the midpoint of the micro edge's
  // own two vertices instead of its own old endpoint - then committed
  // through ReplaceEdgeCurve() itself (re-trimming that neighbor's own
  // one face against the nudged curve, exactly the "re-trim the two faces
  // on either side to close the gap" this is built on), before the two
  // vertices are combined (ON_Brep::CombineCoincidentVertices()) and the
  // now fully degenerate micro edge/trim are deleted and the Brep is
  // Compact()ed. If SetStartPoint()/SetEndPoint() can't move a neighbor's
  // curve (some curve types refuse - see that method's own doc comment)
  // this returns Result::Failed before touching this Brep at all.
  Result RemoveNakedMicroEdge(int edge_index, double tolerance = 1e-4);

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
  // Parallel to face_arc_runs_ (kept in lockstep by every factory that
  // pushes face_arc_runs_): a CylindricalFace's own literal cap-notch
  // rows, consumed ONLY by TessellateConforming()'s per-row strip mesher
  // (see that method's own doc comment). `present` is false for every
  // face except a cylindrical one built by FromMixedFaces() with a
  // non-empty cap0_notch_points and/or cap1_notch_points; for such a face
  // `cap0_points`/`cap1_points` are that input's own lists verbatim (in
  // its fixed increasing-angle order) and `cap0_uv`/`cap1_uv` are the
  // SAME (u, v) coordinates FromMixedFaces() itself computed for them
  // (the ones spliced into the face's visible trim loop), so the mesher's
  // notch row and the trim polygon agree exactly rather than being two
  // independent re-derivations of one curve. `v_end0`/`v_end1` are the
  // flat rows' own v (0 and length), recorded here so the mesher never
  // has to recover them from a trim bounding box that a notch's own dip
  // past the rail band would have widened. An un-notched end keeps an
  // empty list and the mesher builds that row as a flat chain instead.
  struct CylinderNotchRows {
    bool present = false;
    std::vector<Point3d> cap0_points;
    std::vector<Point2d> cap0_uv;
    std::vector<Point3d> cap1_points;
    std::vector<Point2d> cap1_uv;
    double v_end0 = 0.0;
    double v_end1 = 0.0;
  };
  std::vector<CylinderNotchRows> face_notch_rows_;
  // Parallel to face_notch_rows_ (kept in lockstep by every factory that
  // pushes it): the exact PlanarFace or CylindricalFace record
  // FromMixedFaces() built face i from, verbatim - `kind` is kNone for a
  // face built by any other factory, and, deliberately, for a ConicalFace
  // (see MixedFaces()'s own doc comment: a cone's notches come back empty
  // by contract). Consumed ONLY by MixedFaces(), which returns the record
  // in place of the geometric extraction after checking it still matches
  // the face's own surface.
  struct FaceRecord {
    enum Kind { kNone = 0, kPlanar, kCylindrical, kSpherical };
    Kind kind = kNone;
    PlanarFace planar;
    CylindricalFace cyl;
    SphericalFace sph;
  };
  std::vector<FaceRecord> face_records_;
  // Set only by Compound(): the [begin, end) face-index range of each
  // lump, in order. Empty for every other Brep (one lump) - see
  // LumpFaceRanges().
  std::vector<std::pair<int, int>> lump_face_ranges_;
};

}  // namespace dino8::kernel
