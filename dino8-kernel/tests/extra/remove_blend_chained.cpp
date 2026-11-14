// RemoveBlend on a solid that carries SEVERAL blends: two chained (disjoint) edge fillets of a 10-cube (constant r = 1 on the vertical edge at (0,0), r = 1.5
// on the top edge along x at y = z = 10), and the equal-radius mitred pair. Closed forms: each straight 90-degree edge removes a L r^2 (a = 1 - pi/4), so
// removing one blend of the chained pair must leave exactly the other's volume, and removing both must give the sharp box back. Measured, not assumed:
// the chained solids are read back through MixedFaces(), which carries every blend and its notch runs.
// The equal-radius mitred pair (FilletConvexEdgePairMitre) is covered by the second test: removing one of its two blends keeps the other.
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include "dino8/kernel/brep.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Point3d;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }

double Vol(const Brep& b) { return b.TessellateToClosedMeshAdaptive(1e-6).Volume(); }

std::string ReasonOf(const std::function<void()>& f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  return "";
}

void CheckSolid(const Brep& b, int faces, double exact, const std::string& tag) {
  ON_TextLog log;
  bool oriented = false, has_boundary = true;
  const bool manifold = b.raw().IsManifold(&oriented, &has_boundary);
  Check(b.raw().IsValid(&log) && b.raw().IsSolid() && manifold && oriented && !has_boundary && b.FaceCount() == faces,
        (tag + "valid, closed, oriented solid with " + std::to_string(faces) + " faces").c_str());
  Check(std::fabs(Vol(b) - exact) < 5e-7 * exact, (tag + "volume " + std::to_string(Vol(b)) + " vs closed form " + std::to_string(exact)).c_str());
}

}  // namespace

