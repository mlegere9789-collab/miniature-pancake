// SubD::VertexInfoAt() / SubD::SetVertexCorner(): reading back (and pinning) how a control-net vertex is classified.
//
// Oracles are the Catmull-Clark limit rules, derived by hand and independent of the tag the code reports:
//   * corner rule:                 limit point = control point;
//   * boundary/crease rule:        limit = (a + 4 v + b) / 6 along the two crease neighbours a, b;
//   * smooth valence-n vertex:     limit = (n^2 v + 4 sum(edge neighbours) + sum(face-opposite vertices)) / (n (n + 5))
//                                  (Halstead et al.; for the cube n = 3 this is exactly 0.5 v).
// A reported kind is only trusted when the reported limit point agrees with the rule of that kind.
#include <cmath>
#include <cstdio>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::SubDVertexInfo;
using dino8::kernel::SubDVertexKind;

dino8::kernel::Mesh ViBoxMesh(double h) {  // cube [-h,h]^3, outward quads
  dino8::kernel::Mesh m;
  ON_Mesh& raw = m.raw();
  const double s[2] = {-h, h};
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(float(s[x]), float(s[y]), float(s[z])));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

// 3 x 3 quads, 4 x 4 vertices at (i, j, z_ij), normal +z. z only varies on the bottom row (j = 0).
dino8::kernel::Mesh ViSheet(const double bottom_z[4]) {
  dino8::kernel::Mesh m;
  ON_Mesh& raw = m.raw();
  for (int j = 0; j < 4; ++j)
    for (int i = 0; i < 4; ++i) raw.m_V.Append(ON_3fPoint(float(i), float(j), float(j == 0 ? bottom_z[i] : 0.0)));
  for (int j = 0; j < 3; ++j) {
    for (int i = 0; i < 3; ++i) {
      ON_MeshFace f;
      f.vi[0] = j * 4 + i; f.vi[1] = j * 4 + i + 1; f.vi[2] = (j + 1) * 4 + i + 1; f.vi[3] = (j + 1) * 4 + i;
      raw.m_F.Append(f);
    }
  }
  return m;
}

double Dist(const Point3d& a, const Point3d& b) { return a.DistanceTo(b); }

}  // namespace

D8_EXTRA_TEST(SubDVertexInfoCreasedBoxCornersArePinnedAtTheirControlPoints) {
  SubD box = SubD::FromControlMesh(ViBoxMesh(1.0));
  box.SetCreasesByDihedral(30.0);
  const SubDVertexInfo v = box.VertexInfoAt(Point3d(1, 1, 1), 1e-6);
  d8test::Check(v.found, "the box corner (1,1,1) is found");
  d8test::Check(v.kind == SubDVertexKind::Corner, "three crease edges meet: the vertex is a Corner");
  d8test::Check(v.valence == 3 && v.face_count == 3 && v.crease_edge_count == 3, "valence 3, 3 faces, 3 crease edges");
  d8test::Check(v.semi_sharp_edge_count == 0 && v.max_semi_sharpness == 0.0, "no semi-sharp edge");
  d8test::Check(v.limit_valid && Dist(v.limit_point, v.control_point) < 1e-9, "corner rule: the limit point is the control point");
  d8test::Check(!box.VertexInfoAt(Point3d(5, 5, 5), 1e-6).found, "a point far from every vertex is not found");
}

D8_EXTRA_TEST(SubDVertexInfoSmoothCubeVertexMatchesTheHalsteadLimitRule) {
  const SubD cube = SubD::FromControlMesh(ViBoxMesh(1.0));
  const SubDVertexInfo v = cube.VertexInfoAt(Point3d(1, 1, 1), 1e-6);
  d8test::Check(v.found && v.kind == SubDVertexKind::Smooth && v.crease_edge_count == 0 && v.valence == 3, "an uncreased cube vertex is Smooth, valence 3, no crease");
  // (9 v + 4 * (1,1,1) + (-1,-1,-1)) / 24 = 0.5 * (1,1,1)
  d8test::Check(v.limit_valid && Dist(v.limit_point, Point3d(0.5, 0.5, 0.5)) < 1e-9, "smooth valence-3 rule gives the limit point (0.5, 0.5, 0.5)");
}

D8_EXTRA_TEST(SubDVertexInfoSheetBoundaryIsCreaseWithTheCubicBSplineLimit) {
  const double bz[4] = {0.0, 0.3, 0.1, 0.0};
  const SubD sheet = SubD::FromControlMesh(ViSheet(bz));
  const SubDVertexInfo b = sheet.VertexInfoAt(Point3d(1, 0, 0.3), 1e-5);
  d8test::Check(b.found && b.kind == SubDVertexKind::Crease, "a non-corner boundary vertex is a Crease vertex");
  d8test::Check(b.valence == 3 && b.face_count == 2 && b.crease_edge_count == 2, "valence 3, 2 faces, 2 naked (crease) edges");
  const double expect_z = (0.0 + 4.0 * 0.3 + 0.1) / 6.0;  // (a + 4 v + b) / 6 along the boundary
  d8test::Check(b.limit_valid && std::abs(b.limit_point.z - expect_z) < 1e-6 && std::abs(b.limit_point.y) < 1e-6,
                "boundary rule: limit z = (0 + 4*0.3 + 0.1)/6 and the point stays on the boundary line");
  const SubDVertexInfo c = sheet.VertexInfoAt(Point3d(0, 0, 0), 1e-5);
  d8test::Check(c.found && c.face_count == 1 && c.crease_edge_count == 2 && c.kind == SubDVertexKind::Crease,
                "the sheet corner has 1 face and 2 naked edges and is a Crease vertex (not pinned)");
  // crease rule at the corner: (a + 4 v + b) / 6 with a = (1,0,0.3), v = (0,0,0), b = (0,1,0)
  d8test::Check(c.limit_valid && Dist(c.limit_point, Point3d(1.0 / 6.0, 1.0 / 6.0, 0.3 / 6.0)) < 1e-6,
                "its limit point is (a + 4 v + b)/6 = (1/6, 1/6, 0.05): the open sheet shrinks at its corners");
  const SubDVertexInfo in = sheet.VertexInfoAt(Point3d(1, 1, 0), 1e-5);
  d8test::Check(in.found && in.kind == SubDVertexKind::Smooth && in.valence == 4 && in.crease_edge_count == 0, "an interior grid vertex is Smooth, valence 4");
}

