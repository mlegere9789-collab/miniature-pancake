#pragma once

#include <opennurbs.h>

namespace dino8::kernel {

// Thin aliases over OpenNURBS' own math types. Kept as aliases (not
// wrapped) because they're value types with no invariants worth hiding —
// wrapping them would just add copies for no benefit.
using Point3d = ON_3dPoint;
using Vector3d = ON_3dVector;
using Point2d = ON_2dPoint;  // (u, v) parameter-space point, e.g. a trim loop vertex.

// Degree elevation and similar operations return this instead of a bare
// bool so callers can tell "no-op, already at that degree" apart from
// "failed" without inspecting OpenNURBS error state directly.
enum class Result {
  Ok,
  NoOpAlreadySatisfied,
  Failed,
};

// Axis-aligned bounding box: the component-wise min/max corners. A plain
// struct, not a class with invariants to maintain - `min` and `max` are
// two independent points a caller may need on their own (e.g. `max - min`
// for a diagonal, `min` alone as a coarse "is this to the left of X"
// test), not just as a matched pair.
struct BoundingBox {
  Point3d min;
  Point3d max;
};

}  // namespace dino8::kernel
