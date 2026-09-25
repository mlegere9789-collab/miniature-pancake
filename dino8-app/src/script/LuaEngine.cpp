#include "script/LuaEngine.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "commands/cmd_common.h"

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

namespace {

constexpr const char* kEngineKey = "dino8.LuaEngine";

LuaEngine& Eng(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kEngineKey);
  LuaEngine* e = static_cast<LuaEngine*>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return *e;
}

Application& AppOf(lua_State* L) { return Eng(L).App(); }
Document& DocOf(lua_State* L) { return Eng(L).App().Doc(); }

// ---- argument helpers -------------------------------------------------------

double FieldNumber(lua_State* L, int idx, int i, const char* name, bool& ok) {
  lua_rawgeti(L, idx, i);
  if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_getfield(L, idx, name); }
  double v = 0;
  if (lua_isnumber(L, -1)) v = lua_tonumber(L, -1); else ok = false;
  lua_pop(L, 1);
  return v;
}

// A point/vector argument: {x,y,z}, {x=..,y=..,z=..} or {x,y} (z = 0).
Point3d ToPoint(lua_State* L, int idx) {
  idx = lua_absindex(L, idx);
  if (!lua_istable(L, idx)) luaL_error(L, "argument %d: expected a point {x,y,z}", idx);
  bool ok = true;
  const double x = FieldNumber(L, idx, 1, "x", ok);
  const double y = FieldNumber(L, idx, 2, "y", ok);
  bool okz = true;
  const double z = FieldNumber(L, idx, 3, "z", okz);
  if (!ok) luaL_error(L, "argument %d: expected a point {x,y,z}", idx);
  return Point3d(x, y, okz ? z : 0.0);
}

Vector3d ToVector(lua_State* L, int idx) { return Vector3d(ToPoint(L, idx)); }

std::vector<Point3d> ToPoints(lua_State* L, int idx) {
  idx = lua_absindex(L, idx);
  if (!lua_istable(L, idx)) luaL_error(L, "argument %d: expected a list of points", idx);
  std::vector<Point3d> pts;
  const lua_Integer n = luaL_len(L, idx);
  for (lua_Integer i = 1; i <= n; ++i) {
    lua_rawgeti(L, idx, i);
    pts.push_back(ToPoint(L, -1));
    lua_pop(L, 1);
  }
  return pts;
}

// Points come back as both {x,y,z} array entries (rhinoscriptsyntax/VBScript
// style, so p[1]/p[2]/p[3] works) and named x/y/z fields (nicer to read).
void PushPoint(lua_State* L, Point3d p) {
  lua_createtable(L, 3, 3);
  lua_pushnumber(L, p.x); lua_rawseti(L, -2, 1); lua_pushnumber(L, p.x); lua_setfield(L, -2, "x");
  lua_pushnumber(L, p.y); lua_rawseti(L, -2, 2); lua_pushnumber(L, p.y); lua_setfield(L, -2, "y");
  lua_pushnumber(L, p.z); lua_rawseti(L, -2, 3); lua_pushnumber(L, p.z); lua_setfield(L, -2, "z");
}

void PushPoints(lua_State* L, const std::vector<Point3d>& pts) {
  lua_createtable(L, static_cast<int>(pts.size()), 0);
  for (size_t i = 0; i < pts.size(); ++i) { PushPoint(L, pts[i]); lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1)); }
}

ObjectId ToId(lua_State* L, int idx) {
  if (lua_type(L, idx) == LUA_TSTRING) return static_cast<ObjectId>(std::strtoull(lua_tostring(L, idx), nullptr, 10));
  return static_cast<ObjectId>(luaL_checkinteger(L, idx));
}

// One id or a list of ids.
std::vector<ObjectId> ToIds(lua_State* L, int idx) {
  std::vector<ObjectId> ids;
  if (lua_isnoneornil(L, idx)) return ids;
  if (lua_istable(L, idx)) {
    const lua_Integer n = luaL_len(L, idx);
    for (lua_Integer i = 1; i <= n; ++i) { lua_rawgeti(L, idx, i); ids.push_back(ToId(L, -1)); lua_pop(L, 1); }
  } else {
    ids.push_back(ToId(L, idx));
  }
  return ids;
}

void PushIds(lua_State* L, const std::vector<ObjectId>& ids) {
  lua_createtable(L, static_cast<int>(ids.size()), 0);
  for (size_t i = 0; i < ids.size(); ++i) { lua_pushinteger(L, static_cast<lua_Integer>(ids[i])); lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1)); }
}

void PushId(lua_State* L, ObjectId id) {
  if (id == kNoObject) lua_pushnil(L); else lua_pushinteger(L, static_cast<lua_Integer>(id));
}

SceneObject* Obj(lua_State* L, int idx) { return DocOf(L).Find(ToId(L, idx)); }

SceneObject& NeedObj(lua_State* L, int idx) {
  SceneObject* o = Obj(L, idx);
  if (!o) luaL_error(L, "object %s not found", lua_tostring(L, idx) ? lua_tostring(L, idx) : "?");
  return *o;
}

Color ToColor(lua_State* L, int idx) {
  idx = lua_absindex(L, idx);
  if (lua_isinteger(L, idx)) {  // packed 0xRRGGBB
    const lua_Integer v = lua_tointeger(L, idx);
    return Color::FromBytes(static_cast<int>((v >> 16) & 255), static_cast<int>((v >> 8) & 255), static_cast<int>(v & 255));
  }
  if (!lua_istable(L, idx)) luaL_error(L, "argument %d: expected a colour {r,g,b}", idx);
  bool ok = true;
  const double r = FieldNumber(L, idx, 1, "r", ok), g = FieldNumber(L, idx, 2, "g", ok), b = FieldNumber(L, idx, 3, "b", ok);
  if (!ok) luaL_error(L, "argument %d: expected a colour {r,g,b}", idx);
  return Color::FromBytes(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
}

void PushColor(lua_State* L, const Color& c) {
  lua_createtable(L, 3, 0);
  lua_pushinteger(L, static_cast<lua_Integer>(std::lround(c.r * 255))); lua_rawseti(L, -2, 1);
  lua_pushinteger(L, static_cast<lua_Integer>(std::lround(c.g * 255))); lua_rawseti(L, -2, 2);
  lua_pushinteger(L, static_cast<lua_Integer>(std::lround(c.b * 255))); lua_rawseti(L, -2, 3);
}

ON_Xform ToXform(lua_State* L, int idx) {
  idx = lua_absindex(L, idx);
  if (!lua_istable(L, idx)) luaL_error(L, "argument %d: expected a 4x4 transform", idx);
  ON_Xform x = ON_Xform::IdentityTransformation;
  for (int r = 0; r < 4; ++r) {
    lua_rawgeti(L, idx, r + 1);
    if (!lua_istable(L, -1)) luaL_error(L, "argument %d: expected a 4x4 transform", idx);
    for (int c = 0; c < 4; ++c) { lua_rawgeti(L, -1, c + 1); x.m_xform[r][c] = luaL_checknumber(L, -1); lua_pop(L, 1); }
    lua_pop(L, 1);
  }
  return x;
}

void PushXform(lua_State* L, const ON_Xform& x) {
  lua_createtable(L, 4, 0);
  for (int r = 0; r < 4; ++r) {
    lua_createtable(L, 4, 0);
    for (int c = 0; c < 4; ++c) { lua_pushnumber(L, x.m_xform[r][c]); lua_rawseti(L, -2, c + 1); }
    lua_rawseti(L, -2, r + 1);
  }
}

ObjectId AddObj(lua_State* L, SceneObject o, const char* label) {
  Document& d = DocOf(L);
  d.BeginChange(label);
  return d.Add(std::move(o));
}

ObjectId AddCurveObj(lua_State* L, const kernel::NurbsCurve& c, const char* label) { return AddObj(L, SceneObject::MakeCurve(c), label); }

kernel::Brep WrapBrep(ON_Brep* b) {
  kernel::Brep k;
  if (b) { k.raw() = *b; delete b; }
  return k;
}

int PushBrep(lua_State* L, ON_Brep* b, const char* label) {
  if (!b) { lua_pushnil(L); return 1; }
  PushId(L, AddObj(L, SceneObject::MakeBrep(WrapBrep(b)), label));
  return 1;
}

// Rhino object type mask (rs.ObjectType values).
int TypeMask(const SceneObject& o) {
  switch (o.kind) {
    case ObjectKind::Point: return 1;
    case ObjectKind::Curve: return 4;
    case ObjectKind::Surface: return 8;
    case ObjectKind::Brep: return (o.brep && o.brep->FaceCount() == 1) ? 8 : 16;
    case ObjectKind::Mesh: return 32;
    case ObjectKind::SubD: return 262144;
  }
  return 0;
}

int TypeMaskFromArg(lua_State* L, int idx) {
  if (lua_isnoneornil(L, idx)) return 0;
  if (lua_isnumber(L, idx)) return static_cast<int>(lua_tointeger(L, idx));
  const std::string t = ToLower(luaL_checkstring(L, idx));
  if (t == "point") return 1;
  if (t == "curve") return 4;
  if (t == "surface") return 8;
  if (t == "polysurface" || t == "brep") return 16;
  if (t == "mesh") return 32;
  if (t == "subd") return 262144;
  return 0;
}

double ObjectAreaOf(const SceneObject& o) {
  switch (o.kind) {
    case ObjectKind::Surface: return o.surface ? o.surface->ApproximateArea() : 0;
    case ObjectKind::Mesh: return o.mesh ? o.mesh->Area() : 0;
    case ObjectKind::Brep: { std::optional<kernel::Mesh> m = MeshOf(o, 0.005); return m ? m->Area() : 0; }
    case ObjectKind::SubD: return o.subd ? o.subd->ToApproximateMesh().Area() : 0;
    default: return 0;
  }
}

double ObjectVolumeOf(const SceneObject& o, bool& closed) {
  std::optional<kernel::Mesh> m = MeshOf(o, 0.005);
  closed = m && m->IsClosedManifold();
  return closed ? std::fabs(m->Volume()) : 0;
}

int LayerIndexArg(lua_State* L, int idx) {
  Document& d = DocOf(L);
  if (lua_isnumber(L, idx)) {
    const int i = static_cast<int>(lua_tointeger(L, idx));
    return (i >= 0 && i < static_cast<int>(d.Layers().size())) ? i : -1;
  }
  const char* name = luaL_checkstring(L, idx);
  int i = d.FindLayer(name);
  if (i < 0) for (size_t k = 0; k < d.Layers().size(); ++k) if (d.LayerFullPath(static_cast<int>(k)) == name) return static_cast<int>(k);
  return i;
}

int NeedLayer(lua_State* L, int idx) {
  const int i = LayerIndexArg(L, idx);
  if (i < 0) luaL_error(L, "layer '%s' not found", lua_tostring(L, idx) ? lua_tostring(L, idx) : "?");
  return i;
}

void TransformIds(lua_State* L, const std::vector<ObjectId>& ids, const ON_Xform& xf, bool copy, const char* label, std::vector<ObjectId>& out) {
  Document& d = DocOf(L);
  d.BeginChange(label);
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
}

// Returns one id when given one, a list when given a list.
int PushLike(lua_State* L, int arg_idx, const std::vector<ObjectId>& ids) {
  if (lua_istable(L, arg_idx)) { PushIds(L, ids); return 1; }
  if (ids.empty()) lua_pushnil(L); else PushId(L, ids.front());
  return 1;
}

// Extrudes a curve along `v`: closed planar curves become capped solids,
// everything else a surface (the ExtrudeCrv rule).
ObjectId ExtrudeCurveAlong(lua_State* L, const kernel::NurbsCurve& kc, Vector3d v) {
  ON_NurbsCurve c = kc.raw();
  ON_Plane plane;
  if (c.IsClosed() && c.IsPlanar(&plane, DocOf(L).Settings().absolute_tolerance)) {
    if (ON_Brep* b = ON_BrepTrimmedPlane(plane, c)) {
      ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + v));
      if (ON_BrepExtrudeFace(*b, 0, path, true) >= 0) return AddObj(L, SceneObject::MakeBrep(WrapBrep(b)), "ExtrudeCurveStraight");
      delete b;
    }
  }
  ON_SumSurface ss;
  if (!ss.Create(c, v)) return kNoObject;
  kernel::NurbsSurface k;
  if (!SurfaceFromON(ss, k)) return kNoObject;
  return AddObj(L, SceneObject::MakeSurface(k), "ExtrudeCurveStraight");
}

