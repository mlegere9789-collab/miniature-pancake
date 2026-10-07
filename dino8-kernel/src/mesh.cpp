#include "dino8/kernel/mesh.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/detail/polygon2d.h"
#include "dino8/kernel/tolerance.h"
#include "dino8/kernel/detail/segment3d.h"

namespace dino8::kernel {

namespace {

// Parses one '/'-separated field of a .obj face-line token into a
// nonzero 1-based index - positive (absolute) or negative (relative to
// however many `v`/`vt` entries have been declared so far; the caller
// resolves it to an absolute index once it knows which count applies).
// Empty (both slashes present but nothing between them, e.g. the
// "v//vn" form's middle field) is treated as "absent", not a parse
// failure - the caller distinguishes the two via the returned bool.
bool ParseObjIndexField(const std::string& field, int& value) {
  if (field.empty()) {
    return false;
  }
  size_t consumed = 0;
  int parsed = 0;
  try {
    parsed = std::stoi(field, &consumed);
  } catch (const std::exception&) {
    return false;
  }
  if (consumed != field.size() || parsed == 0) {
    return false;
  }
  value = parsed;
  return true;
}

// Parses a .obj face-line token into its 1-based-or-negative-relative
// vertex index (`v_index`, required) and, if present, its
// 1-based-or-negative-relative texture-coordinate index (`vt_index`,
// `has_vt` set true) - accepting the plain "3" form, the "3/4"
// (vertex/texture) form, and the "3/4/5" (vertex/texture/normal) and
// "3//5" (vertex/normal only) forms other tools write, plus the
// standard .obj negative-index form ("-1" meaning "the last vertex/vt
// declared so far") in either position. The normal index, when present,
// is parsed away but discarded - this kernel's ON_Mesh has no
// per-face-corner normal data to put it in (vertex normals here are
// always geometry-derived via ComputeVertexNormals(), never stored
// independently). Rejects a malformed or zero-valued vertex index; the
// caller (LoadObj) resolves a negative index to absolute and rejects
// one that resolves out of range. A malformed (non-empty but
// unparsable) texture-coordinate field is also rejected, but its true
// *absence* (the "v//vn" form) is not.
bool ParseObjFaceIndex(const std::string& token, int& v_index, int& vt_index, bool& has_vt) {
  has_vt = false;
  const size_t first_slash = token.find('/');
  const std::string first = (first_slash == std::string::npos) ? token : token.substr(0, first_slash);
  if (!ParseObjIndexField(first, v_index)) {
    return false;
  }
  if (first_slash == std::string::npos) {
    return true;
  }
  const size_t second_slash = token.find('/', first_slash + 1);
  const std::string second = (second_slash == std::string::npos)
                                  ? token.substr(first_slash + 1)
                                  : token.substr(first_slash + 1, second_slash - first_slash - 1);
  if (second.empty()) {
    return true;  // "v//vn" form - no texture coordinate for this corner
  }
  if (!ParseObjIndexField(second, vt_index)) {
    return false;
  }
  has_vt = true;
  return true;
}

}  // namespace

int Mesh::VertexCount() const { return mesh_.VertexCount(); }

int Mesh::FaceCount() const { return mesh_.FaceCount(); }

double Mesh::Volume() const {
  double volume = 0.0;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& face = mesh_.m_F[i];
    const ON_3fPoint& a = mesh_.m_V[face.vi[0]];
    const ON_3fPoint& b = mesh_.m_V[face.vi[1]];
    const ON_3fPoint& c = mesh_.m_V[face.vi[2]];
    volume += (static_cast<double>(a.x) *
                   (static_cast<double>(b.y) * c.z - static_cast<double>(b.z) * c.y) -
               static_cast<double>(a.y) *
                   (static_cast<double>(b.x) * c.z - static_cast<double>(b.z) * c.x) +
               static_cast<double>(a.z) *
                   (static_cast<double>(b.x) * c.y - static_cast<double>(b.y) * c.x)) /
              6.0;
    if (face.IsQuad()) {
      const ON_3fPoint& d = mesh_.m_V[face.vi[3]];
      volume += (static_cast<double>(a.x) *
                     (static_cast<double>(c.y) * d.z - static_cast<double>(c.z) * d.y) -
                 static_cast<double>(a.y) *
                     (static_cast<double>(c.x) * d.z - static_cast<double>(c.z) * d.x) +
                 static_cast<double>(a.z) *
                     (static_cast<double>(c.x) * d.y - static_cast<double>(c.y) * d.x)) /
                6.0;
    }
  }
  return volume;
}

Point3d Mesh::GetCentroid() const {
  double volume_sum = 0.0;
  Vector3d weighted_sum(0, 0, 0);

  auto accumulate_tet = [&](const ON_3fPoint& a, const ON_3fPoint& b, const ON_3fPoint& c) {
    // Signed volume of the tetrahedron (origin, a, b, c) - the same
    // per-triangle term Volume() sums - and that tetrahedron's own
    // centroid, the average of its 4 vertices ((0,0,0)+a+b+c)/4.
    const double signed_volume =
        (static_cast<double>(a.x) *
             (static_cast<double>(b.y) * c.z - static_cast<double>(b.z) * c.y) -
         static_cast<double>(a.y) *
             (static_cast<double>(b.x) * c.z - static_cast<double>(b.z) * c.x) +
         static_cast<double>(a.z) *
             (static_cast<double>(b.x) * c.y - static_cast<double>(b.y) * c.x)) /
        6.0;
    const Vector3d tet_centroid = (Vector3d(a) + Vector3d(b) + Vector3d(c)) / 4.0;
    volume_sum += signed_volume;
    weighted_sum += tet_centroid * signed_volume;
  };

  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& face = mesh_.m_F[i];
    const ON_3fPoint& a = mesh_.m_V[face.vi[0]];
    const ON_3fPoint& b = mesh_.m_V[face.vi[1]];
    const ON_3fPoint& c = mesh_.m_V[face.vi[2]];
    accumulate_tet(a, b, c);
    if (face.IsQuad()) {
      accumulate_tet(a, c, mesh_.m_V[face.vi[3]]);
    }
  }

  if (std::abs(volume_sum) <= tolerance::kZero) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::GetCentroid: mesh volume is (near) zero - not a "
        "closed, non-degenerate solid this formula can compute a centroid for");
  }
  return Point3d(weighted_sum / volume_sum);
}

double Mesh::Area() const {
  double area = 0.0;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& face = mesh_.m_F[i];
    const ON_3fPoint& a = mesh_.m_V[face.vi[0]];
    const ON_3fPoint& b = mesh_.m_V[face.vi[1]];
    const ON_3fPoint& c = mesh_.m_V[face.vi[2]];
    const ON_3dVector cross1 =
        ON_3dVector::CrossProduct(ON_3dVector(b - a), ON_3dVector(c - a));
    area += 0.5 * cross1.Length();
    if (face.IsQuad()) {
      // Second triangle (a, c, d) - every other quad-aware computation in
      // this class (Volume(), ExtrudeCappedSolid()'s boundary-edge
      // extraction treating a quad as two triangles) already accounts for
      // both halves; Area() didn't, and silently returned exactly half
      // the true area for any real (non-degenerate) quad face - not
      // exercised by any earlier test here, since every tessellator in
      // this file emits triangles only (vi[3] == vi[2]), but a real bug
      // for the quad meshes SubD::ToApproximateMesh() and a hand-built
      // quad mesh (MakeQuadBoxMesh in the tests) actually produce.
      const ON_3fPoint& d = mesh_.m_V[face.vi[3]];
      const ON_3dVector cross2 =
          ON_3dVector::CrossProduct(ON_3dVector(c - a), ON_3dVector(d - a));
      area += 0.5 * cross2.Length();
    }
  }
  return area;
}

namespace {

// Moller-Trumbore ray/line-triangle intersection: whether the line
// `origin + t*direction` crosses triangle (a, b, c) at all, and if so at
// which parameter `t` (any sign - the caller decides whether "behind the
// origin" counts) and barycentric (u, v) in the triangle. `slack` widens
// the barycentric inclusion test by that much on every side (0 = the
// exact closed triangle), so a caller that must not drop a crossing
// landing exactly on an edge to round-off can ask for a hair of
// tolerance. A standard, well-known intersection formula, not something
// needing independent derivation the way this file's own winding
// conventions did. Shared by ContainsPoint()'s ray-casting test,
// FireRay(), and DistanceTo()'s segment/triangle piercing test.
bool LineTriangleParameter(const Point3d& origin, const Vector3d& direction, const Point3d& a,
                           const Point3d& b, const Point3d& c, double& t, double& u, double& v,
                           double slack = 0.0) {
  constexpr double kEpsilon = tolerance::kZero;
  const Vector3d edge1 = b - a;
  const Vector3d edge2 = c - a;
  const Vector3d h = ON_CrossProduct(direction, edge2);
  const double det = ON_DotProduct(edge1, h);
  if (std::abs(det) < kEpsilon) {
    return false;  // line parallel to the triangle's plane
  }
  const double inv_det = 1.0 / det;
  const Vector3d s = origin - a;
  u = inv_det * ON_DotProduct(s, h);
  if (u < -slack || u > 1.0 + slack) {
    return false;
  }
  const Vector3d q = ON_CrossProduct(s, edge1);
  v = inv_det * ON_DotProduct(direction, q);
  if (v < -slack || u + v > 1.0 + slack) {
    return false;
  }
  t = inv_det * ON_DotProduct(edge2, q);
  return true;
}

// Whether the ray `origin + t*direction` (t > kEpsilon, i.e. strictly
// ahead of origin, not behind it or exactly at it) crosses triangle
// (a, b, c). Used by ContainsPoint()'s ray-casting test.
bool RayIntersectsTriangle(const Point3d& origin, const Vector3d& direction, const Point3d& a,
                            const Point3d& b, const Point3d& c) {
  constexpr double kEpsilon = tolerance::kZero;
  double t = 0, u = 0, v = 0;
  return LineTriangleParameter(origin, direction, a, b, c, t, u, v) && t > kEpsilon;
}

// Exact minimum distance between triangles (a0, a1, a2) and (b0, b1, b2)
// and the points where it's attained - see DistanceTo()'s own doc
// comment for the three feature families (vertex/triangle, edge/edge,
// edge-pierces-triangle) that between them cover every configuration.
// `ClosestPointOnTriangle` is declared below; this is defined after it.
Point3d ClosestPointOnTriangle(const Point3d& p, const Point3d& a, const Point3d& b, const Point3d& c);
double TriangleTriangleDistance(const std::array<Point3d, 3>& ta, const std::array<Point3d, 3>& tb, Point3d& on_a,
                                Point3d& on_b) {
  double best = std::numeric_limits<double>::infinity();
  // Vertex of one vs. the other triangle (both ways).
  for (int i = 0; i < 3; ++i) {
    const Point3d q = ClosestPointOnTriangle(ta[static_cast<size_t>(i)], tb[0], tb[1], tb[2]);
    const double d = q.DistanceTo(ta[static_cast<size_t>(i)]);
    if (d < best) {
      best = d;
      on_a = ta[static_cast<size_t>(i)];
      on_b = q;
    }
    const Point3d p = ClosestPointOnTriangle(tb[static_cast<size_t>(i)], ta[0], ta[1], ta[2]);
    const double e = p.DistanceTo(tb[static_cast<size_t>(i)]);
    if (e < best) {
      best = e;
      on_a = p;
      on_b = tb[static_cast<size_t>(i)];
    }
  }
  // Edge vs. edge (9 pairs).
  for (int i = 0; i < 3; ++i) {
    const Point3d& p0 = ta[static_cast<size_t>(i)];
    const Point3d& p1 = ta[static_cast<size_t>((i + 1) % 3)];
    for (int j = 0; j < 3; ++j) {
      const Point3d& q0 = tb[static_cast<size_t>(j)];
      const Point3d& q1 = tb[static_cast<size_t>((j + 1) % 3)];
      double s = 0, t = 0;
      const double d2 = detail::ClosestSegmentSegment(p0, p1, q0, q1, s, t);
      const double d = std::sqrt(std::max(d2, 0.0));
      if (d < best) {
        best = d;
        on_a = p0 + (p1 - p0) * s;
        on_b = q0 + (q1 - q0) * t;
      }
    }
  }
  // Edge of one piercing the other's interior: the one configuration the
  // two feature families above can't see (nothing on either boundary is
  // at distance 0 from the other triangle, yet they cross). A crossing
  // exactly on the other triangle's boundary is already an edge/edge
  // zero above, so the exact (slack-free) inclusion test suffices here.
  if (best > 0.0) {
    auto pierce = [&](const std::array<Point3d, 3>& edges_of, const std::array<Point3d, 3>& tri) {
      for (int i = 0; i < 3 && best > 0.0; ++i) {
        const Point3d& p0 = edges_of[static_cast<size_t>(i)];
        const Point3d& p1 = edges_of[static_cast<size_t>((i + 1) % 3)];
        double t = 0, u = 0, v = 0;
        if (LineTriangleParameter(p0, p1 - p0, tri[0], tri[1], tri[2], t, u, v) && t >= 0.0 && t <= 1.0) {
          best = 0.0;
          const Point3d x = p0 + (p1 - p0) * t;
          on_a = x;
          on_b = x;
        }
      }
    };
    pierce(ta, tb);
    pierce(tb, ta);
  }
  return best;
}

// Closest point on triangle (a, b, c) to `p` - the standard region-based
// algorithm (Ericson, "Real-Time Collision Detection" 5.1.5): classifies
// `p`'s projection into one of the triangle's 7 barycentric Voronoi
// regions (3 vertices, 3 edges, 1 interior face) via a handful of dot
// products, then returns the corresponding vertex, a point clamped onto
// an edge, or the direct interior projection. Used by ClosestPoint().
Point3d ClosestPointOnTriangle(const Point3d& p, const Point3d& a, const Point3d& b,
                                const Point3d& c) {
  const Vector3d ab = b - a;
  const Vector3d ac = c - a;
  const Vector3d ap = p - a;
  const double d1 = ON_DotProduct(ab, ap);
  const double d2 = ON_DotProduct(ac, ap);
  if (d1 <= 0.0 && d2 <= 0.0) {
    return a;  // vertex region a
  }

  const Vector3d bp = p - b;
  const double d3 = ON_DotProduct(ab, bp);
  const double d4 = ON_DotProduct(ac, bp);
  if (d3 >= 0.0 && d4 <= d3) {
    return b;  // vertex region b
  }

  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
    const double v = d1 / (d1 - d3);
    return a + v * ab;  // edge region ab
  }

  const Vector3d cp = p - c;
  const double d5 = ON_DotProduct(ab, cp);
  const double d6 = ON_DotProduct(ac, cp);
  if (d6 >= 0.0 && d5 <= d6) {
    return c;  // vertex region c
  }

  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
    const double w = d2 / (d2 - d6);
    return a + w * ac;  // edge region ac
  }

  const double va = d3 * d6 - d5 * d4;
  if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
    const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return b + w * (c - b);  // edge region bc
  }

  // interior face region
  const double denom = 1.0 / (va + vb + vc);
  const double v = vb * denom;
  const double w = vc * denom;
  return a + ab * v + ac * w;
}

}  // namespace

namespace {

// Forward declaration - defined further below (shared with
// LoftClosedRings()'s own end-cap triangulation). Declared here so the
// n-gon face loaders above it in this file (LoadObj/LoadOff/LoadVrml/
// LoadCollada/LoadX3d/LoadUsda) can call it too.
std::vector<std::array<int, 3>> TriangulatePlanarRing(const std::vector<Point3d>& ring);

// Appends one polygon face's worth of ON_MeshFace entries to `raw`, given
// its corner vertex indices (already validated in range against
// `raw.m_V`) in order around the face. A triangle or quad becomes a
// single native ON_MeshFace, same as always; a genuine n-gon (5+
// corners) is real-ear-clip-triangulated on its own best-fit (Newell)
// plane via TriangulatePlanarRing() - unlike a naive fan from the first
// corner (what every one of this file's n-gon face loaders used to do
// independently), ear-clipping stays correct for a CONCAVE n-gon too: a
// fan can produce a triangle whose interior falls outside the original
// face, which ear-clipping's own interior-point test (PointInTriangle(),
// detail/polygon2d.h) never allows - the same guarantee
// LoftClosedRings()'s own end caps already rely on this exact
// triangulator for. Shared by every "other mesh/scene exchange format"
// n-gon loader in this file rather than reimplemented per format, so a
// future fix to the triangulator benefits all of them at once.
void AppendPolygonFace(ON_Mesh& raw, const std::vector<int>& face_vertex_indices) {
  const size_t n = face_vertex_indices.size();
  if (n <= 4) {
    ON_MeshFace face;
    face.vi[0] = face_vertex_indices[0];
    face.vi[1] = face_vertex_indices[1];
    face.vi[2] = face_vertex_indices[2];
    face.vi[3] = (n == 4) ? face_vertex_indices[3] : face_vertex_indices[2];
    raw.m_F.Append(face);
    return;
  }
  std::vector<Point3d> ring;
  ring.reserve(n);
  for (int idx : face_vertex_indices) {
    ring.push_back(Point3d(raw.m_V[idx]));
  }
  for (const std::array<int, 3>& tri : TriangulatePlanarRing(ring)) {
    ON_MeshFace face;
    face.vi[0] = face_vertex_indices[static_cast<size_t>(tri[0])];
    face.vi[1] = face_vertex_indices[static_cast<size_t>(tri[1])];
    face.vi[2] = face_vertex_indices[static_cast<size_t>(tri[2])];
    face.vi[3] = face.vi[2];
    raw.m_F.Append(face);
  }
}

}  // namespace

BoundingBox Mesh::GetBoundingBox() const {
  if (mesh_.m_V.Count() == 0) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::GetBoundingBox: mesh has no vertices - there's "
        "no box to compute");
  }

  Point3d box_min(mesh_.m_V[0]);
  Point3d box_max(mesh_.m_V[0]);
  for (int i = 1; i < mesh_.m_V.Count(); ++i) {
    const Point3d p(mesh_.m_V[i]);
    box_min.x = std::min(box_min.x, p.x);
    box_min.y = std::min(box_min.y, p.y);
    box_min.z = std::min(box_min.z, p.z);
    box_max.x = std::max(box_max.x, p.x);
    box_max.y = std::max(box_max.y, p.y);
    box_max.z = std::max(box_max.z, p.z);
  }
  return BoundingBox{box_min, box_max};
}

bool Mesh::ContainsPoint(Point3d point) const {
  // A ray along a world axis reliably grazes vertices of any axis-aligned,
  // axis-symmetric or UV-parametrized mesh (spheres, cylinders, revolves,
  // boxes...) whenever the query point sits on that mesh's own axis or
  // symmetry plane - which is exactly where callers most often test (e.g.
  // "is this the sphere's own center inside it", from SelVolumeObject).
  // Those grazing hits are numerically unstable: floating-point noise from
  // translating the same mesh away from the origin flips the parity count
  // (observed: a sphere at the world origin reported its center as
  // contained, the identical sphere translated to (40,20,10) did not). A
  // direction with no axis-aligned or rational-fraction component doesn't
  // line up with these seams, so it isn't sensitive to translation.
  const Vector3d direction(0.6532814824, 0.2705980501, 0.7071067812);
  int crossing_count = 0;

  auto count_triangle = [&](int i0, int i1, int i2) {
    if (RayIntersectsTriangle(point, direction, mesh_.m_V[i0], mesh_.m_V[i1], mesh_.m_V[i2])) {
      ++crossing_count;
    }
  };

  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    count_triangle(f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) {
      count_triangle(f.vi[0], f.vi[2], f.vi[3]);
    }
  }

  return (crossing_count % 2) == 1;
}

Point3d Mesh::ClosestPoint(Point3d point) const {
  if (mesh_.m_F.Count() == 0) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::ClosestPoint: mesh has no faces - there's no "
        "surface to be close to");
  }

  Point3d best_point;
  double best_distance_squared = std::numeric_limits<double>::infinity();

  auto consider_triangle = [&](int i0, int i1, int i2) {
    const Point3d candidate = ClosestPointOnTriangle(point, Point3d(mesh_.m_V[i0]),
                                                       Point3d(mesh_.m_V[i1]),
                                                       Point3d(mesh_.m_V[i2]));
    const double distance_squared = (candidate - point).LengthSquared();
    if (distance_squared < best_distance_squared) {
      best_distance_squared = distance_squared;
      best_point = candidate;
    }
  };

  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    consider_triangle(f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) {
      consider_triangle(f.vi[0], f.vi[2], f.vi[3]);
    }
  }

  return best_point;
}

double Mesh::SignedDistance(Point3d point) const {
  const double distance = (ClosestPoint(point) - point).Length();
  return ContainsPoint(point) ? -distance : distance;
}

MassProperties Mesh::VolumeMassProperties() const {
  // Eberly, "Polyhedral Mass Properties (Revisited)": the ten volume
  // integrals of {1, x, y, z, x^2, y^2, z^2, xy, yz, zx} are each turned
  // into a surface integral by the divergence theorem, and over a flat
  // triangle those surface integrals have closed-form polynomial values
  // in the three vertices. `intg[]` accumulates, in that order, the
  // un-scaled per-triangle terms; the 1/6, 1/24, 1/60, 1/120 factors are
  // applied once at the end.
  std::array<double, 10> intg{};

  auto subexpressions = [](double w0, double w1, double w2, double& f1, double& f2, double& f3,
                           double& g0, double& g1, double& g2) {
    const double temp0 = w0 + w1;
    f1 = temp0 + w2;
    const double temp1 = w0 * w0;
    const double temp2 = temp1 + w1 * temp0;
    f2 = temp2 + w2 * f1;
    f3 = w0 * temp1 + w1 * temp2 + w2 * f2;
    g0 = f2 + w0 * (f1 + w0);
    g1 = f2 + w1 * (f1 + w1);
    g2 = f2 + w2 * (f1 + w2);
  };

  auto accumulate_triangle = [&](const ON_3fPoint& fa, const ON_3fPoint& fb, const ON_3fPoint& fc) {
    const double x0 = fa.x, y0 = fa.y, z0 = fa.z;
    const double x1 = fb.x, y1 = fb.y, z1 = fb.z;
    const double x2 = fc.x, y2 = fc.y, z2 = fc.z;

    // Edge vectors and their cross product (the triangle's un-normalized
    // outward normal, magnitude twice its area).
    const double a1 = x1 - x0, b1 = y1 - y0, c1 = z1 - z0;
    const double a2 = x2 - x0, b2 = y2 - y0, c2 = z2 - z0;
    const double d0 = b1 * c2 - b2 * c1;
    const double d1 = a2 * c1 - a1 * c2;
    const double d2 = a1 * b2 - a2 * b1;

    double f1x, f2x, f3x, g0x, g1x, g2x;
    double f1y, f2y, f3y, g0y, g1y, g2y;
    double f1z, f2z, f3z, g0z, g1z, g2z;
    subexpressions(x0, x1, x2, f1x, f2x, f3x, g0x, g1x, g2x);
    subexpressions(y0, y1, y2, f1y, f2y, f3y, g0y, g1y, g2y);
    subexpressions(z0, z1, z2, f1z, f2z, f3z, g0z, g1z, g2z);

    intg[0] += d0 * f1x;
    intg[1] += d0 * f2x;
    intg[2] += d1 * f2y;
    intg[3] += d2 * f2z;
    intg[4] += d0 * f3x;
    intg[5] += d1 * f3y;
    intg[6] += d2 * f3z;
    intg[7] += d0 * (y0 * g0x + y1 * g1x + y2 * g2x);
    intg[8] += d1 * (z0 * g0y + z1 * g1y + z2 * g2y);
    intg[9] += d2 * (x0 * g0z + x1 * g1z + x2 * g2z);
  };

  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    accumulate_triangle(mesh_.m_V[f.vi[0]], mesh_.m_V[f.vi[1]], mesh_.m_V[f.vi[2]]);
    if (f.IsQuad()) {
      accumulate_triangle(mesh_.m_V[f.vi[0]], mesh_.m_V[f.vi[2]], mesh_.m_V[f.vi[3]]);
    }
  }

  const std::array<double, 10> mult = {1.0 / 6.0,  1.0 / 24.0,  1.0 / 24.0,  1.0 / 24.0,  1.0 / 60.0,
                                       1.0 / 60.0, 1.0 / 60.0,  1.0 / 120.0, 1.0 / 120.0, 1.0 / 120.0};
  for (size_t i = 0; i < intg.size(); ++i) {
    intg[i] *= mult[i];
  }

  MassProperties mp;
  mp.volume = intg[0];
  if (std::abs(mp.volume) <= tolerance::kZero) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::VolumeMassProperties: mesh volume is (near) zero - "
        "not a closed, non-degenerate solid whose moments are defined");
  }
  if (mp.volume < 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::VolumeMassProperties: mesh volume is negative - the "
        "mesh is inside-out (wound CW from outside); FlipNormals() it first "
        "rather than trusting negated moments");
  }

  mp.centroid = Point3d(intg[1] / mp.volume, intg[2] / mp.volume, intg[3] / mp.volume);
  const double cx = mp.centroid.x, cy = mp.centroid.y, cz = mp.centroid.z;

  // intg[4..6] are the integrals of x^2, y^2, z^2; intg[7..9] of xy, yz, zx.
  mp.ixx_origin = intg[5] + intg[6];
  mp.iyy_origin = intg[4] + intg[6];
  mp.izz_origin = intg[4] + intg[5];
  mp.ixy_origin = intg[7];
  mp.iyz_origin = intg[8];
  mp.ixz_origin = intg[9];

  // Parallel-axis theorem, origin -> centroid.
  mp.ixx = mp.ixx_origin - mp.volume * (cy * cy + cz * cz);
  mp.iyy = mp.iyy_origin - mp.volume * (cz * cz + cx * cx);
  mp.izz = mp.izz_origin - mp.volume * (cx * cx + cy * cy);
  mp.ixy = mp.ixy_origin - mp.volume * cx * cy;
  mp.iyz = mp.iyz_origin - mp.volume * cy * cz;
  mp.ixz = mp.ixz_origin - mp.volume * cz * cx;

  // ON_Sym3x3EigenSolver's matrix layout is [[A, D, F], [D, B, E], [F, E,
  // C]] (its own doc comment) - so the off-diagonal entries are the
  // NEGATED products of inertia, per the tensor convention documented on
  // MassProperties.
  double e[3];
  Vector3d v[3];
  if (!ON_Sym3x3EigenSolver(mp.ixx, mp.iyy, mp.izz, -mp.ixy, -mp.iyz, -mp.ixz, &e[0], v[0], &e[1], v[1],
                            &e[2], v[2])) {
    throw std::runtime_error(
        "dino8::kernel::Mesh::VolumeMassProperties: ON_Sym3x3EigenSolver reported "
        "failure on the centroidal inertia tensor");
  }
  std::array<int, 3> order = {0, 1, 2};
  std::sort(order.begin(), order.end(), [&](int p, int q) { return e[p] < e[q]; });
  for (int k = 0; k < 3; ++k) {
    mp.principal_moments[static_cast<size_t>(k)] = e[order[static_cast<size_t>(k)]];
    Vector3d axis = v[order[static_cast<size_t>(k)]];
    axis.Unitize();
    mp.principal_axes[static_cast<size_t>(k)] = axis;
  }
  // Make the frame right-handed: for a symmetric matrix the eigenvectors
  // are mutually orthogonal, so the cross product of the first two is
  // +/- the third and still an eigenvector of it.
  mp.principal_axes[2] = ON_CrossProduct(mp.principal_axes[0], mp.principal_axes[1]);
  mp.principal_axes[2].Unitize();
  for (size_t k = 0; k < 3; ++k) {
    mp.radii_of_gyration[k] = std::sqrt(std::max(mp.principal_moments[k], 0.0) / mp.volume);
  }
  return mp;
}

OrientedBoundingBox Mesh::GetOrientedBoundingBox() const {
  // VolumeMassProperties()'s own precondition (closed, consistently
  // oriented, positive volume) and its own exceptions on failure - see
  // this method's own doc comment for why its principal_axes are exactly
  // the box's own axes, not a separate PCA.
  const MassProperties mp = VolumeMassProperties();

  OrientedBoundingBox obb;
  obb.axes = mp.principal_axes;

  // The tightest slab along each axis that contains every vertex: the
  // largest and smallest signed distance from the centroid (an arbitrary
  // but convenient common reference point - any point would do, since
  // only the difference of extremes and their own midpoint are kept)
  // found by direct search, not estimated.
  std::array<double, 3> lo = {0.0, 0.0, 0.0};
  std::array<double, 3> hi = {0.0, 0.0, 0.0};
  bool first = true;
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const Vector3d d = Point3d(mesh_.m_V[i]) - mp.centroid;
    for (size_t k = 0; k < 3; ++k) {
      const double t = ON_DotProduct(d, obb.axes[k]);
      if (first) {
        lo[k] = hi[k] = t;
      } else {
        lo[k] = std::min(lo[k], t);
        hi[k] = std::max(hi[k], t);
      }
    }
    first = false;
  }

  obb.center = mp.centroid;
  for (size_t k = 0; k < 3; ++k) {
    obb.center = obb.center + obb.axes[k] * (0.5 * (lo[k] + hi[k]));
    obb.half_extents[k] = 0.5 * (hi[k] - lo[k]);
  }
  return obb;
}

std::vector<RayHit> Mesh::FireRay(Point3d origin, Vector3d direction) const {
  if (direction.LengthSquared() <= 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::FireRay: direction is the zero vector - a ray needs "
        "a direction");
  }
  constexpr double kEpsilon = 1e-12;
  std::vector<RayHit> hits;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    auto try_triangle = [&](int i0, int i1, int i2) {
      const Point3d a(mesh_.m_V[i0]), b(mesh_.m_V[i1]), c(mesh_.m_V[i2]);
      double t = 0, u = 0, v = 0;
      // A hair of barycentric slack so a crossing landing exactly on a
      // quad's shared diagonal isn't rejected by BOTH triangles to
      // round-off (u + v = 1 - 1e-17 in one, u = -1e-17 in the other).
      if (!LineTriangleParameter(origin, direction, a, b, c, t, u, v, /*slack=*/1e-9) || t <= kEpsilon) {
        return false;
      }
      RayHit h;
      h.t = t;
      h.point = origin + direction * t;
      h.face_index = i;
      h.entering = ON_DotProduct(direction, ON_CrossProduct(b - a, c - a)) < 0.0;
      hits.push_back(h);
      return true;
    };
    const bool hit_first = try_triangle(f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) {
      // A planar quad is crossed at most once, so a hit in both of its
      // triangles is the same point on their shared diagonal - report it
      // once. (A non-planar quad genuinely crossed twice is not a case
      // this kernel's own tessellators ever emit.)
      if (!hit_first) {
        try_triangle(f.vi[0], f.vi[2], f.vi[3]);
      }
    }
  }
  std::sort(hits.begin(), hits.end(), [](const RayHit& x, const RayHit& y) { return x.t < y.t; });
  return hits;
}

MeshDistance Mesh::DistanceTo(const Mesh& other) const {
  if (mesh_.m_F.Count() == 0 || other.mesh_.m_F.Count() == 0) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::DistanceTo: a mesh has no faces - there's no "
        "surface to measure to");
  }

  struct Tri {
    std::array<Point3d, 3> p;
    int face = -1;
    Point3d lo, hi;
  };
  auto collect = [](const ON_Mesh& m) {
    std::vector<Tri> tris;
    auto add = [&](int face, int i0, int i1, int i2) {
      Tri t;
      t.p = {Point3d(m.m_V[i0]), Point3d(m.m_V[i1]), Point3d(m.m_V[i2])};
      t.face = face;
      t.lo = t.hi = t.p[0];
      for (int k = 1; k < 3; ++k) {
        const Point3d& q = t.p[static_cast<size_t>(k)];
        t.lo.x = std::min(t.lo.x, q.x);
        t.lo.y = std::min(t.lo.y, q.y);
        t.lo.z = std::min(t.lo.z, q.z);
        t.hi.x = std::max(t.hi.x, q.x);
        t.hi.y = std::max(t.hi.y, q.y);
        t.hi.z = std::max(t.hi.z, q.z);
      }
      tris.push_back(t);
    };
    for (int i = 0; i < m.m_F.Count(); ++i) {
      const ON_MeshFace& f = m.m_F[i];
      add(i, f.vi[0], f.vi[1], f.vi[2]);
      if (f.IsQuad()) {
        add(i, f.vi[0], f.vi[2], f.vi[3]);
      }
    }
    return tris;
  };
  const std::vector<Tri> ta = collect(mesh_);
  const std::vector<Tri> tb = collect(other.mesh_);

  // Lower bound on the distance between two triangles: the gap between
  // their axis-aligned boxes (0 if the boxes overlap). A pair whose
  // bound already meets the best exact distance found can't improve it.
  auto box_gap_squared = [](const Tri& a, const Tri& b) {
    double g2 = 0.0;
    auto axis = [&](double alo, double ahi, double blo, double bhi) {
      const double gap = std::max(std::max(blo - ahi, alo - bhi), 0.0);
      g2 += gap * gap;
    };
    axis(a.lo.x, a.hi.x, b.lo.x, b.hi.x);
    axis(a.lo.y, a.hi.y, b.lo.y, b.hi.y);
    axis(a.lo.z, a.hi.z, b.lo.z, b.hi.z);
    return g2;
  };

  MeshDistance best;
  best.distance = std::numeric_limits<double>::infinity();
  for (const Tri& a : ta) {
    for (const Tri& b : tb) {
      if (box_gap_squared(a, b) >= best.distance * best.distance) {
        continue;
      }
      Point3d on_a, on_b;
      const double d = TriangleTriangleDistance(a.p, b.p, on_a, on_b);
      if (d < best.distance) {
        best.distance = d;
        best.point_on_this = on_a;
        best.point_on_other = on_b;
        best.face_on_this = a.face;
        best.face_on_other = b.face;
        if (d == 0.0) {
          return best;
        }
      }
    }
  }
  return best;
}

Clash Mesh::ClashWith(const Mesh& other, double distance_tolerance, double relative_volume_tolerance) const {
  if (distance_tolerance < 0.0 || relative_volume_tolerance < 0.0 || relative_volume_tolerance >= 1.0) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::ClashWith: distance_tolerance must be >= 0 and "
        "relative_volume_tolerance in [0, 1)");
  }
  // Classify by the exact overlap VOLUME rather than by edge/face
  // piercing predicates: the most ordinary CAD clash - two equal-height
  // boxes overlapping in plan - has every edge/face crossing landing
  // exactly on a face edge or lying in a face's own plane, degenerate for
  // any such predicate, whereas the overlap volume is simply 2. The
  // Manifold-backed BooleanCombine() (exact predicates with symbolic
  // perturbation, built for coincident faces) already exists for exactly
  // this kind of robustness. Throws BooleanCombine()'s own
  // std::runtime_error if either mesh isn't a closed manifold.
  // The documented precondition, checked directly: a lone open patch can
  // have a nonzero SIGNED Volume() (the origin-based tetrahedra don't
  // cancel), so "volume > 0" alone would let an open mesh through to
  // Manifold's own less specific rejection.
  if (!IsClosedManifold() || !other.IsClosedManifold()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::ClashWith: both meshes must be closed, "
        "consistently-oriented manifolds (IsClosedManifold()) - an open "
        "surface has no solid to clash");
  }
  const double volume_a = Volume();
  const double volume_b = other.Volume();
  if (volume_a <= 0.0 || volume_b <= 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::ClashWith: both meshes must enclose positive "
        "volume (wound CCW from outside) - an inside-out mesh's overlap "
        "volume would be meaningless; FlipNormals() it first");
  }
  const double overlap = BooleanCombine(*this, other, BooleanOp::Intersection).Volume();
  if (overlap >= (1.0 - relative_volume_tolerance) * volume_a) {
    return Clash::ThisInsideOther;
  }
  if (overlap >= (1.0 - relative_volume_tolerance) * volume_b) {
    return Clash::OtherInsideThis;
  }
  if (overlap > relative_volume_tolerance * std::min(volume_a, volume_b)) {
    return Clash::Intersecting;
  }
  if (DistanceTo(other).distance <= distance_tolerance) {
    return Clash::Touching;
  }
  return Clash::Clear;
}

std::vector<Vector3d> Mesh::ComputeVertexNormals() const {
  std::vector<Vector3d> normals(static_cast<size_t>(mesh_.m_V.Count()), Vector3d(0, 0, 0));

  // Accumulates a triangle's un-normalized cross product (its length is
  // twice the triangle's area) into each of its 3 vertices - summing that
  // directly, rather than a triangle normal already normalized to unit
  // length, is exactly what makes the eventual per-vertex sum
  // area-weighted instead of a plain unweighted average of directions.
  auto accumulate_triangle = [&](int i0, int i1, int i2) {
    const Point3d a(mesh_.m_V[i0]);
    const Point3d b(mesh_.m_V[i1]);
    const Point3d c(mesh_.m_V[i2]);
    const Vector3d weighted_normal = ON_CrossProduct(b - a, c - a);
    normals[static_cast<size_t>(i0)] += weighted_normal;
    normals[static_cast<size_t>(i1)] += weighted_normal;
    normals[static_cast<size_t>(i2)] += weighted_normal;
  };

  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    accumulate_triangle(f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) {
      accumulate_triangle(f.vi[0], f.vi[2], f.vi[3]);
    }
  }

  for (Vector3d& n : normals) {
    if (n.Length() > tolerance::kZero) {
      n.Unitize();
    }
  }
  return normals;
}

std::vector<Vector3d> Mesh::ComputeFaceNormals() const {
  std::vector<Vector3d> normals(static_cast<size_t>(mesh_.m_F.Count()), Vector3d(0, 0, 0));

  auto triangle_normal = [&](int i0, int i1, int i2) {
    const Point3d a(mesh_.m_V[i0]);
    const Point3d b(mesh_.m_V[i1]);
    const Point3d c(mesh_.m_V[i2]);
    return ON_CrossProduct(b - a, c - a);
  };

  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    Vector3d n = triangle_normal(f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) {
      n += triangle_normal(f.vi[0], f.vi[2], f.vi[3]);
    }
    if (n.Length() > tolerance::kZero) {
      n.Unitize();
    }
    normals[static_cast<size_t>(i)] = n;
  }
  return normals;
}

