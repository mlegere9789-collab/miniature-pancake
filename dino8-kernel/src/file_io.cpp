#include "dino8/kernel/file_io.h"

#include <cmath>

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
// an empty name, a layer_index of -1, std::nullopt, and an empty list are
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

// Maps dino8::kernel::UnitSystem to its ON::LengthUnitSystem counterpart,
// for Model::SetUnitSystem() below.
ON::LengthUnitSystem ToLengthUnitSystem(UnitSystem units) {
  switch (units) {
    case UnitSystem::Millimeters: return ON::LengthUnitSystem::Millimeters;
    case UnitSystem::Centimeters: return ON::LengthUnitSystem::Centimeters;
    case UnitSystem::Meters: return ON::LengthUnitSystem::Meters;
    case UnitSystem::Kilometers: return ON::LengthUnitSystem::Kilometers;
    case UnitSystem::Microns: return ON::LengthUnitSystem::Microns;
    case UnitSystem::Inches: return ON::LengthUnitSystem::Inches;
    case UnitSystem::Feet: return ON::LengthUnitSystem::Feet;
    case UnitSystem::Yards: return ON::LengthUnitSystem::Yards;
    case UnitSystem::Miles: return ON::LengthUnitSystem::Miles;
    case UnitSystem::None: return ON::LengthUnitSystem::None;
  }
  return ON::LengthUnitSystem::Millimeters;
}

// The read-side counterpart to ToLengthUnitSystem() above, for
// Model::GetUnitSystem() below. Any ON::LengthUnitSystem this kernel's own
// UnitSystem enum has no matching entry for (Angstroms, Nanometers,
// Decimeters, Dekameters, Hectometers, Megameters, Gigameters,
// Microinches, Mils, PrinterPoints, PrinterPicas, NauticalMiles,
// AstronomicalUnits, LightYears, Parsecs, CustomUnits, Unset - a wider set
// than any caller of SetUnitSystem() above could ever have written through
// this API, reachable only from a .3dm this kernel didn't itself save with
// that unit system) falls back to Millimeters, the same default
// ON_3dmUnitsAndTolerances itself documents.
UnitSystem FromLengthUnitSystem(ON::LengthUnitSystem units) {
  switch (units) {
    case ON::LengthUnitSystem::Millimeters: return UnitSystem::Millimeters;
    case ON::LengthUnitSystem::Centimeters: return UnitSystem::Centimeters;
    case ON::LengthUnitSystem::Meters: return UnitSystem::Meters;
    case ON::LengthUnitSystem::Kilometers: return UnitSystem::Kilometers;
    case ON::LengthUnitSystem::Microns: return UnitSystem::Microns;
    case ON::LengthUnitSystem::Inches: return UnitSystem::Inches;
    case ON::LengthUnitSystem::Feet: return UnitSystem::Feet;
    case ON::LengthUnitSystem::Yards: return UnitSystem::Yards;
    case ON::LengthUnitSystem::Miles: return UnitSystem::Miles;
    case ON::LengthUnitSystem::None: return UnitSystem::None;
    default: return UnitSystem::Millimeters;
  }
}

// Maps dino8::kernel::LightStyle to its ON::light_style counterpart, for
// Model::AddLight() below. Both directions use the "world" (not "camera")
// variant - this kernel's Point3d/Vector3d location/direction parameters
// are always given in world coordinates, matching every other geometric
// parameter elsewhere in this API (AddNamedView()'s own camera_location,
// AddCurve()/AddBrep()'s own untransformed geometry, ...), never a
// viewport-relative camera space.
ON::light_style ToLightStyle(LightStyle style) {
  switch (style) {
    case LightStyle::Point: return ON::world_point_light;
    case LightStyle::Directional: return ON::world_directional_light;
  }
  return ON::world_point_light;
}

