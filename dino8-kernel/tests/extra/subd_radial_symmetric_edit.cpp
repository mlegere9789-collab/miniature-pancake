// SubD::CheckRadialSymmetry / MoveVertexRadial / SetCreaseRadial / SetEdgeSharpnessRadial: live editing of a cage with N-fold rotational symmetry.
//
// Oracles are independent of the new code:
//   * the fixture is a hand-built 2 x 2 quad grid over (i, j) in {-1,0,1}^2 with height z = i^2 + j^2 (centre 0, edge midpoints 1, corners 2):
//     it is 4-fold (and 2-fold) symmetric about the z axis by construction, and not 3-fold;
//   * "symmetric" is re-derived in the test from raw vertex positions with an explicit 90 degree map (x, y) -> (-y, x);
//   * Catmull-Clark commutes with a rotation, so limit(R v) = R limit(v): checked from SubD::LimitPoints(), not from the edit code;
//   * negative controls: the same edit done on ONE vertex only (a cage rebuilt with a single vertex moved) breaks every one of those.
#include <cmath>
#include <stdexcept>
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

const Point3d kAxisPoint(0, 0, 0);
const Vector3d kAxis(0, 0, 1);

int GridIndex(int i, int j) { return (j + 1) * 3 + (i + 1); }

// 3 x 3 vertices, 2 x 2 quads; `moved` (optional) replaces one vertex position to build asymmetric controls.
Mesh RadialGrid(int move_i = 99, int move_j = 99, Point3d moved = Point3d(0, 0, 0)) {
  Mesh m;
  ON_Mesh& raw = m.raw();
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i) {
      if (i == move_i && j == move_j) raw.m_V.Append(ON_3fPoint(float(moved.x), float(moved.y), float(moved.z)));
      else raw.m_V.Append(ON_3fPoint(float(i), float(j), float(i * i + j * j)));
    }
  for (int j = -1; j < 1; ++j)
    for (int i = -1; i < 1; ++i) {
      ON_MeshFace f;
      f.vi[0] = GridIndex(i, j); f.vi[1] = GridIndex(i + 1, j); f.vi[2] = GridIndex(i + 1, j + 1); f.vi[3] = GridIndex(i, j + 1);
      raw.m_F.Append(f);
    }
  return m;
}

SubD Grid() { return SubD::FromControlMesh(RadialGrid()); }

std::vector<Point3d> Positions(const SubD& s) {
  std::vector<Point3d> out;
  for (const SubDLimitPoint& lp : s.LimitPoints()) out.push_back(lp.control_point);
  return out;
}

Point3d Rot90(const Point3d& p) { return Point3d(-p.y, p.x, p.z); }

// Independent re-derivation of "4-fold symmetric": every vertex has a vertex at its 90 degree rotation.
bool FourFoldByPositions(const SubD& s, double tol) {
  const std::vector<Point3d> pts = Positions(s);
  for (const Point3d& p : pts) {
    const Point3d r = Rot90(p);
    bool found = false;
    for (const Point3d& q : pts) if (q.DistanceTo(r) <= tol) { found = true; break; }
    if (!found) return false;
  }
  return true;
}

// Worst | limit(R v) - R limit(v) | over all vertices, matching R v to a vertex by control position.
double LimitRotationError(const SubD& s) {
  const std::vector<SubDLimitPoint> lps = s.LimitPoints();
  double worst = 0.0;
  for (const SubDLimitPoint& a : lps) {
    const Point3d r = Rot90(a.control_point);
    const SubDLimitPoint* partner = nullptr;
    for (const SubDLimitPoint& b : lps) if (b.control_point.DistanceTo(r) < 1e-6) { partner = &b; break; }
    if (partner == nullptr) return 1e9;
    worst = std::max(worst, partner->limit_point.DistanceTo(Rot90(a.limit_point)));
  }
  return worst;
}

bool Has(const std::vector<Point3d>& pts, const Point3d& p, double tol = 1e-6) {
  for (const Point3d& q : pts) if (q.DistanceTo(p) <= tol) return true;
  return false;
}

}  // namespace

