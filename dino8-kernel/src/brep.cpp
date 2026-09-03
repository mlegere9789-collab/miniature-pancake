#include "dino8/kernel/brep.h"

#include "dino8/kernel/mesh.h"

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

std::vector<Mesh> Brep::Tessellate(int u_divisions, int v_divisions) const {
  std::vector<Mesh> result;
  result.reserve(static_cast<size_t>(brep_.m_F.Count()));

  for (int i = 0; i < brep_.m_F.Count(); ++i) {
    const ON_Surface* face_surface = brep_.m_F[i].SurfaceOf();
    const auto* nurbs_surface = ON_NurbsSurface::Cast(face_surface);
    if (nurbs_surface == nullptr) {
      // Every face this chunk's Brep::FromSurface constructs stores a
      // genuine ON_NurbsSurface, so this only trips if Brep grows a way
      // to hold other surface types without updating the tessellator.
      continue;
    }

    NurbsSurface wrapper;
    wrapper.raw() = *nurbs_surface;
    result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions));
  }

  return result;
}

}  // namespace dino8::kernel
