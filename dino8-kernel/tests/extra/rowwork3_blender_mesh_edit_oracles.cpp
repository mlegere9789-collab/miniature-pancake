// Register row tests for the mesh-subd-sculpt factory, round 3 (slice A): kernel outcomes of Blender's edit-mode operators and
// modifiers, each against a closed form that does not use the code under test:
//  * Bisect: a plane at distance d from one face of an a x b x c box splits it into boxes of volume d b c and (a - d) b c; a plane
//    through two opposite vertical edges (x + y = 10) halves the 10-cube into prisms of volume 500 with a cap of area 10 * 10 sqrt 2.
//  * Edge Split / Split: a vertex is duplicated once per fan of faces that stay connected, so a cube split at every edge has 24
//    vertices in 6 pieces, and the 8-sided prism's wall edges turn by exactly 360 / 8 degrees.
//  * Merge: collapsing the four top vertices of the 10-cube leaves a pyramid (V 5, F 5, E 8; volume 100 * 10 / 3).
//  * Poke: each of the f faces of k corners adds one vertex and k triangles; offset h on a square face of side a adds a pyramid of
//    volume a^2 h / 3 and replaces the area a^2 by 4 (a / 2) sqrt((a / 2)^2 + h^2).
//  * Smooth: with factor 1 a lifted grid vertex returns to the mean of its four neighbours.
//  * Screw: a closed rectangle profile of radii a < b and height h turned through n steps has volume (n / 2) sin(2 pi / n)(b^2 - a^2) h.
//  * Mask, Build: face counts of the kept faces.
//  * Extrude individual faces / edges: volume and area change by the prism that is added.
//  * Simple Deform twist/taper: each vertex maps to the rotation / scaling of its offset from the axis by the axial fraction.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dino8/kernel/deform.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/mesh_edit.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

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

// Box [x0,x1] x [y0,y1] x [z0,z1] as 6 outward quads on 8 shared vertices (vertex index = x + 2 y + 4 z).
// Faces in order: z-, y-, y+, x-, x+, z+.
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

// n x n quads of side `pitch` in the plane z = 0, normal +z; vertex (i, j) has index j * (n + 1) + i.
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

char g_what[200];
const char* W(const char* fmt, double a = 0, double b = 0, double c = 0) {
  std::snprintf(g_what, sizeof g_what, fmt, a, b, c);
  return g_what;
}

}  // namespace

// ---- Bisect (D8-BLENDER-MESH-0005) ---------------------------------------------------------------------------------------------------
// Boxes cut by a plane: volumes and areas follow from the box dimensions; the counts of the unfilled cut follow from counting cut edges.
D8_EXTRA_TEST(Rw3MeshBisectCutsBoxesIntoClosedFormPiecesWithShareableCaps) {
  using dino8::kernel::BisectMesh;
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  // Plane x = 3, nothing cleared, no fill: the four faces parallel to x get one new edge each (4 new vertices, 4 new faces).
  {
    const Mesh cut = BisectMesh(box, Point3d(3, 0, 0), Vector3d(1, 0, 0));
    const Counts c = CountsOf(cut);
    d8test::Check(c.v == 12 && c.f == 10 && c.e == 20, "bisect x = 3 without fill: V 8+4 = 12, F 6+4 = 10, E 12 + 4 cut edges + 4 split edges = 20");
    d8test::Check(c.Euler() == 2 && cut.IsClosedManifold(), "the unfilled cut of a closed box is still one closed manifold (Euler characteristic 2)");
    d8test::Check(Near(cut.Volume(), 1000.0, 1e-3) && Near(cut.Area(), 600.0, 1e-3), "volume 1000 and area 600 are unchanged by the cut");
  }
  // Keep x <= 3 with a cap: box 3 x 10 x 10.
  {
    const Mesh part = BisectMesh(box, Point3d(3, 0, 0), Vector3d(1, 0, 0), true, false, true);
    d8test::Check(part.IsClosedManifold(), "x <= 3 with fill is a closed manifold");
    d8test::Check(Near(part.Volume(), 300.0, 1e-3), W("x <= 3 piece volume = 3 * 10 * 10 = 300, got %.6f", part.Volume()));
    d8test::Check(Near(part.Area(), 320.0, 1e-3), W("x <= 3 piece area = 2 (30 + 30 + 100) = 320, got %.6f", part.Area()));
    d8test::Check(Near(part.GetBoundingBox().max.x, 3.0, 1e-5), "the cut plane is the new x maximum");
  }
  // Keep x >= 3 with a cap: box 7 x 10 x 10.
  {
    const Mesh part = BisectMesh(box, Point3d(3, 0, 0), Vector3d(1, 0, 0), true, true, false);
    d8test::Check(part.IsClosedManifold(), "x >= 3 with fill is a closed manifold");
    d8test::Check(Near(part.Volume(), 700.0, 1e-3) && Near(part.Area(), 480.0, 1e-3), "x >= 3 piece: volume 700, area 2 (70 + 70 + 100) = 480");
  }
  // Both sides kept with fill: two closed boxes sharing the cut vertices; volumes add to 1000, two cap faces are added.
  {
    const Mesh both = BisectMesh(box, Point3d(3, 0, 0), Vector3d(1, 0, 0), true);
    d8test::Check(both.FaceCount() == 12 && Near(both.Volume(), 1000.0, 1e-3), "both sides kept with fill: 10 + 2 cap faces, volume 300 + 700");
    d8test::Check(Near(both.Area(), 800.0, 1e-3), "both sides kept with fill: area 320 + 480 = 800");
  }
  // The plane x + y = 10 passes through the vertical edges at (10, 0) and (0, 10): two triangular prisms of legs 10, height 10.
  {
    const Mesh part = BisectMesh(box, Point3d(5, 5, 0), Vector3d(1, 1, 0), true, false, true);
    const Counts c = CountsOf(part);
    d8test::Check(part.IsClosedManifold() && c.v == 6 && c.f == 5, "diagonal cut through two edges: triangular prism with V 6, F 5 (2 triangles, 2 side quads, 1 cap)");
    d8test::Check(Near(part.Volume(), 500.0, 1e-3), W("diagonal half volume = (1/2) 10 10 10 = 500, got %.6f", part.Volume()));
    const double expected_area = 2 * 50.0 + 2 * 100.0 + 10.0 * 10.0 * std::sqrt(2.0);
    d8test::Check(Near(part.Area(), expected_area, 1e-3), W("diagonal half area = 100 + 200 + 100 sqrt 2 = %.6f, got %.6f", expected_area, part.Area()));
  }
  // A triangulated box has vertices in the middle of the cap sides; the volume does not depend on that.
  {
    const Mesh tri = TriBox(0, 0, 0, 10, 10, 10);
    const Mesh part = BisectMesh(tri, Point3d(3, 0, 0), Vector3d(1, 0, 0), true, false, true);
    d8test::Check(part.IsClosedManifold() && Near(part.Volume(), 300.0, 1e-3), "triangulated box cut at x = 3 and capped: closed, volume 300");
  }
  // A plane that misses the mesh changes nothing, and a zero normal is refused.
  {
    const Mesh miss = BisectMesh(box, Point3d(20, 0, 0), Vector3d(1, 0, 0));
    d8test::Check(CountsOf(miss).f == 6 && CountsOf(miss).v == 8, "a plane beyond the box adds nothing");
    bool threw = false;
    try { (void)BisectMesh(box, Point3d(0, 0, 0), Vector3d(0, 0, 0)); } catch (const std::invalid_argument&) { threw = true; }
    d8test::Check(threw, "a zero plane normal is a clear error");
  }
}

