// Object-transform algebra and placement helpers (header only): the matrix and point arithmetic behind the object-level edits of a
// modeler - clear location / rotation / scale, parenting with and without a parent inverse, copy and paste of global and relative
// transforms, delta transforms, snapping a selection or a cursor to a grid / the selection / a target - and a few point maps of the
// interactive transform tools (slide, push/pull, tilt). Everything is a pure function of its arguments: no document, no selection, no UI.
//
// Conventions. Matrices are ON_Xform acting on column points, so `A * B` applies B first. An object's local matrix is
// T(location) * R(rotation) * S(scale); a child's world matrix is  parent_world * parent_inverse * local.
#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "dino8/kernel/types.h"

namespace dino8::kernel {

// ---------------------------------------------------------------------------------------------------------------------------------
// Location / rotation / scale parts of a matrix.

struct TrsParts {
  Vector3d location;
  ON_Xform rotation = ON_Xform::IdentityTransformation;  // a pure rotation (upper 3 x 3 orthonormal, determinant +1)
  Vector3d scale = Vector3d(1, 1, 1);
};

inline ON_Xform ComposeTrs(const TrsParts& p) {
  ON_Xform m = p.rotation;
  for (int row = 0; row < 3; ++row) {
    m[row][0] *= p.scale.x;
    m[row][1] *= p.scale.y;
    m[row][2] *= p.scale.z;
  }
  m[0][3] = p.location.x;
  m[1][3] = p.location.y;
  m[2][3] = p.location.z;
  return m;
}

// Splits an affine matrix with no shear into T * R * S: the location is the last column, the scale the lengths of the three columns of the
// upper 3 x 3, the rotation the normalised columns. A matrix of negative determinant (a mirror) gets a negative scale on all three axes and the
// negated rotation, so that the rotation is always proper and ComposeTrs() gives the matrix back. Returns false for a singular matrix, for
// columns that are not mutually orthogonal (a shear), and for a projective last row.
inline bool DecomposeTrs(const ON_Xform& m, TrsParts& out, double tolerance = 1e-9) {
  if (std::fabs(m[3][0]) > tolerance || std::fabs(m[3][1]) > tolerance || std::fabs(m[3][2]) > tolerance || std::fabs(m[3][3] - 1.0) > tolerance) return false;
  Vector3d c[3];
  for (int j = 0; j < 3; ++j) c[j] = Vector3d(m[0][j], m[1][j], m[2][j]);
  const double len[3] = {c[0].Length(), c[1].Length(), c[2].Length()};
  if (!(len[0] > tolerance) || !(len[1] > tolerance) || !(len[2] > tolerance)) return false;
  for (int j = 0; j < 3; ++j) c[j] = c[j] * (1.0 / len[j]);
  if (std::fabs(ON_DotProduct(c[0], c[1])) > tolerance || std::fabs(ON_DotProduct(c[0], c[2])) > tolerance || std::fabs(ON_DotProduct(c[1], c[2])) > tolerance) return false;
  const double det = ON_DotProduct(ON_CrossProduct(c[0], c[1]), c[2]);
  const double sign = det < 0.0 ? -1.0 : 1.0;
  ON_Xform r = ON_Xform::IdentityTransformation;
  for (int j = 0; j < 3; ++j)
    for (int i = 0; i < 3; ++i) r[i][j] = sign * c[j][i];
  out.rotation = r;
  out.scale = Vector3d(sign * len[0], sign * len[1], sign * len[2]);
  out.location = Vector3d(m[0][3], m[1][3], m[2][3]);
  return true;
}

// Clear Location / Rotation / Scale of a local matrix: the other two parts are kept. Throws std::invalid_argument when the matrix has no
// T * R * S form (singular or sheared).
inline ON_Xform ClearLocation(const ON_Xform& local) {
  TrsParts p;
  if (!DecomposeTrs(local, p)) throw std::invalid_argument("dino8::kernel::ClearLocation: the matrix is not a location / rotation / scale product");
  p.location = Vector3d(0, 0, 0);
  return ComposeTrs(p);
}
inline ON_Xform ClearRotation(const ON_Xform& local) {
  TrsParts p;
  if (!DecomposeTrs(local, p)) throw std::invalid_argument("dino8::kernel::ClearRotation: the matrix is not a location / rotation / scale product");
  p.rotation = ON_Xform::IdentityTransformation;
  return ComposeTrs(p);
}
inline ON_Xform ClearScale(const ON_Xform& local) {
  TrsParts p;
  if (!DecomposeTrs(local, p)) throw std::invalid_argument("dino8::kernel::ClearScale: the matrix is not a location / rotation / scale product");
  p.scale = Vector3d(1, 1, 1);
  return ComposeTrs(p);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Delta transforms. The effective matrix of an object is T(location + delta_location) * R(delta_rotation * rotation) *
// S(scale (componentwise) delta_scale). MoveToDeltas() moves the base location / rotation / scale into the deltas (each part on request) so that
// the effective matrix does not change.

struct ObjectTransform {
  Vector3d location, delta_location;
  ON_Xform rotation = ON_Xform::IdentityTransformation, delta_rotation = ON_Xform::IdentityTransformation;
  Vector3d scale = Vector3d(1, 1, 1), delta_scale = Vector3d(1, 1, 1);
};

inline ON_Xform EffectiveMatrix(const ObjectTransform& t) {
  TrsParts p;
  p.location = t.location + t.delta_location;
  p.rotation = t.delta_rotation * t.rotation;
  p.scale = Vector3d(t.scale.x * t.delta_scale.x, t.scale.y * t.delta_scale.y, t.scale.z * t.delta_scale.z);
  return ComposeTrs(p);
}

inline ObjectTransform MoveToDeltas(ObjectTransform t, bool location = true, bool rotation = true, bool scale = true) {
  if (location) {
    t.delta_location = t.delta_location + t.location;
    t.location = Vector3d(0, 0, 0);
  }
  if (rotation) {
    t.delta_rotation = t.delta_rotation * t.rotation;
    t.rotation = ON_Xform::IdentityTransformation;
  }
  if (scale) {
    t.delta_scale = Vector3d(t.delta_scale.x * t.scale.x, t.delta_scale.y * t.scale.y, t.delta_scale.z * t.scale.z);
    t.scale = Vector3d(1, 1, 1);
  }
  return t;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Parenting. world = parent_world * parent_inverse * local.

inline ON_Xform ChildWorldMatrix(const ON_Xform& parent_world, const ON_Xform& parent_inverse, const ON_Xform& local) { return parent_world * parent_inverse * local; }

// Make Parent (keep transform): the child stays where it is. The parent inverse is the inverse of the parent's world matrix and the child's
// local matrix is not touched, so world = P * P^-1 * local = local. Throws std::invalid_argument for a singular parent matrix.
inline ON_Xform ParentInverseForKeepTransform(const ON_Xform& parent_world) {
  ON_Xform inv = parent_world;
  if (!inv.Invert()) throw std::invalid_argument("dino8::kernel::ParentInverseForKeepTransform: the parent matrix is singular");
  return inv;
}

// Make Parent without inverse: the parent inverse is the identity, the child's world matrix becomes parent_world * local (it moves with the parent).
inline ON_Xform ParentInverseForNoInverse() { return ON_Xform::IdentityTransformation; }

// Apply Parent Inverse: the parent inverse is folded into the child's local matrix and reset to the identity, the world matrix is unchanged.
struct ParentState {
  ON_Xform parent_inverse = ON_Xform::IdentityTransformation;
  ON_Xform local = ON_Xform::IdentityTransformation;
};
inline ParentState ApplyParentInverse(const ParentState& s) {
  ParentState out;
  out.local = s.parent_inverse * s.local;
  out.parent_inverse = ON_Xform::IdentityTransformation;
  return out;
}

// Clear Parent. With keep_transform the child keeps its world matrix as its new local matrix; without it the local matrix stays as it is (the child
// drops to where its local matrix puts it in world space).
inline ON_Xform LocalAfterClearParent(const ON_Xform& parent_world, const ParentState& s, bool keep_transform) {
  return keep_transform ? ChildWorldMatrix(parent_world, s.parent_inverse, s.local) : s.local;
}

// Copy Global Transform / Paste Global Transform: the stored matrix is a world matrix; pasting it sets the local matrix such that the child's world
// matrix equals it: local = (parent_world * parent_inverse)^-1 * stored. Throws std::invalid_argument for a singular parent chain.
inline ON_Xform LocalForGlobalMatrix(const ON_Xform& parent_world, const ON_Xform& parent_inverse, const ON_Xform& global) {
  ON_Xform chain = parent_world * parent_inverse;
  if (!chain.Invert()) throw std::invalid_argument("dino8::kernel::LocalForGlobalMatrix: the parent chain is singular");
  return chain * global;
}

// Copy Relative Transform: the matrix of object A seen from object B, B^-1 * A (both world matrices). Throws std::invalid_argument for a singular B.
inline ON_Xform RelativeMatrix(const ON_Xform& world_a, const ON_Xform& world_b) {
  ON_Xform inv = world_b;
  if (!inv.Invert()) throw std::invalid_argument("dino8::kernel::RelativeMatrix: the reference matrix is singular");
  return inv * world_a;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Snapping and placement of points.

// Each coordinate to the nearest multiple of `spacing` (> 0). Exactly half-way values go to the larger multiple.
inline Point3d SnapToGrid(const Point3d& p, double spacing) {
  if (!(spacing > 0.0) || !std::isfinite(spacing)) throw std::invalid_argument("dino8::kernel::SnapToGrid: the spacing must be positive and finite");
  const auto snap = [spacing](double x) { return std::floor(x / spacing + 0.5) * spacing; };
  return Point3d(snap(p.x), snap(p.y), snap(p.z));
}

inline Point3d MedianCenter(const std::vector<Point3d>& pts) {
  if (pts.empty()) throw std::invalid_argument("dino8::kernel::MedianCenter: no points");
  Vector3d sum(0, 0, 0);
  for (const Point3d& p : pts) sum += Vector3d(p.x, p.y, p.z);
  sum = sum * (1.0 / static_cast<double>(pts.size()));
  return Point3d(sum.x, sum.y, sum.z);
}

inline Point3d BoundsCenter(const std::vector<Point3d>& pts) {
  if (pts.empty()) throw std::invalid_argument("dino8::kernel::BoundsCenter: no points");
  Point3d lo = pts[0], hi = pts[0];
  for (const Point3d& p : pts) {
    lo = Point3d(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
    hi = Point3d(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
  }
  return Point3d(0.5 * (lo.x + hi.x), 0.5 * (lo.y + hi.y), 0.5 * (lo.z + hi.z));
}

// Moves a selection onto `target`. keep_offset: the whole selection is translated so that `pivot` (its median or bounds centre, chosen by the caller)
// lands on the target and the points keep their positions relative to each other; otherwise every point goes to the target itself.
inline std::vector<Point3d> MoveSelectionTo(const std::vector<Point3d>& pts, const Point3d& pivot, const Point3d& target, bool keep_offset) {
  std::vector<Point3d> out;
  out.reserve(pts.size());
  for (const Point3d& p : pts) out.push_back(keep_offset ? Point3d(p.x + target.x - pivot.x, p.y + target.y - pivot.y, p.z + target.z - pivot.z) : target);
  return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Point maps of the interactive transform tools.

// Slide: the point moves toward `neighbor` along their edge by the fraction t of the edge (t = 1 reaches the neighbor, t < 0 moves away).
inline Point3d SlideAlongEdge(const Point3d& p, const Point3d& neighbor, double t) {
  return Point3d(p.x + t * (neighbor.x - p.x), p.y + t * (neighbor.y - p.y), p.z + t * (neighbor.z - p.z));
}

// Push/Pull: the point moves along the line to `center` by `distance` (toward the centre for a positive distance, away for a negative one). A point at
// the centre has no direction and stays.
inline Point3d PushPull(const Point3d& p, const Point3d& center, double distance) {
  Vector3d d = center - p;
  const double len = d.Length();
  if (!(len > 1e-300)) return p;
  d = d * (distance / len);
  return Point3d(p.x + d.x, p.y + d.y, p.z + d.z);
}

// Tilt of a curve frame: the normal turns about the tangent by `angle` radians (right-handed about the tangent). The tangent is unit and the normal
// perpendicular to it; both are normalised on entry. Returns the rotated unit normal.
inline Vector3d TiltNormal(Vector3d tangent, Vector3d normal, double angle) {
  tangent.Unitize();
  normal = normal - tangent * ON_DotProduct(normal, tangent);
  if (!normal.Unitize()) throw std::invalid_argument("dino8::kernel::TiltNormal: the normal is parallel to the tangent");
  return normal * std::cos(angle) + ON_CrossProduct(tangent, normal) * std::sin(angle);
}

}  // namespace dino8::kernel
