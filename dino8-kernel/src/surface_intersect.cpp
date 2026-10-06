#include "dino8/kernel/surface_intersect.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace dino8::kernel {

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
  m.nu = nu;
  m.nv = nv;
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
  return SurfaceClosestPoint(s, p, u, v);
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
  // ON_EvaluateNurbsBasis fills an implicit order x order triangular scratch
  // table (see its header comment: N[d-k][i] for 0<=k<=d), not just the
  // final `order`-length result row, so the buffer must be order*order or
  // it overflows and corrupts the heap.
  std::vector<double> N(static_cast<size_t>(order) * static_cast<size_t>(order));
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

Result NurbsCurve::OffsetOnSurfaceNormal(const NurbsSurface& surface, double distance, NurbsCurve& out,
                                          int sample_count) const {
  if (!ON_IsValid(distance)) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::OffsetOnSurfaceNormal: distance must be finite");
  }
  const bool closed = IsClosed();
  const int n = sample_count > 0 ? sample_count : (closed ? 48 : 40);
  if (n < 2) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::OffsetOnSurfaceNormal: sample_count must be at least 2");
  }

  const Interval d = Domain();
  std::vector<ON_3dPoint> offset_pts;
  offset_pts.reserve(static_cast<size_t>(n));
  // Sample at n points across the domain - n-1 steps for an open curve
  // (endpoints included), n points around the full period for a closed
  // one (the last sample coincides with the first, exactly as
  // ChordParams()/InterpolateCubic() already expect for closed input).
  const int steps = closed ? n : n - 1;
  for (int i = 0; i < n; ++i) {
    const double t = d.min + (d.max - d.min) * (static_cast<double>(i) / steps);
    const Point3d p = PointAt(t);
    const Point2d uv = surface.ClosestPointParameter(p, 24, 24);
    Vector3d normal = surface.NormalAt(uv.x, uv.y);
    if (!normal.Unitize()) continue;
    const Point3d moved = p + distance * normal;
    offset_pts.emplace_back(moved.x, moved.y, moved.z);
  }
  if (offset_pts.size() < 2) return Result::Failed;

  const bool fit_closed = closed && offset_pts.size() == static_cast<size_t>(n);
  if (Degree() == 1 && !closed) {
    out = NurbsCurve::FromControlPoints(offset_pts, 1);
  } else {
    const std::vector<double> params = ChordParams(offset_pts, fit_closed);
    out.raw() = InterpolateCubic(offset_pts, params, fit_closed, 3);
  }
  return Result::Ok;
}

Result NurbsCurve::OffsetInSurface(const NurbsSurface& surface, double distance, NurbsCurve& out,
                                    int sample_count) const {
  if (!ON_IsValid(distance)) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::OffsetInSurface: distance must be finite");
  }
  const bool closed = IsClosed();
  const int n = sample_count > 0 ? sample_count : (closed ? 48 : 40);
  if (n < 2) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::OffsetInSurface: sample_count must be at least 2");
  }

  const Interval d = Domain();
  std::vector<ON_3dPoint> offset_pts;
  offset_pts.reserve(static_cast<size_t>(n));
  const int steps = closed ? n : n - 1;
  for (int i = 0; i < n; ++i) {
    const double t = d.min + (d.max - d.min) * (static_cast<double>(i) / steps);
    const Point3d p = PointAt(t);
    const Point2d uv = surface.ClosestPointParameter(p, 24, 24);
    Vector3d normal = surface.NormalAt(uv.x, uv.y);
    if (!normal.Unitize()) continue;
    Vector3d side = ON_CrossProduct(TangentAt(t), normal);
    if (!side.Unitize()) continue;
    const Point3d moved = surface.PointAt(uv.x, uv.y) + distance * side;
    // Re-project so the offset point actually lands back on the surface,
    // rather than merely near it along a straight tangent-plane step -
    // see this method's own doc comment for why that second step is
    // what makes this an IN-surface offset rather than OffsetOnSurfaceNormal's
    // deliberately off-surface one.
    const Point2d uv2 = surface.ClosestPointParameter(moved, 24, 24);
    offset_pts.push_back(surface.PointAt(uv2.x, uv2.y));
  }
  if (offset_pts.size() < 2) return Result::Failed;

  const bool fit_closed = closed && offset_pts.size() == static_cast<size_t>(n);
  if (Degree() == 1 && !closed) {
    out = NurbsCurve::FromControlPoints(offset_pts, 1);
  } else {
    const std::vector<double> params = ChordParams(offset_pts, fit_closed);
    out.raw() = InterpolateCubic(offset_pts, params, fit_closed, 3);
  }
  return Result::Ok;
}

// --- face containment ------------------------------------------------------------

bool PointInPolygon(const std::vector<ON_2dPoint>& poly, ON_2dPoint p) {
  // A point that sits (within a scale-relative epsilon) ON one of the
  // polygon's own edges is unambiguously part of this loop, not "outside"
  // it - but the even-odd ray-casting test below has no notion of "on the
  // boundary": for a point whose x-coordinate happens to land EXACTLY on
  // a vertical (or near-vertical) edge, whether it counts as in or out
  // depends on which side of a `<` comparison a tiny floating-point
  // residual falls on, which is exactly the kind of case a full-sweep
  // (angle == 2*pi) CylindricalFace's own trim rectangle produces at its
  // own u_max edge: a seam-crossing intersection curve point can be
  // Newton-refined to sit at u == u_max to the last bit, i.e. exactly on
  // the trim's own right-hand edge. Without this check that single point
  // spuriously tested "outside", which then made IntersectFaces() (this
  // file) rip an otherwise-genuinely-closed loop open at that one point -
  // a confirmed root cause (see boolean_general.cpp's own doc comment on
  // the box-fully-pierced-by-a-cylinder case). Checked first and cheaply
  // relative to the polygon's own bounding box scale, so it costs nothing
  // for the overwhelmingly common case of a point nowhere near any edge.
  double minx = poly[0].x, maxx = poly[0].x, miny = poly[0].y, maxy = poly[0].y;
  for (const ON_2dPoint& q : poly) {
    minx = std::min(minx, q.x); maxx = std::max(maxx, q.x);
    miny = std::min(miny, q.y); maxy = std::max(maxy, q.y);
  }
  const double diag = std::hypot(maxx - minx, maxy - miny);
  const double eps = std::max(1e-9 * diag, 1e-12);
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const ON_2dPoint& a = poly[i];
    const ON_2dPoint& b = poly[j];
    const double vx = b.x - a.x, vy = b.y - a.y;
    const double len2 = vx * vx + vy * vy;
    if (len2 > 1e-300) {
      double t = ((p.x - a.x) * vx + (p.y - a.y) * vy) / len2;
      t = t < 0 ? 0 : (t > 1 ? 1 : t);
      const double dx = p.x - (a.x + vx * t), dy = p.y - (a.y + vy * t);
      if (dx * dx + dy * dy <= eps * eps) return true;
    }
  }
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
  // CLOSED-interval test (ON_Interval::Includes's second argument is
  // `bTestOpenInterval`; passing `true` there - as this used to - tests
  // min < t < max and so rejected every (u, v) sitting EXACTLY on the
  // untrimmed surface's own domain boundary). That boundary IS part of an
  // untrimmed face: for a full sphere it is the seam meridian (u == u_min)
  // and both poles (v == v_min/v_max), and RefineSurfaceSurfacePoint()'s
  // Newton solve clamps its (u, v) to the domain, so a refined point that
  // lands on the seam lands on it exactly. Confirmed root cause of the
  // sphere+box gap (see boolean_general.cpp's own top-of-file comment):
  // IntersectFaces()'s own clipping pass below silently discarded every
  // seam-meridian sample refined to u == 0 bit-exactly (while keeping the
  // ones that happened to round to u == 1e-17), ripping the y == 0
  // plane's own meridian arc into disconnected pieces with real gaps, and
  // dropping the equator arc's own u == 0 endpoint.
  if (f.m_li.Count() == 0) return f.Domain(0).Includes(u) && f.Domain(1).Includes(v);
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
  // A segment whose two endpoints sit on opposite sides of one of the
  // surfaces' own closed/periodic parameter directions (its own (u, v)
  // value differs by more than half that direction's domain length, even
  // though the two 3D points are ordinary close neighbours on the real
  // curve) is NOT an ordinary segment for the raw-(u, v) cubic fit below
  // to subdivide: `fit()` interpolates pcurve_a/pcurve_b directly in RAW
  // (u, v) numbers with no notion of wraparound, so across a seam segment
  // it draws a bogus straight-line-ish swing through the MIDDLE of the
  // domain (e.g. from u near a direction's own max down to u near its own
  // min goes through u ~ domain-middle, physically nowhere near either
  // endpoint) - a bad initial guess that can send this segment's own
  // Newton refinement (see the insertion loop below) to a WRONG point
  // entirely (a real, confirmed defect for a surface pair whose true
  // intersection is an extended curve, e.g. an entire circle where a
  // cylinder wall's own periodic angle direction is degenerate along the
  // whole curve - see boolean_general.cpp's own doc comment on the
  // box-fully-pierced-by-a-cylinder case). Simplest safe fix: never
  // subdivide a seam segment at all - the two endpoints already bracket it
  // adequately (this is exactly the same segment SplitAtSeams() itself
  // treats specially), so skipping it only forgoes possibly-unnecessary
  // extra refinement there, never correctness.
  auto is_seam_segment = [&](size_t i, size_t j) {
    for (int dir = 0; dir < 2; ++dir) {
      if (a.IsClosed(dir) && std::fabs(ic.uv_a[i][dir] - ic.uv_a[j][dir]) > 0.5 * a.Domain(dir).Length()) return true;
      if (b.IsClosed(dir) && std::fabs(ic.uv_b[i][dir] - ic.uv_b[j][dir]) > 0.5 * b.Domain(dir).Length()) return true;
    }
    return false;
  };
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
      const size_t j0 = (i + 1) % n;
      if (is_seam_segment(i, j0)) continue;
      const double t0 = ic.params[i], t1 = ic.params[i + 1];
      if (t1 - t0 <= opt.tolerance * 4) continue;
      const double tm = 0.5 * (t0 + t1);
      const Point3d pm = ic.curve.PointAt(tm);
      ON_3dPoint sa = ic.pcurve_a.PointAt(tm), sb = ic.pcurve_b.PointAt(tm);
      double ua = sa.x, va = sa.y, ub = sb.x, vb = sb.y;
      if (!RefineSurfaceSurfacePoint(a, b, ua, va, ub, vb, opt.tolerance)) continue;
      const Point3d x = a.PointAt(ua, va);
      const size_t j = (i + 1) % n;
      // RefineSurfaceSurfacePoint()'s 3-equation/4-unknown Newton system is
      // only weakly damped along a curve's own TANGENT direction (every
      // point along the curve is an equally valid zero-residual solution,
      // so nothing in the linearized system penalizes sliding along it) -
      // for the common case this costs nothing (the seed is already close
      // to the unique nearby true point, so the solve simply doesn't need
      // to move far), but for a curve that is tangent to BOTH surfaces
      // along its own entire length (the canonical case: a plane exactly
      // perpendicular to a cylinder's axis, whose whole circular
      // cross-section is an equally-valid solution everywhere), a segment
      // midpoint seed - a genuine, non-degenerate distance away from the
      // curve in the surfaces' shared NORMAL direction (needing real
      // Newton work to fix) - can converge to an unrelated, distant point
      // on the SAME curve instead of the geometrically nearest one: a
      // real, reproduced defect (see boolean_general.h's own disclosed
      // limitations), confirmed here directly by tracing the exact
      // sequence of points a box-face's own flat cross-section of a
      // cylinder wall produces. The cubic-fit midpoint `pm` is always a
      // trustworthy, close approximation of where the TRUE point should
      // land (it is built from two points already confirmed to lie on the
      // real curve) - `x` landing far from it, rather than just off it by
      // the small normal-direction correction the Newton solve exists to
      // make, is exactly this failure mode; reject the insertion outright
      // (keep the two original, already-correct endpoints, forgoing only
      // this one segment's extra refinement) rather than splice in a wild
      // jump that would later self-intersect this curve's own polyline.
      const double local_span = std::max(ic.points[i].DistanceTo(ic.points[j]), opt.mesh_tolerance * 4);
      // A second, independent guard against the same failure mode: on a
      // genuine smooth curve, a segment's own true midpoint - in EITHER
      // surface's (u, v) chart - never lies outside the bracket its two
      // endpoints already span (plus a little slack for real curvature,
      // scaled to that same segment's own span, not some fixed constant
      // that would be meaningless across wildly different surface
      // scales/units). A wild jump along a tangent-degenerate direction
      // routinely fails this even when it happens to still be shorter
      // than `local_span` above (e.g. jumping backwards a little instead
      // of forward, or overshooting past the segment's own far endpoint
      // toward a different nearby true point on the same curve) - this is
      // NOT a redundant check, it is a real, reproduced tightening (this
      // exact case: the box-vs-cylinder-wall circle) confirmed to catch
      // small-amplitude back-and-forth jitter the plain distance cap above
      // lets through.
      auto in_bracket = [&](double v, double lo, double hi) {
        if (lo > hi) std::swap(lo, hi);
        const double slack = std::max(0.25 * (hi - lo), opt.tolerance * 10);
        return v >= lo - slack && v <= hi + slack;
      };
      const bool bracketed = in_bracket(ua, ic.uv_a[i].x, ic.uv_a[j].x) && in_bracket(va, ic.uv_a[i].y, ic.uv_a[j].y) &&
                              in_bracket(ub, ic.uv_b[i].x, ic.uv_b[j].x) && in_bracket(vb, ic.uv_b[i].y, ic.uv_b[j].y);
      // A THIRD, independent guard, needed alongside the two above: a
      // segment whose own two endpoints (i, j) are themselves NOT
      // adjacent samples of a clean, already-monotonic curve - because an
      // EARLIER pass (or the raw mesh-seeded chain itself, before any
      // refinement) already left a small back-and-forth reversal between
      // i and a DIFFERENT nearby sample not involved in this segment at
      // all - can still pass both the distance cap and the bracket check
      // above for a NEW point that itself reverses direction relative to
      // i (moving AWAY from j, back the way the curve already came from,
      // rather than continuing toward it), since the bracket's own 25%
      // slack is generous enough to admit exactly this: confirmed by
      // direct tracing on a SLOPED cylinder cut (sweep case 03, box+cyl
      // OBLIQUE axis) and a cone (case 15) - a self-intersecting 2D trim
      // loop traced back to two consecutive chain samples out of angular
      // order (e.g. angle 0.093 immediately followed by angle 0.057,
      // reversing a few mesh-cells' worth of otherwise-monotonic angular
      // progress). Reject an insertion whose own coordinate, in EITHER
      // surface's chart, moves backward from `i` relative to the
      // direction `i` is heading toward `j` - a small allowance
      // (opt.tolerance, or a tiny fraction of the segment's own span)
      // covers ordinary Newton noise on an intentionally near-constant
      // coordinate without opening the same hole the 25%-of-range bracket
      // slack does. This does not by itself fix an already-reversed PAIR
      // of raw/coarse samples (this pass only ever proposes ONE new point
      // per segment) but does stop this segment's own refinement from
      // making a bad situation worse, and in practice removes the
      // specific self-crossing traced above.
      auto monotonic_from_i = [&](double v, double vi, double vj) {
        const double range = vj - vi;
        const double back_slack = std::max(opt.tolerance, 1e-9 * std::fabs(range));
        if (std::fabs(range) <= back_slack) return true;  // no meaningful direction on this coordinate
        return range > 0 ? (v >= vi - back_slack) : (v <= vi + back_slack);
      };
      const bool no_reversal = monotonic_from_i(ua, ic.uv_a[i].x, ic.uv_a[j].x) &&
                                monotonic_from_i(va, ic.uv_a[i].y, ic.uv_a[j].y) &&
                                monotonic_from_i(ub, ic.uv_b[i].x, ic.uv_b[j].x) &&
                                monotonic_from_i(vb, ic.uv_b[i].y, ic.uv_b[j].y);
      if (bracketed && no_reversal && pm.DistanceTo(x) > opt.tolerance && pm.DistanceTo(x) <= local_span &&
          x.DistanceTo(ic.points[i]) > opt.tolerance * 2 && x.DistanceTo(ic.points[j]) > opt.tolerance * 2) {
        np.push_back(x); na.emplace_back(ua, va); nb.emplace_back(ub, vb);
        inserted = true;
      }
    }
    if (!inserted) break;
    ic.points = np; ic.uv_a = na; ic.uv_b = nb;
    fit();
  }
  // Final cleanup pass: drop any point that locally reverses direction
  // relative to its own two immediate neighbors, in EITHER surface's own
  // chart - a small-amplitude back-and-forth jitter that the insertion-time
  // guard above cannot always catch, because it can arise from TWO
  // separately-refined adjacent segments each individually valid on its
  // own (only ever checked against its own two endpoints) but mutually
  // inconsistent once assembled - e.g. a segment (P, Q) refined first,
  // landing its own new point near P, followed by a refinement of (that
  // new point, Q) or (P, that new point) that, checked only against ITS
  // own now-nearer endpoints, still passes but overshoots back past where
  // the curve had already gotten to. Confirmed by direct tracing on a cone
  // (sweep case 15, box+cone): a self-intersecting 2D trim loop traced
  // back to exactly a triple of this shape, immune to the per-segment
  // insertion guard for exactly that reason.
  //
  // Uses the SAME 25%-of-neighbor-span relative slack already trusted for
  // ordinary insertion, so a genuinely curved passage - where the middle
  // sample legitimately sits a little outside the dead-straight P-Q line -
  // is not disturbed; only an overshoot beyond that same margin, in some
  // coordinate, is dropped. The ABSOLUTE floor, though, is deliberately
  // tighter here (opt.tolerance * 2, not the insertion guard's own
  // opt.tolerance * 10): root-caused directly on the skew (non-
  // intersecting-axes) perpendicular cylinder pair sweep case (case 08) -
  // a genuine local reversal (three consecutive samples whose middle one
  // sits ~4.5e-3 past its own neighbor-bracket) went undetected because
  // the neighbor-to-neighbor span there was itself only ~1.15e-2, so 25%
  // of it (~2.9e-3) was smaller than the insertion guard's own floor
  // (opt.tolerance * 10 = 1e-2 at this engine's default tolerance) - the
  // floor, not the relative term, ended up setting the bracket, and it was
  // generous enough to hide a reversal comparable in size to the segment
  // span itself. The insertion guard's own floor is left untouched (it
  // vets a single BRAND-NEW candidate point, a different, already-tuned
  // situation - see its own doc comment); only this later, independent
  // pass over ALREADY-accepted points gets the tighter floor, matching
  // the tighter (opt.tolerance * 1) floor `monotonic_from_i` already uses
  // above for the same reversal failure mode during insertion.
  // Runs to a fixed point (a dropped point can occasionally expose a
  // second one, now that its former neighbors are adjacent) with a small
  // iteration cap so a pathological curve degrades to "leaves the jitter
  // in place" rather than loops.
  for (int cleanup_pass = 0; cleanup_pass < 4; ++cleanup_pass) {
    const size_t n = ic.points.size();
    if (n < 4) break;
    const size_t nsegs = ic.closed ? n : n - 1;
    std::vector<char> drop(n, 0);
    bool any_drop = false;
    for (size_t idx = 0; idx < n; ++idx) {
      if (!ic.closed && (idx == 0 || idx + 1 >= n)) continue;
      const size_t ip = (idx + n - 1) % n, in = (idx + 1) % n;
      if (ic.closed && nsegs < 4) continue;  // too small a loop to second-guess
      if (is_seam_segment(ip, idx) || is_seam_segment(idx, in)) continue;
      auto out_of_bracket = [&](double vp, double vi, double vn) {
        double lo = std::min(vp, vn), hi = std::max(vp, vn);
        const double slack = std::max(0.25 * (hi - lo), opt.tolerance * 2);
        return vi < lo - slack || vi > hi + slack;
      };
      const bool bad = out_of_bracket(ic.uv_a[ip].x, ic.uv_a[idx].x, ic.uv_a[in].x) ||
                        out_of_bracket(ic.uv_a[ip].y, ic.uv_a[idx].y, ic.uv_a[in].y) ||
                        out_of_bracket(ic.uv_b[ip].x, ic.uv_b[idx].x, ic.uv_b[in].x) ||
                        out_of_bracket(ic.uv_b[ip].y, ic.uv_b[idx].y, ic.uv_b[in].y);
      if (bad) { drop[idx] = 1; any_drop = true; }
    }
    if (!any_drop) break;
    std::vector<Point3d> np2;
    std::vector<ON_2dPoint> na2, nb2;
    for (size_t idx = 0; idx < n; ++idx) {
      if (drop[idx]) continue;
      np2.push_back(ic.points[idx]); na2.push_back(ic.uv_a[idx]); nb2.push_back(ic.uv_b[idx]);
    }
    if (np2.size() < 3) break;  // never collapse below a usable curve
    ic.points = np2; ic.uv_a = na2; ic.uv_b = nb2;
    fit();
  }
  // Final de-duplication pass: two ADJACENT samples that end up within a
  // couple tolerance-units of each other in 3D - not caught by the initial
  // seed-sampling dedup in IntersectSurfaces()'s own main loop, since this
  // one can be introduced LATER, by a point the insertion loop or the
  // reversal-cleanup pass above added - are effectively the SAME physical
  // point sampled twice, each independently Newton-refined with its own
  // small residual noise in (u, v). Keeping both is worse than harmless:
  // their two slightly-divergent (u, v) values can disagree about which
  // one comes "first" along the curve, producing exactly the kind of
  // spurious local self-crossing the reversal cleanup above exists to
  // catch - but at an amplitude too small (comparable to the noise
  // itself) for that bracket-based check to safely flag without also
  // catching genuine fine curvature elsewhere (see its own doc comment on
  // why its floor can't just be tightened further). Root-caused directly
  // on sweep case 08 (skew, non-intersecting-axes perpendicular cylinder
  // pair): two adjacent samples only ~1.0e-3 apart in 3D - right at this
  // engine's own default refinement tolerance - carried (u, v) residuals
  // that put one of them locally out of order relative to its neighbors.
  // Merging near-duplicates by 3D distance sidesteps the ambiguity
  // entirely: there is no meaningful answer to "which (u, v) came first"
  // for two samples of the same physical point, so simply keep whichever
  // was reached first in the existing ordering and drop the other.
  for (int dedup_pass = 0; dedup_pass < 4; ++dedup_pass) {
    const size_t n = ic.points.size();
    if (n < 4) break;
    const double dedup_tol = opt.tolerance * 3;
    std::vector<char> drop(n, 0);
    bool any_drop = false;
    const size_t nsegs2 = ic.closed ? n : n - 1;
    for (size_t i = 0; i < nsegs2; ++i) {
      if (drop[i]) continue;  // don't chain off a point already being removed this pass
      const size_t j = (i + 1) % n;
      if (drop[j]) continue;
      if (ic.points[i].DistanceTo(ic.points[j]) <= dedup_tol) { drop[j] = 1; any_drop = true; }
    }
    if (!any_drop) break;
    std::vector<Point3d> np3;
    std::vector<ON_2dPoint> na3, nb3;
    for (size_t idx = 0; idx < n; ++idx) {
      if (drop[idx]) continue;
      np3.push_back(ic.points[idx]); na3.push_back(ic.uv_a[idx]); nb3.push_back(ic.uv_b[idx]);
    }
    if (np3.size() < 3) break;  // never collapse below a usable curve
    ic.points = np3; ic.uv_a = na3; ic.uv_b = nb3;
    fit();
  }
  ic.max_error = 0;
  for (size_t i = 0; i < ic.points.size(); ++i) ic.max_error = std::max(ic.max_error, a.PointAt(ic.uv_a[i].x, ic.uv_a[i].y).DistanceTo(b.PointAt(ic.uv_b[i].x, ic.uv_b[i].y)));
}

