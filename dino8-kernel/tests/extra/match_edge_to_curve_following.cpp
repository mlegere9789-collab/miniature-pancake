// NurbsSurface::MatchEdgeToCurveFollowing: match a wall's edge to a curve lying on a HOST surface (a sphere) so that the wall
// leaves the curve in the host's tangent plane.
//
// Fixture: the host is the sphere of radius 5 about the origin; the target is the rational quarter arc of the latitude circle
// z = 3 (centre (0,0,3), radius 4, from (4,0,3) to (0,4,3)), which lies on the sphere because 4^2 + 3^2 = 5^2. The wall's own cross
// derivative is axial, D0 = (0,0,h). Every expectation is a closed form of that geometry, never read back from the construction:
//   * the host normal at an edge point p is p / 5 (a property of the sphere, not of the host's NURBS evaluation);
//   * the tangent-plane projection of D0 is h (0,0,1) - h (3/5) p/5 and, rescaled to the wall's own speed h, equals
//     h * (-0.6 cos t, -0.6 sin t, 0.8) with (cos t, sin t) = (p.x, p.y) / 4;
//   * so after the edit the inward derivative must be exactly that vector, perpendicular to p, with length h;
//   * the control (plain MatchEdgeToCurve with its preserved field) keeps (0,0,h), whose component along p/5 is 0.6 h.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::MatchContinuity;
using dino8::kernel::MatchFollowReport;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

constexpr int kSamples = 33;

struct EdgeD {
  ON_3dPoint p;
  ON_3dVector inward;
};

EdgeD EdgeAt(const NurbsSurface& s, int fixed_direction, bool at_min, double frac) {
  const ON_NurbsSurface& r = s.raw();
  const double at = at_min ? r.Domain(fixed_direction).Min() : r.Domain(fixed_direction).Max();
  const double e = r.Domain(1 - fixed_direction).ParameterAt(frac);
  EdgeD out;
  ON_3dVector du, dv;
  if (fixed_direction == 0) r.Ev1Der(at, e, out.p, du, dv); else r.Ev1Der(e, at, out.p, du, dv);
  out.inward = fixed_direction == 0 ? du : dv;
  if (!at_min) out.inward = -out.inward;
  return out;
}

bool SameCVs(const NurbsSurface& a, const NurbsSurface& b) {
  if (a.CVCountU() != b.CVCountU() || a.CVCountV() != b.CVCountV() || a.IsRational() != b.IsRational()) return false;
  for (int i = 0; i < a.CVCountU(); ++i)
    for (int j = 0; j < a.CVCountV(); ++j) {
      ON_4dPoint pa, pb;
      a.raw().GetCV(i, j, pa);
      b.raw().GetCV(i, j, pb);
      if (pa.x != pb.x || pa.y != pb.y || pa.z != pb.z || pa.w != pb.w) return false;
    }
  return true;
}

NurbsCurve QuarterArc(const ON_3dPoint& c, double r) {
  ON_NurbsCurve a;
  a.Create(3, true, 3, 3);
  a.SetKnot(0, 0.0);
  a.SetKnot(1, 0.0);
  a.SetKnot(2, 1.0);
  a.SetKnot(3, 1.0);
  const double w = std::sqrt(0.5);
  a.SetCV(0, ON_4dPoint(c.x + r, c.y, c.z, 1.0));
  a.SetCV(1, ON_4dPoint(w * (c.x + r), w * (c.y + r), w * c.z, w));
  a.SetCV(2, ON_4dPoint(c.x, c.y + r, c.z, 1.0));
  NurbsCurve out;
  out.raw() = a;
  return out;
}

NurbsSurface Sphere(double radius) {
  ON_Sphere sph(ON_3dPoint(0, 0, 0), radius);
  NurbsSurface out;
  sph.GetNurbForm(out.raw());
  return out;
}

// A flat vertical wall in the plane y = -2: bottom edge (v = min) from (1,-2,z0) to (6,-2,z0), top edge at z1.
NurbsSurface Wall(double z0, double z1) {
  // FromControlGrid indexes CV(i, j) = grid[i * v_count + j] (v fastest): u runs along x, v up the wall.
  const std::vector<Point3d> grid = {Point3d(1, -2, z0), Point3d(1, -2, z1), Point3d(6, -2, z0), Point3d(6, -2, z1)};
  return NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
}

