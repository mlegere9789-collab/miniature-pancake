#pragma once

#include <opennurbs.h>

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
  // Throws std::runtime_error if OpenNURBS' own call fails.
  Mesh ToApproximateMesh() const;

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
  // already cover the face interiors; the irregular-face interior stays
  // approximate (see that method).
  //
  // Throws std::runtime_error if OpenNURBS can't evaluate a vertex's
  // limit point (a vertex with no faces, e.g. from a degenerate control
  // mesh) - never silently returns a NaN position.
  std::vector<SubDLimitPoint> LimitPoints() const;

  const ON_SubD& raw() const { return subd_; }
  ON_SubD& raw() { return subd_; }

 private:
  ON_SubD subd_;
};

}  // namespace dino8::kernel
