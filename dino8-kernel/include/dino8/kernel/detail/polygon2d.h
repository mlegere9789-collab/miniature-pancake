#pragma once

// Small shared 2D polygon utilities used by more than one translation
// unit (surface.cpp's concave-trim clipper, mesh.cpp's loft end caps).
// Not part of the public dino8::kernel API - everything here lives in
// dino8::kernel::detail and is implementation plumbing, not a
// user-facing capability in its own right.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "dino8/kernel/types.h"

namespace dino8::kernel::detail {

inline double Cross2d(const Point2d& a, const Point2d& b) { return a.x * b.y - a.y * b.x; }

inline double SignedArea(const std::vector<Point2d>& polygon) {
  double area = 0.0;
  const size_t n = polygon.size();
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a = polygon[i];
    const Point2d& b = polygon[(i + 1) % n];
    area += Cross2d(a, b);
  }
  return 0.5 * area;
}

inline bool PointInTriangle(const Point2d& p, const Point2d& a, const Point2d& b,
                             const Point2d& c) {
  const double d1 = Cross2d(Point2d(b.x - a.x, b.y - a.y), Point2d(p.x - a.x, p.y - a.y));
  const double d2 = Cross2d(Point2d(c.x - b.x, c.y - b.y), Point2d(p.x - b.x, p.y - b.y));
  const double d3 = Cross2d(Point2d(a.x - c.x, a.y - c.y), Point2d(p.x - c.x, p.y - c.y));
  const bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
  const bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
  return !(has_neg && has_pos);
}

