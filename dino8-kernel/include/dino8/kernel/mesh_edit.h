#pragma once

#include <utility>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// Edit-mode style operators on a triangle/quad Mesh that return a new Mesh (the input is never changed). Every function keeps
// the winding of the faces it does not touch; faces with more than four corners that an operator would produce are triangulated
// (convex or concave, by ear clipping in the face plane), because ON_Mesh stores triangles and quads only. Vertices that no
// face uses after the operation are dropped. Vertex positions are stored as floats, so positions are exact only to float
// precision.

// An undirected mesh edge as (smaller vertex index, larger vertex index).
using MeshEdgeKey = std::pair<int, int>;

// The edges shared by exactly two faces whose face normals differ by MORE than `angle_degrees` (the edge angle of the Edge Split
// modifier). Boundary edges and edges with three or more faces are never listed. Throws std::invalid_argument for a non-finite
// or negative angle.
std::vector<MeshEdgeKey> SharpMeshEdges(const Mesh& mesh, double angle_degrees);

// Splits `edges`: each vertex is duplicated once per group of incident faces that stay connected through the edges NOT listed,
// so the faces on the two sides of a split edge no longer share its two vertices (a duplicated vertex keeps its position).
// A vertex whose faces all stay connected keeps its single copy. Edges that are not in the mesh are ignored.
Mesh SplitMeshEdges(const Mesh& mesh, const std::vector<MeshEdgeKey>& edges);

// SplitMeshEdges(mesh, SharpMeshEdges(mesh, angle_degrees)).
Mesh EdgeSplitByAngle(const Mesh& mesh, double angle_degrees);

// Separates `faces` from the rest without moving anything: the edges between a listed face and an unlisted face are split.
// Throws std::invalid_argument for a face index out of range.
Mesh SplitOffFaces(const Mesh& mesh, const std::vector<int>& faces);

enum class MergeTarget { Center, First, Last, Point };

// Merges the listed vertices into one vertex: at their mean (Center), at the first / last listed vertex, or at `point`. Faces
// that name the merged vertex twice lose the repeated corner (a quad becomes a triangle); a face left with fewer than three
// distinct corners is removed. Throws std::invalid_argument for an empty list or an index out of range.
Mesh MergeVerticesAt(const Mesh& mesh, const std::vector<int>& vertices, MergeTarget target, Point3d point = Point3d::Origin);

enum class PokeCenter { MedianWeighted, Median, Bounds };

// Poke: every listed face is replaced by a fan of triangles around one new vertex placed at the face's center (the centroid of
// the polygon region for MedianWeighted, the mean of the corners for Median, the box centre of the corners for Bounds) moved by
// `offset` along the face normal. Unlisted faces are unchanged. Throws std::invalid_argument for an index out of range or a
// non-finite offset.
Mesh PokeFaces(const Mesh& mesh, const std::vector<int>& faces, double offset = 0.0, PokeCenter center = PokeCenter::MedianWeighted);

// Connect Vertex Path on one face: the face is cut along the chord between its corners `vertex_a` and `vertex_b` (vertex ids)
// into two faces. Throws std::invalid_argument when the face does not have both corners, or they are neighbours.
Mesh ConnectFaceVertices(const Mesh& mesh, int face, int vertex_a, int vertex_b);

// Smooth Vertices: for `repeat` rounds every vertex (or only `only_vertices` when not empty) moves to
// (1 - factor) * p + factor * (mean of the positions of the vertices it shares an edge with), the displacement limited to the
// enabled axes; every round reads the positions of the previous round. A vertex without neighbours stays. Throws
// std::invalid_argument for a factor outside [0, 1] or a negative repeat count.
Mesh SmoothMeshVertices(const Mesh& mesh, double factor, int repeat, bool x = true, bool y = true, bool z = true,
                        const std::vector<int>& only_vertices = {});

// Bisect: the mesh cut by the plane through `plane_point` with normal `plane_normal`. Every face that crosses the plane is cut
// in two along it, the new vertices on cut edges are shared by the faces on both sides, and corners within `plane_tolerance`
// of the plane are used as they are. `fill` closes each cut with new faces (outward normal away from the half that keeps it).
// `clear_outer` drops everything on the positive side of the normal, `clear_inner` everything on the negative side; with both
// off both sides stay in the one mesh. Faces that lie in the plane go with the positive side. Throws std::invalid_argument for
// a zero normal.
Mesh BisectMesh(const Mesh& mesh, Point3d plane_point, Vector3d plane_normal, bool fill = false, bool clear_inner = false,
                bool clear_outer = false, double plane_tolerance = 1e-5);

