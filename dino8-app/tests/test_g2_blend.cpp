// Numerically verifies the curvature-matching claim in
// geom/BlendSurface.h's BuildBlendSurfaceG2 comment: that its
// Continuity=Curvature blend's own curvature vector at a boundary
// actually matches the adjacent surface's true curvature there - not
// just the tangent plane (which the old/G1 tangent-magnitude-boost
// approximation, BuildBlendSurfaceG1 with tangent_boost=true, already
// gave under the old "Continuity=Curvature" label).
//
// Setup: a flat plane (surface A, exact curvature 0 in every direction)
// blended to a cylinder of radius R = 5 (surface B). The blend's B-side
// edge is placed at a fixed angle on the cylinder with the seam running
// along a height range so tiny (1e-4 out of a domain of 10) relative to
// the full 2*pi angle domain that the domain-centre "outward direction"
// heuristic (OutwardDirectionalDerivs/InteriorDirection3d - see
// BlendSurface.cpp) lands, to floating-point precision, on the pure
// circumferential direction - the direction whose true analytic normal
// curvature is exactly 1/R (a cylinder's principal curvature; the axial
// direction's is exactly 0). That makes 1/R a known-exact target to
// check the blend's OWN evaluated curvature against, via the same
// Ev2Der-based analytic evaluation (no finite differencing) applied to
// the constructed ON_NurbsSurface itself.
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

// A flat 10x10 bilinear plane, z = 0, u,v in [0,10] both mapping directly
// to x,y - its own curvature is exactly 0 in every direction, so it is
// insensitive to the domain-centre direction heuristic's imprecision.
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

  const ON_Interval u_dom = cylinder->Domain(0);  // angle, expected ~[0, 2pi]
  const ON_Interval v_dom = cylinder->Domain(1);  // height, expected [0, 10]
  const double angle0 = u_dom.Min();               // a boundary angle, far from the domain mid
  const double v_mid = v_dom.Mid();
  const double v_half = 5e-5;                      // seam height half-range: 1e-4 total, vs a ~2*pi angle domain

  // --- Sanity: the cylinder's own analytic curvature at (angle0, v_mid)
  // matches the textbook 1/R exactly (checks OutwardDirectionalDerivs
  // itself, independent of the blend construction below). ---
  {
    const dino8::app::DirectionalDerivs dd = OutwardDirectionalDerivs(*cylinder, angle0, v_mid);
    Check(dd.ok, "OutwardDirectionalDerivs evaluated the cylinder");
    const ON_3dVector kv = CurvatureVectorFromDerivs(dd.d1, dd.d2);
    const double kmag = kv.Length();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "cylinder's own analytic curvature at the boundary is 1/R = %.6f (got %.6f)", 1.0 / R, kmag);
    Check(std::fabs(kmag - 1.0 / R) < 1e-6, buf);
  }

  // --- Build the flat-to-cylinder blend, Curvature (G2) path. ---
  // A-side: the plane's own u = 10 edge, sampled over its full v in [0,10]
  // (a flat surface's curvature is 0 regardless of direction, so this
  // edge's own domain-centre imprecision is harmless).
  ON_LineCurve ea(ON_3dPoint(10, 0, 0), ON_3dPoint(10, 10, 0));
  ea.SetDomain(0.0, 1.0);
  auto uv_a_at = [&](double t) { return ON_2dPoint(10.0, t * 10.0); };
  // B-side: a fixed angle on the cylinder, height varying only within the
  // tiny [v_mid - v_half, v_mid + v_half] range described above.
  auto uv_b_at = [&](double t) { return ON_2dPoint(angle0, v_mid + (t - 0.5) * 2.0 * v_half); };

  ON_NurbsSurface blend_g2;
  const bool ok_g2 = BuildBlendSurfaceG2(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, 24, blend_g2);
  Check(ok_g2, "BuildBlendSurfaceG2 built the plane-to-cylinder blend");

  ON_NurbsSurface blend_g1;
  const bool ok_g1 = BuildBlendSurfaceG1(ea, plane, uv_a_at, ea, *cylinder, uv_b_at, /*tangent_boost=*/true, 24, blend_g1);
  Check(ok_g1, "BuildBlendSurfaceG1 (old Continuity=Curvature tangent-boost) built the same blend for comparison");

  if (ok_g2) {
    // Evaluate the BLEND SURFACE'S OWN curvature at its b-side boundary
    // (u = domain max), at the seam's mid v - not the algebra used to
    // build it, the actual constructed/evaluated NURBS surface.
    const ON_Interval bu = blend_g2.Domain(0), bv = blend_g2.Domain(1);
    const dino8::app::DirectionalDerivs dd = OutwardDirectionalDerivs(blend_g2, bu.Max(), bv.Mid());
    Check(dd.ok, "evaluated the G2 blend surface's own boundary derivatives");
    const ON_3dVector kv = CurvatureVectorFromDerivs(dd.d1, dd.d2);
    const double kmag = kv.Length();
    char buf[220];
    std::snprintf(buf, sizeof(buf), "G2 blend's own curvature at the cylinder boundary is %.6f, matching the cylinder's true 1/R = %.6f (rel. error %.4f%%)", kmag, 1.0 / R, 100.0 * std::fabs(kmag - 1.0 / R) / (1.0 / R));
    Check(std::fabs(kmag - 1.0 / R) < 1e-3, buf);

    // Direction, not just magnitude: the curvature vector must point
    // toward the cylinder's axis (the -x direction at angle0 = 0), not
    // some unrelated direction.
    ON_3dVector kdir = kv; kdir.Unitize();
    const double axis_dot = ON_DotProduct(kdir, ON_3dVector(-1, 0, 0));
    char buf2[160];
    std::snprintf(buf2, sizeof(buf2), "G2 blend's curvature vector points toward the cylinder axis (dot = %.6f, want ~1)", axis_dot);
    Check(axis_dot > 0.999, buf2);
  }

  if (ok_g1) {
    // The OLD "Continuity=Curvature" (tangent-magnitude boost only) blend
    // must NOT already match the true curvature - this is exactly the gap
    // this change closes. A real difference here is what proves the two
    // constructions are actually different, not just relabelled.
    const ON_Interval bu = blend_g1.Domain(0), bv = blend_g1.Domain(1);
    const dino8::app::DirectionalDerivs dd = OutwardDirectionalDerivs(blend_g1, bu.Max(), bv.Mid());
    if (dd.ok) {
      const ON_3dVector kv = CurvatureVectorFromDerivs(dd.d1, dd.d2);
      const double kmag = kv.Length();
      const double rel_err = std::fabs(kmag - 1.0 / R) / (1.0 / R);
      char buf[220];
      std::snprintf(buf, sizeof(buf), "old G1 tangent-boost blend's curvature (%.6f) differs from the true 1/R = %.6f by %.1f%% - confirms it was never a real G2 match", kmag, 1.0 / R, 100.0 * rel_err);
      Check(rel_err > 0.20, buf);
    }
  }

  delete cylinder;
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
