// Brep::ThickenWelded (and a pin on Brep::FromSurface): single-surface bodies with REAL topology.
//
// FromSurface() already has genuine topology (NewOuterLoop() builds the corner vertices and boundary edges along with the loop: the
// parity map's "surface-only" note for it is stale; pinned here against Euler and area). Thicken() builds every cap and wall with
// the bare NewFace(surface_index): no vertices, edges or loops at all. ThickenWelded() rebuilds the same geometry as complete
// faces welded by JoinNakedEdges(). Oracles independent of the kernel's integrators:
//   * Euler characteristic: a standalone face (a disc) V - E + F = 4 - 4 + 1 = 1; the closed hexahedral solid 8 - 12 + 6 = 2;
//   * every edge of the solid borders exactly two trims, every edge of the standalone face exactly one;
//   * a flat 3 x 4 sheet thickened by t has volume 12 t; a tilted flat patch of edge vectors (3, 0, 1.5) and (0, 4, 0) has area
//     |(3, 0, 1.5) x (0, 4, 0)| = sqrt(180) and its thickening by t has volume sqrt(180) t (the offset is along the unit normal).
#include <cmath>
#include <stdexcept>
#include <cstdio>
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

NurbsSurface Bilinear(Point3d a, Point3d b, Point3d c, Point3d d) {
  // grid index i * 2 + j: (i, j) = (0,0) a, (0,1) b, (1,0) c, (1,1) d  (u along i, v along j)
  return NurbsSurface::FromControlGrid({a, b, c, d}, 2, 2, 1, 1);
}

NurbsSurface FlatSheetSurface() { return Bilinear(Point3d(0, 0, 0), Point3d(0, 4, 0), Point3d(3, 0, 0), Point3d(3, 4, 0)); }

NurbsSurface TiltedSheetSurface() { return Bilinear(Point3d(0, 0, 0), Point3d(0, 4, 0), Point3d(3, 0, 1.5), Point3d(3, 4, 1.5)); }

NurbsSurface BulgedSurface() {
  std::vector<Point3d> grid;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) grid.push_back(Point3d(i, j, (i == 2 && j == 2) ? 3.0 : 0.0));
  }
  return NurbsSurface::FromControlGrid(grid, 4, 4, 3, 3);
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

D8_EXTRA_TEST(FromSurfaceAlreadyHasRealDiscTopology) {
  const Brep face = Brep::FromSurface(BulgedSurface());
  const Counts c = CountTopology(face);
  d8test::Check(c.vertices == 4 && c.edges == 4 && c.faces == 1, "a standalone face has 4 vertices, 4 edges, 1 face");
  d8test::Check(c.vertices - c.edges + c.faces == 1, "Euler characteristic V - E + F = 1 (a disc)");
  d8test::Check(c.edges_with_one_trim == 4 && c.edges_with_two_trims == 0 && c.other_edges == 0, "all four boundary edges are naked");
  d8test::Check(face.raw().IsValid(), "ON_Brep::IsValid() is true (FromSurface() reports false: a loop with no edges)");
  d8test::Check(face.LiveVertexCount() == 4 && face.LiveEdgeCount() == 4, "LiveVertexCount()/LiveEdgeCount() report 4 / 4");
  d8test::Check(face.LoopCount(0) == 1, "the face has exactly one (outer) loop");
}

D8_EXTRA_TEST(FromSurfaceAreaMatchesClosedForms) {
  // Area of a flat 3 x 4 sheet is 12, of the tilted one sqrt(180): independent closed forms.
  const Brep flat = Brep::FromSurface(FlatSheetSurface());
  const Brep tilt = Brep::FromSurface(TiltedSheetSurface());
  d8test::Check(std::fabs(flat.Area() - 12.0) < 1e-9, "flat sheet area 12 (exact)");
  d8test::Check(std::fabs(tilt.Area() - std::sqrt(180.0)) < 1e-9, "tilted sheet area sqrt(180) (exact)");
}

D8_EXTRA_TEST(ThickenWeldedFlatSheetIsAnExactHexahedron) {
  const Brep sheet = Brep::FromSurface(FlatSheetSurface());
  const Brep solid = Brep::ThickenWelded(sheet, 2.0);
  const Counts c = CountTopology(solid);
  d8test::Check(c.vertices == 8 && c.edges == 12 && c.faces == 6, "thickened flat sheet: 8 vertices, 12 edges, 6 faces");
  d8test::Check(c.vertices - c.edges + c.faces == 2, "Euler characteristic V - E + F = 2");
  d8test::Check(c.edges_with_two_trims == 12 && c.edges_with_one_trim == 0 && c.other_edges == 0,
                "every edge borders exactly two faces");
  d8test::Check(solid.raw().IsValid() && solid.raw().IsSolid(), "IsValid() and IsSolid() true");
  const Mesh m = solid.TessellateToClosedMesh(8, 8);
  d8test::Check(m.IsClosedManifold(), "tessellates to a closed manifold");
  d8test::Check(std::fabs(m.Volume() - 24.0) < 1e-9, "volume is exactly 12 * 2 = 24");
}

