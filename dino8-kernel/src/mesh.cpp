#include "dino8/kernel/mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/detail/polygon2d.h"
#include "dino8/kernel/tolerance.h"
#include "dino8/kernel/detail/segment3d.h"

namespace dino8::kernel {

namespace {

// Parses one '/'-separated field of a .obj face-line token into a
// positive 1-based index. Empty (both slashes present but nothing
// between them, e.g. the "v//vn" form's middle field) is treated as
// "absent", not a parse failure - the caller distinguishes the two via
// the returned bool.
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
  if (consumed != field.size() || parsed <= 0) {
    return false;
  }
  value = parsed;
  return true;
}

// Parses a .obj face-line token into its 1-based vertex index (`v_index`,
// required) and, if present, its 1-based texture-coordinate index
// (`vt_index`, `has_vt` set true) - accepting the plain "3" form, the
// "3/4" (vertex/texture) form, and the "3/4/5" (vertex/texture/normal)
// and "3//5" (vertex/normal only) forms other tools write. The normal
// index, when present, is parsed away but discarded - this kernel's
// ON_Mesh has no per-face-corner normal data to put it in (vertex normals
// here are always geometry-derived via ComputeVertexNormals(), never
// stored independently). Rejects a malformed or non-positive vertex
// index, including a negative (relative) one - documented as unsupported
// in LoadObj()'s own comment. A malformed (non-empty but unparsable)
// texture-coordinate field is also rejected, but its true *absence*
// (the "v//vn" form) is not.
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

  Mesh result;
  ON_Mesh& raw = result.mesh_;

  std::vector<Point2d> texture_coords;
  // Vertex index (0-based) -> texture coordinate, populated only for
  // vertices actually referenced with a `vt` in some face corner. Last
  // write wins if two corners sharing a vertex reference different `vt`
  // entries - see LoadObj()'s own doc comment on why (per-vertex-only
  // storage can't represent a genuine UV seam).
  std::map<int, Point2d> vertex_uv_by_index;

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
      raw.m_V.Append(ON_3fPoint(x, y, z));
    } else if (tag == "vt") {
      double u, v;
      if (!(stream >> u >> v)) {
        return Result::Failed;
      }
      texture_coords.push_back(Point2d(u, v));
    } else if (tag == "f") {
      std::vector<int> indices;
      std::vector<int> vt_indices;  // -1 for a corner with no vt reference
      std::string token;
      while (stream >> token) {
        int v_index = 0;
        int vt_index = 0;
        bool has_vt = false;
        if (!ParseObjFaceIndex(token, v_index, vt_index, has_vt)) {
          return Result::Failed;
        }
        indices.push_back(v_index);
        vt_indices.push_back(has_vt ? vt_index : -1);
      }
      if (indices.size() < 3 || indices.size() > 4) {
        return Result::Failed;
      }
      for (const int index : indices) {
        if (index < 1 || index > raw.m_V.Count()) {
          return Result::Failed;  // forward/unknown reference, or out of range
        }
      }
      for (const int vt_index : vt_indices) {
        if (vt_index != -1 && (vt_index < 1 || vt_index > static_cast<int>(texture_coords.size()))) {
          return Result::Failed;  // forward/unknown vt reference, or out of range
        }
      }
      for (size_t i = 0; i < indices.size(); ++i) {
        if (vt_indices[i] != -1) {
          vertex_uv_by_index[indices[i] - 1] =
              texture_coords[static_cast<size_t>(vt_indices[i]) - 1];
        }
      }
      ON_MeshFace face;
      face.vi[0] = indices[0] - 1;
      face.vi[1] = indices[1] - 1;
      face.vi[2] = indices[2] - 1;
      face.vi[3] = (indices.size() == 4) ? indices[3] - 1 : indices[2] - 1;
      raw.m_F.Append(face);
    }
    // Every other tag (comments, vn, g/o, mtllib/usemtl, s, ...) is
    // silently skipped - this kernel only round-trips geometry (and, now,
    // per-vertex texture coordinates).
  }

  // Only store texture coordinates if every vertex ended up with one -
  // ON_Mesh's own "m_S.Count() == m_V.Count() or ignore it entirely"
  // convention (see HasTextureCoordinates()) has no way to represent
  // "some vertices have a UV, others don't", so a partial set (some
  // referenced with `vt`, some never referenced at all) is discarded
  // rather than guessing placeholder values for the rest.
  if (!vertex_uv_by_index.empty() &&
      static_cast<int>(vertex_uv_by_index.size()) == raw.m_V.Count()) {
    std::vector<Point2d> uvs(static_cast<size_t>(raw.m_V.Count()));
    for (const auto& [index, uv] : vertex_uv_by_index) {
      uvs[static_cast<size_t>(index)] = uv;
    }
    result.SetTextureCoordinates(uvs);
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
// <name>" line from a PLY header.
struct PlyProperty {
  bool is_list = false;
  std::string name;
};

// One "element <name> <count>" block from a PLY header, plus the
// property lines that followed it.
struct PlyElement {
  std::string name;
  int count = 0;
  std::vector<PlyProperty> properties;
};

// Parses an ASCII PLY header (everything up to and including
// "end_header") into an ordered list of elements. Returns false on any
// header line this kernel doesn't recognize, a "property" line before
// any "element" line, or a "format" line that isn't exactly
// "format ascii <version>" - PLY's binary_little_endian/
// binary_big_endian formats are a disclosed, out-of-scope gap (see
// Mesh::SavePly()'s own doc comment), not silently misread as ASCII.
bool ParsePlyHeader(std::istream& in, std::vector<PlyElement>& out_elements) {
  std::string line;
  if (!std::getline(in, line) || line != "ply") {
    return false;
  }
  if (!std::getline(in, line)) {
    return false;
  }
  {
    std::istringstream header(line);
    std::string tag, format;
    if (!(header >> tag >> format) || tag != "format" || format != "ascii") {
      return false;
    }
  }
  while (std::getline(in, line)) {
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
      } else {
        if (!(stream >> property.name)) {
          return false;
        }
      }
      out_elements.back().properties.push_back(std::move(property));
      continue;
    }
    return false;  // unrecognized header line
  }
  return false;  // stream ended without "end_header"
}

}  // namespace

