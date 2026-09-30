// Sweep-class Brep factories: Extrude / Revolve / Loft / Sweep1 / Pipe.
// See their doc comments in brep.h for the contract; this file holds the
// shared machinery they are all built from:
//
//   - MakeCompatible(): shape-preserving section conditioning (clamp,
//     common rationality, degree elevation, [0, 1] reparameterization,
//     merged knot refinement) so a set of sections shares one knot
//     vector and control-point count.
//   - RuledBetween() / SkinSections(): the exact degree-1 ruled surface
//     between two compatible sections, and the global interpolating skin
//     (Piegl & Tiller ch. 9/10) through N of them - open (averaged knots)
//     or periodic (cyclic system, then clamped at the seam).
//   - RevolvedSurface(): Piegl & Tiller A8.1, exact rational revolution.
//   - FanCap machinery: kernel-point search for a closed planar boundary
//     and the degree-(p, 1) fan surface to that point.
//   - AssembleSweptBody(): real ON_Brep topology via ON_Brep::NewFace's
//     vid/eid/bRev3d overload - the wall first, then each cap sharing the
//     wall's own boundary edge literally, plus the two revolve caps'
//     shared axis-segment edges - followed by an outward-orientation
//     cross-check on the tessellated volume.

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"

namespace dino8::kernel {

void Brep::AppendUntrimmedFaceSideTables(int count) {
  for (int i = 0; i < count; ++i) {
    face_trim_loops_.emplace_back();
    face_exact_clip_.push_back(false);
    face_hole_loops_.emplace_back();
    face_arc_runs_.emplace_back();
    face_notch_rows_.emplace_back();
    face_records_.emplace_back();
  }
}

namespace {

[[noreturn]] void Fail(const char* caller, const std::string& what) {
  throw std::invalid_argument(std::string("dino8::kernel::Brep::") + caller + ": " + what);
}

[[noreturn]] void Internal(const char* caller, const std::string& what) {
  throw std::logic_error(std::string("dino8::kernel::Brep::") + caller + " (internal): " + what);
}

// ---------------------------------------------------------------------------
// Dense LU with partial pivoting - the interpolation systems here are
// N x N with N = the number of sections/stations (tens at most), and one
// factorization serves 4 * (control points per section) right-hand sides.
// ---------------------------------------------------------------------------
class DenseLU {
 public:
  explicit DenseLU(std::vector<double> a, int n) : n_(n), a_(std::move(a)), piv_(static_cast<size_t>(n)) {}

  bool Factor() {
    for (int k = 0; k < n_; ++k) {
      int p = k;
      double best = std::fabs(At(k, k));
      for (int i = k + 1; i < n_; ++i) {
        const double v = std::fabs(At(i, k));
        if (v > best) {
          best = v;
          p = i;
        }
      }
      if (best <= 0.0) return false;
      piv_[static_cast<size_t>(k)] = p;
      if (p != k) {
        for (int j = 0; j < n_; ++j) std::swap(At(k, j), At(p, j));
      }
      const double inv = 1.0 / At(k, k);
      for (int i = k + 1; i < n_; ++i) {
        const double f = At(i, k) * inv;
        At(i, k) = f;
        if (f == 0.0) continue;
        for (int j = k + 1; j < n_; ++j) At(i, j) -= f * At(k, j);
      }
    }
    return true;
  }

  // Solves in place. Factor() swaps WHOLE rows (the already-computed L
  // multipliers included, LAPACK-style), so P A = L U with P the full
  // interchange sequence: apply every interchange to b first, then the
  // two triangular solves. (Interleaving the swaps with the forward
  // substitution is only correct when the multipliers are NOT swapped -
  // the bug the first draft of this class had, caught by a direct
  // A * x == b check on a random system.)
  void Solve(std::vector<double>& b) const {
    for (int k = 0; k < n_; ++k) {
      const int p = piv_[static_cast<size_t>(k)];
      if (p != k) std::swap(b[static_cast<size_t>(k)], b[static_cast<size_t>(p)]);
    }
    for (int k = 0; k < n_; ++k) {
      for (int i = k + 1; i < n_; ++i) b[static_cast<size_t>(i)] -= At(i, k) * b[static_cast<size_t>(k)];
    }
    for (int i = n_ - 1; i >= 0; --i) {
      double s = b[static_cast<size_t>(i)];
      for (int j = i + 1; j < n_; ++j) s -= At(i, j) * b[static_cast<size_t>(j)];
      b[static_cast<size_t>(i)] = s / At(i, i);
    }
  }

