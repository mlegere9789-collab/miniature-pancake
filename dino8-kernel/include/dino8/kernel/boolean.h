#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

enum class BooleanOp {
  Union,
  Intersection,
  Difference,
  // The region in exactly one of `a`/`b`, not both - "everything except
  // where they overlap." Manifold itself has no direct XOR primitive
  // (only Add/Subtract/Intersect), so this is computed as
  // Union(a, b) - Intersection(a, b) (three underlying Manifold calls
  // instead of one), not a special case Manifold accepts.
  SymmetricDifference,
};

// Real mesh-boolean engine, backed by the Manifold library
// (https://github.com/elalish/manifold) rather than OpenNURBS, which has
// none (see brep.h's comment). Both inputs must be closed/watertight
// meshes - Manifold rejects non-manifold input rather than silently
// producing garbage, and this wrapper does the same: on invalid input it
// throws std::runtime_error rather than returning a corrupt Mesh.
Mesh BooleanCombine(const Mesh& a, const Mesh& b, BooleanOp op);

// Splits `mesh` into two closed, watertight halves along the plane
// `{p : dot(p, plane_normal) == plane_offset}`, backed by Manifold's own
// `Manifold::SplitByPlane` - the real half-space-intersection primitive
// this kernel's own from-scratch clipping (`TessellateGridClippedExact`,
// the Greiner-Hormann polygon clipper) has no 3D-solid equivalent of.
// Returns `{side_along_normal, opposite_side}` - each independently a
// valid closed solid, auto-capped with a flat face at the cut plane, not
// two open shells needing a separate capping step. `plane_normal` need
// not be unit length (Manifold normalizes it internally), but
// `plane_offset` is measured in the same units as `plane_normal`'s own
// magnitude, so passing a non-unit normal changes what offset means -
// pass a unit vector unless that's been accounted for. Throws
// std::runtime_error if `mesh` isn't a valid closed manifold, same
// requirement/failure mode as BooleanCombine().
std::pair<Mesh, Mesh> SplitByPlane(const Mesh& mesh, Vector3d plane_normal, double plane_offset);

// The convex hull of `points`, as a closed watertight solid - backed by
// Manifold's own `Manifold::Hull(const std::vector<vec3>&)`, a genuine
// computational-geometry algorithm (quickhull-family), not something
// this kernel derives itself. A point strictly inside the hull of the
// others contributes nothing to the result (only points that are
// themselves hull vertices survive), so callers don't need to filter
// interior points out first. Throws std::invalid_argument if `points`
// has fewer than 4 entries (fewer can't bound a nonzero 3D volume) or
// std::runtime_error if Manifold's own call fails (e.g. every point
// coplanar, so no 3D hull exists).
Mesh ConvexHull(const std::vector<Point3d>& points);

// Reduces the number of triangles in `mesh` while keeping every point of
// the result within `tolerance` of the original surface - backed by
// Manifold's own `Manifold::Simplify`, a real quadric-error-style
// decimation algorithm, not a naive "merge nearby vertices" pass. Most
// useful for an over-tessellated mesh with many redundant near-coplanar
// triangles (e.g. a flat Brep face tessellated at a much finer resolution
// than its actual geometry needs); a mesh that's already minimally
// tessellated for its own shape (a plain box's 12 triangles) may not
// shrink further at all. `mesh` must be a valid closed manifold, same
// requirement as BooleanCombine(); throws std::runtime_error if
// Manifold's own call fails.
Mesh Simplify(const Mesh& mesh, double tolerance);

// The Minkowski sum of `a` and `b` - `{p + q : p in a, q in b}` - backed
// by Manifold's own `Manifold::MinkowskiSum`. The standard use is
// "growing" or "rounding" a solid by another (e.g. summing with a small
// sphere rounds every edge/corner by that sphere's radius; summing with
// a small box gives a uniform margin, useful for a collision/clearance
// envelope), not something this kernel would derive from more basic
// operations. Both inputs must be valid closed manifolds, same
// requirement as BooleanCombine(); throws std::runtime_error if
// Manifold's own call fails.
Mesh MinkowskiSum(const Mesh& a, const Mesh& b);

// The Minkowski difference (erosion) of `a` and `b` - the complement
// operation to MinkowskiSum() (shrinking `a` by `b` rather than growing
// it), backed by Manifold's own `Manifold::MinkowskiDifference`.
// `MinkowskiDifference(MinkowskiSum(a, b), b)` recovers a shape congruent
// to `a` (same dimensions and volume, confirmed by testing) but not
// necessarily at `a`'s own original position - erosion for a `b` that
// isn't itself centered on the origin translates the result by `b`'s own
// extent, a real (if non-obvious) property of the operation itself, not
// a limitation of this wrapper. Same requirements and failure mode as
// MinkowskiSum().
Mesh MinkowskiDifference(const Mesh& a, const Mesh& b);

