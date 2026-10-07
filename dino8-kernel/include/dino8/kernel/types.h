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

// A closed parameter-space interval [min, max], e.g. a curve's own
// parameter domain or one direction of a surface's. Plain struct for the
// same reason as `BoundingBox` above - `min`/`max` are two independent
// numbers a caller may want separately (e.g. `Domain().min` alone to seed
// a scan from the curve's start), not just as a matched pair.
struct Interval {
  double min;
  double max;
};

// Continuity order a Match operation enforces at a shared edge/point -
// Position (G0), Tangent (G1), Curvature (G2). Originally
// surface.h-only (NurbsSurface::MatchEdge()'s own `continuity`
// parameter); moved here so curve.h can use it too, for
// NurbsCurve::MatchEnd() below - the same three-level continuity
// concept, just applied to a curve's single end point instead of a
// surface's whole shared edge.
enum class MatchContinuity { Position, Tangent, Curvature };

// A plain 0-255 RGB triple, kept independent of ON_Color so a caller
// naming a color (a layer's, a vertex's, ...) doesn't need to know the
// OpenNURBS type underneath. Originally file_io.h-only (Model::AddLayer()'s
// `color` parameter); moved here so mesh.h can use it too, for
// Mesh::SetVertexColors() below.
struct Color {
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;
};

}  // namespace dino8::kernel
