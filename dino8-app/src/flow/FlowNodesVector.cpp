// Vector and Transform node categories.
#include <cmath>

#include "flow/FlowGraph.h"

namespace dino8::flow {

namespace {

PortDef In(std::string name, Kind k, Access acc = Access::Item, Value def = Value::Null(), bool optional = false) {
  PortDef p; p.name = name; p.nick = name; p.kind = k; p.access = acc; p.def = std::move(def); p.optional = optional; return p;
}
PortDef Out(std::string name, Kind k) { PortDef p; p.name = name; p.nick = name; p.kind = k; return p; }

ON_Xform MoveXform(Vector3d v) { ON_Xform x; x.Translation(v); return x; }
ON_Xform ScaleXform(Point3d c, double s) { ON_Xform x; x.Identity(); x = ON_Xform::ScaleTransformation(c, s); return x; }
ON_Xform RotateXform(double angle_deg, Vector3d axis, Point3d center) { ON_Xform x; x.Rotation(angle_deg * ON_PI / 180.0, axis, center); return x; }

void AddVector() {
  Registry& r = Registry::Get();
  {
    NodeDef d; d.name = "Vector XYZ"; d.nick = "Vec"; d.category = "Vector"; d.subcategory = "Vector";
    d.inputs = {In("X", Kind::Number, Access::Item, Value::Number(0)), In("Y", Kind::Number, Access::Item, Value::Number(0)), In("Z", Kind::Number, Access::Item, Value::Number(1))};
    d.outputs = {Out("V", Kind::Vector)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Vector(Vector3d(c.Num(0), c.Num(1), c.Num(2)))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Deconstruct Point"; d.nick = "pDecon"; d.category = "Vector"; d.subcategory = "Point";
    d.inputs = {In("Point", Kind::Point)};
    d.outputs = {Out("X", Kind::Number), Out("Y", Kind::Number), Out("Z", Kind::Number)};
    d.eval = [](EvalContext& c) { Point3d p = c.Pt(0); c.Out(0, Value::Number(p.x)); c.Out(1, Value::Number(p.y)); c.Out(2, Value::Number(p.z)); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Distance"; d.nick = "Dist"; d.category = "Vector"; d.subcategory = "Point";
    d.inputs = {In("A", Kind::Point), In("B", Kind::Point)};
    d.outputs = {Out("Distance", Kind::Number)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Number((c.Pt(0) - c.Pt(1)).Length())); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Vector Length"; d.nick = "Len"; d.category = "Vector"; d.subcategory = "Vector";
    d.inputs = {In("Vector", Kind::Vector)};
    d.outputs = {Out("Length", Kind::Number)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Number(c.Vec(0).Length())); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Unit Vector"; d.nick = "Unit"; d.category = "Vector"; d.subcategory = "Vector";
    d.inputs = {In("Vector", Kind::Vector)};
    d.outputs = {Out("Vector", Kind::Vector)};
    d.eval = [](EvalContext& c) { Vector3d v = c.Vec(0); if (v.Length() > 1e-12) v.Unitize(); c.Out(0, Value::Vector(v)); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Vector Addition"; d.nick = "V+V"; d.category = "Vector"; d.subcategory = "Vector";
    d.inputs = {In("A", Kind::Vector), In("B", Kind::Vector)};
    d.outputs = {Out("Result", Kind::Vector)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Vector(c.Vec(0) + c.Vec(1))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Vector Scale"; d.nick = "V*S"; d.category = "Vector"; d.subcategory = "Vector";
    d.inputs = {In("Vector", Kind::Vector), In("Factor", Kind::Number, Access::Item, Value::Number(1))};
    d.outputs = {Out("Result", Kind::Vector)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Vector(c.Vec(0) * c.Num(1))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Cross Product"; d.nick = "Cross"; d.category = "Vector"; d.subcategory = "Vector";
    d.inputs = {In("A", Kind::Vector), In("B", Kind::Vector)};
    d.outputs = {Out("Result", Kind::Vector)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Vector(ON_CrossProduct(c.Vec(0), c.Vec(1)))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Dot Product"; d.nick = "Dot"; d.category = "Vector"; d.subcategory = "Vector";
    d.inputs = {In("A", Kind::Vector), In("B", Kind::Vector)};
    d.outputs = {Out("Result", Kind::Number)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Number(ON_DotProduct(c.Vec(0), c.Vec(1)))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Plane Origin"; d.nick = "Pl"; d.category = "Vector"; d.subcategory = "Plane";
    d.inputs = {In("Origin", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("X-Axis", Kind::Vector, Access::Item, Value::Vector(Vector3d(1, 0, 0))), In("Y-Axis", Kind::Vector, Access::Item, Value::Vector(Vector3d(0, 1, 0)))};
    d.outputs = {Out("Plane", Kind::Plane)};
    d.eval = [](EvalContext& c) { Plane p; p.origin = c.Pt(0); p.x = c.Vec(1, Vector3d(1, 0, 0)); p.y = c.Vec(2, Vector3d(0, 1, 0)); c.Out(0, Value::PlaneV(p)); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Plane Normal"; d.nick = "PlN"; d.category = "Vector"; d.subcategory = "Plane";
    d.description = "A plane from an origin and a normal.";
    d.inputs = {In("Origin", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("Normal", Kind::Vector, Access::Item, Value::Vector(Vector3d(0, 0, 1)))};
    d.outputs = {Out("Plane", Kind::Plane)};
    d.eval = [](EvalContext& c) {
      ON_Plane pl(c.Pt(0), c.Vec(1, Vector3d(0, 0, 1)));
      c.Out(0, Value::PlaneV(Plane::FromON(pl)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Plane 3Pt"; d.nick = "Pl3Pt"; d.category = "Vector"; d.subcategory = "Plane";
    d.inputs = {In("Origin", Kind::Point), In("X Point", Kind::Point), In("Y Point", Kind::Point)};
    d.outputs = {Out("Plane", Kind::Plane)};
    d.eval = [](EvalContext& c) {
      ON_Plane pl(c.Pt(0), c.Pt(1), c.Pt(2));
      c.Out(0, Value::PlaneV(Plane::FromON(pl)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "XY Plane"; d.nick = "XY"; d.category = "Vector"; d.subcategory = "Plane";
    d.inputs = {In("Origin", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0)))};
    d.outputs = {Out("Plane", Kind::Plane)};
    d.eval = [](EvalContext& c) { Plane p; p.origin = c.Pt(0); p.x = Vector3d(1, 0, 0); p.y = Vector3d(0, 1, 0); c.Out(0, Value::PlaneV(p)); };
    r.Add(d);
  }
}

void AddTransform() {
  Registry& r = Registry::Get();
  {
    NodeDef d; d.name = "Move"; d.nick = "Move"; d.category = "Transform"; d.subcategory = "Euclidean";
    d.inputs = {In("Geometry", Kind::Any), In("Motion", Kind::Vector, Access::Item, Value::Vector(Vector3d(0, 0, 0)))};
    d.outputs = {Out("Geometry", Kind::Any)};
    d.eval = [](EvalContext& c) { c.Out(0, c.In(0).Transformed(MoveXform(c.Vec(1)))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Rotate"; d.nick = "Rotate"; d.category = "Transform"; d.subcategory = "Euclidean";
    d.inputs = {In("Geometry", Kind::Any), In("Angle", Kind::Number, Access::Item, Value::Number(45)), In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{}))};
    d.outputs = {Out("Geometry", Kind::Any)};
    d.eval = [](EvalContext& c) { Plane pl = c.PlaneIn(2); c.Out(0, c.In(0).Transformed(RotateXform(c.Num(1), pl.Normal(), pl.origin))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Scale"; d.nick = "Scale"; d.category = "Transform"; d.subcategory = "Euclidean";
    d.inputs = {In("Geometry", Kind::Any), In("Center", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("Factor", Kind::Number, Access::Item, Value::Number(2))};
    d.outputs = {Out("Geometry", Kind::Any)};
    d.eval = [](EvalContext& c) { c.Out(0, c.In(0).Transformed(ScaleXform(c.Pt(1), c.Num(2)))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Mirror"; d.nick = "Mirror"; d.category = "Transform"; d.subcategory = "Euclidean";
    d.inputs = {In("Geometry", Kind::Any), In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{}))};
    d.outputs = {Out("Geometry", Kind::Any)};
    d.eval = [](EvalContext& c) {
      Plane pl = c.PlaneIn(1);
      ON_Xform x; x.Mirror(pl.origin, pl.Normal());
      c.Out(0, c.In(0).Transformed(x));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Linear Array"; d.nick = "ArrLin"; d.category = "Transform"; d.subcategory = "Array";
    d.inputs = {In("Geometry", Kind::Any), In("Direction", Kind::Vector, Access::Item, Value::Vector(Vector3d(1, 0, 0))),
                In("Distance", Kind::Number, Access::Item, Value::Number(5)), In("Count", Kind::Integer, Access::Item, Value::Integer(5))};
    d.outputs = {Out("Geometry", Kind::Any)};
    d.eval = [](EvalContext& c) {
      const int n = std::max(1, c.Int(3));
      Vector3d dir = c.Vec(1); if (dir.Length() > 1e-12) dir.Unitize();
      const double dist = c.Num(2);
      std::vector<Value> out;
      for (int i = 0; i < n; ++i) out.push_back(c.In(0).Transformed(MoveXform(dir * (dist * i))));
      c.OutList(0, out);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Polar Array"; d.nick = "ArrPolar"; d.category = "Transform"; d.subcategory = "Array";
    d.inputs = {In("Geometry", Kind::Any), In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("Count", Kind::Integer, Access::Item, Value::Integer(6)), In("Angle", Kind::Number, Access::Item, Value::Number(360))};
    d.outputs = {Out("Geometry", Kind::Any)};
    d.eval = [](EvalContext& c) {
      const int n = std::max(1, c.Int(2));
      Plane pl = c.PlaneIn(1);
      const double total = c.Num(3);
      std::vector<Value> out;
      for (int i = 0; i < n; ++i) out.push_back(c.In(0).Transformed(RotateXform(total * i / n, pl.Normal(), pl.origin)));
      c.OutList(0, out);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Orient"; d.nick = "Orient"; d.category = "Transform"; d.subcategory = "Euclidean";
    d.inputs = {In("Geometry", Kind::Any), In("Source", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("Target", Kind::Plane, Access::Item, Value::PlaneV(Plane{}))};
    d.outputs = {Out("Geometry", Kind::Any)};
    d.eval = [](EvalContext& c) {
      Plane a = c.PlaneIn(1), b = c.PlaneIn(2);
      ON_Xform x;
      x.Rotation(a.ToON(), b.ToON());
      c.Out(0, c.In(0).Transformed(x));
    };
    r.Add(d);
  }
}

}  // namespace

void RegisterVectorNodes() {
  AddVector();
  AddTransform();
}

}  // namespace dino8::flow
