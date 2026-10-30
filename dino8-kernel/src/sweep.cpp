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
//   - AddPlanarTrimmedCap(): Loft()'s end caps - ONE planar trimmed face
//     per end (plane patch + the section as its outer trim, mated to the
//     wall's boundary edge) instead of a fan; the fan is then only the
//     kernel's own on-the-fly analysis surface (detail::PlanarCapFanSurface).
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

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/detail/cap_fan.h"
#include "dino8/kernel/detail/segment3d.h"
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

// One PLANAR TRIMMED cap added to `brep`: a single planar face whose
// surface is a bilinear patch in the section plane's own (x, y) frame
// (u = x, v = y, big enough to contain the section with a margin) and
// whose one outer loop is one trim running along `shared_edge` - the wall's
// own closed boundary edge. The same "plane surface + the section curve as
// its outer trim" representation Brep::TrimmedPatchFace() builds for a
// bare face, but here the trim is MATED to the wall's existing edge, so the
// pair closes into a real shared-topology solid.
//
// Why this instead of AddFanCap()'s fan for a lofted solid: a fan is a
// surface COLLAPSED to a point (its whole apex side is one singular trim),
// and a face-by-face (u, v) mesher that triangulates that degenerate
// parameter rectangle - dino8-app's BrepMesher, which is what the app's
// MeshOf()/display/analysis/booleans go through - fails to close it for
// some polygonal sections (a square's 9 CDT constraints fail: wall and cap
// then no longer share a watertight boundary, the mesh reads open, Volume
// 0). A planar trimmed face has no singular side at all: it meshes like
// every ordinary trimmed face.
//
// `boundary` is the wall's own iso curve at that end (its parameter
// direction); the face's outward normal is the plane normal signed to agree
// with `outward_hint`, and the loop is oriented counterclockwise about it.
// The trim's 3D-reversal flag against the shared edge follows from whether
// the loop runs with or against `boundary` and whether the edge runs with
// it (`edge_forward`). The edge's tolerance is raised to the measured
// trim-image-to-edge deviation.
void AddPlanarTrimmedCap(ON_Brep& brep, const ON_NurbsCurve& boundary, int shared_edge, bool edge_forward,
                         ON_3dVector outward_hint, const char* caller) {
  ON_Plane plane;
  const double scale = CurveScale(boundary);
  if (!boundary.IsPlanar(&plane, 1e-8 * scale)) {
    Fail(caller, "cap requested but the end section is not planar - cannot cap a non-planar end");
  }
  ON_3dVector n = plane.zaxis;
  const double agree = ON_DotProduct(n, outward_hint);
  if (std::fabs(agree) <= 1e-9 * outward_hint.Length()) {
    Fail(caller, "the cap plane is parallel to the sweep direction at that end - cannot cap it");
  }
  if (agree < 0.0) n = -n;
  // The frame's x axis: the world axis most nearly IN the plane, projected
  // into it, so the patch for a section in an axis-aligned plane is the
  // world-aligned bounding rectangle of the section - which keeps the
  // B-rep's own bounding box (ON derives a trimmed face's box from its
  // surface patch) exactly the section's rather than a rotated parallelogram's.
  ON_3dVector xaxis;
  {
    const ON_3dVector axes[3] = {ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0), ON_3dVector(0, 0, 1)};
    int best = 0;
    for (int k = 1; k < 3; ++k) {
      if (std::fabs(ON_DotProduct(axes[k], n)) < std::fabs(ON_DotProduct(axes[best], n)) - 1e-12) best = k;
    }
    xaxis = axes[best] - ON_DotProduct(axes[best], n) * n;
    if (!xaxis.Unitize()) xaxis = plane.xaxis;
  }
  ON_3dVector yaxis = ON_CrossProduct(n, xaxis);  // xaxis x yaxis = n: the surface's own u x v is the outward normal
  yaxis.Unitize();
  const ON_3dPoint origin = plane.origin;

  // The section in the plane's own (x, y) coordinates: same knots and
  // weights, every control point mapped by the plane's affine projection -
  // exact for a planar curve (affine maps preserve NURBS form).
  const bool rational = boundary.IsRational();
  ON_NurbsCurve c2(2, rational, boundary.Order(), boundary.CVCount());
  for (int k = 0; k < boundary.KnotCount(); ++k) c2.SetKnot(k, boundary.Knot(k));
  double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;
  for (int i = 0; i < boundary.CVCount(); ++i) {
    const ON_3dVector d = EuclideanCV(boundary, i) - origin;
    const double x = ON_DotProduct(d, xaxis), y = ON_DotProduct(d, yaxis);
    if (i == 0) {
      x0 = x1 = x;
      y0 = y1 = y;
    }
    x0 = std::min(x0, x);
    x1 = std::max(x1, x);
    y0 = std::min(y0, y);
    y1 = std::max(y1, y);
    if (rational) {
      const double w = boundary.Weight(i);
      c2.SetCV(i, ON_4dPoint(x * w, y * w, 0.0, w));
    } else {
      c2.SetCV(i, ON_3dPoint(x, y, 0.0));
    }
  }
  // The loop must be counterclockwise in (u, v) (outer loop, face normal =
  // u x v). The wall's sections run counterclockwise about the SWEEP
  // direction, so at the start cap (normal opposite the sweep) the loop
  // has to run against `boundary` and at the end cap with it - decided
  // from the signed area rather than assumed.
  bool loop_with_boundary = true;
  {
    const ON_Interval dom = c2.Domain();
    std::vector<ON_2dPoint> poly;
    const int m = std::max(256, 32 * boundary.SpanCount());
    for (int i = 0; i < m; ++i) {
      const ON_3dPoint p = c2.PointAt(dom.ParameterAt(static_cast<double>(i) / m));
      poly.emplace_back(p.x, p.y);
    }
    if (SignedArea2d(poly) < 0.0) {
      c2.Reverse();
      loop_with_boundary = false;
    }
  }

  // The patch is exactly the section's control-point bounding rectangle in
  // this frame (zero margin): the B-rep's bounding box is then the
  // section's own, not inflated. The trim touching the domain edge at its
  // extreme points is ordinary (it is one trim of one non-iso curve, so the
  // face is still a trimmed face, never a "whole surface" one). A fully
  // flat section (zero extent in x or y) is refused by the area check.
  if (!(x1 - x0 > 0.0) || !(y1 - y0 > 0.0)) Fail(caller, "cap requested but the end section encloses no area");
  auto* srf = new ON_NurbsSurface(3, false, 2, 2, 2, 2);
  srf->SetKnot(0, 0, x0);
  srf->SetKnot(0, 1, x1);
  srf->SetKnot(1, 0, y0);
  srf->SetKnot(1, 1, y1);
  const double us[2] = {x0, x1}, vs[2] = {y0, y1};
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) srf->SetCV(i, j, origin + us[i] * xaxis + vs[j] * yaxis);
  }

  // How far the surface's image of the 2D trim strays from the shared
  // edge's own 3D curve: the section is planar only to 1e-8 of its scale,
  // so this is the honest edge tolerance, not an assumed zero.
  double deviation = 0.0;
  {
    const ON_Curve* edge_curve = brep.m_E[shared_edge].EdgeCurveOf();
    if (edge_curve == nullptr) Internal(caller, "the shared wall edge has no 3D curve");
    const ON_Interval ed = edge_curve->Domain();
    const ON_Interval td = c2.Domain();
    // c2 runs with `boundary` iff loop_with_boundary; the edge runs with
    // `boundary` iff edge_forward; pair the samples by 3D position.
    const bool trim_runs_with_edge = (loop_with_boundary == edge_forward);
    for (int k = 0; k <= 400; ++k) {
      const double f = static_cast<double>(k) / 400.0;
      const ON_3dPoint uv = c2.PointAt(td.ParameterAt(f));
      const ON_3dPoint e = edge_curve->PointAt(ed.ParameterAt(trim_runs_with_edge ? f : 1.0 - f));
      deviation = std::max(deviation, srf->PointAt(uv.x, uv.y).DistanceTo(e));
    }
  }

  const int si = brep.AddSurface(srf);
  const int c2i = brep.AddTrimCurve(new ON_NurbsCurve(c2));
  ON_BrepFace& face = brep.NewFace(si);
  ON_BrepLoop& loop = brep.NewLoop(ON_BrepLoop::outer, face);
  const bool trim_rev3d = (loop_with_boundary != edge_forward);
  ON_BrepTrim& trim = brep.NewTrim(brep.m_E[shared_edge], trim_rev3d, loop, c2i);
  trim.m_type = ON_BrepTrim::mated;
  trim.m_tolerance[0] = trim.m_tolerance[1] = 0.0;
  if (brep.LoopDirection(loop) < 0) Internal(caller, "the planar cap's outer loop came out clockwise");
  brep.SetTrimIsoFlags(face);
  ON_BrepEdge& edge = brep.m_E[shared_edge];
  if (!(edge.m_tolerance >= 0.0) || edge.m_tolerance < deviation) edge.m_tolerance = deviation;
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

