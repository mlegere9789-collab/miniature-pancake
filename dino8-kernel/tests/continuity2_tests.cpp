// Edge continuity, second pass (see edge_continuity_tests in test_basic.cpp for the first):
//  - LoftMatched against a RATIONAL adjacent surface (a 270 degree cylinder wall): the cross-derivative magnitude search
//    (`optimize_scale`) versus MatchEdge's default scale, which moved an interior section by 0.044 and made the loft refuse;
//  - LoftMatched G1 / G2 against a bicubic (curved) adjacent surface;
//  - FitPatch / FitCurveNetwork derivative-row re-weighting (`max_weight_boost`) versus the single base weight.
// Every figure is MEASURED: gaps / angles / curvature differences come from NurbsSurface::MeasureEdgeContinuity or from the
// independent closed form of the bicubic graph used by edge_continuity_tests; nothing here is derived from how a surface was
// built. A standalone executable (not appended to the 99 000-line test_basic.cpp) so it compiles in seconds.
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/surface_continuity.h"
#include "opennurbs.h"

using dino8::kernel::MatchContinuity;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

namespace {

int g_fail = 0, g_pass = 0;
void Check(bool ok, const std::string& what) {
  if (ok) { ++g_pass; std::printf("ok: %s\n", what.c_str()); }
  else { ++g_fail; std::printf("FAIL: %s\n", what.c_str()); }
}

// ---- the closed-form bicubic graph shared with edge_continuity_tests ---------------------------------------------------
double F(double x, double y) { return 0.6 * x + 0.25 * x * x + 0.3 * x * y - 0.2 * y * y + 0.12 * x * x * x + 0.1 * y * y * y - 0.05 * x * x * y; }
double Fx(double x, double y) { return 0.6 + 0.5 * x + 0.3 * y + 0.36 * x * x - 0.1 * x * y; }
double Fy(double x, double y) { return 0.3 * x - 0.4 * y + 0.3 * y * y - 0.05 * x * x; }
double Fxx(double x, double y) { return 0.5 + 0.72 * x - 0.1 * y; }
double Fxy(double x, double) { return 0.3 - 0.1 * x; }
double Fyy(double, double y) { return -0.4 + 0.6 * y; }
double P1(double x, double y) { return 0.04 * x * (1.0 - x) * (1.0 - x) * (1.0 + y); }
double P2(double x, double) { return 0.05 * x * x * (1.0 - x); }
using Func2 = std::function<double(double, double)>;
const double kYLen = 2.0;

std::array<std::array<double, 4>, 4> BernsteinInverse() {
  double a[4][8];
  const double binom[4] = {1, 3, 3, 1};
  for (int i = 0; i < 4; ++i) {
    const double s = i / 3.0;
    for (int j = 0; j < 4; ++j) a[i][j] = binom[j] * std::pow(s, j) * std::pow(1.0 - s, 3 - j);
    for (int j = 0; j < 4; ++j) a[i][4 + j] = i == j ? 1.0 : 0.0;
  }
  for (int c = 0; c < 4; ++c) {
    int p = c;
    for (int r = c + 1; r < 4; ++r) if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
    for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[p][k]);
    const double d = a[c][c];
    for (int k = 0; k < 8; ++k) a[c][k] /= d;
    for (int r = 0; r < 4; ++r) {
      if (r == c) continue;
      const double f = a[r][c];
      for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
    }
  }
  std::array<std::array<double, 4>, 4> inv{};
  for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) inv[i][j] = a[i][4 + j];
  return inv;
}

NurbsSurface BezierGraph(const Func2& f, double x0, double x1, double y0, double y1) {
  const auto B = BernsteinInverse();
  double Z[4][4], T[4][4], C[4][4];
  for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) Z[i][j] = f(x0 + (x1 - x0) * i / 3.0, y0 + (y1 - y0) * j / 3.0);
  for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { T[i][j] = 0; for (int k = 0; k < 4; ++k) T[i][j] += B[i][k] * Z[k][j]; }
  for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { C[i][j] = 0; for (int k = 0; k < 4; ++k) C[i][j] += T[i][k] * B[j][k]; }
  std::vector<Point3d> grid;
  for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) grid.push_back(Point3d(x0 + (x1 - x0) * i / 3.0, y0 + (y1 - y0) * j / 3.0, C[i][j]));
  return NurbsSurface::FromControlGrid(grid, 4, 4, 3, 3);
}

