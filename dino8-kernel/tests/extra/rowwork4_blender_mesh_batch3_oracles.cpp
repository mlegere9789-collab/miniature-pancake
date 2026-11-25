// Register row tests for the mesh-subd-sculpt factory, round 4 (batch 3, slice A). Every oracle is a closed form that does not use the
// code under test:
//  * Subdivide: a quad cut k times per edge becomes (k+1)^2 quads, a triangle (k+1)^2 triangles; with e edges and the Euler relation the
//    counts of a closed cube are V = 8 + 12 k + 6 k^2, F = 6 (k+1)^2 (quads) and the surface does not move.
//  * Multires levels: Catmull-Clark and simple subdivision of a cube both have F = 6 * 4^L, V = F + 2.
//  * Fill Grid: filling a hole of a unit sheet with the quads of its own lattice reproduces the lattice; a boundary on an affine plane
//    gives an affine interior (the Coons patch reproduces bilinear data exactly).
//  * Select Similar / More / Less / Linked: counts on a box 2 x 3 x 4 (areas 6, 8, 12), on lattices and on disjoint pieces.
//  * Mask filter: dilation and erosion of one vertex over a lattice give the diamonds with 1, 5, 13 vertices.
//  * Mesh filter Inflate: normals of a flat sheet are +z and those of the icosahedron are radial by symmetry.
//  * Trim: halves of a 10-cube by a plane, a through hole and a pocket by a box (volumes 300, 700, 500, 840, 936).
//  * Extract: a 2 x 2 patch of a unit lattice has area 4 and thickened by t volume 4 t, area 8 + 8 t.
//  * Unwrap: a developable strip has an isometric unrolling; the half cylinder of radius 3 turned through 6 steps unrolls to a strip of
//    width 6 * 2 * 3 sin(pi / 12).
//  * Average Islands Scale: after scaling, UV area / 3D area is one common value for every island.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/mesh_edit.h"
#include "dino8/kernel/mesh_unwrap.h"
#include "dino8/kernel/subd.h"
#include "dino8/kernel/vertex_weights.h"
#include "test_registry.h"

namespace {

using namespace dino8::kernel;

constexpr double kPi = 3.14159265358979323846;

bool Near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
int FaceSize(const ON_MeshFace& f) { return f.IsQuad() ? 4 : 3; }

struct Counts {
  int v = 0, e = 0, f = 0;
  int Euler() const { return v - e + f; }
};

Counts CountsOf(const Mesh& m) {
  const ON_Mesh& r = m.raw();
  std::set<std::pair<int, int>> edges;
  std::set<int> used;
  Counts c;
  for (int i = 0; i < r.m_F.Count(); ++i) {
    const ON_MeshFace& f = r.m_F[i];
    const int n = FaceSize(f);
    ++c.f;
    for (int k = 0; k < n; ++k) {
      const int a = f.vi[k], b = f.vi[(k + 1) % n];
      used.insert(a);
      edges.insert({std::min(a, b), std::max(a, b)});
    }
  }
  c.v = int(used.size());
  c.e = int(edges.size());
  return c;
}

int AddQuad(Mesh& m, int a, int b, int c, int d) {
  ON_MeshFace f;
  f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
  m.raw().m_F.Append(f);
  return m.raw().m_F.Count() - 1;
}

int AddTri(Mesh& m, int a, int b, int c) {
  ON_MeshFace f;
  f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = c;
  m.raw().m_F.Append(f);
  return m.raw().m_F.Count() - 1;
}

Mesh QuadBox(double x0, double y0, double z0, double x1, double y1, double z1) {
  Mesh m;
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) m.raw().m_V.Append(ON_3fPoint(float(x ? x1 : x0), float(y ? y1 : y0), float(z ? z1 : z0)));
  AddQuad(m, 0, 2, 3, 1); AddQuad(m, 0, 1, 5, 4); AddQuad(m, 2, 6, 7, 3); AddQuad(m, 0, 4, 6, 2); AddQuad(m, 1, 3, 7, 5); AddQuad(m, 4, 5, 7, 6);
  return m;
}

Mesh TriBox(double x0, double y0, double z0, double x1, double y1, double z1) {
  Mesh m;
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) m.raw().m_V.Append(ON_3fPoint(float(x ? x1 : x0), float(y ? y1 : y0), float(z ? z1 : z0)));
  const int quads[6][4] = {{0, 2, 3, 1}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}, {4, 5, 7, 6}};
  for (const auto& q : quads) {
    AddTri(m, q[0], q[1], q[2]);
    AddTri(m, q[0], q[2], q[3]);
  }
  return m;
}

