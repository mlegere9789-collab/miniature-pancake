// Tiny dependency-free image codecs for textures and render output:
// reads PPM (P3/P6), BMP (24/32-bit uncompressed) and PNG (8/16-bit,
// non-interlaced, via a small zlib inflate); writes BMP and PPM.
#pragma once

#include <string>
#include <vector>

namespace dino8::app {

struct Image {
  int width = 0, height = 0;
  std::vector<unsigned char> rgba;  // top-down rows, 4 bytes per pixel
  bool Valid() const { return width > 0 && height > 0 && rgba.size() == static_cast<size_t>(width) * height * 4; }
};

// Loads a .ppm / .pgm / .bmp / .png file. Returns false and sets `error`
// on failure (unknown format, unsupported variant, corrupt data).
bool LoadImageFile(const std::string& path, Image& out, std::string& error);

// Writes an RGB buffer (top-down rows, 3 bytes per pixel) as a 24-bit BMP
// or binary PPM depending on the extension (.ppm -> PPM, anything else BMP).
bool SaveImageRGB(const std::string& path, int width, int height, const std::vector<unsigned char>& rgb,
                  std::string& error);

// Inflates a zlib stream (RFC 1950/1951). Exposed for tests; used by PNG.
bool ZlibInflate(const unsigned char* data, size_t size, std::vector<unsigned char>& out, std::string& error);

}  // namespace dino8::app
