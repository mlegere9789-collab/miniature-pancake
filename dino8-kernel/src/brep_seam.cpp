#include "dino8/kernel/brep_seam.h"

#include <stdexcept>

#include "dino8/kernel/surface.h"

namespace dino8::kernel {

void ReplaceBrepSurface(ON_Brep& b, int si, ON_Surface* fresh, bool delete_old) {
  if (si < 0 || si >= b.m_S.Count()) throw std::invalid_argument("dino8::kernel::ReplaceBrepSurface: surface index out of range");
  ON_Surface* old = b.m_S[si];
  b.m_S[si] = fresh;
  for (int i = 0; i < b.m_F.Count(); ++i) {
    if (b.m_F[i].m_si == si) b.m_F[i].SetProxySurface(fresh);
  }
  if (delete_old && old != fresh) delete old;
  b.DestroyMesh(ON::any_mesh);
}

Result ChangeFaceSeam(Brep& brep, int face_index, int direction, double t, std::string* reason) {
  ON_Brep& b = brep.raw();
  if (face_index < 0 || face_index >= b.m_F.Count()) {
    throw std::invalid_argument("dino8::kernel::ChangeFaceSeam: face index out of range");
  }
  if (direction != 0 && direction != 1) {
    throw std::invalid_argument("dino8::kernel::ChangeFaceSeam: direction must be 0 (U) or 1 (V)");
  }
  auto refuse = [&](const std::string& why) {
    if (reason) *reason = why;
    return Result::Failed;
  };
  ON_BrepFace& face = b.m_F[face_index];
  if (face.m_li.Count() > 0) {
    return refuse("the face has trim loops; moving the seam would also have to re-seam its trim curves and edges, which is not attempted");
  }
  if (!brep.FaceCoversWholeDomain(face_index)) {
    return refuse("the face is trimmed by the kernel's pseudo-trim tables, whose parameters would have to move with the seam");
  }
  // (m_S[m_si] directly: ON_BrepFace::SurfaceOf() goes through the face's m_brep back-pointer, which is stale in a copied or moved Brep)
  const ON_Surface* srf = (face.m_si >= 0 && face.m_si < b.m_S.Count()) ? b.m_S[face.m_si] : nullptr;
  if (!srf) return refuse("the face has no surface");
  NurbsSurface moved;
  if (const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(srf)) {
    moved.raw() = *ns;
  } else if (srf->GetNurbForm(moved.raw()) == 0) {
    return refuse("the face's surface cannot be expressed as a NURBS surface");
  }
  const Result r = moved.ChangeSeam(direction, t);
  if (r != Result::Ok) {
    if (reason && r == Result::Failed) *reason = "the surface is not closed in that direction, or the parameter is outside its domain";
    return r;
  }
  int users = 0;
  for (int i = 0; i < b.m_F.Count(); ++i) {
    if (b.m_F[i].m_si == face.m_si) ++users;
  }
  ON_NurbsSurface* fresh = new ON_NurbsSurface(moved.raw());
  if (users <= 1) {
    ReplaceBrepSurface(b, face.m_si, fresh, true);
  } else {
    b.m_S.Append(fresh);
    face.m_si = b.m_S.Count() - 1;
    face.SetProxySurface(fresh);
  }
  b.DestroyMesh(ON::any_mesh);
  return Result::Ok;
}

}  // namespace dino8::kernel
