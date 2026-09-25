#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "dino8/kernel/brep.h"
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

// Offsets a closed solid mesh by `distance` - Parasolid `PK_BODY_offset`'s
// uniform-distance body-offset case, at the mesh level (see
// NurbsSurface::OffsetAnalytic() for the exact-surface counterpart on a
// single analytic face). Backed directly by MinkowskiSum()/
// MinkowskiDifference() above with a sphere of radius `|distance|`
// centered at the origin - the standard morphological dilation/erosion
// definition of a uniform body offset, not something this kernel derives
// independently:
//  - `distance > 0` GROWS the solid (`MinkowskiSum(solid, sphere)`).
//    Every CONVEX edge/corner is rounded to radius `distance` - a real
//    property of the ball-offset operation itself (dilating a cube by a
//    small ball rounds its 12 edges and 8 corners into fillets/spherical
//    corners), not a limitation of this wrapper.
//  - `distance < 0` SHRINKS it (`MinkowskiDifference(solid, sphere)`) -
//    the dual case: every CONCAVE (reflex) edge/corner is rounded
//    instead, while convex ones stay sharp (shrinking a cube by a small
//    enough ball keeps its edges sharp, just moved inward - exactly
//    ShellConvexPlanar()'s/OffsetAnalytic()'s own exact-offset behavior
//    for a convex shape, recovered here as a special case of the general
//    mesh-level operation). This asymmetry between growing and shrinking
//    is the genuine, well-known behavior of a uniform ball offset, not
//    approximated or hidden here.
//  - `distance == 0` returns `solid` unchanged (no Minkowski call at
//    all - a zero-radius sphere is degenerate, not a meaningful no-op
//    through Manifold itself).
//
// `sphere_divisions` (both u and v) controls the rounding sphere's own
// tessellation density - a rounded region in the result is only as
// smooth as this sphere is, exactly as coarsely/finely tessellating the
// sphere passed directly to MinkowskiSum()/MinkowskiDifference() would
// be. Throws std::invalid_argument if `sphere_divisions < 3` (fewer
// cannot tessellate a genuine 3D sphere at all), and whatever
// MinkowskiSum()/MinkowskiDifference() themselves throw for other
// failures (e.g. `solid` not a valid closed manifold, same requirement
// as BooleanCombine()).
//
// A real, deliberately enforced correctness guard, not an omission: a
// shrink (`distance < 0`) whose magnitude exceeds `solid`'s own smallest
// feature size (e.g. shrinking a thin plate by more than half its
// thickness) mathematically erodes it away to NOTHING - unlike a naive
// per-vertex offset, morphological erosion by a ball can never produce
// an invalid or self-intersecting mesh, but it CAN legitimately produce
// an EMPTY one, and `MinkowskiDifference()` itself returns that empty
// mesh without complaint (confirmed directly, not assumed: a 10x10x1
// plate shrunk by 0.6, exceeding its own 0.5 half-thickness, silently
// comes back with `VertexCount() == 0`). A caller expecting a genuine
// solid result would otherwise get an empty mesh with no signal
// distinguishing "this shrink was infeasible" from any other empty-mesh
// case, so this throws `std::runtime_error` instead when a `distance <
// 0` call's own result comes back with `VertexCount() == 0` - the direct
// mesh-level analogue of `OffsetAnalytic()`'s `new_radius <= 0` guard and
// `OffsetFace()`'s degenerate-clipped-face guard, generalized here to an
// arbitrary (possibly non-convex, possibly disconnected) solid where no
// single closed-form "local radius of curvature" exists to check against
// in advance - the erosion is actually performed and its result is
// checked, not predicted. Growing (`distance > 0`) is never checked this
// way: dilation by a ball only ever adds volume, so it cannot collapse a
// solid to nothing.
Mesh OffsetSolid(const Mesh& solid, double distance, int sphere_divisions = 24);

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

// Exact B-rep boolean intersection of two CONVEX planar-faced solids -
// e.g. two Brep::Box()es, or two Brep::FromPlanarFaces() results at
// arbitrary transforms. Unlike BooleanCombine() (which tessellates both
// operands to meshes and hands them to the external Manifold library),
// this works directly on each solid's own exact planes: intersection of
// two convex polyhedra is exactly the set of points satisfying every
// half-space of both solids, computed by clipping (Sutherland-Hodgman)
// every face polygon of `a` against every plane of `b`, and every face
// polygon of `b` against every plane of `a` - each surviving fragment
// lies exactly on one of the original planes, so the result is exact to
// floating-point precision, not a tessellation approximation. This is
// classical, unpatented computational geometry (convex polytope
// intersection via half-space clipping - see e.g. Preparata & Shamos,
// "Computational Geometry"), implemented here from that description, not
// from or against any proprietary kernel's source.
//
// Deliberately narrow, and says so rather than silently producing a
// wrong answer outside its scope: throws std::invalid_argument if either
// input has a non-planar face (PlanarFaces()'s own check) or is
// non-convex (checked directly: every vertex of every face must satisfy
// every one of that solid's own half-spaces, within tolerance - a solid
// that fails this would silently clip pieces of itself away against its
// own planes if this function proceeded). When `a` and `b` share an
// exact coincident boundary plane (e.g. two prisms of the same height,
// both with a top face at the same z), that plane's clip result is
// identical from either side and is kept only once - a real closed
// B-rep has exactly one face there, not two stacked copies (verified by
// the octagon-prism test case below, which fails closed/watertight
// without this). Union and difference of convex solids are NOT generally
// convex, and curved-face intersection needs a genuine NURBS-NURBS
// surface-intersection-and-retrim step this doesn't attempt (see
// dino8-app's IntersectSurfaces for that half, not yet wired to a Brep
// boolean) - both are real, out-of-scope-here future work, not silently
// approximated.
Brep BooleanIntersectConvexPlanar(const Brep& a, const Brep& b);

// Exact B-rep boolean (Union, Intersection, or Difference; SymmetricDifference
// is the Brep::Compound of the two lumps Difference(a, b) and
// Difference(b, a) - see BooleanCombineMixed's own SYMMETRIC DIFFERENCE
// paragraph below for why one shell cannot hold an XOR, and Brep::Compound
// in brep.h for the representation) between two planar-faced solids of
// ARBITRARY shape - the
// general non-convex case BooleanIntersectConvexPlanar's own doc comment
// flags as future work. Classical Requicha & Voelcker boundary
// evaluation (see "Boolean operations in solid modeling: Boundary
// evaluation and merging algorithms," Proc. IEEE 73(1), 1985 - the same
// public-domain, unpatented technique BooleanIntersectConvexPlanar's own
// doc comment cites Preparata & Shamos for the convex-only special case
// of): every face of A is SPLIT (not merely clipped) against every plane
// of B, and vice versa, via Sutherland-Hodgman run once per plane but
// keeping both children instead of only the inside one; every surviving
// fragment is then classified IN/OUT/ON the other solid - directly, for
// a fragment coincident with one of the other solid's own faces, or by
// ray-casting along a fixed list of non-axis-aligned directions
// otherwise - and the op-specific combination of classified fragments
// (e.g. Difference keeps A's outside plus B's inside flipped to bound the
// new cavity) is reassembled into the result Brep via Brep::
// FromPlanarFaces, exactly as BooleanIntersectConvexPlanar's own return
// does. Because every face is split against the OTHER solid's full plane
// arrangement (not just clipped to its own convex extent), this is
// correct for a non-convex `a` and/or `b` too - unlike
// BooleanIntersectConvexPlanar, this function has no convexity
// precondition to check or reject.
//
// A degenerate corner this doesn't specially handle: if a single cutting
// plane crosses a sufficiently complex concave face's boundary more than
// twice, the resulting fragment's own loop can come back as a
// "keyhole"-bridged polygon (two or more regions joined by zero-net-area
// edges lying exactly on the cut - see SplitByHalfspace's own comment in
// boolean.cpp) rather than as several separate loops. That fragment's
// signed area, and the interior sample point ClassifyPointVsSolid uses
// (found by ear-clip triangulation, which handles a bridged polygon
// correctly - the same technique used to triangulate a polygon with a
// hole), both still come out exactly right; whether Brep::FromPlanarFaces'
// own exact-clip tessellation renders such a bridged loop as a visually
// clean multi-lobed face is not separately verified here - the geometry
// this function's own tests exercise (an L-shaped non-convex prism
// against an overlapping box) never produces one, since every individual
// cutting plane involved only ever crosses that shape's boundary twice.
Brep BooleanCombinePlanar(const Brep& a, const Brep& b, BooleanOp op);

// The Sutherland-Hodgman half-space clipper shared by
// BooleanIntersectConvexPlanar (above) and ShellConvexPlanar (below) -
// extracted here, not rewritten, so both operations run the same verified
// clipping loop instead of two independent copies of it. `poly` is a
// convex polygon already known to lie in `poly_plane`; the result is
// `poly` clipped against every plane in `halfspaces` in turn, keeping on
// each step the side each plane's own outward normal points away from
// (`dot(p - plane.origin, plane.zaxis) <= tol`, i.e. `plane.DistanceTo(p)
// <= tol` - exactly BooleanIntersectConvexPlanar's own inside test).
// Returns an empty vector if any step leaves fewer than 3 vertices; never
// throws (callers decide what "clipped away to nothing" means for them -
// see BooleanIntersectConvexPlanar's and ShellConvexPlanar's own
// handling of that).
//
// `tol`, if non-negative, is used as-is - this is how
// BooleanIntersectConvexPlanar keeps its own existing, already-verified
// tolerance behavior completely unchanged after this extraction (it
// still computes and passes its own relative tolerance, exactly as
// before). If negative (the default), a tolerance is derived from `poly`
// and `poly_plane`'s own coordinate magnitudes the same way - a
// self-contained default for callers, like ShellConvexPlanar, that don't
// already have an externally-computed one on hand.
std::vector<Point3d> ClipConvexPolygon(const std::vector<Point3d>& poly, const ON_Plane& poly_plane,
                                        const std::vector<ON_Plane>& halfspaces, double tol = -1.0);

