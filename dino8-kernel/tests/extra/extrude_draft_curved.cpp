// Brep::ExtrudeTapered on a CLOSED CURVED profile that is not a circle or a polyline (an ellipse).
//
// The true drafted body has, at height h along the extrusion, the exact parallel curve of the profile at inward distance
// s = h tan(alpha): the wall is S(u, t) = P(u) + t L e_z - t L tan(alpha) n(u) with n the profile's outward unit normal.
// Oracles written here, independent of the kernel's offset code:
//   * the pointwise family above, with P(u) from openNURBS' evaluation of the profile and n(u) from the ellipse's implicit
//     gradient (x/a^2, y/b^2);
//   * the volume by Steiner's formula for the parallel curves of a smooth convex curve: area(s) = A - s P + pi s^2, so
//     V = A L - P tan(alpha) L^2 / 2 + pi tan(alpha)^2 L^3 / 3 with A = pi a b and P the ellipse perimeter by Simpson here;
//   * the circle (an exact conic, the old path) still gives the exact frustum.
// Before the fix the wall of a 3 x 2 ellipse drafted 0.2 rad over 4 left this family by 0.417 (top row) because the refitted
// offset curve had its own parametrization and weights.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

NurbsCurve Ellipse(double a, double b) {
  ON_NurbsCurve n;
  ON_Circle(ON_Plane(Point3d(0, 0, 0), Vector3d(0, 0, 1)), 1.0).GetNurbForm(n);
  n.Transform(ON_Xform::DiagonalTransformation(a, b, 1.0));
  NurbsCurve k;
  k.raw() = n;
  return k;
}

double EllipsePerimeter(double a, double b) {
  const int n = 20000;
  auto f = [&](double t) { return std::hypot(a * std::sin(t), b * std::cos(t)); };
  double s = f(0.0) + f(2 * M_PI);
  for (int k = 1; k < n; ++k) s += f(2 * M_PI * k / n) * (k % 2 ? 4.0 : 2.0);
  return s * (2 * M_PI / n) / 3.0;
}

// Largest distance between the face-0 wall and P(u) + t * direction - t * |direction| * tan(alpha) * n(u); also the part of
// it measured on the base row (t = 0) and the top row (t = 1).
void WallVsFamily(const Brep& body, const NurbsCurve& prof, double a, double b, const ON_3dVector& direction, double alpha, double* all,
                  double* base, double* top, double side = 1.0) {
  const ON_NurbsSurface* wall = ON_NurbsSurface::Cast(body.raw().m_F[0].SurfaceOf());
  *all = *base = *top = 1e9;
  if (!wall) return;
  *all = *base = *top = 0.0;
  const ON_Interval du = wall->Domain(0), dv = wall->Domain(1), pd = prof.raw().Domain();
  const double L = direction.Length();
  for (int i = 0; i <= 96; ++i) {
    const double fu = i / 96.0;
    const ON_3dPoint p = prof.raw().PointAt(pd.ParameterAt(fu));
    ON_3dVector n(p.x / (a * a), p.y / (b * b), 0.0);
    n.Unitize();
    for (int j = 0; j <= 16; ++j) {
      const double t = j / 16.0;
      const ON_3dPoint got = wall->PointAt(du.ParameterAt(fu), dv.ParameterAt(t));
      const ON_3dPoint want = p + direction * t - n * (side * t * L * std::tan(alpha));
      const double e = got.DistanceTo(want);
      *all = std::max(*all, e);
      if (j == 0) *base = std::max(*base, e);
      if (j == 16) *top = std::max(*top, e);
    }
  }
}

bool ThrowsWith(const std::function<void()>& f, const char* needle) {
  try {
    f();
  } catch (const std::invalid_argument& e) {
    return std::string(e.what()).find(needle) != std::string::npos;
  }
  return false;
}

double SteinerVolume(double a, double b, double L, double alpha) {
  const double A = M_PI * a * b, P = EllipsePerimeter(a, b), t = std::tan(alpha);
  return A * L - P * t * L * L / 2.0 + M_PI * t * t * L * L * L / 3.0;
}

}  // namespace

D8_EXTRA_TEST(ExtrudeDraftEllipseWallIsTheExactDraftFamily) {
  const double a = 3.0, b = 2.0, L = 4.0;
  const NurbsCurve prof = Ellipse(a, b);
  for (double alpha : {0.2, -0.15}) {
    const std::string tag = "ellipse 3 x 2, L = 4, draft " + std::to_string(alpha) + " rad";
    const Brep body = Brep::ExtrudeTapered(prof, Vector3d(0, 0, L), alpha, false);
    double all, base, top;
    WallVsFamily(body, prof, a, b, ON_3dVector(0, 0, L), alpha, &all, &base, &top);
    d8test::Check(base < 1e-12, (tag + ": the base row is the profile itself (1e-12)").c_str());
    d8test::Check(all < 2e-5, (tag + ": the whole wall equals P(u) + t L e_z - t L tan(alpha) n(u) to 2e-5 over a 97 x 17 grid").c_str());
    std::printf("%s: wall deviation %.3e (top %.3e)\n", tag.c_str(), all, top);
  }
}

