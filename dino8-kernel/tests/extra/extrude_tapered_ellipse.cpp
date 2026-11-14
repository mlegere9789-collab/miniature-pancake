// Brep::ExtrudeTapered (draft angle) of a CURVED, non-circular profile: an ellipse a = 3, b = 2 extruded L = 2 with a 5 degree draft.
//
// The profile is neither a line, a circle/arc nor a polyline, so the top section comes from OffsetInPlane's general fitted branch.
// Oracle independent of that fit - Steiner's formula for a CONVEX curve's parallel curves: the inward offset at distance d has
//   area(d) = A - P d + pi d^2     (A area, P perimeter; exact while d < the smallest radius of curvature b^2/a = 1.333),
// and the drafted wall moves every point inward by h tan(alpha) at height h, so
//   V = int_0^L area(h t) dh = A L - P t L^2 / 2 + pi t^2 L^3 / 3,        t = tan(alpha).
// P is a numeric integral of |c'(theta)| (Simpson, 20000 steps), A = pi a b. Also: the top section is the parallel curve - every
// sampled top point is at distance L tan(alpha) from the base ellipse (distance to a dense polyline of the base), and the
// uncapped wall edge at the base is the ellipse itself. Sign check: a NEGATIVE angle flares the profile out (V with t -> -t).
#include <cmath>
#include <cstdio>
#include <string>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Vector3d;

constexpr double kPi = 3.14159265358979323846;

NurbsCurve Ellipse(double a, double b) {
  ON_Ellipse e(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0)), a, b);
  ON_NurbsCurve nc;
  e.GetNurbForm(nc);
  NurbsCurve c;
  c.raw() = nc;
  return c;
}

double Perimeter(double a, double b) {
  const int n = 20000;
  double sum = 0.0;
  for (int i = 0; i <= n; ++i) {
    const double th = 2.0 * kPi * i / n;
    const double f = std::sqrt(a * a * std::sin(th) * std::sin(th) + b * b * std::cos(th) * std::cos(th));
    sum += (i == 0 || i == n ? 1.0 : (i % 2 ? 4.0 : 2.0)) * f;
  }
  return sum * (2.0 * kPi / n) / 3.0;
}

double Steiner(double a, double b, double L, double t) {
  return kPi * a * b * L - Perimeter(a, b) * t * L * L / 2.0 + kPi * t * t * L * L * L / 3.0;
}

}  // namespace

D8_EXTRA_TEST(ExtrudeTaperedEllipseSteinerVolume) {
  const double a = 3.0, b = 2.0, L = 2.0;
  for (const double deg : {5.0, 10.0, -5.0}) {
    const double alpha = deg * kPi / 180.0;
    const double t = std::tan(alpha);
    bool built = true;
    double vol = 0.0;
    std::string why;
    try {
      vol = Brep::ExtrudeTapered(Ellipse(a, b), Vector3d(0, 0, L), alpha, true).Volume();
    } catch (const std::exception& e) {
      built = false;
      why = e.what();
    }
    const double exact = Steiner(a, b, L, t);
    const double rel = built ? std::fabs(vol - exact) / exact : 1.0;
    std::printf("  [tapered ellipse] %+.0f deg: volume %.6f, Steiner %.6f, rel %.3g %s\n", deg, vol, exact, rel, why.c_str());
    d8test::Check(built, ("ellipse draft " + std::to_string(deg) + " deg builds: " + why).c_str());
    d8test::Check(rel < 2e-4, ("ellipse draft " + std::to_string(deg) + " deg: capped volume matches Steiner's parallel-curve formula to 2e-4").c_str());
  }
}