// Linear (u, v) interpolation between two consecutive curve samples in one
// surface's own chart, UNWRAPPED across a periodic seam jump (a closed
// direction whose two values differ by more than half its domain length is
// interpolated through the seam, not through the middle of the domain -
// see FinishCurve's is_seam_segment on why raw interpolation there is
// bogus) and wrapped back into the domain afterwards.
ON_2dPoint LerpUV(const ON_Surface& s, const ON_2dPoint& p, const ON_2dPoint& q, double t) {
  ON_2dPoint r;
  for (int dir = 0; dir < 2; ++dir) {
    double pv = p[dir], qv = q[dir];
    const ON_Interval d = s.Domain(dir);
    const double L = d.Length();
    if (s.IsClosed(dir) && L > 0 && std::fabs(pv - qv) > 0.5 * L) qv += pv > qv ? L : -L;
    double v = pv + (qv - pv) * t;
    if (s.IsClosed(dir) && L > 0) {
      if (v < d.Min()) v += L;
      if (v > d.Max()) v -= L;
    }
    r[dir] = Clamp(v, d.Min(), d.Max());
  }
  return r;
}

// Which surface/direction jumps across a periodic seam between samples i
// and j of `c` (the same signal SplitAtSeams's own `jumps` uses). Returns
// false when none does.
bool SeamJumpDir(const IntersectionCurve& c, size_t i, size_t j, const ON_Surface& a, const ON_Surface& b, bool& on_a, int& dir) {
  for (int d = 0; d < 2; ++d) {
    if (a.IsClosed(d) && std::fabs(c.uv_a[i][d] - c.uv_a[j][d]) > 0.5 * a.Domain(d).Length()) { on_a = true; dir = d; return true; }
    if (b.IsClosed(d) && std::fabs(c.uv_b[i][d] - c.uv_b[j][d]) > 0.5 * b.Domain(d).Length()) { on_a = false; dir = d; return true; }
  }
  return false;
}

// The point where the curve crosses a periodic seam between samples i and
// j (which straddle it: `on_a`/`dir` from SeamJumpDir). The seam parameter
// is PINNED at the seam and the other three parameters Newton-solved so
// the result sits exactly on the seam AND on the true curve. Two (u, v)
// copies come back for each surface: [0] carries the seam value on
// sample i's side of the domain (for the piece ending at i), [1] the seam
// value on sample j's side (for the piece starting at j); the 3D point is
// the same for both, so the two pieces weld to one shared vertex.
bool SeamCrossing(const IntersectionCurve& c, size_t i, size_t j, const ON_Surface& a, const ON_Surface& b, bool on_a, int dir, const IntersectOptions& opt, Point3d& p, ON_2dPoint uva[2], ON_2dPoint uvb[2]) {
  const ON_Surface& s = on_a ? a : b;
  const ON_Interval d = s.Domain(dir);
  const double L = d.Length();
  if (L <= 0) return false;
  const double vi = on_a ? c.uv_a[i][dir] : c.uv_b[i][dir];
  const double vj = on_a ? c.uv_a[j][dir] : c.uv_b[j][dir];
  const double vj_un = vj + (vi > vj ? L : -L);
  const double seam_i = vi > vj ? d.Max() : d.Min();
  const double seam_j = vi > vj ? d.Min() : d.Max();
  const double denom = vj_un - vi;
  const double t = std::fabs(denom) > 1e-300 ? Clamp((seam_i - vi) / denom, 0, 1) : 0;
  const ON_2dPoint sa = LerpUV(a, c.uv_a[i], c.uv_a[j], t), sb = LerpUV(b, c.uv_b[i], c.uv_b[j], t);
  double prm[4] = {sa.x, sa.y, sb.x, sb.y};
  const int pinned = (on_a ? 0 : 2) + dir;
  prm[pinned] = seam_i;
  // Three free parameters, three equations (S_a - S_b == 0), seam pinned.
  std::vector<double> x, lo, hi;
  const ON_Surface* srf[4] = {&a, &a, &b, &b};
  for (int k = 0; k < 4; ++k) {
    if (k == pinned) continue;
    x.push_back(prm[k]);
    lo.push_back(srf[k]->Domain(k % 2).Min());
    hi.push_back(srf[k]->Domain(k % 2).Max());
  }
  Residual res = [&](const std::vector<double>& q) {
    double full[4];
    int m = 0;
    for (int k = 0; k < 4; ++k) full[k] = k == pinned ? seam_i : q[static_cast<size_t>(m++)];
    const Point3d pa = a.PointAt(full[0], full[1]), pb = b.PointAt(full[2], full[3]);
    return std::vector<double>{pa.x - pb.x, pa.y - pb.y, pa.z - pb.z};
  };
  if (!NewtonSolve(res, x, lo, hi, opt.tolerance, 40)) return false;
  int m = 0;
  for (int k = 0; k < 4; ++k) if (k != pinned) prm[k] = x[static_cast<size_t>(m++)];
  p = a.PointAt(prm[0], prm[1]);
  // Must still be a point of THIS segment, not a distant solution.
  const double span = std::max(c.points[i].DistanceTo(c.points[j]), opt.tolerance * 10);
  if (p.DistanceTo(c.points[i]) > 2 * span || p.DistanceTo(c.points[j]) > 2 * span) return false;
  // A non-pinned closed direction whose solved value landed exactly on
  // its own domain end is re-expressed on whichever end the neighbouring
  // sample is on, so the pieces stay jump-free there.
  auto side_of = [&](const ON_Surface& srf2, int d2, double v, double ref) {
    const ON_Interval dd = srf2.Domain(d2);
    if (!srf2.IsClosed(d2) || dd.Length() <= 0) return v;
    const double eps = 1e-9 * dd.Length();
    if (v <= dd.Min() + eps && std::fabs(ref - dd.Max()) < std::fabs(ref - dd.Min())) return dd.Max();
    if (v >= dd.Max() - eps && std::fabs(ref - dd.Min()) < std::fabs(ref - dd.Max())) return dd.Min();
    return v;
  };
  for (int copy = 0; copy < 2; ++copy) {
    const size_t ref_idx = copy == 0 ? i : j;
    double out4[4];
    for (int k = 0; k < 4; ++k) {
      if (k == pinned) { out4[k] = copy == 0 ? seam_i : seam_j; continue; }
      const double ref = (k < 2 ? c.uv_a[ref_idx] : c.uv_b[ref_idx])[k % 2];
      out4[k] = side_of(*srf[k], k % 2, prm[k], ref);
    }
    uva[copy] = ON_2dPoint(out4[0], out4[1]);
    uvb[copy] = ON_2dPoint(out4[2], out4[3]);
  }
  return true;
}

