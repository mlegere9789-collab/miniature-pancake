// Register row tests for the mesh-subd-sculpt factory, round 3, batch 2 (slice A): more edit-mode operators, the primitives, vertex
// groups and weight modifiers. Every oracle is a closed form that does not use the code under test:
//  * Quad grid: dissolving an interior vertex or an edge loop removes exactly the vertices and quads that the grid counting says (a
//    5 x 5 vertex grid minus one row of 5 vertices is 20 vertices, 12 quads and 31 edges).
//  * Extrude region: a 1 x 2 patch of the top of a 2 x 2 x 1 slab raised by d adds 2 d of volume and 6 walls of area d.
//  * Primitives: regular n-gon of radius r has area (n/2) r^2 sin(2 pi / n); the icosahedron of circumradius R has edge R / sin(72 deg),
//    area 5 sqrt(3) a^2 and volume (5/12)(3 + sqrt 5) a^3; icosphere counts are F = 20 * 4^(s-1), V = F/2 + 2; a polygonal torus of
//    revolution has volume n sin(2 pi / n) A R_c with A the profile area and R_c its centroid radius.
//  * Beautify: in the parallelogram (0,0), (4,0), (5,1), (1,1) the long diagonal gives smallest angle atan(1/5), the short one atan(1/3).
//  * Weights: invert 1 - w, normalize w / max, quantize round(w s) / s, the falloff polynomials, the distance ramp and the gradient fraction.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/mesh_edit.h"
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

std::set<std::pair<int, int>> EdgesOf(const Mesh& m) {
  std::set<std::pair<int, int>> edges;
  for (int i = 0; i < m.raw().m_F.Count(); ++i) {
    const ON_MeshFace& f = m.raw().m_F[i];
    const int n = FaceSize(f);
    for (int k = 0; k < n; ++k) edges.insert({std::min(f.vi[k], f.vi[(k + 1) % n]), std::max(f.vi[k], f.vi[(k + 1) % n])});
  }
  return edges;
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

Mesh FlatSheet(int n, double pitch) {
  Mesh m;
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i) m.raw().m_V.Append(ON_3fPoint(float(i * pitch), float(j * pitch), 0.0f));
  const auto id = [n](int i, int j) { return j * (n + 1) + i; };
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) AddQuad(m, id(i, j), id(i + 1, j), id(i + 1, j + 1), id(i, j + 1));
  return m;
}

Point3d Vertex(const Mesh& m, int i) {
  const ON_3fPoint& p = m.raw().m_V[i];
  return Point3d(p.x, p.y, p.z);
}

int Quads(const Mesh& m) {
  int q = 0;
  for (int i = 0; i < m.raw().m_F.Count(); ++i) q += m.raw().m_F[i].IsQuad() ? 1 : 0;
  return q;
}

char g_what[220];
const char* W(const char* fmt, double a = 0, double b = 0, double c = 0) {
  std::snprintf(g_what, sizeof g_what, fmt, a, b, c);
  return g_what;
}

}  // namespace

// ---- Dissolve Selection and Delete Edge Loop (D8-BLENDER-MESH-0027, -0021) --------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshDissolveModesAndDeleteEdgeLoopRemoveExactlyTheCountedElements) {
  const Mesh grid = FlatSheet(4, 1.0);  // 5 x 5 vertices, vertex (i, j) = 5 j + i, 16 quads
  // Delete Edge Loop through the interior row j = 2 (edge 12-13): row of 5 vertices gone, 4 x 3 quads of area 4 x 4 remain.
  {
    const Mesh m = DeleteEdgeLoop(grid, 12, 13);
    const Counts c = CountsOf(m);
    d8test::Check(c.v == 20 && c.f == 12 && c.e == 31 && c.Euler() == 1, "row loop deleted: 25 - 5 = 20 vertices, 12 quads, E = 4*4 + 5*3 = 31, disk Euler characteristic 1");
    d8test::Check(Quads(m) == 12 && Near(m.Area(), 16.0, 1e-4), "all faces are still quads and the area is 16");
    const BoundingBox bb = m.GetBoundingBox();
    d8test::Check(Near(bb.max.x, 4.0, 1e-5) && Near(bb.max.y, 4.0, 1e-5), "the outer boundary did not move");
  }
  {
    const Mesh m = DeleteEdgeLoop(grid, 7, 12);  // the column i = 2
    d8test::Check(CountsOf(m).v == 20 && CountsOf(m).f == 12 && Quads(m) == 12, "column loop deleted: 20 vertices, 12 quads");
  }
  // Dissolve Vertices: the interior vertex 12 and its four quads become the 2 x 2 square (8 outline corners -> 6 triangles).
  {
    const Mesh m = DissolveVertices(grid, {12});
    d8test::Check(CountsOf(m).v == 24 && m.FaceCount() == 12 + 6 && Near(m.Area(), 16.0, 1e-4), "vertex dissolved: V 24, 12 untouched quads + 6 triangles, area 16");
    bool boundary = false;
    try { (void)DissolveVertices(grid, {2}); } catch (const std::invalid_argument&) { boundary = true; }
    d8test::Check(boundary, "a boundary vertex is refused");
  }
  // Dissolve Edges: two separate interior edges give two merged pairs (two 1 x 2 hexagons of 4 triangles each).
  {
    const Mesh m = DissolveEdges(grid, {{6, 7}, {7, 8}});
    d8test::Check(CountsOf(m).v == 25 && m.FaceCount() == 12 + 2 * 4 && Near(m.Area(), 16.0, 1e-4), "two edges dissolved: V 25, 12 quads + 2 * 4 triangles, area 16");
    bool bad = false;
    try { (void)DissolveEdges(grid, {{0, 24}}); } catch (const std::invalid_argument&) { bad = true; }
    d8test::Check(bad, "an edge that is not in the mesh is a clear error");
  }
}

