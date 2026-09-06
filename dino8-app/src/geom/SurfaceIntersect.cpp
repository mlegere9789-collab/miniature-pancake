#include "geom/SurfaceIntersect.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

namespace dino8::app {

namespace {

constexpr double kPi = 3.14159265358979323846;

double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// --- dense linear algebra ----------------------------------------------------

// Solves A x = b (n x n, row-major) with partial pivoting. Returns false if singular.
bool SolveDense(std::vector<double> A, std::vector<double> b, int n, std::vector<double>& x) {
  for (int k = 0; k < n; ++k) {
    int piv = k;
    for (int r = k + 1; r < n; ++r) if (std::fabs(A[static_cast<size_t>(r) * n + k]) > std::fabs(A[static_cast<size_t>(piv) * n + k])) piv = r;
    if (std::fabs(A[static_cast<size_t>(piv) * n + k]) < 1e-300) return false;
    if (piv != k) {
      for (int c = 0; c < n; ++c) std::swap(A[static_cast<size_t>(k) * n + c], A[static_cast<size_t>(piv) * n + c]);
      std::swap(b[static_cast<size_t>(k)], b[static_cast<size_t>(piv)]);
    }
    for (int r = k + 1; r < n; ++r) {
      const double f = A[static_cast<size_t>(r) * n + k] / A[static_cast<size_t>(k) * n + k];
      if (f == 0) continue;
      for (int c = k; c < n; ++c) A[static_cast<size_t>(r) * n + c] -= f * A[static_cast<size_t>(k) * n + c];
      b[static_cast<size_t>(r)] -= f * b[static_cast<size_t>(k)];
    }
  }
  x.assign(static_cast<size_t>(n), 0);
  for (int r = n - 1; r >= 0; --r) {
    double s = b[static_cast<size_t>(r)];
    for (int c = r + 1; c < n; ++c) s -= A[static_cast<size_t>(r) * n + c] * x[static_cast<size_t>(c)];
    x[static_cast<size_t>(r)] = s / A[static_cast<size_t>(r) * n + r];
  }
  return true;
}

double Norm(const std::vector<double>& v) {
  double s = 0;
  for (double x : v) s += x * x;
  return std::sqrt(s);
}

// --- tessellation ------------------------------------------------------------

int DivisionsFor(const ON_Surface& s, int dir, const IntersectOptions& opt) {
  const ON_Interval d = s.Domain(dir), o = s.Domain(1 - dir);
  double best = opt.min_mesh_divisions;
  const int m = 48;
  for (int k = 0; k < 5; ++k) {
    const double oc = o.ParameterAt((k + 0.5) / 5);
    double L = 0, turn = 0;
    Vector3d prev(0, 0, 0);
    bool have_prev = false;
    Point3d last = dir == 0 ? s.PointAt(d.ParameterAt(0), oc) : s.PointAt(oc, d.ParameterAt(0));
    for (int i = 1; i <= m; ++i) {
      const double t = d.ParameterAt(static_cast<double>(i) / m);
      const Point3d p = dir == 0 ? s.PointAt(t, oc) : s.PointAt(oc, t);
      Vector3d seg = p - last;
      const double len = seg.Length();
      L += len;
      if (len > 1e-12) {
        seg /= len;
        if (have_prev) {
          const double c = Clamp(ON_DotProduct(prev, seg), -1, 1);
          turn += std::acos(c);
        }
        prev = seg;
        have_prev = true;
      }
      last = p;
    }
    if (L <= 0) continue;
    double n = std::sqrt(L * turn / (8 * std::max(opt.mesh_tolerance, 1e-9)));
    n = std::max(n, turn / (kPi / 12));
    best = std::max(best, n);
  }
  return static_cast<int>(Clamp(std::ceil(best), opt.min_mesh_divisions, opt.max_mesh_divisions));
}

}  // namespace

SurfaceMesh TessellateWithUV(const ON_Surface& s, const IntersectOptions& opt) {
  SurfaceMesh m;
  const int nu = DivisionsFor(s, 0, opt), nv = DivisionsFor(s, 1, opt);
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  m.pts.reserve(static_cast<size_t>((nu + 1) * (nv + 1)));
  m.uv.reserve(m.pts.capacity());
  for (int j = 0; j <= nv; ++j) {
    const double v = dv.ParameterAt(static_cast<double>(j) / nv);
    for (int i = 0; i <= nu; ++i) {
      const double u = du.ParameterAt(static_cast<double>(i) / nu);
      const Point3d p = s.PointAt(u, v);
      m.pts.push_back(p);
      m.uv.emplace_back(u, v);
      m.bbox.Set(p, true);
    }
  }
  auto id = [&](int i, int j) { return j * (nu + 1) + i; };
  for (int j = 0; j < nv; ++j)
    for (int i = 0; i < nu; ++i) {
      const int a = id(i, j), b = id(i + 1, j), c = id(i + 1, j + 1), d = id(i, j + 1);
      // Split along the shorter diagonal for better-shaped triangles.
      if (m.pts[static_cast<size_t>(a)].DistanceTo(m.pts[static_cast<size_t>(c)]) <= m.pts[static_cast<size_t>(b)].DistanceTo(m.pts[static_cast<size_t>(d)])) {
        m.tris.push_back({a, b, c});
        m.tris.push_back({a, c, d});
      } else {
        m.tris.push_back({a, b, d});
        m.tris.push_back({b, c, d});
      }
    }
  return m;
}

namespace {

// --- triangle / triangle ------------------------------------------------------

struct SegEnd {
  Point3d p;
  ON_2dPoint uva, uvb;
};
struct Seg {
  SegEnd a, b;
};

ON_2dPoint Lerp(const ON_2dPoint& a, const ON_2dPoint& b, double t) { return ON_2dPoint(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }

struct PlaneCross {
  Point3d p[2];
  ON_2dPoint uv[2];
  bool ok = false;
};

// Where triangle (p, uv) crosses the plane (n, through q0). Vertices within
// eps of the plane count as being on the positive side.
PlaneCross CrossPlane(const Point3d* p, const ON_2dPoint* uv, const Vector3d& n, const Point3d& q0, double eps) {
  PlaneCross out;
  double s[3];
  int pos = 0, neg = 0;
  for (int i = 0; i < 3; ++i) {
    s[i] = ON_DotProduct(p[i] - q0, n);
    if (s[i] > -eps && s[i] < eps) s[i] = eps;
    if (s[i] > 0) ++pos; else ++neg;
  }
  if (pos == 0 || neg == 0) return out;
  int k = 0;
  for (int i = 0; i < 3 && k < 2; ++i) {
    const int j = (i + 1) % 3;
    if ((s[i] > 0) == (s[j] > 0)) continue;
    const double t = s[i] / (s[i] - s[j]);
    out.p[k] = p[i] + (p[j] - p[i]) * t;
    out.uv[k] = Lerp(uv[i], uv[j], t);
    ++k;
  }
  out.ok = k == 2;
  return out;
}

bool TriTri(const SurfaceMesh& A, int ta, const SurfaceMesh& B, int tb, double eps, Seg& out) {
  Point3d p[3], q[3];
  ON_2dPoint pu[3], qu[3];
  for (int i = 0; i < 3; ++i) {
    p[i] = A.pts[static_cast<size_t>(A.tris[static_cast<size_t>(ta)][static_cast<size_t>(i)])];
    pu[i] = A.uv[static_cast<size_t>(A.tris[static_cast<size_t>(ta)][static_cast<size_t>(i)])];
    q[i] = B.pts[static_cast<size_t>(B.tris[static_cast<size_t>(tb)][static_cast<size_t>(i)])];
    qu[i] = B.uv[static_cast<size_t>(B.tris[static_cast<size_t>(tb)][static_cast<size_t>(i)])];
  }
  Vector3d nA = ON_CrossProduct(p[1] - p[0], p[2] - p[0]), nB = ON_CrossProduct(q[1] - q[0], q[2] - q[0]);
  if (!nA.Unitize() || !nB.Unitize()) return false;
  Vector3d dir = ON_CrossProduct(nA, nB);
  if (dir.Length() < 1e-9) return false;  // coplanar (or parallel): handled by neighbours
  dir.Unitize();
  const PlaneCross ca = CrossPlane(p, pu, nB, q[0], eps);
  if (!ca.ok) return false;
  const PlaneCross cb = CrossPlane(q, qu, nA, p[0], eps);
  if (!cb.ok) return false;
  double ta0 = ON_DotProduct(ca.p[0] - Point3d::Origin, dir), ta1 = ON_DotProduct(ca.p[1] - Point3d::Origin, dir);
  double tb0 = ON_DotProduct(cb.p[0] - Point3d::Origin, dir), tb1 = ON_DotProduct(cb.p[1] - Point3d::Origin, dir);
  int ia0 = 0, ia1 = 1, ib0 = 0, ib1 = 1;
  if (ta0 > ta1) { std::swap(ta0, ta1); std::swap(ia0, ia1); }
  if (tb0 > tb1) { std::swap(tb0, tb1); std::swap(ib0, ib1); }
  const double t0 = std::max(ta0, tb0), t1 = std::min(ta1, tb1);
  if (t1 - t0 <= eps) return false;
  auto uv_along = [](const PlaneCross& c, int i0, int i1, double s0, double s1, double t) {
    const double f = s1 - s0 > 1e-300 ? (t - s0) / (s1 - s0) : 0;
    return Lerp(c.uv[i0], c.uv[i1], Clamp(f, 0, 1));
  };
  auto make_end = [&](double t) {
    SegEnd e;
    if (ta0 >= tb0 ? t == t0 : false) { /* placeholder, resolved below */ }
    return e;
  };
  (void)make_end;
  SegEnd e0, e1;
  if (ta0 >= tb0) { e0.p = ca.p[ia0]; e0.uva = ca.uv[ia0]; e0.uvb = uv_along(cb, ib0, ib1, tb0, tb1, t0); }
  else { e0.p = cb.p[ib0]; e0.uvb = cb.uv[ib0]; e0.uva = uv_along(ca, ia0, ia1, ta0, ta1, t0); }
  if (ta1 <= tb1) { e1.p = ca.p[ia1]; e1.uva = ca.uv[ia1]; e1.uvb = uv_along(cb, ib0, ib1, tb0, tb1, t1); }
  else { e1.p = cb.p[ib1]; e1.uvb = cb.uv[ib1]; e1.uva = uv_along(ca, ia0, ia1, ta0, ta1, t1); }
  out.a = e0;
  out.b = e1;
  return true;
}

// --- uniform grid over a mesh -----------------------------------------------------

struct Grid {
  ON_BoundingBox box;
  int n[3] = {1, 1, 1};
  double cell[3] = {1, 1, 1};
  std::vector<std::vector<int>> cells;
  void Build(const SurfaceMesh& m, const ON_BoundingBox& region) {
    box = region;
    int inside = 0;
    for (size_t t = 0; t < m.tris.size(); ++t) if (TriBox(m, static_cast<int>(t)).IsValid()) ++inside;
    const double target = std::max(1.0, std::cbrt(static_cast<double>(std::max(inside, 1))));
    for (int k = 0; k < 3; ++k) {
      const double ext = box.m_max[k] - box.m_min[k];
      n[k] = static_cast<int>(Clamp(std::ceil(target), 1, 48));
      cell[k] = ext > 0 ? ext / n[k] : 1;
      if (ext <= 0) n[k] = 1;
    }
    cells.assign(static_cast<size_t>(n[0]) * n[1] * n[2], {});
    for (size_t t = 0; t < m.tris.size(); ++t) {
      ON_BoundingBox tb = TriBox(m, static_cast<int>(t));
      if (!tb.IsValid()) continue;
      ON_BoundingBox c;
      if (!c.Intersection(tb, box)) continue;
      int lo[3], hi[3];
      Range(c, lo, hi);
      for (int z = lo[2]; z <= hi[2]; ++z) for (int y = lo[1]; y <= hi[1]; ++y) for (int x = lo[0]; x <= hi[0]; ++x) cells[Index(x, y, z)].push_back(static_cast<int>(t));
    }
  }
  static ON_BoundingBox TriBox(const SurfaceMesh& m, int t) {
    ON_BoundingBox b;
    for (int k = 0; k < 3; ++k) b.Set(m.pts[static_cast<size_t>(m.tris[static_cast<size_t>(t)][static_cast<size_t>(k)])], true);
    return b;
  }
  size_t Index(int x, int y, int z) const { return (static_cast<size_t>(z) * n[1] + y) * n[0] + x; }
  void Range(const ON_BoundingBox& b, int* lo, int* hi) const {
    for (int k = 0; k < 3; ++k) {
      lo[k] = static_cast<int>(Clamp(std::floor((b.m_min[k] - box.m_min[k]) / cell[k]), 0, n[k] - 1));
      hi[k] = static_cast<int>(Clamp(std::floor((b.m_max[k] - box.m_min[k]) / cell[k]), 0, n[k] - 1));
    }
  }
  template <typename F>
  void Query(const ON_BoundingBox& b, std::vector<int>& stamp, int mark, F&& f) const {
    ON_BoundingBox c;
    if (!c.Intersection(b, box)) return;
    int lo[3], hi[3];
    Range(c, lo, hi);
    for (int z = lo[2]; z <= hi[2]; ++z) for (int y = lo[1]; y <= hi[1]; ++y) for (int x = lo[0]; x <= hi[0]; ++x)
      for (int t : cells[Index(x, y, z)]) { if (stamp[static_cast<size_t>(t)] == mark) continue; stamp[static_cast<size_t>(t)] = mark; f(t); }
  }
};

std::vector<Seg> MeshSegments(const SurfaceMesh& A, const SurfaceMesh& B, double eps) {
  std::vector<Seg> segs;
  ON_BoundingBox region;
  if (!region.Intersection(A.bbox, B.bbox)) return segs;
  const double pad = std::max(eps * 100, 1e-9 * region.Diagonal().Length());
  region.m_min -= ON_3dVector(pad, pad, pad);
  region.m_max += ON_3dVector(pad, pad, pad);
  Grid grid;
  grid.Build(B, region);
  std::vector<int> stamp(B.tris.size(), -1);
  int mark = 0;
  for (size_t ta = 0; ta < A.tris.size(); ++ta) {
    ON_BoundingBox tb = Grid::TriBox(A, static_cast<int>(ta));
    ++mark;
    grid.Query(tb, stamp, mark, [&](int tbi) {
      Seg s;
      if (TriTri(A, static_cast<int>(ta), B, tbi, eps, s)) segs.push_back(s);
    });
  }
  return segs;
}

// --- chaining ------------------------------------------------------------------

struct SeedPoint {
  Point3d p;
  ON_2dPoint uva, uvb;
};
struct SeedPolyline {
  std::vector<SeedPoint> pts;
  bool closed = false;
};

std::vector<SeedPolyline> ChainSegments(const std::vector<Seg>& segs, double eps) {
  std::vector<SeedPolyline> out;
  const size_t n = segs.size();
  if (n == 0) return out;
  // Unify endpoints: sort by x, union-find over close pairs.
  std::vector<int> parent(2 * n);
  std::iota(parent.begin(), parent.end(), 0);
  std::function<int(int)> find = [&](int i) { while (parent[static_cast<size_t>(i)] != i) { parent[static_cast<size_t>(i)] = parent[static_cast<size_t>(parent[static_cast<size_t>(i)])]; i = parent[static_cast<size_t>(i)]; } return i; };
  auto unite = [&](int a, int b) { a = find(a); b = find(b); if (a != b) parent[static_cast<size_t>(a)] = b; };
  auto pt = [&](int k) -> const Point3d& { return k % 2 == 0 ? segs[static_cast<size_t>(k / 2)].a.p : segs[static_cast<size_t>(k / 2)].b.p; };
  std::vector<int> order(2 * n);
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return pt(a).x < pt(b).x; });
  for (size_t i = 0; i < order.size(); ++i) {
    for (size_t j = i + 1; j < order.size(); ++j) {
      if (pt(order[j]).x - pt(order[i]).x > eps) break;
      if (pt(order[i]).DistanceTo(pt(order[j])) <= eps) unite(order[i], order[j]);
    }
  }
  std::map<int, std::vector<int>> adj;  // representative -> segment ids
  for (size_t s = 0; s < n; ++s) {
    adj[find(static_cast<int>(2 * s))].push_back(static_cast<int>(s));
    adj[find(static_cast<int>(2 * s + 1))].push_back(static_cast<int>(s));
  }
  std::vector<char> used(n, 0);
  auto end_of = [&](int s, int rep) -> const SegEnd& {
    // The end of segment s that is NOT at rep.
    return find(2 * s) == rep ? segs[static_cast<size_t>(s)].b : segs[static_cast<size_t>(s)].a;
  };
  auto start_of = [&](int s, int rep) -> const SegEnd& { return find(2 * s) == rep ? segs[static_cast<size_t>(s)].a : segs[static_cast<size_t>(s)].b; };
  auto walk = [&](int start_seg, int start_rep) {
    SeedPolyline pl;
    int rep = start_rep, s = start_seg;
    const SegEnd& e0 = start_of(s, rep);
    pl.pts.push_back({e0.p, e0.uva, e0.uvb});
    while (true) {
      used[static_cast<size_t>(s)] = 1;
      const SegEnd& e = end_of(s, rep);
      pl.pts.push_back({e.p, e.uva, e.uvb});
      const int next_rep = find(2 * s) == rep ? find(2 * s + 1) : find(2 * s);
      if (next_rep == start_rep) { pl.closed = true; break; }
      const std::vector<int>& around = adj[next_rep];
      if (around.size() != 2) break;  // open end or junction
      const int ns = around[0] == s ? around[1] : around[0];
      if (used[static_cast<size_t>(ns)]) break;
      rep = next_rep;
      s = ns;
    }
    if (pl.closed && pl.pts.size() > 1) pl.pts.pop_back();
    if (pl.pts.size() >= 2) out.push_back(std::move(pl));
  };
  // Open chains first (from vertices that are not degree 2), then loops.
  for (const auto& [rep, list] : adj) {
    if (list.size() == 2) continue;
    for (int s : list) if (!used[static_cast<size_t>(s)]) walk(s, rep);
  }
  for (size_t s = 0; s < n; ++s) if (!used[s]) walk(static_cast<int>(s), find(static_cast<int>(2 * s)));
  return out;
}

double SurfaceScale(const ON_Surface& s) {
  ON_BoundingBox b = s.BoundingBox();
  const double d = b.IsValid() ? b.Diagonal().Length() : 1;
  return d > 0 ? d : 1;
}

}  // namespace