// Splits a polyline where a closed surface direction's parameter wraps.
// Each cut inserts the exact seam crossing (SeamCrossing) as the LAST
// point of the piece before it and the FIRST point of the piece after it,
// so both halves end exactly on the seam - previously each half simply
// stopped at its own last sample, one mesh step short of the seam (the
// single seam-column sample belonged to only one of the two), leaving one
// "interior" endpoint per piece that boolean_general.cpp's SplitFaceLoop
// then rightly refused to splice (confirmed on two overlapping spheres:
// neither sphere was ever split at all).
void SplitAtSeams(std::vector<IntersectionCurve>& curves, const ON_Surface& a, const ON_Surface& b, const IntersectOptions& opt) {
  std::vector<IntersectionCurve> out;
  auto jumps = [&](const IntersectionCurve& c, size_t i, size_t j) {
    bool on_a;
    int dir;
    return SeamJumpDir(c, i, j, a, b, on_a, dir);
  };
  for (IntersectionCurve& c : curves) {
    const size_t n = c.points.size();
    std::vector<size_t> cuts;
    for (size_t i = 0; i + 1 < n; ++i) if (jumps(c, i, i + 1)) cuts.push_back(i + 1);
    const bool closed_jump = c.closed && jumps(c, n - 1, 0);
    if (cuts.empty() && !closed_jump) { out.push_back(std::move(c)); continue; }
    // Total number of seam crossings around this curve, counting the
    // closing wraparound edge (last point -> first point) as one more if
    // it is itself a crossing. A closed curve that crosses a seam exactly
    // ONCE (whether that one crossing IS the wraparound edge, or is a
    // single ordinary internal cut that then gets rotated to the front
    // below) is NOT actually an open arc: going once around and crossing
    // the seam once just means the loop's start/end sit on opposite sides
    // of that seam in (u, v) - the two ends are still the SAME physical
    // 3D point. The rotate-then-scan logic below only ever detects a jump
    // between two CONSECUTIVE samples of the (possibly rotated) index
    // array, so whichever single crossing was rotated to sit at the
    // wraparound position is never actually cut there, and the entire loop
    // falls out as one piece - which, before this fix, kept the default
    // (false) `closed` flag, silently turning a genuinely closed loop into
    // a bogus "open" arc with no real endpoints on either face's trim
    // boundary (a confirmed root cause: see boolean_general.cpp's own doc
    // comment on the box-fully-pierced-by-a-cylinder case, where this
    // exact mislabeling made the box's flat faces vs. the cylinder's own
    // periodic wall face produce zero usable splits). Fix: recognize this
    // single-crossing case explicitly and keep the piece closed, trimming
    // the duplicated wrap point if the seam split left one (mirrors
    // IntersectSurfaces()'s own closed-curve duplicate-endpoint
    // convention). TWO OR MORE crossings genuinely do produce separate
    // open arcs (each between two different seam-crossing points) and are
    // left to the general splitting logic below, unchanged.
    const size_t seam_crossings = cuts.size() + (closed_jump ? 1 : 0);
    if (c.closed && seam_crossings <= 1) {
      if (n > 2 && c.points.front().DistanceTo(c.points.back()) <= opt.tolerance) {
        c.points.pop_back();
        c.uv_a.pop_back();
        c.uv_b.pop_back();
      }
      if (c.points.size() >= 3) { out.push_back(std::move(c)); continue; }
    }
    // Rotate a closed curve so it starts at a seam, then cut it open.
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    if (c.closed) {
      const size_t start = closed_jump ? 0 : cuts.front();
      std::rotate(idx.begin(), idx.begin() + static_cast<long>(start), idx.end());
    }
    IntersectionCurve cur;
    const size_t first_out = out.size();
    // Cuts the running piece between samples `prev` and `i` (which straddle
    // a seam), giving both sides the exact seam point.
    auto cut = [&](size_t prev, size_t i, IntersectionCurve& before, IntersectionCurve& after) {
      bool on_a = true;
      int dir = 0;
      Point3d p;
      ON_2dPoint xa[2], xb[2];
      if (!SeamJumpDir(c, prev, i, a, b, on_a, dir) || !SeamCrossing(c, prev, i, a, b, on_a, dir, opt, p, xa, xb)) return;
      if (!before.points.empty() && before.points.back().DistanceTo(p) > opt.tolerance) {
        before.points.push_back(p); before.uv_a.push_back(xa[0]); before.uv_b.push_back(xb[0]);
      }
      if (c.points[i].DistanceTo(p) > opt.tolerance) {
        after.points.insert(after.points.begin(), p);
        after.uv_a.insert(after.uv_a.begin(), xa[1]);
        after.uv_b.insert(after.uv_b.begin(), xb[1]);
      }
    };
    for (size_t k = 0; k < n; ++k) {
      const size_t i = idx[k];
      if (k > 0 && jumps(c, idx[k - 1], i)) {
        IntersectionCurve next;
        cut(idx[k - 1], i, cur, next);
        if (cur.points.size() >= 2) out.push_back(cur);
        cur = std::move(next);
      }
      cur.points.push_back(c.points[i]); cur.uv_a.push_back(c.uv_a[i]); cur.uv_b.push_back(c.uv_b[i]);
    }
    // A closed curve was rotated to start right after a seam crossing, so
    // its wraparound edge (last sample -> first sample) is always one too.
    if (c.closed && n >= 2 && jumps(c, idx[n - 1], idx[0])) {
      IntersectionCurve head;
      cut(idx[n - 1], idx[0], cur, head);
      IntersectionCurve& first = out.size() > first_out ? out[first_out] : cur;
      if (!head.points.empty() && (first.points.empty() || first.points.front().DistanceTo(head.points[0]) > opt.tolerance)) {
        first.points.insert(first.points.begin(), head.points[0]);
        first.uv_a.insert(first.uv_a.begin(), head.uv_a[0]);
        first.uv_b.insert(first.uv_b.begin(), head.uv_b[0]);
      }
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

// The point where the curve leaves the trimmed region of face_a/face_b
// between sample `in_idx` (inside both trims) and sample `out_idx`
// (outside at least one): bisection on the segment's own linear (u, v)
// interpolation (LerpUV, both charts), every candidate Newton-refined onto
// the true curve (RefineSurfaceSurfacePoint) BEFORE FaceContainsUV()
// classifies it, so the result is a genuine curve point sitting on the
// trim polygon to bisection precision, consistent in both charts. Returns
// false when no such point exists beyond the inside sample itself.
bool TrimCrossing(const IntersectionCurve& c, size_t in_idx, size_t out_idx, const ON_BrepFace* face_a, const ON_Surface& a, const ON_BrepFace* face_b, const ON_Surface& b, const IntersectOptions& opt, Point3d& p, ON_2dPoint& uva, ON_2dPoint& uvb) {
  const Point3d& p0 = c.points[in_idx];
  const Point3d& p1 = c.points[out_idx];
  const double span = std::max(p0.DistanceTo(p1), opt.tolerance * 10);
  auto inside_at = [&](double t, Point3d& P, ON_2dPoint& A, ON_2dPoint& B) {
    const ON_2dPoint sa = LerpUV(a, c.uv_a[in_idx], c.uv_a[out_idx], t), sb = LerpUV(b, c.uv_b[in_idx], c.uv_b[out_idx], t);
    double ua = sa.x, va = sa.y, ub = sb.x, vb = sb.y;
    if (!RefineSurfaceSurfacePoint(a, b, ua, va, ub, vb, opt.tolerance)) return false;
    P = a.PointAt(ua, va);
    const Point3d chord = p0 + (p1 - p0) * t;
    if (P.DistanceTo(chord) > 2 * span) return false;  // wandered to a distant solution
    A = ON_2dPoint(ua, va); B = ON_2dPoint(ub, vb);
    if (face_a && !FaceContainsUV(*face_a, ua, va)) return false;
    if (face_b && !FaceContainsUV(*face_b, ub, vb)) return false;
    return true;
  };
  double lo = 0, hi = 1;
  bool found = false;
  for (int it = 0; it < 60 && hi - lo > 1e-12; ++it) {
    const double mid = 0.5 * (lo + hi);
    Point3d P;
    ON_2dPoint A, B;
    if (inside_at(mid, P, A, B)) { lo = mid; p = P; uva = A; uvb = B; found = true; }
    else hi = mid;
  }
  return found;
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
  SplitAtSeams(out, a, b, opt);
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
    // Runs of inside points (a closed curve is opened at the first outside
    // point). Every in/out transition - however many a curve has, in
    // either direction, including a closed curve's own wraparound edge -
    // gets the exact trim-boundary crossing (TrimCrossing) as the run's
    // own endpoint. Merely DROPPING the outside samples, as this used to,
    // left every clipped run one mesh step short of the trim boundary, so
    // its endpoint never met the neighbouring face-pair's own piece (which
    // starts exactly on that boundary, where the opposing face's own trim
    // rectangle clamps it) - the confirmed root cause of every
    // FromPlanarFaces/FromMixedFaces operand failing where Box()/Sphere()
    // passed (see boolean_general.cpp's own doc comment).
    size_t start = 0;
    if (c.closed) { while (start < n && in[start]) ++start; }
    IntersectionCurve cur;
    auto add = [&](const Point3d& p, const ON_2dPoint& ua, const ON_2dPoint& ub) {
      if (!cur.points.empty() && cur.points.back().DistanceTo(p) <= opt.tolerance) return;
      cur.points.push_back(p); cur.uv_a.push_back(ua); cur.uv_b.push_back(ub);
    };
    auto add_crossing = [&](size_t in_idx, size_t out_idx) {
      Point3d p;
      ON_2dPoint ua, ub;
      if (TrimCrossing(c, in_idx, out_idx, face_a, a, face_b, b, opt, p, ua, ub)) add(p, ua, ub);
    };
    auto flush = [&]() {
      if (cur.points.size() >= 2) { FinishCurve(cur, a, b, opt); out.push_back(cur); }
      cur = IntersectionCurve();
    };
    size_t prev = n;
    for (size_t k = 0; k < n; ++k) {
      const size_t i = (start + k) % n;
      if (in[i]) {
        if (prev < n && !in[prev]) add_crossing(i, prev);  // entering: crossing first
        add(c.points[i], c.uv_a[i], c.uv_b[i]);
      } else {
        if (prev < n && in[prev]) add_crossing(prev, i);  // leaving: crossing last
        flush();
      }
      prev = i;
    }
    // A closed curve's walk ends one step before `start` (outside) - that
    // wraparound edge is a leaving transition too if the last sample is in.
    if (c.closed && prev < n && in[prev] && !in[start]) add_crossing(prev, start);
    flush();
  }
  return out;
}

std::vector<BrepBrepIntersection> IntersectBreps(const ON_Brep& a, const ON_Brep& b, const IntersectOptions& opt) {
  std::vector<BrepBrepIntersection> out;
  const int na = a.m_F.Count();
  const int nb = b.m_F.Count();
  std::vector<ON_BoundingBox> boxes_a(static_cast<size_t>(na)), boxes_b(static_cast<size_t>(nb));
  for (int i = 0; i < na; ++i) boxes_a[static_cast<size_t>(i)] = a.m_F[i].SurfaceOf()->BoundingBox();
  for (int j = 0; j < nb; ++j) boxes_b[static_cast<size_t>(j)] = b.m_F[j].SurfaceOf()->BoundingBox();
  const double pad = std::max(opt.mesh_tolerance, opt.tolerance * 4);
  for (int i = 0; i < na; ++i) {
    ON_BoundingBox exp_a = boxes_a[static_cast<size_t>(i)];
    exp_a.m_min -= ON_3dVector(pad, pad, pad);
    exp_a.m_max += ON_3dVector(pad, pad, pad);
    const ON_BrepFace& fa = a.m_F[i];
    for (int j = 0; j < nb; ++j) {
      if (exp_a.IsDisjoint(boxes_b[static_cast<size_t>(j)])) continue;
      const ON_BrepFace& fb = b.m_F[j];
      for (IntersectionCurve& ic : IntersectFaces(&fa, *fa.SurfaceOf(), &fb, *fb.SurfaceOf(), opt)) {
        out.push_back(BrepBrepIntersection{i, j, std::move(ic)});
      }
    }
  }
  return out;
}

std::vector<BrepPlaneIntersection> IntersectBrepByPlane(const ON_Brep& b, const ON_Plane& plane, const IntersectOptions& opt) {
  std::vector<BrepPlaneIntersection> out;
  // Deliberately NOT b.BoundingBox() - a real bug found while extending
  // this file (see ContourBrep()'s own identical fix, added alongside
  // this one): this OpenNURBS version's ON_Brep::GetBBox() tightens each
  // face's cached bbox against its own trim loop's 2D parameter-space
  // bbox before unioning them, which can come back degenerate for this
  // kernel's own untrimmed Box()-style faces (confirmed directly: a
  // Brep::Box(0,0,0,3,3,3)'s own b.BoundingBox() returns min=(0,0,0)
  // max=(3,0,0) - y and z silently collapsed to zero - while every
  // individual face's own SurfaceOf()->BoundingBox(), unioned below
  // instead, is exactly correct). Left unfixed, this would have
  // undersized `half` just below and defeated the very "never silently
  // clipped at the plane surface's own edge" guarantee this function's
  // own doc comment promises.
  ON_BoundingBox bbox;
  {
    const int nf0 = b.m_F.Count();
    for (int i = 0; i < nf0; ++i) {
      const ON_Surface* s0 = b.m_F[i].SurfaceOf();
      if (s0) bbox.Union(s0->BoundingBox());
    }
  }
  if (!bbox.IsValid() || !plane.IsValid()) return out;
  // The plane surface must reach past every face that could genuinely meet
  // it - sized off the WHOLE Brep's own bounding box (doubled) rather than
  // a caller-guessed rectangle, so a real section point is never silently
  // clipped at the plane surface's own edge (the same hazard
  // IntersectCurvePlane()'s own doc comment raises for a bounded
  // ON_PlaneSurface stand-in).
  const double half = std::max(bbox.Diagonal().Length(), 1.0) * 2;
  ON_PlaneSurface plane_surface(plane);
  plane_surface.SetExtents(0, ON_Interval(-half, half), true);
  plane_surface.SetExtents(1, ON_Interval(-half, half), true);
  const double pad = std::max(opt.mesh_tolerance, opt.tolerance * 4);
  const int nf = b.m_F.Count();
  for (int i = 0; i < nf; ++i) {
    const ON_BrepFace& f = b.m_F[i];
    const ON_Surface* s = f.SurfaceOf();
    const ON_BoundingBox fb = s->BoundingBox();
    if (fb.IsValid()) {
      const double reach = fb.Diagonal().Length() * 0.5 + pad;
      if (std::fabs(plane.DistanceTo(fb.Center())) > reach) continue;  // face's own box can't reach the plane
    }
    for (IntersectionCurve& ic : IntersectFaces(&f, *s, nullptr, plane_surface, opt)) {
      out.push_back(BrepPlaneIntersection{i, std::move(ic)});
    }
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

std::vector<CurveBrepHit> IntersectCurveBrep(const ON_Curve& c, const ON_Brep& b, const IntersectOptions& opt) {
  std::vector<CurveBrepHit> out;
  const ON_BoundingBox cb = c.BoundingBox();
  const double pad = std::max(opt.mesh_tolerance, opt.tolerance * 4);
  const int nb = b.m_F.Count();
  for (int j = 0; j < nb; ++j) {
    const ON_BrepFace& f = b.m_F[j];
    const ON_Surface* s = f.SurfaceOf();
    ON_BoundingBox fb = s->BoundingBox();
    fb.m_min -= ON_3dVector(pad, pad, pad);
    fb.m_max += ON_3dVector(pad, pad, pad);
    if (cb.IsValid() && fb.IsValid() && cb.IsDisjoint(fb)) continue;
    for (const CurveSurfaceHit& h : IntersectCurveSurface(c, *s, opt)) {
      if (!FaceContainsUV(f, h.uv.x, h.uv.y)) continue;
      out.push_back(CurveBrepHit{j, h});
    }
  }
  return out;
}

// Curve/infinite-plane crossings: bisection-seeded, then Newton-refined on
// the plane's own implicit signed-distance equation (a single scalar
// residual, unlike IntersectCurveSurface()'s 3-unknown (t, u, v) system -
// a plane's own equation is already closed-form, no surface evaluation or
// mesh seed needed at all).
std::vector<CurvePlaneHit> IntersectCurvePlane(const ON_Curve& c, const ON_Plane& plane, const IntersectOptions& opt) {
  std::vector<CurvePlaneHit> hits;
  const ON_Interval d = c.Domain();
  const ON_BoundingBox cb = c.BoundingBox();
  const double clen = cb.IsValid() ? cb.Diagonal().Length() : 1;
  const int n = static_cast<int>(Clamp(std::ceil(clen / std::max(opt.mesh_tolerance, 1e-6)), 64, 2000));
  std::vector<double> params(static_cast<size_t>(n) + 1), dist(static_cast<size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) {
    params[static_cast<size_t>(i)] = d.ParameterAt(static_cast<double>(i) / n);
    dist[static_cast<size_t>(i)] = plane.DistanceTo(c.PointAt(params[static_cast<size_t>(i)]));
  }
  const std::vector<double> lo = {d.Min()}, hi = {d.Max()};
  for (int i = 0; i < n; ++i) {
    const double d0 = dist[static_cast<size_t>(i)], d1 = dist[static_cast<size_t>(i) + 1];
    if ((d0 < 0) == (d1 < 0) && d0 != 0.0 && d1 != 0.0) continue;  // no sign change in this span
    if (d0 == d1) continue;  // degenerate (flat) span - no isolated crossing to seed
    const double seed_t = params[static_cast<size_t>(i)] +
                           (params[static_cast<size_t>(i) + 1] - params[static_cast<size_t>(i)]) * (-d0 / (d1 - d0));
    std::vector<double> x = {seed_t};
    Residual res = [&](const std::vector<double>& q) { return std::vector<double>{plane.DistanceTo(c.PointAt(q[0]))}; };
    double err = 0;
    if (!NewtonSolve(res, x, lo, hi, opt.tolerance, 40, &err)) continue;
    CurvePlaneHit h;
    h.t = x[0];
    h.point = c.PointAt(h.t);
    h.error = std::fabs(err);
    bool dup = false;
    for (const CurvePlaneHit& o : hits) if (o.point.DistanceTo(h.point) <= opt.tolerance * 4) { dup = true; break; }
    if (!dup) hits.push_back(h);
  }
  std::sort(hits.begin(), hits.end(), [](const CurvePlaneHit& a, const CurvePlaneHit& b) { return a.t < b.t; });
  return hits;
}

namespace {

// Cheap axis-aligned-box overlap test (each segment's own box padded by
// `pad`), used to skip the exact closest-point computation below for the
// large majority of segment pairs that plainly can't be within `pad` of
// each other. Never a false negative: any two segments actually within
// `pad` have overlapping padded boxes on every axis.
bool SegmentBoxesOverlap(const Point3d& p0, const Point3d& p1, const Point3d& q0, const Point3d& q1, double pad) {
  auto axis_overlaps = [&](double a0, double a1, double b0, double b1) {
    const double alo = std::min(a0, a1), ahi = std::max(a0, a1);
    const double blo = std::min(b0, b1), bhi = std::max(b0, b1);
    return alo - pad <= bhi && blo - pad <= ahi;
  };
  return axis_overlaps(p0.x, p1.x, q0.x, q1.x) && axis_overlaps(p0.y, p1.y, q0.y, q1.y) &&
         axis_overlaps(p0.z, p1.z, q0.z, q1.z);
}

// Closest points between two 3D segments p0-p1 and q0-q1 (Ericson,
// "Real-Time Collision Detection" 5.1.9 - the same textbook Mesh::
// ClosestPoint()'s own point/triangle region test already cites), used
// here to seed IntersectCurves() the way SegTri() above seeds
// IntersectCurveSurface(). Returns the squared distance at closest
// approach and the two segment parameters s, t in [0, 1] (on p and q
// respectively) at which it occurs - exact closed-form clamped-Voronoi-
// region math, not an iterative search.
double ClosestSegmentSegment(const Point3d& p0, const Point3d& p1, const Point3d& q0, const Point3d& q1, double& s, double& t) {
  const Vector3d d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
  const double a = ON_DotProduct(d1, d1), e = ON_DotProduct(d2, d2), f = ON_DotProduct(d2, r);
  const double kEps = 1e-20;
  if (a <= kEps && e <= kEps) {
    s = 0; t = 0;
  } else if (a <= kEps) {
    s = 0; t = Clamp(f / e, 0, 1);
  } else {
    const double c = ON_DotProduct(d1, r);
    if (e <= kEps) {
      t = 0; s = Clamp(-c / a, 0, 1);
    } else {
      const double b = ON_DotProduct(d1, d2);
      const double denom = a * e - b * b;
      s = denom > kEps ? Clamp((b * f - c * e) / denom, 0, 1) : 0;
      t = (b * s + f) / e;
      if (t < 0) {
        t = 0; s = Clamp(-c / a, 0, 1);
      } else if (t > 1) {
        t = 1; s = Clamp((b - c) / a, 0, 1);
      }
    }
  }
  const Point3d cp = p0 + d1 * s, cq = q0 + d2 * t;
  const Vector3d diff = cp - cq;
  return ON_DotProduct(diff, diff);
}

}  // namespace

std::vector<CurveCurveHit> IntersectCurves(const ON_Curve& a, const ON_Curve& b, const IntersectOptions& opt) {
  std::vector<CurveCurveHit> hits;
  const ON_Interval da = a.Domain(), db = b.Domain();
  const ON_BoundingBox ba = a.BoundingBox(), bb = b.BoundingBox();
  const double len_a = ba.IsValid() ? ba.Diagonal().Length() : 1;
  const double len_b = bb.IsValid() ? bb.Diagonal().Length() : 1;
  const double step = std::max(opt.mesh_tolerance, 1e-6);
  const int na = static_cast<int>(Clamp(std::ceil(len_a / step), 64, 2000));
  const int nb = static_cast<int>(Clamp(std::ceil(len_b / step), 64, 2000));

  std::vector<Point3d> sa(static_cast<size_t>(na) + 1), sb(static_cast<size_t>(nb) + 1);
  for (int i = 0; i <= na; ++i) sa[static_cast<size_t>(i)] = a.PointAt(da.ParameterAt(static_cast<double>(i) / na));
  for (int j = 0; j <= nb; ++j) sb[static_cast<size_t>(j)] = b.PointAt(db.ParameterAt(static_cast<double>(j) / nb));

  const double pad = std::max(opt.mesh_tolerance * 4, 1e-9);
  struct Seed { double ta, tb; };
  std::vector<Seed> seeds;
  for (int i = 0; i < na; ++i) {
    const Point3d& p0 = sa[static_cast<size_t>(i)];
    const Point3d& p1 = sa[static_cast<size_t>(i) + 1];
    for (int j = 0; j < nb; ++j) {
      const Point3d& q0 = sb[static_cast<size_t>(j)];
      const Point3d& q1 = sb[static_cast<size_t>(j) + 1];
      if (!SegmentBoxesOverlap(p0, p1, q0, q1, pad)) continue;
      double s = 0, t = 0;
      if (ClosestSegmentSegment(p0, p1, q0, q1, s, t) > pad * pad) continue;
      seeds.push_back({da.ParameterAt((i + s) / na), db.ParameterAt((j + t) / nb)});
    }
  }

  const std::vector<double> lo = {da.Min(), db.Min()}, hi = {da.Max(), db.Max()};
  for (const Seed& sd : seeds) {
    std::vector<double> x = {sd.ta, sd.tb};
    Residual res = [&](const std::vector<double>& q) {
      const Point3d pa = a.PointAt(q[0]), pb = b.PointAt(q[1]);
      return std::vector<double>{pa.x - pb.x, pa.y - pb.y, pa.z - pb.z};
    };
    double err = 0;
    if (!NewtonSolve(res, x, lo, hi, opt.tolerance, 40, &err)) continue;
    CurveCurveHit h;
    h.ta = x[0];
    h.tb = x[1];
    const Point3d pa = a.PointAt(h.ta), pb = b.PointAt(h.tb);
    h.point = Point3d((pa.x + pb.x) / 2, (pa.y + pb.y) / 2, (pa.z + pb.z) / 2);
    h.error = err;
    bool dup = false;
    for (const CurveCurveHit& o : hits) {
      if (o.point.DistanceTo(h.point) <= opt.tolerance * 4) { dup = true; break; }
    }
    if (!dup) hits.push_back(h);
  }
  std::sort(hits.begin(), hits.end(), [](const CurveCurveHit& x, const CurveCurveHit& y) { return x.ta < y.ta; });
  return hits;
}

std::vector<CurveCurveHit> IntersectCurveSelfIntersections(const ON_Curve& c, const IntersectOptions& opt) {
  std::vector<CurveCurveHit> hits;
  const ON_Interval d = c.Domain();
  const ON_BoundingBox cb = c.BoundingBox();
  const double len = cb.IsValid() ? cb.Diagonal().Length() : 1;
  const double step = std::max(opt.mesh_tolerance, 1e-6);
  const int n = static_cast<int>(Clamp(std::ceil(len / step), 64, 2000));

  std::vector<Point3d> s(static_cast<size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) s[static_cast<size_t>(i)] = c.PointAt(d.ParameterAt(static_cast<double>(i) / n));

  const bool closed = c.IsClosed();
  const double pad = std::max(opt.mesh_tolerance * 4, 1e-9);
  // Minimum separation (in sample-index steps) two segments must have
  // before they're even considered as a candidate crossing - immediately
  // adjacent segments share (or nearly share) an endpoint by construction
  // (ordinary curve continuity), not a self-intersection.
  const int min_gap = 2;

  struct Seed { double ta, tb; };
  std::vector<Seed> seeds;
  for (int i = 0; i < n; ++i) {
    const Point3d& p0 = s[static_cast<size_t>(i)];
    const Point3d& p1 = s[static_cast<size_t>(i) + 1];
    for (int j = i + 1; j < n; ++j) {
      int gap = j - i;
      if (closed) gap = std::min(gap, n - gap);
      if (gap < min_gap) continue;
      const Point3d& q0 = s[static_cast<size_t>(j)];
      const Point3d& q1 = s[static_cast<size_t>(j) + 1];
      if (!SegmentBoxesOverlap(p0, p1, q0, q1, pad)) continue;
      double ss = 0, tt = 0;
      if (ClosestSegmentSegment(p0, p1, q0, q1, ss, tt) > pad * pad) continue;
      seeds.push_back({d.ParameterAt((i + ss) / n), d.ParameterAt((j + tt) / n)});
    }
  }

  const std::vector<double> lo = {d.Min(), d.Min()}, hi = {d.Max(), d.Max()};
  const double domain_len = d.Length();
  const double min_param_sep = step * min_gap * 0.5;
  for (const Seed& sd : seeds) {
    std::vector<double> x = {sd.ta, sd.tb};
    Residual res = [&](const std::vector<double>& q) {
      const Point3d pa = c.PointAt(q[0]), pb = c.PointAt(q[1]);
      return std::vector<double>{pa.x - pb.x, pa.y - pb.y, pa.z - pb.z};
    };
    double err = 0;
    if (!NewtonSolve(res, x, lo, hi, opt.tolerance, 40, &err)) continue;
    // Re-check the same "not a real crossing" separation post-refinement -
    // Newton is free to walk its seed back toward the trivial diagonal
    // (ta == tb), which min_gap's pre-seed filter above cannot catch once
    // that's happened.
    double dt = std::fabs(x[0] - x[1]);
    if (closed) dt = std::min(dt, domain_len - dt);
    if (dt <= min_param_sep) continue;
    CurveCurveHit h;
    h.ta = x[0];
    h.tb = x[1];
    const Point3d pa = c.PointAt(h.ta), pb = c.PointAt(h.tb);
    h.point = Point3d((pa.x + pb.x) / 2, (pa.y + pb.y) / 2, (pa.z + pb.z) / 2);
    h.error = err;
    bool dup = false;
    for (const CurveCurveHit& o : hits) {
      if (o.point.DistanceTo(h.point) <= opt.tolerance * 4) { dup = true; break; }
    }
    if (!dup) hits.push_back(h);
  }
  std::sort(hits.begin(), hits.end(), [](const CurveCurveHit& x, const CurveCurveHit& y) { return x.ta < y.ta; });
  return hits;
}

PullbackResult PullbackCurveToSurface(const ON_Curve& c, const ON_Surface& s, const IntersectOptions& opt) {
  PullbackResult out;
  const ON_Interval d = c.Domain();
  if (!d.IsIncreasing()) return out;
  const ON_BoundingBox cb = c.BoundingBox();
  const double clen = cb.IsValid() ? cb.Diagonal().Length() : 1;
  const int n = static_cast<int>(Clamp(std::ceil(clen / std::max(opt.mesh_tolerance, 1e-6)), 64, 2000));
  const bool closed = c.IsClosed() != 0;

  double u = 0, v = 0;
  double prev_err = 0;
  for (int i = 0; i <= n; ++i) {
    // A closed curve's last sample coincides with its first; InterpolateCubic's
    // own closed-curve contract wants that duplicate point OMITTED (it wraps
    // the seam itself - see its header doc comment), matching the same
    // dedup IntersectSurfaces() applies to its own closed SSX curves.
    if (closed && i == n) break;
    const double t = d.ParameterAt(static_cast<double>(i) / n);
    const Point3d p = c.PointAt(t);
    bool ok;
    if (i == 0) {
      ok = SurfaceClosestPointGlobal(s, p, u, v);
    } else {
      ok = SurfaceClosestPoint(s, p, u, v);  // seeded from the PREVIOUS sample's (u, v)
      const double try_err = ok ? s.PointAt(u, v).DistanceTo(p) : std::numeric_limits<double>::max();
      // Re-seed globally when the warm local seed either failed to converge
      // or landed implausibly far from this sample relative to how close
      // the previous sample managed to land - a warm seed that wandered
      // off a disconnected sheet, or across an awkward periodic seam,
      // self-corrects here instead of silently drifting for the rest of
      // the curve.
      if (!ok || try_err > std::max(opt.mesh_tolerance * 4, prev_err * 8 + 1e-9)) {
        double gu = u, gv = v;
        const bool gok = SurfaceClosestPointGlobal(s, p, gu, gv);
        const double gerr = s.PointAt(gu, gv).DistanceTo(p);
        if (gok && gerr < try_err) { u = gu; v = gv; }
      }
    }
    const double err = s.PointAt(u, v).DistanceTo(p);
    prev_err = err;
    out.max_error = std::max(out.max_error, err);
    out.t.push_back(t);
    out.uv.emplace_back(u, v);
  }
  out.on_surface = out.max_error <= opt.tolerance;
  if (out.uv.size() < 2) return out;

  std::vector<ON_3dPoint> pa, p3;
  pa.reserve(out.uv.size());
  p3.reserve(out.uv.size());
  for (const ON_2dPoint& p : out.uv) {
    pa.emplace_back(p.x, p.y, 0.0);
    const Point3d back = s.PointAt(p.x, p.y);
    p3.emplace_back(back.x, back.y, back.z);
  }
  // Unwrap `pa` across either periodic surface direction before fitting.
  // Each (u, v) in `out.uv` above comes from an independent closest-point
  // search and is wrapped to the surface's own stated domain, so a 3D
  // curve that physically crosses a periodic seam (e.g. a path on a
  // cylinder wall passing through its own angular seam) produces a sample
  // sequence that jumps from near one domain edge to near the other
  // between two adjacent, physically-close samples. Fitting `pa` as given
  // through that jump makes the cubic swing through the middle of the
  // domain for that one stretch - the "not unwrapped" limitation this
  // function's own header comment used to disclose. The fix is the same
  // one `LerpUV`/`SplitAtSeams` already apply to SSX pcurves elsewhere in
  // this file: whenever two consecutive samples in a closed direction
  // differ by more than half that direction's domain length, shift the
  // later one by a whole period so the sequence keeps moving the way it
  // was already moving instead of jumping back through the middle; the
  // fitted pcurve can then legitimately carry parameter values outside the
  // surface's nominal domain for that stretch, the same over-range
  // convention a seam-crossing trim pcurve already uses elsewhere in
  // OpenNURBS-based kernels. `out.uv` itself (the per-sample field other
  // callers read) is left untouched - only the curve fit into `pcurve`
  // (and, via the shared chord-length `out.params`, `pulled_curve`'s own
  // parametrization) is affected.
  for (int dir = 0; dir < 2; ++dir) {
    if (!s.IsClosed(dir)) continue;
    const double L = s.Domain(dir).Length();
    if (L <= 0) continue;
    for (size_t i = 1; i < pa.size(); ++i) {
      double& cur = dir == 0 ? pa[i].x : pa[i].y;
      const double prev = dir == 0 ? pa[i - 1].x : pa[i - 1].y;
      while (cur - prev > 0.5 * L) cur -= L;
      while (prev - cur > 0.5 * L) cur += L;
    }
  }
  out.params = ChordParams(pa, closed);
  out.pcurve = InterpolateCubic(pa, out.params, closed, 2);
  out.pulled_curve = InterpolateCubic(p3, out.params, closed, 3);
  return out;
}

namespace {

// Global seed for the ray/surface system below: scans a grid of the
// surface's own parameter domain (the same grid shape
// SurfaceClosestPointGlobal() uses) and keeps whichever grid point has the
// smallest PERPENDICULAR distance to the infinite line through `p` along
// `dir` - not the smallest distance to `p` itself, since a ray and a
// closest-point search are genuinely different questions (a point far
// along the ray from `p` can still be the correct hit).
bool RaySurfaceGlobalSeed(const ON_Surface& s, Point3d p, const Vector3d& dir, double& u, double& v, int grid = 24) {
  const double dir_len2 = dir.LengthSquared();
  if (dir_len2 <= 0) return false;
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  double best = std::numeric_limits<double>::max();
  bool found = false;
  for (int i = 0; i <= grid; ++i) {
    for (int j = 0; j <= grid; ++j) {
      const double uu = du.ParameterAt(static_cast<double>(i) / grid);
      const double vv = dv.ParameterAt(static_cast<double>(j) / grid);
      const Vector3d w = s.PointAt(uu, vv) - p;
      const double t = ON_DotProduct(w, dir) / dir_len2;
      const Vector3d perp = w - dir * t;
      const double d = perp.Length();
      if (d < best) { best = d; u = uu; v = vv; found = true; }
    }
  }
  return found;
}

// Newton solve of the 3-unknown ray/surface system `p + t*dir == S(u, v)`
// from a seed (t, u, v) - the directional-projection counterpart to
// RefineSurfaceSurfacePoint()/IntersectCurveSurface()'s own 3-unknown
// (t, u, v) system, but against a fixed ray instead of a second curve.
bool RaySurfaceNewton(const ON_Surface& s, Point3d p, const Vector3d& dir, double& t, double& u, double& v, double tol, double t_lo, double t_hi) {
  std::vector<double> x = {t, u, v};
  const std::vector<double> lo = {t_lo, s.Domain(0).Min(), s.Domain(1).Min()};
  const std::vector<double> hi = {t_hi, s.Domain(0).Max(), s.Domain(1).Max()};
  Residual res = [&](const std::vector<double>& q) {
    const Point3d target = p + dir * q[0];
    const Point3d sp = s.PointAt(q[1], q[2]);
    return std::vector<double>{target.x - sp.x, target.y - sp.y, target.z - sp.z};
  };
  const bool ok = NewtonSolve(res, x, lo, hi, tol);
  t = x[0]; u = x[1]; v = x[2];
  return ok;
}

}  // namespace

PointProjectionHit ProjectPointToSurface(Point3d point, const Vector3d& direction, const ON_Surface& s, const IntersectOptions& opt) {
  PointProjectionHit out;
  if (direction.LengthSquared() <= 0) return out;
  const ON_BoundingBox sb = s.BoundingBox();
  double span = 1;
  if (sb.IsValid()) span = sb.Diagonal().Length() + sb.Center().DistanceTo(point);
  span = std::max(span, 1.0) * 4;

  double u = 0, v = 0;
  if (!RaySurfaceGlobalSeed(s, point, direction, u, v)) return out;
  double t = 0;
  if (!RaySurfaceNewton(s, point, direction, t, u, v, opt.tolerance, -span, span)) return out;
  out.hit = true;
  out.t = t;
  out.uv = ON_2dPoint(u, v);
  out.point = point + direction * t;
  return out;
}

ProjectedCurveResult ProjectCurveToSurface(const ON_Curve& c, const ON_Surface& s, const Vector3d& direction, const IntersectOptions& opt) {
  ProjectedCurveResult out;
  if (direction.LengthSquared() <= 0) return out;
  const ON_Interval d = c.Domain();
  if (!d.IsIncreasing()) return out;
  const ON_BoundingBox cb = c.BoundingBox();
  const ON_BoundingBox sb = s.BoundingBox();
  const double clen = cb.IsValid() ? cb.Diagonal().Length() : 1;
  const int n = static_cast<int>(Clamp(std::ceil(clen / std::max(opt.mesh_tolerance, 1e-6)), 64, 2000));
  const bool closed = c.IsClosed() != 0;
  // Generous enough |t| bounds that a genuine hit anywhere between the
  // curve's own bounding box and the surface's own bounding box - either
  // side of the sample point - is never clipped by the Newton solve's own
  // box bounds.
  double span = clen + 1;
  if (sb.IsValid()) span += sb.Diagonal().Length() + (cb.IsValid() ? cb.Center().DistanceTo(sb.Center()) : 0);
  span *= 4;

  double u = 0, v = 0;
  bool have_seed = false;
  for (int i = 0; i <= n; ++i) {
    if (closed && i == n) break;
    const double tc = d.ParameterAt(static_cast<double>(i) / n);
    const Point3d p = c.PointAt(tc);
    ++out.sample_count;

    double tt = 0, uu = u, vv = v;
    bool ok = have_seed && RaySurfaceNewton(s, p, direction, tt, uu, vv, opt.tolerance, -span, span);
    if (!ok) {
      double gu, gv;
      if (RaySurfaceGlobalSeed(s, p, direction, gu, gv)) {
        tt = 0; uu = gu; vv = gv;
        ok = RaySurfaceNewton(s, p, direction, tt, uu, vv, opt.tolerance, -span, span);
      }
    }
    out.hit.push_back(ok);
    if (ok) {
      out.points.push_back(p + direction * tt);
      out.uv.emplace_back(uu, vv);
      out.t.push_back(tc);
      u = uu; v = vv; have_seed = true;
    }
  }
  out.hit_count = static_cast<int>(out.points.size());
  if (out.hit_count < 2) return out;

  std::vector<ON_3dPoint> p3;
  p3.reserve(out.points.size());
  for (const Point3d& p : out.points) p3.emplace_back(p.x, p.y, p.z);
  const bool fit_closed = closed && out.hit_count == out.sample_count;
  const std::vector<double> params = ChordParams(p3, fit_closed);
  out.projected_curve = InterpolateCubic(p3, params, fit_closed, 3);
  return out;
}

namespace {

// Shared core of IntersectCurveSurfaceOverlap()/IntersectCurveBrepOverlap():
// `trim_ok(u, v)` additionally gates whether a given (u, v) counts as
// "on" - the plain curve/surface entry point below passes a trivial
// always-true gate, IntersectCurveBrepOverlap() passes FaceContainsUV() so
// a span never silently crosses a face's own trim boundary.
template <typename TrimOk>
std::vector<CurveSurfaceOverlap> IntersectCurveSurfaceOverlapImpl(const ON_Curve& c, const ON_Surface& s, const IntersectOptions& opt, TrimOk trim_ok) {
  std::vector<CurveSurfaceOverlap> out;
  const ON_Interval d = c.Domain();
  if (!d.IsIncreasing()) return out;
  const ON_BoundingBox cb = c.BoundingBox();
  const double clen = cb.IsValid() ? cb.Diagonal().Length() : 1;
  const int n = static_cast<int>(Clamp(std::ceil(clen / std::max(opt.mesh_tolerance, 1e-6)), 64, 2000));

  // Projects `t` onto `s`, seeded from (seed_u, seed_v), re-seeding
  // globally on failure or a poor fit - the identical discipline the main
  // sampling loop below uses for every sample after the first. Returns
  // whether the projection is both within opt.tolerance AND trim_ok().
  auto on_surface = [&](double t, double seed_u, double seed_v, double& out_u, double& out_v) -> bool {
    const Point3d p = c.PointAt(t);
    double u = seed_u, v = seed_v;
    bool ok = SurfaceClosestPoint(s, p, u, v);
    double err = ok ? s.PointAt(u, v).DistanceTo(p) : std::numeric_limits<double>::max();
    if (!ok || err > std::max(opt.mesh_tolerance * 4, 1e-9)) {
      double gu = u, gv = v;
      const bool gok = SurfaceClosestPointGlobal(s, p, gu, gv);
      const double gerr = gok ? s.PointAt(gu, gv).DistanceTo(p) : std::numeric_limits<double>::max();
      if (gok && gerr < err) { u = gu; v = gv; err = gerr; ok = gok; }
    }
    out_u = u; out_v = v;
    return ok && err <= opt.tolerance && trim_ok(u, v);
  };

  std::vector<double> ts(static_cast<size_t>(n) + 1);
  std::vector<ON_2dPoint> uvs(static_cast<size_t>(n) + 1);
  std::vector<bool> on(static_cast<size_t>(n) + 1, false);
  double u = 0, v = 0;
  for (int i = 0; i <= n; ++i) {
    const double t = d.ParameterAt(static_cast<double>(i) / n);
    ts[static_cast<size_t>(i)] = t;
    if (i == 0) SurfaceClosestPointGlobal(s, c.PointAt(t), u, v);
    on[static_cast<size_t>(i)] = on_surface(t, u, v, u, v);  // seeded from the PREVIOUS sample's (u, v), same discipline as PullbackCurveToSurface()
    uvs[static_cast<size_t>(i)] = ON_2dPoint(u, v);
  }

  // Bisection-refines the boundary between a known off-surface sample at
  // `t_off` and a known on-surface sample at `t_on` (seeded (u, v)
  // `uv_on`) against the identical on_surface() predicate the sampling
  // loop above uses, narrowing the result (biased toward the on-surface
  // side, i.e. the tightest still-on-surface parameter found) to
  // double-precision bisection width - the exact crossing
  // IntersectCurveSurface() would find near this same seed.
  auto refine_boundary = [&](double t_off, double t_on, ON_2dPoint uv_on) -> double {
    double lo = t_off, hi = t_on;
    double seed_u = uv_on.x, seed_v = uv_on.y;
    for (int iter = 0; iter < 60 && lo != hi; ++iter) {
      const double mid = lo + (hi - lo) * 0.5;
      if (mid == lo || mid == hi) break;  // hit double precision - no narrower midpoint exists
      double mu, mv;
      if (on_surface(mid, seed_u, seed_v, mu, mv)) { hi = mid; seed_u = mu; seed_v = mv; } else { lo = mid; }
    }
    return hi;
  };

  // Merge every maximal run of consecutive on-surface samples into one
  // span; a single isolated on-surface sample (both neighbours off) is a
  // transient touch, not an overlap, and is left for IntersectCurveSurface()
  // to report as a discrete crossing instead. Each non-domain-endpoint
  // boundary of the resulting span is then bisection-refined above.
  int i = 0;
  while (i <= n) {
    if (!on[static_cast<size_t>(i)]) { ++i; continue; }
    int j = i;
    while (j <= n && on[static_cast<size_t>(j)]) ++j;
    if (j - 1 > i) {
      CurveSurfaceOverlap ov;
      ov.t0 = (i > 0) ? refine_boundary(ts[static_cast<size_t>(i - 1)], ts[static_cast<size_t>(i)], uvs[static_cast<size_t>(i)]) : ts[static_cast<size_t>(i)];
      ov.t1 = (j <= n) ? refine_boundary(ts[static_cast<size_t>(j)], ts[static_cast<size_t>(j - 1)], uvs[static_cast<size_t>(j - 1)]) : ts[static_cast<size_t>(j - 1)];
      ov.entire_curve = (i == 0 && j - 1 == n);
      out.push_back(ov);
    }
    i = j;
  }
  return out;
}

}  // namespace

std::vector<CurveSurfaceOverlap> IntersectCurveSurfaceOverlap(const ON_Curve& c, const ON_Surface& s, const IntersectOptions& opt) {
  return IntersectCurveSurfaceOverlapImpl(c, s, opt, [](double, double) { return true; });
}

std::vector<CurveBrepOverlap> IntersectCurveBrepOverlap(const ON_Curve& c, const ON_Brep& b, const IntersectOptions& opt) {
  std::vector<CurveBrepOverlap> out;
  const ON_BoundingBox cb = c.BoundingBox();
  const double pad = std::max(opt.mesh_tolerance, opt.tolerance * 4);
  const int nf = b.m_F.Count();
  for (int j = 0; j < nf; ++j) {
    const ON_BrepFace& f = b.m_F[j];
    const ON_Surface* s = f.SurfaceOf();
    if (!s) continue;
    ON_BoundingBox fb = s->BoundingBox();
    fb.m_min -= ON_3dVector(pad, pad, pad);
    fb.m_max += ON_3dVector(pad, pad, pad);
    if (cb.IsValid() && fb.IsValid() && cb.IsDisjoint(fb)) continue;
    for (const CurveSurfaceOverlap& ov : IntersectCurveSurfaceOverlapImpl(c, *s, opt, [&f](double u, double v) { return FaceContainsUV(f, u, v); })) {
      out.push_back(CurveBrepOverlap{j, ov});
    }
  }
  return out;
}

std::vector<BrepContourSection> ContourBrep(const ON_Brep& b, const ON_Plane& base_plane, double spacing, const IntersectOptions& opt) {
  std::vector<BrepContourSection> out;
  if (!(spacing > 0) || !base_plane.IsValid()) return out;
  // Deliberately NOT b.BoundingBox(): a real bug found while building this
  // - this OpenNURBS version's ON_Brep::GetBBox() tightens each face's
  // cached bbox against its own trim loop's 2D parameter-space bbox
  // (InternalFaceBoundingBox(), opennurbs_brep.cpp) before unioning them,
  // and for this kernel's own untrimmed Box()-style faces that path can
  // come back degenerate (confirmed directly: a Brep::Box(0,0,0,3,3,3)'s
  // own b.BoundingBox() returns min=(0,0,0) max=(3,0,0) - y and z
  // collapsed to zero - while every individual face's own
  // SurfaceOf()->BoundingBox() is exactly correct). Unioning the per-face
  // SURFACE boxes directly, the same source IntersectBreps()/
  // IntersectBrepByPlane() already trust per face, sidesteps it entirely.
  ON_BoundingBox bbox;
  const int nf = b.m_F.Count();
  for (int i = 0; i < nf; ++i) {
    const ON_Surface* s = b.m_F[i].SurfaceOf();
    if (!s) continue;
    bbox.Union(s->BoundingBox());
  }
  if (!bbox.IsValid()) return out;

  // The signed-distance range (along base_plane's own normal) that the
  // B-rep's bounding box actually spans - every multiple of `spacing`
  // inside this range gets its own parallel section, covering the whole
  // object the way Rhino's own Contour command does (a base plane and a
  // spacing, not a station count the caller has to guess).
  double lo = std::numeric_limits<double>::max(), hi = -std::numeric_limits<double>::max();
  for (int k = 0; k < 8; ++k) {
    const ON_3dPoint corner((k & 1) ? bbox.m_max.x : bbox.m_min.x,
                             (k & 2) ? bbox.m_max.y : bbox.m_min.y,
                             (k & 4) ? bbox.m_max.z : bbox.m_min.z);
    const double dist = base_plane.DistanceTo(corner);
    lo = std::min(lo, dist);
    hi = std::max(hi, dist);
  }
  if (lo > hi) return out;

  const double start = std::ceil(lo / spacing - 1e-9) * spacing;
  constexpr int kMaxSections = 10000;  // guards against a caller-supplied spacing too small for the object's own extent
  int count = 0;
  for (double off = start; off <= hi + 1e-9 && count < kMaxSections; off += spacing, ++count) {
    ON_Plane plane = base_plane;
    plane.Translate(base_plane.Normal() * off);
    std::vector<BrepPlaneIntersection> hits = IntersectBrepByPlane(b, plane, opt);
    if (hits.empty()) continue;
    out.push_back(BrepContourSection{off, std::move(hits)});
  }
  return out;
}

std::vector<BrepMultiPlaneSection> SectionBrepByPlanes(const ON_Brep& b, const std::vector<ON_Plane>& planes, const IntersectOptions& opt) {
  std::vector<BrepMultiPlaneSection> out;
  for (size_t i = 0; i < planes.size(); ++i) {
    std::vector<BrepPlaneIntersection> hits = IntersectBrepByPlane(b, planes[i], opt);
    if (hits.empty()) continue;
    out.push_back(BrepMultiPlaneSection{static_cast<int>(i), std::move(hits)});
  }
  return out;
}

namespace {

// Every one of a face's own boundary loops, each as a closed 3D polyline
// (sampled from its trims' exact curves, evaluated through the face's own
// surface) - used below to tell a genuine face-vs-face crossing apart
// from two faces that merely TOUCH along a shared physical edge. Built
// from the trims' own 3D curves directly, not this kernel's ON_BrepEdge
// topology - several of this kernel's own face-construction paths
// (Box()/FromUntrimmedQuadFaces(), per their own doc comments) deliberately
// build adjacent faces with NO shared edge topology at all, even though
// they genuinely touch in 3D, so an edge-index adjacency test alone
// cannot be trusted to rule out ordinary touching.
std::vector<std::vector<Point3d>> FaceBoundaryLoops3D(const ON_Brep& b, const ON_BrepFace& f, int samples_per_trim = 48) {
  std::vector<std::vector<Point3d>> loops;
  const ON_Surface* s = f.SurfaceOf();
  if (f.m_li.Count() == 0) {
    // Untrimmed face (no loops at all) - the identical fallback
    // FaceContainsUV() already uses: its own boundary IS the surface's
    // natural (u, v) domain rectangle, not anything derived from trims.
    const ON_Interval du = f.Domain(0), dv = f.Domain(1);
    std::vector<Point3d> pts;
    const auto add = [&](double u, double v) { pts.push_back(s->PointAt(u, v)); };
    for (int i = 0; i < samples_per_trim; ++i) add(du.ParameterAt(static_cast<double>(i) / samples_per_trim), dv.Min());
    for (int i = 0; i < samples_per_trim; ++i) add(du.Max(), dv.ParameterAt(static_cast<double>(i) / samples_per_trim));
    for (int i = 0; i < samples_per_trim; ++i) add(du.ParameterAt(1.0 - static_cast<double>(i) / samples_per_trim), dv.Max());
    for (int i = 0; i < samples_per_trim; ++i) add(du.Min(), dv.ParameterAt(1.0 - static_cast<double>(i) / samples_per_trim));
    if (pts.size() >= 2) loops.push_back(std::move(pts));
    return loops;
  }
  for (int li = 0; li < f.m_li.Count(); ++li) {
    const ON_BrepLoop& loop = b.m_L[f.m_li[li]];
    std::vector<Point3d> pts;
    for (int k = 0; k < loop.m_ti.Count(); ++k) {
      const ON_BrepTrim& trim = b.m_T[loop.m_ti[k]];
      const ON_Interval d = trim.Domain();
      if (!d.IsIncreasing()) continue;
      for (int i = 0; i < samples_per_trim; ++i) {
        const ON_3dPoint uv = trim.PointAt(d.ParameterAt(static_cast<double>(i) / samples_per_trim));
        pts.push_back(s->PointAt(uv.x, uv.y));
      }
    }
    if (pts.size() >= 2) loops.push_back(std::move(pts));
  }
  return loops;
}

// Closest distance from `p` to any segment of any of `loops`' own closed polylines.
double DistanceToBoundaryLoops(const std::vector<std::vector<Point3d>>& loops, const Point3d& p) {
  double best = std::numeric_limits<double>::max();
  for (const std::vector<Point3d>& poly : loops) {
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
      const Point3d& a = poly[i];
      const Point3d& b2 = poly[(i + 1) % n];
      const Vector3d ab = b2 - a;
      const double len2 = ab.LengthSquared();
      const double t = Clamp(len2 > 1e-300 ? ON_DotProduct(p - a, ab) / len2 : 0.0, 0.0, 1.0);
      best = std::min(best, p.DistanceTo(a + ab * t));
    }
  }
  return best;
}

}  // namespace

std::vector<BrepBrepIntersection> FindBrepSelfIntersections(const ON_Brep& b, const IntersectOptions& opt) {
  std::vector<BrepBrepIntersection> out;
  const int nf = b.m_F.Count();
  std::vector<std::set<int>> face_edges(static_cast<size_t>(nf));
  std::vector<ON_BoundingBox> boxes(static_cast<size_t>(nf));
  std::vector<std::vector<std::vector<Point3d>>> boundary(static_cast<size_t>(nf));
  for (int i = 0; i < nf; ++i) {
    const ON_BrepFace& f = b.m_F[i];
    boxes[static_cast<size_t>(i)] = f.SurfaceOf()->BoundingBox();
    boundary[static_cast<size_t>(i)] = FaceBoundaryLoops3D(b, f);
    std::set<int>& edges = face_edges[static_cast<size_t>(i)];
    for (int li = 0; li < f.m_li.Count(); ++li) {
      const ON_BrepLoop& loop = b.m_L[f.m_li[li]];
      for (int k = 0; k < loop.m_ti.Count(); ++k) edges.insert(b.m_T[loop.m_ti[k]].m_ei);
    }
  }
  const double pad = std::max(opt.mesh_tolerance, opt.tolerance * 4);
  const double on_boundary_tol = std::max(opt.tolerance * 8, opt.mesh_tolerance);
  for (int i = 0; i < nf; ++i) {
    ON_BoundingBox exp_i = boxes[static_cast<size_t>(i)];
    exp_i.m_min -= ON_3dVector(pad, pad, pad);
    exp_i.m_max += ON_3dVector(pad, pad, pad);
    const ON_BrepFace& fi = b.m_F[i];
    for (int j = i + 1; j < nf; ++j) {
      bool shares_edge = false;
      for (int e : face_edges[static_cast<size_t>(i)]) {
        if (face_edges[static_cast<size_t>(j)].count(e)) { shares_edge = true; break; }
      }
      if (shares_edge) continue;  // ordinary adjacency - Brep::Check()'s own job, not this function's
      if (exp_i.IsDisjoint(boxes[static_cast<size_t>(j)])) continue;
      const ON_BrepFace& fj = b.m_F[j];
      for (IntersectionCurve& ic : IntersectFaces(&fi, *fi.SurfaceOf(), &fj, *fj.SurfaceOf(), opt)) {
        // Two faces that merely TOUCH along a shared physical edge (no
        // shared ON_BrepEdge, but genuinely coincident in 3D - the case
        // the shares_edge check above cannot see) produce an
        // IntersectionCurve that lies entirely on BOTH faces' own
        // boundary loops at once: that is the literal definition of
        // "this is the seam where the two faces border each other", not
        // one face's material cutting through the other's. A genuine
        // crossing's own curve leaves at least one face's boundary
        // (it runs through that face's interior) at some point.
        bool on_both_boundaries = !ic.points.empty();
        for (const Point3d& p : ic.points) {
          const bool on_i = DistanceToBoundaryLoops(boundary[static_cast<size_t>(i)], p) <= on_boundary_tol;
          const bool on_j = DistanceToBoundaryLoops(boundary[static_cast<size_t>(j)], p) <= on_boundary_tol;
          if (!on_i || !on_j) { on_both_boundaries = false; break; }
        }
        if (on_both_boundaries) continue;  // ordinary touching seam, not a self-intersection
        out.push_back(BrepBrepIntersection{i, j, std::move(ic)});
      }
    }
  }
  return out;
}

std::vector<FaceInteriorSelfIntersection> FindFaceInteriorSelfIntersections(const ON_Brep& b, const IntersectOptions& opt) {
  std::vector<FaceInteriorSelfIntersection> out;
  const int nf = b.m_F.Count();
  const int kMinCellGap = 2;  // the surface-grid analogue of IntersectCurveSelfIntersections()'s "at least 2 segments"
  for (int fi = 0; fi < nf; ++fi) {
    const ON_BrepFace& f = b.m_F[fi];
    const ON_Surface* srf = f.SurfaceOf();
    if (!srf) continue;
    const SurfaceMesh m = TessellateWithUV(*srf, opt);
    if (m.nu <= 0 || m.nv <= 0 || m.tris.empty() || !m.bbox.IsValid()) continue;
    const double eps = std::max(1e-9 * m.bbox.Diagonal().Length(), 1e-12);

    // Only triangles genuinely inside the face's own trim loops can
    // contribute to THIS face's self-intersection - the same FaceContainsUV()
    // trim test IntersectFaces() already applies to SSX results, checked
    // here against each triangle's own centroid (a sampling-resolution
    // approximation, honestly no finer than the tessellation itself, the
    // same caveat this file's other trim-aware functions already carry).
    std::vector<char> keep(m.tris.size(), 1);
    for (size_t t = 0; t < m.tris.size(); ++t) {
      const auto& tri = m.tris[t];
      const double cu = (m.uv[static_cast<size_t>(tri[0])].x + m.uv[static_cast<size_t>(tri[1])].x + m.uv[static_cast<size_t>(tri[2])].x) / 3.0;
      const double cv = (m.uv[static_cast<size_t>(tri[0])].y + m.uv[static_cast<size_t>(tri[1])].y + m.uv[static_cast<size_t>(tri[2])].y) / 3.0;
      keep[t] = FaceContainsUV(f, cu, cv) ? 1 : 0;
    }

    ON_BoundingBox region = m.bbox;
    const double pad = std::max(eps * 100, 1e-9 * region.Diagonal().Length());
    region.m_min -= ON_3dVector(pad, pad, pad);
    region.m_max += ON_3dVector(pad, pad, pad);
    Grid grid;
    grid.Build(m, region);
    std::vector<int> stamp(m.tris.size(), -1);
    int mark = 0;
    const auto cell_of = [&](int t) { const int c = t / 2; return std::pair<int, int>{c % m.nu, c / m.nu}; };

    std::vector<FaceInteriorSelfIntersection> face_hits;
    for (size_t ta = 0; ta < m.tris.size(); ++ta) {
      if (!keep[ta]) continue;
      const ON_BoundingBox box_a = Grid::TriBox(m, static_cast<int>(ta));
      ++mark;
      const std::pair<int, int> cell_a = cell_of(static_cast<int>(ta));
      grid.Query(box_a, stamp, mark, [&](int tbi) {
        if (tbi <= static_cast<int>(ta) || !keep[static_cast<size_t>(tbi)]) return;
        const std::pair<int, int> cell_b = cell_of(tbi);
        // Skip the ordinary local neighbourhood: a smoothly-varying patch's
        // own nearby cells always sit close together in 3D too (that is
        // continuity, not a self-crossing) - only triangle pairs genuinely
        // distant IN THE DOMAIN, yet still close enough in 3D for TriTri to
        // find a crossing, are candidates.
        if (std::max(std::abs(cell_a.first - cell_b.first), std::abs(cell_a.second - cell_b.second)) < kMinCellGap) return;
        Seg seg;
        if (!TriTri(m, static_cast<int>(ta), m, tbi, eps, seg)) return;
        for (const SegEnd& end : {seg.a, seg.b}) {
          double ua = end.uva.x, va = end.uva.y, ub = end.uvb.x, vb = end.uvb.y;
          // Same surface passed as both arguments - purely mechanical,
          // RefineSurfaceSurfacePoint() only ever calls .PointAt() on each
          // argument independently (see its own header comment above).
          if (!RefineSurfaceSurfacePoint(*srf, *srf, ua, va, ub, vb, opt.tolerance)) continue;
          // Discard convergence back onto the trivial ua==ub/va==vb
          // diagonal (every point trivially coincides with itself) -
          // Newton is free to walk away from its own seed, the same caveat
          // IntersectCurveSelfIntersections() discloses one dimension down.
          if (std::abs(ua - ub) < opt.tolerance * 50 && std::abs(va - vb) < opt.tolerance * 50) continue;
          if (!FaceContainsUV(f, ua, va) || !FaceContainsUV(f, ub, vb)) continue;
          const Point3d pa = srf->PointAt(ua, va), pb = srf->PointAt(ub, vb);
          const double gap = pa.DistanceTo(pb);
          if (gap > opt.tolerance * 4) continue;
          face_hits.push_back(FaceInteriorSelfIntersection{fi, pa, ON_2dPoint(ua, va), ON_2dPoint(ub, vb), gap});
        }
      });
    }
    for (const FaceInteriorSelfIntersection& h : face_hits) {
      bool dup = false;
      for (const FaceInteriorSelfIntersection& existing : out) {
        if (existing.face_index == fi && existing.point.DistanceTo(h.point) <= std::max(opt.tolerance * 4, 1e-9)) { dup = true; break; }
      }
      if (!dup) out.push_back(h);
    }
  }
  return out;
}

namespace {

// The 4-equation "both gradients vanish" stationary system for the squared
// gap between two surfaces - deliberately NOT RefineSurfaceSurfacePoint()'s
// 3-equation "Sa == Sb" system (that one is under-determined by one degree
// of freedom whenever the surfaces actually cross, so NewtonSolve's
// minimal-norm step lands it on SOME point of the shared crossing curve
// rather than distinguishing a tangent touch from an ordinary point on one).
// Converges to the closest-approach point from a seed near it, whether that
// closest approach is a genuine touch (gap == 0) or not (gap > 0, the caller
// checks).
// The surface normal at (u, v), nudging away from an exact coordinate
// singularity (a pole - an entire row/column of control points collapsed to
// one point, where d/du or d/dv vanishes even though the surface itself has
// a perfectly well-defined limiting tangent plane there) rather than
// reporting it as undefined. Tries the exact point first, then a handful of
// small steps in v and u (the same "nudge until well-conditioned" idea
// PullbackCurveToSurface()'s own re-seeding already uses elsewhere in this
// file, applied here to a normal instead of a closest-point search); a
// zero-length result means every one of those also degenerated (a
// genuinely malformed surface, not just a pole), which the caller treats as
// "cannot verify" rather than guessing a direction.
ON_3dVector RobustSurfaceNormal(const ON_Surface& s, double u, double v) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  const double step_u = std::max(du.Length(), 1e-9) * 1e-4;
  const double step_v = std::max(dv.Length(), 1e-9) * 1e-4;
  const std::pair<double, double> offsets[] = {{0, 0}, {0, step_v}, {0, -step_v}, {step_u, 0}, {-step_u, 0}};
  for (const auto& [du_off, dv_off] : offsets) {
    const double uu = Clamp(u + du_off, du.Min(), du.Max());
    const double vv = Clamp(v + dv_off, dv.Min(), dv.Max());
    ON_3dPoint p;
    ON_3dVector su, sv;
    s.Ev1Der(uu, vv, p, su, sv);
    const ON_3dVector n = ON_CrossProduct(su, sv);
    if (n.Length() > 1e-9) return n;
  }
  return ON_3dVector(0, 0, 0);
}

bool RefineClosestApproach(const ON_Surface& a, const ON_Surface& b, double& ua, double& va, double& ub, double& vb, double tol) {
  std::vector<double> x = {ua, va, ub, vb};
  const std::vector<double> lo = {a.Domain(0).Min(), a.Domain(1).Min(), b.Domain(0).Min(), b.Domain(1).Min()};
  const std::vector<double> hi = {a.Domain(0).Max(), a.Domain(1).Max(), b.Domain(0).Max(), b.Domain(1).Max()};
  const double scale = SurfaceScale(a) + SurfaceScale(b);
  Residual res = [&](const std::vector<double>& p) {
    ON_3dPoint PA, PB;
    ON_3dVector dau, dav, dbu, dbv;
    a.Ev1Der(p[0], p[1], PA, dau, dav);
    b.Ev1Der(p[2], p[3], PB, dbu, dbv);
    const Vector3d d = PA - PB;
    return std::vector<double>{ON_DotProduct(dau, d), ON_DotProduct(dav, d), ON_DotProduct(dbu, d), ON_DotProduct(dbv, d)};
  };
  const bool ok = NewtonSolve(res, x, lo, hi, 1e-10 * scale * scale, 60);
  ua = x[0]; va = x[1]; ub = x[2]; vb = x[3];
  (void)tol;
  return ok;
}

}  // namespace

std::vector<SurfaceTangentContact> FindSurfaceTangentContacts(const ON_Surface& a, const ON_Surface& b, const IntersectOptions& opt) {
  std::vector<SurfaceTangentContact> out;
  const ON_Interval dua = a.Domain(0), dva = a.Domain(1), dub = b.Domain(0), dvb = b.Domain(1);
  if (!dua.IsIncreasing() || !dva.IsIncreasing() || !dub.IsIncreasing() || !dvb.IsIncreasing()) return out;

  const ON_BoundingBox bba = a.BoundingBox(), bbb = b.BoundingBox();
  const double scale = std::max({bba.IsValid() ? bba.Diagonal().Length() : 1.0, bbb.IsValid() ? bbb.Diagonal().Length() : 1.0, 1e-6});
  // Quick reject: the two bounding boxes are not even close to touching -
  // skip the O(grid^2) search entirely.
  if (bba.IsValid() && bbb.IsValid()) {
    const ON_3dPoint amin = bba.Min(), amax = bba.Max(), bmin = bbb.Min(), bmax = bbb.Max();
    double gap2 = 0;
    for (int k = 0; k < 3; ++k) {
      const double lo_gap = amin[k] > bmax[k] ? amin[k] - bmax[k] : 0;
      const double hi_gap = bmin[k] > amax[k] ? bmin[k] - amax[k] : 0;
      const double g = std::max(lo_gap, hi_gap);
      gap2 += g * g;
    }
    if (std::sqrt(gap2) > std::max(opt.tolerance * 8, scale * 0.01)) return out;
  }

  const int G = 24;
  std::vector<Point3d> pa(static_cast<size_t>(G + 1) * (G + 1)), pb(static_cast<size_t>(G + 1) * (G + 1));
  std::vector<ON_2dPoint> uva(pa.size()), uvb(pb.size());
  for (int i = 0; i <= G; ++i)
    for (int j = 0; j <= G; ++j) {
      const size_t idx = static_cast<size_t>(i) * (G + 1) + j;
      const double u = dua.ParameterAt(static_cast<double>(i) / G), v = dva.ParameterAt(static_cast<double>(j) / G);
      pa[idx] = a.PointAt(u, v);
      uva[idx] = ON_2dPoint(u, v);
      const double u2 = dub.ParameterAt(static_cast<double>(i) / G), v2 = dvb.ParameterAt(static_cast<double>(j) / G);
      pb[idx] = b.PointAt(u2, v2);
      uvb[idx] = ON_2dPoint(u2, v2);
    }

  // For every grid point of `a`, find its nearest grid point of `b`; a pair
  // close relative to the two surfaces' own scale is a plausible seed for a
  // genuine touch nearby.
  struct Seed { double ua, va, ub, vb; };
  std::vector<Seed> seeds;
  for (size_t ia = 0; ia < pa.size(); ++ia) {
    double best = std::numeric_limits<double>::max();
    size_t bestj = 0;
    for (size_t ib = 0; ib < pb.size(); ++ib) {
      const double d = pa[ia].DistanceTo(pb[ib]);
      if (d < best) { best = d; bestj = ib; }
    }
    if (best < scale * 0.15) seeds.push_back({uva[ia].x, uva[ia].y, uvb[bestj].x, uvb[bestj].y});
  }
  if (seeds.empty()) return out;

  for (const Seed& s : seeds) {
    double ua = s.ua, va = s.va, ub = s.ub, vb = s.vb;
    if (!RefineClosestApproach(a, b, ua, va, ub, vb, opt.tolerance)) continue;
    const Point3d pa_pt = a.PointAt(ua, va), pb_pt = b.PointAt(ub, vb);
    const double gap = pa_pt.DistanceTo(pb_pt);
    if (gap > opt.tolerance) continue;  // stationary, but not an actual touch

    // Every point where the gap genuinely reaches zero is automatically a
    // stationary point (RefineClosestApproach's own 4-equation system is
    // satisfied along an ordinary transversal crossing curve too - a
    // nonnegative function that hits zero has a zero gradient there,
    // crossing or not). What distinguishes a TANGENT touch from an ordinary
    // crossing is the surfaces' own tangent planes: at a tangent touch they
    // coincide (the two normals are parallel or antiparallel); at a
    // transversal crossing they meet at a genuine nonzero angle. This is a
    // purely local, sampling-independent test - unlike checking the
    // candidate against IntersectSurfaces()'s own sampled crossing-curve
    // points (tried first; rejected because a crossing curve's own
    // mesh-driven sample spacing can leave a real crossing point farther
    // from its nearest recorded sample than this function's own match
    // tolerance, which both false-negatived - wrongly reporting a genuine
    // crossing as a tangent contact - and would false-positive whenever a
    // true tangent touch happens to be reached by IntersectSurfaces()'s own
    // thinning/fitting pass). RobustSurfaceNormal(), not a plain Ev1Der
    // cross product, so a contact that happens to land exactly on a
    // surface's own coordinate pole (e.g. the most natural possible test
    // of this whole function - a sphere resting on a plane directly below
    // its center touches at precisely that sphere's own south pole in the
    // default ON_Sphere parametrization) is still correctly classified,
    // not silently dropped for a parametrization artifact that has nothing
    // to do with the surface's real, perfectly smooth shape there.
    const ON_3dVector na_raw = RobustSurfaceNormal(a, ua, va), nb_raw = RobustSurfaceNormal(b, ub, vb);
    const double na_len = na_raw.Length(), nb_len = nb_raw.Length();
    if (na_len < 1e-12 || nb_len < 1e-12) continue;  // a genuinely degenerate surface at this point - cannot verify tangency here
    ON_3dVector na = na_raw, nb = nb_raw;
    na /= na_len;
    nb /= nb_len;
    if (ON_CrossProduct(na, nb).Length() > 1e-3) continue;  // the tangent planes meet at a real angle: a genuine crossing, not a tangent-only contact
    bool dup = false;
    for (const SurfaceTangentContact& e : out)
      if (e.point.DistanceTo(pa_pt) <= std::max(opt.tolerance * 4, 1e-9)) { dup = true; break; }
    if (dup) continue;
    out.push_back(SurfaceTangentContact{ON_2dPoint(ua, va), ON_2dPoint(ub, vb), pa_pt, gap});
  }
  return out;
}

std::vector<SurfaceOverlapRegion> IntersectSurfacesOverlap(const ON_Surface& a, const ON_Surface& b, const IntersectOptions& opt) {
  std::vector<SurfaceOverlapRegion> out;
  const ON_Interval dua = a.Domain(0), dva = a.Domain(1);
  if (!dua.IsIncreasing() || !dva.IsIncreasing()) return out;

  const ON_BoundingBox bba = a.BoundingBox();
  const double diag = bba.IsValid() ? bba.Diagonal().Length() : 1;
  const int n = static_cast<int>(Clamp(std::ceil(diag / std::max(opt.mesh_tolerance, 1e-6)), 12, 120));

  std::vector<char> on(static_cast<size_t>(n + 1) * (n + 1), 0);
  for (int i = 0; i <= n; ++i) {
    double u = 0, v = 0;
    for (int j = 0; j <= n; ++j) {
      const double uu = dua.ParameterAt(static_cast<double>(i) / n);
      const double vv = dva.ParameterAt(static_cast<double>(j) / n);
      const Point3d p = a.PointAt(uu, vv);
      bool ok;
      if (j == 0) {
        ok = SurfaceClosestPointGlobal(b, p, u, v);
      } else {
        ok = SurfaceClosestPoint(b, p, u, v);  // seeded from the PREVIOUS sample in this row, same discipline as IntersectCurveSurfaceOverlap()
        const double try_err = ok ? b.PointAt(u, v).DistanceTo(p) : std::numeric_limits<double>::max();
        if (!ok || try_err > opt.mesh_tolerance * 4) {
          double gu = u, gv = v;
          const bool gok = SurfaceClosestPointGlobal(b, p, gu, gv);
          const double gerr = b.PointAt(gu, gv).DistanceTo(p);
          if (gok && gerr < try_err) { u = gu; v = gv; }
        }
      }
      const double err = b.PointAt(u, v).DistanceTo(p);
      on[static_cast<size_t>(i) * (n + 1) + static_cast<size_t>(j)] = err <= opt.tolerance ? 1 : 0;
    }
  }

  int on_count = 0;
  for (char c : on) on_count += c;
  if (on_count == 0) return out;
  const bool whole_surface_coincides = (on_count == static_cast<int>(on.size()));

  // Closest-point error of a's own (u, v) sample against b, used below to
  // bisection-tighten each extent's representative transect - the same
  // global-closest-point test the main grid pass above uses, not seeded
  // from a neighbour, since a bisection midpoint is not guaranteed close to
  // any one grid sample's own warm seed.
  auto ErrOnB = [&](double uu, double vv) {
    double bu = 0, bv = 0;
    const bool ok = SurfaceClosestPointGlobal(b, a.PointAt(uu, vv), bu, bv);
    return ok ? b.PointAt(bu, bv).DistanceTo(a.PointAt(uu, vv)) : std::numeric_limits<double>::max();
  };
  // Bisects `a`'s own u (resp. v) between a known on-`b` grid index (i_on)
  // and a known off-`b` neighbour (i_off), holding the other coordinate
  // fixed at a representative grid line - 40 halvings of the already
  // grid-spacing-wide bracket, the same discipline
  // IntersectCurveSurfaceOverlap()'s own RefineBoundary() uses one
  // dimension down.
  auto BisectU = [&](int i_on, int i_off, int j_fixed) {
    double u_on = dua.ParameterAt(static_cast<double>(i_on) / n);
    double u_off = dua.ParameterAt(static_cast<double>(i_off) / n);
    const double vv = dva.ParameterAt(static_cast<double>(j_fixed) / n);
    for (int it = 0; it < 40; ++it) {
      const double um = 0.5 * (u_on + u_off);
      if (ErrOnB(um, vv) <= opt.tolerance) u_on = um; else u_off = um;
    }
    return u_on;
  };
  auto BisectV = [&](int j_on, int j_off, int i_fixed) {
    double v_on = dva.ParameterAt(static_cast<double>(j_on) / n);
    double v_off = dva.ParameterAt(static_cast<double>(j_off) / n);
    const double uu = dua.ParameterAt(static_cast<double>(i_fixed) / n);
    for (int it = 0; it < 40; ++it) {
      const double vm = 0.5 * (v_on + v_off);
      if (ErrOnB(uu, vm) <= opt.tolerance) v_on = vm; else v_off = vm;
    }
    return v_on;
  };

  // Maximal 4-connected runs of on-`b` grid cells, each reported as its own
  // axis-aligned (u, v) bounding box - not an exact boundary polygon (see
  // this function's own doc comment).
  std::vector<char> visited(on.size(), 0);
  std::vector<std::pair<int, int>> stack;
  for (int i0 = 0; i0 <= n; ++i0) {
    for (int j0 = 0; j0 <= n; ++j0) {
      const size_t idx0 = static_cast<size_t>(i0) * (n + 1) + static_cast<size_t>(j0);
      if (!on[idx0] || visited[idx0]) continue;
      stack.clear();
      stack.push_back({i0, j0});
      visited[idx0] = 1;
      int imin = i0, imax = i0, jmin = j0, jmax = j0;
      // The grid column/row at which each extent was (most recently) seen -
      // a 4-connected run guarantees the cell just past imin/imax/jmin/jmax
      // along that one axis, AT this same representative coordinate, is
      // genuinely off-`b` (see BisectU/BisectV's own call sites below for
      // why), which is exactly what a bisection bracket needs.
      int j_at_imin = j0, j_at_imax = j0, i_at_jmin = i0, i_at_jmax = i0;
      int cell_count = 0;
      while (!stack.empty()) {
        const auto [ci, cj] = stack.back();
        stack.pop_back();
        ++cell_count;
        if (ci < imin) { imin = ci; j_at_imin = cj; }
        if (ci > imax) { imax = ci; j_at_imax = cj; }
        if (cj < jmin) { jmin = cj; i_at_jmin = ci; }
        if (cj > jmax) { jmax = cj; i_at_jmax = ci; }
        static const int kDI[4] = {1, -1, 0, 0}, kDJ[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
          const int ni = ci + kDI[k], nj = cj + kDJ[k];
          if (ni < 0 || ni > n || nj < 0 || nj > n) continue;
          const size_t nidx = static_cast<size_t>(ni) * (n + 1) + static_cast<size_t>(nj);
          if (!on[nidx] || visited[nidx]) continue;
          visited[nidx] = 1;
          stack.push_back({ni, nj});
        }
      }
      if (cell_count < 2) continue;  // a single isolated on-surface cell is a transient touch, not an overlap (FindSurfaceTangentContacts()'s job instead)
      SurfaceOverlapRegion region;
      // Each extent is bisection-tightened along its own representative
      // transect when a genuine off-`b` neighbour exists to bisect toward
      // (4-connectivity guarantees that neighbour, at this same
      // representative coordinate, is off - if it were on, it would be
      // 4-connected to this very cell and imin/imax/jmin/jmax would already
      // have moved past it). An extent sitting at `a`'s own domain edge has
      // no such neighbour and stays exactly as the grid already has it (the
      // domain edge itself, not an approximation).
      region.u0 = imin > 0 ? BisectU(imin, imin - 1, j_at_imin) : dua.ParameterAt(0.0);
      region.u1 = imax < n ? BisectU(imax, imax + 1, j_at_imax) : dua.ParameterAt(1.0);
      region.v0 = jmin > 0 ? BisectV(jmin, jmin - 1, i_at_jmin) : dva.ParameterAt(0.0);
      region.v1 = jmax < n ? BisectV(jmax, jmax + 1, i_at_jmax) : dva.ParameterAt(1.0);
      region.entire_surface = whole_surface_coincides;
      out.push_back(region);
    }
  }
  return out;
}

PlaneSphereIntersection IntersectPlaneSphere(const ON_Plane& plane, const ON_Sphere& sphere, double tolerance) {
  PlaneSphereIntersection out;
  if (!plane.IsValid() || !sphere.IsValid() || !(sphere.Radius() > 0) || !(tolerance >= 0)) return out;  // stays empty
  const double r = sphere.Radius();
  const double d = plane.DistanceTo(sphere.Center());
  const double ad = std::fabs(d);
  if (ad > r + tolerance) return out;  // genuinely do not meet: stays empty

  const Point3d center_proj = sphere.Center() - Vector3d(plane.zaxis) * d;
  out.empty = false;
  if (ad >= r - tolerance) {
    // |d| within tolerance of r: a genuine positive radius would round to
    // ~0 here, so this is the tangent-point degeneracy, not a sliver circle.
    out.tangent = true;
    out.point = center_proj;
    return out;
  }
  const double circle_radius = std::sqrt(std::max(r * r - d * d, 0.0));
  out.circle = ON_Circle(ON_Plane(center_proj, plane.xaxis, plane.yaxis), circle_radius);
  if (out.circle.GetNurbForm(out.curve) == 0) {
    // ON_Circle::GetNurbForm only fails for a degenerate (non-positive-
    // radius) circle - already ruled out by the tangent branch above - or an
    // invalid input plane, already ruled out by the IsValid() guard. Treated
    // as a genuine miss rather than returning a circle with no usable curve.
    out = PlaneSphereIntersection{};
  }
  return out;
}

PlaneCylinderIntersection IntersectPlaneCylinder(const ON_Plane& plane, const ON_Cylinder& cylinder, double tolerance) {
  PlaneCylinderIntersection out;
  if (!plane.IsValid() || !cylinder.IsValid() || !(cylinder.circle.radius > 0) || !(tolerance >= 0)) return out;  // stays empty

  constexpr double kMinAbsC = 1e-6;  // same angular-degeneracy threshold ComputeEllipseFrame3d() (ellipse_clip3d.h) uses
  const double r = cylinder.circle.radius;
  const Vector3d axis = cylinder.Axis();
  const Point3d c0 = cylinder.Center();
  const Vector3d n = plane.zaxis;
  const double C = ON_DotProduct(axis, n);
  const double d = plane.DistanceTo(c0);

  if (std::fabs(C) < kMinAbsC) {
    // The axis lies (to within kMinAbsC) IN the plane, so its own signed
    // distance to the plane is the same `d` at every point along it: moving
    // by t*axis changes that distance by t*C, and C is the very thing that's
    // ~0 here. `n` therefore already lies entirely in the circular
    // cross-section perpendicular to the axis (dot(axis, n) == C ~ 0), so
    // {n, m = axis x n} is a valid orthonormal in-plane-of-the-circle basis
    // for the radial direction, with no need for an arbitrary third vector:
    // a cylinder-surface point at radial angle phi from n sits at signed
    // distance d + r*cos(phi) from the plane (see this function's own
    // header doc comment for the full derivation).
    out.parallel_to_axis = true;
    const double ad = std::fabs(d);
    if (ad > r + tolerance) return out;  // genuinely do not meet: stays empty
    out.empty = false;
    const Vector3d m = ON_CrossProduct(axis, n);  // already unit: |axis|=|n|=1, axis _|_ n here
    auto line_at = [&](double phi) {
      const Point3d p = c0 + r * (std::cos(phi) * n + std::sin(phi) * m);
      return ON_Line(p, p + axis);
    };
    if (ad >= r - tolerance) {
      // |d| within tolerance of r: a genuine two-line pair would be
      // separated by less than tolerance here, so this is the single
      // tangent-line degeneracy, not two near-coincident slivers.
      out.tangent = true;
      out.line_a = line_at(d > 0 ? kPi : 0.0);  // cos(phi) = -d/r -> +-1
      return out;
    }
    const double phi0 = std::acos(Clamp(-d / r, -1.0, 1.0));
    out.line_a = line_at(phi0);
    out.line_b = line_at(-phi0);
    return out;
  }

  // The general (non-edge-on) case: a true ellipse (a true circle when
  // axis _|_ plane, |C| == 1 - the same formula below handles both). See
  // this function's own header doc comment for the derivation summary.
  const Vector3d minor_raw = ON_CrossProduct(n, axis);  // _|_ n and _|_ axis
  const double minor_len = minor_raw.Length();
  const Vector3d e0 = minor_len > 1e-9 ? minor_raw / minor_len : Vector3d(plane.xaxis);  // axis || n (|C|==1): minor direction is arbitrary in-plane, plane.xaxis already _|_ n and so _|_ axis too here
  const Vector3d e1 = ON_CrossProduct(axis, e0);  // unit, _|_ axis and _|_ e0
  const double B = ON_DotProduct(e1, n);
  const Vector3d e1p = e1 - (B / C) * axis;  // _|_ e0, lies IN the plane (dot(e1p, n) == B - (B/C)*C == 0)
  const double major_scale = std::sqrt(1.0 + (B / C) * (B / C));
  const Vector3d major_dir = e1p / major_scale;  // unit, _|_ e0

  out.empty = false;
  const Point3d center = c0 - (d / C) * axis;
  out.ellipse = ON_Ellipse(ON_Plane(center, e0, major_dir), r, r * major_scale);
  if (out.ellipse.GetNurbForm(out.curve) == 0) {
    // Only fails for a degenerate (non-positive) radius - already ruled out
    // above (r > 0, major_scale >= 1) - or an invalid plane, already ruled
    // out by the e0/major_dir construction. Treated as a genuine miss
    // rather than returning an ellipse with no usable curve.
    out = PlaneCylinderIntersection{};
  }
  return out;
}

CylinderCylinderParallelIntersection IntersectCylinderCylinderParallel(const ON_Cylinder& a, const ON_Cylinder& b, double tolerance) {
  CylinderCylinderParallelIntersection out;
  if (!a.IsValid() || !b.IsValid() || !(a.circle.radius > 0) || !(b.circle.radius > 0) || !(tolerance >= 0)) return out;  // stays empty

  constexpr double kAxisParallelTol = 1e-6;  // |cross(unit, unit)| = sin(angle between them); same scale as ComputeEllipseFrame3d's own min_abs_C guard
  const Vector3d axis = a.Axis();
  if (ON_CrossProduct(axis, b.Axis()).Length() > kAxisParallelTol) {
    out.not_parallel = true;
    return out;
  }

  // Project both axes into a plane _|_ `axis` (valid at any height along
  // either infinite axis line identically - see this function's own header
  // doc comment) using a robust arbitrary in-plane basis: cross `axis` with
  // whichever world axis it is LEAST aligned with, avoiding the near-zero
  // cross product a poorly-chosen helper could produce.
  const Vector3d helper = std::fabs(axis.x) < 0.9 ? Vector3d(1, 0, 0) : Vector3d(0, 1, 0);
  const Vector3d e0 = ON_CrossProduct(axis, helper).UnitVector();
  const Vector3d e1 = ON_CrossProduct(axis, e0);  // already unit: axis _|_ e0, both unit

  const Point3d c0 = a.Center();
  const Vector3d d3 = b.Center() - c0;
  const double dx = ON_DotProduct(d3, e0), dy = ON_DotProduct(d3, e1);
  const double dist = std::hypot(dx, dy);
  const double ra = a.circle.radius, rb = b.circle.radius;

  if (dist <= tolerance) return out;  // (anti)parallel AND concentric axes - no well-defined 2D lens (see header doc comment); stays empty
  if (dist > ra + rb + tolerance) return out;        // disjoint: genuinely too far apart
  if (dist < std::fabs(ra - rb) - tolerance) return out;  // one nested entirely inside the other: no touch at any angle

  out.empty = false;
  const double p = (dist * dist + ra * ra - rb * rb) / (2.0 * dist);  // `a` in the Bourke derivation; renamed to avoid shadowing the ON_Cylinder parameter `a`
  const double h = std::sqrt(std::max(ra * ra - p * p, 0.0));
  const Point3d mid = c0 + (p * dx / dist) * e0 + (p * dy / dist) * e1;
  auto line_through = [&](double sign) {
    const Point3d pt = mid + (sign * h * (-dy / dist)) * e0 + (sign * h * (dx / dist)) * e1;
    return ON_Line(pt, pt + axis);
  };
  if (h <= tolerance) {
    out.tangent = true;
    out.line_a = line_through(0.0);
    return out;
  }
  out.line_a = line_through(1.0);
  out.line_b = line_through(-1.0);
  return out;
}

PlanePlaneIntersection IntersectPlanePlane(const ON_Plane& a, const ON_Plane& b, double tolerance) {
  PlanePlaneIntersection out;
  if (!a.IsValid() || !b.IsValid() || !(tolerance >= 0)) return out;  // stays empty

  constexpr double kAxisParallelTol = 1e-9;  // same scale as IntersectCylinderCylinderParallel's own axis-parallel guard
  const Vector3d na = a.zaxis, nb = b.zaxis;
  if (ON_CrossProduct(na, nb).Length() <= kAxisParallelTol) {
    // Parallel (or antiparallel) normals: no single line can exist. Decide
    // coincident-vs-disjoint directly from `b`'s own distance to `a`'s plane
    // (constant everywhere on `b`, since `b` is flat and parallel to `a`).
    if (std::fabs(a.DistanceTo(b.origin)) <= tolerance) {
      out.empty = false;
      out.coincident = true;
    }
    return out;
  }

  const double c = ON_DotProduct(na, nb);
  const double ha = ON_DotProduct(na, Vector3d(a.origin));
  const double hb = ON_DotProduct(nb, Vector3d(b.origin));
  const double denom = 1.0 - c * c;
  const double alpha = (ha - c * hb) / denom;
  const double beta = (hb - c * ha) / denom;
  const Point3d p0 = alpha * na + beta * nb;
  const Vector3d dir = ON_CrossProduct(na, nb).UnitVector();

  out.empty = false;
  out.line = ON_Line(p0, p0 + dir);
  return out;
}

PlaneConeIntersection IntersectPlaneCone(const ON_Plane& plane, const ON_Cone& cone, double tolerance) {
  PlaneConeIntersection out;
  if (!plane.IsValid() || !cone.IsValid() || !(tolerance >= 0)) return out;  // stays empty

  constexpr double kDiscriminantEps = 1e-9;  // dimensionless (A/B/C are bounded ~O(1) direction-cosine combinations) - same scale as this file's other angular-degeneracy guards
  const Point3d apex = cone.ApexPoint();
  const Vector3d axis = cone.Axis();
  const double alpha = std::fabs(cone.AngleInRadians());
  if (!(alpha > 1e-9) || alpha > kPi / 2.0 - 1e-9) return out;  // not a genuine double-napped cone (degenerate to a line or a flat plane)

  const Vector3d e1 = plane.xaxis, e2 = plane.yaxis;
  const double a1 = ON_DotProduct(e1, axis), a2 = ON_DotProduct(e2, axis);
  const double k = std::cos(alpha) * std::cos(alpha);
  const double A = a1 * a1 - k, B = 2.0 * a1 * a2, C = a2 * a2 - k;
  const double Delta = B * B - 4.0 * A * C;

  if (std::fabs(plane.DistanceTo(apex)) <= tolerance) {
    // Through the apex: D, E, F of the restricted conic all vanish in
    // apex-centered local coordinates, leaving the homogeneous direction
    // equation A*cos(theta)^2 + B*cos(theta)*sin(theta) + C*sin(theta)^2 ==
    // 0 - see this function's own header doc comment for the double-angle
    // closed form. Real solutions exist exactly when Delta >= 0 (the same
    // discriminant as the restricted conic itself - the apex is always the
    // boundary point where an ellipse-type section degenerates to a point,
    // a parabola-type to one line, and a hyperbola-type to a line pair).
    out.through_apex = true;
    out.empty = false;
    if (Delta < -kDiscriminantEps) {
      out.line_count = 0;  // the plane touches the (infinite) cone at the apex point only
      return out;
    }
    const double Rx = (A - C) / 2.0, Ry = B / 2.0;
    const double R = std::hypot(Rx, Ry);
    const double phi = std::atan2(Ry, Rx);
    const double rhs = -(A + C) / 2.0;
    const double cos_arg = R > 1e-300 ? Clamp(rhs / R, -1.0, 1.0) : 0.0;
    const double dtheta = std::acos(cos_arg);  // in [0, pi]
    auto line_at = [&](double theta) {
      const Vector3d dir = std::cos(theta) * e1 + std::sin(theta) * e2;
      return ON_Line(apex, apex + dir);
    };
    if (Delta <= kDiscriminantEps) {
      out.line_count = 1;
      out.line_a = line_at(phi / 2.0);
      return out;
    }
    out.line_count = 2;
    out.line_a = line_at((phi - dtheta) / 2.0);
    out.line_b = line_at((phi + dtheta) / 2.0);
    return out;
  }

  if (Delta >= -kDiscriminantEps) {
    out.unsupported = true;  // parabola (Delta ~ 0) or hyperbola (Delta > 0) - not built here
    return out;
  }

  // Ellipse-type (Delta < 0, plane clear of the apex): the restricted
  // conic's center solves the critical-point system 2A*s0+B*t0+D==0,
  // B*s0+2C*t0+E==0 - see this function's own header doc comment.
  const Vector3d v0 = plane.origin - apex;
  const double a0 = ON_DotProduct(v0, axis);
  const double b0 = ON_DotProduct(v0, v0), b1 = ON_DotProduct(v0, e1), b2 = ON_DotProduct(v0, e2);
  const double D = 2.0 * (a0 * a1 - k * b1), E = 2.0 * (a0 * a2 - k * b2), F = a0 * a0 - k * b0;

  const double det = 4.0 * A * C - B * B;  // == -Delta > 0 here
  const double s0 = (-D * 2.0 * C - B * (-E)) / det;
  const double t0 = (2.0 * A * (-E) - B * (-D)) / det;
  const double Fp = A * s0 * s0 + B * s0 * t0 + C * t0 * t0 + D * s0 + E * t0 + F;  // Q evaluated at the center

  // 2x2 symmetric eigen-decomposition of [[A, B/2], [B/2, C]] - the
  // classical conic-rotation angle (cot(2*theta) == (A-C)/B).
  const double theta = 0.5 * std::atan2(B, A - C);
  const double c1 = std::cos(theta), s1 = std::sin(theta);
  const double lam1 = A * c1 * c1 + B * c1 * s1 + C * s1 * s1;
  const double lam2 = A * s1 * s1 - B * c1 * s1 + C * c1 * c1;  // orthogonal direction (theta + pi/2)
  const double sq1 = -Fp / lam1, sq2 = -Fp / lam2;
  if (!(sq1 > 1e-18) || !(sq2 > 1e-18)) {
    // Degenerates below usable precision (would need a genuinely malformed
    // cone/plane pair given Delta < 0 and the apex already ruled clear by
    // the branch above) - treated as a genuine miss rather than NaN.
    return out;
  }

  const Vector3d dir1 = c1 * e1 + s1 * e2;    // unit (e1, e2 orthonormal)
  const Vector3d dir2 = -s1 * e1 + c1 * e2;   // unit, _|_ dir1
  const Point3d center = plane.origin + s0 * e1 + t0 * e2;

  out.empty = false;
  out.ellipse = ON_Ellipse(ON_Plane(center, dir1, dir2), std::sqrt(sq1), std::sqrt(sq2));
  if (out.ellipse.GetNurbForm(out.curve) == 0) {
    out = PlaneConeIntersection{};  // only fails for a degenerate radius, already ruled out above, or an invalid plane, already ruled out by dir1/dir2's construction
  }
  return out;
}

PlaneTorusIntersection IntersectPlaneTorus(const ON_Plane& plane, const ON_Torus& torus, double tolerance) {
  PlaneTorusIntersection out;
  if (!plane.IsValid() || !torus.IsValid() || !(tolerance >= 0)) return out;  // stays empty

  constexpr double kAngleTol = 1e-6;  // same scale as IntersectPlaneCylinder's own kMinAbsC edge-on guard
  const Point3d center = torus.plane.origin;
  const Vector3d taxis = torus.plane.zaxis;
  const Vector3d n = plane.zaxis;
  const double R = torus.major_radius, r = torus.minor_radius;

  const double axis_in_plane = ON_DotProduct(taxis, n);  // ~0 when the torus axis lies IN the plane (meridian candidate)
  if (std::fabs(axis_in_plane) < kAngleTol && std::fabs(plane.DistanceTo(center)) <= tolerance) {
    // MERIDIAN: the torus axis lies in the plane AND the plane passes
    // through the torus center (an axis LINE lies in a plane only if the
    // plane contains one of the axis's own points too - checking the
    // center directly is simpler and equally exact, since the axis is a
    // single line through that center). The two circles sit on either
    // side of the axis, both at the plane's own perpendicular direction
    // to the axis within the plane: `radial = n x taxis` is _|_ both n
    // (so it lies IN the plane) and taxis (so it is a genuine in-plane
    // radial direction), and `taxis` itself is the other in-plane axis
    // (dot(taxis, n) ~ 0 here, so taxis already lies in the plane too).
    const Vector3d radial = ON_CrossProduct(n, taxis);
    const double radial_len = radial.Length();
    if (!(radial_len > 1e-9) || !(r > 0)) return out;  // degenerate basis or invalid torus
    const Vector3d radial_dir = radial / radial_len;
    out.empty = false;
    out.meridian = true;
    out.circle_count = 2;
    out.circle_a = ON_Circle(ON_Plane(center + R * radial_dir, taxis, ON_CrossProduct(radial_dir, taxis)), r);
    out.circle_b = ON_Circle(ON_Plane(center - R * radial_dir, taxis, ON_CrossProduct(radial_dir, taxis)), r);
    if (out.circle_a.GetNurbForm(out.curve_a) == 0 || out.circle_b.GetNurbForm(out.curve_b) == 0) out = PlaneTorusIntersection{};
    return out;
  }

  if (std::fabs(std::fabs(axis_in_plane) - 1.0) < kAngleTol) {
    // AXIAL: the plane's own normal is (anti)parallel to the torus axis -
    // a horizontal slice at signed height z along that axis.
    const double z = ON_DotProduct(plane.origin - center, taxis);
    const double az = std::fabs(z);
    if (az > r + tolerance) return out;  // genuinely clear of the torus: stays empty
    out.empty = false;
    out.axial = true;
    const Point3d slice_center = center + z * taxis;
    const ON_Plane circle_plane(slice_center, torus.plane.xaxis, torus.plane.yaxis);
    const double inner_sq = r * r - z * z;  // >= 0 here (az <= r + tolerance, clamped below)
    const double inner = std::sqrt(std::max(inner_sq, 0.0));
    const double outer_radius = R + inner;
    const double inner_radius = R - inner;
    if (az >= r - tolerance || !(inner_radius > tolerance)) {
      // Tangent at the very top/bottom, or the central hole doesn't reach
      // this far in (inner_radius would be non-positive): one circle only.
      out.circle_count = 1;
      out.circle_a = ON_Circle(circle_plane, outer_radius);
      if (out.circle_a.GetNurbForm(out.curve_a) == 0) out = PlaneTorusIntersection{};
      return out;
    }
    out.circle_count = 2;
    out.circle_a = ON_Circle(circle_plane, outer_radius);
    out.circle_b = ON_Circle(circle_plane, inner_radius);
    if (out.circle_a.GetNurbForm(out.curve_a) == 0 || out.circle_b.GetNurbForm(out.curve_b) == 0) out = PlaneTorusIntersection{};
    return out;
  }

  out.unsupported = true;  // a general oblique plane/torus section is a quartic space curve - not built here
  return out;
}

namespace {

// Shared by FindSurfaceSilhouettePoints()/FindSurfaceSilhouettePointsPerspective()/
// FindSurfaceSilhouetteCurves(): finds, for every edge of the surface's own
// regular TessellateWithUV() grid, the point (if any) where `direction_at`
// (evaluated at the 3D point under test - a fixed vector for the
// orthographic case, `point - eye` for the perspective one) crosses from
// one side of the surface's own tangent plane to the other -
// dot(RobustSurfaceNormal(s, u, v), direction_at(point)) changing sign
// along that edge - bisected to the true crossing the same way every other
// sampling-based detector in this file already does. Keyed by the edge's
// own two grid-vertex ids (lo, hi), not returned as a flat list, so
// FindSurfaceSilhouetteCurves() can tell which crossings share a grid cell
// and chain them; the two point-returning functions just flatten the map's
// own values.
struct GridSilhouetteCrossing {
  ON_2dPoint uv;
  Point3d point;
};
using SilhouetteEdgeKey = std::pair<int, int>;

std::map<SilhouetteEdgeKey, GridSilhouetteCrossing> FindTangencyCrossingsOnGrid(
    const ON_Surface& s, const SurfaceMesh& grid, const std::function<Vector3d(const Point3d&)>& direction_at) {
  std::map<SilhouetteEdgeKey, GridSilhouetteCrossing> out;
  const int nu = grid.nu, nv = grid.nv;
  auto id = [&](int i, int j) { return j * (nu + 1) + i; };

  auto normal_dot = [&](double u, double v, bool& ok) {
    const Vector3d n = RobustSurfaceNormal(s, u, v);
    const Vector3d dir = direction_at(s.PointAt(u, v));
    ok = n.Length() > 1e-9 && dir.Length() > 1e-12;
    return ON_DotProduct(n, dir);
  };

  auto try_edge = [&](int id0, int id1, const ON_2dPoint& uv0, const ON_2dPoint& uv1) {
    bool ok0 = false, ok1 = false;
    const double f0 = normal_dot(uv0.x, uv0.y, ok0);
    const double f1 = normal_dot(uv1.x, uv1.y, ok1);
    if (!ok0 || !ok1 || (f0 > 0) == (f1 > 0)) return;  // no verifiable sign change on this edge
    const bool sign0_positive = f0 > 0;
    double t0 = 0.0, t1 = 1.0;
    for (int iter = 0; iter < 40; ++iter) {
      const double tm = 0.5 * (t0 + t1);
      const double um = uv0.x + tm * (uv1.x - uv0.x), vm = uv0.y + tm * (uv1.y - uv0.y);
      bool okm = false;
      const double fm = normal_dot(um, vm, okm);
      if (!okm) break;  // degenerate mid-point: stop refining, keep the best bracket found so far
      if ((fm > 0) == sign0_positive) t0 = tm; else t1 = tm;
    }
    const double tm = 0.5 * (t0 + t1);
    const double u = uv0.x + tm * (uv1.x - uv0.x), v = uv0.y + tm * (uv1.y - uv0.y);
    GridSilhouetteCrossing c;
    c.uv = ON_2dPoint(u, v);
    c.point = s.PointAt(u, v);
    out[{std::min(id0, id1), std::max(id0, id1)}] = c;
  };

  for (int j = 0; j <= nv; ++j)
    for (int i = 0; i < nu; ++i) try_edge(id(i, j), id(i + 1, j), grid.uv[static_cast<size_t>(id(i, j))], grid.uv[static_cast<size_t>(id(i + 1, j))]);
  for (int j = 0; j < nv; ++j)
    for (int i = 0; i <= nu; ++i) try_edge(id(i, j), id(i, j + 1), grid.uv[static_cast<size_t>(id(i, j))], grid.uv[static_cast<size_t>(id(i, j + 1))]);

  return out;
}

std::vector<SurfaceSilhouettePoint> FlattenSilhouetteCrossings(const std::map<SilhouetteEdgeKey, GridSilhouetteCrossing>& crossings, double dedup_radius) {
  std::vector<SurfaceSilhouettePoint> out;
  for (const auto& [key, c] : crossings) {
    bool dup = false;
    for (const auto& prev : out) {
      if (prev.point.DistanceTo(c.point) < dedup_radius) { dup = true; break; }  // same dedup radius IntersectCurveSurface/IntersectCurves use
    }
    if (dup) continue;
    SurfaceSilhouettePoint pt;
    pt.uv = c.uv;
    pt.point = c.point;
    out.push_back(pt);
  }
  return out;
}

}  // namespace

std::vector<SurfaceSilhouettePoint> FindSurfaceSilhouettePoints(const ON_Surface& s, const Vector3d& view_direction, const IntersectOptions& opt) {
  if (view_direction.Length() < 1e-12) return {};  // no direction to test tangency against
  const SurfaceMesh grid = TessellateWithUV(s, opt);
  const auto crossings = FindTangencyCrossingsOnGrid(s, grid, [&](const Point3d&) { return view_direction; });
  return FlattenSilhouetteCrossings(crossings, opt.tolerance * 4);
}

std::vector<SurfaceSilhouettePoint> FindSurfaceSilhouettePointsPerspective(const ON_Surface& s, const Point3d& eye, const IntersectOptions& opt) {
  const SurfaceMesh grid = TessellateWithUV(s, opt);
  const auto crossings = FindTangencyCrossingsOnGrid(s, grid, [&](const Point3d& p) { return Vector3d(p - eye); });
  return FlattenSilhouetteCrossings(crossings, opt.tolerance * 4);
}

std::vector<SurfaceSilhouetteCurve> FindSurfaceSilhouetteCurves(const ON_Surface& s, const Vector3d& view_direction, const IntersectOptions& opt) {
  std::vector<SurfaceSilhouetteCurve> out;
  if (view_direction.Length() < 1e-12) return out;
  const SurfaceMesh grid = TessellateWithUV(s, opt);
  const auto crossings = FindTangencyCrossingsOnGrid(s, grid, [&](const Point3d&) { return view_direction; });
  if (crossings.empty()) return out;

  const int nu = grid.nu, nv = grid.nv;
  auto id = [&](int i, int j) { return j * (nu + 1) + i; };
  auto edge_key = [&](int a, int b) { return SilhouetteEdgeKey{std::min(a, b), std::max(a, b)}; };
  auto has_crossing = [&](const SilhouetteEdgeKey& k) { return crossings.find(k) != crossings.end(); };

  // One link per grid cell whose boundary has exactly two crossing edges -
  // the clean marching-squares case; 0 crossings means no link, and 4 (the
  // "saddle" ambiguity) is honestly skipped rather than guessed at - see
  // this function's own header doc comment.
  std::vector<std::pair<SilhouetteEdgeKey, SilhouetteEdgeKey>> links;
  for (int j = 0; j < nv; ++j) {
    for (int i = 0; i < nu; ++i) {
      const SilhouetteEdgeKey edges[4] = {edge_key(id(i, j), id(i + 1, j)), edge_key(id(i, j + 1), id(i + 1, j + 1)),
                                           edge_key(id(i, j), id(i, j + 1)), edge_key(id(i + 1, j), id(i + 1, j + 1))};
      SilhouetteEdgeKey hits[4];
      int n_hits = 0;
      for (const auto& e : edges) if (has_crossing(e)) hits[n_hits++] = e;
      if (n_hits == 2) links.push_back({hits[0], hits[1]});
    }
  }

  std::map<SilhouetteEdgeKey, std::vector<size_t>> touching;
  for (size_t li = 0; li < links.size(); ++li) {
    touching[links[li].first].push_back(li);
    touching[links[li].second].push_back(li);
  }
  std::vector<bool> used(links.size(), false);
  auto other_end = [&](size_t li, const SilhouetteEdgeKey& from) { return links[li].first == from ? links[li].second : links[li].first; };
  auto unused_link_at = [&](const SilhouetteEdgeKey& node) -> long {
    for (size_t li : touching[node]) if (!used[li]) return static_cast<long>(li);
    return -1;
  };

  auto emit = [&](const std::vector<SilhouetteEdgeKey>& chain_keys, bool closed) {
    if (chain_keys.size() < 2) return;
    std::vector<Point3d> pts;
    pts.reserve(chain_keys.size());
    for (const auto& k : chain_keys) pts.push_back(crossings.at(k).point);
    SurfaceSilhouetteCurve sc;
    sc.closed = closed;
    sc.curve = InterpolateCubic(pts, ChordParams(pts, closed), closed, 3);
    if (sc.curve.IsValid()) out.push_back(sc);
  };

  // Open chains: walk every still-unused degree-1 node to its matching
  // endpoint.
  for (const auto& [node, lis] : touching) {
    if (lis.size() != 1 || used[lis[0]]) continue;
    std::vector<SilhouetteEdgeKey> chain = {node};
    SilhouetteEdgeKey cur = node;
    for (size_t guard = 0; guard < links.size() + 1; ++guard) {
      const long li = unused_link_at(cur);
      if (li < 0) break;
      used[static_cast<size_t>(li)] = true;
      cur = other_end(static_cast<size_t>(li), cur);
      chain.push_back(cur);
    }
    emit(chain, false);
  }

  // Whatever links remain now form closed loops (every remaining touched
  // node has degree 2) - walk each back to its own start.
  for (size_t start_li = 0; start_li < links.size(); ++start_li) {
    if (used[start_li]) continue;
    const SilhouetteEdgeKey start = links[start_li].first;
    used[start_li] = true;
    SilhouetteEdgeKey cur = other_end(start_li, start);
    std::vector<SilhouetteEdgeKey> chain = {start, cur};
    for (size_t guard = 0; guard < links.size() + 1 && cur != start; ++guard) {
      const long li = unused_link_at(cur);
      if (li < 0) break;
      used[static_cast<size_t>(li)] = true;
      cur = other_end(static_cast<size_t>(li), cur);
      if (cur != start) chain.push_back(cur);
    }
    emit(chain, true);
  }

  return out;
}

}  // namespace dino8::kernel
