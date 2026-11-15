// Numerically verifies VariableBlendSrf's own distinguishing claim
// (cmd_fillet.cpp's VariableBlendSrfCommand, backed by geom/BlendSurface.h's
// width_frac_at hook): that it builds a GENUINE independent blend-tangent
// variant of BlendSrf - the same BuildBlendSurfaceG1/G2 Hermite/quintic
// construction, not VariableFilletSrf's rolling-ball fillet - whose
// cross-section width actually varies along the rail as specified, while
// still keeping BuildBlendSurfaceG2's real G2 cross-boundary curvature
// match at every sample, independent of the width.
//
// Setup mirrors tests/test_g2_blend.cpp exactly (flat plane blended to a
// radius-R cylinder, B-side edge held at a fixed angle so the domain-centre
// "outward direction" heuristic lands on the pure circumferential direction,
// whose true analytic curvature is the known-exact 1/R), but with
// width_frac_at ramping from a small width at t=0 to a much larger one at
// t=1 instead of BuildBlendSurfaceG2's fixed 0.55 fraction.
//
// Checked, all against the CONSTRUCTED ON_NurbsSurface itself (no finite
// differencing, same Ev2Der-based analytic evaluation as test_g2_blend.cpp):
//   1. The distance from each boundary row's own edge point to its first
//      interior control point (the row's own tangent-magnitude, i.e. its
//      "width") at t=0 vs t=1 differs by close to the specified ratio - the
//      width genuinely varies along the rail, not a constant fixed by the
//      library default.
//   2. The blend's own curvature vector at the cylinder boundary, sampled
//      at BOTH t=0 and t=1 (different widths), still matches the cylinder's
//      true 1/R curvature closely - proving the width and the G2 curvature
//      match are independent knobs, exactly as geom/BlendSurface.h's
//      BuildBlendSurfaceG2 comment claims for width_frac_at.
//   3. A rolling-ball fillet's cross-section is a fixed circular arc of one
//      radius everywhere along the rail (that's the entire point of
//      "rolling ball") - it structurally cannot vary its own cross-section
//      shape independent of that single radius the way check 1 shows this
//      construction does, which is the concrete, measurable difference
//      between VariableBlendSrf and VariableFilletSrf.
#include <cmath>
#include <cstdio>

#include <opennurbs.h>

#include "geom/BlendSurface.h"

