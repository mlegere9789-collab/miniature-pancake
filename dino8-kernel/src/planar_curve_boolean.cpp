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

using Kind = CurveBooleanRefusal;

// A helper's refusal: empty means success.
struct Fail {
  Kind kind = Kind::None;
  std::string why;
  Fail() = default;
  Fail(Kind k, std::string w) : kind(k), why(std::move(w)) {}
  bool empty() const { return why.empty(); }
};

PlanarCurveBooleanResult Refuse(Kind kind, std::string why) {
  PlanarCurveBooleanResult r;
  r.refusal = kind;
  r.refusal_reason = std::move(why);
  return r;
}
PlanarCurveBooleanResult Refuse(const Fail& f) { return Refuse(f.kind, f.why); }

// Prepare one operand: closed, in plane, simple, counterclockwise. Returns an empty string on success, else the refusal.
Fail Prepare(const ON_Curve& in, const char* name, const ON_Plane& pl, double tolerance, const IntersectOptions& opt, Boundary& out) {
  std::string nm = name;
  if (!in.IsClosed()) return Fail(Kind::InvalidOperand, "curve " + nm + " is not closed");
  std::unique_ptr<ON_NurbsCurve> nc(in.NurbsCurve());
  if (!nc || !nc->IsValid()) return Fail(Kind::InvalidOperand, "curve " + nm + " has no valid NURBS form");
  out.curve = *nc;
  double worst = 0;
  for (int i = 0; i <= 200; ++i) worst = std::max(worst, std::fabs(pl.DistanceTo(out.curve.PointAt(out.curve.Domain().ParameterAt(i / 200.0)))));
  if (worst > 10 * tolerance) return Fail(Kind::InvalidOperand, "curve " + nm + " does not lie in the plane (worst distance " + Num(worst) + ")");
  SampleBoundary(out, pl, 64, 2048);
  if (!IntersectCurveSelfIntersections(out.curve, opt).empty()) return Fail(Kind::SelfIntersectingOperand, "curve " + nm + " crosses itself (self-intersection)");
  if (out.signed_area == 0) return Fail(Kind::InvalidOperand, "curve " + nm + " bounds no area");
  if (out.signed_area < 0) {
    out.curve.Reverse();
    out.signed_area = -out.signed_area;
    SampleBoundary(out, pl, 64, 2048);
  }
  return {};
}


// Cut a CCW boundary at the given parameters (in its own domain, at least 2, distinct). Returns the pieces in order, or a reason.
Fail CutAt(const Boundary& bd, std::vector<double> ts, double tolerance, std::vector<ON_NurbsCurve>& out) {
  std::sort(ts.begin(), ts.end());
  NurbsCurve w;
  w.raw() = bd.curve;
  const ON_Interval dom = bd.curve.Domain();
  const double len = dom.Length();
  std::vector<ON_3dPoint> pts;
  for (double t : ts) pts.push_back(bd.curve.PointAt(t));
  // Put the seam at the first cut so no piece wraps.
  if (w.ChangeSeam(ts[0]) == Result::Failed) return Fail(Kind::ConstructionFailed, "the seam could not be moved to a crossing");
  const ON_Interval nd = w.raw().Domain();
  std::vector<double> s(ts.size());
  for (size_t i = 0; i < ts.size(); ++i) {
    s[i] = nd.Min() + std::fmod(ts[i] - ts[0] + 2 * len, len);
    if (w.raw().PointAt(s[i]).DistanceTo(pts[i]) > 10 * tolerance) {
      // The relocated seam may have shifted the parameterization: fall back to the nearest point of the relocated curve.
      const double t = CurveClosestParamGlobal(w.raw(), pts[i], 512);
      if (w.raw().PointAt(t).DistanceTo(pts[i]) > 10 * tolerance)
        return Fail(Kind::ConstructionFailed, "a crossing could not be located on the relocated curve");
      s[i] = t;
    }
  }
  s[0] = nd.Min();
  s.push_back(nd.Max());
  for (size_t i = 0; i + 1 < s.size(); ++i) {
    if (!(s[i + 1] > s[i])) return Fail(Kind::ConstructionFailed, "two crossings collapse to one parameter");
    ON_NurbsCurve piece = w.raw();
    if (!piece.Trim(ON_Interval(s[i], s[i + 1]))) return Fail(Kind::ConstructionFailed, "a boundary piece could not be trimmed");
    out.push_back(piece);
  }
  return {};
}

