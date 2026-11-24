// Register rows of the kernel-geometry and direct-modeling factories, Blender curve, object and transform rows (round 3, batch 2),
// and the control-point weight edit that keeps the control point in place. Every test names the rows it supports and checks the kernel
// against an oracle that does not come from the code under test: the rational quadratic midpoint (P0 + 2 w P1 + P2) / (2 + 2 w),
// the weighted mean of a bilinear patch, the exact circle, the length of joined segments, the primitives' area and volume formulas, the
// volume of a prism sheared by an oblique extrusion, hand-written rotation matrices, the mass-weighted centroid of two solids and the
// radial normal of a sphere. None of them marks a row verified; selection, undo and viewport clauses of the operators are not covered
// by a kernel test.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
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
using dino8::kernel::Result;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }
std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }
bool NearRel(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::fabs(b); }
bool Near(const Point3d& a, const Point3d& b, double tol) { return a.DistanceTo(b) <= tol; }

double At(const NurbsCurve& k, double fraction) {
  const auto d = k.Domain();
  return d.min + fraction * (d.max - d.min);
}

NurbsCurve CircleXY(const Point3d& c, double r) {
  const ON_Circle circle(ON_Plane(c, ON_3dVector(0, 0, 1)), r);
  ON_NurbsCurve nurbs;
  circle.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

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

Point3d Mid(const BoundingBox& b) { return P(0.5 * (b.min.x + b.max.x), 0.5 * (b.min.y + b.max.y), 0.5 * (b.min.z + b.max.z)); }

// Rotation about the x axis then about the y axis, written out entry by entry (column vectors, right-handed).
Point3d RotX(const Point3d& p, double a) { return P(p.x, std::cos(a) * p.y - std::sin(a) * p.z, std::sin(a) * p.y + std::cos(a) * p.z); }
Point3d RotY(const Point3d& p, double a) { return P(std::cos(a) * p.x + std::sin(a) * p.z, p.y, -std::sin(a) * p.x + std::cos(a) * p.z); }

}  // namespace

// Weight edit that leaves the control point where it is (the Weight command of a NURBS editor; the app's Weight command and the JSON
// weights of AddCurve/AddSurface use it). For a rational quadratic Bezier with control points P0, P1, P2 and weights 1, w, 1 the middle
// point is C(1/2) = (P0 + 2 w P1 + P2) / (2 + 2 w), and the control point P1 itself must still read back as P1. With w = cos(pi/4) and a
// right-angle control triangle (1,0), (1,1), (0,1) the curve is the quarter circle about the origin. A bilinear patch with weights w_ij
// has the weighted mean sum(w_ij P_ij) / sum(w_ij) at (1/2, 1/2): with the weight of (1,0) set to 4 on the corners (0,0,0), (0,1,0), (1,0,5), (1,1,0)
// that is (5/7, 2/7, 20/7). The older raw setter SetWeightAt keeps its documented homogeneous meaning and is not changed.
D8_EXTRA_TEST(RowBkgWeightEditKeepsTheControlPointAndFollowsTheRationalFormulas) {
  const std::vector<Point3d> tri = {P(1, 0, 0), P(1, 1, 0), P(0, 1, 0)};
  for (double w : {0.25, 0.5, 2.0, 5.0}) {
    NurbsCurve k = NurbsCurve::FromControlPoints(tri, 2);
    d8test::Check(k.SetWeightKeepingPositionAt(1, w) == Result::Ok && k.IsRational(), "setting a weight on a non-rational curve makes it rational");
    d8test::Check(k.WeightAt(1) == w, "the weight reads back");
    d8test::Check(Near(k.ControlPointAt(1), tri[1], 1e-12) && Near(k.ControlPointAt(0), tri[0], 1e-12) && Near(k.ControlPointAt(2), tri[2], 1e-12),
                  Msg("no control point moves when the weight changes, w", w).c_str());
    const Point3d expect = P((tri[0].x + 2 * w * tri[1].x + tri[2].x) / (2 + 2 * w), (tri[0].y + 2 * w * tri[1].y + tri[2].y) / (2 + 2 * w), 0);
    d8test::Check(Near(k.PointAt(At(k, 0.5)), expect, 1e-12), Msg("midpoint equals (P0 + 2 w P1 + P2) / (2 + 2 w) for w", w).c_str());
  }
  NurbsCurve arc = NurbsCurve::FromControlPoints(tri, 2);
  arc.SetWeightKeepingPositionAt(1, std::sqrt(2.0) / 2);
  double worst = 0.0;
  for (int i = 0; i <= 100; ++i) worst = std::max(worst, std::fabs(arc.PointAt(At(arc, i / 100.0)).DistanceTo(P(0, 0, 0)) - 1.0));
  d8test::Check(worst < 1e-12, Msg("weight sqrt2/2 gives the exact quarter circle", worst).c_str());
  // Editing a second time and editing the same value again.
  d8test::Check(arc.SetWeightKeepingPositionAt(1, std::sqrt(2.0) / 2) == Result::NoOpAlreadySatisfied, "the same weight again is a no-op");
  d8test::Check(arc.SetWeightKeepingPositionAt(1, 3.0) == Result::Ok && Near(arc.ControlPointAt(1), tri[1], 1e-12), "a second edit keeps the point too");
  d8test::Check(arc.SetWeightKeepingPositionAt(1, 0.0) == Result::Failed && arc.SetWeightKeepingPositionAt(1, -1.0) == Result::Failed &&
                    arc.SetWeightKeepingPositionAt(1, std::nan("")) == Result::Failed && arc.SetWeightKeepingPositionAt(7, 2.0) == Result::Failed &&
                    arc.SetWeightKeepingPositionAt(-1, 2.0) == Result::Failed,
                "a weight that is not positive and finite, or an index out of range, is refused");
  // A larger weight pulls the curve toward its control point.
  NurbsCurve lo = NurbsCurve::FromControlPoints(tri, 2), hi = lo;
  lo.SetWeightKeepingPositionAt(1, 0.5);
  hi.SetWeightKeepingPositionAt(1, 4.0);
  d8test::Check(hi.PointAt(At(hi, 0.5)).DistanceTo(tri[1]) < lo.PointAt(At(lo, 0.5)).DistanceTo(tri[1]), "a larger weight pulls the curve toward its control point");

  const std::vector<Point3d> grid = {P(0, 0, 0), P(0, 1, 0), P(1, 0, 5), P(1, 1, 0)};  // index = u * 2 + v
  NurbsSurface s = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  d8test::Check(s.SetWeightKeepingPositionAt(1, 0, 4.0) == Result::Ok && s.IsRational(), "surface weight edit makes the surface rational");
  d8test::Check(Near(s.ControlPointAt(1, 0), P(1, 0, 5), 1e-12) && s.WeightAt(1, 0) == 4.0, "the surface control point stays at (1,0,5) with weight 4");
  const auto du = s.Domain(0), dv = s.Domain(1);
  d8test::Check(Near(s.PointAt(0.5 * (du.min + du.max), 0.5 * (dv.min + dv.max)), P(5.0 / 7.0, 2.0 / 7.0, 20.0 / 7.0), 1e-12),
                "the patch middle is the weighted mean (5/7, 2/7, 20/7)");
  d8test::Check(s.SetWeightKeepingPositionAt(1, 0, 4.0) == Result::NoOpAlreadySatisfied && s.SetWeightKeepingPositionAt(5, 0, 2.0) == Result::Failed &&
                    s.SetWeightKeepingPositionAt(0, 0, -2.0) == Result::Failed,
                "the surface edit reports a no-op and refuses bad input");
}

