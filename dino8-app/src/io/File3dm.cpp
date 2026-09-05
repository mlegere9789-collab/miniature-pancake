#include "io/File3dm.h"

#include "geom/BrepMesher.h"

#include <opennurbs.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>

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

bool UuidLess(const ON_UUID& a, const ON_UUID& b) { return ON_UuidCompare(a, b) < 0; }
using UuidMap = std::map<ON_UUID, int, bool (*)(const ON_UUID&, const ON_UUID&)>;

void CameraToViewport(const CameraState& c, ON_Viewport& vp) {
  vp.SetProjection(c.perspective ? ON::perspective_view : ON::parallel_view);
  vp.SetCameraLocation(c.eye);
  vp.SetCameraDirection(c.target - c.eye);
  vp.SetCameraUp(c.up);
  vp.SetTargetPoint(c.target);
  if (!c.perspective) vp.SetFrustum(-c.ortho_height / 2, c.ortho_height / 2, -c.ortho_height / 2, c.ortho_height / 2, 1, 1e6);
}

CameraState ViewportToCamera(const ON_Viewport& vp) {
  CameraState c;
  c.eye = vp.CameraLocation();
  c.target = vp.TargetPoint();
  c.up = vp.CameraUp();
  c.perspective = vp.IsPerspectiveProjection();
  double l, r, b, t;
  if (vp.GetFrustum(&l, &r, &b, &t) && !c.perspective && t - b > 0) c.ortho_height = t - b;
  return c;
}

std::string UuidString(const ON_UUID& id) { char buf[64] = {}; ON_UuidToString(id, buf); return buf; }

// Animation frames <-> one document user string.
std::string AnimationToString(const Animation& a) {
  std::ostringstream out;
  out << a.kind << "|" << a.viewport << "|";
  for (size_t i = 0; i < a.frames.size(); ++i) {
    const CameraState& c = a.frames[i];
    if (i) out << ";";
    out << c.eye.x << "," << c.eye.y << "," << c.eye.z << "," << c.target.x << "," << c.target.y << "," << c.target.z << ","
        << c.up.x << "," << c.up.y << "," << c.up.z << "," << (c.perspective ? 1 : 0) << "," << c.ortho_height << "," << c.lens_mm;
  }
  return out.str();
}

