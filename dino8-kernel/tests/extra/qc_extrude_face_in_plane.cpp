// QC: ExtrudeFace / ExtrudeFaceWelded of a PLANAR face along a direction lying IN its plane.
//
// The sweep volume is |d . n| * area = 0: every wall is a flat sheet and both caps coincide with the sheet. ExtrudeFaceWelded() of a
// 3 x 4 flat sheet along (1, 0, 0) nevertheless returned a body with IsValid() and IsSolid() true (the weld closes: 8 vertices, 12 edges,
// 6 faces) whose Brep::Volume() throws "this Brep is not a closed, watertight solid" - a degenerate "solid" handed out silently. The kernel's
// other sweeps refuse this ("a direction lying in the profile plane": MakeBoss, |sin| < 1e-3). Oracles: the Cavalieri volume |d . n| * area of
// the same sheet extruded along the normal (24 for d = (0, 0, 2)), and the refusal is a std::invalid_argument naming the plane.
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

Brep Sheet(Point3d a, Point3d b, Point3d c, Point3d d) { return Brep::FromSurface(NurbsSurface::FromControlGrid({a, b, c, d}, 2, 2, 1, 1)); }

bool Refused(const std::function<void()>& f, std::string* what) {
  try {
    f();
  } catch (const std::invalid_argument& e) {
    *what = e.what();
    return true;
  } catch (const std::exception& e) {
    *what = std::string("(other exception) ") + e.what();
  }
  return false;
}

}  // namespace

D8_EXTRA_TEST(QcExtrudeFaceInPlaneDirectionIsRefused) {
  const Brep flat = Sheet(Point3d(0, 0, 0), Point3d(0, 4, 0), Point3d(3, 0, 0), Point3d(3, 4, 0));        // 3 x 4 in z = 0
  const Brep tilted = Sheet(Point3d(0, 0, 0), Point3d(0, 4, 0), Point3d(3, 0, 1.5), Point3d(3, 4, 1.5));  // plane contains (3, 0, 1.5) and (0, 4, 0)
  struct Dir { const char* name; const Brep* sheet; Vector3d d; };
  const Dir dirs[] = {{"flat sheet, along +x", &flat, Vector3d(1, 0, 0)},
                      {"flat sheet, along the diagonal (3, 4, 0)", &flat, Vector3d(3, 4, 0)},
                      {"tilted sheet, along its slope (3, 0, 1.5)", &tilted, Vector3d(3, 0, 1.5)},
                      {"tilted sheet, along (3, 4, 1.5)", &tilted, Vector3d(3, 4, 1.5)}};
  for (const Dir& c : dirs) {
    std::string why;
    const bool plain = Refused([&] { (void)Brep::ExtrudeFace(*c.sheet, 0, c.d); }, &why);
    d8test::Check(plain && why.find("plane") != std::string::npos, (std::string(c.name) + ": ExtrudeFace refuses with a reason naming the plane: " + why).c_str());
    std::string why2;
    const bool welded = Refused([&] { (void)Brep::ExtrudeFaceWelded(*c.sheet, 0, c.d); }, &why2);
    d8test::Check(welded && why2.find("plane") != std::string::npos, (std::string(c.name) + ": ExtrudeFaceWelded refuses with a reason naming the plane: " + why2).c_str());
  }
  // the neighbouring valid case still builds: along the normal the volume is |d . n| * area
  const Brep ok = Brep::ExtrudeFaceWelded(flat, 0, Vector3d(0, 0, 2));
  d8test::Check(ok.raw().IsValid() && ok.raw().IsSolid() && std::fabs(ok.Volume() - 24.0) < 1e-9, "flat 3 x 4 sheet along (0, 0, 2): a valid solid of volume 24");
  const Brep slanted = Brep::ExtrudeFaceWelded(flat, 0, Vector3d(1, 1, 2));
  d8test::Check(slanted.raw().IsValid() && slanted.raw().IsSolid() && std::fabs(slanted.Volume() - 24.0) < 1e-9, "flat 3 x 4 sheet along (1, 1, 2): Cavalieri volume 24");
}
