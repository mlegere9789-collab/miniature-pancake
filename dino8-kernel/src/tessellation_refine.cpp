#include "dino8/kernel/tessellation_refine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dino8::kernel {

// ===========================================================================================================
// Mesh quality
// ===========================================================================================================

namespace {

using V3 = std::array<double, 3>;

V3 Sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double Dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 Cross(const V3& a, const V3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double Norm(const V3& a) { return std::sqrt(Dot(a, a)); }
V3 FromPoint(const ON_3dPoint& p) { return {p.x, p.y, p.z}; }

// A triangle is degenerate when its area is negligible against its longest edge squared.
bool IsDegenerate(double area2, double longest) { return !(area2 > 1e-10 * longest * longest); }

}  // namespace

MeshQuality MeasureMeshQuality(const Mesh& mesh) {
  MeshQuality q;
  const ON_Mesh& raw = mesh.raw();
  auto P = [&](int i) { return V3{raw.m_V[i].x, raw.m_V[i].y, raw.m_V[i].z}; };

  struct EdgeUse {
    int count = 0;
    V3 first{0, 0, 0};
    double worst = 0.0;
  };
  std::map<std::pair<int, int>, EdgeUse> edges;

  bool any = false;
  double aspect_sum = 0.0;
  int measured = 0;
  auto add = [&](int ia, int ib, int ic) {
    ++q.triangle_count;
    const V3 a = P(ia), b = P(ib), c = P(ic);
    const double la = Norm(Sub(b, c)), lb = Norm(Sub(c, a)), lc = Norm(Sub(a, b));
    const V3 cr = Cross(Sub(b, a), Sub(c, a));
    const double area2 = Norm(cr);  // twice the area
    const double longest = std::max({la, lb, lc});
    if (IsDegenerate(area2, longest)) {
      ++q.degenerate_count;
      return;
    }
    const double area = 0.5 * area2;
    const double s = 0.5 * (la + lb + lc);
    const double aspect = la * lb * lc * s / (8.0 * area * area);
    // Interior angles from atan2 of the cross and dot products at each corner.
    const double ang_a = std::atan2(area2, Dot(Sub(b, a), Sub(c, a)));
    const double ang_b = std::atan2(area2, Dot(Sub(a, b), Sub(c, b)));
    const double ang_c = std::atan2(area2, Dot(Sub(a, c), Sub(b, c)));
    const double lo = std::min({ang_a, ang_b, ang_c}), hi = std::max({ang_a, ang_b, ang_c});
    const double shortest = std::min({la, lb, lc});
    if (!any) {
      q.min_angle = lo;
      q.max_angle = hi;
      q.max_aspect_ratio = aspect;
      q.min_area = q.max_area = area;
      q.min_edge_length = shortest;
      q.max_edge_length = longest;
      any = true;
    } else {
      q.min_angle = std::min(q.min_angle, lo);
      q.max_angle = std::max(q.max_angle, hi);
      q.max_aspect_ratio = std::max(q.max_aspect_ratio, aspect);
      q.min_area = std::min(q.min_area, area);
      q.max_area = std::max(q.max_area, area);
      q.min_edge_length = std::min(q.min_edge_length, shortest);
      q.max_edge_length = std::max(q.max_edge_length, longest);
    }
    q.total_area += area;
    aspect_sum += aspect;
    ++measured;
    const V3 n = {cr[0] / area2, cr[1] / area2, cr[2] / area2};
    const int v[3] = {ia, ib, ic};
    for (int k = 0; k < 3; ++k) {
      int p = v[k], r = v[(k + 1) % 3];
      if (p > r) std::swap(p, r);
      EdgeUse& e = edges[{p, r}];
      if (e.count == 0) {
        e.first = n;
      } else {
        const double d = std::min(1.0, std::max(-1.0, Dot(e.first, n)));
        e.worst = std::max(e.worst, std::acos(d));
      }
      ++e.count;
    }
  };

  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& f = raw.m_F[i];
    add(f.vi[0], f.vi[1], f.vi[2]);
    if (f.IsQuad()) add(f.vi[0], f.vi[2], f.vi[3]);
  }
  if (measured > 0) q.mean_aspect_ratio = aspect_sum / measured;
  for (const auto& e : edges) q.max_adjacent_normal_angle = std::max(q.max_adjacent_normal_angle, e.second.worst);
  return q;
}

// ===========================================================================================================
// Per-region adaptive tessellation
// ===========================================================================================================