Mesh Mesh::Faceted() const {
  Mesh result;
  ON_Mesh& out = result.mesh_;
  const bool has_tc = HasTextureCoordinates();
  const bool has_colors = HasVertexColors();

  const int corner_count_upper_bound = mesh_.m_F.Count() * 4;
  out.m_V.Reserve(corner_count_upper_bound);
  out.m_F.Reserve(mesh_.m_F.Count());
  if (has_tc) out.m_S.Reserve(corner_count_upper_bound);
  if (has_colors) out.m_C.Reserve(corner_count_upper_bound);

  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    const int corner_count = f.IsQuad() ? 4 : 3;
    ON_MeshFace new_face;
    for (int c = 0; c < corner_count; ++c) {
      const int src = f.vi[c];
      const int dst = out.m_V.Count();
      out.m_V.Append(mesh_.m_V[src]);
      if (has_tc) out.m_S.Append(mesh_.m_S[src]);
      if (has_colors) out.m_C.Append(mesh_.m_C[src]);
      new_face.vi[c] = dst;
    }
    if (corner_count == 3) {
      new_face.vi[3] = new_face.vi[2];
    }
    out.m_F.Append(new_face);
  }
  return result;
}

std::vector<Point2d> Mesh::ComputeBoxMappingUVs(double scale) const {
  if (!std::isfinite(scale) || scale <= 0.0) {
    throw std::invalid_argument("dino8::kernel::Mesh::ComputeBoxMappingUVs: scale must be finite and positive");
  }

  const std::vector<Vector3d> normals = ComputeVertexNormals();
  std::vector<Point2d> uvs(static_cast<size_t>(mesh_.m_V.Count()));
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const Point3d p(mesh_.m_V[i]);
    const Vector3d& n = normals[static_cast<size_t>(i)];
    const double ax = std::fabs(n.x);
    const double ay = std::fabs(n.y);
    const double az = std::fabs(n.z);
    double u, v;
    if (ax >= ay && ax >= az) {
      u = (n.x >= 0.0 ? p.y : -p.y) / scale;
      v = p.z / scale;
    } else if (ay >= ax && ay >= az) {
      u = (n.y >= 0.0 ? -p.x : p.x) / scale;
      v = p.z / scale;
    } else {
      u = (n.z >= 0.0 ? p.x : -p.x) / scale;
      v = p.y / scale;
    }
    uvs[static_cast<size_t>(i)] = Point2d(u, v);
  }
  return uvs;
}

std::vector<Point2d> Mesh::ComputePlanarMappingUVs(double scale) const {
  if (!std::isfinite(scale) || scale <= 0.0) {
    throw std::invalid_argument("dino8::kernel::Mesh::ComputePlanarMappingUVs: scale must be finite and positive");
  }

  std::vector<Point2d> uvs(static_cast<size_t>(mesh_.m_V.Count()));
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const Point3d p(mesh_.m_V[i]);
    uvs[static_cast<size_t>(i)] = Point2d(p.x / scale, p.y / scale);
  }
  return uvs;
}

std::vector<Point2d> Mesh::ComputeCylindricalMappingUVs(double scale) const {
  if (!std::isfinite(scale) || scale <= 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::ComputeCylindricalMappingUVs: scale must be finite and positive");
  }

  const BoundingBox box = GetBoundingBox();
  const double cx = (box.min.x + box.max.x) / 2.0;
  const double cy = (box.min.y + box.max.y) / 2.0;
  std::vector<Point2d> uvs(static_cast<size_t>(mesh_.m_V.Count()));
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const Point3d p(mesh_.m_V[i]);
    const double dx = p.x - cx;
    const double dy = p.y - cy;
    if (dx == 0.0 && dy == 0.0) {
      throw std::invalid_argument(
          "dino8::kernel::Mesh::ComputeCylindricalMappingUVs: a vertex lies exactly on the "
          "mapping axis - its azimuthal angle is undefined");
    }
    const double u = std::atan2(dy, dx) / (2.0 * ON_PI) + 0.5;
    const double v = (p.z - box.min.z) / scale;
    uvs[static_cast<size_t>(i)] = Point2d(u, v);
  }
  return uvs;
}

std::vector<Point2d> Mesh::ComputeSphericalMappingUVs() const {
  const BoundingBox box = GetBoundingBox();
  const Point3d center((box.min.x + box.max.x) / 2.0, (box.min.y + box.max.y) / 2.0,
                        (box.min.z + box.max.z) / 2.0);
  std::vector<Point2d> uvs(static_cast<size_t>(mesh_.m_V.Count()));
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const Point3d p(mesh_.m_V[i]);
    Vector3d d(p.x - center.x, p.y - center.y, p.z - center.z);
    if (!d.Unitize()) {
      throw std::invalid_argument(
          "dino8::kernel::Mesh::ComputeSphericalMappingUVs: a vertex lies exactly at the "
          "mapping center - its direction is undefined");
    }
    const double u = std::atan2(d.y, d.x) / (2.0 * ON_PI) + 0.5;
    const double v = 1.0 - std::acos(std::clamp(d.z, -1.0, 1.0)) / ON_PI;
    uvs[static_cast<size_t>(i)] = Point2d(u, v);
  }
  return uvs;
}

Mesh Mesh::SplitUVSeam(const std::vector<Point2d>& uvs, double wrap_threshold) const {
  if (static_cast<int>(uvs.size()) != mesh_.m_V.Count()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::SplitUVSeam: uvs.size() must equal VertexCount()");
  }
  if (!(wrap_threshold > 0.0) || !(wrap_threshold < 1.0)) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::SplitUVSeam: wrap_threshold must be strictly between 0 and 1");
  }

  Mesh result;
  ON_Mesh& out = result.mesh_;
  const bool has_colors = HasVertexColors();
  out.m_V.Reserve(mesh_.m_V.Count());
  out.m_S.Reserve(mesh_.m_V.Count());
  if (has_colors) out.m_C.Reserve(mesh_.m_V.Count());
  out.m_F.Reserve(mesh_.m_F.Count());

  // A shared (non-seam) vertex is appended lazily, at most once, so a
  // vertex touched only by faces away from the seam keeps exactly one
  // copy - the same as before this function ran.
  std::vector<int> shared_index(static_cast<size_t>(mesh_.m_V.Count()), -1);
  auto append_shared = [&](int src) {
    int& dst = shared_index[static_cast<size_t>(src)];
    if (dst < 0) {
      dst = out.m_V.Count();
      out.m_V.Append(mesh_.m_V[src]);
      const Point2d& uv = uvs[static_cast<size_t>(src)];
      out.m_S.Append(ON_2dPoint(uv.x, uv.y));
      if (has_colors) out.m_C.Append(mesh_.m_C[src]);
    }
    return dst;
  };

  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    const int corner_count = f.IsQuad() ? 4 : 3;
    double u_min = uvs[static_cast<size_t>(f.vi[0])].x;
    double u_max = u_min;
    for (int c = 1; c < corner_count; ++c) {
      const double u = uvs[static_cast<size_t>(f.vi[c])].x;
      u_min = std::min(u_min, u);
      u_max = std::max(u_max, u);
    }

    ON_MeshFace new_face;
    if (u_max - u_min > wrap_threshold) {
      // A genuine seam crossing: duplicate every corner into its own
      // private vertex (never shared with any other face), unwrapping
      // each one's own u relative to the face's own first corner.
      const double u_ref = uvs[static_cast<size_t>(f.vi[0])].x;
      for (int c = 0; c < corner_count; ++c) {
        const int src = f.vi[c];
        double u = uvs[static_cast<size_t>(src)].x;
        if (u - u_ref > 0.5) {
          u -= 1.0;
        } else if (u_ref - u > 0.5) {
          u += 1.0;
        }
        const int dst = out.m_V.Count();
        out.m_V.Append(mesh_.m_V[src]);
        out.m_S.Append(ON_2dPoint(u, uvs[static_cast<size_t>(src)].y));
        if (has_colors) out.m_C.Append(mesh_.m_C[src]);
        new_face.vi[c] = dst;
      }
    } else {
      for (int c = 0; c < corner_count; ++c) {
        new_face.vi[c] = append_shared(f.vi[c]);
      }
    }
    if (corner_count == 3) {
      new_face.vi[3] = new_face.vi[2];
    }
    out.m_F.Append(new_face);
  }
  return result;
}

Result Mesh::SetTextureCoordinates(const std::vector<Point2d>& uvs) {
  if (static_cast<int>(uvs.size()) != mesh_.m_V.Count()) {
    return Result::Failed;
  }
  mesh_.m_S.SetCount(0);
  mesh_.m_S.Reserve(static_cast<int>(uvs.size()));
  for (const Point2d& uv : uvs) {
    mesh_.m_S.Append(ON_2dPoint(uv.x, uv.y));
  }
  return Result::Ok;
}

bool Mesh::HasTextureCoordinates() const {
  return mesh_.m_V.Count() > 0 && mesh_.m_S.Count() == mesh_.m_V.Count();
}

Point2d Mesh::TextureCoordinateAt(int vertex_index) const {
  const ON_2dPoint& s = mesh_.m_S[vertex_index];
  return Point2d(s.x, s.y);
}

Result Mesh::SetVertexColors(const std::vector<Color>& colors) {
  if (static_cast<int>(colors.size()) != mesh_.m_V.Count()) {
    return Result::Failed;
  }
  mesh_.m_C.SetCount(0);
  mesh_.m_C.Reserve(static_cast<int>(colors.size()));
  for (const Color& c : colors) {
    mesh_.m_C.Append(ON_Color(c.r, c.g, c.b));
  }
  return Result::Ok;
}

bool Mesh::HasVertexColors() const {
  return mesh_.m_V.Count() > 0 && mesh_.m_C.Count() == mesh_.m_V.Count();
}

Color Mesh::VertexColorAt(int vertex_index) const {
  const ON_Color& c = mesh_.m_C[vertex_index];
  return Color{static_cast<unsigned char>(c.Red()), static_cast<unsigned char>(c.Green()),
               static_cast<unsigned char>(c.Blue())};
}

Mesh Mesh::FlipNormals() const {
  Mesh result = *this;
  ON_Mesh& out = result.mesh_;
  for (int i = 0; i < out.m_F.Count(); ++i) {
    ON_MeshFace& f = out.m_F[i];
    if (f.IsQuad()) {
      std::swap(f.vi[0], f.vi[3]);
      std::swap(f.vi[1], f.vi[2]);
    } else {
      // A triangle's IsQuad()-false encoding requires vi[3] == vi[2];
      // reversing just vi[0]/vi[2] without also updating vi[3] would
      // break that invariant (vi[3] would keep the *old* vi[2], now
      // different from the *new* vi[2]) and silently turn a triangle
      // into what IsQuad() reads as a quad.
      std::swap(f.vi[0], f.vi[2]);
      f.vi[3] = f.vi[2];
    }
  }
  return result;
}

bool Mesh::IsClosedManifold() const {
  std::map<std::pair<int, int>, int> undirected_edge_count;
  std::map<std::pair<int, int>, int> directed_edge_first_face;
  bool orientation_consistent = true;
  int conflict_a = -1, conflict_b = -1;
  int conflict_face_first = -1, conflict_face_second = -1;

  auto visit_edge = [&](int a, int b, int face_index) {
    ++undirected_edge_count[std::minmax(a, b)];
    auto ins = directed_edge_first_face.emplace(std::make_pair(a, b), face_index);
    if (!ins.second) {
      // The same directed edge walked twice means two faces sharing this
      // edge both "walk" it the same way - a real orientation conflict
      // between neighbors, not just a coincidence.
      if (orientation_consistent) {
        conflict_a = a;
        conflict_b = b;
        conflict_face_first = ins.first->second;
        conflict_face_second = face_index;
      }
      orientation_consistent = false;
    }
  };

  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    visit_edge(f.vi[0], f.vi[1], i);
    visit_edge(f.vi[1], f.vi[2], i);
    if (f.IsQuad()) {
      visit_edge(f.vi[2], f.vi[3], i);
      visit_edge(f.vi[3], f.vi[0], i);
    } else {
      visit_edge(f.vi[2], f.vi[0], i);
    }
  }

  // Diagnostic only (DINO8_MESH_DEBUG) - behavior below is unchanged
  // either way. Confirmed directly on BooleanCombineGeneral's own sweep
  // corpus: every currently-CLOSED case (box+box) reports
  // orientation_consistent=1, bad-edge-count=0 - this check is
  // meaningful, not a chronic false positive - while every curved-face
  // case still failing (box+cylinder etc.) reports
  // orientation_consistent=0 (a directed edge walked twice - two
  // triangles both "claim" the same edge in the same winding direction)
  // ALONGSIDE a large bad-edge-count (hundreds to low thousands of
  // naked/nonmanifold edges) - two DISTINCT failure signatures, not
  // shown to share a single root cause yet. The orientation-conflict
  // signature is NOT documented anywhere in boolean_general.h's own
  // extensive closedmesh-gap writeup (which only discusses missing/
  // mismatched boundary vertices) - a genuinely new lead for a future
  // pass into TessellateGeneralBooleanClosedMesh's own fan-insertion
  // passes (StitchTJunctionsOnce, ReconcileChainToChord) to find which
  // one can emit a mis-wound fan triangle.
  int bad = 0;
  for (const auto& [edge, count] : undirected_edge_count) {
    if (count != 2) ++bad;
  }
  if (std::getenv("DINO8_MESH_DEBUG")) {
    std::fprintf(stderr, "  IsClosedManifold: orientation_consistent=%d bad-edge-count=%d / total-edges=%zu\n",
                 (int)orientation_consistent, bad, undirected_edge_count.size());
    if (!orientation_consistent && conflict_a >= 0) {
      const ON_3fPoint& pa = mesh_.m_V[conflict_a];
      const ON_3fPoint& pb = mesh_.m_V[conflict_b];
      std::fprintf(stderr,
                   "  first orientation conflict: directed edge (v%d -> v%d) walked by face %d and face %d\n"
                   "    v%d = (%.9g, %.9g, %.9g)\n    v%d = (%.9g, %.9g, %.9g)\n",
                   conflict_a, conflict_b, conflict_face_first, conflict_face_second,
                   conflict_a, (double)pa.x, (double)pa.y, (double)pa.z,
                   conflict_b, (double)pb.x, (double)pb.y, (double)pb.z);
      for (int fi : {conflict_face_first, conflict_face_second}) {
        const ON_MeshFace& f = mesh_.m_F[fi];
        std::fprintf(stderr, "    face %d: vi = [%d, %d, %d, %d]%s\n", fi,
                     f.vi[0], f.vi[1], f.vi[2], f.vi[3], f.IsQuad() ? " (quad)" : "");
      }
    }
  }
  if (!orientation_consistent) {
    return false;
  }
  return bad == 0;
}

Mesh Mesh::Transform(const ON_Xform& xform) const {
  Mesh result = *this;
  result.mesh_.Transform(xform);
  return result;
}

Result Mesh::SaveObj(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    out << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
  }
  const std::vector<Vector3d> normals = ComputeVertexNormals();
  for (const Vector3d& n : normals) {
    out << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
  }
  const bool has_uvs = HasTextureCoordinates();
  if (has_uvs) {
    for (int i = 0; i < mesh_.m_V.Count(); ++i) {
      const Point2d uv = TextureCoordinateAt(i);
      out << "vt " << uv.x << ' ' << uv.y << '\n';
    }
  }
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    // OBJ vertex indices are 1-based. Without texture coordinates, "v//vn"
    // form leaves the middle (vt) slot empty, per OBJ's own convention
    // for "no vt"; with them, "v/vt/vn" form references the same vertex
    // index for all three (this kernel has no per-face-corner UV data, so
    // vt and v always coincide here).
    auto write_corner = [&out, has_uvs](int vi) {
      if (has_uvs) {
        out << (vi + 1) << '/' << (vi + 1) << '/' << (vi + 1);
      } else {
        out << (vi + 1) << "//" << (vi + 1);
      }
    };
    out << "f ";
    write_corner(f.vi[0]);
    out << ' ';
    write_corner(f.vi[1]);
    out << ' ';
    write_corner(f.vi[2]);
    if (f.IsQuad()) {
      out << ' ';
      write_corner(f.vi[3]);
    }
    out << '\n';
  }

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadObj(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path);
  if (!in) {
    return Result::Failed;
  }

  std::vector<ON_3fPoint> positions;
  std::vector<Point2d> texture_coords;

  // One parsed face corner: a 0-based vertex index, and, if the corner
  // carried a `vt` reference, the 0-based texture-coordinate index (else
  // -1). Kept per-corner (not resolved into a shared per-vertex value
  // immediately) so a genuine UV seam - two corners sharing a `v` index
  // but naming different `vt` entries - can be detected and preserved
  // below, rather than one silently overwriting another.
  struct Corner {
    int v_index = 0;
    int vt_index = -1;
  };
  std::vector<std::vector<Corner>> faces;

  std::string line;
  while (std::getline(in, line)) {
    std::istringstream stream(line);
    std::string tag;
    stream >> tag;

    if (tag == "v") {
      double x, y, z;
      if (!(stream >> x >> y >> z)) {
        return Result::Failed;
      }
      positions.push_back(ON_3fPoint(x, y, z));
    } else if (tag == "vt") {
      double u, v;
      if (!(stream >> u >> v)) {
        return Result::Failed;
      }
      texture_coords.push_back(Point2d(u, v));
    } else if (tag == "f") {
      std::vector<Corner> corners;
      std::string token;
      while (stream >> token) {
        int v_index = 0;
        int vt_index = 0;
        bool has_vt = false;
        if (!ParseObjFaceIndex(token, v_index, vt_index, has_vt)) {
          return Result::Failed;
        }
        // Standard .obj negative-index form: -1 means "the last v/vt
        // declared so far", relative to the count at THIS point in the
        // file (which is what positions.size()/texture_coords.size()
        // already reflect, since every earlier line has already been
        // processed).
        if (v_index < 0) {
          v_index = static_cast<int>(positions.size()) + v_index + 1;
        }
        if (v_index < 1 || v_index > static_cast<int>(positions.size())) {
          return Result::Failed;  // forward/unknown reference, or out of range
        }
        Corner corner;
        corner.v_index = v_index - 1;
        if (has_vt) {
          if (vt_index < 0) {
            vt_index = static_cast<int>(texture_coords.size()) + vt_index + 1;
          }
          if (vt_index < 1 || vt_index > static_cast<int>(texture_coords.size())) {
            return Result::Failed;  // forward/unknown vt reference, or out of range
          }
          corner.vt_index = vt_index - 1;
        }
        corners.push_back(corner);
      }
      if (corners.size() < 3) {
        return Result::Failed;
      }
      faces.push_back(std::move(corners));
    }
    // Every other tag (comments, vn, g/o, mtllib/usemtl, s, ...) is
    // silently skipped - this kernel only round-trips geometry (and, now,
    // per-vertex texture coordinates).
  }

  // UV coverage is "complete" only if every declared vertex is referenced
  // by at least one has-vt corner somewhere - ON_Mesh's own "every vertex
  // or none" convention (see HasTextureCoordinates()) has no way to
  // represent partial coverage, so a single uncovered vertex (referenced
  // without a `vt`, or never referenced by any face at all) discards
  // texture coordinates for the whole mesh, same as before this seam
  // handling existed.
  std::vector<std::vector<int>> distinct_uvs_by_vertex(positions.size());
  for (const std::vector<Corner>& corners : faces) {
    for (const Corner& corner : corners) {
      if (corner.vt_index < 0) {
        continue;
      }
      std::vector<int>& seen = distinct_uvs_by_vertex[static_cast<size_t>(corner.v_index)];
      if (std::find(seen.begin(), seen.end(), corner.vt_index) == seen.end()) {
        seen.push_back(corner.vt_index);
      }
    }
  }
  bool coverage_complete = true;
  for (const std::vector<int>& seen : distinct_uvs_by_vertex) {
    if (seen.empty()) {
      coverage_complete = false;
      break;
    }
  }

  Mesh result;
  ON_Mesh& raw = result.mesh_;

  auto append_face = [&raw](const std::vector<int>& indices) { AppendPolygonFace(raw, indices); };

  if (!coverage_complete) {
    // No usable UV data (or an uncovered vertex breaks it for everyone) -
    // emit vertices/faces 1:1 against the file's own `v` indices, exactly
    // as if this vertex-splitting logic didn't exist at all.
    for (const ON_3fPoint& position : positions) {
      raw.m_V.Append(position);
    }
    for (const std::vector<Corner>& corners : faces) {
      std::vector<int> indices;
      indices.reserve(corners.size());
      for (const Corner& corner : corners) {
        indices.push_back(corner.v_index);
      }
      append_face(indices);
    }
  } else {
    // Every vertex has at least one distinct UV value. A vertex with
    // exactly one distinct value needs no duplication (the common,
    // non-seam case - this reproduces the exact same output, vertex for
    // vertex, as before this seam handling existed); a vertex with two or
    // more distinct values gets one output vertex per distinct value, and
    // each corner maps to its own matching duplicate - a real UV seam,
    // preserved instead of one value silently overwriting another.
    // output_vertex[v][k] is the output index for original vertex `v`'s
    // k-th distinct UV value (distinct_uvs_by_vertex[v][k]).
    std::vector<std::vector<int>> output_vertex(positions.size());
    std::vector<Point2d> output_uvs;
    for (size_t v = 0; v < positions.size(); ++v) {
      for (const int vt_index : distinct_uvs_by_vertex[v]) {
        output_vertex[v].push_back(raw.m_V.Count());
        raw.m_V.Append(positions[v]);
        output_uvs.push_back(texture_coords[static_cast<size_t>(vt_index)]);
      }
    }
    for (const std::vector<Corner>& corners : faces) {
      std::vector<int> indices;
      indices.reserve(corners.size());
      for (const Corner& corner : corners) {
        const std::vector<int>& variants = output_vertex[static_cast<size_t>(corner.v_index)];
        if (corner.vt_index < 0) {
          // No per-corner UV to disambiguate a seam vertex by - use its
          // first-seen variant, the same "no correct answer" choice
          // LoadObj()'s own doc comment discloses.
          indices.push_back(variants.front());
        } else {
          const std::vector<int>& seen = distinct_uvs_by_vertex[static_cast<size_t>(corner.v_index)];
          const auto it = std::find(seen.begin(), seen.end(), corner.vt_index);
          indices.push_back(variants[static_cast<size_t>(it - seen.begin())]);
        }
      }
      append_face(indices);
    }
    result.SetTextureCoordinates(output_uvs);
  }

  out_mesh = std::move(result);
  return Result::Ok;
}

Result Mesh::SaveStl(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  auto write_facet = [&out](const ON_3fPoint& a, const ON_3fPoint& b, const ON_3fPoint& c) {
    ON_3dVector normal = ON_3dVector::CrossProduct(ON_3dVector(b - a), ON_3dVector(c - a));
    normal.Unitize();
    out << "facet normal " << normal.x << ' ' << normal.y << ' ' << normal.z << '\n';
    out << "outer loop\n";
    out << "vertex " << a.x << ' ' << a.y << ' ' << a.z << '\n';
    out << "vertex " << b.x << ' ' << b.y << ' ' << b.z << '\n';
    out << "vertex " << c.x << ' ' << c.y << ' ' << c.z << '\n';
    out << "endloop\n";
    out << "endfacet\n";
  };

  out << "solid dino8\n";
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    const ON_3fPoint& a = mesh_.m_V[f.vi[0]];
    const ON_3fPoint& b = mesh_.m_V[f.vi[1]];
    const ON_3fPoint& c = mesh_.m_V[f.vi[2]];
    write_facet(a, b, c);
    if (f.IsQuad()) {
      write_facet(a, c, mesh_.m_V[f.vi[3]]);
    }
  }
  out << "endsolid dino8\n";

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::SaveStlBinary(const std::string& path) const {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return Result::Failed;
  }

  const char header[80] = {0};
  out.write(header, sizeof(header));

  uint32_t triangle_count = 0;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    triangle_count += mesh_.m_F[i].IsQuad() ? 2 : 1;
  }
  out.write(reinterpret_cast<const char*>(&triangle_count), sizeof(triangle_count));

  auto write_facet = [&out](const ON_3fPoint& a, const ON_3fPoint& b, const ON_3fPoint& c) {
    ON_3dVector normal_d = ON_3dVector::CrossProduct(ON_3dVector(b - a), ON_3dVector(c - a));
    normal_d.Unitize();
    const float normal[3] = {static_cast<float>(normal_d.x), static_cast<float>(normal_d.y),
                              static_cast<float>(normal_d.z)};
    out.write(reinterpret_cast<const char*>(normal), sizeof(normal));
    const float pa[3] = {a.x, a.y, a.z};
    const float pb[3] = {b.x, b.y, b.z};
    const float pc[3] = {c.x, c.y, c.z};
    out.write(reinterpret_cast<const char*>(pa), sizeof(pa));
    out.write(reinterpret_cast<const char*>(pb), sizeof(pb));
    out.write(reinterpret_cast<const char*>(pc), sizeof(pc));
    const uint16_t attribute_byte_count = 0;
    out.write(reinterpret_cast<const char*>(&attribute_byte_count), sizeof(attribute_byte_count));
  };

  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    const ON_3fPoint& a = mesh_.m_V[f.vi[0]];
    const ON_3fPoint& b = mesh_.m_V[f.vi[1]];
    const ON_3fPoint& c = mesh_.m_V[f.vi[2]];
    write_facet(a, b, c);
    if (f.IsQuad()) {
      write_facet(a, c, mesh_.m_V[f.vi[3]]);
    }
  }

  return out.good() ? Result::Ok : Result::Failed;
}

namespace {

Result LoadAsciiStl(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path);
  if (!in) {
    return Result::Failed;
  }

  Mesh result;
  ON_Mesh& raw = result.raw();

  // Accumulates the current facet's 3 vertices (x,y,z flattened) between
  // "outer loop" and "endloop" - checked for exactly 9 values at
  // "endfacet" rather than trusting the file's own structure.
  std::vector<double> pending_vertices;

  std::string line;
  while (std::getline(in, line)) {
    std::istringstream stream(line);
    std::string tag;
    stream >> tag;

    if (tag == "vertex") {
      double x, y, z;
      if (!(stream >> x >> y >> z)) {
        return Result::Failed;
      }
      pending_vertices.push_back(x);
      pending_vertices.push_back(y);
      pending_vertices.push_back(z);
    } else if (tag == "endfacet") {
      if (pending_vertices.size() != 9) {
        return Result::Failed;  // not exactly 3 vertices for this facet
      }
      ON_MeshFace face;
      for (int i = 0; i < 3; ++i) {
        face.vi[i] = raw.m_V.Count();
        raw.m_V.Append(ON_3fPoint(pending_vertices[static_cast<size_t>(i) * 3 + 0],
                                   pending_vertices[static_cast<size_t>(i) * 3 + 1],
                                   pending_vertices[static_cast<size_t>(i) * 3 + 2]));
      }
      face.vi[3] = face.vi[2];
      raw.m_F.Append(face);
      pending_vertices.clear();
    }
    // "solid ...", "endsolid ...", "facet normal ..." (its own values
    // discarded - see LoadStl()'s own comment on why), "outer loop", and
    // "endloop" are all silently skipped.
  }

  out_mesh = std::move(result);
  return Result::Ok;
}

// Binary STL: an 80-byte header (arbitrary content, not parsed), a
// little-endian uint32 triangle count, then that many 50-byte records
// (3 floats facet normal - discarded, same reason the ASCII parser
// discards "facet normal" - then 3x3 floats for the triangle's vertices,
// then a 2-byte "attribute byte count" almost universally zero and
// discarded here too, since this kernel's ON_Mesh has nowhere to put
// per-facet attribute data). Assumes a little-endian host (reads the
// on-disk bytes directly into a float/uint32_t) - true for every
// platform this kernel is actually built on (x86_64, aarch64), and
// matches every other binary-format assumption already made elsewhere
// in this codebase (none of which handle big-endian either).
Result LoadBinaryStl(const std::string& path, uint32_t triangle_count, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }
  in.seekg(84, std::ios::beg);

  Mesh result;
  ON_Mesh& raw = result.raw();
  raw.m_V.Reserve(static_cast<int>(triangle_count) * 3);
  raw.m_F.Reserve(static_cast<int>(triangle_count));

  for (uint32_t t = 0; t < triangle_count; ++t) {
    float normal[3];
    if (!in.read(reinterpret_cast<char*>(normal), sizeof(normal))) {
      return Result::Failed;
    }
    ON_MeshFace face;
    for (int i = 0; i < 3; ++i) {
      float xyz[3];
      if (!in.read(reinterpret_cast<char*>(xyz), sizeof(xyz))) {
        return Result::Failed;
      }
      // The raw bytes can encode NaN/Inf, which the ASCII path can never
      // produce (operator>> refuses "nan"/"inf"/overflowing tokens) - and
      // which, if let through, silently poisons every downstream query
      // on the returned mesh (Volume()/GetCentroid() go NaN, the vertex
      // never welds) rather than failing here. See LoadStl()'s doc comment.
      if (!std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2])) {
        return Result::Failed;
      }
      face.vi[i] = raw.m_V.Count();
      raw.m_V.Append(ON_3fPoint(xyz[0], xyz[1], xyz[2]));
    }
    face.vi[3] = face.vi[2];
    raw.m_F.Append(face);

    uint16_t attribute_byte_count = 0;
    if (!in.read(reinterpret_cast<char*>(&attribute_byte_count), sizeof(attribute_byte_count))) {
      return Result::Failed;
    }
  }

  out_mesh = std::move(result);
  return Result::Ok;
}

// One "property <type> <name>" or "property list <count_type> <type>
// <name>" line from a PLY header. `type`/`count_type`/`value_type` are
// kept (not just parsed and discarded) so a binary-format payload can be
// read at each property's own declared byte width - see PlyTypeInfo().
struct PlyProperty {
  bool is_list = false;
  std::string name;
  std::string type;        // scalar property's own type
  std::string count_type;  // list property's own count type (e.g. "uchar")
  std::string value_type;  // list property's own element type (e.g. "int")
};

// One "element <name> <count>" block from a PLY header, plus the
// property lines that followed it.
struct PlyElement {
  std::string name;
  int count = 0;
  std::vector<PlyProperty> properties;
};

// Byte width and representation of a PLY scalar type name, covering both
// spellings the spec allows (the "short" char/uchar/short/... names and
// the "long" int8/uint8/int16/... ones). `bytes == 0` means this kernel
// doesn't recognize the name (a legitimate PLY type this kernel has no
// use for, e.g. int64/uint64 - genuinely out of scope, see
// ReadPlyBinaryScalar()).
struct PlyTypeInfo {
  int bytes = 0;
  bool is_float = false;
  bool is_signed = false;
};

PlyTypeInfo LookupPlyType(const std::string& type) {
  if (type == "char" || type == "int8") return {1, false, true};
  if (type == "uchar" || type == "uint8") return {1, false, false};
  if (type == "short" || type == "int16") return {2, false, true};
  if (type == "ushort" || type == "uint16") return {2, false, false};
  if (type == "int" || type == "int32") return {4, false, true};
  if (type == "uint" || type == "uint32") return {4, false, false};
  if (type == "float" || type == "float32") return {4, true, false};
  if (type == "double" || type == "float64") return {8, true, false};
  return {};  // bytes == 0: unrecognized
}

// Reverses the byte order of an `n`-byte buffer in place (n <= 8). A
// no-op for n <= 1, so callers can route every property through this
// unconditionally without special-casing single-byte types.
void SwapBytesInPlace(char* buf, int n) {
  for (int i = 0; i < n / 2; ++i) {
    std::swap(buf[i], buf[n - 1 - i]);
  }
}

// Reads one binary-encoded scalar of `type` from `in`, widened to
// double. This kernel's own host is little-endian (the same assumption
// Mesh::LoadStl()'s own LoadBinaryStl() makes and documents), so
// `big_endian` - read from the file's own `format` header line, see
// ParsePlyHeader() - controls whether the raw bytes are reversed before
// being interpreted as the host's native representation; a
// binary_little_endian file leaves them untouched. Returns
// false (and leaves `out` untouched) on a short read or an unrecognized
// type, never on a value out of some expected range - callers validate
// the widened double themselves (e.g. a face index or corner count).
bool ReadPlyBinaryScalar(std::istream& in, const std::string& type, bool big_endian, double& out) {
  const PlyTypeInfo info = LookupPlyType(type);
  if (info.bytes == 0) return false;
  char buf[8];
  if (!in.read(buf, info.bytes)) return false;
  if (big_endian) SwapBytesInPlace(buf, info.bytes);
  if (info.is_float) {
    if (info.bytes == 4) {
      float v;
      std::memcpy(&v, buf, sizeof(v));
      out = v;
    } else {
      double v;
      std::memcpy(&v, buf, sizeof(v));
      out = v;
    }
  } else if (info.is_signed) {
    switch (info.bytes) {
      case 1: { int8_t v; std::memcpy(&v, buf, sizeof(v)); out = v; break; }
      case 2: { int16_t v; std::memcpy(&v, buf, sizeof(v)); out = v; break; }
      default: { int32_t v; std::memcpy(&v, buf, sizeof(v)); out = v; break; }
    }
  } else {
    switch (info.bytes) {
      case 1: { uint8_t v; std::memcpy(&v, buf, sizeof(v)); out = v; break; }
      case 2: { uint16_t v; std::memcpy(&v, buf, sizeof(v)); out = v; break; }
      default: { uint32_t v; std::memcpy(&v, buf, sizeof(v)); out = v; break; }
    }
  }
  return true;
}

// Strips one trailing '\r' - std::getline() on a stream opened in binary
// mode (which LoadPly() needs for its own binary payload - see its own
// doc comment) leaves a CRLF line ending's '\r' in place, unlike text
// mode's automatic translation, so a Windows-authored .ply's header
// lines would otherwise fail to compare equal to their Unix-line-ending
// literals below.
std::string TrimTrailingCr(std::string s) {
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

// Parses a PLY header (everything up to and including "end_header") into
// an ordered list of elements, and reports via `out_binary`/`out_big_endian`
// whether the format line was "binary_little_endian" or
// "binary_big_endian" rather than "ascii" (`out_big_endian` is only
// meaningful when `out_binary` is true). Returns false on any header line
// this kernel doesn't recognize, a "property" line before any "element"
// line, or a "format" line that isn't exactly "format ascii <version>",
// "format binary_little_endian <version>", or
// "format binary_big_endian <version>".
bool ParsePlyHeader(std::istream& in, std::vector<PlyElement>& out_elements, bool& out_binary,
                     bool& out_big_endian) {
  out_binary = false;
  out_big_endian = false;
  std::string line;
  if (!std::getline(in, line) || TrimTrailingCr(line) != "ply") {
    return false;
  }
  if (!std::getline(in, line)) {
    return false;
  }
  line = TrimTrailingCr(line);
  {
    std::istringstream header(line);
    std::string tag, format;
    if (!(header >> tag >> format) || tag != "format") {
      return false;
    }
    if (format == "ascii") {
      out_binary = false;
    } else if (format == "binary_little_endian") {
      out_binary = true;
      out_big_endian = false;
    } else if (format == "binary_big_endian") {
      out_binary = true;
      out_big_endian = true;
    } else {
      return false;  // anything else: unrecognized format
    }
  }
  while (std::getline(in, line)) {
    line = TrimTrailingCr(line);
    std::istringstream stream(line);
    std::string tag;
    stream >> tag;
    if (tag == "comment" || tag.empty()) {
      continue;
    }
    if (tag == "end_header") {
      return true;
    }
    if (tag == "element") {
      PlyElement element;
      if (!(stream >> element.name >> element.count) || element.count < 0) {
        return false;
      }
      out_elements.push_back(std::move(element));
      continue;
    }
    if (tag == "property") {
      if (out_elements.empty()) {
        return false;  // property line before any element line
      }
      std::string type;
      if (!(stream >> type)) {
        return false;
      }
      PlyProperty property;
      if (type == "list") {
        std::string count_type, value_type;
        if (!(stream >> count_type >> value_type >> property.name)) {
          return false;
        }
        property.is_list = true;
        property.count_type = count_type;
        property.value_type = value_type;
      } else {
        if (!(stream >> property.name)) {
          return false;
        }
        property.type = type;
      }
      out_elements.back().properties.push_back(std::move(property));
      continue;
    }
    return false;  // unrecognized header line
  }
  return false;  // stream ended without "end_header"
}

// Writes one binary-encoded scalar to `out`, byte-swapped first when
// `big_endian` is true - the write-side mirror of ReadPlyBinaryScalar()'s
// swap-after-read. A no-op-swap for sizeof(T) == 1 (the face corner
// count's `uchar`), so callers can route every property through this
// unconditionally.
template <typename T>
void WriteBinaryScalar(std::ostream& out, T value, bool big_endian) {
  char buf[sizeof(T)];
  std::memcpy(buf, &value, sizeof(T));
  if (big_endian) SwapBytesInPlace(buf, sizeof(T));
  out.write(buf, sizeof(T));
}

}  // namespace

