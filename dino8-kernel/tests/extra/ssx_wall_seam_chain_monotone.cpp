// A plane that cuts a cylinder wall all the way round meets it in a closed curve that crosses the wall's own seam once: the wall's
// angle parameter jumps by one whole period between two neighbouring samples of the chain. The midpoint refinement of
// FinishCurve() (surface_intersect.cpp) seeded its Newton solve from cubic fits of the RAW (u, v) samples, so the jump rang
// through the spline, the seeds of the segments around the seam were up to 0.8 off the curve, and the solve slid along the circle
// to a point that the (u, v)-bracket guards still admitted: an inserted point lay BEYOND its segment's far end (1.6 chords along
// the segment on the hole fixture below), so two neighbouring samples were out of order. Every later stage of a Boolean built on
// that chain (fragment loops, reconcile, tessellation) inherited the stray pair and the hole's trim polygon crossed itself.
//
// The oracle is independent of the intersector: the circle (or ellipse) of the cut is a convex closed curve, so the angle of its
// points about the cylinder's axis must be strictly monotone, advance by exactly one turn, and every point must lie at the wall's
// radius from the axis.
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
using dino8::kernel::Vector3d;

struct WallCut {
  bool found = false;
  std::vector<IntersectionCurve> curves;
};

// The faces of `plane_box` (face index `plane_face`) against the cylinder wall of a capped Pipe along `start` -> `end`.
WallCut CutWallByPlane(const Brep& plane_box, int plane_face, const Point3d& start, const Point3d& end, double radius) {
  WallCut out;
  const Brep pipe = Brep::Pipe(NurbsCurve::FromControlPoints({start, end}, 1), radius, /*cap=*/true, /*stations=*/2);
  const ON_Brep& bp = plane_box.raw();
  const ON_Brep& bc = pipe.raw();
  for (int j = 0; j < bc.m_F.Count(); ++j) {
    ON_Cylinder cyl;
    if (!bc.m_F[j].SurfaceOf()->IsCylinder(&cyl, 1e-6)) continue;
    out.found = true;
    IntersectOptions opt;
    out.curves = dino8::kernel::IntersectFaces(&bp.m_F[plane_face], *bp.m_F[plane_face].SurfaceOf(), &bc.m_F[j], *bc.m_F[j].SurfaceOf(), opt);
    break;
  }
  return out;
}

// Largest deviation of a point from the wall's radius about the axis through `center` with unit direction `axis`; and the
// monotonicity / total turn of the point sequence about that axis.
struct Sweep {
  double max_radius_error = 0.0;
  bool monotone = true;
  double turns = 0.0;  // signed number of full turns walked by consecutive samples, closing step included
  int reversals = 0;
};

Sweep AngleSweep(const std::vector<Point3d>& pts, bool closed, const Point3d& center, Vector3d axis, double radius) {
  axis.Unitize();
  Vector3d e1 = ON_CrossProduct(axis, std::fabs(axis.z) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0));
  e1.Unitize();
  const Vector3d e2 = ON_CrossProduct(axis, e1);
  Sweep s;
  std::vector<double> ang;
  for (const Point3d& p : pts) {
    const Vector3d d = p - center;
    const Vector3d w = d - axis * (d * axis);
    s.max_radius_error = std::max(s.max_radius_error, std::fabs(w.Length() - radius));
    ang.push_back(std::atan2(w * e2, w * e1));
  }
  double total = 0.0;
  int sign = 0;
  const size_t n = ang.size();
  const size_t steps = closed ? n : n - 1;
  for (size_t i = 0; i < steps; ++i) {
    double d = ang[(i + 1) % n] - ang[i];
    while (d > ON_PI) d -= 2.0 * ON_PI;
    while (d < -ON_PI) d += 2.0 * ON_PI;
    total += d;
    const int sg = d > 0 ? 1 : -1;
    if (sign == 0) sign = sg;
    if (sg != sign) {
      s.monotone = false;
      ++s.reversals;
    }
  }
  s.turns = total / (2.0 * ON_PI);
  return s;
}

void CheckCut(const char* label, const Brep& plane_box, int plane_face, const Point3d& start, const Point3d& end, double radius) {
  const WallCut cut = CutWallByPlane(plane_box, plane_face, start, end, radius);
  d8test::Check(cut.found, "fixture: the pipe has a cylinder wall");
  d8test::Check(!cut.curves.empty(), label);
  const Vector3d axis = end - start;
  for (const IntersectionCurve& ic : cut.curves) {
    if (!ic.closed) continue;
    const Sweep sw = AngleSweep(ic.points, true, start, axis, radius);
    d8test::Check(sw.max_radius_error < IntersectOptions().tolerance, "the cut's samples all lie on the wall (radius from the axis within the intersection tolerance)");
    d8test::Check(sw.monotone, "the cut's samples advance in one direction about the axis (no sample out of order)");
    d8test::Check(std::fabs(std::fabs(sw.turns) - 1.0) < 1e-9, "the cut winds exactly once about the axis");
  }
}

}  // namespace

D8_EXTRA_TEST(SsxWallSeamCrossingChainStaysMonotone) {
  // The hole fixture, with MakeHole()'s own tool: the slab (4 x 16 x 4, bounding-box diagonal sqrt(288)), a capped r = 0.5 pipe whose
  // near cap sits 0.5 above the top face and whose length is 0.5 + 2 * diagonal + 0.5. The plane meets the wall at right angles, so
  // the whole circle is one closed curve.
  const double diagonal = std::sqrt(4.0 * 4.0 + 16.0 * 16.0 + 4.0 * 4.0);
  const double tool_bottom = 4.0 + 0.5 - (0.5 + 2.0 * diagonal + 0.5);
  {
    const Brep slab = Brep::Box(0, 0, 0, 4, 16, 4);
    CheckCut("plane z = 0 against an r = 0.5 wall about the z axis: one closed circle", slab, 0, Point3d(2, 2, 4.5), Point3d(2, 2, tool_bottom), 0.5);
    CheckCut("plane z = 4 against the same wall: one closed circle", slab, 1, Point3d(2, 2, 4.5), Point3d(2, 2, tool_bottom), 0.5);
  }
  // The other holes of the pattern: the wall's seam sits at the same angle but the plane's chart is shifted.
  {
    const Brep slab = Brep::Box(0, 0, 0, 4, 16, 4);
    for (const double y : {5.0, 8.0, 11.0}) {
      CheckCut("the same cut further along the slab", slab, 0, Point3d(2, y, 4.5), Point3d(2, y, tool_bottom), 0.5);
      CheckCut("the same cut on the top face further along the slab", slab, 1, Point3d(2, y, 4.5), Point3d(2, y, tool_bottom), 0.5);
    }
  }
  // A tilted axis: the cut is an ellipse, still one closed curve across the seam.
  {
    const Brep slab = Brep::Box(-5, -5, 0, 5, 5, 4);
    CheckCut("plane z = 0 against a wall tilted 12 degrees: one closed ellipse", slab, 0, Point3d(0.3, -0.2, 3), Point3d(0.3 + 0.2 * 10, -0.2 + 0.1 * 10, -7), 0.7);
  }
}
