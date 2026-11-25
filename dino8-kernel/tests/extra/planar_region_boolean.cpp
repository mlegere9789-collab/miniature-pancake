// PlanarRegionBoolean / PlanarRegionBooleanN: regions with holes, N-way folds and symmetric difference of planar curve regions.
//
// Independent oracles:
//   * the multi-circle cases use a SCANLINE INTEGRAL: for each vertical line the combined y-intervals of the discs (union, intersection,
//     first-minus-rest are plain interval algebra), integrated over x - no shared code with the construction and no inclusion-exclusion
//     bookkeeping to get wrong;
//   * the ring cases use closed forms: ring area pi (R^2 - r^2), the circular lens of two circles
//     r1^2 a1 + r2^2 a2 - 1/2 sqrt((-d+r1+r2)(d+r1-r2)(d-r1+r2)(d+r1+r2)), a1 = acos((d^2 + r1^2 - r2^2) / (2 d r1));
//   * symmetric difference: area(a) + area(b) - 2 area(a n b); for three discs the odd-coverage identity
//     a + b + c - 2 (ab + ac + bc) + 4 abc with every pairwise and triple intersection from the scanline.
// Area / length of the OUTPUT loops are measured by dense sampling (shoelace / chord sum), independent of the construction.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "dino8/kernel/planar_curve_boolean.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::CurveRegionOp;
using dino8::kernel::PlanarCurveBoolean;
using dino8::kernel::PlanarCurveBooleanResult;
using dino8::kernel::PlanarCurveRegion;
using dino8::kernel::PlanarRegionBoolean;
using dino8::kernel::PlanarRegionBooleanN;

constexpr double kPi = 3.14159265358979323846;

ON_Plane XY() { return ON_Plane::World_xy; }

ON_NurbsCurve Circle(const ON_Plane& pl, double cx, double cy, double r, bool clockwise = false) {
  ON_Plane p = pl;
  p.SetOrigin(pl.PointAt(cx, cy));
  ON_NurbsCurve c;
  ON_Circle(p, r).GetNurbForm(c);
  if (clockwise) c.Reverse();
  return c;
}

std::vector<ON_3dPoint> SamplePoints(const ON_Curve& c, int per_span) {
  const int spans = c.SpanCount();
  std::vector<double> sv(spans + 1);
  c.GetSpanVector(sv.data());
  std::vector<ON_3dPoint> pts;
  for (int s = 0; s < spans; ++s)
    for (int k = 0; k < per_span; ++k) pts.push_back(c.PointAt(sv[s] + (sv[s + 1] - sv[s]) * k / per_span));
  pts.push_back(c.PointAt(sv[spans]));
  return pts;
}

struct Totals {
  double area = 0, length = 0;
  int outer = 0, holes = 0;
  bool orientation_matches_flags = true;
};

Totals Sum(const PlanarCurveBooleanResult& r, const ON_Plane& pl) {
  Totals t;
  for (size_t i = 0; i < r.loops.size(); ++i) {
    const std::vector<ON_3dPoint> pts = SamplePoints(r.loops[i], 10000);
    double a = 0, l = 0;
    for (size_t k = 1; k < pts.size(); ++k) {
      double u0, v0, u1, v1;
      pl.ClosestPointTo(pts[k - 1], &u0, &v0);
      pl.ClosestPointTo(pts[k], &u1, &v1);
      a += 0.5 * (u0 * v1 - u1 * v0);
      l += pts[k - 1].DistanceTo(pts[k]);
    }
    t.area += a;
    t.length += l;
    if (r.loop_is_hole[i]) {
      ++t.holes;
      if (a >= 0) t.orientation_matches_flags = false;
    } else {
      ++t.outer;
      if (a <= 0) t.orientation_matches_flags = false;
    }
  }
  return t;
}

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

struct Lens {
  double area;
};
Lens LensOf(double r1, double r2, double d) {
  const double a1 = std::acos((d * d + r1 * r1 - r2 * r2) / (2 * d * r1));
  const double a2 = std::acos((d * d + r2 * r2 - r1 * r1) / (2 * d * r2));
  return {r1 * r1 * a1 + r2 * r2 * a2 - 0.5 * std::sqrt((-d + r1 + r2) * (d + r1 - r2) * (d - r1 + r2) * (d + r1 + r2))};
}

