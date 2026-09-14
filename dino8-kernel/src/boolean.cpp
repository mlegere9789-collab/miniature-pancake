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
  const size_t n = f.loop.size();
  std::reverse(f.loop.begin(), f.loop.end());
  // Reversing the loop's own vertex order (above) means any attached
  // ArcRun (PlanarFace::ArcRun's own doc comment, brep.h) - recording
  // WHERE in the loop an arc lives, and which direction it sweeps - must
  // be remapped too, not left stale referencing pre-reversal indices/
  // direction. Reversing a length-n array maps old index i to new index
  // (n-1-i), so a contiguous OLD run [begin, begin+count) (mod n, the
  // same circular convention BuildResampledWedgeLoop already uses)
  // becomes the contiguous NEW range starting at (n-begin-count) mod n,
  // same length - but now walked in the OPPOSITE direction, so the run's
  // own angle_begin/angle_end (the SAME two physical endpoints, still)
  // must swap too, or a later re-sample (detail::ArcSchedule3d) would
  // trace this arc backwards relative to the loop's own new vertex
  // order.
  //
  // Found and fixed here by direct counterexample, not anticipated by
  // this function's own original form (which predates any PlanarFace
  // ever carrying content AFTER its own arc run): BuildLensEndCap's own
  // lens-cap loop, [arc_samples..., chord_mid] with run.begin=0 and
  // run.count=the arc's own sample count, leaves exactly one trailing
  // NON-arc vertex (chord_mid) past the run. Left unfixed, a reversed
  // run's own `begin` still pointed at the SAME numeric index as before
  // reversal - which, after reversal, generally lands on a completely
  // DIFFERENT vertex (chord_mid itself, shifted from the loop's own end
  // to its new index 0) - silently overwriting it with a resampled arc
  // point instead and losing the real chord_mid vertex entirely,
  // corrupting the shared boundary this run exists to reconcile against
  // (confirmed directly: exactly the failure this increment's own
  // reverse-subtraction Difference test caught, not a theoretical
  // worry - see TestBooleanCombineMixedParallelCylinderDifferenceAMinusBLensComplement's
  // own comment in the test file).
  for (Brep::PlanarFace::ArcRun& run : f.arc_runs) {
    if (n == 0) continue;
    const size_t new_begin =
        ((static_cast<size_t>(n) - static_cast<size_t>(run.begin) % n - static_cast<size_t>(run.count) % n) % n + n) %
        n;
    run.begin = static_cast<int>(new_begin);
    std::swap(run.angle_begin, run.angle_end);
    // A LITERAL run (see PlanarFace::ArcRun::literal_points) carries its
    // points in the loop's own walk order, so they reverse with it - the
    // literal counterpart of the angle swap just above.
    std::reverse(run.literal_points.begin(), run.literal_points.end());
  }
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

namespace {

// A Brep::Compound() of two or more lumps (the representation every
// SymmetricDifference result below has) is refused as an operand of
// either B-rep boolean: Difference and Intersection would distribute over
// the lumps exactly ((L1 u L2) - B = (L1 - B) u (L2 - B)), but Union
// needs a merge step between lumps that touch or overlap, and neither
// pipeline below has one - the single-shell split/classify/reassemble
// would hand FromMixedFaces the non-manifold contact edges. Refused with
// a clear message rather than failing deep inside FromMixedFaces.
void RefuseCompoundOperand(const Brep& operand, const char* function_name) {
  if (operand.LumpFaceRanges().size() <= 1) return;
  throw std::invalid_argument(std::string("dino8::kernel::") + function_name +
                              ": an operand is a Brep::Compound of several lumps (e.g. a "
                              "SymmetricDifference result) - a boolean over lumps needs a "
                              "per-lump distribution plus a Union merge step this kernel does "
                              "not have yet; see Brep::Compound's own doc comment in brep.h");
}

}  // namespace

