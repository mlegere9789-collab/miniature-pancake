// Verifies BuildBlendSurfaceG1Adaptive/BuildBlendSurfaceG2Adaptive
// (geom/BlendSurface.h): the "enforce max_gap" PARITY_MAP.md gap for a
// rolling-ball-style blend surface on freeform surfaces, where there is no
// closed-form envelope to check the built patch against at all (see
// MaxSurfaceGap's own doc comment for exactly why) - a genuine convergence
// bound instead: keep doubling the row sample count until two successive
// resolutions stop disagreeing by more than max_gap, or give up within a
// caller-supplied sample budget.
#include <cmath>
#include <cstdio>

#include <opennurbs.h>

#include "geom/BlendSurface.h"

using dino8::app::BuildBlendSurfaceG1;
using dino8::app::BuildBlendSurfaceG1Adaptive;
using dino8::app::BuildBlendSurfaceG2Adaptive;
using dino8::app::MaxSurfaceGap;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

// A flat 10x10 bilinear plane, z = 0 - see test_g2_blend.cpp's own
// MakePlane() (duplicated here rather than shared, to keep this file
// self-contained the same way that one is).
ON_NurbsSurface MakePlane() {
  ON_NurbsSurface s;
  s.Create(3, false, 2, 2, 2, 2);
  s.SetKnot(0, 0, 0); s.SetKnot(0, 1, 10);
  s.SetKnot(1, 0, 0); s.SetKnot(1, 1, 10);
  s.SetCV(0, 0, ON_3dPoint(0, 0, 0));
  s.SetCV(1, 0, ON_3dPoint(10, 0, 0));
  s.SetCV(0, 1, ON_3dPoint(0, 10, 0));
  s.SetCV(1, 1, ON_3dPoint(10, 10, 0));
  return s;
}

}  // namespace

int main() {
  const double R = 5.0;
  ON_NurbsSurface plane = MakePlane();
  ON_Cylinder cyl(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), R), 10.0);
  ON_RevSurface* cylinder = cyl.RevSurfaceForm();
  Check(cylinder != nullptr, "cylinder RevSurfaceForm built");
  if (!cylinder) return 1;

  const ON_Interval u_dom = cylinder->Domain(0);
  const double angle0 = u_dom.Min();
  // A WIDE quarter-turn arc on the cylinder (unlike test_g2_blend.cpp's own
  // tiny-height-range fixture, chosen there specifically to make the
  // direction nearly constant): this makes the B-side tangent direction
  // genuinely rotate along the blend, so the row-to-row shape keeps
  // changing as more rows are added - real convergence behaviour to
  // measure, not an already-flat construction that converges trivially at
  // any sample count.
  const double wide_angle = 0.5 * ON_PI;
  const double v_mid = cylinder->Domain(1).Mid();

  ON_LineCurve ea(ON_3dPoint(10, 0, 0), ON_3dPoint(10, 10, 0));
  ea.SetDomain(0.0, 1.0);
  auto uv_a_at = [&](double t) { return ON_2dPoint(10.0, t * 10.0); };
  auto uv_b_at = [&](double t) { return ON_2dPoint(angle0 + t * wide_angle, v_mid); };

  // --- MaxSurfaceGap itself: identical surfaces have gap exactly 0. ---
  {
    ON_NurbsSurface same;
    const bool ok = BuildBlendSurfaceG1(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, false, 16, same);
    Check(ok, "setup: built a reference G1 blend for the self-gap check");
    Check(MaxSurfaceGap(same, same, 8) == 0.0, "MaxSurfaceGap of a surface against itself is exactly 0");
  }

  // --- Genuine convergence: the gap between successive resolutions must
  // shrink as the sample count grows, on this genuinely-rotating fixture. ---
  {
    ON_NurbsSurface s4, s8, s16, s32;
    Check(BuildBlendSurfaceG1(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, false, 4, s4), "built G1 blend at 4 samples");
    Check(BuildBlendSurfaceG1(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, false, 8, s8), "built G1 blend at 8 samples");
    Check(BuildBlendSurfaceG1(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, false, 16, s16), "built G1 blend at 16 samples");
    Check(BuildBlendSurfaceG1(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, false, 32, s32), "built G1 blend at 32 samples");
    const double gap_4_8 = MaxSurfaceGap(s4, s8, 32);
    const double gap_8_16 = MaxSurfaceGap(s8, s16, 32);
    const double gap_16_32 = MaxSurfaceGap(s16, s32, 32);
    char buf[220];
    std::snprintf(buf, sizeof(buf), "gap(4,8)=%.6f > gap(8,16)=%.6f > gap(16,32)=%.6f - genuinely shrinking, not noise",
                  gap_4_8, gap_8_16, gap_16_32);
    Check(gap_4_8 > gap_8_16 && gap_8_16 > gap_16_32, buf);
    Check(gap_16_32 > 0.0, "sanity: the fixture's own rotation keeps the gap strictly positive even at 16 vs 32 samples");
  }

  // --- BuildBlendSurfaceG1Adaptive: succeeds at a reachable tolerance. ---
  {
    ON_NurbsSurface out;
    double achieved = -1.0;
    const bool ok = BuildBlendSurfaceG1Adaptive(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, /*tangent_boost=*/false,
                                                /*max_gap=*/0.01, /*min_samples=*/4, /*max_samples=*/512, out, &achieved);
    Check(ok, "BuildBlendSurfaceG1Adaptive converges to max_gap=0.01 within a 512-sample budget");
    char buf[160];
    std::snprintf(buf, sizeof(buf), "achieved_gap (%.6f) is at most the requested max_gap (0.01)", achieved);
    Check(achieved >= 0.0 && achieved <= 0.01, buf);
    Check(out.IsValid(), "the converged adaptive surface is itself a valid NURBS surface");
  }

  // --- Fails honestly within a deliberately too-small sample budget, but
  // still reports how close it got. ---
  {
    ON_NurbsSurface out;
    double achieved = -1.0;
    const bool ok = BuildBlendSurfaceG1Adaptive(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, /*tangent_boost=*/false,
                                                /*max_gap=*/1e-12, /*min_samples=*/4, /*max_samples=*/8, out, &achieved);
    Check(!ok, "BuildBlendSurfaceG1Adaptive reports failure when max_gap can't be certified within the sample budget");
    char buf[160];
    std::snprintf(buf, sizeof(buf), "achieved_gap (%.6f) is still reported, finite and > the impossible max_gap", achieved);
    Check(achieved > 1e-12 && achieved < 1e6, buf);
  }

  // --- BuildBlendSurfaceG2Adaptive: same contract, curvature-matching path. ---
  {
    ON_NurbsSurface out;
    double achieved = -1.0;
    const bool ok = BuildBlendSurfaceG2Adaptive(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, /*max_gap=*/0.01,
                                                /*min_samples=*/4, /*max_samples=*/512, out, &achieved);
    Check(ok, "BuildBlendSurfaceG2Adaptive converges to max_gap=0.01 within a 512-sample budget");
    Check(achieved >= 0.0 && achieved <= 0.01, "G2 adaptive's own achieved_gap is at most the requested max_gap");
    Check(out.IsValid(), "the converged G2 adaptive surface is itself a valid NURBS surface");
  }

  delete cylinder;
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
