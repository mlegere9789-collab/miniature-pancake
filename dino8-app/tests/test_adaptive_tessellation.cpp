// Unit test for view-dependent adaptive tessellation's pure scaling
// function (src/viewport/AdaptiveTessellation.cpp's LodScaleForPixelSize),
// closing PARITY_MAP.md's "View-dependent adaptive tessellation" item:
// real frustum culling already existed, but there was no LOD and no
// re-tessellation on zoom. Viewport::DrawObjects multiplies the app's own
// curve_tolerance/surface_tolerance Options by this factor before calling
// SceneObject::EnsureAdaptiveDisplay, so a bigger factor here means a
// coarser (larger-chord-height) mesh and a smaller factor means a finer
// one. Standalone like test_aci_palette.cpp - no GL/ImGui/document
// dependency at all.
#include <cmath>
#include <cstdio>

#include <vector>

#include "viewport/AdaptiveTessellation.h"

using dino8::app::LodScaleForPixelSize;
using dino8::app::LodScaleForPixelSizes;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}  // namespace

int main() {
  // At the reference zoom (0.1 world units/pixel - see the .cpp's own
  // comment), the scale is exactly 1.0: an untouched viewport must
  // tessellate exactly as before this feature existed.
  Check(std::fabs(LodScaleForPixelSize(0.1) - 1.0) < 1e-9, "reference pixel size (0.1) scales by exactly 1.0");

  // Zoomed in (fewer world units per pixel) scales below 1.0, so
  // tolerance * scale is a finer (smaller) chord height than the raw
  // Option value.
  Check(LodScaleForPixelSize(0.05) < 1.0, "zoomed in (0.05) scales below 1.0 (finer tessellation)");
  Check(LodScaleForPixelSize(0.01) < LodScaleForPixelSize(0.05), "zooming in further scales down further (monotonic)");

  // Zoomed out (more world units per pixel) scales above 1.0, coarser.
  Check(LodScaleForPixelSize(0.2) > 1.0, "zoomed out (0.2) scales above 1.0 (coarser tessellation)");
  Check(LodScaleForPixelSize(1.0) > LodScaleForPixelSize(0.2), "zooming out further scales up further (monotonic)");

  // Clamped in both directions so an extreme zoom can't blow up the
  // triangle count or collapse it to nothing.
  Check(LodScaleForPixelSize(0.0000001) >= 0.2 - 1e-9, "an extreme close-up is clamped to a floor scale (>= 0.2)");
  Check(LodScaleForPixelSize(1000.0) <= 6.0 + 1e-9, "an extreme zoom-out is clamped to a ceiling scale (<= 6.0)");
  // And the clamp is real, not just coincidentally satisfied at these two
  // probes - a further order of magnitude past each must not move it.
  Check(std::fabs(LodScaleForPixelSize(0.0000001) - LodScaleForPixelSize(0.00000001)) < 1e-9,
        "floor clamp holds across a further order of magnitude closer");
  Check(std::fabs(LodScaleForPixelSize(1000.0) - LodScaleForPixelSize(10000.0)) < 1e-9,
        "ceiling clamp holds across a further order of magnitude further out");

  // A single real "Zoom In" (cmd_view.cpp's ZoomCommand calls
  // Camera::Dolly(2.0), whose own factor is 0.85^steps) must cross
  // SceneObject::EnsureAdaptiveDisplay's own re-tessellation hysteresis
  // band (1.35x) - otherwise "real re-tessellation on zoom" would be true
  // in name only, never actually triggering in practice.
  const double after_one_zoom_in = LodScaleForPixelSize(0.1 * std::pow(0.85, 2.0));
  Check(1.0 / after_one_zoom_in > 1.35, "one Zoom In command moves the scale past the 1.35x re-tessellation threshold");

  // Degenerate camera state (should never happen, but must not divide by
  // zero or return something nonsensical) falls back to no scaling.
  Check(LodScaleForPixelSize(0.0) == 1.0, "pixel_size == 0.0 falls back to no scaling");
  Check(LodScaleForPixelSize(-1.0) == 1.0, "a negative pixel_size falls back to no scaling");

  // LodScaleForPixelSizes: Application::MakeFrameContext's multi-viewport
  // form, closing the "two viewports at very different zoom fight over
  // that one cache's resolution" gap this bullet's own text discloses -
  // every open viewport shares one SceneObject display cache, so whichever
  // viewport is zoomed in furthest must win the scale for all of them.
  Check(std::fabs(LodScaleForPixelSizes({0.1}) - LodScaleForPixelSize(0.1)) < 1e-9,
        "a single viewport matches the plain single-viewport call exactly");
  Check(std::fabs(LodScaleForPixelSizes({0.2, 0.05, 1.0}) - LodScaleForPixelSize(0.05)) < 1e-9,
        "three viewports at different zoom pick the most-zoomed-in (smallest pixel_size) one's own scale");
  Check(LodScaleForPixelSizes({0.05, 0.2}) == LodScaleForPixelSizes({0.2, 0.05}),
        "order of the open viewports doesn't change the result");
  // A viewport reporting a degenerate pixel_size (should never happen, but
  // must not let one bad entry silently win over a real, valid one from
  // another open viewport).
  Check(std::fabs(LodScaleForPixelSizes({0.0, 0.3, -5.0}) - LodScaleForPixelSize(0.3)) < 1e-9,
        "degenerate entries (<= 0) are ignored, not treated as the most-demanding viewport");
  // No open viewport at all (should never happen - DrawViewports only runs
  // with at least one - but must still return the documented fallback).
  Check(LodScaleForPixelSizes({}) == 1.0, "an empty viewport list falls back to no scaling");
  Check(LodScaleForPixelSizes({0.0, -1.0}) == 1.0, "every entry degenerate falls back to no scaling, same as one would");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
