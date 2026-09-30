#include "dino8/kernel/point_cloud.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
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

namespace {

// Packs an 8-bit-per-channel color into the single IEEE-754 float PCD's
// own `rgb` field convention expects - the same bit layout real PCL files
// use (`(r << 16) | (g << 8) | b`, then those 32 bits read back out as a
// float rather than an int) - and its exact inverse. Written as a double
// on the way out (SavePcd()'s own stream precision(17) already makes that
// double print with enough digits to parse back to the exact same float
// bit pattern) and narrowed back to float on the way in before splitting
// out the channels, so a color survives SavePcd()/LoadPcd() exactly, not
// just approximately.
double PackPcdRgb(const ON_Color& c) {
  const uint32_t packed = (static_cast<uint32_t>(c.Red()) << 16) |
                           (static_cast<uint32_t>(c.Green()) << 8) |
                           static_cast<uint32_t>(c.Blue());
  float as_float = 0.0f;
  std::memcpy(&as_float, &packed, sizeof(as_float));
  return static_cast<double>(as_float);
}

ON_Color UnpackPcdRgb(double value) {
  const float as_float = static_cast<float>(value);
  uint32_t packed = 0;
  std::memcpy(&packed, &as_float, sizeof(packed));
  const int r = static_cast<int>((packed >> 16) & 0xFF);
  const int g = static_cast<int>((packed >> 8) & 0xFF);
  const int b = static_cast<int>(packed & 0xFF);
  return ON_Color(r, g, b);
}

// The four FIELDS combinations SavePcd()/LoadPcd() know about, in the
// same order SavePcd() picks between them - see both functions' own doc
// comments in point_cloud.h.
enum class PcdFieldSet { kXyz, kXyzRgb, kXyzNormal, kXyzRgbNormal };

std::string PcdFieldsLine(PcdFieldSet fields) {
  switch (fields) {
    case PcdFieldSet::kXyz: return "x y z";
    case PcdFieldSet::kXyzRgb: return "x y z rgb";
    case PcdFieldSet::kXyzNormal: return "x y z normal_x normal_y normal_z";
    case PcdFieldSet::kXyzRgbNormal: return "x y z rgb normal_x normal_y normal_z";
  }
  return "";
}

int PcdColumnCount(PcdFieldSet fields) {
  switch (fields) {
    case PcdFieldSet::kXyz: return 3;
    case PcdFieldSet::kXyzRgb: return 4;
    case PcdFieldSet::kXyzNormal: return 6;
    case PcdFieldSet::kXyzRgbNormal: return 7;
  }
  return 0;
}

}  // namespace

Result PointCloud::SavePcd(const std::string& path) const {
  std::ofstream out(path);
  if (!out) return Result::Failed;

  const bool has_colors = HasColors();
  const bool has_normals = HasNormals();
  const PcdFieldSet fields = has_colors
                                  ? (has_normals ? PcdFieldSet::kXyzRgbNormal : PcdFieldSet::kXyzRgb)
                                  : (has_normals ? PcdFieldSet::kXyzNormal : PcdFieldSet::kXyz);
  const int column_count = PcdColumnCount(fields);
  const int n = PointCount();

  out << "# .PCD v0.7 - Point Cloud Data file format\n";
  out << "VERSION 0.7\n";
  out << "FIELDS " << PcdFieldsLine(fields) << '\n';
  for (int i = 0; i < column_count; ++i) out << (i == 0 ? "SIZE 4" : " 4");
  out << '\n';
  for (int i = 0; i < column_count; ++i) out << (i == 0 ? "TYPE F" : " F");
  out << '\n';
  for (int i = 0; i < column_count; ++i) out << (i == 0 ? "COUNT 1" : " 1");
  out << '\n';
  out << "WIDTH " << n << '\n';
  out << "HEIGHT 1\n";
  out << "VIEWPOINT 0 0 0 1 0 0 0\n";
  out << "POINTS " << n << '\n';
  out << "DATA ascii\n";

  out.precision(17);
  for (int i = 0; i < n; ++i) {
    const Point3d p = PointAt(i);
    out << p.x << ' ' << p.y << ' ' << p.z;
    if (has_colors) out << ' ' << PackPcdRgb(ColorAt(i));
    if (has_normals) {
      const Vector3d nrm = NormalAt(i);
      out << ' ' << nrm.x << ' ' << nrm.y << ' ' << nrm.z;
    }
    out << '\n';
  }
  out.flush();
  return out.good() ? Result::Ok : Result::Failed;
}

