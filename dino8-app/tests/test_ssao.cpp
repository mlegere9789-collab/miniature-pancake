// Unit test for the pure parts of "SSAO in the rasterized renderer" (see
// PARITY_MAP.md, app_display): the hemisphere sample kernel
// (render/SsaoKernel.h's BuildSsaoKernel) GlRenderer::EndSsaoPass rotates
// per-pixel and scales by radius, and the general 4x4 matrix inverse
// (viewport/Camera.h's Mat4::Inverse) it uses to unproject a depth-buffer
// sample back to view space. Standalone like test_adaptive_tessellation.cpp
// - neither needs a GL context, window or document.
#include <cmath>
#include <cstdio>

#include "render/SsaoKernel.h"
#include "viewport/Camera.h"

using dino8::app::BuildSsaoKernel;
using dino8::app::Mat4;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

float MaxAbsDiff(const Mat4& a, const Mat4& b) {
  float worst = 0.f;
  for (int i = 0; i < 16; ++i) worst = std::max(worst, std::fabs(a.Data()[i] - b.Data()[i]));
  return worst;
}
}  // namespace

int main() {
  // --- BuildSsaoKernel -----------------------------------------------
  const auto kernel = BuildSsaoKernel(16, 0x9e3779b9u);
  Check(kernel.size() == 16, "BuildSsaoKernel(16, ...) returns exactly 16 samples");
  bool all_hemisphere = true, all_bounded = true, any_nonzero_xy = false;
  for (const auto& s : kernel) {
    if (s[2] < 0.0f) all_hemisphere = false;  // +Z hemisphere only - never behind the surface
    const float len = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (len > 1.0001f) all_bounded = false;
    if (std::fabs(s[0]) > 1e-6f || std::fabs(s[1]) > 1e-6f) any_nonzero_xy = true;
  }
  Check(all_hemisphere, "every sample's z is >= 0 (the +Z hemisphere, not a full sphere)");
  Check(all_bounded, "every sample's own length is <= 1 (inside the unit hemisphere)");
  Check(any_nonzero_xy, "samples spread across x/y, not all stacked on the +Z axis");

  const float first_len = std::sqrt(kernel[0][0] * kernel[0][0] + kernel[0][1] * kernel[0][1] + kernel[0][2] * kernel[0][2]);
  const float last_len = std::sqrt(kernel[15][0] * kernel[15][0] + kernel[15][1] * kernel[15][1] + kernel[15][2] * kernel[15][2]);
  Check(first_len < last_len, "samples are biased toward the origin - the first is shorter than the last");

  const auto kernel_same_seed = BuildSsaoKernel(16, 0x9e3779b9u);
  bool identical = kernel.size() == kernel_same_seed.size();
  for (size_t i = 0; identical && i < kernel.size(); ++i)
    identical = kernel[i] == kernel_same_seed[i];
  Check(identical, "the same seed always produces the exact same kernel (deterministic, no <random>)");

  const auto kernel_other_seed = BuildSsaoKernel(16, 12345u);
  bool any_different = kernel_other_seed.size() != kernel.size();
  for (size_t i = 0; !any_different && i < kernel.size(); ++i)
    if (kernel[i] != kernel_other_seed[i]) any_different = true;
  Check(any_different, "a different seed produces a different kernel");

  Check(BuildSsaoKernel(0, 1u).empty(), "BuildSsaoKernel(0, ...) returns no samples");
  Check(BuildSsaoKernel(-3, 1u).empty(), "BuildSsaoKernel with a negative count returns no samples");

  // --- Mat4::Inverse ---------------------------------------------------
  const Mat4 identity = Mat4::Identity();
  Check(MaxAbsDiff(identity.Inverse(), identity) < 1e-6f, "Identity's own inverse is Identity");

  // A real perspective projection (60 degree vertical FOV, 16:9, 0.1..1000
  // near/far - plausible viewport values) composed with its own inverse
  // must round-trip to Identity: this is exactly what GlRenderer::
  // EndSsaoPass needs u_inv_proj for (unprojecting a depth sample back to
  // view space).
  const Mat4 persp = Mat4::Perspective(60.0 * 3.14159265358979 / 180.0, 16.0 / 9.0, 0.1, 1000.0);
  Check(MaxAbsDiff(persp * persp.Inverse(), identity) < 1e-3f, "Perspective(...) * Perspective(...).Inverse() is Identity");
  Check(MaxAbsDiff(persp.Inverse() * persp, identity) < 1e-3f, "Perspective(...).Inverse() * Perspective(...) is Identity");

  // An orthographic projection (the other projection GlRenderer actually
  // uses - parallel viewports) must round-trip the same way.
  const Mat4 ortho = Mat4::Ortho(-10.0, 10.0, -6.0, 6.0, 0.1, 500.0);
  Check(MaxAbsDiff(ortho * ortho.Inverse(), identity) < 1e-3f, "Ortho(...) * Ortho(...).Inverse() is Identity");

  // A concrete round trip: take a homogeneous clip-space point, map it
  // backward through Inverse() then forward again through the original
  // matrix - exactly the ViewPosAt()-then-reproject pair EndSsaoPass
  // performs every frame - and confirm it lands back on the same
  // clip-space point.
  {
    auto mul = [](const Mat4& mm, const float v[4], float out[4]) {
      const float* m = mm.Data();
      for (int r = 0; r < 4; ++r) out[r] = m[0 * 4 + r] * v[0] + m[1 * 4 + r] * v[1] + m[2 * 4 + r] * v[2] + m[3 * 4 + r] * v[3];
    };
    const float clip[4] = {1.f, 1.f, 1.f, 1.f};
    float view[4], back[4];
    mul(persp.Inverse(), clip, view);
    mul(persp, view, back);
    Check(back[3] != 0.f && std::fabs(back[0] / back[3] - 1.f) < 1e-3f && std::fabs(back[1] / back[3] - 1.f) < 1e-3f &&
              std::fabs(back[2] / back[3] - 1.f) < 1e-3f,
          "unprojecting a clip-space point through Inverse() then re-projecting it lands back on the same point");
  }

  // A singular (non-invertible) matrix falls back to Identity rather than
  // dividing by (near-)zero / producing NaNs/Infs.
  Mat4 singular = Mat4::Identity();
  for (auto& v : singular.m) v = 0.f;  // fully zeroed: unambiguously singular, det == 0
  const Mat4 singular_inv = singular.Inverse();
  bool finite = true;
  for (float v : singular_inv.m) if (!std::isfinite(v)) finite = false;
  Check(finite, "inverting a singular (all-zero) matrix stays finite, no NaN/Inf");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
