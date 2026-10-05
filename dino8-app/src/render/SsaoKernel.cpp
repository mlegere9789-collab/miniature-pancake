#include "render/SsaoKernel.h"

#include <cmath>

namespace dino8::app {

namespace {
// xorshift32 (Marsaglia) - a fixed, tiny, fully deterministic PRNG so the
// kernel this produces never depends on <random>'s own (implementation-
// defined) engine output.
uint32_t NextXorshift32(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}
// [0,1).
float Rand01(uint32_t& state) {
  return static_cast<float>(NextXorshift32(state) >> 8) / static_cast<float>(1u << 24);
}
}  // namespace

std::vector<std::array<float, 3>> BuildSsaoKernel(int count, uint32_t seed) {
  std::vector<std::array<float, 3>> kernel;
  if (count <= 0) return kernel;
  kernel.reserve(static_cast<size_t>(count));
  uint32_t state = seed ? seed : 1u;  // xorshift32 is undefined at state 0
  for (int i = 0; i < count; ++i) {
    float x = Rand01(state) * 2.f - 1.f;
    float y = Rand01(state) * 2.f - 1.f;
    float z = Rand01(state);  // hemisphere: +Z only, never behind the surface
    float len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-6f) { x = 0.f; y = 0.f; z = 1.f; len = 1.f; }
    x /= len; y /= len; z /= len;
    // Bias more samples close to the origin than far from it (scale^2),
    // the same distribution the classic LearnOpenGL/Crysis-style kernel
    // uses, so the AO estimate resolves fine nearby detail without
    // needing a much larger raw sample count.
    float scale = static_cast<float>(i) / static_cast<float>(count);
    scale = 0.1f + 0.9f * scale * scale;
    kernel.push_back({x * scale, y * scale, z * scale});
  }
  return kernel;
}

}  // namespace dino8::app
