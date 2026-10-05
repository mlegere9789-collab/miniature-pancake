#include "doc/BlockInstances.h"

#include <algorithm>
#include <sstream>

#include "util/json_mini.h"

namespace dino8::app {

namespace {
constexpr const char* kUserTextKey = "dino8.block_instances";

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
        else out += c;
    }
  }
  return out;
}

std::vector<std::string> SplitComma(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string tok;
  while (std::getline(in, tok, ',')) if (!tok.empty()) out.push_back(tok);
  return out;
}
}  // namespace

bool ObjectVisibleInState(const SceneObject& def_object, const std::string& state) {
  auto it = def_object.user_text.find(kBlockVisStatesKey);
  if (it == def_object.user_text.end() || it->second.empty()) return true;  // untagged: visible always
  if (state.empty()) return true;  // no active state to filter by: show everything
  for (const std::string& s : SplitComma(it->second)) if (s == state) return true;
  return false;
}

std::vector<BlockInstance> LoadBlockInstances(const Document& doc) {
  std::vector<BlockInstance> out;
  auto it = const_cast<Document&>(doc).UserText().find(kUserTextKey);
  if (it == const_cast<Document&>(doc).UserText().end() || it->second.empty()) return out;
  json::Value root;
  std::string err;
  if (!json::Parse(it->second, root, err) || !root.IsArray()) return out;
  for (size_t i = 0; i < root.Size(); ++i) {
    const json::Value& v = root[i];
    BlockInstance b;
    b.group = static_cast<int>(v["group"].number);
    b.block = v["block"].AsString();
    b.state = v["state"].AsString();
    b.insert = kernel::Point3d(v["ix"].number, v["iy"].number, v["iz"].number);
    b.flipped = v["flip"].number != 0;
    const json::Value& arr = v["array"];
    // Clamp before the cast: a JSON number past INT_MAX is undefined
    // behavior to static_cast straight to int, and PlaceFiltered's own cap
    // (kMaxBlockArrayCount) only helps once this is a well-defined int.
    b.array_count = arr.number > 0 ? static_cast<int>(std::min(arr.number, 1e9)) : 1;
    b.lookup_key = v["lookup"].AsString();
    b.stretch_offset = v["stretch"].number;
    const json::Value& objs = v["objects"];
    for (size_t j = 0; j < objs.Size(); ++j) b.objects.push_back(static_cast<ObjectId>(objs[j].number));
    out.push_back(std::move(b));
  }
  return out;
}

void SaveBlockInstances(Document& doc, const std::vector<BlockInstance>& list) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < list.size(); ++i) {
    const BlockInstance& b = list[i];
    out << (i ? "," : "") << "{\"group\":" << b.group << ",\"block\":\"" << JsonEscape(b.block) << "\""
        << ",\"state\":\"" << JsonEscape(b.state) << "\""
        << ",\"ix\":" << b.insert.x << ",\"iy\":" << b.insert.y << ",\"iz\":" << b.insert.z
        << ",\"flip\":" << (b.flipped ? 1 : 0) << ",\"array\":" << b.array_count
        << ",\"lookup\":\"" << JsonEscape(b.lookup_key) << "\",\"stretch\":" << b.stretch_offset << ",\"objects\":[";
    for (size_t j = 0; j < b.objects.size(); ++j) out << (j ? "," : "") << b.objects[j];
    out << "]}";
  }
  out << "]";
  doc.UserText()[kUserTextKey] = out.str();
}

bool FindBlockInstanceByGroup(const Document& doc, int group, BlockInstance& out) {
  for (BlockInstance& b : LoadBlockInstances(doc)) if (b.group == group) { out = b; return true; }
  return false;
}

bool FindBlockInstanceByObject(const Document& doc, ObjectId object_id, BlockInstance& out) {
  for (BlockInstance& b : LoadBlockInstances(doc)) {
    if (std::find(b.objects.begin(), b.objects.end(), object_id) != b.objects.end()) { out = b; return true; }
  }
  return false;
}

