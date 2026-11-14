// Sub-object selection: control points / vertices, edges and faces of one
// object, selected on top of the whole-object selection (Rhino's
// Ctrl+Shift picking and PointsOn control-point editing).
//
// Index conventions (SubObjectRef::index / index2):
//   Vertex  curve: control point i          surface: i * CVCountV + j
//           mesh: vertex index               SubD: control-net vertex index
//   Edge    brep: ON_Brep::m_E index         mesh / SubD: (index, index2) = the two vertex indices
//   Face    brep: ON_Brep::m_F index         mesh: face index   SubD: control-net face index
//           surface: 0 (the whole surface)
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "doc/SceneObject.h"

namespace dino8::app {

enum class SubObjectKind { Vertex, Edge, Face };

const char* SubObjectKindName(SubObjectKind k);

struct SubObjectRef {
  ObjectId id = kNoObject;
  SubObjectKind kind = SubObjectKind::Vertex;
  int index = -1;
  int index2 = -1;  // second vertex of a mesh / SubD edge, else -1

  static SubObjectRef Vertex(ObjectId id, int i) { return {id, SubObjectKind::Vertex, i, -1}; }
  static SubObjectRef Face(ObjectId id, int i) { return {id, SubObjectKind::Face, i, -1}; }
  static SubObjectRef BrepEdge(ObjectId id, int i) { return {id, SubObjectKind::Edge, i, -1}; }
  static SubObjectRef MeshEdge(ObjectId id, int a, int b) { return {id, SubObjectKind::Edge, std::min(a, b), std::max(a, b)}; }

  bool operator==(const SubObjectRef& o) const { return id == o.id && kind == o.kind && index == o.index && index2 == o.index2; }
  bool operator!=(const SubObjectRef& o) const { return !(*this == o); }
};

class SubObjectSelection {
 public:
  const std::vector<SubObjectRef>& Items() const { return items_; }
  bool Empty() const { return items_.empty(); }
  size_t Size() const { return items_.size(); }
  void Clear() { items_.clear(); }
  bool Contains(const SubObjectRef& r) const { return std::find(items_.begin(), items_.end(), r) != items_.end(); }
  void Add(const SubObjectRef& r) { if (!Contains(r)) items_.push_back(r); }
  void Remove(const SubObjectRef& r) { items_.erase(std::remove(items_.begin(), items_.end(), r), items_.end()); }
  void Toggle(const SubObjectRef& r) { if (Contains(r)) Remove(r); else Add(r); }
  void RemoveObject(ObjectId id) { items_.erase(std::remove_if(items_.begin(), items_.end(), [id](const SubObjectRef& r) { return r.id == id; }), items_.end()); }
  // Objects that have at least one selected sub-object (unique, in order).
  std::vector<ObjectId> ObjectIds() const {
    std::vector<ObjectId> ids;
    for (const SubObjectRef& r : items_) if (std::find(ids.begin(), ids.end(), r.id) == ids.end()) ids.push_back(r.id);
    return ids;
  }
  std::vector<SubObjectRef> ItemsOf(ObjectId id) const {
    std::vector<SubObjectRef> out;
    for (const SubObjectRef& r : items_) if (r.id == id) out.push_back(r);
    return out;
  }
  size_t CountOf(SubObjectKind k) const { size_t n = 0; for (const SubObjectRef& r : items_) if (r.kind == k) ++n; return n; }

 private:
  std::vector<SubObjectRef> items_;
};

}  // namespace dino8::app