Brep BooleanCombinePlanar(const Brep& a, const Brep& b, BooleanOp op) {
  RefuseCompoundOperand(a, "BooleanCombinePlanar");
  RefuseCompoundOperand(b, "BooleanCombinePlanar");

  if (op == BooleanOp::SymmetricDifference) {
    // XOR = (A - B) u (B - A), as a Brep::Compound of the two lumps: the
    // two differences touch along the intersection curve of the two
    // boundaries, where the XOR boundary has FOUR incident faces (A's
    // outside, B's outside, and both flipped insides), so no single
    // FromPlanarFaces shell can hold it - the former Difference(Union,
    // Intersection) chain threw "an edge is shared by 3 or more faces"
    // from that final reassembly for two overlapping boxes. Trivially
    // correct given Difference is correct in both argument orders (each
    // lump IS one verified Difference result); no coincident-face rule
    // beyond Difference's own is involved.
    return Brep::Compound({BooleanCombinePlanar(a, b, BooleanOp::Difference),
                           BooleanCombinePlanar(b, a, BooleanOp::Difference)});
  }

  const std::vector<Brep::PlanarFace> fa = a.PlanarFaces();
  const std::vector<Brep::PlanarFace> fb = b.PlanarFaces();
  const double tol = std::max(RelativeTol(fa), RelativeTol(fb));

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

// The CLOSED-OPERAND rule (see BooleanCombineMixed's own doc comment in
// boolean.h): an operand is a BARE TUBE - the legacy drill/boss operand
// Brep::FromMixedFaces({}, {cf}), plain un-notched cylindrical faces and
// nothing else - or it is a CLOSED SOLID whose own faces already bound
// it. Only a bare tube keeps the implicit-end-disk (RayVsMixedFace) and
// end-cap-synthesis (SynthesizeEndCaps) semantics its end flags encode;
// a closed operand's cylindrical ends are all "already sealed" (flags
// forced false here), so no implicit disk is ever cast against them and
// no cap is ever synthesized for their fragments. This is decided per
// OPERAND, not per face: a boss's base inside a Union result is a
// still-original, never-split end of its input cylinder AND an open
// passage into the box, and no per-face flag can say "needs a disk" -
// the box's own faces already bound that solid. Without this rule every
// chained call whose first result kept a cylindrical face re-fired
// SynthesizeEndCaps on that face's true/true flags and stitched spurious
// quadrant caps across a hole that the first call had already sealed
// (measured on Difference(Difference(box, h1), h2): 8 spurious caps at
// the first hole's two ends, volume off by exactly -10*pi/3), or threw
// "3 or more faces" where the spurious cap's edges collided.
//
// Why "any planar face, or any notched cylindrical face" and nothing
// else: a planar face is what every closed solid this kernel builds has
// (a box, a drilled box, a union with a boss, a Steinmetz union with its
// half-disc caps); the one closed solid with NO planar face is the
// Steinmetz Intersection (four doubly-notched eyes), which the notch test
// catches - and a notch is only ever produced by a boolean split or by a
// hand-built closed fixture, never by a drill operand. Neither a partial
// sweep nor a count of cylindrical faces marks a closed solid: a bare
// partial-sweep wedge, and a bare full tube whose wall happens to be two
// un-notched half-bands, are both open shells that only the implicit
// disks close, exactly as before. Every operand the existing suite feeds
// in is either planar-only (the rule is vacuous - no cylindrical face to
// override, no implicit disk to skip) or a bare un-notched tube (the rule
// leaves it bare), so the suite's every result is bit-for-bit unchanged
// (verified: 1267 identical checks in identical order).
std::vector<MixedFace> ToMixed(const Brep::MixedFacesResult& mf) {
  if (!mf.conical.empty()) {
    // Previously dropped silently, leaving a tapered-fillet operand with
    // a hole in its boundary - refused honestly instead.
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: an operand has a ConicalFace (a "
        "tapered fillet) - the mixed planar/cylindrical pipeline has no cone "
        "splitter or classifier, so such an operand is refused rather than "
        "having its conical faces silently dropped from its boundary");
  }
  bool bare_tube = mf.planar.empty();
  for (const Brep::CylindricalFace& c : mf.cylindrical) {
    if (!c.cap0_notch_points.empty() || !c.cap1_notch_points.empty()) bare_tube = false;
  }

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
    if (!bare_tube) {
      m.cyl.end0_is_original = false;
      m.cyl.end1_is_original = false;
    }
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
//
// A face notched at BOTH ends - a Steinmetz eye or an unequal-radius plug
// (length == 0; see SplitCylindricalBySteinmetzCylinder's and
// SplitCylindricalByUnequalCylinder's own section comments, below), or
// the unequal-radius split's positive-length MIDDLE band of the smaller
// cylinder - has no flat end to measure from at all, so it takes its own
// branch first: the mid-angle point halfway between the two curves' own
// mid-angle samples, which is where the other cylinder's axis pierces
// this wall (an eye or plug) or where this wall crosses the other
// cylinder's axis (a middle band - the point on the smaller cylinder's
// wall at the height of the crossing, at the mid-angle of the half, is
// ON the larger cylinder's axis). The half-bands and the unequal-radius
// split's other pieces need no new branch: an upper piece (cap0 notched,
// notch heights in [0, length]) lands on the existing "hi" formula and a
// lower one (cap1 notched) on the existing "lo" formula, and the extent
// precondition that admits each split guarantees both points sit outside
// the crossing cylinder - verified by the Steinmetz and unequal-radius
// Union/Difference face counts and volumes in the tests, not by
// inspection. The unequal-radius split's plain pieces cut by a SLOPED
// chain at a general axis angle land on the same two formulas: the lower
// piece's point is at half the chain's lowest height (half of L/2 -
// |h_ext| from the wall's bottom, below the chain everywhere), the upper
// piece's halfway between the chain's highest height and its flat top
// (above the chain everywhere), both at the plain piece's mid-angle,
// where the wall is at common-perpendicular coordinate +/- r_a, farther
// from the smaller cylinder's axis than its radius at every height.
Point3d RepresentativeInteriorPointMixed(const MixedFace& f) {
  if (!f.is_cyl) {
    // A genuine "pure fan" piece - loop == [center, arc_sample_0, ...,
    // arc_sample_N] with NO other straight-boundary vertices at all (the
    // one ArcRun present covers every vertex except the center at index
    // 0) - is exactly the shape detail::ClipPolygonByCircleInsideOnly3d's
    // own quadrant pieces have (see that function's own doc comment,
    // circle_clip3d.h). SafeInteriorPoint2d's own generic horizontal-scan
    // heuristic (below) picks a point only `eps`-above this piece's own
    // lowest vertex - fine for an ordinary wedge (interleaved with
    // `poly`'s own original boundary, so its own y-extent is bounded by
    // the whole FACE's scale, keeping that margin comfortably above any
    // reasonable tolerance), but for a pure fan whose own y-extent is
    // bounded ONLY by `radius` itself, that same relative-`eps` margin
    // shrinks right along with `radius` - and for a small enough radius
    // (a real, checked-directly failure, not a theoretical worry: a
    // radius=0.01 disc against this pipeline's typical ~1e-8 boolean
    // tolerance) the chosen point lands close enough to the arc boundary
    // to be misclassified PointClass::kOn instead of kIn against the very
    // solid this piece is carved from, leaking a spurious face into a
    // Difference/Union result that should have discarded it entirely.
    // Closed-form and always safe instead, for this one known shape: the
    // point half a radius out from `center` along the arc run's own
    // mid-angle direction sits exactly `0.5 * radius` inside the curved
    // boundary and strictly inside both straight radial edges (any
    // sweep under a full turn, which every producer of this shape - this
    // one included, see that function's own doc comment on why every arc
    // run here stays under a half turn - already guarantees), with a
    // margin that scales WITH `radius` instead of shrinking independently
    // of it. Provably inert on every OTHER MixedFace shape this pipeline
    // already builds: an ordinary ClipPolygonByCircle3d wedge always
    // interleaves at least one piece of `poly`'s own original boundary
    // between its own two arc endpoints (see that function's own doc
    // comment), so `run.count` there is always strictly less than
    // `loop.size() - 1`, and BuildEndCap's own caps never reach this
    // function at all (SynthesizeEndCaps appends them straight to the
    // result, see BooleanCombineMixed's own doc comment) - so this branch
    // is reached, in this codebase today, only by
    // ClipPolygonByCircleInsideOnly3d's own pieces.
    // Guarded on the run being a genuine circular arc: a LITERAL run
    // (PlanarFace::ArcRun::literal_points, the oblique ellipse pieces)
    // has no center/radius/angles to evaluate, and its shape (a sample
    // stretch plus at least two straight rails and a perimeter walk)
    // never matches this begin/count signature anyway - the guard makes
    // that structural fact explicit rather than relied upon.
    const std::vector<Brep::PlanarFace::ArcRun>& runs = f.planar.arc_runs;
    if (runs.size() == 1 && runs[0].literal_points.empty() && runs[0].begin == 1 &&
        static_cast<size_t>(runs[0].count) == f.planar.loop.size() - 1) {
      const Brep::PlanarFace::ArcRun& run = runs[0];
      const double mid_angle = 0.5 * (run.angle_begin + run.angle_end);
      return run.center + (0.5 * run.radius) * (std::cos(mid_angle) * run.plane_xaxis +
                                                  std::sin(mid_angle) * run.plane_yaxis);
    }
    const std::vector<Point2d> loop2d = ProjectLoopOntoPlaneAxes(f.planar.plane, f.planar.loop);
    const Point2d p2d = SafeInteriorPoint2d(loop2d);
    return f.planar.plane.origin + p2d.x * f.planar.plane.xaxis + p2d.y * f.planar.plane.yaxis;
  }
  const Brep::CylindricalFace& cf = f.cyl;
  if (!cf.cap0_notch_points.empty() && !cf.cap1_notch_points.empty()) {
    // A Steinmetz EYE (see SplitCylindricalBySteinmetzCylinder's own
    // section comment): length == 0, bounded below by cap0's half-ellipse
    // and above by cap1's, meeting only at the two pinch vertices at local
    // angles 0 and `angle`. Neither single-notch formula below applies
    // (there is no flat end to measure from at all), so the point is
    // placed at the mid-angle, halfway between the two curves' own
    // mid-angle samples - each list's middle entry sits exactly at that
    // cylinder's mid-angle (it is the sample in the plane spanned by the
    // two axes, for BOTH cylinders), so this height is strictly between
    // the two boundary curves there, with margin half the eye's own
    // mid-angle height (r*min(cot, tan)(alpha/2) at least). At that point
    // the OTHER cylinder's axis pierces this wall, so the classification
    // against it is an unambiguous kIn. The unequal-radius split's plug
    // (same shape, angle 2*asin(r_b/r_a)) and its positive-length middle
    // band land here too, with the same property: both lists' middle
    // entries are the samples at theta = phi + pi/2 on the smaller
    // cylinder, i.e. in the plane spanned by the two axes, so the point
    // is on the other cylinder's axis (margin r_b for a plug, r_a for a
    // middle band) - see that split's own section comment.
    const size_t m0 = cf.cap0_notch_points.size() / 2;
    const size_t m1 = cf.cap1_notch_points.size() / 2;
    const double h_bottom = ON_DotProduct(cf.cap0_notch_points[m0] - cf.frame.origin, cf.frame.zaxis);
    const double h_top = ON_DotProduct(cf.cap1_notch_points[m1] - cf.frame.origin, cf.frame.zaxis);
    return PointOnCylFace(cf, 0.5 * cf.angle, 0.5 * (h_bottom + h_top));
  }
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

// Interpolates the TRUE height of a notch curve at a given angle, from one
// of a CylindricalFace's own cap0_notch_points/cap1_notch_points chains -
// the same dense, fixed-order (first point at angle 0, last at cf.angle,
// interior points recovered by atan2 in cf.frame's own (xaxis, yaxis)
// basis) sample lists FromMixedFaces' own notch_uv lambda (brep.cpp, see
// its doc comment) already reinterprets, walked here instead of re-fit,
// so no separate (angle, height) table is invented - there isn't one
// stored anywhere, only the 3D points themselves (CylindricalFace's own
// field). `angle` is expected to already be clamped to [0, cf.angle] by
// the caller (both ClassifyPointVsMixedSolid's ON-check and
// RayVsMixedFace's cylindrical branch do this before calling in), so this
// never has to extrapolate past either end of the chain's own domain.
double NotchHeightAt(const std::vector<Point3d>& chain, const Brep::CylindricalFace& cf, double angle) {
  double prev_angle = 0.0;
  double prev_h = ON_DotProduct(chain.front() - cf.frame.origin, cf.frame.zaxis);
  for (size_t i = 1; i < chain.size(); ++i) {
    const Vector3d d = chain[i] - cf.frame.origin;
    const double h = ON_DotProduct(d, cf.frame.zaxis);
    double a;
    if (i + 1 == chain.size()) {
      a = cf.angle;
    } else {
      a = std::atan2(ON_DotProduct(d, cf.frame.yaxis), ON_DotProduct(d, cf.frame.xaxis));
      if (a < 0.0) a += 2.0 * ON_PI;
    }
    if (angle <= a || i + 1 == chain.size()) {
      if (a <= prev_angle) return h;  // degenerate (coincident-angle) segment - use this sample directly
      const double t = (angle - prev_angle) / (a - prev_angle);
      return prev_h + std::max(0.0, std::min(1.0, t)) * (h - prev_h);
    }
    prev_angle = a;
    prev_h = h;
  }
  return prev_h;  // angle >= chain.back()'s own angle (== cf.angle) - return the last sample's height
}

// The wall's true lower/upper height bound at a given angle: the flat rail
// (0 / cf.length) for an un-notched end, or the notch curve's own
// interpolated height (NotchHeightAt) for a notched one. For an un-notched
// face this reduces to exactly the constants 0.0/cf.length used
// everywhere before this increment, so every call site substituting these
// in for the old flat bounds is a pure widening for a notched face and a
// bit-identical no-op otherwise.
double Cap0HeightAt(const Brep::CylindricalFace& cf, double angle) {
  return cf.cap0_notch_points.empty() ? 0.0 : NotchHeightAt(cf.cap0_notch_points, cf, angle);
}
double Cap1HeightAt(const Brep::CylindricalFace& cf, double angle) {
  return cf.cap1_notch_points.empty() ? cf.length : NotchHeightAt(cf.cap1_notch_points, cf, angle);
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
    // The true angle is needed for the notch-height lookup below even for
    // a FULL (2*pi) sweep - a full cylindrical face can still carry a
    // notched cap (an oblique-cut tube/hole, e.g. SplitCylindricalByObliquePlane's
    // own full-sweep case, boolean.cpp:1998/2217) whose height varies with
    // angle even though there is no angular TRIM to gate on.
    const Vector3d radial = hp - h * cf.frame.zaxis;
    double ang = std::atan2(ON_DotProduct(radial, cf.frame.yaxis), ON_DotProduct(radial, cf.frame.xaxis));
    if (ang < 0.0) ang += 2.0 * ON_PI;
    double ang_margin = std::numeric_limits<double>::infinity();
    if (!full) {
      if (ang < -ang_tol || ang > cf.angle + ang_tol) continue;  // clearly outside the swept angle range
      ang_margin = std::min(ang, cf.angle - ang);
    }
    // Notch-aware height bounds (Cap0HeightAt/Cap1HeightAt): for an
    // un-notched end these are exactly 0.0/cf.length, so this is a
    // bit-identical no-op versus the old flat gate on every fixture that
    // predates notched faces - see those functions' own doc comments.
    const double clamped_ang = std::max(0.0, std::min(cf.angle, ang));
    const double lo = Cap0HeightAt(cf, clamped_ang);
    const double hi = Cap1HeightAt(cf, clamped_ang);
    if (h < lo - tol || h > hi + tol) continue;  // clearly outside the true (possibly notched) height range
    const double h_margin = std::min(h - lo, hi - h);
    if (h_margin <= tol || ang_margin <= ang_tol) {
      r.grazed = true;
      return r;
    }
    ++r.crossings;
  }

  // The two implicit end disks/sectors - see this function's own doc
  // comment above for why they're needed even though they're never real
  // output faces. An implicit disk exists only at an end that is still
  // an OPEN original terminus of a bare-tube operand: ToMixed (above)
  // clears both flags on every cylindrical face of a closed operand,
  // whose own faces already bound its solid, so a ray leaving such a
  // face's end passes through the operand's real cap or into its
  // interior and must not be counted twice. Every list this function
  // ever sees (SplitAndClassifyMixed's `other_faces`, SynthesizeEndCaps'
  // `other`) is an unsplit ToMixed list, so for a bare tube both flags
  // are still true here and both disks are cast exactly as before.
  const double axial_denom = ON_DotProduct(d, cf.frame.zaxis);
  if (std::fabs(axial_denom) >= 1e-12) {
    for (int end = 0; end < 2; ++end) {
      if (end == 0 ? !cf.end0_is_original : !cf.end1_is_original) continue;
      const double h_cap = end == 0 ? 0.0 : cf.length;
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
    if (std::fabs(dist - cf.radius) <= tol) {
      const bool full = cf.angle >= 2.0 * ON_PI - 1e-9;
      // The true angle is needed for the notch-height lookup below even
      // for a full (2*pi) sweep - see RayVsMixedFace's identical note.
      double ang = std::atan2(ON_DotProduct(radial, cf.frame.yaxis), ON_DotProduct(radial, cf.frame.xaxis));
      if (ang < 0.0) ang += 2.0 * ON_PI;
      const double ang_tol = tol / std::max(cf.radius, tol);
      if (full || (ang >= -ang_tol && ang <= cf.angle + ang_tol)) {
        // Notch-aware height bounds (Cap0HeightAt/Cap1HeightAt): reduces
        // to exactly today's `h >= -tol && h <= cf.length + tol` for any
        // un-notched face - bit-identical no-op there.
        const double clamped_ang = std::max(0.0, std::min(cf.angle, ang));
        const double lo = Cap0HeightAt(cf, clamped_ang);
        const double hi = Cap1HeightAt(cf, clamped_ang);
        if (h >= lo - tol && h <= hi + tol) return PointClass::kOn;
      }
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
// A fragment's own axial band is [0, length] widened, for a notched
// fragment, to cover its notch curves - the same widening
// Brep::FromMixedFaces() applies to the surface's v-domain (see
// CylindricalFace's own doc comment in brep.h), plus twice the notch's
// own sagitta bound: the polyline's extreme heights can undershoot the
// true ellipse's by at most one segment sagitta (the h-component of the
// chord-midpoint deviation, at the segment holding the curve's own
// extremum), doubled here for a margin that costs nothing. Moved ahead of
// its first use in CylinderPlaneNoInteraction below (NonParallelCylinder-
// PairNoInteraction's own fuller doc comment, further down this file,
// covers this same helper's role for the cylinder/cylinder no-interaction
// test too).
struct AxialBand {
  double lo = 0.0, hi = 0.0;
};

AxialBand CylindricalFragmentAxialBand(const Brep::CylindricalFace& cf) {
  AxialBand band{0.0, cf.length};
  auto widen = [&](const std::vector<Point3d>& notch, double notch_tolerance) {
    const double margin = 2.0 * notch_tolerance;
    for (const Point3d& p : notch) {
      const double h = ON_DotProduct(p - cf.frame.origin, cf.frame.zaxis);
      band.lo = std::min(band.lo, h - margin);
      band.hi = std::max(band.hi, h + margin);
    }
  };
  widen(cf.cap0_notch_points, cf.cap0_notch_tolerance);
  widen(cf.cap1_notch_points, cf.cap1_notch_tolerance);
  return band;
}

bool CylinderPlaneNoInteraction(const Brep::CylindricalFace& cf, const ON_Plane& plane, double tol) {
  // The axial extent this bound sweeps over `h` is widened to
  // CylindricalFragmentAxialBand's own [lo,hi] - the un-notched
  // [0,length] rectangle when cf carries no notch, but wider whenever a
  // notch pushes true material below v=0 or above v=length (a cap0 notch
  // generally dips below v=0, a cap1 notch rises above v=length - see
  // CylindricalFace's own doc comment). Without this widening, a plane
  // that only reaches the notched-away extra material would be wrongly
  // declared non-interacting and passed through unmodified. For an
  // un-notched face the band is exactly {0, cf.length}, so this is a
  // bit-identical no-op versus the old d0/d1 formula.
  const AxialBand band = CylindricalFragmentAxialBand(cf);
  const double base = plane.DistanceTo(cf.frame.origin);
  const double axial = ON_DotProduct(cf.frame.zaxis, plane.zaxis);
  const double A = ON_DotProduct(cf.frame.xaxis, plane.zaxis);
  const double B = ON_DotProduct(cf.frame.yaxis, plane.zaxis);
  const double amp = cf.radius * std::sqrt(A * A + B * B);
  const double d0 = base + band.lo * axial;
  const double d1 = base + band.hi * axial;
  const double lo = std::min(d0, d1) - amp;
  const double hi = std::max(d0, d1) + amp;
  return (lo > tol) || (hi < -tol);
}

// True iff two PARALLEL-AXIS cylindrical fragments' own infinite cylinders
// cannot possibly interact at all - closed form, no search: with both axes
// parallel, the radial distance between the two axis LINES (`dist`,
// computed once by projecting `cf_b`'s own axis point into `cf_a`'s own
// (xaxis, yaxis) plane - valid because that projection is the SAME for
// every point along either infinite axis line, both being parallel to the
// same direction) is constant along the shared axis direction, so "the two
// walls never touch, anywhere along their shared axis direction" is
// exactly `dist > r_a + r_b` (the two circular cross-sections are disjoint)
// or `dist < |r_a - r_b|` (one cross-section is strictly nested inside the
// other, no crossing) - the same two-regime circle/circle non-intersection
// test the standard closed-form circle-circle intersection construction
// (Weisstein, MathWorld, "Circle-Circle Intersection"; equivalently Paul
// Bourke, "Intersection of two circles," 1997) starts from. This answers
// ONLY whether the two INFINITE cylinders' walls interact radially -
// whether the two FINITE axial ranges actually overlap is a separate
// question, deliberately left to the existing generic classifier
// (ClassifyPointVsMixedSolid/RayVsMixedFace), exactly the same "infinite
// vs finite reach" split of responsibility CylinderPlaneNoInteraction
// above already establishes for the plane/cylinder case.
bool CylinderCylinderNoInteraction(const Brep::CylindricalFace& cf_a, const Brep::CylindricalFace& cf_b, double tol) {
  const Vector3d d = cf_b.frame.origin - cf_a.frame.origin;
  const double bx = ON_DotProduct(d, cf_a.frame.xaxis);
  const double by = ON_DotProduct(d, cf_a.frame.yaxis);
  const double dist = std::sqrt(bx * bx + by * by);
  const double r_a = cf_a.radius, r_b = cf_b.radius;
  return (dist > r_a + r_b + tol) || (dist < std::fabs(r_a - r_b) - tol);
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
// Wraps an already-built CylindricalFace as a MixedFace - a one-line
// convenience factored out for SplitCylindricalByParallelCylinder below
// (which builds several of these), matching the 3-line pattern several
// existing call sites in this file already repeat inline.
MixedFace MixedFaceFromCyl(Brep::CylindricalFace cf) {
  MixedFace m;
  m.is_cyl = true;
  m.cyl = std::move(cf);
  return m;
}

// Splits a FULL-SWEEP (angle == 2*pi) PARALLEL-axis cylindrical fragment
// `cf` against another PARALLEL-axis cylinder `other`, by the closed-form
// circle/circle intersection of their two cross-sectional circles
// (CylinderCylinderNoInteraction's own doc comment above derives the same
// projection this reuses): with both axes parallel to a shared direction,
// every point on either axis line projects to the SAME 2D point in the
// plane perpendicular to that direction regardless of which point along
// the (infinite) axis is chosen, so this reduces exactly to ordinary 2D
// circle/circle intersection of a circle of radius `cf.radius` at the
// origin (in `cf`'s own (xaxis, yaxis) basis) and a circle of radius
// `other.radius` at the projection of `other.frame.origin`, distance
// `dist` apart - the standard closed form (Weisstein, MathWorld,
// "Circle-Circle Intersection"; Paul Bourke, "Intersection of two
// circles," 1997):
//
//   a = (dist^2 + r_a^2 - r_b^2) / (2*dist)
//   h = sqrt(r_a^2 - a^2)                    (real iff the circles cross)
//   midpoint = a * (center_b / dist)
//   perp = rot90(center_b) / dist
//   P1 = midpoint + h*perp,  P2 = midpoint - h*perp
//
// Three closed-form-checkable regimes (see CylinderCylinderNoInteraction's
// own doc comment for the first two):
//   - no interaction, disjoint circles: `cf` is returned completely
//     unmodified - the existing, untouched ClassifyPointVsMixedSolid/
//     RayVsMixedFace machinery already classifies it correctly as wholly
//     kIn or wholly kOut without any split.
//   - no interaction, one circle fully nested inside the other: same
//     unmodified pass-through - `cf`'s own wall sits at a CONSTANT radial
//     distance from `other`'s own axis for every angle (either always
//     inside `other`'s radius or always outside it), so no angular split
//     is needed even though the two solids DO interact volumetrically.
//   - exactly 2 crossings: `cf` genuinely splits into two angular
//     children at the two circle/circle intersection angles, both pushed
//     onto the worklist (mirroring case (i)'s planar/planar split and case
//     (iii)'s height split, which likewise produce BOTH children and let
//     the existing generic classifier decide which survives) - NEITHER
//     child is privileged as "the kept one" here.
//
// Restricted to a FULL-SWEEP `cf` for the identical reason
// SplitCylindricalByObliquePlane/BuildEndCap already restrict themselves
// to one: no producer in this pipeline ever builds a partial-sweep
// cylindrical boolean operand today, so a genuinely partial-sweep x
// partial-sweep interaction is an unverified shape this increment declines
// to guess at.
//
// Needs NO new CylindricalFace fields and touches NONE of
// ClassifyPointVsMixedSolid/RayVsMixedFace/RepresentativeInteriorPointMixed/
// BooleanCombineMixed's own switch statement: every one of those already
// treats a CylindricalFace's own (angle, height) trim rectangle fully
// generically, with no special-casing of WHY a fragment has the angle it
// has (an oblique cut, a height split, or - now - an angular split). This
// function's only job is to produce two syntactically valid CylindricalFace
// angular children; the rest of the pipeline is unmodified and correct by
// the same argument every other case here already relies on.
// The result of the closed-form circle/circle crossing computation shared
// between SplitCylindricalByParallelCylinder's own angular wall split
// (below) and BuildLensEndCap's own lens-shaped Intersection/Difference
// end cap (further below) - factored out into its own function so BOTH
// call sites consume the EXACT SAME computed intersection points/angles
// rather than two independently re-derived (and potentially numerically
// drifting) copies of the same math. This is a pure extraction of
// SplitCylindricalByParallelCylinder's own pre-existing inline computation
// (see that function's own doc comment for the closed-form derivation
// itself, Weisstein/Bourke) - not new geometry.
struct ParallelCylinderCrossing {
  bool crosses = false;  // false: disjoint, tangent, or one nested inside the other - no lens/split exists
  // The two circle/circle intersection points, in 3D, evaluated AT `cf`'s
  // own frame.origin height (an arbitrary reference height - the
  // projected 2D geometry is identical at every height along the shared
  // axis direction, see SplitCylindricalByParallelCylinder's own doc
  // comment for why). A caller needing these at a DIFFERENT height (e.g.
  // a specific end cap's own height) shifts by `height * cf.frame.zaxis`.
  Point3d p1, p2;
  // The true angle (radians, in `cf`'s own frame) of p1 and p2
  // respectively - deliberately NOT sorted or wrap-adjusted, so a caller
  // needing floating-point IDENTITY with a fragment built from these same
  // raw values elsewhere (e.g. SplitCylindricalByParallelCylinder's own
  // rail corners, or BuildLensEndCap's own cap boundary) can reuse them
  // directly, unmodified - see BuildLensEndCap's own doc comment for why
  // that identity matters there.
  double theta_p1_cf = 0.0, theta_p2_cf = 0.0;
  // Mirror of the above, in `other`'s own frame instead (computed via the
  // SAME p1/p2 points - the angle-only projection onto a frame's own
  // (xaxis, yaxis) is invariant to which height along the shared axis
  // direction p1/p2 happen to be stored at, since xaxis/yaxis are both
  // perpendicular to that shared axis).
  double theta_p1_other = 0.0, theta_p2_other = 0.0;
};

ParallelCylinderCrossing ComputeParallelCylinderCrossing(const Brep::CylindricalFace& cf,
                                                          const Brep::CylindricalFace& other, double tol) {
  ParallelCylinderCrossing result;

  // 2D projection onto cf's own (xaxis, yaxis) - see this function's own
  // doc comment above and CylinderCylinderNoInteraction's for why this is
  // valid for any point along either infinite parallel axis line.
  const Vector3d d = other.frame.origin - cf.frame.origin;
  const double center_b_x = ON_DotProduct(d, cf.frame.xaxis);
  const double center_b_y = ON_DotProduct(d, cf.frame.yaxis);
  const double dist = std::sqrt(center_b_x * center_b_x + center_b_y * center_b_y);
  const double r_a = cf.radius, r_b = other.radius;
  if (dist <= tol) return result;  // concentric (or coincident) axes - not a genuine 2-point crossing

  const double a = (dist * dist + r_a * r_a - r_b * r_b) / (2.0 * dist);
  const double h2 = r_a * r_a - a * a;
  if (h2 <= tol * tol) return result;  // tangent or disjoint - see this function's own callers for what each does with `crosses == false`
  const double h = std::sqrt(h2);
  const double mid_x = a * center_b_x / dist, mid_y = a * center_b_y / dist;
  const double perp_x = -center_b_y / dist, perp_y = center_b_x / dist;
  const double p1_x = mid_x + h * perp_x, p1_y = mid_y + h * perp_y;
  const double p2_x = mid_x - h * perp_x, p2_y = mid_y - h * perp_y;

  result.p1 = cf.frame.origin + p1_x * cf.frame.xaxis + p1_y * cf.frame.yaxis;
  result.p2 = cf.frame.origin + p2_x * cf.frame.xaxis + p2_y * cf.frame.yaxis;
  result.theta_p1_cf = std::atan2(p1_y, p1_x);
  result.theta_p2_cf = std::atan2(p2_y, p2_x);
  if (result.theta_p1_cf < 0.0) result.theta_p1_cf += 2.0 * ON_PI;
  if (result.theta_p2_cf < 0.0) result.theta_p2_cf += 2.0 * ON_PI;

  auto angle_in_other = [&](const Point3d& p) {
    const Vector3d rel = p - other.frame.origin;
    double ang = std::atan2(ON_DotProduct(rel, other.frame.yaxis), ON_DotProduct(rel, other.frame.xaxis));
    if (ang < 0.0) ang += 2.0 * ON_PI;
    return ang;
  };
  result.theta_p1_other = angle_in_other(result.p1);
  result.theta_p2_other = angle_in_other(result.p2);

  result.crosses = true;
  return result;
}

std::vector<MixedFace> SplitCylindricalByParallelCylinder(const Brep::CylindricalFace& cf,
                                                            const Brep::CylindricalFace& other, double tol) {
  if (!(cf.angle >= 2.0 * ON_PI - kAxisAlignTol)) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: a parallel-axis cylinder/"
        "cylinder interaction against a PARTIAL-sweep (angle < 2*pi) "
        "cylindrical fragment is out of scope for this increment - see "
        "SplitCylindricalByParallelCylinder's own doc comment in "
        "boolean.cpp");
  }

  if (CylinderCylinderNoInteraction(cf, other, tol)) {
    return {MixedFaceFromCyl(cf)};  // disjoint, or one fully nested inside the other - no split needed
  }

  // Same closed-form circle/circle crossing math this function always
  // used, now factored into ComputeParallelCylinderCrossing (above) so
  // BuildLensEndCap's own lens cap (further below) shares this EXACT
  // computation rather than an independent copy.
  const ParallelCylinderCrossing crossing = ComputeParallelCylinderCrossing(cf, other, tol);
  if (!crossing.crosses) {
    // Exact/near tangency - a genuine degeneracy this increment excludes
    // (the boundary between "0 crossings" and "2 crossings" regimes is
    // not a closed-form-clean case to split on): thrown rather than
    // silently routed into either branch.
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: two parallel cylinders are "
        "exactly (or near-exactly) tangent - out of scope for this "
        "increment, see SplitCylindricalByParallelCylinder's own doc "
        "comment in boolean.cpp");
  }

  double theta1 = crossing.theta_p1_cf;
  double theta2 = crossing.theta_p2_cf;
  if (theta2 < theta1) std::swap(theta1, theta2);  // theta1 < theta2, both in [0, 2*pi)

  // Two angular children: [theta1, theta2] and [theta2, theta1 + 2*pi] -
  // exactly the "produce both, let the existing generic classifier decide"
  // pattern case (i)'s planar/planar split and case (iii)'s height split
  // already use.
  auto make_child = [&](double begin, double sweep) {
    Brep::CylindricalFace child = cf;
    child.angle = sweep;
    // Rotate (xaxis, yaxis) about zaxis by `begin` so the new angle=0 rail
    // sits at cf's own physical angle `begin` - elementary in-plane
    // rotation of an orthonormal pair about a shared axis, preserving
    // right-handedness with zaxis unchanged. `end0_is_original`/
    // `end1_is_original` are inherited unchanged via `child = cf` above -
    // an angular split touches neither end's true axial terminus, so
    // BooleanCombineMixed's own end-cap synthesis bookkeeping needs no new
    // field for this (see that struct's own doc comment in brep.h).
    const double cb = std::cos(begin), sb = std::sin(begin);
    child.frame.xaxis = cb * cf.frame.xaxis + sb * cf.frame.yaxis;
    child.frame.yaxis = -sb * cf.frame.xaxis + cb * cf.frame.yaxis;
    child.frame.UpdateEquation();
    return child;
  };

  std::vector<MixedFace> out;
  out.push_back(MixedFaceFromCyl(make_child(theta1, theta2 - theta1)));
  out.push_back(MixedFaceFromCyl(make_child(theta2, 2.0 * ON_PI - (theta2 - theta1))));
  return out;
}

// True iff a synthesized end cap for `cf`'s own end (`at_v0` selects which
// one) needs no trimming against a DIFFERENT, interacting, PARALLEL-axis
// cylinder `other` - i.e. `other`'s own finite axial range does not reach
// that end's height AT ALL, checked by converting the cap's own axial
// position into `other`'s own native height coordinate (one dot product,
// since both axes are parallel) and testing it against `other`'s own
// [0, other.length] range.
//
// This is deliberately CONSERVATIVE, not a full trim-need analysis: a
// synthesized end cap is always a FULL 0-to-radius pie slice (BuildEndCap's
// own doc comment), so even for the angular wedge this increment's own
// split keeps as "outside other", the disc's own near-center region can
// still dip into `other`'s own footprint whenever `other`'s axial range
// reaches that height at all (confirmed directly: for two substantially
// overlapping circles, at least one axis typically lies inside the OTHER
// circle, and the disc's own radial sweep at a "kept" angle is not
// monotonic away from that axis - a genuine correctness risk, not a
// theoretical worry) - so this function refuses (returns false, and the
// caller throws rather than emit a possibly-wrong cap) whenever `other`'s
// axial range reaches the cap's own height, regardless of whether the
// dip actually occurs at every angle. Extending this to a real per-angle
// trim (reusing detail::ClipPolygonByCircle3d on the already-built cap
// polygon, exactly case (ii)'s own machinery) is real, tractable, closed-
// form work but is its own follow-up increment (see boolean.h's own
// BooleanCombineMixed doc comment) - not attempted here.
bool ParallelCylinderCapNeedsNoTrim(const Brep::CylindricalFace& cf, bool at_v0,
                                     const Brep::CylindricalFace& other, double tol) {
  const double height = at_v0 ? 0.0 : cf.length;
  const Point3d cap_point = cf.frame.origin + height * cf.frame.zaxis;
  const double other_h = ON_DotProduct(cap_point - other.frame.origin, other.frame.zaxis);
  return other_h < -tol || other_h > other.length + tol;
}

// Splits `cf` axially wherever a DIFFERENT, interacting, PARALLEL-axis
// cylinder `other`'s own two axial termini (height 0 and height
// `other.length`, in `other`'s own frame) fall STRICTLY inside `cf`'s own
// [0, cf.length] range - the cylinder/cylinder analogue of case (iii)'s
// own `v_cut` height-split against an explicit planar face's z=const
// plane (SplitMixedAgainstAllFaces, below), needed for a genuinely
// DIFFERENT reason than that split's own trigger: a bare CylindricalFace
// boolean operand's own finite axial extent has no PlanarFace ANYWHERE in
// this pipeline to split against at all (see BooleanCombineMixed's own
// doc comment on bare CylindricalFace operands - `other`'s own two ends
// are never represented as explicit faces the way case (iii)'s plane is).
// Without this, RepresentativeInteriorPointMixed's own SINGLE
// representative point has to stand in for `cf`'s ENTIRE axial range when
// SplitAndClassifyMixed classifies a fragment against `other` - which is
// simply wrong whenever `other`'s own bounded axial reach ends partway
// through `cf`'s own length, since `cf`'s TRUE in/out classification
// against `other` then genuinely differs above vs below that height and a
// single sample cannot see that.
//
// Confirmed directly, not merely theorized: before this function existed,
// BooleanCombineMixed(a, b, Intersection) on two genuinely CROSSING
// parallel cylinders with PARTIALLY overlapping (neither disjoint nor one
// fully containing the other's) axial ranges measured a kept wall
// fragment spanning `cf`'s own FULL original length when the true axial
// overlap band was strictly shorter - see
// TestBooleanCombineMixedParallelCylinderIntersectionCrossingLensCaps' own
// comment in the test file for the exact numbers this fix makes correct.
// This is a genuinely NEW gap this increment's own research phase did not
// anticipate (it assumed the existing wall-split machinery was already
// fully correct for the wall) - found and fixed here by direct
// counterexample during implementation, not by following any prior spec.
//
// Applies the SAME "lo gets end1_is_original=false, hi gets
// end0_is_original=false" bookkeeping case (iii) already uses just below
// - a fresh cut here is never a genuine unmet terminus of the original
// input cylinder either. Not gated on BooleanOp, for the same reason
// nothing else in SplitMixedAgainstAllFaces is (see that function's own
// doc comment): this is purely a refinement of the geometry available to
// the SHARED classify-then-bucket step downstream, not a new
// operator-specific mechanism.
//
// Deliberately NOT called unconditionally on every parallel-cylinder pair
// (see the case (iv) dispatch below for exactly when it's invoked): doing
// so for a cylinder that is NEVER radially inside `other` at any angle
// (e.g. the OUTER member of a nested pair) would add classification-inert
// internal seams and needlessly change that cylinder's own fragment
// count - confirmed directly to matter, not just tidiness: applying this
// unconditionally broke TestBooleanCombineMixedParallelCylinderUnionOneFullyNestedContributesNothing's
// own "A survives as exactly ONE cylindrical face" assertion during this
// function's own development, by splitting A (the untouched OUTER
// cylinder, whose own wall is never inside the smaller nested B at any
// height) into 3 axially-inert pieces for no classification benefit.
std::vector<Brep::CylindricalFace> SplitCylindricalByOtherCylinderAxialExtent(
    const Brep::CylindricalFace& cf, const Brep::CylindricalFace& other, double tol) {
  std::vector<Brep::CylindricalFace> pieces{cf};
  for (const double other_local_h : {0.0, other.length}) {
    const Point3d cut_point = other.frame.origin + other_local_h * other.frame.zaxis;
    std::vector<Brep::CylindricalFace> next;
    next.reserve(pieces.size() + 1);
    for (const Brep::CylindricalFace& piece : pieces) {
      const double v_cut = ON_DotProduct(cut_point - piece.frame.origin, piece.frame.zaxis);
      if (v_cut > tol && v_cut < piece.length - tol) {
        Brep::CylindricalFace lo = piece;
        lo.length = v_cut;
        lo.end1_is_original = false;
        Brep::CylindricalFace hi = piece;
        hi.frame.origin = piece.frame.origin + v_cut * piece.frame.zaxis;
        hi.length = piece.length - v_cut;
        hi.end0_is_original = false;
        next.push_back(std::move(lo));
        next.push_back(std::move(hi));
      } else {
        next.push_back(piece);
      }
    }
    pieces = std::move(next);
  }
  return pieces;
}

// ---------------------------------------------------------------------
// Steinmetz (equal-radius, intersecting-axes) cylinder/cylinder split
// ---------------------------------------------------------------------
//
// Two equal-radius cylinders whose axes meet at a point Q at an angle
// alpha in (0, pi) - the classical Steinmetz/bicylinder configuration -
// have an intersection curve that factors, in closed form, into two
// PLANAR ELLIPSES (a classical, public-domain fact): subtracting the two
// implicit cylinder equations
//     |p-Q|^2 - ((p-Q).a)^2 = r^2   and   |p-Q|^2 - ((p-Q).b)^2 = r^2
// leaves ((p-Q).a)^2 = ((p-Q).b)^2, i.e. (p-Q).(a-b) = 0 or (p-Q).(a+b) = 0
// - the two planes E1 (normal a-b) and E2 (normal a+b) through Q. Each
// plane cuts each cylinder in an ellipse (detail::ComputeEllipseFrame3d's
// own closed form, ellipse_clip3d.h), and on either plane a point sits at
// the SAME distance from both axes, so E1's ellipse on A's wall and E1's
// ellipse on B's wall are one and the same 3D curve (likewise E2).
//
// On cylinder A's own (theta, h) wall chart - h measured from Q along
// a = A's zaxis, theta from A's xaxis, phi0 = the angle of the in-plane
// component of b - the two ellipses are the cosine curves
//     h1(theta) = +r*cot(alpha/2)*cos(theta - phi0)      (plane E1)
//     h2(theta) = -r*tan(alpha/2)*cos(theta - phi0)      (plane E2)
// and a wall point is inside B iff h lies strictly BETWEEN h1 and h2: the
// quadratic (h*cos(alpha) + r*sin(alpha)*cos(theta-phi0))^2 - h^2 has
// exactly those two roots in h and is positive between them. The two
// curves cross where cos(theta - phi0) = 0 - at the two PINCH points
// Q +/- r*n, n = (a x b)/|a x b|, which lie on both walls. So the part of
// A's wall inside B is NOT an isolated hole punched into the wall's
// interior: it is two "eyes" (one per angular half, [phi0-pi/2, phi0+pi/2]
// and [phi0+pi/2, phi0+3pi/2]), each bounded below by one half-ellipse
// and above by the other, touching the rest of the wall only at the two
// pinch points. Splitting the wall at the two pinch angles therefore
// turns EVERY piece into a shape CylindricalFace already represents with
// no new fields at all:
//   - four HALF-BANDS: angle = pi, one flat original end, the other end a
//     single smooth half-ellipse notch (exactly SplitCylindricalByObliquePlane's
//     notched-at-one-end shape, cap0_notch_points for an upper band /
//     cap1_notch_points for a lower one);
//   - two EYES: angle = pi, length = 0, BOTH ends notched - the lower
//     half-ellipse as cap0_notch_points, the upper one as cap1_notch_points,
//     both running between the same two pinch vertices (see
//     CylindricalFace's own doc comment in brep.h for the length == 0 rule
//     and Brep::FromMixedFaces()'s handling of the two degenerate rails).
// Every half-ellipse bounds exactly two faces in every op (Union: two
// half-bands; Intersection: two eyes; Difference: a half-band of one
// cylinder and an eye of the other), and an eye's own two caps join the
// same two vertices - which is why Brep::FromMixedFaces() tells notched
// cap edges apart by their polyline midpoint (BuildFaceLoop, brep.cpp).
//
// General alpha costs nothing structural: only the two amplitudes (cot
// vs tan of alpha/2) and phi0 change, and every fragment's notch heights
// are read off the sampled lists themselves, never from a closed-form
// amplitude.
//
// SAMPLING IDENTITY. The four half-ellipse lists are sampled ONCE, on a
// CANONICAL cylinder chosen by a deterministic, argument-order-independent
// rule (SteinmetzCanonicalFirst, below), uniformly in that cylinder's own
// angle, and the identical std::vector<Point3d> is handed to every
// fragment of BOTH cylinders bounded by that half-ellipse - detail::
// EllipseBoundarySample3d's own "one canonical producer, several
// consumers" principle. This is what makes the shared boundary bit-
// identical across the two cylinders: samples uniform in A's angle and
// samples uniform in B's angle are the same point SET but never the same
// floating-point values (sin(pi/2 - t) != cos(t) bitwise), and for
// alpha != 90 degrees the correspondence is not even uniform. Because
// SplitAndBucketMixed(fa, fb) and SplitAndBucketMixed(fb, fa) both reach
// this code with the SAME two original faces, the canonical choice - and
// hence every sampled point - is identical in both calls.
//
// EXTENT PRECONDITION (derived, not guessed): the cross-section of the
// bicylinder perpendicular to a at height h (from Q) is non-empty iff
// |h| < r*(1+|cos alpha|)/sin(alpha) = r*max(cot(alpha/2), tan(alpha/2)),
// which is also the larger of the two notch amplitudes. Requiring both
// original ends of BOTH cylinders to satisfy |h_end - h_Q| > that bound
// (+ tol) guarantees at once that both eyes are strictly interior to each
// wall, that neither cylinder's end disc touches the other cylinder, and
// that SynthesizeEndCaps' on-axis probe just past every original end
// reads an unambiguous PointClass::kOut (a point on A's axis at height h
// is inside B iff |h| < r/sin(alpha), which the bound exceeds). A
// configuration violating it - e.g. a cylinder that STARTS at the
// crossing - is refused (thrown) rather than split into fragments whose
// eyes would run into an end disc this pipeline has no face for. Small
// alpha (cot large) and alpha near pi (tan large) are refused by this
// same check; alpha within kAxisAlignTol of 0 or pi never reaches here
// (case (iv)'s own parallel-axis branch takes it first), and neither
// does a pair whose finite cylinders provably never meet at all - the
// same bound, read the other way round, is the no-interaction test
// case (iv) consults first (NonParallelCylinderPairNoInteraction, below).

constexpr int kSteinmetzSamples = 200;  // segments per half-ellipse; EVEN, so index N/2 is the mid-angle sample

struct SteinmetzCrossing {
  Point3d q;                   // the axes' crossing point, ON the canonical cylinder's own axis
  double alpha = 0.0;          // angle between the two axis directions, in (0, pi)
  double amplitude_max = 0.0;  // r * max(cot(alpha/2), tan(alpha/2)) - see the extent precondition above
  Point3d pinch[2];            // Q +/- r*n: the two points every half-ellipse starts or ends at
  // E1 over [phi0-pi/2, phi0+pi/2], E1 over [phi0+pi/2, phi0+3pi/2], then
  // E2 over the same two ranges - each list in increasing CANONICAL angle,
  // kSteinmetzSamples+1 points, first and last points at the two pinches.
  std::array<std::vector<Point3d>, 4> half_ellipses;
  std::array<double, 4> sagitta{};  // per list, the same chord-midpoint bound SplitCylindricalByObliquePlane computes
};

// Deterministic, argument-order-independent choice of which of the two
// cylinders the shared half-ellipses are sampled on: lexicographic on
// (zaxis, origin). Two cylinders reaching the Steinmetz split always have
// different axis directions (a parallel pair takes case (iv)'s other
// branch), so this never needs to break a tie.
bool SteinmetzCanonicalFirst(const Brep::CylindricalFace& p, const Brep::CylindricalFace& q) {
  const double kp[6] = {p.frame.zaxis.x, p.frame.zaxis.y, p.frame.zaxis.z,
                        p.frame.origin.x, p.frame.origin.y, p.frame.origin.z};
  const double kq[6] = {q.frame.zaxis.x, q.frame.zaxis.y, q.frame.zaxis.z,
                        q.frame.origin.x, q.frame.origin.y, q.frame.origin.z};
  for (int i = 0; i < 6; ++i) {
    if (kp[i] < kq[i]) return true;
    if (kp[i] > kq[i]) return false;
  }
  return true;
}

// The radius tolerance every non-parallel cylinder/cylinder decision in
// case (iv) shares - the no-interaction band test, the Steinmetz split
// and the unequal-radius split (below) must all agree on whether a pair
// has "equal" radii, so it is computed in exactly one place.
double CylinderPairRadiusTolerance(const Brep::CylindricalFace& p, const Brep::CylindricalFace& q, double tol) {
  return std::max(tol, 1e-9 * std::max(p.radius, q.radius));
}

// Throws std::invalid_argument (every message naming "non-parallel axes",
// the substring the existing dispatch-boundary test keys on) for genuinely
// skew axes, or a crossing that is not strictly interior to both cylinders
// per the extent precondition above. Unequal radii never reach here (case
// (iv)'s dispatch sends them to SplitCylindricalByUnequalCylinder,
// below), so that guard is a bug check, not a scope refusal.
SteinmetzCrossing ComputeSteinmetzCrossing(const Brep::CylindricalFace& self, const Brep::CylindricalFace& other,
                                           double tol) {
  if (std::fabs(self.radius - other.radius) > CylinderPairRadiusTolerance(self, other, tol)) {
    throw std::runtime_error(
        "dino8::kernel::BooleanCombineMixed: the Steinmetz (equal-radius) split "
        "was reached with UNEQUAL radii - case (iv)'s own dispatch routes those "
        "to the unequal-radius split, so this is a bug; please report it");
  }

  const bool self_first = SteinmetzCanonicalFirst(self, other);
  const Brep::CylindricalFace& canon = self_first ? self : other;
  const Brep::CylindricalFace& partner = self_first ? other : self;
  const Vector3d a = canon.frame.zaxis;
  const Vector3d b = partner.frame.zaxis;

  // Closest points of the two (infinite) axis lines - the standard
  // closed form for two lines O_a + s*a, O_b + t*b with unit directions.
  const Vector3d w = canon.frame.origin - partner.frame.origin;
  const double d_ab = ON_DotProduct(a, b);
  const double d_aw = ON_DotProduct(a, w);
  const double d_bw = ON_DotProduct(b, w);
  const double denom = 1.0 - d_ab * d_ab;  // sin^2(alpha), bounded away from 0 by the dispatch's own parallel test
  const double s = (d_ab * d_bw - d_aw) / denom;
  const double t = (d_bw - d_ab * d_aw) / denom;
  const Point3d on_canon = canon.frame.origin + s * a;
  const Point3d on_partner = partner.frame.origin + t * b;
  if (on_canon.DistanceTo(on_partner) > tol) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: two cylindrical faces interacting "
        "with non-parallel axes that do not INTERSECT (genuinely skew axes) is "
        "out of scope - only the Steinmetz equal-radius, intersecting-axes case "
        "is supported (the general skew case needs a genuine NURBS-NURBS "
        "surface intersection) - see this function's own doc comment in "
        "boolean.h");
  }

  SteinmetzCrossing crossing;
  crossing.q = on_canon;
  crossing.alpha = std::acos(std::max(-1.0, std::min(1.0, d_ab)));
  const double half = 0.5 * crossing.alpha;
  crossing.amplitude_max = canon.radius * std::max(1.0 / std::tan(half), std::tan(half));

  for (const Brep::CylindricalFace* c : {&self, &other}) {
    const double h_q = ON_DotProduct(crossing.q - c->frame.origin, c->frame.zaxis);
    if (!(h_q > crossing.amplitude_max + tol && c->length - h_q > crossing.amplitude_max + tol)) {
      throw std::invalid_argument(
          "dino8::kernel::BooleanCombineMixed: two equal-radius cylindrical "
          "faces with intersecting non-parallel axes (the Steinmetz "
          "configuration) are supported only when the crossing is STRICTLY "
          "interior to both cylinders - every original end must sit more than "
          "r*max(cot(alpha/2), tan(alpha/2)) from the crossing along its own "
          "axis, so both eye-shaped intersection regions lie wholly inside "
          "each wall and neither end disc touches the other cylinder - see "
          "this function's own doc comment in boolean.h");
    }
  }

  // b's in-plane component perpendicular to a, and its angle phi0 in the
  // canonical cylinder's own (xaxis, yaxis) basis - the mid-angle of the
  // first angular half.
  Vector3d u = b - d_ab * a;
  u.Unitize();
  const double phi0 = std::atan2(ON_DotProduct(u, canon.frame.yaxis), ON_DotProduct(u, canon.frame.xaxis));

  // The two cutting planes through Q. Their normals' signs are irrelevant
  // to detail::ComputeEllipseFrame3d (base and C flip together), and the
  // grazing guard is provably clear: |a.n1| = sin(alpha/2), |a.n2| =
  // cos(alpha/2), both far above kMinObliqueC for any alpha the extent
  // precondition above admits.
  const ON_Plane plane1(crossing.q, a - b);
  const ON_Plane plane2(crossing.q, a + b);
  const detail::EllipseFrame3d ef1 = detail::ComputeEllipseFrame3d(canon, plane1, kMinObliqueC);
  const detail::EllipseFrame3d ef2 = detail::ComputeEllipseFrame3d(canon, plane2, kMinObliqueC);

  const double phi_lo = phi0 - 0.5 * ON_PI;
  const double phi_mid = phi0 + 0.5 * ON_PI;
  const double phi_hi = phi0 + 1.5 * ON_PI;
  const detail::EllipseFrame3d* frames[4] = {&ef1, &ef1, &ef2, &ef2};
  const double from[4] = {phi_lo, phi_mid, phi_lo, phi_mid};
  const double to[4] = {phi_mid, phi_hi, phi_mid, phi_hi};
  for (int i = 0; i < 4; ++i) {
    std::vector<Point3d> pts = detail::EllipseBoundarySample3d(*frames[i], from[i], to[i], kSteinmetzSamples);
    // Same sagitta-style bound SplitCylindricalByObliquePlane computes for
    // its own notch: max over every segment of the distance between the
    // chord's midpoint and the true curve's point at the midpoint angle.
    double max_sagitta = 0.0;
    const int n = static_cast<int>(pts.size()) - 1;
    for (int k = 0; k < n; ++k) {
      const double phi_seg_mid = from[i] + (to[i] - from[i]) * (static_cast<double>(k) + 0.5) / n;
      const Point3d chord_mid = 0.5 * (pts[static_cast<size_t>(k)] + pts[static_cast<size_t>(k) + 1]);
      max_sagitta = std::max(max_sagitta, chord_mid.DistanceTo(detail::EllipsePointAt(*frames[i], phi_seg_mid)));
    }
    crossing.half_ellipses[static_cast<size_t>(i)] = std::move(pts);
    crossing.sagitta[static_cast<size_t>(i)] = max_sagitta;
  }
  crossing.pinch[0] = crossing.half_ellipses[0].front();
  crossing.pinch[1] = crossing.half_ellipses[0].back();
  return crossing;
}

// Splits a FULL-SWEEP, un-notched cylindrical fragment `cf` against an
// equal-radius, intersecting-axis cylinder `other` into the six fragments
// derived in the section comment above (per angular half: an upper
// half-band, a lower half-band, and the eye between them), all six pushed
// onto the worklist for the existing generic classifier to keep or
// discard - the same "produce every piece, never privilege one" pattern
// every other split in this file follows. Which of the four canonical
// half-ellipse lists bounds which fragment of THIS cylinder is decided
// from the lists themselves (which pinch each starts at, which angular
// half its mid-angle sample falls in, whether that sample sits above or
// below the crossing height), so the non-canonical cylinder needs no
// separate sign bookkeeping and no closed-form amplitude is ever assumed;
// a list is reversed where this cylinder's own increasing local angle
// runs opposite to the canonical one's.
std::vector<MixedFace> SplitCylindricalBySteinmetzCylinder(const Brep::CylindricalFace& cf,
                                                            const Brep::CylindricalFace& other, double tol) {
  for (const Brep::CylindricalFace* c : {&cf, &other}) {
    if (!(c->angle >= 2.0 * ON_PI - kAxisAlignTol)) {
      throw std::invalid_argument(
          "dino8::kernel::BooleanCombineMixed: a Steinmetz (equal-radius, "
          "intersecting-axes) cylinder/cylinder interaction involving a "
          "PARTIAL-sweep (angle < 2*pi) cylindrical fragment is out of scope "
          "- see SplitCylindricalBySteinmetzCylinder's own doc comment in "
          "boolean.cpp");
    }
  }
  if (!cf.cap0_notch_points.empty() || !cf.cap1_notch_points.empty()) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: a Steinmetz (equal-radius, "
        "intersecting-axes) cylinder/cylinder interaction against a "
        "cylindrical fragment that is ALREADY notched at an end is out of "
        "scope - see SplitCylindricalBySteinmetzCylinder's own doc comment in "
        "boolean.cpp");
  }

  const SteinmetzCrossing crossing = ComputeSteinmetzCrossing(cf, other, tol);

  const double h_q = ON_DotProduct(crossing.q - cf.frame.origin, cf.frame.zaxis);
  const Point3d origin_q = cf.frame.origin + h_q * cf.frame.zaxis;  // Q projected onto cf's own axis
  auto local_angle = [&](const Point3d& p) {
    const Vector3d d = p - origin_q;
    double ang = std::atan2(ON_DotProduct(d, cf.frame.yaxis), ON_DotProduct(d, cf.frame.xaxis));
    if (ang < 0.0) ang += 2.0 * ON_PI;
    return ang;
  };
  const double theta_pinch[2] = {local_angle(crossing.pinch[0]), local_angle(crossing.pinch[1])};

  struct HalfCurves {
    std::vector<Point3d> top, bottom;  // in increasing local angle of this half, pinch to pinch
    double top_sagitta = 0.0, bottom_sagitta = 0.0;
    int top_count = 0, bottom_count = 0;
  };
  HalfCurves halves[2];  // halves[i] starts (local angle 0) at pinch i
  const size_t mid_index = kSteinmetzSamples / 2;
  for (size_t i = 0; i < 4; ++i) {
    const std::vector<Point3d>& pts = crossing.half_ellipses[i];
    const int start = pts.front().DistanceTo(crossing.pinch[0]) <= pts.front().DistanceTo(crossing.pinch[1]) ? 0 : 1;
    double d = local_angle(pts[mid_index]) - theta_pinch[start];
    if (d < 0.0) d += 2.0 * ON_PI;
    int half = start;
    std::vector<Point3d> oriented = pts;
    if (d > ON_PI) {
      // This cylinder's increasing local angle runs the other way along
      // this curve: it lives in the half starting at the OTHER pinch.
      half = 1 - start;
      std::reverse(oriented.begin(), oriented.end());
    }
    const double h_mid = ON_DotProduct(pts[mid_index] - origin_q, cf.frame.zaxis);
    HalfCurves& hc = halves[half];
    if (h_mid > 0.0) {
      hc.top = std::move(oriented);
      hc.top_sagitta = crossing.sagitta[i];
      ++hc.top_count;
    } else {
      hc.bottom = std::move(oriented);
      hc.bottom_sagitta = crossing.sagitta[i];
      ++hc.bottom_count;
    }
  }
  for (const HalfCurves& hc : halves) {
    if (hc.top_count != 1 || hc.bottom_count != 1) {
      throw std::runtime_error(
          "dino8::kernel::BooleanCombineMixed: the Steinmetz half-ellipse "
          "bookkeeping did not assign exactly one upper and one lower curve to "
          "each angular half - please report this as a bug");
    }
  }

  // Same elementary in-plane rotation SplitCylindricalByParallelCylinder's
  // own make_child uses: rotate (xaxis, yaxis) about zaxis by `begin` so
  // local angle 0 sits at cf's own physical angle `begin`.
  auto rotated_half = [&](double begin) {
    Brep::CylindricalFace child = cf;
    child.angle = ON_PI;
    const double cb = std::cos(begin), sb = std::sin(begin);
    child.frame.xaxis = cb * cf.frame.xaxis + sb * cf.frame.yaxis;
    child.frame.yaxis = -sb * cf.frame.xaxis + cb * cf.frame.yaxis;
    child.frame.UpdateEquation();
    return child;
  };

  std::vector<MixedFace> out;
  out.reserve(6);
  for (int half = 0; half < 2; ++half) {
    const HalfCurves& hc = halves[half];
    const Brep::CylindricalFace base = rotated_half(theta_pinch[half]);

    // Upper half-band: from the crossing height up to cf's own original
    // top, notched below by this half's upper curve (whose endpoints are
    // the two pinches, at local angles 0 and pi, at v=0 - exactly the rail
    // corners CylindricalFace's own notch contract requires).
    Brep::CylindricalFace upper = base;
    upper.frame.origin = origin_q;
    upper.frame.UpdateEquation();
    upper.length = cf.length - h_q;
    upper.cap0_notch_points = hc.top;
    upper.cap0_notch_tolerance = hc.top_sagitta;
    upper.end0_is_original = false;  // v=0 is the fresh cut; v=length inherited from cf

    // Lower half-band: from cf's own original bottom up to the crossing
    // height, notched above by this half's lower curve (endpoints at
    // v=length).
    Brep::CylindricalFace lower = base;
    lower.length = h_q;
    lower.cap1_notch_points = hc.bottom;
    lower.cap1_notch_tolerance = hc.bottom_sagitta;
    lower.end1_is_original = false;

    // Eye: the region between the two curves - length 0, both caps
    // notched, no original end at all.
    Brep::CylindricalFace eye = base;
    eye.frame.origin = origin_q;
    eye.frame.UpdateEquation();
    eye.length = 0.0;
    eye.cap0_notch_points = hc.bottom;
    eye.cap0_notch_tolerance = hc.bottom_sagitta;
    eye.cap1_notch_points = hc.top;
    eye.cap1_notch_tolerance = hc.top_sagitta;
    eye.end0_is_original = false;
    eye.end1_is_original = false;

    out.push_back(MixedFaceFromCyl(std::move(upper)));
    out.push_back(MixedFaceFromCyl(std::move(lower)));
    out.push_back(MixedFaceFromCyl(std::move(eye)));
  }
  return out;
}

// ---------------------------------------------------------------------
// Unequal-radius, intersecting-axis cylinder/cylinder split (any angle)
// ---------------------------------------------------------------------
//
// Two cylinders A (radius r_a) and B (radius r_b < r_a) whose axes meet
// at a point Q at an angle alpha in (0, pi). Unlike the equal-radius
// Steinmetz case the intersection curve does not factor into planar
// ellipses (it is a genuine space quartic), but it still has a per-angle
// CLOSED FORM on the SMALLER cylinder's wall, in this file's own
// P + h*z + r*(cos*x + sin*y) convention (PointOnCylFace): with
// X(theta, h) = P_b + h*b + e(theta), e(theta) = r_b*(cos(theta)*x_b +
// sin(theta)*y_b), M*v = v - (v.a)*a the projector onto the plane
// perpendicular to A's axis a (so |M*(X - P_a)| is X's distance from A's
// axis) and D = P_b - P_a, "X lies on A's wall" is
// |M*(D + e(theta)) + h*M*b|^2 = r_a^2 - a QUADRATIC in h at every theta:
//     A2*h^2 + A1(theta)*h + A0(theta) = 0
//     A2        = |M*b|^2 = 1 - (a.b)^2 = sin^2(alpha)     (constant > 0)
//     A1(theta) = 2*(M*b).(M*(D + e(theta)))
//     A0(theta) = |M*(D + e(theta))|^2 - r_a^2
//     h(theta)  = (-A1 +/- sqrt(A1^2 - 4*A2*A0)) / (2*A2).
// For intersecting axes (D perpendicular to both) the discriminant is
// 4*A2*(r_a^2 - r_b^2*cos^2(theta - phi)) >= 4*A2*(r_a^2 - r_b^2) > 0 at
// EVERY theta - phi being the angle, in B's own (x_b, y_b) basis, of the
// axes' common perpendicular n = (a x b)/|a x b| - so both roots exist
// on every generator of B: B pierces A completely, and on B's wall the
// curve is two closed curves each going once around B's axis (the "+"
// root, above Q, and the "-" root, below it). On A's wall the same
// points form two closed LOOPS, one around each point where B's axis
// pierces A's wall, never winding around A's axis: a point of the curve
// has common-perpendicular coordinate r_b*cos(theta - phi), so its angle
// on A satisfies r_a*sin(theta_A - phi_A) = r_b*cos(theta - phi) (phi_A
// = the angle of b's in-plane component in A's basis), whose extremes
// sin(theta_A - phi_A) = +/- r_b/r_a are attained at theta = phi and
// theta = phi + pi - the four PINCH points, B's two generators at
// common-perpendicular coordinate +/- r_b, which are tangent to A's wall
// there. Each loop spans 2*theta_m of A's angle, theta_m = asin(r_b/r_a),
// centred on phi_A (the "+" root) or phi_A + pi (the "-" root) - a span
// INDEPENDENT of alpha, since it is fixed by the common-perpendicular
// coordinate alone.
//
// HEIGHTS ON A AT A GENERAL ANGLE. In the frame with Q at the origin,
// a = z and b = (sin alpha, 0, cos alpha), a point (r_a cos t, r_a sin t,
// h) of A's wall lies on B iff (h sin alpha - r_a cos t cos alpha)^2 =
// r_b^2 - r_a^2 sin^2 t, i.e. for |sin t| <= r_b/r_a
//     h(t) = [r_a cos t cos alpha +/- sqrt(r_b^2 - r_a^2 sin^2 t)] / sin alpha
// (the "+" loop, centred t = 0; the "-" loop is its image under the
// half-turn about n: t -> t + pi, h -> -h). At the two theta-extremes
// sin t = +/- r_b/r_a the root vanishes, so BOTH pinches of a loop sit
// at the same height h_ext = cot(alpha)*sqrt(r_a^2 - r_b^2) from Q (the
// "+" loop's at +h_ext, the "-" loop's at -h_ext; h_ext changes sign
// with cos alpha, so an obtuse angle is the acute case with b -> -b and
// nothing below branches on it). Between the pinches the upper arc
// rises MONOTONICALLY from the pinch height to its maximum at the loop's
// centre and the lower arc falls monotonically to its minimum there:
// dh/dt = -sin t [r_a cot alpha +/- r_a^2 cos t / (sin alpha sqrt(r_b^2
// - r_a^2 sin^2 t))], and the bracket is at least (r_a/sin alpha)
// (r_a/r_b +/- cos alpha) > 0 for every alpha because r_a/r_b > 1 >=
// |cos alpha|. So the "+" loop spans heights r_a cot alpha +/- r_b/sin
// alpha (centred where B's axis pierces A's wall), the "-" loop the
// negatives, and at 90 degrees all four pinches are at h_Q with each
// loop at h_Q +/- r_b. The curve's axial reach is (r_a + r_b*|cos
// alpha|)/sin(alpha) along B's axis and (r_b + r_a*|cos alpha|)/sin(alpha)
// along A's, attained ON the curve at theta = phi +/- pi/2, the samples
// in the plane of the two axes.
//
// NO HOLE REPRESENTATION IS NEEDED - the observation that made the
// Steinmetz split cheap carries over: a loop on A's wall touches its own
// two theta-extreme generators only at its two pinch points, so splitting
// A's wall by the four iso-theta rails at phi_A +/- theta_m and phi_A +
// pi +/- theta_m leaves
//   - two SLABS (angle 2*theta_m, each holding one loop), each in turn an
//     UPPER piece (origin at the loop's own pinch height, cap0 notched by
//     the loop's upper arc), a LOWER piece (top at that pinch height,
//     cap1 notched by the lower arc) and a PLUG (origin at the pinch
//     height, length 0, cap0 = the lower arc, cap1 = the upper arc - the
//     Steinmetz eye shape, now with angle 2*theta_m < pi: the part of A's
//     wall inside B) - each slab anchored at ITS OWN loop's pinch height,
//     which is where its notch chains start and end (CylindricalFace's
//     rail-corner contract), and
//   - two PLAIN pieces (angle pi - 2*theta_m), each split into a lower
//     and an upper piece by ONE cut chain running from the pinch vertex
//     on its one rail (the neighbouring slab's end corner, at that loop's
//     pinch height) to the pinch vertex on its other rail (the next
//     slab's begin corner, at the other loop's pinch height), so that
//     every rail on A ends at a vertex the slab pieces' rails share. At a
//     general angle the two heights differ by 2*|h_ext| and the chain is
//     a HELIX, linear in (angle, height), sampled as a notch polyline
//     shared verbatim by the two pieces (see SLOPED CUT below); when the
//     four pinch heights are level within tol (the LEVEL FAST PATH below)
//     the chain degenerates to the flat circle at h_Q and the cut is the
//     plain cap arc, exactly as it always was;
// and B's wall, split at its two pinch generators theta = phi, phi + pi
// into two halves (angle pi), each an UPPER band (origin at the "+"
// curve's pinch height, cap0 = that curve's half), a MIDDLE band (from
// the "-" curve's pinch height to the "+" curve's, cap0 = the lower half,
// cap1 = the upper half - the positive-length doubly-notched shape
// CylindricalFace already admits: the part of B's wall inside A) and a
// LOWER band (top at the "-" curve's pinch height, cap1 = its half). On
// B both pinches of a curve are level at any angle (+/- sqrt(r_a^2 -
// r_b^2)/sin alpha from Q), so nothing on B depends on alpha; one half's
// chain dips below its own pinch height on one side, which the notch
// contract already allows. Every piece is a shape Brep::FromMixedFaces()
// builds and Brep::TessellateConforming()'s strip mesher meshes. Each of
// the four canonical arcs bounds exactly two faces in every op:
// Intersection keeps the two plugs and the two middle bands (4 faces,
// no original end survives, no cap); Union keeps A's eight wall pieces
// and B's four outer bands plus BuildEndCap's quadrant wedges on every
// original end (each of A's ends is now four angular pieces, each of
// B's two); A - B keeps A's eight wall pieces and B's two middle bands
// flipped as the bore's wall; B - A keeps B's four outer bands and A's
// two plugs flipped. All of that falls out of the op-agnostic
// classify-then-bucket step with no op-specific code, exactly as for
// Steinmetz; RepresentativeInteriorPointMixed's existing branches
// classify every piece with a margin of at least the smaller radius (its
// own doc comment has the per-shape argument).
//
// SLOPED CUT - why a helix and not a second horizontal cut. Cutting the
// plain pieces horizontally at BOTH pinch heights would put the other
// loop's pinch height on a slab piece's rail too (the higher loop's
// lower piece spans it, and the lower loop's upper piece), so those slab
// pieces would have to be cut there as well - and that horizontal line
// crosses the piece's own notch arc whenever cos(alpha)*(r_a +
// sqrt(r_a^2 - r_b^2)) <= r_b, i.e. |cos alpha| <= tan(theta_m/2) (for
// r_b/r_a = 1/2 every angle within 15.5 degrees of a right angle; for
// 0.95, within 46 degrees), leaving pieces no CylindricalFace shape
// describes. The single chain from pinch vertex to pinch vertex needs no
// slab piece cut at all, shares every rail vertex by construction at
// every angle, and keeps the ten-piece decomposition and every face
// count of the right-angle case. Its two pieces are the ordinary singly-
// notched shapes, with one difference FromMixedFaces admits explicitly:
// the chain's last point sits on the angle-`angle` rail at a height
// other than the flat corner's, so that rail corner is the chain's own
// last point (see CylindricalFace's doc comment in brep.h); the chain is
// sampled with kCylinderPairSamples segments, its endpoints the literal
// pinch samples the slab pieces use, its interior points on A's wall,
// and its chord-midpoint sagitta against the helix is the piece's notch
// tolerance, as for the arcs. In A - B, B's middle band's straight rail
// between the two pinch generators joins the same two vertices as one
// plain piece's helix; FromMixedFaces keeps a straight chord and a
// non-degenerate polyline distinct (BuildFaceLoop, brep.cpp), the
// notched analogue of the arc-vs-chord distinction the right-angle cut
// already needed.
//
// SAMPLING IDENTITY: the four arcs are sampled ONCE, uniformly in the
// SMALLER cylinder's own angle theta over [phi, phi+pi] and [phi+pi,
// phi+2pi] for each root, kCylinderPairSamples segments each, and the
// identical std::vector<Point3d> is handed to every fragment of BOTH
// cylinders bounded by that arc (a slab piece of A, a band of B) - the
// same one-producer-several-consumers principle the Steinmetz split
// follows, and what makes the shared boundary bit-identical on both
// sides. "The smaller cylinder" is a canonical choice: the two radii
// differ by more than CylinderPairRadiusTolerance (the dispatch's own
// test), so it is the same cylinder whichever operand is `self`, and
// every quantity below is computed from (larger, smaller) in that fixed
// order, never from (self, other). The helix chain of each plain piece
// is likewise built once and handed to both of its pieces.
//
// LEVEL FAST PATH: when the four sampled pinch points lie at ONE height
// on A within the pipeline tolerance `tol` (measured directly on the
// samples; |cot(alpha)|*sqrt(r_a^2 - r_b^2) <= tol, about 1e-8 radians
// off a right angle at unit scale, an exactly-constructed right angle
// evaluating to ~1e-16), every slab is anchored at h_Q and the plain
// pieces are cut by the plain circle at h_Q - the exact code path, and
// the bit-identical result, of the right-angle-only producer this one
// generalizes. A pair outside that band takes the sloped cut; nothing is
// ever snapped. The two pinches of each of B's curves are additionally
// required to be level on B within tol (they are, for intersecting
// axes, by the reflection through the axes' plane; the check is a
// consistency guard behind the closest-points test, not a scope limit).
//
// EXTENT PRECONDITION - the Steinmetz one with the reaches above: every
// original end of BOTH cylinders must sit farther from the crossing than
// the curve's axial reach on that cylinder plus twice the sampled arcs'
// sagitta bound plus tol. Read off the sampled lists (whose extreme
// heights ARE the closed-form reaches, the extremum being itself a
// sample since kCylinderPairSamples is even), so the check and the
// geometry cannot disagree at any angle. It guarantees that both loops
// are strictly interior to A's wall (the pinch heights included, being
// on the curve) and both curves to B's, that neither end disc touches
// the other cylinder (every point of the other cylinder inside this
// one's wall lies on or between the curves), and that
// SynthesizeEndCaps' on-axis probe just past every original end reads
// kOut. A cylinder ending inside the other (a blind bore, a partial
// penetration) is refused rather than split into fragments whose curves
// would run into an end disc this pipeline has no face for.
//
// SKEW AXES (the two axis lines' closest points farther apart than tol)
// are supported too, when the smaller cylinder still FULLY PIERCES the
// larger on every generator - d + r_b < r_a, d the axes' closest-point
// distance, a single closed-form condition independent of alpha (see
// ComputeUnequalCylinderCrossing's own comment for the derivation) -
// PROVIDED the axes also meet at a RIGHT ANGLE (any d), or the pair is
// actually intersecting (d = 0, any angle). The representation itself
// needs no change for the skew case: B's two "pinch" generators (theta =
// phi, phi + pi) still exist and still bound its bands, only their two
// heights on EITHER cylinder are no longer guaranteed level within a
// band the way they are for intersecting axes - and FromMixedFaces'
// rail-corner mechanism (its own doc comment) already lets a notched
// cap's far rail sit at a height other than the near rail's, exactly
// what a general-angle plain-piece helix chain already needed. What IS
// out of scope is a genuinely OBLIQUE skew pair (alpha != 90 degrees AND
// d != 0): there a single LOOP's own two pinch heights on the LARGER
// cylinder can differ (cot(alpha)*sqrt(r_a^2-(d+r_b)^2) vs the same with
// (d-r_b), equal only when d = 0 or alpha = 90), and measured directly,
// Brep::TessellateConforming()'s strip mesher does not yet triangulate a
// slab built from two such heights into a closed manifold (the B-rep it
// builds IS ON_Brep::IsValid(), so this is a strip-mesher limitation,
// not a representation or splitting one) - refused outright by
// ComputeUnequalCylinderCrossing's own guard rather than shipped with a
// silently wrong mesh. A skew pair that does not fully pierce (a partial
// penetration) is refused for the separate, representational reason a
// blind bore is: its curve would run into an end disc this pipeline has
// no face for.
//
// Also refused, each with std::invalid_argument naming "non-parallel
// axes" and "UNEQUAL radii" (the substrings the existing dispatch-
// boundary tests key on): a skew pair that does not fully pierce, a
// genuinely oblique skew pair (naming "OBLIQUE"), a partial-sweep
// operand, an operand already notched at an end, and near-degenerate
// slivers (a slab or a plain piece narrower than
// kMinCylinderPairPieceAngle, or samples so close that the weld could
// merge them).

constexpr int kCylinderPairSamples = 200;  // segments per arc; EVEN, so index N/2 is the sample at theta = phi + pi/2
constexpr double kMinCylinderPairPieceAngle = 1e-3;  // radians; a narrower slab or plain piece is refused as a sliver

struct UnequalCylinderCrossing {
  Point3d q;               // the axes' crossing point, ON the larger cylinder's own axis
  double alpha = 0.0;      // angle between the two axis directions, in (0, pi)
  double h_q_large = 0.0;  // q's height along the larger cylinder's axis, from its own origin
  double h_q_small = 0.0;  // the smaller cylinder's closest axis point's height, from its own origin
  // theta_m_near/far: asin((d +/- r_b) / r_a), d the axes' closest-point
  // distance (0 for intersecting axes). A slab of the larger cylinder's
  // wall spans theta_m_near - theta_m_far; at d = 0 these are
  // +/- asin(r_b/r_a), the old symmetric theta_m, and the span is
  // 2*asin(r_b/r_a) as before.
  double theta_m_near = 0.0;
  double theta_m_far = 0.0;
  // True when all four sampled pinch points sit at h_q_large within the
  // pipeline tolerance - the LEVEL FAST PATH of the section comment
  // (the right-angle decomposition, one cut at h_Q); false selects the
  // sloped cut.
  bool level = false;
  // The "+" root over theta in [phi, phi+pi] and [phi+pi, phi+2pi], then
  // the "-" root over the same two ranges - each in increasing theta on
  // the smaller cylinder, kCylinderPairSamples+1 points, first and last
  // points at pinch points (arcs[0].back() and arcs[1].front() are the
  // same sample; likewise arcs[2]/arcs[3]).
  std::array<std::vector<Point3d>, 4> arcs;
  std::array<double, 4> sagitta{};  // per list, the same chord-midpoint bound the other producers compute
};

// One point of the intersection curve: the quadratic of the section
// comment solved on the smaller cylinder's generator `theta`, for the
// "+" (root_sign = +1) or "-" (root_sign = -1) root.
Point3d UnequalCylinderCurvePoint(const Brep::CylindricalFace& large, const Brep::CylindricalFace& small,
                                  double theta, int root_sign) {
  const Vector3d a = large.frame.zaxis;
  auto project = [&](const Vector3d& v) { return v - ON_DotProduct(v, a) * a; };
  const Vector3d e = small.radius * (std::cos(theta) * small.frame.xaxis + std::sin(theta) * small.frame.yaxis);
  const Vector3d mb = project(small.frame.zaxis);
  const Vector3d mde = project((small.frame.origin - large.frame.origin) + e);
  const double a2 = ON_DotProduct(mb, mb);
  const double a1 = 2.0 * ON_DotProduct(mb, mde);
  const double a0 = ON_DotProduct(mde, mde) - large.radius * large.radius;
  const double disc = a1 * a1 - 4.0 * a2 * a0;
  if (!(a2 > 0.0) || !(disc > 0.0)) {
    throw std::runtime_error(
        "dino8::kernel::BooleanCombineMixed: the unequal-radius cylinder/cylinder "
        "intersection curve's per-angle quadratic has no real root on a "
        "generator of the smaller cylinder - impossible for intersecting axes "
        "and r_b < r_a, so please report this as a bug");
  }
  const double h = (-a1 + root_sign * std::sqrt(disc)) / (2.0 * a2);
  return small.frame.origin + h * small.frame.zaxis + e;
}

// Samples the four canonical arcs and checks every precondition of the
// section comment. `large` and `small` are the two operands in radius
// order (the caller has already established large.radius > small.radius
// by more than CylinderPairRadiusTolerance), so every result is
// argument-order independent by construction.
UnequalCylinderCrossing ComputeUnequalCylinderCrossing(const Brep::CylindricalFace& large,
                                                       const Brep::CylindricalFace& small, double tol) {
  const Vector3d a = large.frame.zaxis;
  const Vector3d b = small.frame.zaxis;

  // Closest points of the two (infinite) axis lines - the same closed
  // form ComputeSteinmetzCrossing uses.
  const Vector3d w = large.frame.origin - small.frame.origin;
  const double d_ab = ON_DotProduct(a, b);
  const double d_aw = ON_DotProduct(a, w);
  const double d_bw = ON_DotProduct(b, w);
  const double denom = 1.0 - d_ab * d_ab;  // sin^2(alpha), bounded away from 0 by the dispatch's own parallel test
  const double s = (d_ab * d_bw - d_aw) / denom;
  const double t = (d_bw - d_ab * d_aw) / denom;
  const Point3d on_large = large.frame.origin + s * a;
  const Point3d on_small = small.frame.origin + t * b;
  // d: the axes' closest-point distance - 0 for intersecting axes, > tol
  // for genuinely skew ones. A skew pair is supported when it still
  // FULLY PIERCES: the per-angle discriminant on the smaller cylinder,
  // 4*sin^2(alpha)*(r_a^2 - (d + r_b*cos(theta))^2) (see this function's
  // own doc comment for the general-D derivation), is minimized over
  // theta at cos(theta) = 1, so "every generator of the smaller cylinder
  // meets the larger one" reduces to the single closed-form, alpha-
  // INDEPENDENT condition d + r_b < r_a. A skew pair that fails it is a
  // partial penetration (a blind, ragged intersection this pipeline has
  // no fragment shapes for) and stays refused, like an intersecting-axis
  // pair failing the extent precondition below.
  const double d = on_large.DistanceTo(on_small);
  const bool is_skew = d > tol;
  if (is_skew && !(d + small.radius < large.radius - tol)) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: two cylindrical faces interacting "
        "with non-parallel axes and UNEQUAL radii on genuinely SKEW axes (the "
        "axis lines' closest points farther apart than the pipeline tolerance) "
        "whose smaller cylinder does NOT fully pierce the larger one on every "
        "generator (the closest-axis distance plus the smaller radius is not "
        "strictly less than the larger radius) is a partial penetration and is "
        "out of scope - only a skew pair whose smaller cylinder fully pierces "
        "the larger, or an intersecting-axis pair (any angle), is supported - "
        "see this function's own doc comment in boolean.h");
  }

  UnequalCylinderCrossing crossing;
  crossing.q = on_large;
  crossing.alpha = std::acos(std::max(-1.0, std::min(1.0, d_ab)));
  crossing.h_q_large = s;
  crossing.h_q_small = t;
  // The loop-span extremes generalized to a possibly-nonzero closest-axis
  // distance d (see this function's own doc comment): at d = 0 these
  // reduce to +/- asin(r_b/r_a), the old symmetric theta_m. Clamped
  // because (d - r_b)/r_a can be negative (d < r_b) or, right at the
  // full-pierce boundary, brush +/-1.
  crossing.theta_m_near = std::asin(std::max(-1.0, std::min(1.0, (d + small.radius) / large.radius)));
  crossing.theta_m_far = std::asin(std::max(-1.0, std::min(1.0, (d - small.radius) / large.radius)));
  const double slab_span = crossing.theta_m_near - crossing.theta_m_far;

  // Sliver guard: a slab spans `slab_span`, a plain piece pi - slab_span.
  if (!(slab_span >= kMinCylinderPairPieceAngle) || !(ON_PI - slab_span >= kMinCylinderPairPieceAngle)) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: two cylindrical faces interacting "
        "with non-parallel axes and UNEQUAL radii whose radius ratio (and, for "
        "a skew pair, closest-axis distance) makes a slab or a plain piece of "
        "the larger cylinder's wall narrower than 1e-3 radians is refused as a "
        "sliver - see this function's own doc comment in boolean.h");
  }

  // phi: the common perpendicular's angle in the smaller cylinder's own
  // basis - its two pinch generators are theta = phi and theta = phi + pi.
  Vector3d n = ON_CrossProduct(a, b);
  n.Unitize();
  const double phi = std::atan2(ON_DotProduct(n, small.frame.yaxis), ON_DotProduct(n, small.frame.xaxis));

  const int root_signs[4] = {+1, +1, -1, -1};
  const double from[4] = {phi, phi + ON_PI, phi, phi + ON_PI};
  double min_spacing = std::numeric_limits<double>::infinity();
  for (int i = 0; i < 4; ++i) {
    std::vector<Point3d> pts;
    pts.reserve(static_cast<size_t>(kCylinderPairSamples) + 1);
    for (int k = 0; k <= kCylinderPairSamples; ++k) {
      // `from + pi * k / N` rather than `from + (to - from) * k / N`, so
      // the sample at theta = phi + pi is the identical double whether it
      // ends the first range or starts the second.
      const double theta = from[i] + ON_PI * static_cast<double>(k) / kCylinderPairSamples;
      pts.push_back(UnequalCylinderCurvePoint(large, small, theta, root_signs[i]));
    }
    // Same sagitta-style bound the other notch producers compute: max
    // over every segment of the distance between the chord's midpoint
    // and the true curve's point at the midpoint angle.
    double max_sagitta = 0.0;
    for (int k = 0; k < kCylinderPairSamples; ++k) {
      const double theta_mid = from[i] + ON_PI * (static_cast<double>(k) + 0.5) / kCylinderPairSamples;
      const Point3d chord_mid = 0.5 * (pts[static_cast<size_t>(k)] + pts[static_cast<size_t>(k) + 1]);
      max_sagitta = std::max(max_sagitta,
                             chord_mid.DistanceTo(UnequalCylinderCurvePoint(large, small, theta_mid, root_signs[i])));
      min_spacing = std::min(min_spacing, pts[static_cast<size_t>(k)].DistanceTo(pts[static_cast<size_t>(k) + 1]));
    }
    crossing.arcs[static_cast<size_t>(i)] = std::move(pts);
    crossing.sagitta[static_cast<size_t>(i)] = max_sagitta;
  }
  if (!(min_spacing > 10.0 * tol)) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: two cylindrical faces interacting "
        "with non-parallel axes and UNEQUAL radii are too small for the "
        "intersection curve's samples to stay apart by more than 10x the "
        "pipeline tolerance - refused rather than welded into a degenerate "
        "chain - see this function's own doc comment in boolean.h");
  }

  auto height_on = [](const Brep::CylindricalFace& c, const Point3d& p) {
    return ON_DotProduct(p - c.frame.origin, c.frame.zaxis);
  };

  // LEVEL FAST PATH (section comment): the four pinch points at h_Q on
  // the larger cylinder within tol selects the single flat cut; otherwise
  // the sloped cut. This is UNAFFECTED by skew: the pinch height on the
  // larger cylinder is cot(alpha)*sqrt(r_a^2 - (d+/-r_b)^2)-shaped and at
  // alpha = 90 degrees collapses to h_Q regardless of d (cot(90) = 0), so
  // a right-angle skew pair still takes the level fast path exactly as
  // the intersecting-axis one does.
  double pinch_dev_large = 0.0;
  for (const std::vector<Point3d>& arc : crossing.arcs) {
    for (const Point3d* p : {&arc.front(), &arc.back()}) {
      pinch_dev_large = std::max(pinch_dev_large, std::fabs(height_on(large, *p) - crossing.h_q_large));
    }
  }
  crossing.level = pinch_dev_large <= tol;
  // Each root's two pinch points are level on the SMALLER cylinder for
  // intersecting axes (d <= tol, a consistency guard, unreachable behind
  // the closest-points test) - but genuinely NOT for a skew pair (d >
  // tol): its two pinch heights on the smaller cylinder are
  // sqrt(r_a^2-(d+r_b)^2)/sin(alpha) and sqrt(r_a^2-(d-r_b)^2)/sin(alpha),
  // equal only at d = 0. SplitCylindricalByUnequalCylinder's own band
  // construction reads each pinch height directly off the oriented arc's
  // own endpoints rather than assuming they agree, so this stays a pure
  // consistency guard on the intersecting-axis regime, not a scope limit.
  if (!is_skew) {
    const double pinch_dev_small =
        std::max(std::fabs(height_on(small, crossing.arcs[0].front()) - height_on(small, crossing.arcs[0].back())),
                 std::fabs(height_on(small, crossing.arcs[2].front()) - height_on(small, crossing.arcs[2].back())));
    if (pinch_dev_small > tol) {
      throw std::invalid_argument(
          "dino8::kernel::BooleanCombineMixed: two cylindrical faces interacting "
          "with non-parallel axes and UNEQUAL radii whose intersection curve's "
          "two pinch points on one generator of the smaller cylinder are not at "
          "one height within the pipeline tolerance despite intersecting axes - "
          "please report this as a bug - see this function's own doc comment in "
          "boolean.h");
    }
  }
  // A SKEW pair off the level fast path (is_skew && !crossing.level, i.e.
  // genuinely OBLIQUE axes together with skew) can put a single LOOP's
  // own two pinch heights on the LARGER cylinder at two different values
  // (section 1.4 of this increment's own research: h_pinch_near =
  // cot(alpha)*sqrt(r_a^2-(d+r_b)^2), h_pinch_far the same with (d-r_b) -
  // equal only when d = 0 or alpha = 90 degrees). Every producer above
  // already reads each pinch height off its own sampled endpoint rather
  // than assuming they agree (SplitCylindricalByUnequalCylinder's own
  // comment), and the resulting B-rep IS valid - but measured directly,
  // Brep::TessellateConforming()'s strip mesher does not yet triangulate
  // a slab's own upper/lower/plain pieces correctly when their two rail
  // heights differ THIS way (verified: the conforming mesh comes back
  // with duplicated, non-manifold coverage along the slab's own rails,
  // while the SAME configuration's Intersection - built only from the
  // plug and B's bands, neither of which has this asymmetry - and B - A
  // - built only from B's bands and A's plugs, flipped - both come back
  // correct). Rather than ship a silently wrong Union/Difference mesh,
  // this refuses the combination outright until the strip mesher is
  // extended; the B-rep-only (IsValid) path is unaffected by this guard,
  // and the LEVEL cases - any d at alpha = 90 degrees, or any alpha at
  // d = 0 - are unaffected since crossing.level is already true there.
  if (is_skew && !crossing.level) {
    const double loop_pinch_dev_large =
        std::max(std::fabs(height_on(large, crossing.arcs[0].front()) - height_on(large, crossing.arcs[0].back())),
                 std::fabs(height_on(large, crossing.arcs[2].front()) - height_on(large, crossing.arcs[2].back())));
    if (loop_pinch_dev_large > tol) {
      throw std::invalid_argument(
          "dino8::kernel::BooleanCombineMixed: two cylindrical faces interacting "
          "with non-parallel axes and UNEQUAL radii on a genuinely OBLIQUE "
          "(non-right-angle) SKEW pair, where a single loop's own two pinch "
          "points sit at different heights on the larger cylinder, is out of "
          "scope for this increment - measured to build an ON_Brep::IsValid() "
          "result whose conforming tessellation is not yet a closed manifold "
          "(the strip mesher does not yet handle a slab's own asymmetric rail "
          "heights) - a right-angle skew pair (any closest-axis distance) and "
          "an oblique intersecting-axis pair (closest-axis distance 0) both "
          "remain fully supported - see this function's own doc comment in "
          "boolean.h");
    }
  }

  // EXTENT PRECONDITION (section comment), read off the sampled lists.
  double max_sagitta = 0.0;
  for (const double sg : crossing.sagitta) max_sagitta = std::max(max_sagitta, sg);
  for (const Brep::CylindricalFace* c : {&large, &small}) {
    double h_min = std::numeric_limits<double>::infinity();
    double h_max = -std::numeric_limits<double>::infinity();
    for (const std::vector<Point3d>& arc : crossing.arcs) {
      for (const Point3d& p : arc) {
        const double h = height_on(*c, p);
        h_min = std::min(h_min, h);
        h_max = std::max(h_max, h);
      }
    }
    const double margin = 2.0 * max_sagitta + tol;
    if (!(h_min - margin > 0.0 && h_max + margin < c->length)) {
      throw std::invalid_argument(
          "dino8::kernel::BooleanCombineMixed: two cylindrical faces with "
          "non-parallel axes and UNEQUAL radii (intersecting or, when the "
          "smaller cylinder fully pierces the larger, genuinely skew) are "
          "supported only when the crossing is STRICTLY interior to both "
          "cylinders - every "
          "original end must sit farther from the crossing along its own axis "
          "than the intersection curve's axial reach on that cylinder ((r_a + "
          "r_b |cos alpha|)/sin alpha along the smaller cylinder's axis, (r_b + "
          "r_a |cos alpha|)/sin alpha along the larger cylinder's), so that both "
          "curves lie wholly inside each wall and neither end disc touches the "
          "other cylinder - a partial penetration or blind bore is out of scope "
          "- see this function's own doc comment in boolean.h");
    }
  }
  return crossing;
}