namespace {
// Builds the objects for `state` at `at`, unconditionally (no BlockInstance
// bookkeeping) - shared by InstantiateDynamicBlock and RebuildBlockInstance.
// `flipped` mirrors the definition's geometry about a vertical world plane
// (normal +X) through `def.base` before the insert-point translation, so a
// flipped instance's base point still lands exactly at `at` like an
// unflipped one - only left/right of the base is mirrored, not the
// instance's position. `array_count` (clamped to at least 1) then repeats
// that placed copy along `def.array_axis`, `def.array_spacing` apart, in
// world space, after the flip/insert transform - the same order flip
// already establishes ("mirror in place, then move"), array simply adds
// "then repeat" after it. A definition with array_spacing == 0 (the array
// parameter was never configured via BlockSetArraySpacing) always places
// exactly one copy, regardless of `array_count`, so a plain block or one
// that only uses states/flip is unaffected.
// array_count can come straight from a loaded document's embedded JSON
// (LoadBlockInstances), so it isn't trustworthy: a huge value would make
// the loop below instantiate and doc.Add() billions of objects just from
// opening a file. Kept well below Document::Find/Remove's own O(document
// size) cost (RebuildBlockInstance calls both once per array copy when
// rebuilding/clearing an instance), so even the capped worst case stays
// O(count^2) over a small count instead of a huge one - this is still far
// above any plausible real use (a bolt-pattern/rebar array).
constexpr int kMaxBlockArrayCount = 2000;

// Stretch parameter: moves only the part of `c`'s geometry on the far side
// of the definition's stretch-frame anchor plane (through `def.base`,
// normal `def.stretch_axis`, `def.stretch_anchor` model units along that
// axis from `def.base`) by `offset` model units along the same axis,
// leaving the near side untouched - a genuine per-point deformation, not
// a single rigid transform applied to the whole object the way Flip's
// mirror or Array's translation are. Applied in `c`'s own local
// (definition) space, before PlaceFiltered's mirror/insert/array
// transform, since the anchor plane is defined relative to `def.base`.
//
// Point/Curve/Mesh (the geometry kinds a 2D drafting dynamic block
// actually places - lines, polylines, curves, points) get a real
// per-control-point/per-vertex split: each control point or vertex moves
// independently depending on which side of the plane it is on, so a
// single line or curve straddling the plane genuinely stretches (one end
// anchored, the other sliding away) instead of translating as a whole.
// Surface/Brep/SubD/PointCloud have no addressable per-point API reachable
// here without converting the object to a different kind (which would
// change what kind of object a stretched instance displays as, even at
// offset 0 once a frame is configured) - honestly scoped down for those
// four kinds to treating the whole object as a single point at its own
// bounding-box center: moved rigidly if that center is beyond the anchor
// plane, left untouched otherwise. See PARITY_MAP.md's "Dynamic blocks"
// item for this disclosed simplification.
void ApplyStretch(SceneObject& c, const BlockDefinition& def, double offset) {
  if (!def.has_stretch_frame || offset == 0) return;
  kernel::Vector3d axis = def.stretch_axis;
  if (!axis.Unitize()) return;
  const kernel::Vector3d delta = axis * offset;
  auto beyond = [&](const kernel::Point3d& p) {
    return ON_DotProduct(p - def.base, axis) >= def.stretch_anchor;
  };
  switch (c.kind) {
    case ObjectKind::Point:
      if (beyond(c.point)) c.point = c.point + delta;
      break;
    case ObjectKind::Curve:
      if (c.curve) {
        const int n = c.curve->ControlPointCount();
        for (int i = 0; i < n; ++i) {
          const kernel::Point3d p = c.curve->ControlPointAt(i);
          if (beyond(p)) c.curve->SetControlPointAt(i, p + delta);
        }
      }
      break;
    case ObjectKind::Mesh:
      if (c.mesh) {
        ON_Mesh& m = c.mesh->raw();
        for (int i = 0; i < m.VertexCount(); ++i) {
          const kernel::Point3d p = m.Vertex(i);
          if (beyond(p)) m.SetVertex(i, p + delta);
        }
        m.DestroyRuntimeCache(true);
        m.ComputeFaceNormals();
        m.ComputeVertexNormals();
      }
      break;
    default: {
      const kernel::BoundingBox bb = c.BoundingBox();
      const kernel::Point3d center = bb.min + (bb.max - bb.min) * 0.5;
      if (beyond(center)) c.Transform(ON_Xform::TranslationTransformation(delta));
      break;
    }
  }
}

std::vector<ObjectId> PlaceFiltered(Document& doc, const BlockDefinition& def, kernel::Point3d at, const std::string& state, bool flipped, int array_count, double stretch_offset) {
  ON_Xform xf = ON_Xform::TranslationTransformation(at - def.base);
  if (flipped) {
    const kernel::Vector3d n(1, 0, 0);
    const ON_Xform mirror = ON_Xform::MirrorTransformation(
        ON_PlaneEquation(n.x, n.y, n.z, -ON_DotProduct(n, kernel::Vector3d(def.base.x, def.base.y, def.base.z))));
    xf = xf * mirror;
  }
  kernel::Vector3d step(0, 0, 0);
  const int count = def.array_spacing != 0 ? std::clamp(array_count, 1, kMaxBlockArrayCount) : 1;
  if (count > 1) {
    step = def.array_axis;
    if (!step.Unitize()) step = kernel::Vector3d(1, 0, 0);
    step *= def.array_spacing;
  }
  std::vector<ObjectId> ids;
  for (int k = 0; k < count; ++k) {
    const ON_Xform step_xf = ON_Xform::TranslationTransformation(step * static_cast<double>(k)) * xf;
    for (const SceneObject& o : def.objects) {
      if (!ObjectVisibleInState(o, state)) continue;
      SceneObject c = o;
      c.id = kNoObject;
      c.selected = false;
      ApplyStretch(c, def, stretch_offset);  // local space, before mirror/insert/array
      c.Transform(step_xf);
      c.user_text["Block"] = def.name;
      c.user_text["BlockInsert"] = std::to_string(at.x) + "," + std::to_string(at.y) + "," + std::to_string(at.z);
      if (count > 1) c.user_text["BlockArrayIndex"] = std::to_string(k);
      ids.push_back(doc.Add(std::move(c)));
    }
  }
  return ids;
}
}  // namespace

