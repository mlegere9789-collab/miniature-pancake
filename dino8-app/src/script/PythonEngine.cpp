#include "script/PythonEngine.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <fstream>
#include <filesystem>

#include "app/Application.h"
#include "commands/cmd_common.h"

#ifdef DINO8_HAVE_PYTHON
#include <pybind11/embed.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>

namespace py = pybind11;
#endif

namespace dino8::app {

#ifdef DINO8_HAVE_PYTHON

namespace {

using kernel::Point3d;
using kernel::Vector3d;

// The engine currently running a script, and the Application it runs
// against. Set for the duration of Run() (synchronous, one script at a
// time - there is no concurrent/nested Python execution), read by the
// embedded `dino8` module's free functions, which have no other way to
// reach "the document" the way LuaEngine's rs_* functions reach it through
// the lua_State registry.
PythonEngine* g_engine = nullptr;
Application* g_app = nullptr;

Application& AppOf() { return *g_app; }
Document& DocOf() { return g_app->Doc(); }

ObjectId AddObj(SceneObject o, const char* label) {
  Document& d = DocOf();
  d.BeginChange(label);
  return d.Add(std::move(o));
}

ObjectId AddCurveObj(const kernel::NurbsCurve& c, const char* label) { return AddObj(SceneObject::MakeCurve(c), label); }

// Mirrors LuaEngine.cpp's InterpolateCurve: global interpolation through
// points (InterpCrv's fixed-point relaxation), used by AddInterpCurve
// below the same way rs.AddInterpCurve uses it.
bool InterpolateCurve(const std::vector<Point3d>& pts, kernel::NurbsCurve& out) {
  if (pts.size() < 2) return false;
  if (pts.size() == 2) { out = PolylineCurve(pts); return true; }
  ON_3dPointArray arr;
  for (const Point3d& p : pts) arr.Append(p);
  ON_NurbsCurve nc;
  const int order = std::min(4, arr.Count());  // degree 3 with four or more points
  if (!nc.CreateClampedUniformNurbs(3, order, arr.Count(), arr.Array())) return false;
  out.raw() = nc;
  for (int iter = 0; iter < 30; ++iter) {
    for (int i = 0; i < arr.Count(); ++i) {
      const double t = out.raw().Domain().ParameterAt(static_cast<double>(i) / (arr.Count() - 1));
      const Point3d on = out.raw().PointAt(t);
      Point3d cv;
      out.raw().GetCV(i, cv);
      out.raw().SetCV(i, cv + (arr[i] - on));
    }
  }
  return true;
}

kernel::Brep WrapBrep(ON_Brep* b) {
  kernel::Brep k;
  if (b) { k.raw() = *b; delete b; }
  return k;
}

// 0 (kNoObject) reads as None on the Python side - see PyObjIdToPy below.
ObjectId AddBrepObj(ON_Brep* b, const char* label) {
  if (!b) return kNoObject;
  return AddObj(SceneObject::MakeBrep(WrapBrep(b)), label);
}

// Mirrors LuaEngine.cpp's ExtrudeCurveAlong: extrudes `c` along `v`, closed
// planar curves becoming capped solids and everything else a surface (the
// ExtrudeCrv rule), used by ExtrudeCurveStraight below the same way
// rs.ExtrudeCurveStraight uses it. Takes AddObj/AddBrepObj instead of a
// lua_State to reach the document, since PythonEngine has no registry to
// thread one through.
ObjectId ExtrudeCurveAlong(const kernel::NurbsCurve& kc, Vector3d v) {
  ON_NurbsCurve c = kc.raw();
  ON_Plane plane;
  if (c.IsClosed() && c.IsPlanar(&plane, DocOf().Settings().absolute_tolerance)) {
    if (ON_Brep* b = ON_BrepTrimmedPlane(plane, c)) {
      ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + v));
      if (ON_BrepExtrudeFace(*b, 0, path, true) >= 0) return AddBrepObj(b, "ExtrudeCurveStraight");
      delete b;
    }
  }
  ON_SumSurface ss;
  if (!ss.Create(c, v)) return kNoObject;
  kernel::NurbsSurface k;
  if (!SurfaceFromON(ss, k)) return kNoObject;
  return AddObj(SceneObject::MakeSurface(k), "ExtrudeCurveStraight");
}

py::object PyObjId(ObjectId id) {
  if (id == kNoObject) return py::none();
  return py::cast(id);
}

SceneObject* FindObj(ObjectId id) { return DocOf().Find(id); }

int LayerIndexArg(const py::object& layer) {
  Document& d = DocOf();
  if (py::isinstance<py::int_>(layer)) {
    const int i = layer.cast<int>();
    return (i >= 0 && i < static_cast<int>(d.Layers().size())) ? i : -1;
  }
  const std::string name = layer.cast<std::string>();
  int i = d.FindLayer(name);
  if (i < 0) {
    for (size_t k = 0; k < d.Layers().size(); ++k)
      if (d.LayerFullPath(static_cast<int>(k)) == name) return static_cast<int>(k);
  }
  return i;
}

// Mirrors LuaEngine.cpp's NeedLayer: LayerIndexArg above, but raising for a
// layer that doesn't exist instead of returning -1 - used by every
// Dino8LayerTable method that operates on one specific existing layer.
int NeedLayerIndex(const py::object& layer) {
  const int idx = LayerIndexArg(layer);
  if (idx < 0) throw std::runtime_error("layer not found");
  return idx;
}

Color ColorArg(const py::tuple& rgb) {
  if (rgb.size() < 3) throw std::runtime_error("color must be an (r, g, b) tuple");
  return Color::FromBytes(rgb[0].cast<int>(), rgb[1].cast<int>(), rgb[2].cast<int>());
}

py::tuple ColorToTuple(const Color& c) {
  return py::make_tuple(static_cast<int>(std::lround(c.r * 255)), static_cast<int>(std::lround(c.g * 255)), static_cast<int>(std::lround(c.b * 255)));
}

// Mirrors LuaEngine.cpp's UnitCode/rs_UnitSystem table exactly (Rhino's own
// unit codes: 1 Microns ... 10 Miles), so dino8.doc.UnitSystem and
// rs.UnitSystem agree on every code/name for the same document.
const char* const kUnitSystemNames[] = {"None", "Microns", "Millimeters", "Centimeters", "Meters", "Kilometers", "Microinches", "Mils", "Inches", "Feet", "Miles"};
constexpr int kUnitSystemCount = 11;

int UnitCode(const std::string& name) {
  std::string n = name;
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
  for (int i = 1; i < kUnitSystemCount; ++i) {
    std::string cand = kUnitSystemNames[i];
    std::transform(cand.begin(), cand.end(), cand.begin(), [](unsigned char c) { return std::tolower(c); });
    if (n == cand) return i;
  }
  return 0;
}

// Mirrors LuaEngine.cpp's NeedCurve/NeedMesh: fetches the object and raises
// if it doesn't exist or isn't the right kind, used by the curve/mesh query
// bindings below (CurveLength, EvaluateCurve, MeshVertices, ...).
const kernel::NurbsCurve& NeedCurvePy(ObjectId id) {
  SceneObject* o = FindObj(id);
  if (!o) throw std::runtime_error("object " + std::to_string(id) + " no longer exists");
  if (o->kind != ObjectKind::Curve || !o->curve) throw std::runtime_error("object " + std::to_string(id) + " is not a curve");
  return *o->curve;
}

const kernel::Mesh& NeedMeshPy(ObjectId id) {
  SceneObject* o = FindObj(id);
  if (!o) throw std::runtime_error("object " + std::to_string(id) + " no longer exists");
  if (o->kind != ObjectKind::Mesh || !o->mesh) throw std::runtime_error("object " + std::to_string(id) + " is not a mesh");
  return *o->mesh;
}

