// SubD::BooleanToSubDCreased() with a SEMI-SHARP operand.
//
// An edge sharpness s only changes the shape once s Catmull-Clark rounds have consumed it. The method used to take an
// operand "at levels 0" as its raw cage, so a cube whose 12 edges carry sharpness 2 (limit volume ~60.3 of the 64 cage
// volume: rounded corners and edges) entered the mesh Boolean as the full 4-cube, and the result came back far too large
// (the union with a creased cube measured 152 against 98). These tests fail on that behaviour and pin the contract
// "an operand is refined at least ceil(largest edge sharpness) rounds".
//
// Oracles are independent of the method under test: a REFERENCE built from the operands subdivided 5 rounds by
// SubD::Subdivide() (whose volume converges on the true limit shape), combined by the mesh Boolean, and for the disjoint
// case an exact sum of volumes (limit volume of the semi-sharp cube + 1 for a 1x1x1 creased cube far away).
#include <cmath>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombine;
using dino8::kernel::BooleanOp;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::SubDBooleanReport;

Mesh SsBoxMesh(double x0, double y0, double z0, double x1, double y1, double z1) {
  Mesh m;
  ON_Mesh& raw = m.raw();
  const double xs[2] = {x0, x1}, ys[2] = {y0, y1}, zs[2] = {z0, z1};
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x)
        raw.m_V.Append(ON_3fPoint(static_cast<float>(xs[x]), static_cast<float>(ys[y]), static_cast<float>(zs[z])));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

// A cube cage; `crease_deg` > 0 creases its 12 edges, `sharpness` > 0 gives the 12 edges a semi-sharp weight instead.
SubD SsCube(double x0, double y0, double z0, double x1, double y1, double z1, bool creased, double sharpness) {
  SubD s = SubD::FromControlMesh(SsBoxMesh(x0, y0, z0, x1, y1, z1));
  if (creased) s.SetCreasesByDihedral(30.0);
  if (sharpness > 0.0) {
    const double X[2] = {x0, x1}, Y[2] = {y0, y1}, Z[2] = {z0, z1};
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 2; ++j) {
        s.SetEdgeSharpness(Point3d(X[i], Y[j], z0), Point3d(X[i], Y[j], z1), sharpness, 1e-6);
        s.SetEdgeSharpness(Point3d(x0, Y[i], Z[j]), Point3d(x1, Y[i], Z[j]), sharpness, 1e-6);
        s.SetEdgeSharpness(Point3d(X[i], y0, Z[j]), Point3d(X[i], y1, Z[j]), sharpness, 1e-6);
      }
    }
  }
  return s;
}

double RefinedVolume(const SubD& s, int rounds) {
  SubD r = s;
  r.Subdivide(rounds);
  return r.ToApproximateMesh().Volume();
}

// Volume of the Boolean of the two operands' limit shapes, each subdivided 5 rounds (independent of the method).
double ReferenceVolume(const SubD& a, const SubD& b, BooleanOp op) {
  SubD ra = a, rb = b;
  ra.Subdivide(5);
  rb.Subdivide(5);
  return BooleanCombine(ra.ToApproximateMesh(), rb.ToApproximateMesh(), op).Volume();
}

double ResultVolume(const SubD& s) { return RefinedVolume(s, 3); }

}  // namespace

D8_EXTRA_TEST(SubDBooleanSemiSharpOperandAtLevelZeroMatchesTheLimitShapeBoolean) {
  // a: 4-cube with sharpness-2 edges (rounded); b: a sharp (creased) 4-cube offset by (1,1,1).
  const SubD a = SsCube(0, 0, 0, 4, 4, 4, false, 2.0);
  const SubD b = SsCube(1, 1, 1, 5, 5, 5, true, 0.0);
  const double limit_a = RefinedVolume(a, 5);
  d8test::Check(limit_a > 59.0 && limit_a < 61.5, "oracle sanity: the sharpness-2 cube's limit volume is ~60.3, well below its 64 cage volume");
  const struct { BooleanOp op; const char* name; } ops[] = {
      {BooleanOp::Union, "Union"}, {BooleanOp::Difference, "Difference"}, {BooleanOp::Intersection, "Intersection"}};
  for (const auto& o : ops) {
    const double ref = ReferenceVolume(a, b, o.op);
    SubDBooleanReport rep;
    const SubD r = a.BooleanToSubDCreased(b, o.op, 0, 0, 0.0, 3.0, true, &rep);
    const double v = ResultVolume(r);
    char what[200];
    std::snprintf(what, sizeof what, "semi-sharp operand at requested levels (0,0): %s volume %.3f within 2%% of the limit-shape reference %.3f", o.name, v, ref);
    d8test::Check(std::abs(v - ref) <= 0.02 * ref, what);
    std::snprintf(what, sizeof what, "%s: the sharpness-2 operand was refined 2 rounds, the creased one 0 (report %d / %d)", o.name, rep.levels_this_used, rep.levels_other_used);
    d8test::Check(rep.levels_this_used == 2 && rep.levels_other_used == 0, what);
    d8test::Check(r.IsValid() && r.Check().naked_edges == 0 && r.Check().non_manifold_edges == 0,
                  "the result is a valid closed manifold SubD");
  }
}

