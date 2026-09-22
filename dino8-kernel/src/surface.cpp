#include "dino8/kernel/surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/detail/polygon2d.h"
#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

namespace {

// The (u, v)-space precision floor this whole file already treats two
// points as "the same" at (TessellateGridClippedExact's own
// kDuplicatePointEpsilon, which reuses this constant below) - also used by
// ClipConvex's own robust boundary test, see there.
constexpr double kUvCoincidenceEpsilon = 1e-9;

double Cross2d(const Point2d& a, const Point2d& b) { return a.x * b.y - a.y * b.x; }
double Dot2d(const Point2d& a, const Point2d& b) { return a.x * b.x + a.y * b.y; }

double SignedArea(const std::vector<Point2d>& polygon) {
  double area = 0.0;
  const size_t n = polygon.size();
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a = polygon[i];
    const Point2d& b = polygon[(i + 1) % n];
    area += Cross2d(a, b);
  }
  return 0.5 * area;
}

// Standard even-odd ray-casting point-in-polygon test. Boundary behavior
// is not guaranteed either way (ordinary floating-point ray-casting
// caveat) - callers relying on an exact result should keep test points
// off the polygon boundary.
bool PointInPolygon(double u, double v, const std::vector<Point2d>& polygon) {
  bool inside = false;
  const size_t n = polygon.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const Point2d& pi = polygon[i];
    const Point2d& pj = polygon[j];
    const bool crosses = (pi.y > v) != (pj.y > v);
    if (crosses) {
      const double u_at_crossing = (pj.x - pi.x) * (v - pi.y) / (pj.y - pi.y) + pi.x;
      if (u < u_at_crossing) {
        inside = !inside;
      }
    }
  }
  return inside;
}

bool IsConvexPolygon(const std::vector<Point2d>& polygon) {
  const size_t n = polygon.size();
  if (n < 3) {
    return false;
  }
  double sign = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a = polygon[i];
    const Point2d& b = polygon[(i + 1) % n];
    const Point2d& c = polygon[(i + 2) % n];
    const Point2d edge1(b.x - a.x, b.y - a.y);
    const Point2d edge2(c.x - b.x, c.y - b.y);
    const double turn = Cross2d(edge1, edge2);
    if (std::abs(turn) < 1e-12) {
      // Cross2d near zero means edge1/edge2 are PARALLEL, but that covers
      // two very different cases: a genuinely straight run (edge2 points
      // the SAME way as edge1 - harmless, doesn't affect convexity either
      // way) and an exact (or near-exact) 180-degree reversal spike -
      // e.g. a zero/near-zero-width "there and back" notch, such as a
      // keyhole bridge's own in/out crossing (see boolean_general.cpp's
      // BridgeHolesIntoOuter, which deliberately keeps its bridge
      // NON-collinear for exactly this reason) - which is a genuine,
      // sharp concavity that a cross-product-only test cannot see (the
      // turn angle is +-pi either way, indistinguishable from 0 by cross
      // product alone). Checking the dot product too disambiguates them:
      // a real reversal has edge1 and edge2 pointing opposite ways
      // (negative dot), which no genuinely convex polygon can ever
      // contain (a convex polygon's edges only ever turn one way, never
      // fold back on themselves).
      if (Dot2d(edge1, edge2) < 0.0) {
        return false;
      }
      continue;  // genuinely collinear (same direction), doesn't affect convexity either way
    }
    const double turn_sign = turn > 0 ? 1.0 : -1.0;
    if (sign == 0.0) {
      sign = turn_sign;
    } else if (turn_sign != sign) {
      return false;
    }
  }
  return true;
}

Point2d LineIntersection(const Point2d& p1, const Point2d& p2, const Point2d& a,
                          const Point2d& b) {
  const Point2d ab(b.x - a.x, b.y - a.y);
  const double d1 = Cross2d(ab, Point2d(p1.x - a.x, p1.y - a.y));
  const double d2 = Cross2d(ab, Point2d(p2.x - a.x, p2.y - a.y));
  const double t = d1 / (d1 - d2);
  return Point2d(p1.x + t * (p2.x - p1.x), p1.y + t * (p2.y - p1.y));
}

// Sutherland-Hodgman: clips `subject` against the convex polygon `clip`.
// Used only for a convex trim_polygon (TessellateGridClippedExact's fast,
// long-proven path) - ClipPolygon below is the general fallback for a
// concave trim. `orientation_sign` is +1 if `clip`'s vertices are CCW
// (positive signed area), -1 if CW - lets the "inside" test work
// regardless of `clip`'s winding direction.
std::vector<Point2d> ClipConvex(std::vector<Point2d> subject,
                                 const std::vector<Point2d>& clip,
                                 double orientation_sign) {
  const size_t clip_n = clip.size();
  for (size_t i = 0; i < clip_n && !subject.empty(); ++i) {
    const Point2d& a = clip[i];
    const Point2d& b = clip[(i + 1) % clip_n];
    const std::vector<Point2d> input = std::move(subject);
    subject.clear();
    const size_t n = input.size();
    for (size_t j = 0; j < n; ++j) {
      const Point2d& curr = input[j];
      const Point2d& prev = input[(j + n - 1) % n];
      const double curr_side = Cross2d(Point2d(b.x - a.x, b.y - a.y),
                                        Point2d(curr.x - a.x, curr.y - a.y)) *
                                orientation_sign;
      const double prev_side = Cross2d(Point2d(b.x - a.x, b.y - a.y),
                                        Point2d(prev.x - a.x, prev.y - a.y)) *
                                orientation_sign;
      const bool curr_inside = curr_side >= 0.0;
      const bool prev_inside = prev_side >= 0.0;
      if (curr_inside) {
        if (!prev_inside) {
          subject.push_back(LineIntersection(prev, curr, a, b));
        }
        subject.push_back(curr);
      } else if (prev_inside) {
        subject.push_back(LineIntersection(prev, curr, a, b));
      }
    }
  }
  return subject;
}

// Collapses a run of CONSECUTIVE vertices that are collinear (within
// `eps`, a perpendicular distance in the polygon's own (u, v) units) with
// their immediate original neighbors down to just that run's own two
// endpoints. A single left-to-right pass suffices (not an iterative
// Douglas-Peucker): for a genuinely straight run, EVERY interior point's
// immediate original prev/next already form the same line, so every
// interior point is flagged for removal in one pass, regardless of run
// length. Purely a redundant-point cleanup - it does not change the
// polygon's shape by more than `eps` - see ClipConvex's own caller
// (TessellateGridClippedExact) for why removing these specific
// redundant points, rather than loosening ClipConvex's own inside test,
// is the safe fix.
//
// One point SimplifyCollinearRuns() dropped, plus the two SURVIVING
// (simplified-polygon) vertices its own now-collapsed run sat between -
// not just its bare position. See InsertForcedPointsIntoTriangulation's
// own doc comment for why the edge identity matters just as much as the
// point itself: an axis-aligned cut (the common box+box case) is exactly
// as straight, in (u, v), as this SAME cell's own plain grid-line
// boundary, so a forced point's bare position alone cannot tell "this
// cut's own boundary edge" apart from "an ordinary cell edge that merely
// happens to run the same direction" - only knowing which specific trim
// edge (by its own two endpoints) it came from can.
struct RemovedTrimPoint {
  Point2d p;
  Point2d edge_t0, edge_t1;
};

