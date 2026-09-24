#include "dino8/kernel/file_io.h"

namespace dino8::kernel {

namespace {

// ONX_Model::Read() (ON_Mesh::Read()/ReadFaceArray() under it) copies each
// mesh face's four vertex indices straight off the disk with no check
// against the vertex count it read a moment earlier, and never calls
// ON_Mesh::IsValid() afterward - so a corrupt (or hostile) .3dm can hand
// this kernel a Mesh whose faces index past m_V. Every Mesh query here
// (Area(), Volume(), MergeAndWeld(), the boolean ToManifold() bridge, ...)
// indexes m_V by those face indices unchecked, exactly as ON_Mesh itself
// does, so the outcome was a silent out-of-bounds read, not a failure
// (confirmed by a debug run: a 3-vertex mesh with a face at index 70000 -
// written by this class's own Save(), which OpenNURBS doesn't validate
// either, and even silently truncated to 112 by its 1-byte on-disk index
// encoding - loaded with Result::Ok, ON_Mesh::IsValid() false, and
// Area() 0.5 from whatever memory sat past the vertex array).
// LoadObj()/LoadStl() already reject an out-of-range face index at the
// boundary; this makes the .3dm loader hold the same line. Deliberately
// only the range check, not ON_MeshFace::IsValid()'s stricter "no
// repeated indices" rule: a degenerate (repeated-index) face is an
// in-range face other exporters legitimately write, and indexing it is
// perfectly safe.
bool MeshFaceIndicesInRange(const ON_Mesh& mesh) {
  const int vertex_count = mesh.m_V.Count();
  for (int i = 0; i < mesh.m_F.Count(); ++i) {
    const ON_MeshFace& face = mesh.m_F[i];
    for (int k = 0; k < 4; ++k) {
      if (face.vi[k] < 0 || face.vi[k] >= vertex_count) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

Model::Model() = default;

void Model::AddCurve(const NurbsCurve& curve) {
  auto* geometry = new ON_NurbsCurve(curve.raw());
  ON_3dmObjectAttributes attributes;
  ON_CreateUuid(attributes.m_uuid);
  model_.AddModelGeometryComponent(geometry, &attributes);
}

void Model::AddBrep(const Brep& brep) {
  auto* geometry = new ON_Brep(brep.raw());
  ON_3dmObjectAttributes attributes;
  ON_CreateUuid(attributes.m_uuid);
  model_.AddModelGeometryComponent(geometry, &attributes);
}

void Model::AddMesh(const Mesh& mesh) {
  auto* geometry = new ON_Mesh(mesh.raw());
  ON_3dmObjectAttributes attributes;
  ON_CreateUuid(attributes.m_uuid);
  model_.AddModelGeometryComponent(geometry, &attributes);
}

void Model::AddSubD(const SubD& subd) {
  auto* geometry = new ON_SubD(subd.raw());
  ON_3dmObjectAttributes attributes;
  ON_CreateUuid(attributes.m_uuid);
  model_.AddModelGeometryComponent(geometry, &attributes);
}

int Model::ObjectCount() const {
  return static_cast<int>(
      model_.ActiveComponentCount(ON_ModelComponent::Type::ModelGeometry));
}

Result Model::Save(const std::string& path, int version) const {
  ON_TextLog error_log;
  const bool ok = model_.Write(path.c_str(), version, &error_log);
  return ok ? Result::Ok : Result::Failed;
}

Result Model::Load(const std::string& path, Model& out_model) {
  ON_TextLog error_log;
  const bool ok = out_model.model_.Read(path.c_str(), &error_log);
  if (!ok) {
    return Result::Failed;
  }
  // See MeshFaceIndicesInRange() above for why this check exists at all.
  ONX_ModelComponentIterator it(out_model.model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = it.FirstComponent(); component != nullptr;
       component = it.NextComponent()) {
    const ON_ModelGeometryComponent* geometry = ON_ModelGeometryComponent::Cast(component);
    if (geometry == nullptr) {
      continue;
    }
    const ON_Mesh* mesh = ON_Mesh::Cast(geometry->Geometry(nullptr));
    if (mesh != nullptr && !MeshFaceIndicesInRange(*mesh)) {
      out_model.model_.Reset();  // never hand back a half-trusted model
      return Result::Failed;
    }
  }
  return Result::Ok;
}

}  // namespace dino8::kernel