D8_EXTRA_TEST(SubDBooleanSemiSharpOperandDisjointUnionIsLimitVolumePlusExactOne) {
  // Disjoint union: volume = limit volume of the semi-sharp cube + exactly 1 (a creased 1x1x1 cube is exact).
  const SubD a = SsCube(0, 0, 0, 4, 4, 4, false, 2.0);
  const SubD c = SsCube(20, 0, 0, 21, 1, 1, true, 0.0);
  const double expected = RefinedVolume(a, 5) + 1.0;
  SubDBooleanReport rep;
  const SubD u = a.BooleanToSubDCreased(c, BooleanOp::Union, 0, 0, 0.0, 3.0, true, &rep);
  const double v = ResultVolume(u);
  d8test::Check(std::abs(v - expected) <= 0.015 * expected, "disjoint union with a semi-sharp operand: limit volume of that operand + 1 within 1.5%");
  d8test::Check(v < 64.0, "the semi-sharp cube does not enter the Boolean as its 64-volume cage");
  d8test::Check(rep.levels_this_used == 2 && rep.levels_other_used == 0, "report: 2 rounds on the semi-sharp operand, 0 on the creased one");
}

D8_EXTRA_TEST(SubDBooleanSemiSharpLevelsAreRaisedOnlyWhenTheRequestIsLower) {
  const SubD c = SsCube(20, 0, 0, 21, 1, 1, true, 0.0);
  struct Case { double sharp; int req; int expect; const char* what; };
  const Case cases[] = {
      {1.0, 0, 1, "sharpness 1, requested 0 -> 1 round"},
      {1.5, 0, 2, "sharpness 1.5, requested 0 -> 2 rounds (ceil)"},
      {4.0, 0, 4, "sharpness 4 (the maximum), requested 0 -> 4 rounds"},
      {1.0, 3, 3, "sharpness 1, requested 3 -> stays 3 (never lowered)"},
      {2.0, 1, 2, "sharpness 2, requested 1 -> 2 rounds"},
  };
  for (const Case& k : cases) {
    const SubD a = SsCube(0, 0, 0, 4, 4, 4, false, k.sharp);
    SubDBooleanReport rep;
    (void)a.BooleanToSubDCreased(c, BooleanOp::Union, k.req, 0, 0.0, 3.0, false, &rep);
    d8test::Check(rep.levels_this_used == k.expect && rep.levels_other_used == 0, k.what);
  }
}

D8_EXTRA_TEST(SubDBooleanWithoutSemiSharpEdgesKeepsTheRequestedLevelsAndExactCreasedVolume) {
  // Control: two creased cubes, requested (0,0). Nothing is raised, and the creased union is the exact polyhedron
  // union 4^3 + 4^3 - 3^3 = 101 (closed form).
  const SubD a = SsCube(0, 0, 0, 4, 4, 4, true, 0.0);
  const SubD b = SsCube(1, 1, 1, 5, 5, 5, true, 0.0);
  SubDBooleanReport rep;
  const SubD u = a.BooleanToSubDCreased(b, BooleanOp::Union, 0, 0, 0.0, 3.0, true, &rep);
  d8test::Check(rep.levels_this_used == 0 && rep.levels_other_used == 0, "creased-only operands: requested levels are used unchanged");
  d8test::Check(std::abs(ResultVolume(u) - 101.0) < 0.1, "creased union volume is the closed form 101");
}
