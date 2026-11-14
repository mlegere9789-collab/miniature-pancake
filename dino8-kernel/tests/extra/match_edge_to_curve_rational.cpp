// NurbsSurface::MatchEdgeToCurve, Tangent / Curvature against a RATIONAL target curve (an exact arc or circle).
//
// Before this change MatchEdgeToCurve returned Result::Failed for any G1/G2 request whose target curve was rational
// (a prescribed 3D derivative field against a rational row needs the field times the weight function as a spline).
// These checks use independent oracles, none derived from how the kernel builds the ribbon:
//  * the target is an exact circle / arc, so every edge point must lie at the closed-form radius from the closed-form
//    centre (a property of the circle, not of the NURBS algebra);
//  * the inward cross derivative / second cross derivative are compared with the prescribed field curves evaluated by
//    OpenNURBS at the paired parameter (the fields are inputs, the surface is evaluated by ON_NurbsSurface::Ev2Der);
//  * a G1 match of a planar arc with an axial field must leave the surface normal radial (a cylinder's normal), checked
//    from the surface normal against atan2 of the edge point - pure geometry;
//  * negative controls (a rational SURFACE read for its default field) must still fail with the surface bit-identical.
#include <algorithm>
#include <cmath>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::MatchContinuity;
using dino8::kernel::MatchEdgeReport;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

constexpr int kSamples = 33;

struct EdgeD {
  ON_3dPoint p;
  ON_3dVector inward;  // first cross derivative pointing into the surface
  ON_3dVector cc;      // second cross derivative
  ON_3dVector normal;  // unit surface normal
};

EdgeD EdgeAt(const NurbsSurface& s, int fixed_direction, bool at_min, double frac) {
  const ON_NurbsSurface& r = s.raw();
  const double at = at_min ? r.Domain(fixed_direction).Min() : r.Domain(fixed_direction).Max();
  const double e = r.Domain(1 - fixed_direction).ParameterAt(frac);
  EdgeD out;
  ON_3dVector du, dv, duu, duv, dvv;
  if (fixed_direction == 0) r.Ev2Der(at, e, out.p, du, dv, duu, duv, dvv);
  else r.Ev2Der(e, at, out.p, du, dv, duu, duv, dvv);
  out.inward = fixed_direction == 0 ? du : dv;
  if (!at_min) out.inward = -out.inward;
  out.cc = fixed_direction == 0 ? duu : dvv;
  out.normal = ON_CrossProduct(du, dv);
  out.normal.Unitize();
  return out;
}

double Paired(const ON_NurbsCurve& c, double frac, bool reversed) { return c.Domain().ParameterAt(reversed ? 1.0 - frac : frac); }

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

// Exact quarter circle of radius r about `c` in the plane z = c.z, from (c.x + r, c.y) to (c.x, c.y + r): the standard
// rational quadratic Bezier with the middle weight sqrt(2)/2. Domain [0, 1].
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

// Full circle (ON's own rational degree-2 form: 9 CVs, interior double knots) of radius r about `c` in the plane z = c.z.
NurbsCurve FullCircle(const ON_3dPoint& c, double r) {
  ON_Circle circle(ON_Plane(c, ON_3dVector::ZAxis), r);
  NurbsCurve out;
  circle.GetNurbForm(out.raw());
  return out;
}

// A deliberately non-flat bicubic net (CV(u = i, v = j)): the v = min edge is the line y = 0 running with u.
NurbsSurface Bicubic(int nu, int nv) {
  std::vector<Point3d> grid;
  for (int j = 0; j < nv; ++j)
    for (int i = 0; i < nu; ++i) grid.push_back(Point3d(0.7 * i, 0.9 * j, std::sin(1.7 * i) * std::cos(1.3 * j) + 0.3 * i * j));
  return NurbsSurface::FromControlGrid(grid, nu, nv, 3, 3);
}

NurbsCurve Field(const std::vector<Point3d>& cps, int degree, const NurbsCurve& like) {
  NurbsCurve f = NurbsCurve::FromControlPoints(cps, degree);
  f.raw().SetDomain(like.raw().Domain().Min(), like.raw().Domain().Max());
  return f;
}