// Global interpolation through points (InterpCrv's fixed-point relaxation).
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

bool NeedScriptContext(lua_State* L, const char* fn) {
  if (!lua_isyieldable(L)) luaL_error(L, "rs.%s can only prompt from a running script (RunScript)", fn);
  return true;
}

// ---- rs.* functions ------------------------------------------------------------

int rs_Command(lua_State* L) {
  const char* line = luaL_checkstring(L, 1);
  lua_pushboolean(L, AppOf(L).Engine().RunNested(line));
  return 1;
}

int rs_AddPoint(lua_State* L) {
  Point3d p = lua_istable(L, 1) ? ToPoint(L, 1) : Point3d(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_optnumber(L, 3, 0));
  PushId(L, AddObj(L, SceneObject::MakePoint(p), "AddPoint"));
  return 1;
}

int rs_AddPoints(lua_State* L) {
  std::vector<Point3d> pts = ToPoints(L, 1);
  Document& d = DocOf(L);
  d.BeginChange("AddPoints");
  std::vector<ObjectId> ids;
  for (const Point3d& p : pts) ids.push_back(d.Add(SceneObject::MakePoint(p)));
  PushIds(L, ids);
  return 1;
}

int rs_AddLine(lua_State* L) {
  const Point3d a = ToPoint(L, 1), b = ToPoint(L, 2);
  if ((b - a).Length() <= 0) { lua_pushnil(L); return 1; }
  PushId(L, AddCurveObj(L, PolylineCurve({a, b}), "AddLine"));
  return 1;
}

int rs_AddPolyline(lua_State* L) {
  std::vector<Point3d> pts = ToPoints(L, 1);
  if (pts.size() < 2) return luaL_error(L, "AddPolyline needs at least two points");
  PushId(L, AddCurveObj(L, PolylineCurve(pts), "AddPolyline"));
  return 1;
}

int rs_AddCurve(lua_State* L) {
  std::vector<Point3d> pts = ToPoints(L, 1);
  int degree = static_cast<int>(luaL_optinteger(L, 2, 3));
  if (pts.size() < 2) return luaL_error(L, "AddCurve needs at least two points");
  degree = std::max(1, std::min(degree, static_cast<int>(pts.size()) - 1));
  if (degree == 1) PushId(L, AddCurveObj(L, PolylineCurve(pts), "AddCurve"));
  else PushId(L, AddCurveObj(L, kernel::NurbsCurve::FromControlPoints(pts, degree), "AddCurve"));
  return 1;
}

int rs_AddInterpCurve(lua_State* L) {
  std::vector<Point3d> pts = ToPoints(L, 1);
  kernel::NurbsCurve k;
  if (!InterpolateCurve(pts, k)) { lua_pushnil(L); return 1; }
  PushId(L, AddCurveObj(L, k, "AddInterpCurve"));
  return 1;
}

int rs_AddCircle(lua_State* L) {
  const Point3d c = ToPoint(L, 1);
  const double r = luaL_checknumber(L, 2);
  if (r <= 0) return luaL_error(L, "AddCircle: radius must be positive");
  Vector3d n = lua_isnoneornil(L, 3) ? Vector3d(0, 0, 1) : ToVector(L, 3);
  if (!n.Unitize()) n = Vector3d(0, 0, 1);
  ON_ArcCurve ac(ON_Circle(ON_Plane(c, n), r));
  kernel::NurbsCurve k;
  if (!CurveFromON(ac, k)) { lua_pushnil(L); return 1; }
  PushId(L, AddCurveObj(L, k, "AddCircle"));
  return 1;
}

int rs_AddArc3Pt(lua_State* L) {
  const Point3d start = ToPoint(L, 1), end = ToPoint(L, 2), on = ToPoint(L, 3);
  ON_Arc arc(start, on, end);
  if (!arc.IsValid()) { lua_pushnil(L); return 1; }
  ON_ArcCurve ac(arc);
  kernel::NurbsCurve k;
  if (!CurveFromON(ac, k)) { lua_pushnil(L); return 1; }
  PushId(L, AddCurveObj(L, k, "AddArc3Pt"));
  return 1;
}

int rs_AddBox(lua_State* L) {
  ON_3dPoint corners[8];
  std::vector<Point3d> pts = lua_istable(L, 1) && luaL_len(L, 1) == 8 && lua_istable(L, 2) == 0 ? ToPoints(L, 1) : std::vector<Point3d>();
  if (pts.size() == 8) {
    for (int i = 0; i < 8; ++i) corners[i] = pts[static_cast<size_t>(i)];
  } else {
    const Point3d c = ToPoint(L, 1);
    Vector3d s(1, 1, 1);
    if (lua_isnumber(L, 2)) { const double v = lua_tonumber(L, 2); s = Vector3d(v, v, v); }
    else if (lua_istable(L, 2)) s = ToVector(L, 2);
    else return luaL_error(L, "AddBox(corner, size) or AddBox(corners8)");
    if (s.x == 0 || s.y == 0 || s.z == 0) return luaL_error(L, "AddBox: size must be non-zero");
    const double x0 = std::min(c.x, c.x + s.x), x1 = std::max(c.x, c.x + s.x);
    const double y0 = std::min(c.y, c.y + s.y), y1 = std::max(c.y, c.y + s.y);
    const double z0 = std::min(c.z, c.z + s.z), z1 = std::max(c.z, c.z + s.z);
    corners[0] = ON_3dPoint(x0, y0, z0); corners[1] = ON_3dPoint(x1, y0, z0); corners[2] = ON_3dPoint(x1, y1, z0); corners[3] = ON_3dPoint(x0, y1, z0);
    corners[4] = ON_3dPoint(x0, y0, z1); corners[5] = ON_3dPoint(x1, y0, z1); corners[6] = ON_3dPoint(x1, y1, z1); corners[7] = ON_3dPoint(x0, y1, z1);
  }
  return PushBrep(L, ON_BrepBox(corners), "AddBox");
}

int rs_AddSphere(lua_State* L) {
  const Point3d c = ToPoint(L, 1);
  const double r = luaL_checknumber(L, 2);
  if (r <= 0) return luaL_error(L, "AddSphere: radius must be positive");
  return PushBrep(L, ON_BrepSphere(ON_Sphere(c, r)), "AddSphere");
}

int rs_AddCylinder(lua_State* L) {
  const Point3d base = ToPoint(L, 1);
  Vector3d axis(0, 0, 1);
  double h = 0;
  if (lua_istable(L, 2)) { axis = ToPoint(L, 2) - base; h = axis.Length(); }
  else { h = luaL_checknumber(L, 2); if (h < 0) { axis = -axis; h = -h; } }
  const double r = luaL_checknumber(L, 3);
  const bool cap = lua_isnoneornil(L, 4) ? true : lua_toboolean(L, 4);
  if (h <= 0 || r <= 0) return luaL_error(L, "AddCylinder: height and radius must be positive");
  axis.Unitize();
  ON_Cylinder cyl(ON_Circle(ON_Plane(base, axis), r), h);
  return PushBrep(L, ON_BrepCylinder(cyl, cap, cap), "AddCylinder");
}

int rs_AddCone(lua_State* L) {
  const Point3d base = ToPoint(L, 1);
  Vector3d axis(0, 0, 1);
  double h = 0;
  if (lua_istable(L, 2)) { axis = ToPoint(L, 2) - base; h = axis.Length(); }
  else { h = luaL_checknumber(L, 2); if (h < 0) { axis = -axis; h = -h; } }
  const double r = luaL_checknumber(L, 3);
  const bool cap = lua_isnoneornil(L, 4) ? true : lua_toboolean(L, 4);
  if (h <= 0 || r <= 0) return luaL_error(L, "AddCone: height and radius must be positive");
  axis.Unitize();
  ON_Cone cone(ON_Plane(base, axis), h, r);
  return PushBrep(L, ON_BrepCone(cone, cap), "AddCone");
}

int rs_AddTorus(lua_State* L) {
  const Point3d c = ToPoint(L, 1);
  const double R = luaL_checknumber(L, 2), r = luaL_checknumber(L, 3);
  Vector3d n = lua_isnoneornil(L, 4) ? Vector3d(0, 0, 1) : ToVector(L, 4);
  if (R <= 0 || r <= 0 || r >= R) return luaL_error(L, "AddTorus: need 0 < minor radius < major radius");
  n.Unitize();
  ON_Torus torus(ON_Plane(c, n), R, r);
  return PushBrep(L, ON_BrepTorus(torus), "AddTorus");
}

int rs_AddSrfPt(lua_State* L) {
  std::vector<Point3d> p = ToPoints(L, 1);
  if (p.size() == 3) p.push_back(p[2]);
  if (p.size() != 4) return luaL_error(L, "AddSrfPt needs 3 or 4 corner points");
  std::vector<Point3d> grid = {p[0], p[1], p[3], p[2]};
  PushId(L, AddObj(L, SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1)), "AddSrfPt"));
  return 1;
}

int rs_AddPlanarSrf(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1), made;
  Document& d = DocOf(L);
  d.BeginChange("AddPlanarSrf");
  for (ObjectId id : ids) {
    const SceneObject* o = d.Find(id);
    if (!o || o->kind != ObjectKind::Curve) continue;
    ON_Plane pl;
    if (!o->curve->raw().IsClosed() || !o->curve->raw().IsPlanar(&pl, d.Settings().absolute_tolerance)) continue;
    if (ON_Brep* b = ON_BrepTrimmedPlane(pl, o->curve->raw())) made.push_back(d.Add(SceneObject::MakeBrep(WrapBrep(b))));
  }
  if (made.empty()) lua_pushnil(L); else PushIds(L, made);
  return 1;
}