// Even-odd membership of a point in a region (a set of loops: outer boundaries and holes).
bool InRegion(const std::vector<Boundary>& r, const ON_2dPoint& p) {
  int count = 0;
  for (const Boundary& b : r)
    if (Inside(b.poly, p)) ++count;
  return count % 2 == 1;
}

double DistToRegion(const std::vector<Boundary>& r, const ON_2dPoint& p) {
  double best = 1e300;
  for (const Boundary& b : r) best = std::min(best, DistToPolygon(b.poly, p));
  return best;
}

// Prepare every loop of a region, then fix the orientation by nesting depth: a loop inside an even number of the other loops is an outer
// boundary (counterclockwise), inside an odd number a hole (clockwise). The loops of one region must not cross or touch each other.
Fail PrepareRegion(const std::vector<ON_NurbsCurve>& loops, const char* name, const ON_Plane& pl, double tolerance, const IntersectOptions& opt,
                          std::vector<Boundary>& out) {
  out.clear();
  out.resize(loops.size());
  for (size_t k = 0; k < loops.size(); ++k) {
    const std::string label = loops.size() == 1 ? std::string(name) : std::string(name) + " loop " + std::to_string(k);
    if (Fail f = Prepare(loops[k], label.c_str(), pl, tolerance, opt, out[k]); !f.empty()) return f;
  }
  // Loops of one region may touch each other at points (the two crescents of a symmetric difference do) but must not cross or nest into
  // each other while touching: a loop that has a hit with another must have no sample strictly inside that other.
  for (size_t i = 0; i < out.size(); ++i) {
    for (size_t j = i + 1; j < out.size(); ++j) {
      if (IntersectCurves(out[i].curve, out[j].curve, opt).empty()) continue;
      const double clear = 100 * tolerance;
      for (int dir = 0; dir < 2; ++dir) {
        const Boundary& x = dir == 0 ? out[i] : out[j];
        const Boundary& y = dir == 0 ? out[j] : out[i];
        for (const ON_2dPoint& q : x.poly)
          if (DistToPolygon(y.poly, q) > clear && Inside(y.poly, q))
            return Fail(Kind::RegionLoopsCross, "two loops of region " + std::string(name) + " cross or overlap each other (a region's loops may touch at points but not cross)");
      }
    }
  }
  for (size_t i = 0; i < out.size(); ++i) {
    // Depth from a sample well clear of every other loop (a loop may touch another at its start point).
    int depth = 0;
    for (size_t j = 0; j < out.size(); ++j) {
      if (j == i) continue;
      const size_t n = out[i].poly.size();
      size_t best = 0;
      double bd = -1;
      for (size_t k = 1; k <= 7; ++k) {
        const size_t idx = k * n / 8;
        const double d = DistToPolygon(out[j].poly, out[i].poly[idx]);
        if (d > bd) { bd = d; best = idx; }
      }
      if (Inside(out[j].poly, out[i].poly[best])) ++depth;
    }
    if (depth % 2 == 1) {
      out[i].curve.Reverse();
      SampleBoundary(out[i], pl, 64, 2048);
    }
  }
  return {};
}

struct Piece {
  ON_NurbsCurve curve;
  ON_3dPoint start, end;
  bool inside_other = false;
  bool whole_loop = false;  // an uncut loop (no crossings): kept or dropped as a whole
  int owner = 0;            // 0 = a, 1 = b
};

