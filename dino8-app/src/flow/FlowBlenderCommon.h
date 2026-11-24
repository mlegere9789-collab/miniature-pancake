// Shared helpers of the Blender Geometry Nodes flow nodes (FlowNodesBlender.cpp, FlowNodesBlender2.cpp): port builders,
// poly-spline curves, a mesh builder with n-gon support, and Blender-style element counts of a mesh.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "flow/FlowGraph.h"

namespace dino8::flow::bl {

inline PortDef In(std::string name, Kind k, Access acc = Access::Item, Value def = Value::Null(), bool optional = false) {
  PortDef p; p.name = name; p.nick = name; p.kind = k; p.access = acc; p.def = std::move(def); p.optional = optional; return p;
}
inline PortDef Out(std::string name, Kind k) { PortDef p; p.name = name; p.nick = name; p.kind = k; return p; }

constexpr double kPi = 3.14159265358979323846;
constexpr const char* kCat = "Blender Geometry";

// ------------------------------------------------------------------------------------------------------- curves

struct Poly {
  std::vector<Point3d> pts;  // without the repeated closing point
  bool closed = false;
};

inline kernel::NurbsCurve PolyCurve(const std::vector<Point3d>& pts, bool closed) {
  ON_Polyline pl;
  for (const Point3d& p : pts) pl.Append(p);
  if (closed && pl.Count() > 0) pl.Append(pl[0]);
  ON_PolylineCurve pc(pl);
  kernel::NurbsCurve c;
  ON_NurbsCurve nc;
  pc.GetNurbForm(nc);
  c.raw() = nc;
  return c;
}

// The poly-spline points of a curve value: the control points of a degree-1 curve, else 256 samples of the curve.
inline bool CurvePoly(const Value& v, Poly& out) {
  if (v.kind != Kind::Curve || !v.curve) return false;
  const ON_NurbsCurve& nc = v.curve->raw();
  out = Poly();
  if (nc.Degree() == 1 && !nc.IsRational()) {
    for (int i = 0; i < nc.CVCount(); ++i) { ON_3dPoint p; nc.GetCV(i, p); out.pts.push_back(Point3d(p.x, p.y, p.z)); }
  } else {
    const ON_Interval dm = nc.Domain();
    const int n = 256;
    for (int i = 0; i <= n; ++i) out.pts.push_back(nc.PointAt(dm.ParameterAt(static_cast<double>(i) / n)));
  }
  if (out.pts.size() >= 3 && out.pts.front().DistanceTo(out.pts.back()) < 1e-9) { out.pts.pop_back(); out.closed = true; }
  return out.pts.size() >= 2;
}

inline double PolyLength(const Poly& p) {
  double len = 0;
  for (size_t i = 0; i + 1 < p.pts.size(); ++i) len += p.pts[i].DistanceTo(p.pts[i + 1]);
  if (p.closed && p.pts.size() >= 2) len += p.pts.back().DistanceTo(p.pts.front());
  return len;
}

inline int ClampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// ------------------------------------------------------------------------------------------------------- meshes

struct MB {
  std::vector<ON_3fPoint> v;
  std::vector<std::array<int, 4>> f;           // 3 corners: f[3] == f[2]; wire edge: (a, b, b, b)
  std::vector<std::vector<int>> ngon_faces;    // face indices of each n-gon
  int V(double x, double y, double z) {
    v.push_back(ON_3fPoint(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
    return static_cast<int>(v.size()) - 1;
  }
  int Tri(int a, int b, int c) { f.push_back({a, b, c, c}); return static_cast<int>(f.size()) - 1; }
  int Quad(int a, int b, int c, int d) { f.push_back({a, b, c, d}); return static_cast<int>(f.size()) - 1; }
  void Wire(int a, int b) { f.push_back({a, b, b, b}); }
  // A convex polygon as an n-gon (fan of triangles from its first vertex, counter-clockwise seen from the front).
  void Ngon(const std::vector<int>& ring) {
    std::vector<int> faces;
    for (size_t i = 1; i + 1 < ring.size(); ++i) faces.push_back(Tri(ring[0], ring[i], ring[i + 1]));
    ngon_faces.push_back(faces);
  }
  kernel::Mesh Build() const {
    kernel::Mesh m;
    ON_Mesh& r = m.raw();
    for (const ON_3fPoint& p : v) r.m_V.Append(p);
    for (const auto& q : f) {
      ON_MeshFace face;
      for (int k = 0; k < 4; ++k) face.vi[k] = q[static_cast<size_t>(k)];
      r.m_F.Append(face);
    }
    for (const auto& g : ngon_faces) {
      ON_SimpleArray<unsigned int> fi;
      for (int x : g) fi.Append(static_cast<unsigned int>(x));
      r.AddNgon(fi);
    }
    return m;
  }
};

// Counts of a mesh the way Blender counts them: an n-gon is one face with one corner per boundary edge, a wire element
// is an edge only.
struct MeshCounts { long long points = 0, edges = 0, faces = 0, corners = 0; };

inline MeshCounts CountMesh(const ON_Mesh& m) {
  MeshCounts c;
  c.points = m.VertexCount();
  std::set<std::pair<int, int>> edges;
  auto add_edge = [&](int a, int b) { edges.insert({std::min(a, b), std::max(a, b)}); };
  const int nf = m.FaceCount();
  std::vector<bool> in_ngon(static_cast<size_t>(nf), false);
  const ON_MeshNgon* const* ngons = m.Ngons();
  for (int g = 0; g < m.NgonCount(); ++g) {
    const ON_MeshNgon* ng = ngons[g];
    std::map<std::pair<int, int>, int> uses;
    for (unsigned int k = 0; k < ng->m_Fcount; ++k) {
      const unsigned int fi = ng->m_fi[k];
      in_ngon[fi] = true;
      const ON_MeshFace& f = m.m_F[static_cast<int>(fi)];
      const int n = f.IsQuad() ? 4 : 3;
      for (int e = 0; e < n; ++e) {
        const int a = f.vi[e], b = f.vi[(e + 1) % n];
        ++uses[{std::min(a, b), std::max(a, b)}];
      }
    }
    for (const auto& u : uses) if (u.second == 1) { edges.insert(u.first); ++c.corners; }
    ++c.faces;
  }
  for (int i = 0; i < nf; ++i) {
    if (in_ngon[static_cast<size_t>(i)]) continue;
    const ON_MeshFace& f = m.m_F[i];
    if (f.vi[1] == f.vi[2] && f.vi[2] == f.vi[3]) { add_edge(f.vi[0], f.vi[1]); continue; }  // wire edge
    const int n = f.IsQuad() ? 4 : 3;
    for (int e = 0; e < n; ++e) add_edge(f.vi[e], f.vi[(e + 1) % n]);
    ++c.faces;
    c.corners += n;
  }
  c.edges = static_cast<long long>(edges.size());
  return c;
}

// Default value of the type of `like` (Blender gives the type's default for an index that selects nothing).
inline Value DefaultLike(const Value& like) {
  switch (like.kind) {
    case Kind::Number: return Value::Number(0);
    case Kind::Integer: return Value::Integer(0);
    case Kind::Boolean: return Value::Boolean(false);
    case Kind::Text: return Value::Text("");
    case Kind::Point: return Value::Point(Point3d(0, 0, 0));
    case Kind::Vector: return Value::Vector(Vector3d(0, 0, 0));
    default: return Value::Null();
  }
}

}  // namespace dino8::flow::bl