// ---- Edge Split modifier / Edge Split / Split (D8-BLENDER-MODIFIER-0015, -MESH-0035, -MESH-0153) -----------------------------------
D8_EXTRA_TEST(Rw3MeshEdgeSplitDuplicatesVerticesPerConnectedFaceFan) {
  using namespace dino8::kernel;
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  // The 90 degree edges of a cube are split by a 30 degree limit (and by 89) but not by 91: the limit is "greater than".
  {
    const Mesh s = EdgeSplitByAngle(box, 30.0);
    const Counts c = CountsOf(s);
    d8test::Check(c.v == 24 && c.f == 6 && c.e == 24, "cube, every edge split: V 6 * 4 = 24, F 6, E 6 * 4 = 24");
    d8test::Check(s.SplitDisjointPieces().size() == 6, "cube, every edge split: six separate faces");
    d8test::Check(Near(s.Area(), 600.0, 1e-3), "splitting does not move anything: area stays 600");
    d8test::Check(CountsOf(EdgeSplitByAngle(box, 89.0)).v == 24, "an 89 degree limit still splits the 90 degree edges");
    d8test::Check(CountsOf(EdgeSplitByAngle(box, 91.0)).v == 8, "a 91 degree limit splits nothing");
  }
  // A flat sheet has only 0 degree edges.
  d8test::Check(CountsOf(EdgeSplitByAngle(FlatSheet(3, 1.0), 1.0)).v == 16, "a flat 3 x 3 sheet has no sharp edge: V stays 16");
  // A tent: two unit quads joined along an edge and folded by 60 degrees (normals differ by 60).
  {
    Mesh tent;
    tent.raw().m_V.Append(ON_3fPoint(0, 0, 0)); tent.raw().m_V.Append(ON_3fPoint(1, 0, 0)); tent.raw().m_V.Append(ON_3fPoint(1, 1, 0)); tent.raw().m_V.Append(ON_3fPoint(0, 1, 0));
    // second quad rotated about the shared edge x = 1 by 60 degrees: new points (1 + cos 60, y, sin 60) -> the faces' normals differ by 60 degrees
    const float cx = float(1.0 + std::cos(kPi / 3.0)), cz = float(std::sin(kPi / 3.0));
    tent.raw().m_V.Append(ON_3fPoint(cx, 0, cz)); tent.raw().m_V.Append(ON_3fPoint(cx, 1, cz));
    AddQuad(tent, 0, 1, 2, 3);
    AddQuad(tent, 1, 4, 5, 2);
    d8test::Check(CountsOf(EdgeSplitByAngle(tent, 50.0)).v == 8, "tent folded by 60: a 50 degree limit splits the ridge, V 6 + 2 = 8");
    d8test::Check(CountsOf(EdgeSplitByAngle(tent, 70.0)).v == 6, "tent folded by 60: a 70 degree limit keeps V = 6");
    d8test::Check(SharpMeshEdges(tent, 50.0).size() == 1, "exactly one sharp edge (the ridge)");
  }
  // 8-sided open prism: neighbouring wall normals differ by 360 / 8 = 45 degrees exactly.
  {
    Mesh tube;
    for (int i = 0; i < 8; ++i) {
      const double a = 2.0 * kPi * i / 8.0;
      tube.raw().m_V.Append(ON_3fPoint(float(5 * std::cos(a)), float(5 * std::sin(a)), 0));
      tube.raw().m_V.Append(ON_3fPoint(float(5 * std::cos(a)), float(5 * std::sin(a)), 10));
    }
    for (int i = 0; i < 8; ++i) {
      const int j = (i + 1) % 8;
      AddQuad(tube, 2 * i, 2 * j, 2 * j + 1, 2 * i + 1);
    }
    d8test::Check(CountsOf(tube).v == 16, "prism wall has 16 vertices");
    d8test::Check(CountsOf(EdgeSplitByAngle(tube, 44.0)).v == 32, "8-sided wall, 44 degree limit: every one of the 8 wall edges splits, V 32");
    d8test::Check(CountsOf(EdgeSplitByAngle(tube, 46.0)).v == 16, "8-sided wall, 46 degree limit: nothing splits");
  }
  // mesh.edge_split on an explicit edge list: one edge of the sheet.
  {
    const Mesh sheet = FlatSheet(2, 1.0);  // vertices 0..8, centre vertex 4
    const Mesh s = SplitMeshEdges(sheet, {{4, 5}});
    d8test::Check(CountsOf(s).v == 10, "splitting the spoke 4-5 of a 2 x 2 sheet copies only the boundary end 5: the faces around the centre stay connected the other way round");
    const Mesh t = SplitMeshEdges(sheet, {{3, 4}, {4, 5}, {4, 7}, {1, 4}});
    d8test::Check(CountsOf(t).v == 9 + 3 + 4 && t.SplitDisjointPieces().size() == 4, "splitting all four spokes separates the four quads: the centre is copied 3 times and each of the four boundary ends once, V 16");
  }
}