int rs_AddMesh(lua_State* L) {
  std::vector<Point3d> verts = ToPoints(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  kernel::Mesh m;
  ON_Mesh& r = m.raw();
  for (size_t i = 0; i < verts.size(); ++i) r.SetVertex(static_cast<int>(i), verts[i]);
  const lua_Integer nf = luaL_len(L, 2);
  const lua_Integer nv = static_cast<lua_Integer>(verts.size());
  for (lua_Integer f = 1; f <= nf; ++f) {
    lua_rawgeti(L, 2, f);
    if (!lua_istable(L, -1)) return luaL_error(L, "AddMesh: face %d is not a list of vertex indices", static_cast<int>(f));
    const lua_Integer n = luaL_len(L, -1);
    int idx[4] = {0, 0, 0, 0};
    for (lua_Integer k = 1; k <= std::min<lua_Integer>(n, 4); ++k) {
      lua_rawgeti(L, -1, k);
      const lua_Integer v = luaL_checkinteger(L, -1);
      lua_pop(L, 1);
      if (v < 1 || v > nv) return luaL_error(L, "AddMesh: face %d uses vertex %d (1..%d)", static_cast<int>(f), static_cast<int>(v), static_cast<int>(nv));
      idx[k - 1] = static_cast<int>(v - 1);
    }
    lua_pop(L, 1);
    if (n == 3) r.SetTriangle(static_cast<int>(f - 1), idx[0], idx[1], idx[2]);
    else if (n >= 4) r.SetQuad(static_cast<int>(f - 1), idx[0], idx[1], idx[2], idx[3]);
    else return luaL_error(L, "AddMesh: face %d needs 3 or 4 vertices", static_cast<int>(f));
  }
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  PushId(L, AddObj(L, SceneObject::MakeMesh(m), "AddMesh"));
  return 1;
}

int rs_ExtrudeCurveStraight(lua_State* L) {
  SceneObject& o = NeedObj(L, 1);
  if (o.kind != ObjectKind::Curve) return luaL_error(L, "ExtrudeCurveStraight: object is not a curve");
  Vector3d v = lua_isnoneornil(L, 3) ? ToVector(L, 2) : ToPoint(L, 3) - ToPoint(L, 2);
  if (v.Length() <= 0) return luaL_error(L, "ExtrudeCurveStraight: zero-length direction");
  const kernel::NurbsCurve c = *o.curve;
  PushId(L, ExtrudeCurveAlong(L, c, v));
  return 1;
}

int RunBoolean(lua_State* L, kernel::BooleanOp op, bool two_sets, const char* label) {
  std::vector<ObjectId> a = ToIds(L, 1), b = two_sets ? ToIds(L, 2) : std::vector<ObjectId>();
  const bool del = two_sets ? (lua_isnoneornil(L, 3) ? true : lua_toboolean(L, 3)) : (lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2));
  Document& d = DocOf(L);
  std::vector<std::pair<ObjectId, kernel::Mesh>> ma, mb;
  auto collect = [&](const std::vector<ObjectId>& ids, std::vector<std::pair<ObjectId, kernel::Mesh>>& out) {
    for (ObjectId id : ids) {
      const SceneObject* o = d.Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, 0.005);
      if (!m || !m->IsClosedManifold()) { Eng(L).Print("! Object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
      out.push_back({id, *m});
    }
  };
  collect(a, ma);
  if (two_sets) collect(b, mb);
  if (ma.empty() || (two_sets && mb.empty())) { lua_pushnil(L); return 1; }
  try {
    kernel::Mesh result = ma[0].second;
    const int layer = d.Find(ma[0].first) ? d.Find(ma[0].first)->layer_index : 0;
    if (two_sets) {
      for (size_t i = 1; i < ma.size(); ++i) result = kernel::BooleanCombine(result, ma[i].second, kernel::BooleanOp::Union);
      kernel::Mesh other = mb[0].second;
      for (size_t i = 1; i < mb.size(); ++i) other = kernel::BooleanCombine(other, mb[i].second, kernel::BooleanOp::Union);
      result = kernel::BooleanCombine(result, other, op);
    } else {
      for (size_t i = 1; i < ma.size(); ++i) result = kernel::BooleanCombine(result, ma[i].second, op);
    }
    d.BeginChange(label);
    if (del) { for (auto& [id, m] : ma) d.Remove(id); for (auto& [id, m] : mb) d.Remove(id); }
    if (result.FaceCount() == 0) { lua_pushnil(L); return 1; }
    SceneObject n = SceneObject::MakeMesh(result);
    n.layer_index = layer;
    const ObjectId id = d.Add(std::move(n));
    PushIds(L, {id});
    return 1;
  } catch (const std::exception& ex) {
    return luaL_error(L, "%s failed: %s", label, ex.what());
  }
}

int rs_BooleanUnion(lua_State* L) { return RunBoolean(L, kernel::BooleanOp::Union, false, "BooleanUnion"); }
int rs_BooleanDifference(lua_State* L) { return RunBoolean(L, kernel::BooleanOp::Difference, true, "BooleanDifference"); }
int rs_BooleanIntersection(lua_State* L) { return RunBoolean(L, kernel::BooleanOp::Intersection, true, "BooleanIntersection"); }

int rs_MoveObject(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1), out;
  TransformIds(L, ids, ON_Xform::TranslationTransformation(ToVector(L, 2)), false, "MoveObject", out);
  return PushLike(L, 1, out);
}

int rs_CopyObject(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1), out;
  const Vector3d v = lua_isnoneornil(L, 2) ? Vector3d(0, 0, 0) : ToVector(L, 2);
  TransformIds(L, ids, ON_Xform::TranslationTransformation(v), true, "CopyObject", out);
  return PushLike(L, 1, out);
}

int rs_RotateObject(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1), out;
  const Point3d center = ToPoint(L, 2);
  const double deg = luaL_checknumber(L, 3);
  Vector3d axis = lua_isnoneornil(L, 4) ? Vector3d(0, 0, 1) : ToVector(L, 4);
  const bool copy = lua_toboolean(L, 5);
  if (!axis.Unitize()) axis = Vector3d(0, 0, 1);
  ON_Xform xf;
  xf.Rotation(deg * ON_PI / 180.0, axis, center);
  TransformIds(L, ids, xf, copy, "RotateObject", out);
  return PushLike(L, 1, out);
}

int rs_ScaleObject(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1), out;
  const Point3d origin = ToPoint(L, 2);
  Vector3d s(1, 1, 1);
  if (lua_isnumber(L, 3)) { const double v = lua_tonumber(L, 3); s = Vector3d(v, v, v); } else s = ToVector(L, 3);
  const bool copy = lua_toboolean(L, 4);
  const ON_Xform xf = ON_Xform::TranslationTransformation(origin - ON_3dPoint::Origin) * ON_Xform::DiagonalTransformation(s.x, s.y, s.z) * ON_Xform::TranslationTransformation(ON_3dPoint::Origin - origin);
  TransformIds(L, ids, xf, copy, "ScaleObject", out);
  return PushLike(L, 1, out);
}

int rs_MirrorObject(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1), out;
  const Point3d a = ToPoint(L, 2), b = ToPoint(L, 3);
  const bool copy = lua_toboolean(L, 4);
  Vector3d n = ON_CrossProduct(b - a, Vector3d(0, 0, 1));
  if (!n.Unitize()) return luaL_error(L, "MirrorObject: mirror line is degenerate or vertical");
  ON_Xform xf = ON_Xform::MirrorTransformation(ON_PlaneEquation(n.x, n.y, n.z, -ON_DotProduct(n, Vector3d(a))));
  TransformIds(L, ids, xf, copy, "MirrorObject", out);
  return PushLike(L, 1, out);
}

int rs_TransformObject(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1), out;
  const ON_Xform xf = ToXform(L, 2);
  TransformIds(L, ids, xf, lua_toboolean(L, 3), "TransformObject", out);
  return PushLike(L, 1, out);
}

int rs_DeleteObject(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1);
  Document& d = DocOf(L);
  d.BeginChange("DeleteObject");
  int n = 0;
  for (ObjectId id : ids) if (d.Remove(id)) ++n;
  if (lua_istable(L, 1)) lua_pushinteger(L, n); else lua_pushboolean(L, n > 0);
  return 1;
}

int rs_SelectObject(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1);
  Document& d = DocOf(L);
  int n = 0;
  for (ObjectId id : ids) { d.Select(id, true); if (const SceneObject* o = d.Find(id)) n += o->selected ? 1 : 0; }
  if (lua_istable(L, 1)) lua_pushinteger(L, n); else lua_pushboolean(L, n > 0);
  return 1;
}

int rs_UnselectObject(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1);
  for (ObjectId id : ids) DocOf(L).Select(id, false);
  lua_pushinteger(L, static_cast<lua_Integer>(ids.size()));
  return 1;
}

int rs_UnselectAllObjects(lua_State* L) {
  const size_t n = DocOf(L).SelectedCount();
  DocOf(L).SelectNone();
  lua_pushinteger(L, static_cast<lua_Integer>(n));
  return 1;
}

int rs_SelectedObjects(lua_State* L) { PushIds(L, DocOf(L).SelectedIds()); return 1; }

int rs_AllObjects(lua_State* L) {
  Document& d = DocOf(L);
  std::vector<ObjectId> ids;
  for (const SceneObject& o : d.Objects()) ids.push_back(o.id);
  if (lua_toboolean(L, 1)) for (ObjectId id : ids) d.Select(id, true);
  PushIds(L, ids);
  return 1;
}

int rs_ObjectsByLayer(lua_State* L) {
  const int layer = NeedLayer(L, 1);
  Document& d = DocOf(L);
  std::vector<ObjectId> ids;
  for (const SceneObject& o : d.Objects()) if (o.layer_index == layer) ids.push_back(o.id);
  if (lua_toboolean(L, 2)) for (ObjectId id : ids) d.Select(id, true);
  PushIds(L, ids);
  return 1;
}

int rs_ObjectsByName(lua_State* L) {
  const std::string name = luaL_checkstring(L, 1);
  Document& d = DocOf(L);
  std::vector<ObjectId> ids;
  for (const SceneObject& o : d.Objects()) if (o.name == name) ids.push_back(o.id);
  if (lua_toboolean(L, 2)) for (ObjectId id : ids) d.Select(id, true);
  PushIds(L, ids);
  return 1;
}

int rs_ObjectsByType(lua_State* L) {
  const int mask = TypeMaskFromArg(L, 1);
  Document& d = DocOf(L);
  std::vector<ObjectId> ids;
  for (const SceneObject& o : d.Objects()) if (mask == 0 || (TypeMask(o) & mask)) ids.push_back(o.id);
  if (lua_toboolean(L, 2)) for (ObjectId id : ids) d.Select(id, true);
  PushIds(L, ids);
  return 1;
}

int rs_ObjectName(lua_State* L) {
  SceneObject& o = NeedObj(L, 1);
  if (lua_isnoneornil(L, 2)) { lua_pushstring(L, o.name.c_str()); return 1; }
  const std::string old = o.name;
  DocOf(L).BeginChange("ObjectName");
  DocOf(L).Find(o.id)->name = luaL_checkstring(L, 2);
  lua_pushstring(L, old.c_str());
  return 1;
}

int rs_ObjectLayer(lua_State* L) {
  SceneObject& o = NeedObj(L, 1);
  Document& d = DocOf(L);
  const std::string old = (o.layer_index >= 0 && o.layer_index < static_cast<int>(d.Layers().size())) ? d.Layers()[static_cast<size_t>(o.layer_index)].name : "";
  if (lua_isnoneornil(L, 2)) { lua_pushstring(L, old.c_str()); return 1; }
  const int layer = NeedLayer(L, 2);
  d.BeginChange("ObjectLayer");
  if (SceneObject* p = d.Find(o.id)) p->layer_index = layer;
  lua_pushstring(L, old.c_str());
  return 1;
}