// Mirrors LuaEngine.cpp's TypeMaskFromArg: None/no argument means "every
// type" (mask 0), an int is taken as a raw mask, and a string is one of the
// same type names rs.ObjectType/rs.ObjectsByType accept.
int TypeMaskFromPy(const py::object& type) {
  if (type.is_none()) return 0;
  if (py::isinstance<py::int_>(type)) return type.cast<int>();
  std::string t = type.cast<std::string>();
  for (char& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (t == "point") return 1;
  if (t == "curve") return 4;
  if (t == "surface") return 8;
  if (t == "polysurface" || t == "brep") return 16;
  if (t == "mesh") return 32;
  if (t == "subd") return 262144;
  return 0;
}

// A thin reference to a document object, returned by dino8.doc.Objects.*
// so scripts can write `obj.Name = "Widget"` / `obj.Layer = "Parts"`
// (RhinoCommon's rhino3dm.CommonObject-ish surface) instead of threading
// ids through every call the way the Lua rs.* functions do. Holds only the
// id: the underlying SceneObject can move (vector reallocation) between
// calls, so every access re-resolves it via DocOf().Find().
struct PyObjectRef {
  explicit PyObjectRef(ObjectId i) : id(i) {}
  ObjectId id;

  SceneObject& Need(const char* what) const {
    SceneObject* o = FindObj(id);
    if (!o) throw std::runtime_error(std::string("object ") + std::to_string(id) + " no longer exists (" + what + ")");
    return *o;
  }

  std::string GetName() const { return Need("Name").name; }
  void SetName(const std::string& n) {
    DocOf().BeginChange("ObjectName");
    Need("Name").name = n;
  }

  std::string GetLayer() const {
    SceneObject& o = Need("Layer");
    Document& d = DocOf();
    return (o.layer_index >= 0 && o.layer_index < static_cast<int>(d.Layers().size())) ? d.Layers()[static_cast<size_t>(o.layer_index)].name : "";
  }
  void SetLayer(py::object layer) {
    const int idx = LayerIndexArg(layer);
    if (idx < 0) throw std::runtime_error("layer not found");
    DocOf().BeginChange("ObjectLayer");
    Need("Layer").layer_index = idx;
  }

  py::tuple GetColor() const { return ColorToTuple(DocOf().EffectiveColor(Need("Color"))); }
  void SetColor(py::tuple rgb) {
    const Color c = ColorArg(rgb);
    DocOf().BeginChange("ObjectColor");
    SceneObject& o = Need("Color");
    o.color = c;
    o.color_by_layer = false;
  }

  bool GetVisible() const { return Need("Visible").visible; }
  void SetVisible(bool v) { DocOf().BeginChange("HideObject"); Need("Visible").visible = v; }

  bool GetLocked() const { return Need("Locked").locked; }
  void SetLocked(bool v) { DocOf().BeginChange("LockObject"); Need("Locked").locked = v; }

  std::string GetKind() const { return ObjectKindName(Need("ObjectType").kind); }
  std::string Describe() const { return Need("Describe").Describe(); }

  void Select() { DocOf().Select(id, true); }
  void Unselect() { DocOf().Select(id, false); }
  bool IsSelected() const { SceneObject* o = FindObj(id); return o && o->selected; }

  bool Exists() const { return FindObj(id) != nullptr; }
  std::string Repr() const { return "<Dino8Object " + std::to_string(id) + " '" + GetName() + "'>"; }
};

// dino8.doc.Objects - the RhinoCommon ObjectTable equivalent. Every method
// mirrors an rs_Add*/rs_Object* pair in LuaEngine.cpp so the two engines
// stay behaviourally identical; see that file for the geometry-building
// details these thin wrappers share.
struct PyObjectTable {
  py::object AddPoint(double x, double y, double z) { return PyObjId(AddObj(SceneObject::MakePoint(Point3d(x, y, z)), "AddPoint")); }
  py::object AddPoint1(Point3d p) { return AddPoint(p.x, p.y, p.z); }

  // Mirrors rs.AddPoints(points) in LuaEngine.cpp: one point object per
  // {x,y,z}, returning the ids (an empty list for an empty input, never
  // None - same as rs.AddPoints always pushing a table).
  std::vector<ObjectId> AddPoints(std::vector<Point3d> pts) {
    Document& d = DocOf();
    d.BeginChange("AddPoints");
    std::vector<ObjectId> ids;
    for (const Point3d& p : pts) ids.push_back(d.Add(SceneObject::MakePoint(p)));
    return ids;
  }

  py::object AddLine(Point3d a, Point3d b) { return PyObjId(AddCurveObj(PolylineCurve({a, b}), "AddLine")); }

  py::object AddPolyline(std::vector<Point3d> pts) {
    if (pts.size() < 2) throw std::runtime_error("AddPolyline needs at least two points");
    return PyObjId(AddCurveObj(PolylineCurve(pts), "AddPolyline"));
  }

  py::object AddCurve(std::vector<Point3d> pts, int degree) {
    if (pts.size() < 2) throw std::runtime_error("AddCurve needs at least two points");
    if (degree <= 1) return PyObjId(AddCurveObj(PolylineCurve(pts), "AddCurve"));
    return PyObjId(AddCurveObj(kernel::NurbsCurve::FromControlPoints(pts, degree), "AddCurve"));
  }

  // Mirrors rs.AddInterpCurve(points) in LuaEngine.cpp: a degree-3 curve
  // interpolated through the points, or None for fewer than two points
  // (InterpolateCurve above returns false rather than throwing, same as
  // rs.AddInterpCurve pushes nil instead of raising a Lua error).
  py::object AddInterpCurve(std::vector<Point3d> pts) {
    kernel::NurbsCurve k;
    if (!InterpolateCurve(pts, k)) return py::none();
    return PyObjId(AddCurveObj(k, "AddInterpCurve"));
  }

  // Mirrors rs.AddCircle(center, radius, normal={0,0,1}) in LuaEngine.cpp.
  py::object AddCircle(Point3d center, double radius, py::object normal) {
    if (radius <= 0) throw std::runtime_error("AddCircle: radius must be positive");
    Vector3d n = normal.is_none() ? Vector3d(0, 0, 1) : normal.cast<Vector3d>();
    if (!n.Unitize()) n = Vector3d(0, 0, 1);
    ON_ArcCurve ac(ON_Circle(ON_Plane(center, n), radius));
    kernel::NurbsCurve k;
    if (!CurveFromON(ac, k)) return py::none();
    return PyObjId(AddCurveObj(k, "AddCircle"));
  }

  // Mirrors rs.AddArc3Pt(start, end, pointOnArc) in LuaEngine.cpp: an arc
  // through the two endpoints and a third point on it, or None when the
  // three points don't make a valid arc (same as rs.AddArc3Pt pushing nil).
  py::object AddArc3Pt(Point3d start, Point3d end, Point3d on) {
    ON_Arc arc(start, on, end);
    if (!arc.IsValid()) return py::none();
    ON_ArcCurve ac(arc);
    kernel::NurbsCurve k;
    if (!CurveFromON(ac, k)) return py::none();
    return PyObjId(AddCurveObj(k, "AddArc3Pt"));
  }

  py::object AddBox(Point3d corner, Vector3d size) {
    if (size.x == 0 || size.y == 0 || size.z == 0) throw std::runtime_error("AddBox: size must be non-zero");
    ON_3dPoint corners[8];
    const double x0 = std::min(corner.x, corner.x + size.x), x1 = std::max(corner.x, corner.x + size.x);
    const double y0 = std::min(corner.y, corner.y + size.y), y1 = std::max(corner.y, corner.y + size.y);
    const double z0 = std::min(corner.z, corner.z + size.z), z1 = std::max(corner.z, corner.z + size.z);
    corners[0] = ON_3dPoint(x0, y0, z0); corners[1] = ON_3dPoint(x1, y0, z0); corners[2] = ON_3dPoint(x1, y1, z0); corners[3] = ON_3dPoint(x0, y1, z0);
    corners[4] = ON_3dPoint(x0, y0, z1); corners[5] = ON_3dPoint(x1, y0, z1); corners[6] = ON_3dPoint(x1, y1, z1); corners[7] = ON_3dPoint(x0, y1, z1);
    return PyObjId(AddBrepObj(ON_BrepBox(corners), "AddBox"));
  }

  py::object AddSphere(Point3d center, double radius) {
    if (radius <= 0) throw std::runtime_error("AddSphere: radius must be positive");
    return PyObjId(AddBrepObj(ON_BrepSphere(ON_Sphere(center, radius)), "AddSphere"));
  }

  py::object AddCylinder(Point3d base, Vector3d axis, double radius, bool cap) {
    const double h = axis.Length();
    if (h <= 0 || radius <= 0) throw std::runtime_error("AddCylinder: height and radius must be positive");
    Vector3d dir = axis;
    dir.Unitize();
    ON_Cylinder cyl(ON_Circle(ON_Plane(base, dir), radius), h);
    return PyObjId(AddBrepObj(ON_BrepCylinder(cyl, cap, cap), "AddCylinder"));
  }

  // Mirrors rs.AddCone(base, height|apex, radius, cap=true) in
  // LuaEngine.cpp; like AddCylinder above, axis is taken as a vector
  // rather than Lua's height-or-apex-point overload.
  py::object AddCone(Point3d base, Vector3d axis, double radius, bool cap) {
    const double h = axis.Length();
    if (h <= 0 || radius <= 0) throw std::runtime_error("AddCone: height and radius must be positive");
    Vector3d dir = axis;
    dir.Unitize();
    ON_Cone cone(ON_Plane(base, dir), h, radius);
    return PyObjId(AddBrepObj(ON_BrepCone(cone, cap), "AddCone"));
  }

  // Mirrors rs.AddTorus(center, majorRadius, minorRadius, normal={0,0,1})
  // in LuaEngine.cpp.
  py::object AddTorus(Point3d center, double majorRadius, double minorRadius, py::object normal) {
    if (majorRadius <= 0 || minorRadius <= 0 || minorRadius >= majorRadius) {
      throw std::runtime_error("AddTorus: need 0 < minor radius < major radius");
    }
    Vector3d n = normal.is_none() ? Vector3d(0, 0, 1) : normal.cast<Vector3d>();
    if (!n.Unitize()) n = Vector3d(0, 0, 1);
    ON_Torus torus(ON_Plane(center, n), majorRadius, minorRadius);
    return PyObjId(AddBrepObj(ON_BrepTorus(torus), "AddTorus"));
  }

  // Mirrors rs.AddSrfPt({p0,p1,p2,p3}) in LuaEngine.cpp: a bilinear surface
  // through three or four corner points (a triangular corner is doubled,
  // same as rs_AddSrfPt), reordered into a 2x2 control grid.
  py::object AddSrfPt(std::vector<Point3d> p) {
    if (p.size() == 3) p.push_back(p[2]);
    if (p.size() != 4) throw std::runtime_error("AddSrfPt needs 3 or 4 corner points");
    std::vector<Point3d> grid = {p[0], p[1], p[3], p[2]};
    return PyObjId(AddObj(SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1)), "AddSrfPt"));
  }

  // Mirrors rs.AddPlanarSrf(curveIds) in LuaEngine.cpp: one trimmed planar
  // surface per closed, planar input curve, skipping ids that aren't
  // closed planar curves; None when nothing qualified.
  py::object AddPlanarSrf(std::vector<ObjectId> ids) {
    Document& d = DocOf();
    std::vector<ObjectId> made;
    d.BeginChange("AddPlanarSrf");
    for (ObjectId id : ids) {
      const SceneObject* o = d.Find(id);
      if (!o || o->kind != ObjectKind::Curve) continue;
      ON_Plane pl;
      if (!o->curve->raw().IsClosed() || !o->curve->raw().IsPlanar(&pl, d.Settings().absolute_tolerance)) continue;
      if (ON_Brep* b = ON_BrepTrimmedPlane(pl, o->curve->raw())) made.push_back(d.Add(SceneObject::MakeBrep(WrapBrep(b))));
    }
    if (made.empty()) return py::none();
    py::list out;
    for (ObjectId id : made) out.append(PyObjId(id));
    return out;
  }

  // Mirrors rs.ExtrudeCurveStraight(curveId, p0, p1) | (curveId, vector) in
  // LuaEngine.cpp; like AddCylinder/AddCone above, the direction is taken
  // as a single vector rather than Lua's point-pair-or-vector overload.
  py::object ExtrudeCurveStraight(ObjectId curveId, Vector3d v) {
    const SceneObject* o = DocOf().Find(curveId);
    if (!o || o->kind != ObjectKind::Curve) throw std::runtime_error("ExtrudeCurveStraight: object is not a curve");
    if (v.Length() <= 0) throw std::runtime_error("ExtrudeCurveStraight: zero-length direction");
    return PyObjId(ExtrudeCurveAlong(*o->curve, v));
  }

  // Mirrors rs.BooleanUnion(ids, delete=true) in LuaEngine.cpp: unions the
  // closed solids among `ids` (meshing each via MeshOf, same as the Lua
  // RunBoolean helper) into one mesh solid, skipping ids that aren't closed
  // solids rather than raising; None when nothing qualified.
  py::object BooleanUnion(std::vector<ObjectId> ids, bool delete_input) {
    Document& d = DocOf();
    std::vector<std::pair<ObjectId, kernel::Mesh>> meshes;
    for (ObjectId id : ids) {
      const SceneObject* o = d.Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, 0.005);
      if (!m || !m->IsClosedManifold()) continue;
      meshes.push_back({id, *m});
    }
    if (meshes.empty()) return py::none();
    kernel::Mesh result = meshes[0].second;
    const int layer = d.Find(meshes[0].first) ? d.Find(meshes[0].first)->layer_index : 0;
    for (size_t i = 1; i < meshes.size(); ++i) result = kernel::BooleanCombine(result, meshes[i].second, kernel::BooleanOp::Union);
    d.BeginChange("BooleanUnion");
    if (delete_input) for (auto& [id, m] : meshes) d.Remove(id);
    if (result.FaceCount() == 0) return py::none();
    SceneObject n = SceneObject::MakeMesh(result);
    n.layer_index = layer;
    py::list out;
    out.append(PyObjId(d.Add(std::move(n))));
    return out;
  }

  // Meshes the closed solids among `ids` (skipping anything that isn't
  // one) - the two-set counterpart of BooleanUnion's own collect loop
  // above, shared by BooleanDifference/BooleanIntersection below.
  std::vector<std::pair<ObjectId, kernel::Mesh>> CollectClosedMeshes(Document& d, const std::vector<ObjectId>& ids) {
    std::vector<std::pair<ObjectId, kernel::Mesh>> out;
    for (ObjectId id : ids) {
      const SceneObject* o = d.Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, 0.005);
      if (!m || !m->IsClosedManifold()) continue;
      out.push_back({id, *m});
    }
    return out;
  }

  // Mirrors LuaEngine.cpp's RunBoolean(op, two_sets=true): combines each of
  // `ids`/`otherIds` down to one mesh via Union, then combines those two
  // results with `op` - used by BooleanDifference/BooleanIntersection below
  // the same way rs.BooleanDifference/rs.BooleanIntersection use RunBoolean.
  py::object RunBooleanTwoSets(const std::vector<ObjectId>& ids, const std::vector<ObjectId>& otherIds, bool delete_input, kernel::BooleanOp op, const char* label) {
    Document& d = DocOf();
    std::vector<std::pair<ObjectId, kernel::Mesh>> ma = CollectClosedMeshes(d, ids);
    std::vector<std::pair<ObjectId, kernel::Mesh>> mb = CollectClosedMeshes(d, otherIds);
    if (ma.empty() || mb.empty()) return py::none();
    kernel::Mesh result = ma[0].second;
    const int layer = d.Find(ma[0].first) ? d.Find(ma[0].first)->layer_index : 0;
    for (size_t i = 1; i < ma.size(); ++i) result = kernel::BooleanCombine(result, ma[i].second, kernel::BooleanOp::Union);
    kernel::Mesh other = mb[0].second;
    for (size_t i = 1; i < mb.size(); ++i) other = kernel::BooleanCombine(other, mb[i].second, kernel::BooleanOp::Union);
    result = kernel::BooleanCombine(result, other, op);
    d.BeginChange(label);
    if (delete_input) { for (auto& [id, m] : ma) d.Remove(id); for (auto& [id, m] : mb) d.Remove(id); }
    if (result.FaceCount() == 0) return py::none();
    SceneObject n = SceneObject::MakeMesh(result);
    n.layer_index = layer;
    py::list out;
    out.append(PyObjId(d.Add(std::move(n))));
    return out;
  }

  // Mirrors rs.BooleanDifference(ids, subtractIds, delete=true) in
  // LuaEngine.cpp: subtracts the closed solids in `subtractIds` from those
  // in `ids`, returning None when either set had nothing closed to combine.
  py::object BooleanDifference(std::vector<ObjectId> ids, std::vector<ObjectId> subtractIds, bool delete_input) {
    return RunBooleanTwoSets(ids, subtractIds, delete_input, kernel::BooleanOp::Difference, "BooleanDifference");
  }

  // Mirrors rs.BooleanIntersection(ids, otherIds, delete=true) in
  // LuaEngine.cpp: keeps the volume common to both closed-solid sets.
  py::object BooleanIntersection(std::vector<ObjectId> ids, std::vector<ObjectId> otherIds, bool delete_input) {
    return RunBooleanTwoSets(ids, otherIds, delete_input, kernel::BooleanOp::Intersection, "BooleanIntersection");
  }

  // Mirrors LuaEngine.cpp's TransformIds: applies `xf` to each of `ids`,
  // either in place (copy=false) or to a duplicate added under a fresh id
  // (copy=true), skipping ids that no longer exist rather than raising -
  // shared by MoveObject/CopyObject/RotateObject/ScaleObject below the same
  // way LuaEngine.cpp's rs.MoveObject/rs.CopyObject/rs.RotateObject/
  // rs.ScaleObject share it.
  std::vector<ObjectId> TransformIds(const std::vector<ObjectId>& ids, const ON_Xform& xf, bool copy, const char* label) {
    Document& d = DocOf();
    d.BeginChange(label);
    std::vector<ObjectId> out;
    for (ObjectId id : ids) {
      SceneObject* o = d.Find(id);
      if (!o) continue;
      if (copy) {
        SceneObject dup = *o;
        dup.id = kNoObject;
        dup.selected = false;
        dup.Transform(xf);
        out.push_back(d.Add(std::move(dup)));
      } else {
        o->Transform(xf);
        out.push_back(id);
      }
    }
    d.Touch();
    return out;
  }

  // Mirrors rs.MoveObject(ids, vector) in LuaEngine.cpp: translates each of
  // `ids` in place, skipping ids that no longer exist rather than raising -
  // same as LuaEngine.cpp's TransformIds skip-missing loop.
  std::vector<ObjectId> MoveObject(std::vector<ObjectId> ids, Vector3d v) {
    return TransformIds(ids, ON_Xform::TranslationTransformation(v), false, "MoveObject");
  }

  // Mirrors rs.CopyObject(ids, vector={0,0,0}) in LuaEngine.cpp: copies each
  // of `ids`, optionally translated by `vector`, returning the new ids and
  // skipping ids that no longer exist rather than raising.
  std::vector<ObjectId> CopyObject(std::vector<ObjectId> ids, py::object vector) {
    Vector3d v = vector.is_none() ? Vector3d(0, 0, 0) : vector.cast<Vector3d>();
    return TransformIds(ids, ON_Xform::TranslationTransformation(v), true, "CopyObject");
  }

  // Mirrors rs.RotateObject(ids, center, angleDeg, axis={0,0,1}, copy=false)
  // in LuaEngine.cpp: rotates each of `ids` by `angleDeg` degrees about
  // `axis` through `center`, in place or onto copies.
  std::vector<ObjectId> RotateObject(std::vector<ObjectId> ids, Point3d center, double angleDeg, py::object axis, bool copy) {
    Vector3d a = axis.is_none() ? Vector3d(0, 0, 1) : axis.cast<Vector3d>();
    if (!a.Unitize()) a = Vector3d(0, 0, 1);
    ON_Xform xf;
    xf.Rotation(angleDeg * ON_PI / 180.0, a, center);
    return TransformIds(ids, xf, copy, "RotateObject");
  }

  // Mirrors rs.ScaleObject(ids, origin, scale, copy=false) in
  // LuaEngine.cpp: scales each of `ids` about `origin` by `scale`, in place
  // or onto copies. Like AddCylinder/AddCone above, `scale` is taken as a
  // single Vector3d rather than Lua's number-or-vector overload - pass
  // Vector3d(s, s, s) for a uniform scale.
  std::vector<ObjectId> ScaleObject(std::vector<ObjectId> ids, Point3d origin, Vector3d scale, bool copy) {
    const ON_Xform xf = ON_Xform::TranslationTransformation(origin - ON_3dPoint::Origin) * ON_Xform::DiagonalTransformation(scale.x, scale.y, scale.z) * ON_Xform::TranslationTransformation(ON_3dPoint::Origin - origin);
    return TransformIds(ids, xf, copy, "ScaleObject");
  }

  // Mirrors rs.MirrorObject(ids, start, end, copy=false) in LuaEngine.cpp:
  // mirrors each of `ids` across the vertical plane through the line
  // start-end, in place or onto copies. Throws when the line is degenerate
  // or vertical, same as rs.MirrorObject raising a Lua error in that case.
  std::vector<ObjectId> MirrorObject(std::vector<ObjectId> ids, Point3d start, Point3d end, bool copy) {
    Vector3d n = ON_CrossProduct(end - start, Vector3d(0, 0, 1));
    if (!n.Unitize()) throw std::runtime_error("MirrorObject: mirror line is degenerate or vertical");
    const ON_Xform xf = ON_Xform::MirrorTransformation(ON_PlaneEquation(n.x, n.y, n.z, -ON_DotProduct(n, Vector3d(start))));
    return TransformIds(ids, xf, copy, "MirrorObject");
  }

  // Mirrors LuaEngine.cpp's ToXform: reads a 4x4 transform given as four
  // rows of four numbers each (the same shape LuaEngine.cpp's PushXform
  // hands back from XformIdentity/XformTranslation/etc.), used by
  // TransformObject below the same way rs.TransformObject uses ToXform.
  static ON_Xform ToXform(const std::vector<std::vector<double>>& rows) {
    if (rows.size() != 4) throw std::runtime_error("TransformObject: expected a 4x4 transform");
    ON_Xform x = ON_Xform::IdentityTransformation;
    for (int r = 0; r < 4; ++r) {
      if (rows[static_cast<size_t>(r)].size() != 4) throw std::runtime_error("TransformObject: expected a 4x4 transform");
      for (int c = 0; c < 4; ++c) x.m_xform[r][c] = rows[static_cast<size_t>(r)][static_cast<size_t>(c)];
    }
    return x;
  }

  // Mirrors rs.TransformObject(ids, xform, copy=false) in LuaEngine.cpp:
  // applies an arbitrary 4x4 transform to each of `ids`, in place or onto
  // copies, sharing TransformIds with MoveObject/CopyObject/RotateObject/
  // ScaleObject/MirrorObject above.
  std::vector<ObjectId> TransformObject(std::vector<ObjectId> ids, std::vector<std::vector<double>> xform, bool copy) {
    return TransformIds(ids, ToXform(xform), copy, "TransformObject");
  }

  // Mirrors rs.SelectObject(ids) in LuaEngine.cpp: selects each of `ids`
  // (Document::Select is a silent no-op for a missing, locked or hidden
  // object), returning how many ended up selected.
  int SelectObject(std::vector<ObjectId> ids) {
    Document& d = DocOf();
    int n = 0;
    for (ObjectId id : ids) { d.Select(id, true); if (const SceneObject* o = d.Find(id)) n += o->selected ? 1 : 0; }
    return n;
  }

  // Mirrors rs.UnselectObject(ids) in LuaEngine.cpp: deselects each of
  // `ids` (a silent no-op for a missing object, same as Document::Select),
  // returning how many ids were given.
  size_t UnselectObject(std::vector<ObjectId> ids) {
    Document& d = DocOf();
    for (ObjectId id : ids) d.Select(id, false);
    return ids.size();
  }

  // Mirrors rs.UnselectAllObjects() in LuaEngine.cpp: deselects every
  // object in the document, returning how many were selected beforehand.
  size_t UnselectAllObjects() {
    Document& d = DocOf();
    const size_t n = d.SelectedCount();
    d.SelectNone();
    return n;
  }

  // Mirrors rs.ObjectsByLayer(layerName, select=false) in LuaEngine.cpp:
  // ids of the objects on a layer (by name or index, via LayerIndexArg -
  // the same lookup PyObjectRef::SetLayer uses), raising for a layer that
  // doesn't exist exactly as rs.ObjectsByLayer's NeedLayer does, and
  // optionally selecting the objects found.
  std::vector<PyObjectRef> ObjectsByLayer(py::object layer, bool select) {
    Document& d = DocOf();
    const int idx = LayerIndexArg(layer);
    if (idx < 0) throw std::runtime_error("ObjectsByLayer: layer not found");
    std::vector<ObjectId> ids;
    for (const SceneObject& o : d.Objects()) if (o.layer_index == idx) ids.push_back(o.id);
    if (select) for (ObjectId id : ids) d.Select(id, true);
    std::vector<PyObjectRef> out;
    for (ObjectId id : ids) out.emplace_back(id);
    return out;
  }

  py::object AddMesh(std::vector<Point3d> verts, std::vector<std::vector<int>> faces) {
    kernel::Mesh m;
    ON_Mesh& r = m.raw();
    for (size_t i = 0; i < verts.size(); ++i) r.SetVertex(static_cast<int>(i), verts[i]);
    const int nv = static_cast<int>(verts.size());
    for (size_t f = 0; f < faces.size(); ++f) {
      const std::vector<int>& face = faces[f];
      if (face.size() != 3 && face.size() != 4) throw std::runtime_error("AddMesh: face " + std::to_string(f) + " needs 3 or 4 vertex indices");
      int idx[4] = {0, 0, 0, 0};
      for (size_t k = 0; k < face.size(); ++k) {
        const int v = face[k];
        if (v < 0 || v >= nv) throw std::runtime_error("AddMesh: face " + std::to_string(f) + " uses vertex " + std::to_string(v) + " (0.." + std::to_string(nv - 1) + ")");
        idx[k] = v;
      }
      if (face.size() == 3) r.SetTriangle(static_cast<int>(f), idx[0], idx[1], idx[2]);
      else r.SetQuad(static_cast<int>(f), idx[0], idx[1], idx[2], idx[3]);
    }
    r.ComputeFaceNormals();
    r.ComputeVertexNormals();
    return PyObjId(AddObj(SceneObject::MakeMesh(m), "AddMesh"));
  }

  py::object Find(ObjectId id) {
    if (!FindObj(id)) return py::none();
    return py::cast(PyObjectRef(id));
  }

  bool Delete(ObjectId id) {
    Document& d = DocOf();
    if (!d.Find(id)) return false;
    d.BeginChange("DeleteObject");
    return d.Remove(id);
  }

  std::vector<PyObjectRef> AllObjects() {
    std::vector<PyObjectRef> out;
    for (const SceneObject& o : DocOf().Objects()) out.emplace_back(o.id);
    return out;
  }

  std::vector<PyObjectRef> GetSelectedObjects() {
    std::vector<PyObjectRef> out;
    for (ObjectId id : DocOf().SelectedIds()) out.emplace_back(id);
    return out;
  }

  // Mirrors rs.ObjectsByName(name, select=false) in LuaEngine.cpp: every
  // object whose Name exactly matches, optionally selecting them too.
  std::vector<PyObjectRef> ObjectsByName(const std::string& name, bool select) {
    Document& d = DocOf();
    std::vector<PyObjectRef> out;
    for (const SceneObject& o : d.Objects()) {
      if (o.name != name) continue;
      out.emplace_back(o.id);
      if (select) d.Select(o.id, true);
    }
    return out;
  }

  // Mirrors rs.ObjectsByType(type, select=false) in LuaEngine.cpp: `type`
  // is a type-name string ("curve", "surface", "polysurface"/"brep",
  // "mesh", "point", "subd"), a raw rs.ObjectType mask int, or None for
  // every object.
  std::vector<PyObjectRef> ObjectsByType(py::object type, bool select) {
    const int mask = TypeMaskFromPy(type);
    Document& d = DocOf();
    std::vector<PyObjectRef> out;
    for (const SceneObject& o : d.Objects()) {
      if (mask != 0 && !(TypeMask(o) & mask)) continue;
      out.emplace_back(o.id);
      if (select) d.Select(o.id, true);
    }
    return out;
  }

  // Mirrors rs.BoundingBox(ids) in LuaEngine.cpp: the world-axis-aligned
  // box of `ids` as its 8 corners (bottom face then top face, matching
  // Rhino's own corner order), or None when `ids` has no boxable geometry.
  py::object BoundingBox(std::vector<ObjectId> ids) {
    kernel::BoundingBox bb;
    if (!DocOf().BoundingBoxOf(ids, bb)) return py::none();
    const Point3d& a = bb.min;
    const Point3d& b = bb.max;
    py::list out;
    for (const Point3d& p : {a, Point3d(b.x, a.y, a.z), Point3d(b.x, b.y, a.z), Point3d(a.x, b.y, a.z),
                              Point3d(a.x, a.y, b.z), Point3d(b.x, a.y, b.z), b, Point3d(a.x, b.y, b.z)}) {
      out.append(p);
    }
    return out;
  }

  // ---- curve query (mirrors LuaEngine.cpp's rs.Curve* functions) --------

  double CurveLength(ObjectId curveId) { return NeedCurvePy(curveId).Length(); }

  py::tuple CurveDomain(ObjectId curveId) {
    const kernel::Interval d = NeedCurvePy(curveId).Domain();
    return py::make_tuple(d.min, d.max);
  }

  Point3d EvaluateCurve(ObjectId curveId, double t) { return NeedCurvePy(curveId).PointAt(t); }

  double CurveClosestPoint(ObjectId curveId, Point3d point) { return NeedCurvePy(curveId).ClosestPointParameter(point); }

  // Mirrors rs.DivideCurve(curveId, segments, create=false, returnPoints=true)
  // in LuaEngine.cpp: `segments` equally-arc-length-spaced parameters
  // (always including both domain ends), returned as points by default or
  // as raw parameters when returnPoints=False; create=True also adds a
  // point object at each one.
  py::object DivideCurve(ObjectId curveId, int segments, bool create, bool returnPoints) {
    const kernel::NurbsCurve& c = NeedCurvePy(curveId);
    if (segments < 1) throw std::runtime_error("DivideCurve: segments must be >= 1");
    std::vector<double> params = c.DivideByCount(segments);
    const kernel::Interval d = c.Domain();
    if (params.empty() || std::fabs(params.front() - d.min) > 1e-12) params.insert(params.begin(), d.min);
    if (std::fabs(params.back() - d.max) > 1e-12) params.push_back(d.max);
    if (create) {
      Document& doc = DocOf();
      doc.BeginChange("DivideCurve");
      for (double t : params) doc.Add(SceneObject::MakePoint(c.PointAt(t)));
    }
    py::list out;
    if (returnPoints) for (double t : params) out.append(c.PointAt(t));
    else for (double t : params) out.append(t);
    return out;
  }

  // ---- surface/mesh query (mirrors LuaEngine.cpp's rs.Surface*/rs.Mesh*) -

  // Mirrors rs.SurfaceArea(id) in LuaEngine.cpp: works for any object kind
  // that has a defined area (surface/mesh/Brep/SubD), 0 for anything else.
  double SurfaceArea(ObjectId id) { return ObjectAreaOf(Need(id, "SurfaceArea")); }

  // Mirrors rs.SurfaceVolume(id) in LuaEngine.cpp: None unless `id` is a
  // closed, manifold solid.
  py::object SurfaceVolume(ObjectId id) {
    bool closed = false;
    const double v = ObjectVolumeOf(Need(id, "SurfaceVolume"), closed);
    return closed ? py::cast(v) : py::none();
  }

  // Mirrors rs.IsObjectSolid(id) in LuaEngine.cpp: false for a missing
  // object rather than raising (unlike SurfaceArea/SurfaceVolume above).
  bool IsObjectSolid(ObjectId id) {
    SceneObject* o = FindObj(id);
    bool closed = false;
    if (o) ObjectVolumeOf(*o, closed);
    return closed;
  }

  // Mirrors rs.SurfaceClosestPoint(id, point) in LuaEngine.cpp: the exact
  // closest point for a true Surface object, or the closest point on a
  // best-effort mesh (MeshOf) for anything else meshable; None if neither
  // is possible.
  py::object SurfaceClosestPoint(ObjectId id, Point3d point) {
    SceneObject& o = Need(id, "SurfaceClosestPoint");
    if (o.kind == ObjectKind::Surface && o.surface) return py::cast(o.surface->ClosestPoint(point));
    std::optional<kernel::Mesh> m = MeshOf(o, 0.01);
    if (!m) return py::none();
    return py::cast(m->ClosestPoint(point));
  }

  std::vector<Point3d> MeshVertices(ObjectId id) {
    const ON_Mesh& m = NeedMeshPy(id).raw();
    std::vector<Point3d> pts;
    for (int i = 0; i < m.VertexCount(); ++i) pts.push_back(m.Vertex(i));
    return pts;
  }

 private:
  SceneObject& Need(ObjectId id, const char* what) {
    SceneObject* o = FindObj(id);
    if (!o) throw std::runtime_error(std::string("object ") + std::to_string(id) + " no longer exists (" + what + ")");
    return *o;
  }
};