D8_EXTRA_TEST(Rw3MeshSplitSeparatesTheSelectionWithoutMovingIt) {
  using namespace dino8::kernel;
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  // Top face (index 5) alone: its 4 corners are copied; the box loses its lid but nothing moves.
  {
    const Mesh s = SplitOffFaces(box, {5});
    const Counts c = CountsOf(s);
    d8test::Check(c.v == 12 && c.f == 6 && c.e == 16, "top face split off: V 8 + 4 = 12, E 12 + 4 = 16, F 6");
    d8test::Check(s.SplitDisjointPieces().size() == 2, "two pieces: the lid and the open box");
    d8test::Check(Near(s.Area(), 600.0, 1e-3), "nothing moved: area stays 600");
    d8test::Check(!s.IsClosedManifold(), "the lid and the open box are no longer closed");
  }
  // Two neighbouring faces (top 5 and front 1): their region has a 6-edge boundary loop, so V = 8 + 6 = 14 and E = 12 + 6 = 18.
  {
    const Mesh s = SplitOffFaces(box, {1, 5});
    const Counts c = CountsOf(s);
    d8test::Check(c.v == 14 && c.e == 18 && c.f == 6, "two adjacent faces split off: boundary loop of 6 edges -> V 14, E 18");
    d8test::Check(s.SplitDisjointPieces().size() == 2, "two pieces");
  }
  // All faces selected: nothing borders an unselected face, so nothing splits.
  d8test::Check(CountsOf(SplitOffFaces(box, {0, 1, 2, 3, 4, 5})).v == 8, "selecting everything changes nothing");
  bool threw = false;
  try { (void)SplitOffFaces(box, {9}); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a face index out of range is a clear error");
}

// ---- Merge (D8-BLENDER-MESH-0077) -----------------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshMergeCollapsesVerticesWithClosedFormPyramidVolumeAndArea) {
  using namespace dino8::kernel;
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  // The four top vertices (4, 5, 6, 7) merged at their centre (5, 5, 10): the top face vanishes, the sides become triangles.
  {
    const Mesh m = MergeVerticesAt(box, {4, 5, 6, 7}, MergeTarget::Center);
    const Counts c = CountsOf(m);
    d8test::Check(c.v == 5 && c.f == 5 && c.e == 8 && c.Euler() == 2, "top four merged: V 5, F 5, E 8, Euler characteristic 2");
    d8test::Check(m.IsClosedManifold(), "result is a closed manifold");
    d8test::Check(Near(m.Volume(), 100.0 * 10.0 / 3.0, 1e-3), W("pyramid volume = 100 * 10 / 3 = %.6f, got %.6f", 100.0 * 10.0 / 3.0, m.Volume()));
    const double area = 100.0 + 4 * 0.5 * 10.0 * std::sqrt(25.0 + 100.0);
    d8test::Check(Near(m.Area(), area, 1e-3), W("pyramid area = 100 + 4 * 5 sqrt(125) = %.6f, got %.6f", area, m.Area()));
  }
  // At first / at last: the apex is the first / last listed vertex, (0, 0, 10) / (10, 10, 10). Volume is still base * height / 3.
  {
    const Mesh first = MergeVerticesAt(box, {4, 5, 6, 7}, MergeTarget::First);
    const double area_first = 100.0 + 50.0 + 50.0 + 2 * 0.5 * 10.0 * std::sqrt(200.0);  // two right triangles of legs 10, two of base 10, height 10 sqrt 2
    d8test::Check(Near(first.Volume(), 1000.0 / 3.0, 1e-3), "at first: apex height 10 over the 100 base gives volume 1000 / 3");
    d8test::Check(Near(first.Area(), area_first, 1e-3), W("at first: area = 100 + 50 + 50 + 10 * 10 sqrt 2 = %.6f, got %.6f", area_first, first.Area()));
    const Mesh last = MergeVerticesAt(box, {4, 5, 6, 7}, MergeTarget::Last);
    d8test::Check(Near(last.Area(), area_first, 1e-3), "at last: the mirrored corner gives the same area");
    const Mesh at = MergeVerticesAt(box, {4, 5, 6, 7}, MergeTarget::Point, Point3d(2, 3, 10));
    d8test::Check(Near(at.Volume(), 1000.0 / 3.0, 1e-3) && at.IsClosedManifold(), "at a given point of the top plane: volume 1000 / 3");
  }
  // One edge (4-5) collapsed: the top and the front quad become triangles: V 7, F 6, E 11.
  {
    const Mesh m = MergeVerticesAt(box, {4, 5}, MergeTarget::Center);
    const Counts c = CountsOf(m);
    int tris = 0;
    for (int i = 0; i < m.raw().m_F.Count(); ++i) tris += m.raw().m_F[i].IsQuad() ? 0 : 1;
    d8test::Check(c.v == 7 && c.f == 6 && c.e == 11 && c.Euler() == 2 && tris == 2, "edge 4-5 collapsed: V 7, F 6 (2 triangles), E 11");
  }
  bool threw = false;
  try { (void)MergeVerticesAt(box, {}, MergeTarget::Center); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "an empty vertex list is a clear error");
}

