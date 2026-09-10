#include "dino8/kernel/subd.h"

#include <stdexcept>

namespace dino8::kernel {

SubD SubD::FromControlMesh(const Mesh& control_mesh, bool crease_at_double_edges) {
  SubD result;
  const ON_SubDFromMeshParameters& params = crease_at_double_edges
                                                 ? ON_SubDFromMeshParameters::InteriorCreases
                                                 : ON_SubDFromMeshParameters::Smooth;
  const ON_SubD* built = ON_SubD::CreateFromMesh(&control_mesh.raw(), &params, &result.subd_);
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
int SubD::EdgeCount() const { return static_cast<int>(subd_.EdgeCount()); }

int SubD::CreaseEdgeCount() const {
  int count = 0;
  ON_SubDEdgeIterator eit = subd_.EdgeIterator();
  for (const ON_SubDEdge* e = eit.FirstEdge(); e != nullptr; e = eit.NextEdge()) {
    if (e->IsCrease()) {
      ++count;
    }
  }
  return count;
}

}  // namespace dino8::kernel
