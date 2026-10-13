#pragma once

// The shared-boundary-schedule primitive Brep::TessellateConforming()
// (brep.h/brep.cpp) needs to make a wedge PlanarFace's own arc boundary
// and the adjacent Brep::CylindricalFace's own matching boundary row
// tessellate to LITERALLY the same 3D points - the actual fix for the
// watertightness gap BooleanCombineMixed's own doc comment (boolean.h)
// and ClipPolygonByCircle3d's own doc comment (circle_clip3d.h) disclose.
//
// Deliberately isolated into its own tiny header, separate from both
// boolean.cpp (which builds the wedge polygons this samples) and
// brep.cpp (which builds the cylinder's own tessellation): a prior,
// reverted attempt at this exact fix (see circle_clip3d.h's own top
// comment) put the analogous frame-conversion logic INSIDE
// ClipPolygonByCircle3d itself, where a real handedness bug (poly_plane's
// own zaxis coming out anti-parallel to the cylinder frame's own
// xaxis-cross-yaxis for certain cap orientations) silently corrupted the
// wedge polygon actually used for boolean CLASSIFICATION - not just its
// later tessellation. Keeping the frame-to-frame angle conversion here,
// in its own free function nothing classification-critical ever calls,
// means the exact same bug (if it recurred) could only ever break this
// new, additive, opt-in tessellation path - never ClipPolygonByCircle3d,
// SplitMixedAgainstAllFaces, or ClassifyPointVsMixedSolid, none of which
// this header is included by.

#include <cmath>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::kernel::detail {

// `count + 1` points evenly spaced in TRUE ANGLE (not NURBS/circle
// parameter - a plain closed-form trig sample, the same "center +
// radius*(cos*xaxis + sin*yaxis)" construction ClipPolygonByCircle3d's
// own `sample2d` already uses, just generalized to 3D and to an
// arbitrary [angle_begin, angle_end] sweep instead of a fixed 0..2*pi
// one) from `angle_begin` to `angle_end`, linearly interpolated - so
// `angle_end < angle_begin` (a "backwards", decreasing sweep - exactly
// what a wedge's own arc_run records, see PlanarFace::ArcRun's own doc
// comment for why) works the same as an increasing one, no special
// casing needed. `xaxis`/`yaxis` are assumed unit and mutually
// orthogonal (an ON_Plane's own xaxis/yaxis always are) but are NOT
// required to be right-handed with any particular third axis - this
// function never computes or assumes a zaxis at all, unlike an
// ON_Circle/NURBS-form construction would; that is precisely why it
// cannot suffer the earlier attempt's own handedness failure mode (see
// this header's own top comment).
//
// The first returned point is always exactly
// `center + radius*(cos(angle_begin)*xaxis + sin(angle_begin)*yaxis)`
// and the last is the same formula at `angle_end` - callers that need
// two independently-parameterized boundaries to share exact endpoints
// (a wedge's own two rail corners, say) get that "for free" as long as
// both calls pass the same `angle_begin`/`angle_end`/`center`/`radius`/
// `xaxis`/`yaxis`.
//
// Throws std::invalid_argument if `count` is negative (a 0 count is
// valid and degenerate - a single point at `angle_begin`, not an error,
// since some callers may legitimately ask for an unsubdivided run).
inline std::vector<Point3d> ArcSchedule3d(const Point3d& center, double radius, const Vector3d& xaxis,
                                           const Vector3d& yaxis, double angle_begin, double angle_end,
                                           int count) {
  if (count < 0) {
    throw std::invalid_argument("dino8::kernel::detail::ArcSchedule3d: count must be >= 0");
  }
  std::vector<Point3d> pts;
  pts.reserve(static_cast<size_t>(count) + 1);
  for (int k = 0; k <= count; ++k) {
    const double t = (count == 0) ? 0.0 : static_cast<double>(k) / static_cast<double>(count);
    const double angle = angle_begin + (angle_end - angle_begin) * t;
    pts.push_back(center + radius * (std::cos(angle) * xaxis + std::sin(angle) * yaxis));
  }
  return pts;
}

