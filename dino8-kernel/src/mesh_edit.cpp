#include "dino8/kernel/mesh_edit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>

#include "dino8/kernel/detail/polygon2d.h"

namespace dino8::kernel {
namespace {

// A face list with shared vertex ids: the working form of every operator here (any corner count; ON_Mesh only after the end).
struct Soup {
  std::vector<Point3d> p;
  std::vector<std::vector<int>> f;
};

Soup ToSoup(const Mesh& mesh) {
  Soup s;
  const ON_Mesh& r = mesh.raw();
  s.p.reserve(static_cast<size_t>(r.m_V.Count()));
  for (int i = 0; i < r.m_V.Count(); ++i) s.p.emplace_back(r.m_V[i].x, r.m_V[i].y, r.m_V[i].z);
  for (int i = 0; i < r.m_F.Count(); ++i) {
    const ON_MeshFace& face = r.m_F[i];
    if (face.IsQuad()) {
      s.f.push_back({face.vi[0], face.vi[1], face.vi[2], face.vi[3]});
    } else {
      s.f.push_back({face.vi[0], face.vi[1], face.vi[2]});
    }
  }
  return s;
}

Vector3d Newell(const Soup& s, const std::vector<int>& poly) {
  Vector3d n(0, 0, 0);
  const size_t k = poly.size();
  for (size_t i = 0; i < k; ++i) {
    const Point3d& a = s.p[static_cast<size_t>(poly[i])];
    const Point3d& b = s.p[static_cast<size_t>(poly[(i + 1) % k])];
    n.x += (a.y - b.y) * (a.z + b.z);
    n.y += (a.z - b.z) * (a.x + b.x);
    n.z += (a.x - b.x) * (a.y + b.y);
  }
  return n;  // length = twice the polygon area
}

void AddFace(ON_Mesh& m, int a, int b, int c, int d) {
  ON_MeshFace f;
  f.vi[0] = a;
  f.vi[1] = b;
  f.vi[2] = c;
  f.vi[3] = d;
  m.m_F.Append(f);
}

// Triangulates a polygon of 5 or more corners by ear clipping in its own plane; returns index triples into `poly`.
std::vector<std::array<int, 3>> TriangulatePolygon(const Soup& s, const std::vector<int>& poly) {
  Vector3d n = Newell(s, poly);
  if (!n.Unitize()) return {};
  Vector3d u = std::abs(n.x) < 0.9 ? Vector3d(1, 0, 0) : Vector3d(0, 1, 0);
  u = u - (u * n) * n;
  u.Unitize();
  const Vector3d v = ON_CrossProduct(n, u);  // u x v = n
  std::vector<Point2d> pts;
  for (int id : poly) {
    const Vector3d d = s.p[static_cast<size_t>(id)] - Point3d::Origin;
    pts.emplace_back(d * u, d * v);
  }
  return detail::EarClipTriangulate(pts);
}

Mesh FromSoup(const Soup& s) {
  std::vector<int> remap(s.p.size(), -1);
  Mesh out;
  ON_Mesh& m = out.raw();
  auto use = [&](int id) {
    if (remap[static_cast<size_t>(id)] < 0) {
      remap[static_cast<size_t>(id)] = m.m_V.Count();
      const Point3d& q = s.p[static_cast<size_t>(id)];
      m.m_V.Append(ON_3fPoint(static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z)));
    }
    return remap[static_cast<size_t>(id)];
  };
  // Vertices are numbered in order of their old index so a cut or merge does not shuffle unrelated vertices.
  std::vector<char> used(s.p.size(), 0);
  for (const auto& poly : s.f)
    for (int id : poly) used[static_cast<size_t>(id)] = 1;
  for (size_t i = 0; i < s.p.size(); ++i)
    if (used[i]) use(static_cast<int>(i));
  for (const auto& poly : s.f) {
    const size_t k = poly.size();
    if (k == 3) {
      AddFace(m, use(poly[0]), use(poly[1]), use(poly[2]), use(poly[2]));
    } else if (k == 4) {
      AddFace(m, use(poly[0]), use(poly[1]), use(poly[2]), use(poly[3]));
    } else if (k > 4) {
      for (const auto& t : TriangulatePolygon(s, poly))
        AddFace(m, use(poly[static_cast<size_t>(t[0])]), use(poly[static_cast<size_t>(t[1])]), use(poly[static_cast<size_t>(t[2])]),
                use(poly[static_cast<size_t>(t[2])]));
    }
  }
  return out;
}

MeshEdgeKey Key(int a, int b) { return {std::min(a, b), std::max(a, b)}; }

// edge -> faces using it
std::map<MeshEdgeKey, std::vector<int>> EdgeFaces(const Soup& s) {
  std::map<MeshEdgeKey, std::vector<int>> m;
  for (size_t fi = 0; fi < s.f.size(); ++fi) {
    const auto& poly = s.f[fi];
    for (size_t k = 0; k < poly.size(); ++k) m[Key(poly[k], poly[(k + 1) % poly.size()])].push_back(static_cast<int>(fi));
  }
  return m;
}

struct Dsu {
  std::vector<int> p;
  explicit Dsu(size_t n) : p(n) { std::iota(p.begin(), p.end(), 0); }
  int Find(int x) {
    while (p[static_cast<size_t>(x)] != x) {
      p[static_cast<size_t>(x)] = p[static_cast<size_t>(p[static_cast<size_t>(x)])];
      x = p[static_cast<size_t>(x)];
    }
    return x;
  }
  void Union(int a, int b) { p[static_cast<size_t>(Find(a))] = Find(b); }
};

void CheckFaceIndex(const Soup& s, int fi, const char* who) {
  if (fi < 0 || fi >= static_cast<int>(s.f.size())) throw std::invalid_argument(std::string("dino8::kernel::") + who + ": face index out of range");
}

}  // namespace

