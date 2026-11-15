// NurbsSurface::JoinTangent: the exact Join followed by G1 smoothing of the seam (Rhino's MergeSrf with Smooth).
//
// Independent oracles: the cross derivative of the smoothed surface on EITHER side of the seam must equal the AVERAGE of the two inputs' own cross
// derivatives at the shared edge, each read by evaluating the input surface itself (A at its v max edge, B at its v min edge, both with speeds
// unchanged by the join because the merged v domain is A's domain followed by B's). The crease angle is measured from one-sided derivatives of
// the merged surface (OpenNURBS quadrant evaluation). Closed forms where the geometry has them: two planar patches at 30 degrees have a crease of
// exactly 30 degrees, and smoothing moves the two mid control rows by |Q - Q_side| / alpha so the largest deviation is the Bezier bound
// 2 t (1 - t) |dR| = 0.75 sin(15 degrees) = 0.19411; a wall and a ring (rational) meet at exactly 90 degrees.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/surface.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::SurfaceJoinTangentReport;

// One-sided cross derivatives (d = 1: across v) of `s` at the seam v = `seam`, u = `u`: left from below, right from above.
void OneSided(const NurbsSurface& s, double u, double seam, ON_3dVector& left, ON_3dVector& right) {
  ON_3dPoint p;
  ON_3dVector du, dv;
  s.raw().Ev1Der(u, seam, p, du, dv, 3);
  left = dv;
  s.raw().Ev1Der(u, seam, p, du, dv, 1);
  right = dv;
}

double Crease(const NurbsSurface& s, double seam, int n = 33) {
  double worst = 0.0;
  for (int k = 0; k < n; ++k) {
    ON_3dVector l, r;
    OneSided(s, s.raw().Domain(0).ParameterAt(k / (n - 1.0)), seam, l, r);
    worst = std::max(worst, ON_3dVector::Angle(l, r) * 180.0 / ON_PI);
  }
  return worst;
}

// Max over the seam of |left - avg| and |right - avg|, avg = (T_A + T_B) / 2 from the INPUTS' own derivatives.
double AverageError(const NurbsSurface& merged, const NurbsSurface& a, const NurbsSurface& b, double seam, int n = 33) {
  double worst = 0.0;
  for (int k = 0; k < n; ++k) {
    const double f = k / (n - 1.0);
    ON_3dPoint p;
    ON_3dVector du, dv;
    a.raw().Ev1Der(a.raw().Domain(0).ParameterAt(f), a.raw().Domain(1).Max(), p, du, dv, 3);
    const ON_3dVector ta = dv;
    b.raw().Ev1Der(b.raw().Domain(0).ParameterAt(f), b.raw().Domain(1).Min(), p, du, dv, 1);
    const ON_3dVector tb = dv;
    const ON_3dVector avg = (ta + tb) * 0.5;
    ON_3dVector l, r;
    OneSided(merged, merged.raw().Domain(0).ParameterAt(f), seam, l, r);
    worst = std::max({worst, (l - avg).Length(), (r - avg).Length()});
  }
  return worst;
}

// Distance between the merged surface's v = min / v = max edge and the source surface's v = min / v = max edge (same fraction along u).
double EdgeDistance(const NurbsSurface& m, bool m_max, const NurbsSurface& src, bool src_max, int n = 33) {
  double worst = 0.0;
  const double mv = m_max ? m.raw().Domain(1).Max() : m.raw().Domain(1).Min();
  const double sv = src_max ? src.raw().Domain(1).Max() : src.raw().Domain(1).Min();
  for (int k = 0; k < n; ++k) {
    const double f = k / (n - 1.0);
    worst = std::max(worst, m.raw().PointAt(m.raw().Domain(0).ParameterAt(f), mv).DistanceTo(src.raw().PointAt(src.raw().Domain(0).ParameterAt(f), sv)));
  }
  return worst;
}

NurbsSurface Grid(const std::vector<Point3d>& g, int nu, int nv, int pu, int pv) { return NurbsSurface::FromControlGrid(g, nu, nv, pu, pv); }

