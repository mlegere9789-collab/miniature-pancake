// ASPRS LAS read/write. Oracles are independent of src/las.cpp: (1) a byte-level decoder written here straight from the public
// LAS 1.2 header / point-record offsets reads what SaveLas() wrote; (2) LAS files are BUILT here byte by byte (integer X/Y/Z,
// scale, offset, 16-bit RGB) and the expected coordinates are computed here from those integers; (3) round trips are held to
// the quantisation bound scale / 2.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "dino8/kernel/las.h"
#include "dino8/kernel/point_cloud.h"
#include "test_registry.h"

namespace {

using dino8::kernel::LasReport;
using dino8::kernel::LasWriteOptions;
using dino8::kernel::LoadLas;
using dino8::kernel::Point3d;
using dino8::kernel::PointCloud;
using dino8::kernel::Result;
using dino8::kernel::SaveLas;

using Bytes = std::vector<unsigned char>;

Bytes ReadAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
void WriteAll(const std::string& path, const Bytes& b) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}
uint64_t Le(const Bytes& b, size_t at, int bytes) {
  uint64_t v = 0;
  for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | b[at + static_cast<size_t>(i)];
  return v;
}
double F64(const Bytes& b, size_t at) {
  const uint64_t u = Le(b, at, 8);
  double d;
  std::memcpy(&d, &u, 8);
  return d;
}
void PutLe(Bytes& b, size_t at, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) b[at + static_cast<size_t>(i)] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
}
void PutF64(Bytes& b, size_t at, double d) {
  uint64_t u;
  std::memcpy(&u, &d, 8);
  PutLe(b, at, u, 8);
}

struct FixturePoint { int32_t x, y, z; uint16_t r, g, bl; };

// Builds a LAS file: `minor` = 0..3, `fmt` = 0..3, `vlr_bytes` junk bytes between the header and the point data, `extra` bytes
// appended to every record.
Bytes BuildLas(int minor, int fmt, const std::vector<FixturePoint>& pts, double sx, double sy, double sz, double ox, double oy, double oz,
               size_t vlr_bytes = 0, size_t extra = 0) {
  const size_t header = minor == 3 ? 235 : 227;
  const size_t base_len[] = {20, 28, 26, 34};
  const size_t rec = base_len[fmt] + extra;
  Bytes b(header + vlr_bytes + rec * pts.size(), 0xEE);
  std::memset(b.data(), 0, header);
  std::memcpy(b.data(), "LASF", 4);
  b[24] = 1;
  b[25] = static_cast<unsigned char>(minor);
  PutLe(b, 94, header, 2);
  PutLe(b, 96, header + vlr_bytes, 4);
  PutLe(b, 100, vlr_bytes ? 1 : 0, 4);
  b[104] = static_cast<unsigned char>(fmt);
  PutLe(b, 105, rec, 2);
  PutLe(b, 107, pts.size(), 4);
  PutF64(b, 131, sx); PutF64(b, 139, sy); PutF64(b, 147, sz);
  PutF64(b, 155, ox); PutF64(b, 163, oy); PutF64(b, 171, oz);
  size_t at = header + vlr_bytes;
  for (const FixturePoint& p : pts) {
    std::memset(&b[at], 0, rec);
    PutLe(b, at + 0, static_cast<uint32_t>(p.x), 4);
    PutLe(b, at + 4, static_cast<uint32_t>(p.y), 4);
    PutLe(b, at + 8, static_cast<uint32_t>(p.z), 4);
    PutLe(b, at + 12, 777, 2);  // intensity (must be ignored)
    b[at + 15] = 2;             // classification (ignored)
    if (fmt == 1) PutF64(b, at + 20, 123456.789);
    if (fmt == 2) { PutLe(b, at + 20, p.r, 2); PutLe(b, at + 22, p.g, 2); PutLe(b, at + 24, p.bl, 2); }
    if (fmt == 3) { PutF64(b, at + 20, 1.5); PutLe(b, at + 28, p.r, 2); PutLe(b, at + 30, p.g, 2); PutLe(b, at + 32, p.bl, 2); }
    at += rec;
  }
  return b;
}

double Lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0; }

const char* kPath = "dino8_kernel_las_test.las";

}  // namespace

D8_EXTRA_TEST(LasWriterBytesMatchThePublishedHeaderAndRecordLayout) {
  PointCloud cloud;
  const double xyz[5][3] = {{10.0, -4.5, 100.25}, {10.5, -4.0, 100.0}, {12.125, -3.5, 101.0}, {11.0, -6.0, 99.5}, {10.75, -5.25, 100.75}};
  std::vector<ON_Color> cols;
  for (int i = 0; i < 5; ++i) { cloud.AppendPoint(Point3d(xyz[i][0], xyz[i][1], xyz[i][2])); cols.push_back(ON_Color(i * 60, 255 - i * 50, (i * 37) % 256)); }
  cloud.SetColors(cols);
  LasWriteOptions opt;
  opt.scale = 0.01;
  d8test::Check(SaveLas(cloud, kPath, opt) == Result::Ok, "SaveLas succeeds");
  const Bytes b = ReadAll(kPath);
  std::remove(kPath);
  d8test::Check(b.size() == 227 + 5 * 26, "file size = 227-byte header + 5 records of 26 bytes (point format 2)");
  d8test::Check(std::memcmp(b.data(), "LASF", 4) == 0, "file signature LASF");
  d8test::Check(b[24] == 1 && b[25] == 2, "version 1.2");
  d8test::Check(Le(b, 94, 2) == 227 && Le(b, 96, 4) == 227 && Le(b, 100, 4) == 0, "header size 227, point data offset 227, no VLRs");
  d8test::Check(b[104] == 2 && Le(b, 105, 2) == 26 && Le(b, 107, 4) == 5, "point format 2, record length 26, 5 points");
  d8test::Check(F64(b, 131) == 0.01 && F64(b, 139) == 0.01 && F64(b, 147) == 0.01, "scale factors 0.01 on all axes");
  double mn[3] = {1e9, 1e9, 1e9}, mx[3] = {-1e9, -1e9, -1e9};
  for (int i = 0; i < 5; ++i) for (int a = 0; a < 3; ++a) { mn[a] = std::min(mn[a], xyz[i][a]); mx[a] = std::max(mx[a], xyz[i][a]); }
  const double ox = F64(b, 155), oy = F64(b, 163), oz = F64(b, 171);
  // Offsets are the axis minima (a documented choice of the writer); the decoded values must be the data to within scale / 2.
  d8test::Check(std::fabs(ox - mn[0]) < 1e-12 && std::fabs(oy - mn[1]) < 1e-12 && std::fabs(oz - mn[2]) < 1e-12, "offsets are the axis minima");
  const double hdr_ext[6] = {F64(b, 179), F64(b, 187), F64(b, 195), F64(b, 203), F64(b, 211), F64(b, 219)};  // maxX minX maxY minY maxZ minZ
  d8test::Check(std::fabs(hdr_ext[0] - mx[0]) <= 0.005 && std::fabs(hdr_ext[1] - mn[0]) <= 0.005 && std::fabs(hdr_ext[2] - mx[1]) <= 0.005 &&
                    std::fabs(hdr_ext[3] - mn[1]) <= 0.005 && std::fabs(hdr_ext[4] - mx[2]) <= 0.005 && std::fabs(hdr_ext[5] - mn[2]) <= 0.005,
                "header max/min extents match the data to within scale / 2");
  double worst = 0.0;
  bool rgb_ok = true;
  for (int i = 0; i < 5; ++i) {
    const size_t at = 227 + 26 * static_cast<size_t>(i);
    const double x = static_cast<int32_t>(Le(b, at, 4)) * 0.01 + ox, y = static_cast<int32_t>(Le(b, at + 4, 4)) * 0.01 + oy,
                 z = static_cast<int32_t>(Le(b, at + 8, 4)) * 0.01 + oz;
    worst = std::max({worst, std::fabs(x - xyz[i][0]), std::fabs(y - xyz[i][1]), std::fabs(z - xyz[i][2])});
    rgb_ok = rgb_ok && Le(b, at + 20, 2) == static_cast<uint64_t>(cols[static_cast<size_t>(i)].Red()) * 257 &&
             Le(b, at + 22, 2) == static_cast<uint64_t>(cols[static_cast<size_t>(i)].Green()) * 257 &&
             Le(b, at + 24, 2) == static_cast<uint64_t>(cols[static_cast<size_t>(i)].Blue()) * 257;
  }
  d8test::Check(worst <= 0.005 + 1e-12, ("decoded record coordinates within scale / 2 = 0.005 (worst " + std::to_string(worst) + ")").c_str());
  d8test::Check(rgb_ok, "16-bit RGB at record offsets 20/22/24 equal the 8-bit colour times 257");
}