// n x n quads of side `pitch` in z = 0, normal +z, vertex (i, j) = j * (n + 1) + i, face (i, j) = j * n + i.
Mesh FlatSheet(int n, double pitch) {
  Mesh m;
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i) m.raw().m_V.Append(ON_3fPoint(float(i * pitch), float(j * pitch), 0.0f));
  const auto id = [n](int i, int j) { return j * (n + 1) + i; };
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) AddQuad(m, id(i, j), id(i + 1, j), id(i + 1, j + 1), id(i, j + 1));
  return m;
}

// the same mesh keeping only the listed faces (vertices unchanged)
Mesh KeepFaces(const Mesh& m, const std::vector<int>& keep) {
  Mesh out;
  out.raw().m_V = m.raw().m_V;
  for (int fi : keep) out.raw().m_F.Append(m.raw().m_F[fi]);
  return out;
}

Point3d Vertex(const Mesh& m, int i) {
  const ON_3fPoint& p = m.raw().m_V[i];
  return Point3d(p.x, p.y, p.z);
}

bool HasVertexNear(const Mesh& m, Point3d p, double tol = 1e-5) {
  for (int i = 0; i < m.VertexCount(); ++i)
    if (Vertex(m, i).DistanceTo(p) <= tol) return true;
  return false;
}

char g_what[220];
const char* W(const char* fmt, double a = 0, double b = 0, double c = 0) {
  std::snprintf(g_what, sizeof g_what, fmt, a, b, c);
  return g_what;
}

}  // namespace

// ---- Subdivide (D8-BLENDER-MESH-0155) --------------------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw4MeshSubdivideCutsEveryEdgeAndKeepsTheSurface) {
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  for (int k = 1; k <= 3; ++k) {
    const Mesh m = SubdivideMeshEdges(box, k);
    const Counts c = CountsOf(m);
    const int faces = 6 * (k + 1) * (k + 1), verts = 8 + 12 * k + 6 * k * k;
    char what[140];
    std::snprintf(what, sizeof what, "cube cut %d time(s) per edge: F = 6 (k+1)^2 = %d, V = 8 + 12 k + 6 k^2 = %d, E = 2 F = %d, Euler characteristic 2", k, faces, verts, 2 * faces);
    d8test::Check(c.f == faces && c.v == verts && c.e == 2 * faces && c.Euler() == 2, what);
    d8test::Check(m.IsClosedManifold() && Near(m.Volume(), 1000.0, 1e-3) && Near(m.Area(), 600.0, 1e-3), "the surface does not move: closed, volume 1000, area 600");
  }
  // Triangles: each splits into (k+1)^2 triangles; an interior lattice vertex appears from k = 2.
  {
    const Mesh tri = TriBox(0, 0, 0, 10, 10, 10);  // 8 vertices, 12 triangles, 18 edges
    const Counts one = CountsOf(SubdivideMeshEdges(tri, 1)), two = CountsOf(SubdivideMeshEdges(tri, 2));
    d8test::Check(one.f == 48 && one.v == 8 + 18 && one.e == 72 && one.Euler() == 2, "triangulated cube, 1 cut: F 12 * 4 = 48, V 8 + 18 = 26, E 72");
    d8test::Check(two.f == 108 && two.v == 8 + 36 + 12 && two.e == 162 && two.Euler() == 2, "triangulated cube, 2 cuts: F 12 * 9 = 108, V 8 + 36 + 12 interior = 56, E 162");
    d8test::Check(SubdivideMeshEdges(tri, 2).IsClosedManifold() && Near(SubdivideMeshEdges(tri, 2).Volume(), 1000.0, 1e-3), "still closed with volume 1000");
  }
  // Positions on one quad of side 2: the edge midpoints and the centre.
  {
    const Mesh q = SubdivideMeshEdges(FlatSheet(1, 2.0), 1);
    d8test::Check(q.VertexCount() == 9 && q.FaceCount() == 4, "one quad cut once: 9 vertices, 4 quads");
    d8test::Check(HasVertexNear(q, Point3d(1, 1, 0)) && HasVertexNear(q, Point3d(1, 0, 0)) && HasVertexNear(q, Point3d(0, 1, 0)) && HasVertexNear(q, Point3d(2, 1, 0)) && HasVertexNear(q, Point3d(1, 2, 0)),
                  "the centre (1,1) and the four edge midpoints");
    const Mesh t = SubdivideMeshEdges(FlatSheet(1, 3.0), 2);
    d8test::Check(t.VertexCount() == 16 && HasVertexNear(t, Point3d(1, 1, 0)) && HasVertexNear(t, Point3d(2, 2, 0)) && HasVertexNear(t, Point3d(1, 0, 0)), "two cuts: a 4 x 4 lattice of pitch 1");
  }
  bool threw = false;
  try { (void)SubdivideMeshEdges(box, 0); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "zero cuts is a clear error");
}

