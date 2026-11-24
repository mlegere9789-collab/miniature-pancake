// Real OS-clipboard image support, one implementation per platform:
//   Linux:   an in-process X11 (Xlib) ICCCM selection owner - a background
//            thread claims the CLIPBOARD selection on a small private
//            window and answers SelectionRequest events for TARGETS,
//            image/png and image/bmp for as long as the app keeps running
//            (see src/platform/ClipboardLinuxX11.cpp).
//   Windows: the Win32 clipboard (OpenClipboard/SetClipboardData, CF_DIB -
//            see src/platform/ClipboardWin32.cpp).
//   macOS:   NSPasteboard, via a small Objective-C++ shim (see
//            src/platform/ClipboardMac.mm).
// All three place a real image the rest of the desktop can paste (Ctrl+V
// into GIMP, Chrome, Word, ...), not a file path or a text hint.
#pragma once

#include <string>
#include <vector>

namespace dino8::platform {

// Places an RGB image on the OS clipboard as a real image/png (and, where
// convenient, also image/bmp) clipboard payload.
//
// `rgb` is 3 bytes per pixel; `bottom_up` says which end of the buffer is
// row 0 - true for a buffer read back from OpenGL (glReadPixels' native
// order, row 0 = the bottom of the image), false for a buffer already in
// ordinary top-down image order (e.g. Application::LastRender().rgb). The
// function flips internally as needed, so callers never have to pre-flip a
// GL readback themselves.
//
// Returns true once the platform clipboard genuinely holds the image.
// Returns false with `error` set when it does not (no display connection,
// no clipboard API available, ...); callers should treat that as a real
// failure, not silently succeed.
//
// If the environment variable DINO8_CLIPBOARD_DEBUG_FILE is set, the exact
// PNG bytes handed to the clipboard are also written to that path, so a
// test harness without a clipboard reader of its own (e.g. a headless CI
// runner with no `xclip`/`wl-paste`) can still assert on real, non-trivial
// image bytes proving this code path ran rather than merely not crashing.
bool WriteImageToClipboard(int width, int height, const std::vector<unsigned char>& rgb, bool bottom_up,
                            std::string& error);

// Releases any background clipboard-server resources (Linux: stops the X11
// selection-owner thread and closes its display connection). Safe to call
// even if WriteImageToClipboard was never called (no-op then). Called once
// from the app's shutdown path; not calling it merely means the process
// exit itself tears the connection down, same as any other X11 client that
// does not keep running as a clipboard manager.
void ShutdownClipboard();

}  // namespace dino8::platform
