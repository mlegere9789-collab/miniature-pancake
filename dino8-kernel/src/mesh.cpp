#include "dino8/kernel/mesh.h"

#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace dino8::kernel {

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

double Mesh::Area() const {
  double area = 0.0;
  for (int i = 0; i < mesh_.m_F.Count(); ++i) {
    const ON_MeshFace& face = mesh_.m_F[i];
    const ON_3fPoint& a = mesh_.m_V[face.vi[0]];
    const ON_3fPoint& b = mesh_.m_V[face.vi[1]];
    const ON_3fPoint& c = mesh_.m_V[face.vi[2]];
    const ON_3dVector cross =
        ON_3dVector::CrossProduct(ON_3dVector(b - a), ON_3dVector(c - a));
    area += 0.5 * cross.Length();
  }
  return area;
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

}  // namespace dino8::kernel
