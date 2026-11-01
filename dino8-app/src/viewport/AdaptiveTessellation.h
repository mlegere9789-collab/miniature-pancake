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

// The chord error a tessellation tolerance leaves ON SCREEN, in pixels:
// `tolerance` (model units) / `pixel_size` (model units per pixel). This is
// the quantity LodScaleForPixelSize keeps near-constant (tolerance scales with
// pixel size, so tolerance/pixel_size stays at base/reference = 0.5 px for the
// app's default 0.05 surface tolerance) everywhere the scale is not clamped;
// where it IS clamped (past LodMinScale()/LodMaxScale() of zoom) it drifts, and
// that drift is exactly what the diagnostics report. 0 for degenerate input.
double ScreenSpaceErrorPixels(double tolerance, double pixel_size);
// The clamp band of LodScaleForPixelSize (deep-zoom floor, zoomed-out ceiling).
double LodMinScale();
double LodMaxScale();

// Per-object DISTANCE-based LOD, on top of LodScaleForPixelSize's own
// per-VIEWPORT zoom-level scale - closing the "still zoom-level LOD, not
// per-object distance-based LOD" gap PARITY_MAP.md's "View-dependent
// adaptive tessellation" item names as its own last remaining one.
// LodScaleForPixelSize/FinestLodScale give every object in a frame the
// SAME scale, from Camera::PixelSize's own "world units per pixel AT THE
// CAMERA'S TARGET DISTANCE" approximation (Camera.cpp) - exact for an
// object sitting right at the target, but for a PERSPECTIVE camera (true
// for any other object, since perspective's own pixels-per-world-unit
// falls off as 1/distance from the eye) increasingly wrong the farther an
// object's own distance from the eye differs from the target's: a close
// foreground object actually needs finer tessellation than the frame-wide
// scale alone would give it, and a distant background one coarser.
// `object_distance / target_distance` is exactly that correction (the
// same linear falloff `Camera::PixelSize`'s own `2.0 * Distance() * ...`
// formula already uses, just evaluated at this object's own distance
// instead of the target's), clamped to the identical [kLodMinScale,
// kLodMaxScale] band LodScaleForPixelSize already uses, for the identical
// reason - an object right at the eye should get finer, not literally
// zero-size, tessellation, and one far past the horizon should get
// coarser, not an unbounded, wasted-vertex blowup.
// `is_perspective = false` always returns 1.0, regardless of either
// distance: a true parallel (orthographic) projection has NO
// pixels-per-world-unit falloff with distance at all - every object, at
// any distance from the eye, occupies the same screen size per world
// unit - so applying this correction there would be modelling a falloff
// that physically does not exist, not merely a simplification.
// `target_distance <= 0` (a degenerate/just-constructed camera) also
// returns 1.0, the same safe fallback every other function here uses.
double DistanceLodFactor(double object_distance, double target_distance, bool is_perspective);

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
