// Brep::FromSurfaceWelded(): one untrimmed face with complete topology. Oracles: Euler characteristic of the
// surface's own topology (a rectangle V-E+F = 1, a periodic tube 0, a sphere 2) and the exact areas.
#include <cmath>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {
using dino8::kernel::Brep;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
int Chi(const Brep& b) { return b.LiveVertexCount() - b.LiveEdgeCount() + b.LiveFaceCount(); }
}  // namespace

D8_EXTRA_TEST(FromSurfaceWeldedRectangleAndSphere) {
  using d8test::Check;
  ON_TextLog log;
  // bilinear 3 x 4 patch in z = 0
  const NurbsSurface patch = NurbsSurface::FromControlGrid(
      {Point3d(0, 0, 0), Point3d(0, 4, 0), Point3d(3, 0, 0), Point3d(3, 4, 0)}, 2, 2, 1, 1);
  const Brep p = Brep::FromSurfaceWelded(patch);
  Check(p.raw().IsValid(&log), "rectangle: IsValid()");
  Check(p.LiveFaceCount() == 1 && p.LiveVertexCount() == 4 && p.LiveEdgeCount() == 4, "rectangle: 1 face, 4 vertices, 4 edges");
  Check(Chi(p) == 1, "rectangle: V - E + F == 1 (a disc)");
  int naked = 0;
  for (int e = 0; e < p.raw().m_E.Count(); ++e) naked += p.FacesOfEdge(e).size() == 1 && p.raw().m_E[e].m_ti.Count() == 1;
  Check(naked == 4, "rectangle: all 4 edges are naked (one trim)");
  Check(std::abs(p.Area() - 12.0) < 1e-9, "rectangle: Area() == 3 x 4");

  // a sphere's NURBS surface: closed in u, collapsed at both poles
  const Brep bare = Brep::Sphere(Point3d(0, 0, 0), 2.0);
  NurbsSurface sph;
  sph.raw() = *ON_NurbsSurface::Cast(bare.raw().m_S[0]);
  const Brep s = Brep::FromSurfaceWelded(sph);
  Check(s.raw().IsValid(&log), "sphere surface: IsValid()");
  Check(Chi(s) == 2, "sphere surface: V - E + F == 2");
  Check(s.raw().IsSolid(), "sphere surface: the single face closes up (IsSolid)");
  // Area() integrates the rational NURBS sphere to 1.8e-6 relative (measured; the bare Sphere() reads the identical value),
  // so the closed form is checked at 5e-6 and the welded face must agree with the bare one exactly.
  Check(std::abs(s.Area() - 16.0 * 3.14159265358979323846) < 5e-6 * 50.27, "sphere surface: Area() == 4 pi r^2 (5e-6)");
  Check(std::abs(s.Area() - bare.Area()) < 1e-12, "sphere surface: same Area() as the surface-only Sphere()");
}
