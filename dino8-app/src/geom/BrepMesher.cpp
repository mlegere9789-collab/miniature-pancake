#include "geom/BrepMesher.h"

#include <opennurbs.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"

namespace dino8::app {

namespace {

using kernel::Point3d;

// ---------------------------------------------------------------------------
// Constrained Delaunay triangulation in 2D (Bowyer-Watson + constraint
// recovery by edge flips + midpoint refinement that never touches a
// constrained edge).
// ---------------------------------------------------------------------------

struct Vec2 {
  double x = 0, y = 0;
};

struct Tri {
  int v[3] = {-1, -1, -1};
  int n[3] = {-1, -1, -1};  // neighbour across edge i (v[i] -> v[(i+1)%3])
  bool alive = true;
};

class CDT {
 public:
  explicit CDT(const std::vector<Vec2>& points) : pts_(points) {
    // Super triangle around everything.
    double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
    for (const Vec2& p : pts_) {
      minx = std::min(minx, p.x); miny = std::min(miny, p.y);
      maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y);
    }
    const double d = std::max(maxx - minx, maxy - miny) * 20 + 1;
    const double cx = (minx + maxx) / 2, cy = (miny + maxy) / 2;
    n_input_ = static_cast<int>(pts_.size());
    pts_.push_back({cx - d, cy - d});
    pts_.push_back({cx + d, cy - d});
    pts_.push_back({cx, cy + d});
    Tri t;
    t.v[0] = n_input_; t.v[1] = n_input_ + 1; t.v[2] = n_input_ + 2;
    tris_.push_back(t);
    for (int i = 0; i < n_input_; ++i) Insert(i);
  }

  // Marks segment a-b as constrained, flipping edges until it exists.
  bool Constrain(int a, int b) {
    if (a == b) return false;
    constrained_.insert(Key(a, b));
    for (int iter = 0; iter < 10000; ++iter) {
      if (FindEdge(a, b) >= 0) return true;
      // Find a triangle edge crossing a-b and flip it.
      int t = FindTriangleWithVertex(a);
      if (t < 0) return false;
      bool flipped = false;
      // Walk all triangles: fine for face-sized problems.
      for (size_t ti = 0; ti < tris_.size() && !flipped; ++ti) {
        Tri& tr = tris_[ti];
        if (!tr.alive) continue;
        for (int e = 0; e < 3; ++e) {
          const int p = tr.v[e], q = tr.v[(e + 1) % 3];
          if (p == a || p == b || q == a || q == b) continue;
          if (constrained_.count(Key(p, q))) continue;
          if (!SegmentsCross(pts_[a], pts_[b], pts_[p], pts_[q])) continue;
          if (tr.n[e] < 0) continue;
          if (FlipEdge(static_cast<int>(ti), e)) { flipped = true; break; }
        }
      }
      if (!flipped) return false;
    }
    return false;
  }

  // Splits interior (unconstrained) edges longer than `max_len` measured in
  // the anisotropic metric (dx/hx, dy/hy).
  void Refine(double hx, double hy, int max_tris, const std::function<bool(const Vec2&)>& inside) {
    for (int round = 0; round < 80; ++round) {
      if (AliveCount() >= max_tris) return;
      // Collect long interior edges with the triangle that owns them.
      struct LongEdge { double len; int tri, e; };
      std::vector<LongEdge> longest;
      for (size_t ti = 0; ti < tris_.size(); ++ti) {
        const Tri& tr = tris_[ti];
        if (!tr.alive || IsSuper(tr)) continue;
        for (int e = 0; e < 3; ++e) {
          const int p = tr.v[e], q = tr.v[(e + 1) % 3];
          if (p > q) continue;  // each edge once
          if (constrained_.count(Key(p, q))) continue;
          const double dx = (pts_[p].x - pts_[q].x) / hx, dy = (pts_[p].y - pts_[q].y) / hy;
          const double len = std::sqrt(dx * dx + dy * dy);
          if (len > 1.4) longest.push_back({len, static_cast<int>(ti), e});
        }
      }
      if (longest.empty()) return;
      std::sort(longest.begin(), longest.end(), [](const LongEdge& a, const LongEdge& b) { return a.len > b.len; });
      int inserted = 0;
      for (const LongEdge& le : longest) {
        if (AliveCount() >= max_tris) return;
        const Tri& tr = tris_[le.tri];
        if (!tr.alive) continue;
        const int p = tr.v[le.e], q = tr.v[(le.e + 1) % 3];
        if (p < 0) continue;
        // The edge may have been flipped away by an earlier insertion.
        if (constrained_.count(Key(p, q))) continue;
        Vec2 m{(pts_[p].x + pts_[q].x) / 2, (pts_[p].y + pts_[q].y) / 2};
        if (!inside(m)) continue;
        // Verify the owning triangle still carries this edge (index reuse).
        bool still = false;
        for (int k = 0; k < 3; ++k) if (tr.v[k] == p && tr.v[(k + 1) % 3] == q) still = true;
        if (!still) continue;
        pts_.push_back(m);
        InsertOnEdge(static_cast<int>(pts_.size()) - 1, le.tri, le.e);
        ++inserted;
        if (inserted > 6000) break;
      }
      if (inserted == 0) return;
    }
  }

  // Triangles (input-index space), skipping anything touching the super
  // triangle and anything whose centroid fails `inside`.
  std::vector<std::array<int, 3>> Triangles(const std::function<bool(const Vec2&)>& inside) const {
    std::vector<std::array<int, 3>> out;
    for (const Tri& t : tris_) {
      if (!t.alive || IsSuper(t)) continue;
      Vec2 c{(pts_[t.v[0]].x + pts_[t.v[1]].x + pts_[t.v[2]].x) / 3, (pts_[t.v[0]].y + pts_[t.v[1]].y + pts_[t.v[2]].y) / 3};
      if (!inside(c)) continue;
      out.push_back({t.v[0], t.v[1], t.v[2]});
    }
    return out;
  }
  const std::vector<Vec2>& Points() const { return pts_; }

