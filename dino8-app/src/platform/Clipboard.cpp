#include "platform/Clipboard.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/ClipboardPlatform.h"
#include "render/ImageIO.h"

namespace dino8::platform {

namespace {

// Flips a 3-byte-per-pixel RGB buffer top<->bottom. GL readbacks
// (Viewport::CapturePixelsRGB) come out bottom-up; every image codec here
// (and the OS clipboard formats) wants top-down.
std::vector<unsigned char> FlipRows(int width, int height, const std::vector<unsigned char>& rgb) {
  std::vector<unsigned char> out(rgb.size());
  const size_t row = static_cast<size_t>(width) * 3;
  for (int y = 0; y < height; ++y) {
    std::memcpy(&out[static_cast<size_t>(y) * row], &rgb[static_cast<size_t>(height - 1 - y) * row], row);
  }
  return out;
}

void WriteDebugFileIfRequested(const std::vector<unsigned char>& png) {
  const char* path = std::getenv("DINO8_CLIPBOARD_DEBUG_FILE");
  if (!path || !*path) return;
  // Deliberately not treated as a fallback or an error path: this mirrors,
  // byte for byte, what WriteImageToClipboard placed on the real OS
  // clipboard, purely so a test harness that has no clipboard reader of its
  // own (see tests/smoke.sh) can assert on real image bytes instead of just
  // "the command did not crash".
  if (FILE* f = std::fopen(path, "wb")) {
    std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
  }
}

}  // namespace

bool WriteImageToClipboard(int width, int height, const std::vector<unsigned char>& rgb, bool bottom_up,
                            std::string& error) {
  if (width <= 0 || height <= 0 || rgb.size() < static_cast<size_t>(width) * height * 3) {
    error = "Nothing to copy (no image data)";
    return false;
  }
  const std::vector<unsigned char> top_down = bottom_up ? FlipRows(width, height, rgb) : rgb;

  std::vector<unsigned char> png, bmp;
  std::string enc_error;
  if (!dino8::app::EncodePng(width, height, top_down, png, enc_error)) {
    error = "Could not encode PNG for the clipboard: " + enc_error;
    return false;
  }
  // A BMP encoding failure is not fatal - PNG alone is a perfectly usable
  // clipboard payload - so it only downgrades what target(s) get offered.
  dino8::app::EncodeBmp(width, height, top_down, bmp, enc_error);

  WriteDebugFileIfRequested(png);

  return PlatformWriteImageToClipboard(png, bmp, width, height, error);
}

void ShutdownClipboard() { PlatformShutdownClipboard(); }

}  // namespace dino8::platform