namespace detail {

// See detail/cap_fan.h. The fan AddFanCap() would have built for this
// boundary and outward direction, without touching any B-rep.
std::unique_ptr<ON_NurbsSurface> PlanarCapFanSurface(const ON_NurbsCurve& boundary_in,
                                                     const ON_3dVector& outward_normal) {
  try {
    ON_NurbsCurve boundary = boundary_in;
    if (!boundary.IsValid() || !boundary.IsClosed()) return nullptr;
    const CapPlan plan = PlanCap(boundary, /*chord_closed=*/false, "PlanarCapFanSurface");
    const ON_3dVector native = plan.signed_area > 0.0 ? -plan.plane.zaxis : plan.plane.zaxis;
    if (ON_DotProduct(native, outward_normal) < 0.0) ReverseKeepDomain(boundary);
    return FanSurface(boundary, plan.apex, "PlanarCapFanSurface");
  } catch (const std::exception&) {
    return nullptr;
  }
}

}  // namespace detail

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
                             const RoundCapSpec* round_v1, bool planar_trimmed_caps) {
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
        if (planar_trimmed_caps && !chord) {
          AddPlanarTrimmedCap(brep, *b0, eid[0], /*edge_forward=*/true, hint, caller);
        } else {
          first = AddFanCap(brep, *b0, eid[0], /*edge_forward=*/true, hint, plan0, nullptr, /*transpose=*/false, caller);
          have_first = true;
        }
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
        if (planar_trimmed_caps && !chord) {
          AddPlanarTrimmedCap(brep, *b1, eid[2], /*edge_forward=*/false, hint, caller);
        } else {
          AddFanCap(brep, *b1, eid[2], /*edge_forward=*/false, hint, plan1, (chord && have_first) ? &first : nullptr,
                    /*transpose=*/false, caller);
        }
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
  // rule was wrong for this input, and flipping is the safe repair. A
  // fixed 16x16 check (every other caller of this idiom in this file
  // still uses it) is adequate for an ordinary low-span wall, but a wall
  // with many more spans than that in one direction - confirmed directly
  // via ScrewThread() at bolt-shank turn counts (dino8_scratch_test: a
  // 32-turn, stations_per_turn=16 wall has 513 v-spans) - aliases badly
  // enough at 16x16 that the SIGN of the sampled volume can come out
  // wrong, so this safety net flips (or fails to flip) the wrong way and
  // bakes a genuinely inside-out solid in - confirmed by `brep.IsValid()`
  // staying true throughout (this is an orientation defect, not a
  // topological one) while the exact `Brep::Volume()` integral comes back
  // negative. Scaling each direction's own check resolution with the
  // wall's own `SpanCount()` there (never below 16, so every existing
  // low-span caller - Pipe/Revolve/Extrude/Loft/Sweep1/Sweep2/... - keeps
  // the identical 16x16 check and is unaffected) fixes this without
  // touching the other, as-yet-unconfirmed, identical-idiom checks
  // elsewhere in this file.
  if (brep.IsSolid()) {
    const int du_check = std::max(16, 2 * wall.SpanCount(0));
    const int dv_check = std::max(16, 2 * wall.SpanCount(1));
    const Mesh m = result.TessellateToClosedMesh(du_check, dv_check);
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

// Exact, closed-form in-plane offset of a SIMPLE (non-self-intersecting)
// degree-1 polyline (open or closed, non-rational, at least 3 distinct
// vertices, not reducible to a single line or arc - callers filter for
// that), convex OR concave, by `distance`, along the same "edge tangent x
// plane.zaxis" convention NurbsCurve::OffsetInPlane()'s own Line/Arc
// cases use (distance > 0 grows).
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
// The miter-join formula above is exact regardless of local convexity, so
// a concave (reflex) corner is not special-cased away here - each vertex
// still gets its own exact bisector point. Two validity checks are
// applied unconditionally below instead of a convexity pre-check: (1) a
// cheap local one - every offset edge must stay a POSITIVE multiple of
// its own original direction, catching an edge that inverted or
// collapsed - and (2) a global one - no two non-adjacent offset edges may
// come closer than a small tolerance, catching a self-intersection a
// concave polygon's offset can introduce far from any single corner (a
// convex polygon's offset can never fail this: its edges turn
// monotonically by exactly 2*pi, which cannot self-cross). Together these
// two checks are the general "is this a simple polygon" test, not a
// convexity proxy, so both convex and concave SIMPLE inputs pass and any
// genuinely self-intersecting result - simple input or not - is caught
// and reported rather than silently returned.
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

  // Validity (local): every offset edge must be a positive multiple of
  // its own original direction - catches an edge that inverted or
  // collapsed.
  const double length_floor = 1e-12 * CurveScale(c);
  for (int i = 0; i < edge_count; ++i) {
    const ON_3dVector e = out[static_cast<size_t>((i + 1) % vcount)] - out[static_cast<size_t>(i)];
    if (ON_DotProduct(e, edir[static_cast<size_t>(i)]) <= length_floor) {
      Fail(caller,
           "the draft angle/height shrinks the profile past its own inradius - an edge would invert or collapse; "
           "use a smaller draft angle, a shorter extrusion, or a larger profile");
    }
  }

  // Validity (global): no two non-adjacent offset edges may pass within
  // tolerance of each other - the general simple-polygon test that
  // catches a self-intersection a concave corner's offset can introduce
  // far from itself (see this function's own doc comment; a convex
  // input's offset can never trigger this).
  {
    const double gap_tol = 1e-9 * CurveScale(c);
    const double gap_tol2 = gap_tol * gap_tol;
    for (int i = 0; i < edge_count; ++i) {
      const Point3d& p0 = out[static_cast<size_t>(i)];
      const Point3d& p1 = out[static_cast<size_t>((i + 1) % vcount)];
      for (int j = i + 1; j < edge_count; ++j) {
        if (j == i + 1) continue;
        if (closed && i == 0 && j == edge_count - 1) continue;
        const Point3d& q0 = out[static_cast<size_t>(j)];
        const Point3d& q1 = out[static_cast<size_t>((j + 1) % vcount)];
        double s = 0.0, t = 0.0;
        if (detail::ClosestSegmentSegment(p0, p1, q0, q1, s, t) <= gap_tol2) {
          Fail(caller,
               "the offset profile self-intersects - a concave corner's offset crossed a non-adjacent edge; use "
               "a smaller draft angle/offset distance or a simpler profile");
        }
      }
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
    // A genuine multi-segment polyline: this kernel's own exact miter
    // offset (convex or concave input), not OffsetInPlane()'s general
    // least-squares branch (see ExtrudeTapered()'s own brep.h doc comment
    // for why).
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
  const bool trimmed = trim_table_ok && (!body.face_trim_loops_[static_cast<size_t>(face_index)].empty() ||
                                         !body.face_hole_loops_[static_cast<size_t>(face_index)].empty());
  if (!trim_table_ok && (body.face_trim_loops_.empty() || body.face_hole_loops_.empty())) {
    // Side tables entirely unpopulated (some general operation cleared
    // them wholesale - see GetTightBoundingBox()'s own "trim_table_ok"
    // caveat): can't tell trimmed from untrimmed, so refused rather than
    // silently assumed untrimmed.
    Fail(caller,
         "this Brep's own trim side tables are not populated, so whether face_index's face is trimmed can't be "
         "determined - not safe to extrude either way");
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

  if (!trimmed && !body.FaceCoversWholeDomain(face_index)) {
    // A face trimmed by genuine ON_Brep loop topology only (no pseudo-trim
    // side-table polygon): a capped Loft()'s planar trimmed cap is one - its
    // plane patch is bigger than the cap itself, so "extrude the surface's
    // 4 domain isocurves" below would silently build a box on the PADDED
    // patch, not on the cap. Refused rather than misbuilt.
    Fail(caller,
         "the face is trimmed by real ON_Brep loop topology that this extrusion cannot read (only the pseudo-trim "
         "side tables are honored) - e.g. a capped Loft()'s planar trimmed end cap; extruding its surface's domain "
         "rectangle would not be the face");
  }
  if (trimmed) {
    // A trimmed face's real boundary is not its surface's 4 domain
    // isocurves, so the untrimmed path's "4 RuledBetween() walls between
    // matching domain isocurves" construction below does not apply.
    // Scoped down to a PLANAR trimmed face with no holes: the trim loop
    // (`face_trim_loops_`, a polygon in the surface's own (u, v) - see
    // TrimmedPlanarFace()'s own doc comment) maps through a PLANAR
    // surface to a genuine 3D polygon, so both caps and every wall are
    // exact flat facets, built as `Brep::PlanarFace` entries and welded
    // into ONE real, `IsSolid()`-true solid by a single
    // `Brep::FromPlanarFaces()` call - the exact same construction
    // `ExtrudeToBoundary()`'s own N-gon-profile fix already uses and
    // verifies, not a new one. A non-planar trimmed surface (a trimmed
    // patch of a genuinely freeform face) is refused: mapping a straight
    // UV polygon through a curved surface does not give straight (or
    // even necessarily planar) 3D edges, so this flat-facet construction
    // does not apply there - a real, disclosed, narrower gap than the
    // untrimmed path's own "any degree, any shape" scope.
    if (!body.face_hole_loops_[static_cast<size_t>(face_index)].empty()) {
      Fail(caller, "a trimmed face with holes is not attempted here yet - only a single outer trim loop");
    }
    const std::vector<Point2d>& loop_uv = body.face_trim_loops_[static_cast<size_t>(face_index)];
    if (loop_uv.size() < 3) Internal(caller, "a stored trim loop has fewer than 3 points");
    ON_Plane plane;
    if (!near_surf.IsPlanar(&plane, 1e-8 * std::max(near_surf.BoundingBox().Diagonal().Length(), 1.0))) {
      Fail(caller,
           "a trimmed face's surface must be planar - mapping a straight UV trim polygon through a curved "
           "surface does not give straight (or even necessarily planar) 3D edges, so this flat-facet "
           "construction does not apply; the untrimmed path already handles any curved face exactly");
    }
    std::vector<Point3d> loop, far_loop;
    loop.reserve(loop_uv.size());
    far_loop.reserve(loop_uv.size());
    for (const Point2d& uv : loop_uv) {
      const Point3d p = near_surf.PointAt(uv.x, uv.y);
      loop.push_back(p);
      far_loop.push_back(p + direction);
    }
    auto newell_normal = [](const std::vector<Point3d>& poly) {
      Vector3d n(0, 0, 0);
      const int m = static_cast<int>(poly.size());
      for (int i = 0; i < m; ++i) {
        const Point3d& a = poly[static_cast<size_t>(i)];
        const Point3d& b = poly[static_cast<size_t>((i + 1) % m)];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
      }
      return n;
    };
    std::vector<Brep::PlanarFace> pfaces;
    auto add_face = [&](std::vector<Point3d> pts) {
      Vector3d n = newell_normal(pts);
      if (!n.Unitize()) Fail(caller, "a generated face is degenerate (the trim loop is self-intersecting or has a zero-length edge)");
      Brep::PlanarFace pf;
      pf.plane = ON_Plane(pts[0], n);
      pf.loop = std::move(pts);
      pfaces.push_back(std::move(pf));
    };
    const int n = static_cast<int>(loop.size());
    if (cap) {
      // Near faces -direction, far faces +direction; the actual sign is
      // verified (and corrected with a single whole-body flip) below, the
      // same "guess, then check the tessellated volume" convention every
      // sweep factory in this file already ends with.
      std::vector<Point3d> near_rev(loop.rbegin(), loop.rend());
      add_face(std::move(near_rev));
      add_face(far_loop);
    }
    for (int i = 0; i < n; ++i) {
      const int j = (i + 1) % n;
      add_face({loop[static_cast<size_t>(i)], loop[static_cast<size_t>(j)], far_loop[static_cast<size_t>(j)],
                far_loop[static_cast<size_t>(i)]});
    }
    Brep result = Brep::FromPlanarFaces(pfaces);
    if (result.raw().IsSolid()) {
      const Mesh check = result.TessellateToClosedMesh(8, 8);
      if (check.Volume() < 0.0) result.raw().Flip();
    }
    return result;
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

// Walks `wire_body`'s own edge/vertex graph into one ordered edge-index
// chain PER CONNECTED COMPONENT - shared machinery for
// Brep::ExtrudeWireBody()/Brep::OffsetWireBody() below, the multi-
// component generalization of this file's own prior single-chain-only
// walk (every caller now wants every component, not just a refusal the
// moment a second one is found). A branch point (any vertex touching 3
// or more live edges, anywhere in the
// body, in any component) still refuses the WHOLE call by returning an
// empty outer vector - only "more than one disjoint wire component" is
// no longer, by itself, a refusal reason: each component independently
// walks as either a single open path (two degree-1 endpoint vertices)
// or a single closed loop (every vertex degree exactly 2, including the
// one-edge case of a curve closed on itself through a single self-
// referencing vertex - a self-closed edge's own single vertex lists
// that edge twice, so it reports degree 2, correctly routing it into
// the closed-loop case rather than being mistaken for a chain end). A
// genuine IsWireBody() Brep always has at least one live edge, so
// success is never itself an empty outer vector, and no chain inside it
// is ever itself empty.
std::vector<std::vector<int>> WalkWireChains(const Brep& wire_body) {
  const ON_Brep& b = wire_body.raw();

  std::vector<int> live_edges;
  for (int ei = 0; ei < b.m_E.Count(); ++ei) {
    if (b.m_E[ei].m_edge_index >= 0) live_edges.push_back(ei);
  }
  if (live_edges.empty()) return {};

  // A vertex touching 3+ live edges is a branch point - refused up
  // front, before any walk is attempted, exactly as the single-chain
  // version of this check always has. Every other live vertex is degree
  // 1 (a leaf - one of some component's own two chain ends) or degree 2
  // (an interior chain vertex, or every vertex of a closed loop).
  std::vector<int> leaves;
  for (int vi = 0; vi < b.m_V.Count(); ++vi) {
    if (b.m_V[vi].m_vertex_index < 0) continue;
    const int degree = static_cast<int>(wire_body.EdgesOfVertex(vi).size());
    if (degree == 0) continue;  // an unrelated, untouched vertex slot
    if (degree > 2) return {};
    if (degree == 1) leaves.push_back(vi);
  }

  std::vector<bool> visited(static_cast<size_t>(b.m_E.Count()), false);
  auto walk_from = [&](int start_vertex) {
    std::vector<int> order;
    int current = start_vertex;
    while (true) {
      int next_edge = -1;
      for (const int ei : wire_body.EdgesOfVertex(current)) {
        if (!visited[static_cast<size_t>(ei)]) {
          next_edge = ei;
          break;
        }
      }
      if (next_edge < 0) break;  // this component's own chain end (or back to a closed loop's start) reached
      visited[static_cast<size_t>(next_edge)] = true;
      order.push_back(next_edge);
      const ON_BrepEdge& e = b.m_E[next_edge];
      current = (e.m_vi[0] == current) ? e.m_vi[1] : e.m_vi[0];
    }
    return order;
  };

  std::vector<std::vector<int>> chains;
  // Open-chain components: start from each leaf, skipping the second
  // leaf of a chain this loop already walked from its other end (its
  // one touching edge is already visited by then).
  for (const int leaf : leaves) {
    bool already_visited = false;
    for (const int ei : wire_body.EdgesOfVertex(leaf)) {
      already_visited = already_visited || visited[static_cast<size_t>(ei)];
    }
    if (already_visited) continue;
    chains.push_back(walk_from(leaf));
  }
  // Closed-loop components: no leaf touches one at all (every one of
  // its vertices is degree 2), so any live edge still unvisited once
  // every open chain above is walked belongs to one.
  for (const int ei : live_edges) {
    if (visited[static_cast<size_t>(ei)]) continue;
    chains.push_back(walk_from(b.m_E[ei].m_vi[0]));
  }

  // Defensive self-check only: with no branch point anywhere (already
  // refused above), every connected component IS a single open chain or
  // a single closed loop, so every live edge is necessarily walked into
  // exactly one chain above - this can't actually trip.
  size_t total = 0;
  for (const std::vector<int>& chain : chains) total += chain.size();
  if (total != live_edges.size()) return {};
  return chains;
}

}  // namespace

namespace {

// Joins one WalkWireChains() chain's own edge curves, in walk order,
// into one continuous profile - the shared per-chain step
// ExtrudeWireBody()/OffsetWireBody() each now apply once per component
// rather than once per whole call.
NurbsCurve JoinWireChainProfile(const Brep& wire_body, const std::vector<int>& chain, const char* caller) {
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
    // The walk only ever follows edges sharing a real ON_BrepVertex, so
    // `next` is always genuinely meant to continue `profile` - measure
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
  return profile;
}

}  // namespace

Brep Brep::ExtrudeWireBody(const Brep& wire_body, Vector3d direction, bool cap) {
  const char* caller = "ExtrudeWireBody";
  if (!(direction.Length() > 0.0)) Fail(caller, "direction must be non-zero (its length is the extrusion distance)");
  if (!wire_body.IsWireBody()) {
    Fail(caller, "wire_body must satisfy IsWireBody() (at least one live edge, zero live faces)");
  }
  const std::vector<std::vector<int>> chains = WalkWireChains(wire_body);
  if (chains.empty()) {
    Fail(caller,
         "wire_body's own edge graph must be made up of simple open chains and/or closed loops only - a branch "
         "point (a vertex touching 3 or more edges) anywhere is out of scope");
  }

  if (chains.size() == 1) {
    return Extrude(JoinWireChainProfile(wire_body, chains.front(), caller), direction, cap);
  }

  // Several disjoint wire components (e.g. two separate closed loops,
  // or an open chain plus a separate closed loop, in the same wire
  // body): extrude each one independently, exactly as the single-
  // component case above does, then concatenate the results via the
  // existing multi-lump Compound() - deliberately unwelded, the same
  // "Lumps are deliberately NOT welded to each other" contract
  // Compound() already documents, since these components never shared
  // any geometry with each other to begin with.
  std::vector<Brep> pieces;
  pieces.reserve(chains.size());
  for (const std::vector<int>& chain : chains) {
    pieces.push_back(Extrude(JoinWireChainProfile(wire_body, chain, caller), direction, cap));
  }
  return Compound(pieces);
}

Brep Brep::OffsetWireBody(const Brep& wire_body, double distance, double tolerance) {
  const char* caller = "OffsetWireBody";
  if (!wire_body.IsWireBody()) {
    Fail(caller, "wire_body must satisfy IsWireBody() (at least one live edge, zero live faces)");
  }
  const std::vector<std::vector<int>> chains = WalkWireChains(wire_body);
  if (chains.empty()) {
    Fail(caller,
         "wire_body's own edge graph must be made up of simple open chains and/or closed loops only - a branch "
         "point (a vertex touching 3 or more edges) anywhere is out of scope");
  }

  // Each component's own joined profile is offset independently (same
  // single-component construction OffsetWireBody() always used), then
  // every resulting offset curve is rebuilt into ONE fresh, independent
  // wire body via a single WireBody() call - which already welds
  // curves that happen to share an endpoint and leaves genuinely
  // disjoint ones apart (see WireBody()'s/AddWireCurves()'s own tests),
  // so a multi-component input's own disjointness is preserved for free
  // rather than needing a separate compounding step the way
  // ExtrudeWireBody() needs Compound() (a pure wire body has zero
  // faces, so Compound() itself would just discard it - see Compound()'s
  // own "the empty set: contributes nothing" rule).
  std::vector<NurbsCurve> offsets;
  offsets.reserve(chains.size());
  for (const std::vector<int>& chain : chains) {
    NurbsCurve profile = JoinWireChainProfile(wire_body, chain, caller);
    NurbsCurve offset;
    if (profile.OffsetInPlane(distance, offset, tolerance) != Result::Ok) {
      Fail(caller, "the wire body's own joined profile could not be offset - it is not planar within tolerance, "
                   "or the requested distance folds it through itself or through its own center of curvature");
    }
    offsets.push_back(offset);
  }

  return WireBody(offsets, tolerance::kDistance);
}

namespace {
// Maps a TrimmedPlanarFace()-style UV polygon through `raw_surface`'s own
// PointAt(u, v) into a closed, degree-1 3D NurbsCurve (the same "map UV
// through PointAt, close by repeating the first point" convention
// TrimmedPlanarFaceWelded()'s own doc comment already names), then shifts
// every point by `shift` - used by ThickenTrimmedPlanarSheet() below to
// center the profile on the solid's own midplane for a `symmetric` call,
// exactly as Brep::Thicken()'s own untrimmed path already does via its
// `lo`/`hi` offsets.
NurbsCurve TrimLoopToClosedCurve(const ON_Surface* raw_surface, const std::vector<Point2d>& uv_loop,
                                  const Vector3d& shift) {
  std::vector<Point3d> pts;
  pts.reserve(uv_loop.size() + 1);
  for (const Point2d& uv : uv_loop) {
    pts.push_back(raw_surface->PointAt(uv.x, uv.y) + shift);
  }
  pts.push_back(pts.front());
  return NurbsCurve::FromControlPoints(pts, 1);
}

// Brep::Thicken()'s own disclosed "a trimmed sheet's real boundary is not
// its surface's 4 domain isocurves" gap, closed for the PLANAR case: since
// a planar face's own TrimmedPlanarFace()-style UV trim loop (outer plus
// any holes, `sheet`'s own face_trim_loops_/face_hole_loops_ side tables)
// maps through the SAME plane for every point, thickening it is exactly
// Brep::Extrude()/ExtrudeProfileWithHoles() of that loop by
// `thickness * plane.zaxis` - reusing those two already-tested solid-
// building primitives rather than inventing a new ruled-wall-plus-two-caps
// topology assembly for this case (the untrimmed path's own `lo`/`hi` +
// 4-side-wall construction below still owns the genuinely curved case,
// where no such single flat loop exists). `symmetric` shifts both the
// outer loop and every hole loop by `-thickness/2 * plane.zaxis` before
// extruding the full signed `thickness` - the solid then spans from
// `-thickness/2` to `+thickness/2` along the normal regardless of
// `thickness`'s own sign, landing the original surface on the solid's own
// midplane exactly like the untrimmed path's own `symmetric` option.
//
// Still a disclosed, narrower case than the untrimmed path's own: a
// genuinely curved trimmed sheet (the surface itself non-planar) is not
// covered here - inherits whatever `Brep::Extrude()`/
// `ExtrudeProfileWithHoles()` themselves require of a closed, planar,
// star-shaped profile curve (a trim loop that isn't star-shaped from its
// own centroid is a disclosed, separate gap those functions already
// carry, not a new one introduced here). A HOLED trim loop is routed
// through `ExtrudeProfileWithHoles()` rather than plain `Extrude()` below,
// but is not independently verified by a dedicated test in this pass:
// confirmed directly (`dino8_scratch_test`) that this exact composition
// inherits `BooleanCombineGeneral()`'s own pre-existing tolerance
// sensitivity badly enough that even an outer/hole pair that succeeds with
// hand-typed exact coordinates can still throw once the outer loop's own
// points instead come from a real `PointAt()` evaluation (the realistic
// case here) - the same class of fragility this file's own
// "Emboss/deboss" category bullet already discloses for a square hole in
// a square outer profile, just not confined to that one specific shape
// combination. The no-hole path above is unaffected (confirmed by this
// function's own passing tests) since it never calls
// `BooleanCombineGeneral()` at all.
Brep ThickenTrimmedPlanarSheet(const ON_Surface* raw_surface, const std::vector<Point2d>& outer_uv,
                                const std::vector<std::vector<Point2d>>& hole_uvs, double thickness, bool symmetric) {
  const char* caller = "Thicken";
  ON_Plane plane;
  if (!raw_surface->IsPlanar(&plane, tolerance::kDistance)) {
    Fail(caller,
         "a trimmed sheet's face must be planar - a trimmed, genuinely curved sheet (the surface's real "
         "boundary is not its 4 domain isocurves) is a separate, disclosed gap");
  }
  const Vector3d shift = symmetric ? plane.zaxis * (-thickness / 2.0) : Vector3d(0.0, 0.0, 0.0);
  const Vector3d direction = plane.zaxis * thickness;

  const NurbsCurve outer = TrimLoopToClosedCurve(raw_surface, outer_uv, shift);
  std::vector<NurbsCurve> holes;
  for (const std::vector<Point2d>& hole_uv : hole_uvs) {
    holes.push_back(TrimLoopToClosedCurve(raw_surface, hole_uv, shift));
  }
  if (holes.empty()) return Brep::Extrude(outer, direction, /*cap=*/true);
  return ExtrudeProfileWithHoles(outer, holes, direction);
}

// The offset step Brep::Thicken()'s untrimmed path below uses for its
// caps: NurbsSurface::OffsetApproximate() (surface.h) in the general case,
// exactly as before, but EXACT when `src` is a right circular cylinder -
// a radial homothety of the control net about the cylinder's own axis.
// Why that is exact rather than one more approximation: scaling every
// point's distance from a fixed axis by one constant factor `s` is an
// affine map of 3-space ((x, y, z) -> (s*x, s*y, z) in axis-aligned
// coordinates), a NURBS surface is affine-invariant (applying the map to
// its control points applies it to every point of the surface), and the
// image of the cylinder of radius r under that map is precisely the
// cylinder of radius s*r about the same axis - for ANY NURBS form of the
// cylinder (ON_Cylinder::GetNurbForm()'s own, a transposed one, or a
// re-parameterized one), not one particular control-net layout. Knot
// vectors and weights are untouched, so the result's boundary isocurves
// stay compatible with `src`'s own at the same parameter values - the one
// property Thicken()'s RuledBetween() walls rely on, and the reason
// OffsetApproximate() was chosen over OffsetAnalytic() there in the first
// place (Thicken()'s own brep.h doc comment). OffsetApproximate()'s own
// per-control-point Greville-normal translation is NOT exact for a
// cylinder: a rational quadratic circle's off-curve control points sit
// at r/cos(pi/4) from the center, so moving them by `distance` rather
// than by distance/cos(pi/4) leaves each "offset" arc bulging measurably
// inside the true offset circle between its knots (confirmed directly:
// OffsetApproximate(0.25) on a unit cylinder wall comes back IsCylinder()
// false, its mid-arc radius 0.043 short of the true 1.25 - 17% of the
// offset distance, exactly the (1 - (2cos45 + 2w)/(2 + 2w)) a rational
// quadratic arc's own mid-point blend of its three translated control
// points predicts for w = cos45).
//
// Sign: the SAME convention OffsetApproximate() uses - positive
// `distance` moves along `src`'s own NormalAt() direction - resolved by
// comparing NormalAt() at the domain midpoint against the outward radial
// direction there (a cylinder's parametrization may put Su x Sv either
// radially outward or inward; neither can be assumed). Guard: the new
// radius must stay positive (the same `new_radius <= 0` test
// OffsetAnalytic()'s own cylinder branch applies - an inward offset of
// the full radius or more collapses the wall through its own axis),
// Result::Failed otherwise, mirroring OffsetApproximate()'s own fold
// guard so Thicken() reports both the same way.
Result OffsetKeepingNet(const NurbsSurface& src, double distance, NurbsSurface& out) {
  ON_Cylinder cyl;
  if (!src.raw().IsCylinder(&cyl) || !(cyl.circle.radius > 0.0)) {
    return src.OffsetApproximate(distance, out);
  }
  const ON_3dPoint axis_origin = cyl.circle.plane.origin;
  ON_3dVector axis = cyl.circle.plane.zaxis;
  if (!axis.Unitize()) return src.OffsetApproximate(distance, out);
  const double r = cyl.circle.radius;

  const Interval du = src.Domain(0), dv = src.Domain(1);
  const double um = 0.5 * (du.min + du.max), vm = 0.5 * (dv.min + dv.max);
  const ON_3dVector n = src.NormalAt(um, vm);
  ON_3dVector radial = src.PointAt(um, vm) - axis_origin;
  radial -= (radial * axis) * axis;
  const double new_r = r + ((radial * n) >= 0.0 ? distance : -distance);
  if (new_r <= 0.0) return Result::Failed;
  const double s = new_r / r;

  ON_NurbsSurface moved = src.raw();
  for (int i = 0; i < moved.CVCount(0); ++i) {
    for (int j = 0; j < moved.CVCount(1); ++j) {
      ON_4dPoint cv;
      moved.GetCV(i, j, cv);
      const double w = cv.w;
      const ON_3dPoint e(cv.x / w, cv.y / w, cv.z / w);
      const ON_3dVector d = e - axis_origin;
      const double along = d * axis;
      const ON_3dPoint scaled = axis_origin + along * axis + s * (d - along * axis);
      moved.SetCV(i, j, ON_4dPoint(scaled.x * w, scaled.y * w, scaled.z * w, w));
    }
  }
  out.raw() = moved;
  return Result::Ok;
}
}  // namespace

Brep Brep::Thicken(const Brep& sheet, double thickness, bool symmetric) {
  const char* caller = "Thicken";
  if (!std::isfinite(thickness) || thickness == 0.0) {
    Fail(caller, "thickness must be finite and non-zero");
  }
  const ON_Brep& src = sheet.raw();
  if (src.m_F.Count() != 1) {
    Fail(caller, "sheet must be a single-face body - a multi-face shell thicken is a separate, disclosed gap");
  }
  const ON_BrepFace& face = src.m_F[0];
  const ON_Surface* raw_surface = face.SurfaceOf();
  if (raw_surface == nullptr) Internal(caller, "sheet's face has no surface");
  const bool trimmed = !sheet.face_trim_loops_.empty() &&
                        (!sheet.face_trim_loops_[0].empty() || !sheet.face_hole_loops_[0].empty());
  if (trimmed) {
    return ThickenTrimmedPlanarSheet(raw_surface, sheet.face_trim_loops_[0], sheet.face_hole_loops_[0], thickness,
                                      symmetric);
  }
  ON_NurbsSurface base;
  if (!raw_surface->GetNurbForm(base)) {
    Fail(caller, "sheet's surface could not be converted to an exact NURBS form");
  }
  // A direction the surface wraps back on itself in (IsClosed()) has no
  // free boundary to wall at all: a sheet closed in exactly ONE direction
  // (a full cylinder/cone wall, Pipe()'s or an uncapped Extrude()'s own
  // single wall face) gets 2 side walls below instead of the open case's
  // 4, each a ruled surface between two CLOSED isocurves; a sheet closed
  // in BOTH directions (a torus) already bounds a solid by itself, so the
  // "thickened" result would be a hollow two-shell body - a separate,
  // disclosed gap, refused rather than mis-built as one shell.
  const bool closed_u = base.IsClosed(0);
  const bool closed_v = base.IsClosed(1);
  if (closed_u && closed_v) {
    Fail(caller,
         "the surface is closed in BOTH parametric directions (a full torus) - it already bounds a solid with "
         "no free boundary to wall at all, and a hollow two-shell thicken of a closed sheet is a separate, "
         "disclosed gap");
  }
  if (closed_u || closed_v) {
    // The open direction's two ends must be genuine free boundaries, not
    // poles: a full sphere (closed in u, each v end collapsed to a pole)
    // or a cone wall carried all the way to its apex would get a wall
    // ruled between two POINTS there - a degenerate zero-area face, not
    // a solid's boundary. ON_Surface::IsSingular()'s side numbering: 0 =
    // south (v = v_min), 1 = east (u = u_max), 2 = north (v = v_max), 3
    // = west (u = u_min).
    const int side_a = closed_u ? 0 : 3;
    const int side_b = closed_u ? 2 : 1;
    if (base.IsSingular(side_a) || base.IsSingular(side_b)) {
      Fail(caller,
           "the sheet's open parametric direction ends in a side collapsed to a single point (a pole or apex - "
           "a full sphere, or a cone wall carried all the way to its apex) - there is no boundary curve there "
           "to wall between; a sheet with a singular side is a separate, disclosed gap");
    }
  }

  NurbsSurface original;
  original.raw() = base;

  // `lo`/`hi`: the two caps, always assigned so the solid lies on the +N
  // side of `lo` and the -N side of `hi`, N = `original`'s own NormalAt()
  // direction (OffsetApproximate()'s own translation convention, which
  // OffsetKeepingNet() above keeps for its exact cylinder branch too) -
  // see this function's own brep.h doc comment for why OffsetApproximate(),
  // not OffsetAnalytic(), is what makes `lo`/`hi` boundary-compatible.
  NurbsSurface lo, hi;
  if (symmetric) {
    const double half = std::fabs(thickness) * 0.5;
    if (OffsetKeepingNet(original, -half, lo) != Result::Ok || OffsetKeepingNet(original, half, hi) != Result::Ok) {
      Fail(caller,
           "the requested thickness is too large for this surface's own curvature - the offset would fold "
           "through its own center of curvature (see NurbsSurface::OffsetApproximate's own guard; for a "
           "cylinder wall, an inward offset of its full radius or more)");
    }
  } else {
    NurbsSurface offset;
    if (OffsetKeepingNet(original, thickness, offset) != Result::Ok) {
      Fail(caller,
           "the requested thickness is too large for this surface's own curvature - the offset would fold "
           "through its own center of curvature (see NurbsSurface::OffsetApproximate's own guard; for a "
           "cylinder wall, an inward offset of its full radius or more)");
    }
    if (thickness > 0.0) {
      lo = original;
      hi = offset;
    } else {
      lo = offset;
      hi = original;
    }
  }

  // A sheet closed in one direction must STAY closed in it after the
  // offset, or the two caps are no longer closed rings and the result
  // cannot be watertight. Both offset routes guarantee this by
  // construction - OffsetKeepingNet()'s cylinder branch applies one
  // affine map to every control point, and OffsetApproximate() hands the
  // seam's coincident control points the SAME Greville parameter in a
  // closed direction (surface.cpp's OffsetGrevilleParameter(); before
  // that wrap existed, nudging them inward from opposite ends of the
  // domain opened the seam by O(nudge * curvature), 1.9e-6 on an
  // elliptical Extrude() tube, found while building this path) - so a
  // failure here is a broken invariant, not an input the caller could
  // have avoided.
  const int closed_dir = closed_u ? 0 : 1;
  if ((closed_u || closed_v) && (!lo.IsClosed(closed_dir) || !hi.IsClosed(closed_dir))) {
    Internal(caller, "the offset of a sheet closed in one direction came back open across its seam");
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
    // `NewFace(int)` alone leaves the face with ZERO loops (the bare
    // AddSurface+NewFace(int) idiom this function uses throughout builds
    // no ON_BrepLoop/ON_BrepTrim at all - confirmed directly, a real,
    // previously-undisclosed gap: this kernel's own TessellateToClosedMesh()/
    // Volume() never need real loops, reading face_trim_loops_/
    // AppendUntrimmedFaceSideTables() below instead, but any OTHER
    // standard-OpenNURBS-topology consumer (dino8-app's own BrepMesher.cpp,
    // confirmed via dino8_scratch_test/a direct app smoke test: a
    // genuinely closed, positive-volume Thicken() result rendered as an
    // empty 0-triangle mesh, bounding box collapsed to a point) sees a
    // face with no boundary at all and simply skips it) - NewOuterLoop()
    // builds the face's own natural full-domain rectangular loop, the
    // same real OpenNURBS primitive Brep::RemoveOuterTrim() already uses
    // (brep.cpp) for an open, non-singular, non-periodic rectangular
    // surface, and - for a cap closed in one direction - the seam-sharing
    // loop AddFanCap()'s own doc comment above confirms it builds for a
    // periodic-in-u untrimmed face.
    if (brep.NewOuterLoop(f.m_face_index) == nullptr) {
      Internal(caller, "ON_Brep::NewOuterLoop failed unexpectedly for a cap face");
    }
  };
  add_cap(lo_raw, /*brev=*/true);
  add_cap(hi_raw, /*brev=*/false);

  // Side walls, one per OPEN edge of the domain rectangle (all four for
  // an open sheet; only the two across the open direction for a sheet
  // closed in the other, whose closed edges are seams, not boundary): the
  // exact degree-1-in-v ruled surface (RuledBetween()) between that
  // edge's isocurve on `lo` and the SAME edge's isocurve on `hi` -
  // compatible by construction (neither OffsetApproximate() nor
  // OffsetKeepingNet()'s cylinder branch changes the control-point grid or
  // knot vectors, only moves the control points, so `lo`/`hi` share
  // identical knot vectors/CV counts and so do their corresponding
  // isocurves). Walked CCW as seen from the `hi` (outward, N) side, the
  // same convention Extrude() itself relies on for why a CCW boundary here
  // makes Sw_u x Sw_v point outward: v=v_min forward in u, u=u_max forward
  // in v, v=v_max backward in u, u=u_min backward in v - the standard
  // positively-oriented parameter-rectangle loop, already used the same
  // way for a curved-face cap's own rectangle loop elsewhere in this
  // codebase (see FromMixedFaces' own doc comment in brep.cpp). Skipping
  // a closed direction's two edges leaves that orientation argument
  // intact for the remaining walls: each is still the same ruling
  // between the same two isocurves, only now closed curves.
  int wall_count = 0;
  auto add_wall = [&](int dir, double param, bool reverse_iso) {
    ++wall_count;
    std::unique_ptr<ON_NurbsCurve> c_lo(IsoCurveOf(lo_raw, dir, param, caller));
    std::unique_ptr<ON_NurbsCurve> c_hi(IsoCurveOf(hi_raw, dir, param, caller));
    if (reverse_iso) {
      ReverseKeepDomain(*c_lo);
      ReverseKeepDomain(*c_hi);
    }
    std::unique_ptr<ON_NurbsSurface> wall = RuledBetween(*c_lo, *c_hi, 0.0, 1.0, caller);
    const int si = brep.AddSurface(wall.release());
    ON_BrepFace& f = brep.NewFace(si);
    // Same missing-loop gap add_cap() above discloses and fixes, for the
    // identical reason - RuledBetween()'s own wall is just as open,
    // non-singular and non-periodic as either cap.
    if (brep.NewOuterLoop(f.m_face_index) == nullptr) {
      Internal(caller, "ON_Brep::NewOuterLoop failed unexpectedly for a side wall face");
    }
  };
  if (!closed_v) add_wall(0, dv.Min(), /*reverse_iso=*/false);  // v = v_min, walked +u
  if (!closed_u) add_wall(1, du.Max(), /*reverse_iso=*/false);  // u = u_max, walked +v
  if (!closed_v) add_wall(0, dv.Max(), /*reverse_iso=*/true);   // v = v_max, walked -u
  if (!closed_u) add_wall(1, du.Min(), /*reverse_iso=*/true);   // u = u_min, walked -v

  brep.SetTrimIsoFlags();
  result.AppendUntrimmedFaceSideTables(2 + wall_count);

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

// ---------------------------------------------------------------------------
// Partial-angle revolve of an open profile with an off-axis endpoint
// (Brep::AssembleRevolvedWedge below).
// ---------------------------------------------------------------------------

// The two 3D corners a side's trim runs between, in ON_Brep::NewFace()'s
// own side order and direction: 0 south (v = v0, +u), 1 east (u = u1,
// +v), 2 north (v = v1, -u), 3 west (u = u0, -v).
void SideCorners(const ON_NurbsSurface& s, int side, ON_3dPoint& from, ON_3dPoint& to) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  const ON_3dPoint sw = s.PointAt(du.Min(), dv.Min()), se = s.PointAt(du.Max(), dv.Min());
  const ON_3dPoint ne = s.PointAt(du.Max(), dv.Max()), nw = s.PointAt(du.Min(), dv.Max());
  switch (side) {
    case 0:
      from = sw;
      to = se;
      break;
    case 1:
      from = se;
      to = ne;
      break;
    case 2:
      from = ne;
      to = nw;
      break;
    default:
      from = nw;
      to = sw;
      break;
  }
}

// Adds `surface` to `brep` as a new untrimmed face whose side i is the
// existing edge `shared[i]` (or a fresh edge for -1; a singular side must
// be -1, NewFace() builds its singular trim itself). Whether each shared
// edge runs with or against that side's trim is decided by matching the
// edge's own two vertices against the side's corners (within `tol`), so
// callers hand over edge indices alone and never restate ON_Brep's
// side/reversal convention. On return `shared` holds every side's edge
// index, fresh ones included.
void AddFaceSharingEdges(ON_Brep& brep, std::unique_ptr<ON_NurbsSurface> surface, int shared[4], double tol,
                         const char* caller) {
  int vid[4] = {-1, -1, -1, -1};
  int eid[4] = {-1, -1, -1, -1};
  bool rev[4] = {false, false, false, false};
  for (int i = 0; i < 4; ++i) {
    if (shared[i] < 0) continue;
    ON_3dPoint from, to;
    SideCorners(*surface, i, from, to);
    const ON_BrepEdge& e = brep.m_E[shared[i]];
    const ON_3dPoint e0 = brep.m_V[e.m_vi[0]].point, e1 = brep.m_V[e.m_vi[1]].point;
    const bool forward = e0.DistanceTo(from) <= tol && e1.DistanceTo(to) <= tol;
    const bool backward = e1.DistanceTo(from) <= tol && e0.DistanceTo(to) <= tol;
    if (forward && backward) {
      // Both of the edge's ends sit at both corners: the edge is shorter
      // than `tol` (a swept arc at an angle within tol of a full turn
      // but outside Revolve()'s own `full` epsilon, say), so its
      // direction along this side cannot be read off its vertices and
      // guessing would bake a wrong trim direction into the shell.
      Fail(caller, "a wedge edge's two ends coincide within tolerance (a swept arc or radial segment shorter than the "
                   "on-axis tolerance) - the caps would be degenerate");
    }
    if (forward) {
      rev[i] = false;
    } else if (backward) {
      rev[i] = true;
    } else {
      Internal(caller, "a shared edge's vertices do not sit at the face side it was assigned to");
    }
    eid[i] = shared[i];
  }
  if (!brep.NewFace(surface.release(), vid, eid, rev)) Internal(caller, "ON_Brep::NewFace refused a wedge face");
  for (int i = 0; i < 4; ++i) shared[i] = eid[i];
}

// The planar side cap of a partial revolve: the region between `profile`
// (one of the wall's own two v-boundary iso-curves, so the cap's row and
// the wall's edge share CVs bit-for-bit) and the axis, as the ruled
// surface S(u, v) = (1 - v) A(u) + v profile(u) between the axis segment
// A - from the profile's start foot `f0` to its end foot `f1`, carrying
// the profile's own knots and weights with its CVs spread along the
// segment at the profile's own (normalized) Greville abscissae - and the
// profile itself. Equal weights per column make every ruling a straight
// line, so the u = u0 / u = u1 sides are exactly the two straight radial
// segments - or collapse to a point, a singular side, at an endpoint
// already on the axis (`start_on_axis`/`end_on_axis`): that column of
// the axis row is then the profile's own homogeneous CV, bit-identical,
// which is what IsSingular() needs. `profile_south` swaps the two rows -
// the same planar region with the opposite normal, which is how the
// caller picks the outward one.
std::unique_ptr<ON_NurbsSurface> AxisRuledSurface(const ON_NurbsCurve& profile, ON_3dPoint f0, ON_3dPoint f1,
                                                  bool start_on_axis, bool end_on_axis, bool profile_south,
                                                  const char* caller) {
  const int n = profile.CVCount();
  const ON_Interval dom = profile.Domain();
  ON_NurbsCurve axis = profile;  // same order, knots, weights and CV count
  for (int i = 0; i < n; ++i) {
    if ((i == 0 && start_on_axis) || (i == n - 1 && end_on_axis)) continue;  // already the profile's own CV
    const double g = i == 0 ? 0.0 : i == n - 1 ? 1.0 : dom.NormalizedParameterAt(profile.GrevilleAbcissa(i));
    const ON_3dPoint a = f0 + (f1 - f0) * g;
    const double w = HomogeneousCV(profile, i).w;
    if (axis.IsRational()) {
      axis.SetCV(i, ON_4dPoint(a.x * w, a.y * w, a.z * w, w));
    } else {
      axis.SetCV(i, a);
    }
  }
  return profile_south ? RuledBetween(profile, axis, 0.0, 1.0, caller) : RuledBetween(axis, profile, 0.0, 1.0, caller);
}

// The sign of (S_u x S_v) . `normal` over the interior of the planar
// surface `s`, or 0 when it vanishes or changes sign anywhere - for a
// ruled planar face that is exactly the test that every ruling sweeps the
// region the same way, i.e. that the map (u, v) -> S(u, v) is an
// embedding of the rectangle (nowhere-folding, plus the boundary being
// simple, which a fold-free ruling of a straight segment onto a one-sided
// curve guarantees) rather than a sheet folded over itself. Samples
// strictly inside the domain, away from any singular side.
int RulingSign(const ON_NurbsSurface& s, ON_3dVector normal, double scale, const char* caller) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  const int nu = std::max(64, 16 * s.SpanCount(0));
  const int nv = 8;
  const double eps = 1e-12 * scale * scale;
  int sign = 0;
  for (int i = 0; i < nu; ++i) {
    for (int j = 0; j < nv; ++j) {
      ON_3dPoint p;
      ON_3dVector su, sv;
      if (!s.Ev1Der(du.ParameterAt((i + 0.5) / nu), dv.ParameterAt((j + 0.5) / nv), p, su, sv)) {
        Internal(caller, "Ev1Der failed on a wedge side cap");
      }
      const double jac = ON_DotProduct(ON_CrossProduct(su, sv), normal);
      if (std::fabs(jac) <= eps) return 0;
      const int here = jac > 0.0 ? 1 : -1;
      if (sign == 0) sign = here;
      if (here != sign) return 0;
    }
  }
  return sign;
}

}  // namespace

// Closes a partial-angle revolve of an open profile with an off-axis
// endpoint (see brep.h): the wall first, then the two planar side caps,
// then the pie sector over each off-axis endpoint's arc, each later face
// wired onto the edges the earlier ones already own. Orientation is fixed
// per face from the wall's own geometry rather than guessed: a side cap's
// outward normal is -S_v at the sweep start and +S_v at its end (the
// sweep leaves the start half-plane along +S_v and arrives at the end
// one along it), and a sector's is +axis at the profile's START and -axis
// at its END - Revolve() has already reversed the profile so that the
// region (profile, closed by its radial end segments and the axis
// segment) is CLOCKWISE in the (rho, z) half-plane, which puts the
// interior on the right of the outward radial segment at the start
// (below it, outward = +axis) and on the right of the inward one at the
// end (above it, outward = -axis).
Brep Brep::AssembleRevolvedWedge(ON_NurbsSurface* wall_raw, Point3d axis_point, Vector3d axis_direction,
                                 bool start_on_axis, bool end_on_axis, double tol, const char* caller) {
  std::unique_ptr<ON_NurbsSurface> wall_in(wall_raw);
  Brep result;
  ON_Brep& brep = result.raw();
  const ON_NurbsSurface wall = *wall_in;  // keep an evaluable copy; the brep owns the original
  const ON_Interval du = wall.Domain(0), dv = wall.Domain(1);
  const ON_3dVector T = axis_direction;
  double scale = 1.0;
  {
    ON_BoundingBox bb;
    wall.GetBoundingBox(bb);
    scale += bb.Diagonal().Length();
  }
  auto foot_of = [&](ON_3dPoint p) { return ON_3dPoint(axis_point + T * ON_DotProduct(p - axis_point, T)); };

  int wall_e[4] = {-1, -1, -1, -1};
  AddFaceSharingEdges(brep, std::move(wall_in), wall_e, tol, caller);
  int faces = 1;

  // The sweep direction at each end: the wall's own S_v, read where the
  // profile is farthest from the axis so it cannot be the zero vector of
  // an on-axis point.
  auto sweep_direction = [&](double v) {
    ON_3dVector best(0, 0, 0);
    for (int i = 0; i <= 8; ++i) {
      ON_3dPoint p;
      ON_3dVector su, sv;
      if (!wall.Ev1Der(du.ParameterAt(i / 8.0), v, p, su, sv)) Internal(caller, "Ev1Der failed on the wall");
      if (sv.Length() > best.Length()) best = sv;
    }
    if (!best.Unitize()) Internal(caller, "the revolved wall does not move at all along its angle direction");
    return best;
  };

  // One side cap: returns its axis-segment edge; `radial_start`/`radial_end`
  // receive its two radial edges (-1 at an on-axis endpoint, where that
  // side is singular).
  auto add_side_cap = [&](double v, ON_3dVector outward, int profile_edge, int axis_edge, int& radial_start,
                          int& radial_end) {
    std::unique_ptr<ON_NurbsCurve> profile(IsoCurveOf(wall, 0, v, caller));
    const int n = profile->CVCount();
    const ON_3dPoint p0 = EuclideanCV(*profile, 0), p1 = EuclideanCV(*profile, n - 1);
    const ON_3dPoint f0 = start_on_axis ? p0 : foot_of(p0), f1 = end_on_axis ? p1 : foot_of(p1);
    if (f0.DistanceTo(f1) <= tol) {
      Fail(caller, "the profile's two endpoints are at the same axial height - the axis segment between their feet "
                   "is a single point, so the side caps would be degenerate");
    }
    std::unique_ptr<ON_NurbsSurface> cap =
        AxisRuledSurface(*profile, f0, f1, start_on_axis, end_on_axis, /*profile_south=*/false, caller);
    const int sign = RulingSign(*cap, outward, scale, caller);
    if (sign == 0) {
      Fail(caller,
           "the region between the profile and the axis cannot be swept by straight rulings from the axis segment "
           "(the profile doubles back over its own radial end segments) - a partial revolve of this open profile "
           "cannot be capped");
    }
    const bool profile_south = sign < 0;
    if (profile_south) {
      cap = AxisRuledSurface(*profile, f0, f1, start_on_axis, end_on_axis, /*profile_south=*/true, caller);
    }
    int shared[4] = {-1, -1, -1, -1};
    shared[profile_south ? 0 : 2] = profile_edge;
    shared[profile_south ? 2 : 0] = axis_edge;
    AddFaceSharingEdges(brep, std::move(cap), shared, tol, caller);
    ++faces;
    radial_start = shared[3];
    radial_end = shared[1];
    return shared[profile_south ? 2 : 0];
  };
  int radial_start0 = -1, radial_end0 = -1, radial_start1 = -1, radial_end1 = -1;
  const int axis_edge = add_side_cap(dv.Min(), -sweep_direction(dv.Min()), wall_e[0], -1, radial_start0, radial_end0);
  add_side_cap(dv.Max(), sweep_direction(dv.Max()), wall_e[2], axis_edge, radial_start1, radial_end1);

  // One pie sector: the transposed fan AssembleSweptBody()'s own disc
  // caps already use (the arc along v, matching the wall's v there), so
  // its south/north sides are the two straight radial segments.
  auto add_sector = [&](double u, ON_3dVector outward, int arc_edge, int radial0, int radial1) {
    ON_NurbsCurve arc = *std::unique_ptr<ON_NurbsCurve>(IsoCurveOf(wall, 1, u, caller));
    const ON_3dPoint foot = foot_of(arc.PointAtStart());
    auto build = [&]() {
      std::unique_ptr<ON_NurbsSurface> fan = FanSurface(arc, foot, caller);
      if (!fan->Transpose()) Internal(caller, "Transpose failed on a sector cap");
      return fan;
    };
    std::unique_ptr<ON_NurbsSurface> fan = build();
    {
      ON_3dPoint p;
      ON_3dVector su, sv;
      if (!fan->Ev1Der(0.5, fan->Domain(1).Mid(), p, su, sv)) Internal(caller, "Ev1Der failed on a sector cap");
      if (ON_DotProduct(ON_CrossProduct(su, sv), outward) < 0.0) {
        ReverseKeepDomain(arc);
        fan = build();
      }
    }
    // Which radial is the south side depends on the arc's (possibly
    // reversed) direction: pick by its corner, AddFaceSharingEdges()
    // re-verifies both ends.
    ON_3dPoint from, to;
    SideCorners(*fan, 0, from, to);
    const ON_BrepEdge& r0 = brep.m_E[radial0];
    const bool south_is_radial0 =
        brep.m_V[r0.m_vi[0]].point.DistanceTo(to) <= tol || brep.m_V[r0.m_vi[1]].point.DistanceTo(to) <= tol;
    int shared[4] = {south_is_radial0 ? radial0 : radial1, arc_edge, south_is_radial0 ? radial1 : radial0, -1};
    AddFaceSharingEdges(brep, std::move(fan), shared, tol, caller);
    ++faces;
  };
  if (!start_on_axis) add_sector(du.Min(), T, wall_e[3], radial_start0, radial_start1);
  if (!end_on_axis) add_sector(du.Max(), -T, wall_e[1], radial_end0, radial_end1);

  brep.SetTolerancesBoxesAndFlags(/*bLazy=*/true);
  result.AppendUntrimmedFaceSideTables(faces);

  // The same outward cross-check AssembleSweptBody() applies, at one
  // square division count (the construction rules above orient every
  // face outward already; a negative volume would mean one was wrong for
  // this input, and flipping is the safe repair).
  if (brep.IsSolid()) {
    const int divisions = std::max(16, 2 * std::max(wall.SpanCount(0), wall.SpanCount(1)));
    const Mesh m = result.TessellateToClosedMesh(divisions, divisions);
    if (m.Volume() < 0.0) brep.Flip();
  }
  return result;
}

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
      if (!full) {
        // An off-axis endpoint at a partial angle: the side caps are the
        // region between the profile and the axis, plus a pie sector over
        // each off-axis end's arc - a different construction from the
        // single-fan caps below (see AssembleRevolvedWedge's own comment).
        return AssembleRevolvedWedge(wall.release(), axis_point, T, s_on, e_on, tol, caller);
      }
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
  // Closed planar sections are capped with ONE planar trimmed face per end
  // (see AddPlanarTrimmedCap()), not a fan.
  return AssembleSweptBody(wall.release(), want_caps, want_caps, false, false, caller, nullptr, nullptr, nullptr,
                           nullptr, /*planar_trimmed_caps=*/true);
}

Result Brep::RibbonFromCurve(const NurbsCurve& curve, const ON_Plane& plane, double width, Brep& out,
                              double tolerance) {
  if (!ON_IsValid(width) || width == 0.0) {
    throw std::invalid_argument("dino8::kernel::Brep::RibbonFromCurve: width must be finite and nonzero");
  }
  if (!plane.IsValid()) {
    throw std::invalid_argument("dino8::kernel::Brep::RibbonFromCurve: plane must be valid");
  }
  NurbsCurve offset;
  const Result offset_result = curve.OffsetInPlane(plane, width, offset, tolerance);
  if (offset_result != Result::Ok) {
    return offset_result;
  }
  // `cap=false`: `Loft()`'s own capping is meant for a tube swept between
  // profiles at the SAME station (so a flat disk bounded by one profile
  // closes the end) - for a ribbon between a closed curve and its own
  // offset, the two sections are DIFFERENT curves, and a disk capped to
  // just the first one would ignore the offset entirely, not close the
  // annular strip between them. A ribbon is never capped, open curve or
  // closed.
  out = Brep::Loft({curve, offset}, /*degree=*/1, /*closed=*/false, /*cap=*/false);
  return Result::Ok;
}

namespace {

// Position + pure cross-boundary derivatives (up to order `continuity`)
// of `s` along its own isoparametric edge where parameter `rail_dir`
// varies and the OTHER parameter is fixed at `fixed_t` - the surface
// analogue of curve.cpp's own (anonymous-namespace-private, so not
// reusable across this translation unit boundary) `EvaluateBlendEnd`.
// `der_array`'s layout for ON_Surface::Evaluate (opennurbs_evaluate_nurbs.h's
// own doc comment: "Ds^i Dt^j is returned at n = stride*((i+j)*(i+j+1)/2
// + j)") puts the PURE fixed-direction term of order k at block offset
// k*(k+1)/2, plus j = k within that block when the fixed parameter is v
// (rail_dir == 0, i.e. j indexes the Dt/v power), or j = 0 when the fixed
// parameter is u (rail_dir == 1) - confirmed directly against that header
// comment, not guessed. `flip` negates ODD orders only (BlendSurfaces()'s
// own doc comment derives why), mirroring EvaluateBlendEnd's `reverse`.
bool EvaluateSurfaceBlendEnd(const ON_Surface& s, int rail_dir, double rail_t, double fixed_t, bool flip,
                             int continuity, Point3d& out_point, std::array<Vector3d, 4>& out_derivs) {
  const double u = rail_dir == 0 ? rail_t : fixed_t;
  const double v = rail_dir == 0 ? fixed_t : rail_t;
  // Sized for continuity up to 4: ON_Surface::Evaluate's own triangular
  // derivative layout needs dim * (num_der+1)*(num_der+2)/2 entries, i.e.
  // 3 * 5*6/2 = 45 at num_der=4 (3*10=30 only covered continuity<=3).
  double der[3 * 15] = {0.0};
  if (!s.Evaluate(u, v, continuity, 3, der)) return false;
  out_point = Point3d(der[0], der[1], der[2]);
  for (int k = 1; k <= continuity; ++k) {
    const int j = rail_dir == 0 ? k : 0;
    const int off = (k * (k + 1) / 2 + j) * 3;
    Vector3d d(der[off + 0], der[off + 1], der[off + 2]);
    if (flip && (k % 2 == 1)) d = -d;
    out_derivs[static_cast<size_t>(k - 1)] = d;
  }
  return true;
}

// The exact same closed-form Hermite-Bezier control-point construction
// NurbsCurve::BlendCurves() builds (curve.cpp) from one (position,
// derivatives) pair at each end - duplicated here rather than shared
// across the translation unit boundary, since that function's own
// version is anonymous-namespace-private to curve.cpp (the same "each
// file keeps its own copy of these small, already-proven helpers"
// convention the app layer's cmd_fillet.cpp/cmd_srfedit.cpp already
// follow for their own small object/face helpers). See BlendCurves()'s
// own doc comment for the textbook forward/backward Bezier finite-
// difference derivation this is lifted from verbatim.
std::vector<Point3d> HermiteBlendRow(const Point3d& P0, const std::array<Vector3d, 4>& D0, const Point3d& P1,
                                      const std::array<Vector3d, 4>& D1, int continuity) {
  const int d = 2 * continuity + 1;
  const double inv_d1 = 1.0 / static_cast<double>(d);
  const double inv_d2 = 1.0 / static_cast<double>(d * (d - 1));
  const double inv_d3 = 1.0 / static_cast<double>(d * (d - 1) * (d - 2));
  const double inv_d4 = 1.0 / static_cast<double>(d * (d - 1) * (d - 2) * (d - 3));
  std::vector<Point3d> ctrl(static_cast<size_t>(d) + 1);
  ctrl[0] = P0;
  if (continuity >= 1) ctrl[1] = ctrl[0] + D0[0] * inv_d1;
  if (continuity >= 2) ctrl[2] = ctrl[1] + (ctrl[1] - ctrl[0]) + D0[1] * inv_d2;
  if (continuity >= 3) ctrl[3] = ctrl[0] + 3.0 * (ctrl[2] - ctrl[1]) + D0[2] * inv_d3;
  // G4: the same forward identity curve.cpp's BlendCurves() derives one
  // order further, duplicated here per this function's own "each file
  // keeps its own copy" convention (see this function's own doc comment).
  if (continuity >= 4) {
    ctrl[4] = ctrl[3] + 3.0 * (ctrl[3] - ctrl[2]) - 3.0 * (ctrl[2] - ctrl[1]) + (ctrl[1] - ctrl[0]) + D0[3] * inv_d4;
  }

  ctrl[static_cast<size_t>(d)] = P1;
  if (continuity >= 1) ctrl[static_cast<size_t>(d - 1)] = ctrl[static_cast<size_t>(d)] - D1[0] * inv_d1;
  if (continuity >= 2) {
    ctrl[static_cast<size_t>(d - 2)] = ctrl[static_cast<size_t>(d - 1)] -
                                        (ctrl[static_cast<size_t>(d)] - ctrl[static_cast<size_t>(d - 1)]) +
                                        D1[1] * inv_d2;
  }
  if (continuity >= 3) {
    ctrl[static_cast<size_t>(d - 3)] =
        ctrl[static_cast<size_t>(d)] - 3.0 * (ctrl[static_cast<size_t>(d - 1)] - ctrl[static_cast<size_t>(d - 2)]) -
        D1[2] * inv_d3;
  }
  // G4 backward mirror, same alternating-sign pattern as curve.cpp's own.
  if (continuity >= 4) {
    ctrl[static_cast<size_t>(d - 4)] = ctrl[static_cast<size_t>(d - 3)] +
                                        3.0 * (ctrl[static_cast<size_t>(d - 3)] - ctrl[static_cast<size_t>(d - 2)]) -
                                        3.0 * (ctrl[static_cast<size_t>(d - 2)] - ctrl[static_cast<size_t>(d - 1)]) +
                                        (ctrl[static_cast<size_t>(d - 1)] - ctrl[static_cast<size_t>(d)]) +
                                        D1[3] * inv_d4;
  }
  return ctrl;
}

}  // namespace

Result NurbsSurface::BlendSurfaces(const NurbsSurface& srf0, int dir0, bool at_max0, const NurbsSurface& srf1,
                                    int dir1, bool at_max1, bool reverse_rail1, int continuity, int rows,
                                    NurbsSurface& out) {
  if (continuity < 1 || continuity > 4) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::BlendSurfaces: continuity must be 1 (G1), 2 (G2), 3 (G3) or 4 (G4)");
  }
  if (dir0 < 0 || dir0 > 1 || dir1 < 0 || dir1 > 1) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::BlendSurfaces: dir0/dir1 must be 0 or 1");
  }
  if (rows < 2) {
    throw std::invalid_argument("dino8::kernel::NurbsSurface::BlendSurfaces: rows must be at least 2");
  }
  const ON_Surface& s0 = srf0.raw();
  const ON_Surface& s1 = srf1.raw();
  const ON_Interval rail_dom0 = s0.Domain(dir0);
  const ON_Interval fixed_dom0 = s0.Domain(1 - dir0);
  const double fixed_t0 = at_max0 ? fixed_dom0.Max() : fixed_dom0.Min();
  const ON_Interval rail_dom1 = s1.Domain(dir1);
  const ON_Interval fixed_dom1 = s1.Domain(1 - dir1);
  const double fixed_t1 = at_max1 ? fixed_dom1.Max() : fixed_dom1.Min();
  const bool flip0 = !at_max0;
  const bool flip1 = !at_max1;

  std::vector<ON_NurbsCurve> rowCurves;
  rowCurves.reserve(static_cast<size_t>(rows));
  for (int k = 0; k < rows; ++k) {
    const double t = static_cast<double>(k) / static_cast<double>(rows - 1);
    const double rail_t0 = rail_dom0.ParameterAt(t);
    const double t1 = reverse_rail1 ? 1.0 - t : t;
    const double rail_t1 = rail_dom1.ParameterAt(t1);

    Point3d P0, P1;
    std::array<Vector3d, 4> D0{}, D1{};
    if (!EvaluateSurfaceBlendEnd(s0, dir0, rail_t0, fixed_t0, flip0, continuity, P0, D0) ||
        !EvaluateSurfaceBlendEnd(s1, dir1, rail_t1, fixed_t1, flip1, continuity, P1, D1)) {
      return Result::Failed;
    }
    const double scale = std::max(1.0, P0.DistanceTo(Point3d(0, 0, 0)));
    if (P0.DistanceTo(P1) <= 1e-9 * scale) return Result::Failed;

    const std::vector<Point3d> ctrl = HermiteBlendRow(P0, D0, P1, D1, continuity);
    rowCurves.push_back(NurbsCurve::FromControlPoints(ctrl, 2 * continuity + 1).raw());
  }

  const char* caller = "BlendSurfaces";
  const int q = std::min(3, rows - 1);
  double period = 1.0;
  const std::vector<double> params = SkinParameters(rowCurves, false, &period, caller);
  std::unique_ptr<ON_NurbsSurface> skin;
  if (q == 1 && rows == 2) {
    skin = RuledBetween(rowCurves[0], rowCurves[1], 0.0, 1.0, caller);
  } else {
    skin = SkinSections(rowCurves, q, false, params, period, caller);
  }
  out.surface_ = *skin;
  return Result::Ok;
}

