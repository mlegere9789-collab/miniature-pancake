// SubD::Symmetrize() and pinned corners (SubD::SetVertexCorner()).
//
// A pinned corner is a Corner-tagged vertex on exactly two crease edges. Symmetrize() mirrors crease edges and semi-sharp
// weights but built the mirrored faces with Unset tags, so the mirror image of a pinned corner came back an ordinary Crease
// vertex (and the pin was lost altogether on the face-by-face rebuild path used when a face lies in the mirror plane).
//
// Oracles are the limit rules, derived by hand and independent of the tag the code reports. The cage is the cube
// [0,10]^3 with the two edges (0,0,0)-(10,0,0) and (0,0,0)-(0,10,0) creased and the origin pinned:
//   pinned Corner          limit point = control point
//   unpinned Crease        limit = (a + 4 v + b) / 6, a = (10,0,0), b = (0,10,0) from v = (0,0,0): (10/6, 10/6, 0)
// Mirrored across x = 15 the image of the origin is (30,0,0) with crease neighbours (20,0,0) and (30,10,0), so an unpinned
// image has limit (20 + 120 + 30, 0 + 0 + 10, 0) / 6 = (170/6, 10/6, 0) = (28.3333, 1.6667, 0), a pinned one (30,0,0).
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
using dino8::kernel::Vector3d;

dino8::kernel::Mesh SzBox() {  // cube [0,10]^3, outward quads
  dino8::kernel::Mesh m;
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

SubD PinnedCube(bool pin) {
  SubD s = SubD::FromControlMesh(SzBox());
  s.SetCrease(Point3d(0, 0, 0), Point3d(10, 0, 0), true, 1e-6);
  s.SetCrease(Point3d(0, 0, 0), Point3d(0, 10, 0), true, 1e-6);
  if (pin) s.SetVertexCorner(Point3d(0, 0, 0), true, 1e-6);
  return s;
}

bool Near(const Point3d& a, double x, double y, double z, double tol) {
  return std::abs(a.x - x) < tol && std::abs(a.y - y) < tol && std::abs(a.z - z) < tol;
}

}  // namespace

D8_EXTRA_TEST(SubDSymmetrizeKeepsPinnedCornerOnBothHalves) {
  // The mirror plane x = 15 does not touch the cube: two separate bodies in one SubD (the plain, non-rebuilding path).
  const SubD pinned = PinnedCube(true);
  d8test::Check(pinned.VertexInfoAt(Point3d(0, 0, 0), 1e-6).kind == SubDVertexKind::Corner, "sanity: the source origin is pinned");
  const SubD both = pinned.Symmetrize(Vector3d(1, 0, 0), 15.0);
  const SubDVertexInfo orig = both.VertexInfoAt(Point3d(0, 0, 0), 1e-5);
  const SubDVertexInfo image = both.VertexInfoAt(Point3d(30, 0, 0), 1e-5);
  d8test::Check(orig.found && orig.kind == SubDVertexKind::Corner && Near(orig.limit_point, 0, 0, 0, 1e-6), "the original half keeps its pin: limit (0,0,0)");
  d8test::Check(image.found && image.crease_edge_count == 2, "the mirror image (30,0,0) sits on exactly two crease edges");
  d8test::Check(image.kind == SubDVertexKind::Corner, "the mirror image of a pinned corner is a Corner too");
  d8test::Check(image.limit_valid && Near(image.limit_point, 30, 0, 0, 1e-6), "...with limit point == control point (30,0,0)");

  // Control: an UNPINNED crease vertex mirrors to an ordinary Crease vertex with the hand-derived crease limit.
  const SubD plain = PinnedCube(false).Symmetrize(Vector3d(1, 0, 0), 15.0);
  const SubDVertexInfo plain_image = plain.VertexInfoAt(Point3d(30, 0, 0), 1e-5);
  d8test::Check(plain_image.found && plain_image.kind == SubDVertexKind::Crease && Near(plain_image.limit_point, 170.0 / 6.0, 10.0 / 6.0, 0, 1e-4),
                "control: without a pin the image is a Crease vertex with limit (170/6, 10/6, 0)");
}

