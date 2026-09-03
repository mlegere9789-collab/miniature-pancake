#include "dino8/kernel/mesh.h"

#include <cmath>
#include <map>
#include <set>
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

Mesh Mesh::ExtrudeCappedSolid(const Mesh& cap, Vector3d offset) {
  Mesh result;
  ON_Mesh& out = result.mesh_;
  const ON_Mesh& in = cap.mesh_;
  const int n = in.m_V.Count();

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

  // Boundary-edge extraction: a directed edge (a, b) that appears in some
  // triangle's winding is an interior edge if its reverse (b, a) also
  // appears (from the triangle on the other side); otherwise it's on the
  // cap's boundary loop. This works for any cap shape - including a
  // trimmed face's jagged/staircased boundary - without needing to know
  // the boundary's "ideal" curve.
  std::set<std::pair<int, int>> directed_edges;
  for (int i = 0; i < in.m_F.Count(); ++i) {
    const ON_MeshFace& f = in.m_F[i];
    directed_edges.insert({f.vi[0], f.vi[1]});
    directed_edges.insert({f.vi[1], f.vi[2]});
    directed_edges.insert({f.vi[2], f.vi[0]});
  }
  for (const auto& edge : directed_edges) {
    const int a = edge.first;
    const int b = edge.second;
    if (directed_edges.count({b, a}) != 0) {
      continue;  // interior edge, shared by two triangles
    }
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

}  // namespace dino8::kernel
