// Two 45-degree chamfers (equal legs d_A and d_B) of two edges that meet at a box corner, the third edge staying sharp, with DIFFERENT distances per edge.
// ChamferConvexEdge is called twice; the second chamfer's end then meets the first chamfer's oblique face, in the interior of its edge when d_A != d_B
// (the equal-distance case lands exactly on a vertex and has its own regression test). The LARGER chamfer must come first: the other order is refused
// (its end would meet two faces), and that limit is pinned here. Independent closed form: the two triangular prisms
// (d_A^2/2) L_A and (d_B^2/2) L_B minus their overlap, which is the region x + y <= d_A, y + w <= d_B (x, y, w >= 0, w the height below the top):
//   overlap = integral_0^m (d_A - y)(d_B - y) dy = d_A d_B m - (d_A + d_B) m^2 / 2 + m^3 / 3,  m = min(d_A, d_B).
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
using dino8::kernel::Point3d;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }

std::string ReasonOf(const std::function<void()>& f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  return "";
}

double Overlap(double da, double db) {
  const double m = std::min(da, db);
  return da * db * m - (da + db) * m * m / 2.0 + m * m * m / 3.0;
}

double ClosedForm(double lx, double ly, double lz, double da, double db) {
  return lx * ly * lz - da * da / 2.0 * lz - db * db / 2.0 * lx + Overlap(da, db);
}

struct Case { double lx, ly, lz, da, db; };

// Box [0,lx] x [0,ly] x [0,lz]; corner V = (0, 0, lz); edge A = V -> (0,0,0) (length lz), edge B = V -> (lx, 0, lz) (length lx).
void RunCase(const Case& c, const ON_Xform* xf, bool a_first, const std::string& tag) {
  Brep box = Brep::Box(0, 0, 0, c.lx, c.ly, c.lz);
  auto P = [&](double x, double y, double z) {
    const Point3d p(x, y, z);
    return xf ? *xf * p : p;
  };
  if (xf) box = box.Transform(*xf);
  Brep out;
  const std::string why = ReasonOf([&] {
    // After the first chamfer the second edge starts at that chamfer's new vertex: the top edge now runs (dA, 0, lz) -> (lx, 0, lz), the vertical edge
    // (0, 0, lz - dB) -> (0, 0, 0).
    const Brep one = a_first ? ChamferConvexEdge(box, P(0, 0, c.lz), P(0, 0, 0), c.da, c.da) : ChamferConvexEdge(box, P(0, 0, c.lz), P(c.lx, 0, c.lz), c.db, c.db);
    out = a_first ? ChamferConvexEdge(one, P(c.da, 0, c.lz), P(c.lx, 0, c.lz), c.db, c.db) : ChamferConvexEdge(one, P(0, 0, c.lz - c.db), P(0, 0, 0), c.da, c.da);
  });
  const bool larger_first = (a_first ? c.da >= c.db : c.db >= c.da);
  if (!larger_first) {
    // KNOWN LIMIT, measured: when the second chamfer is the larger one, its end meets TWO faces (the first chamfer's oblique face and the side face), which
    // ChamferConvexEdge's one-third-face end condition cannot express. It is refused with a reason, never a bad solid; do the larger chamfer first.
    Check(why.find("overruns a third face") != std::string::npos, (tag + "the smaller chamfer first is refused with the overrun reason: " + (why.empty() ? "(accepted!)" : why.substr(0, 90))).c_str());
    return;
  }
  Check(why.empty(), (tag + "both chamfers accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (!why.empty()) return;
  ON_TextLog log;
  bool oriented = false, has_boundary = true;
  const bool manifold = out.raw().IsManifold(&oriented, &has_boundary);
  Check(out.raw().IsValid(&log) && out.raw().IsSolid() && manifold && oriented && !has_boundary && out.FaceCount() == 8,
        (tag + "valid, closed, oriented solid with 8 faces (box 6 + 2 chamfer planes)").c_str());
  const double exact = ClosedForm(c.lx, c.ly, c.lz, c.da, c.db);
  const double v = out.TessellateToClosedMesh(8, 8).Volume();
  Check(std::fabs(v - exact) < 1e-7 * exact, (tag + "volume " + std::to_string(v) + " vs closed form " + std::to_string(exact)).c_str());
  // Negative control: ignoring the overlap of the two prisms would be wrong by a measurable amount.
  const double no_overlap = exact - Overlap(c.da, c.db);
  Check(std::fabs(v - no_overlap) > 1e-4 * exact, (tag + "negative control: ignoring the overlap would NOT match").c_str());
}

}  // namespace

D8_EXTRA_TEST(ChamferPairAtACornerDifferentDistances) {
  const Case cases[] = {{10, 10, 10, 1, 1}, {10, 10, 10, 1, 2}, {10, 10, 10, 2, 1}, {10, 10, 10, 0.5, 3}, {10, 10, 10, 3, 0.5}, {8, 6, 5, 1.5, 0.7}, {8, 6, 5, 0.7, 1.5}};
  for (const Case& c : cases) {
    const std::string tag = "[chamfer pair " + std::to_string(c.lx) + "x" + std::to_string(c.ly) + "x" + std::to_string(c.lz) + " dA=" + std::to_string(c.da) + " dB=" + std::to_string(c.db) + "] ";
    RunCase(c, nullptr, true, tag + "A then B: ");
    RunCase(c, nullptr, false, tag + "B then A: ");
  }
}

D8_EXTRA_TEST(ChamferPairAtACornerRotatedAndRefusals) {
  ON_Xform rot_xf;
  rot_xf.Rotation(0.7, ON_3dVector(1, 2, 3), ON_3dPoint(0, 0, 0));
  const ON_Xform xf = ON_Xform::TranslationTransformation(ON_3dVector(3, -2, 1)) * rot_xf;
  const Case c{8, 6, 5, 1.5, 0.7};
  RunCase(c, &xf, true, "[chamfer pair rotated, A then B] ");
  RunCase(c, &xf, false, "[chamfer pair rotated, B then A] ");
  // A second distance that overruns the face is refused with a reason, never a bad solid.
  const Brep box = Brep::Box(0, 0, 0, 8, 6, 5);
  const std::string m = ReasonOf([&] { ChamferConvexEdge(ChamferConvexEdge(box, Point3d(0, 0, 5), Point3d(0, 0, 0), 1.0, 1.0), Point3d(1, 0, 5), Point3d(8, 0, 5), 9.0, 9.0); });
  Check(!m.empty(), ("[chamfer pair] a distance beyond the face is refused: " + m).c_str());
}
