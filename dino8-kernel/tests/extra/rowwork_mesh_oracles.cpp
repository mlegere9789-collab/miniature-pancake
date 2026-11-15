// Register row tests for the mesh-subd-sculpt factory (mesh part). Every oracle is a closed form independent of the code under test:
//  * boxes: volume = product of the sides, area = 2 (ab + bc + ca), Euler characteristic V - E + F = 2 (computed here from the faces),
//  * unions / intersections / differences of two overlapping boxes are boxes (volumes 1500 / 500 / 500, areas 800 / 400 / 400),
//  * the octahedron with vertices +-a on the axes has volume 4 a^3 / 3, and by symmetry every vertex normal is an axis,
//  * a torus has V - E + F = 0, volume 2 pi^2 R r^2 and area 4 pi^2 R r (faceting error bounded by the polygon factors sin(x)/x),
//  * disjoint union: pieces have the volumes of the parts.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanOp;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

bool Near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

// Box [x0,x1] x [y0,y1] x [z0,z1] as 12 outward triangles on 8 shared vertices (vertex index = x + 2 y + 4 z).
void AddBoxTriangles(Mesh& m, double x0, double y0, double z0, double x1, double y1, double z1, bool skip_top = false, bool skip_bottom = false) {
  ON_Mesh& raw = m.raw();
  const int base = raw.m_V.Count();
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(float(x ? x1 : x0), float(y ? y1 : y0), float(z ? z1 : z0)));
  const auto tri = [&raw, base](int a, int b, int c) {
    ON_MeshFace f;
    f.vi[0] = base + a; f.vi[1] = base + b; f.vi[2] = base + c; f.vi[3] = base + c;
    raw.m_F.Append(f);
  };
  // bottom (z0, normal -z)
  if (!skip_bottom) { tri(0, 2, 3); tri(0, 3, 1); }
  // top (z1, normal +z)
  if (!skip_top) { tri(4, 5, 7); tri(4, 7, 6); }
  // y0 face (normal -y)
  tri(0, 1, 5); tri(0, 5, 4);
  // y1 face (normal +y)
  tri(2, 6, 7); tri(2, 7, 3);
  // x0 face (normal -x)
  tri(0, 4, 6); tri(0, 6, 2);
  // x1 face (normal +x)
  tri(1, 3, 7); tri(1, 7, 5);
}

Mesh Box(double x0, double y0, double z0, double x1, double y1, double z1) {
  Mesh m;
  AddBoxTriangles(m, x0, y0, z0, x1, y1, z1);
  return m;
}

// Euler characteristic of a welded mesh from its own faces (triangles and quads): V(used) - E(unique) + F.
int Euler(const Mesh& m) {
  const ON_Mesh& r = m.raw();
  std::set<std::pair<int, int>> edges;
  std::set<int> used;
  int faces = 0;
  for (int i = 0; i < r.m_F.Count(); ++i) {
    const ON_MeshFace& f = r.m_F[i];
    const int n = f.IsQuad() ? 4 : 3;
    ++faces;
    for (int k = 0; k < n; ++k) {
      const int a = f.vi[k], b = f.vi[(k + 1) % n];
      used.insert(a);
      edges.insert({std::min(a, b), std::max(a, b)});
    }
  }
  return int(used.size()) - int(edges.size()) + faces;
}

}  // namespace