// The read-side counterpart to ToLightStyle() above, for Model::LightAt()
// below. A light this kernel didn't itself create via AddLight() - loaded
// from a .3dm some other application wrote - can carry any of
// ON::light_style's wider set (camera-space, spot, ambient, linear,
// rectangular); those all fall back to LightStyle::Point, the same
// "narrower kernel enum, unrecognized value falls back to a sane default"
// contract FromLengthUnitSystem() above already uses for UnitSystem.
LightStyle FromLightStyle(ON::light_style style) {
  switch (style) {
    case ON::world_point_light: return LightStyle::Point;
    case ON::world_directional_light: return LightStyle::Directional;
    default: return LightStyle::Point;
  }
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

int Model::AddMaterial(const std::string& name, Color diffuse_color,
                        std::optional<Color> specular_color, std::optional<Color> emission_color,
                        std::optional<double> shine, std::optional<double> transparency,
                        std::optional<double> reflectivity,
                        std::optional<std::string> texture_filename) {
  if (name.empty()) {
    return -1;
  }
  ON_Material material;
  material.SetName(ON_wString(name.c_str()));
  material.SetDiffuse(ON_Color(diffuse_color.r, diffuse_color.g, diffuse_color.b));
  if (specular_color.has_value()) {
    material.SetSpecular(ON_Color(specular_color->r, specular_color->g, specular_color->b));
  }
  if (emission_color.has_value()) {
    material.SetEmission(ON_Color(emission_color->r, emission_color->g, emission_color->b));
  }
  if (shine.has_value()) {
    material.SetShine(*shine);
  }
  if (transparency.has_value()) {
    material.SetTransparency(*transparency);
  }
  if (reflectivity.has_value()) {
    material.SetReflectivity(*reflectivity);
  }
  if (texture_filename.has_value() && !texture_filename->empty()) {
    material.AddTexture(ON_wString(texture_filename->c_str()), ON_Texture::TYPE::bitmap_texture);
  }
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
  // Deliberately NOT ActiveComponentCount(ModelGeometry) (what this used to
  // be): that counts every ModelGeometry component regardless of attribute
  // mode, including a block's own member geometry added via
  // AddInstanceDefinition() below with ON::idef_object mode specifically so
  // it would NOT read as an ordinary scene object - ON::idef_object is an
  // attribute flag, not a different ON_ModelComponent::Type, so it still
  // lands in ONX_Model's own ModelGeometry table either way. Excluding it
  // here changes nothing for any object this class already knew how to add
  // (AddCurve()/AddMesh()/etc. above never set that mode), so this is a
  // pure narrowing for existing callers.
  int count = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const ON_3dmObjectAttributes* attributes = geometry_component->Attributes(nullptr);
    if (attributes != nullptr && attributes->Mode() == ON::idef_object) {
      continue;
    }
    ++count;
  }
  return count;
}

