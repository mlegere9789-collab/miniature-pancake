// AutoCAD Colour Index (ACI) palette: the 256-entry colour table DXF/DWG
// group code 62 indexes into, and the nearest-entry lookup ExportDxf/
// ExportDwg use to write an ACI for an arbitrary RGB colour. Kept in its
// own dependency-free translation unit (no OpenNURBS, no LibreDWG) so
// tests/test_aci_palette.cpp can link it directly.
#pragma once

#include <array>

namespace dino8::app {

// RGB (0-255 each) for ACI 0-255: 1-6 the pure hues, 7 black (the "draw on
// white" default), 8-9 greys, 10-249 the 24 hues x 10 shades wheel, 250-255
// the grey ramp. Out-of-range indices give black.
std::array<int, 3> AciToRgb(int aci);

// The ACI (1-255) whose palette colour is nearest to (r, g, b) in squared
// RGB distance. Near-black input returns 7 by convention.
int RgbToAci(int r, int g, int b);

}  // namespace dino8::app
