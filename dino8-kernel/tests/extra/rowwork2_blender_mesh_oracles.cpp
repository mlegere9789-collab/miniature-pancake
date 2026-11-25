// Register row tests for the mesh-subd-sculpt factory, round 2: kernel outcomes of Blender's mesh operators and modifiers. Each test checks
// counts, volumes or areas against a closed form that does not use the code under test:
//  * Catmull-Clark on a closed quad net (Subdivision Surface modifier): F' = sum of the face sizes, E' = 2E + F', V' = V + E + F; a cube gives
//    F = 6 * 4^L, V = F + 2, E = 2F (Euler characteristic 2 at every level).
//  * Solidify of a flat sheet of area A and perimeter P with thickness t: a closed prism-like shell of volume A t and area 2A + P t, with
//    V = 2 V0, F = 2 F0 + (boundary edges), Euler characteristic 2.
//  * Mirror and Array: copies placed by an exact affine map, vertices on the shared plane merged: V, F and E follow from counting the shared
//    vertices; volumes add; extents are n * pitch.
//  * Weld / Merge by Distance: vertices closer than the distance merge, nothing else; the face area does not change.
//  * Inset: a square face of side a inset by t and lifted by d gives a frustum of volume d (a^2 + a b + b^2) / 3 with b = a - 2t and lateral
//    faces of area (a + b) / 2 sqrt(t^2 + d^2) each.
//  * Convex hull: the hull of a point set containing the 8 corners of a box and points inside it is the box.
//  * Boolean of a box and a prism: the n-gon of radius r has area n r^2 sin(2 pi / n) / 2, so the difference has an exact volume.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanOp;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::Vector3d;

constexpr double kPi = 3.14159265358979323846;

bool Near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

int FaceSize(const ON_MeshFace& f) { return f.IsQuad() ? 4 : 3; }

// V - E + F over the vertices that faces use and the unique undirected edges, plus the counts themselves.
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
Mesh QuadBox(double x0, double y0, double z0, double x1, double y1, double z1) {
  Mesh m;
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) m.raw().m_V.Append(ON_3fPoint(float(x ? x1 : x0), float(y ? y1 : y0), float(z ? z1 : z0)));
  AddQuad(m, 0, 2, 3, 1); AddQuad(m, 0, 1, 5, 4); AddQuad(m, 2, 6, 7, 3); AddQuad(m, 0, 4, 6, 2); AddQuad(m, 1, 3, 7, 5); AddQuad(m, 4, 5, 7, 6);
  return m;
}

// The same box with every quad split along its first diagonal: 12 outward triangles.
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

// Flat n x n grid of unit-pitch-`pitch` quads in the plane z = 0, wound counter-clockwise seen from +z (normal +z).
Mesh FlatSheet(int n, double pitch) {
  Mesh m;
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i) m.raw().m_V.Append(ON_3fPoint(float(i * pitch), float(j * pitch), 0.0f));
  const auto id = [n](int i, int j) { return j * (n + 1) + i; };
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) AddQuad(m, id(i, j), id(i + 1, j), id(i + 1, j + 1), id(i, j + 1));
  return m;
}

}  // namespace

