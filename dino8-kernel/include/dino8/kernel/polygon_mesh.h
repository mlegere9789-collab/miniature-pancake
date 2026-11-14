#pragma once

// A polygon (n-gon) mesh and the primitive generators that give a Blender-style "Add" operator its exact element counts.
//
// ON_Mesh (and so dino8::kernel::Mesh) stores triangles and quads only, but several of Blender's mesh primitives are made of n-gons: the
// cylinder's caps and the circle's fill are one face of N corners each. Counting the faces, edges and vertices of such a primitive therefore
// needs a polygon representation. PolygonMesh is that representation: a vertex list and, per face, an ordered list of vertex indices
// (counter-clockwise seen from outside for a closed solid). It owns only what is needed to measure and convert a primitive; it is not a
// second editing mesh.
//
// The generators (PolygonPrimitives) follow the parameters and the element counts of the Blender operators bpy.ops.mesh.primitive_cube_add,
// primitive_plane_add, primitive_circle_add, primitive_cylinder_add, primitive_ico_sphere_add and primitive_torus_add (defaults: cube size 2,
// plane size 2, circle 32 vertices radius 1, cylinder 32 vertices radius 1 depth 2, ico sphere 2 subdivisions radius 1, torus 48 x 12
// segments, major radius 1, minor radius 0.25). Every primitive is centred on the origin, the axis of the cylinder and the pole of the circle
// are +Z.

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

struct PolygonMesh {
  std::vector<Point3d> vertices;
  std::vector<std::vector<int>> faces;
  std::vector<std::pair<int, int>> loose_edges;  // edges that belong to no face (the unfilled circle ring)

  int VertexCount() const { return static_cast<int>(vertices.size()); }
  int FaceCount() const { return static_cast<int>(faces.size()); }

  // Number of distinct undirected edges: every pair of consecutive corners of a face, a pair counted once however many faces use it.
  int EdgeCount() const { return static_cast<int>(EdgeUse().size()); }

  // V - E + F over every vertex, every edge (loose ones included) and every face.
  int EulerCharacteristic() const { return VertexCount() - EdgeCount() + FaceCount(); }

  // True when every edge is used by exactly two faces, once in each direction (a closed, consistently oriented 2-manifold surface).
  bool IsClosedOrientedManifold() const {
    std::map<std::pair<int, int>, int> directed;
    for (const auto& f : faces) {
      const int n = static_cast<int>(f.size());
      for (int k = 0; k < n; ++k) ++directed[{f[k], f[(k + 1) % n]}];
    }
    for (const auto& [e, c] : directed) {
      if (c != 1) return false;
      auto it = directed.find({e.second, e.first});
      if (it == directed.end() || it->second != 1) return false;
    }
    return !directed.empty();
  }

  // Sum of the face areas; a face is measured as the fan of triangles from its first corner, which is exact for a planar face.
  double Area() const {
    double a = 0.0;
    for (const auto& f : faces) a += FaceNormalTimesArea(f).Length();
    return a;
  }

  // Enclosed volume of a closed, outward-oriented polygon mesh: the sum over fan triangles of the signed tetrahedron volume with the origin.
  double Volume() const {
    double v = 0.0;
    for (const auto& f : faces) {
      const int n = static_cast<int>(f.size());
      for (int k = 1; k + 1 < n; ++k) {
        const ON_3dVector a(vertices[f[0]]), b(vertices[f[k]]), c(vertices[f[k + 1]]);
        v += ON_DotProduct(a, ON_CrossProduct(b, c)) / 6.0;
      }
    }
    return v;
  }

