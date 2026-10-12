#include <cmath>
#include <cstdio>
#include "dino8/kernel/curve.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // NurbsCurve::FindSelfIntersections(), exercised standalone against a
  // hand-built figure-8 (bowtie) polyline curve with a KNOWN crossing
  // point, before trusting the real registered test
  // (TestCurveFindSelfIntersections).

  // A simple bowtie: (0,0) -> (10,10) -> (10,0) -> (0,10) -> back to (0,0).
  // Segment 1 ((0,0)-(10,10)) and segment 3 ((10,0)-(0,10)) cross at
  // exactly (5,5) by symmetry - the one real self-intersection. Segment 2
  // ((10,10)-(10,0)) and segment 4 ((0,10)-(0,0)) don't cross anything.
  {
    const std::vector<Point3d> pts = {
        Point3d(0, 0, 0), Point3d(10, 10, 0), Point3d(10, 0, 0), Point3d(0, 10, 0), Point3d(0, 0, 0),
    };
    NurbsCurve c = NurbsCurve::FromControlPoints(pts, 1);
    const auto hits = c.FindSelfIntersections(1e-6);
    std::printf("bowtie: %zu hit(s)\n", hits.size());
    for (const auto& h : hits) {
      const Point3d pa = c.PointAt(h.first);
      const Point3d pb = c.PointAt(h.second);
      std::printf("  t_a=%.6f pt_a=(%.6f,%.6f,%.6f)  t_b=%.6f pt_b=(%.6f,%.6f,%.6f)\n", h.first, pa.x, pa.y, pa.z,
                  h.second, pb.x, pb.y, pb.z);
    }
  }

  // A plain square (no self-crossing) should report zero hits, including
  // correctly EXCLUDING its own closing seam.
  {
    const std::vector<Point3d> pts = {
        Point3d(0, 0, 0), Point3d(10, 0, 0), Point3d(10, 10, 0), Point3d(0, 10, 0), Point3d(0, 0, 0),
    };
    NurbsCurve c = NurbsCurve::FromControlPoints(pts, 1);
    std::printf("IsClosed=%d\n", c.IsClosed() ? 1 : 0);
    const auto hits = c.FindSelfIntersections(1e-6);
    std::printf("square (closed, no real crossing): %zu hit(s) (expect 0)\n", hits.size());
  }

  // Two genuinely distinct self-crossings (a "figure-8-of-8" zigzag) -
  // confirms the dedup merge above collapses sampling artifacts without
  // over-merging two real, separate crossings.
  {
    const std::vector<Point3d> pts = {
        Point3d(0, 0, 0),   Point3d(10, 10, 0), Point3d(20, 0, 0), Point3d(30, 10, 0),
        Point3d(10, 0, 0),  Point3d(0, 10, 0),
    };
    NurbsCurve c = NurbsCurve::FromControlPoints(pts, 1);
    std::printf("IsClosed(zigzag)=%d\n", c.IsClosed() ? 1 : 0);
    const auto hits = c.FindSelfIntersections(1e-6);
    std::printf("zigzag (open, expect 2 distinct crossings): %zu hit(s)\n", hits.size());
    for (const auto& h : hits) {
      const Point3d pa = c.PointAt(h.first);
      std::printf("  t_a=%.6f t_b=%.6f pt=(%.6f,%.6f,%.6f)\n", h.first, h.second, pa.x, pa.y, pa.z);
    }
  }

  // A non-positive tolerance should throw.
  {
    const std::vector<Point3d> pts = {Point3d(0, 0, 0), Point3d(1, 1, 0)};
    NurbsCurve c = NurbsCurve::FromControlPoints(pts, 1);
    try {
      c.FindSelfIntersections(0.0);
      std::printf("ERROR: expected throw for tolerance=0\n");
    } catch (const std::invalid_argument&) {
      std::printf("OK: tolerance<=0 throws invalid_argument\n");
    }
  }

  return 0;
}