// Ear-clipping triangulation of a simple 2D polygon (convex or concave).
// Returns triangles as index triples into `poly`; on numerical failure
// (couldn't find a valid ear - a degenerate/near-zero-area input),
// returns whatever was triangulated so far rather than looping forever.
inline std::vector<std::array<int, 3>> EarClipTriangulate(const std::vector<Point2d>& poly) {
  std::vector<std::array<int, 3>> triangles;
  std::vector<int> order(poly.size());
  for (size_t i = 0; i < poly.size(); ++i) {
    order[i] = static_cast<int>(i);
  }
  if (SignedArea(poly) < 0.0) {
    std::reverse(order.begin(), order.end());
  }

  // The classic loop (scan from the front for the first vertex that is a convex tip whose triangle holds no other remaining point, clip
  // it, rescan from the front) tests every candidate against every remaining point again after every clip: cubic, and 60+ s for the
  // ~3000-vertex bridged outline of a plate with 64 holes. The set of clipped ears is unchanged here - always the FIRST ear of the
  // current order - but each vertex's "is an ear" verdict is cached and recomputed only when it can have changed: its neighbour was
  // clipped (a new triangle), or the clipped tip itself lay inside its triangle (that point no longer blocks it). A point is never
  // ADDED, so no other verdict changes.
  std::vector<char> ear(poly.size(), 0);
  auto is_ear = [&](size_t pos) {
    const size_t n = order.size();
    const int ia = order[(pos + n - 1) % n];
    const int ib = order[pos];
    const int ic = order[(pos + 1) % n];
    const Point2d& a = poly[static_cast<size_t>(ia)];
    const Point2d& b = poly[static_cast<size_t>(ib)];
    const Point2d& c = poly[static_cast<size_t>(ic)];
    const double turn = Cross2d(Point2d(b.x - a.x, b.y - a.y), Point2d(c.x - b.x, c.y - b.y));
    if (turn <= 1e-15) {
      return false;  // reflex or degenerate vertex - not a valid ear tip
    }
    // A point outside the triangle's bounding box (padded far above rounding) cannot pass PointInTriangle: skip it without the three cross
    // products. The pad keeps this exactly equivalent for the nearly-degenerate triangles whose sign tests could accept a point 1e-17 outside.
    const double lox = std::min({a.x, b.x, c.x}), hix = std::max({a.x, b.x, c.x});
    const double loy = std::min({a.y, b.y, c.y}), hiy = std::max({a.y, b.y, c.y});
    const double pad = 1e-9 * std::max({hix - lox, hiy - loy, 1e-300}) + 1e-300;
    for (size_t k = 0; k < n; ++k) {
      const int idx = order[k];
      if (idx == ia || idx == ib || idx == ic) {
        continue;
      }
      const Point2d& q = poly[static_cast<size_t>(idx)];
      if (q.x < lox - pad || q.x > hix + pad || q.y < loy - pad || q.y > hiy + pad) {
        continue;
      }
      if (PointInTriangle(q, a, b, c)) {
        return false;
      }
    }
    return true;
  };
  if (order.size() > 3) {
    for (size_t pos = 0; pos < order.size(); ++pos) ear[static_cast<size_t>(order[pos])] = is_ear(pos) ? 1 : 0;
  }
  while (order.size() > 3) {
    size_t at = order.size();
    for (size_t i = 0; i < order.size(); ++i) {
      if (ear[static_cast<size_t>(order[i])]) { at = i; break; }
    }
    if (at == order.size()) {
      break;
    }
    const size_t n = order.size();
    const int ia = order[(at + n - 1) % n];
    const int ib = order[at];
    const int ic = order[(at + 1) % n];
    triangles.push_back({ia, ib, ic});
    const Point2d clipped = poly[static_cast<size_t>(ib)];
    order.erase(order.begin() + static_cast<long>(at));
    ear[static_cast<size_t>(ib)] = 0;
    if (order.size() <= 3) break;
    // Verdicts that can have changed: the two neighbours (new triangle), and every vertex whose (unchanged) triangle held the clipped tip.
    const size_t m = order.size();
    const size_t pa = (at + m - 1) % m, pc = at % m;
    for (size_t pos = 0; pos < m; ++pos) {
      const int v = order[pos];
      if (pos == pa || pos == pc) {
        ear[static_cast<size_t>(v)] = is_ear(pos) ? 1 : 0;
        continue;
      }
      if (ear[static_cast<size_t>(v)]) continue;  // a blocker was removed, never added: an ear stays an ear
      const int ia2 = order[(pos + m - 1) % m];
      const int ic2 = order[(pos + 1) % m];
      if (PointInTriangle(clipped, poly[static_cast<size_t>(ia2)], poly[static_cast<size_t>(v)], poly[static_cast<size_t>(ic2)])) {
        ear[static_cast<size_t>(v)] = is_ear(pos) ? 1 : 0;
      }
    }
  }
  if (order.size() == 3) {
    // A real, checked-directly discovery (not merely a defensive extra):
    // when the input polygon carries a genuinely COLLINEAR vertex (turn
    // exactly/near 0 relative to its own neighbors, always failing the
    // `turn <= 1e-15` ear-tip test above no matter how the OTHER vertices
    // around it get stripped away), that vertex can never be clipped as
    // an ordinary ear and is instead left stranded here, among the "final
    // 3" this fallback always used to emit UNCONDITIONALLY - producing a
    // spurious, ZERO-AREA triangle (three collinear points) rather than
    // correctly recognizing that a collinear vertex contributes no extra
    // area on its own. Confirmed directly by the parallel-axis cylinder
    // Difference increment's own lens-cap end faces (boolean.cpp's own
    // BuildLensEndCap): its own `chord_mid` vertex - deliberately placed
    // EXACTLY on the straight line between the arc's own two endpoints
    // (needed there so Brep::FromMixedFaces() sees two sub-edges instead
    // of one, not a shape requirement) - is precisely this case: every
    // OTHER (convex, arc) vertex gets clipped away first, stranding
    // [arc_endpoint_a, arc_endpoint_b, chord_mid] as the final 3, an
    // exactly-degenerate triangle whose own "diagonal" edge
    // (arc_endpoint_a <-> arc_endpoint_b) then corrupts downstream
    // mesh-edge accounting - a genuinely NEW failure mode this codebase's
    // own generic triangulator had never been asked to handle before,
    // since every PRIOR consumer only ever fed truly non-degenerate
    // polygons. Skipping a degenerate final triangle here is safe and
    // lossless: its own area is (by definition) ~0, so omitting it drops
    // no real polygon area, and the polygon's own BOUNDARY edges (built
    // separately, from the ORIGINAL loop points, not from this function's
    // own triangle list) are completely unaffected either way - this
    // function's own callers only ever use its triangles for AREA/VOLUME/
    // MESH purposes, never to reconstruct the boundary.
    const double area2 = std::fabs(Cross2d(Point2d(poly[static_cast<size_t>(order[1])].x - poly[static_cast<size_t>(order[0])].x,
                                                     poly[static_cast<size_t>(order[1])].y - poly[static_cast<size_t>(order[0])].y),
                                            Point2d(poly[static_cast<size_t>(order[2])].x - poly[static_cast<size_t>(order[0])].x,
                                                     poly[static_cast<size_t>(order[2])].y - poly[static_cast<size_t>(order[0])].y)));
    if (area2 > 1e-15) {
      triangles.push_back({order[0], order[1], order[2]});
    }
  }
  return triangles;
}

