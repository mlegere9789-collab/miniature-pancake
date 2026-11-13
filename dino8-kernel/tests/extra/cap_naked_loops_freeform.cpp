// Brep::CapNakedLoops: closing a NON-PLANAR naked loop with a freeform (Coons) patch welded onto the loop's own edges.
//
// CapPlanarHoles() caps only a straight-edged loop whose vertices lie in one plane, and names every other chain it declines
// (SkippedCap::NonPlanar / CurvedEdge) without closing it. CapNakedLoops() is the same pass plus a Coons patch for a closed
// 4-edge loop that is not planar (skew corners) or has curved edges. The oracles are independent of the kernel's integrators:
//   * Euler's formula for the closed genus-0 result, V - E + F = 2, and every edge bordering exactly two trims;
//   * ON_Brep::IsValid() / IsSolid();
//   * volume in closed form. A unit-square prism with the four top corners at heights 1, 1, 1, 1 + d is capped by the bilinear
//     patch z = 1 + d*(corner weight) (the Coons patch of four straight edges IS the bilinear patch), so the volume is
//     1 + d/4 whichever corner is raised. For the curved tube the two caps are Coons patches of one rim and its translate by
//     (0, 0, 1), both graphs over [0,3] x [0,3], so Cavalieri gives exactly 9 * 1.
#include <cmath>
#include <string>
#include <utility>
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

// A vertical-walled prism over the CCW polygon `base` (z = 0), open at the top: the top corner above base[i] sits at heights[i].
// Every wall is a planar quad whatever the heights are, so only the missing top is non-planar.
Brep PrismMissingTop(const std::vector<Point3d>& base, const std::vector<double>& heights) {
  const size_t n = base.size();
  std::vector<Brep::PlanarFace> faces;
  auto make = [](std::vector<Point3d> loop) {
    Brep::PlanarFace f;
    ON_3dVector normal = ON_CrossProduct(loop[1] - loop[0], loop[2] - loop[0]);
    normal.Unitize();
    f.plane = ON_Plane(loop[0], normal);
    f.loop = std::move(loop);
    return f;
  };
  std::vector<Point3d> bottom(base.rbegin(), base.rend());  // CCW seen from below
  faces.push_back(make(bottom));
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    const Point3d top_i(base[i].x, base[i].y, heights[i]);
    const Point3d top_j(base[j].x, base[j].y, heights[j]);
    faces.push_back(make({base[i], base[j], top_j, top_i}));
  }
  return Brep::FromPlanarFaces(faces);
}

std::vector<Point3d> UnitSquare() { return {Point3d(0, 0, 0), Point3d(1, 0, 0), Point3d(1, 1, 0), Point3d(0, 1, 0)}; }

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

// One corner of the unit-square prism's top raised by d above the flat height 1: assert the whole closed-solid contract.
void CheckSkewCapOnCorner(int corner, double d, const char* tag) {
  std::vector<double> heights(4, 1.0);
  heights[static_cast<size_t>(corner)] += d;
  Brep shell = PrismMissingTop(UnitSquare(), heights);
  const Counts before = CountTopology(shell);
  d8test::Check(before.vertices == 8 && before.edges == 12 && before.faces == 5 && before.edges_with_one_trim == 4,
                (std::string(tag) + ": the open prism has 8 vertices, 12 edges, 5 faces and a 4-edge naked top").c_str());

  Brep probe = shell;
  d8test::Check(probe.CapPlanarHoles() == 0, (std::string(tag) + ": CapPlanarHoles() leaves the skew top open (0 caps)").c_str());

  std::vector<Brep::SkippedCap> skipped;
  const int added = shell.CapNakedLoops(dino8::kernel::tolerance::kEdgeJoin, &skipped);
  d8test::Check(added == 1, (std::string(tag) + ": CapNakedLoops() adds exactly one cap").c_str());
  d8test::Check(skipped.empty(), (std::string(tag) + ": and skips no chain").c_str());
  const Counts c = CountTopology(shell);
  d8test::Check(c.vertices == 8 && c.edges == 12 && c.faces == 6,
                (std::string(tag) + ": the capped solid has 8 vertices, 12 edges, 6 faces (the patch welds onto the four rim edges)").c_str());
  d8test::Check(c.vertices - c.edges + c.faces == 2, (std::string(tag) + ": Euler characteristic V - E + F = 2").c_str());
  d8test::Check(c.edges_with_two_trims == 12 && c.edges_with_one_trim == 0 && c.other_edges == 0,
                (std::string(tag) + ": every one of the 12 edges borders exactly two trims").c_str());
  d8test::Check(shell.raw().IsValid() && shell.raw().IsSolid(), (std::string(tag) + ": ON_Brep::IsValid() and IsSolid()").c_str());
  const Mesh m = shell.TessellateToClosedMesh(24, 24);
  d8test::Check(m.IsClosedManifold(), (std::string(tag) + ": tessellates to a closed manifold").c_str());
  const double expected = 1.0 + d / 4.0;
  d8test::Check(std::fabs(m.Volume() - expected) < 2e-3,
                (std::string(tag) + ": volume equals the bilinear-patch closed form 1 + d/4 (within 2e-3)").c_str());
}