// ---- Blender Subdivision Surface modifier (D8-BLENDER-MODIFIER-0068, -0037) --------------------------------------------------------
// Levels of Catmull-Clark subdivision: the count recurrences are closed forms of the scheme (quads split into 4, triangles into 3 quads).
D8_EXTRA_TEST(Rw2SubDSubdivisionLevelsFollowCatmullClarkCountRecurrences) {
  for (int levels = 1; levels <= 3; ++levels) {
    SubD s = SubD::FromControlMesh(QuadBox(0, 0, 0, 10, 10, 10));
    s.Subdivide(levels);
    const int faces = 6 * (1 << (2 * levels));  // 6 * 4^L
    char what[96];
    std::snprintf(what, sizeof what, "cube, %d level(s): F = 6 * 4^L = %d", levels, faces);
    d8test::Check(s.FaceCount() == faces, what);
    std::snprintf(what, sizeof what, "cube, %d level(s): V = F + 2 = %d and E = 2 F = %d (Euler characteristic 2)", levels, faces + 2, 2 * faces);
    d8test::Check(s.VertexCount() == faces + 2 && s.EdgeCount() == 2 * faces, what);
    d8test::Check(s.IsValid() && s.Check().IsManifoldSingleBody(), "still one valid manifold body");
  }
  // Level 0 changes nothing.
  SubD zero = SubD::FromControlMesh(QuadBox(0, 0, 0, 10, 10, 10));
  zero.Subdivide(0);
  d8test::Check(zero.FaceCount() == 6 && zero.VertexCount() == 8 && zero.EdgeCount() == 12, "0 levels keep the 6/8/12 cage");
  // A tetrahedron (4 triangles): level 1 gives 3 quads per triangle, V' = 4 + 6 + 4 = 14, E' = 12 + 12 = 24; level 2 then follows the quad rule.
  Mesh tet;
  tet.raw().m_V.Append(ON_3fPoint(0, 0, 0)); tet.raw().m_V.Append(ON_3fPoint(10, 0, 0)); tet.raw().m_V.Append(ON_3fPoint(0, 10, 0)); tet.raw().m_V.Append(ON_3fPoint(0, 0, 10));
  AddTri(tet, 0, 2, 1); AddTri(tet, 0, 1, 3); AddTri(tet, 0, 3, 2); AddTri(tet, 1, 2, 3);
  SubD t = SubD::FromControlMesh(tet);
  d8test::Check(t.VertexCount() == 4 && t.EdgeCount() == 6 && t.FaceCount() == 4, "tetrahedron cage 4/6/4");
  t.Subdivide(1);
  d8test::Check(t.VertexCount() == 14 && t.EdgeCount() == 24 && t.FaceCount() == 12, "tetrahedron after 1 level: V=14 E=24 F=12 (each triangle -> 3 quads)");
  t.Subdivide(1);
  d8test::Check(t.FaceCount() == 48 && t.VertexCount() == 50 && t.EdgeCount() == 96 && t.IsValid(), "after 2 levels: F=48 V=50 E=96 (Euler 2), valid");
}

// ---- Blender Solidify modifier and mesh.solidify (D8-BLENDER-MODIFIER-0067, D8-BLENDER-MESH-0149) -----------------------------------
D8_EXTRA_TEST(Rw2MeshThickenSheetMakesClosedShellWithExactVolumeAndArea) {
  const int n = 4;
  const double pitch = 2.5, t = 0.5;  // a 10 x 10 sheet
  const Mesh sheet = FlatSheet(n, pitch);
  d8test::Check(sheet.FaceCount() == 16 && sheet.VertexCount() == 25 && !sheet.IsClosedManifold(), "an open 4 x 4 sheet: 16 quads, 25 vertices");
  d8test::Check(Near(sheet.Area(), 100.0, 1e-6), "sheet area 100");
  for (const double signed_thickness : {t, -t}) {
    const Mesh shell = sheet.Thicken(signed_thickness);
    const Counts c = CountsOf(shell);
    d8test::Check(c.v == 50, "two layers of 25 vertices");
    d8test::Check(c.f == 16 + 16 + 16, "faces: 16 + 16 + 16 boundary wall quads");
    d8test::Check(c.Euler() == 2, "the shell is a topological sphere (V - E + F = 2)");
    d8test::Check(shell.IsClosedManifold(), "closed consistently oriented manifold");
    d8test::Check(Near(std::abs(shell.Volume()), 100.0 * t, 1e-4), "volume = area * thickness = 50");
    d8test::Check(shell.Volume() > 0.0, "oriented outward (positive volume)");
    d8test::Check(Near(shell.Area(), 2 * 100.0 + 40.0 * t, 1e-4), "area = 2 A + perimeter * thickness = 220");
    const ON_BoundingBox bb = shell.raw().BoundingBox();
    d8test::Check(Near(bb.Max().z - bb.Min().z, t, 1e-6) && Near(bb.Max().x - bb.Min().x, 10.0, 1e-6), "extent: the thickness along z, the sheet in x and y");
  }
  bool zero_thrown = false;
  try { (void)sheet.Thicken(0.0); } catch (const std::invalid_argument&) { zero_thrown = true; }
  d8test::Check(zero_thrown, "zero thickness is refused");
}