// `removed_out`, if non-null, collects every dropped `curr` in its
// original polygon order - not thrown away, unlike the original version
// of this function: a run collapsed here because it's straight in (u, v)
// (the common case: a curved face cut by a planar face) is NOT
// necessarily redundant NOISE - BuildLoop() (boolean_general.cpp) gives
// each of these points its own real ON_BrepVertex, genuinely spread out
// along the line (real angular/positional spacing, not sub-ULP jitter;
// only the line's OWN perpendicular direction is noisy, which is exactly
// what makes them look "collinear" here) - so TessellateGridClippedExact
// re-injects them as forced boundary points once the CLIPPING decision
// itself (which is what this simplification protects, per this
// function's own doc comment above) is safely made from the clean
// 2-endpoint line instead. See TessellateGridClippedExact's own updated
// doc comment for the full resolution-mismatch writeup this closes.
std::vector<Point2d> SimplifyCollinearRuns(const std::vector<Point2d>& polygon, double eps,
                                            std::vector<RemovedTrimPoint>* removed_out = nullptr) {
  const size_t n = polygon.size();
  if (n < 4) return polygon;  // nothing to collapse without losing the polygon itself
  std::vector<bool> keep(n, true);
  size_t kept = n;
  for (size_t i = 0; i < n; ++i) {
    const Point2d& prev = polygon[(i + n - 1) % n];
    const Point2d& curr = polygon[i];
    const Point2d& next = polygon[(i + 1) % n];
    const double ex = next.x - prev.x, ey = next.y - prev.y;
    const double len = std::sqrt(ex * ex + ey * ey);
    if (len <= 1e-300) continue;  // prev == next (degenerate spike) - never drop `curr` here
    const double dist = std::abs(ex * (curr.y - prev.y) - ey * (curr.x - prev.x)) / len;
    if (dist < eps && kept > 3) {
      keep[i] = false;
      --kept;
    }
  }
  std::vector<Point2d> out;
  out.reserve(kept);
  for (size_t i = 0; i < n; ++i) {
    if (keep[i]) out.push_back(polygon[i]);
  }
  if (out.size() < 3) {
    if (removed_out != nullptr) removed_out->clear();
    return polygon;
  }
  if (removed_out != nullptr) {
    // `out_idx` tracks, in ORIGINAL polygon order, the index (into `out`)
    // of the most recently passed KEPT point - seeded to `out`'s own last
    // element so a removed run starting right at polygon[0] (wrapping
    // across the array boundary) still resolves to the correct pair of
    // surviving endpoints.
    size_t out_idx = out.size() - 1;
    for (size_t i = 0; i < n; ++i) {
      if (keep[i]) {
        out_idx = (out_idx + 1) % out.size();
      } else {
        RemovedTrimPoint rp;
        rp.p = polygon[i];
        rp.edge_t0 = out[out_idx];
        rp.edge_t1 = out[(out_idx + 1) % out.size()];
        removed_out->push_back(rp);
      }
    }
  }
  return out;
}

// Inserts every point of `forced` that lies strictly on one of `pts`'s
// own first `n0` points' boundary edges (i, (i+1) % n0 - `pts`'s own
// ORIGINAL cyclic order, a single grid cell's own already-triangulated
// clipped boundary) into `tris` (a valid triangulation of those `n0`
// points, as EarClipTriangulate already produced), by replacing the ONE
// triangle that owns that boundary edge with a fan through its own apex
// (the triangle's third vertex) and the edge's own new points in sorted
// order - mirroring ReconcileEdgeTopology's own already-proven
// ReconcileChainToChord technique (boolean_general.cpp) at this earlier,
// per-cell stage instead of after full mesh assembly. New points are
// APPENDED to `pts` (never reordering or erasing its first `n0` entries),
// so every original triangle's OWN vertex indices - and every OTHER
// boundary edge's own owning-triangle search, run afterward in the same
// pass - stay valid throughout.
//
// Deliberately does NOT hand the grown point set to EarClipTriangulate:
// this cell's own baseline `n0` (whatever a bare grid-cell clip already
// produces, always small) is all that function ever sees, however many
// forced points this cell's own bucket carries - each forced point costs
// only an O(n0)-triangle scan (to find its edge's owning triangle) plus
// O(1) to fan it in. An earlier version of this fix instead re-ran
// EarClipTriangulate on the ALREADY-forced-point-augmented polygon;
// confirmed directly to roughly DOUBLE the 76-case sweep's own
// wall-clock time (63s -> 125s) purely from EarClipTriangulate's own
// worst-case cost scaling with however large a single cell's own forced
// bucket happened to be - this version measurably restores the
// pre-forced-insertion runtime (see this method's own doc comment for
// the exact numbers).
void InsertForcedPointsIntoTriangulation(std::vector<Point2d>& pts, std::vector<std::array<int, 3>>& tris,
                                          size_t n0, const std::vector<RemovedTrimPoint>& forced, double eps) {
  if (forced.empty() || n0 < 3) return;
  for (size_t i = 0; i < n0; ++i) {
    const size_t a_idx = i, b_idx = (i + 1) % n0;
    const Point2d a = pts[a_idx];
    const Point2d b = pts[b_idx];
    const double ex = b.x - a.x, ey = b.y - a.y;
    const double len2 = ex * ex + ey * ey;
    if (len2 <= 1e-300) continue;  // a == b (already-degenerate edge) - nothing to insert onto
    const double len = std::sqrt(len2);

    std::vector<std::pair<double, Point2d>> hits;
    for (const RemovedTrimPoint& rp : forced) {
      // This cell edge (a, b) must itself sit on `rp`'s own ORIGINATING
      // trim edge's line - not merely run the same direction as it. An
      // axis-aligned cut (the common box+box case) can be dead straight
      // along the SAME direction as an ordinary cell grid-line edge; only
      // checking "is `rp.p` collinear with a-b" (as an earlier version of
      // this function did) cannot tell that genuine coincidence apart
      // from an unrelated cell edge that merely happens to run parallel -
      // confirmed directly: without this check, a forced point could get
      // fanned into a plain interior grid-line edge shared with an
      // untouched neighboring cell, leaving that neighbor without the
      // matching point and opening a small crack (regressed box+box
      // Union/Difference's own previously-closing
      // TessellateGeneralBooleanClosedMesh() result - caught by this
      // repo's own ctest suite, not the sweep).
      const double tex = rp.edge_t1.x - rp.edge_t0.x, tey = rp.edge_t1.y - rp.edge_t0.y;
      const double tlen2 = tex * tex + tey * tey;
      if (tlen2 <= 1e-300) continue;
      const double tlen = std::sqrt(tlen2);
      const double perp_a = std::abs((a.x - rp.edge_t0.x) * tey - (a.y - rp.edge_t0.y) * tex) / tlen;
      if (perp_a > eps) continue;
      const double perp_b = std::abs((b.x - rp.edge_t0.x) * tey - (b.y - rp.edge_t0.y) * tex) / tlen;
      if (perp_b > eps) continue;

      const double fx = rp.p.x - a.x, fy = rp.p.y - a.y;
      const double t = (fx * ex + fy * ey) / len2;
      if (t <= eps || t >= 1.0 - eps) continue;  // not strictly interior - leave to a/b themselves
      const double perp = std::abs(fx * ey - fy * ex) / len;
      if (perp > eps) continue;  // not on this edge's own line
      hits.emplace_back(t, rp.p);
    }
    if (hits.empty()) continue;
    std::sort(hits.begin(), hits.end(), [](const auto& x, const auto& y) { return x.first < y.first; });

    std::vector<Point2d> mids;
    mids.reserve(hits.size());
    for (const auto& [t, p] : hits) {
      (void)t;
      const Point2d& last = mids.empty() ? a : mids.back();
      if (std::abs(p.x - last.x) <= eps && std::abs(p.y - last.y) <= eps) continue;  // near-dup
      mids.push_back(p);
    }
    if (!mids.empty() && std::abs(mids.back().x - b.x) <= eps && std::abs(mids.back().y - b.y) <= eps) {
      mids.pop_back();  // last hit coincides with `b` itself - nothing to add there
    }
    if (mids.empty()) continue;

    // Find the (exactly one, for a genuine polygon boundary edge) triangle
    // whose own directed edge is a_idx -> b_idx.
    int owner = -1;
    for (size_t ti = 0; ti < tris.size() && owner < 0; ++ti) {
      const std::array<int, 3>& t = tris[ti];
      for (int k = 0; k < 3; ++k) {
        if (t[static_cast<size_t>(k)] == static_cast<int>(a_idx) &&
            t[static_cast<size_t>((k + 1) % 3)] == static_cast<int>(b_idx)) {
          owner = static_cast<int>(ti);
          break;
        }
      }
    }
    if (owner < 0) continue;  // defensive - shouldn't happen for a genuine boundary edge

    const std::array<int, 3> owner_tri = tris[static_cast<size_t>(owner)];
    int apex = -1;
    for (int k = 0; k < 3; ++k) {
      if (owner_tri[static_cast<size_t>(k)] != static_cast<int>(a_idx) &&
          owner_tri[static_cast<size_t>(k)] != static_cast<int>(b_idx)) {
        apex = owner_tri[static_cast<size_t>(k)];
        break;
      }
    }
    if (apex < 0) continue;

    std::vector<int> chain_idx;
    chain_idx.reserve(mids.size() + 2);
    chain_idx.push_back(static_cast<int>(a_idx));
    for (const Point2d& p : mids) {
      chain_idx.push_back(static_cast<int>(pts.size()));
      pts.push_back(p);
    }
    chain_idx.push_back(static_cast<int>(b_idx));

    tris.erase(tris.begin() + owner);
    for (size_t k = 0; k + 1 < chain_idx.size(); ++k) {
      tris.push_back({apex, chain_idx[k], chain_idx[k + 1]});
    }
  }
}

