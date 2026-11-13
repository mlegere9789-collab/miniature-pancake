// SubD::FromNurbsSurfaceToTolerance(): a SubD cage from a NURBS surface whose Catmull-Clark LIMIT surface is within a stated tolerance of
// the surface, with the deviation measured (FromNurbsSurface() alone puts the cage vertices on the surface but the limit surface is
// pulled inside the curvature, and nothing measured by how much).
//
// Oracles (none of them is the function's own report):
//   * a TORUS patch (major R = 3, minor r = 1, a 68 degree arc of each circle, inside one rational span, built exactly as a rational NURBS by ON_Torus::GetNurbForm and
//     trimmed): the distance of any point to the torus is the closed form | sqrt((hypot(x, y) - R)^2 + z^2) - r |, evaluated here on the
//     limit points that EvaluateFace() gives at parameters the function did not use itself;
//   * convergence: doubling the grid must cut the deviation by about 4 or more (measured: O(h^2), the boundary rows - a crease limit
//     with no ghost row beyond the sheet edge - set the rate, not the interior), and the fitted cage must beat the plain sampled cage;
//   * a flat sheet (z = 0): every limit point must lie in the plane and inside the sampled rectangle, corners pinned exactly.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "dino8/kernel/subd.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::SubD;
using dino8::kernel::SubDFromNurbsReport;
using dino8::kernel::SubDSurfacePoint;

constexpr double kMajor = 3.0;
constexpr double kMinor = 1.0;

NurbsSurface TorusPatch() {
  ON_Torus torus(ON_Plane::World_xy, kMajor, kMinor);
  NurbsSurface s;
  if (!torus.GetNurbForm(s.raw())) return NurbsSurface();
  const ON_Interval du = s.raw().Domain(0);
  const ON_Interval dv = s.raw().Domain(1);
  s.raw().Trim(0, ON_Interval(du.Min() + 0.28 * du.Length(), du.Min() + 0.47 * du.Length()));
  s.raw().Trim(1, ON_Interval(dv.Min() + 0.28 * dv.Length(), dv.Min() + 0.47 * dv.Length()));
  return s;
}

double TorusDistance(const Point3d& p) {
  const double ring = std::sqrt(p.x * p.x + p.y * p.y) - kMajor;
  return std::fabs(std::sqrt(ring * ring + p.z * p.z) - kMinor);
}

NurbsSurface FlatSheet(int nu, int nv, double sx, double sy) {
  std::vector<Point3d> g;
  for (int i = 0; i < nu; ++i)
    for (int j = 0; j < nv; ++j) g.push_back(Point3d(sx * i / (nu - 1), sy * j / (nv - 1), 0.0));
  return NurbsSurface::FromControlGrid(g, nu, nv, 1, 1);
}

// Worst closed-form distance of the cage's limit surface to the torus, at parameters the function itself does not measure.
double IndependentTorusDeviation(const SubD& cage, int* samples) {
  double worst = 0.0;
  int n = 0;
  ON_SubDFaceIterator it = cage.raw().FaceIterator();
  for (const ON_SubDFace* f = it.FirstFace(); f != nullptr; f = it.NextFace()) {
    for (double u : {0.1, 0.4, 0.9}) {
      for (double v : {0.15, 0.6, 0.85}) {
        const SubDSurfacePoint p = cage.EvaluateFace(f->m_id, u, v);
        worst = std::max(worst, TorusDistance(Point3d(p.position.x, p.position.y, p.position.z)));
        ++n;
      }
    }
  }
  if (samples != nullptr) *samples = n;
  return worst;
}

// The same measure for the plain sampled cage FromNurbsSurface() gives (the limit surface of a cage with vertices ON the surface).
double PlainCageDeviation(const NurbsSurface& s, int n) {
  const SubD cage = SubD::FromNurbsSurface(s, n, n);
  return IndependentTorusDeviation(cage, nullptr);
}

}  // namespace

D8_EXTRA_TEST(SubDFromNurbsToleranceTorusPatchIsWithinToleranceByTheClosedFormDistance) {
  const NurbsSurface torus = TorusPatch();
  d8test::Check(torus.raw().IsValid(), "torus patch (rational NURBS, a 68 degree arc of each circle, inside one rational span) built");
  SubDFromNurbsReport rep;
  const double tol = 0.002;
  const SubD cage = SubD::FromNurbsSurfaceToTolerance(torus, tol, &rep);
  char what[300];
  std::snprintf(what, sizeof what, "converged at %dx%d (%d faces), reported max deviation %.6f <= %.4f; reason '%s'", rep.divisions, rep.divisions,
                rep.faces, rep.max_deviation, tol, rep.reason.c_str());
  d8test::Check(rep.converged && rep.max_deviation <= tol && rep.max_deviation > 0.0, what);
  d8test::Check(cage.FaceCount() == rep.divisions * rep.divisions && rep.faces == cage.FaceCount() && rep.fitted,
                "the returned cage is the reported grid, fitted");
  int samples = 0;
  const double independent = IndependentTorusDeviation(cage, &samples);
  std::snprintf(what, sizeof what, "closed-form torus distance of %d limit points at fresh parameters: worst %.6f (tolerance %.4f)", samples, independent, tol);
  d8test::Check(independent <= 1.3 * tol && samples >= 9 * 16, what);
  d8test::Check(independent <= rep.max_deviation * 3.0 + 1e-6 && rep.max_deviation <= independent * 3.0 + 1e-6,
                "the report's own measure and the independent closed-form measure agree within a factor 3");
}