// dino8.doc.Layers - the RhinoCommon LayerTable equivalent. Every method
// mirrors an rs_*Layer* function in LuaEngine.cpp so the two engines stay
// behaviourally identical; see that file for the layer-index-resolution
// details LayerIndexArg/NeedLayerIndex above share with it.
struct PyLayerTable {
  int Count() const { return static_cast<int>(DocOf().Layers().size()); }

  std::vector<std::string> Names() const {
    Document& d = DocOf();
    std::vector<std::string> out;
    for (size_t i = 0; i < d.Layers().size(); ++i) out.push_back(d.LayerFullPath(static_cast<int>(i)));
    return out;
  }

  bool IsLayer(py::object layer) const { return LayerIndexArg(layer) >= 0; }

  // Mirrors rs.AddLayer(name=None, color=None, visible=None, locked=None,
  // parent=None) in LuaEngine.cpp: finds an existing layer with that name
  // first (so calling it twice with the same name doesn't duplicate),
  // otherwise creates one; returns the layer's (possibly auto-generated)
  // name.
  std::string Add(py::object name, py::object color, py::object visible, py::object locked, py::object parent) {
    Document& d = DocOf();
    const std::string n = name.is_none() ? ("Layer " + std::to_string(d.Layers().size() + 1)) : name.cast<std::string>();
    const Color c = color.is_none() ? Color::FromBytes(0, 0, 0) : ColorArg(color.cast<py::tuple>());
    const int parent_idx = parent.is_none() ? -1 : NeedLayerIndex(parent);
    d.BeginChange("AddLayer");
    int idx = d.FindLayer(n);
    if (idx < 0) idx = d.AddLayer(n, c, parent_idx);
    Layer& layer = d.Layers()[static_cast<size_t>(idx)];
    if (!color.is_none()) layer.color = c;
    if (!visible.is_none()) layer.visible = visible.cast<bool>();
    if (!locked.is_none()) layer.locked = locked.cast<bool>();
    return layer.name;
  }

