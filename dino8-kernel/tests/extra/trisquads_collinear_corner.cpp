// Mesh::TrisToQuads on a real boolean result: no merged quad may have a straight (180 degree) corner.
//
// BooleanToSubDCreased() hands the Manifold boolean of two operands to TrisToQuads() and builds a SubD cage from the result. The
// boolean leaves vertices lying on the straight edges of the cut faces; merging two triangles across a diagonal that ends at such a
// vertex gives a "quad" with a 180 degree corner, which is a triangle with an extra vertex on one side and a degenerate valence-2
// corner in the cage. Before commit 9e13b8d0 TrisToQuads accepted those (any non-negative turn passed); it now refuses them, so the
// two triangles stay triangles. On the mixed SubD + Brep scenario this costs exactly the 8 degenerate merges: the number of proper
// quads is the same (342 in the app's scenario, both versions), the face total goes from 370 to 378.
//
// The oracle is geometric and independent of the ranking: a quad's corner angles from the vertex positions, and the invariants
// TrisToQuads promises (a pure face-list rewrite: same vertices, same area, same enclosed volume, still closed).
#include <cmath>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombine;
using dino8::kernel::BooleanOp;
using dino8::kernel::Mesh;
using dino8::kernel::SubD;

Mesh CqBox(double x0, double y0, double z0, double x1, double y1, double z1) {
  Mesh m;
  ON_Mesh& raw = m.raw();
  const double xs[2] = {x0, x1}, ys[2] = {y0, y1}, zs[2] = {z0, z1};
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(float(xs[x]), float(ys[y]), float(zs[z])));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

// The box as the app's Brep mesher produces it: 8 corners + 6 face centres = 14 vertices, each face a fan of 4 triangles (24 in all).
Mesh CqCenteredBox(double x0, double y0, double z0, double x1, double y1, double z1) {
  Mesh m = CqBox(x0, y0, z0, x1, y1, z1);
  ON_Mesh& raw = m.raw();
  const ON_Mesh quads = raw;
  raw.m_F.SetCount(0);
  for (int i = 0; i < quads.m_F.Count(); ++i) {
    const ON_MeshFace& q = quads.m_F[i];
    ON_3dPoint c(0, 0, 0);
    for (int k = 0; k < 4; ++k) c += ON_3dPoint(quads.m_V[q.vi[k]]) / 4.0;
    const int ci = raw.m_V.Count();
    raw.m_V.Append(ON_3fPoint(float(c.x), float(c.y), float(c.z)));
    for (int k = 0; k < 4; ++k) {
      ON_MeshFace t;
      t.vi[0] = q.vi[k]; t.vi[1] = q.vi[(k + 1) % 4]; t.vi[2] = ci; t.vi[3] = ci;
      raw.m_F.Append(t);
    }
  }
  return m;
}

struct QuadCensus {
  int quads = 0, tris = 0, straight_corner_quads = 0;
  double min_corner_deg = 180.0;
};

QuadCensus Census(const Mesh& m) {
  QuadCensus c;
  const ON_Mesh& r = m.raw();
  for (int i = 0; i < r.m_F.Count(); ++i) {
    const ON_MeshFace& f = r.m_F[i];
    if (f.vi[2] == f.vi[3]) { ++c.tris; continue; }
    ++c.quads;
    for (int k = 0; k < 4; ++k) {
      const ON_3dPoint p0 = r.m_V[f.vi[k]];
      const ON_3dVector a = ON_3dPoint(r.m_V[f.vi[(k + 1) % 4]]) - p0;
      const ON_3dVector b = ON_3dPoint(r.m_V[f.vi[(k + 3) % 4]]) - p0;
      const double ang = std::acos(std::max(-1.0, std::min(1.0, (a * b) / (a.Length() * b.Length())))) * 180.0 / ON_PI;
      if (ang > 179.0) { ++c.straight_corner_quads; break; }
      c.min_corner_deg = std::min(c.min_corner_deg, ang);
    }
  }
  return c;
}

}  // namespace

D8_EXTRA_TEST(TrisToQuadsOnABooleanResultLeavesNoStraightCornerQuad) {
  // The scenario of the app's mixed SubD + Brep union: a 3-round-refined cube [0,2]^3 (as triangles) united with the box [1,3]^3
  // (as the app's mesher gives it: 14 vertices, 24 triangles); the boolean result is the mesh TrisToQuads sees, with dihedral threshold 3 degrees.
  SubD cube = SubD::FromControlMesh(CqBox(0, 0, 0, 2, 2, 2));
  cube.Subdivide(3);
  Mesh a = cube.ToApproximateMesh();
  a.raw().ConvertQuadsToTriangles();
  Mesh b = CqCenteredBox(1, 1, 1, 3, 3, 3);
  const Mesh joined = BooleanCombine(a, b, BooleanOp::Union);
  d8test::Check(joined.IsClosedManifold() && joined.FaceCount() > 500, "oracle sanity: the boolean result is a closed triangle mesh of several hundred faces");
  const double area = joined.Area(), volume = joined.Volume();
  Mesh q = joined;
  const int merged = q.TrisToQuads(3.0);
  const QuadCensus c = Census(q);
  d8test::Check(merged > 0 && c.quads == merged, "triangles were merged into quads (the count returned matches the quads present)");
  d8test::Check(c.straight_corner_quads == 0, "no merged quad has a 180 degree corner (each would be a triangle with an extra vertex on one side)");
  d8test::Check(c.min_corner_deg > 20.0, "every quad corner is at least 20 degrees");
  d8test::Check(q.FaceCount() == joined.FaceCount() - merged, "faces = triangles - merges (each merge replaces two triangles by one quad)");
  d8test::Check(q.VertexCount() == joined.VertexCount() && std::abs(q.Area() - area) < 1e-9 * std::max(1.0, area) && std::abs(q.Volume() - volume) < 1e-9 * std::max(1.0, volume),
                "TrisToQuads only rewrites the face list: same vertices, same area, same enclosed volume");
}
