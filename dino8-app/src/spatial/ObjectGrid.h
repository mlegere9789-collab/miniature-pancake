// A uniform spatial grid over document object bounding boxes, used to turn
// viewport picking (PickObject/PickPoint/PickControlPoint/PickSubObject/
// ObjectsInWindow in Viewport.cpp) from a linear scan of every object in the
// document into a query that only visits objects near the pick ray or
// selection box. With a handful of objects the difference is noise; with
// tens of thousands (see tests/stress.sh) the linear scan runs on every
// mouse-move hover and becomes the dominant per-frame cost - see
// tests/performance_notes.md for measured numbers.
//
// The grid is a broad phase only: QueryRay returns every object whose world
// AABB the ray's line actually passes through, and QueryBox every object
// whose AABB overlaps the query box - callers still run their existing
// precise wire/triangle/point math on the returned candidates. QueryRay is
// exact for "does the ray's infinite line cross this object's AABB", which
// is what PickObject/PickSubObject/ObjectsInWindow need; see the caveat in
// Viewport.cpp (RayCandidates) about the narrow case where a caller also
// accepts a hit within a screen-pixel tolerance of the ray rather than only
// an exact intersection.
#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "dino8/kernel/types.h"

namespace dino8::app {

class Document;
using ObjectId = std::uint64_t;

struct Ray;  // viewport/Camera.h

class ObjectGrid {
 public:
  // Rebuilds from doc.Objects() if `doc.Revision()` has changed since the
  // last call (or this is the first call) - cheap to call every pick since
  // the common case (nothing changed since the last pick) is a single
  // integer comparison.
  void EnsureFresh(const Document& doc);

  // Indices into doc.Objects() (as it stood at the last EnsureFresh) whose
  // grid cells the ray passes through, out to `max_distance` (world units)
  // or the grid's own extent, whichever is shorter. Order is not
  // meaningful; an index can only appear once. Callers must not call
  // EnsureFresh again (directly or via Document::PickGrid()) between
  // getting this list and using it, or the indices could refer to a
  // different, later object list - within one pick call this is naturally
  // true since nothing mutates the document mid-pick.
  //
  // Indices, not ObjectIds: Document::Find(id) is itself a linear scan (see
  // Document.cpp), so returning ids here and resolving each with Find would
  // silently put the O(n) cost right back - indices resolve in O(1) via
  // doc.Objects()[index].
  std::vector<std::size_t> QueryRay(const Ray& ray, double max_distance) const;

  // Indices into doc.Objects() (as of the last EnsureFresh) whose cell
  // range overlaps `box` (world-space AABB, e.g. the box swept out by
  // unprojecting a screen-space selection rectangle through the camera's
  // near/far distance - see Viewport::WindowWorldBox).
  std::vector<std::size_t> QueryBox(const kernel::BoundingBox& box) const;

  bool Empty() const { return object_count_ == 0; }

 private:
  struct CellKey {
    int x = 0, y = 0, z = 0;
    bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
  };
  struct CellKeyHash {
    std::size_t operator()(const CellKey& k) const {
      // A simple, well-distributed 3-int hash (Rhino's own spatial hashes
      // use the same shape); collisions just cost a little extra bucket
      // scanning, they never cause a wrong answer.
      std::size_t h = static_cast<std::size_t>(k.x) * 73856093u;
      h ^= static_cast<std::size_t>(k.y) * 19349663u;
      h ^= static_cast<std::size_t>(k.z) * 83492791u;
      return h;
    }
  };

  CellKey CellOf(kernel::Point3d p) const;
  void ForEachCellInBox(const kernel::BoundingBox& box, const std::function<void(const CellKey&)>& fn) const;

  kernel::Point3d origin_{0, 0, 0};
  double cell_size_ = 1.0;
  std::uint64_t built_for_revision_ = ~0ull;
  std::size_t object_count_ = 0;
  std::unordered_map<CellKey, std::vector<std::size_t>, CellKeyHash> cells_;
};

}  // namespace dino8::app