ObjectAttributes Model::ObjectAttributesAt(int index) const {
  ObjectAttributes result;
  if (index < 0) {
    return result;
  }
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  int position = 0;
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const ON_3dmObjectAttributes* attributes = geometry_component->Attributes(nullptr);
    if (attributes == nullptr) {
      continue;
    }
    // See ObjectCount() above for why a block's own member geometry is
    // skipped here too - otherwise its presence would shift every later
    // object's index, and it would eventually be returned by some index
    // even though ObjectCount() itself doesn't count it.
    if (attributes->Mode() == ON::idef_object) {
      continue;
    }
    if (position != index) {
      ++position;
      continue;
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
    if (attributes->MaterialSource() == ON::material_from_object) {
      result.material_index = attributes->m_material_index;
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

int Model::MaterialCount() const {
  return static_cast<int>(model_.ActiveComponentCount(ON_ModelComponent::Type::RenderMaterial));
}

MaterialInfo Model::MaterialAt(int material_index) const {
  MaterialInfo result;
  const ON_ModelComponentReference material_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::RenderMaterial, material_index);
  const ON_Material* material = ON_Material::Cast(material_ref.ModelComponent());
  if (material == nullptr) {
    return result;
  }
  result.name = ToStdString(material->Name());
  const ON_Color diffuse = material->Diffuse();
  result.diffuse_color = Color{static_cast<unsigned char>(diffuse.Red()),
                                static_cast<unsigned char>(diffuse.Green()),
                                static_cast<unsigned char>(diffuse.Blue())};
  const ON_Color specular = material->Specular();
  result.specular_color = Color{static_cast<unsigned char>(specular.Red()),
                                 static_cast<unsigned char>(specular.Green()),
                                 static_cast<unsigned char>(specular.Blue())};
  const ON_Color emission = material->Emission();
  result.emission_color = Color{static_cast<unsigned char>(emission.Red()),
                                 static_cast<unsigned char>(emission.Green()),
                                 static_cast<unsigned char>(emission.Blue())};
  result.shine = material->Shine();
  result.transparency = material->Transparency();
  result.reflectivity = material->Reflectivity();
  const int texture_index = material->FindTexture(nullptr, ON_Texture::TYPE::bitmap_texture);
  if (texture_index >= 0) {
    result.texture_filename = ToStdString(material->m_textures[texture_index].m_image_file_reference.FullPath());
  }
  return result;
}

int Model::AddNamedView(const std::string& name, Point3d camera_location, Point3d target_point,
                         Vector3d camera_up) {
  if (name.empty()) {
    return -1;
  }
  ON_3dmView view;
  view.m_name = ON_wString(name.c_str());
  view.m_vp.SetCameraLocation(camera_location);
  Vector3d direction = target_point - camera_location;
  if (!direction.IsValid() || direction.IsZero()) {
    direction = Vector3d(0, 0, -1);
  } else {
    direction.Unitize();
  }
  view.m_vp.SetCameraDirection(direction);
  view.m_vp.SetCameraUp(camera_up);
  view.SetTargetPoint(target_point);
  model_.m_settings.m_named_views.Append(view);
  return model_.m_settings.m_named_views.Count() - 1;
}

int Model::NamedViewCount() const {
  return model_.m_settings.m_named_views.Count();
}

NamedViewInfo Model::NamedViewAt(int view_index) const {
  NamedViewInfo result;
  if (view_index < 0 || view_index >= model_.m_settings.m_named_views.Count()) {
    return result;
  }
  const ON_3dmView& view = model_.m_settings.m_named_views[view_index];
  result.name = ToStdString(view.m_name);
  result.camera_location = view.m_vp.CameraLocation();
  result.target_point = view.TargetPoint();
  result.camera_up = view.m_vp.CameraUp();
  return result;
}

int Model::AddLayout(const std::string& name, double page_width_mm, double page_height_mm) {
  if (name.empty() || !(page_width_mm > 0.0) || !(page_height_mm > 0.0)) {
    return -1;
  }
  ON_3dmView view;
  view.m_name = ON_wString(name.c_str());
  view.m_view_type = ON::view_type::page_view_type;
  view.m_page_settings.m_width_mm = page_width_mm;
  view.m_page_settings.m_height_mm = page_height_mm;
  model_.m_settings.m_views.Append(view);
  return model_.m_settings.m_views.Count() - 1;
}

int Model::LayoutCount() const {
  return model_.m_settings.m_views.Count();
}

LayoutInfo Model::LayoutAt(int layout_index) const {
  LayoutInfo result;
  if (layout_index < 0 || layout_index >= model_.m_settings.m_views.Count()) {
    return result;
  }
  const ON_3dmView& view = model_.m_settings.m_views[layout_index];
  result.name = ToStdString(view.m_name);
  result.page_width_mm = view.m_page_settings.m_width_mm;
  result.page_height_mm = view.m_page_settings.m_height_mm;
  return result;
}

int Model::AddLight(const std::string& name, LightStyle style, Point3d location,
                     Vector3d direction, Color diffuse_color, double intensity, int layer_index,
                     std::optional<Color> render_color, const UserStrings& user_strings,
                     std::optional<int> linetype_index, const std::vector<int>& group_indices,
                     std::optional<int> material_index) {
  if (name.empty()) {
    return -1;
  }
  auto* light = new ON_Light();
  light->SetLightName(name.c_str());
  light->SetStyle(ToLightStyle(style));
  light->SetLocation(location);
  light->SetDirection(direction);
  light->SetDiffuse(ON_Color(diffuse_color.r, diffuse_color.g, diffuse_color.b));
  light->SetIntensity(intensity);
  const int index = LightCount();
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(light, &attributes);
  return index;
}

int Model::LightCount() const {
  return static_cast<int>(model_.ActiveComponentCount(ON_ModelComponent::Type::RenderLight));
}

LightInfo Model::LightAt(int light_index) const {
  LightInfo result;
  if (light_index < 0) {
    return result;
  }
  int position = 0;
  // ON_ModelGeometryComponent::Geometry()'s own doc comment: "If the
  // geometry is a light, then ComponentType() will return
  // ON_ModelComponent::Type::RenderLight" - a light gets its own
  // dedicated component type distinct from ModelGeometry (every other
  // geometry kind AddMesh()/AddBrep()/etc. above add falls under
  // ModelGeometry instead), confirmed the hard way when this method
  // first filtered on ModelGeometry and found nothing at all. So a light
  // added via AddLight() above is NOT one more entry in
  // ObjectCount()/ObjectAttributesAt() the way a mesh is - it lives in
  // this own table only, same as a layer or a material does.
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::RenderLight);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const ON_Light* light = ON_Light::Cast(geometry_component->Geometry(nullptr));
    if (light == nullptr) {
      continue;
    }
    if (position == light_index) {
      result.name = ToStdString(light->LightName());
      result.style = FromLightStyle(light->Style());
      result.location = light->Location();
      result.direction = light->Direction();
      const ON_Color diffuse = light->Diffuse();
      result.diffuse_color = Color{static_cast<unsigned char>(diffuse.Red()),
                                    static_cast<unsigned char>(diffuse.Green()),
                                    static_cast<unsigned char>(diffuse.Blue())};
      result.intensity = light->Intensity();
      return result;
    }
    ++position;
  }
  return result;
}

