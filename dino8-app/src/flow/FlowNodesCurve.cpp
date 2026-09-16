// Curve node category.
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

kernel::NurbsCurve ArcCurve(const Plane& pl, double r, double a0, double a1) {
  ON_Arc arc(ON_Circle(pl.ToON(), r), ON_Interval(a0, a1));
  ON_NurbsCurve nc;
  arc.GetNurbForm(nc);
  kernel::NurbsCurve c;
  c.raw() = nc;
  return c;
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
    d.eval = [](EvalContext& c) { c.Out(0, Value::Curve(CircleCurve(c.PlaneIn(0), std::max(1e-6, c.Num(1, 5))))); };
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
    d.inputs = {In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("Radius", Kind::Number, Access::Item, Value::Number(5)),
                In("Start Angle", Kind::Number, Access::Item, Value::Number(0)), In("End Angle", Kind::Number, Access::Item, Value::Number(90))};
    d.outputs = {Out("Arc", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      c.Out(0, Value::Curve(ArcCurve(c.PlaneIn(0), std::max(1e-6, c.Num(1, 5)), c.Num(2) * ON_PI / 180.0, c.Num(3, 90) * ON_PI / 180.0)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Rectangle"; d.nick = "Rec"; d.category = "Curve"; d.subcategory = "Primitive";
    d.inputs = {In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("X Size", Kind::Number, Access::Item, Value::Number(10)), In("Y Size", Kind::Number, Access::Item, Value::Number(10))};
    d.outputs = {Out("Rectangle", Kind::Curve)};
    d.eval = [](EvalContext& c) {
      Plane pl = c.PlaneIn(0);
      const double sx = c.Num(1, 10), sy = c.Num(2, 10);
      std::vector<Point3d> pts = {pl.At(0, 0), pl.At(sx, 0), pl.At(sx, sy), pl.At(0, sy)};
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
    d.inputs = {In("Curve", Kind::Curve), In("t", Kind::Number, Access::Item, Value::Number(0.5))};
    d.outputs = {Out("Point", Kind::Point), Out("Tangent", Kind::Vector)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const auto& crv = *c.In(0).curve;
      const kernel::Interval dm = crv.Domain();
      const double t = dm.min + (dm.max - dm.min) * std::clamp(c.Num(1, 0.5), 0.0, 1.0);
      c.Out(0, Value::Point(crv.PointAt(t)));
      c.Out(1, Value::Vector(crv.TangentAt(t)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Curve Length"; d.nick = "Len"; d.category = "Curve"; d.subcategory = "Analysis";
    d.inputs = {In("Curve", Kind::Curve)};
    d.outputs = {Out("Length", Kind::Number)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Number(c.In(0).curve ? c.In(0).curve->Length(400) : 0.0)); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Divide Curve"; d.nick = "Divide"; d.category = "Curve"; d.subcategory = "Division";
    d.inputs = {In("Curve", Kind::Curve), In("Count", Kind::Integer, Access::Item, Value::Integer(10))};
    d.outputs = {Out("Points", Kind::Point), Out("Tangents", Kind::Vector)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      const auto& crv = *c.In(0).curve;
      const int n = std::max(1, c.Int(1, 10));
      std::vector<double> ts = crv.DivideByCount(n);
      std::vector<Value> pts, tans;
      for (double t : ts) { pts.push_back(Value::Point(crv.PointAt(t))); tans.push_back(Value::Vector(crv.TangentAt(t))); }
      c.OutList(0, pts);
      c.OutList(1, tans);
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
}

}  // namespace

void RegisterCurveNodes() { AddCurve(); }

}  // namespace dino8::flow