NurbsCurve BezierCurve(const std::function<Point3d(double)>& f) {
  const auto B = BernsteinInverse();
  Point3d s[4];
  for (int i = 0; i < 4; ++i) s[i] = f(i / 3.0);
  std::vector<Point3d> ctrl;
  for (int i = 0; i < 4; ++i) {
    Point3d p(0, 0, 0);
    for (int k = 0; k < 4; ++k) p += ON_3dVector(s[k]) * B[i][k];
    ctrl.push_back(p);
  }
  return NurbsCurve::FromControlPoints(ctrl, 3);
}

NurbsSurface Adjacent() { return BezierGraph(F, -1.0, 0.0, 0.0, kYLen); }

// Independent comparison of surface `s` at (u, v) with the graph of F: vertical gap, normal angle, cross-edge normal curvature.
struct Truth { double gap = 0, angle = 0, curv_diff = 0; bool ok = false; };
Truth CompareWithF(const ON_NurbsSurface& s, double u, double v) {
  Truth t;
  ON_3dPoint P;
  ON_3dVector Su, Sv, Suu, Suv, Svv;
  if (!s.Ev2Der(u, v, P, Su, Sv, Suu, Suv, Svv)) return t;
  const double x = P.x, y = P.y;
  t.gap = std::fabs(P.z - F(x, y));
  ON_3dVector n = ON_CrossProduct(Su, Sv);
  n.Unitize();
  ON_3dVector nt(-Fx(x, y), -Fy(x, y), 1.0);
  nt.Unitize();
  if (n * nt < 0) n = -n;
  t.angle = std::atan2(ON_CrossProduct(n, nt).Length(), n * nt);
  ON_3dVector w = ON_CrossProduct(nt, ON_3dVector(0, 1, Fy(x, y)));
  w.Unitize();
  const double k_true = (w.x * w.x * Fxx(x, y) + 2 * w.x * w.y * Fxy(x, y) + w.y * w.y * Fyy(x, y)) / std::sqrt(1 + Fx(x, y) * Fx(x, y) + Fy(x, y) * Fy(x, y));
  ON_3dVector wp = w - (w * n) * n;
  wp.Unitize();
  const double E = Su * Su, Fm = Su * Sv, G = Sv * Sv, det = E * G - Fm * Fm;
  const double r1 = wp * Su, r2 = wp * Sv, a = (r1 * G - r2 * Fm) / det, b = (E * r2 - Fm * r1) / det;
  const double k_new = a * a * (Suu * n) + 2 * a * b * (Suv * n) + b * b * (Svv * n);
  t.curv_diff = std::fabs(k_new - k_true);
  t.ok = true;
  return t;
}
struct Worst { double gap = 0, angle = 0, curv = 0; int n = 0; };
Worst WorstOnPlaneSurface(const NurbsSurface& s, const NurbsSurface::PatchFitReport& r) {
  Worst w;
  for (int k = 0; k < 97; ++k) {
    const double y = kYLen * (k + 0.5) / 97.0;
    const ON_3dPoint truth(0.0, y, F(0.0, y));
    const ON_3dVector d = truth - r.plane_origin;
    const double u = d * ON_3dVector(r.plane_x), v = d * ON_3dVector(r.plane_y);
    const Truth t = CompareWithF(s.raw(), u, v);
    if (!t.ok) { w.n = -1000000; return w; }
    w.gap = std::max(w.gap, t.gap);
    w.angle = std::max(w.angle, t.angle);
    w.curv = std::max(w.curv, t.curv_diff);
    ++w.n;
  }
  return w;
}

std::vector<NurbsCurve> NetworkOf(const Func2& g) {
  std::vector<NurbsCurve> c;
  for (int i = 0; i < 4; ++i) { const double y = kYLen * i / 3.0; c.push_back(BezierCurve([=](double t) { return Point3d(t, y, g(t, y)); })); }
  for (int i = 0; i < 4; ++i) { const double x = i / 3.0; c.push_back(BezierCurve([=](double t) { return Point3d(x, kYLen * t, g(x, kYLen * t)); })); }
  return c;
}

