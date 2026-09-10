#pragma once

// Clips a planar polygon against the ELLIPSE a plane cuts out of a
// CYLINDRICAL face whose axis is NOT perpendicular to that plane - the
// direct generalization of detail::ClipPolygonByCircle3d (circle_clip3d.h)
// needed for BooleanCombineMixed's own oblique plane+cylinder case (see
// boolean.h's own doc comment). A wholly separate file/set of functions,
// not a modification of circle_clip3d.h in place - mirroring how
// EllipseNotchCornerAtVertex (fillet.cpp) is a wholly separate function
// from NotchCornerAtVertex rather than a generalization-in-place, so
// ClipPolygonByCircle3d's own already-verified behavior (and the reverted-
// attempt institutional memory recorded in its own doc comment) is
// provably untouched by anything here.
//
// --- The closed-form ellipse ------------------------------------------
//
// A point on a CylindricalFace's own lateral surface at true angle phi
// (from `cf.frame.xaxis`) and true axial height h is exactly (matching
// boolean.cpp's own file-local PointOnCylFace):
//   Q(phi, h) = frame.origin + h*frame.zaxis
//               + radius*(cos(phi)*frame.xaxis + sin(phi)*frame.yaxis)
// A cutting plane's own equation, written with `base = plane.DistanceTo(
// frame.origin)`, `C = dot(frame.zaxis, plane.zaxis)`, `A = dot(
// frame.xaxis, plane.zaxis)`, `B = dot(frame.yaxis, plane.zaxis)` (the same
// four scalars CylinderPlaneNoInteraction, boolean.cpp, already computes),
// is linear in h:
//   base + h*C + radius*(A*cos(phi) + B*sin(phi)) = 0
//     => h(phi) = -(base + radius*(A*cos(phi)+B*sin(phi))) / C   (C != 0)
// Substituting back into Q gives the intersection curve, parametrized by
// the CYLINDER's own natural angle phi (not a separately-normalized
// ellipse angle):
//   P(phi) = center + radius*cos(phi)*e0 + radius*sin(phi)*e1
//   center = frame.origin - (base/C)*frame.zaxis
//   e0 = frame.xaxis - (A/C)*frame.zaxis
//   e1 = frame.yaxis - (B/C)*frame.zaxis
// `center` is exactly where the cylinder's own axis pierces the plane.
// `e0`/`e1` both lie exactly IN the cutting plane (dot(e0, plane.zaxis) =
// A - (A/C)*C = 0, and likewise for e1) - a real, checked fact this file's
// own 2D-projection logic below relies on: projecting them into the
// plane's own local (xaxis, yaxis) 2D axes loses no information.
//
// A genuine subtlety, NOT swept under the rug: `e0`/`e1` are NOT the true,
// orthogonal geometric semi-axis directions of this ellipse (those are
// radius/|C| along the axis-projected-into-the-plane "tilt" direction, and
// radius along the perpendicular-to-tilt direction) - they are a
// different, generally non-orthogonal, unequal-length basis for the SAME
// ellipse (e0.e1 = A*B/C^2 in general, nonzero whenever both A and B are).
// This does not affect P(phi) itself (still exact, to floating-point
// precision, regardless of basis), but it does mean a circle-clipper's
// "cut wedges at 90-degree PHYSICAL angles" strategy does not carry over
// naively - see ClipPolygonByEllipse3d's own doc comment for how this is
// handled: wedges are cut at phi = phi0 + k*90deg in THIS SAME (e0, e1)/
// phi parametrization, not at 90-degree physical angles, so the ray-cut
// direction and the arc-sample boundary coincide exactly by construction
// (both are literally EllipsePointAt at the same phi), the same guarantee
// ClipPolygonByCircle3d's own angle-as-direction trick provides for the
// (orthogonal) circle case.
//
// --- Line-vs-ellipse crossings, via affine whitening --------------------
//
// SegmentCircleCrossings (circle_clip3d.h) is reused LITERALLY unchanged,
// but on inputs pre-transformed through an affine "whitening" map M^-1
// (M = [radius*e0_2d | radius*e1_2d], the 2x2 matrix whose columns are the
// ellipse's own basis vectors, projected into the polygon's plane's local
// 2D axes) that sends the ellipse to the unit circle: a point p is ON the
// ellipse (p = center + radius*cos(phi)*e0 + radius*sin(phi)*e1 for some
// phi) exactly when w = M^-1*(p - center) equals (cos(phi), sin(phi)),
// i.e. |w| = 1. Since w is an AFFINE map of p (M^-1 is linear, then a
// translation), the segment parameter t along any line a+t*(b-a) is
// affine-invariant - the SAME t at which a segment crosses the true
// ellipse in real space is the t at which the whitened segment crosses
// the unit circle. So SegmentCircleCrossings, called on whitened endpoints
// with radius=1.0, gives exactly the ellipse crossings - a transform, not
// a re-derivation of the quadratic formula itself.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/detail/circle_clip3d.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel::detail {