D8_EXTRA_TEST(SubDFromNurbsToleranceFittedCageBeatsThePlainSampledCageAndConvergesFast) {
  const NurbsSurface torus = TorusPatch();
  // Plain FromNurbsSurface: the limit surface is pulled inside the curvature; measured by the closed form.
  const double plain4 = PlainCageDeviation(torus, 4);
  const double plain8 = PlainCageDeviation(torus, 8);
  SubDFromNurbsReport rep;
  (void)SubD::FromNurbsSurfaceToTolerance(torus, 1e-9, &rep, 16);  // unreachable: only the per-level measurements are wanted
  d8test::Check(!rep.converged && rep.deviation_per_level.size() == 3 && rep.divisions_tried.size() == 3 && rep.divisions_tried[0] == 4 &&
                    rep.divisions_tried[1] == 8 && rep.divisions_tried[2] == 16,
                "tolerance 1e-9 is not reachable: grids 4, 8, 16 were tried and measured");
  if (rep.deviation_per_level.size() != 3) return;
  const double f4 = rep.deviation_per_level[0], f8 = rep.deviation_per_level[1], f16 = rep.deviation_per_level[2];
  char what[300];
  std::snprintf(what, sizeof what, "fitted deviation per grid 4/8/16: %.3e %.3e %.3e; plain sampled cage at 4/8: %.3e %.3e", f4, f8, f16, plain4, plain8);
  d8test::Check(f4 < plain4 / 2.5 && f8 < plain8 / 2.5, what);
  d8test::Check(plain4 / plain8 < 6.0, "the plain cage only converges O(h^2) (halving h cuts the deviation by about 4, here < 6)");
  d8test::Check(f4 / f8 > 3.3 && f8 / f16 > 3.3,
                "the fitted deviation at least quarters (to rounding) when the grid doubles: it converges, at the O(h^2) rate set by the boundary rows");
}

D8_EXTRA_TEST(SubDFromNurbsToleranceReportsNonConvergenceWithAnEmptyCageAndRejectsBadArguments) {
  const NurbsSurface torus = TorusPatch();
  SubDFromNurbsReport rep;
  const SubD none = SubD::FromNurbsSurfaceToTolerance(torus, 1e-7, &rep, 4);
  d8test::Check(!rep.converged && none.FaceCount() == 0 && !rep.reason.empty() && rep.max_deviation > 1e-7 && rep.divisions_tried.size() == 1,
                "an unreachable tolerance within the grid budget: not converged, empty SubD, a reason, the deviation reached");
  bool t_zero = false, t_nan = false, t_inf = false, t_neg = false, t_div = false, t_surf = false;
  try { (void)SubD::FromNurbsSurfaceToTolerance(torus, 0.0); } catch (const std::invalid_argument&) { t_zero = true; }
  try { (void)SubD::FromNurbsSurfaceToTolerance(torus, std::nan("")); } catch (const std::invalid_argument&) { t_nan = true; }
  try { (void)SubD::FromNurbsSurfaceToTolerance(torus, INFINITY); } catch (const std::invalid_argument&) { t_inf = true; }
  try { (void)SubD::FromNurbsSurfaceToTolerance(torus, -1.0); } catch (const std::invalid_argument&) { t_neg = true; }
  try { (void)SubD::FromNurbsSurfaceToTolerance(torus, 0.01, nullptr, 3); } catch (const std::invalid_argument&) { t_div = true; }
  try { (void)SubD::FromNurbsSurfaceToTolerance(NurbsSurface(), 0.01); } catch (const std::invalid_argument&) { t_surf = true; }
  d8test::Check(t_zero && t_nan && t_inf && t_neg && t_div && t_surf, "tolerance 0 / NaN / inf / negative, max_divisions < 4 and an invalid surface all throw invalid_argument");
}

D8_EXTRA_TEST(SubDFromNurbsToleranceFlatSheetStaysInThePlaneAndInsideTheRectangleWithPinnedCorners) {
  const NurbsSurface sheet = FlatSheet(2, 2, 6.0, 4.0);
  SubDFromNurbsReport rep;
  const SubD cage = SubD::FromNurbsSurfaceToTolerance(sheet, 1e-4, &rep);
  d8test::Check(rep.converged && rep.divisions == 4 && rep.max_deviation <= 1e-4, "a flat sheet converges on the first grid (4x4)");
  double zmax = 0.0, xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9;
  for (const dino8::kernel::SubDLimitPoint& lp : cage.LimitPoints()) {
    zmax = std::max(zmax, std::fabs(lp.limit_point.z));
    xmin = std::min(xmin, lp.limit_point.x); xmax = std::max(xmax, lp.limit_point.x);
    ymin = std::min(ymin, lp.limit_point.y); ymax = std::max(ymax, lp.limit_point.y);
  }
  char what[300];
  std::snprintf(what, sizeof what, "limit points: |z| <= %.2e, x in [%.5f, %.5f], y in [%.5f, %.5f] (sheet [0,6]x[0,4])", zmax, xmin, xmax, ymin, ymax);
  d8test::Check(zmax < 1e-5 && std::fabs(xmin) < 1e-5 && std::fabs(xmax - 6.0) < 1e-5 && std::fabs(ymin) < 1e-5 && std::fabs(ymax - 4.0) < 1e-5, what);
  // The 4 sheet corners are pinned: their limit points ARE the corners (no overshoot, no rounding).
  int corners = 0;
  for (const dino8::kernel::SubDLimitPoint& lp : cage.LimitPoints()) {
    const bool cx = std::fabs(lp.limit_point.x) < 1e-5 || std::fabs(lp.limit_point.x - 6.0) < 1e-5;
    const bool cy = std::fabs(lp.limit_point.y) < 1e-5 || std::fabs(lp.limit_point.y - 4.0) < 1e-5;
    if (cx && cy) ++corners;
  }
  d8test::Check(corners == 4, "exactly the 4 sheet corners lie at the sheet's corner points");
}