void PatchData(const Func2& g, std::vector<Point3d>& boundary, std::vector<Point3d>& interior, std::vector<Point3d>& edge) {
  const int n = 32;
  for (int k = 0; k <= n; ++k) {
    const double t = static_cast<double>(k) / n;
    edge.push_back(Point3d(0.0, kYLen * t, g(0.0, kYLen * t)));
    boundary.push_back(Point3d(1.0, kYLen * t, g(1.0, kYLen * t)));
    if (k > 0 && k < n) {
      boundary.push_back(Point3d(t, 0.0, g(t, 0.0)));
      boundary.push_back(Point3d(t, kYLen, g(t, kYLen)));
    }
  }
  for (int i = 1; i < 7; ++i) for (int j = 1; j < 7; ++j) { const double x = i / 7.0, y = kYLen * j / 7.0; interior.push_back(Point3d(x, y, g(x, y))); }
}

// ---- 1. re-weighting: Patch and Network, base weight versus re-weighted ----------------------------------------------------
void TestPatchReweighting(const char* label, MatchContinuity level, const Func2& data, double tol, double angle_tol, double curv_tol) {
  const NurbsSurface adj = Adjacent();
  std::vector<Point3d> boundary, interior, edge;
  PatchData(data, boundary, interior, edge);
  NurbsSurface plain, base, boosted;
  NurbsSurface::PatchFitReport rp, rb, rr;
  Check(NurbsSurface::FitPatch(boundary, interior, tol, plain, &rp) == Result::Ok, std::string(label) + ": the plain fit runs");
  const Worst w0 = WorstOnPlaneSurface(plain, rp);
  NurbsSurface::FitEdgeContinuity e;
  e.points = edge;
  e.adjacent = &adj;
  e.continuity = level;
  e.angle_tolerance = angle_tol;
  e.curvature_tolerance = curv_tol;
  double boost1 = 1.0, boost2 = 1.0;
  const Result r1 = dino8::kernel::FitPatchReweighted(boundary, interior, tol, base, &rb, 16, {e}, 1.0, &boost1);
  const Result r2 = dino8::kernel::FitPatchReweighted(boundary, interior, tol, boosted, &rr, 16, {e}, 1e4, &boost2);
  std::printf("[%s] base-weight reason '%s'; re-weighted reason '%s'\n", label, rb.reason.c_str(), rr.reason.c_str());
  Check(r2 == Result::Ok, std::string(label) + ": the re-weighted fit succeeds");
  if (r2 != Result::Ok) return;
  const Worst wb = WorstOnPlaneSurface(r1 == Result::Ok ? base : plain, rb);
  const Worst wr = WorstOnPlaneSurface(boosted, rr);
  std::printf("[%s] 97 points on the edge, closed form. no option: angle %.4e rad curv %.4e | base weight (%s): angle %.4e curv %.4e | re-weighted x%.0f: angle %.4e rad curv %.4e gap %.2e, data deviation %.3e (tol %.3g), net %dx%d; kernel-measured angle %.3e\n",
              label, w0.angle, w0.curv, r1 == Result::Ok ? "ok" : "refused", wb.angle, wb.curv, boost2, wr.angle, wr.curv, wr.gap, rr.max_deviation, tol,
              rr.u_count, rr.v_count, rr.edge_continuity.empty() ? -1.0 : rr.edge_continuity[0].max_tangent_angle);
  Check(wr.n == 97 && boost2 > 1.0, std::string(label) + ": re-weighting raised the row weight (boost " + std::to_string(boost2) + ")");
  Check(rr.met_tolerance && rr.max_deviation <= tol, std::string(label) + ": the re-weighted fit still honours the data tolerance");
  Check(wr.gap <= tol, std::string(label) + ": the edge stays within the tolerance of the adjacent surface");
  if (r1 == Result::Ok) Check(wr.angle * 10.0 <= wb.angle, std::string(label) + ": measured angle is >= 10x below the base-weight fit (" + std::to_string(wb.angle) + " -> " + std::to_string(wr.angle) + ")");
  Check(wr.angle * 100.0 <= w0.angle, std::string(label) + ": measured angle is >= 100x below the fit without the option (" + std::to_string(w0.angle) + " -> " + std::to_string(wr.angle) + ")");
  if (level == MatchContinuity::Curvature) Check(wr.curv * 10.0 <= w0.curv, std::string(label) + ": cross-edge curvature mismatch >= 10x below the fit without the option");
}

