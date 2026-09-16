#include "doc/Document.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <unordered_set>

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
  linetypes_ = DefaultLinetypes();
  annotation_styles_ = {AnnotationStyle{}};
  named_cplanes_.clear();
  guides_.clear();
  clipping_planes_.clear();
  next_clipping_plane_id_ = 1;
  layouts_.clear();
  animation_ = Animation{};
  reference_models_.clear();
  hole_features_.clear();
  user_text_.clear();
  notes_.clear();
  settings_ = DocumentSettings{};
  path_.clear();
  modified_ = false;
  next_id_ = 1;
  next_group_id_ = 1;
  pending_ = PendingChange{};
  ops_since_checkpoint_ = 0;
  undo_.clear();
  redo_.clear();
  named_snapshots_.clear();
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
  hole_features_.erase(id);
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
  prev_selection_ = SelectedIds();
  for (SceneObject& o : objects_) {
    o.selected = IsObjectVisible(o) && !IsObjectLocked(o);
  }
}

void Document::SelectNone() {
  prev_selection_ = SelectedIds();
  for (SceneObject& o : objects_) o.selected = false;
  for (Light& l : lights_) l.selected = false;
}

void Document::InvertSelection() {
  prev_selection_ = SelectedIds();
  for (SceneObject& o : objects_) {
    if (IsObjectVisible(o) && !IsObjectLocked(o)) o.selected = !o.selected;
  }
}

bool Document::RestorePreviousSelection() {
  const std::vector<ObjectId> cur = SelectedIds();
  if (prev_selection_.empty() && cur.empty()) return false;
  for (SceneObject& o : objects_) o.selected = false;
  for (ObjectId id : prev_selection_) {
    SceneObject* o = Find(id);
    if (o && IsObjectVisible(*o) && !IsObjectLocked(*o)) o->selected = true;
  }
  prev_selection_ = cur;
  return true;
}

