// Surface and Mesh node categories.
#include <algorithm>
#include <cmath>

#include "commands/cmd_common.h"
#include "dino8/kernel/boolean.h"
#include "flow/FlowGraph.h"
#include "geom/BrepMesher.h"

namespace dino8::flow {

namespace {

PortDef In(std::string name, Kind k, Access acc = Access::Item, Value def = Value::Null(), bool optional = false) {
  PortDef p; p.name = name; p.nick = name; p.kind = k; p.access = acc; p.def = std::move(def); p.optional = optional; return p;
}
PortDef Out(std::string name, Kind k) { PortDef p; p.name = name; p.nick = name; p.kind = k; return p; }

kernel::Mesh MeshOfValue(const Value& v, double tol = 0.02) {
  if (v.mesh) return *v.mesh;
  if (v.brep) {
    app::BrepMeshOptions opt;
    opt.chord_tolerance = tol;
    return app::MeshBrepClosed(v.brep->raw(), opt);
  }
  if (v.surface) return v.surface->TessellateGridAdaptive(tol);
  return kernel::Mesh();
}

void AddSurface() {
  Registry& r = Registry::Get();
  {
    NodeDef d; d.name = "Plane Surface"; d.nick = "PlSrf"; d.category = "Surface"; d.subcategory = "Primitive";
    d.inputs = {In("Plane", Kind::Plane, Access::Item, Value::PlaneV(Plane{})), In("X Size", Kind::Number, Access::Item, Value::Number(10)), In("Y Size", Kind::Number, Access::Item, Value::Number(10))};
    d.outputs = {Out("Surface", Kind::Surface)};
    d.eval = [](EvalContext& c) {
      Plane pl = c.PlaneIn(0);
      const double sx = c.Num(1, 10), sy = c.Num(2, 10);
      std::vector<Point3d> grid = {pl.At(0, 0), pl.At(sx, 0), pl.At(0, sy), pl.At(sx, sy)};
      c.Out(0, Value::Surface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Box"; d.nick = "Box"; d.category = "Surface"; d.subcategory = "Primitive";
    d.inputs = {In("Base", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("X", Kind::Number, Access::Item, Value::Number(10)),
                In("Y", Kind::Number, Access::Item, Value::Number(10)), In("Z", Kind::Number, Access::Item, Value::Number(10))};
    d.outputs = {Out("Brep", Kind::Brep)};
    d.eval = [](EvalContext& c) {
      Point3d b = c.Pt(0);
      const double sx = c.Num(1, 10), sy = c.Num(2, 10), sz = c.Num(3, 10);
      c.Out(0, Value::BrepV(kernel::Brep::Box(b.x, b.y, b.z, b.x + sx, b.y + sy, b.z + sz)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Sphere"; d.nick = "Sph"; d.category = "Surface"; d.subcategory = "Primitive";
    d.inputs = {In("Center", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("Radius", Kind::Number, Access::Item, Value::Number(5))};
    d.outputs = {Out("Brep", Kind::Brep)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::BrepV(kernel::Brep::Sphere(c.Pt(0), std::max(1e-6, c.Num(1, 5))))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Extrude"; d.nick = "Extr"; d.category = "Surface"; d.subcategory = "Freeform";
    d.inputs = {In("Curve", Kind::Curve), In("Direction", Kind::Vector, Access::Item, Value::Vector(Vector3d(0, 0, 10)))};
    d.outputs = {Out("Brep", Kind::Brep)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      ON_NurbsCurve crv = c.In(0).curve->raw();
      const Vector3d v = c.Vec(1, Vector3d(0, 0, 10));
      ON_Plane plane;
      if (crv.IsClosed() && crv.IsPlanar(&plane)) {
        ON_Brep* b = ON_BrepTrimmedPlane(plane, crv);
        if (b) {
          ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + v));
          if (ON_BrepExtrudeFace(*b, 0, path, true) >= 0) { kernel::Brep out; out.raw() = *b; delete b; c.Out(0, Value::BrepV(out)); return; }
          delete b;
        }
      }
      ON_SumSurface ss;
      if (ss.Create(crv, v)) { kernel::NurbsSurface k; ON_NurbsSurface ns; ss.GetNurbForm(ns); k.raw() = ns; c.Out(0, Value::Surface(k)); }
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Revolve"; d.nick = "Rev"; d.category = "Surface"; d.subcategory = "Freeform";
    d.inputs = {In("Curve", Kind::Curve), In("Axis Point", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("Axis Direction", Kind::Vector, Access::Item, Value::Vector(Vector3d(0, 0, 1)))};
    d.outputs = {Out("Brep", Kind::Brep)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).curve) return;
      ON_RevSurface* rs = ON_RevSurface::New();
      rs->m_curve = new ON_NurbsCurve(c.In(0).curve->raw());
      const Point3d p0 = c.Pt(1);
      rs->m_axis = ON_Line(p0, p0 + c.Vec(2, Vector3d(0, 0, 1)));
      rs->m_angle = ON_Interval(0, 2 * ON_PI);
      rs->m_t = rs->m_curve->Domain();
      ON_Brep* b = ON_BrepRevSurface(rs, true, true);
      if (b) { kernel::Brep out; out.raw() = *b; delete b; c.Out(0, Value::BrepV(out)); }
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Loft"; d.nick = "Loft"; d.category = "Surface"; d.subcategory = "Freeform";
    d.inputs = {In("Curves", Kind::Curve, Access::List)};
    d.outputs = {Out("Surface", Kind::Surface)};
    d.eval = [](EvalContext& c) {
      const std::vector<Value>& l = c.List(0);
      std::vector<const kernel::NurbsCurve*> curves;
      for (const Value& v : l) if (v.curve) curves.push_back(v.curve.get());
      if (curves.size() < 2) { c.Fail("Loft needs at least two curves"); return; }
      const int n = 24;
      std::vector<Point3d> grid;
      for (const auto* crv : curves) {
        const kernel::Interval dm = crv->Domain();
        for (int i = 0; i < n; ++i) grid.push_back(crv->PointAt(dm.min + (dm.max - dm.min) * i / (n - 1.0)));
      }
      const int vdeg = std::min(3, static_cast<int>(curves.size()) - 1);
      c.Out(0, Value::Surface(kernel::NurbsSurface::FromControlGrid(grid, n, static_cast<int>(curves.size()), 3, vdeg)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Boolean Union"; d.nick = "BUnion"; d.category = "Surface"; d.subcategory = "Solid";
    d.inputs = {In("Breps", Kind::Brep, Access::List)};
    d.outputs = {Out("Result", Kind::Mesh)};
    d.eval = [](EvalContext& c) {
      const std::vector<Value>& l = c.List(0);
      std::vector<kernel::Mesh> meshes;
      for (const Value& v : l) { kernel::Mesh m = MeshOfValue(v); if (m.FaceCount() > 0) meshes.push_back(m); }
      if (meshes.empty()) return;
      try {
        kernel::Mesh out = meshes[0];
        for (size_t i = 1; i < meshes.size(); ++i) out = kernel::BooleanCombine(out, meshes[i], kernel::BooleanOp::Union);
        c.Out(0, Value::MeshV(out));
      } catch (const std::exception& ex) { c.Fail(ex.what()); }
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Boolean Difference"; d.nick = "BDiff"; d.category = "Surface"; d.subcategory = "Solid";
    d.inputs = {In("A", Kind::Brep, Access::List), In("B", Kind::Brep, Access::List)};
    d.outputs = {Out("Result", Kind::Mesh)};
    d.eval = [](EvalContext& c) {
      auto collect = [](const std::vector<Value>& l) { std::vector<kernel::Mesh> ms; for (const Value& v : l) { kernel::Mesh m = MeshOfValue(v); if (m.FaceCount() > 0) ms.push_back(m); } return ms; };
      std::vector<kernel::Mesh> a = collect(c.List(0)), b = collect(c.List(1));
      if (a.empty() || b.empty()) return;
      try {
        kernel::Mesh ma = a[0]; for (size_t i = 1; i < a.size(); ++i) ma = kernel::BooleanCombine(ma, a[i], kernel::BooleanOp::Union);
        kernel::Mesh mb = b[0]; for (size_t i = 1; i < b.size(); ++i) mb = kernel::BooleanCombine(mb, b[i], kernel::BooleanOp::Union);
        c.Out(0, Value::MeshV(kernel::BooleanCombine(ma, mb, kernel::BooleanOp::Difference)));
      } catch (const std::exception& ex) { c.Fail(ex.what()); }
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Boolean Intersection"; d.nick = "BInt"; d.category = "Surface"; d.subcategory = "Solid";
    d.inputs = {In("A", Kind::Brep, Access::List), In("B", Kind::Brep, Access::List)};
    d.outputs = {Out("Result", Kind::Mesh)};
    d.eval = [](EvalContext& c) {
      auto collect = [](const std::vector<Value>& l) { std::vector<kernel::Mesh> ms; for (const Value& v : l) { kernel::Mesh m = MeshOfValue(v); if (m.FaceCount() > 0) ms.push_back(m); } return ms; };
      std::vector<kernel::Mesh> a = collect(c.List(0)), b = collect(c.List(1));
      if (a.empty() || b.empty()) return;
      try {
        kernel::Mesh ma = a[0]; for (size_t i = 1; i < a.size(); ++i) ma = kernel::BooleanCombine(ma, a[i], kernel::BooleanOp::Union);
        kernel::Mesh mb = b[0]; for (size_t i = 1; i < b.size(); ++i) mb = kernel::BooleanCombine(mb, b[i], kernel::BooleanOp::Union);
        c.Out(0, Value::MeshV(kernel::BooleanCombine(ma, mb, kernel::BooleanOp::Intersection)));
      } catch (const std::exception& ex) { c.Fail(ex.what()); }
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Area"; d.nick = "Area"; d.category = "Surface"; d.subcategory = "Analysis";
    d.inputs = {In("Geometry", Kind::Any)};
    d.outputs = {Out("Area", Kind::Number)};
    d.eval = [](EvalContext& c) {
      const Value& v = c.In(0);
      if (v.surface) { c.Out(0, Value::Number(v.surface->ApproximateArea())); return; }
      kernel::Mesh m = MeshOfValue(v);
      c.Out(0, Value::Number(m.FaceCount() > 0 ? m.Area() : 0.0));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Volume"; d.nick = "Vol"; d.category = "Surface"; d.subcategory = "Analysis";
    d.inputs = {In("Geometry", Kind::Any)};
    d.outputs = {Out("Volume", Kind::Number)};
    d.eval = [](EvalContext& c) {
      kernel::Mesh m = MeshOfValue(c.In(0));
      if (m.FaceCount() == 0 || !m.IsClosedManifold()) { c.Out(0, Value::Number(0)); return; }
      c.Out(0, Value::Number(m.Volume()));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Evaluate Surface"; d.nick = "SEval"; d.category = "Surface"; d.subcategory = "Analysis";
    d.inputs = {In("Surface", Kind::Surface), In("u", Kind::Number, Access::Item, Value::Number(0.5)), In("v", Kind::Number, Access::Item, Value::Number(0.5))};
    d.outputs = {Out("Point", Kind::Point), Out("Normal", Kind::Vector)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).surface) return;
      const auto& s = *c.In(0).surface;
      const kernel::Interval du = s.Domain(0), dv = s.Domain(1);
      const double u = du.min + (du.max - du.min) * std::clamp(c.Num(1, 0.5), 0.0, 1.0);
      const double v = dv.min + (dv.max - dv.min) * std::clamp(c.Num(2, 0.5), 0.0, 1.0);
      c.Out(0, Value::Point(s.PointAt(u, v)));
      c.Out(1, Value::Vector(s.NormalAt(u, v)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Offset Surface"; d.nick = "OffSrf"; d.category = "Surface"; d.subcategory = "Freeform";
    d.inputs = {In("Surface", Kind::Surface), In("Distance", Kind::Number, Access::Item, Value::Number(1))};
    d.outputs = {Out("Surface", Kind::Surface)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).surface) return;
      const auto& s = *c.In(0).surface;
      const double dist = c.Num(1, 1);
      const kernel::SurfaceDivisions div = s.SuggestedDivisions(0.05);
      const int nu = std::clamp(div.u, 4, 40), nv = std::clamp(div.v, 4, 40);
      std::vector<Point3d> grid;
      const kernel::Interval du = s.Domain(0), dv = s.Domain(1);
      for (int i = 0; i < nu; ++i) {
        for (int j = 0; j < nv; ++j) {
          const double u = du.min + (du.max - du.min) * i / (nu - 1.0);
          const double v = dv.min + (dv.max - dv.min) * j / (nv - 1.0);
          grid.push_back(s.PointAt(u, v) + s.NormalAt(u, v) * dist);
        }
      }
      c.Out(0, Value::Surface(kernel::NurbsSurface::FromControlGrid(grid, nv, nu, std::min(3, nv - 1), std::min(3, nu - 1))));
    };
    r.Add(d);
  }
}

void AddMesh() {
  Registry& r = Registry::Get();
  {
    NodeDef d; d.name = "Mesh From Surface"; d.nick = "Mesh"; d.category = "Mesh"; d.subcategory = "Util";
    d.inputs = {In("Geometry", Kind::Any)};
    d.outputs = {Out("Mesh", Kind::Mesh)};
    d.eval = [](EvalContext& c) { kernel::Mesh m = MeshOfValue(c.In(0)); if (m.FaceCount() > 0) c.Out(0, Value::MeshV(m)); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Mesh Box"; d.nick = "MBox"; d.category = "Mesh"; d.subcategory = "Primitive";
    d.inputs = {In("Base", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("X", Kind::Number, Access::Item, Value::Number(10)),
                In("Y", Kind::Number, Access::Item, Value::Number(10)), In("Z", Kind::Number, Access::Item, Value::Number(10))};
    d.outputs = {Out("Mesh", Kind::Mesh)};
    d.eval = [](EvalContext& c) {
      Point3d b = c.Pt(0);
      kernel::Brep br = kernel::Brep::Box(b.x, b.y, b.z, b.x + c.Num(1, 10), b.y + c.Num(2, 10), b.z + c.Num(3, 10));
      app::BrepMeshOptions opt;
      c.Out(0, Value::MeshV(app::MeshBrepClosed(br.raw(), opt)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Deconstruct Mesh"; d.nick = "MDecon"; d.category = "Mesh"; d.subcategory = "Analysis";
    d.inputs = {In("Mesh", Kind::Mesh)};
    d.outputs = {Out("Vertices", Kind::Point), Out("Face Count", Kind::Integer)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).mesh) return;
      const ON_Mesh& raw = c.In(0).mesh->raw();
      std::vector<Value> verts;
      for (int i = 0; i < raw.VertexCount(); ++i) { const ON_3dPoint p = raw.Vertex(i); verts.push_back(Value::Point(p)); }
      c.OutList(0, verts);
      c.Out(1, Value::Integer(c.In(0).mesh->FaceCount()));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Join Meshes"; d.nick = "MJoin"; d.category = "Mesh"; d.subcategory = "Util";
    d.inputs = {In("Meshes", Kind::Mesh, Access::List)};
    d.outputs = {Out("Mesh", Kind::Mesh)};
    d.eval = [](EvalContext& c) {
      std::vector<kernel::Mesh> ms;
      for (const Value& v : c.List(0)) if (v.mesh) ms.push_back(*v.mesh);
      if (ms.empty()) return;
      c.Out(0, Value::MeshV(kernel::Mesh::MergeAndWeld(ms)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Mesh Face Count"; d.nick = "MFCount"; d.category = "Mesh"; d.subcategory = "Analysis";
    d.inputs = {In("Mesh", Kind::Mesh)};
    d.outputs = {Out("Faces", Kind::Integer), Out("Vertices", Kind::Integer)};
    d.eval = [](EvalContext& c) {
      if (!c.In(0).mesh) return;
      c.Out(0, Value::Integer(c.In(0).mesh->FaceCount()));
      c.Out(1, Value::Integer(c.In(0).mesh->VertexCount()));
    };
    r.Add(d);
  }
}

}  // namespace

void RegisterSurfaceNodes() { AddSurface(); AddMesh(); }

}  // namespace dino8::flow