// Screw / Spin of an open or closed polyline `profile` about the axis through `axis_point` along `axis_direction`: the profile
// is copied `steps * iterations + 1` times, each copy turned by `angle_radians / steps` more than the one before and moved
// along the axis by `screw_offset / steps` more (so one iteration turns `angle_radians` and advances `screw_offset`; the next
// iteration continues from there), and consecutive copies are joined by quads whose winding follows the profile direction
// (a profile walked counter-clockwise in the (radius, axis) half plane gives outward normals). When one full turn closes
// onto itself (angle 2 pi, offset 0, one iteration) the first and last copy are the same ring. When `profile` is closed
// (`close_profile`) its last point connects back to its first. Throws std::invalid_argument for fewer than two profile points,
// steps < 1, iterations < 1, a zero axis or a non-finite angle or offset.
Mesh ScrewPolyline(const std::vector<Point3d>& profile, Point3d axis_point, Vector3d axis_direction, double angle_radians, int steps,
                   double screw_offset = 0.0, int iterations = 1, bool close_profile = false);

// Mask by vertices: the faces whose corners are ALL kept (a corner is kept when keep_vertex[i] != invert). `keep_vertex` must
// have one entry per vertex. Throws std::invalid_argument for a wrong size.
Mesh MaskMeshByVertices(const Mesh& mesh, const std::vector<bool>& keep_vertex, bool invert = false);

// Build: the first floor(face_count * f) faces in index order, with f = clamp((frame - start) / length, 0, 1) (reversed: 1 - f).
// Throws std::invalid_argument for length <= 0.
Mesh BuildFaces(const Mesh& mesh, double frame, double start, double length, bool reversed = false);

// Extrude Individual Faces: every listed face is extruded on its own along its face normal by `distance` (a negative distance
// goes inward): the face is moved, and a ring of quads joins it to the edges it left behind, even where listed faces were
// neighbours (their side walls then coincide on the original edge). Unlisted faces are unchanged. Throws std::invalid_argument
// for an index out of range.
Mesh ExtrudeIndividualFaces(const Mesh& mesh, const std::vector<int>& faces, double distance);

// Extrude Edges: every listed boundary-or-interior edge is duplicated and moved by `offset`; a quad joins each edge to its copy
// (winding p0, p1, p1 + offset, p0 + offset). The faces are unchanged. Throws std::invalid_argument for an edge that is not in
// the mesh.
Mesh ExtrudeMeshEdges(const Mesh& mesh, const std::vector<MeshEdgeKey>& edges, Vector3d offset);

// Dissolve Faces: every group of listed faces that touch along edges becomes ONE polygon bounded by the group's outline (stored,
// when it has more than four corners, as the triangles of its ear-clipping triangulation). Vertices that only the dissolved
// faces used and that are not on the outline disappear. A listed face alone, or unlisted faces, stay. Throws
// std::invalid_argument for an index out of range or a group whose outline is not one simple loop (a hole, or two groups meeting
// at a single vertex).
Mesh DissolveFaces(const Mesh& mesh, const std::vector<int>& faces);

// Make Edge/Face: adds one face on the listed vertices (3 or more, in loop order). When an edge of the new loop already exists in
// the mesh the face is wound opposite to the existing faces on that edge, so it closes a hole consistently; with no existing edge
// the given order is kept. Throws std::invalid_argument for fewer than three vertices, a repeated vertex or an index out of range.
Mesh MakeFace(const Mesh& mesh, const std::vector<int>& vertices);

// Select More / Select Less on a vertex selection (sorted vertex ids in and out). With `face_step` (the Blender default) More adds
// every vertex of every face that touches a selected vertex, Less removes every selected vertex that shares a face with an
// unselected vertex; without it, More adds the edge neighbours and Less removes the vertices with an unselected edge neighbour.
// Throws std::invalid_argument for a vertex out of range.
std::vector<int> GrowVertexSelection(const Mesh& mesh, const std::vector<int>& selected, bool face_step = true);
std::vector<int> ShrinkVertexSelection(const Mesh& mesh, const std::vector<int>& selected, bool face_step = true);

// Select Non-Manifold: edges with exactly one face (`boundary`), with three or more faces (`multiple_faces`), and edges with two
// faces wound against each other (`non_contiguous`). Sorted.
std::vector<MeshEdgeKey> NonManifoldMeshEdges(const Mesh& mesh, bool boundary = true, bool multiple_faces = true, bool non_contiguous = true);

// Select Linked Flat Faces: the faces reachable from `seed_face` across shared edges whose two faces have normals at most
// `angle_degrees` apart (edges with more than two faces are not crossed). Sorted. Throws std::invalid_argument for a seed out of
// range or a negative angle.
std::vector<int> FlatLinkedFaces(const Mesh& mesh, int seed_face, double angle_degrees);

// Select Shortest Path: the vertex path from `from` to `to` along mesh edges with the smallest total edge length (or the fewest
// edges when `topology_distance`); empty when `to` cannot be reached. Throws std::invalid_argument for a vertex out of range.
std::vector<int> ShortestEdgePath(const Mesh& mesh, int from, int to, bool topology_distance = false);