 private:
  double& At(int i, int j) { return a_[static_cast<size_t>(i) * static_cast<size_t>(n_) + static_cast<size_t>(j)]; }
  double At(int i, int j) const {
    return a_[static_cast<size_t>(i) * static_cast<size_t>(n_) + static_cast<size_t>(j)];
  }
  int n_;
  std::vector<double> a_;
  std::vector<int> piv_;
};

// ---------------------------------------------------------------------------
// B-spline basis on a FULL knot vector `U` (the textbook form, with p+1
// copies at each clamped end; ON's own storage drops one copy at each end
// and is converted where the surfaces are built below).
// ---------------------------------------------------------------------------

// Index i with U[i] <= u < U[i+1], searched over [lo, hi) - the caller
// bounds the search to the span range its knot vector actually covers.
int SpanIndex(const std::vector<double>& U, double u, int lo, int hi) {
  // Right end: the last non-empty span.
  if (u >= U[static_cast<size_t>(hi)]) {
    int i = hi - 1;
    while (i > lo && U[static_cast<size_t>(i)] == U[static_cast<size_t>(i + 1)]) --i;
    return i;
  }
  int i = lo;
  while (i + 1 < hi && !(u < U[static_cast<size_t>(i + 1)])) ++i;
  return i;
}

// Piegl & Tiller A2.2: the p+1 nonzero basis functions N_{i-p..i, p}(u).
void BasisFuns(int i, double u, int p, const std::vector<double>& U, std::vector<double>& N) {
  N.assign(static_cast<size_t>(p) + 1, 0.0);
  std::vector<double> left(static_cast<size_t>(p) + 1, 0.0), right(static_cast<size_t>(p) + 1, 0.0);
  N[0] = 1.0;
  for (int j = 1; j <= p; ++j) {
    left[static_cast<size_t>(j)] = u - U[static_cast<size_t>(i + 1 - j)];
    right[static_cast<size_t>(j)] = U[static_cast<size_t>(i + j)] - u;
    double saved = 0.0;
    for (int r = 0; r < j; ++r) {
      const double denom = right[static_cast<size_t>(r) + 1] + left[static_cast<size_t>(j - r)];
      const double temp = denom != 0.0 ? N[static_cast<size_t>(r)] / denom : 0.0;
      N[static_cast<size_t>(r)] = saved + right[static_cast<size_t>(r) + 1] * temp;
      saved = left[static_cast<size_t>(j - r)] * temp;
    }
    N[static_cast<size_t>(j)] = saved;
  }
}

// ---------------------------------------------------------------------------
// Homogeneous control-point access. ON_NurbsCurve::GetCV(i, ON_4dPoint&)
// returns (x, y, z, 1) for a non-rational curve and the stored
// homogeneous (wx, wy, wz, w) for a rational one, so every construction
// below works in 4D uniformly and the surfaces come out rational exactly
// when their inputs are.
// ---------------------------------------------------------------------------
ON_4dPoint HomogeneousCV(const ON_NurbsCurve& c, int i) {
  ON_4dPoint p;
  c.GetCV(i, p);
  if (!c.IsRational()) p.w = 1.0;
  return p;
}

ON_3dPoint EuclideanCV(const ON_NurbsCurve& c, int i) {
  ON_3dPoint p;
  c.GetCV(i, p);
  return p;
}

// Copies every knot of `c` into direction `dir` of `s` (same ON storage
// convention on both sides, so it is a straight copy).
void CopyKnots(const ON_NurbsCurve& c, ON_NurbsSurface& s, int dir) {
  for (int i = 0; i < c.KnotCount(); ++i) s.SetKnot(dir, i, c.Knot(i));
}

double CurveScale(const ON_NurbsCurve& c) {
  ON_BoundingBox bb;
  c.GetBoundingBox(bb);
  return 1.0 + bb.Diagonal().Length();
}

void ClampIfPeriodic(ON_NurbsCurve& c) {
  if (!c.IsClamped(2)) c.ClampEnd(2);
}

// ---------------------------------------------------------------------------
// Section compatibility (see brep.h Loft() doc comment).
// ---------------------------------------------------------------------------
void MakeCompatible(std::vector<ON_NurbsCurve>& curves, const char* caller) {
  if (curves.empty()) Internal(caller, "MakeCompatible on no curves");
  bool any_rational = false;
  int max_degree = 1;
  for (ON_NurbsCurve& c : curves) {
    if (!c.IsValid()) Fail(caller, "a section is not a valid NURBS curve");
    ClampIfPeriodic(c);
    any_rational = any_rational || c.IsRational();
    max_degree = std::max(max_degree, c.Degree());
  }
  for (ON_NurbsCurve& c : curves) {
    if (any_rational && !c.IsRational() && !c.MakeRational()) Internal(caller, "MakeRational failed");
    if (c.Degree() < max_degree && !c.IncreaseDegree(max_degree)) Internal(caller, "IncreaseDegree failed");
    if (!c.SetDomain(0.0, 1.0)) Internal(caller, "SetDomain failed");
  }
  // Merged interior knots: value -> multiplicity. Values within 1e-12 of
  // an already-collected one are the same knot (bit-noise from the
  // reparameterization above, e.g. 1/3 computed two ways), anything
  // further apart is a genuinely distinct knot and stays distinct
  // (exact, if occasionally a near-degenerate span).
  std::vector<std::pair<double, int>> merged;
  for (const ON_NurbsCurve& c : curves) {
    const int kc = c.KnotCount();
    int i = 0;
    while (i < kc) {
      const double k = c.Knot(i);
      int mult = 1;
      while (i + mult < kc && c.Knot(i + mult) == k) ++mult;
      if (k > 1e-12 && k < 1.0 - 1e-12) {
        bool found = false;
        for (auto& [value, m] : merged) {
          if (std::fabs(value - k) <= 1e-12) {
            m = std::max(m, mult);
            found = true;
            break;
          }
        }
        if (!found) merged.emplace_back(k, mult);
      }
      i += mult;
    }
  }
  std::sort(merged.begin(), merged.end());
  for (ON_NurbsCurve& c : curves) {
    for (const auto& [value, mult] : merged) {
      // Snap a knot within 1e-12 to the representative first so the
      // refined vectors compare bit-for-bit below.
      for (int i = 0; i < c.KnotCount(); ++i) {
        if (c.Knot(i) != value && std::fabs(c.Knot(i) - value) <= 1e-12) c.SetKnot(i, value);
      }
      if (!c.InsertKnot(value, mult)) Internal(caller, "InsertKnot failed while making sections compatible");
    }
  }
  const ON_NurbsCurve& ref = curves.front();
  for (const ON_NurbsCurve& c : curves) {
    if (c.CVCount() != ref.CVCount() || c.KnotCount() != ref.KnotCount() || c.Degree() != ref.Degree() ||
        c.IsRational() != ref.IsRational()) {
      Internal(caller, "sections did not become compatible");
    }
    for (int i = 0; i < ref.KnotCount(); ++i) {
      if (c.Knot(i) != ref.Knot(i)) Internal(caller, "sections' knot vectors differ after refinement");
    }
  }
}

// The exact ruled surface between two compatible sections, v in [v0, v1]:
// S(u, v) = ((v1 - v) c0(u) + (v - v0) c1(u)) / (v1 - v0).
std::unique_ptr<ON_NurbsSurface> RuledBetween(const ON_NurbsCurve& c0, const ON_NurbsCurve& c1, double v0,
                                              double v1, const char* caller) {
  const int n = c0.CVCount();
  auto s = std::make_unique<ON_NurbsSurface>();
  if (!s->Create(3, c0.IsRational(), c0.Order(), 2, n, 2)) Internal(caller, "ON_NurbsSurface::Create failed");
  CopyKnots(c0, *s, 0);
  s->SetKnot(1, 0, v0);
  s->SetKnot(1, 1, v1);
  for (int i = 0; i < n; ++i) {
    s->SetCV(i, 0, HomogeneousCV(c0, i));
    s->SetCV(i, 1, HomogeneousCV(c1, i));
  }
  return s;
}

// Exact tensor-product sum surface S(u, v) = profile(u) + (path(v) -
// path(v_min)): a translational sweep of `profile` along `path`'s own
// displacement, with no frame rotation at all (unlike Sweep1's RMF
// transport). This is exact, not a fit, because a B-spline basis is a
// partition of unity: writing profile(u) = sum_i A_i N_i(u) and
// path(v) - path(v_min) = sum_j B_j M_j(v) (B_j already measured from the
// path's own first control point, which - since `path` is clamped -
// equals path(v_min) exactly), the tensor-product surface with control
// net P_ij = A_i + B_j reduces pointwise to
//   sum_i sum_j (A_i + B_j) N_i(u) M_j(v)
//     = A(u) * (sum_j M_j(v)) + (path(v) - path(v_min)) * (sum_i N_i(u))
//     = A(u) + path(v) - path(v_min),
// using partition of unity (sum_i N_i(u) = 1, sum_j M_j(v) = 1) in the
// last step - true for ANY B-spline basis regardless of degree or knot
// vector, but only for a NON-rational one (a rational basis divides by a
// per-parameter weight sum that does not itself decompose this way, so
// the same additive control-net trick is not exact for a rational curve
// in either direction - both `profile` and `path` must be non-rational,
// checked by the caller).
std::unique_ptr<ON_NurbsSurface> SumSurface(const ON_NurbsCurve& profile, const ON_NurbsCurve& path,
                                            const char* caller) {
  const int nu = profile.CVCount();
  const int nv = path.CVCount();
  auto s = std::make_unique<ON_NurbsSurface>();
  if (!s->Create(3, false, profile.Order(), path.Order(), nu, nv)) Internal(caller, "ON_NurbsSurface::Create failed");
  CopyKnots(profile, *s, 0);
  CopyKnots(path, *s, 1);
  const ON_3dPoint origin = EuclideanCV(path, 0);
  for (int j = 0; j < nv; ++j) {
    const ON_3dVector offset = EuclideanCV(path, j) - origin;
    for (int i = 0; i < nu; ++i) s->SetCV(i, j, EuclideanCV(profile, i) + offset);
  }
  return s;
}

// Chord-length station parameters averaged over the control-point
// columns (Piegl & Tiller 10.3). Returns N values starting at 0; for a
// closed skin the wrap-around chord is included and `period` receives
// the total, else the values are scaled to end at 1.
std::vector<double> SkinParameters(const std::vector<ON_NurbsCurve>& sections, bool closed, double* period,
                                   const char* caller) {
  const int N = static_cast<int>(sections.size());
  const int n = sections.front().CVCount();
  const int segments = closed ? N : N - 1;
  std::vector<double> accum(static_cast<size_t>(segments) + 1, 0.0);
  int used_columns = 0;
  for (int i = 0; i < n; ++i) {
    std::vector<double> d(static_cast<size_t>(segments) + 1, 0.0);
    double total = 0.0;
    for (int k = 1; k <= segments; ++k) {
      const ON_3dPoint a = EuclideanCV(sections[static_cast<size_t>(k - 1)], i);
      const ON_3dPoint b = EuclideanCV(sections[static_cast<size_t>(k % N)], i);
      total += a.DistanceTo(b);
      d[static_cast<size_t>(k)] = total;
    }
    if (total <= 0.0) continue;  // a column shared by every section (a loft to a point) says nothing
    ++used_columns;
    for (int k = 1; k <= segments; ++k) accum[static_cast<size_t>(k)] += d[static_cast<size_t>(k)] / total;
  }
  if (used_columns == 0) Fail(caller, "every section coincides with every other one - nothing to loft");
  std::vector<double> params(static_cast<size_t>(N), 0.0);
  for (int k = 1; k < N; ++k) params[static_cast<size_t>(k)] = accum[static_cast<size_t>(k)] / used_columns;
  for (int k = 1; k < N; ++k) {
    if (!(params[static_cast<size_t>(k)] > params[static_cast<size_t>(k - 1)])) {
      Fail(caller, "two consecutive sections coincide - station parameters must strictly increase");
    }
  }
  if (closed) {
    *period = accum[static_cast<size_t>(N)] / used_columns;
    if (!(*period > params.back())) Fail(caller, "the last section coincides with the first one");
  } else {
    *period = 1.0;  // unused
    const double last = params.back();
    for (double& p : params) p /= last;
  }
  return params;
}

// Global interpolating skin through compatible `sections`, degree q in v.
// Open: clamped, averaged knots, v in [0, 1]. Closed: periodic
// interpolation over the cyclic system, converted to the clamped
// representation of the same closed surface (ClampEnd) so callers see
// an ordinary clamped surface with IsClosed(1) true.
std::unique_ptr<ON_NurbsSurface> SkinSections(const std::vector<ON_NurbsCurve>& sections, int q, bool closed,
                                              const std::vector<double>& params, double period,
                                              const char* caller) {
  const int N = static_cast<int>(sections.size());
  const int n = sections.front().CVCount();
  if (q < 1) Internal(caller, "skin degree < 1");
  if (N < q + 1) Internal(caller, "too few sections for the skin degree");

  std::vector<double> A(static_cast<size_t>(N) * static_cast<size_t>(N), 0.0);
  auto a = [&](int k, int i) -> double& { return A[static_cast<size_t>(k) * static_cast<size_t>(N) + static_cast<size_t>(i)]; };

  std::vector<double> U;     // full knot vector (open) / extended knots (closed)
  int ext_offset = 0;        // closed: extended basis index j <-> U index j + ext_offset
  std::vector<double> Nb;

  if (!closed) {
    // Piegl & Tiller eq. 9.8 averaged knots on a full vector of size N + q + 1.
    U.assign(static_cast<size_t>(N + q + 1), 0.0);
    for (int i = 0; i <= q; ++i) {
      U[static_cast<size_t>(i)] = 0.0;
      U[static_cast<size_t>(N + i)] = 1.0;
    }
    for (int j = 1; j <= N - q - 1; ++j) {
      double s = 0.0;
      for (int i = j; i <= j + q - 1; ++i) s += params[static_cast<size_t>(i)];
      U[static_cast<size_t>(j + q)] = s / q;
    }
    for (int k = 0; k < N; ++k) {
      const int span = SpanIndex(U, params[static_cast<size_t>(k)], q, N);
      BasisFuns(span, params[static_cast<size_t>(k)], q, U, Nb);
      for (int r = 0; r <= q; ++r) a(k, span - q + r) += Nb[static_cast<size_t>(r)];
    }
  } else {
    // Periodic knots t_j, j in [-(q+1), N+q+1]: at the station parameters
    // for odd q, at the midpoints between consecutive stations for even q
    // (Schoenberg-Whitney for the cyclic system), extended by the period.
    std::vector<double> base(static_cast<size_t>(N), 0.0);
    for (int j = 0; j < N; ++j) {
      if (q % 2 == 1) {
        base[static_cast<size_t>(j)] = params[static_cast<size_t>(j)];
      } else {
        const double next = j + 1 < N ? params[static_cast<size_t>(j + 1)] : params[0] + period;
        base[static_cast<size_t>(j)] = 0.5 * (params[static_cast<size_t>(j)] + next);
      }
    }
    const int j_lo = -(q + 1), j_hi = N + q + 1;
    ext_offset = -j_lo;
    U.resize(static_cast<size_t>(j_hi - j_lo + 1));
    for (int j = j_lo; j <= j_hi; ++j) {
      const int m = ((j % N) + N) % N;
      const int wraps = (j - m) / N;  // exact: j - m is a multiple of N
      U[static_cast<size_t>(j + ext_offset)] = base[static_cast<size_t>(m)] + wraps * period;
    }
    for (int k = 0; k < N; ++k) {
      const double v = params[static_cast<size_t>(k)];
      // Span in the extended array: U[m] <= v < U[m+1].
      int m = 0;
      while (m + 1 < static_cast<int>(U.size()) && !(v < U[static_cast<size_t>(m + 1)])) ++m;
      if (m < q || m + 1 >= static_cast<int>(U.size())) Internal(caller, "periodic span search left the extension");
      BasisFuns(m, v, q, U, Nb);
      for (int r = 0; r <= q; ++r) {
        const int j = (m - q + r) - ext_offset;  // extended basis index
        const int col = ((j % N) + N) % N;
        a(k, col) += Nb[static_cast<size_t>(r)];
      }
    }
  }

  DenseLU lu(A, N);
  if (!lu.Factor()) Fail(caller, "the interpolation system is singular (coincident or badly ordered sections)");

  // Solve for every control-point column and every homogeneous coordinate.
  std::vector<std::array<std::vector<double>, 4>> P(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    for (int c = 0; c < 4; ++c) {
      std::vector<double> rhs(static_cast<size_t>(N));
      for (int k = 0; k < N; ++k) {
        const ON_4dPoint h = HomogeneousCV(sections[static_cast<size_t>(k)], i);
        rhs[static_cast<size_t>(k)] = c == 0 ? h.x : c == 1 ? h.y : c == 2 ? h.z : h.w;
      }
      lu.Solve(rhs);
      P[static_cast<size_t>(i)][static_cast<size_t>(c)] = std::move(rhs);
    }
  }

  const bool rational = sections.front().IsRational();
  auto s = std::make_unique<ON_NurbsSurface>();
  if (!closed) {
    if (!s->Create(3, rational, sections.front().Order(), q + 1, n, N)) Internal(caller, "Create failed");
    CopyKnots(sections.front(), *s, 0);
    // ON drops the first and last copy of the full vector.
    for (int j = 0; j < N + q - 1; ++j) s->SetKnot(1, j, U[static_cast<size_t>(j + 1)]);
    for (int i = 0; i < n; ++i) {
      for (int k = 0; k < N; ++k) {
        const auto& col = P[static_cast<size_t>(i)];
        s->SetCV(i, k, ON_4dPoint(col[0][static_cast<size_t>(k)], col[1][static_cast<size_t>(k)],
                                  col[2][static_cast<size_t>(k)], col[3][static_cast<size_t>(k)]));
      }
    }
  } else {
    const int cv_v = N + q;
    if (!s->Create(3, rational, sections.front().Order(), q + 1, n, cv_v)) Internal(caller, "Create failed");
    CopyKnots(sections.front(), *s, 0);
    // ON knot j <-> t_{j - (q - 1)}; domain [t_0, t_N] = one full period.
    for (int j = 0; j < cv_v + q - 1; ++j) s->SetKnot(1, j, U[static_cast<size_t>(j - (q - 1) + ext_offset)]);
    for (int i = 0; i < n; ++i) {
      for (int c = 0; c < cv_v; ++c) {
        const int k = (((c - q) % N) + N) % N;
        const auto& col = P[static_cast<size_t>(i)];
        s->SetCV(i, c, ON_4dPoint(col[0][static_cast<size_t>(k)], col[1][static_cast<size_t>(k)],
                                  col[2][static_cast<size_t>(k)], col[3][static_cast<size_t>(k)]));
      }
    }
    if (!s->IsPeriodic(1)) Internal(caller, "the periodic skin is not ON-periodic");
    if (!s->ClampEnd(1, 2)) Internal(caller, "ClampEnd on the periodic skin failed");
    if (q % 2 == 0) {
      // Midpoint knots put the seam halfway between sections 0 and 1;
      // move it to section 0 (v_0 + period lies inside the domain) and
      // re-anchor the domain at 0 so S(u, v_k) = section k again.
      const ON_Interval dom = s->Domain(1);
      if (!s->ChangeSurfaceSeam(1, params[0] + period)) Internal(caller, "ChangeSurfaceSeam failed on the even-degree skin");
      if (!s->SetDomain(1, 0.0, dom.Length())) Internal(caller, "SetDomain failed on the even-degree skin");
    }
    if (!s->IsClosed(1)) Internal(caller, "the clamped periodic skin does not report closed");
  }
  if (!s->IsValid()) Internal(caller, "the skinned surface is not valid");
  return s;
}

// Global interpolating skin through compatible, OPEN (non-closed,
// non-periodic) `sections`, degree q, additionally pinning the exact
// v-derivative at v=0 (`start_dirs`, one 3D vector per control-point
// column) and/or v=1 (`end_dirs`) - empty means unconstrained at that
// end. Requires q >= 2 whenever either is given: a clamped B-spline's
// derivative at v=0 depends only on its own first two v-control points
// (Piegl & Tiller eq. 3.3, specialised to the p+1 repeated knots at a
// clamped end: C'(0) = q(P_1 - P_0)/(U[q+1] - U[0]), and mirrored at
// v=1), so one extra control point is added per constrained end and one
// extra linear equation - involving only that end's own two nearest
// control points, nothing else - fixes it exactly, leaving every
// section's own interpolation row (built the same way as the open
// branch of SkinSections() above) undisturbed. The extra control
// point(s) need one more interior knot each; `q >= 2` keeps the
// duplicated end station this introduces (see `vparams` below) out of
// every averaging window that would otherwise raise the end knot's own
// multiplicity past q + 1 (an invalid knot vector).
std::unique_ptr<ON_NurbsSurface> SkinSectionsTangent(const std::vector<ON_NurbsCurve>& sections, int q,
                                                     const std::vector<double>& params,
                                                     const std::vector<ON_3dVector>& start_dirs,
                                                     const std::vector<ON_3dVector>& end_dirs, const char* caller) {
  const int N = static_cast<int>(sections.size());
  const int n = sections.front().CVCount();
  const bool se = !start_dirs.empty();
  const bool ee = !end_dirs.empty();
  const int extra = (se ? 1 : 0) + (ee ? 1 : 0);
  const int n_ctrl = N + extra;
  if (q < 2) Internal(caller, "tangent-constrained skin needs degree >= 2");
  if (N < q + 1) Internal(caller, "too few sections for the skin degree");

  // Virtual station sequence: `params` with the constrained end's own
  // station duplicated once per constrained end. A sliding-window
  // average over a non-decreasing sequence is itself non-decreasing, so
  // the interior knots computed from it below stay non-decreasing too.
  std::vector<double> vparams;
  vparams.reserve(static_cast<size_t>(n_ctrl));
  vparams.push_back(params[0]);
  if (se) vparams.push_back(params[0]);
  for (int k = 1; k < N - 1; ++k) vparams.push_back(params[static_cast<size_t>(k)]);
  if (ee) vparams.push_back(params[static_cast<size_t>(N - 1)]);
  vparams.push_back(params[static_cast<size_t>(N - 1)]);
  if (static_cast<int>(vparams.size()) != n_ctrl) Internal(caller, "virtual station count mismatch");

  std::vector<double> U(static_cast<size_t>(n_ctrl + q + 1), 0.0);
  for (int i = 0; i <= q; ++i) {
    U[static_cast<size_t>(i)] = 0.0;
    U[static_cast<size_t>(n_ctrl + i)] = 1.0;
  }
  for (int j = 1; j <= n_ctrl - q - 1; ++j) {
    double s = 0.0;
    for (int i = j; i <= j + q - 1; ++i) s += vparams[static_cast<size_t>(i)];
    U[static_cast<size_t>(j + q)] = s / q;
  }

  std::vector<double> A(static_cast<size_t>(n_ctrl) * static_cast<size_t>(n_ctrl), 0.0);
  auto a = [&](int row, int col) -> double& {
    return A[static_cast<size_t>(row) * static_cast<size_t>(n_ctrl) + static_cast<size_t>(col)];
  };
  // rhs_by_column[i][channel] holds that column/channel's right-hand
  // side over all n_ctrl rows (position rows first, then the up-to-2
  // derivative rows) - built once, solved n times (once per LU factor
  // reuse) below, same layout SkinSections() uses per column/channel.
  std::vector<std::array<std::vector<double>, 4>> rhs(static_cast<size_t>(n));
  for (auto& col : rhs)
    for (auto& ch : col) ch.assign(static_cast<size_t>(n_ctrl), 0.0);

  std::vector<double> Nb;
  for (int k = 0; k < N; ++k) {
    const int span = SpanIndex(U, params[static_cast<size_t>(k)], q, n_ctrl);
    BasisFuns(span, params[static_cast<size_t>(k)], q, U, Nb);
    for (int r = 0; r <= q; ++r) a(k, span - q + r) += Nb[static_cast<size_t>(r)];
    for (int i = 0; i < n; ++i) {
      const ON_4dPoint h = HomogeneousCV(sections[static_cast<size_t>(k)], i);
      rhs[static_cast<size_t>(i)][0][static_cast<size_t>(k)] = h.x;
      rhs[static_cast<size_t>(i)][1][static_cast<size_t>(k)] = h.y;
      rhs[static_cast<size_t>(i)][2][static_cast<size_t>(k)] = h.z;
      rhs[static_cast<size_t>(i)][3][static_cast<size_t>(k)] = h.w;
    }
  }
  int row = N;
  if (se) {
    a(row, 0) = -static_cast<double>(q);
    a(row, 1) = static_cast<double>(q);
    const double span0 = U[static_cast<size_t>(q + 1)];  // - U[0], and U[0] == 0
    for (int i = 0; i < n; ++i) {
      const ON_3dVector d = start_dirs[static_cast<size_t>(i)] * span0;
      rhs[static_cast<size_t>(i)][0][static_cast<size_t>(row)] = d.x;
      rhs[static_cast<size_t>(i)][1][static_cast<size_t>(row)] = d.y;
      rhs[static_cast<size_t>(i)][2][static_cast<size_t>(row)] = d.z;
      rhs[static_cast<size_t>(i)][3][static_cast<size_t>(row)] = 0.0;  // non-rational: weight channel stays 1 throughout
    }
    ++row;
  }
  if (ee) {
    a(row, n_ctrl - 1) = static_cast<double>(q);
    a(row, n_ctrl - 2) = -static_cast<double>(q);
    const double span1 = 1.0 - U[static_cast<size_t>(n_ctrl - 1)];  // U[n_ctrl + q - 1] - U[n_ctrl - 1], and U[n_ctrl + q - 1] == 1
    for (int i = 0; i < n; ++i) {
      const ON_3dVector d = end_dirs[static_cast<size_t>(i)] * span1;
      rhs[static_cast<size_t>(i)][0][static_cast<size_t>(row)] = d.x;
      rhs[static_cast<size_t>(i)][1][static_cast<size_t>(row)] = d.y;
      rhs[static_cast<size_t>(i)][2][static_cast<size_t>(row)] = d.z;
      rhs[static_cast<size_t>(i)][3][static_cast<size_t>(row)] = 0.0;
    }
    ++row;
  }
  if (row != n_ctrl) Internal(caller, "tangent-constrained skin row count mismatch");

  DenseLU lu(A, n_ctrl);
  if (!lu.Factor()) Fail(caller, "the tangent-constrained interpolation system is singular (coincident or badly ordered sections)");

  std::vector<std::array<std::vector<double>, 4>> P(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    for (int c = 0; c < 4; ++c) {
      std::vector<double> b = rhs[static_cast<size_t>(i)][static_cast<size_t>(c)];
      lu.Solve(b);
      P[static_cast<size_t>(i)][static_cast<size_t>(c)] = std::move(b);
    }
  }

  const bool rational = sections.front().IsRational();
  auto s = std::make_unique<ON_NurbsSurface>();
  if (!s->Create(3, rational, sections.front().Order(), q + 1, n, n_ctrl)) Internal(caller, "Create failed");
  CopyKnots(sections.front(), *s, 0);
  for (int j = 0; j < n_ctrl + q - 1; ++j) s->SetKnot(1, j, U[static_cast<size_t>(j + 1)]);
  for (int i = 0; i < n; ++i) {
    for (int k = 0; k < n_ctrl; ++k) {
      const auto& col = P[static_cast<size_t>(i)];
      s->SetCV(i, k, ON_4dPoint(col[0][static_cast<size_t>(k)], col[1][static_cast<size_t>(k)],
                                col[2][static_cast<size_t>(k)], col[3][static_cast<size_t>(k)]));
    }
  }
  if (!s->IsValid()) Internal(caller, "the tangent-constrained skinned surface is not valid");
  return s;
}

// ---------------------------------------------------------------------------
// Surface of revolution (Piegl & Tiller A8.1). u = profile, v = angle in
// radians over [0, angle]. Rational quadratic in v with one arc per <= 90
// degree sector; the full-circle case uses an exact quadrant table so
// the v-seam control points coincide bit-for-bit (IsClosed(1)).
// ---------------------------------------------------------------------------
std::unique_ptr<ON_NurbsSurface> RevolvedSurface(const ON_NurbsCurve& profile_in, ON_3dPoint origin, ON_3dVector T,
                                                 double angle, bool full, double on_axis_tol, const char* caller) {
  ON_NurbsCurve profile = profile_in;
  if (!profile.IsRational()) profile.MakeRational();
  const int narcs = full ? 4 : (angle <= 0.5 * ON_PI + 1e-12 ? 1 : angle <= ON_PI + 1e-12 ? 2 : angle <= 1.5 * ON_PI + 1e-12 ? 3 : 4);
  const double dtheta = full ? 0.5 * ON_PI : angle / narcs;
  const double wm = std::cos(0.5 * dtheta);
  const int n = profile.CVCount();
  const int cv_v = 2 * narcs + 1;

  auto s = std::make_unique<ON_NurbsSurface>();
  if (!s->Create(3, true, profile.Order(), 3, n, cv_v)) Internal(caller, "Create failed");
  CopyKnots(profile, *s, 0);
  // v knots (ON storage: cv_v + 3 - 2 = 2*narcs + 2 entries): 0,0, then
  // each interior sector boundary twice, then angle, angle.
  {
    int j = 0;
    s->SetKnot(1, j++, 0.0);
    s->SetKnot(1, j++, 0.0);
    for (int k = 1; k < narcs; ++k) {
      const double kv = full ? k * (0.5 * ON_PI) : angle * k / narcs;
      s->SetKnot(1, j++, kv);
      s->SetKnot(1, j++, kv);
    }
    const double end = full ? 2.0 * ON_PI : angle;
    s->SetKnot(1, j++, end);
    s->SetKnot(1, j++, end);
  }

  // Exact quadrant table for the full circle: (cos, sin) at 0, 90, 180, 270, 360 degrees.
  static const double kQuadCos[5] = {1.0, 0.0, -1.0, 0.0, 1.0};
  static const double kQuadSin[5] = {0.0, 1.0, 0.0, -1.0, 0.0};

  for (int i = 0; i < n; ++i) {
    const ON_4dPoint h = HomogeneousCV(profile, i);
    const double w = h.w;
    const ON_3dPoint P(h.x / w, h.y / w, h.z / w);
    const ON_3dVector d = P - origin;
    const ON_3dPoint O = origin + T * ON_DotProduct(d, T);
    ON_3dVector X = P - O;
    const double r = X.Length();
    if (r <= on_axis_tol) {
      // On the axis: every CV of this row is the axis point itself,
      // exactly, so the side is genuinely singular (ON_NurbsSurface::
      // IsSingular compares the row's CVs for coincidence).
      for (int c = 0; c < cv_v; ++c) {
        const double wc = (c % 2 == 1) ? w * wm : w;
        s->SetCV(i, c, ON_4dPoint(O.x * wc, O.y * wc, O.z * wc, wc));
      }
      continue;
    }
    X = X / r;
    const ON_3dVector Y = ON_CrossProduct(T, X);
    auto place = [&](int c, ON_3dPoint p, double wc) { s->SetCV(i, c, ON_4dPoint(p.x * wc, p.y * wc, p.z * wc, wc)); };
    if (full) {
      for (int k = 0; k < 4; ++k) {
        const ON_3dPoint p0 = k == 0 ? P : O + (X * (r * kQuadCos[k]) + Y * (r * kQuadSin[k]));
        // Corner CV of quadrant k: O + r (cos_k + cos_{k+1}) X + r (sin_k + sin_{k+1}) Y, weight wm.
        const ON_3dPoint pm = O + (X * (r * (kQuadCos[k] + kQuadCos[k + 1])) + Y * (r * (kQuadSin[k] + kQuadSin[k + 1])));
        place(2 * k, p0, w);
        place(2 * k + 1, pm, w * wm);
      }
      place(8, P, w);  // bit-identical to CV 0: the seam closes exactly
    } else {
      double th = 0.0;
      ON_3dPoint P0 = P;
      ON_3dVector T0 = Y;
      place(0, P0, w);
      for (int k = 1; k <= narcs; ++k) {
        th += dtheta;
        const ON_3dPoint P2 = O + (X * (r * std::cos(th)) + Y * (r * std::sin(th)));
        const ON_3dVector T2 = X * (-std::sin(th)) + Y * std::cos(th);
        const ON_3dPoint P1 = P0 + T0 * (r * std::tan(0.5 * dtheta));
        place(2 * k - 1, P1, w * wm);
        place(2 * k, P2, w);
        P0 = P2;
        T0 = T2;
      }
    }
  }
  if (!s->IsValid()) Internal(caller, "the revolved surface is not valid");
  return s;
}

// ---------------------------------------------------------------------------
// Planar boundary analysis for caps.
// ---------------------------------------------------------------------------

double SignedArea2d(const std::vector<ON_2dPoint>& p) {
  double a = 0.0;
  for (size_t i = 0, n = p.size(); i < n; ++i) {
    const ON_2dPoint& q = p[i];
    const ON_2dPoint& r = p[(i + 1) % n];
    a += q.x * r.y - r.x * q.y;
  }
  return 0.5 * a;
}

// Clips convex polygon `poly` by the half-plane to the LEFT of the
// directed line a -> b (inclusive). Sutherland-Hodgman, one edge.
std::vector<ON_2dPoint> ClipConvexByLeftHalfPlane(const std::vector<ON_2dPoint>& poly, ON_2dPoint a, ON_2dPoint b) {
  std::vector<ON_2dPoint> out;
  const size_t n = poly.size();
  if (n == 0) return out;
  auto side = [&](const ON_2dPoint& p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
  for (size_t i = 0; i < n; ++i) {
    const ON_2dPoint& p = poly[i];
    const ON_2dPoint& q = poly[(i + 1) % n];
    const double sp = side(p), sq = side(q);
    if (sp >= 0.0) out.push_back(p);
    if ((sp > 0.0 && sq < 0.0) || (sp < 0.0 && sq > 0.0)) {
      const double t = sp / (sp - sq);
      out.emplace_back(p.x + t * (q.x - p.x), p.y + t * (q.y - p.y));
    }
  }
  return out;
}

ON_2dPoint Centroid2d(const std::vector<ON_2dPoint>& p) {
  double a = 0.0, cx = 0.0, cy = 0.0;
  for (size_t i = 0, n = p.size(); i < n; ++i) {
    const ON_2dPoint& q = p[i];
    const ON_2dPoint& r = p[(i + 1) % n];
    const double c = q.x * r.y - r.x * q.y;
    a += c;
    cx += (q.x + r.x) * c;
    cy += (q.y + r.y) * c;
  }
  if (std::fabs(a) < 1e-300) {
    // Degenerate (collinear) polygon: plain vertex average.
    ON_2dPoint m(0, 0);
    for (const ON_2dPoint& q : p) {
      m.x += q.x;
      m.y += q.y;
    }
    return ON_2dPoint(m.x / p.size(), m.y / p.size());
  }
  return ON_2dPoint(cx / (3.0 * a), cy / (3.0 * a));
}

struct CapPlan {
  ON_Plane plane;
  ON_3dPoint apex;
  double signed_area = 0.0;  // of the boundary (plus closing chord) about plane.zaxis
};

// Finds a fan apex for the planar region bounded by `boundary` (closed),
// or by `boundary` plus the straight chord from its end back to its
// start (`chord_closed`, in which case the apex is constrained to lie on
// that chord). Throws std::invalid_argument when the region has no
// kernel (is not star-shaped) or is not planar.
CapPlan PlanCap(const ON_NurbsCurve& boundary, bool chord_closed, const char* caller) {
  CapPlan plan;
  const double scale = CurveScale(boundary);
  if (!boundary.IsPlanar(&plan.plane, 1e-8 * scale)) {
    Fail(caller, "cap requested but the end section is not planar - cannot cap a non-planar end");
  }
  const ON_Interval dom = boundary.Domain();
  const int spans = boundary.SpanCount();
  const int m = std::max(256, 32 * spans);
  std::vector<ON_2dPoint> poly;
  poly.reserve(static_cast<size_t>(m) + 1);
  const int last = chord_closed ? m : m - 1;  // a closed boundary's sample m duplicates sample 0
  for (int i = 0; i <= last; ++i) {
    const ON_3dPoint p = boundary.PointAt(dom.ParameterAt(static_cast<double>(i) / m));
    double x, y;
    plan.plane.ClosestPointTo(p, &x, &y);
    poly.emplace_back(x, y);
  }
  plan.signed_area = SignedArea2d(poly);
  const double bbox_area = [&] {
    double x0 = poly[0].x, x1 = x0, y0 = poly[0].y, y1 = y0;
    for (const ON_2dPoint& p : poly) {
      x0 = std::min(x0, p.x);
      x1 = std::max(x1, p.x);
      y0 = std::min(y0, p.y);
      y1 = std::max(y1, p.y);
    }
    return (x1 - x0) * (y1 - y0);
  }();
  if (std::fabs(plan.signed_area) <= 1e-12 * std::max(bbox_area, 1e-300)) {
    Fail(caller, "cap requested but the end section encloses no area");
  }
  std::vector<ON_2dPoint> ccw = poly;
  if (plan.signed_area < 0.0) std::reverse(ccw.begin(), ccw.end());

  // Kernel = intersection of every edge's left half-plane, seeded with a
  // generous box around the polygon.
  std::vector<ON_2dPoint> kernel;
  {
    double x0 = ccw[0].x, x1 = x0, y0 = ccw[0].y, y1 = y0;
    for (const ON_2dPoint& p : ccw) {
      x0 = std::min(x0, p.x);
      x1 = std::max(x1, p.x);
      y0 = std::min(y0, p.y);
      y1 = std::max(y1, p.y);
    }
    const double pad = 1.0 + (x1 - x0) + (y1 - y0);
    kernel = {ON_2dPoint(x0 - pad, y0 - pad), ON_2dPoint(x1 + pad, y0 - pad), ON_2dPoint(x1 + pad, y1 + pad),
              ON_2dPoint(x0 - pad, y1 + pad)};
  }
  const double edge_eps = 1e-14 * std::sqrt(std::max(bbox_area, 1e-300));
  for (size_t i = 0, n = ccw.size(); i < n && !kernel.empty(); ++i) {
    const ON_2dPoint& a = ccw[i];
    const ON_2dPoint& b = ccw[(i + 1) % n];
    if (a.DistanceTo(b) <= edge_eps) continue;  // a repeated sample defines no half-plane
    kernel = ClipConvexByLeftHalfPlane(kernel, a, b);
  }
  const double kernel_area = kernel.size() >= 3 ? std::fabs(SignedArea2d(kernel)) : 0.0;

  ON_2dPoint apex2d;
  if (!chord_closed) {
    if (kernel_area <= 1e-10 * bbox_area) {
      Fail(caller,
           "cap requested but the end section's region is not star-shaped (its kernel is empty) - a fan "
           "cap would fold over itself, so this end cannot be capped yet");
    }
    apex2d = Centroid2d(kernel);
  } else {
    // Apex must lie on the chord (the axis segment) AND in the kernel:
    // clip the chord against every kernel edge.
    if (kernel.size() < 3) Fail(caller, "cap requested but the profile region has no kernel on the axis");
    ON_2dPoint c0 = poly.back(), c1 = poly.front();
    double t0 = 0.0, t1 = 1.0;
    for (size_t i = 0, n = kernel.size(); i < n; ++i) {
      const ON_2dPoint& a = kernel[i];
      const ON_2dPoint& b = kernel[(i + 1) % n];
      const double s0 = (b.x - a.x) * (c0.y - a.y) - (b.y - a.y) * (c0.x - a.x);
      const double s1 = (b.x - a.x) * (c1.y - a.y) - (b.y - a.y) * (c1.x - a.x);
      if (s0 < 0.0 && s1 < 0.0) {
        t0 = 1.0;
        t1 = 0.0;
        break;
      }
      if (s0 < 0.0) t0 = std::max(t0, s0 / (s0 - s1));
      if (s1 < 0.0) t1 = std::min(t1, s0 / (s0 - s1));
    }
    if (!(t1 > t0)) {
      Fail(caller,
           "cap requested but no point of the axis segment sees the whole profile (the region is not "
           "star-shaped from the axis) - this end cannot be capped yet");
    }
    const double t = 0.5 * (t0 + t1);
    apex2d = ON_2dPoint(c0.x + t * (c1.x - c0.x), c0.y + t * (c1.y - c0.y));
  }

  // Re-verify against the true curve, densely: the angle of C(u) - X must
  // be strictly monotone (every fan triangle turns the same way), and no
  // sample may sit on the apex. This is what makes the fan an embedding
  // rather than a self-overlapping sheet.
  {
    const int mm = 4 * m;
    const double turn_eps = 1e-14 * bbox_area;
    int sign = 0;
    ON_2dPoint prev;
    for (int i = 0; i <= mm; ++i) {
      const ON_3dPoint p = boundary.PointAt(dom.ParameterAt(static_cast<double>(i) / mm));
      double x, y;
      plan.plane.ClosestPointTo(p, &x, &y);
      const ON_2dPoint q(x - apex2d.x, y - apex2d.y);
      if (q.x * q.x + q.y * q.y <= turn_eps) Fail(caller, "cap apex lands on the section boundary");
      if (i > 0) {
        const double cross = prev.x * q.y - prev.y * q.x;
        if (std::fabs(cross) <= turn_eps) {
          if (prev.x * q.x + prev.y * q.y < 0.0) Fail(caller, "the section boundary passes through the cap apex");
        } else {
          const int s = cross > 0.0 ? 1 : -1;
          if (sign == 0) sign = s;
          if (s != sign) {
            Fail(caller, "cap requested but the section boundary doubles back as seen from the cap apex - the "
                         "region is not star-shaped there, so this end cannot be capped yet");
          }
        }
      }
      prev = q;
    }
  }
  plan.apex = plan.plane.PointAt(apex2d.x, apex2d.y);
  return plan;
}

// D(u, v) = (1 - v) X + v B(u): degree (p, 1), v in [0, 1]. Row v=0 carries
// B's own weights so the v-lines are straight in Euclidean space.
std::unique_ptr<ON_NurbsSurface> FanSurface(const ON_NurbsCurve& B, ON_3dPoint X, const char* caller) {
  const int n = B.CVCount();
  auto s = std::make_unique<ON_NurbsSurface>();
  if (!s->Create(3, B.IsRational(), B.Order(), 2, n, 2)) Internal(caller, "Create failed");
  CopyKnots(B, *s, 0);
  s->SetKnot(1, 0, 0.0);
  s->SetKnot(1, 1, 1.0);
  for (int i = 0; i < n; ++i) {
    const ON_4dPoint h = HomogeneousCV(B, i);
    s->SetCV(i, 0, ON_4dPoint(X.x * h.w, X.y * h.w, X.z * h.w, h.w));
    s->SetCV(i, 1, h);
  }
  if (!s->IsSingular(0)) Internal(caller, "the fan cap's apex side is not singular");
  return s;
}

ON_NurbsCurve* IsoCurveOf(const ON_NurbsSurface& s, int dir, double c, const char* caller) {
  ON_Curve* raw = s.IsoCurve(dir, c);
  ON_NurbsCurve* nc = ON_NurbsCurve::Cast(raw);
  if (!nc) {
    delete raw;
    Internal(caller, "IsoCurve did not return a NURBS curve");
  }
  return nc;
}

// Reverses `c` in place and restores its original domain so uniform
// parameter samples of the reversed curve are the mirrored set of the
// original's (needed for the cap/wall tessellation weld).
void ReverseKeepDomain(ON_NurbsCurve& c) {
  const ON_Interval dom = c.Domain();
  c.Reverse();
  c.SetDomain(dom.Min(), dom.Max());
}

// One cap face added to `brep`, sharing `shared_edge` (the wall's own
// boundary edge whose 3D direction runs WITH `boundary`'s parameter when
// `edge_forward`). `outward_hint` is a direction the cap's outward normal
// must have a positive dot product with; the boundary is reversed if
// needed so the fan's own u x v normal is that outward normal. Returns
// the cap's east (X -> B(b)) and west (B(a) -> X) edge indices for the
// chord-cap sharing described at AssembleSweptBody.
struct CapEdges {
  int east = -1;
  int west = -1;
  ON_3dPoint east_end;  // the 3D point at the cap boundary's end of its east edge
  ON_3dPoint west_end;  // ... and of its west edge
};

// `transpose` builds the fan as D(u, v) = (1 - u) X + u B(v) instead -
// the boundary then runs along the cap's v, matching a wall edge that is
// itself a v-direction isocurve (a revolve's end circle), so
// Tessellate(u_divisions, v_divisions) samples both sides of that edge
// with the same v_divisions and they weld at ANY division pair (the
// untransposed fan would sample it with u_divisions on the cap side).
CapEdges AddFanCap(ON_Brep& brep, ON_NurbsCurve boundary, int shared_edge, bool edge_forward, ON_3dVector outward_hint,
                   const CapPlan& plan, const CapEdges* share_chord, bool transpose, const char* caller) {
  // A CCW boundary (about plane.zaxis) has fan normal -zaxis for
  // D(u, v) = (1 - v) X + v B(u) (derived in brep.h's doc comment);
  // transposing swaps u and v and so negates it.
  ON_3dVector native = plan.signed_area > 0.0 ? -plan.plane.zaxis : plan.plane.zaxis;
  if (transpose) native = -native;
  const double agree = ON_DotProduct(native, outward_hint);
  if (std::fabs(agree) <= 1e-9 * outward_hint.Length()) {
    Fail(caller, "the cap plane is parallel to the sweep direction at that end - cannot cap it");
  }
  const bool reversed = agree < 0.0;
  if (reversed) ReverseKeepDomain(boundary);
  std::unique_ptr<ON_NurbsSurface> cap = FanSurface(boundary, plan.apex, caller);
  if (transpose && !cap->Transpose()) Internal(caller, "Transpose failed on a disc cap");

  int vid[4] = {-1, -1, -1, -1};
  int eid[4] = {-1, -1, -1, -1};
  bool rev[4] = {false, false, false, false};
  if (!transpose) {
    // Boundary = north side; its trim runs -u_cap. Not reversed: -u_cap
    // opposes the boundary's direction, so it opposes the edge iff the
    // edge runs with the boundary; reversed: the opposite.
    eid[2] = shared_edge;
    rev[2] = edge_forward != reversed;
  } else {
    // Boundary = east side; its trim runs +v_cap, i.e. WITH the (possibly
    // reversed) boundary: it opposes the edge iff (edge runs with the
    // original boundary) == reversed.
    eid[1] = shared_edge;
    rev[1] = edge_forward == reversed;
  }
  if (share_chord && transpose) Internal(caller, "chord sharing is only defined for untransposed caps");
  if (share_chord) {
    // The other chord cap already owns the two axis-segment edges. Our
    // east edge should run X -> B(b_cap); our west edge B(a_cap) -> X.
    const ON_3dPoint our_east_end = boundary.PointAtEnd();
    const ON_3dPoint our_west_end = boundary.PointAtStart();
    const double tol = 1e-9 * (1.0 + our_east_end.DistanceTo(our_west_end));
    auto match = [&](ON_3dPoint p, int& out_edge, bool& out_rev, bool our_side_is_east) {
      if (p.DistanceTo(share_chord->east_end) <= tol) {
        out_edge = share_chord->east;
        // Their east edge runs X -> that point. Our east trim runs X -> point (same), our west trim runs point -> X (opposite).
        out_rev = !our_side_is_east;
      } else if (p.DistanceTo(share_chord->west_end) <= tol) {
        out_edge = share_chord->west;
        // Their west edge runs point -> X. Our east trim X -> point (opposite), our west trim point -> X (same).
        out_rev = our_side_is_east;
      } else {
        Internal(caller, "chord cap endpoints do not match the first cap's");
      }
    };
    match(our_east_end, eid[1], rev[1], true);
    match(our_west_end, eid[3], rev[3], false);
  }
  ON_BrepFace* face = brep.NewFace(cap.release(), vid, eid, rev);
  if (!face) Internal(caller, "ON_Brep::NewFace refused the cap face");
  CapEdges out;
  if (!transpose) {
    out.east = eid[1];
    out.west = eid[3];
    const ON_Interval dom = brep.m_S[face->m_si]->Domain(0);
    out.east_end = brep.m_S[face->m_si]->PointAt(dom.Max(), 1.0);
    out.west_end = brep.m_S[face->m_si]->PointAt(dom.Min(), 1.0);
  }
  return out;
}

// A hemispherical dome cap added to `brep`, sharing `shared_edge` -
// exactly the same "apex-like row at v = 0 (south, singular), boundary
// row at v = 1 (north, = `boundary` itself)" surface layout
// FanSurface()/AddFanCap() already use for a flat fan (see AddFanCap()'s
// own doc comment for the shared eid[2]/rev[2] wiring this reuses
// verbatim - that wiring is a fact about ON_Brep::NewFace()'s own side-
// index and reversal convention for a periodic-in-u untrimmed face,
// confirmed directly against opennurbs_brep_tools.cpp's own
// ON_Brep::NewOuterLoop(), and does not depend on whether the surface
// between those two rows is straight (FanSurface) or curved (this one).
//
// Unlike a flat fan's single apex, a dome needs its meridian - the curve
// from each point of `boundary` to the shared pole - to be an EXACT
// quarter circle of `spec.radius`, not a straight line, for the result
// to be a true sphere patch rather than a cone. This uses the SAME
// closed-form 3-control-point rational-quadratic representation of a
// 90-degree arc that ON_Circle::GetNurbForm() itself already relies on
// (control points at the arc's two ends plus the intersection of their
// two tangent lines, weights (1, cos(pi/4), 1)): for boundary CV i
// (Euclidean point B_i, homogeneous weight w_i), the meridian's three
// rows are
//   v=0 (pole):   center + radius*pole,        weight w_i
//   v=0.5 (corner): B_i + radius*pole,          weight w_i * cos(pi/4)
//   v=1 (equator): B_i (== boundary's own CV i), weight w_i
// - the tangent line at the equator point runs along `pole`'s own
// direction (the meridian there is perpendicular to the radius, in the
// plane the radius and `pole` span) and the tangent line at the pole
// runs along the radius direction there, so their intersection is
// exactly B_i + radius*pole, matching the standard construction.
//
// This is an EXACT hemisphere, not merely a fit, because expanding the
// tensor-product surface algebraically (using corner = equator +
// radius*pole and pole_cv = a FIXED point for every i, exactly
// FanSurface()'s own "same apex point, different homogeneous weight per
// column" trick) shows that for ANY fixed v, S(u, v) is a scaled and
// pole-translated copy of `boundary`(u) itself - i.e. every latitude
// line is an exactly scaled copy of the SAME curve `boundary` traces,
// which is a true circle here since `boundary` is Pipe()'s own
// ON_Circle::GetNurbForm() output (propagated, not re-derived, through
// the wall's own v=0/v=1 iso-curve - a NURBS skin's boundary rows
// reproduce their input section exactly, the same invariant every flat
// chord cap above already relies on via IsoCurveOf(wall, ...)).
//
// `outward_hint` plays the same role as AddFanCap()'s own outward hint,
// but the check here is numerical rather than closed-form: a flat fan's
// native normal direction follows analytically from the boundary's
// signed area about ONE plane, but a curved dome's true outward normal
// varies over the surface (purely radial at the equator itself, which
// is exactly why the equator is NOT where this checks - a radial vector
// is orthogonal to `outward_hint`'s own roughly-axial direction there,
// giving a useless near-zero dot product). A genuine mid-latitude point
// (u, v) = (domain midpoint, 0.5) always has a nonzero component along
// the pole direction (see this function's own git history for the
// derivation), so checking the sign there and rebuilding from a
// reversed `boundary` if it disagrees is both safe and sufficient - the
// same "try it, check the sign, flip if needed" rule AddFanCap() itself
// already applies, just decided numerically instead of in closed form.
void AddDomeCap(ON_Brep& brep, ON_NurbsCurve boundary, int shared_edge, bool edge_forward, ON_3dVector outward_hint,
                const Brep::RoundCapSpec& spec, const char* caller) {
  const int n = boundary.CVCount();
  const double corner_weight = std::sqrt(2.0) / 2.0;  // cos(pi/4): the standard 90-degree-arc corner weight
  const ON_3dPoint pole = spec.center + spec.radius * spec.pole;

  auto build = [&](const ON_NurbsCurve& b) {
    auto s = std::make_unique<ON_NurbsSurface>();
    if (!s->Create(3, /*is_rational=*/true, b.Order(), 3, n, 3)) Internal(caller, "Create failed");
    CopyKnots(b, *s, 0);
    s->SetKnot(1, 0, 0.0);
    s->SetKnot(1, 1, 0.0);
    s->SetKnot(1, 2, 1.0);
    s->SetKnot(1, 3, 1.0);
    for (int i = 0; i < n; ++i) {
      const ON_4dPoint h = HomogeneousCV(b, i);
      const ON_3dPoint equator = EuclideanCV(b, i);
      const ON_3dPoint corner = equator + spec.radius * spec.pole;
      s->SetCV(i, 0, ON_4dPoint(pole.x * h.w, pole.y * h.w, pole.z * h.w, h.w));
      s->SetCV(i, 1,
               ON_4dPoint(corner.x * h.w * corner_weight, corner.y * h.w * corner_weight,
                          corner.z * h.w * corner_weight, h.w * corner_weight));
      s->SetCV(i, 2, h);
    }
    if (!s->IsSingular(0)) Internal(caller, "the dome cap's pole side is not singular");
    return s;
  };

  std::unique_ptr<ON_NurbsSurface> cap = build(boundary);
  bool reversed = false;
  {
    ON_3dPoint p;
    ON_3dVector su, sv;
    if (!cap->Ev1Der(cap->Domain(0).Mid(), 0.5, p, su, sv)) {
      Internal(caller, "Ev1Der failed on a tentative dome cap");
    }
    const ON_3dVector normal = ON_CrossProduct(su, sv);
    if (normal.Length() <= 1e-12 * (1.0 + spec.radius)) {
      Internal(caller, "the dome cap's mid-latitude normal is degenerate");
    }
    if (ON_DotProduct(normal, outward_hint) < 0.0) {
      reversed = true;
      ReverseKeepDomain(boundary);
      cap = build(boundary);
    }
  }

  int vid[4] = {-1, -1, -1, -1};
  int eid[4] = {-1, -1, -1, -1};
  bool rev[4] = {false, false, false, false};
  eid[2] = shared_edge;
  rev[2] = edge_forward != reversed;
  ON_BrepFace* face = brep.NewFace(cap.release(), vid, eid, rev);
  if (!face) Internal(caller, "ON_Brep::NewFace refused the dome cap face");
}

}  // namespace

// See this method's own brep.h doc comment: a standalone one-face shell
// capping a single closed planar curve exactly, via the same star-shaped
// fan-apex search and fan surface (PlanCap()/FanSurface() just above)
// AssembleSweptBody()'s own end caps already use and verify. `boundary`
// is used exactly as given - no orientation reasoning is done here,
// unlike AddFanCap() (which must agree with an existing wall's own
// outward direction): this result shares no topology with anything else,
// so whichever of the two consistent fan orientations FanSurface()
// happens to build from `boundary`'s own parameter direction is fine -
// CapPlanarHoles() joins the result onto the original edges with
// JoinNakedEdges(), whose own UnifyNormals() pass fixes the sign against
// the rest of the shell afterward, the same "narrowing, not correctness,
// is this function's job" split CapPlanarHoles()'s existing straight-loop
// path already relies on for its own Newell-normal cap plane.
Brep Brep::CapClosedCurvedLoop(const ON_NurbsCurve& boundary_in) {
  const char* caller = "CapPlanarHoles";
  ON_NurbsCurve boundary = boundary_in;
  ClampIfPeriodic(boundary);
  if (!boundary.IsClosed()) Fail(caller, "cap loop curve is not closed");
  const CapPlan plan = PlanCap(boundary, /*chord_closed=*/false, caller);
  std::unique_ptr<ON_NurbsSurface> cap = FanSurface(boundary, plan.apex, caller);
  Brep result;
  ON_Brep& brep = result.raw();
  int vid[4] = {-1, -1, -1, -1};
  int eid[4] = {-1, -1, -1, -1};
  bool rev[4] = {false, false, false, false};
  if (!brep.NewFace(cap.release(), vid, eid, rev)) Internal(caller, "ON_Brep::NewFace refused the curved cap");
  brep.SetTolerancesBoxesAndFlags(/*bLazy=*/true);
  return result;
}

// Assembles the wall plus the requested caps. `cap_v0`/`cap_v1` cap the
// sweep's start/end (the wall's south/north sides); `cap_u0`/`cap_u1`
// cap the wall's west/east sides (a full revolve's off-axis end circles).
Brep Brep::AssembleSweptBody(ON_NurbsSurface* wall_raw, bool cap_v0, bool cap_v1, bool cap_u0, bool cap_u1,
                             const char* caller, const Vector3d* cap_v0_outward_hint,
                             const Vector3d* cap_v1_outward_hint, const RoundCapSpec* round_v0,
                             const RoundCapSpec* round_v1) {
  std::unique_ptr<ON_NurbsSurface> wall_in(wall_raw);
  Brep result;
  ON_Brep& brep = result.raw();
  const ON_NurbsSurface wall = *wall_in;  // keep an evaluable copy; the brep owns the original
  const bool closed_u = wall.IsClosed(0);
  const bool closed_v = wall.IsClosed(1);
  const bool sing[4] = {wall.IsSingular(0), wall.IsSingular(1), wall.IsSingular(2), wall.IsSingular(3)};

  int vid[4] = {-1, -1, -1, -1};
  int eid[4] = {-1, -1, -1, -1};
  bool rev[4] = {false, false, false, false};
  if (!brep.NewFace(wall_in.release(), vid, eid, rev)) Internal(caller, "ON_Brep::NewFace refused the wall");
  int faces = 1;

  const ON_Interval du = wall.Domain(0), dv = wall.Domain(1);
  const double um = du.Mid(), vm = dv.Mid();
  auto derivs = [&](double u, double v, ON_3dVector& su, ON_3dVector& sv) {
    ON_3dPoint p;
    if (!wall.Ev1Der(u, v, p, su, sv)) Internal(caller, "Ev1Der failed on the wall");
  };

  if (cap_v0 || cap_v1) {
    if (closed_v) Fail(caller, "cap requested on a periodic result, which has no ends");
    const bool chord = !closed_u;
    if (chord && (round_v0 || round_v1)) {
      Fail(caller, "a round cap requires a section that does not touch the axis");
    }
    if (chord && !(sing[1] && sing[3])) {
      Fail(caller, "cap requested but the section is open and its ends do not collapse to the axis");
    }
    if (sing[0] || sing[2]) Fail(caller, "cap requested on an end that collapses to a point");
    // Both caps must use ONE apex when they share the chord (axis) edges.
    std::unique_ptr<ON_NurbsCurve> b0(IsoCurveOf(wall, 0, dv.Min(), caller));
    std::unique_ptr<ON_NurbsCurve> b1(IsoCurveOf(wall, 0, dv.Max(), caller));
    CapPlan plan0, plan1;
    if (cap_v0 && !round_v0) plan0 = PlanCap(*b0, chord, caller);
    if (cap_v1 && !round_v1) plan1 = PlanCap(*b1, chord, caller);
    // Chord caps share the axis-segment edges and therefore the apex
    // vertex: the apex lies on the axis, which the sweep leaves fixed, so
    // plan1's own (independently found, rounding-different) apex is the
    // same point - use plan0's literally.
    if (chord) plan1.apex = plan0.apex;
    CapEdges first;
    bool have_first = false;
    if (cap_v0) {
      ON_3dVector hint;
      if (cap_v0_outward_hint) {
        hint = *cap_v0_outward_hint;
      } else {
        ON_3dVector su, sv;
        derivs(um, dv.Min(), su, sv);
        hint = -sv;
      }
      // Wall south edge runs +u (bRev3d[0] false): with the boundary.
      if (round_v0) {
        AddDomeCap(brep, *b0, eid[0], /*edge_forward=*/true, hint, *round_v0, caller);
      } else {
        first = AddFanCap(brep, *b0, eid[0], /*edge_forward=*/true, hint, plan0, nullptr, /*transpose=*/false, caller);
        have_first = true;
      }
      ++faces;
    }
    if (cap_v1) {
      ON_3dVector hint;
      if (cap_v1_outward_hint) {
        hint = *cap_v1_outward_hint;
      } else {
        ON_3dVector su, sv;
        derivs(um, dv.Max(), su, sv);
        hint = sv;
      }
      // Wall north edge: isocurve reversed at creation (i == 2, !bRev3d), so it runs -u: against the boundary.
      if (round_v1) {
        AddDomeCap(brep, *b1, eid[2], /*edge_forward=*/false, hint, *round_v1, caller);
      } else {
        AddFanCap(brep, *b1, eid[2], /*edge_forward=*/false, hint, plan1, (chord && have_first) ? &first : nullptr,
                  /*transpose=*/false, caller);
      }
      ++faces;
    }
  }
  if (cap_u0 || cap_u1) {
    if (!closed_v) Fail(caller, "a disc cap on a u-side needs the wall closed in v");
    if ((cap_u0 && sing[3]) || (cap_u1 && sing[1])) Fail(caller, "a disc cap was requested on a singular side");
    if (cap_u0) {
      std::unique_ptr<ON_NurbsCurve> b(IsoCurveOf(wall, 1, du.Min(), caller));
      CapPlan plan = PlanCap(*b, false, caller);
      ON_3dVector su, sv;
      derivs(du.Min(), vm, su, sv);
      // West edge: isocurve (+v) reversed at creation -> runs -v: against the boundary.
      AddFanCap(brep, *b, eid[3], /*edge_forward=*/false, -su, plan, nullptr, /*transpose=*/true, caller);
      ++faces;
    }
    if (cap_u1) {
      std::unique_ptr<ON_NurbsCurve> b(IsoCurveOf(wall, 1, du.Max(), caller));
      CapPlan plan = PlanCap(*b, false, caller);
      ON_3dVector su, sv;
      derivs(du.Max(), vm, su, sv);
      // East edge: isocurve (+v), not reversed -> runs +v: with the boundary.
      AddFanCap(brep, *b, eid[1], /*edge_forward=*/true, su, plan, nullptr, /*transpose=*/true, caller);
      ++faces;
    }
  }

  brep.SetTolerancesBoxesAndFlags(/*bLazy=*/true);
  result.AppendUntrimmedFaceSideTables(faces);

  // Outward cross-check for a closed body: the construction rules orient
  // it outward already; a negative tessellated volume here would mean a
  // rule was wrong for this input, and flipping is the safe repair.
  if (brep.IsSolid()) {
    const Mesh m = result.TessellateToClosedMesh(16, 16);
    if (m.Volume() < 0.0) brep.Flip();
  }
  return result;
}

namespace {

// Signed area of `c` (closed) about plane normal `n`, from a dense sampling.
double SignedAreaAbout(const ON_NurbsCurve& c, const ON_Plane& plane, int samples = 512) {
  const ON_Interval dom = c.Domain();
  std::vector<ON_2dPoint> poly;
  for (int i = 0; i < samples; ++i) {
    const ON_3dPoint p = c.PointAt(dom.ParameterAt(static_cast<double>(i) / samples));
    double x, y;
    plane.ClosestPointTo(p, &x, &y);
    poly.emplace_back(x, y);
  }
  return SignedArea2d(poly);
}

ON_3dPoint CvCentroid(const ON_NurbsCurve& c) {
  ON_3dPoint m(0, 0, 0);
  for (int i = 0; i < c.CVCount(); ++i) m += EuclideanCV(c, i);
  return m / static_cast<double>(c.CVCount());
}

// Rotation-minimizing frames (double reflection) at the rail points.
struct Frame {
  ON_3dPoint origin;
  ON_3dVector t, r, s;  // tangent, reference normal, binormal (r x t = s? we use s = t x r)
};

std::vector<Frame> RmfFrames(const ON_NurbsCurve& rail, const std::vector<double>& params, bool wrap,
                             const char* caller) {
  const int m = static_cast<int>(params.size());
  std::vector<Frame> frames(static_cast<size_t>(m));
  for (int k = 0; k < m; ++k) {
    frames[static_cast<size_t>(k)].origin = rail.PointAt(params[static_cast<size_t>(k)]);
    ON_3dVector t = rail.TangentAt(params[static_cast<size_t>(k)]);
    if (!t.Unitize()) Fail(caller, "the rail has a zero tangent at a station");
    frames[static_cast<size_t>(k)].t = t;
  }
  // Initial reference normal: the world axis least aligned with t0.
  {
    const ON_3dVector t0 = frames[0].t;
    ON_3dVector a = std::fabs(t0.x) <= std::fabs(t0.y) && std::fabs(t0.x) <= std::fabs(t0.z) ? ON_3dVector(1, 0, 0)
                    : std::fabs(t0.y) <= std::fabs(t0.z)                                    ? ON_3dVector(0, 1, 0)
                                                                                            : ON_3dVector(0, 0, 1);
    ON_3dVector r0 = a - t0 * ON_DotProduct(a, t0);
    r0.Unitize();
    frames[0].r = r0;
  }
  for (int k = 0; k + 1 < m; ++k) {
    const Frame& f = frames[static_cast<size_t>(k)];
    Frame& g = frames[static_cast<size_t>(k + 1)];
    const ON_3dVector v1 = g.origin - f.origin;
    const double c1 = ON_DotProduct(v1, v1);
    if (c1 <= 0.0) Fail(caller, "two consecutive rail stations coincide");
    const ON_3dVector rL = f.r - v1 * (2.0 / c1 * ON_DotProduct(v1, f.r));
    const ON_3dVector tL = f.t - v1 * (2.0 / c1 * ON_DotProduct(v1, f.t));
    const ON_3dVector v2 = g.t - tL;
    const double c2 = ON_DotProduct(v2, v2);
    ON_3dVector r = c2 > 0.0 ? rL - v2 * (2.0 / c2 * ON_DotProduct(v2, rL)) : rL;
    r = r - g.t * ON_DotProduct(r, g.t);  // re-orthogonalize against rounding
    r.Unitize();
    g.r = r;
  }
  if (wrap && m >= 2) {
    // Holonomy: transport one more step back to station 0 and measure the
    // angle between the transported normal and the starting one; spread
    // the correction linearly so the last frame meets the first.
    const Frame& f = frames.back();
    const Frame& g0 = frames.front();
    const ON_3dVector v1 = g0.origin - f.origin;
    const double c1 = ON_DotProduct(v1, v1);
    if (c1 <= 0.0) Fail(caller, "the closed rail's last station coincides with its first");
    const ON_3dVector rL = f.r - v1 * (2.0 / c1 * ON_DotProduct(v1, f.r));
    const ON_3dVector tL = f.t - v1 * (2.0 / c1 * ON_DotProduct(v1, f.t));
    const ON_3dVector v2 = g0.t - tL;
    const double c2 = ON_DotProduct(v2, v2);
    ON_3dVector r = c2 > 0.0 ? rL - v2 * (2.0 / c2 * ON_DotProduct(v2, rL)) : rL;
    r = r - g0.t * ON_DotProduct(r, g0.t);
    r.Unitize();
    const ON_3dVector s0 = ON_CrossProduct(g0.t, g0.r);
    const double twist = std::atan2(ON_DotProduct(r, s0), ON_DotProduct(r, g0.r));
    for (int k = 1; k < m; ++k) {
      Frame& fk = frames[static_cast<size_t>(k)];
      const double a = -twist * static_cast<double>(k) / m;
      const ON_3dVector sk = ON_CrossProduct(fk.t, fk.r);
      fk.r = fk.r * std::cos(a) + sk * std::sin(a);
    }
  }
  for (Frame& f : frames) f.s = ON_CrossProduct(f.t, f.r);
  return frames;
}

ON_Xform FrameToFrame(const Frame& from, const Frame& to) {
  const ON_Plane p0(from.origin, from.r, from.s);
  const ON_Plane p1(to.origin, to.r, to.s);
  ON_Xform xf;
  xf.Rotation(p0, p1);
  return xf;
}

}  // namespace

namespace {

// Two-rail sweep frame (see brep.h Sweep2()'s own doc comment): origin on
// rail1, x toward rail2, an orthonormal completion via the averaged rail
// tangent, and the rail-to-rail `width` a local profile is uniformly
// scaled by. `sweep_dir` (the averaged tangent used to build z) doubles
// as the "forward" direction Sweep1()'s own cap-orientation check uses
// frames[0].t for.
struct TwoRailFrame {
  ON_3dPoint origin;
  ON_3dVector x, y, z;
  ON_3dVector sweep_dir;
  double width = 0.0;
};

std::vector<TwoRailFrame> TwoRailFrames(const ON_NurbsCurve& rail1, const ON_NurbsCurve& rail2,
                                        const std::vector<double>& p1, const std::vector<double>& p2,
                                        const char* caller) {
  const int m = static_cast<int>(p1.size());
  if (static_cast<int>(p2.size()) != m) Internal(caller, "rail station count mismatch");
  const double scale = 1.0 + CurveScale(rail1) + CurveScale(rail2);
  std::vector<TwoRailFrame> frames(static_cast<size_t>(m));
  for (int k = 0; k < m; ++k) {
    TwoRailFrame& f = frames[static_cast<size_t>(k)];
    f.origin = rail1.PointAt(p1[static_cast<size_t>(k)]);
    const ON_3dPoint q = rail2.PointAt(p2[static_cast<size_t>(k)]);
    ON_3dVector x = q - f.origin;
    f.width = x.Length();
    if (f.width <= 1e-9 * scale) {
      Fail(caller, "the two rails touch (zero separation) at a station - Sweep2 does not support this");
    }
    x = x / f.width;
    ON_3dVector t1 = rail1.TangentAt(p1[static_cast<size_t>(k)]);
    ON_3dVector t2 = rail2.TangentAt(p2[static_cast<size_t>(k)]);
    if (!t1.Unitize() || !t2.Unitize()) Fail(caller, "a rail has a zero tangent at a station");
    ON_3dVector t = t1 + t2;
    if (!t.Unitize()) t = t1;  // exactly opposite rail tangents: fall back to rail1's own direction
    ON_3dVector z = ON_CrossProduct(x, t);
    if (!z.Unitize()) {
      Fail(caller,
           "a rail's tangent is exactly parallel to the rail-to-rail direction at a station - the two-rail "
           "frame is undefined there");
    }
    f.x = x;
    f.z = z;
    f.y = ON_CrossProduct(z, x);
    f.sweep_dir = t;
  }
  return frames;
}

}  // namespace

namespace {

// Exact, closed-form in-plane offset of a CONVEX degree-1 polyline (open
// or closed, non-rational, at least 3 distinct vertices, not reducible to
// a single line or arc - callers filter for that) by `distance`, along
// the same "edge tangent x plane.zaxis" convention NurbsCurve::
// OffsetInPlane()'s own Line/Arc cases use (distance > 0 grows).
//
// Every vertex gets the exact planar MITER-JOIN point: for the two unit
// offset directions n0 (incoming edge) and n1 (outgoing edge) meeting at
// a vertex,
//     V' = V + distance * (n0 + n1) / (1 + n0 . n1)
// - the standard closed-form polygon-offset miter point, derivable
// directly: the bisector of n0 and n1 makes half-angle theta/2 with
// each, where cos(theta) = n0 . n1; |n0 + n1| = 2 cos(theta/2) (both
// unit vectors); and reaching perpendicular distance `distance` from
// EACH edge along that bisector needs a further 1 / cos(theta/2) - the
// two factors combine to 1 / (2 cos^2(theta/2)) = 1 / (1 + cos(theta)) =
// 1 / (1 + n0 . n1). An open polyline's two end vertices have only one
// adjacent edge and just translate by `distance * n` along it - exactly
// OffsetInPlane()'s own Line case.
//
// Deliberately restricted to CONVEX input (checked here first; throws
// otherwise) - the same scope this kernel's polygon machinery already
// draws elsewhere (PlanCap()'s star-shaped-only cap above, fillet.h's
// "concave/degenerate edges are out of scope"). The restriction earns
// something concrete in return: for a convex polygon offset uniformly, a
// cheap and EXACT sufficient validity check exists and is applied
// unconditionally below - every offset edge must stay a POSITIVE
// multiple of its own original direction. That this is sufficient is a
// direct consequence of convexity, not merely plausible: walking a
// convex polygon's edges in order turns monotonically in ONE angular
// direction by a total of exactly 2*pi; each edge's own supporting line
// moves outward (grow) or inward (shrink) by the same `distance` without
// changing its DIRECTION (a translated line is still parallel to
// itself), so the new edges still turn monotonically the same way by the
// same total 2*pi PROVIDED none of them inverted - which is exactly what
// the check confirms. A monotonically-turning closed polygon with no
// inverted edge is convex and simple by construction (it cannot cross
// itself: crossing would require an edge to double back, i.e. invert,
// somewhere). A general (possibly concave) polygon has no such
// guarantee - its offset can self-intersect far from any single corner -
// which is exactly the "Offset self-intersection / invalid-loop removal"
// gap this kernel discloses in PARITY_MAP.md ("Offsetting, shelling,
// thickening"); that harder problem is not attempted here.
ON_NurbsCurve OffsetConvexPolyline(const ON_NurbsCurve& c, const ON_Plane& plane, double distance,
                                   const char* caller) {
  const bool closed = c.IsClosed();
  const int cv_count = c.CVCount();
  const int vcount = closed ? cv_count - 1 : cv_count;
  if (vcount < 3) Internal(caller, "OffsetConvexPolyline needs at least 3 distinct vertices");
  std::vector<ON_3dPoint> v(static_cast<size_t>(vcount));
  for (int i = 0; i < vcount; ++i) v[static_cast<size_t>(i)] = EuclideanCV(c, i);

  const int edge_count = closed ? vcount : vcount - 1;
  std::vector<ON_3dVector> edir(static_cast<size_t>(edge_count)), ndir(static_cast<size_t>(edge_count));
  for (int i = 0; i < edge_count; ++i) {
    ON_3dVector d = v[static_cast<size_t>((i + 1) % vcount)] - v[static_cast<size_t>(i)];
    if (!d.Unitize()) Fail(caller, "the profile has a zero-length edge");
    edir[static_cast<size_t>(i)] = d;
    ON_3dVector n = ON_CrossProduct(d, plane.zaxis);
    if (!n.Unitize()) Internal(caller, "degenerate edge offset direction");
    ndir[static_cast<size_t>(i)] = n;
  }

  // Convexity: every turn (consecutive edge pair; wrapping for a closed
  // polyline, interior vertices only for an open one) must have the SAME
  // sign of cross product about plane.zaxis - a dimensionless quantity
  // (both edir entries are unit vectors), so a small absolute tolerance
  // is the right kind of tolerance here, not a scaled one. Collinear
  // (near-zero) turns are allowed either way.
  {
    double sign = 0.0;
    const int turns = closed ? edge_count : edge_count - 1;
    for (int i = 0; i < turns; ++i) {
      const ON_3dVector& a = edir[static_cast<size_t>(i)];
      const ON_3dVector& b = edir[static_cast<size_t>((i + 1) % edge_count)];
      const double cross = ON_DotProduct(ON_CrossProduct(a, b), plane.zaxis);
      if (std::fabs(cross) <= 1e-9) continue;
      const double this_sign = cross > 0.0 ? 1.0 : -1.0;
      if (sign == 0.0) {
        sign = this_sign;
      } else if (this_sign != sign) {
        Fail(caller,
             "a draft-angle extrusion of a multi-segment profile needs a CONVEX polygon - this one turns both "
             "ways (a reflex corner), which risks a self-intersecting offset this kernel does not detect/repair "
             "for general polygons (see PARITY_MAP.md's disclosed offset self-intersection gap)");
      }
    }
  }

  std::vector<ON_3dPoint> out(static_cast<size_t>(vcount));
  for (int i = 0; i < vcount; ++i) {
    if (!closed && i == 0) {
      out[0] = v[0] + distance * ndir[0];
      continue;
    }
    if (!closed && i == vcount - 1) {
      out[static_cast<size_t>(i)] = v[static_cast<size_t>(i)] + distance * ndir[static_cast<size_t>(edge_count - 1)];
      continue;
    }
    const ON_3dVector& n0 = ndir[static_cast<size_t>((i - 1 + edge_count) % edge_count)];
    const ON_3dVector& n1 = ndir[static_cast<size_t>(i % edge_count)];
    const double denom = 1.0 + ON_DotProduct(n0, n1);
    if (denom <= 1e-9) {
      Fail(caller, "the profile folds back on itself at a near-180-degree corner - no finite miter offset exists there");
    }
    out[static_cast<size_t>(i)] = v[static_cast<size_t>(i)] + (distance / denom) * (n0 + n1);
  }

  // Validity: every offset edge must be a positive multiple of its own
  // original direction (see this function's own doc comment for why
  // that is a sufficient simplicity proof for a convex input).
  const double length_floor = 1e-12 * CurveScale(c);
  for (int i = 0; i < edge_count; ++i) {
    const ON_3dVector e = out[static_cast<size_t>((i + 1) % vcount)] - out[static_cast<size_t>(i)];
    if (ON_DotProduct(e, edir[static_cast<size_t>(i)]) <= length_floor) {
      Fail(caller,
           "the draft angle/height shrinks the profile past its own inradius - an edge would invert or collapse; "
           "use a smaller draft angle, a shorter extrusion, or a larger profile");
    }
  }

  std::vector<Point3d> pts(out.begin(), out.end());
  if (closed) pts.push_back(out.front());
  return NurbsCurve::FromControlPoints(pts, 1).raw();
}

}  // namespace

// ---------------------------------------------------------------------------
// Public factories.
// ---------------------------------------------------------------------------

Brep Brep::Extrude(const NurbsCurve& profile, Vector3d direction, bool cap) {
  const char* caller = "Extrude";
  const double L = direction.Length();
  if (!(L > 0.0)) Fail(caller, "direction must be non-zero (its length is the extrusion distance)");
  ON_NurbsCurve c = profile.raw();
  if (!c.IsValid()) Fail(caller, "profile is not a valid NURBS curve");
  ClampIfPeriodic(c);
  const bool closed = c.IsClosed();
  const bool want_caps = cap && closed;
  if (want_caps) {
    ON_Plane plane;
    if (!c.IsPlanar(&plane, 1e-8 * CurveScale(c))) {
      Fail(caller, "cap requested but the closed profile is not planar - cannot cap it");
    }
    const double along = ON_DotProduct(plane.zaxis, direction) / L;
    if (std::fabs(along) <= 1e-9) Fail(caller, "direction lies in the profile's plane - the extrusion is flat");
    // Outward wall: the profile must run counterclockwise about the
    // extrusion direction (normal = tangent x direction points out).
    const ON_3dVector d_unit = direction / L;
    ON_Plane about_d(plane.origin, d_unit);
    if (SignedAreaAbout(c, about_d) < 0.0) ReverseKeepDomain(c);
  }
  ON_NurbsCurve c1 = c;
  c1.Translate(direction);
  std::unique_ptr<ON_NurbsSurface> wall = RuledBetween(c, c1, 0.0, L, caller);
  return AssembleSweptBody(wall.release(), want_caps, want_caps, false, false, caller);
}

Brep Brep::ExtrudeAlongCurve(const NurbsCurve& profile, const NurbsCurve& path, bool cap) {
  const char* caller = "ExtrudeAlongCurve";
  ON_NurbsCurve c = profile.raw();
  if (!c.IsValid()) Fail(caller, "profile is not a valid NURBS curve");
  if (c.IsRational()) {
    Fail(caller, "profile must be a non-rational NURBS curve - the exact tensor-product sum construction has no "
                 "rational form (see this function's own brep.h doc comment)");
  }
  ClampIfPeriodic(c);

  ON_NurbsCurve path_c = path.raw();
  if (!path_c.IsValid()) Fail(caller, "path is not a valid NURBS curve");
  if (path_c.IsRational()) {
    Fail(caller, "path must be a non-rational NURBS curve - the exact tensor-product sum construction has no "
                 "rational form (see this function's own brep.h doc comment)");
  }
  if (path_c.IsClosed()) {
    Fail(caller, "path must be an open curve - a closed path has no well-defined start/end displacement to "
                 "translate the profile by, and the resulting wall would need periodic capping this does not attempt");
  }
  ClampIfPeriodic(path_c);

  const ON_3dPoint path_start = path_c.PointAtStart();
  const ON_3dPoint path_end = path_c.PointAtEnd();
  const double L = path_start.DistanceTo(path_end);
  if (!(L > 0.0)) Fail(caller, "the path starts and ends at the same point - the extrusion is zero-length");

  const bool closed = c.IsClosed();
  const bool want_caps = cap && closed;
  if (want_caps) {
    ON_Plane plane;
    if (!c.IsPlanar(&plane, 1e-8 * CurveScale(c))) {
      Fail(caller, "cap requested but the closed profile is not planar - cannot cap it");
    }
    // Same flatness/orientation convention as Extrude(), but against the
    // path's own NET displacement (start to end) rather than a fixed
    // direction - the two caps sit in planes parallel to `profile`'s own,
    // offset by that same net vector, regardless of how the path wanders
    // in between (this sweep never rotates the profile).
    const ON_3dVector net_unit = (path_end - path_start) / L;
    if (std::fabs(ON_DotProduct(plane.zaxis, net_unit)) <= 1e-9) {
      Fail(caller, "the path's net displacement lies in the profile's plane - the extrusion is flat");
    }
    ON_Plane about_net(plane.origin, net_unit);
    if (SignedAreaAbout(c, about_net) < 0.0) ReverseKeepDomain(c);
  }

  std::unique_ptr<ON_NurbsSurface> wall = SumSurface(c, path_c, caller);
  return AssembleSweptBody(wall.release(), want_caps, want_caps, false, false, caller);
}

Brep Brep::ExtrudeTapered(const NurbsCurve& profile, Vector3d direction, double draft_angle, bool cap) {
  const char* caller = "ExtrudeTapered";
  const double L = direction.Length();
  if (!(L > 0.0)) Fail(caller, "direction must be non-zero (its length is the extrusion distance)");
  if (!ON_IsValid(draft_angle) || !(draft_angle > -0.5 * ON_PI && draft_angle < 0.5 * ON_PI)) {
    Fail(caller, "draft_angle must be a finite value strictly between -pi/2 and pi/2 radians");
  }
  if (draft_angle == 0.0) return Extrude(profile, direction, cap);

  ON_NurbsCurve c = profile.raw();
  if (!c.IsValid()) Fail(caller, "profile is not a valid NURBS curve");
  ClampIfPeriodic(c);
  ON_Plane plane;
  if (!c.IsPlanar(&plane, 1e-8 * CurveScale(c))) {
    Fail(caller, "profile is not planar - a draft angle needs a well-defined in-plane offset direction");
  }
  const ON_3dVector d_unit = direction / L;
  const double along = ON_DotProduct(plane.zaxis, d_unit);
  if (std::fabs(along) <= 1e-9) {
    Fail(caller, "direction lies in the profile's own plane - the extrusion is flat, with no well-defined "
                 "extrusion axis to measure the draft angle against");
  }

  // Positive draft_angle SHRINKS the profile moving along +direction (see
  // this function's own brep.h doc comment for the convention and why
  // the sign here is the negative of L * tan(draft_angle)). This formula
  // is unchanged for an OBLIQUE `direction` (one not parallel to the
  // profile's own fitted plane normal, `along` above anywhere short of
  // +-1): the in-plane offset magnitude is scaled by the FULL travel
  // distance L, exactly as it already is in the parallel case, so the
  // result stays invariant to whichever of the two equally-valid signs
  // `IsPlanar()` happens to fit the profile's own normal to (see this
  // function's own "same draft_angle gives the same shrinking frustum
  // regardless of sign" test) - the oblique component of `direction`
  // shows up only in the `top_raw.Translate(direction)` below, as a pure
  // shear the in-plane offset math never sees. The resulting solid is a
  // genuine oblique (sheared) frustum: by Cavalieri's principle its
  // cross-sectional area at a given PERPENDICULAR distance from the
  // profile's own plane depends only on that distance (both end curves
  // are similarity-scaled, translated copies of the same profile, linearly
  // interpolated), so the untapered/undrafted closed-form frustum volume
  // still holds when measured against the along-normal component of
  // `direction`, not its full oblique length.
  const double offset_distance = -L * std::tan(draft_angle);

  NurbsCurve top;
  const bool is_line_or_arc = c.IsLinear(1e-9 * CurveScale(c)) || c.IsArc(nullptr, nullptr, 1e-9 * CurveScale(c));
  if (c.Degree() == 1 && !c.IsRational() && !is_line_or_arc) {
    // A genuine multi-segment polyline: this kernel's own exact convex
    // miter offset, not OffsetInPlane()'s general least-squares branch
    // (see ExtrudeTapered()'s own brep.h doc comment for why).
    top.raw() = OffsetConvexPolyline(c, plane, offset_distance, caller);
  } else {
    NurbsCurve profile_wrapped;
    profile_wrapped.raw() = c;
    if (profile_wrapped.OffsetInPlane(offset_distance, top) != Result::Ok) {
      Fail(caller,
           "the draft angle/extrusion height is too large for this profile - the offset curve would "
           "self-intersect or fold through its own center of curvature");
    }
  }
  ON_NurbsCurve top_raw = top.raw();
  top_raw.Translate(direction);
  NurbsCurve top_translated;
  top_translated.raw() = top_raw;

  return Loft({profile, top_translated}, 1, /*closed=*/false, cap);
}

Brep Brep::ExtrudeFace(const Brep& body, int face_index, Vector3d direction, bool cap) {
  const char* caller = "ExtrudeFace";
  const double L = direction.Length();
  if (!(L > 0.0)) Fail(caller, "direction must be non-zero (its length is the extrusion distance)");
  const ON_Brep& src = body.raw();
  if (face_index < 0 || face_index >= src.m_F.Count()) {
    Fail(caller, "face_index " + std::to_string(face_index) + " is out of range (this Brep has " +
                     std::to_string(src.m_F.Count()) + " face slot(s))");
  }
  const ON_BrepFace& face = src.m_F[face_index];
  if (face.m_face_index < 0) {
    Fail(caller, "face_index " + std::to_string(face_index) + " refers to a deleted face");
  }
  const bool trim_table_ok = body.face_trim_loops_.size() == static_cast<size_t>(src.m_F.Count()) &&
                              body.face_hole_loops_.size() == static_cast<size_t>(src.m_F.Count());
  if (!trim_table_ok || !body.face_trim_loops_[static_cast<size_t>(face_index)].empty() ||
      !body.face_hole_loops_[static_cast<size_t>(face_index)].empty()) {
    Fail(caller,
         "face_index's face must be untrimmed (e.g. a face built by FromSurface()/Box()/Extrude() itself) - a "
         "trimmed face's real boundary is not its surface's 4 domain isocurves, and this Brep's own trim side "
         "tables either are not populated or record a real trim/hole loop for this face");
  }
  const ON_Surface* raw_surface = face.SurfaceOf();
  if (raw_surface == nullptr) Internal(caller, "face has no surface");
  ON_NurbsSurface near_surf;
  if (!raw_surface->GetNurbForm(near_surf)) {
    Fail(caller, "face's surface could not be converted to an exact NURBS form");
  }
  if (near_surf.IsClosed(0) || near_surf.IsClosed(1)) {
    Fail(caller,
         "the face's surface must be open (non-periodic) in both parametric directions - a fully or partially "
         "closed face (e.g. a full cylinder, sphere, or torus patch) needs a variable side-wall count this "
         "scoped version does not attempt");
  }

  ON_NurbsSurface far_surf = near_surf;
  far_surf.Translate(direction);

  const ON_Interval du = near_surf.Domain(0), dv = near_surf.Domain(1);

  Brep result;
  ON_Brep& brep = result.raw();
  int faces = 0;

  if (cap) {
    // Same "verify, don't assume" outward convention every sweep factory
    // in this file ends with: build one cap reversed and the other not,
    // then let the tessellated-volume sign check below correct a wrong
    // guess for this particular face's own (arbitrary) parametrization.
    auto add_cap = [&](const ON_NurbsSurface& s, bool brev) {
      auto* copy = new ON_NurbsSurface(s);
      const int si = brep.AddSurface(copy);
      ON_BrepFace& f = brep.NewFace(si);
      f.m_bRev = brev;
    };
    add_cap(near_surf, /*brev=*/true);
    add_cap(far_surf, /*brev=*/false);
    faces += 2;
  }

  // Four side walls, one per edge of the domain rectangle - identical
  // construction to Thicken()'s own add_wall lambda (see its own doc
  // comment for the CCW-as-seen-from-`far`-side walk convention that
  // makes Sw_u x Sw_v point outward).
  auto add_wall = [&](int dir, double param, bool reverse_iso) {
    std::unique_ptr<ON_NurbsCurve> c_near(IsoCurveOf(near_surf, dir, param, caller));
    std::unique_ptr<ON_NurbsCurve> c_far(IsoCurveOf(far_surf, dir, param, caller));
    if (reverse_iso) {
      ReverseKeepDomain(*c_near);
      ReverseKeepDomain(*c_far);
    }
    std::unique_ptr<ON_NurbsSurface> wall = RuledBetween(*c_near, *c_far, 0.0, 1.0, caller);
    const int si = brep.AddSurface(wall.release());
    brep.NewFace(si);
    ++faces;
  };
  add_wall(0, dv.Min(), /*reverse_iso=*/false);  // v = v_min, walked +u
  add_wall(1, du.Max(), /*reverse_iso=*/false);  // u = u_max, walked +v
  add_wall(0, dv.Max(), /*reverse_iso=*/true);   // v = v_max, walked -u
  add_wall(1, du.Min(), /*reverse_iso=*/true);   // u = u_min, walked -v

  brep.SetTrimIsoFlags();
  result.AppendUntrimmedFaceSideTables(faces);

  if (cap && brep.IsSolid()) {
    // Outward-orientation safety net, the same one Thicken()/
    // AssembleSweptBody() themselves use.
    const Mesh check = result.TessellateToClosedMesh(16, 16);
    if (check.Volume() < 0.0) brep.Flip();
  }

  return result;
}

namespace {

// Walks `wire_body`'s own edge/vertex graph into one ordered, fully-
// connected chain of edge indices - shared machinery for
// Brep::ExtrudeWireBody() below. Returns an empty vector to signal
// refusal (a branch point, or more than one disjoint wire component) -
// never ambiguous with success, since a genuine IsWireBody() Brep always
// has at least one live edge, so a successful walk is never itself
// empty.
std::vector<int> WalkWireChain(const Brep& wire_body) {
  const ON_Brep& b = wire_body.raw();

  std::vector<int> live_edges;
  for (int ei = 0; ei < b.m_E.Count(); ++ei) {
    if (b.m_E[ei].m_edge_index >= 0) live_edges.push_back(ei);
  }
  if (live_edges.empty()) return {};

  // A vertex touching 3+ live edges is a branch point - refused up
  // front, before any walk is attempted. A degree-1 vertex is one of
  // (at most two) chain ends; a self-closed edge's own single vertex
  // lists that edge twice (see KillEdgeVertex()'s own doc comment), so
  // it reports degree 2, correctly routing it into the "closed loop"
  // case below rather than being mistaken for a chain end.
  std::vector<int> leaves;
  for (int vi = 0; vi < b.m_V.Count(); ++vi) {
    if (b.m_V[vi].m_vertex_index < 0) continue;
    const int degree = static_cast<int>(wire_body.EdgesOfVertex(vi).size());
    if (degree == 0) continue;  // an unrelated, untouched vertex slot
    if (degree > 2) return {};
    if (degree == 1) leaves.push_back(vi);
  }
  // Exactly 0 (one or more closed loops) or exactly 2 (one open chain)
  // ends are structurally sound so far; anything else (e.g. 4, from two
  // disjoint open chains) is refused here without even attempting a
  // walk. Whether a lone open chain plus a separate closed loop (0 + 2 =
  // 2 leaves, structurally passing this check) is genuinely one
  // component is caught below instead, by the walk not covering every
  // live edge.
  if (!leaves.empty() && leaves.size() != 2) return {};

  const int start_vertex = leaves.empty() ? b.m_E[live_edges.front()].m_vi[0] : leaves.front();

  std::vector<int> order;
  std::vector<bool> visited(static_cast<size_t>(b.m_E.Count()), false);
  int current = start_vertex;
  while (order.size() < live_edges.size()) {
    int next_edge = -1;
    for (const int ei : wire_body.EdgesOfVertex(current)) {
      if (!visited[static_cast<size_t>(ei)]) {
        next_edge = ei;
        break;
      }
    }
    if (next_edge < 0) break;  // chain end reached
    visited[static_cast<size_t>(next_edge)] = true;
    order.push_back(next_edge);
    const ON_BrepEdge& e = b.m_E[next_edge];
    current = (e.m_vi[0] == current) ? e.m_vi[1] : e.m_vi[0];
  }
  // A short walk means a branch was taken that didn't reach every live
  // edge (e.g. the lone-open-chain-plus-closed-loop case above) - a
  // disjoint second component this function refuses rather than guesses
  // which one the caller meant.
  if (order.size() != live_edges.size()) return {};
  return order;
}

}  // namespace

Brep Brep::ExtrudeWireBody(const Brep& wire_body, Vector3d direction, bool cap) {
  const char* caller = "ExtrudeWireBody";
  if (!(direction.Length() > 0.0)) Fail(caller, "direction must be non-zero (its length is the extrusion distance)");
  if (!wire_body.IsWireBody()) {
    Fail(caller, "wire_body must satisfy IsWireBody() (at least one live edge, zero live faces)");
  }
  const std::vector<int> chain = WalkWireChain(wire_body);
  if (chain.empty()) {
    Fail(caller,
         "wire_body's own edge graph must be a single simple open chain or closed loop - a branch point (a "
         "vertex touching 3 or more edges) or more than one disjoint wire component is out of scope");
  }

  const ON_Brep& b = wire_body.raw();
  auto edge_curve = [&](int edge_index) {
    ON_NurbsCurve nc;
    if (b.m_E[edge_index].GetNurbForm(nc) <= 0) {
      Internal(caller, "a wire edge's own curve could not be converted to an exact NURBS form");
    }
    NurbsCurve c;
    c.raw() = nc;
    return c;
  };

  NurbsCurve profile = edge_curve(chain.front());
  for (size_t k = 1; k < chain.size(); ++k) {
    NurbsCurve next = edge_curve(chain[k]);
    // The walk above only ever follows edges sharing a real ON_BrepVertex,
    // so `next` is always genuinely meant to continue `profile` - measure
    // the actual gap (rather than assuming WireBody()'s/AddWireCurves()'
    // own possibly-looser caller-chosen weld tolerance matches Join()'s
    // fixed 1e-6 default) and pass a tolerance comfortably above it, so
    // Join()'s own point-matching check can't spuriously refuse a
    // topologically genuine connection.
    const Point3d end = profile.PointAt(profile.Domain().max);
    const Interval nd = next.Domain();
    const double gap = std::min(end.DistanceTo(next.PointAt(nd.min)), end.DistanceTo(next.PointAt(nd.max)));
    const double join_tolerance = std::max(tolerance::kDistance, 2.0 * gap + tolerance::kDistance);
    if (profile.Join(next, join_tolerance) != Result::Ok) {
      Internal(caller, "the wire body's own edge curves failed to join into one continuous profile despite "
                       "sharing a vertex - a wire-body invariant this function relies on was violated");
    }
  }

  return Extrude(profile, direction, cap);
}

Brep Brep::OffsetWireBody(const Brep& wire_body, double distance, double tolerance) {
  const char* caller = "OffsetWireBody";
  if (!wire_body.IsWireBody()) {
    Fail(caller, "wire_body must satisfy IsWireBody() (at least one live edge, zero live faces)");
  }
  const std::vector<int> chain = WalkWireChain(wire_body);
  if (chain.empty()) {
    Fail(caller,
         "wire_body's own edge graph must be a single simple open chain or closed loop - a branch point (a "
         "vertex touching 3 or more edges) or more than one disjoint wire component is out of scope");
  }

  const ON_Brep& b = wire_body.raw();
  auto edge_curve = [&](int edge_index) {
    ON_NurbsCurve nc;
    if (b.m_E[edge_index].GetNurbForm(nc) <= 0) {
      Internal(caller, "a wire edge's own curve could not be converted to an exact NURBS form");
    }
    NurbsCurve c;
    c.raw() = nc;
    return c;
  };

  NurbsCurve profile = edge_curve(chain.front());
  for (size_t k = 1; k < chain.size(); ++k) {
    NurbsCurve next = edge_curve(chain[k]);
    // Same actual-gap-driven join tolerance ExtrudeWireBody() uses above,
    // for the same reason: don't assume Join()'s fixed 1e-6 default
    // matches whatever caller-chosen weld tolerance WireBody()/
    // AddWireCurves() built the wire body with.
    const Point3d end = profile.PointAt(profile.Domain().max);
    const Interval nd = next.Domain();
    const double gap = std::min(end.DistanceTo(next.PointAt(nd.min)), end.DistanceTo(next.PointAt(nd.max)));
    const double join_tolerance = std::max(tolerance::kDistance, 2.0 * gap + tolerance::kDistance);
    if (profile.Join(next, join_tolerance) != Result::Ok) {
      Internal(caller, "the wire body's own edge curves failed to join into one continuous profile despite "
                       "sharing a vertex - a wire-body invariant this function relies on was violated");
    }
  }

  NurbsCurve offset;
  if (profile.OffsetInPlane(distance, offset, tolerance) != Result::Ok) {
    Fail(caller, "the wire body's own joined profile could not be offset - it is not planar within tolerance, "
                 "or the requested distance folds it through itself or through its own center of curvature");
  }

  return WireBody({offset}, tolerance::kDistance);
}

Brep Brep::Thicken(const Brep& sheet, double thickness, bool symmetric) {
  const char* caller = "Thicken";
  if (!std::isfinite(thickness) || thickness == 0.0) {
    Fail(caller, "thickness must be finite and non-zero");
  }
  const ON_Brep& src = sheet.raw();
  if (src.m_F.Count() != 1) {
    Fail(caller, "sheet must be a single-face body - a multi-face shell thicken is a separate, disclosed gap");
  }
  if (!sheet.face_trim_loops_.empty() &&
      (!sheet.face_trim_loops_[0].empty() || !sheet.face_hole_loops_[0].empty())) {
    Fail(caller,
         "sheet's face must be untrimmed (e.g. built by Brep::FromSurface()) - a trimmed sheet's real boundary "
         "is not its surface's 4 domain isocurves");
  }
  const ON_BrepFace& face = src.m_F[0];
  const ON_Surface* raw_surface = face.SurfaceOf();
  if (raw_surface == nullptr) Internal(caller, "sheet's face has no surface");
  ON_NurbsSurface base;
  if (!raw_surface->GetNurbForm(base)) {
    Fail(caller, "sheet's surface could not be converted to an exact NURBS form");
  }
  if (base.IsClosed(0) || base.IsClosed(1)) {
    Fail(caller,
         "the surface must be open (non-periodic) in both parametric directions - a fully or partially closed "
         "sheet (e.g. a full cylinder, sphere, or torus patch) needs a variable side-wall count this scoped "
         "version does not attempt");
  }

  NurbsSurface original;
  original.raw() = base;

  // `lo`/`hi`: the two caps, always assigned so the solid lies on the +N
  // side of `lo` and the -N side of `hi`, N = `original`'s own NormalAt()
  // direction (OffsetApproximate()'s own translation convention) - see
  // this function's own brep.h doc comment for why OffsetApproximate(),
  // not OffsetAnalytic(), is what makes `lo`/`hi` boundary-compatible.
  NurbsSurface lo, hi;
  if (symmetric) {
    const double half = std::fabs(thickness) * 0.5;
    if (original.OffsetApproximate(-half, lo) != Result::Ok || original.OffsetApproximate(half, hi) != Result::Ok) {
      Fail(caller,
           "the requested thickness is too large for this surface's own curvature - the offset would fold "
           "through its own center of curvature (see NurbsSurface::OffsetApproximate's own guard)");
    }
  } else {
    NurbsSurface offset;
    if (original.OffsetApproximate(thickness, offset) != Result::Ok) {
      Fail(caller,
           "the requested thickness is too large for this surface's own curvature - the offset would fold "
           "through its own center of curvature (see NurbsSurface::OffsetApproximate's own guard)");
    }
    if (thickness > 0.0) {
      lo = original;
      hi = offset;
    } else {
      lo = offset;
      hi = original;
    }
  }

  const ON_NurbsSurface& lo_raw = lo.raw();
  const ON_NurbsSurface& hi_raw = hi.raw();
  const ON_Interval du = lo_raw.Domain(0), dv = lo_raw.Domain(1);

  Brep result;
  ON_Brep& brep = result.raw();

  // Two caps: `lo`'s own natural (Su x Sv) normal points toward `hi` - INTO
  // the solid - so it needs reversing to face outward; `hi`'s own natural
  // normal already points away from `lo` - away from the solid - and needs
  // no reversal. (Verified by hand for a flat XY sheet with N = +Z in this
  // function's own tests, then trusted for the curved case since
  // OffsetApproximate()'s own curvature-fold guard is exactly what
  // prevents the local normal direction from flipping between `lo` and
  // `hi` - a fold is precisely a normal reversal, and that guard already
  // refuses any offset that would cause one.)
  auto add_cap = [&](const ON_NurbsSurface& s, bool brev) {
    auto* copy = new ON_NurbsSurface(s);
    const int si = brep.AddSurface(copy);
    ON_BrepFace& f = brep.NewFace(si);
    f.m_bRev = brev;
  };
  add_cap(lo_raw, /*brev=*/true);
  add_cap(hi_raw, /*brev=*/false);

  // Four side walls, one per edge of the domain rectangle: the exact
  // degree-1-in-v ruled surface (RuledBetween()) between that edge's
  // isocurve on `lo` and the SAME edge's isocurve on `hi` - compatible by
  // construction (OffsetApproximate() never changes the control-point grid
  // or knot vectors, only moves the control points, so `lo`/`hi` share
  // identical knot vectors/CV counts and so do their corresponding
  // isocurves). Walked CCW as seen from the `hi` (outward, N) side, the
  // same convention Extrude() itself relies on for why a CCW boundary here
  // makes Sw_u x Sw_v point outward: v=v_min forward in u, u=u_max forward
  // in v, v=v_max backward in u, u=u_min backward in v - the standard
  // positively-oriented parameter-rectangle loop, already used the same
  // way for a curved-face cap's own rectangle loop elsewhere in this
  // codebase (see FromMixedFaces' own doc comment in brep.cpp).
  auto add_wall = [&](int dir, double param, bool reverse_iso) {
    std::unique_ptr<ON_NurbsCurve> c_lo(IsoCurveOf(lo_raw, dir, param, caller));
    std::unique_ptr<ON_NurbsCurve> c_hi(IsoCurveOf(hi_raw, dir, param, caller));
    if (reverse_iso) {
      ReverseKeepDomain(*c_lo);
      ReverseKeepDomain(*c_hi);
    }
    std::unique_ptr<ON_NurbsSurface> wall = RuledBetween(*c_lo, *c_hi, 0.0, 1.0, caller);
    const int si = brep.AddSurface(wall.release());
    brep.NewFace(si);
  };
  add_wall(0, dv.Min(), /*reverse_iso=*/false);  // v = v_min, walked +u
  add_wall(1, du.Max(), /*reverse_iso=*/false);  // u = u_max, walked +v
  add_wall(0, dv.Max(), /*reverse_iso=*/true);   // v = v_max, walked -u
  add_wall(1, du.Min(), /*reverse_iso=*/true);   // u = u_min, walked -v

  brep.SetTrimIsoFlags();
  result.AppendUntrimmedFaceSideTables(6);

  // Outward-orientation safety net, the same one AssembleSweptBody() itself
  // uses: the construction above orients the result outward already under
  // the NormalAt() == Su x Sv assumption; a negative tessellated volume
  // here would mean that assumption didn't hold for this particular
  // surface, and a single whole-body flip is the correct repair since
  // every face above was derived from the SAME assumption (never a
  // per-face-only mistake that a global flip could get wrong).
  const Mesh check = result.TessellateToClosedMesh(16, 16);
  if (check.Volume() < 0.0) brep.Flip();

  return result;
}

Brep Brep::ExtrudeToPoint(const NurbsCurve& profile, Point3d apex, bool cap) {
  const char* caller = "ExtrudeToPoint";
  ON_NurbsCurve c = profile.raw();
  if (!c.IsValid()) Fail(caller, "profile is not a valid NURBS curve");
  ClampIfPeriodic(c);
  const bool closed = c.IsClosed();

  ON_Plane plane;
  if (!c.IsPlanar(&plane, 1e-8 * CurveScale(c))) {
    Fail(caller,
         "profile is not planar - the embedding proof (any two rulings apex -> profile(u1), "
         "apex -> profile(u2) are distinct lines through the common point apex, and two distinct lines "
         "through a common point meet only there) requires profile to lie in a single plane that apex does "
         "not, so a non-planar profile has no such plane to check apex against");
  }
  const double scale = std::max(CurveScale(c), 1.0);
  const double signed_dist = plane.DistanceTo(apex);
  if (std::fabs(signed_dist) <= 1e-9 * scale) {
    Fail(caller,
         "apex lies in the profile's own plane - every ruling apex -> profile(u) would then lie IN that "
         "plane too, so the embedding proof (which needs apex OFF the plane, so a ruling meets the plane "
         "only at its own profile(u)) does not apply and the cone could self-intersect; apex must be "
         "strictly off the profile's plane");
  }

  // Outward wall: the same CCW-about-the-sweep-direction convention
  // Extrude() uses, with the direction from apex (this wall's v = 0 end)
  // toward the profile's own plane (v = 1, the far/rim end) standing in
  // for Extrude()'s own `direction` (which likewise points from its
  // wall's v = 0 end to its v = 1 end) - the negative of (apex - plane
  // point), i.e. pointing away from apex. Only meaningful for a CLOSED
  // profile: "outward" here means "radially away from the cone's own
  // interior", which needs a closed loop with a well-defined interior to
  // be outward FROM - an open profile's fan has no such interior (it is
  // a curved wedge, not a solid boundary), so no auto-reversal is applied
  // there; the wall's own normal direction follows directly from
  // FanSurface(profile, apex)'s own construction, exactly as given.
  if (closed) {
    const ON_3dVector d_unit = signed_dist >= 0.0 ? -plane.zaxis : plane.zaxis;
    const ON_Plane about_d(plane.origin, d_unit);
    if (SignedAreaAbout(c, about_d) < 0.0) ReverseKeepDomain(c);
  }

  std::unique_ptr<ON_NurbsSurface> wall_in = FanSurface(c, apex, caller);
  const ON_NurbsSurface wall = *wall_in;  // keep an evaluable copy; the brep owns the original

  Brep result;
  ON_Brep& brep = result.raw();
  int vid[4] = {-1, -1, -1, -1};
  int eid[4] = {-1, -1, -1, -1};
  bool rev[4] = {false, false, false, false};
  // wall is closed in u (profile closed) and singular at v = 0 (the apex):
  // ON_Brep::NewFace's own closed/singular handling (see opennurbs_brep_
  // tools.cpp's NewOuterLoop) gives exactly the cone topology this
  // function's own brep.h doc comment describes - a singular south trim
  // at the apex, an auto-shared east/west seam edge (apex -> the rim's
  // own seam point, appearing twice in the loop), and a genuine closed
  // rim edge at v = 1. AssembleSweptBody() is deliberately NOT used here:
  // it explicitly refuses any wall singular at v0/v1 (built for non-
  // degenerate rectangular sweeps), which this wall is by construction.
  // For an OPEN profile, `wall` is open in u too (the profile's own two
  // distinct endpoints), giving NewFace's own topology inference two
  // separate straight "spoke" edges (apex to each endpoint) instead of
  // the closed case's one doubled seam edge, plus the open profile curve
  // itself as the third boundary edge - a valid, if uncappable, open
  // fan shell (a curved wedge/slice), no different in kind from the
  // "open profile gives one open face" convention `ExtrudeAlongCurve()`
  // already documents.
  if (!brep.NewFace(wall_in.release(), vid, eid, rev)) Internal(caller, "ON_Brep::NewFace refused the cone wall");
  int faces = 1;

  // `cap` is silently ignored for an open profile - the same "no caps
  // regardless of cap" convention `ExtrudeAlongCurve()` already uses for
  // its own open-profile case, since an open profile's fan has no single
  // natural closing cap the way a closed one's flat rim does (the "chord
  // closing the open boundary" a cap would need is not a real edge of
  // this wall at all, unlike the closed case's genuine closed rim).
  if (cap && closed) {
    const ON_Interval dv = wall.Domain(1);
    const double um = wall.Domain(0).Mid();
    std::unique_ptr<ON_NurbsCurve> rim(IsoCurveOf(wall, 0, dv.Max(), caller));
    const CapPlan plan = PlanCap(*rim, /*chord_closed=*/false, caller);
    ON_3dPoint p;
    ON_3dVector su, sv;
    if (!wall.Ev1Der(um, dv.Max(), p, su, sv)) Internal(caller, "Ev1Der failed on the cone wall");
    // Wall's own north (v = v_max, the rim) side: built reversed at
    // NewFace time (rev[2] left false -> NewOuterLoop's own i >= 2 &&
    // !bRev3d rule reverses it), the same "isocurve reversed at creation"
    // fact AssembleSweptBody's own cap_v1 branch documents - so
    // edge_forward is false here too, and the outward hint is +sv (the
    // rim is the wall's FAR end from the apex, same as a cap_v1).
    AddFanCap(brep, *rim, eid[2], /*edge_forward=*/false, sv, plan, nullptr, /*transpose=*/false, caller);
    ++faces;
  }

  brep.SetTolerancesBoxesAndFlags(/*bLazy=*/true);
  result.AppendUntrimmedFaceSideTables(faces);

  // Same outward cross-check AssembleSweptBody() ends with: the
  // construction rules above orient it outward already, so a negative
  // tessellated volume here would mean a rule was wrong for this input.
  if (brep.IsSolid()) {
    const Mesh m = result.TessellateToClosedMesh(16, 16);
    if (m.Volume() < 0.0) brep.Flip();
  }
  return result;
}

namespace {

// A CLOSED profile with a sub-arc running exactly along the revolve axis
// (e.g. a rectangle with one side on it) would sweep that sub-arc to a
// degenerate zero-area band, not a real surface region - Revolve() cannot
// use it as given. But the exact same solid is what revolving the
// profile's own AWAY-from-axis remainder gives, as an OPEN profile with
// both new endpoints (where the touching arc began/ended) sitting ON the
// axis - Revolve()'s own already-supported "open profile, both ends on
// the axis" pole construction (the doc comment's own cylinder example,
// (0,0)->(r,0)->(r,h)->(0,h), IS that remainder for a rectangle touching
// along its 4th side). This finds that single touching sub-arc (sampled
// circularly) and splits it off, returning the remainder - so callers
// needing this need not build the open profile by hand.
//
// Reseams the curve (ChangeClosedCurveSeam) to the touching run's OWN
// midpoint first - a point guaranteed strictly interior to the touching
// arc, never at an existing knot or domain boundary the way a point
// picked at the run's own edge can land (that was tried first: it makes
// `ON_NurbsCurve::Split` refuse whenever the touching arc happens to
// already sit at the profile's own authored seam, e.g. this function's
// own canonical rectangle example, since the "boundary" there IS the
// existing domain edge) - so both splits below are always comfortably
// interior and never hit that refusal. Throws (via Fail/Internal) for
// more than one separate touching region, or a single-point kiss rather
// than a genuine sub-arc - both out of scope here, same as this
// function's own callers' existing "touches the axis away from its
// endpoints" restriction for an open profile.
ON_NurbsCurve SplitTouchingAxisArc(const ON_NurbsCurve& profile, ON_3dPoint axis_point, ON_3dVector T,
                                   ON_3dVector e_rho, double tol, const char* caller) {
  auto rho_at = [&](const ON_NurbsCurve& curve, double t) {
    const ON_3dVector d = curve.PointAt(t) - axis_point;
    return ON_DotProduct(d - T * ON_DotProduct(d, T), e_rho);
  };
  // Bisects (t_away, t_touch) - opposite rho-vs-tol classifications
  // guaranteed by every call site below - to the true crossing, to
  // double precision, regardless of the initial bracket width.
  auto refine = [&](const ON_NurbsCurve& curve, double t_away, double t_touch) {
    for (int iter = 0; iter < 80; ++iter) {
      const double mid = 0.5 * (t_away + t_touch);
      if (rho_at(curve, mid) > tol) {
        t_away = mid;
      } else {
        t_touch = mid;
      }
    }
    return t_touch;
  };

  const ON_NurbsCurve c = profile;
  const ON_Interval dom = c.Domain();
  const int n = std::max(256, 32 * c.SpanCount());
  std::vector<double> t(static_cast<size_t>(n));
  std::vector<bool> touching(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    t[static_cast<size_t>(i)] = dom.ParameterAt(static_cast<double>(i) / n);
    touching[static_cast<size_t>(i)] = rho_at(c, t[static_cast<size_t>(i)]) <= tol;
  }
  int start = -1;
  for (int i = 0; i < n; ++i) {
    if (!touching[static_cast<size_t>(i)]) {
      start = i;
      break;
    }
  }
  if (start < 0) Internal(caller, "SplitTouchingAxisArc: the whole profile touches the axis");

  // Walk once around from a non-touching sample, recording the single
  // away -> touching transition (enter) and touching -> away one (leave).
  int enter = -1, leave = -1;
  for (int k = 0; k < n; ++k) {
    const int i = (start + k) % n;
    const int i_next = (start + k + 1) % n;
    if (!touching[static_cast<size_t>(i)] && touching[static_cast<size_t>(i_next)]) {
      if (enter >= 0) {
        Fail(caller,
             "a closed profile touching the axis at more than one place is not supported - revolve the away "
             "portion(s) as separate open profiles instead");
      }
      enter = i_next;
    }
    if (touching[static_cast<size_t>(i)] && !touching[static_cast<size_t>(i_next)]) leave = i_next;
  }
  if (enter < 0 || leave < 0) Internal(caller, "SplitTouchingAxisArc: no touching run found");
  const int run_len = (leave - enter + n) % n;
  if (run_len < 2) {
    // A touching run this close to the sample grid's own resolution is a
    // single-point kiss (true tangency, e.g. a circle tangent to the
    // axis), not a genuine sub-arc - the original degenerate-band refusal
    // still applies; there is no "away remainder" to find here.
    Fail(caller,
         "a closed profile touching the axis is not supported (the touching part would sweep to a degenerate "
         "band) - revolve the open profile instead, e.g. (0,0)->(r,0)->(r,h)->(0,h) for a cylinder");
  }
  const int mid_index = (enter + run_len / 2) % n;
  const double t_seam = t[static_cast<size_t>(mid_index)];

  ON_NurbsCurve c2 = c;
  if (!c2.ChangeClosedCurveSeam(t_seam)) {
    Internal(caller, "SplitTouchingAxisArc: could not reseam the closed profile inside its own touching arc");
  }

  // Re-locate the two transitions in the reseamed parametrization:
  // dom2.Min() sits inside the touching run by construction, so walking
  // forward first LEAVES it (u_leave) and later RE-ENTERS it (u_enter) -
  // both strictly interior to (dom2.Min(), dom2.Max()).
  const ON_Interval dom2 = c2.Domain();
  const int n2 = std::max(256, 32 * c2.SpanCount());
  double u_leave = -1.0, u_enter = -1.0;
  {
    bool prev_touching = true;  // dom2.Min() is inside the touching run
    double prev_t = dom2.Min();
    for (int i = 1; i <= n2 && u_enter < 0.0; ++i) {
      const double u = dom2.ParameterAt(static_cast<double>(i) / n2);
      const bool now = rho_at(c2, u) <= tol;
      if (prev_touching && !now && u_leave < 0.0) u_leave = refine(c2, u, prev_t);
      if (!prev_touching && now && u_leave >= 0.0) u_enter = refine(c2, prev_t, u);
      prev_touching = now;
      prev_t = u;
    }
  }
  if (u_leave < 0.0 || u_enter < 0.0) {
    Internal(caller, "SplitTouchingAxisArc: could not re-locate the touching arc after reseaming");
  }

  ON_Curve *l1 = nullptr, *r1 = nullptr;
  if (!c2.Split(u_leave, l1, r1)) {
    delete l1;
    delete r1;
    Internal(caller, "SplitTouchingAxisArc: first split failed");
  }
  std::unique_ptr<ON_Curve> left1(l1), right1(r1);
  ON_NurbsCurve* remainder = ON_NurbsCurve::Cast(right1.get());
  if (!remainder) Internal(caller, "SplitTouchingAxisArc: first split did not return a NURBS curve");

  ON_Curve *l2 = nullptr, *r2 = nullptr;
  if (!remainder->Split(u_enter, l2, r2)) {
    delete l2;
    delete r2;
    Internal(caller, "SplitTouchingAxisArc: second split failed");
  }
  std::unique_ptr<ON_Curve> left2(l2), right2(r2);
  ON_NurbsCurve* away = ON_NurbsCurve::Cast(left2.get());
  if (!away) Internal(caller, "SplitTouchingAxisArc: second split did not return a NURBS curve");
  return *away;
}

}  // namespace

Brep Brep::Revolve(const NurbsCurve& profile, Point3d axis_point, Vector3d axis_direction, double angle, bool cap,
                   double start_angle) {
  const char* caller = "Revolve";
  ON_3dVector T = axis_direction;
  if (!T.Unitize()) Fail(caller, "axis_direction must be non-zero");
  if (!(angle > 0.0) || angle > 2.0 * ON_PI + 1e-12) Fail(caller, "angle must be in (0, 2*pi] radians");
  if (!std::isfinite(start_angle)) Fail(caller, "start_angle must be finite");
  const bool full = std::fabs(angle - 2.0 * ON_PI) <= 1e-12;
  ON_NurbsCurve c = profile.raw();
  if (!c.IsValid()) Fail(caller, "profile is not a valid NURBS curve");
  ClampIfPeriodic(c);
  if (start_angle != 0.0) {
    // Rotate the profile rigidly about the same axis so the sweep below
    // (which always starts at the profile's own current position) begins
    // `start_angle` around from where it was given.
    ON_Xform rot;
    rot.Rotation(start_angle, T, axis_point);
    c.Transform(rot);
  }
  const double scale = CurveScale(c) + axis_point.DistanceTo(CvCentroid(c));
  const double tol = 1e-9 * scale;

  // The profile must lie in a plane through the axis, on one side of it.
  // Radial basis e_rho from the control point farthest from the axis.
  ON_3dVector e_rho;
  {
    double best = -1.0;
    for (int i = 0; i < c.CVCount(); ++i) {
      const ON_3dVector d = EuclideanCV(c, i) - axis_point;
      const ON_3dVector radial = d - T * ON_DotProduct(d, T);
      if (radial.Length() > best) {
        best = radial.Length();
        e_rho = radial;
      }
    }
    if (best <= tol) Fail(caller, "the whole profile lies on the axis - nothing to revolve");
    e_rho.Unitize();
  }
  const ON_3dVector e_phi = ON_CrossProduct(T, e_rho);
  // Scans `curve` against the axis: throws if any sample leaves its own
  // plane through the axis or crosses to the axis' other side, else
  // returns the minimum e_rho-coordinate seen (<= tol means some point -
  // possibly a whole sub-arc - sits ON the axis).
  auto scan = [&](const ON_NurbsCurve& curve) {
    const ON_Interval d = curve.Domain();
    const int n = std::max(256, 32 * curve.SpanCount());
    double lo = std::numeric_limits<double>::max();
    for (int i = 0; i <= n; ++i) {
      const ON_3dVector v = curve.PointAt(d.ParameterAt(static_cast<double>(i) / n)) - axis_point;
      if (std::fabs(ON_DotProduct(v, e_phi)) > 1e-8 * scale) {
        Fail(caller, "profile must lie in a plane containing the axis");
      }
      lo = std::min(lo, ON_DotProduct(v, e_rho));
    }
    if (lo < -1e-8 * scale) Fail(caller, "profile must stay on one side of the axis (it crosses it)");
    return lo;
  };
  double min_rho = scan(c);

  if (c.IsClosed() && min_rho <= tol) {
    // A closed profile touching the axis along a sub-arc: revolve its
    // away-from-axis remainder instead (see SplitTouchingAxisArc's own
    // doc comment) - the exact same solid, and every rule below already
    // supports it as the "open profile, both ends on the axis" case.
    c = SplitTouchingAxisArc(c, axis_point, T, e_rho, tol, caller);
    min_rho = scan(c);
  }

  const ON_Interval dom = c.Domain();
  const int samples = std::max(256, 32 * c.SpanCount());
  const bool closed = c.IsClosed();
  const ON_3dPoint p_start = c.PointAtStart(), p_end = c.PointAtEnd();
  auto radius_of = [&](ON_3dPoint p) {
    const ON_3dVector d = p - axis_point;
    return (d - T * ON_DotProduct(d, T)).Length();
  };
  if (closed && min_rho <= tol) {
    Fail(caller,
         "a closed profile touching the axis is not supported (the touching part would sweep to a degenerate "
         "band) - revolve the open profile instead, e.g. (0,0)->(r,0)->(r,h)->(0,h) for a cylinder");
  }
  // Interior on-axis contact of an open profile (touching the axis away
  // from its endpoints) is the same degenerate band.
  if (!closed && min_rho <= tol) {
    for (int i = 1; i < samples; ++i) {
      const ON_3dVector d = c.PointAt(dom.ParameterAt(static_cast<double>(i) / samples)) - axis_point;
      if (ON_DotProduct(d, e_rho) <= tol) {
        const double t = static_cast<double>(i) / samples;
        if (t > 0.02 && t < 0.98) Fail(caller, "the profile touches the axis away from its endpoints");
      }
    }
  }

  // Outward orientation: the region (profile, closed by the axis segment
  // through the endpoints' axis feet for an open profile) must be
  // clockwise in the (rho, z) half-plane - see below.
  {
    std::vector<ON_2dPoint> poly;
    for (int i = 0; i < samples; ++i) {
      const ON_3dVector d = c.PointAt(dom.ParameterAt(static_cast<double>(i) / samples)) - axis_point;
      poly.emplace_back(ON_DotProduct(d, e_rho), ON_DotProduct(d, T));
    }
    if (!closed) {
      const ON_3dVector de = p_end - axis_point, ds = p_start - axis_point;
      poly.emplace_back(ON_DotProduct(de, e_rho), ON_DotProduct(de, T));
      poly.emplace_back(0.0, ON_DotProduct(de, T));
      poly.emplace_back(0.0, ON_DotProduct(ds, T));
    }
    const double area = SignedArea2d(poly);
    if (std::fabs(area) <= 1e-12 * scale * scale) Fail(caller, "the profile encloses no area with the axis");
    // Wall normal = S_u x S_v = C'(u) x e_phi; for C' = e_z that is
    // e_z x (e_z x e_rho) = -e_rho, i.e. INWARD when the region is
    // counterclockwise in (rho, z). So the region must be clockwise
    // there for an outward wall (verified by TestRevolve*'s m_bRev == 0
    // checks, which would catch a flipped rule via the volume cross-check).
    if (area > 0.0) ReverseKeepDomain(c);
  }
  // (Re)derived after the possible reversal above.
  const bool s_on = radius_of(c.PointAtStart()) <= tol;
  const bool e_on = radius_of(c.PointAtEnd()) <= tol;

  std::unique_ptr<ON_NurbsSurface> wall = RevolvedSurface(c, axis_point, T, angle, full, tol, caller);

  bool cap_v0 = false, cap_v1 = false, cap_u0 = false, cap_u1 = false;
  if (cap) {
    if (closed) {
      cap_v0 = cap_v1 = !full;
    } else if (s_on && e_on) {
      cap_v0 = cap_v1 = !full;
    } else {
      if (!full) Fail(caller, "a partial revolve of an open profile with an endpoint off the axis cannot be capped here");
      cap_u0 = !s_on;
      cap_u1 = !e_on;
      if (cap_u0 && cap_u1) {
        const double z0 = ON_DotProduct(c.PointAtStart() - axis_point, T), z1 = ON_DotProduct(c.PointAtEnd() - axis_point, T);
        if (std::fabs(z0 - z1) <= tol) Fail(caller, "both endpoints are off the axis at the same height - a zero-thickness disc");
      }
    }
  }
  return AssembleSweptBody(wall.release(), cap_v0, cap_v1, cap_u0, cap_u1, caller);
}

Brep Brep::RailRevolve(const NurbsCurve& profile_in, Point3d axis_point, Vector3d axis_direction,
                       const NurbsCurve& rail_in, double angle, int stations, bool cap) {
  const char* caller = "RailRevolve";
  ON_3dVector T = axis_direction;
  if (!T.Unitize()) Fail(caller, "axis_direction must be non-zero");
  if (!(angle > 0.0) || angle > 2.0 * ON_PI + 1e-12) Fail(caller, "angle must be in (0, 2*pi] radians");
  if (stations < 2) Fail(caller, "stations must be at least 2");
  ON_NurbsCurve c = profile_in.raw();
  if (!c.IsValid()) Fail(caller, "profile is not a valid NURBS curve");
  ClampIfPeriodic(c);
  const bool closed = c.IsClosed();
  ON_NurbsCurve rail = rail_in.raw();
  if (!rail.IsValid()) Fail(caller, "rail is not a valid NURBS curve");

  const double scale = CurveScale(c) + axis_point.DistanceTo(CvCentroid(c));
  const double tol = 1e-9 * scale;

  // profile must lie in a plane through the axis, on one side of it - the
  // same constraint Revolve() itself imposes; e_rho is the radial
  // direction of the control point farthest from the axis.
  ON_3dVector e_rho;
  {
    double best = -1.0;
    for (int i = 0; i < c.CVCount(); ++i) {
      const ON_3dVector d = EuclideanCV(c, i) - axis_point;
      const ON_3dVector radial = d - T * ON_DotProduct(d, T);
      if (radial.Length() > best) {
        best = radial.Length();
        e_rho = radial;
      }
    }
    if (best <= tol) Fail(caller, "the whole profile lies on the axis - nothing to revolve");
    e_rho.Unitize();
  }
  const ON_3dVector e_phi = ON_CrossProduct(T, e_rho);
  const ON_Interval dom = c.Domain();
  const int samples = std::max(256, 32 * c.SpanCount());
  double min_rho = std::numeric_limits<double>::max();
  for (int i = 0; i <= samples; ++i) {
    const ON_3dVector d = c.PointAt(dom.ParameterAt(static_cast<double>(i) / samples)) - axis_point;
    if (std::fabs(ON_DotProduct(d, e_phi)) > 1e-8 * scale) {
      Fail(caller, "profile must lie in a plane containing the axis");
    }
    min_rho = std::min(min_rho, ON_DotProduct(d, e_rho));
  }
  if (closed && min_rho <= tol) {
    Fail(caller, "profile must stay strictly off the axis - a closed profile touching it is not supported "
                 "(the touching part would sweep to a degenerate band), the same restriction Revolve() imposes");
  }
  // An open profile may touch the axis only at its own endpoints (the
  // same "both ends on the axis" pole case Revolve() supports) - touching
  // it away from them is the same degenerate band as the closed case.
  if (!closed && min_rho <= tol) {
    for (int i = 1; i < samples; ++i) {
      const ON_3dVector d = c.PointAt(dom.ParameterAt(static_cast<double>(i) / samples)) - axis_point;
      if (ON_DotProduct(d, e_rho) <= tol) {
        const double t = static_cast<double>(i) / samples;
        if (t > 0.02 && t < 0.98) Fail(caller, "the profile touches the axis away from its endpoints");
      }
    }
  }

  // Outward orientation: the same (rho, z) half-plane clockwise rule
  // Revolve() itself uses - see its own doc comment for the derivation,
  // including (for an open profile) closing the polygon through the axis
  // via the two endpoints' own axis-projected feet, purely to get a
  // reliable winding sign - not a claim about the wall's real topology.
  {
    std::vector<ON_2dPoint> poly;
    for (int i = 0; i < samples; ++i) {
      const ON_3dVector d = c.PointAt(dom.ParameterAt(static_cast<double>(i) / samples)) - axis_point;
      poly.emplace_back(ON_DotProduct(d, e_rho), ON_DotProduct(d, T));
    }
    if (!closed) {
      const ON_3dVector de = c.PointAtEnd() - axis_point, ds = c.PointAtStart() - axis_point;
      poly.emplace_back(ON_DotProduct(de, e_rho), ON_DotProduct(de, T));
      poly.emplace_back(0.0, ON_DotProduct(de, T));
      poly.emplace_back(0.0, ON_DotProduct(ds, T));
    }
    const double area = SignedArea2d(poly);
    if (std::fabs(area) <= 1e-12 * scale * scale) Fail(caller, "the profile encloses no area with the axis");
    if (area > 0.0) ReverseKeepDomain(c);
  }

  // Decode the profile ONCE, into local (rho, z) coordinates about the
  // axis basis (e_rho, T) - `rail` supplies a per-station radial scale
  // factor below; the axial coordinate `z` is left untouched at every
  // station.
  const int n = c.CVCount();
  std::vector<double> rho(static_cast<size_t>(n)), z(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const ON_3dVector d = EuclideanCV(c, i) - axis_point;
    rho[static_cast<size_t>(i)] = ON_DotProduct(d, e_rho);
    z[static_cast<size_t>(i)] = ON_DotProduct(d, T);
  }

  const bool wrap = std::fabs(angle - 2.0 * ON_PI) <= 1e-12;
  const int m = wrap ? std::max(stations, 3) : stations;

  std::vector<double> rp = rail_in.DivideByCount(wrap ? m : m - 1);
  if (wrap) rp.pop_back();
  if (static_cast<int>(rp.size()) != m) Internal(caller, "station count mismatch");

  auto rail_radius = [&](double t) {
    const ON_3dVector d = rail.PointAt(t) - axis_point;
    return (d - T * ON_DotProduct(d, T)).Length();
  };
  const double r0 = rail_radius(rp[0]);
  if (r0 <= tol) Fail(caller, "the rail lies on the axis at its own start station - nothing to scale by");

  // Any profile control point already ON the axis (rho[i] <= tol - only
  // possible at the profile's own endpoints, the "both ends on the axis"
  // open-profile case; already checked above) must land EXACTLY on the
  // axis at every station, not just approximately: `rho[i] * s` could
  // still be a tiny nonzero residual (rho[i] itself is only <= tol, not
  // necessarily bit-exact 0), and multiplying that by a DIFFERENT
  // rotation direction e_rho_k at every station would scatter it into a
  // tiny but genuinely DIFFERENT point per station - not coincident
  // across stations even though each is individually within tolerance of
  // the axis. That would make AssembleSweptBody()'s own IsSingular()
  // check (an exact coincidence test, not a tolerance one) correctly
  // report the column as NOT singular, silently losing the pole capping
  // this function's own brep.h doc comment promises. Skipping the radial
  // term entirely for such a CV, rather than trusting it to numerically
  // cancel, keeps every station's own copy of it bit-identical.
  std::vector<ON_NurbsCurve> copies;
  copies.reserve(static_cast<size_t>(m));
  for (int k = 0; k < m; ++k) {
    const double theta = wrap ? (2.0 * ON_PI * static_cast<double>(k) / static_cast<double>(m))
                              : (angle * static_cast<double>(k) / static_cast<double>(m - 1));
    const double s = rail_radius(rp[static_cast<size_t>(k)]) / r0;
    const ON_3dVector e_rho_k = e_rho * std::cos(theta) + e_phi * std::sin(theta);
    ON_NurbsCurve ck = c;
    for (int i = 0; i < n; ++i) {
      const ON_3dPoint p = rho[static_cast<size_t>(i)] <= tol
                               ? axis_point + T * z[static_cast<size_t>(i)]
                               : axis_point + T * z[static_cast<size_t>(i)] +
                                     e_rho_k * (rho[static_cast<size_t>(i)] * s);
      const double w = c.Weight(i);
      ck.SetCV(i, ON_4dPoint(p.x * w, p.y * w, p.z * w, w));
    }
    copies.push_back(std::move(ck));
  }
  MakeCompatible(copies, caller);  // no-op (identical control structure at every station)

  std::unique_ptr<ON_NurbsSurface> wall;
  if (m == 2) {
    wall = RuledBetween(copies[0], copies[1], 0.0, 1.0, caller);
  } else {
    double period = 1.0;
    const std::vector<double> params_v = SkinParameters(copies, wrap, &period, caller);
    wall = SkinSections(copies, std::min(3, m - 1), wrap, params_v, period, caller);
  }

  // Every `copies[k]`'s own CV i was already made bit-identical above
  // whenever rho[i] <= tol (0.0 for a genuinely on-axis profile CV, the
  // ordinary case), but a CV only within `tol` of the axis rather than
  // bit-exact 0 is possible too (a profile built by upstream floating-
  // point construction rather than typed in by hand) - `rho[i] * s`
  // would then be a tiny but genuinely nonzero residual, scattered into a
  // DIFFERENT tiny offset per station by each station's own rotation
  // e_rho_k, so the m per-station copies of that CV would no longer be
  // bit-identical even though each is individually within tolerance of
  // the axis. RuledBetween()'s/SkinSections()'s own arithmetic is not
  // guaranteed to cancel that back out either. Left uncorrected, this
  // would make IsSingular()'s own exact-coincidence check (no tolerance)
  // read a genuinely (to this function's own `tol`) on-axis column as
  // NOT singular, silently losing the pole `AssembleSweptBody()` needs to
  // cap it or close a full-wrap tube without extra caps - so every such
  // column is forced back to the exact axis point directly here,
  // sidestepping whatever either function's own arithmetic produced.
  for (int i = 0; i < n; ++i) {
    if (rho[static_cast<size_t>(i)] > tol) continue;
    const ON_3dPoint axis_p = axis_point + T * z[static_cast<size_t>(i)];
    const double w = c.Weight(i);
    for (int j = 0; j < wall->CVCount(1); ++j) {
      wall->SetCV(i, j, ON_4dPoint(axis_p.x * w, axis_p.y * w, axis_p.z * w, w));
    }
  }

  const bool want_caps = cap && !wrap;
  return AssembleSweptBody(wall.release(), want_caps, want_caps, false, false, caller);
}

Brep Brep::Loft(const std::vector<NurbsCurve>& sections_in, int degree, bool closed, bool cap,
                const NurbsCurve* start_tangent, const NurbsCurve* end_tangent) {
  const char* caller = "Loft";
  const int N = static_cast<int>(sections_in.size());
  if (N < 2) Fail(caller, "at least 2 sections are required");
  if (degree < 1) Fail(caller, "degree must be at least 1");
  int q = std::min(degree, N - 1);
  if (closed && N < degree + 1) Fail(caller, "a closed loft needs at least degree + 1 sections");
  const bool want_tangent = start_tangent != nullptr || end_tangent != nullptr;
  if (want_tangent) {
    if (closed) Fail(caller, "start_tangent/end_tangent are not supported for a closed (periodic) loft - it has no ends");
    if (sections_in.front().raw().IsClosed()) {
      Fail(caller, "start_tangent/end_tangent are not supported for closed-curve (periodic-loop) sections");
    }
    if (q < 2) Fail(caller, "start_tangent/end_tangent need degree >= 2 and at least 3 sections");
  }
  std::vector<ON_NurbsCurve> sections;
  sections.reserve(static_cast<size_t>(N));
  for (const NurbsCurve& s : sections_in) sections.push_back(s.raw());
  std::vector<ON_NurbsCurve> compat = sections;
  if (start_tangent) compat.push_back(start_tangent->raw());
  if (end_tangent) compat.push_back(end_tangent->raw());
  if (want_tangent) {
    for (const ON_NurbsCurve& c : compat) {
      if (c.IsRational()) Fail(caller, "start_tangent/end_tangent do not support rational sections or tangent curves");
    }
  }
  MakeCompatible(compat, caller);
  for (int i = 0; i < N; ++i) sections[static_cast<size_t>(i)] = compat[static_cast<size_t>(i)];
  size_t next_compat = static_cast<size_t>(N);
  ON_NurbsCurve start_tangent_compat, end_tangent_compat;
  if (start_tangent) start_tangent_compat = compat[next_compat++];
  if (end_tangent) end_tangent_compat = compat[next_compat++];
  const bool first_closed = sections.front().IsClosed();
  for (const ON_NurbsCurve& s : sections) {
    if (s.IsClosed() != first_closed) Fail(caller, "sections must be all closed or all open");
  }
  const bool want_caps = cap && first_closed && !closed;
  if (first_closed) {
    // Outward wall: the first section must run counterclockwise about the
    // direction toward the second section. Applied to every closed-
    // section loft (capped or periodic) so the result faces outward by
    // construction; a non-planar first section is left as given (a
    // capped one is refused, a periodic one relies on the volume cross-
    // check in AssembleSweptBody).
    ON_Plane plane;
    const ON_NurbsCurve& c0 = sections.front();
    const bool planar = c0.IsPlanar(&plane, 1e-8 * CurveScale(c0));
    if (!planar && want_caps) Fail(caller, "cap requested but the first section is not planar");
    if (planar) {
      ON_3dVector inward = CvCentroid(sections[1]) - CvCentroid(c0);
      if (ON_DotProduct(inward, plane.zaxis) < 0.0) plane.Flip();
      if (SignedAreaAbout(c0, plane) < 0.0) {
        for (ON_NurbsCurve& s : sections) ReverseKeepDomain(s);
      }
    }
  }
  std::unique_ptr<ON_NurbsSurface> wall;
  if (want_tangent) {
    const int n = sections.front().CVCount();
    std::vector<ON_3dVector> start_dirs, end_dirs;
    if (start_tangent) {
      start_dirs.resize(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i) {
        const ON_3dPoint p = EuclideanCV(start_tangent_compat, i);
        start_dirs[static_cast<size_t>(i)] = ON_3dVector(p.x, p.y, p.z);
      }
    }
    if (end_tangent) {
      end_dirs.resize(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i) {
        const ON_3dPoint p = EuclideanCV(end_tangent_compat, i);
        end_dirs[static_cast<size_t>(i)] = ON_3dVector(p.x, p.y, p.z);
      }
    }
    double period = 1.0;
    const std::vector<double> params = SkinParameters(sections, false, &period, caller);
    wall = SkinSectionsTangent(sections, q, params, start_dirs, end_dirs, caller);
  } else if (closed && q == 1) {
    // A degree-1 closed loft is the piecewise-ruled loop back to the
    // first section - built on the clamped path through the wrapped
    // list, since ON_NurbsSurface::IsPeriodic() is defined only for
    // degree >= 2 (the geometry is identical either way).
    std::vector<ON_NurbsCurve> wrapped = sections;
    wrapped.push_back(sections.front());
    double period = 1.0;
    const std::vector<double> params = SkinParameters(wrapped, false, &period, caller);
    wall = SkinSections(wrapped, 1, false, params, period, caller);
    if (!wall->IsClosed(1)) Internal(caller, "the degree-1 closed loft does not report closed");
  } else {
    double period = 1.0;
    const std::vector<double> params = SkinParameters(sections, closed, &period, caller);
    if (!closed && q == 1 && N == 2) {
      wall = RuledBetween(sections[0], sections[1], 0.0, 1.0, caller);
    } else {
      wall = SkinSections(sections, q, closed, params, period, caller);
    }
  }
  return AssembleSweptBody(wall.release(), want_caps, want_caps, false, false, caller);
}

Brep Brep::Sweep1(const NurbsCurve& section_in, const NurbsCurve& rail_in, int stations, bool cap, double twist_total,
                  double scale_end, const Vector3d* roadlike_up) {
  const char* caller = "Sweep1";
  if (stations < 2) Fail(caller, "stations must be at least 2");
  if (!(scale_end > 0.0)) Fail(caller, "scale_end must be positive");
  ON_NurbsCurve rail = rail_in.raw();
  if (!rail.IsValid()) Fail(caller, "rail is not a valid NURBS curve");
  ON_NurbsCurve section = section_in.raw();
  if (!section.IsValid()) Fail(caller, "section is not a valid NURBS curve");
  ClampIfPeriodic(section);
  const bool wrap = rail.IsClosed();
  if (wrap && twist_total != 0.0) {
    Fail(caller, "twist_total is not supported for a closed rail - a non-multiple-of-2*pi twist would keep the tube from "
                 "closing up smoothly, and the multiple-of-2*pi spiral case is not attempted here");
  }
  if (wrap && scale_end != 1.0) {
    Fail(caller, "scale_end != 1.0 is not supported for a closed rail - the tube would not meet itself at the seam");
  }
  ON_3dVector up;
  if (roadlike_up) {
    up = *roadlike_up;
    if (!up.Unitize()) Fail(caller, "roadlike_up must be a nonzero vector");
  }
  const bool straight = !wrap && rail.IsLinear(1e-9 * CurveScale(rail));
  const int m = straight ? 2 : std::max(stations, 3);

  // Equal-arc-length stations (the wrapper's DivideByCount); a closed
  // rail's last division point is its first and is dropped.
  std::vector<double> params = rail_in.DivideByCount(wrap ? m : m - 1);
  if (wrap) params.pop_back();
  if (static_cast<int>(params.size()) != m) Internal(caller, "station count mismatch");
  std::vector<Frame> frames = RmfFrames(rail, params, wrap, caller);
  if (roadlike_up) {
    // Road-like alignment: replace RMF's own transported reference
    // direction at every station with `up` projected perpendicular to
    // the tangent there, independent of every other station - see
    // brep.h's own doc comment for why this needs no holonomy fix-up on
    // a closed rail the way RMF itself does above.
    for (int k = 0; k < m; ++k) {
      Frame& f = frames[static_cast<size_t>(k)];
      ON_3dVector r = up - f.t * ON_DotProduct(up, f.t);
      if (r.LengthSquared() <= 1e-12) {
        Fail(caller, "roadlike_up is parallel to the rail tangent at a station - road-like alignment is undefined there");
      }
      r.Unitize();
      f.r = r;
      f.s = ON_CrossProduct(f.t, f.r);
    }
  }
  if (twist_total != 0.0) {
    // Extra rotation about each station's own tangent, linear in arc-
    // length station fraction k / (m - 1): 0 at the start, exactly
    // twist_total at the end. Same (r, s)-plane rotation the closed-
    // rail holonomy correction above already uses, so a straight rail's
    // m == 2 exact-extrusion path stays exact - RuledBetween() below
    // connects frame 0 (untouched) straight to frame m - 1 (rotated by
    // exactly twist_total), nothing in between to approximate.
    for (int k = 0; k < m; ++k) {
      Frame& f = frames[static_cast<size_t>(k)];
      const double a = twist_total * static_cast<double>(k) / static_cast<double>(m - 1);
      const ON_3dVector r0 = f.r, s0 = f.s;
      f.r = r0 * std::cos(a) + s0 * std::sin(a);
      f.s = ON_CrossProduct(f.t, f.r);
    }
  }

  const bool closed_section = section.IsClosed();
  const bool want_caps = cap && closed_section && !wrap;
  if (want_caps) {
    ON_Plane plane;
    if (!section.IsPlanar(&plane, 1e-8 * CurveScale(section))) Fail(caller, "cap requested but the section is not planar");
    if (std::fabs(ON_DotProduct(plane.zaxis, frames[0].t)) <= 1e-9) {
      Fail(caller, "the section's plane contains the rail tangent at the start - the sweep is flat there");
    }
    ON_Plane about_t(plane.origin, frames[0].t);
    if (SignedAreaAbout(section, about_t) < 0.0) ReverseKeepDomain(section);
  }

  std::vector<ON_NurbsCurve> copies;
  copies.reserve(static_cast<size_t>(m));
  for (int k = 0; k < m; ++k) {
    ON_NurbsCurve ck = section;
    if (k > 0) {
      ON_Xform xf = FrameToFrame(frames[0], frames[static_cast<size_t>(k)]);
      if (scale_end != 1.0) {
        // Uniform scale about the station's OWN rail point, applied
        // AFTER the rigid transport so it scales the already-placed
        // local geometry rather than the pre-transport section - linear
        // in station fraction, 1.0 at k = 0 (skipped above; a scale of
        // 1.0 there is the identity anyway) to exactly `scale_end` at
        // k = m - 1.
        const double s = 1.0 + (scale_end - 1.0) * static_cast<double>(k) / static_cast<double>(m - 1);
        xf = ON_Xform::ScaleTransformation(frames[static_cast<size_t>(k)].origin, s) * xf;
      }
      ck.Transform(xf);
    }
    copies.push_back(std::move(ck));
  }
  MakeCompatible(copies, caller);  // no-op for rigid+scaled copies; keeps one code path
  std::unique_ptr<ON_NurbsSurface> wall;
  if (m == 2) {
    wall = RuledBetween(copies[0], copies[1], 0.0, 1.0, caller);
  } else {
    // Stations are equally spaced in arc length, so the natural station
    // parameter is uniform; the chord-length average agrees for a rigid
    // sweep and is used for consistency with Loft().
    double period = 1.0;
    const std::vector<double> params_v = SkinParameters(copies, wrap, &period, caller);
    wall = SkinSections(copies, std::min(3, m - 1), wrap, params_v, period, caller);
  }
  return AssembleSweptBody(wall.release(), want_caps, want_caps, false, false, caller);
}

Brep Brep::Sweep2(const NurbsCurve& section_in, const NurbsCurve& rail1_in, const NurbsCurve& rail2_in, int stations,
                  bool cap) {
  const char* caller = "Sweep2";
  if (stations < 2) Fail(caller, "stations must be at least 2");
  ON_NurbsCurve rail1 = rail1_in.raw();
  ON_NurbsCurve rail2 = rail2_in.raw();
  if (!rail1.IsValid()) Fail(caller, "rail1 is not a valid NURBS curve");
  if (!rail2.IsValid()) Fail(caller, "rail2 is not a valid NURBS curve");
  ON_NurbsCurve section = section_in.raw();
  if (!section.IsValid()) Fail(caller, "section is not a valid NURBS curve");
  ClampIfPeriodic(section);

  // Match rail2's direction to rail1's (compare start-to-start against
  // start-to-end), the same convention the app's own Sweep2Command uses.
  const ON_3dPoint a0 = rail1.PointAtStart();
  const ON_3dPoint b0 = rail2.PointAtStart(), b1 = rail2.PointAtEnd();
  if (a0.DistanceTo(b1) < a0.DistanceTo(b0)) ReverseKeepDomain(rail2);

  const bool wrap = rail1.IsClosed() && rail2.IsClosed();
  const double rail_scale = 1e-9 * (CurveScale(rail1) + CurveScale(rail2));
  const bool straight = !wrap && rail1.IsLinear(rail_scale) && rail2.IsLinear(rail_scale);
  const int m = straight ? 2 : std::max(stations, 3);

  std::vector<double> p1 = rail1_in.DivideByCount(wrap ? m : m - 1);
  NurbsCurve rail2_k;
  rail2_k.raw() = rail2;
  std::vector<double> p2 = rail2_k.DivideByCount(wrap ? m : m - 1);
  if (wrap) {
    p1.pop_back();
    p2.pop_back();
  }
  if (static_cast<int>(p1.size()) != m || static_cast<int>(p2.size()) != m) Internal(caller, "station count mismatch");

  const std::vector<TwoRailFrame> frames = TwoRailFrames(rail1, rail2, p1, p2, caller);

  const bool closed_section = section.IsClosed();
  const bool want_caps = cap && closed_section && !wrap;
  if (want_caps) {
    ON_Plane plane;
    if (!section.IsPlanar(&plane, 1e-8 * CurveScale(section))) Fail(caller, "cap requested but the section is not planar");
    if (std::fabs(ON_DotProduct(plane.zaxis, frames[0].sweep_dir)) <= 1e-9) {
      Fail(caller, "the section's plane contains the sweep direction at the start - the sweep is flat there");
    }
    ON_Plane about_t(plane.origin, frames[0].sweep_dir);
    if (SignedAreaAbout(section, about_t) < 0.0) ReverseKeepDomain(section);
  }

  // Decode the (possibly just-reversed) input section ONCE, into station-
  // 0's frame, as local coordinates uniformly scaled by that station's
  // own rail-to-rail width - see brep.h's own doc comment for why a
  // single shared scale factor (not one per axis) is the right contract.
  const int n = section.CVCount();
  const double w0 = frames[0].width;
  std::vector<ON_3dVector> local(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const ON_3dVector d = EuclideanCV(section, i) - frames[0].origin;
    local[static_cast<size_t>(i)] =
        ON_3dVector(ON_DotProduct(d, frames[0].x), ON_DotProduct(d, frames[0].y), ON_DotProduct(d, frames[0].z)) / w0;
  }

  std::vector<ON_NurbsCurve> copies;
  copies.reserve(static_cast<size_t>(m));
  for (int k = 0; k < m; ++k) {
    ON_NurbsCurve ck = section;
    const TwoRailFrame& f = frames[static_cast<size_t>(k)];
    for (int i = 0; i < n; ++i) {
      const ON_3dVector& l = local[static_cast<size_t>(i)];
      const ON_3dPoint p = f.origin + (f.x * l.x + f.y * l.y + f.z * l.z) * f.width;
      const double w = section.Weight(i);  // 1.0 for a non-rational section
      ck.SetCV(i, ON_4dPoint(p.x * w, p.y * w, p.z * w, w));
    }
    copies.push_back(std::move(ck));
  }
  MakeCompatible(copies, caller);  // no-op (same control structure at every station); one code path with Loft()/Sweep1()
  std::unique_ptr<ON_NurbsSurface> wall;
  if (m == 2) {
    wall = RuledBetween(copies[0], copies[1], 0.0, 1.0, caller);
  } else {
    double period = 1.0;
    const std::vector<double> params_v = SkinParameters(copies, wrap, &period, caller);
    wall = SkinSections(copies, std::min(3, m - 1), wrap, params_v, period, caller);
  }
  return AssembleSweptBody(wall.release(), want_caps, want_caps, false, false, caller);
}

Brep Brep::Pipe(const NurbsCurve& rail, double radius, bool cap, int stations, bool round_caps) {
  const char* caller = "Pipe";
  if (!(radius > 0.0)) Fail(caller, "radius must be positive");
  const ON_NurbsCurve& r = rail.raw();
  if (!r.IsValid()) Fail(caller, "rail is not a valid NURBS curve");
  ON_3dVector t0 = r.TangentAt(r.Domain().Min());
  if (!t0.Unitize()) Fail(caller, "the rail has a zero tangent at its start");
  const ON_Plane plane(r.PointAtStart(), t0);
  const ON_Circle circle(plane, radius);
  ON_NurbsCurve nurbs_circle;
  if (circle.GetNurbForm(nurbs_circle) == 0) Internal(caller, "ON_Circle::GetNurbForm failed");
  NurbsCurve section;
  section.raw() = nurbs_circle;
  if (!round_caps) return Sweep1(section, rail, stations, cap);

  // Round-cap path: builds the wall directly (rather than delegating to
  // Sweep1(), which returns a finished Brep with no way to recover the
  // wall's own rim edge indices) so AssembleSweptBody() can be handed a
  // RoundCapSpec for each end - see AddDomeCap()'s own doc comment for
  // why the dome needs the EXACT center/radius/pole this function
  // already knows, rather than re-deriving them from the wall.
  if (!cap) Fail(caller, "round_caps requires cap");
  if (r.IsClosed()) Fail(caller, "round_caps requires an open rail - a closed tube has no ends to dome");
  if (stations < 2) Fail(caller, "stations must be at least 2");
  const bool straight = r.IsLinear(1e-9 * CurveScale(r));
  const int m = straight ? 2 : std::max(stations, 3);
  std::vector<double> params = rail.DivideByCount(m - 1);
  if (static_cast<int>(params.size()) != m) Internal(caller, "station count mismatch");
  const std::vector<Frame> frames = RmfFrames(r, params, /*wrap=*/false, caller);

  std::vector<ON_NurbsCurve> copies;
  copies.reserve(static_cast<size_t>(m));
  for (int k = 0; k < m; ++k) {
    ON_NurbsCurve ck = nurbs_circle;
    if (k > 0) ck.Transform(FrameToFrame(frames[0], frames[static_cast<size_t>(k)]));
    copies.push_back(std::move(ck));
  }
  MakeCompatible(copies, caller);  // no-op for rigid copies of the same circle; keeps one code path
  std::unique_ptr<ON_NurbsSurface> wall;
  if (m == 2) {
    wall = RuledBetween(copies[0], copies[1], 0.0, 1.0, caller);
  } else {
    double period = 1.0;
    const std::vector<double> params_v = SkinParameters(copies, /*closed=*/false, &period, caller);
    wall = SkinSections(copies, std::min(3, m - 1), /*closed=*/false, params_v, period, caller);
  }
  const RoundCapSpec spec0{frames.front().origin, -frames.front().t, radius};
  const RoundCapSpec spec1{frames.back().origin, frames.back().t, radius};
  return AssembleSweptBody(wall.release(), /*cap_v0=*/true, /*cap_v1=*/true, /*cap_u0=*/false, /*cap_u1=*/false,
                           caller, nullptr, nullptr, &spec0, &spec1);
}

Brep Brep::PipeVariable(const NurbsCurve& rail_in, const std::vector<std::pair<double, double>>& radius_points,
                        bool cap, int stations, bool round_caps) {
  const char* caller = "PipeVariable";
  if (stations < 2) Fail(caller, "stations must be at least 2");
  if (radius_points.size() < 2) Fail(caller, "at least 2 radius points are required");
  for (size_t i = 0; i < radius_points.size(); ++i) {
    const double t = radius_points[i].first, r = radius_points[i].second;
    if (!std::isfinite(t) || t < 0.0 || t > 1.0) {
      Fail(caller, "radius point " + std::to_string(i) + " has t outside [0, 1]");
    }
    if (!(r > 0.0)) Fail(caller, "radius point " + std::to_string(i) + " has a non-positive radius");
    if (i > 0 && !(t > radius_points[i - 1].first)) {
      Fail(caller, "radius points must be strictly increasing in t");
    }
  }

  ON_NurbsCurve rail = rail_in.raw();
  if (!rail.IsValid()) Fail(caller, "rail is not a valid NURBS curve");
  const bool wrap = rail.IsClosed();
  if (wrap && std::fabs(radius_points.front().second - radius_points.back().second) > 1e-9 * CurveScale(rail)) {
    Fail(caller, "a closed rail needs equal radius at t = 0 and t = 1 (the tube must meet itself at the seam)");
  }
  if (round_caps && !cap) Fail(caller, "round_caps requires cap");
  if (round_caps && wrap) Fail(caller, "round_caps requires an open rail - a closed tube has no ends to dome");

  const double total_length = rail_in.Length();
  if (!(total_length > 0.0)) Fail(caller, "the rail has zero length");

  // Exact case: exactly 2 radius points spanning the whole rail (t = 0 and
  // t = 1) on a STRAIGHT rail is the exact rational cone frustum wall -
  // Sweep1()'s own straight-rail shortcut (RuledBetween at 2 stations,
  // degree 1), only with the two end circles built at their own radius
  // instead of copies of one section. `stations` does not matter here,
  // exactly as it does not for Sweep1() along a straight rail.
  const bool exact_two_point_taper = !wrap && radius_points.size() == 2 && radius_points.front().first == 0.0 &&
                                     radius_points.back().first == 1.0 && rail.IsLinear(1e-9 * CurveScale(rail));

  std::vector<double> merged;
  std::vector<double> params;
  if (exact_two_point_taper) {
    merged = {0.0, 1.0};
    params = {rail.Domain().Min(), rail.Domain().Max()};
  } else {
    // Station fractions: `stations` spaced evenly in arc length, plus
    // every radius point's own fraction so the tube's radius matches it
    // exactly.
    const int m_even = std::max(stations, 3);
    std::vector<double> fractions;
    fractions.reserve(static_cast<size_t>(m_even) + radius_points.size());
    for (int k = 0; k < m_even; ++k) fractions.push_back(static_cast<double>(k) / static_cast<double>(m_even - 1));
    for (const auto& rp : radius_points) fractions.push_back(rp.first);
    std::sort(fractions.begin(), fractions.end());
    for (double f : fractions) {
      if (merged.empty() || f - merged.back() > 1e-9) merged.push_back(f);
    }
    if (wrap && merged.size() > 1 && merged.back() >= 1.0 - 1e-9) merged.pop_back();  // t=1 is t=0 on a closed rail
    if (merged.size() < 2) Fail(caller, "too few distinct stations - the radius points are too close together");
    params.resize(merged.size());
    for (size_t k = 0; k < merged.size(); ++k) params[k] = rail_in.ParameterAtArcLength(merged[k] * total_length);
  }
  const int m = static_cast<int>(merged.size());
  const std::vector<Frame> frames = RmfFrames(rail, params, wrap, caller);

  // Piecewise-linear radius at an arbitrary arc-length fraction, held
  // flat at the nearest endpoint's radius outside the given range.
  auto radius_at = [&](double f) {
    if (f <= radius_points.front().first) return radius_points.front().second;
    if (f >= radius_points.back().first) return radius_points.back().second;
    for (size_t i = 1; i < radius_points.size(); ++i) {
      if (f <= radius_points[i].first) {
        const double t0 = radius_points[i - 1].first, t1 = radius_points[i].first;
        const double r0 = radius_points[i - 1].second, r1 = radius_points[i].second;
        return r0 + (f - t0) / (t1 - t0) * (r1 - r0);
      }
    }
    return radius_points.back().second;  // unreachable given the f >= back() check above
  };

  std::vector<ON_NurbsCurve> sections;
  sections.reserve(static_cast<size_t>(m));
  for (int k = 0; k < m; ++k) {
    const Frame& f = frames[static_cast<size_t>(k)];
    const ON_Plane plane(f.origin, f.r, f.s);
    const ON_Circle circle(plane, radius_at(merged[static_cast<size_t>(k)]));
    ON_NurbsCurve nc;
    if (circle.GetNurbForm(nc) == 0) Internal(caller, "ON_Circle::GetNurbForm failed");
    sections.push_back(std::move(nc));
  }
  // Every station is the same ON_Circle::GetNurbForm() representation
  // (same degree/knots/weights, only plane and radius differ), so this is
  // effectively a no-op - kept for the same reason Sweep1() keeps it on
  // its own rigid copies: one code path, and a real check if that ever
  // stops being true.
  MakeCompatible(sections, caller);

  const bool want_caps = cap && !wrap;
  std::unique_ptr<ON_NurbsSurface> wall;
  if (m == 2) {
    // The exact ruled surface between the two end circles - identical to
    // Loft()/Sweep1()'s own 2-section shortcut, and the only path
    // `exact_two_point_taper` above takes.
    wall = RuledBetween(sections[0], sections[1], 0.0, 1.0, caller);
  } else {
    double period = 1.0;
    const std::vector<double> params_v = SkinParameters(sections, wrap, &period, caller);
    wall = SkinSections(sections, std::min(3, m - 1), wrap, params_v, period, caller);
  }
  if (!round_caps) return AssembleSweptBody(wall.release(), want_caps, want_caps, false, false, caller);
  // Each dome is sized to its OWN end's local radius (radius_at() at that
  // end's exact merged fraction, matching sections[0]/sections.back()'s
  // own circle radius bit-for-bit) rather than assuming Pipe()'s single
  // constant radius - the two ends need not match.
  const RoundCapSpec spec0{frames.front().origin, -frames.front().t, radius_at(merged.front())};
  const RoundCapSpec spec1{frames.back().origin, frames.back().t, radius_at(merged.back())};
  return AssembleSweptBody(wall.release(), /*cap_v0=*/true, /*cap_v1=*/true, /*cap_u0=*/false, /*cap_u1=*/false,
                           caller, nullptr, nullptr, &spec0, &spec1);
}

Brep Brep::PipeThickWalled(const NurbsCurve& rail_in, double outer_radius, double inner_radius, bool cap,
                          int stations) {
  const char* caller = "PipeThickWalled";
  if (!(outer_radius > 0.0)) Fail(caller, "outer_radius must be positive");
  if (!(inner_radius > 0.0)) Fail(caller, "inner_radius must be positive");
  if (!(inner_radius < outer_radius)) Fail(caller, "inner_radius must be less than outer_radius");
  if (stations < 2) Fail(caller, "stations must be at least 2");
  ON_NurbsCurve rail = rail_in.raw();
  if (!rail.IsValid()) Fail(caller, "rail is not a valid NURBS curve");

  const bool wrap = rail.IsClosed();
  const bool straight = !wrap && rail.IsLinear(1e-9 * CurveScale(rail));
  const int m = straight ? 2 : std::max(stations, 3);

  std::vector<double> params = rail_in.DivideByCount(wrap ? m : m - 1);
  if (wrap) params.pop_back();
  if (static_cast<int>(params.size()) != m) Internal(caller, "station count mismatch");
  const std::vector<Frame> frames = RmfFrames(rail, params, wrap, caller);

  // Outer and inner circle at every station, both centered on the rail
  // in the plane perpendicular to it there (Pipe()'s own convention).
  // The inner circle is built REVERSED so its wall's own u x v normal
  // faces the opposite way from the outer wall's - into the bore rather
  // than away from the axis - by construction, the same "traverse the
  // boundary the other way to flip the face" idea `Extrude()`'s own
  // orientation logic and `AssembleSweptBody()`'s cap-reversal already
  // rely on, not a separate flip applied after the fact.
  //
  // `inner` (unreversed) is kept SEPARATE from `inner_for_wall`
  // (reversed): the annular caps below rule directly between `outer[k]`
  // and `inner[k]` at the SAME parameter u, which is only a flat,
  // non-self-overlapping annulus when the two circles are ANGULARLY
  // ALIGNED at every u, not just at u = 0 - true for `outer`/`inner`
  // (both traversed the same, CCW, way) but false for a reversed inner
  // circle (u = 0 still lands on the same point by periodicity, but any
  // OTHER u then lands on the physically OPPOSITE angle from `outer`'s
  // own u there, twisting the ruled cap into a self-overlapping shape).
  // Found by measuring, not assumed: an early version built the caps
  // from the already-reversed inner circle and its own annulus reported
  // an area of ~41.5 against the true pi*(R^2 - r^2) ~= 15.7 - a clear
  // sign of self-overlap, not a rounding error.
  std::vector<ON_NurbsCurve> outer(static_cast<size_t>(m)), inner(static_cast<size_t>(m));
  for (int k = 0; k < m; ++k) {
    const ON_Plane plane(frames[static_cast<size_t>(k)].origin, frames[static_cast<size_t>(k)].r, frames[static_cast<size_t>(k)].s);
    const ON_Circle oc(plane, outer_radius);
    if (oc.GetNurbForm(outer[static_cast<size_t>(k)]) == 0) Internal(caller, "ON_Circle::GetNurbForm failed (outer)");
    const ON_Circle ic(plane, inner_radius);
    if (ic.GetNurbForm(inner[static_cast<size_t>(k)]) == 0) Internal(caller, "ON_Circle::GetNurbForm failed (inner)");
  }
  MakeCompatible(outer, caller);
  MakeCompatible(inner, caller);
  std::vector<ON_NurbsCurve> inner_for_wall = inner;
  for (ON_NurbsCurve& c : inner_for_wall) {
    if (!c.Reverse()) Internal(caller, "reversing the inner circle failed");
  }

  auto build_wall = [&](std::vector<ON_NurbsCurve>& copies) -> std::unique_ptr<ON_NurbsSurface> {
    if (m == 2) return RuledBetween(copies[0], copies[1], 0.0, 1.0, caller);
    double period = 1.0;
    const std::vector<double> params_v = SkinParameters(copies, wrap, &period, caller);
    return SkinSections(copies, std::min(3, m - 1), wrap, params_v, period, caller);
  };
  std::unique_ptr<ON_NurbsSurface> outer_wall = build_wall(outer);
  std::unique_ptr<ON_NurbsSurface> inner_wall = build_wall(inner_for_wall);

  // Assembly: two independent walls (each periodic in its own u, so
  // ON_Brep::NewFace's own bIsClosed(0) handling merges its east/west
  // sides into one seam edge automatically - the same mechanism every
  // other circular wall in this file already relies on), sharing edges
  // with the two annular caps at their own south (v = 0, near) and
  // north (v = 1, far) sides.
  Brep result;
  ON_Brep& brep = result.raw();
  int vid[4] = {-1, -1, -1, -1}, eid[4] = {-1, -1, -1, -1};
  bool rev[4] = {false, false, false, false};
  ON_BrepFace* outer_face = brep.NewFace(outer_wall.release(), vid, eid, rev);
  if (!outer_face) Internal(caller, "ON_Brep::NewFace refused the outer wall");
  const int outer_south = eid[0], outer_north = eid[2];

  vid[0] = vid[1] = vid[2] = vid[3] = -1;
  eid[0] = eid[1] = eid[2] = eid[3] = -1;
  rev[0] = rev[1] = rev[2] = rev[3] = false;
  ON_BrepFace* inner_face = brep.NewFace(inner_wall.release(), vid, eid, rev);
  if (!inner_face) Internal(caller, "ON_Brep::NewFace refused the inner wall");
  const int inner_south = eid[0], inner_north = eid[2];

  int faces = 2;
  const bool want_caps = cap && !wrap;
  if (want_caps) {
    // Each annular cap rules directly between `outer[si]` and `inner[si]`
    // (the ANGULARLY ALIGNED, unreversed pair - see the comment above on
    // why `inner`, not `inner_for_wall`, is used here) and shares its own
    // outer/inner boundary with the matching wall's already-built edge -
    // the same "second face reuses the first face's real edge" idea
    // `AddFanCap()` and every other cap in this file already rely on.
    for (int end = 0; end < 2; ++end) {
      const size_t si = static_cast<size_t>(end == 0 ? 0 : m - 1);
      // The near and far caps are NOT mirror images built from one
      // formula: since the far circles are plain translates of the near
      // ones (no reversal between them), RuledBetween(inner, outer)
      // gives the SAME local outward direction at both ends - correct
      // for exactly one of them. Verified directly (Ev1Der + cross
      // product against the known straight-rail outward directions
      // (0, 0, -1) near / (0, 0, +1) far): RuledBetween(inner, outer)
      // is the near cap's own correct orientation; the far cap needs
      // the arguments swapped, RuledBetween(outer, inner).
      std::unique_ptr<ON_NurbsSurface> annulus =
          end == 0 ? RuledBetween(inner[si], outer[si], 0.0, 1.0, caller) : RuledBetween(outer[si], inner[si], 0.0, 1.0, caller);
      vid[0] = vid[1] = vid[2] = vid[3] = -1;
      eid[0] = end == 0 ? inner_south : outer_north;
      eid[1] = -1;
      eid[2] = end == 0 ? outer_south : inner_north;
      eid[3] = -1;
      // bRev3d values were derived empirically (sweeping all 4
      // combinations of {rev[0], rev[2]} on a straight-rail fixture and
      // keeping the one where raw().IsValid() and raw().IsSolid() both
      // hold - the same discipline `AddFanCap()`'s own rule was
      // established with, not assumed from theory alone): both trims
      // run OPPOSITE to how the matching wall recorded that edge.
      rev[0] = true;
      rev[1] = false;
      rev[2] = true;
      rev[3] = false;
      if (!brep.NewFace(annulus.release(), vid, eid, rev)) {
        Internal(caller, end == 0 ? "ON_Brep::NewFace refused the near annular cap"
                                  : "ON_Brep::NewFace refused the far annular cap");
      }
      ++faces;
    }
  }

  brep.SetTolerancesBoxesAndFlags(/*bLazy=*/true);
  result.AppendUntrimmedFaceSideTables(faces);

  if (brep.IsSolid()) {
    const Mesh check = result.TessellateToClosedMesh(16, 16);
    if (check.Volume() < 0.0) brep.Flip();
  }
  return result;
}

Brep Brep::ScrewThread(Point3d axis_point, Vector3d axis_direction, double minor_radius, double major_radius,
                       double pitch, double turns, bool right_handed, int stations_per_turn) {
  const char* caller = "ScrewThread";
  if (!(minor_radius > 0.0)) Fail(caller, "minor_radius must be positive");
  if (!(major_radius > minor_radius)) Fail(caller, "major_radius must exceed minor_radius");
  if (!(pitch > 0.0)) Fail(caller, "pitch must be positive");
  if (!(turns > 0.0)) Fail(caller, "turns must be positive");
  if (stations_per_turn < 4) Fail(caller, "stations_per_turn must be at least 4");
  ON_3dVector axis = axis_direction;
  if (!axis.Unitize()) Fail(caller, "axis_direction must be nonzero");

  // Canonical perpendicular reference at angle 0 - the same point+normal
  // ON_Plane convention Pipe() itself uses to seed its own start frame.
  const ON_Plane base(axis_point, axis);
  const double half_pitch = pitch / 2.0;
  const double sign = right_handed ? 1.0 : -1.0;
  const int m = std::max(4, static_cast<int>(std::ceil(turns * stations_per_turn)) + 1);

  std::vector<ON_NurbsCurve> sections;
  sections.reserve(static_cast<size_t>(m));
  for (int k = 0; k < m; ++k) {
    const double frac = static_cast<double>(k) / static_cast<double>(m - 1);
    const double height = frac * pitch * turns;
    const double angle = sign * 2.0 * ON_PI * turns * frac;
    const ON_3dVector radial = base.xaxis * std::cos(angle) + base.yaxis * std::sin(angle);
    const ON_Plane plane(axis_point + axis * height, radial, axis);
    const ON_3dPoint p0 = plane.PointAt(minor_radius, -half_pitch);
    const ON_3dPoint p1 = plane.PointAt(major_radius, 0.0);
    const ON_3dPoint p2 = plane.PointAt(minor_radius, half_pitch);
    NurbsCurve tri = NurbsCurve::FromControlPoints({p0, p1, p2, p0}, 1);
    sections.push_back(tri.raw());
  }
  MakeCompatible(sections, caller);
  double period = 1.0;
  const std::vector<double> params_v = SkinParameters(sections, /*closed=*/false, &period, caller);
  std::unique_ptr<ON_NurbsSurface> wall =
      SkinSections(sections, std::min(3, m - 1), /*closed=*/false, params_v, period, caller);

  // Explicit cap-orientation hints - NOT the wall surface's own `Ev1Der`
  // boundary derivative AssembleSweptBody() defaults to: every per-u-
  // column of THIS wall (any fixed radius between minor_radius and
  // major_radius) traces a full sin/cos period in x/y over the course of
  // even a single turn, and a global interpolating skin's (SkinSections)
  // own boundary derivative for that kind of column came out both
  // numerically oversized and, confirmed directly (disabling each cap in
  // turn isolated it to the cap/wall join, not the wall itself, which
  // tests orientation-consistent alone at every turn count tried), wrong
  // in sign - a case Sweep1()/Pipe()'s existing rails never hit (no
  // existing rail motion is periodic in an individual coordinate over
  // its own whole sweep).
  //
  // The cap's own plane is, by this construction, always exactly the one
  // spanned by the radial direction and `axis` itself, so its normal has
  // no axial component at all - only the TANGENTIAL (circumferential)
  // direction at that end's own station angle matters, and `phi_hat`
  // (the derivative of `radial` with respect to angle) gives it exactly,
  // no spline involved - `angle_last` already carries `sign` (the
  // handedness), so `phi_hat(angle_last)` alone reflects it; no separate
  // sign factor on the hint itself. That this is the right pair (and not
  // one of the other 3 sign combinations) was settled empirically, not
  // derived: checked directly against a real fixture, BOTH handedness,
  // at 3 different tessellation divisions each, for both an orientation-
  // consistent closed manifold and the expected positive volume (see
  // TestScrewThreadExactHelicalSweepVolumeAndGeometry, test_basic.cpp) -
  // every other combination tried left a genuine mis-wound cap on at
  // least one end for at least one handedness (a directed mesh edge
  // walked the same way by two faces - including, confusingly, one
  // combination that mis-wound only the LEFT-handed case while the
  // right-handed one looked fine), confirmed via Mesh::IsClosedManifold()'s
  // own DINO8_MESH_DEBUG diagnostic, not assumed from a sign argument
  // that turned out unreliable here. This pair is locally consistent
  // (no edge conflict either handedness) but, unlike a plain Pipe()'s
  // own always-natively-outward construction, still lands net-INWARD as
  // a whole body here - AssembleSweptBody()'s own final tessellated-
  // volume-sign check (the same safety net every sweep factory in this
  // file already ends with) is what flips it, setting every face's own
  // m_bRev in the process; that is why ScrewThread(), alone among this
  // file's factories, needs its own topology check rather than the
  // shared `CheckSolidTopology()` helper's stricter "no flip was needed"
  // assertion (see TestScrewThreadExactHelicalSweepVolumeAndGeometry).
  auto phi_hat = [&](double angle) { return -base.xaxis * std::sin(angle) + base.yaxis * std::cos(angle); };
  const double angle_last = sign * 2.0 * ON_PI * turns;
  const ON_3dVector hint_v0 = phi_hat(0.0);
  const ON_3dVector hint_v1 = -phi_hat(angle_last);
  return AssembleSweptBody(wall.release(), /*cap_v0=*/true, /*cap_v1=*/true, false, false, caller, &hint_v0, &hint_v1);
}

}  // namespace dino8::kernel
