#include "dino8/kernel/vertex_weights.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace dino8::kernel {
namespace {

void CheckGroup(const VertexGroupSet& set, int group, const char* who) {
  if (group < 0 || group >= set.GroupCount()) throw std::invalid_argument(std::string("dino8::kernel::") + who + ": group index out of range");
}

void CheckVertex(const VertexGroupSet& set, int vertex, const char* who) {
  if (vertex < 0 || vertex >= set.VertexCount()) throw std::invalid_argument(std::string("dino8::kernel::") + who + ": vertex index out of range");
}

// case-insensitive natural comparison: digit runs compare as numbers
int NaturalCompare(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
      size_t i2 = i, j2 = j;
      while (i2 < a.size() && std::isdigit(static_cast<unsigned char>(a[i2]))) ++i2;
      while (j2 < b.size() && std::isdigit(static_cast<unsigned char>(b[j2]))) ++j2;
      std::string na = a.substr(i, i2 - i), nb = b.substr(j, j2 - j);
      na.erase(0, std::min(na.find_first_not_of('0'), na.size()));
      nb.erase(0, std::min(nb.find_first_not_of('0'), nb.size()));
      if (na.size() != nb.size()) return na.size() < nb.size() ? -1 : 1;
      if (na != nb) return na < nb ? -1 : 1;
      i = i2;
      j = j2;
    } else {
      const int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[j]));
      if (ca != cb) return ca < cb ? -1 : 1;
      ++i;
      ++j;
    }
  }
  if (i < a.size()) return 1;
  if (j < b.size()) return -1;
  return 0;
}

}  // namespace

bool VertexGroupSet::Weight(int vertex, int group, float& weight) const {
  if (vertex < 0 || vertex >= VertexCount()) return false;
  const auto& m = weights[static_cast<size_t>(vertex)];
  auto it = m.find(group);
  if (it == m.end()) return false;
  weight = it->second;
  return true;
}

int AddVertexGroup(VertexGroupSet& set, const std::string& name) {
  set.names.push_back(name);
  if (set.names.size() == 1) set.active = 0;
  return static_cast<int>(set.names.size()) - 1;
}

void AssignToGroup(VertexGroupSet& set, int group, const std::vector<int>& vertices, float weight) {
  CheckGroup(set, group, "AssignToGroup");
  if (!(weight >= 0.0f && weight <= 1.0f)) throw std::invalid_argument("dino8::kernel::AssignToGroup: weight must be in [0, 1]");
  for (int v : vertices) CheckVertex(set, v, "AssignToGroup");
  for (int v : vertices) set.weights[static_cast<size_t>(v)][group] = weight;
}

void RemoveFromGroup(VertexGroupSet& set, int group, const std::vector<int>& vertices) {
  CheckGroup(set, group, "RemoveFromGroup");
  for (int v : vertices) CheckVertex(set, v, "RemoveFromGroup");
  for (int v : vertices) set.weights[static_cast<size_t>(v)].erase(group);
}

void PasteWeightToVertices(VertexGroupSet& set, int group, int from_vertex, const std::vector<int>& to_vertices) {
  CheckGroup(set, group, "PasteWeightToVertices");
  CheckVertex(set, from_vertex, "PasteWeightToVertices");
  float w = 0.0f;
  if (!set.Weight(from_vertex, group, w)) throw std::invalid_argument("dino8::kernel::PasteWeightToVertices: the source vertex is not in the group");
  for (int v : to_vertices) CheckVertex(set, v, "PasteWeightToVertices");
  for (int v : to_vertices) set.weights[static_cast<size_t>(v)][group] = w;
}

void InvertGroup(VertexGroupSet& set, int group, bool add_missing, bool remove_zero) {
  CheckGroup(set, group, "InvertGroup");
  for (auto& m : set.weights) {
    auto it = m.find(group);
    if (it == m.end()) {
      if (!add_missing) continue;
      it = m.emplace(group, 0.0f).first;
    }
    it->second = 1.0f - it->second;
    if (remove_zero && it->second == 0.0f) m.erase(it);
  }
}

