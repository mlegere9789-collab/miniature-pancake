#include "dino8/kernel/pattern.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace dino8::kernel {

CurveArrayFrame ArrayFrameOnCurve(const NurbsCurve& path, double t, Vector3d up) {
  CurveArrayFrame f;
  f.origin = path.PointAt(t);
  Vector3d tan = path.TangentAt(t);
  if (!tan.Unitize()) tan = ON_xaxis;
  Vector3d n = ON_CrossProduct(up, tan);
  if (!n.Unitize()) {
    n = ON_CrossProduct(ON_zaxis, tan);
    if (!n.Unitize()) n = ON_CrossProduct(ON_yaxis, tan);
    n.Unitize();
  }
  Vector3d b = ON_CrossProduct(tan, n);
  b.Unitize();
  f.tangent = tan;
  f.normal = n;
  f.binormal = b;
  return f;
}

std::vector<ON_Xform> ArrayAlongCurveTransforms(const NurbsCurve& path, int count, bool orient, Vector3d up) {
  if (count < 2) {
    throw std::invalid_argument("dino8::kernel::ArrayAlongCurveTransforms: count must be at least 2 (got " + std::to_string(count) + ")");
  }
  if (!path.raw().IsValid()) throw std::invalid_argument("dino8::kernel::ArrayAlongCurveTransforms: the path curve is not valid");
  const std::vector<double> params = path.DivideByCount(count - 1);
  if (static_cast<int>(params.size()) != count) {
    throw std::invalid_argument("dino8::kernel::ArrayAlongCurveTransforms: the path could not be divided into " + std::to_string(count - 1) +
                                " equal arc-length parts");
  }
  const CurveArrayFrame f0 = ArrayFrameOnCurve(path, params.front(), up);
  const ON_Plane p0(f0.origin, f0.tangent, f0.normal);
  std::vector<ON_Xform> out;
  out.reserve(params.size());
  for (size_t i = 0; i < params.size(); ++i) {
    if (i == 0) {
      out.push_back(ON_Xform::IdentityTransformation);
      continue;
    }
    if (orient) {
      const CurveArrayFrame fi = ArrayFrameOnCurve(path, params[i], up);
      ON_Xform xf;
      xf.Rotation(p0, ON_Plane(fi.origin, fi.tangent, fi.normal));
      out.push_back(xf);
    } else {
      out.push_back(ON_Xform::TranslationTransformation(path.PointAt(params[i]) - f0.origin));
    }
  }
  return out;
}

std::vector<Brep> ArrayBrepAlongCurve(const Brep& body, const NurbsCurve& path, int count, bool orient, Vector3d up) {
  const std::vector<ON_Xform> maps = ArrayAlongCurveTransforms(path, count, orient, up);
  std::vector<Brep> out;
  out.reserve(maps.size());
  for (const ON_Xform& xf : maps) out.push_back(body.Transform(xf));
  return out;
}

}  // namespace dino8::kernel