 private:
  static std::pair<int, int> Key(int a, int b) { return {std::min(a, b), std::max(a, b)}; }
  bool IsSuper(const Tri& t) const { return t.v[0] >= n_input_ && t.v[0] < n_input_ + 3 && pts_.size() > 0 && IsSuperIndex(t.v[0]) || IsSuperIndex(t.v[1]) || IsSuperIndex(t.v[2]); }
  bool IsSuperIndex(int i) const { return i >= n_input_ && i < n_input_ + 3; }
  int AliveCount() const { int n = 0; for (const Tri& t : tris_) n += t.alive; return n; }

  static double Orient(const Vec2& a, const Vec2& b, const Vec2& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }
  static bool SegmentsCross(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    const double o1 = Orient(a, b, c), o2 = Orient(a, b, d), o3 = Orient(c, d, a), o4 = Orient(c, d, b);
    return ((o1 > 0) != (o2 > 0)) && ((o3 > 0) != (o4 > 0)) && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
  }
  bool InCircumcircle(const Tri& t, const Vec2& p) const {
    const Vec2& a = pts_[t.v[0]]; const Vec2& b = pts_[t.v[1]]; const Vec2& c = pts_[t.v[2]];
    const double ax = a.x - p.x, ay = a.y - p.y, bx = b.x - p.x, by = b.y - p.y, cx = c.x - p.x, cy = c.y - p.y;
    const double det = (ax * ax + ay * ay) * (bx * cy - cx * by) - (bx * bx + by * by) * (ax * cy - cx * ay) + (cx * cx + cy * cy) * (ax * by - bx * ay);
    return Orient(a, b, c) > 0 ? det > 0 : det < 0;
  }
  int FindEdge(int a, int b) const {
    for (size_t ti = 0; ti < tris_.size(); ++ti) {
      const Tri& t = tris_[ti];
      if (!t.alive) continue;
      for (int e = 0; e < 3; ++e) if ((t.v[e] == a && t.v[(e + 1) % 3] == b) || (t.v[e] == b && t.v[(e + 1) % 3] == a)) return static_cast<int>(ti);
    }
    return -1;
  }
  int FindTriangleWithVertex(int a) const {
    for (size_t ti = 0; ti < tris_.size(); ++ti) if (tris_[ti].alive && (tris_[ti].v[0] == a || tris_[ti].v[1] == a || tris_[ti].v[2] == a)) return static_cast<int>(ti);
    return -1;
  }
  int EdgeIndexOf(const Tri& t, int a, int b) const {
    for (int e = 0; e < 3; ++e) if (t.v[e] == a && t.v[(e + 1) % 3] == b) return e;
    return -1;
  }
  void Link(int t, int e, int u) {
    if (t >= 0) tris_[t].n[e] = u;
  }
  void Relink(int old_t, int new_t, int a, int b) {
    // Neighbour across edge (a->b) of old_t must now point at new_t.
    if (new_t < 0) return;
    Tri& nt = tris_[new_t];
    const int e = EdgeIndexOf(nt, a, b);
    if (e < 0) return;
    const int other = nt.n[e];
    if (other >= 0) {
      Tri& ot = tris_[other];
      for (int k = 0; k < 3; ++k) if (ot.n[k] == old_t) ot.n[k] = new_t;
    }
  }
  // Flip the edge e of triangle t with its neighbour. Returns false if the
  // quad is not convex.
  bool FlipEdge(int t, int e) {
    Tri& A = tris_[t];
    const int u = A.n[e];
    if (u < 0) return false;
    Tri& B = tris_[u];
    const int a = A.v[e], b = A.v[(e + 1) % 3], c = A.v[(e + 2) % 3];
    const int eb = EdgeIndexOf(B, b, a);
    if (eb < 0) return false;
    const int d = B.v[(eb + 2) % 3];
    // Convexity: c and d on opposite sides of a-b and a, b on opposite sides of c-d.
    if (!SegmentsCross(pts_[a], pts_[b], pts_[c], pts_[d])) return false;
    // Neighbours before.
    const int A_bc = A.n[(e + 1) % 3], A_ca = A.n[(e + 2) % 3];
    const int B_ad = B.n[(eb + 1) % 3], B_db = B.n[(eb + 2) % 3];
    // New triangles: A' = (a, d, c), B' = (b, c, d).
    A.v[0] = a; A.v[1] = d; A.v[2] = c;
    B.v[0] = b; B.v[1] = c; B.v[2] = d;
    A.n[0] = B_ad; A.n[1] = u; A.n[2] = A_ca;
    B.n[0] = A_bc; B.n[1] = t; B.n[2] = B_db;
    auto fix = [&](int nb, int old_t, int new_t) {
      if (nb < 0) return;
      for (int k = 0; k < 3; ++k) if (tris_[nb].n[k] == old_t) tris_[nb].n[k] = new_t;
    };
    fix(B_ad, u, t);
    fix(A_bc, t, u);
    return true;
  }
  int Locate(const Vec2& p) const {
    for (size_t ti = 0; ti < tris_.size(); ++ti) {
      const Tri& t = tris_[ti];
      if (!t.alive) continue;
      const Vec2& a = pts_[t.v[0]]; const Vec2& b = pts_[t.v[1]]; const Vec2& c = pts_[t.v[2]];
      const double o1 = Orient(a, b, p), o2 = Orient(b, c, p), o3 = Orient(c, a, p);
      if ((o1 >= 0 && o2 >= 0 && o3 >= 0) || (o1 <= 0 && o2 <= 0 && o3 <= 0)) return static_cast<int>(ti);
    }
    return -1;
  }
  int NewTri(const Tri& t) {
    if (!free_.empty()) { const int idx = free_.back(); free_.pop_back(); tris_[idx] = t; return idx; }
    tris_.push_back(t);
    return static_cast<int>(tris_.size()) - 1;
  }
  void Repoint(int nb, int old_t, int new_t) {
    if (nb < 0) return;
    for (int k = 0; k < 3; ++k) if (tris_[nb].n[k] == old_t) tris_[nb].n[k] = new_t;
  }
  // Lawson insertion: split the containing triangle (or the two triangles
  // sharing the edge the point lies on), then legalise by flipping every
  // non-constrained edge that violates the Delaunay condition.
  void Insert(int pi, std::vector<int> seeds = {}) {
    const Vec2 p = pts_[pi];
    int t = -1;
    if (!seeds.empty() && seeds[0] >= 0 && tris_[seeds[0]].alive) t = seeds[0];
    else t = Locate(p);
    if (t < 0) return;
    // On an edge?
    int on_edge = -1;
    {
      const Tri& tr = tris_[t];
      double best = 1e300;
      for (int e = 0; e < 3; ++e) {
        const Vec2& a = pts_[tr.v[e]]; const Vec2& b = pts_[tr.v[(e + 1) % 3]];
        const double lab = std::hypot(b.x - a.x, b.y - a.y);
        const double o = std::fabs(Orient(a, b, p)) / std::max(lab, 1e-300);  // distance to the line
        if (o < best) { best = o; on_edge = e; }
      }
      const Vec2& a = pts_[tr.v[on_edge]]; const Vec2& b = pts_[tr.v[(on_edge + 1) % 3]];
      const double lab = std::hypot(b.x - a.x, b.y - a.y);
      if (best > 1e-9 * std::max(lab, 1e-300)) on_edge = -1;
    }
    std::vector<std::pair<int, int>> to_legalize;
    if (on_edge < 0) {
      SplitInside(t, pi, to_legalize);
    } else {
      SplitOnEdge(t, on_edge, pi, to_legalize);
    }
    while (!to_legalize.empty()) {
      auto [tri, e] = to_legalize.back();
      to_legalize.pop_back();
      Legalize(tri, e, pi, to_legalize);
    }
  }
  // Inserts point pi exactly on edge e of triangle t (used by refinement,
  // which already knows the edge; detection by distance is unreliable in
  // sliver triangles where all three edges are nearly collinear).
  void InsertOnEdge(int pi, int t, int e) {
    if (t < 0 || !tris_[t].alive) return;
    std::vector<std::pair<int, int>> to_legalize;
    SplitOnEdge(t, e, pi, to_legalize);
    while (!to_legalize.empty()) {
      auto [tri, ee] = to_legalize.back();
      to_legalize.pop_back();
      Legalize(tri, ee, pi, to_legalize);
    }
  }
  void SplitInside(int t, int pi, std::vector<std::pair<int, int>>& out) {
    const Tri old = tris_[t];
    const int a = old.v[0], b = old.v[1], c = old.v[2];
    Tri t1, t2, t3;
    t1.v[0] = a; t1.v[1] = b; t1.v[2] = pi;
    t2.v[0] = b; t2.v[1] = c; t2.v[2] = pi;
    t3.v[0] = c; t3.v[1] = a; t3.v[2] = pi;
    const int i1 = t;
    tris_[i1] = t1;
    const int i2 = NewTri(t2);
    const int i3 = NewTri(t3);
    tris_[i1].n[0] = old.n[0]; tris_[i1].n[1] = i2; tris_[i1].n[2] = i3;
    tris_[i2].n[0] = old.n[1]; tris_[i2].n[1] = i3; tris_[i2].n[2] = i1;
    tris_[i3].n[0] = old.n[2]; tris_[i3].n[1] = i1; tris_[i3].n[2] = i2;
    Repoint(old.n[1], t, i2);
    Repoint(old.n[2], t, i3);
    out.push_back({i1, 0}); out.push_back({i2, 0}); out.push_back({i3, 0});
  }
  void SplitOnEdge(int t, int e, int pi, std::vector<std::pair<int, int>>& out) {
    const Tri old_t = tris_[t];
    const int a = old_t.v[e], b = old_t.v[(e + 1) % 3], c = old_t.v[(e + 2) % 3];
    const int u = old_t.n[e];
    const int t_bc = old_t.n[(e + 1) % 3], t_ca = old_t.n[(e + 2) % 3];
    const bool was_constrained = constrained_.count(Key(a, b)) > 0;
    if (was_constrained) { constrained_.erase(Key(a, b)); constrained_.insert(Key(a, pi)); constrained_.insert(Key(pi, b)); }
    Tri T1, T2;
    T1.v[0] = a; T1.v[1] = pi; T1.v[2] = c;
    T2.v[0] = pi; T2.v[1] = b; T2.v[2] = c;
    tris_[t] = T1;
    const int i2 = NewTri(T2);
    tris_[t].n[1] = i2; tris_[t].n[2] = t_ca;
    tris_[i2].n[1] = t_bc; tris_[i2].n[2] = t;
    Repoint(t_bc, t, i2);
    out.push_back({t, 2}); out.push_back({i2, 1});
    if (u < 0) { tris_[t].n[0] = -1; tris_[i2].n[0] = -1; return; }
    const Tri old_u = tris_[u];
    const int eb = EdgeIndexOf(old_u, b, a);
    if (eb < 0) { tris_[t].n[0] = -1; tris_[i2].n[0] = -1; return; }
    const int d = old_u.v[(eb + 2) % 3];
    const int u_ad = old_u.n[(eb + 1) % 3], u_db = old_u.n[(eb + 2) % 3];
    Tri U1, U2;
    U1.v[0] = b; U1.v[1] = pi; U1.v[2] = d;
    U2.v[0] = pi; U2.v[1] = a; U2.v[2] = d;
    tris_[u] = U1;
    const int j2 = NewTri(U2);
    tris_[u].n[0] = i2; tris_[u].n[1] = j2; tris_[u].n[2] = u_db;
    tris_[j2].n[0] = t; tris_[j2].n[1] = u_ad; tris_[j2].n[2] = u;
    tris_[t].n[0] = j2;
    tris_[i2].n[0] = u;
    Repoint(u_ad, u, j2);
    out.push_back({u, 2}); out.push_back({j2, 1});
  }
  // Edge e of triangle t is opposite the newly inserted point.
  void Legalize(int t, int e, int pi, std::vector<std::pair<int, int>>& out) {
    if (t < 0 || !tris_[t].alive) return;
    Tri& tr = tris_[t];
    if (tr.v[(e + 2) % 3] != pi) {
      // Find the edge opposite pi.
      for (int k = 0; k < 3; ++k) if (tr.v[(k + 2) % 3] == pi) { e = k; break; }
      if (tr.v[(e + 2) % 3] != pi) return;
    }
    const int a = tr.v[e], b = tr.v[(e + 1) % 3];
    const int nb = tr.n[e];
    if (nb < 0 || constrained_.count(Key(a, b))) return;
    const Tri& other = tris_[nb];
    const int eb = EdgeIndexOf(other, b, a);
    if (eb < 0) return;
    const int d = other.v[(eb + 2) % 3];
    if (!InCircumcircle(tr, pts_[d])) return;
    if (!FlipEdge(t, e)) return;
    // After the flip: t = (a, d, pi), nb = (b, pi, d).
    out.push_back({t, 0});
    out.push_back({nb, 2});
  }