// ---- Extrude Region (D8-BLENDER-VIEW3D-0014) ------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshExtrudeRegionRaisesAPatchWithWallsOnlyAroundItsOutline) {
  // Slab 2 x 2 x 1 whose top (z = 1) is cut into 4 unit quads by planes x = 1 and y = 1.
  Mesh slab = BisectMesh(QuadBox(0, 0, 0, 2, 2, 1), Point3d(1, 0, 0), Vector3d(1, 0, 0));
  slab = BisectMesh(slab, Point3d(0, 1, 0), Vector3d(0, 1, 0));
  d8test::Check(slab.IsClosedManifold() && Near(slab.Volume(), 4.0, 1e-4), "the cut slab is closed with volume 4");
  const std::vector<Vector3d> normals = slab.ComputeFaceNormals();
  std::vector<int> left;  // top quads with x < 1
  for (int i = 0; i < slab.FaceCount(); ++i) {
    const ON_MeshFace& f = slab.raw().m_F[i];
    double cx = 0, cz = 0;
    for (int k = 0; k < 4; ++k) {
      cx += Vertex(slab, f.vi[k]).x / 4.0;
      cz += Vertex(slab, f.vi[k]).z / 4.0;
    }
    if (normals[static_cast<size_t>(i)].z > 0.9 && Near(cz, 1.0, 1e-6) && cx < 1.0) left.push_back(i);
  }
  d8test::Check(left.size() == 2, "two top quads on the left");
  const Mesh up = ExtrudeFaceRegion(slab, left, 3.0);
  d8test::Check(up.IsClosedManifold(), "the extruded patch is a closed manifold");
  d8test::Check(Near(up.Volume(), 4.0 + 2.0 * 3.0, 1e-3), W("volume = 4 + (1 x 2 patch) * 3 = 10, got %.6f", up.Volume()));
  d8test::Check(Near(up.Area(), slab.Area() + 6.0 * 3.0, 1e-3), W("area = %.3f + 6 walls * (1 * 3), got %.6f", slab.Area(), up.Area()));
  d8test::Check(up.FaceCount() == slab.FaceCount() + 6 && CountsOf(up).v == CountsOf(slab).v + 6, "6 outline edges give 6 wall quads; the 6 patch vertices are copied (the shared middle edge gets no wall)");
  d8test::Check(Near(up.GetBoundingBox().max.z, 4.0, 1e-5), "the patch moved along its +z normal");
  bool threw = false;
  try { (void)ExtrudeFaceRegion(slab, {99}, 1.0); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a face index out of range is a clear error");
}

// ---- Select Linked, Select Faces by Sides, Select Mirror, Select Axis (D8-BLENDER-MESH-0129, -0125, -0131, -0119) -----------------------
D8_EXTRA_TEST(Rw3MeshSelectionQueriesFollowTopologyCornerCountsAndPositions) {
  // Select Linked: the vertices of the cube that holds the picked vertex, not those of the other cube.
  {
    const Mesh both = Mesh::MergeAndWeld({QuadBox(0, 0, 0, 1, 1, 1), QuadBox(5, 0, 0, 6, 1, 1)});
    const std::vector<int> linked = LinkedVertices(both, 0);
    bool all_left = true;
    for (int v : linked) all_left = all_left && Vertex(both, v).x <= 1.0 + 1e-6;
    d8test::Check(both.VertexCount() == 16 && linked.size() == 8 && all_left, "two disjoint cubes: the linked set of one vertex is its own 8 vertices");
    d8test::Check(LinkedVertices(FlatSheet(3, 1.0), 5).size() == 16, "a connected sheet: every vertex is linked");
    bool threw = false;
    try { (void)LinkedVertices(both, 99); } catch (const std::invalid_argument&) { threw = true; }
    d8test::Check(threw, "a vertex out of range is a clear error");
  }
  // Select Faces by Sides: a cube with two faces cut along a diagonal has 4 quads and 4 triangles.
  {
    Mesh m = ConnectFaceVertices(QuadBox(0, 0, 0, 10, 10, 10), 5, 4, 7);
    m = ConnectFaceVertices(m, 0, 0, 3);
    d8test::Check(m.FaceCount() == 8 && Quads(m) == 4, "4 quads and 4 triangles");
    d8test::Check(FacesBySides(m, 3, SidesCompare::Equal).size() == 4 && FacesBySides(m, 4, SidesCompare::Equal).size() == 4, "equal to 3: 4 faces; equal to 4: 4 faces");
    d8test::Check(FacesBySides(m, 3, SidesCompare::Greater).size() == 4 && FacesBySides(m, 4, SidesCompare::Less).size() == 4, "greater than 3: the 4 quads; less than 4: the 4 triangles");
    d8test::Check(FacesBySides(m, 4, SidesCompare::NotEqual).size() == 4 && FacesBySides(m, 5, SidesCompare::Less).size() == 8 && FacesBySides(m, 3, SidesCompare::Less).empty(),
                  "not equal to 4: the 4 triangles; less than 5: all 8; less than 3: none");
  }
  // Select Mirror: a 2 x 2 sheet centred on the origin (vertex (i, j) = 3 j + i at (i - 1, j - 1)).
  {
    const Mesh sheet = FlatSheet(2, 1.0).Transform(ON_Xform::TranslationTransformation(-1.0, -1.0, 0.0));
    d8test::Check(MirroredVertices(sheet, {0, 3, 6}, 0) == std::vector<int>({2, 5, 8}), "the left column mirrors to the right column in x");
    d8test::Check(MirroredVertices(sheet, {1, 4, 7}, 0) == std::vector<int>({1, 4, 7}), "the centre column lies on the mirror plane and matches itself");
    d8test::Check(MirroredVertices(sheet, {0, 1, 2}, 1) == std::vector<int>({6, 7, 8}), "the bottom row mirrors to the top row in y");
    Mesh skew = sheet;
    skew.raw().m_V[2].x = 1.5f;
    d8test::Check(MirroredVertices(skew, {0}, 0).empty(), "with the counterpart moved by 0.5 there is no match within 1e-4");
    d8test::Check(MirroredVertices(skew, {0}, 0, 0.6) == std::vector<int>({2}), "a threshold of 0.6 accepts it");
  }
  // Select Axis on the 5 x 5 unit sheet, active vertex 12 = (2, 2).
  {
    const Mesh sheet = FlatSheet(4, 1.0);
    d8test::Check(VerticesByAxis(sheet, 12, 0, AxisSide::Positive).size() == 10 && VerticesByAxis(sheet, 12, 0, AxisSide::Negative).size() == 10, "x above / below 2: 2 columns of 5 each");
    const std::vector<int> aligned = VerticesByAxis(sheet, 12, 0, AxisSide::Aligned);
    d8test::Check(aligned.size() == 5 && std::find(aligned.begin(), aligned.end(), 12) != aligned.end(), "aligned in x: the column of 5 including the active vertex");
    d8test::Check(VerticesByAxis(sheet, 12, 2, AxisSide::Aligned).size() == 25 && VerticesByAxis(sheet, 12, 2, AxisSide::Positive).empty(), "all vertices are at the same z");
  }
}

