#include <cstdio>
#include <cmath>
#include <vector>
#include "dino8/kernel/surface.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  Mesh m;
  ON_Mesh& raw = m.raw();
  const double col_u[4] = {0.1, 0.35, 0.6, 0.85};
  for (int col = 0; col < 4; ++col) {
    raw.m_V.Append(ON_3fPoint(static_cast<float>(col), 0, 0));
    raw.m_V.Append(ON_3fPoint(static_cast<float>(col), 0, 1));
  }
  auto add_quad = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  add_quad(0, 2, 3, 1);
  add_quad(2, 4, 5, 3);
  add_quad(4, 6, 7, 5);
  add_quad(6, 0, 1, 7);

  std::vector<Point2d> uvs(8);
  for (int col = 0; col < 4; ++col) {
    uvs[static_cast<size_t>(2 * col)] = Point2d(col_u[col], 0.0);
    uvs[static_cast<size_t>(2 * col + 1)] = Point2d(col_u[col], 1.0);
  }

  const Mesh split = m.SplitUVSeam(uvs, 0.5);
  std::printf("orig verts=%d faces=%d -> split verts=%d faces=%d\n", m.VertexCount(),
              m.FaceCount(), split.VertexCount(), split.FaceCount());
  std::printf("HasTextureCoordinates=%d\n", split.HasTextureCoordinates());
  for (int i = 0; i < split.VertexCount(); ++i) {
    const Point2d uv = split.TextureCoordinateAt(i);
    std::printf("  vert %d: u=%g v=%g\n", i, uv.x, uv.y);
  }

  // No-seam sanity check
  Mesh no_seam;
  ON_Mesh& raw2 = no_seam.raw();
  raw2.m_V.Append(ON_3fPoint(0, 0, 0));
  raw2.m_V.Append(ON_3fPoint(1, 0, 0));
  raw2.m_V.Append(ON_3fPoint(1, 1, 0));
  raw2.m_V.Append(ON_3fPoint(0, 1, 0));
  ON_MeshFace nf;
  nf.vi[0] = 0; nf.vi[1] = 1; nf.vi[2] = 2; nf.vi[3] = 3;
  raw2.m_F.Append(nf);
  const std::vector<Point2d> no_seam_uvs = {Point2d(0.1, 0), Point2d(0.2, 0), Point2d(0.2, 1), Point2d(0.1, 1)};
  const Mesh no_seam_split = no_seam.SplitUVSeam(no_seam_uvs);
  std::printf("no_seam: orig verts=%d -> split verts=%d (expect equal)\n", no_seam.VertexCount(),
              no_seam_split.VertexCount());

  // Invalid-argument checks
  try {
    m.SplitUVSeam({Point2d(0, 0)});
    std::printf("ERROR: expected throw for bad size, did not throw\n");
  } catch (const std::invalid_argument& e) {
    std::printf("OK threw for bad size: %s\n", e.what());
  }
  try {
    m.SplitUVSeam(uvs, 0.0);
    std::printf("ERROR: expected throw for threshold=0, did not throw\n");
  } catch (const std::invalid_argument& e) {
    std::printf("OK threw for threshold=0: %s\n", e.what());
  }
  try {
    m.SplitUVSeam(uvs, 1.0);
    std::printf("ERROR: expected throw for threshold=1, did not throw\n");
  } catch (const std::invalid_argument& e) {
    std::printf("OK threw for threshold=1: %s\n", e.what());
  }

  ON::End();
  return 0;
}
