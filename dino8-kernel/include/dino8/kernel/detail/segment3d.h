#pragma once

// Small shared 3D segment utilities used by more than one translation
// unit (surface_intersect.cpp's curve/curve intersector seeding,
// mesh.cpp's mesh/mesh distance query). Not part of the public
// dino8::kernel API - everything here lives in dino8::kernel::detail and
// is implementation plumbing, not a user-facing capability in its own
// right.

#include <algorithm>

#include "dino8/kernel/types.h"

namespace dino8::kernel::detail {

// Closest points between two 3D segments p0-p1 and q0-q1 (Ericson,
// "Real-Time Collision Detection" 5.1.9 - the same textbook
// Mesh::ClosestPoint()'s own point/triangle region test cites). Returns
// the SQUARED distance at closest approach and the two segment
// parameters s, t in [0, 1] (on p and q respectively) at which it
// occurs - exact closed-form clamped-Voronoi-region math, not an
// iterative search. Degenerate (zero-length) segments are handled as
// points. Parallel segments take the s = 0 end of p (any point along the
// overlap is equally close; the distance itself is still exact).
inline double ClosestSegmentSegment(const Point3d& p0, const Point3d& p1, const Point3d& q0,
                                    const Point3d& q1, double& s, double& t) {
  auto clamp01 = [](double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); };
  const Vector3d d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
  const double a = ON_DotProduct(d1, d1), e = ON_DotProduct(d2, d2), f = ON_DotProduct(d2, r);
  const double kEps = 1e-20;
  if (a <= kEps && e <= kEps) {
    s = 0;
    t = 0;
  } else if (a <= kEps) {
    s = 0;
    t = clamp01(f / e);
  } else {
    const double c = ON_DotProduct(d1, r);
    if (e <= kEps) {
      t = 0;
      s = clamp01(-c / a);
    } else {
      const double b = ON_DotProduct(d1, d2);
      const double denom = a * e - b * b;
      s = denom > kEps ? clamp01((b * f - c * e) / denom) : 0;
      t = (b * s + f) / e;
      if (t < 0) {
        t = 0;
        s = clamp01(-c / a);
      } else if (t > 1) {
        t = 1;
        s = clamp01((b - c) / a);
      }
    }
  }
  const Point3d cp = p0 + d1 * s, cq = q0 + d2 * t;
  const Vector3d diff = cp - cq;
  return ON_DotProduct(diff, diff);
}

}  // namespace dino8::kernel::detail
