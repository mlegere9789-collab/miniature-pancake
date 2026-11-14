// NurbsSurface::ChangeSeam (the kernel half of the app's SrfSeam) had no geometric test at all - the app section only checked a message
// string. Independent oracles, none of them read back from what ChangeSeam builds:
//  * the ORIGINAL surface: a seam move changes where the wraparound parameter starts, not the shape or the parameterization, so the moved
//    surface at parameter u in its new domain [t, t + L] must be the original at u wrapped into [a, a + L);
//  * closed forms of the fixtures: every point of the sphere is at radius 5 from the origin, every point of the cylinder at distance 3 from
//    its axis, and the seam curve of the moved surface is the original's isocurve at the requested parameter;
//  * negative controls: an open plane, a parameter outside the domain, and a call that must change nothing leave the surface bit-identical.
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

void Check(bool ok, const std::string& what) { d8test::Check(ok, what.c_str()); }

bool SameNet(const NurbsSurface& a, const NurbsSurface& b) {
  if (a.CVCountU() != b.CVCountU() || a.CVCountV() != b.CVCountV() || a.IsRational() != b.IsRational()) return false;
  for (int i = 0; i < a.CVCountU(); ++i)
    for (int j = 0; j < a.CVCountV(); ++j) {
      ON_4dPoint pa, pb;
      a.raw().GetCV(i, j, pa);
      b.raw().GetCV(i, j, pb);
      if (pa.x != pb.x || pa.y != pb.y || pa.z != pb.z || pa.w != pb.w) return false;
    }
  for (int d = 0; d < 2; ++d) {
    if (a.raw().Domain(d).Min() != b.raw().Domain(d).Min() || a.raw().Domain(d).Max() != b.raw().Domain(d).Max()) return false;
  }
  return true;
}

// Largest distance between `moved` at parameter u (new domain) and `orig` at u wrapped into orig's domain, over a grid.
double WrapDeviation(const NurbsSurface& moved, const NurbsSurface& orig, int dir, int nu = 33, int nv = 17) {
  const ON_Interval od = orig.raw().Domain(dir), md = moved.raw().Domain(dir);
  double worst = 0.0;
  for (int i = 0; i < nu; ++i) {
    for (int j = 0; j < nv; ++j) {
      const double f = i / (nu - 1.0), g = j / (nv - 1.0);
      double u = md.ParameterAt(f);
      double w = od.Min() + std::fmod(u - od.Min(), od.Length());
      if (w < od.Min()) w += od.Length();
      ON_3dPoint pm, po;
      if (dir == 0) {
        pm = moved.raw().PointAt(u, moved.raw().Domain(1).ParameterAt(g));
        po = orig.raw().PointAt(w, orig.raw().Domain(1).ParameterAt(g));
      } else {
        pm = moved.raw().PointAt(moved.raw().Domain(0).ParameterAt(g), u);
        po = orig.raw().PointAt(orig.raw().Domain(0).ParameterAt(g), w);
      }
      worst = std::max(worst, pm.DistanceTo(po));
    }
  }
  return worst;
}

NurbsSurface Sphere(double r) {
  NurbsSurface s;
  ON_Sphere(ON_3dPoint(0, 0, 0), r).GetNurbForm(s.raw());
  return s;
}

NurbsSurface Cylinder(double r, double h) {
  NurbsSurface s;
  ON_Cylinder cyl(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector::ZAxis), r));
  cyl.height[0] = 0.0;
  cyl.height[1] = h;
  cyl.GetNurbForm(s.raw());
  return s;
}

}  // namespace

