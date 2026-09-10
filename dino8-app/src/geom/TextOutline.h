// Text as NURBS curves: glyph outlines from a system TrueType font through
// the FreeType library bundled with OpenNURBS. Used by Text, TextObject and
// the dimension commands.
#pragma once

#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/curve.h"

namespace dino8::app {

// Returns one closed curve per glyph contour, laid out on `plane` starting
// at its origin, with capital height `height`. `font_used` receives the
// font file that was found; false when no font is available.
bool TextToCurves(const std::string& text, double height, const ON_Plane& plane,
                  std::vector<kernel::NurbsCurve>& out, std::string& font_used, double* advance_width = nullptr);

// Font file search order for this platform (first existing wins).
std::vector<std::string> CandidateFontFiles();

}  // namespace dino8::app