// Splits a FULL-SWEEP, un-notched cylindrical fragment `cf` against an
// unequal-radius, intersecting-axis cylinder `other` (any axis angle)
// into the ten (when `cf` is the larger cylinder) or six (the smaller)
// fragments derived in the section comment above, all pushed onto the
// worklist for the existing generic classifier to keep or discard - the
// same "produce every piece, never privilege one" pattern every other
// split in this file follows. Which canonical arc bounds which fragment
// is decided from the lists themselves (a loop's pinch angles and pinch
// height from its arcs' endpoints, upper vs lower by comparing the
// loop's two arcs' mid-angle samples against each other - never against
// h_Q, since at a general angle both arcs of a loop can lie on one side
// of it - orientation from the quarter- and mid-angle samples' local
// angles), exactly as the Steinmetz split does, so neither cylinder
// needs separate sign bookkeeping and no closed-form height is ever
// assumed: the cut heights are the sampled pinch heights themselves.
std::vector<MixedFace> SplitCylindricalByUnequalCylinder(const Brep::CylindricalFace& cf,
                                                          const Brep::CylindricalFace& other, double tol) {
  for (const Brep::CylindricalFace* c : {&cf, &other}) {
    if (!(c->angle >= 2.0 * ON_PI - kAxisAlignTol)) {
      throw std::invalid_argument(
          "dino8::kernel::BooleanCombineMixed: an unequal-radius cylinder/"
          "cylinder interaction with non-parallel axes (UNEQUAL radii, "
          "intersecting axes) involving a PARTIAL-sweep (angle < 2*pi) "
          "cylindrical fragment is out of scope - see "
          "SplitCylindricalByUnequalCylinder's own doc comment in boolean.cpp");
    }
  }
  if (!cf.cap0_notch_points.empty() || !cf.cap1_notch_points.empty()) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: an unequal-radius cylinder/cylinder "
        "interaction with non-parallel axes (UNEQUAL radii, intersecting axes) "
        "against a cylindrical fragment that is ALREADY notched at an end is "
        "out of scope - see SplitCylindricalByUnequalCylinder's own doc comment "
        "in boolean.cpp");
  }

  const bool cf_is_large = cf.radius > other.radius;
  const Brep::CylindricalFace& large = cf_is_large ? cf : other;
  const Brep::CylindricalFace& small = cf_is_large ? other : cf;
  const UnequalCylinderCrossing crossing = ComputeUnequalCylinderCrossing(large, small, tol);
  const double h_q = cf_is_large ? crossing.h_q_large : crossing.h_q_small;

  auto height_on = [&](const Point3d& p) { return ON_DotProduct(p - cf.frame.origin, cf.frame.zaxis); };
  auto local_angle = [&](const Brep::CylindricalFace& c, const Point3d& p) {
    const Vector3d d = p - c.frame.origin;
    double ang = std::atan2(ON_DotProduct(d, c.frame.yaxis), ON_DotProduct(d, c.frame.xaxis));
    if (ang < 0.0) ang += 2.0 * ON_PI;
    return ang;
  };
  auto bug = [](const char* what) {
    throw std::runtime_error(std::string("dino8::kernel::BooleanCombineMixed: the unequal-radius cylinder/"
                                         "cylinder bookkeeping ") +
                             what + " - please report this as a bug");
  };
  const size_t mid_index = kCylinderPairSamples / 2;
  const size_t quarter_index = kCylinderPairSamples / 4;

  // Same elementary in-plane rotation the parallel-axis and Steinmetz
  // splits use: rotate (xaxis, yaxis) about zaxis by `begin` so local
  // angle 0 sits at cf's own physical angle `begin`.
  auto rotated = [&](double begin, double angle) {
    Brep::CylindricalFace child = cf;
    child.angle = angle;
    const double cb = std::cos(begin), sb = std::sin(begin);
    child.frame.xaxis = cb * cf.frame.xaxis + sb * cf.frame.yaxis;
    child.frame.yaxis = -sb * cf.frame.xaxis + cb * cf.frame.yaxis;
    child.frame.UpdateEquation();
    return child;
  };
  auto with_origin_at = [&](Brep::CylindricalFace child, double height, double length) {
    child.frame.origin = cf.frame.origin + height * cf.frame.zaxis;
    child.frame.UpdateEquation();
    child.length = length;
    return child;
  };
  // A canonical list in `child`'s own increasing local angle: reversed
  // iff its quarter-angle sample sits at a larger local angle than its
  // mid-angle sample (both strictly inside the child's sweep, so neither
  // can wrap at the 0/2*pi seam the endpoints touch).
  auto oriented = [&](const Brep::CylindricalFace& child, const std::vector<Point3d>& pts) {
    std::vector<Point3d> out = pts;
    const double a_quarter = local_angle(child, pts[quarter_index]);
    const double a_mid = local_angle(child, pts[mid_index]);
    if (!(a_quarter > 0.0 && a_quarter < child.angle && a_mid > 0.0 && a_mid < child.angle)) {
      bug("found a canonical arc's interior samples outside the fragment's own sweep");
    }
    if (a_quarter > a_mid) std::reverse(out.begin(), out.end());
    return out;
  };

  std::vector<MixedFace> out;
  if (cf_is_large) {
    // A: two slabs (each: upper, lower, plug, anchored at the loop's own
    // pinch height) and two plain pieces (each cut from pinch vertex to
    // pinch vertex), see the section comment.
    struct Slab {
      double begin = 0.0, angle = 0.0;
      const std::vector<Point3d>* upper = nullptr;
      const std::vector<Point3d>* lower = nullptr;
      double upper_sagitta = 0.0, lower_sagitta = 0.0;
      // The loop's two pinch samples, at the slab's begin (local angle 0)
      // and end (local angle `angle`) rails - the literal points every
      // rail vertex on A is welded from.
      const Point3d* begin_pt = nullptr;
      const Point3d* end_pt = nullptr;
      // The height every piece of this slab is anchored at: h_Q on the
      // level path (bit-identical to the right-angle producer), else the
      // loop's own sampled pinch height.
      double h_cut = 0.0;
    };
    Slab slabs[2];
    for (size_t loop = 0; loop < 2; ++loop) {
      const std::vector<Point3d>& first = crossing.arcs[2 * loop];
      const std::vector<Point3d>& second = crossing.arcs[2 * loop + 1];
      const double t0 = local_angle(cf, first.front());
      const double t1 = local_angle(cf, first.back());
      double span = t1 - t0;
      if (span < 0.0) span += 2.0 * ON_PI;
      double begin = t0;
      const Point3d* begin_pt = &first.front();
      const Point3d* end_pt = &first.back();
      if (span > ON_PI) {  // the loop is the SHORT way round between its two pinches
        begin = t1;
        span = 2.0 * ON_PI - span;
        std::swap(begin_pt, end_pt);
      }
      if (std::fabs(span - (crossing.theta_m_near - crossing.theta_m_far)) > 1e-6) {
        bug("measured a slab's angular span that disagrees with theta_m_near - theta_m_far");
      }
      // Upper vs lower by comparing the two arcs' centre samples against
      // EACH OTHER: at a general angle both arcs of one loop can sit on
      // the same side of h_Q (for r 2/1 whenever alpha is 60 degrees or
      // less from either end of (0, pi)); at a right angle they straddle
      // it at h_Q +/- r_b and the comparison answers exactly as "above
      // h_Q" did.
      const bool first_is_upper = height_on(first[mid_index]) > height_on(second[mid_index]);
      if (!(std::fabs(height_on(first[mid_index]) - height_on(second[mid_index])) > tol)) {
        bug("found a loop's two arcs at one height at the loop's centre");
      }
      Slab& sl = slabs[loop];
      sl.begin = begin;
      sl.angle = span;
      sl.upper = first_is_upper ? &first : &second;
      sl.lower = first_is_upper ? &second : &first;
      sl.upper_sagitta = crossing.sagitta[first_is_upper ? 2 * loop : 2 * loop + 1];
      sl.lower_sagitta = crossing.sagitta[first_is_upper ? 2 * loop + 1 : 2 * loop];
      sl.begin_pt = begin_pt;
      sl.end_pt = end_pt;
      sl.h_cut = crossing.level ? h_q : height_on(*begin_pt);
      if (!crossing.level && !(sl.h_cut > 0.0 && sl.h_cut < cf.length)) {
        bug("found a loop's pinch height outside the larger cylinder's wall despite the extent precondition");
      }
    }
    if (slabs[1].begin < slabs[0].begin) std::swap(slabs[0], slabs[1]);

    out.reserve(10);
    for (const Slab& sl : slabs) {
      const Brep::CylindricalFace base = rotated(sl.begin, sl.angle);
      const double h_cut = sl.h_cut;

      // Upper piece: from the loop's pinch height up to cf's own original
      // top, notched below by the loop's upper arc (endpoints at the two
      // pinches, local angles 0 and `angle`, at v=0 - exactly for the
      // begin pinch, within ~1e-16 for the end pinch, both far inside
      // the rail-corner contract's 1e-6).
      Brep::CylindricalFace upper = with_origin_at(base, h_cut, cf.length - h_cut);
      upper.cap0_notch_points = oriented(upper, *sl.upper);
      upper.cap0_notch_tolerance = sl.upper_sagitta;
      upper.end0_is_original = false;

      // Lower piece: from cf's own original bottom up to the pinch
      // height, notched above by the lower arc.
      Brep::CylindricalFace lower = base;
      lower.length = h_cut;
      lower.cap1_notch_points = oriented(lower, *sl.lower);
      lower.cap1_notch_tolerance = sl.lower_sagitta;
      lower.end1_is_original = false;

      // Plug: the wall inside the other cylinder - length 0, both caps
      // notched, no original end.
      Brep::CylindricalFace plug = with_origin_at(base, h_cut, 0.0);
      plug.cap0_notch_points = oriented(plug, *sl.lower);
      plug.cap0_notch_tolerance = sl.lower_sagitta;
      plug.cap1_notch_points = oriented(plug, *sl.upper);
      plug.cap1_notch_tolerance = sl.upper_sagitta;
      plug.end0_is_original = false;
      plug.end1_is_original = false;

      out.push_back(MixedFaceFromCyl(std::move(upper)));
      out.push_back(MixedFaceFromCyl(std::move(lower)));
      out.push_back(MixedFaceFromCyl(std::move(plug)));
    }
    // Plain pieces: the two gaps between the slabs, each cut so its rails
    // end where the slab pieces' rails do.
    for (size_t gap = 0; gap < 2; ++gap) {
      const Slab& before = slabs[gap];
      const Slab& after = slabs[1 - gap];
      const double begin = before.begin + before.angle;
      double angle = after.begin - begin;
      if (angle < 0.0) angle += 2.0 * ON_PI;
      if (angle < kMinCylinderPairPieceAngle || angle > ON_PI) bug("measured a plain piece that is not the gap between the two slabs");
      if (crossing.level) {
        // LEVEL FAST PATH: the flat cut at h_Q, the right-angle producer's
        // own statements.
        Brep::CylindricalFace plain_lower = rotated(begin, angle);
        plain_lower.length = h_q;
        plain_lower.end1_is_original = false;
        Brep::CylindricalFace plain_upper = with_origin_at(plain_lower, h_q, cf.length - h_q);
        plain_upper.end0_is_original = false;
        plain_upper.end1_is_original = cf.end1_is_original;
        out.push_back(MixedFaceFromCyl(std::move(plain_lower)));
        out.push_back(MixedFaceFromCyl(std::move(plain_upper)));
        continue;
      }
      // SLOPED CUT (section comment): one helix chain, linear in (local
      // angle, height), from the slab-before's end pinch on this piece's
      // angle-0 rail (height h_left) to the slab-after's begin pinch on
      // its angle-`angle` rail (height h_right), its endpoints the
      // literal pinch samples so the vertices weld exactly as the slab
      // pieces' do, its interior on cf's wall. The one list is the lower
      // piece's cap1 notch (origin at cf's own bottom, length h_left) and
      // the upper piece's cap0 notch (origin at h_left): both pieces share
      // the plain piece's frame and the chain is already in increasing
      // local angle, so no orientation step is needed. The chain's last
      // point sits at height h_right on the angle-`angle` rail of both -
      // v = h_right for the lower piece and v = h_right - h_left (negative
      // for one of the two plain pieces) for the upper - which is the
      // sloped rail corner FromMixedFaces admits and whose height its
      // v-domain widening already covers.
      const Brep::CylindricalFace base = rotated(begin, angle);
      const Point3d& p_left = *before.end_pt;
      const Point3d& p_right = *after.begin_pt;
      const double h_left = height_on(p_left);
      const double h_right = height_on(p_right);
      if (!(std::fabs(local_angle(base, p_right) - angle) < 1e-6) || !(std::fabs(h_right - h_left) > tol)) {
        bug("found a plain piece's two pinch vertices not on its rails, or level on the sloped path");
      }
      std::vector<Point3d> chain;
      chain.reserve(static_cast<size_t>(kCylinderPairSamples) + 1);
      auto helix_at = [&](double s) {  // s in [0, 1] along the chain
        return PointOnCylFace(base, angle * s, h_left + (h_right - h_left) * s);
      };
      chain.push_back(p_left);
      for (int k = 1; k < kCylinderPairSamples; ++k) {
        chain.push_back(helix_at(static_cast<double>(k) / kCylinderPairSamples));
      }
      chain.push_back(p_right);
      double helix_sagitta = 0.0;
      double helix_spacing = std::numeric_limits<double>::infinity();
      for (int k = 0; k < kCylinderPairSamples; ++k) {
        const Point3d chord_mid = 0.5 * (chain[static_cast<size_t>(k)] + chain[static_cast<size_t>(k) + 1]);
        helix_sagitta = std::max(helix_sagitta,
                                 chord_mid.DistanceTo(helix_at((static_cast<double>(k) + 0.5) / kCylinderPairSamples)));
        helix_spacing = std::min(helix_spacing, chain[static_cast<size_t>(k)].DistanceTo(chain[static_cast<size_t>(k) + 1]));
      }
      if (!(helix_spacing > 10.0 * tol)) {
        throw std::invalid_argument(
            "dino8::kernel::BooleanCombineMixed: two cylindrical faces interacting "
            "with non-parallel axes and UNEQUAL radii are too small for the plain "
            "pieces' sloped cut chain's samples to stay apart by more than 10x "
            "the pipeline tolerance - refused rather than welded into a "
            "degenerate chain - see this function's own doc comment in boolean.h");
      }
      Brep::CylindricalFace plain_lower = base;
      plain_lower.length = h_left;
      plain_lower.cap1_notch_points = chain;
      plain_lower.cap1_notch_tolerance = helix_sagitta;
      plain_lower.end1_is_original = false;
      Brep::CylindricalFace plain_upper = with_origin_at(base, h_left, cf.length - h_left);
      plain_upper.cap0_notch_points = chain;
      plain_upper.cap0_notch_tolerance = helix_sagitta;
      plain_upper.end0_is_original = false;
      plain_upper.end1_is_original = cf.end1_is_original;
      out.push_back(MixedFaceFromCyl(std::move(plain_lower)));
      out.push_back(MixedFaceFromCyl(std::move(plain_upper)));
    }
  } else {
    // B: two halves at the pinch generators, each an upper, a middle and
    // a lower band, see the section comment.
    const double t0 = local_angle(cf, crossing.arcs[0].front());  // the pinch generator theta = phi
    const bool plus_is_upper = height_on(crossing.arcs[0][mid_index]) > h_q;
    if ((height_on(crossing.arcs[2][mid_index]) > h_q) == plus_is_upper) {
      bug("did not find one root above and one below the crossing on the smaller cylinder");
    }
    const size_t top_first = plus_is_upper ? 0 : 2;
    const size_t bottom_first = plus_is_upper ? 2 : 0;
    // For INTERSECTING axes the two pinches of a curve are level, so one
    // height per curve serves both halves. For a SKEW pair (d > tol) they
    // generally are not (sqrt(r_a^2-(d+r_b)^2) and sqrt(r_a^2-(d-r_b)^2),
    // both over sin(alpha), equal only at d = 0): each half's own near
    // rail (local angle 0) must be anchored at THAT half's own sampled
    // pinch height, read off the oriented arc's own front point, exactly
    // the mechanism FromMixedFaces' rail-corner contract already uses for
    // the far rail (local angle `angle`) via cap*_notch_points.back() - so
    // here the SAME arrays already carry both ends' true heights and no
    // extra bookkeeping is needed beyond reading them per half instead of
    // once globally.

    out.reserve(6);
    for (int half = 0; half < 2; ++half) {
      const Brep::CylindricalFace base = rotated(t0 + half * ON_PI, ON_PI);
      // The arc of each curve whose mid-angle sample lies in this half.
      auto arc_in_half = [&](size_t first) -> size_t {
        for (size_t i = first; i < first + 2; ++i) {
          double dd = local_angle(cf, crossing.arcs[i][mid_index]) - t0;
          if (dd < 0.0) dd += 2.0 * ON_PI;
          if ((dd < ON_PI) == (half == 0)) return i;
        }
        bug("found no arc of a curve in an angular half of the smaller cylinder");
        return first;
      };
      const size_t top_i = arc_in_half(top_first);
      const size_t bottom_i = arc_in_half(bottom_first);
      // Oriented ONCE per half, per curve, so the identical array (and
      // the identical front/back heights it carries) is handed to every
      // piece of this half that shares that rail - the file's own
      // one-producer-several-consumers principle.
      const std::vector<Point3d> top = oriented(base, crossing.arcs[top_i]);
      const std::vector<Point3d> bottom = oriented(base, crossing.arcs[bottom_i]);
      const double h_top = height_on(top.front());
      const double h_bottom = height_on(bottom.front());
      if (!(h_top - h_bottom > tol)) bug("found the upper curve's pinch not above the lower curve's");

      Brep::CylindricalFace upper = with_origin_at(base, h_top, cf.length - h_top);
      upper.cap0_notch_points = top;
      upper.cap0_notch_tolerance = crossing.sagitta[top_i];
      upper.end0_is_original = false;

      Brep::CylindricalFace middle = with_origin_at(base, h_bottom, h_top - h_bottom);
      middle.cap0_notch_points = bottom;
      middle.cap0_notch_tolerance = crossing.sagitta[bottom_i];
      middle.cap1_notch_points = top;
      middle.cap1_notch_tolerance = crossing.sagitta[top_i];
      middle.end0_is_original = false;
      middle.end1_is_original = false;

      Brep::CylindricalFace lower = base;
      lower.length = h_bottom;
      lower.cap1_notch_points = bottom;
      lower.cap1_notch_tolerance = crossing.sagitta[bottom_i];
      lower.end1_is_original = false;

      out.push_back(MixedFaceFromCyl(std::move(upper)));
      out.push_back(MixedFaceFromCyl(std::move(middle)));
      out.push_back(MixedFaceFromCyl(std::move(lower)));
    }
  }
  return out;
}

