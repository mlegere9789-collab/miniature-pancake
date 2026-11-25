// SubD::SplitByPlane(): a plane cut that stays SubD (first step of a topological SubD-to-SubD boolean).
//
// Oracles are closed forms of the cube [0,10]^3, independent of the code under test:
//  * A FULLY CREASED cube (every edge a crease, every vertex a corner) is exactly the solid cube at any subdivision level, so a cut at
//    z = 4 must give boxes [0,10]x[0,10]x[0,4] and [0,10]x[0,10]x[4,10]: volumes 400 / 600, cap area 100, surface areas 360 / 440,
//    each a valid closed manifold with Euler characteristic 2 (V - E + F = 8 - 12 + 6). The volume is read from the control net
//    after Subdivide(2): it stays exact only if the cut rim and the corners really are creased.
//  * A plane through the centre perpendicular to the body diagonal, x + y + z = 15, cuts the cube in a regular hexagon of side
//    10 / sqrt 2: area (3 sqrt 3 / 2) * 50 = 75 sqrt 3 = 129.9038; by point symmetry each half has volume 500.
#include <cmath>
#include <cstdio>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::SubDPlaneSplit;
using dino8::kernel::SubDVertexInfo;
using dino8::kernel::SubDVertexKind;
using dino8::kernel::Vector3d;

Mesh SpBox() {  // cube [0,10]^3, outward quads
  Mesh m;
  ON_Mesh& raw = m.raw();
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(10.0f * x, 10.0f * y, 10.0f * z));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

// Area of the mesh triangles whose three vertices all satisfy |n . p - offset| < tol.
double AreaInPlane(const Mesh& m, Vector3d n, double offset, double tol) {
  n.Unitize();
  const ON_Mesh& r = m.raw();
  double area = 0.0;
  for (int i = 0; i < r.m_F.Count(); ++i) {
    const ON_MeshFace& f = r.m_F[i];
    const int corners = f.IsQuad() ? 4 : 3;
    bool all = true;
    for (int k = 0; k < corners; ++k) {
      const ON_3dPoint p(r.m_V[f.vi[k]]);
      if (std::abs(n.x * p.x + n.y * p.y + n.z * p.z - offset) >= tol) all = false;
    }
    if (!all) continue;
    for (int k = 1; k + 1 < corners; ++k) {
      const ON_3dPoint a(r.m_V[f.vi[0]]), b(r.m_V[f.vi[k]]), c(r.m_V[f.vi[k + 1]]);
      area += 0.5 * ON_CrossProduct(b - a, c - a).Length();
    }
  }
  return area;
}

bool Near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

}  // namespace

D8_EXTRA_TEST(SubDSplitByPlaneFullyCreasedCubeGivesTwoExactBoxes) {
  SubD cube = SubD::FromControlMesh(SpBox());
  d8test::Check(cube.SetCreasesByDihedral(30.0) == 12, "sanity: all 12 cube edges creased");
  const SubDPlaneSplit cut = cube.SplitByPlane(Vector3d(0, 0, 1), 4.0);
  d8test::Check(cut.has_positive && cut.has_negative, "both halves exist");
  d8test::Check(cut.cut_vertices == 4 && cut.split_faces == 4 && cut.cap_loops == 1, "4 cut vertices (the 4 vertical edges), 4 split faces, 1 cap loop");

  struct Half { const SubD* s; double volume, area; const char* name; };
  const Half halves[2] = {{&cut.positive, 600.0, 440.0, "z>4 half"}, {&cut.negative, 400.0, 360.0, "z<4 half"}};
  for (const Half& h : halves) {
    char what[160];
    d8test::Check(h.s->IsValid(), (std::string(h.name) + ": valid SubD").c_str());
    d8test::Check(h.s->FaceCount() == 6 && h.s->VertexCount() == 8 && h.s->EdgeCount() == 12,
                  (std::string(h.name) + ": 6 faces, 8 vertices, 12 edges (a box; Euler 8 - 12 + 6 = 2)").c_str());
    const auto rep = h.s->Check();
    d8test::Check(rep.naked_edges == 0 && rep.IsManifoldSingleBody(), (std::string(h.name) + ": closed, manifold, one body").c_str());
    d8test::Check(h.s->CreaseEdgeCount() == 12, (std::string(h.name) + ": all 12 edges creased (the cut rim is a crease)").c_str());
    Mesh m0 = h.s->ToApproximateMesh();
    std::snprintf(what, sizeof what, "%s: control-net volume %.6f == %.0f", h.name, m0.Volume(), h.volume);
    d8test::Check(Near(m0.Volume(), h.volume, 1e-6), what);
    std::snprintf(what, sizeof what, "%s: surface area %.6f == %.0f", h.name, m0.Area(), h.area);
    d8test::Check(Near(m0.Area(), h.area, 1e-6), what);
    SubD refined = *h.s;
    refined.Subdivide(2);
    Mesh m2 = refined.ToApproximateMesh();
    std::snprintf(what, sizeof what, "%s: volume after Subdivide(2) is still exactly %.0f (measured %.6f): the rim and corners are sharp", h.name, h.volume, m2.Volume());
    d8test::Check(Near(m2.Volume(), h.volume, 1e-6), what);
  }
  d8test::Check(Near(AreaInPlane(cut.positive.ToApproximateMesh(), Vector3d(0, 0, 1), 4.0, 1e-9), 100.0, 1e-6), "the cap of the upper half has area 10 x 10 = 100");
  d8test::Check(Near(AreaInPlane(cut.negative.ToApproximateMesh(), Vector3d(0, 0, 1), 4.0, 1e-9), 100.0, 1e-6), "the cap of the lower half has area 100");
  d8test::Check(Near(cut.positive.ToApproximateMesh().Volume() + cut.negative.ToApproximateMesh().Volume(), 1000.0, 1e-6), "the halves add up to the cube, 1000");

  // negative control: without crease_cut the rim is smooth, so the refined half shrinks below the exact 600
  const SubDPlaneSplit soft = cube.SplitByPlane(Vector3d(0, 0, 1), 4.0, true, /*crease_cut=*/false);
  SubD refined = soft.positive;
  refined.Subdivide(2);
  d8test::Check(refined.ToApproximateMesh().Volume() < 600.0 - 1.0, "control: crease_cut=false leaves a smooth rim and the refined upper half is smaller than 600");
}

