#include "render/PngWrite.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <zlib.h>

namespace dino8::app {

namespace {

void PutU32Be(std::string& s, uint32_t v) {
  for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

void AppendChunk(std::string& out, const char type[5], const std::string& data) {
  PutU32Be(out, static_cast<uint32_t>(data.size()));
  const size_t type_at = out.size();
  out.append(type, 4);
  out += data;
  const uLong crc = crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(out.data() + type_at), static_cast<uInt>(4 + data.size()));
  PutU32Be(out, static_cast<uint32_t>(crc));
}

}  // namespace

bool WritePngRgb(const std::string& path, int width, int height, const std::vector<unsigned char>& rgb, int dpi, std::string& error) {
  if (width <= 0 || height <= 0 || rgb.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 3) {
    error = "Nothing to save";
    return false;
  }
  // Scanlines, each preceded by its filter type byte (0 = None).
  const size_t row_bytes = static_cast<size_t>(width) * 3;
  std::string raw;
  raw.reserve((row_bytes + 1) * static_cast<size_t>(height));
  for (int y = 0; y < height; ++y) {
    raw.push_back('\0');
    raw.append(reinterpret_cast<const char*>(rgb.data()) + static_cast<size_t>(y) * row_bytes, row_bytes);
  }
  uLongf packed_size = compressBound(static_cast<uLong>(raw.size()));
  std::string packed(packed_size, '\0');
  if (compress2(reinterpret_cast<Bytef*>(packed.data()), &packed_size, reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), 6) != Z_OK) {
    error = "PNG compression failed";
    return false;
  }
  packed.resize(packed_size);

  std::string out("\x89PNG\r\n\x1a\n", 8);
  std::string ihdr;
  PutU32Be(ihdr, static_cast<uint32_t>(width));
  PutU32Be(ihdr, static_cast<uint32_t>(height));
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // colour type: truecolour RGB
  ihdr.push_back(0);  // compression: deflate
  ihdr.push_back(0);  // filter method
  ihdr.push_back(0);  // interlace: none
  AppendChunk(out, "IHDR", ihdr);
  if (dpi > 0) {
    const uint32_t ppm = static_cast<uint32_t>(std::lround(dpi / 0.0254));
    std::string phys;
    PutU32Be(phys, ppm);
    PutU32Be(phys, ppm);
    phys.push_back(1);  // unit: metre
    AppendChunk(out, "pHYs", phys);
  }
  AppendChunk(out, "IDAT", packed);
  AppendChunk(out, "IEND", std::string());

  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    error = "Cannot write " + path;
    return false;
  }
  const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
  std::fclose(f);
  if (!ok) error = "Could not write " + path;
  return ok;
}

}  // namespace dino8::app
