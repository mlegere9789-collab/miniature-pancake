#include "dino8/kernel/boolean.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include <manifold/manifold.h>

#include "dino8/kernel/detail/arc_schedule3d.h"
#include "dino8/kernel/detail/circle_clip3d.h"
#include "dino8/kernel/detail/ellipse_clip3d.h"
#include "dino8/kernel/detail/halfspace_clip3d.h"
#include "dino8/kernel/detail/polygon2d.h"

namespace dino8::kernel {

namespace {

// Manifold's own default tolerance is tuned for "a few units" scale models,
// and calling SetTolerance() unconditionally on every operand - even ones
// that already boolean cleanly at the default tolerance - is NOT a free
// robustness improvement: raising the tolerance triggers real topology
// simplification (coplanar triangle merging, short-edge collapse; see
// Manifold::SetTolerance's own doc comment), which measurably changed
// results on ALREADY-WELL-CONDITIONED geometry during testing here (a
// two-plane coplanar-face merge that unions two 1-unit-tall slabs and
// slices the result: applying an adaptive tolerance up front changed the
// slab union's own triangulation enough to corrupt the mid-height slice,
// producing a zero-area result instead of the correct one - confirmed by
// reverting just this one call and rebuilding). So this tolerance is used
// only as a FALLBACK, engaged in BooleanCombine/SplitByPlane below after
// the default-tolerance attempt has already failed - never applied to an
// operation that would otherwise have succeeded unchanged.
double AdaptiveManifoldTolerance(const ON_Mesh& raw) {
  ON_BoundingBox bbox;
  if (!raw.GetBoundingBox(bbox) || !bbox.IsValid()) return 0.0;
  const double diag = bbox.Diagonal().Length();
  if (!std::isfinite(diag) || diag <= 0) return 0.0;
  // 1e-6 of the mesh's own diagonal: small enough not to erase real detail
  // on ordinary models, but large enough to bridge the float-precision
  // noise (~1e-7 relative) that FromManifold's single-precision vertices
  // already introduce at any scale.
  return diag * 1e-6;
}

manifold::Manifold ToManifold(const Mesh& mesh) {
  const ON_Mesh& raw = mesh.raw();

  manifold::MeshGL gl;
  gl.numProp = 3;
  gl.vertProperties.reserve(static_cast<size_t>(raw.m_V.Count()) * 3);
  for (int i = 0; i < raw.m_V.Count(); ++i) {
    const ON_3fPoint& v = raw.m_V[i];
    gl.vertProperties.push_back(v.x);
    gl.vertProperties.push_back(v.y);
    gl.vertProperties.push_back(v.z);
  }

  gl.triVerts.reserve(static_cast<size_t>(raw.m_F.Count()) * 6);
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& face = raw.m_F[i];
    gl.triVerts.push_back(static_cast<uint32_t>(face.vi[0]));
    gl.triVerts.push_back(static_cast<uint32_t>(face.vi[1]));
    gl.triVerts.push_back(static_cast<uint32_t>(face.vi[2]));
    if (face.IsQuad()) {
      gl.triVerts.push_back(static_cast<uint32_t>(face.vi[0]));
      gl.triVerts.push_back(static_cast<uint32_t>(face.vi[2]));
      gl.triVerts.push_back(static_cast<uint32_t>(face.vi[3]));
    }
  }

  manifold::Manifold m(gl);
  if (m.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::BooleanCombine: input mesh is not a valid closed "
        "manifold (Manifold::Status() != NoError) - booleans require "
        "watertight solids, not arbitrary tessellated surfaces");
  }
  return m;
}

// NOTE - genuine kernel-level precision ceiling, not fixed here: `ON_Mesh`
// (dino8::kernel::Mesh's underlying storage, used identically by every
// other mesh consumer in this codebase - rendering, BrepMesher, Remesh,
// etc., not just booleans) stores vertex coordinates in `ON_3fPoint`,
// i.e. single-precision floats, independent of Manifold's own (double-
// precision) internal representation. Every round trip through
// ToManifold()/FromManifold() therefore quantizes coordinates to ~7
// significant decimal digits. For a solid whose absolute coordinate
// magnitude is large (e.g. ~1e6 units), that quantization step is itself
// on the order of 0.1 unit - larger than many real modelling tolerances -
// regardless of how tight the tolerance passed to Manifold is. This is not
// something AdaptiveManifoldTolerance (above) can compensate for: that
// tolerance only controls how aggressively Manifold merges/collapses
// features that are already coincident to within the stored (already-
// quantized) float coordinates; it cannot recover precision the float
// storage already discarded. Fixing this for real would mean migrating
// dino8::kernel::Mesh off ON_Mesh's single-precision vertex array to a
// double-precision store throughout the kernel (rendering, meshing, every
// other consumer) - a large, invasive change out of scope here. See
// dino8-app/tests/adversarial_corpus_notes.md for the specific test case
// this limits and why it's a structural ceiling rather than a boolean bug.
Mesh FromManifold(const manifold::Manifold& m) {
  const manifold::MeshGL gl = m.GetMeshGL();

  Mesh mesh;
  ON_Mesh& raw = mesh.raw();

  raw.m_V.Reserve(static_cast<int>(gl.NumVert()));
  for (uint32_t i = 0; i < gl.NumVert(); ++i) {
    raw.m_V.Append(ON_3fPoint(gl.vertProperties[i * gl.numProp + 0],
                               gl.vertProperties[i * gl.numProp + 1],
                               gl.vertProperties[i * gl.numProp + 2]));
  }

  raw.m_F.Reserve(static_cast<int>(gl.NumTri()));
  for (uint32_t i = 0; i < gl.NumTri(); ++i) {
    ON_MeshFace face;
    face.vi[0] = static_cast<int>(gl.triVerts[i * 3 + 0]);
    face.vi[1] = static_cast<int>(gl.triVerts[i * 3 + 1]);
    face.vi[2] = static_cast<int>(gl.triVerts[i * 3 + 2]);
    face.vi[3] = face.vi[2];
    raw.m_F.Append(face);
  }

  return mesh;
}

manifold::OpType ToManifoldOp(BooleanOp op) {
  switch (op) {
    case BooleanOp::Union:
      return manifold::OpType::Add;
    case BooleanOp::Intersection:
      return manifold::OpType::Intersect;
    case BooleanOp::Difference:
      return manifold::OpType::Subtract;
    case BooleanOp::SymmetricDifference:
      break;  // handled separately in BooleanCombine - not a single Manifold op
  }
  throw std::invalid_argument("dino8::kernel::BooleanCombine: unknown BooleanOp");
}

}  // namespace

Mesh BooleanCombine(const Mesh& a, const Mesh& b, BooleanOp op) {
  if (op == BooleanOp::SymmetricDifference) {
    const Mesh union_mesh = BooleanCombine(a, b, BooleanOp::Union);
    const Mesh intersection_mesh = BooleanCombine(a, b, BooleanOp::Intersection);
    return BooleanCombine(union_mesh, intersection_mesh, BooleanOp::Difference);
  }

  const manifold::Manifold ma = ToManifold(a), mb = ToManifold(b);
  manifold::Manifold result = ma.Boolean(mb, ToManifoldOp(op));
  if (result.Status() != manifold::Manifold::Error::NoError) {
    // Fallback only, never the first attempt (see AdaptiveManifoldTolerance's
    // comment): near-tangent or near-coincident geometry can fail Manifold's
    // default-tolerance boolean outright, so retry once with a tolerance
    // scaled to the larger operand's own size before giving up.
    const double tol = std::max(AdaptiveManifoldTolerance(a.raw()), AdaptiveManifoldTolerance(b.raw()));
    if (tol > 0) result = ma.SetTolerance(tol).Boolean(mb.SetTolerance(tol), ToManifoldOp(op));
  }
  if (result.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::BooleanCombine: boolean operation failed "
        "(Manifold::Status() != NoError after Boolean())");
  }
  return FromManifold(result);
}

std::pair<Mesh, Mesh> SplitByPlane(const Mesh& mesh, Vector3d plane_normal, double plane_offset) {
  const manifold::vec3 n(plane_normal.x, plane_normal.y, plane_normal.z);
  const manifold::Manifold m = ToManifold(mesh);
  auto halves = m.SplitByPlane(n, plane_offset);
  if (halves.first.Status() != manifold::Manifold::Error::NoError ||
      halves.second.Status() != manifold::Manifold::Error::NoError) {
    const double tol = AdaptiveManifoldTolerance(mesh.raw());
    if (tol > 0) halves = m.SetTolerance(tol).SplitByPlane(n, plane_offset);
  }
  if (halves.first.Status() != manifold::Manifold::Error::NoError ||
      halves.second.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::SplitByPlane: split failed (Manifold::Status() != "
        "NoError after SplitByPlane())");
  }
  return {FromManifold(halves.first), FromManifold(halves.second)};
}

Mesh ConvexHull(const std::vector<Point3d>& points) {
  if (points.size() < 4) {
    throw std::invalid_argument(
        "dino8::kernel::ConvexHull: needs at least 4 points - fewer can't "
        "bound a nonzero 3D volume");
  }

  std::vector<manifold::vec3> manifold_points;
  manifold_points.reserve(points.size());
  for (const Point3d& p : points) {
    manifold_points.emplace_back(p.x, p.y, p.z);
  }

  const manifold::Manifold hull = manifold::Manifold::Hull(manifold_points);
  if (hull.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::ConvexHull: Manifold::Hull failed (Manifold::"
        "Status() != NoError) - e.g. every point coplanar, so no 3D hull "
        "exists");
  }
  return FromManifold(hull);
}

Mesh Simplify(const Mesh& mesh, double tolerance) {
  const manifold::Manifold simplified = ToManifold(mesh).Simplify(tolerance);
  if (simplified.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::Simplify: Manifold::Simplify failed (Manifold::"
        "Status() != NoError)");
  }
  return FromManifold(simplified);
}

Mesh MinkowskiSum(const Mesh& a, const Mesh& b) {
  const manifold::Manifold result = ToManifold(a).MinkowskiSum(ToManifold(b));
  if (result.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::MinkowskiSum: Manifold::MinkowskiSum failed "
        "(Manifold::Status() != NoError)");
  }
  return FromManifold(result);
}

Mesh MinkowskiDifference(const Mesh& a, const Mesh& b) {
  const manifold::Manifold result = ToManifold(a).MinkowskiDifference(ToManifold(b));
  if (result.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::MinkowskiDifference: Manifold::MinkowskiDifference "
        "failed (Manifold::Status() != NoError)");
  }
  return FromManifold(result);
}

std::vector<Mesh> Decompose(const Mesh& mesh) {
  const std::vector<manifold::Manifold> pieces = ToManifold(mesh).Decompose();
  std::vector<Mesh> result;
  result.reserve(pieces.size());
  for (const manifold::Manifold& piece : pieces) {
    if (piece.Status() != manifold::Manifold::Error::NoError) {
      throw std::runtime_error(
          "dino8::kernel::Decompose: Manifold::Decompose produced an "
          "invalid piece (Manifold::Status() != NoError)");
    }
    result.push_back(FromManifold(piece));
  }
  return result;
}

double MinGap(const Mesh& a, const Mesh& b, double search_length) {
  return ToManifold(a).MinGap(ToManifold(b), search_length);
}

Mesh RefineToLength(const Mesh& mesh, double length) {
  const manifold::Manifold refined = ToManifold(mesh).RefineToLength(length);
  if (refined.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::RefineToLength: Manifold::RefineToLength failed "
        "(Manifold::Status() != NoError)");
  }
  return FromManifold(refined);
}

Mesh SmoothAndRefine(const Mesh& mesh, double target_length, double min_sharp_angle,
                      double min_smoothness) {
  const manifold::Manifold smoothed =
      ToManifold(mesh).SmoothOut(min_sharp_angle, min_smoothness);
  if (smoothed.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::SmoothAndRefine: Manifold::SmoothOut failed "
        "(Manifold::Status() != NoError)");
  }
  const manifold::Manifold refined = smoothed.RefineToLength(target_length);
  if (refined.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::SmoothAndRefine: Manifold::RefineToLength failed "
        "(Manifold::Status() != NoError)");
  }
  return FromManifold(refined);
}

size_t CountDegenerateTriangles(const Mesh& mesh) { return ToManifold(mesh).NumDegenerateTris(); }

