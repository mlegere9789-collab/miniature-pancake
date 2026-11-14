#include "dino8/kernel/mesh_unwrap.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace dino8::kernel {

namespace {

struct Tri {
  int v[3];
};

[[noreturn]] void Refuse(const std::string& why) {
  throw std::invalid_argument("dino8::kernel::ComputeLscmUVs: " + why);
}

int Find(std::vector<int>& parent, int x) {
  while (parent[static_cast<size_t>(x)] != x) {
    parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
    x = parent[static_cast<size_t>(x)];
  }
  return x;
}

// Compressed sparse rows of the 2T x 2F least-squares matrix (T triangles, F free vertices; unknowns are (u_0, v_0, u_1, ...)).
struct Sparse {
  std::vector<int> row_start{0};
  std::vector<int> col;
  std::vector<double> val;
  int rows() const { return static_cast<int>(row_start.size()) - 1; }
  void EndRow() { row_start.push_back(static_cast<int>(col.size())); }
  void Add(int c, double v) {
    col.push_back(c);
    val.push_back(v);
  }
  void Mul(const std::vector<double>& x, std::vector<double>& y) const {  // y = A x
    y.assign(static_cast<size_t>(rows()), 0.0);
    for (int r = 0; r < rows(); ++r) {
      double s = 0.0;
      for (int k = row_start[static_cast<size_t>(r)]; k < row_start[static_cast<size_t>(r) + 1]; ++k) s += val[static_cast<size_t>(k)] * x[static_cast<size_t>(col[static_cast<size_t>(k)])];
      y[static_cast<size_t>(r)] = s;
    }
  }
  void MulT(const std::vector<double>& r, std::vector<double>& x) const {  // x = A^T r
    std::fill(x.begin(), x.end(), 0.0);
    for (int i = 0; i < rows(); ++i) {
      const double ri = r[static_cast<size_t>(i)];
      for (int k = row_start[static_cast<size_t>(i)]; k < row_start[static_cast<size_t>(i) + 1]; ++k) x[static_cast<size_t>(col[static_cast<size_t>(k)])] += val[static_cast<size_t>(k)] * ri;
    }
  }
};

double Dot(const std::vector<double>& a, const std::vector<double>& b) {
  double s = 0.0;
  for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
  return s;
}

}  // namespace

