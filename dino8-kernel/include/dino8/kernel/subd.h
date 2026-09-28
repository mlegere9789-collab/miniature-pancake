#pragma once

#include <opennurbs.h>

#include <utility>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"

namespace dino8::kernel {

// One NURBS patch produced by SubD::ToNurbsPatches(), covering exactly one
// face of the current subdivision level's control net.
struct SubDNurbsPatch {
  // The patch's underlying surface - always an untrimmed, degree-3 x
  // degree-3 (4x4 control points, single Bezier-form span) surface over
  // [0,1]x[0,1], whether `exact` or not (an irregular patch is still
  // returned as a bicubic surface, built by degree-elevating a bilinear
  // corner interpolant, so every patch composes into a Brep the same way -
  // see ToNurbsPatches()'s own doc comment for what `exact` actually
  // means for each case).
  NurbsSurface surface;
  // True if `surface` is the mathematically exact Catmull-Clark limit
  // surface over this face (a regular face - see ToNurbsPatches()).
  // False if `surface` is a tolerance-bounded approximation (an
  // irregular face - touches an extraordinary vertex, a crease, or a
  // boundary).
  bool exact;
};

// One control-net vertex's exact Catmull-Clark limit-surface point, from
// SubD::LimitPoints().
struct SubDLimitPoint {
  unsigned int vertex_id = 0;  // ON_SubDVertex::m_id (stable across edits, not an array index)
  Point3d control_point;       // the vertex's current control-net position
  Point3d limit_point;         // the point on the limit surface this vertex converges to
  // Unit limit-surface normal at `limit_point`, or the zero vector if
  // OpenNURBS reports it undefined. For a crease/corner vertex the
  // limit surface has one normal PER SECTOR (per smooth region around
  // the vertex, separated by its crease edges); this is the sector
  // containing the vertex's first face - the other sectors' normals
  // differ and aren't reported here.
  Vector3d limit_normal;
  int valence = 0;      // number of edges at the vertex
  bool smooth = false;  // ON_SubDVertex::IsSmooth(): interior, not on a crease/corner/dart
};

// One evaluation of the Catmull-Clark limit surface at an arbitrary
// (u, v) parameter inside one face, from SubD::EvaluateFace().
struct SubDSurfacePoint {
  Point3d position;
  // Unit limit-surface normal, or the zero vector if the two partial
  // derivatives below are parallel or zero (mirrors SubDLimitPoint::
  // limit_normal's degenerate-case convention).
  Vector3d normal;
  // The raw (non-unit) partial derivatives d(position)/du,
  // d(position)/dv - not normalized, same convention as
  // NurbsSurface::CurvatureAt()'s internal du/dv.
  Vector3d tangent_u;
  Vector3d tangent_v;
  // True if `position`/`normal`/`tangent_u`/`tangent_v` are exact (to
  // floating-point precision) values of the true Catmull-Clark limit
  // surface - see EvaluateFace()'s own doc comment for exactly when
  // that holds. False means they come from EvaluateFace()'s
  // tolerance-bounded fallback approximation instead.
  bool exact = false;
};

// Wraps ON_SubD - OpenNURBS' real, working Catmull-Clark subdivision
// surface implementation. Unlike ON_Brep::CreateMesh/ON_Surface::CreateMesh
// (chunk 2) and ON_SubD::BrepForm/GetSurfaceBrep (checked for this chunk),
// which are declared in OpenNURBS' public headers but are unimplemented
// stubs there (BrepForm() literally `return nullptr;`, verified directly
// against the v8.34 source, not assumed) - ON_SubD::GlobalSubdivide() is a
// genuine, non-stub Catmull-Clark refinement: real face/edge/vertex point
// computation, verified by reading its implementation, not just calling it
// and hoping.
//
// What OpenNURBS' public API does *not* give us: a limit-surface MESHER,
// or evaluation at an arbitrary point inside a face. GetControlNetMesh()
// only ever returns the current subdivision level's control net (a mesh
// of flat quads through the control points) - repeatedly subdividing and
// re-extracting that net is the standard "just subdivide a lot"
// approximation technique, not exact limit-surface evaluation. CORRECTED
// (this comment used to claim no exact limit evaluator existed at all):
// it does ship exact per-VERTEX limit evaluation, `ON_SubDVertex::
// SurfacePoint()`/`SurfaceNormal()`, real and non-stub in
// opennurbs_subd_eval.cpp - wrapped as LimitPoints() below and verified
// against the closed-form limit masks. Face-interior exactness comes
// from ToNurbsPatches() on regular faces only.
class SubD {
 public:
  // Builds a SubD control cage directly from `control_mesh`'s own faces -
  // each mesh face (triangle, quad, or n-gon) becomes one SubD face at
  // level 0. Throws std::runtime_error if OpenNURBS' own
  // ON_SubD::CreateFromMesh rejects the input (e.g. a mesh with no faces).
  //
  // `crease_at_double_edges`, if true, creates an interior SubD crease
  // (a sharp fold, not smoothed away by subdivision) at every "mesh
  // double edge": an interior edge where two adjacent faces reference
  // *distinct* vertex indices at coincident locations, rather than
  // sharing the same vertex index (verified directly against the v8.34
  // source's own definition of ON_SubDFromMeshParameters::
  // InteriorCreaseOption::AtMeshDoubleEdge, not guessed). A caller wanting
  // a crease along some edge must therefore duplicate that edge's two
  // vertices (same 3D position, different array indices) in
  // `control_mesh` on at least one of the two faces meeting there -
  // ordinary shared-index construction (as every other primitive in this
  // kernel builds) never produces a double edge, so this is opt-in and
  // doesn't change behavior for existing meshes. Defaults to false
  // (`ON_SubDFromMeshParameters::Smooth`, this class's original behavior).
  static SubD FromControlMesh(const Mesh& control_mesh, bool crease_at_double_edges = false);

