// SubD::BooleanSubD(): union / intersection / difference of two SubDs that stays SubD, with creased seams.
//
// Oracles (all independent of the code under test):
//  * Two overlapping CREASED boxes are exactly the solid boxes at any subdivision level. A = [0,10]^3, B = [5,15]^3 (overlap [5,10]^3 = 125):
//    union 1000 + 1000 - 125 = 1875, intersection 125, A - B = 875, B - A = 875 - control-net volume at level 0 AND after Subdivide(2).
//    The plain BooleanToSubD() (no creases) rounds the result: its Subdivide(2) union is 1113, far below 1875 (the comparison).
//  * Smooth Catmull-Clark cube A = [0,10]^3 against a creased box B = [5,15] x [-5,15] x [-5,15] whose face x = 5 cuts A through its
//    middle, a symmetry plane of A: A & B = A - B = V/2 and A | B = vol(B) + V/2 = 4000 + V/2, V = A's own limit volume (read with the
//    same limit-point procedure). Measured deviation of the result's limit volume, levels 1 / 2 / 3:
//      intersection  -1.35 % / -0.48 % / +0.013 %      union  -0.054 % / -0.019 % / 0.000 %
//  * Inclusion-exclusion needs no formula: for ANY two solids vol(A | B) + vol(A & B) = vol(A) + vol(B) and vol(A - B) = vol(A) - vol(A & B).
//    Two smooth cubes, B = A moved by (5, 3, 2), levels 3: both identities hold to a fraction of a percent.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanOp;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::SubDBooleanReport;
using dino8::kernel::SubDLimitPoint;

Mesh BsBox(double x0, double y0, double z0, double sx, double sy, double sz) {  // axis-aligned box, outward quads
  Mesh m;
  ON_Mesh& raw = m.raw();
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(float(x0 + sx * x), float(y0 + sy * y), float(z0 + sz * z)));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

SubD BsSolid(double x0, double y0, double z0, double sx, double sy, double sz, bool creased) {
  SubD b = SubD::FromControlMesh(BsBox(x0, y0, z0, sx, sy, sz));
  if (creased) b.SetCreasesByDihedral(30.0);
  return b;
}

using Key = std::tuple<long, long, long>;
Key K(const Point3d& p) { return {std::lround(p.x * 1e5), std::lround(p.y * 1e5), std::lround(p.z * 1e5)}; }

// Closed-mesh volume of `s` refined `more` times with every control point moved to its limit point.
double LimitVolume(SubD s, int more) {
  if (more > 0) s.Subdivide(more);
  std::map<Key, Point3d> limit;
  for (const SubDLimitPoint& lp : s.LimitPoints()) limit[K(lp.control_point)] = lp.limit_point;
  Mesh me = s.ToApproximateMesh();
  ON_Mesh& r = me.raw();
  for (int i = 0; i < r.m_V.Count(); ++i) {
    const auto it = limit.find(K(Point3d(r.m_V[i])));
    if (it != limit.end()) r.m_V[i] = ON_3fPoint(float(it->second.x), float(it->second.y), float(it->second.z));
  }
  return me.IsClosedManifold() ? me.Volume() : -1.0;
}

double NetVolume(SubD s, int more) {
  if (more > 0) s.Subdivide(more);
  const Mesh m = s.ToApproximateMesh();
  return m.IsClosedManifold() ? m.Volume() : -1.0;
}

bool Near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

}  // namespace

D8_EXTRA_TEST(SubDBooleanSubDOfCreasedBoxesIsExactAtAnyLevel) {
  const SubD a = BsSolid(0, 0, 0, 10, 10, 10, true);
  const SubD b = BsSolid(5, 5, 5, 10, 10, 10, true);
  struct Case { BooleanOp op; double volume; const char* name; };
  const Case cases[3] = {{BooleanOp::Union, 1875.0, "union"}, {BooleanOp::Intersection, 125.0, "intersection"}, {BooleanOp::Difference, 875.0, "A - B"}};
  for (const Case& c : cases) {
    SubDBooleanReport rep;
    const SubD r = a.BooleanSubD(b, c.op, 3, &rep);
    char what[200];
    d8test::Check(r.IsValid() && r.Check().naked_edges == 0, (std::string(c.name) + ": valid and closed").c_str());
    d8test::Check(rep.levels_this_used == 0 && rep.levels_other_used == 0, (std::string(c.name) + ": both operands are exactly their cages, so no refinement is used (levels 3 asked, 0 used)").c_str());
    std::snprintf(what, sizeof what, "%s: control-net volume %.6f == %.0f", c.name, NetVolume(r, 0), c.volume);
    d8test::Check(Near(NetVolume(r, 0), c.volume, 1e-6), what);
    std::snprintf(what, sizeof what, "%s: volume after Subdivide(2) %.6f is still exactly %.0f (the seams and edges are creased)", c.name, NetVolume(r, 2), c.volume);
    d8test::Check(Near(NetVolume(r, 2), c.volume, 1e-6), what);
    d8test::Check(r.CreaseEdgeCount() >= 12, (std::string(c.name) + ": the sharp edges, including the seam, are creases (at least 12)").c_str());
  }
  const SubD ba = b.BooleanSubD(a, BooleanOp::Difference);
  d8test::Check(Near(NetVolume(ba, 2), 875.0, 1e-6), "B - A: exactly 875 as well");
  d8test::Check(a.BooleanSubD(b, BooleanOp::Intersection).CreaseEdgeCount() == 12, "the intersection is the box [5,10]^3: all 12 of its edges are creases");
  // comparison: the plain, uncreased BooleanToSubD rounds the same result
  const SubD plain = a.BooleanToSubD(b, BooleanOp::Union);
  d8test::Check(plain.CreaseEdgeCount() == 0 && NetVolume(plain, 2) < 1500.0, "control: BooleanToSubD (no creases) gives a rounded union, Subdivide(2) volume below 1500 instead of 1875");
}

