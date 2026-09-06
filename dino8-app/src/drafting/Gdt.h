// GD&T vector glyphs: the feature-control-frame characteristic symbols,
// drawn as curves (not text) so they render without depending on a
// symbol font being installed. Every glyph is built inside a unit box
// [0,1]x[0,1] on the given plane, with `size` the box edge length, so
// callers can drop them straight into a frame cell.
#pragma once

#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/types.h"

namespace dino8::app::drafting {

enum class GdtSymbol {
  Flatness, Straightness, Circularity, Cylindricity, Profile, ProfileSurface,
  Perpendicularity, Angularity, Parallelism, Position, Concentricity,
  Symmetry, Runout, TotalRunout
};

const char* GdtSymbolName(GdtSymbol s);
bool ParseGdtSymbol(const std::string& text, GdtSymbol& out);
std::vector<std::string> GdtSymbolNames();

// Appends the curves of the characteristic symbol at `origin` on `plane`,
// `size` tall (roughly the frame's text height).
void AppendGdtGlyph(GdtSymbol symbol, kernel::Point3d origin, const ON_Plane& plane, double size,
                    std::vector<kernel::NurbsCurve>& out);

// Modifier circle-letters: Ⓜ (MMC), Ⓛ (LMC), Ⓢ (RFS) - a circle around the
// letter, letter drawn from the font (TextToCurves handles the letter; this
// only draws the circle) at `origin`, `size` tall.
void AppendModifierCircle(kernel::Point3d origin, const ON_Plane& plane, double size, std::vector<kernel::NurbsCurve>& out);

// Basic weld symbol glyph (fillet weld triangle) sitting on the reference
// line, `size` tall, above (`above=true`) or below the line.
void AppendWeldGlyph(kernel::Point3d origin, const ON_Plane& plane, double size, bool above, std::vector<kernel::NurbsCurve>& out);

// Surface-finish (machining) checkmark glyph, `size` tall, apex at `origin`.
void AppendSurfaceFinishGlyph(kernel::Point3d origin, const ON_Plane& plane, double size, std::vector<kernel::NurbsCurve>& out);

// Datum-feature symbol: a size x size box with a filled triangle pointing
// down to `origin` (the leader landing).
void AppendDatumTriangle(kernel::Point3d origin, const ON_Plane& plane, double size, std::vector<kernel::NurbsCurve>& out);

}  // namespace dino8::app::drafting