// Greiner-Hormann polygon intersection: clips `subject` against `clip`,
// where both are simple (non-self-intersecting) polygons and either may
// be concave. This is TessellateGridClippedExact's fallback for a concave
// trim_polygon - ClipConvex above stays the path for a convex one, both
// because it's simpler and because it's the long-proven implementation
// Mesh::Cylinder()'s circular trim (and everything else that exercises
// exact clipping so far) already depends on; concave clipping is a newer,
// narrower-tested addition and doesn't need to displace it. Unlike
// Sutherland-Hodgman, this doesn't require the clip region to be convex -
// it works by inserting every boundary-crossing point into both polygons'
// vertex lists, tagging each subject-side crossing "entry" (the subject
// path moves from outside `clip` to inside there) or "exit", then tracing
// the shared boundary starting from each unvisited *entry*, always moving
// forward (never backward - both polygons are normalized to the same CCW
// winding below, which is what makes "always forward" valid for plain
// intersection, unlike the fuller Greiner-Hormann/Foster algorithm's
// forward/backward rule needed for union and difference) and switching
// polygon at every crossing. Starting only from entries matters: starting
// a forward-only trace from an exit instead traces the wrong (much
// larger, effectively union-shaped) loop - a real bug an earlier version
// of this function had, caught by a wildly-too-large measured area, not a
// subtle one. Returns zero or more closed loops - zero if disjoint, one
// for the ordinary case, more than one if `clip` carves `subject` (here,
// always a single grid cell) into disjoint pieces.
std::vector<std::vector<Point2d>> ClipPolygon(const std::vector<Point2d>& subject_in,
                                               const std::vector<Point2d>& clip_in) {
  struct Vertex {
    Point2d p;
    bool intersect = false;
    bool entry = false;
    bool visited = false;
    int neighbor = -1;  // index into the *other* list (or, before fix-up, a shared id)
  };
  struct PendingHit {
    double alpha;
    Point2d p;
    int id;
  };

  std::vector<Point2d> subject = subject_in;
  if (SignedArea(subject) < 0.0) {
    std::reverse(subject.begin(), subject.end());
  }
  std::vector<Point2d> clip = clip_in;
  if (SignedArea(clip) < 0.0) {
    std::reverse(clip.begin(), clip.end());
  }

  const size_t sn = subject.size();
  const size_t cn = clip.size();
  std::vector<std::vector<PendingHit>> subject_hits(sn);
  std::vector<std::vector<PendingHit>> clip_hits(cn);
  int next_id = 0;

  constexpr double kEps = 1e-9;
  for (size_t i = 0; i < sn; ++i) {
    const Point2d& p1 = subject[i];
    const Point2d& p2 = subject[(i + 1) % sn];
    const double d1x = p2.x - p1.x;
    const double d1y = p2.y - p1.y;
    for (size_t j = 0; j < cn; ++j) {
      const Point2d& p3 = clip[j];
      const Point2d& p4 = clip[(j + 1) % cn];
      const double d2x = p4.x - p3.x;
      const double d2y = p4.y - p3.y;
      const double denom = d1x * d2y - d1y * d2x;
      if (std::abs(denom) < 1e-15) {
        continue;  // parallel (or one segment is degenerate)
      }
      const double dx = p3.x - p1.x;
      const double dy = p3.y - p1.y;
      const double t = (dx * d2y - dy * d2x) / denom;
      const double u = (dx * d1y - dy * d1x) / denom;
      if (t < kEps || t > 1.0 - kEps || u < kEps || u > 1.0 - kEps) {
        continue;
      }
      const Point2d hit(p1.x + t * d1x, p1.y + t * d1y);
      const int id = next_id++;
      subject_hits[i].push_back({t, hit, id});
      clip_hits[j].push_back({u, hit, id});
    }
  }

  auto build_list = [](const std::vector<Point2d>& poly,
                        std::vector<std::vector<PendingHit>>& hits) {
    std::vector<Vertex> list;
    for (size_t i = 0; i < poly.size(); ++i) {
      Vertex original;
      original.p = poly[i];
      list.push_back(original);
      std::sort(hits[i].begin(), hits[i].end(),
                [](const PendingHit& a, const PendingHit& b) { return a.alpha < b.alpha; });
      for (const PendingHit& hit : hits[i]) {
        Vertex crossing;
        crossing.p = hit.p;
        crossing.intersect = true;
        crossing.neighbor = hit.id;  // temporary: fixed up to a real index below
        list.push_back(crossing);
      }
    }
    return list;
  };

  std::vector<Vertex> subject_list = build_list(subject, subject_hits);
  std::vector<Vertex> clip_list = build_list(clip, clip_hits);

  if (next_id == 0) {
    // No boundary crossings at all: one polygon is entirely inside the
    // other, or they're disjoint.
    if (PointInPolygon(subject[0].x, subject[0].y, clip)) {
      return {subject};
    }
    if (PointInPolygon(clip[0].x, clip[0].y, subject)) {
      return {clip};
    }
    return {};
  }

  std::vector<int> id_to_subject_index(static_cast<size_t>(next_id), -1);
  std::vector<int> id_to_clip_index(static_cast<size_t>(next_id), -1);
  for (size_t i = 0; i < subject_list.size(); ++i) {
    if (subject_list[i].intersect) {
      id_to_subject_index[static_cast<size_t>(subject_list[i].neighbor)] = static_cast<int>(i);
    }
  }
  for (size_t i = 0; i < clip_list.size(); ++i) {
    if (clip_list[i].intersect) {
      id_to_clip_index[static_cast<size_t>(clip_list[i].neighbor)] = static_cast<int>(i);
    }
  }
  for (Vertex& v : subject_list) {
    if (v.intersect) {
      v.neighbor = id_to_clip_index[static_cast<size_t>(v.neighbor)];
    }
  }
  for (Vertex& v : clip_list) {
    if (v.intersect) {
      v.neighbor = id_to_subject_index[static_cast<size_t>(v.neighbor)];
    }
  }

  // Only subject_list needs entry/exit: a trace always starts at a
  // subject-side "entry" (a crossing where the subject path moves from
  // outside `clip` to inside it) - starting at an "exit" instead and
  // still always moving forward traces the wrong (much larger,
  // effectively union-shaped) loop, not the intersection. Each list's
  // first vertex is always an original (non-crossing) polygon vertex
  // (build_list appends it before that edge's crossings), so it's safe to
  // seed the toggle with a direct point-in-polygon test.
  {
    bool inside = PointInPolygon(subject_list[0].p.x, subject_list[0].p.y, clip);
    for (Vertex& v : subject_list) {
      if (v.intersect) {
        inside = !inside;
        v.entry = inside;
      }
    }
  }

  std::vector<std::vector<Point2d>> result;
  for (size_t s = 0; s < subject_list.size(); ++s) {
    if (!subject_list[s].intersect || !subject_list[s].entry || subject_list[s].visited) {
      continue;
    }
    std::vector<Point2d> polygon;
    std::vector<Vertex>* list = &subject_list;
    std::vector<Vertex>* other = &clip_list;
    size_t idx = s;
    while (true) {
      Vertex& v = (*list)[idx];
      v.visited = true;
      if (v.intersect && v.neighbor >= 0) {
        (*other)[static_cast<size_t>(v.neighbor)].visited = true;
      }
      polygon.push_back(v.p);
      size_t next_idx = (idx + 1) % list->size();
      while (!(*list)[next_idx].intersect) {
        polygon.push_back((*list)[next_idx].p);
        next_idx = (next_idx + 1) % list->size();
      }
      idx = static_cast<size_t>((*list)[next_idx].neighbor);
      std::swap(list, other);
      if (list == &subject_list && idx == s) {
        break;
      }
    }
    if (polygon.size() >= 3) {
      result.push_back(std::move(polygon));
    }
  }
  return result;
}