namespace {

constexpr double kConvexTol = 1e-9;

// A relative tolerance so this scales with the solid's own size instead
// of using one fixed epsilon on both a millimeter part and a
// kilometer-scale one.
double RelativeTol(const std::vector<Brep::PlanarFace>& faces) {
  double max_extent = 0.0;
  for (const Brep::PlanarFace& f : faces) {
    for (const Point3d& p : f.loop) {
      max_extent = std::max({max_extent, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
    }
  }
  return std::max(kConvexTol, max_extent * 1e-9);
}

// Every vertex of every face must lie on the inside (or exactly on) of
// every one of this solid's own half-spaces - the direct definition of
// convexity for a solid already given as a set of outward-facing planes.
bool IsConvex(const std::vector<Brep::PlanarFace>& faces, double tol) {
  for (const Brep::PlanarFace& plane_face : faces) {
    for (const Brep::PlanarFace& vertex_face : faces) {
      for (const Point3d& p : vertex_face.loop) {
        if (plane_face.plane.DistanceTo(p) > tol) return false;
      }
    }
  }
  return true;
}

// Sutherland-Hodgman half-space clip, run once but keeping BOTH children
// instead of only the "inside" one - what every non-convex boolean below
// is built from (see Requicha & Voelcker, "Boolean operations in solid
// modeling: Boundary evaluation and merging algorithms," Proc. IEEE 73(1),
// 1985, for the classical (unpatented) boundary-evaluation technique this
// implements: partitioning a face against every plane of the other solid
// until each surviving fragment lies wholly on one side of every such
// plane and can be classified with a single point-in-solid test).
//
// Valid for a CONCAVE `poly`, not just a convex one - see this function's
// own doc comment in dino8/kernel/detail/halfspace_clip3d.h. If the plane
// crosses a concave polygon's boundary more than twice, one side's output
// is a single vertex loop that revisits the cut line more than once (two
// or more regions joined by zero-net-area "bridge" edges lying exactly on
// the cut) rather than several separate loops - the same "keyhole" trick
// used to triangulate a polygon with a hole - whose signed area, and
// hence any ear-clip triangulation of it, still comes out exactly right.
//
// This one primitive is shared with fillet.cpp's FilletConvexEdge (see
// dino8/kernel/detail/halfspace_clip3d.h for the extracted, single copy);
// these are thin aliases so every call site below reads exactly as it did
// before the extraction.
using HalfspaceSplit = detail::HalfspaceSplit3d;

HalfspaceSplit SplitByHalfspace(const std::vector<Point3d>& poly, const ON_Plane& clip_plane, double tol) {
  return detail::SplitByHalfspace3d(poly, clip_plane, tol);
}

// Clips a convex 3D polygon (already known to lie in one plane) against
// one half-space, keeping the side the plane's own normal points away
// from (DistanceTo <= tol is "inside"). A thin wrapper so
// ClipByAllHalfspaces/BooleanIntersectConvexPlanar - both of which only
// ever want the "inside" child - need no change.
std::vector<Point3d> ClipByHalfspace(const std::vector<Point3d>& poly, const ON_Plane& clip_plane, double tol) {
  return detail::ClipByHalfspace3d(poly, clip_plane, tol);
}

// When a clip plane's boundary exactly coincides with an existing edge or
// vertex of the polygon being clipped (the common case for two
// axis-aligned or otherwise boundary-sharing solids, not a rare corner
// case), Sutherland-Hodgman's own "insert an intersection point on every
// crossing edge" step can emit a point that's a near-duplicate of one
// already in the polygon - a hairline zero-length or reflex "spike" a
// strict simple-polygon check correctly flags as self-intersecting, even
// though the region it bounds is geometrically fine. Collapsing
// consecutive near-duplicates (and any resulting collinear-through
// vertex) after every clip keeps the polygon genuinely simple without
// changing the region it encloses.
std::vector<Point3d> CleanPolygon(const std::vector<Point3d>& poly, double tol) {
  return detail::CleanPolygon3d(poly, tol);
}

std::vector<Point3d> ClipByAllHalfspaces(const std::vector<Point3d>& poly, const ON_Plane& poly_plane,
                                          const std::vector<Brep::PlanarFace>& other, double tol) {
  std::vector<ON_Plane> planes;
  planes.reserve(other.size());
  for (const Brep::PlanarFace& f : other) planes.push_back(f.plane);
  return ClipConvexPolygon(poly, poly_plane, planes, tol);
}

// ---------------------------------------------------------------------
// Non-convex planar boolean (BooleanCombinePlanar, below): split every
// face of A against every plane of B (and vice versa), classify each
// surviving fragment IN/OUT/ON the other solid, then reassemble the
// fragments the op calls for. Classical Requicha & Voelcker boundary
// evaluation (see SplitByHalfspace's own doc comment for the citation),
// generalized from BooleanIntersectConvexPlanar's convex-only clipping
// to solids of either shape.
// ---------------------------------------------------------------------

// Splits `loop` against EVERY plane of `other`, keeping both children of
// every cut instead of only the inside one: a worklist starts as `{loop}`
// and each plane of `other` in turn runs every polygon currently in the
// worklist through SplitByHalfspace, replacing it with whichever of its
// inside/outside children survive CleanPolygon with >= 3 vertices. After
// every plane has been applied, each surviving polygon lies entirely on
// one side of every plane of `other` - see ClassifyPointVsSolid's own
// comment for why that's exactly what makes single-point classification
// of each survivor valid, even when `loop` (or `other`) is non-convex.
std::vector<std::vector<Point3d>> SplitAgainstAllPlanes(std::vector<Point3d> loop,
                                                         const std::vector<Brep::PlanarFace>& other, double tol) {
  std::vector<std::vector<Point3d>> worklist;
  worklist.push_back(std::move(loop));
  for (const Brep::PlanarFace& f : other) {
    std::vector<std::vector<Point3d>> next;
    next.reserve(worklist.size() * 2);
    for (const std::vector<Point3d>& poly : worklist) {
      const HalfspaceSplit split = SplitByHalfspace(poly, f.plane, tol);
      std::vector<Point3d> inside = CleanPolygon(split.inside, tol);
      std::vector<Point3d> outside = CleanPolygon(split.outside, tol);
      if (inside.size() >= 3) next.push_back(std::move(inside));
      if (outside.size() >= 3) next.push_back(std::move(outside));
    }
    worklist = std::move(next);
  }
  return worklist;
}

// Projects a 3D point already known to lie in `plane` onto the plane's
// own (x, y) axes - the same local 2D coordinate system Brep::
// FromPlanarFaces already projects a face's loop into (see its own `d *
// pl.xaxis, d * pl.yaxis`), so a loop and a query point end up in a
// mutually consistent 2D frame no matter which arbitrary in-plane
// rotation `plane`'s constructor happened to pick.
Point2d ProjectOntoPlaneAxes(const ON_Plane& plane, const Point3d& p) {
  const ON_3dVector d = p - plane.origin;
  return Point2d(d * plane.xaxis, d * plane.yaxis);
}

std::vector<Point2d> ProjectLoopOntoPlaneAxes(const ON_Plane& plane, const std::vector<Point3d>& loop) {
  std::vector<Point2d> out;
  out.reserve(loop.size());
  for (const Point3d& v : loop) out.push_back(ProjectOntoPlaneAxes(plane, v));
  return out;
}

// Standard even-odd ray-casting point-in-polygon test in 2D (the same
// algorithm as surface.cpp's own file-local PointInPolygon, duplicated
// here rather than shared across translation units for a two-line
// function - boundary behavior is deliberately not relied upon by any
// caller here; see DistanceToPolygonBoundary2D for the boundary case
// this module actually needs to detect).
bool PointInPolygon2D(double x, double y, const std::vector<Point2d>& polygon) {
  bool inside = false;
  const size_t n = polygon.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const Point2d& pi = polygon[i];
    const Point2d& pj = polygon[j];
    const bool crosses = (pi.y > y) != (pj.y > y);
    if (crosses) {
      const double x_at_crossing = (pj.x - pi.x) * (y - pi.y) / (pj.y - pi.y) + pi.x;
      if (x < x_at_crossing) inside = !inside;
    }
  }
  return inside;
}

double DistancePointToSegment2D(double px, double py, double ax, double ay, double bx, double by) {
  const double vx = bx - ax, vy = by - ay;
  const double wx = px - ax, wy = py - ay;
  const double len2 = vx * vx + vy * vy;
  double t = (len2 > 1e-30) ? (wx * vx + wy * vy) / len2 : 0.0;
  t = std::max(0.0, std::min(1.0, t));
  const double cx = ax + t * vx, cy = ay + t * vy;
  return std::hypot(px - cx, py - cy);
}

// Minimum distance from (x, y) to any edge of `polygon` - used to detect
// a ray-cast hit that grazes an edge or vertex (within `tol` of the
// boundary), which ClassifyPointVsSolid's ray caster can't parity-count
// reliably and must instead treat as a reason to abandon that direction.
double DistanceToPolygonBoundary2D(double x, double y, const std::vector<Point2d>& polygon) {
  double best = std::numeric_limits<double>::infinity();
  const size_t n = polygon.size();
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a = polygon[i];
    const Point2d& b = polygon[(i + 1) % n];
    best = std::min(best, DistancePointToSegment2D(x, y, a.x, a.y, b.x, b.y));
  }
  return best;
}

enum class PointClass { kIn, kOut, kOn };

// A fixed list of non-axis-aligned "generic" unit directions to ray-cast
// along - irrational-slope-ish (built from the golden ratio) so a
// direction is vanishingly unlikely to be exactly parallel to any input
// plane or to pass exactly through a vertex/edge of an unrelated face,
// the two situations ClassifyPointVsSolid's ray caster has to detect and
// route around rather than silently mis-parity-count. Up to 8 attempts,
// per this module's own doc comment.
std::vector<Vector3d> GenericRayDirections() {
  const double phi = 1.6180339887498948482;
  const double phi2 = phi * phi;
  std::vector<Vector3d> dirs = {
      Vector3d(1.0, phi, phi2),      Vector3d(phi, phi2, 1.0),   Vector3d(phi2, 1.0, phi),
      Vector3d(1.0, -phi, phi2),     Vector3d(-phi, phi2, 1.0),  Vector3d(phi2, -1.0, -phi),
      Vector3d(-1.0, phi, -phi2),    Vector3d(phi, -phi2, 1.0),
  };
  for (Vector3d& d : dirs) {
    const double len = d.Length();
    if (len > 1e-12) d = d / len;
  }
  return dirs;
}

// Point-in-polyhedron classification for a single point against a closed
// planar-faced solid (`faces`), per Requicha & Voelcker-style boundary
// evaluation (see SplitByHalfspace's own doc comment for the citation).
//
// This is called once per fragment surviving SplitAgainstAllPlanes, with
// that fragment's own representative interior point - valid (not just for
// a convex `faces`) because splitting a face against EVERY plane of the
// other solid, not just its finite faces, means a surviving fragment's
// open interior can never cross any of those planes; since the other
// solid's actual boundary is entirely made of finite pieces OF those same
// planes, the fragment's interior can never cross the actual boundary
// either, so every point in it shares one true classification.
//
// First checks ON: coincidence with one of `faces`' own planes, within
// `tol`, AND the point's projection landing inside that face's own loop -
// the same role `same_plane` plays in BooleanIntersectConvexPlanar,
// collapsing a coincident face to a single copy rather than double-
// counting it as if it were floating just inside or outside the solid.
//
// Otherwise ray-casts along GenericRayDirections() until one direction
// resolves cleanly (no face grazed edge-on or parallel-and-coincident),
// and returns IN/OUT by the parity of crossings. Exhausting every
// direction without a clean pass - only possible on adversarial input,
// since the directions are generic and non-parallel to any real input
// plane - falls back to the sign of the distance to the nearest face.
PointClass ClassifyPointVsSolid(const Point3d& p, const std::vector<Brep::PlanarFace>& faces, double tol) {
  for (const Brep::PlanarFace& f : faces) {
    if (std::fabs(f.plane.DistanceTo(p)) <= tol) {
      const std::vector<Point2d> loop2d = ProjectLoopOntoPlaneAxes(f.plane, f.loop);
      const Point2d p2d = ProjectOntoPlaneAxes(f.plane, p);
      if (PointInPolygon2D(p2d.x, p2d.y, loop2d)) return PointClass::kOn;
    }
  }

  for (const Vector3d& d : GenericRayDirections()) {
    bool clean = true;
    int crossings = 0;
    for (const Brep::PlanarFace& f : faces) {
      const double denom = f.plane.zaxis * d;
      if (std::fabs(denom) < 1e-9) {
        // The ray runs parallel to this face's own plane. If p is also IN
        // that plane, the ray can't cross this face cleanly at all -
        // abandon this direction rather than guess. Otherwise the ray
        // simply never meets this face's (infinite) plane - not a
        // degenerate case, just skip it.
        if (std::fabs(f.plane.DistanceTo(p)) <= tol) { clean = false; break; }
        continue;
      }
      const double t = ((f.plane.origin - p) * f.plane.zaxis) / denom;
      if (t <= tol) continue;  // behind (or at) the ray's own origin
      const Point3d hit = p + t * d;
      const std::vector<Point2d> loop2d = ProjectLoopOntoPlaneAxes(f.plane, f.loop);
      const Point2d hit2d = ProjectOntoPlaneAxes(f.plane, hit);
      if (DistanceToPolygonBoundary2D(hit2d.x, hit2d.y, loop2d) <= tol) {
        // Grazes an edge or vertex - can't be parity-counted reliably.
        clean = false;
        break;
      }
      if (PointInPolygon2D(hit2d.x, hit2d.y, loop2d)) ++crossings;
    }
    if (clean) return (crossings % 2 == 1) ? PointClass::kIn : PointClass::kOut;
  }

  // Fallback: sign of the distance to the nearest face. DistanceTo > 0
  // means p is on the side the face's own outward normal points toward -
  // i.e. outside that face's half-space - matching every other half-space
  // convention in this file (see ClipByHalfspace's own `dc <= tol` test).
  double best_abs = std::numeric_limits<double>::infinity();
  double best_signed = 0.0;
  for (const Brep::PlanarFace& f : faces) {
    const double dist = f.plane.DistanceTo(p);
    if (std::fabs(dist) < best_abs) {
      best_abs = std::fabs(dist);
      best_signed = dist;
    }
  }
  return (best_signed > 0.0) ? PointClass::kOut : PointClass::kIn;
}

// A point guaranteed to lie in `face.loop`'s own interior (not just its
// vertex or area-weighted average, either of which can fall outside a
// concave or "keyhole"-bridged polygon - see SplitByHalfspace's own
// comment on why a survivor can be bridged): ear-clip triangulate the
// loop in its own local 2D axes (dino8::kernel::detail::
// EarClipTriangulate already handles concave polygons robustly - it's
// what this codebase's own loft end caps use) and take the centroid of
// whichever triangle it finds first, which by construction is strictly
// inside the polygon.
Point3d RepresentativeInteriorPoint(const Brep::PlanarFace& face) {
  const std::vector<Point2d> loop2d = ProjectLoopOntoPlaneAxes(face.plane, face.loop);
  const std::vector<std::array<int, 3>> tris = dino8::kernel::detail::EarClipTriangulate(loop2d);
  if (!tris.empty()) {
    const std::array<int, 3>& t = tris.front();
    const Point2d c2d((loop2d[t[0]].x + loop2d[t[1]].x + loop2d[t[2]].x) / 3.0,
                       (loop2d[t[0]].y + loop2d[t[1]].y + loop2d[t[2]].y) / 3.0);
    return face.plane.origin + c2d.x * face.plane.xaxis + c2d.y * face.plane.yaxis;
  }
  // Ear-clipping only fails to find any triangle on a badly degenerate
  // loop (e.g. near-zero area) - fall back to a plain vertex average
  // rather than crash; a degenerate sliver's exact interior point matters
  // far less than not throwing on it.
  Point3d sum(0.0, 0.0, 0.0);
  for (const Point3d& v : face.loop) sum = sum + v;
  return sum / static_cast<double>(face.loop.size());
}

// One face fragment plus its classification against the OTHER solid.
struct ClassifiedFace {
  Brep::PlanarFace face;
  PointClass cls;
};

// Splits every face of `self_faces` against every plane of
// `other_faces`, then classifies each surviving fragment against
// `other_faces` via its own representative interior point.
std::vector<ClassifiedFace> SplitAndClassify(const std::vector<Brep::PlanarFace>& self_faces,
                                              const std::vector<Brep::PlanarFace>& other_faces, double tol) {
  std::vector<ClassifiedFace> result;
  for (const Brep::PlanarFace& f : self_faces) {
    for (std::vector<Point3d>& piece : SplitAgainstAllPlanes(f.loop, other_faces, tol)) {
      Brep::PlanarFace fragment;
      fragment.plane = f.plane;
      fragment.loop = std::move(piece);
      const PointClass cls = ClassifyPointVsSolid(RepresentativeInteriorPoint(fragment), other_faces, tol);
      result.push_back({std::move(fragment), cls});
    }
  }
  return result;
}

struct ClassifiedBuckets {
  std::vector<Brep::PlanarFace> in, out, on;
};

ClassifiedBuckets SplitAndBucket(const std::vector<Brep::PlanarFace>& self_faces,
                                  const std::vector<Brep::PlanarFace>& other_faces, double tol) {
  ClassifiedBuckets buckets;
  for (ClassifiedFace& cf : SplitAndClassify(self_faces, other_faces, tol)) {
    switch (cf.cls) {
      case PointClass::kIn:
        buckets.in.push_back(std::move(cf.face));
        break;
      case PointClass::kOut:
        buckets.out.push_back(std::move(cf.face));
        break;
      case PointClass::kOn:
        buckets.on.push_back(std::move(cf.face));
        break;
    }
  }
  return buckets;
}

// Flips a face so its outward normal (and winding) point the opposite
// way - what a B face bounding material A is losing becomes, in A - B,
// a face bounding material into the new cavity. ON_Plane::Flip() swaps
// the plane's x/y axes, reverses its z axis, and updates its cached
// plane equation, so DistanceTo/zaxis stay self-consistent afterward;
// reversing the loop's own vertex order is what keeps "CCW as seen from
// outside" true of the new outward normal, independent of which in-plane
// (x, y) axes Flip() happened to pick.
Brep::PlanarFace FlipFace(Brep::PlanarFace f) {
  f.plane.Flip();
  std::reverse(f.loop.begin(), f.loop.end());
  return f;
}

}  // namespace

