// SubD::EdgeLoopThrough() / SubD::EdgeRingThrough(): edge loops and rings of the control net.
//
// Oracles are combinatorial facts of the test cages, derived by hand, not read back from the code:
//   * a cube whose faces are 4 x 4 quad grids (all lattice points of [0,4]^3 on the surface; V - E + F = 98 - 192 + 96 = 2):
//       - the belt at z = 2 is a closed loop of 4 sides x 4 edges = 16 unit edges, every vertex on it has valence 4;
//       - the quad ring through a vertical edge of the row z in [1,2] is the closed strip of 4 sides x 4 quads = 16 quads, one
//         parallel (vertical) edge per quad column;
//       - a loop along a cube edge runs through the 3 regular vertices and ends at the two valence-3 cube corners: 4 edges, open;
//   * a 3 x 3 quad sheet (4 x 4 vertices): the interior horizontal loop has 3 edges (ends on the valence-3 boundary vertices),
//     the ring through a boundary edge is the 3-quad column: 4 edges, open;
//   * the one-quad-per-side cube cage (every vertex valence 3): any loop is a single edge, the ring is the 4-quad belt, closed.
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::SubDEdgeChain;

// Cube [0,n]^3 whose six faces are n x n quad grids, outward winding.
dino8::kernel::Mesh GridCube(int n) {
  dino8::kernel::Mesh m;
  ON_Mesh& raw = m.raw();
  std::map<std::tuple<int, int, int>, int> id;
  const auto vtx = [&](int x, int y, int z) {
    const auto k = std::make_tuple(x, y, z);
    auto it = id.find(k);
    if (it != id.end()) return it->second;
    const int i = raw.m_V.Count();
    raw.m_V.Append(ON_3fPoint(float(x), float(y), float(z)));
    id[k] = i;
    return i;
  };
  const double c = n / 2.0;
  for (int axis = 0; axis < 3; ++axis) {
    for (int side = 0; side < 2; ++side) {
      for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
          int corner[4][3];
          const int du[4] = {0, 1, 1, 0}, dv[4] = {0, 0, 1, 1};
          for (int k = 0; k < 4; ++k) {
            int p[3];
            p[axis] = side * n;
            p[(axis + 1) % 3] = i + du[k];
            p[(axis + 2) % 3] = j + dv[k];
            for (int t = 0; t < 3; ++t) corner[k][t] = p[t];
          }
          // outward check with the geometric normal
          double e1[3], e2[3], nrm[3], ctr[3] = {0, 0, 0};
          for (int t = 0; t < 3; ++t) { e1[t] = corner[1][t] - corner[0][t]; e2[t] = corner[3][t] - corner[0][t]; }
          nrm[0] = e1[1] * e2[2] - e1[2] * e2[1]; nrm[1] = e1[2] * e2[0] - e1[0] * e2[2]; nrm[2] = e1[0] * e2[1] - e1[1] * e2[0];
          for (int k = 0; k < 4; ++k) for (int t = 0; t < 3; ++t) ctr[t] += corner[k][t] / 4.0;
          const double dot = nrm[0] * (ctr[0] - c) + nrm[1] * (ctr[1] - c) + nrm[2] * (ctr[2] - c);
          ON_MeshFace f;
          if (dot > 0) { for (int k = 0; k < 4; ++k) f.vi[k] = vtx(corner[k][0], corner[k][1], corner[k][2]); }
          else { for (int k = 0; k < 4; ++k) f.vi[k] = vtx(corner[3 - k][0], corner[3 - k][1], corner[3 - k][2]); }
          raw.m_F.Append(f);
        }
      }
    }
  }
  return m;
}

dino8::kernel::Mesh Sheet4x4() {  // 3 x 3 quads in the z = 0 plane, normal +z
  dino8::kernel::Mesh m;
  ON_Mesh& raw = m.raw();
  for (int j = 0; j < 4; ++j)
    for (int i = 0; i < 4; ++i) raw.m_V.Append(ON_3fPoint(float(i), float(j), 0.0f));
  for (int j = 0; j < 3; ++j) {
    for (int i = 0; i < 3; ++i) {
      ON_MeshFace f;
      f.vi[0] = j * 4 + i; f.vi[1] = j * 4 + i + 1; f.vi[2] = (j + 1) * 4 + i + 1; f.vi[3] = (j + 1) * 4 + i;
      raw.m_F.Append(f);
    }
  }
  return m;
}

double Len(const Point3d& a, const Point3d& b) { return a.DistanceTo(b); }

// True when each edge of a loop shares an end point with the next one.
bool Connected(const SubDEdgeChain& c) {
  for (size_t i = 0; i + 1 < c.edges.size(); ++i) {
    const auto& a = c.edges[i];
    const auto& b = c.edges[i + 1];
    const bool share = Len(a.p0, b.p0) < 1e-9 || Len(a.p0, b.p1) < 1e-9 || Len(a.p1, b.p0) < 1e-9 || Len(a.p1, b.p1) < 1e-9;
    if (!share) return false;
  }
  return true;
}

bool AllDistinct(const SubDEdgeChain& c) {
  for (size_t i = 0; i < c.edges.size(); ++i)
    for (size_t j = i + 1; j < c.edges.size(); ++j) {
      const auto& a = c.edges[i];
      const auto& b = c.edges[j];
      if ((Len(a.p0, b.p0) < 1e-9 && Len(a.p1, b.p1) < 1e-9) || (Len(a.p0, b.p1) < 1e-9 && Len(a.p1, b.p0) < 1e-9)) return false;
    }
  return true;
}

}  // namespace