// CURVE-0001 Toggle Cyclic. Making a closed curve cyclic (periodic) must not change its shape: the exact circle stays at distance r
// from the centre at 361 samples (1e-9) and keeps its length 2 pi r, and a closed cubic keeps every point of its original domain
// (1e-9). The reverse (open again) and the cyclic closing of an open spline are not covered.
D8_EXTRA_TEST(RowBkgCyclicToggleKeepsTheShapeOfAClosedCurve) {
  const Point3d c = P(1, 2, 0);
  const double r = 3.0;
  NurbsCurve circle = CircleXY(c, r);
  const NurbsCurve before = circle;
  const Result res = circle.MakePeriodicExact();
  d8test::Check(res != Result::Failed, "make the circle periodic");
  d8test::Check(circle.IsPeriodic() && circle.IsClosed(), "the circle is periodic and closed afterwards");
  double worst = 0.0;
  for (int i = 0; i <= 360; ++i) worst = std::max(worst, std::fabs(circle.PointAt(At(circle, i / 360.0)).DistanceTo(c) - r));
  d8test::Check(worst < 1e-9, Msg("periodic circle samples stay at distance r", worst).c_str());
  d8test::Check(NearRel(circle.Length(4000), 2.0 * kPi * r, 1e-6), "the periodic circle has length 2 pi r");

  const std::vector<Point3d> loop = {P(0, 0, 0), P(2, 0, 0), P(2, 2, 0), P(0, 2, 0), P(0, 0, 0)};
  NurbsCurve closed = NurbsCurve::FromControlPoints(loop, 3);
  const NurbsCurve orig = closed;
  d8test::Check(closed.IsClosed(), "the closed cubic is closed");
  d8test::Check(closed.MakePeriodicExact() != Result::Failed && closed.IsPeriodic(), "make the closed cubic periodic");
  double dev = 0.0;
  const auto d = orig.Domain();
  for (int i = 0; i <= 100; ++i) {
    const double t = d.min + (d.max - d.min) * i / 100.0;
    dev = std::max(dev, orig.PointAt(t).DistanceTo(closed.PointAt(t)));
  }
  d8test::Check(dev < 1e-9, Msg("the periodic cubic equals the closed cubic over the original domain", dev).c_str());
  NurbsCurve open = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(1, 1, 0), P(2, 0, 0), P(3, 1, 0)}, 3);
  d8test::Check(open.MakePeriodicExact() == Result::Failed, "an open curve cannot be made periodic exactly (it is refused, not bent shut)");
}

