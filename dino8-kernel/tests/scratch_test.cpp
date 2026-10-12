#include <cmath>
#include <cstdio>
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

namespace {
// Reproduces the OLD (pre-this-round) single-ray ContainsPoint exactly,
// to compare against the new majority-voted one.
bool OldContainsPoint(const dino8::kernel::Mesh& mesh, dino8::kernel::Point3d point) {
  using namespace dino8::kernel;
  const ON_Mesh& m = mesh.raw();
  const Vector3d direction(0.6532814824, 0.2705980501, 0.7071067812);
  int crossing_count = 0;
  for (int i = 0; i < m.m_F.Count(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    auto test = [&](int i0, int i1, int i2) {
      const Point3d a(m.m_V[i0]), b(m.m_V[i1]), c(m.m_V[i2]);
      const Vector3d edge1 = b - a;
      const Vector3d edge2 = c - a;
      const Vector3d h = ON_CrossProduct(direction, edge2);
      const double det = ON_DotProduct(edge1, h);
      if (std::fabs(det) < 1e-12) return;
      const double inv_det = 1.0 / det;
      const Vector3d s = point - a;
      const double u = inv_det * ON_DotProduct(s, h);
      if (u < 0.0 || u > 1.0) return;
      const Vector3d q = ON_CrossProduct(s, edge1);
      const double v = inv_det * ON_DotProduct(direction, q);
      if (v < 0.0 || u + v > 1.0) return;
      const double t = inv_det * ON_DotProduct(edge2, q);
      if (t > 1e-12) ++crossing_count;
    };
    test(f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) test(f.vi[0], f.vi[2], f.vi[3]);
  }
  return (crossing_count % 2) == 1;
}
}  // namespace

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // An octahedron (6 vertices, 8 triangular faces, every vertex shared by
  // exactly 4 triangles) - the simplest closed mesh where many triangles
  // meet at one vertex, the classic ray-cast "grazes a shared vertex"
  // scenario. Query point P is placed so the OLD single fixed direction's
  // own ray passes EXACTLY through vertex (1,0,0) as it travels forward.
  Mesh oct;
  ON_Mesh& raw = oct.raw();
  raw.m_V.Append(ON_3fPoint(1, 0, 0));   // 0 +X
  raw.m_V.Append(ON_3fPoint(-1, 0, 0));  // 1 -X
  raw.m_V.Append(ON_3fPoint(0, 1, 0));   // 2 +Y
  raw.m_V.Append(ON_3fPoint(0, -1, 0));  // 3 -Y
  raw.m_V.Append(ON_3fPoint(0, 0, 1));   // 4 +Z
  raw.m_V.Append(ON_3fPoint(0, 0, -1));  // 5 -Z
  auto add_tri = [&raw](int a, int b, int c) {
    ON_MeshFace f;
    f.vi[0] = a;
    f.vi[1] = b;
    f.vi[2] = c;
    f.vi[3] = c;
    raw.m_F.Append(f);
  };
  // 8 faces, outward-facing winding.
  add_tri(0, 2, 4);
  add_tri(2, 1, 4);
  add_tri(1, 3, 4);
  add_tri(3, 0, 4);
  add_tri(2, 0, 5);
  add_tri(1, 2, 5);
  add_tri(3, 1, 5);
  add_tri(0, 3, 5);

  const Vector3d direction(0.6532814824, 0.2705980501, 0.7071067812);
  // Per-direction vote breakdown at the two confirmed-ambiguous k values.
  {
    static const double kPhi = 1.6180339887498948482;
    static const double kPhi2 = kPhi * kPhi;
    std::array<Vector3d, 5> dirs = {
        Vector3d(0.6532814824, 0.2705980501, 0.7071067812), Vector3d(1.0, kPhi, kPhi2), Vector3d(kPhi, kPhi2, 1.0),
        Vector3d(kPhi2, 1.0, kPhi),                          Vector3d(1.0, -kPhi, kPhi2),
    };
    for (Vector3d& d : dirs) d = d / d.Length();
    for (double k : {2.0, 3.0}) {
      const Point3d p = Point3d(1, 0, 0) - direction * k;
      std::printf("k=%.1f per-direction votes: ", k);
      const ON_Mesh& m = oct.raw();
      for (const Vector3d& d : dirs) {
        int crossings = 0;
        for (int i = 0; i < m.m_F.Count(); ++i) {
          const ON_MeshFace& f = m.m_F[i];
          const Point3d a(m.m_V[f.vi[0]]), b(m.m_V[f.vi[1]]), c(m.m_V[f.vi[2]]);
          const Vector3d edge1 = b - a, edge2 = c - a;
          const Vector3d h = ON_CrossProduct(d, edge2);
          const double det = ON_DotProduct(edge1, h);
          if (std::fabs(det) < 1e-12) continue;
          const double inv_det = 1.0 / det;
          const Vector3d s = p - a;
          const double u = inv_det * ON_DotProduct(s, h);
          if (u < 0.0 || u > 1.0) continue;
          const Vector3d q = ON_CrossProduct(s, edge1);
          const double v = inv_det * ON_DotProduct(d, q);
          if (v < 0.0 || u + v > 1.0) continue;
          const double t = inv_det * ON_DotProduct(edge2, q);
          if (t > 1e-12) ++crossings;
        }
        std::printf("%d ", crossings % 2);
      }
      std::printf("\n");
    }
  }
  for (double k : {0.1, 0.5, 1.0, 2.0, 3.0, 5.0, 10.0}) {
    const Point3d vertex(1, 0, 0);
    const Point3d p = vertex - direction * k;
    const bool old_result = OldContainsPoint(oct, p);
    const bool new_result = oct.ContainsPoint(p);
    const double l1 = std::fabs(p.x) + std::fabs(p.y) + std::fabs(p.z);
    const bool truly_inside = l1 < 1.0;  // exact octahedron containment: |x|+|y|+|z| < 1
    std::printf("k=%.2f p=(%.4f,%.4f,%.4f) l1=%.4f truly_inside=%d old=%d new=%d\n", k, p.x, p.y, p.z, l1,
                truly_inside, old_result, new_result);
  }

  ON::End();
  return 0;
}