struct Disc {
  double cx, cy, r;
};
enum class Combine { Union, Intersection, DifferenceFromFirst };

double ScanlineArea(const std::vector<Disc>& d, Combine mode, int n = 400000) {
  double x0 = 1e300, x1 = -1e300;
  for (const Disc& c : d) {
    x0 = std::min(x0, c.cx - c.r);
    x1 = std::max(x1, c.cx + c.r);
  }
  const double h = (x1 - x0) / n;
  double area = 0;
  for (int i = 0; i < n; ++i) {
    const double x = x0 + (i + 0.5) * h;
    std::vector<std::pair<double, double>> iv;
    for (const Disc& c : d) {
      const double t = c.r * c.r - (x - c.cx) * (x - c.cx);
      if (t > 0) iv.emplace_back(c.cy - std::sqrt(t), c.cy + std::sqrt(t));
      else iv.emplace_back(1, -1);  // empty
    }
    double len = 0;
    if (mode == Combine::Union) {
      std::vector<std::pair<double, double>> u;
      for (auto& p : iv) if (p.first < p.second) u.push_back(p);
      std::sort(u.begin(), u.end());
      double lo = 0, hi = 0;
      bool open = false;
      for (auto& p : u) {
        if (!open) { lo = p.first; hi = p.second; open = true; }
        else if (p.first <= hi) hi = std::max(hi, p.second);
        else { len += hi - lo; lo = p.first; hi = p.second; }
      }
      if (open) len += hi - lo;
    } else if (mode == Combine::Intersection) {
      double lo = -1e300, hi = 1e300;
      for (auto& p : iv) { lo = std::max(lo, p.first); hi = std::min(hi, p.second); }
      len = std::max(0.0, hi - lo);
    } else if (iv[0].first < iv[0].second) {
      std::vector<std::pair<double, double>> rest;
      for (size_t k = 1; k < iv.size(); ++k) if (iv[k].first < iv[k].second) rest.push_back(iv[k]);
      std::sort(rest.begin(), rest.end());
      double cur = iv[0].first;
      const double end = iv[0].second;
      for (auto& p : rest) {
        if (p.second <= cur) continue;
        if (p.first >= end) break;
        if (p.first > cur) len += p.first - cur;
        cur = std::max(cur, p.second);
      }
      if (cur < end) len += end - cur;
    }
    area += len * h;
  }
  return area;
}

PlanarCurveRegion DiscRegion(const ON_Plane& pl, const Disc& d) {
  PlanarCurveRegion r;
  r.loops.push_back(Circle(pl, d.cx, d.cy, d.r));
  return r;
}

std::vector<PlanarCurveRegion> DiscRegions(const ON_Plane& pl, const std::vector<Disc>& ds) {
  std::vector<PlanarCurveRegion> out;
  for (const Disc& d : ds) out.push_back(DiscRegion(pl, d));
  return out;
}

PlanarCurveRegion Ring(const ON_Plane& pl, double R, double r, bool hole_ccw = false) {
  PlanarCurveRegion g;
  g.loops.push_back(Circle(pl, 0, 0, R));
  g.loops.push_back(Circle(pl, 0, 0, r, !hole_ccw));
  return g;
}

}  // namespace

D8_EXTRA_TEST(PlanarRegionBooleanNWayUnionOfThreeCirclesWithAHole) {
  const ON_Plane pl = XY();
  // Three discs of radius 2 on the corners of an equilateral triangle of side 3.6: pairwise overlapping, but the centroid is 2.078 from
  // each centre (> 2), so the union encloses a small curved-triangle hole.
  const double s = 3.6;
  const std::vector<Disc> ds = {{0, 0, 2}, {s, 0, 2}, {s / 2, s * std::sqrt(3.0) / 2, 2}};
  const PlanarCurveBooleanResult r = PlanarRegionBooleanN(DiscRegions(pl, ds), pl, CurveRegionOp::Union);
  d8test::Check(r.ok, ("three-circle union accepted: " + r.refusal_reason).c_str());
  const Totals t = Sum(r, pl);
  char buf[160];
  const double want = ScanlineArea(ds, Combine::Union);
  std::snprintf(buf, sizeof(buf), "three-circle union area %.6f vs scanline integral %.6f", t.area, want);
  d8test::Check(Near(t.area, want, 3e-4), buf);
  d8test::Check(t.outer == 1 && t.holes == 1 && t.orientation_matches_flags, "the union has one outer loop and one hole (the gap in the middle)");
}

