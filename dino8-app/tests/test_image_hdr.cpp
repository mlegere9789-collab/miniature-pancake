// Unit test for the Radiance .hdr / RGBE codec (src/render/ImageIO.cpp's
// LoadImageHdr/SaveImageHdr, plus LoadImageFile's dispatch to it): an
// encode/decode round trip well above the [0,1] range PNG/BMP/PPM are stuck
// at (the documented gap this codec closes - see PARITY_MAP.md's
// "Environments and image-based lighting" entry), a real-world-shaped
// new-style-RLE scanline built by hand and decoded, and the malformed-input
// error paths.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "render/ImageIO.h"

using dino8::app::Image;
using dino8::app::ImageHdr;
using dino8::app::LoadImageFile;
using dino8::app::LoadImageHdr;
using dino8::app::SaveImageHdr;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

bool Close(float a, float b, float rel_tol = 0.03f, float abs_tol = 0.02f) {
  return std::fabs(a - b) <= abs_tol + rel_tol * std::max(std::fabs(a), std::fabs(b));
}

bool WriteFile(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
  FILE* f = std::fopen(path.string().c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  std::fclose(f);
  return ok;
}

void AppendStr(std::vector<unsigned char>& out, const char* s) {
  for (; *s; ++s) out.push_back(static_cast<unsigned char>(*s));
}
}  // namespace