// ---- Poke Faces (D8-BLENDER-MESH-0085) ------------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshPokeFacesAddsOneVertexAndOneTrianglePerEdgeWithClosedFormVolume) {
  using namespace dino8::kernel;
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  // All six faces, offset h = 2.5: V 8 + 6, F 6 * 4, E 12 + 6 * 4, Euler 2; volume 1000 + 6 * 100 h / 3; area 24 * (1/2) 5 ... = 120 sqrt(25 + h^2).
  {
    const double h = 2.5;
    const Mesh m = PokeFaces(box, {0, 1, 2, 3, 4, 5}, h);
    const Counts c = CountsOf(m);
    d8test::Check(c.v == 14 && c.f == 24 && c.e == 36 && c.Euler() == 2, "all faces poked: V 14, F 24, E 36, Euler characteristic 2");
    d8test::Check(m.IsClosedManifold(), "poked cube is closed");
    d8test::Check(Near(m.Volume(), 1000.0 + 200.0 * h, 1e-3), W("volume = 1000 + 200 h = %.6f, got %.6f", 1000.0 + 200.0 * h, m.Volume()));
    const double area = 120.0 * std::sqrt(25.0 + h * h);
    d8test::Check(Near(m.Area(), area, 1e-3), W("area = 120 sqrt(25 + h^2) = %.6f, got %.6f", area, m.Area()));
  }
  // A negative offset dents the faces inward; zero offset keeps the shape.
  {
    d8test::Check(Near(PokeFaces(box, {5}, -2.0).Volume(), 1000.0 - 100.0 * 2.0 / 3.0, 1e-3), "top face poked inward by 2: volume 1000 - 200 / 3");
    d8test::Check(Near(PokeFaces(box, {0, 1, 2, 3, 4, 5}, 0.0).Volume(), 1000.0, 1e-3), "offset 0: volume unchanged");
  }
  // Only one face: V 9, F 9, E 16.
  {
    const Counts c = CountsOf(PokeFaces(box, {5}, 1.0));
    d8test::Check(c.v == 9 && c.f == 9 && c.e == 16 && c.Euler() == 2, "one face poked: V 9, F 5 + 4 = 9, E 12 + 4 = 16");
  }
  // Poke centre of a trapezoid (parallel sides 4 and 2, height 2): area centroid at height h (a + 2 b) / (3 (a + b)) = 2 * 8 / 18 above the long side;
  // the vertex mean and the bounding-box centre are at height 1.
  {
    Mesh trap;
    trap.raw().m_V.Append(ON_3fPoint(0, 0, 0)); trap.raw().m_V.Append(ON_3fPoint(4, 0, 0)); trap.raw().m_V.Append(ON_3fPoint(3, 2, 0)); trap.raw().m_V.Append(ON_3fPoint(1, 2, 0));
    AddQuad(trap, 0, 1, 2, 3);
    const Mesh weighted = PokeFaces(trap, {0}, 0.5, PokeCenter::MedianWeighted);
    const Mesh median = PokeFaces(trap, {0}, 0.5, PokeCenter::Median);
    const Mesh bounds = PokeFaces(trap, {0}, 0.5, PokeCenter::Bounds);
    const Point3d w = Vertex(weighted, weighted.VertexCount() - 1), md = Vertex(median, median.VertexCount() - 1), b = Vertex(bounds, bounds.VertexCount() - 1);
    d8test::Check(Near(w.x, 2.0, 1e-5) && Near(w.y, 16.0 / 18.0, 1e-5) && Near(w.z, 0.5, 1e-5), W("median weighted centre (2, 16/18, 0.5), got y = %.6f", w.y));
    d8test::Check(Near(md.x, 2.0, 1e-5) && Near(md.y, 1.0, 1e-5) && Near(md.z, 0.5, 1e-5), "median (corner mean) centre (2, 1, 0.5)");
    d8test::Check(Near(b.x, 2.0, 1e-5) && Near(b.y, 1.0, 1e-5) && Near(b.z, 0.5, 1e-5), "bounds centre (2, 1, 0.5)");
  }
}