  // Triangle / quad mesh of the same surface: a triangle and a quad are kept, a larger face is fanned from its first corner (planar convex
  // faces only, which every generator here produces).
  Mesh ToMesh() const {
    Mesh m;
    for (const auto& p : vertices) m.raw().m_V.Append(ON_3fPoint(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)));
    for (const auto& f : faces) {
      const int n = static_cast<int>(f.size());
      if (n < 3) continue;
      if (n == 3 || n == 4) {
        ON_MeshFace mf;
        mf.vi[0] = f[0];
        mf.vi[1] = f[1];
        mf.vi[2] = f[2];
        mf.vi[3] = n == 4 ? f[3] : f[2];
        m.raw().m_F.Append(mf);
      } else {
        for (int k = 1; k + 1 < n; ++k) {
          ON_MeshFace mf;
          mf.vi[0] = f[0];
          mf.vi[1] = f[k];
          mf.vi[2] = f[k + 1];
          mf.vi[3] = f[k + 1];
          m.raw().m_F.Append(mf);
        }
      }
    }
    return m;
  }

  // Unit vertex normals: the sum over the faces around a vertex of the face normal weighted by the face area (the normal-times-area of the
  // fan), normalised; the zero vector for a vertex no face uses.
  std::vector<Vector3d> VertexNormals() const {
    std::vector<Vector3d> n(vertices.size(), Vector3d(0, 0, 0));
    for (const auto& f : faces) {
      const Vector3d fa = FaceNormalTimesArea(f);
      for (int i : f) n[i] += fa;
    }
    for (auto& v : n) v.Unitize();
    return n;
  }

  // Vertex -> sorted unique list of the vertices it shares an edge with.
  std::vector<std::vector<int>> VertexNeighbors() const {
    std::vector<std::vector<int>> nb(vertices.size());
    for (const auto& f : faces) {
      const int n = static_cast<int>(f.size());
      for (int k = 0; k < n; ++k) {
        const int a = f[k], b = f[(k + 1) % n];
        if (a == b) continue;
        nb[a].push_back(b);
        nb[b].push_back(a);
      }
    }
    for (auto& l : nb) {
      std::sort(l.begin(), l.end());
      l.erase(std::unique(l.begin(), l.end()), l.end());
    }
    return nb;
  }

  Vector3d FaceNormalTimesArea(const std::vector<int>& f) const {
    // Newell's method: exact for a planar polygon, and the area-weighted mean normal for a warped one. Its length is twice the area.
    Vector3d s(0, 0, 0);
    const int n = static_cast<int>(f.size());
    for (int k = 0; k < n; ++k) {
      const Point3d& p = vertices[f[k]];
      const Point3d& q = vertices[f[(k + 1) % n]];
      s.x += (p.y - q.y) * (p.z + q.z);
      s.y += (p.z - q.z) * (p.x + q.x);
      s.z += (p.x - q.x) * (p.y + q.y);
    }
    return s * 0.5;
  }

 private:
  std::map<std::pair<int, int>, int> EdgeUse() const {
    std::map<std::pair<int, int>, int> e;
    for (const auto& f : faces) {
      const int n = static_cast<int>(f.size());
      for (int k = 0; k < n; ++k) {
        const int a = f[k], b = f[(k + 1) % n];
        ++e[{std::min(a, b), std::max(a, b)}];
      }
    }
    for (const auto& le : loose_edges) ++e[{std::min(le.first, le.second), std::max(le.first, le.second)}];
    return e;
  }
};

enum class CapFill { Nothing, NGon, TriangleFan };

namespace PolygonPrimitives {

constexpr double kTwoPi = 6.283185307179586476925286766559;

// bpy.ops.mesh.primitive_cube_add(size): 8 vertices, 12 edges, 6 quads, edge length `size`.
inline PolygonMesh MakeCube(double size = 2.0) {
  PolygonMesh m;
  const double h = size / 2.0;
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) m.vertices.emplace_back(x ? h : -h, y ? h : -h, z ? h : -h);
  m.faces = {{0, 2, 3, 1}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}, {4, 5, 7, 6}};
  return m;
}

// bpy.ops.mesh.primitive_plane_add(size): 4 vertices, 4 edges, 1 quad of side `size` in the XY plane, normal +Z.
inline PolygonMesh MakePlane(double size = 2.0) {
  PolygonMesh m;
  const double h = size / 2.0;
  m.vertices = {Point3d(-h, -h, 0), Point3d(h, -h, 0), Point3d(h, h, 0), Point3d(-h, h, 0)};
  m.faces = {{0, 1, 2, 3}};
  return m;
}

// bpy.ops.mesh.primitive_circle_add(vertices, radius, fill_type): a ring of `vertices` points in the XY plane. Nothing: only the ring's
// edges (loose_edges); NGon: one face of `vertices` corners; TriangleFan: a centre vertex and `vertices` triangles.
inline PolygonMesh MakeCircle(int vertices = 32, double radius = 1.0, CapFill fill = CapFill::Nothing) {
  if (vertices < 3) throw std::invalid_argument("MakeCircle: vertices must be at least 3");
  PolygonMesh m;
  for (int i = 0; i < vertices; ++i) {
    const double a = kTwoPi * i / vertices;
    m.vertices.emplace_back(radius * std::cos(a), radius * std::sin(a), 0.0);
  }
  if (fill == CapFill::Nothing) {
    for (int i = 0; i < vertices; ++i) m.loose_edges.push_back({i, (i + 1) % vertices});
  } else if (fill == CapFill::NGon) {
    std::vector<int> f(vertices);
    for (int i = 0; i < vertices; ++i) f[i] = i;
    m.faces.push_back(f);
  } else {
    m.vertices.emplace_back(0.0, 0.0, 0.0);
    for (int i = 0; i < vertices; ++i) m.faces.push_back({vertices, i, (i + 1) % vertices});
  }
  return m;
}