int Model::AddClippingPlane(const std::string& name, Point3d origin, Vector3d normal, bool enabled,
                             int layer_index, std::optional<Color> render_color,
                             const UserStrings& user_strings, std::optional<int> linetype_index,
                             const std::vector<int>& group_indices,
                             std::optional<int> material_index) {
  if (name.empty()) {
    return -1;
  }
  const ON_Plane plane(origin, normal);
  if (!plane.IsValid()) {
    return -1;
  }
  auto* surface = new ON_ClippingPlaneSurface(plane);
  surface->m_clipping_plane.m_bEnabled = enabled;
  const int index = ClippingPlaneCount();
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(surface, &attributes);
  return index;
}

int Model::ClippingPlaneCount() const {
  int count = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    if (ON_ClippingPlaneSurface::Cast(geometry_component->Geometry(nullptr)) != nullptr) {
      ++count;
    }
  }
  return count;
}

ClippingPlaneInfo Model::ClippingPlaneAt(int clipping_plane_index) const {
  ClippingPlaneInfo result;
  if (clipping_plane_index < 0) {
    return result;
  }
  int position = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const ON_ClippingPlaneSurface* surface =
        ON_ClippingPlaneSurface::Cast(geometry_component->Geometry(nullptr));
    if (surface == nullptr) {
      continue;
    }
    if (position == clipping_plane_index) {
      const ON_3dmObjectAttributes* attributes = geometry_component->Attributes(nullptr);
      if (attributes != nullptr) {
        result.name = ToStdString(attributes->Name());
      }
      result.origin = surface->m_plane.origin;
      result.normal = surface->m_plane.zaxis;
      result.enabled = surface->m_clipping_plane.m_bEnabled;
      return result;
    }
    ++position;
  }
  return result;
}

int Model::AddInstanceDefinition(const std::string& name, const std::vector<Mesh>& member_meshes) {
  if (name.empty() || member_meshes.empty()) {
    return -1;
  }
  ON_SimpleArray<ON_UUID> member_ids;
  for (const Mesh& mesh : member_meshes) {
    auto* geometry = new ON_Mesh(mesh.raw());
    ON_3dmObjectAttributes attributes;
    ON_CreateUuid(attributes.m_uuid);
    // ON::idef_object: "this object is part of an ON_InstanceDefinition" -
    // see AddInstanceDefinition()'s own doc comment in file_io.h for why
    // this is what keeps a block's member geometry out of ObjectCount().
    attributes.SetMode(ON::idef_object);
    model_.AddModelGeometryComponent(geometry, &attributes);
    member_ids.Append(attributes.m_uuid);
  }
  ON_InstanceDefinition idef;
  idef.SetName(ON_wString(name.c_str()));
  idef.SetInstanceGeometryIdList(member_ids);
  const ON_ModelComponentReference idef_ref = model_.AddModelComponent(idef, true);
  const ON_InstanceDefinition* managed_idef = ON_InstanceDefinition::FromModelComponentRef(idef_ref, nullptr);
  return managed_idef != nullptr ? managed_idef->Index() : -1;
}

