#include "flow/FlowData.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dino8::flow {

const char* KindName(Kind k) {
  switch (k) {
    case Kind::Null: return "Null";
    case Kind::Number: return "Number";
    case Kind::Integer: return "Integer";
    case Kind::Boolean: return "Boolean";
    case Kind::Text: return "Text";
    case Kind::Point: return "Point";
    case Kind::Vector: return "Vector";
    case Kind::Plane: return "Plane";
    case Kind::Curve: return "Curve";
    case Kind::Surface: return "Surface";
    case Kind::Brep: return "Brep";
    case Kind::Mesh: return "Mesh";
    case Kind::Colour: return "Colour";
    case Kind::Any: return "Any";
  }
  return "Null";
}

Kind KindFromName(const std::string& n) {
  static const Kind all[] = {Kind::Null, Kind::Number, Kind::Integer, Kind::Boolean, Kind::Text, Kind::Point, Kind::Vector, Kind::Plane, Kind::Curve, Kind::Surface, Kind::Brep, Kind::Mesh, Kind::Colour, Kind::Any};
  for (Kind k : all) if (n == KindName(k)) return k;
  return Kind::Any;
}

void KindColor(Kind k, float* c) {
  auto set = [&](int r, int g, int b) { c[0] = r / 255.f; c[1] = g / 255.f; c[2] = b / 255.f; c[3] = 1.f; };
  switch (k) {
    case Kind::Number: set(96, 190, 255); break;
    case Kind::Integer: set(72, 160, 230); break;
    case Kind::Boolean: set(230, 96, 96); break;
    case Kind::Text: set(230, 200, 90); break;
    case Kind::Point: set(255, 128, 64); break;
    case Kind::Vector: set(200, 120, 255); break;
    case Kind::Plane: set(120, 220, 200); break;
    case Kind::Curve: set(80, 210, 120); break;
    case Kind::Surface: set(110, 200, 170); break;
    case Kind::Brep: set(70, 180, 150); break;
    case Kind::Mesh: set(160, 200, 100); break;
    case Kind::Colour: set(255, 100, 180); break;
    default: set(190, 190, 190); break;
  }
}

bool KindCompatible(Kind from, Kind to) {
  if (from == to || to == Kind::Any || from == Kind::Any || from == Kind::Null) return true;
  auto numeric = [](Kind k) { return k == Kind::Number || k == Kind::Integer || k == Kind::Boolean || k == Kind::Text; };
  if (numeric(from) && numeric(to)) return true;
  if (to == Kind::Text) return true;
  if ((from == Kind::Point || from == Kind::Vector || from == Kind::Plane) && (to == Kind::Point || to == Kind::Vector || to == Kind::Plane)) return true;
  if (from == Kind::Surface && to == Kind::Brep) return true;
  if ((from == Kind::Brep || from == Kind::Surface || from == Kind::Mesh) && (to == Kind::Brep || to == Kind::Surface || to == Kind::Mesh)) return true;
  return false;
}

bool Value::AsNumber(double& out) const {
  switch (kind) {
    case Kind::Number: case Kind::Integer: case Kind::Boolean: out = num; return true;
    case Kind::Text: {
      char* end = nullptr;
      const double v = std::strtod(text.c_str(), &end);
      if (end && end != text.c_str()) { out = v; return true; }
      return false;
    }
    case Kind::Point: case Kind::Vector: out = Vector3d(point.x, point.y, point.z).Length(); return true;
    default: return false;
  }
}

bool Value::AsBool(bool& out) const {
  if (kind == Kind::Text) {
    std::string t = text;
    for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t == "true" || t == "yes" || t == "on") { out = true; return true; }
    if (t == "false" || t == "no" || t == "off") { out = false; return true; }
  }
  double v;
  if (!AsNumber(v)) { if (IsNull()) return false; out = true; return true; }
  out = v != 0;
  return true;
}

