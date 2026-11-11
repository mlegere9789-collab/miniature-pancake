// A box with a cylinder through it whose two end caps lie exactly in the box's top and bottom faces ("a hole cut exactly through"),
// the cylinder built the way the app's Cylinder command builds it (ON_BrepCylinder: a NURBS wall and two planar caps that share the
// rim edge with the wall), with the wall's seam rotated to different angles around the axis.
//
// Before: the Difference was refused as "not consistently oriented" for 22 of 24 seam placements (swept in 15 degree steps); the
// engine had kept, for the cap face's hole, the surface intersector's own polyline of the rim circle (48 points on r = 2, with long
// chords where it skips samples), which shares no vertex with the polyline the wall keeps for the same circle (the 24 samples of the
// rim edge). Sewing the two only closes at 3e-3 of the diagonal, where the keyhole corridor is welded shut and the orientation breaks
// for almost every seam placement. Now of two coincident hole chains the one that shares the most vertices with the other solid's own
// face boundaries is kept (here the cap's own 24 rim samples - the very vertices of the wall's rim), and every placement seals.
//
// Oracle: the closed form box - pi r^2 h, and the plain facts ON_Brep::IsValid() / IsSolid() / 7 faces (4 box sides, 2 caps with a
// hole, 1 wall). The Intersection of the same pair is the cylinder: pi r^2 h.
#include <cmath>
#include <string>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineGeneralSealed;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::SealedGeneralBoolean;

// The app's Cylinder: ON_BrepCylinder of a circle in a plane whose x axis is rotated by `theta_deg` (that puts the wall seam there).
Brep AppCylinder(double theta_deg, double cx, double cy, double r, double length, double z0) {
  const double t = theta_deg * ON_PI / 180.0;
  ON_Plane pl;
  pl.origin = ON_3dPoint(cx, cy, z0);
  pl.xaxis = ON_3dVector(std::cos(t), std::sin(t), 0);
  pl.yaxis = ON_3dVector(-std::sin(t), std::cos(t), 0);
  pl.zaxis = ON_3dVector(0, 0, 1);
  pl.UpdateEquation();
  ON_Cylinder cyl(ON_Circle(pl, r), length);
  ON_Brep* b = ON_BrepCylinder(cyl, true, true);
  Brep out;
  out.raw() = *b;
  delete b;
  return out;
}

void CheckSeals(const std::string& label, const Brep& a, const Brep& b, BooleanOp op, double expect, int expect_faces) {
  SealedGeneralBoolean s;
  std::string refused;
  bool sealed = false;
  try {
    s = BooleanCombineGeneralSealed(a, b, op);
    sealed = true;
  } catch (const std::exception& e) {
    refused = std::string(e.what()).substr(0, 120);
  }
  d8test::Check(sealed, (label + ": seals (no refusal" + (sealed ? "" : "; refused: " + refused) + ")").c_str());
  if (!sealed) return;
  d8test::Check(s.brep.raw().IsValid() && s.brep.raw().IsSolid(), (label + ": ON_Brep::IsValid() and IsSolid()").c_str());
  const double err = std::fabs(s.volume - expect) / expect;
  d8test::Check(err < 0.002, (label + ": exact volume " + std::to_string(s.volume) + " matches the closed form " + std::to_string(expect) + " (rel err " + std::to_string(err) + ")").c_str());
  if (expect_faces > 0) d8test::Check(s.brep.FaceCount() == expect_faces, (label + ": " + std::to_string(s.brep.FaceCount()) + " faces == " + std::to_string(expect_faces)).c_str());
}

}  // namespace

D8_EXTRA_TEST(FlushThroughHoleDifferenceSealsAtEverySeamPlacement) {
  const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
  const double expect = 1000.0 - ON_PI * 2.0 * 2.0 * 10.0;
  for (const double theta : {0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0}) {
    const Brep cyl = AppCylinder(theta, 5, 5, 2.0, 10.0, 0);
    CheckSeals("box - flush through cylinder, seam at " + std::to_string(static_cast<int>(theta)) + " deg", box, cyl, BooleanOp::Difference, expect, 7);
  }
}

D8_EXTRA_TEST(FlushThroughCylinderUnionAndIntersectionAtSeveralSeamPlacements) {
  const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
  for (const double theta : {0.0, 135.0, 270.0}) {
    const Brep cyl = AppCylinder(theta, 5, 5, 2.0, 10.0, 0);
    const std::string l = "box / flush through cylinder, seam at " + std::to_string(static_cast<int>(theta)) + " deg";
    CheckSeals(l + " Union", box, cyl, BooleanOp::Union, 1000.0, -1);
    CheckSeals(l + " Intersection", box, cyl, BooleanOp::Intersection, ON_PI * 2.0 * 2.0 * 10.0, -1);
  }
}

// A blind cylinder with only its bottom cap on the box's bottom face (the other end inside the box): a flush pocket from below.
D8_EXTRA_TEST(FlushBlindCylinderDifferenceSeals) {
  const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
  for (const double theta : {0.0, 90.0, 225.0}) {
    const Brep cyl = AppCylinder(theta, 5, 5, 2.0, 6.0, 0);
    CheckSeals("box - blind cylinder flush at the bottom, seam at " + std::to_string(static_cast<int>(theta)) + " deg", box, cyl, BooleanOp::Difference,
               1000.0 - ON_PI * 2.0 * 2.0 * 6.0, -1);
  }
}