// ---- Blender Mirror modifier (D8-BLENDER-MODIFIER-0053) ----------------------------------------------------------------------------
// A box [0,5] x [0,10] x [0,10] without its x = 0 face, mirrored across x = 0 with the plane vertices merged (the modifier's Merge option).
// The two halves share the 4 vertices of the open rim: V = 8 + 8 - 4 = 12, F = 5 + 5 = 10, E = 12 + 12 - 4 = 20, volume 2 * 500, area =
// the two end caps 2 * 100 plus four sides 4 * 100 = 600.
D8_EXTRA_TEST(Rw2MeshMirrorWithMergeClosesOpenHalfIntoTheFullBox) {
  Mesh half = QuadBox(0, 0, 0, 5, 10, 10);
  // Drop the x = 0 face (the one whose four corners all have x = 0): rebuild the face list without it.
  {
    ON_SimpleArray<ON_MeshFace>& faces = half.raw().m_F;
    for (int i = faces.Count() - 1; i >= 0; --i) {
      const ON_MeshFace& f = faces[i];
      if (half.raw().m_V[f.vi[0]].x == 0.0f && half.raw().m_V[f.vi[1]].x == 0.0f && half.raw().m_V[f.vi[2]].x == 0.0f && half.raw().m_V[f.vi[3]].x == 0.0f) faces.Remove(i);
    }
  }
  d8test::Check(half.FaceCount() == 5 && !half.IsClosedManifold(), "the half is open: 5 faces");
  const Mesh mirrored = half.Transform(ON_Xform::MirrorTransformation(ON_PlaneEquation(1.0, 0.0, 0.0, 0.0)));
  d8test::Check(mirrored.FaceCount() == 5, "the mirror image has 5 faces");
  // Transform() with a reflection (negative determinant) reverses the winding so the copy still faces outward: its volume stays positive.
  d8test::Check(Near(mirrored.Volume(), 500.0, 1e-6), "the mirror image has volume +500: winding corrected by the reflection");
  Mesh merged = Mesh::MergeAndWeld({half, mirrored}, 1e-6);
  const Counts c = CountsOf(merged);
  d8test::Check(c.v == 12 && c.f == 10 && c.e == 20, "merge: V = 8 + 8 - 4 = 12, F = 10, E = 12 + 12 - 4 = 20");
  d8test::Check(c.Euler() == 2, "topological sphere");
  d8test::Check(merged.IsClosedManifold(), "closed consistently oriented manifold");
  d8test::Check(Near(merged.Volume(), 1000.0, 1e-3), "volume 2 * 500 = 1000");
  d8test::Check(Near(merged.Area(), 600.0, 1e-3), "area of the 10 x 10 x 10 cube");
  const ON_BoundingBox bb = merged.raw().BoundingBox();
  d8test::Check(Near(bb.Min().x, -5.0, 1e-6) && Near(bb.Max().x, 5.0, 1e-6), "extent x from -5 to 5 (mirror across the plane through the origin)");
  // Without merging (merge limit 0), the seam vertices stay doubled: 16 vertices.
  Mesh unmerged;
  {
    ON_Mesh& raw = unmerged.raw();
    for (int i = 0; i < half.raw().m_V.Count(); ++i) raw.m_V.Append(half.raw().m_V[i]);
    for (int i = 0; i < mirrored.raw().m_V.Count(); ++i) raw.m_V.Append(mirrored.raw().m_V[i]);
    for (int i = 0; i < half.raw().m_F.Count(); ++i) raw.m_F.Append(half.raw().m_F[i]);
    const int off = half.raw().m_V.Count();
    for (int i = 0; i < mirrored.raw().m_F.Count(); ++i) {
      ON_MeshFace f = mirrored.raw().m_F[i];
      for (int k = 0; k < 4; ++k) f.vi[k] += off;
      raw.m_F.Append(f);
    }
  }
  d8test::Check(CountsOf(unmerged).v == 16 && !unmerged.IsClosedManifold(), "no merge: the seam keeps 16 vertices and stays open");
}

