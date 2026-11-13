// SplitBrepBySolid: a 10 x 10 x 10 cube split by a sphere / a cylinder cutter into the part inside and the part outside, two closed solids,
// against closed forms: sphere r 3 fully inside = 4/3 pi 27 = 36 pi; the same sphere centred on the top face = 18 pi (a hemisphere); a cylinder
// r 2 through the whole cube = pi 4 10 = 40 pi; the outside is 1000 minus that. Topology from first principles: the inside piece is a ball
// (Euler characteristic V - E + F - R = 2); the outside is a cube with a cavity (two shells, chi 2 + 2 = 4), a cube with a dent (chi 2) and a
// cube with a through hole (a torus: chi 0).
// Before SplitBrepBySolid sewed its open pieces, the outside of the crossing cases was IsValid but not a solid (naked edges, no exact volume).
#include <cmath>
#include <exception>
#include <string>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {
using dino8::kernel::Brep;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

int Euler(const Brep& b) {
  const ON_Brep& r = b.raw();
  int inner = 0;
  for (int i = 0; i < r.m_L.Count(); ++i) inner += r.m_L[i].m_type == ON_BrepLoop::inner ? 1 : 0;
  return r.m_V.Count() - r.m_E.Count() + r.m_F.Count() - inner;
}

double Vol(const Brep& b) {
  try {
    return b.VolumeOfSealedSolid();
  } catch (const std::exception&) {
    return std::nan("");
  }
}

void Check(const char* label, const Brep& cutter, double inside_vol, int outside_chi) {
  const Brep cube = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
  try {
    const auto [outside, inside] = dino8::kernel::SplitBrepBySolid(cube, cutter);
    const std::string l(label);
    d8test::Check(inside.raw().IsValid() && inside.raw().IsSolid(), (l + ": the inside piece is a valid closed solid").c_str());
    d8test::Check(outside.raw().IsValid() && outside.raw().IsSolid(), (l + ": the outside piece is a valid closed solid").c_str());
    d8test::Check(Euler(inside) == 2, (l + ": the inside piece is a ball (chi 2)").c_str());
    d8test::Check(Euler(outside) == outside_chi, (l + ": the outside piece has the closed-form Euler characteristic").c_str());
    const double vi = Vol(inside), vo = Vol(outside);
    d8test::Check(std::fabs(vi - inside_vol) <= 1e-4 * inside_vol, (l + ": inside volume matches the closed form to 1e-4").c_str());
    d8test::Check(std::fabs(vo - (1000.0 - inside_vol)) <= 1e-4 * 1000.0, (l + ": outside volume is 1000 minus it to 1e-4 of 1000").c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string(label) + ": threw " + e.what()).c_str());
  }
}
}  // namespace

D8_EXTRA_TEST(SplitSolidBySolidCutterSphereAndCylinder) {
  Check("sphere r 3 inside the cube", Brep::SphereWelded(Point3d(5, 5, 5), 3), 36.0 * ON_PI, 4);
  Check("sphere r 3 centred on the top face", Brep::SphereWelded(Point3d(5, 5, 10), 3), 18.0 * ON_PI, 2);
  Check("NURBS sphere r 3 centred on the top face", Brep::Sphere(Point3d(5, 5, 10), 3), 18.0 * ON_PI, 2);
  Check("cylinder r 2 through the cube", Brep::CylinderWelded(Point3d(5, 5, -1), Vector3d(0, 0, 1), 2, 12), 40.0 * ON_PI, 0);
}