std::vector<MeshEdgeKey> SharpMeshEdges(const Mesh& mesh, double angle_degrees) {
  if (!std::isfinite(angle_degrees) || angle_degrees < 0.0) throw std::invalid_argument("dino8::kernel::SharpMeshEdges: angle must be finite and >= 0");
  const Soup s = ToSoup(mesh);
  std::vector<Vector3d> normals;
  for (const auto& poly : s.f) {
    Vector3d n = Newell(s, poly);
    n.Unitize();
    normals.push_back(n);
  }
  std::vector<MeshEdgeKey> out;
  const double limit = angle_degrees * ON_PI / 180.0;
  for (const auto& [edge, faces] : EdgeFaces(s)) {
    if (faces.size() != 2) continue;
    double c = normals[static_cast<size_t>(faces[0])] * normals[static_cast<size_t>(faces[1])];
    c = std::max(-1.0, std::min(1.0, c));
    if (std::acos(c) > limit + 1e-9) out.push_back(edge);
  }
  return out;
}

Mesh SplitMeshEdges(const Mesh& mesh, const std::vector<MeshEdgeKey>& edges) {
  Soup s = ToSoup(mesh);
  std::set<MeshEdgeKey> split;
  for (const auto& e : edges) split.insert(Key(e.first, e.second));
  const auto ef = EdgeFaces(s);
  // faces around each vertex, grouped by connectivity across edges that stay
  std::vector<std::vector<int>> around(s.p.size());
  for (size_t fi = 0; fi < s.f.size(); ++fi)
    for (int v : s.f[fi]) around[static_cast<size_t>(v)].push_back(static_cast<int>(fi));
  std::map<std::pair<int, int>, int> corner_vertex;  // (vertex, face) -> new vertex id
  std::vector<Point3d> pos = s.p;
  for (size_t v = 0; v < s.p.size(); ++v) {
    const auto& faces = around[v];
    if (faces.empty()) continue;
    Dsu dsu(s.f.size());
    for (const auto& [edge, fl] : ef) {
      if (edge.first != static_cast<int>(v) && edge.second != static_cast<int>(v)) continue;
      if (split.count(edge) != 0) continue;
      for (size_t i = 1; i < fl.size(); ++i) dsu.Union(fl[0], fl[i]);
    }
    std::map<int, int> group_vertex;
    for (int fi : faces) {
      const int root = dsu.Find(fi);
      auto it = group_vertex.find(root);
      if (it == group_vertex.end()) {
        int id = static_cast<int>(v);
        if (!group_vertex.empty()) {
          id = static_cast<int>(pos.size());
          pos.push_back(s.p[v]);
        }
        it = group_vertex.emplace(root, id).first;
      }
      corner_vertex[{static_cast<int>(v), fi}] = it->second;
    }
  }
  for (size_t fi = 0; fi < s.f.size(); ++fi)
    for (int& v : s.f[fi]) v = corner_vertex.at({v, static_cast<int>(fi)});
  s.p = pos;
  return FromSoup(s);
}

Mesh EdgeSplitByAngle(const Mesh& mesh, double angle_degrees) { return SplitMeshEdges(mesh, SharpMeshEdges(mesh, angle_degrees)); }

Mesh SplitOffFaces(const Mesh& mesh, const std::vector<int>& faces) {
  const Soup s = ToSoup(mesh);
  std::set<int> sel;
  for (int fi : faces) {
    CheckFaceIndex(s, fi, "SplitOffFaces");
    sel.insert(fi);
  }
  std::vector<MeshEdgeKey> edges;
  for (const auto& [edge, fl] : EdgeFaces(s)) {
    bool in = false, out = false;
    for (int fi : fl) (sel.count(fi) != 0 ? in : out) = true;
    if (in && out) edges.push_back(edge);
  }
  return SplitMeshEdges(mesh, edges);
}