// ---- rhino.mesh.culldegeneratemeshfaces (D8-RHINO-MESH-0011) ---------------------------------------------------------------------
D8_EXTRA_TEST(RwmMeshCullDegenerateFacesRemovesOnlyZeroAreaFaces) {
  Mesh m = Box(0, 0, 0, 10, 10, 10);
  ON_Mesh& raw = m.raw();
  const int c0 = raw.m_V.Count();
  raw.m_V.Append(ON_3fPoint(2, 0, 0));   // three collinear points: zero area
  raw.m_V.Append(ON_3fPoint(4, 0, 0));
  raw.m_V.Append(ON_3fPoint(8, 0, 0));
  ON_MeshFace collinear;
  collinear.vi[0] = c0; collinear.vi[1] = c0 + 1; collinear.vi[2] = c0 + 2; collinear.vi[3] = c0 + 2;
  raw.m_F.Append(collinear);
  ON_MeshFace repeated;  // a vertex used twice: zero area
  repeated.vi[0] = 0; repeated.vi[1] = 1; repeated.vi[2] = 1; repeated.vi[3] = 1;
  raw.m_F.Append(repeated);
  d8test::Check(m.FaceCount() == 14, "12 box triangles plus 2 degenerate faces");
  d8test::Check(m.Check().degenerate_faces == 2, "the checker counts the 2 zero-area faces");
  d8test::Check(Near(m.Area(), 600.0, 1e-6), "zero-area faces add no area");
  const int removed = m.RemoveDegenerateFaces();
  d8test::Check(removed == 2, "exactly the 2 zero-area faces are removed");
  d8test::Check(m.FaceCount() == 12, "12 faces remain");
  d8test::Check(m.Check().degenerate_faces == 0, "no degenerate face left");
  d8test::Check(Near(m.Area(), 600.0, 1e-6) && Near(m.Volume(), 1000.0, 1e-6), "area 600 and volume 1000 unchanged");
  d8test::Check(m.RemoveDegenerateFaces() == 0, "a second pass removes nothing");
}

// ---- rhino.mesh.splitdisjointmesh (D8-RHINO-MESH-0080) ---------------------------------------------------------------------------
D8_EXTRA_TEST(RwmMeshSplitDisjointMeshGivesOneClosedMeshPerBody) {
  Mesh m;
  AddBoxTriangles(m, 0, 0, 0, 10, 10, 10);
  AddBoxTriangles(m, 20, 0, 0, 24, 4, 4);
  d8test::Check(m.Check().body_count == 2, "two disjoint boxes: body_count 2");
  const std::vector<Mesh> pieces = m.SplitDisjointPieces();
  d8test::Check(pieces.size() == 2, "two pieces");
  if (pieces.size() == 2) {
    const bool first_big = pieces[0].Volume() > pieces[1].Volume();
    const Mesh& big = first_big ? pieces[0] : pieces[1];
    const Mesh& small = first_big ? pieces[1] : pieces[0];
    d8test::Check(Near(big.Volume(), 1000.0, 1e-6) && Near(small.Volume(), 64.0, 1e-6), "volumes 1000 and 4^3 = 64");
    d8test::Check(Near(big.Area(), 600.0, 1e-6) && Near(small.Area(), 96.0, 1e-6), "areas 600 and 96");
    d8test::Check(big.FaceCount() == 12 && small.FaceCount() == 12 && big.VertexCount() == 8 && small.VertexCount() == 8,
                  "each piece: 8 vertices and 12 faces");
    d8test::Check(big.IsClosedManifold() && small.IsClosedManifold(), "each piece is a closed manifold");
    d8test::Check(Euler(big) == 2 && Euler(small) == 2, "each piece has Euler characteristic 2");
  }
  const std::vector<Mesh> one = Box(0, 0, 0, 1, 1, 1).SplitDisjointPieces();
  d8test::Check(one.size() == 1, "a connected mesh stays one piece");
}

// ---- rhino.mesh.unifymeshnormals (D8-RHINO-MESH-0088) ----------------------------------------------------------------------------
D8_EXTRA_TEST(RwmMeshUnifyNormalsRestoresOutwardWindingOfClosedBox) {
  Mesh m = Box(0, 0, 0, 10, 10, 10);
  ON_Mesh& raw = m.raw();
  for (int fi : {1, 4, 7}) raw.m_F[fi].Flip();  // three faces reversed
  d8test::Check(m.Check().orientation_conflicts > 0, "reversed faces make orientation conflicts");
  const int flips = m.UnifyNormals();
  d8test::Check(flips == 3, "exactly the 3 reversed faces are flipped back");
  d8test::Check(m.Check().orientation_conflicts == 0 && m.IsClosedManifold(), "consistent closed manifold");
  d8test::Check(Near(m.Volume(), 1000.0, 1e-6), "signed volume +1000: normals point outward");

  Mesh inside = Box(0, 0, 0, 10, 10, 10).FlipNormals();
  d8test::Check(Near(inside.Volume(), -1000.0, 1e-6), "a fully flipped box has volume -1000");
  d8test::Check(inside.UnifyNormals() == 12, "all 12 faces are flipped to face outward");
  d8test::Check(Near(inside.Volume(), 1000.0, 1e-6), "volume +1000 afterwards");
}

