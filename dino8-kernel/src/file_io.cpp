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

// Shared by every Add*() below: a fresh UUID, plus `name` set via
// SetName() when non-empty, `layer_index` written straight through,
// `render_color` (when present) written to `m_color` with `ColorSource()`
// switched to ON::color_from_object, every `user_strings` pair written
// via SetUserString(), `linetype_index` (when present) written to
// `m_linetype_index` with `LinetypeSource()` switched to
// ON::linetype_from_object, every `group_indices` entry added via
// AddToGroup(), and `material_index` (when present) written to
// `m_material_index` with `MaterialSource()` switched to
// ON::material_from_object. See file_io.h's own doc comment on the
// `name`/`layer_index`/`render_color`/`user_strings`/`linetype_index`/
// `group_indices`/`material_index` parameters for why this exists and why
// an empty name, a layer_index of 0, std::nullopt, and an empty list are
// all no-ops.
ON_3dmObjectAttributes MakeAttributes(const std::string& name, int layer_index,
                                       std::optional<Color> render_color,
                                       const UserStrings& user_strings,
                                       std::optional<int> linetype_index,
                                       const std::vector<int>& group_indices,
                                       std::optional<int> material_index) {
  ON_3dmObjectAttributes attributes;
  ON_CreateUuid(attributes.m_uuid);
  if (!name.empty()) {
    attributes.SetName(ON_wString(name.c_str()), true);
  }
  attributes.m_layer_index = layer_index;
  if (render_color.has_value()) {
    attributes.m_color = ON_Color(render_color->r, render_color->g, render_color->b);
    attributes.SetColorSource(ON::color_from_object);
  }
  for (const auto& [key, value] : user_strings) {
    attributes.SetUserString(ON_wString(key.c_str()), ON_wString(value.c_str()));
  }
  if (linetype_index.has_value()) {
    attributes.m_linetype_index = *linetype_index;
    attributes.SetLinetypeSource(ON::linetype_from_object);
  }
  for (int group_index : group_indices) {
    attributes.AddToGroup(group_index);
  }
  if (material_index.has_value()) {
    attributes.m_material_index = *material_index;
    attributes.SetMaterialSource(ON::material_from_object);
  }
  return attributes;
}

// Converts an OpenNURBS wide string to std::string, the same
// ON_String(w)-then-cast pattern dino8-app/src/io/File3dm.cpp's own
// FromWide() already uses for this exact conversion.
std::string ToStdString(const ON_wString& wide) {
  ON_String narrow(wide);
  return std::string(static_cast<const char*>(narrow));
}

}  // namespace

Model::Model() = default;

int Model::AddLayer(const std::string& name, Color color, int linetype_index) {
  if (name.empty()) {
    return -1;
  }
  ON_Layer layer;
  layer.SetName(ON_wString(name.c_str()));
  layer.SetColor(ON_Color(color.r, color.g, color.b));
  if (linetype_index >= 0) {
    layer.SetLinetypeIndex(linetype_index);
  }
  const ON_ModelComponentReference layer_ref = model_.AddModelComponent(layer, true);
  const ON_Layer* managed_layer = ON_Layer::FromModelComponentRef(layer_ref, nullptr);
  return managed_layer != nullptr ? managed_layer->Index() : -1;
}

int Model::AddLinetype(const std::string& name, const LinetypePattern& pattern) {
  if (name.empty()) {
    return -1;
  }
  ON_Linetype linetype;
  linetype.SetName(ON_wString(name.c_str()));
  for (const LinetypeSegment& segment : pattern) {
    linetype.AppendSegment(ON_LinetypeSegment(
        segment.length_mm, segment.is_dash ? ON_LinetypeSegment::eSegType::stLine
                                            : ON_LinetypeSegment::eSegType::stSpace));
  }
  const ON_ModelComponentReference linetype_ref = model_.AddModelComponent(linetype, true);
  const ON_Linetype* managed_linetype = ON_Linetype::FromModelComponentRef(linetype_ref, nullptr);
  return managed_linetype != nullptr ? managed_linetype->Index() : -1;
}

int Model::AddGroup(const std::string& name) {
  if (name.empty()) {
    return -1;
  }
  ON_Group group;
  group.SetName(ON_wString(name.c_str()));
  const ON_ModelComponentReference group_ref = model_.AddModelComponent(group, true);
  const ON_Group* managed_group = ON_Group::FromModelComponentRef(group_ref, nullptr);
  return managed_group != nullptr ? managed_group->Index() : -1;
}

int Model::AddMaterial(const std::string& name, Color diffuse_color) {
  if (name.empty()) {
    return -1;
  }
  ON_Material material;
  material.SetName(ON_wString(name.c_str()));
  material.SetDiffuse(ON_Color(diffuse_color.r, diffuse_color.g, diffuse_color.b));
  const ON_ModelComponentReference material_ref = model_.AddModelComponent(material, true);
  const ON_Material* managed_material = ON_Material::FromModelComponentRef(material_ref, nullptr);
  return managed_material != nullptr ? managed_material->Index() : -1;
}

void Model::AddCurve(const NurbsCurve& curve, const std::string& name, int layer_index,
                      std::optional<Color> render_color, const UserStrings& user_strings,
                      std::optional<int> linetype_index, const std::vector<int>& group_indices,
                      std::optional<int> material_index) {
  auto* geometry = new ON_NurbsCurve(curve.raw());
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(geometry, &attributes);
}