Mesh MergeVerticesAt(const Mesh& mesh, const std::vector<int>& vertices, MergeTarget target, Point3d point) {
  Soup s = ToSoup(mesh);
  if (vertices.empty()) throw std::invalid_argument("dino8::kernel::MergeVerticesAt: no vertices");
  for (int v : vertices)
    if (v < 0 || v >= static_cast<int>(s.p.size())) throw std::invalid_argument("dino8::kernel::MergeVerticesAt: vertex index out of range");
  const int keep = vertices.front();
  Point3d at = point;
  if (target == MergeTarget::Center) {
    Vector3d sum(0, 0, 0);
    for (int v : vertices) sum += s.p[static_cast<size_t>(v)] - Point3d::Origin;
    sum /= static_cast<double>(vertices.size());
    at = Point3d::Origin + sum;
  } else if (target == MergeTarget::First) {
    at = s.p[static_cast<size_t>(vertices.front())];
  } else if (target == MergeTarget::Last) {
    at = s.p[static_cast<size_t>(vertices.back())];
  }
  const std::set<int> merged(vertices.begin(), vertices.end());
  s.p[static_cast<size_t>(keep)] = at;
  std::vector<std::vector<int>> faces;
  for (auto poly : s.f) {
    for (int& v : poly)
      if (merged.count(v) != 0) v = keep;
    std::vector<int> cleaned;
    for (int v : poly)
      if (cleaned.empty() || cleaned.back() != v) cleaned.push_back(v);
    while (cleaned.size() > 1 && cleaned.front() == cleaned.back()) cleaned.pop_back();
    // a corner repeated apart from its neighbours would pinch the face; only a face with all corners distinct stays
    if (cleaned.size() >= 3 && std::set<int>(cleaned.begin(), cleaned.end()).size() == cleaned.size()) faces.push_back(cleaned);
  }
  s.f = faces;
  return FromSoup(s);
}

Mesh PokeFaces(const Mesh& mesh, const std::vector<int>& faces, double offset, PokeCenter center) {
  if (!std::isfinite(offset)) throw std::invalid_argument("dino8::kernel::PokeFaces: offset must be finite");
  Soup s = ToSoup(mesh);
  std::set<int> sel;
  for (int fi : faces) {
    CheckFaceIndex(s, fi, "PokeFaces");
    sel.insert(fi);
  }
  std::vector<std::vector<int>> out;
  for (size_t fi = 0; fi < s.f.size(); ++fi) {
    if (sel.count(static_cast<int>(fi)) == 0) {
      out.push_back(s.f[fi]);
      continue;
    }
    const auto poly = s.f[fi];
    const size_t k = poly.size();
    Vector3d n = Newell(s, poly);
    const double twice_area = n.Length();
    n.Unitize();
    Point3d c = Point3d::Origin;
    if (center == PokeCenter::Median) {
      Vector3d sum(0, 0, 0);
      for (int v : poly) sum += s.p[static_cast<size_t>(v)] - Point3d::Origin;
      c = Point3d::Origin + sum / static_cast<double>(k);
    } else if (center == PokeCenter::Bounds) {
      Point3d lo = s.p[static_cast<size_t>(poly[0])], hi = lo;
      for (int v : poly) {
        const Point3d& q = s.p[static_cast<size_t>(v)];
        lo = Point3d(std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z));
        hi = Point3d(std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z));
      }
      c = Point3d((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2);
    } else {
      // area centroid of the polygon region: fan triangles from corner 0, each weighted by its area
      Vector3d acc(0, 0, 0);
      double total = 0.0;
      const Point3d& p0 = s.p[static_cast<size_t>(poly[0])];
      for (size_t i = 1; i + 1 < k; ++i) {
        const Point3d& a = s.p[static_cast<size_t>(poly[i])];
        const Point3d& b = s.p[static_cast<size_t>(poly[i + 1])];
        const double w = (ON_CrossProduct(a - p0, b - p0) * n);  // signed twice-area along the face normal
        acc += w * ((p0 - Point3d::Origin) + (a - Point3d::Origin) + (b - Point3d::Origin)) / 3.0;
        total += w;
      }
      c = (total != 0.0 && twice_area > 0.0) ? Point3d::Origin + acc / total : s.p[static_cast<size_t>(poly[0])];
    }
    c = c + n * offset;
    const int apex = static_cast<int>(s.p.size());
    s.p.push_back(c);
    for (size_t i = 0; i < k; ++i) out.push_back({poly[i], poly[(i + 1) % k], apex});
  }
  s.f = out;
  return FromSoup(s);
}

Mesh ConnectFaceVertices(const Mesh& mesh, int face, int vertex_a, int vertex_b) {
  Soup s = ToSoup(mesh);
  CheckFaceIndex(s, face, "ConnectFaceVertices");
  const auto poly = s.f[static_cast<size_t>(face)];
  const int k = static_cast<int>(poly.size());
  const int ia = static_cast<int>(std::find(poly.begin(), poly.end(), vertex_a) - poly.begin());
  const int ib = static_cast<int>(std::find(poly.begin(), poly.end(), vertex_b) - poly.begin());
  if (ia >= k || ib >= k || ia == ib) throw std::invalid_argument("dino8::kernel::ConnectFaceVertices: the face does not have both corners");
  const int gap = (ib - ia + k) % k;
  if (gap == 1 || gap == k - 1) throw std::invalid_argument("dino8::kernel::ConnectFaceVertices: the corners are neighbours");
  std::vector<int> first, second;
  for (int i = ia;; i = (i + 1) % k) {
    first.push_back(poly[static_cast<size_t>(i)]);
    if (i == ib) break;
  }
  for (int i = ib;; i = (i + 1) % k) {
    second.push_back(poly[static_cast<size_t>(i)]);
    if (i == ia) break;
  }
  s.f[static_cast<size_t>(face)] = first;
  s.f.push_back(second);
  return FromSoup(s);
}

