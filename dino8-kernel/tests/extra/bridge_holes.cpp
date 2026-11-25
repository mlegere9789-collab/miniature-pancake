// BridgeHolesIntoOuter used to MOVE every hole into its result list and, when it could not bridge any of them, return with the
// caller's `holes` left as empty husks: the face was then built without the opening (a Difference left 60 unpaired edges on a
// box with one counterbore). These pin the contract "an unbridgeable hole stays a hole" on Difference results, with exact
// B-rep volumes (Brep::VolumeOfSealedSolid(), the divergence theorem) against closed forms.
#include <cmath>
#include <vector>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::ExtrudeProfileWithHoles;
using dino8::kernel::MakeCounterboreHole;
using dino8::kernel::MakeCounterboreHoles;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

NurbsCurve BhCircle(const ON_3dPoint& c, double r) {
  const ON_Circle circle(ON_Plane(c, ON_3dVector(0, 0, 1)), r);
  ON_NurbsCurve n;
  circle.GetNurbForm(n);
  NurbsCurve k;
  k.raw() = n;
  return k;
}

int NakedEdges(const Brep& b) {
  int n = 0;
  for (int i = 0; i < b.raw().m_E.Count(); ++i) {
    if (b.raw().m_E[i].m_edge_index >= 0 && b.raw().m_E[i].TrimCount() != 2) ++n;
  }
  return n;
}

}  // namespace

// One counterbore (bore r=0.3 to depth 3, counterbore r=0.6 depth 1) in a 10x10x4 slab: V = 400 - pi*0.3^2*2 - pi*0.6^2*1.
D8_EXTRA_TEST(BridgeHolesCounterboreOnSlabIsSealedWithExactVolume) {
  const Brep slab = Brep::Box(0, 0, 0, 10, 10, 4);
  const Brep drilled = MakeCounterboreHole(slab, Point3d(2, 2, 4), Vector3d(0, 0, -1), 0.3, 3.0, false, 0.6, 1.0);
  d8test::Check(NakedEdges(drilled) == 0, "counterbore on a slab: every edge is shared by exactly two faces (no hole was lost)");
  d8test::Check(drilled.raw().IsValid() && drilled.raw().IsSolid(), "counterbore on a slab: IsValid and IsSolid");
  if (!(drilled.raw().IsValid() && drilled.raw().IsSolid())) return;
  const double expect = 400.0 - ON_PI * 0.09 * 2.0 - ON_PI * 0.36 * 1.0;
  d8test::Check(std::fabs(drilled.VolumeOfSealedSolid() - expect) < 0.05,
                "counterbore on a slab: exact B-rep volume matches 400 - 2 pi 0.09 - pi 0.36 to 0.05");
}

// Two counterbores cut in one pass: V = 400 - 2 (pi*0.3^2*2 + pi*0.6^2*1).
D8_EXTRA_TEST(BridgeHolesTwoCounterboresBatchIsSealedWithExactVolume) {
  const Brep slab = Brep::Box(0, 0, 0, 10, 10, 4);
  const Brep drilled = MakeCounterboreHoles(slab, {Point3d(2, 2, 4), Point3d(8, 8, 4)}, Vector3d(0, 0, -1), 0.3, 3.0, false, 0.6, 1.0);
  d8test::Check(NakedEdges(drilled) == 0, "two counterbores on a slab: every edge is shared by exactly two faces");
  d8test::Check(drilled.raw().IsValid() && drilled.raw().IsSolid(), "two counterbores on a slab: IsValid and IsSolid");
  if (!(drilled.raw().IsValid() && drilled.raw().IsSolid())) return;
  const double expect = 400.0 - 2.0 * (ON_PI * 0.09 * 2.0 + ON_PI * 0.36 * 1.0);
  d8test::Check(std::fabs(drilled.VolumeOfSealedSolid() - expect) < 0.05,
                "two counterbores on a slab: exact B-rep volume matches the closed form to 0.05");
}

// A disk (r=1, height 1) with two holes r=0.2 cut in one pass has outer wall, two caps and one wall per hole: 5 faces. Cutting
// the holes one after the other re-fragmented a cap that already carried the first hole and left 7 faces and thousands of
// unpaired edges.
D8_EXTRA_TEST(BridgeHolesExtrudeTwoHolesIsOnePassFiveFaces) {
  const Brep solid = ExtrudeProfileWithHoles(BhCircle(ON_3dPoint(0, 0, 0), 1.0),
                                             {BhCircle(ON_3dPoint(-0.4, 0, 0), 0.2), BhCircle(ON_3dPoint(0.4, 0, 0), 0.2)},
                                             Vector3d(0, 0, 1.0));
  d8test::Check(solid.raw().IsValid(), "ExtrudeProfileWithHoles two holes: IsValid");
  d8test::Check(solid.raw().m_F.Count() == 5, "ExtrudeProfileWithHoles two holes: outer wall + two caps + one wall per hole = 5 faces");
  d8test::Check(NakedEdges(solid) < 100, "ExtrudeProfileWithHoles two holes: far fewer than the thousands of unpaired edges of the sequential cut");
}