void Document::SelectWhere(const std::function<bool(const SceneObject&)>& predicate, bool add) {
  prev_selection_ = SelectedIds();
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

std::vector<Linetype> Document::DefaultLinetypes() {
  return {{"Continuous", {}},
          {"Dashed", {5, 2}},
          {"Dots", {0.5, 1.5}},
          {"DashDot", {5, 2, 0.5, 2}},
          {"Center", {10, 2, 2, 2}},
          {"Hidden", {2, 2}},
          {"Border", {5, 2, 5, 2, 0.5, 2}}};
}

const Linetype* Document::FindLinetype(const std::string& name) const {
  for (const Linetype& l : linetypes_) if (l.name == name) return &l;
  return nullptr;
}

Linetype* Document::FindLinetype(const std::string& name) {
  for (Linetype& l : linetypes_) if (l.name == name) return &l;
  return nullptr;
}

int Document::SetLinetype(const std::string& name, const std::vector<double>& pattern) {
  for (size_t i = 0; i < linetypes_.size(); ++i) {
    if (linetypes_[i].name == name) { linetypes_[i].pattern = pattern; Touch(); return static_cast<int>(i); }
  }
  linetypes_.push_back({name, pattern});
  Touch();
  return static_cast<int>(linetypes_.size()) - 1;
}

std::string Document::EffectiveLinetype(const SceneObject& o) const {
  if (!o.linetype.empty() && o.linetype != "ByLayer") return o.linetype;
  if (o.layer_index >= 0 && o.layer_index < static_cast<int>(layers_.size())) {
    const std::string& l = layers_[static_cast<size_t>(o.layer_index)].linetype;
    if (!l.empty()) return l;
  }
  return "Continuous";
}

std::vector<double> Document::EffectiveDashes(const SceneObject& o) const {
  if (!settings_.linetype_display || o.kind != ObjectKind::Curve) return {};
  const Linetype* lt = FindLinetype(EffectiveLinetype(o));
  if (!lt || lt->pattern.empty()) return {};
  std::vector<double> out;
  const double s = settings_.linetype_scale > 0 ? settings_.linetype_scale : 1.0;
  for (double d : lt->pattern) out.push_back(d * s);
  return out;
}

AnnotationStyle* Document::FindAnnotationStyle(const std::string& name) {
  for (AnnotationStyle& a : annotation_styles_) if (a.name == name) return &a;
  return nullptr;
}

const AnnotationStyle& Document::CurrentAnnotationStyle() const {
  for (const AnnotationStyle& a : annotation_styles_) if (a.name == settings_.annotation_style) return a;
  static const AnnotationStyle kDefault;
  return annotation_styles_.empty() ? kDefault : annotation_styles_.front();
}

NamedCPlane* Document::FindNamedCPlane(const std::string& name) {
  for (NamedCPlane& c : named_cplanes_) if (c.name == name) return &c;
  return nullptr;
}

int Document::AddClippingPlane(ClippingPlane plane) {
  plane.id = next_clipping_plane_id_++;
  if (plane.name.empty()) plane.name = "Clipping Plane " + std::to_string(plane.id);
  clipping_planes_.push_back(std::move(plane));
  Touch();
  return clipping_planes_.back().id;
}

ClippingPlane* Document::FindClippingPlane(int id) {
  for (ClippingPlane& c : clipping_planes_) if (c.id == id) return &c;
  return nullptr;
}

Layout* Document::FindLayout(const std::string& name) {
  for (Layout& l : layouts_) if (l.name == name) return &l;
  return nullptr;
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
  s.clipping_planes = clipping_planes_;
  s.layouts = layouts_;
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
  clipping_planes_ = s.clipping_planes;
  layouts_ = s.layouts;
  next_id_ = s.next_id;
  next_group_id_ = s.next_group_id;
  next_light_id_ = s.next_light_id;
  for (SceneObject& o : objects_) o.InvalidateDisplay();
  Touch();
}

bool Document::SaveNamedSnapshot(const std::string& name) {
  Snapshot s = Capture(name);
  for (auto& [n, snap] : named_snapshots_) {
    if (n == name) { snap = std::move(s); return true; }
  }
  named_snapshots_.emplace_back(name, std::move(s));
  return true;
}

bool Document::RestoreNamedSnapshot(const std::string& name) {
  for (const auto& [n, snap] : named_snapshots_) {
    if (n != name) continue;
    Restore(snap);
    return true;
  }
  return false;
}

bool Document::DeleteNamedSnapshot(const std::string& name) {
  const auto it = std::find_if(named_snapshots_.begin(), named_snapshots_.end(),
                               [&](const auto& p) { return p.first == name; });
  if (it == named_snapshots_.end()) return false;
  named_snapshots_.erase(it);
  return true;
}

std::vector<std::string> Document::NamedSnapshotNames() const {
  std::vector<std::string> names;
  names.reserve(named_snapshots_.size());
  for (const auto& [n, snap] : named_snapshots_) names.push_back(n);
  return names;
}

// ---- diff-based undo/redo history ----------------------------------------
//
// See the StateDelta/HistoryEntry/PendingChange comments in Document.h for
// the overall design. Summary of the four pieces below:
//  - BeginChange/BeginChangeForObjects: finalize whatever edit is already
//    pending (it has, by now, finished mutating - see FinalizePending),
//    then record the *new* pending pre-edit state and clear redo_, exactly
//    matching the old immediate-push contract from the outside (CanUndo()/
//    UndoLabels() below report the still-pending entry as already present,
//    just like the old code's synchronous push did).
//  - FinalizePending: turns the recorded pre-edit state plus the *current*
//    (post-edit) live state into a real StateDelta and pushes it.
//  - Undo/Redo: pop one entry, lazily materialize its "after" objects the
//    first time it's undone, apply the appropriate side, push it onto the
//    other stack. No full-document capture anywhere in this path.
//  - ApplyDelta/ApplyObjectDelta: mutate live state to match one side of a
//    StateDelta.

void Document::BeginChange(const std::string& label) {
  FinalizePending();
  pending_.active = true;
  pending_.fast_path = false;
  pending_.label = label;
  pending_.layers = layers_;
  pending_.current_layer = current_layer_;
  pending_.groups = groups_;
  pending_.materials = materials_;
  pending_.lights = lights_;
  pending_.clipping_planes = clipping_planes_;
  pending_.layouts = layouts_;
  pending_.next_id = next_id_;
  pending_.next_group_id = next_group_id_;
  pending_.next_light_id = next_light_id_;
  pending_.objects = objects_;  // O(document) - same cost the old Capture() paid at BeginChange time
  pending_.fast_path_before.clear();
  redo_.clear();
}

void Document::BeginChangeForObjects(const std::string& label, const std::vector<ObjectId>& ids) {
  FinalizePending();
  pending_.active = true;
  pending_.fast_path = true;
  pending_.label = label;
  pending_.layers = layers_;
  pending_.current_layer = current_layer_;
  pending_.groups = groups_;
  pending_.materials = materials_;
  pending_.lights = lights_;
  pending_.clipping_planes = clipping_planes_;
  pending_.layouts = layouts_;
  pending_.next_id = next_id_;
  pending_.next_group_id = next_group_id_;
  pending_.next_light_id = next_light_id_;
  pending_.objects.clear();
  pending_.fast_path_before.clear();
  pending_.fast_path_before.reserve(ids.size());
  for (ObjectId id : ids) {
    if (const SceneObject* o = Find(id)) pending_.fast_path_before.push_back(*o);
  }
  redo_.clear();
}

void Document::FinalizePending() {
  if (!pending_.active) return;
  StateDelta d;
  d.label = pending_.label;
  d.layers_before = std::move(pending_.layers);
  d.layers_after = layers_;
  d.current_layer_before = pending_.current_layer;
  d.current_layer_after = current_layer_;
  d.groups_before = std::move(pending_.groups);
  d.groups_after = groups_;
  d.materials_before = std::move(pending_.materials);
  d.materials_after = materials_;
  d.lights_before = std::move(pending_.lights);
  d.lights_after = lights_;
  d.clipping_planes_before = std::move(pending_.clipping_planes);
  d.clipping_planes_after = clipping_planes_;
  d.layouts_before = std::move(pending_.layouts);
  d.layouts_after = layouts_;
  d.next_id_before = pending_.next_id;
  d.next_id_after = next_id_;
  d.next_group_id_before = pending_.next_group_id;
  d.next_group_id_after = next_group_id_;
  d.next_light_id_before = pending_.next_light_id;
  d.next_light_id_after = next_light_id_;

  if (pending_.fast_path) {
    std::unordered_set<ObjectId> live_ids;
    live_ids.reserve(objects_.size());
    for (const SceneObject& o : objects_) live_ids.insert(o.id);
    for (SceneObject& before : pending_.fast_path_before) {
      if (live_ids.count(before.id)) {
        d.modified_before.push_back(std::move(before));
      } else {
        // The command removed this object too, outside
        // BeginChangeForObjects' "modify only" contract. Fall back to
        // treating it as removed (reinserted at the end on Undo, since its
        // original position isn't tracked here) rather than silently
        // dropping the change - not exercised by any call site this
        // feature currently migrates.
        d.removed.push_back({std::move(before), objects_.size()});
      }
    }
  } else {
    std::unordered_set<ObjectId> after_ids;
    after_ids.reserve(objects_.size());
    for (const SceneObject& o : objects_) after_ids.insert(o.id);
    std::unordered_set<ObjectId> before_ids;
    before_ids.reserve(pending_.objects.size());
    for (size_t i = 0; i < pending_.objects.size(); ++i) {
      SceneObject& before = pending_.objects[i];
      before_ids.insert(before.id);
      if (after_ids.count(before.id)) {
        d.modified_before.push_back(std::move(before));
      } else {
        d.removed.push_back({std::move(before), i});
      }
    }
    for (const SceneObject& o : objects_) {
      if (!before_ids.count(o.id)) d.added.push_back(o);
    }
  }

  pending_.active = false;
  pending_.objects.clear();
  pending_.objects.shrink_to_fit();
  pending_.fast_path_before.clear();

  std::shared_ptr<const Snapshot> checkpoint;
  if (++ops_since_checkpoint_ >= kCheckpointInterval) {
    ops_since_checkpoint_ = 0;
    checkpoint = std::make_shared<Snapshot>(Capture(d.label));
  }
  undo_.push_back(HistoryEntry{std::move(d), std::move(checkpoint)});
  if (undo_.size() > max_undo_) undo_.erase(undo_.begin());
}

void Document::ApplyObjectDelta(const StateDelta& d, bool undo) {
  std::unordered_map<ObjectId, size_t> index_of;
  index_of.reserve(objects_.size());
  for (size_t i = 0; i < objects_.size(); ++i) index_of[objects_[i].id] = i;

  const std::vector<SceneObject>& modified_source = undo ? d.modified_before : d.modified_after;
  for (const SceneObject& src : modified_source) {
    const auto it = index_of.find(src.id);
    if (it != index_of.end()) objects_[it->second] = src;
  }

  if (undo) {
    // `added` objects were appended at the tail when this delta was
    // recorded (Add() always appends; nothing in the codebase reorders
    // objects_). Removing them by id (via the index map) rather than
    // assuming tail position keeps this correct even if that changes,
    // while remaining O(added.size()) amortized. Reverse order so earlier
    // erases don't invalidate the indices of later ones.
    for (auto it = d.added.rbegin(); it != d.added.rend(); ++it) {
      const auto pos = index_of.find(it->id);
      if (pos != index_of.end()) {
        objects_.erase(objects_.begin() + static_cast<std::ptrdiff_t>(pos->second));
        index_of.erase(pos);
      }
    }
    // Reinsert removed objects in ascending original-index order, each at
    // min(index_before, current size). This exactly reconstructs the
    // original ordering: by the time object k is reinserted, every
    // already-reinserted object had a smaller original index, so it was
    // inserted at or before this position - the same reasoning as
    // reconstructing a vector from a set of (index, value) pairs by
    // inserting them in ascending index order.
    std::vector<const StateDelta::RemovedObject*> order;
    order.reserve(d.removed.size());
    for (const auto& r : d.removed) order.push_back(&r);
    std::sort(order.begin(), order.end(),
              [](const auto* a, const auto* b) { return a->index_before < b->index_before; });
    for (const auto* r : order) {
      const size_t idx = std::min(r->index_before, objects_.size());
      objects_.insert(objects_.begin() + static_cast<std::ptrdiff_t>(idx), r->object);
    }
  } else {
    for (const auto& r : d.removed) {
      const auto pos = index_of.find(r.object.id);
      if (pos != index_of.end()) objects_.erase(objects_.begin() + static_cast<std::ptrdiff_t>(pos->second));
    }
    for (const SceneObject& o : d.added) objects_.push_back(o);
  }
  for (SceneObject& o : objects_) o.InvalidateDisplay();  // matches Restore()'s blanket invalidate
}

void Document::ApplyDelta(const StateDelta& d, bool undo) {
  ApplyObjectDelta(d, undo);
  layers_ = undo ? d.layers_before : d.layers_after;
  current_layer_ = undo ? d.current_layer_before : d.current_layer_after;
  groups_ = undo ? d.groups_before : d.groups_after;
  materials_ = undo ? d.materials_before : d.materials_after;
  lights_ = undo ? d.lights_before : d.lights_after;
  clipping_planes_ = undo ? d.clipping_planes_before : d.clipping_planes_after;
  layouts_ = undo ? d.layouts_before : d.layouts_after;
  next_id_ = undo ? d.next_id_before : d.next_id_after;
  next_group_id_ = undo ? d.next_group_id_before : d.next_group_id_after;
  next_light_id_ = undo ? d.next_light_id_before : d.next_light_id_after;
  Touch();
}

bool Document::Undo() {
  FinalizePending();
  if (undo_.empty()) return false;
  HistoryEntry entry = std::move(undo_.back());
  undo_.pop_back();
  if (!entry.delta.modified_after_ready) {
    // First time this entry is undone: materialize its "after" objects
    // from the *current* live state (which, at this exact point, is
    // exactly the post-edit state this delta recorded, since nothing has
    // touched the document between recording and this Undo). O(candidate
    // count) once, then cached on the entry for every future Undo/Redo
    // toggle - see the StateDelta comment in Document.h for why this is
    // deferred rather than computed at record time.
    std::unordered_map<ObjectId, const SceneObject*> live;
    live.reserve(objects_.size());
    for (const SceneObject& o : objects_) live[o.id] = &o;
    entry.delta.modified_after.clear();
    entry.delta.modified_after.reserve(entry.delta.modified_before.size());
    for (const SceneObject& before : entry.delta.modified_before) {
      const auto it = live.find(before.id);
      entry.delta.modified_after.push_back(it != live.end() ? *it->second : before);
    }
    entry.delta.modified_after_ready = true;
  }
  if (entry.checkpoint) {
    // Cheap structural self-check against the periodic full checkpoint -
    // see the HistoryEntry comment in Document.h. Compiled out entirely in
    // release builds; not a substitute for the delta application below.
    assert(entry.checkpoint->next_id == entry.delta.next_id_after);
    assert(entry.checkpoint->next_group_id == entry.delta.next_group_id_after);
    assert(entry.checkpoint->next_light_id == entry.delta.next_light_id_after);
  }
  ApplyDelta(entry.delta, /*undo=*/true);
  redo_.push_back(std::move(entry));
  return true;
}

bool Document::Redo() {
  FinalizePending();
  if (redo_.empty()) return false;
  HistoryEntry entry = std::move(redo_.back());
  redo_.pop_back();
  // modified_after is always ready here: the only way an entry reaches
  // redo_ is via Undo() above, which already materialized it.
  ApplyDelta(entry.delta, /*undo=*/false);
  undo_.push_back(std::move(entry));
  return true;
}

std::vector<std::string> Document::UndoLabels() const {
  std::vector<std::string> labels;
  if (pending_.active) labels.push_back(pending_.label);
  for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) labels.push_back(it->delta.label);
  return labels;
}

std::vector<std::string> Document::RedoLabels() const {
  std::vector<std::string> labels;
  for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) labels.push_back(it->delta.label);
  return labels;
}

void Document::ClearUndo() {
  pending_ = PendingChange{};
  ops_since_checkpoint_ = 0;
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