// Max over the samples of |edge point - circle|: distance of the xy position from the centre minus r, and of z from the plane.
double CircleError(const NurbsSurface& s, int fd, bool at_min, const ON_3dPoint& c, double r) {
  double worst = 0.0;
  for (int k = 0; k < kSamples; ++k) {
    const EdgeD e = EdgeAt(s, fd, at_min, k / (kSamples - 1.0));
    worst = std::max({worst, std::abs(std::hypot(e.p.x - c.x, e.p.y - c.y) - r), std::abs(e.p.z - c.z)});
  }
  return worst;
}

}  // namespace

D8_EXTRA_TEST(MatchEdgeToCurveRationalArcTangentPrescribedField) {
  using d8test::Check;
  const ON_3dPoint centre(0.5, -2.0, 0.25);
  const double radius = 2.0;
  const NurbsCurve arc = QuarterArc(centre, radius);
  {  // the fixture itself: the arc really is the circle (so the oracle below means something)
    double worst = 0.0;
    for (int k = 0; k <= 20; ++k) {
      const ON_3dPoint p = arc.raw().PointAt(arc.raw().Domain().ParameterAt(k / 20.0));
      worst = std::max(worst, std::abs(std::hypot(p.x - centre.x, p.y - centre.y) - radius));
    }
    Check(arc.raw().IsRational() && worst < 1e-14, "fixture: the quarter arc is rational and lies on the circle (1e-14)");
  }

  const NurbsSurface original = Bicubic(5, 4);
  std::vector<EdgeD> far_before;
  for (int k = 0; k < kSamples; ++k) far_before.push_back(EdgeAt(original, 1, false, k / (kSamples - 1.0)));
  auto far_edge_untouched = [&](const NurbsSurface& s, const char* what) {
    double pos = 0.0;
    for (int k = 0; k < kSamples; ++k) pos = std::max(pos, EdgeAt(s, 1, false, k / (kSamples - 1.0)).p.DistanceTo(far_before[static_cast<size_t>(k)].p));
    Check(pos < 1e-12, what);
  };

  // --- G1 with a constant axial field: the surface must leave the arc exactly like a cylinder (radial normal) ---
  {
    NurbsSurface s = original;
    const NurbsCurve d = Field({Point3d(0, 0, 1.5), Point3d(0, 0, 1.5)}, 1, arc);
    MatchEdgeReport rep;
    const Result r = s.MatchEdgeToCurve(1, true, arc, MatchContinuity::Tangent, &d, nullptr, &rep);
    Check(r == Result::Ok, "rational arc, G1, axial field: MatchEdgeToCurve returns Ok");
    if (r == Result::Ok) {
      double tan = 0.0, radial = 0.0, off = 0.0;
      for (int k = 0; k < kSamples; ++k) {
        const EdgeD e = EdgeAt(s, 1, true, k / (kSamples - 1.0));
        tan = std::max(tan, (e.inward - ON_3dVector(0, 0, 1.5)).Length());
        const double theta = std::atan2(e.p.y - centre.y, e.p.x - centre.x);
        const ON_3dVector rad(std::cos(theta), std::sin(theta), 0.0);
        radial = std::max(radial, 1.0 - std::abs(ON_DotProduct(e.normal, rad)));  // |n . radial| = 1 for a cylinder normal
        off = std::max(off, std::abs(e.normal.z));
      }
      Check(CircleError(s, 1, true, centre, radius) < 1e-12, "rational arc, G1, axial field: the edge is the circle (radius and plane, 1e-12)");
      Check(tan < 1e-9, "rational arc, G1, axial field: the inward cross derivative IS the prescribed (0,0,1.5) (1e-9)");
      Check(radial < 1e-9 && off < 1e-9, "rational arc, G1, axial field: the surface normal along the edge is the cylinder's radial normal (1e-9)");
      Check(rep.max_position_error < 1e-12 && rep.max_tangent_error < 1e-9, "rational arc, G1, axial field: the report's own residuals agree");
      far_edge_untouched(s, "rational arc, G1, axial field: the far edge never moved");
    }
  }

  // --- G1 with a varying (quadratic) field: the weight function is not constant, so the field times the weight is a real product ---
  {
    NurbsSurface s = original;
    const NurbsCurve d = Field({Point3d(0.4, -0.2, 1.1), Point3d(-0.3, 0.5, 0.7), Point3d(0.2, 0.1, 1.6)}, 2, arc);
    MatchEdgeReport rep;
    const Result r = s.MatchEdgeToCurve(1, true, arc, MatchContinuity::Tangent, &d, nullptr, &rep);
    Check(r == Result::Ok, "rational arc, G1, varying field: MatchEdgeToCurve returns Ok");
    if (r == Result::Ok) {
      double pos = 0.0, tan = 0.0;
      for (int k = 0; k < kSamples; ++k) {
        const double f = k / (kSamples - 1.0);
        const EdgeD e = EdgeAt(s, 1, true, f);
        const double t = Paired(arc.raw(), f, rep.target_edge_reversed);
        pos = std::max(pos, e.p.DistanceTo(arc.raw().PointAt(t)));
        tan = std::max(tan, (e.inward - ON_3dVector(d.raw().PointAt(t))).Length());
      }
      Check(pos < 1e-12, "rational arc, G1, varying field: the edge IS the target arc at the paired parameter (1e-12)");
      Check(tan < 1e-9, "rational arc, G1, varying field: the inward cross derivative IS the prescribed field at every sample (1e-9)");
      Check(CircleError(s, 1, true, centre, radius) < 1e-12, "rational arc, G1, varying field: the edge lies on the circle (1e-12)");
      far_edge_untouched(s, "rational arc, G1, varying field: the far edge never moved");
    }
  }

  // --- Orientation: the same arc handed over in both directions must give the same edge, the field following its target ---
  {
    NurbsCurve flipped = arc;
    flipped.raw().Reverse();
    flipped.raw().SetDomain(arc.raw().Domain().Min(), arc.raw().Domain().Max());  // Reverse() mirrors the domain to [-1, 0]
    const NurbsCurve d = Field({Point3d(0.4, -0.2, 1.1), Point3d(-0.3, 0.5, 0.7), Point3d(0.2, 0.1, 1.6)}, 2, arc);
    const NurbsCurve d_flipped = Field({Point3d(0.2, 0.1, 1.6), Point3d(-0.3, 0.5, 0.7), Point3d(0.4, -0.2, 1.1)}, 2, arc);  // d reversed
    NurbsSurface a = original, b = original;
    MatchEdgeReport ra, rb;
    const Result r1 = a.MatchEdgeToCurve(1, true, arc, MatchContinuity::Tangent, &d, nullptr, &ra);
    const Result r2 = b.MatchEdgeToCurve(1, true, flipped, MatchContinuity::Tangent, &d_flipped, nullptr, &rb);
    Check(r1 == Result::Ok && r2 == Result::Ok, "rational arc, G1: both orientations of the same arc return Ok");
    Check(ra.target_edge_reversed != rb.target_edge_reversed, "rational arc, G1: exactly one of the two orientations needed the reversal (both branches exercised)");
    if (r1 == Result::Ok && r2 == Result::Ok) {
      double dp = 0.0, dd = 0.0;
      for (int k = 0; k < kSamples; ++k) {
        const EdgeD ea = EdgeAt(a, 1, true, k / (kSamples - 1.0)), eb = EdgeAt(b, 1, true, k / (kSamples - 1.0));
        dp = std::max(dp, ea.p.DistanceTo(eb.p));
        dd = std::max(dd, (ea.inward - eb.inward).Length());
      }
      Check(dp < 1e-12 && dd < 1e-9, "rational arc, G1: the two orientations give the same edge and the same inward derivative (1e-12 / 1e-9)");
    }
  }

  // --- G1 with the surface's own cross derivative preserved (no field passed): the default-field path with a rational target ---
  {
    NurbsSurface s = original;
    std::vector<EdgeD> before;
    for (int k = 0; k < kSamples; ++k) before.push_back(EdgeAt(original, 1, true, k / (kSamples - 1.0)));
    MatchEdgeReport rep;
    const Result r = s.MatchEdgeToCurve(1, true, arc, MatchContinuity::Tangent, nullptr, nullptr, &rep);
    Check(r == Result::Ok, "rational arc, G1, preserved field: MatchEdgeToCurve returns Ok");
    if (r == Result::Ok) {
      double tan = 0.0, moved = 0.0;
      for (int k = 0; k < kSamples; ++k) {
        const EdgeD e = EdgeAt(s, 1, true, k / (kSamples - 1.0));
        tan = std::max(tan, (e.inward - before[static_cast<size_t>(k)].inward).Length());
        moved = std::max(moved, e.p.DistanceTo(before[static_cast<size_t>(k)].p));
      }
      Check(tan < 1e-9, "rational arc, G1, preserved field: the inward cross derivative is exactly the pre-edit one (1e-9)");
      Check(moved > 0.5 && CircleError(s, 1, true, centre, radius) < 1e-12, "rational arc, G1, preserved field: the edge really moved onto the circle");
      far_edge_untouched(s, "rational arc, G1, preserved field: the far edge never moved");
    }
  }
}

