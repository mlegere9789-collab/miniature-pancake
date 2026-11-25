// Register rows of the kernel-geometry and direct-modeling factories, Blender NURBS surface primitives and interactive transform
// rows (round 3). Each test names the rows it supports and checks the kernel against a closed form that does not come from the code
// under test: the area and volume of the sphere, cylinder, torus and disc, the area of a planar control net, the image of a box under
// an affine map (bounding box, volume x |det|, centroid mapped by the matrix), the parallelogram areas of a shear, the arc of
// the bend, the radial law of To Sphere and the volume of a regular octahedron under a normal offset. None of them marks a row
// verified; operator behaviour in the viewport (selection, snapping, constraints, undo) is not covered by a kernel test.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/deform.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BoundingBox;
using dino8::kernel::Brep;
using dino8::kernel::Mesh;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }
std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }
bool NearRel(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::fabs(b); }
bool Near(const Point3d& a, const Point3d& b, double tol) { return a.DistanceTo(b) <= tol; }

double AreaOf(const Brep& b, const char* what) {
  try {
    return b.Area();
  } catch (...) {
    d8test::Check(false, (std::string("Area() works for ") + what).c_str());
    return 0.0;
  }
}

double VolumeOf(const Brep& b, const char* what) {
  try {
    return b.VolumeOfSealedSolid();
  } catch (...) {
  }
  try {
    return b.Volume();
  } catch (...) {
  }
  d8test::Check(false, (std::string("a volume is available for ") + what).c_str());
  return 0.0;
}

// The surface of the first face of a Brep: what the Blender "Add Surface ..." operators create is one NURBS surface.
const ON_Surface* FirstSurface(const Brep& b) {
  const ON_Brep& raw = b.raw();
  return raw.m_F.Count() > 0 ? raw.m_F[0].SurfaceOf() : nullptr;
}

// Semicircle in the xz plane from the south pole (0, 0, -r) through (r, 0, 0) to the north pole (0, 0, r): the plane x axis is (0,0,-1).
NurbsCurve SemicircleXZ(double r) {
  ON_Arc arc(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, -1), ON_3dVector(1, 0, 0)), r, kPi);
  ON_NurbsCurve nurbs;
  arc.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

ON_Xform Affine(double sx, double sy, double sz, const Point3d& pivot) {
  ON_Xform m(1.0);
  m[0][0] = sx;
  m[1][1] = sy;
  m[2][2] = sz;
  m[0][3] = pivot.x - sx * pivot.x;
  m[1][3] = pivot.y - sy * pivot.y;
  m[2][3] = pivot.z - sz * pivot.z;
  return m;
}

}  // namespace

