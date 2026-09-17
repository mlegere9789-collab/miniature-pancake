#include "session/Worksession.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "io/File3dm.h"
#include "util/json_mini.h"

namespace dino8::app {

namespace {
namespace fs = std::filesystem;

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool BoxesIntersect(kernel::Point3d amin, kernel::Point3d amax, kernel::Point3d bmin, kernel::Point3d bmax) {
  return amin.x <= bmax.x && amax.x >= bmin.x && amin.y <= bmax.y && amax.y >= bmin.y && amin.z <= bmax.z &&
         amax.z >= bmin.z;
}

std::string EscapeJson(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

// Finds a reference model by path (exact) or alias (case-insensitive);
// "*"/"all" (case-insensitive) matches nothing here - callers loop over
// every model themselves for that case.
ReferenceModel* FindModel(Document& doc, const std::string& alias_or_path) {
  for (ReferenceModel& m : doc.ReferenceModels()) {
    if (m.path == alias_or_path || Lower(m.alias) == Lower(alias_or_path)) return &m;
  }
  return nullptr;
}

}  // namespace

int AttachWorksession(Document& doc, const std::string& path, std::string& error, bool has_limit,
                     kernel::Point3d limit_min, kernel::Point3d limit_max) {
  Document ref;
  if (!Load3dm(ref, path, error)) return -1;
  const std::string alias = fs::path(path).filename().string();
  ReferenceModel rm;
  rm.path = path;
  rm.alias = alias.empty() ? path : alias;
  rm.has_limit_box = has_limit;
  rm.limit_min = limit_min;
  rm.limit_max = limit_max;
  doc.BeginChange("Worksession Attach");
  int layer_idx = doc.AddLayer("Ref: " + rm.alias);
  doc.Layers()[static_cast<size_t>(layer_idx)].locked = true;
  doc.Layers()[static_cast<size_t>(layer_idx)].color = Color::FromBytes(140, 140, 140);
  int added = 0;
  for (SceneObject& o : ref.Objects()) {
    if (has_limit) {
      const kernel::BoundingBox bb = o.BoundingBox();
      if (!BoxesIntersect(bb.min, bb.max, limit_min, limit_max)) continue;
    }
    o.selected = false;
    o.locked = true;
    o.visible = true;
    o.layer_index = layer_idx;
    o.user_text["Dino8.Reference"] = path;
    const ObjectId id = doc.Add(std::move(o));
    rm.object_ids.push_back(id);
    ++added;
  }
  doc.ReferenceModels().push_back(rm);
  return added;
}

int DetachWorksession(Document& doc, const std::string& alias_or_path) {
  const bool all = Lower(alias_or_path) == "all" || alias_or_path == "*";
  doc.BeginChange("Worksession Detach");
  int removed = 0;
  auto& models = doc.ReferenceModels();
  for (auto it = models.begin(); it != models.end();) {
    if (!all && it->path != alias_or_path && Lower(it->alias) != Lower(alias_or_path)) { ++it; continue; }
    for (ObjectId id : it->object_ids)
      if (doc.Remove(id)) ++removed;
    // The reference model's own layer, if it's now empty and not current.
    const int layer_idx = doc.FindLayer("Ref: " + it->alias);
    if (layer_idx >= 0) doc.RemoveLayer(layer_idx);
    it = models.erase(it);
  }
  return removed;
}

int LimitWorksessionModel(Document& doc, const std::string& alias_or_path, kernel::Point3d limit_min,
                         kernel::Point3d limit_max) {
  ReferenceModel* m = FindModel(doc, alias_or_path);
  if (!m) return 0;
  doc.BeginChange("LimitReferenceModel");
  m->has_limit_box = true;
  m->limit_min = limit_min;
  m->limit_max = limit_max;
  int removed = 0;
  std::vector<ObjectId> kept;
  for (ObjectId id : m->object_ids) {
    SceneObject* o = doc.Find(id);
    if (!o) continue;
    const kernel::BoundingBox bb = o->BoundingBox();
    if (BoxesIntersect(bb.min, bb.max, limit_min, limit_max)) {
      kept.push_back(id);
    } else {
      doc.Remove(id);
      ++removed;
    }
  }
  m->object_ids = kept;
  return removed;
}

bool SaveWorksessionFile(const Document& doc, const std::string& path, std::string& error) {
  std::ofstream out(path);
  if (!out) { error = "Could not write " + path; return false; }
  out << "{\n  \"dino8_worksession\": 1,\n  \"models\": [\n";
  const auto& models = doc.ReferenceModels();
  for (size_t i = 0; i < models.size(); ++i) {
    out << "    {\"path\": \"" << EscapeJson(models[i].path) << "\", \"alias\": \"" << EscapeJson(models[i].alias)
        << "\"}" << (i + 1 < models.size() ? "," : "") << "\n";
  }
  out << "  ]\n}\n";
  return true;
}

int LoadWorksessionFile(Document& doc, const std::string& path, std::string& error) {
  std::ifstream in(path);
  if (!in) { error = "Could not open " + path; return 0; }
  std::stringstream buf;
  buf << in.rdbuf();
  json::Value root;
  if (!json::Parse(buf.str(), root, error) || !root.IsObject()) {
    if (error.empty()) error = path + " is not a valid worksession file";
    return 0;
  }
  const json::Value& models = root["models"];
  if (!models.IsArray()) return 0;
  int attached = 0;
  for (size_t i = 0; i < models.Size(); ++i) {
    const std::string mpath = models[i]["path"].AsString();
    if (mpath.empty()) continue;
    if (FindModel(doc, mpath)) continue;  // already attached
    std::string err;
    if (AttachWorksession(doc, mpath, err) >= 0) ++attached;
  }
  return attached;
}

}  // namespace dino8::app