D8_EXTRA_TEST(SubDBooleanSubDSmoothCubeAgainstCreasedBoxConvergesToHalfTheVolume) {
  const SubD a = BsSolid(0, 0, 0, 10, 10, 10, false);
  const SubD b = BsSolid(5, -5, -5, 10, 20, 20, true);
  const double v = LimitVolume(a, 5);
  d8test::Check(v > 300.0 && v < 335.0, "sanity: the smooth cube's limit volume is the rounded blob, about 327");
  double dev_i[4] = {0, 0, 0, 0};
  for (int levels = 1; levels <= 3; ++levels) {
    SubDBooleanReport rep;
    const SubD inter = a.BooleanSubD(b, BooleanOp::Intersection, levels, &rep);
    const SubD diff = a.BooleanSubD(b, BooleanOp::Difference, levels);
    const SubD uni = a.BooleanSubD(b, BooleanOp::Union, levels);
    d8test::Check(rep.levels_this_used == levels && rep.levels_other_used == 0, "the smooth operand is refined, the creased box is not");
    d8test::Check(inter.IsValid() && inter.Check().naked_edges == 0 && diff.IsValid() && uni.IsValid(), "all three results are valid closed SubDs");
    const double vi = LimitVolume(inter, 3), vd = LimitVolume(diff, 3), vu = LimitVolume(uni, 3);
    dev_i[levels] = std::abs(vi - v / 2) / (v / 2);
    std::printf("measured: levels %d  A&B %+.3f %%  A-B %+.3f %%  A|B %+.4f %%\n", levels, 100 * (vi - v / 2) / (v / 2), 100 * (vd - v / 2) / (v / 2), 100 * (vu - (4000 + v / 2)) / (4000 + v / 2));
    if (levels == 3) {
      d8test::Check(dev_i[3] < 0.002, "levels 3: the intersection is within 0.2 % of V/2");
      d8test::Check(std::abs(vd - v / 2) / (v / 2) < 0.002, "levels 3: the difference is within 0.2 % of V/2");
      d8test::Check(std::abs(vu - (4000 + v / 2)) / (4000 + v / 2) < 0.0005, "levels 3: the union is within 0.05 % of 4000 + V/2");
    }
    d8test::Check(inter.CreaseEdgeCount() >= 4 * (1 << (levels - 1)) * 2, "the seam (the cut rim) is creased");
  }
  d8test::Check(dev_i[1] > dev_i[2] && dev_i[2] > dev_i[3], "the intersection error shrinks with every refinement level");
  // control: the same operands through the plain BooleanToSubD (no creases): the seam is smooth and the result is rounded
  const SubD plain = a.BooleanToSubD(b, BooleanOp::Intersection);
  d8test::Check(plain.CreaseEdgeCount() == 0, "control: BooleanToSubD leaves the seam uncreased");
}

D8_EXTRA_TEST(SubDBooleanSubDTwoSmoothCubesSatisfyInclusionExclusion) {
  const SubD a = BsSolid(0, 0, 0, 10, 10, 10, false);
  const SubD b = BsSolid(5, 3, 2, 10, 10, 10, false);
  const double va = LimitVolume(a, 5), vb = LimitVolume(b, 5);
  d8test::Check(Near(va, vb, 0.05), "sanity: the moved cube has the same limit volume (within 0.05 of 327)");
  const double vu = LimitVolume(a.BooleanSubD(b, BooleanOp::Union, 3), 3);
  const double vi = LimitVolume(a.BooleanSubD(b, BooleanOp::Intersection, 3), 3);
  const double vd = LimitVolume(a.BooleanSubD(b, BooleanOp::Difference, 3), 3);
  std::printf("measured: smooth cubes: V = %.4f, union %.4f, intersection %.4f, difference %.4f\n", va, vu, vi, vd);
  d8test::Check(vi > 0.0 && vi < va, "the intersection is a non-empty part of A");
  d8test::Check(std::abs((vu + vi) - (va + vb)) / (va + vb) < 0.01, "vol(A|B) + vol(A&B) == vol(A) + vol(B) within 1 %");
  d8test::Check(std::abs(vd - (va - vi)) / va < 0.01, "vol(A-B) == vol(A) - vol(A&B) within 1 % of V");
}

D8_EXTRA_TEST(SubDBooleanSubDRejectsBadLevels) {
  const SubD a = BsSolid(0, 0, 0, 10, 10, 10, true);
  bool threw_low = false, threw_high = false;
  try { (void)a.BooleanSubD(a, BooleanOp::Union, -1); } catch (const std::invalid_argument&) { threw_low = true; }
  try { (void)a.BooleanSubD(a, BooleanOp::Union, 6); } catch (const std::invalid_argument&) { threw_high = true; }
  d8test::Check(threw_low && threw_high, "levels outside [0, 5] are rejected");
}