// True if segments (p1,p2) and (p3,p4) cross at an interior point of
// both (a "proper" intersection) - shared endpoints, touching, and
// collinear overlap are NOT reported as crossings here. That's a
// deliberate, narrower check than "these two segments have any point in
// common": it's enough to catch a genuinely self-intersecting polygon
// (two non-adjacent edges crossing through each other) without also
// rejecting the ordinary case of adjacent edges sharing an endpoint, and
// it mirrors this file's existing epsilon-based, not exhaustively
// robust, approach to degenerate cases (see PointInPolygon's own
// documented boundary caveat elsewhere in this codebase).
inline bool SegmentsProperlyIntersect(const Point2d& p1, const Point2d& p2, const Point2d& p3,
                                       const Point2d& p4) {
  auto sign = [](double v) { return (v > 1e-12) ? 1 : ((v < -1e-12) ? -1 : 0); };
  const int d1 = sign(Cross2d(Point2d(p4.x - p3.x, p4.y - p3.y), Point2d(p1.x - p3.x, p1.y - p3.y)));
  const int d2 = sign(Cross2d(Point2d(p4.x - p3.x, p4.y - p3.y), Point2d(p2.x - p3.x, p2.y - p3.y)));
  const int d3 = sign(Cross2d(Point2d(p2.x - p1.x, p2.y - p1.y), Point2d(p3.x - p1.x, p3.y - p1.y)));
  const int d4 = sign(Cross2d(Point2d(p2.x - p1.x, p2.y - p1.y), Point2d(p4.x - p1.x, p4.y - p1.y)));
  return d1 != 0 && d2 != 0 && d1 != d2 && d3 != 0 && d4 != 0 && d3 != d4;
}

// True if `poly` is a simple polygon: no two non-adjacent edges properly
// cross (see SegmentsProperlyIntersect's own caveats - this doesn't catch
// every possible degeneracy, e.g. an edge passing exactly through a
// non-adjacent vertex, only genuine edge-edge crossings). O(n^2) in the
// vertex count, fine for the modestly-sized trim/ring polygons this is
// meant for.
inline bool IsSimplePolygon(const std::vector<Point2d>& poly, size_t* out_i = nullptr, size_t* out_j = nullptr) {
  const size_t n = poly.size();
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a1 = poly[i];
    const Point2d& a2 = poly[(i + 1) % n];
    for (size_t j = i + 1; j < n; ++j) {
      if (j == i + 1 || (j + 1) % n == i) {
        continue;  // adjacent edges share a vertex, not a crossing
      }
      const Point2d& b1 = poly[j];
      const Point2d& b2 = poly[(j + 1) % n];
      if (SegmentsProperlyIntersect(a1, a2, b1, b2)) {
        if (out_i) *out_i = i;
        if (out_j) *out_j = j;
        return false;
      }
    }
  }
  return true;
}

}  // namespace dino8::kernel::detail
