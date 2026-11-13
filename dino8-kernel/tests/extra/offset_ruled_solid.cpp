// OffsetRuledSolid(): whole-body offset of the ruled solid (loft with straight rulings) between two planar convex polygons.
// Independent closed-form oracles (NOT the kernel's own plane-moving):
//  - square frustum, half-width 5 at z=0 and 3 at z=8: side plane x = 5 - z/4, so moving every plane outward by d puts the
//    cross-section half-width at w(z) = 5 + d*sqrt(1+1/16) - z/4 on z in [-d, 8+d]; the volume is the integral of (2w)^2 dz,
//    done by Simpson's rule (exact for this quadratic) - a different formula from the prismatoid one used for the d=0 check;
//  - regular hexagon frustum (circumradius 4 -> 2 over height 6): apothem a(z) linear, area 2*sqrt(3)*a^2, same integral;
//  - d = 0 is the plain frustum volume h/3 (A1 + A2 + sqrt(A1 A2));
//  - the caps and rim sit exactly where the moved planes put them;
//  - honest refusals: top twisted 20 degrees (side quads not planar), mismatched counts, non-finite distance, a concave
//    profile, an inward offset that collapses the solid, zero height.
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::OffsetRuledSolid;
using dino8::kernel::Point3d;

std::vector<Point3d> Poly(int n, double R, double z, double rot, double cx = 0.0, double cy = 0.0) {
  std::vector<Point3d> p;
  for (int i = 0; i < n; ++i) {
    const double a = rot + 2.0 * M_PI * i / n;
    p.push_back(Point3d(cx + R * std::cos(a), cy + R * std::sin(a), z));
  }
  return p;
}
std::vector<Point3d> Square(double w, double z) {
  return {Point3d(w, w, z), Point3d(-w, w, z), Point3d(-w, -w, z), Point3d(w, -w, z)};
}
bool Throws(const std::vector<Point3d>& b, const std::vector<Point3d>& t, double d) {
  try {
    OffsetRuledSolid(b, t, d);
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}
// Simpson on f over [a,b]; exact for the quadratic cross-section areas here.
template <class F>
double Simpson(F f, double a, double b) {
  return (b - a) / 6.0 * (f(a) + 4.0 * f(0.5 * (a + b)) + f(b));
}
double Rel(double a, double b) { return std::fabs(a - b) / std::fabs(b); }

}  // namespace

D8_EXTRA_TEST(OffsetRuledSolidSquareFrustum) {
  const std::vector<Point3d> b = Square(5.0, 0.0), t = Square(3.0, 8.0);
  const double s = std::sqrt(1.0 + 1.0 / 16.0);
  {
    const Brep r = OffsetRuledSolid(b, t, 0.0);
    const double A1 = 100.0, A2 = 36.0;
    d8test::Check(r.raw().IsSolid(), "ruled frustum d=0: IsSolid");
    d8test::Check(Rel(r.Volume(), 8.0 / 3.0 * (A1 + A2 + std::sqrt(A1 * A2))) < 1e-9,
                  "ruled frustum d=0: volume = h/3 (A1 + A2 + sqrt(A1 A2)) = 349.33");
  }
  for (double d : {0.5, 1.0, 2.5, -0.5, -1.5}) {
    const Brep r = OffsetRuledSolid(b, t, d);
    auto area = [&](double z) {
      const double w = 5.0 + d * s - z / 4.0;
      return 4.0 * w * w;
    };
    const double expect = Simpson(area, -d, 8.0 + d);
    d8test::Check(r.raw().IsSolid(), "ruled frustum offset: IsSolid");
    d8test::Check(r.PlanarFaces().size() == 6, "ruled frustum offset: still 6 faces");
    d8test::Check(Rel(r.Volume(), expect) < 1e-9, "ruled frustum offset: volume matches the integral of (2w(z))^2");
  }
  {
    const Brep r = OffsetRuledSolid(b, t, 1.25);
    double zmin = 1e9, zmax = -1e9, xmax = -1e9;
    for (const auto& f : r.PlanarFaces())
      for (const Point3d& p : f.loop) {
        zmin = std::fmin(zmin, p.z);
        zmax = std::fmax(zmax, p.z);
      }
    for (const auto& f : r.PlanarFaces())
      for (const Point3d& p : f.loop)
        if (std::fabs(p.z - zmin) < 1e-9) xmax = std::fmax(xmax, p.x);
    d8test::Check(std::fabs(zmin + 1.25) < 1e-9 && std::fabs(zmax - 9.25) < 1e-9, "ruled frustum d=1.25: caps at z=-1.25 and 9.25");
    d8test::Check(std::fabs(xmax - (5.0 + 1.25 * s + 1.25 / 4.0)) < 1e-9, "ruled frustum d=1.25: bottom rim half-width 5 + d s + d/4");
  }
  {
    std::vector<Point3d> rb(b.rbegin(), b.rend()), rt(t.rbegin(), t.rend());
    d8test::Check(Rel(OffsetRuledSolid(rb, rt, 1.0).Volume(), OffsetRuledSolid(b, t, 1.0).Volume()) < 1e-12,
                  "ruled solid: reversed winding gives the same body");
  }
  d8test::Check(Rel(OffsetRuledSolid(t, b, 1.0).Volume(), OffsetRuledSolid(b, t, 1.0).Volume()) < 1e-12,
                "ruled solid: bottom/top swapped gives the same body");
}

