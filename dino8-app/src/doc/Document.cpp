#include "doc/Document.h"

#include <algorithm>

namespace dino8::app {

Document::Document() { Clear(); }

void Document::Clear() {
  objects_.clear();
  layers_.clear();
  Layer def;
  def.name = "Default";
  layers_.push_back(def);
  current_layer_ = 0;
  groups_.clear();
  materials_.clear();
  lights_.clear();
  render_ = RenderSettings{};
  next_light_id_ = 1;
  named_views_.clear();
  user_text_.clear();
  notes_.clear();
  settings_ = DocumentSettings{};
  path_.clear();
  modified_ = false;
  next_id_ = 1;
  next_group_id_ = 1;
  undo_.clear();
  redo_.clear();
  ++revision_;
}

ObjectId Document::Add(SceneObject object) {
  object.id = next_id_++;
  if (object.layer_index < 0 || object.layer_index >= static_cast<int>(layers_.size())) {
    object.layer_index = current_layer_;
  }
  objects_.push_back(std::move(object));
  Touch();
  return objects_.back().id;
}

bool Document::Remove(ObjectId id) {
  const auto it = std::find_if(objects_.begin(), objects_.end(),
                               [id](const SceneObject& o) { return o.id == id; });
  if (it == objects_.end()) return false;
  objects_.erase(it);
  Touch();
  return true;
}

SceneObject* Document::Find(ObjectId id) {
  for (SceneObject& o : objects_) {
    if (o.id == id) return &o;
  }
  return nullptr;
}

const SceneObject* Document::Find(ObjectId id) const {
  for (const SceneObject& o : objects_) {
    if (o.id == id) return &o;
  }
  return nullptr;
}

std::vector<ObjectId> Document::SelectedIds() const {
  std::vector<ObjectId> ids;
  for (const SceneObject& o : objects_) {
    if (o.selected) ids.push_back(o.id);
  }
  return ids;
}

size_t Document::SelectedCount() const {
  return static_cast<size_t>(std::count_if(objects_.begin(), objects_.end(),
                                           [](const SceneObject& o) { return o.selected; }));
}

void Document::Select(ObjectId id, bool selected) {
  if (SceneObject* o = Find(id)) {
    if (selected && (IsObjectLocked(*o) || !IsObjectVisible(*o))) return;
    o->selected = selected;
  }
}

void Document::SelectAll() {
  for (SceneObject& o : objects_) {
    o.selected = IsObjectVisible(o) && !IsObjectLocked(o);
  }
}

void Document::SelectNone() {
  for (SceneObject& o : objects_) o.selected = false;
  for (Light& l : lights_) l.selected = false;
}

void Document::InvertSelection() {
  for (SceneObject& o : objects_) {
    if (IsObjectVisible(o) && !IsObjectLocked(o)) o.selected = !o.selected;
  }
}

void Document::SelectWhere(const std::function<bool(const SceneObject&)>& predicate, bool add) {
  for (SceneObject& o : objects_) {
    const bool eligible = IsObjectVisible(o) && !IsObjectLocked(o);
    const bool match = eligible && predicate(o);
    if (add) {
      if (match) o.selected = true;
    } else {
      o.selected = match;
    }
  }
}

bool Document::IsObjectVisible(const SceneObject& o) const {
  if (!o.visible) return false;
  int layer = o.layer_index;
  while (layer >= 0 && layer < static_cast<int>(layers_.size())) {
    if (!layers_[layer].visible) return false;
    layer = layers_[layer].parent;
  }
  return true;
}

bool Document::IsObjectLocked(const SceneObject& o) const {
  if (o.locked) return true;
  int layer = o.layer_index;
  while (layer >= 0 && layer < static_cast<int>(layers_.size())) {
    if (layers_[layer].locked) return true;
    layer = layers_[layer].parent;
  }
  return false;
}

const char* LightTypeName(LightType t) {
  switch (t) {
    case LightType::Point: return "Point";
    case LightType::Spot: return "Spot";
    case LightType::Directional: return "Directional";
    case LightType::Rectangular: return "Rectangular";
    case LightType::Linear: return "Linear";
  }
  return "Point";
}

Material* Document::FindMaterial(const std::string& name) {
  for (Material& m : materials_) if (m.name == name) return &m;
  return nullptr;
}

const Material* Document::FindMaterial(const std::string& name) const {
  for (const Material& m : materials_) if (m.name == name) return &m;
  return nullptr;
}

std::string Document::AddMaterial(Material m) {
  if (m.name.empty()) m.name = "Material";
  if (Material* existing = FindMaterial(m.name)) {
    *existing = m;
  } else {
    materials_.push_back(m);
  }
  Touch();
  return m.name;
}

bool Document::RemoveMaterial(const std::string& name) {
  const auto it = std::find_if(materials_.begin(), materials_.end(), [&](const Material& m) { return m.name == name; });
  if (it == materials_.end()) return false;
  materials_.erase(it);
  for (SceneObject& o : objects_) if (o.material_name == name) o.material_name.clear();
  for (Layer& l : layers_) if (l.material == name) l.material.clear();
  Touch();
  return true;
}

Material Document::MaterialFor(const SceneObject& o) const {
  if (!o.material_name.empty()) {
    if (const Material* m = FindMaterial(o.material_name)) return *m;
  }
  if (o.layer_index >= 0 && o.layer_index < static_cast<int>(layers_.size()) && !layers_[static_cast<size_t>(o.layer_index)].material.empty()) {
    if (const Material* m = FindMaterial(layers_[static_cast<size_t>(o.layer_index)].material)) return *m;
  }
  Material m;
  m.name.clear();
  m.diffuse = EffectiveColor(o);
  // Rhino's default layer colour is black; a black render material looks
  // like a hole, so plain objects get the neutral default material.
  if (m.diffuse.r + m.diffuse.g + m.diffuse.b < 0.05f && o.color_by_layer) m.diffuse = Color::FromBytes(200, 200, 200);
  return m;
}

int Document::AddLight(Light light) {
  light.id = next_light_id_++;
  if (light.name.empty()) light.name = std::string(LightTypeName(light.type)) + " light " + std::to_string(light.id);
  lights_.push_back(light);
  Touch();
  return lights_.back().id;
}

bool Document::RemoveLight(int id) {
  const auto it = std::find_if(lights_.begin(), lights_.end(), [id](const Light& l) { return l.id == id; });
  if (it == lights_.end()) return false;
  lights_.erase(it);
  Touch();
  return true;
}

Light* Document::FindLight(int id) {
  for (Light& l : lights_) if (l.id == id) return &l;
  return nullptr;
}

Color Document::EffectiveColor(const SceneObject& o) const {
  if (!o.color_by_layer) return o.color;
  if (o.layer_index >= 0 && o.layer_index < static_cast<int>(layers_.size())) {
    return layers_[o.layer_index].color;
  }
  return Color::FromBytes(0, 0, 0);
}

int Document::AddLayer(const std::string& name, Color color, int parent) {
  Layer layer;
  layer.name = name;
  layer.color = color;
  layer.parent = parent;
  layers_.push_back(layer);
  Touch();
  return static_cast<int>(layers_.size()) - 1;
}

bool Document::RemoveLayer(int index) {
  if (index < 0 || index >= static_cast<int>(layers_.size())) return false;
  if (layers_.size() == 1) return false;  // always keep one layer
  if (index == current_layer_) return false;
  for (const SceneObject& o : objects_) {
    if (o.layer_index == index) return false;
  }
  for (const Layer& l : layers_) {
    if (l.parent == index) return false;
  }
  layers_.erase(layers_.begin() + index);
  for (SceneObject& o : objects_) {
    if (o.layer_index > index) --o.layer_index;
  }
  for (Layer& l : layers_) {
    if (l.parent > index) --l.parent;
  }
  if (current_layer_ > index) --current_layer_;
  Touch();
  return true;
}

int Document::FindLayer(const std::string& name) const {
  for (size_t i = 0; i < layers_.size(); ++i) {
    if (layers_[i].name == name || LayerFullPath(static_cast<int>(i)) == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void Document::SetCurrentLayer(int index) {
  if (index >= 0 && index < static_cast<int>(layers_.size())) current_layer_ = index;
}

std::string Document::LayerFullPath(int index) const {
  if (index < 0 || index >= static_cast<int>(layers_.size())) return "";
  std::string path = layers_[index].name;
  int parent = layers_[index].parent;
  int guard = 0;
  while (parent >= 0 && parent < static_cast<int>(layers_.size()) && guard++ < 64) {
    path = layers_[parent].name + "::" + path;
    parent = layers_[parent].parent;
  }
  return path;
}

int Document::CreateGroup(const std::vector<ObjectId>& ids, const std::string& name) {
  Group g;
  g.id = next_group_id_++;
  g.name = name.empty() ? "Group" + std::to_string(g.id) : name;
  groups_.push_back(g);
  for (ObjectId id : ids) {
    if (SceneObject* o = Find(id)) o->group_id = g.id;
  }
  Touch();
  return g.id;
}

void Document::Ungroup(const std::vector<ObjectId>& ids) {
  for (ObjectId id : ids) {
    if (SceneObject* o = Find(id)) o->group_id = -1;
  }
  groups_.erase(std::remove_if(groups_.begin(), groups_.end(),
                               [this](const Group& g) { return GroupMembers(g.id).empty(); }),
                groups_.end());
  Touch();
}

std::vector<ObjectId> Document::GroupMembers(int group_id) const {
  std::vector<ObjectId> ids;
  for (const SceneObject& o : objects_) {
    if (o.group_id == group_id) ids.push_back(o.id);
  }
  return ids;
}

Document::Snapshot Document::Capture(const std::string& label) const {
  Snapshot s;
  s.label = label;
  s.objects = objects_;
  s.layers = layers_;
  s.current_layer = current_layer_;
  s.groups = groups_;
  s.materials = materials_;
  s.lights = lights_;
  s.next_id = next_id_;
  s.next_group_id = next_group_id_;
  s.next_light_id = next_light_id_;
  return s;
}

void Document::Restore(const Snapshot& s) {
  objects_ = s.objects;
  layers_ = s.layers;
  current_layer_ = s.current_layer;
  groups_ = s.groups;
  materials_ = s.materials;
  lights_ = s.lights;
  next_id_ = s.next_id;
  next_group_id_ = s.next_group_id;
  next_light_id_ = s.next_light_id;
  for (SceneObject& o : objects_) o.InvalidateDisplay();
  Touch();
}

void Document::BeginChange(const std::string& label) {
  undo_.push_back(Capture(label));
  if (undo_.size() > max_undo_) undo_.erase(undo_.begin());
  redo_.clear();
}

bool Document::Undo() {
  if (undo_.empty()) return false;
  Snapshot current = Capture(undo_.back().label);
  redo_.push_back(current);
  Restore(undo_.back());
  undo_.pop_back();
  return true;
}

bool Document::Redo() {
  if (redo_.empty()) return false;
  undo_.push_back(Capture(redo_.back().label));
  Restore(redo_.back());
  redo_.pop_back();
  return true;
}

std::vector<std::string> Document::UndoLabels() const {
  std::vector<std::string> labels;
  for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) labels.push_back(it->label);
  return labels;
}

std::vector<std::string> Document::RedoLabels() const {
  std::vector<std::string> labels;
  for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) labels.push_back(it->label);
  return labels;
}

void Document::ClearUndo() {
  undo_.clear();
  redo_.clear();
}

bool Document::BoundingBoxOf(const std::vector<ObjectId>& ids, kernel::BoundingBox& out) const {
  bool has = false;
  for (ObjectId id : ids) {
    const SceneObject* o = Find(id);
    if (!o) continue;
    const kernel::BoundingBox b = o->BoundingBox();
    if (!has) {
      out = b;
      has = true;
    } else {
      out.min.x = std::min(out.min.x, b.min.x);
      out.min.y = std::min(out.min.y, b.min.y);
      out.min.z = std::min(out.min.z, b.min.z);
      out.max.x = std::max(out.max.x, b.max.x);
      out.max.y = std::max(out.max.y, b.max.y);
      out.max.z = std::max(out.max.z, b.max.z);
    }
  }
  return has;
}

bool Document::VisibleBoundingBox(kernel::BoundingBox& out) const {
  std::vector<ObjectId> ids;
  for (const SceneObject& o : objects_) {
    if (IsObjectVisible(o)) ids.push_back(o.id);
  }
  return BoundingBoxOf(ids, out);
}

}  // namespace dino8::app
