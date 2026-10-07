#pragma once

// A single 3D convex-polygon-vs-half-space clip (Sutherland-Hodgman),
// shared by more than one translation unit: boolean.cpp's planar B-rep
// booleans (BooleanIntersectConvexPlanar/BooleanCombinePlanar) clip a
// face polygon against another solid's own planes, and fillet.cpp's
// FilletConvexEdge re-trims the two faces adjacent to a filleted edge
// against a single cutting plane - the exact same primitive, not a
// second implementation of it. This header is the one place it lives;
// boolean.cpp used to keep a private copy before fillet.cpp needed the
// same thing, so this is a pure extraction, not new geometry code.
//
// See e.g. Preparata & Shamos, "Computational Geometry" (Sutherland-
// Hodgman polygon clipping) for the classical, unpatented algorithm this
// implements. Valid for a CONCAVE `poly`, not just a convex one: clipping
// against a single half-space (one plane) is a purely local per-edge
// operation that doesn't depend on the subject polygon's own convexity -
// see boolean.cpp's own BooleanCombinePlanar for how it exploits that on
// a genuinely non-convex face.

#include <cmath>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::kernel::detail {

// Both children of clipping `poly` against `clip_plane`: `inside` is the
// portion where `clip_plane.DistanceTo(p) <= tol` ("inside" = the side
// the plane's own normal points away from), `outside` the rest. A vertex
// exactly on the plane (within `tol`) counts as inside; the new edge
// introduced by a genuine crossing sits exactly on `clip_plane` and is
// shared, coordinate-for-coordinate, by both children.
struct HalfspaceSplit3d {
  std::vector<Point3d> inside;
  std::vector<Point3d> outside;
};

inline HalfspaceSplit3d SplitByHalfspace3d(const std::vector<Point3d>& poly, const ON_Plane& clip_plane,
                                            double tol) {
  HalfspaceSplit3d result;
  if (poly.size() < 3) return result;
  result.inside.reserve(poly.size() + 1);
  result.outside.reserve(poly.size() + 1);
  const size_t n = poly.size();
  for (size_t i = 0; i < n; ++i) {
    const Point3d& cur = poly[i];
    const Point3d& nxt = poly[(i + 1) % n];
    const double dc = clip_plane.DistanceTo(cur);
    const double dn = clip_plane.DistanceTo(nxt);
    const bool cur_in = dc <= tol;
    const bool nxt_in = dn <= tol;
    if (cur_in) {
      result.inside.push_back(cur);
    } else {
      result.outside.push_back(cur);
    }
    if (cur_in != nxt_in && std::fabs(dc - dn) > 1e-15) {
      const double t = dc / (dc - dn);
      const Point3d crossing = cur + t * (nxt - cur);
      result.inside.push_back(crossing);
      result.outside.push_back(crossing);
    }
  }
  return result;
}

// Keeps only the side `clip_plane`'s own normal points away from
// (DistanceTo <= tol) - the "inside" half of SplitByHalfspace3d, for
// every caller (BooleanIntersectConvexPlanar, FilletConvexEdge) that only
// ever wants that one child.
inline std::vector<Point3d> ClipByHalfspace3d(const std::vector<Point3d>& poly, const ON_Plane& clip_plane,
                                               double tol) {
  return SplitByHalfspace3d(poly, clip_plane, tol).inside;
}

// Collapses consecutive near-duplicate vertices (and a closing vertex
// that duplicates the first) - the same cleanup every clip result here
// needs: Sutherland-Hodgman can emit a crossing point that's a
// near-duplicate of an existing vertex when the clip boundary exactly
// coincides with (or passes very close to) one already in the polygon.
inline std::vector<Point3d> CleanPolygon3d(const std::vector<Point3d>& poly, double tol) {
  if (poly.size() < 3) return poly;
  std::vector<Point3d> out;
  out.reserve(poly.size());
  for (const Point3d& p : poly) {
    if (out.empty() || out.back().DistanceTo(p) > tol) out.push_back(p);
  }
  while (out.size() > 1 && out.front().DistanceTo(out.back()) <= tol) out.pop_back();
  return out;
}

}  // namespace dino8::kernel::detail