// ---- Beautify Faces (D8-BLENDER-MESH-0003) -----------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshBeautifyFlipsTheLongDiagonalOfAParallelogram) {
  Mesh para;
  para.raw().m_V.Append(ON_3fPoint(0, 0, 0)); para.raw().m_V.Append(ON_3fPoint(4, 0, 0)); para.raw().m_V.Append(ON_3fPoint(5, 1, 0)); para.raw().m_V.Append(ON_3fPoint(1, 1, 0));
  AddTri(para, 0, 1, 2);
  AddTri(para, 0, 2, 3);  // split along the long diagonal 0-2 (length sqrt 26)
  auto smallest_angle_deg = [](const Mesh& m) {
    double best = 180.0;
    for (int i = 0; i < m.raw().m_F.Count(); ++i) {
      const ON_MeshFace& f = m.raw().m_F[i];
      for (int k = 0; k < 3; ++k) {
        const Point3d p = Vertex(m, f.vi[k]);
        Vector3d u = Vertex(m, f.vi[(k + 1) % 3]) - p, w = Vertex(m, f.vi[(k + 2) % 3]) - p;
        u.Unitize();
        w.Unitize();
        best = std::min(best, std::acos(std::max(-1.0, std::min(1.0, u * w))) * 180.0 / kPi);
      }
    }
    return best;
  };
  d8test::Check(Near(smallest_angle_deg(para), std::atan(1.0 / 5.0) * 180.0 / kPi, 1e-3), "before: the smallest angle is atan(1/5) = 11.31 degrees");
  const Mesh fixed = BeautifyTriangles(para);
  const auto edges = EdgesOf(fixed);
  d8test::Check(edges.count({1, 3}) == 1 && edges.count({0, 2}) == 0, "the short diagonal 1-3 replaces the long diagonal 0-2");
  d8test::Check(Near(smallest_angle_deg(fixed), std::atan(1.0 / 3.0) * 180.0 / kPi, 1e-3), "after: the smallest angle is atan(1/3) = 18.43 degrees");
  d8test::Check(fixed.FaceCount() == 2 && Near(fixed.Area(), 4.0, 1e-5), "still two triangles of total area 4");
  // A unit square has equal diagonals: nothing improves, nothing flips.
  Mesh sq;
  sq.raw().m_V.Append(ON_3fPoint(0, 0, 0)); sq.raw().m_V.Append(ON_3fPoint(1, 0, 0)); sq.raw().m_V.Append(ON_3fPoint(1, 1, 0)); sq.raw().m_V.Append(ON_3fPoint(0, 1, 0));
  AddTri(sq, 0, 1, 2);
  AddTri(sq, 0, 2, 3);
  d8test::Check(EdgesOf(BeautifyTriangles(sq)).count({0, 2}) == 1, "a square keeps its diagonal");
  // With the fourth corner lifted the two triangles are 32.5 degrees apart: a 10 degree limit keeps the long diagonal.
  Mesh bent = para;
  bent.raw().m_V[3].z = 0.5f;
  d8test::Check(EdgesOf(BeautifyTriangles(bent, 10.0)).count({0, 2}) == 1, "a 10 degree limit leaves a pair that folds by 32.5 degrees alone");
}