// ---- Blender Array modifier (D8-BLENDER-MODIFIER-0002) -------------------------------------------------------------------------------
// N copies of the box [0,2]^3 offset by (3,0,0) (constant offset): each copy keeps its own faces; volumes add; the extent is (N - 1) * 3 + 2.
// With relative offset 1 (touching boxes) and merging, the touching corner vertices merge: V = 4 (N + 1), E = 8 N + 4, F = 6 N.
D8_EXTRA_TEST(Rw2MeshArrayCopiesFollowCountOffsetAndMerge) {
  const int N = 5;
  std::vector<Mesh> separated, touching;
  for (int i = 0; i < N; ++i) {
    separated.push_back(QuadBox(0, 0, 0, 2, 2, 2).Transform(ON_Xform::TranslationTransformation(ON_3dVector(3.0 * i, 0, 0))));
    touching.push_back(QuadBox(0, 0, 0, 2, 2, 2).Transform(ON_Xform::TranslationTransformation(ON_3dVector(2.0 * i, 0, 0))));
  }
  const Mesh apart = Mesh::MergeAndWeld(separated, 1e-6);
  const Counts a = CountsOf(apart);
  d8test::Check(a.v == 8 * N && a.f == 6 * N && a.e == 12 * N, "gaps of 1: nothing merges: V = 40, F = 30, E = 60");
  d8test::Check(Near(apart.Volume(), 8.0 * N, 1e-6), "volume 5 * 8 = 40");
  const ON_BoundingBox bb = apart.raw().BoundingBox();
  d8test::Check(Near(bb.Max().x - bb.Min().x, 3.0 * (N - 1) + 2.0, 1e-6), "extent (N - 1) * 3 + 2 = 14");
  d8test::Check(Near(apart.Area(), 24.0 * N, 1e-6), "area 5 * 24 = 120");
  const Mesh joined = Mesh::MergeAndWeld(touching, 1e-6);
  const Counts j = CountsOf(joined);
  d8test::Check(j.v == 4 * (N + 1), "relative offset 1 with merge: V = 4 (N + 1) = 24");
  d8test::Check(j.e == 8 * N + 4, "E = 12 N - 4 (N - 1) = 8 N + 4 = 44 (every interface shares 4 edges)");
  const ON_BoundingBox bj = joined.raw().BoundingBox();
  d8test::Check(Near(bj.Max().x - bj.Min().x, 2.0 * N, 1e-6), "touching copies span N * 2 = 10");
}

// ---- Blender Weld modifier and mesh.remove_doubles (D8-BLENDER-MODIFIER-0082, D8-BLENDER-MESH-0110) -----------------------------------
// Two unit quads side by side with a 0.0004 seam gap: the two seam vertex pairs are 0.0004 apart. Distance 0.001 merges them (6 vertices, the
// shared edge becomes one), distance 0.0001 merges nothing (8 vertices); the face area never changes.
D8_EXTRA_TEST(Rw2MeshWeldMergesExactlyTheVerticesWithinTheDistance) {
  auto build = []() {
    Mesh m;
    ON_Mesh& r = m.raw();
    // left quad x in [0,1], right quad x in [1.0004, 2.0004]
    const float xs[2][2] = {{0.0f, 1.0f}, {1.0004f, 2.0004f}};
    for (int q = 0; q < 2; ++q) {
      r.m_V.Append(ON_3fPoint(xs[q][0], 0, 0));
      r.m_V.Append(ON_3fPoint(xs[q][1], 0, 0));
      r.m_V.Append(ON_3fPoint(xs[q][1], 1, 0));
      r.m_V.Append(ON_3fPoint(xs[q][0], 1, 0));
      AddQuad(m, 4 * q, 4 * q + 1, 4 * q + 2, 4 * q + 3);
    }
    return m;
  };
  Mesh tight = build();
  d8test::Check(tight.MergeDuplicateVertices(1e-4) == 0, "distance 0.0001 < gap 0.0004: no vertex merges");
  d8test::Check(CountsOf(tight).v == 8 && tight.FaceCount() == 2, "8 vertices, 2 faces unchanged");
  Mesh loose = build();
  const double area_before = loose.Area();
  d8test::Check(loose.MergeDuplicateVertices(1e-3) == 2, "distance 0.001 > gap: the two seam pairs merge (2 vertices removed)");
  const Counts c = CountsOf(loose);
  d8test::Check(c.v == 6 && c.f == 2 && c.e == 7, "V = 6, F = 2, E = 8 - 1 = 7 (the doubled seam edge became one)");
  d8test::Check(Near(loose.Area(), area_before, 1e-3), "area changes only by the closed gap (at most 0.0004)");
  // Merging is not transitive chaining across larger gaps: vertices 0.0004 apart in a row of three (spacing 0.0004) are all within 1e-3 of
  // the first, so one cluster of 3 collapses to a single vertex.
  Mesh strip;
  for (int i = 0; i < 3; ++i) strip.raw().m_V.Append(ON_3fPoint(float(0.0004 * i), 0, 0));
  strip.raw().m_V.Append(ON_3fPoint(5, 0, 0));
  strip.raw().m_V.Append(ON_3fPoint(5, 5, 0));
  AddTri(strip, 0, 3, 4);
  AddTri(strip, 1, 4, 3);
  d8test::Check(strip.MergeDuplicateVertices(1e-3) == 2, "a cluster of 3 vertices within the distance collapses to one (2 removed)");
}