// ---------------------------------------------------------------------
// Non-parallel cylinder/cylinder NO-INTERACTION tests
// ---------------------------------------------------------------------
//
// The non-parallel-axis analogue of CylinderCylinderNoInteraction (above):
// a closed-form, provably-sufficient answer to "can these two FINITE
// cylinders touch at all?", consulted by case (iv)'s dispatch BEFORE the
// Steinmetz split, so a pair that provably never meets passes through the
// split unchanged (exactly the parallel-axis no-interaction mechanism:
// the fragment is pushed onto the worklist as-is, the existing generic
// classifier then reads kOut against the other solid, and the shared
// classify-then-bucket step keeps it for Union/Difference-from-this-side
// and drops it for Intersection - no new bookkeeping anywhere downstream)
// instead of being refused by ComputeSteinmetzCrossing's own guards. Two
// independent tests, either sufficient on its own:
//
// (1) CAPSULE SEPARATION - any pair, no assumption on radii or on whether
//     the axes meet. Every point of a finite cylinder (axial band
//     [lo, hi], radius r) is within r of the foot of its own axial
//     projection, which lies on the axis SEGMENT {origin + h*zaxis,
//     h in [lo, hi]} - so the finite cylinder is contained in the capsule
//     (segment swept by a ball of radius r) of that segment. Two capsules
//     whose segments are more than r_a + r_b apart are disjoint (a shared
//     point would put the two segments within r_a + r_b of each other by
//     the triangle inequality), so the finite cylinders are too. The
//     segment/segment distance is the standard closed form
//     (Ericson, "Real-Time Collision Detection," 5.1.9, ClosestPtSegmentSegment;
//     equivalently Eberly / Sunday's dist3D_Segment_to_Segment): minimize
//     the quadratic |P(s) - Q(t)|^2 over the unit square, clamping to its
//     edges. Conservative in both directions: it never separates an
//     interacting pair, and it can fail to separate a disjoint one whose
//     axis segments come within r_a + r_b (e.g. two equal-radius pegs
//     end-to-side with a small gap) - which is exactly what test (2)
//     closes for the one configuration this kernel otherwise supports.
//
// (2) STEINMETZ AXIAL BAND - equal radii and genuinely intersecting axes
//     only (the same radius_tol and closest-points test
//     ComputeSteinmetzCrossing itself applies, so the two never disagree
//     about which regime a pair is in). Derivation: put Q at the origin,
//     A's axis along z, B's along b = (sin alpha, 0, cos alpha). A point
//     p = (x, y, h) is inside A iff x^2 + y^2 < r^2 and inside B iff
//     |p|^2 - (p.b)^2 < r^2, i.e. x^2 + y^2 + h^2 - (x sin alpha + h cos alpha)^2 < r^2.
//     Subtracting the two: inside BOTH iff y^2 < r^2 - x^2 - (x cos alpha - h sin alpha)^2,
//     so the cross-section of A_inf ∩ B_inf perpendicular to A's axis at
//     height h is non-empty iff |h sin alpha - x cos alpha| < r for some
//     |x| < r - minimizing over x, iff |h| sin alpha < r(1 + |cos alpha|),
//     i.e. |h| < r(1 + |cos alpha|)/sin alpha = r*max(cot(alpha/2), tan(alpha/2))
//     = amplitude_max, the very bound ComputeSteinmetzCrossing's extent
//     precondition uses (this is EXACT: it is the Steinmetz solid's full
//     axial extent, not an over-estimate). By symmetry the same holds
//     along B's axis with the same amplitude. Since
//     A_fin ∩ B_fin ⊆ A_inf ∩ B_inf, if for EITHER cylinder the band
//     [h_Q - amplitude_max, h_Q + amplitude_max] misses that cylinder's own
//     axial band [lo, hi], the finite pair cannot meet. This is exact
//     along each axis separately, so it never refuses a pair that one
//     axis alone can separate; when BOTH bands overlap their extents the
//     pair generally does meet (always so at alpha = 90 degrees, where
//     the solid's axial tips are whole chords rather than points), and
//     the existing extent precondition decides between "split" and
//     "refuse" as before. The three regimes tile the axis with no gap and
//     no overlap: no-interaction below -amplitude_max - tol or above
//     length + amplitude_max + tol, split strictly inside
//     (amplitude_max + tol, length - amplitude_max - tol), refuse between.
//
// A fragment's own axial band is [0, length] widened, for a notched
// fragment, to cover its notch curves - the same widening
// Brep::FromMixedFaces() applies to the surface's v-domain (see
// CylindricalFace's own doc comment in brep.h), plus twice the notch's
// own sagitta bound: the polyline's extreme heights can undershoot the
// true ellipse's by at most one segment sagitta (the h-component of the
// chord-midpoint deviation, at the segment holding the curve's own
// extremum), doubled here for a margin that costs nothing. Full-sweep is
// never assumed either: a partial sweep is a subset of the full circle,
// so both tests stay conservative for every CylindricalFace shape this
// pipeline produces, which is why this check runs BEFORE the Steinmetz
// split's own partial-sweep/already-notched guards - those guards refuse
// an INTERACTION they cannot represent, and a pair that provably never
// interacts needs no representation at all.
//
// Argument-order symmetry: SplitAndBucketMixed(fa, fb) and (fb, fa) both
// reach this with the same two original faces, and the verdict must be
// identical in both directions (a fragment passing through in one
// direction while its partner is refused in the other would throw from
// one of the two calls anyway). Both tests are symmetric functions of the
// pair mathematically, and the pair is put into SteinmetzCanonicalFirst's
// argument-order-independent order first so the floating-point arithmetic
// is bit-identical too - no verdict can flip on a rounding difference at
// the exact boundary.