// --- Newton ----------------------------------------------------------------------

bool NewtonSolve(const Residual& residual, std::vector<double>& x, const std::vector<double>& lo, const std::vector<double>& hi, double tol, int max_iter, double* final_norm) {
  const int n = static_cast<int>(x.size());
  auto clamp_x = [&](std::vector<double>& v) { for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = Clamp(v[static_cast<size_t>(i)], lo[static_cast<size_t>(i)], hi[static_cast<size_t>(i)]); };
  clamp_x(x);
  std::vector<double> r = residual(x);
  const int m = static_cast<int>(r.size());
  double norm = Norm(r);
  for (int it = 0; it < max_iter; ++it) {
    if (norm <= tol) { if (final_norm) *final_norm = norm; return true; }
    // Jacobian by central differences (one-sided at the bounds).
    std::vector<double> J(static_cast<size_t>(m) * n);
    for (int j = 0; j < n; ++j) {
      const double range = hi[static_cast<size_t>(j)] - lo[static_cast<size_t>(j)];
      const double h = std::max(1e-7 * (range > 0 ? range : 1.0), 1e-10);
      std::vector<double> xp = x, xm = x;
      xp[static_cast<size_t>(j)] = std::min(x[static_cast<size_t>(j)] + h, hi[static_cast<size_t>(j)]);
      xm[static_cast<size_t>(j)] = std::max(x[static_cast<size_t>(j)] - h, lo[static_cast<size_t>(j)]);
      const double dh = xp[static_cast<size_t>(j)] - xm[static_cast<size_t>(j)];
      if (dh <= 0) continue;
      std::vector<double> rp = residual(xp), rm = residual(xm);
      for (int i = 0; i < m; ++i) J[static_cast<size_t>(i) * n + j] = (rp[static_cast<size_t>(i)] - rm[static_cast<size_t>(i)]) / dh;
    }
    std::vector<double> delta(static_cast<size_t>(n), 0);
    bool solved = false;
    if (m >= n) {
      std::vector<double> A(static_cast<size_t>(n) * n, 0), b(static_cast<size_t>(n), 0);
      double trace = 0;
      for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) { double s = 0; for (int k = 0; k < m; ++k) s += J[static_cast<size_t>(k) * n + i] * J[static_cast<size_t>(k) * n + j]; A[static_cast<size_t>(i) * n + j] = s; if (i == j) trace += s; }
      for (int i = 0; i < n; ++i) { A[static_cast<size_t>(i) * n + i] += 1e-12 * trace + 1e-300; double s = 0; for (int k = 0; k < m; ++k) s -= J[static_cast<size_t>(k) * n + i] * r[static_cast<size_t>(k)]; b[static_cast<size_t>(i)] = s; }
      solved = SolveDense(A, b, n, delta);
    } else {
      std::vector<double> A(static_cast<size_t>(m) * m, 0), b(static_cast<size_t>(m), 0), lambda;
      double trace = 0;
      for (int i = 0; i < m; ++i) for (int j = 0; j < m; ++j) { double s = 0; for (int k = 0; k < n; ++k) s += J[static_cast<size_t>(i) * n + k] * J[static_cast<size_t>(j) * n + k]; A[static_cast<size_t>(i) * m + j] = s; if (i == j) trace += s; }
      for (int i = 0; i < m; ++i) { A[static_cast<size_t>(i) * m + i] += 1e-12 * trace + 1e-300; b[static_cast<size_t>(i)] = -r[static_cast<size_t>(i)]; }
      solved = SolveDense(A, b, m, lambda);
      if (solved) for (int k = 0; k < n; ++k) { double s = 0; for (int i = 0; i < m; ++i) s += J[static_cast<size_t>(i) * n + k] * lambda[static_cast<size_t>(i)]; delta[static_cast<size_t>(k)] = s; }
    }
    if (!solved) break;
    bool accepted = false;
    double step = 1;
    for (int ls = 0; ls < 8; ++ls, step *= 0.5) {
      std::vector<double> xn = x;
      for (int j = 0; j < n; ++j) xn[static_cast<size_t>(j)] += step * delta[static_cast<size_t>(j)];
      clamp_x(xn);
      std::vector<double> rn = residual(xn);
      const double nn = Norm(rn);
      if (nn < norm) { x = xn; r = rn; norm = nn; accepted = true; break; }
    }
    if (!accepted) break;
  }
  if (final_norm) *final_norm = norm;
  return norm <= tol;
}