// ---- Blender mesh.flip_normals (D8-BLENDER-MESH-0061) -------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw2MeshFlipNormalsReversesEveryFaceAndNegatesTheVolume) {
  const Mesh box = QuadBox(0, 0, 0, 10, 10, 10);
  d8test::Check(Near(box.Volume(), 1000.0, 1e-6) && box.IsClosedManifold(), "outward box: volume +1000, closed");
  const Mesh flipped = box.FlipNormals();
  d8test::Check(Near(flipped.Volume(), -1000.0, 1e-6), "flipped box: signed volume -1000");
  d8test::Check(flipped.IsClosedManifold(), "still a closed consistently oriented manifold");
  d8test::Check(flipped.VertexCount() == 8 && flipped.FaceCount() == 6 && Near(flipped.Area(), 600.0, 1e-9), "same vertices, faces and area");
  const std::vector<Vector3d> n0 = box.ComputeFaceNormals(), n1 = flipped.ComputeFaceNormals();
  bool all_opposite = n0.size() == 6 && n1.size() == 6;
  for (size_t i = 0; all_opposite && i < n0.size(); ++i) all_opposite = Near(n0[i] * n1[i], -1.0, 1e-9);
  d8test::Check(all_opposite, "every face normal is exactly reversed");
  const Mesh twice = flipped.FlipNormals();
  bool identical = twice.FaceCount() == 6;
  for (int i = 0; identical && i < 6; ++i)
    for (int k = 0; k < 4; ++k) identical = identical && twice.raw().m_F[i].vi[k] == box.raw().m_F[i].vi[k];
  d8test::Check(identical, "flipping twice restores the original faces exactly");
}

