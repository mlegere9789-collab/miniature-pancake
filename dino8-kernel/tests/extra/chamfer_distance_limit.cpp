// MaxChamferDistance / MaxChamferDistanceForAngle: the largest setback ChamferConvexEdge / ChamferConvexEdgeAngle can fit on an edge (the chamfer's overflow
// bound; MaxFilletRadius is the fillet's). Oracles, all independent of the new functions:
//   (1) closed forms - each face's in-plane extent perpendicular to the edge (a box's side lengths; for an oblique chamfer face sqrt(d1^2 + d2^2) and the
//       remaining height), and for the angle form  d_i < min(extent_i, extent_j sin(theta + alpha) / sin(alpha))  (law of sines);
//   (2) the MEASURED accept/refuse boundary of the real ChamferConvexEdge[Angle], found by bisecting over the setback;
//   (3) the boundary itself: 0.999 x limit builds a chamfer whose removed volume is d_i d_j L / 2, 1.001 x limit is refused with the "too large" reason.
// MaxChamferDistance already existed (tested in test_basic on a unit cube and an L prism); here it also gets the oblique-face and rotated closed forms and the
// measured boundary. Failing first: MaxChamferDistanceForAngle did not exist (this file did not compile against the unchanged headers).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "dino8/kernel/brep.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::ChamferConvexEdge;
using dino8::kernel::ChamferConvexEdgeAngle;
using dino8::kernel::MaxChamferDistance;
using dino8::kernel::MaxChamferDistanceForAngle;
using dino8::kernel::Point3d;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }

std::string ReasonOf(const std::function<void()>& f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  return "";
}

// Largest x in (lo, hi) with accept(x) true, by bisection (accept is true at lo and false at hi, monotone for these fixtures).
double Boundary(const std::function<bool(double)>& accept, double lo, double hi) {
  for (int i = 0; i < 80; ++i) {
    const double mid = 0.5 * (lo + hi);
    (accept(mid) ? lo : hi) = mid;
  }
  return lo;
}

bool Accepts(const std::function<void()>& f) { return ReasonOf(f).empty(); }

constexpr double kTiny = 1e-3;

struct Measured { double i, j; };

Measured MeasureLimits(const Brep& solid, Point3d p0, Point3d p1) {
  Measured m;
  m.i = Boundary([&](double d) { return Accepts([&] { ChamferConvexEdge(solid, p0, p1, d, kTiny); }); }, 1e-6, 1e3);
  m.j = Boundary([&](double d) { return Accepts([&] { ChamferConvexEdge(solid, p0, p1, kTiny, d); }); }, 1e-6, 1e3);
  return m;
}

double MeasureAngleLimit(const Brep& solid, Point3d p0, Point3d p1, double angle) {
  return Boundary([&](double d) { return Accepts([&] { ChamferConvexEdgeAngle(solid, p0, p1, d, angle); }); }, 1e-6, 1e3);
}

void CheckEdge(const Brep& solid, Point3d p0, Point3d p1, double expect_a, double expect_b, const std::string& tag) {
  const auto limit = MaxChamferDistance(solid, p0, p1);
  const Measured m = MeasureLimits(solid, p0, p1);
  Check(std::fabs(limit.max_distance_i - m.i) < 1e-6 && std::fabs(limit.max_distance_j - m.j) < 1e-6,
        (tag + "limits " + std::to_string(limit.max_distance_i) + ", " + std::to_string(limit.max_distance_j) + " vs measured accept/refuse boundary " +
         std::to_string(m.i) + ", " + std::to_string(m.j)).c_str());
  const double lo = std::min(limit.max_distance_i, limit.max_distance_j), hi = std::max(limit.max_distance_i, limit.max_distance_j);
  Check(std::fabs(lo - std::min(expect_a, expect_b)) < 1e-9 && std::fabs(hi - std::max(expect_a, expect_b)) < 1e-9,
        (tag + "limits are the two face extents " + std::to_string(expect_a) + " and " + std::to_string(expect_b) + " (closed form)").c_str());
}

}  // namespace