// The closed-form ellipse plane∩cylinder cuts - see this file's own top
// comment for the derivation. Pure data, built once by
// ComputeEllipseFrame3d and then shared, unchanged, by every caller that
// needs the SAME physical curve (both the planar side's
// ClipPolygonByEllipse3d and the cylindrical side's own split logic in
// boolean.cpp), so two independent evaluations of "the same point" are
// never possible - only one evaluation, EllipsePointAt, ever exists.
struct EllipseFrame3d {
  Point3d center;
  Vector3d e0, e1;  // P(phi) = center + radius*cos(phi)*e0 + radius*sin(phi)*e1
  double radius = 0.0;
  double C = 0.0;  // dot(cf.frame.zaxis, plane.zaxis), signed
};

// Throws std::runtime_error if |dot(cf.frame.zaxis, plane.zaxis)| <
// min_abs_C: the grazing (near-axis-parallel) case where the ellipse's own
// semi-major axis (radius/|C|) is unboundedly large - a genuine geometric
// degeneracy, checked directly rather than silently divided through (the
// same style CylinderPlaneNoInteraction's own amplitude bound and
// EllipseNotchCornerAtVertex's own g(phi).e sign check already use).
inline EllipseFrame3d ComputeEllipseFrame3d(const Brep::CylindricalFace& cf, const ON_Plane& plane,
                                             double min_abs_C = 1e-6) {
  const double base = plane.DistanceTo(cf.frame.origin);
  const double C = ON_DotProduct(cf.frame.zaxis, plane.zaxis);
  if (std::fabs(C) < min_abs_C) {
    throw std::runtime_error(
        "dino8::kernel::detail::ComputeEllipseFrame3d: the cylinder's own axis "
        "is asymptotically parallel to the cutting plane (grazing incidence) - "
        "the ellipse's own semi-major axis is unboundedly large here, a "
        "genuine geometric degeneracy, out of scope for this increment");
  }
  const double A = ON_DotProduct(cf.frame.xaxis, plane.zaxis);
  const double B = ON_DotProduct(cf.frame.yaxis, plane.zaxis);
  EllipseFrame3d ef;
  ef.center = cf.frame.origin - (base / C) * cf.frame.zaxis;
  ef.e0 = cf.frame.xaxis - (A / C) * cf.frame.zaxis;
  ef.e1 = cf.frame.yaxis - (B / C) * cf.frame.zaxis;
  ef.radius = cf.radius;
  ef.C = C;
  return ef;
}

inline Point3d EllipsePointAt(const EllipseFrame3d& ef, double phi) {
  return ef.center + ef.radius * std::cos(phi) * ef.e0 + ef.radius * std::sin(phi) * ef.e1;
}