// A bicubic Bezier graph z = f(x, y) over [0, 3] x [0, 3] whose boundary edges are curved: control point (1, 0) lifted to 2 (the
// y = 0 edge) and (3, 2) lowered to -1.5 (the x = 3 edge). The four corners stay at z = 0.
Brep CurvedBoundarySheet() {
  std::vector<Point3d> grid;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      double z = 0.0;
      if (i == 1 && j == 0) z = 2.0;
      if (i == 3 && j == 2) z = -1.5;
      grid.push_back(Point3d(i, j, z));
    }
  }
  return Brep::FromSurface(NurbsSurface::FromControlGrid(grid, 4, 4, 3, 3));
}

}  // namespace

D8_EXTRA_TEST(CapNakedLoopsSkewTopCornerThreeRaisedByOne) { CheckSkewCapOnCorner(3, 1.0, "skew top, corner 3 raised by 1"); }

D8_EXTRA_TEST(CapNakedLoopsSkewTopCornerOneRaisedByTwoAndAHalf) {
  CheckSkewCapOnCorner(1, 2.5, "skew top, corner 1 raised by 2.5");
}

D8_EXTRA_TEST(CapNakedLoopsFlatTopIsStillCappedPlanar) {
  Brep shell = PrismMissingTop(UnitSquare(), {1.0, 1.0, 1.0, 1.0});
  const int added = shell.CapNakedLoops();
  d8test::Check(added == 1, "flat top: CapNakedLoops() adds the one planar cap");
  const Counts c = CountTopology(shell);
  d8test::Check(c.vertices - c.edges + c.faces == 2 && c.edges_with_two_trims == 12, "flat top: Euler 2, all 12 edges shared by two faces");
  d8test::Check(shell.raw().IsValid() && shell.raw().IsSolid(), "flat top: IsValid() and IsSolid()");
  d8test::Check(std::fabs(shell.TessellateToClosedMesh(8, 8).Volume() - 1.0) < 1e-9, "flat top: volume is exactly 1");
}

D8_EXTRA_TEST(CapNakedLoopsCurvedRimTubeIsClosedByTwoCoonsCaps) {
  const Brep tube = Brep::ExtrudeFaceWelded(CurvedBoundarySheet(), 0, Vector3d(0, 0, 1.0), /*cap=*/false);
  const Counts before = CountTopology(tube);
  d8test::Check(before.vertices == 8 && before.edges == 12 && before.faces == 4 && before.edges_with_one_trim == 8,
                "curved-rim tube: 8 vertices, 12 edges, 4 walls, 8 naked rim edges");
  Brep probe = tube;
  d8test::Check(probe.CapPlanarHoles() == 0, "curved-rim tube: CapPlanarHoles() caps neither rim (curved edges)");

  Brep solid = tube;
  std::vector<Brep::SkippedCap> skipped;
  const int added = solid.CapNakedLoops(dino8::kernel::tolerance::kEdgeJoin, &skipped);
  d8test::Check(added == 2 && skipped.empty(), "curved-rim tube: CapNakedLoops() adds both caps and skips nothing");
  const Counts c = CountTopology(solid);
  d8test::Check(c.vertices == 8 && c.edges == 12 && c.faces == 6 && c.vertices - c.edges + c.faces == 2,
                "curved-rim tube: 8 / 12 / 6, Euler characteristic 2");
  d8test::Check(c.edges_with_two_trims == 12 && c.edges_with_one_trim == 0 && c.other_edges == 0,
                "curved-rim tube: every edge borders exactly two trims");
  d8test::Check(solid.raw().IsValid() && solid.raw().IsSolid(), "curved-rim tube: IsValid() and IsSolid()");
  const Mesh m = solid.TessellateToClosedMesh(32, 32);
  d8test::Check(m.IsClosedManifold(), "curved-rim tube: tessellates to a closed manifold");
  // Both caps are Coons patches of one rim and its (0,0,1) translate, each a graph over [0,3]^2: Cavalieri gives 9 * 1.
  d8test::Check(std::fabs(m.Volume() - 9.0) < 0.02, "curved-rim tube: volume equals the Cavalieri closed form 9 (within 0.02)");
}

D8_EXTRA_TEST(CapNakedLoopsFivefoldSkewTopIsStillReportedNotGuessed) {
  // Five rim edges: no 4-sided Coons patch exists, so the loop is left open and named, the Brep untouched.
  std::vector<Point3d> base;
  for (int k = 0; k < 5; ++k) {
    const double a = 2.0 * M_PI * k / 5.0;
    base.push_back(Point3d(std::cos(a), std::sin(a), 0.0));
  }
  Brep shell = PrismMissingTop(base, {1.0, 1.0, 1.0, 1.0, 1.7});
  const Counts before = CountTopology(shell);
  std::vector<Brep::SkippedCap> skipped;
  const int added = shell.CapNakedLoops(dino8::kernel::tolerance::kEdgeJoin, &skipped);
  d8test::Check(added == 0, "five-edge skew top: nothing is capped");
  d8test::Check(skipped.size() == 1 && skipped[0].reason == Brep::SkippedCap::Reason::NonPlanar && skipped[0].edge_indices.size() == 5,
                "five-edge skew top: the one skipped chain is reported NonPlanar with its 5 edges");
  const Counts after = CountTopology(shell);
  d8test::Check(after.vertices == before.vertices && after.edges == before.edges && after.faces == before.faces &&
                    after.edges_with_one_trim == 5,
                "five-edge skew top: topology unchanged, the 5 rim edges still naked");
}
