#pragma once

// Vertex groups: named, per-vertex weight tables of a mesh, and the data operators Blender runs on them (Object Data properties, Vertex Groups
// panel and the Weights menu of Weight Paint / Edit mode: bpy.ops.object.vertex_group_* and vertex_weight_*).
//
// Model. A VertexGroups table belongs to a mesh with `vertex_count` vertices. It holds an ordered list of named groups; every group holds a
// weight (any finite number, normally 0..1) for each vertex that is a member of it. A vertex that is not a member has no weight at all, which
// is different from being a member with weight 0 ("assigned" versus "unassigned"): Weight() reports 0 for both, Contains() tells them apart.
// One group is the active group (-1 when there is none). Group indices are positions in the list.
//
// The operations here are pure data operations on that table (the part of each Blender operator that does not depend on a viewport, a
// selection state or an undo stack); the caller passes the vertex selection explicitly where an operator uses it.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "dino8/kernel/types.h"

namespace dino8::kernel {

class VertexGroups {
 public:
  explicit VertexGroups(int vertex_count) : vertex_count_(vertex_count) {
    if (vertex_count < 0) throw std::invalid_argument("VertexGroups: negative vertex count");
  }

  int VertexCount() const { return vertex_count_; }
  int GroupCount() const { return static_cast<int>(names_.size()); }
  const std::string& Name(int g) const { return names_.at(static_cast<size_t>(g)); }
  int ActiveGroup() const { return active_; }

  // object.vertex_group_add: a new empty group at the end of the list, which becomes the active group. Returns its index.
  int AddGroup(const std::string& name) {
    names_.push_back(name);
    weights_.emplace_back();
    active_ = GroupCount() - 1;
    return active_;
  }

  // object.vertex_group_set_active(group_index): refuses an index outside the list (returns false and changes nothing).
  bool SetActiveGroup(int g) {
    if (g < 0 || g >= GroupCount()) return false;
    active_ = g;
    return true;
  }

  bool Contains(int g, int v) const { return weights_.at(static_cast<size_t>(g)).count(v) != 0; }
  double Weight(int g, int v) const {
    const auto& m = weights_.at(static_cast<size_t>(g));
    auto it = m.find(v);
    return it == m.end() ? 0.0 : it->second;
  }
  int MemberCount(int g) const { return static_cast<int>(weights_.at(static_cast<size_t>(g)).size()); }

  // Number of groups vertex `v` is a member of.
  int GroupsOfVertex(int v) const {
    int n = 0;
    for (int g = 0; g < GroupCount(); ++g) n += Contains(g, v) ? 1 : 0;
    return n;
  }

  // object.vertex_group_assign: the listed vertices of the ACTIVE group get exactly `weight` (replacing any weight they had). Returns the
  // number of vertices written; does nothing (0) when there is no active group.
  int AssignActive(const std::vector<int>& vertices, double weight) {
    if (active_ < 0) return 0;
    int n = 0;
    for (int v : vertices) {
      CheckVertex(v);
      weights_[static_cast<size_t>(active_)][v] = weight;
      ++n;
    }
    return n;
  }

  // object.vertex_group_remove_from: the listed vertices stop being members of the ACTIVE group (use_all_groups = false) or of every group
  // (use_all_groups = true). Returns the number of memberships removed.
  int RemoveFrom(const std::vector<int>& vertices, bool use_all_groups = false) {
    int n = 0;
    for (int v : vertices) {
      CheckVertex(v);
      if (use_all_groups) {
        for (auto& m : weights_) n += static_cast<int>(m.erase(v));
      } else if (active_ >= 0) {
        n += static_cast<int>(weights_[static_cast<size_t>(active_)].erase(v));
      }
    }
    return n;
  }

  // object.vertex_weight_delete(weight_group, ...): removes vertex `v` from group `g` only if it is a member; returns whether it was.
  bool DeleteVertexWeight(int g, int v) {
    CheckVertex(v);
    return weights_.at(static_cast<size_t>(g)).erase(v) != 0;
  }

  // object.vertex_weight_paste(weight_group): the weight that `source_vertex` has in group `g` is written to every vertex of `targets` in
  // the same group (a target that was not a member becomes one). Returns the number of vertices written, or 0 when the source is not a member
  // of the group (there is nothing to paste).
  int PasteVertexWeight(int g, int source_vertex, const std::vector<int>& targets) {
    CheckVertex(source_vertex);
    if (!Contains(g, source_vertex)) return 0;
    const double w = Weight(g, source_vertex);
    int n = 0;
    for (int v : targets) {
      CheckVertex(v);
      weights_[static_cast<size_t>(g)][v] = w;
      ++n;
    }
    return n;
  }

