// Internal seam between Clipboard.cpp (shared PNG/BMP encoding + the
// DINO8_CLIPBOARD_DEBUG_FILE test hook) and the one platform-specific .cpp
// that CMake actually compiles for the host OS (ClipboardLinuxX11.cpp,
// ClipboardWin32.cpp or ClipboardMac.mm). Not part of the public API - see
// Clipboard.h for that.
#pragma once

#include <string>
#include <vector>

namespace dino8::platform {

// Hands the OS clipboard both encodings of the same image (top-down RGB
// already turned into real PNG and BMP bytes by Clipboard.cpp) and lets it
// pick whichever target(s) it actually implements. `width`/`height` are
// passed along for formats (like Win32's CF_DIB) that want the raw
// dimensions rather than re-parsing them out of the BMP header.
bool PlatformWriteImageToClipboard(const std::vector<unsigned char>& png, const std::vector<unsigned char>& bmp,
                                    int width, int height, std::string& error);

void PlatformShutdownClipboard();

}  // namespace dino8::platform
