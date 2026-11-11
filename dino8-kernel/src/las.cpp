#include "dino8/kernel/las.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace dino8::kernel {

namespace {

constexpr size_t kHeader12 = 227;

void PutU16(std::vector<unsigned char>& b, size_t at, uint16_t v) {
  b[at] = static_cast<unsigned char>(v & 0xFF);
  b[at + 1] = static_cast<unsigned char>(v >> 8);
}
void PutU32(std::vector<unsigned char>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
}
void PutF64(std::vector<unsigned char>& b, size_t at, double v) {
  uint64_t u;
  std::memcpy(&u, &v, 8);
  for (int i = 0; i < 8; ++i) b[at + i] = static_cast<unsigned char>((u >> (8 * i)) & 0xFF);
}
uint16_t GetU16(const std::vector<unsigned char>& b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }
uint32_t GetU32(const std::vector<unsigned char>& b, size_t at) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | b[at + static_cast<size_t>(i)];
  return v;
}
int32_t GetI32(const std::vector<unsigned char>& b, size_t at) { return static_cast<int32_t>(GetU32(b, at)); }
double GetF64(const std::vector<unsigned char>& b, size_t at) {
  uint64_t u = 0;
  for (int i = 7; i >= 0; --i) u = (u << 8) | b[at + static_cast<size_t>(i)];
  double v;
  std::memcpy(&v, &u, 8);
  return v;
}

size_t FormatLength(int fmt) {
  switch (fmt) {
    case 0: return 20;
    case 1: return 28;
    case 2: return 26;
    case 3: return 34;
    default: return 0;
  }
}

}  // namespace

Result SaveLas(const PointCloud& cloud, const std::string& path, const LasWriteOptions& options) {
  const int n = cloud.PointCount();
  if (n <= 0 || !std::isfinite(options.scale) || !(options.scale > 0.0)) return Result::Failed;
  double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
  for (int i = 0; i < n; ++i) {
    const Point3d p = cloud.PointAt(i);
    const double c[3] = {p.x, p.y, p.z};
    for (int a = 0; a < 3; ++a) {
      if (!std::isfinite(c[a])) return Result::Failed;
      lo[a] = std::min(lo[a], c[a]);
      hi[a] = std::max(hi[a], c[a]);
    }
  }
  for (int a = 0; a < 3; ++a) {
    if ((hi[a] - lo[a]) / options.scale > 2147483647.0) return Result::Failed;
  }
  const bool colors = options.write_colors && cloud.HasColors();
  const int fmt = colors ? 2 : 0;
  const size_t rec = FormatLength(fmt);
  std::vector<unsigned char> buf(kHeader12 + rec * static_cast<size_t>(n), 0);
  std::memcpy(buf.data(), "LASF", 4);
  buf[24] = 1;
  buf[25] = 2;
  std::memcpy(buf.data() + 26, "dino8", 5);          // system identifier
  std::memcpy(buf.data() + 58, "dino8-kernel", 12);  // generating software
  PutU16(buf, 94, static_cast<uint16_t>(kHeader12));
  PutU32(buf, 96, static_cast<uint32_t>(kHeader12));
  buf[104] = static_cast<unsigned char>(fmt);
  PutU16(buf, 105, static_cast<uint16_t>(rec));
  PutU32(buf, 107, static_cast<uint32_t>(n));
  PutU32(buf, 111, static_cast<uint32_t>(n));  // points by return 1
  for (int a = 0; a < 3; ++a) {
    PutF64(buf, 131 + 8 * static_cast<size_t>(a), options.scale);
    PutF64(buf, 155 + 8 * static_cast<size_t>(a), lo[a]);
  }
  double dmin[3] = {1e300, 1e300, 1e300}, dmax[3] = {-1e300, -1e300, -1e300};
  for (int i = 0; i < n; ++i) {
    const Point3d p = cloud.PointAt(i);
    const double c[3] = {p.x, p.y, p.z};
    unsigned char* r = buf.data() + kHeader12 + rec * static_cast<size_t>(i);
    for (int a = 0; a < 3; ++a) {
      const int32_t q = static_cast<int32_t>(std::llround((c[a] - lo[a]) / options.scale));
      const double decoded = q * options.scale + lo[a];
      dmin[a] = std::min(dmin[a], decoded);
      dmax[a] = std::max(dmax[a], decoded);
      PutU32(buf, static_cast<size_t>(r - buf.data()) + 4 * static_cast<size_t>(a), static_cast<uint32_t>(q));
    }
    r[14] = static_cast<unsigned char>(1 | (1 << 3));
    if (colors) {
      const ON_Color col = cloud.ColorAt(i);
      PutU16(buf, static_cast<size_t>(r - buf.data()) + 20, static_cast<uint16_t>(col.Red() * 257));
      PutU16(buf, static_cast<size_t>(r - buf.data()) + 22, static_cast<uint16_t>(col.Green() * 257));
      PutU16(buf, static_cast<size_t>(r - buf.data()) + 24, static_cast<uint16_t>(col.Blue() * 257));
    }
  }
  // Header extents: max X, min X, max Y, min Y, max Z, min Z.
  for (int a = 0; a < 3; ++a) {
    PutF64(buf, 179 + 16 * static_cast<size_t>(a), dmax[a]);
    PutF64(buf, 187 + 16 * static_cast<size_t>(a), dmin[a]);
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) return Result::Failed;
  out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
  return out ? Result::Ok : Result::Failed;
}