// ---- Blender mesh.tris_convert_to_quads (D8-BLENDER-MESH-0159) -----------------------------------------------------------------------
// The 12 triangles of a box pair up across the diagonals of the 6 coplanar sides (dihedral 0) but not across the 90 degree box edges
// (limit 20 degrees): 6 quads. A tetrahedron has only 109.47 degree dihedral angles: nothing merges.
D8_EXTRA_TEST(Rw2MeshTrisToQuadsMergesOnlyCoplanarTrianglePairsWithinTheAngleLimit) {
  Mesh box = TriBox(0, 0, 0, 10, 10, 10);
  d8test::Check(box.FaceCount() == 12, "12 triangles");
  const int made = box.TrisToQuads(20.0);
  d8test::Check(made == 6 && box.FaceCount() == 6, "6 quads made, 6 faces left");
  bool all_quads = true;
  for (int i = 0; i < box.FaceCount(); ++i) all_quads = all_quads && box.raw().m_F[i].IsQuad();
  d8test::Check(all_quads, "every face is a quad");
  d8test::Check(Near(box.Volume(), 1000.0, 1e-6) && Near(box.Area(), 600.0, 1e-6) && box.IsClosedManifold(), "volume, area and closure unchanged");
  Mesh tet;
  tet.raw().m_V.Append(ON_3fPoint(0, 0, 0)); tet.raw().m_V.Append(ON_3fPoint(10, 0, 0)); tet.raw().m_V.Append(ON_3fPoint(0, 10, 0)); tet.raw().m_V.Append(ON_3fPoint(0, 0, 10));
  AddTri(tet, 0, 2, 1); AddTri(tet, 0, 1, 3); AddTri(tet, 0, 3, 2); AddTri(tet, 1, 2, 3);
  d8test::Check(tet.TrisToQuads(20.0) == 0 && tet.FaceCount() == 4, "a tetrahedron (dihedral angles about 90 to 109 degrees) merges nothing");
  Mesh flat = FlatSheet(1, 4.0);  // one flat quad
  Mesh split;
  for (int i = 0; i < flat.raw().m_V.Count(); ++i) split.raw().m_V.Append(flat.raw().m_V[i]);
  AddTri(split, 0, 1, 3);  // the sheet's corner order is 0:(0,0) 1:(4,0) 2:(0,4) 3:(4,4): the ring is 0, 1, 3, 2
  AddTri(split, 0, 3, 2);
  d8test::Check(split.TrisToQuads(0.0) == 1 && split.FaceCount() == 1 && Near(split.Area(), 16.0, 1e-9), "two coplanar triangles merge even at limit 0: one quad of area 16");
}

// ---- Blender mesh.inset (D8-BLENDER-MESH-0064) ---------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw2MeshInsetFaceMatchesClosedFormCountsAreaAndFrustumVolume) {
  const double a = 10.0, t = 2.0, b = a - 2.0 * t;
  const Mesh cube = QuadBox(0, 0, 0, a, a, a);
  const int top = 5;  // the face with corners (4,5,7,6): z = a, outward normal +z
  // Flat inset: 4 frame quads + the inner quad replace the face: F = 6 - 1 + 5 = 10, V = 8 + 4 = 12, E = 12 + 8 = 20; area and volume unchanged.
  const Mesh flat = cube.InsetFace(top, t);
  const Counts cf = CountsOf(flat);
  d8test::Check(cf.f == 10 && cf.v == 12 && cf.e == 20 && cf.Euler() == 2, "flat inset: F = 10, V = 12, E = 20, Euler 2");
  d8test::Check(Near(flat.Area(), 6 * a * a, 1e-3) && Near(flat.Volume(), a * a * a, 1e-3), "flat inset keeps area 600 and volume 1000");
  d8test::Check(flat.IsClosedManifold(), "closed manifold");
  // Inner face is the square of side b = 6 centred on the face: its area is 36.
  const ON_BoundingBox bf = flat.raw().BoundingBox();
  d8test::Check(Near(bf.Max().z, a, 1e-6), "flat inset does not leave the plane z = 10");
  // Inset with depth d = 3: the frame and the lifted inner face are the lateral faces and the top of a square frustum.
  const double d = 3.0;
  const Mesh raised = cube.InsetFace(top, t, d);
  const Counts cr = CountsOf(raised);
  d8test::Check(cr.f == 10 && cr.v == 12 && cr.e == 20 && cr.Euler() == 2 && raised.IsClosedManifold(), "raised inset: same counts, closed");
  const double frustum = d * (a * a + a * b + b * b) / 3.0;
  d8test::Check(Near(raised.Volume(), a * a * a + frustum, 1e-3), "volume = 1000 + d (a^2 + a b + b^2) / 3 = 1196");
  const double lateral = 4.0 * (a + b) / 2.0 * std::sqrt(t * t + d * d);
  d8test::Check(Near(raised.Area(), 5 * a * a + b * b + lateral, 1e-3), "area = 500 + b^2 + 4 (a + b) / 2 sqrt(t^2 + d^2)");
  d8test::Check(Near(raised.raw().BoundingBox().Max().z, a + d, 1e-6), "the inner ring is lifted by exactly d along the outward normal");
  bool threw = false;
  try { (void)cube.InsetFace(top, 5.0); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "an inset equal to half the side (the face collapses) is refused");
}