Result Mesh::SavePly(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  const std::vector<Vector3d> normals = ComputeVertexNormals();
  const bool has_uvs = HasTextureCoordinates();

  out << "ply\n";
  out << "format ascii 1.0\n";
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
  out << "element face " << mesh_.m_F.Count() << '\n';
  out << "property list uchar int vertex_indices\n";
  out << "end_header\n";

  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& p = mesh_.m_V[i];
    const Vector3d& n = normals[static_cast<size_t>(i)];
    out << p.x << ' ' << p.y << ' ' << p.z << ' ' << n.x << ' ' << n.y << ' ' << n.z;
    if (has_uvs) {
      const Point2d uv = TextureCoordinateAt(i);
      out << ' ' << uv.x << ' ' << uv.y;
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

Result Mesh::LoadPly(const std::string& path, Mesh& out_mesh) {
  std::ifstream in(path);
  if (!in) {
    return Result::Failed;
  }

  std::vector<PlyElement> elements;
  if (!ParsePlyHeader(in, elements)) {
    return Result::Failed;
  }

  Mesh result;
  ON_Mesh& raw = result.raw();
  bool found_vertex = false;
  bool found_face = false;
  std::vector<Point2d> uvs;
  bool have_uvs = false;

  for (const PlyElement& element : elements) {
    if (element.name == "vertex") {
      found_vertex = true;
      int idx_x = -1, idx_y = -1, idx_z = -1, idx_u = -1, idx_v = -1;
      for (size_t i = 0; i < element.properties.size(); ++i) {
        const PlyProperty& property = element.properties[i];
        if (property.is_list) {
          return Result::Failed;  // a list property on a vertex isn't a position/normal/UV
        }
        if (property.name == "x") idx_x = static_cast<int>(i);
        else if (property.name == "y") idx_y = static_cast<int>(i);
        else if (property.name == "z") idx_z = static_cast<int>(i);
        else if (property.name == "u") idx_u = static_cast<int>(i);
        else if (property.name == "v") idx_v = static_cast<int>(i);
        // nx/ny/nz and any other property (color, ...) are read as plain
        // columns below but never looked up by name - discarded, same
        // "always geometry-derived" convention as LoadObj()'s vn.
      }
      if (idx_x < 0 || idx_y < 0 || idx_z < 0) {
        return Result::Failed;
      }
      have_uvs = idx_u >= 0 && idx_v >= 0;

      std::string line;
      for (int row = 0; row < element.count; ++row) {
        if (!std::getline(in, line)) {
          return Result::Failed;
        }
        std::istringstream stream(line);
        std::vector<double> values(element.properties.size());
        for (double& value : values) {
          if (!(stream >> value)) {
            return Result::Failed;
          }
        }
        raw.m_V.Append(ON_3fPoint(values[static_cast<size_t>(idx_x)],
                                   values[static_cast<size_t>(idx_y)],
                                   values[static_cast<size_t>(idx_z)]));
        if (have_uvs) {
          uvs.push_back(Point2d(values[static_cast<size_t>(idx_u)], values[static_cast<size_t>(idx_v)]));
        }
      }
    } else if (element.name == "face") {
      found_face = true;
      if (element.properties.size() != 1 || !element.properties[0].is_list) {
        return Result::Failed;  // this kernel only reads the ordinary "one index list" face shape
      }
      std::string line;
      for (int row = 0; row < element.count; ++row) {
        if (!std::getline(in, line)) {
          return Result::Failed;
        }
        std::istringstream stream(line);
        int corner_count = 0;
        if (!(stream >> corner_count) || corner_count < 3 || corner_count > 4) {
          return Result::Failed;
        }
        int indices[4] = {0, 0, 0, 0};
        for (int i = 0; i < corner_count; ++i) {
          if (!(stream >> indices[i]) || indices[i] < 0 || indices[i] >= raw.m_V.Count()) {
            return Result::Failed;
          }
        }
        ON_MeshFace face;
        face.vi[0] = indices[0];
        face.vi[1] = indices[1];
        face.vi[2] = indices[2];
        face.vi[3] = (corner_count == 4) ? indices[3] : indices[2];
        raw.m_F.Append(face);
      }
    } else {
      // An element type this kernel doesn't read (e.g. a color-only
      // "edge" element) - skip its data lines rather than rejecting the
      // file over data this kernel was never going to use.
      std::string line;
      for (int row = 0; row < element.count; ++row) {
        if (!std::getline(in, line)) {
          return Result::Failed;
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
                          int revolve_segments) {
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
        const double theta = 2.0 * ON_PI * static_cast<double>(k) / revolve_segments;
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
  // Duplicate vertices: every vertex is a candidate.
  std::vector<int> all(static_cast<size_t>(mesh_.m_V.Count()));
  for (int i = 0; i < mesh_.m_V.Count(); ++i) all[static_cast<size_t>(i)] = i;
  const std::vector<int> rep = WeldGroups(mesh_, all, tol);
  std::map<int, int> group_size;
  for (int i = 0; i < mesh_.m_V.Count(); ++i) ++group_size[rep[static_cast<size_t>(i)]];
  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    if (group_size[rep[static_cast<size_t>(i)]] > 1) ++report.duplicate_vertices;
  }
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
            if (TrianglesProperlyOverlap(pa, pb, tol)) {
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

Mesh Mesh::Offset(double distance) const {
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

Mesh Mesh::Thicken(double distance) const {
  if (distance == 0.0) {
    throw std::invalid_argument("dino8::kernel::Mesh::Thicken: distance must be nonzero");
  }
  const CheckReport report = Check();
  if (report.naked_edge_list.empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::Thicken: this mesh has no naked edges to wall "
        "up (it's already closed) - Thicken() only handles an open sheet; "
        "a closed mesh needs a hollowing/shell operation this method "
        "doesn't attempt");
  }

  const Mesh outer = Offset(distance);
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

}  // namespace dino8::kernel
