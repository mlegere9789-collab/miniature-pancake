// Internal helper (no public API): re-seamed copies of a rotationally symmetric host, for MakeBoss / MakeRib / MakeRibFlush.
//
// A boolean on a periodic face is only as good as where the face's parametrization seam (and, for a sphere, its poles) happens to
// fall relative to the tool. Measured: a rectangular boss on a torus's outer equator at azimuth 0 straddles the seam corner (u = 0 and
// v = 0 at once) and is refused ("an edge is claimed by 3 or more fragment loops"), while the same boss at azimuth 10, 45, 90, 135, 180
// and 270 degrees fuses. A host that is a single UNTRIMMED sphere or torus is invariant under rotation about its axis, so a copy rotated
// by an arbitrary angle is the SAME solid with its seam somewhere else: fusing the tool onto that copy gives the same body.
#pragma once

#include <algorithm>
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

// The `tolerance` MakeBoss / MakePocket / MakeRib hand to BooleanCombineGeneralSealed(). The engine's default, 0.001, is an ABSOLUTE
// surface-surface Newton accuracy: right for a model a few units across, but a rib 0.004 thick on a 0.1-unit plate (the 10 x 10 x 1 plate
// of the rib tests at 1/100 scale) is then within a quarter of the tolerance of its own thickness, and the fused solid comes back
// 0.43 percent short of the closed form (V = 1.0674e-4 against 1.072e-4) - valid, closed, and wrong - or the sealing is refused. The
// tolerance is therefore capped at 2.5e-3 of the tool's thinnest side and 1e-4 of the two bodies' joint diagonal; it is exactly the
// old 0.001 for every model whose tool is at least 0.4 thick and whose joint diagonal is at least 10 (all of the existing tests).
inline double ScaledBooleanTolerance(const Brep& solid, const Brep& tool) {
  const BoundingBox sb = solid.GetTightBoundingBox();
  const BoundingBox tb = tool.GetTightBoundingBox();
  const double lo[3] = {std::min(sb.min.x, tb.min.x), std::min(sb.min.y, tb.min.y), std::min(sb.min.z, tb.min.z)};
  const double hi[3] = {std::max(sb.max.x, tb.max.x), std::max(sb.max.y, tb.max.y), std::max(sb.max.z, tb.max.z)};
  const double diag = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
  const double ext[3] = {tb.max.x - tb.min.x, tb.max.y - tb.min.y, tb.max.z - tb.min.z};
  double thin = std::max({ext[0], ext[1], ext[2]});
  for (double e : ext) {
    if (e > 1e-6 * thin) thin = std::min(thin, e);
  }
  double tol = 1e-3;
  if (std::isfinite(diag) && diag > 0.0) tol = std::min(tol, 1e-4 * diag);
  if (std::isfinite(thin) && thin > 0.0) tol = std::min(tol, 2.5e-3 * thin);
  return std::max(tol, 1e-9 * (std::isfinite(diag) && diag > 0.0 ? diag : 1.0));
}

}  // namespace dino8::kernel::detail
