// Unequal-distance and distance+angle chamfers of two edges that meet at a right-angle (trihedral) corner, third edge sharp, in ANY argument order.
// Local frame at the corner V: edge A runs along w (down), edge B along x, the sharp edge C along y. The face y = 0 is the one A and B SHARE;
// A's other face is x = 0, B's other face is the top w = 0. Each chamfer is given by its two setbacks:
//   A: s_A on the shared face (along x), o_A on its other face (along y)  -> removes the prism  x/s_A + y/o_A <= 1  (all w)
//   B: s_B on the shared face (along w), o_B on its other face (along y)  -> removes the prism  w/s_B + y/o_B <= 1  (all x)
// V = V_box - s_A o_A/2 L_A - s_B o_B/2 L_B + overlap, and the overlap {x/s_A + y/o_A <= 1, w/s_B + y/o_B <= 1} slices at height y into a rectangle
// s_A (1 - y/o_A) by s_B (1 - y/o_B), so  overlap = s_A s_B * integral_0^m (1 - y/o_A)(1 - y/o_B) dy
//                                                 = s_A s_B (m - m^2 (1/o_A + 1/o_B)/2 + m^3/(3 o_A o_B)),  m = min(o_A, o_B).
// The closed form is checked against two independent numerics (a midpoint rule over y, and a 3D midpoint grid of the indicator), then the kernel's
// volume is checked against the closed form. Distance+angle form: the angle is between the shared face and the chamfer plane, so o = s tan(angle).
// Failing first: the functions did not exist (this file did not compile against the unchanged headers).
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
using dino8::kernel::ChamferConvexEdgePairAngle;
using dino8::kernel::ChamferConvexEdgePairLegs;
using dino8::kernel::Point3d;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }

std::string ReasonOf(const std::function<void()>& f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  return "";
}

constexpr double kLx = 8.0, kLy = 6.0, kLz = 5.0;  // L_B = kLx, sharp edge C = kLy, L_A = kLz

struct Legs { double sa, oa, sb, ob; };

double OverlapClosed(const Legs& c) {
  const double m = std::min(c.oa, c.ob);
  return c.sa * c.sb * (m - m * m * (1.0 / c.oa + 1.0 / c.ob) / 2.0 + m * m * m / (3.0 * c.oa * c.ob));
}

// Independent numeric 1: midpoint rule in y of the product of the two slice widths.
double OverlapMidpointY(const Legs& c) {
  const double m = std::min(c.oa, c.ob);
  const int n = 200000;
  double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    const double y = (i + 0.5) * m / n;
    sum += c.sa * (1.0 - y / c.oa) * c.sb * (1.0 - y / c.ob);
  }
  return sum * m / n;
}

// Independent numeric 2: 3D midpoint grid of the indicator of the two prisms' intersection (no slicing, no antiderivative).
double OverlapGrid(const Legs& c) {
  const double m = std::min(c.oa, c.ob);
  const int n = 100;
  const double hx = c.sa / n, hy = m / n, hw = c.sb / n;
  long hits = 0;
  for (int i = 0; i < n; ++i) {
    const double x = (i + 0.5) * hx;
    for (int j = 0; j < n; ++j) {
      const double y = (j + 0.5) * hy;
      if (x / c.sa + y / c.oa > 1.0) continue;
      for (int k = 0; k < n; ++k) {
        const double w = (k + 0.5) * hw;
        if (w / c.sb + y / c.ob <= 1.0) ++hits;
      }
    }
  }
  return static_cast<double>(hits) * hx * hy * hw;
}

double ClosedForm(const Legs& c) {
  return kLx * kLy * kLz - c.sa * c.oa / 2.0 * kLz - c.sb * c.ob / 2.0 * kLx + OverlapClosed(c);
}

void CheckOracle(const Legs& c, const std::string& tag) {
  const double o = OverlapClosed(c);
  Check(std::fabs(OverlapMidpointY(c) - o) < 1e-9 * (1.0 + o), (tag + "closed-form overlap " + std::to_string(o) + " vs midpoint-y integral " + std::to_string(OverlapMidpointY(c))).c_str());
  const double g = OverlapGrid(c);
  Check(std::fabs(g - o) < 2e-2 * o, (tag + "closed-form overlap vs 3D grid integral " + std::to_string(g)).c_str());
}