Animation AnimationFromString(const std::string& text) {
  Animation a;
  const size_t p1 = text.find('|');
  if (p1 == std::string::npos) return a;
  const size_t p2 = text.find('|', p1 + 1);
  if (p2 == std::string::npos) return a;
  a.kind = text.substr(0, p1);
  a.viewport = text.substr(p1 + 1, p2 - p1 - 1);
  std::istringstream in(text.substr(p2 + 1));
  std::string frame;
  while (std::getline(in, frame, ';')) {
    double v[12] = {};
    if (std::sscanf(frame.c_str(), "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11]) != 12) continue;
    CameraState c;
    c.eye = kernel::Point3d(v[0], v[1], v[2]); c.target = kernel::Point3d(v[3], v[4], v[5]); c.up = kernel::Vector3d(v[6], v[7], v[8]);
    c.perspective = v[9] != 0; c.ortho_height = v[10]; c.lens_mm = v[11];
    a.frames.push_back(c);
  }
  return a;
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
  // Linetypes: the file's table (index -> name) merged into the document's.
  std::map<int, std::string> linetype_by_index;
  {
    ONX_ModelComponentIterator lit(model, ON_ModelComponent::Type::LinePattern);
    for (const ON_ModelComponent* c = lit.FirstComponent(); c; c = lit.NextComponent()) {
      const ON_Linetype* lt = ON_Linetype::Cast(c);
      if (!lt) continue;
      std::string name = FromWide(lt->Name());
      if (name.empty()) continue;
      std::vector<double> pattern;
      for (int i = 0; i < lt->SegmentCount(); ++i) pattern.push_back(lt->Segment(i).m_length);
      // A pattern of a single dash is continuous.
      bool any_gap = false;
      for (int i = 0; i < lt->SegmentCount(); ++i) if (lt->Segment(i).m_seg_type == ON_LinetypeSegment::eSegType::stSpace) any_gap = true;
      if (!any_gap) pattern.clear();
      if (!doc.FindLinetype(name)) doc.SetLinetype(name, pattern);
      else if (!pattern.empty()) doc.FindLinetype(name)->pattern = pattern;
      linetype_by_index[lt->Index()] = name;
    }
    ONX_ModelComponentIterator lyr(model, ON_ModelComponent::Type::Layer);
    for (const ON_ModelComponent* c = lyr.FirstComponent(); c; c = lyr.NextComponent()) {
      const ON_Layer* layer = ON_Layer::Cast(c);
      if (!layer) continue;
      auto me = layer_by_id.find(layer->Id());
      auto lt = linetype_by_index.find(layer->LinetypeIndex());
      if (me != layer_by_id.end() && lt != linetype_by_index.end()) doc.Layers()[static_cast<size_t>(me->second)].linetype = lt->second;
    }
  }
  // Viewports and layout pages: model views give clipping planes their
  // viewport names, page views become layouts.
  std::map<ON_UUID, std::string, bool (*)(const ON_UUID&, const ON_UUID&)> view_names(UuidLess);
  UuidMap page_layout(UuidLess);
  for (int i = 0; i < model.m_settings.m_views.Count(); ++i) {
    const ON_3dmView& v = model.m_settings.m_views[i];
    if (v.m_view_type == ON::page_view_type) {
      Layout L;
      L.name = FromWide(v.m_name);
      if (L.name.empty()) L.name = "Layout " + std::to_string(doc.Layouts().size() + 1);
      if (v.m_page_settings.m_width_mm > 0) L.width_mm = v.m_page_settings.m_width_mm;
      if (v.m_page_settings.m_height_mm > 0) L.height_mm = v.m_page_settings.m_height_mm;
      page_layout[v.m_vp.ViewportId()] = static_cast<int>(doc.Layouts().size());
      doc.Layouts().push_back(L);
    } else {
      view_names[v.m_vp.ViewportId()] = FromWide(v.m_name);
    }
  }
  struct PendingDetail { int layout; size_t detail; std::string hidden_objects; };
  std::vector<PendingDetail> pending_details;
  UuidMap object_ids(UuidLess);  // object uuid -> document id (as int)

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
    if (const ON_ClippingPlaneSurface* cps = ON_ClippingPlaneSurface::Cast(g)) {
      ClippingPlane cp;
      cp.origin = cps->m_plane.origin;
      cp.x_axis = cps->m_plane.xaxis;
      cp.y_axis = cps->m_plane.yaxis;
      cp.width = std::max(cps->Extents(0).Length(), 0.1);
      cp.height = std::max(cps->Extents(1).Length(), 0.1);
      cp.enabled = cps->m_clipping_plane.m_bEnabled;
      for (int i = 0; i < cps->m_clipping_plane.m_viewport_ids.Count(); ++i) {
        auto vn = view_names.find(cps->m_clipping_plane.m_viewport_ids.Array()[i]);
        if (vn != view_names.end() && !vn->second.empty()) cp.viewports.push_back(vn->second);
      }
      if (attr) cp.name = FromWide(attr->Name());
      doc.AddClippingPlane(cp);
      continue;
    }
    if (const ON_DetailView* dv = ON_DetailView::Cast(g)) {
      int layout = -1;
      if (attr) {
        auto pl = page_layout.find(attr->m_viewport_id);
        if (pl != page_layout.end()) layout = pl->second;
      }
      if (layout < 0 && !doc.Layouts().empty()) layout = 0;
      if (layout < 0) { ++skipped; continue; }
      Layout& L = doc.Layouts()[static_cast<size_t>(layout)];
      LayoutDetail d;
      d.name = FromWide(dv->m_view.m_name);
      if (d.name.empty() && attr) d.name = FromWide(attr->Name());
      if (d.name.empty()) d.name = "Detail " + std::to_string(L.details.size() + 1);
      d.camera = ViewportToCamera(dv->m_view.m_vp);
      ON_BoundingBox bb;
      if (dv->m_boundary.GetBoundingBox(bb)) { d.x = bb.m_min.x; d.y = bb.m_min.y; d.width = std::max(bb.m_max.x - bb.m_min.x, 1.0); d.height = std::max(bb.m_max.y - bb.m_min.y, 1.0); }
      d.scale = dv->m_page_per_model_ratio > 0 ? dv->m_page_per_model_ratio : 0;
      std::string hidden;
      if (attr) {
        ON_wString v;
        if (attr->GetUserString(L"Dino8.DetailLocked", v)) d.locked = FromWide(v) == "1";
        if (attr->GetUserString(L"Dino8.DetailMode", v)) d.display_mode = FromWide(v);
        if (attr->GetUserString(L"Dino8.DetailView", v)) d.standard_view = FromWide(v);
        if (attr->GetUserString(L"Dino8.HiddenObjects", v)) hidden = FromWide(v);
      }
      const ON_UUID detail_id = dv->m_view.m_vp.ViewportId();
      ONX_ModelComponentIterator lit(model, ON_ModelComponent::Type::Layer);
      for (const ON_ModelComponent* lc = lit.FirstComponent(); lc; lc = lit.NextComponent()) {
        const ON_Layer* layer = ON_Layer::Cast(lc);
        if (!layer || layer->PerViewportIsVisible(detail_id)) continue;
        auto lm = layer_map.find(layer->Index());
        if (lm != layer_map.end()) d.hidden_layers.push_back(lm->second);
      }
      L.details.push_back(d);
      pending_details.push_back({layout, L.details.size() - 1, hidden});
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
      if (attr->LinetypeSource() == ON::linetype_from_object) {
        auto lt = linetype_by_index.find(attr->m_linetype_index);
        if (lt != linetype_by_index.end()) obj.linetype = lt->second;
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
    const ObjectId added = doc.Add(std::move(obj));
    if (attr) object_ids[attr->m_uuid] = static_cast<int>(added);
  }
  // Per-detail hidden objects (saved as object uuids).
  for (const PendingDetail& pd : pending_details) {
    LayoutDetail& d = doc.Layouts()[static_cast<size_t>(pd.layout)].details[pd.detail];
    std::istringstream in(pd.hidden_objects);
    std::string tok;
    while (std::getline(in, tok, ';')) {
      if (tok.empty()) continue;
      auto oi = object_ids.find(ON_UuidFromString(tok.c_str()));
      if (oi != object_ids.end()) d.hidden_objects.push_back(static_cast<ObjectId>(oi->second));
    }
  }
  // Named construction planes.
  for (int i = 0; i < model.m_settings.m_named_cplanes.Count(); ++i) {
    const ON_3dmConstructionPlane& c = model.m_settings.m_named_cplanes[i];
    NamedCPlane n;
    n.name = FromWide(c.m_name);
    if (n.name.empty()) n.name = "CPlane " + std::to_string(i + 1);
    n.origin = c.m_plane.origin; n.x_axis = c.m_plane.xaxis; n.y_axis = c.m_plane.yaxis;
    doc.NamedCPlanes().push_back(n);
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
    if (model.GetDocumentUserString(L"Dino8.LinetypeScale", v)) doc.Settings().linetype_scale = std::max(1e-6, std::atof(FromWide(v).c_str()));
    if (model.GetDocumentUserString(L"Dino8.LinetypeDisplay", v)) doc.Settings().linetype_display = FromWide(v) != "0";
    if (model.GetDocumentUserString(L"Dino8.DimensionLayer", v)) doc.Settings().dimension_layer = FromWide(v);
    if (model.GetDocumentUserString(L"Dino8.AnnotationStyle", v)) doc.Settings().annotation_style = FromWide(v);
    if (model.GetDocumentUserString(L"Dino8.HatchBase", v)) {
      double x = 0, y = 0, z = 0;
      if (std::sscanf(FromWide(v).c_str(), "%lf,%lf,%lf", &x, &y, &z) == 3) doc.Settings().hatch_base = kernel::Point3d(x, y, z);
    }
    if (model.GetDocumentUserString(L"Dino8.Animation", v)) doc.GetAnimation() = AnimationFromString(FromWide(v));
  }
  {
    ON_ClassArray<ON_UserString> strings;
    model.GetDocumentUserStrings(strings);
    std::map<std::string, std::string> render_strings;
    for (int i = 0; i < strings.Count(); ++i) {
      const std::string key = FromWide(strings[i].m_key);
      if (key.compare(0, 13, "Dino8.Render.") == 0) { render_strings[key] = FromWide(strings[i].m_string_value); continue; }
      const std::string value = FromWide(strings[i].m_string_value);
      const std::string style_prefix = "Dino8.AnnotationStyle.";
      if (key.compare(0, style_prefix.size(), style_prefix) == 0) {
        // "height;arrow;font"
        AnnotationStyle st;
        st.name = key.substr(style_prefix.size());
        char font[256] = "";
        std::sscanf(value.c_str(), "%lf;%lf;%255[^\n]", &st.text_height, &st.arrow_size, font);
        st.font = font;
        if (AnnotationStyle* existing = doc.FindAnnotationStyle(st.name)) *existing = st; else doc.AnnotationStyles().push_back(st);
        continue;
      }
      if (key.compare(0, 6, "Dino8.") == 0) continue;  // settings, handled above
      doc.UserText()[key] = value;
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
  {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%g", doc.Settings().linetype_scale);
    model.SetDocumentUserString(L"Dino8.LinetypeScale", ON_wString(buf));
    model.SetDocumentUserString(L"Dino8.LinetypeDisplay", doc.Settings().linetype_display ? L"1" : L"0");
    if (!doc.Settings().dimension_layer.empty()) model.SetDocumentUserString(L"Dino8.DimensionLayer", ON_wString(doc.Settings().dimension_layer.c_str()));
    model.SetDocumentUserString(L"Dino8.AnnotationStyle", ON_wString(doc.Settings().annotation_style.c_str()));
    const kernel::Point3d hb = doc.Settings().hatch_base;
    std::snprintf(buf, sizeof(buf), "%g,%g,%g", hb.x, hb.y, hb.z);
    model.SetDocumentUserString(L"Dino8.HatchBase", ON_wString(buf));
    for (const AnnotationStyle& st : doc.AnnotationStyles()) {
      std::snprintf(buf, sizeof(buf), "%g;%g;%s", st.text_height, st.arrow_size, st.font.c_str());
      model.SetDocumentUserString(ON_wString(("Dino8.AnnotationStyle." + st.name).c_str()), ON_wString(buf));
    }
  }

  // Linetype table (name -> index in the file).
  std::map<std::string, int> file_linetype_index;
  for (const Linetype& lt : doc.Linetypes()) {
    ON_Linetype olt;
    olt.SetName(ON_wString(lt.name.c_str()));
    if (lt.pattern.empty()) {
      olt.AppendSegment(ON_LinetypeSegment(1.0, ON_LinetypeSegment::eSegType::stLine));
    } else {
      for (size_t i = 0; i < lt.pattern.size(); ++i) {
        olt.AppendSegment(ON_LinetypeSegment(lt.pattern[i], i % 2 == 0 ? ON_LinetypeSegment::eSegType::stLine : ON_LinetypeSegment::eSegType::stSpace));
      }
    }
    ON_ModelComponentReference ref = model.AddModelComponent(olt, true);
    if (const ON_ModelComponent* mc = ref.ModelComponent()) file_linetype_index[lt.name] = mc->Index();
  }
  auto linetype_index = [&](const std::string& name) {
    auto it = file_linetype_index.find(name);
    return it == file_linetype_index.end() ? -1 : it->second;
  };
  model.m_properties.m_RevisionHistory.m_sCreatedBy = ON_wString(doc.Settings().author.c_str());
  model.m_properties.m_RevisionHistory.m_sLastEditedBy = ON_wString(doc.Settings().author.c_str());
  model.m_properties.m_RevisionHistory.m_revision_count += 1;
  if (!doc.GetAnimation().frames.empty()) model.SetDocumentUserString(L"Dino8.Animation", ON_wString(AnimationToString(doc.GetAnimation()).c_str()));

  // Viewport ids: model views referenced by clipping planes, one page view
  // per layout and one viewport id per detail (used for per-detail layer
  // visibility and as the detail's own viewport id).
  std::map<std::string, ON_UUID> model_view_ids;
  for (const char* n : {"Top", "Perspective", "Front", "Right"}) { ON_UUID id; ON_CreateUuid(id); model_view_ids[n] = id; }
  for (const ClippingPlane& cp : doc.ClippingPlanes()) {
    for (const std::string& v : cp.viewports) if (!model_view_ids.count(v)) { ON_UUID id; ON_CreateUuid(id); model_view_ids[v] = id; }
  }
  for (const auto& [name, id] : model_view_ids) {
    ON_3dmView v;
    v.m_name = ON_wString(name.c_str());
    v.m_view_type = ON::model_view_type;
    v.m_vp.SetViewportId(id);
    if (name == "Top") { v.m_vp.SetProjection(ON::parallel_view); v.m_vp.SetCameraLocation(ON_3dPoint(0, 0, 100)); v.m_vp.SetCameraDirection(ON_3dVector(0, 0, -1)); v.m_vp.SetCameraUp(ON_3dVector(0, 1, 0)); }
    model.m_settings.m_views.Append(v);
  }
  std::vector<ON_UUID> page_ids;
  std::vector<std::vector<ON_UUID>> detail_ids;
  for (const Layout& L : doc.Layouts()) {
    ON_UUID id; ON_CreateUuid(id);
    page_ids.push_back(id);
    ON_3dmView v;
    v.m_name = ON_wString(L.name.c_str());
    v.m_view_type = ON::page_view_type;
    v.m_page_settings.m_width_mm = L.width_mm;
    v.m_page_settings.m_height_mm = L.height_mm;
    v.m_page_settings.m_page_number = static_cast<int>(page_ids.size());
    v.m_vp.SetViewportId(id);
    v.m_vp.SetProjection(ON::parallel_view);
    v.m_vp.SetCameraLocation(ON_3dPoint(L.width_mm / 2, L.height_mm / 2, 100));
    v.m_vp.SetCameraDirection(ON_3dVector(0, 0, -1));
    v.m_vp.SetCameraUp(ON_3dVector(0, 1, 0));
    model.m_settings.m_views.Append(v);
    detail_ids.emplace_back();
    for (size_t i = 0; i < L.details.size(); ++i) { ON_UUID d; ON_CreateUuid(d); detail_ids.back().push_back(d); }
  }

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
      if (linetype_index(L.linetype) >= 0) stored->SetLinetypeIndex(linetype_index(L.linetype));
      for (size_t li = 0; li < doc.Layouts().size(); ++li) {
        const Layout& lay = doc.Layouts()[li];
        for (size_t di = 0; di < lay.details.size(); ++di) {
          const std::vector<int>& hidden = lay.details[di].hidden_layers;
          if (std::find(hidden.begin(), hidden.end(), static_cast<int>(i)) != hidden.end()) stored->SetPerViewportVisible(detail_ids[li][di], false);
        }
      }
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

  // Named construction planes.
  for (const NamedCPlane& n : doc.NamedCPlanes()) {
    ON_3dmConstructionPlane c;
    c.m_plane = ON_Plane(n.origin, n.x_axis, n.y_axis);
    c.m_name = ON_wString(n.name.c_str());
    model.m_settings.m_named_cplanes.Append(c);
  }

  int written = 0;
  std::map<ObjectId, ON_UUID> object_uuids;
  for (const SceneObject& o : doc.Objects()) {
    ON_3dmObjectAttributes attr;
    ON_CreateUuid(attr.m_uuid);
    object_uuids[o.id] = attr.m_uuid;
    attr.SetName(ON_wString(o.name.c_str()), true);
    attr.m_layer_index = file_layer_index[static_cast<size_t>(std::clamp(o.layer_index, 0, static_cast<int>(doc.Layers().size()) - 1))];
    if (!o.color_by_layer) {
      attr.SetColorSource(ON::color_from_object);
      attr.m_color = ToOnColor(o.color);
    }
    attr.SetVisible(o.visible);
    attr.SetMode(o.locked ? ON::locked_object : ON::normal_object);
    if (o.linetype != "ByLayer" && linetype_index(o.linetype) >= 0) {
      attr.SetLinetypeSource(ON::linetype_from_object);
      attr.m_linetype_index = linetype_index(o.linetype);
    }
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
  // Clipping planes.
  for (const ClippingPlane& cp : doc.ClippingPlanes()) {
    ON_Plane plane(cp.origin, cp.x_axis, cp.y_axis);
    ON_ClippingPlaneSurface* cps = new ON_ClippingPlaneSurface(plane);
    cps->SetExtents(0, ON_Interval(-cp.width / 2, cp.width / 2), true);
    cps->SetExtents(1, ON_Interval(-cp.height / 2, cp.height / 2), true);
    cps->m_clipping_plane.m_plane = plane;
    cps->m_clipping_plane.m_bEnabled = cp.enabled;
    ON_CreateUuid(cps->m_clipping_plane.m_plane_id);
    for (const std::string& v : cp.viewports) {
      auto id = model_view_ids.find(v);
      if (id != model_view_ids.end()) cps->m_clipping_plane.m_viewport_ids.AddUuid(id->second);
    }
    ON_3dmObjectAttributes attr;
    ON_CreateUuid(attr.m_uuid);
    attr.SetName(ON_wString(cp.name.c_str()), true);
    attr.m_layer_index = file_layer_index.empty() ? 0 : file_layer_index[0];
    model.AddModelGeometryComponent(cps, &attr);
  }

  // Layout details (page views were written with the settings above).
  for (size_t li = 0; li < doc.Layouts().size(); ++li) {
    const Layout& L = doc.Layouts()[li];
    for (size_t di = 0; di < L.details.size(); ++di) {
      const LayoutDetail& d = L.details[di];
      ON_DetailView* dv = new ON_DetailView();
      dv->m_view.m_name = ON_wString(d.name.c_str());
      dv->m_view.m_view_type = ON::nested_view_type;
      CameraToViewport(d.camera, dv->m_view.m_vp);
      dv->m_view.m_vp.SetViewportId(detail_ids[li][di]);
      dv->m_page_per_model_ratio = d.scale;
      ON_Polyline rect;
      rect.Append(ON_3dPoint(d.x, d.y, 0)); rect.Append(ON_3dPoint(d.x + d.width, d.y, 0));
      rect.Append(ON_3dPoint(d.x + d.width, d.y + d.height, 0)); rect.Append(ON_3dPoint(d.x, d.y + d.height, 0)); rect.Append(ON_3dPoint(d.x, d.y, 0));
      ON_PolylineCurve pc(rect);
      pc.GetNurbForm(dv->m_boundary);
      ON_3dmObjectAttributes attr;
      ON_CreateUuid(attr.m_uuid);
      attr.SetName(ON_wString(d.name.c_str()), true);
      attr.m_viewport_id = page_ids[li];
      attr.m_layer_index = file_layer_index.empty() ? 0 : file_layer_index[0];
      attr.SetUserString(L"Dino8.DetailLocked", d.locked ? L"1" : L"0");
      attr.SetUserString(L"Dino8.DetailMode", ON_wString(d.display_mode.c_str()));
      attr.SetUserString(L"Dino8.DetailView", ON_wString(d.standard_view.c_str()));
      std::string hidden;
      for (ObjectId id : d.hidden_objects) {
        auto u = object_uuids.find(id);
        if (u != object_uuids.end()) hidden += UuidString(u->second) + ";";
      }
      if (!hidden.empty()) attr.SetUserString(L"Dino8.HiddenObjects", ON_wString(hidden.c_str()));
      model.AddModelGeometryComponent(dv, &attr);
    }
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
