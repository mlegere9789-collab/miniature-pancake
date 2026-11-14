// SubD::ToNurbsPatchesWatertight(report, fit_interior = true) / ToNurbsPatchesToTolerance(..., fit_interior = true).
//
// The Coons interior of an irregular patch is a bilinear blend of its boundary curves and the boundary of an irregular patch is a
// cubic Hermite built from end tangents; neither follows the limit surface near an extraordinary vertex. With fit_interior the
// non-exact edge curves and the four interior control points are least-squares fitted to the limit surface (EvaluateFace samples),
// with the corners and the shared edge curves identical on both faces, so the polysurface stays watertight.
//
// Oracles (independent of the report the fit itself produces):
//   * an independent dense deviation: 33 x 33 samples per patch against a limit-point mesh built HERE from SubD::LimitPoints() of the
//     cage subdivided 4 more times;
//   * the closed form of the Catmull-Clark limit point of a valence-3 cube corner, (n^2 v + 4 sum(edge nbrs) + sum(face-diagonal
//     nbrs)) / (n (n + 5)) = (2.5, 2.5, 2.5) for the cube [0,10]^3: patch corners must stay exactly there;
//   * the uniform bicubic B-spline: a regular torus cage has only exact patches, fit_interior must change nothing there;
//   * seam sharing: every patch boundary curve occurs exactly twice.
#include <cmath>
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
using dino8::kernel::SubDFitReport;
using dino8::kernel::SubDNurbsPatch;
using dino8::kernel::SubDWatertightReport;

Mesh FiBox(double x0, double y0, double z0, double x1, double y1, double z1) {
  Mesh m;
  ON_Mesh& raw = m.raw();
  const double xs[2] = {x0, x1}, ys[2] = {y0, y1}, zs[2] = {z0, z1};
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(float(xs[x]), float(ys[y]), float(zs[z])));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

// Limit-point mesh of `s` built here: vertex positions are SubD::LimitPoints(), connectivity ToApproximateMesh() matched by position.
Mesh FiLimitMesh(const SubD& s) {
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

double FiDenseDeviation(const std::vector<SubDNurbsPatch>& patches, const SubD& cage_at_patch_level) {
  SubD ref = cage_at_patch_level;
  ref.Subdivide(4);
  const Mesh lm = FiLimitMesh(ref);
  const dino8::kernel::MeshClosestPointIndex idx(lm);
  double worst = 0.0;
  for (const SubDNurbsPatch& p : patches) {
    for (int i = 0; i <= 32; ++i)
      for (int j = 0; j <= 32; ++j) {
        const Point3d q = p.surface.PointAt(i / 32.0, j / 32.0);
        worst = std::max(worst, (idx.ClosestPoint(q) - q).Length());
      }
  }
  return worst;
}

int FiSeamViolations(const std::vector<SubDNurbsPatch>& patches) {
  std::map<std::vector<long long>, int> count;
  const auto push = [&](const SubDNurbsPatch& p, bool along_u, double fixed) {
    std::vector<Point3d> pts;
    for (int k = 0; k <= 6; ++k) pts.push_back(along_u ? p.surface.PointAt(k / 6.0, fixed) : p.surface.PointAt(fixed, k / 6.0));
    const auto flat = [](const std::vector<Point3d>& v) {
      std::vector<long long> key;
      for (const Point3d& q : v) { key.push_back(std::llround(q.x * 1e7)); key.push_back(std::llround(q.y * 1e7)); key.push_back(std::llround(q.z * 1e7)); }
      return key;
    };
    std::vector<Point3d> rev(pts.rbegin(), pts.rend());
    ++count[std::min(flat(pts), flat(rev))];
  };
  for (const SubDNurbsPatch& p : patches) { push(p, true, 0.0); push(p, true, 1.0); push(p, false, 0.0); push(p, false, 1.0); }
  int bad = 0;
  for (const auto& kv : count) if (kv.second != 2) ++bad;
  return bad;
}

}  // namespace

D8_EXTRA_TEST(SubDFitInteriorLowersTheDeviationOfIrregularPatchesAtEveryLevel) {
  const SubD cube = SubD::FromControlMesh(FiBox(0, 0, 0, 10, 10, 10));
  for (int level = 1; level <= 2; ++level) {
    SubD cage = cube;
    cage.Subdivide(level);
    SubDWatertightReport coons_rep, fit_rep;
    const std::vector<SubDNurbsPatch> coons = cage.ToNurbsPatchesWatertight(&coons_rep, false);
    const std::vector<SubDNurbsPatch> fit = cage.ToNurbsPatchesWatertight(&fit_rep, true);
    const double dc = FiDenseDeviation(coons, cage), df = FiDenseDeviation(fit, cage);
    char what[220];
    std::snprintf(what, sizeof what, "level %d: independent dense deviation %.5f (fitted) vs %.5f (Coons interior): the fit is at least 2.5x closer", level, df, dc);
    d8test::Check(df * 2.5 <= dc, what);
    d8test::Check(coons_rep.fitted_patches == 0 && fit_rep.fitted_patches == fit_rep.coons_patches && fit_rep.coons_patches > 0 && fit.size() == coons.size(),
                  "default off fits nothing; with fit_interior every irregular patch is fitted and the patch count is unchanged");
    d8test::Check(FiSeamViolations(fit) == 0, "the fitted polysurface is still watertight: every boundary curve is shared by exactly two patches");
  }
}