Mesh SmoothMeshVertices(const Mesh& mesh, double factor, int repeat, bool x, bool y, bool z, const std::vector<int>& only_vertices) {
  if (!std::isfinite(factor) || factor < 0.0 || factor > 1.0) throw std::invalid_argument("dino8::kernel::SmoothMeshVertices: factor must be in [0, 1]");
  if (repeat < 0) throw std::invalid_argument("dino8::kernel::SmoothMeshVertices: repeat must be >= 0");
  Soup s = ToSoup(mesh);
  std::vector<std::set<int>> nb(s.p.size());
  for (const auto& poly : s.f)
    for (size_t k = 0; k < poly.size(); ++k) {
      const int a = poly[k], b = poly[(k + 1) % poly.size()];
      nb[static_cast<size_t>(a)].insert(b);
      nb[static_cast<size_t>(b)].insert(a);
    }
  std::vector<char> active(s.p.size(), only_vertices.empty() ? 1 : 0);
  for (int v : only_vertices)
    if (v >= 0 && v < static_cast<int>(s.p.size())) active[static_cast<size_t>(v)] = 1;
  for (int round = 0; round < repeat; ++round) {
    std::vector<Point3d> next = s.p;
    for (size_t v = 0; v < s.p.size(); ++v) {
      if (!active[v] || nb[v].empty()) continue;
      Vector3d mean(0, 0, 0);
      for (int w : nb[v]) mean += s.p[static_cast<size_t>(w)] - Point3d::Origin;
      mean /= static_cast<double>(nb[v].size());
      const Vector3d move = factor * (mean - (s.p[v] - Point3d::Origin));
      next[v] = Point3d(s.p[v].x + (x ? move.x : 0.0), s.p[v].y + (y ? move.y : 0.0), s.p[v].z + (z ? move.z : 0.0));
    }
    s.p = next;
  }
  return FromSoup(s);
}

Mesh BisectMesh(const Mesh& mesh, Point3d plane_point, Vector3d plane_normal, bool fill, bool clear_inner, bool clear_outer, double plane_tolerance) {
  if (!plane_normal.Unitize()) throw std::invalid_argument("dino8::kernel::BisectMesh: the plane normal is zero");
  const Soup in = ToSoup(mesh);
  Soup out;
  out.p = in.p;
  std::vector<double> dist(in.p.size());
  std::vector<int> side(in.p.size());
  for (size_t i = 0; i < in.p.size(); ++i) {
    dist[i] = (in.p[i] - plane_point) * plane_normal;
    side[i] = dist[i] > plane_tolerance ? 1 : (dist[i] < -plane_tolerance ? -1 : 0);
  }
  std::map<MeshEdgeKey, int> cut_vertex;
  auto cut = [&](int a, int b) {
    const MeshEdgeKey key = Key(a, b);
    auto it = cut_vertex.find(key);
    if (it != cut_vertex.end()) return it->second;
    const int lo = key.first, hi = key.second;  // interpolate from the lower index so both faces compute the same point
    const double t = dist[static_cast<size_t>(lo)] / (dist[static_cast<size_t>(lo)] - dist[static_cast<size_t>(hi)]);
    const Point3d q = in.p[static_cast<size_t>(lo)] + t * (in.p[static_cast<size_t>(hi)] - in.p[static_cast<size_t>(lo)]);
    const int id = static_cast<int>(out.p.size());
    out.p.push_back(q);
    cut_vertex[key] = id;
    return id;
  };
  std::vector<std::vector<int>> pos_faces, neg_faces;
  // Directed cut edges as each part walks them (a -> b along the boundary the part gets on the plane).
  std::map<int, int> pos_cap_next, neg_cap_next;
  const int original_vertices = static_cast<int>(in.p.size());
  auto on_plane = [&](int v) { return v >= original_vertices || side[static_cast<size_t>(v)] == 0; };
  for (const auto& poly : in.f) {
    bool any_pos = false, any_neg = false;
    for (int v : poly) {
      any_pos |= side[static_cast<size_t>(v)] > 0;
      any_neg |= side[static_cast<size_t>(v)] < 0;
    }
    const size_t k = poly.size();
    if (!any_pos && !any_neg) {
      pos_faces.push_back(poly);  // lying in the plane: goes with the positive side and adds no cut edge
      continue;
    }
    if (!any_neg || !any_pos) {
      (any_neg ? neg_faces : pos_faces).push_back(poly);
      // an edge of the face that lies in the plane is a boundary edge of that part on the plane
      auto& next = any_neg ? neg_cap_next : pos_cap_next;
      for (size_t i = 0; i < k; ++i) {
        const int a = poly[i], b = poly[(i + 1) % k];
        if (side[static_cast<size_t>(a)] == 0 && side[static_cast<size_t>(b)] == 0) next[a] = b;
      }
      continue;
    }
    std::vector<int> pos, neg;
    std::set<MeshEdgeKey> own_edges;
    for (size_t i = 0; i < k; ++i) own_edges.insert(Key(poly[i], poly[(i + 1) % k]));
    for (size_t i = 0; i < k; ++i) {
      const int a = poly[i], b = poly[(i + 1) % k];
      const int sa = side[static_cast<size_t>(a)], sb = side[static_cast<size_t>(b)];
      if (sa >= 0) pos.push_back(a);
      if (sa <= 0) neg.push_back(a);
      if ((sa > 0 && sb < 0) || (sa < 0 && sb > 0)) {
        const int c = cut(a, b);
        pos.push_back(c);
        neg.push_back(c);
      }
    }
    // The cut edge is the one pair of neighbouring plane points of a part that is not an edge of the original face.
    for (size_t i = 0; i < pos.size(); ++i) {
      const int a = pos[i], b = pos[(i + 1) % pos.size()];
      if (on_plane(a) && on_plane(b) && own_edges.count(Key(a, b)) == 0) pos_cap_next[a] = b;
    }
    for (size_t i = 0; i < neg.size(); ++i) {
      const int a = neg[i], b = neg[(i + 1) % neg.size()];
      if (on_plane(a) && on_plane(b) && own_edges.count(Key(a, b)) == 0) neg_cap_next[a] = b;
    }
    if (pos.size() >= 3) pos_faces.push_back(pos);
    if (neg.size() >= 3) neg_faces.push_back(neg);
  }
  Soup result;
  result.p = out.p;
  auto keep_pos = !clear_outer;
  auto keep_neg = !clear_inner;
  if (keep_pos) result.f.insert(result.f.end(), pos_faces.begin(), pos_faces.end());
  if (keep_neg) result.f.insert(result.f.end(), neg_faces.begin(), neg_faces.end());
  if (fill) {
    // The cap of a part runs opposite to the cut edges of that part, so each cut edge is used once in each direction.
    auto cap = [&](const std::map<int, int>& next) {
      std::map<int, int> reverse;
      for (const auto& [a, b] : next) reverse[b] = a;
      std::set<int> seen;
      std::vector<std::vector<int>> loops;
      for (const auto& [start, unused] : reverse) {
        (void)unused;
        if (seen.count(start) != 0) continue;
        std::vector<int> loop;
        int cur = start;
        bool closed = false;
        while (seen.count(cur) == 0) {
          seen.insert(cur);
          loop.push_back(cur);
          auto it = reverse.find(cur);
          if (it == reverse.end()) break;
          cur = it->second;
          if (cur == start) {
            closed = true;
            break;
          }
        }
        if (closed && loop.size() >= 3) loops.push_back(loop);
      }
      return loops;
    };
    if (keep_pos)
      for (const auto& loop : cap(pos_cap_next)) result.f.push_back(loop);
    if (keep_neg)
      for (const auto& loop : cap(neg_cap_next)) result.f.push_back(loop);
  }
  return FromSoup(result);
}