  std::vector<Vec2> pts_;
  std::vector<Tri> tris_;
  std::vector<int> free_;
  std::set<std::pair<int, int>> constrained_;
  int n_input_ = 0;
};

bool PointInPolygon(const std::vector<Vec2>& poly, const Vec2& p) {
  bool inside = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const Vec2& a = poly[i]; const Vec2& b = poly[j];
    if ((a.y > p.y) != (b.y > p.y)) {
      const double x = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
      if (p.x < x) inside = !inside;
    }
  }
  return inside;
}

// ---------------------------------------------------------------------------
// Edge sampling
// ---------------------------------------------------------------------------

struct EdgeSamples {
  std::vector<double> t;      // edge parameters, increasing
  std::vector<Point3d> pts;   // 3D points (shared by both faces)
};

EdgeSamples SampleEdge(const ON_BrepEdge& edge, double tol, double min_samples, double max_spacing) {
  EdgeSamples s;
  const ON_Interval d = edge.Domain();
  ON_NurbsCurve nc;
  std::vector<double> params;
  // Degenerate edge (a sphere pole): two coincident samples are enough; the
  // triangle they span collapses and is culled after welding.
  {
    ON_BoundingBox ebox;
    if (edge.GetBoundingBox(ebox) && ebox.Diagonal().Length() < tol * 0.5) {
      s.t = {d.Min(), d.Max()};
      s.pts = {edge.PointAt(d.Min()), edge.PointAt(d.Max())};
      return s;
    }
  }
  if (edge.GetNurbForm(nc) > 0) {
    kernel::NurbsCurve k;
    k.raw() = nc;
    params = k.SuggestedParameterValues(tol);
    // NURBS form may have a different domain: remap to the edge's.
    const ON_Interval nd = nc.Domain();
    for (double& t : params) t = d.ParameterAt(nd.NormalizedParameterAt(t));
  }
  if (params.size() < 2) params = {d.Min(), d.Max()};
  // Enforce a minimum count so long straight edges still get vertices for
  // refinement to hook on; closed edges (a cap circle) need a real polygon.
  int min_n = static_cast<int>(std::max(2.0, min_samples));
  if (edge.IsClosed()) min_n = std::max(min_n, 9);
  else if (!edge.IsLinear()) min_n = std::max(min_n, 5);
  if (static_cast<int>(params.size()) < min_n) {
    params.clear();
    for (int i = 0; i < min_n; ++i) params.push_back(d.ParameterAt(static_cast<double>(i) / (min_n - 1)));
  }
  std::sort(params.begin(), params.end());
  params.erase(std::unique(params.begin(), params.end()), params.end());
  // Subdivide any segment longer than the neighbouring faces' target size so
  // constrained edges never dwarf the interior triangles.
  if (max_spacing > 0) {
    std::vector<double> refined;
    for (size_t i = 0; i + 1 < params.size(); ++i) {
      refined.push_back(params[i]);
      const double len = edge.PointAt(params[i]).DistanceTo(edge.PointAt(params[i + 1]));
      const int pieces = std::min(512, static_cast<int>(std::ceil(len / max_spacing)));
      for (int k = 1; k < pieces; ++k) refined.push_back(params[i] + (params[i + 1] - params[i]) * k / pieces);
    }
    refined.push_back(params.back());
    params.swap(refined);
  }
  for (double t : params) {
    s.t.push_back(t);
    s.pts.push_back(edge.PointAt(t));
  }
  return s;
}

