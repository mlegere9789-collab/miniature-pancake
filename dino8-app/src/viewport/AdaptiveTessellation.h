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

// Combines every open modeling viewport's own LodScaleForPixelSize into the
// one scale Application::MakeFrameContext hands to every viewport's
// DrawObjects this frame (see Viewport::FrameContext::lod_scale). Every
// SceneObject has exactly one shared DisplayCache across all viewports, not
// one per viewport, so there is no way to give two viewports at different
// zoom their own independently-correct resolution in the same frame without
// one of them re-tessellating the other's work away. Picking just the
// *active* viewport's own scale (the previous behaviour) starves every
// *other* open viewport of the resolution its own zoom needs - a viewport
// zoomed in close on an object, but not the active one, would silently
// keep showing that object at whatever coarser (or finer) resolution the
// active viewport happens to want, never refining on its own zoom at all.
// The honest fix that still needs no per-viewport cache: take the SMALLEST
// (finest) scale across every visible viewport's own pixel size, so no
// open viewport is ever under-tessellated for its own zoom - the cost,
// disclosed rather than hidden, is that a coarser/zoomed-out viewport may
// render somewhat finer than it strictly needs on its own, never the
// other way around. Returns 1.0 (no scaling) for an empty list.
double FinestLodScale(const std::vector<double>& viewport_pixel_sizes);

}  // namespace dino8::app
