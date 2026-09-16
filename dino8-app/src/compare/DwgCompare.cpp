#include "compare/DwgCompare.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <vector>

#include "io/File3dm.h"
#include "io/FileExchange.h"
#include "io/FileIgesStep.h"

namespace dino8::app {

namespace {
namespace fs = std::filesystem;

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Tags this module owns on SceneObject::user_text. Kept as plain string
// keys (matching Dino8.Reference's own convention) rather than new
// Document/SceneObject fields, so a compare overlay is entirely optional
// state riding along in the existing user-text map.
constexpr const char* kOverlayTag = "Dino8.CompareOverlay";  // "1" on a removed-object ghost copy
constexpr const char* kStateTag = "Dino8.CompareState";       // "Added" / "Modified" on a live object
const char* kRemovedLayer = "Compare: Removed";

const Color kAddedColor = Color::FromBytes(60, 200, 90);     // green
const Color kModifiedColor = Color::FromBytes(235, 165, 40); // orange
const Color kRemovedColor = Color::FromBytes(220, 60, 60);   // red, ghosted/dashed

// Rounds to 1e-3 model units - coarse enough to absorb the kind of
// re-tessellation/round-trip noise DWG/DXF export-then-import already
// introduces (see FileExchange.cpp's own fidelity notes), fine enough that
// an intentional edit of any real size still changes the key.
double Quantize(double v) {
  return std::round(v * 1000.0) / 1000.0;
}

std::string PointKey(double x, double y, double z) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f;", Quantize(x), Quantize(y), Quantize(z));
  return buf;
}

// Pulls every position out of an object's tessellation (kind-agnostic: this
// covers points, curves' polyline samples, and surfaces/breps/meshes/SubDs'
// triangle vertices, whatever DisplayCache actually populated for `o`).
std::vector<std::array<double, 3>> SampleGeometry(const SceneObject& o) {
  o.EnsureDisplay(0.05, 0.1);  // fixed, coarse tolerance - this is a diff aid, not a display cache borrow
  const DisplayCache& d = o.Display();
  std::vector<std::array<double, 3>> pts;
  pts.reserve(d.points.size() / 3 + d.lines.size() / 3 + d.triangles.size() / 6);
  for (size_t i = 0; i + 2 < d.points.size(); i += 3) pts.push_back({d.points[i], d.points[i + 1], d.points[i + 2]});
  for (size_t i = 0; i + 2 < d.lines.size(); i += 3) pts.push_back({d.lines[i], d.lines[i + 1], d.lines[i + 2]});
  for (size_t i = 0; i + 5 < d.triangles.size(); i += 6) pts.push_back({d.triangles[i], d.triangles[i + 1], d.triangles[i + 2]});
  if (pts.empty() && o.kind == ObjectKind::Point) pts.push_back({o.point.x, o.point.y, o.point.z});
  return pts;
}

// The exact-match key: kind + layer name + sorted, quantized sample points.
// Sorting makes the key tolerant of a curve being re-exported reversed or
// a polyline restarting at a different vertex (same shape, different
// sample order) - see DwgCompare.h's "Matching heuristic" section.
std::string FingerprintKey(const SceneObject& o, const std::string& layer_name) {
  std::vector<std::array<double, 3>> pts = SampleGeometry(o);
  std::sort(pts.begin(), pts.end());
  std::string key = ObjectKindName(o.kind);
  key += '|';
  key += layer_name;
  key += '|';
  for (const auto& p : pts) key += PointKey(p[0], p[1], p[2]);
  return key;
}

std::string LayerNameOf(const Document& d, int layer_index) {
  if (layer_index >= 0 && layer_index < static_cast<int>(d.Layers().size())) return d.Layers()[static_cast<size_t>(layer_index)].name;
  return "";
}

kernel::Point3d BoxCenter(const kernel::BoundingBox& b) {
  return kernel::Point3d((b.min.x + b.max.x) * 0.5, (b.min.y + b.max.y) * 0.5, (b.min.z + b.max.z) * 0.5);
}

// One candidate on either side of the diff, carrying just what the
// matcher/pairing heuristic needs (not the whole SceneObject, so the "old"
// side can be indices into a scratch Document that stays alive only for
// the duration of the compare).
struct Candidate {
  std::string key;
  std::string layer;
  kernel::Point3d center{0, 0, 0};
  size_t index = 0;  // index into the owning object list (doc.Objects() for "new", other.Objects() for "old")
};

std::vector<Candidate> BuildCandidates(const Document& d, const std::vector<size_t>& indices) {
  std::vector<Candidate> out;
  out.reserve(indices.size());
  for (size_t i : indices) {
    const SceneObject& o = d.Objects()[i];
    const std::string layer = LayerNameOf(d, o.layer_index);
    Candidate c;
    c.key = FingerprintKey(o, layer);
    c.layer = layer;
    c.center = BoxCenter(o.BoundingBox());
    c.index = i;
    out.push_back(std::move(c));
  }
  return out;
}

// The result of matching two candidate sets: which "new" indices are
// added/modified(paired)/unchanged, and which "old" indices are
// removed/modified(paired)/unchanged.
struct MatchResult {
  std::vector<size_t> added;                       // new_objs indices
  std::vector<size_t> removed;                      // old_objs indices
  std::vector<std::pair<size_t, size_t>> modified;  // (new index, old index)
  int unchanged = 0;
};

MatchResult Match(std::vector<Candidate> new_cands, std::vector<Candidate> old_cands) {
  MatchResult result;

  // Step 1: exact fingerprint match -> unchanged, removed from both pools.
  std::vector<bool> old_used(old_cands.size(), false);
  std::vector<bool> new_used(new_cands.size(), false);
  for (size_t ni = 0; ni < new_cands.size(); ++ni) {
    for (size_t oi = 0; oi < old_cands.size(); ++oi) {
      if (old_used[oi]) continue;
      if (new_cands[ni].key == old_cands[oi].key) {
        old_used[oi] = true;
        new_used[ni] = true;
        ++result.unchanged;
        break;
      }
    }
  }

  // Step 2: pair remaining same-layer candidates by nearest bounding-box
  // center (greedy nearest-neighbor over all remaining pairs, closest
  // first) - see DwgCompare.h's disclosed heuristic limits.
  struct Pair { double dist2; size_t ni, oi; };
  std::vector<Pair> pairs;
  for (size_t ni = 0; ni < new_cands.size(); ++ni) {
    if (new_used[ni]) continue;
    for (size_t oi = 0; oi < old_cands.size(); ++oi) {
      if (old_used[oi]) continue;
      if (new_cands[ni].layer != old_cands[oi].layer) continue;
      const kernel::Point3d dv = new_cands[ni].center - old_cands[oi].center;
      pairs.push_back({dv.x * dv.x + dv.y * dv.y + dv.z * dv.z, ni, oi});
    }
  }
  std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.dist2 < b.dist2; });
  for (const Pair& p : pairs) {
    if (new_used[p.ni] || old_used[p.oi]) continue;
    new_used[p.ni] = true;
    old_used[p.oi] = true;
    result.modified.emplace_back(new_cands[p.ni].index, old_cands[p.oi].index);
  }

  for (size_t ni = 0; ni < new_cands.size(); ++ni) if (!new_used[ni]) result.added.push_back(new_cands[ni].index);
  for (size_t oi = 0; oi < old_cands.size(); ++oi) if (!old_used[oi]) result.removed.push_back(old_cands[oi].index);
  return result;
}