namespace {

struct Node {
  double u = 0.0, v = 0.0;
  ON_3dPoint p;
  ON_3dVector n;
  bool normal_valid = false;
  bool normal_done = false;
  int canon = 0;  // identity modulo the closed-direction seams (for edge matching only)
};

struct Tri {
  int a = 0;       // apex (the newest vertex)
  int b0 = 0;      // base edge b0-b1 is the edge bisected next; (a, b0, b1) is counter-clockwise in (u, v)
  int b1 = 0;
  int level = 0;
  bool alive = true;
  bool evaluated = false;
  double dev = 0.0;
  double ang = 0.0;
  double longest = 0.0;
  double severity = 0.0;
};

void ValidateOptions(const RegionRefineOptions& o) {
  const char* who = "dino8::kernel::TessellateSurfaceRegionAdaptive: ";
  if (!(o.chord_tolerance > 0.0) || !std::isfinite(o.chord_tolerance)) {
    throw std::invalid_argument(std::string(who) + "chord_tolerance must be positive and finite");
  }
  if (!(o.angle_tolerance >= 0.0) || o.angle_tolerance > ON_PI) {
    throw std::invalid_argument(std::string(who) + "angle_tolerance must be in [0, pi]");
  }
  if (!(o.max_edge_length >= 0.0) || !std::isfinite(o.max_edge_length)) {
    throw std::invalid_argument(std::string(who) + "max_edge_length must be >= 0 and finite");
  }
  if (!(o.max_aspect_ratio >= 1.0) || !std::isfinite(o.max_aspect_ratio)) {
    throw std::invalid_argument(std::string(who) + "max_aspect_ratio must be >= 1");
  }
  if (o.min_divisions < 1 || o.max_levels < 0 || o.max_triangles < 0 || o.samples < 2) {
    throw std::invalid_argument(std::string(who) +
                                "min_divisions >= 1, max_levels >= 0, max_triangles >= 0 and samples >= 2 are required");
  }
}

// Interior parameters in `direction` where the surface has a real tangent discontinuity (a crease: turning angle above
// about 1.1 degrees across a C0 knot). Smooth-but-C0-parametrised knots (an exact rational circle's double knots) are
// not creases.
std::vector<double> CreaseParameters(const NurbsSurface& surface, int direction) {
  const ON_NurbsSurface& raw = surface.raw();
  std::vector<double> out;
  const int degree = raw.Degree(direction);
  const int knot_count = raw.KnotCount(direction);
  const ON_Interval domain = raw.Domain(direction);
  const ON_Interval other = raw.Domain(1 - direction);
  auto point = [&](double along, double across) {
    return direction == 0 ? surface.PointAt(along, across) : surface.PointAt(across, along);
  };
  int i = 0;
  while (i < knot_count) {
    int j = i;
    while (j + 1 < knot_count && raw.Knot(direction, j + 1) == raw.Knot(direction, i)) ++j;
    const double k = raw.Knot(direction, i);
    const int multiplicity = j - i + 1;
    i = j + 1;
    if (!(k > domain.Min() && k < domain.Max()) || multiplicity < degree) continue;
    double lo = domain.Min(), hi = domain.Max();
    for (int m = 0; m < knot_count; ++m) {
      const double km = raw.Knot(direction, m);
      if (km < k) lo = std::max(lo, km);
      if (km > k) hi = std::min(hi, km);
    }
    const double h = 1e-3 * std::min(k - lo, hi - k);
    bool crease = false;
    for (int s = 1; s <= 5 && !crease; ++s) {
      const double across = other.ParameterAt(static_cast<double>(s) / 6.0);
      const Vector3d before = point(k, across) - point(k - h, across);
      const Vector3d after = point(k + h, across) - point(k, across);
      const double lb = before.Length(), la = after.Length();
      if (!(lb > 1e-12) || !(la > 1e-12)) continue;
      const double cos_turn = (before.x * after.x + before.y * after.y + before.z * after.z) / (lb * la);
      if (cos_turn < std::cos(0.02)) crease = true;
    }
    if (crease) out.push_back(k);
  }
  return out;
}

// Largest 3D length of the `direction` isocurve over [a, b], over a handful of positions across the other direction.
double SpanLength(const NurbsSurface& surface, int direction, double a, double b) {
  const ON_Interval other = surface.raw().Domain(1 - direction);
  double best = 0.0;
  for (int s = 0; s <= 4; ++s) {
    const double across = other.ParameterAt(static_cast<double>(s) / 4.0);
    double len = 0.0;
    Point3d prev;
    const int segments = 12;
    for (int k = 0; k <= segments; ++k) {
      const double t = a + (b - a) * static_cast<double>(k) / segments;
      const Point3d p = direction == 0 ? surface.PointAt(t, across) : surface.PointAt(across, t);
      if (k > 0) len += p.DistanceTo(prev);
      prev = p;
    }
    best = std::max(best, len);
  }
  return best;
}

class Refiner {
 public:
  Refiner(const NurbsSurface& surface, const RegionRefineOptions& opt) : s_(surface), o_(opt) {
    const ON_Interval du = s_.raw().Domain(0), dv = s_.raw().Domain(1);
    umin_ = du.Min();
    umax_ = du.Max();
    vmin_ = dv.Min();
    vmax_ = dv.Max();
    closed_u_ = s_.IsClosed(0);
    closed_v_ = s_.IsClosed(1);
  }

  Mesh Run(RegionRefineReport* report);