// bpy.ops.mesh.primitive_cylinder_add(vertices, radius, depth, end_fill_type): two rings of `vertices` points at z = -depth/2 and +depth/2,
// `vertices` side quads and, per cap, one n-gon (NGon), a fan of `vertices` triangles around a centre vertex (TriangleFan) or nothing.
inline PolygonMesh MakeCylinder(int vertices = 32, double radius = 1.0, double depth = 2.0, CapFill cap = CapFill::NGon) {
  if (vertices < 3) throw std::invalid_argument("MakeCylinder: vertices must be at least 3");
  PolygonMesh m;
  const int n = vertices;
  for (int ring = 0; ring < 2; ++ring)
    for (int i = 0; i < n; ++i) {
      const double a = kTwoPi * i / n;
      m.vertices.emplace_back(radius * std::cos(a), radius * std::sin(a), ring ? depth / 2 : -depth / 2);
    }
  for (int i = 0; i < n; ++i) m.faces.push_back({i, (i + 1) % n, n + (i + 1) % n, n + i});
  if (cap == CapFill::NGon) {
    std::vector<int> bottom(n), top(n);
    for (int i = 0; i < n; ++i) {
      bottom[i] = n - 1 - i;  // seen from below, counter-clockwise
      top[i] = n + i;
    }
    m.faces.push_back(bottom);
    m.faces.push_back(top);
  } else if (cap == CapFill::TriangleFan) {
    const int cb = static_cast<int>(m.vertices.size());
    m.vertices.emplace_back(0.0, 0.0, -depth / 2);
    const int ct = cb + 1;
    m.vertices.emplace_back(0.0, 0.0, depth / 2);
    for (int i = 0; i < n; ++i) {
      m.faces.push_back({cb, (i + 1) % n, i});
      m.faces.push_back({ct, n + i, n + (i + 1) % n});
    }
  }
  return m;
}

// bpy.ops.mesh.primitive_ico_sphere_add(subdivisions, radius): the icosahedron (subdivisions = 1: 12 vertices, 30 edges, 20 triangles) with
// every triangle split into four, the new midpoints pushed out to `radius`, repeated subdivisions - 1 times.
inline PolygonMesh MakeIcoSphere(int subdivisions = 2, double radius = 1.0) {
  if (subdivisions < 1 || subdivisions > 8) throw std::invalid_argument("MakeIcoSphere: subdivisions must be in 1..8");
  PolygonMesh m;
  const double t = (1.0 + std::sqrt(5.0)) / 2.0;
  const double base[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                              {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  for (const auto& b : base) {
    Point3d p(b[0], b[1], b[2]);
    const double len = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    m.vertices.emplace_back(p.x * radius / len, p.y * radius / len, p.z * radius / len);
  }
  const int tri[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                          {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
  for (const auto& f : tri) m.faces.push_back({f[0], f[1], f[2]});
  for (int level = 1; level < subdivisions; ++level) {
    std::map<std::pair<int, int>, int> mid;
    auto midpoint = [&](int a, int b) {
      const auto key = std::make_pair(std::min(a, b), std::max(a, b));
      auto it = mid.find(key);
      if (it != mid.end()) return it->second;
      Point3d p = (m.vertices[a] + m.vertices[b]) * 0.5;  // midpoint, then pushed back out to the sphere
      const double len = ON_3dVector(p).Length();
      m.vertices.emplace_back(p.x * radius / len, p.y * radius / len, p.z * radius / len);
      const int id = static_cast<int>(m.vertices.size()) - 1;
      mid[key] = id;
      return id;
    };
    std::vector<std::vector<int>> next;
    for (const auto& f : m.faces) {
      const int a = f[0], b = f[1], c = f[2];
      const int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
      next.push_back({a, ab, ca});
      next.push_back({b, bc, ab});
      next.push_back({c, ca, bc});
      next.push_back({ab, bc, ca});
    }
    m.faces = std::move(next);
  }
  return m;
}

// bpy.ops.mesh.primitive_torus_add(major_segments, minor_segments, major_radius, minor_radius): a ring of `major_segments` circles of
// `minor_segments` vertices each; vertex (i, j) sits at ((R + r cos b) cos a, (R + r cos b) sin a, r sin b); one quad per (i, j).
inline PolygonMesh MakeTorus(int major_segments = 48, int minor_segments = 12, double major_radius = 1.0, double minor_radius = 0.25) {
  if (major_segments < 3 || minor_segments < 3) throw std::invalid_argument("MakeTorus: segments must be at least 3");
  PolygonMesh m;
  const int M = major_segments, N = minor_segments;
  for (int i = 0; i < M; ++i) {
    const double a = kTwoPi * i / M;
    for (int j = 0; j < N; ++j) {
      const double b = kTwoPi * j / N;
      const double rho = major_radius + minor_radius * std::cos(b);
      m.vertices.emplace_back(rho * std::cos(a), rho * std::sin(a), minor_radius * std::sin(b));
    }
  }
  auto id = [&](int i, int j) { return (i % M) * N + (j % N); };
  for (int i = 0; i < M; ++i)
    for (int j = 0; j < N; ++j) m.faces.push_back({id(i, j), id(i + 1, j), id(i + 1, j + 1), id(i, j + 1)});
  return m;
}

}  // namespace PolygonPrimitives

}  // namespace dino8::kernel
