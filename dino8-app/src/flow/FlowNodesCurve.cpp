// Curve node category.
#include <algorithm>
#include <cmath>

#include "commands/cmd_common.h"
#include "flow/FlowGraph.h"

namespace dino8::flow {

namespace {

PortDef In(std::string name, Kind k, Access acc = Access::Item, Value def = Value::Null(), bool optional = false) {
  PortDef p; p.name = name; p.nick = name; p.kind = k; p.access = acc; p.def = std::move(def); p.optional = optional; return p;
}
PortDef Out(std::string name, Kind k) { PortDef p; p.name = name; p.nick = name; p.kind = k; return p; }

kernel::NurbsCurve LineCurve(Point3d a, Point3d b) {
  ON_LineCurve lc(a, b);
  kernel::NurbsCurve c;
  ON_NurbsCurve nc;
  lc.GetNurbForm(nc);
  c.raw() = nc;
  return c;
}

kernel::NurbsCurve PolylineCurveOf(const std::vector<Point3d>& pts, bool closed) {
  ON_Polyline pl;
  for (const Point3d& p : pts) pl.Append(p);
  if (closed && pl.Count() > 0 && pl[0].DistanceTo(pl[pl.Count() - 1]) > 1e-9) pl.Append(pl[0]);
  ON_PolylineCurve pc(pl);
  kernel::NurbsCurve c;
  ON_NurbsCurve nc;
  pc.GetNurbForm(nc);
  c.raw() = nc;
  return c;
}

kernel::NurbsCurve CircleCurve(const Plane& pl, double r) {
  ON_Circle circ(pl.ToON(), r);
  ON_NurbsCurve nc;
  circ.GetNurbForm(nc);
  kernel::NurbsCurve c;
  c.raw() = nc;
  return c;
}

// A zero-length curve at one point (a degree-1 curve whose two control points coincide), so a zero radius still gives a
// curve whose length is 0 and whose evaluation is that point.
kernel::NurbsCurve PointCurve(Point3d p) { return kernel::NurbsCurve::FromControlPoints({p, p}, 1); }

kernel::NurbsCurve ArcCurve(const Plane& pl, double r, double a0, double a1) {
  ON_Arc arc(ON_Circle(pl.ToON(), r), ON_Interval(a0, a1));
  ON_NurbsCurve nc;
  arc.GetNurbForm(nc);
  kernel::NurbsCurve c;
  c.raw() = nc;
  return c;
}


// Gauss-Legendre nodes and weights on [-1, 1] (Newton iteration on the Legendre polynomial P_n).
void GaussLegendre(int n, std::vector<double>& x, std::vector<double>& w) {
  x.assign(static_cast<size_t>(n), 0.0);
  w.assign(static_cast<size_t>(n), 0.0);
  for (int i = 0; i < (n + 1) / 2; ++i) {
    double z = std::cos(ON_PI * (i + 0.75) / (n + 0.5)), pp = 1.0;
    for (int it = 0; it < 100; ++it) {
      double p1 = 1.0, p2 = 0.0;
      for (int j = 0; j < n; ++j) { const double p3 = p2; p2 = p1; p1 = ((2.0 * j + 1.0) * z * p2 - j * p3) / (j + 1.0); }
      pp = n * (z * p1 - p2) / (z * z - 1.0);
      const double z1 = z;
      z = z1 - p1 / pp;
      if (std::fabs(z - z1) < 1e-15) break;
    }
    x[static_cast<size_t>(i)] = -z; x[static_cast<size_t>(n - 1 - i)] = z;
    w[static_cast<size_t>(i)] = w[static_cast<size_t>(n - 1 - i)] = 2.0 / ((1.0 - z * z) * pp * pp);
  }
}

// Area and centroid of the region a closed planar curve bounds: Green's theorem, A = (1/2) closed-integral (u dv - v du),
// the first moments as (1/2) closed-integral u^2 dv and -(1/2) closed-integral v^2 du, integrated with a 12-point
// Gauss-Legendre rule on every knot span cut in 8 (exact to rounding for lines and circles). u, v are the coordinates in the
// curve's own plane. Returns false for an open or non-planar curve.
bool PlanarAreaCentroid(const kernel::NurbsCurve& crv, double& area, Point3d& centroid) {
  const ON_NurbsCurve& nc = crv.raw();
  if (!crv.IsClosed()) return false;
  ON_Plane pl;
  if (!nc.IsPlanar(&pl, 1e-6)) return false;
  std::vector<double> knots;
  const kernel::Interval dm = crv.Domain();
  for (int i = 0; i < nc.KnotCount(); ++i) { const double k = nc.Knot(i); if (k >= dm.min - 1e-14 && k <= dm.max + 1e-14) knots.push_back(k); }
  std::sort(knots.begin(), knots.end());
  knots.erase(std::unique(knots.begin(), knots.end(), [](double a, double b) { return std::fabs(a - b) < 1e-14; }), knots.end());
  if (knots.size() < 2) { knots = {dm.min, dm.max}; }
  std::vector<double> gx, gw;
  GaussLegendre(12, gx, gw);
  double a2 = 0, mu = 0, mv = 0;  // 2*area, int u dA, int v dA
  const int sub = 8;
  for (size_t s = 0; s + 1 < knots.size(); ++s) {
    for (int q = 0; q < sub; ++q) {
      const double t0 = knots[s] + (knots[s + 1] - knots[s]) * q / sub, t1 = knots[s] + (knots[s + 1] - knots[s]) * (q + 1) / sub;
      const double hm = 0.5 * (t0 + t1), hh = 0.5 * (t1 - t0);
      for (int g = 0; g < 12; ++g) {
        const double t = hm + hh * gx[static_cast<size_t>(g)];
        ON_3dPoint p; ON_3dVector d;
        if (!nc.Ev1Der(t, p, d)) continue;
        const ON_3dVector rel = p - pl.origin;
        const double u = ON_DotProduct(rel, pl.xaxis), v = ON_DotProduct(rel, pl.yaxis);
        const double du = ON_DotProduct(d, pl.xaxis), dv = ON_DotProduct(d, pl.yaxis);
        const double wt = gw[static_cast<size_t>(g)] * hh;
        a2 += wt * (u * dv - v * du);
        mu += wt * 0.5 * u * u * dv;
        mv += wt * -0.5 * v * v * du;
      }
    }
  }
  const double a = 0.5 * a2;
  if (std::fabs(a) < 1e-300) return false;
  area = std::fabs(a);
  centroid = pl.PointAt(mu / a, mv / a);
  return true;
}

void AddCurve() {
  Registry& r = Registry::Get();
  {
    NodeDef d; d.name = "Line"; d.nick = "Ln"; d.category = "Curve"; d.subcategory = "Primitive";
    d.inputs = {In("Start", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("End", Kind::Point, Access::Item, Value::Point(Point3d(10, 0, 0)))};
    d.outputs = {Out("Line", Kind::Curve)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Curve(LineCurve(c.Pt(0), c.Pt(1, Point3d(10, 0, 0))))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Polyline"; d.nick = "PL"; d.category = "Curve"; d.subcategory = "Primitive";
    d.inputs = {In("Vertices", Kind::Point, Access::List), In("Closed", Kind::Boolean, Access::Item, Value::Boolean(false))};
    d.outputs = {Out("Polyline", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      std::vector<Point3d> pts;
      for (const Value& v : c.List(0)) { Point3d p; if (v.AsPoint(p)) pts.push_back(p); }
      if (pts.size() < 2) { c.Fail("Polyline needs at least two vertices"); return; }
      c.Out(0, Value::Curve(PolylineCurveOf(pts, c.Bool(1))));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Circle"; d.nick = "Cir"; d.category = "Curve"; d.subcategory = "Primitive";
    d.inputs = {In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("Radius", Kind::Number, Access::Item, Value::Number(5))};
    d.outputs = {Out("Circle", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      // The radius is taken by its size (a negative one is the same circle); a radius of 0 is a point-sized curve of
      // length 0 at the plane origin, not a tiny circle.
      const Plane pl = c.PlaneIn(0);
      const double rad = std::fabs(c.Num(1, 5));
      if (rad == 0.0) { c.Out(0, Value::Curve(PointCurve(pl.origin))); return; }
      c.Out(0, Value::Curve(CircleCurve(pl, rad)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Circle CNR"; d.nick = "CirCNR"; d.category = "Curve"; d.subcategory = "Primitive";
    d.description = "Circle from center, normal, and radius.";
    d.inputs = {In("Center", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("Normal", Kind::Vector, Access::Item, Value::Vector(Vector3d(0, 0, 1))), In("Radius", Kind::Number, Access::Item, Value::Number(5))};
    d.outputs = {Out("Circle", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      ON_Plane pl(c.Pt(0), c.Vec(1, Vector3d(0, 0, 1)));
      c.Out(0, Value::Curve(CircleCurve(Plane::FromON(pl), std::max(1e-6, c.Num(2, 5)))));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Arc"; d.nick = "Arc"; d.category = "Curve"; d.subcategory = "Primitive";
    d.description = "An arc of a circle in a plane about the plane origin, from Start Angle to End Angle in RADIANS, counter-clockwise from the plane's X axis. The curve's parameter domain is that angle interval.";
    d.inputs = {In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("Radius", Kind::Number, Access::Item, Value::Number(5)),
                In("Start Angle", Kind::Number, Access::Item, Value::Number(0)), In("End Angle", Kind::Number, Access::Item, Value::Number(ON_PI / 2.0))};
    d.outputs = {Out("Arc", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      c.Out(0, Value::Curve(ArcCurve(c.PlaneIn(0), std::max(1e-6, c.Num(1, 5)), c.Num(2), c.Num(3, ON_PI / 2.0))));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Rectangle"; d.nick = "Rec"; d.category = "Curve"; d.subcategory = "Primitive";
    d.description = "A rectangle in a plane, centred on the plane origin: X Size along the plane's X axis, Y Size along its Y axis. The corners run counter-clockwise from (-X/2, -Y/2).";
    d.inputs = {In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("X Size", Kind::Number, Access::Item, Value::Number(10)), In("Y Size", Kind::Number, Access::Item, Value::Number(10))};
    d.outputs = {Out("Rectangle", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      Plane pl = c.PlaneIn(0);
      const double sx = c.Num(1, 10), sy = c.Num(2, 10);
      const double hx = 0.5 * sx, hy = 0.5 * sy;
      std::vector<Point3d> pts = {pl.At(-hx, -hy), pl.At(hx, -hy), pl.At(hx, hy), pl.At(-hx, hy)};
      c.Out(0, Value::Curve(PolylineCurveOf(pts, true)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Interpolate"; d.nick = "IntCrv"; d.category = "Curve"; d.subcategory = "Spline";
    d.inputs = {In("Vertices", Kind::Point, Access::List), In("Closed", Kind::Boolean, Access::Item, Value::Boolean(false))};
    d.outputs = {Out("Curve", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      std::vector<Point3d> pts;
      for (const Value& v : c.List(0)) { Point3d p; if (v.AsPoint(p)) pts.push_back(p); }
      if (pts.size() < 2) { c.Fail("Interpolate needs at least two points"); return; }
      if (c.Bool(1) && pts.front().DistanceTo(pts.back()) > 1e-9) pts.push_back(pts.front());
      ON_3dPointArray arr;
      for (const Point3d& p : pts) arr.Append(p);
      ON_NurbsCurve nc;
      const int degree = std::min<int>(3, static_cast<int>(pts.size()) - 1);
      if (arr.Count() >= 2 && nc.CreateClampedUniformNurbs(3, std::max(1, degree), arr.Count(), arr.Array()) != 0) {
        kernel::NurbsCurve out; out.raw() = nc;
        c.Out(0, Value::Curve(out));
        return;
      }
      c.Out(0, Value::Curve(PolylineCurveOf(pts, false)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "NURBS Curve"; d.nick = "NurbsCrv"; d.category = "Curve"; d.subcategory = "Spline";
    d.inputs = {In("Control Points", Kind::Point, Access::List), In("Degree", Kind::Integer, Access::Item, Value::Integer(3)), In("Closed", Kind::Boolean, Access::Item, Value::Boolean(false))};
    d.outputs = {Out("Curve", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      std::vector<Point3d> pts;
      for (const Value& v : c.List(0)) { Point3d p; if (v.AsPoint(p)) pts.push_back(p); }
      if (pts.size() < 2) { c.Fail("NURBS Curve needs at least two control points"); return; }
      const int deg = std::max(1, std::min(c.Int(1, 3), static_cast<int>(pts.size()) - 1));
      if (c.Bool(2) && pts.front().DistanceTo(pts.back()) > 1e-9) pts.push_back(pts.front());
      c.Out(0, Value::Curve(kernel::NurbsCurve::FromControlPoints(pts, deg)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Evaluate Curve"; d.nick = "Eval"; d.category = "Curve"; d.subcategory = "Analysis";
    d.description = "The point and unit tangent at the curve's own parameter t, clamped to the curve's domain (the domain midpoint when t is not given); Parameter is the value actually evaluated.";
    d.inputs = {In("Curve", Kind::Curve), In("t", Kind::Number, Access::Item, Value::Null(), true)};
    d.outputs = {Out("Point", Kind::Point), Out("Tangent", Kind::Vector), Out("Parameter", Kind::Number)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const auto& crv = *c.In(0).curve;
      const kernel::Interval dm = crv.Domain();
      const double t = c.Has(1) ? std::clamp(c.Num(1), dm.min, dm.max) : 0.5 * (dm.min + dm.max);
      c.Out(0, Value::Point(crv.PointAt(t)));
      c.Out(1, Value::Vector(crv.TangentAt(t)));
      c.Out(2, Value::Number(t));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Curve Length"; d.nick = "Len"; d.category = "Curve"; d.subcategory = "Analysis";
    d.inputs = {In("Curve", Kind::Curve)};
    d.outputs = {Out("Length", Kind::Number)};
    d.eval = [](EvalContext& c) {
      // Adaptive Gauss-Legendre arc length to 1e-10 (the kernel's LengthToTolerance), not the 400-chord polyline sum, which
      // is short by 6e-5 on a unit circle; a point-sized curve has length 0.
      const kernel::NurbsCurve* crv = c.In(0).curve.get();
      c.Out(0, Value::Number(crv ? crv->LengthToTolerance(1e-10) : 0.0));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Divide Curve"; d.nick = "Divide"; d.category = "Curve"; d.subcategory = "Division";
    d.inputs = {In("Curve", Kind::Curve), In("Count", Kind::Integer, Access::Item, Value::Integer(10))};
    d.outputs = {Out("Points", Kind::Point), Out("Tangents", Kind::Vector), Out("Parameters", Kind::Number)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const auto& crv = *c.In(0).curve;
      // Clamped, not just floored: Count can come straight from a .dflow
      // file's JSON, and DivideByCount(n) both reserves a vector of n+1
      // doubles and calls ParameterAtArcLength() (itself O(samples)) n times,
      // so an unbounded untrusted-file count would force a multi-GB
      // allocation and/or a hang here, the same bug class already fixed for
      // RunSolverNode's gene_count/Population/Generations. Kept to the same
      // 10000 ceiling as Population/Generations there (not Range/Series'
      // 100000): each of the n points here costs an O(samples)
      // ParameterAtArcLength() call, not a single push_back.
      const int n = std::clamp(c.Int(1, 10), 1, 10000);
      // Equal ARC LENGTH to 1e-10 (the 1000-chord polyline overload puts the points 1e-5 off on a circle). A closed curve
      // gives Count points, its last division point being the first again; an open one Count + 1. A curve of length 0
      // repeats its start point.
      std::vector<double> ts;
      if (crv.LengthToTolerance(1e-10) > 1e-12) {
        ts = crv.DivideByCount(n, 1e-10);
        if (crv.IsClosed() && ts.size() > 1) ts.pop_back();
      } else {
        ts.assign(static_cast<size_t>(n) + 1, crv.Domain().min);
      }
      std::vector<Value> pts, tans, pars;
      for (double t : ts) { pts.push_back(Value::Point(crv.PointAt(t))); tans.push_back(Value::Vector(crv.TangentAt(t))); pars.push_back(Value::Number(t)); }
      c.OutList(0, pts);
      c.OutList(1, tans);
      c.OutList(2, pars);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Offset Curve"; d.nick = "Offset"; d.category = "Curve"; d.subcategory = "Util";
    d.inputs = {In("Curve", Kind::Curve), In("Distance", Kind::Number, Access::Item, Value::Number(1)), In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{}))};
    d.outputs = {Out("Curve", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const kernel::NurbsCurve& crv = *c.In(0).curve;
      const Plane pl = c.PlaneIn(2);
      const double dist = c.Num(1, 1);
      const kernel::Interval dm = crv.Domain();
      const int n = std::max(8, crv.SuggestedSamples(0.01));
      std::vector<Point3d> pts;
      for (int i = 0; i <= n; ++i) {
        const double t = dm.min + (dm.max - dm.min) * i / n;
        const Point3d p = crv.PointAt(t);
        Vector3d tan = crv.TangentAt(t);
        Vector3d normal = ON_CrossProduct(pl.Normal(), tan);
        if (normal.Length() > 1e-9) normal.Unitize();
        pts.push_back(p + normal * dist);
      }
      c.Out(0, Value::Curve(PolylineCurveOf(pts, crv.IsClosed())));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Join Curves"; d.nick = "Join"; d.category = "Curve"; d.subcategory = "Util";
    d.inputs = {In("Curves", Kind::Curve, Access::List)};
    d.outputs = {Out("Curve", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      const std::vector<Value>& l = c.List(0);
      if (l.empty() || !l.front().curve) return;
      // Best-effort: chain into one polyline-approximated curve when disjoint;
      // otherwise pass the first through when already a single loop.
      if (l.size() == 1) { c.Out(0, l.front()); return; }
      std::vector<Point3d> pts;
      for (const Value& v : l) {
        if (!v.curve) continue;
        const kernel::Interval dm = v.curve->Domain();
        const int n = std::max(4, v.curve->SuggestedSamples(0.02));
        for (int i = 0; i <= n; ++i) pts.push_back(v.curve->PointAt(dm.min + (dm.max - dm.min) * i / n));
      }
      c.Out(0, Value::Curve(PolylineCurveOf(pts, false)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Explode Curve"; d.nick = "Explode"; d.category = "Curve"; d.subcategory = "Util";
    d.inputs = {In("Curve", Kind::Curve)};
    d.outputs = {Out("Segments", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const kernel::NurbsCurve& crv = *c.In(0).curve;
      const int n = std::max(2, crv.SuggestedSamples(0.02));
      const kernel::Interval dm = crv.Domain();
      std::vector<Value> segs;
      Point3d prev = crv.PointAt(dm.min);
      for (int i = 1; i <= n; ++i) { Point3d cur = crv.PointAt(dm.min + (dm.max - dm.min) * i / n); segs.push_back(Value::Curve(LineCurve(prev, cur))); prev = cur; }
      c.OutList(0, segs);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Extend Curve"; d.nick = "Extend"; d.category = "Curve"; d.subcategory = "Util";
    d.inputs = {In("Curve", Kind::Curve), In("Start", Kind::Number, Access::Item, Value::Number(1)), In("End", Kind::Number, Access::Item, Value::Number(1))};
    d.outputs = {Out("Curve", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      kernel::NurbsCurve crv = *c.In(0).curve;
      const kernel::Interval dm = crv.Domain();
      crv.Extend(dm.min - std::max(0.0, c.Num(1, 1)), dm.max + std::max(0.0, c.Num(2, 1)));
      c.Out(0, Value::Curve(crv));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Fillet Curve"; d.nick = "FilletCrv"; d.category = "Curve"; d.subcategory = "Util";
    d.description = "Fillets the corner between two straight segments (a two-point polyline pair).";
    d.inputs = {In("Curve A", Kind::Curve), In("Curve B", Kind::Curve), In("Radius", Kind::Number, Access::Item, Value::Number(1))};
    d.outputs = {Out("Fillet", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve || !c.In(1).curve) return;
      const kernel::NurbsCurve& a = *c.In(0).curve;
      const kernel::NurbsCurve& b = *c.In(1).curve;
      const kernel::Interval da = a.Domain(), db = b.Domain();
      const Point3d corner = a.PointAt(da.max);
      Vector3d ta = -a.TangentAt(da.max);
      Vector3d tb = b.TangentAt(db.min);
      const double r = std::max(1e-6, c.Num(2, 1));
      if (ta.Length() < 1e-9 || tb.Length() < 1e-9) { c.Fail("Degenerate tangents"); return; }
      ta.Unitize(); tb.Unitize();
      const double half = std::acos(std::clamp(ON_DotProduct(ta, tb), -1.0, 1.0)) / 2.0;
      if (half < 1e-6) { c.Out(0, Value::Curve(a)); return; }
      const double trim = r / std::tan(half);
      const Point3d p0 = corner + ta * trim;
      Vector3d bis = ta + tb; if (bis.Length() > 1e-9) bis.Unitize();
      const Point3d center = corner + bis * (r / std::sin(half));
      ON_Plane arc_plane(center, p0 - center, ON_CrossProduct(ta, tb));
      ON_Circle circ(arc_plane, r);
      ON_Arc real_arc(circ, ON_Interval(0.0, 2 * half));
      ON_NurbsCurve nc;
      real_arc.GetNurbForm(nc);
      kernel::NurbsCurve out;
      out.raw() = nc;
      c.Out(0, Value::Curve(out));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Curve Closest Point"; d.nick = "CP"; d.category = "Curve"; d.subcategory = "Analysis";
    d.inputs = {In("Curve", Kind::Curve), In("Point", Kind::Point)};
    d.outputs = {Out("Point", Kind::Point), Out("Parameter", Kind::Number), Out("Distance", Kind::Number)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const kernel::NurbsCurve& crv = *c.In(0).curve;
      const Point3d p = c.Pt(1);
      const double t = crv.ClosestPointParameter(p);
      const Point3d cp = crv.PointAt(t);
      c.Out(0, Value::Point(cp));
      c.Out(1, Value::Number(t));
      c.Out(2, Value::Number(cp.DistanceTo(p)));
    };
    r.Add(d);
  }

  {
    NodeDef d; d.name = "End Points"; d.nick = "End"; d.category = "Curve"; d.subcategory = "Analysis";
    d.description = "The start and the end point of a curve.";
    d.inputs = {In("Curve", Kind::Curve)};
    d.outputs = {Out("Start", Kind::Point), Out("End", Kind::Point)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const kernel::Interval dm = c.In(0).curve->Domain();
      c.Out(0, Value::Point(c.In(0).curve->PointAt(dm.min)));
      c.Out(1, Value::Point(c.In(0).curve->PointAt(dm.max)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Control Points"; d.nick = "CPts"; d.category = "Curve"; d.subcategory = "Analysis";
    d.description = "The control points, their weights and the knot vector of a curve, as stored (a curve with n control points and degree p has n + p - 1 knots).";
    d.inputs = {In("Curve", Kind::Curve)};
    d.outputs = {Out("Points", Kind::Point), Out("Weights", Kind::Number), Out("Knots", Kind::Number)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const kernel::NurbsCurve& crv = *c.In(0).curve;
      std::vector<Value> pts, wts, knots;
      for (int i = 0; i < crv.ControlPointCount(); ++i) {
        pts.push_back(Value::Point(crv.ControlPointAt(i)));
        wts.push_back(Value::Number(crv.IsRational() ? crv.WeightAt(i) : 1.0));
      }
      for (int i = 0; i < crv.KnotCount(); ++i) knots.push_back(Value::Number(crv.KnotAt(i)));
      c.OutList(0, pts); c.OutList(1, wts); c.OutList(2, knots);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Is Closed"; d.nick = "Closed"; d.category = "Curve"; d.subcategory = "Analysis";
    d.description = "True when the curve's end meets its start.";
    d.inputs = {In("Curve", Kind::Curve)};
    d.outputs = {Out("Closed", Kind::Boolean)};
    d.eval = [](EvalContext& c) { if (c.In(0).curve) c.Out(0, Value::Boolean(c.In(0).curve->IsClosed())); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Curve Area"; d.nick = "CArea"; d.category = "Curve"; d.subcategory = "Analysis";
    d.description = "Area and centroid of the region a closed planar curve bounds (Green's theorem, Gauss-Legendre per knot span).";
    d.inputs = {In("Curve", Kind::Curve)};
    d.outputs = {Out("Area", Kind::Number), Out("Centroid", Kind::Point)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      double area = 0; Point3d cen;
      if (!PlanarAreaCentroid(*c.In(0).curve, area, cen)) { c.Fail("Curve Area needs a closed planar curve"); return; }
      c.Out(0, Value::Number(area));
      c.Out(1, Value::Point(cen));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Curve Curvature"; d.nick = "Crv"; d.category = "Curve"; d.subcategory = "Analysis";
    d.description = "Curvature at the curve's own parameter t (clamped to the domain; the domain midpoint when t is not given): the point, the curvature vector (towards the centre of curvature, length 1/radius) and the osculating circle (centre = point + vector / |vector|^2, radius 1/|vector|, in the plane of the tangent and the vector, counter-clockwise from the point). A straight stretch has a zero vector and no circle.";
    d.inputs = {In("Curve", Kind::Curve), In("t", Kind::Number, Access::Item, Value::Null(), true)};
    d.outputs = {Out("Point", Kind::Point), Out("Curvature", Kind::Vector), Out("Circle", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const auto& crv = *c.In(0).curve;
      const kernel::Interval dm = crv.Domain();
      const double t = c.Has(1) ? std::clamp(c.Num(1), dm.min, dm.max) : 0.5 * (dm.min + dm.max);
      const Point3d p = crv.PointAt(t);
      const Vector3d k = crv.CurvatureAt(t);
      c.Out(0, Value::Point(p));
      c.Out(1, Value::Vector(k));
      const double kl = k.Length();
      if (!(kl > 1e-12)) { c.Warn("Zero curvature here: no osculating circle"); return; }
      const double rad = 1.0 / kl;
      Vector3d x = k * (-1.0 / kl);                    // from the centre to the point
      Vector3d y = crv.TangentAt(t);
      y = y - x * ON_DotProduct(y, x);                 // the tangent is perpendicular to the curvature vector; clean up rounding
      if (!(y.Length() > 1e-12)) return;
      y.Unitize();
      Plane pl; pl.origin = p + k * (rad * rad); pl.x = x; pl.y = y;
      c.Out(2, Value::Curve(CircleCurve(pl, rad)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Evaluate Length"; d.nick = "EvalLen"; d.category = "Curve"; d.subcategory = "Analysis";
    d.description = "The point, tangent and parameter at a distance measured along the curve from its start (to 1e-10); Normalized reads the distance as a fraction of the whole length.";
    d.inputs = {In("Curve", Kind::Curve), In("Length", Kind::Number, Access::Item, Value::Number(0)), In("Normalized", Kind::Boolean, Access::Item, Value::Boolean(false))};
    d.outputs = {Out("Point", Kind::Point), Out("Tangent", Kind::Vector), Out("Parameter", Kind::Number)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const auto& crv = *c.In(0).curve;
      const double total = crv.LengthToTolerance(1e-10);
      const double target = std::clamp(c.Bool(2) ? c.Num(1) * total : c.Num(1), 0.0, total);
      const double t = total > 1e-12 ? crv.ParameterAtArcLength(target, 1e-10) : crv.Domain().min;
      c.Out(0, Value::Point(crv.PointAt(t)));
      c.Out(1, Value::Vector(crv.TangentAt(t)));
      c.Out(2, Value::Number(t));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Divide Length"; d.nick = "DivLen"; d.category = "Curve"; d.subcategory = "Division";
    d.description = "Points at 0, Length, 2 Length, ... along the curve, measured as arc length from its start (each to 1e-10). The leftover piece at the end is not given a point of its own; a multiple that lands on the end gives the end point.";
    d.inputs = {In("Curve", Kind::Curve), In("Length", Kind::Number, Access::Item, Value::Number(1))};
    d.outputs = {Out("Points", Kind::Point), Out("Tangents", Kind::Vector), Out("Parameters", Kind::Number)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const auto& crv = *c.In(0).curve;
      const double step = c.Num(1, 1);
      if (!(step > 1e-9)) { c.Fail("Length must be positive"); return; }
      const double total = crv.LengthToTolerance(1e-10);
      // At most 10000 points, as Divide Curve: the count comes from a file's number.
      const int kmax = static_cast<int>(std::min(10000.0, std::floor(total / step + 1e-9)));
      std::vector<Value> pts, tans, pars;
      for (int k = 0; k <= kmax; ++k) {
        const double target = std::min(total, step * k);
        const double t = (k == 0 || total <= 1e-12) ? crv.Domain().min : crv.ParameterAtArcLength(target, 1e-10);
        pts.push_back(Value::Point(crv.PointAt(t))); tans.push_back(Value::Vector(crv.TangentAt(t))); pars.push_back(Value::Number(t));
      }
      c.OutList(0, pts); c.OutList(1, tans); c.OutList(2, pars);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Line SDL"; d.nick = "LnSDL"; d.category = "Curve"; d.subcategory = "Primitive";
    d.description = "A line from a start point along a direction for a length: the end is start + unit(direction) * length.";
    d.inputs = {In("Start", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("Direction", Kind::Vector, Access::Item, Value::Vector(Vector3d(1, 0, 0))), In("Length", Kind::Number, Access::Item, Value::Number(1))};
    d.outputs = {Out("Line", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      Vector3d dir = c.Vec(1, Vector3d(1, 0, 0));
      if (!(dir.Length() > 1e-12)) { c.Fail("Direction must not be the zero vector"); return; }
      dir.Unitize();
      const Point3d a = c.Pt(0);
      c.Out(0, Value::Curve(kernel::NurbsCurve::FromControlPoints({a, a + dir * c.Num(2, 1)}, 1)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Circle 3Pt"; d.nick = "Cir3Pt"; d.category = "Curve"; d.subcategory = "Primitive";
    d.description = "The circle through three points, with its plane (origin at the centre) and radius.";
    d.inputs = {In("P1", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("P2", Kind::Point, Access::Item, Value::Point(Point3d(1, 0, 0))), In("P3", Kind::Point, Access::Item, Value::Point(Point3d(0, 1, 0)))};
    d.outputs = {Out("Circle", Kind::Curve), Out("Plane", Kind::Plane), Out("Radius", Kind::Number)};
    d.eval = [](EvalContext& c) {
      ON_Circle circ(c.Pt(0), c.Pt(1, Point3d(1, 0, 0)), c.Pt(2, Point3d(0, 1, 0)));
      if (!circ.IsValid()) { c.Fail("The three points are on one line (or coincide)"); return; }
      ON_NurbsCurve nc;
      circ.GetNurbForm(nc);
      kernel::NurbsCurve out; out.raw() = nc;
      c.Out(0, Value::Curve(out));
      c.Out(1, Value::PlaneV(Plane::FromON(circ.plane)));
      c.Out(2, Value::Number(circ.radius));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Arc 3Pt"; d.nick = "Arc3Pt"; d.category = "Curve"; d.subcategory = "Primitive";
    d.description = "The arc that starts at the first point, passes through the second and ends at the third; with its plane (origin at the centre), radius and angle in radians.";
    d.inputs = {In("Start", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("Interior", Kind::Point, Access::Item, Value::Point(Point3d(1, 1, 0))), In("End", Kind::Point, Access::Item, Value::Point(Point3d(2, 0, 0)))};
    d.outputs = {Out("Arc", Kind::Curve), Out("Plane", Kind::Plane), Out("Radius", Kind::Number), Out("Angle", Kind::Number)};
    d.eval = [](EvalContext& c) {
      ON_Arc arc(c.Pt(0), c.Pt(1, Point3d(1, 1, 0)), c.Pt(2, Point3d(2, 0, 0)));
      if (!arc.IsValid()) { c.Fail("The three points are on one line (or coincide)"); return; }
      ON_NurbsCurve nc;
      arc.GetNurbForm(nc);
      kernel::NurbsCurve out; out.raw() = nc;
      c.Out(0, Value::Curve(out));
      c.Out(1, Value::PlaneV(Plane::FromON(arc.plane)));
      c.Out(2, Value::Number(arc.radius));
      c.Out(3, Value::Number(arc.AngleRadians()));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Rectangle 2Pt"; d.nick = "Rec2Pt"; d.category = "Curve"; d.subcategory = "Primitive";
    d.description = "The rectangle in a plane with the two points as opposite corners (sides along the plane's axes); the points are projected into the plane.";
    d.inputs = {In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("Point A", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("Point B", Kind::Point, Access::Item, Value::Point(Point3d(10, 10, 0)))};
    d.outputs = {Out("Rectangle", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      const ON_Plane pl = c.PlaneIn(0).ToON();
      const Vector3d ra = c.Pt(1) - pl.origin, rb = c.Pt(2, Point3d(10, 10, 0)) - pl.origin;
      const double u0 = ON_DotProduct(ra, pl.xaxis), v0 = ON_DotProduct(ra, pl.yaxis), u1 = ON_DotProduct(rb, pl.xaxis), v1 = ON_DotProduct(rb, pl.yaxis);
      std::vector<Point3d> pts = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)};
      c.Out(0, Value::Curve(PolylineCurveOf(pts, true)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Polygon"; d.nick = "Pgon"; d.category = "Curve"; d.subcategory = "Primitive";
    d.description = "A regular polygon in a plane, its corners on the circle of the given radius around the plane origin, the first on the plane's X axis, counter-clockwise about the normal. When Edge Length is given it sets the size instead (radius = edge / (2 sin(pi / segments))).";
    d.inputs = {In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("Radius", Kind::Number, Access::Item, Value::Number(5)), In("Segments", Kind::Integer, Access::Item, Value::Integer(6)),
                In("Edge Length", Kind::Number, Access::Item, Value::Null(), true)};
    d.outputs = {Out("Polygon", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      const int n = std::clamp(c.Int(2, 6), 3, 10000);
      const ON_Plane pl = c.PlaneIn(0).ToON();
      const double rad = (c.Has(3) && c.Num(3) != 0.0) ? std::fabs(c.Num(3)) / (2.0 * std::sin(ON_PI / n)) : std::fabs(c.Num(1, 5));
      std::vector<Point3d> pts;
      for (int k = 0; k < n; ++k) { const double a = 2.0 * ON_PI * k / n; pts.push_back(pl.PointAt(rad * std::cos(a), rad * std::sin(a))); }
      c.Out(0, Value::Curve(PolylineCurveOf(pts, true)));
    };
    r.Add(d);
  }
}

}  // namespace

void RegisterCurveNodes() { AddCurve(); }

}  // namespace dino8::flow
