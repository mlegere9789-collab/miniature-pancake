// Two perpendicular fillets of DIFFERENT radii r_a, r_b at a right-angle box corner whose third edge stays sharp (FilletConvexEdgePairMitre with r_a != r_b).
// Corner frame: x along edge B, w along edge A, y along the sharp edge C into the material; faces y = 0 (holds A and B), x = 0 (A and C), w = 0 (B and C).
// Cylinder A (axis along w through (x, y) = (r_a, r_a)) removes, at height y < r_a, the slab x < f_a(y) = r_a - sqrt(2 r_a y - y^2) (for every w); cylinder B
// (axis along x through (y, w) = (r_b, r_b)) removes w < f_b(y) = r_b - sqrt(2 r_b y - y^2) (every x). They meet in the space curve (f_a(y), y, f_b(y)) for
// y in [0, m], m = min(r_a, r_b) - planar only when r_a = r_b. The removed volume is the two rounded prisms minus their overlap:
//   V = V_box - a r_a^2 L_A - a r_b^2 L_B + overlap,  a = 1 - pi/4,  overlap = integral_0^m f_a(y) f_b(y) dy
//     = r_a r_b m - r_a S(r_b, m) - r_b S(r_a, m) + K,
//   S(r, m) = integral_0^m sqrt(2 r y - y^2) dy = (1/2) [ (m - r) sqrt(2 r m - m^2) + r^2 (asin((m - r)/r) + pi/2) ],
//   K = integral_0^m y sqrt((2 r_a - y)(2 r_b - y)) dy = (1/3) [q(m)^(3/2) - q(0)^(3/2)] + c [G(c) - G(c - m)],
//   q(y) = (2 r_a - y)(2 r_b - y), c = r_a + r_b, d = r_b - r_a, G(t) = (1/2) [ t sqrt(t^2 - d^2) - d^2 ln(t + sqrt(t^2 - d^2)) ]
// (for r_a = r_b this is r^3 (5/3 - pi/2), the equal-radius value of fillet_pair_mitre.cpp). The closed form is checked here against two independent numerics -
// a midpoint rule over y and a 3D indicator grid of the two prisms written from the cylinder equations directly - and then the kernel's body against it.
// Failing first: the kernel refused unequal radii ("the radii differ ... a space curve that is not planar"), pinned by the first test below.
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
using dino8::kernel::FilletConvexEdge;
using dino8::kernel::FilletConvexEdgePairMitre;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }

std::string ReasonOf(const std::function<void()>& f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  return "";
}

constexpr double kPi = 3.14159265358979323846;

double S(double r, double m) {
  const double u = m - r;
  return 0.5 * (u * std::sqrt(std::max(0.0, r * r - u * u)) + r * r * (std::asin(std::max(-1.0, std::min(1.0, u / r))) + kPi / 2.0));
}

double OverlapClosed(double ra, double rb) {
  const double m = std::min(ra, rb), c = ra + rb, d = rb - ra;
  auto q = [&](double y) { return (2.0 * ra - y) * (2.0 * rb - y); };
  auto G = [&](double t) {
    const double s = std::sqrt(std::max(0.0, t * t - d * d));
    return 0.5 * (t * s - (d == 0.0 ? 0.0 : d * d * std::log(t + s)));
  };
  const double K = (std::pow(q(m), 1.5) - std::pow(q(0.0), 1.5)) / 3.0 + c * (G(c) - G(c - m));
  return ra * rb * m - ra * S(rb, m) - rb * S(ra, m) + K;
}

double OverlapMidpoint(double ra, double rb) {
  const double m = std::min(ra, rb);
  const int n = 400000;
  double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    const double y = (i + 0.5) * m / n;
    sum += (ra - std::sqrt(2.0 * ra * y - y * y)) * (rb - std::sqrt(2.0 * rb * y - y * y));
  }
  return sum * m / n;
}