std::vector<Point2d> ComputeLscmUVs(const Mesh& mesh, LscmReport* report) {
  const ON_Mesh& m = mesh.raw();
  const int nv = m.m_V.Count();
  if (m.m_F.Count() == 0 || nv < 3) Refuse("the mesh has no faces");

  std::vector<ON_3dPoint> P(static_cast<size_t>(nv));
  for (int i = 0; i < nv; ++i) P[static_cast<size_t>(i)] = ON_3dPoint(m.m_V[i]);

  std::vector<Tri> tris;
  tris.reserve(static_cast<size_t>(m.m_F.Count()) * 2);
  std::vector<char> used(static_cast<size_t>(nv), 0);
  for (int i = 0; i < m.m_F.Count(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    const int corners = f.IsQuad() ? 4 : 3;
    for (int c = 0; c < corners; ++c) {
      if (f.vi[c] < 0 || f.vi[c] >= nv) Refuse("a face refers to a vertex index outside the mesh");
      used[static_cast<size_t>(f.vi[c])] = 1;
    }
    tris.push_back({{f.vi[0], f.vi[1], f.vi[2]}});
    if (corners == 4) tris.push_back({{f.vi[0], f.vi[2], f.vi[3]}});
  }
  for (int i = 0; i < nv; ++i)
    if (!used[static_cast<size_t>(i)]) Refuse("vertex " + std::to_string(i) + " belongs to no face");

  // Edge use: a directed edge may be used once; an undirected edge by at most two faces, in opposite directions.
  std::map<std::pair<int, int>, int> directed;
  std::map<std::pair<int, int>, int> undirected;
  std::vector<int> parent(static_cast<size_t>(nv));
  std::iota(parent.begin(), parent.end(), 0);
  for (const Tri& t : tris) {
    for (int k = 0; k < 3; ++k) {
      const int a = t.v[k], b = t.v[(k + 1) % 3];
      if (a == b) Refuse("a face uses the same vertex twice");
      if (++directed[{a, b}] > 1) Refuse("two faces traverse an edge in the same direction (inconsistent orientation, or a duplicated face)");
      if (++undirected[{std::min(a, b), std::max(a, b)}] > 2) Refuse("an edge is used by three or more faces (non-manifold)");
      parent[static_cast<size_t>(Find(parent, a))] = Find(parent, b);
    }
  }
  const int root = Find(parent, 0);
  for (int i = 1; i < nv; ++i)
    if (Find(parent, i) != root) Refuse("the mesh has more than one connected component");

  std::vector<char> on_boundary(static_cast<size_t>(nv), 0);
  int boundary_edges = 0;
  for (const auto& e : undirected) {
    if (e.second == 1) {
      ++boundary_edges;
      on_boundary[static_cast<size_t>(e.first.first)] = 1;
      on_boundary[static_cast<size_t>(e.first.second)] = 1;
    }
  }
  if (boundary_edges == 0) Refuse("the mesh is closed: it has no boundary to unfold from (cut it open first)");

  // The pinned pair: the two boundary vertices farthest apart (exhaustive up to a few thousand boundary vertices, then the
  // standard double sweep), ties broken by the lowest index so the result is deterministic.
  std::vector<int> bnd;
  for (int i = 0; i < nv; ++i)
    if (on_boundary[static_cast<size_t>(i)]) bnd.push_back(i);
  int pa = bnd[0], pb = bnd[0];
  double best = -1.0;
  if (bnd.size() <= 3000) {
    for (size_t i = 0; i < bnd.size(); ++i)
      for (size_t j = i + 1; j < bnd.size(); ++j) {
        const double d = P[static_cast<size_t>(bnd[i])].DistanceTo(P[static_cast<size_t>(bnd[j])]);
        if (d > best * (1.0 + 1e-12)) {
          best = d;
          pa = bnd[i];
          pb = bnd[j];
        }
      }
  } else {
    auto farthest = [&](int from) {
      int arg = from;
      double bd = -1.0;
      for (int v : bnd) {
        const double d = P[static_cast<size_t>(from)].DistanceTo(P[static_cast<size_t>(v)]);
        if (d > bd * (1.0 + 1e-12)) {
          bd = d;
          arg = v;
        }
      }
      return arg;
    };
    pa = farthest(bnd[0]);
    pb = farthest(pa);
    best = P[static_cast<size_t>(pa)].DistanceTo(P[static_cast<size_t>(pb)]);
  }
  if (!(best > 0.0)) Refuse("the boundary has no two distinct points");
  if (pa > pb) std::swap(pa, pb);

  std::vector<int> free_index(static_cast<size_t>(nv), -1);
  int nfree = 0;
  for (int i = 0; i < nv; ++i)
    if (i != pa && i != pb) free_index[static_cast<size_t>(i)] = nfree++;
  const Point2d pin_a(0.0, 0.0), pin_b(best, 0.0);

  // Two real rows per triangle. With U_j = u_j + i v_j and the per-triangle complex weights W_j = (z_{j+2} - z_{j+1}) /
  // sqrt(2 area) (z the triangle flattened isometrically into its own plane), the conformality residual of a triangle is
  // sum_j W_j U_j; its real part is sum (a_j u_j - b_j v_j), its imaginary part sum (b_j u_j + a_j v_j), W_j = a_j + i b_j.
  Sparse A;
  std::vector<double> rhs;
  rhs.reserve(tris.size() * 2);
  for (size_t ti = 0; ti < tris.size(); ++ti) {
    const Tri& t = tris[ti];
    const ON_3dPoint& p0 = P[static_cast<size_t>(t.v[0])];
    const ON_3dPoint& p1 = P[static_cast<size_t>(t.v[1])];
    const ON_3dPoint& p2 = P[static_cast<size_t>(t.v[2])];
    ON_3dVector e1 = p1 - p0, e2 = p2 - p0;
    const double a = e1.Length();
    ON_3dVector n = ON_CrossProduct(e1, e2);
    const double twice_area = n.Length();
    const double scale2 = std::max(e1.LengthSquared(), e2.LengthSquared());
    if (!(a > 0.0) || !(twice_area > 1e-12 * scale2)) Refuse("a face has zero area (or is degenerate to within 1e-12 of its squared edge length)");
    ON_3dVector xh = e1 / a;
    n.Unitize();
    ON_3dVector yh = ON_CrossProduct(n, xh);
    const std::complex<double> z[3] = {{0.0, 0.0}, {a, 0.0}, {e2 * xh, e2 * yh}};
    const double inv = 1.0 / std::sqrt(twice_area);  // sqrt(2 * area) with 2 * area = |e1 x e2|
    std::complex<double> W[3];
    for (int j = 0; j < 3; ++j) W[j] = (z[(j + 2) % 3] - z[(j + 1) % 3]) * inv;
    for (int pass = 0; pass < 2; ++pass) {
      // pass 0 -> real row, pass 1 -> imaginary row
      double known = 0.0;
      for (int j = 0; j < 3; ++j) {
        const int v = t.v[j];
        const double aj = W[j].real(), bj = W[j].imag();
        const double cu = pass == 0 ? aj : bj;   // coefficient of u_j
        const double cv = pass == 0 ? -bj : aj;  // coefficient of v_j
        if (v == pa || v == pb) {
          const Point2d& pin = v == pa ? pin_a : pin_b;
          known += cu * pin.x + cv * pin.y;
        } else {
          A.Add(2 * free_index[static_cast<size_t>(v)], cu);
          A.Add(2 * free_index[static_cast<size_t>(v)] + 1, cv);
        }
      }
      A.EndRow();
      rhs.push_back(-known);
    }
  }

  const size_t nx = static_cast<size_t>(2 * nfree);
  std::vector<double> x(nx, 0.0);
  int iterations = 0;
  double rel = 0.0;
  bool converged = true;
  if (nx > 0) {
    // Jacobi-preconditioned CG on A^T A x = A^T b.
    std::vector<double> diag(nx, 0.0);
    for (size_t k = 0; k < A.val.size(); ++k) diag[static_cast<size_t>(A.col[k])] += A.val[k] * A.val[k];
    for (double& d : diag) d = d > 0.0 ? 1.0 / d : 1.0;
    std::vector<double> r(nx), z(nx), p(nx), Ap_rows, Ap(nx);
    A.MulT(rhs, r);  // r = A^T b - A^T A * 0
    const double bnorm = std::sqrt(Dot(r, r));
    if (bnorm == 0.0) {
      rel = 0.0;
    } else {
      for (size_t i = 0; i < nx; ++i) z[i] = r[i] * diag[i];
      p = z;
      double rz = Dot(r, z);
      const int max_it = static_cast<int>(std::max<size_t>(2000, 20 * nx));
      converged = false;
      for (iterations = 0; iterations < max_it; ++iterations) {
        rel = std::sqrt(Dot(r, r)) / bnorm;
        if (rel <= 1e-14) {
          converged = true;
          break;
        }
        A.Mul(p, Ap_rows);
        A.MulT(Ap_rows, Ap);
        const double pAp = Dot(p, Ap);
        if (!(pAp > 0.0)) break;
        const double alpha = rz / pAp;
        for (size_t i = 0; i < nx; ++i) {
          x[i] += alpha * p[i];
          r[i] -= alpha * Ap[i];
        }
        for (size_t i = 0; i < nx; ++i) z[i] = r[i] * diag[i];
        const double rz_new = Dot(r, z);
        const double beta = rz_new / rz;
        rz = rz_new;
        for (size_t i = 0; i < nx; ++i) p[i] = z[i] + beta * p[i];
      }
      rel = std::sqrt(Dot(r, r)) / bnorm;
      if (!converged && rel <= 1e-10) converged = true;  // stalled at round-off of a consistent system
    }
  }
  if (report) {
    report->vertex_count = nv;
    report->triangle_count = static_cast<int>(tris.size());
    report->boundary_edge_count = boundary_edges;
    report->pinned_a = pa;
    report->pinned_b = pb;
    report->iterations = iterations;
    report->residual = rel;
    report->converged = converged;
  }
  if (!converged) throw std::runtime_error("dino8::kernel::ComputeLscmUVs: the conjugate-gradient solve did not converge (relative normal-equation residual " + std::to_string(rel) + ")");

  std::vector<Point2d> uv(static_cast<size_t>(nv));
  for (int i = 0; i < nv; ++i) {
    if (i == pa) uv[static_cast<size_t>(i)] = pin_a;
    else if (i == pb) uv[static_cast<size_t>(i)] = pin_b;
    else uv[static_cast<size_t>(i)] = Point2d(x[static_cast<size_t>(2 * free_index[static_cast<size_t>(i)])], x[static_cast<size_t>(2 * free_index[static_cast<size_t>(i)] + 1)]);
  }
  return uv;
}

}  // namespace dino8::kernel
