#pragma once

#include <string>
#include <vector>

namespace dino8::app {

// Writes an 8-bit RGB image as a PNG file: `rgb` is `width * height * 3` bytes, TOP-DOWN rows (the order PNG stores; a glReadPixels
// buffer is bottom-up and must be flipped by the caller). The pixel data is zlib-compressed (filter type 0 on every row). When
// `dpi` > 0 a pHYs chunk records the physical resolution as round(dpi / 0.0254) pixels per metre, so a reader reports it back as
// `dpi`; with 0 no pHYs chunk is written (unknown resolution). Returns false with `error` set for an empty or short buffer, an
// unwritable path or a compression failure. Kept in its own translation unit because it needs zlib (the copy OpenNURBS already
// links into dino8_kernel), which the dependency-free ImageIO.cpp deliberately does not.
bool WritePngRgb(const std::string& path, int width, int height, const std::vector<unsigned char>& rgb, int dpi, std::string& error);

}  // namespace dino8::app
