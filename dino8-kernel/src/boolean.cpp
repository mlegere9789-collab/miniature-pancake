#include "dino8/kernel/boolean.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include <manifold/manifold.h>

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

// Sutherland-Hodgman, run once but keeping BOTH children instead of only
// the "inside" one: a single pass over `poly`'s edges classifies each
// vertex against `clip_plane` and files it (or, at a sign change, the
// shared interpolated crossing point) into `inside` and/or `outside`.
// This "split, not clip" primitive is what every non-convex boolean below
// is built from - see Requicha & Voelcker, "Boolean operations in solid
// modeling: Boundary evaluation and merging algorithms," Proc. IEEE 73(1),
// 1985, for the classical (unpatented) boundary-evaluation technique this
// implements: partitioning a face against every plane of the other solid
// until each surviving fragment lies wholly on one side of every such
// plane and can be classified with a single point-in-solid test.
//
// Valid for a CONCAVE `poly`, not just a convex one: clipping against a
// single half-space (one plane) is a purely local per-edge operation that
// doesn't depend on the subject polygon's own convexity. If the plane
// crosses a concave polygon's boundary more than twice, one side's output
// is a single vertex loop that revisits the cut line more than once (two
// or more regions joined by zero-net-area "bridge" edges lying exactly on
// the cut) rather than several separate loops - the same "keyhole" trick
// used to triangulate a polygon with a hole - whose signed area, and
// hence any ear-clip triangulation of it, still comes out exactly right.
struct HalfspaceSplit {
  std::vector<Point3d> inside;
  std::vector<Point3d> outside;
};

HalfspaceSplit SplitByHalfspace(const std::vector<Point3d>& poly, const ON_Plane& clip_plane, double tol) {
  HalfspaceSplit result;
  if (poly.size() < 3) return result;
  result.inside.reserve(poly.size() + 1);
  result.outside.reserve(poly.size() + 1);
  const size_t n = poly.size();
  for (size_t i = 0; i < n; ++i) {
    const Point3d& cur = poly[i];
    const Point3d& nxt = poly[(i + 1) % n];
    const double dc = clip_plane.DistanceTo(cur);
    const double dn = clip_plane.DistanceTo(nxt);
    const bool cur_in = dc <= tol;
    const bool nxt_in = dn <= tol;
    if (cur_in) {
      result.inside.push_back(cur);
    } else {
      result.outside.push_back(cur);
    }
    if (cur_in != nxt_in && std::fabs(dc - dn) > 1e-15) {
      const double t = dc / (dc - dn);
      const Point3d crossing = cur + t * (nxt - cur);
      // The crossing point sits exactly on `clip_plane`, so it's a shared
      // vertex of BOTH children - the new edge along the cut.
      result.inside.push_back(crossing);
      result.outside.push_back(crossing);
    }
  }
  return result;
}

// Clips a convex 3D polygon (already known to lie in one plane) against
// one half-space, keeping the side the plane's own normal points away
// from (DistanceTo <= tol is "inside"). A thin wrapper so
// ClipByAllHalfspaces/BooleanIntersectConvexPlanar - both of which only
// ever want the "inside" child - need no change.
std::vector<Point3d> ClipByHalfspace(const std::vector<Point3d>& poly, const ON_Plane& clip_plane, double tol) {
  return SplitByHalfspace(poly, clip_plane, tol).inside;
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
  if (poly.size() < 3) return poly;
  std::vector<Point3d> out;
  out.reserve(poly.size());
  for (const Point3d& p : poly) {
    if (out.empty() || out.back().DistanceTo(p) > tol) out.push_back(p);
  }
  while (out.size() > 1 && out.front().DistanceTo(out.back()) <= tol) out.pop_back();
  return out;
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

}  // namespace dino8::kernel
