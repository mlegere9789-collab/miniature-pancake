#pragma once

#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::kernel {

// Wraps ON_PointCloud - OpenNURBS' own point-set geometry class, the same
// one its own .3dm reader/writer already round-trips (ON_BinaryArchive's
// object table dispatches on ON_Geometry::Cast the same way every other
// object kind here does). A real point-cloud representation, not a
// grouped set of separate Point objects: one array of positions plus,
// per point cloud (not per point - ON_PointCloud's own convention, see
// HasPointColors()/HasPointNormals() below), an optional parallel array
// of per-point colors and an optional parallel array of per-point
// normals.
//
// Deliberately scoped down from Rhino's own PointCloud object: this
// wrapper covers position + per-point color + per-point normal (what a
// viewport draw, a Save/Open round-trip, and a Move/Rotate/Scale
// transform all genuinely need) and leaves out ON_PointCloud's own
// per-point "hidden" flag and per-point value channel (m_H/m_V) - both
// exist to support interactively editing individual points within a
// cloud (grip-dragging/hiding one point at a time), and nothing in this
// app's UI or command set can select a sub-point of a point cloud (only
// pick/move/delete the whole object, the same granularity every other
// object kind here already has) - so there's nothing that would ever
// read or write those two fields. Honestly noted here, in
// PointCloud::PointCount()'s own doc comment on SceneObject.h, and in
// the PointCloud command's own registration text, rather than silently
// left unimplemented.
class PointCloud {
 public:
  int PointCount() const { return cloud_.PointCount(); }
  void AppendPoint(const Point3d& p) { cloud_.AppendPoint(p); }

  Point3d PointAt(int i) const { return cloud_.m_P[i]; }
  void SetPointAt(int i, const Point3d& p) { cloud_.m_P[i] = p; }

  // True only when there is exactly one color per point (ON_PointCloud's
  // own "all or nothing" convention - a partially-set m_C is read back as
  // "no colors at all", never silently truncated or index-out-of-range).
  bool HasColors() const { return cloud_.HasPointColors(); }
  // `colors.size()` must equal PointCount(); anything else clears the
  // per-point colors entirely rather than partially setting them (same
  // "all or nothing" convention ON_PointCloud itself enforces).
  void SetColors(const std::vector<ON_Color>& colors);
  ON_Color ColorAt(int i) const { return cloud_.m_C[i]; }

  bool HasNormals() const { return cloud_.HasPointNormals(); }
  void SetNormals(const std::vector<Vector3d>& normals);
  Vector3d NormalAt(int i) const { return cloud_.m_N[i]; }

  // Axis-aligned bounding box over every point. Throws
  // std::invalid_argument on an empty cloud, matching Mesh::GetBoundingBox's
  // own convention for "nothing to bound" rather than returning a
  // degenerate all-zero box that would look like a real point at the
  // origin.
  BoundingBox GetBoundingBox() const;

  // Applies `xform` to a copy of this cloud (points, and normals if
  // present - ON_PointCloud::Transform already handles both, including
  // re-normalizing transformed normals) and returns it, the same
  // "returns a transformed copy" convention Mesh::Transform() and
  // NurbsCurve/NurbsSurface's own in-place-vs-Brep::Transform split use
  // elsewhere in this kernel.
  PointCloud Transform(const ON_Xform& xform) const;

  const ON_PointCloud& raw() const { return cloud_; }
  ON_PointCloud& raw() { return cloud_; }

 private:
  ON_PointCloud cloud_;
};

}  // namespace dino8::kernel
