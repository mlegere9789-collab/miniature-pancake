// Brep::Loft(..., align_seams = true): closed sections whose start points (seams) or running directions do not
// correspond are re-seamed / reversed so that section k matches section k-1 (the nearest point of section k to the
// start of section k-1 becomes its start; a section running the opposite way about the loft direction is reversed).
//
// Independent oracles: loft between coaxial circles of equal radius is the exact cylinder. Aligned, EVERY wall point is at
// radius r from the axis (to 1e-12) and the analytic wall area is 2 pi r h; unaligned with a rotated seam the same loft
// pinches (the mid-station radius drops well below r) - the negative control that the fixture really is twisted. A capped
// loft through squares is the exact prism, volume = side^2 * height.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
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

// Circle about the z axis at height z; `seam_fraction` moves the seam (curve start) that far round the domain.
NurbsCurve Circle(double z, double r, double normal_z = 1.0, double seam_fraction = 0.0) {
  const ON_Circle circle(ON_Plane(Point3d(0, 0, z), Vector3d(0, 0, normal_z)), r);
  ON_NurbsCurve n;
  circle.GetNurbForm(n);
  if (seam_fraction != 0.0) n.ChangeClosedCurveSeam(n.Domain().ParameterAt(seam_fraction));
  NurbsCurve k;
  k.raw() = n;
  return k;
}

// Closed square (side s) at height z starting at corner `start` (0..3), counterclockwise or clockwise about +z.
NurbsCurve Square(double z, double s, int start, bool ccw = true) {
  const Point3d c[4] = {Point3d(0, 0, z), Point3d(s, 0, z), Point3d(s, s, z), Point3d(0, s, z)};
  std::vector<Point3d> pts;
  for (int i = 0; i <= 4; ++i) pts.push_back(c[ccw ? (start + i) % 4 : ((start - i) % 4 + 4) % 4]);
  return NurbsCurve::FromControlPoints(pts, 1);
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

// min and max of |hypot(x, y) - r| over a u grid at station v, and of |z - z_expected| when z_expected is finite.
void RadialSpread(const ON_NurbsSurface& wall, double v, double r, double* worst_abs, double* min_radius) {
  const ON_Interval du = wall.Domain(0), dv = wall.Domain(1);
  *worst_abs = 0.0;
  *min_radius = 1e300;
  for (int j = 0; j <= 128; ++j) {
    const ON_3dPoint p = wall.PointAt(du.ParameterAt(j / 128.0), dv.ParameterAt(v));
    const double rho = std::hypot(p.x, p.y);
    *worst_abs = std::max(*worst_abs, std::fabs(rho - r));
    *min_radius = std::min(*min_radius, rho);
  }
}

void CheckCylinder(const Brep& loft, double r, double h, const std::string& tag) {
  d8test::Check(loft.FaceCount() == 1 && loft.raw().IsValid(), (tag + ": one valid face").c_str());
  const ON_NurbsSurface* wall = Wall(loft);
  if (!wall) {
    d8test::Check(false, (tag + ": has a NURBS wall").c_str());
    return;
  }
  double worst = 0.0, lo = 0.0;
  for (double v : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    double w, l;
    RadialSpread(*wall, v, r, &w, &l);
    worst = std::max(worst, w);
  }
  d8test::Check(worst < 1e-12, (tag + ": every wall point (5 stations x 129 u samples) is at radius r from the axis to 1e-12").c_str());
  RadialSpread(*wall, 0.5, r, &worst, &lo);
  const double exact = 2.0 * M_PI * r * h;
  d8test::Check(std::fabs(loft.Area() - exact) <= 1e-6 * exact, (tag + ": analytic wall area = 2 pi r h to 1e-6 relative").c_str());
}

}  // namespace

D8_EXTRA_TEST(LoftAlignSeamsRotatedSeamGivesTheExactCylinder) {
  const double r = 2.0, h = 5.0;
  const std::vector<NurbsCurve> twisted = {Circle(0.0, r), Circle(h, r, 1.0, 0.3)};
  // Negative control (the option off): the rotated seam pinches the middle station well below radius r.
  const Brep plain = Brep::Loft(twisted, 1, false, false);
  double w = 0.0, lo = 0.0;
  RadialSpread(*Wall(plain), 0.5, r, &w, &lo);
  d8test::Check(lo < 0.9 * r, "control: without align_seams the rotated seam pinches the mid-station radius below 0.9 r");
  CheckCylinder(Brep::Loft(twisted, 1, false, false, nullptr, nullptr, Spacing::Chord, true), r, h, "align_seams, seam rotated by 0.3 of the domain");
  // Several sections, cubic loft: all rotated differently.
  const std::vector<NurbsCurve> three = {Circle(0.0, r), Circle(2.0, r, 1.0, 0.45), Circle(5.0, r, 1.0, 0.8)};
  const Brep b3 = Brep::Loft(three, 2, false, false, nullptr, nullptr, Spacing::Chord, true);
  const ON_NurbsSurface* wall = Wall(b3);
  double worst = 0.0;
  if (wall) {
    for (double v : {0.0, 0.1, 0.37, 0.5, 0.9, 1.0}) {
      double ww, ll;
      RadialSpread(*wall, v, r, &ww, &ll);
      worst = std::max(worst, ww);
    }
  }
  d8test::Check(wall != nullptr && worst < 1e-12, "three sections with three different seams, quadratic loft: still the exact cylinder (radius to 1e-12)");
}

