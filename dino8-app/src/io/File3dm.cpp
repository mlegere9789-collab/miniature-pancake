#include "io/File3dm.h"

#include "geom/BrepMesher.h"

#include <opennurbs.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

namespace dino8::app {

namespace {

std::string LowerExt(const std::string& path) {
  std::string e = std::filesystem::path(path).extension().string();
  for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

std::string FromWide(const ON_wString& w) {
  ON_String s(w);
  return std::string(static_cast<const char*>(s));
}

ON_Color ToOnColor(const Color& c) {
  return ON_Color(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255));
}

Color FromOnColor(const ON_Color& c) { return Color::FromBytes(c.Red(), c.Green(), c.Blue()); }

std::string ColorKey(const Color& c) {
  return std::to_string(static_cast<int>(c.r * 255 + 0.5f)) + "," + std::to_string(static_cast<int>(c.g * 255 + 0.5f)) + "," + std::to_string(static_cast<int>(c.b * 255 + 0.5f));
}

bool ColorFromKey(const std::string& t, Color& out) {
  int r, g, b;
  if (std::sscanf(t.c_str(), "%d,%d,%d", &r, &g, &b) != 3) return false;
  out = Color::FromBytes(r, g, b);
  return true;
}

// Render settings travel as document user strings (Dino8.Render.*).
void WriteRenderSettings(ONX_Model& model, const RenderSettings& r) {
  auto set = [&](const char* key, const std::string& v) { model.SetDocumentUserString(ON_wString(key), ON_wString(v.c_str())); };
  set("Dino8.Render.Background", std::to_string(static_cast<int>(r.background)));
  set("Dino8.Render.BackgroundColor", ColorKey(r.background_color));
  set("Dino8.Render.GradientTop", ColorKey(r.gradient_top));
  set("Dino8.Render.GradientBottom", ColorKey(r.gradient_bottom));
  set("Dino8.Render.GradientView", r.gradient_view ? "1" : "0");
  set("Dino8.Render.GroundPlane", r.ground_plane ? "1" : "0");
  set("Dino8.Render.GroundAutoHeight", r.ground_auto_height ? "1" : "0");
  set("Dino8.Render.GroundHeight", std::to_string(r.ground_height));
  set("Dino8.Render.GroundColor", ColorKey(r.ground_color));
  set("Dino8.Render.GroundShadows", r.ground_shadows ? "1" : "0");
  set("Dino8.Render.Sun", r.sun ? "1" : "0");
  set("Dino8.Render.SunAzimuth", std::to_string(r.sun_azimuth));
  set("Dino8.Render.SunAltitude", std::to_string(r.sun_altitude));
  set("Dino8.Render.SunIntensity", std::to_string(r.sun_intensity));
  set("Dino8.Render.SunColor", ColorKey(r.sun_color));
  set("Dino8.Render.Skylight", r.skylight ? "1" : "0");
  set("Dino8.Render.Width", std::to_string(r.render_width));
  set("Dino8.Render.Height", std::to_string(r.render_height));
  set("Dino8.Render.Quality", std::to_string(r.render_quality));
  if (!r.environment_image.empty()) set("Dino8.Render.EnvironmentImage", r.environment_image);
}

void ReadRenderSettings(const std::map<std::string, std::string>& strings, RenderSettings& r) {
  auto get = [&](const char* key) -> const std::string* { auto it = strings.find(key); return it == strings.end() ? nullptr : &it->second; };
  auto num = [&](const char* key, double& v) { if (const std::string* t = get(key)) v = std::atof(t->c_str()); };
  auto flag = [&](const char* key, bool& v) { if (const std::string* t = get(key)) v = *t == "1"; };
  auto col = [&](const char* key, Color& v) { if (const std::string* t = get(key)) ColorFromKey(*t, v); };
  if (const std::string* t = get("Dino8.Render.Background")) r.background = static_cast<RenderSettings::Background>(std::clamp(std::atoi(t->c_str()), 0, 2));
  col("Dino8.Render.BackgroundColor", r.background_color);
  col("Dino8.Render.GradientTop", r.gradient_top);
  col("Dino8.Render.GradientBottom", r.gradient_bottom);
  flag("Dino8.Render.GradientView", r.gradient_view);
  flag("Dino8.Render.GroundPlane", r.ground_plane);
  flag("Dino8.Render.GroundAutoHeight", r.ground_auto_height);
  num("Dino8.Render.GroundHeight", r.ground_height);
  col("Dino8.Render.GroundColor", r.ground_color);
  flag("Dino8.Render.GroundShadows", r.ground_shadows);
  flag("Dino8.Render.Sun", r.sun);
  num("Dino8.Render.SunAzimuth", r.sun_azimuth);
  num("Dino8.Render.SunAltitude", r.sun_altitude);
  double d = r.sun_intensity; num("Dino8.Render.SunIntensity", d); r.sun_intensity = static_cast<float>(d);
  col("Dino8.Render.SunColor", r.sun_color);
  flag("Dino8.Render.Skylight", r.skylight);
  d = r.render_width; num("Dino8.Render.Width", d); r.render_width = std::clamp(static_cast<int>(d), 16, 8192);
  d = r.render_height; num("Dino8.Render.Height", d); r.render_height = std::clamp(static_cast<int>(d), 16, 8192);
  d = r.render_quality; num("Dino8.Render.Quality", d); r.render_quality = std::clamp(static_cast<int>(d), 1, 4);
  if (const std::string* t = get("Dino8.Render.EnvironmentImage")) r.environment_image = *t;
}

void AddLightFromOn(Document& doc, const ON_Light& light_ref, const ON_3dmObjectAttributes* attr) {
  const ON_Light* light = &light_ref;
  Light L;
  L.name = FromWide(light->LightName());
  if (L.name.empty() && attr) L.name = FromWide(attr->Name());
  switch (light->Style()) {
    case ON::world_spot_light: case ON::camera_spot_light: L.type = LightType::Spot; break;
    case ON::world_directional_light: case ON::camera_directional_light: L.type = LightType::Directional; break;
    case ON::world_linear_light: L.type = LightType::Linear; break;
    case ON::world_rectangular_light: L.type = LightType::Rectangular; break;
    default: L.type = LightType::Point; break;
  }
  L.position = light->Location();
  L.direction = light->Direction();
  if (!L.direction.Unitize()) L.direction = kernel::Vector3d(0, 0, -1);
  L.color = FromOnColor(light->Diffuse());
  L.intensity = static_cast<float>(light->Intensity());
  L.spot_angle = static_cast<float>(std::clamp(light->SpotAngleDegrees(), 1.0, 89.0));
  L.enabled = light->IsEnabled();
  if (L.type == LightType::Spot) { L.length = light->Direction().Length(); if (L.length <= 0) L.length = 10; }
  if (L.type == LightType::Rectangular || L.type == LightType::Linear) {
    L.x_axis = light->Length();
    L.length = L.x_axis.Length();
    if (!L.x_axis.Unitize()) L.x_axis = kernel::Vector3d(1, 0, 0);
    L.width = light->Width().Length();
    if (L.type == LightType::Rectangular) { L.position = light->Location() + light->Length() * 0.5 + light->Width() * 0.5; }
  }
  doc.AddLight(L);
}

}  // namespace

// ---------------------------------------------------------------------------
// .3dm
// ---------------------------------------------------------------------------

bool Load3dm(Document& doc, const std::string& path, std::string& error) {
  ONX_Model model;
  ON_TextLog log;
  if (!model.Read(path.c_str(), &log)) {
    error = "OpenNURBS could not read " + path;
    return false;
  }
  doc.Clear();

  // Layers: map layer index in the file -> layer index in the document.
  std::map<int, int> layer_map;
  std::map<ON_UUID, int, bool (*)(const ON_UUID&, const ON_UUID&)> layer_by_id(
      [](const ON_UUID& a, const ON_UUID& b) { return ON_UuidCompare(a, b) < 0; });
  bool first_layer = true;
  {
    ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::Layer);
    for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
      const ON_Layer* layer = ON_Layer::Cast(c);
      if (!layer) continue;
      const std::string name = FromWide(layer->Name());
      int idx;
      if (first_layer) {
        idx = 0;
        doc.Layers()[0].name = name.empty() ? "Default" : name;
        first_layer = false;
      } else {
        idx = doc.AddLayer(name.empty() ? "Layer" : name);
      }
      Layer& L = doc.Layers()[static_cast<size_t>(idx)];
      L.color = FromOnColor(layer->Color());
      L.visible = layer->IsVisible();
      L.locked = layer->IsLocked();
      layer_map[layer->Index()] = idx;
      layer_by_id[layer->Id()] = idx;
    }
    // Resolve parents.
    it = ONX_ModelComponentIterator(model, ON_ModelComponent::Type::Layer);
    for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
      const ON_Layer* layer = ON_Layer::Cast(c);
      if (!layer) continue;
      const ON_UUID pid = layer->ParentLayerId();
      if (ON_UuidIsNil(pid)) continue;
      auto me = layer_by_id.find(layer->Id());
      auto parent = layer_by_id.find(pid);
      if (me != layer_by_id.end() && parent != layer_by_id.end()) {
        doc.Layers()[static_cast<size_t>(me->second)].parent = parent->second;
      }
    }
  }

  // Materials: file index -> document material name.
  std::map<int, std::string> material_map;
  {
    ONX_ModelComponentIterator mit(model, ON_ModelComponent::Type::RenderMaterial);
    for (const ON_ModelComponent* c = mit.FirstComponent(); c; c = mit.NextComponent()) {
      const ON_Material* om = ON_Material::Cast(c);
      if (!om) continue;
      Material m;
      m.name = FromWide(om->Name());
      if (m.name.empty()) m.name = "Material " + std::to_string(om->Index());
      m.diffuse = FromOnColor(om->Diffuse());
      m.specular = FromOnColor(om->Specular());
      m.emission = FromOnColor(om->Emission());
      m.gloss = static_cast<float>(std::clamp(om->Shine() / ON_Material::MaxShine, 0.0, 1.0));
      m.transparency = static_cast<float>(std::clamp(om->Transparency(), 0.0, 1.0));
      m.reflectivity = static_cast<float>(std::clamp(om->Reflectivity(), 0.0, 1.0));
      for (int t = 0; t < om->m_textures.Count(); ++t) {
        const ON_Texture& tx = om->m_textures[t];
        if (tx.m_type != ON_Texture::TYPE::bitmap_texture && tx.m_type != ON_Texture::TYPE::pbr_base_color_texture) continue;
        m.texture_path = FromWide(tx.m_image_file_reference.FullPath());
        if (m.texture_path.empty()) m.texture_path = FromWide(tx.m_image_file_reference.RelativePath());
        break;
      }
      ON_wString v;
      if (om->GetUserString(L"Dino8.Mapping", v)) ParseTextureMapping(FromWide(v), m.mapping);
      if (om->GetUserString(L"Dino8.MappingScale", v)) m.mapping_scale = static_cast<float>(std::atof(FromWide(v).c_str()));
      if (m.mapping == TextureMapping::Default) m.mapping = TextureMapping::Surface;
      // Names collide? Keep the first; later ones get a suffix.
      std::string base = m.name;
      for (int k = 2; doc.FindMaterial(m.name); ++k) m.name = base + " " + std::to_string(k);
      material_map[om->Index()] = doc.AddMaterial(m);
    }
  }
  // Layer render materials.
  {
    ONX_ModelComponentIterator lit(model, ON_ModelComponent::Type::Layer);
    for (const ON_ModelComponent* c = lit.FirstComponent(); c; c = lit.NextComponent()) {
      const ON_Layer* layer = ON_Layer::Cast(c);
      if (!layer) continue;
      auto lm = layer_map.find(layer->Index());
      auto mm = material_map.find(layer->RenderMaterialIndex());
      if (lm != layer_map.end() && mm != material_map.end()) doc.Layers()[static_cast<size_t>(lm->second)].material = mm->second;
    }
  }

  int skipped = 0;
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
    if (!mg) continue;
    const ON_Geometry* g = mg->Geometry(nullptr);
    const ON_3dmObjectAttributes* attr = mg->Attributes(nullptr);
    if (!g) continue;
    SceneObject obj;
    bool made = false;
    if (const ON_Light* light = ON_Light::Cast(g)) {
      AddLightFromOn(doc, *light, attr);
      continue;
    }
    if (const ON_Point* p = ON_Point::Cast(g)) {
      obj = SceneObject::MakePoint(p->point);
      made = true;
    } else if (const ON_Curve* cv = ON_Curve::Cast(g)) {
      ON_NurbsCurve nc;
      if (cv->GetNurbForm(nc) > 0) {
        kernel::NurbsCurve k;
        k.raw() = nc;
        obj = SceneObject::MakeCurve(k);
        made = true;
      }
    } else if (const ON_Brep* b = ON_Brep::Cast(g)) {
      kernel::Brep k;
      k.raw() = *b;
      obj = SceneObject::MakeBrep(k);
      made = true;
    } else if (const ON_Surface* s = ON_Surface::Cast(g)) {
      ON_NurbsSurface ns;
      if (s->GetNurbForm(ns) > 0) {
        kernel::NurbsSurface k;
        k.raw() = ns;
        obj = SceneObject::MakeSurface(k);
        made = true;
      }
    } else if (const ON_Mesh* m = ON_Mesh::Cast(g)) {
      kernel::Mesh k;
      k.raw() = *m;
      obj = SceneObject::MakeMesh(k);
      made = true;
    } else if (const ON_SubD* sd = ON_SubD::Cast(g)) {
      kernel::SubD k;
      k.raw() = *sd;
      obj = SceneObject::MakeSubD(k);
      made = true;
    } else if (const ON_Extrusion* ex = ON_Extrusion::Cast(g)) {
      ON_Brep* b = ex->BrepForm(nullptr);
      if (b) {
        kernel::Brep k;
        k.raw() = *b;
        delete b;
        obj = SceneObject::MakeBrep(k);
        made = true;
      }
    } else if (const ON_PointCloud* pc = ON_PointCloud::Cast(g)) {
      for (int i = 0; i < pc->PointCount(); ++i) {
        SceneObject po = SceneObject::MakePoint(pc->m_P[i]);
        if (attr) {
          auto lm = layer_map.find(attr->m_layer_index);
          if (lm != layer_map.end()) po.layer_index = lm->second;
        }
        doc.Add(std::move(po));
      }
      continue;
    }
    if (!made) {
      ++skipped;
      continue;
    }
    if (attr) {
      obj.name = FromWide(attr->Name());
      auto lm = layer_map.find(attr->m_layer_index);
      if (lm != layer_map.end()) obj.layer_index = lm->second;
      if (attr->ColorSource() == ON::color_from_object) {
        obj.color_by_layer = false;
        obj.color = FromOnColor(attr->m_color);
      }
      obj.visible = attr->IsVisible();
      obj.locked = attr->Mode() == ON::locked_object;
      if (attr->MaterialSource() == ON::material_from_object) {
        auto mm = material_map.find(attr->m_material_index);
        if (mm != material_map.end()) obj.material_name = mm->second;
      }
      // Attribute user strings.
      ON_ClassArray<ON_UserString> strings;
      attr->GetUserStrings(strings);
      for (int i = 0; i < strings.Count(); ++i) {
        const std::string key = FromWide(strings[i].m_key), val = FromWide(strings[i].m_string_value);
        if (key == "Dino8.Mapping") { ParseTextureMapping(val, obj.mapping); continue; }
        if (key == "Dino8.MappingScale") { obj.mapping_scale = static_cast<float>(std::atof(val.c_str())); continue; }
        obj.user_text[key] = val;
      }
    }
    doc.Add(std::move(obj));
  }

  // Render lights live in their own table.
  {
    ONX_ModelComponentIterator lit(model, ON_ModelComponent::Type::RenderLight);
    for (const ON_ModelComponent* c = lit.FirstComponent(); c; c = lit.NextComponent()) {
      const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
      if (!mg) continue;
      if (const ON_Light* light = ON_Light::Cast(mg->Geometry(nullptr))) AddLightFromOn(doc, *light, mg->Attributes(nullptr));
    }
  }

  // Document notes, metadata and user text.
  doc.Notes() = FromWide(model.m_properties.m_Notes.m_notes);
  doc.Settings().author = FromWide(model.m_properties.m_RevisionHistory.m_sCreatedBy);
  {
    ON_wString v;
    if (model.GetDocumentUserString(L"Dino8.Title", v)) doc.Settings().title = FromWide(v);
    if (model.GetDocumentUserString(L"Dino8.Comments", v)) doc.Settings().comments = FromWide(v);
  }
  {
    ON_ClassArray<ON_UserString> strings;
    model.GetDocumentUserStrings(strings);
    std::map<std::string, std::string> render_strings;
    for (int i = 0; i < strings.Count(); ++i) {
      const std::string key = FromWide(strings[i].m_key);
      if (key.compare(0, 13, "Dino8.Render.") == 0) { render_strings[key] = FromWide(strings[i].m_string_value); continue; }
      if (key == "Dino8.Title" || key == "Dino8.Comments") continue;
      doc.UserText()[key] = FromWide(strings[i].m_string_value);
    }
    ReadRenderSettings(render_strings, doc.Render());
  }
  // Named views.
  for (int i = 0; i < model.m_settings.m_named_views.Count(); ++i) {
    const ON_3dmView& v = model.m_settings.m_named_views[i];
    NamedView nv;
    nv.name = FromWide(v.m_name);
    nv.camera.eye = v.m_vp.CameraLocation();
    nv.camera.target = v.m_vp.TargetPoint();
    nv.camera.up = v.m_vp.CameraUp();
    nv.camera.perspective = v.m_vp.IsPerspectiveProjection();
    double l, r, b, t;
    if (v.m_vp.GetFrustum(&l, &r, &b, &t) && !nv.camera.perspective) nv.camera.ortho_height = t - b;
    doc.NamedViews().push_back(nv);
  }
  // Units / tolerances.
  switch (model.m_settings.m_ModelUnitsAndTolerances.m_unit_system.UnitSystem()) {
    case ON::LengthUnitSystem::Inches: doc.Settings().unit_system = "Inches"; break;
    case ON::LengthUnitSystem::Feet: doc.Settings().unit_system = "Feet"; break;
    case ON::LengthUnitSystem::Centimeters: doc.Settings().unit_system = "Centimeters"; break;
    case ON::LengthUnitSystem::Meters: doc.Settings().unit_system = "Meters"; break;
    default: doc.Settings().unit_system = "Millimeters"; break;
  }
  if (model.m_settings.m_ModelUnitsAndTolerances.m_absolute_tolerance > 0) {
    doc.Settings().absolute_tolerance = model.m_settings.m_ModelUnitsAndTolerances.m_absolute_tolerance;
  }
  if (skipped > 0) error = std::to_string(skipped) + " unsupported object(s) were skipped";
  doc.ClearUndo();
  return true;
}

