// Brep::LoftWithSectionTangents: a v-derivative prescribed (with a scalar weight) at any chosen sections.
//
// Oracles (independent of the skinning code):
//  * Two coaxial circles with end fields T = (a x, a y, b): the wall is a cubic HERMITE patch in v, so radius and height
//    follow the closed-form Hermite polynomials h00, h10, h01, h11 evaluated here (rational circles have constant
//    weights, so every wall point scales radially with R(v) = r (h00 + h01) + r a0 w0 h10 + r a1 w1 h11, and
//    z(v) = z0 h00 + z1 h01 + b0 w0 h10 + b1 w1 h11). The capped volume is pi * integral R^2 z' dv by Simpson here.
//  * Interior-section tangent: derivative equals weight * field at the wall's own point (openNURBS Ev1Der), sections
//    reproduced by their implicit equations, radius exactly constant for an axial field on equal circles.
//  * Cross-check with Loft(start/end tangent): same data, degree 2 and three sections give the identical wall.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;
using Spacing = Brep::LoftSpacing;
using Tan = Brep::LoftSectionTangent;

NurbsCurve Section(double z, double r) {
  const ON_Circle circle(ON_Plane(Point3d(0, 0, z), Vector3d(0, 0, 1)), r);
  ON_NurbsCurve n;
  circle.GetNurbForm(n);
  NurbsCurve k;
  k.raw() = n;
  return k;
}

// Field (a x, a y, b) over the section's own control structure (see loft_rational_tangent.cpp).
NurbsCurve Field(const NurbsCurve& sec, double a, double b) {
  ON_NurbsCurve t = sec.raw();
  for (int i = 0; i < t.CVCount(); ++i) {
    ON_3dPoint p;
    t.GetCV(i, p);
    const double w = t.Weight(i);
    t.SetCV(i, ON_4dPoint(a * p.x * w, a * p.y * w, b * w, w));
  }
  NurbsCurve k;
  k.raw() = t;
  return k;
}

Tan MakeTan(int section, const NurbsCurve& c, double w) {
  Tan t;
  t.section = section;
  t.tangent = c;
  t.weight = w;
  return t;
}

const ON_NurbsSurface* Wall(const Brep& b) { return ON_NurbsSurface::Cast(b.raw().m_F[0].SurfaceOf()); }

bool ThrowsWith(const std::function<void()>& f, const char* needle) {
  try {
    f();
  } catch (const std::invalid_argument& e) {
    return std::string(e.what()).find(needle) != std::string::npos;
  }
  return false;
}

struct Hermite {
  static double h00(double v) { return 2 * v * v * v - 3 * v * v + 1; }
  static double h10(double v) { return v * v * v - 2 * v * v + v; }
  static double h01(double v) { return -2 * v * v * v + 3 * v * v; }
  static double h11(double v) { return v * v * v - v * v; }
  static double d00(double v) { return 6 * v * v - 6 * v; }
  static double d10(double v) { return 3 * v * v - 4 * v + 1; }
  static double d01(double v) { return -6 * v * v + 6 * v; }
  static double d11(double v) { return 3 * v * v - 2 * v; }
};

}  // namespace

