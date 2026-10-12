#include <cmath>
#include <cstdio>
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // Mesh::ComputeBoxMappingUVs() composed with Mesh::Faceted(), exercised
  // standalone: the full dino8_kernel_tests binary aborts earlier (in the
  // pre-existing, disclosed BooleanCombineGeneral "3+ fragment loops"
  // limitation, which several early TestBooleanCombineGeneral* fixtures
  // happen to trip) before it ever reaches
  // TestMeshComputeBoxMappingUVsFacetedResolvesSeam, so this checks the
  // same logic that test exercises, directly, in isolation.

  // Build the same unit box as MakeQuadBoxMesh(0,0,0,1,1,1): 8 vertices,
  // 6 quad faces, in the exact same winding/index order.
  Mesh box;
  ON_Mesh& raw = box.raw();
  raw.m_V.Append(ON_3fPoint(0, 0, 0));  // 0
  raw.m_V.Append(ON_3fPoint(1, 0, 0));  // 1
  raw.m_V.Append(ON_3fPoint(1, 1, 0));  // 2
  raw.m_V.Append(ON_3fPoint(0, 1, 0));  // 3
  raw.m_V.Append(ON_3fPoint(0, 0, 1));  // 4
  raw.m_V.Append(ON_3fPoint(1, 0, 1));  // 5
  raw.m_V.Append(ON_3fPoint(1, 1, 1));  // 6
  raw.m_V.Append(ON_3fPoint(0, 1, 1));  // 7

  auto add_quad = [&raw](int a, int b, int c, int d) {
    ON_MeshFace face;
    face.vi[0] = a;
    face.vi[1] = b;
    face.vi[2] = c;
    face.vi[3] = d;
    raw.m_F.Append(face);
  };

  add_quad(0, 3, 2, 1);  // bottom (-z), face 0
  add_quad(4, 5, 6, 7);  // top (+z), face 1
  add_quad(0, 1, 5, 4);  // front (-y), face 2
  add_quad(3, 7, 6, 2);  // back (+y), face 3
  add_quad(0, 4, 7, 3);  // left (-x), face 4
  add_quad(1, 2, 6, 5);  // right (+x), face 5

  const auto original_uvs = box.ComputeBoxMappingUVs(1.0);
  std::printf("original_uvs[6] = (%.9f, %.9f) (expect (1,1))\n", original_uvs[6].x, original_uvs[6].y);
  std::printf("matches X-dominant tie-break (1,1): %d\n",
              std::fabs(original_uvs[6].x - 1.0) < 1e-9 && std::fabs(original_uvs[6].y - 1.0) < 1e-9);
  std::printf("does NOT match back face's own (-1,1): %d\n", std::fabs(original_uvs[6].x - (-1.0)) > 0.5);

  const Mesh faceted = box.Faceted();
  const auto faceted_uvs = faceted.ComputeBoxMappingUVs(1.0);

  const Point2d top_copy = faceted_uvs[1 * 4 + 2];
  const Point2d back_copy = faceted_uvs[3 * 4 + 2];
  const Point2d right_copy = faceted_uvs[5 * 4 + 2];

  std::printf("top_copy    = (%.9f, %.9f) (expect (1,1))\n", top_copy.x, top_copy.y);
  std::printf("right_copy  = (%.9f, %.9f) (expect (1,1))\n", right_copy.x, right_copy.y);
  std::printf("back_copy   = (%.9f, %.9f) (expect (-1,1))\n", back_copy.x, back_copy.y);

  std::printf("top_copy correct: %d\n", std::fabs(top_copy.x - 1.0) < 1e-9 && std::fabs(top_copy.y - 1.0) < 1e-9);
  std::printf("right_copy correct: %d\n",
              std::fabs(right_copy.x - 1.0) < 1e-9 && std::fabs(right_copy.y - 1.0) < 1e-9);
  std::printf("back_copy correct: %d\n",
              std::fabs(back_copy.x - (-1.0)) < 1e-9 && std::fabs(back_copy.y - 1.0) < 1e-9);

  ON::End();
  return 0;
}