// Applies bucket colouring/tags to `doc`'s own objects (added/modified) and
// copies removed objects in from `old_doc` as locked, dashed ghosts on
// kRemovedLayer. Reuses the existing colour-override mechanism
// (color_by_layer=false + a Color, exactly what SetObjectDisplayMode-style
// per-object commands already do) and the existing dashed-linetype
// rendering path (SceneObject::linetype + Document::EffectiveDashes) rather
// than adding a new rendering path.
void ApplyResult(Document& doc, const Document& old_doc, const MatchResult& m, CompareStats* stats) {
  for (size_t ni : m.added) {
    SceneObject& o = doc.Objects()[ni];
    o.color_by_layer = false;
    o.color = kAddedColor;
    o.user_text[kStateTag] = "Added";
  }
  for (const auto& pr : m.modified) {
    SceneObject& o = doc.Objects()[pr.first];
    o.color_by_layer = false;
    o.color = kModifiedColor;
    o.user_text[kStateTag] = "Modified";
  }
  if (!m.removed.empty()) {
    int layer_idx = doc.FindLayer(kRemovedLayer);
    if (layer_idx < 0) {
      layer_idx = doc.AddLayer(kRemovedLayer, kRemovedColor);
      doc.Layers()[static_cast<size_t>(layer_idx)].locked = true;
    }
    for (size_t oi : m.removed) {
      SceneObject ghost = old_doc.Objects()[oi];
      ghost.selected = false;
      ghost.locked = true;
      ghost.visible = true;
      ghost.layer_index = layer_idx;
      ghost.color_by_layer = false;
      ghost.color = kRemovedColor;
      ghost.linetype = "Dashed";  // ghosted: dashed since it no longer exists in the current document
      ghost.user_text[kOverlayTag] = "1";
      ghost.user_text[kStateTag] = "Removed";
      doc.Add(std::move(ghost));
    }
  }
  if (stats) {
    stats->added = static_cast<int>(m.added.size());
    stats->removed = static_cast<int>(m.removed.size());
    stats->modified = static_cast<int>(m.modified.size());
    stats->unchanged = m.unchanged;
  }
}

}  // namespace