// ---- rhino.mesh.fillmeshholes (D8-RHINO-MESH-0029) -------------------------------------------------------------------------------
D8_EXTRA_TEST(RwmMeshFillHolesClosesEveryOpeningWithinExtent) {
  Mesh one;
  AddBoxTriangles(one, 0, 0, 0, 10, 10, 10, /*skip_top=*/true);
  d8test::Check(one.FaceCount() == 10 && one.NakedEdgeLoops().size() == 1, "box without lid: 10 faces and one boundary loop");
  d8test::Check(one.FillSmallHoles(5.0) == 0, "hole diagonal 14.14 is above the extent 5: left open");
  d8test::Check(one.FillSmallHoles(20.0) == 1, "one hole filled");
  d8test::Check(one.IsClosedManifold() && one.NakedEdgeLoops().empty(), "closed afterwards");
  d8test::Check(one.FaceCount() == 14 && one.VertexCount() == 9, "4 fan triangles round one centre vertex: 14 faces, 9 vertices");
  d8test::Check(Near(one.Volume(), 1000.0, 1e-5) && Near(one.Area(), 600.0, 1e-5), "planar hole: volume 1000, area 600");
  d8test::Check(Euler(one) == 2, "Euler characteristic 2");

  Mesh two;
  AddBoxTriangles(two, 0, 0, 0, 10, 10, 10, true, true);
  d8test::Check(two.NakedEdgeLoops().size() == 2, "lid and floor removed: two loops");
  d8test::Check(two.FillSmallHoles(20.0) == 2, "both holes filled in one call");
  d8test::Check(two.IsClosedManifold() && Near(two.Volume(), 1000.0, 1e-5) && Euler(two) == 2, "closed box again");
}

// ---- rhino.mesh.weld (D8-RHINO-MESH-0092) ----------------------------------------------------------------------------------------
D8_EXTRA_TEST(RwmMeshWeldMergesCoincidentCornersIntoClosedSolid) {
  // Triangle soup: every face owns its 3 corners (36 vertices), all 18 edges are naked.
  const Mesh src = Box(0, 0, 0, 10, 10, 10);
  Mesh soup;
  ON_Mesh& raw = soup.raw();
  for (int i = 0; i < src.raw().m_F.Count(); ++i) {
    const ON_MeshFace& f = src.raw().m_F[i];
    ON_MeshFace nf;
    for (int k = 0; k < 3; ++k) {
      raw.m_V.Append(src.raw().m_V[f.vi[k]]);
      nf.vi[k] = raw.m_V.Count() - 1;
    }
    nf.vi[3] = nf.vi[2];
    raw.m_F.Append(nf);
  }
  d8test::Check(soup.VertexCount() == 36 && !soup.IsClosedManifold(), "soup: 36 vertices, not closed");
  d8test::Check(soup.Check().duplicate_vertices > 0, "the checker sees the coincident corners");
  const int merged = soup.MergeDuplicateVertices(1e-6);
  d8test::Check(merged == 28, "36 - 8 = 28 vertices merged away");
  d8test::Check(soup.VertexCount() == 8 && soup.FaceCount() == 12, "8 vertices, 12 faces");
  d8test::Check(soup.IsClosedManifold() && Euler(soup) == 2, "welded: closed manifold, Euler characteristic 2");
  d8test::Check(Near(soup.Volume(), 1000.0, 1e-6) && Near(soup.Area(), 600.0, 1e-6), "volume 1000, area 600 kept");
  d8test::Check(soup.MergeDuplicateVertices(1e-6) == 0, "welding twice changes nothing");
}

