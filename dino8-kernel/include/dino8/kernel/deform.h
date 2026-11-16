#pragma once

#include <functional>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// Non-affine deformations (space warps) as kernel operations: Twist / Bend / Taper as closed-form point maps, and the
// deformation of a mesh, a NURBS curve and a NURBS surface by such a map. A NURBS cannot be warped exactly by moving its
// control points (the image of a NURBS under a nonlinear map is not a NURBS of the same net), so the curve and surface
// versions refine the knot vectors until the control-point-mapped result stays within a caller tolerance of the true image
// map(S(u, v)) and MEASURE that deviation, never assuming it.

using PointMap = std::function<Point3d(Point3d)>;

// Twist about the axis `axis_start -> axis_end`: a point at axial fraction t = (p - axis_start).axis / |axis| is rotated about
// the axis by `angle_radians * clamp(t, 0, 1)` (not clamped when `infinite`, the twist then continues past both ends).
// Throws std::invalid_argument for a zero-length axis or a non-finite angle.
PointMap MakeTwist(Point3d axis_start, Point3d axis_end, double angle_radians, bool infinite = false);

// Bend of the straight spine `spine_start -> spine_end` into the circular arc that leaves `spine_start` along the spine and
// passes through `through` (the arc's plane contains the spine and `through`): radius = |d|^2 / (2 dn), angle = 2 atan2(dn, d.u)
// with d = through - spine_start, u the spine direction and dn the distance of `through` from the spine line. A point at spine
// coordinate s <= 0 is unchanged; 0 < s <= |spine| is carried onto the arc at angle angle * s / |spine| (its offset h towards
// the arc's centre side and its offset along the arc's binormal are kept); s > |spine| continues along the arc's end tangent.
// Throws std::invalid_argument when `through` lies on (or behind) the spine, or the spine has zero length.
PointMap MakeBend(Point3d spine_start, Point3d spine_end, Point3d through);

// Radius and angle (radians) of the arc MakeBend() builds, for callers that report them. Same preconditions.
void BendRadiusAndAngle(Point3d spine_start, Point3d spine_end, Point3d through, double& radius, double& angle_radians);

// Taper along `axis_start -> axis_end`: the offset of a point from the axis is scaled by ((1 - t) d0 + t d1) / d0 with t the
// axial fraction (clamped to [0, 1] unless `infinite`). Throws std::invalid_argument for a zero-length axis or d0 <= 0.
PointMap MakeTaper(Point3d axis_start, Point3d axis_end, double d0, double d1, bool infinite = false);

// To Sphere about `center`: a point at distance d from the centre (d > 0) moves along its own radial line to the distance
// (1 - factor) d + factor radius, so factor 0 leaves every point where it is, factor 1 puts every point on the sphere of the given
// radius and a value in between interpolates linearly in the radius. A point at the centre itself has no radial direction and stays.
// Blender's To Sphere takes the mean distance of the selection as the radius: MeanDistanceFrom() gives that value for a point list.
// Throws std::invalid_argument for a negative or non-finite radius or a factor outside [0, 1].
PointMap MakeToSphere(Point3d center, double radius, double factor);

// Mean of |p - center| over `points` (0 for an empty list).
double MeanDistanceFrom(Point3d center, const std::vector<Point3d>& points);

// What DeformSurface()/DeformCurve() measured.
struct DeformReport {
  int refinements = 0;               // knot-refinement rounds used (0 = the original net already met the tolerance)
  double achieved_deviation = 0.0;   // worst |result(p) - map(source(p))| over the verification points, a SAMPLED lower bound
  int u_count = 0, v_count = 0;      // control-net size of the result (v_count 0 for a curve)
};

// The mesh with every vertex replaced by map(vertex). Same topology; normals and spatial indices are not carried (recompute
// with ComputeVertexNormals()). Throws std::invalid_argument if the map returns a non-finite point.
Mesh DeformMesh(const Mesh& mesh, const PointMap& map);

// `surface` deformed by `map`, to within `tolerance` (> 0) of the true image. Rounds of midpoint knot insertion in both
// directions (rational surfaces keep their weights; each control point's Euclidean position is mapped) run until the worst
// deviation at the verification points - three interior samples per knot span per direction, never the points the net itself
// is built from - is <= tolerance, at most `max_refinements` rounds. Returns Ok (result in `out`), or Failed with `out`
// untouched and the best deviation reached in `report`. `report` is filled in both cases. Throws std::invalid_argument for a
// non-finite or non-positive tolerance, a negative `max_refinements`, or a map that returns a non-finite point.
Result DeformSurface(const NurbsSurface& surface, const PointMap& map, double tolerance, NurbsSurface& out,
                     DeformReport* report = nullptr, int max_refinements = 6);

// The same for a curve.
Result DeformCurve(const NurbsCurve& curve, const PointMap& map, double tolerance, NurbsCurve& out,
                   DeformReport* report = nullptr, int max_refinements = 8);

}  // namespace dino8::kernel
