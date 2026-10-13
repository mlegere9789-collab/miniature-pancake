#include <cstdio>
#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // Probe: does BooleanCombineGeneral's own "3+ fragment loops" throw
  // actually fire on the identical edge-only-touching two-cube fixture
  // BooleanCombinePlanar/BooleanCombineMixed's own allow_non_manifold_result
  // tests use, before writing a real registered test around it.
  const Brep a = Brep::Box(0, 0, 0, 1, 1, 1);
  const Brep b = Brep::Box(1, 1, 0, 2, 2, 1);

  try {
    const Brep r = BooleanCombineGeneral(a, b, BooleanOp::Union, 0.001, false);
    std::printf("default path did NOT throw; FaceCount=%d NonManifoldEdges=%d\n", r.FaceCount(),
                r.Check().Count(Brep::CheckIssue::Kind::NonManifoldEdge));
  } catch (const std::exception& e) {
    std::printf("default path threw: %s\n", e.what());
  }

  try {
    const Brep r = BooleanCombineGeneral(a, b, BooleanOp::Union, 0.001, true);
    std::printf("allow_non_manifold path: FaceCount=%d NonManifoldEdges=%d\n", r.FaceCount(),
                r.Check().Count(Brep::CheckIssue::Kind::NonManifoldEdge));
    double area = 0.0;
    for (const Mesh& m : r.Tessellate(2, 2)) area += m.Area();
    std::printf("total tessellated area = %.9f (expect 12)\n", area);
  } catch (const std::exception& e) {
    std::printf("allow_non_manifold path threw: %s\n", e.what());
  }

  // Also probe the SymmetricDifference corner-overlap fixture used by the
  // Planar/Mixed tests, to see what this engine does for it (two-shell
  // Append either way, per the header's own disclosed scope).
  {
    const Brep a2 = Brep::Box(0, 0, 0, 2, 2, 2);
    const Brep b2 = Brep::Box(1, 1, 1, 3, 3, 3);
    try {
      const Brep r = BooleanCombineGeneral(a2, b2, BooleanOp::SymmetricDifference, 0.001, false);
      std::printf("XOR default: FaceCount=%d LumpRanges=%zu NonManifoldEdges=%d\n", r.FaceCount(),
                  r.LumpFaceRanges().size(), r.Check().Count(Brep::CheckIssue::Kind::NonManifoldEdge));
    } catch (const std::exception& e) {
      std::printf("XOR default threw: %s\n", e.what());
    }
    try {
      const Brep r = BooleanCombineGeneral(a2, b2, BooleanOp::SymmetricDifference, 0.001, true);
      std::printf("XOR allow_non_manifold: FaceCount=%d LumpRanges=%zu NonManifoldEdges=%d\n", r.FaceCount(),
                  r.LumpFaceRanges().size(), r.Check().Count(Brep::CheckIssue::Kind::NonManifoldEdge));
    } catch (const std::exception& e) {
      std::printf("XOR allow_non_manifold threw: %s\n", e.what());
    }
  }

  return 0;
}