// Loop Select: the edge loop through the edge (a, b) as a vertex sequence. At a vertex with four edges the loop continues along
// the edge that shares no face with the edge it came in on; along a boundary it continues along the next boundary edge; anywhere
// else it stops. `closed` is true when the walk returns to its start. Throws std::invalid_argument for an edge that is not in
// the mesh.
struct MeshEdgeLoop {
  std::vector<int> vertices;
  bool closed = false;
};
MeshEdgeLoop EdgeLoopThrough(const Mesh& mesh, int a, int b);

// ---- Round 3, batch 2 ----

// Dissolve Edges: the two faces on each listed edge (an edge with exactly two faces) become one polygon, as DissolveFaces does for
// a face list, but only across the listed edges. Edges with one or three faces are ignored. Throws std::invalid_argument for an edge
// that is not in the mesh or an outline that is not one simple loop.
Mesh DissolveEdges(const Mesh& mesh, const std::vector<MeshEdgeKey>& edges);

// Dissolve Vertices: every listed interior vertex (all its edges have two faces) disappears and the faces around it become one
// polygon. Throws std::invalid_argument for a vertex out of range or on the boundary.
Mesh DissolveVertices(const Mesh& mesh, const std::vector<int>& vertices);

// Delete Edge Loop: the loop through the edge (a, b) (EdgeLoopThrough) is dissolved edge by edge, and the loop vertices that are
// left with two edges in a straight line are removed, so a quad grid loses one row or column of quads. Throws std::invalid_argument
// for an edge that is not in the mesh.
Mesh DeleteEdgeLoop(const Mesh& mesh, int a, int b);

// Extrude Region: each group of listed faces that touch along edges is extruded as one piece along its own (area weighted) normal
// by `distance`; walls join the moved faces to the outline of the group, so edges between two listed faces get no wall. Throws
// std::invalid_argument for an index out of range.
Mesh ExtrudeFaceRegion(const Mesh& mesh, const std::vector<int>& faces, double distance);

// Select Linked: the vertices reachable from `seed_vertex` along edges, sorted.
std::vector<int> LinkedVertices(const Mesh& mesh, int seed_vertex);

enum class SidesCompare { Less, Equal, Greater, NotEqual };

// Select Faces by Sides: the faces whose corner count compares to `sides` (a mesh here holds triangles and quads only).
std::vector<int> FacesBySides(const Mesh& mesh, int sides, SidesCompare compare);

// Select Mirror: for every selected vertex the vertex at its position mirrored in the plane through the origin normal to `axis`
// (0 x, 1 y, 2 z), within `threshold`; sorted, without duplicates. A vertex with no counterpart contributes nothing.
std::vector<int> MirroredVertices(const Mesh& mesh, const std::vector<int>& selected, int axis, double threshold = 1e-4);

enum class AxisSide { Positive, Negative, Aligned };

// Select Axis: the vertices whose coordinate on `axis` is above (Positive), below (Negative) or within `threshold` of (Aligned) that
// of `active_vertex`; sorted. The active vertex is listed only for Aligned.
std::vector<int> VerticesByAxis(const Mesh& mesh, int active_vertex, int axis, AxisSide side, double threshold = 1e-4);

// Beautify Faces on triangle pairs: every edge shared by exactly two triangles whose union is a convex quad within `angle_limit_degrees`
// of coplanar is flipped when that raises the smallest of the six interior angles; repeated until no flip helps (at most 4 * faces
// rounds). Quads and edges that do not qualify are untouched.
Mesh BeautifyTriangles(const Mesh& mesh, double angle_limit_degrees = 180.0);

// Make Planar Faces: for `repeat` rounds every corner of every listed face moves toward the plane through the face's corner mean
// with the face's Newell normal by `factor` (1 puts it on the plane); a vertex shared by several listed faces moves by the mean of
// their moves. Throws std::invalid_argument for a factor outside [0, 1], repeat < 0 or an index out of range.
Mesh MakeFacesPlanar(const Mesh& mesh, const std::vector<int>& faces, double factor = 1.0, int repeat = 1);

// Primitives (centred on the origin, axis +z, faces wound outward; vertices on the circle start at angle 0).
enum class CapFill { Nothing, NGon, TriFan };
Mesh MakeCubeMesh(double size = 2.0);                                         // 8 vertices, 6 quads
Mesh MakePlaneMesh(double size = 2.0);                                        // 1 quad, normal +z
Mesh MakeCircleMesh(int vertices = 32, double radius = 1.0, CapFill fill = CapFill::Nothing);
Mesh MakeCylinderMesh(int vertices = 32, double radius = 1.0, double depth = 2.0, CapFill end_fill = CapFill::NGon);
Mesh MakeIcoSphereMesh(int subdivisions = 2, double radius = 1.0);           // 1 = icosahedron; faces 20 * 4^(s - 1)
Mesh MakeTorusMesh(int major_segments = 48, int minor_segments = 12, double major_radius = 1.0, double minor_radius = 0.25);

}  // namespace dino8::kernel
