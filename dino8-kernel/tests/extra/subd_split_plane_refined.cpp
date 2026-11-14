// SubD::SplitByPlaneRefined() and the semi-sharp carry-over of SubD::SplitByPlane().
//
// 1. Refinement before the cut. The original is the smooth (uncreased) Catmull-Clark cube cage [0,10]^3; the plane is its mid-plane z = 5, a
//    symmetry plane of the limit surface, so each half's limit volume must be EXACTLY half the limit volume V of the uncut cube (an exact
//    oracle that needs no formula for V: V is read once, from the uncut cube, with the same procedure). A cage-level cut (levels = 0) puts
//    a creased rim through the control net, which is far from the limit surface; refining first brings the net onto the surface. Measured
//    (each half refined to a total of 5 levels, vertices moved to their limit points, closed-mesh volume; V = 327.3777):
//      levels 0: +51.9 %   levels 1: +5.18 %   levels 2: +0.667 %   levels 3: +0.091 %   (about 8x per level)
//    and the rim (the cut curve) half-width at y = 5 against the original surface's mid-plane half-width 9.1975:
//      levels 2: +0.0965   levels 3: +0.0241   (4x per level).
// 2. Semi-sharp carry-over. An edge cut at fraction t from vertex a keeps a's end value and the far end's value, and both pieces share the
//    linearly interpolated weight s_a + t (s_b - s_a) at the new vertex. Hand-derived for the edge (0,0,0)-(0,0,10) with weights (2, 3) cut at
//    z = 4 (t = 0.4): lower piece (2, 2.4), upper piece (2.4, 3); an edge with a constant weight keeps it on both pieces; an edge on one
//    side only goes to that side unchanged.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::SubDLimitPoint;
using dino8::kernel::SubDPlaneSplit;
using dino8::kernel::Vector3d;

Mesh SrBox() {  // cube [0,10]^3, outward quads
  Mesh m;
  ON_Mesh& raw = m.raw();
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(10.0f * x, 10.0f * y, 10.0f * z));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

using Key = std::tuple<long, long, long>;
Key K(const Point3d& p) { return {std::lround(p.x * 1e6), std::lround(p.y * 1e6), std::lround(p.z * 1e6)}; }

// Closed-mesh volume of `s` refined `more` times with every control point moved to its limit point; the largest x of a limit point on the plane z = 5.
double LimitVolume(SubD s, int more, double* xmax_on_mid_plane) {
  if (more > 0) s.Subdivide(more);
  std::map<Key, Point3d> limit;
  double xmax = -1e30;
  for (const SubDLimitPoint& lp : s.LimitPoints()) {
    limit[K(lp.control_point)] = lp.limit_point;
    if (std::abs(lp.limit_point.z - 5.0) < 1e-6) xmax = std::max(xmax, lp.limit_point.x);
  }
  if (xmax_on_mid_plane) *xmax_on_mid_plane = xmax;
  Mesh me = s.ToApproximateMesh();
  ON_Mesh& r = me.raw();
  for (int i = 0; i < r.m_V.Count(); ++i) {
    const auto it = limit.find(K(Point3d(r.m_V[i])));
    if (it != limit.end()) r.m_V[i] = ON_3fPoint(float(it->second.x), float(it->second.y), float(it->second.z));
  }
  return me.IsClosedManifold() ? me.Volume() : -1.0;
}

}  // namespace