// Shared with ShellConvexPlanar (see boolean.h) - this is the same
// clip-a-polygon-against-a-list-of-planes loop BooleanIntersectConvexPlanar
// has always run per face, now factored out so both operations run one
// verified clipper instead of two copies of the same algorithm. Behavior
// for existing callers here is unchanged: `tol` is always passed explicitly
// (never the default), so this extraction doesn't alter
// BooleanIntersectConvexPlanar's own already-verified precision.
std::vector<Point3d> ClipConvexPolygon(const std::vector<Point3d>& poly, const ON_Plane& poly_plane,
                                        const std::vector<ON_Plane>& halfspaces, double tol) {
  if (tol < 0.0) {
    double max_extent = std::max({std::fabs(poly_plane.origin.x), std::fabs(poly_plane.origin.y),
                                   std::fabs(poly_plane.origin.z)});
    for (const Point3d& p : poly) {
      max_extent = std::max({max_extent, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
    }
    tol = std::max(kConvexTol, max_extent * 1e-9);
  }
  std::vector<Point3d> out = poly;
  for (const ON_Plane& hs : halfspaces) {
    out = CleanPolygon(ClipByHalfspace(out, hs, tol), tol);
    if (out.size() < 3) return {};
  }
  return out;
}

Brep BooleanIntersectConvexPlanar(const Brep& a, const Brep& b) {
  const std::vector<Brep::PlanarFace> fa = a.PlanarFaces();
  const std::vector<Brep::PlanarFace> fb = b.PlanarFaces();
  const double tol = std::max(RelativeTol(fa), RelativeTol(fb));
  if (!IsConvex(fa, tol) || !IsConvex(fb, tol)) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanIntersectConvexPlanar: both solids must be convex "
        "(a vertex of one lies outside one of its own faces' half-spaces) - see "
        "this function's own doc comment for why non-convex inputs aren't handled here");
  }
  // When A and B share an exact coincident boundary plane (e.g. two
  // prisms of the same height, both with a top face at the same z), that
  // shared plane's clip result is identical whichever solid it's clipped
  // relative to - so clipping BOTH A's copy of it against B AND B's copy
  // against A produces the same polygon twice. Two coincident faces on
  // one plane isn't a valid closed B-rep (a real boundary has exactly one
  // face there), so keep only the first one found on any given plane.
  auto same_plane = [tol](const ON_Plane& p, const ON_Plane& q) {
    return std::fabs(p.DistanceTo(q.origin)) <= tol && p.zaxis.IsParallelTo(q.zaxis, 1e-6) == 1;
  };
  std::vector<Brep::PlanarFace> result;
  auto add_clipped = [&](const std::vector<Brep::PlanarFace>& faces, const std::vector<Brep::PlanarFace>& clip_against) {
    for (const Brep::PlanarFace& face : faces) {
      bool already_have = false;
      for (const Brep::PlanarFace& existing : result) {
        if (same_plane(face.plane, existing.plane)) { already_have = true; break; }
      }
      if (already_have) continue;
      Brep::PlanarFace clipped;
      clipped.plane = face.plane;
      clipped.loop = ClipByAllHalfspaces(face.loop, face.plane, clip_against, tol);
      if (clipped.loop.size() >= 3) result.push_back(std::move(clipped));
    }
  };
  add_clipped(fa, fb);
  add_clipped(fb, fa);
  return Brep::FromPlanarFaces(result);
}

Brep BooleanCombinePlanar(const Brep& a, const Brep& b, BooleanOp op) {
  const std::vector<Brep::PlanarFace> fa = a.PlanarFaces();
  const std::vector<Brep::PlanarFace> fb = b.PlanarFaces();
  const double tol = std::max(RelativeTol(fa), RelativeTol(fb));

  if (op == BooleanOp::SymmetricDifference) {
    // No direct XOR primitive here either (see BooleanCombine's own
    // comment on the mesh-boolean side of this same enum) - composed from
    // the three ops this function does implement, exactly like
    // BooleanCombine does for meshes.
    const Brep union_brep = BooleanCombinePlanar(a, b, BooleanOp::Union);
    const Brep intersection_brep = BooleanCombinePlanar(a, b, BooleanOp::Intersection);
    return BooleanCombinePlanar(union_brep, intersection_brep, BooleanOp::Difference);
  }

  // Split every face of A against every plane of B, classify each
  // survivor against B; then the same the other way around.
  const ClassifiedBuckets from_a = SplitAndBucket(fa, fb, tol);
  const ClassifiedBuckets from_b = SplitAndBucket(fb, fa, tol);

  // A coincident A_on/B_on pair (same plane, same finite extent) is one
  // physical face counted twice - the same role `same_plane` plays in
  // BooleanIntersectConvexPlanar's own dedup, generalized here to also
  // recognize the OPPOSED-normal case Difference needs (two solids
  // touching face-to-face but filling opposite sides of that face).
  auto same_plane = [tol](const ON_Plane& p, const ON_Plane& q) {
    return std::fabs(p.DistanceTo(q.origin)) <= tol && p.zaxis.IsParallelTo(q.zaxis, 1e-6) == 1;
  };

  std::vector<Brep::PlanarFace> result;
  switch (op) {
    case BooleanOp::Union:
      // A_out u B_out u A_on: B's own coincident copy of a shared
      // boundary face is a duplicate of A's (same_plane, by construction
      // of ON classification), so it's never separately added.
      for (const Brep::PlanarFace& f : from_a.out) result.push_back(f);
      for (const Brep::PlanarFace& f : from_b.out) result.push_back(f);
      for (const Brep::PlanarFace& f : from_a.on) result.push_back(f);
      break;
    case BooleanOp::Intersection:
      // A_in u B_in u A_on - with convex A, B every face is then wholly
      // inside or outside every plane, so A_in here is exactly what
      // ClipByAllHalfspaces already returns: this reduces to
      // BooleanIntersectConvexPlanar's own result on convex inputs.
      for (const Brep::PlanarFace& f : from_a.in) result.push_back(f);
      for (const Brep::PlanarFace& f : from_b.in) result.push_back(f);
      for (const Brep::PlanarFace& f : from_a.on) result.push_back(f);
      break;
    case BooleanOp::Difference:
      // A - B: keep the part of A outside B, plus the part of B inside A
      // flipped to bound the new cavity from the other side.
      for (const Brep::PlanarFace& f : from_a.out) result.push_back(f);
      for (const Brep::PlanarFace& f : from_b.in) result.push_back(FlipFace(f));
      // A coincident A_on/B_on pair: outward normals agreeing means the
      // two solids have material on the SAME side of that shared face
      // (touching flush, e.g. two prisms sharing a base) - subtracting B
      // removes that material too, so the pair cancels and neither copy
      // belongs in A - B. Normals opposed means B's face bounds material
      // A is losing from the OTHER side (B sits on the far side of A's
      // own boundary) - A's copy is still a real boundary of A - B and is
      // kept unchanged; so is any A_on face with no B_on counterpart at
      // all (nothing to cancel or reorient it against).
      for (const Brep::PlanarFace& a_on : from_a.on) {
        bool cancelled = false;
        for (const Brep::PlanarFace& b_on : from_b.on) {
          if (same_plane(a_on.plane, b_on.plane)) {
            cancelled = true;
            break;
          }
        }
        if (!cancelled) result.push_back(a_on);
      }
      break;
    default:
      throw std::invalid_argument("dino8::kernel::BooleanCombinePlanar: unknown BooleanOp");
  }
  return Brep::FromPlanarFaces(result);
}

namespace {
// Signed area of a planar polygon (known to already lie in one plane,
// with unit `normal`), via fan triangulation from the polygon's own
// first vertex - the standard formula for a polygon given as an ordered
// vertex loop in a known plane (Preparata & Shamos, "Computational
// Geometry", the same textbook already cited for the half-space clipper
// above). Every polygon ShellConvexPlanar measures here is convex (a
// clip of a convex polygon against half-spaces), so the fan from vertex
// 0 never leaves it.
double PlanarPolygonArea(const std::vector<Point3d>& poly, const Vector3d& normal) {
  if (poly.size() < 3) return 0.0;
  const Point3d& origin = poly[0];
  Vector3d sum(0, 0, 0);
  for (size_t i = 1; i + 1 < poly.size(); ++i) {
    sum += ON_CrossProduct(poly[i] - origin, poly[i + 1] - origin);
  }
  return 0.5 * std::fabs(sum * normal);
}

}  // namespace