namespace {

// Piecewise-linear lookup over a (t, value) schedule, held flat at the
// nearest endpoint's value outside the given range - the exact convention
// PipeVariable()'s own radius_at() uses, shared here verbatim.
double ScheduleAt(const std::vector<std::pair<double, double>>& schedule, double f) {
  if (f <= schedule.front().first) return schedule.front().second;
  if (f >= schedule.back().first) return schedule.back().second;
  for (size_t i = 1; i < schedule.size(); ++i) {
    if (f <= schedule[i].first) {
      const double t0 = schedule[i - 1].first, t1 = schedule[i].first;
      const double v0 = schedule[i - 1].second, v1 = schedule[i].second;
      return v0 + (f - t0) / (t1 - t0) * (v1 - v0);
    }
  }
  return schedule.back().second;  // unreachable given the f >= back() check above
}

void ValidateSweep1Schedule(const std::vector<std::pair<double, double>>* schedule, const char* name,
                            bool value_must_be_positive, const char* caller) {
  if (!schedule) return;
  if (schedule->size() < 2) Fail(caller, std::string(name) + " needs at least 2 points");
  for (size_t i = 0; i < schedule->size(); ++i) {
    const double t = (*schedule)[i].first;
    if (!std::isfinite(t) || t < 0.0 || t > 1.0) {
      Fail(caller, std::string(name) + " point " + std::to_string(i) + " has t outside [0, 1]");
    }
    if (value_must_be_positive && !((*schedule)[i].second > 0.0)) {
      Fail(caller, std::string(name) + " point " + std::to_string(i) + " has a non-positive value");
    }
    if (i > 0 && !(t > (*schedule)[i - 1].first)) {
      Fail(caller, std::string(name) + " points must be strictly increasing in t");
    }
  }
}

}  // namespace

