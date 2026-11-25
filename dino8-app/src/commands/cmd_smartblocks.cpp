// Smart Blocks: Search & Convert (AutoCAD 2026's BSEARCH/BCONVERT
// equivalent) - detects repeated groups of geometry scattered around a
// drawing at different placements/rotations and converts each detected
// group into instances of a new BlockDefinition (see BlockCommand /
// InstantiateBlock in cmd_drafting.cpp, which this file reuses rather than
// re-implementing block creation).
//
// This is a classical geometry-clustering algorithm, not machine learning:
//
//   1. Spatial clustering (candidate groups): objects are unioned into
//      connected components by expanding each object's world AABB by a
//      small gap tolerance and looking for overlaps, using ObjectGrid to
//      avoid a full O(n^2) pairwise test (see BuildSpatialClusters below).
//      This turns "everything touching or nearly touching" into one
//      candidate group, exactly the sub-groups a human would call "one
//      piece of geometry" when picking a block by eye.
//
//   2. Shape signature (per candidate group): a translation- and
//      rotation-invariant descriptor built from two classical, well known
//      ideas:
//        - a type/size multiset: each member object (or each contiguous
//          run of glyph-outline curves that make up one Text/TextObject
//          annotation, collapsed to a single "Text" node - see
//          CollapseTextRuns) is reduced to a (kind tag, scalar size)
//          pair - line length, circle/arc radius, curve arc length,
//          bounding-box diagonal for surfaces/breps/meshes/SubDs, 0 for
//          points. The multiset of these pairs, sorted, is invariant to
//          translation, rotation and reflection because each entry is a
//          scalar computed from the object's own local geometry.
//        - a D2 shape distribution (Osada et al., "Shape Distributions",
//          2002): the sorted list of pairwise Euclidean distances between
//          every member object's reference point (its own bbox centroid).
//          Pairwise point-to-point distances are exactly invariant under
//          any rigid transform (translation + rotation, and reflection),
//          which is exactly the invariance a "same shape, moved/rotated
//          elsewhere in the drawing" detector needs. This is the real
//          geometric content of the signature: two groups whose objects
//          are individually the same sizes/kinds AND arranged with the
//          same relative distances between them are, up to a rigid
//          motion, the same shape.
//      Two signatures are matched (within kMatchTolerance, a relative
//      tolerance on every scalar) only when both the type/size multiset
//      AND the D2 distance list agree elementwise after sorting - the
//      multiset alone can't distinguish e.g. a 3-4-5 right triangle of
//      reference points from an isoceles one with the same edge lengths
//      in a different order, so the sorted pairwise-distance vector (not
//      just component sizes) is what actually pins down the arrangement.
//
//   3. Clustering the candidate groups by signature match is a simple
//      union-find over the (already small - one entry per candidate
//      spatial group, not per object) list of groups, so it costs
//      O(k^2) comparisons for k candidate groups rather than O(n^2) over
//      every object in the drawing.
//
// Text wildcarding ("near-matches with different text"): a run of glyph
// curves belonging to one Text/TextObject annotation (user_text
// Annotation=Text, see cmd_annotate2.cpp) is collapsed into a single node
// before signature comparison, using only its combined bounding box
// (centroid + diagonal) - the actual glyph outlines and hence the text
// content are never compared. Two groups that are geometrically identical
// except that one says "A1" and the other says "B12" therefore still
// match, as long as the text block's bounding box is a comparable size in
// both. This is real (not stubbed) but limited to the text/attribute case:
// non-text geometry differences (a different-radius circle, an extra
// line) are never wildcarded, matching AutoCAD's own "near-match" scope,
// which BCONVERT documents as text/attribute-tolerant rather than
// shape-tolerant.
//
// Known limits, disclosed rather than hidden:
//   - Curve identity beyond "arc length + open/closed + straight-or-not"
//     is coarse: two different-shaped open curves that happen to have the
//     same length, same endpoint-to-endpoint arrangement relative to
//     their group's other members, would be a false positive. Genuinely
//     rare in practice (it requires an engineered adversarial case) but
//     possible - a full curve-shape signature (e.g. curvature-vs-arclength
//     sampling) is a reasonable follow-up if this proves too coarse.
//   - Objects that touch or nearly touch a neighboring, unrelated group
//     (closer than kSpatialGapTolerance) are clustered together into one
//     candidate group and will fail to match anything, a false negative
//     at the tolerance boundary rather than a false positive.
//   - Non-uniform scale (a copy of the same shape resized) is deliberately
//     NOT matched: AutoCAD's own tool and this one both treat a resized
//     copy as a different block, since instancing a resized copy would
//     require a scaled instance transform Dino 8's BlockDefinition does
//     not model as a per-instance parameter.
#include "commands/cmd_common.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace dino8::app {