void Model::AddBrep(const Brep& brep, const std::string& name, int layer_index,
                     std::optional<Color> render_color, const UserStrings& user_strings,
                     std::optional<int> linetype_index, const std::vector<int>& group_indices,
                     std::optional<int> material_index) {
  auto* geometry = new ON_Brep(brep.raw());
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(geometry, &attributes);
}

void Model::AddMesh(const Mesh& mesh, const std::string& name, int layer_index,
                     std::optional<Color> render_color, const UserStrings& user_strings,
                     std::optional<int> linetype_index, const std::vector<int>& group_indices,
                     std::optional<int> material_index) {
  auto* geometry = new ON_Mesh(mesh.raw());
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(geometry, &attributes);
}

void Model::AddSubD(const SubD& subd, const std::string& name, int layer_index,
                     std::optional<Color> render_color, const UserStrings& user_strings,
                     std::optional<int> linetype_index, const std::vector<int>& group_indices,
                     std::optional<int> material_index) {
  auto* geometry = new ON_SubD(subd.raw());
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(geometry, &attributes);
}

void Model::AddPointCloud(const PointCloud& cloud, const std::string& name, int layer_index,
                           std::optional<Color> render_color, const UserStrings& user_strings,
                           std::optional<int> linetype_index,
                           const std::vector<int>& group_indices,
                           std::optional<int> material_index) {
  auto* geometry = new ON_PointCloud(cloud.raw());
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(geometry, &attributes);
}

int Model::ObjectCount() const {
  return static_cast<int>(
      model_.ActiveComponentCount(ON_ModelComponent::Type::ModelGeometry));
}

ObjectAttributes Model::ObjectAttributesAt(int index) const {
  ObjectAttributes result;
  if (index < 0) {
    return result;
  }
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  int position = 0;
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent(), ++position) {
    if (position != index) {
      continue;
    }
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const ON_3dmObjectAttributes* attributes = geometry_component->Attributes(nullptr);
    if (attributes == nullptr) {
      return result;
    }
    result.name = ToStdString(attributes->Name());
    result.layer_index = attributes->m_layer_index;
    if (attributes->ColorSource() == ON::color_from_object) {
      result.render_color =
          Color{static_cast<unsigned char>(attributes->m_color.Red()),
                static_cast<unsigned char>(attributes->m_color.Green()),
                static_cast<unsigned char>(attributes->m_color.Blue())};
    }
    if (attributes->LinetypeSource() == ON::linetype_from_object) {
      result.linetype_index = attributes->m_linetype_index;
    }
    ON_SimpleArray<int> group_indices;
    attributes->GetGroupList(group_indices);
    result.group_indices.assign(group_indices.Array(), group_indices.Array() + group_indices.Count());
    ON_ClassArray<ON_UserString> user_strings;
    attributes->GetUserStrings(user_strings);
    for (int i = 0; i < user_strings.Count(); ++i) {
      result.user_strings.emplace_back(ToStdString(user_strings[i].m_key),
                                        ToStdString(user_strings[i].m_string_value));
    }
    return result;
  }
  return result;
}

int Model::LayerCount() const {
  return static_cast<int>(model_.ActiveComponentCount(ON_ModelComponent::Type::Layer));
}

LayerInfo Model::LayerAt(int layer_index) const {
  LayerInfo result;
  // Deliberately ComponentFromIndex(), not the ONX_Model::LayerFromIndex()
  // convenience wrapper AddLayer() and this file's own round-trip tests
  // use elsewhere: LayerFromIndex() silently falls back to
  // ONX_Model::m_default_layer for any index it doesn't recognize (see
  // its own implementation in opennurbs_extensions.cpp), so it can never
  // signal "no such layer" the way this method's own contract (a default-
  // constructed LayerInfo for an unrecognized index) requires.
  const ON_ModelComponentReference layer_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::Layer, layer_index);
  const ON_Layer* layer = ON_Layer::Cast(layer_ref.ModelComponent());
  if (layer == nullptr) {
    return result;
  }
  result.name = ToStdString(layer->Name());
  const ON_Color color = layer->Color();
  result.color = Color{static_cast<unsigned char>(color.Red()),
                        static_cast<unsigned char>(color.Green()),
                        static_cast<unsigned char>(color.Blue())};
  result.linetype_index = layer->LinetypeIndex();
  return result;
}

int Model::LinetypeCount() const {
  return static_cast<int>(model_.ActiveComponentCount(ON_ModelComponent::Type::LinePattern));
}

LinetypeInfo Model::LinetypeAt(int linetype_index) const {
  LinetypeInfo result;
  const ON_ModelComponentReference linetype_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::LinePattern, linetype_index);
  const ON_Linetype* linetype = ON_Linetype::Cast(linetype_ref.ModelComponent());
  if (linetype == nullptr) {
    return result;
  }
  result.name = ToStdString(linetype->Name());
  for (int i = 0; i < linetype->SegmentCount(); ++i) {
    const ON_LinetypeSegment& segment = linetype->Segment(i);
    result.pattern.push_back(
        LinetypeSegment{segment.m_length, segment.m_seg_type == ON_LinetypeSegment::eSegType::stLine});
  }
  return result;
}

int Model::GroupCount() const {
  return static_cast<int>(model_.ActiveComponentCount(ON_ModelComponent::Type::Group));
}

std::string Model::GroupNameAt(int group_index) const {
  const ON_ModelComponentReference group_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::Group, group_index);
  const ON_Group* group = ON_Group::Cast(group_ref.ModelComponent());
  return group != nullptr ? ToStdString(group->Name()) : std::string();
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