D8_EXTRA_TEST(PlanarRegionBooleanNWayIntersectionDifferenceAndChain) {
  const ON_Plane pl = XY();
  const double s = 3.0;  // circumradius 1.732 < 2: the three discs share a common region
  const std::vector<Disc> ds = {{0, 0, 2}, {s, 0, 2}, {s / 2, s * std::sqrt(3.0) / 2, 2}};
  char buf[160];
  PlanarCurveBooleanResult r = PlanarRegionBooleanN(DiscRegions(pl, ds), pl, CurveRegionOp::Intersection);
  double want = ScanlineArea(ds, Combine::Intersection);
  d8test::Check(r.ok && r.loops.size() == 1, "three-circle intersection is one loop");
  std::snprintf(buf, sizeof(buf), "three-circle intersection area %.6f vs scanline %.6f", Sum(r, pl).area, want);
  d8test::Check(Near(Sum(r, pl).area, want, 3e-4), buf);
  r = PlanarRegionBooleanN(DiscRegions(pl, ds), pl, CurveRegionOp::Difference);
  want = ScanlineArea(ds, Combine::DifferenceFromFirst);
  d8test::Check(r.ok, ("a - b - c accepted: " + r.refusal_reason).c_str());
  std::snprintf(buf, sizeof(buf), "a - b - c area %.6f vs scanline %.6f", Sum(r, pl).area, want);
  d8test::Check(Near(Sum(r, pl).area, want, 3e-4), buf);
  r = PlanarRegionBooleanN(DiscRegions(pl, ds), pl, CurveRegionOp::Union);
  want = ScanlineArea(ds, Combine::Union);
  std::snprintf(buf, sizeof(buf), "overlapping three-circle union area %.6f vs scanline %.6f", Sum(r, pl).area, want);
  d8test::Check(r.ok && r.loops.size() == 1 && Near(Sum(r, pl).area, want, 3e-4), buf);
  const std::vector<Disc> row = {{0, 0, 2}, {3, 0, 2}, {6, 0, 2}, {9, 0, 2}};
  r = PlanarRegionBooleanN(DiscRegions(pl, row), pl, CurveRegionOp::Union);
  want = ScanlineArea(row, Combine::Union);
  std::snprintf(buf, sizeof(buf), "four-circle chain union area %.6f vs scanline %.6f", Sum(r, pl).area, want);
  d8test::Check(r.ok && r.loops.size() == 1 && Near(Sum(r, pl).area, want, 3e-4), buf);
  r = PlanarRegionBooleanN(DiscRegions(pl, {{0, 0, 1}}), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && !r.refusal_reason.empty(), "a single region is refused");
}