  std::string CurrentLayerName() const {
    Document& d = DocOf();
    return d.Layers()[static_cast<size_t>(d.CurrentLayer())].name;
  }
  void SetCurrentLayer(py::object layer) { DocOf().SetCurrentLayer(NeedLayerIndex(layer)); }

  bool Visible(py::object layer) const { return DocOf().Layers()[static_cast<size_t>(NeedLayerIndex(layer))].visible; }
  void SetVisible(py::object layer, bool v) {
    Document& d = DocOf();
    d.Layers()[static_cast<size_t>(NeedLayerIndex(layer))].visible = v;
    d.Touch();
  }

  bool Locked(py::object layer) const { return DocOf().Layers()[static_cast<size_t>(NeedLayerIndex(layer))].locked; }
  void SetLocked(py::object layer, bool v) {
    Document& d = DocOf();
    d.Layers()[static_cast<size_t>(NeedLayerIndex(layer))].locked = v;
    d.Touch();
  }

  // Named GetColor, not Color: a C++ member function named the same as the
  // `Color` type would hide that type name for unqualified lookup
  // throughout this whole class body (the Add() method above needs it).
  // The pybind11 registration below still exposes this as Python's
  // `.Color(layer)`.
  py::tuple GetColor(py::object layer) const { return ColorToTuple(DocOf().Layers()[static_cast<size_t>(NeedLayerIndex(layer))].color); }
  void SetColor(py::object layer, py::tuple rgb) {
    Document& d = DocOf();
    d.Layers()[static_cast<size_t>(NeedLayerIndex(layer))].color = ColorArg(rgb);
    d.Touch();
  }

