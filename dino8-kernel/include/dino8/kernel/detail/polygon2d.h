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

  while (order.size() > 3) {
    bool ear_found = false;
    const size_t n = order.size();
    for (size_t i = 0; i < n; ++i) {
      const int ia = order[(i + n - 1) % n];
      const int ib = order[i];
      const int ic = order[(i + 1) % n];
      const Point2d& a = poly[static_cast<size_t>(ia)];
      const Point2d& b = poly[static_cast<size_t>(ib)];
      const Point2d& c = poly[static_cast<size_t>(ic)];
      const double turn = Cross2d(Point2d(b.x - a.x, b.y - a.y), Point2d(c.x - b.x, c.y - b.y));
      if (turn <= 1e-15) {
        continue;  // reflex or degenerate vertex - not a valid ear tip
      }
      bool contains_other = false;
      for (size_t k = 0; k < n; ++k) {
        const int idx = order[k];
        if (idx == ia || idx == ib || idx == ic) {
          continue;
        }
        if (PointInTriangle(poly[static_cast<size_t>(idx)], a, b, c)) {
          contains_other = true;
          break;
        }
      }
      if (contains_other) {
        continue;
      }
      triangles.push_back({ia, ib, ic});
      order.erase(order.begin() + static_cast<long>(i));
      ear_found = true;
      break;
    }
    if (!ear_found) {
      break;
    }
  }
  if (order.size() == 3) {
    triangles.push_back({order[0], order[1], order[2]});
  }
  return triangles;
}

}  // namespace dino8::kernel::detail