// SURFACE-0004 Add Surface Sphere, SURFACE-0003 Add Surface Cylinder, SURFACE-0006 Add Surface Torus. The NURBS wall of a spun
// profile is recognised as the analytic surface with the right radius, and the whole body integrates to the closed forms:
// sphere area 4 pi r^2 and volume 4/3 pi r^3; cylinder wall 2 pi r h, area 2 pi r (r + h) and volume pi r^2 h with two caps; torus
// area 4 pi^2 R r and volume 2 pi^2 R r^2. Blender's own control-net sizes are not reproduced: the geometry is what is compared.
D8_EXTRA_TEST(RowBkgSurfaceSphereCylinderAndTorusHaveTheClosedFormAreasAndVolumes) {
  const double r = 2.0;
  const Brep sphere = Brep::Revolve(SemicircleXZ(r), P(0, 0, 0), Vector3d(0, 0, 1));
  const ON_Surface* ss = FirstSurface(sphere);
  ON_Sphere osph;
  d8test::Check(ss != nullptr && ss->IsSphere(&osph, 1e-9), "the spun semicircle is a sphere surface");
  if (ss != nullptr && ss->IsSphere(&osph, 1e-9)) d8test::Check(std::fabs(osph.radius - r) < 1e-9 && osph.Center().DistanceTo(ON_3dPoint(0, 0, 0)) < 1e-9, "sphere radius r and centre at the origin");
  d8test::Check(NearRel(AreaOf(sphere, "sphere"), 4.0 * kPi * r * r, 5e-6), Msg("sphere area is 4 pi r^2", AreaOf(sphere, "sphere")).c_str());
  d8test::Check(NearRel(VolumeOf(sphere, "sphere"), 4.0 / 3.0 * kPi * r * r * r, 5e-6), Msg("sphere volume is 4/3 pi r^3", VolumeOf(sphere, "sphere")).c_str());

  const double rc = 1.5, h = 4.0;
  const NurbsCurve ell = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(rc, 0, 0), P(rc, 0, h), P(0, 0, h)}, 1);
  const Brep cyl = Brep::Revolve(ell, P(0, 0, 0), Vector3d(0, 0, 1));
  // The open wall alone (a vertical segment spun) is one cylinder surface of radius rc and area 2 pi rc h.
  const NurbsCurve line = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(rc, 0, 0), P(rc, 0, h)}, 1);
  const Brep wall = Brep::Revolve(line, P(0, 0, 0), Vector3d(0, 0, 1), 2.0 * kPi, false);
  ON_Cylinder oc;
  const ON_Surface* cs = FirstSurface(wall);
  d8test::Check(cs != nullptr && cs->IsCylinder(&oc, 1e-9) && std::fabs(oc.circle.Radius() - rc) < 1e-9, "the spun segment is a cylinder surface of radius rc");
  d8test::Check(NearRel(AreaOf(wall, "cylinder wall"), 2.0 * kPi * rc * h, 1e-6), Msg("cylinder wall area is 2 pi r h", AreaOf(wall, "cylinder wall")).c_str());
  d8test::Check(NearRel(AreaOf(cyl, "cylinder"), 2.0 * kPi * rc * (rc + h), 1e-6), Msg("cylinder area is 2 pi r (r + h)", AreaOf(cyl, "cylinder")).c_str());
  d8test::Check(NearRel(VolumeOf(cyl, "cylinder"), kPi * rc * rc * h, 1e-6), Msg("cylinder volume is pi r^2 h", VolumeOf(cyl, "cylinder")).c_str());

  const double R = 4.0, rt = 1.0;
  const Brep torus = Brep::TorusWelded(P(0, 0, 0), Vector3d(0, 0, 1), R, rt);
  ON_Torus ot;
  const ON_Surface* ts = FirstSurface(torus);
  d8test::Check(ts != nullptr && ts->IsTorus(&ot, 1e-9), "the torus wall is recognised as a torus");
  if (ts != nullptr && ts->IsTorus(&ot, 1e-9)) d8test::Check(std::fabs(ot.major_radius - R) < 1e-9 && std::fabs(ot.minor_radius - rt) < 1e-9, "major radius R and minor radius r");
  d8test::Check(NearRel(AreaOf(torus, "torus"), 4.0 * kPi * kPi * R * rt, 1e-6), "torus area is 4 pi^2 R r");
  d8test::Check(NearRel(VolumeOf(torus, "torus"), 2.0 * kPi * kPi * R * rt * rt, 1e-6), "torus volume is 2 pi^2 R r^2");
}

