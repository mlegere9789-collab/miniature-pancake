// Follow Me (profile swept along a polyline path with mitred corners), kernel level.
//
// The sweep of a planar profile polygon along the corners of a polyline path, the way a modeler's "follow me" tool does it for a path made
// of straight edges: along every segment each profile vertex travels in a straight line parallel to the segment, and at every corner all
// the lines are cut by the MITRE plane of that corner (the plane through the corner whose normal is the bisector of the two segment
// directions). Every side face is therefore a planar trapezoid (two of its edges are parallel to the same segment), every section is
// planar, and the result is a valid planar-faced B-rep (Brep::FromPlanarFaces): a closed solid for an open path (the profile and the last
// section are its two end caps), a closed ring without caps for a closed path.
//
// Volume: a prism whose ends are cut obliquely has the volume (cross-section area) x (length of the line through the centroid), so the
// volume of the result is the profile area times the length of the mitred line of the profile's CENTROID; with the centroid on the path
// that is exactly area x path length.
#pragma once

#include <vector>

#include "dino8/kernel/brep.h"

namespace dino8::kernel {

// `profile`: a closed planar polygon (3 or more corners, in order, the first corner is not repeated at the end). Its plane must be
// perpendicular to the first segment of the path (within `tolerance` radians of the direction); the profile does not have to touch
// the path. `path`: the corners of the path (2 or more); with `closed_path` the last corner connects back to the first. The sweep starts
// at the profile (the point where the profile's plane meets the first segment's line, which must lie on that segment) and ends at the
// last corner (open path: a plane perpendicular to the last segment; closed path: back at the profile, which must be reproduced within
// 1e-7 of the profile's size, otherwise the path is refused as not closing).
//
// Throws std::invalid_argument for fewer than 3 profile corners or 2 path corners, a profile that is not planar, a profile plane that is
// not perpendicular to the first segment, a zero-length segment, a corner that reverses direction (the mitre plane would be degenerate),
// a profile plane that does not meet the first segment, a profile that is too large for a corner (a mitred section folds over: its
// normal turns against the direction of travel), and a closed path that does not close.
Brep FollowMePolyline(const std::vector<Point3d>& profile, const std::vector<Point3d>& path, bool closed_path = false,
                      double tolerance = 1e-9);

}  // namespace dino8::kernel
