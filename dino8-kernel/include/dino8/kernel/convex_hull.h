#pragma once

#include <vector>

#include "dino8/kernel/brep.h"

namespace dino8::kernel {

// Exact (double-precision, planar-polygon) 3D convex hull of `points`,
// returned as a genuine Brep built via Brep::FromPlanarFaces - NOT the
// Manifold-backed ConvexHull() in boolean.h, whose Mesh result is a
// purely triangulated facet soup (single-precision ON_3fPoint vertices)
// that can never be fed into this kernel's own exact planar operations:
// BooleanIntersectConvexPlanar, BooleanCombinePlanar, ShellConvexPlanar,
// and FilletConvexEdge all require a Brep whose faces resolve to genuine
// Brep::PlanarFace polygons via PlanarFaces(), which a Mesh simply
// cannot do. ExactConvexHull() closes exactly that gap: hull a point
// cloud, then fillet/shell/boolean the result directly - zero mesh
// round-trip, zero adaptation code on the receiving side.
//
// Algorithm: classical QuickHull (Barber, Dobkin & Huddleston, "The
// Quickhull Algorithm for Convex Hulls," ACM Transactions on
// Mathematical Software 22(4), 1996 - the same published, unpatented,
// divide-and-conquer algorithm underlying qhull and CGAL's own 3D convex
// hull), implemented here from that description, not from or against any
// proprietary kernel's source:
//   1. Seeds an initial tetrahedron from the 6 axis-extreme points (an
//      O(1)-per-axis scan, not a search over every direction): the pair
//      among those 6 with maximum separation, then the point (searched
//      over ALL of `points`, not just the 6) farthest from that line,
//      then the point (again over ALL of `points`) farthest from the
//      plane those three span - the standard QuickHull seeding. Throws
//      std::invalid_argument if that farthest plane distance is within
//      tolerance of zero: `points` is coplanar (or collinear, or all
//      coincident), and no 3D hull exists.
//   2. Partitions the remaining points into each of the 4 seed faces'
//      own "outside set" (Barber/Dobkin/Huddleston's own term): points
//      whose signed distance to that face's own plane is positive.
//   3. Repeatedly: take any live face with a nonempty outside set, find
//      its farthest outside point (the new apex), find every OTHER live
//      face also visible from that apex - on a convex polytope the
//      visible set as seen from an external point is always a single
//      connected patch, so this is a plain per-face distance test, no
//      adjacency graph needed to discover it - find the "horizon" (the
//      boundary loop between the visible patch and the rest of the
//      hull, via a directed-edge map: a horizon edge is one whose
//      opposite-direction copy belongs to a face that ISN'T visible),
//      delete the visible faces, fan one new triangle from the apex to
//      each horizon edge (preserving that edge's own direction, which is
//      what keeps every new face's winding consistently outward with no
//      separate orientation pass), and redistribute the deleted faces'
//      own outside points among just those new faces - provably
//      sufficient, not merely convenient: enlarging the hull to include
//      the apex can only ever expose a leftover point as outside one of
//      the NEW cap faces, never a face that didn't change (see
//      convex_hull.cpp's own comment at the redistribution step for the
//      short convexity argument).
//   4. Terminates when every live face's outside set is empty.
//
// The genuinely new part beyond a plain triangulated mesh hull (what
// ConvexHull() in boolean.h already gives, and what step 3 above
// produces on its own, before any further work): the resulting triangles
// are grouped by supporting plane using the exact same tolerance-based
// plane-equality test BooleanIntersectConvexPlanar/BooleanCombinePlanar
// already use (own-plane DistanceTo <= tol && zaxis parallel within
// 1e-6 - see boolean.cpp's own `same_plane`), each group's incident
// vertex set is projected into that plane's own local (x, y) axes (the
// same convention boolean.cpp's ProjectOntoPlaneAxes and Brep::
// FromPlanarFaces both already use), and - since the solid is convex, so
// every vertex incident to a given face is automatically ON that face's
// own true boundary, none in its interior - that face's exact polygon is
// recovered as the 2D convex hull of those projected points via Andrew's
// monotone chain (a second small, classical, published algorithm: see
// e.g. Preparata & Shamos, "Computational Geometry," or de Berg, van
// Kreveld, Overmars & Schwarzkopf, "Computational Geometry: Algorithms
// and Applications," for the standard sort-and-sweep construction). A
// cube's 12 triangular facets come back as 6 exact quads this way, wound
// CCW-outward to match every other PlanarFace factory's own convention,
// assembled via the existing Brep::FromPlanarFaces() - so the result
// plugs directly into FilletConvexEdge, ShellConvexPlanar,
// BooleanIntersectConvexPlanar, and BooleanCombinePlanar with no
// adaptation on their side.
//
// SCOPE, stated as plainly as this kernel's other convex-only primitives
// (BooleanIntersectConvexPlanar/ShellConvexPlanar/FilletConvexEdge) state
// theirs: convex-only by definition - QuickHull computes a hull, which
// is always convex, so "a non-convex hull" isn't a thing this could even
// mean - and points-only, planar-output: it doesn't attempt a curved
// hull surface (e.g. one that should include a rolling-ball-style
// blended edge) - a real, out-of-scope future combination with
// FilletConvexEdge, not silently approximated. Throws
// std::invalid_argument if `points` has fewer than 4 entries (matching
// ConvexHull()'s own existing error contract for the same reason - fewer
// can't bound a nonzero 3D volume) or if every point is coplanar,
// collinear, or coincident (no 3D hull exists) - see step 1 above.
Brep ExactConvexHull(const std::vector<Point3d>& points);

}  // namespace dino8::kernel
