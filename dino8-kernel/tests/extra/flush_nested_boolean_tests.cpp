// General Booleans whose operands meet in coplanar FLUSH end caps while one solid lies inside the other: a cylinder inside a bigger
// cylinder or a box with both end planes shared, a cylinder through a box with its ends on the box's faces, two coaxial cylinders of
// nearby radii. All of them used to be refused ("an edge is claimed by 3 or more fragment loops") or sealed wrongly.
//
// Two defects, both in FragmentFaces (boolean_general.cpp):
//   1. A closed intersection chain that runs along the face's own trim was never dropped (it is only a rim arc when OPEN: a closed one
//      may be a boss footprint's keyhole outline). A cylinder wall's whole rim circle, re-found by the other solid's flush cap, is a
//      closed chain along its own trim that divides nothing; kept, it cut the wall into slivers between the rim polyline and the
//      chain polyline.
//   2. The cap-cap rim and the wall-plane section are one circle sampled twice (a 128-gon and the surface intersector's own coarser
//      polyline). Both were kept as hole outlines of the same cap, so the cap carried the same hole twice.
//
// Every expected number is a closed form: a coaxial/nested cylinder volume pi r^2 h, an annulus pi (R^2 - r^2) h, a box 1000. The
// caps are 128-gons (inscribed), so volumes are compared to 0.2% (measured errors are <= 4e-5 relative).
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

ON_Plane FrameFromAxis(const ON_3dPoint& origin, ON_3dVector axis) {
  axis.Unitize();
  ON_3dVector seed = std::fabs(axis.z) < 0.9 ? ON_3dVector(0, 0, 1) : ON_3dVector(1, 0, 0);
  ON_3dVector x = ON_CrossProduct(seed, axis);
  x.Unitize();
  ON_3dVector y = ON_CrossProduct(axis, x);
  y.Unitize();
  ON_Plane f;
  f.origin = origin;
  f.xaxis = x;
  f.yaxis = y;
  f.zaxis = axis;
  f.UpdateEquation();
  return f;
}

Brep::PlanarFace DiskCap(const ON_Plane& frame, double h, double r, bool flip, int n = 128) {
  Brep::PlanarFace pf;
  const ON_3dPoint c = frame.origin + frame.zaxis * h;
  pf.plane = ON_Plane(c, flip ? -frame.zaxis : frame.zaxis);
  for (int i = 0; i < n; ++i) {
    const double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
    pf.loop.push_back(c + frame.xaxis * (r * std::cos(a)) + frame.yaxis * (r * std::sin(a)));
  }
  pf.notch_begin = 0;
  pf.notch_count = static_cast<int>(pf.loop.size());
  return pf;
}

Brep Cylinder(const ON_3dPoint& base, double r, double length) {
  Brep::CylindricalFace cf;
  cf.frame = FrameFromAxis(base, ON_3dVector(0, 0, 1));
  cf.radius = r;
  cf.angle = 2.0 * ON_PI;
  cf.length = length;
  return Brep::FromMixedFaces({DiskCap(cf.frame, 0.0, r, true), DiskCap(cf.frame, length, r, false)}, {cf});
}

const char* OpName(BooleanOp op) { return op == BooleanOp::Union ? "Union" : op == BooleanOp::Intersection ? "Intersection" : "Difference"; }

// Seals, is a valid closed solid, and its exact volume matches the closed form; a body `expect == 0` must come back EMPTY.
void CheckOp(const std::string& label, const Brep& a, const Brep& b, BooleanOp op, double expect, int expect_faces = -1) {
  const std::string l = label + " " + OpName(op);
  SealedGeneralBoolean s;
  std::string refused;
  bool sealed = false;
  try {
    s = BooleanCombineGeneralSealed(a, b, op);
    sealed = true;
  } catch (const std::exception& e) {
    refused = std::string(e.what()).substr(0, 140);
  }
  d8test::Check(sealed, (l + ": seals (no refusal" + (sealed ? "" : "; refused: " + refused) + ")").c_str());
  if (!sealed) return;
  if (expect == 0.0) {
    d8test::Check(s.brep.FaceCount() == 0 && s.volume == 0.0, (l + ": the empty set (0 faces, volume 0)").c_str());
    return;
  }
  d8test::Check(s.brep.raw().IsValid(), (l + ": ON_Brep::IsValid()").c_str());
  d8test::Check(s.brep.raw().IsSolid(), (l + ": ON_Brep::IsSolid()").c_str());
  const double err = std::fabs(s.volume - expect) / expect;
  d8test::Check(err < 0.002, (l + ": exact volume " + std::to_string(s.volume) + " matches the closed form " + std::to_string(expect) + " (rel err " + std::to_string(err) + ")").c_str());
  if (expect_faces >= 0) d8test::Check(s.brep.FaceCount() == expect_faces, (l + ": face count " + std::to_string(s.brep.FaceCount()) + " == " + std::to_string(expect_faces)).c_str());
}

}  // namespace

