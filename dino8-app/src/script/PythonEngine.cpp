#include "script/PythonEngine.h"

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

  py::tuple GetColor() const {
    Document& d = DocOf();
    const Color c = d.EffectiveColor(Need("Color"));
    return py::make_tuple(static_cast<int>(std::lround(c.r * 255)), static_cast<int>(std::lround(c.g * 255)), static_cast<int>(std::lround(c.b * 255)));
  }
  void SetColor(py::tuple rgb) {
    if (rgb.size() < 3) throw std::runtime_error("Color must be an (r, g, b) tuple");
    const Color c = Color::FromBytes(rgb[0].cast<int>(), rgb[1].cast<int>(), rgb[2].cast<int>());
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
};

// dino8.doc - just enough of RhinoCommon's RhinoDoc to reach Objects; more
// (Layers, ActiveDoc-style globals) can grow here the same way.
struct PyDoc {
  PyObjectTable objects;
};

bool RunCommand(const std::string& name, py::args args) {
  std::string line = name;
  for (const py::handle& a : args) {
    line += ' ';
    line += py::str(a).cast<std::string>();
  }
  return AppOf().Engine().RunNested(line);
}

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
      .def("AddLine", &PyObjectTable::AddLine)
      .def("AddPolyline", &PyObjectTable::AddPolyline)
      .def("AddCurve", &PyObjectTable::AddCurve, py::arg("points"), py::arg("degree") = 3)
      .def("AddBox", &PyObjectTable::AddBox, py::arg("corner"), py::arg("size"))
      .def("AddSphere", &PyObjectTable::AddSphere, py::arg("center"), py::arg("radius"))
      .def("AddCylinder", &PyObjectTable::AddCylinder, py::arg("base"), py::arg("axis"), py::arg("radius"), py::arg("cap") = true)
      .def("AddMesh", &PyObjectTable::AddMesh, py::arg("vertices"), py::arg("faces"))
      .def("Find", &PyObjectTable::Find)
      .def("Delete", &PyObjectTable::Delete)
      .def("AllObjects", &PyObjectTable::AllObjects)
      .def("GetSelectedObjects", &PyObjectTable::GetSelectedObjects);

  py::class_<PyDoc>(m, "Dino8Doc")
      .def_readonly("Objects", &PyDoc::objects);

  // A single persistent PyDoc instance, like RhinoCommon's `scriptcontext.doc`.
  m.attr("doc") = PyDoc{};

  m.def("RunCommand", &RunCommand, "Runs one Dino 8 command line by name, exactly as if typed on the command line (dino8.RunCommand('Box 0,0,0 5,5,5')).");

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