bool Value::AsPoint(Point3d& out) const {
  switch (kind) {
    case Kind::Point: case Kind::Vector: out = point; return true;
    case Kind::Plane: out = plane.origin; return true;
    case Kind::Text: {
      double x = 0, y = 0, z = 0;
      const int n = std::sscanf(text.c_str(), " {%lf , %lf , %lf", &x, &y, &z) == 3 ? 3 : std::sscanf(text.c_str(), "%lf , %lf , %lf", &x, &y, &z);
      if (n >= 2) { out = Point3d(x, y, z); return true; }
      return false;
    }
    case Kind::Number: case Kind::Integer: out = Point3d(num, num, num); return true;
    case Kind::Curve: if (curve) { out = curve->PointAt(curve->Domain().min); return true; } return false;
    default: return false;
  }
}

bool Value::AsVector(Vector3d& out) const {
  if (kind == Kind::Plane) { out = plane.Normal(); return true; }
  Point3d p;
  if (!AsPoint(p)) return false;
  out = Vector3d(p.x, p.y, p.z);
  return true;
}

bool Value::AsPlane(Plane& out) const {
  if (kind == Kind::Plane) { out = plane; return true; }
  if (kind == Kind::Point) { out = Plane{}; out.origin = point; return true; }
  if (kind == Kind::Vector) {
    ON_Plane p(ON_origin, Vector3d(point.x, point.y, point.z));
    if (!p.IsValid()) return false;
    out = Plane::FromON(p);
    return true;
  }
  if (kind == Kind::Curve && curve) {
    ON_Plane p;
    if (curve->raw().IsPlanar(&p, 1e-6)) { out = Plane::FromON(p); return true; }
    out = Plane{}; out.origin = curve->PointAt(curve->Domain().min); return true;
  }
  return false;
}

bool Value::AsColour(Colour& out) const {
  if (kind == Kind::Colour) { out = colour; return true; }
  if (kind == Kind::Text) {
    int r, g, b;
    if (std::sscanf(text.c_str(), "%d , %d , %d", &r, &g, &b) == 3) { out = Colour{r / 255.f, g / 255.f, b / 255.f, 1.f}; return true; }
  }
  Point3d p;
  if (AsPoint(p)) { out = Colour{static_cast<float>(p.x / 255.0), static_cast<float>(p.y / 255.0), static_cast<float>(p.z / 255.0), 1.f}; return true; }
  return false;
}

static std::string Fmt(double v) {
  char buf[64];
  if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", std::round(v));
  else std::snprintf(buf, sizeof buf, "%.4g", v);
  return buf;
}

std::string Value::AsText() const {
  switch (kind) {
    case Kind::Null: return "<null>";
    case Kind::Number: return Fmt(num);
    case Kind::Integer: return Fmt(std::round(num));
    case Kind::Boolean: return num != 0 ? "True" : "False";
    case Kind::Text: return text;
    case Kind::Point: return "{" + Fmt(point.x) + ", " + Fmt(point.y) + ", " + Fmt(point.z) + "}";
    case Kind::Vector: return "{" + Fmt(point.x) + ", " + Fmt(point.y) + ", " + Fmt(point.z) + "}";
    case Kind::Plane: return "Plane " + Value::Point(plane.origin).AsText();
    case Kind::Curve: return curve ? (curve->IsClosed() ? "Closed curve, L=" : "Curve, L=") + Fmt(curve->Length(200)) : "Curve";
    case Kind::Surface: return "Surface";
    case Kind::Brep: return brep ? "Brep (" + std::to_string(brep->FaceCount()) + " faces)" : "Brep";
    case Kind::Mesh: return mesh ? "Mesh (" + std::to_string(mesh->FaceCount()) + " faces)" : "Mesh";
    case Kind::Colour: return Fmt(std::round(colour.r * 255)) + "," + Fmt(std::round(colour.g * 255)) + "," + Fmt(std::round(colour.b * 255));
    case Kind::Any: return "?";
  }
  return "";
}