D8_EXTRA_TEST(ExtrudeDraftEllipseVolumeFollowsSteiner) {
  const double a = 3.0, b = 2.0, L = 4.0;
  const NurbsCurve prof = Ellipse(a, b);
  for (double alpha : {0.2, -0.15, 0.05}) {
    const Brep solid = Brep::ExtrudeTapered(prof, Vector3d(0, 0, L), alpha, true);
    const double want = SteinerVolume(a, b, L, alpha);
    d8test::Check(solid.FaceCount() == 3 && solid.raw().IsValid() && solid.raw().IsSolid(), "capped drafted ellipse: a valid closed solid of 3 faces");
    std::printf("draft %.2f: kernel volume %.6f Steiner %.6f\n", alpha, solid.Volume(), want);
    d8test::Check(std::fabs(solid.Volume() - want) <= 1e-6 * want, ("capped volume equals A L - P tan(alpha) L^2/2 + pi tan^2 L^3/3 (alpha " + std::to_string(alpha) + ")").c_str());
  }
  // The circle (an exact conic, the older path) still gives the exact frustum.
  ON_NurbsCurve cn;
  ON_Circle(ON_Plane(Point3d(0, 0, 0), Vector3d(0, 0, 1)), 2.0).GetNurbForm(cn);
  NurbsCurve circle;
  circle.raw() = cn;
  const double alpha = 0.1, L3 = 3.0, r2 = 2.0 - L3 * std::tan(alpha);
  const Brep frustum = Brep::ExtrudeTapered(circle, Vector3d(0, 0, L3), alpha, true);
  const double fr = M_PI * L3 / 3.0 * (4.0 + 2.0 * r2 + r2 * r2);
  d8test::Check(std::fabs(frustum.Volume() - fr) <= 1e-6 * fr, "control: the circle still gives the exact frustum volume pi L/3 (r1^2 + r1 r2 + r2^2)");
}

D8_EXTRA_TEST(ExtrudeDraftEllipseObliqueAndFoldRefusal) {
  const double a = 3.0, b = 2.0;
  const NurbsCurve prof = Ellipse(a, b);
  // Oblique direction: every point also shears by (1, 0.5) per unit t * 4 (the family keeps the same offset magnitude, L = |direction|).
  const ON_3dVector dir(1.0, 0.5, 4.0);
  const double alpha = 0.1;
  const Brep body = Brep::ExtrudeTapered(prof, Vector3d(dir.x, dir.y, dir.z), alpha, false);
  double all, base, top;
  WallVsFamily(body, prof, a, b, dir, alpha, &all, &base, &top);
  d8test::Check(base < 1e-12 && all < 2e-5, "oblique direction: the wall equals P(u) + t d - t |d| tan(alpha) n(u) to 2e-5");
  // Fold: the ellipse's smallest radius of curvature is b^2 / a = 4/3. L tan(alpha) = 1.5 folds through it, 1.2 does not.
  const double fold_alpha = std::atan(1.5 / 4.0), ok_alpha = std::atan(1.2 / 4.0);
  d8test::Check(ThrowsWith([&] { Brep::ExtrudeTapered(prof, Vector3d(0, 0, 4.0), fold_alpha, true); }, "fold"),
                "negative control: L tan(alpha) = 1.5 exceeds the smallest radius of curvature 4/3 and is refused with the fold reason");
  d8test::Check(!ThrowsWith([&] { Brep::ExtrudeTapered(prof, Vector3d(0, 0, 4.0), ok_alpha, true); }, "fold"),
                "control: L tan(alpha) = 1.2 stays inside it and builds");
}

// OPEN smooth curved profile: an elliptical arc (the ellipse 3 x 2 trimmed to 0.2..0.7 of its domain, about 180 degrees). There is no inside,
// so the offset side is T x zaxis with the plane normal turned along the extrusion direction: for a counterclockwise parametrization that is the
// ellipse's outward normal (side +1), for the reversed curve the inward one (side -1). Open profiles give a wall only.
D8_EXTRA_TEST(ExtrudeDraftOpenEllipseArcWallIsTheExactDraftFamily) {
  const double a = 3.0, b = 2.0, L = 4.0;
  ON_NurbsCurve n = Ellipse(a, b).raw();
  const ON_Interval dom = n.Domain();
  d8test::Check(n.Trim(ON_Interval(dom.ParameterAt(0.2), dom.ParameterAt(0.7))), "fixture: the elliptical arc is trimmed from the NURBS ellipse");
  d8test::Check(!n.IsClosed(), "fixture: the arc is open");
  NurbsCurve arc;
  arc.raw() = n;
  NurbsCurve rev = arc;
  rev.raw().Reverse();
  struct Case {
    const NurbsCurve* c;
    double side;
    const char* tag;
    double alpha;
  };
  const Case cases[] = {{&arc, 1.0, "counterclockwise arc, draft 0.2", 0.2},
                        {&rev, -1.0, "reversed arc, draft 0.2", 0.2},
                        {&arc, 1.0, "counterclockwise arc, flared -0.15", -0.15}};
  for (const Case& cs : cases) {
    const Brep body = Brep::ExtrudeTapered(*cs.c, Vector3d(0, 0, L), cs.alpha, true);
    double all, base, top;
    WallVsFamily(body, *cs.c, a, b, ON_3dVector(0, 0, L), cs.alpha, &all, &base, &top, cs.side);
    std::printf("open arc %s: wall deviation %.3e\n", cs.tag, all);
    d8test::Check(body.FaceCount() == 1 && body.raw().IsValid(), (std::string("open ") + cs.tag + ": one wall face, no caps").c_str());
    d8test::Check(base < 1e-12, (std::string("open ") + cs.tag + ": the base row is the profile itself (1e-12)").c_str());
    d8test::Check(all < 2e-5, (std::string("open ") + cs.tag + ": the wall equals P(u) + t L e_z -/+ t L tan(alpha) n(u) to 2e-5").c_str());
  }
}