  // object.vertex_group_normalize (active group): every weight is divided by the largest weight of the group, so the largest becomes 1.0 and
  // the ratios between the weights are unchanged. A group that is empty or whose largest weight is not positive is left unchanged (false).
  bool NormalizeActive() {
    if (active_ < 0) return false;
    auto& m = weights_[static_cast<size_t>(active_)];
    double mx = 0.0;
    for (const auto& kv : m) mx = std::max(mx, kv.second);
    if (!(mx > 0.0)) return false;
    for (auto& kv : m) kv.second /= mx;
    return true;
  }

  // object.vertex_group_normalize_all: for every vertex, the weights it has in all groups (or, with lock_active, in all groups except the
  // active one, which keeps its weight) are scaled so that the vertex' weights sum to 1. A vertex whose weights sum to 0 is left alone.
  void NormalizeAll(bool lock_active = false) {
    for (int v = 0; v < vertex_count_; ++v) {
      double locked = 0.0, free_sum = 0.0;
      for (int g = 0; g < GroupCount(); ++g) {
        if (!Contains(g, v)) continue;
        (lock_active && g == active_ ? locked : free_sum) += Weight(g, v);
      }
      if (!(free_sum > 0.0)) continue;
      const double scale = std::max(0.0, 1.0 - locked) / free_sum;
      for (int g = 0; g < GroupCount(); ++g) {
        if (!Contains(g, v) || (lock_active && g == active_)) continue;
        weights_[static_cast<size_t>(g)][v] *= scale;
      }
    }
  }

  // object.vertex_group_invert (active group, auto_assign, auto_remove): every member weight w becomes 1 - w; with auto_assign the vertices
  // that were not members are added with weight 1 first; with auto_remove the members whose new weight is 0 are removed afterwards.
  void InvertActive(bool auto_assign = true, bool auto_remove = true) {
    if (active_ < 0) return;
    auto& m = weights_[static_cast<size_t>(active_)];
    if (auto_assign)
      for (int v = 0; v < vertex_count_; ++v)
        if (!m.count(v)) m[v] = 0.0;  // not a member = weight 0 before the inversion, so it becomes 1
    for (auto& kv : m) kv.second = 1.0 - kv.second;
    if (auto_remove)
      for (auto it = m.begin(); it != m.end();) it = (it->second == 0.0) ? m.erase(it) : std::next(it);
  }