 private:
  using Key = std::pair<double, double>;

  int AddNode(double u, double v) {
    const auto it = node_ids_.find({u, v});
    if (it != node_ids_.end()) return it->second;
    Node n;
    n.u = u;
    n.v = v;
    n.p = s_.PointAt(u, v);
    const double cu = (closed_u_ && u == umax_) ? umin_ : u;
    const double cv = (closed_v_ && v == vmax_) ? vmin_ : v;
    const auto cit = canon_ids_.find({cu, cv});
    if (cit != canon_ids_.end()) {
      n.canon = cit->second;
    } else {
      n.canon = static_cast<int>(canon_ids_.size());
      canon_ids_[{cu, cv}] = n.canon;
    }
    const int id = static_cast<int>(nodes_.size());
    nodes_.push_back(n);
    node_ids_[{u, v}] = id;
    return id;
  }

  static std::uint64_t EdgeKey(int c0, int c1) {
    if (c0 > c1) std::swap(c0, c1);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(c0)) << 32) | static_cast<std::uint32_t>(c1);
  }
  std::uint64_t BaseKey(const Tri& t) const { return EdgeKey(nodes_[static_cast<size_t>(t.b0)].canon, nodes_[static_cast<size_t>(t.b1)].canon); }

  int AddTri(int a, int b0, int b1, int level) {
    Tri t;
    t.a = a;
    t.b0 = b0;
    t.b1 = b1;
    t.level = level;
    const int id = static_cast<int>(tris_.size());
    tris_.push_back(t);
    const int ca = nodes_[static_cast<size_t>(a)].canon, c0 = nodes_[static_cast<size_t>(b0)].canon,
              c1 = nodes_[static_cast<size_t>(b1)].canon;
    edges_[EdgeKey(c0, c1)].push_back(id);
    edges_[EdgeKey(ca, c0)].push_back(id);
    edges_[EdgeKey(ca, c1)].push_back(id);
    fresh_.push_back(id);
    ++alive_;
    return id;
  }

  int Partner(int t, std::uint64_t key) const {
    const auto it = edges_.find(key);
    if (it == edges_.end()) return -1;
    for (const int other : it->second) {
      if (other != t && tris_[static_cast<size_t>(other)].alive) return other;
    }
    return -1;
  }

  void SplitOne(int t) {
    const Tri parent = tris_[static_cast<size_t>(t)];
    const Node& n0 = nodes_[static_cast<size_t>(parent.b0)];
    const Node& n1 = nodes_[static_cast<size_t>(parent.b1)];
    const int m = AddNode((n0.u + n1.u) * 0.5, (n0.v + n1.v) * 0.5);
    tris_[static_cast<size_t>(t)].alive = false;
    --alive_;
    AddTri(m, parent.a, parent.b0, parent.level + 1);
    AddTri(m, parent.b1, parent.a, parent.level + 1);
    max_level_ = std::max(max_level_, parent.level + 1);
  }

  // Bisects t's base edge, first bisecting whatever neighbour does not share that edge as its own base edge so the
  // mesh stays conforming (no T-junction).
  void Refine(int t) {
    for (;;) {
      if (!tris_[static_cast<size_t>(t)].alive) return;
      const std::uint64_t key = BaseKey(tris_[static_cast<size_t>(t)]);
      const int n = Partner(t, key);
      if (n < 0) {
        SplitOne(t);
        return;
      }
      if (BaseKey(tris_[static_cast<size_t>(n)]) == key) {
        SplitOne(t);
        SplitOne(n);
        return;
      }
      Refine(n);
    }
  }

  const Node& NodeWithNormal(int id) {
    Node& n = nodes_[static_cast<size_t>(id)];
    if (!n.normal_done) {
      n.normal_done = true;
      try {
        n.n = s_.NormalAt(n.u, n.v);
        n.normal_valid = n.n.Length() > 0.5;
      } catch (const std::exception&) {
        n.normal_valid = false;
      }
    }
    return n;
  }

  void Wrap(double& u, double& v) const {
    if (closed_u_) {
      const double P = umax_ - umin_;
      while (u > umax_) u -= P;
      while (u < umin_) u += P;
    }
    if (closed_v_) {
      const double P = vmax_ - vmin_;
      while (v > vmax_) v -= P;
      while (v < vmin_) v += P;
    }
  }

  void Evaluate(int id) { Evaluate(tris_[static_cast<size_t>(id)]); }

  void Evaluate(Tri& t) {
    const Node& na = nodes_[static_cast<size_t>(t.a)];
    const Node& nb = nodes_[static_cast<size_t>(t.b0)];
    const Node& nc = nodes_[static_cast<size_t>(t.b1)];
    const V3 pa = FromPoint(na.p), pb = FromPoint(nb.p), pc = FromPoint(nc.p);
    const V3 cr = Cross(Sub(pb, pa), Sub(pc, pa));
    const double area2 = Norm(cr);
    // Corner parameters expressed in one frame: across a closed direction's seam a corner may sit a period away.
    const double ua = na.u, va = na.v;
    double ub = nb.u, vb = nb.v, uc = nc.u, vc = nc.v;
    if (closed_u_) {
      const double P = umax_ - umin_;
      for (double* x : {&ub, &uc}) {
        while (*x - ua > 0.5 * P) *x -= P;
        while (*x - ua < -0.5 * P) *x += P;
      }
    }
    if (closed_v_) {
      const double P = vmax_ - vmin_;
      for (double* x : {&vb, &vc}) {
        while (*x - va > 0.5 * P) *x -= P;
        while (*x - va < -0.5 * P) *x += P;
      }
    }
    t.longest = std::max({Norm(Sub(pb, pc)), Norm(Sub(pc, pa)), Norm(Sub(pa, pb))});
    t.dev = 0.0;
    t.ang = 0.0;
    t.evaluated = true;
    double sev = 0.0;
    if (o_.max_edge_length > 0.0) sev = std::max(sev, t.longest / o_.max_edge_length);
    if (!IsDegenerate(area2, t.longest)) {
      const V3 n = {cr[0] / area2, cr[1] / area2, cr[2] / area2};
      const int k = o_.samples;
      // The three values are read after the loop: node references stay valid (nodes_ only grows in AddNode, which
      // Evaluate never calls).
      for (int i = 0; i <= k; ++i) {
        for (int j = 0; i + j <= k; ++j) {
          if (i == k || j == k || i + j == 0) continue;  // the corners lie on the surface by construction
          const int l = k - i - j;
          const double wa = static_cast<double>(l) / k, wb = static_cast<double>(i) / k, wc = static_cast<double>(j) / k;
          double su = wa * ua + wb * ub + wc * uc, sv = wa * va + wb * vb + wc * vc;
          Wrap(su, sv);
          const V3 sp = FromPoint(s_.PointAt(su, sv));
          // Distance from the surface point to the facet plane, and from the facet point at the same parameters to
          // the surface (first order: the gap along the surface normal there); the larger of the two is the deviation.
          double d = std::fabs(Dot(Sub(sp, pa), n));
          ON_3dPoint on_surface;
          ON_3dVector sn;
          if (s_.raw().EvNormal(su, sv, on_surface, sn) && sn.Length() > 0.5) {
            const V3 facet = {wa * pa[0] + wb * pb[0] + wc * pc[0], wa * pa[1] + wb * pb[1] + wc * pc[1],
                              wa * pa[2] + wb * pb[2] + wc * pc[2]};
            d = std::max(d, std::fabs(Dot(Sub(facet, sp), V3{sn.x, sn.y, sn.z})));
          }
          t.dev = std::max(t.dev, d);
        }
      }
      // The maximum of the (smooth) deviation can fall between lattice samples; for a quadratic bowl sampled at spacing
      // 1/k the miss is at most about 1/k^2 of the peak, so the sampled deviation is inflated by that much.
      sev = std::max(sev, t.dev * (1.0 + 1.0 / (static_cast<double>(k) * k)) / o_.chord_tolerance);
      if (o_.angle_tolerance > 0.0) {
        for (const int c : {t.a, t.b0, t.b1}) {
          const Node& nn = NodeWithNormal(c);
          if (!nn.normal_valid) continue;
          const double d = std::min(1.0, std::max(-1.0, Dot(n, V3{nn.n.x, nn.n.y, nn.n.z})));
          t.ang = std::max(t.ang, std::acos(d));
        }
        sev = std::max(sev, t.ang / (0.5 * o_.angle_tolerance));
      }
    }
    t.severity = sev;
  }

  double AspectOf(const Tri& t) const {
    const V3 pa = FromPoint(nodes_[static_cast<size_t>(t.a)].p), pb = FromPoint(nodes_[static_cast<size_t>(t.b0)].p),
             pc = FromPoint(nodes_[static_cast<size_t>(t.b1)].p);
    const double la = Norm(Sub(pb, pc)), lb = Norm(Sub(pc, pa)), lc = Norm(Sub(pa, pb));
    const double area2 = Norm(Cross(Sub(pb, pa), Sub(pc, pa)));
    if (IsDegenerate(area2, std::max({la, lb, lc}))) return std::numeric_limits<double>::infinity();
    const double area = 0.5 * area2, sp = 0.5 * (la + lb + lc);
    return la * lb * lc * sp / (8.0 * area * area);
  }

  V3 NormalOf(const Tri& t) const {
    const V3 pa = FromPoint(nodes_[static_cast<size_t>(t.a)].p);
    const V3 c = Cross(Sub(FromPoint(nodes_[static_cast<size_t>(t.b0)].p), pa), Sub(FromPoint(nodes_[static_cast<size_t>(t.b1)].p), pa));
    const double l = Norm(c);
    return l > 0 ? V3{c[0] / l, c[1] / l, c[2] / l} : V3{0, 0, 0};
  }

  // Edge flips on the finished mesh: an interior edge (both triangles hold the same two nodes, so seams and pole rows
  // are left alone) is replaced by the other diagonal of its quad when that lowers the worse aspect ratio of the pair,
  // keeps both normals on the same side, and every control still holds for the two new triangles. Conformity is kept
  // (a flip never adds a vertex) and the refinement hierarchy is no longer needed.
  void ImproveAspectByFlips() {
    for (int pass = 0; pass < 8; ++pass) {
      std::map<std::pair<int, int>, std::vector<int>> by_edge;
      auto canon = [&](int node) { return nodes_[static_cast<size_t>(node)].canon; };
      for (size_t i = 0; i < tris_.size(); ++i) {
        if (!tris_[i].alive) continue;
        const int v[3] = {tris_[i].a, tris_[i].b0, tris_[i].b1};
        for (int k = 0; k < 3; ++k) {
          int p = canon(v[k]), q = canon(v[(k + 1) % 3]);
          if (p > q) std::swap(p, q);
          by_edge[{p, q}].push_back(static_cast<int>(i));
        }
      }
      int flips = 0;
      std::vector<char> touched(tris_.size(), 0);
      for (const auto& e : by_edge) {
        if (e.second.size() != 2) continue;
        const int i1 = e.second[0], i2 = e.second[1];
        if (touched[static_cast<size_t>(i1)] || touched[static_cast<size_t>(i2)]) continue;
        const Tri t1 = tris_[static_cast<size_t>(i1)];
        const Tri t2 = tris_[static_cast<size_t>(i2)];
        const int cp = e.first.first, cq = e.first.second;
        const int v1[3] = {t1.a, t1.b0, t1.b1}, v2[3] = {t2.a, t2.b0, t2.b1};
        int c = -1, d = -1, first = -1, second = -1, first2 = -1, second2 = -1;
        for (int k = 0; k < 3; ++k) {
          const int x = v1[k], y = v1[(k + 1) % 3];
          if ((canon(x) == cp && canon(y) == cq) || (canon(x) == cq && canon(y) == cp)) {
            first = x;
            second = y;
            c = v1[(k + 2) % 3];
          }
        }
        for (int k = 0; k < 3; ++k) {
          const int x = v2[k], y = v2[(k + 1) % 3];
          if (canon(x) == canon(second) && canon(y) == canon(first)) {
            second2 = x;
            first2 = y;
            d = v2[(k + 2) % 3];
          }
        }
        if (first < 0 || d < 0 || c == d || canon(c) == canon(d)) continue;
        // t1 = (first, second, c) and t2 = (second, first, d), both counter-clockwise.
        Tri n1, n2;
        n1.a = c; n1.b0 = first; n1.b1 = d;
        n2.a = d; n2.b0 = second2; n2.b1 = c;
        (void)first2;
        const double old_worst = std::max(AspectOf(t1), AspectOf(t2));
        const double new_worst = std::max(AspectOf(n1), AspectOf(n2));
        if (!(new_worst < 0.9 * old_worst)) continue;
        const V3 o1 = NormalOf(t1), o2 = NormalOf(t2), m1 = NormalOf(n1), m2 = NormalOf(n2);
        if (Dot(m1, o1) <= 0.5 || Dot(m1, o2) <= 0.5 || Dot(m2, o1) <= 0.5 || Dot(m2, o2) <= 0.5) continue;
        Evaluate(n1);
        Evaluate(n2);
        if (n1.severity > 1.0 || n2.severity > 1.0) continue;
        n1.level = n2.level = std::max(t1.level, t2.level);
        touched[static_cast<size_t>(i1)] = touched[static_cast<size_t>(i2)] = 1;
        tris_[static_cast<size_t>(i1)].alive = false;
        tris_[static_cast<size_t>(i2)].alive = false;
        alive_ -= 2;
        const int j1 = AddTri(n1.a, n1.b0, n1.b1, n1.level);
        const int j2 = AddTri(n2.a, n2.b0, n2.b1, n2.level);
        tris_[static_cast<size_t>(j1)] = n1;
        tris_[static_cast<size_t>(j2)] = n2;
        fresh_.clear();
        ++flips;
        touched.resize(tris_.size(), 1);
      }
      if (flips == 0) break;
    }
  }

  // Last pass on the welded mesh: a sliver (aspect ratio above 6) that the node-level flips could not reach, because
  // its neighbour across a pole / apex edge was a collapsed triangle, is removed by flipping an edge of the welded
  // mesh. A flip is kept only when it lowers the pair's worse aspect ratio, keeps the normals on one side, and the
  // new triangles' sample points are within the chord tolerance of the surface (measured by closest-point search).
  void FlipWeldedSlivers(std::vector<std::array<int, 3>>& faces, const std::vector<ON_3dPoint>& pos) {
    auto aspect = [&](int a, int b, int c) {
      Tri t;
      const V3 pa = FromPoint(pos[static_cast<size_t>(a)]), pb = FromPoint(pos[static_cast<size_t>(b)]),
               pc = FromPoint(pos[static_cast<size_t>(c)]);
      const double la = Norm(Sub(pb, pc)), lb = Norm(Sub(pc, pa)), lc = Norm(Sub(pa, pb));
      const double area2 = Norm(Cross(Sub(pb, pa), Sub(pc, pa)));
      if (IsDegenerate(area2, std::max({la, lb, lc}))) return std::numeric_limits<double>::infinity();
      const double area = 0.5 * area2, sp = 0.5 * (la + lb + lc);
      return la * lb * lc * sp / (8.0 * area * area);
    };
    auto normal = [&](int a, int b, int c) {
      const V3 pa = FromPoint(pos[static_cast<size_t>(a)]);
      const V3 cr = Cross(Sub(FromPoint(pos[static_cast<size_t>(b)]), pa), Sub(FromPoint(pos[static_cast<size_t>(c)]), pa));
      const double l = Norm(cr);
      return l > 0 ? V3{cr[0] / l, cr[1] / l, cr[2] / l} : V3{0, 0, 0};
    };
    auto within_tolerance = [&](int a, int b, int c) {
      const V3 pa = FromPoint(pos[static_cast<size_t>(a)]), pb = FromPoint(pos[static_cast<size_t>(b)]),
               pc = FromPoint(pos[static_cast<size_t>(c)]);
      const int k = 4;
      for (int i = 0; i <= k; ++i) {
        for (int j = 0; i + j <= k; ++j) {
          if (i == k || j == k || i + j == 0) continue;
          const double wb = static_cast<double>(i) / k, wc = static_cast<double>(j) / k, wa = 1.0 - wb - wc;
          const ON_3dPoint x(wa * pa[0] + wb * pb[0] + wc * pc[0], wa * pa[1] + wb * pb[1] + wc * pc[1],
                             wa * pa[2] + wb * pb[2] + wc * pc[2]);
          const Point2d uv = s_.ClosestPointParameter(x, 24, 24);
          if (s_.PointAt(uv.x, uv.y).DistanceTo(x) > o_.chord_tolerance) return false;
        }
      }
      return true;
    };
    for (int pass = 0; pass < 6; ++pass) {
      std::map<std::pair<int, int>, std::vector<int>> by_edge;
      for (size_t i = 0; i < faces.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
          int p = faces[i][static_cast<size_t>(k)], q = faces[i][static_cast<size_t>((k + 1) % 3)];
          if (p > q) std::swap(p, q);
          by_edge[{p, q}].push_back(static_cast<int>(i));
        }
      }
      std::vector<char> touched(faces.size(), 0);
      int flips = 0;
      for (const auto& e : by_edge) {
        if (e.second.size() != 2) continue;
        const int i1 = e.second[0], i2 = e.second[1];
        if (touched[static_cast<size_t>(i1)] || touched[static_cast<size_t>(i2)]) continue;
        const auto f1 = faces[static_cast<size_t>(i1)], f2 = faces[static_cast<size_t>(i2)];
        int first = -1, second = -1, c = -1, d = -1;
        for (int k = 0; k < 3; ++k) {
          const int x = f1[static_cast<size_t>(k)], y = f1[static_cast<size_t>((k + 1) % 3)];
          if ((x == e.first.first && y == e.first.second) || (x == e.first.second && y == e.first.first)) {
            first = x;
            second = y;
            c = f1[static_cast<size_t>((k + 2) % 3)];
          }
        }
        for (int k = 0; k < 3; ++k) {
          if (f2[static_cast<size_t>(k)] == second && f2[static_cast<size_t>((k + 1) % 3)] == first) {
            d = f2[static_cast<size_t>((k + 2) % 3)];
          }
        }
        if (first < 0 || d < 0 || c == d) continue;
        const double old_worst = std::max(aspect(first, second, c), aspect(second, first, d));
        if (!(old_worst > 6.0)) continue;
        const double new_worst = std::max(aspect(c, first, d), aspect(d, second, c));
        if (!(new_worst < 0.9 * old_worst)) continue;
        const V3 o1 = normal(first, second, c), o2 = normal(second, first, d);
        const V3 m1 = normal(c, first, d), m2 = normal(d, second, c);
        if (Dot(m1, o1) <= 0.5 || Dot(m1, o2) <= 0.5 || Dot(m2, o1) <= 0.5 || Dot(m2, o2) <= 0.5) continue;
        if (!within_tolerance(c, first, d) || !within_tolerance(d, second, c)) continue;
        faces[static_cast<size_t>(i1)] = {c, first, d};
        faces[static_cast<size_t>(i2)] = {d, second, c};
        touched[static_cast<size_t>(i1)] = touched[static_cast<size_t>(i2)] = 1;
        ++flips;
      }
      if (flips == 0) break;
    }
  }

  const NurbsSurface& s_;
  const RegionRefineOptions& o_;
  double umin_ = 0, umax_ = 0, vmin_ = 0, vmax_ = 0;
  bool closed_u_ = false, closed_v_ = false;
  std::vector<Node> nodes_;
  std::map<Key, int> node_ids_;
  std::map<Key, int> canon_ids_;
  std::vector<Tri> tris_;
  std::unordered_map<std::uint64_t, std::vector<int>> edges_;
  std::vector<int> fresh_;
  int alive_ = 0;
  int max_level_ = 0;

};