D8_EXTRA_TEST(MatchEdgeToCurveRationalCircleCurvaturePrescribedFields) {
  using d8test::Check;
  const ON_3dPoint centre(-1.0, 0.5, 0.0);
  const double radius = 1.5;
  const NurbsCurve circle = FullCircle(centre, radius);  // 9 CVs, interior double knots: the multi-span product case
  Check(circle.raw().IsRational() && circle.raw().CVCount() == 9, "fixture: the full circle is a rational 9-CV degree-2 curve");

  const NurbsSurface original = Bicubic(6, 5);
  std::vector<EdgeD> far_before;
  for (int k = 0; k < kSamples; ++k) far_before.push_back(EdgeAt(original, 1, false, k / (kSamples - 1.0)));

  NurbsSurface s = original;
  const NurbsCurve d = Field({Point3d(0.3, 0.1, 1.2), Point3d(-0.2, 0.4, 0.8), Point3d(0.5, -0.3, 1.0), Point3d(0.1, 0.2, 1.4)}, 3, circle);
  const NurbsCurve e2 = Field({Point3d(0.4, 0.1, -0.5), Point3d(-0.3, 0.6, 0.2), Point3d(0.2, -0.4, 0.7)}, 2, circle);
  MatchEdgeReport rep;
  const Result r = s.MatchEdgeToCurve(1, true, circle, MatchContinuity::Curvature, &d, &e2, &rep);
  Check(r == Result::Ok, "rational circle, G2, prescribed D and E: MatchEdgeToCurve returns Ok");
  if (r == Result::Ok) {
    double pos = 0.0, tan = 0.0, cur = 0.0, far = 0.0;
    for (int k = 0; k < kSamples; ++k) {
      const double f = k / (kSamples - 1.0);
      const EdgeD e = EdgeAt(s, 1, true, f);
      const double t = Paired(circle.raw(), f, rep.target_edge_reversed);
      pos = std::max(pos, e.p.DistanceTo(circle.raw().PointAt(t)));
      tan = std::max(tan, (e.inward - ON_3dVector(d.raw().PointAt(t))).Length());
      cur = std::max(cur, (e.cc - ON_3dVector(e2.raw().PointAt(t))).Length());
      far = std::max(far, EdgeAt(s, 1, false, f).p.DistanceTo(far_before[static_cast<size_t>(k)].p));
    }
    Check(pos < 1e-12, "rational circle, G2: the edge IS the target circle at the paired parameter (1e-12)");
    Check(CircleError(s, 1, true, centre, radius) < 1e-12, "rational circle, G2: every edge sample is at radius 1.5 from the centre in the plane z = 0 (1e-12)");
    Check(tan < 1e-9, "rational circle, G2: the inward cross derivative IS the prescribed D across all four spans (1e-9)");
    Check(cur < 1e-9, "rational circle, G2: the second cross derivative IS the prescribed E across all four spans (1e-9)");
    Check(rep.max_curvature_error < 1e-9 && rep.scale == 1.0, "rational circle, G2: the report's curvature residual agrees and the scale is exactly 1");
    Check(far < 1e-12, "rational circle, G2: the far edge never moved");
  }
}

