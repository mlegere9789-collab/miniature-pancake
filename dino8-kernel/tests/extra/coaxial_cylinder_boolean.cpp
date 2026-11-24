// General Boolean of two coaxial cylinders whose walls are 0.001 apart, and of a box with a sphere centred on one of its faces.
//
// Cylinder A: radius 1, axial range [0, 4]. Cylinder B: radius 1.001 (a tolerance wider), axial range [1, 3]. Both on one axis, so the cross-section is a
// pair of rectangles in the (radius, height) half-plane and every result is a solid of revolution with closed forms (rA = 1, rB = 1.001):
//   Union          = rA over [0,1], rB over [1,3], rA over [3,4]:   V = pi (rA^2 2 + rB^2 2),
//                    A = caps 2 pi rA^2 + walls 2 pi rA 2 + 2 pi rB 2 + the two rings 2 pi (rB^2 - rA^2).
//   Intersection   = rA over [1,3] (B is the wider):              V = pi rA^2 2,   A = 2 pi rA 2 + 2 pi rA^2.
//   A minus B      = rA over [0,1] and rA over [3,4] (B wider over the middle): two separate cylinders, V = 2 pi rA^2,  A = 2 (2 pi rA + 2 pi rA^2).
// Box [-2,2]^3 united with a sphere of radius 1.5 centred at (2,0,0), the middle of the +x face: the box plus the half ball beyond that face,
//   V = 64 + (2/3) pi r^3,   A = 96 + pi r^2 (the disc the half ball covers is replaced by the hemisphere 2 pi r^2).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }
double RelErr(double got, double want) { return std::fabs(got - want) / std::fabs(want); }

// The oracle runner's own route: general engine, then the sealing step.
bool Seal(const Brep& a, const Brep& b, BooleanOp op, Brep& out, std::string& why) {
  try {
    const Brep raw = dino8::kernel::BooleanCombineGeneralNAry({a}, {b}, op, 0.001);
    const double va = std::fabs(a.Volume()), vb = std::fabs(b.Volume());
    out = dino8::kernel::SealGeneralBooleanResult(raw, op, va, vb).brep;
    return true;
  } catch (const std::exception& e) {
    why = e.what();
    return false;
  }
}

void CheckPair(const Brep& a, const Brep& b, BooleanOp op, double want_volume, double want_area, int want_faces, const char* tag) {
  Brep out;
  std::string why;
  const bool ok = Seal(a, b, op, out, why);
  if (!ok) std::fprintf(stderr, "%s: refused: %s\n", tag, why.c_str());
  const std::string t = std::string(tag) + ": ";
  Check(ok, (t + "the general Boolean returns a sealed body").c_str());
  if (!ok) return;
  Check(out.raw().IsSolid() && out.raw().IsValid(), (t + "a valid closed solid").c_str());
  Check(RelErr(out.VolumeOfSealedSolid(), want_volume) < 1e-6, (t + "exact volume equals the closed form").c_str());
  Check(RelErr(out.Area(), want_area) < 1e-6, (t + "area equals the closed form").c_str());
  Check(out.LiveFaceCount() == want_faces, (t + "face count").c_str());
  Check(RelErr(out.TessellateToClosedMeshAdaptive(1e-5).Volume(), want_volume) < 1e-4, (t + "the closed tessellation has the same volume").c_str());
}

void RunCylinderPair(const ON_Xform* xf, const char* tag) {
  const double ra = 1.0, rb = 1.001, pi = ON_PI;
  Brep a = Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), ra, 4.0);
  Brep b = Brep::CylinderWelded(Point3d(0, 0, 1), Vector3d(0, 0, 1), rb, 2.0);
  if (xf) {
    a = a.Transform(*xf);
    b = b.Transform(*xf);
  }
  const std::string t(tag);
  CheckPair(a, b, BooleanOp::Union, pi * (ra * ra * 2 + rb * rb * 2),
            2 * pi * ra * ra + 2 * pi * ra * 2 + 2 * pi * rb * 2 + 2 * pi * (rb * rb - ra * ra), 1, (t + " union").c_str());
  CheckPair(b, a, BooleanOp::Union, pi * (ra * ra * 2 + rb * rb * 2),
            2 * pi * ra * ra + 2 * pi * ra * 2 + 2 * pi * rb * 2 + 2 * pi * (rb * rb - ra * ra), 1, (t + " union, operands swapped").c_str());
  CheckPair(a, b, BooleanOp::Intersection, pi * ra * ra * 2, 2 * pi * ra * 2 + 2 * pi * ra * ra, 3, (t + " intersection").c_str());
  CheckPair(a, b, BooleanOp::Difference, 2 * pi * ra * ra, 2 * (2 * pi * ra + 2 * pi * ra * ra), 6, (t + " difference A - B (two cylinders)").c_str());
}

}  // namespace

D8_EXTRA_TEST(CoaxialCylindersWallsOneToleranceApart) {
  RunCylinderPair(nullptr, "coaxial r 1 / 1.001");
}

D8_EXTRA_TEST(CoaxialCylindersWallsOneToleranceApartTurnedAndMoved) {
  ON_Xform rot;
  rot.Rotation(0.9, ON_3dVector(0.3, -1.0, 0.7), ON_3dPoint(0.5, 0.25, 0.125));
  ON_Xform move = ON_Xform::TranslationTransformation(ON_3dVector(40.0, -12.5, 7.0));
  const ON_Xform both = move * rot;
  RunCylinderPair(&both, "coaxial r 1 / 1.001, turned and moved");
}

D8_EXTRA_TEST(BoxUnionSphereCentredOnItsFace) {
  const double r = 1.5, pi = ON_PI;
  const Brep box = Brep::BoxWelded(-2, -2, -2, 2, 2, 2);
  const Brep ball = Brep::SphereWelded(Point3d(2, 0, 0), r);
  Brep out;
  std::string why;
  const bool ok = Seal(box, ball, BooleanOp::Union, out, why);
  if (!ok) std::fprintf(stderr, "box + sphere on its face: refused: %s\n", why.c_str());
  Check(ok, "box + sphere centred on the +x face: the general Boolean returns a sealed body");
  if (!ok) return;
  Check(out.raw().IsSolid() && out.raw().IsValid(), "box + sphere centred on the +x face: a valid closed solid (its pole slivers become singular trims, not diagonal seam trims)");
  // The body carries the polyline edges the engine's cut curve is made of (about 24 chords per half circle): the figures are exact to that sampling.
  const double want_v = 64.0 + 2.0 / 3.0 * pi * r * r * r, want_a = 96.0 + pi * r * r;
  Check(RelErr(out.VolumeOfSealedSolid(), want_v) < 5e-4, "box + sphere on its face: volume = 64 + 2/3 pi r^3 to the polyline sampling of the cut circle");
  Check(RelErr(out.Area(), want_a) < 2e-3, "box + sphere on its face: area = 96 + pi r^2 to the polyline sampling of the cut circle");
  // The extent of the closed tessellation (the raw control box of a trimmed sphere face is looser than the body).
  const dino8::kernel::BoundingBox bb = out.TessellateToClosedMeshAdaptive(1e-4).GetBoundingBox();
  Check(std::fabs(bb.max.x - (2.0 + r)) < 1e-3 && std::fabs(bb.min.x + 2.0) < 1e-9 && std::fabs(bb.max.y - 2.0) < 1e-9 && std::fabs(bb.max.z - 2.0) < 1e-9,
        "box + sphere on its face: extent x -2 .. 3.5, y and z -2 .. 2");
}