  // Builds a SubD control cage from a single UNTRIMMED NURBS surface by
  // evaluating a u_divisions x v_divisions grid of points across its
  // parameter domain and taking each grid cell as one genuine QUAD SubD
  // face - closing PARITY_MAP.md's subd_mesh "SubD from NURBS/B-rep
  // conversion (reverse of ToNurbsPatches)" [missing] item for the
  // single-surface case (a full Brep -> SubD conversion, matching faces
  // and creases across a whole solid or polysurface, is a materially
  // bigger problem this does not attempt).
  //
  // Unlike `NurbsSurface::TessellateGrid()` (built for mesh-boolean work
  // and always TRIANGULATING each grid cell), this keeps every cell a
  // genuine quad - the whole point of building a SubD cage: a
  // triangulated control net starts every face irregular
  // (`ToNurbsPatches()` only gives an exact limit patch on regular,
  // all-quad faces), throwing away the surface's own regular parametric
  // structure before `Subdivide()` even runs once.
  //
  // This is deliberately an APPROXIMATION of the input surface, not a
  // lossless conversion: a Catmull-Clark limit surface over a regular
  // interior quad reproduces a UNIFORM bicubic B-spline patch (see
  // `ToNurbsPatches()`'s own doc comment), not an arbitrary NURBS
  // surface's real shape between grid points (non-uniform knots, a
  // different degree, rational weights - none of that survives sampling
  // into flat grid quads); the approximation improves as
  // u_divisions/v_divisions increase, the same tradeoff
  // `TessellateGrid()` already documents for its own triangulated
  // output. A flat/bilinear input surface is the one case this IS exact
  // for (verified in the tests: every corner of a regular quad's flat
  // Catmull-Clark limit patch coincides with its own control points).
  //
  // Throws std::invalid_argument if u_divisions or v_divisions is less
  // than 1, the same validation `TessellateGrid()` already applies.
  static SubD FromNurbsSurface(const NurbsSurface& surface, int u_divisions, int v_divisions);

  // Applies `levels` rounds of real Catmull-Clark global subdivision in
  // place. Each round refines every face, edge, and vertex of the
  // current control net into a strictly finer one; the result converges
  // toward, but never reaches, the smooth limit surface (see the class
  // comment on why exact limit evaluation isn't available here). No-op
  // if `levels <= 0`. Throws std::runtime_error if OpenNURBS'
  // ON_SubD::GlobalSubdivide fails (e.g. `levels` would exceed
  // ON_SubD::maximum_subd_level).
  void Subdivide(int levels);

  // Extracts the current subdivision level's control net as a Mesh
  // (ON_SubD::GetControlNetMesh) - after enough Subdivide() calls, a
  // dense, all-quad mesh that visually approximates the limit surface.
  // The raw GetControlNetMesh() output is then deduplicated
  // (ON_Mesh::CombineIdenticalVertices()) before returning: OpenNURBS'
  // own routine emits one mesh vertex per face-corner, not one per shared
  // ON_SubDVertex (confirmed on a Symmetrize()d SubD, whose seam vertices
  // - genuinely shared at the SubD level - otherwise came out as
  // duplicate, unwelded mesh vertices at bit-identical positions,
  // reporting spurious boundary edges on an actually-closed shape); a
  // control net's own face corners at the exact same 3D point are always
  // the same conceptual vertex by definition, so this exact-position
  // merge is always safe here. Throws std::runtime_error if OpenNURBS'
  // own call fails.
  Mesh ToApproximateMesh() const;

  // Applies `xform` to a copy of this SubD's ENTIRE control cage (every
  // level it currently holds, not just the active one) and returns it -
  // the same missing piece `Mesh::Transform()` already closes for
  // `Mesh`, but this class never had at all: no way to move, rotate,
  // scale, or mirror a SubD once built, other than baking the transform
  // into the control mesh BEFORE calling FromControlMesh() (impossible
  // after Subdivide() has already run, since that discards the original
  // mesh). Delegates directly to `ON_SubD::Transform` (verified by
  // reading `ON_SubDimple::Transform`'s own implementation: it
  // transforms every level's vertices, correctly detects a similarity
  // transform to preserve cached subdivision/limit points instead of
  // discarding them, and updates texture/color mapping and symmetry
  // state - real, thorough work, not a stub).
  //
  // One caveat worth being explicit about, since it is genuinely easy to
  // miss: for a MIRROR (a negative-determinant `xform`), this only moves
  // vertex positions - it does not touch any face's own vertex winding
  // order, the same convention `Mesh::Transform()` already follows (see
  // `ON_Mesh::Transform`'s own handling of `xform.Determinant() < 0`,
  // which flips stored normal VECTORS but never `ON_MeshFace::vi[]`
  // order). The result is still a perfectly VALID SubD (`IsValid()`
  // holds - the topology's internal edge/face winding agreement is
  // unaffected by a uniform coordinate transform, the same reason a
  // wholly `Mesh::FlipNormals()`-ed mesh stays a valid closed manifold)
  // but is now "inside-out" relative to the mirrored geometry, the exact
  // analog of `Mesh::UnifyNormals()`'s own "a wholly inverted box is
  // still a consistent closed manifold" case. There is no
  // `SubD::UnifyNormals()`/`FlipNormals()` counterpart here (a real,
  // disclosed gap, not attempted in this pass) - a caller mirroring a
  // SubD should account for this at the `ToApproximateMesh()`/
  // `ToNurbsPatches()` stage instead, where `Mesh::FlipNormals()` and
  // NURBS surface reversal already exist.
  //
  // Throws std::invalid_argument if `xform` itself is not a valid
  // transform (e.g. contains a NaN/infinite entry) - `ON_SubD::
  // Transform`'s own first check, read directly, matching this class's
  // existing convention of failing loudly rather than silently handing
  // back an untransformed or partially-transformed copy.
  SubD Transform(const ON_Xform& xform) const;

  // Mirrors this SubD's entire control cage across the plane
  // `{p : p . plane_normal == plane_offset}` (same convention as
  // `SplitByPlane()` in boolean.h) and combines the original half with
  // its mirrored copy into a single result - closing the "no
  // flip/weld" half of PARITY_MAP.md's subd_mesh "SubD symmetry/
  // mirror-in-place" gap that `Transform()`'s own doc comment above
  // already flags but does not fix:
  //
  //  - flip: each mirrored face's vertex order is reversed before
  //    being added, undoing the orientation reversal a reflection
  //    always introduces, so the mirrored half comes out right-side-out
  //    (not the "inside-out" copy a bare `Transform(mirror_xform)`
  //    would give, per that method's own documented caveat).
  //  - weld: any ORIGINAL vertex already within `point_tolerance` of
  //    the mirror plane is reused as-is for the mirrored face touching
  //    it, instead of being duplicated at its own (unchanged, since
  //    it's on the plane) reflected position - so a naked boundary loop
  //    that already lies in the mirror plane becomes a single shared
  //    seam between the two halves (each of its edges now has both an
  //    original-side and a mirrored-side face, so it stops being naked)
  //    rather than two separate coincident-but-disconnected loops.
  //
  // Still partial, deliberately not attempted here: live constrained
  // symmetric editing (a later edit to one half automatically
  // re-mirroring into the other) is a distinct, materially larger
  // feature - this produces one static symmetrized snapshot, with no
  // ongoing relationship between the two halves afterward. A vertex
  // that starts strictly off-plane is always duplicated (never welded
  // to a same-side neighbor), so this only closes gaps that coincide
  // with the mirror plane itself, not general internal seams.
  //
  // Throws std::invalid_argument if `plane_normal` is zero (or too
  // close to it to unitize), the same failure convention `Transform()`
  // above uses for a degenerate input.
  SubD Symmetrize(Vector3d plane_normal, double plane_offset, double point_tolerance = 1e-9) const;