// SURFACE-0002 Add Surface Curve and SURFACE-0005 Add Surface Patch. A quarter arc of radius r extruded by h has the closed-form
// area (pi r / 2) h; a 4 x 4 planar cubic control net with unit spacing covers the 3 x 3 square exactly (area 9) with its centre at the
// middle of the net, and raising the middle points of the net lifts the middle of the patch by the Bernstein weight of those points
// (cubic Bezier weights at 1/2 are 1/8, 3/8, 3/8, 1/8, so the four central points weigh (3/8 + 3/8)^2 = 9/16 together). Blender's own
// patch size and order are not reproduced.
D8_EXTRA_TEST(RowBkgSurfaceArcSheetAndPatchHaveTheClosedFormAreas) {
  const double r = 2.5;
  ON_Arc arc(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0)), r, kPi / 2);
  ON_NurbsCurve an;
  arc.GetNurbForm(an);
  NurbsCurve quarter;
  quarter.raw() = an;
  const double h = 3.0;
  const Brep sheet = Brep::Extrude(quarter, Vector3d(0, 0, h), false);
  d8test::Check(NearRel(AreaOf(sheet, "arc sheet"), kPi * r / 2.0 * h, 1e-6), Msg("quarter-arc sheet area is (pi r / 2) h", AreaOf(sheet, "arc sheet")).c_str());

  std::vector<Point3d> net;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) net.push_back(P(i, j, 0));
  const NurbsSurface flat = NurbsSurface::FromControlGrid(net, 4, 4, 3, 3);
  const auto du = flat.Domain(0), dv = flat.Domain(1);
  d8test::Check(Near(flat.PointAt(0.5 * (du.min + du.max), 0.5 * (dv.min + dv.max)), P(1.5, 1.5, 0), 1e-12), "the middle of the flat patch is the middle of the net");
  d8test::Check(Near(flat.PointAt(du.min, dv.min), P(0, 0, 0), 1e-12) && Near(flat.PointAt(du.max, dv.max), P(3, 3, 0), 1e-12), "the corners of the patch are the corners of the net");
  d8test::Check(NearRel(AreaOf(Brep::FromSurface(flat), "flat patch"), 9.0, 1e-6), Msg("the flat patch has area 9", AreaOf(Brep::FromSurface(flat), "flat patch")).c_str());
  std::vector<Point3d> bump = net;
  const double lift = 2.0;
  for (int i : {1, 2})
    for (int j : {1, 2}) bump[i * 4 + j].z = lift;
  const NurbsSurface bsrf = NurbsSurface::FromControlGrid(bump, 4, 4, 3, 3);
  d8test::Check(Near(bsrf.PointAt(0.5 * (bsrf.Domain(0).min + bsrf.Domain(0).max), 0.5 * (bsrf.Domain(1).min + bsrf.Domain(1).max)), P(1.5, 1.5, lift * 9.0 / 16.0), 1e-12),
                "the middle of a patch with the four central net points raised by 2 lies 9/16 x 2 above the plane");
}

