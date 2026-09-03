#pragma once

#include <opennurbs.h>

#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

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
  static SubD FromControlMesh(const Mesh& control_mesh);

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

  int FaceCount() const;
  int VertexCount() const;

  const ON_SubD& raw() const { return subd_; }
  ON_SubD& raw() { return subd_; }

 private:
  ON_SubD subd_;
};

}  // namespace dino8::kernel
