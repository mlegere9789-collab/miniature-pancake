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
// composed from those three, same as BooleanCombine()'s mesh-boolean
// version) between two planar-faced solids of ARBITRARY shape - the
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
//     and why neither is silently mishandled. A DISCLOSED, NOT extended,
//     limitation versus the perpendicular case above: Brep::
//     TessellateConforming()'s own circle-specific arc-reconciliation
//     machinery is not extended to the ellipse case, so an oblique-cut
//     result's own ordinary Tessellate() output carries the SAME known,
//     already-disclosed non-watertight-at-the-wedge-seam limitation the
//     PERPENDICULAR case already has without TessellateConforming() (see
//     that function's own doc comment) - this is not a new gap, just an
//     un-widened existing one.
//   - a face pair with NO possible interaction at all (checked via a
//     closed-form conservative bound on the cylinder's own signed
//     distance to the other face's plane) is left completely unmodified -
//     the same behavior BooleanCombinePlanar's own planar-only pipeline
//     already has for two faces that don't intersect, so a face with no
//     cylindrical interaction (e.g. a drilled box's own four side walls,
//     when the hole's footprint stays strictly inside the box's own
//     cross-section) reduces EXACTLY to what BooleanCombinePlanar would
//     already do with it.
//
// Explicitly OUT OF SCOPE, and this throws std::invalid_argument (or, for
// the grazing-incidence sub-case, std::runtime_error surfaced through
// detail::ComputeEllipseFrame3d) rather than silently approximating: a
// planar/cylindrical pair at a GRAZING (near-axis-parallel) angle (the
// ellipse's own semi-major axis is unboundedly large there), an oblique
// interaction against a partial-sweep cylindrical operand, a
// non-monotonic (re-entrant) oblique crossing, and any two CYLINDRICAL
// faces interacting (or potentially interacting) at all - the last needs a
// genuine NURBS-NURBS surface intersection and re-trim step (see
// IntersectSurfaces in dino8-app's own geom layer for the
// intersection-curve half of that, not yet wired to a Brep boolean here),
// a materially bigger, separate follow-up this increment doesn't attempt.
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
