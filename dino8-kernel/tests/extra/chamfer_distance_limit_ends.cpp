// MaxChamferDistanceWithEnds: the largest setbacks ChamferConvexEdge can fit on an edge counting the faces that meet the edge's two ends, not only the two
// faces along it (MaxChamferDistance). At an end where a third face k is OBLIQUE to the edge the rail of distance d pierces plane k at
//   Q = R - ((R - v).n_k / (e.n_k)) e,   R = v + d m   (m the in-plane, into-material unit direction of that face, e the edge direction),
// which lies on k's edge toward that neighbour only while  d (Q1 - v).w / (w.w) <= 1  (w = neighbour - v, Q1 = Q for d = 1), i.e. d <= w.w / ((Q1 - v).w).
// Hand-worked case below: a 10-cube whose vertical edge A at (0,0) is chamfered with s_A = 3 on the face y = 0 and o_A = 2 on the face x = 0 leaves the top edge B'
// from (3,0,10) to (10,0,10) ending on the oblique chamfer face; the rail on the top face (distance o_B) pierces the chamfer plane x/3 + y/2 = 1 at
// x = 3 (1 - o_B/2), which is on the chamfer face's top edge only while o_B <= 2 = o_A; the rail on y = 0 (distance s_B) pierces it at z = 10 - s_B on the
// chamfer face's vertical edge of length 10, so s_B <= 10. Expected limits {2, 10}. Independent oracles: that closed form, the MEASURED accept/refuse boundary of
// the real ChamferConvexEdge found by bisection, and the removed volume at 0.99 x the bound against V_box - sA oA/2 L_A - sB oB/2 L_B + overlap.
// Failing first: the function did not exist (this file did not compile against the unchanged headers); the measured current behaviour is the refusal
// "the chamfer overruns a third face" (pinned below), never a silent over-length chamfer.
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
using dino8::kernel::MaxChamferDistanceWithEnds;
using dino8::kernel::Point3d;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }

std::string ReasonOf(const std::function<void()>& f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  return "";
}

bool Accepts(const std::function<void()>& f) { return ReasonOf(f).empty(); }

double Boundary(const std::function<bool(double)>& accept, double lo, double hi) {
  for (int i = 0; i < 80; ++i) {
    const double mid = 0.5 * (lo + hi);
    (accept(mid) ? lo : hi) = mid;
  }
  return lo;
}

constexpr double kTiny = 1e-3;

// Chamfer-pair volume (chamfer_pair_legs.cpp's closed form) for a 10-cube, L_A = L_B = 10.
double PairVolume(double sa, double oa, double sb, double ob) {
  const double m = std::min(oa, ob);
  const double overlap = sa * sb * (m - m * m * (1.0 / oa + 1.0 / ob) / 2.0 + m * m * m / (3.0 * oa * ob));
  return 1000.0 - sa * oa / 2.0 * 10.0 - sb * ob / 2.0 * 10.0 + overlap;
}

struct Case { double sa, oa; };