// The rotation offset between `plane.xaxis` and `cyl_frame`'s own
// angle-zero direction (`cyl_frame.xaxis`) - concretely, the angle at
// which `cyl_frame.xaxis` itself sits when expressed in `plane`'s own
// (xaxis, yaxis) basis: `cyl_frame.xaxis` (projected onto that basis) is
// at `plane`-local angle `AngleOffsetBetweenFrames(plane, cyl_frame)`.
// `plane` and `cyl_frame` are assumed to share the same physical 2D
// subspace (`plane.zaxis` parallel OR antiparallel to `cyl_frame.zaxis`)
// - exactly the precondition SplitMixedAgainstAllFaces' own case
// (ii)/(iii) already checks (`align > 1 - kAxisAlignTol`) before ever
// producing a PlanarFace::ArcRun at all.
//
// Signed via a full `atan2` against BOTH the `plane.xaxis` AND
// `plane.yaxis` components of `cyl_frame.xaxis` (not, say, an `acos` of
// a single dot product, which would lose the sign entirely) - so the
// returned angle is exact and well-defined for ANY relative orientation
// of the two frames, including a `cyl_frame` that is a mirror image of
// `plane` (`plane.zaxis` antiparallel to `cyl_frame.zaxis`) - the exact
// scenario that broke the earlier, reverted attempt at this fix (see
// this header's own top comment). This function alone does not decide
// whether converting a full angle SWEEP between the two frames also
// needs a sign flip on top of this offset (see
// ConvertAngleBetweenFrames() below for that) - it only ever answers the
// narrower, unconditionally well-defined question "where does
// cyl_frame.xaxis point, in plane's own coordinates" - which is why this
// one function is small enough to unit-test standalone, including
// against a deliberately left-handed synthetic frame pair, before it
// ever touches real geometry.
inline double AngleOffsetBetweenFrames(const ON_Plane& plane, const ON_Plane& cyl_frame) {
  const double cx = ON_DotProduct(cyl_frame.xaxis, plane.xaxis);
  const double cy = ON_DotProduct(cyl_frame.xaxis, plane.yaxis);
  return std::atan2(cy, cx);
}

// Converts a true angle `theta`, measured in `plane`'s own (xaxis,
// yaxis) basis, into the equivalent true angle in `cyl_frame`'s own
// (xaxis, yaxis) basis - the actual angle-conversion operation
// Brep::TessellateConforming() needs, built on AngleOffsetBetweenFrames()
// above but additionally reading off the two frames' relative handedness
// DIRECTLY (via the sign of `plane.zaxis . cyl_frame.zaxis`) rather than
// assuming it.
//
// This extra step is not optional bookkeeping: when the two frames share
// the same normal direction (`plane.zaxis` parallel to `cyl_frame.zaxis`
// - e.g. a drilled box's own TOP cap, whose outward normal already
// matches the drilling cylinder's fixed axis direction), converting
// between their two angle conventions is a pure rotation, and
// `theta - AngleOffsetBetweenFrames(...)` is exactly right. But when the
// normals are OPPOSED (a drilled box's own BOTTOM cap, whose outward
// normal points the opposite way from that same fixed axis direction),
// the two (xaxis, yaxis) bases are mirror images of one another over
// that shared physical plane - converting between them is a REFLECTION,
// under which increasing angle in one frame corresponds to DECREASING
// angle in the other, so the very same additive formula would silently
// return an angle running the wrong way around the circle. This is
// exactly the class of bug that broke the earlier, reverted attempt at
// this fix (see circle_clip3d.h's own doc comment: "for certain cap
// orientations the constructed circle plane ended up left-handed ...
// which silently ... misplaced several wedges' own arc vertices,
// corrupting classification") - the reason this function exists at all,
// rather than every caller re-deriving this formula (and this exact
// pitfall) by hand.
//
// Verified directly (not merely derived on paper) against a synthetic
// same-handed pair AND a synthetic MIRRORED (opposite-normal) pair by
// this file's own unit tests - the mirrored case is the one that matters
// here, since it is the one a hand-rolled "always add the offset" version
// would get silently backwards.
inline double ConvertAngleBetweenFrames(double theta, const ON_Plane& plane, const ON_Plane& cyl_frame) {
  const double offset = AngleOffsetBetweenFrames(plane, cyl_frame);
  const bool same_handed = ON_DotProduct(plane.zaxis, cyl_frame.zaxis) > 0.0;
  return same_handed ? (theta - offset) : (offset - theta);
}

}  // namespace dino8::kernel::detail
