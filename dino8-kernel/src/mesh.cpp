#include "dino8/kernel/mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dino8/kernel/detail/polygon2d.h"

namespace dino8::kernel {

namespace {

// Parses a .obj face-line token into a 1-based vertex index, accepting
// both the plain "3" form SaveObj() writes and the "3/4/5"
// (vertex/texture/normal) form other tools write - only the part before
// the first '/' matters here, texture/normal indices are ignored (this
// kernel's ON_Mesh has no per-face-corner texture/normal data to put
// them in). Rejects anything else, including a negative (relative)
// index - documented as unsupported in LoadObj()'s own comment - and
// leaves `index` unchanged on failure.
bool ParseObjFaceIndex(const std::string& token, int& index) {
  const size_t slash = token.find('/');
  const std::string first = (slash == std::string::npos) ? token : token.substr(0, slash);
  if (first.empty()) {
    return false;
  }
  size_t consumed = 0;
  int value = 0;
  try {
    value = std::stoi(first, &consumed);
  } catch (const std::exception&) {
    return false;
  }
  if (consumed != first.size() || value <= 0) {
    return false;
  }
  index = value;
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

  if (std::abs(volume_sum) <= 1e-12) {
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

Result Mesh::SaveObj(const std::string& path) const {
  std::ofstream out(path);
  if (!out) {
    return Result::Failed;
  }

  for (int i = 0; i < mesh_.m_V.Count(); ++i) {
    const ON_3fPoint& v = mesh_.m_V[i];
    out << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
  }
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& f = mesh_.m_F[i];
    // OBJ vertex indices are 1-based.
    out << "f " << (f.vi[0] + 1) << ' ' << (f.vi[1] + 1) << ' ' << (f.vi[2] + 1);
    if (f.IsQuad()) {
      out << ' ' << (f.vi[3] + 1);
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
    } else if (tag == "f") {
      std::vector<int> indices;
      std::string token;
      while (stream >> token) {
        int index = 0;
        if (!ParseObjFaceIndex(token, index)) {
          return Result::Failed;
        }
        indices.push_back(index);
      }
      if (indices.size() < 3 || indices.size() > 4) {
        return Result::Failed;
      }
      for (const int index : indices) {
        if (index < 1 || index > raw.m_V.Count()) {
          return Result::Failed;  // forward/unknown reference, or out of range
        }
      }
      ON_MeshFace face;
      face.vi[0] = indices[0] - 1;
      face.vi[1] = indices[1] - 1;
      face.vi[2] = indices[2] - 1;
      face.vi[3] = (indices.size() == 4) ? indices[3] - 1 : indices[2] - 1;
      raw.m_F.Append(face);
    }
    // Every other tag (comments, vt/vn, g/o, mtllib/usemtl, s, ...) is
    // silently skipped - this kernel only round-trips geometry.
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

Mesh Mesh::MergeAndWeld(const std::vector<Mesh>& meshes, double tolerance) {
  Mesh result;
  ON_Mesh& out = result.mesh_;

  // Snap each coordinate to a grid of `tolerance` size so two vertices
  // within `tolerance` of each other (in particular, the same seam point
  // computed independently by two adjacent faces) map to the same key.
  auto snap = [tolerance](float v) {
    return static_cast<long long>(std::lround(static_cast<double>(v) / tolerance));
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
    if (normal.Length() > 1e-9) {
      break;
    }
  }
  if (normal.Length() <= 1e-9) {
    // Every triple tried was collinear/degenerate - not planar-polygon
    // shaped at all; leave that to fail elsewhere (or trivially "pass"
    // here) rather than misclassify a degenerate ring as self-intersecting.
    return true;
  }
  return dino8::kernel::detail::IsSimplePolygon(ProjectOntoPlane(ring, ring[0], normal));
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
  if (m < 3) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::RevolveProfile: profile needs at least 3 points "
        "(on-axis start, at least one ring, on-axis end)");
  }
  constexpr double kOnAxisEpsilon = 1e-9;
  if (std::abs(profile.front().x) > kOnAxisEpsilon || std::abs(profile.back().x) > kOnAxisEpsilon) {
    throw std::invalid_argument(
        "dino8::kernel::Mesh::RevolveProfile: profile's first and last points "
        "must have radius 0 (lie on the axis) - a profile needing a flat end "
        "cap instead isn't supported here");
  }

  Vector3d n = axis;
  n.Unitize();
  const Vector3d reference = (std::abs(n.z) < 0.9) ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d ex = ON_CrossProduct(reference, n);
  ex.Unitize();
  const Vector3d ey = ON_CrossProduct(n, ex);  // ex x ey = n (ex _|_ n): a right-handed frame,
                                                // so increasing theta below sweeps ex toward ey.

  Mesh result;
  ON_Mesh& out = result.mesh_;

  // ring_start[i]: index of profile point i's first (and, for the two
  // on-axis ends, only) vertex in the output mesh.
  std::vector<int> ring_start(static_cast<size_t>(m));
  for (int i = 0; i < m; ++i) {
    ring_start[static_cast<size_t>(i)] = out.m_V.Count();
    const double r = profile[static_cast<size_t>(i)].x;
    const double h = profile[static_cast<size_t>(i)].y;
    if (i == 0 || i == m - 1) {
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
    const bool i_is_apex = (i == 0);
    const bool i1_is_apex = (i + 1 == m - 1);
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

Mesh Mesh::Torus(Point3d center, Vector3d axis, double major_radius, double minor_radius,
                 int major_segments, int minor_segments) {
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

}  // namespace dino8::kernel