D8_EXTRA_TEST(SurfaceChangeSeamSphereAndCylinder) {
  struct Fix { const char* name; NurbsSurface s; };
  Fix fixes[] = {{"sphere r5", Sphere(5.0)}, {"cylinder r3", Cylinder(3.0, 4.0)}};
  for (Fix& f : fixes) {
    const NurbsSurface& orig = f.s;
    const std::string n = f.name;
    Check(orig.IsClosed(0) && orig.IsRational(), n + ": fixture is a rational surface closed in U");
    const ON_Interval d0 = orig.raw().Domain(0);
    for (double frac : {0.17, 0.5, 0.83}) {
      const double t = d0.ParameterAt(frac);
      NurbsSurface m = orig;
      const Result r = m.ChangeSeam(0, t);
      const std::string tag = n + " seam -> t = " + std::to_string(frac) + " of the domain";
      Check(r == Result::Ok, tag + ": ChangeSeam returns Ok");
      if (r != Result::Ok) continue;
      Check(std::abs(m.raw().Domain(0).Min() - t) < 1e-12 && std::abs(m.raw().Domain(0).Length() - d0.Length()) < 1e-12, tag + ": the new domain is [t, t + L]");
      Check(m.raw().Domain(1).Min() == orig.raw().Domain(1).Min() && m.raw().Domain(1).Max() == orig.raw().Domain(1).Max(), tag + ": the other direction's domain is unchanged");
      Check(m.IsClosed(0), tag + ": still closed in U");
      Check(WrapDeviation(m, orig, 0) < 1e-9, tag + ": the moved surface IS the original at the wrapped parameter (1e-9)");
      double off = 0.0, seam_gap = 0.0, closure = 0.0;
      for (int i = 0; i <= 32; ++i) {
        for (int j = 0; j <= 16; ++j) {
          const ON_3dPoint p = m.raw().PointAt(m.raw().Domain(0).ParameterAt(i / 32.0), m.raw().Domain(1).ParameterAt(j / 16.0));
          if (n[0] == 's') off = std::max(off, std::abs(ON_3dVector(p).Length() - 5.0));
          else off = std::max(off, std::abs(std::hypot(p.x, p.y) - 3.0));
        }
      }
      for (int j = 0; j <= 16; ++j) {
        const double v = m.raw().Domain(1).ParameterAt(j / 16.0);
        seam_gap = std::max(seam_gap, m.raw().PointAt(t, v).DistanceTo(orig.raw().PointAt(t, v)));
        closure = std::max(closure, m.raw().PointAt(m.raw().Domain(0).Min(), v).DistanceTo(m.raw().PointAt(m.raw().Domain(0).Max(), v)));
      }
      Check(off < 1e-9, tag + (n[0] == 's' ? ": every sample is at radius 5 from the origin (closed form, 1e-9)" : ": every sample is 3 from the axis (closed form, 1e-9)"));
      Check(seam_gap < 1e-9, tag + ": the new seam curve is the original's isocurve at t (1e-9)");
      Check(closure < 1e-9, tag + ": the new seam closes up (start edge = end edge, 1e-9)");
      // Round trip: put the seam back where it was.
      // (the old seam parameter a lies outside the moved domain [t, t + L], its copy a + L inside it)
      NurbsSurface back = m;
      const double a_again = d0.Min() + d0.Length();
      const Result rb = back.ChangeSeam(0, a_again);
      Check(rb == Result::Ok && WrapDeviation(back, orig, 0) < 1e-9 && std::abs(back.raw().Domain(0).Min() - a_again) < 1e-12,
            tag + ": moving the seam back (to a + L) restores the original geometry at the wrapped parameter, domain [a + L, a + 2L] (1e-9)");
      NurbsSurface stale = m;
      Check(stale.ChangeSeam(0, d0.Min()) == Result::Failed && SameNet(stale, m), tag + ": the old seam parameter a is outside the moved domain [t, t + L]: refused, bit-identical");
    }
    // Moving the seam to where it already is must not change the shape.
    {
      NurbsSurface m = orig;
      const Result r = m.ChangeSeam(0, d0.Min());
      Check(r == Result::NoOpAlreadySatisfied && SameNet(m, orig), n + ": seam -> current seam is NoOpAlreadySatisfied and bit-identical");
    }
    // Refusals leave the surface bit-identical.
    {
      NurbsSurface m = orig;
      Check(m.ChangeSeam(0, d0.Max() + 0.5) == Result::Failed && SameNet(m, orig), n + ": a parameter outside the domain is refused, surface bit-identical");
      Check(m.ChangeSeam(1, orig.raw().Domain(1).ParameterAt(0.5)) == Result::Failed && SameNet(m, orig), n + ": the open V direction is refused, surface bit-identical");
      bool threw = false;
      try { m.ChangeSeam(2, 0.0); } catch (const std::invalid_argument&) { threw = true; }
      Check(threw && SameNet(m, orig), n + ": a direction outside 0/1 throws, surface bit-identical");
    }
  }
}

D8_EXTRA_TEST(SurfaceChangeSeamPeriodicTorusBothDirections) {
  // A periodic bicubic interpolant of a torus grid (R = 4, r = 1), closed in both directions (non-rational, periodic knots).
  std::vector<Point3d> grid;
  const int nu = 16, nv = 8;
  for (int i = 0; i < nu; ++i)
    for (int j = 0; j < nv; ++j) {
      const double a = 2.0 * ON_PI * i / nu, b = 2.0 * ON_PI * j / nv;
      grid.push_back(Point3d((4.0 + std::cos(b)) * std::cos(a), (4.0 + std::cos(b)) * std::sin(a), std::sin(b)));
    }
  NurbsSurface orig;
  const Result made = NurbsSurface::InterpolateThroughClosedGrid(grid, nu, nv, 3, 3, true, true, orig);
  Check(made == Result::Ok && orig.IsClosed(0) && orig.IsClosed(1), "fixture: the periodic torus interpolant is closed in U and V");
  if (made != Result::Ok) return;
  for (int dir = 0; dir < 2; ++dir) {
    const ON_Interval d = orig.raw().Domain(dir);
    for (double frac : {0.3, 0.7}) {
      const double t = d.ParameterAt(frac);
      NurbsSurface m = orig;
      const Result r = m.ChangeSeam(dir, t);
      const std::string tag = std::string("torus, direction ") + (dir == 0 ? "U" : "V") + ", seam -> " + std::to_string(frac);
      Check(r == Result::Ok, tag + ": ChangeSeam returns Ok");
      if (r != Result::Ok) continue;
      Check(std::abs(m.raw().Domain(dir).Min() - t) < 1e-12 && std::abs(m.raw().Domain(dir).Length() - d.Length()) < 1e-12, tag + ": the new domain is [t, t + L]");
      Check(WrapDeviation(m, orig, dir, 33, 33) < 1e-9, tag + ": the moved surface IS the original at the wrapped parameter (1e-9)");
      Check(m.IsClosed(dir), tag + ": still closed");
    }
  }
}

