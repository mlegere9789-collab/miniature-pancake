// ChamferConvexEdgePairDistances: the right-angle-corner chamfer pair taking each edge's two distances in ChamferConvexEdge's OWN convention - distance_i
// belongs to the face that walks vertex -> end on its loop, distance_j to the other face - which is what an application that already speaks
// ChamferConvexEdge(p0, p1, distance_i, distance_j) has in hand (ChamferConvexEdgePairLegs takes them as shared-face / other-face setbacks instead).
// Hand-derived mapping at the box corner V = (0, 0, lz) (outward-CCW loops): edge A = V -> (0,0,0) is walked V -> end by the shared face y = 0, so
// (A_i, A_j) = (shared, other); edge B = V -> (lx,0,lz) is walked V -> end by the top face (B's OTHER face), so (B_i, B_j) = (other, shared).
// The closed form is the one of chamfer_pair_legs.cpp: V = V_box - sA oA/2 L_A - sB oB/2 L_B + sA sB (m - m^2 (1/oA + 1/oB)/2 + m^3/(3 oA oB)), m = min(oA, oB).
// Failing first: the function did not exist (this file did not compile against the unchanged headers).
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
using dino8::kernel::ChamferConvexEdgePairDistances;
using dino8::kernel::Point3d;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }

std::string ReasonOf(const std::function<void()>& f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  return "";
}

constexpr double kLx = 8.0, kLy = 6.0, kLz = 5.0;

struct Legs { double sa, oa, sb, ob; };

double ClosedForm(const Legs& c) {
  const double m = std::min(c.oa, c.ob);
  const double overlap = c.sa * c.sb * (m - m * m * (1.0 / c.oa + 1.0 / c.ob) / 2.0 + m * m * m / (3.0 * c.oa * c.ob));
  return kLx * kLy * kLz - c.sa * c.oa / 2.0 * kLz - c.sb * c.ob / 2.0 * kLx + overlap;
}

// `swap_roles` passes the two edges the other way round; `mirror` hands the distances in the WRONG convention (negative control).
void CheckDistances(const Legs& c, const ON_Xform* xf, bool swap_roles, bool mirror, const std::string& tag) {
  Brep box = Brep::Box(0, 0, 0, kLx, kLy, kLz);
  auto P = [&](double x, double y, double z) {
    const Point3d p(x, y, z);
    return xf ? *xf * p : p;
  };
  if (xf) box = box.Transform(*xf);
  const Point3d V = P(0, 0, kLz), A = P(0, 0, 0), B = P(kLx, 0, kLz);
  const double a_i = mirror ? c.oa : c.sa, a_j = mirror ? c.sa : c.oa, b_i = mirror ? c.sb : c.ob, b_j = mirror ? c.ob : c.sb;
  Brep out;
  const std::string why = ReasonOf([&] {
    out = swap_roles ? ChamferConvexEdgePairDistances(box, V, B, A, b_i, b_j, a_i, a_j) : ChamferConvexEdgePairDistances(box, V, A, B, a_i, a_j, b_i, b_j);
  });
  const double exact = ClosedForm(c);
  if (mirror) {
    double v = -1.0;
    if (why.empty()) v = out.TessellateToClosedMesh(8, 8).Volume();
    Check(!why.empty() || std::fabs(v - exact) > 1e-4 * exact, (tag + "negative control: distances in the wrong convention do NOT give the closed form").c_str());
    return;
  }
  Check(why.empty(), (tag + "accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (!why.empty()) return;
  ON_TextLog log;
  bool oriented = false, has_boundary = true;
  const bool manifold = out.raw().IsManifold(&oriented, &has_boundary);
  Check(out.raw().IsValid(&log) && out.raw().IsSolid() && manifold && oriented && !has_boundary && out.FaceCount() == 8,
        (tag + "valid, closed, oriented solid with 8 faces").c_str());
  const double v = out.TessellateToClosedMesh(8, 8).Volume();
  Check(std::fabs(v - exact) < 1e-7 * exact, (tag + "volume " + std::to_string(v) + " vs closed form " + std::to_string(exact)).c_str());
}

}  // namespace

D8_EXTRA_TEST(ChamferPairDistancesConvention) {
  const Legs cases[] = {{1, 2, 1.5, 1}, {1, 1, 1.5, 2.5}, {2, 1, 1, 1}, {0.5, 3, 2, 1}, {1.2, 0.8, 0.6, 1.7}, {1, 1, 1, 1}};
  for (const Legs& c : cases) {
    const std::string tag = "[chamfer distances sA=" + std::to_string(c.sa) + " oA=" + std::to_string(c.oa) + " sB=" + std::to_string(c.sb) + " oB=" + std::to_string(c.ob) + "] ";
    CheckDistances(c, nullptr, false, false, tag + "A,B: ");
    CheckDistances(c, nullptr, true, false, tag + "B,A: ");
    if (std::fabs(c.sa - c.oa) > 1e-9 || std::fabs(c.sb - c.ob) > 1e-9) CheckDistances(c, nullptr, false, true, tag + "mirrored: ");
  }
}

D8_EXTRA_TEST(ChamferPairDistancesRotatedAndRefusals) {
  ON_Xform rot_xf;
  rot_xf.Rotation(0.7, ON_3dVector(1, 2, 3), ON_3dPoint(0, 0, 0));
  const ON_Xform xf = ON_Xform::TranslationTransformation(ON_3dVector(3, -2, 1)) * rot_xf;
  const Legs c{1.2, 0.8, 0.6, 1.7};
  CheckDistances(c, &xf, false, false, "[chamfer distances rotated, A,B] ");
  CheckDistances(c, &xf, true, false, "[chamfer distances rotated, B,A] ");
  const Brep box = Brep::Box(0, 0, 0, kLx, kLy, kLz);
  const Point3d V(0, 0, kLz), A(0, 0, 0), B(kLx, 0, kLz);
  std::string m = ReasonOf([&] { ChamferConvexEdgePairDistances(box, Point3d(0, 0, 2.5), A, B, 1, 1, 1, 1); });
  Check(m.find("trihedral") != std::string::npos, ("[chamfer distances] a point that is not a corner is refused: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePairDistances(box, V, A, Point3d(kLx, kLy, kLz), 1, 1, 1, 1); });
  Check(!m.empty(), ("[chamfer distances] two points that are not both edges at the corner are refused: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePairDistances(box, V, A, B, 1, 0, 1, 1); });
  Check(!m.empty(), ("[chamfer distances] a zero distance is refused: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePairDistances(box, V, A, B, 9, 1, 1, 1); });
  Check(!m.empty(), ("[chamfer distances] a distance beyond its face is refused with a reason: " + m).c_str());
}