// ---- Make Planar Faces (D8-BLENDER-MESH-0051) --------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshMakePlanarMovesCornersAlongTheFaceNormalToTheMeanPlane) {
  const double h = 0.4;
  Mesh warped;
  warped.raw().m_V.Append(ON_3fPoint(0, 0, 0)); warped.raw().m_V.Append(ON_3fPoint(1, 0, 0)); warped.raw().m_V.Append(ON_3fPoint(1, 1, float(h))); warped.raw().m_V.Append(ON_3fPoint(0, 1, 0));
  AddQuad(warped, 0, 1, 2, 3);
  // The quad normal is the cross product of its diagonals (1, 1, h) x (-1, 1, 0) = (-h, -h, 2); the corner mean is (1/2, 1/2, h/4).
  Vector3d n(-h, -h, 2.0);
  n.Unitize();
  const double dist = (h / 2.0) / std::sqrt(2 * h * h + 4);  // each corner is this far from the mean plane (alternating sides)
  const Mesh flat = MakeFacesPlanar(warped, {0}, 1.0);
  const Point3d c(0.5, 0.5, h / 4.0);
  bool on_plane = true, along_normal = true;
  for (int i = 0; i < 4; ++i) {
    const Vector3d d = Vertex(flat, i) - Vertex(warped, i);
    on_plane = on_plane && Near((Vertex(flat, i) - c) * n, 0.0, 1e-5);
    along_normal = along_normal && Near(d.Length(), dist, 1e-5) && Near(std::abs(d * n), dist, 1e-5);
  }
  d8test::Check(on_plane, "factor 1: every corner lies on the mean plane");
  d8test::Check(along_normal, W("every corner moved by %.6f along the face normal", dist));
  Vector3d mean(0, 0, 0);
  for (int i = 0; i < 4; ++i) mean += Vertex(flat, i) - Vertex(warped, i);
  d8test::Check(mean.Length() < 1e-5, "the moves cancel: the corner mean did not change");
  const Mesh half = MakeFacesPlanar(warped, {0}, 0.5);
  d8test::Check(Near((Vertex(half, 0) - c) * n, 0.5 * ((Vertex(warped, 0) - c) * n), 1e-5), "factor 0.5: the distance to the plane halves");
  d8test::Check(Near(MakeFacesPlanar(warped, {0}, 1.0, 0).raw().m_V[2].z, h, 1e-6), "0 rounds change nothing");
  bool threw = false;
  try { (void)MakeFacesPlanar(warped, {0}, 2.0); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a factor above 1 is a clear error");
}