  // Converts the *current* subdivision level's control net to real NURBS
  // patches, one per face - a genuine Catmull-Clark limit-surface
  // conversion, not the "just subdivide a lot and facet it" approximation
  // ToApproximateMesh() gives.
  //
  // This is not proprietary technology: away from extraordinary vertices,
  // a Catmull-Clark limit surface is *identical* to a uniform bicubic
  // B-spline surface whose control lattice is the control net itself -
  // this reduction is in Catmull & Clark's original 1978 paper and is
  // standard in every subdivision-surfaces reference since (e.g. Jos
  // Stam's 1998 SIGGRAPH paper "Exact Evaluation of Catmull-Clark
  // Subdivision Surfaces at Arbitrary Parameter Values" treats the
  // regular case as the base/degenerate case of its eigenbasis; Pixar's
  // open-source OpenSubdiv implements the same regular/irregular split).
  // So for every "regular" face - all 4 corners are ordinary interior
  // vertices (valence exactly 4, smooth, not on a boundary or crease) -
  // this gathers that face's 4x4 neighborhood of control points (the
  // standard closed-form regular-patch stencil: the 4 face corners, the
  // 2 "outer" neighbors across each of the face's 4 edges, and the 1
  // diagonal-opposite vertex in the 4th face around each corner),
  // converts it from B-spline to Bezier form with the standard uniform
  // cubic B-spline-to-Bezier conversion matrix, and returns it as an
  // `exact = true` patch: this *is* the limit surface over that face, to
  // floating-point precision, not an approximation.
  //
  // For every "irregular" face - touches an extraordinary vertex
  // (valence != 4), a crease, or a boundary - no such closed form exists
  // from the control net alone (that needs either Stam's per-vertex
  // eigenbasis, which requires solving that vertex's subdivision
  // matrix's eigenstructure, or a Gregory-patch-style G1 construction);
  // this instead returns an `exact = false` bicubic patch built by
  // bilinearly interpolating the face's 4 control-net corner points and
  // degree-elevating that to a (still flat, but topologically bicubic)
  // Bezier patch. That patch's deviation from the true limit surface is
  // bounded by the face's own size (it's the face's flat corner
  // interpolant, not the curved limit surface), which is why callers
  // needing a tighter bound should Subdivide() first: Catmull-Clark
  // subdivision shrinks every face's linear size by 2x per level while
  // leaving the number of irregular faces fixed (exactly one per
  // extraordinary/boundary vertex, forever - subdividing can never make
  // an extraordinary vertex's incident faces regular), so each
  // subdivision level roughly quarters the irregular patches' maximum
  // deviation from the true surface (their area, hence their flatness
  // error, shrinks with the square of their shrinking linear size).
  //
  // Every patch (regular or irregular) is returned as an independent,
  // untrimmed 4x4-control-point degree-3 surface over [0,1]^2; adjacent
  // *regular* patches share an identical boundary curve (same
  // underlying uniform B-spline, split at a knot - Bezier subdivision of
  // one B-spline surface is C0 *and* the shared curve is bit-identical,
  // not just close), so joining them into one Brep with an ordinary
  // edge-matching join (ON_Brep::Append + naked-edge matching) recovers
  // a real seamless polysurface wherever the surface is regular; an
  // irregular patch's boundary generally does NOT bit-match its regular
  // neighbors' (it's a different, approximate construction), so those
  // stay as naked, unjoined edges - deliberately: no silently making
  // that approximation look exact.
  std::vector<SubDNurbsPatch> ToNurbsPatches() const;

  // Like ToNurbsPatches(), but replaces each IRREGULAR face's single
  // flat bilinear-corner-interpolant patch with several smaller, more
  // accurate ones - reusing EvaluateFace()'s own adaptive-refinement
  // technique (one real level of Catmull-Clark GlobalSubdivide() always
  // turns every quadrant NOT touching the face's own extraordinary
  // vertex/crease/boundary corner(s) fully regular) but pursuing all 4
  // of a subdivision's child quadrants, not just whichever one a single
  // (u, v) query lands in. A regular face is unaffected (still one exact
  // patch, same as ToNurbsPatches()); an irregular face with exactly one
  // bad corner becomes 3 exact regular patches plus 1 still-irregular
  // patch covering a quadrant 2x smaller (in each parametric direction)
  // than the original face - which itself splits the same way if
  // `max_adaptive_levels` allows another level, recursively. A face with
  // more than one bad corner (e.g. two adjacent extraordinary vertices,
  // or a boundary face with 2+ naked corners) can leave more than one
  // child irregular per level; each still recurses independently.
  //
  // `max_adaptive_levels` is the same per-face recursion budget
  // EvaluateFace() takes: 0 makes this identical to ToNurbsPatches()
  // (every irregular face returned as one flat patch, no splitting).
  // Throws std::invalid_argument if `max_adaptive_levels` is negative.
  //
  // Cost and the large-mesh fallback are the same as EvaluateFace()'s:
  // each split clones the current subdivision level's WHOLE control net
  // into an internal working copy (`raw()` is never touched) and
  // globally refines it, so cost is proportional to the working copy's
  // OWN size per level, and a working copy already past 500,000 faces
  // stops splitting further and falls back to the flat patch for
  // whatever's left, regardless of `max_adaptive_levels` remaining -
  // this can leave some irregular faces less-split than others on a
  // very large or very irregular net, never a correctness problem, only
  // a coarser approximation there.
  //
  // Boundary caveat carried over from ToNurbsPatches(), now also
  // between a face's own split-off patches: two SIBLING patches from the
  // very same split (e.g. two of the 3 newly-regular quadrants) share a
  // bit-identical boundary curve, same as two regular patches from
  // different faces already do - but where one quadrant recurses again
  // and its neighbor doesn't, the neighbor's one full-length edge faces
  // TWO half-length edges on the further-split side (a T-junction, the
  // same well-known artifact adaptive subdivision-surface tessellation
  // always has without an explicit transition/Gregory-patch construction
  // this class doesn't attempt) - deliberately left as naked, unjoined
  // edges there too, for the same reason ToNurbsPatches() already leaves
  // an irregular patch's boundary naked: no silently making an
  // approximation look exact. A caller wanting a single watertight Brep
  // despite this should Subdivide() the whole SubD first (shrinking, not
  // eliminating, the irregular region) rather than relying on this
  // method to close every seam.
  std::vector<SubDNurbsPatch> ToNurbsPatchesAdaptive(int max_adaptive_levels) const;