Brep ShellConvexPlanar(const Brep& solid, const std::vector<int>& removed_faces, double t) {

  if (!(t > 0.0)) {
    throw std::invalid_argument("dino8::kernel::ShellConvexPlanar: t must be positive");
  }

  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const int n = static_cast<int>(faces.size());
  const double tol = RelativeTol(faces);
  if (!IsConvex(faces, tol)) {
    throw std::invalid_argument(
        "dino8::kernel::ShellConvexPlanar: solid must be convex (a vertex of "
        "one of its own faces lies outside one of its own other faces' "
        "half-spaces) - see BooleanIntersectConvexPlanar's own doc comment "
        "for why non-convex input isn't handled here");
  }

  std::vector<bool> is_removed(static_cast<size_t>(n), false);
  for (int idx : removed_faces) {
    if (idx < 0 || idx >= n) {
      throw std::invalid_argument(
          "dino8::kernel::ShellConvexPlanar: removed_faces contains an index "
          "out of range for solid.PlanarFaces()");
    }
    is_removed[static_cast<size_t>(idx)] = true;
  }

  // Scope limit: two removed faces sharing an edge would need a
  // non-planar, multi-facet rim to close the combined opening - genuinely
  // out of scope here (see this function's own doc comment), so refuse
  // rather than emit a wrong single-plane rim for either one.
  auto shares_edge = [tol](const std::vector<Point3d>& a, const std::vector<Point3d>& b) {
    for (size_t i = 0; i < a.size(); ++i) {
      const Point3d& a0 = a[i];
      const Point3d& a1 = a[(i + 1) % a.size()];
      for (size_t j = 0; j < b.size(); ++j) {
        if (a0.DistanceTo(b[(j + 1) % b.size()]) <= tol && a1.DistanceTo(b[j]) <= tol) return true;
      }
    }
    return false;
  };
  for (size_t i = 0; i < removed_faces.size(); ++i) {
    for (size_t j = i + 1; j < removed_faces.size(); ++j) {
      if (shares_edge(faces[static_cast<size_t>(removed_faces[i])].loop,
                       faces[static_cast<size_t>(removed_faces[j])].loop)) {
        throw std::invalid_argument(
            "dino8::kernel::ShellConvexPlanar: two entries of removed_faces "
            "are mutually adjacent - an opening spanning more than one "
            "original face needs a non-planar, multi-facet rim, out of "
            "scope here (see this function's own doc comment)");
      }
    }
  }

  // (1) Constraint planes: kept -> that face's own plane offset inward by
  // t (translated by -t*n_i); removed -> unchanged (a removed face
  // contributes an opening, so nothing is offset there, and its plane
  // never moves on either side of the cut - see the rim derivation
  // below).
  std::vector<ON_Plane> pi(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    if (is_removed[static_cast<size_t>(i)]) {
      pi[static_cast<size_t>(i)] = faces[static_cast<size_t>(i)].plane;
      continue;
    }
    ON_Plane offset = faces[static_cast<size_t>(i)].plane;
    offset.origin = offset.origin - t * offset.zaxis;
    offset.UpdateEquation();
    pi[static_cast<size_t>(i)] = offset;
  }

  // Every kept face's inner (cavity-side) loop, computed - and checked
  // for degeneracy - for ALL kept faces before any output face is built,
  // so a t that's too large anywhere aborts the whole operation instead
  // of emitting a partially-shelled Brep.
  std::vector<std::vector<Point3d>> inner(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    if (is_removed[static_cast<size_t>(i)]) continue;
    const Brep::PlanarFace& face = faces[static_cast<size_t>(i)];
    const Vector3d n_i = face.plane.zaxis;
    std::vector<Point3d> translated;
    translated.reserve(face.loop.size());
    for (const Point3d& p : face.loop) translated.push_back(p - t * n_i);

    std::vector<ON_Plane> others;
    others.reserve(static_cast<size_t>(n - 1));
    for (int k = 0; k < n; ++k) {
      if (k == i) continue;
      others.push_back(pi[static_cast<size_t>(k)]);
    }
    std::vector<Point3d> clipped = ClipConvexPolygon(translated, pi[static_cast<size_t>(i)], others, tol);
    const double area = PlanarPolygonArea(clipped, n_i);
    // Zero-area threshold scaled the same way RelativeTol scales length:
    // a genuine sliver at this solid's own size, not one fixed epsilon.
    const double area_tol = tol * tol;
    if (clipped.size() < 3 || area <= area_tol) {
      throw std::invalid_argument(
          "dino8::kernel::ShellConvexPlanar: wall thickness t is too large "
          "for face " + std::to_string(i) + " - its inner offset collapses "
          "to fewer than 3 vertices or ~0 area (t at or beyond that face's "
          "own local offset feasibility, up to the solid's inradius)");
    }
    inner[static_cast<size_t>(i)] = std::move(clipped);
  }

  std::vector<Brep::PlanarFace> result;

  // (1)+(2) Kept faces: outer copy unchanged, inner copy at the offset
  // plane with reversed winding/flipped normal - the material lies
  // between the two, so the inner surface's outward-from-material normal
  // is -n_i, the opposite of the exterior copy's own outward normal (see
  // this function's header comment for the sign argument).
  for (int i = 0; i < n; ++i) {
    if (is_removed[static_cast<size_t>(i)]) continue;
    const Brep::PlanarFace& face = faces[static_cast<size_t>(i)];
    result.push_back(face);  // exterior wall, unmodified

    std::vector<Point3d> reversed(inner[static_cast<size_t>(i)].rbegin(), inner[static_cast<size_t>(i)].rend());
    Brep::PlanarFace inner_face;
    inner_face.plane = ON_Plane(reversed[0], -face.plane.zaxis);
    inner_face.loop = std::move(reversed);
    result.push_back(std::move(inner_face));
  }

  // (3) Rim/washer around each opening: outer edge = the removed face's
  // own original loop, inner edge = that same loop clipped against every
  // OTHER face's constraint plane. Both lie in the removed face's own
  // (unmoved) plane - it's the one plane on either side of the cut that
  // never gets offset - so the whole rim is flat, split here into one
  // flat quad per edge, equivalent to a single TrimmedPlanarFace washer
  // (outer=loop_j, hole=rim_j) but directly expressible as PlanarFaces
  // through the existing FromPlanarFaces() path.
  //
  // Sutherland-Hodgman clipping preserves the CYCLIC order of surviving
  // vertices but not their absolute list index - rim_j[0] is not in
  // general the inset of loop_j[0] (clipping against several planes in
  // sequence can rotate which surviving/inserted vertex ends up first).
  // So outer edge k is paired with its rim edge by IDENTITY (which kept
  // face bounds it), not by index: edge k of loop_j is shared with
  // exactly one other face adj[k] (found by matching it, reversed,
  // against every other face's own loop); for t > 0 that whole edge lies
  // entirely outside pi[adj[k]] (every point on it sits at distance
  // exactly t from that plane, since it lay at distance 0 on adj[k]'s
  // own original, un-offset plane), so it is replaced wholesale by a new
  // rim edge lying exactly on pi[adj[k]] - found by checking which rim
  // edge's own midpoint lies on that plane.
  for (int j = 0; j < n; ++j) {
    if (!is_removed[static_cast<size_t>(j)]) continue;
    const Brep::PlanarFace& face_j = faces[static_cast<size_t>(j)];
    std::vector<ON_Plane> others;
    others.reserve(static_cast<size_t>(n - 1));
    for (int k = 0; k < n; ++k) {
      if (k == j) continue;
      others.push_back(pi[static_cast<size_t>(k)]);
    }
    const std::vector<Point3d>& loop_j = face_j.loop;
    std::vector<Point3d> rim_j = ClipConvexPolygon(loop_j, pi[static_cast<size_t>(j)], others, tol);

    if (rim_j.size() < 3) {
      throw std::invalid_argument(
          "dino8::kernel::ShellConvexPlanar: wall thickness t collapses "
          "opening " + std::to_string(j) + "'s rim entirely (t at or beyond "
          "the opening's own local offset feasibility)");
    }

    const size_t m = loop_j.size();
    // adj[k]: the other face sharing loop_j's edge (k, k+1) - found by a
    // reversed-edge match against every other face's own loop (two
    // adjacent faces of a solid always traverse a shared edge in
    // opposite directions).
    std::vector<int> adj(m, -1);
    for (size_t k = 0; k < m; ++k) {
      const Point3d& e0 = loop_j[k];
      const Point3d& e1 = loop_j[(k + 1) % m];
      for (int p = 0; p < n && adj[k] < 0; ++p) {
        if (p == j) continue;
        const std::vector<Point3d>& lp = faces[static_cast<size_t>(p)].loop;
        for (size_t q = 0; q < lp.size(); ++q) {
          if (e0.DistanceTo(lp[(q + 1) % lp.size()]) <= tol && e1.DistanceTo(lp[q]) <= tol) {
            adj[k] = p;
            break;
          }
        }
      }
      if (adj[k] < 0) {
        throw std::invalid_argument(
            "dino8::kernel::ShellConvexPlanar: opening " + std::to_string(j) +
            "'s own loop has an edge shared with no other face - not a valid "
            "closed solid boundary");
      }
    }

    // rim_edge_for_face[p]: the rim edge (start index) whose own midpoint
    // lies on face p's constraint plane pi[p].
    const size_t rim_n = rim_j.size();
    std::vector<int> rim_edge_for_face(static_cast<size_t>(n), -1);
    for (size_t rm = 0; rm < rim_n; ++rm) {
      const Point3d mid = rim_j[rm] + 0.5 * (rim_j[(rm + 1) % rim_n] - rim_j[rm]);
      for (int p = 0; p < n; ++p) {
        if (p == j) continue;
        if (std::fabs(pi[static_cast<size_t>(p)].DistanceTo(mid)) <= tol) {
          rim_edge_for_face[static_cast<size_t>(p)] = static_cast<int>(rm);
          break;
        }
      }
    }

    for (size_t k = 0; k < m; ++k) {
      const int owner = adj[k];
      const int rm = rim_edge_for_face[static_cast<size_t>(owner)];
      if (rm < 0) {
        throw std::invalid_argument(
            "dino8::kernel::ShellConvexPlanar: wall thickness t collapses "
            "the rim edge of opening " + std::to_string(j) + " adjacent to "
            "face " + std::to_string(owner) +
            " - out of scope here, see this function's own doc comment");
      }
      Brep::PlanarFace quad;
      quad.plane = face_j.plane;
      quad.loop = {loop_j[k], loop_j[(k + 1) % m], rim_j[(static_cast<size_t>(rm) + 1) % rim_n],
                   rim_j[static_cast<size_t>(rm)]};
      result.push_back(std::move(quad));
    }
  }

  return Brep::FromPlanarFaces(result);
}

// ---------------------------------------------------------------------
// BooleanCombineMixed: the axis-perpendicular-only extension of the
// non-convex planar pipeline above to a solid that may have a
// CYLINDRICAL face - see boolean.h's own doc comment for the exact,
// deliberately narrow scope. Every helper below is NEW and independent of
// the planar-only helpers above (SplitAgainstAllPlanes, ClassifyPointVsSolid,
// SplitAndBucket, ...) wherever the two would otherwise diverge; where the
// underlying primitive is identical (a plane-vs-plane half-space split,
// the interior-point/ear-clip trick, FlipFace) this reuses that exact
// same helper rather than a second copy of it, so BooleanCombinePlanar's
// own already-verified behavior is provably untouched.
// ---------------------------------------------------------------------