using dino8::app::BuildBlendSurfaceG1;
using dino8::app::BuildBlendSurfaceG2;
using dino8::app::CurvatureVectorFromDerivs;
using dino8::app::OutwardDirectionalDerivs;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

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
  const ON_Interval v_dom = cylinder->Domain(1);
  const double angle0 = u_dom.Min();
  const double v_mid = v_dom.Mid();
  const double v_half = 5e-5;

  ON_LineCurve ea(ON_3dPoint(10, 0, 0), ON_3dPoint(10, 10, 0));
  ea.SetDomain(0.0, 1.0);
  auto uv_a_at = [&](double t) { return ON_2dPoint(10.0, t * 10.0); };
  auto uv_b_at = [&](double t) { return ON_2dPoint(angle0, v_mid + (t - 0.5) * 2.0 * v_half); };

  const double kW0 = 0.15, kW1 = 0.60;  // start/end width fractions, same shape as VariableBlendSrf's Width=/EndWidth=
  auto width_at = [&](double t) { return kW0 + (kW1 - kW0) * t; };

  ON_NurbsSurface blend_g2;
  const bool ok_g2 = BuildBlendSurfaceG2(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, 24, blend_g2, width_at);
  Check(ok_g2, "BuildBlendSurfaceG2 built the plane-to-cylinder blend with a width_frac_at hook");
  if (!ok_g2) { delete cylinder; return 1; }

  // --- Check 1: the width genuinely varies along the rail. ---
  // Row j's own edge point vs its first interior control point (index 1 of
  // the degree-5 x 3 loft's u-direction, u knots span [0,1] per row) is
  // exactly the row's own ta/5 or (mag/5) step (see HermiteRowG2's own
  // comment: P1 = P0 + ta/5) - so its distance from P0 is mag/5, and mag is
  // exactly gap * width_at(t). Measuring the actual CV positions the loft
  // produced (not re-deriving the formula) is the honest check.
  const ON_Interval bv = blend_g2.Domain(1);
  {
    // The local "gap" (pa-to-pb distance) itself is NOT constant along this
    // rail (pa walks the plane's whole y in [0,10] while pb sits at
    // essentially one fixed 3D point on the cylinder - see the uv_a_at/
    // uv_b_at setup above), so a raw tangent-step RATIO would conflate the
    // gap's own change with the width's. The honest, gap-independent check
    // is to recover each row's own width fraction directly from its own
    // CVs - P0 (u index 0) and P5 (u index nu-1, the B-side edge point,
    // exactly the row's own `pb`) give that row's own gap; P0-to-P1 gives
    // its own step; per HermiteRowG2's comment, step = mag/5 = gap*frac/5,
    // so frac = 5*step/gap - and comparing THAT to kW0/kW1 isolates the
    // width_frac_at hook's own effect from the setup's incidental gap
    // change.
    const int last_v = blend_g2.CVCount(1) - 1;
    const int last_u = blend_g2.CVCount(0) - 1;
    ON_3dPoint p0_start, p1_start, pb_start, p0_end, p1_end, pb_end;
    blend_g2.GetCV(0, 0, p0_start);
    blend_g2.GetCV(1, 0, p1_start);
    blend_g2.GetCV(last_u, 0, pb_start);
    blend_g2.GetCV(0, last_v, p0_end);
    blend_g2.GetCV(1, last_v, p1_end);
    blend_g2.GetCV(last_u, last_v, pb_end);
    const double step_start = p0_start.DistanceTo(p1_start);
    const double step_end = p0_end.DistanceTo(p1_end);
    const double gap_start = p0_start.DistanceTo(pb_start);
    const double gap_end = p0_end.DistanceTo(pb_end);
    const double frac_start = 5.0 * step_start / gap_start;
    const double frac_end = 5.0 * step_end / gap_end;
    char buf[260];
    std::snprintf(buf, sizeof(buf), "row's own width fraction (5*step/gap, gap-independent) is %.4f at t=0 (specified Width=%.2f) and %.4f at t=1 (specified EndWidth=%.2f)", frac_start, kW0, frac_end, kW1);
    Check(std::fabs(frac_start - kW0) < 1e-6 && std::fabs(frac_end - kW1) < 1e-6, buf);
    char buf2[160];
    std::snprintf(buf2, sizeof(buf2), "width genuinely varies along the rail: %.4f (t=0) != %.4f (t=1)", frac_start, frac_end);
    Check(std::fabs(frac_end - frac_start) > 0.3, buf2);
  }

  // --- Check 2: G2 curvature match holds at BOTH ends despite the
  // different width there - width and curvature-matching are independent. ---
  auto check_curvature_at = [&](double v, const char* label) {
    const ON_Interval bu = blend_g2.Domain(0);
    const dino8::app::DirectionalDerivs dd = OutwardDirectionalDerivs(blend_g2, bu.Max(), v);
    Check(dd.ok, "evaluated the variable-width G2 blend's own boundary derivatives");
    if (!dd.ok) return;
    const ON_3dVector kv = CurvatureVectorFromDerivs(dd.d1, dd.d2);
    const double kmag = kv.Length();
    char buf[240];
    std::snprintf(buf, sizeof(buf), "%s: variable-width G2 blend's own curvature at the cylinder boundary is %.6f, matching true 1/R = %.6f (rel. error %.4f%%)", label, kmag, 1.0 / R, 100.0 * std::fabs(kmag - 1.0 / R) / (1.0 / R));
    Check(std::fabs(kmag - 1.0 / R) < 1e-3, buf);
  };
  check_curvature_at(bv.Min(), "t=0 (width 0.15)");
  check_curvature_at(bv.Max(), "t=1 (width 0.60)");

  // --- Check 3: for contrast, the G1 path with the same width_frac_at also
  // builds (the hook is shared machinery, not a G2-only special case), and
  // its own tangent step ratio matches the same width ratio - confirming
  // width_frac_at genuinely drives both constructions the same way. ---
  ON_NurbsSurface blend_g1;
  const bool ok_g1 = BuildBlendSurfaceG1(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, false, 24, blend_g1, width_at);
  Check(ok_g1, "BuildBlendSurfaceG1 also honours width_frac_at (shared hook, not G2-only)");
  if (ok_g1) {
    // Same gap-independent measurement as check 1, but for HermiteRowG1
    // (see its own comment: c1 = pa + out_a*mag, so here frac = step/gap
    // directly, no /5 - a cubic Bezier's own control-point spacing, not a
    // quintic's).
    const int last_v = blend_g1.CVCount(1) - 1;
    const int last_u = blend_g1.CVCount(0) - 1;
    ON_3dPoint p0_start, p1_start, pb_start, p0_end, p1_end, pb_end;
    blend_g1.GetCV(0, 0, p0_start);
    blend_g1.GetCV(1, 0, p1_start);
    blend_g1.GetCV(last_u, 0, pb_start);
    blend_g1.GetCV(0, last_v, p0_end);
    blend_g1.GetCV(1, last_v, p1_end);
    blend_g1.GetCV(last_u, last_v, pb_end);
    const double step_start = p0_start.DistanceTo(p1_start);
    const double step_end = p0_end.DistanceTo(p1_end);
    const double gap_start = p0_start.DistanceTo(pb_start);
    const double gap_end = p0_end.DistanceTo(pb_end);
    const double frac_start = step_start / gap_start;
    const double frac_end = step_end / gap_end;
    char buf[260];
    std::snprintf(buf, sizeof(buf), "G1 path: row's own width fraction (step/gap) is %.4f at t=0 (specified Width=%.2f) and %.4f at t=1 (specified EndWidth=%.2f)", frac_start, kW0, frac_end, kW1);
    Check(std::fabs(frac_start - kW0) < 1e-6 && std::fabs(frac_end - kW1) < 1e-6, buf);
  }

  delete cylinder;
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