D8_EXTRA_TEST(ThickenWeldedTiltedSheetVolumeIsAreaTimesThickness) {
  const Brep sheet = Brep::FromSurface(TiltedSheetSurface());
  const Brep solid = Brep::ThickenWelded(sheet, 0.75);
  const Counts c = CountTopology(solid);
  d8test::Check(c.vertices - c.edges + c.faces == 2 && c.faces == 6 && c.edges_with_two_trims == 12, "tilted sheet: closed hexahedral topology");
  d8test::Check(solid.raw().IsValid() && solid.raw().IsSolid(), "tilted sheet: IsValid() and IsSolid()");
  const Mesh m = solid.TessellateToClosedMesh(8, 8);
  d8test::Check(m.IsClosedManifold(), "tilted sheet: closed manifold tessellation");
  d8test::Check(std::fabs(m.Volume() - std::sqrt(180.0) * 0.75) < 1e-5, "tilted sheet: volume equals sqrt(180) * 0.75 (area * thickness, to 1e-5: the offset surface is an approximation)");
}

D8_EXTRA_TEST(ThickenWeldedSymmetricAndNegativeThicknessKeepTheSameVolume) {
  const Brep sheet = Brep::FromSurface(TiltedSheetSurface());
  const double expect = std::sqrt(180.0) * 1.0;
  const Brep sym = Brep::ThickenWelded(sheet, 1.0, /*symmetric=*/true);
  const Brep neg = Brep::ThickenWelded(sheet, -1.0);
  d8test::Check(sym.raw().IsValid() && sym.raw().IsSolid() && neg.raw().IsValid() && neg.raw().IsSolid(),
                "symmetric and negative-thickness results are valid solids");
  d8test::Check(std::fabs(sym.TessellateToClosedMesh(8, 8).Volume() - expect) < 1e-5, "symmetric thicken: volume area * 1");
  d8test::Check(std::fabs(neg.TessellateToClosedMesh(8, 8).Volume() - expect) < 1e-5, "negative thickness: volume area * 1");
}

D8_EXTRA_TEST(ThickenWeldedCurvedSheetIsAValidSolid) {
  // The bulged bicubic: thickening is along the surface normal, no closed form, so only the topology is asserted, plus the
  // implementation-independent bounds on the volume: strictly between 0 and (area of the sheet's xy-projection = 9) *
  // (thickness / min(cos tilt)) is too loose to prove anything, so the volume is compared with the surface-only Thicken().
  const Brep sheet = Brep::FromSurface(BulgedSurface());
  const Brep plain = Brep::Thicken(sheet, 0.2);
  const Brep welded = Brep::ThickenWelded(sheet, 0.2);
  const Counts c = CountTopology(welded);
  d8test::Check(c.vertices - c.edges + c.faces == 2 && c.faces == 6 && c.edges_with_two_trims == 12, "bulged sheet: closed hexahedral topology");
  d8test::Check(welded.raw().IsValid() && welded.raw().IsSolid(), "bulged sheet: IsValid() and IsSolid()");
  d8test::Check(std::fabs(welded.TessellateToClosedMesh(24, 24).Volume() - plain.TessellateToClosedMesh(24, 24).Volume()) < 1e-6,
                "same tessellated volume as the surface-only Thicken() (to 1e-6)");
}

D8_EXTRA_TEST(ThickenWeldedKeepsThickensRefusals) {
  const Brep sheet = Brep::FromSurface(FlatSheetSurface());
  bool zero = false, nan = false;
  try { Brep::ThickenWelded(sheet, 0.0); } catch (const std::exception&) { zero = true; }
  try { Brep::ThickenWelded(sheet, std::nan("")); } catch (const std::exception&) { nan = true; }
  d8test::Check(zero && nan, "a zero or non-finite thickness is refused");
}

// A sheet closed in ONE direction (a full cylinder wall, r = 1, h = 2): Thicken() makes 4 faces (inner and outer wall, two flat
// annuli), the solid tube of volume pi ((r + t)^2 - r^2) h. ThickenWelded() must weld it into a valid genus-1 solid.
D8_EXTRA_TEST(ThickenWeldedFullCylinderWallIsAValidTube) {
  const double r = 1.0, h = 2.0, t = 0.25;
  ON_Plane base(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1));
  ON_Cylinder cyl(ON_Circle(base, r), h);
  NurbsSurface wall;
  if (!cyl.GetNurbForm(wall.raw())) { d8test::Check(false, "ON_Cylinder::GetNurbForm built the wall"); return; }
  const Brep sheet = Brep::FromSurface(wall);
  Brep tube;
  try {
    tube = Brep::ThickenWelded(sheet, t);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "ThickenWelded refused the cylinder wall: %s\n", e.what());
    d8test::Check(false, "ThickenWelded welds the cylinder wall (it refused)");
    return;
  }
  const Counts c = CountTopology(tube);
  d8test::Check(tube.raw().IsValid() && tube.raw().IsSolid(), "welded tube: IsValid() and IsSolid()");
  d8test::Check(c.faces == 4 && c.other_edges == 0 && c.edges_with_one_trim == 0, "welded tube: 4 faces and every edge borders exactly two trims");
  // A solid torus-like tube has Euler characteristic 0 (genus 1).
  d8test::Check(c.vertices - c.edges + c.faces == 0, "welded tube: Euler characteristic V - E + F = 0 (genus 1)");
  const double expect = ON_PI * ((r + t) * (r + t) - r * r) * h;
  d8test::Check(std::fabs(tube.TessellateToClosedMesh(48, 8).Volume() - expect) / expect < 0.01, "welded tube: volume matches pi ((r+t)^2 - r^2) h within 1%");
}