// a OP b for Union / Intersection / Difference on prepared regions.
PlanarCurveBooleanResult Core(const std::vector<Boundary>& A, const std::vector<Boundary>& B, const ON_Plane& plane, CurveRegionOp op, double tolerance,
                              const IntersectOptions& opt) {
  PlanarCurveBooleanResult res;
  ON_BoundingBox bb;
  for (const Boundary& b : A) bb.Union(b.curve.BoundingBox());
  for (const Boundary& b : B) bb.Union(b.curve.BoundingBox());
  const double scale = bb.IsValid() ? bb.Diagonal().Length() : 1.0;

  std::vector<std::vector<double>> tsa(A.size()), tsb(B.size());
  std::vector<CurveCurveHit> all;
  for (size_t ia = 0; ia < A.size(); ++ia) {
    for (size_t ib = 0; ib < B.size(); ++ib) {
      std::vector<CurveCurveHit> hits;
      try {
        hits = IntersectCurves(A[ia].curve, B[ib].curve, opt);
      } catch (const std::exception& e) {
        return Refuse(Kind::ConstructionFailed, std::string("curve intersection failed: ") + e.what());
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
        const ON_3dVector ta = A[ia].curve.TangentAt(h.ta), tb = B[ib].curve.TangentAt(h.tb);
        const double s = ON_CrossProduct(ta, tb).Length();
        if (s < 1e-3) return Refuse(Kind::TangentContact, "the boundaries touch tangentially at (" + Num(h.point.x) + ", " + Num(h.point.y) + ", " + Num(h.point.z) + ") without crossing");
      }
      for (size_t i = 0; i < uniq.size(); ++i)
        for (size_t j = i + 1; j < uniq.size(); ++j)
          if (uniq[i].point.DistanceTo(uniq[j].point) < 100 * tolerance)
            return Refuse(Kind::CrossingsTooClose, "two crossings are closer than 100 x tolerance (" + Num(uniq[i].point.DistanceTo(uniq[j].point)) + "): a piece that short has no reliable side");
      // Near contact that is not a crossing: sample A against B's polygon; polish each local minimum of the distance profile.
      {
        const size_t n = A[ia].poly.size();
        std::vector<double> d(n);
        for (size_t i = 0; i < n; ++i) d[i] = DistToPolygon(B[ib].poly, A[ia].poly[i]);
        const double coarse = 0.01 * scale;
        for (size_t i = 0; i < n; ++i) {
          if (!(d[i] < coarse && d[i] <= d[(i + n - 1) % n] && d[i] <= d[(i + 1) % n])) continue;
          double ta = A[ia].poly_t[i];
          const ON_3dPoint pa0 = A[ia].curve.PointAt(ta);
          double tb = CurveClosestParamGlobal(B[ib].curve, pa0, 64);
          double dist = 1e300;
          ON_3dPoint pa = pa0, pb = B[ib].curve.PointAt(tb);
          for (int it = 0; it < 60; ++it) {
            tb = CurveClosestParam(B[ib].curve, pa, tb);
            pb = B[ib].curve.PointAt(tb);
            ta = CurveClosestParam(A[ia].curve, pb, ta);
            pa = A[ia].curve.PointAt(ta);
            dist = pa.DistanceTo(pb);
          }
          if (dist > 10 * tolerance) continue;
          bool at_hit = false;
          for (const CurveCurveHit& h : uniq)
            if (h.point.DistanceTo(pa) < 100 * tolerance) at_hit = true;
          if (!at_hit) return Refuse(Kind::TangentContact, "the boundaries touch (or share a span) near (" + Num(pa.x) + ", " + Num(pa.y) + ", " + Num(pa.z) + ") without a transversal crossing");
        }
      }
      if (uniq.size() % 2 != 0) return Refuse(Kind::OddCrossings, "an odd number of crossings (" + std::to_string(uniq.size()) + ") for two closed curves: a crossing was missed or a contact is tangential");
      for (const CurveCurveHit& h : uniq) {
        tsa[ia].push_back(h.ta);
        tsb[ib].push_back(h.tb);
        all.push_back(h);
      }
    }
  }
  for (size_t i = 0; i < all.size(); ++i)
    for (size_t j = i + 1; j < all.size(); ++j)
      if (all[i].point.DistanceTo(all[j].point) < 100 * tolerance)
        return Refuse(Kind::CrossingsTooClose, "two crossings are closer than 100 x tolerance (" + Num(all[i].point.DistanceTo(all[j].point)) + "): a piece that short has no reliable side");
  res.crossings = static_cast<int>(all.size());

  auto push_loop = [&](ON_NurbsCurve c) {
    Boundary tmp;
    tmp.curve = c;
    SampleBoundary(tmp, plane, 64, 2048);
    res.loop_is_hole.push_back(tmp.signed_area < 0);
    res.loops.push_back(std::move(c));
  };

  std::vector<Piece> pieces;
  auto classify = [&](const std::vector<Boundary>& src, const std::vector<std::vector<double>>& ts, int owner, const std::vector<Boundary>& other) -> Fail {
    for (size_t k = 0; k < src.size(); ++k) {
      std::vector<ON_NurbsCurve> cut;
      const bool whole = ts[k].empty();
      if (whole) {
        cut.push_back(src[k].curve);
      } else if (Fail f = CutAt(src[k], ts[k], tolerance, cut); !f.empty()) {
        return Fail(f.kind, std::string(owner == 0 ? "region a: " : "region b: ") + f.why);
      }
      for (ON_NurbsCurve& c : cut) {
        Piece p;
        p.curve = c;
        p.owner = owner;
        p.whole_loop = whole;
        p.start = c.PointAtStart();
        p.end = c.PointAtEnd();
        const ON_2dPoint mid = InPlane(plane, whole ? src[k].curve.PointAt(src[k].poly_t[0]) : c.PointAt(c.Domain().ParameterAt(0.5)));
        if (DistToRegion(other, mid) < 10 * tolerance) return Fail(Kind::SharedSpan, "a boundary piece lies on the other boundary (shared span)");
        p.inside_other = InRegion(other, mid);
        pieces.push_back(std::move(p));
      }
    }
    return {};
  };
  if (Fail f = classify(A, tsa, 0, B); !f.empty()) return Refuse(f);
  if (Fail f = classify(B, tsb, 1, A); !f.empty()) return Refuse(f);

  // Which pieces the operation keeps, and whether each runs backwards.
  std::vector<Piece> kept;
  for (Piece& p : pieces) {
    bool keep = false, reverse = false;
    switch (op) {
      case CurveRegionOp::Union: keep = !p.inside_other; break;
      case CurveRegionOp::Intersection: keep = p.inside_other; break;
      default:  // Difference (symmetric difference is split into two Differences by the caller)
        if (p.owner == 0) keep = !p.inside_other;
        else { keep = p.inside_other; reverse = true; }
        break;
    }
    if (!keep) continue;
    if (reverse) {
      p.curve.Reverse();
      std::swap(p.start, p.end);
    }
    if (p.whole_loop) push_loop(p.curve);
    else kept.push_back(std::move(p));
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
      if (++guard > static_cast<int>(kept.size()) + 1) return Refuse(Kind::ConstructionFailed, "the kept pieces do not chain into a closed loop");
      int best = -1, candidates = 0;
      double bd = link;
      for (size_t j = 0; j < kept.size(); ++j) {
        if (used[j]) continue;
        const double d = kept[j].start.DistanceTo(cur_end);
        if (d <= link) ++candidates;
        if (d <= bd) { bd = d; best = static_cast<int>(j); }
      }
      if (candidates > 1)
        return Refuse(Kind::PinchPoint, "more than one kept piece leaves the junction at (" + Num(cur_end.x) + ", " + Num(cur_end.y) + ", " + Num(cur_end.z) +
                      "): the boundaries touch at a point there, so the result's loops are ambiguous");
      if (best < 0) return Refuse(Kind::ConstructionFailed, "the kept pieces do not chain into a closed loop (a piece has no successor)");
      used[best] = true;
      NurbsCurve next;
      next.raw() = kept[best].curve;
      try {
        if (acc.Join(next, link) == Result::Failed) return Refuse(Kind::ConstructionFailed, "joining two boundary pieces failed");
      } catch (const std::exception& e) {
        return Refuse(Kind::ConstructionFailed, std::string("joining two boundary pieces failed: ") + e.what());
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

IntersectOptions OptionsFor(double tolerance) {
  IntersectOptions opt;
  opt.tolerance = tolerance * 0.1;
  return opt;
}

}  // namespace

PlanarCurveBooleanResult PlanarRegionBoolean(const PlanarCurveRegion& a, const PlanarCurveRegion& b, const ON_Plane& plane, CurveRegionOp op, double tolerance) {
  if (!(tolerance > 0)) return Refuse(Kind::InvalidOperand, "tolerance must be positive");
  const IntersectOptions opt = OptionsFor(tolerance);
  std::vector<Boundary> A, B;
  try {
    if (Fail f = PrepareRegion(a.loops, "a", plane, tolerance, opt, A); !f.empty()) return Refuse(f);
    if (Fail f = PrepareRegion(b.loops, "b", plane, tolerance, opt, B); !f.empty()) return Refuse(f);
    if (op != CurveRegionOp::SymmetricDifference) return Core(A, B, plane, op, tolerance, opt);
    PlanarCurveBooleanResult amb = Core(A, B, plane, CurveRegionOp::Difference, tolerance, opt);
    if (!amb.ok) return amb;
    const PlanarCurveBooleanResult bma = Core(B, A, plane, CurveRegionOp::Difference, tolerance, opt);
    if (!bma.ok) return bma;
    for (size_t i = 0; i < bma.loops.size(); ++i) {
      amb.loops.push_back(bma.loops[i]);
      amb.loop_is_hole.push_back(bma.loop_is_hole[i]);
    }
    return amb;
  } catch (const std::exception& e) {
    return Refuse(Kind::ConstructionFailed, std::string("the region boolean failed: ") + e.what());
  }
}

PlanarCurveBooleanResult PlanarRegionBooleanN(const std::vector<PlanarCurveRegion>& regions, const ON_Plane& plane, CurveRegionOp op, double tolerance) {
  if (regions.size() < 2) return Refuse(Kind::InvalidOperand, "an N-way boolean needs at least two regions");
  PlanarCurveRegion acc = regions[0];
  PlanarCurveBooleanResult last;
  int crossings = 0;
  for (size_t i = 1; i < regions.size(); ++i) {
    last = PlanarRegionBoolean(acc, regions[i], plane, op, tolerance);
    if (!last.ok) {
      last.refusal_reason = "step " + std::to_string(i) + " of the fold: " + last.refusal_reason;
      return last;
    }
    crossings += last.crossings;
    acc = RegionOf(last);
  }
  last.crossings = crossings;
  return last;
}

PlanarCurveBooleanResult PlanarCurveBoolean(const ON_Curve& a_in, const ON_Curve& b_in, const ON_Plane& plane, CurveRegionOp op, double tolerance) {
  PlanarCurveRegion a, b;
  for (auto [in, region] : {std::pair<const ON_Curve*, PlanarCurveRegion*>{&a_in, &a}, {&b_in, &b}}) {
    std::unique_ptr<ON_NurbsCurve> nc(in->NurbsCurve());
    if (!nc || !nc->IsValid()) return Refuse(Kind::InvalidOperand, std::string("curve ") + (region == &a ? "a" : "b") + " has no valid NURBS form");
    region->loops.push_back(*nc);
  }
  return PlanarRegionBoolean(a, b, plane, op, tolerance);
}

PlanarCurveRegion RegionOf(const PlanarCurveBooleanResult& r) { return PlanarCurveRegion{r.loops}; }

}  // namespace dino8::kernel