// ---- rhino.mesh.meshtorus (D8-RHINO-MESH-0059) -----------------------------------------------------------------------------------
D8_EXTRA_TEST(RwmMeshTorusHasClosedFormTopologyVolumeAndArea) {
  const double R = 10.0, r = 3.0, pi = 3.14159265358979323846;
  const int ms[2][2] = {{48, 24}, {12, 6}};
  for (const auto& seg : ms) {
    const int M = seg[0], mm = seg[1];
    const Mesh t = Mesh::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), R, r, M, mm);
    d8test::Check(t.VertexCount() == M * mm, "V = major_segments * minor_segments");
    d8test::Check(t.IsClosedManifold(), "closed manifold");
    d8test::Check(Euler(t) == 0, "Euler characteristic 0 (genus 1)");
    d8test::Check(t.FaceCount() == M * mm || t.FaceCount() == 2 * M * mm, "one quad or two triangles per grid cell");
    const double fm = std::sin(2.0 * pi / mm) / (2.0 * pi / mm);
    const double fM = std::sin(2.0 * pi / M) / (2.0 * pi / M);
    const double vol = 2.0 * pi * pi * R * r * r;
    const double area = 4.0 * pi * pi * R * r;
    // The faceted solid lies inside the smooth one and loses at most the polygon factors (cross-section fm^1 area, sweep fM^1).
    d8test::Check(t.Volume() < vol && t.Volume() > vol * fm * fM * 0.995, "volume within the polygon factors of 2 pi^2 R r^2");
    d8test::Check(t.Area() < area && t.Area() > area * fm * fM * 0.995 * 0.995, "area within the polygon factors of 4 pi^2 R r");
  }
  bool threw = false;
  try { Mesh::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), R, r, 2, 8); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "fewer than 3 segments is a reported error");
}