  int FaceCount() const;
  int VertexCount() const;

  // Count of the current subdivision level's own edges - the third
  // topology count alongside FaceCount()/VertexCount(), closing a small
  // gap this class left open (a caller wanting Euler-characteristic-style
  // topology checks, e.g. V - E + F, had no way to get an edge count
  // before). Delegates to `ON_SubD::EdgeCount`.
  int EdgeCount() const;

  // True if OpenNURBS' own ON_SubD::IsValid() considers the current
  // control net structurally sound - the SubD-level counterpart to
  // Mesh::IsClosedManifold(), closing a real gap this class had: no
  // Check()/IsValid() at all, so a caller could only discover a broken
  // SubD (e.g. one built by hand-editing raw() rather than through this
  // class's own methods) the hard way, whatever ON_SubD happened to do
  // internally when handed one. Delegates to the real, non-stub
  // ON_SubD::IsValid() (verified by reading its implementation in
  // opennurbs_subd.cpp: it walks every level's vertices/edges/faces
  // checking cross-reference and tag consistency, a genuine structural
  // check, not a placeholder). Passes OpenNURBS' own documented sentinel
  // (an ON_TextLog* with its low bit set - not a dereferenced pointer;
  // ON_SubD::IsValid masks that bit off again before ever touching it,
  // verified the same way) so a "no" answer never has the side effect of
  // writing to OpenNURBS' global error log - this is a validity CHECK a
  // caller may reasonably expect to fail sometimes (e.g. mid-edit), not
  // an assertion that something already went wrong.
  bool IsValid() const;

  // Count of the current subdivision level's own crease edges (the
  // sharp folds `FromControlMesh(mesh, crease_at_double_edges=true)`
  // can create - see that method's own doc comment) - the only direct
  // way this class has ever offered to check how many creases actually
  // exist, versus only checking their geometric *effect* the way
  // TestSubDCreaseAtDoubleEdgeKeepsFoldStraight() does. NOT a wrapper
  // around `ON_SubD::CreaseEdgeCount` despite that method's existence in
  // OpenNURBS' own public header - verified by grepping the whole
  // v8.34 source tree that it's declared but never implemented
  // anywhere (only an unrelated class, `ON_SubDVertexSharpnessCalculator
  // ::CreaseEdgeCount`, actually exists), the same "declared for Rhino,
  // not present in the public build" pattern this file's own class
  // comment already documents for `ON_SubD::BrepForm`. Implemented here
  // instead via the real, working `ON_SubD::EdgeIterator()` +
  // `ON_SubDEdge::IsCrease()` (the exact pattern `ON_SubD::FirstEdge()`'s
  // own doc comment recommends), counting edges whose tag is genuinely
  // a crease.
  int CreaseEdgeCount() const;

  // Marks the SMOOTH interior edge between the control-net vertices found
  // at (or within `point_tolerance` of) `p0` and `p1` as a semi-sharp
  // crease of constant weight `sharpness` - real Pixar/OpenSubdiv-style
  // variable-weight creasing (OpenNURBS' own ON_SubDEdge::m_sharpness /
  // ON_SubDEdgeSharpness), which this class previously had no way to set
  // at all: `FromControlMesh(mesh, crease_at_double_edges=true)` only
  // ever gives a binary sharp/smooth split (ON_SubDEdgeTag::Crease,
  // permanent and never relaxes), with no way to dial in anything
  // between "fully smooth" and "fully creased".
  //
  // `sharpness` must be in [0, ON_SubDEdgeSharpness::MaximumValue] (== 4,
  // verified against the v8.34 source, not guessed) - 0 is a no-op
  // ("fully smooth"), and MaximumValue makes the edge behave like a real
  // Crease-tagged edge at the CURRENT subdivision level only (see below
  // for how that differs from an actual crease tag). GlobalSubdivide()
  // genuinely consumes this value in its Catmull-Clark matrix -
  // opennurbs_subd_limit.cpp's regular-patch evaluator branches on
  // ON_SubDEdge::IsSharp() and ON_SubDVertex::VertexSharpness() to blend
  // face/edge/vertex points toward crease behavior, read directly from
  // OpenNURBS' source, not assumed - and decays it toward zero by (at
  // most) 1.0 per level via ON_SubDEdge::SubdivideSharpness(). So a
  // semi-sharp edge relaxes into an ordinary smooth edge after
  // ceil(sharpness) further Subdivide() levels; a real Crease-tagged
  // edge never relaxes. This wrapper exposes one constant weight per
  // edge (both ends equal); OpenNURBS also supports a per-end-variable
  // sharpness (linearly interpolated along the edge, decaying
  // differently at each end) that this method does not expose - a
  // caller needing that must use raw() directly.
  //
  // Returns false, unchanged, if: `sharpness` is outside
  // [0, ON_SubDEdgeSharpness::MaximumValue]; no vertex is found at p0 or
  // at p1 within point_tolerance; no edge connects them; or that edge is
  // not smooth (it's already a hard Crease-tagged edge - sharpness has
  // no meaning there in OpenNURBS' own model, so this refuses rather
  // than silently no-op'ing and pretending it worked).
  bool SetEdgeSharpness(const Point3d& p0, const Point3d& p1, double sharpness,
                        double point_tolerance = 0.0);

  // Retags the interior edge between the control-net vertices found at
  // (or within `point_tolerance` of) `p0` and `p1` as a hard Crease
  // (`crease = true`) or back to Smooth (`crease = false`) - a direct
  // kernel-level crease tagging operation, which this class previously
  // had no way to do at all after construction: `FromControlMesh(mesh,
  // crease_at_double_edges=true)` only ever decides creases once, from
  // mesh topology, at build time.
  //
  // Unlike SetEdgeSharpness() above (which needs a const_cast onto a
  // low-level "for experts" primitive, because OpenNURBS' own
  // convenience wrapper for THAT turned out to be unimplemented), this
  // delegates to a genuinely public, fully-implemented
  // `ON_SubD::SetEdgeTags()` - verified by reading its body in
  // opennurbs_subd.cpp, not assumed: it does real work beyond the one
  // edge's tag, reclassifying both endpoint vertices (Smooth / Dart /
  // Crease / Corner, by their new count of incident crease edges),
  // clearing any leftover SetEdgeSharpness() weight on either transition
  // (a semi-sharp edge retagged Crease doesn't keep its old sharpness
  // value sitting around unused), and invalidating cached evaluation
  // state - all the bookkeeping a caller hand-editing `raw()` would have
  // to get right itself.
  //
  // `crease = true` on an edge that already has 3+ faces (non-manifold)
  // or fewer than 2 (a boundary edge - always already a crease by
  // OpenNURBS' own convention) is refused by `ON_SubD::SetEdgeTags`
  // itself, same as `crease = false` there.
  //
  // Returns false if: no vertex is found at p0 or at p1 within
  // point_tolerance; no edge connects them; or the edge already has the
  // requested tag, or otherwise can't accept it (see above) - a real
  // no-op, same as `ON_SubD::SetEdgeTags` returning a 0 changed-count.
  // Returns true only when the edge's tag genuinely changed.
  bool SetCrease(const Point3d& p0, const Point3d& p1, bool crease, double point_tolerance = 0.0);