D8_EXTRA_TEST(SubDCheckRadialSymmetryOfAFourFoldGridAndOfBrokenOnes) {
  const SubD g = Grid();
  d8test::Check(g.VertexCount() == 9 && FourFoldByPositions(g, 1e-9), "oracle sanity: the grid has 9 vertices and is 4-fold symmetric by the test's own rotation map");
  const dino8::kernel::SubDSymmetryReport r4 = g.CheckRadialSymmetry(kAxisPoint, kAxis, 4);
  d8test::Check(r4.symmetric && r4.vertices == 9 && r4.on_plane == 1 && r4.paired == 8 && r4.unpaired == 0 && r4.worst_pair_distance < 1e-5,
                "CheckRadialSymmetry count=4: symmetric, 1 vertex on the axis, 8 paired, none unpaired");
  d8test::Check(g.CheckRadialSymmetry(kAxisPoint, kAxis, 2).symmetric, "count=2 (the 180 degree subgroup) is symmetric too");
  const dino8::kernel::SubDSymmetryReport r3 = g.CheckRadialSymmetry(kAxisPoint, kAxis, 3);
  d8test::Check(!r3.symmetric && r3.unpaired > 0, "count=3 is not a symmetry of a square grid: unpaired vertices reported");
  // a different axis (through (5,0,0)) is no symmetry axis of the grid
  d8test::Check(!g.CheckRadialSymmetry(Point3d(5, 0, 0), kAxis, 4).symmetric, "an axis through (5,0,0) is refused as a symmetry axis");
  // negative control: one corner moved by hand
  const SubD broken = SubD::FromControlMesh(RadialGrid(1, 1, Point3d(1.2, 0.9, 2.5)));
  const dino8::kernel::SubDSymmetryReport b = broken.CheckRadialSymmetry(kAxisPoint, kAxis, 4);
  d8test::Check(!b.symmetric && b.unpaired == 2 && !FourFoldByPositions(broken, 1e-6), "a grid with one corner moved: 2 unpaired vertices (the moved one and the one whose image it was)");
  bool threw = false;
  try { (void)g.CheckRadialSymmetry(kAxisPoint, Vector3d(0, 0, 0), 4); } catch (const std::invalid_argument&) { threw = true; }
  bool threw_count = false;
  try { (void)g.CheckRadialSymmetry(kAxisPoint, kAxis, 1); } catch (const std::invalid_argument&) { threw_count = true; }
  d8test::Check(threw && threw_count, "a zero axis and count < 2 are refused");
}

D8_EXTRA_TEST(SubDMoveVertexRadialKeepsTheCageAndItsLimitSurfaceRotationallySymmetric) {
  SubD g = Grid();
  const Point3d target(1.2, 0.9, 2.5);
  d8test::Check(g.MoveVertexRadial(Point3d(1, 1, 2), target, kAxisPoint, kAxis, 4), "moved the corner (1,1,2) to (1.2,0.9,2.5)");
  const std::vector<Point3d> pos = Positions(g);
  Point3d expect = target;
  bool all_images = true;
  for (int k = 0; k < 4; ++k) {  // the four corners are the orbit: target rotated by k * 90 degrees
    all_images = all_images && Has(pos, expect);
    expect = Rot90(expect);
  }
  d8test::Check(all_images, "all four corners sit at the 0/90/180/270 degree rotations of the target (explicit (x,y)->(-y,x) map)");
  d8test::Check(!Has(pos, Point3d(1, 1, 2)) && !Has(pos, Point3d(-1, 1, 2)), "the old corner positions are gone");
  d8test::Check(FourFoldByPositions(g, 1e-6) && g.CheckRadialSymmetry(kAxisPoint, kAxis, 4).symmetric, "the cage is still 4-fold symmetric (test's own check and CheckRadialSymmetry)");
  d8test::Check(LimitRotationError(g) < 1e-9, "the limit points are rotated images: limit(R v) = R limit(v) to 1e-9");
  SubD refined = g;
  refined.Subdivide(2);
  d8test::Check(FourFoldByPositions(refined, 1e-6) && LimitRotationError(refined) < 1e-9, "two Catmull-Clark rounds later the cage and its limit points are still 4-fold symmetric");
  // count = 2 moves only the opposite corner
  SubD h = Grid();
  d8test::Check(h.MoveVertexRadial(Point3d(1, 1, 2), target, kAxisPoint, kAxis, 2), "count=2 move");
  const std::vector<Point3d> hp = Positions(h);
  d8test::Check(Has(hp, target) && Has(hp, Point3d(-target.x, -target.y, target.z)) && Has(hp, Point3d(-1, 1, 2)) && Has(hp, Point3d(1, -1, 2)),
                "count=2 moves the corner and its 180 degree partner only; the other two corners stay");
  // negative control
  const SubD broken = SubD::FromControlMesh(RadialGrid(1, 1, target));
  d8test::Check(!FourFoldByPositions(broken, 1e-6) && LimitRotationError(broken) > 1e-3, "control: moving only one corner breaks the symmetry (limit rotation error above 1e-3)");
}