int rs_ObjectColor(lua_State* L) {
  SceneObject& o = NeedObj(L, 1);
  Document& d = DocOf(L);
  const Color old = d.EffectiveColor(o);
  if (lua_isnoneornil(L, 2)) { PushColor(L, old); return 1; }
  const Color c = ToColor(L, 2);
  d.BeginChange("ObjectColor");
  if (SceneObject* p = d.Find(o.id)) { p->color = c; p->color_by_layer = false; }
  PushColor(L, old);
  return 1;
}

int rs_ObjectType(lua_State* L) { SceneObject* o = Obj(L, 1); if (!o) lua_pushnil(L); else lua_pushinteger(L, TypeMask(*o)); return 1; }
int rs_ObjectDescription(lua_State* L) { lua_pushstring(L, NeedObj(L, 1).Describe().c_str()); return 1; }
int rs_IsObject(lua_State* L) { lua_pushboolean(L, Obj(L, 1) != nullptr); return 1; }
int rs_IsPoint(lua_State* L) { SceneObject* o = Obj(L, 1); lua_pushboolean(L, o && o->kind == ObjectKind::Point); return 1; }
int rs_IsCurve(lua_State* L) { SceneObject* o = Obj(L, 1); lua_pushboolean(L, o && o->kind == ObjectKind::Curve); return 1; }
int rs_IsSurface(lua_State* L) { SceneObject* o = Obj(L, 1); lua_pushboolean(L, o && (o->kind == ObjectKind::Surface || (o->kind == ObjectKind::Brep && TypeMask(*o) == 8))); return 1; }
int rs_IsPolysurface(lua_State* L) { SceneObject* o = Obj(L, 1); lua_pushboolean(L, o && o->kind == ObjectKind::Brep && TypeMask(*o) == 16); return 1; }
int rs_IsBrep(lua_State* L) { SceneObject* o = Obj(L, 1); lua_pushboolean(L, o && o->kind == ObjectKind::Brep); return 1; }
int rs_IsMesh(lua_State* L) { SceneObject* o = Obj(L, 1); lua_pushboolean(L, o && o->kind == ObjectKind::Mesh); return 1; }
int rs_IsSubD(lua_State* L) { SceneObject* o = Obj(L, 1); lua_pushboolean(L, o && o->kind == ObjectKind::SubD); return 1; }
int rs_IsObjectSelected(lua_State* L) { SceneObject* o = Obj(L, 1); lua_pushboolean(L, o && o->selected); return 1; }
int rs_IsObjectHidden(lua_State* L) { SceneObject* o = Obj(L, 1); lua_pushboolean(L, o && !o->visible); return 1; }
int rs_IsObjectLocked(lua_State* L) { SceneObject* o = Obj(L, 1); lua_pushboolean(L, o && o->locked); return 1; }

int SetFlag(lua_State* L, bool SceneObject::*flag, bool value, const char* label) {
  std::vector<ObjectId> ids = ToIds(L, 1);
  Document& d = DocOf(L);
  d.BeginChange(label);
  int n = 0;
  for (ObjectId id : ids) if (SceneObject* o = d.Find(id)) { o->*flag = value; if (!o->visible || o->locked) o->selected = false; ++n; }
  if (lua_istable(L, 1)) lua_pushinteger(L, n); else lua_pushboolean(L, n > 0);
  return 1;
}
int rs_HideObject(lua_State* L) { return SetFlag(L, &SceneObject::visible, false, "HideObject"); }
int rs_ShowObject(lua_State* L) { return SetFlag(L, &SceneObject::visible, true, "ShowObject"); }
int rs_LockObject(lua_State* L) { return SetFlag(L, &SceneObject::locked, true, "LockObject"); }
int rs_UnlockObject(lua_State* L) { return SetFlag(L, &SceneObject::locked, false, "UnlockObject"); }

int rs_BoundingBox(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1);
  kernel::BoundingBox bb;
  if (!DocOf(L).BoundingBoxOf(ids, bb)) { lua_pushnil(L); return 1; }
  const Point3d& a = bb.min;
  const Point3d& b = bb.max;
  PushPoints(L, {a, Point3d(b.x, a.y, a.z), Point3d(b.x, b.y, a.z), Point3d(a.x, b.y, a.z),
                 Point3d(a.x, a.y, b.z), Point3d(b.x, a.y, b.z), b, Point3d(a.x, b.y, b.z)});
  return 1;
}

const kernel::NurbsCurve& NeedCurve(lua_State* L, int idx) {
  SceneObject& o = NeedObj(L, idx);
  if (o.kind != ObjectKind::Curve || !o.curve) luaL_error(L, "object %s is not a curve", lua_tostring(L, idx));
  return *o.curve;
}

int rs_CurveLength(lua_State* L) { lua_pushnumber(L, NeedCurve(L, 1).Length()); return 1; }
int rs_CurveDomain(lua_State* L) { const kernel::Interval d = NeedCurve(L, 1).Domain(); lua_createtable(L, 2, 0); lua_pushnumber(L, d.min); lua_rawseti(L, -2, 1); lua_pushnumber(L, d.max); lua_rawseti(L, -2, 2); return 1; }
int rs_CurveStartPoint(lua_State* L) { const kernel::NurbsCurve& c = NeedCurve(L, 1); PushPoint(L, c.PointAt(c.Domain().min)); return 1; }
int rs_CurveEndPoint(lua_State* L) { const kernel::NurbsCurve& c = NeedCurve(L, 1); PushPoint(L, c.PointAt(c.Domain().max)); return 1; }
int rs_CurveMidPoint(lua_State* L) { const kernel::NurbsCurve& c = NeedCurve(L, 1); PushPoint(L, c.PointAt(c.ParameterAtArcLength(c.Length() / 2))); return 1; }
int rs_EvaluateCurve(lua_State* L) { PushPoint(L, NeedCurve(L, 1).PointAt(luaL_checknumber(L, 2))); return 1; }
int rs_CurveTangent(lua_State* L) { PushPoint(L, Point3d(NeedCurve(L, 1).TangentAt(luaL_checknumber(L, 2)))); return 1; }
int rs_CurveDegree(lua_State* L) { lua_pushinteger(L, NeedCurve(L, 1).Degree()); return 1; }
int rs_CurvePointCount(lua_State* L) { lua_pushinteger(L, NeedCurve(L, 1).ControlPointCount()); return 1; }
int rs_IsCurveClosed(lua_State* L) { lua_pushboolean(L, NeedCurve(L, 1).IsClosed()); return 1; }
int rs_IsCurvePlanar(lua_State* L) { lua_pushboolean(L, NeedCurve(L, 1).IsPlanar(DocOf(L).Settings().absolute_tolerance)); return 1; }
int rs_IsCurveLinear(lua_State* L) { lua_pushboolean(L, NeedCurve(L, 1).IsLinear(DocOf(L).Settings().absolute_tolerance)); return 1; }
int rs_CurveClosestPoint(lua_State* L) { lua_pushnumber(L, NeedCurve(L, 1).ClosestPointParameter(ToPoint(L, 2))); return 1; }
int rs_CurveArcLengthPoint(lua_State* L) { const kernel::NurbsCurve& c = NeedCurve(L, 1); PushPoint(L, c.PointAt(c.ParameterAtArcLength(luaL_checknumber(L, 2)))); return 1; }

int rs_CurvePoints(lua_State* L) {
  const kernel::NurbsCurve& c = NeedCurve(L, 1);
  std::vector<Point3d> pts;
  for (int i = 0; i < c.ControlPointCount(); ++i) pts.push_back(c.ControlPointAt(i));
  PushPoints(L, pts);
  return 1;
}

int rs_DivideCurve(lua_State* L) {
  const kernel::NurbsCurve& c = NeedCurve(L, 1);
  const int segments = static_cast<int>(luaL_checkinteger(L, 2));
  const bool create = lua_toboolean(L, 3);
  const bool return_points = lua_isnoneornil(L, 4) ? true : lua_toboolean(L, 4);
  if (segments < 1) return luaL_error(L, "DivideCurve: segments must be >= 1");
  std::vector<double> params = c.DivideByCount(segments);
  const kernel::Interval d = c.Domain();
  if (params.empty() || std::fabs(params.front() - d.min) > 1e-12) params.insert(params.begin(), d.min);
  if (std::fabs(params.back() - d.max) > 1e-12) params.push_back(d.max);
  std::vector<Point3d> pts;
  for (double t : params) pts.push_back(c.PointAt(t));
  if (create) {
    Document& doc = DocOf(L);
    doc.BeginChange("DivideCurve");
    for (const Point3d& p : pts) doc.Add(SceneObject::MakePoint(p));
  }
  if (return_points) PushPoints(L, pts);
  else { lua_createtable(L, static_cast<int>(params.size()), 0); for (size_t i = 0; i < params.size(); ++i) { lua_pushnumber(L, params[i]); lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1)); } }
  return 1;
}

int rs_SurfaceArea(lua_State* L) { lua_pushnumber(L, ObjectAreaOf(NeedObj(L, 1))); return 1; }
int rs_SurfaceVolume(lua_State* L) { bool closed = false; const double v = ObjectVolumeOf(NeedObj(L, 1), closed); if (!closed) lua_pushnil(L); else lua_pushnumber(L, v); return 1; }
int rs_IsObjectSolid(lua_State* L) { SceneObject* o = Obj(L, 1); bool closed = false; if (o) ObjectVolumeOf(*o, closed); lua_pushboolean(L, closed); return 1; }
int rs_SurfaceClosestPoint(lua_State* L) {
  SceneObject& o = NeedObj(L, 1);
  const Point3d p = ToPoint(L, 2);
  if (o.kind == ObjectKind::Surface && o.surface) { PushPoint(L, o.surface->ClosestPoint(p)); return 1; }
  std::optional<kernel::Mesh> m = MeshOf(o, 0.01);
  if (!m) { lua_pushnil(L); return 1; }
  PushPoint(L, m->ClosestPoint(p));
  return 1;
}