  // Caps ONE open boundary loop of the current subdivision level's
  // control net with a single new N-GON SubD face spanning the whole
  // loop - the SubD-level counterpart to `Mesh::FillSmallHoles()`,
  // closing PARITY_MAP.md's subd_mesh "SubD hole/opening capping at
  // kernel level" [missing] item. Genuinely SubD-native, not a ported
  // mesh trick: `Mesh::FillSmallHoles()` needs a centroid vertex and a
  // triangle fan because `ON_MeshFace` tops out at 4 indices, but
  // `ON_SubDFace` supports any edge count directly - so an n-sided hole
  // becomes exactly one new n-gon face, no extra vertex, and (being a
  // real SubD face like any other) a fully genuine, further-subdividable
  // part of the control net from the moment it's added.
  //
  // Identifies the loop from ONE of its own boundary vertices (`start`):
  // every boundary vertex has exactly 2 naked (single-face) edges, so
  // the walk from `start` - follow a naked edge to its far end, take
  // that vertex's OTHER naked edge, repeat - is unambiguous and
  // terminates by returning to `start`, UNLESS `start` is a "bowtie"
  // vertex where two different boundary loops touch (more than 2 naked
  // edges) - there this picks whichever loop its first naked edge
  // happens to belong to (`ON_SubDVertex::EdgeCount()`'s own iteration
  // order), the same acknowledged ambiguity `NakedEdgeLoops()` documents
  // for the identical case on `Mesh`. The collected edges are handed to
  // the real, working `ON_SubD::AddFace(const ON_SimpleArray<ON_SubDEdge*>&)`
  // (verified by reading its implementation in opennurbs_subd.cpp: it
  // validates the loop genuinely closes and computes each edge's
  // orientation from shared vertices automatically, not a stub).
  //
  // After capping, the loop's own edges - tagged Crease purely because
  // they were a boundary (OpenNURBS' "an open SubD's own boundary edges
  // are themselves always creases" convention this file's
  // `crease_at_double_edges` comment already documents, not because
  // anyone asked for a sharp seam there) - are retagged Smooth via the
  // same `ON_SubD::SetEdgeTags()` primitive `SetCrease()` above already
  // wraps, so the cap blends into the surrounding surface instead of
  // leaving an unintended permanent crease ring where the hole used to
  // be. A caller who DOES want a sharp ring around the cap can call
  // `SetCrease()` again afterward - this method's own job is only to
  // reproduce the "ordinary hole in an otherwise smooth surface" case.
  //
  // Returns false, unchanged, if: no vertex is found at `start` within
  // `point_tolerance`; that vertex has no naked edge (it's fully
  // interior, or the SubD is already closed); or the boundary doesn't
  // close back on itself (a dead end / non-manifold boundary chain,
  // e.g. one `NakedEdgeLoops()` would also refuse to chain) - refuses
  // rather than adding a wrong or malformed face.
  bool CapBoundaryLoop(const Point3d& start, double point_tolerance = 0.0);

  // Divides the single face touching BOTH the control-net vertices found
  // at (or within `point_tolerance` of) `p0` and `p1` into two faces, by
  // inserting a new edge directly between them - the kernel-native
  // "InsertEdge" local edit operator, one of the five PARITY_MAP.md
  // subd_mesh "Kernel-native SubD local edit operators" [missing] item
  // named zero-hit ("insert edge, extrude face, spin edge, weld,
  // expand") - genuinely present at the kernel level now, not app-only.
  //
  // Delegates to the real, non-stub `ON_SubD::SplitFace(face, v0, v1)`
  // (verified by reading `ON_SubDimple::SplitFace` in
  // opennurbs_subd.cpp: it validates both sides of the split come out at
  // least a triangle, rejects an already-adjacent vertex pair - that
  // would create a degenerate two-sided "face" - and builds the new
  // edge/face with correctly inferred orientation, not a stub).
  //
  // `p0`/`p1` must both be corner vertices of the SAME face (found by
  // scanning `p0`'s own incident faces for one that also has `p1` as a
  // corner) - if they touch more than one common face at once (a
  // degenerate/non-manifold configuration), this refuses rather than
  // guessing which one the caller meant, the same ambiguity convention
  // `CapBoundaryLoop()`'s own bowtie handling above already uses.
  //
  // Returns false, unchanged, if: no vertex is found at p0 or at p1
  // within point_tolerance; the two vertices don't share exactly one
  // common face; that face has fewer than 4 edges (a triangle can't be
  // split into two faces that are each still at least a triangle); p0
  // and p1 are already adjacent on that face (nothing to insert - they
  // already share an edge); or `ON_SubD::SplitFace` itself refuses for
  // any other reason. Returns true only once the new edge genuinely
  // exists.
  bool InsertEdge(const Point3d& p0, const Point3d& p1, double point_tolerance = 0.0);

  // Spins the interior edge between the control-net vertices found at
  // (or within `point_tolerance` of) `p0` and `p1` around its two
  // adjacent faces' boundaries - the kernel-native "SpinEdge" local edit
  // operator named by the same PARITY_MAP.md [missing] item InsertEdge()
  // above closes part of. Each endpoint moves to the next vertex around
  // its OWN face's boundary (the face on the right for the start vertex,
  // the face on the left for the end vertex, in a counter-clockwise
  // spin - reversed by `spin_clockwise`), the SubD analog of flipping a
  // triangle mesh's diagonal edge, generalized to whatever polygon each
  // adjacent face actually is.
  //
  // Delegates to the real, non-stub `ON_SubD::SpinEdge(edge,
  // spin_clockwise)` (verified by reading `ON_SubDimple::SpinEdge` in
  // opennurbs_subd.cpp: genuine topology surgery - re-parents the edge's
  // vertex/face arrays on both sides, not a stub).
  //
  // Returns false, unchanged, if: no vertex is found at p0 or at p1
  // within point_tolerance; no edge connects them; that edge is not a
  // genuine interior edge with exactly one face on each side
  // (`ON_SubDEdge::HasInteriorEdgeTopology`) - a naked boundary edge or a
  // non-manifold one (3+ faces) has no well-defined spin; or
  // `ON_SubD::SpinEdge` itself refuses (e.g. either adjacent face is a
  // triangle, so there's no "next" vertex distinct from the edge's own
  // far endpoint to spin onto).
  bool SpinEdge(const Point3d& p0, const Point3d& p1, bool spin_clockwise = false,
                double point_tolerance = 0.0);