// Closest distance between the segments [p1, q1] and [p2, q2] - Ericson's
// ClosestPtSegmentSegment (see the section comment above), degenerate
// (point-like) segments included.
double SegmentSegmentDistance(const Point3d& p1, const Point3d& q1, const Point3d& p2, const Point3d& q2) {
  constexpr double kTiny = 1e-30;  // squared length below which a segment is treated as a point
  auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
  const Vector3d d1 = q1 - p1;
  const Vector3d d2 = q2 - p2;
  const Vector3d r = p1 - p2;
  const double a = ON_DotProduct(d1, d1);
  const double e = ON_DotProduct(d2, d2);
  const double f = ON_DotProduct(d2, r);
  double s = 0.0, t = 0.0;
  if (a <= kTiny && e <= kTiny) {
    // both segments are points
  } else if (a <= kTiny) {
    t = clamp01(f / e);
  } else {
    const double c = ON_DotProduct(d1, r);
    if (e <= kTiny) {
      s = clamp01(-c / a);
    } else {
      const double b = ON_DotProduct(d1, d2);
      const double denom = a * e - b * b;  // >= 0 by Cauchy-Schwarz, 0 iff parallel
      s = denom > 0.0 ? clamp01((b * f - c * e) / denom) : 0.0;
      t = (b * s + f) / e;
      if (t < 0.0) {
        t = 0.0;
        s = clamp01(-c / a);
      } else if (t > 1.0) {
        t = 1.0;
        s = clamp01((b - c) / a);
      }
    }
  }
  return (p1 + s * d1).DistanceTo(p2 + t * d2);
}