int Model::InstanceDefinitionCount() const {
  return static_cast<int>(model_.ActiveComponentCount(ON_ModelComponent::Type::InstanceDefinition));
}

std::string Model::InstanceDefinitionNameAt(int definition_index) const {
  const ON_ModelComponentReference idef_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::InstanceDefinition, definition_index);
  const ON_InstanceDefinition* idef = ON_InstanceDefinition::Cast(idef_ref.ModelComponent());
  return idef != nullptr ? ToStdString(idef->Name()) : std::string();
}

int Model::InstanceDefinitionMemberMeshCount(int definition_index) const {
  const ON_ModelComponentReference idef_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::InstanceDefinition, definition_index);
  const ON_InstanceDefinition* idef = ON_InstanceDefinition::Cast(idef_ref.ModelComponent());
  return idef != nullptr ? idef->InstanceGeometryIdList().Count() : 0;
}

Mesh Model::InstanceDefinitionMemberMeshAt(int definition_index, int member_index) const {
  Mesh result;
  const ON_ModelComponentReference idef_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::InstanceDefinition, definition_index);
  const ON_InstanceDefinition* idef = ON_InstanceDefinition::Cast(idef_ref.ModelComponent());
  if (idef == nullptr) {
    return result;
  }
  const ON_SimpleArray<ON_UUID>& member_ids = idef->InstanceGeometryIdList();
  if (member_index < 0 || member_index >= member_ids.Count()) {
    return result;
  }
  const ON_ModelGeometryComponent& geometry_component =
      model_.ModelGeometryComponentFromId(member_ids[member_index]);
  const ON_Mesh* mesh = ON_Mesh::Cast(geometry_component.Geometry(nullptr));
  if (mesh != nullptr) {
    result.raw() = *mesh;
  }
  return result;
}

int Model::AddInstanceReference(int definition_index, const ON_Xform& xform, const std::string& name,
                                 int layer_index, std::optional<Color> render_color,
                                 const UserStrings& user_strings, std::optional<int> linetype_index,
                                 const std::vector<int>& group_indices, std::optional<int> material_index) {
  const ON_ModelComponentReference idef_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::InstanceDefinition, definition_index);
  const ON_InstanceDefinition* idef = ON_InstanceDefinition::Cast(idef_ref.ModelComponent());
  if (idef == nullptr) {
    return -1;
  }
  auto* instance_ref = new ON_InstanceRef();
  instance_ref->m_instance_definition_uuid = idef->Id();
  instance_ref->m_xform = xform;
  const int index = InstanceReferenceCount();
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(instance_ref, &attributes);
  return index;
}

int Model::InstanceReferenceCount() const {
  int count = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    if (ON_InstanceRef::Cast(geometry_component->Geometry(nullptr)) != nullptr) {
      ++count;
    }
  }
  return count;
}

InstanceReferenceInfo Model::InstanceReferenceAt(int index) const {
  InstanceReferenceInfo result;
  if (index < 0) {
    return result;
  }
  int position = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const ON_InstanceRef* instance_ref = ON_InstanceRef::Cast(geometry_component->Geometry(nullptr));
    if (instance_ref == nullptr) {
      continue;
    }
    if (position == index) {
      const ON_3dmObjectAttributes* attributes = geometry_component->Attributes(nullptr);
      if (attributes != nullptr) {
        result.name = ToStdString(attributes->Name());
      }
      const ON_ModelComponentReference idef_ref = model_.ComponentFromId(
          ON_ModelComponent::Type::InstanceDefinition, instance_ref->m_instance_definition_uuid);
      const ON_InstanceDefinition* idef = ON_InstanceDefinition::Cast(idef_ref.ModelComponent());
      result.definition_index = idef != nullptr ? idef->Index() : -1;
      result.transform = instance_ref->m_xform;
      return result;
    }
    ++position;
  }
  return result;
}