const kernel::Mesh& NeedMesh(lua_State* L, int idx) {
  SceneObject& o = NeedObj(L, idx);
  if (o.kind != ObjectKind::Mesh || !o.mesh) luaL_error(L, "object %s is not a mesh", lua_tostring(L, idx));
  return *o.mesh;
}
int rs_MeshVertexCount(lua_State* L) { lua_pushinteger(L, NeedMesh(L, 1).VertexCount()); return 1; }
int rs_MeshFaceCount(lua_State* L) { lua_pushinteger(L, NeedMesh(L, 1).FaceCount()); return 1; }
int rs_MeshVolume(lua_State* L) { lua_pushnumber(L, std::fabs(NeedMesh(L, 1).Volume())); return 1; }
int rs_MeshArea(lua_State* L) { lua_pushnumber(L, NeedMesh(L, 1).Area()); return 1; }
int rs_MeshVertices(lua_State* L) {
  const ON_Mesh& m = NeedMesh(L, 1).raw();
  std::vector<Point3d> pts;
  for (int i = 0; i < m.VertexCount(); ++i) pts.push_back(m.Vertex(i));
  PushPoints(L, pts);
  return 1;
}
int rs_MeshFaces(lua_State* L) {
  const ON_Mesh& m = NeedMesh(L, 1).raw();
  lua_createtable(L, m.FaceCount(), 0);
  for (int i = 0; i < m.FaceCount(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    const int n = f.IsQuad() ? 4 : 3;
    lua_createtable(L, n, 0);
    for (int k = 0; k < n; ++k) { lua_pushinteger(L, f.vi[k] + 1); lua_rawseti(L, -2, k + 1); }
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

// ---- layers --------------------------------------------------------------------

int rs_AddLayer(lua_State* L) {
  const char* name = luaL_optstring(L, 1, nullptr);
  Document& d = DocOf(L);
  std::string n = name ? name : "Layer " + std::to_string(d.Layers().size() + 1);
  const Color c = lua_isnoneornil(L, 2) ? Color::FromBytes(0, 0, 0) : ToColor(L, 2);
  int parent = -1;
  if (!lua_isnoneornil(L, 5)) parent = LayerIndexArg(L, 5);
  d.BeginChange("AddLayer");
  int idx = d.FindLayer(n);
  if (idx < 0) idx = d.AddLayer(n, c, parent);
  Layer& layer = d.Layers()[static_cast<size_t>(idx)];
  if (!lua_isnoneornil(L, 2)) layer.color = c;
  if (!lua_isnoneornil(L, 3)) layer.visible = lua_toboolean(L, 3);
  if (!lua_isnoneornil(L, 4)) layer.locked = lua_toboolean(L, 4);
  lua_pushstring(L, layer.name.c_str());
  return 1;
}

int rs_CurrentLayer(lua_State* L) {
  Document& d = DocOf(L);
  const std::string old = d.Layers()[static_cast<size_t>(d.CurrentLayer())].name;
  if (!lua_isnoneornil(L, 1)) d.SetCurrentLayer(NeedLayer(L, 1));
  lua_pushstring(L, old.c_str());
  return 1;
}

int rs_LayerNames(lua_State* L) {
  Document& d = DocOf(L);
  lua_createtable(L, static_cast<int>(d.Layers().size()), 0);
  for (size_t i = 0; i < d.Layers().size(); ++i) { lua_pushstring(L, d.LayerFullPath(static_cast<int>(i)).c_str()); lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1)); }
  return 1;
}

int rs_IsLayer(lua_State* L) { lua_pushboolean(L, LayerIndexArg(L, 1) >= 0); return 1; }

int rs_LayerVisible(lua_State* L) {
  Layer& l = DocOf(L).Layers()[static_cast<size_t>(NeedLayer(L, 1))];
  const bool old = l.visible;
  if (!lua_isnoneornil(L, 2)) { l.visible = lua_toboolean(L, 2); DocOf(L).Touch(); }
  lua_pushboolean(L, old);
  return 1;
}

int rs_LayerLocked(lua_State* L) {
  Layer& l = DocOf(L).Layers()[static_cast<size_t>(NeedLayer(L, 1))];
  const bool old = l.locked;
  if (!lua_isnoneornil(L, 2)) { l.locked = lua_toboolean(L, 2); DocOf(L).Touch(); }
  lua_pushboolean(L, old);
  return 1;
}

int rs_LayerColor(lua_State* L) {
  Layer& l = DocOf(L).Layers()[static_cast<size_t>(NeedLayer(L, 1))];
  const Color old = l.color;
  if (!lua_isnoneornil(L, 2)) { l.color = ToColor(L, 2); DocOf(L).Touch(); }
  PushColor(L, old);
  return 1;
}

int rs_DeleteLayer(lua_State* L) {
  const int idx = LayerIndexArg(L, 1);
  if (idx < 0) { lua_pushboolean(L, false); return 1; }
  DocOf(L).BeginChange("DeleteLayer");
  lua_pushboolean(L, DocOf(L).RemoveLayer(idx));
  return 1;
}

int rs_LayerCount(lua_State* L) { lua_pushinteger(L, static_cast<lua_Integer>(DocOf(L).Layers().size())); return 1; }

// ---- user text -------------------------------------------------------------------

int rs_GetUserText(lua_State* L) {
  SceneObject& o = NeedObj(L, 1);
  if (lua_isnoneornil(L, 2)) {
    lua_createtable(L, static_cast<int>(o.user_text.size()), 0);
    int i = 1;
    for (const auto& kv : o.user_text) { lua_pushstring(L, kv.first.c_str()); lua_rawseti(L, -2, i++); }
    return 1;
  }
  const auto it = o.user_text.find(luaL_checkstring(L, 2));
  if (it == o.user_text.end()) lua_pushnil(L); else lua_pushstring(L, it->second.c_str());
  return 1;
}

int rs_SetUserText(lua_State* L) {
  std::vector<ObjectId> ids = ToIds(L, 1);
  const std::string key = luaL_checkstring(L, 2);
  Document& d = DocOf(L);
  d.BeginChange("SetUserText");
  for (ObjectId id : ids) {
    SceneObject* o = d.Find(id);
    if (!o) continue;
    if (lua_isnoneornil(L, 3)) o->user_text.erase(key); else o->user_text[key] = luaL_checkstring(L, 3);
  }
  lua_pushboolean(L, !ids.empty());
  return 1;
}

int rs_GetDocumentUserText(lua_State* L) {
  std::map<std::string, std::string>& ut = DocOf(L).UserText();
  if (lua_isnoneornil(L, 1)) {
    lua_createtable(L, static_cast<int>(ut.size()), 0);
    int i = 1;
    for (const auto& kv : ut) { lua_pushstring(L, kv.first.c_str()); lua_rawseti(L, -2, i++); }
    return 1;
  }
  const auto it = ut.find(luaL_checkstring(L, 1));
  if (it == ut.end()) lua_pushnil(L); else lua_pushstring(L, it->second.c_str());
  return 1;
}

int rs_SetDocumentUserText(lua_State* L) {
  const std::string key = luaL_checkstring(L, 1);
  if (lua_isnoneornil(L, 2)) DocOf(L).UserText().erase(key); else DocOf(L).UserText()[key] = luaL_checkstring(L, 2);
  DocOf(L).Touch();
  lua_pushboolean(L, true);
  return 1;
}

// ---- prompts (yield the coroutine; the RunScript command resumes it) -------------

int rs_GetPoint(lua_State* L) {
  if (!NeedScriptContext(L, "GetPoint")) return 0;
  ScriptRequest r;
  r.want = ScriptWant::Point;
  r.prompt = luaL_optstring(L, 1, "Pick a point");
  Eng(L).SetRequest(r);
  return lua_yield(L, 0);
}

int GetObjectsImpl(lua_State* L, bool single) {
  ScriptRequest r;
  r.want = ScriptWant::Objects;
  r.prompt = luaL_optstring(L, 1, single ? "Select object" : "Select objects");
  r.min_objects = single ? 1 : static_cast<int>(luaL_optinteger(L, 3, 1));
  r.single_object = single;
  Eng(L).SetRequest(r);
  return lua_yield(L, 0);
}

int rs_GetObject(lua_State* L) {
  if (!NeedScriptContext(L, "GetObject")) return 0;
  return GetObjectsImpl(L, true);
}

int rs_GetObjects(lua_State* L) {
  if (!NeedScriptContext(L, "GetObjects")) return 0;
  return GetObjectsImpl(L, false);
}

int rs_GetString(lua_State* L) {
  if (!NeedScriptContext(L, "GetString")) return 0;
  ScriptRequest r;
  r.want = ScriptWant::Text;
  r.prompt = luaL_optstring(L, 1, "Text");
  if (!lua_isnoneornil(L, 2)) r.default_text = luaL_checkstring(L, 2);
  Eng(L).SetRequest(r);
  return lua_yield(L, 0);
}

int rs_GetReal(lua_State* L) {
  if (!NeedScriptContext(L, "GetReal")) return 0;
  ScriptRequest r;
  r.want = ScriptWant::Number;
  r.prompt = luaL_optstring(L, 1, "Number");
  if (!lua_isnoneornil(L, 2)) r.default_number = luaL_checknumber(L, 2);
  Eng(L).SetRequest(r);
  return lua_yield(L, 0);
}

int rs_GetInteger(lua_State* L) {
  if (!NeedScriptContext(L, "GetInteger")) return 0;
  ScriptRequest r;
  r.want = ScriptWant::Integer;
  r.prompt = luaL_optstring(L, 1, "Integer");
  if (!lua_isnoneornil(L, 2)) r.default_number = static_cast<double>(luaL_checkinteger(L, 2));
  Eng(L).SetRequest(r);
  return lua_yield(L, 0);
}

// ---- application / document -----------------------------------------------------

int rs_MessageBox(lua_State* L) {
  const std::string text = luaL_checkstring(L, 1);
  const std::string title = luaL_optstring(L, 3, "Script");
  Eng(L).Print("[" + title + "] " + text);
  AppOf(L).ShowMessageBox(title, text);
  lua_pushinteger(L, 1);
  return 1;
}

int rs_Prompt(lua_State* L) { Eng(L).Print(luaL_checkstring(L, 1)); return 0; }
int rs_Redraw(lua_State* L) { (void)L; return 0; }
int rs_EnableRedraw(lua_State* L) {
  bool& redraw = AppOf(L).State().redraw;
  const bool old = redraw;
  if (!lua_isnoneornil(L, 1)) redraw = lua_toboolean(L, 1);
  lua_pushboolean(L, old);
  return 1;
}
int rs_ZoomExtents(lua_State* L) { AppOf(L).ZoomExtentsAll(); return 0; }
int rs_Sleep(lua_State* L) { (void)L; return 0; }
int rs_Undo(lua_State* L) { lua_pushboolean(L, DocOf(L).Undo()); return 1; }
int rs_Redo(lua_State* L) { lua_pushboolean(L, DocOf(L).Redo()); return 1; }
int rs_BeginUndo(lua_State* L) { DocOf(L).BeginChange(luaL_optstring(L, 1, "Script")); return 0; }

int UnitCode(const std::string& name) {
  const std::string n = ToLower(name);
  if (n == "microns") return 1;
  if (n == "millimeters") return 2;
  if (n == "centimeters") return 3;
  if (n == "meters") return 4;
  if (n == "kilometers") return 5;
  if (n == "microinches") return 6;
  if (n == "mils") return 7;
  if (n == "inches") return 8;
  if (n == "feet") return 9;
  if (n == "miles") return 10;
  return 0;
}

int rs_UnitSystem(lua_State* L) {
  DocumentSettings& s = DocOf(L).Settings();
  const int old = UnitCode(s.unit_system);
  if (!lua_isnoneornil(L, 1)) {
    static const char* names[] = {"None", "Microns", "Millimeters", "Centimeters", "Meters", "Kilometers", "Microinches", "Mils", "Inches", "Feet", "Miles"};
    if (lua_isnumber(L, 1)) { const int c = static_cast<int>(lua_tointeger(L, 1)); if (c >= 0 && c <= 10) s.unit_system = names[c]; }
    else s.unit_system = luaL_checkstring(L, 1);
    DocOf(L).Touch();
  }
  lua_pushinteger(L, old);
  return 1;
}

int rs_UnitSystemName(lua_State* L) { lua_pushstring(L, DocOf(L).Settings().unit_system.c_str()); return 1; }

int rs_DocumentName(lua_State* L) {
  const std::string& p = DocOf(L).Path();
  lua_pushstring(L, p.empty() ? "Untitled" : std::filesystem::path(p).filename().string().c_str());
  return 1;
}

int rs_DocumentPath(lua_State* L) {
  const std::string& p = DocOf(L).Path();
  if (p.empty()) lua_pushnil(L); else lua_pushstring(L, std::filesystem::path(p).parent_path().string().c_str());
  return 1;
}

int rs_DocumentModified(lua_State* L) {
  const bool old = DocOf(L).Modified();
  if (!lua_isnoneornil(L, 1)) DocOf(L).SetModified(lua_toboolean(L, 1));
  lua_pushboolean(L, old);
  return 1;
}

int rs_CommandHistory(lua_State* L) {
  std::string all;
  for (const std::string& line : AppOf(L).Engine().History()) { all += line; all += '\n'; }
  lua_pushstring(L, all.c_str());
  return 1;
}

int rs_ClearCommandHistory(lua_State* L) { AppOf(L).Engine().ClearHistory(); return 0; }
int rs_Version(lua_State* L) { lua_pushstring(L, "Dino 8 " DINO8_VERSION " (Lua " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR ")"); return 1; }
int rs_LastCommandName(lua_State* L) { lua_pushstring(L, AppOf(L).Engine().LastCommand().c_str()); return 1; }

// ---- points / vectors / transforms (pure math) ------------------------------------

int rs_Distance(lua_State* L) { lua_pushnumber(L, (ToPoint(L, 1) - ToPoint(L, 2)).Length()); return 1; }
int rs_VectorAdd(lua_State* L) { PushPoint(L, ToPoint(L, 1) + ToVector(L, 2)); return 1; }
int rs_VectorSubtract(lua_State* L) { PushPoint(L, Point3d(ToVector(L, 1) - ToVector(L, 2))); return 1; }
int rs_VectorScale(lua_State* L) { PushPoint(L, Point3d(ToVector(L, 1) * luaL_checknumber(L, 2))); return 1; }
int rs_VectorDivide(lua_State* L) { const double s = luaL_checknumber(L, 2); if (s == 0) return luaL_error(L, "VectorDivide by zero"); PushPoint(L, Point3d(ToVector(L, 1) / s)); return 1; }
int rs_VectorUnitize(lua_State* L) { Vector3d v = ToVector(L, 1); if (!v.Unitize()) { lua_pushnil(L); return 1; } PushPoint(L, Point3d(v)); return 1; }
int rs_VectorLength(lua_State* L) { lua_pushnumber(L, ToVector(L, 1).Length()); return 1; }
int rs_VectorReverse(lua_State* L) { PushPoint(L, Point3d(-ToVector(L, 1))); return 1; }
int rs_VectorCrossProduct(lua_State* L) { PushPoint(L, Point3d(ON_CrossProduct(ToVector(L, 1), ToVector(L, 2)))); return 1; }
int rs_VectorDotProduct(lua_State* L) { lua_pushnumber(L, ON_DotProduct(ToVector(L, 1), ToVector(L, 2))); return 1; }
int rs_VectorCreate(lua_State* L) { PushPoint(L, Point3d(ToPoint(L, 1) - ToPoint(L, 2))); return 1; }
int rs_VectorAngle(lua_State* L) { lua_pushnumber(L, ON_3dVector::Angle(ToVector(L, 1), ToVector(L, 2)) * 180.0 / ON_PI); return 1; }
int rs_VectorRotate(lua_State* L) {
  Vector3d v = ToVector(L, 1), axis = ToVector(L, 3);
  if (!axis.Unitize()) return luaL_error(L, "VectorRotate: zero axis");
  v.Rotate(luaL_checknumber(L, 2) * ON_PI / 180.0, axis);
  PushPoint(L, Point3d(v));
  return 1;
}
int rs_IsVectorZero(lua_State* L) { lua_pushboolean(L, ToVector(L, 1).IsZero()); return 1; }
int rs_PointAdd(lua_State* L) { PushPoint(L, ToPoint(L, 1) + ToVector(L, 2)); return 1; }
int rs_PointSubtract(lua_State* L) { PushPoint(L, Point3d(ToPoint(L, 1) - ToPoint(L, 2))); return 1; }
int rs_PointScale(lua_State* L) { PushPoint(L, Point3d(ToVector(L, 1) * luaL_checknumber(L, 2))); return 1; }
int rs_PointDivide(lua_State* L) { const double s = luaL_checknumber(L, 2); if (s == 0) return luaL_error(L, "PointDivide by zero"); PushPoint(L, Point3d(ToVector(L, 1) / s)); return 1; }
int rs_PointCompare(lua_State* L) { lua_pushboolean(L, (ToPoint(L, 1) - ToPoint(L, 2)).Length() <= luaL_optnumber(L, 3, DocOf(L).Settings().absolute_tolerance)); return 1; }
int rs_PointTransform(lua_State* L) { Point3d p = ToPoint(L, 1); p.Transform(ToXform(L, 2)); PushPoint(L, p); return 1; }
int rs_VectorTransform(lua_State* L) { Vector3d v = ToVector(L, 1); v.Transform(ToXform(L, 2)); PushPoint(L, Point3d(v)); return 1; }

int rs_XformIdentity(lua_State* L) { PushXform(L, ON_Xform::IdentityTransformation); return 1; }
int rs_XformTranslation(lua_State* L) { PushXform(L, ON_Xform::TranslationTransformation(ToVector(L, 1))); return 1; }
int rs_XformRotation(lua_State* L) {
  Vector3d axis = lua_isnoneornil(L, 2) ? Vector3d(0, 0, 1) : ToVector(L, 2);
  if (!axis.Unitize()) return luaL_error(L, "XformRotation: zero axis");
  const Point3d c = lua_isnoneornil(L, 3) ? Point3d(0, 0, 0) : ToPoint(L, 3);
  ON_Xform x;
  x.Rotation(luaL_checknumber(L, 1) * ON_PI / 180.0, axis, c);
  PushXform(L, x);
  return 1;
}
int rs_XformScale(lua_State* L) {
  Vector3d s(1, 1, 1);
  if (lua_isnumber(L, 1)) { const double v = lua_tonumber(L, 1); s = Vector3d(v, v, v); } else s = ToVector(L, 1);
  const Point3d c = lua_isnoneornil(L, 2) ? Point3d(0, 0, 0) : ToPoint(L, 2);
  PushXform(L, ON_Xform::TranslationTransformation(c - ON_3dPoint::Origin) * ON_Xform::DiagonalTransformation(s.x, s.y, s.z) * ON_Xform::TranslationTransformation(ON_3dPoint::Origin - c));
  return 1;
}
int rs_XformMirror(lua_State* L) {
  const Point3d p = ToPoint(L, 1);
  Vector3d n = ToVector(L, 2);
  if (!n.Unitize()) return luaL_error(L, "XformMirror: zero normal");
  PushXform(L, ON_Xform::MirrorTransformation(ON_PlaneEquation(n.x, n.y, n.z, -ON_DotProduct(n, Vector3d(p)))));
  return 1;
}
int rs_XformMultiply(lua_State* L) { PushXform(L, ToXform(L, 1) * ToXform(L, 2)); return 1; }
int rs_XformInverse(lua_State* L) { ON_Xform x = ToXform(L, 1); if (!x.Invert()) { lua_pushnil(L); return 1; } PushXform(L, x); return 1; }

// print(): everything goes to the command history (and the script output).
int l_print(lua_State* L) {
  const int n = lua_gettop(L);
  std::string line;
  for (int i = 1; i <= n; ++i) {
    size_t len = 0;
    const char* s = luaL_tolstring(L, i, &len);
    if (i > 1) line += '\t';
    line.append(s, len);
    lua_pop(L, 1);
  }
  Eng(L).Print(line);
  return 0;
}

struct RsEntry {
  const char* name;
  lua_CFunction fn;
  const char* signature;
  const char* doc;
};

// The single source of truth for the rs module: registration and the
// Help > Scripting Reference panel both read this table.
const RsEntry kRs[] = {
    {"Command", rs_Command, "rs.Command(commandLine)", "Runs a Dino 8 command line with its inputs (\"Box 0,0,0 10,10,0 5\"); returns true when it completed."},
    {"AddPoint", rs_AddPoint, "rs.AddPoint(x, y, z) | rs.AddPoint({x,y,z})", "Adds a point object; returns its id."},
    {"AddPoints", rs_AddPoints, "rs.AddPoints(points)", "Adds one point object per {x,y,z}; returns the ids."},
    {"AddLine", rs_AddLine, "rs.AddLine(p0, p1)", "Adds a line curve between two points."},
    {"AddPolyline", rs_AddPolyline, "rs.AddPolyline(points)", "Adds a polyline through the points."},
    {"AddCurve", rs_AddCurve, "rs.AddCurve(points, degree=3)", "Adds a NURBS curve with the points as control points."},
    {"AddInterpCurve", rs_AddInterpCurve, "rs.AddInterpCurve(points)", "Adds a degree-3 curve interpolated through the points."},
    {"AddCircle", rs_AddCircle, "rs.AddCircle(center, radius, normal={0,0,1})", "Adds a circle in the plane with the given normal."},
    {"AddArc3Pt", rs_AddArc3Pt, "rs.AddArc3Pt(start, end, pointOnArc)", "Adds an arc from start to end through a third point."},
    {"AddBox", rs_AddBox, "rs.AddBox(corner, size) | rs.AddBox(corners8)", "Adds a box polysurface: corner + {dx,dy,dz} (or a number), or Rhino's eight corners."},
    {"AddSphere", rs_AddSphere, "rs.AddSphere(center, radius)", "Adds a sphere polysurface."},
    {"AddCylinder", rs_AddCylinder, "rs.AddCylinder(base, height|topPoint, radius, cap=true)", "Adds a cylinder along Z (or towards a top point)."},
    {"AddCone", rs_AddCone, "rs.AddCone(base, height|apex, radius, cap=true)", "Adds a cone."},
    {"AddTorus", rs_AddTorus, "rs.AddTorus(center, majorRadius, minorRadius, normal={0,0,1})", "Adds a torus."},
    {"AddSrfPt", rs_AddSrfPt, "rs.AddSrfPt({p0,p1,p2,p3})", "Adds a surface through three or four corner points."},
    {"AddPlanarSrf", rs_AddPlanarSrf, "rs.AddPlanarSrf(curveIds)", "Adds planar surfaces bounded by closed planar curves; returns the ids."},
    {"AddMesh", rs_AddMesh, "rs.AddMesh(vertices, faces)", "Adds a mesh; faces are lists of 3 or 4 one-based vertex indices."},
    {"ExtrudeCurveStraight", rs_ExtrudeCurveStraight, "rs.ExtrudeCurveStraight(curveId, p0, p1) | (curveId, vector)", "Extrudes a curve along a vector (closed planar curves become capped solids)."},
    {"BooleanUnion", rs_BooleanUnion, "rs.BooleanUnion(ids, delete=true)", "Unions closed solids into one mesh solid; returns the result ids."},
    {"BooleanDifference", rs_BooleanDifference, "rs.BooleanDifference(ids, subtractIds, delete=true)", "Subtracts the second set of solids from the first."},
    {"BooleanIntersection", rs_BooleanIntersection, "rs.BooleanIntersection(ids, otherIds, delete=true)", "Keeps the volume common to both sets."},
    {"MoveObject", rs_MoveObject, "rs.MoveObject(id|ids, vector)", "Translates one object (or a list)."},
    {"MoveObjects", rs_MoveObject, "rs.MoveObjects(ids, vector)", "Translates several objects."},
    {"CopyObject", rs_CopyObject, "rs.CopyObject(id|ids, vector={0,0,0})", "Copies objects, optionally translated; returns the new ids."},
    {"CopyObjects", rs_CopyObject, "rs.CopyObjects(ids, vector={0,0,0})", "Copies several objects."},
    {"RotateObject", rs_RotateObject, "rs.RotateObject(id|ids, center, angleDeg, axis={0,0,1}, copy=false)", "Rotates objects about an axis through a center."},
    {"RotateObjects", rs_RotateObject, "rs.RotateObjects(ids, center, angleDeg, axis, copy)", "Rotates several objects."},
    {"ScaleObject", rs_ScaleObject, "rs.ScaleObject(id|ids, origin, scale|{sx,sy,sz}, copy=false)", "Scales objects about an origin."},
    {"ScaleObjects", rs_ScaleObject, "rs.ScaleObjects(ids, origin, scale, copy)", "Scales several objects."},
    {"MirrorObject", rs_MirrorObject, "rs.MirrorObject(id|ids, start, end, copy=false)", "Mirrors objects across the vertical plane through the line start-end."},
    {"MirrorObjects", rs_MirrorObject, "rs.MirrorObjects(ids, start, end, copy)", "Mirrors several objects."},
    {"TransformObject", rs_TransformObject, "rs.TransformObject(id|ids, xform, copy=false)", "Applies a 4x4 transform."},
    {"TransformObjects", rs_TransformObject, "rs.TransformObjects(ids, xform, copy)", "Applies a 4x4 transform to several objects."},
    {"DeleteObject", rs_DeleteObject, "rs.DeleteObject(id|ids)", "Deletes objects."},
    {"DeleteObjects", rs_DeleteObject, "rs.DeleteObjects(ids)", "Deletes several objects; returns the count."},
    {"SelectObject", rs_SelectObject, "rs.SelectObject(id|ids)", "Selects objects."},
    {"SelectObjects", rs_SelectObject, "rs.SelectObjects(ids)", "Selects several objects; returns the count."},
    {"UnselectObject", rs_UnselectObject, "rs.UnselectObject(id|ids)", "Deselects objects."},
    {"UnselectObjects", rs_UnselectObject, "rs.UnselectObjects(ids)", "Deselects several objects."},
    {"UnselectAllObjects", rs_UnselectAllObjects, "rs.UnselectAllObjects()", "Deselects everything; returns how many were selected."},
    {"SelectedObjects", rs_SelectedObjects, "rs.SelectedObjects()", "Ids of the selected objects."},
    {"AllObjects", rs_AllObjects, "rs.AllObjects(select=false)", "Ids of every object in the document."},
    {"ObjectsByLayer", rs_ObjectsByLayer, "rs.ObjectsByLayer(layerName, select=false)", "Ids of the objects on a layer."},
    {"ObjectsByName", rs_ObjectsByName, "rs.ObjectsByName(name, select=false)", "Ids of the objects with a name."},
    {"ObjectsByType", rs_ObjectsByType, "rs.ObjectsByType(typeMask|\"curve\", select=false)", "Ids by type: 1 point, 4 curve, 8 surface, 16 polysurface, 32 mesh, 262144 SubD (or a name)."},
    {"ObjectName", rs_ObjectName, "rs.ObjectName(id [, name])", "Gets or sets an object's name."},
    {"ObjectLayer", rs_ObjectLayer, "rs.ObjectLayer(id [, layerName])", "Gets or sets an object's layer."},
    {"ObjectColor", rs_ObjectColor, "rs.ObjectColor(id [, {r,g,b}])", "Gets the display colour, or sets a per-object colour (0-255)."},
    {"ObjectType", rs_ObjectType, "rs.ObjectType(id)", "Rhino type mask: 1 point, 4 curve, 8 surface, 16 polysurface, 32 mesh, 262144 SubD."},
    {"ObjectDescription", rs_ObjectDescription, "rs.ObjectDescription(id)", "The What/List text for an object."},
    {"IsObject", rs_IsObject, "rs.IsObject(id)", "True when an object with that id exists."},
    {"IsPoint", rs_IsPoint, "rs.IsPoint(id)", "True for point objects."},
    {"IsCurve", rs_IsCurve, "rs.IsCurve(id)", "True for curves."},
    {"IsSurface", rs_IsSurface, "rs.IsSurface(id)", "True for single surfaces."},
    {"IsPolysurface", rs_IsPolysurface, "rs.IsPolysurface(id)", "True for polysurfaces (breps with several faces)."},
    {"IsBrep", rs_IsBrep, "rs.IsBrep(id)", "True for any brep (surface or polysurface)."},
    {"IsMesh", rs_IsMesh, "rs.IsMesh(id)", "True for meshes."},
    {"IsSubD", rs_IsSubD, "rs.IsSubD(id)", "True for SubD objects."},
    {"IsObjectSelected", rs_IsObjectSelected, "rs.IsObjectSelected(id)", "True when selected."},
    {"IsObjectHidden", rs_IsObjectHidden, "rs.IsObjectHidden(id)", "True when hidden."},
    {"IsObjectLocked", rs_IsObjectLocked, "rs.IsObjectLocked(id)", "True when locked."},
    {"IsObjectSolid", rs_IsObjectSolid, "rs.IsObjectSolid(id)", "True when the object is a closed solid."},
    {"HideObject", rs_HideObject, "rs.HideObject(id|ids)", "Hides objects."},
    {"HideObjects", rs_HideObject, "rs.HideObjects(ids)", "Hides several objects."},
    {"ShowObject", rs_ShowObject, "rs.ShowObject(id|ids)", "Shows hidden objects."},
    {"ShowObjects", rs_ShowObject, "rs.ShowObjects(ids)", "Shows several hidden objects."},
    {"LockObject", rs_LockObject, "rs.LockObject(id|ids)", "Locks objects."},
    {"LockObjects", rs_LockObject, "rs.LockObjects(ids)", "Locks several objects."},
    {"UnlockObject", rs_UnlockObject, "rs.UnlockObject(id|ids)", "Unlocks objects."},
    {"UnlockObjects", rs_UnlockObject, "rs.UnlockObjects(ids)", "Unlocks several objects."},
    {"BoundingBox", rs_BoundingBox, "rs.BoundingBox(id|ids)", "Eight world-axis-aligned corner points (Rhino order: bottom 4 then top 4)."},
    {"CurveLength", rs_CurveLength, "rs.CurveLength(curveId)", "Length of a curve."},
    {"CurveDomain", rs_CurveDomain, "rs.CurveDomain(curveId)", "{t0, t1} parameter domain."},
    {"CurveStartPoint", rs_CurveStartPoint, "rs.CurveStartPoint(curveId)", "Start point."},
    {"CurveEndPoint", rs_CurveEndPoint, "rs.CurveEndPoint(curveId)", "End point."},
    {"CurveMidPoint", rs_CurveMidPoint, "rs.CurveMidPoint(curveId)", "Point at half the arc length."},
    {"CurvePoints", rs_CurvePoints, "rs.CurvePoints(curveId)", "Control points."},
    {"CurvePointCount", rs_CurvePointCount, "rs.CurvePointCount(curveId)", "Number of control points."},
    {"CurveDegree", rs_CurveDegree, "rs.CurveDegree(curveId)", "Degree."},
    {"EvaluateCurve", rs_EvaluateCurve, "rs.EvaluateCurve(curveId, t)", "Point at parameter t."},
    {"CurveTangent", rs_CurveTangent, "rs.CurveTangent(curveId, t)", "Unit tangent at parameter t."},
    {"CurveClosestPoint", rs_CurveClosestPoint, "rs.CurveClosestPoint(curveId, point)", "Parameter of the closest point on the curve."},
    {"CurveArcLengthPoint", rs_CurveArcLengthPoint, "rs.CurveArcLengthPoint(curveId, length)", "Point at a distance along the curve."},
    {"DivideCurve", rs_DivideCurve, "rs.DivideCurve(curveId, segments, createPoints=false, returnPoints=true)", "Points (or parameters) dividing the curve into equal-length segments."},
    {"IsCurveClosed", rs_IsCurveClosed, "rs.IsCurveClosed(curveId)", "True for closed curves."},
    {"IsCurvePlanar", rs_IsCurvePlanar, "rs.IsCurvePlanar(curveId)", "True for planar curves."},
    {"IsCurveLinear", rs_IsCurveLinear, "rs.IsCurveLinear(curveId)", "True for straight curves."},
    {"SurfaceArea", rs_SurfaceArea, "rs.SurfaceArea(id)", "Area of a surface, polysurface, mesh or SubD."},
    {"SurfaceVolume", rs_SurfaceVolume, "rs.SurfaceVolume(id)", "Volume of a closed solid (nil when not closed)."},
    {"SurfaceClosestPoint", rs_SurfaceClosestPoint, "rs.SurfaceClosestPoint(id, point)", "Closest point on a surface, polysurface or mesh."},
    {"MeshVertexCount", rs_MeshVertexCount, "rs.MeshVertexCount(meshId)", "Vertex count."},
    {"MeshFaceCount", rs_MeshFaceCount, "rs.MeshFaceCount(meshId)", "Face count."},
    {"MeshVertices", rs_MeshVertices, "rs.MeshVertices(meshId)", "Vertex positions."},
    {"MeshFaces", rs_MeshFaces, "rs.MeshFaces(meshId)", "Faces as lists of one-based vertex indices."},
    {"MeshVolume", rs_MeshVolume, "rs.MeshVolume(meshId)", "Volume of a closed mesh."},
    {"MeshArea", rs_MeshArea, "rs.MeshArea(meshId)", "Surface area of a mesh."},
    {"AddLayer", rs_AddLayer, "rs.AddLayer(name, color, visible, locked, parent)", "Adds (or updates) a layer; returns its name."},
    {"CurrentLayer", rs_CurrentLayer, "rs.CurrentLayer([layerName])", "Gets or sets the current layer."},
    {"LayerNames", rs_LayerNames, "rs.LayerNames()", "Every layer name."},
    {"LayerCount", rs_LayerCount, "rs.LayerCount()", "Number of layers."},
    {"IsLayer", rs_IsLayer, "rs.IsLayer(name)", "True when the layer exists."},
    {"LayerVisible", rs_LayerVisible, "rs.LayerVisible(name [, visible])", "Gets or sets layer visibility."},
    {"LayerLocked", rs_LayerLocked, "rs.LayerLocked(name [, locked])", "Gets or sets the layer lock."},
    {"LayerColor", rs_LayerColor, "rs.LayerColor(name [, {r,g,b}])", "Gets or sets the layer colour."},
    {"DeleteLayer", rs_DeleteLayer, "rs.DeleteLayer(name)", "Deletes an empty, non-current layer."},
    {"GetUserText", rs_GetUserText, "rs.GetUserText(id [, key])", "Attribute user text value, or every key when no key is given."},
    {"SetUserText", rs_SetUserText, "rs.SetUserText(id|ids, key [, value])", "Sets (or with no value removes) attribute user text."},
    {"GetDocumentUserText", rs_GetDocumentUserText, "rs.GetDocumentUserText([key])", "Document user text value, or every key."},
    {"SetDocumentUserText", rs_SetDocumentUserText, "rs.SetDocumentUserText(key [, value])", "Sets (or removes) document user text."},
    {"GetPoint", rs_GetPoint, "rs.GetPoint(prompt)", "Suspends the script until a point is picked or typed; script tokens after the file name feed it."},
    {"GetObject", rs_GetObject, "rs.GetObject(prompt)", "Suspends until an object is selected (Enter without a selection returns nil)."},
    {"GetObjects", rs_GetObjects, "rs.GetObjects(prompt, filter, minimum=1)", "Suspends until objects are selected; returns the ids."},
    {"GetString", rs_GetString, "rs.GetString(prompt, default)", "Suspends until text is typed."},
    {"GetReal", rs_GetReal, "rs.GetReal(prompt, default)", "Suspends until a number is typed."},
    {"GetInteger", rs_GetInteger, "rs.GetInteger(prompt, default)", "Suspends until an integer is typed."},
    {"MessageBox", rs_MessageBox, "rs.MessageBox(text, buttons, title)", "Prints the text to the history and shows it in a dialog."},
    {"Prompt", rs_Prompt, "rs.Prompt(text)", "Prints a line to the command history."},
    {"Redraw", rs_Redraw, "rs.Redraw()", "No-op: viewports redraw every frame."},
    {"EnableRedraw", rs_EnableRedraw, "rs.EnableRedraw(enable)", "Gets or sets the redraw flag (SetRedrawOn/Off)."},
    {"ZoomExtents", rs_ZoomExtents, "rs.ZoomExtents()", "Zooms every viewport to the model extents."},
    {"UnitSystem", rs_UnitSystem, "rs.UnitSystem([code|name])", "Gets or sets the document unit system (Rhino codes: 2 mm, 3 cm, 4 m, 8 in, 9 ft)."},
    {"UnitSystemName", rs_UnitSystemName, "rs.UnitSystemName()", "The unit system as a word."},
    {"DocumentName", rs_DocumentName, "rs.DocumentName()", "File name of the document, or Untitled."},
    {"DocumentPath", rs_DocumentPath, "rs.DocumentPath()", "Folder of the document, or nil."},
    {"DocumentModified", rs_DocumentModified, "rs.DocumentModified([modified])", "Gets or sets the modified flag."},
    {"Sleep", rs_Sleep, "rs.Sleep(milliseconds)", "No-op (the UI never blocks)."},
    {"Undo", rs_Undo, "rs.Undo()", "Undoes the last change."},
    {"Redo", rs_Redo, "rs.Redo()", "Redoes the last undone change."},
    {"BeginUndo", rs_BeginUndo, "rs.BeginUndo(label)", "Records an undo point with a label."},
    {"CommandHistory", rs_CommandHistory, "rs.CommandHistory()", "The command history text."},
    {"ClearCommandHistory", rs_ClearCommandHistory, "rs.ClearCommandHistory()", "Clears the command history."},
    {"LastCommandName", rs_LastCommandName, "rs.LastCommandName()", "Name of the last command run."},
    {"Version", rs_Version, "rs.Version()", "Dino 8 and Lua version string."},
    {"Distance", rs_Distance, "rs.Distance(p, q)", "Distance between two points."},
    {"VectorAdd", rs_VectorAdd, "rs.VectorAdd(a, b)", "a + b."},
    {"VectorSubtract", rs_VectorSubtract, "rs.VectorSubtract(a, b)", "a - b."},
    {"VectorScale", rs_VectorScale, "rs.VectorScale(v, s)", "v * s."},
    {"VectorDivide", rs_VectorDivide, "rs.VectorDivide(v, s)", "v / s."},
    {"VectorUnitize", rs_VectorUnitize, "rs.VectorUnitize(v)", "Unit vector (nil for a zero vector)."},
    {"VectorLength", rs_VectorLength, "rs.VectorLength(v)", "Length."},
    {"VectorReverse", rs_VectorReverse, "rs.VectorReverse(v)", "-v."},
    {"VectorCrossProduct", rs_VectorCrossProduct, "rs.VectorCrossProduct(a, b)", "Cross product."},
    {"VectorDotProduct", rs_VectorDotProduct, "rs.VectorDotProduct(a, b)", "Dot product."},
    {"VectorCreate", rs_VectorCreate, "rs.VectorCreate(to, from)", "to - from."},
    {"VectorAngle", rs_VectorAngle, "rs.VectorAngle(a, b)", "Angle between vectors in degrees."},
    {"VectorRotate", rs_VectorRotate, "rs.VectorRotate(v, angleDeg, axis)", "Rotates a vector about an axis."},
    {"IsVectorZero", rs_IsVectorZero, "rs.IsVectorZero(v)", "True for the zero vector."},
    {"PointAdd", rs_PointAdd, "rs.PointAdd(p, v)", "p + v."},
    {"PointSubtract", rs_PointSubtract, "rs.PointSubtract(p, q)", "p - q."},
    {"PointScale", rs_PointScale, "rs.PointScale(p, s)", "p * s."},
    {"PointDivide", rs_PointDivide, "rs.PointDivide(p, s)", "p / s."},
    {"PointCompare", rs_PointCompare, "rs.PointCompare(p, q, tolerance)", "True when two points coincide within the tolerance."},
    {"PointTransform", rs_PointTransform, "rs.PointTransform(p, xform)", "Transforms a point."},
    {"VectorTransform", rs_VectorTransform, "rs.VectorTransform(v, xform)", "Transforms a vector (no translation)."},
    {"XformIdentity", rs_XformIdentity, "rs.XformIdentity()", "The identity 4x4."},
    {"XformTranslation", rs_XformTranslation, "rs.XformTranslation(vector)", "Translation transform."},
    {"XformRotation", rs_XformRotation, "rs.XformRotation(angleDeg, axis={0,0,1}, center={0,0,0})", "Rotation transform."},
    {"XformScale", rs_XformScale, "rs.XformScale(scale|{sx,sy,sz}, center={0,0,0})", "Scale transform."},
    {"XformMirror", rs_XformMirror, "rs.XformMirror(point, normal)", "Mirror transform across a plane."},
    {"XformMultiply", rs_XformMultiply, "rs.XformMultiply(a, b)", "a * b (apply b, then a)."},
    {"XformInverse", rs_XformInverse, "rs.XformInverse(xform)", "Inverse transform (nil when singular)."},
};

}  // namespace