D8_EXTRA_TEST(ExtrudeTaperedEllipseTopSectionIsTheParallelCurve) {
  const double a = 3.0, b = 2.0, L = 2.0, alpha = 5.0 * kPi / 180.0;
  const double d = L * std::tan(alpha);
  const Brep wall = Brep::ExtrudeTapered(Ellipse(a, b), Vector3d(0, 0, L), alpha, false);
  const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(wall.raw().m_S[0]);
  d8test::Check(ns != nullptr, "the drafted ellipse wall is a NURBS surface");
  if (ns == nullptr) return;
  // dense polyline of the base ellipse
  const int m = 4000;
  std::vector<ON_3dPoint> base;
  for (int i = 0; i < m; ++i) base.emplace_back(a * std::cos(2.0 * kPi * i / m), b * std::sin(2.0 * kPi * i / m), 0.0);
  auto dist_to_base = [&](const ON_3dPoint& p) {
    double best = 1e300;
    for (int i = 0; i < m; ++i) {
      const ON_3dPoint& p0 = base[static_cast<size_t>(i)];
      const ON_3dPoint& p1 = base[static_cast<size_t>((i + 1) % m)];
      ON_Line l(p0, p1);
      double s;
      l.ClosestPointTo(ON_3dPoint(p.x, p.y, 0.0), &s);
      s = std::min(1.0, std::max(0.0, s));
      best = std::min(best, l.PointAt(s).DistanceTo(ON_3dPoint(p.x, p.y, 0.0)));
    }
    return best;
  };
  // wall v = height direction (degree 1): find which parameter direction is the height
  const int hdir = ns->Degree(1) == 1 ? 1 : 0;
  double worst_base = 0.0, worst_top = 0.0;
  const ON_Interval du = ns->Domain(1 - hdir), dh = ns->Domain(hdir);
  for (int i = 0; i < 200; ++i) {
    const double u = du.ParameterAt(i / 200.0);
    const ON_3dPoint pb = hdir == 1 ? ns->PointAt(u, dh.Min()) : ns->PointAt(dh.Min(), u);
    const ON_3dPoint pt = hdir == 1 ? ns->PointAt(u, dh.Max()) : ns->PointAt(dh.Max(), u);
    worst_base = std::max(worst_base, dist_to_base(pb));
    worst_top = std::max(worst_top, std::fabs(dist_to_base(pt) - d));
    // the top section lies at height L
    worst_top = std::max(worst_top, std::fabs(pt.z - L));
  }
  std::printf("  [tapered ellipse] base-edge distance to the ellipse %.3g; top section distance error to the parallel curve %.3g (d = %.6f)\n", worst_base, worst_top, d);
  d8test::Check(worst_base < 1e-5, "the wall's base edge is the ellipse (within 1e-5 of the dense polyline)");
  d8test::Check(worst_top < 2e-4, "every sampled top point is exactly L*tan(alpha) inside the base ellipse, at height L (2e-4)");
}

// Lateral area of the drafted wall: the wall at height h has perimeter P - 2 pi h t (inward Steiner offset of a convex curve) and the
// slant factor is 1/cos(alpha), so A_wall = (L / cos a) (P - pi t L). Measured by dense midpoint quadrature of |Su x Sv| on the wall
// surface itself (not Brep::Area()). Before the same-parameter top section the wall pairing was off by a tangential shift and the area
// read 0.07% low at 5 degrees (and 0.05% low even at a 1e-4 rad draft, where the true wall is the plain cylinder).
D8_EXTRA_TEST(ExtrudeTaperedEllipseWallAreaClosedForm) {
  const double a = 3.0, b = 2.0, L = 2.0;
  for (const double deg : {0.005729578, 5.0, 10.0, -5.0}) {
    const double al = deg * kPi / 180.0, t = std::tan(al);
    const Brep wall = Brep::ExtrudeTapered(Ellipse(a, b), Vector3d(0, 0, L), al, false);
    const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(wall.raw().m_S[0]);
    d8test::Check(ns != nullptr, "the drafted ellipse wall is a NURBS surface");
    if (ns == nullptr) continue;
    const ON_Interval du = ns->Domain(0), dv = ns->Domain(1);
    const int nu = 3000, nv = 20;
    double area = 0.0;
    for (int i = 0; i < nu; ++i) {
      for (int j = 0; j < nv; ++j) {
        ON_3dPoint p;
        ON_3dVector su, sv;
        ns->Ev1Der(du.ParameterAt((i + 0.5) / nu), dv.ParameterAt((j + 0.5) / nv), p, su, sv);
        area += ON_CrossProduct(su, sv).Length() * du.Length() / nu * dv.Length() / nv;
      }
    }
    const double exact = (L / std::cos(al)) * (Perimeter(a, b) - kPi * t * L);
    const double rel = std::fabs(area - exact) / exact;
    std::printf("  [tapered ellipse] %+.4f deg: wall area %.6f closed form %.6f rel %.3g\n", deg, area, exact, rel);
    d8test::Check(rel < 5e-5, ("ellipse draft " + std::to_string(deg) + " deg: the wall area is the closed form (L/cos a)(P - pi t L) to 5e-5").c_str());
  }
}