// Splits `mesh` into its disconnected pieces - one Mesh per connected
// component - backed by Manifold's own `Manifold::Decompose`. The
// counterpart to Mesh::MergeAndWeld() concatenating several meshes into
// one: that operation has no way to tell the pieces apart again
// afterward, which this closes. `mesh` must be a valid closed manifold
// (each individual piece, not just the whole - Manifold requires every
// component to itself be watertight), same requirement as
// BooleanCombine(); throws std::runtime_error if Manifold's own call
// fails. Order of the returned pieces isn't specified.
std::vector<Mesh> Decompose(const Mesh& mesh);

// The minimum distance between `a` and `b`'s surfaces - 0 if they
// overlap or touch at all (checked directly via a real intersection
// test, not just "assume nonzero"), otherwise the true minimum gap,
// searched up to `search_length` away. Backed by Manifold's own
// `Manifold::MinGap`. The two-solid counterpart to
// `Mesh::SignedDistance()` (one mesh, one point) - useful for a
// clearance/collision check between two whole solids rather than a
// solid and a single point. Both inputs must be valid closed manifolds,
// same requirement as BooleanCombine().
double MinGap(const Mesh& a, const Mesh& b, double search_length);

// Subdivides `mesh`'s triangles so that no resulting edge is longer than
// `length` - the opposite direction from Simplify() (adding detail
// rather than removing it), backed by Manifold's own
// `Manifold::RefineToLength`. Doesn't change the underlying shape at all
// (a flat face stays exactly flat, just with more/smaller triangles
// covering it) - useful as a uniform-resolution pass before an operation
// that wants a denser mesh to work with (e.g. `Mesh::ComputeVertexNormals()`
// on a coarse mesh where per-face flat shading would otherwise be too
// visible). `mesh` must be a valid closed manifold, same requirement as
// BooleanCombine(); throws std::runtime_error if Manifold's own call
// fails.
Mesh RefineToLength(const Mesh& mesh, double length);

// Turns a faceted polyhedron (e.g. `ConvexHull()`'s flat-faced output)
// into an approximation of a smoothly curved surface, without knowing
// what that surface "should" be in closed form - backed by Manifold's
// own `Manifold::SmoothOut` followed immediately by
// `Manifold::RefineToLength`, both performed on the same live Manifold
// object before converting back to a Mesh. That "before converting
// back" matters and is why this is one combined function rather than
// two separate `SmoothOut()`/`RefineToLength()`-style wrappers: SmoothOut
// only records half-edge tangent vectors on the live Manifold - the
// actual geometry doesn't change until a subsequent Refine call
// interpolates new vertices from them - and those tangents live only in
// Manifold's own internal representation, not in this kernel's Mesh/
// ON_Mesh format, so a separate `SmoothOut()` call that round-tripped
// through Mesh before a later, separate `RefineToLength()` call would
// silently discard the smoothing entirely (confirmed by testing: an
// earlier version of this API split the two calls, and a refined
// "smoothed" octahedron came back with byte-for-byte the same volume as
// the unsmoothed input - the smoothing had no effect at all). Combining
// them here means it doesn't matter whether the caller notices.
// `min_sharp_angle` (degrees) is the face-to-face angle above which an
// edge stays a hard crease rather than being smoothed; `min_smoothness`
// (0-1) softens even those creases into a small fillet;
// `target_length` is the same subdivision target `RefineToLength()`
// takes. `mesh` must be a valid closed manifold, same requirement as
// BooleanCombine(); throws std::runtime_error if either underlying
// Manifold call fails.
Mesh SmoothAndRefine(const Mesh& mesh, double target_length, double min_sharp_angle = 52.5,
                      double min_smoothness = 0.0);

// The number of `mesh`'s triangles that are degenerate (collinear/
// zero-area) to within Manifold's own internal precision, *after*
// Manifold's own mesh construction - backed by
// `Manifold::NumDegenerateTris`, whose own doc comment says the library
// "attempts to remove all of these" as part of building the Manifold in
// the first place. Confirmed by testing, not just quoting the doc:
// deliberately collapsing one triangle to a straight line before calling
// this still reports 0, because that degeneracy gets cleaned up before
// NumDegenerateTris() is ever asked about it - so a nonzero result means
// a degeneracy the library specifically *couldn't* clean up, not "any
// degeneracy that was ever present in the input." `mesh` must be a valid
// closed manifold, same requirement as BooleanCombine().
size_t CountDegenerateTriangles(const Mesh& mesh);

}  // namespace dino8::kernel