Value Value::Transformed(const ON_Xform& x) const {
  Value r = *this;
  switch (kind) {
    case Kind::Point: r.point = x * point; break;
    case Kind::Vector: { Vector3d v(point.x, point.y, point.z); v.Transform(x); r.point = Point3d(v.x, v.y, v.z); break; }
    case Kind::Plane: { ON_Plane p = plane.ToON(); p.Transform(x); r.plane = Plane::FromON(p); r.point = r.plane.origin; break; }
    case Kind::Curve: if (curve) { kernel::NurbsCurve c = *curve; c.raw().Transform(x); r.curve = std::make_shared<kernel::NurbsCurve>(std::move(c)); } break;
    case Kind::Surface: if (surface) { kernel::NurbsSurface s = *surface; s.raw().Transform(x); r.surface = std::make_shared<kernel::NurbsSurface>(std::move(s)); } break;
    case Kind::Brep: if (brep) { kernel::Brep b = *brep; b.raw().Transform(x); r.brep = std::make_shared<kernel::Brep>(std::move(b)); } break;
    case Kind::Mesh: if (mesh) { r.mesh = std::make_shared<kernel::Mesh>(mesh->Transform(x)); } break;
    default: break;
  }
  return r;
}

std::string PathString(const std::vector<int>& path) {
  std::string s = "{";
  for (size_t i = 0; i < path.size(); ++i) s += (i ? ";" : "") + std::to_string(path[i]);
  return s + "}";
}

bool Tree::Empty() const { return ItemCount() == 0; }

size_t Tree::ItemCount() const {
  size_t n = 0;
  for (const Branch& b : branches) n += b.items.size();
  return n;
}

Branch& Tree::BranchFor(const std::vector<int>& path) {
  for (Branch& b : branches) if (b.path == path) return b;
  branches.push_back(Branch{path, {}});
  return branches.back();
}

void Tree::Add(const Value& v, const std::vector<int>& path) { BranchFor(path).items.push_back(v); }

std::vector<Value> Tree::AllItems() const {
  std::vector<Value> out;
  for (const Branch& b : branches) out.insert(out.end(), b.items.begin(), b.items.end());
  return out;
}

const Value* Tree::First() const {
  for (const Branch& b : branches) if (!b.items.empty()) return &b.items.front();
  return nullptr;
}

Tree Tree::Flattened() const {
  Tree t;
  Branch b;
  b.path = {0};
  b.items = AllItems();
  t.branches.push_back(std::move(b));
  return t;
}

Tree Tree::Grafted() const {
  Tree t;
  for (const Branch& b : branches) {
    for (size_t i = 0; i < b.items.size(); ++i) {
      std::vector<int> p = b.path;
      p.push_back(static_cast<int>(i));
      t.branches.push_back(Branch{p, {b.items[i]}});
    }
  }
  return t;
}

Tree Tree::Simplified() const {
  if (branches.size() <= 1) { Tree t = *this; for (Branch& b : t.branches) b.path = {0}; return t; }
  // Drop leading path elements shared by every branch.
  size_t common = branches.front().path.size();
  for (const Branch& b : branches) {
    size_t k = 0;
    while (k < common && k < b.path.size() && b.path[k] == branches.front().path[k]) ++k;
    common = k;
  }
  Tree t = *this;
  for (Branch& b : t.branches) {
    if (common > 0 && b.path.size() > common) b.path.erase(b.path.begin(), b.path.begin() + static_cast<long>(common));
    if (b.path.empty()) b.path = {0};
  }
  return t;
}

Tree Tree::FromList(const std::vector<Value>& items, const std::vector<int>& path) {
  Tree t;
  t.branches.push_back(Branch{path, items});
  return t;
}

std::string Tree::Summary() const {
  const size_t n = ItemCount();
  if (branches.empty() || n == 0) return "empty";
  if (branches.size() == 1) return n == 1 ? "1 item" : std::to_string(n) + " items";
  return std::to_string(n) + " items in " + std::to_string(branches.size()) + " branches";
}

}  // namespace dino8::flow