// CURVE-0014 Make Segment. Joining two curves that meet end to end gives one curve whose length is the sum, which starts at the first
// start and ends at the second end: two perpendicular segments 3 and 4 give length 7; a segment followed by a quarter arc of radius r
// gives 5 + r pi / 2; the second curve may also be supplied end first; curves that do not meet are refused.
D8_EXTRA_TEST(RowBkgMakeSegmentJoinsCurvesIntoOneOfTheSummedLength) {
  NurbsCurve a = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(3, 0, 0)}, 1);
  const NurbsCurve b = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(3, 0, 0), P(3, 4, 0)}, 1);
  NurbsCurve j = a;
  d8test::Check(j.Join(b) == Result::Ok, "join two perpendicular segments");
  d8test::Check(NearRel(j.Length(), 7.0, 1e-9) && Near(j.PointAt(j.Domain().min), P(0, 0, 0), 1e-9) && Near(j.PointAt(j.Domain().max), P(3, 4, 0), 1e-9),
                "length 7 from (0,0,0) to (3,4,0)");
  d8test::Check(j.ClosestPoint(P(3, 0, 0)).DistanceTo(P(3, 0, 0)) < 1e-9, "the corner (3,0,0) lies on the joined curve");
  const NurbsCurve breversed = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(3, 4, 0), P(3, 0, 0)}, 1);
  NurbsCurve j2 = a;
  d8test::Check(j2.Join(breversed) == Result::Ok && NearRel(j2.Length(), 7.0, 1e-9) && Near(j2.PointAt(j2.Domain().max), P(3, 4, 0), 1e-9), "a second curve supplied end first is reversed and joined");

  const double r = 2.0;
  ON_Arc arc(ON_Plane(ON_3dPoint(5, 0, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0)), r, kPi / 2);  // starts at (5 + r, 0, 0)
  ON_NurbsCurve an;
  arc.GetNurbForm(an);
  NurbsCurve qa;
  qa.raw() = an;
  NurbsCurve line = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(5 + r, 0, 0)}, 1);
  d8test::Check(line.Join(qa) == Result::Ok, "join a segment and a quarter arc");
  d8test::Check(NearRel(line.Length(2000), 7.0 + r * kPi / 2.0, 1e-5), Msg("length is 7 + r pi / 2", line.Length(2000)).c_str());
  const NurbsCurve far = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(10, 10, 0), P(11, 10, 0)}, 1);
  NurbsCurve bad = a;
  bool refused = false;
  try {
    bad.Join(far);
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  d8test::Check(refused && bad.ControlPointCount() == 2, "curves that do not meet are refused and the curve is left alone");
}

// CURVE-0006 Dissolve Vertices. Removing a control point without changing the curve is knot removal: a cubic Bezier into which a knot
// was inserted at 0.4 of the domain has the original four control points back after that knot is removed (1e-9), with a measured
// deviation bound below 1e-9; a knot the shape needs (the interior knot of a cubic with five generic control points) is refused with the
// curve untouched. Choosing which Blender vertices to dissolve and the tolerance of the operator are not covered.
D8_EXTRA_TEST(RowBkgDissolveVerticesIsExactKnotRemovalAndRefusesNeededKnots) {
  const std::vector<Point3d> p = {P(0, 0, 0), P(1, 3, 1), P(4, 3, -1), P(5, 0, 2)};
  const NurbsCurve orig = NurbsCurve::FromControlPoints(p, 3);
  NurbsCurve k = orig;
  const double t = At(orig, 0.4);
  d8test::Check(k.InsertKnotAt(t, 1) == Result::Ok && k.ControlPointCount() == 5, "insert a knot (4 -> 5 control points)");
  int index = -1;
  for (int i = 0; i < k.KnotCount(); ++i)
    if (std::fabs(k.KnotAt(i) - t) < 1e-12) index = i;
  d8test::Check(index >= 0, "the inserted knot is in the knot vector");
  double bound = 1.0;
  d8test::Check(index >= 0 && k.RemoveKnotAt(index, 1e-9, &bound) == Result::Ok, "remove it again");
  d8test::Check(bound < 1e-9, Msg("the measured deviation bound is below 1e-9", bound).c_str());
  d8test::Check(k.ControlPointCount() == 4, "four control points again");
  double dev = 0.0;
  for (int i = 0; i < 4 && k.ControlPointCount() == 4; ++i) dev = std::max(dev, k.ControlPointAt(i).DistanceTo(p[i]));
  d8test::Check(dev < 1e-9, Msg("the control points are the original ones", dev).c_str());

  NurbsCurve generic = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(1, 2, 0), P(2, -2, 1), P(3, 2, 0), P(4, -1, 2)}, 3);
  const NurbsCurve snapshot = generic;
  int interior = -1;
  for (int i = 0; i < generic.KnotCount(); ++i)
    if (generic.KnotAt(i) > generic.Domain().min + 1e-9 && generic.KnotAt(i) < generic.Domain().max - 1e-9) interior = i;
  double b2 = 0.0;
  d8test::Check(interior >= 0 && generic.RemoveKnotAt(interior, 1e-9, &b2) == Result::Failed, "a knot the shape needs is refused");
  d8test::Check(b2 > 1e-9 && generic.ControlPointCount() == snapshot.ControlPointCount() && generic.PointAt(At(generic, 0.5)).DistanceTo(snapshot.PointAt(At(snapshot, 0.5))) == 0.0,
                "the refused removal reports a bound above the tolerance and leaves the curve untouched");
}

