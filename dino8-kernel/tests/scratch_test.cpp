#include <cstdio>
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // Regression repro: plain two-operand Difference, boss centered on a
  // bigger face, NO prior union at all.
  {
    const Brep box = Brep::Box(0, 0, 0, 4, 4, 4);
    const Brep boss = Brep::Box(1.5, 1.5, 4, 2.5, 2.5, 5);
    const Brep d_ab = BooleanCombineGeneral(box, boss, BooleanOp::Difference);
    const Mesh mesh_ab = TessellateGeneralBooleanClosedMesh(d_ab, 8, 8);
    std::printf("plain box-boss: faces=%d valid=%d closed_manifold=%d volume=%f (expect 64)\n", d_ab.FaceCount(),
                d_ab.raw().IsValid(), mesh_ab.IsClosedManifold(), mesh_ab.Volume());
    const Brep d_ba = BooleanCombineGeneral(boss, box, BooleanOp::Difference);
    const Mesh mesh_ba = TessellateGeneralBooleanClosedMesh(d_ba, 8, 8);
    std::printf("plain boss-box: faces=%d valid=%d closed_manifold=%d volume=%f (expect 1)\n", d_ba.FaceCount(),
                d_ba.raw().IsValid(), mesh_ba.IsClosedManifold(), mesh_ba.Volume());
  }

  // Single-boss union-then-subtract-back-out.
  {
    const Brep box = Brep::Box(0, 0, 0, 8, 8, 4);
    const Brep boss = Brep::Box(1, 1, 4, 2, 2, 5);
    const Brep combined = BooleanCombineGeneral(box, boss, BooleanOp::Union);
    const Brep back = BooleanCombineGeneral(combined, boss, BooleanOp::Difference);
    const Mesh m = TessellateGeneralBooleanClosedMesh(back, 16, 16);
    std::printf("single-boss subtract-back-out: faces=%d valid=%d closed_manifold=%d volume=%f (expect 256)\n",
                back.FaceCount(), back.raw().IsValid(), m.IsClosedManifold(), m.Volume());
  }

  // The actual reported bug: two independent bosses, subtract either back out.
  {
    const Brep box = Brep::Box(0, 0, 0, 8, 8, 4);
    const Brep boss_a = Brep::Box(1, 1, 4, 2, 2, 5);
    const Brep boss_b = Brep::Box(5, 5, 4, 6, 6, 5);
    const Brep combined = BooleanCombineGeneral(box, boss_a, BooleanOp::Union);
    const Brep both = BooleanCombineGeneral(combined, boss_b, BooleanOp::Union);
    std::printf("both faces=%d valid=%d\n", both.FaceCount(), both.raw().IsValid());
    for (const char* name : {"boss_a", "boss_b"}) {
      const Brep& tool = (std::string(name) == "boss_a") ? boss_a : boss_b;
      const Brep result = BooleanCombineGeneral(both, tool, BooleanOp::Difference);
      const Mesh m = TessellateGeneralBooleanClosedMesh(result, 16, 16);
      std::printf("subtract %s: faces=%d valid=%d closed_manifold=%d volume=%f (expect 257)\n", name,
                  result.FaceCount(), result.raw().IsValid(), m.IsClosedManifold(), m.Volume());
    }
  }

  // Multi-boss UNION only (the already-existing, previously-passing test
  // this session must not regress).
  {
    const Brep box = Brep::Box(0, 0, 0, 8, 8, 4);
    const Brep boss1 = Brep::Box(1, 1, 4, 2, 2, 5);
    const Brep boss2 = Brep::Box(5, 5, 4, 6, 6, 5);
    const Brep combined = BooleanCombineGeneral(box, boss1, BooleanOp::Union);
    const Brep both = BooleanCombineGeneral(combined, boss2, BooleanOp::Union);
    const Mesh m = TessellateGeneralBooleanClosedMesh(both, 16, 16);
    std::printf("multi-boss union only: faces=%d valid=%d closed_manifold=%d volume=%f (expect 258)\n",
                both.FaceCount(), both.raw().IsValid(), m.IsClosedManifold(), m.Volume());
  }

  ON::End();
  return 0;
}
