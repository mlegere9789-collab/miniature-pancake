#include "dino8/kernel/point_cloud.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

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

Result PointCloud::SaveXyz(const std::string& path) const {
  std::ofstream out(path);
  if (!out) return Result::Failed;
  out.precision(17);
  const bool has_normals = HasNormals();
  for (int i = 0; i < PointCount(); ++i) {
    const Point3d p = PointAt(i);
    out << p.x << ' ' << p.y << ' ' << p.z;
    if (has_normals) {
      const Vector3d n = NormalAt(i);
      out << ' ' << n.x << ' ' << n.y << ' ' << n.z;
    }
    out << '\n';
  }
  out.flush();
  return out.good() ? Result::Ok : Result::Failed;
}

Result PointCloud::LoadXyz(const std::string& path, PointCloud& out_cloud) {
  std::ifstream in(path);
  if (!in) return Result::Failed;

  std::vector<std::array<double, 3>> positions;
  std::vector<std::array<double, 3>> normals;
  int line_width = -1;  // fixed to 3 or 6 by the first non-blank line
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream iss(line);
    std::vector<double> values;
    double value = 0;
    while (iss >> value) values.push_back(value);
    if (values.empty()) continue;  // blank/whitespace-only line
    if (!iss.eof()) return Result::Failed;  // stopped on a non-numeric token, not real EOF
    if (values.size() != 3 && values.size() != 6) return Result::Failed;
    if (line_width < 0) {
      line_width = static_cast<int>(values.size());
    } else if (line_width != static_cast<int>(values.size())) {
      return Result::Failed;  // mixed 3-/6-column lines in one file: genuinely ambiguous
    }
    positions.push_back({values[0], values[1], values[2]});
    if (values.size() == 6) normals.push_back({values[3], values[4], values[5]});
  }
  if (positions.empty()) return Result::Failed;

  PointCloud cloud;
  for (const auto& p : positions) cloud.AppendPoint(Point3d(p[0], p[1], p[2]));
  if (!normals.empty()) {
    std::vector<Vector3d> normal_vectors;
    normal_vectors.reserve(normals.size());
    for (const auto& n : normals) normal_vectors.push_back(Vector3d(n[0], n[1], n[2]));
    cloud.SetNormals(normal_vectors);
  }
  out_cloud = std::move(cloud);
  return Result::Ok;
}

namespace {
// Shared by LoadPts()'s header-count and per-point R/G/B parsing - both
// must be whole numbers (a point count and a 0-255 color channel are never
// fractional), so a value like "3.5" is a parse failure, not silently
// truncated to 3.
bool IsIntegerValued(double value) { return value == std::floor(value); }
}  // namespace

Result PointCloud::SavePts(const std::string& path) const {
  std::ofstream out(path);
  if (!out) return Result::Failed;
  out.precision(17);
  const bool has_colors = HasColors();
  out << PointCount() << '\n';
  for (int i = 0; i < PointCount(); ++i) {
    const Point3d p = PointAt(i);
    out << p.x << ' ' << p.y << ' ' << p.z;
    if (has_colors) {
      const ON_Color c = ColorAt(i);
      out << ' ' << c.Red() << ' ' << c.Green() << ' ' << c.Blue();
    }
    out << '\n';
  }
  out.flush();
  return out.good() ? Result::Ok : Result::Failed;
}

Result PointCloud::LoadPts(const std::string& path, PointCloud& out_cloud) {
  std::ifstream in(path);
  if (!in) return Result::Failed;

  std::string line;
  long long declared_count = -1;
  while (std::getline(in, line)) {
    std::istringstream header(line);
    double value = 0;
    if (!(header >> value)) continue;  // blank/whitespace-only line
    if (!header.eof() || !IsIntegerValued(value) || value <= 0) return Result::Failed;
    declared_count = static_cast<long long>(value);
    break;
  }
  if (declared_count < 0) return Result::Failed;  // no header line found at all

  std::vector<std::array<double, 3>> positions;
  std::vector<ON_Color> colors;
  int line_width = -1;  // fixed to 3 or 6 by the first non-blank point line
  while (std::getline(in, line)) {
    std::istringstream iss(line);
    std::vector<double> values;
    double value = 0;
    while (iss >> value) values.push_back(value);
    if (values.empty()) continue;  // blank/whitespace-only line
    if (!iss.eof()) return Result::Failed;  // stopped on a non-numeric token, not real EOF
    if (values.size() != 3 && values.size() != 6) return Result::Failed;
    if (line_width < 0) {
      line_width = static_cast<int>(values.size());
    } else if (line_width != static_cast<int>(values.size())) {
      return Result::Failed;  // mixed 3-/6-column lines in one file: genuinely ambiguous
    }
    positions.push_back({values[0], values[1], values[2]});
    if (values.size() == 6) {
      for (int k = 3; k < 6; ++k) {
        if (!IsIntegerValued(values[k]) || values[k] < 0 || values[k] > 255) return Result::Failed;
      }
      colors.push_back(ON_Color(static_cast<int>(values[3]), static_cast<int>(values[4]),
                                 static_cast<int>(values[5])));
    }
  }
  if (static_cast<long long>(positions.size()) != declared_count) return Result::Failed;

  PointCloud cloud;
  for (const auto& p : positions) cloud.AppendPoint(Point3d(p[0], p[1], p[2]));
  if (!colors.empty()) cloud.SetColors(colors);
  out_cloud = std::move(cloud);
  return Result::Ok;
}

}  // namespace dino8::kernel