namespace {

// The sum-type currency this pipeline operates on in place of
// vector<Brep::PlanarFace> - a plain tagged struct (not std::variant)
// since every consumer below already needs to branch on face kind
// explicitly, and a tag+two-members struct is copy/move-friendly and
// simple to build without visitor boilerplate.
struct MixedFace {
  bool is_cyl = false;
  Brep::PlanarFace planar;
  Brep::CylindricalFace cyl;
};

std::vector<MixedFace> ToMixed(const Brep::MixedFacesResult& mf) {
  std::vector<MixedFace> out;
  out.reserve(mf.planar.size() + mf.cylindrical.size());
  for (const Brep::PlanarFace& p : mf.planar) {
    MixedFace m;
    m.planar = p;
    out.push_back(std::move(m));
  }
  for (const Brep::CylindricalFace& c : mf.cylindrical) {
    MixedFace m;
    m.is_cyl = true;
    m.cyl = c;
    out.push_back(std::move(m));
  }
  return out;
}

// A relative tolerance in the same spirit as RelativeTol() above,
// generalized to also cover a CylindricalFace's own extent (its axis
// endpoints, expanded by `radius` in every axis - a cheap, deliberately
// loose bound, not a tight bounding box; this only ever feeds a tolerance
// scale, not a geometric result).
double RelativeTolMixed(const std::vector<MixedFace>& faces) {
  double max_extent = 0.0;
  for (const MixedFace& f : faces) {
    if (!f.is_cyl) {
      for (const Point3d& p : f.planar.loop) {
        max_extent = std::max({max_extent, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
      }
      continue;
    }
    const Brep::CylindricalFace& cf = f.cyl;
    const Point3d ends[2] = {cf.frame.origin, cf.frame.origin + cf.length * cf.frame.zaxis};
    for (const Point3d& e : ends) {
      max_extent = std::max({max_extent, std::fabs(e.x) + cf.radius, std::fabs(e.y) + cf.radius,
                              std::fabs(e.z) + cf.radius});
    }
  }
  return std::max(kConvexTol, max_extent * 1e-9);
}

// How closely `dir` has to align with `normal` (or vice versa) to count
// as the "perpendicular axis" case this increment's own splitting/no-
// interaction logic branches on, rather than the oblique case it refuses.
constexpr double kAxisAlignTol = 1e-6;

// How small |dot(cylinder axis, plane normal)| can get before the oblique
// plane+cylinder ellipse (case (ii)/(iii) below) is refused as a genuine
// geometric degeneracy rather than silently divided through - see
// detail::ComputeEllipseFrame3d's own doc comment (ellipse_clip3d.h) for
// why: the ellipse's own semi-major axis (radius/|C|) is unboundedly large
// as C -> 0 (grazing/near-axis-parallel incidence).
constexpr double kMinObliqueC = 1e-6;

// A point ON a CylindricalFace's own lateral surface, at true angle
// `angle` (radians from `cf.frame.xaxis`) and true axial height `height`
// (distance from `cf.frame.origin` along `cf.frame.zaxis`) - the same
// parameterization FromMixedFaces()/MixedFaces() already use, just
// evaluated directly instead of through a NURBS surface.
Point3d PointOnCylFace(const Brep::CylindricalFace& cf, double angle, double height) {
  return cf.frame.origin + height * cf.frame.zaxis +
         cf.radius * (std::cos(angle) * cf.frame.xaxis + std::sin(angle) * cf.frame.yaxis);
}

// A point in a SIMPLE 2D polygon's own interior (convex or concave), via
// the standard "scan a horizontal line just above the polygon's own
// lowest vertex" construction: that line crosses the polygon's boundary
// an EVEN number of times (even-odd rule), and the interval immediately
// next to the lowest vertex is always genuinely interior material for
// ANY simple polygon - a real, general-purpose technique (see e.g. any
// standard computational-geometry text's treatment of the even-odd
// rule).
//
// Deliberately NOT the existing, shared RepresentativeInteriorPoint()
// (used unchanged elsewhere in this file, including by
// BooleanCombinePlanar): that helper's ear-clip triangulation only checks
// that a candidate ear contains no OTHER POLYGON VERTEX - which can't
// detect an ear whose own interior instead cuts straight across a
// VERTEX-FREE gap. Confirmed directly during development, not a
// theoretical worry: an earlier "keyhole"-bridged representation of a
// hole-punched cap polygon (see ClipPolygonByCircle3d's own doc comment
// for why that representation was abandoned) made EarClipTriangulate's
// first "ear" land with its centroid essentially AT the removed
// material's own center - which would have silently misclassified that
// fragment as inside the drilled cylinder instead of outside it. This
// function is a from-scratch, independent fix used only by this new
// pipeline, so BooleanCombinePlanar's own use of the shared helper is
// completely unaffected.
Point2d SafeInteriorPoint2d(const std::vector<Point2d>& poly) {
  const size_t n = poly.size();
  size_t lo = 0;
  double y_max = poly[0].y;
  for (size_t i = 1; i < n; ++i) {
    if (poly[i].y < poly[lo].y) lo = i;
    y_max = std::max(y_max, poly[i].y);
  }
  const double y_min = poly[lo].y;
  const double eps = std::max(1e-9, (y_max - y_min) * 1e-6);
  const double y = y_min + eps;
  std::vector<double> xs;
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a = poly[i];
    const Point2d& b = poly[(i + 1) % n];
    if ((a.y <= y) != (b.y <= y)) {
      const double t = (y - a.y) / (b.y - a.y);
      xs.push_back(a.x + t * (b.x - a.x));
    }
  }
  std::sort(xs.begin(), xs.end());
  if (xs.size() >= 2) return Point2d(0.5 * (xs[0] + xs[1]), y);
  // Degenerate input (shouldn't happen for any polygon this pipeline
  // builds) - fall back to the lowest vertex itself rather than crash.
  return poly[lo];
}

// A point guaranteed to lie in the interior of `f`'s own (angle, height)
// or (loop) trim region - the MixedFace-aware sibling of
// RepresentativeInteriorPoint() (above). A cylindrical fragment's own
// trim region is always exactly an axis-aligned (angle, height) rectangle
// (CylindricalFace's own doc comment), so its own midpoint is trivially,
// always interior - no triangulation needed... UNLESS that fragment is
// itself an oblique-cut notch (cap0_notch_points/cap1_notch_points
// non-empty - see SplitCylindricalByObliquePlane, above): there, the
// TRUE physical boundary at that end is the wavy ellipse, not the flat
// v=0/v=length rectangle edge the plain midpoint formula assumes, and for
// a sufficiently large tilt the flat rectangle's own midpoint height can
// sit on the WRONG side of that true wavy boundary at some angles (the
// ellipse's own amplitude can exceed half of the flat reference height) -
// a real, checked-directly correctness gap the plain formula alone does
// not handle, not merely a theoretical worry. Fixed by picking a height
// GUARANTEED to clear the true wavy boundary everywhere across the sweep:
// half of the LOWEST notch sample (for a cap1-notched "lo" fragment,
// guaranteed strictly below the true floor at every angle) or the
// midpoint between the HIGHEST notch sample and the flat v=length top
// (for a cap0-notched "hi" fragment, guaranteed strictly above the true
// ceiling at every angle) - both closed-form, no search beyond a linear
// scan of the same dense sample list already computed once by
// SplitCylindricalByObliquePlane.
Point3d RepresentativeInteriorPointMixed(const MixedFace& f) {
  if (!f.is_cyl) {
    const std::vector<Point2d> loop2d = ProjectLoopOntoPlaneAxes(f.planar.plane, f.planar.loop);
    const Point2d p2d = SafeInteriorPoint2d(loop2d);
    return f.planar.plane.origin + p2d.x * f.planar.plane.xaxis + p2d.y * f.planar.plane.yaxis;
  }
  const Brep::CylindricalFace& cf = f.cyl;
  double height = 0.5 * cf.length;
  if (!cf.cap1_notch_points.empty()) {
    double min_h = std::numeric_limits<double>::infinity();
    for (const Point3d& p : cf.cap1_notch_points) {
      min_h = std::min(min_h, ON_DotProduct(p - cf.frame.origin, cf.frame.zaxis));
    }
    height = std::min(height, 0.5 * min_h);
  }
  if (!cf.cap0_notch_points.empty()) {
    double max_h = -std::numeric_limits<double>::infinity();
    for (const Point3d& p : cf.cap0_notch_points) {
      max_h = std::max(max_h, ON_DotProduct(p - cf.frame.origin, cf.frame.zaxis));
    }
    height = std::max(height, 0.5 * (max_h + cf.length));
  }
  return PointOnCylFace(cf, 0.5 * cf.angle, height);
}

// Result of casting one ray against one MixedFace: how many times it
// crosses that face's own finite trim region cleanly, and whether it
// instead grazed that region's own boundary (in which case the caller
// must abandon this whole ray direction, exactly as
// ClassifyPointVsSolid's own planar-only ray caster already does).
struct FaceHitResult {
  int crossings = 0;
  bool grazed = false;
};

// Ray-vs-one-MixedFace, generalizing ClassifyPointVsSolid's own inner
// per-face loop body to also handle a cylindrical face. The planar branch
// is copied verbatim from that existing, already-verified logic (not
// refactored to share code across the two ray-casters, so
// ClassifyPointVsSolid's own behavior is provably unaffected by this
// function's existence).
//
// The cylindrical branch is a standard closed-form ray-vs-infinite-
// cylinder test: project the ray's origin and direction into the plane
// perpendicular to the cylinder's own axis (subtracting off each vector's
// own component along `cf.frame.zaxis`), giving a 2D ray-vs-circle
// problem - a quadratic in the ray parameter t, solved in closed form,
// exactly like ClipPolygonByCircle3d's own line-circle intersection. For
// each candidate root (t > tol, i.e. genuinely in front of the ray's own
// origin), true axial height is recovered by one dot product and true
// angle by one atan2 in the frame's own local (xaxis, yaxis) basis - then
// checked against the CylindricalFace's own axis-aligned (angle, height)
// trim rectangle, the same "two-interval test" boolean.h's own doc
// comment describes. A hit within `tol` of that rectangle's own boundary
// (in either height or angle) grazes it and can't be parity-counted
// reliably, so it's reported the same way a grazed planar face is.
//
// PLUS: a ray intersection against `cf`'s own two IMPLICIT flat end
// disks/sectors at height 0 and height `cf.length` - not real PlanarFace
// entries anywhere in the Brep (a bounded CylindricalFace used as a
// boolean operand - this increment's own drilling/boss cylinder, built
// via Brep::FromMixedFaces({}, {cf}) with no cap faces at all, since any
// real cap material always ends up either outside the OTHER operand
// entirely or is provided by that operand's own faces instead), but a
// finite-length, finite-angle cylindrical patch still defines a genuinely
// closed SOLID region (a bounded wedge of a cylinder) for point-
// membership purposes, and ray-parity against an OPEN tube (lateral
// surface only, no caps) undercounts by exactly one crossing whenever a
// ray happens to exit through an END rather than the side - confirmed
// directly during development: a point safely outside the cylinder's own
// radius (e.g. one of the drilled box's own side-wall centroids) but
// within its axial height range was misclassified as INSIDE the cylinder
// before these two implicit disks were added, because at least one of
// GenericRayDirections()'s own 8 directions happened to leave through the
// (missing) cap rather than the lateral wall. These two disks are purely
// an internal bookkeeping device for THIS classification, not anything
// FromMixedFaces()/the result Brep ever sees - they never appear as
// actual output faces, don't affect splitting, and cost nothing when
// `faces` is a purely planar list (this whole function only runs for a
// cylindrical `f` at all).
FaceHitResult RayVsMixedFace(const Point3d& p, const Vector3d& d, const MixedFace& f, double tol) {
  FaceHitResult r;
  if (!f.is_cyl) {
    const Brep::PlanarFace& pf = f.planar;
    const double denom = pf.plane.zaxis * d;
    if (std::fabs(denom) < 1e-9) {
      if (std::fabs(pf.plane.DistanceTo(p)) <= tol) r.grazed = true;
      return r;
    }
    const double t = ((pf.plane.origin - p) * pf.plane.zaxis) / denom;
    if (t <= tol) return r;
    const Point3d hit = p + t * d;
    const std::vector<Point2d> loop2d = ProjectLoopOntoPlaneAxes(pf.plane, pf.loop);
    const Point2d hit2d = ProjectOntoPlaneAxes(pf.plane, hit);
    if (DistanceToPolygonBoundary2D(hit2d.x, hit2d.y, loop2d) <= tol) {
      r.grazed = true;
      return r;
    }
    if (PointInPolygon2D(hit2d.x, hit2d.y, loop2d)) r.crossings = 1;
    return r;
  }

  const Brep::CylindricalFace& cf = f.cyl;
  const Vector3d op = p - cf.frame.origin;
  const Vector3d op_perp = op - ON_DotProduct(op, cf.frame.zaxis) * cf.frame.zaxis;
  const Vector3d d_perp = d - ON_DotProduct(d, cf.frame.zaxis) * cf.frame.zaxis;
  const double qa = ON_DotProduct(d_perp, d_perp);
  if (qa < 1e-14) return r;  // ray runs (near-)parallel to the axis - see this function's own doc comment
  const double qb = 2.0 * ON_DotProduct(op_perp, d_perp);
  const double qc = ON_DotProduct(op_perp, op_perp) - cf.radius * cf.radius;
  const double disc = qb * qb - 4.0 * qa * qc;
  if (disc < 0.0) return r;  // the ray's own infinite line never meets the infinite cylinder at all
  const double sq = std::sqrt(disc);
  const double roots[2] = {(-qb - sq) / (2.0 * qa), (-qb + sq) / (2.0 * qa)};
  const bool full = cf.angle >= 2.0 * ON_PI - 1e-9;
  const double ang_tol = tol / std::max(cf.radius, tol);
  for (const double t : roots) {
    if (t <= tol) continue;
    const Point3d hit = p + t * d;
    const Vector3d hp = hit - cf.frame.origin;
    const double h = ON_DotProduct(hp, cf.frame.zaxis);
    if (h < -tol || h > cf.length + tol) continue;  // clearly outside the finite height range
    double ang_margin = std::numeric_limits<double>::infinity();
    if (!full) {
      const Vector3d radial = hp - h * cf.frame.zaxis;
      double ang = std::atan2(ON_DotProduct(radial, cf.frame.yaxis), ON_DotProduct(radial, cf.frame.xaxis));
      if (ang < 0.0) ang += 2.0 * ON_PI;
      if (ang < -ang_tol || ang > cf.angle + ang_tol) continue;  // clearly outside the swept angle range
      ang_margin = std::min(ang, cf.angle - ang);
    }
    const double h_margin = std::min(h, cf.length - h);
    if (h_margin <= tol || ang_margin <= ang_tol) {
      r.grazed = true;
      return r;
    }
    ++r.crossings;
  }

  // The two implicit end disks/sectors - see this function's own doc
  // comment above for why they're needed even though they're never real
  // output faces.
  const double axial_denom = ON_DotProduct(d, cf.frame.zaxis);
  if (std::fabs(axial_denom) >= 1e-12) {
    for (const double h_cap : {0.0, cf.length}) {
      const Point3d cap_center = cf.frame.origin + h_cap * cf.frame.zaxis;
      const double t = ON_DotProduct(cap_center - p, cf.frame.zaxis) / axial_denom;
      if (t <= tol) continue;
      const Point3d hit = p + t * d;
      const Vector3d hp = hit - cap_center;
      const double dist = hp.Length();
      if (dist > cf.radius + tol) continue;  // outside this disk/sector's own outer radius entirely
      double ang_margin = std::numeric_limits<double>::infinity();
      if (!full) {
        double ang = std::atan2(ON_DotProduct(hp, cf.frame.yaxis), ON_DotProduct(hp, cf.frame.xaxis));
        if (ang < 0.0) ang += 2.0 * ON_PI;
        if (ang < -ang_tol || ang > cf.angle + ang_tol) continue;  // outside this sector's own angular wedge
        ang_margin = std::min(ang, cf.angle - ang);
      }
      const double radial_margin = cf.radius - dist;  // >= -tol here (see the `continue` above)
      if (radial_margin <= tol || ang_margin <= ang_tol) {
        r.grazed = true;
        return r;
      }
      ++r.crossings;
    }
  }
  return r;
}

// The MixedFace-aware sibling of ClassifyPointVsSolid() (above), reusing
// that function's own structure (ON-check, then ray-cast, then a nearest-
// face fallback) but not its code, so the existing planar-only classifier
// is provably unaffected by this one's existence.
PointClass ClassifyPointVsMixedSolid(const Point3d& p, const std::vector<MixedFace>& faces, double tol) {
  for (const MixedFace& f : faces) {
    if (!f.is_cyl) {
      if (std::fabs(f.planar.plane.DistanceTo(p)) <= tol) {
        const std::vector<Point2d> loop2d = ProjectLoopOntoPlaneAxes(f.planar.plane, f.planar.loop);
        const Point2d p2d = ProjectOntoPlaneAxes(f.planar.plane, p);
        if (PointInPolygon2D(p2d.x, p2d.y, loop2d)) return PointClass::kOn;
      }
      continue;
    }
    const Brep::CylindricalFace& cf = f.cyl;
    const Vector3d rel = p - cf.frame.origin;
    const double h = ON_DotProduct(rel, cf.frame.zaxis);
    const Vector3d radial = rel - h * cf.frame.zaxis;
    const double dist = radial.Length();
    if (std::fabs(dist - cf.radius) <= tol && h >= -tol && h <= cf.length + tol) {
      const bool full = cf.angle >= 2.0 * ON_PI - 1e-9;
      if (full) return PointClass::kOn;
      double ang = std::atan2(ON_DotProduct(radial, cf.frame.yaxis), ON_DotProduct(radial, cf.frame.xaxis));
      if (ang < 0.0) ang += 2.0 * ON_PI;
      const double ang_tol = tol / std::max(cf.radius, tol);
      if (ang >= -ang_tol && ang <= cf.angle + ang_tol) return PointClass::kOn;
    }
  }

  for (const Vector3d& d : GenericRayDirections()) {
    bool clean = true;
    int crossings = 0;
    for (const MixedFace& f : faces) {
      const FaceHitResult hit = RayVsMixedFace(p, d, f, tol);
      if (hit.grazed) {
        clean = false;
        break;
      }
      crossings += hit.crossings;
    }
    if (clean) return (crossings % 2 == 1) ? PointClass::kIn : PointClass::kOut;
  }

  // Fallback: sign of the distance to the nearest PLANAR face only (a
  // real, disclosed narrowing versus ClassifyPointVsSolid's own fallback,
  // which considers every face) - never expected to be reached in
  // practice, since GenericRayDirections()' own directions are generic/
  // irrational-ish and this increment's own test geometry (an
  // axis-aligned box and an axis-aligned cylinder) has no adversarial
  // grazing alignment with any of them. If every face here happened to be
  // cylindrical (no planar face at all to fall back on), this
  // conservatively reports kOut rather than guessing further.
  double best_abs = std::numeric_limits<double>::infinity();
  double best_signed = 0.0;
  for (const MixedFace& f : faces) {
    if (f.is_cyl) continue;
    const double dist = f.planar.plane.DistanceTo(p);
    if (std::fabs(dist) < best_abs) {
      best_abs = std::fabs(dist);
      best_signed = dist;
    }
  }
  if (!std::isfinite(best_abs)) return PointClass::kOut;
  return (best_signed > 0.0) ? PointClass::kOut : PointClass::kIn;
}

// True if the cylindrical fragment `cf` cannot possibly reach `plane` at
// all - a closed-form conservative bound, not a search: a point on cf's
// own base circle at true angle theta and axial height h has signed
// distance to `plane` equal to
//   base + h*axial + radius*(cos(theta)*A + sin(theta)*B)
// where base = plane.DistanceTo(cf.frame.origin), axial =
// dot(cf.frame.zaxis, plane.zaxis), A = dot(cf.frame.xaxis, plane.zaxis),
// B = dot(cf.frame.yaxis, plane.zaxis) - a plain dot-product sinusoid in
// theta, whose own min/max over a full revolution is exactly
// +/- radius*sqrt(A^2+B^2) (amplitude of a*cos+b*sin), and whose min/max
// over h in [0, length] (linear in h) is at one of the two endpoints. The
// overall min/max of that expression over the WHOLE fragment therefore
// has a closed form with no search - if that whole range stays strictly
// on one side of `plane` (both bounds share sign, outside `tol`), the
// fragment truly cannot cross `plane` anywhere, regardless of how `cf`'s
// own axis happens to be oriented relative to it (this subsumes both the
// "axis lies within the plane" case this increment's own box side walls
// hit, and the general oblique case, into one formula).
bool CylinderPlaneNoInteraction(const Brep::CylindricalFace& cf, const ON_Plane& plane, double tol) {
  const double base = plane.DistanceTo(cf.frame.origin);
  const double axial = ON_DotProduct(cf.frame.zaxis, plane.zaxis);
  const double A = ON_DotProduct(cf.frame.xaxis, plane.zaxis);
  const double B = ON_DotProduct(cf.frame.yaxis, plane.zaxis);
  const double amp = cf.radius * std::sqrt(A * A + B * B);
  const double d0 = base;
  const double d1 = base + cf.length * axial;
  const double lo = std::min(d0, d1) - amp;
  const double hi = std::max(d0, d1) + amp;
  return (lo > tol) || (hi < -tol);
}

// Recovers PlanarFace::ArcRun bookkeeping from a wedge polygon
// detail::ClipPolygonByCircle3d already returned - pure bookkeeping over
// that function's own already-computed output, NOT a second, independent
// re-derivation of any geometry (see PlanarFace::ArcRun's own doc
// comment in brep.h, and this function's own doc comment there, for why
// this is deliberately kept OUTSIDE ClipPolygonByCircle3d itself, unlike
// the prior, reverted attempt at this same fix).
//
// ClipPolygonByCircle3d's own vertex ordering (see its own doc comment
// for the exact construction) guarantees `loop`'s vertex at index 1 is
// always a radial exit point onto the ORIGINAL polygon's own boundary
// (`e0.point`) - generically NOT at distance `radius` from `center` -
// while the run of genuine arc-sample vertices is ALWAYS a single
// contiguous block that wraps across `loop`'s own index-0 seam (the loop
// both starts AND ends at arc samples - see PlanarFace::ArcRun's own doc
// comment for why). So scanning `loop` in the ROTATED order starting at
// index 1 (1, 2, ..., n-1, 0) - guaranteed to begin at a non-arc vertex -
// finds that one run as a single contiguous stretch with no wraparound
// bookkeeping of its own needed here; FindArcRun() itself then reports it
// back in `loop`'s own original (unrotated) indexing, wraparound and all.
//
// Detection is purely by distance from `center`: a vertex within `tol`
// (scaled to at least `radius * 1e-6`, the same relative-tolerance
// pattern MixedFaces()' own recovered-radius check in brep.cpp uses) of
// `radius` from `center` is an arc sample; this needs no knowledge of
// ClipPolygonByCircle3d's own internal sample count or quadrant
// structure, so it stays correct even if that function's own internal
// implementation details change later. Returns std::nullopt (rather than
// throwing) if no run of at least 2 vertices is found - a degenerate
// input this increment's own actual callers never produce, but a safe,
// silent "no conforming tessellation available for this wedge" fallback
// is more defensible than either crashing or guessing.
std::optional<Brep::PlanarFace::ArcRun> FindArcRun(const std::vector<Point3d>& loop, const Point3d& center,
                                                    double radius, const ON_Plane& plane, double tol) {
  const size_t n = loop.size();
  if (n < 2) return std::nullopt;
  const double rtol = std::max(tol, radius * 1e-6);
  std::vector<bool> is_arc(n);
  for (size_t i = 0; i < n; ++i) {
    is_arc[i] = std::fabs(loop[i].DistanceTo(center) - radius) <= rtol;
  }

  size_t best_begin = 0, best_count = 0, cur_begin = 0, cur_count = 0;
  for (size_t k = 0; k < n; ++k) {
    const size_t idx = (1 + k) % n;  // rotated scan starting at index 1
    if (is_arc[idx]) {
      if (cur_count == 0) cur_begin = idx;
      ++cur_count;
      if (cur_count > best_count) {
        best_begin = cur_begin;
        best_count = cur_count;
      }
    } else {
      cur_count = 0;
    }
  }
  if (best_count < 2) return std::nullopt;

  Brep::PlanarFace::ArcRun run;
  run.begin = static_cast<int>(best_begin);
  run.count = static_cast<int>(best_count);
  run.center = center;
  run.radius = radius;
  run.plane_xaxis = plane.xaxis;
  run.plane_yaxis = plane.yaxis;

  auto angle_of = [&](const Point3d& p) {
    const Vector3d d = p - center;
    return std::atan2(ON_DotProduct(d, plane.yaxis), ON_DotProduct(d, plane.xaxis));
  };
  run.angle_begin = angle_of(loop[best_begin]);
  const double raw_end = angle_of(loop[(best_begin + best_count - 1) % n]);

  // atan2's own (-pi, pi] branch cut means `raw_end` can land on the
  // "wrong side" of it relative to `angle_begin` even when the two
  // points are physically close together on the circle (e.g. one just
  // past +pi, reported as a negative angle near -pi) - two INDEPENDENT
  // atan2 calls have no reason to agree on which of a point's own
  // (angle, angle +/- 2*pi, ...) representations to report. Left
  // uncorrected, detail::ArcSchedule3d's own plain linear interpolation
  // from angle_begin to angle_end would sweep the LONG way around the
  // circle instead of the short arc this run actually is - confirmed
  // directly during development (not a theoretical worry): exactly one
  // of the drilled box's own four wedges per cap hit this, and its own
  // substituted boundary points swept 270 degrees instead of the true
  // 90, corrupting that quadrant's own shared boundary with the
  // cylinder and leaving it un-welded.
  //
  // Fixed here by re-expressing the end angle as `angle_begin` plus the
  // SHORTEST signed angular delta to `raw_end` (the standard
  // atan2(sin(d), cos(d)) unwrap, landing in (-pi, pi]) - correct for
  // this run's own only current producer, ClipPolygonByCircle3d, whose
  // every run is exactly one 90-degree quadrant (see its own doc
  // comment) and therefore always strictly under the half-turn this
  // "always take the short way" choice assumes; a hypothetical future
  // producer whose own run spans a HALF turn or more would need a
  // different (not merely atan2-branch-driven) disambiguation, which
  // this function does not attempt.
  const double raw_delta = raw_end - run.angle_begin;
  const double wrapped_delta = std::atan2(std::sin(raw_delta), std::cos(raw_delta));
  run.angle_end = run.angle_begin + wrapped_delta;
  return run;
}

// Result of SplitCylindricalByObliquePlane: either the fragment is left
// completely unmodified (the plane's own ellipse never enters `cf`'s own
// [0, length] band across the whole swept angle - a real interaction
// CylinderPlaneNoInteraction's own conservative closed-form bound can
// still miss, since that bound is deliberately pessimistic, not exact),
// or it genuinely splits into a "below the cut" and "above the cut"
// fragment.
struct ObliqueCylinderSplit {
  bool split = false;
  Brep::CylindricalFace lo, hi;  // valid only when split == true
};

// Splits a FULL-SWEEP (angle == 2*pi) CylindricalFace `cf` by the oblique
// plane described by `ef` (= detail::ComputeEllipseFrame3d(cf, plane) -
// passed in already built, rather than rebuilt here, so a caller sharing
// this same `ef` with the planar side's own detail::ClipPolygonByEllipse3d
// call - case (ii) below - gets the exact same closed-form curve, not two
// independently-computed approximations of it).
//
// Restricted to angle == 2*pi (checked directly, not assumed): a genuinely
// PARTIAL-sweep CylindricalFace's own two rail corners sit at two
// DIFFERENT angular positions, generally at two DIFFERENT true heights
// h(phi) along this same ellipse - so a notched cap's own two endpoints
// could not both match a single scalar `length` the way
// CylindricalFace::cap0_notch_points/cap1_notch_points' own doc comment
// requires, unlike the full-sweep case (where phi=0 and phi=2*pi are the
// SAME physical point by periodicity, so h(0) == h(2*pi) automatically -
// see that field's own doc comment for the direct verification). Every
// CylindricalFace this kernel's own BooleanCombineMixed pipeline ever
// builds as an operand - and every fragment its OWN existing perpendicular
// branch (case (iii)'s align>1-kAxisAlignTol path, which only ever changes
// `length`/`frame.origin`, never `angle`) ever produces - is full-sweep,
// so this restriction costs nothing this increment's own callers actually
// need; a genuinely partial-sweep oblique operand is real, disclosed,
// out-of-scope future work (see boolean.h's own doc comment), not silently
// mishandled.
//
// The height function h(phi) = dot(EllipsePointAt(ef, phi) - cf.frame.origin,
// cf.frame.zaxis) is sampled at `samples`+1 points across the full sweep
// [0, 2*pi]. Three closed-form-checkable outcomes:
//   - h(phi) stays entirely at or below 0 (within `tol`), or entirely at
//     or above cf.length (within `tol`), for EVERY sample: the ellipse
//     never actually enters this fragment's own [0, length] band - no
//     genuine interaction here (the conservative CylinderPlaneNoInteraction
//     bound was merely pessimistic) - `split` is left false, caller keeps
//     `cf` unmodified.
//   - h(phi) stays STRICTLY inside (tol, cf.length - tol) for EVERY
//     sample: the plane's ellipse crosses the WHOLE swept angle strictly
//     between the fragment's two existing ends - the "cylindrical wedge"
//     case this increment's own closed-form volume test targets. Splits
//     into a "lo" fragment (frame.origin unchanged, new length = h(0),
//     cap1_notch_points = the ellipse's own canonical sample list) and a
//     "hi" fragment (frame.origin shifted to the cut height, new length =
//     cf.length - h(0), cap0_notch_points = the SAME canonical sample
//     list) - both anchored at the exact same scalar h(0) (== h(2*pi) by
//     periodicity, see above), so both new fragments' own rail-corner
//     checks in FromMixedFaces() are satisfied by construction, not by
//     coincidence.
//   - anything else (some samples inside the band, some outside): the
//     plane's own ellipse enters and/or exits the [0, length] band only
//     across PART of the swept angle - a harder, genuinely non-monotonic
//     case (this fragment would need MORE than 2 angular*height pieces to
//     represent exactly) this function does not attempt. Throws
//     std::invalid_argument rather than silently building a wrong 2-piece
//     split.
ObliqueCylinderSplit SplitCylindricalByObliquePlane(const Brep::CylindricalFace& cf, const detail::EllipseFrame3d& ef,
                                                     double tol, int samples = 200) {
  if (!(cf.angle >= 2.0 * ON_PI - kAxisAlignTol)) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: an oblique plane+cylinder "
        "interaction against a PARTIAL-sweep (angle < 2*pi) cylindrical "
        "fragment is out of scope for this increment - see "
        "SplitCylindricalByObliquePlane's own doc comment in boolean.cpp");
  }