// ---- Add Cube, Plane, Circle, Cylinder, Ico Sphere, Torus (D8-BLENDER-MESH-0097, -0103, -0095, -0099, -0101, -0105) ----------------------------
D8_EXTRA_TEST(Rw3MeshBlenderPrimitivesHaveTheirCountsAndClosedFormVolumes) {
  // Cube: default size 2.
  {
    const Mesh c = MakeCubeMesh();
    d8test::Check(CountsOf(c).v == 8 && CountsOf(c).f == 6 && c.IsClosedManifold(), "cube: 8 vertices, 6 faces, closed");
    d8test::Check(Near(c.Volume(), 8.0, 1e-5) && Near(c.Area(), 24.0, 1e-5), "cube of size 2: volume 8, area 24");
    d8test::Check(Near(MakeCubeMesh(3.0).Volume(), 27.0, 1e-4), "size 3: volume 27");
  }
  // Plane: one quad of side 2 facing +z.
  {
    const Mesh p = MakePlaneMesh();
    d8test::Check(p.FaceCount() == 1 && Near(p.Area(), 4.0, 1e-6) && Near(p.ComputeFaceNormals()[0].z, 1.0, 1e-9), "plane: 1 quad, area 4, normal +z");
  }
  // Circle of 32 vertices.
  {
    const double area = 32.0 / 2.0 * std::sin(2.0 * kPi / 32.0);
    const Mesh none = MakeCircleMesh(32, 1.0, CapFill::Nothing);
    const Mesh fan = MakeCircleMesh(32, 1.0, CapFill::TriFan);
    const Mesh ngon = MakeCircleMesh(32, 1.0, CapFill::NGon);
    d8test::Check(none.VertexCount() == 32 && none.FaceCount() == 0, "circle without fill: 32 vertices, no face");
    d8test::Check(fan.VertexCount() == 33 && fan.FaceCount() == 32 && Near(fan.Area(), area, 1e-4), W("triangle fan: 33 vertices, 32 triangles, area (n/2) sin(2 pi/n) = %.6f, got %.6f", area, fan.Area()));
    d8test::Check(ngon.VertexCount() == 32 && ngon.FaceCount() == 30 && Near(ngon.Area(), area, 1e-4), "n-gon fill (stored as n - 2 = 30 triangles): same area");
    d8test::Check(Near(MakeCircleMesh(32, 2.0, CapFill::TriFan).Area(), 4.0 * area, 1e-3), "radius 2: area x 4");
    bool threw = false;
    try { (void)MakeCircleMesh(2); } catch (const std::invalid_argument&) { threw = true; }
    d8test::Check(threw, "fewer than 3 vertices is a clear error");
  }
  // Cylinder: 32 vertices, radius 1, depth 2.
  {
    const double base = 32.0 / 2.0 * std::sin(2.0 * kPi / 32.0);
    const Mesh tube = MakeCylinderMesh(32, 1.0, 2.0, CapFill::Nothing);
    const Mesh ngon = MakeCylinderMesh(32, 1.0, 2.0, CapFill::NGon);
    const Mesh fan = MakeCylinderMesh(32, 1.0, 2.0, CapFill::TriFan);
    d8test::Check(tube.VertexCount() == 64 && tube.FaceCount() == 32 && tube.NakedEdgeLoops().size() == 2, "no caps: 64 vertices, 32 quads, two open rims");
    d8test::Check(Near(tube.Area(), 32.0 * 2.0 * std::sin(kPi / 32.0) * 2.0, 1e-4), "wall area = n * chord * depth");
    d8test::Check(ngon.VertexCount() == 64 && ngon.FaceCount() == 32 + 2 * 30 && ngon.IsClosedManifold(), "n-gon caps (stored as 30 triangles each): 64 vertices, closed");
    d8test::Check(Near(ngon.Volume(), 2.0 * base, 1e-4), W("volume = depth * (n/2) r^2 sin(2 pi/n) = %.6f, got %.6f", 2.0 * base, ngon.Volume()));
    d8test::Check(fan.VertexCount() == 66 && fan.FaceCount() == 32 + 2 * 32 && fan.IsClosedManifold() && Near(fan.Volume(), 2.0 * base, 1e-4), "triangle-fan caps: 66 vertices, 96 faces, closed, same volume");
    const BoundingBox bb = ngon.GetBoundingBox();
    d8test::Check(Near(bb.min.z, -1.0, 1e-6) && Near(bb.max.z, 1.0, 1e-6), "centred on the origin: z from -1 to 1");
  }
  // Ico sphere.
  {
    const double a = 1.0 / std::sin(2.0 * kPi / 5.0);  // edge of the icosahedron of circumradius 1
    const Mesh ico1 = MakeIcoSphereMesh(1, 1.0);
    d8test::Check(CountsOf(ico1).v == 12 && CountsOf(ico1).f == 20 && CountsOf(ico1).e == 30 && ico1.IsClosedManifold(), "subdivisions 1: the icosahedron, 12 / 30 / 20");
    d8test::Check(Near(ico1.Volume(), 5.0 / 12.0 * (3.0 + std::sqrt(5.0)) * a * a * a, 1e-4), W("icosahedron volume (5/12)(3 + sqrt 5) a^3 = %.6f", 5.0 / 12.0 * (3.0 + std::sqrt(5.0)) * a * a * a));
    d8test::Check(Near(ico1.Area(), 5.0 * std::sqrt(3.0) * a * a, 1e-4), "icosahedron area 5 sqrt(3) a^2");
    double last = ico1.Volume();
    for (int s = 2; s <= 4; ++s) {
      const Mesh m = MakeIcoSphereMesh(s, 1.0);
      const int faces = 20 * (1 << (2 * (s - 1)));
      const Counts c = CountsOf(m);
      bool on_sphere = true;
      for (int i = 0; i < m.VertexCount(); ++i) on_sphere = on_sphere && Near((Vertex(m, i) - Point3d::Origin).Length(), 1.0, 1e-5);
      char what[120];
      std::snprintf(what, sizeof what, "subdivisions %d: F = 20 * 4^(s-1) = %d, V = F/2 + 2, E = 3F/2, closed, all on the unit sphere", s, faces);
      d8test::Check(c.f == faces && c.v == faces / 2 + 2 && c.e == 3 * faces / 2 && m.IsClosedManifold() && on_sphere, what);
      d8test::Check(m.Volume() > last && m.Volume() < 4.0 / 3.0 * kPi, "the volume grows with the level and stays below the sphere 4 pi / 3");
      last = m.Volume();
    }
    d8test::Check(Near(MakeIcoSphereMesh(2, 2.0).Volume(), 8.0 * MakeIcoSphereMesh(2, 1.0).Volume(), 1e-3), "radius 2 scales the volume by 8");
    d8test::Check(MakeIcoSphereMesh(2).FaceCount() == 80, "default subdivisions 2: 80 faces");
  }
  // Torus: defaults 48 x 12, radii 1 and 0.25.
  {
    const int ma = 48, mi = 12;
    const double R = 1.0, r = 0.25;
    const Mesh t = MakeTorusMesh();
    const Counts c = CountsOf(t);
    d8test::Check(c.v == ma * mi && c.f == ma * mi && c.e == 2 * ma * mi && c.Euler() == 0 && t.IsClosedManifold(), "torus 48 x 12: 576 vertices, 576 quads, 1152 edges, Euler characteristic 0, closed");
    const double profile_area = mi / 2.0 * r * r * std::sin(2.0 * kPi / mi);
    const double vol = ma * std::sin(2.0 * kPi / ma) * profile_area * R;
    d8test::Check(Near(t.Volume(), vol, 1e-4), W("volume = n sin(2 pi/n) * A * R = %.6f, got %.6f", vol, t.Volume()));
    d8test::Check(Near(t.Volume(), 2.0 * kPi * kPi * R * r * r, 0.05 * 2.0 * kPi * kPi * R * r * r), "within 5 percent of the smooth torus 2 pi^2 R r^2");
    const BoundingBox bb = t.GetBoundingBox();
    d8test::Check(Near(bb.max.x, R + r, 1e-5) && Near(bb.min.z, -r, 1e-5) && Near(bb.max.z, r, 1e-5), "outer radius 1.25, tube height 0.5");
  }
}