NurbsSurface RationalArcStrip(double r0, double z0, double r1, double z1) {
  // u: rational quarter arc (radius ramps linearly from r0 at v = 0 to r1 at v = 1, height z0 -> z1); v: straight ruling. Both rows share the arc weights.
  ON_NurbsSurface s;
  s.Create(3, true, 3, 2, 3, 2);
  s.SetKnot(0, 0, 0.0);
  s.SetKnot(0, 1, 0.0);
  s.SetKnot(0, 2, 1.0);
  s.SetKnot(0, 3, 1.0);
  s.SetKnot(1, 0, 0.0);
  s.SetKnot(1, 1, 1.0);
  const double w = std::sqrt(0.5);
  const double bx[3] = {1.0, 1.0, 0.0}, by[3] = {0.0, 1.0, 1.0}, bw[3] = {1.0, w, 1.0};
  for (int i = 0; i < 3; ++i) {
    s.SetCV(i, 0, ON_4dPoint(bw[i] * r0 * bx[i], bw[i] * r0 * by[i], bw[i] * z0, bw[i]));
    s.SetCV(i, 1, ON_4dPoint(bw[i] * r1 * bx[i], bw[i] * r1 * by[i], bw[i] * z1, bw[i]));
  }
  NurbsSurface out;
  out.raw() = s;
  return out;
}

}  // namespace

D8_EXTRA_TEST(JoinTangentPlanarPatchesAtThirtyDegrees) {
  using d8test::Check;
  const double c = std::cos(ON_PI / 6.0), sn = std::sin(ON_PI / 6.0);
  // FromControlGrid indexes CV(i, j) = grid[i * v_count + j]: u along x, v along y (A) and along the tilted direction (B).
  const NurbsSurface a = Grid({Point3d(0, 0, 0), Point3d(0, 3, 0), Point3d(4, 0, 0), Point3d(4, 3, 0)}, 2, 2, 1, 1);
  const NurbsSurface b = Grid({Point3d(0, 3, 0), Point3d(0, 3 + 3 * c, 3 * sn), Point3d(4, 3, 0), Point3d(4, 3 + 3 * c, 3 * sn)}, 2, 2, 1, 1);

  NurbsSurface plain = a;
  Check(plain.Join(b) == Result::Ok, "control: the exact Join of the two planar patches returns Ok");
  Check(std::abs(Crease(plain, 1.0) - 30.0) < 1e-6, "control: the plain join keeps the 30 degree crease (measured from one-sided derivatives, closed form 30)");

  NurbsSurface m = a;
  SurfaceJoinTangentReport rep;
  const Result r = m.JoinTangent(b, rep);
  Check(r == Result::Ok, "JoinTangent of two planar patches at 30 degrees returns Ok");
  if (r != Result::Ok) return;
  Check(Crease(m, 1.0) < 1e-6, "JoinTangent: the crease at the seam is gone (< 1e-6 degrees, measured independently)");
  Check(AverageError(m, a, b, 1.0) < 1e-9, "JoinTangent: the cross derivative on both sides of the seam IS the average of the inputs' own (1e-9)");
  Check(std::abs(rep.max_crease_angle_before_degrees - 30.0) < 1e-6 && rep.max_crease_angle_after_degrees < 1e-6, "JoinTangent: the report's before/after crease agree with the independent measurements");
  Check(rep.across_direction == 1 && std::abs(rep.seam_parameter - 1.0) < 1e-12, "JoinTangent: the seam is reported at v = 1 across V");
  Check(std::abs(rep.max_deviation - 0.75 * std::sin(ON_PI / 12.0)) < 1e-6, "JoinTangent: the deviation from the plain join is the Bezier bound 0.75 sin(15 degrees) = 0.19411 (1e-6)");
  Check(rep.max_seam_shift < 1e-12, "JoinTangent: the seam curve did not move (1e-12)");
  Check(EdgeDistance(m, false, a, false) < 1e-12 && EdgeDistance(m, true, b, true) < 1e-12, "JoinTangent: both outer edges are exactly the inputs' (1e-12)");
  double seam_err = 0.0;
  for (int k = 0; k <= 16; ++k) {
    const double x = 4.0 * k / 16.0;
    seam_err = std::max(seam_err, m.raw().PointAt(m.raw().Domain(0).ParameterAt(k / 16.0), 1.0).DistanceTo(ON_3dPoint(x, 3.0, 0.0)));
  }
  Check(seam_err < 1e-12, "JoinTangent: the seam is the line y = 3, z = 0 (closed form, 1e-12)");
}