bool RefineSurfaceSurfacePoint(const ON_Surface& a, const ON_Surface& b, double& ua, double& va, double& ub, double& vb, double tol, int max_iter) {
  std::vector<double> x = {ua, va, ub, vb};
  const std::vector<double> lo = {a.Domain(0).Min(), a.Domain(1).Min(), b.Domain(0).Min(), b.Domain(1).Min()};
  const std::vector<double> hi = {a.Domain(0).Max(), a.Domain(1).Max(), b.Domain(0).Max(), b.Domain(1).Max()};
  Residual res = [&](const std::vector<double>& p) {
    const Point3d pa = a.PointAt(p[0], p[1]), pb = b.PointAt(p[2], p[3]);
    return std::vector<double>{pa.x - pb.x, pa.y - pb.y, pa.z - pb.z};
  };
  const bool ok = NewtonSolve(res, x, lo, hi, tol, max_iter);
  ua = x[0]; va = x[1]; ub = x[2]; vb = x[3];
  return ok;
}

bool SurfaceClosestPoint(const ON_Surface& s, Point3d p, double& u, double& v, int max_iter) {
  const double scale = SurfaceScale(s) + p.DistanceTo(Point3d::Origin);
  std::vector<double> x = {u, v};
  const std::vector<double> lo = {s.Domain(0).Min(), s.Domain(1).Min()}, hi = {s.Domain(0).Max(), s.Domain(1).Max()};
  Residual res = [&](const std::vector<double>& q) {
    ON_3dPoint P;
    ON_3dVector du, dv;
    s.Ev1Der(q[0], q[1], P, du, dv);
    const Vector3d d = P - p;
    return std::vector<double>{ON_DotProduct(du, d), ON_DotProduct(dv, d)};
  };
  const bool ok = NewtonSolve(res, x, lo, hi, 1e-11 * scale * scale, max_iter);
  u = x[0]; v = x[1];
  return ok;
}