Result LoadLas(const std::string& path, PointCloud& out_cloud, LasReport* report) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Result::Failed;
  std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (b.size() < kHeader12 || std::memcmp(b.data(), "LASF", 4) != 0) return Result::Failed;
  const int major = b[24], minor = b[25];
  if (major != 1 || minor > 3) return Result::Failed;
  const size_t header_size = GetU16(b, 94);
  const size_t min_header = minor == 3 ? 235 : 227;
  if (header_size < min_header || header_size > b.size()) return Result::Failed;
  const size_t data_offset = GetU32(b, 96);
  const int fmt_raw = b[104];
  if (fmt_raw > 3) return Result::Failed;  // includes the LAZ-compressed flag bits and formats 4+
  const int fmt = fmt_raw;
  const size_t rec = GetU16(b, 105);
  const size_t count = GetU32(b, 107);
  if (rec < FormatLength(fmt) || data_offset < header_size || data_offset > b.size()) return Result::Failed;
  if (count > 0 && (b.size() - data_offset) / rec < count) return Result::Failed;  // truncated
  double scale[3], offset[3];
  for (int a = 0; a < 3; ++a) {
    scale[a] = GetF64(b, 131 + 8 * static_cast<size_t>(a));
    offset[a] = GetF64(b, 155 + 8 * static_cast<size_t>(a));
    if (!std::isfinite(scale[a]) || !(scale[a] > 0.0) || !std::isfinite(offset[a])) return Result::Failed;
  }
  const bool colors = fmt == 2 || fmt == 3;
  const size_t color_at = fmt == 2 ? 20 : 28;
  PointCloud cloud;
  std::vector<ON_Color> cols;
  for (size_t i = 0; i < count; ++i) {
    const size_t r = data_offset + rec * i;
    cloud.AppendPoint(Point3d(GetI32(b, r) * scale[0] + offset[0], GetI32(b, r + 4) * scale[1] + offset[1],
                               GetI32(b, r + 8) * scale[2] + offset[2]));
    if (colors) {
      auto c8 = [&](size_t at) { return static_cast<int>((GetU16(b, at) + 128u) / 257u); };
      cols.push_back(ON_Color(c8(r + color_at), c8(r + color_at + 2), c8(r + color_at + 4)));
    }
  }
  if (colors && count > 0) cloud.SetColors(cols);
  if (report) {
    report->version_major = major;
    report->version_minor = minor;
    report->point_format = fmt;
    for (int a = 0; a < 3; ++a) { report->scale[a] = scale[a]; report->offset[a] = offset[a]; }
    report->has_colors = colors;
  }
  out_cloud = std::move(cloud);
  return Result::Ok;
}

}  // namespace dino8::kernel