D8_EXTRA_TEST(PlanarRegionBooleanSymmetricDifference) {
  const ON_Plane pl = XY();
  const double r1 = 3, r2 = 2, d = 4;
  const Lens L = LensOf(r1, r2, d);
  char buf[160];
  PlanarCurveBooleanResult x = PlanarCurveBoolean(Circle(pl, 0, 0, r1), Circle(pl, d, 0, r2), pl, CurveRegionOp::SymmetricDifference);
  const Totals t = Sum(x, pl);
  d8test::Check(x.ok && x.loops.size() == 2 && t.outer == 2 && t.holes == 0, "xor of two overlapping discs is two crescents");
  std::snprintf(buf, sizeof(buf), "xor area %.6f vs pi r1^2 + pi r2^2 - 2 lens = %.6f", t.area, kPi * 13 - 2 * L.area);
  d8test::Check(Near(t.area, kPi * 13 - 2 * L.area, 1e-5), buf);
  d8test::Check(Near(t.length, 2 * kPi * (r1 + r2), 1e-4), "xor perimeter = both circumferences (every arc appears once)");
  x = PlanarCurveBoolean(Circle(pl, 0, 0, 3), Circle(pl, 0, 0, 1), pl, CurveRegionOp::SymmetricDifference);
  d8test::Check(x.ok && x.loops.size() == 2 && Sum(x, pl).holes == 1 && Near(Sum(x, pl).area, kPi * 8, 1e-5), "xor of nested discs is the ring pi (R^2 - r^2)");
  x = PlanarCurveBoolean(Circle(pl, 0, 0, 1), Circle(pl, 5, 0, 1), pl, CurveRegionOp::SymmetricDifference);
  d8test::Check(x.ok && x.loops.size() == 2 && Near(Sum(x, pl).area, 2 * kPi, 1e-5), "xor of disjoint discs is both");
  const std::vector<Disc> ds = {{0, 0, 2}, {3, 0, 2}, {1.5, 2.5, 2}};
  const PlanarCurveBooleanResult x3 = PlanarRegionBooleanN(DiscRegions(pl, ds), pl, CurveRegionOp::SymmetricDifference);
  const double ab = ScanlineArea({ds[0], ds[1]}, Combine::Intersection), ac = ScanlineArea({ds[0], ds[2]}, Combine::Intersection),
               bc = ScanlineArea({ds[1], ds[2]}, Combine::Intersection), abc = ScanlineArea(ds, Combine::Intersection);
  const double want = 3 * kPi * 4 - 2 * (ab + ac + bc) + 4 * abc;
  std::snprintf(buf, sizeof(buf), "three-way xor area %.6f vs a+b+c - 2(ab+ac+bc) + 4abc = %.6f", Sum(x3, pl).area, want);
  d8test::Check(x3.ok && Near(Sum(x3, pl).area, want, 6e-4), (std::string(buf) + " [" + x3.refusal_reason + "]").c_str());
}