// ---- rhino.mesh.offsetmesh (D8-RHINO-MESH-0062) ----------------------------------------------------------------------------------
D8_EXTRA_TEST(RwmMeshOffsetMovesVerticesByExactDistanceInEachMode) {
  // Octahedron, vertices +-1 on the axes: every vertex normal is its own axis, so offset d gives the octahedron with a = 1 + d.
  Mesh o;
  ON_Mesh& raw = o.raw();
  const float v[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (const auto& p : v) raw.m_V.Append(ON_3fPoint(p[0], p[1], p[2]));
  const int tris[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
  for (const auto& t : tris) {
    ON_MeshFace f;
    f.vi[0] = t[0]; f.vi[1] = t[1]; f.vi[2] = t[2]; f.vi[3] = t[2];
    raw.m_F.Append(f);
  }
  d8test::Check(Near(o.Volume(), 4.0 / 3.0, 1e-6), "octahedron volume 4/3");
  for (double d : {0.5, 2.0, -0.25}) {
    const Mesh w = o.Offset(d);
    const double a = 1.0 + d;
    d8test::Check(Near(w.Volume(), 4.0 * a * a * a / 3.0, 1e-5), "offset octahedron volume 4 (1+d)^3 / 3");
    d8test::Check(w.VertexCount() == 6 && w.FaceCount() == 8, "topology unchanged");
  }
  d8test::Check(Near(o.Offset(0.0).Volume(), 4.0 / 3.0, 1e-9), "zero distance: same shape");
  d8test::Check(Near(o.Volume(), 4.0 / 3.0, 1e-9), "source mesh untouched");

  // Fixed-direction mode on a flat 3x3 grid in z = 0: every vertex moves by exactly d along the given unit direction.
  Mesh g;
  ON_Mesh& gr = g.raw();
  for (int j = 0; j < 3; ++j)
    for (int i = 0; i < 3; ++i) gr.m_V.Append(ON_3fPoint(float(i), float(j), 0.0f));
  for (int j = 0; j < 2; ++j)
    for (int i = 0; i < 2; ++i) {
      ON_MeshFace f;
      f.vi[0] = j * 3 + i; f.vi[1] = j * 3 + i + 1; f.vi[2] = (j + 1) * 3 + i + 1; f.vi[3] = (j + 1) * 3 + i;
      gr.m_F.Append(f);
    }
  const Mesh lifted = g.OffsetDirectional(3.0, Vector3d(0, 0, 1));
  const Mesh moved = g.OffsetDirectional(2.0, Vector3d(3, 4, 0));  // direction is normalised: (0.6, 0.8, 0)
  bool lift_ok = true, move_ok = true;
  for (int k = 0; k < 9; ++k) {
    const ON_3dPoint a(lifted.raw().m_V[k]), b(moved.raw().m_V[k]);
    if (!Near(a.x, k % 3, 1e-6) || !Near(a.y, k / 3, 1e-6) || !Near(a.z, 3.0, 1e-6)) lift_ok = false;
    if (!Near(b.x, k % 3 + 1.2, 1e-5) || !Near(b.y, k / 3 + 1.6, 1e-5) || !Near(b.z, 0.0, 1e-6)) move_ok = false;
  }
  d8test::Check(lift_ok, "direction +z: every vertex lands at z = 3");
  d8test::Check(move_ok, "direction (3,4,0): every vertex moves by (1.2, 1.6, 0), a distance of exactly 2");
  d8test::Check(Near(lifted.Area(), 4.0, 1e-6), "a translated flat mesh keeps its area");
}

// ---- rhino.mesh.meshbooleanunion / difference / intersection (D8-RHINO-MESH-0041 / 0038 / 0039) --------------------------------
// Two 10-cubes shifted by 5 along x overlap in a 5 x 10 x 10 box: union 15 x 10 x 10 (volume 1500, area 800), intersection
// (500, 400), A minus B = 5 x 10 x 10 (500, 400), symmetric difference 1000. All results are closed manifolds with Euler number 2.
D8_EXTRA_TEST(RwmMeshBooleanOfOverlappingBoxesMatchesBoxClosedForms) {
  const Mesh a = Box(0, 0, 0, 10, 10, 10);
  const Mesh b = Box(5, 0, 0, 15, 10, 10);
  const Mesh u = dino8::kernel::BooleanCombine(a, b, BooleanOp::Union);
  const Mesh i = dino8::kernel::BooleanCombine(a, b, BooleanOp::Intersection);
  const Mesh d = dino8::kernel::BooleanCombine(a, b, BooleanOp::Difference);
  d8test::Check(Near(u.Volume(), 1500.0, 1e-3) && Near(u.Area(), 800.0, 1e-3), "union: 15 x 10 x 10 -> volume 1500, area 800");
  d8test::Check(Near(i.Volume(), 500.0, 1e-3) && Near(i.Area(), 400.0, 1e-3), "intersection: 5 x 10 x 10 -> volume 500, area 400");
  d8test::Check(Near(d.Volume(), 500.0, 1e-3) && Near(d.Area(), 400.0, 1e-3), "difference: 5 x 10 x 10 -> volume 500, area 400");
  d8test::Check(u.IsClosedManifold() && i.IsClosedManifold() && d.IsClosedManifold(), "every result is a valid closed manifold");
  d8test::Check(Euler(u) == 2 && Euler(i) == 2 && Euler(d) == 2, "every result has Euler characteristic 2");
  const Mesh x = dino8::kernel::BooleanCombine(a, b, BooleanOp::SymmetricDifference);
  d8test::Check(Near(x.Volume(), 1000.0, 1e-3), "symmetric difference: 1500 - 500 = 1000");
  // Disjoint cubes: union keeps both volumes, intersection is empty, difference is A unchanged.
  const Mesh far = Box(30, 0, 0, 40, 10, 10);
  d8test::Check(Near(dino8::kernel::BooleanCombine(a, far, BooleanOp::Union).Volume(), 2000.0, 1e-3), "disjoint union: 2000");
  d8test::Check(Near(dino8::kernel::BooleanCombine(a, far, BooleanOp::Difference).Volume(), 1000.0, 1e-3), "disjoint difference: A unchanged");
  d8test::Check(Near(dino8::kernel::BooleanCombine(a, far, BooleanOp::Intersection).Volume(), 0.0, 1e-9), "disjoint intersection: empty");
}
