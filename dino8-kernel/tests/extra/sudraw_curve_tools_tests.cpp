// SketchUp-style drawing and edit rows on edges and curves, kernel part: dividing an edge, welding edges into one curve, offsetting an
// outline and an arc, a lathe (revolve) checked against Pappus, and copies along a path. Each expectation is a closed form worked out in the
// comment of its test (lengths from coordinates, Pappus' centroid theorem, the offset of a rectangle, the arc-length stations of a path),
// not a value read off the code under test.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/pattern.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::CurveKind;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

NurbsCurve Poly(const std::vector<Point3d>& pts) { return NurbsCurve::FromControlPoints(pts, 1); }

double Dist(const Point3d& a, const Point3d& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

NurbsCurve ArcCurve(const ON_3dPoint& center, double r, double sweep_radians) {
  const ON_Arc arc(ON_Circle(ON_Plane(center, ON_3dVector(0, 0, 1)), r), sweep_radians);
  ON_NurbsCurve nurbs;
  arc.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

}  // namespace

// DRAW-0042, "Divide edge into equal segments; Split; Explode Curve; Weld; Reverse faces; Align face." Acceptance sketch: "Divide a 3 m edge into
// 5; 5 edges of 0.6 m within 1e-12." The edge runs from (1,2,0) to (1,2,3) (not on an axis through the origin, to keep the check honest about
// where the points are): DivideByCount(5) gives 5 + 1 = 6 stations; consecutive stations are 3 / 5 = 0.6 apart (read off the points by their
// coordinates, not from the parameters), the first and last are the edge ends, and the same holds for a slanted edge whose length 3 comes
// from a 3-4-5 triangle scaled (0,0,0) to (1.8, 2.4, 0).
D8_EXTRA_TEST(SuDivideEdgeIntoFiveGivesFiveSegmentsOfPointSixMetres) {
  const NurbsCurve edge = Poly({P(1, 2, 0), P(1, 2, 3)});
  const std::vector<double> t = edge.DivideByCount(5);
  d8test::Check(t.size() == 6, Msg("6 stations (5 segments)", static_cast<double>(t.size())).c_str());
  if (t.size() != 6) return;
  double worst = 0.0;
  for (size_t i = 0; i + 1 < t.size(); ++i) worst = std::max(worst, std::fabs(Dist(edge.PointAt(t[i]), edge.PointAt(t[i + 1])) - 0.6));
  d8test::Check(worst < 1e-12, Msg("every segment is 0.6 within 1e-12", worst).c_str());
  d8test::Check(Dist(edge.PointAt(t.front()), P(1, 2, 0)) < 1e-12 && Dist(edge.PointAt(t.back()), P(1, 2, 3)) < 1e-12, "the first and last stations are the ends of the edge");
  d8test::Check(std::fabs(edge.PointAt(t[2]).z - 1.2) < 1e-12 && std::fabs(edge.PointAt(t[3]).z - 1.8) < 1e-12, "the middle stations are at z = 1.2 and 1.8");
  const NurbsCurve slant = Poly({P(0, 0, 0), P(1.8, 2.4, 0)});
  const std::vector<double> u = slant.DivideByCount(5);
  double worst2 = 0.0;
  for (size_t i = 0; i + 1 < u.size(); ++i) worst2 = std::max(worst2, std::fabs(Dist(slant.PointAt(u[i]), slant.PointAt(u[i + 1])) - 0.6));
  d8test::Check(u.size() == 6 && worst2 < 1e-12, Msg("a slanted 3 m edge divides into five 0.6 m segments", worst2).c_str());
}

// DRAW-0044, "Weld joins connected edges into a single curve entity (polyline)". Acceptance sketch: "Weld 5 edges; one curve entity with 5
// segments." Five edges (0,0)-(2,0), (2,0)-(2,1), (2,1)-(4,1), (4,1)-(4,3), (4,3)-(5,3) are joined one after the other: one curve whose
// classification is a polyline of 6 vertices (those points in order) and whose length is the sum of the five lengths 2 + 1 + 2 + 2 + 1 = 8.
// An edge that does not touch the end (a 0.5 gap) is refused with an exception and leaves the curve as it was.
D8_EXTRA_TEST(SuWeldFiveEdgesMakesOneCurveWithFiveSegments) {
  const std::vector<Point3d> corners = {P(0, 0, 0), P(2, 0, 0), P(2, 1, 0), P(4, 1, 0), P(4, 3, 0), P(5, 3, 0)};
  NurbsCurve whole = Poly({corners[0], corners[1]});
  for (size_t i = 1; i + 1 < corners.size(); ++i) {
    const Result r = whole.Join(Poly({corners[i], corners[i + 1]}));
    d8test::Check(r == Result::Ok, (std::string("joining edge ") + std::to_string(i + 1)).c_str());
  }
  const dino8::kernel::CurveClassification k = whole.Classify();
  d8test::Check(k.kind == CurveKind::Polyline, "the welded edges classify as one polyline");
  d8test::Check(k.vertices.size() == 6, Msg("the polyline has 6 vertices, that is 5 segments", static_cast<double>(k.vertices.size())).c_str());
  double worst = 0.0;
  for (size_t i = 0; i < k.vertices.size() && i < corners.size(); ++i) worst = std::max(worst, Dist(k.vertices[i], corners[i]));
  d8test::Check(worst < 1e-9, Msg("the vertices are the five edges' end points in order", worst).c_str());
  d8test::Check(std::fabs(whole.Length() - 8.0) < 1e-6, Msg("length is 2 + 1 + 2 + 2 + 1 = 8", whole.Length()).c_str());
  d8test::Check(!whole.IsClosed(), "the welded chain is open");
  NurbsCurve chain = Poly({P(0, 0, 0), P(1, 0, 0)});
  bool refused = false;
  try { (void)chain.Join(Poly({P(1.5, 0, 0), P(2.5, 0, 0)})); } catch (const std::exception&) { refused = true; }
  d8test::Check(refused && chain.Classify().kind == CurveKind::Line && std::fabs(chain.Length() - 1.0) < 1e-9, "an edge 0.5 away from the end is refused and the chain is unchanged (still the one line of length 1)");
}

// EDIT-0028, "Offset creates an equidistant copy of selected coplanar edges or a face with typed distance". Acceptance sketch: "Offset a 4 m by
// 3 m rectangle by 0.2 m inward: inner rectangle 3.6 by 2.6 exactly." The closed outline (0,0)-(4,0)-(4,3)-(0,3) offset by 0.2 toward the
// inside is the rectangle [0.2, 3.8] x [0.2, 2.8] (corners meet at the intersections of the offset sides): sides 3.6 and 2.6, perimeter 12.4,
// area 9.36. Outward by 0.2 it is [-0.2, 4.2] x [-0.2, 3.2]: 4.4 by 3.4. The two signs of the distance give the two answers; which is inward
// is decided by the bounding box (smaller), not by the code.
D8_EXTRA_TEST(SuOffsetOfTheFourByThreeRectangleIsTheThreePointSixByTwoPointSix) {
  const NurbsCurve rect = Poly({P(0, 0, 0), P(4, 0, 0), P(4, 3, 0), P(0, 3, 0), P(0, 0, 0)});
  NurbsCurve a, b;
  d8test::Check(rect.OffsetInPlane(0.2, a) == Result::Ok && rect.OffsetInPlane(-0.2, b) == Result::Ok, "both signs of the offset succeed");
  auto extent = [](const NurbsCurve& c, double& w, double& h, double& x0, double& y0) {
    const dino8::kernel::CurveClassification k = c.Classify();
    double xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9;
    for (const Point3d& p : k.vertices) { xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x); ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y); }
    w = xmax - xmin; h = ymax - ymin; x0 = xmin; y0 = ymin;
    return k.kind == CurveKind::Polyline;
  };
  double wa, ha, xa, ya, wb, hb, xb, yb;
  d8test::Check(extent(a, wa, ha, xa, ya) && extent(b, wb, hb, xb, yb), "both offsets are polylines");
  const bool a_inward = wa < wb;
  const double wi = a_inward ? wa : wb, hi = a_inward ? ha : hb, xi = a_inward ? xa : xb, yi = a_inward ? ya : yb;
  const double wo = a_inward ? wb : wa, ho = a_inward ? hb : ha;
  d8test::Check(std::fabs(wi - 3.6) < 1e-12 && std::fabs(hi - 2.6) < 1e-12, Msg("inner rectangle is 3.6 by 2.6", wi).c_str());
  d8test::Check(std::fabs(xi - 0.2) < 1e-12 && std::fabs(yi - 0.2) < 1e-12, "the inner rectangle starts at (0.2, 0.2)");
  d8test::Check(std::fabs(wo - 4.4) < 1e-12 && std::fabs(ho - 3.4) < 1e-12, Msg("outer rectangle is 4.4 by 3.4", wo).c_str());
  const NurbsCurve& inner = a_inward ? a : b;
  d8test::Check(std::fabs(inner.Length() - 12.4) < 1e-6 && inner.IsClosed(), Msg("inner outline is closed with perimeter 2 (3.6 + 2.6) = 12.4", inner.Length()).c_str());
}