// CURVE-0008 Duplicate Curve, CURVE-0009 Add Duplicate, OBJECT-0037 and OBJECT-0038 Duplicate Objects. A duplicate is an independent copy with
// the same values: it equals the source at 101 parameters (and as a solid has the same volume and area), editing a control point of the copy
// leaves the source unchanged, and a moved duplicate of a solid is displaced by exactly the move while the source keeps its place.
// Linked duplicates (OBJECT-0039) share data and are not covered.
D8_EXTRA_TEST(RowBkgDuplicateIsAnIndependentCopyAndMoveDisplacesOnlyTheCopy) {
  const NurbsCurve src = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(1, 4, 0), P(3, -2, 1), P(5, 1, 1)}, 3);
  NurbsCurve copy = src;
  double same = 0.0;
  for (int i = 0; i <= 100; ++i) same = std::max(same, src.PointAt(At(src, i / 100.0)).DistanceTo(copy.PointAt(At(copy, i / 100.0))));
  d8test::Check(same == 0.0 && copy.ControlPointCount() == src.ControlPointCount() && copy.Degree() == src.Degree(), "the copy equals the source exactly");
  const Point3d before = src.ControlPointAt(1);
  d8test::Check(copy.SetControlPointAt(1, P(9, 9, 9)) == Result::Ok, "edit the copy");
  d8test::Check(src.ControlPointAt(1) == before && copy.ControlPointAt(1) != before, "the source is unchanged by an edit of the copy");

  const Brep box = Brep::BoxWelded(0, 0, 0, 2, 3, 4);
  const Brep dup = box;
  d8test::Check(NearRel(VolumeOf(dup, "duplicate"), 24.0, 1e-9) && NearRel(AreaOf(dup, "duplicate"), 52.0, 1e-9), "the duplicate has volume 24 and area 52");
  const Brep moved = dup.Transform(ON_Xform::TranslationTransformation(ON_3dVector(10, -5, 1.5)));
  const BoundingBox mb = moved.GetTightBoundingBox(), ob = box.GetTightBoundingBox();
  d8test::Check(Near(mb.min, P(10, -5, 1.5), 1e-9) && Near(mb.max, P(12, -2, 5.5), 1e-9), "the moved duplicate is displaced by (10,-5,1.5)");
  d8test::Check(Near(ob.min, P(0, 0, 0), 1e-9) && Near(ob.max, P(2, 3, 4), 1e-9), "the source keeps its place");
}

// CURVE-0011 Extrude Curve and Move. Extruding a closed profile along a direction that is not perpendicular to its plane gives an oblique
// prism whose volume is base area x height measured along the plane normal (Cavalieri): the 3 x 2 rectangle extruded by (1, 0, 4) has volume
// 6 x 4 = 24, its x extent grows to [0, 4], and the cap lies at z = 4 shifted by the direction. A segment extruded the same way has wall area
// 4 x 4 (base x height). Moving the extrusion with the mouse is not covered.
D8_EXTRA_TEST(RowBkgExtrudeAndMoveMakesTheObliquePrismOfCavalieriVolume) {
  const std::vector<Point3d> rect = {P(0, 0, 0), P(3, 0, 0), P(3, 2, 0), P(0, 2, 0), P(0, 0, 0)};
  const NurbsCurve rc = NurbsCurve::FromControlPoints(rect, 1);
  const Brep prism = Brep::Extrude(rc, Vector3d(1, 0, 4), true);
  d8test::Check(NearRel(VolumeOf(prism, "oblique prism"), 24.0, 1e-6), Msg("oblique prism volume is base x normal height = 24", VolumeOf(prism, "oblique prism")).c_str());
  const BoundingBox b = prism.GetTightBoundingBox();
  d8test::Check(Near(b.min, P(0, 0, 0), 1e-9) && Near(b.max, P(4, 2, 4), 1e-9), "the bounding box is (0,0,0) to (4,2,4)");
  const NurbsCurve seg = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(4, 0, 0)}, 1);
  d8test::Check(NearRel(AreaOf(Brep::Extrude(seg, Vector3d(2, 0, 4), false), "oblique sheet"), 16.0, 1e-9), "an oblique sheet over a segment of length 4 has area 4 x 4");
}