void TestNetworkReweighting(const char* label, MatchContinuity level, const Func2& data, double tol, double angle_tol, double curv_tol) {
  const NurbsSurface adj = Adjacent();
  const std::vector<NurbsCurve> curves = NetworkOf(data);
  NurbsSurface plain, base, boosted;
  NurbsSurface::CurveNetworkReport rp, rb, rr;
  Check(NurbsSurface::FitCurveNetwork(curves, tol, plain, rp) == Result::Ok, std::string(label) + ": the plain network fit runs");
  const Worst w0 = WorstOnPlaneSurface(plain, rp.fit);
  NurbsSurface::NetworkEdgeContinuity e;
  e.curve_index = 4;
  e.adjacent = &adj;
  e.continuity = level;
  e.angle_tolerance = angle_tol;
  e.curvature_tolerance = curv_tol;
  double boost1 = 1.0, boost2 = 1.0;
  const Result r1 = dino8::kernel::FitCurveNetworkReweighted(curves, tol, base, rb, {e}, 1.0, &boost1);
  const Result r2 = dino8::kernel::FitCurveNetworkReweighted(curves, tol, boosted, rr, {e}, 1e4, &boost2);
  std::printf("[%s] base-weight reason '%s'; re-weighted reason '%s'\n", label, rb.reason.c_str(), rr.reason.c_str());
  Check(r2 == Result::Ok, std::string(label) + ": the re-weighted network fit succeeds");
  if (r2 != Result::Ok) return;
  const Worst wb = WorstOnPlaneSurface(r1 == Result::Ok ? base : plain, rb.fit);
  const Worst wr = WorstOnPlaneSurface(boosted, rr.fit);
  std::printf("[%s] 97 points on the edge, closed form. no option: angle %.4e rad curv %.4e | base weight (%s): angle %.4e curv %.4e | re-weighted x%.0f: angle %.4e rad curv %.4e gap %.2e, worst input-curve deviation %.3e (tol %.3g)\n",
              label, w0.angle, w0.curv, r1 == Result::Ok ? "ok" : "refused", wb.angle, wb.curv, boost2, wr.angle, wr.curv, wr.gap, rr.max_deviation, tol);
  Check(boost2 > 1.0, std::string(label) + ": re-weighting raised the row weight (boost " + std::to_string(boost2) + ")");
  Check(rr.met_tolerance && rr.max_deviation <= tol, std::string(label) + ": every input curve is still within the tolerance");
  Check(wr.gap <= tol, std::string(label) + ": the shared edge stays within the tolerance of the adjacent surface");
  if (r1 == Result::Ok) Check(wr.angle * 10.0 <= wb.angle, std::string(label) + ": measured angle is >= 10x below the base-weight fit (" + std::to_string(wb.angle) + " -> " + std::to_string(wr.angle) + ")");
  Check(wr.angle * 100.0 <= w0.angle, std::string(label) + ": measured angle is >= 100x below the fit without the option (" + std::to_string(w0.angle) + " -> " + std::to_string(wr.angle) + ")");
  if (level == MatchContinuity::Curvature) Check(wr.curv * 10.0 <= w0.curv, std::string(label) + ": cross-edge curvature mismatch >= 10x below the fit without the option");
}

// ---- 2. LoftMatched against a rational cylinder wall ----------------------------------------------------------------------
NurbsSurface CylinderWall(double sweep_deg, double radius) {
  ON_RevSurface* rev = ON_RevSurface::New();
  rev->m_curve = new ON_LineCurve(ON_Line(ON_3dPoint(radius, 0, 0), ON_3dPoint(radius, 0, 10)));
  rev->m_axis = ON_Line(ON_3dPoint(0, 0, 0), ON_3dPoint(0, 0, 1));
  rev->m_angle = ON_Interval(0.0, sweep_deg * ON_PI / 180.0);
  rev->m_t = rev->m_angle;
  NurbsSurface out;
  ON_NurbsSurface ns;
  if (rev->GetNurbForm(ns)) out.raw() = ns;
  delete rev;
  return out;
}
NurbsCurve VerticalLine(double deg, double radius) {
  const double t = deg * ON_PI / 180.0;
  return NurbsCurve::FromControlPoints({Point3d(radius * std::cos(t), radius * std::sin(t), 0), Point3d(radius * std::cos(t), radius * std::sin(t), 10)}, 1);
}