D8_EXTRA_TEST(JoinTangentBicubicPatchesWithDifferentKnotSpans) {
  using d8test::Check;
  // A: 5 x 4 bicubic (one span across), B: 5 x 5 bicubic (two spans across), sharing the row S. Different spans on the two sides (below) make the
  // one-sided derivative scales alpha = p / span differ, so equal speeds are NOT assumed anywhere.
  std::vector<Point3d> shared;
  for (int i = 0; i < 5; ++i) shared.push_back(Point3d(i, 4.0, 0.3 * std::sin(1.1 * i)));
  std::vector<Point3d> ga, gb;
  for (int i = 0; i < 5; ++i) {
    for (int j = 0; j < 3; ++j) ga.push_back(Point3d(i, 1.3 * j, 0.4 * std::cos(0.9 * i + j)));  // rows j = 0..2, then the shared row
    ga.push_back(shared[static_cast<size_t>(i)]);
  }
  for (int i = 0; i < 5; ++i) {
    gb.push_back(shared[static_cast<size_t>(i)]);
    for (int j = 1; j < 5; ++j) gb.push_back(Point3d(i + 0.3 * j, 4.0 + 1.1 * j, 0.5 * std::sin(0.7 * i * j) + 0.6 * j));
  }
  const NurbsSurface a = Grid(ga, 5, 4, 3, 3);
  NurbsSurface b = Grid(gb, 5, 5, 3, 3);
  // B's across domain is rescaled from [0, 2] to [0, 0.5]: its spans are 0.25 long against A's 1, so the one-sided scales alpha = p / span are 12
  // and 3 and B's cross derivative is four times as fast.
  Check(b.SetDomain(1, 0.0, 0.5) == Result::Ok && std::abs(b.raw().Domain(1).Length() - 0.5) < 1e-15, "fixture: B's across domain is rescaled to [0, 0.5]");

  NurbsSurface plain = a;
  Check(plain.Join(b) == Result::Ok, "control: the exact Join returns Ok");
  const double before = Crease(plain, 1.0);
  Check(before > 1.0, "control: the plain join of the two bicubic patches has a real crease (> 1 degree)");

  NurbsSurface m = a;
  SurfaceJoinTangentReport rep;
  const Result r = m.JoinTangent(b, rep);
  Check(r == Result::Ok, "JoinTangent of two bicubic patches with different knot spans returns Ok");
  if (r != Result::Ok) return;
  Check(Crease(m, 1.0) < 1e-6, "JoinTangent: the crease is gone (< 1e-6 degrees)");
  Check(AverageError(m, a, b, 1.0) < 1e-9, "JoinTangent: both one-sided cross derivatives equal the average of the inputs' own, though their spans differ (1e-9)");
  Check(std::abs(rep.max_crease_angle_before_degrees - before) < 1e-6 && rep.max_deviation > 1e-3, "JoinTangent: the report agrees with the measured crease and shows a real deviation");
  Check(EdgeDistance(m, false, a, false) < 1e-12 && EdgeDistance(m, true, b, true) < 1e-12 && rep.max_seam_shift < 1e-12, "JoinTangent: outer edges and seam curve are untouched (1e-12)");
}