// 3D indicator grid straight from the two cylinder equations (no f_a / f_b): a point is in rounded prism A when x, y < r_a and outside the circle about
// (r_a, r_a), in rounded prism B when y, w < r_b and outside the circle about (r_b, r_b); the overlap lies in x < r_a, y < m, w < r_b.
double OverlapGrid(double ra, double rb) {
  const double m = std::min(ra, rb);
  const int n = 120;
  const double hx = ra / n, hy = m / n, hw = rb / n;
  long hits = 0;
  for (int i = 0; i < n; ++i) {
    const double x = (i + 0.5) * hx;
    for (int j = 0; j < n; ++j) {
      const double y = (j + 0.5) * hy;
      const bool in_a = (x - ra) * (x - ra) + (y - ra) * (y - ra) > ra * ra;
      if (!in_a) continue;
      for (int k = 0; k < n; ++k) {
        const double w = (k + 0.5) * hw;
        if ((y - rb) * (y - rb) + (w - rb) * (w - rb) > rb * rb) ++hits;
      }
    }
  }
  return static_cast<double>(hits) * hx * hy * hw;
}

double BodyVolume(double box, double la, double lb, double ra, double rb) {
  const double a = 1.0 - kPi / 4.0;
  return box - a * ra * ra * la - a * rb * rb * lb + OverlapClosed(ra, rb);
}

void CheckBody(const Brep& b, double exact, int expected_faces, const std::string& tag) {
  ON_TextLog log;
  bool oriented = false, has_boundary = true;
  const bool manifold = b.raw().IsManifold(&oriented, &has_boundary);
  Check(b.raw().IsValid(&log) && b.raw().IsSolid() && manifold && oriented && !has_boundary && b.FaceCount() == expected_faces,
        (tag + "valid, closed, oriented solid with " + std::to_string(expected_faces) + " faces (got " + std::to_string(b.FaceCount()) + ")").c_str());
  struct C { double chord, tol; };
  for (const C& c : {C{1e-5, 3e-6}, C{1e-7, 3e-7}}) {
    const Mesh m = b.TessellateToClosedMeshAdaptive(c.chord);
    const Mesh::CheckReport r = m.Check();
    Check(m.IsClosedManifold() && r.naked_edges == 0 && r.orientation_conflicts == 0,
          (tag + "adaptive chord " + std::to_string(c.chord) + " mesh is closed and oriented (naked " + std::to_string(r.naked_edges) + ")").c_str());
    Check(std::fabs(m.Volume() - exact) < c.tol * exact,
          (tag + "adaptive chord " + std::to_string(c.chord) + " volume " + std::to_string(m.Volume()) + " vs closed form " + std::to_string(exact)).c_str());
  }
}

}  // namespace

D8_EXTRA_TEST(PairUnequalOracleMatchesTwoNumerics) {
  const double rr[][2] = {{1.0, 1.5}, {1.5, 1.0}, {0.7, 2.0}, {2.0, 0.7}, {1.0, 1.0}, {1.0, 1.0001}};
  for (const auto& p : rr) {
    const double ra = p[0], rb = p[1];
    const std::string tag = "[unequal oracle ra=" + std::to_string(ra) + " rb=" + std::to_string(rb) + "] ";
    const double o = OverlapClosed(ra, rb);
    Check(std::fabs(OverlapMidpoint(ra, rb) - o) < 1e-9 * (1.0 + o), (tag + "closed-form overlap " + std::to_string(o) + " vs midpoint integral " + std::to_string(OverlapMidpoint(ra, rb))).c_str());
    const double g = OverlapGrid(ra, rb);
    Check(std::fabs(g - o) < 1.5e-2 * o, (tag + "closed-form overlap vs 3D grid integral " + std::to_string(g)).c_str());
  }
  // The equal-radius limit is r^3 (5/3 - pi/2), the value fillet_pair_mitre.cpp checks the planar mitre against.
  for (double r : {0.7, 1.0, 2.5}) {
    Check(std::fabs(OverlapClosed(r, r) - r * r * r * (5.0 / 3.0 - kPi / 2.0)) < 1e-12 * r * r * r, ("[unequal oracle] equal radii r = " + std::to_string(r) + " give r^3 (5/3 - pi/2)").c_str());
  }
  // Swapping the radii swaps nothing in the volume.
  Check(std::fabs(OverlapClosed(1.0, 1.5) - OverlapClosed(1.5, 1.0)) < 1e-12, "[unequal oracle] the overlap is symmetric in (r_a, r_b)");
}

