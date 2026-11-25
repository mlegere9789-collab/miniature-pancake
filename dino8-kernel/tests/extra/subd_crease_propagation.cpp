// Crease / semi-sharp propagation through SubD::Subdivide(): the tags a caller sets on the control net must survive refinement
// by the Catmull-Clark / Pixar rules, because every downstream consumer (BooleanToSubDCreased, ToNURBS, the app's SubD
// refinement) relies on it. Oracles are the published rules, not the library's own output:
//   * a crease edge splits into two crease edges per round, so n creases become n * 2^k after k rounds;
//   * a Corner stays a Corner with its crease count unchanged, the new midpoint of a crease edge is a Crease vertex with exactly
//     two crease edges (valence 4) lying ON the straight edge (cubic B-spline crease rule: the midpoint of a straight crease
//     edge is the average of its ends);
//   * a single crease edge (Dart) splits into two;
//   * semi-sharpness decays by exactly 1 per round, s' = max(s - 1, 0), and a per-end value interpolates linearly along the
//     split edge (ends (1, 3) give 2 at the midpoint before the decay, so 1 after round 1).
// (Not asserted: where the vertices of a semi-sharp edge sit after round 2. They leave the straight edge under OpenNURBS' vertex
// rule - e.g. the midpoint of a sharpness-3 edge is at 0.889 instead of 1 after two rounds - and no independent oracle for that
// blend was available, so only the sharpness values and counts are pinned.)
// These are characterisation tests: the behaviour already held when they were written (no kernel change), they pin it.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::SubDVertexInfo;
using dino8::kernel::SubDVertexKind;

dino8::kernel::Mesh CpBox() {  // cube [-1, 1]^3, outward quads
  dino8::kernel::Mesh m;
  ON_Mesh& raw = m.raw();
  const double s[2] = {-1.0, 1.0};
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(float(s[x]), float(s[y]), float(s[z])));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

SubD Refined(const SubD& s, int rounds) {
  SubD r = s;
  r.Subdivide(rounds);
  return r;
}

}  // namespace

D8_EXTRA_TEST(SubDSubdivideDoublesCreaseEdgesAndKeepsCornersAndCreaseMidpoints) {
  SubD box = SubD::FromControlMesh(CpBox());
  box.SetCreasesByDihedral(30.0);
  d8test::Check(box.CreaseEdgeCount() == 12, "the creased cube has 12 crease edges");
  const SubD r1 = Refined(box, 1), r2 = Refined(box, 2), r3 = Refined(box, 3);
  d8test::Check(r1.CreaseEdgeCount() == 24 && r2.CreaseEdgeCount() == 48 && r3.CreaseEdgeCount() == 96, "crease edges double per round: 24, 48, 96");
  const SubDVertexInfo corner = r1.VertexInfoAt(Point3d(1, 1, 1), 1e-4);
  d8test::Check(corner.found && corner.kind == SubDVertexKind::Corner && corner.crease_edge_count == 3, "the cube corner stays a Corner with its 3 crease edges (and stays at (1,1,1))");
  const SubDVertexInfo mid = r1.VertexInfoAt(Point3d(1, 1, 0), 1e-4);
  d8test::Check(mid.found && mid.kind == SubDVertexKind::Crease && mid.crease_edge_count == 2 && mid.valence == 4,
                "the new vertex at the middle of a crease edge is a Crease vertex: 2 crease edges, valence 4, exactly on the straight edge (1,1,0)");
  d8test::Check(r1.IsValid() && r1.Check().naked_edges == 0, "the refined creased cube is a valid closed SubD");
}

