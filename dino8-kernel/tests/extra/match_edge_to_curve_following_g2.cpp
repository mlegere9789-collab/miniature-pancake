// NurbsSurface::MatchEdgeToCurveFollowing with MatchContinuity::Curvature: G2 to a host surface along a curve.
//
// Hosts with closed-form curvature: the sphere of radius 5 (umbilic: every normal curvature is -1/5 against the outward normal,
// K = 1/25, |H| = 1/5) and the cylinder of radius 5 about the z axis (normal curvature in a tangent direction D is -(D.T)^2 / (|D|^2 R)
// with T the circumferential unit vector, K = 0, |H| = 1/10). The edited surface is measured from ITS OWN second derivatives (Gaussian
// and mean curvature from the first and second fundamental forms, the cross-direction normal curvature S_cc.n/|D|^2), compared with
// those closed forms - never with what the construction prescribed. Control: the same match at Tangent (G1) leaves the wall's own
// S_cc (zero for a planar wall), so its curvature across the edge is 0, not the host's.
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

struct EdgeCurv {
  ON_3dPoint p;
  ON_3dVector inward;
  double gauss = 0.0;      // Gaussian curvature from the fundamental forms
  double mean_abs = 0.0;   // |mean curvature|
  double cross_cc_n = 0.0; // S_cc . n_out, n_out = the outward host normal supplied by the caller
};

// Measures the edited surface at the edge: inward derivative, Gaussian and |mean| curvature, and S_cc . n_out.
EdgeCurv Measure(const NurbsSurface& s, int fd, bool at_min, double frac, const ON_3dVector& n_out) {
  const ON_NurbsSurface& r = s.raw();
  const double at = at_min ? r.Domain(fd).Min() : r.Domain(fd).Max();
  const double e = r.Domain(1 - fd).ParameterAt(frac);
  EdgeCurv out;
  ON_3dVector su, sv, suu, suv, svv;
  if (fd == 0) r.Ev2Der(at, e, out.p, su, sv, suu, suv, svv); else r.Ev2Der(e, at, out.p, su, sv, suu, suv, svv);
  out.inward = fd == 0 ? su : sv;
  if (!at_min) out.inward = -out.inward;
  ON_3dVector nn = ON_CrossProduct(su, sv);
  nn.Unitize();
  const double E = su * su, F = su * sv, G = sv * sv, L = nn * suu, M = nn * suv, N = nn * svv;
  const double den = E * G - F * F;
  out.gauss = (L * N - M * M) / den;
  out.mean_abs = std::abs((E * N - 2.0 * F * M + G * L) / (2.0 * den));
  out.cross_cc_n = (fd == 0 ? suu : svv) * n_out;
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

NurbsSurface Cylinder(double radius, double z0, double z1) {
  ON_Cylinder cyl(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector::ZAxis), radius));
  cyl.height[0] = z0;
  cyl.height[1] = z1;
  NurbsSurface out;
  cyl.GetNurbForm(out.raw());
  return out;
}

// A flat wall in the plane y = -2 whose bottom edge (v = min) runs from (1,-2,z0) to (6,-2,z0) and whose top edge is shifted by
// (dx, 0, z1 - z0): constant cross derivative (dx, 0, z1 - z0) per unit v. FromControlGrid indexes CV(i, j) = grid[i * v_count + j].
NurbsSurface Wall(double z0, double z1, double dx = 0.0) {
  const std::vector<Point3d> grid = {Point3d(1, -2, z0), Point3d(1 + dx, -2, z1), Point3d(6, -2, z0), Point3d(6 + dx, -2, z1)};
  return NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
}

// A rational quarter cylinder wall (radius 2 about z, height h), u along the base arc, v along the axis.
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

struct Worst {
  double gauss = 0, mean = 0, cross = 0, on_host = 0;
};

// Sphere host, centre origin, radius 5: K = 0.04, |H| = 0.2, S_cc . n_out / |D|^2 = -0.2.
Worst MeasureSphere(const NurbsSurface& s, int fd, bool at_min) {
  Worst w;
  for (int k = 0; k < kSamples; ++k) {
    const double f = k / (kSamples - 1.0);
    ON_3dPoint p0;
    {
      const EdgeCurv probe = Measure(s, fd, at_min, f, ON_3dVector(0, 0, 0));
      p0 = probe.p;
    }
    const ON_3dVector n_out = ON_3dVector(p0) * (1.0 / 5.0);
    const EdgeCurv e = Measure(s, fd, at_min, f, n_out);
    w.on_host = std::max(w.on_host, std::abs(ON_3dVector(e.p).Length() - 5.0));
    w.gauss = std::max(w.gauss, std::abs(e.gauss - 0.04));
    w.mean = std::max(w.mean, std::abs(e.mean_abs - 0.2));
    w.cross = std::max(w.cross, std::abs(e.cross_cc_n / (e.inward * e.inward) + 0.2));
  }
  return w;
}

}  // namespace

