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
        << ",\"ix\":" << b.insert.x << ",\"iy\":" << b.insert.y << ",\"iz\":" << b.insert.z << ",\"objects\":[";
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
std::vector<ObjectId> PlaceFiltered(Document& doc, const BlockDefinition& def, kernel::Point3d at, const std::string& state) {
  const ON_Xform xf = ON_Xform::TranslationTransformation(at - def.base);
  std::vector<ObjectId> ids;
  for (const SceneObject& o : def.objects) {
    if (!ObjectVisibleInState(o, state)) continue;
    SceneObject c = o;
    c.id = kNoObject;
    c.selected = false;
    c.Transform(xf);
    c.user_text["Block"] = def.name;
    c.user_text["BlockInsert"] = std::to_string(at.x) + "," + std::to_string(at.y) + "," + std::to_string(at.z);
    ids.push_back(doc.Add(std::move(c)));
  }
  return ids;
}
}  // namespace

int InstantiateDynamicBlock(Document& doc, const std::string& name, kernel::Point3d at, const std::string& state) {
  BlockDefinition* def = doc.FindBlock(name);
  if (!def) return -1;
  std::string active = state;
  if (active.empty() && !def->states.empty()) active = def->states.front();
  const std::vector<ObjectId> ids = PlaceFiltered(doc, *def, at, active);
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
  it->objects = PlaceFiltered(doc, *def, it->insert, it->state);
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

}  // namespace dino8::app