bool LoadCompareSource(Document& out, const std::string& path, std::string& error) {
  const std::string ext = Lower(fs::path(path).extension().string());
  if (ext == ".3dm") return Load3dm(out, path, error);
  if (ext == ".dxf") return ImportDxf(out, path, error);
  if (ext == ".dwg") return ImportDwg(out, path, error);
  if (ext == ".ply") return ImportPly(out, path, error);
  if (ext == ".obj" || ext == ".stl") return ImportMeshFile(out, path, error);
  if (ext == ".igs" || ext == ".iges") return ImportIges(out, path, error);
  if (ext == ".stp" || ext == ".step") return ImportStep(out, path, error);
  error = "Unsupported file type for compare: " + ext;
  return false;
}

bool RunDwgCompare(Document& doc, const std::string& path, std::string& error, CompareStats* stats) {
  Document old_doc;
  if (!LoadCompareSource(old_doc, path, error)) return false;

  // Candidates: every current-document object not already part of a
  // previous compare overlay (so re-running DwgCompare after ClearDwgCompare,
  // or comparing again without clearing, doesn't fold a stale ghost/tint
  // into the new diff).
  std::vector<size_t> new_indices;
  for (size_t i = 0; i < doc.Objects().size(); ++i) {
    if (doc.Objects()[i].user_text.count(kOverlayTag)) continue;
    new_indices.push_back(i);
  }
  std::vector<size_t> old_indices(old_doc.Objects().size());
  for (size_t i = 0; i < old_indices.size(); ++i) old_indices[i] = i;

  doc.BeginChange("DwgCompare");
  const MatchResult m = Match(BuildCandidates(doc, new_indices), BuildCandidates(old_doc, old_indices));
  ApplyResult(doc, old_doc, m, stats);
  doc.Touch();
  return true;
}

bool RunXrefCompare(Document& doc, const std::string& alias_or_path, std::string& error, CompareStats* stats) {
  ReferenceModel* rm = nullptr;
  for (ReferenceModel& r : doc.ReferenceModels()) {
    if (r.path == alias_or_path || Lower(r.alias) == Lower(alias_or_path)) { rm = &r; break; }
  }
  if (!rm) { error = "No attached reference model (xref) matches '" + alias_or_path + "'. Use Worksession Attach first."; return false; }

  Document old_doc;  // the source file, re-read fresh from disk right now
  if (!LoadCompareSource(old_doc, rm->path, error)) return false;

  // "New" candidates: only this reference model's own objects currently in
  // doc (identified the same way Worksession itself tracks them - by the
  // Dino8.Reference user-text tag matching this model's source path), not
  // the whole document - an xref compare is scoped to the xref.
  std::vector<size_t> new_indices;
  for (size_t i = 0; i < doc.Objects().size(); ++i) {
    const SceneObject& o = doc.Objects()[i];
    auto it = o.user_text.find("Dino8.Reference");
    if (it != o.user_text.end() && it->second == rm->path) new_indices.push_back(i);
  }
  std::vector<size_t> old_indices(old_doc.Objects().size());
  for (size_t i = 0; i < old_indices.size(); ++i) old_indices[i] = i;

  doc.BeginChange("XrefCompare");
  const MatchResult m = Match(BuildCandidates(doc, new_indices), BuildCandidates(old_doc, old_indices));
  ApplyResult(doc, old_doc, m, stats);
  doc.Touch();
  return true;
}

int ClearDwgCompare(Document& doc) {
  doc.BeginChange("Clear compare overlay");
  int touched = 0;
  std::vector<ObjectId> to_remove;
  for (SceneObject& o : doc.Objects()) {
    if (o.user_text.count(kOverlayTag)) { to_remove.push_back(o.id); continue; }
    auto it = o.user_text.find(kStateTag);
    if (it != o.user_text.end()) {
      o.color_by_layer = true;
      o.color = Color{};
      o.user_text.erase(it);
      ++touched;
    }
  }
  for (ObjectId id : to_remove) {
    if (doc.Remove(id)) ++touched;
  }
  const int layer_idx = doc.FindLayer(kRemovedLayer);
  if (layer_idx >= 0) doc.RemoveLayer(layer_idx);  // no-op (refused) if anything still uses it
  doc.Touch();
  return touched;
}

}  // namespace dino8::app
