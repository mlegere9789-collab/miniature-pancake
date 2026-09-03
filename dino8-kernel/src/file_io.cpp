#include "dino8/kernel/file_io.h"

namespace dino8::kernel {

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
  return ok ? Result::Ok : Result::Failed;
}

}  // namespace dino8::kernel
