#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"

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

 private:
  friend class Brep;
  friend class NurbsSurface;
  ON_Mesh mesh_;
};

}  // namespace dino8::kernel