D8_EXTRA_TEST(SubDFitInteriorKeepsPatchCornersAtTheClosedFormLimitPoints) {
  const SubD cube = SubD::FromControlMesh(FiBox(0, 0, 0, 10, 10, 10));
  const double corner = (4.0 * 10.0 + 20.0) / 24.0;  // (9*0 + 4*(10,10,10) + (20,20,20)) / 24 = 2.5
  SubD cage = cube;
  cage.Subdivide(1);
  const std::vector<SubDNurbsPatch> fit = cage.ToNurbsPatchesWatertight(nullptr, true);
  int found = 0;
  for (int cx = 0; cx < 2; ++cx)
    for (int cy = 0; cy < 2; ++cy)
      for (int cz = 0; cz < 2; ++cz) {
        const Point3d want(cx ? 10.0 - corner : corner, cy ? 10.0 - corner : corner, cz ? 10.0 - corner : corner);
        bool hit = false;
        for (const SubDNurbsPatch& p : fit)
          for (int a = 0; a < 2 && !hit; ++a)
            for (int b = 0; b < 2 && !hit; ++b) hit = p.surface.PointAt(a, b).DistanceTo(want) < 1e-9;
        if (hit) ++found;
      }
  d8test::Check(found == 8, "all 8 closed-form cube-corner limit points (2.5 / 7.5) are still patch corners to 1e-9 after the fit");
}

D8_EXTRA_TEST(SubDFitInteriorNeedsFewerLevelsForTheSameTolerance) {
  const SubD cube = SubD::FromControlMesh(FiBox(0, 0, 0, 10, 10, 10));
  const double tol = 5e-3;
  SubDFitReport coons, fit;
  const std::vector<SubDNurbsPatch> a = cube.ToNurbsPatchesToTolerance(tol, &coons, 6, 200000, false);
  const std::vector<SubDNurbsPatch> b = cube.ToNurbsPatchesToTolerance(tol, &fit, 6, 200000, true);
  d8test::Check(coons.converged && fit.converged, "both variants converge at tolerance 0.005 on the 10-unit cube");
  char what[200];
  std::snprintf(what, sizeof what, "the fitted variant reaches it at level %d (%d patches), the Coons variant needs level %d (%d patches)", fit.levels_used, fit.patches, coons.levels_used, coons.patches);
  d8test::Check(fit.levels_used < coons.levels_used && fit.patches < coons.patches, what);
  SubD cage = cube;
  cage.Subdivide(fit.levels_used);
  const double dense = FiDenseDeviation(b, cage);
  d8test::Check(dense <= 1.5 * tol && fit.max_deviation <= tol, "the fitted result's independent dense deviation is within 1.5x the tolerance and the reported figure within the tolerance");
  d8test::Check(FiSeamViolations(b) == 0 && !a.empty(), "...and it is watertight");
}

D8_EXTRA_TEST(SubDFitInteriorChangesNothingOnARegularCage) {
  // 6 x 6 torus cage: every vertex valence 4 -> every patch exact -> there is nothing to fit.
  const int N = 6;
  Mesh m;
  ON_Mesh& raw = m.raw();
  for (int i = 0; i < N; ++i)
    for (int j = 0; j < N; ++j) {
      const double a = 2.0 * 3.14159265358979323846 * i / N, b = 2.0 * 3.14159265358979323846 * j / N;
      raw.m_V.Append(ON_3fPoint(float((5.0 + 2.0 * std::cos(b)) * std::cos(a)), float((5.0 + 2.0 * std::cos(b)) * std::sin(a)), float(2.0 * std::sin(b))));
    }
  for (int i = 0; i < N; ++i)
    for (int j = 0; j < N; ++j) {
      ON_MeshFace f;
      f.vi[0] = i * N + j; f.vi[1] = ((i + 1) % N) * N + j; f.vi[2] = ((i + 1) % N) * N + (j + 1) % N; f.vi[3] = i * N + (j + 1) % N;
      raw.m_F.Append(f);
    }
  const SubD torus = SubD::FromControlMesh(m);
  SubDWatertightReport r0, r1;
  const std::vector<SubDNurbsPatch> a = torus.ToNurbsPatchesWatertight(&r0, false);
  const std::vector<SubDNurbsPatch> b = torus.ToNurbsPatchesWatertight(&r1, true);
  d8test::Check(r1.coons_patches == 0 && r1.fitted_patches == 0 && a.size() == b.size(), "a regular torus cage has only exact patches and fits none");
  double worst = 0.0;
  for (size_t k = 0; k < a.size(); ++k)
    for (int i = 0; i <= 4; ++i)
      for (int j = 0; j <= 4; ++j) worst = std::max(worst, a[k].surface.PointAt(i / 4.0, j / 4.0).DistanceTo(b[k].surface.PointAt(i / 4.0, j / 4.0)));
  d8test::Check(worst == 0.0, "...and the patches are identical to the default ones");
}
