// A fragment loop whose straight run was merged from two samplings of different density can carry two points placed OUT OF ORDER
// along the run, a few 1e-8 off it (the 2D crossing predicate only sees offsets above ~1e-9 on steps this short): the boundary goes
// forward along the line, steps back to the first out-of-order point, and crosses its own edges. The spike removal every loop goes
// through used to judge "the same line" by the ANGLE between steps (1e-9 rad), which an offset of 2e-8 on a 2e-4 step (1e-4 rad)
// exceeds, so such a loop stayed non-simple and the tessellation refused it ("trim_polygon must be simple"). Oracles are independent:
// the shoelace area of the intended rectangle and the engine-independent 2D simplicity predicate.
#include <cmath>
#include <vector>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/detail/polygon2d.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Point2d;
using dino8::kernel::Point3d;

// A 4 x 2 rectangle (CCW) whose bottom edge is a dense run of 2e-4 steps from x = 0 to 0.01 followed by a coarse remainder to
// x = 4. `noise` is the perpendicular offset given to the two out-of-order points (alternating sides of the run).
void BuildLoop(double noise, bool out_of_order, std::vector<Point3d>& pts, std::vector<Point2d>& uv) {
  std::vector<Point2d> poly;
  for (int i = 0; i <= 50; ++i) poly.emplace_back(2e-4 * i, 0.0);  // dense run: x = 0 .. 0.01
  if (out_of_order) {
    poly.emplace_back(0.0063, noise);   // back between earlier vertices
    poly.emplace_back(0.0067, -noise);  // and again, on the other side of the run
  }
  poly.emplace_back(0.011, 0.0);  // on: spacing 5x coarser than the dense run
  poly.emplace_back(0.5, 0.0);
  poly.emplace_back(4.0, 0.0);
  poly.emplace_back(4.0, 2.0);
  poly.emplace_back(0.0, 2.0);
  for (const Point2d& q : poly) {
    pts.emplace_back(q.x, q.y, 0.0);
    uv.push_back(q);
  }
}

double ShoelaceArea(const std::vector<Point2d>& poly) {
  double a = 0.0;
  for (size_t i = 0; i < poly.size(); ++i) {
    const Point2d& p = poly[i];
    const Point2d& q = poly[(i + 1) % poly.size()];
    a += p.x * q.y - q.x * p.y;
  }
  return 0.5 * a;
}

}  // namespace

D8_EXTRA_TEST(LoopSpikeRemovalMergesOutOfOrderPointsOfAStraightRun) {
  // Control: a clean loop is untouched.
  {
    std::vector<Point3d> pts;
    std::vector<Point2d> uv;
    BuildLoop(0.0, false, pts, uv);
    const size_t n = pts.size();
    dino8::kernel::detail::RemoveBacktrackSpikesForTest(pts, uv);
    d8test::Check(pts.size() == n, "clean loop: no vertex removed");
    d8test::Check(dino8::kernel::detail::IsSimplePolygon(uv), "clean loop: simple");
  }
  // The defect: two out-of-order points 2e-8 off the run, on opposite sides (their edges cross the run's own edges).
  {
    std::vector<Point3d> pts;
    std::vector<Point2d> uv;
    BuildLoop(2e-8, true, pts, uv);
    d8test::Check(!dino8::kernel::detail::IsSimplePolygon(uv), "fixture: the out-of-order loop really crosses itself");
    dino8::kernel::detail::RemoveBacktrackSpikesForTest(pts, uv);
    d8test::Check(dino8::kernel::detail::IsSimplePolygon(uv), "out-of-order points of a straight run: the loop is simple afterwards");
    d8test::Check(std::fabs(ShoelaceArea(uv) - 8.0) < 1e-9, "...and still encloses the 4 x 2 rectangle (area 8)");
    bool monotone = true;
    // the bottom run is the leading points whose y is within 1e-9 of 0 (up to the corner at x = 4)
    for (size_t i = 0; i + 1 < uv.size() && std::fabs(uv[i + 1].y) < 1e-9 && uv[i + 1].x > 0.0; ++i) monotone = monotone && uv[i + 1].x > uv[i].x;
    d8test::Check(monotone, "...and the x coordinates along the bottom run increase strictly (no point out of order)");
    bool matches3d = pts.size() == uv.size();
    for (size_t i = 0; matches3d && i < pts.size(); ++i) matches3d = pts[i].x == uv[i].x && pts[i].y == uv[i].y;
    d8test::Check(matches3d, "...with the 3D points and (u, v) kept in step");
  }
}