bool Save3dm(const Document& doc, const std::string& path, std::string& error) {
  ONX_Model model;
  model.m_sStartSectionComments = "Dino 8 - free NURBS modeler";
  model.m_properties.m_Application.m_application_name = L"Dino 8";
  model.m_properties.m_Application.m_application_URL = L"https://github.com/mlegere9789-collab/miniature-pancake";
  model.m_properties.m_Notes.m_notes = ON_wString(const_cast<Document&>(doc).Notes().c_str());
  model.m_properties.m_Notes.m_bVisible = !const_cast<Document&>(doc).Notes().empty();
  for (const auto& [k, v] : const_cast<Document&>(doc).UserText()) {
    model.SetDocumentUserString(ON_wString(k.c_str()), ON_wString(v.c_str()));
  }
  if (!doc.Settings().title.empty()) model.SetDocumentUserString(L"Dino8.Title", ON_wString(doc.Settings().title.c_str()));
  if (!doc.Settings().comments.empty()) model.SetDocumentUserString(L"Dino8.Comments", ON_wString(doc.Settings().comments.c_str()));
  WriteRenderSettings(model, doc.Render());
  model.m_properties.m_RevisionHistory.m_sCreatedBy = ON_wString(doc.Settings().author.c_str());
  model.m_properties.m_RevisionHistory.m_sLastEditedBy = ON_wString(doc.Settings().author.c_str());
  model.m_properties.m_RevisionHistory.m_revision_count += 1;

  // Units.
  ON::LengthUnitSystem us = ON::LengthUnitSystem::Millimeters;
  const std::string& u = doc.Settings().unit_system;
  if (u == "Inches") us = ON::LengthUnitSystem::Inches;
  else if (u == "Feet") us = ON::LengthUnitSystem::Feet;
  else if (u == "Centimeters") us = ON::LengthUnitSystem::Centimeters;
  else if (u == "Meters") us = ON::LengthUnitSystem::Meters;
  model.m_settings.m_ModelUnitsAndTolerances.m_unit_system = ON_UnitSystem(us);
  model.m_settings.m_ModelUnitsAndTolerances.m_absolute_tolerance = doc.Settings().absolute_tolerance;
  model.m_settings.m_ModelUnitsAndTolerances.m_angle_tolerance = doc.Settings().angle_tolerance_degrees * ON_PI / 180.0;

  // Materials.
  std::map<std::string, int> material_index;
  for (const Material& m : doc.Materials()) {
    ON_Material om;
    om.SetName(ON_wString(m.name.c_str()));
    om.SetDiffuse(ToOnColor(m.diffuse));
    om.SetSpecular(ToOnColor(m.specular));
    om.SetEmission(ToOnColor(m.emission));
    om.SetShine(std::clamp(static_cast<double>(m.gloss), 0.0, 1.0) * ON_Material::MaxShine);
    om.SetTransparency(std::clamp(static_cast<double>(m.transparency), 0.0, 1.0));
    om.SetReflectivity(std::clamp(static_cast<double>(m.reflectivity), 0.0, 1.0));
    if (!m.texture_path.empty()) {
      ON_Texture tx;
      tx.m_image_file_reference.SetFullPath(m.texture_path.c_str(), false);
      tx.m_type = ON_Texture::TYPE::bitmap_texture;
      om.AddTexture(tx);
    }
    om.SetUserString(L"Dino8.Mapping", ON_wString(TextureMappingName(m.mapping)));
    om.SetUserString(L"Dino8.MappingScale", ON_wString(std::to_string(m.mapping_scale).c_str()));
    ON_ModelComponentReference ref = model.AddModelComponent(om, true);
    if (const ON_Material* stored = ON_Material::Cast(ref.ModelComponent())) material_index[m.name] = stored->Index();
  }

  // Layers.
  std::vector<int> file_layer_index(doc.Layers().size(), 0);
  std::vector<ON_UUID> layer_ids(doc.Layers().size(), ON_nil_uuid);
  for (size_t i = 0; i < doc.Layers().size(); ++i) {
    const Layer& L = doc.Layers()[i];
    ON_Layer layer;
    layer.SetName(ON_wString(L.name.c_str()));
    layer.SetColor(ToOnColor(L.color));
    layer.SetVisible(L.visible);
    layer.SetLocked(L.locked);
    if (L.parent >= 0 && static_cast<size_t>(L.parent) < i) layer.SetParentLayerId(layer_ids[static_cast<size_t>(L.parent)]);
    ON_CreateUuid(layer_ids[i]);
    layer.SetId(layer_ids[i]);
    file_layer_index[i] = model.AddLayer(ON_wString(L.name.c_str()), ToOnColor(L.color));
    // AddLayer created a fresh layer; update its properties in place.
    ON_ModelComponentReference ref = model.LayerFromIndex(file_layer_index[i]);
    if (ON_Layer* stored = const_cast<ON_Layer*>(ON_Layer::Cast(ref.ModelComponent()))) {
      stored->SetVisible(L.visible);
      stored->SetLocked(L.locked);
      if (!L.material.empty() && material_index.count(L.material)) stored->SetRenderMaterialIndex(material_index[L.material]);
      if (L.parent >= 0 && static_cast<size_t>(L.parent) < i) {
        ON_ModelComponentReference pref = model.LayerFromIndex(file_layer_index[static_cast<size_t>(L.parent)]);
        if (const ON_Layer* pl = ON_Layer::Cast(pref.ModelComponent())) stored->SetParentLayerId(pl->Id());
      }
    }
  }
  if (!doc.Layers().empty()) {
    ON_ModelComponentReference cur = model.LayerFromIndex(file_layer_index[static_cast<size_t>(std::max(0, doc.CurrentLayer()))]);
    if (const ON_Layer* cl = ON_Layer::Cast(cur.ModelComponent())) model.m_settings.SetCurrentLayerId(cl->Id());
  }

  // Named views.
  for (const NamedView& nv : const_cast<Document&>(doc).NamedViews()) {
    ON_3dmView v;
    v.m_name = ON_wString(nv.name.c_str());
    v.m_vp.SetProjection(nv.camera.perspective ? ON::perspective_view : ON::parallel_view);
    v.m_vp.SetCameraLocation(nv.camera.eye);
    v.m_vp.SetCameraDirection(nv.camera.target - nv.camera.eye);
    v.m_vp.SetCameraUp(nv.camera.up);
    v.m_vp.SetTargetPoint(nv.camera.target);
    model.m_settings.m_named_views.Append(v);
  }

  int written = 0;
  for (const SceneObject& o : doc.Objects()) {
    ON_3dmObjectAttributes attr;
    ON_CreateUuid(attr.m_uuid);
    attr.SetName(ON_wString(o.name.c_str()), true);
    attr.m_layer_index = file_layer_index[static_cast<size_t>(std::clamp(o.layer_index, 0, static_cast<int>(doc.Layers().size()) - 1))];
    if (!o.color_by_layer) {
      attr.SetColorSource(ON::color_from_object);
      attr.m_color = ToOnColor(o.color);
    }
    attr.SetVisible(o.visible);
    attr.SetMode(o.locked ? ON::locked_object : ON::normal_object);
    for (const auto& [k, v] : o.user_text) attr.SetUserString(ON_wString(k.c_str()), ON_wString(v.c_str()));
    if (!o.material_name.empty() && material_index.count(o.material_name)) {
      attr.m_material_index = material_index[o.material_name];
      attr.SetMaterialSource(ON::material_from_object);
    }
    if (o.mapping != TextureMapping::Default) {
      attr.SetUserString(L"Dino8.Mapping", ON_wString(TextureMappingName(o.mapping)));
      attr.SetUserString(L"Dino8.MappingScale", ON_wString(std::to_string(o.mapping_scale).c_str()));
    }

    ON_Geometry* g = nullptr;
    switch (o.kind) {
      case ObjectKind::Point: g = new ON_Point(o.point); break;
      case ObjectKind::Curve: if (o.curve) g = new ON_NurbsCurve(o.curve->raw()); break;
      case ObjectKind::Surface: if (o.surface) g = new ON_NurbsSurface(o.surface->raw()); break;
      case ObjectKind::Brep: if (o.brep) g = new ON_Brep(o.brep->raw()); break;
      case ObjectKind::Mesh: if (o.mesh) g = new ON_Mesh(o.mesh->raw()); break;
      case ObjectKind::SubD: if (o.subd) g = new ON_SubD(o.subd->raw()); break;
    }
    if (!g) continue;
    model.AddModelGeometryComponent(g, &attr);
    ++written;
  }

  // Lights are geometry components in the 3dm.
  for (const Light& L : doc.Lights()) {
    ON_Light* light = new ON_Light();
    light->SetLightName(L.name.c_str());
    switch (L.type) {
      case LightType::Point: light->SetStyle(ON::world_point_light); light->SetLocation(L.position); break;
      case LightType::Spot:
        light->SetStyle(ON::world_spot_light);
        light->SetLocation(L.position);
        light->SetDirection(L.direction * (L.length > 0 ? L.length : 10.0));
        light->SetSpotAngleDegrees(L.spot_angle);
        break;
      case LightType::Directional: light->SetStyle(ON::world_directional_light); light->SetLocation(L.position); light->SetDirection(L.direction); break;
      case LightType::Rectangular: {
        light->SetStyle(ON::world_rectangular_light);
        kernel::Vector3d y = ON_CrossProduct(L.direction, L.x_axis);
        y.Unitize();
        light->SetLocation(L.position - L.x_axis * (L.length / 2) - y * (L.width / 2));
        light->SetDirection(L.direction);
        light->SetLength(L.x_axis * L.length);
        light->SetWidth(y * L.width);
        break;
      }
      case LightType::Linear:
        light->SetStyle(ON::world_linear_light);
        light->SetLocation(L.position);
        light->SetDirection(L.direction);
        light->SetLength(L.x_axis * L.length);
        break;
    }
    light->SetDiffuse(ToOnColor(L.color));
    light->SetIntensity(L.intensity);
    light->Enable(L.enabled);
    ON_3dmObjectAttributes attr;
    ON_CreateUuid(attr.m_uuid);
    attr.SetName(ON_wString(L.name.c_str()), true);
    model.AddModelGeometryComponent(light, &attr);
  }

  ON_TextLog log;
  if (!model.Write(path.c_str(), 0, &log)) {
    error = "OpenNURBS could not write " + path;
    return false;
  }
  (void)written;
  return true;
}