// ---- Blender mesh.convex_hull (D8-BLENDER-MESH-0011) ---------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw2MeshConvexHullOfBoxCornersWithInteriorPointsIsTheBox) {
  std::vector<Point3d> pts;
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) pts.emplace_back(10.0 * x, 6.0 * y, 4.0 * z);
  // interior and face-interior points do not change the hull
  pts.emplace_back(5, 3, 2);
  pts.emplace_back(1, 1, 1);
  pts.emplace_back(9, 5, 3);
  pts.emplace_back(5, 3, 0);  // inside the bottom face
  const Mesh hull = dino8::kernel::ConvexHull(pts);
  d8test::Check(hull.IsClosedManifold(), "the hull is a closed manifold");
  d8test::Check(Near(hull.Volume(), 10.0 * 6.0 * 4.0, 1e-6), "volume 240 = the box");
  d8test::Check(Near(hull.Area(), 2.0 * (60 + 40 + 24), 1e-6), "area 248 = the box");
  const Counts c = CountsOf(hull);
  d8test::Check(c.Euler() == 2 && c.v >= 8, "topological sphere");
  const ON_BoundingBox bb = hull.raw().BoundingBox();
  d8test::Check(Near(bb.Max().x - bb.Min().x, 10.0, 1e-6) && Near(bb.Max().y - bb.Min().y, 6.0, 1e-6) && Near(bb.Max().z - bb.Min().z, 4.0, 1e-6), "extents 10 x 6 x 4");
  // The octahedron with vertices at +-a on the axes: V = 6, F = 8, E = 12, volume 4 a^3 / 3.
  std::vector<Point3d> oct = {Point3d(5, 0, 0), Point3d(-5, 0, 0), Point3d(0, 5, 0), Point3d(0, -5, 0), Point3d(0, 0, 5), Point3d(0, 0, -5), Point3d(0, 0, 0), Point3d(1, 1, 1)};
  const Mesh o = dino8::kernel::ConvexHull(oct);
  const Counts co = CountsOf(o);
  d8test::Check(co.v == 6 && co.f == 8 && co.e == 12, "octahedron hull: V = 6, F = 8, E = 12");
  d8test::Check(Near(o.Volume(), 4.0 * 125.0 / 3.0, 1e-6), "octahedron volume 4 a^3 / 3");
}

// ---- Blender Boolean modifier and mesh.intersect_boolean (D8-BLENDER-MODIFIER-0004, D8-BLENDER-MESH-0066) ---------------------------------
// A box with a prism hole: the difference of the 10 x 10 x 10 box and a 32-gon prism through it has the exact volume 1000 - area(32-gon) * 10;
// the intersection is the prism itself; union + intersection = the sum of the volumes.
D8_EXTRA_TEST(Rw2MeshBooleanBoxAndPrismMatchPolygonClosedForms) {
  const int n = 32;
  const double r = 3.0, h = 10.0;
  const Mesh box = QuadBox(-5, -5, 0, 5, 5, h);
  // 32-gon prism taller than the box: z from -1 to 11, so it passes completely through.
  Mesh prism = Mesh::Cylinder(Point3d(0, 0, -1), Vector3d(0, 0, 1), r, h + 2.0, n, 1);
  d8test::Check(prism.IsClosedManifold(), "the prism is a closed solid");
  const double polygon = 0.5 * n * r * r * std::sin(2.0 * kPi / n);
  const double prism_volume = polygon * (h + 2.0);
  d8test::Check(Near(prism.Volume(), prism_volume, 1e-3 * prism_volume), "the prism has the 32-gon closed-form volume");
  const Mesh diff = dino8::kernel::BooleanCombine(box, prism, BooleanOp::Difference);
  const Mesh inter = dino8::kernel::BooleanCombine(box, prism, BooleanOp::Intersection);
  const Mesh uni = dino8::kernel::BooleanCombine(box, prism, BooleanOp::Union);
  const double hole = polygon * h;
  d8test::Check(diff.IsClosedManifold() && Near(diff.Volume(), 1000.0 - hole, 1e-3 * 1000.0), "difference: 1000 - polygon * 10");
  d8test::Check(inter.IsClosedManifold() && Near(inter.Volume(), hole, 1e-3 * hole), "intersection: the prism inside the box");
  d8test::Check(Near(uni.Volume() + inter.Volume(), box.Volume() + prism.Volume(), 2e-3 * 1000.0), "inclusion-exclusion: |A u B| + |A n B| = |A| + |B|");
  // The difference is a solid with a through-hole: genus 1, Euler characteristic 0 for the connected surface.
  d8test::Check(CountsOf(diff).Euler() == 0, "a box with a through-hole has Euler characteristic 0 (genus 1)");
  d8test::Check(diff.SplitDisjointPieces().size() == 1, "one connected body");
}