D8_EXTRA_TEST(LoftSectionTangentsTwoCirclesAreAnExactHermiteBarrel) {
  const double r = 1.0, z0 = 0.0, z1 = 2.0;
  const double a0 = 0.6, b0 = 1.0, w0 = 1.5, a1 = -0.6, b1 = 1.0, w1 = 0.8;
  const std::vector<NurbsCurve> secs = {Section(z0, r), Section(z1, r)};
  const std::vector<Tan> tans = {MakeTan(0, Field(secs[0], a0, b0), w0), MakeTan(1, Field(secs[1], a1, b1), w1)};
  Brep barrel;
  bool built = true;
  try {
    barrel = Brep::LoftWithSectionTangents(secs, tans, 3, false);
  } catch (const std::exception& e) {
    built = false;
    std::fprintf(stderr, "threw: %s\n", e.what());
  }
  d8test::Check(built, "two sections with a tangent at each end are accepted (cubic Hermite wall)");
  if (!built) return;
  const ON_NurbsSurface* wall = Wall(barrel);
  d8test::Check(wall != nullptr && wall->CVCount(1) == 4 && wall->Order(1) == 4, "the wall is a cubic Bezier in v (4 control rows)");
  if (!wall) return;
  double worst_r = 0.0, worst_z = 0.0, worst_d0 = 0.0, worst_d1 = 0.0;
  const ON_Interval du = wall->Domain(0), dv = wall->Domain(1);
  for (int i = 0; i <= 48; ++i) {
    for (int j = 0; j <= 20; ++j) {
      const double v = j / 20.0;
      const ON_3dPoint p = wall->PointAt(du.ParameterAt(i / 48.0), dv.ParameterAt(v));
      const double R = r * (Hermite::h00(v) + Hermite::h01(v)) + r * a0 * w0 * Hermite::h10(v) + r * a1 * w1 * Hermite::h11(v);
      const double Z = z0 * Hermite::h00(v) + z1 * Hermite::h01(v) + b0 * w0 * Hermite::h10(v) + b1 * w1 * Hermite::h11(v);
      worst_r = std::max(worst_r, std::fabs(std::hypot(p.x, p.y) - R));
      worst_z = std::max(worst_z, std::fabs(p.z - Z));
    }
    ON_3dPoint pt;
    ON_3dVector su, sv;
    wall->Ev1Der(du.ParameterAt(i / 48.0), 0.0, pt, su, sv);
    worst_d0 = std::max(worst_d0, (sv - ON_3dVector(w0 * a0 * pt.x, w0 * a0 * pt.y, w0 * b0)).Length());
    wall->Ev1Der(du.ParameterAt(i / 48.0), 1.0, pt, su, sv);
    worst_d1 = std::max(worst_d1, (sv - ON_3dVector(w1 * a1 * pt.x, w1 * a1 * pt.y, w1 * b1)).Length());
  }
  d8test::Check(worst_r < 1e-12, "wall radius equals the closed-form Hermite R(v) to 1e-12 over a 49 x 21 grid");
  d8test::Check(worst_z < 1e-12, "wall height equals the closed-form Hermite z(v) to 1e-12");
  d8test::Check(worst_d0 < 1e-9 && worst_d1 < 1e-9, "dS/dv at v = 0 and v = 1 equals weight * field at the wall's own point to 1e-9");

  // Capped: pi * integral R^2 z' dv (Simpson, 4000 intervals) against the kernel's volume.
  const Brep solid = Brep::LoftWithSectionTangents(secs, tans, 3, true);
  d8test::Check(solid.FaceCount() == 3 && solid.raw().IsValid() && solid.raw().IsSolid(), "capped: a valid closed solid of 3 faces");
  auto integrand = [&](double v) {
    const double R = r * (Hermite::h00(v) + Hermite::h01(v)) + r * a0 * w0 * Hermite::h10(v) + r * a1 * w1 * Hermite::h11(v);
    const double dz = z0 * Hermite::d00(v) + z1 * Hermite::d01(v) + b0 * w0 * Hermite::d10(v) + b1 * w1 * Hermite::d11(v);
    return M_PI * R * R * dz;
  };
  const int n = 4000;
  double simpson = integrand(0.0) + integrand(1.0);
  for (int k = 1; k < n; ++k) simpson += integrand(static_cast<double>(k) / n) * (k % 2 ? 4.0 : 2.0);
  simpson /= 3.0 * n;
  std::printf("barrel volume: oracle %.9f kernel %.9f\n", simpson, solid.Volume());
  d8test::Check(std::fabs(solid.Volume() - simpson) <= 1e-5 * simpson, "capped volume equals pi * integral R(v)^2 z'(v) dv to 1e-5 relative");
  // The tangent weights really are the bulge: doubling both flares changes the volume the way the formula says.
  const std::vector<Tan> heavy = {MakeTan(0, Field(secs[0], a0, b0), 2.0 * w0), MakeTan(1, Field(secs[1], a1, b1), 2.0 * w1)};
  const Brep solid2 = Brep::LoftWithSectionTangents(secs, heavy, 3, true);
  d8test::Check(std::fabs(solid2.Volume() - solid.Volume()) > 0.05 * solid.Volume(), "control: doubling both weights changes the volume by more than 5 percent");
}

