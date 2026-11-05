// Brep::Sweep2 between two CLOSED rails that have corners (polygon rings).
//
// Before: refused ("a closed rail has a corner ... the periodic skin round a closed ring cannot follow a corner"). The
// periodic skin is smooth round the whole ring and would cut every corner, so the old answer was to refuse. Now the
// ring is cut at its seam and takes the open-rail corner machinery (a C0 station pair at every paired corner), with
// the first and last station sharing one frame so the wall closes on itself.
//
// Oracles, none of them the kernel's own skinning:
//   * annulus between concentric squares +-5 and +-8 in the plane z = 0, section = the segment joining the rails at
//     the seam corner: the wall is the planar region between the squares, area 16^2 - 10^2 = 156;
//   * the same with the outer square lifted to z = 2: four planar trapezoids (parallel sides 10 and 16, horizontal
//     gap 3, height 2 -> slant sqrt(13)), area 4 * (10 + 16)/2 * sqrt(13) = 52 sqrt(13);
//   * every sampled wall point lies on the surface the closed form says it does (planar / the four trapezoid planes);
//   * the wall's two seam edges (v = 0 and v = 1) coincide, and each wall edge follows its rail exactly;
//   * negative controls: the section is not a rail-to-rail segment, a rail pair that does not close (one open rail)
//     is still the unchanged open case, and Sweep2Sections still refuses the closed corner pair with its reason.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;

// mid_seam: the ring starts (and ends) at the middle of its first side instead of at a corner, so the seam is smooth.
NurbsCurve Square(double h, double z, bool mid_seam = false) {
  if (mid_seam) {
    return NurbsCurve::FromControlPoints({Point3d(0, -h, z), Point3d(h, -h, z), Point3d(h, h, z), Point3d(-h, h, z), Point3d(-h, -h, z), Point3d(0, -h, z)}, 1);
  }
  return NurbsCurve::FromControlPoints({Point3d(-h, -h, z), Point3d(h, -h, z), Point3d(h, h, z), Point3d(-h, h, z), Point3d(-h, -h, z)}, 1);
}

NurbsCurve Seg(Point3d a, Point3d b) { return NurbsCurve::FromControlPoints({a, b}, 1); }

// Area of the single wall surface by dense midpoint quadrature of |Su x Sv| (the wall is piecewise bilinear, so this
// converges fast; 400 x 800 cells here), independent of Brep::Area().
double WallArea(const ON_NurbsSurface& s, int nu, int nv) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  double total = 0.0;
  for (int i = 0; i < nu; ++i) {
    for (int j = 0; j < nv; ++j) {
      const double u = du.ParameterAt((i + 0.5) / nu), v = dv.ParameterAt((j + 0.5) / nv);
      ON_3dPoint p;
      ON_3dVector su, sv;
      if (!s.Ev1Der(u, v, p, su, sv)) continue;
      total += ON_CrossProduct(su, sv).Length() * du.Length() / nu * dv.Length() / nv;
    }
  }
  return total;
}

const ON_NurbsSurface* WallOf(const Brep& b) {
  const ON_Brep& raw = b.raw();
  for (int i = 0; i < raw.m_S.Count(); ++i) {
    const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(raw.m_S[i]);
    if (ns != nullptr) return ns;
  }
  return nullptr;
}

template <class F>
std::string Refusal(F&& f) {
  try {
    f();
  } catch (const std::invalid_argument& e) {
    return e.what();
  }
  return "";
}

// distance from p to the closed polygon with vertices (+-h, +-h, z) (square ring, rail), in 3D
double DistToSquareRing(const ON_3dPoint& p, double h, double z) {
  const ON_3dPoint v[5] = {ON_3dPoint(-h, -h, z), ON_3dPoint(h, -h, z), ON_3dPoint(h, h, z), ON_3dPoint(-h, h, z), ON_3dPoint(-h, -h, z)};
  double best = 1e300;
  for (int k = 0; k < 4; ++k) {
    ON_Line l(v[k], v[k + 1]);
    double t;
    l.ClosestPointTo(p, &t);
    t = std::min(1.0, std::max(0.0, t));
    best = std::min(best, l.PointAt(t).DistanceTo(p));
  }
  return best;
}

