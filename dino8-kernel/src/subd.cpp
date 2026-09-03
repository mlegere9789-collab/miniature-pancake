#include "dino8/kernel/subd.h"

#include <stdexcept>

namespace dino8::kernel {

SubD SubD::FromControlMesh(const Mesh& control_mesh) {
  SubD result;
  const ON_SubD* built = ON_SubD::CreateFromMesh(&control_mesh.raw(),
                                                  &ON_SubDFromMeshParameters::Smooth, &result.subd_);
  if (built == nullptr) {
    throw std::runtime_error(
        "dino8::kernel::SubD::FromControlMesh: ON_SubD::CreateFromMesh failed "
        "(control_mesh may have no faces or invalid topology)");
  }
  return result;
}

void SubD::Subdivide(int levels) {
  if (levels <= 0) {
    return;
  }
  if (!subd_.GlobalSubdivide(static_cast<unsigned int>(levels))) {
    throw std::runtime_error(
        "dino8::kernel::SubD::Subdivide: ON_SubD::GlobalSubdivide failed "
        "(levels may exceed ON_SubD::maximum_subd_level, or the SubD is empty)");
  }
}

Mesh SubD::ToApproximateMesh() const {
  Mesh result;
  const ON_Mesh* out =
      subd_.GetControlNetMesh(&result.raw(), ON_SubDGetControlNetMeshPriority::Geometry);
  if (out == nullptr) {
    throw std::runtime_error(
        "dino8::kernel::SubD::ToApproximateMesh: ON_SubD::GetControlNetMesh failed");
  }
  return result;
}

int SubD::FaceCount() const { return static_cast<int>(subd_.FaceCount()); }
int SubD::VertexCount() const { return static_cast<int>(subd_.VertexCount()); }

}  // namespace dino8::kernel