D8_EXTRA_TEST(LasReaderDecodesHandBuiltFilesOfEveryVersionAndFormat) {
  const std::vector<FixturePoint> pts = {{0, 0, 0, 0, 0, 0}, {1000, -2000, 3000, 65535, 32768, 257 * 77}, {-2147483647, 2147483647, 5, 257 * 200, 1, 65534}, {12345, 6789, -1, 100, 50000, 60000}};
  const double sx = 0.005, sy = 0.002, sz = 0.25, ox = 1000.5, oy = -2000.25, oz = 12.0;
  struct Case { int minor, fmt; size_t vlr, extra; };
  const Case cases[] = {{0, 0, 0, 0}, {1, 1, 0, 0}, {2, 2, 0, 0}, {2, 3, 0, 0}, {3, 0, 0, 0}, {3, 3, 0, 0}, {2, 0, 120, 0}, {2, 2, 54 + 17, 0}, {2, 0, 0, 6}, {2, 3, 0, 9}};
  for (const Case& cs : cases) {
    const std::string tag = "LAS 1." + std::to_string(cs.minor) + " format " + std::to_string(cs.fmt) + " vlr " + std::to_string(cs.vlr) + " extra " + std::to_string(cs.extra) + ": ";
    WriteAll(kPath, BuildLas(cs.minor, cs.fmt, pts, sx, sy, sz, ox, oy, oz, cs.vlr, cs.extra));
    PointCloud cloud;
    LasReport rep;
    const Result r = LoadLas(kPath, cloud, &rep);
    d8test::Check(r == Result::Ok, (tag + "reads").c_str());
    if (r != Result::Ok) continue;
    d8test::Check(cloud.PointCount() == 4 && rep.version_minor == cs.minor && rep.point_format == cs.fmt, (tag + "count / version / format").c_str());
    double worst = 0.0;
    for (int i = 0; i < 4; ++i) {
      const Point3d e(pts[static_cast<size_t>(i)].x * sx + ox, pts[static_cast<size_t>(i)].y * sy + oy, pts[static_cast<size_t>(i)].z * sz + oz);
      worst = std::max(worst, (cloud.PointAt(i) - e).Length());
    }
    d8test::Check(worst < 1e-9, (tag + "every coordinate = integer * scale + offset (worst " + std::to_string(worst) + ")").c_str());
    const bool want_colors = cs.fmt == 2 || cs.fmt == 3;
    d8test::Check(cloud.HasColors() == want_colors && rep.has_colors == want_colors, (tag + "colours present exactly for formats 2 and 3").c_str());
    if (want_colors && cloud.HasColors()) {
      // 16-bit -> nearest 8-bit: 65535 -> 255, 32768 -> 128, 257*77 -> 77, 0 -> 0, 257*200 -> 200, 1 -> 0, 65534 -> 255, 100 -> 0, 50000 -> 195, 60000 -> 233.
      const int expect[4][3] = {{0, 0, 0}, {255, 128, 77}, {200, 0, 255}, {0, 195, 233}};
      bool ok = true;
      for (int i = 0; i < 4; ++i) {
        ok = ok && static_cast<int>(cloud.ColorAt(i).Red()) == expect[i][0] && static_cast<int>(cloud.ColorAt(i).Green()) == expect[i][1] &&
             static_cast<int>(cloud.ColorAt(i).Blue()) == expect[i][2];
      }
      d8test::Check(ok, (tag + "16-bit RGB decodes to the nearest 8-bit value").c_str());
    }
  }
  std::remove(kPath);
}