// ---- Blender mesh.extrude_region and mesh.extrude_region_move (D8-BLENDER-MESH-0045, D8-BLENDER-MESH-0046) --------------------------------
// Extruding a flat 2 x 2 region of quads (area 16, 9 vertices, 4 quads, normal +z) along an offset o pointing away from the side the region
// faces: the volume is area * |o . n| whether the offset is normal to the region or sheared, V = 2 * 9, F = 4 + 4 + 8 boundary edges * 2
// wall triangles, Euler characteristic 2; the shear changes the wall areas only. A cone from the same region to an apex: V = 9 + 1,
// volume area * h / 3.
D8_EXTRA_TEST(Rw2MeshExtrudeRegionAndMoveHasClosedFormCountsVolumeAndArea) {
  const Mesh region = FlatSheet(2, 2.0);
  d8test::Check(region.FaceCount() == 4 && region.VertexCount() == 9 && Near(region.Area(), 16.0, 1e-9), "setup: 4 quads, 9 vertices, area 16");
  for (const Vector3d offset : {Vector3d(0, 0, -3), Vector3d(1, 0, -3)}) {
    const Mesh solid = Mesh::ExtrudeCappedSolid(region, offset);
    const Counts c = CountsOf(solid);
    char what[160];
    std::snprintf(what, sizeof what, "offset (%g, %g, %g): V = 18, F = 4 + 4 + 16, Euler 2, closed", offset.x, offset.y, offset.z);
    d8test::Check(c.v == 18 && c.f == 4 + 4 + 16 && c.Euler() == 2 && solid.IsClosedManifold(), what);
    std::snprintf(what, sizeof what, "offset (%g, %g, %g): volume = area * height = 48 (positive, outward)", offset.x, offset.y, offset.z);
    d8test::Check(Near(solid.Volume(), 48.0, 1e-6), what);
  }
  const Mesh straight = Mesh::ExtrudeCappedSolid(region, Vector3d(0, 0, -3));
  d8test::Check(Near(straight.Area(), 2 * 16.0 + 4 * 4.0 * 3.0, 1e-6), "straight extrusion: area = 2 * 16 + perimeter 16 * height 3 = 80");
  const Mesh sheared = Mesh::ExtrudeCappedSolid(region, Vector3d(1, 0, -3));
  d8test::Check(Near(sheared.Area(), 2 * 16.0 + 2 * std::sqrt(160.0) + 2 * 12.0, 1e-4), "sheared extrusion: walls are parallelograms: area = 32 + 2 sqrt(160) + 24");
  const ON_BoundingBox bb = sheared.raw().BoundingBox();
  d8test::Check(Near(bb.Max().x - bb.Min().x, 5.0, 1e-6) && Near(std::abs(bb.Max().z - bb.Min().z), 3.0, 1e-6), "extent: 4 + 1 in x, the height 3 in z");
  const Mesh cone = Mesh::ConeToApex(region, Point3d(2, 2, -3));
  const Counts cc = CountsOf(cone);
  d8test::Check(cc.v == 10 && cc.f == 4 + 8 && cc.Euler() == 2 && cone.IsClosedManifold(), "cone of the region: V = 10, F = 4 + 8, Euler 2, closed");
  d8test::Check(Near(cone.Volume(), 16.0, 1e-6) && Near(cone.Area(), 16.0 + 8.0 * std::sqrt(13.0), 1e-6), "cone volume = area * h / 3 = 16, area = 16 + 8 sqrt(13)");
}
