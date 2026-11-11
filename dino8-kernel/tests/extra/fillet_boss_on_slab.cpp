// FilletBossOnSlab: a freeform boss on a slab, filleted along its closed rim into a closed solid. Independent volume oracles (an area-weighted
// column integral and a flux-form surface integral), never the routine's own report.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "dino8/kernel/brep.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/surface_intersect.h"
#include "opennurbs.h"
#include "test_registry.h"
namespace {
void Check(bool condition, const char* what) { d8test::Check(condition, what); }
}  // namespace
namespace fillet_boss_extra_tests {

using dino8::kernel::FilletSurfaces;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Result;
using dino8::kernel::SurfaceFillet;
using dino8::kernel::SurfaceFilletOptions;

// A bicubic (degree 3 x 3) NURBS surface from an nu x nv control net sampled from f over [x0,x1] x [y0,y1]; `w` (optional)
// makes it genuinely rational.
NurbsSurface MakeNet(int nu, int nv, const std::function<ON_3dPoint(double, double)>& f, const std::function<double(int, int)>& w = nullptr) {
  std::vector<ON_3dPoint> g;
  for (int i = 0; i < nu; ++i)
    for (int j = 0; j < nv; ++j) g.push_back(f((double)i / (nu - 1), (double)j / (nv - 1)));
  NurbsSurface s = NurbsSurface::FromControlGrid(g, nu, nv, 3, 3);
  if (w)
    for (int i = 0; i < nu; ++i)
      for (int j = 0; j < nv; ++j) s.SetWeightAt(i, j, w(i, j));
  return s;
}

// Floor: x in [0,6], y in [-3,3], heights = two Gaussian bumps.
NurbsSurface BumpFloor() {
  return MakeNet(10, 10, [](double a, double b) {
    const double x = 6.0 * a, y = -3.0 + 6.0 * b;
    const double z = 0.35 * std::exp(-((x - 2) * (x - 2) + (y - 1) * (y - 1)) / 1.2) + 0.25 * std::exp(-((x - 4.5) * (x - 4.5) + (y + 1.5) * (y + 1.5)) / 1.5);
    return ON_3dPoint(x, y, z);
  });
}

// Wall crossing the floor near x = 3.2: x = 3.2 + 0.4 sin(0.8 y) + 0.1 z + 0.05 z^2, y in [-3.2,3.2], z in [-1,2.5].
NurbsSurface CurvedWall(double lean = 0.1) {
  return MakeNet(10, 8, [lean](double a, double b) {
    const double y = -3.2 + 6.4 * a, z = -1.0 + 3.5 * b;
    return ON_3dPoint(3.2 + 0.4 * std::sin(0.8 * y) + lean * z + 0.05 * z * z, y, z);
  });
}

// Wavy rational floor.
NurbsSurface WavyRationalFloor() {
  return MakeNet(
      12, 12,
      [](double a, double b) {
        const double x = 6.0 * a, y = -3.0 + 6.0 * b;
        return ON_3dPoint(x, y, 0.16 * std::sin(1.1 * x) * std::cos(0.8 * y));
      },
      [](int i, int j) { return 1.0 + 0.15 * std::sin(0.5 * i + 0.3 * j); });
}

// Rational wall bulging towards the floor's far side.
NurbsSurface RationalBulgeWall() {
  return MakeNet(
      10, 9,
      [](double a, double b) {
        const double y = -3.2 + 6.4 * a, z = -1.0 + 3.5 * b;
        return ON_3dPoint(3.0 + 0.5 * std::cos(0.5 * y) * std::cos(0.5 * y) + 0.18 * z + 0.12 * std::sin(1.3 * z) * std::cos(0.6 * y), y, z);
      },
      [](int i, int j) { return 1.0 + 0.12 * std::cos(0.5 * i - 0.4 * j); });
}

// Domed top (the body lies below it) for the convex-edge case.
NurbsSurface DomedTop() {
  return MakeNet(11, 10, [](double a, double b) {
    const double x = 6.0 * a, y = -3.0 + 6.0 * b;
    return ON_3dPoint(x, y, 1.5 + 0.22 * std::sin(0.7 * x) * std::cos(0.6 * y) - 0.02 * x * x);
  });
}

// +1 / -1: the side of `s` whose normal at its parameter centre has a positive component along `want`.
int SideToward(const NurbsSurface& s, const ON_3dVector& want) {
  const ON_Interval du = s.raw().Domain(0), dv = s.raw().Domain(1);
  return (s.NormalAt(du.ParameterAt(0.5), dv.ParameterAt(0.5)) * want) >= 0 ? 1 : -1;
}

double DistTo(const NurbsSurface& s, const ON_3dPoint& p, ON_2dPoint* uv = nullptr) {
  const ON_2dPoint q = s.ClosestPointParameter(p, 40, 40);
  if (uv) *uv = q;
  return s.PointAt(q.x, q.y).DistanceTo(p);
}

struct Meas {
  double gap_a = 0, gap_b = 0;          // blend edge rows vs the parents (distance to the parent)
  double ball_err = 0;                  // | |blend - c| - r | over the whole arc, c from the parents' own normals at the contact feet
  double centre_gap = 0;                // the two centres implied by a's and b's normals at the feet must coincide
  double touch_a = 0, touch_b = 0;      // |dist(centre, parent) - r| (the ball touches, never penetrates)
  double g1_a = 0, g1_b = 0;            // angle between the blend's normal and the parent's normal at the contact (radians)
  double min_cos = 1e300;               // min |blend normal . rolling-ball direction| (fold-over would be <= 0)
  bool one_sign = true;
  int samples = 0;
};

Meas Measure(const SurfaceFillet& f, const NurbsSurface& a, const NurbsSurface& b, int sa, int sb, double r, bool midpoints) {
  Meas m;
  const int N = f.intervals;
  int sign = 0;
  for (int i = 0; i < (midpoints ? N : N + 1); ++i) {
    const double s = midpoints ? 0.5 * (f.station_s[(size_t)i] + f.station_s[(size_t)i + 1]) : f.station_s[(size_t)i];
    const ON_3dPoint P0 = f.blend.PointAt(s, 0.0), P1 = f.blend.PointAt(s, 1.0);
    ON_2dPoint uva, uvb;
    m.gap_a = std::max(m.gap_a, DistTo(a, P0, &uva));
    m.gap_b = std::max(m.gap_b, DistTo(b, P1, &uvb));
    const ON_3dVector Na = a.NormalAt(uva.x, uva.y), Nb = b.NormalAt(uvb.x, uvb.y);
    const ON_3dPoint Qa = a.PointAt(uva.x, uva.y), Qb = b.PointAt(uvb.x, uvb.y);
    const ON_3dPoint ca = Qa + (sa * r) * Na, cb = Qb + (sb * r) * Nb;
    m.centre_gap = std::max(m.centre_gap, ca.DistanceTo(cb));
    const ON_3dPoint c = 0.5 * (ca + cb);
    m.touch_a = std::max(m.touch_a, std::fabs(DistTo(a, c) - r));
    m.touch_b = std::max(m.touch_b, std::fabs(DistTo(b, c) - r));
    for (int j = 0; j <= 8; ++j) {
      const double v = j / 8.0;
      ON_3dPoint P;
      ON_3dVector n;
      f.blend.raw().EvNormal(s, v, P, n);
      m.ball_err = std::max(m.ball_err, std::fabs(P.DistanceTo(c) - r));
      ON_3dVector toc = c - P;
      toc.Unitize();
      const double dt = n * toc;
      const int sg = dt >= 0 ? 1 : -1;
      if (sign == 0) sign = sg;
      if (sg != sign) m.one_sign = false;
      m.min_cos = std::min(m.min_cos, std::fabs(dt));
      ++m.samples;
      if (j == 0) m.g1_a = std::max(m.g1_a, std::acos(std::min(1.0, std::fabs(n * Na))));
      if (j == 8) m.g1_b = std::max(m.g1_b, std::acos(std::min(1.0, std::fabs(n * Nb))));
    }
  }
  return m;
}

void Report(const char* tag, const SurfaceFillet& f, const Meas& st, const Meas& mid) {
  std::printf("[%s] N=%d s=[%.4f,%.4f] trims=(%.4f,%.4f) reported dev=%.3e (stations %.3e) minalign=%.4f max r*k=%.4f\n", tag, f.intervals, f.s_start, f.s_end, f.start_trim, f.end_trim,
              f.deviation, f.station_deviation, f.min_normal_alignment, f.max_radius_times_curvature);
  std::printf("[%s]   stations : gapA %.2e gapB %.2e ball %.2e centre %.2e touchA %.2e touchB %.2e g1A %.2e g1B %.2e mincos %.5f\n", tag, st.gap_a, st.gap_b, st.ball_err, st.centre_gap, st.touch_a, st.touch_b, st.g1_a, st.g1_b, st.min_cos);
  std::printf("[%s]   midpoints: gapA %.2e gapB %.2e ball %.2e centre %.2e touchA %.2e touchB %.2e g1A %.2e g1B %.2e mincos %.5f\n", tag, mid.gap_a, mid.gap_b, mid.ball_err, mid.centre_gap, mid.touch_a, mid.touch_b, mid.g1_a, mid.g1_b, mid.min_cos);
}

// Runs the routine and checks every measured property against `tol` (geometry) and `g1_tol` (radians).
SurfaceFillet RunCase(const char* tag, const NurbsSurface& a, const NurbsSurface& b, const ON_3dVector& want_a, const ON_3dVector& want_b, double r, double tol, double g1_tol) {
  SurfaceFilletOptions o;
  o.side_a = SideToward(a, want_a);
  o.side_b = SideToward(b, want_b);
  const SurfaceFillet f = FilletSurfaces(a, b, r, tol, o);
  const std::string t(tag);
  Check(f.status == Result::Ok, (t + ": the fillet is built (reason: " + f.reason + ")").c_str());
  if (f.status != Result::Ok) return f;
  const Meas st = Measure(f, a, b, o.side_a, o.side_b, r, false), mid = Measure(f, a, b, o.side_a, o.side_b, r, true);
  Report(tag, f, st, mid);
  Check(f.deviation <= tol, (t + ": reported sampled deviation is within the tolerance").c_str());
  Check(f.station_deviation <= f.deviation, (t + ": station deviation <= overall deviation").c_str());
  Check(f.intervals >= 8 && f.uv_a.size() == (size_t)f.intervals + 1 && f.uv_b.size() == f.uv_a.size(), (t + ": stations are reported").c_str());
  Check(f.blend.raw().IsValid() && f.blend.IsRational() && f.blend.DegreeU() == 3 && f.blend.DegreeV() == 2, (t + ": the blend is a valid rational cubic x quadratic NURBS").c_str());
  Check(f.s_end > f.s_start && f.start_trim >= 0 && f.end_trim >= 0, (t + ": the covered range is reported").c_str());
  for (const Meas* m : {&st, &mid}) {
    const std::string w = t + (m == &st ? " (stations)" : " (midpoints)");
    // The measurements use their own closest-point searches, so allow them a small slack above the stated tolerance.
    const double lim = 2.0 * tol;
    Check(m->gap_a <= lim, (w + ": blend edge row lies on parent A").c_str());
    Check(m->gap_b <= lim, (w + ": blend edge row lies on parent B").c_str());
    Check(m->ball_err <= lim, (w + ": every blend point is at distance r from the ball centre").c_str());
    // (the centre is rebuilt from closest points of off-surface midpoint samples, which amplifies the along-surface error: 5x)
    Check(m->centre_gap <= 5.0 * tol, (w + ": the centres implied by A's and B's normals coincide").c_str());
    Check(m->touch_a <= lim && m->touch_b <= lim, (w + ": the ball touches both parents at distance r (no penetration, no gap)").c_str());
    Check(m->g1_a <= g1_tol, (w + ": G1 with parent A at the contact curve").c_str());
    Check(m->g1_b <= g1_tol, (w + ": G1 with parent B at the contact curve").c_str());
    Check(m->one_sign && m->min_cos > 0.999, (w + ": no fold-over (normal = rolling-ball direction, one orientation)").c_str());
  }
  return f;
}


// ---------------------------------------------------------------------------------------------------------------------
// Closed rims, refusal branches, and the closed boss-on-slab solid (see fillet.h).
// ---------------------------------------------------------------------------------------------------------------------

// A cylinder wall (axis z, centre (0,0)) as an exact rational NURBS: u = angle, v = height (z0..z1).
NurbsSurface CylinderWallNurbs(double R, double z0, double z1) {
  ON_Cylinder cyl(ON_Circle(ON_Plane(ON_3dPoint(0, 0, z0), ON_3dVector::ZAxis), R), z1 - z0);
  ON_NurbsSurface ns;
  cyl.GetNurbForm(ns);
  NurbsSurface n;
  n.raw() = ns;
  return n;
}

// Bilinear plane z = const over [x0,x1] x [y0,y1] with u = x, v = y.
NurbsSurface FloorPlane(double x0, double x1, double y0, double y1, double z) {
  const std::vector<dino8::kernel::Point3d> g = {dino8::kernel::Point3d(x0, y0, z), dino8::kernel::Point3d(x0, y1, z), dino8::kernel::Point3d(x1, y0, z), dino8::kernel::Point3d(x1, y1, z)};
  NurbsSurface s = NurbsSurface::FromControlGrid(g, 2, 2, 1, 1);
  s.raw().SetDomain(0, x0, x1);
  s.raw().SetDomain(1, y0, y1);
  return s;
}

// A freeform boss: an island whose height is 3 / (1 + (rho / 1.5)^10) - 0.3 over [-3,3]^2, rho an elliptical, skewed radius
// (rim at z = 0 is about 1.9 out, flank slope about 55 degrees at the floor), as a bicubic net.
NurbsSurface FreeformBoss(int n = 22, double skew = 0.12) {
  return MakeNet(n, n, [skew](double a, double b) {
    const double x = -3.0 + 6.0 * a, y = -3.0 + 6.0 * b;
    const double rho2 = (x * x) / 1.0 + (y * y) / 0.8 + skew * x * y;
    const double rho = std::sqrt(std::max(rho2, 0.0));
    return ON_3dPoint(x, y, 3.0 / (1.0 + std::pow(rho / 1.5, 10.0)) - 0.3);
  });
}

void PrintCurves(const char* tag, const NurbsSurface& a, const NurbsSurface& b) {
  dino8::kernel::IntersectOptions io;
  io.tolerance = 1e-9;
  const auto cs = dino8::kernel::IntersectSurfaces(a.raw(), b.raw(), io);
  std::printf("[%s] IntersectSurfaces -> %zu curves\n", tag, cs.size());
  for (const auto& c : cs)
    std::printf("[%s]    closed=%d pts=%zu len=%.4f start=(%.3f,%.3f,%.3f) end=(%.3f,%.3f,%.3f) uv_a start=(%.3f,%.3f) end=(%.3f,%.3f)\n", tag, (int)c.closed, c.points.size(), c.Length(),
                c.points.front().x, c.points.front().y, c.points.front().z, c.points.back().x, c.points.back().y, c.points.back().z, c.uv_a.front().x, c.uv_a.front().y, c.uv_a.back().x, c.uv_a.back().y);
}

double IndependentSolidVolume(const dino8::kernel::BossOnSlabFillet& r, const NurbsSurface& boss, double floor_z, double x0, double x1, double y0, double y1, double depth) {
  // V = slab + INT INT (height above the floor) dx dy, the height piecewise over the xy tiling floor | blend annulus | boss:
  //   - blend: INT INT (z - floor_z) * J du dv over the whole blend patch, J = x_u y_v - x_v y_u (signed, no overhang);
  //     since the annulus between the contact curves' projections is exactly the image of the patch.
  //   - boss: INT INT over the polygon bounded by the boss contact curve (in the boss chart) of (z - floor_z) * J_boss.
  // Both by Gauss-Legendre (12 points per panel) on a panel grid; the boss region by scanlines against the rim polygon.
  (void)x0; (void)x1; (void)y0; (void)y1;
  const double A = (x1 - x0) * (y1 - y0);
  double v = A * depth;
  static const double gx[6] = {-0.9324695142031521, -0.6612093864662645, -0.2386191860831969, 0.2386191860831969, 0.6612093864662645, 0.9324695142031521};
  static const double gw[6] = {0.1713244923791704, 0.3607615730481386, 0.4679139345726910, 0.4679139345726910, 0.3607615730481386, 0.1713244923791704};
  const NurbsSurface& bl = r.fillet.blend;
  const ON_Interval bu = bl.raw().Domain(0);
  {
    const int PU = std::getenv("SFX_PU") ? std::atoi(std::getenv("SFX_PU")) : 160, PV = 8;
    double sum = 0;
    for (int i = 0; i < PU; ++i)
      for (int a = 0; a < 6; ++a) {
        const double u = bu.Min() + (bu.Length()) * (i + 0.5 + 0.5 * gx[a]) / PU;
        for (int j = 0; j < PV; ++j)
          for (int b = 0; b < 6; ++b) {
            const double vv = (j + 0.5 + 0.5 * gx[b]) / PV;
            ON_3dPoint P;
            ON_3dVector Su, Sv;
            bl.raw().Ev1Der(u, vv, P, Su, Sv);
            const double J = Su.x * Sv.y - Su.y * Sv.x;
            sum += gw[a] * gw[b] * 0.25 * (P.z - floor_z) * std::fabs(J) * (bu.Length() / PU) * (1.0 / PV);
          }
      }
    v += sum;
  }
  {
    const ON_Interval du = boss.raw().Domain(0), dv = boss.raw().Domain(1);
    std::vector<ON_2dPoint> poly;
    const ON_Interval cd = r.fillet.trim_b.raw().Domain();
    for (int i = 0; i < 20000; ++i) {
      const ON_3dPoint p = r.fillet.trim_b.raw().PointAt(cd.ParameterAt(i / 20000.0));
      poly.emplace_back(p.x, p.y);
    }
    const int SL = std::getenv("SFX_SL") ? std::atoi(std::getenv("SFX_SL")) : 600;
    double sum = 0;
    for (int i = 0; i < SL; ++i)
      for (int a = 0; a < 6; ++a) {
        const double u = du.Min() + du.Length() * (i + 0.5 + 0.5 * gx[a]) / SL;
        std::vector<double> xs;
        for (size_t k = 0; k < poly.size(); ++k) {
          const ON_2dPoint &p = poly[k], &q = poly[(k + 1) % poly.size()];
          if ((p.x <= u) != (q.x <= u)) xs.push_back(p.y + (u - p.x) * (q.y - p.y) / (q.x - p.x));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
          const double lo = xs[k], hi = xs[k + 1];
          const int PV = 40;
          for (int j = 0; j < PV; ++j)
            for (int b = 0; b < 6; ++b) {
              const double vv = lo + (hi - lo) * (j + 0.5 + 0.5 * gx[b]) / PV;
              ON_3dPoint P;
              ON_3dVector Su, Sv;
              boss.raw().Ev1Der(u, vv, P, Su, Sv);
              const double J = Su.x * Sv.y - Su.y * Sv.x;
              sum += gw[a] * gw[b] * 0.25 * (P.z - floor_z) * std::fabs(J) * (du.Length() / SL) * ((hi - lo) / PV);
            }
        }
      }
    v += sum;
    (void)dv;
  }
  return v;
}

// A SECOND independent volume: the divergence theorem V = (1/3) sum of faces of INT p . (Su x Sv) dA, every face done here
// (flat faces in closed form; the blend by 2D Gauss-Legendre over its whole patch; the boss over its chart region cut out by
// scanlines against a 20000-gon of the boss contact curve) - none of the kernel's trimmed-face machinery.
double FluxSolidVolume(const dino8::kernel::BossOnSlabFillet& r, const NurbsSurface& boss, double floor_z, double x0, double x1, double y0, double y1, double depth) {
  static const double gx[6] = {-0.9324695142031521, -0.6612093864662645, -0.2386191860831969, 0.2386191860831969, 0.6612093864662645, 0.9324695142031521};
  static const double gw[6] = {0.1713244923791704, 0.3607615730481386, 0.4679139345726910, 0.4679139345726910, 0.3607615730481386, 0.1713244923791704};
  const double zb = floor_z - depth, A = (x1 - x0) * (y1 - y0);
  // hole area (floor contact curve in the floor's (x, y) chart)
  double hole = 0;
  {
    const ON_Interval cd = r.fillet.trim_a.raw().Domain();
    const int n = 20000;
    ON_3dPoint prev = r.fillet.trim_a.raw().PointAt(cd.ParameterAt(0.0));
    const ON_3dPoint first = prev;
    for (int i = 1; i <= n; ++i) {
      const ON_3dPoint p = (i == n) ? first : r.fillet.trim_a.raw().PointAt(cd.ParameterAt((double)i / n));
      hole += 0.5 * (prev.x * p.y - p.x * prev.y);
      prev = p;
    }
    hole = std::fabs(hole);
  }
  double v = 0;
  v += floor_z * (A - hole) / 3.0;               // top face (n = +z)
  v += -zb * A / 3.0;                            // bottom (n = -z)
  v += x1 * (y1 - y0) * depth / 3.0 + (-x0) * (y1 - y0) * depth / 3.0 + y1 * (x1 - x0) * depth / 3.0 + (-y0) * (x1 - x0) * depth / 3.0;
  // blend
  {
    const NurbsSurface& bl = r.fillet.blend;
    const ON_Interval bu = bl.raw().Domain(0);
    double score = 0;
    for (int i = 0; i < 24; ++i) {
      const double s = bu.ParameterAt((i + 0.5) / 24.0);
      ON_3dPoint P;
      ON_3dVector n;
      bl.raw().EvNormal(s, 0.5, P, n);
      score += n * (r.fillet.centre.raw().PointAt(s) - P);
    }
    const double sgn = score > 0 ? 1.0 : -1.0;
    const int PU = 400, PV = 8;
    double sum = 0;
    for (int i = 0; i < PU; ++i)
      for (int a = 0; a < 6; ++a) {
        const double u = bu.Min() + bu.Length() * (i + 0.5 + 0.5 * gx[a]) / PU;
        for (int j = 0; j < PV; ++j)
          for (int b = 0; b < 6; ++b) {
            const double vv = (j + 0.5 + 0.5 * gx[b]) / PV;
            ON_3dPoint P;
            ON_3dVector Su, Sv;
            bl.raw().Ev1Der(u, vv, P, Su, Sv);
            const ON_3dVector n = ON_CrossProduct(Su, Sv);
            sum += gw[a] * gw[b] * 0.25 * sgn * (P.x * n.x + P.y * n.y + P.z * n.z) / 3.0 * (bu.Length() / PU) * (1.0 / PV);
          }
      }
    v += sum;
  }
  // boss (normal pointing up)
  {
    const ON_Interval du = boss.raw().Domain(0), dv = boss.raw().Domain(1);
    ON_3dPoint P0;
    ON_3dVector Su0, Sv0;
    boss.raw().Ev1Der(du.ParameterAt(0.5), dv.ParameterAt(0.5), P0, Su0, Sv0);
    const double sgn = ON_CrossProduct(Su0, Sv0).z >= 0 ? 1.0 : -1.0;
    std::vector<ON_2dPoint> poly;
    const ON_Interval cd = r.fillet.trim_b.raw().Domain();
    for (int i = 0; i < 20000; ++i) {
      const ON_3dPoint p = r.fillet.trim_b.raw().PointAt(cd.ParameterAt(i / 20000.0));
      poly.emplace_back(p.x, p.y);
    }
    const int SL = 2400;
    double sum = 0;
    for (int i = 0; i < SL; ++i)
      for (int a = 0; a < 6; ++a) {
        const double u = du.Min() + du.Length() * (i + 0.5 + 0.5 * gx[a]) / SL;
        std::vector<double> xs;
        for (size_t k = 0; k < poly.size(); ++k) {
          const ON_2dPoint &p = poly[k], &q = poly[(k + 1) % poly.size()];
          if ((p.x <= u) != (q.x <= u)) xs.push_back(p.y + (u - p.x) * (q.y - p.y) / (q.x - p.x));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
          const double lo = xs[k], hi = xs[k + 1];
          const int PV = 40;
          for (int j = 0; j < PV; ++j)
            for (int b = 0; b < 6; ++b) {
              const double vv = lo + (hi - lo) * (j + 0.5 + 0.5 * gx[b]) / PV;
              ON_3dPoint P;
              ON_3dVector Su, Sv;
              boss.raw().Ev1Der(u, vv, P, Su, Sv);
              const ON_3dVector n = ON_CrossProduct(Su, Sv);
              sum += gw[a] * gw[b] * 0.25 * sgn * (P.x * n.x + P.y * n.y + P.z * n.z) / 3.0 * (du.Length() / SL) * ((hi - lo) / PV);
            }
        }
      }
    v += sum;
  }
  return v;
}

void TestFilletBossOnSlabClosedSolid() {
  const NurbsSurface boss = FreeformBoss();
  const double x0 = -4.5, x1 = 4.5, y0 = -4.5, y1 = 4.5, depth = 1.5, r = 0.3, tol = 1e-4;
  const dino8::kernel::BossOnSlabFillet res = dino8::kernel::FilletBossOnSlab(boss, 0.0, x0, x1, y0, y1, depth, r, tol);
  std::printf("[boss on slab] status=%d reason=%s\n", (int)res.status, res.reason.c_str());
  Check(res.status == Result::Ok, "boss on slab: a closed solid is built");
  if (res.status != Result::Ok) return;
  const ON_Brep& b = res.solid.raw();
  std::printf("[boss on slab] faces=%d edges=%d vertices=%d loops=%d trims=%d edge_gap=%.3e blend N=%d dev=%.3e\n", b.m_F.Count(), b.m_E.Count(), b.m_V.Count(), b.m_L.Count(), b.m_T.Count(), res.edge_gap, res.fillet.intervals, res.fillet.deviation);
  ON_TextLog log;
  Check(b.IsValid(&log), "boss on slab: ON_Brep::IsValid");
  Check(b.IsSolid(), "boss on slab: ON_Brep::IsSolid (closed oriented manifold)");
  Check(b.m_F.Count() == 8, "boss on slab: 8 faces (5 slab sides/bottom, top with a hole, blend, trimmed boss)");
  int inner = 0;
  for (int i = 0; i < b.m_L.Count(); ++i) inner += b.m_L[i].m_type == ON_BrepLoop::inner;
  Check(inner == 1, "boss on slab: exactly one real inner loop (the floor's hole)");
  Check(res.solid.raw().SolidOrientation() > 0, "boss on slab: outward orientation (SolidOrientation > 0)");
  Check(res.edge_gap < 5e-4, "boss on slab: measured edge-vs-trim gap is below 5e-4");
  double kv = std::nan("");
  try {
    kv = res.solid.VolumeOfSealedSolid();
  } catch (const std::exception& e) {
    std::printf("[boss on slab] VolumeOfSealedSolid threw: %s\n", e.what());
  }
  const double iv = IndependentSolidVolume(res, boss, 0.0, x0, x1, y0, y1, depth);
  const double fv = FluxSolidVolume(res, boss, 0.0, x0, x1, y0, y1, depth);
  std::printf("[boss on slab] kernel exact volume %.8f, independent integral %.8f, relative difference %.3e; flux-form independent %.8f (vs height form %.3e)\n", kv, iv, std::fabs(kv - iv) / iv, fv, std::fabs(fv - iv) / iv);
  Check(std::isfinite(kv) && std::fabs(kv - iv) <= 2e-4 * iv, "boss on slab: kernel divergence-theorem volume agrees with the independent integral to 2e-4");
  // Slab-only reference: the fillet adds material (concave edge): volume must exceed the same solid with no fillet bits.
  const double slab = (x1 - x0) * (y1 - y0) * depth;
  Check(kv > slab, "boss on slab: volume exceeds the bare slab");
  // Negative control: a boss that touches the floor plane along its boundary is refused with its reason.
  {
    const NurbsSurface bad = MakeNet(12, 12, [](double a, double b2) { return ON_3dPoint(-3 + 6 * a, -3 + 6 * b2, 1.0); });
    const auto g = dino8::kernel::FilletBossOnSlab(bad, 0.0, x0, x1, y0, y1, depth, r, tol);
    std::printf("[boss on slab] flat plate: %s\n", g.reason.c_str());
    Check(g.status == Result::Failed && g.reason.find("below the floor") != std::string::npos && g.solid.raw().m_F.Count() == 0, "boss on slab: a plate never crossing the floor is refused, no solid");
  }
}
}  // namespace fillet_boss_extra_tests

D8_EXTRA_TEST(FilletBossOnSlabClosedSolid) { fillet_boss_extra_tests::TestFilletBossOnSlabClosedSolid(); }
