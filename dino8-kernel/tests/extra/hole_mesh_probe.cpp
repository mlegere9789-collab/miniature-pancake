// PROBE (not a regression test): where does the one-hole / four-hole slab's mesh stop being a closed solid of the right volume?
// Prints, through d8test::Check(false, ...), so the CI log shows each line as a FAILED line. Drop in dino8-kernel/tests/extra/ on a
// throwaway branch; run with DINO8_TEST_FILTER=HoleMeshProbe.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/features.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {
using dino8::kernel::Brep;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

void Line(const std::string& s) { d8test::Check(false, ("PROBE " + s).c_str()); }

void Describe(const char* tag, const Brep& b, double slab_z_top, double expected_volume) {
  const ON_Brep& r = b.raw();
  char buf[400];
  std::snprintf(buf, sizeof buf, "%s: faces=%d valid=%d solid=%d expected_volume=%.4f", tag, r.m_F.Count(), (int)r.IsValid(),
                (int)r.IsSolid(), expected_volume);
  Line(buf);
  int naked = 0, over = 0;
  for (int i = 0; i < r.m_E.Count(); ++i) {
    if (r.m_E[i].m_edge_index < 0) continue;
    if (r.m_E[i].TrimCount() < 2) ++naked;
    if (r.m_E[i].TrimCount() > 2) ++over;
  }
  std::snprintf(buf, sizeof buf, "%s: edges naked(<2 trims)=%d over(>2 trims)=%d", tag, naked, over);
  Line(buf);
  for (int fi = 0; fi < r.m_F.Count(); ++fi) {
    const ON_BrepFace& f = r.m_F[fi];
    std::string loops;
    for (int k = 0; k < f.m_li.Count(); ++k) {
      const ON_BrepLoop& l = r.m_L[f.m_li[k]];
      loops += (l.m_type == ON_BrepLoop::inner ? " inner(" : " outer(") + std::to_string(l.m_ti.Count()) + ")";
    }
    ON_BoundingBox bb;
    f.GetBoundingBox(bb);
    std::snprintf(buf, sizeof buf, "%s: face %d surf=%s rev=%d bbox z[%.3f,%.3f] loops:", tag, fi,
                  f.SurfaceOf() ? f.SurfaceOf()->ClassId()->ClassName() : "none", (int)f.m_bRev, bb.m_min.z, bb.m_max.z);
    Line(std::string(buf) + loops);
  }
  const Mesh m = b.TessellateToClosedMesh(32, 128);
  const ON_Mesh& om = m.raw();
  double a_top = 0, a_bot = 0, a_other = 0;
  for (int t = 0; t < om.m_F.Count(); ++t) {
    const ON_MeshFace& mf = om.m_F[t];
    const int nv = mf.IsQuad() ? 4 : 3;
    ON_3dPoint p[4];
    for (int k = 0; k < nv; ++k) p[k] = om.m_V[mf.vi[k]];
    double area = 0.5 * ON_CrossProduct(p[1] - p[0], p[2] - p[0]).Length();
    if (nv == 4) area += 0.5 * ON_CrossProduct(p[2] - p[0], p[3] - p[0]).Length();
    bool top = true, bot = true;
    for (int k = 0; k < nv; ++k) {
      top = top && std::fabs(p[k].z - slab_z_top) < 1e-6;
      bot = bot && std::fabs(p[k].z) < 1e-6;
    }
    (top ? a_top : bot ? a_bot : a_other) += area;
  }
  const auto loops_n = m.NakedEdgeLoops();
  std::snprintf(buf, sizeof buf, "%s: mesh manifold=%d volume=%.4f area top=%.4f bottom=%.4f other=%.4f naked_loops=%zu", tag,
                (int)m.IsClosedManifold(), m.Volume(), a_top, a_bot, a_other, loops_n.size());
  Line(buf);
  for (size_t li = 0; li < loops_n.size() && li < 4; ++li) {
    ON_BoundingBox lb;
    for (int v : loops_n[li]) lb.Set(om.m_V[v], true);
    std::snprintf(buf, sizeof buf, "%s: naked loop %zu n=%zu bbox (%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f)", tag, li, loops_n[li].size(),
                  lb.m_min.x, lb.m_min.y, lb.m_min.z, lb.m_max.x, lb.m_max.y, lb.m_max.z);
    Line(buf);
  }
}
}  // namespace

D8_EXTRA_TEST(HoleMeshProbe) {
  const Vector3d down(0, 0, -1);
  const double r = 0.5, hole_volume = ON_PI * r * r * 4.0;
  const Brep slab = Brep::Box(0, 0, 0, 4, 16, 4);
  const Brep one = dino8::kernel::MakeHole(slab, Point3d(2, 2, 4), down, r, 0.0, true);
  // expected top area = 64 - pi r^2 for each hole removed, same for the bottom
  Describe("one hole", one, 4.0, 256.0 - hole_volume);
  const Brep two = dino8::kernel::MakeHole(one, Point3d(2, 5, 4), down, r, 0.0, true);
  Describe("two holes (2nd cut on the 1st result)", two, 4.0, 256.0 - 2 * hole_volume);
}