// TRANSFORM-0023 Move, TRANSFORM-0011 Resize, TRANSFORM-0012 Rotate, TRANSFORM-0009 Mirror, TRANSFORM-0016 Shear and TRANSFORM-0022
// Transform. The box [0,2] x [0,3] x [0,4] has volume 24, area 52 and centroid (1, 1.5, 2). Under an affine map M the image of the
// box has the volume |det M| 24, the centroid M (1, 1.5, 2) and a bounding box found by mapping the eight corners; a move and a rotation
// keep the area, a mirror keeps the volume positive, a shear along x by k z has det 1 and the area of its parallelogram faces
// (top and bottom 6 + 6, the y faces 2 x 4 = 8 each, the x faces |(0,3,0) x (k 4, 0, 4)| each). Constraints, snapping, proportional
// editing and the numeric input of the operator are not covered.
D8_EXTRA_TEST(RowBkgTransformsMapTheBoxByTheMatrixClosedForms) {
  const Brep box = Brep::BoxWelded(0, 0, 0, 2, 3, 4);
  d8test::Check(NearRel(VolumeOf(box, "box"), 24.0, 1e-9) && NearRel(AreaOf(box, "box"), 52.0, 1e-9), "box volume 24 and area 52");

  // Move.
  ON_Xform move = ON_Xform::TranslationTransformation(ON_3dVector(5, -1, 2));
  const Brep moved = box.Transform(move);
  const BoundingBox mb = moved.GetTightBoundingBox();
  d8test::Check(Near(mb.min, P(5, -1, 2), 1e-9) && Near(mb.max, P(7, 2, 6), 1e-9), "move: the bounding box shifts by (5, -1, 2)");
  d8test::Check(NearRel(VolumeOf(moved, "moved box"), 24.0, 1e-9) && NearRel(AreaOf(moved, "moved box"), 52.0, 1e-9), "move keeps volume and area");
  d8test::Check(Near(moved.VolumeMassProperties().centroid, P(6, 0.5, 4), 1e-9), "move: the centroid is (6, 0.5, 4)");

  // Resize about (1, 1, 1) by (2, 3, 0.5).
  const Brep scaled = box.Transform(Affine(2.0, 3.0, 0.5, P(1, 1, 1)));
  const BoundingBox sb = scaled.GetTightBoundingBox();
  d8test::Check(Near(sb.min, P(-1, -2, 0.5), 1e-9) && Near(sb.max, P(3, 7, 2.5), 1e-9), "resize: the bounding box is the mapped corners (-1,-2,0.5) to (3,7,2.5)");
  d8test::Check(NearRel(VolumeOf(scaled, "scaled box"), 24.0 * 3.0, 1e-9), "resize: volume is |det| x 24 = 72");
  d8test::Check(Near(scaled.VolumeMassProperties().centroid, P(1 + 2 * (1 - 1), 1 + 3 * (1.5 - 1), 1 + 0.5 * (2 - 1)), 1e-9), "resize: the centroid is the mapped centroid (1, 2.5, 1.5)");
  const Brep ball = Brep::SphereWelded(P(0, 0, 0), 1.5);
  const Brep big = ball.Transform(Affine(2.0, 2.0, 2.0, P(0, 0, 0)));
  d8test::Check(NearRel(AreaOf(big, "scaled sphere"), 4.0 * kPi * 3.0 * 3.0, 1e-6) && NearRel(VolumeOf(big, "scaled sphere"), 4.0 / 3.0 * kPi * 27.0, 1e-6),
                "uniform scale 2 of a sphere: area x 4 and volume x 8 (radius 3)");

  // Rotate by 90 degrees about the z axis through (1, 1, 0): (x, y) -> (2 - y, x).
  ON_Xform rot;
  rot.Rotation(kPi / 2, ON_3dVector(0, 0, 1), ON_3dPoint(1, 1, 0));
  const Brep turned = box.Transform(rot);
  const BoundingBox rb = turned.GetTightBoundingBox();
  d8test::Check(Near(rb.min, P(-1, 0, 0), 1e-9) && Near(rb.max, P(2, 2, 4), 1e-9), "rotate: the bounding box is (-1,0,0) to (2,2,4)");
  d8test::Check(Near(turned.VolumeMassProperties().centroid, P(2 - 1.5, 1.0, 2), 1e-9), "rotate: the centroid (1, 1.5) maps to (0.5, 1)");
  d8test::Check(NearRel(VolumeOf(turned, "rotated box"), 24.0, 1e-9) && NearRel(AreaOf(turned, "rotated box"), 52.0, 1e-9), "rotate keeps volume and area");

  // Mirror in the plane x = 3: x -> 6 - x.
  ON_Xform mir;
  mir.Mirror(ON_3dPoint(3, 0, 0), ON_3dVector(1, 0, 0));
  const Brep mirrored = box.Transform(mir);
  const BoundingBox ob = mirrored.GetTightBoundingBox();
  d8test::Check(Near(ob.min, P(4, 0, 0), 1e-9) && Near(ob.max, P(6, 3, 4), 1e-9), "mirror: the bounding box is x in [4, 6]");
  d8test::Check(VolumeOf(mirrored, "mirrored box") > 0.0 && NearRel(VolumeOf(mirrored, "mirrored box"), 24.0, 1e-9), "mirror keeps the volume positive (outward faces)");
  d8test::Check(Near(mirrored.VolumeMassProperties().centroid, P(5, 1.5, 2), 1e-9), "mirror: the centroid is (5, 1.5, 2)");

  // Shear x' = x + k z with k = 0.5.
  ON_Xform shear(1.0);
  shear[0][2] = 0.5;
  const Brep sheared = box.Transform(shear);
  const BoundingBox hb = sheared.GetTightBoundingBox();
  d8test::Check(Near(hb.min, P(0, 0, 0), 1e-9) && Near(hb.max, P(4, 3, 4), 1e-9), "shear: x spans [0, 2 + 0.5 x 4] = [0, 4]");
  d8test::Check(NearRel(VolumeOf(sheared, "sheared box"), 24.0, 1e-9), "shear keeps the volume (det 1)");
  const double ax = std::sqrt(12.0 * 12.0 + 6.0 * 6.0);  // |(0,3,0) x (2,0,4)| = |(12, 0, -6)|
  d8test::Check(NearRel(AreaOf(sheared, "sheared box"), 12.0 + 16.0 + 2.0 * ax, 1e-9), Msg("shear: area is 12 + 16 + 2 sqrt(180)", AreaOf(sheared, "sheared box")).c_str());
  d8test::Check(Near(sheared.VolumeMassProperties().centroid, P(2, 1.5, 2), 1e-9), "shear: the centroid is (1 + 0.5 x 2, 1.5, 2)");

  // Transform: rotate 30 degrees about (1,1,1), scale 2, then move; every closed form comes from the matrix.
  ON_Xform rz, sc = Affine(2.0, 2.0, 2.0, P(0, 0, 0)), mv = ON_Xform::TranslationTransformation(ON_3dVector(-3, 4, 1));
  rz.Rotation(kPi / 6, ON_3dVector(1, 1, 1), ON_3dPoint(0, 0, 0));
  const ON_Xform m = mv * sc * rz;
  const Brep composed = box.Transform(m);
  d8test::Check(NearRel(VolumeOf(composed, "composed box"), 24.0 * 8.0, 1e-9) && NearRel(AreaOf(composed, "composed box"), 52.0 * 4.0, 1e-9), "composite map: volume x 8, area x 4");
  const ON_3dPoint expect = m * ON_3dPoint(1, 1.5, 2);
  d8test::Check(Near(composed.VolumeMassProperties().centroid, expect, 1e-9), "composite map: the centroid is the matrix applied to (1, 1.5, 2)");
}