int Model::AddHatchPattern(const std::string& name, HatchFillType fill_type,
                            const std::vector<HatchPatternLine>& lines) {
  if (name.empty()) {
    return -1;
  }
  ON_HatchPattern pattern;
  pattern.SetName(ON_wString(name.c_str()));
  pattern.SetFillType(fill_type == HatchFillType::Lines ? ON_HatchPattern::HatchFillType::Lines
                                                         : ON_HatchPattern::HatchFillType::Solid);
  for (const HatchPatternLine& line : lines) {
    ON_SimpleArray<double> dash_array;
    for (double dash : line.dashes) {
      dash_array.Append(dash);
    }
    const ON_HatchLine hatch_line(line.angle_radians, ON_2dPoint(line.base.x, line.base.y),
                                   ON_2dVector(line.offset.x, line.offset.y), dash_array);
    pattern.AddHatchLine(hatch_line);
  }
  const ON_ModelComponentReference pattern_ref = model_.AddModelComponent(pattern, true);
  const ON_HatchPattern* managed_pattern = ON_HatchPattern::FromModelComponentRef(pattern_ref, nullptr);
  return managed_pattern != nullptr ? managed_pattern->Index() : -1;
}

int Model::HatchPatternCount() const {
  return static_cast<int>(model_.ActiveComponentCount(ON_ModelComponent::Type::HatchPattern));
}

int Model::HatchPatternLineCount(int pattern_index) const {
  if (pattern_index < 0) {
    return 0;
  }
  const ON_ModelComponentReference pattern_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::HatchPattern, pattern_index);
  const ON_HatchPattern* pattern = ON_HatchPattern::Cast(pattern_ref.ModelComponent());
  return pattern != nullptr ? pattern->HatchLineCount() : 0;
}

HatchPatternLine Model::HatchPatternLineAt(int pattern_index, int line_index) const {
  HatchPatternLine result;
  if (pattern_index < 0 || line_index < 0) {
    return result;
  }
  const ON_ModelComponentReference pattern_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::HatchPattern, pattern_index);
  const ON_HatchPattern* pattern = ON_HatchPattern::Cast(pattern_ref.ModelComponent());
  if (pattern == nullptr) {
    return result;
  }
  const ON_HatchLine* line = pattern->HatchLine(line_index);
  if (line == nullptr) {
    return result;
  }
  result.angle_radians = line->AngleRadians();
  const ON_2dPoint base = line->Base();
  result.base = Point2d(base.x, base.y);
  const ON_2dVector offset = line->Offset();
  result.offset = Point2d(offset.x, offset.y);
  for (int i = 0; i < line->DashCount(); ++i) {
    result.dashes.push_back(line->Dash(i));
  }
  return result;
}

int Model::AddHatch(const ON_Plane& plane, const std::vector<Point2d>& boundary, int pattern_index,
                     double pattern_rotation, double pattern_scale, const std::string& name,
                     int layer_index, std::optional<Color> render_color, const UserStrings& user_strings,
                     std::optional<int> linetype_index, const std::vector<int>& group_indices,
                     std::optional<int> material_index) {
  if (name.empty() || boundary.size() < 3 || pattern_index < 0) {
    return -1;
  }
  const ON_ModelComponentReference pattern_ref =
      model_.ComponentFromIndex(ON_ModelComponent::Type::HatchPattern, pattern_index);
  if (ON_HatchPattern::Cast(pattern_ref.ModelComponent()) == nullptr) {
    return -1;
  }
  // ON_HatchLoop's own doc comment: "the 2d loop curve in the hatch's plane
  // coordinates ... really a 3d curve with z coordinates = 0" - so `boundary`
  // becomes a closed 3D polyline with each (u, v) point's z forced to 0,
  // not a curve in world coordinates.
  ON_3dPointArray loop_points;
  for (const Point2d& uv : boundary) {
    loop_points.Append(ON_3dPoint(uv.x, uv.y, 0.0));
  }
  loop_points.Append(loop_points[0]);  // ON_PolylineCurve requires an explicitly closed point list
  ON_PolylineCurve loop_curve(loop_points);
  ON_SimpleArray<const ON_Curve*> loops;
  loops.Append(static_cast<const ON_Curve*>(&loop_curve));
  auto* hatch = new ON_Hatch();
  if (!hatch->Create(plane, loops, pattern_index, pattern_rotation, pattern_scale)) {
    delete hatch;
    return -1;
  }
  const int index = HatchCount();
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(hatch, &attributes);
  return index;
}