// A rational quarter cylinder (radius 2 about the z axis, height h): u along the base arc, v along the axis.
NurbsSurface RationalWall(double h) {
  ON_NurbsSurface cyl;
  cyl.Create(3, true, 3, 2, 3, 2);
  cyl.SetKnot(0, 0, 0.0);
  cyl.SetKnot(0, 1, 0.0);
  cyl.SetKnot(0, 2, 1.0);
  cyl.SetKnot(0, 3, 1.0);
  cyl.SetKnot(1, 0, 0.0);
  cyl.SetKnot(1, 1, 1.0);
  const double w = std::sqrt(0.5), R = 2.0;
  const double bx[3] = {R, R, 0.0}, by[3] = {0.0, R, R}, bw[3] = {1.0, w, 1.0};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 2; ++j) cyl.SetCV(i, j, ON_4dPoint(bw[i] * bx[i], bw[i] * by[i], bw[i] * h * j, bw[i]));
  NurbsSurface s;
  s.raw() = cyl;
  return s;
}

// Checks the closed forms on an edited surface whose own cross derivative was (0,0,h). Returns the worst deviations.
struct Worst {
  double on_circle = 0, on_sphere = 0, perpendicular = 0, length = 0, direction = 0;
};
Worst Measure(const NurbsSurface& s, int fd, bool at_min, double h) {
  Worst w;
  for (int k = 0; k < kSamples; ++k) {
    const EdgeD e = EdgeAt(s, fd, at_min, k / (kSamples - 1.0));
    w.on_circle = std::max({w.on_circle, std::abs(std::hypot(e.p.x, e.p.y) - 4.0), std::abs(e.p.z - 3.0)});
    w.on_sphere = std::max(w.on_sphere, std::abs(ON_3dVector(e.p).Length() - 5.0));
    const ON_3dVector n = ON_3dVector(e.p) * (1.0 / 5.0);
    w.perpendicular = std::max(w.perpendicular, std::abs(e.inward * n) / h);
    w.length = std::max(w.length, std::abs(e.inward.Length() - h) / h);
    const double c = e.p.x / 4.0, sn = e.p.y / 4.0;
    w.direction = std::max(w.direction, (e.inward - ON_3dVector(-0.6 * c, -0.6 * sn, 0.8) * h).Length() / h);
  }
  return w;
}

}  // namespace