D8_EXTRA_TEST(SubDSymmetrizeKeepsPinnedCornerWhenAFaceLiesInTheMirrorPlane) {
  // The plane x = 10 contains the cube's far face, which is dropped from both halves; the result is rebuilt face by face.
  const SubD two = PinnedCube(true).Symmetrize(Vector3d(1, 0, 0), 10.0);
  d8test::Check(two.FaceCount() == 10, "sanity: 6 + 6 faces minus the two in-plane faces = 10");
  const SubDVertexInfo orig = two.VertexInfoAt(Point3d(0, 0, 0), 1e-5);
  const SubDVertexInfo image = two.VertexInfoAt(Point3d(20, 0, 0), 1e-5);
  d8test::Check(orig.found && orig.kind == SubDVertexKind::Corner && Near(orig.limit_point, 0, 0, 0, 1e-6), "the original pin survives the rebuild: limit (0,0,0)");
  d8test::Check(image.found && image.kind == SubDVertexKind::Corner && Near(image.limit_point, 20, 0, 0, 1e-6), "the image (20,0,0) is a pinned Corner, limit (20,0,0)");
}

// BooleanToSubDCreased() (the fold Reflect uses for more than one selected SubD, called as BooleanToSubDCreased(other, Union, 0, 0, 0.0, 3.0,
// false)) rebuilds a fresh cage from the Boolean mesh and re-derives the crease EDGES from the operands, but a pin is a vertex tag and was
// dropped. Operand A: the pinned cube PinnedCube(true) (origin pinned); operand B: a second cube 20 units away, or overlapping, pinned the same
// way at its own origin by translation. Pinned corner: limit point == control point.
D8_EXTRA_TEST(SubDBooleanToSubDCreasedKeepsPinnedCornersOfBothOperands) {
  const auto cube_at = [](double ox, double oy, double oz, bool pin) {
    SubD s = SubD::FromControlMesh(SzBox());
    ON_Xform t = ON_Xform::TranslationTransformation(ON_3dVector(ox, oy, oz));
    SubD moved = s.Transform(t);
    moved.SetCrease(Point3d(ox, oy, oz), Point3d(ox + 10, oy, oz), true, 1e-6);
    moved.SetCrease(Point3d(ox, oy, oz), Point3d(ox, oy + 10, oz), true, 1e-6);
    if (pin) moved.SetVertexCorner(Point3d(ox, oy, oz), true, 1e-6);
    return moved;
  };
  // disjoint operands, both pinned
  {
    const SubD u = cube_at(0, 0, 0, true).BooleanToSubDCreased(cube_at(20, 0, 0, true), dino8::kernel::BooleanOp::Union, 0, 0, 0.0, 3.0, false);
    const SubDVertexInfo a = u.VertexInfoAt(Point3d(0, 0, 0), 1e-4);
    const SubDVertexInfo b = u.VertexInfoAt(Point3d(20, 0, 0), 1e-4);
    d8test::Check(a.found && a.crease_edge_count == 2, "disjoint union: operand A's pinned vertex is on exactly two crease edges");
    d8test::Check(a.kind == SubDVertexKind::Corner && Near(a.limit_point, 0, 0, 0, 1e-5), "...and stays a pinned Corner, limit (0,0,0)");
    d8test::Check(b.found && b.kind == SubDVertexKind::Corner && Near(b.limit_point, 20, 0, 0, 1e-5), "operand B's pinned vertex stays a Corner, limit (20,0,0)");
  }
  // overlapping operands (B [5,15]^3 is unpinned): A's origin lies outside B and must keep its pin
  {
    const SubD u = cube_at(0, 0, 0, true).BooleanToSubDCreased(cube_at(5, 5, 5, false), dino8::kernel::BooleanOp::Union, 0, 0, 0.0, 3.0, false);
    const SubDVertexInfo a = u.VertexInfoAt(Point3d(0, 0, 0), 1e-4);
    d8test::Check(a.found && a.kind == SubDVertexKind::Corner && Near(a.limit_point, 0, 0, 0, 1e-5), "overlapping union: A's pinned origin stays a Corner, limit (0,0,0)");
    const SubDVertexInfo c = u.VertexInfoAt(Point3d(5, 5, 5), 1e-4);
    d8test::Check(!c.found || c.kind != SubDVertexKind::Corner || c.crease_edge_count != 2, "control: B's unpinned vertex (5,5,5) does not become a two-crease pinned corner");
  }
}

