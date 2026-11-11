// Brep::ExtrudeFaceWelded: a freeform face extruded into a solid with REAL topology.
//
// ExtrudeFace()'s untrimmed path builds its six faces with the bare NewFace(surface_index) overload: the solid has no vertices,
// edges or loops, so ON_Brep::IsValid() is false and there is nothing for the adjacency queries to walk (the parity map's
// "Genuine topology produced by every constructor/primitive" gap). ExtrudeFaceWelded() builds the same geometry as a welded
// B-rep. The oracles are independent of the kernel's own integrators:
//   * Euler's formula for a closed genus-0 polyhedral topology: V - E + F = 2 (8 - 12 + 6); the open 4-wall tube: 8 - 12 + 4 = 0;
//   * every edge of the closed solid borders exactly two trims, every edge of the tube's two rims exactly one;
//   * volume: the face is a GRAPH z = f(x, y) over a rectangle, so the volume swept by translating it along d is
//     |d.z| * (rectangle area) for ANY f and any x/y component of d (Cavalieri; no fold when the slabs do not overlap itself).
#include <cmath>
#include <stdexcept>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Mesh;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

// A bicubic Bezier graph z = f(x, y) over [0, 3] x [0, 3] with an asymmetric bump (control point (2, 2) lifted by 3).
Brep BulgedSheet() {
  std::vector<Point3d> grid;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) grid.push_back(Point3d(i, j, (i == 2 && j == 2) ? 3.0 : 0.0));
  }
  return Brep::FromSurface(NurbsSurface::FromControlGrid(grid, 4, 4, 3, 3));
}

// A flat 3 x 4 sheet in the z = 0 plane.
Brep FlatSheet() {
  std::vector<Point3d> grid;
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) grid.push_back(Point3d(3.0 * i, 4.0 * j, 0.0));
  }
  return Brep::FromSurface(NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1));
}

struct Counts {
  int vertices = 0, edges = 0, faces = 0;
  int edges_with_two_trims = 0, edges_with_one_trim = 0, other_edges = 0;
};

Counts CountTopology(const Brep& b) {
  Counts c;
  const ON_Brep& r = b.raw();
  for (int i = 0; i < r.m_V.Count(); ++i) c.vertices += r.m_V[i].m_vertex_index >= 0 ? 1 : 0;
  for (int i = 0; i < r.m_F.Count(); ++i) c.faces += r.m_F[i].m_face_index >= 0 ? 1 : 0;
  for (int i = 0; i < r.m_E.Count(); ++i) {
    if (r.m_E[i].m_edge_index < 0) continue;
    ++c.edges;
    const int t = r.m_E[i].TrimCount();
    if (t == 2) ++c.edges_with_two_trims;
    else if (t == 1) ++c.edges_with_one_trim;
    else ++c.other_edges;
  }
  return c;
}

}  // namespace

D8_EXTRA_TEST(ExtrudeFaceWeldedFreeformIsAClosedEulerSphere) {
  const Brep sheet = BulgedSheet();
  const Brep solid = Brep::ExtrudeFaceWelded(sheet, 0, Vector3d(0, 0, 1.0));
  const Counts c = CountTopology(solid);
  d8test::Check(c.vertices == 8 && c.edges == 12 && c.faces == 6,
                "welded extrusion of a bicubic freeform face has 8 vertices, 12 edges, 6 faces");
  d8test::Check(c.vertices - c.edges + c.faces == 2, "Euler characteristic V - E + F = 2 (a closed genus-0 shell)");
  d8test::Check(c.edges_with_two_trims == 12 && c.edges_with_one_trim == 0 && c.other_edges == 0,
                "every one of the 12 edges borders exactly two faces (a closed 2-manifold)");
  d8test::Check(solid.raw().IsValid(), "ON_Brep::IsValid() is true (the surface-only extrusion reports false)");
  d8test::Check(solid.raw().IsSolid(), "ON_Brep::IsSolid() is true");
  d8test::Check(solid.LiveVertexCount() == 8 && solid.LiveEdgeCount() == 12, "LiveVertexCount()/LiveEdgeCount() report 8 / 12");
  // Cavalieri: the face is a graph over [0,3] x [0,3] (area 9) and the translate is (0, 0, 1): V = 9 * 1.
  const Mesh m = solid.TessellateToClosedMesh(24, 24);
  d8test::Check(m.IsClosedManifold(), "the welded solid tessellates to a closed manifold");
  d8test::Check(std::fabs(m.Volume() - 9.0) < 0.02, "tessellated volume equals the Cavalieri closed form 9 * 1 = 9 (within 0.02)");
}

D8_EXTRA_TEST(ExtrudeFaceWeldedObliqueDirectionKeepsCavalieriVolume) {
  // d = (1.5, -0.75, 2): the swept volume is |d.z| * 9 = 18 whatever the x/y shear of the direction.
  const Brep solid = Brep::ExtrudeFaceWelded(BulgedSheet(), 0, Vector3d(1.5, -0.75, 2.0));
  const Counts c = CountTopology(solid);
  d8test::Check(c.vertices - c.edges + c.faces == 2 && c.faces == 6, "oblique extrusion: Euler characteristic 2 with 6 faces");
  d8test::Check(solid.raw().IsValid() && solid.raw().IsSolid(), "oblique extrusion: IsValid() and IsSolid()");
  const Mesh m = solid.TessellateToClosedMesh(24, 24);
  d8test::Check(m.IsClosedManifold(), "oblique extrusion tessellates to a closed manifold");
  d8test::Check(std::fabs(m.Volume() - 18.0) < 0.05, "oblique extrusion volume equals |d.z| * area = 2 * 9 = 18 (within 0.05)");
}