D8_EXTRA_TEST(RemoveBlendOnChainedFillets) {
  const double L = 10.0, rA = 1.0, rB = 1.5, a = 1.0 - ON_PI / 4.0;
  const Brep box = Brep::Box(0, 0, 0, L, L, L);
  const Brep chained = dino8::kernel::FilletConvexEdge(dino8::kernel::FilletConvexEdge(box, Point3d(0, 0, 0), Point3d(0, 0, L), rA), Point3d(0, L, L), Point3d(L, L, L), rB);
  const double vA = a * L * rA * rA, vB = a * L * rB * rB;  // the volume each fillet removes
  CheckSolid(chained, 8, L * L * L - vA - vB, "[chained] both blends: ");
  // A point on each cylinder: A has its axis at x = y = rA, B at y = z = L - rB.
  const Point3d on_a(rA - rA * std::cos(0.8), rA - rA * std::sin(0.8), 5.0);
  const Point3d on_b(5.0, L - rB + rB * std::sin(0.8), L - rB + rB * std::cos(0.8));
  Brep without_a, without_b;
  std::string why = ReasonOf([&] { without_a = dino8::kernel::RemoveBlend(chained, on_a); });
  Check(why.empty(), ("[chained] RemoveBlend on the first blend is accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (why.empty()) CheckSolid(without_a, 7, L * L * L - vB, "[chained, A removed] only B left: ");
  why = ReasonOf([&] { without_b = dino8::kernel::RemoveBlend(chained, on_b); });
  Check(why.empty(), ("[chained] RemoveBlend on the second blend is accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (why.empty()) CheckSolid(without_b, 7, L * L * L - vA, "[chained, B removed] only A left: ");
  // Removing both, one after the other, gives the sharp box back.
  if (why.empty() && without_a.FaceCount() > 0) {
    Brep sharp;
    why = ReasonOf([&] { sharp = dino8::kernel::RemoveBlend(without_a, on_b); });
    Check(why.empty(), ("[chained] the second removal is accepted too" + (why.empty() ? "" : ": " + why)).c_str());
    if (why.empty()) CheckSolid(sharp, 6, L * L * L, "[chained, both removed] the sharp box: ");
  }
  // Negative control: a point that is on no blend is refused.
  const std::string nowhere = ReasonOf([&] { dino8::kernel::RemoveBlend(chained, Point3d(5, 5, 5)); });
  Check(!nowhere.empty(), ("[chained] a point on no blend is refused: " + nowhere).c_str());
  // Negative control for the oracle: the volume with the radii swapped is outside the tolerance that was just passed.
  Check(std::fabs((L * L * L - a * L * rB * rB) - (L * L * L - vA)) > 1e-3 * L * L * L * 0.5, "[chained] negative control: swapping the two radii would NOT match");
}

D8_EXTRA_TEST(RemoveBlendOnTheMitredPair) {
  // Removing one blend of the equal-radius mitred pair: the other keeps its own fillet, which now runs the whole edge. Closed form for either survivor:
  // V_box - a L r^2; removing the survivor too gives the sharp box. Exact inverse of FilletConvexEdgePairMitre.
  const double L = 10.0, r = 1.0, a = 1.0 - ON_PI / 4.0;
  const Brep box = Brep::Box(0, 0, 0, L, L, L);
  const Brep mitred = dino8::kernel::FilletConvexEdgePairMitre(box, Point3d(0, 0, L), Point3d(0, 0, 0), Point3d(L, 0, L), r, r);
  const Point3d on_a(r - r * std::cos(0.8), r - r * std::sin(0.8), 3.0);                  // the vertical blend (axis along z)
  const Point3d on_b(7.0, r - r * std::sin(0.8), L - r + r * std::cos(0.8));              // the top-edge blend (axis along x)
  auto axis_of_only_cylinder = [&](const Brep& b, double& radius) {
    const Brep::MixedFacesResult mf = b.MixedFaces();
    radius = mf.cylindrical.size() == 1 ? mf.cylindrical[0].radius : -1.0;
    return mf.cylindrical.size() == 1 ? mf.cylindrical[0].frame.zaxis : ON_3dVector(0, 0, 0);
  };
  Brep without_a, without_b;
  std::string why = ReasonOf([&] { without_a = dino8::kernel::RemoveBlend(mitred, on_a); });
  Check(why.empty(), ("[mitre removal] RemoveBlend on the vertical blend is accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (why.empty()) {
    CheckSolid(without_a, 7, L * L * L - a * L * r * r, "[mitre removal, vertical removed] ");
    double rad = 0;
    const ON_3dVector ax = axis_of_only_cylinder(without_a, rad);
    Check(std::fabs(rad - r) < 1e-9 && std::fabs(std::fabs(ax.x) - 1.0) < 1e-9, "[mitre removal, vertical removed] one cylinder is left: radius r along x (the top-edge blend)");
  }
  why = ReasonOf([&] { without_b = dino8::kernel::RemoveBlend(mitred, on_b); });
  Check(why.empty(), ("[mitre removal] RemoveBlend on the top-edge blend is accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (why.empty()) {
    CheckSolid(without_b, 7, L * L * L - a * L * r * r, "[mitre removal, top removed] ");
    double rad = 0;
    const ON_3dVector ax = axis_of_only_cylinder(without_b, rad);
    Check(std::fabs(rad - r) < 1e-9 && std::fabs(std::fabs(ax.z) - 1.0) < 1e-9, "[mitre removal, top removed] one cylinder is left: radius r along z (the vertical blend)");
  }
  if (without_a.FaceCount() > 0) {
    Brep sharp;
    why = ReasonOf([&] { sharp = dino8::kernel::RemoveBlend(without_a, on_b); });
    Check(why.empty(), ("[mitre removal] removing the survivor too is accepted" + (why.empty() ? "" : ": " + why)).c_str());
    if (why.empty()) CheckSolid(sharp, 6, L * L * L, "[mitre removal, both removed] the sharp box: ");
  }
  // The same on a rotated, translated copy of a box with different edge lengths (8 x 6 x 5, r = 0.7): no dependence on the axis-aligned frame.
  const double r2 = 0.7;
  const Brep box2 = Brep::Box(0, 0, 0, 8, 6, 5);
  ON_Xform rot_xf;
  rot_xf.Rotation(0.7, ON_3dVector(1, 2, 3), ON_3dPoint(0, 0, 0));
  const ON_Xform xf = ON_Xform::TranslationTransformation(ON_3dVector(3, -2, 1)) * rot_xf;
  const Brep rot = dino8::kernel::FilletConvexEdgePairMitre(box2.Transform(xf), xf * Point3d(0, 0, 5), xf * Point3d(0, 0, 0), xf * Point3d(8, 0, 5), r2, r2);
  Brep rot_without_a;
  why = ReasonOf([&] { rot_without_a = dino8::kernel::RemoveBlend(rot, xf * Point3d(r2 - r2 * std::cos(0.8), r2 - r2 * std::sin(0.8), 2.0)); });
  Check(why.empty(), ("[mitre removal rotated] accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (why.empty()) CheckSolid(rot_without_a, 7, 240.0 - a * 8.0 * r2 * r2, "[mitre removal rotated, vertical removed] the 8-long top edge keeps its fillet: ");
  // Negative control: the closed form for the OTHER edge length does not match.
  Check(std::fabs((240.0 - a * 5.0 * r2 * r2) - (240.0 - a * 8.0 * r2 * r2)) > 1e-4 * 240.0, "[mitre removal rotated] negative control: the other edge's closed form would NOT match");
}