// EarClipTriangulate itself now lives in detail/polygon2d.h, shared with
// mesh.cpp's loft end-cap triangulation - both need "triangulate a simple,
// possibly-concave 2D polygon" and there's no reason to maintain two
// copies of that logic.
using dino8::kernel::detail::EarClipTriangulate;

}  // namespace

NurbsSurface NurbsSurface::FromControlGrid(const std::vector<Point3d>& control_grid,
                                            int u_count, int v_count, int u_degree,
                                            int v_degree) {
  NurbsSurface result;
  const int u_order = u_degree + 1;
  const int v_order = v_degree + 1;
  result.surface_.Create(/*dimension=*/3, /*is_rational=*/false, u_order, v_order,
                          u_count, v_count);

  for (int u = 0; u < u_count; ++u) {
    for (int v = 0; v < v_count; ++v) {
      const size_t idx = static_cast<size_t>(u) * static_cast<size_t>(v_count) +
                          static_cast<size_t>(v);
      result.surface_.SetCV(u, v, control_grid[idx]);
    }
  }

  result.surface_.MakeClampedUniformKnotVector(0);
  result.surface_.MakeClampedUniformKnotVector(1);

  return result;
}

int NurbsSurface::DegreeU() const { return surface_.Degree(0); }
int NurbsSurface::DegreeV() const { return surface_.Degree(1); }

int NurbsSurface::CVCountU() const { return surface_.CVCount(0); }
int NurbsSurface::CVCountV() const { return surface_.CVCount(1); }

bool NurbsSurface::IsRational() const { return surface_.IsRational(); }

double NurbsSurface::WeightAt(int i, int j) const { return surface_.Weight(i, j); }

Result NurbsSurface::SetWeightAt(int i, int j, double weight) {
  if (i < 0 || i >= CVCountU() || j < 0 || j >= CVCountV()) {
    return Result::Failed;
  }
  if (WeightAt(i, j) == weight) {
    return Result::NoOpAlreadySatisfied;
  }
  return surface_.SetWeight(i, j, weight) ? Result::Ok : Result::Failed;
}

Point3d NurbsSurface::ControlPointAt(int i, int j) const {
  if (i < 0 || i >= CVCountU() || j < 0 || j >= CVCountV()) {
    throw std::out_of_range(
        "dino8::kernel::NurbsSurface::ControlPointAt: i or j out of range");
  }
  ON_3dPoint point;
  surface_.GetCV(i, j, point);
  return point;
}

Result NurbsSurface::SetControlPointAt(int i, int j, Point3d point) {
  if (i < 0 || i >= CVCountU() || j < 0 || j >= CVCountV()) {
    throw std::out_of_range(
        "dino8::kernel::NurbsSurface::SetControlPointAt: i or j out of range");
  }
  if (ControlPointAt(i, j) == point) {
    return Result::NoOpAlreadySatisfied;
  }
  return surface_.SetCV(i, j, point) ? Result::Ok : Result::Failed;
}

int NurbsSurface::KnotCount(int direction) const { return surface_.KnotCount(direction); }

double NurbsSurface::KnotAt(int direction, int i) const {
  if (i < 0 || i >= KnotCount(direction)) {
    throw std::out_of_range("dino8::kernel::NurbsSurface::KnotAt: i out of range");
  }
  return surface_.Knot(direction, i);
}

Result NurbsSurface::SetKnotAt(int direction, int i, double value) {
  if (i < 0 || i >= KnotCount(direction)) {
    return Result::Failed;
  }
  if (surface_.Knot(direction, i) == value) {
    return Result::NoOpAlreadySatisfied;
  }
  return surface_.SetKnot(direction, i, value) ? Result::Ok : Result::Failed;
}

Result NurbsSurface::InsertKnotAt(int direction, double knot_value, int multiplicity) {
  const Interval domain = Domain(direction);
  if (knot_value <= domain.min || knot_value >= domain.max) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::InsertKnotAt: knot_value must be "
        "strictly inside the surface's own domain in that direction");
  }
  const int degree = surface_.Degree(direction);
  if (multiplicity < 1 || multiplicity > degree) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::InsertKnotAt: multiplicity must be "
        "between 1 and the degree in that direction, inclusive");
  }
  return surface_.InsertKnot(direction, knot_value, multiplicity) ? Result::Ok : Result::Failed;
}

Result NurbsSurface::MakeRational() {
  if (surface_.IsRational()) {
    return Result::NoOpAlreadySatisfied;
  }
  return surface_.MakeRational() ? Result::Ok : Result::Failed;
}

Result NurbsSurface::MakeNonRational() {
  if (!surface_.IsRational()) {
    return Result::NoOpAlreadySatisfied;
  }
  return surface_.MakeNonRational() ? Result::Ok : Result::Failed;
}

Result NurbsSurface::ElevateDegree(int direction, int new_degree) {
  if (new_degree <= surface_.Degree(direction)) {
    return Result::NoOpAlreadySatisfied;
  }
  const bool ok = surface_.IncreaseDegree(direction, new_degree);
  return ok ? Result::Ok : Result::Failed;
}

bool NurbsSurface::IsClosed(int direction) const { return surface_.IsClosed(direction); }

bool NurbsSurface::IsPeriodic(int direction) const { return surface_.IsPeriodic(direction); }

bool NurbsSurface::IsPlanar(double tolerance) const { return surface_.IsPlanar(nullptr, tolerance); }

bool NurbsSurface::IsSphere(double tolerance) const { return surface_.IsSphere(nullptr, tolerance); }

bool NurbsSurface::IsCylinder(double tolerance) const {
  return surface_.IsCylinder(nullptr, tolerance);
}

bool NurbsSurface::IsCone(double tolerance) const { return surface_.IsCone(nullptr, tolerance); }

bool NurbsSurface::IsTorus(double tolerance) const { return surface_.IsTorus(nullptr, tolerance); }

SurfaceSize NurbsSurface::GetApproximateSize() const {
  double width = 0.0;
  double height = 0.0;
  surface_.GetSurfaceSize(&width, &height);
  return SurfaceSize{width, height};
}

double NurbsSurface::ApproximateArea(int u_divisions, int v_divisions) const {
  return TessellateGrid(u_divisions, v_divisions).Area();
}

Result NurbsSurface::Reverse(int direction) {
  return surface_.Reverse(direction) ? Result::Ok : Result::Failed;
}

void NurbsSurface::Transpose() { surface_.Transpose(); }

Result NurbsSurface::Trim(int direction, double t0, double t1) {
  if (t0 >= t1) {
    return Result::Failed;
  }
  return surface_.Trim(direction, ON_Interval(t0, t1)) ? Result::Ok : Result::Failed;
}

Result NurbsSurface::Extend(int direction, double t0, double t1) {
  if (t0 >= t1) {
    return Result::Failed;
  }
  if (surface_.IsClosed(direction)) {
    return Result::Failed;
  }
  const ON_Interval current = surface_.Domain(direction);
  if (t0 >= current.Min() && t1 <= current.Max()) {
    return Result::NoOpAlreadySatisfied;
  }
  return surface_.Extend(direction, ON_Interval(t0, t1)) ? Result::Ok : Result::Failed;
}

