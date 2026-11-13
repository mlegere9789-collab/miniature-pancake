// PlanarCurveBoolean: see planar_curve_boolean.h for the construction and the honest scope.
#include "dino8/kernel/planar_curve_boolean.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dino8/kernel/surface_intersect.h"

namespace dino8::kernel {

namespace {

std::string Num(double v) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

// A closed boundary prepared for the boolean: an oriented NURBS copy plus a dense in-plane polygon used ONLY for classification
// (even-odd containment, signed area, rough distance scans) - never for the geometry of the result.
struct Boundary {
  ON_NurbsCurve curve;
  std::vector<ON_2dPoint> poly;  // closed polygon in plane (u, v), last point == first omitted
  std::vector<double> poly_t;    // parameter of each polygon vertex
  double signed_area = 0;
};

ON_2dPoint InPlane(const ON_Plane& pl, const ON_3dPoint& p) {
  const ON_3dVector d = p - pl.origin;
  return ON_2dPoint(ON_DotProduct(d, pl.xaxis), ON_DotProduct(d, pl.yaxis));
}

// Sample every knot span at least `per_span` times so a polyline corner (a span boundary) is a polygon vertex, and at least
// `min_total` points overall.
void SampleBoundary(Boundary& b, const ON_Plane& pl, int per_span, int min_total) {
  const ON_NurbsCurve& c = b.curve;
  const int spans = std::max(1, c.SpanCount());
  std::vector<double> sv(spans + 1);
  c.GetSpanVector(sv.data());
  const int per = std::max(per_span, (min_total + spans - 1) / spans);
  b.poly.clear();
  b.poly_t.clear();
  for (int s = 0; s < spans; ++s) {
    for (int k = 0; k < per; ++k) {
      const double t = sv[s] + (sv[s + 1] - sv[s]) * k / per;
      b.poly_t.push_back(t);
      b.poly.push_back(InPlane(pl, c.PointAt(t)));
    }
  }
  double a2 = 0;
  const size_t n = b.poly.size();
  for (size_t i = 0; i < n; ++i) {
    const ON_2dPoint &p = b.poly[i], &q = b.poly[(i + 1) % n];
    a2 += p.x * q.y - q.x * p.y;
  }
  b.signed_area = 0.5 * a2;
}

// Even-odd containment of `p` in the polygon.
bool Inside(const std::vector<ON_2dPoint>& poly, const ON_2dPoint& p) {
  bool in = false;
  const size_t n = poly.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const ON_2dPoint &a = poly[i], &b = poly[j];
    if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
  }
  return in;
}

double DistToSegment(const ON_2dPoint& p, const ON_2dPoint& a, const ON_2dPoint& b) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double l2 = dx * dx + dy * dy;
  double t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
  t = std::clamp(t, 0.0, 1.0);
  return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

double DistToPolygon(const std::vector<ON_2dPoint>& poly, const ON_2dPoint& p) {
  double best = 1e300;
  const size_t n = poly.size();
  for (size_t i = 0; i < n; ++i) best = std::min(best, DistToSegment(p, poly[i], poly[(i + 1) % n]));
  return best;
}

PlanarCurveBooleanResult Refuse(std::string why) {
  PlanarCurveBooleanResult r;
  r.refusal_reason = std::move(why);
  return r;
}

// Prepare one operand: closed, in plane, simple, counterclockwise. Returns an empty string on success, else the refusal.
std::string Prepare(const ON_Curve& in, const char* name, const ON_Plane& pl, double tolerance, const IntersectOptions& opt, Boundary& out) {
  std::string nm = name;
  if (!in.IsClosed()) return "curve " + nm + " is not closed";
  std::unique_ptr<ON_NurbsCurve> nc(in.NurbsCurve());
  if (!nc || !nc->IsValid()) return "curve " + nm + " has no valid NURBS form";
  out.curve = *nc;
  double worst = 0;
  for (int i = 0; i <= 200; ++i) worst = std::max(worst, std::fabs(pl.DistanceTo(out.curve.PointAt(out.curve.Domain().ParameterAt(i / 200.0)))));
  if (worst > 10 * tolerance) return "curve " + nm + " does not lie in the plane (worst distance " + Num(worst) + ")";
  SampleBoundary(out, pl, 64, 2048);
  if (!IntersectCurveSelfIntersections(out.curve, opt).empty()) return "curve " + nm + " crosses itself (self-intersection)";
  if (out.signed_area == 0) return "curve " + nm + " bounds no area";
  if (out.signed_area < 0) {
    out.curve.Reverse();
    out.signed_area = -out.signed_area;
    SampleBoundary(out, pl, 64, 2048);
  }
  return {};
}

struct Piece {
  ON_NurbsCurve curve;
  ON_3dPoint start, end;
  bool inside_other = false;
  int owner = 0;  // 0 = a, 1 = b
};

// Cut a CCW boundary at the given parameters (in its own domain, at least 2, distinct). Returns the pieces in order, or a reason.
std::string CutAt(const Boundary& bd, std::vector<double> ts, double tolerance, std::vector<ON_NurbsCurve>& out) {
  std::sort(ts.begin(), ts.end());
  NurbsCurve w;
  w.raw() = bd.curve;
  const ON_Interval dom = bd.curve.Domain();
  const double len = dom.Length();
  std::vector<ON_3dPoint> pts;
  for (double t : ts) pts.push_back(bd.curve.PointAt(t));
  // Put the seam at the first cut so no piece wraps.
  if (w.ChangeSeam(ts[0]) == Result::Failed) return "the seam could not be moved to a crossing";
  const ON_Interval nd = w.raw().Domain();
  std::vector<double> s(ts.size());
  for (size_t i = 0; i < ts.size(); ++i) {
    s[i] = nd.Min() + std::fmod(ts[i] - ts[0] + 2 * len, len);
    if (w.raw().PointAt(s[i]).DistanceTo(pts[i]) > 10 * tolerance) {
      // The relocated seam may have shifted the parameterization: fall back to the nearest point of the relocated curve.
      const double t = CurveClosestParamGlobal(w.raw(), pts[i], 512);
      if (w.raw().PointAt(t).DistanceTo(pts[i]) > 10 * tolerance)
        return "a crossing could not be located on the relocated curve";
      s[i] = t;
    }
  }
  s[0] = nd.Min();
  s.push_back(nd.Max());
  for (size_t i = 0; i + 1 < s.size(); ++i) {
    if (!(s[i + 1] > s[i])) return "two crossings collapse to one parameter";
    ON_NurbsCurve piece = w.raw();
    if (!piece.Trim(ON_Interval(s[i], s[i + 1]))) return "a boundary piece could not be trimmed";
    out.push_back(piece);
  }
  return {};
}

}  // namespace