  // object.vertex_group_clean(group_select_mode = ACTIVE, limit, keep_single): removes the memberships whose weight is at most `limit`
  // (limit 0 removes the zero weights). With keep_single a membership is kept when it is the only group that vertex belongs to.
  // Returns the number of memberships removed.
  int CleanActive(double limit = 0.0, bool keep_single = false) {
    if (active_ < 0) return 0;
    auto& m = weights_[static_cast<size_t>(active_)];
    int removed = 0;
    for (auto it = m.begin(); it != m.end();) {
      const bool small = it->second <= limit;
      const bool lone = keep_single && GroupsOfVertex(it->first) == 1;
      if (small && !lone) {
        it = m.erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
    return removed;
  }

  // object.vertex_group_limit_total(limit): every vertex keeps only its `limit` highest-weighted memberships (all groups); the lower ones are
  // removed. Among equal weights the group with the lower index is kept. Returns the number of memberships removed.
  int LimitTotal(int limit = 4) {
    if (limit < 0) limit = 0;
    int removed = 0;
    for (int v = 0; v < vertex_count_; ++v) {
      std::vector<std::pair<double, int>> own;  // (weight, group)
      for (int g = 0; g < GroupCount(); ++g)
        if (Contains(g, v)) own.push_back({Weight(g, v), g});
      if (static_cast<int>(own.size()) <= limit) continue;
      std::stable_sort(own.begin(), own.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
      for (size_t k = static_cast<size_t>(limit); k < own.size(); ++k) {
        weights_[static_cast<size_t>(own[k].second)].erase(v);
        ++removed;
      }
    }
    return removed;
  }

  // object.vertex_group_quantize(steps): every member weight w of the active group becomes round(w * steps) / steps (halves round up), so the
  // weights fall on the grid 0, 1/steps, ..., 1. A member stays a member even if it rounds to 0.
  void QuantizeActive(int steps = 4) {
    if (active_ < 0 || steps < 1) return;
    for (auto& kv : weights_[static_cast<size_t>(active_)]) kv.second = std::floor(kv.second * steps + 0.5) / steps;
  }

  // object.vertex_group_sort(sort_type = NAME): the groups are reordered by name, case-insensitive with runs of digits compared as numbers
  // ("Leg.2" before "Leg.10"); the weights follow their group and the active group stays the same group.
  void SortByName() {
    std::vector<int> order(names_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return NaturalLess(names_[static_cast<size_t>(a)], names_[static_cast<size_t>(b)]); });
    std::vector<std::string> n2;
    std::vector<std::map<int, double>> w2;
    int new_active = -1;
    for (size_t i = 0; i < order.size(); ++i) {
      n2.push_back(names_[static_cast<size_t>(order[i])]);
      w2.push_back(weights_[static_cast<size_t>(order[i])]);
      if (order[i] == active_) new_active = static_cast<int>(i);
    }
    names_ = std::move(n2);
    weights_ = std::move(w2);
    active_ = new_active;
  }

  // object.vertex_group_mirror (mirror_weights, flip_group_names, all_groups = false): the weights of the active group are mirrored across the
  // plane x = 0 given the vertex positions: for every vertex pair (a, b) whose positions are each other's mirror image (within `tolerance`) the
  // two weights are swapped; a vertex on the plane (|x| <= tolerance) maps to itself and keeps its weight; a vertex with no mirror partner is
  // left unchanged. With flip_group_names the active group's name has ".L"/".R", "_L"/"_R", "-L"/"-R" suffix (also Left/Right, case kept)
  // swapped, e.g. "Arm.L" becomes "Arm.R". Returns the number of pairs swapped.
  int MirrorActiveX(const std::vector<Point3d>& positions, double tolerance = 1e-5, bool flip_group_names = false) {
    if (active_ < 0) return 0;
    if (static_cast<int>(positions.size()) != vertex_count_) throw std::invalid_argument("MirrorActiveX: positions do not match the vertex count");
    auto& m = weights_[static_cast<size_t>(active_)];
    int pairs = 0;
    std::vector<char> done(static_cast<size_t>(vertex_count_), 0);
    for (int a = 0; a < vertex_count_; ++a) {
      if (done[static_cast<size_t>(a)]) continue;
      const Point3d& p = positions[static_cast<size_t>(a)];
      int b = -1;
      for (int c = 0; c < vertex_count_; ++c) {
        const Point3d& q = positions[static_cast<size_t>(c)];
        if (std::abs(q.x + p.x) <= tolerance && std::abs(q.y - p.y) <= tolerance && std::abs(q.z - p.z) <= tolerance) {
          b = c;
          break;
        }
      }
      done[static_cast<size_t>(a)] = 1;
      if (b < 0 || b == a) continue;
      done[static_cast<size_t>(b)] = 1;
      const bool ha = m.count(a) != 0, hb = m.count(b) != 0;
      const double wa = ha ? m[a] : 0.0, wb = hb ? m[b] : 0.0;
      if (hb) m[a] = wb; else m.erase(a);
      if (ha) m[b] = wa; else m.erase(b);
      ++pairs;
    }
    if (flip_group_names) names_[static_cast<size_t>(active_)] = FlipSideName(names_[static_cast<size_t>(active_)]);
    return pairs;
  }

  // object.vertex_group_copy_to_selected: replaces the groups of `target` by those of this table (names, weights, active group) when both
  // meshes have the same vertex count, and refuses (false, `target` unchanged) otherwise.
  bool CopyTo(VertexGroups& target) const {
    if (target.vertex_count_ != vertex_count_) return false;
    target.names_ = names_;
    target.weights_ = weights_;
    target.active_ = active_;
    return true;
  }

  // mesh.select_ungrouped: the vertices that are not a member of any group (extend = false returns only these).
  std::vector<int> UngroupedVertices() const {
    std::vector<int> r;
    for (int v = 0; v < vertex_count_; ++v)
      if (GroupsOfVertex(v) == 0) r.push_back(v);
    return r;
  }

  // Name flip used by MirrorActiveX (".L" <-> ".R" and the same with "_" or "-" as separator, or the words Left / Right).
  static std::string FlipSideName(const std::string& name) {
    const char seps[] = {'.', '_', '-'};
    for (char s : seps) {
      for (const char* pair : {"LR", "RL", "lr", "rl"}) {
        const std::string tail = std::string(1, s) + pair[0];
        if (name.size() >= tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0) {
          std::string r = name;
          r[r.size() - 1] = pair[1];
          return r;
        }
      }
    }
    return name;
  }

 private:
  static bool NaturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
      if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
        size_t i2 = i, j2 = j;
        while (i2 < a.size() && std::isdigit(static_cast<unsigned char>(a[i2]))) ++i2;
        while (j2 < b.size() && std::isdigit(static_cast<unsigned char>(b[j2]))) ++j2;
        std::string na = a.substr(i, i2 - i), nb = b.substr(j, j2 - j);
        na.erase(0, std::min(na.find_first_not_of('0'), na.size()));
        nb.erase(0, std::min(nb.find_first_not_of('0'), nb.size()));
        if (na.size() != nb.size()) return na.size() < nb.size();
        if (na != nb) return na < nb;
        i = i2;
        j = j2;
      } else {
        const int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[j]));
        if (ca != cb) return ca < cb;
        ++i;
        ++j;
      }
    }
    return (a.size() - i) < (b.size() - j);
  }

  void CheckVertex(int v) const {
    if (v < 0 || v >= vertex_count_) throw std::out_of_range("VertexGroups: vertex index out of range");
  }

  int vertex_count_;
  int active_ = -1;
  std::vector<std::string> names_;
  std::vector<std::map<int, double>> weights_;
};

}  // namespace dino8::kernel
