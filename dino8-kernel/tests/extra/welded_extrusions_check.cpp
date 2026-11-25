// Brep::Check() coverage for the genuine-topology extrusion / thickening results (ExtrudeFaceWelded, ThickenWelded).
// Oracles: a closed welded hexahedral shell must report NO issue at all (every edge shared by exactly two faces, consistent
// orientation); the open 4-wall tube has exactly its 8 boundary edges naked and nothing else; the surface-only results keep
// reporting open boundaries or no topology. Volumes are Cavalieri closed forms (area * height).
#include <cmath>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {
using dino8::kernel::Brep;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

Brep FlatSheet() {
  std::vector<Point3d> g;
  for (int i = 0; i < 2; ++i)
    for (int j = 0; j < 2; ++j) g.push_back(Point3d(3.0 * i, 4.0 * j, 0.0));
  return Brep::FromSurface(NurbsSurface::FromControlGrid(g, 2, 2, 1, 1));
}

int Naked(const Brep::CheckReport& r) {
  int n = 0;
  for (const auto& i : r.issues) n += i.kind == Brep::CheckIssue::Kind::NakedEdge;
  return n;
}
}  // namespace

D8_EXTRA_TEST(WeldedExtrusionsCheckReports) {
  using d8test::Check;
  const Brep solid = Brep::ExtrudeFaceWelded(FlatSheet(), 0, Vector3d(0, 0, 2.5), true);
  const Brep::CheckReport rs = solid.Check();
  Check(rs.IsClean() && rs.is_closed && rs.is_oriented, "ExtrudeFaceWelded (capped): Check() reports no issue, closed and oriented");
  Check(std::abs(solid.VolumeOfSealedSolid() - 3.0 * 4.0 * 2.5) < 1e-6, "ExtrudeFaceWelded (capped): sealed volume 3 * 4 * 2.5 = 30");

  const Brep tube = Brep::ExtrudeFaceWelded(FlatSheet(), 0, Vector3d(0, 0, 2.5), false);
  const Brep::CheckReport rt = tube.Check();
  Check(Naked(rt) == 8 && static_cast<int>(rt.issues.size()) == 8 && !rt.is_closed,
        "ExtrudeFaceWelded (uncapped): exactly the 8 rim edges are naked and nothing else");

  const Brep thick = Brep::ThickenWelded(FlatSheet(), 1.5, false);
  const Brep::CheckReport rk = thick.Check();
  Check(rk.IsClean() && rk.is_closed && rk.is_oriented, "ThickenWelded: Check() reports no issue, closed and oriented");
  Check(std::abs(thick.VolumeOfSealedSolid() - 12.0 * 1.5) < 1e-6, "ThickenWelded: sealed volume area * thickness = 18");

  const Brep plain = Brep::ExtrudeFace(FlatSheet(), 0, Vector3d(0, 0, 2.5), true);
  Check(!plain.raw().IsValid(), "contrast: the surface-only ExtrudeFace() is still not a valid ON_Brep");
}
