#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

int Mesh::VertexCount() const { return mesh_.VertexCount(); }

int Mesh::FaceCount() const { return mesh_.FaceCount(); }

}  // namespace dino8::kernel
