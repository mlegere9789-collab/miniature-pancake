#include "io/File3dm.h"

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
      // Attribute user strings.
      ON_ClassArray<ON_UserString> strings;
      attr->GetUserStrings(strings);
      for (int i = 0; i < strings.Count(); ++i) {
        obj.user_text[FromWide(strings[i].m_key)] = FromWide(strings[i].m_string_value);
      }
    }
    doc.Add(std::move(obj));
  }

  // Document notes and user text.
  doc.Notes() = FromWide(model.m_properties.m_Notes.m_notes);
  {
    ON_ClassArray<ON_UserString> strings;
    model.GetUserStrings(strings);
    for (int i = 0; i < strings.Count(); ++i) {
      doc.UserText()[FromWide(strings[i].m_key)] = FromWide(strings[i].m_string_value);
    }
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
    if (v.m_vp.GetFrustumLeftRightBottomTop(&l, &r, &b, &t) && !nv.camera.perspective) nv.camera.ortho_height = t - b;
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
  model.m_properties.m_Notes.m_notes = ON_wString(doc.Notes().c_str());
  model.m_properties.m_Notes.m_bVisible = !doc.Notes().empty();
  for (const auto& [k, v] : const_cast<Document&>(doc).UserText()) {
    model.SetUserString(ON_wString(k.c_str()), ON_wString(v.c_str()));
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
      if (L.parent >= 0 && static_cast<size_t>(L.parent) < i) {
        ON_ModelComponentReference pref = model.LayerFromIndex(file_layer_index[static_cast<size_t>(L.parent)]);
        if (const ON_Layer* pl = ON_Layer::Cast(pref.ModelComponent())) stored->SetParentLayerId(pl->Id());
      }
    }
  }
  if (!doc.Layers().empty()) model.m_settings.m_current_layer_index = file_layer_index[static_cast<size_t>(std::max(0, doc.CurrentLayer()))];

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
      meshes.push_back(o.brep->TessellateToClosedMeshAdaptive(0.01));
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
