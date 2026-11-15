// Internal helper (no public API): re-seamed copies of a rotationally symmetric host, for MakeBoss / MakeRib / MakeRibFlush.
//
// A boolean on a periodic face is only as good as where the face's parametrization seam (and, for a sphere, its poles) happens to
// fall relative to the tool. Measured: a rectangular boss on a torus's outer equator at azimuth 0 straddles the seam corner (u = 0 and
// v = 0 at once) and is refused ("an edge is claimed by 3 or more fragment loops"), while the same boss at azimuth 10, 45, 90, 135, 180
// and 270 degrees fuses. A host that is a single UNTRIMMED sphere or torus is invariant under rotation about its axis, so a copy rotated
// by an arbitrary angle is the SAME solid with its seam somewhere else: fusing the tool onto that copy gives the same body.
#pragma once

#include <cmath>
#include <vector>

#include "dino8/kernel/brep.h"

namespace dino8::kernel::detail {

// Copies of `host` with its seam moved, or none when the host is not a single untrimmed sphere / torus.
inline ON_Xform Rot(double angle, const ON_3dVector& axis, const ON_3dPoint& center) {
  ON_Xform x;
  x.Rotation(angle, axis, center);
  return x;
}

inline std::vector<Brep> ReseamedHostVariants(const Brep& host) {
  std::vector<Brep> out;
  const ON_Brep& b = host.raw();
  if (b.m_F.Count() != 1) return out;
  const ON_BrepFace& face = b.m_F[0];
  const ON_Surface* surf = face.SurfaceOf();
  if (!surf || face.m_li.Count() > 1) return out;  // 0 loops: Brep::Sphere()/Torus() carry a bare surface
  // Untrimmed: every trim of the single loop is an iso boundary (or a singular pole edge) of the surface domain.
  if (face.m_li.Count() == 1) {
    const ON_BrepLoop& loop = b.m_L[face.m_li[0]];
    for (int k = 0; k < loop.m_ti.Count(); ++k) {
      const ON_BrepTrim& t = b.m_T[loop.m_ti[k]];
      if (t.m_iso == ON_Surface::not_iso && t.m_type != ON_BrepTrim::singular) return out;
    }
  }
  ON_3dPoint center;
  ON_3dVector axis;
  ON_Sphere sph;
  ON_Torus tor;
  if (surf->IsSphere(&sph, 1e-6)) {
    center = sph.Center();
    axis = ON_3dVector(0, 0, 1);
  } else if (surf->IsTorus(&tor, 1e-6)) {
    center = tor.plane.origin;
    axis = tor.plane.zaxis;
  } else {
    return out;
  }
  for (const double angle : {0.9, 2.1, 3.3, 4.5}) {
    out.push_back(host.Transform(Rot(angle, axis, center)));
  }
  if (surf->IsSphere(nullptr, 1e-6)) {
    // A sphere also has poles: tip it about an axis in its equatorial plane.
    out.push_back(host.Transform(Rot(0.9, ON_3dVector(1, 0, 0), center)));
    out.push_back(host.Transform(Rot(2.2, ON_3dVector(0, 1, 0), center)));
  }
  return out;
}

}  // namespace dino8::kernel::detail
