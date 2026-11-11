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

}  // namespace dino8::kernel