  // Mirrors rs.DeleteLayer(layer) in LuaEngine.cpp: false for a
  // nonexistent layer, and Document::RemoveLayer itself refuses (also
  // returning false) if any object still uses the layer or it's current.
  bool Delete(py::object layer) {
    const int idx = LayerIndexArg(layer);
    if (idx < 0) return false;
    DocOf().BeginChange("DeleteLayer");
    return DocOf().RemoveLayer(idx);
  }
};

// dino8.doc - just enough of RhinoCommon's RhinoDoc to reach Objects/Layers;
// more (ActiveDoc-style globals) can grow here the same way.
//
// The document-state members below (Undo/Redo/BeginUndo/UnitSystem/Name/
// Path/Modified) mirror LuaEngine.cpp's rs_Undo/rs_Redo/rs_BeginUndo/
// rs_UnitSystem/rs_UnitSystemName/rs_DocumentName/rs_DocumentPath/
// rs_DocumentModified - previously entirely unported, per the PARITY_MAP
// note that Python scripts had no way to undo a change or inspect/change
// the document's unit system from inside a script.
struct PyDoc {
  PyObjectTable objects;
  PyLayerTable layers;

  bool Undo() { return DocOf().Undo(); }
  bool Redo() { return DocOf().Redo(); }
  void BeginUndo(const std::string& label) { DocOf().BeginChange(label); }