// ---- Vertex group operators (D8-BLENDER-OBJECT-0218, -0220, -0224, -0226, -0230, -0232, -0234, -0238, -0241, -0243; MESH-0141) -------------------
D8_EXTRA_TEST(Rw3VertexGroupsAssignInvertNormalizeQuantizeLimitCleanSortAndPaste) {
  // Assign / Remove / Delete Weight / Paste Weight.
  {
    VertexGroupSet set(6);
    const int a = AddVertexGroup(set, "A");
    AssignToGroup(set, a, {0, 1, 2}, 0.5f);
    float w = -1;
    d8test::Check(set.Weight(0, a, w) && w == 0.5f && set.Weight(2, a, w) && !set.Weight(3, a, w), "assigned vertices 0, 1, 2 have weight 0.5, vertex 3 is not a member");
    AssignToGroup(set, a, {1}, 0.25f);
    d8test::Check(set.Weight(1, a, w) && w == 0.25f && set.Weight(0, a, w) && w == 0.5f, "assigning again replaces only that vertex");
    RemoveFromGroup(set, a, {0});
    d8test::Check(!set.Weight(0, a, w) && set.Weight(1, a, w) && set.Weight(2, a, w), "removing vertex 0 leaves the others");
    PasteWeightToVertices(set, a, 1, {3, 4});
    d8test::Check(set.Weight(3, a, w) && w == 0.25f && set.Weight(4, a, w) && w == 0.25f && set.Weight(2, a, w) && w == 0.5f, "pasting vertex 1's 0.25 to vertices 3 and 4");
    bool no_source = false, bad_weight = false, bad_group = false;
    try { PasteWeightToVertices(set, a, 5, {0}); } catch (const std::invalid_argument&) { no_source = true; }
    try { AssignToGroup(set, a, {0}, 1.5f); } catch (const std::invalid_argument&) { bad_weight = true; }
    try { AssignToGroup(set, 7, {0}, 0.5f); } catch (const std::invalid_argument&) { bad_group = true; }
    d8test::Check(no_source && bad_weight && bad_group, "a source outside the group, a weight above 1 and a group out of range are clear errors");
  }
  // Invert: 0.2 -> 0.8, 0.7 -> 0.3, 1.0 -> 0 (dropped), non-members come in at 1.
  {
    VertexGroupSet set(6);
    const int a = AddVertexGroup(set, "A");
    AssignToGroup(set, a, {0}, 0.2f); AssignToGroup(set, a, {1}, 0.7f); AssignToGroup(set, a, {2}, 1.0f);
    VertexGroupSet keep = set;
    InvertGroup(set, a);
    float w = -1;
    d8test::Check(set.Weight(0, a, w) && Near(w, 0.8, 1e-6) && set.Weight(1, a, w) && Near(w, 0.3, 1e-6), "0.2 -> 0.8 and 0.7 -> 0.3");
    d8test::Check(!set.Weight(2, a, w), "1.0 -> 0 and the vertex is removed");
    d8test::Check(set.Weight(3, a, w) && w == 1.0f && set.Weight(5, a, w) && w == 1.0f, "former non-members are added with weight 1");
    InvertGroup(keep, a, false, false);
    d8test::Check(keep.Weight(2, a, w) && w == 0.0f && !keep.Weight(3, a, w), "without add / remove: 1.0 -> 0 stays a member and non-members stay out");
  }
  // Normalize: 0.2, 0.4, 0.1 -> 0.5, 1, 0.25.
  {
    VertexGroupSet set(4);
    const int a = AddVertexGroup(set, "A");
    AssignToGroup(set, a, {0}, 0.2f); AssignToGroup(set, a, {1}, 0.4f); AssignToGroup(set, a, {2}, 0.1f);
    NormalizeGroup(set, a);
    float w0, w1, w2;
    set.Weight(0, a, w0); set.Weight(1, a, w1); set.Weight(2, a, w2);
    d8test::Check(Near(w0, 0.5, 1e-6) && Near(w1, 1.0, 1e-6) && Near(w2, 0.25, 1e-6), "weights divided by the highest: 0.5, 1, 0.25");
    VertexGroupSet zeros(2);
    const int z = AddVertexGroup(zeros, "Z");
    AssignToGroup(zeros, z, {0, 1}, 0.0f);
    NormalizeGroup(zeros, z);
    float w = -1;
    d8test::Check(zeros.Weight(0, z, w) && w == 0.0f, "an all-zero group is unchanged");
  }
  // Quantize to 4 steps.
  {
    VertexGroupSet set(8);
    const int a = AddVertexGroup(set, "A");
    const float in[7] = {0.10f, 0.13f, 0.37f, 0.38f, 0.62f, 0.63f, 0.90f};
    const float out[7] = {0.0f, 0.25f, 0.25f, 0.5f, 0.5f, 0.75f, 1.0f};
    for (int i = 0; i < 7; ++i) AssignToGroup(set, a, {i}, in[i]);
    QuantizeGroup(set, a, 4);
    bool ok = true;
    for (int i = 0; i < 7; ++i) {
      float w = -1;
      ok = ok && set.Weight(i, a, w) && Near(w, out[i], 1e-6);
    }
    d8test::Check(ok, "round(w * 4) / 4: 0.10 -> 0, 0.13 -> 0.25, 0.37 -> 0.25, 0.38 -> 0.5, 0.62 -> 0.5, 0.63 -> 0.75, 0.90 -> 1");
    VertexGroupSet one(2);
    const int b = AddVertexGroup(one, "B");
    AssignToGroup(one, b, {0}, 0.4f); AssignToGroup(one, b, {1}, 0.6f);
    QuantizeGroup(one, b, 1);
    float w0, w1;
    one.Weight(0, b, w0); one.Weight(1, b, w1);
    d8test::Check(w0 == 0.0f && w1 == 1.0f, "one step: 0.4 -> 0, 0.6 -> 1");
    bool threw = false;
    try { QuantizeGroup(one, b, 0); } catch (const std::invalid_argument&) { threw = true; }
    d8test::Check(threw, "zero steps is a clear error");
  }
  // Limit the number of weights per vertex.
  {
    VertexGroupSet set(3);
    for (const char* name : {"A", "B", "C", "D"}) AddVertexGroup(set, name);
    AssignToGroup(set, 0, {0}, 0.9f); AssignToGroup(set, 1, {0}, 0.5f); AssignToGroup(set, 2, {0}, 0.3f); AssignToGroup(set, 3, {0}, 0.1f);
    AssignToGroup(set, 0, {1}, 0.4f); AssignToGroup(set, 3, {1}, 0.6f);
    LimitGroupsPerVertex(set, 2);
    float w;
    d8test::Check(set.weights[0].size() == 2 && set.Weight(0, 0, w) && set.Weight(0, 1, w) && !set.Weight(0, 2, w) && !set.Weight(0, 3, w), "vertex 0 keeps its two largest (A 0.9, B 0.5)");
    d8test::Check(set.weights[1].size() == 2 && set.weights[2].empty(), "a vertex with two groups is unchanged; one with none stays empty");
    d8test::Check(set.Weight(0, 0, w) && w == 0.9f, "kept weights are not renormalised");
    LimitGroupsPerVertex(set, 1);
    d8test::Check(set.weights[1].size() == 1 && set.Weight(1, 3, w) && w == 0.6f, "limit 1: vertex 1 keeps D 0.6");
    bool threw = false;
    try { LimitGroupsPerVertex(set, -1); } catch (const std::invalid_argument&) { threw = true; }
    d8test::Check(threw, "a negative limit is a clear error");
  }
  // Clean.
  {
    VertexGroupSet set(4);
    const int a = AddVertexGroup(set, "A");
    const int b = AddVertexGroup(set, "B");
    AssignToGroup(set, a, {0}, 0.0f); AssignToGroup(set, a, {1}, 0.05f); AssignToGroup(set, a, {2}, 0.5f); AssignToGroup(set, a, {3}, 0.05f);
    AssignToGroup(set, b, {3}, 0.8f);
    VertexGroupSet single = set;
    CleanGroup(set, a, 0.1f, false);
    float w;
    d8test::Check(!set.Weight(0, a, w) && !set.Weight(1, a, w) && set.Weight(2, a, w) && !set.Weight(3, a, w) && set.Weight(3, b, w), "weights at or below 0.1 leave group A; 0.5 stays; other groups are untouched");
    CleanGroup(single, a, 0.1f, true);
    d8test::Check(single.Weight(0, a, w) && single.Weight(1, a, w) && single.Weight(2, a, w) && !single.Weight(3, a, w), "keep single: vertices 0 and 1 would end up in no group and keep their weight; vertex 3 is also in B and loses A");
  }
  // Sort by name, natural and case-insensitive.
  {
    VertexGroupSet set(2);
    for (const char* name : {"Spine", "arm.L", "Arm.R", "leg10", "leg2"}) AddVertexGroup(set, name);
    set.active = 0;
    AssignToGroup(set, 0, {0}, 1.0f);
    AssignToGroup(set, 3, {0}, 0.5f);
    SortGroupsByName(set);
    d8test::Check(set.names == std::vector<std::string>({"arm.L", "Arm.R", "leg2", "leg10", "Spine"}), "sorted: arm.L, Arm.R, leg2, leg10, Spine");
    float w;
    d8test::Check(set.active == 4 && set.Weight(0, 4, w) && w == 1.0f && set.Weight(0, 3, w) && w == 0.5f && set.weights[0].size() == 2, "the active group and the weights follow their groups (Spine -> 4, leg10 -> 3)");
  }
  // Select Ungrouped.
  {
    VertexGroupSet set(6);
    const int a = AddVertexGroup(set, "A");
    const int b = AddVertexGroup(set, "B");
    AssignToGroup(set, a, {0, 1}, 0.5f);
    AssignToGroup(set, b, {1, 2}, 0.0f);
    d8test::Check(UngroupedVertices(set) == std::vector<int>({3, 4, 5}), "vertices 3, 4, 5 are in no group (a weight of 0 still counts as a member)");
    RemoveFromGroup(set, a, {0});
    d8test::Check(UngroupedVertices(set) == std::vector<int>({0, 3, 4, 5}), "after vertex 0 leaves its only group it is ungrouped");
  }
}

