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
// A real architectural fact, checked directly (`ON_Brep::IsValid()`),
// not assumed: every face-adding factory here (Box(), Sphere(),
// TrimmedPlanarFace()) builds its face via `ON_Brep::NewFace(int
// surface_index)` - the minimal, surface-only overload - rather than
// constructing genuine `ON_Brep` vertex/edge/trim/loop topology the way
// Rhino's own file format expects. `ON_Brep::IsValid()` checks exactly
// that topology, so it reports every Brep this kernel builds as invalid,
// even a perfectly good one like `Box()`. This doesn't stop a Brep built
// here from being fully usable through this kernel's own pipeline, which
// never calls `IsValid()` and doesn't need the topology it checks for:
// `Tessellate()` reads each face's surface directly, and
// `TessellateToClosedMesh()`'s own vertex-welding step is what actually
// closes the seams between faces, not shared `ON_Brep` vertex/edge
// records. Still, a `.3dm` file saved via `Model::AddBrep()` may not
// round-trip cleanly through other OpenNURBS-based tools that validate
// topology on load.
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
  struct CylindricalFace {
    ON_Plane frame;
    double radius = 0.0;
    double angle = 0.0;
    double length = 0.0;
  };

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
  // FromPlanarFaces(faces) is exactly FromMixedFaces(faces, {}).
  static Brep FromMixedFaces(const std::vector<PlanarFace>& faces,
                              const std::vector<CylindricalFace>& cylindrical_faces);

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
};

}  // namespace dino8::kernel