D8_EXTRA_TEST(SubDSplitByPlaneRefinedConvergesToHalfTheLimitVolume) {
  const SubD cube = SubD::FromControlMesh(SrBox());
  const int total = 5;
  double x_whole = 0.0;
  const double v_whole = LimitVolume(cube, total, &x_whole);
  d8test::Check(v_whole > 300.0 && v_whole < 335.0, "sanity: the smooth cube's limit volume is the rounded blob, about 327");
  double dev[4] = {0, 0, 0, 0}, rim[4] = {0, 0, 0, 0};
  for (int levels = 0; levels <= 3; ++levels) {
    const SubDPlaneSplit cut = cube.SplitByPlaneRefined(Vector3d(0, 0, 1), 5.0, levels);
    d8test::Check(cut.has_positive && cut.has_negative && cut.cap_loops == 1, "both halves, one cap");
    const SubD* halves[2] = {&cut.positive, &cut.negative};
    for (const SubD* h : halves) {
      d8test::Check(h->IsValid() && h->Check().naked_edges == 0 && h->Check().IsManifoldSingleBody(), "half: valid, closed, manifold, one body");
      double x_half = 0.0;
      const double v = LimitVolume(*h, total - levels, &x_half);
      d8test::Check(v > 0.0, "half: the refined limit mesh is a closed manifold");
      dev[levels] = std::max(dev[levels], std::abs(v - v_whole / 2.0) / (v_whole / 2.0));
      rim[levels] = std::max(rim[levels], std::abs(x_half - x_whole));
    }
    std::printf("measured: levels %d  half-volume deviation %.4f %%   rim half-width deviation %.5f\n", levels, 100.0 * dev[levels], rim[levels]);
  }
  d8test::Check(dev[0] > 0.4, "a cage-level cut is far from the limit surface: the half is over 40 % too large");
  d8test::Check(dev[1] < dev[0] * 0.2 && dev[2] < dev[1] * 0.2 && dev[3] < dev[2] * 0.2, "each refinement level cuts the volume deviation by more than 5x");
  d8test::Check(dev[3] < 0.0015, "levels 3: the half volume is within 0.15 % of half the limit volume");
  d8test::Check(rim[2] < 0.12 && rim[3] < 0.03 && rim[3] < rim[2] * 0.3, "the rim half-width deviation is under 0.12 / 0.03 at levels 2 / 3 and drops by more than 3x");
}

D8_EXTRA_TEST(SubDSplitByPlaneCarriesSemiSharpWeightsToThePieces) {
  SubD cube = SubD::FromControlMesh(SrBox());
  d8test::Check(cube.SetEdgeSharpness(Point3d(0, 0, 0), Point3d(0, 0, 10), 2.0, 3.0, 1e-6), "sanity: weights (2, 3) on the vertical edge (0,0,0)-(0,0,10)");
  d8test::Check(cube.SetEdgeSharpness(Point3d(10, 10, 0), Point3d(10, 10, 10), 3.0, 3.0, 1e-6), "sanity: constant weight 3 on the vertical edge (10,10,0)-(10,10,10)");
  d8test::Check(cube.SetEdgeSharpness(Point3d(0, 0, 0), Point3d(10, 0, 0), 1.0, 1.0, 1e-6), "sanity: weight 1 on the bottom edge (0,0,0)-(10,0,0)");
  const SubDPlaneSplit cut = cube.SplitByPlane(Vector3d(0, 0, 1), 4.0);
  const auto lower = cut.negative.EdgeSharpnessAt(Point3d(0, 0, 0), Point3d(0, 0, 4), 1e-6);
  const auto upper = cut.positive.EdgeSharpnessAt(Point3d(0, 0, 4), Point3d(0, 0, 10), 1e-6);
  d8test::Check(lower.found && std::abs(lower.sharpness_at_p0 - 2.0) < 1e-9 && std::abs(lower.sharpness_at_p1 - 2.4) < 1e-6, "lower piece (0,0,0)-(0,0,4): weights (2, 2.4)");
  d8test::Check(upper.found && std::abs(upper.sharpness_at_p0 - 2.4) < 1e-6 && std::abs(upper.sharpness_at_p1 - 3.0) < 1e-9, "upper piece (0,0,4)-(0,0,10): weights (2.4, 3)");
  const auto c_lo = cut.negative.EdgeSharpnessAt(Point3d(10, 10, 0), Point3d(10, 10, 4), 1e-6);
  const auto c_hi = cut.positive.EdgeSharpnessAt(Point3d(10, 10, 4), Point3d(10, 10, 10), 1e-6);
  d8test::Check(c_lo.found && std::abs(c_lo.sharpness_at_p0 - 3.0) < 1e-9 && std::abs(c_lo.sharpness_at_p1 - 3.0) < 1e-9, "constant weight 3 stays (3, 3) on the lower piece");
  d8test::Check(c_hi.found && std::abs(c_hi.sharpness_at_p0 - 3.0) < 1e-9 && std::abs(c_hi.sharpness_at_p1 - 3.0) < 1e-9, "...and on the upper piece");
  const auto bottom = cut.negative.EdgeSharpnessAt(Point3d(0, 0, 0), Point3d(10, 0, 0), 1e-6);
  d8test::Check(bottom.found && std::abs(bottom.sharpness_at_p0 - 1.0) < 1e-9 && std::abs(bottom.sharpness_at_p1 - 1.0) < 1e-9, "an edge entirely below the plane keeps weight (1, 1) in the lower half");
  d8test::Check(!cut.positive.EdgeSharpnessAt(Point3d(0, 0, 0), Point3d(10, 0, 0), 1e-6).found, "...and is absent from the upper half");
}