D8_EXTRA_TEST(LoftSectionTangentsInteriorSectionAndWeightScaling) {
  const double r = 1.0;
  const double zs[3] = {0.0, 1.0, 3.0};
  std::vector<NurbsCurve> secs;
  for (double z : zs) secs.push_back(Section(z, r));
  double zp_w1 = 0.0, zp_w2 = 0.0;
  for (double w : {1.0, 2.0}) {
    const std::vector<Tan> tans = {MakeTan(1, Field(secs[1], 0.0, 1.0), w)};
    const Brep b = Brep::LoftWithSectionTangents(secs, tans, 3, false, Spacing::Uniform);
    const ON_NurbsSurface* wall = Wall(b);
    d8test::Check(wall != nullptr && wall->CVCount(1) == 4, "a tangent at the middle section only: 3 sections + 1 derivative = 4 control rows");
    if (!wall) return;
    const ON_Interval du = wall->Domain(0), dv = wall->Domain(1);
    double radial = 0.0, mid = 0.0, deriv = 0.0, ends = 0.0;
    for (int i = 0; i <= 64; ++i) {
      for (int j = 0; j <= 20; ++j) {
        const ON_3dPoint p = wall->PointAt(du.ParameterAt(i / 64.0), dv.ParameterAt(j / 20.0));
        radial = std::max(radial, std::fabs(std::hypot(p.x, p.y) - r));
      }
      const double u = du.ParameterAt(i / 64.0);
      const ON_3dPoint m = wall->PointAt(u, 0.5);
      mid = std::max(mid, std::fabs(m.z - zs[1]));
      ends = std::max(ends, std::fabs(wall->PointAt(u, 0.0).z - zs[0]) + std::fabs(wall->PointAt(u, 1.0).z - zs[2]));
      ON_3dPoint pt;
      ON_3dVector su, sv;
      wall->Ev1Der(u, 0.5, pt, su, sv);
      deriv = std::max(deriv, (sv - ON_3dVector(0.0, 0.0, w)).Length());
      if (i == 0) (w == 1.0 ? zp_w1 : zp_w2) = sv.z;
    }
    d8test::Check(radial < 1e-12, "equal circles with an axial field keep the radius exactly r everywhere");
    d8test::Check(mid < 1e-12 && ends < 1e-12, "all three sections are reproduced at v = 0, 1/2, 1 (heights to 1e-12)");
    d8test::Check(deriv < 1e-9, "dS/dv at the middle section equals weight * (0, 0, 1) to 1e-9");
  }
  d8test::Check(std::fabs(zp_w2 - 2.0 * zp_w1) < 1e-9, "doubling the weight doubles the interior derivative exactly");
}

D8_EXTRA_TEST(LoftSectionTangentsAgreeWithLoftStartEndTangentsAndRefuseWithReasons) {
  std::vector<NurbsCurve> secs;
  for (double z : {0.0, 1.0, 3.0}) secs.push_back(Section(z, 1.0 + 0.5 * z));
  const NurbsCurve t0 = Field(secs.front(), 0.5, 1.0), t1 = Field(secs.back(), 0.7, 2.5);
  const Brep via_loft = Brep::Loft(secs, 2, false, false, &t0, &t1, Spacing::Uniform);
  const Brep via_new = Brep::LoftWithSectionTangents(secs, {MakeTan(0, t0, 1.0), MakeTan(2, t1, 1.0)}, 2, false, Spacing::Uniform);
  const ON_NurbsSurface *wa = Wall(via_loft), *wb = Wall(via_new);
  double dev = 1e9;
  if (wa && wb) {
    dev = 0.0;
    for (int i = 0; i <= 32; ++i)
      for (int j = 0; j <= 16; ++j)
        dev = std::max(dev, wa->PointAt(wa->Domain(0).ParameterAt(i / 32.0), wa->Domain(1).ParameterAt(j / 16.0))
                                .DistanceTo(wb->PointAt(wb->Domain(0).ParameterAt(i / 32.0), wb->Domain(1).ParameterAt(j / 16.0))));
  }
  d8test::Check(dev < 1e-12, "degree 2, tangents at the two ends: identical to Loft(start_tangent, end_tangent) to 1e-12");

  const Tan ok = MakeTan(0, t0, 1.0);
  d8test::Check(ThrowsWith([&] { Brep::LoftWithSectionTangents(secs, {}, 2, false); }, "at least one section tangent"), "no tangents: refused");
  d8test::Check(ThrowsWith([&] { Brep::LoftWithSectionTangents(secs, {ok, ok}, 2, false); }, "same section"), "two tangents for one section: refused");
  d8test::Check(ThrowsWith([&] { Brep::LoftWithSectionTangents(secs, {MakeTan(3, t0, 1.0)}, 2, false); }, "outside the section list"), "section index out of range: refused");
  d8test::Check(ThrowsWith([&] { Brep::LoftWithSectionTangents(secs, {MakeTan(0, t0, std::numeric_limits<double>::quiet_NaN())}, 2, false); }, "finite"),
                "NaN weight: refused");
  d8test::Check(ThrowsWith([&] { Brep::LoftWithSectionTangents(secs, {ok}, 1, false); }, "degree >= 2"), "degree 1: refused");
  ON_NurbsCurve plain = t0.raw();
  plain.MakeNonRational();
  NurbsCurve plain_curve;
  plain_curve.raw() = plain;
  d8test::Check(ThrowsWith([&] { Brep::LoftWithSectionTangents(secs, {MakeTan(0, plain_curve, 1.0)}, 2, false); }, "cannot be matched exactly"),
                "a varying non-rational tangent against a rational section: refused with the weights reason");
}