// ---- Connect Vertex Path (D8-BLENDER-MESH-0165) ---------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshConnectVerticesSplitsOneFaceIntoTwoAlongTheChord) {
  using namespace dino8::kernel;
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  const Mesh m = ConnectFaceVertices(box, 5, 4, 7);  // top face, opposite corners (0,0,10) and (10,10,10)
  const Counts c = CountsOf(m);
  d8test::Check(c.v == 8 && c.f == 7 && c.e == 13 && c.Euler() == 2, "one chord: V 8, F 6 + 1 = 7, E 12 + 1 = 13, Euler characteristic 2");
  d8test::Check(m.IsClosedManifold() && Near(m.Volume(), 1000.0, 1e-3) && Near(m.Area(), 600.0, 1e-3), "the cut is in the face plane: closed, volume 1000, area 600");
  int tris = 0;
  for (int i = 0; i < m.raw().m_F.Count(); ++i) tris += m.raw().m_F[i].IsQuad() ? 0 : 1;
  d8test::Check(tris == 2, "the quad became two triangles");
  // A hexagon-free check of the error paths: neighbouring corners, and a corner the face does not have.
  bool neighbours = false, missing = false;
  try { (void)ConnectFaceVertices(box, 5, 4, 5); } catch (const std::invalid_argument&) { neighbours = true; }
  try { (void)ConnectFaceVertices(box, 5, 4, 0); } catch (const std::invalid_argument&) { missing = true; }
  d8test::Check(neighbours && missing, "neighbouring corners and a vertex that is not on the face are clear errors");
}