Result NurbsSurface::Split(int direction, double t, NurbsSurface& out_west_or_south,
                            NurbsSurface& out_east_or_north) const {
  ON_Surface* west_or_south = nullptr;
  ON_Surface* east_or_north = nullptr;
  const bool ok = surface_.Split(direction, t, west_or_south, east_or_north);
  if (!ok) {
    delete west_or_south;
    delete east_or_north;
    return Result::Failed;
  }

  ON_NurbsSurface* west_or_south_nurbs = ON_NurbsSurface::Cast(west_or_south);
  ON_NurbsSurface* east_or_north_nurbs = ON_NurbsSurface::Cast(east_or_north);
  if (west_or_south_nurbs == nullptr || east_or_north_nurbs == nullptr) {
    delete west_or_south;
    delete east_or_north;
    return Result::Failed;
  }

  out_west_or_south.surface_ = *west_or_south_nurbs;
  out_east_or_north.surface_ = *east_or_north_nurbs;
  delete west_or_south;
  delete east_or_north;
  return Result::Ok;
}

Interval NurbsSurface::Domain(int direction) const {
  const ON_Interval domain = surface_.Domain(direction);
  return Interval{domain.Min(), domain.Max()};
}

Point3d NurbsSurface::PointAt(double u, double v) const {
  ON_3dPoint pt;
  surface_.EvPoint(u, v, pt);
  return pt;
}

Point2d NurbsSurface::ClosestPointParameter(Point3d point, int u_divisions, int v_divisions) const {
  const ON_Interval u_domain = surface_.Domain(0);
  const ON_Interval v_domain = surface_.Domain(1);
  auto distance_squared = [&](double u, double v) { return (PointAt(u, v) - point).LengthSquared(); };

  double u_lo = u_domain.Min();
  double u_hi = u_domain.Max();
  double v_lo = v_domain.Min();
  double v_hi = v_domain.Max();
  double best_u = u_lo;
  double best_v = v_lo;

  constexpr int kRefinementLevels = 8;
  for (int level = 0; level < kRefinementLevels; ++level) {
    double best_d2 = std::numeric_limits<double>::max();
    for (int i = 0; i <= u_divisions; ++i) {
      const double u = u_lo + (u_hi - u_lo) * static_cast<double>(i) / u_divisions;
      for (int j = 0; j <= v_divisions; ++j) {
        const double v = v_lo + (v_hi - v_lo) * static_cast<double>(j) / v_divisions;
        const double d2 = distance_squared(u, v);
        if (d2 < best_d2) {
          best_d2 = d2;
          best_u = u;
          best_v = v;
        }
      }
    }
    const double u_step = (u_hi - u_lo) / u_divisions;
    const double v_step = (v_hi - v_lo) / v_divisions;
    u_lo = std::max(u_domain.Min(), best_u - u_step);
    u_hi = std::min(u_domain.Max(), best_u + u_step);
    v_lo = std::max(v_domain.Min(), best_v - v_step);
    v_hi = std::min(v_domain.Max(), best_v + v_step);
  }
  return Point2d(best_u, best_v);
}

Point3d NurbsSurface::ClosestPoint(Point3d point, int u_divisions, int v_divisions) const {
  const Point2d uv = ClosestPointParameter(point, u_divisions, v_divisions);
  return PointAt(uv.x, uv.y);
}

Vector3d NurbsSurface::NormalAt(double u, double v) const {
  ON_3dPoint point;
  ON_3dVector normal;
  if (!surface_.EvNormal(u, v, point, normal)) {
    throw std::runtime_error(
        "dino8::kernel::NurbsSurface::NormalAt: OpenNURBS couldn't evaluate "
        "a normal at this (u, v) - likely a singular point where the "
        "surface's two partial derivatives are parallel or zero");
  }
  return normal;
}

SurfaceCurvature NurbsSurface::CurvatureAt(double u, double v) const {
  ON_3dPoint point;
  ON_3dVector du, dv, duu, duv, dvv;
  if (!surface_.Ev2Der(u, v, point, du, dv, duu, duv, dvv)) {
    throw std::runtime_error(
        "dino8::kernel::NurbsSurface::CurvatureAt: OpenNURBS couldn't "
        "evaluate second derivatives at this (u, v)");
  }
  ON_3dVector normal = ON_CrossProduct(du, dv);
  if (!normal.Unitize()) {
    throw std::runtime_error(
        "dino8::kernel::NurbsSurface::CurvatureAt: singular point - the "
        "surface's two partial derivatives are parallel or zero");
  }

  const double e_coeff = du * du;
  const double f_coeff = du * dv;
  const double g_coeff = dv * dv;
  const double l_coeff = duu * normal;
  const double m_coeff = duv * normal;
  const double n_coeff = dvv * normal;

  const double denom = e_coeff * g_coeff - f_coeff * f_coeff;
  if (std::abs(denom) < 1e-15) {
    throw std::runtime_error(
        "dino8::kernel::NurbsSurface::CurvatureAt: degenerate first "
        "fundamental form at this (u, v)");
  }

  const double gaussian = (l_coeff * n_coeff - m_coeff * m_coeff) / denom;
  const double mean =
      (l_coeff * g_coeff - 2.0 * m_coeff * f_coeff + n_coeff * e_coeff) / (2.0 * denom);
  const double discriminant = std::max(0.0, mean * mean - gaussian);
  const double sqrt_discriminant = std::sqrt(discriminant);
  return SurfaceCurvature{gaussian, mean, mean + sqrt_discriminant, mean - sqrt_discriminant};
}

SurfaceDivisions NurbsSurface::SuggestedDivisions(double chord_tolerance,
                                                   int isocurve_samples) const {
  if (chord_tolerance <= 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::SuggestedDivisions: chord_tolerance "
        "must be positive");
  }

  const ON_Interval u_domain = surface_.Domain(0);
  const ON_Interval v_domain = surface_.Domain(1);

  // dir=0: first parameter (u) varies, second (v) is held constant - an
  // isocurve running in the U direction. Sampling several of these
  // (at different fixed v) and taking the worst-case suggested sample
  // count accounts for a surface whose U-direction curvature varies
  // across v.
  int u_divisions = 1;
  for (int i = 0; i <= isocurve_samples; ++i) {
    const double v = v_domain.ParameterAt(static_cast<double>(i) / isocurve_samples);
    ON_Curve* iso = surface_.IsoCurve(0, v);
    if (iso == nullptr) {
      continue;
    }
    if (ON_NurbsCurve* nurbs_iso = ON_NurbsCurve::Cast(iso)) {
      NurbsCurve wrapped;
      wrapped.raw() = *nurbs_iso;
      u_divisions = std::max(u_divisions, wrapped.SuggestedSamples(chord_tolerance));
    }
    delete iso;
  }

  // dir=1: second parameter (v) varies, first (u) is held constant - the
  // V-direction counterpart, sampled the same way.
  int v_divisions = 1;
  for (int i = 0; i <= isocurve_samples; ++i) {
    const double u = u_domain.ParameterAt(static_cast<double>(i) / isocurve_samples);
    ON_Curve* iso = surface_.IsoCurve(1, u);
    if (iso == nullptr) {
      continue;
    }
    if (ON_NurbsCurve* nurbs_iso = ON_NurbsCurve::Cast(iso)) {
      NurbsCurve wrapped;
      wrapped.raw() = *nurbs_iso;
      v_divisions = std::max(v_divisions, wrapped.SuggestedSamples(chord_tolerance));
    }
    delete iso;
  }

  return SurfaceDivisions{u_divisions, v_divisions};
}

std::vector<double> NurbsSurface::SuggestedParameterValues(int direction, double chord_tolerance,
                                                            int isocurve_samples) const {
  if (chord_tolerance <= 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::SuggestedParameterValues: "
        "chord_tolerance must be positive");
  }

  const ON_Interval other_domain = surface_.Domain(1 - direction);
  std::vector<double> best_values;
  for (int i = 0; i <= isocurve_samples; ++i) {
    const double c = other_domain.ParameterAt(static_cast<double>(i) / isocurve_samples);
    ON_Curve* iso = surface_.IsoCurve(direction, c);
    if (iso == nullptr) {
      continue;
    }
    if (ON_NurbsCurve* nurbs_iso = ON_NurbsCurve::Cast(iso)) {
      NurbsCurve wrapped;
      wrapped.raw() = *nurbs_iso;
      std::vector<double> values = wrapped.SuggestedParameterValues(chord_tolerance);
      if (values.size() > best_values.size()) {
        best_values = std::move(values);
      }
    }
    delete iso;
  }

  if (best_values.size() < 2) {
    const ON_Interval domain = surface_.Domain(direction);
    best_values = {domain.Min(), domain.Max()};
  }
  return best_values;
}