D8_EXTRA_TEST(ExtrudeFaceWeldedFlatSheetIsAnExactPrism) {
  const Brep solid = Brep::ExtrudeFaceWelded(FlatSheet(), 0, Vector3d(0, 0, 2.5));
  const Counts c = CountTopology(solid);
  d8test::Check(c.vertices == 8 && c.edges == 12 && c.faces == 6 && c.edges_with_two_trims == 12,
                "flat sheet: a hexahedron's 8 / 12 / 6 with every edge shared by two faces");
  d8test::Check(solid.raw().IsValid() && solid.raw().IsSolid(), "flat sheet: IsValid() and IsSolid()");
  const Mesh m = solid.TessellateToClosedMesh(8, 8);
  d8test::Check(std::fabs(m.Volume() - 3.0 * 4.0 * 2.5) < 1e-9, "flat sheet: volume is exactly 3 * 4 * 2.5 = 30");
}

D8_EXTRA_TEST(ExtrudeFaceWeldedUncappedIsAnOpenTubeWithEightNakedEdges) {
  const Brep tube = Brep::ExtrudeFaceWelded(BulgedSheet(), 0, Vector3d(0, 0, 1.0), /*cap=*/false);
  const Counts c = CountTopology(tube);
  d8test::Check(c.vertices == 8 && c.edges == 12 && c.faces == 4, "uncapped: 8 vertices, 12 edges, 4 walls");
  d8test::Check(c.vertices - c.edges + c.faces == 0, "uncapped: Euler characteristic V - E + F = 0 (an annulus)");
  d8test::Check(c.edges_with_two_trims == 4 && c.edges_with_one_trim == 8 && c.other_edges == 0,
                "uncapped: the 4 vertical seams are shared by two walls, the 8 rim edges are naked");
  d8test::Check(tube.raw().IsValid() && !tube.raw().IsSolid(), "uncapped: IsValid() true, IsSolid() false (open)");
}

D8_EXTRA_TEST(ExtrudeFaceWeldedGeometryMatchesTheSurfaceOnlyExtrusion) {
  const Brep sheet = BulgedSheet();
  const Vector3d d(0.5, 0.25, 1.25);
  const Brep plain = Brep::ExtrudeFace(sheet, 0, d);
  const Brep welded = Brep::ExtrudeFaceWelded(sheet, 0, d);
  // (The surface-only extrusion's own ON_Brep bounding box is degenerate - it has no edges - so it is not the oracle here.) The
  // welded solid's box must contain the eight corners (0 or 3, 0 or 3, 0) + {0, d} and sit inside the control hull
  // [0, 3.5] x [0, 3.25] x [0, 3 + 1.25] of the bicubic face plus its translate.
  ON_BoundingBox b;
  welded.raw().GetBoundingBox(b);
  bool corners_inside = true;
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      for (int k = 0; k < 2; ++k) {
        const ON_3dPoint p(3.0 * i + k * d.x, 3.0 * j + k * d.y, k * d.z);
        corners_inside = corners_inside && b.IsPointIn(p, false);
      }
    }
  }
  d8test::Check(corners_inside, "the welded solid's bounding box contains all eight corners of the extruded rectangle");
  d8test::Check(b.m_min.x > -1e-9 && b.m_min.y > -1e-9 && b.m_min.z > -1e-9 && b.m_max.x < 3.5 + 1e-9 && b.m_max.y < 3.25 + 1e-9 &&
                    b.m_max.z < 4.25 + 1e-9,
                "and stays inside the control hull of the face and its translate");
  d8test::Check(plain.FaceCount() == welded.FaceCount(), "same face count as the surface-only extrusion");
  d8test::Check(std::fabs(plain.TessellateToClosedMesh(24, 24).Volume() - welded.TessellateToClosedMesh(24, 24).Volume()) < 1e-6,
                "same tessellated volume as the surface-only extrusion (to 1e-6)");
}

D8_EXTRA_TEST(ExtrudeFaceWeldedKeepsExtrudeFacesRefusals) {
  const Brep sheet = BulgedSheet();
  bool zero = false, bad_index = false, negative = false;
  try { Brep::ExtrudeFaceWelded(sheet, 0, Vector3d(0, 0, 0)); } catch (const std::exception&) { zero = true; }
  try { Brep::ExtrudeFaceWelded(sheet, 7, Vector3d(0, 0, 1)); } catch (const std::exception&) { bad_index = true; }
  try { Brep::ExtrudeFaceWelded(sheet, -1, Vector3d(0, 0, 1)); } catch (const std::exception&) { negative = true; }
  d8test::Check(zero, "a zero direction is refused");
  d8test::Check(bad_index && negative, "an out-of-range face_index is refused");
}
