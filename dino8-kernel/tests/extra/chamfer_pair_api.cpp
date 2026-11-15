// ChamferConvexEdgePair: two 45-degree chamfers (one distance per edge) of two edges meeting at a right-angle corner, third edge sharp, in ANY argument order.
// ChamferConvexEdge alone refuses the smaller-chamfer-first order (see chamfer_pair_corner.cpp); the pair function does the larger one first.
// Independent closed form (same as chamfer_pair_corner.cpp): V = V_box - dA^2/2 L_A - dB^2/2 L_B + overlap,
//   overlap = dA dB m - (dA + dB) m^2 / 2 + m^3 / 3, m = min(dA, dB).
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
using dino8::kernel::ChamferConvexEdgePair;
using dino8::kernel::Point3d;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }

std::string ReasonOf(const std::function<void()>& f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  return "";
}

double ClosedForm(double lx, double ly, double lz, double da, double db) {
  const double m = std::min(da, db);
  const double overlap = da * db * m - (da + db) * m * m / 2.0 + m * m * m / 3.0;
  return lx * ly * lz - da * da / 2.0 * lz - db * db / 2.0 * lx + overlap;
}

struct Case { double lx, ly, lz, da, db; };

void CheckPair(const Case& c, const ON_Xform* xf, bool swap_roles, const std::string& tag) {
  Brep box = Brep::Box(0, 0, 0, c.lx, c.ly, c.lz);
  auto P = [&](double x, double y, double z) {
    const Point3d p(x, y, z);
    return xf ? *xf * p : p;
  };
  if (xf) box = box.Transform(*xf);
  // corner V = (0, 0, lz); edge A -> (0, 0, 0) with distance da; edge B -> (lx, 0, lz) with distance db; `swap_roles` passes them the other way round.
  Brep out;
  const std::string why = ReasonOf([&] {
    out = swap_roles ? ChamferConvexEdgePair(box, P(0, 0, c.lz), P(c.lx, 0, c.lz), P(0, 0, 0), c.db, c.da)
                     : ChamferConvexEdgePair(box, P(0, 0, c.lz), P(0, 0, 0), P(c.lx, 0, c.lz), c.da, c.db);
  });
  Check(why.empty(), (tag + "accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (!why.empty()) return;
  ON_TextLog log;
  bool oriented = false, has_boundary = true;
  const bool manifold = out.raw().IsManifold(&oriented, &has_boundary);
  Check(out.raw().IsValid(&log) && out.raw().IsSolid() && manifold && oriented && !has_boundary && out.FaceCount() == 8,
        (tag + "valid, closed, oriented solid with 8 faces").c_str());
  const double exact = ClosedForm(c.lx, c.ly, c.lz, c.da, c.db);
  const double v = out.TessellateToClosedMesh(8, 8).Volume();
  Check(std::fabs(v - exact) < 1e-7 * exact, (tag + "volume " + std::to_string(v) + " vs closed form " + std::to_string(exact)).c_str());
  Check(std::fabs(v - (exact - 0.5 * std::min(c.da, c.db) * std::min(c.da, c.db) * std::min(c.da, c.db) / 3.0)) > 1e-5 * exact, (tag + "negative control: a wrong overlap would NOT match").c_str());
}

}  // namespace

D8_EXTRA_TEST(ChamferPairAnyOrder) {
  const Case cases[] = {{10, 10, 10, 1, 1}, {10, 10, 10, 1, 2}, {10, 10, 10, 2, 1}, {10, 10, 10, 0.5, 3}, {10, 10, 10, 3, 0.5}, {8, 6, 5, 1.5, 0.7}, {8, 6, 5, 0.7, 1.5}};
  for (const Case& c : cases) {
    const std::string tag = "[chamfer pair api " + std::to_string(c.lx) + "x" + std::to_string(c.ly) + "x" + std::to_string(c.lz) + " dA=" + std::to_string(c.da) + " dB=" + std::to_string(c.db) + "] ";
    CheckPair(c, nullptr, false, tag + "A,B: ");
    CheckPair(c, nullptr, true, tag + "B,A: ");
  }
}

D8_EXTRA_TEST(ChamferPairRotatedAndRefusals) {
  ON_Xform rot_xf;
  rot_xf.Rotation(0.7, ON_3dVector(1, 2, 3), ON_3dPoint(0, 0, 0));
  const ON_Xform xf = ON_Xform::TranslationTransformation(ON_3dVector(3, -2, 1)) * rot_xf;
  const Case c{8, 6, 5, 0.7, 1.5};
  CheckPair(c, &xf, false, "[chamfer pair api rotated, A,B] ");
  CheckPair(c, &xf, true, "[chamfer pair api rotated, B,A] ");
  const Brep box = Brep::Box(0, 0, 0, 8, 6, 5);
  const Point3d V(0, 0, 5), A(0, 0, 0), B(8, 0, 5);
  std::string m = ReasonOf([&] { ChamferConvexEdgePair(box, Point3d(0, 0, 2.5), A, B, 1.0, 1.0); });
  Check(m.find("trihedral") != std::string::npos, ("[chamfer pair api] a point that is not a corner is refused: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePair(box, V, A, Point3d(8, 6, 5), 1.0, 1.0); });
  Check(!m.empty(), ("[chamfer pair api] two points that are not both edges at the corner are refused: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePair(box, V, A, B, 0.0, 1.0); });
  Check(!m.empty(), ("[chamfer pair api] a zero distance is refused: " + m).c_str());
  m = ReasonOf([&] { ChamferConvexEdgePair(box, V, A, B, 1.0, 9.0); });
  Check(!m.empty(), ("[chamfer pair api] a distance beyond the face is refused with a reason, never a bad solid: " + m).c_str());
}
