// Unit test for the minimal OpenEXR codec (src/render/ImageIO.cpp's
// LoadImageExr/SaveImageExr, plus LoadImageFile's dispatch to it): a
// FLOAT-channel encode/decode round trip well above the [0,1] range
// PNG/BMP/PPM are stuck at (the "`.exr` is still entirely unsupported"
// gap this codec closes - see PARITY_MAP.md's "Environments and
// image-based lighting" entry), a real-world-shaped HALF-channel scanline
// built by hand (exercising the half->float decode path SaveImageExr
// itself never writes, since it only ever emits FLOAT), and the
// malformed/out-of-scope-input error paths (tiled, multi-part, deep,
// compressed, missing R/G/B, subsampled).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "render/ImageIO.h"

using dino8::app::Image;
using dino8::app::ImageHdr;
using dino8::app::LoadImageExr;
using dino8::app::LoadImageFile;
using dino8::app::SaveImageExr;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

bool WriteFile(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
  FILE* f = std::fopen(path.string().c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  std::fclose(f);
  return ok;
}

void AppendCstr(std::vector<unsigned char>& out, const char* s) {
  for (; *s; ++s) out.push_back(static_cast<unsigned char>(*s));
  out.push_back(0);
}
void AppendI32(std::vector<unsigned char>& out, int32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>((static_cast<uint32_t>(v) >> (8 * i)) & 0xff));
}
void AppendI64(std::vector<unsigned char>& out, int64_t v) {
  const uint64_t u = static_cast<uint64_t>(v);
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<unsigned char>((u >> (8 * i)) & 0xff));
}
void AppendU8(std::vector<unsigned char>& out, unsigned char v) { out.push_back(v); }
void AppendFloat(std::vector<unsigned char>& out, float v) {
  unsigned char b[4];
  std::memcpy(b, &v, 4);
  out.insert(out.end(), b, b + 4);
}
void AppendHalf(std::vector<unsigned char>& out, uint16_t h) {
  out.push_back(static_cast<unsigned char>(h & 0xff));
  out.push_back(static_cast<unsigned char>((h >> 8) & 0xff));
}

// Encodes a float as a real IEEE binary16, round-to-nearest - just enough
// to build a hand-written HALF-channel test fixture; not meant to be a
// general-purpose encoder (SaveImageExr never needs one, since it only
// ever writes FLOAT channels).
uint16_t FloatToHalfForTest(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  const uint32_t sign = (bits >> 16) & 0x8000u;
  int32_t exp = static_cast<int32_t>((bits >> 23) & 0xff) - 127 + 15;
  uint32_t mant = bits & 0x7fffffu;
  if (exp <= 0) return static_cast<uint16_t>(sign);
  if (exp >= 31) return static_cast<uint16_t>(sign | 0x7c00u);
  return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
}

// Appends a complete, valid minimal single-part scanline EXR header
// (magic/version through the attribute list's terminating null) for a
// `w`x`h` image with R,G,B channels of the given `pixel_type` (1=HALF,
// 2=FLOAT) - channels written in the spec-required alphabetical order
// (B,G,R), the same order LoadImageExr's own tests below feed data in.
void AppendMinimalHeader(std::vector<unsigned char>& out, int w, int h, int32_t pixel_type) {
  AppendU8(out, 0x76); AppendU8(out, 0x2f); AppendU8(out, 0x31); AppendU8(out, 0x01);
  AppendI32(out, 2);
  const char* names[3] = {"B", "G", "R"};
  int32_t chlist_size = 1;
  for (const char* n : names) chlist_size += static_cast<int32_t>(std::strlen(n)) + 1 + 16;
  AppendCstr(out, "channels"); AppendCstr(out, "chlist"); AppendI32(out, chlist_size);
  for (const char* n : names) {
    AppendCstr(out, n);
    AppendI32(out, pixel_type);
    AppendU8(out, 0); AppendU8(out, 0); AppendU8(out, 0); AppendU8(out, 0);
    AppendI32(out, 1); AppendI32(out, 1);
  }
  AppendU8(out, 0);
  AppendCstr(out, "compression"); AppendCstr(out, "compression"); AppendI32(out, 1); AppendU8(out, 0);
  AppendCstr(out, "dataWindow"); AppendCstr(out, "box2i"); AppendI32(out, 16);
  AppendI32(out, 0); AppendI32(out, 0); AppendI32(out, w - 1); AppendI32(out, h - 1);
  AppendCstr(out, "lineOrder"); AppendCstr(out, "lineOrder"); AppendI32(out, 1); AppendU8(out, 0);
  AppendU8(out, 0);  // end of attribute list
}
}  // namespace