D8_EXTRA_TEST(SubDEdgeLoopAroundAGridCubeIsTheClosedBelt) {
  const SubD cube = SubD::FromControlMesh(GridCube(4));
  d8test::Check(cube.FaceCount() == 96 && cube.EdgeCount() == 192 && cube.VertexCount() == 98, "oracle sanity: the grid cube has 96 faces, 192 edges, 98 vertices (V - E + F = 2)");
  const SubDEdgeChain loop = cube.EdgeLoopThrough(Point3d(0, 0, 2), Point3d(1, 0, 2), 1e-6);
  d8test::Check(loop.found && loop.closed, "the belt at z = 2 through the edge (0,0,2)-(1,0,2) is found and closed");
  d8test::Check(loop.edges.size() == 16, "...16 edges: 4 sides x 4 unit edges");
  bool flat = true, unit = true;
  for (const auto& e : loop.edges) {
    flat = flat && std::abs(e.p0.z - 2.0) < 1e-9 && std::abs(e.p1.z - 2.0) < 1e-9;
    unit = unit && std::abs(Len(e.p0, e.p1) - 1.0) < 1e-9;
  }
  d8test::Check(flat && unit, "...every edge is a unit edge lying in the plane z = 2");
  d8test::Check(AllDistinct(loop) && Connected(loop), "...16 distinct edges, each sharing an end with the next");
}

D8_EXTRA_TEST(SubDEdgeRingAroundAGridCubeIsTheClosedQuadStrip) {
  const SubD cube = SubD::FromControlMesh(GridCube(4));
  const SubDEdgeChain ring = cube.EdgeRingThrough(Point3d(1, 0, 1), Point3d(1, 0, 2), 1e-6);
  d8test::Check(ring.found && ring.closed && ring.edges.size() == 16, "the ring through the vertical edge (1,0,1)-(1,0,2) is closed with 16 edges (4 sides x 4 quad columns)");
  bool vertical = true;
  for (const auto& e : ring.edges) vertical = vertical && std::abs(e.p0.x - e.p1.x) + std::abs(e.p0.y - e.p1.y) < 1e-9 && std::min(e.p0.z, e.p1.z) > 0.999 && std::max(e.p0.z, e.p1.z) < 2.001;
  d8test::Check(vertical, "...every edge is a unit vertical edge of the row z in [1, 2] (parallel across the strip)");
  d8test::Check(AllDistinct(ring), "...16 distinct edges");
}

D8_EXTRA_TEST(SubDEdgeLoopAlongACubeEdgeStopsAtTheValenceThreeCorners) {
  const SubD cube = SubD::FromControlMesh(GridCube(4));
  const SubDEdgeChain loop = cube.EdgeLoopThrough(Point3d(1, 0, 0), Point3d(2, 0, 0), 1e-6);
  d8test::Check(loop.found && !loop.closed && loop.edges.size() == 4, "the loop along the cube edge y = z = 0 has its 4 unit edges and is open (ends at the two cube corners)");
  double lo = 1e9, hi = -1e9;
  for (const auto& e : loop.edges) { lo = std::min({lo, e.p0.x, e.p1.x}); hi = std::max({hi, e.p0.x, e.p1.x}); }
  d8test::Check(std::abs(lo - 0.0) < 1e-9 && std::abs(hi - 4.0) < 1e-9 && Connected(loop), "...spanning x from 0 to 4 as a connected chain");
}

D8_EXTRA_TEST(SubDEdgeLoopAndRingOnAnOpenSheet) {
  const SubD sheet = SubD::FromControlMesh(Sheet4x4());
  const SubDEdgeChain loop = sheet.EdgeLoopThrough(Point3d(0, 1, 0), Point3d(1, 1, 0), 1e-6);
  d8test::Check(loop.found && !loop.closed && loop.edges.size() == 3 && Connected(loop), "interior horizontal loop of the 3 x 3 sheet: 3 edges, open (boundary vertices have valence 3)");
  const SubDEdgeChain ring = sheet.EdgeRingThrough(Point3d(1, 0, 0), Point3d(2, 0, 0), 1e-6);
  d8test::Check(ring.found && !ring.closed && ring.edges.size() == 4, "ring through a boundary edge: the 3-quad column has 4 parallel edges, open");
  bool horizontal = true;
  for (const auto& e : ring.edges) horizontal = horizontal && std::abs(e.p0.y - e.p1.y) < 1e-9 && std::min(e.p0.x, e.p1.x) > 0.999 && std::max(e.p0.x, e.p1.x) < 2.001;
  d8test::Check(horizontal, "...all four lie between x = 1 and x = 2, one per row y = 0..3");
}

D8_EXTRA_TEST(SubDEdgeLoopOnAOneQuadPerSideCubeIsASingleEdgeAndTheRingTheFourQuadBelt) {
  const SubD cube = SubD::FromControlMesh(GridCube(1));
  const SubDEdgeChain loop = cube.EdgeLoopThrough(Point3d(0, 0, 0), Point3d(1, 0, 0), 1e-6);
  d8test::Check(loop.found && !loop.closed && loop.edges.size() == 1, "every vertex of the 6-face cube has valence 3: the loop through an edge is that single edge");
  const SubDEdgeChain ring = cube.EdgeRingThrough(Point3d(0, 0, 0), Point3d(1, 0, 0), 1e-6);
  d8test::Check(ring.found && ring.closed && ring.edges.size() == 4 && AllDistinct(ring), "the ring through an edge is the closed 4-quad belt: 4 parallel edges");
  d8test::Check(!cube.EdgeLoopThrough(Point3d(0, 0, 0), Point3d(1, 1, 1), 1e-6).found && !cube.EdgeRingThrough(Point3d(0, 0, 0), Point3d(1, 1, 1), 1e-6).found,
                "two vertices that are not joined by an edge (a space diagonal) give found = false");
}