// The starting lattice values of one direction: crease-bounded spans, each subdivided uniformly. The counts are
// balanced across the two directions afterwards by the caller (see Run()).
struct SpanPlan {
  std::vector<double> bounds;   // span boundaries (domain ends and creases)
  std::vector<double> lengths;  // 3D length of each span
  std::vector<int> counts;      // divisions of each span
};

SpanPlan PlanSpans(const NurbsSurface& surface, int direction, int min_divisions, bool closed) {
  SpanPlan plan;
  const ON_Interval domain = surface.raw().Domain(direction);
  plan.bounds.push_back(domain.Min());
  for (const double c : CreaseParameters(surface, direction)) plan.bounds.push_back(c);
  plan.bounds.push_back(domain.Max());
  std::sort(plan.bounds.begin(), plan.bounds.end());
  const int spans = static_cast<int>(plan.bounds.size()) - 1;
  for (int i = 0; i < spans; ++i) {
    plan.lengths.push_back(SpanLength(surface, direction, plan.bounds[static_cast<size_t>(i)], plan.bounds[static_cast<size_t>(i + 1)]));
    plan.counts.push_back(min_divisions);
  }
  // A closed direction needs at least 3 divisions: with 2, the two edges of a row would share both seam-wrapped end
  // points and be indistinguishable to the conforming-edge lookup.
  if (closed) {
    int total = 0;
    for (const int c : plan.counts) total += c;
    while (total < 3) {
      ++plan.counts[0];
      ++total;
    }
  }
  return plan;
}