namespace {

// Shared body of TessellateGrid()/TessellateGridNonUniform(): builds a
// tensor-product mesh over an explicit (not necessarily uniform)
// u_values x v_values grid, with the same whole-cell trim/hole handling
// both public methods document.
Mesh TessellateFromValues(const NurbsSurface& surface, const std::vector<double>& u_values,
                           const std::vector<double>& v_values,
                           const std::vector<Point2d>* trim_polygon,
                           const std::vector<std::vector<Point2d>>* hole_polygons) {
  if (trim_polygon != nullptr && trim_polygon->size() < 3) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface: a non-null trim_polygon must have at "
        "least 3 points (fewer isn't a closed polygon at all - and, before "
        "this check, PointInPolygon() silently treated every point as "
        "outside it, tessellating to a fully empty mesh instead of "
        "failing loudly)");
  }
  if (hole_polygons != nullptr) {
    for (const auto& hole : *hole_polygons) {
      if (hole.size() < 3) {
        throw std::invalid_argument(
            "dino8::kernel::NurbsSurface: every hole polygon must have at "
            "least 3 points (fewer isn't a closed polygon at all - and, "
            "before this check, PointInPolygon() silently treated every "
            "point as outside a too-short hole, so it was ignored entirely "
            "rather than failing loudly)");
      }
    }
  }
  Mesh mesh;
  ON_Mesh& raw = mesh.raw();

  const int u_points = static_cast<int>(u_values.size());
  const int v_points = static_cast<int>(v_values.size());
  const int u_divisions = u_points - 1;
  const int v_divisions = v_points - 1;

  auto grid_index = [v_points](int i, int j) { return i * v_points + j; };

  if (trim_polygon == nullptr) {
    raw.m_V.Reserve(u_points * v_points);
    for (int i = 0; i < u_points; ++i) {
      for (int j = 0; j < v_points; ++j) {
        raw.m_V.Append(ON_3fPoint(surface.PointAt(u_values[static_cast<size_t>(i)],
                                                   v_values[static_cast<size_t>(j)])));
      }
    }

    raw.m_F.Reserve(u_divisions * v_divisions * 2);
    for (int i = 0; i < u_divisions; ++i) {
      for (int j = 0; j < v_divisions; ++j) {
        const int v00 = grid_index(i, j);
        const int v10 = grid_index(i + 1, j);
        const int v11 = grid_index(i + 1, j + 1);
        const int v01 = grid_index(i, j + 1);

        ON_MeshFace tri1;
        tri1.vi[0] = v00;
        tri1.vi[1] = v10;
        tri1.vi[2] = v11;
        tri1.vi[3] = v11;
        raw.m_F.Append(tri1);

        ON_MeshFace tri2;
        tri2.vi[0] = v00;
        tri2.vi[1] = v11;
        tri2.vi[2] = v01;
        tri2.vi[3] = v01;
        raw.m_F.Append(tri2);
      }
    }

    return mesh;
  }

  // Trimmed path: only emit cells whose four corners are all inside the
  // trim polygon, and only the vertices those emitted cells actually use
  // (lazily assigned, so unused grid points aren't left dangling in the
  // output mesh).
  std::vector<bool> inside(static_cast<size_t>(u_points) * static_cast<size_t>(v_points));
  for (int i = 0; i < u_points; ++i) {
    for (int j = 0; j < v_points; ++j) {
      const double u = u_values[static_cast<size_t>(i)];
      const double v = v_values[static_cast<size_t>(j)];
      bool point_inside = PointInPolygon(u, v, *trim_polygon);
      if (point_inside && hole_polygons != nullptr) {
        for (const auto& hole : *hole_polygons) {
          if (PointInPolygon(u, v, hole)) {
            point_inside = false;
            break;
          }
        }
      }
      inside[static_cast<size_t>(grid_index(i, j))] = point_inside;
    }
  }

  std::vector<int> compacted_index(static_cast<size_t>(u_points) * static_cast<size_t>(v_points),
                                    -1);
  auto emit_vertex = [&](int i, int j) {
    const int raw_index = grid_index(i, j);
    if (compacted_index[static_cast<size_t>(raw_index)] == -1) {
      compacted_index[static_cast<size_t>(raw_index)] = raw.m_V.Count();
      raw.m_V.Append(ON_3fPoint(surface.PointAt(u_values[static_cast<size_t>(i)],
                                                 v_values[static_cast<size_t>(j)])));
    }
    return compacted_index[static_cast<size_t>(raw_index)];
  };

  for (int i = 0; i < u_divisions; ++i) {
    for (int j = 0; j < v_divisions; ++j) {
      const bool cell_inside = inside[static_cast<size_t>(grid_index(i, j))] &&
                                inside[static_cast<size_t>(grid_index(i + 1, j))] &&
                                inside[static_cast<size_t>(grid_index(i + 1, j + 1))] &&
                                inside[static_cast<size_t>(grid_index(i, j + 1))];
      if (!cell_inside) {
        continue;
      }

      const int v00 = emit_vertex(i, j);
      const int v10 = emit_vertex(i + 1, j);
      const int v11 = emit_vertex(i + 1, j + 1);
      const int v01 = emit_vertex(i, j + 1);

      ON_MeshFace tri1;
      tri1.vi[0] = v00;
      tri1.vi[1] = v10;
      tri1.vi[2] = v11;
      tri1.vi[3] = v11;
      raw.m_F.Append(tri1);

      ON_MeshFace tri2;
      tri2.vi[0] = v00;
      tri2.vi[1] = v11;
      tri2.vi[2] = v01;
      tri2.vi[3] = v01;
      raw.m_F.Append(tri2);
    }
  }

  return mesh;
}

}  // namespace

Mesh NurbsSurface::TessellateGrid(int u_divisions, int v_divisions,
                                   const std::vector<Point2d>* trim_polygon,
                                   const std::vector<std::vector<Point2d>>* hole_polygons) const {
  if (u_divisions < 1 || v_divisions < 1) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::TessellateGrid: u_divisions and "
        "v_divisions must be at least 1");
  }
  const ON_Interval u_domain = surface_.Domain(0);
  const ON_Interval v_domain = surface_.Domain(1);

  std::vector<double> u_values(static_cast<size_t>(u_divisions) + 1);
  std::vector<double> v_values(static_cast<size_t>(v_divisions) + 1);
  for (int i = 0; i <= u_divisions; ++i) {
    u_values[static_cast<size_t>(i)] = u_domain.ParameterAt(static_cast<double>(i) / u_divisions);
  }
  for (int j = 0; j <= v_divisions; ++j) {
    v_values[static_cast<size_t>(j)] = v_domain.ParameterAt(static_cast<double>(j) / v_divisions);
  }

  return TessellateFromValues(*this, u_values, v_values, trim_polygon, hole_polygons);
}

Mesh NurbsSurface::TessellateGridNonUniform(
    const std::vector<double>& u_values, const std::vector<double>& v_values,
    const std::vector<Point2d>* trim_polygon,
    const std::vector<std::vector<Point2d>>* hole_polygons) const {
  if (u_values.size() < 2 || v_values.size() < 2) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::TessellateGridNonUniform: u_values "
        "and v_values must each have at least 2 entries");
  }
  for (size_t i = 1; i < u_values.size(); ++i) {
    if (u_values[i] <= u_values[i - 1]) {
      throw std::invalid_argument(
          "dino8::kernel::NurbsSurface::TessellateGridNonUniform: u_values "
          "must be strictly increasing");
    }
  }
  for (size_t j = 1; j < v_values.size(); ++j) {
    if (v_values[j] <= v_values[j - 1]) {
      throw std::invalid_argument(
          "dino8::kernel::NurbsSurface::TessellateGridNonUniform: v_values "
          "must be strictly increasing");
    }
  }
  return TessellateFromValues(*this, u_values, v_values, trim_polygon, hole_polygons);
}

