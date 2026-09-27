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

}  // namespace

// Assembles the wall plus the requested caps. `cap_v0`/`cap_v1` cap the
// sweep's start/end (the wall's south/north sides); `cap_u0`/`cap_u1`
// cap the wall's west/east sides (a full revolve's off-axis end circles).
Brep Brep::AssembleSweptBody(ON_NurbsSurface* wall_raw, bool cap_v0, bool cap_v1, bool cap_u0, bool cap_u1,
                             const char* caller) {
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
    if (chord && !(sing[1] && sing[3])) {
      Fail(caller, "cap requested but the section is open and its ends do not collapse to the axis");
    }
    if (sing[0] || sing[2]) Fail(caller, "cap requested on an end that collapses to a point");
    // Both caps must use ONE apex when they share the chord (axis) edges.
    std::unique_ptr<ON_NurbsCurve> b0(IsoCurveOf(wall, 0, dv.Min(), caller));
    std::unique_ptr<ON_NurbsCurve> b1(IsoCurveOf(wall, 0, dv.Max(), caller));
    CapPlan plan0 = PlanCap(*b0, chord, caller);
    CapPlan plan1 = PlanCap(*b1, chord, caller);
    // Chord caps share the axis-segment edges and therefore the apex
    // vertex: the apex lies on the axis, which the sweep leaves fixed, so
    // plan1's own (independently found, rounding-different) apex is the
    // same point - use plan0's literally.
    if (chord) plan1.apex = plan0.apex;
    CapEdges first;
    bool have_first = false;
    if (cap_v0) {
      ON_3dVector su, sv;
      derivs(um, dv.Min(), su, sv);
      // Wall south edge runs +u (bRev3d[0] false): with the boundary.
      first = AddFanCap(brep, *b0, eid[0], /*edge_forward=*/true, -sv, plan0, nullptr, /*transpose=*/false, caller);
      have_first = true;
      ++faces;
    }
    if (cap_v1) {
      ON_3dVector su, sv;
      derivs(um, dv.Max(), su, sv);
      // Wall north edge: isocurve reversed at creation (i == 2, !bRev3d), so it runs -u: against the boundary.
      AddFanCap(brep, *b1, eid[2], /*edge_forward=*/false, sv, plan1, (chord && have_first) ? &first : nullptr,
                /*transpose=*/false, caller);
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
  if (std::fabs(along) <= 1.0 - 1e-9) {
    Fail(caller,
         "direction must be parallel to the profile's own plane normal - an oblique draft direction would need "
         "the in-plane offset and the extrusion translation decomposed separately, which this does not attempt");
  }

  // Positive draft_angle SHRINKS the profile moving along +direction (see
  // this function's own brep.h doc comment for the convention and why
  // the sign here is the negative of L * tan(draft_angle)).
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

Brep Brep::Revolve(const NurbsCurve& profile, Point3d axis_point, Vector3d axis_direction, double angle, bool cap) {
  const char* caller = "Revolve";
  ON_3dVector T = axis_direction;
  if (!T.Unitize()) Fail(caller, "axis_direction must be non-zero");
  if (!(angle > 0.0) || angle > 2.0 * ON_PI + 1e-12) Fail(caller, "angle must be in (0, 2*pi] radians");
  const bool full = std::fabs(angle - 2.0 * ON_PI) <= 1e-12;
  ON_NurbsCurve c = profile.raw();
  if (!c.IsValid()) Fail(caller, "profile is not a valid NURBS curve");
  ClampIfPeriodic(c);
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
  if (min_rho < -1e-8 * scale) Fail(caller, "profile must stay on one side of the axis (it crosses it)");

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

Brep Brep::Sweep1(const NurbsCurve& section_in, const NurbsCurve& rail_in, int stations, bool cap, double twist_total) {
  const char* caller = "Sweep1";
  if (stations < 2) Fail(caller, "stations must be at least 2");
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
  const bool straight = !wrap && rail.IsLinear(1e-9 * CurveScale(rail));
  const int m = straight ? 2 : std::max(stations, 3);

  // Equal-arc-length stations (the wrapper's DivideByCount); a closed
  // rail's last division point is its first and is dropped.
  std::vector<double> params = rail_in.DivideByCount(wrap ? m : m - 1);
  if (wrap) params.pop_back();
  if (static_cast<int>(params.size()) != m) Internal(caller, "station count mismatch");
  std::vector<Frame> frames = RmfFrames(rail, params, wrap, caller);
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
    if (k > 0) ck.Transform(FrameToFrame(frames[0], frames[static_cast<size_t>(k)]));
    copies.push_back(std::move(ck));
  }
  MakeCompatible(copies, caller);  // no-op for rigid copies; keeps one code path
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

Brep Brep::Pipe(const NurbsCurve& rail, double radius, bool cap, int stations) {
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
  return Sweep1(section, rail, stations, cap);
}

Brep Brep::PipeVariable(const NurbsCurve& rail_in, const std::vector<std::pair<double, double>>& radius_points,
                        bool cap, int stations) {
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
  return AssembleSweptBody(wall.release(), want_caps, want_caps, false, false, caller);
}

}  // namespace dino8::kernel
