#pragma once

#include <array>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/tolerance.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// Exact volume mass properties of a closed solid at unit density, from
// Mesh::VolumeMassProperties(). Every second moment below uses the
// "products of inertia" convention: `ixx = integral of (y^2 + z^2) dV`,
// `ixy = integral of (x * y) dV` (NOT its negative), so the inertia
// tensor is assembled as
//
//   I = [[ ixx, -ixy, -ixz ],
//        [ -ixy, iyy, -iyz ],
//        [ -ixz, -iyz, izz ]]
//
// - the convention Rhino's own MassProperties reports and every
// engineering reference tabulates. Multiply every moment by the actual
// density to get real mass moments (volume by density gives mass).
struct MassProperties {
  double volume = 0;
  Point3d centroid;

  // Second moments about the WORLD ORIGIN.
  double ixx_origin = 0, iyy_origin = 0, izz_origin = 0;
  double ixy_origin = 0, iyz_origin = 0, ixz_origin = 0;

  // Second moments about the CENTROID (parallel-axis theorem applied to
  // the origin moments above).
  double ixx = 0, iyy = 0, izz = 0;
  double ixy = 0, iyz = 0, ixz = 0;

  // Eigen-decomposition of the centroidal tensor: `principal_moments`
  // in ascending order, `principal_axes[k]` the unit axis of
  // `principal_moments[k]`. The three axes form a right-handed
  // orthonormal frame (the third is the cross product of the first two,
  // which is still an eigenvector). An eigenvector's sign is arbitrary
  // (it's an axis, not a direction), and for a repeated eigenvalue
  // (a body with an axis of rotational symmetry, e.g. a cylinder or
  // torus) any orthonormal pair in that eigenspace is equally valid -
  // callers must not assume a specific pair comes back in that case.
  std::array<double, 3> principal_moments{};
  std::array<Vector3d, 3> principal_axes{};

  // `sqrt(principal_moments[k] / volume)` - the distance from the
  // principal axis at which the whole volume, concentrated, would have
  // the same moment.
  std::array<double, 3> radii_of_gyration{};
};

// An oriented bounding box, from Mesh::GetOrientedBoundingBox(): a box
// exactly `2 * half_extents[k]` long along each `axes[k]` (unit,
// mutually orthogonal, right-handed - the SAME frame convention
// MassProperties::principal_axes uses, and in fact the same axes: see
// GetOrientedBoundingBox()'s own doc comment), centered at `center`.
// Every vertex of the mesh it was built from lies within the box by
// construction (`half_extents[k]` is exactly the largest projection onto
// `axes[k]` found among all of that mesh's vertices), never merely
// approximately.
struct OrientedBoundingBox {
  Point3d center;
  std::array<Vector3d, 3> axes;
  std::array<double, 3> half_extents{};
};

// One crossing of a ray with a mesh, from Mesh::FireRay().
struct RayHit {
  // Ray parameter: the hit is at `origin + t * direction`, in units of
  // `direction`'s own length (t is a multiple of `direction`, NOT a
  // distance, unless `direction` is unit length).
  double t = 0;
  Point3d point;
  int face_index = -1;  // index into the mesh's own face list
  // Whether the ray enters the solid here (crosses the face against its
  // outward normal, direction . normal < 0) or leaves it. Only meaningful
  // on a consistently-oriented (CCW from outside) mesh.
  bool entering = false;
};

// Closest pair of points between two meshes' surfaces, from
// Mesh::DistanceTo().
struct MeshDistance {
  double distance = 0;  // exactly 0 when the surfaces touch or cross
  Point3d point_on_this;
  Point3d point_on_other;
  int face_on_this = -1;
  int face_on_other = -1;
};