PlanarCurveBooleanResult PlanarCurveBoolean(const ON_Curve& a_in, const ON_Curve& b_in, const ON_Plane& plane, CurveRegionOp op, double tolerance) {
  if (!(tolerance > 0)) return Refuse("tolerance must be positive");
  IntersectOptions opt;
  opt.tolerance = tolerance * 0.1;
  Boundary A, B;
  try {
    if (std::string why = Prepare(a_in, "a", plane, tolerance, opt, A); !why.empty()) return Refuse(why);
    if (std::string why = Prepare(b_in, "b", plane, tolerance, opt, B); !why.empty()) return Refuse(why);
  } catch (const std::exception& e) {
    return Refuse(std::string("preparing the operands failed: ") + e.what());
  }
  ON_BoundingBox bb = A.curve.BoundingBox();
  bb.Union(B.curve.BoundingBox());
  const double scale = bb.Diagonal().Length();

  std::vector<CurveCurveHit> hits;
  try {
    hits = IntersectCurves(A.curve, B.curve, opt);
  } catch (const std::exception& e) {
    return Refuse(std::string("curve intersection failed: ") + e.what());
  }
  // Merge hits that are the same point (a crossing at a seam is reported at both ends of a closed curve's domain).
  std::vector<CurveCurveHit> uniq;
  for (const CurveCurveHit& h : hits) {
    bool dup = false;
    for (const CurveCurveHit& u : uniq)
      if (u.point.DistanceTo(h.point) <= 10 * tolerance) dup = true;
    if (!dup) uniq.push_back(h);
  }
  // Transversality: a tangent contact is not a crossing.
  for (const CurveCurveHit& h : uniq) {
    const ON_3dVector ta = A.curve.TangentAt(h.ta), tb = B.curve.TangentAt(h.tb);
    const double s = ON_CrossProduct(ta, tb).Length();
    if (s < 1e-3) return Refuse("the boundaries touch tangentially at (" + Num(h.point.x) + ", " + Num(h.point.y) + ", " + Num(h.point.z) + ") without crossing");
  }

  for (size_t i = 0; i < uniq.size(); ++i)
    for (size_t j = i + 1; j < uniq.size(); ++j)
      if (uniq[i].point.DistanceTo(uniq[j].point) < 100 * tolerance)
        return Refuse("two crossings are closer than 100 x tolerance (" + Num(uniq[i].point.DistanceTo(uniq[j].point)) + "): a piece that short has no reliable side");
  // Near contact that is not a crossing: sample A against B's polygon; polish each local minimum of the distance profile.
  {
    const size_t n = A.poly.size();
    std::vector<double> d(n);
    for (size_t i = 0; i < n; ++i) d[i] = DistToPolygon(B.poly, A.poly[i]);
    const double coarse = 0.01 * scale;
    for (size_t i = 0; i < n; ++i) {
      if (!(d[i] < coarse && d[i] <= d[(i + n - 1) % n] && d[i] <= d[(i + 1) % n])) continue;
      double ta = A.poly_t[i];
      const ON_3dPoint pa0 = A.curve.PointAt(ta);
      double tb = CurveClosestParamGlobal(B.curve, pa0, 64);
      double dist = 1e300;
      ON_3dPoint pa = pa0, pb = B.curve.PointAt(tb);
      for (int it = 0; it < 60; ++it) {
        tb = CurveClosestParam(B.curve, pa, tb);
        pb = B.curve.PointAt(tb);
        ta = CurveClosestParam(A.curve, pb, ta);
        pa = A.curve.PointAt(ta);
        dist = pa.DistanceTo(pb);
      }
      if (dist > 10 * tolerance) continue;
      bool at_hit = false;
      for (const CurveCurveHit& h : uniq)
        if (h.point.DistanceTo(pa) < 100 * tolerance) at_hit = true;
      if (!at_hit) return Refuse("the boundaries touch (or share a span) near (" + Num(pa.x) + ", " + Num(pa.y) + ", " + Num(pa.z) + ") without a transversal crossing");
    }
  }

  PlanarCurveBooleanResult res;
  res.crossings = static_cast<int>(uniq.size());
  auto push_loop = [&](ON_NurbsCurve c) {
    Boundary tmp;
    tmp.curve = c;
    SampleBoundary(tmp, plane, 64, 2048);
    res.loop_is_hole.push_back(tmp.signed_area < 0);
    res.loops.push_back(std::move(c));
  };

  if (uniq.empty()) {
    const bool a_in_b = Inside(B.poly, A.poly[0]);
    const bool b_in_a = Inside(A.poly, B.poly[0]);
    if (a_in_b && b_in_a) return Refuse("each boundary lies inside the other (inconsistent operands)");
    switch (op) {
      case CurveRegionOp::Union:
        if (a_in_b) push_loop(B.curve);
        else if (b_in_a) push_loop(A.curve);
        else { push_loop(A.curve); push_loop(B.curve); }
        break;
      case CurveRegionOp::Intersection:
        if (a_in_b) push_loop(A.curve);
        else if (b_in_a) push_loop(B.curve);
        break;
      case CurveRegionOp::Difference:
        if (a_in_b) break;
        push_loop(A.curve);
        if (b_in_a) {
          ON_NurbsCurve hole = B.curve;
          hole.Reverse();
          push_loop(hole);
        }
        break;
    }
    res.ok = true;
    return res;
  }

  if (uniq.size() % 2 != 0) return Refuse("an odd number of crossings (" + std::to_string(uniq.size()) + ") for two closed curves: a crossing was missed or a contact is tangential");

  std::vector<double> tsa, tsb;
  for (const CurveCurveHit& h : uniq) {
    tsa.push_back(h.ta);
    tsb.push_back(h.tb);
  }
  std::vector<ON_NurbsCurve> pa, pb;
  try {
    if (std::string why = CutAt(A, tsa, tolerance, pa); !why.empty()) return Refuse("curve a: " + why);
    if (std::string why = CutAt(B, tsb, tolerance, pb); !why.empty()) return Refuse("curve b: " + why);
  } catch (const std::exception& e) {
    return Refuse(std::string("cutting the boundaries failed: ") + e.what());
  }

  std::vector<Piece> pieces;
  auto classify = [&](std::vector<ON_NurbsCurve>& src, int owner, const Boundary& other) -> std::string {
    for (ON_NurbsCurve& c : src) {
      Piece p;
      p.curve = c;
      p.owner = owner;
      p.start = c.PointAtStart();
      p.end = c.PointAtEnd();
      const ON_2dPoint mid = InPlane(plane, c.PointAt(c.Domain().ParameterAt(0.5)));
      const double dist = DistToPolygon(other.poly, mid);
      if (dist < 10 * tolerance) return "a boundary piece lies on the other boundary (shared span)";
      p.inside_other = Inside(other.poly, mid);
      pieces.push_back(std::move(p));
    }
    return {};
  };
  if (std::string why = classify(pa, 0, B); !why.empty()) return Refuse(why);
  if (std::string why = classify(pb, 1, A); !why.empty()) return Refuse(why);

  // Which pieces the operation keeps, and whether each runs backwards.
  std::vector<Piece> kept;
  for (Piece& p : pieces) {
    bool keep = false, reverse = false;
    switch (op) {
      case CurveRegionOp::Union: keep = !p.inside_other; break;
      case CurveRegionOp::Intersection: keep = p.inside_other; break;
      case CurveRegionOp::Difference:
        if (p.owner == 0) keep = !p.inside_other;
        else { keep = p.inside_other; reverse = true; }
        break;
    }
    if (!keep) continue;
    if (reverse) {
      p.curve.Reverse();
      std::swap(p.start, p.end);
    }
    kept.push_back(std::move(p));
  }

  // Chain the kept pieces end to end into closed loops.
  const double link = 10 * tolerance;
  std::vector<bool> used(kept.size(), false);
  for (size_t first = 0; first < kept.size(); ++first) {
    if (used[first]) continue;
    NurbsCurve acc;
    acc.raw() = kept[first].curve;
    used[first] = true;
    ON_3dPoint cur_end = kept[first].end;
    const ON_3dPoint loop_start = kept[first].start;
    int guard = 0;
    while (cur_end.DistanceTo(loop_start) > link) {
      if (++guard > static_cast<int>(kept.size()) + 1) return Refuse("the kept pieces do not chain into a closed loop");
      int best = -1, candidates = 0;
      double bd = link;
      for (size_t j = 0; j < kept.size(); ++j) {
        if (used[j]) continue;
        const double d = kept[j].start.DistanceTo(cur_end);
        if (d <= link) ++candidates;
        if (d <= bd) { bd = d; best = static_cast<int>(j); }
      }
      if (candidates > 1)
        return Refuse("more than one kept piece leaves the junction at (" + Num(cur_end.x) + ", " + Num(cur_end.y) + ", " + Num(cur_end.z) +
                      "): the boundaries touch at a point there, so the result's loops are ambiguous");
      if (best < 0) return Refuse("the kept pieces do not chain into a closed loop (a piece has no successor)");
      used[best] = true;
      NurbsCurve next;
      next.raw() = kept[best].curve;
      try {
        if (acc.Join(next, link) == Result::Failed) return Refuse("joining two boundary pieces failed");
      } catch (const std::exception& e) {
        return Refuse(std::string("joining two boundary pieces failed: ") + e.what());
      }
      cur_end = kept[best].end;
    }
    ON_NurbsCurve loop = acc.raw();
    if (loop.PointAtEnd().DistanceTo(loop.PointAtStart()) > 0) loop.SetEndPoint(loop.PointAtStart());
    push_loop(std::move(loop));
  }
  res.ok = true;
  return res;
}


PlanarCurveRegion RegionOf(const PlanarCurveBooleanResult& r) { return PlanarCurveRegion{r.loops}; }
PlanarCurveBooleanResult PlanarRegionBoolean(const PlanarCurveRegion&, const PlanarCurveRegion&, const ON_Plane&, CurveRegionOp, double) { return Refuse("not implemented"); }
PlanarCurveBooleanResult PlanarRegionBooleanN(const std::vector<PlanarCurveRegion>&, const ON_Plane&, CurveRegionOp, double) { return Refuse("not implemented"); }

}  // namespace dino8::kernel
