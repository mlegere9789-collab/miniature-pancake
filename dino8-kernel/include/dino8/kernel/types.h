#pragma once

#include <array>

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

// Exact volume mass properties of a closed solid at unit density.
// Originally mesh.h-only (Mesh::VolumeMassProperties()'s own return
// type); moved here so brep.h can use it too, for
// Brep::VolumeMassProperties() below (brep.h can't include mesh.h -
// mesh.h already includes brep.h, so that direction would be circular).
// Every second moment below uses the "products of inertia" convention:
// `ixx = integral of (y^2 + z^2) dV`, `ixy = integral of (x * y) dV`
// (NOT its negative), so the inertia tensor is assembled as
//
//   I = [[ ixx, -ixy, -ixz ],
//        [ -ixy, iyy, -iyz ],
//        [ -ixz, -iyz, izz ]]
//
// - the convention Rhino's own MassProperties reports and every
// engineering reference tabulates. Multiply every moment by the actual
// density to get real mass moments (volume by density gives mass).
struct MassProperties {
  double volume = 0;
  Point3d centroid;

  // Second moments about the WORLD ORIGIN.
  double ixx_origin = 0, iyy_origin = 0, izz_origin = 0;
  double ixy_origin = 0, iyz_origin = 0, ixz_origin = 0;

  // Second moments about the CENTROID (parallel-axis theorem applied to
  // the origin moments above).
  double ixx = 0, iyy = 0, izz = 0;
  double ixy = 0, iyz = 0, ixz = 0;

  // Eigen-decomposition of the centroidal tensor: `principal_moments`
  // in ascending order, `principal_axes[k]` the unit axis of
  // `principal_moments[k]`. The three axes form a right-handed
  // orthonormal frame (the third is the cross product of the first two,
  // which is still an eigenvector). An eigenvector's sign is arbitrary
  // (it's an axis, not a direction), and for a repeated eigenvalue
  // (a body with an axis of rotational symmetry, e.g. a cylinder or
  // torus) any orthonormal pair in that eigenspace is equally valid -
  // callers must not assume a specific pair comes back in that case.
  std::array<double, 3> principal_moments{};
  std::array<Vector3d, 3> principal_axes{};

  // `sqrt(principal_moments[k] / volume)` - the distance from the
  // principal axis at which the whole volume, concentrated, would have
  // the same moment.
  std::array<double, 3> radii_of_gyration{};
};

}  // namespace dino8::kernel
