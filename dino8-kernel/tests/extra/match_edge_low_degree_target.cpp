// NurbsSurface::MatchEdge to a target whose degree across the edge is below the continuity's row count (a bilinear / ruled target for G2).
//
// It used to be refused ("a degree-1 target has no curvature row"). Degree elevation is exact, so the elevated copy is the target and its second cross
// derivative there is the target's own (zero for a degree-1 direction): the match is well defined and now runs.
//
// Target: the hyperbolic paraboloid z = x y over [0,1]^2 (a degree-1 x degree-1 surface). Closed forms, from the formula and not from the code under
// test (the candidate is never compared with its own printout):
//   * its edge y = 1 is the straight line z = x;
//   * its normal is (-y, -x, 1) / sqrt(1 + x^2 + y^2);
//   * its Gaussian curvature is K = -1 / (1 + x^2 + y^2)^2, so -1 / (2 + x^2)^2 on that edge.
// The candidate is a wiggly bicubic whose u = min edge starts 0.05 above the line, continuing away from the target (y from 1 to 2). After
// MatchEdge(G2) its edge must lie on the line z = x, y = 1 (to 1e-12), its normals must be the target's along it, and its OWN Gaussian curvature (read
// by OpenNURBS' curvature evaluator) must be the closed form -1 / (2 + x^2)^2. The refusals around it (a rational / unclamped target is still handled
// the same as before) are not touched.
#include <algorithm>
#include <cmath>
#include <vector>

#include "dino8/kernel/surface.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::MatchContinuity;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

// z = x y: CV(i, j) = grid[i * 2 + j] with u running along +y and v along +x.
NurbsSurface HyperbolicParaboloid() {
  const std::vector<Point3d> g = {Point3d(0, 0, 0), Point3d(1, 0, 0), Point3d(0, 1, 0), Point3d(1, 1, 1)};
  return NurbsSurface::FromControlGrid(g, 2, 2, 1, 1);
}

// 4 x 4 bicubic: u runs along +y from 1 to 2, v along +x from 0 to 1; the u = min row sits on the line z = x raised by 0.05.
NurbsSurface Candidate() {
  std::vector<Point3d> g;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      const double x = j / 3.0, y = 1.0 + i / 3.0;
      const double z = (i == 0 ? x + 0.05 : x * y) + 0.08 * std::sin(1.3 * i + 2.1 * j);
      g.push_back(Point3d(x, y, z));
    }
  }
  return NurbsSurface::FromControlGrid(g, 4, 4, 3, 3);
}

D8_EXTRA_TEST(MatchEdgeG2ToDegreeOneTarget) {
  const NurbsSurface target = HyperbolicParaboloid();
  NurbsSurface s = Candidate();
  d8test::Check(target.raw().Degree(0) == 1 && target.raw().Degree(1) == 1, "fixture: the target is degree 1 x 1");
  dino8::kernel::MatchEdgeReport rep;
  const Result r = s.MatchEdge(0, true, target, 0, false, MatchContinuity::Curvature, &rep);
  d8test::Check(r == Result::Ok, "MatchEdge(G2) to a degree-1 target now runs (it was refused as 'no curvature row')");
  if (r != Result::Ok) return;

  double on_line = 0, normal_cross = 0, k_err = 0;
  const ON_Interval du = s.raw().Domain(0), dv = s.raw().Domain(1);
  for (int k = 0; k <= 64; ++k) {
    const double v = dv.ParameterAt(k / 64.0);
    const ON_3dPoint p = s.raw().PointAt(du.Min(), v);
    on_line = std::max({on_line, std::abs(p.y - 1.0), std::abs(p.z - p.x)});
    const ON_3dVector nt(-1.0, -p.x, 1.0);  // (-y, -x, 1) at y = 1
    ON_3dPoint q;
    ON_3dVector su, sv;
    s.raw().Ev1Der(du.Min(), v, q, su, sv);
    ON_3dVector n = ON_CrossProduct(su, sv);
    n.Unitize();
    ON_3dVector t = nt;
    t.Unitize();
    normal_cross = std::max(normal_cross, ON_CrossProduct(n, t).Length());
    k_err = std::max(k_err, std::abs(s.CurvatureAt(du.Min(), v).gaussian - (-1.0 / ((2.0 + p.x * p.x) * (2.0 + p.x * p.x)))));
  }
  d8test::Check(on_line < 1e-12, "the matched edge lies on the target's edge line y = 1, z = x to 1e-12");
  d8test::Check(normal_cross < 1e-9, "the candidate's normals along the edge are the target's closed-form normal (-y, -x, 1)/|.|");
  d8test::Check(k_err < 1e-6, "the candidate's own Gaussian curvature along the edge is the closed form -1/(1 + x^2 + y^2)^2");
}

// The same call test_basic.cpp makes (TestSurfaceMatchEdge...: a 4 x 4 wiggly bicubic against the z = xy-style ruled target, edges v-min / u-max-ish
// with no geometric relation between the two): the curvature match must run, and a tangent match to the same target keeps running.
D8_EXTRA_TEST(MatchEdgeG2ToRuledTargetFromTestBasic) {
  std::vector<Point3d> g;
  for (int j = 0; j < 4; ++j)
    for (int i = 0; i < 4; ++i) g.push_back(Point3d(i, j, std::sin(1.7 * i) * std::cos(1.3 * j) + 0.3 * i * j));
  const NurbsSurface flat = NurbsSurface::FromControlGrid(g, 4, 4, 3, 3);
  const NurbsSurface ruled = HyperbolicParaboloid();
  NurbsSurface g2 = flat;
  d8test::Check(g2.MatchEdge(1, true, ruled, 1, false, MatchContinuity::Curvature) == Result::Ok,
                "a wiggly bicubic curvature-matches the degree-1 ruled target (test_basic's refusal check, now Ok)");
  NurbsSurface g1 = flat;
  d8test::Check(g1.MatchEdge(1, true, ruled, 1, false, MatchContinuity::Tangent) == Result::Ok, "...and tangent-matches it as before");
}

}  // namespace
