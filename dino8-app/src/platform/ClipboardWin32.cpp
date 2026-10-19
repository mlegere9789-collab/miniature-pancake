// Windows OS-clipboard image support: the real Win32 clipboard API
// (OpenClipboard/EmptyClipboard/SetClipboardData/CloseClipboard) with
// CF_DIB, plus the widely-supported registered "PNG" clipboard format (the
// same convention Chrome/Firefox use to put a real PNG on the Windows
// clipboard alongside a DIB fallback) so paste targets that specifically
// want PNG - and ones that only understand CF_DIB - both work.
//
// Not runtime-testable from this Linux sandbox (see the top-level report
// for what remains unverified); written directly against the documented
// Win32 clipboard API and compiled only under _WIN32 (see CMakeLists.txt).
#ifdef _WIN32

#include "platform/ClipboardPlatform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstring>

#pragma comment(lib, "user32.lib")

namespace dino8::platform {

namespace {

// Copies `data` into a newly GlobalAlloc'd GMEM_MOVEABLE block and hands it
// to SetClipboardData under `format`. Ownership of the handle passes to the
// clipboard on success (Windows requires this - the caller must not free or
// keep using it), which is why this never frees `mem` itself; on failure to
// even lock it, the handle is freed here since SetClipboardData was never
// reached.
bool SetClipboardBytes(UINT format, const void* data, size_t size) {
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
  if (!mem) return false;
  void* dst = GlobalLock(mem);
  if (!dst) {
    GlobalFree(mem);
    return false;
  }
  std::memcpy(dst, data, size);
  GlobalUnlock(mem);
  if (!SetClipboardData(format, mem)) {
    GlobalFree(mem);
    return false;
  }
  return true;
}

}  // namespace

bool PlatformWriteImageToClipboard(const std::vector<unsigned char>& png, const std::vector<unsigned char>& bmp,
                                    int /*width*/, int /*height*/, std::string& error) {
  // CF_DIB is a BITMAPINFOHEADER followed by pixel bits - exactly what our
  // BMP encoding contains after its 14-byte BITMAPFILEHEADER, so the same
  // EncodeBmp() output Linux/macOS don't need is reused here rather than
  // building a second encoder.
  if (bmp.size() <= 14) {
    error = "No BMP-encoded image available for CF_DIB";
    return false;
  }

  bool opened = false;
  for (int attempt = 0; attempt < 5 && !opened; ++attempt) {
    if (OpenClipboard(nullptr)) opened = true;
    else Sleep(20);  // another app can transiently hold the clipboard open
  }
  if (!opened) {
    error = "Could not open the Windows clipboard (another application is holding it)";
    return false;
  }

  bool ok = EmptyClipboard() != 0;
  if (ok) ok = SetClipboardBytes(CF_DIB, bmp.data() + 14, bmp.size() - 14);
  if (ok && !png.empty()) {
    // Best-effort: the registered "PNG" format is not guaranteed to exist
    // on every Windows install (RegisterClipboardFormat creates it on
    // first use), but plenty of real applications (browsers, image
    // editors) look for it, so offer it whenever CF_DIB succeeded.
    const UINT png_format = RegisterClipboardFormatW(L"PNG");
    if (png_format != 0) SetClipboardBytes(png_format, png.data(), png.size());
  }
  CloseClipboard();

  if (!ok) {
    error = "SetClipboardData(CF_DIB) failed (GetLastError=" + std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
}

void PlatformShutdownClipboard() {
  // Windows clipboard ownership is per-open/close, not held by a background
  // thread the way the X11 selection owner is - there is nothing to tear
  // down here.
}

}  // namespace dino8::platform

#endif  // _WIN32