  const std::vector<Point3d> canonical = detail::EllipseBoundarySample3d(ef, 0.0, 2.0 * ON_PI, samples);
  std::vector<double> h(canonical.size());
  for (size_t s = 0; s < canonical.size(); ++s) {
    h[s] = ON_DotProduct(canonical[s] - cf.frame.origin, cf.frame.zaxis);
  }

  bool all_below = true, all_above = true, all_inside = true;
  for (const double hv : h) {
    if (hv > tol) all_below = false;
    if (hv < cf.length - tol) all_above = false;
    if (!(hv > tol && hv < cf.length - tol)) all_inside = false;
  }

  ObliqueCylinderSplit result;
  if (all_below || all_above) {
    result.split = false;
    return result;
  }
  if (!all_inside) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: an oblique plane's own "
        "intersection with a cylindrical face enters/exits that face's own "
        "[0, length] band across only PART of the swept angle (a "
        "non-monotonic interaction) - out of scope for this increment, see "
        "SplitCylindricalByObliquePlane's own doc comment in boolean.cpp");
  }

  // Genuine sagitta-style tolerance, the same quantity
  // EllipseNotchCornerAtVertex (fillet.cpp) computes for its own ellipse
  // notch: the max distance, over every sample segment, between that
  // segment's own straight-line midpoint and the true curve's own point at
  // the matching MIDPOINT phi (not one of the two sampled endpoints).
  double max_sagitta = 0.0;
  const int n = static_cast<int>(canonical.size()) - 1;
  for (int s = 0; s < n; ++s) {
    const double phi_mid = 2.0 * ON_PI * (static_cast<double>(s) + 0.5) / n;
    const Point3d chord_mid = 0.5 * (canonical[static_cast<size_t>(s)] + canonical[static_cast<size_t>(s) + 1]);
    max_sagitta = std::max(max_sagitta, chord_mid.DistanceTo(detail::EllipsePointAt(ef, phi_mid)));
  }

  const double h0 = h.front();  // == h(2*pi) by periodicity, see this function's own doc comment
  result.split = true;
  result.lo = cf;
  result.lo.length = h0;
  result.lo.cap1_notch_points = canonical;
  result.lo.cap1_notch_tolerance = max_sagitta;
  // `lo`'s own new v=length end is this notched cut, never a genuine
  // unmet terminus of the input cylinder - see
  // CylindricalFace::end0_is_original/end1_is_original's own doc comment.
  // `lo`'s own v=0 end is untouched, inherited via `result.lo = cf` above.
  result.lo.end1_is_original = false;
  result.hi = cf;
  result.hi.frame.origin = cf.frame.origin + h0 * cf.frame.zaxis;
  result.hi.length = cf.length - h0;
  result.hi.cap0_notch_points = canonical;
  result.hi.cap0_notch_tolerance = max_sagitta;
  // Mirror of `lo` above: `hi`'s own new v=0 end is this same notched
  // cut; `hi`'s own v=length end is inherited.
  result.hi.end0_is_original = false;
  return result;
}