D8_EXTRA_TEST(MatchEdgeToCurveFollowingCurvatureSphere) {
  using d8test::Check;
  const NurbsSurface host = Sphere(5.0);
  const NurbsCurve arc = QuarterArc(ON_3dPoint(0, 0, 3), 4.0);  // latitude z = 3: on the sphere

  // --- planar wall, G2 to the sphere ---
  {
    const NurbsSurface original = Wall(3.0, 6.0);
    NurbsSurface s = original;
    MatchFollowReport rep;
    const Result r = s.MatchEdgeToCurveFollowing(1, true, arc, host, MatchContinuity::Curvature, &rep);
    Check(r == Result::Ok, "G2 to a sphere: MatchEdgeToCurveFollowing(Curvature) returns Ok");
    if (r == Result::Ok) {
      const Worst w = MeasureSphere(s, 1, true);
      Check(w.on_host < 1e-12, "G2 to a sphere: every edge sample is on the sphere (1e-12)");
      Check(w.gauss < 1e-5, "G2 to a sphere: the edited surface's Gaussian curvature along the edge is 1/25 (1e-5, from its own fundamental forms)");
      Check(w.mean < 1e-5, "G2 to a sphere: its mean curvature magnitude along the edge is 1/5 (1e-5)");
      Check(w.cross < 1e-5, "G2 to a sphere: the normal curvature across the edge, S_vv.n/|D|^2 against the outward normal p/5, is -1/5 (1e-5)");
      Check(rep.max_normal_curvature_error < 1e-5 && rep.max_gaussian_curvature_error < 1e-5 && rep.max_tangent_plane_deviation < 1e-6,
            "G2 to a sphere: the report's own measurements agree (1e-5 / 1e-5 / 1e-6)");
      double far = 0.0;
      for (int k = 0; k < kSamples; ++k) {
        const double f = k / (kSamples - 1.0);
        const ON_NurbsSurface& a = s.raw();
        const ON_NurbsSurface& b = original.raw();
        far = std::max(far, a.PointAt(a.Domain(0).ParameterAt(f), a.Domain(1).Max()).DistanceTo(b.PointAt(b.Domain(0).ParameterAt(f), b.Domain(1).Max())));
      }
      Check(far < 1e-12, "G2 to a sphere: the far edge never moved (1e-12)");
    }

    // control: G1 follow keeps the wall's own (zero) S_vv, so it is NOT curvature-continuous with the sphere
    NurbsSurface g1 = original;
    Check(g1.MatchEdgeToCurveFollowing(1, true, arc, host) == Result::Ok, "control: the G1 follow returns Ok");
    const Worst wg1 = MeasureSphere(g1, 1, true);
    Check(wg1.cross > 0.15 && wg1.gauss > 0.02, "control: the G1 follow's normal curvature across the edge is ~0 (error ~0.2) and its Gaussian curvature is not 1/25");
  }

  // --- a rational wall: its own second cross derivative is a rational function (sampled, not read off the rows) ---
  {
    NurbsSurface s = RationalWall(1.2);
    MatchFollowReport rep;
    const Result r = s.MatchEdgeToCurveFollowing(1, true, arc, host, MatchContinuity::Curvature, &rep);
    Check(r == Result::Ok, "G2 to a sphere, rational wall: returns Ok");
    if (r == Result::Ok) {
      const Worst w = MeasureSphere(s, 1, true);
      Check(w.gauss < 1e-5 && w.mean < 1e-5 && w.cross < 1e-5, "G2 to a sphere, rational wall: K = 1/25, |H| = 1/5 and normal curvature -1/5 across the edge (1e-5)");
    }
  }

  // --- v = max edge, arc handed over reversed ---
  {
    const NurbsSurface original = Wall(6.0, 3.0);  // the z = 3 row is v = 1
    NurbsCurve flipped = arc;
    flipped.raw().Reverse();
    flipped.raw().SetDomain(arc.raw().Domain().Min(), arc.raw().Domain().Max());
    NurbsSurface a = original, b = original;
    const Result r1 = a.MatchEdgeToCurveFollowing(1, false, arc, host, MatchContinuity::Curvature);
    const Result r2 = b.MatchEdgeToCurveFollowing(1, false, flipped, host, MatchContinuity::Curvature);
    Check(r1 == Result::Ok && r2 == Result::Ok, "G2 on the v = max edge: both orientations of the arc return Ok");
    if (r1 == Result::Ok && r2 == Result::Ok) {
      const Worst wa = MeasureSphere(a, 1, false), wb = MeasureSphere(b, 1, false);
      Check(std::max(wa.gauss, wb.gauss) < 1e-5 && std::max(wa.cross, wb.cross) < 1e-5, "G2 on the v = max edge: K = 1/25 and normal curvature -1/5 for both orientations (1e-5)");
    }
  }

  // --- refusals: surface restored bit-identical ---
  {
    NurbsSurface s = Wall(3.0, 6.0);
    const NurbsSurface before = s;
    MatchFollowReport rep;
    Check(s.MatchEdgeToCurveFollowing(1, true, arc, host, MatchContinuity::Position, &rep) == Result::Failed && SameCVs(s, before) && !rep.refusal_reason.empty(),
          "Position is refused with a reason, surface bit-identical");
    const NurbsCurve off = QuarterArc(ON_3dPoint(0, 0, 3.5), 4.0);
    MatchFollowReport rep2;
    Check(s.MatchEdgeToCurveFollowing(1, true, off, host, MatchContinuity::Curvature, &rep2) == Result::Failed && SameCVs(s, before) &&
              rep2.refusal_reason.find("not on the host") != std::string::npos,
          "G2 with a curve 0.315 off the host is refused (not on the host), surface bit-identical");
  }
}