Brep Brep::Sweep1(const NurbsCurve& section_in, const NurbsCurve& rail_in, int stations, bool cap, double twist_total,
                  double scale_end, const Vector3d* roadlike_up,
                  const std::vector<std::pair<double, double>>* twist_schedule,
                  const std::vector<std::pair<double, double>>* scale_schedule) {
  const char* caller = "Sweep1";
  if (stations < 2) Fail(caller, "stations must be at least 2");
  if (!(scale_end > 0.0)) Fail(caller, "scale_end must be positive");
  ON_NurbsCurve rail = rail_in.raw();
  if (!rail.IsValid()) Fail(caller, "rail is not a valid NURBS curve");
  ON_NurbsCurve section = section_in.raw();
  if (!section.IsValid()) Fail(caller, "section is not a valid NURBS curve");
  ClampIfPeriodic(section);
  const bool wrap = rail.IsClosed();

  ValidateSweep1Schedule(twist_schedule, "twist_schedule", /*value_must_be_positive=*/false, caller);
  ValidateSweep1Schedule(scale_schedule, "scale_schedule", /*value_must_be_positive=*/true, caller);
  if (twist_schedule && twist_total != 0.0) {
    Fail(caller, "twist_total must be 0 when twist_schedule is given - the two are mutually exclusive");
  }
  if (scale_schedule && scale_end != 1.0) {
    Fail(caller, "scale_end must be 1 when scale_schedule is given - the two are mutually exclusive");
  }
  if (wrap && (twist_total != 0.0 || twist_schedule)) {
    Fail(caller, "twist is not supported for a closed rail - a non-multiple-of-2*pi twist would keep the tube from "
                 "closing up smoothly, and the multiple-of-2*pi spiral case is not attempted here");
  }
  if (wrap && (scale_end != 1.0 || scale_schedule)) {
    Fail(caller, "scale is not supported for a closed rail - the tube would not meet itself at the seam");
  }
  ON_3dVector up;
  if (roadlike_up) {
    up = *roadlike_up;
    if (!up.Unitize()) Fail(caller, "roadlike_up must be a nonzero vector");
  }
  const bool straight = !wrap && rail.IsLinear(1e-9 * CurveScale(rail));
  const bool has_schedule = twist_schedule || scale_schedule;  // wrap already refused above when true

  // Station fractions (of the rail's own arc length from its start) and
  // the rail parameter at each.
  std::vector<double> fractions;
  std::vector<double> params;
  if (!has_schedule) {
    // Exactly the pre-existing construction, byte-for-byte - m = 2 on a
    // straight rail (the exact-extrusion shortcut), max(stations, 3)
    // otherwise; equal-arc-length stations via the wrapper's
    // DivideByCount(), with a closed rail's own last (repeated) division
    // point dropped. Every existing (non-schedule) call keeps its own
    // already-established numerical guarantees untouched, on a closed
    // rail in particular: DivideByCount()'s own m-point spacing (not the
    // schedule branch's arc-length-fraction reconstruction below, which
    // assumes a non-wrapping, non-repeating station list) is what
    // RmfFrames()'s own wrap holonomy correction requires.
    const int m_ = straight ? 2 : std::max(stations, 3);
    params = rail_in.DivideByCount(wrap ? m_ : m_ - 1);
    if (wrap) params.pop_back();
    if (static_cast<int>(params.size()) != m_) Internal(caller, "station count mismatch");
    fractions.resize(static_cast<size_t>(m_));
    for (int k = 0; k < m_; ++k) fractions[static_cast<size_t>(k)] = static_cast<double>(k) / static_cast<double>(m_ - 1);
  } else {
    // A schedule was given (wrap already refused above, so there is no
    // holonomy concern here). A schedule of exactly the two endpoints
    // {(0, .), (1, .)} on a straight rail collapses onto the SAME m = 2
    // exact-extrusion shortcut the plain scalar case already takes; any
    // other schedule (more breakpoints, or a curved rail) merges every
    // breakpoint's own fraction into the arc-length-equal grid - the
    // "insert exact stations at the given points" construction
    // PipeVariable() already uses for its own radius schedule.
    auto is_trivial_two_point = [](const std::vector<std::pair<double, double>>* s) {
      return !s || (s->size() == 2 && (*s)[0].first == 0.0 && (*s)[1].first == 1.0);
    };
    if (straight && is_trivial_two_point(twist_schedule) && is_trivial_two_point(scale_schedule)) {
      fractions = {0.0, 1.0};
      params = {rail.Domain().Min(), rail.Domain().Max()};
    } else {
      const double total_length = rail_in.Length();
      if (!(total_length > 0.0)) Fail(caller, "the rail has zero length");
      const int m_even = std::max(stations, 3);
      std::vector<double> raw;
      raw.reserve(static_cast<size_t>(m_even) + (twist_schedule ? twist_schedule->size() : 0) +
                  (scale_schedule ? scale_schedule->size() : 0));
      for (int k = 0; k < m_even; ++k) raw.push_back(static_cast<double>(k) / static_cast<double>(m_even - 1));
      if (twist_schedule) {
        for (const auto& p : *twist_schedule) raw.push_back(p.first);
      }
      if (scale_schedule) {
        for (const auto& p : *scale_schedule) raw.push_back(p.first);
      }
      std::sort(raw.begin(), raw.end());
      for (double f : raw) {
        if (fractions.empty() || f - fractions.back() > 1e-9) fractions.push_back(f);
      }
      if (fractions.size() < 2) Internal(caller, "station count mismatch");
      params.resize(fractions.size());
      for (size_t k = 0; k < fractions.size(); ++k) {
        params[k] = rail_in.ParameterAtArcLength(fractions[k] * total_length);
      }
    }
  }
  const int m = static_cast<int>(fractions.size());
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

  // Absolute twist/scale at a given arc-length fraction: the schedule's
  // own piecewise-linear value when given, otherwise the pre-existing
  // linear-from-the-start formula (0 / 1.0 at f = 0, exactly
  // twist_total / scale_end at f = 1).
  auto twist_at = [&](double f) { return twist_schedule ? ScheduleAt(*twist_schedule, f) : twist_total * f; };
  auto scale_at = [&](double f) {
    return scale_schedule ? ScheduleAt(*scale_schedule, f) : 1.0 + (scale_end - 1.0) * f;
  };

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

  // Every station's own transform is built the SAME way, including k = 0
  // (FrameToFrame(frames[0], frames[0]) is the identity, and the default
  // twist_at(0) == 0 / scale_at(0) == 1.0 formulas are exact zeros there
  // too) - unlike the old "skip k == 0" special case, this stays correct
  // even for a schedule anchored away from a literal zero at the start
  // (held-flat rather than forced to 0 / 1.0), rather than silently
  // leaving station 0 unrotated/unscaled while every later station reads
  // an absolute schedule value. The twist rotation is applied as its own
  // explicit rotation about the station's OWN tangent at its OWN origin
  // (mathematically identical to the old approach of baking the same
  // rotation into the RMF frame's own (r, s) before computing
  // FrameToFrame - both give the same rigid rotation mapping frame 0's
  // basis onto the twisted frame k's basis, since twisting a frame's
  // (r, s) about its own t and then reading the rotation from an
  // untwisted frame 0 is the same rotation as reading frame-0-to-
  // untwisted-frame-k first and rotating the RESULT about frame k's own
  // t afterward - the two are the same unique rigid rotation agreeing on
  // all three orthonormal axes), just decoupled from the frames array
  // itself so a nonzero twist_at(0) has somewhere to go.
  std::vector<ON_NurbsCurve> copies;
  copies.reserve(static_cast<size_t>(m));
  for (int k = 0; k < m; ++k) {
    ON_NurbsCurve ck = section;
    const Frame& fk = frames[static_cast<size_t>(k)];
    ON_Xform xf = FrameToFrame(frames[0], fk);
    const double a = twist_at(fractions[static_cast<size_t>(k)]);
    if (a != 0.0) {
      ON_Xform twist_xf;
      twist_xf.Rotation(a, fk.t, fk.origin);
      xf = twist_xf * xf;
    }
    const double s = scale_at(fractions[static_cast<size_t>(k)]);
    if (s != 1.0) {
      xf = ON_Xform::ScaleTransformation(fk.origin, s) * xf;
    }
    if (a != 0.0 || s != 1.0 || k > 0) ck.Transform(xf);
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

Brep Brep::Sweep2Sections(const std::vector<NurbsCurve>& sections_in, const NurbsCurve& rail1_in,
                          const NurbsCurve& rail2_in, int stations, Sweep2SectionsReport* report) {
  const char* caller = "Sweep2Sections";
  if (stations < 2) Fail(caller, "stations must be at least 2");
  if (sections_in.size() < 2) Fail(caller, "at least 2 sections are required (one section is Sweep2())");
  ON_NurbsCurve rail1 = rail1_in.raw();
  ON_NurbsCurve rail2 = rail2_in.raw();
  if (!rail1.IsValid()) Fail(caller, "rail1 is not a valid NURBS curve");
  if (!rail2.IsValid()) Fail(caller, "rail2 is not a valid NURBS curve");
  if (rail1.IsClosed() && rail2.IsClosed()) {
    Fail(caller, "two closed rails are not supported with several sections (use Sweep2() with one section)");
  }
  // Same direction convention as Sweep2().
  if (rail1.PointAtStart().DistanceTo(rail2.PointAtEnd()) < rail1.PointAtStart().DistanceTo(rail2.PointAtStart())) {
    ReverseKeepDomain(rail2);
  }

  std::vector<ON_NurbsCurve> sections;
  sections.reserve(sections_in.size());
  for (const NurbsCurve& s : sections_in) {
    sections.push_back(s.raw());
    if (!sections.back().IsValid()) Fail(caller, "a section is not a valid NURBS curve");
    ClampIfPeriodic(sections.back());
  }

  const NurbsCurve k_rail1 = [&] { NurbsCurve k; k.raw() = rail1; return k; }();
  const NurbsCurve k_rail2 = [&] { NurbsCurve k; k.raw() = rail2; return k; }();
  auto closest_on = [&](const NurbsCurve& k, const ON_3dPoint& p) { return k.ClosestPointParameter(Point3d(p.x, p.y, p.z), 400); };
  // Parameter at which rail `r` crosses `plane` (the crossing nearest `t0`;
  // `t0` itself if the rail never crosses it): a 256-interval sign scan, each
  // bracket bisected to round-off.
  auto plane_crossing = [&](const ON_NurbsCurve& r, const ON_Plane& plane, double t0) {
    const double lo_d = r.Domain().Min(), hi_d = r.Domain().Max();
    const auto f = [&](double t) { return ON_DotProduct(r.PointAt(t) - plane.origin, plane.zaxis); };
    double best = t0, best_gap = std::numeric_limits<double>::infinity();
    double prev_t = lo_d, prev_f = f(lo_d);
    for (int i = 1; i <= 256; ++i) {
      const double t = lo_d + (hi_d - lo_d) * i / 256.0, ft = f(t);
      if (prev_f == 0.0 || prev_f * ft < 0.0 || ft == 0.0) {
        double lo = prev_t, hi = t, flo = prev_f;
        for (int it = 0; it < 80; ++it) {
          const double mid = 0.5 * (lo + hi), fm = f(mid);
          if (flo * fm <= 0.0) hi = mid; else { lo = mid; flo = fm; }
        }
        const double root = 0.5 * (lo + hi);
        if (std::fabs(root - t0) < best_gap) { best_gap = std::fabs(root - t0); best = root; }
      }
      prev_t = t;
      prev_f = ft;
    }
    return best;
  };
  // Anchor of every section: the rail1 parameter t1 and rail2 parameter t2 of
  // the station its own frame is decoded at. An open section is turned so it
  // starts on the rail1 side, then anchored at the closest rail1 point to its
  // start and the closest rail2 point to its end (a section drawn between the
  // rails touches both there); a closed section is anchored at the closest
  // point of each rail to its control-point centroid.
  const double scale = 1.0 + CurveScale(rail1) + CurveScale(rail2);
  struct Anchor {
    double t1 = 0.0, t2 = 0.0;
    size_t section = 0;
  };
  std::vector<Anchor> anchors(sections.size());
  double section_rail_gap = 0.0;
  bool all_open = true;
  for (size_t i = 0; i < sections.size(); ++i) {
    ON_NurbsCurve& s = sections[i];
    ON_3dPoint p_for_1, p_for_2;
    ON_Plane closed_plane;
    bool closed_planar = false;
    if (s.IsClosed()) {
      all_open = false;
      ON_3dPoint centroid(0, 0, 0);
      for (int c = 0; c < s.CVCount(); ++c) centroid += ON_3dVector(EuclideanCV(s, c));
      centroid = ON_3dPoint(centroid.x / s.CVCount(), centroid.y / s.CVCount(), centroid.z / s.CVCount());
      p_for_1 = p_for_2 = centroid;
      closed_planar = s.IsPlanar(&closed_plane, 1e-8 * CurveScale(s));
    } else {
      const double ts = closest_on(k_rail1, s.PointAtStart()), te = closest_on(k_rail1, s.PointAtEnd());
      if (rail1.PointAt(te).DistanceTo(s.PointAtEnd()) < rail1.PointAt(ts).DistanceTo(s.PointAtStart())) {
        ReverseKeepDomain(s);
      }
      p_for_1 = s.PointAtStart();
      p_for_2 = s.PointAtEnd();
    }
    Anchor a;
    a.section = i;
    a.t1 = closest_on(k_rail1, p_for_1);
    a.t2 = closest_on(k_rail2, p_for_2);
    // A closed PLANAR section is anchored where each rail crosses its plane
    // (the nearest crossing to the centroid's closest point) - for a section
    // not square to slanted rails the centroid's closest point sits at a
    // different station than the section itself.
    if (closed_planar) {
      a.t1 = plane_crossing(rail1, closed_plane, a.t1);
      a.t2 = plane_crossing(rail2, closed_plane, a.t2);
    }
    if (!s.IsClosed()) {
      section_rail_gap = std::max({section_rail_gap, rail1.PointAt(a.t1).DistanceTo(p_for_1), rail2.PointAt(a.t2).DistanceTo(p_for_2)});
    }
    anchors[i] = a;
  }
  std::sort(anchors.begin(), anchors.end(), [](const Anchor& a, const Anchor& b) { return a.t1 < b.t1; });

  // Rail arc lengths (a rail's length up to a parameter, and a parameter at a
  // fraction of the length of a sub-range).
  // (a 512-chord polyline table of the sub-range: station SPACING only, so a
  // relative error of ~1e-6 here moves a station along the rail, never off it)
  auto arc_table = [&](const ON_NurbsCurve& r, double a, double b) {
    std::vector<double> cum(513, 0.0);
    ON_3dPoint prev = r.PointAt(a);
    for (int q = 1; q <= 512; ++q) {
      const ON_3dPoint cur = r.PointAt(a + (b - a) * q / 512.0);
      cum[static_cast<size_t>(q)] = cum[static_cast<size_t>(q - 1)] + prev.DistanceTo(cur);
      prev = cur;
    }
    return cum;
  };
  auto arc_len = [&](const ON_NurbsCurve& r, double a, double b) { return b > a ? arc_table(r, a, b).back() : 0.0; };
  auto param_at_fraction = [&](const ON_NurbsCurve& r, double a, double b, double f) {
    const std::vector<double> cum = arc_table(r, a, b);
    const double target = f * cum.back();
    size_t q = static_cast<size_t>(std::upper_bound(cum.begin(), cum.end(), target) - cum.begin());
    if (q == 0) q = 1;
    if (q > 512) q = 512;
    const double span = cum[q] - cum[q - 1];
    const double frac = span > 0.0 ? (target - cum[q - 1]) / span : 0.0;
    return a + (b - a) * (static_cast<double>(q - 1) + frac) / 512.0;
  };
  const double d1_min = rail1.Domain().Min(), d1_max = rail1.Domain().Max();
  const double d2_min = rail2.Domain().Min(), d2_max = rail2.Domain().Max();
  const double len1_total = arc_len(rail1, d1_min, d1_max), len2_total = arc_len(rail2, d2_min, d2_max);
  if (!(len1_total > 1e-9 * scale) || !(len2_total > 1e-9 * scale)) Fail(caller, "a rail has zero length");
  const double eps1 = 1e-9 * (d1_max - d1_min), eps2 = 1e-9 * (d2_max - d2_min);

  // Breakpoints: both rails' parameters at the domain start, every anchor and
  // the domain end. Anchors must advance on BOTH rails (the sections are
  // ordered along the sweep, and cannot cross or share a station).
  std::vector<double> bp1{d1_min}, bp2{d2_min};
  std::vector<int> anchor_at{-1};  // anchor index (into `anchors`) living at each breakpoint
  for (size_t i = 0; i < anchors.size(); ++i) {
    const double t1 = anchors[i].t1, t2 = anchors[i].t2;
    const bool same1 = t1 - bp1.back() <= eps1, same2 = t2 - bp2.back() <= eps2;
    if (same1 != same2 || t2 < bp2.back() - eps2) {
      Fail(caller, "the sections are not ordered consistently along the two rails (their anchors cross)");
    }
    if (same1 && same2) {
      if (anchor_at.back() != -1) Fail(caller, "two sections sit at the same station of the rails");
      anchor_at.back() = static_cast<int>(i);
      continue;
    }
    bp1.push_back(t1);
    bp2.push_back(t2);
    anchor_at.push_back(static_cast<int>(i));
  }
  if (d1_max - bp1.back() > eps1 || d2_max - bp2.back() > eps2) {
    if ((d1_max - bp1.back() > eps1) != (d2_max - bp2.back() > eps2)) {
      Fail(caller, "the last section is at the end of one rail but not the other");
    }
    bp1.push_back(d1_max);
    bp2.push_back(d2_max);
    anchor_at.push_back(-1);
  }
  const size_t nbp = bp1.size();
  if (nbp < 2) Fail(caller, "the rails are degenerate");

  // Segment lengths along rail1 (used for the station budget and the blend's
  // abscissa), then the station list: segment s gets round(stations-1) times
  // its share of rail1's length subdivisions, never fewer than 4, each station
  // at the same arc-length FRACTION of the segment on both rails (the pairing
  // Sweep2() uses over the whole rails). Breakpoints are placed exactly.
  std::vector<double> x_at_bp(nbp, 0.0);  // rail1 arc length at each breakpoint
  for (size_t b = 1; b < nbp; ++b) x_at_bp[b] = x_at_bp[b - 1] + arc_len(rail1, bp1[b - 1], bp1[b]);
  std::vector<double> p1, p2, xs;  // station parameters on each rail and the rail1 abscissa
  std::vector<int> station_of_bp(nbp, 0);
  p1.push_back(bp1[0]);
  p2.push_back(bp2[0]);
  xs.push_back(0.0);
  for (size_t b = 1; b < nbp; ++b) {
    const double seg = x_at_bp[b] - x_at_bp[b - 1];
    const int sub = std::max(4, static_cast<int>(std::lround((stations - 1) * seg / x_at_bp.back())));
    for (int q = 1; q < sub; ++q) {
      const double f = static_cast<double>(q) / sub;
      p1.push_back(param_at_fraction(rail1, bp1[b - 1], bp1[b], f));
      p2.push_back(param_at_fraction(rail2, bp2[b - 1], bp2[b], f));
      xs.push_back(x_at_bp[b - 1] + f * seg);
    }
    p1.push_back(bp1[b]);
    p2.push_back(bp2[b]);
    xs.push_back(x_at_bp[b]);
    station_of_bp[b] = static_cast<int>(p1.size()) - 1;
  }
  const int m = static_cast<int>(p1.size());
  const std::vector<TwoRailFrame> frames = TwoRailFrames(rail1, rail2, p1, p2, caller);

  // Common control structure, then every section decoded ONCE into its own
  // anchor station's frame: the local coordinates of a control point are its
  // offsets along (x, y, z) from that frame's origin, divided by its width
  // (the same single uniform scale Sweep2() uses), and its weight.
  std::vector<ON_NurbsCurve> compat;
  compat.reserve(anchors.size());
  for (const Anchor& a : anchors) compat.push_back(sections[a.section]);
  MakeCompatible(compat, caller);
  const int n = compat.front().CVCount();
  const size_t na = anchors.size();
  std::vector<std::vector<ON_3dVector>> local(na, std::vector<ON_3dVector>(static_cast<size_t>(n)));
  std::vector<std::vector<double>> weight(na, std::vector<double>(static_cast<size_t>(n), 1.0));
  std::vector<int> anchor_station(na, 0);
  std::vector<double> anchor_x(na, 0.0);
  for (size_t b = 0; b < nbp; ++b) {
    if (anchor_at[b] >= 0) {
      anchor_station[static_cast<size_t>(anchor_at[b])] = station_of_bp[b];
      anchor_x[static_cast<size_t>(anchor_at[b])] = x_at_bp[b];
    }
  }
  for (size_t i = 0; i < na; ++i) {
    const TwoRailFrame& f = frames[static_cast<size_t>(anchor_station[i])];
    for (int c = 0; c < n; ++c) {
      const ON_3dVector d = EuclideanCV(compat[i], c) - f.origin;
      local[i][static_cast<size_t>(c)] =
          ON_3dVector(ON_DotProduct(d, f.x), ON_DotProduct(d, f.y), ON_DotProduct(d, f.z)) / f.width;
      weight[i][static_cast<size_t>(c)] = compat[i].Weight(c);
    }
  }
  // Catmull-Rom tangent of every local coordinate at every anchor, against the
  // rail1 arc-length abscissa (one-sided chord slope at the two ends; for two
  // sections that is the chord, i.e. a linear blend).
  std::vector<std::vector<ON_3dVector>> slope(na, std::vector<ON_3dVector>(static_cast<size_t>(n)));
  for (size_t i = 0; i < na; ++i) {
    const size_t lo = i == 0 ? 0 : i - 1, hi = i + 1 == na ? na - 1 : i + 1;
    const double dx = anchor_x[hi] - anchor_x[lo];
    for (int c = 0; c < n; ++c) {
      slope[i][static_cast<size_t>(c)] = (local[hi][static_cast<size_t>(c)] - local[lo][static_cast<size_t>(c)]) / dx;
    }
  }

  // One copy of the section per station: local coordinates blended between the
  // two neighbouring anchors (cubic Hermite, constant beyond the first and last
  // anchor), weights blended linearly, then placed into that station's frame.
  std::vector<ON_NurbsCurve> copies;
  copies.reserve(static_cast<size_t>(m));
  size_t seg = 0;
  for (int k = 0; k < m; ++k) {
    while (seg + 1 < na && anchor_station[seg + 1] <= k) ++seg;
    const TwoRailFrame& f = frames[static_cast<size_t>(k)];
    ON_NurbsCurve ck = compat.front();
    const bool before_first = k <= anchor_station[0];
    const bool after_last = k >= anchor_station[na - 1];
    for (int c = 0; c < n; ++c) {
      ON_3dVector l;
      double w;
      if (before_first || after_last) {
        const size_t e = before_first ? 0 : na - 1;
        l = local[e][static_cast<size_t>(c)];
        w = weight[e][static_cast<size_t>(c)];
      } else {
        const double x0 = anchor_x[seg], x1 = anchor_x[seg + 1], h = x1 - x0;
        const double s = (xs[static_cast<size_t>(k)] - x0) / h;
        const double s2 = s * s, s3 = s2 * s;
        l = local[seg][static_cast<size_t>(c)] * (2 * s3 - 3 * s2 + 1) + slope[seg][static_cast<size_t>(c)] * (h * (s3 - 2 * s2 + s)) +
            local[seg + 1][static_cast<size_t>(c)] * (-2 * s3 + 3 * s2) + slope[seg + 1][static_cast<size_t>(c)] * (h * (s3 - s2));
        w = weight[seg][static_cast<size_t>(c)] * (1.0 - s) + weight[seg + 1][static_cast<size_t>(c)] * s;
      }
      const ON_3dPoint p = f.origin + (f.x * l.x + f.y * l.y + f.z * l.z) * f.width;
      ck.SetCV(c, ON_4dPoint(p.x * w, p.y * w, p.z * w, w));
    }
    copies.push_back(std::move(ck));
  }
  MakeCompatible(copies, caller);  // no-op (same control structure at every station)
  double period = 1.0;
  const std::vector<double> params_v = SkinParameters(copies, false, &period, caller);
  std::unique_ptr<ON_NurbsSurface> wall = SkinSections(copies, std::min(3, m - 1), false, params_v, period, caller);

  if (report) {
    *report = Sweep2SectionsReport{};
    report->sections = static_cast<int>(na);
    report->stations = m;
    report->section_rail_gap = section_rail_gap;
    report->rails_on_boundary = all_open;
    // Every section is reproduced at its own station: compare the wall's
    // isocurve there (same u parameterization as the compatible section, both
    // on [0, 1]) with that section.
    const ON_Interval du = wall->Domain(0);
    for (size_t i = 0; i < na; ++i) {
      const double v = params_v[static_cast<size_t>(anchor_station[i])];
      for (int q = 0; q <= 64; ++q) {
        const double u = du.ParameterAt(q / 64.0);
        const double t = compat[i].Domain().ParameterAt(q / 64.0);
        report->max_section_deviation = std::max(report->max_section_deviation, wall->PointAt(u, v).DistanceTo(compat[i].PointAt(t)));
      }
    }
    if (all_open) {
      const ON_Interval dv = wall->Domain(1);
      for (int q = 0; q <= 400; ++q) {
        const double v = dv.ParameterAt(q / 400.0);
        const ON_3dPoint a = wall->PointAt(du.Min(), v), b = wall->PointAt(du.Max(), v);
        report->max_rail1_deviation = std::max(report->max_rail1_deviation, a.DistanceTo(rail1.PointAt(closest_on(k_rail1, a))));
        report->max_rail2_deviation = std::max(report->max_rail2_deviation, b.DistanceTo(rail2.PointAt(closest_on(k_rail2, b))));
      }
    }
  }
  return AssembleSweptBody(wall.release(), false, false, false, false, caller);
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