// Splits `self` against EVERY face of `other`, keeping both children of
// every genuine cut (case (i)/(iii)) or the single surviving fragment of
// a hole-punch (case (ii)) or an unmodified whole fragment ("no
// interaction") - the MixedFace-aware sibling of SplitAgainstAllPlanes()
// (above). See boolean.h's own BooleanCombineMixed doc comment for the
// four pair cases this dispatches between; throws std::invalid_argument
// for the two genuinely out-of-scope ones (oblique plane/cylinder, any
// cylinder/cylinder interaction) rather than silently approximating them.
std::vector<MixedFace> SplitMixedAgainstAllFaces(MixedFace self, const std::vector<MixedFace>& other, double tol) {
  std::vector<MixedFace> worklist;
  worklist.push_back(std::move(self));
  for (const MixedFace& g : other) {
    std::vector<MixedFace> next;
    next.reserve(worklist.size() * 2);
    for (MixedFace& f : worklist) {
      if (!f.is_cyl && !g.is_cyl) {
        // Case (i): both planar - UNCHANGED, still SplitByHalfspace3d,
        // exactly BooleanCombinePlanar's own SplitAgainstAllPlanes.
        const HalfspaceSplit split = SplitByHalfspace(f.planar.loop, g.planar.plane, tol);
        std::vector<Point3d> inside = CleanPolygon(split.inside, tol);
        std::vector<Point3d> outside = CleanPolygon(split.outside, tol);
        if (inside.size() >= 3) {
          MixedFace m;
          m.planar.plane = f.planar.plane;
          m.planar.loop = std::move(inside);
          next.push_back(std::move(m));
        }
        if (outside.size() >= 3) {
          MixedFace m;
          m.planar.plane = f.planar.plane;
          m.planar.loop = std::move(outside);
          next.push_back(std::move(m));
        }
      } else if (!f.is_cyl && g.is_cyl) {
        // Case (ii) / no-interaction / oblique (out of scope).
        const double align = std::fabs(ON_DotProduct(g.cyl.frame.zaxis, f.planar.plane.zaxis));
        // A real, checked-directly correction to this branch's own
        // earlier form (found and verified during this increment's own
        // end-cap-synthesis testing, not merely theorized): "the axis is
        // perpendicular to this plane" alone does NOT mean the FINITE
        // cylinder `g.cyl` (bounded to [0, g.cyl.length] along that axis)
        // actually reaches this plane at all - only that ITS INFINITE
        // EXTENSION would. The prior form of this branch punched a
        // circular hole into `f.planar` unconditionally whenever
        // `align > 1 - kAxisAlignTol`, regardless of axial reach - exactly
        // right for every EXISTING test (BuildDrilledBoxInputs' own hole
        // always spans clean through both of the box's own z-caps, so the
        // finite cylinder genuinely does reach both), but WRONG whenever
        // it doesn't: a Union/boss cylinder whose base sits flush with
        // (or embedded in) only ONE of a box's two z-perpendicular caps is
        // still perpendicular to the OTHER, untouched cap too - and this
        // branch used to punch a hole in that FAR cap as well, with
        // nothing else in the pipeline ever refilling the missing
        // material (ClipPolygonByCircle3d's own wedges represent ONLY the
        // material outside the circle - see that function's own doc
        // comment - so the "hole" was pure data loss, not merely lost
        // topology). Confirmed directly: every one of this increment's
        // own new Union/boss tests reproduced a genuinely wrong,
        // non-closed, under-volume result at the FAR cap before this
        // correction, using the exact same CylinderPlaneNoInteraction
        // closed-form bound case (iii) below already trusts for the
        // identical question on the OTHER side of this same seam.
        if (align > 1.0 - kAxisAlignTol && CylinderPlaneNoInteraction(g.cyl, f.planar.plane, tol)) {
          next.push_back(std::move(f));
        } else if (align > 1.0 - kAxisAlignTol) {
          // The infinite cylinder's own axis runs perpendicular to
          // `f.planar`'s plane, so its silhouette IN that plane is
          // exactly a circle: center = the projection of the cylinder's
          // own axis point onto the plane along that same axis (one dot
          // product, since axis and plane normal are parallel here),
          // radius = g.cyl.radius. Each surviving piece (see
          // ClipPolygonByCircle3d's own doc comment for why it returns
          // several simple wedge pieces rather than one bridged or holed
          // loop) becomes its own planar MixedFace, continuing
          // independently in the worklist.
          const Point3d proj_center =
              g.cyl.frame.origin - f.planar.plane.DistanceTo(g.cyl.frame.origin) * f.planar.plane.zaxis;
          for (std::vector<Point3d>& piece :
               detail::ClipPolygonByCircle3d(f.planar.loop, f.planar.plane, proj_center, g.cyl.radius, tol)) {
            if (piece.size() < 3) continue;
            MixedFace m;
            m.planar.plane = f.planar.plane;
            // Bookkeeping only, over ClipPolygonByCircle3d's own already-
            // computed output and this branch's own already-computed
            // proj_center/g.cyl.radius/f.planar.plane - see
            // PlanarFace::ArcRun's own doc comment (brep.h) and
            // FindArcRun's own doc comment (above) for why this never
            // touches ClipPolygonByCircle3d itself. Consumed ONLY by
            // Brep::TessellateConforming(); every other consumer of this
            // MixedFace (SplitMixedAgainstAllFaces' own remaining logic,
            // ClassifyPointVsMixedSolid, RepresentativeInteriorPointMixed,
            // Tessellate()) never reads PlanarFace::arc_runs at all.
            if (std::optional<Brep::PlanarFace::ArcRun> run =
                    FindArcRun(piece, proj_center, g.cyl.radius, f.planar.plane, tol)) {
              m.planar.arc_runs.push_back(*run);
            }
            m.planar.loop = std::move(piece);
            next.push_back(std::move(m));
          }
        } else if (CylinderPlaneNoInteraction(g.cyl, f.planar.plane, tol)) {
          next.push_back(std::move(f));
        } else {
          // Case (ii), OBLIQUE: g.cyl's own axis is neither perpendicular
          // to f.planar's plane (the branch above) nor provably
          // non-interacting (CylinderPlaneNoInteraction) - the closed-form
          // ellipse case (see detail::ComputeEllipseFrame3d's own doc
          // comment, ellipse_clip3d.h, for the P(phi) derivation).
          const double axial = ON_DotProduct(g.cyl.frame.zaxis, f.planar.plane.zaxis);
          if (std::fabs(axial) < kMinObliqueC) {
            throw std::invalid_argument(
                "dino8::kernel::BooleanCombineMixed: a planar face crosses a "
                "cylindrical face's silhouette at a grazing (near-axis-"
                "parallel) angle - the ellipse's own semi-major axis is "
                "unboundedly large here, a real geometric degeneracy, not a "
                "bug - out of scope for this increment");
          }
          const detail::EllipseFrame3d ef = detail::ComputeEllipseFrame3d(g.cyl, f.planar.plane, kMinObliqueC);
          for (std::vector<Point3d>& piece : detail::ClipPolygonByEllipse3d(f.planar.loop, f.planar.plane, ef, tol)) {
            if (piece.size() < 3) continue;
            MixedFace m;
            m.planar.plane = f.planar.plane;
            // No arc_runs entry here (unlike the perpendicular branch
            // above): PlanarFace::arc_runs is consumed ONLY by
            // Brep::TessellateConforming(), whose own reconciliation
            // machinery (detail::ArcSchedule3d) is CIRCLE-specific and is
            // deliberately NOT extended to the oblique ellipse case by this
            // increment (see boolean.h's own BooleanCombineMixed doc
            // comment for the honestly-disclosed scope note this implies:
            // ordinary Tessellate() on an oblique-drilled result carries
            // the SAME known non-watertight-at-the-wedge-seam limitation
            // the existing PERPENDICULAR case already has without
            // TessellateConforming() - see TestBooleanCombineMixedDrilledBoxThroughHole's
            // own comment for that pre-existing, unchanged limitation).
            m.planar.loop = std::move(piece);
            next.push_back(std::move(m));
          }
        }
      } else if (f.is_cyl && !g.is_cyl) {
        // Case (iii) / no-interaction / oblique (out of scope) - the
        // trivial, fully exact mirror of case (ii): a plane perpendicular
        // to the cylinder's own axis intersects it along an exact
        // constant-height iso-line.
        const double align = std::fabs(ON_DotProduct(f.cyl.frame.zaxis, g.planar.plane.zaxis));
        if (align > 1.0 - kAxisAlignTol) {
          const double v_cut = ON_DotProduct(g.planar.plane.origin - f.cyl.frame.origin, f.cyl.frame.zaxis);
          if (v_cut > tol && v_cut < f.cyl.length - tol) {
            Brep::CylindricalFace lo = f.cyl;
            lo.length = v_cut;
            // `lo`'s own v=length end is a FRESH boundary this split just
            // cut - never a genuine unmet terminus of the input cylinder
            // (see CylindricalFace::end0_is_original/end1_is_original's
            // own doc comment) - `lo`'s own v=0 end is untouched, so its
            // own flag is simply inherited via `lo = f.cyl` above.
            lo.end1_is_original = false;
            Brep::CylindricalFace hi = f.cyl;
            hi.frame.origin = f.cyl.frame.origin + v_cut * f.cyl.frame.zaxis;
            hi.length = f.cyl.length - v_cut;
            // Mirror of `lo` above: `hi`'s own NEW v=0 end is this same
            // fresh cut; `hi`'s own v=length end is inherited.
            hi.end0_is_original = false;
            MixedFace mlo;
            mlo.is_cyl = true;
            mlo.cyl = lo;
            MixedFace mhi;
            mhi.is_cyl = true;
            mhi.cyl = hi;
            next.push_back(std::move(mlo));
            next.push_back(std::move(mhi));
          } else {
            // The cut plane coincides with (or lies beyond) one of this
            // fragment's own two existing endpoints - nothing to split,
            // the whole fragment already lies on one side.
            next.push_back(std::move(f));
          }
        } else if (CylinderPlaneNoInteraction(f.cyl, g.planar.plane, tol)) {
          next.push_back(std::move(f));
        } else {
          // Case (iii), OBLIQUE: the direct mirror of case (ii)'s own new
          // oblique branch above, splitting the CYLINDRICAL fragment
          // instead of the planar one - see SplitCylindricalByObliquePlane's
          // own doc comment for the closed-form "cylindrical wedge" math.
          const double axial = ON_DotProduct(f.cyl.frame.zaxis, g.planar.plane.zaxis);
          if (std::fabs(axial) < kMinObliqueC) {
            throw std::invalid_argument(
                "dino8::kernel::BooleanCombineMixed: a cylindrical face "
                "crosses a planar face's own cutting plane at a grazing "
                "(near-axis-parallel) angle - the ellipse's own semi-major "
                "axis is unboundedly large here, a real geometric "
                "degeneracy, not a bug - out of scope for this increment");
          }
          const detail::EllipseFrame3d ef = detail::ComputeEllipseFrame3d(f.cyl, g.planar.plane, kMinObliqueC);
          const ObliqueCylinderSplit split = SplitCylindricalByObliquePlane(f.cyl, ef, tol);
          if (!split.split) {
            next.push_back(std::move(f));
          } else {
            MixedFace mlo;
            mlo.is_cyl = true;
            mlo.cyl = split.lo;
            MixedFace mhi;
            mhi.is_cyl = true;
            mhi.cyl = split.hi;
            next.push_back(std::move(mlo));
            next.push_back(std::move(mhi));
          }
        }
      } else {
        // Case (iv): both cylindrical - explicitly OUT OF SCOPE here
        // regardless of whether they'd actually interact (this
        // increment's own test solids never put a cylindrical face on
        // both sides of a single BooleanCombineMixed call, so this branch
        // is never exercised by them) - needs a genuine NURBS-NURBS
        // surface intersection (SurfaceIntersect, dino8-app's own geom
        // layer), a materially bigger, separate follow-up.
        throw std::invalid_argument(
            "dino8::kernel::BooleanCombineMixed: two cylindrical faces "
            "interacting is out of scope for this increment - needs a "
            "genuine NURBS-NURBS surface intersection, see this function's "
            "own doc comment in boolean.h");
      }
    }
    worklist = std::move(next);
  }
  return worklist;
}

struct ClassifiedMixedFace {
  MixedFace face;
  PointClass cls;
};

std::vector<ClassifiedMixedFace> SplitAndClassifyMixed(const std::vector<MixedFace>& self_faces,
                                                        const std::vector<MixedFace>& other_faces, double tol) {
  std::vector<ClassifiedMixedFace> result;
  for (const MixedFace& f : self_faces) {
    for (MixedFace& piece : SplitMixedAgainstAllFaces(f, other_faces, tol)) {
      const Point3d rep = RepresentativeInteriorPointMixed(piece);
      const PointClass cls = ClassifyPointVsMixedSolid(rep, other_faces, tol);
      result.push_back({std::move(piece), cls});
    }
  }
  return result;
}

struct ClassifiedBucketsMixed {
  std::vector<MixedFace> in, out, on;
};

ClassifiedBucketsMixed SplitAndBucketMixed(const std::vector<MixedFace>& self_faces,
                                            const std::vector<MixedFace>& other_faces, double tol) {
  ClassifiedBucketsMixed buckets;
  for (ClassifiedMixedFace& cf : SplitAndClassifyMixed(self_faces, other_faces, tol)) {
    switch (cf.cls) {
      case PointClass::kIn:
        buckets.in.push_back(std::move(cf.face));
        break;
      case PointClass::kOut:
        buckets.out.push_back(std::move(cf.face));
        break;
      case PointClass::kOn:
        buckets.on.push_back(std::move(cf.face));
        break;
    }
  }
  return buckets;
}

// The MixedFace-aware sibling of FlipFace() (above): a planar fragment
// flips exactly as FlipFace() already does (reused directly, not
// reimplemented); a cylindrical fragment has no loop/plane of its own to
// reverse, so it flips via CylindricalFace::outward instead - see that
// field's own doc comment in brep.h for why that's the one piece of
// information a CylindricalFace needs to bound material from either side.
MixedFace FlipMixedFace(MixedFace f) {
  if (!f.is_cyl) {
    f.planar = FlipFace(f.planar);
  } else {
    f.cyl.outward = !f.cyl.outward;
  }
  return f;
}

// Number of boundary samples the placeholder disc polygon below is built
// from - the same order of magnitude ClipPolygonByCircle3d's own default
// `circle_samples = 200` already uses for a comparable circular boundary.
// This is ONLY the placeholder ordinary-Tessellate() fidelity: whenever
// Brep::TessellateConforming() is actually used (as every IsClosedManifold()
// check in this increment's own tests does), this whole polygon is
// discarded and rebuilt at the caller's own requested division count via
// the PlanarFace::ArcRun attached below - see BuildEndCap's own doc
// comment.
constexpr int kEndCapSamples = 200;