Result Mesh::SavePly(const std::string& path, bool binary, bool big_endian) const {
  // Binary mode throughout: a no-op difference for the ASCII payload (the
  // '\n' bytes written below are already exactly what text mode would
  // translate to on any platform this kernel builds for), but required
  // for the binary payload to reach disk untranslated - see LoadPly()'s
  // own doc comment for the read-side half of this.
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return Result::Failed;
  }

  const std::vector<Vector3d> normals = ComputeVertexNormals();
  const bool has_uvs = HasTextureCoordinates();
  const bool has_colors = HasVertexColors();

  const std::string format = !binary ? "ascii" : (big_endian ? "binary_big_endian" : "binary_little_endian");
  out << "ply\n";
  out << "format " << format << " 1.0\n";
  out << "comment written by dino8-kernel\n";
  out << "element vertex " << mesh_.m_V.Count() << '\n';
  out << "property float x\n";
  out << "property float y\n";
  out << "property float z\n";
  out << "property float nx\n";
  out << "property float ny\n";
  out << "property float nz\n";
  if (has_uvs) {
    out << "property float u\n";
    out << "property float v\n";
  }
  if (has_colors) {
    out << "property uchar red\n";
    out << "property uchar green\n";
    out << "property uchar blue\n";
  }
  out << "element face " << mesh_.m_F.Count() << '\n';
  out << "property list uchar int vertex_indices\n";
  out << "end_header\n";

  auto write_f32 = [&](double v) {
    const float f = static_cast<float>(v);
    WriteBinaryScalar(out, f, big_endian);
  };

  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& p = mesh_.m_V[i];
    const Vector3d& n = normals[static_cast<size_t>(i)];
    if (binary) {
      write_f32(p.x);
      write_f32(p.y);
      write_f32(p.z);
      write_f32(n.x);
      write_f32(n.y);
      write_f32(n.z);
      if (has_uvs) {
        const Point2d uv = TextureCoordinateAt(i);
        write_f32(uv.x);
        write_f32(uv.y);
      }
      if (has_colors) {
        const Color c = VertexColorAt(i);
        WriteBinaryScalar(out, static_cast<uint8_t>(c.r), big_endian);
        WriteBinaryScalar(out, static_cast<uint8_t>(c.g), big_endian);
        WriteBinaryScalar(out, static_cast<uint8_t>(c.b), big_endian);
      }
    } else {
      out << p.x << ' ' << p.y << ' ' << p.z << ' ' << n.x << ' ' << n.y << ' ' << n.z;
      if (has_uvs) {
        const Point2d uv = TextureCoordinateAt(i);
        out << ' ' << uv.x << ' ' << uv.y;
      }
      if (has_colors) {
        const Color c = VertexColorAt(i);
        out << ' ' << static_cast<int>(c.r) << ' ' << static_cast<int>(c.g) << ' ' << static_cast<int>(c.b);
      }
      out << '\n';
    }
  }
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    const bool quad = f.IsQuad();
    if (binary) {
      const uint8_t count = quad ? 4 : 3;
      WriteBinaryScalar(out, count, big_endian);
      for (uint8_t c = 0; c < count; ++c) {
        WriteBinaryScalar(out, static_cast<int32_t>(f.vi[c]), big_endian);
      }
    } else if (quad) {
      out << "4 " << f.vi[0] << ' ' << f.vi[1] << ' ' << f.vi[2] << ' ' << f.vi[3] << '\n';
    } else {
      out << "3 " << f.vi[0] << ' ' << f.vi[1] << ' ' << f.vi[2] << '\n';
    }
  }

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadPly(const std::string& path, Mesh& out_mesh) {
  // Binary mode throughout - the header is plain ASCII either way
  // (TrimTrailingCr() in ParsePlyHeader() strips a CRLF line ending's
  // stray '\r', which text mode would otherwise have translated away for
  // us), and a binary-format payload needs its raw bytes untranslated.
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }

  std::vector<PlyElement> elements;
  bool is_binary = false;
  bool is_big_endian = false;
  if (!ParsePlyHeader(in, elements, is_binary, is_big_endian)) {
    return Result::Failed;
  }

  // Reads one element row's scalar properties, in the encoding
  // ParsePlyHeader() already determined: whitespace-separated ASCII
  // tokens on one line, or each property's own binary width in order.
  auto read_scalar_row = [&](const PlyElement& element, std::vector<double>& values) {
    values.assign(element.properties.size(), 0.0);
    if (!is_binary) {
      std::string line;
      if (!std::getline(in, line)) return false;
      std::istringstream stream(line);
      for (double& value : values) {
        if (!(stream >> value)) return false;
      }
      return true;
    }
    for (size_t i = 0; i < element.properties.size(); ++i) {
      if (!ReadPlyBinaryScalar(in, element.properties[i].type, is_big_endian, values[i])) return false;
    }
    return true;
  };

  Mesh result;
  ON_Mesh& raw = result.raw();
  bool found_vertex = false;
  bool found_face = false;
  std::vector<Point2d> uvs;
  bool have_uvs = false;
  std::vector<Color> colors;
  bool have_colors = false;

  for (const PlyElement& element : elements) {
    if (element.name == "vertex") {
      found_vertex = true;
      int idx_x = -1, idx_y = -1, idx_z = -1, idx_u = -1, idx_v = -1;
      int idx_r = -1, idx_g = -1, idx_b = -1;
      for (size_t i = 0; i < element.properties.size(); ++i) {
        const PlyProperty& property = element.properties[i];
        if (property.is_list) {
          return Result::Failed;  // a list property on a vertex isn't a position/normal/UV/color
        }
        if (property.name == "x") idx_x = static_cast<int>(i);
        else if (property.name == "y") idx_y = static_cast<int>(i);
        else if (property.name == "z") idx_z = static_cast<int>(i);
        else if (property.name == "u") idx_u = static_cast<int>(i);
        else if (property.name == "v") idx_v = static_cast<int>(i);
        else if (property.name == "red") idx_r = static_cast<int>(i);
        else if (property.name == "green") idx_g = static_cast<int>(i);
        else if (property.name == "blue") idx_b = static_cast<int>(i);
        // nx/ny/nz and any other property (alpha, ...) are read as plain
        // columns below but never looked up by name - discarded, same
        // "always geometry-derived" convention as LoadObj()'s vn.
      }
      if (idx_x < 0 || idx_y < 0 || idx_z < 0) {
        return Result::Failed;
      }
      have_uvs = idx_u >= 0 && idx_v >= 0;
      have_colors = idx_r >= 0 && idx_g >= 0 && idx_b >= 0;

      // Rounds a raw property value (whatever scalar type it was declared
      // as - `uchar` 0-255 the ordinary case, but a `float`/`double` file
      // from another tool is tolerated too) to the 0-255 byte Color
      // holds, clamping rather than wrapping/truncating an out-of-range
      // value (e.g. a malformed or negative one).
      auto to_byte = [](double v) -> unsigned char {
        if (v < 0.0) return 0;
        if (v > 255.0) return 255;
        return static_cast<unsigned char>(v + 0.5);
      };

      std::vector<double> values;
      for (int row = 0; row < element.count; ++row) {
        if (!read_scalar_row(element, values)) {
          return Result::Failed;
        }
        raw.m_V.Append(ON_3fPoint(values[static_cast<size_t>(idx_x)],
                                   values[static_cast<size_t>(idx_y)],
                                   values[static_cast<size_t>(idx_z)]));
        if (have_uvs) {
          uvs.push_back(Point2d(values[static_cast<size_t>(idx_u)], values[static_cast<size_t>(idx_v)]));
        }
        if (have_colors) {
          colors.push_back(Color{to_byte(values[static_cast<size_t>(idx_r)]),
                                  to_byte(values[static_cast<size_t>(idx_g)]),
                                  to_byte(values[static_cast<size_t>(idx_b)])});
        }
      }
    } else if (element.name == "face") {
      found_face = true;
      if (element.properties.size() != 1 || !element.properties[0].is_list) {
        return Result::Failed;  // this kernel only reads the ordinary "one index list" face shape
      }
      const PlyProperty& list_property = element.properties[0];
      for (int row = 0; row < element.count; ++row) {
        int corner_count = 0;
        int indices[4] = {0, 0, 0, 0};
        if (!is_binary) {
          std::string line;
          if (!std::getline(in, line)) {
            return Result::Failed;
          }
          std::istringstream stream(line);
          if (!(stream >> corner_count) || corner_count < 3 || corner_count > 4) {
            return Result::Failed;
          }
          for (int i = 0; i < corner_count; ++i) {
            if (!(stream >> indices[i]) || indices[i] < 0 || indices[i] >= raw.m_V.Count()) {
              return Result::Failed;
            }
          }
        } else {
          double count_value;
          if (!ReadPlyBinaryScalar(in, list_property.count_type, is_big_endian, count_value)) {
            return Result::Failed;
          }
          corner_count = static_cast<int>(count_value);
          if (corner_count < 3 || corner_count > 4) {
            return Result::Failed;
          }
          for (int i = 0; i < corner_count; ++i) {
            double index_value;
            if (!ReadPlyBinaryScalar(in, list_property.value_type, is_big_endian, index_value)) {
              return Result::Failed;
            }
            indices[i] = static_cast<int>(index_value);
            if (indices[i] < 0 || indices[i] >= raw.m_V.Count()) {
              return Result::Failed;
            }
          }
        }
        ON_MeshFace face;
        face.vi[0] = indices[0];
        face.vi[1] = indices[1];
        face.vi[2] = indices[2];
        face.vi[3] = (corner_count == 4) ? indices[3] : indices[2];
        raw.m_F.Append(face);
      }
    } else if (!is_binary) {
      // An element type this kernel doesn't read (e.g. a color-only
      // "edge" element) - skip its data lines rather than rejecting the
      // file over data this kernel was never going to use.
      std::string line;
      for (int row = 0; row < element.count; ++row) {
        if (!std::getline(in, line)) {
          return Result::Failed;
        }
      }
    } else {
      // Same skip, but a binary payload has no line breaks to skip by -
      // every property of every row must still be read (at its own
      // declared width) to keep the stream aligned for whatever element
      // follows this one.
      double scalar;
      for (int row = 0; row < element.count; ++row) {
        for (const PlyProperty& property : element.properties) {
          if (property.is_list) {
            double count_value;
            if (!ReadPlyBinaryScalar(in, property.count_type, is_big_endian, count_value)) {
              return Result::Failed;
            }
            const int count = static_cast<int>(count_value);
            if (count < 0) return Result::Failed;
            for (int i = 0; i < count; ++i) {
              if (!ReadPlyBinaryScalar(in, property.value_type, is_big_endian, scalar)) return Result::Failed;
            }
          } else if (!ReadPlyBinaryScalar(in, property.type, is_big_endian, scalar)) {
            return Result::Failed;
          }
        }
      }
    }
  }

  if (!found_vertex || !found_face) {
    return Result::Failed;
  }
  if (have_uvs) {
    if (static_cast<int>(uvs.size()) != raw.m_V.Count()) {
      return Result::Failed;  // can only happen if the header lied about the vertex count
    }
    result.SetTextureCoordinates(uvs);
  }
  if (have_colors) {
    if (static_cast<int>(colors.size()) != raw.m_V.Count()) {
      return Result::Failed;  // can only happen if the header lied about the vertex count
    }
    result.SetVertexColors(colors);
  }

  out_mesh = std::move(result);
  return Result::Ok;
}

namespace {

// Reads the next whitespace-delimited token from an OFF file, skipping
// any `#`-to-end-of-line comment along the way (a comment can start a
// standalone line or trail after real data - both are handled the same
// way here: once a token starting with '#' is seen, the rest of ITS line
// is discarded and reading resumes at the next token). Returns false at
// end of file.
bool NextOffToken(std::istream& in, std::string& token) {
  while (in >> token) {
    if (token[0] == '#') {
      std::string rest;
      std::getline(in, rest);
      continue;
    }
    return true;
  }
  return false;
}

// Strict whole-token int/double parsing - same "must consume the entire
// field, not just a valid prefix of it" rigor ParseObjIndexField() above
// already applies to a .obj face index, so an OFF token like "3abc" or
// "3.5" (where an integer is expected) is rejected rather than silently
// truncated to "3".
bool ParseOffInt(const std::string& token, int& value) {
  if (token.empty()) return false;
  size_t consumed = 0;
  int parsed = 0;
  try {
    parsed = std::stoi(token, &consumed);
  } catch (const std::exception&) {
    return false;
  }
  if (consumed != token.size()) return false;
  value = parsed;
  return true;
}

bool ParseOffDouble(const std::string& token, double& value) {
  if (token.empty()) return false;
  size_t consumed = 0;
  double parsed = 0;
  try {
    parsed = std::stod(token, &consumed);
  } catch (const std::exception&) {
    return false;
  }
  if (consumed != token.size()) return false;
  value = parsed;
  return true;
}

}  // namespace

Result Mesh::SaveOff(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  const bool write_colors = HasVertexColors();
  out << (write_colors ? "COFF\n" : "OFF\n");
  out << mesh_.m_V.Count() << ' ' << mesh_.m_F.Count() << " 0\n";
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    out << v.x << ' ' << v.y << ' ' << v.z;
    if (write_colors) {
      const Color c = VertexColorAt(i);
      out << ' ' << static_cast<int>(c.r) << ' ' << static_cast<int>(c.g) << ' '
          << static_cast<int>(c.b) << " 255";
    }
    out << '\n';
  }
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    if (f.IsQuad()) {
      out << "4 " << f.vi[0] << ' ' << f.vi[1] << ' ' << f.vi[2] << ' ' << f.vi[3] << '\n';
    } else {
      out << "3 " << f.vi[0] << ' ' << f.vi[1] << ' ' << f.vi[2] << '\n';
    }
  }

  return out.good() ? Result::Ok : Result::Failed;
}

// OFF's vertex/face counts sit in the header with no data behind them yet -
// a handful of bytes ("COFF\n2147483647 0 0\n") is enough to declare a
// vertex_count near INT_MAX. Used unchecked, that count used to go straight
// into colors.reserve() below (and drives the vertex-loop bound either way),
// so that one line alone forced a multi-gigabyte allocation before a single
// real vertex was read - the same untrusted-file-count hazard already fixed
// for PLY import (see FileExchange.cpp's kMaxPlyListCount) and Dino Flow's
// node inputs. Real OFF meshes/scans never approach this; it's headroom
// above any legitimate use, just low enough to reject the lie outright.
constexpr int kMaxOffElementCount = 200'000'000;

Result Mesh::LoadOff(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path);
  if (!in) {
    return Result::Failed;
  }

  std::string token;
  if (!NextOffToken(in, token) || (token != "OFF" && token != "COFF")) {
    return Result::Failed;  // missing header, or an NOFF/4OFF/STOFF
                             // variant this parser doesn't support
  }
  const bool is_coff = (token == "COFF");

  int vertex_count = 0, face_count = 0, edge_count = 0;
  if (!NextOffToken(in, token) || !ParseOffInt(token, vertex_count)) return Result::Failed;
  if (!NextOffToken(in, token) || !ParseOffInt(token, face_count)) return Result::Failed;
  if (!NextOffToken(in, token) || !ParseOffInt(token, edge_count)) return Result::Failed;
  (void)edge_count;  // read but unused - see LoadOff()'s own doc comment
  if (vertex_count < 0 || face_count < 0 || vertex_count > kMaxOffElementCount ||
      face_count > kMaxOffElementCount) {
    return Result::Failed;
  }

  Mesh result;
  ON_Mesh& raw = result.mesh_;
  std::vector<Color> colors;
  if (is_coff) colors.reserve(static_cast<size_t>(vertex_count));

  for (int i = 0; i < vertex_count; ++i) {
    double x, y, z;
    if (!NextOffToken(in, token) || !ParseOffDouble(token, x)) return Result::Failed;
    if (!NextOffToken(in, token) || !ParseOffDouble(token, y)) return Result::Failed;
    if (!NextOffToken(in, token) || !ParseOffDouble(token, z)) return Result::Failed;
    raw.m_V.Append(ON_3fPoint(x, y, z));
    if (is_coff) {
      int r = 0, g = 0, b = 0, a = 0;
      if (!NextOffToken(in, token) || !ParseOffInt(token, r) || r < 0 || r > 255) return Result::Failed;
      if (!NextOffToken(in, token) || !ParseOffInt(token, g) || g < 0 || g > 255) return Result::Failed;
      if (!NextOffToken(in, token) || !ParseOffInt(token, b) || b < 0 || b > 255) return Result::Failed;
      if (!NextOffToken(in, token) || !ParseOffInt(token, a) || a < 0 || a > 255) return Result::Failed;
      colors.push_back(Color{static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                              static_cast<unsigned char>(b)});
    }
  }

  for (int i = 0; i < face_count; ++i) {
    int n = 0;
    if (!NextOffToken(in, token) || !ParseOffInt(token, n)) return Result::Failed;
    if (n < 3) {
      return Result::Failed;
    }
    std::vector<int> indices(static_cast<size_t>(n));
    for (int c = 0; c < n; ++c) {
      if (!NextOffToken(in, token)) return Result::Failed;
      int idx = 0;
      if (!ParseOffInt(token, idx)) return Result::Failed;
      if (idx < 0 || idx >= vertex_count) {
        return Result::Failed;
      }
      indices[static_cast<size_t>(c)] = idx;
    }
    AppendPolygonFace(raw, indices);
  }

  if (is_coff && !colors.empty()) {
    result.SetVertexColors(colors);
  }
  out_mesh = std::move(result);
  return Result::Ok;
}

namespace {

// Trims leading/trailing ASCII whitespace - used when pulling a leaf
// value's text out of an AMF file, since the text between e.g. `<x>` and
// `</x>` commonly carries surrounding newlines/indentation that must not
// be fed to ParseOffDouble()/ParseOffInt() above (which require the WHOLE
// string to be the number, no surrounding slack).
std::string TrimAmfWhitespace(const std::string& s) {
  size_t begin = 0;
  while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
  size_t end = s.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  return s.substr(begin, end - begin);
}

// Finds the next opening tag named exactly `tag_name` (e.g. "vertex") at
// or after `from`, tolerating attributes and both `<tag>`/`<tag/>` forms -
// but NOT a longer tag name that merely starts with the same characters
// (e.g. searching for "vertex" must not match "vertices"): the character
// immediately following the name must be '>', '/', or whitespace.
size_t FindAmfOpenTag(const std::string& text, const std::string& tag_name, size_t from) {
  const std::string needle = "<" + tag_name;
  size_t pos = from;
  while (true) {
    pos = text.find(needle, pos);
    if (pos == std::string::npos) return std::string::npos;
    size_t after = pos + needle.size();
    if (after < text.size()) {
      char c = text[after];
      if (c == '>' || c == '/' || std::isspace(static_cast<unsigned char>(c))) {
        return pos;
      }
    }
    pos = after;
  }
}

// Extracts the content between an AMF element's own open and close tags -
// `<tag_name ...>CONTENT</tag_name>` - searching for the open tag at or
// after `from`. On success, `content` gets everything between the '>' of
// the open tag and the start of the matching `</tag_name>`, and
// `next_from` is set just past that close tag, so a caller can keep
// scanning for a further sibling of the same name (e.g. a second
// `<vertex>`). A self-closing `<tag_name/>` yields an empty `content`.
// Returns false if no (further) open tag of this name exists, or an open
// tag is never closed (`>` missing, or the matching close tag never
// appears).
bool ExtractAmfElement(const std::string& text, const std::string& tag_name, size_t from,
                        std::string& content, size_t& next_from) {
  size_t open = FindAmfOpenTag(text, tag_name, from);
  if (open == std::string::npos) return false;
  size_t gt = text.find('>', open);
  if (gt == std::string::npos) return false;
  if (gt > open && text[gt - 1] == '/') {
    content.clear();
    next_from = gt + 1;
    return true;
  }
  const std::string close_tag = "</" + tag_name + ">";
  size_t close = text.find(close_tag, gt + 1);
  if (close == std::string::npos) return false;
  content = text.substr(gt + 1, close - gt - 1);
  next_from = close + close_tag.size();
  return true;
}

// Extracts a leaf element's own trimmed text content (e.g. the "1.5" in
// "  <x>1.5</x>  ") from within `scope` - a narrower substring the caller
// has already isolated (one `<coordinates>`'s content, or one
// `<triangle>`'s content), so the first match found within it IS the
// right one, not a same-named sibling belonging to a different
// vertex/triangle.
bool ExtractAmfLeafText(const std::string& scope, const std::string& tag_name, std::string& out_text) {
  std::string content;
  size_t next_from = 0;
  if (!ExtractAmfElement(scope, tag_name, 0, content, next_from)) return false;
  out_text = TrimAmfWhitespace(content);
  return true;
}

}  // namespace

Result Mesh::SaveAmf(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out << "<amf unit=\"millimeter\">\n";
  out << " <object id=\"0\">\n";
  out << "  <mesh>\n";
  out << "   <vertices>\n";
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    out << "    <vertex>\n";
    out << "     <coordinates>\n";
    out << "      <x>" << v.x << "</x>\n";
    out << "      <y>" << v.y << "</y>\n";
    out << "      <z>" << v.z << "</z>\n";
    out << "     </coordinates>\n";
    out << "    </vertex>\n";
  }
  out << "   </vertices>\n";
  out << "   <volume>\n";
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    out << "    <triangle><v1>" << f.vi[0] << "</v1><v2>" << f.vi[1] << "</v2><v3>" << f.vi[2]
        << "</v3></triangle>\n";
    if (f.IsQuad()) {
      // AMF's <volume> is triangle-only - split the quad's second
      // triangle out, the same accommodation SaveStl() already makes.
      out << "    <triangle><v1>" << f.vi[0] << "</v1><v2>" << f.vi[2] << "</v2><v3>" << f.vi[3]
          << "</v3></triangle>\n";
    }
  }
  out << "   </volume>\n";
  out << "  </mesh>\n";
  out << " </object>\n";
  out << "</amf>\n";

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadAmf(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();

  std::string vertices_content;
  size_t after_vertices = 0;
  if (!ExtractAmfElement(text, "vertices", 0, vertices_content, after_vertices)) {
    return Result::Failed;
  }

  Mesh result;
  ON_Mesh& raw = result.mesh_;

  {
    size_t pos = 0;
    std::string vertex_content;
    size_t next = 0;
    while (ExtractAmfElement(vertices_content, "vertex", pos, vertex_content, next)) {
      std::string coords_content;
      size_t coords_next = 0;
      if (!ExtractAmfElement(vertex_content, "coordinates", 0, coords_content, coords_next)) {
        return Result::Failed;
      }
      std::string x_text, y_text, z_text;
      double x = 0, y = 0, z = 0;
      if (!ExtractAmfLeafText(coords_content, "x", x_text) || !ParseOffDouble(x_text, x)) return Result::Failed;
      if (!ExtractAmfLeafText(coords_content, "y", y_text) || !ParseOffDouble(y_text, y)) return Result::Failed;
      if (!ExtractAmfLeafText(coords_content, "z", z_text) || !ParseOffDouble(z_text, z)) return Result::Failed;
      raw.m_V.Append(ON_3fPoint(x, y, z));
      pos = next;
    }
  }

  std::string volume_content;
  size_t after_volume = 0;
  if (!ExtractAmfElement(text, "volume", 0, volume_content, after_volume)) {
    return Result::Failed;
  }

  {
    size_t pos = 0;
    std::string triangle_content;
    size_t next = 0;
    while (ExtractAmfElement(volume_content, "triangle", pos, triangle_content, next)) {
      std::string v1_text, v2_text, v3_text;
      int v1 = 0, v2 = 0, v3 = 0;
      if (!ExtractAmfLeafText(triangle_content, "v1", v1_text) || !ParseOffInt(v1_text, v1)) return Result::Failed;
      if (!ExtractAmfLeafText(triangle_content, "v2", v2_text) || !ParseOffInt(v2_text, v2)) return Result::Failed;
      if (!ExtractAmfLeafText(triangle_content, "v3", v3_text) || !ParseOffInt(v3_text, v3)) return Result::Failed;
      if (v1 < 0 || v1 >= raw.m_V.Count() || v2 < 0 || v2 >= raw.m_V.Count() || v3 < 0 || v3 >= raw.m_V.Count()) {
        return Result::Failed;
      }
      ON_MeshFace face;
      face.vi[0] = v1;
      face.vi[1] = v2;
      face.vi[2] = v3;
      face.vi[3] = v3;
      raw.m_F.Append(face);
      pos = next;
    }
  }

  out_mesh = std::move(result);
  return Result::Ok;
}

namespace {

// Tokenizes a VRML97 file body (everything after the `#VRML ...` header
// line, which is consumed separately - see LoadVrml()) into a flat token
// stream: `{`, `}`, `[`, `]`, and `,` each become their own single-
// character token, except `,` which is dropped entirely rather than
// emitted as its own token (VRML97 itself specifies a comma as
// insignificant whitespace between values, so the number/index parsing
// loops below never need to special-case it - it's exactly as if it
// weren't there, the same as a space or a newline). A `#` starts a
// comment that runs to end of line (VRML allows a comment anywhere, not
// just the header), and anything else is split on ASCII whitespace the
// same way NextOffToken() already does for `.off`.
std::vector<std::string> TokenizeVrmlBody(const std::string& text) {
  std::vector<std::string> tokens;
  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    char c = text[i];
    if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
      ++i;
      continue;
    }
    if (c == '#') {
      while (i < n && text[i] != '\n') ++i;
      continue;
    }
    if (c == '{' || c == '}' || c == '[' || c == ']') {
      tokens.emplace_back(1, c);
      ++i;
      continue;
    }
    size_t start = i;
    while (i < n) {
      char d = text[i];
      if (std::isspace(static_cast<unsigned char>(d)) || d == '#' || d == '{' || d == '}' ||
          d == '[' || d == ']' || d == ',') {
        break;
      }
      ++i;
    }
    tokens.push_back(text.substr(start, i - start));
  }
  return tokens;
}

// Finds the index of the next token equal to `word` at or after `from`.
// Returns tokens.size() if not found.
size_t FindVrmlToken(const std::vector<std::string>& tokens, const std::string& word, size_t from) {
  for (size_t i = from; i < tokens.size(); ++i) {
    if (tokens[i] == word) return i;
  }
  return tokens.size();
}

}  // namespace

Result Mesh::SaveVrml(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  out << "#VRML V2.0 utf8\n";
  out << "Shape {\n";
  out << " geometry IndexedFaceSet {\n";
  out << "  coord Coordinate {\n";
  out << "   point [\n";
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    out << "    " << v.x << ' ' << v.y << ' ' << v.z << ",\n";
  }
  out << "   ]\n";
  out << "  }\n";
  out << "  coordIndex [\n";
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    if (f.IsQuad()) {
      out << "   " << f.vi[0] << ' ' << f.vi[1] << ' ' << f.vi[2] << ' ' << f.vi[3] << " -1,\n";
    } else {
      out << "   " << f.vi[0] << ' ' << f.vi[1] << ' ' << f.vi[2] << " -1,\n";
    }
  }
  out << "  ]\n";
  if (HasVertexColors()) {
    out << "  color Color {\n";
    out << "   color [\n";
    out << std::fixed << std::setprecision(8);
    for (int i = 0; i < mesh_.m_V.Count(); ++i) {
      const Color c = VertexColorAt(i);
      out << "    " << (c.r / 255.0) << ' ' << (c.g / 255.0) << ' ' << (c.b / 255.0) << ",\n";
    }
    out << std::defaultfloat << std::setprecision(6);
    out << "   ]\n";
    out << "  }\n";
    out << "  colorPerVertex TRUE\n";
  }
  out << " }\n";
  out << "}\n";

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadVrml(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }

  std::string header_line;
  std::getline(in, header_line);
  // Tolerate a trailing '\r' from a CRLF file, same as the rest of this
  // check would otherwise silently fail on one.
  if (!header_line.empty() && header_line.back() == '\r') header_line.pop_back();
  if (header_line.rfind("#VRML", 0) != 0) {
    return Result::Failed;  // not a VRML file at all - never silently misread
  }

  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::vector<std::string> tokens = TokenizeVrmlBody(buffer.str());

  const size_t point_kw = FindVrmlToken(tokens, "point", 0);
  if (point_kw == tokens.size() || point_kw + 1 >= tokens.size() || tokens[point_kw + 1] != "[") {
    return Result::Failed;
  }

  Mesh result;
  ON_Mesh& raw = result.mesh_;

  {
    std::vector<double> numbers;
    size_t i = point_kw + 2;
    for (; i < tokens.size() && tokens[i] != "]"; ++i) {
      double value = 0;
      if (!ParseOffDouble(tokens[i], value)) return Result::Failed;
      numbers.push_back(value);
    }
    if (i >= tokens.size()) return Result::Failed;  // unterminated point [ ... ]
    if (numbers.size() % 3 != 0) return Result::Failed;
    for (size_t v = 0; v + 2 < numbers.size(); v += 3) {
      raw.m_V.Append(ON_3fPoint(numbers[v], numbers[v + 1], numbers[v + 2]));
    }
  }

  const size_t index_kw = FindVrmlToken(tokens, "coordIndex", 0);
  if (index_kw == tokens.size() || index_kw + 1 >= tokens.size() || tokens[index_kw + 1] != "[") {
    return Result::Failed;
  }

  {
    std::vector<int> current_face;
    size_t i = index_kw + 2;
    for (; i < tokens.size() && tokens[i] != "]"; ++i) {
      int value = 0;
      if (!ParseOffInt(tokens[i], value)) return Result::Failed;
      if (value == -1) {
        if (current_face.size() < 3) return Result::Failed;
        AppendPolygonFace(raw, current_face);
        current_face.clear();
        continue;
      }
      if (value < 0 || value >= raw.m_V.Count()) return Result::Failed;
      current_face.push_back(value);
    }
    if (i >= tokens.size()) return Result::Failed;  // unterminated coordIndex [ ... ]
    if (!current_face.empty()) return Result::Failed;  // trailing run never closed with -1
  }

  // A `Color` node (VRML97's own per-vertex color convention, distinct from
  // the lowercase `color` field name both introducing it and, again, naming
  // its own value array - `FindVrmlToken` matches the capitalized node-type
  // token first, then the field keyword nested inside it) is optional; a
  // file with none leaves the mesh with no vertex colors at all, same as
  // before this was understood.
  const size_t color_node = FindVrmlToken(tokens, "Color", 0);
  if (color_node != tokens.size()) {
    const size_t color_kw = FindVrmlToken(tokens, "color", color_node + 1);
    if (color_kw == tokens.size() || color_kw + 1 >= tokens.size() || tokens[color_kw + 1] != "[") {
      return Result::Failed;
    }
    std::vector<double> numbers;
    size_t i = color_kw + 2;
    for (; i < tokens.size() && tokens[i] != "]"; ++i) {
      double value = 0;
      if (!ParseOffDouble(tokens[i], value)) return Result::Failed;
      numbers.push_back(value);
    }
    if (i >= tokens.size()) return Result::Failed;  // unterminated color [ ... ]
    if (numbers.size() % 3 != 0) return Result::Failed;
    // Deliberately narrow: only the `colorPerVertex TRUE` shape (one RGB
    // triple per vertex, `SaveVrml()`'s own convention) is understood - a
    // per-face color list (VRML97's other, `colorPerVertex FALSE` option)
    // wouldn't line up with this kernel's per-vertex-only color model and
    // is rejected outright rather than silently misapplied.
    if (numbers.size() / 3 != static_cast<size_t>(raw.m_V.Count())) {
      return Result::Failed;
    }
    std::vector<Color> colors;
    colors.reserve(numbers.size() / 3);
    for (size_t v = 0; v + 2 < numbers.size(); v += 3) {
      auto to_byte = [](double x) {
        x = std::max(0.0, std::min(1.0, x));
        return static_cast<unsigned char>(std::lround(x * 255.0));
      };
      colors.push_back(Color{to_byte(numbers[v]), to_byte(numbers[v + 1]), to_byte(numbers[v + 2])});
    }
    result.SetVertexColors(colors);
  }

  out_mesh = std::move(result);
  return Result::Ok;
}

namespace {

// Splits a COLLADA leaf array's own whitespace-separated numeric content
// (a `<float_array>`, `<vcount>`, or `<p>` element's text) into values -
// COLLADA's own convention for these arrays, the same "space is the only
// separator, newlines/indentation between values don't matter" contract
// TokenizeVrmlBody() already assumes for VRML's `point`/`coordIndex`.
// Returns false if any token fails to parse as the requested type.
bool ParseColladaDoubles(const std::string& text, std::vector<double>& out_values) {
  std::istringstream iss(text);
  std::string token;
  while (iss >> token) {
    double value = 0;
    if (!ParseOffDouble(token, value)) return false;
    out_values.push_back(value);
  }
  return true;
}

bool ParseColladaInts(const std::string& text, std::vector<int>& out_values) {
  std::istringstream iss(text);
  std::string token;
  while (iss >> token) {
    int value = 0;
    if (!ParseOffInt(token, value)) return false;
    out_values.push_back(value);
  }
  return true;
}

}  // namespace

Result Mesh::SaveCollada(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
  out << "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n";
  out << " <library_geometries>\n";
  out << "  <geometry id=\"mesh0\">\n";
  out << "   <mesh>\n";
  out << "    <source id=\"mesh0-positions\">\n";
  out << "     <float_array id=\"mesh0-positions-array\" count=\"" << (mesh_.m_V.Count() * 3)
      << "\">";
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    if (i > 0) out << ' ';
    out << v.x << ' ' << v.y << ' ' << v.z;
  }
  out << "</float_array>\n";
  out << "     <technique_common>\n";
  out << "      <accessor source=\"#mesh0-positions-array\" count=\"" << mesh_.m_V.Count()
      << "\" stride=\"3\">\n";
  out << "       <param name=\"X\" type=\"float\"/>\n";
  out << "       <param name=\"Y\" type=\"float\"/>\n";
  out << "       <param name=\"Z\" type=\"float\"/>\n";
  out << "      </accessor>\n";
  out << "     </technique_common>\n";
  out << "    </source>\n";
  out << "    <vertices id=\"mesh0-vertices\">\n";
  out << "     <input semantic=\"POSITION\" source=\"#mesh0-positions\"/>\n";
  out << "    </vertices>\n";
  out << "    <polylist count=\"" << mesh_.m_F.Count() << "\">\n";
  out << "     <input semantic=\"VERTEX\" source=\"#mesh0-vertices\" offset=\"0\"/>\n";
  out << "     <vcount>";
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    if (i > 0) out << ' ';
    out << (mesh_.m_F[i].IsQuad() ? 4 : 3);
  }
  out << "</vcount>\n";
  out << "     <p>";
  bool first_index = true;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    const int n = f.IsQuad() ? 4 : 3;
    for (int c = 0; c < n; ++c) {
      if (!first_index) out << ' ';
      out << f.vi[c];
      first_index = false;
    }
  }
  out << "</p>\n";
  out << "    </polylist>\n";
  out << "   </mesh>\n";
  out << "  </geometry>\n";
  out << " </library_geometries>\n";
  out << "</COLLADA>\n";

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadCollada(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();

  std::string positions_content;
  size_t after_positions = 0;
  if (!ExtractAmfElement(text, "float_array", 0, positions_content, after_positions)) {
    return Result::Failed;
  }
  std::vector<double> numbers;
  if (!ParseColladaDoubles(positions_content, numbers)) return Result::Failed;
  if (numbers.size() % 3 != 0) return Result::Failed;

  Mesh result;
  ON_Mesh& raw = result.mesh_;
  for (size_t v = 0; v + 2 < numbers.size(); v += 3) {
    raw.m_V.Append(ON_3fPoint(numbers[v], numbers[v + 1], numbers[v + 2]));
  }

  std::string polylist_content;
  size_t after_polylist = 0;
  const bool is_polylist = ExtractAmfElement(text, "polylist", 0, polylist_content, after_polylist);
  std::string triangles_content;
  size_t after_triangles = 0;
  const bool is_triangles =
      !is_polylist && ExtractAmfElement(text, "triangles", 0, triangles_content, after_triangles);
  if (!is_polylist && !is_triangles) return Result::Failed;
  const std::string& faces_content = is_polylist ? polylist_content : triangles_content;

  std::string p_content;
  size_t p_next = 0;
  if (!ExtractAmfElement(faces_content, "p", 0, p_content, p_next)) return Result::Failed;
  std::vector<int> indices;
  if (!ParseColladaInts(p_content, indices)) return Result::Failed;

  std::vector<int> vcounts;
  if (is_polylist) {
    std::string vcount_content;
    size_t vcount_next = 0;
    if (!ExtractAmfElement(faces_content, "vcount", 0, vcount_content, vcount_next)) {
      return Result::Failed;
    }
    if (!ParseColladaInts(vcount_content, vcounts)) return Result::Failed;
  } else {
    // <triangles> has no <vcount> of its own - COLLADA's own convention is
    // that every face here is implicitly a 3-index group.
    if (indices.size() % 3 != 0) return Result::Failed;
    vcounts.assign(indices.size() / 3, 3);
  }

  size_t cursor = 0;
  for (int vcount : vcounts) {
    if (vcount < 3) return Result::Failed;
    if (cursor + static_cast<size_t>(vcount) > indices.size()) return Result::Failed;
    for (int c = 0; c < vcount; ++c) {
      const int idx = indices[cursor + c];
      if (idx < 0 || idx >= raw.m_V.Count()) return Result::Failed;
    }
    AppendPolygonFace(raw, std::vector<int>(indices.begin() + static_cast<long>(cursor),
                                             indices.begin() + static_cast<long>(cursor + static_cast<size_t>(vcount))));
    cursor += static_cast<size_t>(vcount);
  }
  if (cursor != indices.size()) return Result::Failed;  // <p> has indices past the last <vcount>

  out_mesh = std::move(result);
  return Result::Ok;
}

namespace {

// Finds the next opening tag named exactly `tag_name` at or after `from`
// and returns the full tag text from its own '<' through its own closing
// '>' (inclusive of a self-closing "/>" if present) - unlike
// ExtractAmfElement()'s nested-element CONTENT, X3D encodes its
// coordIndex/point fields as attributes on the tag itself, so the caller
// needs the tag's own text, not what's between its open and close tags.
// Same "don't false-match a longer tag name" guard FindAmfOpenTag()
// already applies (searching for "X3D" must not match some other tag that
// merely starts with those characters).
size_t FindX3dTag(const std::string& text, const std::string& tag_name, size_t from,
                   std::string& tag_text) {
  const std::string needle = "<" + tag_name;
  size_t pos = from;
  while (true) {
    pos = text.find(needle, pos);
    if (pos == std::string::npos) return std::string::npos;
    size_t after = pos + needle.size();
    if (after < text.size()) {
      char c = text[after];
      if (c == '>' || c == '/' || std::isspace(static_cast<unsigned char>(c))) {
        size_t gt = text.find('>', after);
        if (gt == std::string::npos) return std::string::npos;
        tag_text = text.substr(pos, gt - pos + 1);
        return pos;
      }
    }
    pos = after;
  }
}

// Extracts the quoted value of `attr_name="..."` from a tag's own text (as
// FindX3dTag() returns it). Returns false if the attribute isn't present
// or its opening quote is never closed.
bool ExtractX3dAttribute(const std::string& tag_text, const std::string& attr_name,
                          std::string& out_value) {
  const std::string needle = attr_name + "=\"";
  size_t pos = tag_text.find(needle);
  if (pos == std::string::npos) return false;
  const size_t start = pos + needle.size();
  const size_t end = tag_text.find('"', start);
  if (end == std::string::npos) return false;
  out_value = tag_text.substr(start, end - start);
  return true;
}

}  // namespace