Mesh ScrewPolyline(const std::vector<Point3d>& profile, Point3d axis_point, Vector3d axis_direction, double angle_radians, int steps,
                   double screw_offset, int iterations, bool close_profile) {
  if (profile.size() < 2) throw std::invalid_argument("dino8::kernel::ScrewPolyline: the profile needs at least two points");
  if (steps < 1 || iterations < 1) throw std::invalid_argument("dino8::kernel::ScrewPolyline: steps and iterations must be >= 1");
  if (!std::isfinite(angle_radians) || !std::isfinite(screw_offset)) throw std::invalid_argument("dino8::kernel::ScrewPolyline: angle and offset must be finite");
  if (!axis_direction.Unitize()) throw std::invalid_argument("dino8::kernel::ScrewPolyline: the axis is zero");
  const int total = steps * iterations;
  const bool ring = iterations == 1 && screw_offset == 0.0 && std::abs(std::abs(angle_radians) - 2.0 * ON_PI) < 1e-12;
  const int copies = ring ? total : total + 1;
  const int n = static_cast<int>(profile.size());
  Soup s;
  for (int c = 0; c < copies; ++c) {
    const double theta = angle_radians / steps * c;
    const double lift = screw_offset / steps * c;
    ON_Xform rot;
    rot.Rotation(theta, axis_direction, axis_point);
    for (const Point3d& q : profile) {
      Point3d r = q;
      r.Transform(rot);
      s.p.push_back(r + axis_direction * lift);
    }
  }
  const int segs = close_profile ? n : n - 1;
  for (int c = 0; c < total; ++c) {
    const int c0 = c, c1 = (c + 1) % copies;
    for (int i = 0; i < segs; ++i) {
      const int i0 = i, i1 = (i + 1) % n;
      // (profile i at step c, profile i at step c+1, profile i+1 at step c+1, profile i+1 at step c): the turn direction
      // then profile direction gives the normal  d(turn) x d(profile)
      s.f.push_back({c0 * n + i0, c1 * n + i0, c1 * n + i1, c0 * n + i1});
    }
  }
  return FromSoup(s);
}

Mesh MaskMeshByVertices(const Mesh& mesh, const std::vector<bool>& keep_vertex, bool invert) {
  Soup s = ToSoup(mesh);
  if (keep_vertex.size() != s.p.size()) throw std::invalid_argument("dino8::kernel::MaskMeshByVertices: one flag per vertex is required");
  std::vector<std::vector<int>> faces;
  for (const auto& poly : s.f) {
    bool all = true;
    for (int v : poly) all = all && (keep_vertex[static_cast<size_t>(v)] != invert);
    if (all) faces.push_back(poly);
  }
  s.f = faces;
  return FromSoup(s);
}