D8_EXTRA_TEST(SubDVertexInfoReportsDartAndSemiSharpEdges) {
  SubD box = SubD::FromControlMesh(ViBoxMesh(1.0));
  d8test::Check(box.SetCrease(Point3d(1, 1, -1), Point3d(1, 1, 1), true, 1e-6), "creased one box edge");
  const SubDVertexInfo d = box.VertexInfoAt(Point3d(1, 1, 1), 1e-6);
  d8test::Check(d.found && d.crease_edge_count == 1 && d.kind == SubDVertexKind::Dart, "one crease edge ending at a smooth vertex: Dart");
  SubD semi = SubD::FromControlMesh(ViBoxMesh(1.0));
  d8test::Check(semi.SetEdgeSharpness(Point3d(1, 1, -1), Point3d(1, 1, 1), 1.0, 2.0, 1e-6), "set per-end sharpness 1.0 at z=-1, 2.0 at z=+1");
  const SubDVertexInfo top = semi.VertexInfoAt(Point3d(1, 1, 1), 1e-6);
  const SubDVertexInfo bot = semi.VertexInfoAt(Point3d(1, 1, -1), 1e-6);
  d8test::Check(top.semi_sharp_edge_count == 1 && std::abs(top.max_semi_sharpness - 2.0) < 1e-12, "the +z end sees one semi-sharp edge of sharpness 2");
  d8test::Check(bot.semi_sharp_edge_count == 1 && std::abs(bot.max_semi_sharpness - 1.0) < 1e-12, "the -z end sees the same edge with its own sharpness 1");
  d8test::Check(top.kind == SubDVertexKind::Smooth && top.crease_edge_count == 0, "a semi-sharp edge does not change the vertex tag");
}

D8_EXTRA_TEST(SubDSetVertexCornerPinsAndReleasesACreaseVertex) {
  const double bz[4] = {0.0, 0.3, 0.1, 0.0};
  SubD sheet = SubD::FromControlMesh(ViSheet(bz));
  const Point3d p(1, 0, 0.3);
  const double crease_z = (0.0 + 4.0 * 0.3 + 0.1) / 6.0;
  d8test::Check(sheet.SetVertexCorner(p, true, 1e-5), "pinning the boundary Crease vertex succeeds");
  SubDVertexInfo v = sheet.VertexInfoAt(p, 1e-5);
  d8test::Check(v.kind == SubDVertexKind::Corner && Dist(v.limit_point, v.control_point) < 1e-6, "now a Corner: limit point == control point (z = 0.3)");
  d8test::Check(!sheet.SetVertexCorner(p, true, 1e-5), "pinning an already pinned vertex is a no-op (false)");
  // The pin changes the neighbours' boundary limit too: vertex (2,0) = (v_1 + 4 v_2 + v_3)/6 is unaffected (tag is per vertex),
  // but the pinned vertex splits the crease chain: a refined cage keeps z = 0.3 exactly at that location.
  SubD refined = sheet;
  refined.Subdivide(1);
  const SubDVertexInfo kept = refined.VertexInfoAt(p, 1e-5);
  d8test::Check(kept.found && std::abs(kept.control_point.z - 0.3) < 1e-6, "after one Catmull-Clark round the pinned vertex still sits at z = 0.3 (a Crease vertex would have moved to the B-spline point)");
  d8test::Check(sheet.SetVertexCorner(p, false, 1e-5), "releasing it succeeds");
  v = sheet.VertexInfoAt(p, 1e-5);
  d8test::Check(v.kind == SubDVertexKind::Crease && std::abs(v.limit_point.z - crease_z) < 1e-6, "back to Crease: limit z returns to (0 + 4*0.3 + 0.1)/6");
  d8test::Check(!sheet.SetVertexCorner(Point3d(1, 1, 0), true, 1e-5), "a smooth interior vertex cannot be pinned (false)");
  d8test::Check(!sheet.SetVertexCorner(Point3d(9, 9, 9), true, 1e-5), "an unknown point returns false");
  // The sheet corner: pinning it holds the corner of the open sheet in place (limit == control), unlike the Crease rule above.
  d8test::Check(sheet.SetVertexCorner(Point3d(0, 0, 0), true, 1e-5), "pinning the sheet corner succeeds");
  const SubDVertexInfo pc = sheet.VertexInfoAt(Point3d(0, 0, 0), 1e-5);
  d8test::Check(pc.kind == SubDVertexKind::Corner && Dist(pc.limit_point, Point3d(0, 0, 0)) < 1e-6, "the pinned sheet corner's limit point is the control point (0,0,0)");
  SubD box = SubD::FromControlMesh(ViBoxMesh(1.0));
  box.SetCreasesByDihedral(30.0);
  d8test::Check(!box.SetVertexCorner(Point3d(1, 1, 1), false, 1e-6), "a corner at 3 crease edges cannot be released (false)");
}