Result Mesh::SaveX3d(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out << "<X3D version=\"3.3\" profile=\"Interchange\">\n";
  out << " <Scene>\n";
  out << "  <Shape>\n";
  out << "   <IndexedFaceSet coordIndex=\"";
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    if (i > 0) out << ' ';
    if (f.IsQuad()) {
      out << f.vi[0] << ' ' << f.vi[1] << ' ' << f.vi[2] << ' ' << f.vi[3] << " -1";
    } else {
      out << f.vi[0] << ' ' << f.vi[1] << ' ' << f.vi[2] << " -1";
    }
  }
  out << "\"";
  if (HasVertexColors()) out << " colorPerVertex=\"true\"";
  out << ">\n";
  out << "    <Coordinate point=\"";
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    if (i > 0) out << ' ';
    out << v.x << ' ' << v.y << ' ' << v.z;
  }
  out << "\"/>\n";
  if (HasVertexColors()) {
    out << "    <Color color=\"";
    out << std::fixed << std::setprecision(8);
    for (int i = 0; i < mesh_.m_V.Count(); ++i) {
      const Color c = VertexColorAt(i);
      if (i > 0) out << ' ';
      out << (c.r / 255.0) << ' ' << (c.g / 255.0) << ' ' << (c.b / 255.0);
    }
    out << std::defaultfloat << std::setprecision(6);
    out << "\"/>\n";
  }
  out << "   </IndexedFaceSet>\n";
  out << "  </Shape>\n";
  out << " </Scene>\n";
  out << "</X3D>\n";

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadX3d(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();

  std::string x3d_tag;
  if (FindX3dTag(text, "X3D", 0, x3d_tag) == std::string::npos) {
    return Result::Failed;  // not an X3D file at all - never silently misread
  }

  std::string coordinate_tag;
  if (FindX3dTag(text, "Coordinate", 0, coordinate_tag) == std::string::npos) {
    return Result::Failed;
  }
  std::string point_attr;
  if (!ExtractX3dAttribute(coordinate_tag, "point", point_attr)) return Result::Failed;

  Mesh result;
  ON_Mesh& raw = result.mesh_;

  {
    std::vector<double> numbers;
    if (!ParseColladaDoubles(point_attr, numbers)) return Result::Failed;
    if (numbers.size() % 3 != 0) return Result::Failed;
    for (size_t v = 0; v + 2 < numbers.size(); v += 3) {
      raw.m_V.Append(ON_3fPoint(numbers[v], numbers[v + 1], numbers[v + 2]));
    }
  }

  std::string indexed_face_set_tag;
  if (FindX3dTag(text, "IndexedFaceSet", 0, indexed_face_set_tag) == std::string::npos) {
    return Result::Failed;
  }
  std::string coord_index_attr;
  if (!ExtractX3dAttribute(indexed_face_set_tag, "coordIndex", coord_index_attr)) {
    return Result::Failed;
  }

  {
    std::vector<int> indices;
    if (!ParseColladaInts(coord_index_attr, indices)) return Result::Failed;

    std::vector<int> current_face;
    for (int value : indices) {
      if (value == -1) {
        if (current_face.size() < 3) return Result::Failed;
        AppendPolygonFace(raw, current_face);
        current_face.clear();
        continue;
      }
      if (value < 0 || value >= raw.m_V.Count()) return Result::Failed;
      current_face.push_back(value);
    }
    if (!current_face.empty()) return Result::Failed;  // trailing run never closed with -1
  }

  // A `<Color color="...">` element (X3D's own per-vertex color convention,
  // reusing `LoadVrml()`'s Color-node reasoning re-encoded as an attribute
  // the same way `coordIndex`/`point` already are) is optional; a file with
  // none leaves the mesh with no vertex colors, same as before this was
  // understood. `Color` can never false-match the earlier `Coordinate`
  // search - `FindX3dTag()`'s own "don't false-match a longer tag name"
  // guard already rejects that.
  std::string color_tag;
  if (FindX3dTag(text, "Color", 0, color_tag) != std::string::npos) {
    std::string color_attr;
    if (!ExtractX3dAttribute(color_tag, "color", color_attr)) return Result::Failed;
    std::vector<double> numbers;
    if (!ParseColladaDoubles(color_attr, numbers)) return Result::Failed;
    if (numbers.size() % 3 != 0) return Result::Failed;
    // Deliberately narrow, the same reasoning LoadVrml() already gives for
    // its own Color node: only one RGB triple per vertex is understood - a
    // per-face color list doesn't fit this kernel's per-vertex-only color
    // model and is rejected outright rather than silently misapplied.
    if (numbers.size() / 3 != static_cast<size_t>(raw.m_V.Count())) {
      return Result::Failed;
    }
    std::vector<Color> colors;
    colors.reserve(numbers.size() / 3);
    for (size_t v = 0; v + 2 < numbers.size(); v += 3) {
      auto to_byte = [](double x) {
        x = std::max(0.0, std::min(1.0, x));
        return static_cast<unsigned char>(std::lround(x * 255.0));
      };
      colors.push_back(Color{to_byte(numbers[v]), to_byte(numbers[v + 1]), to_byte(numbers[v + 2])});
    }
    result.SetVertexColors(colors);
  }

  out_mesh = std::move(result);
  return Result::Ok;
}

namespace {

// Finds `key = [...]` (USD's own attribute-assignment syntax) and returns
// the content strictly between the first `[` after `key`'s own `=` and its
// matching `]` - USD arrays here never nest brackets (a `point3f[]`'s own
// tuples use parens, not brackets), so a plain first-`[`-to-first-`]` scan
// is sufficient, the same "no need to be more general than what SaveUsda()
// itself writes" stance FindX3dTag()/FindAmfOpenTag() already take for
// their own formats. Returns false if `key` isn't found or its `[...]`
// is never closed.
bool FindUsdaArray(const std::string& text, const std::string& key, std::string& out_content) {
  const size_t key_pos = text.find(key);
  if (key_pos == std::string::npos) return false;
  const size_t open = text.find('[', key_pos);
  if (open == std::string::npos) return false;
  const size_t close = text.find(']', open);
  if (close == std::string::npos) return false;
  out_content = text.substr(open + 1, close - open - 1);
  return true;
}

// Parses a `point3f[]` array's own "(x, y, z), (x, y, z), ..." content into
// a flat x0,y0,z0,x1,y1,z1,... list. Returns false if a tuple isn't found,
// doesn't hold exactly 3 comma-separated numbers, or any number fails to
// parse.
bool ParseUsdaPointTuples(const std::string& text, std::vector<double>& out_values) {
  size_t pos = 0;
  while (true) {
    const size_t open = text.find('(', pos);
    if (open == std::string::npos) break;
    const size_t close = text.find(')', open);
    if (close == std::string::npos) return false;
    std::string inner = text.substr(open + 1, close - open - 1);
    for (char& c : inner) {
      if (c == ',') c = ' ';
    }
    std::istringstream iss(inner);
    std::string token;
    int count = 0;
    while (iss >> token) {
      double value = 0;
      if (!ParseOffDouble(token, value)) return false;
      out_values.push_back(value);
      ++count;
    }
    if (count != 3) return false;
    pos = close + 1;
  }
  return true;
}

// Parses a flat `int[]` array's own comma-or-whitespace-separated content
// (USD writes both `faceVertexCounts`/`faceVertexIndices` this way) into
// values - a comma is treated as pure whitespace, the same convention
// TokenizeVrmlBody() already applies for VRML97's own comma-separated
// arrays. Returns false if any token fails to parse as an int.
bool ParseUsdaInts(const std::string& text, std::vector<int>& out_values) {
  std::string cleaned = text;
  for (char& c : cleaned) {
    if (c == ',') c = ' ';
  }
  std::istringstream iss(cleaned);
  std::string token;
  while (iss >> token) {
    int value = 0;
    if (!ParseOffInt(token, value)) return false;
    out_values.push_back(value);
  }
  return true;
}

}  // namespace

Result Mesh::SaveUsda(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  out << "#usda 1.0\n";
  out << "\n";
  out << "def Mesh \"mesh\"\n";
  out << "{\n";
  out << "    point3f[] points = [";
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    if (i > 0) out << ", ";
    out << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  }
  out << "]\n";

  out << "    int[] faceVertexCounts = [";
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    if (i > 0) out << ", ";
    out << (mesh_.m_F[i].IsQuad() ? 4 : 3);
  }
  out << "]\n";

  out << "    int[] faceVertexIndices = [";
  bool first_index = true;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    const int corners = f.IsQuad() ? 4 : 3;
    for (int c = 0; c < corners; ++c) {
      if (!first_index) out << ", ";
      first_index = false;
      out << f.vi[c];
    }
  }
  out << "]\n";
  out << "}\n";

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadUsda(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();

  if (text.find("#usda") == std::string::npos) {
    return Result::Failed;  // not a USD ASCII file at all - never silently misread
  }

  Mesh result;
  ON_Mesh& raw = result.mesh_;

  std::string points_content;
  if (!FindUsdaArray(text, "points", points_content)) return Result::Failed;
  {
    std::vector<double> numbers;
    if (!ParseUsdaPointTuples(points_content, numbers)) return Result::Failed;
    if (numbers.size() % 3 != 0) return Result::Failed;
    for (size_t v = 0; v + 2 < numbers.size(); v += 3) {
      raw.m_V.Append(ON_3fPoint(numbers[v], numbers[v + 1], numbers[v + 2]));
    }
  }

  std::string counts_content;
  if (!FindUsdaArray(text, "faceVertexCounts", counts_content)) return Result::Failed;
  std::vector<int> counts;
  if (!ParseUsdaInts(counts_content, counts)) return Result::Failed;

  std::string indices_content;
  if (!FindUsdaArray(text, "faceVertexIndices", indices_content)) return Result::Failed;
  std::vector<int> indices;
  if (!ParseUsdaInts(indices_content, indices)) return Result::Failed;

  size_t cursor = 0;
  for (int count : counts) {
    if (count < 3) return Result::Failed;
    if (cursor + static_cast<size_t>(count) > indices.size()) return Result::Failed;
    for (int k = 0; k < count; ++k) {
      const int idx = indices[cursor + k];
      if (idx < 0 || idx >= raw.m_V.Count()) return Result::Failed;
    }
    AppendPolygonFace(raw, std::vector<int>(indices.begin() + static_cast<long>(cursor),
                                             indices.begin() + static_cast<long>(cursor + static_cast<size_t>(count))));
    cursor += count;
  }
  if (cursor != indices.size()) return Result::Failed;  // trailing indices no count claims

  out_mesh = std::move(result);
  return Result::Ok;
}

namespace {

// Encodes `bytes` as standard base64 (RFC 4648, '+'/'/' alphabet, '='
// padding) - glTF's own `data:` URI convention for an embedded buffer.
std::string Base64Encode(const std::string& bytes) {
  static const char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= bytes.size()) {
    const uint32_t n = (static_cast<uint8_t>(bytes[i]) << 16) |
                        (static_cast<uint8_t>(bytes[i + 1]) << 8) |
                        static_cast<uint8_t>(bytes[i + 2]);
    out.push_back(kAlphabet[(n >> 18) & 0x3F]);
    out.push_back(kAlphabet[(n >> 12) & 0x3F]);
    out.push_back(kAlphabet[(n >> 6) & 0x3F]);
    out.push_back(kAlphabet[n & 0x3F]);
    i += 3;
  }
  const size_t remaining = bytes.size() - i;
  if (remaining == 1) {
    const uint32_t n = static_cast<uint8_t>(bytes[i]) << 16;
    out.push_back(kAlphabet[(n >> 18) & 0x3F]);
    out.push_back(kAlphabet[(n >> 12) & 0x3F]);
    out.push_back('=');
    out.push_back('=');
  } else if (remaining == 2) {
    const uint32_t n = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8);
    out.push_back(kAlphabet[(n >> 18) & 0x3F]);
    out.push_back(kAlphabet[(n >> 12) & 0x3F]);
    out.push_back(kAlphabet[(n >> 6) & 0x3F]);
    out.push_back('=');
  }
  return out;
}

// Decodes standard base64 (with or without '=' padding) back to bytes.
// Returns false on an invalid length or a character outside the base64
// alphabet (whitespace included - a real `data:` URI payload SaveGltf()
// itself writes never contains any).
bool Base64Decode(const std::string& text, std::string& out_bytes) {
  auto decode_char = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };

  std::string cleaned = text;
  while (!cleaned.empty() && cleaned.back() == '=') cleaned.pop_back();
  if (cleaned.size() % 4 == 1) return false;  // no valid base64 length ends in 1 leftover char

  out_bytes.clear();
  size_t i = 0;
  while (i + 4 <= cleaned.size()) {
    int v[4];
    for (int k = 0; k < 4; ++k) {
      v[k] = decode_char(cleaned[i + k]);
      if (v[k] < 0) return false;
    }
    const uint32_t n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
    out_bytes.push_back(static_cast<char>((n >> 16) & 0xFF));
    out_bytes.push_back(static_cast<char>((n >> 8) & 0xFF));
    out_bytes.push_back(static_cast<char>(n & 0xFF));
    i += 4;
  }
  const size_t remaining = cleaned.size() - i;
  if (remaining == 2) {
    int v0 = decode_char(cleaned[i]);
    int v1 = decode_char(cleaned[i + 1]);
    if (v0 < 0 || v1 < 0) return false;
    const uint32_t n = (v0 << 18) | (v1 << 12);
    out_bytes.push_back(static_cast<char>((n >> 16) & 0xFF));
  } else if (remaining == 3) {
    int v0 = decode_char(cleaned[i]);
    int v1 = decode_char(cleaned[i + 1]);
    int v2 = decode_char(cleaned[i + 2]);
    if (v0 < 0 || v1 < 0 || v2 < 0) return false;
    const uint32_t n = (v0 << 18) | (v1 << 12) | (v2 << 6);
    out_bytes.push_back(static_cast<char>((n >> 16) & 0xFF));
    out_bytes.push_back(static_cast<char>((n >> 8) & 0xFF));
  } else if (remaining != 0) {
    return false;
  }
  return true;
}

// Finds the next JSON object at or after `from` (its own next '{') and
// returns the content strictly between that '{' and its matching '}',
// tracking brace/bracket depth but not string literals - safe here because
// the only string value this reader ever has to scan past (the base64
// `data:` URI) is guaranteed by the base64 alphabet to contain none of
// `{}[]`, the same "deliberately narrow, not a general parser" trade-off
// FindX3dTag()/FindAmfOpenTag() already make for their own formats.
// Returns std::string::npos (leaving `out_content` untouched) if no
// complete object is found; otherwise returns the position just after the
// object's own closing '}'.
size_t FindNextJsonObject(const std::string& text, size_t from, std::string& out_content) {
  const size_t open = text.find('{', from);
  if (open == std::string::npos) return std::string::npos;
  int depth = 0;
  for (size_t i = open; i < text.size(); ++i) {
    if (text[i] == '{') ++depth;
    else if (text[i] == '}') {
      --depth;
      if (depth == 0) {
        out_content = text.substr(open + 1, i - open - 1);
        return i + 1;
      }
    }
  }
  return std::string::npos;
}

// Finds `"key"` followed by `:` and, after any whitespace, an array - and
// returns the content strictly between that array's own '[' and its
// matching ']' (bracket-depth tracked, same string-literal caveat as
// FindNextJsonObject() above). Returns false if `key` isn't found as a
// quoted key or its array is never closed.
bool FindJsonArrayContent(const std::string& text, const std::string& key, std::string& out_content) {
  const std::string needle = "\"" + key + "\"";
  size_t pos = text.find(needle);
  if (pos == std::string::npos) return false;
  size_t colon = text.find(':', pos + needle.size());
  if (colon == std::string::npos) return false;
  size_t open = colon + 1;
  while (open < text.size() && std::isspace(static_cast<unsigned char>(text[open]))) ++open;
  if (open >= text.size() || text[open] != '[') return false;
  int depth = 0;
  for (size_t i = open; i < text.size(); ++i) {
    if (text[i] == '[') ++depth;
    else if (text[i] == ']') {
      --depth;
      if (depth == 0) {
        out_content = text.substr(open + 1, i - open - 1);
        return true;
      }
    }
  }
  return false;
}

// Extracts an integer field `"key": <number>` from a JSON object's own
// content (as FindNextJsonObject() returns it). Returns false if `key`
// isn't found as a quoted key immediately followed by `:` and a number.
bool ExtractJsonIntField(const std::string& object_text, const std::string& key, long long& out_value) {
  const std::string needle = "\"" + key + "\"";
  size_t pos = object_text.find(needle);
  if (pos == std::string::npos) return false;
  size_t colon = object_text.find(':', pos + needle.size());
  if (colon == std::string::npos) return false;
  size_t start = colon + 1;
  while (start < object_text.size() && std::isspace(static_cast<unsigned char>(object_text[start]))) ++start;
  size_t end = start;
  while (end < object_text.size() &&
         (std::isdigit(static_cast<unsigned char>(object_text[end])) || object_text[end] == '-')) {
    ++end;
  }
  if (end == start) return false;
  out_value = std::atoll(object_text.substr(start, end - start).c_str());
  return true;
}

// Extracts a string field `"key": "value"` from a JSON object's own
// content. Returns false if `key` isn't found as a quoted key immediately
// followed by `:` and a quoted string, or the string's opening quote is
// never closed. Does not process backslash escapes (SaveGltf() itself
// never writes one into the fields this reader looks at).
bool ExtractJsonStringField(const std::string& object_text, const std::string& key, std::string& out_value) {
  const std::string needle = "\"" + key + "\"";
  size_t pos = object_text.find(needle);
  if (pos == std::string::npos) return false;
  size_t colon = object_text.find(':', pos + needle.size());
  if (colon == std::string::npos) return false;
  size_t quote_open = object_text.find('"', colon + 1);
  if (quote_open == std::string::npos) return false;
  size_t quote_close = object_text.find('"', quote_open + 1);
  if (quote_close == std::string::npos) return false;
  out_value = object_text.substr(quote_open + 1, quote_close - quote_open - 1);
  return true;
}

const char kGltfDataUriPrefix[] = "data:application/octet-stream;base64,";

// The binary payload SaveGltf()/SaveGlb() both embed - positions first
// (float32 XYZ, always little-endian per the glTF spec - the same "no
// swap" WriteBinaryScalar() path SavePly()'s own little-endian mode
// already uses), then every triangle's indices (uint32) - a quad is split
// into two triangles here, since glTF's TRIANGLES mode has no native quad,
// the same accommodation SaveStl()/SaveAmf() already make - plus the
// bounding box and counts both formats' JSON needs. Shared by the two
// container formats so their binary payload and JSON stay identical except
// for how each one stores/points at that payload (a base64 `data:` URI vs.
// a GLB chunk).
struct GltfMeshBuffer {
  std::string bytes;
  size_t position_bytes = 0;
  int vertex_count = 0;
  int triangle_count = 0;
  double min_x = 0, min_y = 0, min_z = 0, max_x = 0, max_y = 0, max_z = 0;
};

GltfMeshBuffer BuildGltfMeshBuffer(const ON_Mesh& mesh) {
  GltfMeshBuffer result;
  std::ostringstream buffer_stream;
  for (int i = 0; i < mesh.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh.m_V[i];
    WriteBinaryScalar(buffer_stream, static_cast<float>(v.x), false);
    WriteBinaryScalar(buffer_stream, static_cast<float>(v.y), false);
    WriteBinaryScalar(buffer_stream, static_cast<float>(v.z), false);
  }
  result.position_bytes = buffer_stream.str().size();

  for (int i = 0; i < mesh.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh.m_F[i];
    WriteBinaryScalar(buffer_stream, static_cast<uint32_t>(f.vi[0]), false);
    WriteBinaryScalar(buffer_stream, static_cast<uint32_t>(f.vi[1]), false);
    WriteBinaryScalar(buffer_stream, static_cast<uint32_t>(f.vi[2]), false);
    ++result.triangle_count;
    if (f.IsQuad()) {
      WriteBinaryScalar(buffer_stream, static_cast<uint32_t>(f.vi[0]), false);
      WriteBinaryScalar(buffer_stream, static_cast<uint32_t>(f.vi[2]), false);
      WriteBinaryScalar(buffer_stream, static_cast<uint32_t>(f.vi[3]), false);
      ++result.triangle_count;
    }
  }
  result.bytes = buffer_stream.str();
  result.vertex_count = mesh.m_V.Count();

  if (mesh.m_V.Count() > 0) {
    result.min_x = result.max_x = mesh.m_V[0].x;
    result.min_y = result.max_y = mesh.m_V[0].y;
    result.min_z = result.max_z = mesh.m_V[0].z;
    for (int i = 1; i < mesh.m_V.Count(); ++i) {
      const ON_3fPoint& v = mesh.m_V[i];
      result.min_x = std::min(result.min_x, static_cast<double>(v.x));
      result.min_y = std::min(result.min_y, static_cast<double>(v.y));
      result.min_z = std::min(result.min_z, static_cast<double>(v.z));
      result.max_x = std::max(result.max_x, static_cast<double>(v.x));
      result.max_y = std::max(result.max_y, static_cast<double>(v.y));
      result.max_z = std::max(result.max_z, static_cast<double>(v.z));
    }
  }
  return result;
}

// The glTF JSON both container formats write, identical apart from the
// single `"buffers"` array item the caller supplies - SaveGltf()'s own
// `{ "uri": "data:...", "byteLength": N }` or SaveGlb()'s bare
// `{ "byteLength": N }` (a GLB buffer with no `uri` refers to the
// container's own BIN chunk, per the glTF spec).
std::string BuildGltfJson(const GltfMeshBuffer& buf, const std::string& buffers_array_item) {
  std::ostringstream out;
  out << "{\n";
  out << "  \"asset\": { \"version\": \"2.0\", \"generator\": \"dino8-kernel\" },\n";
  out << "  \"buffers\": [ " << buffers_array_item << " ],\n";
  out << "  \"bufferViews\": [\n";
  out << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": " << buf.position_bytes
      << ", \"target\": 34962 },\n";
  out << "    { \"buffer\": 0, \"byteOffset\": " << buf.position_bytes << ", \"byteLength\": "
      << (buf.bytes.size() - buf.position_bytes) << ", \"target\": 34963 }\n";
  out << "  ],\n";
  out << "  \"accessors\": [\n";
  out << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": " << buf.vertex_count
      << ", \"type\": \"VEC3\", \"min\": [" << buf.min_x << ", " << buf.min_y << ", " << buf.min_z << "], \"max\": ["
      << buf.max_x << ", " << buf.max_y << ", " << buf.max_z << "] },\n";
  out << "    { \"bufferView\": 1, \"componentType\": 5125, \"count\": " << (buf.triangle_count * 3)
      << ", \"type\": \"SCALAR\" }\n";
  out << "  ],\n";
  out << "  \"meshes\": [ { \"primitives\": [ { \"attributes\": { \"POSITION\": 0 }, \"indices\": 1, "
         "\"mode\": 4 } ] } ],\n";
  out << "  \"nodes\": [ { \"mesh\": 0 } ],\n";
  out << "  \"scenes\": [ { \"nodes\": [ 0 ] } ],\n";
  out << "  \"scene\": 0\n";
  out << "}\n";
  return out.str();
}

// The read side shared by LoadGltf()/LoadGlb(): given the glTF JSON text
// and the already-resolved binary buffer (base64-decoded for `.gltf`, the
// GLB's own BIN chunk for `.glb`), extracts the two bufferViews SaveGltf()/
// SaveGlb() both write (positions, then indices) and rebuilds the mesh -
// see LoadGltf()'s own doc comment in mesh.h for the exact scope/failure
// conditions, identical for both formats since they share this one code
// path.
Result BuildMeshFromGltfJsonAndBuffer(const std::string& json_text, const std::string& decoded_buffer,
                                       Mesh& out_mesh) {
  std::string buffer_views_content;
  if (!FindJsonArrayContent(json_text, "bufferViews", buffer_views_content)) return Result::Failed;
  std::string position_view, index_view;
  size_t next = FindNextJsonObject(buffer_views_content, 0, position_view);
  if (next == std::string::npos) return Result::Failed;
  next = FindNextJsonObject(buffer_views_content, next, index_view);
  if (next == std::string::npos) return Result::Failed;

  long long position_offset = 0, position_length = 0, index_offset = 0, index_length = 0;
  if (!ExtractJsonIntField(position_view, "byteLength", position_length)) return Result::Failed;
  ExtractJsonIntField(position_view, "byteOffset", position_offset);  // defaults to 0 if absent
  if (!ExtractJsonIntField(index_view, "byteLength", index_length)) return Result::Failed;
  if (!ExtractJsonIntField(index_view, "byteOffset", index_offset)) return Result::Failed;

  if (position_length < 0 || index_length < 0 || position_offset < 0 || index_offset < 0) {
    return Result::Failed;
  }
  if (position_length % 12 != 0) return Result::Failed;  // not a whole number of float32 VEC3s
  if (index_length % 4 != 0 || index_length % 12 != 0) return Result::Failed;  // not whole uint32 triangles
  if (static_cast<size_t>(position_offset + position_length) > decoded_buffer.size()) return Result::Failed;
  if (static_cast<size_t>(index_offset + index_length) > decoded_buffer.size()) return Result::Failed;

  Mesh result;
  ON_Mesh& raw = result.raw();

  const int vertex_count = static_cast<int>(position_length / 12);
  for (int i = 0; i < vertex_count; ++i) {
    const size_t base = static_cast<size_t>(position_offset) + static_cast<size_t>(i) * 12;
    float x, y, z;
    std::memcpy(&x, decoded_buffer.data() + base, 4);
    std::memcpy(&y, decoded_buffer.data() + base + 4, 4);
    std::memcpy(&z, decoded_buffer.data() + base + 8, 4);
    raw.m_V.Append(ON_3fPoint(x, y, z));
  }

  const int triangle_count = static_cast<int>(index_length / 12);
  for (int i = 0; i < triangle_count; ++i) {
    const size_t base = static_cast<size_t>(index_offset) + static_cast<size_t>(i) * 12;
    uint32_t a, b, c;
    std::memcpy(&a, decoded_buffer.data() + base, 4);
    std::memcpy(&b, decoded_buffer.data() + base + 4, 4);
    std::memcpy(&c, decoded_buffer.data() + base + 8, 4);
    if (static_cast<int>(a) < 0 || static_cast<int>(a) >= vertex_count ||
        static_cast<int>(b) < 0 || static_cast<int>(b) >= vertex_count ||
        static_cast<int>(c) < 0 || static_cast<int>(c) >= vertex_count) {
      return Result::Failed;
    }
    ON_MeshFace face;
    face.vi[0] = static_cast<int>(a);
    face.vi[1] = static_cast<int>(b);
    face.vi[2] = static_cast<int>(c);
    face.vi[3] = static_cast<int>(c);
    raw.m_F.Append(face);
  }

  out_mesh = std::move(result);
  return Result::Ok;
}

// GLB chunk-type magic numbers, big-endian-in-the-name-but-always-written-
// little-endian per the binary glTF spec - "glTF", "JSON" and "BIN\0" read
// as 4-byte little-endian words.
constexpr uint32_t kGlbMagic = 0x46546C67;
constexpr uint32_t kGlbChunkTypeJson = 0x4E4F534A;
constexpr uint32_t kGlbChunkTypeBin = 0x004E4942;

}  // namespace

Result Mesh::SaveGltf(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  const GltfMeshBuffer buf = BuildGltfMeshBuffer(mesh_);
  const std::string base64 = Base64Encode(buf.bytes);
  std::ostringstream buffers_item;
  buffers_item << "{ \"uri\": \"" << kGltfDataUriPrefix << base64 << "\", \"byteLength\": " << buf.bytes.size()
               << " }";
  out << BuildGltfJson(buf, buffers_item.str());

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadGltf(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }
  std::ostringstream stream_buffer;
  stream_buffer << in.rdbuf();
  const std::string text = stream_buffer.str();

  std::string buffers_content;
  if (!FindJsonArrayContent(text, "buffers", buffers_content)) return Result::Failed;
  std::string buffer_object;
  if (FindNextJsonObject(buffers_content, 0, buffer_object) == std::string::npos) return Result::Failed;
  std::string uri;
  if (!ExtractJsonStringField(buffer_object, "uri", uri)) return Result::Failed;
  if (uri.rfind(kGltfDataUriPrefix, 0) != 0) {
    return Result::Failed;  // an external .bin reference - never silently misread
  }
  std::string decoded_buffer;
  if (!Base64Decode(uri.substr(std::strlen(kGltfDataUriPrefix)), decoded_buffer)) return Result::Failed;

  return BuildMeshFromGltfJsonAndBuffer(text, decoded_buffer, out_mesh);
}

Result Mesh::SaveGlb(const std::string& path) const {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return Result::Failed;
  }

  const GltfMeshBuffer buf = BuildGltfMeshBuffer(mesh_);
  std::ostringstream buffers_item;
  buffers_item << "{ \"byteLength\": " << buf.bytes.size() << " }";
  std::string json_text = BuildGltfJson(buf, buffers_item.str());
  while (json_text.size() % 4 != 0) json_text.push_back(' ');  // JSON chunk padded to 4 bytes with spaces

  std::string bin_chunk = buf.bytes;
  while (bin_chunk.size() % 4 != 0) bin_chunk.push_back('\0');  // BIN chunk padded to 4 bytes with zeros

  const uint32_t json_chunk_length = static_cast<uint32_t>(json_text.size());
  const uint32_t bin_chunk_length = static_cast<uint32_t>(bin_chunk.size());
  const uint32_t total_length =
      12 + 8 + json_chunk_length + 8 + bin_chunk_length;  // 12-byte header + two 8-byte chunk headers

  WriteBinaryScalar(out, kGlbMagic, false);
  WriteBinaryScalar(out, static_cast<uint32_t>(2), false);  // version
  WriteBinaryScalar(out, total_length, false);

  WriteBinaryScalar(out, json_chunk_length, false);
  WriteBinaryScalar(out, kGlbChunkTypeJson, false);
  out.write(json_text.data(), static_cast<std::streamsize>(json_text.size()));

  WriteBinaryScalar(out, bin_chunk_length, false);
  WriteBinaryScalar(out, kGlbChunkTypeBin, false);
  out.write(bin_chunk.data(), static_cast<std::streamsize>(bin_chunk.size()));

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadGlb(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }
  std::ostringstream stream_buffer;
  stream_buffer << in.rdbuf();
  const std::string data = stream_buffer.str();

  if (data.size() < 12) return Result::Failed;
  uint32_t magic = 0, version = 0, total_length = 0;
  std::memcpy(&magic, data.data(), 4);
  std::memcpy(&version, data.data() + 4, 4);
  std::memcpy(&total_length, data.data() + 8, 4);
  if (magic != kGlbMagic) return Result::Failed;
  if (version != 2) return Result::Failed;
  if (total_length > data.size()) return Result::Failed;  // truncated file

  // Walk the chunk list looking for the one JSON chunk (always first per
  // spec, but found by type here rather than assumed) and the one BIN
  // chunk; any other chunk type (a spec-sanctioned extension this reader
  // doesn't understand) is skipped rather than rejected outright.
  std::string json_text;
  std::string bin_chunk;
  bool have_json = false, have_bin = false;
  size_t offset = 12;
  while (offset + 8 <= static_cast<size_t>(total_length)) {
    uint32_t chunk_length = 0, chunk_type = 0;
    std::memcpy(&chunk_length, data.data() + offset, 4);
    std::memcpy(&chunk_type, data.data() + offset + 4, 4);
    offset += 8;
    if (offset + chunk_length > data.size()) return Result::Failed;
    if (chunk_type == kGlbChunkTypeJson) {
      json_text = data.substr(offset, chunk_length);
      have_json = true;
    } else if (chunk_type == kGlbChunkTypeBin) {
      bin_chunk = data.substr(offset, chunk_length);
      have_bin = true;
    }
    offset += chunk_length;
  }
  if (!have_json || !have_bin) return Result::Failed;

  return BuildMeshFromGltfJsonAndBuffer(json_text, bin_chunk, out_mesh);
}

namespace {

// Finds a STEP/IFC entity call `ENTITY_NAME(...)` at or after `from` and
// returns the balanced-parenthesis text strictly between the call's own
// outer parentheses - just enough to pull one entity's own argument list
// out of a `#n=ENTITY_NAME(args);` line, not a general EXPRESS parser. The
// same "don't false-match a longer name" guard `FindAmfOpenTag()` already
// applies: the character right before the match must not be an identifier
// character. Matching is case-sensitive, uppercase only - the exact case
// `SaveIfc()` itself writes; IFC/STEP keywords are formally case-
// insensitive, but this reader only ever needs to read its own writer's
// output plus hand-authored uppercase fixtures, the same narrowing every
// other reader in this file already takes for its own format.
bool FindIfcEntityArgs(const std::string& text, const std::string& entity_name, size_t from,
                       std::string& out_args) {
  size_t pos = from;
  while (true) {
    pos = text.find(entity_name, pos);
    if (pos == std::string::npos) return false;
    const bool boundary_before =
        pos == 0 || !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
    const size_t after = pos + entity_name.size();
    if (boundary_before && after < text.size() && text[after] == '(') {
      int depth = 1;
      size_t i = after + 1;
      for (; i < text.size() && depth > 0; ++i) {
        if (text[i] == '(') {
          ++depth;
        } else if (text[i] == ')') {
          --depth;
        }
      }
      if (depth != 0) return false;  // never closed
      out_args = text.substr(after + 1, (i - 1) - (after + 1));
      return true;
    }
    pos = after;
  }
}

// Splits a STEP/IFC entity's own argument list at top-level commas only - a
// comma inside a nested `(...)` (e.g. within CoordIndex's own list-of-
// triples) is not a split point. Every other multi-field "record" this file
// reads is XML- or bracket-delimited, so this depth-tracked split has no
// precedent to reuse here.
std::vector<std::string> SplitIfcTopLevelArgs(const std::string& args) {
  std::vector<std::string> out;
  int depth = 0;
  size_t start = 0;
  for (size_t i = 0; i < args.size(); ++i) {
    const char c = args[i];
    if (c == '(') {
      ++depth;
    } else if (c == ')') {
      --depth;
    } else if (c == ',' && depth == 0) {
      out.push_back(args.substr(start, i - start));
      start = i + 1;
    }
  }
  out.push_back(args.substr(start));
  return out;
}

// Parses a `LIST [3:3] OF INTEGER` list-of-triples - IfcTriangulatedFaceSet's
// own `CoordIndex` shape, `((i0,i1,i2),(i0,i1,i2),...)` - into a flat int
// list, 3 entries per tuple. Returns false if a tuple isn't found, doesn't
// hold exactly 3 comma-separated integers, or any integer fails to parse -
// the same strictness `ParseUsdaPointTuples()` already applies to its own
// 3-number tuples, adapted from real to integer.
bool ParseIfcIntTriples(const std::string& text, std::vector<int>& out_values) {
  size_t pos = 0;
  while (true) {
    const size_t open = text.find('(', pos);
    if (open == std::string::npos) break;
    const size_t close = text.find(')', open);
    if (close == std::string::npos) return false;
    std::string inner = text.substr(open + 1, close - open - 1);
    for (char& c : inner) {
      if (c == ',') c = ' ';
    }
    std::istringstream iss(inner);
    std::string token;
    int count = 0;
    while (iss >> token) {
      int value = 0;
      if (!ParseOffInt(token, value)) return false;
      out_values.push_back(value);
      ++count;
    }
    if (count != 3) return false;
    pos = close + 1;
  }
  return true;
}

}  // namespace

Result Mesh::SaveIfc(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  out << "ISO-10303-21;\n";
  out << "HEADER;\n";
  out << "FILE_DESCRIPTION((''),'2;1');\n";
  out << "FILE_NAME('','',(''),(''),'dino8-kernel','dino8-kernel','');\n";
  out << "FILE_SCHEMA(('IFC4'));\n";
  out << "ENDSEC;\n";
  out << "\n";
  out << "DATA;\n";

  out << "#1=IFCCARTESIANPOINTLIST3D((";
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    if (i > 0) out << ",";
    out << "(" << v.x << "," << v.y << "," << v.z << ")";
  }
  out << "));\n";

  out << "#2=IFCTRIANGULATEDFACESET(#1,$,$,(";
  bool first_triangle = true;
  const auto write_triangle = [&](int a, int b, int c) {
    if (!first_triangle) out << ",";
    first_triangle = false;
    // 1-based, per STEP's own IfcPositiveInteger convention.
    out << "(" << (a + 1) << "," << (b + 1) << "," << (c + 1) << ")";
  };
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    write_triangle(f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) {
      write_triangle(f.vi[0], f.vi[2], f.vi[3]);
    }
  }
  out << "),$);\n";
  out << "ENDSEC;\n";
  out << "\n";
  out << "END-ISO-10303-21;\n";

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadIfc(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();

  if (text.find("ISO-10303-21;") == std::string::npos) {
    return Result::Failed;  // not a STEP/IFC physical file at all - never silently misread
  }

  Mesh result;
  ON_Mesh& raw = result.mesh_;

  std::string points_args;
  if (!FindIfcEntityArgs(text, "IFCCARTESIANPOINTLIST3D", 0, points_args)) return Result::Failed;
  // `points_args` is the CoordList attribute's own value, one extra paren
  // layer around the tuple list (see SaveIfc()'s own doc comment: the
  // entity call is `IFCCARTESIANPOINTLIST3D( CoordList )`, and CoordList
  // itself, being a LIST, is written as `(item, item, ...)`) - stripped
  // here before reusing ParseUsdaPointTuples()'s own "(x, y, z), ..." tuple
  // scan, which expects the bare tuple list with no further wrapping.
  const std::string points_trimmed = TrimAmfWhitespace(points_args);
  if (points_trimmed.size() < 2 || points_trimmed.front() != '(' || points_trimmed.back() != ')') {
    return Result::Failed;
  }
  const std::string points_inner = points_trimmed.substr(1, points_trimmed.size() - 2);
  std::vector<double> coords;
  if (!ParseUsdaPointTuples(points_inner, coords)) return Result::Failed;
  if (coords.size() % 3 != 0) return Result::Failed;
  for (size_t v = 0; v + 2 < coords.size(); v += 3) {
    raw.m_V.Append(ON_3fPoint(coords[v], coords[v + 1], coords[v + 2]));
  }

  std::string faceset_args;
  if (!FindIfcEntityArgs(text, "IFCTRIANGULATEDFACESET", 0, faceset_args)) return Result::Failed;
  const std::vector<std::string> top_level_args = SplitIfcTopLevelArgs(faceset_args);
  // Coordinates, Normals, Closed, CoordIndex, [PnIndex] - CoordIndex is the
  // 4th attribute; PnIndex (5th) is written but never read back.
  if (top_level_args.size() < 4) return Result::Failed;
  const std::string coord_index_trimmed = TrimAmfWhitespace(top_level_args[3]);
  if (coord_index_trimmed.size() < 2 || coord_index_trimmed.front() != '(' ||
      coord_index_trimmed.back() != ')') {
    return Result::Failed;
  }
  const std::string coord_index_inner = coord_index_trimmed.substr(1, coord_index_trimmed.size() - 2);
  std::vector<int> one_based_indices;
  if (!ParseIfcIntTriples(coord_index_inner, one_based_indices)) return Result::Failed;
  if (one_based_indices.size() % 3 != 0) return Result::Failed;
  for (size_t i = 0; i + 2 < one_based_indices.size(); i += 3) {
    ON_MeshFace face;
    for (int c = 0; c < 3; ++c) {
      const int one_based = one_based_indices[i + c];
      if (one_based < 1 || one_based > raw.m_V.Count()) return Result::Failed;
      face.vi[c] = one_based - 1;
    }
    face.vi[3] = face.vi[2];
    raw.m_F.Append(face);
  }

  out_mesh = std::move(result);
  return Result::Ok;
}

