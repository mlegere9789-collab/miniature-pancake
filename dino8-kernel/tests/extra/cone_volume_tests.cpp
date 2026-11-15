// Exact volume of a cone with a TRIMMED lateral face (VolumeOfSealedSolid). The lateral face of Brep::Revolve's cone is one periodic,
// apex-singular face; once a boolean trims it, the volume used to come out wrong by several percent (a round boss whose footprint
// straddled the seam: +5.9%; other angles -1.4%), because the outer loop then carries the hole as a notch of a dense, nearly collinear
// polyline and the ear clipper dropped area. Expected values are closed forms / quadratures of the construction.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/boss.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineGeneralSealed;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::MakeBoss;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }
bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// Cone: base radius 4 at z = 0, apex at z = 6 (revolved triangle).
Brep MakeCone() {
  NurbsCurve tri = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(4, 0, 0), P(0, 0, 6), P(0, 0, 0)}, 1);
  return Brep::Revolve(tri, P(0, 0, 0), Vector3d(0, 0, 1));
}
double Rho(double z) { return 4.0 * (1.0 - z / 6.0); }

NurbsCurve CircleCurve(const ON_3dPoint& c, const ON_3dVector& n, double r) {
  const ON_Circle circle(ON_Plane(c, n), r);
  ON_NurbsCurve nurbs;
  circle.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

}  // namespace

// The untrimmed cone is the baseline: volume pi r^2 h / 3.
D8_EXTRA_TEST(ConeBareVolumeMatchesClosedForm) {
  const Brep cone = MakeCone();
  d8test::Check(Near(cone.VolumeOfSealedSolid(), ON_PI * 16.0 * 6.0 / 3.0, 1e-4), "bare cone: V = pi r^2 h / 3 = 100.531");
}

// Cone cut by the half-space x >= 1 (kept x <= 1): for each z the disc of radius rho(z) cut at x = 1 leaves the circular segment
// rho^2 acos(-a / rho) + a sqrt(rho^2 - a^2), a = 1.
D8_EXTRA_TEST(ConeCutByPlaneTrimmedLateralFaceVolume) {
  const Brep cone = MakeCone();
  const auto s = BooleanCombineGeneralSealed(cone, Brep::Box(1, -10, -10, 10, 10, 10), BooleanOp::Difference);
  double expect = 0;
  const int n = 40000;
  for (int i = 0; i < n; ++i) {
    const double z = (i + 0.5) * 6.0 / n, rho = Rho(z), a = 1.0;
    const double area = a >= rho ? ON_PI * rho * rho : rho * rho * std::acos(-a / rho) + a * std::sqrt(rho * rho - a * a);
    expect += area * 6.0 / n;
  }
  const double v = s.brep.VolumeOfSealedSolid();
  d8test::Check(s.brep.raw().IsValid() && s.brep.raw().IsSolid(), "cone cut at x = 1: valid closed solid");
  d8test::Check(Near(v, expect, 1e-3 * expect), ("cone cut at x = 1: volume " + std::to_string(v) + " vs quadrature " + std::to_string(expect)).c_str());
}

// Cone cut by the plane z >= 2: a frustum with radii rho(2) = 8/3 and 0 over height 4, pi (8/3)^2 4 / 3.
D8_EXTRA_TEST(ConeCutPerpendicularToAxisIsAConeFrustumVolume) {
  const Brep cone = MakeCone();
  try {
    const auto s = BooleanCombineGeneralSealed(cone, Brep::Box(-10, -10, -10, 10, 10, 2), BooleanOp::Difference);
    const double expect = ON_PI * (8.0 / 3.0) * (8.0 / 3.0) * 4.0 / 3.0;
    d8test::Check(Near(s.brep.VolumeOfSealedSolid(), expect, 1e-3 * expect), ("cone above z = 2: volume " + std::to_string(s.brep.VolumeOfSealedSolid()) + " vs " + std::to_string(expect)).c_str());
  } catch (const std::exception& e) {
    d8test::Check(std::string(e.what()).find("BooleanCombineGeneral") != std::string::npos, "cone above z = 2: honest refusal");
  }
}

// Round boss on the cone: base disc (radius 0.8, centre z = 2) at 1.5 from the axis, top at 4.5, axis direction at `deg` degrees. The cone
// is rotation-symmetric, so every angle has the same added volume (quadrature over the footprint disc).
D8_EXTRA_TEST(ConeRoundBossAddedVolumeIsRotationInvariant) {
  const Brep cone = MakeCone();
  const double rb = 0.8, z0 = 2.0, xb = 1.5, xt = 4.5;
  double expect = 0;
  const int nr = 600, nt = 600;
  for (int i = 0; i < nr; ++i) {
    const double rho = (i + 0.5) * rb / nr;
    for (int j = 0; j < nt; ++j) {
      const double a = 2.0 * ON_PI * (j + 0.5) / nt;
      const double y = rho * std::cos(a), z = z0 + rho * std::sin(a), r = Rho(z);
      expect += (xt - std::sqrt(r * r - y * y)) * rho * (rb / nr) * (2.0 * ON_PI / nt);
    }
  }
  for (const int deg : {0, 90, 180, 45}) {  // 0: the footprint straddles the cone's seam
    const double a = deg * ON_PI / 180.0;
    const ON_3dVector n(std::cos(a), std::sin(a), 0);
    const std::string label = "round boss on cone at " + std::to_string(deg) + " deg: ";
    try {
      const auto b = MakeBoss(cone, CircleCurve(ON_3dPoint(xb * n.x, xb * n.y, z0), n, rb), Vector3d(n.x, n.y, n.z), xt - xb);
      d8test::Check(Near(b.added_volume, expect, 0.005 * expect),
                    (label + "added " + std::to_string(b.added_volume) + " vs quadrature " + std::to_string(expect) + " (bound 0.5%)").c_str());
    } catch (const std::exception& e) {
      d8test::Check(false, (label + "was refused: " + e.what()).c_str());
    }
  }
}

// Rectangular boss on the seam side: y in [-0.6, 0.6], z in [1.4, 2.6], base x = 1.5, top x = 4.5.
D8_EXTRA_TEST(ConeRectangularBossAddedVolumeMatchesQuadrature) {
  const Brep cone = MakeCone();
  const double xb = 1.5, xt = 4.5;
  double expect = 0;
  const int n = 800;
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
      const double y = -0.6 + (i + 0.5) * 1.2 / n, z = 1.4 + (j + 0.5) * 1.2 / n, r = Rho(z);
      expect += (xt - std::sqrt(r * r - y * y)) * (1.2 / n) * (1.2 / n);
    }
  try {
    const auto b = MakeBoss(cone, std::vector<Point3d>{P(xb, -0.6, 1.4), P(xb, 0.6, 1.4), P(xb, 0.6, 2.6), P(xb, -0.6, 2.6)}, Vector3d(1, 0, 0), xt - xb);
    d8test::Check(Near(b.added_volume, expect, 0.005 * expect), ("rect boss on cone: added " + std::to_string(b.added_volume) + " vs quadrature " + std::to_string(expect) + " (bound 0.5%)").c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("rect boss on cone was refused: ") + e.what()).c_str());
  }
}
