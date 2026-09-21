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
// What OpenNURBS' public API does *not* give us: an exact limit-surface
// evaluator or mesher. GetControlNetMesh() only ever returns the current
// subdivision level's control net (a mesh of flat quads through the
// control points) - repeatedly subdividing and re-extracting that net is
// the standard "just subdivide a lot" approximation technique, not exact
// limit-surface evaluation. A real product needs the latter for accurate
// rendering at any zoom level; this chunk only adds the former.
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

  const ON_SubD& raw() const { return subd_; }
  ON_SubD& raw() { return subd_; }

 private:
  ON_SubD subd_;
};

}  // namespace dino8::kernel