D8_EXTRA_TEST(OffsetRuledSolidHexFrustum) {
  const double R0 = 4.0, R1 = 2.0, h = 6.0;
  const double a0 = R0 * std::cos(M_PI / 6), a1 = R1 * std::cos(M_PI / 6);
  const double k = (a0 - a1) / h, s = std::sqrt(1.0 + k * k);
  for (double d : {0.0, 0.7, -0.4}) {
    const Brep r = OffsetRuledSolid(Poly(6, R0, 0.0, 0.0), Poly(6, R1, h, 0.0), d);
    auto area = [&](double z) {
      const double a = a0 + d * s - k * z;
      return 2.0 * std::sqrt(3.0) * a * a;
    };
    d8test::Check(r.raw().IsSolid() && r.PlanarFaces().size() == 8, "hex frustum: 8-face solid");
    d8test::Check(Rel(r.Volume(), Simpson(area, -d, h + d)) < 1e-9, "hex frustum offset: volume matches integral of 2 sqrt(3) a(z)^2");
  }
  // a laterally shifted top is still a ruled solid with planar sides (parallel edges): Cavalieri volume at d=0
  const Brep sh = OffsetRuledSolid(Poly(4, 3.0, 0.0, M_PI / 4), Poly(4, 3.0, 5.0, M_PI / 4, 2.0, 1.0), 0.0);
  d8test::Check(Rel(sh.Volume(), 18.0 * 5.0) < 1e-9, "sheared prism d=0: volume = base area 18 x height 5 (Cavalieri)");
}

D8_EXTRA_TEST(OffsetRuledSolidRefuses) {
  const std::vector<Point3d> b = Square(5.0, 0.0), t = Square(3.0, 8.0);
  d8test::Check(Throws(b, Poly(4, 3.0 * std::sqrt(2.0), 8.0, 20.0 * M_PI / 180.0), 1.0), "twisted top: sides not planar -> refused");
  {
    // the refusal must come from the planarity check itself (not by luck from a later stage)
    std::string msg;
    try {
      OffsetRuledSolid(b, Poly(4, 3.0 * std::sqrt(2.0), 8.0, 20.0 * M_PI / 180.0), 1.0);
    } catch (const std::invalid_argument& e) {
      msg = e.what();
    }
    d8test::Check(msg.find("not planar") != std::string::npos, "twisted top: refusal names the non-planar ruled side");
  }
  d8test::Check(Throws(b, Poly(5, 3.0, 8.0, 0.0), 1.0), "mismatched vertex counts -> refused");
  d8test::Check(Throws(b, t, std::nan("")), "NaN distance -> refused");
  d8test::Check(Throws(b, t, INFINITY), "infinite distance -> refused");
  d8test::Check(Throws({Point3d(0, 0, 0), Point3d(1, 0, 0)}, {Point3d(0, 0, 1), Point3d(1, 0, 1)}, 0.1), "2-gon -> refused");
  const std::vector<Point3d> L0 = {Point3d(0, 0, 0), Point3d(4, 0, 0), Point3d(4, 2, 0), Point3d(2, 2, 0), Point3d(2, 4, 0), Point3d(0, 4, 0)};
  std::vector<Point3d> L1 = L0;
  for (Point3d& p : L1) p.z = 3.0;
  d8test::Check(Throws(L0, L1, 0.2), "concave L profile -> refused");
  d8test::Check(Throws(b, t, -4.5), "inward offset past the half height (8 + 2d < 0) -> refused");
  d8test::Check(Throws(b, Square(5.0, 0.0), 1.0), "both profiles on the same plane (no height) -> refused");
}