struct FaceVertex {
  Vec2 uv;
  Point3d p;
};

}  // namespace

// ---------------------------------------------------------------------------
// Face meshing
// ---------------------------------------------------------------------------

struct FaceInfo {
  bool valid = false;
  ON_NurbsSurface ns;
  ON_Interval du, dv, ndu, ndv;
  double Lu = 1, Lv = 1, h3 = 1, scale_u = 1, scale_v = 1;
  int nu = 1, nv = 1;
};

FaceInfo AnalyseFace(const ON_BrepFace& face, const BrepMeshOptions& options, double tol) {
  FaceInfo fi;
  const ON_Surface* srf = face.SurfaceOf();
  if (!srf) return fi;
  const ON_NurbsSurface* nsp = ON_NurbsSurface::Cast(srf);
  if (nsp) fi.ns = *nsp; else if (srf->GetNurbForm(fi.ns) <= 0) return fi;
  fi.du = srf->Domain(0); fi.dv = srf->Domain(1);
  fi.ndu = fi.ns.Domain(0); fi.ndv = fi.ns.Domain(1);
  const ON_NurbsSurface& ns = fi.ns;
  // 3D extent of the surface in each parameter direction (longest
  // isoparametric polyline on a coarse grid), so the (u, v) domain can be
  // scaled to be roughly isotropic in model space before triangulating.
  double Lu = 0, Lv = 0;
  const int g = 9;
  for (int j = 0; j < g; ++j) {
    double row = 0, col = 0;
    for (int i = 0; i + 1 < g; ++i) {
      const double v = fi.ndv.ParameterAt(static_cast<double>(j) / (g - 1));
      row += ns.PointAt(fi.ndu.ParameterAt(static_cast<double>(i) / (g - 1)), v).DistanceTo(ns.PointAt(fi.ndu.ParameterAt(static_cast<double>(i + 1) / (g - 1)), v));
      const double u = fi.ndu.ParameterAt(static_cast<double>(j) / (g - 1));
      col += ns.PointAt(u, fi.ndv.ParameterAt(static_cast<double>(i) / (g - 1))).DistanceTo(ns.PointAt(u, fi.ndv.ParameterAt(static_cast<double>(i + 1) / (g - 1))));
    }
    Lu = std::max(Lu, row);
    Lv = std::max(Lv, col);
  }
  fi.Lu = std::max(Lu, 1e-9);
  fi.Lv = std::max(Lv, 1e-9);
  // Target triangle size in model units: from the kernel's curvature-aware
  // division counts, then capped by the triangle budget.
  kernel::NurbsSurface ksrf;
  ksrf.raw() = ns;
  kernel::SurfaceDivisions div = ksrf.SuggestedDivisions(tol);
  fi.nu = std::max(1, div.u); fi.nv = std::max(1, div.v);
  double h3 = std::min(fi.Lu / fi.nu, fi.Lv / fi.nv);
  h3 = std::max(h3, (fi.Lu + fi.Lv) / 1500.0);
  const double est = 2.0 * (fi.Lu / h3) * (fi.Lv / h3);
  if (est > options.max_triangles_per_face) h3 *= std::sqrt(est / options.max_triangles_per_face);
  fi.h3 = h3;
  fi.scale_u = fi.Lu / std::max(1e-12, fi.ndu.Length());
  fi.scale_v = fi.Lv / std::max(1e-12, fi.ndv.Length());
  fi.valid = true;
  return fi;
}

