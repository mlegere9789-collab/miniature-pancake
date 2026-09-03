#include "dino8/kernel/brep.h"

namespace dino8::kernel {

Brep Brep::FromSurface(const NurbsSurface& surface) {
  Brep result;
  ON_Brep& brep = result.brep_;

  auto* surface_copy = new ON_NurbsSurface(surface.raw());
  const int surface_index = brep.AddSurface(surface_copy);

  ON_BrepFace& face = brep.NewFace(surface_index);
  (void)face;

  brep.SetTrimIsoFlags();

  return result;
}

int Brep::FaceCount() const { return brep_.m_F.Count(); }

}  // namespace dino8::kernel