D8_EXTRA_TEST(SubDSubdivideSplitsASingleCreaseEdgeIntoTwo) {
  SubD box = SubD::FromControlMesh(CpBox());
  d8test::Check(box.SetCrease(Point3d(1, 1, -1), Point3d(1, 1, 1), true, 1e-6), "creased one edge");
  d8test::Check(Refined(box, 1).CreaseEdgeCount() == 2 && Refined(box, 2).CreaseEdgeCount() == 4 && Refined(box, 3).CreaseEdgeCount() == 8,
                "one crease edge (a Dart chain) becomes 2, 4, 8 crease edges");
  const SubDVertexInfo mid = Refined(box, 1).VertexInfoAt(Point3d(1, 1, 0), 1e-4);
  d8test::Check(mid.found && mid.kind == SubDVertexKind::Crease && mid.crease_edge_count == 2, "the split point is a Crease vertex with the 2 child crease edges");
}

// Largest sharpness seen at any vertex, and how many vertices see a semi-sharp edge end (positions are not assumed: a semi-sharp
// edge's vertices move under the vertex rule from the second round on, so vertices are enumerated through LimitPoints()).
static void SharpnessCensus(const SubD& s, double* max_sharp, int* vertices_with_sharp) {
  *max_sharp = 0.0;
  *vertices_with_sharp = 0;
  for (const dino8::kernel::SubDLimitPoint& lp : s.LimitPoints()) {
    const SubDVertexInfo v = s.VertexInfoAt(lp.control_point, 1e-9);
    if (v.found && v.semi_sharp_edge_count > 0) {
      ++*vertices_with_sharp;
      *max_sharp = std::max(*max_sharp, v.max_semi_sharpness);
    }
  }
}

D8_EXTRA_TEST(SubDSubdivideDecaysSemiSharpnessByOnePerRoundAndInterpolatesPerEndValues) {
  // Uniform sharpness s on one cube edge: after r rounds the edge is 2^r segments, every one with sharpness max(s - r, 0), so the
  // 2^r + 1 vertices along it see a semi-sharp edge while s - r > 0, and none once it is spent.
  const struct { double s; int round; } uniform[] = {{3.0, 1}, {3.0, 2}, {3.0, 3}, {1.5, 1}, {1.5, 2}, {1.0, 1}, {2.0, 2}};
  for (const auto& u : uniform) {
    SubD box = SubD::FromControlMesh(CpBox());
    box.SetEdgeSharpness(Point3d(1, 1, -1), Point3d(1, 1, 1), u.s, 1e-6);
    double mx = 0.0;
    int n = 0;
    SharpnessCensus(Refined(box, u.round), &mx, &n);
    const double expect = std::max(u.s - u.round, 0.0);
    const int expect_n = expect > 0.0 ? (1 << u.round) + 1 : 0;
    char what[200];
    std::snprintf(what, sizeof what, "uniform sharpness %.1f after %d round(s): largest sharpness %.1f on %d vertices (rule s' = max(s - 1, 0) per round, 2^r + 1 vertices)", u.s, u.round, expect, expect_n);
    d8test::Check(std::abs(mx - expect) < 1e-9 && n == expect_n, what);
  }
  // Per-end values (1 at z = -1, 3 at z = +1): the split edge's end values interpolate linearly, then lose 1 per round, so after
  // one round the largest is 3 - 1 = 2 (the z = +1 end) and the z = -1 end (1 - 1 = 0) no longer sees a semi-sharp edge.
  SubD box = SubD::FromControlMesh(CpBox());
  box.SetEdgeSharpness(Point3d(1, 1, -1), Point3d(1, 1, 1), 1.0, 3.0, 1e-6);
  double mx = 0.0;
  int n = 0;
  SharpnessCensus(Refined(box, 1), &mx, &n);
  d8test::Check(std::abs(mx - 2.0) < 1e-9, "per-end (1, 3): after one round the largest sharpness is 3 - 1 = 2");
  d8test::Check(n == 2, "...seen at the midpoint (mean 2, minus 1) and the z = +1 end only; the z = -1 end (1 - 1 = 0) sees none");
  SharpnessCensus(Refined(box, 3), &mx, &n);
  d8test::Check(mx < 1e-12 && n == 0, "per-end (1, 3): everything is spent after 3 rounds");
}