struct EdgeRef { int dir = -1; bool at_min = false; };
// The isoparametric edge of `s` whose middle lies at (x, y) in plan.
EdgeRef EdgeAt(const NurbsSurface& s, double x, double y) {
  const ON_NurbsSurface& n = s.raw();
  EdgeRef r;
  for (int d = 0; d < 2; ++d) for (int m = 0; m < 2; ++m) {
    const double f = m ? n.Domain(d).Min() : n.Domain(d).Max();
    const ON_3dPoint p = d == 0 ? n.PointAt(f, n.Domain(1).Mid()) : n.PointAt(n.Domain(0).Mid(), f);
    if (std::fabs(p.x - x) < 1e-6 && std::fabs(p.y - y) < 1e-6) { r.dir = d; r.at_min = (m != 0); }
  }
  return r;
}

struct LoftRun { Result r; NurbsSurface::LoftMatchReport rep; dino8::kernel::LoftMatchScaleReport sc; };
LoftRun RunLoft(const std::vector<NurbsCurve>& secs, const NurbsSurface& adj, EdgeRef er, MatchContinuity c, bool optimize, double tol = 1e-3) {
  NurbsSurface::LoftEndMatch m;
  m.surface = &adj;
  m.fixed_direction = er.dir;
  m.at_min = er.at_min;
  m.continuity = c;
  LoftRun out;
  NurbsSurface surf;
  out.r = dino8::kernel::LoftMatchedScaled(secs, std::min(3, static_cast<int>(secs.size()) - 1), &m, nullptr, tol, surf, out.rep, 1e-3, 1e-2, optimize, &out.sc);
  return out;
}

void TestLoftOnCylinderWall() {
  const NurbsSurface wall = CylinderWall(270.0, 5.0);
  Check(wall.raw().IsValid() && wall.raw().IsRational(), "cylinder wall: valid and RATIONAL");
  const EdgeRef er = EdgeAt(wall, 0.0, -5.0);
  Check(er.dir >= 0, "cylinder wall: the 270 degree edge was found");
  std::vector<NurbsCurve> secs3 = {VerticalLine(270, 5), VerticalLine(300, 5), VerticalLine(330, 5)};
  const LoftRun g0 = RunLoft(secs3, wall, er, MatchContinuity::Position, true);
  const LoftRun old_g1 = RunLoft(secs3, wall, er, MatchContinuity::Tangent, false);
  const LoftRun new_g1 = RunLoft(secs3, wall, er, MatchContinuity::Tangent, true);
  std::printf("[cylinder G1, 3 sections] G0 only: ok=%d angle %.3e curv %.3e | default scale: ok=%d '%s' (default-scale deviation %.4g) | searched scale: ok=%d scale %.4g deviation %.3e angle %.3e curv %.3e gap %.2e '%s'\n",
              g0.r == Result::Ok, g0.rep.start.max_tangent_angle, g0.rep.start.max_cross_curvature_diff, old_g1.r == Result::Ok, old_g1.rep.reason.c_str(),
              old_g1.sc.start_default_deviation, new_g1.r == Result::Ok, new_g1.sc.start_scale, new_g1.rep.section_deviation_max, new_g1.rep.start.max_tangent_angle,
              new_g1.rep.start.max_cross_curvature_diff, new_g1.rep.start.max_position_gap, new_g1.rep.reason.c_str());
  Check(g0.r == Result::Ok && g0.rep.start.max_tangent_angle > 1e-4, "cylinder G1: the loft without tangent matching is measurably off the wall's tangent plane");
  Check(old_g1.r == Result::Failed && old_g1.rep.reason.find("moves section") != std::string::npos, "cylinder G1: MatchEdge's default scale is refused (a section would move too far)");
  Check(new_g1.r == Result::Ok, "cylinder G1: the searched scale builds the loft");
  if (new_g1.r == Result::Ok) {
    Check(new_g1.rep.section_deviation_max <= 1e-3, "cylinder G1: every section is within the tolerance 1e-3 of the finished surface");
    Check(new_g1.rep.start.max_tangent_angle < 1e-6 && new_g1.rep.start.max_position_gap < 1e-6, "cylinder G1: measured angle < 1e-6 rad and gap < 1e-6");
    Check(new_g1.rep.start.max_tangent_angle * 100.0 < g0.rep.start.max_tangent_angle, "cylinder G1: measured angle >= 100x below the loft without matching");
    Check(new_g1.sc.start_default_deviation > 10.0 * new_g1.rep.section_deviation_max, "cylinder G1: the searched scale cuts the section deviation >= 10x");
  }
  std::vector<NurbsCurve> secs4 = {VerticalLine(270, 5), VerticalLine(290, 5), VerticalLine(310, 5), VerticalLine(330, 5)};
  const LoftRun old_g2 = RunLoft(secs4, wall, er, MatchContinuity::Curvature, false);
  const LoftRun new_g2 = RunLoft(secs4, wall, er, MatchContinuity::Curvature, true);
  const LoftRun g1_4 = RunLoft(secs4, wall, er, MatchContinuity::Position, true);
  std::printf("[cylinder G2, 4 sections] G0 only: angle %.3e curv %.3e | default scale: ok=%d '%s' | searched: ok=%d scale %.4g dev %.3e angle %.3e curv %.3e gauss %.3e '%s'\n",
              g1_4.rep.start.max_tangent_angle, g1_4.rep.start.max_cross_curvature_diff, old_g2.r == Result::Ok, old_g2.rep.reason.c_str(), new_g2.r == Result::Ok,
              new_g2.sc.start_scale, new_g2.rep.section_deviation_max, new_g2.rep.start.max_tangent_angle, new_g2.rep.start.max_cross_curvature_diff,
              new_g2.rep.start.max_gaussian_diff, new_g2.rep.reason.c_str());
}

