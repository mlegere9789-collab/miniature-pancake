#include "spatial/ObjectGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "doc/Document.h"
#include "viewport/Camera.h"  // Ray

namespace dino8::app {

ObjectGrid::CellKey ObjectGrid::CellOf(kernel::Point3d p) const {
  const double inv = 1.0 / cell_size_;
  return CellKey{static_cast<int>(std::floor((p.x - origin_.x) * inv)),
                 static_cast<int>(std::floor((p.y - origin_.y) * inv)),
                 static_cast<int>(std::floor((p.z - origin_.z) * inv))};
}

void ObjectGrid::ForEachCellInBox(const kernel::BoundingBox& box, const std::function<void(const CellKey&)>& fn) const {
  const CellKey lo = CellOf(box.min);
  const CellKey hi = CellOf(box.max);
  // A query box spanning more than this many cells on a side almost always
  // means "the whole scene" (e.g. a selection rectangle around everything,
  // or a degenerate/huge box) - fall back to letting the caller iterate
  // every cell's contents once instead of looping millions of empty cells.
  constexpr int kMaxSpan = 4096;
  const int span_x = hi.x - lo.x, span_y = hi.y - lo.y, span_z = hi.z - lo.z;
  if (span_x > kMaxSpan || span_y > kMaxSpan || span_z > kMaxSpan || span_x < 0 || span_y < 0 || span_z < 0) {
    for (const auto& [key, ids] : cells_) fn(key);
    return;
  }
  for (int x = lo.x; x <= hi.x; ++x)
    for (int y = lo.y; y <= hi.y; ++y)
      for (int z = lo.z; z <= hi.z; ++z) fn(CellKey{x, y, z});
}

void ObjectGrid::EnsureFresh(const Document& doc) {
  if (built_for_revision_ == doc.Revision() && built_for_revision_ != ~0ull) return;
  built_for_revision_ = doc.Revision();
  cells_.clear();
  object_count_ = 0;

  const std::vector<SceneObject>& objects = doc.Objects();
  if (objects.empty()) return;

  kernel::BoundingBox scene{objects.front().BoundingBox()};
  for (const SceneObject& o : objects) {
    const kernel::BoundingBox b = o.BoundingBox();
    scene.min.x = std::min(scene.min.x, b.min.x);
    scene.min.y = std::min(scene.min.y, b.min.y);
    scene.min.z = std::min(scene.min.z, b.min.z);
    scene.max.x = std::max(scene.max.x, b.max.x);
    scene.max.y = std::max(scene.max.y, b.max.y);
    scene.max.z = std::max(scene.max.z, b.max.z);
  }
  origin_ = scene.min;
  extent_ = scene;
  const double dx = std::max(0.0, scene.max.x - scene.min.x);
  const double dy = std::max(0.0, scene.max.y - scene.min.y);
  const double dz = std::max(0.0, scene.max.z - scene.min.z);
  const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  // Aim for a handful of objects per populated cell: a cell volume of
  // roughly (scene volume) / (object count), floored so a scene of tiny or
  // coincident objects still gets a usable (non-zero) cell size.
  const double volume = std::max(dx * dy * dz, diag * diag * diag * 1e-6);
  const double target = volume > 0 ? std::cbrt(volume / static_cast<double>(std::max<std::size_t>(1, objects.size())))
                                    : 1.0;
  cell_size_ = std::max(target, diag > 0 ? diag * 1e-4 : 1.0);
  if (!(cell_size_ > 0.0) || !std::isfinite(cell_size_)) cell_size_ = 1.0;

  for (std::size_t index = 0; index < objects.size(); ++index) {
    const kernel::BoundingBox b = objects[index].BoundingBox();
    const CellKey lo = CellOf(b.min);
    const CellKey hi = CellOf(b.max);
    for (int x = lo.x; x <= hi.x; ++x)
      for (int y = lo.y; y <= hi.y; ++y)
        for (int z = lo.z; z <= hi.z; ++z) cells_[CellKey{x, y, z}].push_back(index);
    ++object_count_;
  }
}

namespace {
// Per-axis setup for the Amanatides & Woo grid march below: which way the
// cell index steps, the ray parameter t at which it first crosses into the
// next cell on this axis, and how much t advances per further cell step.
void SetupAxis(double origin_component, double dir_component, double grid_origin_component, int cell_index,
               double cell_size, int& step, double& t_max, double& t_delta) {
  if (dir_component > 1e-12) {
    step = 1;
    const double next_boundary = grid_origin_component + (cell_index + 1) * cell_size;
    t_max = (next_boundary - origin_component) / dir_component;
    t_delta = cell_size / dir_component;
  } else if (dir_component < -1e-12) {
    step = -1;
    const double next_boundary = grid_origin_component + cell_index * cell_size;
    t_max = (next_boundary - origin_component) / dir_component;
    t_delta = cell_size / -dir_component;
  } else {
    step = 0;
    t_max = std::numeric_limits<double>::infinity();
    t_delta = std::numeric_limits<double>::infinity();
  }
}
}  // namespace

std::vector<std::size_t> ObjectGrid::QueryRay(const Ray& ray, double max_distance) const {
  std::vector<std::size_t> result;
  if (cells_.empty()) return result;
  std::unordered_set<std::size_t> seen;

  // Standard 3D DDA grid march (Amanatides & Woo 1987): step from cell to
  // cell along the ray, visiting exactly the cells the ray's *line* passes
  // through - independent of how many objects or cells the whole document
  // has, which is the actual algorithmic win over the old per-object scan.
  CellKey cell = CellOf(ray.origin);
  int step_x, step_y, step_z;
  double t_max_x, t_max_y, t_max_z, t_delta_x, t_delta_y, t_delta_z;
  SetupAxis(ray.origin.x, ray.direction.x, origin_.x, cell.x, cell_size_, step_x, t_max_x, t_delta_x);
  SetupAxis(ray.origin.y, ray.direction.y, origin_.y, cell.y, cell_size_, step_y, t_max_y, t_delta_y);
  SetupAxis(ray.origin.z, ray.direction.z, origin_.z, cell.z, cell_size_, step_z, t_max_z, t_delta_z);

  // Bound the march by both the caller's max_distance and a hard cell-count
  // cap, so a ray that is (nearly) parallel to the grid's bounding volume
  // and skims just past its edge forever cannot loop indefinitely.
  const int kMaxSteps = 1 << 16;
  double t = 0.0;
  for (int i = 0; i < kMaxSteps && t <= max_distance; ++i) {
    const auto it = cells_.find(cell);
    if (it != cells_.end()) {
      for (std::size_t index : it->second) {
        if (seen.insert(index).second) result.push_back(index);
      }
    }
    if (t_max_x < t_max_y) {
      if (t_max_x < t_max_z) {
        if (step_x == 0) break;
        cell.x += step_x;
        t = t_max_x;
        t_max_x += t_delta_x;
      } else {
        if (step_z == 0) break;
        cell.z += step_z;
        t = t_max_z;
        t_max_z += t_delta_z;
      }
    } else {
      if (t_max_y < t_max_z) {
        if (step_y == 0) break;
        cell.y += step_y;
        t = t_max_y;
        t_max_y += t_delta_y;
      } else {
        if (step_z == 0) break;
        cell.z += step_z;
        t = t_max_z;
        t_max_z += t_delta_z;
      }
    }
  }
  return result;
}

std::vector<std::size_t> ObjectGrid::QueryBox(const kernel::BoundingBox& box) const {
  std::vector<std::size_t> result;
  if (cells_.empty()) return result;
  std::unordered_set<std::size_t> seen;
  ForEachCellInBox(box, [&](const CellKey& key) {
    const auto it = cells_.find(key);
    if (it == cells_.end()) return;
    for (std::size_t index : it->second) {
      if (seen.insert(index).second) result.push_back(index);
    }
  });
  return result;
}

}  // namespace dino8::app