// ---------------------------------------------------------------------------
// LuaEngine
// ---------------------------------------------------------------------------

const std::vector<RsFunctionDoc>& LuaEngine::ApiDocs() {
  static const std::vector<RsFunctionDoc> docs = [] {
    std::vector<RsFunctionDoc> d;
    for (const RsEntry& e : kRs) d.push_back({e.name, e.signature, e.doc});
    return d;
  }();
  return docs;
}

LuaEngine::LuaEngine(Application& app) : app_(app) {
  L_ = luaL_newstate();
  luaL_openlibs(L_);
  lua_pushlightuserdata(L_, this);
  lua_setfield(L_, LUA_REGISTRYINDEX, kEngineKey);
  // rs module.
  lua_createtable(L_, 0, static_cast<int>(sizeof(kRs) / sizeof(kRs[0])));
  for (const RsEntry& e : kRs) { lua_pushcfunction(L_, e.fn); lua_setfield(L_, -2, e.name); }
  lua_setglobal(L_, "rs");
  // rhinoscriptsyntax alias so pasted Rhino snippets need fewer edits.
  lua_getglobal(L_, "rs");
  lua_setglobal(L_, "rhinoscriptsyntax");
  lua_pushcfunction(L_, l_print);
  lua_setglobal(L_, "print");
}