// Dense, ordered sample of P(phi) for phi in [phi_lo, phi_hi] (`samples`+1
// points total, matching the "kNotchSamples segments" convention
// fillet.cpp's own EllipseNotchCornerAtVertex/kNotchSamples use) - the ONE
// canonical producer of this curve's own points, called from both the
// planar side (ClipPolygonByEllipse3d, below) and the cylindrical side
// (boolean.cpp's own oblique-split logic) so the two faces share BIT-
// IDENTICAL 3D points at every matching sample, the same "one canonical
// list, two consumers" pattern EllipseNotchCornerAtVertex already
// establishes for ConicalFace's own notched cap.
inline std::vector<Point3d> EllipseBoundarySample3d(const EllipseFrame3d& ef, double phi_lo, double phi_hi,
                                                      int samples = 200) {
  std::vector<Point3d> pts;
  pts.reserve(static_cast<size_t>(samples) + 1);
  for (int s = 0; s <= samples; ++s) {
    const double phi = phi_lo + (phi_hi - phi_lo) * static_cast<double>(s) / samples;
    pts.push_back(EllipsePointAt(ef, phi));
  }
  return pts;
}

namespace ellipse_clip_detail {

inline Point2d VectorToPlaneAxes2d(const ON_Plane& plane, const Vector3d& v) {
  return Point2d(v * plane.xaxis, v * plane.yaxis);
}

// Every real root of the segment a->b crossing the ellipse (center c2d,
// basis columns radius*e0_2d/radius*e1_2d) with t in (tol_t, 1-tol_t) - a
// genuine interior crossing. Built by whitening a/b/c2d through M^-1 (see
// this file's own top comment for the affine-invariance argument) and
// reusing circle_clip_detail::SegmentCircleCrossings UNCHANGED on the
// whitened inputs with radius=1.0 - the quadratic formula itself is not
// re-derived here.
inline std::vector<double> SegmentEllipseCrossings(const Point2d& a, const Point2d& b, const Point2d& c2d,
                                                    const Point2d& e0_2d, const Point2d& e1_2d, double radius,
                                                    double tol_t) {
  const double m00 = radius * e0_2d.x, m01 = radius * e1_2d.x;
  const double m10 = radius * e0_2d.y, m11 = radius * e1_2d.y;
  const double det = m00 * m11 - m01 * m10;
  if (std::fabs(det) < 1e-300) {
    throw std::runtime_error(
        "dino8::kernel::detail::SegmentEllipseCrossings: the ellipse's own "
        "basis (e0, e1) projects to a degenerate (singular) 2D frame - "
        "should not happen for any valid EllipseFrame3d");
  }
  const double inv00 = m11 / det, inv01 = -m01 / det;
  const double inv10 = -m10 / det, inv11 = m00 / det;
  auto whiten = [&](const Point2d& p) {
    const double dx = p.x - c2d.x, dy = p.y - c2d.y;
    return Point2d(inv00 * dx + inv01 * dy, inv10 * dx + inv11 * dy);
  };
  const Point2d aw = whiten(a), bw = whiten(b);
  return circle_clip_detail::SegmentCircleCrossings(aw, bw, Point2d(0.0, 0.0), 1.0, tol_t);
}

}  // namespace ellipse_clip_detail