D8_EXTRA_TEST(LoftAlignSeamsReversedSectionIsReversedBack) {
  const double r = 2.0, h = 5.0;
  // The second circle runs clockwise about +z AND has a rotated seam.
  const std::vector<NurbsCurve> secs = {Circle(0.0, r), Circle(h, r, -1.0, 0.3)};
  CheckCylinder(Brep::Loft(secs, 1, false, false, nullptr, nullptr, Spacing::Chord, true), r, h, "align_seams, second circle clockwise with a rotated seam");
  const Brep plain = Brep::Loft(secs, 1, false, false);
  double w = 0.0, lo = 0.0;
  RadialSpread(*Wall(plain), 0.5, r, &w, &lo);
  d8test::Check(lo < 0.9 * r, "control: without align_seams the reversed section pinches the mid-station radius below 0.9 r");
}

D8_EXTRA_TEST(LoftAlignSeamsSquaresGiveTheExactPrism) {
  // Three squares of side 2 at z = 0, 1, 2 (height 2): the second starts at the opposite corner, the third runs
  // clockwise from corner 3. Capped, aligned: the exact prism, volume 2 * 2 * 2 = 8, three faces.
  const std::vector<NurbsCurve> secs = {Square(0.0, 2.0, 0), Square(1.0, 2.0, 2), Square(2.0, 2.0, 3, false)};
  const Brep prism = Brep::Loft(secs, 2, false, true, nullptr, nullptr, Spacing::Chord, true);
  d8test::Check(prism.FaceCount() == 3 && prism.raw().IsValid() && prism.raw().IsSolid(), "aligned squares: a valid closed solid of 3 faces");
  d8test::Check(std::fabs(prism.Volume() - 8.0) <= 1e-6 * 8.0, "...its analytic volume is side^2 x height = 8");
  // Control: unaligned, the same sections do NOT produce the prism (the loft throws, or is not the volume-8 solid).
  bool prism_without = false;
  try {
    const Brep b = Brep::Loft(secs, 2, false, true);
    prism_without = b.raw().IsSolid() && std::fabs(b.Volume() - 8.0) <= 1e-3;
  } catch (const std::exception&) {
  }
  d8test::Check(!prism_without, "control: without align_seams the mismatched squares are not the volume-8 prism");
}

D8_EXTRA_TEST(LoftAlignSeamsIsIdentityWhenAlreadyAlignedAndRefusedWithTangents) {
  const double r = 1.5;
  const std::vector<NurbsCurve> aligned = {Circle(0.0, r), Circle(1.0, 2.0 * r), Circle(3.0, r)};
  const Brep a = Brep::Loft(aligned, 2, false, false);
  const Brep b = Brep::Loft(aligned, 2, false, false, nullptr, nullptr, Spacing::Chord, true);
  double dev = 0.0;
  const ON_NurbsSurface *wa = Wall(a), *wb = Wall(b);
  if (wa && wb) {
    const ON_Interval du = wa->Domain(0), dv = wa->Domain(1);
    for (int i = 0; i <= 32; ++i)
      for (int j = 0; j <= 16; ++j)
        dev = std::max(dev, wa->PointAt(du.ParameterAt(i / 32.0), dv.ParameterAt(j / 16.0))
                                .DistanceTo(wb->PointAt(wb->Domain(0).ParameterAt(i / 32.0), wb->Domain(1).ParameterAt(j / 16.0))));
  }
  d8test::Check(wa != nullptr && wb != nullptr && dev < 1e-12, "already aligned sections: align_seams changes nothing (wall deviation < 1e-12)");
  // Open sections: the option is a no-op.
  const std::vector<NurbsCurve> lines = {NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(1, 0, 0)}, 1),
                                         NurbsCurve::FromControlPoints({Point3d(0, 0, 1), Point3d(1, 0, 1)}, 1)};
  d8test::Check(Brep::Loft(lines, 1, false, false, nullptr, nullptr, Spacing::Chord, true).FaceCount() == 1, "open sections: align_seams is accepted and ignored");
  // Tangent constraints refer to the sections' own parametrization: not combinable.
  const NurbsCurve t0 = NurbsCurve::FromControlPoints({Point3d(0, 0, 1), Point3d(0, 0, 1)}, 1);
  d8test::Check(ThrowsWith([&] { Brep::Loft(aligned, 2, false, false, &t0, nullptr, Spacing::Chord, true); }, "align_seams"),
                "align_seams together with start_tangent/end_tangent is refused with that reason");
}