LuaEngine::~LuaEngine() {
  if (L_) lua_close(L_);
}

void LuaEngine::Print(const std::string& line) {
  output_.push_back(line);
  if (output_.size() > 500) output_.erase(output_.begin());
  app_.Engine().Print(line);
}

bool LuaEngine::StartThread(const std::string& code, const std::string& chunk_name, bool as_expression) {
  if (thread_) {
    Print("! Script error: a script is already running (nested RunScript is not supported)");
    return false;
  }
  output_.clear();
  request_ = ScriptRequest{};
  chunk_name_ = chunk_name;
  print_results_ = false;
  thread_ = lua_newthread(L_);
  thread_ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
  int rc = LUA_ERRSYNTAX;
  if (as_expression) {
    const std::string wrapped = "return " + code;
    rc = luaL_loadbuffer(thread_, wrapped.c_str(), wrapped.size(), chunk_name.c_str());
    if (rc == LUA_OK) print_results_ = true;
    else lua_pop(thread_, 1);
  }
  if (rc != LUA_OK) rc = luaL_loadbuffer(thread_, code.c_str(), code.size(), chunk_name.c_str());
  if (rc != LUA_OK) {
    const char* msg = lua_tostring(thread_, -1);
    Print(std::string("! Script error: ") + (msg ? msg : "syntax error"));
    Finish(false);
    return false;
  }
  return Resume(0);
}