int main() {
  char label[256];
  const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "dino8_test_image_hdr";
  std::filesystem::create_directories(tmp);

  // ---- SaveImageHdr / LoadImageHdr round trip, well above [0,1] ---------
  {
    const int w = 16, h = 12;
    std::vector<float> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const size_t i = (static_cast<size_t>(y) * w + x) * 3;
        // A deliberately HDR-range gradient: brightness sweeps 1..20 (a
        // bright sun/highlight a PNG/BMP/PPM loader would have to clip to
        // 1.0), with the three channels kept within the same order of
        // magnitude of each other - RGBE's shared exponent, by design,
        // only has ~8 bits of precision for whichever channel is *not*
        // the pixel's brightest, so a channel sitting orders of magnitude
        // below the brightest one (as a real low-saturation HDR pixel
        // often does) is expected to lose precision; that is a property
        // of the format, not a bug, and not what this round trip tests.
        const float v = 1.0f + 19.0f * x / (w - 1);
        rgb[i + 0] = v;
        rgb[i + 1] = v * 0.6f;
        rgb[i + 2] = v * 0.3f;
      }
    }
    const std::filesystem::path path = tmp / "gradient.hdr";
    std::string err;
    Check(SaveImageHdr(path.string(), w, h, rgb, err), "SaveImageHdr writes a gradient.hdr");

    ImageHdr back;
    Check(LoadImageHdr(path.string(), back, err), ("LoadImageHdr reads it back: " + err).c_str());
    Check(back.Valid() && back.width == w && back.height == h, "round-tripped size matches");

    int bad = 0;
    float worst_red_above_one = 0.0f;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const size_t i = (static_cast<size_t>(y) * w + x) * 3;
        if (!Close(back.rgb[i + 0], rgb[i + 0]) || !Close(back.rgb[i + 1], rgb[i + 1]) ||
            !Close(back.rgb[i + 2], rgb[i + 2])) {
          ++bad;
        }
        if (rgb[i + 0] > 1.0f) worst_red_above_one = std::max(worst_red_above_one, back.rgb[i + 0]);
      }
    }
    std::snprintf(label, sizeof(label), "every pixel round-trips within tolerance (%d/%d mismatches)", bad, w * h);
    Check(bad == 0, label);
    // The whole point: values above 1.0 must survive, not clip to 1.0 the
    // way an 8-bit codec would.
    std::snprintf(label, sizeof(label), "an above-1.0 radiance value survives the round trip unclamped (got %.3f, want > 1.0)",
                  static_cast<double>(worst_red_above_one));
    Check(worst_red_above_one > 1.0f, label);
  }

  // ---- Hand-built new-style-RLE scanline (the shape a real HDRI file uses) ----
  {
    std::vector<unsigned char> file;
    AppendStr(file, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 8\n");
    // Marker: 2,2,(w>>8),w&0xff for w=8.
    file.push_back(2); file.push_back(2); file.push_back(0); file.push_back(8);
    // Pixels 0-3: radiance (1,1,1) -> RGBE (128,128,128,129) (Ward's shared-
    // exponent encode: frexp(1.0)=(0.5,e=1), scale=128, E=129).
    // Pixels 4-7: radiance (0,0,0) -> RGBE (0,0,0,0), the format's zero case.
    const unsigned char plane_r[8] = {128, 128, 128, 128, 0, 0, 0, 0};
    const unsigned char plane_g[8] = {128, 128, 128, 128, 0, 0, 0, 0};
    const unsigned char plane_b[8] = {128, 128, 128, 128, 0, 0, 0, 0};
    const unsigned char plane_e[8] = {129, 129, 129, 129, 0, 0, 0, 0};
    for (const unsigned char* plane : {plane_r, plane_g, plane_b, plane_e}) {
      file.push_back(8);  // literal count
      for (int i = 0; i < 8; ++i) file.push_back(plane[i]);
    }
    const std::filesystem::path path = tmp / "handbuilt_rle.hdr";
    Check(WriteFile(path, file), "hand-built RLE .hdr written");

    ImageHdr img;
    std::string err;
    Check(LoadImageHdr(path.string(), img, err), ("LoadImageHdr decodes the hand-built RLE scanline: " + err).c_str());
    Check(img.width == 8 && img.height == 1, "decoded size is 8x1");
    if (img.Valid()) {
      bool bright_ok = true, dark_ok = true;
      for (int x = 0; x < 4; ++x) {
        const size_t i = static_cast<size_t>(x) * 3;
        if (!Close(img.rgb[i], 1.0f) || !Close(img.rgb[i + 1], 1.0f) || !Close(img.rgb[i + 2], 1.0f)) bright_ok = false;
      }
      for (int x = 4; x < 8; ++x) {
        const size_t i = static_cast<size_t>(x) * 3;
        if (img.rgb[i] != 0.0f || img.rgb[i + 1] != 0.0f || img.rgb[i + 2] != 0.0f) dark_ok = false;
      }
      Check(bright_ok, "RLE-decoded pixels 0-3 are radiance ~(1,1,1)");
      Check(dark_ok, "RLE-decoded pixels 4-7 are exactly (0,0,0)");
    }
  }

  // ---- Error paths ---------------------------------------------------------
  {
    std::string err;
    ImageHdr img;
    const std::filesystem::path not_hdr = tmp / "not_an_hdr.hdr";
    Check(WriteFile(not_hdr, {'n', 'o', 'p', 'e'}), "wrote a non-HDR file");
    Check(!LoadImageHdr(not_hdr.string(), img, err) && !err.empty(), "LoadImageHdr rejects a file without the #? magic");

    std::vector<unsigned char> truncated;
    AppendStr(truncated, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 4\n");
    // No scanline bytes at all follow the resolution line.
    const std::filesystem::path trunc_path = tmp / "truncated.hdr";
    Check(WriteFile(trunc_path, truncated), "wrote a truncated .hdr (header only, no pixel data)");
    Check(!LoadImageHdr(trunc_path.string(), img, err) && !err.empty(), "LoadImageHdr rejects truncated scanline data");

    std::vector<unsigned char> bad_orientation;
    AppendStr(bad_orientation, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n+X 4 -Y 4\n");
    const std::filesystem::path orient_path = tmp / "bad_orientation.hdr";
    Check(WriteFile(orient_path, bad_orientation), "wrote a non-top-down-oriented .hdr");
    Check(!LoadImageHdr(orient_path.string(), img, err) && !err.empty(), "LoadImageHdr rejects an unsupported orientation");
  }

  // ---- LoadImageFile dispatches .hdr through the tone-mapped 8-bit path ----
  {
    const int w = 2, h = 1;
    std::vector<float> rgb = {1.0f, 1.0f, 1.0f, 8.0f, 8.0f, 8.0f};  // pixel0 = 1.0, pixel1 = a much brighter 8.0
    const std::filesystem::path path = tmp / "tonemap.hdr";
    std::string err;
    Check(SaveImageHdr(path.string(), w, h, rgb, err), "SaveImageHdr for the tone-map check");

    Image img;
    Check(LoadImageFile(path.string(), img, err), ("LoadImageFile reads a .hdr via the tone-mapped path: " + err).c_str());
    Check(img.Valid() && img.width == w && img.height == h, "LoadImageFile's tone-mapped Image has the right size");
    if (img.Valid()) {
      const unsigned char c0 = img.rgba[0], c1 = img.rgba[4];
      std::snprintf(label, sizeof(label), "the brighter (8.0) pixel tone-maps brighter than the dimmer (1.0) one (%d vs %d)",
                    c1, c0);
      Check(c1 > c0, label);
      std::snprintf(label, sizeof(label), "and neither clips to a flat, undifferentiated 255 (%d, %d)", c0, c1);
      Check(c1 < 255, label);
    }
  }

  std::filesystem::remove_all(tmp);
  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
