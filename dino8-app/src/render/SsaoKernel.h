// Pure part of "SSAO in the rasterized renderer" (see PARITY_MAP.md,
// app_display): the hemisphere sample kernel GlRenderer's SSAO pass
// rotates per-pixel and scales by the view-space normal/radius. Split out
// from GlRenderer.cpp (which pulls in the GL loader) the same way
// viewport/AdaptiveTessellation.h/.cpp splits its own pure logic out of
// Viewport.cpp, so it can be unit-tested standalone with no GL context.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dino8::app {

// `count` offsets inside the unit hemisphere +Z (x,y in [-1,1], z in
// [0,1], each vector's own length <= 1), biased toward the origin
// (`scale*scale` on the per-sample length) the same way the classic
// LearnOpenGL/Crysis-style SSAO kernel is - more samples close to the
// shaded point than far from it - so GlRenderer's AO shader need only
// rotate each one into the per-fragment tangent space built from the
// depth buffer's own screen-space derivatives (no separate G-buffer
// normal pass) and scale by its chosen world-space radius. Deterministic
// for a given `seed` (a fixed xorshift PRNG, not <random> - whose own
// generated sequence is unspecified-but-implementation-defined across
// standard libraries - so this produces the exact same kernel on every
// platform/toolchain this project ships for).
std::vector<std::array<float, 3>> BuildSsaoKernel(int count, uint32_t seed);

}  // namespace dino8::app