// CURVE-0022 Add Path. A path of five equally spaced collinear control points with a clamped cubic is a straight line: it starts and ends
// at the end points, stays on the line, advances monotonically and has length equal to the distance between the end points. The Blender operator's
// own order and flags for a path are from recollection (needs owner check) and this test is not credited for them.
D8_EXTRA_TEST(RowBkgPathAddIsAClampedStraightCurveThroughEquallySpacedPoints) {
  std::vector<Point3d> path;
  for (int i = 0; i < 5; ++i) path.push_back(P(2.0 * i, 1.0, -0.5));
  const NurbsCurve line = NurbsCurve::FromControlPoints(path, 3);
  d8test::Check(line.ControlPointCount() == 5 && Near(line.PointAt(line.Domain().min), path.front(), 1e-12) && Near(line.PointAt(line.Domain().max), path.back(), 1e-12),
                "five control points, clamped to the end points");
  double off = 0.0, prev = -1.0;
  bool monotone = true;
  for (int i = 0; i <= 100; ++i) {
    const Point3d p = line.PointAt(At(line, i / 100.0));
    off = std::max(off, std::max(std::fabs(p.y - 1.0), std::fabs(p.z + 0.5)));
    monotone = monotone && p.x >= prev - 1e-12;
    prev = p.x;
  }
  d8test::Check(off < 1e-12 && monotone && NearRel(line.Length(), 8.0, 1e-9) && line.IsLinear(1e-9), "the path is the straight segment of length 8");
}

// TRANSFORM-0021 Trackball. A trackball drag rotates by an angle a about the screen x axis and then b about the screen y axis through the
// pivot. The result on a point is R_y(b) R_x(a) (p - pivot) + pivot with the matrices written out; the box [0,2] x [0,3] x [0,4] about the pivot
// (1, 1, 1) keeps volume 24 and area 52, its centroid goes to the hand-computed image of (1, 1.5, 2) and the pivot stays where it is.
D8_EXTRA_TEST(RowBkgTrackballComposesTwoRotationsAboutThePivot) {
  const double a = 0.6, b = -0.9;
  const Point3d pivot = P(1, 1, 1);
  ON_Xform rx, ry;
  rx.Rotation(a, ON_3dVector(1, 0, 0), ON_3dPoint(pivot));
  ry.Rotation(b, ON_3dVector(0, 1, 0), ON_3dPoint(pivot));
  const ON_Xform m = ry * rx;
  const Brep box = Brep::BoxWelded(0, 0, 0, 2, 3, 4);
  const Brep turned = box.Transform(m);
  d8test::Check(NearRel(VolumeOf(turned, "trackballed box"), 24.0, 1e-9) && NearRel(AreaOf(turned, "trackballed box"), 52.0, 1e-9), "volume and area are unchanged");
  const auto image = [&](const Point3d& p) {
    const Point3d rel = P(p.x - pivot.x, p.y - pivot.y, p.z - pivot.z);
    const Point3d q = RotY(RotX(rel, a), b);
    return P(q.x + pivot.x, q.y + pivot.y, q.z + pivot.z);
  };
  d8test::Check(Near(turned.VolumeMassProperties().centroid, image(P(1, 1.5, 2)), 1e-9), "the centroid is the hand-written composite rotation of (1, 1.5, 2)");
  d8test::Check(Near(m * ON_3dPoint(pivot), pivot, 1e-12), "the pivot does not move");
  d8test::Check(Near(m * ON_3dPoint(3, -1, 2), image(P(3, -1, 2)), 1e-12), "the matrix equals the written-out composition at (3, -1, 2)");
}

