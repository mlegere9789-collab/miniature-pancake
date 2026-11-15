// Brep::ExtrudeTapered on a CORNERED curved profile (an arc plus its chord).
//
// The exact drafted solid is the intersection of the drafted walls: each horizontal section is the INWARD parallel set of the profile
// with sharp (mitred) corners. For a convex profile with perimeter P, area A, smooth turning T (radians) and corners of exterior angles
// phi_i, the inward parallel area is A(s) = A - P s + c s^2 with c = T/2 + sum tan(phi_i / 2) (a circle: T = 2 pi, c = pi; a square:
// phi = pi/2 four times, c = 4; valid while s is below the inradius). With s = h tan(alpha):
//     V = A L - P tan(alpha) L^2 / 2 + c tan(alpha)^2 L^3 / 3.
// A mitred corner contributes tan(phi/2), NOT phi/2: the "pi tan^2 L^3 / 3 over all corners" form is the smooth-curve (rounded corner) one.
//
// A ruled loft between the profile and an offset curve cannot reproduce the trimmed corner walls of this body, so the kernel keeps the
// older OffsetInPlane refit for a profile with a tangent corner. This test MEASURES what that fallback does and pins it, so a regression (or an
// improvement) is visible.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

// The arc of radius r from -span to +span about +x (counterclockwise), then the chord back down the line x = r cos(span).
// span = pi/2 is the D shape (a semicircle and its diameter, corners of exterior angle 90 degrees).
NurbsCurve ArcAndChord(double r, double span) {
  ON_Arc arc(ON_Circle(ON_Plane(Point3d(0, 0, 0), Vector3d(0, 0, 1)), r), ON_Interval(-span, span));
  ON_NurbsCurve n;
  arc.GetNurbForm(n);
  NurbsCurve k;
  k.raw() = n;
  const Point3d top(r * std::cos(span), r * std::sin(span), 0.0), bottom(r * std::cos(span), -r * std::sin(span), 0.0);
  const NurbsCurve chord = NurbsCurve::FromControlPoints({top, bottom}, 1);
  k.Join(chord);
  return k;
}

// Exact drafted volume of ArcAndChord(r, span). Area: circle minus the removed cap (central angle 2 (pi - span)); perimeter: arc 2 r span plus
// chord 2 r sin(span); smooth turning T = 2 span; each of the two corners turns by phi = the angle between the arc's end tangent
// (-sin span, cos span) and the chord direction (0, -1), i.e. cos phi = -cos(span).
double ExactVolume(double r, double span, double L, double alpha) {
  const double removed = 2.0 * (M_PI - span);
  const double cap = 0.5 * r * r * (removed - std::sin(removed));
  const double A = M_PI * r * r - cap;
  const double P = r * 2.0 * span + 2.0 * r * std::sin(span);
  const double phi = std::acos(std::max(-1.0, std::min(1.0, -std::cos(span))));
  const double c = span + 2.0 * std::tan(0.5 * phi);
  const double t = std::tan(alpha);
  return A * L - P * t * L * L / 2.0 + c * t * t * L * L * L / 3.0;
}

// Runs ExtrudeTapered (capped) and prints what happened; returns the relative volume error, or -1 when it refused.
double Measure(const char* tag, const NurbsCurve& d, double r, double span, double L, double deg, std::string* refusal) {
  const double alpha = deg * M_PI / 180.0;
  try {
    const Brep solid = Brep::ExtrudeTapered(d, Vector3d(0, 0, L), alpha, true);
    const double want = ExactVolume(r, span, L, alpha), got = solid.Volume();
    const double rel = std::fabs(got - want) / want;
    std::printf("%s draft %.1f deg: kernel volume %.6f exact %.6f relative error %.3e valid=%d solid=%d\n", tag, deg, got, want, rel,
                solid.raw().IsValid() ? 1 : 0, solid.raw().IsSolid() ? 1 : 0);
    return rel;
  } catch (const std::exception& e) {
    if (refusal) *refusal = e.what();
    std::printf("%s draft %.1f deg REFUSED: %s\n", tag, deg, e.what());
    return -1.0;
  }
}

}  // namespace

D8_EXTRA_TEST(ExtrudeDraftCorneredCurvedProfileFallbackIsPinned) {
  const double r = 2.0, L = 3.0;
  const NurbsCurve dshape = ArcAndChord(r, 0.5 * M_PI);
  const NurbsCurve cut = ArcAndChord(r, 2.0 * M_PI / 3.0);  // arc -120..120 degrees, corners of exterior angle 60 degrees
  d8test::Check(dshape.IsClosed() && cut.IsClosed(), "fixtures: both arc-and-chord profiles are single closed NURBS curves");
  // MEASURED (probe, before this test): the fallback does not give an approximate solid for these profiles, it REFUSES at every draft angle from
  // 0.1 to 10 degrees (OffsetInPlane cannot offset a curve with a tangent corner between curved spans; it reads the corner as an offset-direction
  // flip). So the error bound of the fallback is "no result". Pinned: a refusal that names the corner reason, at the drafts below; if the
  // construction is ever extended to build the trimmed corner walls this test fails and the exact-volume check beneath it takes over.
  for (double deg : {0.1, 1.0, 5.0, 10.0}) {
    std::string d_why, c_why;
    const double e1 = Measure("D shape", dshape, r, 0.5 * M_PI, L, deg, &d_why);
    const double e2 = Measure("cut circle", cut, r, 2.0 * M_PI / 3.0, L, deg, &c_why);
    const std::string tag = " at draft " + std::to_string(deg) + " degrees";
    d8test::Check(e1 < 0.0 && d_why.find("tangent corner") != std::string::npos, ("D shape: refused naming the tangent corner" + tag).c_str());
    d8test::Check(e2 < 0.0 && c_why.find("tangent corner") != std::string::npos, ("cut circle: refused naming the tangent corner" + tag).c_str());
    if (e1 >= 0.0) d8test::Check(e1 < 1e-3, ("D shape built: volume within 0.1 percent of the exact mitred solid" + tag).c_str());
    if (e2 >= 0.0) d8test::Check(e2 < 1e-3, ("cut circle built: volume within 0.1 percent of the exact mitred solid" + tag).c_str());
  }
  // Control: the same machinery is exact for the closed smooth ellipse (see extrude_draft_curved.cpp) - a smooth profile does NOT hit this refusal.
  ON_NurbsCurve en;
  ON_Circle(ON_Plane(Point3d(0, 0, 0), Vector3d(0, 0, 1)), 1.0).GetNurbForm(en);
  en.Transform(ON_Xform::DiagonalTransformation(3.0, 2.0, 1.0));
  NurbsCurve ellipse;
  ellipse.raw() = en;
  d8test::Check(Brep::ExtrudeTapered(ellipse, Vector3d(0, 0, L), 0.1745, true).raw().IsSolid(), "control: a smooth ellipse drafts 10 degrees without the refusal");
}