// Synthesizes the flat, full-circle Brep::PlanarFace disc that closes one
// end of a surviving Union-result CylindricalFace fragment `cf` whose end
// is a genuine, EXPOSED, unmet terminus of the original input cylinder
// (see CylindricalFace::end0_is_original/end1_is_original's own doc
// comment, and BooleanCombineMixed's own doc comment for the three-case
// argument for when this is and isn't needed) - `at_v0` selects which end
// (true: v=0/cf.frame.origin, false: v=length).
//
// Restricted to a FULL-SWEEP (angle == 2*pi) `cf`: the only kind of bare
// CylindricalFace boolean operand any producer in this codebase ever
// builds today (see CylindricalFace::cap0_notch_points' own doc comment
// for the identical restriction already accepted elsewhere) - a genuinely
// partial-angle ("pie slice") cap would need an additional pair of
// straight radial rails plus a center vertex, a real generalization this
// increment's own test plan never exercises; throws rather than guessing
// at an unverified shape.
//
// Built entirely from already-verified primitives, not new closed-form
// geometry: PointOnCylFace (this file's own exact circle-point evaluator,
// already used by RepresentativeInteriorPointMixed above) samples the
// disc's own placeholder boundary, and a hand-built PlanarFace::ArcRun
// (NOT recovered via FindArcRun - see this function's own orientation
// comment below for exactly why) lets Brep::TessellateConforming() later
// re-sample that SAME boundary, bit-identically, against the adjoining
// cylindrical face's own matching row, at whatever division count the
// caller actually asks for - the exact mechanism already proven correct
// for a wedge cap vs. a cylindrical wall's shared arc (SplitMixedAgainstAllFaces
// case (ii), above).
//
// Orientation: the loop's own true outward normal must be
// `-cf.frame.zaxis` at the v=0 end and `+cf.frame.zaxis` at v=length (or
// the reverse, if `cf.outward` is false - see CylindricalFace::outward's
// own doc comment) for PlanarFace's own "loop is CCW as seen from
// outside, plane.zaxis is the outward normal" invariant to hold. Rather
// than build the loop always in increasing PHYSICAL angle and separately
// reason about when that needs reversing, this parameterizes each wedge's
// own loop directly in the DISC'S OWN plane-local angle (always
// increasing within that wedge, so its own run.angle_begin/angle_end are
// unconditionally correct on BOTH ends - genuinely simpler than
// FindArcRun's own "recover angle_begin/angle_end from an already-built
// loop of unknown handedness" problem: this function builds its own loop
// and its own ArcRun together, from a single already-consistent
// (plane_xaxis, plane_yaxis) basis, rather than inferring one after the
// fact from an arbitrary polygon). `plane.yaxis` (== `run.plane_yaxis`)
// is cf.frame.yaxis when the target normal is +cf.frame.zaxis
// (same-handed, a plain reparameterization) and -cf.frame.yaxis when it's
// -cf.frame.zaxis (mirrored, a reflection) - direct algebra confirms this
// choice always places disc-local angle `theta` at PHYSICAL cf.frame
// angle `+-theta` (sign matching same_handed) while ALWAYS tracing CCW in
// the disc's own (plane.xaxis, plane.yaxis) basis, regardless of which
// case applies - verified by direct substitution, not merely asserted,
// and empirically confirmed by this increment's own volume-sign test (a
// flipped normal would double the closed-form volume error in the wrong
// direction, exactly the kind of falsifiable check this codebase's own
// sibling increments already use).
//
// Split into 4 QUADRANT "pie slice" pieces (center + one quarter-turn arc
// each) rather than one single loop wrapping the whole circle - directly
// mirroring detail::ClipPolygonByCircle3d's own established "4 simple
// wedges, cut at four rays 90 degrees apart" pattern (see that function's
// own top comment for why FOUR pieces, not one bridged/holed loop) rather
// than inventing a new shape this codebase has never built before. This
// is not merely stylistic: a SINGLE loop covering the full [0, 2*pi]
// sweep would need its own ArcRun to span the loop's own start/end seam,
// and detail::ArcSchedule3d's own first and last returned points
// (angle_begin=0, angle_end=2*pi) are the SAME physical location (cos/sin
// are 2*pi-periodic) - a genuine, confirmed-directly duplicate-vertex
// degeneracy at that seam (caught by this increment's own falsifiability
// testing, not merely anticipated) that a plain wedge's own run - always
// strictly UNDER a quarter turn - never has to contend with. Each
// quadrant's own run here is a plain 90-degree sweep, structurally
// identical in shape to every ArcRun this codebase's own existing,
// already-verified TessellateConforming() machinery already handles.
std::vector<MixedFace> BuildEndCap(const Brep::CylindricalFace& cf, bool at_v0) {
  if (!(cf.angle >= 2.0 * ON_PI - kAxisAlignTol)) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: synthesizing an end cap for a "
        "PARTIAL-sweep (angle < 2*pi) cylindrical fragment is out of scope "
        "for this increment - see BuildEndCap's own doc comment in "
        "boolean.cpp");
  }

  const double height = at_v0 ? 0.0 : cf.length;
  const Point3d center = cf.frame.origin + height * cf.frame.zaxis;
  const bool same_handed = at_v0 ? !cf.outward : cf.outward;  // true iff target normal == +cf.frame.zaxis

  ON_Plane plane;
  plane.origin = center;
  plane.xaxis = cf.frame.xaxis;
  plane.yaxis = same_handed ? cf.frame.yaxis : -cf.frame.yaxis;
  plane.zaxis = same_handed ? cf.frame.zaxis : -cf.frame.zaxis;
  plane.UpdateEquation();

  constexpr int kQuadrants = 4;
  const int per_quadrant = std::max(2, kEndCapSamples / kQuadrants);

  std::vector<MixedFace> pieces;
  pieces.reserve(kQuadrants);
  for (int q = 0; q < kQuadrants; ++q) {
    const double plane_theta_begin = cf.angle * static_cast<double>(q) / static_cast<double>(kQuadrants);
    const double plane_theta_end = cf.angle * static_cast<double>(q + 1) / static_cast<double>(kQuadrants);

    std::vector<Point3d> loop;
    loop.reserve(static_cast<size_t>(per_quadrant) + 2);
    loop.push_back(center);  // the one non-arc anchor vertex - see this function's own doc comment
    for (int s = 0; s <= per_quadrant; ++s) {
      const double t = static_cast<double>(s) / static_cast<double>(per_quadrant);
      const double plane_theta = plane_theta_begin + (plane_theta_end - plane_theta_begin) * t;
      const double physical_theta = same_handed ? plane_theta : -plane_theta;
      loop.push_back(PointOnCylFace(cf, physical_theta, height));
    }

    Brep::PlanarFace::ArcRun run;
    run.begin = 1;  // skip the center vertex at index 0
    run.count = per_quadrant + 1;
    run.center = center;
    run.radius = cf.radius;
    run.angle_begin = plane_theta_begin;
    run.angle_end = plane_theta_end;
    run.plane_xaxis = plane.xaxis;
    run.plane_yaxis = plane.yaxis;

    MixedFace m;
    m.planar.plane = plane;
    m.planar.loop = std::move(loop);
    m.planar.arc_runs.push_back(run);
    pieces.push_back(std::move(m));
  }
  return pieces;
}

// Scans every surviving cylindrical fragment in `fragments` (one side's
// own `out` bucket - see BooleanCombineMixed's own Union branch below)
// for an end that (a) is still `end{0,1}_is_original` (a genuine terminus
// of the input cylinder, not a boundary this pipeline already cut against
// `other`'s own surface) and (b) probes as PointClass::kOut just past
// that end against `other` (the SAME `other_faces` list
// SplitAndBucketMixed already classified this very fragment against) -
// i.e. genuinely exposed, nothing else in the result already closes it
// (see BooleanCombineMixed's own doc comment for the worked three-case
// argument this directly implements). Builds a real Brep::PlanarFace disc
// via BuildEndCap for each such end; every fragment/end that fails either
// check is left alone, exactly as before this increment - so this
// function can only ever ADD faces to the Union result, never remove or
// modify one.
//
// The probe point sits ON the cylinder's own axis, a fixed small step
// `probe_eps` past the end (not at the rim) - representative for every
// case this increment's own tests build (a flat operand boundary at or
// near that height), and the same "classify a single interior-ish point"
// technique RepresentativeInteriorPointMixed/ClassifyPointVsMixedSolid
// already rely on elsewhere in this pipeline, not a new technique.
std::vector<MixedFace> SynthesizeEndCaps(const std::vector<MixedFace>& fragments, const std::vector<MixedFace>& other,
                                          double tol) {
  std::vector<MixedFace> caps;
  for (const MixedFace& f : fragments) {
    if (!f.is_cyl) continue;
    const Brep::CylindricalFace& cf = f.cyl;
    const double probe_eps = std::max(tol, 1e-6 * std::max(cf.radius, std::max(cf.length, 1.0)));
    if (cf.end0_is_original) {
      const Point3d probe = cf.frame.origin - probe_eps * cf.frame.zaxis;
      if (ClassifyPointVsMixedSolid(probe, other, tol) == PointClass::kOut) {
        for (MixedFace& piece : BuildEndCap(cf, /*at_v0=*/true)) caps.push_back(std::move(piece));
      }
    }
    if (cf.end1_is_original) {
      const Point3d probe = cf.frame.origin + (cf.length + probe_eps) * cf.frame.zaxis;
      if (ClassifyPointVsMixedSolid(probe, other, tol) == PointClass::kOut) {
        for (MixedFace& piece : BuildEndCap(cf, /*at_v0=*/false)) caps.push_back(std::move(piece));
      }
    }
  }
  return caps;
}

}  // namespace

Brep BooleanCombineMixed(const Brep& a, const Brep& b, BooleanOp op) {
  std::vector<MixedFace> fa = ToMixed(a.MixedFaces());
  std::vector<MixedFace> fb = ToMixed(b.MixedFaces());
  const double tol = std::max(RelativeTolMixed(fa), RelativeTolMixed(fb));

  if (op == BooleanOp::SymmetricDifference) {
    const Brep union_brep = BooleanCombineMixed(a, b, BooleanOp::Union);
    const Brep intersection_brep = BooleanCombineMixed(a, b, BooleanOp::Intersection);
    return BooleanCombineMixed(union_brep, intersection_brep, BooleanOp::Difference);
  }

  const ClassifiedBucketsMixed from_a = SplitAndBucketMixed(fa, fb, tol);
  const ClassifiedBucketsMixed from_b = SplitAndBucketMixed(fb, fa, tol);

  // Same coincident-plane dedup BooleanCombinePlanar's own Difference
  // branch uses, restricted to the planar/planar pair (a coincident
  // CylindricalFace "on" pair needs its own dedup rule this increment's
  // own narrow test scope never exercises - see boolean.h's own doc
  // comment; a CylindricalFace fragment's own representative point is
  // always strictly interior to its own solid along its curved surface's
  // interior height range in every case this increment's tests produce,
  // so it is classified kIn/kOut directly rather than ever landing in
  // the `on` bucket at all - not a silently-dropped case, a genuinely
  // unreached one for the geometry this increment builds).
  auto same_plane = [tol](const ON_Plane& p, const ON_Plane& q) {
    return std::fabs(p.DistanceTo(q.origin)) <= tol && p.zaxis.IsParallelTo(q.zaxis, 1e-6) == 1;
  };

  std::vector<MixedFace> result;
  switch (op) {
    case BooleanOp::Union: {
      for (const MixedFace& f : from_a.out) result.push_back(f);
      for (const MixedFace& f : from_b.out) result.push_back(f);
      for (const MixedFace& f : from_a.on) result.push_back(f);
      // End-cap synthesis (see SynthesizeEndCaps' own doc comment above,
      // and this function's own doc comment in boolean.h): a bare
      // CylindricalFace Union/boss operand's own genuinely EXPOSED,
      // never-split end has no PlanarFace anywhere in either operand to
      // close it - every existing branch above only ever collects
      // fragments the two operands ALREADY built, never synthesizes new
      // material. `from_a.on`/`from_b.on` are not scanned here: a
      // cylindrical fragment's own representative point is always
      // strictly interior along its curved surface, so it is classified
      // kIn/kOut directly and never lands in the `on` bucket at all for
      // any geometry this increment's own tests build (see this
      // function's own same_plane comment above for the analogous,
      // already-accepted narrowing on the Difference path).
      for (MixedFace& cap : SynthesizeEndCaps(from_a.out, fb, tol)) result.push_back(std::move(cap));
      for (MixedFace& cap : SynthesizeEndCaps(from_b.out, fa, tol)) result.push_back(std::move(cap));
      break;
    }
    case BooleanOp::Intersection:
      for (const MixedFace& f : from_a.in) result.push_back(f);
      for (const MixedFace& f : from_b.in) result.push_back(f);
      for (const MixedFace& f : from_a.on) result.push_back(f);
      break;
    case BooleanOp::Difference:
      for (const MixedFace& f : from_a.out) result.push_back(f);
      for (const MixedFace& f : from_b.in) result.push_back(FlipMixedFace(f));
      for (const MixedFace& a_on : from_a.on) {
        bool cancelled = false;
        for (const MixedFace& b_on : from_b.on) {
          if (!a_on.is_cyl && !b_on.is_cyl && same_plane(a_on.planar.plane, b_on.planar.plane)) {
            cancelled = true;
            break;
          }
        }
        if (!cancelled) result.push_back(a_on);
      }
      break;
    default:
      throw std::invalid_argument("dino8::kernel::BooleanCombineMixed: unknown BooleanOp");
  }

  std::vector<Brep::PlanarFace> out_planar;
  std::vector<Brep::CylindricalFace> out_cyl;
  out_planar.reserve(result.size());
  for (const MixedFace& f : result) {
    if (f.is_cyl) {
      out_cyl.push_back(f.cyl);
    } else {
      out_planar.push_back(f.planar);
    }
  }
  return Brep::FromMixedFaces(out_planar, out_cyl);
}

}  // namespace dino8::kernel
