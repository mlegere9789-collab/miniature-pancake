// Sealed general-Boolean regressions from the golden/evil corpus: cases whose result used to be refused with "not a closed
// solid after sewing". Each is an exact closed-form volume (cylinder/cone/sphere/box formulas, no kernel number) checked to
// 2e-5 relative (1e-4 for the parallel-cylinder lens, whose caps are 24-gons), and the sealed body must be ON_Brep::IsValid() and IsSolid() with outward orientation.
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

// Plane with `origin` and z axis `axis`; the x axis (hence the seam of a cylinder, cone or sphere built on it) is the world x
// axis projected perpendicular to the axis, as the corpus builds its operands.
ON_Plane CorpusFrame(const ON_3dPoint& origin, ON_3dVector axis) {
  axis.Unitize();
  const ON_3dVector seed = std::fabs(axis.x) < 0.9 ? ON_3dVector(1, 0, 0) : ON_3dVector(0, 1, 0);
  ON_3dVector x = seed - (seed * axis) * axis;
  x.Unitize();
  return ON_Plane(origin, x, ON_CrossProduct(axis, x));
}

Brep Own(ON_Brep* b) {
  Brep k;
  if (!b) return k;
  k.raw() = *b;
  delete b;
  if (k.raw().IsSolid()) {
    double v = 0;
    try { v = k.VolumeOfSealedSolid(); } catch (const std::exception&) {}
    if (v < 0) k.raw().Flip();
  }
  return k;
}

Brep BoxOf(const ON_3dPoint& lo, const ON_3dPoint& hi) {
  const ON_3dPoint c[8] = {ON_3dPoint(lo.x, lo.y, lo.z), ON_3dPoint(hi.x, lo.y, lo.z), ON_3dPoint(hi.x, hi.y, lo.z),
                           ON_3dPoint(lo.x, hi.y, lo.z), ON_3dPoint(lo.x, lo.y, hi.z), ON_3dPoint(hi.x, lo.y, hi.z),
                           ON_3dPoint(hi.x, hi.y, hi.z), ON_3dPoint(lo.x, hi.y, hi.z)};
  return Own(ON_BrepBox(c));
}

Brep ZCylinder(const ON_3dPoint& base, double radius, double height) {
  ON_Cylinder cyl(ON_Circle(CorpusFrame(base, ON_3dVector(0, 0, 1)), radius), height);
  return Own(ON_BrepCylinder(cyl, true, true));
}

// `what` names the corpus case; the sealed body must be closed, valid, and of the exact volume.
void ExpectSealed(const Brep& a, const Brep& b, BooleanOp op, double volume, const char* what, double rel = 2e-5) {
  const std::string name(what);
  try {
    const SealedGeneralBoolean s = BooleanCombineGeneralSealed(a, b, op);
    d8test::Check(s.brep.raw().IsValid(), (name + ": sealed result is a valid ON_Brep").c_str());
    d8test::Check(s.brep.raw().IsSolid(), (name + ": sealed result is a closed solid").c_str());
    const double v = s.brep.VolumeOfSealedSolid();
    d8test::Check(std::fabs(v - volume) <= rel * volume, (name + ": sealed volume equals the closed form").c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (name + ": sealing refused: " + e.what()).c_str());
  }
}

}  // namespace

// Corpus G-CYL-CYL-PARALLEL-I: two unit cylinders 1 apart (z in [0,4] and [1,5]); the lens prism has area
// 2 acos(1/2) - sqrt(3)/2 and height 3.
D8_EXTRA_TEST(SealedBooleanCorpusParallelCylindersIntersection) {
  const double lens = 2.0 * std::acos(0.5) - 0.5 * std::sqrt(3.0);
  ExpectSealed(ZCylinder(ON_3dPoint(0, 0, 0), 1.0, 4.0), ZCylinder(ON_3dPoint(1, 0, 1), 1.0, 4.0), BooleanOp::Intersection, 3.0 * lens,
               "corpus G-CYL-CYL-PARALLEL-I", 1e-4);
}