bool SurfaceClosestPointGlobal(const ON_Surface& s, Point3d p, double& u, double& v, int grid) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  double best = std::numeric_limits<double>::max();
  for (int i = 0; i <= grid; ++i)
    for (int j = 0; j <= grid; ++j) {
      const double uu = du.ParameterAt(static_cast<double>(i) / grid), vv = dv.ParameterAt(static_cast<double>(j) / grid);
      const double d = s.PointAt(uu, vv).DistanceTo(p);
      if (d < best) { best = d; u = uu; v = vv; }
    }
  SurfaceClosestPoint(s, p, u, v);
  return true;
}

double CurveClosestParam(const ON_Curve& c, Point3d p, double seed, int max_iter) {
  ON_BoundingBox bb = c.BoundingBox();
  const double scale = (bb.IsValid() ? bb.Diagonal().Length() : 1) + p.DistanceTo(Point3d::Origin) + 1;
  std::vector<double> x = {Clamp(seed, c.Domain().Min(), c.Domain().Max())};
  const std::vector<double> lo = {c.Domain().Min()}, hi = {c.Domain().Max()};
  Residual res = [&](const std::vector<double>& q) {
    ON_3dPoint P;
    ON_3dVector d1;
    c.Ev1Der(q[0], P, d1);
    return std::vector<double>{ON_DotProduct(d1, P - p)};
  };
  NewtonSolve(res, x, lo, hi, 1e-12 * scale * scale, max_iter);
  // Guard against a saddle/maximum: compare with the seed and the ends.
  double bt = x[0], bd = c.PointAt(bt).DistanceTo(p);
  for (double t : {seed, lo[0], hi[0]}) { const double d = c.PointAt(t).DistanceTo(p); if (d < bd) { bd = d; bt = t; } }
  return bt;
}