std::vector<kernel::Mesh> MeshBrepFaces(const ON_Brep& brep, const BrepMeshOptions& options) {
  std::vector<kernel::Mesh> result;
  const double tol = std::max(1e-6, options.chord_tolerance);

  // Pass 1: per-face sizing.
  std::vector<FaceInfo> infos(static_cast<size_t>(brep.m_F.Count()));
  for (int fi = 0; fi < brep.m_F.Count(); ++fi) {
    if (brep.m_F[fi].m_face_index >= 0) infos[static_cast<size_t>(fi)] = AnalyseFace(brep.m_F[fi], options, tol);
  }

  // Pass 2: edges, sampled at the finest neighbouring face's target size.
  std::vector<EdgeSamples> edges(static_cast<size_t>(brep.m_E.Count()));
  for (int ei = 0; ei < brep.m_E.Count(); ++ei) {
    const ON_BrepEdge& e = brep.m_E[ei];
    if (e.m_edge_index < 0 || !e.EdgeCurveOf()) continue;
    double spacing = 0;
    for (int k = 0; k < e.m_ti.Count(); ++k) {
      const ON_BrepTrim& trim = brep.m_T[e.m_ti[k]];
      if (trim.m_li < 0) continue;
      const int f = brep.m_L[trim.m_li].m_fi;
      if (f < 0 || !infos[static_cast<size_t>(f)].valid) continue;
      const double h = infos[static_cast<size_t>(f)].h3;
      spacing = spacing > 0 ? std::min(spacing, h) : h;
    }
    edges[static_cast<size_t>(ei)] = SampleEdge(e, tol, options.min_edge_samples, spacing);
  }

  // Pass 3: faces.
  for (int fi = 0; fi < brep.m_F.Count(); ++fi) {
    const ON_BrepFace& face = brep.m_F[fi];
    const FaceInfo& info = infos[static_cast<size_t>(fi)];
    if (face.m_face_index < 0 || !info.valid) continue;
    const ON_NurbsSurface& ns = info.ns;
    kernel::NurbsSurface ksrf;
    ksrf.raw() = ns;
    const ON_Interval du = info.du, dv = info.dv, ndu = info.ndu, ndv = info.ndv;
    auto to_nurbs_uv = [&](double u, double v) {
      return Vec2{ndu.ParameterAt(du.NormalizedParameterAt(u)), ndv.ParameterAt(dv.NormalizedParameterAt(v))};
    };
    const double Lu = info.Lu, Lv = info.Lv, h3 = info.h3, scale_u = info.scale_u, scale_v = info.scale_v;
    const int nu = info.nu, nv = info.nv;
    auto pole_samples_for = [&](const ON_3dPoint& a, const ON_3dPoint& b) {
      const double len = std::hypot((b.x - a.x) * scale_u, (b.y - a.y) * scale_v);
      return static_cast<int>(std::clamp(len / h3, 4.0, 512.0));
    };

    // Build loops.
    std::vector<FaceVertex> verts;
    std::vector<std::vector<int>> loops;      // vertex indices per loop
    std::vector<bool> loop_is_outer;
    struct PoleRun { size_t loop; size_t start; int count; };
    std::vector<PoleRun> pole_runs;           // singular trims: a run of vertices at one 3D point
    std::vector<std::vector<int>> extra_chains;  // interior constrained polylines (guard rows)
    for (int li = 0; li < face.m_li.Count(); ++li) {
      const ON_BrepLoop& loop = brep.m_L[face.m_li[li]];
      if (loop.m_type != ON_BrepLoop::outer && loop.m_type != ON_BrepLoop::inner) continue;
      std::vector<int> ids;
      for (int k = 0; k < loop.m_ti.Count(); ++k) {
        const ON_BrepTrim& trim = brep.m_T[loop.m_ti[k]];
        const ON_Interval td = trim.Domain();
        if (trim.m_ei >= 0 && !edges[static_cast<size_t>(trim.m_ei)].t.empty()) {
          const EdgeSamples& es = edges[static_cast<size_t>(trim.m_ei)];
          const ON_BrepEdge& edge = brep.m_E[trim.m_ei];
          const ON_Interval ed = edge.Domain();
          const size_t n = es.t.size();
          // Dense trim samples for locating edge points whose trim/edge
          // parameterisations are not linearly related (tori, revolves).
          const int dense_n = static_cast<int>(std::max<size_t>(64, 4 * n));
          std::vector<double> dense_t(static_cast<size_t>(dense_n) + 1);
          std::vector<Point3d> dense_p(static_cast<size_t>(dense_n) + 1);
          for (int j = 0; j <= dense_n; ++j) {
            dense_t[static_cast<size_t>(j)] = td.ParameterAt(static_cast<double>(j) / dense_n);
            ON_3dPoint uv = trim.PointAt(dense_t[static_cast<size_t>(j)]);
            Vec2 nuv = to_nurbs_uv(uv.x, uv.y);
            dense_p[static_cast<size_t>(j)] = ns.PointAt(nuv.x, nuv.y);
          }
          auto eval3d = [&](double tt) { ON_3dPoint uv = trim.PointAt(tt); Vec2 nuv = to_nurbs_uv(uv.x, uv.y); return ns.PointAt(nuv.x, nuv.y); };
          for (size_t i = 0; i + 1 < n; ++i) {  // last sample belongs to the next trim
            const size_t si = trim.m_bRev3d ? (n - 1 - i) : i;
            const double sn = ed.NormalizedParameterAt(es.t[si]);
            double tt = td.ParameterAt(trim.m_bRev3d ? 1.0 - sn : sn);
            const Point3d target = es.pts[si];
            if (eval3d(tt).DistanceTo(target) > tol) {
              // Nearest dense sample, then golden-section refine around it.
              int best = 0;
              double bestd = 1e300;
              for (int j = 0; j <= dense_n; ++j) { const double dd = dense_p[static_cast<size_t>(j)].DistanceTo(target); if (dd < bestd) { bestd = dd; best = j; } }
              double lo = dense_t[static_cast<size_t>(std::max(0, best - 1))], hi = dense_t[static_cast<size_t>(std::min(dense_n, best + 1))];
              const double gr = 0.6180339887498949;
              double x1 = hi - gr * (hi - lo), x2 = lo + gr * (hi - lo);
              double f1 = eval3d(x1).DistanceTo(target), f2 = eval3d(x2).DistanceTo(target);
              for (int it = 0; it < 40; ++it) {
                if (f1 < f2) { hi = x2; x2 = x1; f2 = f1; x1 = hi - gr * (hi - lo); f1 = eval3d(x1).DistanceTo(target); }
                else { lo = x1; x1 = x2; f1 = f2; x2 = lo + gr * (hi - lo); f2 = eval3d(x2).DistanceTo(target); }
              }
              tt = (lo + hi) / 2;
            }
            ON_3dPoint uv = trim.PointAt(tt);
            FaceVertex fv;
            fv.uv = to_nurbs_uv(uv.x, uv.y);
            fv.p = target;
            ids.push_back(static_cast<int>(verts.size()));
            verts.push_back(fv);
          }
        } else {
          // Singular trim (a sphere pole) or a trim without an edge: many uv
          // samples that all share one 3D point. Short constrained segments
          // keep the CDT well conditioned; the collapsed triangles are
          // culled after welding, leaving a clean fan around the pole.
          const int k = pole_samples_for(trim.PointAtStart(), trim.PointAtEnd());
          pole_runs.push_back({loops.size(), ids.size(), k});
          ON_3dPoint uv0 = trim.PointAt(td.Min());
          Vec2 nuv0 = to_nurbs_uv(uv0.x, uv0.y);
          const Point3d pole = ns.PointAt(nuv0.x, nuv0.y);
          for (int i = 0; i < k; ++i) {
            ON_3dPoint uv = trim.PointAt(td.ParameterAt(static_cast<double>(i) / k));
            FaceVertex fv;
            fv.uv = to_nurbs_uv(uv.x, uv.y);
            fv.p = pole;
            ids.push_back(static_cast<int>(verts.size()));
            verts.push_back(fv);
          }
        }
      }
      if (std::getenv("DINO8_MESH_DEBUG")) {
        std::fprintf(stderr, "[mesh] face %d loop %d type %d trims %d verts %zu\n", fi, li, static_cast<int>(loop.m_type), loop.m_ti.Count(), ids.size());
        for (int k = 0; k < loop.m_ti.Count(); ++k) {
          const ON_BrepTrim& trim = brep.m_T[loop.m_ti[k]];
          ON_3dPoint a = trim.PointAtStart(), b = trim.PointAtEnd();
          std::fprintf(stderr, "[mesh]   trim %d type %d edge %d rev %d samples %zu uv (%.3f,%.3f)->(%.3f,%.3f)\n", k, static_cast<int>(trim.m_type), trim.m_ei, trim.m_bRev3d ? 1 : 0,
                       trim.m_ei >= 0 ? edges[static_cast<size_t>(trim.m_ei)].t.size() : size_t(0), a.x, a.y, b.x, b.y);
        }
      }
      if (ids.size() < 3) continue;
      loops.push_back(ids);
      loop_is_outer.push_back(loop.m_type == ON_BrepLoop::outer);
    }
    if (loops.empty()) continue;

    // Guard rows: a constrained polyline one seam sample above each pole
    // run, so the strip next to the pole triangulates as a ladder. After
    // welding, the ladder collapses into a clean fan around the pole and
    // no seam vertex can reach two different pole samples.
    for (const PoleRun& run : pole_runs) {
      if (run.loop >= loops.size()) continue;
      const std::vector<int>& ids = loops[run.loop];
      const size_t n = ids.size();
      if (n < static_cast<size_t>(run.count) + 3) continue;
      const int prev = ids[(run.start + n - 1) % n];
      const int end_corner = ids[(run.start + static_cast<size_t>(run.count)) % n];
      const int next2 = ids[(run.start + static_cast<size_t>(run.count) + 1) % n];
      const Vec2 c0 = verts[static_cast<size_t>(ids[run.start])].uv;
      const Vec2 c1 = verts[static_cast<size_t>(end_corner)].uv;
      const Vec2 d0{verts[static_cast<size_t>(prev)].uv.x - c0.x, verts[static_cast<size_t>(prev)].uv.y - c0.y};
      const Vec2 d1{verts[static_cast<size_t>(next2)].uv.x - c1.x, verts[static_cast<size_t>(next2)].uv.y - c1.y};
      std::vector<int> chain = {prev};
      for (int i = 1; i < run.count; ++i) {
        const double t = static_cast<double>(i) / run.count;
        const Vec2 pole_uv = verts[static_cast<size_t>(ids[run.start + static_cast<size_t>(i)])].uv;
        FaceVertex g;
        g.uv = Vec2{pole_uv.x + d0.x * (1 - t) + d1.x * t, pole_uv.y + d0.y * (1 - t) + d1.y * t};
        g.p = ns.PointAt(g.uv.x, g.uv.y);
        chain.push_back(static_cast<int>(verts.size()));
        verts.push_back(g);
      }
      chain.push_back(next2);
      extra_chains.push_back(chain);
    }

    // Normalise uv to a unit-ish square so Delaunay is isotropic.
    double umin = 1e300, umax = -1e300, vmin = 1e300, vmax = -1e300;
    for (const FaceVertex& v : verts) { umin = std::min(umin, v.uv.x); umax = std::max(umax, v.uv.x); vmin = std::min(vmin, v.uv.y); vmax = std::max(vmax, v.uv.y); }
    // Scale to model-space-like units so Delaunay triangles are fat in 3D.
    const double su = scale_u, sv = scale_v;
    std::vector<Vec2> pts;
    for (const FaceVertex& v : verts) pts.push_back({(v.uv.x - umin) * su, (v.uv.y - vmin) * sv});
    // Drop exact duplicate points (they break Delaunay).
    std::vector<int> remap(pts.size());
    std::vector<Vec2> uniq;
    std::map<std::pair<long long, long long>, int> seen;
    for (size_t i = 0; i < pts.size(); ++i) {
      std::pair<long long, long long> key{std::llround(pts[i].x / (Lu + Lv) * 1e9), std::llround(pts[i].y / (Lu + Lv) * 1e9)};
      auto it = seen.find(key);
      if (it == seen.end()) { seen[key] = static_cast<int>(uniq.size()); remap[i] = static_cast<int>(uniq.size()); uniq.push_back(pts[i]); }
      else remap[i] = it->second;
    }
    std::vector<int> uniq_to_vert(uniq.size(), -1);
    for (size_t i = 0; i < pts.size(); ++i) if (uniq_to_vert[static_cast<size_t>(remap[i])] < 0) uniq_to_vert[static_cast<size_t>(remap[i])] = static_cast<int>(i);
    if (uniq.size() < 3) continue;

    CDT cdt(uniq);
    int failed_constraints = 0;
    std::vector<std::vector<Vec2>> polys;
    for (size_t li = 0; li < loops.size(); ++li) {
      std::vector<Vec2> poly;
      const std::vector<int>& ids = loops[li];
      for (size_t k = 0; k < ids.size(); ++k) {
        const int a = remap[static_cast<size_t>(ids[k])], b = remap[static_cast<size_t>(ids[(k + 1) % ids.size()])];
        if (a != b && !cdt.Constrain(a, b)) ++failed_constraints;
        poly.push_back(uniq[static_cast<size_t>(a)]);
      }
      polys.push_back(poly);
    }
    for (const std::vector<int>& chain : extra_chains) {
      for (size_t k = 0; k + 1 < chain.size(); ++k) {
        const int a = remap[static_cast<size_t>(chain[k])], b = remap[static_cast<size_t>(chain[k + 1])];
        if (a != b && !cdt.Constrain(a, b)) ++failed_constraints;
      }
    }
    auto inside = [&](const Vec2& p) {
      bool in = false;
      for (size_t li = 0; li < polys.size(); ++li) {
        const bool pin = PointInPolygon(polys[li], p);
        if (loop_is_outer[li]) { if (!pin) return false; in = true; }
        else if (pin) return false;
      }
      return in;
    };
    if (std::getenv("DINO8_MESH_DEBUG")) std::fprintf(stderr, "[mesh] face %d: %zu boundary pts, Lu %.2f Lv %.2f h %.3f (div %d x %d) failed constraints %d\n", fi, uniq.size(), Lu, Lv, h3, nu, nv, failed_constraints);
    cdt.Refine(h3, h3, options.max_triangles_per_face, inside);
    if (std::getenv("DINO8_MESH_DEBUG")) {
      auto all_tris = cdt.Triangles([](const Vec2&) { return true; });
      auto in_tris = cdt.Triangles(inside);
      std::set<int> used;
      for (const auto& t : in_tris) for (int k = 0; k < 3; ++k) used.insert(t[k]);
      std::fprintf(stderr, "[mesh] face %d: cdt points %zu, tris total %zu inside %zu, points used %zu\n", fi, cdt.Points().size(), all_tris.size(), in_tris.size(), used.size());
    }

    // Emit mesh.
    kernel::Mesh mesh;
    ON_Mesh& m = mesh.raw();
    const std::vector<Vec2>& all = cdt.Points();
    std::vector<int> mesh_index(all.size(), -1);
    for (size_t i = 0; i < all.size(); ++i) {
      Point3d p;
      if (i < uniq.size()) {
        p = verts[static_cast<size_t>(uniq_to_vert[i])].p;
      } else {
        const double u = umin + all[i].x / su, v = vmin + all[i].y / sv;
        p = ns.PointAt(u, v);
      }
      mesh_index[i] = m.VertexCount();
      m.SetVertex(mesh_index[i], p);
    }
    for (const std::array<int, 3>& t : cdt.Triangles(inside)) {
      int a = mesh_index[static_cast<size_t>(t[0])], b = mesh_index[static_cast<size_t>(t[1])], c = mesh_index[static_cast<size_t>(t[2])];
      // Orientation: uv CCW => surface normal; flip if face is reversed.
      const Vec2& pa = all[static_cast<size_t>(t[0])]; const Vec2& pb = all[static_cast<size_t>(t[1])]; const Vec2& pc = all[static_cast<size_t>(t[2])];
      const double orient = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
      bool flip = orient < 0;
      if (face.m_bRev) flip = !flip;
      if (flip) std::swap(b, c);
      m.SetTriangle(m.FaceCount(), a, b, c);
    }
    m.CullDegenerateFaces();
    m.ComputeFaceNormals();
    m.ComputeVertexNormals();
    result.push_back(std::move(mesh));
  }
  return result;
}