  // Extrudes the single face identified by `face_id` (ON_SubDFace::m_id,
  // same convention as EvaluateFace()) straight out along its own
  // outward `ControlNetCenterNormal()` by `distance`, turning it into a
  // protrusion - the kernel-native "ExtrudeFace" local edit operator,
  // the third of the five named PARITY_MAP.md subd_mesh "Kernel-native
  // SubD local edit operators" [missing] item's operators this pass adds
  // (with InsertEdge()/SpinEdge() above). The face's own boundary loop
  // stays exactly where it is - still shared with whatever neighboring
  // faces already touched it, so extruding one face out of a larger
  // cage opens a "chimney" rather than detaching a floating island - a
  // new copy of the loop is added at the offset position (becoming the
  // new top face), and a ring of new side quad faces connects the two
  // loops, each tagged Crease exactly where the original boundary
  // already was naked or already Crease-tagged (so extruding a face out
  // of a flat interior patch stays smooth at the base, while extruding a
  // face off an open mesh's own boundary produces a genuinely sharp new
  // edge there) - real behavior of the delegate itself, not something
  // this wrapper adds.
  //
  // Delegates to the real, substantial (hundreds of lines, not a stub -
  // verified by reading `ON_SubD::Internal_ExtrudeComponents` in
  // opennurbs_subd.cpp) `ON_SubD::ExtrudeComponents(xform, cptr_list,
  // cptr_count)`, called here with exactly one component (this face) and
  // `xform` a pure translation along the face's own unit normal.
  //
  // Returns false, unchanged, if: `face_id` doesn't identify a face of
  // the current subdivision level; `distance` is 0 (a zero/identity
  // transform is refused up front by the delegate, matching this
  // class's existing "no-op on a degenerate input" convention rather
  // than silently returning an unmodified copy); the face's own control
  // net is degenerate enough that `ControlNetCenterNormal()` comes back
  // as the zero vector (cannot be unitized into a direction); or
  // extruding this face would create a non-manifold edge (3+ side faces
  // meeting one extruded vertex - refused by the delegate's own
  // `bPermitNonManifoldEdgeCreation = false` default, same safety this
  // class's other topology-mutating methods already apply).
  bool ExtrudeFace(unsigned int face_id, double distance);

  // Moves an entire connected region of faces - `face_ids` - away from the
  // rest of the control net as one rigid unit, along the region's own
  // averaged outward normal - the kernel-native "expand" local edit
  // operator, the fourth of the five named PARITY_MAP.md subd_mesh
  // "Kernel-native SubD local edit operators" item's operators (with
  // InsertEdge()/SpinEdge()/ExtrudeFace() above). Unlike calling
  // ExtrudeFace() once per face in the region, an edge shared by two faces
  // that are BOTH in `face_ids` stays interior - untouched, no new wall
  // built along it - so a multi-face region opens exactly one ring of new
  // side faces around its own outer boundary, the region itself moving as
  // a single block ("push apart"), not each face growing its own
  // independent chimney.
  //
  // Delegates to the same real, non-stub `ON_SubD::ExtrudeComponents(xform,
  // cptr_list, cptr_count)` ExtrudeFace() uses, called here with every
  // requested face as one component list and a single shared translation -
  // it is `ON_SubD`'s own component-marking pass (marks every listed face;
  // an edge attached to two marked faces is left alone; an edge attached to
  // exactly one marked face, or already a boundary edge, is extruded into a
  // side face) that gives the "interior edges stay put" behavior, not
  // anything this wrapper adds.
  //
  // The push direction is the unit vector sum of each listed face's own
  // `ControlNetCenterNormal()` (unitized before summing, then the sum
  // itself unitized), matching ExtrudeFace()'s single-face convention when
  // `face_ids` has exactly one element, and giving a sensible single
  // direction for a roughly coplanar/convex region generally.
  //
  // Returns false, unchanged, if: `face_ids` is empty or contains a
  // duplicate id; `distance` is 0; any id doesn't identify a face of the
  // current subdivision level; any listed face's own
  // `ControlNetCenterNormal()` is degenerate (cannot be unitized); the
  // summed direction itself is degenerate (e.g. two faces with opposite
  // normals exactly cancel); or `ON_SubD::ExtrudeComponents` itself refuses
  // (e.g. extruding the region would create a non-manifold edge, refused by
  // the delegate's own `bPermitNonManifoldEdgeCreation = false` default).
  bool ExpandFaces(const std::vector<unsigned int>& face_ids, double distance);