Result Mesh::SaveStepAp242(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  out << "ISO-10303-21;\n";
  out << "HEADER;\n";
  out << "FILE_DESCRIPTION((''),'2;1');\n";
  out << "FILE_NAME('','',(''),(''),'dino8-kernel','dino8-kernel','');\n";
  out << "FILE_SCHEMA(('AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF'));\n";
  out << "ENDSEC;\n";
  out << "\n";
  out << "DATA;\n";

  out << "#1=COORDINATES_LIST((";
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    if (i > 0) out << ",";
    out << "(" << v.x << "," << v.y << "," << v.z << ")";
  }
  out << "));\n";

  // Coordinates, Pnmax, Normals, Pnindex, TriangleStrips, Triangles - the
  // same attribute-order convention SaveIfc() already documents for its own
  // IfcTriangulatedFaceSet, adapted to AP242's own tessellated_face entity
  // chain (coordinates_list -> tessellated_face -> triangulated_face).
  out << "#2=TRIANGULATED_FACE(#1," << mesh_.m_V.Count() << ",$,$,$,(";
  bool first_triangle = true;
  const auto write_triangle = [&](int a, int b, int c) {
    if (!first_triangle) out << ",";
    first_triangle = false;
    // 1-based, the same STEP-wide convention IFCTRIANGULATEDFACESET's own
    // CoordIndex already uses.
    out << "(" << (a + 1) << "," << (b + 1) << "," << (c + 1) << ")";
  };
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    write_triangle(f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) {
      write_triangle(f.vi[0], f.vi[2], f.vi[3]);
    }
  }
  out << "));\n";
  out << "ENDSEC;\n";
  out << "\n";
  out << "END-ISO-10303-21;\n";

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadStepAp242(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();

  if (text.find("ISO-10303-21;") == std::string::npos) {
    return Result::Failed;  // not a STEP physical file at all - never silently misread
  }

  Mesh result;
  ON_Mesh& raw = result.mesh_;

  std::string points_args;
  if (!FindIfcEntityArgs(text, "COORDINATES_LIST", 0, points_args)) return Result::Failed;
  const std::string points_trimmed = TrimAmfWhitespace(points_args);
  if (points_trimmed.size() < 2 || points_trimmed.front() != '(' || points_trimmed.back() != ')') {
    return Result::Failed;
  }
  const std::string points_inner = points_trimmed.substr(1, points_trimmed.size() - 2);
  std::vector<double> coords;
  if (!ParseUsdaPointTuples(points_inner, coords)) return Result::Failed;
  if (coords.size() % 3 != 0) return Result::Failed;
  for (size_t v = 0; v + 2 < coords.size(); v += 3) {
    raw.m_V.Append(ON_3fPoint(coords[v], coords[v + 1], coords[v + 2]));
  }

  std::string face_args;
  if (!FindIfcEntityArgs(text, "TRIANGULATED_FACE", 0, face_args)) return Result::Failed;
  const std::vector<std::string> top_level_args = SplitIfcTopLevelArgs(face_args);
  // Coordinates, Pnmax, Normals, Pnindex, TriangleStrips, Triangles -
  // Triangles is the 6th attribute.
  if (top_level_args.size() < 6) return Result::Failed;
  const std::string triangles_trimmed = TrimAmfWhitespace(top_level_args[5]);
  if (triangles_trimmed.size() < 2 || triangles_trimmed.front() != '(' || triangles_trimmed.back() != ')') {
    return Result::Failed;
  }
  const std::string triangles_inner = triangles_trimmed.substr(1, triangles_trimmed.size() - 2);
  std::vector<int> one_based_indices;
  if (!ParseIfcIntTriples(triangles_inner, one_based_indices)) return Result::Failed;
  if (one_based_indices.size() % 3 != 0) return Result::Failed;
  for (size_t i = 0; i + 2 < one_based_indices.size(); i += 3) {
    ON_MeshFace face;
    for (int c = 0; c < 3; ++c) {
      const int one_based = one_based_indices[i + c];
      if (one_based < 1 || one_based > raw.m_V.Count()) return Result::Failed;
      face.vi[c] = one_based - 1;
    }
    face.vi[3] = face.vi[2];
    raw.m_F.Append(face);
  }

  out_mesh = std::move(result);
  return Result::Ok;
}

namespace {

// CRC-32 (IEEE 802.3 / zlib) over `data`, computed bit-by-bit rather than
// via a lookup table - every mesh file this kernel writes is small enough
// that the table's own setup cost isn't worth it, and a bit-by-bit
// implementation is easier to verify against the standard polynomial
// directly rather than trusting a hand-copied 256-entry table.
uint32_t Crc32(const std::string& data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const unsigned char byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

struct ZipFileEntry {
  std::string name;
  std::string data;
};

// Writes `entries` as a plain ZIP archive with every entry "stored" (no
// deflate - compression method 0, compressed size == uncompressed size): a
// local file header + raw data per entry, a central directory, then an End
// Of Central Directory (EOCD) record, the three sections every real ZIP
// reader (and the OPC/3MF spec itself, which only requires a valid ZIP
// container, not any particular compression method) already expects. Used
// by Save3mf() to build a real, spec-valid `.3mf` package - genuinely
// readable by any ZIP tool, just not the smallest possible file, since
// "stored" wastes the compression deflate would normally buy. Returns
// false if `path` can't be opened for writing.
bool WriteZipArchive(const std::string& path, const std::vector<ZipFileEntry>& entries) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;

  struct CentralRecord {
    std::string name;
    uint32_t crc = 0;
    uint32_t size = 0;
    uint32_t local_offset = 0;
  };
  std::vector<CentralRecord> central;
  central.reserve(entries.size());

  for (const ZipFileEntry& entry : entries) {
    CentralRecord rec;
    rec.name = entry.name;
    rec.crc = Crc32(entry.data);
    rec.size = static_cast<uint32_t>(entry.data.size());
    rec.local_offset = static_cast<uint32_t>(out.tellp());

    WriteBinaryScalar(out, static_cast<uint32_t>(0x04034b50), false);  // local file header signature
    WriteBinaryScalar(out, static_cast<uint16_t>(20), false);          // version needed to extract
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);           // general purpose flag
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);           // compression method: stored
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);           // last mod file time
    WriteBinaryScalar(out, static_cast<uint16_t>(0x21), false);        // last mod file date (1980-01-01)
    WriteBinaryScalar(out, rec.crc, false);
    WriteBinaryScalar(out, rec.size, false);  // compressed size
    WriteBinaryScalar(out, rec.size, false);  // uncompressed size
    WriteBinaryScalar(out, static_cast<uint16_t>(rec.name.size()), false);
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);  // extra field length
    out.write(rec.name.data(), static_cast<std::streamsize>(rec.name.size()));
    out.write(entry.data.data(), static_cast<std::streamsize>(entry.data.size()));

    central.push_back(rec);
  }

  const uint32_t central_directory_offset = static_cast<uint32_t>(out.tellp());
  for (const CentralRecord& rec : central) {
    WriteBinaryScalar(out, static_cast<uint32_t>(0x02014b50), false);  // central directory signature
    WriteBinaryScalar(out, static_cast<uint16_t>(20), false);          // version made by
    WriteBinaryScalar(out, static_cast<uint16_t>(20), false);          // version needed to extract
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);           // general purpose flag
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);           // compression method
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);           // last mod file time
    WriteBinaryScalar(out, static_cast<uint16_t>(0x21), false);        // last mod file date
    WriteBinaryScalar(out, rec.crc, false);
    WriteBinaryScalar(out, rec.size, false);
    WriteBinaryScalar(out, rec.size, false);
    WriteBinaryScalar(out, static_cast<uint16_t>(rec.name.size()), false);
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);  // extra field length
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);  // file comment length
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);  // disk number start
    WriteBinaryScalar(out, static_cast<uint16_t>(0), false);  // internal file attributes
    WriteBinaryScalar(out, static_cast<uint32_t>(0), false);  // external file attributes
    WriteBinaryScalar(out, rec.local_offset, false);
    out.write(rec.name.data(), static_cast<std::streamsize>(rec.name.size()));
  }
  const uint32_t central_directory_size =
      static_cast<uint32_t>(out.tellp()) - central_directory_offset;

  WriteBinaryScalar(out, static_cast<uint32_t>(0x06054b50), false);  // EOCD signature
  WriteBinaryScalar(out, static_cast<uint16_t>(0), false);           // disk number
  WriteBinaryScalar(out, static_cast<uint16_t>(0), false);           // disk with central directory
  WriteBinaryScalar(out, static_cast<uint16_t>(central.size()), false);  // entries on this disk
  WriteBinaryScalar(out, static_cast<uint16_t>(central.size()), false);  // total entries
  WriteBinaryScalar(out, central_directory_size, false);
  WriteBinaryScalar(out, central_directory_offset, false);
  WriteBinaryScalar(out, static_cast<uint16_t>(0), false);  // comment length

  return out.good();
}

// Reads a ZIP archive written by WriteZipArchive() (or any other ZIP whose
// entries are all "stored" - compression method 0) by locating the EOCD
// record (scanning backward for its signature, since a comment field - a
// real archive tool might add one, though this kernel's own writer never
// does - means it isn't necessarily the file's last 22 bytes), then
// reading every entry straight out of the central directory. Returns false
// if `path` can't be opened, is too small to hold an EOCD at all, has no
// EOCD signature anywhere, the central directory it points at is
// truncated/out of range, any entry's compression method isn't 0 (a
// deflate-compressed entry - out of this narrow reader's scope, since
// implementing INFLATE is a much larger, separate problem than the ZIP
// *container* format this function actually exists to read), or any
// entry's actual data doesn't match its own recorded CRC-32 (a corrupt or
// truncated file, caught rather than silently trusted).
bool ReadZipArchive(const std::string& path, std::vector<ZipFileEntry>& out_entries) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string data = buffer.str();

  if (data.size() < 22) return false;
  size_t eocd_pos = std::string::npos;
  for (size_t i = data.size() - 22;; --i) {
    uint32_t sig = 0;
    std::memcpy(&sig, data.data() + i, 4);
    if (sig == 0x06054b50u) {
      eocd_pos = i;
      break;
    }
    if (i == 0) break;
  }
  if (eocd_pos == std::string::npos) return false;

  uint16_t entry_count = 0;
  uint32_t central_dir_size = 0, central_dir_offset = 0;
  std::memcpy(&entry_count, data.data() + eocd_pos + 10, 2);
  std::memcpy(&central_dir_size, data.data() + eocd_pos + 12, 4);
  std::memcpy(&central_dir_offset, data.data() + eocd_pos + 16, 4);
  if (static_cast<size_t>(central_dir_offset) + central_dir_size > data.size()) return false;

  size_t pos = central_dir_offset;
  for (uint16_t i = 0; i < entry_count; ++i) {
    if (pos + 46 > data.size()) return false;
    uint32_t sig = 0;
    std::memcpy(&sig, data.data() + pos, 4);
    if (sig != 0x02014b50u) return false;
    uint16_t compression = 0, name_len = 0, extra_len = 0, comment_len = 0;
    uint32_t crc = 0, comp_size = 0, uncomp_size = 0, local_offset = 0;
    std::memcpy(&compression, data.data() + pos + 10, 2);
    std::memcpy(&crc, data.data() + pos + 16, 4);
    std::memcpy(&comp_size, data.data() + pos + 20, 4);
    std::memcpy(&uncomp_size, data.data() + pos + 24, 4);
    std::memcpy(&name_len, data.data() + pos + 28, 2);
    std::memcpy(&extra_len, data.data() + pos + 30, 2);
    std::memcpy(&comment_len, data.data() + pos + 32, 2);
    std::memcpy(&local_offset, data.data() + pos + 42, 4);
    if (compression != 0) return false;  // deflate not supported - see this function's own doc comment
    if (pos + 46 + name_len > data.size()) return false;
    const std::string name = data.substr(pos + 46, name_len);
    pos += 46 + name_len + extra_len + comment_len;

    if (static_cast<size_t>(local_offset) + 30 > data.size()) return false;
    uint32_t local_sig = 0;
    std::memcpy(&local_sig, data.data() + local_offset, 4);
    if (local_sig != 0x04034b50u) return false;
    uint16_t local_name_len = 0, local_extra_len = 0;
    std::memcpy(&local_name_len, data.data() + local_offset + 26, 2);
    std::memcpy(&local_extra_len, data.data() + local_offset + 28, 2);
    const size_t data_start =
        static_cast<size_t>(local_offset) + 30 + local_name_len + local_extra_len;
    if (data_start + comp_size > data.size()) return false;
    std::string file_data = data.substr(data_start, comp_size);
    if (file_data.size() != uncomp_size) return false;
    if (Crc32(file_data) != crc) return false;

    out_entries.push_back({name, std::move(file_data)});
  }
  return true;
}

// Finds every self-closing `<tag_name .../>` element in `xml` (3MF's own
// convention - a `<vertex>`/`<triangle>` element is always self-closing)
// and passes each one's own attribute text (from right after `tag_name` to
// the closing `/>`) to `callback`, in document order. The character right
// after `tag_name` must be whitespace, `/`, or `>` - the same "don't false-
// match a longer tag name" guard `FindAmfOpenTag()` already applies, so a
// search for `vertex` can't match `vertices`. Returns false if a match is
// found but isn't actually self-closing (a `<vertex>...</vertex>` form 3MF
// itself never produces) rather than silently skipping it.
bool ForEachSelfClosingXmlTag(const std::string& xml, const std::string& tag_name,
                               const std::function<void(const std::string&)>& callback) {
  size_t pos = 0;
  bool found_any = false;
  while (true) {
    pos = xml.find("<" + tag_name, pos);
    if (pos == std::string::npos) break;
    const size_t after = pos + 1 + tag_name.size();
    if (after < xml.size() && !std::isspace(static_cast<unsigned char>(xml[after])) &&
        xml[after] != '/' && xml[after] != '>') {
      pos = after;
      continue;
    }
    const size_t close = xml.find('>', after);
    if (close == std::string::npos) return false;
    if (close == 0 || xml[close - 1] != '/') return false;
    callback(xml.substr(after, close - 1 - after));
    found_any = true;
    pos = close + 1;
  }
  return found_any;
}

// Extracts `attr_name="value"` from a self-closing tag's own attribute
// text (as ForEachSelfClosingXmlTag() hands it to its callback). Returns
// false if the attribute isn't present or its closing quote is missing.
bool ExtractXmlAttribute(const std::string& tag_text, const std::string& attr_name,
                          std::string& out_value) {
  const std::string needle = attr_name + "=\"";
  const size_t pos = tag_text.find(needle);
  if (pos == std::string::npos) return false;
  const size_t value_start = pos + needle.size();
  const size_t value_end = tag_text.find('"', value_start);
  if (value_end == std::string::npos) return false;
  out_value = tag_text.substr(value_start, value_end - value_start);
  return true;
}

}  // namespace

Result Mesh::Save3mf(const std::string& path) const {
  std::ostringstream model;
  model << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  model << "<model unit=\"millimeter\" xml:lang=\"en-US\" "
           "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n";
  model << " <resources>\n";
  model << "  <object id=\"1\" type=\"model\">\n";
  model << "   <mesh>\n";
  model << "    <vertices>\n";
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    model << "     <vertex x=\"" << v.x << "\" y=\"" << v.y << "\" z=\"" << v.z << "\"/>\n";
  }
  model << "    </vertices>\n";
  model << "    <triangles>\n";
  const auto write_triangle = [&](int a, int b, int c) {
    model << "     <triangle v1=\"" << a << "\" v2=\"" << b << "\" v3=\"" << c << "\"/>\n";
  };
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    write_triangle(f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) {
      write_triangle(f.vi[0], f.vi[2], f.vi[3]);
    }
  }
  model << "    </triangles>\n";
  model << "   </mesh>\n";
  model << "  </object>\n";
  model << " </resources>\n";
  model << " <build>\n";
  model << "  <item objectid=\"1\"/>\n";
  model << " </build>\n";
  model << "</model>\n";

  const std::string content_types =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
      " <Default Extension=\"rels\" "
      "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
      " <Default Extension=\"model\" "
      "ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
      "</Types>\n";

  const std::string rels =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
      " <Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
      "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n"
      "</Relationships>\n";

  const std::vector<ZipFileEntry> entries = {
      {"[Content_Types].xml", content_types},
      {"_rels/.rels", rels},
      {"3D/3dmodel.model", model.str()},
  };
  return WriteZipArchive(path, entries) ? Result::Ok : Result::Failed;
}

Result Mesh::Load3mf(const std::string& path, Mesh& out_mesh) {
  std::vector<ZipFileEntry> entries;
  if (!ReadZipArchive(path, entries)) return Result::Failed;

  const ZipFileEntry* model_part = nullptr;
  for (const ZipFileEntry& entry : entries) {
    if (entry.name == "3D/3dmodel.model") {
      model_part = &entry;
      break;
    }
  }
  if (model_part == nullptr) return Result::Failed;

  Mesh result;
  ON_Mesh& raw = result.mesh_;

  bool vertex_parse_failed = false;
  const bool found_vertices =
      ForEachSelfClosingXmlTag(model_part->data, "vertex", [&](const std::string& tag) {
        std::string x_text, y_text, z_text;
        double x = 0, y = 0, z = 0;
        if (!ExtractXmlAttribute(tag, "x", x_text) || !ExtractXmlAttribute(tag, "y", y_text) ||
            !ExtractXmlAttribute(tag, "z", z_text) || !ParseOffDouble(x_text, x) ||
            !ParseOffDouble(y_text, y) || !ParseOffDouble(z_text, z)) {
          vertex_parse_failed = true;
          return;
        }
        raw.m_V.Append(ON_3fPoint(x, y, z));
      });
  if (!found_vertices || vertex_parse_failed) return Result::Failed;

  bool triangle_parse_failed = false;
  const bool found_triangles =
      ForEachSelfClosingXmlTag(model_part->data, "triangle", [&](const std::string& tag) {
        std::string v1_text, v2_text, v3_text;
        int v1 = 0, v2 = 0, v3 = 0;
        if (!ExtractXmlAttribute(tag, "v1", v1_text) || !ExtractXmlAttribute(tag, "v2", v2_text) ||
            !ExtractXmlAttribute(tag, "v3", v3_text) || !ParseOffInt(v1_text, v1) ||
            !ParseOffInt(v2_text, v2) || !ParseOffInt(v3_text, v3)) {
          triangle_parse_failed = true;
          return;
        }
        if (v1 < 0 || v1 >= raw.m_V.Count() || v2 < 0 || v2 >= raw.m_V.Count() || v3 < 0 ||
            v3 >= raw.m_V.Count()) {
          triangle_parse_failed = true;
          return;
        }
        ON_MeshFace face;
        face.vi[0] = v1;
        face.vi[1] = v2;
        face.vi[2] = v3;
        face.vi[3] = v3;
        raw.m_F.Append(face);
      });
  if (!found_triangles || triangle_parse_failed) return Result::Failed;

  out_mesh = std::move(result);
  return Result::Ok;
}

namespace {

// Finds the first `array_name: *N { ... a: v,v,v,... }` block in a plain-
// ASCII FBX file - the only shape SaveFbx() ever writes for `Vertices`/
// `PolygonVertexIndex` - and splits its `a:` value list on commas into
// `out_tokens` (still raw text, not yet parsed as numbers - the caller
// knows whether it wants a real or an int). The same boundary guard
// `FindIfcEntityArgs()` already applies: the character right before the
// match can't be an identifier character, so a search for `Vertices`
// cannot false-match a longer name ending the same way. Returns false if
// `array_name:` is never found, or no `{`/`a:`/`}` follows it.
bool FindFbxArrayValues(const std::string& text, const std::string& array_name,
                         std::vector<std::string>& out_tokens) {
  size_t pos = 0;
  while (true) {
    pos = text.find(array_name + ":", pos);
    if (pos == std::string::npos) return false;
    const bool boundary_before =
        pos == 0 || !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
    if (boundary_before) break;
    pos += array_name.size();
  }
  const size_t brace_open = text.find('{', pos);
  if (brace_open == std::string::npos) return false;
  const size_t a_pos = text.find("a:", brace_open);
  if (a_pos == std::string::npos) return false;
  const size_t brace_close = text.find('}', a_pos);
  if (brace_close == std::string::npos) return false;

  const std::string values_text = text.substr(a_pos + 2, brace_close - (a_pos + 2));
  std::istringstream iss(values_text);
  std::string token;
  while (std::getline(iss, token, ',')) {
    out_tokens.push_back(TrimAmfWhitespace(token));
  }
  return true;
}

}  // namespace

Result Mesh::SaveFbx(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  out << "; FBX 7.3.0 project file\n";
  out << "; ----------------------------------------------------\n\n";
  out << "FBXHeaderExtension:  {\n";
  out << "\tFBXHeaderVersion: 1003\n";
  out << "\tFBXVersion: 7300\n";
  out << "}\n\n";
  out << "GlobalSettings:  {\n";
  out << "\tVersion: 1000\n";
  out << "}\n\n";
  out << "Objects:  {\n";
  out << "\tGeometry: 1000000000, \"Geometry::\", \"Mesh\" {\n";
  out << "\t\tVertices: *" << (mesh_.m_V.Count() * 3) << " {\n";
  out << "\t\t\ta: ";
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    if (i > 0) out << ",";
    out << v.x << "," << v.y << "," << v.z;
  }
  out << "\n\t\t}\n";

  // Quads/n-gons kept native in PolygonVertexIndex - unlike every triangle-
  // only format above, FBX's own index list has a real variable-length
  // polygon shape, so nothing here needs to be split.
  int polygon_vertex_count = 0;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    polygon_vertex_count += mesh_.m_F[i].IsQuad() ? 4 : 3;
  }
  out << "\t\tPolygonVertexIndex: *" << polygon_vertex_count << " {\n";
  out << "\t\t\ta: ";
  bool first_value = true;
  const auto write_index = [&](int index, bool is_last) {
    if (!first_value) out << ",";
    first_value = false;
    // A polygon's last corner is written as the one's-complement of its
    // real index - always negative, since a real index is never negative -
    // the real FBX convention a reader uses to find each polygon's end
    // without a separate per-polygon count array.
    out << (is_last ? ~index : index);
  };
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    const int corner_count = f.IsQuad() ? 4 : 3;
    for (int c = 0; c < corner_count; ++c) {
      write_index(f.vi[c], c == corner_count - 1);
    }
  }
  out << "\n\t\t}\n";
  out << "\t}\n";
  out << "\tModel: 2000000000, \"Model::mesh\", \"Mesh\" {\n";
  out << "\t}\n";
  out << "}\n\n";
  out << "Connections:  {\n";
  out << "\tC: \"OO\",1000000000,2000000000\n";
  out << "\tC: \"OO\",2000000000,0\n";
  out << "}\n";

  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadFbx(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result::Failed;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();

  // Real FBX binary files start with this exact 23-byte magic string - the
  // same "require the format's own real marker" stance LoadVrml()'s
  // `#VRML` check already takes, here used to reject the OTHER real FBX
  // dialect outright rather than trying (and failing) to parse its bytes
  // as ASCII text.
  if (text.rfind("Kaydara FBX Binary", 0) == 0) return Result::Failed;

  std::vector<std::string> vertex_tokens;
  if (!FindFbxArrayValues(text, "Vertices", vertex_tokens)) return Result::Failed;
  if (vertex_tokens.empty() || vertex_tokens.size() % 3 != 0) return Result::Failed;

  Mesh result;
  ON_Mesh& raw = result.mesh_;
  for (size_t i = 0; i + 2 < vertex_tokens.size(); i += 3) {
    double x = 0, y = 0, z = 0;
    if (!ParseOffDouble(vertex_tokens[i], x) || !ParseOffDouble(vertex_tokens[i + 1], y) ||
        !ParseOffDouble(vertex_tokens[i + 2], z)) {
      return Result::Failed;
    }
    raw.m_V.Append(ON_3fPoint(x, y, z));
  }

  std::vector<std::string> index_tokens;
  if (!FindFbxArrayValues(text, "PolygonVertexIndex", index_tokens)) return Result::Failed;
  if (index_tokens.empty()) return Result::Failed;

  std::vector<int> current_polygon;
  for (const std::string& token : index_tokens) {
    int value = 0;
    if (!ParseOffInt(token, value)) return Result::Failed;
    const bool is_last = value < 0;
    const int real_index = is_last ? ~value : value;
    if (real_index < 0 || real_index >= raw.m_V.Count()) return Result::Failed;
    current_polygon.push_back(real_index);
    if (is_last) {
      if (current_polygon.size() < 3) return Result::Failed;
      if (current_polygon.size() <= 4) {
        ON_MeshFace face;
        face.vi[0] = current_polygon[0];
        face.vi[1] = current_polygon[1];
        face.vi[2] = current_polygon[2];
        face.vi[3] = current_polygon.size() == 4 ? current_polygon[3] : current_polygon[2];
        raw.m_F.Append(face);
      } else {
        // Fan-triangulate a genuine n-gon from its own first corner, the
        // same accommodation LoadObj()/LoadOff()/LoadVrml() already make.
        for (size_t k = 1; k + 1 < current_polygon.size(); ++k) {
          ON_MeshFace face;
          face.vi[0] = current_polygon[0];
          face.vi[1] = current_polygon[k];
          face.vi[2] = current_polygon[k + 1];
          face.vi[3] = current_polygon[k + 1];
          raw.m_F.Append(face);
        }
      }
      current_polygon.clear();
    }
  }
  if (!current_polygon.empty()) return Result::Failed;  // a run that never hit its terminator

  out_mesh = std::move(result);
  return Result::Ok;
}

namespace {

// DXF's whole file is a flat sequence of (group code, value) line pairs -
// the group code always a plain integer on its own line, the value on the
// line right after it. Real-world DXF writers commonly right-align the
// code in a fixed-width field (e.g. "  0" for a one-digit code), so both
// lines need trimming, not just the value - reusing TrimAmfWhitespace()
// above rather than writing a second near-identical trimmer.
bool ReadDxfPair(std::istream& in, int& code, std::string& value) {
  std::string code_line;
  if (!std::getline(in, code_line)) return false;
  if (!ParseOffInt(TrimAmfWhitespace(code_line), code)) return false;
  std::string value_line;
  if (!std::getline(in, value_line)) return false;
  value = TrimAmfWhitespace(value_line);
  return true;
}

}  // namespace

Result Mesh::SaveDxf(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }
  out << "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n";
  out << "0\nSECTION\n2\nENTITIES\n";
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    const ON_3fPoint& p0 = mesh_.m_V[f.vi[0]];
    const ON_3fPoint& p1 = mesh_.m_V[f.vi[1]];
    const ON_3fPoint& p2 = mesh_.m_V[f.vi[2]];
    // A triangle writes its third corner again as the fourth - DXF's own
    // documented convention ("If the 3DFACE is a triangle, make the third
    // and fourth points identical"), not a guess; a genuine quad
    // (IsQuad()) writes its own real fourth corner instead, since 3DFACE
    // is natively quad-capable and needs no splitting the way an all-
    // triangle format (STL, glTF, IFC, ...) would require.
    const ON_3fPoint& p3 = f.IsQuad() ? mesh_.m_V[f.vi[3]] : p2;
    out << "0\n3DFACE\n8\n0\n";
    out << "10\n" << p0.x << '\n' << "20\n" << p0.y << '\n' << "30\n" << p0.z << '\n';
    out << "11\n" << p1.x << '\n' << "21\n" << p1.y << '\n' << "31\n" << p1.z << '\n';
    out << "12\n" << p2.x << '\n' << "22\n" << p2.y << '\n' << "32\n" << p2.z << '\n';
    out << "13\n" << p3.x << '\n' << "23\n" << p3.y << '\n' << "33\n" << p3.z << '\n';
  }
  out << "0\nENDSEC\n0\nEOF\n";
  return out.good() ? Result::Ok : Result::Failed;
}

Result Mesh::LoadDxf(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path);
  if (!in) {
    return Result::Failed;
  }

  Mesh result;
  ON_Mesh& raw = result.mesh_;

  bool seen_entities_section = false;
  bool in_3dface = false;
  double coord[4][3] = {};
  bool have_coord[4][3] = {};

  // Finalizes the 3DFACE entity currently being accumulated (if any) into
  // a real ON_MeshFace, failing if any of its twelve required coordinate
  // group codes (10/20/30 .. 13/23/33) never showed up.
  auto FlushFace = [&]() -> bool {
    if (!in_3dface) return true;
    for (int c = 0; c < 4; ++c) {
      for (int a = 0; a < 3; ++a) {
        if (!have_coord[c][a]) return false;
      }
    }
    const ON_3fPoint p[4] = {
        ON_3fPoint(coord[0][0], coord[0][1], coord[0][2]),
        ON_3fPoint(coord[1][0], coord[1][1], coord[1][2]),
        ON_3fPoint(coord[2][0], coord[2][1], coord[2][2]),
        ON_3fPoint(coord[3][0], coord[3][1], coord[3][2]),
    };
    const int base = raw.m_V.Count();
    raw.m_V.Append(p[0]);
    raw.m_V.Append(p[1]);
    raw.m_V.Append(p[2]);
    // SaveDxf()'s own triangle convention (third and fourth corners
    // identical) - matched here with a small fixed tolerance rather than
    // exact float equality, since a real-world DXF writer's own rounding
    // may not reproduce the stored value bit-for-bit.
    const double dx = p[3].x - p[2].x, dy = p[3].y - p[2].y, dz = p[3].z - p[2].z;
    const bool is_triangle = (dx * dx + dy * dy + dz * dz) < 1e-12;
    ON_MeshFace face;
    face.vi[0] = base;
    face.vi[1] = base + 1;
    face.vi[2] = base + 2;
    if (is_triangle) {
      face.vi[3] = base + 2;
    } else {
      raw.m_V.Append(p[3]);
      face.vi[3] = base + 3;
    }
    raw.m_F.Append(face);
    in_3dface = false;
    for (auto& row : have_coord) row[0] = row[1] = row[2] = false;
    return true;
  };

  int code = 0;
  std::string value;
  while (ReadDxfPair(in, code, value)) {
    if (code == 0) {
      if (!FlushFace()) return Result::Failed;
      in_3dface = (value == "3DFACE");
      continue;
    }
    if (code == 2 && value == "ENTITIES") {
      seen_entities_section = true;
      continue;
    }
    if (!in_3dface) continue;
    // 1x/2x/3x are this entity's four corners' x/y/z group codes exactly
    // (10,20,30 = corner 0; 11,21,31 = corner 1; ...; 13,23,33 = corner 3).
    const int corner = code % 10;
    const int axis = code / 10 - 1;
    if (corner < 0 || corner > 3 || axis < 0 || axis > 2) continue;
    double parsed = 0.0;
    if (!ParseOffDouble(value, parsed)) return Result::Failed;
    coord[corner][axis] = parsed;
    have_coord[corner][axis] = true;
  }
  if (!FlushFace()) return Result::Failed;  // a 3DFACE truncated before its own closing 0-code
  if (!seen_entities_section) return Result::Failed;

  out_mesh = std::move(result);
  return Result::Ok;
}

Result Mesh::LoadStl(const std::string& path, Mesh& out_mesh) {
  // Distinguishes binary from ASCII the same way most real-world STL
  // readers do: an ASCII file's own text can start with "solid" and
  // still be ASCII (or, per the spec, a binary file's 80-byte header
  // can *also* start with the bytes "solid" - that keyword alone isn't a
  // reliable discriminator either way). What's actually reliable is the
  // binary format's exact, self-describing size: header (80) + count (4)
  // + count*50 bytes, nothing more and nothing less. If the file's real
  // size matches that formula for the triangle count its own header
  // claims, it's binary; otherwise, fall back to the ASCII parser.
  std::ifstream size_probe(path, std::ios::binary | std::ios::ate);
  if (!size_probe) {
    return Result::Failed;
  }
  const std::streamoff file_size = size_probe.tellg();
  if (file_size >= 84) {
    size_probe.seekg(80, std::ios::beg);
    uint32_t triangle_count = 0;
    if (size_probe.read(reinterpret_cast<char*>(&triangle_count), sizeof(triangle_count))) {
      const std::streamoff expected_size =
          84 + static_cast<std::streamoff>(triangle_count) * 50;
      if (expected_size == file_size) {
        return LoadBinaryStl(path, triangle_count, out_mesh);
      }
    }
  }
  return LoadAsciiStl(path, out_mesh);
}

Mesh Mesh::MergeAndWeld(const std::vector<Mesh>& meshes, double tolerance) {
  Mesh result;
  ON_Mesh& out = result.mesh_;

  // Snap each coordinate to a grid of `tolerance` size so two vertices
  // within `tolerance` of each other (in particular, the same seam point
  // computed independently by two adjacent faces) map to the same key.
  // std::llround, not std::lround: the quotient is coordinate / tolerance,
  // routinely 1e9..1e11 (a brep mesher welds at diagonal * 1e-8, some
  // callers at 1e-9), and std::lround returns `long`, which is only 32 bits
  // on Windows (LLP64). There every coordinate beyond ~2^31 * tolerance
  // overflowed to the same key, so distinct vertices a few units apart were
  // welded into one and closed solids came back as open, garbage meshes -
  // while the 64-bit `long` on Linux/macOS hid it entirely.
  auto snap = [tolerance](float v) {
    return std::llround(static_cast<double>(v) / tolerance);
  };

  std::map<std::tuple<long long, long long, long long>, int> vertex_by_position;

  for (const Mesh& mesh : meshes) {
    const ON_Mesh& in = mesh.mesh_;
    std::vector<int> remap(static_cast<size_t>(in.m_V.Count()));

    for (int i = 0; i < in.m_V.Count(); ++i) {
      const ON_3fPoint& v = in.m_V[i];
      const auto key = std::make_tuple(snap(v.x), snap(v.y), snap(v.z));
      const auto it = vertex_by_position.find(key);
      if (it != vertex_by_position.end()) {
        remap[static_cast<size_t>(i)] = it->second;
      } else {
        const int new_index = out.m_V.Count();
        out.m_V.Append(v);
        vertex_by_position.emplace(key, new_index);
        remap[static_cast<size_t>(i)] = new_index;
      }
    }

    for (int i = 0; i < in.m_F.Count(); ++i) {
      const ON_MeshFace& face = in.m_F[i];
      ON_MeshFace remapped;
      remapped.vi[0] = remap[static_cast<size_t>(face.vi[0])];
      remapped.vi[1] = remap[static_cast<size_t>(face.vi[1])];
      remapped.vi[2] = remap[static_cast<size_t>(face.vi[2])];
      remapped.vi[3] = remap[static_cast<size_t>(face.vi[3])];
      // A face that welding collapsed - two of its corners landed on one
      // vertex - is dropped (or, for a quad with one repeated corner,
      // kept as the triangle that remains). Before this, a pole row of a
      // sphere/cone/fan-cap tessellation (every sample at v=v0 is the
      // same physical point) survived as zero-area triangles (a, a, b)
      // whose edge {a, b} was then counted by THREE faces, so
      // Brep::Sphere().TessellateToClosedMesh() never reported
      // Mesh::IsClosedManifold() even though it was geometrically
      // watertight - see TestMergeAndWeldDropsCollapsedPoleTriangles and
      // TestMergeAndWeldMakesBrepSphereAClosedManifold. Volume()/Area()
      // are unchanged by this (a collapsed face contributes exactly zero
      // to both, by the same (a,b,c)+(a,c,d) quad split those methods
      // already use - the v[0]==v[2]/v[1]==v[3] check below is exactly
      // that split's own degeneracy condition, not a separate heuristic).
      if (face.IsQuad()) {
        int v[4] = {remapped.vi[0], remapped.vi[1], remapped.vi[2], remapped.vi[3]};
        int distinct[4];
        int nd = 0;
        for (int k = 0; k < 4; ++k) {
          if (v[k] != v[(k + 3) % 4]) distinct[nd++] = v[k];  // drop a corner equal to its predecessor (cyclically)
        }
        if (nd == 4) {
          if (v[0] == v[2] || v[1] == v[3]) continue;  // opposite corners coincide: no area
          out.m_F.Append(remapped);
        } else if (nd == 3) {
          ON_MeshFace tri;
          tri.vi[0] = distinct[0];
          tri.vi[1] = distinct[1];
          tri.vi[2] = distinct[2];
          tri.vi[3] = distinct[2];
          out.m_F.Append(tri);
        }
        continue;
      }
      if (remapped.vi[0] == remapped.vi[1] || remapped.vi[1] == remapped.vi[2] || remapped.vi[2] == remapped.vi[0]) continue;
      out.m_F.Append(remapped);
    }
  }

  return result;
}

