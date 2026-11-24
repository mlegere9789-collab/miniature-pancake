// Register rows of the kernel-geometry and direct-modeling factories, Blender object, view and transform-tool rows (round 4, batch 3).
// The oracle of every test is a hand-written point function: a matrix is built entry by entry from an explicit affine function f
// (column j = f(e_j) - f(0), last column f(0)), the expected image of a point is f(point) evaluated step by step (scale, rotate, move; or
// apply a parent, then its inverse written out), and the code under test must agree at several points. Snapping is checked against
// the written-out rounding of each coordinate, centres against the explicit mean / min-max, the tilt against (cos, sin), and the surface disc
// against the area pi r^2 and the radial law (1 - v) r. None of them marks a row verified. The semantics of the Blender operators that are
// not defined by arithmetic alone (delta-rotation order, the sign of a tilt, the factor of a slide, which direction is "push") are
// recalled, not read from the manual, and are flagged in the row notes as recalled, needs owner check.
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/object_transform.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;
namespace k = dino8::kernel;

const double kPi = 3.14159265358979323846;

using Fn = std::function<Point3d(const Point3d&)>;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }
std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }
bool Near(const Point3d& a, const Point3d& b, double tol) { return a.DistanceTo(b) <= tol; }
double VD(const Vector3d& a, const Vector3d& b) { return (a - b).Length(); }
bool NearRel(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::fabs(b); }

Point3d RotZ(const Point3d& p, double a) { return P(std::cos(a) * p.x - std::sin(a) * p.y, std::sin(a) * p.x + std::cos(a) * p.y, p.z); }
Point3d RotX(const Point3d& p, double a) { return P(p.x, std::cos(a) * p.y - std::sin(a) * p.z, std::sin(a) * p.y + std::cos(a) * p.z); }
Point3d Scale(const Point3d& p, double sx, double sy, double sz) { return P(p.x * sx, p.y * sy, p.z * sz); }
Point3d Move(const Point3d& p, double dx, double dy, double dz) { return P(p.x + dx, p.y + dy, p.z + dz); }

// The matrix of an explicit affine function, entry by entry.
ON_Xform FromFunction(const Fn& f) {
  const Point3d o = f(P(0, 0, 0));
  ON_Xform m = ON_Xform::IdentityTransformation;
  const Point3d e[3] = {f(P(1, 0, 0)), f(P(0, 1, 0)), f(P(0, 0, 1))};
  for (int j = 0; j < 3; ++j) {
    m[0][j] = e[j].x - o.x;
    m[1][j] = e[j].y - o.y;
    m[2][j] = e[j].z - o.z;
  }
  m[0][3] = o.x;
  m[1][3] = o.y;
  m[2][3] = o.z;
  return m;
}

Point3d Apply(const ON_Xform& m, const Point3d& p) { return m * ON_3dPoint(p); }

const std::vector<Point3d>& Probes() {
  static const std::vector<Point3d> pts = {P(0, 0, 0), P(1, 0, 0), P(0, 1, 0), P(0, 0, 1), P(2, -1, 3), P(-4, 2.5, -1.5)};
  return pts;
}

double WorstOver(const ON_Xform& m, const Fn& expect) {
  double worst = 0.0;
  for (const Point3d& p : Probes()) worst = std::max(worst, Apply(m, p).DistanceTo(expect(p)));
  return worst;
}

// An object matrix T(2,-3,5) Rz(0.7) Rx(-0.4) S(2,3,0.5) written as a function.
const double kAz = 0.7, kAx = -0.4;
Point3d LocalFn(const Point3d& p) { return Move(RotZ(RotX(Scale(p, 2, 3, 0.5), kAx), kAz), 2, -3, 5); }

}  // namespace