// ---- Smooth Vertices / Smooth modifier (D8-BLENDER-MESH-0169, -MODIFIER-0036, -MODIFIER-0065) ------------------------------------------
D8_EXTRA_TEST(Rw3MeshSmoothMovesEachVertexTowardTheMeanOfItsEdgeNeighbours) {
  using namespace dino8::kernel;
  // 4 x 4 quads, vertex (i, j) = j * 5 + i; the centre (2, 2) is vertex 12, its edge neighbours are 7, 11, 13, 17.
  Mesh sheet = FlatSheet(4, 1.0);
  sheet.raw().m_V[12].z = 1.0f;
  {
    const Mesh s = SmoothMeshVertices(sheet, 1.0, 1);
    d8test::Check(Near(Vertex(s, 12).z, 0.0, 1e-6), "factor 1: the lifted centre returns to the mean of its four neighbours (0)");
    d8test::Check(Near(Vertex(s, 7).z, 0.25, 1e-6) && Near(Vertex(s, 13).z, 0.25, 1e-6), "each edge neighbour of the centre rises to (1 + 0 + 0 + 0) / 4 = 0.25");
    d8test::Check(Near(Vertex(s, 6).z, 0.0, 1e-6), "a diagonal neighbour is not an edge neighbour and stays 0");
  }
  {
    const Mesh s = SmoothMeshVertices(sheet, 0.5, 1);
    d8test::Check(Near(Vertex(s, 12).z, 0.5, 1e-6) && Near(Vertex(s, 7).z, 0.125, 1e-6), "factor 0.5: centre 0.5 * 1 + 0.5 * 0 = 0.5, neighbour 0.5 * 0 + 0.5 * 0.25 = 0.125");
  }
  {
    const Mesh s = SmoothMeshVertices(sheet, 1.0, 2);
    d8test::Check(Near(Vertex(s, 12).z, 0.25, 1e-6) && Near(Vertex(s, 7).z, 0.0, 1e-6), "two rounds at factor 1: centre = mean of the first-round neighbours = 0.25, neighbour = mean of zeros = 0");
  }
  {
    const Mesh s = SmoothMeshVertices(sheet, 1.0, 0);
    d8test::Check(Near(Vertex(s, 12).z, 1.0, 1e-6), "0 rounds change nothing");
  }
  // Axis restriction: a centre vertex pushed off the lattice in x returns in x only; its z is not smoothed when z is disabled.
  {
    Mesh m = FlatSheet(4, 1.0);
    m.raw().m_V[12].x = 2.2f;
    m.raw().m_V[12].z = 1.0f;
    const Mesh s = SmoothMeshVertices(m, 1.0, 1, true, false, false, {12});
    d8test::Check(Near(Vertex(s, 12).x, 2.0, 1e-6) && Near(Vertex(s, 12).z, 1.0, 1e-6) && Near(Vertex(s, 12).y, 2.0, 1e-6), "x only: x -> mean(1, 3, 2, 2) = 2, z stays 1");
    d8test::Check(Near(Vertex(s, 7).z, 0.0, 1e-6), "only the listed vertex moves");
  }
  bool threw = false;
  try { (void)SmoothMeshVertices(sheet, 1.5, 1); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a factor above 1 is a clear error");
}

// ---- Screw / Spin (D8-BLENDER-MESH-0117, -MODIFIER-0061) ------------------------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshScrewTurnsAProfileWithClosedFormVolumeAndHelixPositions) {
  using namespace dino8::kernel;
  const Point3d origin(0, 0, 0);
  const Vector3d zaxis(0, 0, 1);
  // Closed rectangle profile r in [2, 5], z in [0, 4], walked counter-clockwise in (r, z): a ring of 8 steps.
  {
    const int n = 8;
    const std::vector<Point3d> profile = {Point3d(2, 0, 0), Point3d(5, 0, 0), Point3d(5, 0, 4), Point3d(2, 0, 4)};
    const Mesh m = ScrewPolyline(profile, origin, zaxis, 2.0 * kPi, n, 0.0, 1, true);
    const Counts c = CountsOf(m);
    d8test::Check(c.v == 32 && c.f == 32 && c.e == 64 && c.Euler() == 0, "8 steps of a 4-point closed profile: V 32, F 32, E 64, Euler characteristic 0 (torus)");
    d8test::Check(m.IsClosedManifold(), "a full turn closes onto itself");
    const double vol = n / 2.0 * std::sin(2.0 * kPi / n) * (25.0 - 4.0) * 4.0;
    d8test::Check(Near(m.Volume(), vol, 1e-3), W("volume = (n/2) sin(2 pi / n) (b^2 - a^2) h = %.6f, got %.6f", vol, m.Volume()));
  }
  // Open vertical segment at r = 3, height 5, 12 steps: a tube wall of 12 planar quads.
  {
    const int n = 12;
    const std::vector<Point3d> wall = {Point3d(3, 0, 0), Point3d(3, 0, 5)};
    const Mesh tube = ScrewPolyline(wall, origin, zaxis, 2.0 * kPi, n);
    const double area = 2.0 * n * 3.0 * std::sin(kPi / n) * 5.0;
    d8test::Check(CountsOf(tube).v == 24 && CountsOf(tube).f == 12 && tube.NakedEdgeLoops().size() == 2, "12 steps of a segment: V 24, F 12, two open rims");
    d8test::Check(Near(tube.Area(), area, 1e-3), W("tube area = 2 n r sin(pi / n) h = %.6f, got %.6f", area, tube.Area()));
    // The same wall as a helix with a rise of 6 over the full turn: every quad is a parallelogram of the same area.
    const Mesh helix = ScrewPolyline(wall, origin, zaxis, 2.0 * kPi, n, 6.0);
    d8test::Check(CountsOf(helix).v == 26 && CountsOf(helix).f == 12, "with a screw offset the ends do not close: V 26, F 12");
    d8test::Check(Near(helix.Area(), area, 1e-3), "area of the helical wall is the same as the straight tube (parallelograms with the same base and height)");
    const BoundingBox bb = helix.GetBoundingBox();
    d8test::Check(Near(bb.min.z, 0.0, 1e-5) && Near(bb.max.z, 11.0, 1e-5), "z spans 0 to 5 + 6 = 11");
    // vertex k of the lower rim is at angle 2 pi k / n, height 6 k / n; the 7th copy (k = 6) is at angle pi, height 3
    const Point3d p = Vertex(helix, 2 * 6);
    d8test::Check(Near(p.x, -3.0, 1e-5) && Near(p.y, 0.0, 1e-4) && Near(p.z, 3.0, 1e-5), "copy 6 of 12 sits half a turn round at height 3");
  }
  // Two iterations of a half turn with a rise of 3 each continue the same helix: after 2 pi and 6 the profile is back above its start.
  {
    const std::vector<Point3d> wall = {Point3d(3, 0, 0), Point3d(3, 0, 5)};
    const Mesh m = ScrewPolyline(wall, origin, zaxis, kPi, 6, 3.0, 2);
    d8test::Check(CountsOf(m).v == 26 && CountsOf(m).f == 12, "2 iterations x 6 steps: 13 copies, V 26, F 12");
    const Point3d last = Vertex(m, 24);
    d8test::Check(Near(last.x, 3.0, 1e-5) && Near(last.y, 0.0, 1e-4) && Near(last.z, 6.0, 1e-5), "the last copy is one full turn round and 2 * 3 = 6 up");
  }
  bool threw = false;
  try { (void)ScrewPolyline({Point3d(1, 0, 0)}, origin, zaxis, kPi, 4); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a one-point profile is a clear error");
}

// ---- Mask modifier (D8-BLENDER-MODIFIER-0048) -----------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshMaskKeepsOnlyFacesWhoseCornersAreAllKept) {
  using namespace dino8::kernel;
  const Mesh sheet = FlatSheet(2, 1.0);  // 3 x 3 vertices, 4 quads; vertex (i, j) = 3 j + i
  std::vector<bool> left(9, false);
  for (int j = 0; j < 3; ++j) {
    left[3 * j] = true;
    left[3 * j + 1] = true;
  }
  const Mesh kept = MaskMeshByVertices(sheet, left);
  d8test::Check(kept.FaceCount() == 2 && kept.VertexCount() == 6 && Near(kept.Area(), 2.0, 1e-6), "columns x = 0, 1 kept: the two quads between them, 6 vertices, area 2");
  const Mesh inverted = MaskMeshByVertices(sheet, left, true);
  d8test::Check(inverted.FaceCount() == 0, "inverted: only the x = 2 column is kept, and no face has all corners there");
  std::vector<bool> all(9, true);
  d8test::Check(MaskMeshByVertices(sheet, all).FaceCount() == 4, "all kept: all four quads");
  std::vector<bool> none(9, false);
  d8test::Check(MaskMeshByVertices(sheet, none).FaceCount() == 0 && MaskMeshByVertices(sheet, none, true).FaceCount() == 4, "none kept: no face; inverted: all faces");
  bool threw = false;
  try { (void)MaskMeshByVertices(sheet, std::vector<bool>(3, true)); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a flag list of the wrong size is a clear error");
}

// ---- Build modifier (D8-BLENDER-MODIFIER-0005) ----------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshBuildShowsTheFirstFractionOfFacesInOrder) {
  using namespace dino8::kernel;
  const Mesh tri = TriBox(0, 0, 0, 10, 10, 10);  // 12 triangles
  d8test::Check(BuildFaces(tri, 10.0, 10.0, 4.0).FaceCount() == 0, "at the start frame nothing is built");
  d8test::Check(BuildFaces(tri, 11.0, 10.0, 4.0).FaceCount() == 3, "a quarter of the way: floor(12 / 4) = 3 faces");
  d8test::Check(BuildFaces(tri, 12.0, 10.0, 4.0).FaceCount() == 6, "half way: 6 faces");
  d8test::Check(BuildFaces(tri, 14.0, 10.0, 4.0).FaceCount() == 12, "at the end of the length everything is built");
  d8test::Check(BuildFaces(tri, 99.0, 10.0, 4.0).FaceCount() == 12 && BuildFaces(tri, -5.0, 10.0, 4.0).FaceCount() == 0, "outside the range the count is clamped");
  d8test::Check(BuildFaces(tri, 12.0, 10.0, 4.0, true).FaceCount() == 6 && BuildFaces(tri, 11.0, 10.0, 4.0, true).FaceCount() == 9, "reversed: the fraction is 1 - f (9 of 12 at a quarter)");
  // The faces built are the first ones in order: a 6-face build of the box is faces 0..5, i.e. the first three quads' triangles.
  const Mesh half = BuildFaces(tri, 12.0, 10.0, 4.0);
  bool same = true;
  for (int i = 0; i < 6; ++i)
    for (int k = 0; k < 3; ++k) {
      const Point3d a = Vertex(tri, tri.raw().m_F[i].vi[k]);
      const Point3d b = Vertex(half, half.raw().m_F[i].vi[k]);
      same = same && a.DistanceTo(b) < 1e-9;
    }
  d8test::Check(same, "the built faces are the first six faces, corner for corner");
  bool threw = false;
  try { (void)BuildFaces(tri, 1.0, 0.0, 0.0); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a zero length is a clear error");
}

// ---- Extrude individual faces / extrude edges (D8-BLENDER-MESH-0043, -VIEW3D-0012, -MESH-0041) ---------------------------------------
D8_EXTRA_TEST(Rw3MeshExtrudeIndividualFacesAndEdgesAddClosedFormPrismsAndRibbons) {
  using namespace dino8::kernel;
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  // Top face by 2: a 10 x 10 x 2 prism is added; its four walls add 4 * 10 * 2 area.
  {
    const Mesh m = ExtrudeIndividualFaces(box, {5}, 2.0);
    const Counts c = CountsOf(m);
    d8test::Check(c.v == 12 && c.f == 10 && c.e == 20 && c.Euler() == 2 && m.IsClosedManifold(), "top face extruded: V 12, F 10, E 20, closed, Euler characteristic 2");
    d8test::Check(Near(m.Volume(), 1200.0, 1e-3) && Near(m.Area(), 680.0, 1e-3), "volume 1000 + 100 * 2 = 1200, area 600 + 4 * 10 * 2 = 680");
    d8test::Check(Near(m.GetBoundingBox().max.z, 12.0, 1e-5), "the face moved up along its normal");
  }
  // Inward by 3: a pocket; volume 1000 - 300, area 600 + 4 * 10 * 3.
  {
    const Mesh m = ExtrudeIndividualFaces(box, {5}, -3.0);
    d8test::Check(m.IsClosedManifold() && Near(m.Volume(), 700.0, 1e-3) && Near(m.Area(), 720.0, 1e-3), "top face pushed in by 3: closed, volume 700, area 720");
  }
  // Two opposite faces: each adds its own prism.
  {
    const Mesh m = ExtrudeIndividualFaces(box, {0, 5}, 2.0);
    d8test::Check(CountsOf(m).v == 16 && CountsOf(m).f == 14 && Near(m.Volume(), 1400.0, 1e-3) && m.IsClosedManifold(), "top and bottom extruded: V 16, F 14, volume 1400");
  }
  // Extrude edges: the two bottom-row boundary edges of a 2 x 2 sheet by (0, 0, 3): two quads of 1 x 3.
  {
    const Mesh sheet = FlatSheet(2, 1.0);
    const Mesh m = ExtrudeMeshEdges(sheet, {{0, 1}, {1, 2}}, Vector3d(0, 0, 3));
    const Counts c = CountsOf(m);
    d8test::Check(c.v == 9 + 3 && c.f == 4 + 2, "two chained edges: the shared end vertex is copied once, V 9 + 3 = 12, F 4 + 2 = 6");
    d8test::Check(Near(m.Area(), 4.0 + 2 * 3.0, 1e-5), "area 4 + 2 * (1 * 3) = 10");
    bool threw = false;
    try { (void)ExtrudeMeshEdges(sheet, {{0, 8}}, Vector3d(0, 0, 1)); } catch (const std::invalid_argument&) { threw = true; }
    d8test::Check(threw, "an edge that is not in the mesh is a clear error");
  }
}

// ---- Simple Deform modifier, Twist and Taper (D8-BLENDER-MODIFIER-0063) ---------------------------------------------------------
D8_EXTRA_TEST(Rw3MeshSimpleDeformTwistAndTaperMapEveryVertexByTheAxialFraction) {
  using namespace dino8::kernel;
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 20);
  const Point3d a0(5, 5, 0), a1(5, 5, 20);
  // Twist by 90 degrees over the axis: a vertex at height z turns by 90 z / 20 degrees about the axis.
  const Mesh twisted = DeformMesh(box, MakeTwist(a0, a1, kPi / 2.0));
  bool twist_ok = true, radius_ok = true;
  for (int i = 0; i < box.VertexCount(); ++i) {
    const Point3d p = Vertex(box, i), q = Vertex(twisted, i);
    const double theta = kPi / 2.0 * p.z / 20.0;
    const double dx = p.x - 5.0, dy = p.y - 5.0;
    const double ex = 5.0 + dx * std::cos(theta) - dy * std::sin(theta), ey = 5.0 + dx * std::sin(theta) + dy * std::cos(theta);
    twist_ok = twist_ok && Near(q.x, ex, 1e-4) && Near(q.y, ey, 1e-4) && Near(q.z, p.z, 1e-5);
    radius_ok = radius_ok && Near(std::hypot(q.x - 5.0, q.y - 5.0), std::hypot(dx, dy), 1e-4);
  }
  d8test::Check(twist_ok, "twist 90 degrees: each vertex turns by 90 z / 20 degrees about the axis, z is unchanged");
  d8test::Check(radius_ok, "twist keeps every vertex's distance from the axis");
  d8test::Check(Near(Vertex(twisted, 4).x, 10.0, 1e-4) && Near(Vertex(twisted, 4).y, 0.0, 1e-4) && Near(Vertex(twisted, 4).z, 20.0, 1e-5),
                "the corner (0, 0, 20) at the far end lands on (10, 0, 20) after a quarter turn (offset (-5, -5) -> (5, -5))");
  d8test::Check(twisted.FaceCount() == box.FaceCount() && twisted.VertexCount() == box.VertexCount(), "topology is unchanged");
  // Taper from 1 to 2: the far end grows to double the offset from the axis.
  const Mesh tapered = DeformMesh(box, MakeTaper(a0, a1, 1.0, 2.0));
  bool taper_ok = true;
  for (int i = 0; i < box.VertexCount(); ++i) {
    const Point3d p = Vertex(box, i), q = Vertex(tapered, i);
    const double s = 1.0 + p.z / 20.0;
    taper_ok = taper_ok && Near(q.x, 5.0 + (p.x - 5.0) * s, 1e-4) && Near(q.y, 5.0 + (p.y - 5.0) * s, 1e-4) && Near(q.z, p.z, 1e-5);
  }
  d8test::Check(taper_ok, "taper 1 -> 2: the offset from the axis is scaled by 1 + z / 20");
  d8test::Check(Near(tapered.GetBoundingBox().max.x, 15.0, 1e-4) && Near(tapered.GetBoundingBox().min.x, -5.0, 1e-4), "the far end spans -5 .. 15 (twice the width)");
}