// OBJECT-0001 Add Object, OBJECT-0003 Add Object (named) and VIEW3D-0017 Add Primitive Object. The primitives a modeler adds have the closed
// forms of their solids: box [0,2] x [0,3] x [0,4] volume 24 area 52; sphere r 1.5 volume 4/3 pi r^3 area 4 pi r^2; cylinder r 1.25 h 3 volume
// pi r^2 h area 2 pi r (r + h); cone r 2 h 5 volume pi r^2 h / 3 area pi r (r + slant); torus R 4 r 1 volume 2 pi^2 R r^2 area 4 pi^2 R r; and
// each has the bounding box of its definition. Placement at the cursor, the redo panel and the interactive drag are not covered.
D8_EXTRA_TEST(RowBkgAddPrimitivesHaveTheClosedFormVolumesAreasAndBounds) {
  const Brep box = Brep::BoxWelded(0, 0, 0, 2, 3, 4);
  d8test::Check(NearRel(VolumeOf(box, "box"), 24.0, 1e-9) && NearRel(AreaOf(box, "box"), 52.0, 1e-9), "box: volume 24, area 52");
  const Point3d c = P(1, -2, 3);
  const Brep sph = Brep::SphereWelded(c, 1.5);
  d8test::Check(NearRel(VolumeOf(sph, "sphere"), 4.0 / 3.0 * kPi * 3.375, 1e-6) && NearRel(AreaOf(sph, "sphere"), 4.0 * kPi * 2.25, 1e-6), "sphere: 4/3 pi r^3 and 4 pi r^2");
  const BoundingBox sb = sph.GetTightBoundingBox();
  d8test::Check(Near(sb.min, P(c.x - 1.5, c.y - 1.5, c.z - 1.5), 1e-6) && Near(sb.max, P(c.x + 1.5, c.y + 1.5, c.z + 1.5), 1e-6), "sphere: bounding box is the centre plus or minus r");
  const double rc = 1.25, h = 3.0;
  const Brep cyl = Brep::CylinderWelded(P(0, 0, 0), Vector3d(0, 0, 1), rc, h);
  d8test::Check(NearRel(VolumeOf(cyl, "cylinder"), kPi * rc * rc * h, 1e-6) && NearRel(AreaOf(cyl, "cylinder"), 2.0 * kPi * rc * (rc + h), 1e-6), "cylinder: pi r^2 h and 2 pi r (r + h)");
  const BoundingBox cb = cyl.GetTightBoundingBox();
  d8test::Check(Near(cb.min, P(-rc, -rc, 0), 1e-6) && Near(cb.max, P(rc, rc, h), 1e-6), "cylinder: bounding box (+-r, +-r, [0, h])");
  const double rk = 2.0, hk = 5.0;
  const Brep cone = Brep::ConeWelded(P(0, 0, 0), Vector3d(0, 0, 1), rk, 0.0, hk);
  d8test::Check(NearRel(VolumeOf(cone, "cone"), kPi * rk * rk * hk / 3.0, 1e-6) && NearRel(AreaOf(cone, "cone"), kPi * rk * (rk + std::sqrt(rk * rk + hk * hk)), 1e-6), "cone: pi r^2 h / 3 and pi r (r + slant)");
  const Brep tor = Brep::TorusWelded(P(0, 0, 0), Vector3d(0, 0, 1), 4.0, 1.0);
  d8test::Check(NearRel(VolumeOf(tor, "torus"), 2.0 * kPi * kPi * 4.0, 1e-6) && NearRel(AreaOf(tor, "torus"), 4.0 * kPi * kPi * 4.0, 1e-6), "torus: 2 pi^2 R r^2 and 4 pi^2 R r");
  const BoundingBox tb = tor.GetTightBoundingBox();
  d8test::Check(Near(tb.min, P(-5, -5, -1), 1e-6) && Near(tb.max, P(5, 5, 1), 1e-6), "torus: bounding box (+-(R + r), +-(R + r), +-r)");
}

// OBJECT-0004 Align Objects. Aligning object B to object A on one axis moves B along that axis only: Centers aligns the bounding box centres,
// Negative Sides the minima, Positive Sides the maxima. A = [0,2] x [0,3] x [0,4] and B = [10,13] x [-5,-1] x [2,3]: centres on x moves B by
// 1 - 11.5 = -10.5, minima on y by +5, maxima on z by +1; the other coordinates of B do not change and its volume stays 12. The mode names and the
// options "relative to" are from recollection of the Blender manual (needs owner/independent check) and are not credited.
D8_EXTRA_TEST(RowBkgAlignObjectsMovesTheSecondBoxToTheChosenSideOfTheFirst) {
  const Brep a = Brep::BoxWelded(0, 0, 0, 2, 3, 4), b = Brep::BoxWelded(10, -5, 2, 13, -1, 3);
  const BoundingBox ab = a.GetTightBoundingBox(), bb = b.GetTightBoundingBox();
  const double dx = Mid(ab).x - Mid(bb).x, dy = ab.min.y - bb.min.y, dz = ab.max.z - bb.max.z;
  d8test::Check(std::fabs(dx - (-10.5)) < 1e-9, "the centre offset on x is 1 - 11.5 = -10.5");
  d8test::Check(std::fabs(dy - 5.0) < 1e-9 && std::fabs(dz - 1.0) < 1e-9, "the minimum offset on y is +5 and the maximum offset on z is +1");
  const Brep bx = b.Transform(ON_Xform::TranslationTransformation(ON_3dVector(dx, 0, 0)));
  const Brep by = b.Transform(ON_Xform::TranslationTransformation(ON_3dVector(0, dy, 0)));
  const Brep bz = b.Transform(ON_Xform::TranslationTransformation(ON_3dVector(0, 0, dz)));
  d8test::Check(std::fabs(Mid(bx.GetTightBoundingBox()).x - Mid(ab).x) < 1e-9 && std::fabs(bx.GetTightBoundingBox().min.y - bb.min.y) < 1e-9, "centres aligned on x, y and z untouched");
  d8test::Check(std::fabs(by.GetTightBoundingBox().min.y - ab.min.y) < 1e-9 && std::fabs(by.GetTightBoundingBox().max.x - bb.max.x) < 1e-9, "minima aligned on y, x untouched");
  d8test::Check(std::fabs(bz.GetTightBoundingBox().max.z - ab.max.z) < 1e-9 && std::fabs(bz.GetTightBoundingBox().min.x - bb.min.x) < 1e-9, "maxima aligned on z, x untouched");
  d8test::Check(NearRel(VolumeOf(bz, "aligned box"), 12.0, 1e-9), "alignment keeps the volume");
}

