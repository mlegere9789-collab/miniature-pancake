// Tiny dependency-free image codecs for textures and render output:
// reads PPM (P3/P6), BMP (24/32-bit uncompressed), PNG (8/16-bit,
// non-interlaced, via a small zlib inflate) and Radiance HDR/RGBE
// (8-bit-per-channel-with-shared-exponent, true unclamped radiance);
// writes BMP, PPM, PNG and HDR.
#pragma once

#include <string>
#include <vector>

#include "util/Inflate.h"

namespace dino8::app {

struct Image {
  int width = 0, height = 0;
  std::vector<unsigned char> rgba;  // top-down rows, 4 bytes per pixel
  bool Valid() const { return width > 0 && height > 0 && rgba.size() == static_cast<size_t>(width) * height * 4; }
};

// A true HDR image: linear radiance per channel, not clamped to [0,1] the
// way `Image` above is. Loaded from / saved to Radiance's .hdr (RGBE) file
// format (used for environment/IBL panoramas — see LoadImageHdr).
struct ImageHdr {
  int width = 0, height = 0;
  std::vector<float> rgb;  // top-down rows, 3 floats per pixel, linear
  bool Valid() const { return width > 0 && height > 0 && rgb.size() == static_cast<size_t>(width) * height * 3; }
};

// Loads a .ppm / .pgm / .bmp / .png / .hdr file. Returns false and sets
// `error` on failure (unknown format, unsupported variant, corrupt data).
// A loaded .hdr is tone-mapped (Reinhard + gamma 2.2) into `out.rgba`, so
// every existing 8-bit consumer (thumbnails, GUI previews, the texture
// atlases) keeps working unchanged; a consumer that wants the real,
// unclamped radiance values (environment lighting) should call
// LoadImageHdr directly instead.
bool LoadImageFile(const std::string& path, Image& out, std::string& error);

// Loads a Radiance .hdr (RGBE) file: the classic "#?RADIANCE" flat-ASCII
// header followed by either flat or new-style per-scanline RLE-compressed
// scanlines of 4-byte (R,G,B,E) shared-exponent pixels. Supports both
// encodings (a real-world .hdr, e.g. one downloaded from an HDRI site, is
// virtually always new-style RLE; SaveImageHdr below writes the simpler
// flat encoding). Only the standard top-down, left-to-right orientation
// ("-Y H +X W") is supported.
bool LoadImageHdr(const std::string& path, ImageHdr& out, std::string& error);

// Writes a linear-radiance RGB buffer (top-down rows, 3 floats per pixel,
// any non-negative range — not clamped to [0,1]) as a flat-encoded
// Radiance .hdr file.
bool SaveImageHdr(const std::string& path, int width, int height, const std::vector<float>& rgb, std::string& error);

// Writes an RGB buffer (top-down rows, 3 bytes per pixel) as a 24-bit BMP
// or binary PPM depending on the extension (.ppm -> PPM, anything else BMP).
bool SaveImageRGB(const std::string& path, int width, int height, const std::vector<unsigned char>& rgb,
                  std::string& error);

// Inflates a zlib stream (RFC 1950/1951). Exposed for tests; used by PNG.
// `max_output` bounds the decompressed size exactly like
// util::InflateRaw's own parameter of the same name (defaulting to
// util::kDefaultMaxInflateOutput) - LoadPng() passes the image's own
// declared, pre-decompression raw-scanline size here rather than letting
// a tiny-dimensioned file's IDAT stream decompress far past what its own
// width/height could ever need (see util/Inflate.h's doc comment on why
// the generic default alone isn't enough once a caller knows its own
// expected size).
bool ZlibInflate(const unsigned char* data, size_t size, std::vector<unsigned char>& out, std::string& error,
                  size_t max_output = dino8::util::kDefaultMaxInflateOutput);

// Encodes an RGB buffer (top-down rows, 3 bytes per pixel) as an in-memory
// 8-bit truecolor PNG (RFC 2083): IHDR/IDAT/IEND chunks, filter type None
// per scanline, and a real (if uncompressed - "stored" deflate blocks, RFC
// 1951 section 3.2.4) zlib stream, so the bytes this produces are a fully
// spec-conformant PNG any decoder (including LoadImageFile above) can read
// back byte-for-byte, just not a maximally compressed one. Used by the
// clipboard-image commands (ViewCaptureToClipboard, ScreenCaptureToClipboard,
// CopyRenderWindowToClipboard) to hand the OS clipboard a real image/png
// payload without a third-party PNG library.
bool EncodePng(int width, int height, const std::vector<unsigned char>& rgb, std::vector<unsigned char>& out,
                std::string& error);

// Encodes an RGB buffer (top-down rows, 3 bytes per pixel) as an in-memory
// 24-bit BMP (the same bytes SaveImageRGB would write to a .bmp file).
// Used alongside EncodePng to offer the OS clipboard an image/bmp target
// too, for whichever paste target the requesting app prefers.
bool EncodeBmp(int width, int height, const std::vector<unsigned char>& rgb, std::vector<unsigned char>& out,
                std::string& error);

}  // namespace dino8::app