D8_EXTRA_TEST(LasRoundTripStaysWithinHalfAScaleStepAndKeepsEveryColour) {
  uint32_t seed = 12345;
  for (double scale : {1.0, 0.01, 0.001, 1e-4}) {
    PointCloud cloud;
    std::vector<ON_Color> cols;
    for (int i = 0; i < 2000; ++i) {
      cloud.AppendPoint(Point3d(500.0 * Lcg(seed) - 250.0 + 1e5, 80.0 * Lcg(seed) - 40.0, 12.0 * Lcg(seed)));
      cols.push_back(ON_Color(i % 256, (255 - i) & 255, (i * 7) % 256));
    }
    cloud.SetColors(cols);
    LasWriteOptions opt;
    opt.scale = scale;
    const std::string tag = "scale " + std::to_string(scale) + ": ";
    d8test::Check(SaveLas(cloud, kPath, opt) == Result::Ok, (tag + "saves").c_str());
    PointCloud back;
    LasReport rep;
    d8test::Check(LoadLas(kPath, back, &rep) == Result::Ok, (tag + "loads").c_str());
    d8test::Check(back.PointCount() == 2000 && rep.point_format == 2 && rep.version_major == 1 && rep.version_minor == 2, (tag + "2000 points, version 1.2, format 2").c_str());
    double worst = 0.0;
    bool colours = back.HasColors();
    for (int i = 0; i < back.PointCount() && i < 2000; ++i) {
      const Point3d a = cloud.PointAt(i), b = back.PointAt(i);
      worst = std::max({worst, std::fabs(a.x - b.x), std::fabs(a.y - b.y), std::fabs(a.z - b.z)});
      if (colours) colours = back.ColorAt(i) == cloud.ColorAt(i);
    }
    d8test::Check(worst <= scale / 2 + 1e-9, (tag + "worst coordinate error " + std::to_string(worst) + " <= scale / 2").c_str());
    d8test::Check(colours, (tag + "all 2000 colours identical").c_str());
  }
  std::remove(kPath);
}

D8_EXTRA_TEST(LasWriterOmitsColoursOnRequestAndNeverWritesNormals) {
  PointCloud cloud;
  cloud.AppendPoint(Point3d(1, 2, 3));
  cloud.AppendPoint(Point3d(4, 5, 6));
  cloud.SetColors({ON_Color(1, 2, 3), ON_Color(4, 5, 6)});
  LasWriteOptions opt;
  opt.write_colors = false;
  d8test::Check(SaveLas(cloud, kPath, opt) == Result::Ok, "colourless save succeeds");
  const Bytes b = ReadAll(kPath);
  d8test::Check(b[104] == 0 && Le(b, 105, 2) == 20 && b.size() == 227 + 2 * 20, "point format 0, 20-byte records");
  PointCloud back;
  d8test::Check(LoadLas(kPath, back) == Result::Ok && !back.HasColors() && !back.HasNormals() && back.PointCount() == 2, "reads back without colours or normals");
  std::remove(kPath);
  // A single point round trips exactly on the offset.
  PointCloud one;
  one.AppendPoint(Point3d(-7.123456789, 0.5, 1e6));
  d8test::Check(SaveLas(one, kPath) == Result::Ok, "single point saves");
  PointCloud one_back;
  d8test::Check(LoadLas(kPath, one_back) == Result::Ok && one_back.PointCount() == 1 && (one_back.PointAt(0) - one.PointAt(0)).Length() < 1e-12, "a single point is exact (it is its own offset)");
  std::remove(kPath);
}