// OBJECT-0077 Join. Joining solids keeps every face: two boxes [0,2] x [0,3] x [0,4] (volume 24, centroid (1, 1.5, 2)) and [10,13] x [-5,-1] x [2,3]
// (volume 12, centroid (11.5, -3, 2.5)) give a body with 12 faces, two lumps, the summed volume and area (24 + 12, 52 + 38) and the
// volume-weighted centroid (24 A + 12 B) / 36 = (9/2, 0, 13/6).
D8_EXTRA_TEST(RowBkgJoinKeepsEveryFaceAndTheMassWeightedCentroid) {
  const Brep a = Brep::BoxWelded(0, 0, 0, 2, 3, 4), b = Brep::BoxWelded(10, -5, 2, 13, -1, 3);
  const Brep j = Brep::Compound({a, b});
  d8test::Check(j.FaceCount() == a.FaceCount() + b.FaceCount() && j.FaceCount() == 12, "twelve faces");
  d8test::Check(j.LumpFaceRanges().size() == 2, "two lumps");
  d8test::Check(NearRel(AreaOf(j, "joined body"), 52.0 + 38.0, 1e-9), "the area is the sum 52 + 38");
  const auto mp = j.VolumeMassProperties();
  d8test::Check(NearRel(mp.volume, 36.0, 1e-9), "the volume is 24 + 12");
  d8test::Check(Near(mp.centroid, P(4.5, 0.0, 13.0 / 6.0), 1e-9), "the centroid is the volume-weighted mean (9/2, 0, 13/6)");
}

// OBJECT-0211 Apply Object Transform and OBJECT-0246 Apply Visual Transform. Applying a transform to a mesh bakes it into the vertices: every vertex
// of the octahedron (circumradius 2) lands at M v (1e-5, float vertices), the face count is unchanged, the volume is |det M| times the original
// 4 rho^3 / 3, and applying the inverse restores the vertices. The visual transform of a child with a parent is parent x local: baking that one
// matrix equals baking the local matrix and then the parent matrix. Reset of the object matrix and the data-sharing checks are not covered.
D8_EXTRA_TEST(RowBkgApplyTransformBakesTheMatrixIntoTheVertices) {
  ON_Mesh om;
  const double rho = 2.0;
  const ON_3dPoint v[6] = {ON_3dPoint(rho, 0, 0), ON_3dPoint(-rho, 0, 0), ON_3dPoint(0, rho, 0), ON_3dPoint(0, -rho, 0), ON_3dPoint(0, 0, rho), ON_3dPoint(0, 0, -rho)};
  for (int i = 0; i < 6; ++i) om.SetVertex(i, v[i]);
  int f = 0;
  for (int sx : {0, 1})
    for (int sy : {0, 1})
      for (int sz : {0, 1}) {
        const int sign = (sx ? -1 : 1) * (sy ? -1 : 1) * (sz ? -1 : 1);
        if (sign > 0) om.SetTriangle(f++, sx, 2 + sy, 4 + sz);
        else om.SetTriangle(f++, sx, 4 + sz, 2 + sy);
      }
  Mesh oct;
  oct.raw() = om;
  ON_Xform scale(1.0), rot, move = ON_Xform::TranslationTransformation(ON_3dVector(3, -1, 2));
  scale[0][0] = 1.5;
  scale[1][1] = 1.5;
  scale[2][2] = 1.5;
  rot.Rotation(0.8, ON_3dVector(1, 2, 3), ON_3dPoint(0, 0, 0));
  const ON_Xform local = move * rot * scale;
  const Mesh baked = oct.Transform(local);
  d8test::Check(baked.FaceCount() == oct.FaceCount() && baked.VertexCount() == 6, "the topology is unchanged");
  double worst = 0.0;
  for (int i = 0; i < 6; ++i) {
    const ON_3dPoint expect = local * v[i];
    const ON_3fPoint got = baked.raw().m_V[i];
    worst = std::max(worst, ON_3dPoint(got.x, got.y, got.z).DistanceTo(expect));
  }
  d8test::Check(worst < 1e-5, Msg("every vertex is M v", worst).c_str());
  d8test::Check(NearRel(baked.Volume(), 1.5 * 1.5 * 1.5 * 4.0 / 3.0 * rho * rho * rho, 1e-5), "the volume is |det M| x 4 rho^3 / 3 = 3.375 x 10.667");
  ON_Xform inv = local;
  d8test::Check(inv.Invert(), "the matrix inverts");
  const Mesh back = baked.Transform(inv);
  double restore = 0.0;
  for (int i = 0; i < 6; ++i) {
    const ON_3fPoint got = back.raw().m_V[i];
    restore = std::max(restore, ON_3dPoint(got.x, got.y, got.z).DistanceTo(v[i]));
  }
  d8test::Check(restore < 1e-4, Msg("applying the inverse restores the vertices", restore).c_str());
  ON_Xform parent;
  parent.Rotation(-0.3, ON_3dVector(0, 0, 1), ON_3dPoint(1, 1, 0));
  const ON_Xform visual = parent * local;
  const Mesh one = oct.Transform(visual), two = oct.Transform(local).Transform(parent);
  double seq = 0.0;
  for (int i = 0; i < 6; ++i) {
    const ON_3fPoint p = one.raw().m_V[i], q = two.raw().m_V[i];
    seq = std::max(seq, ON_3dPoint(p.x, p.y, p.z).DistanceTo(ON_3dPoint(q.x, q.y, q.z)));
  }
  d8test::Check(seq < 1e-4, Msg("baking parent x local equals local then parent", seq).c_str());
}