double CurveClosestParamGlobal(const ON_Curve& c, Point3d p, int samples) {
  const ON_Interval d = c.Domain();
  double bt = d.Min(), bd = std::numeric_limits<double>::max();
  for (int i = 0; i <= samples; ++i) {
    const double t = d.ParameterAt(static_cast<double>(i) / samples);
    const double dd = c.PointAt(t).DistanceTo(p);
    if (dd < bd) { bd = dd; bt = t; }
  }
  return CurveClosestParam(c, p, bt);
}

// --- interpolation ---------------------------------------------------------------

std::vector<double> ChordParams(const std::vector<ON_3dPoint>& pts, bool closed) {
  std::vector<double> t;
  t.reserve(pts.size() + 1);
  t.push_back(0);
  for (size_t i = 1; i < pts.size(); ++i) t.push_back(t.back() + pts[i].DistanceTo(pts[i - 1]));
  if (closed && pts.size() > 1) t.push_back(t.back() + pts.back().DistanceTo(pts.front()));
  return t;
}

ON_NurbsCurve InterpolateCubic(const std::vector<ON_3dPoint>& in_pts, std::vector<double> in_params, bool closed, int dim) {
  ON_NurbsCurve curve;
  std::vector<ON_3dPoint> pts = in_pts;
  if (pts.size() < 2) return curve;
  if (in_params.size() != pts.size() + (closed ? 1 : 0)) in_params = ChordParams(pts, closed);
  std::vector<double> params = in_params;
  const size_t n0 = pts.size();
  const int wrap = closed && n0 >= 4 ? 3 : 0;
  if (wrap) {
    // Wrap three points at both ends so the seam is smooth, trim afterwards.
    std::vector<ON_3dPoint> w;
    std::vector<double> wt;
    const double period = in_params[n0];
    for (int k = wrap; k >= 1; --k) { w.push_back(pts[n0 - static_cast<size_t>(k)]); wt.push_back(in_params[n0 - static_cast<size_t>(k)] - period); }
    for (size_t i = 0; i < n0; ++i) { w.push_back(pts[i]); wt.push_back(in_params[i]); }
    for (int k = 0; k <= wrap; ++k) { w.push_back(pts[static_cast<size_t>(k) % n0]); wt.push_back(in_params[static_cast<size_t>(k) % n0] + period); }
    pts = w;
    params = wt;
  } else if (closed) {
    pts.push_back(pts.front());
  }
  const int n = static_cast<int>(pts.size());
  const int order = std::min(4, n), p = order - 1;
  // Full clamped knot vector by parameter averaging, then OpenNURBS style (drop the two end copies).
  std::vector<double> U(static_cast<size_t>(n + order));
  for (int i = 0; i <= p; ++i) { U[static_cast<size_t>(i)] = params.front(); U[static_cast<size_t>(n + i)] = params.back(); }
  for (int j = 1; j <= n - p - 1; ++j) {
    double s = 0;
    for (int k = j; k < j + p; ++k) s += params[static_cast<size_t>(k)];
    U[static_cast<size_t>(p + j)] = s / p;
  }
  curve.Create(dim, false, order, n);
  for (int i = 0; i < n + order - 2; ++i) curve.SetKnot(i, U[static_cast<size_t>(i + 1)]);
  const double* knot = curve.Knot();
  // Banded collocation matrix (half width p).
  const int bw = 2 * p + 1;
  std::vector<double> A(static_cast<size_t>(n) * bw, 0);
  auto at = [&](int r, int c) -> double& { return A[static_cast<size_t>(r) * bw + (c - r + p)]; };
  std::vector<double> rhs[3];
  for (int d = 0; d < 3; ++d) rhs[d].assign(static_cast<size_t>(n), 0);
  std::vector<double> N(static_cast<size_t>(order));
  for (int k = 0; k < n; ++k) {
    const double t = params[static_cast<size_t>(k)];
    int span = ON_NurbsSpanIndex(order, n, knot, t, 0, 0);
    ON_EvaluateNurbsBasis(order, knot + span, t, N.data());
    for (int j = 0; j < order; ++j) {
      const int c = span + j;
      if (c - k + p < 0 || c - k + p >= bw) continue;  // outside the band: should not happen for averaged knots
      at(k, c) = N[static_cast<size_t>(j)];
    }
    rhs[0][static_cast<size_t>(k)] = pts[static_cast<size_t>(k)].x;
    rhs[1][static_cast<size_t>(k)] = pts[static_cast<size_t>(k)].y;
    rhs[2][static_cast<size_t>(k)] = pts[static_cast<size_t>(k)].z;
  }
  // Banded Gaussian elimination without pivoting (totally positive matrix).
  for (int k = 0; k < n; ++k) {
    const double piv = at(k, k);
    if (std::fabs(piv) < 1e-300) continue;
    for (int r = k + 1; r <= std::min(n - 1, k + p); ++r) {
      const double f = at(r, k) / piv;
      if (f == 0) continue;
      for (int c = k; c <= std::min(n - 1, k + p); ++c) at(r, c) -= f * at(k, c);
      for (int d = 0; d < 3; ++d) rhs[d][static_cast<size_t>(r)] -= f * rhs[d][static_cast<size_t>(k)];
    }
  }
  std::vector<double> sol[3];
  for (int d = 0; d < 3; ++d) {
    sol[d].assign(static_cast<size_t>(n), 0);
    for (int r = n - 1; r >= 0; --r) {
      double s = rhs[d][static_cast<size_t>(r)];
      for (int c = r + 1; c <= std::min(n - 1, r + p); ++c) s -= at(r, c) * sol[d][static_cast<size_t>(c)];
      const double piv = at(r, r);
      sol[d][static_cast<size_t>(r)] = std::fabs(piv) < 1e-300 ? 0 : s / piv;
    }
  }
  for (int i = 0; i < n; ++i) curve.SetCV(i, ON_3dPoint(sol[0][static_cast<size_t>(i)], sol[1][static_cast<size_t>(i)], sol[2][static_cast<size_t>(i)]));
  if (wrap) curve.Trim(ON_Interval(in_params.front(), in_params[n0]));
  return curve;
}