Mesh BuildFaces(const Mesh& mesh, double frame, double start, double length, bool reversed) {
  if (!(length > 0.0) || !std::isfinite(frame) || !std::isfinite(start)) throw std::invalid_argument("dino8::kernel::BuildFaces: length must be > 0");
  Soup s = ToSoup(mesh);
  double f = std::max(0.0, std::min(1.0, (frame - start) / length));
  if (reversed) f = 1.0 - f;
  const size_t keep = std::min(s.f.size(), static_cast<size_t>(std::floor(static_cast<double>(s.f.size()) * f + 1e-9)));
  s.f.resize(keep);
  return FromSoup(s);
}

Mesh ExtrudeIndividualFaces(const Mesh& mesh, const std::vector<int>& faces, double distance) {
  Soup s = ToSoup(mesh);
  std::set<int> sel;
  for (int fi : faces) {
    CheckFaceIndex(s, fi, "ExtrudeIndividualFaces");
    sel.insert(fi);
  }
  const size_t original_faces = s.f.size();
  for (size_t fi = 0; fi < original_faces; ++fi) {
    if (sel.count(static_cast<int>(fi)) == 0) continue;
    const auto poly = s.f[fi];
    Vector3d n = Newell(s, poly);
    n.Unitize();
    std::vector<int> moved;
    for (int v : poly) {
      moved.push_back(static_cast<int>(s.p.size()));
      s.p.push_back(s.p[static_cast<size_t>(v)] + n * distance);
    }
    const size_t k = poly.size();
    for (size_t i = 0; i < k; ++i) s.f.push_back({poly[i], poly[(i + 1) % k], moved[(i + 1) % k], moved[i]});
    s.f[fi] = moved;
  }
  return FromSoup(s);
}

Mesh ExtrudeMeshEdges(const Mesh& mesh, const std::vector<MeshEdgeKey>& edges, Vector3d offset) {
  Soup s = ToSoup(mesh);
  const auto ef = EdgeFaces(s);
  std::map<int, int> copy;
  auto dup = [&](int v) {
    auto it = copy.find(v);
    if (it != copy.end()) return it->second;
    const int id = static_cast<int>(s.p.size());
    s.p.push_back(s.p[static_cast<size_t>(v)] + offset);
    copy[v] = id;
    return id;
  };
  for (const auto& e : edges) {
    const MeshEdgeKey k = Key(e.first, e.second);
    if (ef.find(k) == ef.end()) throw std::invalid_argument("dino8::kernel::ExtrudeMeshEdges: the edge is not in the mesh");
    s.f.push_back({k.first, k.second, dup(k.second), dup(k.first)});
  }
  return FromSoup(s);
}

Mesh DissolveFaces(const Mesh& mesh, const std::vector<int>& faces) {
  Soup s = ToSoup(mesh);
  std::set<int> sel;
  for (int fi : faces) {
    CheckFaceIndex(s, fi, "DissolveFaces");
    sel.insert(fi);
  }
  const auto ef = EdgeFaces(s);
  Dsu dsu(s.f.size());
  for (const auto& [edge, fl] : ef) {
    (void)edge;
    int first = -1;
    for (int fi : fl) {
      if (sel.count(fi) == 0) continue;
      if (first < 0) first = fi; else dsu.Union(first, fi);
    }
  }
  std::map<int, std::vector<int>> groups;
  for (int fi : sel) groups[dsu.Find(fi)].push_back(fi);
  std::vector<char> dropped(s.f.size(), 0);
  std::vector<std::vector<int>> added;
  for (const auto& [root, members] : groups) {
    (void)root;
    if (members.size() < 2) continue;
    // directed outline edges: an edge of a member face whose reverse is not an edge of another member
    std::set<std::pair<int, int>> directed;
    for (int fi : members) {
      const auto& poly = s.f[static_cast<size_t>(fi)];
      for (size_t k = 0; k < poly.size(); ++k) directed.insert({poly[k], poly[(k + 1) % poly.size()]});
    }
    std::map<int, int> next;
    bool simple = true;
    for (const auto& d : directed) {
      if (directed.count({d.second, d.first}) != 0) continue;
      if (!next.emplace(d.first, d.second).second) simple = false;
    }
    std::vector<int> loop;
    if (simple && !next.empty()) {
      const int start = next.begin()->first;
      int cur = start;
      do {
        loop.push_back(cur);
        auto it = next.find(cur);
        if (it == next.end()) { simple = false; break; }
        cur = it->second;
      } while (cur != start && loop.size() <= next.size());
      simple = simple && loop.size() == next.size();
    } else {
      simple = false;
    }
    if (!simple) throw std::invalid_argument("dino8::kernel::DissolveFaces: the outline of the faces is not one simple loop");
    for (int fi : members) dropped[static_cast<size_t>(fi)] = 1;
    added.push_back(loop);
  }
  std::vector<std::vector<int>> out;
  for (size_t fi = 0; fi < s.f.size(); ++fi)
    if (!dropped[fi]) out.push_back(s.f[fi]);
  for (const auto& poly : added) out.push_back(poly);
  s.f = out;
  return FromSoup(s);
}