// ---- 3. LoftMatched against the bicubic graph -------------------------------------------------------------------------------
void TestLoftOnBicubic() {
  const NurbsSurface adj = Adjacent();
  const EdgeRef er = EdgeAt(adj, 0.0, kYLen / 2.0);
  // sections along y at x = 0, 0.3, 0.6, 1.0 following F + P1 + P2 (the P terms vanish at x = 0 but change slope / curvature there).
  auto sections = [](const Func2& g) {
    std::vector<NurbsCurve> s;
    for (double x : {0.0, 0.35, 0.7, 1.0}) s.push_back(BezierCurve([=](double t) { return Point3d(x, kYLen * t, g(x, kYLen * t)); }));
    return s;
  };
  const auto data = sections([](double x, double y) { return F(x, y) + P1(x, y) + P2(x, y); });
  const LoftRun g0 = RunLoft(data, adj, er, MatchContinuity::Position, true, 0.05);
  const LoftRun g1 = RunLoft(data, adj, er, MatchContinuity::Tangent, true, 0.05);
  const LoftRun g2 = RunLoft(data, adj, er, MatchContinuity::Curvature, true, 0.05);
  std::printf("[bicubic loft] G0 only: angle %.3e curv %.3e | G1: ok=%d dev %.3e (default scale %.3e) angle %.3e curv %.3e '%s' | G2: ok=%d dev %.3e (default %.3e) angle %.3e curv %.3e '%s'\n",
              g0.rep.start.max_tangent_angle, g0.rep.start.max_cross_curvature_diff, g1.r == Result::Ok, g1.rep.section_deviation_max, g1.sc.start_default_deviation,
              g1.rep.start.max_tangent_angle, g1.rep.start.max_cross_curvature_diff, g1.rep.reason.c_str(), g2.r == Result::Ok, g2.rep.section_deviation_max,
              g2.sc.start_default_deviation, g2.rep.start.max_tangent_angle, g2.rep.start.max_cross_curvature_diff, g2.rep.reason.c_str());
}

}  // namespace

int main() {
  ON::Begin();
  TestPatchReweighting("patch G1", MatchContinuity::Tangent, [](double x, double y) { return F(x, y) + P1(x, y); }, 0.03, 2e-3, 1e-2);
  TestPatchReweighting("patch G2", MatchContinuity::Curvature, [](double x, double y) { return F(x, y) + P1(x, y) + P2(x, y); }, 0.05, 2e-3, 2e-2);
  TestNetworkReweighting("network G1", MatchContinuity::Tangent, [](double x, double y) { return F(x, y) + P1(x, y); }, 0.03, 2e-3, 1e-2);
  TestNetworkReweighting("network G2", MatchContinuity::Curvature, [](double x, double y) { return F(x, y) + P1(x, y) + P2(x, y); }, 0.05, 2e-3, 2e-2);
  TestLoftOnCylinderWall();
  TestLoftOnBicubic();
  std::printf("%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
