// macOS OS-clipboard image support: the real NSPasteboard API. A small
// Objective-C++ shim, compiled only under __APPLE__ (see CMakeLists.txt,
// which also enables the OBJCXX language and links AppKit for this one
// file) so the rest of the codebase stays plain C++.
//
// Not runtime-testable from this Linux sandbox (see the top-level report
// for what remains unverified); written directly against the documented
// AppKit NSPasteboard API.
#ifdef __APPLE__

#include "platform/ClipboardPlatform.h"

#import <AppKit/AppKit.h>

namespace dino8::platform {

bool PlatformWriteImageToClipboard(const std::vector<unsigned char>& png, const std::vector<unsigned char>& /*bmp*/,
                                    int /*width*/, int /*height*/, std::string& error) {
  @autoreleasepool {
    if (png.empty()) {
      error = "No PNG-encoded image available";
      return false;
    }
    NSData* png_data = [NSData dataWithBytes:png.data() length:png.size()];

    // Offer the classic NSPasteboardTypeTIFF representation too, alongside
    // PNG: it costs nothing extra (NSImage decodes the PNG bytes once) and
    // covers pasteboard readers that only ever learned to look for TIFF.
    NSImage* image = [[NSImage alloc] initWithData:png_data];
    NSData* tiff_data = image ? [image TIFFRepresentation] : nil;

    NSMutableArray<NSPasteboardType>* types = [NSMutableArray arrayWithObject:NSPasteboardTypePNG];
    if (tiff_data) [types addObject:NSPasteboardTypeTIFF];

    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    [pasteboard declareTypes:types owner:nil];
    const BOOL ok = [pasteboard setData:png_data forType:NSPasteboardTypePNG];
    if (tiff_data) [pasteboard setData:tiff_data forType:NSPasteboardTypeTIFF];

    if (!ok) {
      error = "NSPasteboard setData:forType:NSPasteboardTypePNG failed";
      return false;
    }
    return true;
  }
}

void PlatformShutdownClipboard() {
  // NSPasteboard ownership is not held by a background thread/connection
  // the way the X11 selection owner is - nothing to tear down here.
}

}  // namespace dino8::platform

#endif  // __APPLE__