D8_EXTRA_TEST(PairUnequalChainedAndPairRefusalsArePinned) {
  // What the kernel does today with two unequal fillets at one corner (measured): the pair function refuses the radii, and chaining two single-edge
  // fillets is refused because the second edge's near end meets the first fillet's cylinder.
  const double L = 10.0;
  const Brep box = Brep::Box(0, 0, 0, L, L, L);
  const Point3d V(0, 0, L), A(0, 0, 0), B(L, 0, L);
  Brep one;
  const std::string first = ReasonOf([&] { one = FilletConvexEdge(box, V, A, 1.0); });
  Check(first.empty(), ("[unequal chained] the first fillet builds" + (first.empty() ? std::string() : ": " + first)).c_str());
  if (first.empty()) {
    const std::string second = ReasonOf([&] { FilletConvexEdge(one, Point3d(1.0, 0, L), B, 1.5); });
    Check(second.find("curved (blended) face") != std::string::npos, ("[unequal chained] the second fillet is refused: " + second.substr(0, 150)).c_str());
  }
}

D8_EXTRA_TEST(PairUnequalBuildsTheClosedFormBody) {
  // The 8 x 6 x 5 box, edge A from (0,0,5) down to (0,0,0) (L_A = 5), edge B from (0,0,5) to (8,0,5) (L_B = 8), both argument orders, and a rotated copy.
  const Brep box = Brep::Box(0, 0, 0, 8, 6, 5);
  const Point3d V(0, 0, 5), A(0, 0, 0), B(8, 0, 5);
  const double rr[][2] = {{0.8, 1.5}, {1.5, 0.8}, {1.0, 2.0}, {0.6, 0.9}};
  for (const auto& p : rr) {
    const double ra = p[0], rb = p[1];
    const double exact = BodyVolume(240.0, 5.0, 8.0, ra, rb);
    const std::string tag = "[unequal ra=" + std::to_string(ra) + " rb=" + std::to_string(rb) + "] ";
    Brep out;
    std::string why = ReasonOf([&] { out = FilletConvexEdgePairMitre(box, V, A, B, ra, rb); });
    Check(why.empty(), (tag + "A,B accepted" + (why.empty() ? std::string() : ": " + why)).c_str());
    if (why.empty()) CheckBody(out, exact, 9, tag + "A,B: ");
    Brep swapped;
    why = ReasonOf([&] { swapped = FilletConvexEdgePairMitre(box, V, B, A, rb, ra); });
    Check(why.empty(), (tag + "B,A accepted" + (why.empty() ? std::string() : ": " + why)).c_str());
    if (why.empty()) CheckBody(swapped, exact, 9, tag + "B,A: ");
  }
  ON_Xform rot_xf;
  rot_xf.Rotation(0.7, ON_3dVector(1, 2, 3), ON_3dPoint(0, 0, 0));
  const ON_Xform xf = ON_Xform::TranslationTransformation(ON_3dVector(3, -2, 1)) * rot_xf;
  const Brep moved = box.Transform(xf);
  Brep out;
  const std::string why = ReasonOf([&] { out = FilletConvexEdgePairMitre(moved, xf * V, xf * A, xf * B, 0.8, 1.5); });
  Check(why.empty(), ("[unequal rotated] accepted" + (why.empty() ? std::string() : ": " + why)).c_str());
  if (why.empty()) CheckBody(out, BodyVolume(240.0, 5.0, 8.0, 0.8, 1.5), 9, "[unequal rotated] ");
  // Negative controls: ignoring the overlap, and the equal-radius overlap, are both measurably wrong for unequal radii.
  const double exact = BodyVolume(240.0, 5.0, 8.0, 0.8, 1.5);
  const double a = 1.0 - kPi / 4.0;
  Check(std::fabs((240.0 - a * 0.8 * 0.8 * 5.0 - a * 1.5 * 1.5 * 8.0) - exact) > 1e-4 * exact, "[unequal] negative control: ignoring the overlap would NOT match");
  Check(std::fabs(OverlapClosed(0.8, 1.5) - 0.8 * 0.8 * 0.8 * (5.0 / 3.0 - kPi / 2.0)) > 1e-3, "[unequal] negative control: the equal-radius overlap formula is not the unequal one");
}