D8_EXTRA_TEST(ChamferDistanceLimitBoxEdge) {
  const Brep box = Brep::Box(0, 0, 0, 8, 6, 5);
  // Top edge along x at y = 0: its faces are y = 0 (extent 5 down) and the top z = 5 (extent 6 along y).
  CheckEdge(box, Point3d(0, 0, 5), Point3d(8, 0, 5), 5.0, 6.0, "[chamfer limit box top edge] ");
  // Vertical edge along z at the origin: faces x = 0 (extent 6) and y = 0 (extent 8).
  CheckEdge(box, Point3d(0, 0, 0), Point3d(0, 0, 5), 6.0, 8.0, "[chamfer limit box vertical edge] ");
  // A rotated and translated copy has the same limits.
  ON_Xform rot_xf;
  rot_xf.Rotation(0.7, ON_3dVector(1, 2, 3), ON_3dPoint(0, 0, 0));
  const ON_Xform xf = ON_Xform::TranslationTransformation(ON_3dVector(3, -2, 1)) * rot_xf;
  const Brep moved = box.Transform(xf);
  CheckEdge(moved, xf * Point3d(0, 0, 5), xf * Point3d(8, 0, 5), 5.0, 6.0, "[chamfer limit rotated box edge] ");
  // The boundary itself: just inside builds, with the closed-form removed volume d_i d_j L / 2; just outside is refused as too large.
  const auto limit = MaxChamferDistance(box, Point3d(0, 0, 5), Point3d(8, 0, 5));
  const double di = 0.999 * limit.max_distance_i, dj = 0.5;
  Brep out;
  const std::string why = ReasonOf([&] { out = ChamferConvexEdge(box, Point3d(0, 0, 5), Point3d(8, 0, 5), di, dj); });
  Check(why.empty(), ("[chamfer limit] 0.999 x the limit builds" + (why.empty() ? std::string() : ": " + why)).c_str());
  if (why.empty()) {
    const double exact = 8.0 * 6.0 * 5.0 - di * dj * 8.0 / 2.0;
    const double v = out.TessellateToClosedMesh(8, 8).Volume();
    Check(std::fabs(v - exact) < 1e-7 * exact, ("[chamfer limit] volume at 0.999 x the limit " + std::to_string(v) + " vs " + std::to_string(exact)).c_str());
  }
  const std::string over = ReasonOf([&] { ChamferConvexEdge(box, Point3d(0, 0, 5), Point3d(8, 0, 5), 1.001 * limit.max_distance_i, dj); });
  Check(over.find("too large") != std::string::npos, ("[chamfer limit] 1.001 x the limit is refused as too large: " + over.substr(0, 100)).c_str());
}

D8_EXTRA_TEST(ChamferDistanceLimitObliqueFace) {
  // A 10-cube whose top edge along x at y = 0 is chamfered with 2 on the top and 3 down the y = 0 face. The remaining edge (0,0,7)-(10,0,7) lies between
  // the y = 0 face (7 high) and the oblique chamfer face (width sqrt(2^2 + 3^2)); the dihedral between their outward normals (0,-1,0) and (0,-3,2)/sqrt(13)
  // is acos(3/sqrt(13)), so the interior angle is pi minus that (used by the angle-form test below).
  const Brep cube = Brep::Box(0, 0, 0, 10, 10, 10);
  const Brep one = ChamferConvexEdge(cube, Point3d(0, 0, 10), Point3d(10, 0, 10), 2.0, 3.0);
  CheckEdge(one, Point3d(0, 0, 7), Point3d(10, 0, 7), std::sqrt(13.0), 7.0, "[chamfer limit oblique chamfer face edge] ");
}

D8_EXTRA_TEST(ChamferDistanceLimitForAngle) {
  const double deg = 3.14159265358979323846 / 180.0;
  const Brep box = Brep::Box(0, 0, 0, 8, 6, 5);
  const Point3d p0(0, 0, 5), p1(8, 0, 5);
  // Which face is `i` is measured (a probe setback of 5.5 on i only fits the 6-wide top face), so the closed form uses the real assignment.
  const bool i_is_wide = Accepts([&] { ChamferConvexEdge(box, p0, p1, 5.5, kTiny); });
  const double ext_i = i_is_wide ? 6.0 : 5.0, ext_j = i_is_wide ? 5.0 : 6.0;
  for (double a : {20.0, 40.0, 60.0, 75.0}) {
    const double alpha = a * deg;
    const double exact = std::min(ext_i, ext_j / std::tan(alpha));  // right-angle edge: d_j = d_i tan(alpha)
    const double got = MaxChamferDistanceForAngle(box, p0, p1, alpha);
    const double measured = MeasureAngleLimit(box, p0, p1, alpha);
    const std::string tag = "[chamfer angle limit " + std::to_string(a) + " deg] ";
    Check(std::fabs(got - exact) < 1e-9, (tag + "closed form min(extent_i, extent_j / tan(alpha)) = " + std::to_string(exact) + ", got " + std::to_string(got)).c_str());
    Check(std::fabs(got - measured) < 1e-6, (tag + "vs the measured accept/refuse boundary " + std::to_string(measured)).c_str());
    const std::string over = ReasonOf([&] { ChamferConvexEdgeAngle(box, p0, p1, 1.001 * got, alpha); });
    Check(over.find("too large") != std::string::npos, (tag + "1.001 x the limit is refused as too large").c_str());
    Check(Accepts([&] { ChamferConvexEdgeAngle(box, p0, p1, 0.999 * got, alpha); }), (tag + "0.999 x the limit builds").c_str());
  }
}