  int GetUnitSystem() const { return UnitCode(DocOf().Settings().unit_system); }
  void SetUnitSystem(py::object value) {
    DocumentSettings& s = DocOf().Settings();
    if (py::isinstance<py::int_>(value)) {
      const int c = value.cast<int>();
      if (c >= 0 && c < kUnitSystemCount) s.unit_system = kUnitSystemNames[c];
    } else {
      s.unit_system = value.cast<std::string>();
    }
    DocOf().Touch();
  }
  std::string UnitSystemName() const { return DocOf().Settings().unit_system; }

  std::string Name() const {
    const std::string& p = DocOf().Path();
    return p.empty() ? "Untitled" : std::filesystem::path(p).filename().string();
  }
  py::object Path() const {
    const std::string& p = DocOf().Path();
    if (p.empty()) return py::none();
    return py::cast(std::filesystem::path(p).parent_path().string());
  }

  bool GetModified() const { return DocOf().Modified(); }
  void SetModified(bool m) { DocOf().SetModified(m); }
};

bool RunCommand(const std::string& name, py::args args) {
  std::string line = name;
  for (const py::handle& a : args) {
    line += ' ';
    line += py::str(a).cast<std::string>();
  }
  return AppOf().Engine().RunNested(line);
}

// Mirrors rs.CommandHistory/rs.ClearCommandHistory/rs.Version/
// rs.LastCommandName in LuaEngine.cpp - module-level (not dino8.doc.*,
// since they report on the command line/engine, not the document), and
// previously entirely unported to Python per the PARITY_MAP note on
// document-state functions.
std::string CommandHistory() {
  std::string all;
  for (const std::string& line : AppOf().Engine().History()) { all += line; all += '\n'; }
  return all;
}
void ClearCommandHistory() { AppOf().Engine().ClearHistory(); }
std::string Version() { return "Dino 8 " DINO8_VERSION " (Python " PY_VERSION ")"; }
std::string LastCommandName() { return AppOf().Engine().LastCommand(); }

