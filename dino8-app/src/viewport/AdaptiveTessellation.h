// View-dependent adaptive tessellation: the pure part of "View-dependent
// adaptive tessellation - real frustum culling exists, but there is still
// no LOD and no re-tessellation on zoom" (see PARITY_MAP.md, app_display).
// Split out from Viewport.cpp/.h (which pull in GL/ImGui and the whole
// document model) the same way src/app/ShortcutRules.h/.cpp splits its own
// pure logic out of Application.cpp, so it can be unit-tested standalone
// with no GL context, window or document required.
#pragma once

#include <vector>

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

// The multi-viewport form Application::MakeFrameContext actually calls.
// Every open modelling viewport shares one SceneObject display-tessellation
// cache per object (PARITY_MAP.md's own disclosed limitation), so a scale
// driven by only one viewport's zoom - as a single-viewport call would be -
// leaves every *other* open viewport looking at a mesh built for someone
// else's zoom level: too coarse if that other viewport is zoomed in closer
// than whichever viewport picked the scale. Passing every open viewport's
// own ZoomPixelSize() here instead picks the SMALLEST of them (the most
// zoomed-in, most demanding viewport) and returns its own scale - the one
// resolution fine enough to satisfy every open viewport at once, so no
// viewport ever renders coarser than its own zoom actually calls for. A
// zoomed-out viewport still gets a finer mesh than its own zoom alone would
// pick in exchange (more triangles than strictly necessary for that one
// view), the deliberate trade-off against ever showing a visibly blocky
// mesh in a viewport nobody just zoomed. Non-positive entries (a degenerate
// camera state) are ignored; an empty vector or one with no valid entry
// falls back to 1.0, the same as LodScaleForPixelSize's own fallback.
double LodScaleForPixelSizes(const std::vector<double>& pixel_sizes);

}  // namespace dino8::app
