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
  // `cap`'s boundary may be multiple disjoint loops (an annulus/washer
  // face - outer boundary plus a hole - extrudes to a tube with
  // independently-walled outer and inner surfaces), but every loop must
  // be simple: each boundary vertex must have exactly one boundary edge
  // leaving it and one arriving. Throws std::invalid_argument otherwise
  // (a self-intersecting or "bowtie" boundary, or a cap with no boundary
  // at all - i.e. already closed) rather than emitting overlapping or
  // malformed wall geometry.
  static Mesh ExtrudeCappedSolid(const Mesh& cap, Vector3d offset);

  // Builds a real cylinder: a circular disk cap (Brep::TrimmedPlanarFace()
  // with an N-gon trim polygon approximating a circle) swept along `axis`
  // by `height` via ExtrudeCappedSolid(). Returns Mesh rather than Brep
  // because it's already a closed-solid convenience, not a Brep
  // primitive - the wall geometry comes from ExtrudeCappedSolid's
  // boundary-edge extraction, not real trimmed-surface topology.
  //
  // This is the real test of ExtrudeCappedSolid() generalizing beyond a
  // rectangular trim boundary: the circle's N-gon trim is approximated
  // the same whole-cell-in/out way any TrimmedPlanarFace() is, so the
  // resulting solid's volume approaches (not exactly equals) the ideal
  // pi*r^2*h as circle_segments and the tessellation grid resolution
  // increase - unlike Box()/the rectangular trim tests, which hit exact
  // values by construction.
  static Mesh Cylinder(Point3d base_center, Vector3d axis, double radius,
                        double height, int circle_segments = 48,
                        int grid_divisions = 48);

  // Cones `cap`'s boundary loop to a single point `apex`, closing it into
  // a solid the way ExtrudeCappedSolid() closes it into a prism: `cap`
  // becomes the base as-is, and each boundary edge becomes one triangle
  // to `apex` instead of a translated-copy wall quad. Same boundary
  // requirements as ExtrudeCappedSolid() (a set of simple, disjoint
  // closed loops - one boundary edge leaving and one arriving at every
  // boundary vertex), and the same "no welding needed" property (`apex`
  // is a single new vertex all wall triangles share exactly).
  static Mesh ConeToApex(const Mesh& cap, Point3d apex);

  // Builds a real cone: a circular disk cap (Brep::TrimmedPlanarFace()
  // with an N-gon trim polygon approximating a circle), same construction
  // as Cylinder(), coned to a single apex point along `axis` at `height`
  // via ConeToApex() instead of swept via ExtrudeCappedSolid(). Volume
  // approaches (not exactly equals) the ideal (1/3)*pi*r^2*h as
  // circle_segments and grid_divisions increase, same caveat as
  // Cylinder().
  static Mesh Cone(Point3d base_center, Vector3d axis, double radius,
                    double height, int circle_segments = 48,
                    int grid_divisions = 48);

 private:
  friend class Brep;
  friend class NurbsSurface;

  // Shared by ExtrudeCappedSolid() and ConeToApex(): extracts `cap`'s
  // boundary edges from triangle adjacency (an edge used by exactly one
  // triangle is a boundary edge) and validates they form a set of simple,
  // disjoint closed loops, throwing std::invalid_argument (naming
  // `caller` in the message) otherwise - see ExtrudeCappedSolid()'s own
  // comment for why an already-closed cap or a bowtie/self-intersecting
  // boundary can't be trusted to "probably be fine."
  static std::vector<std::pair<int, int>> ExtractValidatedBoundaryEdges(
      const ON_Mesh& cap, const char* caller);

  ON_Mesh mesh_;
};

}  // namespace dino8::kernel