Mesh MakeFace(const Mesh& mesh, const std::vector<int>& vertices) {
  Soup s = ToSoup(mesh);
  if (vertices.size() < 3) throw std::invalid_argument("dino8::kernel::MakeFace: at least three vertices are needed");
  if (std::set<int>(vertices.begin(), vertices.end()).size() != vertices.size()) throw std::invalid_argument("dino8::kernel::MakeFace: a vertex is repeated");
  for (int v : vertices)
    if (v < 0 || v >= static_cast<int>(s.p.size())) throw std::invalid_argument("dino8::kernel::MakeFace: vertex index out of range");
  std::set<std::pair<int, int>> directed;
  for (const auto& poly : s.f)
    for (size_t k = 0; k < poly.size(); ++k) directed.insert({poly[k], poly[(k + 1) % poly.size()]});
  int same = 0;
  const size_t n = vertices.size();
  for (size_t k = 0; k < n; ++k)
    if (directed.count({vertices[k], vertices[(k + 1) % n]}) != 0) ++same;
  std::vector<int> loop = vertices;
  if (same > 0) std::reverse(loop.begin(), loop.end());  // an existing face already walks this way: wind the new one against it
  s.f.push_back(loop);
  return FromSoup(s);
}

namespace {
std::vector<int> StepSelection(const Mesh& mesh, const std::vector<int>& selected, bool face_step, bool grow) {
  const Soup s = ToSoup(mesh);
  std::vector<char> in(s.p.size(), 0);
  for (int v : selected) {
    if (v < 0 || v >= static_cast<int>(s.p.size())) throw std::invalid_argument("dino8::kernel: selected vertex index out of range");
    in[static_cast<size_t>(v)] = 1;
  }
  std::vector<char> out = in;
  auto visit = [&](const std::vector<int>& group) {
    // a group is a face (face step) or an edge (no face step): its vertices see each other
    bool any_in = false, any_out = false;
    for (int v : group) (in[static_cast<size_t>(v)] ? any_in : any_out) = true;
    if (grow && any_in)
      for (int v : group) out[static_cast<size_t>(v)] = 1;
    if (!grow && any_out)
      for (int v : group) out[static_cast<size_t>(v)] = 0;
  };
  if (face_step) {
    for (const auto& poly : s.f) visit(poly);
  } else {
    for (const auto& [edge, fl] : EdgeFaces(s)) {
      (void)fl;
      visit({edge.first, edge.second});
    }
  }
  std::vector<int> result;
  for (size_t v = 0; v < out.size(); ++v)
    if (out[v]) result.push_back(static_cast<int>(v));
  return result;
}
}  // namespace

std::vector<int> GrowVertexSelection(const Mesh& mesh, const std::vector<int>& selected, bool face_step) {
  return StepSelection(mesh, selected, face_step, true);
}

std::vector<int> ShrinkVertexSelection(const Mesh& mesh, const std::vector<int>& selected, bool face_step) {
  return StepSelection(mesh, selected, face_step, false);
}

std::vector<MeshEdgeKey> NonManifoldMeshEdges(const Mesh& mesh, bool boundary, bool multiple_faces, bool non_contiguous) {
  const Soup s = ToSoup(mesh);
  std::map<MeshEdgeKey, std::vector<std::pair<int, int>>> uses;  // directed uses of each edge
  for (const auto& poly : s.f)
    for (size_t k = 0; k < poly.size(); ++k) {
      const int a = poly[k], b = poly[(k + 1) % poly.size()];
      uses[Key(a, b)].push_back({a, b});
    }
  std::vector<MeshEdgeKey> out;
  for (const auto& [edge, u] : uses) {
    if (u.size() == 1 && boundary) out.push_back(edge);
    else if (u.size() >= 3 && multiple_faces) out.push_back(edge);
    else if (u.size() == 2 && non_contiguous && u[0] == u[1]) out.push_back(edge);
  }
  return out;
}

std::vector<int> FlatLinkedFaces(const Mesh& mesh, int seed_face, double angle_degrees) {
  const Soup s = ToSoup(mesh);
  CheckFaceIndex(s, seed_face, "FlatLinkedFaces");
  if (!std::isfinite(angle_degrees) || angle_degrees < 0.0) throw std::invalid_argument("dino8::kernel::FlatLinkedFaces: angle must be finite and >= 0");
  std::vector<Vector3d> normals;
  for (const auto& poly : s.f) {
    Vector3d n = Newell(s, poly);
    n.Unitize();
    normals.push_back(n);
  }
  const double limit = angle_degrees * ON_PI / 180.0;
  const auto ef = EdgeFaces(s);
  std::vector<char> seen(s.f.size(), 0);
  std::vector<int> stack{seed_face};
  seen[static_cast<size_t>(seed_face)] = 1;
  while (!stack.empty()) {
    const int fi = stack.back();
    stack.pop_back();
    const auto& poly = s.f[static_cast<size_t>(fi)];
    for (size_t k = 0; k < poly.size(); ++k) {
      const auto& fl = ef.at(Key(poly[k], poly[(k + 1) % poly.size()]));
      if (fl.size() != 2) continue;
      const int other = fl[0] == fi ? fl[1] : fl[0];
      if (seen[static_cast<size_t>(other)]) continue;
      double c = normals[static_cast<size_t>(fi)] * normals[static_cast<size_t>(other)];
      c = std::max(-1.0, std::min(1.0, c));
      if (std::acos(c) <= limit + 1e-9) {
        seen[static_cast<size_t>(other)] = 1;
        stack.push_back(other);
      }
    }
  }
  std::vector<int> out;
  for (size_t i = 0; i < seen.size(); ++i)
    if (seen[i]) out.push_back(static_cast<int>(i));
  return out;
}

