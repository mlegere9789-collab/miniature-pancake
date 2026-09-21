#include "dino8/kernel/point_cloud.h"

#include <stdexcept>

namespace dino8::kernel {

void PointCloud::SetColors(const std::vector<ON_Color>& colors) {
  cloud_.m_C.SetCount(0);
  if (static_cast<int>(colors.size()) != cloud_.PointCount()) return;
  cloud_.m_C.Reserve(static_cast<int>(colors.size()));
  for (const ON_Color& c : colors) cloud_.m_C.Append(c);
}

void PointCloud::SetNormals(const std::vector<Vector3d>& normals) {
  cloud_.m_N.SetCount(0);
  if (static_cast<int>(normals.size()) != cloud_.PointCount()) return;
  cloud_.m_N.Reserve(static_cast<int>(normals.size()));
  for (const Vector3d& n : normals) cloud_.m_N.Append(n);
}

BoundingBox PointCloud::GetBoundingBox() const {
  if (cloud_.PointCount() == 0) throw std::invalid_argument("PointCloud::GetBoundingBox: empty point cloud");
  const ON_BoundingBox bb = cloud_.m_P.BoundingBox();
  return BoundingBox{bb.m_min, bb.m_max};
}

PointCloud PointCloud::Transform(const ON_Xform& xform) const {
  PointCloud out = *this;
  out.cloud_.Transform(xform);
  return out;
}

}  // namespace dino8::kernel