int Model::HatchCount() const {
  int count = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    if (ON_Hatch::Cast(geometry_component->Geometry(nullptr)) != nullptr) {
      ++count;
    }
  }
  return count;
}

HatchInfo Model::HatchAt(int hatch_index) const {
  HatchInfo result;
  if (hatch_index < 0) {
    return result;
  }
  int position = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const ON_Hatch* hatch = ON_Hatch::Cast(geometry_component->Geometry(nullptr));
    if (hatch == nullptr) {
      continue;
    }
    if (position == hatch_index) {
      const ON_3dmObjectAttributes* attributes = geometry_component->Attributes(nullptr);
      if (attributes != nullptr) {
        result.name = ToStdString(attributes->Name());
      }
      result.plane = hatch->Plane();
      if (hatch->LoopCount() > 0) {
        const ON_HatchLoop* loop = hatch->Loop(0);
        const ON_Curve* loop_curve = loop != nullptr ? loop->Curve() : nullptr;
        if (loop_curve != nullptr) {
          // The loop curve is a closed polyline whose last point duplicates
          // its first (see AddHatch() above, which appends that duplicate
          // to close ON_PolylineCurve's own point list) - dropped here so
          // `boundary` round-trips exactly what AddHatch() was given.
          ON_3dPointArray points;
          if (loop_curve->IsPolyline(&points) >= 2) {
            const int usable = points.Count() - 1;
            for (int i = 0; i < usable; ++i) {
              result.boundary.push_back(Point2d(points[i].x, points[i].y));
            }
          }
        }
      }
      result.pattern_index = hatch->PatternIndex();
      result.pattern_rotation = hatch->PatternRotation();
      result.pattern_scale = hatch->PatternScale();
      return result;
    }
    ++position;
  }
  return result;
}

int Model::AddTextDot(Point3d center, const std::string& primary_text, const std::string& secondary_text,
                       const std::string& name, int layer_index, std::optional<Color> render_color,
                       const UserStrings& user_strings, std::optional<int> linetype_index,
                       const std::vector<int>& group_indices, std::optional<int> material_index) {
  if (name.empty()) {
    return -1;
  }
  auto* dot = new ON_TextDot(center, ON_wString(primary_text.c_str()), ON_wString(secondary_text.c_str()));
  const int index = TextDotCount();
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(dot, &attributes);
  return index;
}

int Model::TextDotCount() const {
  int count = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    if (ON_TextDot::Cast(geometry_component->Geometry(nullptr)) != nullptr) {
      ++count;
    }
  }
  return count;
}

TextDotInfo Model::TextDotAt(int text_dot_index) const {
  TextDotInfo result;
  if (text_dot_index < 0) {
    return result;
  }
  int position = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const ON_TextDot* dot = ON_TextDot::Cast(geometry_component->Geometry(nullptr));
    if (dot == nullptr) {
      continue;
    }
    if (position == text_dot_index) {
      const ON_3dmObjectAttributes* attributes = geometry_component->Attributes(nullptr);
      if (attributes != nullptr) {
        result.name = ToStdString(attributes->Name());
      }
      result.center = dot->CenterPoint();
      result.primary_text = ToStdString(ON_wString(dot->PrimaryText()));
      result.secondary_text = ToStdString(ON_wString(dot->SecondaryText()));
      return result;
    }
    ++position;
  }
  return result;
}

int Model::AddText(const std::string& text, const ON_Plane& plane, const std::string& name, int layer_index,
                    std::optional<Color> render_color, const UserStrings& user_strings,
                    std::optional<int> linetype_index, const std::vector<int>& group_indices,
                    std::optional<int> material_index) {
  if (name.empty() || text.empty() || !plane.IsValid()) {
    return -1;
  }
  auto* annotation = new ON_Text();
  if (!annotation->Create(ON_wString(text.c_str()), &ON_DimStyle::Default, plane)) {
    delete annotation;
    return -1;
  }
  const int index = TextCount();
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(annotation, &attributes);
  return index;
}

int Model::TextCount() const {
  int count = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    if (ON_Text::Cast(geometry_component->Geometry(nullptr)) != nullptr) {
      ++count;
    }
  }
  return count;
}

