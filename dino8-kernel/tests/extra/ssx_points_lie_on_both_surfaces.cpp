// IntersectOptions::tolerance (1e-3 by default, and what the Boolean pipeline passes) is the residual |S1 - S2| a refined chain point
// must come within before it is ACCEPTED. The Newton solve used to stop the moment it was inside that, so a seed that sat on a facet
// of the other surface's seed mesh (a chord sagitta, up to 7e-4 for a cylinder of radius 0.5, off the true curve) came back
// untouched: the chain point lay on the plane exactly and on the cylinder wall up to 7e-4 away. Two faces sharing that rim then
// evaluated one trim vertex at points 7e-4 apart, orders of magnitude over the weld tolerance, and the tessellated rim never closed
// (the top rims of holes 2, 3 and 4 of a hole pattern; the bottom rims, whose seeds happened to be exact, were fine).
//
// The oracle is independent of the intersector: the closed form of the cut. A plane z = c meets the wall of the r = 0.5 cylinder
// about the vertical line (x0, y0) in the circle of radius 0.5 about that line, so EVERY chain point has z == c and a radial distance
// of exactly 0.5 from the axis, to the floating-point floor, not merely to the acceptance tolerance.
#include <algorithm>
#include <cmath>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::IntersectionCurve;
using dino8::kernel::IntersectOptions;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;

}  // namespace

D8_EXTRA_TEST(SsxChainPointsLieOnBothSurfaces) {
  const double r = 0.5;
  const double diagonal = std::sqrt(4.0 * 4.0 + 16.0 * 16.0 + 4.0 * 4.0);
  const double tool_bottom = 4.0 + 0.5 - (0.5 + 2.0 * diagonal + 0.5);  // MakeHole()'s tool for the 4 x 16 x 4 slab
  const Brep slab = Brep::Box(0, 0, 0, 4, 16, 4);
  const ON_Brep& bp = slab.raw();
  double worst = 0.0;
  int curves_seen = 0;
  for (const double y0 : {2.0, 5.0, 8.0, 11.0}) {
    const Brep pipe = Brep::Pipe(NurbsCurve::FromControlPoints({Point3d(2, y0, 4.5), Point3d(2, y0, tool_bottom)}, 1), r, /*cap=*/true, /*stations=*/2);
    const ON_Brep& bc = pipe.raw();
    for (int j = 0; j < bc.m_F.Count(); ++j) {
      ON_Cylinder cyl;
      if (!bc.m_F[j].SurfaceOf()->IsCylinder(&cyl, 1e-6)) continue;
      for (const int plane_face : {0, 1}) {  // z = 0 and z = 4
        const double z = plane_face == 0 ? 0.0 : 4.0;
        IntersectOptions opt;
        for (const IntersectionCurve& ic : dino8::kernel::IntersectFaces(&bp.m_F[plane_face], *bp.m_F[plane_face].SurfaceOf(), &bc.m_F[j],
                                                                           *bc.m_F[j].SurfaceOf(), opt)) {
          ++curves_seen;
          for (const Point3d& p : ic.points) {
            worst = std::max(worst, std::fabs(std::hypot(p.x - 2.0, p.y - y0) - r));
            worst = std::max(worst, std::fabs(p.z - z));
          }
        }
      }
    }
  }
  d8test::Check(curves_seen >= 8, "fixture: each of the four hole axes is cut by both the z = 0 and the z = 4 plane");
  d8test::Check(worst < 1e-8,
                "every chain point lies on the cut circle (radius 0.5 about the axis, on the plane) to 1e-8, not merely within the 1e-3 acceptance tolerance");
}
