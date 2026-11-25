// Intersect node category: analytic intersections of lines, planes and curves.
#include <algorithm>
#include <cmath>

#include "commands/cmd_common.h"
#include "dino8/kernel/surface_intersect.h"
#include "flow/FlowGraph.h"

namespace dino8::flow {

namespace {

PortDef In(std::string name, Kind k, Access acc = Access::Item, Value def = Value::Null(), bool optional = false) {
  PortDef p; p.name = name; p.nick = name; p.kind = k; p.access = acc; p.def = std::move(def); p.optional = optional; return p;
}
PortDef Out(std::string name, Kind k) { PortDef p; p.name = name; p.nick = name; p.kind = k; return p; }

kernel::NurbsCurve LineBetween(Point3d a, Point3d b) { return kernel::NurbsCurve::FromControlPoints({a, b}, 1); }

void AddIntersect() {
  Registry& r = Registry::Get();
  {
    NodeDef d; d.name = "Line x Line"; d.nick = "LxL"; d.category = "Intersect"; d.subcategory = "Mathematical";
    d.description = "Where two lines cross, or their closest approach: the nearest point on each line (the lines taken as infinite), the distance between those points and each point's position as a fraction of its own segment (0 at the start, 1 at the end). Parallel lines have no answer.";
    d.inputs = {In("Line A", Kind::Curve), In("Line B", Kind::Curve)};
    d.outputs = {Out("Point A", Kind::Point), Out("Point B", Kind::Point), Out("Distance", Kind::Number), Out("Parameter A", Kind::Number), Out("Parameter B", Kind::Number)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve || !c.In(1).curve) return;
      const kernel::NurbsCurve& ca = *c.In(0).curve;
      const kernel::NurbsCurve& cb = *c.In(1).curve;
      const Point3d a0 = ca.PointAt(ca.Domain().min), a1 = ca.PointAt(ca.Domain().max);
      const Point3d b0 = cb.PointAt(cb.Domain().min), b1 = cb.PointAt(cb.Domain().max);
      const Vector3d da = a1 - a0, db = b1 - b0, w = a0 - b0;
      const double aa = ON_DotProduct(da, da), bb = ON_DotProduct(da, db), cc = ON_DotProduct(db, db);
      const double dd = ON_DotProduct(da, w), ee = ON_DotProduct(db, w);
      const double den = aa * cc - bb * bb;
      if (aa < 1e-24 || cc < 1e-24 || den <= 1e-12 * aa * cc) { c.Fail("The lines are parallel (or one is a point)"); return; }
      const double s = (bb * ee - cc * dd) / den;  // along A
      const double t = (aa * ee - bb * dd) / den;  // along B
      const Point3d pa = a0 + da * s, pb = b0 + db * t;
      c.Out(0, Value::Point(pa));
      c.Out(1, Value::Point(pb));
      c.Out(2, Value::Number(pa.DistanceTo(pb)));
      c.Out(3, Value::Number(s));
      c.Out(4, Value::Number(t));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Plane x Plane"; d.nick = "PxP"; d.category = "Intersect"; d.subcategory = "Mathematical";
    d.description = "The line where two planes meet. Direction is the unit vector A.normal x B.normal; Point is the point of that line nearest the origin of plane A; Line is the 2-unit segment centred on Point. Parallel planes have no answer.";
    d.inputs = {In("Plane A", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("Plane B", Kind::Plane, Access::Item, Value::PlaneV(Plane{}))};
    d.outputs = {Out("Line", Kind::Curve), Out("Point", Kind::Point), Out("Direction", Kind::Vector)};
    d.eval = [](EvalContext& c) {
      const ON_Plane pa = c.PlaneIn(0).ToON(), pb = c.PlaneIn(1).ToON();
      Vector3d dir = ON_CrossProduct(pa.zaxis, pb.zaxis);
      const double len = dir.Length();
      if (len < 1e-9) { c.Fail("The planes are parallel"); return; }
      dir *= 1.0 / len;
      // The line point p with n_a.(p - o_a) = 0 and n_b.(p - o_b) = 0 that lies in the plane spanned by the two normals
      // (so it is the one nearest o_a): p = o_a + alpha n_a + beta n_b.
      const double ab = ON_DotProduct(pa.zaxis, pb.zaxis);
      const double h = ON_DotProduct(pb.zaxis, pa.origin - pb.origin);  // n_b.(o_a - o_b)
      const double det = 1.0 - ab * ab;
      const double alpha = -h * (-ab) / det;  // solves [1 ab; ab 1] [alpha beta]^T = [0; -h]
      const double beta = -h / det;
      const Point3d p = pa.origin + pa.zaxis * alpha + pb.zaxis * beta;
      c.Out(0, Value::Curve(LineBetween(p - dir, p + dir)));
      c.Out(1, Value::Point(p));
      c.Out(2, Value::Vector(dir));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Plane x Plane x Plane"; d.nick = "PxPxP"; d.category = "Intersect"; d.subcategory = "Mathematical";
    d.description = "The point common to three planes. Planes with no single common point (two parallel, or all three through one line) have no answer.";
    d.inputs = {In("Plane A", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("Plane B", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("Plane C", Kind::Plane, Access::Item, Value::PlaneV(Plane{}))};
    d.outputs = {Out("Point", Kind::Point)};
    d.eval = [](EvalContext& c) {
      const ON_Plane p1 = c.PlaneIn(0).ToON(), p2 = c.PlaneIn(1).ToON(), p3 = c.PlaneIn(2).ToON();
      const Vector3d n1 = p1.zaxis, n2 = p2.zaxis, n3 = p3.zaxis;
      const double det = ON_DotProduct(n1, ON_CrossProduct(n2, n3));
      if (std::fabs(det) < 1e-9) { c.Fail("The three planes have no single common point"); return; }
      const double d1 = ON_DotProduct(n1, p1.origin), d2 = ON_DotProduct(n2, p2.origin), d3 = ON_DotProduct(n3, p3.origin);
      // Cramer's rule in vector form: p = (d1 (n2 x n3) + d2 (n3 x n1) + d3 (n1 x n2)) / det
      const Vector3d p = (ON_CrossProduct(n2, n3) * d1 + ON_CrossProduct(n3, n1) * d2 + ON_CrossProduct(n1, n2) * d3) * (1.0 / det);
      c.Out(0, Value::Point(Point3d(p.x, p.y, p.z)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Curve x Plane"; d.nick = "CxP"; d.category = "Intersect"; d.subcategory = "Mathematical";
    d.description = "The points where a curve crosses a plane (the plane is infinite), in order along the curve, with the curve parameter of each.";
    d.inputs = {In("Curve", Kind::Curve), In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{}))};
    d.outputs = {Out("Points", Kind::Point), Out("Parameters", Kind::Number)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      kernel::IntersectOptions opt;
      opt.tolerance = 1e-10;
      opt.mesh_tolerance = 1e-3;
      std::vector<kernel::CurvePlaneHit> hits = kernel::IntersectCurvePlane(c.In(0).curve->raw(), c.PlaneIn(1).ToON(), opt);
      std::sort(hits.begin(), hits.end(), [](const kernel::CurvePlaneHit& a, const kernel::CurvePlaneHit& b) { return a.t < b.t; });
      std::vector<Value> pts, ts;
      for (const kernel::CurvePlaneHit& h : hits) { pts.push_back(Value::Point(h.point)); ts.push_back(Value::Number(h.t)); }
      c.OutList(0, pts);
      c.OutList(1, ts);
    };
    r.Add(d);
  }
}

}  // namespace

void RegisterIntersectNodes() { AddIntersect(); }

}  // namespace dino8::flow