D8_EXTRA_TEST(PlanarRegionBooleanRegionWithAHoleAgainstACircle) {
  const ON_Plane pl = XY();
  const double R = 4, r = 1, br = 1.5, bx = 2;
  const Lens L = LensOf(r, br, bx);  // hole disc against the circle: the part of the circle that falls in the hole
  const double ring = kPi * (R * R - r * r);
  const double in_ring = kPi * br * br - L.area;  // the part of the circle that lies in the ring itself
  const PlanarCurveRegion B = DiscRegion(pl, {bx, 0, br});
  char buf[160];
  for (bool hole_ccw : {false, true}) {
    const PlanarCurveRegion A = Ring(pl, R, r, hole_ccw);
    PlanarCurveBooleanResult x = PlanarRegionBoolean(A, B, pl, CurveRegionOp::Difference);
    Totals t = Sum(x, pl);
    std::snprintf(buf, sizeof(buf), "ring - circle area %.6f vs %.6f (hole input %s)", t.area, ring - in_ring, hole_ccw ? "ccw" : "cw");
    d8test::Check(x.ok && x.loops.size() == 2 && t.outer == 1 && t.holes == 1 && Near(t.area, ring - in_ring, 1e-5), buf);
    x = PlanarRegionBoolean(A, B, pl, CurveRegionOp::Union);
    t = Sum(x, pl);
    std::snprintf(buf, sizeof(buf), "ring + circle area %.6f vs %.6f", t.area, ring + L.area);
    d8test::Check(x.ok && x.loops.size() == 2 && t.holes == 1 && Near(t.area, ring + L.area, 1e-5), buf);
    x = PlanarRegionBoolean(A, B, pl, CurveRegionOp::Intersection);
    t = Sum(x, pl);
    std::snprintf(buf, sizeof(buf), "ring n circle area %.6f vs %.6f", t.area, in_ring);
    d8test::Check(x.ok && x.loops.size() == 1 && Near(t.area, in_ring, 1e-5), buf);
    x = PlanarRegionBoolean(A, B, pl, CurveRegionOp::SymmetricDifference);
    t = Sum(x, pl);
    d8test::Check(x.ok && Near(t.area, ring + kPi * br * br - 2 * in_ring, 1e-5), "ring xor circle area = ring + circle - 2 (ring n circle)");
  }
  const PlanarCurveRegion A = Ring(pl, R, r);
  const PlanarCurveRegion inner = DiscRegion(pl, {0, 0, 0.4});
  PlanarCurveBooleanResult x = PlanarRegionBoolean(A, inner, pl, CurveRegionOp::Union);
  Totals t = Sum(x, pl);
  d8test::Check(x.ok && x.loops.size() == 3 && t.outer == 2 && t.holes == 1 && Near(t.area, ring + kPi * 0.16, 1e-5), "ring U disc-in-the-hole: ring plus an island");
  x = PlanarRegionBoolean(A, inner, pl, CurveRegionOp::Intersection);
  d8test::Check(x.ok && x.loops.empty(), "ring n disc-in-the-hole is empty");
  x = PlanarRegionBoolean(A, inner, pl, CurveRegionOp::Difference);
  d8test::Check(x.ok && x.loops.size() == 2 && Near(Sum(x, pl).area, ring, 1e-5), "ring - disc-in-the-hole is the ring");
  x = PlanarRegionBoolean(A, DiscRegion(pl, {0, 0, 2}), pl, CurveRegionOp::Union);
  d8test::Check(x.ok && x.loops.size() == 1 && Near(Sum(x, pl).area, kPi * 16, 1e-5), "ring U a disc covering the hole is the full disc (hole closed)");
  // Fold: the ring minus two circles; the second circle (centre -2) is the mirror image of the first about the hole, so its part
  // inside the ring is the same shape with the same area.
  const Lens L2 = LensOf(r, 1.2, 2);
  const double in_ring2 = kPi * 1.44 - L2.area;
  x = PlanarRegionBooleanN({A, B, DiscRegion(pl, {-2, 0, 1.2})}, pl, CurveRegionOp::Difference);
  std::snprintf(buf, sizeof(buf), "ring - circle - circle area %.6f vs %.6f", Sum(x, pl).area, ring - in_ring - in_ring2);
  d8test::Check(x.ok && Near(Sum(x, pl).area, ring - in_ring - in_ring2, 1e-5), buf);
}

D8_EXTRA_TEST(PlanarRegionBooleanRegionLoopsThatCrossAreRefused) {
  const ON_Plane pl = XY();
  PlanarCurveRegion bad;
  bad.loops.push_back(Circle(pl, 0, 0, 3));
  bad.loops.push_back(Circle(pl, 2, 0, 2));  // crosses the first loop: not a region
  const PlanarCurveBooleanResult r = PlanarRegionBoolean(bad, DiscRegion(pl, {10, 0, 1}), pl, CurveRegionOp::Union);
  d8test::Check(!r.ok && r.refusal_reason.find("loops") != std::string::npos, "a region whose own loops cross is refused naming the loops");
}

// Touching contacts (pinch points, tangent contacts) are refused with a TYPED reason, never guessed.
namespace {
ON_PolylineCurve Poly(const std::vector<std::pair<double, double>>& xy) {
  ON_3dPointArray p;
  for (auto& q : xy) p.Append(ON_3dPoint(q.first, q.second, 0));
  p.Append(ON_3dPoint(xy[0].first, xy[0].second, 0));
  return ON_PolylineCurve(p);
}
PlanarCurveRegion One(const ON_Curve& c) {
  PlanarCurveRegion r;
  ON_NurbsCurve n;
  c.GetNurbForm(n);
  r.loops.push_back(n);
  return r;
}
}  // namespace