// EDIT-0029, "Offsetting an arc creates a curve entity unless it has three or more segments; arc entity is preserved for circular arcs".
// Acceptance sketch: "Offset a 12-segment arc; arc entity remains with radius +/- d." A quarter arc of radius 5 about (1, 2) is offset by 0.75
// in both directions: both results are still arcs (the classification says Arc, not a free-form curve), concentric (the same centre within
// 1e-9), with the same sweep angle pi / 2, and radii 5 + 0.75 = 5.75 and 5 - 0.75 = 4.25 (which is which is read from the radii, one each);
// arc lengths are r times the sweep: 5.75 * pi / 2 and 4.25 * pi / 2.
D8_EXTRA_TEST(SuOffsetOfAnArcStaysAnArcWithTheRadiusChangedByTheDistance) {
  const NurbsCurve arc = ArcCurve(ON_3dPoint(1, 2, 0), 5.0, kPi / 2);
  const dino8::kernel::CurveClassification base = arc.Classify();
  d8test::Check(base.kind == CurveKind::Arc && std::fabs(base.arc.Radius() - 5.0) < 1e-9, "the fixture is an arc of radius 5");
  NurbsCurve a, b;
  d8test::Check(arc.OffsetInPlane(0.75, a) == Result::Ok && arc.OffsetInPlane(-0.75, b) == Result::Ok, "both offsets succeed");
  const dino8::kernel::CurveClassification ka = a.Classify(), kb = b.Classify();
  d8test::Check(ka.kind == CurveKind::Arc && kb.kind == CurveKind::Arc, "both offsets are still arcs");
  const double ra = ka.arc.Radius(), rb = kb.arc.Radius();
  const double lo = std::min(ra, rb), hi = std::max(ra, rb);
  d8test::Check(std::fabs(lo - 4.25) < 1e-9 && std::fabs(hi - 5.75) < 1e-9, Msg("radii are 5 -/+ 0.75", hi).c_str());
  d8test::Check(Dist(P(ka.arc.Center().x, ka.arc.Center().y, ka.arc.Center().z), P(1, 2, 0)) < 1e-9 && Dist(P(kb.arc.Center().x, kb.arc.Center().y, kb.arc.Center().z), P(1, 2, 0)) < 1e-9, "both are concentric with the original");
  d8test::Check(std::fabs(ka.arc.AngleRadians() - kPi / 2) < 1e-9 && std::fabs(kb.arc.AngleRadians() - kPi / 2) < 1e-9, "the sweep angle is unchanged (pi / 2)");
  d8test::Check(std::fabs(a.Length() - ra * kPi / 2) < 1e-6 && std::fabs(b.Length() - rb * kPi / 2) < 1e-6, "arc lengths are radius times the sweep");
}

