// SketchUp-style Rotate, Scale, Flip rows: Brep::Transform (the kernel function behind Move / Rotate / Scale / Mirror) applied to a box with the
// matrices a user's gesture produces. Expectations are textbook closed forms written in the test: rotation x' = c + R(theta) (x - c), a scale about a
// fixed point x' = a + s (x - a) per axis, a mirror about the plane n . x = d as x' = x - 2 (n . x - d) n, never the matrix code under test; the
// matrices themselves are compared cell by cell with those closed forms. A reflection reverses orientation, so a mirrored solid must still be a
// valid closed solid whose face normals point OUT (checked with the surface normal, the face's own reverse flag and the point-vs-centre test).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Point3d;

const double kPi = 3.14159265358979323846;

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

std::vector<Point3d> Corners(const Brep& b) {
  std::vector<Point3d> out;
  for (const Brep::PlanarFace& f : b.PlanarFaces()) {
    for (const Point3d& p : f.loop) {
      bool dup = false;
      for (const Point3d& q : out) if (q.DistanceTo(p) < 1e-9) dup = true;
      if (!dup) out.push_back(p);
    }
  }
  return out;
}

// Largest distance from an expected corner to the nearest actual corner, and the other way round: 0 when the two sets agree.
double SetDistance(const std::vector<Point3d>& expected, const std::vector<Point3d>& actual) {
  double worst = 0.0;
  for (const Point3d& e : expected) {
    double best = 1e300;
    for (const Point3d& a : actual) best = std::min(best, e.DistanceTo(a));
    worst = std::max(worst, best);
  }
  for (const Point3d& a : actual) {
    double best = 1e300;
    for (const Point3d& e : expected) best = std::min(best, e.DistanceTo(a));
    worst = std::max(worst, best);
  }
  return worst;
}

double SignedVolume(const Brep& b) {
  double v = 0.0;
  for (const Brep::PlanarFace& f : b.PlanarFaces()) {
    for (size_t i = 1; i + 1 < f.loop.size(); ++i) {
      const Point3d& a = f.loop[0];
      const Point3d& p = f.loop[i];
      const Point3d& q = f.loop[i + 1];
      v += (a.x * (p.y * q.z - p.z * q.y) - a.y * (p.x * q.z - p.z * q.x) + a.z * (p.x * q.y - p.y * q.x)) / 6.0;
    }
  }
  return v;
}

// Faces whose oriented normal points toward `centre` (inside the convex body).
int InwardFaces(const Brep& b, const Point3d& centre) {
  const ON_Brep& r = b.raw();
  int inward = 0;
  for (int i = 0; i < r.m_F.Count(); ++i) {
    if (r.m_F[i].m_face_index < 0) continue;
    const ON_BrepFace& f = r.m_F[i];
    const double u = f.Domain(0).ParameterAt(0.5), w = f.Domain(1).ParameterAt(0.5);
    ON_3dPoint p;
    if (!f.EvPoint(u, w, p)) continue;
    ON_3dVector n = f.NormalAt(u, w);
    if (f.m_bRev) n = -n;
    if (n.x * (p.x - centre.x) + n.y * (p.y - centre.y) + n.z * (p.z - centre.z) < 0) ++inward;
  }
  return inward;
}

double MatrixDeviation(const ON_Xform& m, const double expect[4][4]) {
  double worst = 0.0;
  for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) worst = std::max(worst, std::fabs(m.m_xform[i][j] - expect[i][j]));
  return worst;
}

ON_Xform RotationAbout(double angle, const ON_3dVector& axis, const ON_3dPoint& centre) {
  ON_Xform x;
  x.Rotation(angle, axis, centre);
  return x;
}

ON_Xform MirrorAbout(const ON_3dPoint& point, ON_3dVector normal) {
  normal.Unitize();
  ON_Xform x;
  x.Mirror(point, normal);
  return x;
}

std::vector<Point3d> BoxCorners(double x0, double y0, double z0, double x1, double y1, double z1) {
  std::vector<Point3d> c;
  for (double x : {x0, x1}) for (double y : {y0, y1}) for (double z : {z0, z1}) c.push_back(Point3d(x, y, z));
  return c;
}

}  // namespace