D8_EXTRA_TEST(PlanarRegionBooleanTouchingContactsAreRefusedWithATypedReason) {
  using dino8::kernel::CurveBooleanRefusal;
  const ON_Plane pl = XY();
  auto kind = [&](const PlanarCurveRegion& a, const PlanarCurveRegion& b, CurveRegionOp op) { return PlanarRegionBoolean(a, b, pl, op).refusal; };
  const PlanarCurveRegion big = DiscRegion(pl, {0, 0, 3});
  d8test::Check(kind(big, DiscRegion(pl, {5, 0, 2}), CurveRegionOp::Union) == CurveBooleanRefusal::TangentContact, "externally tangent circles: TangentContact");
  d8test::Check(kind(big, DiscRegion(pl, {1, 0, 2}), CurveRegionOp::Union) == CurveBooleanRefusal::TangentContact, "internally tangent circles: TangentContact");
  d8test::Check(kind(big, DiscRegion(pl, {5.000005, 0, 2}), CurveRegionOp::Union) == CurveBooleanRefusal::TangentContact, "a gap far below the tolerance: TangentContact");
  // A square touching another only at a corner: one transversal-looking contact, an odd count.
  d8test::Check(kind(One(Poly({{0, 0}, {4, 0}, {4, 4}, {0, 4}})), One(Poly({{4, 4}, {8, 4}, {8, 8}, {4, 8}})), CurveRegionOp::Union) == CurveBooleanRefusal::OddCrossings,
                "squares touching at one corner: OddCrossings");
  // The pinch point: a circle minus its inscribed square - four segments meeting at the corners.
  const double R = std::sqrt(2.0);
  const PlanarCurveRegion circ = DiscRegion(pl, {0, 0, R});
  const PlanarCurveRegion sq = One(Poly({{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}));
  d8test::Check(kind(circ, sq, CurveRegionOp::Difference) == CurveBooleanRefusal::PinchPoint, "circle - inscribed square: PinchPoint");
  d8test::Check(kind(circ, sq, CurveRegionOp::SymmetricDifference) == CurveBooleanRefusal::PinchPoint, "circle xor inscribed square: PinchPoint");
  d8test::Check(PlanarRegionBoolean(circ, sq, pl, CurveRegionOp::Union).refusal == CurveBooleanRefusal::None, "...while the unambiguous union carries no refusal");
  // Closeness, operands and regions.
  d8test::Check(kind(big, One(Poly({{-0.5, 0}, {0.5, 0}, {0, 3.00015}})), CurveRegionOp::Union) == CurveBooleanRefusal::CrossingsTooClose, "crossings 5e-5 apart: CrossingsTooClose");
  d8test::Check(kind(big, One(Poly({{0, 0}, {2, 2}, {2, 0}, {0, 2}})), CurveRegionOp::Union) == CurveBooleanRefusal::SelfIntersectingOperand, "a figure eight: SelfIntersectingOperand");
  PlanarCurveRegion open;
  ON_NurbsCurve arc;
  ON_Arc(ON_Circle(pl, 3), ON_Interval(0, 2.0)).GetNurbForm(arc);
  open.loops.push_back(arc);
  d8test::Check(kind(open, big, CurveRegionOp::Union) == CurveBooleanRefusal::InvalidOperand, "an open curve: InvalidOperand");
  PlanarCurveRegion bad;
  bad.loops.push_back(Circle(pl, 0, 0, 3));
  bad.loops.push_back(Circle(pl, 2, 0, 2));
  d8test::Check(kind(bad, DiscRegion(pl, {10, 0, 1}), CurveRegionOp::Union) == CurveBooleanRefusal::RegionLoopsCross, "crossing loops of one region: RegionLoopsCross");
  d8test::Check(PlanarRegionBooleanN({big}, pl, CurveRegionOp::Union).refusal == CurveBooleanRefusal::InvalidOperand, "one region in an N-way call: InvalidOperand");
  d8test::Check(PlanarRegionBoolean(big, DiscRegion(pl, {4, 0, 2}), pl, CurveRegionOp::Union).refusal == CurveBooleanRefusal::None, "an accepted boolean carries no refusal");
  // A fold reports the kind of the failing step.
  const PlanarCurveBooleanResult f = PlanarRegionBooleanN({big, DiscRegion(pl, {4, 0, 2}), DiscRegion(pl, {8, 0, 2})}, pl, CurveRegionOp::Union);
  d8test::Check(!f.ok && f.refusal == CurveBooleanRefusal::TangentContact && f.refusal_reason.find("step 2") != std::string::npos, "a fold names the failing step and keeps its kind");
}