double MaxCell(const SpanPlan& p, int* arg) {
  double best = -1.0;
  for (size_t i = 0; i < p.counts.size(); ++i) {
    const double c = p.lengths[i] / p.counts[i];
    if (c > best) {
      best = c;
      *arg = static_cast<int>(i);
    }
  }
  return best;
}

std::vector<double> Values(const SpanPlan& p) {
  std::vector<double> out;
  for (size_t i = 0; i + 1 < p.bounds.size(); ++i) {
    const double a = p.bounds[i], b = p.bounds[i + 1];
    const int n = p.counts[i];
    for (int k = 0; k < n; ++k) out.push_back(k == 0 ? a : a + (b - a) * static_cast<double>(k) / n);
  }
  out.push_back(p.bounds.back());
  return out;
}

Mesh Refiner::Run(RegionRefineReport* report) {
  SpanPlan pu = PlanSpans(s_, 0, o_.min_divisions, closed_u_);
  SpanPlan pv = PlanSpans(s_, 1, o_.min_divisions, closed_v_);
  // Balance the starting cells' 3D aspect ratio: coarsen-never, only add divisions to the direction whose cells are
  // too long.
  for (int guard = 0; guard < 4096; ++guard) {
    int iu = 0, iv = 0;
    const double hu = MaxCell(pu, &iu), hv = MaxCell(pv, &iv);
    if (!(hu > 0.0) || !(hv > 0.0)) break;
    if (hu > o_.max_aspect_ratio * hv) {
      ++pu.counts[static_cast<size_t>(iu)];
    } else if (hv > o_.max_aspect_ratio * hu) {
      ++pv.counts[static_cast<size_t>(iv)];
    } else {
      break;
    }
  }
  const std::vector<double> uv = Values(pu), vv = Values(pv);
  const int nu = static_cast<int>(uv.size()), nv = static_cast<int>(vv.size());
  std::vector<int> grid(static_cast<size_t>(nu) * static_cast<size_t>(nv));
  for (int i = 0; i < nu; ++i) {
    for (int j = 0; j < nv; ++j) grid[static_cast<size_t>(i * nv + j)] = AddNode(uv[static_cast<size_t>(i)], vv[static_cast<size_t>(j)]);
  }
  for (int i = 0; i + 1 < nu; ++i) {
    for (int j = 0; j + 1 < nv; ++j) {
      const int v00 = grid[static_cast<size_t>(i * nv + j)], v10 = grid[static_cast<size_t>((i + 1) * nv + j)];
      const int v11 = grid[static_cast<size_t>((i + 1) * nv + j + 1)], v01 = grid[static_cast<size_t>(i * nv + j + 1)];
      AddTri(v10, v11, v00, 0);  // (v00, v10, v11) counter-clockwise, hypotenuse v11-v00
      AddTri(v01, v00, v11, 0);  // (v00, v11, v01)
    }
  }
  const int base_count = alive_;

  std::priority_queue<std::pair<double, int>> queue;
  auto consider = [&]() {
    std::vector<int> batch;
    batch.swap(fresh_);
    for (const int id : batch) {
      if (!tris_[static_cast<size_t>(id)].alive) continue;
      Evaluate(id);
      const Tri& t = tris_[static_cast<size_t>(id)];
      if (t.severity > 1.0 && t.level < o_.max_levels) queue.push({t.severity, id});
    }
  };
  consider();
  bool budget_stop = false;
  while (!queue.empty()) {
    const int id = queue.top().second;
    queue.pop();
    if (!tris_[static_cast<size_t>(id)].alive) continue;
    if (o_.max_triangles > 0 && alive_ >= o_.max_triangles) {
      budget_stop = true;
      break;
    }
    Refine(id);
    consider();
  }

  ImproveAspectByFlips();

  // Report and the welded mesh.
  RegionRefineReport rep;
  rep.base_triangle_count = base_count;
  rep.max_level_reached = max_level_;
  bool unmet = false;
  for (const Tri& t : tris_) {
    if (!t.alive) continue;
    rep.achieved_deviation = std::max(rep.achieved_deviation, t.dev);
    rep.achieved_normal_angle = std::max(rep.achieved_normal_angle, t.ang);
    rep.longest_edge = std::max(rep.longest_edge, t.longest);
    if (t.severity > 1.0) {
      unmet = true;
      if (t.level >= o_.max_levels) rep.depth_limited = true;
    }
  }
  rep.controls_met = !unmet;
  rep.budget_exhausted = budget_stop && unmet;

  // Weld nodes that coincide in 3D (the closed-direction seams and the collapsed pole / apex rows).
  double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
  for (const Node& n : nodes_) {
    lo[0] = std::min(lo[0], n.p.x);
    lo[1] = std::min(lo[1], n.p.y);
    lo[2] = std::min(lo[2], n.p.z);
    hi[0] = std::max(hi[0], n.p.x);
    hi[1] = std::max(hi[1], n.p.y);
    hi[2] = std::max(hi[2], n.p.z);
  }
  const double diag = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                (hi[2] - lo[2]) * (hi[2] - lo[2]));
  const double eps = std::max(1e-9 * diag, 1e-300);
  const double cell = 4.0 * eps;
  using Cell = std::array<std::int64_t, 3>;
  std::map<Cell, std::vector<int>> grid_of_reps;
  std::vector<int> weld(nodes_.size(), -1);
  Mesh mesh;
  ON_Mesh& raw = mesh.raw();
  std::vector<ON_3dPoint> reps;
  auto cell_of = [&](const ON_3dPoint& p) {
    return Cell{static_cast<std::int64_t>(std::floor(p.x / cell)), static_cast<std::int64_t>(std::floor(p.y / cell)),
                static_cast<std::int64_t>(std::floor(p.z / cell))};
  };
  auto vertex_of = [&](int node) {
    int& w = weld[static_cast<size_t>(node)];
    if (w >= 0) return w;
    const ON_3dPoint& p = nodes_[static_cast<size_t>(node)].p;
    const Cell c = cell_of(p);
    for (std::int64_t dx = -1; dx <= 1; ++dx) {
      for (std::int64_t dy = -1; dy <= 1; ++dy) {
        for (std::int64_t dz = -1; dz <= 1; ++dz) {
          const auto it = grid_of_reps.find(Cell{c[0] + dx, c[1] + dy, c[2] + dz});
          if (it == grid_of_reps.end()) continue;
          for (const int r : it->second) {
            if (reps[static_cast<size_t>(r)].DistanceTo(p) <= eps) return w = r;
          }
        }
      }
    }
    w = static_cast<int>(reps.size());
    reps.push_back(p);
    grid_of_reps[c].push_back(w);
    raw.m_V.Append(ON_3fPoint(p));
    return w;
  };
  std::vector<std::array<int, 3>> faces;
  for (const Tri& t : tris_) {
    if (!t.alive) continue;
    const int a = vertex_of(t.a), b = vertex_of(t.b0), c = vertex_of(t.b1);
    if (a == b || b == c || a == c) continue;
    faces.push_back({a, b, c});
  }
  FlipWeldedSlivers(faces, reps);
  for (const auto& f3 : faces) {
    ON_MeshFace f;
    f.vi[0] = f3[0];
    f.vi[1] = f3[1];
    f.vi[2] = f3[2];
    f.vi[3] = f3[2];
    raw.m_F.Append(f);
  }
  rep.triangle_count = raw.m_F.Count();
  if (report != nullptr) *report = rep;
  return mesh;
}

}  // namespace

Mesh TessellateSurfaceRegionAdaptive(const NurbsSurface& surface, const RegionRefineOptions& options,
                                     RegionRefineReport* report) {
  ValidateOptions(options);
  Refiner refiner(surface, options);
  return refiner.Run(report);
}

}  // namespace dino8::kernel
