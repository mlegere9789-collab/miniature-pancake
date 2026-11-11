#pragma once

#include <string>

#include "dino8/kernel/point_cloud.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// ASPRS LAS point-cloud files (the public LAS 1.2 specification: a fixed 227-byte little-endian header, integer X/Y/Z records
// quantised by a per-axis scale and offset). Written clean from the public header and point-record layouts; no LAS library
// is linked. Scope, stated up front:
//   write: LAS 1.2, point data format 0 (xyz) or 2 (xyz + RGB, written when the cloud has colours and `write_colors`);
//   read : LAS 1.0 - 1.3, point data formats 0, 1, 2 and 3 (xyz, optional GPS time ignored, optional RGB);
//   not handled: LAS 1.4 / point formats 4-10, compressed LAZ (the point-format high bits), variable-length-record content
//   (VLRs are skipped by header offset, never interpreted), extra bytes, classification / intensity / return data (read and
//   dropped), normals (LAS has no normal channel, so a cloud's normals are not written and a read cloud has none).
// RGB is 16 bits in the file: it is written as 8-bit value * 257 (so 0 -> 0, 255 -> 65535) and read back as the nearest
// 8-bit value (v + 128) / 257, which inverts the write exactly; a foreign file that fills only the low byte of each channel
// therefore reads as near-black, which is that file's convention problem, not detectable here.

struct LasWriteOptions {
  // Quantisation step for all three axes (metres, or whatever unit the cloud is in). Coordinates are stored as
  // round((x - offset) / scale) with offset = the minimum of each axis, so the worst round-trip error per axis is scale / 2.
  double scale = 0.001;
  bool write_colors = true;
};

struct LasReport {
  int version_major = 0;
  int version_minor = 0;
  int point_format = 0;
  double scale[3] = {0, 0, 0};
  double offset[3] = {0, 0, 0};
  bool has_colors = false;
};

// Writes `cloud` to `path`. Result::Failed (no file promised) for an empty cloud, a non-finite coordinate, a non-finite or
// non-positive scale, a coordinate range that does not fit 32-bit integers at that scale, or a file that cannot be opened.
Result SaveLas(const PointCloud& cloud, const std::string& path, const LasWriteOptions& options = {});

// Reads `path` into `out_cloud` (left unchanged on failure). Result::Failed for: a missing file, a bad "LASF" signature,
// an unsupported version (> 1.3) or point format (> 3, or a LAZ-compressed flag), a header smaller than the version's
// minimum, a zero / non-finite / negative scale, a record length shorter than the format's own, an offset-to-point-data that
// lies outside the file, or a file shorter than offset + point count * record length (truncated).
Result LoadLas(const std::string& path, PointCloud& out_cloud, LasReport* report = nullptr);

}  // namespace dino8::kernel