std::vector<std::pair<int, int>> Mesh::ExtractValidatedBoundaryEdges(const ON_Mesh& cap,
                                                                      const char* caller) {
  // Boundary-edge extraction: a directed edge (a, b) that appears in some
  // triangle's winding is an interior edge if its reverse (b, a) also
  // appears (from the triangle on the other side); otherwise it's on the
  // cap's boundary loop. This works for any cap shape - including a
  // trimmed face's jagged/staircased boundary - without needing to know
  // the boundary's "ideal" curve.
  std::set<std::pair<int, int>> directed_edges;
  for (int i = 0; i < cap.m_F.Count(); ++i) {
    const ON_MeshFace& f = cap.m_F[i];
    directed_edges.insert({f.vi[0], f.vi[1]});
    directed_edges.insert({f.vi[1], f.vi[2]});
    directed_edges.insert({f.vi[2], f.vi[0]});
  }

  std::vector<std::pair<int, int>> boundary_edges;
  for (const auto& edge : directed_edges) {
    if (directed_edges.count({edge.second, edge.first}) == 0) {
      boundary_edges.push_back(edge);
    }
  }
  if (boundary_edges.empty()) {
    throw std::invalid_argument(std::string("dino8::kernel::Mesh::") + caller +
                                 ": cap has no boundary (it's already a closed mesh) - "
                                 "nothing to sweep into walls");
  }

  // A cap whose boundary is a set of simple, disjoint closed loops has
  // exactly one boundary edge leaving and one arriving at each boundary
  // vertex - that's what lets the wall geometry below join up into a
  // clean tube (or cone) per loop. A self-intersecting or "bowtie"
  // boundary (two loops touching at a shared vertex, or a figure-eight)
  // breaks that, and would otherwise silently produce overlapping or
  // malformed wall geometry instead of a clean solid - checked here and
  // rejected outright, rather than trusted to "probably be fine."
  std::map<int, int> outgoing_count;
  std::map<int, int> incoming_count;
  for (const auto& edge : boundary_edges) {
    ++outgoing_count[edge.first];
    ++incoming_count[edge.second];
  }
  for (const auto& [vertex, count] : outgoing_count) {
    if (count != 1 || incoming_count[vertex] != 1) {
      throw std::invalid_argument(
          std::string("dino8::kernel::Mesh::") + caller +
          ": cap's boundary is not a set of simple, disjoint closed loops (a "
          "vertex has more than one boundary edge) - self-intersecting or "
          "touching boundary loops aren't supported");
    }
  }

  return boundary_edges;
}

Mesh Mesh::ExtrudeCappedSolid(const Mesh& cap, Vector3d offset) {
  Mesh result;
  ON_Mesh& out = result.mesh_;
  const ON_Mesh& in = cap.mesh_;
  const int n = in.m_V.Count();

  const std::vector<std::pair<int, int>> boundary_edges =
      ExtractValidatedBoundaryEdges(in, "ExtrudeCappedSolid");

  // Near end (the cap as given) at indices [0, n); far end (translated by
  // offset) at indices [n, 2n).
  for (int i = 0; i < n; ++i) {
    out.m_V.Append(in.m_V[i]);
  }
  for (int i = 0; i < n; ++i) {
    const ON_3dPoint far_point = ON_3dPoint(in.m_V[i]) + offset;
    out.m_V.Append(ON_3fPoint(far_point));
  }

  // Near-end faces keep the cap's own winding/orientation.
  for (int i = 0; i < in.m_F.Count(); ++i) {
    const ON_MeshFace& f = in.m_F[i];
    ON_MeshFace near_face;
    near_face.vi[0] = f.vi[0];
    near_face.vi[1] = f.vi[1];
    near_face.vi[2] = f.vi[2];
    near_face.vi[3] = f.vi[2];
    out.m_F.Append(near_face);
  }
  // Far-end faces are the same triangles, translated and wound the
  // opposite way (so their normal points away from the solid, into +offset,
  // rather than back toward the near end).
  for (int i = 0; i < in.m_F.Count(); ++i) {
    const ON_MeshFace& f = in.m_F[i];
    ON_MeshFace far_face;
    far_face.vi[0] = f.vi[0] + n;
    far_face.vi[1] = f.vi[2] + n;
    far_face.vi[2] = f.vi[1] + n;
    far_face.vi[3] = far_face.vi[2];
    out.m_F.Append(far_face);
  }

  for (const auto& edge : boundary_edges) {
    const int a = edge.first;
    const int b = edge.second;
    ON_MeshFace wall1;
    wall1.vi[0] = a;
    wall1.vi[1] = b + n;
    wall1.vi[2] = b;
    wall1.vi[3] = wall1.vi[2];
    out.m_F.Append(wall1);

    ON_MeshFace wall2;
    wall2.vi[0] = a;
    wall2.vi[1] = a + n;
    wall2.vi[2] = b + n;
    wall2.vi[3] = wall2.vi[2];
    out.m_F.Append(wall2);
  }

  return result;
}

namespace {

// Shared by Cylinder() and Cone(): builds a flat circular disk cap
// centered at `center`, with its own u_dir x v_dir (and so its
// tessellated triangles' outward normal) pointing along -unit_axis - the
// orientation both ExtrudeCappedSolid() and ConeToApex() need from a cap
// that then gets closed off along +unit_axis.
Mesh BuildCircularDiskCap(Point3d center, Vector3d unit_axis, double radius,
                          int circle_segments, int grid_divisions) {
  if (circle_segments < 3) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh: circle_segments must be at least 3 (fewer "
        "can't form a non-degenerate circular trim loop)");
  }
  const Vector3d& n = unit_axis;

  // Arbitrary orthonormal in-plane basis (ex, ey) perpendicular to n -
  // standard "pick a non-parallel reference vector, cross twice" trick.
  const Vector3d reference =
      (std::abs(n.z) < 0.9) ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d ex = ON_CrossProduct(reference, n);
  ex.Unitize();
  const Vector3d ey = ON_CrossProduct(n, ex);

  // A square surface, big enough to contain the circle with margin, whose
  // own u_dir x v_dir gives an outward normal of -n (see the corner-order
  // derivation in the header comment / commit message: the cap has to
  // face away from where the sweep will build the solid). half_size in
  // parameter space maps back to the physical half-width s below.
  const double s = radius * 1.2;
  const Point3d a = center - ex * s - ey * s;
  const Point3d b = center + ex * s - ey * s;
  const Point3d c = center - ex * s + ey * s;
  const Point3d d = center + ex * s + ey * s;
  // Grid order [a, b, c, d] assigned to (u0,v0),(u0,v1),(u1,v0),(u1,v1):
  // u_dir = c - a = 2s*ey, v_dir = b - a = 2s*ex, so
  // u_dir x v_dir = 4s^2 (ey x ex) = -4s^2 n - the outward -n this cap
  // needs. P(u,v) = center + ex*s*(2v-1) + ey*s*(2u-1).
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid({a, b, c, d}, 2, 2, /*u_degree=*/1, /*v_degree=*/1);

  // Circle boundary, matching that same P(u,v) parameterization: a point
  // at angle theta is center + radius*cos(theta)*ex + radius*sin(theta)*ey,
  // i.e. u = 0.5 + radius*sin(theta)/(2s), v = 0.5 + radius*cos(theta)/(2s).
  std::vector<Point2d> trim_loop;
  trim_loop.reserve(static_cast<size_t>(circle_segments));
  for (int i = 0; i < circle_segments; ++i) {
    const double theta = 2.0 * ON_PI * static_cast<double>(i) / circle_segments;
    trim_loop.push_back(
        Point2d(0.5 + radius * std::sin(theta) / (2.0 * s),
                0.5 + radius * std::cos(theta) / (2.0 * s)));
  }

  // exact_clip=true: this gets real boundary clipping (NurbsSurface::
  // TessellateGridClippedExact) rather than TessellateGrid()'s whole-cell
  // approximation - the fix for the resolution/accuracy tradeoff the
  // whole-cell version measured.
  const Brep disk = Brep::TrimmedPlanarFace(surface, trim_loop, /*exact_clip=*/true);
  return disk.Tessellate(grid_divisions, grid_divisions).front();
}

// Projects `ring` onto the plane through `origin` with the given
// `normal`, using an arbitrary right-handed (ex, ey) in-plane basis
// (ex x ey = normal, the same convention this file already uses for a
// circular disk cap). Shared by both TriangulatePlanarRing() and the
// simplicity check below - whichever normal each computes, actually
// flattening the ring to 2D is the same operation either way.
std::vector<Point2d> ProjectOntoPlane(const std::vector<Point3d>& ring, const Point3d& origin,
                                       Vector3d normal) {
  normal.Unitize();
  const Vector3d reference =
      (std::abs(normal.z) < 0.9) ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d ex = ON_CrossProduct(reference, normal);
  ex.Unitize();
  const Vector3d ey = ON_CrossProduct(normal, ex);

  std::vector<Point2d> flat;
  flat.reserve(ring.size());
  for (const Point3d& p : ring) {
    const Vector3d offset = p - origin;
    flat.emplace_back(ON_DotProduct(offset, ex), ON_DotProduct(offset, ey));
  }
  return flat;
}

// Newell's method (summing cross-product-like terms over every edge)
// gives a normal consistent with `ring`'s own listed winding for any
// *simple* planar polygon, convex or concave - unlike a single 3-point
// cross product, which can pick the wrong sign or degenerate entirely if
// those 3 points happen to be nearly collinear. Only valid to call on a
// ring already known to be simple: a self-intersecting polygon's two
// "lobes" wind in opposite senses, so their Newell contributions can
// cancel to exactly zero (verified: a bowtie's does) - the wrong
// direction to detect that very self-intersection, which is exactly why
// IsPlanarRingSimple() below uses a different, cruder normal instead.
Vector3d NewellNormal(const std::vector<Point3d>& ring) {
  const size_t n = ring.size();
  Vector3d normal(0, 0, 0);
  for (size_t i = 0; i < n; ++i) {
    const Point3d& p = ring[i];
    const Point3d& q = ring[(i + 1) % n];
    normal.x += (p.y - q.y) * (p.z + q.z);
    normal.y += (p.z - q.z) * (p.x + q.x);
    normal.z += (p.x - q.x) * (p.y + q.y);
  }
  return normal;
}

// Triangulates a planar polygon (`ring`, convex or concave) given in 3D,
// for LoftClosedRings()'s end caps. Returns triangles as index triples
// into `ring` whose winding is CCW as seen looking back along the
// polygon's own Newell normal - i.e. the "natural", not reversed,
// orientation a front/last-ring cap needs (see LoftClosedRings()'s own
// comment for why the first/back ring's cap needs the reverse of this).
// Only meaningful on a ring already validated as simple - see
// NewellNormal()'s own comment.
std::vector<std::array<int, 3>> TriangulatePlanarRing(const std::vector<Point3d>& ring) {
  return dino8::kernel::detail::EarClipTriangulate(
      ProjectOntoPlane(ring, ring[0], NewellNormal(ring)));
}

// Whether `ring` is a simple (non-self-intersecting) planar polygon -
// LoftClosedRings()'s validation for its two end rings, which get
// ear-clipped into caps. Deliberately doesn't use NewellNormal() (see its
// own comment on why that degenerates for exactly the self-intersecting
// input this needs to detect); instead scans consecutive point triples
// for the first with a non-negligible cross product, i.e. any two
// non-parallel edges from the ring's own point set. Self-intersection is
// preserved under projection onto any plane containing the (assumed
// planar) points, regardless of which of the two possible normal
// directions is picked - unlike triangulation winding, this check doesn't
// care which way the normal points.
bool IsPlanarRingSimple(const std::vector<Point3d>& ring) {
  const size_t n = ring.size();
  Vector3d normal(0, 0, 0);
  for (size_t i = 0; i < n; ++i) {
    const Vector3d e1 = ring[(i + 1) % n] - ring[i];
    const Vector3d e2 = ring[(i + 2) % n] - ring[i];
    normal = ON_CrossProduct(e1, e2);
    if (normal.Length() > tolerance::kZeroVector) {
      break;
    }
  }
  if (normal.Length() <= tolerance::kZeroVector) {
    // Every triple tried was collinear/degenerate - not planar-polygon
    // shaped at all; leave that to fail elsewhere (or trivially "pass"
    // here) rather than misclassify a degenerate ring as self-intersecting.
    return true;
  }
  return dino8::kernel::detail::IsSimplePolygon(ProjectOntoPlane(ring, ring[0], normal));
}

// Whether every point of `ring` lies in a single plane, to a tolerance
// scaled by the ring's own size - LoftClosedRings()'s other end-ring
// validation (see IsPlanarRingSimple() above for the self-intersection
// one, which this deliberately doesn't replace: a ring can fail either
// check independently of the other). Reuses the same "scan consecutive
// triples for the first non-degenerate cross product" normal
// IsPlanarRingSimple() computes, rather than a least-squares fit - three
// points from the ring itself already pin down the only plane a genuinely
// planar ring could lie in.
bool IsRingPlanar(const std::vector<Point3d>& ring) {
  const size_t n = ring.size();
  Vector3d normal(0, 0, 0);
  size_t origin_index = 0;
  for (size_t i = 0; i < n; ++i) {
    const Vector3d e1 = ring[(i + 1) % n] - ring[i];
    const Vector3d e2 = ring[(i + 2) % n] - ring[i];
    normal = ON_CrossProduct(e1, e2);
    if (normal.Length() > tolerance::kZeroVector) {
      origin_index = i;
      break;
    }
  }
  if (normal.Length() <= tolerance::kZeroVector) {
    // Every triple tried was collinear/degenerate - not planar-polygon
    // shaped at all; leave that to fail elsewhere rather than misclassify
    // a degenerate ring as non-planar.
    return true;
  }
  normal.Unitize();
  const Point3d& origin = ring[origin_index];

  double scale = 0.0;
  for (const Point3d& p : ring) {
    scale = std::max(scale, (p - origin).Length());
  }
  if (scale <= tolerance::kZero) {
    return true;
  }

  // Relative, not absolute, tolerance: a ring's own coordinates set the
  // scale a "how far out of plane" check has to be judged against, the
  // same reasoning MergeAndWeld()'s own tolerance already uses. The
  // fraction itself is the kernel's policy value (tolerance.h), not a
  // literal of this function's own.
  const double plane_tolerance = scale * tolerance::kPlanarityRelative;
  for (const Point3d& p : ring) {
    if (std::abs(ON_DotProduct(p - origin, normal)) > plane_tolerance) {
      return false;
    }
  }
  return true;
}

}  // namespace

Mesh Mesh::Cylinder(Point3d base_center, Vector3d axis, double radius, double height,
                     int circle_segments, int grid_divisions) {
  Vector3d n = axis;
  n.Unitize();
  const Mesh cap = BuildCircularDiskCap(base_center, n, radius, circle_segments, grid_divisions);
  return ExtrudeCappedSolid(cap, n * height);
}

Mesh Mesh::ConeToApex(const Mesh& cap, Point3d apex) {
  Mesh result;
  ON_Mesh& out = result.mesh_;
  const ON_Mesh& in = cap.mesh_;
  const int n = in.m_V.Count();

  const std::vector<std::pair<int, int>> boundary_edges =
      ExtractValidatedBoundaryEdges(in, "ConeToApex");

  // Base (the cap as given) at indices [0, n); apex is the single new
  // vertex at index n.
  for (int i = 0; i < n; ++i) {
    out.m_V.Append(in.m_V[i]);
  }
  out.m_V.Append(ON_3fPoint(apex));

  // Base faces keep the cap's own winding/orientation.
  for (int i = 0; i < in.m_F.Count(); ++i) {
    const ON_MeshFace& f = in.m_F[i];
    ON_MeshFace base_face;
    base_face.vi[0] = f.vi[0];
    base_face.vi[1] = f.vi[1];
    base_face.vi[2] = f.vi[2];
    base_face.vi[3] = f.vi[2];
    out.m_F.Append(base_face);
  }

  // One triangle per boundary edge, to the shared apex vertex - the
  // ExtrudeCappedSolid()-style two-quad wall collapses to a single
  // triangle once the far end is a point instead of a translated copy.
  // Same winding convention as ExtrudeCappedSolid()'s wall triangles
  // (a, apex, b): outward-facing given the same boundary-edge direction.
  for (const auto& edge : boundary_edges) {
    ON_MeshFace wall;
    wall.vi[0] = edge.first;
    wall.vi[1] = n;
    wall.vi[2] = edge.second;
    wall.vi[3] = wall.vi[2];
    out.m_F.Append(wall);
  }

  return result;
}

Mesh Mesh::Cone(Point3d base_center, Vector3d axis, double radius, double height,
                int circle_segments, int grid_divisions) {
  Vector3d n = axis;
  n.Unitize();
  const Mesh cap = BuildCircularDiskCap(base_center, n, radius, circle_segments, grid_divisions);
  const Point3d apex = base_center + n * height;
  return ConeToApex(cap, apex);
}

Mesh Mesh::RevolveProfile(const std::vector<Point2d>& profile, Point3d axis_point, Vector3d axis,
                          int revolve_segments, double angle, double start_angle) {
  const int m = static_cast<int>(profile.size());
  if (m < 2) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::RevolveProfile: profile needs at least 2 points "
        "(nothing to revolve into a solid otherwise)");
  }
  if (revolve_segments < 3) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::RevolveProfile: revolve_segments must be at "
        "least 3 (fewer can't form a non-degenerate ring)");
  }
  if (!ON_IsValid(angle) || !(angle > 0.0) || angle > 2.0 * ON_PI + 1e-12) {
    throw std::invalid_argument("dino8::kernel::Mesh::RevolveProfile: angle must be in (0, 2*pi] radians");
  }
  if (!ON_IsValid(start_angle)) {
    throw std::invalid_argument("dino8::kernel::Mesh::RevolveProfile: start_angle must be finite");
  }
  if (angle < 2.0 * ON_PI - 1e-12) {
    // Partial angle: this function's own fast, exact-shared-vertex ring
    // construction below has no notion of a "start"/"end" pie-slice cap
    // (only ever the two on-axis-or-disc END caps a FULL revolve needs),
    // so building one from scratch here would duplicate machinery this
    // kernel already has and has already verified: Brep::Revolve()'s own
    // partial-angle support (fan fan caps in the start/end half-planes -
    // see its own doc comment for exactly which profile shapes it can
    // and can't cap at a partial angle). Delegate to it instead of
    // re-deriving cap topology a second, independent way.
    //
    // The profile is placed in an ARBITRARY plane containing `axis` (any
    // unit vector `ex` perpendicular to it will do - Brep::Revolve()
    // derives its own actual radial reference from the profile's own
    // farthest-from-axis point, not from whatever plane it happens to be
    // handed), as a degree-1 (piecewise-linear) 3D curve through the
    // profile's own (radius, height) points - the same L-shaped-polyline
    // convention Brep::Revolve()'s own doc comment uses for its cylinder
    // example.
    ON_3dVector n = axis;
    if (!n.Unitize()) {
      throw std::invalid_argument("dino8::kernel::Mesh::RevolveProfile: axis must be non-zero");
    }
    const ON_3dVector reference = (std::abs(n.z) < 0.9) ? ON_3dVector(0, 0, 1) : ON_3dVector(1, 0, 0);
    ON_3dVector ex = ON_CrossProduct(reference, n);
    ex.Unitize();
    std::vector<Point3d> profile_points;
    profile_points.reserve(static_cast<size_t>(m));
    for (const Point2d& p : profile) profile_points.push_back(axis_point + ex * p.x + n * p.y);
    const NurbsCurve profile_curve = NurbsCurve::FromControlPoints(profile_points, 1);
    const Brep revolved = Brep::Revolve(profile_curve, axis_point, axis, angle, /*cap=*/true, start_angle);
    // u (profile) divisions: one per input segment, so a straight run of
    // the (exactly piecewise-linear) wall between consecutive profile
    // points is resolved at least at its own two endpoints - coarser
    // than that would still lie exactly ON the true ruled surface (a
    // degree-1 NURBS curve/surface is exact everywhere along its own
    // parameter, not just at its knots) but could visibly round off an
    // interior profile vertex into a single averaged facet. v (angle)
    // divisions: `revolve_segments`, the same angular resolution the
    // full-circle path below uses.
    return revolved.TessellateToClosedMesh(std::max(1, m - 1), revolve_segments);
  }
  constexpr double kOnAxisEpsilon = tolerance::kZeroVector;
  const bool front_is_apex = std::abs(profile.front().x) <= kOnAxisEpsilon;
  const bool back_is_apex = std::abs(profile.back().x) <= kOnAxisEpsilon;

  Vector3d n = axis;
  n.Unitize();
  const Vector3d reference = (std::abs(n.z) < 0.9) ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d ex = ON_CrossProduct(reference, n);
  ex.Unitize();
  const Vector3d ey = ON_CrossProduct(n, ex);  // ex x ey = n (ex _|_ n): a right-handed frame,
                                                // so increasing theta below sweeps ex toward ey.

  Mesh result;
  ON_Mesh& out = result.mesh_;

  // ring_start[i]: index of profile point i's first (and, for an on-axis
  // end, only) vertex in the output mesh.
  std::vector<int> ring_start(static_cast<size_t>(m));
  for (int i = 0; i < m; ++i) {
    ring_start[static_cast<size_t>(i)] = out.m_V.Count();
    const double r = profile[static_cast<size_t>(i)].x;
    const double h = profile[static_cast<size_t>(i)].y;
    const bool is_apex = (i == 0 && front_is_apex) || (i == m - 1 && back_is_apex);
    if (is_apex) {
      out.m_V.Append(ON_3fPoint(axis_point + n * h));
    } else {
      for (int k = 0; k < revolve_segments; ++k) {
        const double theta = 2.0 * ON_PI * static_cast<double>(k) / revolve_segments + start_angle;
        out.m_V.Append(ON_3fPoint(axis_point + n * h + ex * (r * std::cos(theta)) +
                                   ey * (r * std::sin(theta))));
      }
    }
  }

  // Winding, derived (not guessed) from this codebase's one consistent
  // rule for outward normals: u_dir x v_dir must equal the outward
  // normal (see TessellateGrid's own comment). Parameterize a band
  // between ring i (u=tangential, increasing k/theta) and ring i+1
  // (v=height) the same way: tri1=(a,a2,b2), tri2=(a,b2,b) for corner
  // indices (a=ring i at k, a2=ring i at k+1, b=ring i+1 at k,
  // b2=ring i+1 at k+1) - u_dir (tangential, ~+ey at theta=0) x v_dir
  // (height, +n) = ey x n = ex, the radially-outward direction at
  // theta=0, confirming this is the outward-facing winding. At the two
  // on-axis ends, the apex's single vertex makes one triangle per pair
  // degenerate (zero area); skipping it leaves exactly ConeToApex()'s own
  // one-triangle-per-edge fan, corroborating this derivation rather than
  // introducing a second, independent convention for the end caps.
  for (int i = 0; i + 1 < m; ++i) {
    const bool i_is_apex = (i == 0 && front_is_apex);
    const bool i1_is_apex = (i + 1 == m - 1 && back_is_apex);
    for (int k = 0; k < revolve_segments; ++k) {
      const int k2 = (k + 1) % revolve_segments;
      const int a = i_is_apex ? ring_start[static_cast<size_t>(i)]
                               : ring_start[static_cast<size_t>(i)] + k;
      const int a2 = i_is_apex ? ring_start[static_cast<size_t>(i)]
                                : ring_start[static_cast<size_t>(i)] + k2;
      const int b = i1_is_apex ? ring_start[static_cast<size_t>(i + 1)]
                                : ring_start[static_cast<size_t>(i + 1)] + k;
      const int b2 = i1_is_apex ? ring_start[static_cast<size_t>(i + 1)]
                                 : ring_start[static_cast<size_t>(i + 1)] + k2;

      auto append_tri = [&out](int v0, int v1, int v2) {
        ON_MeshFace face;
        face.vi[0] = v0;
        face.vi[1] = v1;
        face.vi[2] = v2;
        face.vi[3] = v2;
        out.m_F.Append(face);
      };
      if (a != a2) {
        append_tri(a, a2, b2);
      }
      if (b != b2) {
        append_tri(a, b2, b);
      }
    }
  }

  // Flat disc cap for an off-axis end: a new center vertex plus a fan to
  // that end's ring. Fan triangle (center, k, k+1) has normal
  // (ring[k]-center) x (ring[k+1]-center) = r^2*sin(dtheta)*(ex x ey) =
  // +n (dtheta > 0, ex x ey = n) - the outward direction for the *back*
  // end. The front end's outward direction is -n, so its fan uses the
  // reverse order (center, k+1, k) instead.
  auto append_cap = [&out](int center, int ring_base, int segments, bool reverse) {
    for (int k = 0; k < segments; ++k) {
      const int k2 = (k + 1) % segments;
      ON_MeshFace face;
      face.vi[0] = center;
      face.vi[1] = reverse ? ring_base + k2 : ring_base + k;
      face.vi[2] = reverse ? ring_base + k : ring_base + k2;
      face.vi[3] = face.vi[2];
      out.m_F.Append(face);
    }
  };
  if (!front_is_apex) {
    const int center = out.m_V.Count();
    out.m_V.Append(ON_3fPoint(axis_point + n * profile.front().y));
    append_cap(center, ring_start.front(), revolve_segments, /*reverse=*/true);
  }
  if (!back_is_apex) {
    const int center = out.m_V.Count();
    out.m_V.Append(ON_3fPoint(axis_point + n * profile.back().y));
    append_cap(center, ring_start.back(), revolve_segments, /*reverse=*/false);
  }

  return result;
}

Mesh Mesh::LoftClosedRings(const std::vector<std::vector<Point3d>>& rings) {
  const int m = static_cast<int>(rings.size());
  if (m < 2) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::LoftClosedRings: needs at least 2 rings to loft between");
  }
  const int ring_size = static_cast<int>(rings.front().size());
  if (ring_size < 3) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::LoftClosedRings: each ring needs at least 3 vertices");
  }
  for (const auto& ring : rings) {
    if (static_cast<int>(ring.size()) != ring_size) {
      throw std::invalid_argument(
          "dino8::kernel::Mesh::LoftClosedRings: every ring must have the same "
          "vertex count");
    }
  }
  // Only the two end rings need to be simple (non-self-intersecting):
  // they're the ones ear-clipped into end caps below, and a
  // self-intersecting ring isn't decomposable into a well-defined
  // "inside" for that - the same requirement and check
  // TessellateGridClippedExact() applies to its trim_polygon. Interior
  // rings only feed the bands between them, which don't have that
  // requirement.
  if (!IsPlanarRingSimple(rings.front()) || !IsPlanarRingSimple(rings.back())) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::LoftClosedRings: the first and last rings must be "
        "simple (non-self-intersecting) polygons - they're each closed into an "
        "end cap, which isn't well-defined for a self-intersecting ring");
  }
  // Same reasoning, for planarity instead of simplicity: TriangulatePlanarRing()
  // projects a ring onto its own Newell-normal plane before ear-clipping it,
  // which is only a faithful triangulation of the ring's actual 3D shape if
  // that ring is genuinely planar to begin with.
  if (!IsRingPlanar(rings.front()) || !IsRingPlanar(rings.back())) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::LoftClosedRings: the first and last rings must be "
        "planar - a non-planar ring's cap triangulation (projected onto a "
        "single plane) isn't well-defined");
  }

  Mesh result;
  ON_Mesh& out = result.mesh_;

  std::vector<int> ring_start(static_cast<size_t>(m));
  for (int i = 0; i < m; ++i) {
    ring_start[static_cast<size_t>(i)] = out.m_V.Count();
    for (const Point3d& p : rings[static_cast<size_t>(i)]) {
      out.m_V.Append(ON_3fPoint(p));
    }
  }

  auto append_tri = [&out](int v0, int v1, int v2) {
    ON_MeshFace face;
    face.vi[0] = v0;
    face.vi[1] = v1;
    face.vi[2] = v2;
    face.vi[3] = v2;
    out.m_F.Append(face);
  };

  // Bands between consecutive rings: the same winding as RevolveProfile's
  // bands (tri1=(a,a2,b2), tri2=(a,b2,b)) - that derivation only used
  // "ring i is CCW-as-seen-from-ahead, ring i+1 is the next one along the
  // loft direction," which holds for any same-vertex-count ring pair,
  // not just RevolveProfile's circular ones.
  for (int i = 0; i + 1 < m; ++i) {
    const int base_a = ring_start[static_cast<size_t>(i)];
    const int base_b = ring_start[static_cast<size_t>(i + 1)];
    for (int k = 0; k < ring_size; ++k) {
      const int k2 = (k + 1) % ring_size;
      append_tri(base_a + k, base_a + k2, base_b + k2);
      append_tri(base_a + k, base_b + k2, base_b + k);
    }
  }

  // End caps: TriangulatePlanarRing() (ear-clipping via each ring's own
  // Newell normal) rather than a plain fan from vertex 0 - correct for a
  // concave ring too, not just a convex one a fan would silently mishandle
  // (self-intersecting or inverted triangles). Its triangles are wound
  // "natural" (CCW looking back along the ring's own Newell normal); the
  // first ring's cap needs that reversed - its normal must point
  // backward, away from the loft body, like Cylinder()'s base disk
  // needing -n while the sweep goes +n - while the last ring's cap keeps
  // the natural orientation, since forward is already outward there.
  for (const auto& tri : TriangulatePlanarRing(rings[0])) {
    append_tri(ring_start[0] + tri[0], ring_start[0] + tri[2], ring_start[0] + tri[1]);
  }
  const int last_base = ring_start[static_cast<size_t>(m - 1)];
  for (const auto& tri : TriangulatePlanarRing(rings[static_cast<size_t>(m - 1)])) {
    append_tri(last_base + tri[0], last_base + tri[1], last_base + tri[2]);
  }

  return result;
}

Mesh Mesh::LoftPeriodicRings(const std::vector<std::vector<Point3d>>& rings) {
  // Same band construction as LoftClosedRings(), but for a spine that loops
  // back on itself (e.g. a fillet tube swept all the way around a closed
  // edge, like a cylinder's own rim) rather than one with two distinct open
  // ends: every ring gets a band to the *next* ring, wrapping the last ring
  // back to the first, and there are no end caps at all - the tube is
  // already a closed torus-like tube with no ends to cap. Using
  // LoftClosedRings() (whose bands only run i -> i+1 for a plain open chain)
  // for a periodic spine instead leaves two independent flat end caps sitting
  // on top of each other where the spine closes up, which is not a manifold
  // seam and produced a real, previously-undiscovered non-watertight gap in
  // FilletEdge's mesh fallback for any fully closed edge (a solid cylinder's
  // own end-cap rim, a bore/hole rim, etc).
  const int m = static_cast<int>(rings.size());
  if (m < 3) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::LoftPeriodicRings: needs at least 3 rings to "
        "close a loop");
  }
  const int ring_size = static_cast<int>(rings.front().size());
  if (ring_size < 3) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::LoftPeriodicRings: each ring needs at least 3 "
        "vertices");
  }
  for (const auto& ring : rings) {
    if (static_cast<int>(ring.size()) != ring_size) {
      throw std::invalid_argument(
          "dino8::kernel::Mesh::LoftPeriodicRings: every ring must have the "
          "same vertex count");
    }
  }

  Mesh result;
  ON_Mesh& out = result.mesh_;

  std::vector<int> ring_start(static_cast<size_t>(m));
  for (int i = 0; i < m; ++i) {
    ring_start[static_cast<size_t>(i)] = out.m_V.Count();
    for (const Point3d& p : rings[static_cast<size_t>(i)]) {
      out.m_V.Append(ON_3fPoint(p));
    }
  }

  auto append_tri = [&out](int v0, int v1, int v2) {
    ON_MeshFace face;
    face.vi[0] = v0;
    face.vi[1] = v1;
    face.vi[2] = v2;
    face.vi[3] = v2;
    out.m_F.Append(face);
  };

  // Same band winding as LoftClosedRings() (tri1=(a,a2,b2), tri2=(a,b2,b)),
  // but i's partner wraps around with modulo m instead of stopping at m-1.
  for (int i = 0; i < m; ++i) {
    const int base_a = ring_start[static_cast<size_t>(i)];
    const int base_b = ring_start[static_cast<size_t>((i + 1) % m)];
    for (int k = 0; k < ring_size; ++k) {
      const int k2 = (k + 1) % ring_size;
      append_tri(base_a + k, base_a + k2, base_b + k2);
      append_tri(base_a + k, base_b + k2, base_b + k);
    }
  }

  return result;
}

Mesh Mesh::Torus(Point3d center, Vector3d axis, double major_radius, double minor_radius,
                 int major_segments, int minor_segments) {
  if (major_segments < 3 || minor_segments < 3) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Torus: major_segments and minor_segments must "
        "each be at least 3");
  }
  Vector3d n = axis;
  n.Unitize();

  // Arbitrary orthonormal in-plane basis (ex, ey) perpendicular to n -
  // the same "pick a non-parallel reference vector, cross twice" trick
  // used everywhere else in this file (Cylinder(), RevolveProfile()).
  const Vector3d reference = (std::abs(n.z) < 0.9) ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d ex = ON_CrossProduct(reference, n);
  ex.Unitize();
  const Vector3d ey = ON_CrossProduct(n, ex);

  Mesh result;
  ON_Mesh& out = result.mesh_;

  // Grid indexed [major][minor], row-major, matching this file's other
  // grid_index() helpers (see NurbsSurface::TessellateGrid).
  auto grid_index = [minor_segments](int i, int j) { return i * minor_segments + j; };

  out.m_V.Reserve(major_segments * minor_segments);
  for (int i = 0; i < major_segments; ++i) {
    const double phi = 2.0 * ON_PI * static_cast<double>(i) / major_segments;
    const Vector3d radial = ex * std::cos(phi) + ey * std::sin(phi);
    for (int j = 0; j < minor_segments; ++j) {
      const double theta = 2.0 * ON_PI * static_cast<double>(j) / minor_segments;
      const Point3d p = center + radial * (major_radius + minor_radius * std::cos(theta)) +
                         n * (minor_radius * std::sin(theta));
      out.m_V.Append(ON_3fPoint(p));
    }
  }

  out.m_F.Reserve(major_segments * minor_segments * 2);
  for (int i = 0; i < major_segments; ++i) {
    const int i2 = (i + 1) % major_segments;
    for (int j = 0; j < minor_segments; ++j) {
      const int j2 = (j + 1) % minor_segments;
      const int v00 = grid_index(i, j);
      const int v10 = grid_index(i2, j);
      const int v11 = grid_index(i2, j2);
      const int v01 = grid_index(i, j2);

      ON_MeshFace tri1;
      tri1.vi[0] = v00;
      tri1.vi[1] = v10;
      tri1.vi[2] = v11;
      tri1.vi[3] = v11;
      out.m_F.Append(tri1);

      ON_MeshFace tri2;
      tri2.vi[0] = v00;
      tri2.vi[1] = v11;
      tri2.vi[2] = v01;
      tri2.vi[3] = v01;
      out.m_F.Append(tri2);
    }
  }

  return result;
}

// ---------------------------------------------------------------------------
// Check / heal - see mesh.h's own doc comments on each method.