D8_EXTRA_TEST(LasRefusesBadFilesAndBadInputWithoutTouchingTheOutput) {
  const std::vector<FixturePoint> pts = {{1, 2, 3, 0, 0, 0}, {4, 5, 6, 0, 0, 0}, {7, 8, 9, 0, 0, 0}};
  const Bytes good = BuildLas(2, 0, pts, 0.01, 0.01, 0.01, 0, 0, 0);
  auto refuses = [&](Bytes b, const char* what) {
    WriteAll(kPath, b);
    PointCloud c;
    c.AppendPoint(Point3d(9, 9, 9));
    const Result r = LoadLas(kPath, c);
    d8test::Check(r == Result::Failed, (std::string(what) + ": Result::Failed").c_str());
    d8test::Check(c.PointCount() == 1 && c.PointAt(0).x == 9, (std::string(what) + ": output untouched").c_str());
  };
  { WriteAll(kPath, good); PointCloud c; d8test::Check(LoadLas(kPath, c) == Result::Ok && c.PointCount() == 3, "the unmodified fixture loads (control)"); }
  Bytes b = good; b[0] = 'X';             refuses(b, "bad signature");
  b = good; b[25] = 4;                    refuses(b, "version 1.4");
  b = good; b[24] = 2;                    refuses(b, "version 2.x");
  b = good; b[104] = 6;                   refuses(b, "point format 6");
  b = good; b[104] = 0x80 | 0;            refuses(b, "LAZ-compressed point format flag");
  b = good; PutF64(b, 131, 0.0);          refuses(b, "zero scale");
  b = good; PutF64(b, 139, -0.01);        refuses(b, "negative scale");
  b = good; PutF64(b, 147, std::nan("")); refuses(b, "NaN scale");
  b = good; PutF64(b, 155, INFINITY);     refuses(b, "infinite offset");
  b = good; b.pop_back();                 refuses(b, "truncated by one byte");
  b = good; b.resize(100);                refuses(b, "shorter than the header");
  b = good; PutLe(b, 105, 12, 2);         refuses(b, "record length shorter than the format");
  b = good; PutLe(b, 96, 100000, 4);      refuses(b, "point data offset beyond the file");
  b = good; PutLe(b, 96, 100, 4);         refuses(b, "point data offset inside the header");
  b = good; PutLe(b, 94, 100, 2);         refuses(b, "header size below the 1.2 minimum");
  b = good; PutLe(b, 107, 4, 4);          refuses(b, "point count larger than the data");
  {
    PointCloud c;
    c.AppendPoint(Point3d(9, 9, 9));
    d8test::Check(LoadLas("dino8_kernel_las_does_not_exist.las", c) == Result::Failed && c.PointCount() == 1, "missing file refused, output untouched");
  }
  std::remove(kPath);
  // Writer refusals.
  PointCloud empty, nan_cloud, huge, ok;
  nan_cloud.AppendPoint(Point3d(0, 0, 0));
  nan_cloud.AppendPoint(Point3d(std::nan(""), 0, 0));
  huge.AppendPoint(Point3d(0, 0, 0));
  huge.AppendPoint(Point3d(1e7, 0, 0));
  ok.AppendPoint(Point3d(0, 0, 0));
  ok.AppendPoint(Point3d(1, 1, 1));
  LasWriteOptions tiny; tiny.scale = 1e-9;
  LasWriteOptions zero; zero.scale = 0.0;
  LasWriteOptions neg; neg.scale = -1.0;
  LasWriteOptions nan_scale; nan_scale.scale = std::nan("");
  d8test::Check(SaveLas(empty, kPath) == Result::Failed, "empty cloud refused");
  d8test::Check(SaveLas(nan_cloud, kPath) == Result::Failed, "non-finite coordinate refused");
  d8test::Check(SaveLas(huge, kPath, tiny) == Result::Failed, "a range that overflows 32-bit integers at this scale is refused");
  d8test::Check(SaveLas(ok, kPath, zero) == Result::Failed && SaveLas(ok, kPath, neg) == Result::Failed && SaveLas(ok, kPath, nan_scale) == Result::Failed, "zero / negative / NaN scale refused");
  d8test::Check(SaveLas(ok, "dino8_kernel_no_such_dir/x.las") == Result::Failed, "unwritable path refused");
  std::remove(kPath);
}
