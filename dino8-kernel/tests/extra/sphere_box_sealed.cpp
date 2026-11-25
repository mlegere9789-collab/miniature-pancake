// A box and a sphere that cross each other, united / subtracted / intersected through BooleanCombineGeneralNAry() and sealed with
// SealGeneralBooleanResult(), against closed forms that do not come from the code under test. The box is the 10 cube [0, 10]^3.
//   * a sphere r 4 centred at z = 8 pokes a cap of height h = 2 out of the top face (the cap volume pi h^2 (3 r - h) / 3 = 41.888; the sphere's
//     part inside the box is the whole sphere less that cap, 4/3 pi 64 - 41.888 = 226.195),
//   * a sphere r 3 centred on the x = 10 face has exactly half of itself outside (2/3 pi 27 = 56.549),
//   * a sphere r 4 centred on the top face z = 10 (the case that always sealed) is the control: 2/3 pi 64 = 134.041 outside.
// Before the seam-vertex reuse in FinishSeamCrossing() and the tolerated UnifyNormals() tessellation refusal after JoinNakedEdges()
// (the plane-split work), the cap case was refused ("the sewn result is not a valid ON_Brep: ... seam but m_iso is not N/E/W/S_iso") and
// the side case ("2 unpaired edge(s)"); both now seal. Each sealed result must be ON_Brep::IsValid(), IsSolid(), have no edge with a
// number of trims other than 2, have 7 faces (5 box faces + the face with the circle removed + the spherical piece, or for the Difference
// the same 7 with the sphere piece turned inward), and an exact (divergence theorem, no tessellation) volume within the stated relative
// tolerance of the closed form (measured relative errors in the comments).
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::BooleanOp;

Brep Wrap(ON_Brep* b) {
  Brep k;
  if (b) {
    k.raw() = *b;
    delete b;
  }
  return k;
}

Brep Cube10() {
  const ON_3dPoint c[8] = {ON_3dPoint(0, 0, 0),  ON_3dPoint(10, 0, 0),  ON_3dPoint(10, 10, 0),  ON_3dPoint(0, 10, 0),
                           ON_3dPoint(0, 0, 10), ON_3dPoint(10, 0, 10), ON_3dPoint(10, 10, 10), ON_3dPoint(0, 10, 10)};
  return Wrap(ON_BrepBox(c));
}

Brep Ball(double x, double y, double z, double r) { return Wrap(ON_BrepSphere(ON_Sphere(ON_3dPoint(x, y, z), r))); }

void Case(const char* name, const Brep& box, const Brep& ball, double ball_volume, BooleanOp op, int faces, double expected, double rel_tol) {
  const std::string n(name);
  try {
    const Brep raw = dino8::kernel::BooleanCombineGeneralNAry({box}, {ball}, op);
    const dino8::kernel::SealedGeneralBoolean sb = dino8::kernel::SealGeneralBooleanResult(raw, op, 1000.0, ball_volume);
    const ON_Brep& r = sb.brep.raw();
    int bad_edges = 0;
    for (int i = 0; i < r.m_E.Count(); ++i) bad_edges += r.m_E[i].TrimCount() != 2 ? 1 : 0;
    ON_TextLog log;
    d8test::Check(r.IsValid(&log) && r.IsSolid() && bad_edges == 0, (n + ": sealed result is ON_Brep::IsValid(), IsSolid(), every edge has two trims").c_str());
    d8test::Check(r.m_F.Count() == faces, (n + ": " + std::to_string(faces) + " faces").c_str());
    const double v = sb.brep.VolumeOfSealedSolid();
    char what[200];
    std::snprintf(what, sizeof what, ": exact volume %.4f against the closed form %.4f (relative %.2e, allowed %.0e)", v, expected, (v - expected) / expected, rel_tol);
    d8test::Check(std::fabs(v - expected) <= rel_tol * expected, (n + what).c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (n + " threw: " + e.what()).c_str());
  }
}

}  // namespace

D8_EXTRA_TEST(SphereBoxSealedClosedForms) {
  const double pi = ON_PI;
  const double cap = pi * 2 * 2 * (3 * 4 - 2) / 3.0;  // sphere r 4, height 2 above the top face
  const double ball4 = 4.0 / 3.0 * pi * 64;
  const double half3 = 2.0 / 3.0 * pi * 27;
  const Brep box = Cube10();
  Case("box + sphere r4 at z = 10 (control)", box, Ball(5, 5, 10, 4), 4.0 / 3.0 * pi * 4 * 4 * 4, BooleanOp::Union, 7, 1000 + 2.0 / 3.0 * pi * 64, 1e-4);  // measured +3.4e-6
  Case("box + sphere r4 at z = 8 (cap of height 2 out of the top)", box, Ball(5, 5, 8, 4), 4.0 / 3.0 * pi * 4 * 4 * 4, BooleanOp::Union, 7, 1000 + cap, 2e-4);  // measured -4.1e-5
  Case("box - sphere r4 at z = 8", box, Ball(5, 5, 8, 4), 4.0 / 3.0 * pi * 4 * 4 * 4, BooleanOp::Difference, 7, 1000 - (ball4 - cap), 2e-4);  // measured -5.5e-5
  Case("box + sphere r3 centred on the x = 10 face", box, Ball(10, 5, 5, 3), 4.0 / 3.0 * pi * 3 * 3 * 3, BooleanOp::Union, 7, 1000 + half3, 1e-3);  // measured +4.5e-4
}