D8_EXTRA_TEST(SubDMoveVertexRadialConstrainsAnOnAxisVertexAndRefusesWithoutAnImage) {
  SubD g = Grid();
  d8test::Check(g.MoveVertexRadial(Point3d(0, 0, 0), Point3d(0.5, 0.3, 1.0), kAxisPoint, kAxis, 4), "moved the on-axis vertex (0,0,0)");
  d8test::Check(Has(Positions(g), Point3d(0, 0, 1.0), 1e-6) && FourFoldByPositions(g, 1e-6), "it landed on the axis at (0,0,1): x and y of the request are projected away, the cage stays symmetric");
  SubD broken = SubD::FromControlMesh(RadialGrid(1, 1, Point3d(1.2, 0.9, 2.5)));
  const std::vector<Point3d> before = Positions(broken);
  d8test::Check(!broken.MoveVertexRadial(Point3d(1.2, 0.9, 2.5), Point3d(2, 2, 2), kAxisPoint, kAxis, 4), "a vertex whose rotated images are missing is refused");
  const std::vector<Point3d> after = Positions(broken);
  bool same = before.size() == after.size();
  for (size_t i = 0; same && i < before.size(); ++i) same = before[i].DistanceTo(after[i]) < 1e-12;
  d8test::Check(same, "...and nothing moved");
  d8test::Check(!g.MoveVertexRadial(Point3d(9, 9, 9), Point3d(0, 0, 0), kAxisPoint, kAxis, 4), "a point that is not a vertex is refused");
}

D8_EXTRA_TEST(SubDCreaseAndSharpnessEditsAreRotatedAndRepairAnAsymmetry) {
  SubD g = Grid();
  const int base = g.CreaseEdgeCount();
  d8test::Check(g.SetCreaseRadial(Point3d(0, 0, 0), Point3d(1, 0, 1), true, kAxisPoint, kAxis, 4), "creased the spoke (0,0,0)-(1,0,1) radially");
  d8test::Check(g.CreaseEdgeCount() == base + 4, "four spokes are creased now (the edge and its three rotations)");
  d8test::Check(g.CheckRadialSymmetry(kAxisPoint, kAxis, 4).symmetric, "the cage is still 4-fold symmetric");
  d8test::Check(g.SetCreaseRadial(Point3d(0, 0, 0), Point3d(1, 0, 1), false, kAxisPoint, kAxis, 4) && g.CreaseEdgeCount() == base, "removing it radially smooths all four again");
  // one-sided edit, then the radial call repairs it
  d8test::Check(g.SetCrease(Point3d(0, 0, 0), Point3d(0, 1, 1), true, 1e-6) && g.CreaseEdgeCount() == base + 1, "creased only the spoke (0,0,0)-(0,1,1)");
  const dino8::kernel::SubDSymmetryReport asym = g.CheckRadialSymmetry(kAxisPoint, kAxis, 4);
  d8test::Check(!asym.symmetric && asym.crease_mismatch == 2, "CheckRadialSymmetry reports 2 crease mismatches (the creased spoke and its pre-image differ from their images)");
  d8test::Check(g.SetCreaseRadial(Point3d(0, 0, 0), Point3d(0, 1, 1), true, kAxisPoint, kAxis, 4) && g.CreaseEdgeCount() == base + 4 && g.CheckRadialSymmetry(kAxisPoint, kAxis, 4).symmetric,
                "the radial call creases the other three spokes and the cage is symmetric again");
  d8test::Check(!g.SetCreaseRadial(Point3d(0, 0, 0), Point3d(0, 1, 1), true, kAxisPoint, kAxis, 4), "repeating it changes nothing (false)");
  d8test::Check(!g.SetCreaseRadial(Point3d(0, 0, 0), Point3d(9, 9, 9), true, kAxisPoint, kAxis, 4), "an edge that does not exist is refused");
  // semi-sharp weight on a fresh grid: the four spokes read back 2
  SubD s = Grid();
  d8test::Check(s.SetEdgeSharpnessRadial(Point3d(0, 0, 0), Point3d(0, -1, 1), 2.0, kAxisPoint, kAxis, 4), "gave the spoke (0,0,0)-(0,-1,1) sharpness 2 radially");
  const Point3d tips[4] = {Point3d(1, 0, 1), Point3d(0, 1, 1), Point3d(-1, 0, 1), Point3d(0, -1, 1)};
  bool all_two = true;
  for (const Point3d& t : tips) {
    const dino8::kernel::SubDEdgeSharpnessInfo info = s.EdgeSharpnessAt(Point3d(0, 0, 0), t, 1e-6);
    all_two = all_two && info.found && std::abs(info.sharpness_at_p0 - 2.0) < 1e-12;
  }
  d8test::Check(all_two && s.CheckRadialSymmetry(kAxisPoint, kAxis, 4).symmetric, "all four spokes read back sharpness 2 and the cage is symmetric");
  d8test::Check(LimitRotationError(s) < 1e-9, "the semi-sharp limit surface is rotation symmetric: limit(R v) = R limit(v) to 1e-9");
}
