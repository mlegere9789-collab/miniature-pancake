#pragma once

#include <string>
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

  // Volume-weighted centroid (center of mass, assuming uniform density),
  // via the same divergence-theorem decomposition Volume() uses: each
  // triangle (plus the origin) forms a tetrahedron whose own centroid is
  // the average of its 4 vertices and whose signed volume is already
  // exactly what Volume() sums; the mesh's centroid is the volume-weighted
  // average of those per-tetrahedron centroids. Only meaningful for a
  // closed, consistently-oriented mesh, same requirement as Volume() (and
  // for the same reason - GetBoundingBox() computes a plain vertex
  // average/extent instead, which needs no such assumption). Throws
  // std::invalid_argument if the mesh's volume is (near) zero - the
  // centroid of an open surface or a degenerate/zero-volume solid isn't
  // well-defined by this formula (it would divide by ~0).
  Point3d GetCentroid() const;

  // Sum of face areas (each via half the cross-product magnitude of its
  // one or two triangles - a quad face's second triangle is included,
  // same as Volume()'s own IsQuad() handling). Unlike Volume(), meaningful
  // for open surfaces too - e.g. a single trimmed planar face isn't
  // closed, so Volume() doesn't apply to it.
  double Area() const;

  // Axis-aligned bounding box over every vertex, regardless of whether
  // it's actually used by a face - a real, if narrow, gap: nothing
  // earlier in this file could answer "roughly how big/where is this,"
  // which any future viewport (camera framing) or spatial query (a
  // coarse overlap test before a real boolean) needs. Throws
  // std::invalid_argument on a mesh with no vertices, rather than
  // returning a degenerate all-zero box that would look like a valid
  // point-sized mesh at the origin.
  BoundingBox GetBoundingBox() const;

  // Whether `point` lies inside this mesh - a real "is this point part
  // of the solid" query nothing here could answer before (every existing
  // query - Volume(), GetCentroid(), GetBoundingBox() - describes the
  // solid as a whole, not a specific point's relationship to it). Uses
  // the standard ray-casting rule: casts a ray from `point` in the fixed
  // +X direction and counts how many of the mesh's triangles it crosses
  // (a quad face's own two triangles, same split Area()/Volume() already
  // use, each counted independently) - an odd count means `point` is
  // inside. Only meaningful for a closed, consistently-oriented mesh
  // (IsClosedManifold()), the same requirement Volume() already has, for
  // the same reason: an open surface has no well-defined "inside" at
  // all. `point` exactly on the boundary, or a ray that happens to pass
  // exactly through an edge or vertex, is an unhandled degenerate case
  // (the standard caveat any single-direction ray-cast test has) - not
  // hardened against here.
  bool ContainsPoint(Point3d point) const;

  // Per-vertex normals: for each vertex, the area-weighted sum of every
  // adjacent face's own flat (non-normalized) triangle normal, then
  // normalized - the standard "average of what touches this vertex,
  // weighted by how much surface each neighbor actually covers" smoothing
  // normal, not a placeholder or a plain unweighted average. A quad
  // face's own two triangles (the same diagonal split Area()/Volume()
  // already use) are summed separately rather than treating the quad as
  // one unit, so a vertex on a non-planar quad still gets a real
  // per-triangle contribution instead of one undefined "quad normal".
  // Returns one entry per vertex, in vertex-index order, aligned with
  // Mesh's own vertex indices; a vertex with no adjacent faces gets the
  // zero vector (nothing to average).
  std::vector<Vector3d> ComputeVertexNormals() const;

  // Returns a copy of this mesh with every face's winding reversed (each
  // face's own vertex loop reversed in place, not the vertex list
  // reordered) - flipping which side is "outward" without moving a single
  // vertex. The missing piece for a mesh built (or loaded) with the wrong
  // handedness: everything else here (Volume(), ComputeVertexNormals(),
  // BooleanCombine()) assumes CCW-from-outside winding and silently gives
  // a sign-flipped or inside-out answer otherwise, with nothing earlier
  // to correct it after the fact. Flipping twice is an exact involution -
  // FlipNormals().FlipNormals() reproduces the original mesh's vertex
  // order exactly, not just an equivalent one.
  Mesh FlipNormals() const;

  // Whether this mesh is a closed, consistently-oriented 2-manifold - the
  // exact precondition Volume()/GetCentroid()/BooleanCombine() all
  // silently assume rather than check. Two independent conditions, both
  // required: every edge borders exactly 2 faces (closed - no boundary,
  // and no non-manifold edge shared by 3+ faces), and no directed edge
  // (a, b) appears twice (consistent orientation - two adjacent faces
  // that both "walk" a shared edge the same way, rather than opposite
  // ways, means one of them is wound backwards relative to the other).
  // Built directly from this mesh's own face list rather than by running
  // a boolean and checking whether Manifold accepted it - a real
  // diagnostic that answers the question directly, not a side effect of
  // an unrelated operation.
  bool IsClosedManifold() const;

  // Applies `xform` to a copy of this mesh and returns it - the missing
  // piece that let every primitive here be positioned/oriented only via
  // its own constructor parameters (Cylinder()'s base_center/axis, say),
  // with no way to move, rotate, or scale a mesh already built. Delegates
  // directly to ON_Mesh::Transform (verified as a real, working
  // implementation, not a stub like ON_Brep::CreateMesh) rather than
  // reimplementing per-vertex transformation here. Callers build `xform`
  // from OpenNURBS' own factories (already available via the <opennurbs.h>
  // this header already includes) - e.g.
  // ON_Xform::TranslationTransformation(offset) or an ON_Xform whose
  // Rotation(angle_radians, axis, center) member sets a rotation - rather
  // than this class adding narrower Translate()/Rotate()/Scale() wrappers
  // around the same thing.
  Mesh Transform(const ON_Xform& xform) const;

  // Writes this mesh as a plain-text Wavefront .obj file (`v x y z`
  // vertex lines, `f i j k` / `f i j k l` 1-indexed face lines - OBJ
  // supports quad faces natively, so a quad face is written as one
  // 4-index line rather than split into two triangles). Also writes each
  // vertex's own `vn` line, via ComputeVertexNormals(), and references it
  // from every face line in `v//vn` form (no texture coordinates, so the
  // middle slot is left empty, same as OBJ's own convention for "no vt"),
  // so a viewer gets real smooth-shading normals instead of falling back
  // to its own flat per-facet ones. This is the first "other file format"
  // this kernel writes, alongside the .3dm support in file_io.h - a
  // deliberately simple, widely-supported format so anything built here
  // can actually be opened and looked at in an ordinary 3D viewer
  // (Blender, MeshLab, etc.), not just verified by its own numbers.
  // Returns Result::Failed if the file can't be opened for writing; does
  // not validate the mesh's own geometry (an empty mesh writes a valid,
  // empty .obj).
  Result SaveObj(const std::string& path) const;

  // Reads a plain-text Wavefront .obj file written by SaveObj() (or any
  // other reasonably well-formed .obj) into `out_mesh`. Only `v` (vertex)
  // and `f` (face) lines are understood - `vn` (including the ones
  // SaveObj() itself now writes), texture coordinates, materials, groups,
  // and negative (relative) indices are all silently skipped, since a
  // face's geometry is already fully determined by its vertex indices
  // alone; round-tripping through SaveObj()/LoadObj() reproduces the same
  // geometry (and the same normals, recomputed from it) but not
  // necessarily the same file bytes. A face line with
  // more than 4 indices is rejected rather than silently fan-triangulated
  // (this kernel's own ON_MeshFace only holds a triangle or quad, so
  // reading, say, a 5-gon would need to change its meaning without
  // telling the caller). Returns Result::Failed if the file can't be
  // opened, a face line references a vertex index that doesn't exist yet
  // (must appear before any face referencing it, same requirement any
  // valid .obj already satisfies), or a face has more than 4 or fewer
  // than 3 indices - `out_mesh` is left unspecified in that case, not
  // partially filled and silently trusted.
  static Result LoadObj(const std::string& path, Mesh& out_mesh);

  // Writes this mesh as an ASCII Wavefront `.stl` file - the second
  // "other file format" here, aimed at the specific tools/workflows that
  // want STL rather than OBJ (3D printing slicers in particular). Unlike
  // `.obj`, STL is triangle-only and carries no shared vertex list - each
  // facet repeats its own 3 vertex positions, and a quad face
  // (`ON_MeshFace::IsQuad()`) is split into its two triangles rather than
  // written as a single facet, since the format has no quad facet at all.
  // Each facet's normal is computed directly from its own 3 vertices
  // (`(v1-v0) x (v2-v0)`, normalized) rather than written as the
  // permitted-but-not-required all-zero placeholder, so the file is
  // actually useful to a consumer that reads facet normals. Returns
  // Result::Failed if the file can't be opened for writing.
  Result SaveStl(const std::string& path) const;

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

  // Revolves a 2D profile fully around `axis` into a closed solid of
  // revolution (a lathe operation) - the general answer to "no revolve"
  // that Cylinder()/Cone() don't cover (constant or linearly-tapering
  // radius only). `profile[i] = (radius, height)`: radius >= 0 measured
  // from `axis`, height measured along `axis` from `axis_point`.
  //
  // An end whose radius is 0 (lies on the axis) is closed with a triangle
  // fan to a single shared apex vertex, the same way ConeToApex() closes
  // a cap; an end with nonzero radius instead gets a flat circular disc
  // cap (a center vertex plus a fan to that end's ring, oriented outward:
  // -axis at the start, +axis at the end - the same orientation
  // ExtrudeCappedSolid()'s own caps use). Mixing the two is fine (e.g. an
  // on-axis start tapering to an off-axis end, closed with a flat disc
  // there). Throws std::invalid_argument if `profile` has fewer than 2
  // points (fewer leaves nothing to revolve into a solid).
  //
  // Every profile point becomes either a single apex vertex (on-axis end)
  // or a `revolve_segments`-vertex ring (everywhere else, including an
  // off-axis end). No MergeAndWeld() is needed: each ring's vertices are
  // shared directly by the band before/after it and by that end's own cap
  // fan if it has one (an on-axis end's fan reuses the same apex vertex
  // for every triangle), so the result is already a single closed mesh -
  // same "exact shared vertices, no welding tolerance" property as
  // ExtrudeCappedSolid() and ConeToApex().
  static Mesh RevolveProfile(const std::vector<Point2d>& profile, Point3d axis_point,
                              Vector3d axis, int revolve_segments = 48);

  // Lofts a sequence of closed polygonal cross-sections ("rings") into a
  // closed solid - the general answer to "no loft" that RevolveProfile()
  // doesn't cover (a shape that changes profile shape along its length,
  // not just radius). Every ring must have the same vertex count
  // (>= 3) and must be listed in the same rotational order: CCW as seen
  // looking from beyond the last ring back toward the first (the same
  // "u_dir x v_dir = outward normal" convention this file already uses
  // everywhere else) - not validated here, since checking a ring's
  // winding requires assuming it's planar, which correctly building the
  // two end caps below already requires. The first and last rings are
  // closed off with an ear-clipping triangulation each (each ring's own
  // Newell-normal-derived 2D projection, via
  // dino8::kernel::detail::EarClipTriangulate - the same triangulator
  // TessellateGridClippedExact() uses for a concave trim), so a ring may
  // be concave, not just convex. The first and last rings must still be
  // planar and simple/non-self-intersecting - both now checked (planarity
  // via a relative-tolerance out-of-plane distance against a normal found
  // from the ring's own points; simplicity via
  // dino8::kernel::detail::IsSimplePolygon on that ring's own 2D
  // projection - the same check and requirement
  // TessellateGridClippedExact() applies to its trim_polygon); interior
  // rings only feed bands and aren't checked either way. Throws
  // std::invalid_argument if fewer than 2 rings are given, ring vertex
  // counts don't match, or the first/last ring is non-planar or
  // self-intersecting.
  //
  // No MergeAndWeld() is needed: consecutive rings' vertices are shared
  // directly between the band before and after them, and each end cap's
  // fan reuses that ring's own vertices - same "exact shared vertices, no
  // welding tolerance" property as ExtrudeCappedSolid(), ConeToApex(),
  // and RevolveProfile().
  static Mesh LoftClosedRings(const std::vector<std::vector<Point3d>>& rings);

  // Builds a real torus: a circular tube of `minor_radius`, swept around
  // `axis` at `major_radius` from `center`. Doesn't fit any earlier
  // primitive's shape: RevolveProfile()'s profile must start and end on
  // the axis, but a torus's circular cross-section never touches the
  // axis at all (it's a full loop offset from it) - a genuinely different
  // case, not a special case of RevolveProfile() with different
  // parameters. Built directly as a `major_segments` x `minor_segments`
  // quad grid that wraps in *both* directions (unlike Cylinder()/Cone(),
  // there's no boundary anywhere on a torus, so no end caps or
  // ExtrudeCappedSolid()/ConeToApex() call is needed - the grid is
  // already a closed manifold by construction).
  //
  // The winding was derived independently from Cylinder()'s/
  // RevolveProfile()'s (a torus isn't built from either), but checks
  // against the same standing rule this file always uses: parameterizing
  // by (major angle, minor angle) and evaluating (d/d-major-angle) x
  // (d/d-minor-angle) at the tube's outer equator gives the radially
  // outward direction, confirming grid cell winding
  // tri1=(v(i,j),v(i+1,j),v(i+1,j+1)), tri2=(v(i,j),v(i+1,j+1),v(i,j+1))
  // (the same cell-winding convention TessellateGrid() uses) is correct
  // here too.
  //
  // Volume approaches (not exactly equals) the ideal
  // 2*pi^2*major_radius*minor_radius^2 as `major_segments`/
  // `minor_segments` increase, same caveat as Cylinder()/Cone()'s
  // circular approximation.
  static Mesh Torus(Point3d center, Vector3d axis, double major_radius, double minor_radius,
                     int major_segments = 48, int minor_segments = 24);

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