// SymmetrizeRadial(): the same loss for the rotated images. Cage: the 2 x 2 quad quadrant [0,2]^2 with its rim vertex (2,1) pulled out to
// (2.2, 1) and pinned (it sits on the two naked rim edges to (2,0) and (2,2)). Rotated 4 times about z the images are
// (-1, 2.2), (-2.2, -1), (1, -2.2). Rim crease rule for the unpinned vertex: (a + 4 v + b)/6 with a = (2,0), b = (2,2):
// ((2 + 8.8 + 2)/6, (0 + 4 + 2)/6) = (2.13333, 1); rotated by 90 degrees (x,y) -> (-y,x): (-1, 2.13333).
D8_EXTRA_TEST(SubDSymmetrizeRadialKeepsPinnedCornerOnEveryRotatedImage) {
  const auto build = [](bool pin) {
    dino8::kernel::Mesh m;
    ON_Mesh& raw = m.raw();
    for (int j = 0; j < 3; ++j)
      for (int i = 0; i < 3; ++i) raw.m_V.Append(ON_3fPoint((i == 2 && j == 1) ? 2.2f : float(i), float(j), 0.0f));
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i) {
        ON_MeshFace f;
        f.vi[0] = j * 3 + i; f.vi[1] = j * 3 + i + 1; f.vi[2] = (j + 1) * 3 + i + 1; f.vi[3] = (j + 1) * 3 + i;
        raw.m_F.Append(f);
      }
    SubD s = SubD::FromControlMesh(m);
    if (pin) s.SetVertexCorner(Point3d(2.2, 1, 0), true, 1e-5);
    return s;
  };
  const SubD src = build(true);
  d8test::Check(src.VertexInfoAt(Point3d(2.2, 1, 0), 1e-5).kind == SubDVertexKind::Corner, "sanity: the source rim vertex is pinned");
  const SubD ring = src.SymmetrizeRadial(Point3d(0, 0, 0), Vector3d(0, 0, 1), 4);
  d8test::Check(ring.FaceCount() == 16, "sanity: 4 quads x 4 copies = 16 faces");
  const double pts[4][2] = {{2.2, 1}, {-1, 2.2}, {-2.2, -1}, {1, -2.2}};
  for (int k = 0; k < 4; ++k) {
    const SubDVertexInfo v = ring.VertexInfoAt(Point3d(pts[k][0], pts[k][1], 0), 1e-4);
    char what[160];
    std::snprintf(what, sizeof what, "rotation %d: the image (%g, %g) is a pinned Corner with limit == control point", k, pts[k][0], pts[k][1]);
    d8test::Check(v.found && v.crease_edge_count == 2 && v.kind == SubDVertexKind::Corner && Near(v.limit_point, pts[k][0], pts[k][1], 0, 1e-5), what);
  }
  const SubD plain = build(false).SymmetrizeRadial(Point3d(0, 0, 0), Vector3d(0, 0, 1), 4);
  const SubDVertexInfo image = plain.VertexInfoAt(Point3d(-1, 2.2, 0), 1e-4);
  d8test::Check(image.found && image.kind == SubDVertexKind::Crease && Near(image.limit_point, -1, 2.0 + 2.0 / 15.0, 0, 1e-4),
                "control: unpinned, the 90-degree image is a Crease vertex with limit (-1, 2.13333, 0)");
}