// ---- Multires Subdivide (D8-BLENDER-OBJECT-0137) --------------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw4MultiresSubdivideLevelsHaveTheClosedFormFaceAndVertexCounts) {
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  Mesh simple = box;
  for (int level = 1; level <= 3; ++level) {
    simple = SubdivideMeshEdges(simple, 1);
    SubD cc = SubD::FromControlMesh(box);
    cc.Subdivide(level);
    const int faces = 6 * (1 << (2 * level));
    char what[140];
    std::snprintf(what, sizeof what, "level %d: Catmull-Clark F = 6 * 4^L = %d, V = F + 2, E = 2 F; simple subdivision has the same counts", level, faces);
    d8test::Check(cc.FaceCount() == faces && cc.VertexCount() == faces + 2 && cc.EdgeCount() == 2 * faces && simple.FaceCount() == faces && CountsOf(simple).v == faces + 2, what);
    d8test::Check(Near(simple.Volume(), 1000.0, 1e-3), "simple subdivision keeps the shape: volume 1000");
    d8test::Check(cc.IsValid() && cc.Check().IsManifoldSingleBody(), "the Catmull-Clark level is one valid manifold body");
  }
}

// ---- Duplicate (D8-BLENDER-MESH-0031) -----------------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw4MeshDuplicateFacesCopiesWithOwnVerticesAndMovesThem) {
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  {
    const Mesh m = DuplicateFaces(box, {5}, Vector3d(0, 0, 5));
    d8test::Check(CountsOf(m).v == 12 && m.FaceCount() == 7 && m.SplitDisjointPieces().size() == 2, "top face duplicated: 4 new vertices, 7 faces, a separate piece");
    d8test::Check(Near(m.Area(), 700.0, 1e-3) && Near(m.GetBoundingBox().max.z, 15.0, 1e-5), "area 600 + 100, the copy sits at z = 15");
  }
  {
    const Mesh m = DuplicateFaces(box, {1, 5}, Vector3d(20, 0, 0));
    d8test::Check(CountsOf(m).v == 8 + 6 && m.FaceCount() == 8, "two adjacent faces: their 6 vertices are copied once each (the shared edge stays shared in the copy)");
    d8test::Check(m.SplitDisjointPieces().size() == 2 && Near(m.GetBoundingBox().max.x, 30.0, 1e-5), "the copy is one piece 20 away");
  }
  d8test::Check(DuplicateFaces(box, {0}).FaceCount() == 7 && Near(DuplicateFaces(box, {0}).Area(), 700.0, 1e-3), "without a move the copy lies on the original");
  bool threw = false;
  try { (void)DuplicateFaces(box, {9}); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a face out of range is a clear error");
}

// ---- Fill Grid (D8-BLENDER-MESH-0057) ---------------------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw4MeshFillGridClosesAHoleWithTheLatticeOfItsBoundary) {
  const Mesh sheet = FlatSheet(4, 1.0);  // vertex (i, j) = 5 j + i; face (i, j) = 4 j + i
  // 2 x 2 hole (faces 5, 6, 9, 10) whose boundary has 8 vertices.
  {
    std::vector<int> keep;
    for (int f = 0; f < 16; ++f)
      if (f != 5 && f != 6 && f != 9 && f != 10) keep.push_back(f);
    const Mesh holed = KeepFaces(sheet, keep);
    const Mesh filled = FillGrid(holed, {6, 7, 8, 13, 18, 17, 16, 11}, 2);
    const Counts c = CountsOf(filled);
    d8test::Check(c.v == 25 && c.f == 16 && c.e == 40 && c.Euler() == 1, "2 x 2 hole refilled: V 25, F 16, E 40 (the lattice again), disk Euler characteristic 1");
    d8test::Check(HasVertexNear(filled, Point3d(2, 2, 0)) && Near(filled.Area(), 16.0, 1e-4), "the one interior vertex is at (2, 2) and the area is 16");
    const std::vector<Vector3d> n = filled.ComputeFaceNormals();
    bool up = true;
    for (const Vector3d& v : n) up = up && v.z > 0.99;
    d8test::Check(up, "every face, new ones included, faces +z");
    d8test::Check(filled.NakedEdgeLoops().size() == 1, "one boundary loop left: the outer rim");
  }
  // 3 x 1 hole in a 5 x 5 sheet (vertex (i, j) = 6 j + i, face (i, j) = 5 j + i): boundary of 8 vertices, span 3 -> 3 quads, no interior vertex.
  {
    const Mesh big = FlatSheet(5, 1.0);
    std::vector<int> keep;
    for (int f = 0; f < 25; ++f)
      if (f != 11 && f != 12 && f != 13) keep.push_back(f);
    const Mesh filled = FillGrid(KeepFaces(big, keep), {13, 14, 15, 16, 22, 21, 20, 19}, 3);
    d8test::Check(CountsOf(filled).v == 36 && filled.FaceCount() == 25 && Near(filled.Area(), 25.0, 1e-4), "3 x 1 hole: three quads, no new vertex, area 25");
  }
  // The same 2 x 2 hole on a tilted plane z = 0.1 (x + y): the interior vertex is exactly on the plane (0.4 at (2, 2)).
  {
    Mesh tilted = sheet;
    for (int v = 0; v < tilted.VertexCount(); ++v) tilted.raw().m_V[v].z = float(0.1 * (tilted.raw().m_V[v].x + tilted.raw().m_V[v].y));
    std::vector<int> keep;
    for (int f = 0; f < 16; ++f)
      if (f != 5 && f != 6 && f != 9 && f != 10) keep.push_back(f);
    const Mesh filled = FillGrid(KeepFaces(tilted, keep), {6, 7, 8, 13, 18, 17, 16, 11}, 2);
    d8test::Check(HasVertexNear(filled, Point3d(2, 2, 0.4), 1e-5), "boundary on an affine plane: the blended interior vertex is on the plane, z = 0.1 (2 + 2) = 0.4");
  }
  bool odd = false, span = false, chain = false;
  try { (void)FillGrid(sheet, {6, 7, 8, 13, 18, 17, 16}, 2); } catch (const std::invalid_argument&) { odd = true; }
  try { (void)FillGrid(sheet, {6, 7, 8, 13, 18, 17, 16, 11}, 0); } catch (const std::invalid_argument&) { span = true; }
  try { (void)FillGrid(sheet, {0, 2, 4, 6}, 1); } catch (const std::invalid_argument&) { chain = true; }
  d8test::Check(odd && span && chain, "an odd loop, a span of 0 and a loop that is not a chain of mesh edges are clear errors");
}

