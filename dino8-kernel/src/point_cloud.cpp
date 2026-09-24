#include "dino8/kernel/point_cloud.h"

#include <algorithm>
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

namespace {
// Ascending by distance, ties broken by ascending index - shared by
// KNearest() and PointsWithinRadius() so both return a deterministic
// order regardless of point insertion order or coincident points.
bool ByDistanceThenIndex(const PointCloudNeighbor& a, const PointCloudNeighbor& b) {
  if (a.distance != b.distance) return a.distance < b.distance;
  return a.index < b.index;
}
}  // namespace

std::vector<PointCloudNeighbor> PointCloud::KNearest(Point3d query, int k) const {
  if (k <= 0) throw std::invalid_argument("PointCloud::KNearest: k must be positive");
  const int n = cloud_.PointCount();
  if (n == 0) throw std::invalid_argument("PointCloud::KNearest: empty point cloud");

  std::vector<PointCloudNeighbor> all;
  all.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    all.push_back(PointCloudNeighbor{i, query.DistanceTo(Point3d(cloud_.m_P[i]))});
  }
  const size_t keep = std::min(static_cast<size_t>(k), all.size());
  std::partial_sort(all.begin(), all.begin() + static_cast<long>(keep), all.end(), ByDistanceThenIndex);
  all.resize(keep);
  return all;
}

std::vector<PointCloudNeighbor> PointCloud::PointsWithinRadius(Point3d query, double radius) const {
  if (radius < 0.0) throw std::invalid_argument("PointCloud::PointsWithinRadius: radius must be >= 0");
  const int n = cloud_.PointCount();

  std::vector<PointCloudNeighbor> found;
  for (int i = 0; i < n; ++i) {
    const double d = query.DistanceTo(Point3d(cloud_.m_P[i]));
    if (d <= radius) found.push_back(PointCloudNeighbor{i, d});
  }
  std::sort(found.begin(), found.end(), ByDistanceThenIndex);
  return found;
}

}  // namespace dino8::kernel
