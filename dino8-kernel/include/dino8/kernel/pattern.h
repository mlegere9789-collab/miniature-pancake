// Arrays of a body along a path curve (Rhino ArrayCrv, AutoCAD ARRAYPATH): kernel-level, so a caller no longer has to rebuild the
// frame walk by hand around `Brep::Transform()`.
//
// The stations are the ARC-LENGTH-even parameters of the path (`NurbsCurve::DivideByCount(count - 1)`: the first station is the
// path's start, the last its end). At every station the path carries a frame
//     x = unit tangent T,   y = unit(up x T),   z = x cross y,
// (`up` is the caller's reference direction - the active construction plane's normal in the application; when `up` is parallel to
// the tangent the world z axis, then the world y axis, take its place, so a vertical path still has a frame).
//
// Copy i is the body moved by the rigid map that carries the START frame onto the frame at station i (`ON_Xform::Rotation(f0, fi)`),
// so copy 0 is the body itself, and a body whose own axes were lined up with the start frame keeps its attitude relative to the
// path all along (a helical or curved path turns the copies with it). With `orient == false` every copy is only translated by
// `P(t_i) - P(t_0)` (Rhino's "NoRotation").
#pragma once

#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"

namespace dino8::kernel {

struct CurveArrayFrame {
  Point3d origin;
  Vector3d tangent, normal, binormal;  // an orthonormal, right-handed triple (tangent x normal = binormal)
};

// The frame of the path at parameter `t` (see the file comment). Never fails: a zero tangent falls back to the world x axis.
CurveArrayFrame ArrayFrameOnCurve(const NurbsCurve& path, double t, Vector3d up = Vector3d(0, 0, 1));

// The `count` rigid maps (copy 0 is the identity) described above. Throws std::invalid_argument for `count < 2` or an invalid path.
std::vector<ON_Xform> ArrayAlongCurveTransforms(const NurbsCurve& path, int count, bool orient = true,
                                                Vector3d up = Vector3d(0, 0, 1));

// `count` copies of `body`, each `body.Transform(map)` of the matching map above (side tables and analytic records survive: the maps
// are rigid). Throws what ArrayAlongCurveTransforms() throws.
std::vector<Brep> ArrayBrepAlongCurve(const Brep& body, const NurbsCurve& path, int count, bool orient = true,
                                      Vector3d up = Vector3d(0, 0, 1));

// ---------------------------------------------------------------------------------------------------------------------------------
// Arrays ON A SURFACE (Rhino ArraySrf): copies of a body at a (u, v) lattice of a NURBS surface, each carried by the surface frame.
//
// The lattice is uniform in the surface's PARAMETER domain: u_i = u_min + (u_max - u_min) i / (nu - 1) and likewise v_j (a count of 1
// puts that direction in the middle of its domain); copies are ordered i-major, index i * nv + j. The surface frame at (u, v) is
//     origin = S(u, v),   z = unit(Su x Sv) (the surface's own normal side),   x = unit(Su),   y = z cross x,
// (a degenerate Su x Sv - a pole, a collapsed edge - falls back to the world z axis, and a degenerate Su to the world x axis made
// orthogonal to z, so the frame is always orthonormal and right-handed). `reference` is the frame the body sits in (its origin on the
// body's footprint, its x / y axes the body's own, z the side that should face along the surface normal): copy (i, j) is the body
// moved by the rigid map carrying `reference` onto the surface frame at lattice point (i, j). `orient == false` only translates it
// by S(u, v) - reference.origin.
struct SurfaceArrayFrame {
  Point3d origin;
  Vector3d u_dir, v_dir, normal;  // an orthonormal, right-handed triple (u_dir x v_dir = normal)
};

SurfaceArrayFrame ArrayFrameOnSurface(const NurbsSurface& surface, double u, double v);

// The (u, v) lattice described above, i-major. Throws std::invalid_argument for nu < 1 or nv < 1.
std::vector<Point2d> ArrayLatticeOnSurface(const NurbsSurface& surface, int nu, int nv);

// The nu * nv rigid maps. Throws what ArrayLatticeOnSurface() throws, and for an invalid surface.
std::vector<ON_Xform> ArrayOnSurfaceTransforms(const NurbsSurface& surface, int nu, int nv, const ON_Plane& reference, bool orient = true);

std::vector<Brep> ArrayBrepOnSurface(const Brep& body, const NurbsSurface& surface, int nu, int nv, const ON_Plane& reference,
                                     bool orient = true);

// ---------------------------------------------------------------------------------------------------------------------------------
// Orient on a surface (Rhino OrientOnSrf / Splop) and flow along a surface (Rhino FlowAlongSrf).
//
// SurfaceNormalFrameAt: the frame at the surface point CLOSEST to `near_point` - origin S(u, v), z the surface normal (NormalAt's side),
// x / y a right-handed orthonormal pair in the tangent plane (ON_Plane's own choice for that normal). `uv`, when given, receives the (u, v).
// OrientOnSurfaceTransform(surface, from, near_point) is the rigid map carrying the frame `from` (the object's base point and the z axis
// that should end up along the normal) onto that frame: `from.origin` lands on the surface point, `from.zaxis` on the normal, so an object
// standing on `from` by h along its z has its centroid at distance (r + h) from a sphere's centre. A point that is not on the surface is
// replaced by its closest point first. Throws std::invalid_argument for an invalid surface.
ON_Plane SurfaceNormalFrameAt(const NurbsSurface& surface, Point3d near_point, Point2d* uv = nullptr);
ON_Xform OrientOnSurfaceTransform(const NurbsSurface& surface, const ON_Plane& from, Point3d near_point);
Brep OrientBrepOnSurface(const Brep& body, const NurbsSurface& surface, const ON_Plane& from, Point3d near_point);

// FlowPointAlongSurface: where FlowAlongSurface sends `p` when flowing from `base` onto `target`: p is written as (u, v, d) - the (u, v) of
// its closest point on `base` and its signed distance d along base's normal there - and lands at target(u', v') + d * normal_target(u', v').
// (u', v') reproduce the point's ARC-LENGTH fractions: its distance along base's u isocurve (v fixed) and v isocurve (u fixed), as fractions
// of those isocurves' lengths, is the distance along target's isocurves, as fractions of theirs (the two target parameters are coupled and
// solved by a short fixed-point iteration; an isocurve of zero length, a pole, falls back to the parameter fraction). So lengths along an
// isocurve are preserved where the two isocurves have the same length - a line over ANY interval of a plane patch whose width is a
// cylinder's circumference lands on an arc of the same length, knots or not (a rational circle's parameter is not proportional to its arc
// length between knots, which a parameter-fraction map would inherit) - and an offset d from the base scales lengths by (r + d) / r on a
// cylinder of radius r. Throws std::invalid_argument for an invalid surface.
Point3d FlowPointAlongSurface(const NurbsSurface& base, const NurbsSurface& target, Point3d p);


}  // namespace dino8::kernel
