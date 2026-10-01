#include "viewport/AdaptiveTessellation.h"

#include <algorithm>

namespace dino8::app {

namespace {
// The app's own curve/surface display-tolerance Options are tuned for a
// viewport showing roughly this many world units per screen pixel (an
// 80-unit-tall ortho view in a ~800px-tall docked viewport - Viewport's own
// constructor default), so that pixel size is the "no scaling" reference
// point: a viewport actually showing fewer world units per pixel than this
// (zoomed in) tessellates finer than the raw Option value, and one showing
// more (zoomed out) tessellates coarser.
constexpr double kLodReferencePixelSize = 0.1;
// Clamped so neither an extreme close-up nor a zoomed-way-out view can
// drive the triangle count unbounded in either direction.
constexpr double kLodMinScale = 0.2;
constexpr double kLodMaxScale = 6.0;
}  // namespace

double LodScaleForPixelSize(double pixel_size) {
  if (!(pixel_size > 0.0)) return 1.0;  // degenerate camera state: no scaling
  return std::clamp(pixel_size / kLodReferencePixelSize, kLodMinScale, kLodMaxScale);
}

}  // namespace dino8::app