// Hollows out a CONVEX planar-faced solid to wall thickness `t`, opening
// it at `removed_faces` (indices into `solid.PlanarFaces()`) - e.g. an
// open-top box from Brep::Box() with removed_faces={1} (see PlanarFaces()/
// Box()'s own face-order comment for which index that is).
//
// The construction, face by face:
//  - Every KEPT face i contributes two faces to the result: its own
//    original outer loop, unchanged (the exterior wall doesn't move), and
//    an inner loop lying in that face's plane offset inward by `t`
//    (translated by -t*n_i, n_i = faces[i].plane.zaxis), clipped
//    (ClipConvexPolygon, above) against every OTHER face's own
//    "constraint plane" - that other face's own inward offset if it's
//    also kept, or its original unmoved plane if it's in
//    `removed_faces`. The inner loop's outward normal (from the shell's
//    own material) is -n_i, the opposite of the exterior copy's own - a
//    direct consequence of the material being sandwiched between the two
//    (see this function's own .cpp comment for the worked-through sign
//    argument).
//  - Every REMOVED face contributes neither an outer nor an inner copy
//    (dropped entirely - that absence is the opening); its own plane is
//    also never offset, so it never bounds any other face's clip either.
//  - The opening's boundary is closed by a flat "rim" (washer) lying
//    entirely in that removed face's own original, unmoved plane: outer
//    edge = the removed face's own original loop, inner edge = that same
//    loop clipped against every OTHER face's constraint plane (both the
//    outer and inner rim edges lie in the removed face's plane, since
//    that's the one plane left unmoved on both sides of the cut - see
//    the .cpp comment for why that's forced, not assumed). Represented
//    as one flat quad per edge of the removed face's own loop, matching
//    outer edge k to inner edge k - see the .cpp comment for why that
//    trapezoid strip comes out correctly wound without extra
//    bookkeeping.
//
// Convex-solid precondition: same check and failure mode as
// BooleanIntersectConvexPlanar (a non-convex solid would clip pieces of
// itself away against its own offset planes). Every inner loop is
// computed - and checked for degeneracy - before any output face is
// built: if offsetting any kept face by `t` collapses its inner loop to
// fewer than 3 vertices or ~0 area (t at or beyond that face's own local
// offset feasibility, up to the solid's inradius - t = s/2 for a cube of
// side s, say), this throws std::invalid_argument rather than emitting a
// partially-correct shell. Likewise throws if a removed face's rim edge
// count doesn't match its own outer loop's (the same t-too-large
// degeneracy, on the rim instead of an inner face) or if two entries of
// `removed_faces` are mutually adjacent (share an edge) - an opening
// spanning more than one original face needs a non-planar, multi-facet
// rim, genuinely out of scope here, not silently approximated.
Brep ShellConvexPlanar(const Brep& solid, const std::vector<int>& removed_faces, double t);

// Per-face wall-thickness override of ShellConvexPlanar() above (Parasolid
// PK_BODY_shell's own per-face `thickness` array, as distinct from its
// single-scalar form): identical construction, except every place the
// single `t` above offsets a KEPT face's own plane/loop inward, this uses
// THAT FACE's own `wall_thickness[i]` instead - so two adjacent kept faces
// may end up with genuinely different wall thickness, each face's inner
// offset still independently clipped against every OTHER (possibly
// differently-offset) face's own constraint plane exactly as before.
// `wall_thickness.size()` must equal `solid.PlanarFaces().size()` (one
// entry per face, by the same index PlanarFaces()/`removed_faces` already
// use) - throws std::invalid_argument otherwise, rather than silently
// zip-truncating or index-wrapping a mismatched-length array. Every entry
// for a KEPT face (an index not in `removed_faces`) must be positive (the
// same check the scalar overload makes on its own single `t`); an entry
// for a REMOVED face is never read (that face has no wall of its own to
// thicken) and may be anything, including left at 0.
//
// The scalar `ShellConvexPlanar(solid, removed_faces, t)` above is
// exactly `ShellConvexPlanar(solid, removed_faces, std::vector<double>(
// solid.PlanarFaces().size(), t))` - a thin delegation, not a second
// implementation, so its own already-verified behavior (including every
// one of its own degeneracy/adjacency checks) is provably unchanged by
// this overload's existence.
Brep ShellConvexPlanar(const Brep& solid, const std::vector<int>& removed_faces,
                        const std::vector<double>& wall_thickness);

// Hollows a FULL sphere into a spherical SHELL solid, wall thickness
// `thickness`: a concentric inner sphere at radius `outer_radius -
// thickness`, its single face reversed (`ON_Brep::FlipFace`) so its own
// outward-from-material direction points INWARD, combined with the outer
// sphere via `Brep::Compound()` - the exact closed-surface counterpart of
// ShellConvexPlanar() above, for the one case ShellConvexPlanar() itself
// cannot reach at all (a sphere has no planar faces for `PlanarFaces()`
// to see, so ShellConvexPlanar() cannot even be called on one). No rim/
// wall construction is needed here, unlike ShellConvexPlanar()'s own
// planar rim washers, because a full sphere has no boundary curve to
// begin with - it is already a closed 2-manifold on its own, and so is
// its concentric inner copy; `Brep::Compound()` is exactly the
// "two disjoint closed shells, one solid" combinator this needs (see its
// own doc comment: "IsValid()/IsSolid() hold for a compound of valid
// solid lumps ... Tessellate*() volumes add up per face").
//
// Throws std::invalid_argument if `outer_radius` is not positive, or if
// `thickness` is not strictly between 0 and `outer_radius` - the exact
// self-intersection guard NurbsSurface::OffsetAnalytic()'s own sphere
// case already enforces (a thickness at or beyond the radius collapses
// or inverts the inner sphere through the center).
Brep ShellClosedSphere(Point3d center, double outer_radius, double thickness);

// The torus sibling of ShellClosedSphere() above: hollows a FULL torus
// (major radius `major_radius`, tube/minor radius `outer_minor_radius`,
// lying in `plane`) into a shell of wall thickness `thickness` - a
// concentric inner torus with the SAME major radius and plane, minor
// radius `outer_minor_radius - thickness`, its face reversed and combined
// via `Brep::Compound()`, exactly as ShellClosedSphere() does. Throws
// std::invalid_argument if `major_radius`/`outer_minor_radius` are not
// positive, if `outer_minor_radius >= major_radius` (the OUTER torus
// itself would already be a self-intersecting spindle torus - checked
// here rather than left to a downstream, harder-to-diagnose failure), or
// if `thickness` is not strictly between 0 and `outer_minor_radius` (the
// same collapse-through-center hazard ShellClosedSphere() and
// OffsetAnalytic()'s own torus case both guard against).
Brep ShellClosedTorus(const ON_Plane& plane, double major_radius, double outer_minor_radius, double thickness);

// Moves ONE face of a convex planar-faced solid along its own outward
// normal by `distance` (positive grows the solid at that face, negative
// shrinks it), re-extending or re-trimming every OTHER face so the
// result is still a valid closed solid - the direct-editing "push/pull"
// or "move face" operation (Rhino/SolidWorks' own such tool), distinct
// from ShellConvexPlanar() (which offsets every KEPT face at once to
// build a hollow shell) and from NurbsSurface::OffsetAnalytic() (which
// offsets a single bare surface with no neighbours to reconcile at all).
//
// The construction: `distance` moves ONLY `face_index`'s own plane
// (translated by `distance * plane.zaxis`); every other face's plane is
// UNCHANGED. Every face's own new boundary is then computed the same
// way, uniformly, whether or not it moved: start from a polygon in that
// face's own (possibly-moved) plane, generous enough to be guaranteed to
// contain the true final polytope's face there (a square of side
// `100 * this solid's own bounding-box diagonal`, centered at that
// plane's own origin) and clip it (ClipConvexPolygon, above) against
// every OTHER face's own plane - the standard technique for
// reconstructing a convex polytope's boundary directly from a set of
// half-spaces (start from a superset, intersect down to the true
// bounded result), which is exactly what "one plane moved, the rest
// fixed" is. No special case is needed for `face_index` itself: it goes
// through the identical oversized-polygon-clipped-against-every-OTHER-
// plane construction as any other face.
//
// Convex-solid precondition, same check and failure mode as
// ShellConvexPlanar()/BooleanIntersectConvexPlanar() (a non-convex solid
// would clip pieces of itself away against its own planes). Throws
// std::invalid_argument if `face_index` is out of range for
// `solid.PlanarFaces()`, or if any face's own new boundary collapses to
// fewer than 3 vertices or ~0 area - `distance` large enough that a face
// vanishes entirely (the topology itself would need to change - a
// different face count or adjacency, not just moved boundaries) is
// genuinely out of scope here, not silently approximated by dropping
// that face or guessing a replacement.
Brep OffsetFace(const Brep& solid, int face_index, double distance);

