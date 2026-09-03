#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// Wraps ON_Mesh. OpenNURBS' polygon-mesh representation, produced by
// tessellating a Brep — this is as far as OpenNURBS' public API goes
// toward "meshing"; it has no boolean/CSG operations on top of it (see
// the note on Brep::Tessellate below).
class Mesh {
 public:
  int VertexCount() const;
  int FaceCount() const;

  // Signed volume via the divergence theorem (sum of signed tetrahedron
  // volumes from the origin to each triangle). Only meaningful for a
  // closed, consistently-oriented (CCW from outside) mesh - exactly the
  // kind BooleanCombine requires as input and produces as output.
  double Volume() const;

  // Sum of triangle areas (each via half the cross-product magnitude).
  // Unlike Volume(), meaningful for open surfaces too - e.g. a single
  // trimmed planar face isn't closed, so Volume() doesn't apply to it.
  double Area() const;

  const ON_Mesh& raw() const { return mesh_; }
  ON_Mesh& raw() { return mesh_; }

  // Concatenates several independently-tessellated meshes into one and
  // welds vertices within `tolerance` of each other into a single shared
  // vertex. Needed because Brep::Tessellate() tessellates each face on
  // its own: two faces meeting at a shared edge each produce their own
  // copy of that edge's vertices, at identical (or near-identical,
  // depending on tolerance) positions but as distinct array entries. A
  // boolean engine like Manifold requires a genuinely closed manifold -
  // coincident-but-separate vertices at a seam don't count - so this is
  // the step that turns "several open patches that happen to line up"
  // into "one watertight solid."
  static Mesh MergeAndWeld(const std::vector<Mesh>& meshes,
                            double tolerance = 1e-6);

  // Sweeps `cap` (any open mesh with a well-defined boundary loop - a
  // trimmed planar face's tessellation, an untrimmed one, or any other
  // manifold-with-boundary patch) along `offset` into a closed solid:
  // `cap` becomes one end as-is, a copy of it translated by `offset`
  // (with reversed winding) becomes the other end, and side walls are
  // generated to connect them.
  //
  // This is the general answer to the gap earlier chunks flagged
  // ("nothing here builds the matching edges/walls a real trimmed solid
  // needs"): rather than hand-deriving matching wall geometry per shape
  // (as Box() and a hypothetical Cylinder() would each need to), this
  // extracts `cap`'s boundary loop directly from its own triangle
  // adjacency (an edge used by exactly one triangle is a boundary edge)
  // and builds walls from that - so it works on any cap shape, including
  // Brep::TrimmedPlanarFace()'s jagged/staircased trim boundary, without
  // needing the wall geometry to be constructed to match some idealized
  // curve. No welding tolerance is involved: top, bottom, and wall
  // vertices at the shared seams reuse `cap`'s own vertex positions
  // exactly (translated for the far end), so the result is already a
  // single closed mesh - it does not need MergeAndWeld().
  //
  // Requires `cap` to have a single, simple (non-self-intersecting)
  // boundary loop - e.g. not already closed, and not multiply-connected
  // (a face with a hole isn't supported here).
  static Mesh ExtrudeCappedSolid(const Mesh& cap, Vector3d offset);

 private:
  friend class Brep;
  friend class NurbsSurface;
  ON_Mesh mesh_;
};

}  // namespace dino8::kernel
