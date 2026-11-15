// SubD::EvaluateFace() on faces whose neighbourhood is TAGGED (a naked boundary, an interior crease, a pinned corner), and the
// least-squares fit that evaluates every irregular patch through it.
//
// Before: a tagged neighbourhood was evaluated on a clone of the WHOLE SubD that was then refined as a whole net up to four times, so
// one call cost O(net) and evaluating every boundary face of an N-face sheet was quadratic (measured: ToNurbsPatchesWatertight with
// fit_interior on an open 20 x 20 sheet took 58.8 s, 40 x 40 did not finish). The neighbourhood is now copied locally, with its crease
// tags and pinned corners, and the recursion works on that copy.
//
// Oracles:
//   * cost: CPU time of the same 300 boundary-face evaluations on a 10 x 10 and on a 60 x 60 sheet (36x the faces) - with a clone of
//     the net the ratio is about the face ratio, with a local copy it is about 1;
//   * accuracy: every sample EvaluateFace reports as exact is compared with the independent limit-point mesh built HERE from
//     SubD::LimitPoints() of the cage subdivided 4 more times (a creased cube whose single crease edge ends in two darts);
//   * the fit: its CPU time per irregular patch must not grow with the net.
#include <cmath>
#include <ctime>
#include <map>
#include <tuple>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::SubDNurbsPatch;
using dino8::kernel::SubDSurfacePoint;
using dino8::kernel::SubDWatertightReport;

Mesh TcSheet(int n) {
  Mesh m;
  ON_Mesh& raw = m.raw();
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i) raw.m_V.Append(ON_3fPoint(float(i), float(j), float(0.3 * std::sin(0.7 * i) * std::cos(0.5 * j))));
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      ON_MeshFace f;
      f.vi[0] = j * (n + 1) + i; f.vi[1] = j * (n + 1) + i + 1; f.vi[2] = (j + 1) * (n + 1) + i + 1; f.vi[3] = (j + 1) * (n + 1) + i;
      raw.m_F.Append(f);
    }
  }
  return m;
}

Mesh TcBox(double h) {
  Mesh m;
  ON_Mesh& raw = m.raw();
  const double s[2] = {-h, h};
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(float(s[x]), float(s[y]), float(s[z])));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

Mesh TcLimitMesh(const SubD& s) {
  const Mesh net = s.ToApproximateMesh();
  std::map<std::tuple<float, float, float>, Point3d> lim;
  for (const dino8::kernel::SubDLimitPoint& lp : s.LimitPoints()) {
    lim[std::make_tuple(float(lp.control_point.x), float(lp.control_point.y), float(lp.control_point.z))] = lp.limit_point;
  }
  Mesh out;
  ON_Mesh& m = out.raw();
  const ON_Mesh& n = net.raw();
  for (int i = 0; i < n.m_V.Count(); ++i) {
    const auto it = lim.find(std::make_tuple(n.m_V[i].x, n.m_V[i].y, n.m_V[i].z));
    m.m_V.Append(it == lim.end() ? n.m_V[i] : ON_3fPoint(it->second));
  }
  for (int i = 0; i < n.m_F.Count(); ++i) m.m_F.Append(n.m_F[i]);
  return out;
}

double CpuSeconds() { return static_cast<double>(std::clock()) / CLOCKS_PER_SEC; }

// CPU time of 300 EvaluateFace calls on faces of the bottom row (every one has a naked boundary edge).
double BoundaryEvalSeconds(int n) {
  const SubD sheet = SubD::FromControlMesh(TcSheet(n));
  const double t0 = CpuSeconds();
  int calls = 0;
  for (int rep = 0; rep < 4; ++rep) {
    for (int id = 1; id <= n && calls < 300; ++id) {
      for (double u : {0.2, 0.7}) {
        (void)sheet.EvaluateFace(static_cast<unsigned int>(id), u, 0.3 + 0.1 * rep);
        ++calls;
      }
    }
  }
  return CpuSeconds() - t0;
}

double FitSecondsPerPatch(int n, int* fitted) {
  const SubD sheet = SubD::FromControlMesh(TcSheet(n));
  SubDWatertightReport rep;
  const double t0 = CpuSeconds();
  (void)sheet.ToNurbsPatchesWatertight(&rep, true);
  const double dt = CpuSeconds() - t0;
  if (fitted != nullptr) *fitted = rep.fitted_patches;
  return rep.coons_patches > 0 ? dt / rep.coons_patches : dt;
}

}  // namespace

D8_EXTRA_TEST(SubDEvaluateFaceOnATaggedNeighbourhoodCostsTheSameOnABigNet) {
  const double small = std::max(BoundaryEvalSeconds(10), 1e-4);
  const double big = BoundaryEvalSeconds(60);
  char what[200];
  std::snprintf(what, sizeof what, "300 boundary-face evaluations: %.3fs on 100 faces, %.3fs on 3600 faces (ratio %.1f, a whole-net clone would be about 36)", small, big, big / small);
  d8test::Check(big < 12.0 * small, what);
}

D8_EXTRA_TEST(SubDEvaluateFaceNearACreaseDartIsExactWhereItSaysSoAndNearTheLimitMesh) {
  SubD cube = SubD::FromControlMesh(TcBox(5.0));
  d8test::Check(cube.SetCrease(Point3d(5, 5, -5), Point3d(5, 5, 5), true, 1e-6), "creased one cube edge (its two ends are darts)");
  cube.Subdivide(1);
  SubD ref = cube;
  ref.Subdivide(4);
  const Mesh lm = TcLimitMesh(ref);
  const dino8::kernel::MeshClosestPointIndex idx(lm);
  int exact = 0, inexact = 0, bad_exact = 0;
  double worst_exact = 0.0;
  for (int id = 1; id <= cube.FaceCount(); ++id) {
    for (double u : {0.0, 0.125, 0.5, 0.875, 1.0}) {
      for (double v : {0.0, 0.3, 0.75, 1.0}) {
        const SubDSurfacePoint p = cube.EvaluateFace(static_cast<unsigned int>(id), u, v);
        const Point3d q(p.position.x, p.position.y, p.position.z);
        const double d = (idx.ClosestPoint(q) - q).Length();
        if (p.exact) {
          ++exact;
          worst_exact = std::max(worst_exact, d);
          if (d > 0.02) ++bad_exact;
        } else {
          ++inexact;
        }
      }
    }
  }
  char what[200];
  std::snprintf(what, sizeof what, "%d samples reported exact, worst distance to the 4-levels-deeper test-built limit mesh %.5f; %d reported approximate", exact, worst_exact, inexact);
  d8test::Check(bad_exact == 0 && exact > 0, what);
  d8test::Check(exact >= 470 && inexact <= 10, "all but a handful of the 480 samples are exact now (the whole-net fallback left about 390 of them approximate, up to 3.7 off)");
}

D8_EXTRA_TEST(SubDFitInteriorCostPerIrregularPatchDoesNotGrowWithTheNet) {
  int f_small = 0, f_big = 0;
  const double small = std::max(FitSecondsPerPatch(8, &f_small), 1e-6);
  const double big = FitSecondsPerPatch(24, &f_big);
  char what[220];
  std::snprintf(what, sizeof what, "fit time per irregular patch: %.4fs on an 8 x 8 sheet (%d fitted), %.4fs on a 24 x 24 sheet (%d fitted): ratio %.1f (quadratic would be about 3)", small, f_small, big, f_big, big / small);
  d8test::Check(big < 2.0 * small && f_small > 0 && f_big > f_small, what);
}