// OBJECT-0212 Look at Surface. Pointing an object's z axis along the surface normal at a picked point: for a NURBS sphere of radius r about
// c the normal at any (u, v) is the radial unit vector +-(p - c)/r (the sign is the surface orientation), and the rotation about the axis
// z x n through the angle acos(z . n), written out by Rodrigues' formula, takes z to n, keeps lengths and moves a box's centroid to the
// hand-computed image. Picking the point with the mouse is not covered.
D8_EXTRA_TEST(RowBkgLookAtSurfaceTurnsTheZAxisToTheSphereNormal) {
  const Point3d c = P(1, -1, 2);
  const double r = 2.5;
  ON_NurbsSurface ns;
  ON_Sphere(ON_3dPoint(c), r).GetNurbForm(ns);
  NurbsSurface sphere;
  sphere.raw() = ns;
  const auto du = sphere.Domain(0), dv = sphere.Domain(1);
  double worst_dir = 0.0, worst_len = 0.0;
  Vector3d last(0, 0, 1);
  for (double fu : {0.1, 0.35, 0.6, 0.85})
    for (double fv : {0.2, 0.5, 0.8}) {
      const double u = du.min + fu * (du.max - du.min), v = dv.min + fv * (dv.max - dv.min);
      const Point3d p = sphere.PointAt(u, v);
      Vector3d radial = p - c;
      radial.Unitize();
      Vector3d n = sphere.NormalAt(u, v);
      worst_len = std::max(worst_len, std::fabs(n.Length() - 1.0));
      worst_dir = std::max(worst_dir, std::min((n - radial).Length(), (n + radial).Length()));
      last = n;
    }
  d8test::Check(worst_len < 1e-9 && worst_dir < 1e-6, Msg("the sphere normal is +-radial and of unit length", worst_dir).c_str());
  const Vector3d z(0, 0, 1);
  const Vector3d n = last;
  Vector3d axis = ON_CrossProduct(z, n);
  const double s = axis.Length(), co = ON_DotProduct(z, n);
  d8test::Check(s > 1e-3, "the chosen normal is not parallel to z");
  axis.Unitize();
  const auto rodrigues = [&](const Vector3d& x) {  // v cos + (k x v) sin + k (k . v)(1 - cos), with sin = s and cos = co
    const Vector3d kx = ON_CrossProduct(axis, x);
    return x * co + kx * s + axis * (ON_DotProduct(axis, x) * (1.0 - co));
  };
  d8test::Check((rodrigues(z) - n).Length() < 1e-12, "Rodrigues' rotation takes z to the normal");
  ON_Xform m(1.0);
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      Vector3d e(0, 0, 0);
      e[col] = 1.0;
      m[row][col] = rodrigues(e)[row];
    }
  }
  const Brep box = Brep::BoxWelded(-1, -1, 0, 1, 1, 2);
  const Brep turned = box.Transform(m);
  const Vector3d cen = rodrigues(Vector3d(0, 0, 1));
  d8test::Check(Near(turned.VolumeMassProperties().centroid, P(cen.x, cen.y, cen.z), 1e-9) && NearRel(VolumeOf(turned, "turned box"), 8.0, 1e-9),
                "the box centroid (0, 0, 1) goes to R z and the volume stays 8");
}
