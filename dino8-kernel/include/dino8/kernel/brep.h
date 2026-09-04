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
  static Brep TrimmedPlanarFace(const NurbsSurface& surface,
                                 const std::vector<Point2d>& trim_loop_uv,
                                 bool exact_clip = false,
                                 std::vector<std::vector<Point2d>> hole_loops_uv = {});

  int FaceCount() const;

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
