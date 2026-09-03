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

}  // namespace dino8::kernel
