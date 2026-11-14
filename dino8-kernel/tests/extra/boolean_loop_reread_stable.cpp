// Every Boolean re-reads the trims of the faces it walks into (u, v) loops (FaceBoundaryLoop() in boolean_general.cpp). A trim the
// previous operation built is ONE straight segment between two welded points (BuildLoop(): the edge is the 3D chord, the trim the
// straight (u, v) segment); the reader used to expand it again, start + midpoint for a segment straight in 3D and 24 samples of its
// (u, v) image on a curved face, so the two faces sharing a rim were read back at different densities (a plane doubles, a cylinder
// wall multiplies by 24), the reconcile step handed the denser side's points to the sparser one, and every later operation
// multiplied the point count of every rim it merely walked past. Drilling holes one after another into a slab: the first hole's
// rim 55 -> 1276 -> 12584 edges after the second and third drilling, the third drilling 3.7 s, the fourth 31 s, and the trim
// polygon of the top face then held 34113 vertices.
//
// The oracle is the contract itself: a loop is read back exactly as it was written, so an operation that does not cut a rim leaves
// its edges alone. The rim of a hole is a circle sampled to the intersection tolerance (about 50 edges for r = 0.5); drilling a
// hole 3 units away does not touch it.
#include <cmath>
#include <vector>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::MakeHole;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

// Edges of `b` lying on the circle of radius `r` about (cx, cy) in the plane z = `z` (both end points on it).
int RimEdges(const Brep& b, double cx, double cy, double r, double z) {
  const ON_Brep& br = b.raw();
  int n = 0;
  for (int ei = 0; ei < br.m_E.Count(); ++ei) {
    const ON_BrepEdge& e = br.m_E[ei];
    if (e.m_edge_index < 0) continue;
    const auto on = [&](const ON_3dPoint& p) { return std::fabs(p.z - z) < 1e-6 && std::fabs(std::hypot(p.x - cx, p.y - cy) - r) < 2e-3; };
    if (on(e.PointAtStart()) && on(e.PointAtEnd())) ++n;
  }
  return n;
}

}  // namespace

D8_EXTRA_TEST(BooleanRereadLeavesAnUntouchedRimAlone) {
  const double r = 0.5;
  const Vector3d down(0, 0, -1);
  Brep b = Brep::Box(0, 0, 0, 4, 16, 4);  // volume 256
  b = MakeHole(b, Point3d(2, 2, 4), down, r, 0.0, true);
  const int first = RimEdges(b, 2, 2, r, 4.0);
  d8test::Check(first >= 30 && first <= 150, "fixture: the first hole's top rim is one circle of 30..150 edges");

  // Three more holes, 3 units apart: none of them touches the first hole's rim or its wall. The loop stops at the first drilling that
  // multiplies the rim (each further drilling of such a result would only take longer).
  for (const double y : {5.0, 8.0, 11.0}) {
    b = MakeHole(b, Point3d(2, y, 4), down, r, 0.0, true);
    const int now = RimEdges(b, 2, 2, r, 4.0);
    d8test::Check(now <= first + 8,
                  "the first hole's top rim keeps its edges (at most a few corridor points added) when another hole is drilled elsewhere");
    if (now > first + 8) return;
  }
  for (const double y : {5.0, 8.0, 11.0}) {
    const int rim = RimEdges(b, 2, y, r, 4.0);
    d8test::Check(rim >= 20 && rim <= 150, "each later hole's top rim is one circle of 20..150 edges");
  }
  d8test::Check(b.raw().m_E.Count() < 1500, "four holes in a box: fewer than 1500 edges in all (not tens of thousands)");
  d8test::Check(b.raw().IsValid(), "the four-hole slab is a valid ON_Brep");

  // The result tessellates (the 34113-vertex trim polygon used to throw "trim_polygon must be simple") to the closed-form volume
  // 256 - 4 * pi * r^2 * 4 = 243.434, within the polygonal rim's own deficit and the open seam's slack.
  const dino8::kernel::Mesh m = b.TessellateToClosedMesh(32, 128);
  d8test::Check(std::fabs(m.Volume() - (256.0 - 4.0 * ON_PI * r * r * 4.0)) < 0.5,
                "the four-hole slab tessellates to 256 - 4 pi r^2 4 within 0.5");
}