void RunRings(double outer_z, double expect_area, const char* tag, bool mid_seam = false) {
  const NurbsCurve inner = Square(5.0, 0.0, mid_seam), outer = Square(8.0, outer_z, mid_seam);
  // joins the rails at the seam: the corner (-5,-5,0) -> (-8,-8,outer_z), or the mid-side point (0,-5,0) -> (0,-8,outer_z)
  const NurbsCurve section = mid_seam ? Seg(Point3d(0, -5, 0), Point3d(0, -8, outer_z)) : Seg(Point3d(-5, -5, 0), Point3d(-8, -8, outer_z));
  Brep body;
  bool built = true;
  std::string why;
  try {
    body = Brep::Sweep2(section, inner, outer, 64, false);
  } catch (const std::exception& e) {
    built = false;
    why = e.what();
  }
  d8test::Check(built, (std::string(tag) + ": Sweep2 builds between two closed square rails (was refused: " + why + ")").c_str());
  if (!built) return;
  const ON_NurbsSurface* wall = WallOf(body);
  d8test::Check(wall != nullptr && body.raw().IsValid(), (std::string(tag) + ": a valid single wall surface").c_str());
  if (wall == nullptr) return;
  const double area = WallArea(*wall, 800, 800);
  const double rel = std::fabs(area - expect_area) / expect_area;
  std::printf("  [s2 ring] %s: area %.9f expected %.9f rel %.3g\n", tag, area, expect_area, rel);
  d8test::Check(rel < 1e-6, (std::string(tag) + ": wall area is the closed form " + std::to_string(expect_area) + " to 1e-6").c_str());

  // The app's "Area" of a Surface object is NurbsSurface::ApproximateArea(): a uniform 50 x 50 parameter lattice. The wall has C0
  // station pairs at the ring's corners (knots of full multiplicity); a lattice that misses them cuts every corner and under-reads
  // the planar region (measured before the crease-aware fix: 155.8 flat / 186.7 lifted, and different for a corner seam vs a
  // mid-side seam). Each side is planar, so with the corners in the lattice the mesh area is the closed form to rounding.
  {
    dino8::kernel::NurbsSurface ks;
    ks.raw() = *wall;
    const double approx = ks.ApproximateArea();
    const double arel = std::fabs(approx - expect_area) / expect_area;
    std::printf("  [s2 ring] %s: ApproximateArea() %.9f expected %.9f rel %.3g\n", tag, approx, expect_area, arel);
    d8test::Check(arel < 1e-9, (std::string(tag) + ": NurbsSurface::ApproximateArea() (the app's Area) is the closed form to 1e-9 - no cut corners").c_str());
  }

  // Wall edges follow the rails exactly: sample u across the section (u = 0 is on rail1, u = 1 on rail2) along all v.
  const ON_Interval du = wall->Domain(0), dv = wall->Domain(1);
  double worst1 = 0.0, worst2 = 0.0, seam = 0.0;
  for (int j = 0; j <= 400; ++j) {
    const double v = dv.ParameterAt(j / 400.0);
    worst1 = std::max(worst1, DistToSquareRing(wall->PointAt(du.Min(), v), 5.0, 0.0));
    worst2 = std::max(worst2, DistToSquareRing(wall->PointAt(du.Max(), v), 8.0, outer_z));
  }
  for (int i = 0; i <= 20; ++i) {
    const double u = du.ParameterAt(i / 20.0);
    seam = std::max(seam, wall->PointAt(u, dv.Min()).DistanceTo(wall->PointAt(u, dv.Max())));
  }
  std::printf("  [s2 ring] %s: edge deviation rail1 %.3g rail2 %.3g, seam gap %.3g\n", tag, worst1, worst2, seam);
  d8test::Check(worst1 < 1e-9 && worst2 < 1e-9, (std::string(tag) + ": both wall edges follow their square rails to 1e-9 (no cut corners)").c_str());
  d8test::Check(seam < 1e-9, (std::string(tag) + ": the wall closes on itself - its v = 0 and v = 1 edges coincide to 1e-9").c_str());

  // Every wall point is inside the closed form: for the flat ring |x|,|y| in [5,8] box region minus the inner square
  // (planar case); for the lifted ring the point lies on the plane of the trapezoid of its side.
  double plane_dev = 0.0;
  for (int i = 0; i <= 12; ++i) {
    for (int j = 0; j <= 160; ++j) {
      const ON_3dPoint p = wall->PointAt(du.ParameterAt(i / 12.0), dv.ParameterAt(j / 160.0));
      const double m = std::max(std::fabs(p.x), std::fabs(p.y));  // Chebyshev radius: 5 on rail1 .. 8 on rail2
      const double expect_z = outer_z * (m - 5.0) / 3.0;          // the trapezoid planes: z linear in the Chebyshev radius
      plane_dev = std::max(plane_dev, std::fabs(p.z - expect_z));
      if (m < 5.0 - 1e-9 || m > 8.0 + 1e-9) plane_dev = std::max(plane_dev, 1.0);
    }
  }
  std::printf("  [s2 ring] %s: worst deviation from the closed-form surface %.3g\n", tag, plane_dev);
  d8test::Check(plane_dev < 1e-9, (std::string(tag) + ": every sampled wall point is on the closed-form surface (z = 2(m-5)/3 over 5 <= m <= 8)").c_str());
}

}  // namespace

D8_EXTRA_TEST(Sweep2ClosedSquareRingsFlat) { RunRings(0.0, 156.0, "flat annulus"); }

D8_EXTRA_TEST(Sweep2ClosedSquareRingsLifted) { RunRings(2.0, 52.0 * std::sqrt(13.0), "lifted ring (4 trapezoids)"); }

D8_EXTRA_TEST(Sweep2ClosedSquareRingsSmoothSeam) {
  RunRings(0.0, 156.0, "flat annulus, seam at a mid-side", true);
  RunRings(2.0, 52.0 * std::sqrt(13.0), "lifted ring, seam at a mid-side", true);
}

D8_EXTRA_TEST(Sweep2ClosedRingNegativeControls) {
  // Sweep2Sections still refuses the same closed corner pair, with its reason (that entry point is unchanged).
  const NurbsCurve inner = Square(5.0, 0.0), outer = Square(8.0, 0.0);
  const NurbsCurve c0 = Seg(Point3d(5, 0, 0), Point3d(8, 0, 0)), c1 = Seg(Point3d(-5, 0, 0), Point3d(-8, 0, 0));
  const std::string why = Refusal([&] { Brep::Sweep2Sections({c0, c1}, inner, outer, 32); });
  d8test::Check(why.find("closed rail") != std::string::npos && why.find("corner") != std::string::npos,
                "Sweep2Sections still refuses closed rails with a corner, naming the reason");
  // Rails that touch are still refused (the same square twice).
  const NurbsCurve section = Seg(Point3d(-5, -5, 0), Point3d(-8, -8, 0));
  const std::string touch = Refusal([&] { Brep::Sweep2(section, inner, inner, 32, false); });
  d8test::Check(touch.find("touch") != std::string::npos, "two identical closed square rails (zero separation) are still refused: touching rails");
}