// EDIT-0026, "Follow Me with a circle path lathes a half-profile into a solid of revolution". Acceptance sketch: "Revolve a 1x1 profile at
// radius 2; volume equals Pappus value within segment tolerance." The profile is the unit square [2,3] x [0,1] in the xz plane, revolved about
// the z axis. Pappus: volume = area * 2 pi * (distance of the centroid from the axis) = 1 * 2 pi * 2.5 = 5 pi. The same square revolved a
// half turn (pi) gives 5 pi / 2, and the surface of the full solid is perimeter-weighted: each side's area times its centroid radius, 2 pi *
// (2 * 1 on the inner and outer walls at radii 2 and 3: 1 * 2 pi 2 + 1 * 2 pi 3) + two annuli pi (9 - 4) each = 10 pi + 10 pi = 20 pi.
D8_EXTRA_TEST(SuLatheOfAUnitSquareMatchesPappusTheorem) {
  const NurbsCurve square = Poly({P(2, 0, 0), P(3, 0, 0), P(3, 0, 1), P(2, 0, 1), P(2, 0, 0)});
  const Brep full = Brep::Revolve(square, P(0, 0, 0), Vector3d(0, 0, 1));
  const double v = full.Volume();
  d8test::Check(std::fabs(v - 5 * kPi) / (5 * kPi) < 1e-6, Msg("full turn volume is 5 pi (Pappus 1 * 2 pi * 2.5)", v - 5 * kPi).c_str());
  const double area = full.Area();
  d8test::Check(std::fabs(area - 20 * kPi) / (20 * kPi) < 1e-6, Msg("surface is 20 pi (walls 10 pi + two annuli 10 pi)", area - 20 * kPi).c_str());
  const Brep half = Brep::Revolve(square, P(0, 0, 0), Vector3d(0, 0, 1), kPi);
  d8test::Check(std::fabs(half.Volume() - 2.5 * kPi) / (2.5 * kPi) < 1e-6, Msg("a half turn gives 5 pi / 2", half.Volume() - 2.5 * kPi).c_str());
}