bool NonParallelCylinderPairNoInteraction(const Brep::CylindricalFace& cf_a, const Brep::CylindricalFace& cf_b,
                                          double tol) {
  const bool a_first = SteinmetzCanonicalFirst(cf_a, cf_b);
  const Brep::CylindricalFace& p = a_first ? cf_a : cf_b;
  const Brep::CylindricalFace& q = a_first ? cf_b : cf_a;
  const AxialBand band_p = CylindricalFragmentAxialBand(p);
  const AxialBand band_q = CylindricalFragmentAxialBand(q);

  // (1) Capsule separation - any pair.
  const double segment_distance =
      SegmentSegmentDistance(p.frame.origin + band_p.lo * p.frame.zaxis, p.frame.origin + band_p.hi * p.frame.zaxis,
                             q.frame.origin + band_q.lo * q.frame.zaxis, q.frame.origin + band_q.hi * q.frame.zaxis);
  if (segment_distance > p.radius + q.radius + tol) return true;

  // (2) Steinmetz axial band - equal radii, intersecting axes only, with
  // the SAME regime tests ComputeSteinmetzCrossing applies.
  if (std::fabs(p.radius - q.radius) > CylinderPairRadiusTolerance(p, q, tol)) return false;
  const Vector3d a = p.frame.zaxis;
  const Vector3d b = q.frame.zaxis;
  const Vector3d w = p.frame.origin - q.frame.origin;
  const double d_ab = ON_DotProduct(a, b);
  const double d_aw = ON_DotProduct(a, w);
  const double d_bw = ON_DotProduct(b, w);
  const double denom = 1.0 - d_ab * d_ab;  // sin^2(alpha) - the dispatch only sends non-parallel pairs here
  if (!(denom > 0.0)) return false;
  const double s = (d_ab * d_bw - d_aw) / denom;
  const double t = (d_bw - d_ab * d_aw) / denom;
  const Point3d on_p = p.frame.origin + s * a;
  const Point3d on_q = q.frame.origin + t * b;
  if (on_p.DistanceTo(on_q) > tol) return false;  // genuinely skew - only the capsule test applies
  const double alpha = std::acos(std::max(-1.0, std::min(1.0, d_ab)));
  const double half = 0.5 * alpha;
  const double amplitude_max = p.radius * std::max(1.0 / std::tan(half), std::tan(half));
  const Point3d crossing = on_p;  // ComputeSteinmetzCrossing's own Q: on the canonical cylinder's axis
  auto band_misses = [&](const Brep::CylindricalFace& c, const AxialBand& band) {
    const double h_q = ON_DotProduct(crossing - c.frame.origin, c.frame.zaxis);
    return h_q + amplitude_max < band.lo - tol || h_q - amplitude_max > band.hi + tol;
  };
  return band_misses(p, band_p) || band_misses(q, band_q);
}

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

          // The inside-circle disc, ONLY when g.cyl's own finite axial range
          // genuinely reaches this plane MID-LENGTH - not just touching an
          // already-original, unsplit end (that case is already correctly
          // closed by SynthesizeEndCaps, see boolean.h's own doc comment).
          // Mirrors case (iii)'s own identical `v_cut` condition below
          // exactly, computed directly from `g.cyl` rather than from any
          // fragment case (iii) may or may not have produced elsewhere -
          // the two conditions are symmetric by construction (plane.origin
          // minus cylinder.origin, dotted with the cylinder's own axis),
          // not coincidentally similar.
          //
          // Deliberately NOT gated on BooleanOp: SplitMixedAgainstAllFaces
          // stays fully op-agnostic (see boolean.h's own BooleanCombineMixed
          // doc comment for the full reasoning) - the disc's own
          // representative point always classifies kIn against the OTHER
          // operand (it sits strictly inside the crossing cylinder's
          // occupied volume by construction), so the EXISTING
          // classify-then-bucket switch in BooleanCombineMixed already
          // discards it for Union/Difference-from-this-side and keeps it
          // for Intersection, with zero new op-specific logic anywhere in
          // this shared split pipeline - the same reuse-not-reimplement
          // principle this codebase's own case (i)/(ii)/(iii) dispatch
          // already follows throughout.
          const double v_cut = ON_DotProduct(f.planar.plane.origin - g.cyl.frame.origin, g.cyl.frame.zaxis);
          if (v_cut > tol && v_cut < g.cyl.length - tol) {
            for (std::vector<Point3d>& piece : detail::ClipPolygonByCircleInsideOnly3d(
                     f.planar.loop, f.planar.plane, proj_center, g.cyl.radius, tol)) {
              if (piece.size() < 3) continue;
              MixedFace m;
              m.planar.plane = f.planar.plane;
              if (std::optional<Brep::PlanarFace::ArcRun> run =
                      FindArcRun(piece, proj_center, g.cyl.radius, f.planar.plane, tol)) {
                m.planar.arc_runs.push_back(*run);
              }
              m.planar.loop = std::move(piece);
              next.push_back(std::move(m));
            }
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
          std::vector<std::pair<int, int>> ellipse_runs;
          std::vector<std::vector<Point3d>> oblique_pieces =
              detail::ClipPolygonByEllipse3d(f.planar.loop, f.planar.plane, ef, tol, 200, &ellipse_runs);
          for (size_t pi = 0; pi < oblique_pieces.size(); ++pi) {
            std::vector<Point3d>& piece = oblique_pieces[pi];
            if (piece.size() < 3) continue;
            MixedFace m;
            m.planar.plane = f.planar.plane;
            // Unlike the perpendicular branch above (whose FindArcRun
            // re-detects a CIRCLE by radius so TessellateConforming() can
            // re-sample it via detail::ArcSchedule3d), the ellipse has no
            // circle-style resampling - and needs none: the piece's own
            // ellipse stretch already IS the canonical sample list the
            // adjoining cylindrical fragment carries verbatim in its
            // cap0_notch_points/cap1_notch_points (both are the same
            // EllipsePointAt evaluations over the same `ef`, see
            // ellipse_clip3d.h). So the run is recorded as a LITERAL
            // ArcRun (PlanarFace::ArcRun::literal_points): the exact
            // points, in loop order, that TessellateConforming()
            // substitutes for the wedge's boundary and ear-clips around,
            // making the planar/cylinder ellipse seam bit-identical on
            // both sides. Consumed ONLY by TessellateConforming(); every
            // other consumer of this MixedFace never reads arc_runs (the
            // one arc-specific reader, RepresentativeInteriorPointMixed's
            // inside-disc shortcut, explicitly skips literal runs), and
            // ordinary Tessellate() still carries the pre-existing,
            // disclosed non-watertight wedge seam the perpendicular case
            // has too (see boolean.h's own BooleanCombineMixed doc
            // comment).
            if (pi < ellipse_runs.size()) {
              Brep::PlanarFace::ArcRun run;
              run.begin = ellipse_runs[pi].first;
              run.count = ellipse_runs[pi].second;
              const size_t pn = piece.size();
              run.literal_points.reserve(static_cast<size_t>(run.count));
              for (int j = 0; j < run.count; ++j) {
                run.literal_points.push_back(piece[(static_cast<size_t>(run.begin) + static_cast<size_t>(j)) % pn]);
              }
              m.planar.arc_runs.push_back(std::move(run));
            }
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
        // Case (iv): both cylindrical. PARALLEL axes (checked via
        // cross-product-near-zero on the two unit axis directions, which
        // correctly catches both same-direction and anti-parallel axes in
        // one test - both frame.zaxis are already unit by construction of
        // every ON_Plane-backed frame this pipeline builds) get the new
        // closed-form circle/circle angular split this increment adds; a
        // non-parallel pair is first tested for provable non-interaction,
        // then routed by radius: unequal radii to the intersecting-axis
        // split at any axis angle (SplitCylindricalByUnequalCylinder),
        // equal radii to the Steinmetz split at any axis angle
        // (SplitCylindricalBySteinmetzCylinder) - both decompose each wall
        // into shapes CylindricalFace already represents (the region of
        // one wall inside the other cylinder touches the rest of the wall
        // only at pinch points, so no interior-trim representation is
        // needed; see both section comments). What remains out of scope
        // - unequal radii at a general axis angle, skew axes, partial
        // penetration - is refused by those splits' own guards, each for
        // the specific reason named there (see this function's own doc
        // comment in boolean.h for the full, disclosed reasoning).
        const Vector3d cross_axes = ON_CrossProduct(f.cyl.frame.zaxis, g.cyl.frame.zaxis);
        const bool axes_parallel = cross_axes.Length() < kAxisAlignTol;
        if (axes_parallel) {
          // The existing angular split (unchanged - crossing regime: 2
          // children; disjoint/nested regime: 1 unmodified fragment), THEN
          // - new for this increment's own Intersection/Difference support
          // - an axial split of each resulting piece against `g.cyl`'s own
          // finite axial termini (SplitCylindricalByOtherCylinderAxialExtent,
          // above): unconditionally in the CROSSING regime (angular.size()
          // > 1 - applied to BOTH children, not just the one that dips
          // into `g.cyl`, for a reason found only by direct
          // counterexample, see below), or in the NESTED/disjoint regime
          // (angular.size() == 1) only when `f.cyl` is itself the member
          // wholly nested inside `g.cyl` (checked directly below, NOT the
          // reverse - see SplitCylindricalByOtherCylinderAxialExtent's own
          // doc comment for why applying this to the OUTER member of a
          // nested pair is both unnecessary and would silently change
          // existing, already-verified fragment counts).
          //
          // Why BOTH crossing-regime children, not just the one that can
          // be radially inside `g.cyl` (an earlier, narrower version of
          // this dispatch's own reasoning, since corrected by direct
          // counterexample): the WALL is not the only thing that needs to
          // reconnect correctly. Consider `f.cyl`'s OUTSIDE-`g.cyl` child
          // - angularly always outside `g.cyl`'s footprint, so its own
          // CLASSIFICATION against `g.cyl` is genuinely height-invariant
          // (always kOut), but its own two straight RADIAL RAILS (at its
          // shared boundary angles with the INSIDE child, i.e. exactly at
          // `g.cyl`'s own crossing points) are NOT height-invariant
          // structurally: wherever the INSIDE child's own middle axial
          // band gets discarded (the genuine A-inside-B region, excluded
          // from Difference/Intersection's own kept buckets) and replaced
          // by `g.cyl`'s OWN wall instead, `g.cyl`'s wall's own rail (at
          // that SAME crossing angle, but spanning only ITS OWN finite
          // axial reach) needs a correctly length-matched rail segment
          // from the OUTSIDE child to weld against - which only exists if
          // the OUTSIDE child is ALSO cut at the same two heights.
          // Skipping this (an earlier version of this fix did, to dodge a
          // DIFFERENT bug - see immediately below) leaves the outside
          // child's own single, full-length rail with no correctly-sized
          // partner there: a genuine open seam, caught directly by this
          // increment's own Difference test's own IsClosedManifold()/
          // volume checks (not merely inferred).
          //
          // This does reintroduce a SEPARATE, genuinely new ambiguity
          // this increment's own implementation found and fixed at its
          // real source instead of working around here: once BOTH
          // children are axially split, the SHORT arc (inside child's own
          // cap-isocurve boundary at the cut height) and the LONG arc
          // (outside child's own two axially-adjacent pieces reconnecting
          // at that SAME height) share the identical two endpoint
          // vertices (the crossing points) - and Brep::FromMixedFaces()
          // used to identify an edge PURELY by that vertex pair, so a
          // short arc, a long arc, and that long arc's own self-pairing
          // partner all collided under one key, throwing "edge shared by
          // 3 or more faces". Fixed at the actual source
          // (BuildFaceLoop's own edge key, brep.cpp) by additionally
          // hashing each cap edge's own midpoint position - see that
          // function's own doc comment for the full derivation - rather
          // than worked around here by narrowing which pieces get split
          // (which merely swaps one open-seam bug for a different one, as
          // this increment's own history directly demonstrates).
          std::vector<MixedFace> angular = SplitCylindricalByParallelCylinder(f.cyl, g.cyl, tol);
          bool need_axial_split = angular.size() > 1;
          if (!need_axial_split) {
            const Vector3d dd = g.cyl.frame.origin - f.cyl.frame.origin;
            const double bx = ON_DotProduct(dd, f.cyl.frame.xaxis);
            const double by = ON_DotProduct(dd, f.cyl.frame.yaxis);
            const double dist = std::sqrt(bx * bx + by * by);
            need_axial_split = (dist + f.cyl.radius <= g.cyl.radius + tol);
          }
          for (MixedFace& piece : angular) {
            if (need_axial_split) {
              for (Brep::CylindricalFace& sub : SplitCylindricalByOtherCylinderAxialExtent(piece.cyl, g.cyl, tol)) {
                next.push_back(MixedFaceFromCyl(std::move(sub)));
              }
            } else {
              next.push_back(std::move(piece));
            }
          }
        } else if (NonParallelCylinderPairNoInteraction(f.cyl, g.cyl, tol)) {
          // Non-parallel axes, but the two FINITE cylinders provably never
          // meet (capsule separation for any pair, or - for an equal-
          // radius intersecting-axes pair - the Steinmetz solid's own
          // exact axial band missing either cylinder's extent; see the
          // section comment above NonParallelCylinderPairNoInteraction):
          // the fragment passes through unchanged, exactly the way
          // SplitCylindricalByParallelCylinder returns a disjoint
          // parallel-axis fragment untouched, and the existing generic
          // classifier reads it kOut against the other solid downstream.
          // Checked BEFORE the Steinmetz split's own guards, so a pair
          // that never interacts is never refused for a property (unequal
          // radii, skew axes, an end short of the crossing, a partial
          // sweep, an existing notch) that only matters when it does.
          next.push_back(std::move(f));
        } else if (std::fabs(f.cyl.radius - g.cyl.radius) > CylinderPairRadiusTolerance(f.cyl, g.cyl, tol)) {
          // Non-parallel axes, UNEQUAL radii: the intersecting-axis split
          // at any axis angle - see SplitCylindricalByUnequalCylinder's
          // own section comment above for the per-angle closed form on
          // the smaller cylinder, the ten-piece (larger) / six-piece
          // (smaller) decomposition, the sloped cut of the larger
          // cylinder's plain pieces at a general angle and the level fast
          // path at a right angle. Skew axes are supported too, when the
          // smaller cylinder fully pierces the larger (d + r_b < r_a, d
          // the axes' closest-point distance). Its own guards throw
          // std::invalid_argument (each naming "non-parallel axes" and
          // "UNEQUAL radii") for a skew pair that does NOT fully pierce
          // (a partial penetration), a crossing not strictly interior to
          // both cylinders, a partial-sweep or already-notched operand,
          // or a sliver - again only for pairs the no-interaction test
          // above could not separate.
          for (MixedFace& piece : SplitCylindricalByUnequalCylinder(f.cyl, g.cyl, tol)) {
            next.push_back(std::move(piece));
          }
        } else {
          // Non-parallel axes, equal radii: the Steinmetz (intersecting-
          // axes) split - see SplitCylindricalBySteinmetzCylinder's own
          // section comment above for the closed-form two-ellipse
          // decomposition into four half-bands and two eyes. Its own
          // guards throw std::invalid_argument (each naming "non-parallel
          // axes") for genuinely skew axes, a crossing not strictly
          // interior to both cylinders, or a partial-sweep operand - now
          // only for pairs the no-interaction test above could not
          // separate, i.e. pairs that genuinely (or, for the conservative
          // capsule test, possibly) touch; the equal-radius skew
          // INTERACTION remains out of scope.
          for (MixedFace& piece : SplitCylindricalBySteinmetzCylinder(f.cyl, g.cyl, tol)) {
            next.push_back(std::move(piece));
          }
        }
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
// -cf.frame.zaxis (mirrored, a reflection).
//
// The mirrored (same_handed == false) case maps disc-local angle `theta`
// to PHYSICAL cf.frame angle `cf.angle - theta`, NOT the naive `-theta` a
// prior version of this function used (#61's own original increment,
// which only ever built this for a FULL 2*pi sweep). Both choices trace
// the correct set of points and correct winding for a FULL 2*pi sweep -
// `cos(2*pi - theta) == cos(-theta)` and `sin(2*pi - theta) == sin(-theta)`
// are exact trig identities, so the two formulas are bit-identical there,
// and every one of #61/#62's own already-verified full-circle tests stay
// completely unaffected (this is a real, checked substitution, not an
// assumption). But for a genuinely PARTIAL sweep - unreachable before
// this increment's own relaxed guard above, since every prior producer of
// a CylindricalFace boolean operand built only full circles - `-theta`
// and `cf.angle - theta` are NOT the same: `-theta` sweeps disc-local
// [0, cf.angle] to PHYSICAL [-cf.angle, 0], a completely DIFFERENT
// angular range than the wall's own actual rail corners at PHYSICAL
// [0, cf.angle], while `cf.angle - theta` correctly keeps BOTH endpoints
// (disc-local 0 and cf.angle) pinned to the wall's own true PHYSICAL rail
// corners (0 and cf.angle) and only reverses the INTERIOR traversal
// order - the genuinely intended effect of "same_handed == false", now
// achieved without also corrupting which physical angles get covered.
// This is a real, previously-latent bug this increment's own relaxed
// guard newly exposed (not introduced): confirmed directly by building a
// two-wedge parallel-cylinder Union with the two formulas swapped back and
// forth - the naive `-theta` version measures a wildly wrong tessellated
// volume and a non-closed mesh (the mirrored end's cap silently covers
// the WRONG angular range, misaligned with its own wall's rail), while
// `cf.angle - theta` measures the correct closed-form volume and a
// genuinely closed manifold - see
// TestBooleanCombineMixedParallelCylinderUnionAxiallyDisjointBothEndsCapped's
// own comment in the test file for the falsifiable claim this fix makes
// true.
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
  // Was restricted to a FULL-SWEEP (angle == 2*pi) `cf` only; relaxed here
  // (the parallel-axis cylinder/cylinder increment's own addition) to any
  // genuinely positive sweep, once inspection of this function's own body
  // confirmed the restriction was conservative, not structural: every
  // wedge below is already built as [center, arc_pt_0, ..., arc_pt_N] with
  // an implicit closing edge back to `center` - i.e. every wedge, whether
  // an interior chunk of a full circle or one of the two BOUNDARY wedges
  // of a partial sweep, already carries its own two straight radial edges
  // (center->arc_start, arc_end->center via the implicit close). For a
  // full 2*pi sweep those boundary wedges' radial edges are internal
  // diagonals, welded away by the adjacent wedges tiling the whole disc
  // (exactly ClipPolygonByCircle3d's own 4-wedge pattern); for a
  // genuinely partial `cf.angle` (now reachable here: the surviving
  // angular child of a SplitCylindricalByParallelCylinder split), the
  // FIRST wedge's own center->arc(0) edge and the LAST wedge's own
  // arc(cf.angle)->center edge become real boundary edges of a pie-slice
  // cap - and they are already EXACT (straight lines, built from the same
  // PointOnCylFace the adjoining cylindrical wall's own rail at that same
  // angle already uses - no polygonal-approximation notch needed, since a
  // straight radial edge has no curvature to approximate). The
  // kQuadrants/per_quadrant wedge-count logic below already divides
  // `cf.angle` (whatever it is) into 4 equal pieces and needs no change at
  // all for this: it continues to work identically for cf.angle == 2*pi
  // (existing, already-verified behavior, completely untouched by this
  // guard relaxation) and now also produces a geometrically valid partial
  // pie-slice for any 0 < cf.angle < 2*pi.
  if (!(cf.angle > kAxisAlignTol)) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: synthesizing an end cap for a "
        "cylindrical fragment with zero (or near-zero) swept angle is a "
        "degenerate no-op, not a real cap - see BuildEndCap's own doc "
        "comment in boolean.cpp");
  }

  const double height = at_v0 ? 0.0 : cf.length;
  const Point3d center = cf.frame.origin + height * cf.frame.zaxis;
  const bool same_handed = at_v0 ? !cf.outward : cf.outward;  // true iff target normal == +cf.frame.zaxis

  ON_Plane plane;
  plane.origin = center;
  if (same_handed) {
    plane.xaxis = cf.frame.xaxis;
    plane.yaxis = cf.frame.yaxis;
  } else {
    // Chosen so that, for EVERY plane_theta (not just the full-circle
    // special case), `cos(plane_theta)*plane.xaxis +
    // sin(plane_theta)*plane.yaxis` is EXACTLY the same 3D point as
    // `PointOnCylFace(cf, cf.angle - plane_theta, height)` - the actual
    // formula the loop below uses to build this cap's own real boundary
    // points (see this function's own "Orientation" doc comment above for
    // why the mirrored case maps plane_theta to physical angle
    // `cf.angle - plane_theta`, not merely `-plane_theta`). Derived by
    // direct trig expansion of `cos(cf.angle - theta)*cf.frame.xaxis +
    // sin(cf.angle - theta)*cf.frame.yaxis` into
    // `cos(theta)*[cos(cf.angle)*xaxis + sin(cf.angle)*yaxis] +
    // sin(theta)*[sin(cf.angle)*xaxis - cos(cf.angle)*yaxis]` - i.e.
    // exactly the coefficients of cos(theta)/sin(theta) below - and
    // verified orthonormal (both unit length, mutually perpendicular) by
    // direct substitution using cf.frame.xaxis/yaxis's own orthonormality.
    // This basis is WHAT `run.plane_xaxis`/`run.plane_yaxis` below get set
    // to, so that Brep::TessellateConforming()'s own INDEPENDENT
    // recomputation of this same boundary (detail::ArcSchedule3d, which
    // evaluates purely from run.center/radius/plane_xaxis/plane_yaxis/
    // angle_begin/angle_end, with no knowledge of `cf` or `same_handed` at
    // all) reproduces the IDENTICAL points this loop already computed,
    // not a second, independently-drifting approximation of them - the
    // same "single canonical producer, shared unchanged" principle this
    // codebase's own detail::ellipse_clip3d.h/circle_clip3d.h headers
    // already document. For cf.angle == 2*pi exactly this reduces (via
    // cos(2*pi)=1, sin(2*pi)=0) to plane.xaxis=cf.frame.xaxis,
    // plane.yaxis=-cf.frame.yaxis - bit-identical to this function's own
    // prior, already-verified full-circle-only behavior, so every
    // pre-existing full-sweep caller is completely unaffected.
    const double ca = std::cos(cf.angle), sa = std::sin(cf.angle);
    plane.xaxis = ca * cf.frame.xaxis + sa * cf.frame.yaxis;
    plane.yaxis = sa * cf.frame.xaxis - ca * cf.frame.yaxis;
  }
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
      const double physical_theta = same_handed ? plane_theta : (cf.angle - plane_theta);
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
//
// `needed_class` is the probe classification (against `other`) that means
// "this original end needs a synthesized cap" - PointClass::kOut for the
// Union/boss caller below (nothing in EITHER operand continues past this
// end, so the union boundary genuinely terminates here and needs its own
// disk), but the OPPOSITE, PointClass::kIn, for the BooleanOp::Intersection
// caller (see BooleanCombineMixed's own switch statement below and
// boolean.h's own doc comment for the worked argument): an
// Intersection-kept fragment's own original end means THIS solid's
// material stops there regardless of the other operand, so a probe that's
// kIn (the other operand's material keeps going past the point where this
// fragment's own material stops) is exactly the case where nothing else in
// the result bounds A∩B there - the other operand's own surface is
// interior, not a boundary, at that point - and a cap is needed; a probe
// that's kOut at an original end means the other operand doesn't reach
// past there either, consistent with "already sealed or not actually
// reached" (a genuine crossing would have produced a split, clearing
// end{0,1}_is_original), so no cap is added. Defaults to kOut so every
// pre-existing two-argument call site (the Union branch) is completely
// unaffected - same probes, same classification, same faces produced.
// True iff none of `other`'s own PARALLEL-axis cylindrical faces that
// genuinely interact with `cf` (CylinderCylinderNoInteraction false - a
// disjoint or fully-nested-and-never-touching pair is skipped entirely,
// since a cap can never dip into a footprint it never reaches) require the
// disclosed cap-trim gap ParallelCylinderCapNeedsNoTrim's own doc comment
// describes. Non-parallel-axis cylindrical faces in `other` are skipped
// too (irrelevant to this specific guard - only reachable if `other` holds
// faces from a solid this increment's own case (iv) dispatch never
// actually split `cf` against, e.g. an unrelated feature elsewhere on the
// same Brep).
bool ParallelCylinderCapSafeAgainstAll(const Brep::CylindricalFace& cf, bool at_v0,
                                        const std::vector<MixedFace>& other, double tol) {
  for (const MixedFace& g : other) {
    if (!g.is_cyl) continue;
    const Vector3d cross_axes = ON_CrossProduct(cf.frame.zaxis, g.cyl.frame.zaxis);
    if (cross_axes.Length() >= kAxisAlignTol) continue;  // not parallel - irrelevant to this guard
    if (CylinderCylinderNoInteraction(cf, g.cyl, tol)) continue;  // never touches radially - always safe
    if (!ParallelCylinderCapNeedsNoTrim(cf, at_v0, g.cyl, tol)) return false;
  }
  return true;
}