// Corpus E-TAN-CYL-CYL-OV1e-6-U: two parallel unit cylinders overlapping by 1e-6 (a sliver far below the SSX tolerance).
// V = 2 pi 3 minus the sliver (circular segment of height h = 1e-6 per cylinder, area ~ (4/3) sqrt(2) h^1.5, times 2.5 overlap).
D8_EXTRA_TEST(SealedBooleanCorpusParallelCylindersSubToleranceOverlapUnion) {
  const double h = 1e-6;
  const double segment = std::acos(1.0 - h) - (1.0 - h) * std::sqrt(1.0 - (1.0 - h) * (1.0 - h));
  const double overlap = 2.0 * segment * 2.5;
  ExpectSealed(ZCylinder(ON_3dPoint(0, 0, 0), 1.0, 3.0), ZCylinder(ON_3dPoint(1.999999, 0, 0.5), 1.0, 3.0), BooleanOp::Union,
               2.0 * ON_PI * 3.0 - overlap, "corpus E-TAN-CYL-CYL-OV1e-6-U");
}

// Corpus E-TAN-CYL-BOX-OV1e-6-U: cylinder wall 1e-6 inside the face x = 0 of the box [0,4]^3 (the box face the sliver touches
// used to be dropped from the result).
D8_EXTRA_TEST(SealedBooleanCorpusCylinderWallSunkIntoBoxFaceUnion) {
  const double h = 1e-6;
  const double segment = std::acos(1.0 - h) - (1.0 - h) * std::sqrt(1.0 - (1.0 - h) * (1.0 - h));
  const double sliver = segment * 4.0;  // z in [0, 4] is inside the box
  ExpectSealed(BoxOf(ON_3dPoint(0, 0, 0), ON_3dPoint(4, 4, 4)), ZCylinder(ON_3dPoint(-0.999999, 2, -1), 1.0, 6.0), BooleanOp::Union,
               64.0 + ON_PI * 6.0 - sliver, "corpus E-TAN-CYL-BOX-OV1e-6-U");
}

// Corpus E-TAN-SPH-BOX-POLE-OV1e-6-U: unit sphere (pole axis z) sunk 1e-6 below the face z = 4 of the box [0,4]^3.
D8_EXTRA_TEST(SealedBooleanCorpusSpherePoleSunkIntoBoxFaceUnion) {
  const double d = 1e-6;
  const double cap = ON_PI * d * d * (3.0 - d) / 3.0;  // part of the sphere inside the box
  ON_Sphere sph(ON_3dPoint(2, 2, 4.999999), 1.0);
  sph.plane = CorpusFrame(ON_3dPoint(2, 2, 4.999999), ON_3dVector(0, 0, 1));
  ExpectSealed(BoxOf(ON_3dPoint(0, 0, 0), ON_3dPoint(4, 4, 4)), Own(ON_BrepSphere(sph)), BooleanOp::Union,
               64.0 + 4.0 * ON_PI / 3.0 - cap, "corpus E-TAN-SPH-BOX-POLE-OV1e-6-U");
}

// Corpus E-APEX-BOX-SUNK1e-6-U: cone (apex down, height 2, base radius 1) whose tip is 1e-6 below the top face of the cube
// [-2,2]^3; the tip inside the cube is a cone of height d and radius d/2.
D8_EXTRA_TEST(SealedBooleanCorpusConeTipSunkIntoBoxFaceUnion) {
  const double d = 1e-6;
  const double tip = ON_PI * d * d * d / 12.0;
  ON_Cone cone(CorpusFrame(ON_3dPoint(0, 0, 1.999999), ON_3dVector(0, 0, 1)), 2.0, 1.0);
  ExpectSealed(BoxOf(ON_3dPoint(-2, -2, -2), ON_3dPoint(2, 2, 2)), Own(ON_BrepCone(cone, true)), BooleanOp::Union,
               64.0 + ON_PI * 2.0 / 3.0 - tip, "corpus E-APEX-BOX-SUNK1e-6-U");
}