  // Merges the two DISTINCT control-net vertices `keep_vertex_id` and
  // `discard_vertex_id` (both ON_SubDVertex::m_id, the same stable-across-
  // edits convention ExtrudeFace()/ExpandFaces() already use for faces)
  // into one - the kernel-native "Weld" local edit operator, the fifth
  // and last of the five named PARITY_MAP.md subd_mesh "Kernel-native
  // SubD local edit operators" item's operators (with InsertEdge()/
  // SpinEdge()/ExtrudeFace()/ExpandFaces() above).
  //
  // Deliberately id-based rather than point-based like InsertEdge()/
  // SpinEdge()/SetCrease() above: this operator's own main reason to
  // exist is merging two vertices that sit at (or near) the exact SAME
  // position but were never joined at the SubD level (e.g. two separately
  // built cages placed edge-to-edge) - `ON_SubD::FindVertex(point,
  // tolerance)` can only ever resolve to ONE of two such coincident
  // vertices, so a point-based signature could never even name the
  // second one to weld it to the first.
  //
  // Unlike InsertEdge()/SpinEdge()/ExtrudeFace()/ExpandFaces(), `ON_SubD`
  // exposes no ready-made primitive for this (re-confirmed against the
  // v8.34 source: no `Weld`/`MergeEdge`/`MergeVertex` anywhere in
  // opennurbs_subd.h/.cpp) - this hand-rolls the merge instead, as a
  // whole-net rebuild rather than local surgery. The obvious-looking
  // local approach - `ON_SubD::DeleteComponents()` on the discarded
  // vertex, then `FindOrAddFace()` the affected faces back onto the kept
  // one, the same technique `Symmetrize()` above uses for its own
  // plane-seam weld - turns out to be unsafe here: `DeleteComponents()`'s
  // own "delete isolated edges" pass (always on for the public overload,
  // verified by reading `ON_SubDimple::DeleteComponents`) also deletes
  // any OTHER vertex left with zero faces once the discarded vertex's own
  // faces are gone, even one that still has edges - not just the
  // discarded vertex itself. A vertex whose only face WAS one of those
  // (an ordinary case: two quads placed edge-to-edge share no OTHER face)
  // gets silently swept away too, taking the very corners this method
  // needs to reconnect with it (confirmed by direct reproduction, not
  // assumed: welding one coincident corner pair of two disjoint quads
  // this way dropped the vertex count by 4, not 1, and left the result
  // invalid).
  //
  // So this instead snapshots the CURRENT control net's entire vertex/
  // face/edge set (ids, positions, corner-id lists, tags, sharpness),
  // remaps every reference to the discarded vertex's id onto the kept
  // vertex's id, and rebuilds a fresh `ON_SubD` from that snapshot via
  // `ON_SubD::AddVertexForExperts()` - explicitly documented for exactly
  // this "copying portions of an existing SubD to a new SubD" use case -
  // preserving every ORIGINAL vertex's own id, so a component belonging
  // to both the old and new net can be found by the same id in either.
  // Every original edge's tag and sharpness is reapplied by that same id
  // pair afterward; only the handful of brand-new edges this merge
  // actually creates (directly between the kept vertex and a former
  // neighbor of the discarded one) are left `Unset` for the final
  // `ON_SubD::UpdateAllTagsAndSectorCoefficients(true)` to resolve - no
  // vertex tag needs any special-case preservation at all, since every
  // vertex starts `Unset` and is re-derived purely from its (correctly
  // restored) edges, the same "vertex tags are always DERIVED, never
  // stored history" fact `Symmetrize()`'s own seam handling above already
  // relies on. Net effect: wherever the rebuilt net's own edges already
  // exist elsewhere around the kept vertex, they're reused (gaining a
  // second face, becoming genuinely interior) rather than duplicated -
  // exactly what "weld two separate edges/vertices together" means - and
  // an edge that already had two faces before this call (real interior,
  // whether smooth or a genuine user-set crease via `SetCrease()`) keeps
  // its own tag and sharpness exactly as before, untouched by the
  // rebuild.
  //
  // `weld_tolerance` bounds how far apart the two vertices' own
  // ControlNetPoint()s may be and still be merged (0.0 - the default -
  // requires them to be bit-identical); the discarded vertex's own
  // position is simply dropped, every face that used it now meeting at
  // the kept vertex's unchanged position, so a nonzero tolerance is a
  // real "snap together" - the two need not be perfectly coincident
  // first.
  //
  // Returns false, unchanged, if: `keep_vertex_id`/`discard_vertex_id`
  // don't identify two distinct vertices of the current subdivision
  // level; the distance between them exceeds `weld_tolerance`; the two
  // vertices are already connected by an edge, or already share a face
  // as two of its own distinct corners (merging them would collapse that
  // edge/face to fewer distinct components - refused rather than
  // guessing, the same ambiguity convention `InsertEdge()`'s own "already
  // adjacent" refusal above uses); or `ON_SubD::DeleteComponents`/
  // `FindOrAddFace` themselves refuse for any other reason. Returns true
  // only once the discarded vertex genuinely no longer exists and every
  // face that used to touch it is reattached to the kept vertex instead.
  bool Weld(unsigned int keep_vertex_id, unsigned int discard_vertex_id, double weld_tolerance = 0.0);

  // The EXACT limit-surface point (and normal) of every vertex of the
  // current subdivision level's control net, in ON_SubD's own vertex
  // iteration order - one SubDLimitPoint per VertexCount(). This is
  // genuine limit evaluation, not "subdivide a lot": the point each
  // control vertex converges to under infinitely many Catmull-Clark
  // refinements, in closed form.
  //
  // CORRECTS this class's own long-standing claim (see the comment
  // above, and the README's "What's still not done") that OpenNURBS'
  // public API ships no exact limit-surface evaluator: it does, for the
  // vertices. `ON_SubDVertex::SurfacePoint()`/`SurfaceNormal()` are
  // implemented in opennurbs_subd_eval.cpp (verified by reading the
  // source, not assumed - a real sector-based computation that walks
  // the vertex's incident faces via ON_SubDSectorIterator and fills the
  // vertex's cached limit point/normal, unlike the `BrepForm()`/
  // `CreaseEdgeCount()` stubs this file already documents). Verified
  // numerically too, not just by code reading: on a cube cage the limit
  // points land exactly where the standard closed-form Catmull-Clark
  // limit mask puts a valence-3 vertex, `(n^2 v + 4 sum(edge neighbors)
  // + sum(diagonal neighbors)) / (n (n + 5))` (Halstead, Kass & DeRose
  // 1993), and repeated `Subdivide()` visibly converges toward them.
  //
  // What this still is NOT: evaluation at an arbitrary (u, v) inside a
  // face - only the vertices' own limit positions. Away from
  // extraordinary vertices `ToNurbsPatches()`'s exact regular patches
  // already cover the face interiors; EvaluateFace() below closes most
  // of the remaining irregular-face-interior gap.
  //
  // Throws std::runtime_error if OpenNURBS can't evaluate a vertex's
  // limit point (a vertex with no faces, e.g. from a degenerate control
  // mesh) - never silently returns a NaN position.
  std::vector<SubDLimitPoint> LimitPoints() const;