// TRANSFORM-0002 Bend. MakeBend carries a point at spine coordinate s onto the circular arc through the picked point: for the spine
// (0,0,0) -> (L,0,0) and the picked point (a, b, 0) the arc has radius R = (a^2 + b^2) / (2 b), turns by theta = 2 atan2(b, a), has its
// centre at (0, R, 0), and a point (s, h, z) with 0 < s <= L goes to centre + (R - h) (sin phi, -cos phi) with phi = theta s / L (z kept);
// a point behind the start does not move and one beyond the end continues along the end tangent (cos theta, sin theta, 0). A curve through the
// map has the closed-form arc length too: the neutral line of the bar becomes the arc of length R theta.
D8_EXTRA_TEST(RowBkgBendCarriesTheSpineOntoTheArcOfTheClosedForm) {
  const double L = 10.0, a = 5.0, b = 1.0;
  const auto map = dino8::kernel::MakeBend(P(0, 0, 0), P(L, 0, 0), P(a, b, 0));
  const double R = (a * a + b * b) / (2.0 * b), theta = 2.0 * std::atan2(b, a);
  double r_got = 0.0, th_got = 0.0;
  dino8::kernel::BendRadiusAndAngle(P(0, 0, 0), P(L, 0, 0), P(a, b, 0), r_got, th_got);
  d8test::Check(std::fabs(r_got - R) < 1e-12 && std::fabs(th_got - theta) < 1e-12, "radius (a^2 + b^2) / (2 b) = 13 and angle 2 atan2(b, a)");
  const Point3d centre = P(0, R, 0);
  double worst = 0.0;
  for (double s : {0.5, 2.0, 5.0, 7.5, 10.0}) {
    for (double hh : {0.0, 0.4, -0.7}) {
      const double phi = theta * s / L;
      const Point3d expect = P(centre.x + (R - hh) * std::sin(phi), centre.y - (R - hh) * std::cos(phi), 0.3);
      worst = std::max(worst, map(P(s, hh, 0.3)).DistanceTo(expect));
    }
  }
  d8test::Check(worst < 1e-12, Msg("points on and beside the spine land on the arc of radius R - h", worst).c_str());
  d8test::Check(map(P(-2.0, 0.5, 0.1)).DistanceTo(P(-2.0, 0.5, 0.1)) < 1e-12, "a point behind the start does not move");
  const Point3d end = P(R * std::sin(theta), R * (1 - std::cos(theta)), 0);
  d8test::Check(map(P(L + 2.0, 0, 0)).DistanceTo(end + Vector3d(std::cos(theta), std::sin(theta), 0) * 2.0) < 1e-12, "beyond the end the bar continues along the end tangent");
  NurbsCurve bar = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(L, 0, 0)}, 1), bent;
  dino8::kernel::DeformReport rep;
  d8test::Check(dino8::kernel::DeformCurve(bar, map, 1e-4, bent, &rep) == dino8::kernel::Result::Ok, "deform the bar curve");
  d8test::Check(NearRel(bent.Length(4000), R * theta, 2e-4), Msg("the bent bar has the arc length R theta", bent.Length(4000)).c_str());
}