void CheckObliqueEnd(const Case& c, const ON_Xform* xf, const std::string& tag) {
  Brep cube = Brep::Box(0, 0, 0, 10, 10, 10);
  auto P = [&](double x, double y, double z) {
    const Point3d p(x, y, z);
    return xf ? *xf * p : p;
  };
  if (xf) cube = cube.Transform(*xf);
  // Vertical edge A, (distance_i, distance_j) = (shared face y = 0, other face x = 0) in ChamferConvexEdge's own convention (hand-derived mapping of
  // chamfer_pair_distances.cpp: the face y = 0 walks the vertical edge top -> bottom).
  const Brep one = ChamferConvexEdge(cube, P(0, 0, 10), P(0, 0, 0), c.sa, c.oa);
  const Point3d p0 = P(c.sa, 0, 10), p1 = P(10, 0, 10);
  const auto limit = MaxChamferDistanceWithEnds(one, p0, p1);
  // Measured accept/refuse boundary of the real construction.
  const double mi = Boundary([&](double d) { return Accepts([&] { ChamferConvexEdge(one, p0, p1, d, kTiny); }); }, 1e-6, 1e3);
  const double mj = Boundary([&](double d) { return Accepts([&] { ChamferConvexEdge(one, p0, p1, kTiny, d); }); }, 1e-6, 1e3);
  Check(std::fabs(limit.max_distance_i - mi) < 1e-6 && std::fabs(limit.max_distance_j - mj) < 1e-6,
        (tag + "limits " + std::to_string(limit.max_distance_i) + ", " + std::to_string(limit.max_distance_j) + " vs measured accept/refuse boundary " +
         std::to_string(mi) + ", " + std::to_string(mj)).c_str());
  // Closed form: the top-face rail is bounded by o_A (distance_i), the y = 0 rail by the chamfer face's edge length 10 (distance_j).
  Check(std::fabs(limit.max_distance_i - c.oa) < 1e-9 && std::fabs(limit.max_distance_j - 10.0) < 1e-9,
        (tag + "closed form {o_A, 10} = {" + std::to_string(c.oa) + ", 10}").c_str());
  // The plain extents ignore the end face: this is what the new bound corrects.
  Check(limit.max_distance_i < 10.0 - 1e-6, (tag + "tighter than the face extents the edge's own faces give").c_str());
  // 0.99 x the bound builds, with the removed volume the pair closed form (the chain is the pair, A first as the larger-other-setback, here o_B < o_A).
  const double ob = 0.99 * limit.max_distance_i, sb = 0.5;
  Brep out;
  const std::string why = ReasonOf([&] { out = ChamferConvexEdge(one, p0, p1, ob, sb); });
  Check(why.empty(), (tag + "0.99 x the bound builds" + (why.empty() ? std::string() : ": " + why)).c_str());
  if (why.empty()) {
    const double exact = PairVolume(c.sa, c.oa, sb, ob);
    const double v = out.TessellateToClosedMesh(8, 8).Volume();
    Check(std::fabs(v - exact) < 1e-7 * exact, (tag + "volume at 0.99 x the bound " + std::to_string(v) + " vs closed form " + std::to_string(exact)).c_str());
  }
  const std::string over = ReasonOf([&] { ChamferConvexEdge(one, p0, p1, 1.01 * limit.max_distance_i, sb); });
  Check(over.find("overruns a third face") != std::string::npos, (tag + "1.01 x the bound is refused as overrunning the third face: " + over.substr(0, 80)).c_str());
}

}  // namespace

D8_EXTRA_TEST(ChamferLimitWithEndsObliqueThirdFace) {
  CheckObliqueEnd({3.0, 2.0}, nullptr, "[chamfer ends sA=3 oA=2] ");
  CheckObliqueEnd({1.5, 3.0}, nullptr, "[chamfer ends sA=1.5 oA=3] ");
  ON_Xform rot_xf;
  rot_xf.Rotation(0.7, ON_3dVector(1, 2, 3), ON_3dPoint(0, 0, 0));
  const ON_Xform xf = ON_Xform::TranslationTransformation(ON_3dVector(3, -2, 1)) * rot_xf;
  CheckObliqueEnd({3.0, 2.0}, &xf, "[chamfer ends rotated sA=3 oA=2] ");
}

D8_EXTRA_TEST(ChamferLimitWithEndsPerpendicularEndsEqualTheFaceExtents) {
  // Perpendicular end faces (a box): the end condition adds nothing, so the bound equals MaxChamferDistance's extents {5, 6}.
  const Brep box = Brep::Box(0, 0, 0, 8, 6, 5);
  const auto lim = MaxChamferDistanceWithEnds(box, Point3d(0, 0, 5), Point3d(8, 0, 5));
  const double lo = std::min(lim.max_distance_i, lim.max_distance_j), hi = std::max(lim.max_distance_i, lim.max_distance_j);
  Check(std::fabs(lo - 5.0) < 1e-9 && std::fabs(hi - 6.0) < 1e-9, ("[chamfer ends box] limits " + std::to_string(lo) + ", " + std::to_string(hi) + " = the face extents 5 and 6").c_str());
  std::string m = ReasonOf([&] { MaxChamferDistanceWithEnds(box, Point3d(0, 0, 5), Point3d(8, 6, 5)); });
  Check(!m.empty(), ("[chamfer ends] two points that are not an edge are refused: " + m).c_str());
}