// Builds the lens-shaped Intersection/Difference end cap that closes
// cylinder `cf`'s own genuine terminus (`at_v0` selects which end) where
// it is cut off by a DIFFERENT, PARALLEL-axis cylinder `other` that
// genuinely CROSSES `cf` (a real 2-point circle/circle crossing - not
// disjoint, not tangent, not one nested inside the other) and whose own
// finite axial range reaches this end's height mid-length
// (ParallelCylinderCapNeedsNoTrim returning false is exactly this
// trigger - see SynthesizeEndCaps' own kIn-branch doc comment below for
// why the ORDINARY on-axis probe cannot see this case at all).
//
// Re-derived directly, not merely following the prior research phase's
// own proposal: that probe sits ON `cf`'s own axis, which for a genuinely
// CROSSING pair generally sits OUTSIDE `other` entirely (the two axes are
// `dist` apart, with `|r_a - r_b| < dist < r_a + r_b` - `cf`'s own axis
// only has to be within `other.radius` of `other`'s axis for the probe to
// read kIn, which is NOT implied by a genuine crossing at all). Confirmed
// by direct counterexample, not merely theorized: two parallel cylinders
// of radius 3 and 2, axes 4 apart (a genuine crossing: 3+2=5 > 4 > 3-2=1),
// have a real, non-empty lens-shaped intersection at every height in their
// axial overlap band, yet BOTH cylinders' own axis points sit strictly
// outside the OTHER cylinder (distance 4 exceeds either radius) - see this
// increment's own test file for the worked volume check this exact
// configuration is verified against.
//
// The lens cross-section is the classical two-circle "lens" (Weisstein,
// MathWorld, "Circle-Circle Intersection"; Bourke, "Intersection of two
// circles," 1997) - built here as TWO simple, non-self-touching circular-
// segment pieces meeting at the chord between the two crossing points,
// mirroring the "several simple pieces sharing an internal cut, never one
// bridged loop" principle circle_clip3d.h's own top comment already
// establishes for an analogous problem: one piece is bounded by the arc
// of `cf`'s own circle that lies inside `other`, plus the straight chord;
// the other is the mirror, bounded by the arc of `other`'s own circle
// that lies inside `cf`, plus the SAME chord, traversed in the opposite
// direction so the two pieces share that edge with opposite orientation
// (weldable, not two unrelated pieces) - the same convention BuildEndCap's
// own 4 quadrant wedges already use for their shared internal radial cuts.
// Each piece carries exactly ONE PlanarFace::ArcRun (never two arcs in one
// loop - existing `detail::ClipPolygonByCircle3d`/`ClipPolygonByCircleInsideOnly3d`
// were checked directly and cannot be reused here at all: BOTH explicitly
// throw whenever the clip circle crosses the polygon boundary, which is
// exactly what a genuine two-circle lens does by definition - so this is
// genuinely new geometry, not a case of under-using existing machinery),
// so Brep::TessellateConforming()'s existing, unmodified reconciliation
// machinery needs no new case to weld each piece's own arc edge against
// the matching CylindricalFace wall wedge's own rail, exactly the way it
// already does for a plain pie-slice cap (BuildEndCap) or an inside-circle
// disc (case (ii)'s own mid-length crossing producer).
//
// Reuses ComputeParallelCylinderCrossing (above) for the actual
// intersection points/angles - the SAME computation
// SplitCylindricalByParallelCylinder's own angular wall split already
// uses - rather than an independent, potentially-drifting re-derivation,
// so this cap's own corner vertices are computed via the EXACT SAME raw
// angle values (bit-identical, not merely close) as the adjoining wall
// wedge's own rail corners: both ultimately evaluate
// `radius*(cos(theta)*xaxis+sin(theta)*yaxis)` at one of
// `crossing.theta_p1_cf`/`theta_p2_cf`, unmodified - see `build_segment`
// below for why only the interior samples (never the two loop endpoints)
// go through a `+/-2*pi` wrap adjustment.
//
// Winding: rather than hand-deriving which of the two candidate arcs'
// sweep DIRECTIONS produces the correct outward orientation for every
// possible relative circle placement (a genuinely easy-to-get-backwards
// derivation - see AngleOffsetBetweenFrames/ConvertAngleBetweenFrames's
// own doc comment, arc_schedule3d.h, for a documented real regression of
// exactly this kind elsewhere in this codebase), this determines ONE
// global reversal flag from segment 1's own natural sweep direction versus
// the target outward normal (both expressed in `cf`'s own right-handed
// frame, where "increasing angle" is unambiguously CCW as seen from
// `+cf.frame.zaxis` by construction) and applies that SAME flag to BOTH
// segments - reversing only segment 1 (or only segment 2) would break the
// P1<->P2 shared-chord identity between them; reversing both preserves it
// while still correctly flipping the whole boundary's orientation when
// needed.
std::vector<MixedFace> BuildLensEndCap(const Brep::CylindricalFace& cf, bool at_v0,
                                        const Brep::CylindricalFace& other, double tol, int arc_samples = 200) {
  const ParallelCylinderCrossing crossing = ComputeParallelCylinderCrossing(cf, other, tol);
  if (!crossing.crosses) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineMixed: BuildLensEndCap called on a "
        "non-crossing parallel-axis cylinder pair - a caller bug (see "
        "SynthesizeEndCaps' own doc comment, which only ever calls this "
        "after confirming ComputeParallelCylinderCrossing(...).crosses)");
  }

  const double height = at_v0 ? 0.0 : cf.length;
  const bool same_handed = at_v0 ? !cf.outward : cf.outward;  // true iff target normal == +cf.frame.zaxis (BuildEndCap's own convention)

  const Point3d cf_cap_center = cf.frame.origin + height * cf.frame.zaxis;
  const Point3d other_axis_here =
      other.frame.origin + ON_DotProduct(cf_cap_center - other.frame.origin, other.frame.zaxis) * other.frame.zaxis;
  auto point_on_other = [&](double theta) {
    return other_axis_here + other.radius * (std::cos(theta) * other.frame.xaxis + std::sin(theta) * other.frame.yaxis);
  };

  auto inside_other = [&](const Point3d& p) {
    const Vector3d rel = p - other.frame.origin;
    const Vector3d perp = rel - ON_DotProduct(rel, other.frame.zaxis) * other.frame.zaxis;
    return perp.Length() < other.radius - tol;
  };
  auto inside_cf = [&](const Point3d& p) {
    const Vector3d rel = p - cf.frame.origin;
    const Vector3d perp = rel - ON_DotProduct(rel, cf.frame.zaxis) * cf.frame.zaxis;
    return perp.Length() < cf.radius - tol;
  };

  // For each circle, of the two arcs connecting its own two raw crossing
  // angles, pick whichever one's MIDPOINT genuinely lies inside the other
  // cylinder (not assumed - a lens's own "inside" arc is not always the
  // numerically shorter one). Expressed as a possibly-`+/-2*pi`-shifted
  // "end" value purely to encode WHICH arc/direction was chosen; the raw,
  // unshifted values are used again below for the loop's own two boundary
  // samples.
  auto pick_inside_arc_end = [](double begin_raw, double end_raw, const std::function<bool(double)>& midpoint_inside) {
    double end_direct = end_raw;
    while (end_direct < begin_raw) end_direct += 2.0 * ON_PI;
    const double mid = 0.5 * (begin_raw + end_direct);
    return midpoint_inside(mid) ? end_direct : (end_direct - 2.0 * ON_PI);
  };
  const double seg1_end_shifted = pick_inside_arc_end(
      crossing.theta_p1_cf, crossing.theta_p2_cf,
      [&](double mid) { return inside_other(PointOnCylFace(cf, mid, height)); });
  const double seg2_end_shifted = pick_inside_arc_end(
      crossing.theta_p2_other, crossing.theta_p1_other,
      [&](double mid) { return inside_cf(point_on_other(mid)); });

  // Segment 1's own NATURAL sweep (P1 -> P2, via cf's own inside arc):
  // positive iff that sweep is increasing angle in cf's own right-handed
  // frame, i.e. CCW as seen from +cf.frame.zaxis.
  const bool natural_ccw_from_plus_cf_z = (seg1_end_shifted - crossing.theta_p1_cf) > 0.0;
  const bool reverse_both = (natural_ccw_from_plus_cf_z != same_handed);

  auto build_segment = [&](double natural_raw_first, double natural_raw_last, double natural_interp_last,
                            bool reverse, const std::function<Point3d(double)>& eval) {
    const double raw_first = reverse ? natural_raw_last : natural_raw_first;
    const double raw_last = reverse ? natural_raw_first : natural_raw_last;
    const double interp_first = reverse ? natural_interp_last : natural_raw_first;
    const double interp_last = reverse ? natural_raw_first : natural_interp_last;
    std::vector<Point3d> loop;
    loop.reserve(static_cast<size_t>(arc_samples) + 1);
    for (int s = 0; s <= arc_samples; ++s) {
      if (s == 0) {
        loop.push_back(eval(raw_first));
      } else if (s == arc_samples) {
        loop.push_back(eval(raw_last));
      } else {
        const double t = static_cast<double>(s) / static_cast<double>(arc_samples);
        loop.push_back(eval(interp_first + (interp_last - interp_first) * t));
      }
    }
    return loop;
  };

  std::vector<Point3d> loop1 = build_segment(
      crossing.theta_p1_cf, crossing.theta_p2_cf, seg1_end_shifted, reverse_both,
      [&](double theta) { return PointOnCylFace(cf, theta, height); });
  std::vector<Point3d> loop2 =
      build_segment(crossing.theta_p2_other, crossing.theta_p1_other, seg2_end_shifted, reverse_both, point_on_other);
  const int loop1_arc_count = static_cast<int>(loop1.size());
  const int loop2_arc_count = static_cast<int>(loop2.size());

  // Neither loop is closed by a direct chord between its own two arc
  // endpoints (P1<->P2 directly) - instead an extra, genuinely interior
  // vertex `chord_mid` (the chord's own midpoint - any point strictly off
  // both circles works equally; the midpoint is simplest) is appended to
  // EACH loop, splitting what would otherwise be one P1<->P2 edge into two
  // (P2->mid, mid->P1 for one loop; the reverse for the other) - still
  // exactly the same straight chord geometrically (both sub-edges are
  // exactly colinear with it, zero area difference), but no longer the
  // SAME vertex pair `Brep::FromMixedFaces()` also assigns to `cf`'s own
  // (or `other`'s own) wall CylindricalFace's own v-const cap edge at
  // this same height.
  //
  // This is a genuinely NEW wrinkle this increment's own research phase
  // did not anticipate, found and fixed here by direct counterexample
  // during implementation: `Brep::FromMixedFaces()` identifies an edge
  // PURELY by its own two endpoint vertices (see its own
  // `edge_of_vertex_pair` map), with no awareness that a curved
  // CylindricalFace's own v-const "cap" boundary is built as a single
  // ISOCURVE edge between its two rail corners (P1, P2) - exactly the
  // SAME two points a lens cap's own naive P1<->P2 chord would connect.
  // Confirmed directly: without this fix, `Brep::FromMixedFaces()` throws
  // its own "an edge is shared by 3 or more faces" error - the wall's own
  // single-curve cap edge, plus BOTH lens segments' own naive chords, all
  // three claiming the identical (P1, P2) vertex pair. Every EXISTING arc-
  // bearing cap producer in this codebase (BuildEndCap's own pie-slice
  // wedges; case (ii)'s own inside-circle disc quadrants) sidesteps this
  // exact collision structurally, not by coincidence: a "pie slice" loop's
  // own closing edge always runs from its LAST arc sample back to a
  // CENTER vertex, never directly between its own two arc endpoints - so
  // this collision never arises for any shape this codebase has built
  // before a genuine circular-SEGMENT (chord-bounded, no center) cap.
  const Point3d chord_mid = 0.5 * (crossing.p1 + crossing.p2) + height * cf.frame.zaxis;
  loop1.push_back(chord_mid);
  loop2.push_back(chord_mid);

  ON_Plane plane;
  plane.origin = cf_cap_center;
  plane.xaxis = cf.frame.xaxis;
  plane.yaxis = cf.frame.yaxis;
  plane.zaxis = same_handed ? cf.frame.zaxis : -cf.frame.zaxis;
  plane.UpdateEquation();

  // angle_begin/angle_end for each run must reflect the ACTUAL signed
  // sweep this loop was built with (which can exceed a quarter turn, and
  // can run either direction) - not re-derived via atan2 after the fact
  // (see FindArcRun's own doc comment for why a plain atan2 reconstruction
  // silently picks the wrong, "short way" direction/magnitude for a run
  // that isn't itself short). Tracked explicitly alongside each loop's own
  // construction instead.
  const double loop1_swept = reverse_both ? (crossing.theta_p1_cf - seg1_end_shifted) : (seg1_end_shifted - crossing.theta_p1_cf);
  const double loop1_begin_angle = reverse_both ? crossing.theta_p2_cf : crossing.theta_p1_cf;
  const double loop2_swept =
      reverse_both ? (crossing.theta_p2_other - seg2_end_shifted) : (seg2_end_shifted - crossing.theta_p2_other);
  const double loop2_begin_angle = reverse_both ? crossing.theta_p1_other : crossing.theta_p2_other;

  Brep::PlanarFace::ArcRun run1;
  run1.begin = 0;
  run1.count = loop1_arc_count;  // the arc samples only - NOT the appended chord_mid vertex
  run1.center = cf_cap_center;
  run1.radius = cf.radius;
  run1.plane_xaxis = cf.frame.xaxis;
  run1.plane_yaxis = cf.frame.yaxis;
  run1.angle_begin = loop1_begin_angle;
  run1.angle_end = loop1_begin_angle + loop1_swept;

  Brep::PlanarFace::ArcRun run2;
  run2.begin = 0;
  run2.count = loop2_arc_count;  // the arc samples only - NOT the appended chord_mid vertex
  run2.center = other_axis_here;
  run2.radius = other.radius;
  run2.plane_xaxis = other.frame.xaxis;
  run2.plane_yaxis = other.frame.yaxis;
  run2.angle_begin = loop2_begin_angle;
  run2.angle_end = loop2_begin_angle + loop2_swept;

  MixedFace m1, m2;
  m1.planar.plane = plane;
  m1.planar.loop = std::move(loop1);
  m1.planar.arc_runs.push_back(run1);
  m2.planar.plane = plane;
  m2.planar.loop = std::move(loop2);
  m2.planar.arc_runs.push_back(run2);

  std::vector<MixedFace> pieces;
  pieces.push_back(std::move(m1));
  pieces.push_back(std::move(m2));
  return pieces;
}