// SOLID-0015 Copy Along, "places copies of a group or component along a whole path ... distribution by count". Acceptance sketch: "10 copies by
// count along a 10 m path: spacing is path length over (n-1) within 1e-9." The path is the straight 10 m edge (2,1,0) to (8,9,0) (length
// sqrt(36 + 64) = 10): copy k of a point at the path start lands at start + k (end - start) / 9, so consecutive copies are 10 / 9 apart and
// the last copy is at the end of the path. With orient off the copies are pure translations, checked vertex by vertex on a unit box.
D8_EXTRA_TEST(SuCopyAlongTenCopiesByCountAreSpacedPathLengthOverNMinusOne) {
  const NurbsCurve path = Poly({P(2, 1, 0), P(8, 9, 0)});
  const int n = 10;
  const std::vector<Brep> copies = dino8::kernel::ArrayBrepAlongCurve(Brep::BoxWelded(2, 1, 0, 3, 2, 1), path, n, false);
  d8test::Check(static_cast<int>(copies.size()) == n, "ten copies");
  if (static_cast<int>(copies.size()) != n) return;
  double worst = 0.0;
  Point3d prev(0, 0, 0);
  for (int k = 0; k < n; ++k) {
    const std::vector<Brep::PlanarFace> f = copies[static_cast<size_t>(k)].PlanarFaces();
    double xmin = 1e9, ymin = 1e9;
    for (const Brep::PlanarFace& face : f) for (const Point3d& p : face.loop) { xmin = std::min(xmin, p.x); ymin = std::min(ymin, p.y); }
    const double ex = 2.0 + 6.0 * k / 9.0, ey = 1.0 + 8.0 * k / 9.0;
    worst = std::max(worst, std::max(std::fabs(xmin - ex), std::fabs(ymin - ey)));
    const Point3d here(xmin, ymin, 0);
    if (k > 0) worst = std::max(worst, std::fabs(Dist(here, prev) - 10.0 / 9.0));
    prev = here;
  }
  d8test::Check(worst < 1e-9, Msg("copy k sits at k/9 of the path and consecutive copies are 10/9 apart within 1e-9", worst).c_str());
}
