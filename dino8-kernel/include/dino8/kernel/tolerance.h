#pragma once

#include <algorithm>

namespace dino8::kernel {

// The kernel's tolerance policy - the ONE place the numeric constants
// every "is this the same point / is this planar / is this degenerate"
// decision in this kernel routes through, replacing the ad-hoc literals
// (`1e-6`, `1e-9`, `1e-12`, `1e-4`) that used to be scattered across
// mesh.cpp/brep.cpp/surface.cpp with no statement of what each one
// meant or why it had the value it did. The README's long-standing
// "Known gaps" note ("no tolerance-management policy defined yet") is
// closed by THIS file, in two deliberately separate steps:
//
//   1. (this pass) NAME and ROUTE. Every constant below carries the exact
//      numeric value the call site it replaces already had, so nothing
//      measurable changes - proven by the BooleanCombineGeneral sweep
//      (tests/general_boolean_sweep.cpp) producing byte-for-byte
//      identical output and the full smoke suite staying green before
//      and after the routing (see the README build-log entry).
//   2. (a later pass, once real modelling tolerances are decided) TUNE.
//      Because every call site now reads the same named value, changing
//      the policy is a one-line edit whose blast radius is knowable,
//      instead of a hunt for every literal that happens to spell 1e-6.
//
// Three PRIMITIVE tolerances, in the same three classes Parasolid's and
// ACIS's own tolerance models distinguish, plus the DERIVED constants
// that specific operations use (each defined in terms of a primitive, so
// tuning a primitive moves every derived value with it):
namespace tolerance {

// --- Primitives -----------------------------------------------------------

// Absolute distance, in model units: two 3D points closer than this are
// the same point. This is the single most load-bearing number in the
// kernel - it is what Mesh::MergeAndWeld() snaps tessellation seams
// with and what FromPlanarFaces()/FromMixedFaces() weld loop points into
// shared ON_BrepVertex records with (brep.cpp's kBrepWeldTolerance).
// Honest limitation, unchanged from before this policy existed: a genuine
// feature smaller than this (a 5e-7-long edge, say) mis-welds into its
// neighbour, and the value is NOT scaled by model size (a 1e-6 gap on a
// 1e6-unit model is far below double precision's own ~1e-10 there).
constexpr double kDistance = 1e-6;

// Relative distance: a dimensionless fraction of a local size (a ring's
// own extent, a cylinder's radius, an edge's length, a grid cell's
// width) below which a deviation counts as zero. Used wherever an
// absolute number would be wrong at the scale in question - e.g. the
// planarity test of a lofted ring compares out-of-plane deviation
// against `kRelative * ring extent`, not against kDistance.
constexpr double kRelative = 1e-6;

// Unit-vector alignment: two unit vectors are parallel when
// `1 - dot(a, b) <= kAlignment`. Expressed as a dot-product deficit rather
// than an angle because that is what every existing call site computes
// (no acos in the inner loop); the equivalent angle is
// acos(1 - kAlignment) ~= 1.41e-3 rad (~0.081 degrees). This is the
// kernel's ANGLE tolerance.
constexpr double kAlignment = 1e-6;

// --- Degeneracy floors ------------------------------------------------------

// A vector (cross product, normal, direction) whose length is at or below
// this is treated as the zero vector - "these three points are
// collinear", "this axis has no direction". Below kDistance by design: a
// cross product of two kDistance-long edges is ~1e-12, so a floor at
// kDistance would call every small-but-real triangle degenerate.
constexpr double kZeroVector = 1e-9;

// A scalar magnitude (a volume, a squared length, a scale) at or below
// this is treated as exactly zero. The smallest floor here; used only
// to guard divisions and "is there anything here at all" tests.
constexpr double kZero = 1e-12;

// --- Derived ----------------------------------------------------------------

// Vertex weld distance: Mesh::MergeAndWeld()'s default `tolerance` and
// brep.cpp's kBrepWeldTolerance. Identical to kDistance by definition
// (welding IS the "same point" test), named separately so a future
// policy can loosen welding without loosening every other coincidence
// test.
constexpr double kWeld = kDistance;

// Planarity, relative: the out-of-plane deviation a set of points may
// show, as a fraction of their own extent, and still be called planar
// (mesh.cpp's IsRingPlanar for LoftClosedRings()'s end caps).
constexpr double kPlanarityRelative = kRelative;

// TessellateGridClippedExact()'s concave-trim grid-line handling
// (surface.cpp): a trim vertex within kOnGridLineFraction of a grid
// line (as a fraction of one grid cell) is nudged kGridNudgeFraction of
// a cell off it, so the clipper never has to decide which side of a
// line a vertex exactly on it belongs to. Both are fractions of a cell,
// i.e. relative tolerances.
constexpr double kOnGridLineFraction = kRelative;
constexpr double kGridNudgeFraction = kRelative;

// Edge join distance: how far apart two naked edges' endpoints/midpoints
// may be and still be joined into one shared edge (Brep::JoinNakedEdges,
// Brep::RemoveNakedMicroEdge's and ReplaceEdgeCurve's default
// `tolerance`, and the floor MergeCoplanarFaces re-welds exposed naked
// edges with). Deliberately 100x looser than kDistance: a join closes
// gaps that construction left open, so it must accept what the weld
// rejected. A joined edge whose two sides were further apart than
// kDistance is a TOLERANT edge (its ON_BrepEdge::m_tolerance records the
// gap), exactly Parasolid's tolerant-edge notion.
constexpr double kEdgeJoin = 1e-4;

// A distance tolerance scaled to a local size, floored at kDistance:
// `max(kDistance, size * kRelative)` - the "at least the absolute
// tolerance, but relatively larger for a large feature" pattern brep.cpp
// already used in several places by hand.
inline double DistanceForSize(double size) {
  return std::max(kDistance, size * kRelative);
}

// The floor a PURELY relative distance tolerance is clamped to, so a
// zero-size feature (a zero-length edge, a zero radius) does not get a
// tolerance of exactly zero and fail every comparison on rounding alone.
// Deliberately far below kDistance: these sites are relative by design
// (brep.cpp's radius/edge-length fits), and flooring them at kDistance
// would silently loosen every small-feature fit to the absolute
// tolerance.
constexpr double kTinyDistance = 1e-9;

// `max(kTinyDistance, size * kRelative)` - the purely relative sibling of
// DistanceForSize(), for the brep.cpp fit tolerances that used to spell
// `std::max(1e-9, x * 1e-6)` by hand.
inline double RelativeDistance(double size) {
  return std::max(kTinyDistance, size * kRelative);
}

}  // namespace tolerance
}  // namespace dino8::kernel