// Box [0,lx] x [0,ly] x [0,lz]; V = (0, 0, lz); A = V -> (0,0,0); B = V -> (lx, 0, lz).
// `mode`: 0 = legs A,B; 1 = legs B,A (roles swapped); 2 = angle form A,B (o derived by tan); 3 = angle form B,A.
void CheckLegs(const Legs& c, const ON_Xform* xf, int mode, const std::string& tag) {
  Brep box = Brep::Box(0, 0, 0, kLx, kLy, kLz);
  auto P = [&](double x, double y, double z) {
    const Point3d p(x, y, z);
    return xf ? *xf * p : p;
  };
  if (xf) box = box.Transform(*xf);
  const Point3d V = P(0, 0, kLz), A = P(0, 0, 0), B = P(kLx, 0, kLz);
  Brep out;
  const std::string why = ReasonOf([&] {
    switch (mode) {
      case 0: out = ChamferConvexEdgePairLegs(box, V, A, B, c.sa, c.oa, c.sb, c.ob); break;
      case 1: out = ChamferConvexEdgePairLegs(box, V, B, A, c.sb, c.ob, c.sa, c.oa); break;
      case 2: out = ChamferConvexEdgePairAngle(box, V, A, B, c.sa, std::atan(c.oa / c.sa), c.sb, std::atan(c.ob / c.sb)); break;
      default: out = ChamferConvexEdgePairAngle(box, V, B, A, c.sb, std::atan(c.ob / c.sb), c.sa, std::atan(c.oa / c.sa)); break;
    }
  });
  Check(why.empty(), (tag + "accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (!why.empty()) return;
  ON_TextLog log;
  bool oriented = false, has_boundary = true;
  const bool manifold = out.raw().IsManifold(&oriented, &has_boundary);
  Check(out.raw().IsValid(&log) && out.raw().IsSolid() && manifold && oriented && !has_boundary && out.FaceCount() == 8,
        (tag + "valid, closed, oriented solid with 8 faces").c_str());
  const double exact = ClosedForm(c);
  const double v = out.TessellateToClosedMesh(8, 8).Volume();
  Check(std::fabs(v - exact) < 1e-7 * exact, (tag + "volume " + std::to_string(v) + " vs closed form " + std::to_string(exact)).c_str());
  const double ov = OverlapClosed(c);
  Check(ov > 1e-3 && std::fabs(v - (exact - ov)) > 0.5 * ov, (tag + "negative control: ignoring the overlap would NOT match").c_str());
}

}  // namespace

D8_EXTRA_TEST(ChamferPairLegsUnequalDistances) {
  // (s_A, o_A, s_B, o_B): o_A > o_B (A first), o_B > o_A (B first), equal o with different s, strongly unequal, equal everything.
  const Legs cases[] = {{1, 2, 1.5, 1}, {1, 1, 1.5, 2.5}, {2, 1, 1, 1}, {0.5, 3, 2, 1}, {1.2, 0.8, 0.6, 1.7}, {1, 1, 1, 1}, {2.5, 0.6, 0.4, 3.5}};
  for (const Legs& c : cases) {
    const std::string tag = "[chamfer legs sA=" + std::to_string(c.sa) + " oA=" + std::to_string(c.oa) + " sB=" + std::to_string(c.sb) + " oB=" + std::to_string(c.ob) + "] ";
    CheckOracle(c, tag + "oracle: ");
    CheckLegs(c, nullptr, 0, tag + "A,B: ");
    CheckLegs(c, nullptr, 1, tag + "B,A: ");
  }
}

D8_EXTRA_TEST(ChamferPairLegsDistanceAndAngle) {
  // distance on the shared face + angle from it: o = s tan(angle). 30/60, 70/40, 45/45 with different distances, 20/80.
  const double deg = 3.14159265358979323846 / 180.0;
  struct AngleCase { double sa, aa, sb, ab; };
  const AngleCase ac[] = {{1.5, 30, 2.0, 60}, {1.0, 70, 2.0, 40}, {1.0, 45, 1.6, 45}, {2.0, 20, 0.5, 80}};
  for (const AngleCase& a : ac) {
    const Legs c{a.sa, a.sa * std::tan(a.aa * deg), a.sb, a.sb * std::tan(a.ab * deg)};
    const std::string tag = "[chamfer angle sA=" + std::to_string(a.sa) + "@" + std::to_string(a.aa) + " sB=" + std::to_string(a.sb) + "@" + std::to_string(a.ab) + "] ";
    CheckOracle(c, tag + "oracle: ");
    CheckLegs(c, nullptr, 2, tag + "A,B: ");
    CheckLegs(c, nullptr, 3, tag + "B,A: ");
  }
}

D8_EXTRA_TEST(ChamferPairLegsRotatedAndRefusals) {
  ON_Xform rot_xf;
  rot_xf.Rotation(0.7, ON_3dVector(1, 2, 3), ON_3dPoint(0, 0, 0));
  const ON_Xform xf = ON_Xform::TranslationTransformation(ON_3dVector(3, -2, 1)) * rot_xf;
  const Legs c{1.2, 0.8, 0.6, 1.7};
  CheckLegs(c, &xf, 0, "[chamfer legs rotated, A,B] ");
  CheckLegs(c, &xf, 1, "[chamfer legs rotated, B,A] ");
  CheckLegs(c, &xf, 3, "[chamfer angle rotated, B,A] ");
  const Brep box = Brep::Box(0, 0, 0, kLx, kLy, kLz);
  const Point3d V(0, 0, kLz), A(0, 0, 0), B(kLx, 0, kLz);
  std::string m = ReasonOf([&] { ChamferConvexEdgePairLegs(box, Point3d(0, 0, 2.5), A, B, 1, 1, 1, 1); });
  Check(m.find("trihedral") != std::string::npos, ("[chamfer legs] a point that is not a corner is refused: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePairLegs(box, V, A, Point3d(kLx, kLy, kLz), 1, 1, 1, 1); });
  Check(!m.empty(), ("[chamfer legs] two points that are not both edges at the corner are refused: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePairLegs(box, V, A, B, 1, 0, 1, 1); });
  Check(!m.empty(), ("[chamfer legs] a zero leg is refused: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePairLegs(box, V, A, B, 9, 1, 1, 1); });
  Check(!m.empty(), ("[chamfer legs] a leg beyond the face is refused with a reason, never a bad solid: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePairLegs(box, V, A, B, 1, 7, 1, 1); });
  Check(!m.empty(), ("[chamfer legs] a leg beyond the other face is refused: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePairAngle(box, V, A, B, 1, 0.0, 1, 0.5); });
  Check(m.find("angle") != std::string::npos, ("[chamfer angle] a zero angle is refused naming the angle: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePairAngle(box, V, A, B, 1, 1.5707963267948966, 1, 0.5); });
  Check(m.find("angle") != std::string::npos, ("[chamfer angle] a right angle is refused naming the angle: " + m).c_str());
}