// EDIT-0009, "Rotate uses a protractor to rotate a selection about a point and axis, with typed angle or slope, copy mode...". Acceptance sketch:
// "Rotate a component 37.5 degrees about a vertical axis; matrix equals closed form within 1e-12." The box [1,3] x [0,2] x [0,1] is rotated by 37.5
// degrees about the vertical axis through (1, 0): x' = 1 + c (x - 1) - s y, y' = s (x - 1) + c y, z' = z with c = cos 37.5, s = sin 37.5. The matrix
// cells are those of that map (rotation block c, -s, s, c; translation (1 - c, -s, 0)) and the eight corners go to the images of the eight corners.
// Volume (a rotation keeps it) is 4.
D8_EXTRA_TEST(SuRotateTransformsABoxByTheClosedFormRotationAboutAVerticalAxis) {
  const double th = 37.5 * kPi / 180.0, c = std::cos(th), s = std::sin(th);
  const ON_Xform rot = RotationAbout(th, ON_3dVector(0, 0, 1), ON_3dPoint(1, 0, 0));
  const double expect[4][4] = {{c, -s, 0, 1 - c}, {s, c, 0, -s}, {0, 0, 1, 0}, {0, 0, 0, 1}};
  d8test::Check(MatrixDeviation(rot, expect) < 1e-12, Msg("the rotation matrix equals the closed form within 1e-12", MatrixDeviation(rot, expect)).c_str());
  const Brep box = Brep::BoxWelded(1, 0, 0, 3, 2, 1);
  const Brep r = box.Transform(rot);
  std::vector<Point3d> want;
  for (const Point3d& p : BoxCorners(1, 0, 0, 3, 2, 1)) want.push_back(Point3d(1 + c * (p.x - 1) - s * p.y, s * (p.x - 1) + c * p.y, p.z));
  d8test::Check(SetDistance(want, Corners(r)) < 1e-12, Msg("the eight corners are the images of the eight corners within 1e-12", SetDistance(want, Corners(r))).c_str());
  d8test::Check(r.raw().IsSolid() && std::fabs(SignedVolume(r) - 4.0) < 1e-12, Msg("the rotated box is a closed solid of volume 4", SignedVolume(r)).c_str());
  d8test::Check(InwardFaces(r, Point3d(1 + c * 1 - s * 1, s * 1 + c * 1, 0.5)) == 0, "no face is turned inside out by the rotation");
}

// EDIT-0013, "Scale tool scales uniformly from corner grips, in two axes from edge grips and one axis from face grips, with scale-from-center toggle".
// Acceptance sketch: "Scale a box 150 percent uniformly then 50 percent on the red axis only; vertex coordinates equal closed form within 1e-12."
// The box [2,4] x [1,4] x [0,2] is scaled by 1.5 about its corner (2,1,0) (corner grip, the opposite corner moves), then by 0.5 along x only about the
// same corner: x'' = 2 + 0.5 * 1.5 (x - 2), y'' = 1 + 1.5 (y - 1), z'' = 1.5 z. The volume is multiplied by 1.5 * 1.5 * 1.5 * 0.5 = 1.6875 (12 -> 20.25), and
// the matrix of the pair is diag(0.75, 1.5, 1.5) with translation (2 - 1.5, 1 - 1.5, 0) = (0.5, -0.5, 0).
D8_EXTRA_TEST(SuScaleTransformsABoxUniformlyThenAlongOneAxisByTheClosedForm) {
  const ON_Xform s1 = ON_Xform::ScaleTransformation(ON_3dPoint(2, 1, 0), 1.5, 1.5, 1.5);
  const ON_Xform s2 = ON_Xform::ScaleTransformation(ON_3dPoint(2, 1, 0), 0.5, 1.0, 1.0);
  const ON_Xform both = s2 * s1;
  const double expect[4][4] = {{0.75, 0, 0, 0.5}, {0, 1.5, 0, -0.5}, {0, 0, 1.5, 0}, {0, 0, 0, 1}};
  d8test::Check(MatrixDeviation(both, expect) < 1e-12, Msg("the composed scale matrix equals diag(0.75, 1.5, 1.5) with translation (0.5, -0.5, 0)", MatrixDeviation(both, expect)).c_str());
  const Brep box = Brep::BoxWelded(2, 1, 0, 4, 4, 2);
  const Brep once = box.Transform(s1);
  std::vector<Point3d> want1;
  for (const Point3d& p : BoxCorners(2, 1, 0, 4, 4, 2)) want1.push_back(Point3d(2 + 1.5 * (p.x - 2), 1 + 1.5 * (p.y - 1), 1.5 * p.z));
  d8test::Check(SetDistance(want1, Corners(once)) < 1e-12 && std::fabs(SignedVolume(once) - 12.0 * 3.375) < 1e-9, Msg("150 percent uniform: corners within 1e-12, volume 12 x 3.375 = 40.5", SignedVolume(once)).c_str());
  const Brep twice = once.Transform(s2);
  std::vector<Point3d> want2;
  for (const Point3d& p : BoxCorners(2, 1, 0, 4, 4, 2)) want2.push_back(Point3d(2 + 0.5 * 1.5 * (p.x - 2), 1 + 1.5 * (p.y - 1), 1.5 * p.z));
  d8test::Check(SetDistance(want2, Corners(twice)) < 1e-12, Msg("then 50 percent on the red axis: corners equal the closed form within 1e-12", SetDistance(want2, Corners(twice))).c_str());
  d8test::Check(std::fabs(SignedVolume(twice) - 12.0 * 1.6875) < 1e-9 && twice.raw().IsSolid(), Msg("volume is 12 x 1.6875 = 20.25", SignedVolume(twice)).c_str());
}