bool LuaEngine::Start(const std::string& code, const std::string& chunk_name) {
  return StartThread(code, "@" + chunk_name, false);
}

bool LuaEngine::StartExpression(const std::string& expr) {
  return StartThread(expr, "=command line", true);
}

bool LuaEngine::StartFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    Print("! Script error: cannot open " + path);
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string code = ss.str();
  if (code.compare(0, 1, "#") == 0) code.insert(0, "--");  // shebang line
  return Start(code, std::filesystem::path(path).filename().string());
}

bool LuaEngine::Resume(int nargs) {
  request_ = ScriptRequest{};
  in_script_ = true;
  int nres = 0;
  const int rc = lua_resume(thread_, L_, nargs, &nres);
  in_script_ = false;
  if (rc == LUA_YIELD) {
    lua_pop(thread_, nres);
    if (request_.want == ScriptWant::Nothing) {
      // A bare coroutine.yield() from user code: just keep going.
      return Resume(0);
    }
    return true;
  }
  if (rc == LUA_OK) {
    if (print_results_ && nres > 0) {
      // A finished coroutine leaves only its results on the stack (1..nres).
      std::string line;
      for (int i = 1; i <= nres; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(thread_, i, &len);
        if (i > 1) line += '\t';
        line.append(s, len);
        lua_pop(thread_, 1);
      }
      Print(line);
    }
    Finish(true);
    return true;
  }
  const char* msg = lua_tostring(thread_, -1);
  std::string text = msg ? msg : "unknown error";
  Print("! Script error: " + text);
  Finish(false);
  return false;
}

void LuaEngine::Finish(bool ok) {
  (void)ok;
  if (thread_) {
    luaL_unref(L_, LUA_REGISTRYINDEX, thread_ref_);
    thread_ = nullptr;
    thread_ref_ = 0;
  }
  request_ = ScriptRequest{};
}

bool LuaEngine::ResumePoint(Point3d p) {
  if (!thread_) return false;
  PushPoint(thread_, p);
  return Resume(1);
}

bool LuaEngine::ResumeObjects(const std::vector<ObjectId>& ids) {
  if (!thread_) return false;
  if (request_.single_object) {
    if (ids.empty()) lua_pushnil(thread_); else PushId(thread_, ids.front());
  } else {
    PushIds(thread_, ids);
  }
  return Resume(1);
}

bool LuaEngine::ResumeText(const std::string& text) {
  if (!thread_) return false;
  lua_pushstring(thread_, text.c_str());
  return Resume(1);
}

bool LuaEngine::ResumeNumber(double v) {
  if (!thread_) return false;
  if (request_.want == ScriptWant::Integer) lua_pushinteger(thread_, static_cast<lua_Integer>(std::llround(v)));
  else lua_pushnumber(thread_, v);
  return Resume(1);
}

bool LuaEngine::ResumeNil() {
  if (!thread_) return false;
  lua_pushnil(thread_);
  return Resume(1);
}

void LuaEngine::Abort() {
  if (thread_) Print("Script cancelled: " + chunk_name_);
  Finish(false);
}

}  // namespace dino8::app