void NormalizeGroup(VertexGroupSet& set, int group) {
  CheckGroup(set, group, "NormalizeGroup");
  float top = 0.0f;
  for (const auto& m : set.weights) {
    auto it = m.find(group);
    if (it != m.end()) top = std::max(top, it->second);
  }
  if (!(top > 0.0f)) return;
  for (auto& m : set.weights) {
    auto it = m.find(group);
    if (it != m.end()) it->second = std::min(1.0f, it->second / top);
  }
}

void QuantizeGroup(VertexGroupSet& set, int group, int steps) {
  CheckGroup(set, group, "QuantizeGroup");
  if (steps < 1) throw std::invalid_argument("dino8::kernel::QuantizeGroup: steps must be >= 1");
  for (auto& m : set.weights) {
    auto it = m.find(group);
    if (it != m.end()) it->second = static_cast<float>(std::floor(static_cast<double>(it->second) * steps + 0.5) / steps);
  }
}

void LimitGroupsPerVertex(VertexGroupSet& set, int limit) {
  if (limit < 0) throw std::invalid_argument("dino8::kernel::LimitGroupsPerVertex: limit must be >= 0");
  for (auto& m : set.weights) {
    if (static_cast<int>(m.size()) <= limit) continue;
    std::vector<std::pair<int, float>> entries(m.begin(), m.end());
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    m.clear();
    for (int k = 0; k < limit; ++k) m.insert(entries[static_cast<size_t>(k)]);
  }
}

void CleanGroup(VertexGroupSet& set, int group, float limit, bool keep_single) {
  CheckGroup(set, group, "CleanGroup");
  for (auto& m : set.weights) {
    auto it = m.find(group);
    if (it == m.end() || it->second > limit) continue;
    if (keep_single && m.size() == 1) continue;
    m.erase(it);
  }
}

void SortGroupsByName(VertexGroupSet& set) {
  std::vector<int> order(set.names.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return NaturalCompare(set.names[static_cast<size_t>(a)], set.names[static_cast<size_t>(b)]) < 0; });
  std::vector<int> new_index(order.size());
  for (size_t k = 0; k < order.size(); ++k) new_index[static_cast<size_t>(order[k])] = static_cast<int>(k);
  std::vector<std::string> names;
  for (int old : order) names.push_back(set.names[static_cast<size_t>(old)]);
  for (auto& m : set.weights) {
    std::map<int, float> remapped;
    for (const auto& [g, w] : m) remapped[new_index[static_cast<size_t>(g)]] = w;
    m = remapped;
  }
  if (set.active >= 0 && set.active < static_cast<int>(new_index.size())) set.active = new_index[static_cast<size_t>(set.active)];
  set.names = names;
}

std::vector<int> UngroupedVertices(const VertexGroupSet& set) {
  std::vector<int> out;
  for (size_t v = 0; v < set.weights.size(); ++v)
    if (set.weights[v].empty()) out.push_back(static_cast<int>(v));
  return out;
}

float MapWeight(WeightFalloff falloff, float w) {
  switch (falloff) {
    case WeightFalloff::Linear: return w;
    case WeightFalloff::Sharp: return w * w;
    case WeightFalloff::Smooth: return 3.0f * w * w - 2.0f * w * w * w;
    case WeightFalloff::Root: return std::sqrt(w);
    case WeightFalloff::Sphere: return std::sqrt(std::max(0.0f, 2.0f * w - w * w));
    case WeightFalloff::Invert: return 1.0f - w;
    case WeightFalloff::Step: return w >= 0.5f ? 1.0f : 0.0f;
  }
  return w;
}

namespace {
float RampWeight(double d, double lo, double hi) {
  return static_cast<float>(std::max(0.0, std::min(1.0, (d - lo) / (hi - lo))));
}
}  // namespace

std::vector<float> ProximityWeightsToPoint(const Mesh& mesh, Point3d target, double min_distance, double max_distance) {
  if (!(std::abs(max_distance - min_distance) > 0.0)) throw std::invalid_argument("dino8::kernel::ProximityWeightsToPoint: the two distances must differ");
  std::vector<float> out;
  const ON_Mesh& r = mesh.raw();
  for (int i = 0; i < r.m_V.Count(); ++i) out.push_back(RampWeight(Point3d(r.m_V[i]).DistanceTo(target), min_distance, max_distance));
  return out;
}