namespace {

// Grid cells are already an approximate spatial index; this is the extra
// slack (world units) two objects' bounding boxes are allowed to be apart
// and still be considered "the same candidate group" - matches the kind of
// gap a human eye tolerates between a leader line and its arrowhead.
constexpr double kSpatialGapTolerance = 1e-3;

// Relative tolerance for matching signature scalars (sizes and pairwise
// reference-point distances) between two candidate groups.
constexpr double kMatchTolerance = 1e-3;

struct ShapeNode {
  std::string kind;   // "Point" / "Line" / "Circle" / "Curve:closed" / "Curve:open" / "Surface" / "Brep" / "Mesh" / "SubD" / "Text"
  double size = 0.0;   // rotation/translation-invariant scalar: length/radius/diagonal
  kernel::Point3d ref{0, 0, 0};  // world-space reference point (bbox centroid) at detection time
};

struct CandidateGroup {
  std::vector<ObjectId> members;      // every real object id in the group (glyph curves included)
  std::vector<ShapeNode> nodes;       // after collapsing text runs
};

struct GroupSignature {
  std::vector<std::pair<std::string, double>> composition;  // sorted (kind, size)
  std::vector<double> pair_distances;                        // sorted D2 distribution
};

double Dist(kernel::Point3d a, kernel::Point3d b) {
  return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

kernel::BoundingBox ExpandedBox(const kernel::BoundingBox& b, double gap) {
  kernel::BoundingBox out = b;
  out.min.x -= gap; out.min.y -= gap; out.min.z -= gap;
  out.max.x += gap; out.max.y += gap; out.max.z += gap;
  return out;
}

bool Overlaps(const kernel::BoundingBox& a, const kernel::BoundingBox& b) {
  return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
         a.min.z <= b.max.z && a.max.z >= b.min.z;
}

kernel::Point3d Centroid(const kernel::BoundingBox& b) {
  return kernel::Point3d((b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, (b.min.z + b.max.z) / 2);
}

double Diagonal(const kernel::BoundingBox& b) { return Dist(b.min, b.max); }

// Union-find over object indices (into a caller-provided flat index list),
// used both for spatial clustering (step 1) and for grouping matching
// candidate signatures (step 3).
struct UnionFind {
  std::vector<int> parent;
  explicit UnionFind(int n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
  int Find(int x) { while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; }
  void Union(int a, int b) { a = Find(a); b = Find(b); if (a != b) parent[a] = b; }
};

// Step 1: partitions doc.Objects() into spatially-connected candidate
// groups. Uses ObjectGrid::QueryBox on each object's gap-expanded AABB to
// find nearby candidates instead of testing every pair, so cost is
// proportional to how cluttered the drawing actually is near each object,
// not to the total object count squared.
std::vector<std::vector<std::size_t>> BuildSpatialClusters(CommandContext& ctx) {
  Document& doc = ctx.Doc();
  const std::vector<SceneObject>& objs = doc.Objects();
  ObjectGrid grid;
  grid.EnsureFresh(doc);
  std::vector<kernel::BoundingBox> boxes(objs.size());
  for (std::size_t i = 0; i < objs.size(); ++i) {
    objs[i].EnsureDisplay(ctx.App().curve_display_tolerance, ctx.App().surface_display_tolerance);
    boxes[i] = objs[i].BoundingBox();
  }
  UnionFind uf(static_cast<int>(objs.size()));
  for (std::size_t i = 0; i < objs.size(); ++i) {
    const kernel::BoundingBox expanded = ExpandedBox(boxes[i], kSpatialGapTolerance);
    for (std::size_t j : grid.QueryBox(expanded)) {
      if (j == i) continue;
      if (Overlaps(expanded, boxes[j])) uf.Union(static_cast<int>(i), static_cast<int>(j));
    }
  }
  std::unordered_map<int, std::vector<std::size_t>> by_root;
  for (std::size_t i = 0; i < objs.size(); ++i) by_root[uf.Find(static_cast<int>(i))].push_back(i);
  std::vector<std::vector<std::size_t>> clusters;
  clusters.reserve(by_root.size());
  for (auto& [root, members] : by_root) clusters.push_back(std::move(members));
  return clusters;
}

bool IsTextRun(const SceneObject& o) {
  auto it = o.user_text.find("Annotation");
  return it != o.user_text.end() && (it->second == "Text" || it->second == "TextObject");
}

// Classifies one non-text object into a (kind, size) shape node.
ShapeNode ClassifyObject(const SceneObject& o) {
  ShapeNode n;
  n.ref = Centroid(o.BoundingBox());
  switch (o.kind) {
    case ObjectKind::Point:
      n.kind = "Point";
      n.size = 0.0;
      return n;
    case ObjectKind::Curve: {
      const ON_NurbsCurve& c = o.curve->raw();
      const double len = o.curve->Length();
      ON_Arc arc;
      const bool closed = c.IsClosed() != 0;
      if (c.IsLinear(1e-7)) {
        n.kind = "Line";
        n.size = len;
      } else if (c.IsArc(nullptr, &arc, 1e-7)) {
        n.kind = closed ? "Circle" : "Arc";
        n.size = arc.Radius();
      } else {
        n.kind = closed ? "Curve:closed" : "Curve:open";
        n.size = len;
      }
      return n;
    }
    case ObjectKind::Surface:
      n.kind = "Surface";
      n.size = Diagonal(o.BoundingBox());
      return n;
    case ObjectKind::Brep:
      n.kind = "Brep";
      n.size = Diagonal(o.BoundingBox());
      return n;
    case ObjectKind::Mesh:
      n.kind = "Mesh";
      n.size = Diagonal(o.BoundingBox());
      return n;
    case ObjectKind::SubD:
      n.kind = "SubD";
      n.size = Diagonal(o.BoundingBox());
      return n;
  }
  n.kind = "Unknown";
  return n;
}

// Reduces a spatial cluster's raw member list into shape nodes, collapsing
// every contiguous run of Text/TextObject glyph curves that share a
// group_id into one wildcard "Text" node (bounding-box centroid + diagonal
// only - glyph shapes/content are never compared).
CandidateGroup BuildCandidateGroup(const Document& doc, const std::vector<std::size_t>& indices) {
  CandidateGroup g;
  const std::vector<SceneObject>& objs = doc.Objects();
  std::unordered_map<int, std::vector<std::size_t>> text_by_group;
  std::vector<std::size_t> plain;
  for (std::size_t idx : indices) {
    const SceneObject& o = objs[idx];
    g.members.push_back(o.id);
    if (IsTextRun(o) && o.group_id >= 0) {
      text_by_group[o.group_id].push_back(idx);
    } else {
      plain.push_back(idx);
    }
  }
  for (std::size_t idx : plain) g.nodes.push_back(ClassifyObject(objs[idx]));
  for (auto& [gid, members] : text_by_group) {
    kernel::BoundingBox bb = objs[members[0]].BoundingBox();
    for (std::size_t k = 1; k < members.size(); ++k) {
      const kernel::BoundingBox b = objs[members[k]].BoundingBox();
      bb.min.x = std::min(bb.min.x, b.min.x); bb.min.y = std::min(bb.min.y, b.min.y); bb.min.z = std::min(bb.min.z, b.min.z);
      bb.max.x = std::max(bb.max.x, b.max.x); bb.max.y = std::max(bb.max.y, b.max.y); bb.max.z = std::max(bb.max.z, b.max.z);
    }
    ShapeNode n;
    n.kind = "Text";
    n.size = Diagonal(bb);
    n.ref = Centroid(bb);
    g.nodes.push_back(n);
  }
  return g;
}

GroupSignature Signature(const CandidateGroup& g) {
  GroupSignature s;
  for (const ShapeNode& n : g.nodes) s.composition.emplace_back(n.kind, n.size);
  std::sort(s.composition.begin(), s.composition.end());
  for (std::size_t i = 0; i < g.nodes.size(); ++i)
    for (std::size_t j = i + 1; j < g.nodes.size(); ++j) s.pair_distances.push_back(Dist(g.nodes[i].ref, g.nodes[j].ref));
  std::sort(s.pair_distances.begin(), s.pair_distances.end());
  return s;
}

bool NearlyEqual(double a, double b, double tol) {
  const double scale = std::max({1.0, std::fabs(a), std::fabs(b)});
  return std::fabs(a - b) <= tol * scale;
}

bool SignaturesMatch(const GroupSignature& a, const GroupSignature& b) {
  if (a.composition.size() != b.composition.size()) return false;
  for (std::size_t i = 0; i < a.composition.size(); ++i) {
    if (a.composition[i].first != b.composition[i].first) return false;
    if (!NearlyEqual(a.composition[i].second, b.composition[i].second, kMatchTolerance)) return false;
  }
  if (a.pair_distances.size() != b.pair_distances.size()) return false;
  for (std::size_t i = 0; i < a.pair_distances.size(); ++i)
    if (!NearlyEqual(a.pair_distances[i], b.pair_distances[i], kMatchTolerance)) return false;
  return true;
}

// Cached across a SmartBlockDetect -> SmartBlockConvert pair within one
// running session; never written to a document or a file (no persistence
// claim, so this does not touch the separate BlockDefinition save/load
// work). Rebuilt whenever the document revision changes.
struct DetectedPattern {
  std::vector<std::vector<ObjectId>> instances;  // one entry per matched occurrence
};
struct DetectCache {
  std::uint64_t revision = ~0ull;
  std::vector<DetectedPattern> patterns;
};
DetectCache& Cache() {
  static DetectCache cache;
  return cache;
}

std::vector<DetectedPattern> DetectPatterns(CommandContext& ctx) {
  std::vector<std::vector<std::size_t>> clusters = BuildSpatialClusters(ctx);
  const Document& doc = ctx.Doc();
  std::vector<CandidateGroup> groups;
  groups.reserve(clusters.size());
  for (auto& c : clusters) {
    // Skip clusters that are already a Block instance (nothing to detect -
    // they are already the outcome this feature produces) and clusters
    // too small to be a meaningful "block" (a single lone object repeated
    // is not the multi-entity clutter this tool targets).
    bool already_block = false;
    for (std::size_t idx : c) if (doc.Objects()[idx].user_text.count("Block")) { already_block = true; break; }
    if (already_block) continue;
    CandidateGroup g = BuildCandidateGroup(doc, c);
    if (g.nodes.size() < 2) continue;
    groups.push_back(std::move(g));
  }
  std::vector<GroupSignature> sigs;
  sigs.reserve(groups.size());
  for (auto& g : groups) sigs.push_back(Signature(g));
  UnionFind uf(static_cast<int>(groups.size()));
  for (std::size_t i = 0; i < groups.size(); ++i)
    for (std::size_t j = i + 1; j < groups.size(); ++j)
      if (SignaturesMatch(sigs[i], sigs[j])) uf.Union(static_cast<int>(i), static_cast<int>(j));
  std::unordered_map<int, std::vector<std::size_t>> by_root;
  for (std::size_t i = 0; i < groups.size(); ++i) by_root[uf.Find(static_cast<int>(i))].push_back(i);
  std::vector<DetectedPattern> patterns;
  for (auto& [root, members] : by_root) {
    if (members.size() < 2) continue;  // repeated means at least 2 occurrences
    DetectedPattern p;
    for (std::size_t idx : members) p.instances.push_back(groups[idx].members);
    patterns.push_back(std::move(p));
  }
  // Largest / most-repeated patterns first, matching how a human would
  // triage the report (the most valuable blocks to make are the ones that
  // appear most often).
  std::sort(patterns.begin(), patterns.end(), [](const DetectedPattern& a, const DetectedPattern& b) {
    return a.instances.size() > b.instances.size();
  });
  return patterns;
}

}  // namespace

void RegisterSmartBlockCommands(CommandEngine& e) {
  Reg(e, "SmartBlockDetect", Immediate([](CommandContext& ctx) {
        std::vector<DetectedPattern> patterns = DetectPatterns(ctx);
        Cache().revision = ctx.Doc().Revision();
        Cache().patterns = patterns;
        if (patterns.empty()) { ctx.Print("SmartBlockDetect: no repeated geometry groups found"); return; }
        ctx.Print("SmartBlockDetect: " + std::to_string(patterns.size()) + " repeated group(s) found");
        for (std::size_t i = 0; i < patterns.size(); ++i) {
          const DetectedPattern& p = patterns[i];
          const std::size_t objs_per_instance = p.instances.front().size();
          ctx.Print("  group " + std::to_string(i + 1) + ": " + std::to_string(p.instances.size()) +
                    " instance(s), " + std::to_string(objs_per_instance) + " object(s) each");
        }
        ctx.App().Panels().command_history = true;
      }),
      CommandStatus::Implemented,
      "Classical shape-signature clustering (no ML): builds spatial candidate groups via ObjectGrid, reduces each to "
      "a translation/rotation-invariant signature (per-object kind+size multiset plus the sorted pairwise distances "
      "between member reference points - a D2 shape distribution), and reports every set of >= 2 candidate groups "
      "whose signatures match within tolerance. Text/TextObject glyph runs are collapsed to a single wildcard node "
      "(bounding box only, content ignored), so two groups differing only in embedded text still match; other "
      "geometry differences are never wildcarded. Results are cached in-memory (not saved to the document/file) for "
      "a following SmartBlockConvert.");
  Reg(e, "SmartBlockConvert", Immediate([](CommandContext& ctx) {
        if (Cache().revision != ctx.Doc().Revision() || Cache().patterns.empty()) {
          std::vector<DetectedPattern> patterns = DetectPatterns(ctx);
          Cache().revision = ctx.Doc().Revision();
          Cache().patterns = patterns;
        }
        if (Cache().patterns.empty()) { ctx.Warn("SmartBlockConvert: no detected groups (run SmartBlockDetect first)"); return; }
        ctx.Doc().BeginChange("SmartBlockConvert");
        int converted_groups = 0, converted_instances = 0;
        for (std::size_t gi = 0; gi < Cache().patterns.size(); ++gi) {
          const DetectedPattern& p = Cache().patterns[gi];
          const std::string block_name = "SmartBlock" + std::to_string(gi + 1);
          // Representative instance: the first match found. Its own
          // current placement becomes the block definition's local frame
          // (base point = its bounding-box centroid), matching how
          // BlockCommand above defines a block from whatever is selected.
          const std::vector<ObjectId>& rep_ids = p.instances.front();
          kernel::BoundingBox rep_bb;
          bool have_bb = false;
          BlockDefinition def;
          def.name = block_name;
          for (ObjectId id : rep_ids) {
            const SceneObject* o = ctx.Doc().Find(id);
            if (!o) continue;
            const kernel::BoundingBox b = o->BoundingBox();
            if (!have_bb) { rep_bb = b; have_bb = true; }
            else {
              rep_bb.min.x = std::min(rep_bb.min.x, b.min.x); rep_bb.min.y = std::min(rep_bb.min.y, b.min.y); rep_bb.min.z = std::min(rep_bb.min.z, b.min.z);
              rep_bb.max.x = std::max(rep_bb.max.x, b.max.x); rep_bb.max.y = std::max(rep_bb.max.y, b.max.y); rep_bb.max.z = std::max(rep_bb.max.z, b.max.z);
            }
          }
          if (!have_bb) continue;
          def.base = Centroid(rep_bb);
          for (ObjectId id : rep_ids) {
            const SceneObject* o = ctx.Doc().Find(id);
            if (!o) continue;
            SceneObject c = *o;
            c.selected = false;
            c.group_id = -1;
            c.user_text.erase("Block");
            c.user_text.erase("BlockInsert");
            def.objects.push_back(c);
          }
          if (BlockDefinition* existing = ctx.Doc().FindBlock(block_name)) *existing = def; else ctx.Doc().Blocks().push_back(def);
          // Tag every matched occurrence - including the representative one
          // - as a grouped instance of the new block, in place: this reuses
          // TagExistingAsBlockInstance, which shares its Block/BlockInsert
          // tagging convention and CreateGroup call with InstantiateBlock
          // (cmd_drafting.cpp / the Block/Insert commands), but leaves the
          // occurrence's own geometry untouched rather than replacing it
          // with a translated copy of the representative - InstantiateBlock
          // only supports a translation instance transform, so calling it
          // here for a rotated occurrence would silently discard that
          // rotation and redraw it as the representative's un-rotated
          // shape, which is a real geometry-corrupting bug, not a
          // simplification.
          for (const std::vector<ObjectId>& occurrence : p.instances) {
            kernel::BoundingBox occ_bb;
            bool occ_have = false;
            for (ObjectId id : occurrence) {
              const SceneObject* o = ctx.Doc().Find(id);
              if (!o) continue;
              const kernel::BoundingBox b = o->BoundingBox();
              if (!occ_have) { occ_bb = b; occ_have = true; }
              else {
                occ_bb.min.x = std::min(occ_bb.min.x, b.min.x); occ_bb.min.y = std::min(occ_bb.min.y, b.min.y); occ_bb.min.z = std::min(occ_bb.min.z, b.min.z);
                occ_bb.max.x = std::max(occ_bb.max.x, b.max.x); occ_bb.max.y = std::max(occ_bb.max.y, b.max.y); occ_bb.max.z = std::max(occ_bb.max.z, b.max.z);
              }
            }
            if (!occ_have) continue;
            TagExistingAsBlockInstance(ctx, occurrence, block_name, Centroid(occ_bb));
            ++converted_instances;
          }
          ++converted_groups;
        }
        Cache().patterns.clear();
        Cache().revision = ctx.Doc().Revision();
        ctx.Print("SmartBlockConvert: " + std::to_string(converted_groups) + " block definition(s) created, " +
                  std::to_string(converted_instances) + " instance(s) converted");
      }),
      CommandStatus::Implemented,
      "Converts every group SmartBlockDetect found (or re-runs detection first if none is cached) into a new "
      "BlockDefinition (from the representative occurrence) plus a tagged, grouped instance at every matched "
      "occurrence, via TagExistingAsBlockInstance - the same Block/BlockInsert tagging and CreateGroup call "
      "InstantiateBlock uses (cmd_drafting.cpp) - so block creation/tagging itself is not reimplemented here, only "
      "the detection that decides which existing objects to feed it. Occurrences are tagged in place rather than "
      "replaced by a transformed copy of the representative, because BlockDefinition/InstantiateBlock only support "
      "a translation instance transform: replacing a rotated occurrence's geometry that way would silently discard "
      "its rotation.");
}

}  // namespace dino8::app