// OBJECT-0096 Clear Location, OBJECT-0162 Clear Rotation, OBJECT-0163 Clear Scale. The matrix T(2,-3,5) Rz(0.7) Rx(-0.4) S(2,3,0.5), built from its
// written-out function, splits into location (2,-3,5), scale (2,3,0.5) and a proper rotation; clearing one part leaves the other two: without the
// location the image of every probe point is LocalFn(p) - (2,-3,5); without the rotation it is S p + (2,-3,5); without the scale it is Rz Rx p + (2,-3,5)
// (1e-12 at six points). A mirror (determinant -1) still splits with a proper rotation and composes back; a sheared or singular matrix is refused.
// Which of the three parts Blender clears from selected objects and the "clear delta" option are not covered.
D8_EXTRA_TEST(RowBkgClearLocationRotationScaleKeepTheOtherParts) {
  const ON_Xform m = FromFunction(LocalFn);
  k::TrsParts parts;
  d8test::Check(k::DecomposeTrs(m, parts), "the matrix splits into location, rotation and scale");
  d8test::Check(VD(parts.location, Vector3d(2, -3, 5)) < 1e-12 && VD(parts.scale, Vector3d(2, 3, 0.5)) < 1e-12, "location (2,-3,5) and scale (2,3,0.5)");
  double rdev = 0.0;
  for (const Point3d& p : Probes()) rdev = std::max(rdev, Apply(parts.rotation, p).DistanceTo(RotZ(RotX(p, kAx), kAz)));
  d8test::Check(rdev < 1e-12, Msg("the rotation part is Rz(0.7) Rx(-0.4)", rdev).c_str());
  d8test::Check(WorstOver(k::ComposeTrs(parts), LocalFn) < 1e-12, "composing the parts gives the matrix back");
  d8test::Check(WorstOver(k::ClearLocation(m), [](const Point3d& p) { return Move(LocalFn(p), -2, 3, -5); }) < 1e-12, "clear location: the image minus (2,-3,5)");
  d8test::Check(WorstOver(k::ClearRotation(m), [](const Point3d& p) { return Move(Scale(p, 2, 3, 0.5), 2, -3, 5); }) < 1e-12, "clear rotation: S p + (2,-3,5)");
  d8test::Check(WorstOver(k::ClearScale(m), [](const Point3d& p) { return Move(RotZ(RotX(p, kAx), kAz), 2, -3, 5); }) < 1e-12, "clear scale: R p + (2,-3,5)");

  const auto mirrored = [](const Point3d& p) { return Move(RotZ(Scale(p, -1, 1, 1), 0.3), 1, 1, 1); };
  k::TrsParts mp;
  d8test::Check(k::DecomposeTrs(FromFunction(mirrored), mp), "a mirrored matrix splits");
  const double det = (mp.rotation[0][0] * (mp.rotation[1][1] * mp.rotation[2][2] - mp.rotation[1][2] * mp.rotation[2][1]) -
                      mp.rotation[0][1] * (mp.rotation[1][0] * mp.rotation[2][2] - mp.rotation[1][2] * mp.rotation[2][0]) +
                      mp.rotation[0][2] * (mp.rotation[1][0] * mp.rotation[2][1] - mp.rotation[1][1] * mp.rotation[2][0]));
  d8test::Check(std::fabs(det - 1.0) < 1e-12 && WorstOver(k::ComposeTrs(mp), mirrored) < 1e-12, "the rotation of a mirror is proper and the parts compose back");
  ON_Xform shear = ON_Xform::IdentityTransformation;
  shear[0][1] = 0.5;
  ON_Xform flat = ON_Xform::IdentityTransformation;
  flat[2][2] = 0.0;
  k::TrsParts none;
  bool threw = false;
  try {
    k::ClearRotation(shear);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  d8test::Check(!k::DecomposeTrs(shear, none) && !k::DecomposeTrs(flat, none) && threw, "a shear and a singular matrix have no location / rotation / scale form");
}

// OBJECT-0214 Transforms to Deltas. Moving the base location, rotation and scale into the delta transform does not change the effective matrix
// T(loc + dloc) (drot rot) S(scale dscale): the image of six probe points is the same before and after (1e-12), the base parts are then
// zero / identity / one and the deltas hold the sums and products (dloc + loc, dscale x scale). Moving only the location leaves the rotation and scale in
// place. The order in which a delta rotation composes with the base rotation is recalled, not read from the manual: needs owner check.
D8_EXTRA_TEST(RowBkgTransformsToDeltasKeepTheEffectiveMatrix) {
  k::ObjectTransform t;
  t.location = Vector3d(2, -3, 5);
  t.delta_location = Vector3d(0.5, 0.25, -1);
  t.scale = Vector3d(2, 3, 0.5);
  t.delta_scale = Vector3d(1.5, 0.5, 2);
  t.rotation = FromFunction([](const Point3d& p) { return RotX(p, 0.4); });
  t.delta_rotation = FromFunction([](const Point3d& p) { return RotZ(p, -0.9); });
  const auto effective = [](const Point3d& p) {  // T(loc + dloc) Rz(-0.9) Rx(0.4) S(scale dscale), step by step
    return Move(RotZ(RotX(Scale(p, 2 * 1.5, 3 * 0.5, 0.5 * 2), 0.4), -0.9), 2.5, -2.75, 4);
  };
  d8test::Check(WorstOver(k::EffectiveMatrix(t), effective) < 1e-12, "the effective matrix is the written-out product");
  const k::ObjectTransform all = k::MoveToDeltas(t);
  d8test::Check(WorstOver(k::EffectiveMatrix(all), effective) < 1e-12, "moving everything to the deltas does not change the effective matrix");
  d8test::Check(VD(all.location, Vector3d(0, 0, 0)) < 1e-12 && VD(all.scale, Vector3d(1, 1, 1)) < 1e-12 && WorstOver(all.rotation, [](const Point3d& p) { return p; }) < 1e-12,
                "the base parts are zero, one and the identity");
  d8test::Check(VD(all.delta_location, Vector3d(2.5, -2.75, 4)) < 1e-12 && VD(all.delta_scale, Vector3d(3, 1.5, 1)) < 1e-12, "the deltas hold (2.5,-2.75,4) and (3,1.5,1)");
  const k::ObjectTransform only_loc = k::MoveToDeltas(t, true, false, false);
  d8test::Check(WorstOver(k::EffectiveMatrix(only_loc), effective) < 1e-12 && VD(only_loc.scale, t.scale) == 0.0 && VD(only_loc.delta_location, Vector3d(2.5, -2.75, 4)) < 1e-12,
                "moving only the location keeps the rest in place");
}

// OBJECT-0145 Make Parent, OBJECT-0144 Make Parent without Inverse, OBJECT-0143 Apply Parent Inverse, OBJECT-0142 Clear Parent. With the parent
// P(x) = T(10,0,0) Rz(pi/2) S(2,2,2) x and the child's local L(x) = T(1,2,3) Rx(0.4) x: Make Parent (keep transform) uses the parent inverse P^-1(y) =
// S(1/2) Rz(-pi/2) (y - (10,0,0)) and the world map P(P^-1(L(x))) equals L(x) (the child stays); without the inverse the world map is P(L(x)) (the child
// moves with the parent); Apply Parent Inverse gives the local map P^-1(L(x)), the identity parent inverse and the same world map; Clear Parent keeping the
// transform gives the local map equal to the world map P(P^-1(L(x))), and without keeping it leaves L. All at six points (1e-12). Parenting to a
// vertex or bone and the parent type are not covered.
D8_EXTRA_TEST(RowBkgParentOperationsKeepOrMoveTheChildAsDefined) {
  const Fn parent = [](const Point3d& x) { return Move(RotZ(Scale(x, 2, 2, 2), kPi / 2), 10, 0, 0); };
  const Fn parent_inverse = [](const Point3d& y) { return Scale(RotZ(Move(y, -10, 0, 0), -kPi / 2), 0.5, 0.5, 0.5); };
  const Fn local = [](const Point3d& x) { return Move(RotX(x, 0.4), 1, 2, 3); };
  const ON_Xform pm = FromFunction(parent), lm = FromFunction(local);
  const ON_Xform pinv = k::ParentInverseForKeepTransform(pm);
  d8test::Check(WorstOver(pinv, parent_inverse) < 1e-12, "the parent inverse is the written-out inverse map");
  d8test::Check(WorstOver(k::ChildWorldMatrix(pm, pinv, lm), local) < 1e-12, "Make Parent (keep transform): the child's world map is its local map");
  d8test::Check(WorstOver(k::ChildWorldMatrix(pm, k::ParentInverseForNoInverse(), lm), [&](const Point3d& x) { return parent(local(x)); }) < 1e-12,
                "Make Parent without inverse: the world map is P(L(x)), the child moves with the parent");
  k::ParentState s;
  s.parent_inverse = pinv;
  s.local = lm;
  const k::ParentState applied = k::ApplyParentInverse(s);
  d8test::Check(WorstOver(applied.local, [&](const Point3d& x) { return parent_inverse(local(x)); }) < 1e-12 && WorstOver(applied.parent_inverse, [](const Point3d& p) { return p; }) < 1e-12,
                "Apply Parent Inverse: the local map is P^-1(L(x)) and the parent inverse is the identity");
  d8test::Check(WorstOver(k::ChildWorldMatrix(pm, applied.parent_inverse, applied.local), local) < 1e-12, "Apply Parent Inverse leaves the world map as it was");
  d8test::Check(WorstOver(k::LocalAfterClearParent(pm, s, true), [&](const Point3d& x) { return parent(parent_inverse(local(x))); }) < 1e-12, "Clear Parent keeping the transform: the new local map is the world map");
  d8test::Check(WorstOver(k::LocalAfterClearParent(pm, s, false), local) < 1e-12, "Clear Parent without keeping it: the local map stays");
  ON_Xform singular = ON_Xform::IdentityTransformation;
  singular[0][0] = 0.0;
  bool refused = false;
  try {
    k::ParentInverseForKeepTransform(singular);
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  d8test::Check(refused, "a singular parent matrix is refused");
}

// OBJECT-0025 Copy Global Transform, OBJECT-0148 Paste Global Transform, OBJECT-0026 Copy Relative Transform. A child of the parent P(x) = T(10,0,0)
// Rz(pi/2) S(2,2,2) with parent inverse P^-1 and local L has the world map W = P P^-1 L. Pasting a stored world map G onto another child of a
// different parent Q(x) = T(-3,4,0) Rz(0.5) x with its own parent inverse (identity) sets the local map Q^-1(G(x)), so the world map equals G (1e-12);
// the relative map of A seen from B is x -> B^-1(A(x)) written out with B(x) = T(1,1,1) Rz(0.3) S(2,1,1) x. Mirror and the "use current frame" options are not covered.
D8_EXTRA_TEST(RowBkgCopyAndPasteGlobalAndRelativeTransforms) {
  const Fn parent = [](const Point3d& x) { return Move(RotZ(Scale(x, 2, 2, 2), kPi / 2), 10, 0, 0); };
  const Fn local = [](const Point3d& x) { return Move(RotX(x, 0.4), 1, 2, 3); };
  const ON_Xform pm = FromFunction(parent), lm = FromFunction(local);
  const ON_Xform global = k::ChildWorldMatrix(pm, k::ParentInverseForKeepTransform(pm), lm);  // the copied world matrix, = L
  const Fn q = [](const Point3d& x) { return Move(RotZ(x, 0.5), -3, 4, 0); };
  const Fn q_inverse = [](const Point3d& y) { return RotZ(Move(y, 3, -4, 0), -0.5); };
  const ON_Xform qm = FromFunction(q);
  const ON_Xform pasted = k::LocalForGlobalMatrix(qm, ON_Xform::IdentityTransformation, global);
  d8test::Check(WorstOver(pasted, [&](const Point3d& x) { return q_inverse(local(x)); }) < 1e-12, "paste: the new local map is Q^-1(G(x))");
  d8test::Check(WorstOver(k::ChildWorldMatrix(qm, ON_Xform::IdentityTransformation, pasted), local) < 1e-12, "paste: the world map of the pasted child is the copied one");
  const Fn b = [](const Point3d& x) { return Move(RotZ(Scale(x, 2, 1, 1), 0.3), 1, 1, 1); };
  const Fn b_inverse = [](const Point3d& y) { return Scale(RotZ(Move(y, -1, -1, -1), -0.3), 0.5, 1, 1); };
  const ON_Xform rel = k::RelativeMatrix(global, FromFunction(b));
  d8test::Check(WorstOver(rel, [&](const Point3d& x) { return b_inverse(local(x)); }) < 1e-12, "relative matrix: x -> B^-1(A(x))");
  d8test::Check(WorstOver(FromFunction(b) * rel, local) < 1e-12, "B times the relative matrix gives A back");
}

// VIEW3D-0037 Cursor to Grid, VIEW3D-0038 Cursor to Selected, VIEW3D-0039 Selection to Active, VIEW3D-0040 Selection to Cursor, VIEW3D-0041 Selection to Grid.
// Snapping rounds each coordinate to the nearest multiple of the grid spacing 0.5 (1.26 -> 1.5, 0.25 -> 0.5, -0.26 -> -0.5, 1.2 -> 1.0, exact multiples
// stay); the median of (0,0,0), (4,0,0), (0,2,0), (0,0,6) is (1, 0.5, 1.5) and its bounds centre is (2, 1, 3); Selection to Cursor / Active with the offset kept
// translates by target - pivot so that the pivot lands on the target and the pairwise differences are unchanged, and without the offset puts every point on
// the target. Which centre (median or bounds) Blender picks follows the pivot setting and is recalled, not read from the manual: needs owner check.
D8_EXTRA_TEST(RowBkgSnapSelectionAndCursorToGridSelectionAndTarget) {
  d8test::Check(Near(k::SnapToGrid(P(1.26, 0.25, -0.26), 0.5), P(1.5, 0.5, -0.5), 1e-12) && Near(k::SnapToGrid(P(1.2, 3.0, -1.74), 0.5), P(1.0, 3.0, -1.5), 1e-12), "grid 0.5 rounds every coordinate to the nearest multiple");
  d8test::Check(Near(k::SnapToGrid(P(7.3, -0.9, 0.1), 2.0), P(8.0, 0.0, 0.0), 1e-12), "grid 2 rounds (7.3, -0.9, 0.1) to (8, 0, 0)");
  const Point3d once = k::SnapToGrid(P(0.37, 5.55, -2.2), 0.25);
  d8test::Check(Near(k::SnapToGrid(once, 0.25), once, 1e-12) && Near(once, P(0.25, 5.5, -2.25), 1e-12), "snapping is idempotent and (0.37, 5.55, -2.2) goes to (0.25, 5.5, -2.25) on a 0.25 grid");
  bool refused = false;
  try {
    k::SnapToGrid(P(1, 1, 1), 0.0);
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  d8test::Check(refused, "a grid spacing of zero is refused");
  const std::vector<Point3d> sel = {P(0, 0, 0), P(4, 0, 0), P(0, 2, 0), P(0, 0, 6)};
  d8test::Check(Near(k::MedianCenter(sel), P(1, 0.5, 1.5), 1e-12) && Near(k::BoundsCenter(sel), P(2, 1, 3), 1e-12), "median (1, 0.5, 1.5) and bounds centre (2, 1, 3)");
  const Point3d target = P(10, 20, 30);
  const std::vector<Point3d> moved = k::MoveSelectionTo(sel, k::MedianCenter(sel), target, true);
  d8test::Check(Near(k::MedianCenter(moved), target, 1e-12), "with the offset kept the median lands on the target");
  double pair = 0.0;
  for (size_t i = 1; i < sel.size(); ++i) pair = std::max(pair, ((moved[i] - moved[0]) - (sel[i] - sel[0])).Length());
  d8test::Check(pair < 1e-12, "the pairwise differences are unchanged");
  const std::vector<Point3d> stacked = k::MoveSelectionTo(sel, k::MedianCenter(sel), target, false);
  bool all = true;
  for (const Point3d& p : stacked) all = all && Near(p, target, 0.0);
  d8test::Check(all && stacked.size() == 4, "without the offset every point goes to the target");
  std::vector<Point3d> grid;
  for (const Point3d& p : std::vector<Point3d>{P(0.2, 0.7, 1.4), P(-0.6, 2.2, 0.49)}) grid.push_back(k::SnapToGrid(p, 0.5));
  d8test::Check(Near(grid[0], P(0.0, 0.5, 1.5), 1e-12) && Near(grid[1], P(-0.5, 2.0, 0.5), 1e-12), "Selection to Grid: each point rounds on its own");
}

// TRANSFORM-0010 Push/Pull, TRANSFORM-0007 Edge Slide, TRANSFORM-0025 Vertex Slide, TRANSFORM-0019 Tilt. Push/Pull moves a point along the line to the
// centre: the vertices of the octahedron of circumradius 2 move to distance 2 - d from the centre (d = 0.5: 1.5, d = -1: 3). Sliding the top edge of the
// box [0,2] x [0,3] x [0,4] along its vertical rails by t = 1/4 puts both end points at z = 3 and keeps the edge parallel to y; a vertex slides to
// p + t (q - p) toward its neighbor (t = 1 reaches it, t = -1/2 moves away). The tilt turns the normal x about the tangent z to (cos a, sin a, 0), a = pi/2 gives
// (0, 1, 0), two tilts add their angles, and a tilt of 0 gives the normal back. The direction of push, the factor range of a slide and the sign of a tilt are
// recalled from the Blender behaviour, not read from the manual: needs owner check, not independent.
D8_EXTRA_TEST(RowBkgPushPullSlideAndTiltPointMaps) {
  const double rho = 2.0;
  const Point3d c = P(1, -1, 0.5);
  double worst = 0.0;
  for (const Point3d& d : {P(rho, 0, 0), P(-rho, 0, 0), P(0, rho, 0), P(0, 0, -rho)}) {
    const Point3d p = P(c.x + d.x, c.y + d.y, c.z + d.z);
    worst = std::max(worst, std::fabs(k::PushPull(p, c, 0.5).DistanceTo(c) - 1.5));
    worst = std::max(worst, std::fabs(k::PushPull(p, c, -1.0).DistanceTo(c) - 3.0));
    worst = std::max(worst, k::PushPull(p, c, 0.0).DistanceTo(p));
  }
  d8test::Check(worst < 1e-12, "push/pull moves the point to distance 2 - d on the same ray");
  d8test::Check(Near(k::PushPull(c, c, 1.0), c, 0.0), "a point at the centre stays");
  const Point3d top_a = P(0, 0, 4), top_b = P(0, 3, 4), low_a = P(0, 0, 0), low_b = P(0, 3, 0);
  const Point3d sa = k::SlideAlongEdge(top_a, low_a, 0.25), sb = k::SlideAlongEdge(top_b, low_b, 0.25);
  d8test::Check(Near(sa, P(0, 0, 3), 1e-12) && Near(sb, P(0, 3, 3), 1e-12) && std::fabs((sb - sa).z) < 1e-12 && std::fabs((sb - sa).y - 3.0) < 1e-12, "edge slide by 1/4: both ends at z = 3, the edge stays parallel to y");
  d8test::Check(Near(k::SlideAlongEdge(P(1, 2, 3), P(5, 2, -1), 1.0), P(5, 2, -1), 1e-12) && Near(k::SlideAlongEdge(P(1, 2, 3), P(5, 2, -1), -0.5), P(-1, 2, 5), 1e-12), "vertex slide: t = 1 reaches the neighbor, t = -1/2 moves away by half the edge");
  const Vector3d t(0, 0, 1), n(1, 0, 0);
  const double a = 0.9;
  d8test::Check(VD(k::TiltNormal(t, n, a), Vector3d(std::cos(a), std::sin(a), 0)) < 1e-12 && VD(k::TiltNormal(t, n, kPi / 2), Vector3d(0, 1, 0)) < 1e-12, "tilt turns x about z to (cos a, sin a, 0)");
  d8test::Check(VD(k::TiltNormal(t, k::TiltNormal(t, n, 0.4), 0.5), k::TiltNormal(t, n, 0.9)) < 1e-12 && VD(k::TiltNormal(t, n, 0.0), n) < 1e-12, "tilts add their angles and a tilt of 0 changes nothing");
  d8test::Check(std::fabs(k::TiltNormal(Vector3d(0, 0, 3), Vector3d(2, 0, 1), 0.3).Length() - 1.0) < 1e-12, "the tilted normal is a unit vector even for an unnormalised input");
}

// SURFACE-0001 Add Surface Circle. The disc patch of centre c, normal (0,0,1) and radius r is exactly planar, S(u, 0) lies on the circle, S(u, 1) is the
// centre, every point is at distance (1 - v) r from the centre, the surface normal points along +z, and the area is pi r^2 (1e-6). Blender's own
// control-net layout of the operator is not reproduced.
D8_EXTRA_TEST(RowBkgSurfaceCircleIsTheExactDisc) {
  const Point3d c = P(1, -2, 3);
  const double r = 2.5;
  const NurbsSurface disc = NurbsSurface::Disc(c, Vector3d(0, 0, 1), r);
  const auto du = disc.Domain(0), dv = disc.Domain(1);
  double radial = 0.0, plane = 0.0, normal_err = 0.0;
  for (int i = 0; i <= 24; ++i)
    for (int j = 0; j <= 8; ++j) {
      const double u = du.min + (du.max - du.min) * i / 24.0, v = dv.min + (dv.max - dv.min) * j / 8.0;
      const Point3d p = disc.PointAt(u, v);
      const double fv = j / 8.0;
      radial = std::max(radial, std::fabs(p.DistanceTo(c) - (1.0 - fv) * r));
      plane = std::max(plane, std::fabs(p.z - c.z));
      if (j > 0 && j < 8) normal_err = std::max(normal_err, (disc.NormalAt(u, v) - Vector3d(0, 0, 1)).Length());
    }
  d8test::Check(radial < 1e-12, Msg("every point is at distance (1 - v) r from the centre", radial).c_str());
  d8test::Check(plane < 1e-12, "the patch is planar");
  d8test::Check(normal_err < 1e-9, Msg("the surface normal points along +z", normal_err).c_str());
  double area = 0.0;
  try {
    area = Brep::FromSurface(disc).Area();
  } catch (...) {
    d8test::Check(false, "Area() of the disc");
  }
  d8test::Check(NearRel(area, kPi * r * r, 1e-6), Msg("area is pi r^2", area).c_str());
  bool refused = false;
  try {
    NurbsSurface::Disc(c, Vector3d(0, 0, 0), r);
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  d8test::Check(refused, "a zero normal is refused");
  const NurbsSurface tilted = NurbsSurface::Disc(P(0, 0, 0), Vector3d(1, 0, 0), 1.0);
  double on_plane = 0.0;
  for (int i = 0; i <= 12; ++i) on_plane = std::max(on_plane, std::fabs(tilted.PointAt(tilted.Domain(0).min + (tilted.Domain(0).max - tilted.Domain(0).min) * i / 12.0, 0.0).x));
  d8test::Check(on_plane < 1e-12, "a disc with normal x lies in the plane x = 0");
}