Result PointCloud::LoadPcd(const std::string& path, PointCloud& out_cloud) {
  std::ifstream in(path);
  if (!in) return Result::Failed;

  bool have_version = false;
  bool have_fields = false;
  bool have_points = false;
  bool have_data = false;
  PcdFieldSet fields = PcdFieldSet::kXyz;
  long long declared_count = -1;
  int height = 1;

  std::string line;
  while (std::getline(in, line)) {
    std::istringstream iss(line);
    std::string keyword;
    if (!(iss >> keyword)) continue;  // blank/whitespace-only line
    if (keyword[0] == '#') continue;

    if (keyword == "VERSION") {
      have_version = true;
    } else if (keyword == "FIELDS") {
      std::vector<std::string> tokens;
      std::string t;
      while (iss >> t) tokens.push_back(t);
      std::string joined;
      for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) joined += ' ';
        joined += tokens[i];
      }
      if (joined == PcdFieldsLine(PcdFieldSet::kXyz)) {
        fields = PcdFieldSet::kXyz;
      } else if (joined == PcdFieldsLine(PcdFieldSet::kXyzRgb)) {
        fields = PcdFieldSet::kXyzRgb;
      } else if (joined == PcdFieldsLine(PcdFieldSet::kXyzNormal)) {
        fields = PcdFieldSet::kXyzNormal;
      } else if (joined == PcdFieldsLine(PcdFieldSet::kXyzRgbNormal)) {
        fields = PcdFieldSet::kXyzRgbNormal;
      } else {
        return Result::Failed;  // an unsupported field list/order
      }
      have_fields = true;
    } else if (keyword == "HEIGHT") {
      double value = 0;
      if (!(iss >> value) || !IsIntegerValued(value)) return Result::Failed;
      height = static_cast<int>(value);
    } else if (keyword == "POINTS") {
      double value = 0;
      if (!(iss >> value) || !IsIntegerValued(value) || value < 0) return Result::Failed;
      declared_count = static_cast<long long>(value);
      have_points = true;
    } else if (keyword == "DATA") {
      std::string mode;
      if (!(iss >> mode) || mode != "ascii") return Result::Failed;  // binary/binary_compressed: out of scope
      have_data = true;
      break;  // everything after DATA is the point payload, handled below
    }
    // SIZE/TYPE/COUNT/WIDTH/VIEWPOINT and anything else: tolerated, not
    // otherwise validated - see LoadPcd()'s own doc comment.
  }

  if (!have_version || !have_fields || !have_points || !have_data || height != 1) {
    return Result::Failed;
  }

  const int column_count = PcdColumnCount(fields);
  std::vector<std::array<double, 3>> positions;
  std::vector<ON_Color> colors;
  std::vector<std::array<double, 3>> normals;

  while (std::getline(in, line)) {
    std::istringstream iss(line);
    std::vector<double> values;
    double value = 0;
    while (iss >> value) values.push_back(value);
    if (values.empty()) continue;  // blank/whitespace-only line
    if (!iss.eof()) return Result::Failed;
    if (static_cast<int>(values.size()) != column_count) return Result::Failed;

    size_t next = 0;
    positions.push_back({values[next], values[next + 1], values[next + 2]});
    next += 3;
    if (fields == PcdFieldSet::kXyzRgb || fields == PcdFieldSet::kXyzRgbNormal) {
      colors.push_back(UnpackPcdRgb(values[next]));
      next += 1;
    }
    if (fields == PcdFieldSet::kXyzNormal || fields == PcdFieldSet::kXyzRgbNormal) {
      normals.push_back({values[next], values[next + 1], values[next + 2]});
      next += 3;
    }
  }
  if (static_cast<long long>(positions.size()) != declared_count) return Result::Failed;

  PointCloud cloud;
  for (const auto& p : positions) cloud.AppendPoint(Point3d(p[0], p[1], p[2]));
  if (!colors.empty()) cloud.SetColors(colors);
  if (!normals.empty()) {
    std::vector<Vector3d> normal_vectors;
    normal_vectors.reserve(normals.size());
    for (const auto& nrm : normals) normal_vectors.push_back(Vector3d(nrm[0], nrm[1], nrm[2]));
    cloud.SetNormals(normal_vectors);
  }
  out_cloud = std::move(cloud);
  return Result::Ok;
}

}  // namespace dino8::kernel