namespace {
void DebugStats(const char* label, const kernel::Mesh& mesh) {
  if (!std::getenv("DINO8_MESH_DEBUG")) return;
  const ON_Mesh& m = mesh.raw();
  std::map<std::pair<int, int>, int> edges;
  for (int i = 0; i < m.FaceCount(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    const int n = f.IsQuad() ? 4 : 3;
    for (int k = 0; k < n; ++k) { int a = f.vi[k], b = f.vi[(k + 1) % n]; edges[{std::min(a, b), std::max(a, b)}]++; }
  }
  int naked = 0, nonmanifold = 0;
  for (const auto& [e, c] : edges) { if (c == 1) ++naked; else if (c > 2) ++nonmanifold; }
  std::fprintf(stderr, "[mesh] %s: V=%d F=%d naked=%d nonmanifold=%d\n", label, m.VertexCount(), m.FaceCount(), naked, nonmanifold);
}
}  // namespace

namespace {
kernel::Mesh MeshBrepClosedOnce(const ON_Brep& brep, const BrepMeshOptions& options);
}

kernel::Mesh MeshBrepClosed(const ON_Brep& brep, const BrepMeshOptions& options) {
  kernel::Mesh m = MeshBrepClosedOnce(brep, options);
  // A closed brep that did not mesh watertight at this tolerance usually
  // does at a finer one: retry twice before giving up.
  if (brep.IsSolid() && !m.IsClosedManifold()) {
    BrepMeshOptions finer = options;
    for (int attempt = 0; attempt < 2 && !m.IsClosedManifold(); ++attempt) {
      finer.chord_tolerance *= 0.25;
      finer.max_triangles_per_face = options.max_triangles_per_face * 2;
      m = MeshBrepClosedOnce(brep, finer);
    }
  }
  return m;
}

namespace {
kernel::Mesh MeshBrepClosedOnce(const ON_Brep& brep, const BrepMeshOptions& options) {
  std::vector<kernel::Mesh> faces = MeshBrepFaces(brep, options);
  if (faces.empty()) return kernel::Mesh();
  for (size_t i = 0; i < faces.size(); ++i) DebugStats(("face " + std::to_string(i)).c_str(), faces[i]);
  ON_BoundingBox bb;
  brep.GetBoundingBox(bb);
  const double size = std::max(1e-9, bb.Diagonal().Length());
  kernel::Mesh merged = kernel::Mesh::MergeAndWeld(faces, std::max(1e-12, size * 1e-8));
  merged.raw().CullDegenerateFaces();
  // Welding a pole collapses fans of triangles; two of them can end up as
  // the same triangle (possibly with opposite winding). Keep one copy.
  {
    ON_Mesh& m = merged.raw();
    std::set<std::array<int, 3>> seen;
    ON_SimpleArray<ON_MeshFace> kept;
    for (int i = 0; i < m.m_F.Count(); ++i) {
      const ON_MeshFace& f = m.m_F[i];
      if (!f.IsTriangle()) { kept.Append(f); continue; }
      std::array<int, 3> key = {f.vi[0], f.vi[1], f.vi[2]};
      std::sort(key.begin(), key.end());
      if (seen.insert(key).second) kept.Append(f);
    }
    if (kept.Count() != m.m_F.Count()) m.m_F = kept;
  }
  merged.raw().Compact();
  merged.raw().ComputeFaceNormals();
  merged.raw().ComputeVertexNormals();
  DebugStats("welded", merged);
  return merged;
}
}  // namespace

}  // namespace dino8::app