// Clips `poly` (closed, CCW, CONVEX, lying in `poly_plane`) against the
// ELLIPSE `ef` describes (also lying in `poly_plane` - `ef` MUST have been
// built via ComputeEllipseFrame3d(cf, poly_plane) for consistency, since
// this function projects `ef.e0`/`ef.e1`/`ef.center` into `poly_plane`'s
// own local 2D axes directly, trusting they already lie in that plane) -
// the direct generalization of ClipPolygonByCircle3d, same contract:
//  - ellipse center outside poly: returns {poly} unchanged.
//  - ellipse center inside poly, ZERO boundary crossings: returns 4 simple
//    wedge pieces, cut at ellipse-parameter phi = phi0, phi0+90deg,
//    phi0+180deg, phi0+270deg (phi0 = 0, an arbitrary reference) - NOT at
//    90-degree PHYSICAL angles (see this file's own top comment for why
//    that distinction matters for a non-circular ellipse). Each wedge's
//    own radial edges are cut along direction EllipsePointAt(ef, phi0 +
//    k*90deg) - center (k = 0..3, generally NOT mutually orthogonal), and
//    each wedge's own arc portion is exactly
//    EllipseBoundarySample3d(ef, phi0+k*90deg, phi0+(k+1)*90deg, samples/4)
//    - the SAME sample points used to compute the ray-cut direction at
//    each of its two ends, so the cut and the arc coincide exactly by
//    construction.
//  - a genuine boundary CROSSING (partial overlap): throws
//    std::invalid_argument, the same disclosed out-of-scope case
//    ClipPolygonByCircle3d already has.
inline std::vector<std::vector<Point3d>> ClipPolygonByEllipse3d(const std::vector<Point3d>& poly,
                                                                  const ON_Plane& poly_plane,
                                                                  const EllipseFrame3d& ef, double tol,
                                                                  int samples = 200) {
  using namespace circle_clip_detail;
  using namespace ellipse_clip_detail;

  if (poly.size() < 3) return {};

  std::vector<Point2d> poly2d;
  poly2d.reserve(poly.size());
  for (const Point3d& p : poly) poly2d.push_back(ProjectToPlaneAxes2d(poly_plane, p));
  const Point2d c2d = ProjectToPlaneAxes2d(poly_plane, ef.center);
  const Point2d e0_2d = VectorToPlaneAxes2d(poly_plane, ef.e0);
  const Point2d e1_2d = VectorToPlaneAxes2d(poly_plane, ef.e1);

  // A conservative, cheap scale for the segment-parameter tolerance:
  // the smaller of the two projected basis vectors' own lengths (times
  // radius) bounds how much physical distance one unit of whitened-space
  // radius covers, mirroring ClipPolygonByCircle3d's own tol/radius
  // scaling.
  const double min_axis_len =
      ef.radius * std::min(std::hypot(e0_2d.x, e0_2d.y), std::hypot(e1_2d.x, e1_2d.y));
  const double tol_t = std::max(1e-12, tol / std::max(min_axis_len, tol));

  const size_t n = poly2d.size();
  size_t crossing_count = 0;
  for (size_t i = 0; i < n; ++i) {
    crossing_count += SegmentEllipseCrossings(poly2d[i], poly2d[(i + 1) % n], c2d, e0_2d, e1_2d, ef.radius, tol_t)
                          .size();
  }
  if (crossing_count > 0) {
    throw std::invalid_argument(
        "dino8::kernel::detail::ClipPolygonByEllipse3d: the ellipse crosses "
        "the polygon's own boundary (a partial overlap) - out of scope for "
        "this increment, see this function's own doc comment");
  }

  if (!PointInPolygon2d(c2d.x, c2d.y, poly2d)) {
    return {poly};
  }

  const int n_samples = std::max(8, (samples / 4) * 4);  // round up to a multiple of 4
  const int per_quadrant = n_samples / 4;
  constexpr double phi0 = 0.0;
  const double full = 2.0 * ON_PI;

  auto sample3d = [&](int k) { return EllipsePointAt(ef, phi0 + full * static_cast<double>(k) / n_samples); };
  auto to2d = [&](const Point3d& p) { return ProjectToPlaneAxes2d(poly_plane, p); };
  auto to_3d = [&](const Point2d& p) { return poly_plane.origin + p.x * poly_plane.xaxis + p.y * poly_plane.yaxis; };

  // Whether increasing phi (the SAME canonical direction
  // EllipseBoundarySample3d/EllipsePointAt always sweep, matching the
  // cylindrical side's own cap0_notch_points/cap1_notch_points bit-for-
  // bit - see this file's own top comment) traces COUNTERCLOCKWISE or
  // CLOCKWISE in `poly_plane`'s own local (xaxis, yaxis) 2D axes: the
  // signed 2D cross product of the projected basis (e0_2d, e1_2d) - a
  // real, checked-directly subtlety, NOT a theoretical worry: `e0`/`e1`
  // are built from the CYLINDER's own frame (tied to its axis direction
  // and the SIGN of C = dot(cyl.zaxis, plane.zaxis)), with no reason to
  // match `poly_plane`'s own independently-chosen (Newell-normal-derived)
  // local axes' handedness - confirmed to flip sign in practice between
  // two DIFFERENT cutting planes of the very same cylinder whose own
  // outward normals point in opposite directions (e.g. a box's z=0 and
  // z=10 caps). Unlike ClipPolygonByCircle3d (whose circle points are
  // built directly IN `poly_plane`'s own local axes, so this mismatch
  // structurally cannot occur there), the ellipse's own canonical phi
  // sweep is a property of the CYLINDER, not of `poly_plane` - so this
  // function corrects for the mismatch on the BOUNDARY-WALK side (below),
  // never by altering which points sample3d/EllipseBoundarySample3d
  // produce, preserving bit-identical sharing with the cylindrical side
  // in BOTH orientations.
  const double orientation = e0_2d.x * e1_2d.y - e0_2d.y * e1_2d.x;
  const bool phi_is_ccw_in_plane = orientation >= 0.0;

  RayExit exits[4];
  for (int q = 0; q < 4; ++q) {
    const Point2d dir2d = to2d(sample3d(q * per_quadrant)) - c2d;
    exits[q] = ConvexPolygonRayExitDir(poly2d, c2d, dir2d);
  }

  std::vector<std::vector<Point3d>> pieces;
  pieces.reserve(4);
  for (int q = 0; q < 4; ++q) {
    const RayExit& e0x = exits[q];
    const RayExit& e1x = exits[(q + 1) % 4];
    std::vector<Point3d> piece3d;
    // Start at this quadrant's own starting arc sample (3D, exact - the
    // same point EllipseBoundarySample3d would report at this index)...
    piece3d.push_back(sample3d(q * per_quadrant));
    // ...radially out to where that ray exits `poly` (2D exit point lifted
    // back into 3D via poly_plane's own local axes)...
    piece3d.push_back(to_3d(e0x.point));
    // ...along `poly`'s own boundary (every original vertex strictly
    // between the two exit points), walked in whichever POLYGON-INDEX
    // direction actually matches the arc's own physical sweep direction
    // from e0x to e1x (see `phi_is_ccw_in_plane`'s own comment above) -
    // increasing index when the ellipse's own canonical phi sweep is CCW
    // in this plane's local axes (the common case, and the ONLY case
    // ClipPolygonByCircle3d's own analogous logic ever needs), decreasing
    // when it's CW...
    if (phi_is_ccw_in_plane) {
      for (size_t k = (e0x.edge_index + 1) % n; k != (e1x.edge_index + 1) % n; k = (k + 1) % n) {
        piece3d.push_back(poly[k]);
      }
    } else {
      for (size_t k = e0x.edge_index; k != e1x.edge_index; k = (k + n - 1) % n) {
        piece3d.push_back(poly[k]);
      }
    }
    piece3d.push_back(to_3d(e1x.point));
    // ...radially back in to the ellipse's own boundary at this quadrant's
    // end sample...
    piece3d.push_back(sample3d((q + 1) * per_quadrant));
    // ...and finally along the ellipse's own boundary (CLOCKWISE - the
    // "short way" back from this quadrant's end sample to its start
    // sample), sampled at the SAME phi values EllipseBoundarySample3d
    // would produce for this quadrant's own reversed range.
    for (int s = 1; s < per_quadrant; ++s) {
      piece3d.push_back(sample3d((q + 1) * per_quadrant - s));
    }

    pieces.push_back(std::move(piece3d));
  }
  return pieces;
}

}  // namespace dino8::kernel::detail