// ---------------------------------------------------------------------------
// OBJ / STL
// ---------------------------------------------------------------------------

bool ImportMeshFile(Document& doc, const std::string& path, std::string& error) {
  const std::string ext = LowerExt(path);
  kernel::Mesh mesh;
  kernel::Result r = kernel::Result::Failed;
  if (ext == ".obj") r = kernel::Mesh::LoadObj(path, mesh);
  else if (ext == ".stl") r = kernel::Mesh::LoadStl(path, mesh);
  else {
    error = "Unsupported mesh format: " + ext;
    return false;
  }
  if (r != kernel::Result::Ok || mesh.FaceCount() == 0) {
    error = "Could not read a mesh from " + path;
    return false;
  }
  SceneObject o = SceneObject::MakeMesh(mesh);
  o.name = std::filesystem::path(path).stem().string();
  doc.Add(std::move(o));
  return true;
}

bool ExportMeshFile(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  const std::string ext = LowerExt(path);
  std::vector<kernel::Mesh> meshes;
  for (const SceneObject& o : doc.Objects()) {
    if (selected_only && !o.selected) continue;
    if (!doc.IsObjectVisible(o)) continue;
    if (o.kind == ObjectKind::Mesh && o.mesh) {
      meshes.push_back(*o.mesh);
    } else if (o.kind == ObjectKind::Brep && o.brep) {
      BrepMeshOptions opt; opt.chord_tolerance = 0.01; meshes.push_back(MeshBrepClosed(o.brep->raw(), opt));
    } else if (o.kind == ObjectKind::Surface && o.surface) {
      meshes.push_back(o.surface->TessellateGridAdaptive(0.01));
    } else if (o.kind == ObjectKind::SubD && o.subd) {
      meshes.push_back(o.subd->ToApproximateMesh());
    }
  }
  if (meshes.empty()) {
    error = "Nothing to export: select meshes, surfaces, polysurfaces or SubDs";
    return false;
  }
  kernel::Mesh merged = meshes.size() == 1 ? meshes[0] : kernel::Mesh::MergeAndWeld(meshes);
  kernel::Result r = kernel::Result::Failed;
  if (ext == ".obj") r = merged.SaveObj(path);
  else if (ext == ".stl") r = merged.SaveStlBinary(path);
  else {
    error = "Unsupported export format: " + ext;
    return false;
  }
  if (r != kernel::Result::Ok) {
    error = "Could not write " + path;
    return false;
  }
  return true;
}

}  // namespace dino8::app