D8_EXTRA_TEST(JoinTangentRationalWallAndRing) {
  using d8test::Check;
  // A vertical rational wall (radius 2, z 0 -> 1.2) and a flat rational ring (radius 2 -> 4 at z = 1.2) meet at exactly 90 degrees along the arc.
  const NurbsSurface wall = RationalArcStrip(2.0, 0.0, 2.0, 1.2);
  const NurbsSurface ring = RationalArcStrip(2.0, 1.2, 4.0, 1.2);
  NurbsSurface plain = wall;
  Check(plain.Join(ring) == Result::Ok, "control: the exact Join of a rational wall and ring returns Ok");
  Check(std::abs(Crease(plain, 1.0) - 90.0) < 1e-6, "control: the wall/ring join has the 90 degree crease (closed form)");
  NurbsSurface m = wall;
  SurfaceJoinTangentReport rep;
  const Result r = m.JoinTangent(ring, rep);
  Check(r == Result::Ok, "JoinTangent of a rational wall and ring returns Ok");
  if (r != Result::Ok) return;
  Check(m.IsRational(), "JoinTangent: the result stays rational");
  Check(Crease(m, 1.0) < 1e-6, "JoinTangent: the 90 degree crease is gone (< 1e-6 degrees)");
  Check(AverageError(m, wall, ring, 1.0) < 1e-9, "JoinTangent: the Euclidean cross derivatives on both sides equal the average of the inputs' own, rational weights included (1e-9)");
  double on_arc = 0.0, outer = 0.0, inner = 0.0;
  for (int k = 0; k <= 32; ++k) {
    const double f = k / 32.0;
    const double u = m.raw().Domain(0).ParameterAt(f);
    const ON_3dPoint seam = m.raw().PointAt(u, 1.0), top = m.raw().PointAt(u, 2.0), bottom = m.raw().PointAt(u, 0.0);
    on_arc = std::max({on_arc, std::abs(std::hypot(seam.x, seam.y) - 2.0), std::abs(seam.z - 1.2)});
    outer = std::max({outer, std::abs(std::hypot(top.x, top.y) - 4.0), std::abs(top.z - 1.2)});
    inner = std::max({inner, std::abs(std::hypot(bottom.x, bottom.y) - 2.0), std::abs(bottom.z)});
  }
  Check(on_arc < 1e-12 && outer < 1e-12 && inner < 1e-12, "JoinTangent: the seam is the radius-2 arc at z = 1.2, the outer edge the radius-4 arc, the base the radius-2 arc at z = 0 (1e-12)");
}

D8_EXTRA_TEST(JoinTangentAlreadySmoothAndRefusals) {
  using d8test::Check;
  // Coplanar continuation: no crease to begin with, smoothing must change nothing.
  const NurbsSurface a = Grid({Point3d(0, 0, 0), Point3d(0, 3, 0), Point3d(4, 0, 0), Point3d(4, 3, 0)}, 2, 2, 1, 1);
  const NurbsSurface b = Grid({Point3d(0, 3, 0), Point3d(0, 6, 0), Point3d(4, 3, 0), Point3d(4, 6, 0)}, 2, 2, 1, 1);
  NurbsSurface m = a;
  SurfaceJoinTangentReport rep;
  Check(m.JoinTangent(b, rep) == Result::Ok, "coplanar patches: JoinTangent returns Ok");
  Check(rep.max_crease_angle_before_degrees < 1e-9 && rep.max_deviation < 1e-12, "coplanar patches: no crease before, and smoothing deviates by < 1e-12");
  double zmax = 0.0;
  for (int i = 0; i <= 8; ++i)
    for (int j = 0; j <= 8; ++j) zmax = std::max(zmax, std::abs(m.raw().PointAt(m.raw().Domain(0).ParameterAt(i / 8.0), m.raw().Domain(1).ParameterAt(j / 8.0)).z));
  Check(zmax < 1e-12, "coplanar patches: the merged surface is still the plane z = 0");

  // Refusals: patches that do not share an edge throw (documented Join behaviour) and leave this surface untouched.
  const NurbsSurface far = Grid({Point3d(0, 10, 0), Point3d(0, 13, 0), Point3d(4, 10, 0), Point3d(4, 13, 0)}, 2, 2, 1, 1);
  NurbsSurface n = a;
  bool threw = false;
  try { SurfaceJoinTangentReport r2; n.JoinTangent(far, r2); } catch (const std::invalid_argument&) { threw = true; }
  Check(threw && n.raw().PointAt(0.5, 0.5).DistanceTo(a.raw().PointAt(0.5, 0.5)) < 1e-15 && n.CVCountU() == a.CVCountU() && n.CVCountV() == a.CVCountV(),
        "patches that do not share an edge: JoinTangent throws and leaves the surface untouched");
}