// A cylinder wholly inside a bigger one, the same z range (both end planes shared), axes 1 apart. Before: every op refused ("an
// edge is claimed by 3 or more fragment loops"), small - big sealed wrongly (3 / 9 unpaired edges).
D8_EXTRA_TEST(FlushNestedCylindersAllOpsMatchClosedForms) {
  const double pi = ON_PI, h = 4.0;
  for (const double rs : {0.9, 0.5}) {
    const Brep big = Cylinder(ON_3dPoint(0, 0, 0), 2.0, h);
    const Brep small = Cylinder(ON_3dPoint(1, 0, 0), rs, h);
    const std::string l = "nested flush cylinders R=2 r=" + std::to_string(rs).substr(0, 3);
    CheckOp(l, big, small, BooleanOp::Union, pi * 4.0 * h);
    CheckOp(l, big, small, BooleanOp::Intersection, pi * rs * rs * h);
    CheckOp(l + " (big - small)", big, small, BooleanOp::Difference, pi * (4.0 - rs * rs) * h);
    CheckOp(l + " (small - big)", small, big, BooleanOp::Difference, 0.0);
  }
}

// Coaxial cylinders 2% apart in radius, both end planes shared: the two cap circles are distinct curves a real feature apart, far
// more than any sampling error, so they must NOT be merged (merging them would make the ring vanish). The ring is the annulus.
D8_EXTRA_TEST(FlushCoaxialCylindersTwoPercentApartStayDistinct) {
  const double pi = ON_PI, h = 4.0, R = 2.04, r = 2.0;
  const Brep big = Cylinder(ON_3dPoint(0, 0, 0), R, h);
  const Brep small = Cylinder(ON_3dPoint(0, 0, 0), r, h);
  CheckOp("coaxial flush R=2.04 r=2", big, small, BooleanOp::Union, pi * R * R * h);
  CheckOp("coaxial flush R=2.04 r=2", big, small, BooleanOp::Intersection, pi * r * r * h);
  CheckOp("coaxial flush R=2.04 r=2 (big - small)", big, small, BooleanOp::Difference, pi * (R * R - r * r) * h, 4);  // two annular caps + two walls
  CheckOp("coaxial flush R=2.04 r=2 (small - big)", small, big, BooleanOp::Difference, 0.0);
}

// A cylinder with its ends on the faces of a box (a through cylinder; and a blind one flush with one face only). Before: all refused.
D8_EXTRA_TEST(FlushCylinderInBoxUnionAndIntersectionMatchClosedForms) {
  const double pi = ON_PI;
  const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
  const Brep through = Cylinder(ON_3dPoint(5, 5, 0), 2.0, 10.0);
  const Brep blind = Cylinder(ON_3dPoint(5, 5, 0), 2.0, 6.0);
  CheckOp("box with a through cylinder flush at both ends", box, through, BooleanOp::Union, 1000.0);
  CheckOp("box with a through cylinder flush at both ends", box, through, BooleanOp::Intersection, pi * 4.0 * 10.0);
  CheckOp("box with a blind cylinder flush at one end", box, blind, BooleanOp::Union, 1000.0);
  CheckOp("box with a blind cylinder flush at one end", box, blind, BooleanOp::Intersection, pi * 4.0 * 6.0);
}

// Internally tangent along a line AND flush ends: the small cylinder is wholly inside the big one except for one line. small - big is
// empty (it was refused as "not consistently oriented"); the intersection is the small cylinder. (Union and big - small touch the
// big cylinder's wall in a cusp and are still refused: see the parity map's grazing-contact gap.)
D8_EXTRA_TEST(FlushInternallyTangentCylinders) {
  const double pi = ON_PI, h = 4.0;
  const Brep big = Cylinder(ON_3dPoint(0, 0, 0), 2.0, h);
  const Brep small = Cylinder(ON_3dPoint(1, 0, 0), 1.0, h);
  CheckOp("internally tangent flush cylinders (small - big)", small, big, BooleanOp::Difference, 0.0);
  CheckOp("internally tangent flush cylinders", big, small, BooleanOp::Intersection, pi * 1.0 * h);
}