Mesh NurbsSurface::TessellateGridAdaptive(
    double chord_tolerance, const std::vector<Point2d>* trim_polygon,
    const std::vector<std::vector<Point2d>>* hole_polygons) const {
  const SurfaceDivisions divisions = SuggestedDivisions(chord_tolerance);
  return TessellateGrid(divisions.u, divisions.v, trim_polygon, hole_polygons);
}

Mesh NurbsSurface::TessellateGridNonUniformAdaptive(
    double chord_tolerance, const std::vector<Point2d>* trim_polygon,
    const std::vector<std::vector<Point2d>>* hole_polygons) const {
  const std::vector<double> u_values = SuggestedParameterValues(0, chord_tolerance);
  const std::vector<double> v_values = SuggestedParameterValues(1, chord_tolerance);
  return TessellateGridNonUniform(u_values, v_values, trim_polygon, hole_polygons);
}

Mesh NurbsSurface::TessellateGridClippedExact(int u_divisions, int v_divisions,
                                               const std::vector<Point2d>& trim_polygon) const {
  if (u_divisions < 1 || v_divisions < 1) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::TessellateGridClippedExact: u_divisions "
        "and v_divisions must be at least 1");
  }
  if (trim_polygon.size() < 3) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::TessellateGridClippedExact: trim_polygon "
        "must have at least 3 points (fewer isn't a closed polygon at all - "
        "and, before this check, an empty trim_polygon segfaulted via an "
        "out-of-bounds clip[0] access deep in the concave-clipping path, "
        "confirmed by a debug run, not merely a silent wrong result)");
  }
  if (!dino8::kernel::detail::IsSimplePolygon(trim_polygon)) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::TessellateGridClippedExact: trim_polygon "
        "must be simple (non-self-intersecting) - a self-intersecting trim "
        "isn't decomposable into a well-defined \"inside\" at all");
  }

  Mesh mesh;
  ON_Mesh& raw = mesh.mesh_;

  const ON_Interval u_domain = surface_.Domain(0);
  const ON_Interval v_domain = surface_.Domain(1);
  auto u_at = [&](int i) { return u_domain.ParameterAt(static_cast<double>(i) / u_divisions); };
  auto v_at = [&](int j) { return v_domain.ParameterAt(static_cast<double>(j) / v_divisions); };

  // Dispatch by convexity: ClipConvex (Sutherland-Hodgman) is the
  // long-proven path every existing caller of exact clipping (in
  // particular Mesh::Cylinder()'s circular trim) already exercises;
  // ClipPolygon (Greiner-Hormann-style) is the newer, general fallback
  // that also handles a concave trim_polygon. Keeping both rather than
  // routing everything through the general path avoids regressing
  // already-proven convex behavior with a less battle-tested one.
  const bool trim_is_convex = IsConvexPolygon(trim_polygon);
  const double orientation_sign = SignedArea(trim_polygon) >= 0.0 ? 1.0 : -1.0;

  // ClipConvex (Sutherland-Hodgman) clips a cell against `clip` one EDGE
  // at a time, testing every subject point's side of that edge's own
  // infinite line via a strict `>= 0.0` cross-product test. That is
  // exactly right for a genuine polygon edge, but a trim_polygon whose
  // boundary runs along a real straight (u, v) line - the common case for
  // a curved face cut by a planar face, e.g. a cylinder wall's cut
  // circle, dead straight in (u, v) since v there is literally height -
  // arrives here as MANY near-duplicate collinear vertices, not one
  // straight edge: BuildLoop() resamples every original chain segment at
  // up to ~samples_per_edge points regardless of curvature, and each of
  // those points is only Newton-refined to the intersecting surfaces' own
  // convergence tolerance, not bit-identical to its neighbors (confirmed
  // directly: up to ~1e-11 (u, v)-unit jitter measured on this exact
  // case). A grid cell corner sitting exactly on that line - the
  // documented, worst-case coincidence: the cut also lands on the
  // tessellation's own v-grid line - then gets tested against dozens of
  // near-duplicate copies of essentially the same infinite line in a row,
  // each with its own independent sub-ULP-to-1e-11 jitter; roughly half
  // of those redundant tests land the point a hair on the wrong side by
  // pure noise, and ONE wrong verdict anywhere in that sequence silently
  // drops the point for good (Sutherland-Hodgman only ever narrows the
  // clipped result, never recovers a point once excluded).
  //
  // Fixed by removing the REDUNDANCY itself rather than loosening the
  // per-edge test: SimplifyCollinearRuns() collapses a run of consecutive
  // trim_polygon vertices that are collinear with their own immediate
  // neighbors (within this function's own kDuplicatePointEpsilon-scale
  // (u, v) precision floor) down to that run's own two endpoints, so a
  // dead-straight cut contributes ONE clip edge instead of dozens of
  // near-duplicate ones - eliminating the redundant re-testing that
  // erodes a coincident point, without changing what "inside" means
  // anywhere (a version of this fix that widened ClipConvex's own
  // boundary test instead was tried first and reverted: it also papers
  // over a SEPARATE, genuinely degenerate case - two DIFFERENT real
  // boundaries meeting at a near-zero-width sliver, e.g. a cut landing
  // exactly on a face's own untouched domain edge - by manufacturing a
  // sliver's worth of spurious extra area there, breaking previously-
  // exact box+box cases (confirmed directly: new nonmanifold mesh edges
  // appeared exactly at such a domain-edge/cut-edge coincidence)).
  // Applied once per call (O(trim_polygon size), not per grid cell), so
  // its cost is negligible next to the O(u_divisions * v_divisions) grid
  // loop below - unlike the reverted EnsureBoundaryVertex repair, this
  // adds no per-boundary-edge or per-triangle work at all.
  //
  // RESOLUTION-MISMATCH FIX (later session, see this method's own doc
  // comment above for the full writeup): `removed_points` is exactly
  // what SimplifyCollinearRuns() above just erased from `trim_polygon` -
  // real, individually-meaningful boundary points (usually a denser
  // neighboring face's own shared cut vertices; see BuildLoop() in
  // boolean_general.cpp for why they arrive here at all), not
  // redundant noise, just collinear with their own straight run.
  // Re-injected below, per grid cell, as forced extra boundary vertices,
  // so this face's own clipped mesh carries the same points its neighbor
  // does along their shared straight cut - without touching the
  // COLLINEAR-simplified polygon ClipConvex itself clips against, so
  // 413c0ae's own fix (this exact section, above) is untouched.
  std::vector<RemovedTrimPoint> removed_points;
  const std::vector<Point2d> simplified_convex_trim =
      trim_is_convex ? SimplifyCollinearRuns(trim_polygon, kUvCoincidenceEpsilon, &removed_points) : trim_polygon;

  // Buckets `removed_points` by which grid cell's own (u, v) domain
  // rectangle contains each one, so the per-cell loop below can look up
  // "does THIS cell need any forced points" in O(1) instead of testing
  // every removed point against every cell (O(u_divisions * v_divisions *
  // removed_points.size()) - the exact per-boundary-edge cost pattern the
  // EARLIER EnsureBoundaryVertex repair was reverted for). A point
  // exactly on a grid line (the u/v_divisions crossing points
  // SimplifyCollinearRuns's own two surviving endpoints already produce
  // natively) rounds into one adjacent cell or the other - if that
  // specific cell's own clipped boundary doesn't happen to carry it (a
  // point already ON a grid line is already a natural mesh vertex there,
  // so InsertForcedPointsIntoTriangulation's own strictly-interior test
  // just no-ops), nothing is lost: the grid clip already produces that
  // exact vertex on both adjacent cells regardless.
  std::vector<std::vector<RemovedTrimPoint>> removed_buckets;
  if (!removed_points.empty()) {
    removed_buckets.resize(static_cast<size_t>(u_divisions) * static_cast<size_t>(v_divisions));
    const double u_width = u_domain.Length() / u_divisions;
    const double v_width = v_domain.Length() / v_divisions;
    for (const RemovedTrimPoint& rp : removed_points) {
      int i = u_width > 0.0 ? static_cast<int>(std::floor((rp.p.x - u_domain.Min()) / u_width)) : 0;
      int j = v_width > 0.0 ? static_cast<int>(std::floor((rp.p.y - v_domain.Min()) / v_width)) : 0;
      i = std::clamp(i, 0, u_divisions - 1);
      j = std::clamp(j, 0, v_divisions - 1);
      removed_buckets[static_cast<size_t>(i) * static_cast<size_t>(v_divisions) + static_cast<size_t>(j)]
          .push_back(rp);
    }
  }

  // ClipPolygon's own crossing detection deliberately excludes an
  // intersection landing within `kEps` of either segment's endpoint (see
  // its comment) - the standard way to avoid double-registering a
  // crossing at a shared vertex. That same exclusion misfires when a
  // trim_polygon vertex lands exactly on a cell's grid line: the cell
  // edge lying along that line hits the trim edge right at its endpoint,
  // gets excluded as "not a real crossing," and the cell's clipped
  // topology comes out wrong (a documented, previously-unhardened
  // degeneracy). Nudging any trim_polygon vertex that's suspiciously
  // close to a grid line off of it by a tiny fraction of one cell's
  // width - the standard "simulation of simplicity" fix for an exact
  // degeneracy in a numerical geometry algorithm, not a workaround for a
  // wrong algorithm - removes the coincidence with a shape change far
  // below this function's own kDuplicatePointEpsilon, let alone any
  // caller's area/volume tolerance. Convex trims go through ClipConvex
  // instead, which has no such exclusion, so this only applies to the
  // concave path.
  std::vector<Point2d> trim_for_clipping = trim_polygon;
  if (!trim_is_convex) {
    const double u_width = u_domain.Length() / u_divisions;
    const double v_width = v_domain.Length() / v_divisions;
    // static: MSVC will not let a lambda use a non-static constexpr local
    // without an explicit capture.
    static constexpr double kOnGridLineFraction = 1e-6;
    static constexpr double kNudgeFraction = 1e-6;
    auto nudge_onto_grid_line = [](double coord, double origin, double width) {
      if (width == 0.0) {
        return coord;
      }
      const double steps = (coord - origin) / width;
      const double nearest_line = std::round(steps);
      if (std::abs(steps - nearest_line) < kOnGridLineFraction) {
        return origin + (nearest_line + kNudgeFraction) * width;
      }
      return coord;
    };
    for (Point2d& p : trim_for_clipping) {
      p.x = nudge_onto_grid_line(p.x, u_domain.Min(), u_width);
      p.y = nudge_onto_grid_line(p.y, v_domain.Min(), v_width);
    }
  }

  // A crossing point computed independently by two adjacent cells (or by
  // both loops of a split cell) can land at slightly different floating
  // point values; dedupe near-coincident consecutive points in a clipped
  // loop before triangulating it - a zero-area sliver edge is a real
  // topological defect (it corrupts ExtrudeCappedSolid's boundary-edge
  // extraction), not just a rendering nit.
  constexpr double kDuplicatePointEpsilon = kUvCoincidenceEpsilon;

  for (int i = 0; i < u_divisions; ++i) {
    for (int j = 0; j < v_divisions; ++j) {
      const double u0 = u_at(i);
      const double u1 = u_at(i + 1);
      const double v0 = v_at(j);
      const double v1 = v_at(j + 1);
      // Same corner order as TessellateGrid's cell winding (v00,v10,v11,v01).
      const std::vector<Point2d> cell = {Point2d(u0, v0), Point2d(u1, v0), Point2d(u1, v1),
                                          Point2d(u0, v1)};

      // A cell straddling a concave trim boundary can clip into more than
      // one disjoint piece (e.g. the trim polygon's boundary crosses the
      // cell twice, cutting off two separate corners); each is
      // triangulated independently below. A convex trim never splits a
      // convex cell into more than one piece, so that path always
      // produces zero or one.
      std::vector<std::vector<Point2d>> pieces;
      if (trim_is_convex) {
        std::vector<Point2d> clipped = ClipConvex(cell, simplified_convex_trim, orientation_sign);
        if (clipped.size() >= 3) {
          pieces.push_back(std::move(clipped));
        }
      } else {
        pieces = ClipPolygon(cell, trim_for_clipping);
      }

      // Only meaningful for the convex path (the only one `removed_buckets`
      // is ever populated for - see this method's own doc comment above);
      // a single lookup per cell, shared by every piece below (the convex
      // path only ever produces zero or one piece per cell anyway).
      static const std::vector<RemovedTrimPoint> kNoForced;
      const std::vector<RemovedTrimPoint>& forced_here =
          (trim_is_convex && !removed_buckets.empty())
              ? removed_buckets[static_cast<size_t>(i) * static_cast<size_t>(v_divisions) + static_cast<size_t>(j)]
              : kNoForced;

      for (const std::vector<Point2d>& piece : pieces) {
        std::vector<Point2d> deduped;
        deduped.reserve(piece.size());
        for (const Point2d& p : piece) {
          if (deduped.empty() ||
              std::abs(p.x - deduped.back().x) > kDuplicatePointEpsilon ||
              std::abs(p.y - deduped.back().y) > kDuplicatePointEpsilon) {
            deduped.push_back(p);
          }
        }
        if (deduped.size() > 1 &&
            std::abs(deduped.front().x - deduped.back().x) <= kDuplicatePointEpsilon &&
            std::abs(deduped.front().y - deduped.back().y) <= kDuplicatePointEpsilon) {
          deduped.pop_back();
        }
        if (deduped.size() < 3) {
          continue;
        }

        // EarClipTriangulate() runs on `deduped` alone - this cell's own
        // bare grid-clip boundary, whatever `forced_here` this cell also
        // carries - and `pts`/`tris` are only grown/patched afterward, by
        // InsertForcedPointsIntoTriangulation's own O(n0)-per-point fan
        // insertion, not by re-triangulating a larger polygon; see that
        // function's own doc comment for why (a real, measured perf
        // regression this avoids).
        std::vector<Point2d> pts = deduped;
        std::vector<std::array<int, 3>> tris = EarClipTriangulate(deduped);
        if (!forced_here.empty()) {
          InsertForcedPointsIntoTriangulation(pts, tris, deduped.size(), forced_here, kUvCoincidenceEpsilon);
        }

        std::vector<int> indices;
        indices.reserve(pts.size());
        for (const Point2d& p : pts) {
          indices.push_back(raw.m_V.Count());
          raw.m_V.Append(ON_3fPoint(PointAt(p.x, p.y)));
        }
        for (const std::array<int, 3>& tri : tris) {
          const Point2d& p0 = pts[static_cast<size_t>(tri[0])];
          const Point2d& p1 = pts[static_cast<size_t>(tri[1])];
          const Point2d& p2 = pts[static_cast<size_t>(tri[2])];
          const double area2 = Cross2d(Point2d(p1.x - p0.x, p1.y - p0.y),
                                        Point2d(p2.x - p0.x, p2.y - p0.y));
          if (std::abs(area2) <= 1e-15) {
            continue;  // degenerate (e.g. three near-collinear points)
          }
          ON_MeshFace face;
          face.vi[0] = indices[static_cast<size_t>(tri[0])];
          face.vi[1] = indices[static_cast<size_t>(tri[1])];
          face.vi[2] = indices[static_cast<size_t>(tri[2])];
          face.vi[3] = face.vi[2];
          raw.m_F.Append(face);
        }
      }
    }
  }

  // Adjacent cells independently compute the same boundary-intersection
  // points as separate vertices; weld them into one consistent mesh.
  return Mesh::MergeAndWeld({mesh});
}

Mesh NurbsSurface::TessellateGridClippedExactAdaptive(double chord_tolerance,
                                                        const std::vector<Point2d>& trim_polygon) const {
  const SurfaceDivisions divisions = SuggestedDivisions(chord_tolerance);
  return TessellateGridClippedExact(divisions.u, divisions.v, trim_polygon);
}

}  // namespace dino8::kernel