// --- face containment ------------------------------------------------------------

bool PointInPolygon(const std::vector<ON_2dPoint>& poly, ON_2dPoint p) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const ON_2dPoint& a = poly[i];
    const ON_2dPoint& b = poly[j];
    if ((a.y > p.y) != (b.y > p.y)) {
      const double x = a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x);
      if (p.x < x) in = !in;
    }
  }
  return in;
}

std::vector<ON_2dPoint> LoopPolygon(const ON_Brep& b, int loop_index, int samples_per_trim) {
  std::vector<ON_2dPoint> poly;
  if (loop_index < 0 || loop_index >= b.m_L.Count()) return poly;
  const ON_BrepLoop& loop = b.m_L[loop_index];
  for (int k = 0; k < loop.m_ti.Count(); ++k) {
    const ON_BrepTrim& trim = b.m_T[loop.m_ti[k]];
    const ON_Interval d = trim.Domain();
    const bool line = trim.m_iso != ON_Surface::not_iso;
    const int ns = line ? 1 : samples_per_trim;
    for (int i = 0; i < ns; ++i) {
      ON_3dPoint uv = trim.PointAt(d.ParameterAt(static_cast<double>(i) / ns));
      poly.emplace_back(uv.x, uv.y);
    }
  }
  return poly;
}

bool FaceContainsUV(const ON_BrepFace& f, double u, double v) {
  const ON_Brep* b = f.Brep();
  if (!b) return true;
  if (f.m_li.Count() == 0) return f.Domain(0).Includes(u, true) && f.Domain(1).Includes(v, true);
  const ON_2dPoint p(u, v);
  bool inside_outer = false;
  for (int li = 0; li < f.m_li.Count(); ++li) {
    const ON_BrepLoop& loop = b->m_L[f.m_li[li]];
    if (loop.m_type == ON_BrepLoop::outer) inside_outer = PointInPolygon(LoopPolygon(*b, f.m_li[li]), p);
  }
  if (!inside_outer) return false;
  for (int li = 0; li < f.m_li.Count(); ++li) {
    const ON_BrepLoop& loop = b->m_L[f.m_li[li]];
    if (loop.m_type == ON_BrepLoop::inner && PointInPolygon(LoopPolygon(*b, f.m_li[li]), p)) return false;
  }
  return true;
}

// --- SSX assembly ------------------------------------------------------------------