D8_EXTRA_TEST(ChamferDistanceLimitForAngleObliqueEdge) {
  // The same oblique edge as above: interior angle theta = pi - acos(3/sqrt(13)) = 146.31 deg, so alpha must lie in (0, 33.69 deg) and
  // d_j = d_i sin(alpha) / sin(theta + alpha); the limit is min(extent_i, extent_j sin(theta + alpha) / sin(alpha)).
  const Brep cube = Brep::Box(0, 0, 0, 10, 10, 10);
  const Brep one = ChamferConvexEdge(cube, Point3d(0, 0, 10), Point3d(10, 0, 10), 2.0, 3.0);
  const Point3d p0(0, 0, 7), p1(10, 0, 7);
  const double theta = 3.14159265358979323846 - std::acos(3.0 / std::sqrt(13.0));
  const bool i_is_short = Accepts([&] { ChamferConvexEdge(one, p0, p1, kTiny, 5.0); });  // only the 7-high face fits a setback of 5 as `j`, so then i is the sqrt(13) face
  const double ext_i = i_is_short ? std::sqrt(13.0) : 7.0, ext_j = i_is_short ? 7.0 : std::sqrt(13.0);
  const double deg = 3.14159265358979323846 / 180.0;
  for (double a : {5.0, 15.0, 30.0, 33.0}) {
    const double alpha = a * deg;
    const double exact = std::min(ext_i, ext_j * std::sin(theta + alpha) / std::sin(alpha));
    const double got = MaxChamferDistanceForAngle(one, p0, p1, alpha);
    const double measured = MeasureAngleLimit(one, p0, p1, alpha);
    const std::string tag = "[chamfer angle limit oblique edge " + std::to_string(a) + " deg] ";
    Check(std::fabs(got - exact) < 1e-9, (tag + "law of sines closed form " + std::to_string(exact) + ", got " + std::to_string(got)).c_str());
    Check(std::fabs(got - measured) < 1e-6, (tag + "vs the measured accept/refuse boundary " + std::to_string(measured)).c_str());
  }
  const std::string m = ReasonOf([&] { MaxChamferDistanceForAngle(one, p0, p1, 34.0 * deg); });
  Check(m.find("angle") != std::string::npos, ("[chamfer angle limit oblique edge] an angle past pi - theta is refused naming the angle: " + m).c_str());
}

D8_EXTRA_TEST(ChamferDistanceLimitRefusals) {
  const Brep box = Brep::Box(0, 0, 0, 8, 6, 5);
  std::string m = ReasonOf([&] { MaxChamferDistance(box, Point3d(0, 0, 5), Point3d(8, 6, 5)); });
  Check(!m.empty(), ("[chamfer limit] two points that are not an edge are refused: " + m).c_str());
  m = ReasonOf([&] { MaxChamferDistance(box, Point3d(0, 0, 5), Point3d(0, 0, 5)); });
  Check(!m.empty(), ("[chamfer limit] coincident points are refused: " + m).c_str());
  m = ReasonOf([&] { MaxChamferDistanceForAngle(box, Point3d(0, 0, 5), Point3d(8, 0, 5), 0.0); });
  Check(m.find("angle") != std::string::npos, ("[chamfer angle limit] a zero angle is refused naming the angle: " + m).c_str());
  m = ReasonOf([&] { MaxChamferDistanceForAngle(box, Point3d(0, 0, 5), Point3d(8, 0, 5), 1.5707963267948966); });
  Check(m.find("angle") != std::string::npos, ("[chamfer angle limit] a right angle on a right-angle edge is refused naming the angle: " + m).c_str());
}