namespace {

// Reverses one face's winding in place - the exact per-face operation
// Mesh::FlipNormals() applies to every face (see its own comment on why
// a triangle's vi[3] must follow vi[2]).
void FlipOneFace(ON_MeshFace& f) {
  if (f.IsQuad()) {
    std::swap(f.vi[0], f.vi[3]);
    std::swap(f.vi[1], f.vi[2]);
  } else {
    std::swap(f.vi[0], f.vi[2]);
    f.vi[3] = f.vi[2];
  }
}

// Calls visit(a, b) for each directed edge of `f` in its winding order.
template <typename Visit>
void ForEachDirectedEdge(const ON_MeshFace& f, Visit visit) {
  visit(f.vi[0], f.vi[1]);
  visit(f.vi[1], f.vi[2]);
  if (f.IsQuad()) {
    visit(f.vi[2], f.vi[3]);
    visit(f.vi[3], f.vi[0]);
  } else {
    visit(f.vi[2], f.vi[0]);
  }
}

// Groups of vertex indices (from `candidates`) that are within
// `tolerance` of each other, by TRUE distance: each candidate is hashed
// into a grid of cell size `tolerance` and compared against every
// candidate in its own and the 26 neighbouring cells, so two points
// straddling a cell boundary are still found (the case plain grid
// snapping misses). Union-find over the pairs; returns each vertex's
// representative (the lowest index in its group), or -1 for a vertex
// not in `candidates`.
//
// INVESTIGATION LOG - a SPOTTED, NOT YET MEASURED, tolerance-compounding
// concern (found while looking for the next healing/tolerance gap to
// close; left as a documented concern rather than a guessed-at fix, per
// this codebase's own standard for an unproven correctness worry - see
// boolean_general.h's own investigation-log entries for the house style):
// union-find welding is SINGLE-LINKAGE clustering, which has no bound on
// a group's own diameter. Three candidates A, B, C with |AB| = |BC| =
// 0.9*tolerance but |AC| = 1.8*tolerance (comfortably ABOVE tolerance,
// so A and C are NOT themselves "the same point" by this function's own
// stated contract) still end up in ONE group here, because A-B and B-C
// each individually pass the pairwise test - and CloseNakedEdges()
// (this function's own caller, mesh.h) then snaps C onto A's position,
// 1.8*tolerance away, not <= tolerance away. Nothing bounds this to one
// extra hop either: a longer chain of candidates each tolerance-close to
// the next can walk arbitrarily far in aggregate before landing in a
// single group, all merged onto whichever member happens to have the
// lowest index - an amount of drift with NO relationship to `tolerance`
// itself, which is exactly the "does this silently drift instead of
// compounding correctly" failure shape this pass was asked to look for.
// NOT fixed here: every plausible fix (clustering by distance-to-the-
// group's-own-representative instead of distance-to-any-member, which
// bounds a group's diameter to 2*tolerance at the cost of welding fewer
// candidates per pass) changes what CloseNakedEdges() welds on EVERY
// existing caller, and this function backs the mesh-level side of the
// exact seam-closing machinery boolean_general.h's own "closedmesh gap"
// investigation spent an entire session tuning case-by-case (see its own
// "WHY THE SUGGESTED... WAS NOT ENOUGH" and "WHAT IS LEFT" entries) -
// changing its clustering rule here risks silently reopening some of
// that already-hard-won closed-mesh territory in a way a quick pass
// cannot responsibly verify. Whether a REAL chain of that shape ever
// actually arises on this kernel's own naked-edge fixtures (as opposed
// to being merely possible in principle) was not established either way
// - this needed the same kind of dedicated, instrumented investigation
// boolean_general.h's own log entries used, not a guess. Left as a
// documented concern for a future, dedicated pass rather than an
// uncertain fix landed under this one's own time budget.
std::vector<int> WeldGroups(const ON_Mesh& mesh, const std::vector<int>& candidates, double tolerance) {
  std::vector<int> parent(static_cast<size_t>(mesh.m_V.Count()), -1);
  for (const int v : candidates) parent[static_cast<size_t>(v)] = v;
  std::function<int(int)> find = [&](int v) {
    while (parent[static_cast<size_t>(v)] != v) {
      parent[static_cast<size_t>(v)] = parent[static_cast<size_t>(parent[static_cast<size_t>(v)])];
      v = parent[static_cast<size_t>(v)];
    }
    return v;
  };
  auto unite = [&](int a, int b) {
    a = find(a);
    b = find(b);
    if (a == b) return;
    if (a < b) parent[static_cast<size_t>(b)] = a; else parent[static_cast<size_t>(a)] = b;
  };
  const double cell = std::max(tolerance, tolerance::kZero);
  auto key_of = [&](const ON_3fPoint& p) {
    return std::make_tuple(static_cast<long long>(std::floor(p.x / cell)),
                           static_cast<long long>(std::floor(p.y / cell)),
                           static_cast<long long>(std::floor(p.z / cell)));
  };
  std::map<std::tuple<long long, long long, long long>, std::vector<int>> grid;
  for (const int v : candidates) grid[key_of(mesh.m_V[v])].push_back(v);
  for (const int v : candidates) {
    const ON_3fPoint& p = mesh.m_V[v];
    const auto [kx, ky, kz] = key_of(p);
    for (long long dx = -1; dx <= 1; ++dx) {
      for (long long dy = -1; dy <= 1; ++dy) {
        for (long long dz = -1; dz <= 1; ++dz) {
          const auto it = grid.find(std::make_tuple(kx + dx, ky + dy, kz + dz));
          if (it == grid.end()) continue;
          for (const int w : it->second) {
            if (w <= v) continue;
            const ON_3fPoint& q = mesh.m_V[w];
            const double d = ON_3dPoint(p).DistanceTo(ON_3dPoint(q));
            if (d <= tolerance) unite(v, w);
          }
        }
      }
    }
  }
  std::vector<int> rep(static_cast<size_t>(mesh.m_V.Count()), -1);
  for (const int v : candidates) rep[static_cast<size_t>(v)] = find(v);
  return rep;
}

// Groups the faces in `incident` (every face touching vertex `v`, already
// collected by the caller - Check() collects every vertex's own list in
// one pass over the whole face list; SplitNonManifoldVertex() needs only
// one vertex's, so it collects just that one) by shared-edge adjacency AT
// `v`: two incident faces land in the same group iff they share an edge
// that also touches `v`, the same "does this vertex's own fan form ONE
// connected piece" union-find-over-shared-incident-edges construction
// SubD::Check()'s own non_manifold_vertices/SplitNonManifoldVertex()
// already use via v->EdgeCount()/e->FaceCount(), reproduced here over a
// plain incident-face index list instead of ON_SubD's own edge objects.
// Returns each entry's own 0-based group id, parallel to `incident`, in
// first-seen (face-list) order - all zero when `incident` has fewer than
// 2 faces (trivially one group).
std::vector<int> GroupIncidentFacesByVertex(const ON_Mesh& mesh, int v, const std::vector<int>& incident) {
  std::vector<int> group(incident.size(), 0);
  if (incident.size() < 2) return group;

  std::vector<int> parent(incident.size());
  std::iota(parent.begin(), parent.end(), 0);
  std::function<int(int)> find = [&](int x) {
    while (parent[static_cast<size_t>(x)] != x) {
      parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
      x = parent[static_cast<size_t>(x)];
    }
    return x;
  };
  auto unite = [&](int a, int b) {
    a = find(a);
    b = find(b);
    if (a != b) parent[static_cast<size_t>(a)] = b;
  };
  // Two incident faces sharing an edge (v, other) are the same edge at v -
  // unite the first face seen at that "other" endpoint with every later
  // one sharing it.
  std::map<int, int> first_position_at_other_end;
  for (size_t k = 0; k < incident.size(); ++k) {
    ForEachDirectedEdge(mesh.m_F[incident[k]], [&](int a, int b) {
      int other = -1;
      if (a == v) other = b;
      else if (b == v) other = a;
      if (other < 0) return;
      const auto it = first_position_at_other_end.find(other);
      if (it == first_position_at_other_end.end()) {
        first_position_at_other_end[other] = static_cast<int>(k);
      } else {
        unite(it->second, static_cast<int>(k));
      }
    });
  }
  std::map<int, int> root_to_group;
  for (size_t k = 0; k < incident.size(); ++k) {
    const int root = find(static_cast<int>(k));
    const auto it = root_to_group.find(root);
    if (it == root_to_group.end()) {
      const int gid = static_cast<int>(root_to_group.size());
      root_to_group.emplace(root, gid);
      group[k] = gid;
    } else {
      group[k] = it->second;
    }
  }
  return group;
}

// Groups every face in `mesh` by whole-mesh connectivity: two faces land
// in the same group iff they share an edge (any undirected edge used by
// 2+ faces), transitively - the same "faces sharing an edge are the same
// piece" definition SubD::Check()'s own body_count/SplitDisjointPieces()
// use for SubD (itself modeled on Brep::SplitDisjointPieces()'s own
// ON_Brep::LabelConnectedComponents()). Shared by Mesh::Check() (which
// only needs `.second`, the group COUNT) and Mesh::SplitDisjointPieces()
// (which needs `.first`, the actual per-face membership). Returns a
// per-face 0-based group id, in first-seen (face-list) order, alongside
// the total group count - {}/{0} for an empty face list.
std::pair<std::vector<int>, size_t> GroupFacesByConnectivity(const ON_Mesh& mesh) {
  const int face_count = mesh.m_F.Count();
  std::vector<int> group(static_cast<size_t>(face_count), 0);
  if (face_count == 0) return {group, 0};

  std::vector<int> parent(static_cast<size_t>(face_count));
  std::iota(parent.begin(), parent.end(), 0);
  std::function<int(int)> find = [&](int x) {
    while (parent[static_cast<size_t>(x)] != x) {
      parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
      x = parent[static_cast<size_t>(x)];
    }
    return x;
  };
  auto unite = [&](int a, int b) {
    a = find(a);
    b = find(b);
    if (a != b) parent[static_cast<size_t>(a)] = b;
  };
  // Two faces sharing an edge (a, b) are the same piece - unite the first
  // face seen at that edge with every later one sharing it (handles a
  // non-manifold 3+-face edge the same way, all landing in one group).
  std::map<std::pair<int, int>, int> first_face_at_edge;
  for (int i = 0; i < face_count; ++i) {
    ForEachDirectedEdge(mesh.m_F[i], [&](int a, int b) {
      const std::pair<int, int> key = std::minmax(a, b);
      const auto it = first_face_at_edge.find(key);
      if (it == first_face_at_edge.end()) {
        first_face_at_edge.emplace(key, i);
      } else {
        unite(it->second, i);
      }
    });
  }
  std::map<int, int> root_to_group;
  for (int i = 0; i < face_count; ++i) {
    const int root = find(i);
    const auto it = root_to_group.find(root);
    if (it == root_to_group.end()) {
      const int gid = static_cast<int>(root_to_group.size());
      root_to_group.emplace(root, gid);
      group[static_cast<size_t>(i)] = gid;
    } else {
      group[static_cast<size_t>(i)] = it->second;
    }
  }
  return {group, root_to_group.size()};
}

// Same degeneracy test Mesh::Check() has always used, factored out so
// Mesh::RemoveDegenerateFaces() removes EXACTLY what Check() counts - a
// repeated vertex index, an edge shorter than `tolerance`, or a height
// (2*area / longest edge) at or below `tolerance`.
bool IsDegenerateFace(const ON_Mesh& mesh, const ON_MeshFace& f, double tolerance) {
  const int n = f.IsQuad() ? 4 : 3;
  for (int a = 0; a < n; ++a) {
    for (int b = a + 1; b < n; ++b) {
      if (f.vi[a] == f.vi[b]) return true;
    }
  }
  const ON_3dPoint p0(mesh.m_V[f.vi[0]]), p1(mesh.m_V[f.vi[1]]), p2(mesh.m_V[f.vi[2]]);
  double longest = std::max({p0.DistanceTo(p1), p1.DistanceTo(p2), p2.DistanceTo(p0)});
  double shortest = std::min({p0.DistanceTo(p1), p1.DistanceTo(p2), p2.DistanceTo(p0)});
  double area2 = ON_CrossProduct(p1 - p0, p2 - p0).Length();
  if (f.IsQuad()) {
    const ON_3dPoint p3(mesh.m_V[f.vi[3]]);
    longest = std::max({longest, p2.DistanceTo(p3), p3.DistanceTo(p0)});
    shortest = std::min({shortest, p2.DistanceTo(p3), p3.DistanceTo(p0)});
    area2 += ON_CrossProduct(p2 - p0, p3 - p0).Length();
  }
  const double height = longest > 0.0 ? area2 / longest : 0.0;
  return shortest <= tolerance || height <= tolerance;
}

// The one vertex of triangle `f` that is neither `p` nor `q` -
// TrisToQuads()'s own per-candidate lookup for "this triangle's apex
// away from the shared edge." Only meaningful when `f` is a genuine
// triangle (every call site below checks `!IsQuad()` first) and both
// `p` and `q` are among its 3 distinct vertex indices (both are, since
// every call site derives `p`/`q` from `f`'s own walked edges).
int ThirdTriangleVertex(const ON_MeshFace& f, int p, int q) {
  for (int k = 0; k < 3; ++k) {
    if (f.vi[k] != p && f.vi[k] != q) return f.vi[k];
  }
  return -1;
}

// A face's identity independent of vertex order or winding direction:
// the lexicographically smallest of all 2n rotations (n forward + n
// reversed, n = 3 or 4) of its vertex-index sequence. Two faces are the
// "same polygon" - Mesh::Check()'s duplicate_faces / RemoveDuplicateFaces()'s
// own definition - exactly when their keys are equal: the same vertices,
// same cyclic adjacency, either winding direction.
std::vector<int> CanonicalFaceKey(const ON_MeshFace& f) {
  const int n = f.IsQuad() ? 4 : 3;
  std::vector<int> v(f.vi, f.vi + n);
  std::vector<int> best = v;
  for (int dir = 0; dir < 2; ++dir) {
    for (int start = 0; start < n; ++start) {
      std::vector<int> cand(static_cast<size_t>(n));
      for (int k = 0; k < n; ++k) cand[static_cast<size_t>(k)] = v[static_cast<size_t>((start + k) % n)];
      if (cand < best) best = cand;
    }
    std::reverse(v.begin(), v.end());
  }
  return best;
}

// Drops every vertex no surviving face of `mesh` references and
// reindexes those faces to match - the exact compaction step
// CloseNakedEdges() and RemoveDegenerateFaces() both need after removing
// or remapping faces. Does not touch m_F itself, only m_V and the
// indices already in m_F.
void CompactUnusedVertices(ON_Mesh& mesh) {
  std::vector<int> new_index(static_cast<size_t>(mesh.m_V.Count()), -1);
  ON_3fPointArray vertices;
  for (int i = 0; i < mesh.m_F.Count(); ++i) {
    for (int k = 0; k < 4; ++k) {
      int& v = mesh.m_F[i].vi[k];
      if (new_index[static_cast<size_t>(v)] < 0) {
        new_index[static_cast<size_t>(v)] = vertices.Count();
        vertices.Append(mesh.m_V[v]);
      }
      v = new_index[static_cast<size_t>(v)];
    }
  }
  mesh.m_V = vertices;
}

// --- self-intersection -------------------------------------------------

// Whether triangle `p` genuinely crosses the plane with unit normal `n`
// through `q0`, filling `out` with the two points where its boundary
// crosses it. A vertex within `eps` of the plane counts as on the positive
// side (the same convention surface_intersect.cpp's own CrossPlane uses for
// its cross-surface intersection curves); false if all three vertices land
// on the same side (no crossing at all).
bool CrossesPlane(const Point3d p[3], const Vector3d& n, const Point3d& q0, double eps, Point3d out[2]) {
  double s[3];
  int pos = 0, neg = 0;
  for (int i = 0; i < 3; ++i) {
    s[i] = ON_DotProduct(p[i] - q0, n);
    if (s[i] > -eps && s[i] < eps) s[i] = eps;
    if (s[i] > 0) ++pos; else ++neg;
  }
  if (pos == 0 || neg == 0) return false;
  int k = 0;
  for (int i = 0; i < 3 && k < 2; ++i) {
    const int j = (i + 1) % 3;
    if ((s[i] > 0) == (s[j] > 0)) continue;
    const double t = s[i] / (s[i] - s[j]);
    out[k++] = p[i] + (p[j] - p[i]) * t;
  }
  return k == 2;
}

// Whether two triangles that share NO vertex genuinely overlap in 3D by more
// than `tolerance` - Mesh::FindSelfIntersections()'s own per-pair test,
// the same construction as surface_intersect.cpp's own cross-surface TriTri
// (used there for two independent meshes' intersection curve, with UV
// tracking this test doesn't need), specialized to a single mesh's
// self-overlap question: each triangle is split by the other's plane, and
// the two resulting intervals along the two planes' own cross-product line
// must overlap by MORE than `tolerance`, so a hairline touch (two triangles
// meeting exactly at a tolerance-close edge or point) is not reported, only
// a genuine crossing is. Coplanar or parallel triangles (near-zero cross
// product of their normals) return false - the same "handled by neighbours"
// limitation TriTri's own comment documents; see FindSelfIntersections'
// own doc comment for why that is an honest, not silent, gap.
bool TrianglesProperlyOverlap(const Point3d a[3], const Point3d b[3], double tolerance) {
  Vector3d na = ON_CrossProduct(a[1] - a[0], a[2] - a[0]);
  Vector3d nb = ON_CrossProduct(b[1] - b[0], b[2] - b[0]);
  // A degenerate triangle is Check()'s degenerate_faces's own job, not this
  // test's - treat it as never overlapping rather than dividing by zero.
  if (!na.Unitize() || !nb.Unitize()) return false;
  Vector3d dir = ON_CrossProduct(na, nb);
  const double dir_len = dir.Length();
  if (dir_len < tolerance::kZeroVector) return false;  // coplanar or parallel
  dir /= dir_len;

  Point3d ca[2], cb[2];
  if (!CrossesPlane(a, nb, b[0], tolerance, ca)) return false;
  if (!CrossesPlane(b, na, a[0], tolerance, cb)) return false;

  double ta0 = ON_DotProduct(ca[0] - Point3d::Origin, dir);
  double ta1 = ON_DotProduct(ca[1] - Point3d::Origin, dir);
  double tb0 = ON_DotProduct(cb[0] - Point3d::Origin, dir);
  double tb1 = ON_DotProduct(cb[1] - Point3d::Origin, dir);
  if (ta0 > ta1) std::swap(ta0, ta1);
  if (tb0 > tb1) std::swap(tb0, tb1);
  const double t0 = std::max(ta0, tb0);
  const double t1 = std::min(ta1, tb1);
  return t1 - t0 > tolerance;
}

// The coplanar counterpart TrianglesProperlyOverlap() above cannot answer
// (see its own "coplanar or parallel" early-out): whether two triangles
// that genuinely SHARE a plane (not merely have parallel normals - every
// vertex of `b` must also lie within `tolerance` of `a`'s own plane, so
// two parallel-but-offset faces, e.g. the box test fixture's own top and
// bottom, are correctly rejected here too) overlap by a positive AREA
// once projected onto that shared plane. Both triangles are projected
// onto an orthonormal (u, v) basis of the plane (`u` along `a`'s own
// first edge, `v = na x u`), then tested with the standard two-convex-
// polygon separating-axis theorem: a line perpendicular to one of the
// two triangles' own (up to) 6 edges, in this 2D basis, separates them
// iff every projected extent of one triangle lies more than `tolerance`
// to one side of the other's - no such axis existing among all 6 means a
// genuine overlap. Each candidate axis is unitized before its projected
// extents are compared, so `tolerance` is a real distance in the shared
// plane, not a raw (edge-length-scaled) dot product.
bool CoplanarTrianglesOverlap(const Point3d a[3], const Point3d b[3], double tolerance) {
  Vector3d na = ON_CrossProduct(a[1] - a[0], a[2] - a[0]);
  if (!na.Unitize()) return false;
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(ON_DotProduct(b[i] - a[0], na)) > tolerance) return false;  // not on a's own plane
  }
  Vector3d nb = ON_CrossProduct(b[1] - b[0], b[2] - b[0]);
  if (!nb.Unitize()) return false;
  if (ON_CrossProduct(na, nb).Length() > tolerance::kZeroVector) return false;  // not even parallel

  Vector3d u = a[1] - a[0];
  if (!u.Unitize()) return false;
  const Vector3d v = ON_CrossProduct(na, u);
  auto project = [&](const Point3d p[3], ON_2dPoint out[3]) {
    for (int i = 0; i < 3; ++i) {
      const Vector3d d = p[i] - a[0];
      out[i] = ON_2dPoint(ON_DotProduct(d, u), ON_DotProduct(d, v));
    }
  };
  ON_2dPoint pa[3], pb[3];
  project(a, pa);
  project(b, pb);

  ON_2dVector axes[6];
  for (int i = 0; i < 3; ++i) axes[i] = pa[(i + 1) % 3] - pa[i];
  for (int i = 0; i < 3; ++i) axes[3 + i] = pb[(i + 1) % 3] - pb[i];
  for (ON_2dVector edge : axes) {
    ON_2dVector axis(-edge.y, edge.x);
    if (!axis.Unitize()) continue;  // degenerate (zero-length) edge - not a valid axis
    double amin = std::numeric_limits<double>::infinity(), amax = -amin;
    double bmin = amin, bmax = amax;
    for (int i = 0; i < 3; ++i) {
      const double ta = axis.x * pa[i].x + axis.y * pa[i].y;
      amin = std::min(amin, ta);
      amax = std::max(amax, ta);
      const double tb = axis.x * pb[i].x + axis.y * pb[i].y;
      bmin = std::min(bmin, tb);
      bmax = std::max(bmax, tb);
    }
    if (amax < bmin - tolerance || bmax < amin - tolerance) return false;  // separating axis found
  }
  return true;
}

// Area-weighted per-vertex reconciliation of a per-face thickness vector -
// the same weighting scheme ComputeVertexNormals() already uses for
// direction, applied here to a scalar instead. Shared by both per-face
// `Mesh::Shell` overloads (with and without removed faces): a face named in
// `removed_face_indices` still contributes its own thickness entry to any
// vertex it shares with a kept neighbour, the same "no special-casing for
// removed faces" choice the uniform-thickness `Shell(thickness,
// removed_face_indices)` overload already makes for its own feasibility
// guard (checked against the FULL mesh, not just the post-removal part).
std::vector<double> AreaWeightedVertexThickness(const ON_Mesh& mesh, const std::vector<double>& face_thickness) {
  const int n = mesh.m_V.Count();
  std::vector<double> weighted_sum(static_cast<size_t>(n), 0.0);
  std::vector<double> weight_sum(static_cast<size_t>(n), 0.0);
  auto accumulate_triangle = [&](int i0, int i1, int i2, double t) {
    const Point3d a(mesh.m_V[i0]);
    const Point3d b(mesh.m_V[i1]);
    const Point3d c(mesh.m_V[i2]);
    const double w = ON_CrossProduct(b - a, c - a).Length();
    weighted_sum[static_cast<size_t>(i0)] += w * t;
    weighted_sum[static_cast<size_t>(i1)] += w * t;
    weighted_sum[static_cast<size_t>(i2)] += w * t;
    weight_sum[static_cast<size_t>(i0)] += w;
    weight_sum[static_cast<size_t>(i1)] += w;
    weight_sum[static_cast<size_t>(i2)] += w;
  };
  for (int i = 0; i < mesh.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh.m_F[i];
    accumulate_triangle(f.vi[0], f.vi[1], f.vi[2], face_thickness[static_cast<size_t>(i)]);
    if (f.IsQuad()) {
      accumulate_triangle(f.vi[0], f.vi[2], f.vi[3], face_thickness[static_cast<size_t>(i)]);
    }
  }
  std::vector<double> vertex_thickness(static_cast<size_t>(n), 0.0);
  for (int i = 0; i < n; ++i) {
    const double w = weight_sum[static_cast<size_t>(i)];
    vertex_thickness[static_cast<size_t>(i)] = w > tolerance::kZero ? weighted_sum[static_cast<size_t>(i)] / w : 0.0;
  }
  return vertex_thickness;
}

// Refuses a naked-edge boundary that is not a single simple loop (or
// several entirely disjoint simple loops) - a vertex visited by more than
// 2 naked edges means the boundary revisits itself ("bowtie": two lobes
// pinched together at one vertex, or a lobe sharing a vertex with its own
// rim elsewhere), which the side-wall stitching loop shared by `Thicken()`
// and both removed-face `Shell` overloads (one quad per naked edge,
// walking (a, b) -> (b+n, a+n) or its reverse) can't represent
// unambiguously: at a degree-4-or-more vertex there is no way to tell,
// from the naked edges alone, which two of them form one lobe's own local
// corner and which two form another's. `method_name` and
// `boundary_description` are folded into the exception text so the same
// check reads naturally from either caller - see PARITY_MAP.md's "Shell
// with removed/open faces (cup/case)" and "Thicken sheet" bullets for the
// disclosed gaps this closes.
void ThrowIfNakedBoundaryIsBowtie(const std::vector<std::pair<int, int>>& naked_edges, const char* method_name,
                                   const char* boundary_description) {
  std::map<int, int> naked_degree;
  for (const auto& [a, b] : naked_edges) {
    ++naked_degree[a];
    ++naked_degree[b];
  }
  for (const auto& [vertex, degree] : naked_degree) {
    if (degree > 2) {
      throw std::invalid_argument(std::string("dino8::kernel::Mesh::") + method_name + ": " + boundary_description +
                                   " is a self-touching (\"bowtie\") boundary - a vertex is shared by more than two "
                                   "naked edges, which the side-wall stitching can't represent unambiguously");
    }
  }
}

}  // namespace

Mesh::CheckReport Mesh::Check(double tolerance) const {
  CheckReport report;
  const double tol = std::max(tolerance, 0.0);
  std::map<std::pair<int, int>, int> undirected_count;
  std::map<std::pair<int, int>, int> directed_count;
  std::set<std::vector<int>> seen_faces;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    ForEachDirectedEdge(f, [&](int a, int b) {
      ++undirected_count[std::minmax(a, b)];
      ++directed_count[std::make_pair(a, b)];
    });

    if (IsDegenerateFace(mesh_, f, tol)) ++report.degenerate_faces;
    if (!seen_faces.insert(CanonicalFaceKey(f)).second) ++report.duplicate_faces;
  }
  for (const auto& [edge, count] : undirected_count) {
    if (count == 1) ++report.naked_edges;
    else if (count > 2) ++report.non_manifold_edges;
  }
  for (const auto& [edge, count] : directed_count) {
    if (count > 1) ++report.orientation_conflicts;
    if (count == 1 && undirected_count[std::minmax(edge.first, edge.second)] == 1) {
      report.naked_edge_list.push_back(edge);
    }
  }
  // naked_edge_list in face order, not map order.
  {
    std::set<std::pair<int, int>> naked(report.naked_edge_list.begin(), report.naked_edge_list.end());
    report.naked_edge_list.clear();
    for (int i = 0; i < mesh_.m_F.Count(); ++i) {
      ForEachDirectedEdge(mesh_.m_F[i], [&](int a, int b) {
        if (naked.count({a, b})) report.naked_edge_list.emplace_back(a, b);
      });
    }
  }
  // non_manifold_edge_list: one entry per non-manifold edge, undirected,
  // in the order first encountered walking the face list.
  {
    std::set<std::pair<int, int>> non_manifold;
    for (const auto& [edge, count] : undirected_count) {
      if (count > 2) non_manifold.insert(edge);
    }
    std::set<std::pair<int, int>> seen_nm;
    for (int i = 0; i < mesh_.m_F.Count() && seen_nm.size() < non_manifold.size(); ++i) {
      ForEachDirectedEdge(mesh_.m_F[i], [&](int a, int b) {
        const std::pair<int, int> key = std::minmax(a, b);
        if (non_manifold.count(key) != 0 && seen_nm.insert(key).second) {
          report.non_manifold_edge_list.push_back(key);
        }
      });
    }
  }
  // Duplicate vertices: every vertex is a candidate.
  std::vector<int> all(static_cast<size_t>(mesh_.m_V.Count()));
  for (int i = 0; i < mesh_.m_V.Count(); ++i) all[static_cast<size_t>(i)] = i;
  const std::vector<int> rep = WeldGroups(mesh_, all, tol);
  std::map<int, int> group_size;
  for (int i = 0; i < mesh_.m_V.Count(); ++i) ++group_size[rep[static_cast<size_t>(i)]];
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    if (group_size[rep[static_cast<size_t>(i)]] > 1) {
      ++report.duplicate_vertices;
      report.duplicate_vertex_list.push_back(i);
    }
  }
  // non_manifold_vertices / non_manifold_vertex_list: every vertex's own
  // incident faces, collected in one pass over the face list, then
  // grouped by GroupIncidentFacesByVertex() above - more than one group
  // means that vertex is a bowtie.
  {
    std::vector<std::vector<int>> incident(static_cast<size_t>(mesh_.m_V.Count()));
    for (int i = 0; i < mesh_.m_F.Count(); ++i) {
      const ON_MeshFace& f = mesh_.m_F[i];
      const int n = f.IsQuad() ? 4 : 3;
      for (int k = 0; k < n; ++k) incident[static_cast<size_t>(f.vi[k])].push_back(i);
    }
    for (int v = 0; v < mesh_.m_V.Count(); ++v) {
      const std::vector<int>& faces_here = incident[static_cast<size_t>(v)];
      if (faces_here.size() < 2) continue;
      const std::vector<int> group = GroupIncidentFacesByVertex(mesh_, v, faces_here);
      const int group_count = *std::max_element(group.begin(), group.end()) + 1;
      if (group_count > 1) {
        ++report.non_manifold_vertices;
        report.non_manifold_vertex_list.push_back(v);
      }
    }
  }
  // Whole-mesh body count: the distinct face-connectivity groups among all
  // faces - the same "faces sharing an edge are the same piece" definition
  // SubD::Check()'s own body_count already uses for SubD.
  report.body_count = static_cast<int>(GroupFacesByConnectivity(mesh_).second);
  return report;
}

std::vector<std::pair<int, int>> Mesh::FindSelfIntersections(double tolerance) const {
  const double tol = std::max(tolerance, 0.0);

  // Flat triangle list: each face contributes one triangle, or two -
  // (0,1,2) and (0,2,3) - for a quad, the exact split Contains()'s own ray
  // cast already uses. A quad's own two triangles always share an edge, so
  // they fall out through the ordinary "shares a vertex" skip below, the
  // same as any other legitimately adjacent pair.
  struct Tri {
    int face;
    int vi[3];
    ON_BoundingBox box;
  };
  std::vector<Tri> tris;
  tris.reserve(static_cast<size_t>(mesh_.m_F.Count()) * 2);
  auto add_tri = [&](int face, int i0, int i1, int i2) {
    Tri t;
    t.face = face;
    t.vi[0] = i0;
    t.vi[1] = i1;
    t.vi[2] = i2;
    t.box.Set(Point3d(mesh_.m_V[i0]), true);
    t.box.Set(Point3d(mesh_.m_V[i1]), true);
    t.box.Set(Point3d(mesh_.m_V[i2]), true);
    tris.push_back(t);
  };
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    add_tri(i, f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) add_tri(i, f.vi[0], f.vi[2], f.vi[3]);
  }
  if (tris.size() < 2) return {};

  // Broad phase: a uniform grid over every triangle's own bounding box,
  // cell count scaled to triangle count - mirrors surface_intersect.cpp's
  // own Grid (built there for a cross-mesh test; here for a single mesh's
  // self-test). Every triangle is inserted into every cell its own,
  // `tol`-padded bounding box overlaps, so a near-miss at a cell boundary
  // is still found; this never skips a genuine candidate pair, only
  // (in the ordinary case) avoids testing every pair outright.
  ON_BoundingBox box = tris[0].box;
  for (size_t i = 1; i < tris.size(); ++i) box.Union(tris[i].box);
  const ON_3dVector pad(tol, tol, tol);
  box.m_min -= pad;
  box.m_max += pad;
  const double diag = std::max(box.Diagonal().Length(), 1.0);
  const double target = std::max(1.0, std::cbrt(static_cast<double>(tris.size())));
  int n[3];
  double cell[3];
  for (int k = 0; k < 3; ++k) {
    n[k] = static_cast<int>(std::clamp(std::ceil(target), 1.0, 48.0));
    const double ext = box.m_max[k] - box.m_min[k];
    if (ext > 0) {
      cell[k] = ext / n[k];
    } else {
      n[k] = 1;
      cell[k] = diag;
    }
  }
  auto index_of = [&](int x, int y, int z) { return (static_cast<size_t>(z) * n[1] + y) * static_cast<size_t>(n[0]) + x; };
  auto box_range = [&](const ON_BoundingBox& b, int lo[3], int hi[3]) {
    for (int k = 0; k < 3; ++k) {
      lo[k] = static_cast<int>(std::clamp(std::floor((b.m_min[k] - box.m_min[k]) / cell[k]), 0.0, static_cast<double>(n[k] - 1)));
      hi[k] = static_cast<int>(std::clamp(std::floor((b.m_max[k] - box.m_min[k]) / cell[k]), 0.0, static_cast<double>(n[k] - 1)));
    }
  };
  std::map<size_t, std::vector<int>> cells;
  for (size_t t = 0; t < tris.size(); ++t) {
    int lo[3], hi[3];
    box_range(tris[t].box, lo, hi);
    for (int z = lo[2]; z <= hi[2]; ++z)
      for (int y = lo[1]; y <= hi[1]; ++y)
        for (int x = lo[0]; x <= hi[0]; ++x) cells[index_of(x, y, z)].push_back(static_cast<int>(t));
  }

  std::set<std::pair<int, int>> hits;
  std::vector<int> stamp(tris.size(), -1);
  int mark = 0;
  for (size_t ta = 0; ta < tris.size(); ++ta) {
    int lo[3], hi[3];
    box_range(tris[ta].box, lo, hi);
    ++mark;
    for (int z = lo[2]; z <= hi[2]; ++z)
      for (int y = lo[1]; y <= hi[1]; ++y)
        for (int x = lo[0]; x <= hi[0]; ++x) {
          const auto it = cells.find(index_of(x, y, z));
          if (it == cells.end()) continue;
          for (int tb : it->second) {
            if (tb <= static_cast<int>(ta) || stamp[static_cast<size_t>(tb)] == mark) continue;
            stamp[static_cast<size_t>(tb)] = mark;
            const Tri& A = tris[ta];
            const Tri& B = tris[static_cast<size_t>(tb)];
            if (A.face == B.face) continue;  // a quad's own two split triangles
            bool shares_vertex = false;
            for (int i = 0; i < 3 && !shares_vertex; ++i) {
              for (int j = 0; j < 3 && !shares_vertex; ++j) {
                if (A.vi[i] == B.vi[j]) shares_vertex = true;
              }
            }
            if (shares_vertex) continue;
            const Point3d pa[3] = {Point3d(mesh_.m_V[A.vi[0]]), Point3d(mesh_.m_V[A.vi[1]]), Point3d(mesh_.m_V[A.vi[2]])};
            const Point3d pb[3] = {Point3d(mesh_.m_V[B.vi[0]]), Point3d(mesh_.m_V[B.vi[1]]), Point3d(mesh_.m_V[B.vi[2]])};
            if (TrianglesProperlyOverlap(pa, pb, tol) || CoplanarTrianglesOverlap(pa, pb, tol)) {
              hits.insert(std::minmax(A.face, B.face));
            }
          }
        }
  }
  return std::vector<std::pair<int, int>>(hits.begin(), hits.end());
}

std::vector<std::vector<int>> Mesh::NakedEdgeLoops() const {
  const CheckReport report = Check();
  std::map<int, std::vector<int>> next;
  for (const auto& [a, b] : report.naked_edge_list) next[a].push_back(b);
  std::vector<std::vector<int>> loops;
  std::set<int> used;
  for (const auto& [a, b] : report.naked_edge_list) {
    if (used.count(a)) continue;
    std::vector<int> loop;
    int v = a;
    bool ok = true;
    while (true) {
      const auto it = next.find(v);
      if (it == next.end() || it->second.size() != 1 || used.count(v)) {
        ok = false;  // dead end, bowtie vertex, or re-entry into a used vertex
        break;
      }
      used.insert(v);
      loop.push_back(v);
      v = it->second[0];
      if (v == a) break;
    }
    if (ok && loop.size() >= 3) loops.push_back(std::move(loop));
  }
  return loops;
}

int Mesh::CloseNakedEdges(double tolerance) {
  const CheckReport report = Check(tolerance);
  std::set<int> boundary;
  for (const auto& [a, b] : report.naked_edge_list) {
    boundary.insert(a);
    boundary.insert(b);
  }
  if (boundary.empty()) return 0;
  const std::vector<int> candidates(boundary.begin(), boundary.end());
  const std::vector<int> rep = WeldGroups(mesh_, candidates, std::max(tolerance, 0.0));
  int welded = 0;
  std::vector<int> remap(static_cast<size_t>(mesh_.m_V.Count()));
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const int r = rep[static_cast<size_t>(i)];
    remap[static_cast<size_t>(i)] = (r >= 0) ? r : i;
    if (r >= 0 && r != i) ++welded;
  }
  if (welded == 0) return 0;

  // Remap faces, dropping the ones that collapsed.
  ON_SimpleArray<ON_MeshFace> faces;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    ON_MeshFace f = mesh_.m_F[i];
    const bool quad = f.IsQuad();
    for (int k = 0; k < 4; ++k) f.vi[k] = remap[static_cast<size_t>(f.vi[k])];
    std::vector<int> distinct;
    for (int k = 0; k < (quad ? 4 : 3); ++k) {
      if (std::find(distinct.begin(), distinct.end(), f.vi[k]) == distinct.end()) distinct.push_back(f.vi[k]);
    }
    if (distinct.size() < 3) continue;
    if (distinct.size() == 3) {
      f.vi[0] = distinct[0];
      f.vi[1] = distinct[1];
      f.vi[2] = distinct[2];
      f.vi[3] = distinct[2];
    }
    faces.Append(f);
  }
  mesh_.m_F = faces;
  CompactUnusedVertices(mesh_);
  mesh_.m_S.Destroy();
  mesh_.m_N.Destroy();
  mesh_.m_FN.Destroy();
  return welded;
}

int Mesh::MergeDuplicateVertices(double tolerance) {
  const double tol = std::max(tolerance, 0.0);
  if (mesh_.m_V.Count() < 2) return 0;
  std::vector<int> all(static_cast<size_t>(mesh_.m_V.Count()));
  for (int i = 0; i < mesh_.m_V.Count(); ++i) all[static_cast<size_t>(i)] = i;
  const std::vector<int> rep = WeldGroups(mesh_, all, tol);
  int welded = 0;
  std::vector<int> remap(static_cast<size_t>(mesh_.m_V.Count()));
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const int r = rep[static_cast<size_t>(i)];
    remap[static_cast<size_t>(i)] = (r >= 0) ? r : i;
    if (r >= 0 && r != i) ++welded;
  }
  if (welded == 0) return 0;

  // Same remap-and-drop-collapsed-faces shape as CloseNakedEdges() above,
  // just applied to every vertex's own group rather than only the
  // naked-edge-restricted `candidates` that method builds.
  ON_SimpleArray<ON_MeshFace> faces;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    ON_MeshFace f = mesh_.m_F[i];
    const bool quad = f.IsQuad();
    for (int k = 0; k < 4; ++k) f.vi[k] = remap[static_cast<size_t>(f.vi[k])];
    std::vector<int> distinct;
    for (int k = 0; k < (quad ? 4 : 3); ++k) {
      if (std::find(distinct.begin(), distinct.end(), f.vi[k]) == distinct.end()) distinct.push_back(f.vi[k]);
    }
    if (distinct.size() < 3) continue;
    if (distinct.size() == 3) {
      f.vi[0] = distinct[0];
      f.vi[1] = distinct[1];
      f.vi[2] = distinct[2];
      f.vi[3] = distinct[2];
    }
    faces.Append(f);
  }
  mesh_.m_F = faces;
  CompactUnusedVertices(mesh_);
  mesh_.m_S.Destroy();
  mesh_.m_N.Destroy();
  mesh_.m_FN.Destroy();
  return welded;
}

bool Mesh::SplitNonManifoldVertex(int vertex_index) {
  if (vertex_index < 0 || vertex_index >= mesh_.m_V.Count()) return false;

  std::vector<int> incident;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    const int n = f.IsQuad() ? 4 : 3;
    for (int k = 0; k < n; ++k) {
      if (f.vi[k] == vertex_index) {
        incident.push_back(i);
        break;
      }
    }
  }
  if (incident.size() < 2) return false;

  const std::vector<int> group = GroupIncidentFacesByVertex(mesh_, vertex_index, incident);
  const int group_count = *std::max_element(group.begin(), group.end()) + 1;
  if (group_count < 2) return false;  // already one connected fan - nothing to split

  // Group 0 (first-seen order, the same convention Brep::SplitNonManifold
  // Vertex()/SubD::SplitNonManifoldVertex() both already use) keeps
  // `vertex_index` itself; every other group gets a freshly appended
  // vertex at the same point. Unlike the SubD version, a Mesh vertex is
  // just an array position, so no id/watermark bookkeeping is needed - a
  // plain append is enough, and no OTHER vertex's own index moves.
  std::vector<int> new_index(static_cast<size_t>(group_count), vertex_index);
  for (int g = 1; g < group_count; ++g) {
    new_index[static_cast<size_t>(g)] = mesh_.m_V.Count();
    mesh_.m_V.Append(mesh_.m_V[vertex_index]);
  }
  for (size_t k = 0; k < incident.size(); ++k) {
    ON_MeshFace& f = mesh_.m_F[incident[k]];
    // All 4 slots, unconditionally - a triangle's own vi[3] mirrors vi[2]
    // (ON_MeshFace::IsQuad()'s own definition), so both independently
    // matching `vertex_index` here and getting the SAME new_index[group[k]]
    // keeps that mirror intact without a separate triangle-only fixup.
    for (int c = 0; c < 4; ++c) {
      if (f.vi[c] == vertex_index) f.vi[c] = new_index[static_cast<size_t>(group[k])];
    }
  }
  mesh_.m_S.Destroy();
  mesh_.m_N.Destroy();
  mesh_.m_FN.Destroy();
  return true;
}

int Mesh::SplitNonManifoldVertices(double tolerance) {
  const std::vector<int> to_split = Check(tolerance).non_manifold_vertex_list;
  int count = 0;
  for (const int v : to_split) {
    if (SplitNonManifoldVertex(v)) ++count;
  }
  return count;
}