std::vector<float> ProximityWeightsToMesh(const Mesh& mesh, const Mesh& target_surface, double min_distance, double max_distance) {
  if (!(std::abs(max_distance - min_distance) > 0.0)) throw std::invalid_argument("dino8::kernel::ProximityWeightsToMesh: the two distances must differ");
  std::vector<float> out;
  const ON_Mesh& r = mesh.raw();
  for (int i = 0; i < r.m_V.Count(); ++i) {
    const Point3d p(r.m_V[i]);
    out.push_back(RampWeight(p.DistanceTo(target_surface.ClosestPoint(p)), min_distance, max_distance));
  }
  return out;
}

std::vector<float> GradientWeights(const Mesh& mesh, Point3d start, Point3d end, float start_weight, float end_weight, GradientKind kind) {
  const Vector3d axis = end - start;
  const double len2 = axis * axis;
  if (!(len2 > 0.0)) throw std::invalid_argument("dino8::kernel::GradientWeights: start and end coincide");
  std::vector<float> out;
  const ON_Mesh& r = mesh.raw();
  for (int i = 0; i < r.m_V.Count(); ++i) {
    const Vector3d d = Point3d(r.m_V[i]) - start;
    double t = kind == GradientKind::Linear ? (d * axis) / len2 : d.Length() / std::sqrt(len2);
    t = std::max(0.0, std::min(1.0, t));
    out.push_back(static_cast<float>(start_weight + t * (end_weight - start_weight)));
  }
  return out;
}

VertexGroupSet CopyVertexGroups(const VertexGroupSet& source, int vertex_count) {
  if (vertex_count != source.VertexCount()) throw std::invalid_argument("dino8::kernel::CopyVertexGroups: the vertex counts differ");
  return source;
}

std::vector<float> FloodFillMask(const std::vector<float>& mask, MaskFill mode, float value) {
  std::vector<float> out(mask.size());
  for (size_t i = 0; i < mask.size(); ++i)
    out[i] = mode == MaskFill::Value ? value : (mode == MaskFill::ValueInverse ? 1.0f - value : 1.0f - mask[i]);
  return out;
}

std::vector<float> FilterMask(const Mesh& mesh, const std::vector<float>& mask, MaskFilterKind kind, int iterations) {
  const ON_Mesh& r = mesh.raw();
  if (static_cast<int>(mask.size()) != r.m_V.Count()) throw std::invalid_argument("dino8::kernel::FilterMask: one mask value per vertex is required");
  if (iterations < 0) throw std::invalid_argument("dino8::kernel::FilterMask: iterations must be >= 0");
  std::vector<std::vector<int>> nb(mask.size());
  for (int i = 0; i < r.m_F.Count(); ++i) {
    const ON_MeshFace& f = r.m_F[i];
    const int n = f.IsQuad() ? 4 : 3;
    for (int k = 0; k < n; ++k) {
      const int a = f.vi[k], b = f.vi[(k + 1) % n];
      nb[static_cast<size_t>(a)].push_back(b);
      nb[static_cast<size_t>(b)].push_back(a);
    }
  }
  for (auto& l : nb) {
    std::sort(l.begin(), l.end());
    l.erase(std::unique(l.begin(), l.end()), l.end());
  }
  std::vector<float> cur = mask;
  for (int it = 0; it < iterations; ++it) {
    std::vector<float> next = cur;
    for (size_t v = 0; v < cur.size(); ++v) {
      if (nb[v].empty()) continue;
      if (kind == MaskFilterKind::Smooth) {
        double sum = 0;
        for (int w : nb[v]) sum += cur[static_cast<size_t>(w)];
        next[v] = static_cast<float>(sum / static_cast<double>(nb[v].size()));
      } else {
        float best = cur[v];
        for (int w : nb[v]) best = kind == MaskFilterKind::Grow ? std::max(best, cur[static_cast<size_t>(w)]) : std::min(best, cur[static_cast<size_t>(w)]);
        next[v] = best;
      }
    }
    cur = next;
  }
  return cur;
}

}  // namespace dino8::kernel