  // Position, unit normal, and raw tangent vectors of the Catmull-Clark
  // limit surface at parameter (u, v) - (0, 0) at face(face_id)'s own
  // Vertex(0), (1, 0) at Vertex(1), (1, 1) at Vertex(2), (0, 1) at
  // Vertex(3) - inside the CURRENT subdivision level's face identified
  // by `face_id` (ON_SubDFace::m_id, as returned by e.g. ToNurbsPatches()
  // or a face iterator, NOT an array index). Unlike ToNurbsPatches(),
  // which only gives the exact limit surface over a REGULAR face (all 4
  // corners ordinary-interior), this genuinely improves on the
  // irregular case too - not just "subdivide a lot and hope".
  //
  // How: away from any extraordinary vertex/crease/boundary, this is
  // just ToNurbsPatches()'s own regular-patch construction (same
  // stencil-gathering, same B-spline-to-Bezier conversion), evaluated
  // directly at (u, v) via the standard bicubic Bezier position/partial-
  // derivative formulas - `exact = true`.
  //
  // Over an irregular face, one real level of Catmull-Clark refinement
  // (`ON_SubD::GlobalSubdivide` on an internal working copy - `raw()` is
  // untouched) always turns 3 of the face's 4 quadrants fully regular
  // (an ordinary-interior vertex's valence never changes under
  // subdivision, and every newly-created edge/face-point vertex has
  // valence exactly 4 - the only quadrant that can stay irregular is the
  // one touching the original extraordinary/crease/boundary vertex).
  // The correct child face for (u, v)'s quadrant is found without
  // guessing at OpenNURBS' internal numbering: `ON_SubDFace::
  // SubdivisionPoint()`/`ON_SubDEdge::SubdivisionPoint()` (the same real,
  // non-stub methods `GlobalSubdivide` itself uses - verified by reading
  // opennurbs_subd.cpp) give the EXACT position the refined face-point/
  // edge-points will land at, `ON_SubD::FindVertex()` (the same lookup
  // SetCrease()/SetEdgeSharpness() above already rely on) relocates them
  // in the refined copy, and the one still-unknown 4th corner of the
  // face touching all three IS the target child - no assumption that
  // ids or pointers survive subdivision. This repeats up to
  // `max_adaptive_levels` times (each level only re-examines whichever
  // single quadrant remains irregular, but still by globally refining
  // the WHOLE internal working copy - there's no cheaper "just this
  // face's neighborhood" primitive plugged in here - so cost is
  // proportional to the working copy's OWN current size PER LEVEL, and
  // that size itself grows roughly 4x each level; a working copy already
  // past 500,000 faces stops refining further and falls back rather
  // than risk exhausting memory, regardless of `max_adaptive_levels`
  // left. A query at exactly a face corner's (u, v) - any face, any
  // level - always resolves immediately via that vertex's own real
  // `ON_SubDVertex::SurfacePoint()`/`SurfaceNormal()` instead (the same
  // ones LimitPoints() uses) rather than adaptive refinement at all;
  // `tangent_u`/`tangent_v` there are the zero vector, since the tangent
  // PLANE at an extraordinary/crease/boundary vertex needs the full
  // Catmull-Clark eigenbasis this class doesn't implement.
  //
  // If a quadrant is still irregular once `max_adaptive_levels` (or the
  // working-copy size cap above) is exhausted, this falls back to the
  // SAME tolerance-bounded corner interpolant ToNurbsPatches() uses for
  // a whole irregular face - just over that quadrant's own (already
  // once-or-more shrunk) corners instead of the original face's, which
  // is never a worse bound - and reports `exact = false`. `tangent_u`/
  // `tangent_v` from that fallback are the flat interpolant's own (still
  // well-defined, just not limit-accurate) partial derivatives.
  //
  // Throws std::runtime_error if `face_id` doesn't identify a face of
  // the current subdivision level, or that face isn't a quad (same
  // "Subdivide(1) first" limitation ToNurbsPatches() already documents
  // for a level-0 n-gon).
  SubDSurfacePoint EvaluateFace(unsigned int face_id, double u, double v,
                                int max_adaptive_levels = 4) const;

  // The SubD-level counterpart of Mesh::CheckReport - closing
  // PARITY_MAP.md's subd_mesh "SubD non-manifold/multi-body validity
  // checks" [partial] item, whose own PARITY_MAP text calls out that
  // IsValid() above was "a thin bool wrapper over ON_SubD::IsValid": a
  // caller could learn a SubD was broken, never how, nor whether it was
  // actually several disconnected pieces masquerading as one object (the
  // same "multi-body" question Brep::SplitDisjointPieces()'s own
  // ON_Brep::LabelConnectedComponents() already answers for Breps - SubD
  // had no counterpart at all). IsValid() itself is a structural
  // cross-reference check (do the vertex/edge/face tables agree with each
  // other); Check() answers a DIFFERENT, complementary question this class
  // never asked before: is the topology itself well-formed as a single
  // connected 2-manifold-with-boundary, the same "closed manifold" shape
  // Mesh::CheckReport::IsClosedManifold() already checks for meshes.
  struct SubDCheckReport {
    // Undirected edges used by exactly one face (the open boundary) -
    // ON_SubDEdge::FaceCount() == 1. Not itself a defect (an intentionally
    // open patch has these), same convention as Mesh::CheckReport::
    // naked_edges.
    int naked_edges = 0;
    // Undirected edges used by three or more faces -
    // ON_SubDEdge::FaceCount() >= 3. A SubD can be IsValid() (internally
    // self-consistent) and still have these; OpenNURBS' own limit
    // evaluation and GlobalSubdivide() have no defined behavior for them.
    int non_manifold_edges = 0;
    // Vertices whose incident faces do NOT form a single fan around the
    // vertex - a "bowtie"/pinch-point vertex, e.g. two otherwise-unrelated
    // cones of faces that happen to share only this one vertex. Computed
    // by union-finding the vertex's own incident faces via the faces they
    // share an edge with AT that vertex; more than one resulting group
    // means the faces don't form one fan. This is a genuinely different
    // condition from non_manifold_edges (a bowtie vertex can exist with
    // zero non-manifold edges - every edge at it still has only 1 or 2
    // faces).
    int non_manifold_vertices = 0;
    // Number of face-connected pieces the control net's faces fall into -
    // 1 for an ordinary single connected SubD, 0 if there are no faces at
    // all, 2+ for a "multi-body" SubD (e.g. two separate box cages built
    // once and never joined). Two faces are in the same piece if they
    // share an edge (any ON_SubDEdge with FaceCount() >= 2), transitively.
    // A caller finding this > 1 knows their SubD is actually several
    // unrelated pieces, something IsValid() alone never reveals (each
    // piece can be perfectly well-formed on its own).
    int body_count = 0;
    // Every non-manifold edge's two endpoint vertex ids (ON_SubDVertex::
    // m_id, stable across edits - not an array index), as (a, b) with
    // a < b - the localization non_manifold_edges' bare count doesn't
    // give by itself. One entry per such edge, in the order first
    // encountered walking the SubD's own edge list.
    std::vector<std::pair<unsigned int, unsigned int>> non_manifold_edge_list;
    // Every non-manifold (bowtie) vertex's own id (ON_SubDVertex::m_id),
    // one entry per such vertex, in the order first encountered walking
    // the SubD's own vertex list.
    std::vector<unsigned int> non_manifold_vertex_list;
    // True iff this SubD is a single connected piece with no non-manifold
    // edge or vertex - the SubD-level analog of Mesh::CheckReport::
    // IsClosedManifold() (naked_edges is deliberately excluded, same as
    // there: an intentionally open patch is still "clean").
    bool IsManifoldSingleBody() const {
      return non_manifold_edges == 0 && non_manifold_vertices == 0 && body_count <= 1;
    }
  };
  SubDCheckReport Check() const;

  const ON_SubD& raw() const { return subd_; }
  ON_SubD& raw() { return subd_; }

 private:
  ON_SubD subd_;
};

}  // namespace dino8::kernel