D8_EXTRA_TEST(SurfaceChangeSeamOpenSurfaceRefused) {
  const NurbsSurface plane = NurbsSurface::FromControlGrid({Point3d(0, 0, 0), Point3d(0, 3, 0), Point3d(4, 0, 0), Point3d(4, 3, 0)}, 2, 2, 1, 1);
  NurbsSurface m = plane;
  Check(m.ChangeSeam(0, 0.5) == Result::Failed && SameNet(m, plane), "an open plane has no seam to move: refused, bit-identical");
  Check(m.ChangeSeam(1, 0.5) == Result::Failed && SameNet(m, plane), "an open plane has no seam to move in V either: refused, bit-identical");
}

D8_EXTRA_TEST(SurfaceChangeSeamEdgeCases) {
  // Awkward parameters on the rational sphere: exactly at an interior knot, at the domain end, a hair from the old seam, and in the V direction
  // (a transposed sphere is closed in V). The oracle is again the original at the wrapped parameter (1e-9) plus the sphere's radius.
  NurbsSurface orig = Sphere(5.0);
  const ON_Interval d = orig.raw().Domain(0);
  // an interior knot of the closed direction
  double knot = d.Min();
  for (int k = 0; k < orig.raw().KnotCount(0); ++k) {
    if (orig.raw().Knot(0, k) > d.Min() + 1e-9 && orig.raw().Knot(0, k) < d.Max() - 1e-9) { knot = orig.raw().Knot(0, k); break; }
  }
  Check(knot > d.Min(), "fixture: the sphere has an interior knot in the closed direction");
  struct Case { const char* what; double t; };
  const Case cases[] = {{"an interior knot", knot}, {"the domain end", d.Max()}, {"1e-9 above the old seam", d.Min() + 1e-9}, {"1e-9 below the domain end", d.Max() - 1e-9}};
  for (const Case& c : cases) {
    NurbsSurface m = orig;
    const Result r = m.ChangeSeam(0, c.t);
    const std::string tag = std::string("sphere, seam -> ") + c.what;
    // At (or a hair from) either end of the domain the requested seam IS the existing one: NoOpAlreadySatisfied, surface untouched.
    // (OpenNURBS' own call refuses a t 1e-9 from an end; the kernel wrapper used to pass that failure on.) An interior knot is a real move.
    const bool at_seam = c.t <= d.Min() + 1e-8 || c.t >= d.Max() - 1e-8;
    Check(r == (at_seam ? Result::NoOpAlreadySatisfied : Result::Ok), tag + (at_seam ? ": reported as already satisfied (NoOp)" : ": accepted (Ok)"));
    if (at_seam) Check(SameNet(m, orig), tag + ": and the surface is bit-identical");
    if (r == Result::Failed) continue;
    double radius = 0.0;
    for (int i = 0; i <= 32; ++i)
      for (int j = 0; j <= 16; ++j)
        radius = std::max(radius, std::abs(ON_3dVector(m.raw().PointAt(m.raw().Domain(0).ParameterAt(i / 32.0), m.raw().Domain(1).ParameterAt(j / 16.0))).Length() - 5.0));
    Check(WrapDeviation(m, orig, 0) < 1e-9 && radius < 1e-9 && std::abs(m.raw().Domain(0).Length() - d.Length()) < 1e-12, tag + ": same shape, parameterization and domain length (1e-9)");
  }
  // V direction of a transposed sphere
  NurbsSurface tr = orig;
  tr.raw().Transpose();
  Check(tr.IsClosed(1) && !tr.IsClosed(0), "fixture: the transposed sphere is closed in V only");
  const NurbsSurface tr_orig = tr;
  const double tv = tr.raw().Domain(1).ParameterAt(0.4);
  Check(tr.ChangeSeam(1, tv) == Result::Ok, "transposed sphere: ChangeSeam in V returns Ok");
  Check(std::abs(tr.raw().Domain(1).Min() - tv) < 1e-12 && WrapDeviation(tr, tr_orig, 1) < 1e-9, "transposed sphere: new V domain starts at t and the shape is the original at the wrapped parameter (1e-9)");
}