D8_EXTRA_TEST(MatchEdgeToCurveFollowingSphereTangentPlane) {
  using d8test::Check;
  const NurbsSurface host = Sphere(5.0);
  Check(host.raw().IsValid() && host.IsRational(), "fixture: the host sphere is a valid rational surface");
  const NurbsCurve arc = QuarterArc(ON_3dPoint(0, 0, 3), 4.0);

  // --- the wall's v = min edge (z = 3), cross derivative (0, 0, h) with h measured on the input wall ---
  {
    const NurbsSurface original = Wall(3.0, 6.0);
    const double h = EdgeAt(original, 1, true, 0.5).inward.Length();
    Check(std::abs(EdgeAt(original, 1, true, 0.5).inward.z - h) < 1e-12 && h > 0.5, "fixture: the wall's own inward derivative is axial");

    NurbsSurface control = original;  // plain MatchEdgeToCurve keeps the axial direction, which is NOT tangent to the sphere
    Check(control.MatchEdgeToCurve(1, true, arc, MatchContinuity::Tangent) == Result::Ok, "control: the preserved-field match returns Ok");
    double along = 0.0;
    for (int k = 0; k < kSamples; ++k) {
      const EdgeD e = EdgeAt(control, 1, true, k / (kSamples - 1.0));
      along = std::max(along, std::abs(std::abs(e.inward * (ON_3dVector(e.p) * 0.2)) / h - 0.6));
    }
    Check(along < 1e-9, "control: the preserved axial direction makes the angle with the sphere normal cos = 3/5 (0.6 h along p/5) at every sample");

    NurbsSurface s = original;
    MatchFollowReport rep;
    const Result r = s.MatchEdgeToCurveFollowing(1, true, arc, host, &rep);
    Check(r == Result::Ok, "following the sphere: MatchEdgeToCurveFollowing returns Ok");
    if (r == Result::Ok) {
      const Worst w = Measure(s, 1, true, h);
      Check(w.on_circle < 1e-12 && w.on_sphere < 1e-12, "following: the edge IS the latitude circle (radius 4 at z = 3) and lies on the sphere (1e-12)");
      Check(w.perpendicular < 1e-6, "following: the inward derivative is perpendicular to the sphere normal p/5 (|d.n|/h < 1e-6)");
      Check(w.length < 1e-6, "following: the wall keeps its own speed |d| = h (relative 1e-6)");
      Check(w.direction < 1e-6, "following: d = h (-0.6 cos t, -0.6 sin t, 0.8), the axial direction rotated into the tangent plane (1e-6)");
      Check(rep.max_tangent_plane_deviation < 1e-6 && rep.max_host_gap < 1e-7, "following: the report's own measurements agree (deviation < 1e-6, host gap < 1e-7: the closest-point search converges to ~1e-8)");
      Check(rep.field_fit_error < 1e-6 && rep.field_control_points >= 16, "following: the fitted field reproduces the sampled one between its nodes (1e-6)");
      double far = 0.0;
      for (int k = 0; k < kSamples; ++k) far = std::max(far, EdgeAt(s, 1, false, k / (kSamples - 1.0)).p.DistanceTo(EdgeAt(original, 1, false, k / (kSamples - 1.0)).p));
      Check(far < 1e-12, "following: the far edge never moved (1e-12)");
    }
  }

  // --- the same match on the v = max edge (wall flipped: the z = 3 row is v = 1) and with the arc handed over reversed ---
  {
    const NurbsSurface original = Wall(6.0, 3.0);
    const double h = EdgeAt(original, 1, false, 0.5).inward.Length();
    NurbsCurve flipped = arc;
    flipped.raw().Reverse();
    flipped.raw().SetDomain(arc.raw().Domain().Min(), arc.raw().Domain().Max());
    NurbsSurface a = original, b = original;
    MatchFollowReport ra, rb;
    const Result r1 = a.MatchEdgeToCurveFollowing(1, false, arc, host, &ra);
    const Result r2 = b.MatchEdgeToCurveFollowing(1, false, flipped, host, &rb);
    Check(r1 == Result::Ok && r2 == Result::Ok, "following on the v = max edge: both orientations of the arc return Ok");
    Check(ra.match.target_edge_reversed != rb.match.target_edge_reversed, "following on the v = max edge: exactly one orientation needed the reversal (both branches exercised)");
    if (r1 == Result::Ok && r2 == Result::Ok) {
      const Worst wa = Measure(a, 1, false, h), wb = Measure(b, 1, false, h);
      Check(std::max(wa.perpendicular, wb.perpendicular) < 1e-6 && std::max(wa.direction, wb.direction) < 1e-6,
            "following on the v = max edge: perpendicular to the normal and the closed-form direction for both orientations (1e-6)");
    }
  }

  // --- a RATIONAL wall: its own cross derivative is a rational function, so the preserved-field mode refuses it, but it can follow ---
  {
    const NurbsSurface original = RationalWall(1.2);
    const double h = EdgeAt(original, 1, true, 0.5).inward.Length();
    NurbsSurface refused = original;
    Check(refused.MatchEdgeToCurve(1, true, arc, MatchContinuity::Tangent) == Result::Failed && SameCVs(refused, original),
          "control: MatchEdgeToCurve with the default field still refuses the rational wall, surface bit-identical");
    NurbsSurface s = original;
    MatchFollowReport rep;
    const Result r = s.MatchEdgeToCurveFollowing(1, true, arc, host, &rep);
    Check(r == Result::Ok, "rational wall: MatchEdgeToCurveFollowing returns Ok");
    if (r == Result::Ok) {
      const Worst w = Measure(s, 1, true, h);
      Check(w.on_circle < 1e-12 && w.on_sphere < 1e-12, "rational wall: the edge is the latitude circle on the sphere (1e-12)");
      Check(w.perpendicular < 1e-6 && w.length < 1e-6 && w.direction < 1e-6,
            "rational wall: inward derivative perpendicular to p/5, speed kept, direction h (-0.6 cos t, -0.6 sin t, 0.8) (1e-6)");
    }
  }

  // --- refusals: surface restored bit-identical, reason given ---
  {
    NurbsSurface s = Wall(3.0, 6.0);
    const NurbsSurface before = s;
    const NurbsCurve off = QuarterArc(ON_3dPoint(0, 0, 3.5), 4.0);  // |p| = 5.315: 0.315 off the sphere
    MatchFollowReport rep;
    Check(s.MatchEdgeToCurveFollowing(1, true, off, host, &rep) == Result::Failed && SameCVs(s, before) && rep.refusal_reason.find("not on the host") != std::string::npos,
          "a target 0.315 off the host is refused (not on the host), surface bit-identical");
    NurbsSurface flat = Wall(3.0, 3.0);  // identical rows: zero cross derivative, nothing to rotate
    const NurbsSurface flat_before = flat;
    MatchFollowReport rep2;
    Check(flat.MatchEdgeToCurveFollowing(1, true, arc, host, &rep2) == Result::Failed && SameCVs(flat, flat_before) && !rep2.refusal_reason.empty(),
          "a wall with no cross direction is refused with a reason, surface bit-identical");
    bool threw = false;
    try { NurbsSurface t = Wall(3.0, 6.0); t.MatchEdgeToCurveFollowing(2, true, arc, host); } catch (const std::invalid_argument&) { threw = true; }
    Check(threw, "a direction outside 0/1 throws std::invalid_argument");
  }
}
