// View-dependent adaptive tessellation: the pure part of "View-dependent
// adaptive tessellation - real frustum culling exists, but there is still
// no LOD and no re-tessellation on zoom" (see PARITY_MAP.md, app_display).
// Split out from Viewport.cpp/.h (which pull in GL/ImGui and the whole
// document model) the same way src/app/ShortcutRules.h/.cpp splits its own
// pure logic out of Application.cpp, so it can be unit-tested standalone
// with no GL context, window or document required.
#pragma once

namespace dino8::app {

// Maps a viewport's current zoom - `pixel_size` world units per screen
// pixel, i.e. Camera::PixelSize(viewport_height) - to the factor
// Viewport::DrawObjects scales the app's curve_tolerance/surface_tolerance
// Options by before tessellating. 1.0 (no scaling) at the reference zoom
// the app's default Options were tuned for; smaller once zoomed in enough
// (finer tessellation), larger once zoomed out enough (coarser), each
// clamped so a triangle count can't run away in either direction.
// `pixel_size <= 0` (a degenerate camera state) returns 1.0 - no scaling.
double LodScaleForPixelSize(double pixel_size);

}  // namespace dino8::app