namespace {

// Fits the NURBS curves of an intersection curve from its refined points and
// runs the adaptive midpoint check against both surfaces.
void FinishCurve(IntersectionCurve& ic, const ON_Surface& a, const ON_Surface& b, const IntersectOptions& opt) {
  auto fit = [&]() {
    std::vector<ON_3dPoint> p3, pa, pb;
    for (size_t i = 0; i < ic.points.size(); ++i) {
      p3.push_back(ic.points[i]);
      pa.emplace_back(ic.uv_a[i].x, ic.uv_a[i].y, 0);
      pb.emplace_back(ic.uv_b[i].x, ic.uv_b[i].y, 0);
    }
    ic.params = ChordParams(p3, ic.closed);
    ic.curve = InterpolateCubic(p3, ic.params, ic.closed, 3);
    ic.pcurve_a = InterpolateCubic(pa, ic.params, ic.closed, 2);
    ic.pcurve_b = InterpolateCubic(pb, ic.params, ic.closed, 2);
  };
  fit();
  for (int pass = 0; pass < 3; ++pass) {
    bool inserted = false;
    const size_t n = ic.points.size();
    std::vector<Point3d> np;
    std::vector<ON_2dPoint> na, nb;
    const size_t segs = ic.closed ? n : n - 1;
    for (size_t i = 0; i < n; ++i) {
      np.push_back(ic.points[i]); na.push_back(ic.uv_a[i]); nb.push_back(ic.uv_b[i]);
      if (i >= segs) continue;
      const double t0 = ic.params[i], t1 = ic.params[i + 1];
      if (t1 - t0 <= opt.tolerance * 4) continue;
      const double tm = 0.5 * (t0 + t1);
      const Point3d pm = ic.curve.PointAt(tm);
      ON_3dPoint sa = ic.pcurve_a.PointAt(tm), sb = ic.pcurve_b.PointAt(tm);
      double ua = sa.x, va = sa.y, ub = sb.x, vb = sb.y;
      if (!RefineSurfaceSurfacePoint(a, b, ua, va, ub, vb, opt.tolerance)) continue;
      const Point3d x = a.PointAt(ua, va);
      const size_t j = (i + 1) % n;
      if (pm.DistanceTo(x) > opt.tolerance && x.DistanceTo(ic.points[i]) > opt.tolerance * 2 && x.DistanceTo(ic.points[j]) > opt.tolerance * 2) {
        np.push_back(x); na.emplace_back(ua, va); nb.emplace_back(ub, vb);
        inserted = true;
      }
    }
    if (!inserted) break;
    ic.points = np; ic.uv_a = na; ic.uv_b = nb;
    fit();
  }
  ic.max_error = 0;
  for (size_t i = 0; i < ic.points.size(); ++i) ic.max_error = std::max(ic.max_error, a.PointAt(ic.uv_a[i].x, ic.uv_a[i].y).DistanceTo(b.PointAt(ic.uv_b[i].x, ic.uv_b[i].y)));
}

// Splits a polyline where a closed surface direction's parameter wraps.
void SplitAtSeams(std::vector<IntersectionCurve>& curves, const ON_Surface& a, const ON_Surface& b) {
  std::vector<IntersectionCurve> out;
  auto jumps = [&](const IntersectionCurve& c, size_t i, size_t j) {
    for (int dir = 0; dir < 2; ++dir) {
      if (a.IsClosed(dir) && std::fabs(c.uv_a[i][dir] - c.uv_a[j][dir]) > 0.5 * a.Domain(dir).Length()) return true;
      if (b.IsClosed(dir) && std::fabs(c.uv_b[i][dir] - c.uv_b[j][dir]) > 0.5 * b.Domain(dir).Length()) return true;
    }
    return false;
  };
  for (IntersectionCurve& c : curves) {
    const size_t n = c.points.size();
    std::vector<size_t> cuts;
    for (size_t i = 0; i + 1 < n; ++i) if (jumps(c, i, i + 1)) cuts.push_back(i + 1);
    const bool closed_jump = c.closed && jumps(c, n - 1, 0);
    if (cuts.empty() && !closed_jump) { out.push_back(std::move(c)); continue; }
    // Rotate a closed curve so it starts at a seam, then cut it open.
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    if (c.closed) {
      const size_t start = closed_jump ? 0 : cuts.front();
      std::rotate(idx.begin(), idx.begin() + static_cast<long>(start), idx.end());
    }
    IntersectionCurve cur;
    for (size_t k = 0; k < n; ++k) {
      const size_t i = idx[k];
      if (k > 0 && jumps(c, idx[k - 1], i)) {
        if (cur.points.size() >= 2) out.push_back(cur);
        cur = IntersectionCurve();
      }
      cur.points.push_back(c.points[i]); cur.uv_a.push_back(c.uv_a[i]); cur.uv_b.push_back(c.uv_b[i]);
    }
    if (cur.points.size() >= 2) out.push_back(cur);
  }
  curves = std::move(out);
}

void ThinPoints(IntersectionCurve& c, double min_gap, size_t max_points) {
  const size_t n = c.points.size();
  if (n < 3) return;
  double len = 0;
  for (size_t i = 1; i < n; ++i) len += c.points[i].DistanceTo(c.points[i - 1]);
  const double gap = std::max(min_gap, len / static_cast<double>(std::max<size_t>(max_points, 2)));
  std::vector<Point3d> np;
  std::vector<ON_2dPoint> na, nb;
  for (size_t i = 0; i < n; ++i) {
    const bool last = i + 1 == n;
    if (!np.empty() && !last && np.back().DistanceTo(c.points[i]) < gap) continue;
    if (last && !c.closed && !np.empty() && np.back().DistanceTo(c.points[i]) < gap * 0.25 && np.size() > 1) { np.back() = c.points[i]; na.back() = c.uv_a[i]; nb.back() = c.uv_b[i]; continue; }
    if (last && c.closed && !np.empty() && np.back().DistanceTo(c.points[i]) < gap) continue;
    np.push_back(c.points[i]); na.push_back(c.uv_a[i]); nb.push_back(c.uv_b[i]);
  }
  if (np.size() >= 2) { c.points = np; c.uv_a = na; c.uv_b = nb; }
}

}  // namespace

std::vector<IntersectionCurve> IntersectSurfaces(const ON_Surface& a, const ON_Surface& b, const IntersectOptions& opt) {
  std::vector<IntersectionCurve> out;
  const SurfaceMesh ma = TessellateWithUV(a, opt), mb = TessellateWithUV(b, opt);
  ON_BoundingBox both = ma.bbox;
  both.Union(mb.bbox);
  const double diag = both.IsValid() ? both.Diagonal().Length() : 1;
  const double eps = std::max(1e-9 * diag, 1e-12);
  const std::vector<Seg> segs = MeshSegments(ma, mb, eps);
  if (segs.empty()) return out;
  std::vector<SeedPolyline> chains = ChainSegments(segs, std::max(eps * 100, 1e-7 * diag));
  for (const SeedPolyline& pl : chains) {
    IntersectionCurve ic;
    ic.closed = pl.closed;
    for (const SeedPoint& sp : pl.pts) {
      double ua = sp.uva.x, va = sp.uva.y, ub = sp.uvb.x, vb = sp.uvb.y;
      if (!RefineSurfaceSurfacePoint(a, b, ua, va, ub, vb, opt.tolerance)) continue;
      const Point3d p = a.PointAt(ua, va);
      if (p.DistanceTo(sp.p) > std::max(opt.mesh_tolerance * 20, opt.tolerance * 100)) continue;  // wandered off the seed
      if (!ic.points.empty() && ic.points.back().DistanceTo(p) <= opt.tolerance) continue;
      ic.points.push_back(p);
      ic.uv_a.emplace_back(ua, va);
      ic.uv_b.emplace_back(ub, vb);
    }
    if (ic.closed && ic.points.size() > 2 && ic.points.front().DistanceTo(ic.points.back()) <= opt.tolerance) { ic.points.pop_back(); ic.uv_a.pop_back(); ic.uv_b.pop_back(); }
    if (ic.points.size() < 2) continue;
    if (ic.closed && ic.points.size() < 3) ic.closed = false;
    out.push_back(std::move(ic));
  }
  SplitAtSeams(out, a, b);
  std::vector<IntersectionCurve> finished;
  for (IntersectionCurve& ic : out) {
    ThinPoints(ic, opt.tolerance * 4, 400);
    if (ic.points.size() < 2) continue;
    double len = 0;
    for (size_t i = 1; i < ic.points.size(); ++i) len += ic.points[i].DistanceTo(ic.points[i - 1]);
    if (len <= opt.tolerance * 4) continue;
    FinishCurve(ic, a, b, opt);
    finished.push_back(std::move(ic));
  }
  return finished;
}

