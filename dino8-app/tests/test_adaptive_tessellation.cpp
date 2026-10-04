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

using dino8::app::FinestLodScale;
using dino8::app::LodScaleForPixelSize;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }
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

  // FinestLodScale: Application::MakeFrameContext's own combiner across
  // every open, visible viewport's pixel size - see its own comment in
  // AdaptiveTessellation.h for why "finest wins" rather than "the active
  // viewport's own scale" (the bug this closes: a non-active viewport
  // zoomed in close on an object used to be silently capped at whatever
  // the active viewport's own, possibly much coarser, zoom wanted).
  Check(FinestLodScale({}) == 1.0, "no open viewports falls back to no scaling");
  Check(Near(FinestLodScale({0.1}), 1.0), "a single viewport at the reference zoom scales by exactly 1.0, same as before this existed");
  {
    // Two viewports, one zoomed in (0.01 -> a fine scale), one zoomed out
    // (1.0 -> a coarse scale): the combined result must be the FINE one -
    // the zoomed-in viewport's own need, not the zoomed-out one's, and not
    // some average of the two either.
    const std::vector<double> two_viewports = {0.01, 1.0};
    const double combined = FinestLodScale(two_viewports);
    Check(Near(combined, LodScaleForPixelSize(0.01)), "the finer of two open viewports' own scales wins, not the coarser one");
    Check(combined < LodScaleForPixelSize(1.0), "the combined scale is strictly finer than the zoomed-out viewport's own scale alone");
  }
  {
    // Order must not matter - this is a plain minimum, not "whichever
    // viewport happens to be first/active".
    Check(Near(FinestLodScale({1.0, 0.01, 0.3}), FinestLodScale({0.3, 0.01, 1.0})),
          "the combined scale does not depend on viewport order");
  }
  Check(Near(FinestLodScale({0.1, 0.1, 0.1}), 1.0), "every open viewport at the same zoom still scales by exactly 1.0");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
