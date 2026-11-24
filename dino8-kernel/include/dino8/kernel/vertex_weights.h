#pragma once

#include <map>
#include <string>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// Named vertex groups of a mesh: for each vertex the groups it belongs to and its weight in each. A vertex belongs to a group when it has
// an entry for it, whatever the weight (a weight of 0 is still membership). Weights are floats.
struct VertexGroupSet {
  std::vector<std::string> names;
  std::vector<std::map<int, float>> weights;  // per vertex: group index -> weight
  int active = 0;                             // index into names, or -1 when there is no group

  explicit VertexGroupSet(int vertex_count = 0) : weights(static_cast<size_t>(vertex_count)) {}
  int VertexCount() const { return static_cast<int>(weights.size()); }
  int GroupCount() const { return static_cast<int>(names.size()); }
  // false when the vertex is not in the group (or an index is out of range)
  bool Weight(int vertex, int group, float& weight) const;
};

// Adds an empty group and returns its index (it becomes the active group when it is the first).
int AddVertexGroup(VertexGroupSet& set, const std::string& name);

// Assign to Vertex Group: the listed vertices get `weight` (0..1) in `group`, replacing a previous weight. Throws
// std::invalid_argument for an index out of range or a weight outside [0, 1].
void AssignToGroup(VertexGroupSet& set, int group, const std::vector<int>& vertices, float weight);

// Remove from Vertex Group / Delete Weight: the listed vertices leave `group`.
void RemoveFromGroup(VertexGroupSet& set, int group, const std::vector<int>& vertices);

// Paste Weight to Selected: the weight that `from_vertex` has in `group` is given to every listed vertex (which joins the group); when
// `from_vertex` is not in the group the call throws std::invalid_argument.
void PasteWeightToVertices(VertexGroupSet& set, int group, int from_vertex, const std::vector<int>& to_vertices);

// Invert Vertex Group: every member's weight w becomes 1 - w. `add_missing` first gives the vertices that are not in the group the
// weight 0 (so they come out as 1); `remove_zero` afterwards drops the members whose weight is 0.
void InvertGroup(VertexGroupSet& set, int group, bool add_missing = true, bool remove_zero = true);

// Normalize Vertex Group: the weights of `group` are divided by the largest one so that the highest becomes 1 (a group whose weights are
// all 0 is left unchanged).
void NormalizeGroup(VertexGroupSet& set, int group);

// Quantize Vertex Weights: every weight of `group` becomes round(w * steps) / steps. Throws std::invalid_argument for steps < 1.
void QuantizeGroup(VertexGroupSet& set, int group, int steps);

// Limit Number of Weights per Vertex: every vertex keeps only its `limit` largest weights (over all groups); the others are removed
// and nothing is renormalised. Equal weights keep the group with the lower index. Throws std::invalid_argument for limit < 0.
void LimitGroupsPerVertex(VertexGroupSet& set, int limit);

// Clean Vertex Group Weights: the members of `group` whose weight is at most `limit` leave it; with `keep_single` a vertex that
// would end up in no group at all keeps its largest weight instead.
void CleanGroup(VertexGroupSet& set, int group, float limit, bool keep_single);

// Sort Vertex Groups by name, case-insensitively with digit runs compared as numbers; weights and the active group follow their group.
void SortGroupsByName(VertexGroupSet& set);

// Select Ungrouped: the vertices that are in no group, sorted.
std::vector<int> UngroupedVertices(const VertexGroupSet& set);

// WeightVG Edit falloff curves applied to a weight (0..1): Linear w, Sharp w^2, Smooth 3w^2 - 2w^3, Root sqrt(w), Sphere sqrt(2w - w^2),
// Invert 1 - w, Step (w >= 0.5 ? 1 : 0).
enum class WeightFalloff { Linear, Sharp, Smooth, Root, Sphere, Invert, Step };
float MapWeight(WeightFalloff falloff, float weight);

// WeightVG Proximity: for every vertex the weight (d - min_distance) / (max_distance - min_distance) clamped to [0, 1], d being the
// distance to `target` (a point), so min_distance maps to 0 and max_distance to 1 (min above max inverts the ramp). Throws
// std::invalid_argument when the two distances are equal.
std::vector<float> ProximityWeightsToPoint(const Mesh& mesh, Point3d target, double min_distance, double max_distance);
// The same with d the distance to the closest point of `target_surface`.
std::vector<float> ProximityWeightsToMesh(const Mesh& mesh, const Mesh& target_surface, double min_distance, double max_distance);

// Weight Gradient: linear (the projection of the vertex on start -> end, as a fraction clamped to [0, 1]) or radial (the distance
// from `start` divided by |end - start|, clamped to [0, 1]) interpolation from `start_weight` at the start to `end_weight` at the end.
// Throws std::invalid_argument when start and end coincide.
enum class GradientKind { Linear, Radial };
std::vector<float> GradientWeights(const Mesh& mesh, Point3d start, Point3d end, float start_weight, float end_weight, GradientKind kind);

}  // namespace dino8::kernel