D8_EXTRA_TEST(MatchEdgeToCurveFollowingCurvatureCylinder) {
  using d8test::Check;
  // Cylinder host (radius 5 about z): NOT umbilic - normal curvature depends on the direction. The wall's cross direction is tilted,
  // (3, 0, 3), so after rotation into the tangent plane D has a circumferential part and the required normal curvature across the edge
  // is -(D.T)^2 / (|D|^2 R), T = (-sin t, cos t, 0) at the edge point (cos t, sin t) = (p.x, p.y) / 5; K = 0 and |H| = 1/10 everywhere.
  const NurbsSurface host = Cylinder(5.0, -2.0, 8.0);
  const NurbsCurve arc = QuarterArc(ON_3dPoint(0, 0, 3), 5.0);
  NurbsSurface s = Wall(3.0, 6.0, 3.0);
  MatchFollowReport rep;
  const Result r = s.MatchEdgeToCurveFollowing(1, true, arc, host, MatchContinuity::Curvature, &rep);
  Check(r == Result::Ok, "G2 to a cylinder with a tilted wall: returns Ok");
  if (r == Result::Ok) {
    double on = 0.0, perp = 0.0, gauss = 0.0, mean = 0.0, cross = 0.0, span = 0.0;
    for (int k = 0; k < kSamples; ++k) {
      const double f = k / (kSamples - 1.0);
      const EdgeCurv pr = Measure(s, 1, true, f, ON_3dVector(0, 0, 0));
      const double c = pr.p.x / 5.0, sn = pr.p.y / 5.0;
      const ON_3dVector n_out(c, sn, 0.0), tcirc(-sn, c, 0.0);
      const EdgeCurv e = Measure(s, 1, true, f, n_out);
      on = std::max(on, std::abs(std::hypot(e.p.x, e.p.y) - 5.0));
      perp = std::max(perp, std::abs(e.inward * n_out) / e.inward.Length());
      gauss = std::max(gauss, std::abs(e.gauss));
      mean = std::max(mean, std::abs(e.mean_abs - 0.1));
      const double want = -((e.inward * tcirc) * (e.inward * tcirc)) / ((e.inward * e.inward) * 5.0);
      cross = std::max(cross, std::abs(e.cross_cc_n / (e.inward * e.inward) - want));
      span = std::max(span, std::abs(want));
    }
    Check(on < 1e-12 && perp < 1e-6, "G2 to a cylinder: the edge is on the cylinder and the wall leaves in its tangent plane (1e-12 / 1e-6)");
    Check(gauss < 1e-5 && mean < 1e-5, "G2 to a cylinder: K = 0 and |H| = 1/10 along the edge, from the edited surface's own forms (1e-5)");
    Check(span > 0.02 && cross < 1e-5, "G2 to a cylinder: the normal curvature across the edge equals -(D.T)^2/(|D|^2 R) (1e-5), and that target is not trivially 0");
    Check(rep.max_normal_curvature_error < 1e-5 && rep.max_gaussian_curvature_error < 1e-5, "G2 to a cylinder: the report's own measurements agree");
  }
}
