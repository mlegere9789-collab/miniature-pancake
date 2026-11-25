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
void Check(bool condition, const char* what) { d8test::Check(condition, what); }
}  // namespace
// A tapered (variable-radius) fillet chained with a constant-radius one on the same box, in either order. 10-cube: the vertical edge at (0,0)
// gets a constant radius r1, the top edge along x at y = z = 10 gets a radius running linearly from s0 to s1. The two edges share no vertex.
// Removed volume per straight 90-degree edge: a * integral of r(t)^2 dt with a = 1 - pi/4, i.e. a L r1^2 for the constant one and
// a L (s0^2 + s0 s1 + s1^2) / 3 for the linear taper (the frustum formula).
namespace chained_tapered_fillet_tests {

using dino8::kernel::Brep;
using dino8::kernel::FilletConvexEdge;
using dino8::kernel::FilletConvexEdgeTapered;
using dino8::kernel::Point3d;

std::string ReasonOf(const std::function<void()>& f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  return "";
}

double MeshVol(const Brep& b) { return b.TessellateToClosedMeshAdaptive(1e-6).Volume(); }

void TestTaperedFilletChainsWithAConstantRadiusFilletInEitherOrder() {
  const double L = 10.0, r1 = 1.0, s0 = 0.5, s1 = 1.5, a = 1.0 - ON_PI / 4.0;
  const Brep box = Brep::Box(0, 0, 0, L, L, L);
  const Point3d v0(0, 0, 0), v1(0, 0, L), t0(0, L, L), t1(L, L, L);
  const double taper_only = L * L * L - a * L * (s0 * s0 + s0 * s1 + s1 * s1) / 3.0;

  const Brep tap_alone = FilletConvexEdgeTapered(box, t0, t1, s0, s1);
  Check(tap_alone.raw().IsValid() && tap_alone.raw().IsSolid(), "[tapered chain] the tapered fillet alone is a valid closed solid");
  // The cone-built taper is not exactly the frustum formula (the sloped cone's section is not a circle): 1e-5 relative. The chained
  // oracle is then "tapered alone, measured, minus the exact constant-edge term" - the two edges share no vertex.
  const double exact = MeshVol(tap_alone) - a * L * r1 * r1;
  Check(std::fabs(MeshVol(tap_alone) - taper_only) < 1e-5 * taper_only,
        ("[tapered chain] tapered alone: volume " + std::to_string(MeshVol(tap_alone)) + " vs frustum closed form " + std::to_string(taper_only)).c_str());

  Brep const_then_taper, taper_then_const;
  std::string why = ReasonOf([&] { const_then_taper = FilletConvexEdgeTapered(FilletConvexEdge(box, v0, v1, r1), t0, t1, s0, s1); });
  Check(why.empty(), ("[tapered chain] a constant fillet, then a tapered fillet on the blended solid, is accepted" + (why.empty() ? "" : ": " + why)).c_str());
  why = ReasonOf([&] { taper_then_const = FilletConvexEdge(tap_alone, v0, v1, r1); });
  Check(why.empty(), ("[tapered chain] a tapered fillet, then a constant fillet on the blended solid, is accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (const_then_taper.FaceCount() == 0 || taper_then_const.FaceCount() == 0) return;
  for (const Brep* b : {&const_then_taper, &taper_then_const}) {
    const std::string tag = b == &const_then_taper ? "[tapered chain, constant first] " : "[tapered chain, taper first] ";
    ON_TextLog log;
    bool oriented = false, has_boundary = true;
    Check(b->raw().IsValid(&log) && b->raw().IsSolid() && b->raw().IsManifold(&oriented, &has_boundary) && oriented && !has_boundary && b->FaceCount() == 8,
          (tag + "valid, closed, oriented solid with 8 faces (box 6 + 2 blends)").c_str());
    Check(std::fabs(MeshVol(*b) - exact) < 2e-6 * exact, (tag + "volume " + std::to_string(MeshVol(*b)) + " vs closed form " + std::to_string(exact)).c_str());
  }
  Check(std::fabs(MeshVol(const_then_taper) - MeshVol(taper_then_const)) < 2e-6 * exact, "[tapered chain] order independence: both orders give the same volume");
  // Negative control: swapping the taper's end radii changes nothing for a symmetric taper, so use a 10% larger constant radius instead.
  const double wrong = MeshVol(tap_alone) - a * L * (1.1 * r1) * (1.1 * r1);
  Check(std::fabs(MeshVol(const_then_taper) - wrong) > 1e-4 * exact, "[tapered chain] negative control: a 10% larger constant radius would NOT match");
}

void TestTaperedChainRefusesAnEdgeWhoseEndMeetsTheBlend() {
  const double L = 10.0;
  const Brep blended = FilletConvexEdge(Brep::Box(0, 0, 0, L, L, L), Point3d(0, 0, 0), Point3d(0, 0, L), 1.0);
  // The top edge along x at y = 0 now starts at the first blend's tangent line (x = 1): its end meets the cylinder.
  const std::string m = ReasonOf([&] { FilletConvexEdgeTapered(blended, Point3d(1, 0, L), Point3d(L, 0, L), 0.3, 0.6); });
  Check(m.find("curved") != std::string::npos, ("[tapered chain] a tapered fillet whose end meets the existing blend is refused with a reason: " + m).c_str());
}

}  // namespace chained_tapered_fillet_tests

D8_EXTRA_TEST(TaperedFilletChainsWithAConstantRadiusFilletInEitherOrder) { chained_tapered_fillet_tests::TestTaperedFilletChainsWithAConstantRadiusFilletInEitherOrder(); }
D8_EXTRA_TEST(TaperedChainRefusesAnEdgeWhoseEndMeetsTheBlend) { chained_tapered_fillet_tests::TestTaperedChainRefusesAnEdgeWhoseEndMeetsTheBlend(); }