std::vector<Mesh> Mesh::SplitDisjointPieces() const {
  const auto [group, group_count] = GroupFacesByConnectivity(mesh_);
  if (group_count <= 1) return {*this};

  // Group faces by group id, preserving first-encountered order so the
  // returned pieces come back in a stable, reproducible order rather than
  // whatever order the underlying union-find roots happen to land on -
  // GroupFacesByConnectivity() already assigns group ids in that order.
  std::vector<std::vector<int>> member_faces(group_count);
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    member_faces[static_cast<size_t>(group[static_cast<size_t>(i)])].push_back(i);
  }

  std::vector<Mesh> pieces;
  pieces.reserve(group_count);
  for (const std::vector<int>& faces : member_faces) {
    Mesh piece;
    piece.mesh_.m_V = mesh_.m_V;
    for (const int idx : faces) piece.mesh_.m_F.Append(mesh_.m_F[idx]);
    // Drops every vertex not referenced by this piece's own faces and
    // remaps m_F onto the resulting compact 0-based indices - a plain
    // per-piece renumbering, not an id-preserving rebuild the way
    // SubD::SplitDisjointPieces() needs (a Mesh vertex is just an array
    // position, with no stable id to preserve across pieces).
    CompactUnusedVertices(piece.mesh_);
    pieces.push_back(std::move(piece));
  }
  return pieces;
}

int Mesh::RemoveDegenerateFaces(double tolerance) {
  const double tol = std::max(tolerance, 0.0);
  ON_SimpleArray<ON_MeshFace> faces;
  int removed = 0;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    if (IsDegenerateFace(mesh_, mesh_.m_F[i], tol)) {
      ++removed;
      continue;
    }
    faces.Append(mesh_.m_F[i]);
  }
  if (removed == 0) return 0;
  mesh_.m_F = faces;
  CompactUnusedVertices(mesh_);
  mesh_.m_S.Destroy();
  mesh_.m_N.Destroy();
  mesh_.m_FN.Destroy();
  return removed;
}

int Mesh::RemoveDuplicateFaces() {
  std::set<std::vector<int>> seen;
  ON_SimpleArray<ON_MeshFace> faces;
  int removed = 0;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    if (!seen.insert(CanonicalFaceKey(mesh_.m_F[i])).second) {
      ++removed;
      continue;
    }
    faces.Append(mesh_.m_F[i]);
  }
  if (removed == 0) return 0;
  mesh_.m_F = faces;
  CompactUnusedVertices(mesh_);
  mesh_.m_S.Destroy();
  mesh_.m_N.Destroy();
  mesh_.m_FN.Destroy();
  return removed;
}

int Mesh::FillSmallHoles(double max_extent) {
  int filled = 0;
  for (const std::vector<int>& loop : NakedEdgeLoops()) {
    ON_BoundingBox box;
    for (const int v : loop) box.Set(ON_3dPoint(mesh_.m_V[v]), true);
    if (box.Diagonal().Length() > max_extent) continue;
    if (loop.size() == 3) {
      ON_MeshFace f;
      f.vi[0] = loop[2];
      f.vi[1] = loop[1];
      f.vi[2] = loop[0];
      f.vi[3] = loop[0];
      mesh_.m_F.Append(f);
    } else {
      ON_3dPoint centroid(0, 0, 0);
      for (const int v : loop) centroid += ON_3dPoint(mesh_.m_V[v]);
      centroid = ON_3dPoint(centroid.x / loop.size(), centroid.y / loop.size(), centroid.z / loop.size());
      const int c = mesh_.m_V.Count();
      mesh_.m_V.Append(ON_3fPoint(centroid));
      for (size_t i = 0; i < loop.size(); ++i) {
        const int a = loop[i], b = loop[(i + 1) % loop.size()];
        ON_MeshFace f;
        f.vi[0] = b;  // reverse of the naked edge a -> b
        f.vi[1] = a;
        f.vi[2] = c;
        f.vi[3] = c;
        mesh_.m_F.Append(f);
      }
    }
    ++filled;
  }
  if (filled > 0) {
    mesh_.m_S.Destroy();
    mesh_.m_N.Destroy();
    mesh_.m_FN.Destroy();
  }
  return filled;
}

int Mesh::UnifyNormals() {
  const int n = mesh_.m_F.Count();
  std::map<std::pair<int, int>, std::vector<int>> faces_of_edge;
  for (int i = 0; i < n; ++i) {
    ForEachDirectedEdge(mesh_.m_F[i], [&](int a, int b) { faces_of_edge[std::minmax(a, b)].push_back(i); });
  }
  auto walks = [&](int face, int a, int b) {
    bool forward = false;
    ForEachDirectedEdge(mesh_.m_F[face], [&](int x, int y) {
      if (x == a && y == b) forward = true;
    });
    return forward;
  };
  std::vector<char> done(static_cast<size_t>(n), 0);
  int flipped = 0;
  for (int seed = 0; seed < n; ++seed) {
    if (done[static_cast<size_t>(seed)]) continue;
    std::vector<int> queue = {seed};
    done[static_cast<size_t>(seed)] = 1;
    while (!queue.empty()) {
      const int fi = queue.back();
      queue.pop_back();
      std::vector<std::pair<int, int>> edges;
      ForEachDirectedEdge(mesh_.m_F[fi], [&](int a, int b) { edges.emplace_back(a, b); });
      for (const auto& [a, b] : edges) {
        const std::vector<int>& users = faces_of_edge[std::minmax(a, b)];
        if (users.size() != 2) continue;
        const int other = users[0] == fi ? users[1] : users[0];
        if (done[static_cast<size_t>(other)]) continue;
        if (walks(other, a, b)) {
          FlipOneFace(mesh_.m_F[other]);
          ++flipped;
        }
        done[static_cast<size_t>(other)] = 1;
        queue.push_back(other);
      }
    }
  }
  if (IsClosedManifold() && Volume() < 0.0) {
    for (int i = 0; i < n; ++i) FlipOneFace(mesh_.m_F[i]);
    flipped += n;
  }
  if (flipped > 0) {
    mesh_.m_N.Destroy();
    mesh_.m_FN.Destroy();
  }
  return flipped;
}

int Mesh::TrisToQuads(double max_dihedral_deg) {
  const int n = mesh_.m_F.Count();
  if (n < 2) return 0;

  // Every triangle face's participation in each undirected edge - a
  // quad's own edges, and any edge already non-manifold, are excluded
  // up front since they can never be a merge candidate.
  std::map<std::pair<int, int>, std::vector<int>> tri_faces_of_edge;
  for (int i = 0; i < n; ++i) {
    if (mesh_.m_F[i].IsQuad()) continue;
    ForEachDirectedEdge(mesh_.m_F[i], [&](int a, int b) { tri_faces_of_edge[std::minmax(a, b)].push_back(i); });
  }

  struct Candidate {
    int face_a, face_b;
    int q0, q1, q2, q3;
    double score;
  };
  std::vector<Candidate> candidates;
  const double max_dihedral_rad = std::max(0.0, max_dihedral_deg) * ON_PI / 180.0;

  for (const auto& [edge, faces] : tri_faces_of_edge) {
    if (faces.size() != 2) continue;
    const int fi = faces[0], fj = faces[1];
    const ON_MeshFace& tri_i = mesh_.m_F[fi];
    const ON_MeshFace& tri_j = mesh_.m_F[fj];

    // The edge's own walked direction in tri_i; a consistently-wound
    // manifold pair walks it the opposite way in tri_j.
    int a = -1, b = -1;
    ForEachDirectedEdge(tri_i, [&](int x, int y) {
      if (x == edge.first && y == edge.second) { a = x; b = y; }
      else if (x == edge.second && y == edge.first) { a = x; b = y; }
    });
    bool opposite = false;
    ForEachDirectedEdge(tri_j, [&](int x, int y) { if (x == b && y == a) opposite = true; });
    if (a < 0 || !opposite) continue;

    const int c = ThirdTriangleVertex(tri_i, a, b);
    const int d = ThirdTriangleVertex(tri_j, a, b);
    if (c < 0 || d < 0 || c == d) continue;

    const Point3d pa(mesh_.m_V[a]), pb(mesh_.m_V[b]), pc(mesh_.m_V[c]), pd(mesh_.m_V[d]);
    Vector3d ni = ON_CrossProduct(pb - pa, pc - pa);
    Vector3d nj = ON_CrossProduct(pa - pb, pd - pb);
    if (!ni.Unitize() || !nj.Unitize()) continue;  // degenerate triangle: no normal
    const double cos_angle = std::max(-1.0, std::min(1.0, ON_DotProduct(ni, nj)));
    const double dihedral = std::acos(cos_angle);
    if (dihedral > max_dihedral_rad) continue;

    // Merged quad boundary, dropping the shared edge as the implicit
    // diagonal: a -> d -> b -> c -> a (see TrisToQuads()'s own doc
    // comment in mesh.h for the derivation).
    const int q[4] = {a, d, b, c};
    Vector3d qn = ni + nj;
    if (!qn.Unitize()) continue;
    bool convex = true;
    for (int k = 0; k < 4 && convex; ++k) {
      const Point3d p0(mesh_.m_V[q[(k + 3) % 4]]);
      const Point3d p1(mesh_.m_V[q[k]]);
      const Point3d p2(mesh_.m_V[q[(k + 1) % 4]]);
      const Vector3d cr = ON_CrossProduct(p1 - p0, p2 - p1);
      if (ON_DotProduct(cr, qn) < -tolerance::kZeroVector) convex = false;
    }
    if (!convex) continue;

    candidates.push_back({fi, fj, q[0], q[1], q[2], q[3], dihedral});
  }

  std::sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) { return x.score < y.score; });

  std::vector<char> consumed(static_cast<size_t>(n), 0);
  std::vector<char> absorbed(static_cast<size_t>(n), 0);
  std::map<int, ON_MeshFace> merged_quad;
  int merged = 0;
  for (const Candidate& cand : candidates) {
    if (consumed[static_cast<size_t>(cand.face_a)] || consumed[static_cast<size_t>(cand.face_b)]) continue;
    consumed[static_cast<size_t>(cand.face_a)] = consumed[static_cast<size_t>(cand.face_b)] = 1;
    absorbed[static_cast<size_t>(cand.face_b)] = 1;
    ON_MeshFace qf;
    qf.vi[0] = cand.q0;
    qf.vi[1] = cand.q1;
    qf.vi[2] = cand.q2;
    qf.vi[3] = cand.q3;
    merged_quad[cand.face_a] = qf;
    ++merged;
  }
  if (merged == 0) return 0;

  ON_SimpleArray<ON_MeshFace> new_faces;
  for (int i = 0; i < n; ++i) {
    if (absorbed[static_cast<size_t>(i)]) continue;
    const auto it = merged_quad.find(i);
    new_faces.Append(it != merged_quad.end() ? it->second : mesh_.m_F[i]);
  }
  mesh_.m_F = new_faces;
  mesh_.m_S.Destroy();
  mesh_.m_N.Destroy();
  mesh_.m_FN.Destroy();
  return merged;
}

Mesh Mesh::Offset(double distance) const {
  if (!std::isfinite(distance)) {
    throw std::invalid_argument("dino8::kernel::Mesh::Offset: distance must be finite");
  }
  Mesh result = *this;
  const std::vector<Vector3d> normals = ComputeVertexNormals();
  for (int i = 0; i < result.mesh_.m_V.Count(); ++i) {
    const ON_3dPoint moved = ON_3dPoint(result.mesh_.m_V[i]) + distance * normals[static_cast<size_t>(i)];
    result.mesh_.m_V[i] = ON_3fPoint(moved);
  }
  result.mesh_.m_N.Destroy();
  result.mesh_.m_FN.Destroy();
  return result;
}

Mesh Mesh::OffsetDirectional(double distance, const Vector3d& direction) const {
  if (!std::isfinite(distance)) {
    throw std::invalid_argument("dino8::kernel::Mesh::OffsetDirectional: distance must be finite");
  }
  Vector3d unit_direction = direction;
  if (!unit_direction.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::OffsetDirectional: direction must not be the "
        "zero vector");
  }
  Mesh result = *this;
  const ON_3dVector offset = distance * unit_direction;
  for (int i = 0; i < result.mesh_.m_V.Count(); ++i) {
    const ON_3dPoint moved = ON_3dPoint(result.mesh_.m_V[i]) + offset;
    result.mesh_.m_V[i] = ON_3fPoint(moved);
  }
  result.mesh_.m_N.Destroy();
  result.mesh_.m_FN.Destroy();
  return result;
}

Mesh Mesh::Thicken(double distance) const {
  if (!std::isfinite(distance) || distance == 0.0) {
    throw std::invalid_argument("dino8::kernel::Mesh::Thicken: distance must be finite and nonzero");
  }
  const CheckReport report = Check();
  if (report.naked_edge_list.empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Thicken: this mesh has no naked edges to wall "
        "up (it's already closed) - Thicken() only handles an open sheet; "
        "a closed mesh needs a hollowing/shell operation this method "
        "doesn't attempt");
  }
  // Same hazard `Mesh::Shell`'s removed-face overloads already guard
  // against on their own opening boundary: an open sheet whose naked-edge
  // rim touches itself at a vertex (e.g. two lobes joined at a single
  // point, not an edge) gives that vertex a naked-edge degree above 2,
  // which the side-wall loop below - one quad per naked edge, no
  // knowledge of which lobe an edge belongs to - can't stitch
  // unambiguously without producing a non-manifold wall there.
  ThrowIfNakedBoundaryIsBowtie(report.naked_edge_list, "Thicken", "this mesh's own naked-edge boundary");

  const Mesh outer = Offset(distance);
  // Same fold-safety guard `Mesh::Shell(thickness)` already runs on its own
  // inward offset copy before trusting it as a wall (see that overload's
  // own `FindSelfIntersections()` check): a `distance` large enough - or a
  // sheet curved/creased enough - folds this offset copy through itself,
  // silently producing a self-intersecting outer wall with no single
  // out-of-range input to catch it otherwise. Scope-limited like that
  // sibling guard: it only catches the offset copy folding through
  // ITSELF, not a wall crossing the original sheet or the side walls
  // self-intersecting against either layer - the "no fold repair"
  // disclosed gap on this method (see PARITY_MAP.md's "Thicken sheet"
  // bullet) is about repair, and stays open; this closes the silent-
  // corruption half of it by refusing instead.
  if (!outer.FindSelfIntersections().empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Thicken: distance folds the offset copy "
        "through itself - exceeds this sheet's own local wall feasibility "
        "somewhere (too large a distance for how sharply it's curved or "
        "creased there)");
  }
  const int n = mesh_.m_V.Count();

  Mesh result;
  ON_Mesh& raw = result.raw();
  raw.m_V.Reserve(n * 2);
  for (int i = 0; i < n; ++i) raw.m_V.Append(mesh_.m_V[i]);
  for (int i = 0; i < n; ++i) raw.m_V.Append(outer.raw().m_V[i]);

  raw.m_F.Reserve(mesh_.m_F.Count() * 2 + static_cast<int>(report.naked_edge_list.size()));
  // Inner wall: the original faces, flipped.
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    ON_MeshFace f = mesh_.m_F[i];
    FlipOneFace(f);
    raw.m_F.Append(f);
  }
  // Outer wall: the offset copy's faces, same winding, reindexed by +n.
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    ON_MeshFace f = mesh_.m_F[i];
    for (int k = 0; k < 4; ++k) f.vi[k] += n;
    raw.m_F.Append(f);
  }
  // Side walls: one quad per naked edge (a, b), already directed outward.
  for (const auto& [a, b] : report.naked_edge_list) {
    ON_MeshFace f;
    f.vi[0] = a;
    f.vi[1] = b;
    f.vi[2] = b + n;
    f.vi[3] = a + n;
    raw.m_F.Append(f);
  }
  return result;
}

Mesh Mesh::Shell(double thickness) const {
  if (!(thickness > 0.0)) {
    throw std::invalid_argument("dino8::kernel::Mesh::Shell: thickness must be strictly positive");
  }
  if (!IsClosedManifold()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: this mesh is not a closed 2-manifold - "
        "an open sheet needs Thicken(), not Shell()");
  }

  // Always inward - see this method's own header doc comment for why
  // `thickness` is a magnitude, not a signed direction, unlike Thicken()'s
  // own `distance`.
  const Mesh inner_unflipped = Offset(-thickness);
  if (!inner_unflipped.FindSelfIntersections().empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: thickness folds the inward offset "
        "through itself - exceeds the local wall-to-wall feasibility "
        "somewhere on this mesh");
  }
  // A self-intersection-free result can still have silently turned inside
  // out (every wall folded past the far side without any single pair of
  // triangles crossing, on a shape thin/curved enough) - the enclosed
  // volume must be strictly smaller than the original's, and still
  // positive, or this "shell" isn't genuinely nested inside its own outer
  // layer.
  const double outer_volume = Volume();
  const double inner_volume = inner_unflipped.Volume();
  if (!(inner_volume > 0.0 && inner_volume < outer_volume)) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: thickness is too large - the inward "
        "offset has collapsed or inverted through the opposite wall rather "
        "than nesting inside this mesh");
  }

  const int n = mesh_.m_V.Count();
  Mesh result;
  ON_Mesh& raw = result.raw();
  raw.m_V.Reserve(n * 2);
  for (int i = 0; i < n; ++i) raw.m_V.Append(mesh_.m_V[i]);
  for (int i = 0; i < n; ++i) raw.m_V.Append(inner_unflipped.raw().m_V[i]);

  raw.m_F.Reserve(mesh_.m_F.Count() * 2);
  // Outer wall: this mesh's own faces, entirely unchanged.
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    raw.m_F.Append(mesh_.m_F[i]);
  }
  // Inner wall: the inward offset copy's faces, flipped, reindexed by +n.
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    ON_MeshFace f = mesh_.m_F[i];
    for (int k = 0; k < 4; ++k) f.vi[k] += n;
    FlipOneFace(f);
    raw.m_F.Append(f);
  }
  return result;
}

Mesh Mesh::Shell(double thickness, const std::vector<int>& removed_face_indices) const {
  if (!(thickness > 0.0)) {
    throw std::invalid_argument("dino8::kernel::Mesh::Shell: thickness must be strictly positive");
  }
  if (!IsClosedManifold()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: this mesh is not a closed 2-manifold - "
        "an open sheet needs Thicken(), not Shell()");
  }
  if (removed_face_indices.empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: removed_face_indices is empty - call "
        "Shell(thickness) instead for a fully closed shell with no openings");
  }
  const int face_count = mesh_.m_F.Count();
  std::set<int> removed(removed_face_indices.begin(), removed_face_indices.end());
  if (removed.size() != removed_face_indices.size()) {
    throw std::invalid_argument("dino8::kernel::Mesh::Shell: removed_face_indices contains a duplicate index");
  }
  for (int i : removed) {
    if (i < 0 || i >= face_count) {
      throw std::invalid_argument("dino8::kernel::Mesh::Shell: removed_face_indices contains an out-of-range index");
    }
  }
  if (static_cast<int>(removed.size()) >= face_count) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: removed_face_indices names every face - "
        "an entirely open shell has no outer wall left to hollow");
  }

  // Same feasibility guards as Shell(thickness), checked against the FULL
  // mesh - see this overload's own header doc comment for why opening up
  // some faces can only ever relax this, never worsen it.
  const Mesh inner_unflipped_full = Offset(-thickness);
  if (!inner_unflipped_full.FindSelfIntersections().empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: thickness folds the inward offset "
        "through itself - exceeds the local wall-to-wall feasibility "
        "somewhere on this mesh");
  }
  const double outer_volume = Volume();
  const double inner_volume = inner_unflipped_full.Volume();
  if (!(inner_volume > 0.0 && inner_volume < outer_volume)) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: thickness is too large - the inward "
        "offset has collapsed or inverted through the opposite wall rather "
        "than nesting inside this mesh");
  }

  const int n = mesh_.m_V.Count();

  // The outer layer: this mesh's own faces, unchanged, minus the removed
  // ones - kept in a real Mesh so Check() can compute its post-removal
  // naked_edge_list directly, rather than re-deriving edge-counting logic
  // here a second time.
  Mesh outer_open;
  {
    ON_Mesh& outer_raw = outer_open.raw();
    outer_raw.m_V = mesh_.m_V;
    outer_raw.m_F.Reserve(face_count - static_cast<int>(removed.size()));
    for (int i = 0; i < face_count; ++i) {
      if (removed.count(i) == 0) outer_raw.m_F.Append(mesh_.m_F[i]);
    }
  }
  const auto opening_naked_edges = outer_open.Check().naked_edge_list;
  ThrowIfNakedBoundaryIsBowtie(opening_naked_edges, "Shell", "removed_face_indices produces an opening whose own rim");

  Mesh result;
  ON_Mesh& raw = result.raw();
  raw.m_V.Reserve(n * 2);
  for (int i = 0; i < n; ++i) raw.m_V.Append(mesh_.m_V[i]);
  for (int i = 0; i < n; ++i) raw.m_V.Append(inner_unflipped_full.raw().m_V[i]);

  raw.m_F.Reserve(outer_open.raw().m_F.Count() * 2 + static_cast<int>(opening_naked_edges.size()));
  // Outer wall: the post-removal outer layer, entirely unchanged.
  for (int i = 0; i < outer_open.raw().m_F.Count(); ++i) {
    raw.m_F.Append(outer_open.raw().m_F[i]);
  }
  // Inner wall: the SAME faces removed from the inward offset copy,
  // flipped, reindexed by +n.
  for (int i = 0; i < face_count; ++i) {
    if (removed.count(i) != 0) continue;
    ON_MeshFace f = mesh_.m_F[i];
    for (int k = 0; k < 4; ++k) f.vi[k] += n;
    FlipOneFace(f);
    raw.m_F.Append(f);
  }
  // Side walls: one quad per naked edge of the post-removal outer layer -
  // REVERSED from Thicken()'s own `vi = {a, b, b+n, a+n}`, deliberately:
  // Thicken()'s naked edge (a, b) comes from the sheet BEFORE it gets
  // flipped into the inner-wall role, so the wall's own (a, b) edge ends
  // up opposite the STORED (post-flip) inner face's direction there. Here
  // the outer layer is stored UNFLIPPED (this method's own "outer layer
  // unchanged" convention), so the wall must instead walk (b, a) to end
  // up opposite the outer layer's own stored direction at that edge - and
  // correspondingly opposite the flipped inner layer's stored direction
  // at (a+n, b+n) too (a flip reverses every one of a face's directed
  // edges individually, so the inner layer's stored direction there is
  // (b+n, a+n), the reverse of the wall's own (a+n, b+n)). Verified
  // empirically via a standalone Check() dump before finalizing (8
  // "orientation_conflicts" with the naive a/b order, 0 with this one).
  for (const auto& [a, b] : opening_naked_edges) {
    ON_MeshFace f;
    f.vi[0] = b;
    f.vi[1] = a;
    f.vi[2] = a + n;
    f.vi[3] = b + n;
    raw.m_F.Append(f);
  }
  return result;
}

Mesh Mesh::Shell(const std::vector<double>& face_thickness) const {
  const int face_count = mesh_.m_F.Count();
  if (static_cast<int>(face_thickness.size()) != face_count) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: face_thickness.size() must equal FaceCount()");
  }
  for (double t : face_thickness) {
    if (!std::isfinite(t) || !(t > 0.0)) {
      throw std::invalid_argument(
          "dino8::kernel::Mesh::Shell: every face_thickness entry must be finite and strictly positive");
    }
  }
  if (!IsClosedManifold()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: this mesh is not a closed 2-manifold - "
        "an open sheet needs Thicken(), not Shell()");
  }

  // Area-weighted per-vertex reconciliation of the per-face thicknesses -
  // the same weighting scheme ComputeVertexNormals() already uses for
  // direction (see this method's own header doc comment), applied here
  // to a scalar instead of a vector. Shared with the removed-faces overload
  // below via AreaWeightedVertexThickness().
  const int n = mesh_.m_V.Count();
  const std::vector<double> vertex_thickness = AreaWeightedVertexThickness(mesh_, face_thickness);

  const std::vector<Vector3d> normals = ComputeVertexNormals();
  Mesh inner_unflipped = *this;
  for (int i = 0; i < n; ++i) {
    const ON_3dPoint moved =
        ON_3dPoint(inner_unflipped.raw().m_V[i]) - vertex_thickness[static_cast<size_t>(i)] * normals[static_cast<size_t>(i)];
    inner_unflipped.raw().m_V[i] = ON_3fPoint(moved);
  }
  inner_unflipped.raw().m_N.Destroy();
  inner_unflipped.raw().m_FN.Destroy();

  // Same feasibility guards Shell(thickness) already applies, against the
  // actual per-vertex offset this method applies rather than a uniform
  // stand-in.
  if (!inner_unflipped.FindSelfIntersections().empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: face_thickness folds the inward offset "
        "through itself - exceeds the local wall-to-wall feasibility somewhere on this mesh");
  }
  const double outer_volume = Volume();
  const double inner_volume = inner_unflipped.Volume();
  if (!(inner_volume > 0.0 && inner_volume < outer_volume)) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: face_thickness is too large somewhere - the inward "
        "offset has collapsed or inverted through the opposite wall rather than nesting inside this mesh");
  }

  Mesh result;
  ON_Mesh& raw = result.raw();
  raw.m_V.Reserve(n * 2);
  for (int i = 0; i < n; ++i) raw.m_V.Append(mesh_.m_V[i]);
  for (int i = 0; i < n; ++i) raw.m_V.Append(inner_unflipped.raw().m_V[i]);

  raw.m_F.Reserve(face_count * 2);
  for (int i = 0; i < face_count; ++i) raw.m_F.Append(mesh_.m_F[i]);
  for (int i = 0; i < face_count; ++i) {
    ON_MeshFace f = mesh_.m_F[i];
    for (int k = 0; k < 4; ++k) f.vi[k] += n;
    FlipOneFace(f);
    raw.m_F.Append(f);
  }
  return result;
}

// The two most recent Shell() generalizations combined: a per-face
// thickness vector AND a set of removed (opening) faces in one call,
// closing PARITY_MAP.md's "Shell with removed/open faces" and "Per-face
// (multi-thickness) shell" bullets' own remaining "still separate" gap -
// previously a caller wanting both a cup/case opening and a varying wall
// thickness had no single kernel entry point for it. Construction reuses
// every piece of machinery its two parents already established rather
// than inventing a third: AreaWeightedVertexThickness() for the per-vertex
// blend (Shell(face_thickness)'s own technique), the post-removal outer
// layer plus opening-boundary side-wall stitching (Shell(thickness,
// removed_face_indices)'s own technique, including its `vi = {b, a, a+n,
// b+n}` winding and its new ThrowIfOpeningBoundaryIsBowtie() guard).
Mesh Mesh::Shell(const std::vector<double>& face_thickness, const std::vector<int>& removed_face_indices) const {
  const int face_count = mesh_.m_F.Count();
  if (static_cast<int>(face_thickness.size()) != face_count) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: face_thickness.size() must equal FaceCount()");
  }
  for (double t : face_thickness) {
    if (!std::isfinite(t) || !(t > 0.0)) {
      throw std::invalid_argument(
          "dino8::kernel::Mesh::Shell: every face_thickness entry must be finite and strictly positive");
    }
  }
  if (!IsClosedManifold()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: this mesh is not a closed 2-manifold - "
        "an open sheet needs Thicken(), not Shell()");
  }
  if (removed_face_indices.empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: removed_face_indices is empty - call "
        "Shell(face_thickness) instead for a fully closed per-face shell with no openings");
  }
  std::set<int> removed(removed_face_indices.begin(), removed_face_indices.end());
  if (removed.size() != removed_face_indices.size()) {
    throw std::invalid_argument("dino8::kernel::Mesh::Shell: removed_face_indices contains a duplicate index");
  }
  for (int i : removed) {
    if (i < 0 || i >= face_count) {
      throw std::invalid_argument("dino8::kernel::Mesh::Shell: removed_face_indices contains an out-of-range index");
    }
  }
  if (static_cast<int>(removed.size()) >= face_count) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: removed_face_indices names every face - "
        "an entirely open shell has no outer wall left to hollow");
  }

  // Same area-weighted reconciliation Shell(face_thickness) uses, over the
  // FULL face set - a removed face's own named thickness still blends into
  // a boundary vertex it shares with a kept neighbour (see
  // AreaWeightedVertexThickness()'s own comment for why this is the
  // deliberate, non-special-cased choice).
  const int n = mesh_.m_V.Count();
  const std::vector<double> vertex_thickness = AreaWeightedVertexThickness(mesh_, face_thickness);

  const std::vector<Vector3d> normals = ComputeVertexNormals();
  Mesh inner_unflipped = *this;
  for (int i = 0; i < n; ++i) {
    const ON_3dPoint moved =
        ON_3dPoint(inner_unflipped.raw().m_V[i]) - vertex_thickness[static_cast<size_t>(i)] * normals[static_cast<size_t>(i)];
    inner_unflipped.raw().m_V[i] = ON_3fPoint(moved);
  }
  inner_unflipped.raw().m_N.Destroy();
  inner_unflipped.raw().m_FN.Destroy();

  // Same feasibility guards the other three Shell() overloads already
  // apply, against the FULL per-vertex offset (see Shell(thickness,
  // removed_face_indices)'s own comment for why opening up faces can only
  // relax this, never worsen it).
  if (!inner_unflipped.FindSelfIntersections().empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: face_thickness folds the inward offset "
        "through itself - exceeds the local wall-to-wall feasibility somewhere on this mesh");
  }
  const double outer_volume = Volume();
  const double inner_volume = inner_unflipped.Volume();
  if (!(inner_volume > 0.0 && inner_volume < outer_volume)) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Shell: face_thickness is too large somewhere - the inward "
        "offset has collapsed or inverted through the opposite wall rather than nesting inside this mesh");
  }

  Mesh outer_open;
  {
    ON_Mesh& outer_raw = outer_open.raw();
    outer_raw.m_V = mesh_.m_V;
    outer_raw.m_F.Reserve(face_count - static_cast<int>(removed.size()));
    for (int i = 0; i < face_count; ++i) {
      if (removed.count(i) == 0) outer_raw.m_F.Append(mesh_.m_F[i]);
    }
  }
  const auto opening_naked_edges = outer_open.Check().naked_edge_list;
  ThrowIfNakedBoundaryIsBowtie(opening_naked_edges, "Shell", "removed_face_indices produces an opening whose own rim");

  Mesh result;
  ON_Mesh& raw = result.raw();
  raw.m_V.Reserve(n * 2);
  for (int i = 0; i < n; ++i) raw.m_V.Append(mesh_.m_V[i]);
  for (int i = 0; i < n; ++i) raw.m_V.Append(inner_unflipped.raw().m_V[i]);

  raw.m_F.Reserve(outer_open.raw().m_F.Count() * 2 + static_cast<int>(opening_naked_edges.size()));
  // Outer wall: the post-removal outer layer, entirely unchanged.
  for (int i = 0; i < outer_open.raw().m_F.Count(); ++i) {
    raw.m_F.Append(outer_open.raw().m_F[i]);
  }
  // Inner wall: the SAME faces removed from the inward offset copy,
  // flipped, reindexed by +n.
  for (int i = 0; i < face_count; ++i) {
    if (removed.count(i) != 0) continue;
    ON_MeshFace f = mesh_.m_F[i];
    for (int k = 0; k < 4; ++k) f.vi[k] += n;
    FlipOneFace(f);
    raw.m_F.Append(f);
  }
  // Side walls: same reversed winding as Shell(thickness,
  // removed_face_indices) uses, for the same reason (see that overload's
  // own comment) - the outer layer here is likewise stored unflipped.
  for (const auto& [a, b] : opening_naked_edges) {
    ON_MeshFace f;
    f.vi[0] = b;
    f.vi[1] = a;
    f.vi[2] = a + n;
    f.vi[3] = b + n;
    raw.m_F.Append(f);
  }
  return result;
}

std::vector<std::pair<int, int>> Mesh::FindOffsetSelfIntersections(double distance, double tolerance) const {
  return Offset(distance).FindSelfIntersections(tolerance);
}

Mesh Mesh::InsetFace(int face_index, double distance, double depth) const {
  if (face_index < 0 || face_index >= mesh_.m_F.Count()) {
    throw std::invalid_argument("dino8::kernel::Mesh::InsetFace: face_index out of range");
  }
  if (!std::isfinite(distance) || !(distance > 0.0)) {
    throw std::invalid_argument("dino8::kernel::Mesh::InsetFace: distance must be finite and strictly positive");
  }
  if (!std::isfinite(depth)) {
    throw std::invalid_argument("dino8::kernel::Mesh::InsetFace: depth must be finite");
  }

  const ON_MeshFace& f = mesh_.m_F[face_index];
  const int n = f.IsQuad() ? 4 : 3;
  const std::array<int, 4> idx{f.vi[0], f.vi[1], f.vi[2], f.vi[3]};
  std::vector<Point3d> ring(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) ring[static_cast<size_t>(i)] = Point3d(mesh_.m_V[idx[static_cast<size_t>(i)]]);

  // Reuses the same planar/simple ring validation LoftClosedRings()'s own
  // end-cap fixtures already rely on (see NewellNormal()'s own comment
  // for why a self-intersecting ring must be rejected BEFORE taking its
  // Newell normal, not after).
  if (!IsRingPlanar(ring)) {
    throw std::invalid_argument("dino8::kernel::Mesh::InsetFace: face is not planar");
  }
  if (!IsPlanarRingSimple(ring)) {
    throw std::invalid_argument("dino8::kernel::Mesh::InsetFace: face boundary self-intersects");
  }

  Vector3d normal = NewellNormal(ring);
  if (!normal.Unitize()) {
    throw std::invalid_argument("dino8::kernel::Mesh::InsetFace: face is degenerate (zero area)");
  }

  // Per-edge direction and inward (in-plane) normal - the exact
  // `OffsetConvexPolyline` (sweep.cpp) construction, applied to this
  // face's own closed boundary ring instead of an open/closed profile
  // curve.
  std::vector<Vector3d> edir(static_cast<size_t>(n)), ndir(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    Vector3d d = ring[static_cast<size_t>((i + 1) % n)] - ring[static_cast<size_t>(i)];
    if (!d.Unitize()) {
      throw std::invalid_argument("dino8::kernel::Mesh::InsetFace: face has a zero-length edge");
    }
    edir[static_cast<size_t>(i)] = d;
    Vector3d nrm = ON_CrossProduct(normal, d);
    if (!nrm.Unitize()) {
      throw std::invalid_argument("dino8::kernel::Mesh::InsetFace: degenerate edge inward direction");
    }
    ndir[static_cast<size_t>(i)] = nrm;
  }

  // Convexity: every turn must agree in sign, the same test
  // OffsetConvexPolyline() uses (a reflex corner risks a self-
  // intersecting inset this method does not detect/repair).
  {
    double sign = 0.0;
    for (int i = 0; i < n; ++i) {
      const Vector3d& a = edir[static_cast<size_t>(i)];
      const Vector3d& b = edir[static_cast<size_t>((i + 1) % n)];
      const double cross = ON_DotProduct(ON_CrossProduct(a, b), normal);
      if (std::fabs(cross) <= 1e-9) continue;
      const double this_sign = cross > 0.0 ? 1.0 : -1.0;
      if (sign == 0.0) {
        sign = this_sign;
      } else if (this_sign != sign) {
        throw std::invalid_argument(
            "dino8::kernel::Mesh::InsetFace: face has a reflex (concave) corner - "
            "only a convex triangle or quad face can be inset");
      }
    }
  }

  // Mitered inset corner = exact intersection of the two adjacent moved
  // (parallel-translated inward by `distance`) edges - identical formula
  // to OffsetConvexPolyline()'s own closed-polygon case.
  std::vector<Point3d> inset(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const Vector3d& n0 = ndir[static_cast<size_t>((i - 1 + n) % n)];
    const Vector3d& n1 = ndir[static_cast<size_t>(i)];
    const double denom = 1.0 + ON_DotProduct(n0, n1);
    if (denom <= 1e-9) {
      throw std::invalid_argument(
          "dino8::kernel::Mesh::InsetFace: the face folds back on itself at a "
          "near-180-degree corner - no finite miter inset exists there");
    }
    inset[static_cast<size_t>(i)] = ring[static_cast<size_t>(i)] + (distance / denom) * (n0 + n1);
  }

  // Validity: every inset edge must be a positive multiple of its own
  // original direction - OffsetConvexPolyline()'s own sufficient
  // simplicity proof for a convex ring, applied here (see that
  // function's doc comment in sweep.cpp for the full argument). A
  // `distance` past the face's own inradius fails this.
  for (int i = 0; i < n; ++i) {
    const Vector3d e = inset[static_cast<size_t>((i + 1) % n)] - inset[static_cast<size_t>(i)];
    if (ON_DotProduct(e, edir[static_cast<size_t>(i)]) <= 0.0) {
      throw std::invalid_argument(
          "dino8::kernel::Mesh::InsetFace: distance exceeds the face's own "
          "inradius - the inset would invert past a corner");
    }
  }

  if (depth != 0.0) {
    for (Point3d& p : inset) p = p + depth * normal;
  }

  Mesh result = *this;
  ON_Mesh& raw = result.mesh_;
  const int base = raw.m_V.Count();
  for (int i = 0; i < n; ++i) raw.m_V.Append(ON_3fPoint(inset[static_cast<size_t>(i)]));

  // Replace the original face with a ring of `n` frame quads (one per
  // original edge, spanning that edge and its own inset counterpart,
  // wound the same way as the original face) plus one new inner face at
  // the inset ring itself.
  ON_SimpleArray<ON_MeshFace> faces;
  faces.Reserve(raw.m_F.Count() + n);
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    if (i != face_index) faces.Append(raw.m_F[i]);
  }
  for (int i = 0; i < n; ++i) {
    ON_MeshFace frame;
    frame.vi[0] = idx[static_cast<size_t>(i)];
    frame.vi[1] = idx[static_cast<size_t>((i + 1) % n)];
    frame.vi[2] = base + (i + 1) % n;
    frame.vi[3] = base + i;
    faces.Append(frame);
  }
  ON_MeshFace inner;
  inner.vi[0] = base + 0;
  inner.vi[1] = base + 1;
  inner.vi[2] = base + 2;
  inner.vi[3] = (n == 4) ? base + 3 : base + 2;
  faces.Append(inner);
  raw.m_F = faces;

  raw.m_N.Destroy();
  raw.m_FN.Destroy();
  return result;
}

}  // namespace dino8::kernel