std::vector<MixedFace> SynthesizeEndCaps(const std::vector<MixedFace>& fragments, const std::vector<MixedFace>& other,
                                          double tol, PointClass needed_class = PointClass::kOut) {
  std::vector<MixedFace> caps;
  for (const MixedFace& f : fragments) {
    if (!f.is_cyl) continue;
    const Brep::CylindricalFace& cf = f.cyl;
    const double probe_eps = std::max(tol, 1e-6 * std::max(cf.radius, std::max(cf.length, 1.0)));

    // NEW for this increment: a genuinely CROSSING parallel-axis
    // interactor whose own axial range reaches a given end's height needs
    // a LENS cap there, for the kIn (Intersection/Difference) polarity
    // ONLY - see BuildLensEndCap's own doc comment for why the ordinary
    // on-axis probe just below cannot see this shape at all (it can
    // wrongly read kOut even though a genuine, non-empty lens exists).
    // Checked FIRST, before that probe, since the probe's own answer is
    // not meaningful for this case. At most ONE qualifying interactor is
    // supported per end - two or more simultaneously-reaching crossing
    // interactors is a genuine three-or-more-cylinder mutual interaction
    // this increment does not attempt (the true cross-section there can
    // be a more complex multi-arc region than a single lens) - refused
    // (thrown) rather than silently picking one, mirroring
    // ParallelCylinderCapNeedsNoTrim's own "refuse rather than guess"
    // convention.
    auto try_lens_cap = [&](bool at_v0) -> bool {
      if (needed_class != PointClass::kIn) return false;
      const MixedFace* crossing_interactor = nullptr;
      int crossing_count = 0;
      for (const MixedFace& g : other) {
        if (!g.is_cyl) continue;
        const Vector3d cross_axes = ON_CrossProduct(cf.frame.zaxis, g.cyl.frame.zaxis);
        if (cross_axes.Length() >= kAxisAlignTol) continue;
        if (CylinderCylinderNoInteraction(cf, g.cyl, tol)) continue;
        if (!ComputeParallelCylinderCrossing(cf, g.cyl, tol).crosses) continue;
        if (ParallelCylinderCapNeedsNoTrim(cf, at_v0, g.cyl, tol)) continue;
        crossing_interactor = &g;
        ++crossing_count;
      }
      if (crossing_count > 1) {
        throw std::invalid_argument(
            "dino8::kernel::BooleanCombineMixed: a synthesized Intersection/"
            "Difference end cap's own footprint is reached by MORE THAN ONE "
            "genuinely-crossing parallel-axis cylinder at once - a "
            "three-or-more-cylinder mutual interaction, out of scope for "
            "this increment, see SynthesizeEndCaps' own doc comment in "
            "boolean.cpp");
      }
      if (crossing_interactor == nullptr) return false;
      for (MixedFace& piece : BuildLensEndCap(cf, at_v0, crossing_interactor->cyl, tol)) {
        caps.push_back(std::move(piece));
      }
      return true;
    };

    if (cf.end0_is_original && !try_lens_cap(/*at_v0=*/true)) {
      const Point3d probe = cf.frame.origin - probe_eps * cf.frame.zaxis;
      if (ClassifyPointVsMixedSolid(probe, other, tol) == needed_class) {
        // See ParallelCylinderCapNeedsNoTrim's own doc comment: a
        // synthesized cap whose own footprint might need trimming against
        // an interacting parallel-axis cylinder in `other` is refused
        // rather than silently emitted over-large - a real, disclosed
        // scope limit of this increment's own parallel-axis cylinder/
        // cylinder capability, not a bug.
        if (!ParallelCylinderCapSafeAgainstAll(cf, /*at_v0=*/true, other, tol)) {
          throw std::invalid_argument(
              "dino8::kernel::BooleanCombineMixed: a synthesized end cap's "
              "own footprint may need trimming against an interacting "
              "parallel-axis cylinder that also reaches this end's height "
              "- out of scope for this increment, see "
              "ParallelCylinderCapNeedsNoTrim's own doc comment in "
              "boolean.cpp");
        }
        for (MixedFace& piece : BuildEndCap(cf, /*at_v0=*/true)) caps.push_back(std::move(piece));
      }
    }
    if (cf.end1_is_original && !try_lens_cap(/*at_v0=*/false)) {
      const Point3d probe = cf.frame.origin + (cf.length + probe_eps) * cf.frame.zaxis;
      if (ClassifyPointVsMixedSolid(probe, other, tol) == needed_class) {
        if (!ParallelCylinderCapSafeAgainstAll(cf, /*at_v0=*/false, other, tol)) {
          throw std::invalid_argument(
              "dino8::kernel::BooleanCombineMixed: a synthesized end cap's "
              "own footprint may need trimming against an interacting "
              "parallel-axis cylinder that also reaches this end's height "
              "- out of scope for this increment, see "
              "ParallelCylinderCapNeedsNoTrim's own doc comment in "
              "boolean.cpp");
        }
        for (MixedFace& piece : BuildEndCap(cf, /*at_v0=*/false)) caps.push_back(std::move(piece));
      }
    }
  }
  return caps;
}

}  // namespace

Brep BooleanCombineMixed(const Brep& a, const Brep& b, BooleanOp op) {
  RefuseCompoundOperand(a, "BooleanCombineMixed");
  RefuseCompoundOperand(b, "BooleanCombineMixed");

  if (op == BooleanOp::SymmetricDifference) {
    // XOR = (A - B) u (B - A) as a Brep::Compound of two lumps - see
    // BooleanCombinePlanar's own SymmetricDifference branch above for
    // why one shell can never hold it (four faces meet along every
    // intersection-curve edge) and boolean.h's own doc comment for the
    // measured closed forms. Each lump is one verified Difference; no
    // round trip through a prior result is involved at all.
    return Brep::Compound({BooleanCombineMixed(a, b, BooleanOp::Difference),
                           BooleanCombineMixed(b, a, BooleanOp::Difference)});
  }

  std::vector<MixedFace> fa = ToMixed(a.MixedFaces());
  std::vector<MixedFace> fb = ToMixed(b.MixedFaces());
  const double tol = std::max(RelativeTolMixed(fa), RelativeTolMixed(fb));

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
    case BooleanOp::Intersection: {
      for (const MixedFace& f : from_a.in) result.push_back(f);
      for (const MixedFace& f : from_b.in) result.push_back(f);
      for (const MixedFace& f : from_a.on) result.push_back(f);
      // End-cap synthesis, mirroring the Union branch above but with the
      // OPPOSITE probe polarity (PointClass::kIn, not the default kOut -
      // see SynthesizeEndCaps' own doc comment above and boolean.h's own
      // BooleanCombineMixed doc comment for the worked argument): a bare
      // CylindricalFace operand kept in an Intersection result (from_a.in/
      // from_b.in) can have a genuinely original end where NEITHER
      // operand's already-collected faces supply the closing disk - most
      // simply, a cylinder fully embedded in the other operand, which
      // needs a cap at BOTH its own ends. `from_a.on`/`from_b.on` are not
      // scanned here for the same reason the Union branch's comment above
      // already gives: a cylindrical fragment's own representative point
      // is always strictly interior along its curved surface, never
      // landing in the `on` bucket for any geometry this increment's own
      // tests build.
      for (MixedFace& cap : SynthesizeEndCaps(from_a.in, fb, tol, PointClass::kIn)) result.push_back(std::move(cap));
      for (MixedFace& cap : SynthesizeEndCaps(from_b.in, fa, tol, PointClass::kIn)) result.push_back(std::move(cap));
      break;
    }
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
      // New for this increment: Difference previously called
      // SynthesizeEndCaps nowhere at all, so a bare CylindricalFace
      // operand's own genuinely exposed original end (parallel-axis
      // cylinder/cylinder pair or otherwise) was silently left open by
      // this branch specifically - a real, previously-latent gap this
      // increment's own test plan is the first to exercise for the
      // parallel-cylinder case. `from_a.out` is capped exactly the way
      // Union caps its own `from_a.out`/`from_b.out` (the SAME code path,
      // SAME polarity, SAME disclosed crossing-cap-trim refusal for a
      // plain disc cap - A's outer material here has the identical trim
      // risk Union already declines to guess at). `from_b.in` is capped
      // exactly the way Intersection caps its own `from_a.in`/`from_b.in`
      // (kIn polarity, including the new lens-cap path above), then EACH
      // resulting cap is flipped via the existing, unmodified
      // FlipMixedFace before being appended - correct for a lens cap's
      // straight-chord+arc boundary exactly as it already is for any
      // other planar loop, verified directly (not merely assumed) by this
      // increment's own reverse-subtraction (B - A) test, which exercises
      // FlipFace on this genuinely new loop shape for the first time.
      for (MixedFace& cap : SynthesizeEndCaps(from_a.out, fb, tol)) result.push_back(std::move(cap));
      for (MixedFace& cap : SynthesizeEndCaps(from_b.in, fa, tol, PointClass::kIn)) {
        result.push_back(FlipMixedFace(std::move(cap)));
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