// ---- Select Similar for faces (D8-BLENDER-MESH-0139) -------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw4MeshSelectSimilarFacesComparesAreaPerimeterNormalSidesAndPlane) {
  // Box 2 x 3 x 4. Faces: 0 z- (area 6, perimeter 10), 1 y- (8, 12), 2 y+ (8, 12), 3 x- (12, 14), 4 x+ (12, 14), 5 z+ (6, 10).
  const Mesh box = QuadBox(0, 0, 0, 2, 3, 4);
  auto same = [](const std::vector<int>& a, const std::vector<int>& b) { return a == b; };
  d8test::Check(same(SimilarFaces(box, {0}, SimilarFace::Area, SimilarCompare::Equal), {0, 5}), "area equal to 6: the two 2 x 3 faces");
  d8test::Check(same(SimilarFaces(box, {1}, SimilarFace::Area, SimilarCompare::Equal), {1, 2}), "area equal to 8: the two 2 x 4 faces");
  d8test::Check(SimilarFaces(box, {0}, SimilarFace::Area, SimilarCompare::Greater).size() == 6 && same(SimilarFaces(box, {0}, SimilarFace::Area, SimilarCompare::Less), {0, 5}), "area >= 6: all six; area <= 6: the two 6s");
  d8test::Check(same(SimilarFaces(box, {0}, SimilarFace::Area, SimilarCompare::Equal, 2.0), {0, 1, 2, 5}), "area within 2 of 6: 6, 6, 8, 8");
  d8test::Check(same(SimilarFaces(box, {3}, SimilarFace::Perimeter, SimilarCompare::Equal), {3, 4}), "perimeter equal to 14: the two 3 x 4 faces");
  d8test::Check(same(SimilarFaces(box, {5}, SimilarFace::Normal, SimilarCompare::Equal), {5}), "normal equal to +z: only the top");
  d8test::Check(same(SimilarFaces(box, {5}, SimilarFace::Normal, SimilarCompare::Equal, 91.0), {1, 2, 3, 4, 5}), "normal within 91 degrees of +z: the top and the four sides, not the bottom (180)");
  d8test::Check(same(SimilarFaces(box, {5}, SimilarFace::Normal, SimilarCompare::Greater, 100.0), {0}), "normal at least 100 degrees from +z: the bottom");
  d8test::Check(SimilarFaces(box, {0}, SimilarFace::Sides, SimilarCompare::Equal).size() == 6, "all six faces are quads");
  Mesh mixed = ConnectFaceVertices(box, 5, 4, 7);
  d8test::Check(same(SimilarFaces(mixed, {5}, SimilarFace::Sides, SimilarCompare::Equal), {5, 6}) && SimilarFaces(mixed, {0}, SimilarFace::Sides, SimilarCompare::Equal).size() == 5, "triangles select the 2 triangles, a quad the 5 quads");
  const Mesh sheet = FlatSheet(3, 1.0);
  d8test::Check(SimilarFaces(sheet, {0}, SimilarFace::Coplanar, SimilarCompare::Equal, 1e-6).size() == 9, "a flat sheet is one plane");
  d8test::Check(same(SimilarFaces(box, {5}, SimilarFace::Coplanar, SimilarCompare::Equal, 1e-6), {5}), "box: the top shares its normal with nothing else, and the bottom is a parallel but distinct plane");
  bool threw = false;
  try { (void)SimilarFaces(box, {7}, SimilarFace::Area, SimilarCompare::Equal); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a seed out of range is a clear error");
}