// TRANSFORM-0020 To Sphere. Every point moves along its radial line to the distance (1 - f) d + f R: factor 0 changes nothing, factor 1
// puts all points on the sphere (the cube corners at distance sqrt 3 go to R), and with R the mean distance of the selection the
// total radius is unchanged on average. Mesh vertices go through DeformMesh with the same map.
D8_EXTRA_TEST(RowBkgToSphereMovesPointsRadiallyToTheMixedRadius) {
  const Point3d c = P(1, -1, 2);
  std::vector<Point3d> pts;
  for (int sx : {-1, 1})
    for (int sy : {-1, 1})
      for (int sz : {-1, 1}) pts.push_back(P(c.x + sx, c.y + sy, c.z + sz));
  pts.push_back(P(c.x + 3, c.y, c.z));
  const double mean = dino8::kernel::MeanDistanceFrom(c, pts);
  const double expect_mean = (8.0 * std::sqrt(3.0) + 3.0) / 9.0;
  d8test::Check(std::fabs(mean - expect_mean) < 1e-12, "the mean distance of the cube corners and one outlier is (8 sqrt 3 + 3) / 9");
  const auto full = dino8::kernel::MakeToSphere(c, mean, 1.0), none = dino8::kernel::MakeToSphere(c, mean, 0.0), half = dino8::kernel::MakeToSphere(c, mean, 0.5);
  double on_sphere = 0.0, unchanged = 0.0, mixed = 0.0, radial = 0.0;
  for (const Point3d& p : pts) {
    const double d = p.DistanceTo(c);
    on_sphere = std::max(on_sphere, std::fabs(full(p).DistanceTo(c) - mean));
    unchanged = std::max(unchanged, none(p).DistanceTo(p));
    mixed = std::max(mixed, std::fabs(half(p).DistanceTo(c) - (0.5 * d + 0.5 * mean)));
    Vector3d a = p - c, b = half(p) - c;
    a.Unitize();
    b.Unitize();
    radial = std::max(radial, (a - b).Length());
  }
  d8test::Check(on_sphere < 1e-12, "factor 1: every point is at the radius");
  d8test::Check(unchanged < 1e-12, "factor 0: no point moves");
  d8test::Check(mixed < 1e-12, "factor 1/2: the distance is (d + R) / 2");
  d8test::Check(radial < 1e-12, "the direction from the centre is kept");
  d8test::Check(full(c).DistanceTo(c) < 1e-12, "the centre stays");
  bool refused = false;
  try {
    (void)dino8::kernel::MakeToSphere(c, mean, 1.5);
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  d8test::Check(refused, "a factor above 1 is refused");
  Mesh m;
  ON_Mesh om;
  om.SetVertex(0, ON_3dPoint(2, 0, 0));
  om.SetVertex(1, ON_3dPoint(0, 2, 0));
  om.SetVertex(2, ON_3dPoint(0, 0, 4));
  om.SetTriangle(0, 0, 1, 2);
  m.raw() = om;
  const Mesh sm = dino8::kernel::DeformMesh(m, dino8::kernel::MakeToSphere(P(0, 0, 0), 3.0, 1.0));
  double md = 0.0;
  for (int i = 0; i < 3; ++i) {
    const ON_3fPoint v = sm.raw().m_V[i];
    md = std::max(md, std::fabs(P(v.x, v.y, v.z).DistanceTo(P(0, 0, 0)) - 3.0));
  }
  d8test::Check(md < 1e-6, "mesh vertices land on the sphere of radius 3");
}

// TRANSFORM-0017 Shrink/Fatten. Moving every vertex of a closed mesh by d along its vertex normal: for the regular octahedron with
// vertices at distance rho on the three axes the vertex normal is radial by symmetry, so the offset mesh is the octahedron of
// circumradius rho + d with volume 4 (rho + d)^3 / 3 and area 4 sqrt 3 (rho + d)^2; a negative distance shrinks it.
D8_EXTRA_TEST(RowBkgShrinkFattenOffsetsTheOctahedronToTheClosedFormVolume) {
  const double rho = 2.0;
  ON_Mesh om;
  const ON_3dPoint v[6] = {ON_3dPoint(rho, 0, 0), ON_3dPoint(-rho, 0, 0), ON_3dPoint(0, rho, 0), ON_3dPoint(0, -rho, 0), ON_3dPoint(0, 0, rho), ON_3dPoint(0, 0, -rho)};
  for (int i = 0; i < 6; ++i) om.SetVertex(i, v[i]);
  int f = 0;
  for (int sx : {0, 1})
    for (int sy : {0, 1})
      for (int sz : {0, 1}) {
        const int a = sx, b = 2 + sy, c = 4 + sz;
        const int sign = (sx ? -1 : 1) * (sy ? -1 : 1) * (sz ? -1 : 1);
        if (sign > 0) om.SetTriangle(f++, a, b, c);
        else om.SetTriangle(f++, a, c, b);
      }
  Mesh oct;
  oct.raw() = om;
  d8test::Check(oct.FaceCount() == 8 && oct.VertexCount() == 6, "an octahedron has 6 vertices and 8 faces");
  d8test::Check(NearRel(oct.Volume(), 4.0 / 3.0 * rho * rho * rho, 1e-6), "the base octahedron has volume 4 rho^3 / 3");
  for (double d : {0.5, 1.0, -0.5}) {
    const Mesh off = oct.Offset(d);
    const double q = rho + d;
    d8test::Check(NearRel(off.Volume(), 4.0 / 3.0 * q * q * q, 1e-5), Msg("fatten/shrink: volume is 4 (rho + d)^3 / 3 for d", d).c_str());
    d8test::Check(NearRel(off.Area(), 4.0 * std::sqrt(3.0) * q * q, 1e-5), Msg("fatten/shrink: area is 4 sqrt 3 (rho + d)^2 for d", d).c_str());
  }
}

// OBJECT-0141 Set Origin. The new origin of a solid is its centre of mass or its bounds centre; the kernel supplies both: the mass
// centroid of a cone of height h over a base in the plane z = 0 sits at h / 4 on the axis, that of a box at its centre, and the
// tight bounding box centre of the cone is (0, 0, h/2). Moving the solid by minus the chosen origin puts that point at (0, 0, 0)
// without changing the volume. Placing the 3D cursor and the object-versus-data distinction are not covered.
D8_EXTRA_TEST(RowBkgSetOriginUsesTheClosedFormCentroidAndBoundsCentre) {
  const double rr = 3.0, h = 8.0;
  const NurbsCurve tri = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(rr, 0, 0), P(0, 0, h), P(0, 0, 0)}, 1);
  const Brep cone0 = Brep::Revolve(tri, P(0, 0, 0), Vector3d(0, 0, 1));
  const auto mp = cone0.VolumeMassProperties();
  d8test::Check(NearRel(mp.volume, kPi * rr * rr * h / 3.0, 1e-6), "cone volume is pi r^2 h / 3");
  d8test::Check(Near(mp.centroid, P(0, 0, h / 4.0), 1e-6), "the mass centroid of the cone is on the axis at h / 4 above the base");
  const BoundingBox bb = cone0.GetTightBoundingBox();
  d8test::Check(Near(P(0.5 * (bb.min.x + bb.max.x), 0.5 * (bb.min.y + bb.max.y), 0.5 * (bb.min.z + bb.max.z)), P(0, 0, h / 2.0), 1e-6), "the bounds centre of the cone is at h / 2");
  const Brep moved = cone0.Transform(ON_Xform::TranslationTransformation(ON_3dVector(-mp.centroid.x, -mp.centroid.y, -mp.centroid.z)));
  const auto mp2 = moved.VolumeMassProperties();
  d8test::Check(Near(mp2.centroid, P(0, 0, 0), 1e-6) && NearRel(mp2.volume, mp.volume, 1e-9), "after moving by minus the centroid the centroid is the origin and the volume is unchanged");
  const Brep box = Brep::BoxWelded(-1, 2, 3, 5, 4, 9);
  d8test::Check(Near(box.VolumeMassProperties().centroid, P(2, 3, 6), 1e-9), "the centroid of a box is its centre");
}
