// SubD::CheckSymmetry / MoveVertexSymmetric / SetCreaseSymmetric / SetEdgeSharpnessSymmetric: live (step-by-step) mirror-constrained editing.
//
// Oracles are independent of the new code:
//   * the fixture is built by Symmetrize() of a box across its own x = 2 face (a 4 x 2 x 2 box: 12 vertices, 4 of them on the plane);
//   * "symmetric" is re-derived in the test from the raw vertex positions (every off-plane vertex has a vertex at its reflection);
//   * Catmull-Clark commutes with a reflection, so for a symmetric cage the limit point of the mirror vertex is the reflection of the limit
//     point of the vertex, and Subdivide() keeps the cage symmetric - both checked from SubD::LimitPoints() / Subdivide(), not from the edit code;
//   * negative control: the same single-vertex move done WITHOUT the symmetric call (a cage rebuilt with one vertex moved) breaks both.
#include <cmath>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::SubDLimitPoint;
using dino8::kernel::Vector3d;

const Vector3d kN(1, 0, 0);
const double kOffset = 2.0;

Mesh SeBox(double x0, double y0, double z0, double x1, double y1, double z1) {
  Mesh m;
  ON_Mesh& raw = m.raw();
  const double xs[2] = {x0, x1}, ys[2] = {y0, y1}, zs[2] = {z0, z1};
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(float(xs[x]), float(ys[y]), float(zs[z])));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

SubD SymmetricBox() {
  const SubD half = SubD::FromControlMesh(SeBox(0, 0, 0, 2, 2, 2));
  return half.Symmetrize(kN, kOffset, 1e-9, 1e-4);
}

std::vector<Point3d> Positions(const SubD& s) {
  std::vector<Point3d> out;
  for (const SubDLimitPoint& lp : s.LimitPoints()) out.push_back(lp.control_point);
  return out;
}

// Independent re-derivation of "mirror symmetric": every vertex has a vertex at its reflection across x = 2.
bool SymmetricByPositions(const SubD& s, double tol) {
  const std::vector<Point3d> pts = Positions(s);
  for (const Point3d& p : pts) {
    const Point3d r(2.0 * kOffset - p.x, p.y, p.z);
    bool found = false;
    for (const Point3d& q : pts) if (q.DistanceTo(r) <= tol) { found = true; break; }
    if (!found) return false;
  }
  return true;
}

// Worst | limit(mirror v) - reflect(limit v) | over all vertices, matching a vertex to its mirror by control position.
double LimitMirrorError(const SubD& s) {
  const std::vector<SubDLimitPoint> lps = s.LimitPoints();
  double worst = 0.0;
  for (const SubDLimitPoint& a : lps) {
    const Point3d r(2.0 * kOffset - a.control_point.x, a.control_point.y, a.control_point.z);
    const SubDLimitPoint* partner = nullptr;
    for (const SubDLimitPoint& b : lps) if (b.control_point.DistanceTo(r) < 1e-6) { partner = &b; break; }
    if (partner == nullptr) return 1e9;
    const Point3d expect(2.0 * kOffset - a.limit_point.x, a.limit_point.y, a.limit_point.z);
    worst = std::max(worst, partner->limit_point.DistanceTo(expect));
  }
  return worst;
}

}  // namespace

D8_EXTRA_TEST(SubDCheckSymmetryOfASymmetrizedBoxAndOfABrokenOne) {
  const SubD box = SymmetricBox();
  d8test::Check(box.VertexCount() == 12 && SymmetricByPositions(box, 1e-9), "oracle sanity: the Symmetrize()d box has 12 vertices and is symmetric by the test's own position check");
  const dino8::kernel::SubDSymmetryReport r = box.CheckSymmetry(kN, kOffset);
  d8test::Check(r.symmetric && r.vertices == 12 && r.on_plane == 4 && r.paired == 8 && r.unpaired == 0 && r.worst_pair_distance < 1e-9, "CheckSymmetry: symmetric, 4 vertices on the plane, 8 paired, none unpaired");
  // negative control: the same box built whole, with one off-plane vertex moved
  Mesh whole = SeBox(0, 0, 0, 4, 2, 2);
  whole.raw().m_V[0] = ON_3fPoint(-0.3f, 0.2f, 0.1f);
  const SubD broken = SubD::FromControlMesh(whole);
  const dino8::kernel::SubDSymmetryReport b = broken.CheckSymmetry(kN, kOffset);
  d8test::Check(!b.symmetric && b.unpaired == 2 && !SymmetricByPositions(broken, 1e-6), "a box with one vertex moved is not symmetric: CheckSymmetry reports 2 unpaired vertices (the moved one and its old partner)");
  bool threw = false;
  try { (void)box.CheckSymmetry(Vector3d(0, 0, 0), 0.0); } catch (const std::invalid_argument&) { threw = true; }
  d8test::Check(threw, "a zero plane normal is refused");
}

