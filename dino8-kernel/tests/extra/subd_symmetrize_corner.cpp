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