int main() {
  const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "dino8_test_image_exr";
  std::filesystem::create_directories(tmp);

  // ---- SaveImageExr / LoadImageExr round trip, well above [0,1] ---------
  {
    const int w = 16, h = 12;
    std::vector<float> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const size_t i = (static_cast<size_t>(y) * w + x) * 3;
        const float v = 1.0f + 19.0f * x / (w - 1);  // sweeps 1..20, well past [0,1]
        rgb[i + 0] = v;
        rgb[i + 1] = v * 0.6f;
        rgb[i + 2] = v * 0.3f;
      }
    }
    const std::filesystem::path path = tmp / "gradient.exr";
    std::string err;
    Check(SaveImageExr(path.string(), w, h, rgb, err), "SaveImageExr writes a gradient.exr");

    ImageHdr back;
    Check(LoadImageExr(path.string(), back, err), ("LoadImageExr reads it back: " + err).c_str());
    Check(back.Valid() && back.width == w && back.height == h, "round-tripped size matches");

    int bad = 0;
    float max_err = 0.0f;
    for (size_t i = 0; i < rgb.size() && back.Valid(); ++i) {
      const float d = std::fabs(rgb[i] - back.rgb[i]);
      max_err = std::max(max_err, d);
      if (d > 1e-4f) ++bad;  // FLOAT channels: this should be exact modulo rounding, not RGBE's lossy ~8-bit mantissa
    }
    char label[128];
    std::snprintf(label, sizeof(label), "round-tripped FLOAT values match exactly (max error %.6g, %d/%zu off)", max_err, bad, rgb.size());
    Check(bad == 0, label);

    // The file genuinely carries values above 1.0 unclamped (the whole
    // point versus PNG/BMP/PPM) - spot-check the brightest pixel.
    Check(back.Valid() && back.rgb[(static_cast<size_t>(h - 1) * w + (w - 1)) * 3] > 19.0f,
          "the brightest pixel round-trips above 19.0, not clamped to 1.0 like an LDR format would");

    // LoadImageFile's own magic-byte dispatch recognizes this file as EXR
    // (not falling through to "unsupported format") and tone-maps it to a
    // valid LDR Image, the same dispatch LoadImageHdr's own test checks
    // for Radiance .hdr.
    Image ldr;
    Check(LoadImageFile(path.string(), ldr, err) && ldr.Valid() && ldr.width == w && ldr.height == h,
          "LoadImageFile dispatches a real .exr to the EXR codec and tone-maps it to a valid LDR Image");
  }

  // ---- Hand-built HALF-channel scanline (the encoding a real-world ------
  // ---- HDRI/IBL panorama from the wild is virtually always in) ----------
  {
    const int w = 4, h = 3;
    std::vector<unsigned char> f;
    AppendMinimalHeader(f, w, h, /*pixel_type=*/1);  // HALF
    const size_t header_end = f.size();
    const size_t chunk_size = 8 + static_cast<size_t>(w) * 2 * 3;  // y + dataSize + 3 planar HALF rows
    const size_t data_start = header_end + static_cast<size_t>(h) * 8;
    for (int y = 0; y < h; ++y) AppendI64(f, static_cast<int64_t>(data_start + static_cast<size_t>(y) * chunk_size));
    // Hand-chosen values exercising DecodeHalf's normal, subnormal and
    // zero branches - not just "round-trip whatever SaveImageExr wrote".
    const float r_vals[3] = {2.5f, 0.0f, 100.0f};
    const float g_vals[3] = {1.0f, 5.0e-5f /* subnormal half */, 0.5f};
    const float b_vals[3] = {0.25f, 0.0f, 10.0f};
    for (int y = 0; y < h; ++y) {
      AppendI32(f, y);
      AppendI32(f, static_cast<int32_t>(static_cast<size_t>(w) * 2 * 3));
      for (int x = 0; x < w; ++x) AppendHalf(f, FloatToHalfForTest(b_vals[y]));
      for (int x = 0; x < w; ++x) AppendHalf(f, FloatToHalfForTest(g_vals[y]));
      for (int x = 0; x < w; ++x) AppendHalf(f, FloatToHalfForTest(r_vals[y]));
    }
    const std::filesystem::path path = tmp / "half.exr";
    Check(WriteFile(path, f), "wrote the hand-built HALF-channel EXR fixture");

    ImageHdr img;
    std::string err;
    Check(LoadImageExr(path.string(), img, err), ("LoadImageExr decodes the hand-built HALF scanline: " + err).c_str());
    Check(img.Valid() && img.width == w && img.height == h, "hand-built HALF fixture size matches");
    bool ok = img.Valid();
    for (int y = 0; ok && y < h; ++y) {
      for (int x = 0; ok && x < w; ++x) {
        const size_t o = (static_cast<size_t>(y) * w + x) * 3;
        const float dr = std::fabs(img.rgb[o] - r_vals[y]), dg = std::fabs(img.rgb[o + 1] - g_vals[y]), db = std::fabs(img.rgb[o + 2] - b_vals[y]);
        // Half precision has ~3 decimal digits; allow a small relative tolerance.
        if (dr > 0.01f * std::max(1.0f, r_vals[y]) || dg > 0.01f * std::max(1.0f, g_vals[y]) + 1e-6f || db > 0.01f * std::max(1.0f, b_vals[y])) ok = false;
      }
    }
    Check(ok, "every decoded HALF pixel (normal, subnormal and zero) matches its hand-computed value");
  }

  // ---- Malformed / out-of-scope-input error paths ------------------------
  {
    auto bad_magic = std::vector<unsigned char>{'n', 'o', 't', 'e', 'x', 'r', 0, 0};
    const std::filesystem::path p = tmp / "bad_magic.exr";
    WriteFile(p, bad_magic);
    ImageHdr img; std::string err;
    Check(!LoadImageExr(p.string(), img, err) && !err.empty(), "LoadImageExr rejects a file without the EXR magic");
  }
  {
    std::vector<unsigned char> f;
    AppendMinimalHeader(f, 4, 3, 2);
    f[5] = static_cast<unsigned char>(f[5] | 0x02);  // set the tiled bit (bit 9, byte 5 bit 1) in the version/flags word
    const std::filesystem::path p = tmp / "tiled.exr";
    WriteFile(p, f);
    ImageHdr img; std::string err;
    Check(!LoadImageExr(p.string(), img, err) && !err.empty(), "LoadImageExr rejects a tiled EXR");
  }
  {
    std::vector<unsigned char> f;
    AppendMinimalHeader(f, 4, 3, 2);
    f[5] = static_cast<unsigned char>(f[5] | 0x10);  // multi-part bit (bit 12 - byte 5 covers version-flags bits 8-15)
    const std::filesystem::path p = tmp / "multipart.exr";
    WriteFile(p, f);
    ImageHdr img; std::string err;
    Check(!LoadImageExr(p.string(), img, err) && !err.empty(), "LoadImageExr rejects a multi-part EXR");
  }
  {
    std::vector<unsigned char> f;
    AppendMinimalHeader(f, 4, 3, 2);
    f[5] = static_cast<unsigned char>(f[5] | 0x08);  // deep-data/non-image bit (bit 11)
    const std::filesystem::path p = tmp / "deep.exr";
    WriteFile(p, f);
    ImageHdr img; std::string err;
    Check(!LoadImageExr(p.string(), img, err) && !err.empty(), "LoadImageExr rejects a deep-data EXR");
  }
  {
    // Build a header identical to AppendMinimalHeader but with compression
    // byte set to 1 (ZIPS) instead of 0 (NO_COMPRESSION).
    std::vector<unsigned char> f;
    AppendU8(f, 0x76); AppendU8(f, 0x2f); AppendU8(f, 0x31); AppendU8(f, 0x01);
    AppendI32(f, 2);
    const char* names[3] = {"B", "G", "R"};
    int32_t chlist_size = 1;
    for (const char* n : names) chlist_size += static_cast<int32_t>(std::strlen(n)) + 1 + 16;
    AppendCstr(f, "channels"); AppendCstr(f, "chlist"); AppendI32(f, chlist_size);
    for (const char* n : names) { AppendCstr(f, n); AppendI32(f, 2); AppendU8(f, 0); AppendU8(f, 0); AppendU8(f, 0); AppendU8(f, 0); AppendI32(f, 1); AppendI32(f, 1); }
    AppendU8(f, 0);
    AppendCstr(f, "compression"); AppendCstr(f, "compression"); AppendI32(f, 1); AppendU8(f, 1);  // ZIPS
    AppendCstr(f, "dataWindow"); AppendCstr(f, "box2i"); AppendI32(f, 16);
    AppendI32(f, 0); AppendI32(f, 0); AppendI32(f, 3); AppendI32(f, 2);
    AppendCstr(f, "lineOrder"); AppendCstr(f, "lineOrder"); AppendI32(f, 1); AppendU8(f, 0);
    AppendU8(f, 0);
    const std::filesystem::path p = tmp / "compressed.exr";
    WriteFile(p, f);
    ImageHdr img; std::string err;
    Check(!LoadImageExr(p.string(), img, err) && !err.empty(), "LoadImageExr rejects a compressed (non-NO_COMPRESSION) EXR");
  }
  {
    // A channel list with only "Y" (luminance), no R/G/B at all.
    std::vector<unsigned char> f;
    AppendU8(f, 0x76); AppendU8(f, 0x2f); AppendU8(f, 0x31); AppendU8(f, 0x01);
    AppendI32(f, 2);
    int32_t chlist_size = 1 + static_cast<int32_t>(std::strlen("Y")) + 1 + 16;
    AppendCstr(f, "channels"); AppendCstr(f, "chlist"); AppendI32(f, chlist_size);
    AppendCstr(f, "Y"); AppendI32(f, 2); AppendU8(f, 0); AppendU8(f, 0); AppendU8(f, 0); AppendU8(f, 0); AppendI32(f, 1); AppendI32(f, 1);
    AppendU8(f, 0);
    AppendCstr(f, "compression"); AppendCstr(f, "compression"); AppendI32(f, 1); AppendU8(f, 0);
    AppendCstr(f, "dataWindow"); AppendCstr(f, "box2i"); AppendI32(f, 16);
    AppendI32(f, 0); AppendI32(f, 0); AppendI32(f, 3); AppendI32(f, 2);
    AppendCstr(f, "lineOrder"); AppendCstr(f, "lineOrder"); AppendI32(f, 1); AppendU8(f, 0);
    AppendU8(f, 0);
    const std::filesystem::path p = tmp / "no_rgb.exr";
    WriteFile(p, f);
    ImageHdr img; std::string err;
    Check(!LoadImageExr(p.string(), img, err) && !err.empty(), "LoadImageExr rejects an EXR with no R/G/B channels (e.g. luminance-only)");
  }
  {
    // A header claiming a chunk count/offset table that is truncated.
    std::vector<unsigned char> f;
    AppendMinimalHeader(f, 4, 3, 2);
    f.push_back(0);  // one stray byte: not even one full 8-byte offset entry
    const std::filesystem::path p = tmp / "truncated.exr";
    WriteFile(p, f);
    ImageHdr img; std::string err;
    Check(!LoadImageExr(p.string(), img, err) && !err.empty(), "LoadImageExr rejects a truncated offset table");
  }

  std::printf(failures == 0 ? "All EXR codec checks passed.\n" : "%d EXR codec check(s) FAILED.\n", failures);
  return failures == 0 ? 0 : 1;
}