D8_EXTRA_TEST(SubDMoveVertexSymmetricKeepsTheCageAndItsLimitSurfaceMirrorSymmetric) {
  SubD box = SymmetricBox();
  d8test::Check(box.MoveVertexSymmetric(Point3d(0, 0, 0), Point3d(-0.3, 0.2, 0.1), kN, kOffset), "moved the vertex (0,0,0) to (-0.3,0.2,0.1)");
  bool at_target = false, partner_moved = false;
  for (const Point3d& p : Positions(box)) {
    if (p.DistanceTo(Point3d(-0.3, 0.2, 0.1)) < 1e-6) at_target = true;
    if (p.DistanceTo(Point3d(4.3, 0.2, 0.1)) < 1e-6) partner_moved = true;
  }
  d8test::Check(at_target && partner_moved, "the vertex is at (-0.3,0.2,0.1) and its partner at the reflection (4.3,0.2,0.1)");
  d8test::Check(SymmetricByPositions(box, 1e-6) && box.CheckSymmetry(kN, kOffset).symmetric, "the cage is still symmetric (test's own position check and CheckSymmetry)");
  d8test::Check(LimitMirrorError(box) < 1e-9, "the limit points are mirror images: limit(mirror v) = reflect(limit v) to 1e-9");
  SubD refined = box;
  refined.Subdivide(2);
  d8test::Check(SymmetricByPositions(refined, 1e-6) && LimitMirrorError(refined) < 1e-9, "two Catmull-Clark rounds later the cage and its limit points are still mirror symmetric");
  // negative control: only one vertex moved (what a plain single-vertex edit does)
  Mesh whole = SeBox(0, 0, 0, 4, 2, 2);
  whole.raw().m_V[0] = ON_3fPoint(-0.3f, 0.2f, 0.1f);
  const SubD broken = SubD::FromControlMesh(whole);
  d8test::Check(!SymmetricByPositions(broken, 1e-6) && LimitMirrorError(broken) > 1e-3, "control: moving only one vertex breaks the symmetry (limit mirror error above 1e-3)");
}

D8_EXTRA_TEST(SubDMoveVertexSymmetricConstrainsAnOnPlaneVertexAndRefusesWithoutAPartner) {
  SubD box = SymmetricBox();
  d8test::Check(box.MoveVertexSymmetric(Point3d(2, 0, 0), Point3d(2.5, 0.2, 0.3), kN, kOffset), "moved the on-plane vertex (2,0,0)");
  bool constrained = false;
  for (const Point3d& p : Positions(box)) if (p.DistanceTo(Point3d(2.0, 0.2, 0.3)) < 1e-9) constrained = true;
  d8test::Check(constrained && SymmetricByPositions(box, 1e-6), "it landed on the plane (x = 2: the requested x = 2.5 is projected away) and the cage stays symmetric");
  Mesh whole = SeBox(0, 0, 0, 4, 2, 2);
  whole.raw().m_V[0] = ON_3fPoint(-0.3f, 0.2f, 0.1f);
  SubD broken = SubD::FromControlMesh(whole);
  const std::vector<Point3d> before = Positions(broken);
  d8test::Check(!broken.MoveVertexSymmetric(Point3d(-0.3, 0.2, 0.1), Point3d(-0.5, 0, 0), kN, kOffset), "a vertex without a mirror partner is refused");
  const std::vector<Point3d> after = Positions(broken);
  bool same = before.size() == after.size();
  for (size_t i = 0; same && i < before.size(); ++i) same = before[i].DistanceTo(after[i]) < 1e-12;
  d8test::Check(same, "...and nothing moved");
  d8test::Check(!box.MoveVertexSymmetric(Point3d(9, 9, 9), Point3d(0, 0, 0), kN, kOffset), "a point that is not a vertex is refused");
}

D8_EXTRA_TEST(SubDCreaseAndSharpnessEditsAreMirroredAndRepairAnAsymmetry) {
  SubD box = SymmetricBox();
  d8test::Check(box.SetCreaseSymmetric(Point3d(0, 0, 0), Point3d(0, 2, 0), true, kN, kOffset), "creased the edge (0,0,0)-(0,2,0) symmetrically");
  d8test::Check(box.CreaseEdgeCount() == 2 && box.CheckSymmetry(kN, kOffset).symmetric, "two crease edges now (the edge and its mirror (4,0,0)-(4,2,0)) and the cage is symmetric");
  d8test::Check(box.SetEdgeSharpnessSymmetric(Point3d(0, 0, 2), Point3d(0, 2, 2), 2.0, kN, kOffset), "gave the edge (0,0,2)-(0,2,2) sharpness 2 symmetrically");
  const dino8::kernel::SubDEdgeSharpnessInfo a = box.EdgeSharpnessAt(Point3d(0, 0, 2), Point3d(0, 2, 2), 1e-6);
  const dino8::kernel::SubDEdgeSharpnessInfo b = box.EdgeSharpnessAt(Point3d(4, 0, 2), Point3d(4, 2, 2), 1e-6);
  d8test::Check(a.found && b.found && std::abs(a.sharpness_at_p0 - 2.0) < 1e-12 && std::abs(b.sharpness_at_p0 - 2.0) < 1e-12 && box.CheckSymmetry(kN, kOffset).symmetric,
                "both mirror edges read back sharpness 2 and the cage is symmetric");
  // an asymmetry made by a one-sided edit, repaired by the symmetric call
  d8test::Check(box.SetCrease(Point3d(0, 0, 0), Point3d(0, 0, 2), true, 1e-6), "creased only the edge (0,0,0)-(0,0,2) (one-sided)");
  d8test::Check(box.CheckSymmetry(kN, kOffset).crease_mismatch == 1 && !box.CheckSymmetry(kN, kOffset).symmetric, "CheckSymmetry reports the 1 crease mismatch");
  d8test::Check(box.SetCreaseSymmetric(Point3d(0, 0, 0), Point3d(0, 0, 2), true, kN, kOffset) && box.CheckSymmetry(kN, kOffset).symmetric, "the symmetric call creases the mirror edge too and the cage is symmetric again");
  d8test::Check(!box.SetCreaseSymmetric(Point3d(0, 0, 0), Point3d(0, 0, 2), true, kN, kOffset), "repeating it changes nothing (false)");
  d8test::Check(!box.SetCreaseSymmetric(Point3d(0, 0, 0), Point3d(9, 9, 9), true, kN, kOffset), "an edge that does not exist is refused");
}