// EDIT-0017, "Flip mirrors selected geometry about adjustable planes". Acceptance sketch: "Mirror a component about a plane at distance 1 m;
// reflection matrix has determinant -1 and positions match closed form within 1e-12." The plane is x = 1 (normal +x at distance 1 from the origin):
// x' = 2 - x, y and z unchanged; matrix diag(-1, 1, 1) with translation (2, 0, 0), determinant -1. The box [2,4] x [0,2] x [0,3] goes to [-2,0] x
// [0,2] x [0,3]; the volume is still +12 (the faces were turned over with the reflection, so the shell is still outward), and the shell is still a
// valid closed solid. A second plane, tilted 45 degrees (normal (1,1,0)/sqrt 2 through the origin) swaps x and y: the corners go to (y, x, z).
D8_EXTRA_TEST(SuFlipMirrorsABoxAboutAPlaneWithADeterminantOfMinusOneAndAnOutwardShell) {
  const ON_Xform m = MirrorAbout(ON_3dPoint(1, 0, 0), ON_3dVector(1, 0, 0));
  const double expect[4][4] = {{-1, 0, 0, 2}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
  d8test::Check(MatrixDeviation(m, expect) < 1e-12 && std::fabs(m.Determinant() + 1.0) < 1e-12, "the mirror matrix is diag(-1,1,1) with translation (2,0,0) and determinant -1");
  const Brep box = Brep::BoxWelded(2, 0, 0, 4, 2, 3);
  const Brep f = box.Transform(m);
  std::vector<Point3d> want;
  for (const Point3d& p : BoxCorners(2, 0, 0, 4, 2, 3)) want.push_back(Point3d(2 - p.x, p.y, p.z));
  d8test::Check(SetDistance(want, Corners(f)) < 1e-12, Msg("the corners are the reflections x' = 2 - x within 1e-12", SetDistance(want, Corners(f))).c_str());
  d8test::Check(f.raw().IsSolid() && f.raw().IsValid(), "the mirrored box is a valid closed solid");
  d8test::Check(std::fabs(SignedVolume(f) - 12.0) < 1e-12, Msg("the mirrored shell has volume +12 (it was turned outward again)", SignedVolume(f)).c_str());
  d8test::Check(InwardFaces(f, Point3d(-1, 1, 1.5)) == 0, "no face of the mirrored box points inward");
  const ON_Xform diag = MirrorAbout(ON_3dPoint(0, 0, 0), ON_3dVector(1, -1, 0));
  const Brep g = box.Transform(diag);
  std::vector<Point3d> want2;
  for (const Point3d& p : BoxCorners(2, 0, 0, 4, 2, 3)) want2.push_back(Point3d(p.y, p.x, p.z));
  d8test::Check(SetDistance(want2, Corners(g)) < 1e-12 && std::fabs(SignedVolume(g) - 12.0) < 1e-12 && InwardFaces(g, Point3d(1, 3, 1.5)) == 0, "the mirror about the plane x = y swaps x and y, volume +12, outward");
}

// EDIT-0018, "Context-click a component to Flip Along its red, green or blue axis". Acceptance sketch: "Flip along each axis; component transformation
// negates that axis exactly." The component's own axes are the world axes through its origin (0,0,0): flipping along red gives diag(-1,1,1), along
// green diag(1,-1,1), along blue diag(1,1,-1), every other cell exactly 0 and the translation exactly 0 (no rounding: the entries are compared
// with ==). The box [1,3] x [2,5] x [-1,4] goes to the corners with that one coordinate negated; the volume is +30 and the shell still outward each time.
D8_EXTRA_TEST(SuFlipAlongEachAxisNegatesExactlyThatAxis) {
  const Brep box = Brep::BoxWelded(1, 2, -1, 3, 5, 4);
  for (int axis = 0; axis < 3; ++axis) {
    ON_3dVector n(0, 0, 0);
    if (axis == 0) n.x = 1; else if (axis == 1) n.y = 1; else n.z = 1;
    const ON_Xform m = MirrorAbout(ON_3dPoint(0, 0, 0), n);
    bool exact = true;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
      const double want = (i == j) ? ((i == axis) ? -1.0 : 1.0) : 0.0;
      if (m.m_xform[i][j] != want) exact = false;
    }
    d8test::Check(exact, (std::string("the flip matrix along axis ") + std::to_string(axis) + " negates that axis and nothing else, exactly").c_str());
    const Brep f = box.Transform(m);
    std::vector<Point3d> want;
    for (const Point3d& p : BoxCorners(1, 2, -1, 3, 5, 4)) want.push_back(Point3d(axis == 0 ? -p.x : p.x, axis == 1 ? -p.y : p.y, axis == 2 ? -p.z : p.z));
    d8test::Check(SetDistance(want, Corners(f)) < 1e-12, Msg("the corners have exactly that coordinate negated", SetDistance(want, Corners(f))).c_str());
    const Point3d centre(axis == 0 ? -2 : 2, axis == 1 ? -3.5 : 3.5, axis == 2 ? -1.5 : 1.5);
    d8test::Check(f.raw().IsSolid() && std::fabs(SignedVolume(f) - 30.0) < 1e-12 && InwardFaces(f, centre) == 0, "the flipped box is a closed outward solid of volume +30");
  }
}