// ---- WeightVG Edit, WeightVG Proximity, Weight Gradient (D8-BLENDER-MODIFIER-0074, -0076, D8-BLENDER-PAINT-0050) ------------------------------
D8_EXTRA_TEST(Rw3WeightFalloffProximityAndGradientFollowTheirFormulas) {
  // Falloff curves at w = 0.25.
  d8test::Check(Near(MapWeight(WeightFalloff::Linear, 0.25f), 0.25, 1e-6) && Near(MapWeight(WeightFalloff::Sharp, 0.25f), 0.0625, 1e-6), "linear 0.25, sharp w^2 = 0.0625");
  d8test::Check(Near(MapWeight(WeightFalloff::Smooth, 0.25f), 3 * 0.0625 - 2 * 0.015625, 1e-6) && Near(MapWeight(WeightFalloff::Root, 0.25f), 0.5, 1e-6), "smooth 3w^2 - 2w^3 = 0.15625, root sqrt(w) = 0.5");
  d8test::Check(Near(MapWeight(WeightFalloff::Sphere, 0.25f), std::sqrt(0.4375), 1e-6) && Near(MapWeight(WeightFalloff::Invert, 0.25f), 0.75, 1e-6), "sphere sqrt(2w - w^2) = 0.661438, invert 0.75");
  d8test::Check(MapWeight(WeightFalloff::Step, 0.25f) == 0.0f && MapWeight(WeightFalloff::Step, 0.5f) == 1.0f, "step: 0.25 -> 0, 0.5 -> 1");
  for (WeightFalloff f : {WeightFalloff::Linear, WeightFalloff::Sharp, WeightFalloff::Smooth, WeightFalloff::Root, WeightFalloff::Sphere}) {
    d8test::Check(Near(MapWeight(f, 0.0f), 0.0, 1e-6) && Near(MapWeight(f, 1.0f), 1.0, 1e-6), "every ramp-type curve fixes 0 and 1");
  }
  const Mesh sheet = FlatSheet(4, 1.0);  // vertex (i, j) = 5 j + i at (i, j, 0)
  // Proximity to the origin, 0 at distance 1 and 1 at distance 3.
  {
    const std::vector<float> w = ProximityWeightsToPoint(sheet, Point3d(0, 0, 0), 1.0, 3.0);
    d8test::Check(w.size() == 25 && Near(w[0], 0.0, 1e-6) && Near(w[1], 0.0, 1e-6) && Near(w[2], 0.5, 1e-6) && Near(w[3], 1.0, 1e-6) && Near(w[4], 1.0, 1e-6), "along x: distance 0, 1, 2, 3, 4 -> 0, 0, 0.5, 1, 1");
    d8test::Check(Near(w[6], (std::sqrt(2.0) - 1.0) / 2.0, 1e-6), "the diagonal vertex at sqrt 2: (sqrt 2 - 1) / 2 = 0.2071");
    const std::vector<float> inv = ProximityWeightsToPoint(sheet, Point3d(0, 0, 0), 3.0, 1.0);
    d8test::Check(Near(inv[0], 1.0, 1e-6) && Near(inv[2], 0.5, 1e-6) && Near(inv[4], 0.0, 1e-6), "min above max inverts the ramp: distance 0 -> 1, 2 -> 0.5, 4 -> 0");
    bool threw = false;
    try { (void)ProximityWeightsToPoint(sheet, Point3d(0, 0, 0), 2.0, 2.0); } catch (const std::invalid_argument&) { threw = true; }
    d8test::Check(threw, "equal distances are a clear error");
  }
  // Proximity to a surface: the plane z = 2, so every vertex of the sheet is 2 away; one vertex lifted to z = 1 is 1 away.
  {
    const Mesh plane = MakePlaneMesh(100.0).Transform(ON_Xform::TranslationTransformation(0.0, 0.0, 2.0));
    Mesh lifted = sheet;
    lifted.raw().m_V[12].z = 1.0f;
    const std::vector<float> w = ProximityWeightsToMesh(lifted, plane, 0.0, 4.0);
    d8test::Check(Near(w[0], 0.5, 1e-5) && Near(w[24], 0.5, 1e-5) && Near(w[12], 0.25, 1e-5), "distance 2 of 4 -> 0.5, the lifted vertex at distance 1 -> 0.25");
  }
  // Gradient.
  {
    const std::vector<float> lin = GradientWeights(sheet, Point3d(0, 0, 0), Point3d(4, 0, 0), 1.0f, 0.0f, GradientKind::Linear);
    d8test::Check(Near(lin[0], 1.0, 1e-6) && Near(lin[2], 0.5, 1e-6) && Near(lin[4], 0.0, 1e-6) && Near(lin[22], 0.5, 1e-6), "linear 1 -> 0 over x = 0..4: 1, 0.5, 0 and independent of y");
    const std::vector<float> rad = GradientWeights(sheet, Point3d(2, 2, 0), Point3d(4, 2, 0), 0.0f, 1.0f, GradientKind::Radial);
    d8test::Check(Near(rad[12], 0.0, 1e-6) && Near(rad[13], 0.5, 1e-6) && Near(rad[14], 1.0, 1e-6) && Near(rad[22], 1.0, 1e-6) && Near(rad[0], 1.0, 1e-6), "radial 0 -> 1 over radius 2 about (2,2): 0, 0.5, 1 and clamped beyond");
    const std::vector<float> behind = GradientWeights(sheet, Point3d(2, 0, 0), Point3d(4, 0, 0), 1.0f, 0.0f, GradientKind::Linear);
    d8test::Check(Near(behind[0], 1.0, 1e-6) && Near(behind[4], 0.0, 1e-6), "before the start the start weight holds; beyond the end the end weight holds");
    bool threw = false;
    try { (void)GradientWeights(sheet, Point3d(1, 1, 0), Point3d(1, 1, 0), 0.f, 1.f, GradientKind::Linear); } catch (const std::invalid_argument&) { threw = true; }
    d8test::Check(threw, "coinciding start and end are a clear error");
  }
}