std::vector<IntersectionCurve> IntersectFaces(const ON_BrepFace* face_a, const ON_Surface& a, const ON_BrepFace* face_b, const ON_Surface& b, const IntersectOptions& opt) {
  std::vector<IntersectionCurve> raw = IntersectSurfaces(a, b, opt);
  if (!face_a && !face_b) return raw;
  std::vector<IntersectionCurve> out;
  for (IntersectionCurve& c : raw) {
    const size_t n = c.points.size();
    std::vector<char> in(n, 1);
    for (size_t i = 0; i < n; ++i) {
      if (face_a && !FaceContainsUV(*face_a, c.uv_a[i].x, c.uv_a[i].y)) in[i] = 0;
      if (face_b && !FaceContainsUV(*face_b, c.uv_b[i].x, c.uv_b[i].y)) in[i] = 0;
    }
    if (std::all_of(in.begin(), in.end(), [](char v) { return v == 1; })) { out.push_back(std::move(c)); continue; }
    // Runs of inside points (a closed curve is opened at the first outside point).
    size_t start = 0;
    if (c.closed) { while (start < n && in[start]) ++start; }
    IntersectionCurve cur;
    for (size_t k = 0; k < n; ++k) {
      const size_t i = (start + k) % n;
      if (!in[i]) {
        if (cur.points.size() >= 2) { FinishCurve(cur, a, b, opt); out.push_back(cur); }
        cur = IntersectionCurve();
        continue;
      }
      cur.points.push_back(c.points[i]); cur.uv_a.push_back(c.uv_a[i]); cur.uv_b.push_back(c.uv_b[i]);
    }
    if (cur.points.size() >= 2) { FinishCurve(cur, a, b, opt); out.push_back(cur); }
  }
  return out;
}

namespace {

// Segment / triangle intersection (Moller-Trumbore), returns the segment parameter.
bool SegTri(const Point3d& p0, const Point3d& p1, const Point3d& a, const Point3d& b, const Point3d& c, double& s, double& bu, double& bv) {
  const Vector3d d = p1 - p0, e1 = b - a, e2 = c - a;
  const Vector3d h = ON_CrossProduct(d, e2);
  const double det = ON_DotProduct(e1, h);
  if (std::fabs(det) < 1e-300) return false;
  const double inv = 1 / det;
  const Vector3d sv = p0 - a;
  bu = inv * ON_DotProduct(sv, h);
  if (bu < -1e-9 || bu > 1 + 1e-9) return false;
  const Vector3d q = ON_CrossProduct(sv, e1);
  bv = inv * ON_DotProduct(d, q);
  if (bv < -1e-9 || bu + bv > 1 + 1e-9) return false;
  s = inv * ON_DotProduct(e2, q);
  return s >= -1e-9 && s <= 1 + 1e-9;
}

}  // namespace

std::vector<CurveSurfaceHit> IntersectCurveSurface(const ON_Curve& c, const ON_Surface& s, const IntersectOptions& opt) {
  std::vector<CurveSurfaceHit> hits;
  const SurfaceMesh m = TessellateWithUV(s, opt);
  ON_BoundingBox region = m.bbox;
  const double pad = std::max(opt.mesh_tolerance * 2, 1e-9);
  region.m_min -= ON_3dVector(pad, pad, pad);
  region.m_max += ON_3dVector(pad, pad, pad);
  Grid grid;
  grid.Build(m, region);
  std::vector<int> stamp(m.tris.size(), -1);
  int mark = 0;
  const ON_Interval d = c.Domain();
  ON_BoundingBox cb = c.BoundingBox();
  const double clen = cb.IsValid() ? cb.Diagonal().Length() : 1;
  const int n = static_cast<int>(Clamp(std::ceil(clen / std::max(opt.mesh_tolerance, 1e-6)), 64, 2000));
  std::vector<Point3d> samples(static_cast<size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) samples[static_cast<size_t>(i)] = c.PointAt(d.ParameterAt(static_cast<double>(i) / n));
  struct Seed { double t; ON_2dPoint uv; };
  std::vector<Seed> seeds;
  for (int i = 0; i < n; ++i) {
    const Point3d& p0 = samples[static_cast<size_t>(i)];
    const Point3d& p1 = samples[static_cast<size_t>(i) + 1];
    ON_BoundingBox sb;
    sb.Set(p0, true); sb.Set(p1, true);
    sb.m_min -= ON_3dVector(pad, pad, pad); sb.m_max += ON_3dVector(pad, pad, pad);
    ++mark;
    grid.Query(sb, stamp, mark, [&](int ti) {
      const auto& tr = m.tris[static_cast<size_t>(ti)];
      double sp, bu, bv;
      if (!SegTri(p0, p1, m.pts[static_cast<size_t>(tr[0])], m.pts[static_cast<size_t>(tr[1])], m.pts[static_cast<size_t>(tr[2])], sp, bu, bv)) return;
      const ON_2dPoint& u0 = m.uv[static_cast<size_t>(tr[0])];
      const ON_2dPoint& u1 = m.uv[static_cast<size_t>(tr[1])];
      const ON_2dPoint& u2 = m.uv[static_cast<size_t>(tr[2])];
      ON_2dPoint uv(u0.x + (u1.x - u0.x) * bu + (u2.x - u0.x) * bv, u0.y + (u1.y - u0.y) * bu + (u2.y - u0.y) * bv);
      seeds.push_back({d.ParameterAt((i + Clamp(sp, 0, 1)) / n), uv});
    });
  }
  const std::vector<double> lo = {d.Min(), s.Domain(0).Min(), s.Domain(1).Min()}, hi = {d.Max(), s.Domain(0).Max(), s.Domain(1).Max()};
  for (const Seed& sd : seeds) {
    std::vector<double> x = {sd.t, sd.uv.x, sd.uv.y};
    Residual res = [&](const std::vector<double>& q) {
      const Point3d pc = c.PointAt(q[0]), ps = s.PointAt(q[1], q[2]);
      return std::vector<double>{pc.x - ps.x, pc.y - ps.y, pc.z - ps.z};
    };
    double err = 0;
    if (!NewtonSolve(res, x, lo, hi, opt.tolerance, 40, &err)) continue;
    CurveSurfaceHit h;
    h.t = x[0]; h.uv = ON_2dPoint(x[1], x[2]); h.point = c.PointAt(h.t); h.error = err;
    bool dup = false;
    for (const CurveSurfaceHit& o : hits) if (o.point.DistanceTo(h.point) <= opt.tolerance * 4) { dup = true; break; }
    if (!dup) hits.push_back(h);
  }
  std::sort(hits.begin(), hits.end(), [](const CurveSurfaceHit& a, const CurveSurfaceHit& b) { return a.t < b.t; });
  return hits;
}

}  // namespace dino8::app