// ---- Face select More / Less / Linked (D8-BLENDER-PAINT-0010, -0006, -0008) -------------------------------------------------------------
D8_EXTRA_TEST(Rw4PaintFaceSelectMoreLessAndLinkedFollowVertexAndEdgeAdjacency) {
  const Mesh sheet = FlatSheet(5, 1.0);  // face (i, j) = 5 j + i
  const std::vector<int> block = {6, 7, 8, 11, 12, 13, 16, 17, 18};
  d8test::Check(GrowFaceSelection(sheet, {12}) == block, "one face grows through its vertices to the 3 x 3 block");
  d8test::Check(GrowFaceSelection(sheet, {12}, false) == std::vector<int>({7, 11, 12, 13, 17}), "without face step: the face and its four edge neighbours");
  d8test::Check(GrowFaceSelection(sheet, GrowFaceSelection(sheet, {12})).size() == 25, "grown twice: the whole 5 x 5 sheet");
  d8test::Check(ShrinkFaceSelection(sheet, block) == std::vector<int>({12}), "the 3 x 3 block shrinks to its centre");
  d8test::Check(ShrinkFaceSelection(sheet, block, false) == std::vector<int>({12}), "edge step: only the centre has all four edge neighbours selected");
  std::vector<int> all(25);
  for (int i = 0; i < 25; ++i) all[static_cast<size_t>(i)] = i;
  d8test::Check(ShrinkFaceSelection(sheet, all) == all && GrowFaceSelection(sheet, all) == all, "everything selected: nothing to shrink or grow");
  d8test::Check(ShrinkFaceSelection(sheet, {}).empty(), "an empty selection stays empty");
  // Linked faces follow shared edges only.
  const Mesh two = Mesh::MergeAndWeld({QuadBox(0, 0, 0, 1, 1, 1), QuadBox(5, 0, 0, 6, 1, 1)});
  const std::vector<int> first = LinkedFaces(two, 0);
  d8test::Check(first == std::vector<int>({0, 1, 2, 3, 4, 5}), "two disjoint cubes: the linked faces of face 0 are its own cube's six");
  d8test::Check(LinkedFaces(sheet, 12).size() == 25, "a connected sheet is linked as a whole");
  Mesh corner;  // two quads that meet at one vertex only
  for (int i = 0; i < 7; ++i) corner.raw().m_V.Append(ON_3fPoint(float(i), float(i % 3), 0));
  AddQuad(corner, 0, 1, 2, 3);
  AddQuad(corner, 2, 4, 5, 6);
  d8test::Check(LinkedFaces(corner, 0) == std::vector<int>({0}), "faces that share a vertex but no edge are not linked");
  bool threw = false;
  try { (void)GrowFaceSelection(sheet, {99}); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a face out of range is a clear error");
}

// ---- Mask Flood Fill and Mask Filter (D8-BLENDER-PAINT-0022, D8-BLENDER-SCULPT-0019) ------------------------------------------------------
D8_EXTRA_TEST(Rw4SculptMaskFloodFillAndFilterGiveFillsAndLatticeDiamonds) {
  const std::vector<float> m3 = {0.0f, 0.25f, 1.0f};
  d8test::Check(FloodFillMask(m3, MaskFill::Value, 0.3f) == std::vector<float>({0.3f, 0.3f, 0.3f}), "fill with a value: every vertex 0.3");
  d8test::Check(FloodFillMask(m3, MaskFill::ValueInverse, 0.3f) == std::vector<float>({0.7f, 0.7f, 0.7f}), "fill with the inverse of a value: 1 - 0.3 = 0.7");
  d8test::Check(FloodFillMask(m3, MaskFill::Invert) == std::vector<float>({1.0f, 0.75f, 0.0f}), "invert: 1 - m");
  const Mesh sheet = FlatSheet(4, 1.0);  // 5 x 5 vertices, vertex (i, j) = 5 j + i, the centre is 12
  std::vector<float> spike(25, 0.0f);
  spike[12] = 1.0f;
  auto count = [](const std::vector<float>& m) { int n = 0; for (float x : m) n += x > 0.5f ? 1 : 0; return n; };
  const std::vector<float> g1 = FilterMask(sheet, spike, MaskFilterKind::Grow);
  d8test::Check(count(g1) == 5 && g1[7] == 1.0f && g1[11] == 1.0f && g1[13] == 1.0f && g1[17] == 1.0f && g1[6] == 0.0f, "grow once: the vertex and its 4 edge neighbours (diagonals stay 0)");
  d8test::Check(count(FilterMask(sheet, spike, MaskFilterKind::Grow, 2)) == 13, "grow twice: the diamond of radius 2 has 1 + 4 + 8 = 13 vertices");
  const std::vector<float> s1 = FilterMask(sheet, g1, MaskFilterKind::Shrink);
  d8test::Check(count(s1) == 1 && s1[12] == 1.0f, "shrinking the 5-vertex diamond leaves its centre");
  d8test::Check(count(FilterMask(sheet, FilterMask(sheet, spike, MaskFilterKind::Grow, 2), MaskFilterKind::Shrink)) == 5, "shrinking the 13-vertex diamond once gives the 5-vertex diamond");
  d8test::Check(FilterMask(sheet, spike, MaskFilterKind::Grow, 0) == spike, "0 iterations change nothing");
  const std::vector<float> sm = FilterMask(sheet, spike, MaskFilterKind::Smooth);
  d8test::Check(Near(sm[7], 0.25, 1e-6) && Near(sm[12], 0.0, 1e-6) && Near(sm[6], 0.0, 1e-6), "smooth (mean of the neighbours): the 4 neighbours of the spike get 1/4, the spike itself 0");
  bool threw = false;
  try { (void)FilterMask(sheet, std::vector<float>(3, 0.0f), MaskFilterKind::Grow); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a mask of the wrong size is a clear error");
}

// ---- Mesh Filter, Inflate (D8-BLENDER-SCULPT-0023) -----------------------------------------------------------------------------
D8_EXTRA_TEST(Rw4SculptMeshFilterInflateMovesVerticesAlongTheirNormals) {
  const Mesh sheet = FlatSheet(3, 1.0);
  const Mesh up = InflateMesh(sheet, 0.5), down = InflateMesh(sheet, -0.25);
  bool ok = true;
  for (int i = 0; i < sheet.VertexCount(); ++i)
    ok = ok && Near(Vertex(up, i).z, 0.5, 1e-6) && Near(Vertex(up, i).x, Vertex(sheet, i).x, 1e-6) && Near(Vertex(down, i).z, -0.25, 1e-6);
  d8test::Check(ok, "flat sheet (normals +z): z becomes +0.5 and -0.25 and x, y stay");
  const Mesh ico = MakeIcoSphereMesh(1, 1.0);
  const Mesh big = InflateMesh(ico, 0.25);
  bool radial = true;
  for (int i = 0; i < big.VertexCount(); ++i) radial = radial && Near((Vertex(big, i) - Point3d::Origin).Length(), 1.25, 1e-4);
  d8test::Check(radial, "icosahedron (vertex normals radial by symmetry): every vertex moves from radius 1 to 1.25");
  d8test::Check(InflateMesh(ico, 0.0).FaceCount() == 20 && Near(InflateMesh(ico, 0.0).Volume(), ico.Volume(), 1e-9), "strength 0 changes nothing");
}

// ---- Trim Box / Trim Line gestures (D8-BLENDER-SCULPT-0033, -0035) ---------------------------------------------------------------------
D8_EXTRA_TEST(Rw4SculptTrimByPlaneAndBoxHasClosedFormVolumes) {
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  {
    const Mesh a = TrimMeshByPlane(box, Point3d(3, 0, 0), Vector3d(1, 0, 0));
    const Mesh b = TrimMeshByPlane(box, Point3d(3, 0, 0), Vector3d(-1, 0, 0));
    d8test::Check(a.IsClosedManifold() && Near(a.Volume(), 300.0, 1e-3) && Near(a.Area(), 320.0, 1e-3), "the half x <= 3 stays: volume 300, area 320, closed");
    d8test::Check(b.IsClosedManifold() && Near(b.Volume(), 700.0, 1e-3), "the half x >= 3 stays when the normal points the other way: volume 700");
    const Mesh diag = TrimMeshByPlane(box, Point3d(5, 5, 0), Vector3d(1, 1, 0));
    d8test::Check(diag.IsClosedManifold() && Near(diag.Volume(), 500.0, 1e-3), "a slanted cut through two edges leaves half: 500");
    d8test::Check(Near(TrimMeshByPlane(box, Point3d(20, 0, 0), Vector3d(1, 0, 0)).Volume(), 1000.0, 1e-3), "a cut plane beyond the mesh removes nothing");
  }
  {
    const Mesh hole = TrimMeshByBox(box, Point3d(3, 3, -1), Point3d(7, 7, 11));
    const Counts c = CountsOf(hole);
    d8test::Check(hole.IsClosedManifold() && c.Euler() == 0, "a through hole: closed, Euler characteristic 0 (a torus)");
    d8test::Check(Near(hole.Volume(), 1000.0 - 16.0 * 10.0, 1e-2), W("through hole volume = 1000 - 4*4*10 = 840, got %.4f", hole.Volume()));
    d8test::Check(Near(hole.Area(), 600.0 - 32.0 + 160.0, 1e-2), W("through hole area = 600 - 2*16 + 4*4*10 = 728, got %.4f", hole.Area()));
    const Mesh pocket = TrimMeshByBox(box, Point3d(3, 3, 6), Point3d(7, 7, 11));
    d8test::Check(pocket.IsClosedManifold() && CountsOf(pocket).Euler() == 2 && Near(pocket.Volume(), 1000.0 - 16.0 * 4.0, 1e-2), "a pocket 4 deep: Euler characteristic 2, volume 1000 - 64 = 936");
    d8test::Check(Near(pocket.Area(), 600.0 + 4 * 4 * 4.0, 1e-2), "pocket area = 600 + 4 walls of 4 x 4 = 664 (floor equals the lost opening)");
  }
  bool threw = false;
  try { (void)TrimMeshByBox(box, Point3d(1, 1, 1), Point3d(1, 2, 2)); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a flat box is a clear error");
}

// ---- Extract Face Set / Extract Mask (D8-BLENDER-SCULPT-0011, -0025) ---------------------------------------------------------------------------
D8_EXTRA_TEST(Rw4SculptExtractFaceSetAndMaskGiveThePatchAndItsSolid) {
  const Mesh sheet = FlatSheet(4, 1.0);  // vertex (i, j) = 5 j + i, face (i, j) = 4 j + i
  // Four face sets, one per quadrant of the 4 x 4 faces.
  std::vector<int> sets(16);
  for (int j = 0; j < 4; ++j)
    for (int i = 0; i < 4; ++i) sets[static_cast<size_t>(4 * j + i)] = (i >= 2 ? 1 : 0) + (j >= 2 ? 2 : 0);
  const std::vector<int> quadrant = FacesInSet(sets, 3);
  d8test::Check(quadrant == std::vector<int>({10, 11, 14, 15}), "set 3 is the upper right 2 x 2 block of faces");
  const Mesh patch = ExtractFaces(sheet, quadrant);
  d8test::Check(patch.FaceCount() == 4 && patch.VertexCount() == 9 && Near(patch.Area(), 4.0, 1e-5), "extracted patch: 4 faces, 9 vertices, area 4");
  const Mesh solid = ExtractFaces(sheet, quadrant, 0.5);
  d8test::Check(solid.IsClosedManifold(), "extracted with thickness: a closed solid");
  d8test::Check(Near(solid.Volume(), 4.0 * 0.5, 1e-4) && Near(solid.Area(), 2.0 * 4.0 + 8.0 * 0.5, 1e-4), "volume = area * t = 2 and area = 2 * 4 + perimeter 8 * 0.5 = 12");
  // Mask: 1 on the vertices with x <= 2 (i <= 2); a face is taken when all its corners are above the threshold, so the two face columns left of x = 2.
  std::vector<float> mask(25, 0.0f);
  for (int j = 0; j < 5; ++j)
    for (int i = 0; i <= 2; ++i) mask[static_cast<size_t>(5 * j + i)] = 1.0f;
  const std::vector<int> taken = FacesAboveMask(sheet, mask);
  d8test::Check(taken.size() == 8, "faces with all corners masked: the 2 columns x 4 rows = 8");
  d8test::Check(Near(ExtractFaces(sheet, taken).Area(), 8.0, 1e-5) && Near(ExtractFaces(sheet, taken, 0.25).Volume(), 8.0 * 0.25, 1e-4), "the masked area is 8; thickened by 0.25 its volume is 2");
  for (float& m : mask) m = m > 0.5f ? 0.5f : m;
  d8test::Check(FacesAboveMask(sheet, mask).empty() && FacesAboveMask(sheet, mask, 0.4f).size() == 8, "a mask exactly at the threshold is not above it; at a lower threshold it is");
  bool empty = false, range = false;
  try { (void)ExtractFaces(sheet, {}); } catch (const std::invalid_argument&) { empty = true; }
  try { (void)ExtractFaces(sheet, {40}); } catch (const std::invalid_argument&) { range = true; }
  d8test::Check(empty && range, "an empty list and a face out of range are clear errors");
}

// ---- UV Unwrap (D8-BLENDER-UV-0054) ---------------------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw4UvUnwrapOfADevelopableStripIsIsometric) {
  const double r = 3.0, h = 5.0;
  const int steps = 6;
  const Mesh strip = ScrewPolyline({Point3d(r, 0, 0), Point3d(r, 0, h)}, Point3d(0, 0, 0), Vector3d(0, 0, 1), kPi, steps);  // half of a prism wall: 7 x 2 vertices, 6 quads
  d8test::Check(strip.VertexCount() == 14 && strip.FaceCount() == 6, "half-cylinder wall: 14 vertices, 6 quads");
  LscmReport report;
  const std::vector<Point2d> uv = ComputeLscmUVs(strip, &report);
  d8test::Check(report.converged && uv.size() == 14, "the solve converged");
  double lo = 1e30, hi = 0.0;
  for (int i = 0; i < strip.raw().m_F.Count(); ++i) {
    const ON_MeshFace& f = strip.raw().m_F[i];
    for (int k = 0; k < 4; ++k) {
      const int a = f.vi[k], b = f.vi[(k + 1) % 4];
      const double ratio = Point2d(uv[static_cast<size_t>(a)]).DistanceTo(Point2d(uv[static_cast<size_t>(b)])) / Vertex(strip, a).DistanceTo(Vertex(strip, b));
      lo = std::min(lo, ratio);
      hi = std::max(hi, ratio);
    }
  }
  d8test::Check(hi / lo < 1.001, W("every edge keeps its length by one common factor (min %.6f, max %.6f)", lo, hi));
  // the unrolled bottom edge is the straight run of 6 chords, a vertical edge is the height: their ratio is the developed length over h
  const double developed = steps * 2.0 * r * std::sin(kPi / (2.0 * steps));  // the chords laid side by side
  const double run = Point2d(uv[0]).DistanceTo(Point2d(uv[2 * steps])), rise = Point2d(uv[0]).DistanceTo(Point2d(uv[1]));
  d8test::Check(Near(run / rise, developed / h, 1e-3), W("unrolled bottom run / vertical edge = 6 chords / 5 = %.6f, got %.6f", developed / h, run / rise));
  double chain = 0.0;
  for (int k = 0; k < steps; ++k) chain += Point2d(uv[static_cast<size_t>(2 * k)]).DistanceTo(Point2d(uv[static_cast<size_t>(2 * k + 2)]));
  d8test::Check(Near(chain, run, 1e-4 * run), "the bottom vertices are collinear in the unrolling (the chain length equals the end to end distance)");
}

// ---- Average Islands Scale (D8-BLENDER-UV-0004) ---------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw4UvAverageIslandsScaleEqualisesTheTexelDensity) {
  // Island A: a 2 x 2 quad (3D area 4); island B: a 1 x 1 quad (area 1); both have a unit-square UV (area 1).
  Mesh m;
  m.raw().m_V.Append(ON_3fPoint(0, 0, 0)); m.raw().m_V.Append(ON_3fPoint(2, 0, 0)); m.raw().m_V.Append(ON_3fPoint(2, 2, 0)); m.raw().m_V.Append(ON_3fPoint(0, 2, 0));
  m.raw().m_V.Append(ON_3fPoint(5, 0, 0)); m.raw().m_V.Append(ON_3fPoint(6, 0, 0)); m.raw().m_V.Append(ON_3fPoint(6, 1, 0)); m.raw().m_V.Append(ON_3fPoint(5, 1, 0));
  AddQuad(m, 0, 1, 2, 3);
  AddQuad(m, 4, 5, 6, 7);
  const std::vector<Point2d> uv = {Point2d(0, 0), Point2d(1, 0), Point2d(1, 1), Point2d(0, 1), Point2d(2, 0), Point2d(3, 0), Point2d(3, 1), Point2d(2, 1)};
  const std::vector<Point2d> out = AverageUvIslandScales(m, uv);
  auto uv_area = [&](int first) {
    double a = 0;
    for (int k = 0; k < 4; ++k) {
      const Point2d& p = out[static_cast<size_t>(first + k)];
      const Point2d& q = out[static_cast<size_t>(first + (k + 1) % 4)];
      a += p.x * q.y - q.x * p.y;
    }
    return std::abs(a) / 2.0;
  };
  const double density = 2.0 / 5.0;  // total UV area 2 over total 3D area 5
  d8test::Check(Near(uv_area(0) / 4.0, density, 1e-9) && Near(uv_area(4) / 1.0, density, 1e-9), "after scaling both islands have the same UV area per 3D area, 2/5");
  d8test::Check(Near(uv_area(0) + uv_area(4), 2.0, 1e-9), "the total UV area is unchanged");
  d8test::Check(Near(uv_area(0), 1.6, 1e-9) && Near(uv_area(4), 0.4, 1e-9), "island A grows to area 1.6, island B shrinks to 0.4");
  const Point2d ca((out[0].x + out[2].x) / 2, (out[0].y + out[2].y) / 2), cb((out[4].x + out[6].x) / 2, (out[4].y + out[6].y) / 2);
  d8test::Check(Near(ca.x, 0.5, 1e-9) && Near(ca.y, 0.5, 1e-9) && Near(cb.x, 2.5, 1e-9) && Near(cb.y, 0.5, 1e-9), "each island is scaled about its own centre");
  d8test::Check(Near((out[1].x - out[0].x) / (out[2].y - out[1].y), 1.0, 1e-9), "scaling is uniform: the squares stay squares");
  bool threw = false;
  try { (void)AverageUvIslandScales(m, std::vector<Point2d>(3)); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a wrong UV count is a clear error");
}
