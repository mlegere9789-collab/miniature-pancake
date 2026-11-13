// ReplaceBrepSurface: replacing a Brep's surface slot must repoint the faces' cached surface proxy.
//
// ON_BrepFace::SurfaceOf() returns the face's ON_SurfaceProxy cache first, so `brep.m_S[i] = fresh` alone leaves SurfaceOf() - and Brep::Area() and
// everything else reading through it - on the OLD surface. Closed form: face 0 of Box(0,0,0, 2,3,4) is a flat 2 x 3 (or 3 x 4 / 2 x 4) quad of area A0;
// replaced by the same plane scaled 2x in both directions its area is 4 A0, so the Brep's area grows by exactly 3 A0.
#include <cmath>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/brep_seam.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;

ON_NurbsSurface* ScaledCopy(const ON_Brep& b, int face, double k) {
  ON_NurbsSurface* ns = new ON_NurbsSurface;
  b.m_S[b.m_F[face].m_si]->GetNurbForm(*ns);
  const ON_BoundingBox box = ns->BoundingBox();
  const ON_3dPoint c = box.Center();
  ON_Xform x = ON_Xform::DiagonalTransformation(k);
  ON_Xform t = ON_Xform::TranslationTransformation(-c.x, -c.y, -c.z), tb = ON_Xform::TranslationTransformation(c.x, c.y, c.z);
  ns->Transform(tb * x * t);
  return ns;
}

D8_EXTRA_TEST(ReplaceBrepSurfaceRepointsFaces) {
  Brep plain = Brep::Box(0, 0, 0, 2, 3, 4);
  Brep fixed = plain;
  const double area0 = plain.Area();
  // Plain assignment (the bug): the cached proxy keeps pointing at the old surface.
  {
    ON_Brep& b = plain.raw();
    const int si = b.m_F[0].m_si;
    ON_Surface* fresh = ScaledCopy(b, 0, 2.0);
    ON_Surface* old = b.m_S[si];
    b.m_S[si] = fresh;
    d8test::Check(b.m_F[0].SurfaceOf() == old, "premise: plain m_S assignment leaves SurfaceOf() on the OLD surface (stale proxy)");
    b.m_S[si] = old;  // restore before `plain` is destroyed
    delete fresh;
  }
  ON_Brep& b = fixed.raw();
  const int si = b.m_F[0].m_si;
  ON_Surface* fresh = ScaledCopy(b, 0, 2.0);
  dino8::kernel::ReplaceBrepSurface(b, si, fresh, true);
  d8test::Check(b.m_F[0].SurfaceOf() == fresh && b.m_S[si] == fresh, "after ReplaceBrepSurface the face's SurfaceOf() is the new surface");
  const ON_3dVector e = fresh->BoundingBox().Diagonal();
  const double flat = (e.x > 1e-9 ? e.x : 1.0) * (e.y > 1e-9 ? e.y : 1.0) * (e.z > 1e-9 ? e.z : 1.0);  // product of the two nonzero extents = 4 A0
  const double grown = fixed.Area() - area0;
  d8test::Check(std::abs(grown - 3.0 * flat / 4.0) < 1e-6, "Brep::Area() grows by exactly 3 A0 (the new face has 4 A0): the Brep sees the replaced surface");
}

}  // namespace