std::string ResolveLookupState(const BlockDefinition& def, const std::string& key, const std::string& fallback) {
  if (key.empty()) return fallback;
  for (size_t i = 0; i < def.lookup_keys.size() && i < def.lookup_states.size(); ++i) {
    if (def.lookup_keys[i] == key) return def.lookup_states[i];
  }
  return fallback;  // no matching row: lookup has no effect, same as an empty key
}

int InstantiateDynamicBlock(Document& doc, const std::string& name, kernel::Point3d at, const std::string& state) {
  BlockDefinition* def = doc.FindBlock(name);
  if (!def) return -1;
  std::string active = state;
  if (active.empty() && !def->states.empty()) active = def->states.front();
  const std::vector<ObjectId> ids = PlaceFiltered(doc, *def, at, active, false, 1, 0);
  // Same anchor-object provenance as the static-block path in
  // InstantiateBlockInDocument (cmd_drafting.cpp) - see ProvenanceInfo's
  // comment in doc/Document.h.
  for (size_t i = 1; i < ids.size(); ++i) doc.SetProvenance(ids[i], ids[0], ProvenanceKind::BlockInstanceMember);
  const int group = doc.CreateGroup(ids, name);
  if (!def->states.empty()) {
    std::vector<BlockInstance> list = LoadBlockInstances(doc);
    BlockInstance rec;
    rec.group = group;
    rec.block = name;
    rec.state = active;
    rec.insert = at;
    rec.objects = ids;
    list.push_back(rec);
    SaveBlockInstances(doc, list);
  }
  return group;
}

bool RebuildBlockInstance(Document& doc, int group) {
  std::vector<BlockInstance> list = LoadBlockInstances(doc);
  auto it = std::find_if(list.begin(), list.end(), [&](const BlockInstance& b) { return b.group == group; });
  if (it == list.end()) return false;
  BlockDefinition* def = doc.FindBlock(it->block);
  if (!def) return false;
  for (ObjectId id : it->objects) doc.Remove(id);
  const std::string place_state = ResolveLookupState(*def, it->lookup_key, it->state);
  it->objects = PlaceFiltered(doc, *def, it->insert, place_state, it->flipped, it->array_count, it->stretch_offset);
  // Re-attach the fresh objects to the same group id so selection/explode
  // (which key off Document::Group membership) still find this instance,
  // and rebuild the anchor-object provenance the same way InstantiateDynamicBlock does.
  for (ObjectId id : it->objects) if (SceneObject* o = doc.Find(id)) o->group_id = it->group;
  for (size_t i = 1; i < it->objects.size(); ++i) doc.SetProvenance(it->objects[i], it->objects[0], ProvenanceKind::BlockInstanceMember);
  SaveBlockInstances(doc, list);
  return true;
}

bool SetBlockInstanceState(Document& doc, int group, const std::string& new_state) {
  std::vector<BlockInstance> list = LoadBlockInstances(doc);
  auto it = std::find_if(list.begin(), list.end(), [&](const BlockInstance& b) { return b.group == group; });
  if (it == list.end()) return false;
  it->state = new_state;
  SaveBlockInstances(doc, list);
  return RebuildBlockInstance(doc, group);
}

bool SetBlockInstanceFlip(Document& doc, int group, bool flipped) {
  std::vector<BlockInstance> list = LoadBlockInstances(doc);
  auto it = std::find_if(list.begin(), list.end(), [&](const BlockInstance& b) { return b.group == group; });
  if (it == list.end()) return false;
  it->flipped = flipped;
  SaveBlockInstances(doc, list);
  return RebuildBlockInstance(doc, group);
}

bool SetBlockInstanceArrayCount(Document& doc, int group, int count) {
  std::vector<BlockInstance> list = LoadBlockInstances(doc);
  auto it = std::find_if(list.begin(), list.end(), [&](const BlockInstance& b) { return b.group == group; });
  if (it == list.end()) return false;
  it->array_count = std::max(1, count);
  SaveBlockInstances(doc, list);
  return RebuildBlockInstance(doc, group);
}

bool SetBlockInstanceLookup(Document& doc, int group, const std::string& key) {
  std::vector<BlockInstance> list = LoadBlockInstances(doc);
  auto it = std::find_if(list.begin(), list.end(), [&](const BlockInstance& b) { return b.group == group; });
  if (it == list.end()) return false;
  it->lookup_key = key;
  SaveBlockInstances(doc, list);
  return RebuildBlockInstance(doc, group);
}

bool SetBlockInstanceStretch(Document& doc, int group, double offset) {
  std::vector<BlockInstance> list = LoadBlockInstances(doc);
  auto it = std::find_if(list.begin(), list.end(), [&](const BlockInstance& b) { return b.group == group; });
  if (it == list.end()) return false;
  it->stretch_offset = offset;
  SaveBlockInstances(doc, list);
  return RebuildBlockInstance(doc, group);
}

}  // namespace dino8::app