// Buffers Python's sys.stdout/sys.stderr writes and forwards them to the
// engine one line at a time (print() issues one write() per argument/sep
// plus one for the trailing newline, so lines have to be reassembled here
// rather than treating every write() as its own line).
void EmitStdout(const std::string& text) {
  if (!g_engine) return;
  g_engine->FeedStdout(text);
}

}  // namespace

PYBIND11_EMBEDDED_MODULE(dino8, m) {
  m.doc() = "Dino 8 scripting API (RhinoCommon-flavoured).";

  py::class_<Point3d>(m, "Point3d")
      .def(py::init<>())
      .def(py::init<double, double, double>())
      .def_readwrite("X", &Point3d::x)
      .def_readwrite("Y", &Point3d::y)
      .def_readwrite("Z", &Point3d::z)
      .def("DistanceTo", [](const Point3d& a, const Point3d& b) { return a.DistanceTo(b); })
      .def("__add__", [](const Point3d& p, const Vector3d& v) { return Point3d(p.x + v.x, p.y + v.y, p.z + v.z); })
      .def("__sub__", [](const Point3d& a, const Point3d& b) { return Vector3d(a.x - b.x, a.y - b.y, a.z - b.z); })
      .def("__sub__", [](const Point3d& p, const Vector3d& v) { return Point3d(p.x - v.x, p.y - v.y, p.z - v.z); })
      .def("__repr__", [](const Point3d& p) { return "Point3d(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z) + ")"; });

  py::class_<Vector3d>(m, "Vector3d")
      .def(py::init<>())
      .def(py::init<double, double, double>())
      .def_readwrite("X", &Vector3d::x)
      .def_readwrite("Y", &Vector3d::y)
      .def_readwrite("Z", &Vector3d::z)
      .def_property_readonly("Length", [](const Vector3d& v) { return v.Length(); })
      .def("Unitize", [](Vector3d& v) { return v.Unitize(); })
      .def("__add__", [](const Vector3d& a, const Vector3d& b) { return Vector3d(a.x + b.x, a.y + b.y, a.z + b.z); })
      .def("__sub__", [](const Vector3d& a, const Vector3d& b) { return Vector3d(a.x - b.x, a.y - b.y, a.z - b.z); })
      .def("__mul__", [](const Vector3d& v, double s) { return Vector3d(v.x * s, v.y * s, v.z * s); })
      .def("__rmul__", [](const Vector3d& v, double s) { return Vector3d(v.x * s, v.y * s, v.z * s); })
      .def("__repr__", [](const Vector3d& v) { return "Vector3d(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")"; });

  py::class_<PyObjectRef>(m, "Dino8Object")
      .def_property("Name", &PyObjectRef::GetName, &PyObjectRef::SetName)
      .def_property("Layer", &PyObjectRef::GetLayer, &PyObjectRef::SetLayer)
      .def_property("Color", &PyObjectRef::GetColor, &PyObjectRef::SetColor)
      .def_property("Visible", &PyObjectRef::GetVisible, &PyObjectRef::SetVisible)
      .def_property("Locked", &PyObjectRef::GetLocked, &PyObjectRef::SetLocked)
      .def_property_readonly("ObjectType", &PyObjectRef::GetKind)
      .def_property_readonly("Id", [](const PyObjectRef& o) { return o.id; })
      .def_property_readonly("IsSelected", &PyObjectRef::IsSelected)
      .def_property_readonly("Exists", &PyObjectRef::Exists)
      .def("Select", &PyObjectRef::Select)
      .def("Unselect", &PyObjectRef::Unselect)
      .def("Describe", &PyObjectRef::Describe)
      .def("__repr__", &PyObjectRef::Repr);

  py::class_<PyObjectTable>(m, "Dino8ObjectTable")
      .def("AddPoint", py::overload_cast<double, double, double>(&PyObjectTable::AddPoint))
      .def("AddPoint", &PyObjectTable::AddPoint1)
      .def("AddPoints", &PyObjectTable::AddPoints)
      .def("AddLine", &PyObjectTable::AddLine)
      .def("AddPolyline", &PyObjectTable::AddPolyline)
      .def("AddCurve", &PyObjectTable::AddCurve, py::arg("points"), py::arg("degree") = 3)
      .def("AddInterpCurve", &PyObjectTable::AddInterpCurve)
      .def("AddCircle", &PyObjectTable::AddCircle, py::arg("center"), py::arg("radius"), py::arg("normal") = py::none())
      .def("AddArc3Pt", &PyObjectTable::AddArc3Pt)
      .def("AddSrfPt", &PyObjectTable::AddSrfPt)
      .def("AddPlanarSrf", &PyObjectTable::AddPlanarSrf)
      .def("ExtrudeCurveStraight", &PyObjectTable::ExtrudeCurveStraight, py::arg("curveId"), py::arg("vector"))
      .def("BooleanUnion", &PyObjectTable::BooleanUnion, py::arg("ids"), py::arg("delete") = true)
      .def("BooleanDifference", &PyObjectTable::BooleanDifference, py::arg("ids"), py::arg("subtractIds"), py::arg("delete") = true)
      .def("BooleanIntersection", &PyObjectTable::BooleanIntersection, py::arg("ids"), py::arg("otherIds"), py::arg("delete") = true)
      .def("MoveObject", &PyObjectTable::MoveObject, py::arg("ids"), py::arg("vector"))
      .def("CopyObject", &PyObjectTable::CopyObject, py::arg("ids"), py::arg("vector") = py::none())
      .def("RotateObject", &PyObjectTable::RotateObject, py::arg("ids"), py::arg("center"), py::arg("angleDeg"), py::arg("axis") = py::none(), py::arg("copy") = false)
      .def("ScaleObject", &PyObjectTable::ScaleObject, py::arg("ids"), py::arg("origin"), py::arg("scale"), py::arg("copy") = false)
      .def("MirrorObject", &PyObjectTable::MirrorObject, py::arg("ids"), py::arg("start"), py::arg("end"), py::arg("copy") = false)
      .def("TransformObject", &PyObjectTable::TransformObject, py::arg("ids"), py::arg("xform"), py::arg("copy") = false)
      .def("SelectObject", &PyObjectTable::SelectObject, py::arg("ids"))
      .def("UnselectObject", &PyObjectTable::UnselectObject, py::arg("ids"))
      .def("UnselectAllObjects", &PyObjectTable::UnselectAllObjects)
      .def("ObjectsByLayer", &PyObjectTable::ObjectsByLayer, py::arg("layer"), py::arg("select") = false)
      .def("AddBox", &PyObjectTable::AddBox, py::arg("corner"), py::arg("size"))
      .def("AddSphere", &PyObjectTable::AddSphere, py::arg("center"), py::arg("radius"))
      .def("AddCylinder", &PyObjectTable::AddCylinder, py::arg("base"), py::arg("axis"), py::arg("radius"), py::arg("cap") = true)
      .def("AddCone", &PyObjectTable::AddCone, py::arg("base"), py::arg("axis"), py::arg("radius"), py::arg("cap") = true)
      .def("AddTorus", &PyObjectTable::AddTorus, py::arg("center"), py::arg("majorRadius"), py::arg("minorRadius"), py::arg("normal") = py::none())
      .def("AddMesh", &PyObjectTable::AddMesh, py::arg("vertices"), py::arg("faces"))
      .def("Find", &PyObjectTable::Find)
      .def("Delete", &PyObjectTable::Delete)
      .def("AllObjects", &PyObjectTable::AllObjects)
      .def("GetSelectedObjects", &PyObjectTable::GetSelectedObjects)
      .def("ObjectsByName", &PyObjectTable::ObjectsByName, py::arg("name"), py::arg("select") = false)
      .def("ObjectsByType", &PyObjectTable::ObjectsByType, py::arg("type") = py::none(), py::arg("select") = false)
      .def("BoundingBox", &PyObjectTable::BoundingBox, py::arg("ids"))
      .def("CurveLength", &PyObjectTable::CurveLength, py::arg("curveId"))
      .def("CurveDomain", &PyObjectTable::CurveDomain, py::arg("curveId"))
      .def("EvaluateCurve", &PyObjectTable::EvaluateCurve, py::arg("curveId"), py::arg("t"))
      .def("CurveClosestPoint", &PyObjectTable::CurveClosestPoint, py::arg("curveId"), py::arg("point"))
      .def("DivideCurve", &PyObjectTable::DivideCurve, py::arg("curveId"), py::arg("segments"), py::arg("create") = false, py::arg("returnPoints") = true)
      .def("SurfaceArea", &PyObjectTable::SurfaceArea, py::arg("id"))
      .def("SurfaceVolume", &PyObjectTable::SurfaceVolume, py::arg("id"))
      .def("IsObjectSolid", &PyObjectTable::IsObjectSolid, py::arg("id"))
      .def("SurfaceClosestPoint", &PyObjectTable::SurfaceClosestPoint, py::arg("id"), py::arg("point"))
      .def("MeshVertices", &PyObjectTable::MeshVertices, py::arg("id"));

  py::class_<PyLayerTable>(m, "Dino8LayerTable")
      .def("Count", &PyLayerTable::Count)
      .def("Names", &PyLayerTable::Names)
      .def("IsLayer", &PyLayerTable::IsLayer, py::arg("layer"))
      .def("Add", &PyLayerTable::Add, py::arg("name") = py::none(), py::arg("color") = py::none(), py::arg("visible") = py::none(), py::arg("locked") = py::none(), py::arg("parent") = py::none())
      .def_property("CurrentLayer", &PyLayerTable::CurrentLayerName, &PyLayerTable::SetCurrentLayer)
      .def("Visible", &PyLayerTable::Visible, py::arg("layer"))
      .def("SetVisible", &PyLayerTable::SetVisible, py::arg("layer"), py::arg("visible"))
      .def("Locked", &PyLayerTable::Locked, py::arg("layer"))
      .def("SetLocked", &PyLayerTable::SetLocked, py::arg("layer"), py::arg("locked"))
      .def("Color", &PyLayerTable::GetColor, py::arg("layer"))
      .def("SetColor", &PyLayerTable::SetColor, py::arg("layer"), py::arg("color"))
      .def("Delete", &PyLayerTable::Delete, py::arg("layer"));

  py::class_<PyDoc>(m, "Dino8Doc")
      .def_readonly("Objects", &PyDoc::objects)
      .def_readonly("Layers", &PyDoc::layers)
      .def("Undo", &PyDoc::Undo)
      .def("Redo", &PyDoc::Redo)
      .def("BeginUndo", &PyDoc::BeginUndo, py::arg("label") = "Script")
      .def_property("UnitSystem", &PyDoc::GetUnitSystem, &PyDoc::SetUnitSystem)
      .def_property_readonly("UnitSystemName", &PyDoc::UnitSystemName)
      .def_property_readonly("Name", &PyDoc::Name)
      .def_property_readonly("Path", &PyDoc::Path)
      .def_property("Modified", &PyDoc::GetModified, &PyDoc::SetModified);

  // A single persistent PyDoc instance, like RhinoCommon's `scriptcontext.doc`.
  m.attr("doc") = PyDoc{};

  m.def("RunCommand", &RunCommand, "Runs one Dino 8 command line by name, exactly as if typed on the command line (dino8.RunCommand('Box 0,0,0 5,5,5')).");
  m.def("CommandHistory", &CommandHistory, "Every command-line history line so far, newline-separated.");
  m.def("ClearCommandHistory", &ClearCommandHistory, "Clears the command-line history.");
  m.def("Version", &Version, "The running Dino 8 version plus the embedded Python version.");
  m.def("LastCommandName", &LastCommandName, "The name of the most recently run command.");

  // Internal: sys.stdout/sys.stderr are redirected to this on construction
  // (see PythonEngine::PythonEngine) so print() output reaches the command
  // history the same way rs.* Lua scripts' print() does.
  m.def("_emit_stdout", &EmitStdout);
}

#endif  // DINO8_HAVE_PYTHON

namespace {
#ifdef DINO8_HAVE_PYTHON
bool g_interpreter_started = false;
#endif
}  // namespace

PythonEngine::PythonEngine(Application& app) : app_(app) {
#ifdef DINO8_HAVE_PYTHON
  if (!g_interpreter_started) {
    static py::scoped_interpreter interpreter;  // lives for the process; never finalized early
    g_interpreter_started = true;
  }
  try {
    py::exec(R"PY(
import sys
import dino8 as _dino8

class _Dino8Stdout:
    def write(self, s):
        _dino8._emit_stdout(s)
    def flush(self):
        pass

sys.stdout = _Dino8Stdout()
sys.stderr = _Dino8Stdout()
)PY");
  } catch (const py::error_already_set&) {
    // Should not happen (the embedded module is always available once the
    // interpreter is up); if it ever does, print() output just won't be
    // captured into the command history - scripts still run.
  }
#endif
}

PythonEngine::~PythonEngine() = default;

bool PythonEngine::Available() {
#ifdef DINO8_HAVE_PYTHON
  return true;
#else
  return false;
#endif
}

void PythonEngine::Print(const std::string& line) {
  output_.push_back(line);
  if (output_.size() > 500) output_.erase(output_.begin());
  app_.Engine().Print(line);
}

bool PythonEngine::Run(const std::string& code, const std::string& chunk_name, bool as_expression) {
#ifndef DINO8_HAVE_PYTHON
  (void)code; (void)chunk_name; (void)as_expression;
  Print("! Python error: this build of Dino 8 has no embedded Python interpreter (no Python 3 development install was found when it was built)");
  return false;
#else
  output_.clear();
  g_engine = this;
  g_app = &app_;
  bool ok = true;
  try {
    py::object main_module = py::module_::import("__main__");
    py::dict globals = main_module.attr("__dict__");
    bool handled = false;
    if (as_expression) {
      try {
        py::object result = py::eval(code, globals, globals);
        if (!result.is_none()) Print(py::str(result).cast<std::string>());
        handled = true;
      } catch (const py::error_already_set&) {
        PyErr_Clear();  // not a bare expression - fall through and exec it as a statement
      }
    }
    if (!handled) py::exec(code, globals, globals);
  } catch (const py::error_already_set& e) {
    Print("! Python error in " + chunk_name + ": " + std::string(e.what()));
    ok = false;
  } catch (const std::exception& e) {
    Print("! Python error in " + chunk_name + ": " + std::string(e.what()));
    ok = false;
  }
  if (!print_buffer_.empty()) { Print(print_buffer_); print_buffer_.clear(); }
  g_engine = nullptr;
  g_app = nullptr;
  return ok;
#endif
}

void PythonEngine::FeedStdout(const std::string& text) {
  print_buffer_ += text;
  size_t pos;
  while ((pos = print_buffer_.find('\n')) != std::string::npos) {
    Print(print_buffer_.substr(0, pos));
    print_buffer_.erase(0, pos + 1);
  }
}

bool PythonEngine::Start(const std::string& code, const std::string& chunk_name) {
  return Run(code, "@" + chunk_name, false);
}

bool PythonEngine::StartExpression(const std::string& expr) {
  return Run(expr, "=command line", true);
}

bool PythonEngine::StartFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    Print("! Python error: cannot open " + path);
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return Start(ss.str(), std::filesystem::path(path).filename().string());
}

}  // namespace dino8::app