std::vector<int> ShortestEdgePath(const Mesh& mesh, int from, int to, bool topology_distance) {
  const Soup s = ToSoup(mesh);
  const int n = static_cast<int>(s.p.size());
  if (from < 0 || from >= n || to < 0 || to >= n) throw std::invalid_argument("dino8::kernel::ShortestEdgePath: vertex index out of range");
  std::vector<std::vector<int>> nb(static_cast<size_t>(n));
  for (const auto& [edge, fl] : EdgeFaces(s)) {
    (void)fl;
    nb[static_cast<size_t>(edge.first)].push_back(edge.second);
    nb[static_cast<size_t>(edge.second)].push_back(edge.first);
  }
  const double inf = std::numeric_limits<double>::infinity();
  std::vector<double> dist(static_cast<size_t>(n), inf);
  std::vector<int> prev(static_cast<size_t>(n), -1);
  std::set<std::pair<double, int>> queue;
  dist[static_cast<size_t>(from)] = 0.0;
  queue.insert({0.0, from});
  while (!queue.empty()) {
    const auto [d, v] = *queue.begin();
    queue.erase(queue.begin());
    if (v == to) break;
    for (int w : nb[static_cast<size_t>(v)]) {
      const double step = topology_distance ? 1.0 : s.p[static_cast<size_t>(v)].DistanceTo(s.p[static_cast<size_t>(w)]);
      if (d + step < dist[static_cast<size_t>(w)] - 1e-12) {
        queue.erase({dist[static_cast<size_t>(w)], w});
        dist[static_cast<size_t>(w)] = d + step;
        prev[static_cast<size_t>(w)] = v;
        queue.insert({dist[static_cast<size_t>(w)], w});
      }
    }
  }
  std::vector<int> path;
  if (dist[static_cast<size_t>(to)] == inf) return path;
  for (int v = to; v >= 0; v = prev[static_cast<size_t>(v)]) path.push_back(v);
  std::reverse(path.begin(), path.end());
  return path;
}

MeshEdgeLoop EdgeLoopThrough(const Mesh& mesh, int a, int b) {
  const Soup s = ToSoup(mesh);
  const auto ef = EdgeFaces(s);
  if (ef.find(Key(a, b)) == ef.end()) throw std::invalid_argument("dino8::kernel::EdgeLoopThrough: the edge is not in the mesh");
  std::map<int, std::vector<int>> nb;
  for (const auto& [edge, fl] : ef) {
    (void)fl;
    nb[edge.first].push_back(edge.second);
    nb[edge.second].push_back(edge.first);
  }
  auto share_face = [&](int u, int v, int w) {  // do edges (u, v) and (v, w) lie in one face?
    for (int fi : ef.at(Key(u, v)))
      for (int gj : ef.at(Key(v, w)))
        if (fi == gj) return true;
    return false;
  };
  // the vertex that follows `v` when the walk arrives from `u`, or -1
  auto next_vertex = [&](int u, int v) {
    const auto& around = nb.at(v);
    if (around.size() == 4) {
      int found = -1, count = 0;
      for (int w : around) {
        if (w == u || share_face(u, v, w)) continue;
        found = w;
        ++count;
      }
      return count == 1 ? found : -1;
    }
    if (ef.at(Key(u, v)).size() == 1) {  // walking along the boundary
      int found = -1, count = 0;
      for (int w : around) {
        if (w == u || ef.at(Key(v, w)).size() != 1) continue;
        found = w;
        ++count;
      }
      return count == 1 ? found : -1;
    }
    return -1;
  };
  MeshEdgeLoop loop;
  std::deque<int> seq{a, b};
  // forward from b
  bool closed = false;
  for (;;) {
    const int u = seq[seq.size() - 2], v = seq.back();
    const int w = next_vertex(u, v);
    if (w < 0) break;
    if (w == seq.front() && seq.size() >= 3) {
      closed = true;
      break;
    }
    if (std::find(seq.begin(), seq.end(), w) != seq.end()) break;
    seq.push_back(w);
  }
  if (!closed) {
    for (;;) {
      const int u = seq[1], v = seq.front();
      const int w = next_vertex(u, v);
      if (w < 0) break;
      if (std::find(seq.begin(), seq.end(), w) != seq.end()) break;
      seq.push_front(w);
    }
  }
  loop.vertices.assign(seq.begin(), seq.end());
  loop.closed = closed;
  return loop;
}

}  // namespace dino8::kernel