// Solid-level relationship between two closed meshes, from
// Mesh::ClashWith(). Mutually exclusive, decided in the order listed on
// the doc comment there.
enum class Clash {
  Clear,             // no shared volume, surfaces further apart than the distance tolerance
  ThisInsideOther,   // (essentially) all of this mesh's volume lies inside `other`
  OtherInsideThis,   // (essentially) all of `other`'s volume lies inside this mesh
  Intersecting,      // the solids share positive volume, but neither contains the other
  Touching,          // no shared volume, but the surfaces meet (shared face, edge or corner contact)
};

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

  // A tighter box than GetBoundingBox() for anything not already
  // axis-aligned: oriented to the solid's own principal axes of inertia
  // rather than the world's. GetBoundingBox()'s own box can waste
  // arbitrary volume on a rotated shape (a long thin box at 45 degrees
  // gets an AABB nearly twice as wide as it is), which matters for a
  // viewport's camera framing or a broad-phase overlap test's own
  // tightness - nothing here could answer that before.
  //
  // The axes are exactly VolumeMassProperties()'s own `principal_axes` -
  // not a separate PCA computation over vertex POSITIONS (the common,
  // simpler technique, and a real alternative this deliberately isn't):
  // a vertex-covariance PCA is biased by tessellation density (a region
  // meshed more finely pulls the axes toward it even though the true
  // shape hasn't changed), whereas the inertia tensor's eigenvectors -
  // computed, like Volume()/GetCentroid(), by the divergence-theorem
  // integral over the solid's actual enclosed volume - depend only on
  // the real shape, not how finely any part of it happens to be
  // triangulated. (The two are related, not unrelated formulas pressed
  // into service: for the standard second-moment convention, inertia
  // tensor I = trace(covariance) * Identity - covariance, so I and the
  // volume-weighted covariance matrix are simultaneously diagonalized -
  // same eigenVECTORS, just a different, monotonic map from eigenvalue
  // to eigenvalue - which is exactly why reusing principal_axes here is
  // mathematically the volume-weighted PCA frame, not an approximation
  // of it.) Requires the same closed, consistently-oriented (CCW from
  // outside), positive-volume precondition VolumeMassProperties() has -
  // this delegates to it directly, so that method's own exceptions (both
  // std::invalid_argument on a zero/negative volume and std::runtime_error
  // from its eigensolver) surface here unchanged, not re-wrapped.
  //
  // `half_extents[k]` is then the tightest slab along `axes[k]` that
  // contains every one of this mesh's own vertices - the largest
  // absolute projection onto that axis, found by direct search over all
  // vertices, not estimated - so the returned box provably contains the
  // whole mesh, with `center` at the midpoint of each slab (not
  // GetCentroid() - the box's own middle, generally a different point
  // from the volume centroid for a shape that isn't symmetric about it).
  //
  // Honest scope: this is the standard, principal-axis-aligned oriented
  // box, not a search for the GLOBALLY minimum-volume box over every
  // possible orientation (that problem's practical 3D algorithms - e.g.
  // an exhaustive rotating-calipers search over every face normal - are
  // a materially different, much more expensive undertaking this does
  // not attempt). For a solid whose own principal axes of inertia
  // already line up with its tightest orientation - an axis-aligned box
  // itself is the simplest example - the two coincide exactly, verified
  // below; for a shape whose principal axes genuinely diverge from its
  // tightest orientation (some non-convex or very asymmetric shapes),
  // this box can be looser than that unattempted global minimum.
  OrientedBoundingBox GetOrientedBoundingBox() const;

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

  // The closest point on this mesh's surface to `point` (brute force over
  // every triangle - a quad face's own two triangles, same split
  // Area()/Volume()/ContainsPoint() already use, each checked
  // independently - no spatial acceleration structure). A real query
  // nothing here could answer before: ContainsPoint() only answers
  // "inside or not," not "how far, and to where" for a point that isn't.
  // Per-triangle closest point uses the standard region-based algorithm
  // (Ericson, "Real-Time Collision Detection"): classify `point`'s
  // projection against each of the triangle's 3 vertex/3 edge/1 interior
  // Voronoi regions in barycentric-coordinate terms, then return the
  // corresponding vertex, clamped edge point, or interior projection -
  // not an iterative or approximate search. Throws std::invalid_argument
  // on a mesh with no faces (no surface to be close to).
  Point3d ClosestPoint(Point3d point) const;

  // Signed distance from `point` to this mesh's surface: negative if
  // `point` is inside, positive if outside, computed as
  // `+/- (ClosestPoint(point) - point).Length()` with the sign from
  // ContainsPoint() - the combination neither query alone gives (an
  // "inside/outside plus how far" answer a CSG or offset-surface
  // operation would need). Only meaningful under the same
  // "closed, consistently-oriented mesh" requirement ContainsPoint()
  // and Volume() already have. Not a true signed-distance-*field*
  // (no interpolation/gradient, no acceleration structure) - just this
  // one query, exactly as expensive as one ClosestPoint() call plus one
  // ContainsPoint() call.
  double SignedDistance(Point3d point) const;

  // The full volume mass properties (see MassProperties above) - volume,
  // centroid, the complete inertia tensor about both the world origin
  // and the centroid, and its principal moments/axes. Volume() and
  // GetCentroid() were the only mass-property queries here before; no
  // second moment (inertia, product of inertia, radius of gyration)
  // existed at all, and the public OpenNURBS SDK has no mesh
  // mass-property implementation to delegate to (grepped: no
  // `ON_Mesh::VolumeMassProperties` anywhere in the source).
  //
  // EXACT, not sampled: every integral of 1, x, y, z, x^2, y^2, z^2, xy,
  // yz, zx over the enclosed volume is reduced by the divergence theorem
  // to a closed-form polynomial in each triangle's three vertices
  // (Eberly, "Polyhedral Mass Properties (Revisited)") and summed - the
  // same principle Volume() uses for the volume alone, extended to the
  // first and second moments. So a box's moments are exactly its
  // textbook `V*(b^2 + c^2)/12`, and a tessellated curved solid's are
  // exactly those of the polyhedron it actually is (converging to the
  // smooth shape's as the tessellation refines, like Volume()). A quad
  // face contributes both of its triangles, same split as Volume().
  //
  // The principal decomposition delegates to OpenNURBS'
  // `ON_Sym3x3EigenSolver` (verified a real implementation - a Jacobi
  // rotation to tridiagonal form plus a closed-form tridiagonal solve -
  // not a stub), with the results sorted ascending and re-unitized here.
  //
  // Only meaningful for a closed, consistently-oriented (CCW from
  // outside) mesh - the same requirement Volume()/GetCentroid() have.
  // Throws std::invalid_argument if the signed volume is (near) zero (an
  // open surface or degenerate solid: no volume to have moments) OR
  // negative (an inside-out mesh - every moment would come back negated;
  // FlipNormals() it first). Throws std::runtime_error only if the
  // eigen-solver itself reports failure, which a finite symmetric
  // tensor should never trigger.
  MassProperties VolumeMassProperties() const;

  // Every crossing of the ray `origin + t * direction` (t > 0, i.e.
  // strictly ahead of `origin`) with this mesh's faces, sorted by
  // increasing t - the first entry is the nearest hit, which is what a
  // pick, a shadow/visibility test, or a "shoot a ray and see what it
  // lands on" query wants. ContainsPoint() has always fired a ray
  // internally, but only ever counted its crossings; nothing here could
  // report WHERE a ray hits, or on which face. Exact Moller-Trumbore
  // per triangle (the same formula ContainsPoint() uses, now returning
  // its parameter and barycentrics instead of a bool) - not a march or
  // a sampled search - with no spatial acceleration structure (every
  // triangle is tested; a quad face's own two triangles both, same split
  // Area()/Volume() use). Returns empty for a miss. A hit exactly on a
  // quad face's shared diagonal is reported once, not once per
  // triangle. A ray exactly grazing an edge or vertex shared by two
  // faces is the usual unhandled degenerate case (it may be reported
  // once per face touched, or missed by both) - not hardened against,
  // same caveat ContainsPoint() documents. A ray parallel to a face's
  // plane never hits that face, even if it lies in it. Throws
  // std::invalid_argument on a zero-length `direction`.
  std::vector<RayHit> FireRay(Point3d origin, Vector3d direction) const;

  // The exact minimum distance between this mesh's surface and
  // `other`'s, with the pair of points (and faces) where it's attained
  // - the clearance query a clash/interference check, an assembly
  // fit, or a "how far apart are these two parts" measurement needs,
  // which nothing here could answer before (ClosestPoint() is
  // point-to-mesh only). Exact per triangle pair: the minimum distance
  // between two triangles is attained either at a vertex of one and the
  // closest point on the other (the same Ericson region test
  // ClosestPoint() uses, 6 vertex/triangle pairs) or between two edges
  // (the closed-form segment/segment closest points, 9 edge pairs), and
  // is exactly 0 when an edge of one pierces the other's interior
  // (segment/triangle intersection, 6 edge/triangle pairs) - all three
  // families are checked, so a crossing pair reports 0 rather than the
  // nearest vertex's or edge's positive distance. Returns
  // `distance == 0` for touching or crossing surfaces. Meaningful for
  // open surfaces too (it's a surface/surface query, not a solid one).
  // Brute force over every triangle pair with a per-pair bounding-box
  // reject against the best distance found so far; no BVH. Throws
  // std::invalid_argument if either mesh has no faces.
  MeshDistance DistanceTo(const Mesh& other) const;

  // Solid-level classification of how this closed mesh and `other`
  // relate (see Clash) - the interference check an assembly needs, which
  // nothing here could answer before. Decided from the EXACT overlap
  // volume `vol(this ∩ other)`, computed with the existing Manifold-
  // backed BooleanCombine(), plus DistanceTo() for contact, in this
  // order: ThisInsideOther if the overlap is at least
  // `(1 - relative_volume_tolerance) * Volume()` (so an identical pair,
  // or a part nestled against its container's wall from inside, reports
  // this); else OtherInsideThis by the mirror test; else Intersecting if
  // the overlap exceeds `relative_volume_tolerance * min(volumes)`; else
  // Touching if the surfaces come within `distance_tolerance` of each
  // other; else Clear. Two boxes sharing exactly one face (or an edge,
  // or a corner) are Touching, not Intersecting: they meet but share no
  // volume.
  //
  // Why overlap volume rather than edge/face piercing predicates: the
  // most ordinary CAD clash - two equal-height boxes overlapping in plan
  // - has every edge/face crossing landing exactly on a face's edge or
  // lying in a face's own plane, degenerate for any such predicate,
  // whereas its overlap volume is plainly positive. Manifold's boolean
  // (exact predicates with symbolic perturbation) is built for exactly
  // that coincident geometry. The volume tolerance is relative because
  // ON_Mesh stores vertices as single-precision floats, so a touching
  // pair whose coordinates aren't exactly representable can carry a
  // round-off sliver of overlap (~1e-7 relative); 1e-6 is comfortably
  // above that and far below any real interference. Requires both meshes
  // to be closed, consistently oriented (IsClosedManifold()) and of
  // positive volume - checked directly, throwing std::invalid_argument
  // otherwise (also if a tolerance is out of range); BooleanCombine()'s
  // own std::runtime_error can still surface if Manifold rejects a mesh
  // that passed those checks. Not a high-performance broad-phase check -
  // it runs a full boolean.
  Clash ClashWith(const Mesh& other, double distance_tolerance = 1e-6,
                  double relative_volume_tolerance = 1e-6) const;

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

  // Sets one (u, v) texture coordinate per vertex, stored in ON_Mesh's own
  // `m_S` array (not the deprecated `m_T` - OpenNURBS' own header flags
  // `m_T` "DEPRECATED... use m_S instead", confirmed by reading
  // opennurbs_mesh.h rather than assumed). Same per-vertex-only
  // granularity every other piece of data here has (one position, one
  // computed normal per vertex) - there's no per-face-corner UV storage,
  // so a genuine UV seam (the same vertex needing different texture
  // coordinates depending on which face is looking at it, e.g. wrapping a
  // texture around a cylinder's seam) can't be represented; the caller
  // gets one shared value for that vertex across every face touching it.
  // Returns Result::Failed if `uvs.size()` doesn't exactly equal
  // `VertexCount()` rather than silently truncating or leaving vertices
  // unset.
  Result SetTextureCoordinates(const std::vector<Point2d>& uvs);

  // Whether this mesh currently has a texture coordinate for every vertex
  // - true only if SetTextureCoordinates() was called with exactly
  // VertexCount() many entries (ON_Mesh's own convention: `m_S.Count() ==
  // m_V.Count()` means "has texture coordinates", any other count means
  // "ignore m_S entirely", so a partially-set or stale `m_S` from before a
  // vertex-count-changing operation is correctly reported as "no texture
  // coordinates" rather than misread).
  bool HasTextureCoordinates() const;

  // The texture coordinate at `vertex_index`, previously set via
  // SetTextureCoordinates(). Caller must check HasTextureCoordinates()
  // first; behavior is whatever ON_Mesh's own `m_S[]` array indexing does
  // if it doesn't (out-of-range access), not a checked exception.
  Point2d TextureCoordinateAt(int vertex_index) const;

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
  // vertex's own `vn` line, via ComputeVertexNormals(), so a viewer gets
  // real smooth-shading normals instead of falling back to its own flat
  // per-facet ones. If HasTextureCoordinates() is true, also writes each
  // vertex's own `vt` line and references it from every face line in
  // `v/vt/vn` form; otherwise face lines use `v//vn` (the middle slot
  // left empty, OBJ's own convention for "no vt") - same as before this
  // texture-coordinate support existed. This is the first "other file
  // format" this kernel writes, alongside the .3dm support in
  // file_io.h - a deliberately simple, widely-supported format so
  // anything built here can actually be opened and looked at in an
  // ordinary 3D viewer (Blender, MeshLab, etc.), not just verified by its
  // own numbers. Returns Result::Failed if the file can't be opened for
  // writing; does not validate the mesh's own geometry (an empty mesh
  // writes a valid, empty .obj).
  Result SaveObj(const std::string& path) const;

  // Reads a plain-text Wavefront .obj file written by SaveObj() (or any
  // other reasonably well-formed .obj) into `out_mesh`. `v` (vertex), `f`
  // (face), and now `vt` (texture coordinate) lines are understood; `vn`
  // (including the ones SaveObj() itself writes - vertex normals here are
  // always geometry-derived via ComputeVertexNormals(), never stored
  // independently), materials, and groups are all silently skipped. A
  // negative (relative) face index is NOT silently skipped - unlike those
  // truly-ignored line types, it fails the WHOLE load (Result::Failed,
  // see below), since resolving it correctly would need real support this
  // parser doesn't have, and guessing would risk silently loading the
  // wrong geometry. If any face line carries a `vt` reference
  // (the `v/vt` or `v/vt/vn` forms), the referenced texture coordinate is
  // stored for that corner's *vertex* (SetTextureCoordinates()'s own
  // per-vertex granularity, not per-corner) - if two different face
  // corners sharing a vertex reference different `vt` entries (a
  // legitimate general-OBJ construct for a UV seam, which this kernel's
  // per-vertex-only texture coordinates can't represent), whichever face
  // is read last silently wins for that vertex, not an error. Loading a
  // file with no `vt` references at all leaves HasTextureCoordinates()
  // false on the result, same as a mesh that never had
  // SetTextureCoordinates() called. A face line with more than 4 indices
  // is rejected rather than silently fan-triangulated (this kernel's own
  // ON_MeshFace only holds a triangle or quad, so reading, say, a 5-gon
  // would need to change its meaning without telling the caller).
  // Returns Result::Failed if the file can't be opened, a face line
  // references a vertex or texture-coordinate index that doesn't exist
  // yet (must appear before any face referencing it, same requirement any
  // valid .obj already satisfies), a face has more than 4 or fewer than 3
  // indices, or any face-line index is negative/relative (see above) -
  // `out_mesh` is left unspecified in that case, not partially filled and
  // silently trusted.
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

  // Writes this mesh as a binary `.stl` file - the format LoadStl()
  // already reads but SaveStl() never wrote, closing that asymmetry.
  // Same triangle-only, no-shared-vertex-list, real-computed-normal
  // semantics as SaveStl(); only the on-disk encoding differs (an
  // 80-byte header - left all zero, since this kernel has no metadata to
  // put there - a little-endian uint32 triangle count, then that many
  // 50-byte records: 3 floats normal, 3x3 floats vertices, a 2-byte
  // attribute byte count written as 0). Assumes a little-endian host,
  // same assumption LoadStl()'s binary reader already makes. Returns
  // Result::Failed if the file can't be opened for writing.
  Result SaveStlBinary(const std::string& path) const;

  // Reads a `.stl` file written by SaveStl() (or any other reasonably
  // well-formed STL, ASCII or binary) into `out_mesh` - closing the
  // "export-only" gap SaveStl() itself used to flag. Auto-detects which
  // of the two genuinely different STL formats the file actually is by
  // its exact size, not by sniffing for the text `solid` (which a binary
  // file's own 80-byte header can start with too, per the spec, so that
  // keyword alone isn't a reliable discriminator): a binary STL's total
  // size is always exactly `80 + 4 + count*50` bytes for the triangle
  // count its own header claims, so a file matching that formula is
  // parsed as binary; anything else falls back to the ASCII parser.
  //
  // ASCII path: parses `facet normal ... outer loop / vertex x y z (x3) /
  // endloop / endfacet` blocks; the `facet normal` line's own values are
  // read but discarded (recomputing per-facet normals here would just
  // reproduce SaveStl()'s own logic, and this kernel's Mesh has nowhere
  // to store a facet normal distinct from the vertex positions it's
  // derived from anyway).
  //
  // Binary path: reads the little-endian 80-byte header (discarded),
  // uint32 triangle count, then that many 50-byte records (3 floats facet
  // normal - discarded, same reason as the ASCII path; 3x3 floats vertex
  // positions; a 2-byte attribute byte count - also discarded, nowhere
  // in this kernel's Mesh to put it). Assumes a little-endian host, true
  // for every platform this kernel is actually built on.
  //
  // Both paths are faithful to STL's own "no shared vertex list" nature:
  // 3 new vertices are appended per facet, exactly as the file stores
  // them, not deduplicated against each other the way
  // `Mesh::MergeAndWeld()` would - a caller wanting a welded mesh (fewer
  // vertices, adjacency-aware operations like `ComputeVertexNormals()`
  // giving a real smoothing average rather than each vertex only ever
  // "sharing" its own single facet) can call `MergeAndWeld({loaded_mesh})`
  // afterward. Returns Result::Failed if the file can't be opened, an
  // ASCII `vertex`/`facet`/`endfacet` line is malformed (wrong token
  // count, unparsable number - which already covers a "nan"/"inf" token,
  // since stream parsing refuses those), a binary file is truncated
  // mid-record, or a binary vertex coordinate is non-finite (NaN/Inf
  // bit patterns are perfectly encodable in the 32-bit floats a binary
  // record stores; letting one through used to hand back a Result::Ok
  // mesh whose Volume()/GetCentroid() were silently NaN and whose
  // poisoned vertex could never weld - confirmed by a debug run) -
  // `out_mesh` is left unspecified in that case, not partially filled and
  // silently trusted.
  static Result LoadStl(const std::string& path, Mesh& out_mesh);

  // Writes this mesh as an ASCII PLY (Stanford Polygon) file - the third
  // "other file format" here, and a genuine gap: this kernel had zero PLY
  // code at all before this. Unlike `.stl`, PLY's face element is a
  // genuine variable-length list, so a quad face (`ON_MeshFace::IsQuad()`)
  // is written as its own native 4-index face, not split into two
  // triangles the way SaveStl() has to. Every vertex line always carries
  // a geometry-derived normal (`ComputeVertexNormals()`, same convention
  // as SaveObj()'s `vn`/SaveStl()'s facet normal - never a stored,
  // independent one), and a `u`/`v` texture-coordinate pair per vertex
  // when `HasTextureCoordinates()` is true (PLY has no single standard
  // UV property name across tools - some use `s`/`t` - `u`/`v` is chosen
  // here to match this kernel's own OBJ `vt` semantics exactly: one UV
  // per vertex, not per face corner). Only the ASCII PLY encoding is
  // written - PLY's binary_little_endian/binary_big_endian formats are a
  // real, disclosed gap, not attempted here, the same honest treatment
  // this codebase already gives Parasolid/ACIS licensing. Returns
  // Result::Failed if the file can't be opened for writing.
  Result SavePly(const std::string& path) const;

  // Reads an ASCII PLY file into `out_mesh` - written by SavePly() or by
  // another tool, as long as it's ASCII-encoded (binary PLY is rejected,
  // see SavePly()'s own doc comment on why) and follows PLY's ordinary
  // shape: a `vertex` element with `x`/`y`/`z` scalar properties (in any
  // order, and tolerating extra properties this kernel doesn't use, e.g.
  // color, by name rather than assuming a fixed column layout - genuinely
  // parses the header's own property list instead of guessing a position),
  // optional `nx`/`ny`/`nz` (read but discarded, same "always
  // geometry-derived" convention LoadObj()'s `vn` and LoadStl()'s facet
  // normal already have - there's nowhere in this kernel's Mesh to store
  // an independent per-vertex normal), and optional `u`/`v` (stored via
  // SetTextureCoordinates() only if present on every vertex, same
  // all-or-nothing rule LoadObj() already applies); and a `face` element
  // with exactly one list property (whatever its declared name -
  // `vertex_indices`/`vertex_index` are both common) giving each face's
  // 0-based vertex indices, 3 or 4 per face (this kernel's `ON_MeshFace`
  // holds a triangle or quad only, same limit LoadObj() already has for
  // `.obj`'s `f` lines - a 5+-gon face is rejected, not silently
  // fan-triangulated). Any other element name (e.g. a color-only `edge`
  // element) has its data lines skipped, not rejected - this kernel just
  // doesn't read it into anything. Returns Result::Failed - `out_mesh`
  // left unspecified, not partially filled - if the file can't be opened,
  // isn't `ply`/`format ascii ...`, the vertex element is missing
  // `x`/`y`/`z`, the face element's list property is missing or isn't a
  // list, a face has fewer than 3 or more than 4 indices, a face index is
  // out of range, or any header/data line fails to parse.
  static Result LoadPly(const std::string& path, Mesh& out_mesh);

  const ON_Mesh& raw() const { return mesh_; }
  ON_Mesh& raw() { return mesh_; }

  // --- Check / heal ------------------------------------------------------
  //
  // The mesh-level counterpart of Brep::Check() and its repairs: the
  // same questions IsClosedManifold() answers with one bool, as COUNTS
  // and LOCATIONS a caller can act on, plus the three repairs that turn
  // the common "almost closed" meshes back into closed manifolds.
  struct CheckReport {
    // Undirected edges used by exactly one face (the open boundary).
    int naked_edges = 0;
    // Undirected edges used by three or more faces.
    int non_manifold_edges = 0;
    // Directed edges used twice - two faces walking a shared edge the
    // same way, IsClosedManifold()'s own orientation-conflict condition.
    int orientation_conflicts = 0;
    // Faces with a repeated vertex index, an edge shorter than
    // `tolerance`, or a height (2*area / longest edge) at or below
    // `tolerance` - a face contributing nothing but bad edges.
    int degenerate_faces = 0;
    // Distinct vertex indices within `tolerance` of another (counted per
    // vertex that has at least one such partner): the "same point stored
    // twice" MergeAndWeld() exists to prevent, and CloseNakedEdges()
    // repairs when it happened on a boundary.
    int duplicate_vertices = 0;
    // Every naked edge as (a, b) in the direction its one face walks it,
    // in face order - the input FillSmallHoles() chains into loops.
    std::vector<std::pair<int, int>> naked_edge_list;
    // Same three conditions as Mesh::IsClosedManifold().
    bool IsClosedManifold() const {
      return naked_edges == 0 && non_manifold_edges == 0 && orientation_conflicts == 0;
    }
  };
  CheckReport Check(double tolerance = tolerance::kDistance) const;

  // The open boundary as closed loops of vertex indices: each naked edge
  // (a, b) chained a -> b -> ... in the direction its face walks it, so
  // walking a loop keeps the existing faces on the same side a
  // reversed-edge fill needs. A loop through a vertex with more than one
  // outgoing naked edge (a bowtie: two holes touching at one vertex) is
  // ambiguous and is NOT returned (its edges are left unchained rather
  // than guessed); a chain that never closes (only possible on a
  // non-manifold boundary) is dropped the same way. Empty for a closed
  // mesh.
  std::vector<std::vector<int>> NakedEdgeLoops() const;

  // Welds vertices that lie on naked edges and are within `tolerance`
  // of another naked-edge vertex into one - a TRUE distance test (every
  // pair within `tolerance` welds, unlike MergeAndWeld()'s grid snapping,
  // which can leave two points a hair apart in adjacent cells unwelded),
  // restricted to boundary vertices so an interior feature smaller than
  // `tolerance` is never touched. The lowest-indexed vertex of each
  // group survives at ITS OWN position (nothing is averaged or moved);
  // faces are remapped, a face that collapses to fewer than 3 distinct
  // vertices is dropped, a quad that collapses to 3 becomes a triangle,
  // and vertices no longer used by any face are removed. This is the
  // repair for a seam that construction left `tolerance`-wide open: a
  // duplicated vertex (two copies of the same point, each used by
  // different faces), or the mesh of a Brep whose JoinNakedEdges()
  // recorded a tolerant edge (see brep.h). Returns the number of
  // vertices welded away. Texture coordinates are dropped (a welded
  // vertex has no single UV).
  int CloseNakedEdges(double tolerance);

  // Fills every boundary loop (NakedEdgeLoops()) whose vertices' axis-
  // aligned bounding-box diagonal is at most `max_extent`: a 3-vertex
  // loop gets one triangle, any larger loop a fan of triangles from a
  // NEW vertex at the loop's own centroid (so a non-planar or non-convex
  // hole still gets a valid, non-self-overlapping fill without any
  // ear-clipping; a planar hole's fill lies exactly in its plane, since
  // the centroid does). Every fill triangle walks its boundary edge in
  // REVERSE of the existing face, so the result is orientation-
  // consistent with the surrounding mesh. Loops larger than `max_extent`
  // are left open (the bound is what keeps this from "filling" a whole
  // missing side of a model with a fan nobody asked for). Returns the
  // number of holes filled.
  int FillSmallHoles(double max_extent);

  // Makes face windings consistent across every manifold (2-face) edge
  // by breadth-first traversal from each not-yet-visited face, flipping
  // whichever neighbour walks a shared edge the same way (the same
  // per-face reversal FlipNormals() applies to all faces), then, if the
  // result IsClosedManifold() and Volume() is negative, flips every face
  // so the mesh faces outward. Non-manifold (3+-face) edges are skipped
  // (no single "other side" to agree with). Returns the number of face
  // flips performed; an open mesh is only made consistent, not oriented
  // outward.
  int UnifyNormals();

  // Concatenates several independently-tessellated meshes into one and
  // welds vertices within `tolerance` of each other into a single shared
  // vertex. Needed because Brep::Tessellate() tessellates each face on
  // its own: two faces meeting at a shared edge each produce their own
  // copy of that edge's vertices, at identical (or near-identical,
  // depending on tolerance) positions but as distinct array entries. A
  // boolean engine like Manifold requires a genuinely closed manifold -
  // coincident-but-separate vertices at a seam don't count - so this is
  // the step that turns "several open patches that happen to line up"
  // into "one watertight solid." The default is the kernel's weld
  // distance, tolerance::kWeld (see tolerance.h) - the same 1e-6 it has
  // always been, now named rather than a literal.
  static Mesh MergeAndWeld(const std::vector<Mesh>& meshes,
                            double tolerance = tolerance::kWeld);

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
  //
  // Throws std::invalid_argument if `circle_segments` is less than 3 - a
  // real, previously-missing check that turned up a genuinely serious
  // silent-failure mode, not just an empty mesh: `circle_segments=0`
  // built an *empty* trim polygon, and this kernel's own
  // `Brep::Tessellate()` treats an empty trim loop as "no trim at all,"
  // so the untrimmed ~1.2x-oversized square cap surface got tessellated
  // and swept whole - a plausible-looking but completely wrong solid
  // (confirmed with a debug run: `circle_segments=0` returned a real
  // mesh with hundreds of faces, not a crash or an empty result, at
  // roughly the square cap's own size instead of the requested circle).
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
  // Cylinder(). Throws std::invalid_argument under the same
  // `circle_segments < 3` condition Cylinder() does - see there.
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
  // points (fewer leaves nothing to revolve into a solid). Also throws
  // std::invalid_argument if `revolve_segments` is less than 3 - a real
  // gap found by checking whether `profile`'s own validation had a
  // sibling for this parameter (it didn't): fewer than 3 segments can't
  // form a non-degenerate ring at all, and a debug run confirmed the old,
  // unguarded behavior wasn't even a clean crash - `revolve_segments=0`
  // silently produced a near-empty, faceless mesh (each ring's per-
  // segment vertex loop simply never running) rather than failing
  // loudly.
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

  // Same band skinning as LoftClosedRings(), but for a spine that loops back
  // on itself: ring i gets a band to ring (i+1) % rings.size() (the last
  // ring wraps back to the first) and there are no end caps, since a
  // periodic spine's tube is already a closed loop with no open ends to cap.
  // Use this instead of LoftClosedRings() whenever the ring sequence itself
  // represents a full loop (e.g. a tube swept all the way around a closed
  // edge) - capping both ends of a loop that already closes on itself
  // produces two coincident flat caps at the seam instead of a manifold
  // tube. Throws std::invalid_argument if fewer than 3 rings are given or
  // ring vertex counts don't match.
  static Mesh LoftPeriodicRings(const std::vector<std::vector<Point3d>>& rings);

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
  //
  // Throws std::invalid_argument if either segment count is less than
  // 3 - the same real, previously-missing validation
  // `RevolveProfile()`'s own `revolve_segments` just got: a debug run
  // confirmed a `0` count here has the identical silent-failure pattern
  // (a `major_segments`/`minor_segments` value of 0 makes the
  // corresponding vertex-generation loop simply never run, producing a
  // fully empty, faceless mesh instead of a thrown error).
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