// Exact B-rep boolean between two solids where either (or both) may have
// a CYLINDRICAL face, not just planar ones - what closes the gap
// BooleanCombinePlanar's own PlanarFaces()-only precondition leaves open:
// a box with a drilled hole or a boss (bolt holes, counterbores, pipe
// stubs) is flatly impossible to build via BooleanCombinePlanar today, and
// this is the narrowest, most valuable slice of the general non-planar
// case - see the audit doc this increment's own spec cites for why. A NEW
// entry point, not a modification of BooleanCombinePlanar: that function's
// own behavior (verified to ~1e-9 by hundreds of already-passing checks)
// is completely unaffected - a planar-only pair of faces always goes
// through the exact same Sutherland-Hodgman half-space split
// (SplitByHalfspace3d) BooleanCombinePlanar itself uses, just reached via
// this function's own, separate pipeline.
//
// SCOPE, stated as plainly as BooleanIntersectConvexPlanar/
// BooleanCombinePlanar's own doc comments state theirs: this handles TWO
// kinds of face interaction, both closed-form and exact except for one
// disclosed polygonal approximation each -
//   - a planar face crossed by a cylindrical face whose axis is
//     PERPENDICULAR to that plane (the infinite cylinder's silhouette in
//     the plane is exactly a circle - the same closed-form fact dino8-
//     app's own BuildPlaneCylinderVariableFillet, cmd_fillet.cpp, already
//     exploits for a plane+cylinder fillet): splits the planar face by
//     punching that circle out of it (detail/circle_clip3d.h's
//     ClipPolygonByCircle3d - exact except for a fine polygonal sampling
//     of the circle boundary, the same kind of disclosed, bounded
//     approximation FilletConvexEdge's own end-cap notch already makes,
//     not a new kind of inexactness).
//   - the same relationship the other way around - a cylindrical face
//     crossed by a planar face perpendicular to ITS axis - splits the
//     cylindrical face by height into two CylindricalFace children at the
//     exact axial cut point (one dot product, zero approximation - MORE
//     exact than the planar side).
//   - the OBLIQUE case (neither perpendicular nor clearly non-
//     interacting): the infinite cylinder's own silhouette in a
//     non-perpendicular cutting plane is a genuine ELLIPSE, not a circle -
//     a classical closed-form fact (P(phi) = center + radius*cos(phi)*e0 +
//     radius*sin(phi)*e1, derived and verified directly in detail/
//     ellipse_clip3d.h's own top comment, not merely asserted). The planar
//     side is split by detail::ClipPolygonByEllipse3d (the direct
//     generalization of ClipPolygonByCircle3d, same "4 simple wedges, one
//     disclosed polygonal-sampling approximation of the boundary, throws
//     on a genuine boundary crossing" contract); the cylindrical side is
//     split by SplitCylindricalByObliquePlane (boolean.cpp) into a "below
//     the cut" and "above the cut" CylindricalFace, each carrying the true
//     wavy ellipse boundary in its own new cap0_notch_points/
//     cap1_notch_points field (Brep::CylindricalFace's own doc comment) -
//     mirroring the closed-form ellipse notch ConicalFace/
//     FilletConvexEdgeTapered already use for a TAPERED fillet's own cap,
//     applied here to a genuinely different curve (a plane-cylinder
//     ellipse, not a plane-cone one) via a wholly separate function, not a
//     generalization-in-place. Restricted to a FULL-SWEEP (angle == 2*pi)
//     cylindrical operand and to an interaction that crosses the whole
//     swept angle monotonically (the closed-form "cylindrical wedge"
//     case, whose exact volume equals a PERPENDICULAR cut at the ellipse's
//     own center height - see this increment's own volume-formula test) -
//     see SplitCylindricalByObliquePlane's own doc comment (boolean.cpp)
//     for the two real, checked-directly limitations this implies
//     (a partial-sweep operand, and a non-monotonic/re-entrant crossing)
//     and why neither is silently mishandled. Under Brep::
//     TessellateConforming() the ellipse seam is watertight: each planar
//     piece records its ellipse stretch as a LITERAL ArcRun
//     (PlanarFace::ArcRun::literal_points - the very EllipsePointAt
//     samples the fragment's cap0/cap1_notch_points hold, reported by
//     ClipPolygonByEllipse3d's own `ellipse_runs`), which that method
//     ear-clips around verbatim while the fragment meshes the same list
//     as its notch row (see TessellateConforming's own doc comment, the
//     SEVENTH and EIGHTH entries). An oblique-cut result's own ordinary
//     Tessellate() output still carries the SAME known, already-disclosed
//     non-watertight-at-the-wedge-seam limitation the PERPENDICULAR case
//     has without TessellateConforming() - not a new gap, just the
//     existing one.
//   - a face pair with NO possible interaction at all (checked via a
//     closed-form conservative bound on the cylinder's own signed
//     distance to the other face's plane) is left completely unmodified -
//     the same behavior BooleanCombinePlanar's own planar-only pipeline
//     already has for two faces that don't intersect, so a face with no
//     cylindrical interaction (e.g. a drilled box's own four side walls,
//     when the hole's footprint stays strictly inside the box's own
//     cross-section) reduces EXACTLY to what BooleanCombinePlanar would
//     already do with it. A real, checked-directly correction versus this
//     branch's own earlier form (found and fixed by the same increment
//     that added end-cap synthesis below, not merely theorized): "the
//     cylinder's axis is perpendicular to this plane" alone does NOT mean
//     the FINITE cylinder actually reaches this plane at all - only that
//     its infinite extension would - so this closed-form no-interaction
//     bound is now checked FIRST, even in the axis-perpendicular case,
//     before ever punching a circular hole; every existing Difference/hole
//     test is unaffected (its own drilled cylinder always genuinely
//     reaches both of the box's own caps, so this bound was already false
//     there), but a Union/boss cylinder that only touches ONE of a box's
//     two z-perpendicular caps needs this correction to avoid a spurious,
//     material-losing hole punched in the FAR, untouched cap too (see
//     SplitMixedAgainstAllFaces' own doc comment at that branch,
//     boolean.cpp, for the full citation).
//
// UNION/BOSS END CAPS: a bare CylindricalFace operand (built the same way
// every existing hole/drill operand is, Brep::FromMixedFaces({}, {cf}))
// combined via BooleanOp::Union - a boss, pipe stub, or standoff sitting
// on or embedded in the other operand, rather than a hole drilled INTO
// it - has a genuine, exact closed-form gap the Difference/hole path
// never hits: a Difference result never collects the "outside" bucket of
// the cylindrical operand at all (only its "inside" fragment, flipped, is
// kept - see this function's own .cpp switch statement), so a drilled
// bit's own free-hanging stub ends are simply dropped, never needing a
// cap. A Union result DOES collect that "outside" bucket directly, so a
// surviving cylindrical fragment's own end can be a genuine, unmet
// terminus of the input solid with NOTHING in either operand to close it
// - RayVsMixedFace's own two IMPLICIT end disks (this function's own .cpp
// comment) make point-CLASSIFICATION correct regardless, but were never
// real output faces, so the RESULT B-rep itself was left with an open
// boundary there.
//
// The fix, closed-form and additive (touches nothing any existing test
// exercises outside the two corrections named above): two new bookkeeping
// fields on CylindricalFace, `end0_is_original`/`end1_is_original` (see
// that struct's own doc comment in brep.h), record whether a fragment's
// v=0/v=length end is STILL the original, never-split terminus of the
// input cylinder (true, the default - every existing producer is
// unaffected) or a boundary this same split pipeline already manufactured
// (case (iii)'s own axis-aligned and oblique branches, boolean.cpp, the
// only two places that ever set either to false). For every surviving
// Union-result cylindrical fragment, a new step (SynthesizeEndCaps,
// boolean.cpp) probes a point just past each ORIGINAL end along the axis
// against the OTHER operand's own faces: PointClass::kOut there means the
// end is genuinely exposed (needs a cap - BuildEndCap synthesizes a real,
// full-circle Brep::PlanarFace disc there, split into 4 quadrant "pie
// slice" pieces mirroring detail::ClipPolygonByCircle3d's own established
// pattern, each carrying a genuine PlanarFace::ArcRun so
// Brep::TessellateConforming() reconciles the new cap's own boundary with
// the adjacent cylindrical wall's row bit-identically, the same already-
// proven mechanism that closes the wedge-cap/cylinder-wall seam
// elsewhere); kIn or kOn means the end is already sealed by the other
// operand's own material (either fully embedded, or exactly coincident
// with a split boundary) and no cap is added. Restricted, like every
// other cap-notch mechanism in this codebase, to a FULL-SWEEP
// (angle == 2*pi) cylindrical fragment - the only kind any producer here
// ever builds - BuildEndCap throws for a genuinely partial-angle case
// rather than guessing at an unverified "pie slice with a real sector cut
// out" shape.
//
// SCOPE, stated plainly: verified by this increment's own closed-form
// volume and IsClosedManifold() tests for a boss whose base sits flush
// with the other operand's own cap, overlaps it partway, or never touches
// it at all (both ends exposed), and for a negative control (a fully
// embedded boss, correctly adding no cap at all).
//
// INTERSECTION END CAPS (a later increment): BooleanOp::Intersection has an
// analogous gap - an Intersection-collected cylindrical fragment
// (from_a.in/from_b.in) can also have an end that's exposed because the
// CYLINDER itself terminates there, not because the other solid's own
// boundary does - and the correct polarity of "does an exposed end there
// need a cap" is the literal OPPOSITE of the Union rule above, not its
// mirror: a fully-embedded-in-the-other-solid cylinder used for
// Intersection needs caps at BOTH its own ends, where a naive
// generalization of the Union probe's own kOut-means-cap rule would add
// NEITHER. The reasoning: a fragment kept in an Intersection result is
// material of A∩B, which can only exist where BOTH solids have material.
// A still-original end means no more of THIS fragment's own solid exists
// past that point, regardless of the other operand - so if the probe just
// past that end is PointClass::kIn (the OTHER operand's material keeps
// going past where this fragment's own material stops), the intersection
// region also has to stop exactly there, and nothing else in the result
// bounds it there (the other operand's own surface is interior, not a
// boundary, at that point) - a cap is needed. If the probe is kOut, the
// other operand doesn't reach past there either, consistent with "already
// sealed by a real split, or never actually reached" (a genuine crossing
// there would already have produced a split, clearing
// end{0,1}_is_original) - no cap is added. `SynthesizeEndCaps` (boolean.cpp)
// takes this polarity as an explicit `needed_class` parameter (defaulting
// to kOut, so the Union call sites above are completely unaffected); the
// Intersection branch calls it with PointClass::kIn on both from_a.in/fb
// and from_b.in/fa, mirroring the Union branch's own symmetric pairing.
// Verified the same way as the Union fix: closed-form volume and
// IsClosedManifold() checks for two differently-proportioned fully-embedded
// boss/box pairs (one centered, one off-center with different box/cylinder
// proportions), a disjoint negative control (empty result, no cap
// reachable), and a falsifiability check confirming the underlying
// unfixed fragment is provably open on its own.
//
// MID-LENGTH CROSSING INTERSECTION SEAM (closed, a later increment): the
// paragraph above's own gap - wherever the cylinder genuinely CROSSES a
// planar face of the other operand mid-length (a real split,
// end{0,1}_is_original cleared at that end, so no cap synthesized on the
// cylindrical fragment's own end can supply the missing material there
// either) - is now closed. SplitMixedAgainstAllFaces' own case (ii) (the
// planar-face-crosses-the-cylinder split, boolean.cpp) still builds the
// planar operand's own OUTSIDE-the-circle wedge pieces via
// detail::ClipPolygonByCircle3d exactly as before (that function itself is
// completely untouched - see its own doc comment for why: it already
// discloses TWO separate, real, confirmed regressions from prior attempts
// to alter its own behavior in place, a "keyhole"-bridged loop and a NURBS-
// frame-aligned arc sampling, both reverted). The missing disc-shaped
// INSIDE piece is instead produced by a new, ADDITIVE sibling function,
// detail::ClipPolygonByCircleInsideOnly3d (same header), sharing every
// piece of nontrivial math (SegmentCircleCrossings, PointInPolygon2d) with
// the existing function while never modifying it - the same
// "new sibling, not a risky in-place edit of proven-fragile code" pattern
// ellipse_clip3d.h's own ClipPolygonByEllipse3d/ConvexPolygonRayExitDir
// already established for the oblique case.
//
// Case (ii) calls this new function ONLY when `g.cyl`'s own finite axial
// range genuinely reaches the plane MID-LENGTH (a `v_cut` gate exactly
// mirroring case (iii)'s own identical condition on the opposite side of
// this same seam - see that gate's own comment in boolean.cpp) - a flush-
// end touch is left to the existing end-cap synthesis above, unchanged.
// The new disc pieces are split into the SAME 4-quadrant "pie slice"
// pattern BuildEndCap's own synthesized caps already use (never a single
// loop wrapping the whole circle - see ClipPolygonByCircleInsideOnly3d's
// own doc comment for why: a full 0-to-2*pi ArcRun has a confirmed,
// checked-directly duplicate-vertex seam degeneracy FindArcRun/
// detail::ArcSchedule3d were never built to handle), so the SAME existing
// FindArcRun helper recovers PlanarFace::ArcRun bookkeeping for them
// completely unmodified, and Brep::TessellateConforming()'s own circle-
// identity/axial-height matching (brep.cpp) reconciles the new disc's own
// boundary against the adjoining cylindrical fragment's own matching row
// with zero new code in brep.cpp/brep.h/arc_schedule3d.h at all.
//
// Deliberately NOT gated on BooleanOp: SplitMixedAgainstAllFaces stays
// fully op-agnostic, exactly like every other case (i)/(ii)/(iii) branch.
// The new disc's own representative point always classifies kIn against
// the OTHER operand (it sits strictly inside the crossing cylinder's
// occupied volume by construction), so the EXISTING classify-then-bucket
// switch below already discards it for Union (never scans `.in` at all)
// and for Difference-from-this-side (`from_a.in` is never collected
// either) with zero new op-specific logic anywhere in this shared split
// pipeline - verified directly, not merely argued: every existing
// drilled-through-hole Difference/Union test (both of whose caps ARE
// mid-length crossings of the same drilling/boss cylinder, so this new
// code genuinely runs there too) produces an UNCHANGED face count, volume,
// and IsClosedManifold() result, confirming "generate-then-discard" rather
// than merely "never reached".
//
// One real, checked-directly subtlety this increment's own tiny-radius
// regression testing found and fixed: RepresentativeInteriorPointMixed's
// existing, generic SafeInteriorPoint2d-based interior-point heuristic
// (used for every OTHER planar MixedFace shape, unchanged) picks a point
// only a `radius`-scaled epsilon above the polygon's own lowest vertex -
// fine for an ordinary wedge (whose own y-extent is bounded by the WHOLE
// face's scale), but for this new function's own "pure fan" pieces (whose
// own y-extent is bounded ONLY by the circle's radius) that margin shrinks
// right along with the radius, and for a small enough radius relative to
// this pipeline's own boolean tolerance (confirmed directly: radius=0.01
// against a typical ~1e-8 tolerance) lands close enough to the arc
// boundary to be misclassified PointClass::kOn instead of kIn, leaking a
// spurious face into results that should have discarded it. Fixed with a
// closed-form, always-safe representative point (half a radius out from
// center along the arc run's own mid-angle) for exactly this one known
// "pure fan" shape (center plus a full, uninterrupted arc run, the
// signature no OTHER MixedFace producer in this codebase - including
// BuildEndCap's own caps, which never reach this function at all, see
// SynthesizeEndCaps above - happens to build), provably inert on every
// pre-existing shape (RepresentativeInteriorPointMixed's own doc comment,
// boolean.cpp, has the full argument).
//
// Honest remaining scope, unchanged from before this increment: two
// DIFFERENT cylinders whose footprint circles interact on the SAME planar
// face (each gets its own correctly-produced disc independently, but nei
// -ther this fix nor ClipPolygonByCircle3d attempts to reason about a
// once-already-clipped, non-rectangular `poly` losing convexity for a
// second interacting circle); a non-full-circle (partial-sweep)
// cylindrical operand (both circle-clip functions share this same
// unstated full-circle assumption, not newly introduced or newly lifted
// here); and oblique (non-perpendicular) plane/cylinder crossings, whose
// own already-disclosed non-watertight-at-the-wedge-seam limitation is
// entirely unaffected and unwidened by this increment.
//
// An OBLIQUE (non-axis-aligned) Union boss is a straightforward
// generalization of already-exact primitives here (the oblique split
// already marks its own notched end as not-original) but is not separately
// tested by this increment - out of scope, not silently mishandled.
//
// CYLINDER/CYLINDER (PARALLEL AXES) - a later increment: case (iv) above
// (two cylindrical faces interacting) now handles the PARALLEL-axis
// sub-case for BooleanOp::Union: the two cross-sectional circles
// (projected onto the plane perpendicular to the shared axis direction)
// are split by the standard closed-form circle/circle intersection
// (Weisstein, MathWorld, "Circle-Circle Intersection"; Paul Bourke,
// "Intersection of two circles," 1997) into either an unmodified pass-
// through (disjoint circles, or one fully nested inside the other - the
// existing generic classifier already handles both correctly with no
// split) or two angular CylindricalFace children at the two crossing
// angles (SplitCylindricalByParallelCylinder, boolean.cpp), which the rest
// of the pipeline (splitting, classification, bucketing) treats exactly
// like any other cylindrical fragment - no new CylindricalFace field, and
// no change to ClassifyPointVsMixedSolid/RayVsMixedFace/
// RepresentativeInteriorPointMixed/this function's own switch statement,
// since none of those special-case WHY a fragment has the (angle, height)
// trim rectangle it has. BuildEndCap's own former FULL-SWEEP-only
// restriction is relaxed to any positive sweep (its own body already
// built every wedge's boundary from exact straight radial edges, so a
// genuinely partial pie-slice cap needed no new machinery, only a weaker
// guard - see BuildEndCap's own doc comment in boolean.cpp).
//
// Originally restricted, in the increment that added the parallel-axis
// split above, to BooleanOp::Union only. A LATER increment extends
// PARALLEL-axis cylinder/cylinder support to BooleanOp::Intersection and
// BooleanOp::Difference as well, for exactly two axial sub-cases (a THIRD,
// the same "cap-trim" gap Union's own paragraph above already discloses,
// remains genuinely out of scope - see below):
//   - NESTED (one cylinder's circle strictly inside the other's, the same
//     0-crossing regime CylinderCylinderNoInteraction already names): needs
//     ZERO new geometry. The inner cylinder's own wall is already collected
//     into from_a.in/from_b.in unmodified by the existing, unchanged
//     classify-then-bucket pipeline, and its own two original ends already
//     pass the EXISTING ParallelCylinderCapNeedsNoTrim check cleanly
//     (the outer cylinder's axial reach frequently doesn't even overlap the
//     inner one's own end heights) - so the existing on-axis-probe/
//     BuildEndCap path this increment does not touch handles it already.
//   - CROSSING (a genuine 2-point circle/circle intersection): the wall
//     wedges were ALSO already correct before this later increment (the
//     split/classify/bucket pipeline is, and remains, fully op-agnostic -
//     the SAME wedge fragments Union already computed are simply routed
//     into `.in` instead of `.out`/`.on` by the SHARED bucketing switch),
//     but two genuinely NEW pieces of machinery were needed to close the
//     result, both found by direct implementation, not anticipated in
//     advance:
//       (1) SplitCylindricalByOtherCylinderAxialExtent (boolean.cpp): when
//           the two cylinders' finite axial ranges only PARTIALLY overlap,
//           a fragment's classification against the other operand can
//           genuinely differ above vs. below the other's own axial
//           terminus - which a single RepresentativeInteriorPointMixed
//           sample cannot see. This performs the missing axial split
//           (mirroring case (iii)'s own v_cut plane split, but against a
//           bare CylindricalFace's own two ends, which have no explicit
//           PlanarFace to split against anywhere in this pipeline) so the
//           existing classifier gets one representative sample per
//           genuinely-distinct axial band, not one for the whole
//           fragment. Applied to BOTH angular children in the crossing
//           regime (not just the one radially inside the other) - a real,
//           checked-directly correction to this increment's own first
//           attempt, which applied it only to the inside child for
//           classification purposes and left the outside child as one
//           long, unsplit piece: that is classification-safe (the outside
//           child's own kOut answer is genuinely height-invariant) but
//           leaves its own straight rail unable to weld against whichever
//           OTHER fragment (the inside child's own surviving end band, or
//           the other cylinder's own wall) ends up adjoining it at each
//           axial cut - a real, checked-directly open seam, not a
//           theoretical concern. Applied only to the nested regime's own
//           INNER member (never the outer one, which has no such rail-
//           adjacency concern since nothing else ever borders it along
//           the shared axial cuts) - see BuildFaceLoop's own doc comment,
//           brep.cpp, for the OTHER real bug this split-both-children
//           correction re-exposed and fixed: two axially-adjacent bands
//           of the SAME angular child re-sharing their own boundary
//           correctly (2 uses), against a genuinely different child's own
//           short/long-arc cap sharing the identical two endpoint
//           vertices, needed disambiguating by more than vertex identity
//           alone.
//       (2) BuildLensEndCap (boolean.cpp): where a genuinely crossing
//           interactor's own finite axial range reaches an end mid-length
//           (ParallelCylinderCapNeedsNoTrim returning false - previously
//           always a REFUSAL, now this specific trigger instead), the
//           correct cap is the classical two-circle "lens" (Weisstein,
//           MathWorld, "Circle-Circle Intersection"; Bourke, "Intersection
//           of two circles," 1997), NOT a re-trim of a full pie-slice disc
//           - the on-axis probe SynthesizeEndCaps already used for Union
//           is not even a valid proxy for whether this cap is needed here
//           (a genuinely crossing pair's own axes commonly sit outside
//           each OTHER's circle entirely, confirmed by direct
//           counterexample - see that function's own doc comment), so this
//           is checked independently, ahead of the ordinary probe, for the
//           kIn (Intersection/Difference-from-this-side) polarity only.
//           Built as two simple, non-self-touching circular-segment
//           MixedFace pieces sharing the crossing chord as an internal
//           edge (mirroring the "several simple pieces, never one bridged
//           loop" convention circle_clip3d.h's own top comment already
//           establishes) - closed via an interior chord-midpoint vertex
//           rather than the chord's own two crossing-point endpoints
//           directly, because those same two points are ALSO exactly the
//           wall's own v-const cap-edge endpoints (Brep::FromMixedFaces()
//           identifies an edge purely by its endpoint-vertex pair), and a
//           third, fourth edge claiming that identical pair throws
//           Brep::FromMixedFaces()'s own "edge shared by 3+ faces" error -
//           a second genuinely new wrinkle found by direct implementation,
//           not anticipated by this feature's own original scoping. At
//           most ONE genuinely-crossing interactor's own lens is supported
//           per end; a second, simultaneously-reaching crossing cylinder
//           at the same end (a true three-or-more-cylinder mutual
//           interaction, whose real cross-section there is not generally a
//           single lens) is refused (thrown) rather than silently picking
//           one, mirroring ParallelCylinderCapNeedsNoTrim's own
//           "refuse rather than guess" convention.
//   Difference itself needed one further, purely mechanical addition: its
//   own switch branch previously never called SynthesizeEndCaps AT ALL (a
//   real, previously-latent gap, invisible before a bare CylindricalFace
//   operand's exposed original end became reachable through Difference),
//   so `from_a.out` is now capped exactly the way Union caps its own
//   `.out` fragments (identical polarity, identical disclosed cap-trim
//   refusal below), and `from_b.in` is capped exactly the way Intersection
//   caps its own `.in` fragments (kIn polarity, including the new lens
//   path above) and then flipped via the existing, unmodified
//   FlipMixedFace - which itself needed a real fix here too (see
//   FlipFace's own doc comment, boolean.cpp): it used to reverse a planar
//   loop's own vertex order without remapping any attached ArcRun's own
//   begin/angle_begin/angle_end to match, silently corrupting a flipped
//   lens cap's own chord-midpoint vertex (this increment's own first
//   PlanarFace shape with content trailing its own arc run) - fixed and
//   verified directly by this increment's own Difference tests.
//
//   Difference's NESTED sub-case (one cylinder wholly inside the other,
//   no crossing) is fully verified CLOSED and volume-exact, needing none
//   of the machinery below - identical to the nested Intersection case
//   above. Difference's CROSSING sub-case (a genuine lens) is verified to
//   CONSTRUCT correctly (Brep::FromMixedFaces() accepts the result; the
//   tessellated volume matches the closed-form lens-complement formula to
//   well under 1%), and its conforming mesh is a closed manifold: two
//   later increments closed the two conforming-mesh gaps this crossing
//   sub-case originally exposed (1260 non-manifold edges at 64/64 when
//   the sub-case first landed, now 0):
//
//   (1) CLOSED: a crossing pair's own OUTER angular wedge, once axially
//   split into 3 or more bands by SplitCylindricalByOtherCylinderAxialExtent,
//   can have a MIDDLE band with no original ends at all - never an
//   ArcRun match target in Brep::TessellateConforming() the way its own
//   axially-adjacent siblings (each carrying either an ordinary
//   BuildEndCap cap or this increment's own lens cap) are - so that
//   middle band's own conforming mesh used to fall back to a plain,
//   uniform-in-u grid at a different effective density than its capped
//   neighbors, leaving a real, narrow non-manifold seam at the internal
//   wedge-to-wedge cut lines (not a boolean-topology defect - purely a
//   Brep::TessellateConforming() mesh-density reconciliation gap). Fixed
//   by giving such a "friendless" fragment a fallback breakpoint schedule
//   reused directly from an axially-adjacent, already-matched sibling of
//   the SAME wedge (SameWedgeAsCylinder, brep.cpp) rather than an
//   independently-resampled one - see Brep::TessellateConforming()'s own
//   doc comment (brep.h, the "FIFTH gap" entry) for the exact mechanism
//   and TestTessellateConformingFriendlessMiddleBandSyntheticWedgeIsClosedManifold
//   (test_basic.cpp) for the isolated, falsifiable proof.
//
//   (2) CLOSED, by a later increment than (1), which found it while
//   verifying (1): fixing (1) did NOT, by itself, make this exact
//   crossing-Difference fixture a full Mesh::IsClosedManifold() - a
//   SEPARATE gap in Brep::TessellateConforming()'s own dispatch machinery
//   affected A's own INNER angular wedge bands instead. Those bands each
//   have a REAL ArcRun match at BOTH their own ends (an ordinary,
//   4-quadrant-split BuildEndCap cap on one end, a single, un-split
//   BuildLensEndCap arc on the other) - and BuildConformingCylinderMesh
//   (brep.cpp) shared ONE u-breakpoint list between its v=0 and v=length
//   rows, so the quadrant cap's own denser breakpoints leaked, as extra
//   unforced columns, into the row that had to match the sparser lens
//   cap's own simpler boundary loop instead - a genuine T-junction (not
//   a breakpoint-VALUE mismatch: the lens run's own literal points were
//   already bit-identical on both sides). Closed by exactly the
//   restructuring that diagnosis called for: a per-row strip mesher
//   (BuildConformingCylinderStripMesh, brep.cpp) that gives each row its
//   OWN breakpoint schedule and lofts the band between two differently-
//   sampled rows by a monotone-polygon stack sweep - see
//   Brep::TessellateConforming()'s own doc comment (brep.h, the "SIXTH
//   gap" entry) for the mechanism and its dispatch rule, and
//   TestBooleanCombineMixedParallelCylinderDifferenceCrossingIsClosedManifold
//   (test_basic.cpp, plus its asymmetric-divisions and row-schedule
//   companions) for the falsifiable closure claim on this exact fixture.
//   The one conforming-mesh seam that REMAINS open for the mixed
//   pipeline is unrelated to parallel cylinders: an OBLIQUE plane-cut
//   fragment's seam against its oblique PLANAR cap, where the planar side
//   still tessellates by exact clipping over its own grid rather than
//   from the shared literal notch points (the cylinder side now does).
//
// STILL restricted, for BOTH Union and the new Intersection/Difference
// support, to an end whose synthesized cap provably needs no TRIMMING
// against the other, interacting cylinder outside of the two sub-cases
// named above (a synthesized plain pie-slice disc's own near-center
// region can dip into the other cylinder's footprint whenever that other
// cylinder's own finite axial range reaches the cap's own height at all -
// a real, checked-directly correctness risk for substantially-overlapping
// circles sharing an axial span, not a theoretical worry - see
// ParallelCylinderCapNeedsNoTrim's own doc comment, boolean.cpp).
// SynthesizeEndCaps throws std::invalid_argument rather than emit a
// possibly-wrong, untrimmed disc whenever this still applies (Union's own
// kOut polarity has no lens-cap alternative at all; a kIn cap only gets
// the lens treatment when its one crossing interactor's footprint reaches
// that exact height, not for a same-axial-span coincidence generally).
// Genuinely TRIMMING an ordinary plain-disc cap against an interacting
// cylinder (as opposed to building the closed-form lens this increment
// adds for the kIn/crossing case specifically) remains real, tractable,
// closed-form follow-up work, not attempted here: reusing
// detail::ClipPolygonByCircle3d directly on the cap's own already-built
// polygon against the other cylinder's own circular footprint on that same
// plane, exactly case (ii)'s own machinery, just invoked on a synthesized
// face instead of an original operand face - but honestly a further
// increment, not a one-line addition, since it needs SynthesizeEndCaps/
// BuildEndCap re-architected to feed the synthesized cap back into
// SplitMixedAgainstAllFaces' own worklist (so it gets classified/bucketed
// generically) rather than appended directly to the result the way
// today's code does.
// Also out of scope, unaffected and unwidened by the Intersection/
// Difference support above: exact/near-tangent parallel cylinders
// (SplitCylindricalByParallelCylinder / ComputeParallelCylinderCrossing
// throw - the boundary between the "0 crossings" and "2 crossings"
// regimes is a genuine degeneracy, not a closed-form-clean case to split
// on) and a PARTIAL-sweep cylindrical operand on either side of a
// parallel-axis interaction (SplitCylindricalByParallelCylinder throws -
// the identical restriction BuildEndCap/SplitCylindricalByObliquePlane
// already state, for the identical reason: no producer here ever builds
// one).
//
// STEINMETZ (equal-radius, INTERSECTING non-parallel axes) cylinder/
// cylinder pairs are supported for every op, by the classical closed-form
// decomposition (SplitCylindricalBySteinmetzCylinder's own section comment
// in boolean.cpp has the derivation): the intersection curve of two
// equal-radius cylinders whose axes meet at Q factors into two planar
// ellipses (the planes through Q with normals a-b and a+b), the two
// ellipses cross at the two "pinch" points Q +/- r*n on the axes' common
// perpendicular, and on each wall the region inside the other cylinder is
// two eye-shaped regions between the curves, touching the rest of the
// wall only at those two points - NOT a hole punched into the wall's
// interior. Splitting each wall at the two pinch angles yields four
// half-bands (angle pi, one flat original end, the other end a single
// half-ellipse notch - the notched-at-one-end CylindricalFace shape the
// oblique case already uses) and two eyes (angle pi, length 0, BOTH ends
// notched - see CylindricalFace's own doc comment in brep.h), so no new
// trim representation is needed. The four half-ellipses are sampled once,
// on a canonical cylinder chosen independently of argument order, and the
// identical point lists are handed to both cylinders' fragments, so the
// shared boundary is bit-identical on both sides. Union keeps the eight
// half-bands and closes the four original ends with half-discs through
// the unchanged BuildEndCap; Intersection keeps the four eyes;
// Difference keeps one cylinder's half-bands and the other's eyes,
// flipped, as the cavity wall. Any axis angle alpha is supported
// (Intersection volume 16 r^3 / (3 sin alpha)).
//
// Steinmetz PRECONDITIONS, each refused with std::invalid_argument (every
// message naming "non-parallel axes") rather than approximated: equal
// radii; axes that genuinely intersect (a skew pair is the general case
// below); both operands full-sweep and not already notched; and the
// crossing STRICTLY interior to both cylinders - every original end
// farther than r*max(cot(alpha/2), tan(alpha/2)) from the crossing along
// its own axis, which guarantees both eyes lie wholly inside each wall,
// neither end disc touches the other cylinder, and SynthesizeEndCaps'
// on-axis probes read an unambiguous kOut (a cylinder that starts at the
// crossing is refused).
//
// UNEQUAL-RADIUS, intersecting-axis cylinder/cylinder pairs (r_b < r_a,
// the axes meeting at ANY angle alpha in (0, pi), B piercing A
// completely) are supported for every op as well
// (SplitCylindricalByUnequalCylinder's own section comment in
// boolean.cpp has the derivation). The intersection curve is no longer
// a pair of planar ellipses, but on the SMALLER cylinder's wall it has a
// per-angle closed form - "the point at angle theta and height h on B
// lies on A's wall" is a quadratic in h whose discriminant is positive
// on every generator when r_b < r_a - so it is sampled exactly, once,
// uniformly in B's own angle, as four canonical arcs handed verbatim to
// both cylinders' fragments (bit-identical shared boundary, as for
// Steinmetz). On B the curve is two closed curves, one above and one
// below the crossing, each going once around B; on A it is two closed
// loops, one around each point where B's axis pierces A's wall, each
// spanning 2*asin(r_b/r_a) of A's angle (independent of alpha) and
// touching its own theta-extreme generators only at two PINCH points -
// B's generators at common-perpendicular coordinate +/- r_b. Both
// pinches of a loop are at one height on A, cot(alpha) sqrt(r_a^2 -
// r_b^2) above the crossing for one loop and below it for the other
// (the loops' height ranges are r_a cot(alpha) +/- r_b/sin(alpha) and
// its negative, each arc monotone from the pinch height to the loop's
// centre); at a right angle all four pinches are at the crossing's
// height. So, again, no hole is ever punched into a wall's interior:
// A's wall splits at the four pinch angles into two SLABS (each, at its
// own loop's pinch height, an upper piece notched by the loop's upper
// arc, a lower piece notched by its lower arc, and a PLUG - the eye
// shape, angle 2*asin(r_b/r_a), length 0, both caps notched) and two
// PLAIN pieces, each cut by ONE chain from the pinch vertex on its one
// rail to the pinch vertex on its other rail - the flat circle at the
// crossing height when the four pinches are level within the pipeline
// tolerance (the right-angle case, whose results are unchanged bit for
// bit), else a HELIX linear in (angle, height) sampled as a notch
// polyline shared by the two pieces, whose last point sits on the rail
// at the other loop's pinch height (the one sloped rail corner
// Brep::FromMixedFaces() admits - see CylindricalFace in brep.h); B's
// wall splits at its two pinch generators into two halves, each an
// upper band, a positive-length doubly-notched MIDDLE band (the part of
// B's wall inside A) and a lower band, none of which depends on alpha.
// Union keeps A's eight wall pieces, B's four outer bands and
// BuildEndCap's wedges on every original end (12 cylindrical + 48 planar
// faces); Intersection keeps the two plugs and the two middle bands (4
// faces, IsValid and IsSolid, volume the 1-D quadrature closed form
// integral_{-r_b}^{r_b} 4 sqrt(r_a^2 - y^2) sqrt(r_b^2 - y^2) dy divided
// by sin(alpha): at offset y along the common perpendicular the section
// of A is a strip of width 2 sqrt(r_a^2 - y^2) along A's axis, the
// section of B a strip of width 2 sqrt(r_b^2 - y^2) along B's, and two
// strips crossing at alpha meet in a parallelogram of area their widths'
// product over sin(alpha)); A - B keeps A's pieces and B's middle bands
// flipped as the bore's wall (10 + 32); B - A keeps B's outer bands and
// A's plugs flipped (6 + 16) - the same counts at every angle, all from
// the same op-agnostic classify-then-bucket step.
// Brep::TessellateConforming() is a closed manifold on every one of
// these results at symmetric and asymmetric divisions (measured within
// ~1e-4 relative of the closed forms at r 2/1, 2/1.5 and 2/1.9, from 30
// to 120 degrees); ordinary Brep::Tessellate() converges but carries the
// same shared-curve T-junctions the Steinmetz results do. The changes
// this needed outside boolean.cpp are in Brep::FromMixedFaces(): B's
// middle band's straight rail and A's plain piece's cut join the same
// two pinch vertices, and are kept as distinct edges whether that cut
// is the right-angle arc or the general-angle helix (an arc or a
// polyline and its chord are never one curve - see BuildFaceLoop in
// brep.cpp), and a notch chain may end at a sloped rail corner. A
// sloped-corner piece re-extracted by Brep::MixedFaces() comes back, as
// every notched piece does, as a record spanning its trim's bounding
// height band with the notch chain, not as the original piece.
//
// SKEW axes (the axis lines' closest points farther apart than the
// pipeline tolerance, distance d) are supported too, for every op above,
// PROVIDED the smaller cylinder still fully pierces the larger one on
// every generator - d + r_b < r_a, a single closed-form condition
// independent of alpha (the per-angle discriminant on the smaller
// cylinder is minimized at the generator closest to the larger
// cylinder's axis, and is positive there exactly under this condition -
// see SplitCylindricalByUnequalCylinder's own section comment in
// boolean.cpp for the derivation) - AND the axes meet at a RIGHT ANGLE
// (any d), or are actually intersecting (d = 0, any angle). The
// representation above needs no extension for the skew case: B's two
// pinch generators still exist and still bound its two bands, and
// CylindricalFace's own rail-corner contract (brep.h) already lets a
// notch chain's far rail corner sit at a height other than its near
// one - what changes is only that this asymmetry, guaranteed absent for
// intersecting axes, is now common: on the larger cylinder it appears
// exactly when alpha != 90 degrees AND d != 0 (a right-angle skew pair
// still anchors both loops level, since cot(90 degrees) = 0), and on the
// smaller cylinder it appears whenever d != 0, at ANY axis angle. A
// genuinely OBLIQUE skew pair (alpha != 90 degrees AND d != 0) is
// measured, not assumed, to be out of scope: there a single loop's two
// pinch heights on the LARGER cylinder can themselves differ (equal only
// at d = 0 or alpha = 90), and Brep::TessellateConforming()'s strip
// mesher does not yet triangulate a slab built from two such heights
// into a closed manifold, even though the B-rep it builds is
// ON_Brep::IsValid() - a strip-mesher limitation, refused by
// ComputeUnequalCylinderCrossing's own guard rather than shipped broken.
// A skew pair whose smaller cylinder does NOT fully pierce the larger
// one (a partial penetration) is refused for the separate reason a
// blind bore is - its curve would run into an end disc this pipeline
// has no face for.
//
// Unequal-radius PRECONDITIONS, each refused with std::invalid_argument
// naming "non-parallel axes" and "UNEQUAL radii": axes that neither
// intersect nor, if skew, fully pierce (d + r_b < r_a); a genuinely
// OBLIQUE skew pair (alpha != 90 degrees AND d != 0, naming "OBLIQUE" -
// the strip-mesher limitation above); both operands
// full-sweep and not already notched; a radius ratio (and, for a skew
// pair, closest-axis distance) that leaves no slab or plain piece
// narrower than 1e-3 radians; samples of the four arcs and of the sloped
// cut chains more than 10x the tolerance apart; and
// the crossing STRICTLY interior to both cylinders - every original end
// farther from the crossing than the curve's axial reach on that
// cylinder ((r_a + r_b |cos alpha|)/sin(alpha) along B's axis, (r_b +
// r_a |cos alpha|)/sin(alpha) along A's, read off the samples) plus the
// sampled arcs' sagitta - so both loops lie wholly inside each wall,
// neither end disc touches the other cylinder and SynthesizeEndCaps'
// probes read kOut; a blind bore or a partial penetration is refused.
// The reach grows without bound as alpha approaches 0 or pi, so a
// nearly-parallel pair needs correspondingly long operands; a pair
// parallel within the axis-alignment tolerance takes the parallel-axis
// path instead.
//
// NON-PARALLEL NO-INTERACTION pairs: before any of those preconditions is
// consulted, a non-parallel pair whose two FINITE cylinders provably
// cannot touch passes through the split unchanged - exactly the way a
// disjoint PARALLEL pair already does - so its Union is both solids (each
// wall one face, every original end capped), A - B is A unchanged, and
// its Intersection is empty, for any radii and any axis placement. Two
// closed-form tests decide this (NonParallelCylinderPairNoInteraction,
// boolean.cpp), either sufficient on its own:
//   - EXACT for equal radii and intersecting axes: the region inside both
//     infinite cylinders (the Steinmetz solid) extends along either axis
//     exactly r*max(cot(alpha/2), tan(alpha/2)) either side of the
//     crossing - the same bound as the extent precondition, which is the
//     solid's true axial reach, not an over-estimate - so if that band
//     misses EITHER cylinder's own axial extent (by more than the
//     pipeline tolerance), the pair never meets. Along each axis this is
//     exact: a pair separable by one axis alone is never refused. A pair
//     whose bands overlap both extents while the crossing is not strictly
//     interior to both cylinders (a genuine or, at a general angle, a
//     near-tip partial end crossing) still throws the extent refusal
//     above, message unchanged; so does a pair that touches only through
//     the combination of both axial clips.
//   - CONSERVATIVE for every other pair (skew axes and/or unequal radii):
//     each finite cylinder lies inside the capsule of its own axis
//     segment with its own radius, so two pairs whose axis SEGMENTS are
//     more than r_a + r_b + tol apart (standard closed-form segment/
//     segment distance) never meet. A skew or unequal-radius pair the
//     capsule test cannot separate - the segments within r_a + r_b, which
//     for pegs meeting end-to-side can still be a disjoint pair - goes on
//     to the unequal-radius or Steinmetz split, whose own guards then
//     decide (an unequal-radius pair at a general angle, skew, or not
//     piercing completely still throws, naming "UNEQUAL radii" and the
//     specific reason).
// A fragment's axial extent is its [0, length] widened over any notch it
// already carries, so partial-sweep or already-notched fragments are
// passed through as well when the pair provably never meets; their
// guards still fire when it does. The verdict is argument-order
// independent (the pair is canonically ordered before any arithmetic),
// so neither the (a, b) nor the (b, a) split direction ever throws for a
// no-interaction pair. A no-interaction Union/Difference result's
// conforming tessellation is a closed manifold (only the quadrant end
// caps meet plain full-sweep walls, the configuration already verified
// closed for the disjoint parallel-axis case).
//
// Steinmetz TESSELLATION: ordinary Brep::Tessellate() covers every
// fragment exactly (volumes converge to the closed forms, -1.6e-4
// relative at 256 divisions), but the two cylinders tessellate their
// sides of each shared half-ellipse on their own parameter grids, so the
// mesh has T-junctions there and is NOT Mesh::IsClosedManifold() - the
// same disclosed limitation the oblique plane+cylinder case carries.
// Brep::TessellateConforming() IS watertight on every Steinmetz result:
// its per-row strip mesher (see that method's own doc comment in brep.h)
// meshes each eye as a length-0 strip between its two literal
// half-ellipse sample lists and each half-band as a strip between its
// literal notch list and its cap-forced flat row, so the shared curves
// are vertex-identical across the two cylinders and the eye is honored
// rather than filled back in - Mesh::IsClosedManifold() holds and the
// volume lands within ~1e-4 relative of the closed forms for Union,
// Intersection and both Differences at 90 and 60 degrees, at symmetric
// and asymmetric divisions (see the Steinmetz tests in
// tests/test_basic.cpp for the exact measured residuals).
//
// RESULTS AS OPERANDS (chained calls): a BooleanCombineMixed result is a
// first-class operand of a second call, on two supports that a chained
// call used to lack - the measured defects and their fixes:
//   - VERBATIM FACE RECORDS. Brep::MixedFaces() hands back the exact
//     PlanarFace/CylindricalFace records Brep::FromMixedFaces() built the
//     result from (see that method's own doc comment in brep.h): a
//     notched wall keeps its cap0/cap1_notch_points (an oblique hole's
//     201-point ellipses, a Steinmetz eye's two half-ellipses, a
//     half-band's one), its true frame.origin/length (the former
//     geometric re-extraction read the trim's bounding box, which a notch
//     widens - an oblique drilled box's wall came back 10.889 long instead
//     of 10/cos 15 = 10.353, and a rebuilt shared-notch solid came back
//     +26% in volume with its notch filled in), and a cap piece keeps its
//     arc runs. So a Steinmetz Union/Intersection or an oblique drilled
//     box rebuilt from its own MixedFaces() reproduces its volume and is
//     still closed under the conforming mesher.
//   - The CLOSED-OPERAND RULE (ToMixed, boolean.cpp): an operand is
//     either a BARE TUBE - only plain, un-notched cylindrical faces, the
//     legacy drill/boss operand Brep::FromMixedFaces({}, {cf}) - or a
//     CLOSED SOLID whose own faces already bound it (any planar face, or
//     any notched cylindrical face). Only a bare tube keeps the
//     implicit-end-disk classification (RayVsMixedFace) and the end-cap
//     synthesis (SynthesizeEndCaps) its end flags encode; a closed
//     operand's cylindrical faces have both end0/end1_is_original forced
//     false before anything reads them, so no implicit disk is cast at
//     their ends and no cap is synthesized for their fragments. This is
//     an OPERAND property, not a face property: a boss's base inside a
//     union result is a still-original end of the input cylinder AND an
//     open passage into the box. Before this rule every chained call
//     whose first result kept a cylindrical face re-fired the cap
//     synthesis on that face's true/true flags and stitched 8 spurious
//     quadrant caps across the FIRST hole (Difference(Difference(box,
//     h1), h2) measured 1000 - 20 pi - 10 pi/3, i.e. -1.1%; Union(drilled
//     box, box2) -0.7%; Difference(Union(box, boss), hole) threw "3 or
//     more faces" where the spurious disc's edges collided).
//   Measured with both in place (ordinary 64-division volumes, relative
//   to the closed forms): a second hole through a drilled box (parallel
//   or perpendicular) +7.6e-5 with 18 planar + 2 cylindrical faces (the
//   26 + 2 of the spurious caps gone); a far hole through a box-plus-boss
//   union, in either order, -2.1e-5; Intersection/Union/Difference of a
//   drilled box with a second box +4.1e-5 / +2.4e-5 / +7.6e-5; a boss on,
//   a far hole through, and a planar cut of the oblique drilled box
//   +6.2e-6 / +4.6e-5 / +3.8e-5 (its doubly-notched wall passes through
//   the non-parallel no-interaction test verbatim); the shared-notch
//   fixture cut above its notch -1.0e-4. Every existing operand is a
//   planar-only solid or a bare un-notched tube, so the whole prior suite
//   is bit-for-bit unchanged. An operand carrying a ConicalFace (a
//   tapered fillet) is refused (std::invalid_argument) - it used to have
//   its cones silently dropped from its boundary.
//   - NOTCH-AWARE CLASSIFICATION. ClassifyPointVsMixedSolid's ON-check and
//     RayVsMixedFace's cylindrical branch used to test a notched
//     CylindricalFace's own true (angle, height) trim against its
//     UN-notched flat [0, length] rectangle - a point genuinely carved
//     away by the notch at that angle (inside the flat rectangle but
//     outside the notch curve) misclassified kOn/counted a ray crossing
//     it should not have, and CylinderPlaneNoInteraction's own closed-form
//     bound swept only that same flat rectangle, so a plane reaching ONLY
//     a notch's own extended material (a cap0 notch generally dips below
//     v=0, a cap1 notch generally rises above v=length) could be wrongly
//     declared non-interacting and passed through unmodified instead of
//     being classified or split at all. Both now consult the notched
//     cap's own interpolated (angle, height) curve directly (the same
//     dense sample lists FromMixedFaces() itself reinterprets) instead of
//     the flat rectangle; for an un-notched face every formula reduces to
//     the exact prior arithmetic, so this is a bit-for-bit no-op on every
//     pre-existing fixture (confirmed: the whole prior suite is unchanged).
//     A fully-enclosing or fully-disjoint second operand's own planar
//     faces already resolved via the flat CylinderPlaneNoInteraction bound
//     before this fix too (its notch never reaches far enough to matter
//     for those), so no measured volume changes for that shape of second
//     operand either - this closes a real, source-confirmed
//     misclassification risk rather than a defect visible in any of this
//     codebase's own existing fixtures.
//   - MID-LENGTH SPLIT NOW CLEARS THE STALE NOTCH IT CANNOT KEEP. A plane
//     perpendicular to a cylindrical fragment's axis that lands strictly
//     inside (0, length) used to build its two children by a plain field
//     copy of the original fragment (case (iii)'s own align>1-kAxisAlignTol
//     mid-length branch, and the identical pattern in
//     SplitCylindricalByOtherCylinderAxialExtent for a parallel-cylinder
//     pair's own axial-extent split) - correcting `length`/`frame.origin`/
//     one end{0,1}_is_original flag, but never the notch fields. Each
//     child's OWN untouched end keeps its inherited cap0/cap1_notch_points
//     correctly (nothing changed there), but each child's OWN fresh-cut end
//     is never a genuine terminus of the input, so whichever notch the
//     PARENT had at THAT same cap index (if any) still described the
//     parent's old, no-longer-existent boundary - FromMixedFaces() (rightly)
//     rejected the mismatch as a rail-corner error rather than silently
//     building the wrong shape. Both call sites now clear the fresh-cut
//     side's own cap notch (points + tolerance) unconditionally - a
//     no-op for the overwhelmingly common un-notched fragment, since
//     clearing an already-empty vector changes nothing. This is a pure
//     "which cap can this child still claim" correction: it does not
//     interpolate or re-derive a notch for the new cut face (there isn't
//     one - the cut is flat), it only stops attaching someone else's notch
//     to a boundary that no longer has it.
//   - INSIDE-DISC PRODUCER NOW COVERS A SEALED END AT THE BOUNDARY. Case
//     (ii)'s own mid-length inside-circle-disc producer (the piece of a
//     crossing planar face that refills a cylindrical hole's interior)
//     used to gate strictly on v_cut in the OPEN interval (0, length),
//     excluding a plane that lands EXACTLY at v=0 or v=length - even when
//     that end is a sealed, non-original terminus (end0_is_original/
//     end1_is_original false; see those fields' own doc comment), where a
//     real disc of material genuinely belongs and nothing else in the
//     pipeline supplies it (a genuinely OPEN original end at the same
//     boundary is already closed by SynthesizeEndCaps and must not
//     double-count, so the gate widens only for a sealed end). Confirmed
//     directly: a box refilling a through-hole exactly flush with the
//     hole's own sealed far end used to leave a real, volume-measurable
//     gap in the roof there (Union(drilled box, a flush cover) measured
//     989.529747 against the true 1000.000000 - a deficit of 10*pi/3,
//     the exact divergence-sum signature of a missing disc at that
//     height); now measures the true value.
//   - CASE (i) NOW CARRIES arc_runs THROUGH A GENUINE PASS-THROUGH. The
//     both-planar split used to rebuild each surviving loop's MixedFace
//     with only plane+loop, unconditionally dropping PlanarFace::arc_runs
//     - even when the second plane never actually clips this face at all
//     (SplitByHalfspace's own "keep both children" variant only inserts
//     new points at a real crossing, so a face with every vertex already
//     on the inside halfspace comes back as the exact same point sequence
//     in the exact same order - the stored begin/count indices are still
//     valid for it). That case is now detected (split.outside empty
//     before CleanPolygon) and arc_runs is carried forward verbatim; a
//     genuinely-clipped face (the plane actually cuts the loop) still
//     drops arc_runs exactly as before, deliberately - the surviving
//     loop's vertices may be reordered or have new intersection points
//     inserted, so the old indices are not safely reusable without new
//     re-detection logic this fix does not add. Confirmed directly: an
//     enclosing box that never clips a Steinmetz union's own 32 wedge
//     caps used to drop arc_runs on all 32 (1 -> 0); now all 32 keep it.
//   Still out of scope, each thrown honestly, not approximated: a second
//   cut that INTERACTS with a notched or partial-sweep fragment (every
//   cylinder-pair producer's own partial-sweep/already-notched guard, and
//   a plane crossing a notched wall's own notch), a plane containing a
//   wall's axis direction (the grazing refusal), a plane meeting a wall
//   that a previous cut left as several fragments of one cylinder (each
//   fragment punches the plane separately - "3 or more faces" - this is
//   also why the shared-notch fixture cut through its own notch band still
//   throws even after the mid-length split fix above: the split producer
//   itself now succeeds, but that fixture's wall is several fragments of
//   one cylinder for an unrelated, pre-existing reason and the final
//   reassembly still refuses the resulting edge multiplicity), a third
//   hole whose circle straddles an earlier hole's wedge cut
//   (ClipPolygonByCircle3d's partial-overlap refusal), and a genuinely-
//   clipped planar face's own arc_runs (the case (i) note above).
//
// SYMMETRIC DIFFERENCE: BooleanOp::SymmetricDifference returns
// Brep::Compound({Difference(a, b), Difference(b, a)}) - two lumps in one
// Brep, NOT welded to each other (see Brep::Compound in brep.h). It has
// to: along the intersection curve of the two boundaries the XOR boundary
// has four incident faces (A's outside, B's outside, and the two flipped
// insides), which a single FromMixedFaces shell cannot hold - the former
// Difference(Union, Intersection) chain threw "an edge is shared by 3 or
// more faces" for EVERY operand kind, two plain overlapping boxes
// included, in BooleanCombinePlanar and BooleanCombineMixed alike.
// Manifold's own mesh XOR keeps the touching curve's vertices duplicated
// for the same reason (its welded result has 4-fold edges). Measured:
// two overlapping unit-ish boxes 14.000 exactly (IsValid, IsSolid, 48
// planar faces, each lump 7 and closed under the conforming mesher);
// disjoint boxes 16; a nested pair collapses to the single lump A - B
// (7); the Steinmetz pair at 90 and 60 degrees within 2e-6 (conforming)
// of 2 pi r^2 L - 2 V_int with 12 cylindrical + 32 planar faces; a box
// and a through-hole within 3e-6 of 1000 - 10 pi + 2 pi. A compound is
// refused as an operand of either boolean (std::invalid_argument): a
// boolean over lumps needs Difference/Intersection distributed per lump
// plus a merge step for Union, a later increment. Difference's own
// coincident-face rule carries into each lump unchanged: a same-normal
// coincident pair is dropped in both lumps (the XOR has no boundary
// there), an opposite-normal pair is kept in both (two lumps touching
// face to face).
// Finally: Brep::TessellateConforming()'s own quad-vs-quad seam pass
// (tasks #55-57's own domain, both files completely untouched by this
// increment) used to have its own SEPARATE, pre-existing limitation,
// newly exposed (not caused) by this increment's own tests: a box where
// only ONE of its two z-perpendicular caps gets wedge-split while the
// other stays a single untouched quad - a face-topology combination no
// test before this increment ever built - left the box's own
// untouched-cap corners with real open boundary edges, unrelated to and
// far from this fix's own new seam (both were separately, directly
// confirmed watertight at the time - see
// TestBooleanCombineMixedUnionBossFlushBaseVolumeAndCapSeamIsClosed's own
// comment in tests/test_basic.cpp for the original measurement). Task #65
// closed that gap AT SYMMETRIC u_divisions/v_divisions (see
// Brep::TessellateConforming()'s own doc comment in brep.h for the exact
// mechanism and its own honestly-disclosed asymmetric-divisions scope
// limit); both Union/boss tests above now assert a full, unscoped
// Mesh::IsClosedManifold() rather than the height-scoped probe they used
// to need.
//
// Explicitly OUT OF SCOPE, and this throws std::invalid_argument (or, for
// the grazing-incidence sub-case, std::runtime_error surfaced through
// detail::ComputeEllipseFrame3d) rather than silently approximating: a
// planar/cylindrical pair at a GRAZING (near-axis-parallel) angle (the
// ellipse's own semi-major axis is unboundedly large there), an oblique
// interaction against a partial-sweep cylindrical operand, a
// non-monotonic (re-entrant) oblique crossing, and two CYLINDRICAL faces
// with NON-PARALLEL axes that INTERACT (or cannot be proven not to)
// outside the Steinmetz and unequal-radius preconditions above (see the
// CYLINDER/CYLINDER, STEINMETZ, UNEQUAL-RADIUS and NON-PARALLEL
// NO-INTERACTION paragraphs for what IS supported - unequal radii at ANY
// axis angle, intersecting OR skew, are now covered there), each for a
// specific, named reason: a PARTIAL penetration - a cylinder ending
// inside the other on intersecting axes, or a skew pair whose smaller
// cylinder does not fully pierce the larger one (d + r_b >= r_a, d the
// axes' closest-point distance) - where the curve is a single loop on
// each wall whose split points differ between the two walls, so a notch
// chain would need to carry several edges, a further decomposition step
// this pipeline does not yet have. This needs no NURBS-NURBS surface
// intersection (the curve's per-angle closed form holds for any pair of
// cylinders, skew included - see UNEQUAL-RADIUS above); it is a
// representational follow-up.
//
// Point-in-solid classification (the other half of the non-convex
// pipeline, alongside splitting) gets one new, exact closed-form branch:
// a ray cast against a cylindrical face is a standard ray-vs-infinite-
// cylinder quadratic (project the ray into the plane perpendicular to the
// cylinder's axis), with true axial height and true angle recovered by a
// dot product and an atan2 respectively, checked against the
// CylindricalFace's own axis-aligned (angle, height) trim rectangle - see
// this function's own .cpp comment for the exact derivation. The existing
// graze-the-boundary fallback (try the next GenericRayDirections() entry)
// carries over unchanged in spirit, generalized to also detect a graze on
// a cylindrical face's own (angle, height) rectangle boundary.
Brep BooleanCombineMixed(const Brep& a, const Brep& b, BooleanOp op);

}  // namespace dino8::kernel