TextAnnotationInfo Model::TextAt(int text_index) const {
  TextAnnotationInfo result;
  if (text_index < 0) {
    return result;
  }
  int position = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const ON_Text* annotation = ON_Text::Cast(geometry_component->Geometry(nullptr));
    if (annotation == nullptr) {
      continue;
    }
    if (position == text_index) {
      const ON_3dmObjectAttributes* attributes = geometry_component->Attributes(nullptr);
      if (attributes != nullptr) {
        result.name = ToStdString(attributes->Name());
      }
      result.text = ToStdString(annotation->PlainText());
      result.plane = annotation->Plane();
      return result;
    }
    ++position;
  }
  return result;
}

int Model::AddLeader(const std::string& text, const std::vector<Point3d>& points, const ON_Plane& plane,
                      const std::string& name, int layer_index, std::optional<Color> render_color,
                      const UserStrings& user_strings, std::optional<int> linetype_index,
                      const std::vector<int>& group_indices, std::optional<int> material_index) {
  if (name.empty() || text.empty() || !plane.IsValid() || points.size() < 2) {
    return -1;
  }
  auto* annotation = new ON_Leader();
  if (!annotation->Create(ON_wString(text.c_str()), &ON_DimStyle::Default,
                           static_cast<int>(points.size()), points.data(), plane,
                           /*bWrapped=*/false, /*rect_width=*/0.0)) {
    delete annotation;
    return -1;
  }
  const int index = LeaderCount();
  ON_3dmObjectAttributes attributes = MakeAttributes(
      name, layer_index, render_color, user_strings, linetype_index, group_indices, material_index);
  model_.AddModelGeometryComponent(annotation, &attributes);
  return index;
}

int Model::LeaderCount() const {
  int count = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    if (ON_Leader::Cast(geometry_component->Geometry(nullptr)) != nullptr) {
      ++count;
    }
  }
  return count;
}

LeaderInfo Model::LeaderAt(int leader_index) const {
  LeaderInfo result;
  if (leader_index < 0) {
    return result;
  }
  int position = 0;
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const ON_Leader* annotation = ON_Leader::Cast(geometry_component->Geometry(nullptr));
    if (annotation == nullptr) {
      continue;
    }
    if (position == leader_index) {
      const ON_3dmObjectAttributes* attributes = geometry_component->Attributes(nullptr);
      if (attributes != nullptr) {
        result.name = ToStdString(attributes->Name());
      }
      result.text = ToStdString(annotation->PlainText());
      result.plane = annotation->Plane();
      const ON_2dPointArray& points2d = annotation->Points2d();
      result.points.reserve(static_cast<size_t>(points2d.Count()));
      for (int i = 0; i < points2d.Count(); ++i) {
        result.points.push_back(result.plane.PointAt(points2d[i].x, points2d[i].y));
      }
      return result;
    }
    ++position;
  }
  return result;
}

void Model::SetUnitSystem(UnitSystem units) {
  model_.m_settings.m_ModelUnitsAndTolerances.m_unit_system = ON_UnitSystem(ToLengthUnitSystem(units));
}

UnitSystem Model::GetUnitSystem() const {
  return FromLengthUnitSystem(model_.m_settings.m_ModelUnitsAndTolerances.m_unit_system.UnitSystem());
}

double Model::UnitConversionFactor(UnitSystem from, UnitSystem to) {
  return ON::UnitScale(ToLengthUnitSystem(from), ToLengthUnitSystem(to));
}

Result Model::ConvertUnits(UnitSystem to) {
  if (to == UnitSystem::None || GetUnitSystem() == UnitSystem::None) {
    return Result::Failed;
  }
  const double factor = UnitConversionFactor(GetUnitSystem(), to);
  if (!std::isfinite(factor)) {
    return Result::Failed;
  }
  const ON_Xform scale = ON_Xform::DiagonalTransformation(factor, factor, factor);
  ONX_ModelComponentIterator iterator(model_, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    ON_Geometry* geometry = geometry_component->ExclusiveGeometry();
    if (geometry == nullptr || !geometry->Transform(scale)) {
      return Result::Failed;
    }
  }
  SetUnitSystem(to);
  return Result::Ok;
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