D8_EXTRA_TEST(MatchEdgeToCurveRationalSurfaceExplicitFieldsAndFailClosed) {
  using d8test::Check;
  // A rational surface: quarter cylinder of radius 2 about the z axis, height 1.2. u runs along the base arc (rational
  // quadratic), v along the axis (degree 1). Its v = min edge is the base arc.
  ON_NurbsSurface cyl;
  cyl.Create(3, true, 3, 2, 3, 2);
  cyl.SetKnot(0, 0, 0.0);
  cyl.SetKnot(0, 1, 0.0);
  cyl.SetKnot(0, 2, 1.0);
  cyl.SetKnot(0, 3, 1.0);
  cyl.SetKnot(1, 0, 0.0);
  cyl.SetKnot(1, 1, 1.0);
  const double w = std::sqrt(0.5), R = 2.0, H = 1.2;
  const double bx[3] = {R, R, 0.0}, by[3] = {0.0, R, R}, bw[3] = {1.0, w, 1.0};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 2; ++j) cyl.SetCV(i, j, ON_4dPoint(bw[i] * bx[i], bw[i] * by[i], bw[i] * H * j, bw[i]));
  NurbsSurface original;
  original.raw() = cyl;
  Check(original.IsRational(), "fixture: the cylinder patch is rational");

  // Target: a straight, polynomial line far from the cylinder; explicit G1 field with a x component.
  const NurbsCurve line = NurbsCurve::FromControlPoints({Point3d(3, 0, 0), Point3d(3, 2.5, 0.4)}, 1);
  {
    NurbsSurface s = original;
    const NurbsCurve d = Field({Point3d(-0.6, 0.2, 0.9), Point3d(-0.4, -0.3, 1.1)}, 1, line);
    MatchEdgeReport rep;
    const Result r = s.MatchEdgeToCurve(1, true, line, MatchContinuity::Tangent, &d, nullptr, &rep);
    Check(r == Result::Ok, "rational surface, explicit G1 field, polynomial target: MatchEdgeToCurve returns Ok");
    if (r == Result::Ok) {
      double pos = 0.0, tan = 0.0, far = 0.0;
      for (int k = 0; k < kSamples; ++k) {
        const double f = k / (kSamples - 1.0);
        const EdgeD e = EdgeAt(s, 1, true, f);
        const double t = Paired(line.raw(), f, rep.target_edge_reversed);
        pos = std::max(pos, e.p.DistanceTo(line.raw().PointAt(t)));
        tan = std::max(tan, (e.inward - ON_3dVector(d.raw().PointAt(t))).Length());
        far = std::max(far, EdgeAt(s, 1, false, f).p.DistanceTo(EdgeAt(original, 1, false, f).p));
      }
      Check(pos < 1e-12, "rational surface, explicit G1 field: the v = min edge IS the target line (1e-12)");
      Check(tan < 1e-9, "rational surface, explicit G1 field: the inward cross derivative IS the prescribed field (1e-9)");
      Check(far < 1e-12, "rational surface, explicit G1 field: the far (v = max) edge never moved (1e-12)");
    }
  }
  // Rational surface AND rational target with explicit field.
  {
    NurbsSurface s = original;
    const ON_3dPoint centre(0.0, 0.0, -1.0);
    const NurbsCurve arc = QuarterArc(centre, 2.0);
    const NurbsCurve d = Field({Point3d(0.0, 0.0, 1.0), Point3d(0.0, 0.0, 1.0)}, 1, arc);
    MatchEdgeReport rep;
    const Result r = s.MatchEdgeToCurve(1, true, arc, MatchContinuity::Tangent, &d, nullptr, &rep);
    Check(r == Result::Ok, "rational surface, explicit G1 field, rational target: MatchEdgeToCurve returns Ok");
    if (r == Result::Ok) {
      double tan = 0.0;
      for (int k = 0; k < kSamples; ++k) tan = std::max(tan, (EdgeAt(s, 1, true, k / (kSamples - 1.0)).inward - ON_3dVector(0, 0, 1.0)).Length());
      Check(CircleError(s, 1, true, centre, 2.0) < 1e-12 && tan < 1e-9, "rational surface + rational target: the edge is the circle and the inward derivative is (0,0,1) (1e-9)");
    }
  }
  // Negative controls: a rational surface with NO explicit field would need its own pre-edit field as a rational function -
  // still refused, surface bit-identical.
  {
    NurbsSurface s = original;
    const Result r = s.MatchEdgeToCurve(1, true, line, MatchContinuity::Tangent, nullptr, nullptr, nullptr);
    Check(r == Result::Failed && SameCVs(s, original), "rational surface, default (preserved) field: still fails closed, surface bit-identical");
  }
  // Rational target + a rational (non-polynomial) field is not a valid field: refused, surface untouched.
  {
    NurbsSurface s = NurbsSurface(Bicubic(5, 4));
    const NurbsSurface before = s;
    const NurbsCurve arc = QuarterArc(ON_3dPoint(0, 0, 0), 2.0);
    const NurbsCurve rational_field = QuarterArc(ON_3dPoint(0, 0, 0), 1.0);  // domain [0,1] like the arc, but rational
    const Result r = s.MatchEdgeToCurve(1, true, arc, MatchContinuity::Tangent, &rational_field, nullptr, nullptr);
    Check(r == Result::Failed && SameCVs(s, before), "rational target with a rational field curve: refused, surface bit-identical");
  }
}