D8_EXTRA_TEST(SubDSplitByPlaneObliqueCutMakesAHexagonCap) {
  const SubD cube = SubD::FromControlMesh(SpBox());  // smooth cage: the check is topology, the cap and the cage volume
  const SubDPlaneSplit cut = cube.SplitByPlane(Vector3d(1, 1, 1), 15.0 / std::sqrt(3.0));  // x + y + z = 15 (the offset belongs to the unit normal)
  d8test::Check(cut.has_positive && cut.has_negative, "both halves exist");
  d8test::Check(cut.cut_vertices == 6 && cut.split_faces == 6 && cut.cap_loops == 1, "6 crossing edges, all 6 faces split, 1 cap loop");
  const SubD* halves[2] = {&cut.positive, &cut.negative};
  for (int i = 0; i < 2; ++i) {
    const SubD& h = *halves[i];
    d8test::Check(h.IsValid() && h.Check().naked_edges == 0 && h.Check().IsManifoldSingleBody(), "half: valid, closed, manifold, one body");
    d8test::Check(h.VertexCount() == 10 && h.EdgeCount() == 15 && h.FaceCount() == 7, "half: 4 + 6 vertices, 15 edges, 6 clipped faces + 1 hexagon cap (Euler 10 - 15 + 7 = 2)");
    const Mesh m = h.ToApproximateMesh();
    d8test::Check(Near(m.Volume(), 500.0, 1e-4), "half: cage volume 500 (point symmetry about the cube centre)");
    d8test::Check(Near(AreaInPlane(m, Vector3d(1, 1, 1), 15.0 / std::sqrt(3.0), 1e-6), 75.0 * std::sqrt(3.0), 1e-4), "half: the cap is a regular hexagon of area 75 sqrt 3 = 129.9038");
  }
}

D8_EXTRA_TEST(SubDSplitByPlaneKeepsPinnedCornersAndHandlesPlanesThatMissOrTouch) {
  SubD cube = SubD::FromControlMesh(SpBox());
  cube.SetCrease(Point3d(0, 0, 0), Point3d(10, 0, 0), true, 1e-6);
  cube.SetCrease(Point3d(0, 0, 0), Point3d(0, 10, 0), true, 1e-6);
  d8test::Check(cube.SetVertexCorner(Point3d(0, 0, 0), true, 1e-6), "sanity: the origin is pinned");
  const SubDPlaneSplit cut = cube.SplitByPlane(Vector3d(0, 0, 1), 4.0);
  const SubDVertexInfo o = cut.negative.VertexInfoAt(Point3d(0, 0, 0), 1e-6);
  d8test::Check(o.found && o.kind == SubDVertexKind::Corner && std::abs(o.limit_point.x) + std::abs(o.limit_point.y) + std::abs(o.limit_point.z) < 1e-6,
                "the lower half keeps the pinned origin as a Corner with limit (0,0,0)");
  const SubDPlaneSplit miss = SubD::FromControlMesh(SpBox()).SplitByPlane(Vector3d(0, 0, 1), 20.0);
  d8test::Check(!miss.has_positive && miss.has_negative && miss.negative.FaceCount() == 6 && miss.cut_vertices == 0, "a plane above the cube: no positive half, the negative half is the whole cube");
  const SubDPlaneSplit touch = SubD::FromControlMesh(SpBox()).SplitByPlane(Vector3d(0, 0, 1), 10.0);
  d8test::Check(!touch.has_positive && touch.has_negative && touch.negative.FaceCount() == 6 && touch.negative.Check().naked_edges == 0,
                "a plane through the top face only touches: no positive half, the negative half is the whole closed cube (the in-plane face stays with it)");
  bool threw_zero = false, threw_offset = false, threw_tol = false;
  try { (void)cube.SplitByPlane(Vector3d(0, 0, 0), 1.0); } catch (const std::invalid_argument&) { threw_zero = true; }
  try { (void)cube.SplitByPlane(Vector3d(0, 0, 1), std::nan("")); } catch (const std::invalid_argument&) { threw_offset = true; }
  try { (void)cube.SplitByPlane(Vector3d(0, 0, 1), 1.0, true, true, -1.0); } catch (const std::invalid_argument&) { threw_tol = true; }
  d8test::Check(threw_zero && threw_offset && threw_tol, "a zero normal, a NaN offset and a negative tolerance are rejected");
}
