// Dino Flow data model: the values that travel along wires (numbers, text,
// points, vectors, planes, curves, surfaces, breps, meshes, colours) and the
// Grasshopper-style data tree (branches addressed by an integer path) that
// carries lists of them.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/types.h"

namespace dino8::flow {

using kernel::Point3d;
using kernel::Vector3d;

enum class Kind { Null, Number, Integer, Boolean, Text, Point, Vector, Plane, Curve, Surface, Brep, Mesh, Colour, Any };

const char* KindName(Kind k);
Kind KindFromName(const std::string& name);
// Port colour for a kind (RGBA 0..1).
void KindColor(Kind k, float* rgba);
// Whether a value of `from` may be plugged into a port of `to`.
bool KindCompatible(Kind from, Kind to);

struct Plane {
  Point3d origin{0, 0, 0};
  Vector3d x{1, 0, 0};
  Vector3d y{0, 1, 0};
  Vector3d Normal() const { return ON_CrossProduct(x, y); }
  ON_Plane ToON() const { return ON_Plane(origin, x, y); }
  static Plane FromON(const ON_Plane& p) { Plane r; r.origin = p.origin; r.x = p.xaxis; r.y = p.yaxis; return r; }
  Point3d At(double u, double v, double w = 0) const { return origin + x * u + y * v + Normal() * w; }
};

struct Colour {
  float r = 0.8f, g = 0.2f, b = 0.2f, a = 1.f;
};

// One value. Geometry is shared (immutable once created) so lists of the
// same curve can be copied cheaply through the graph.
struct Value {
  Kind kind = Kind::Null;
  double num = 0;  // Number / Integer / Boolean
  std::string text;
  Point3d point{0, 0, 0};  // Point / Vector (also used as xyz storage)
  Plane plane;
  Colour colour;
  std::shared_ptr<const kernel::NurbsCurve> curve;
  std::shared_ptr<const kernel::NurbsSurface> surface;
  std::shared_ptr<const kernel::Brep> brep;
  std::shared_ptr<const kernel::Mesh> mesh;

  static Value Null() { return Value{}; }
  static Value Number(double v) { Value r; r.kind = Kind::Number; r.num = v; return r; }
  static Value Integer(long long v) { Value r; r.kind = Kind::Integer; r.num = static_cast<double>(v); return r; }
  static Value Boolean(bool v) { Value r; r.kind = Kind::Boolean; r.num = v ? 1 : 0; return r; }
  static Value Text(std::string v) { Value r; r.kind = Kind::Text; r.text = std::move(v); return r; }
  static Value Point(Point3d p) { Value r; r.kind = Kind::Point; r.point = p; return r; }
  static Value Vector(Vector3d v) { Value r; r.kind = Kind::Vector; r.point = Point3d(v.x, v.y, v.z); return r; }
  static Value PlaneV(const Plane& p) { Value r; r.kind = Kind::Plane; r.plane = p; r.point = p.origin; return r; }
  static Value ColourV(const Colour& c) { Value r; r.kind = Kind::Colour; r.colour = c; return r; }
  static Value Curve(kernel::NurbsCurve c) { Value r; r.kind = Kind::Curve; r.curve = std::make_shared<kernel::NurbsCurve>(std::move(c)); return r; }
  static Value Surface(kernel::NurbsSurface s) { Value r; r.kind = Kind::Surface; r.surface = std::make_shared<kernel::NurbsSurface>(std::move(s)); return r; }
  static Value BrepV(kernel::Brep b) { Value r; r.kind = Kind::Brep; r.brep = std::make_shared<kernel::Brep>(std::move(b)); return r; }
  static Value MeshV(kernel::Mesh m) { Value r; r.kind = Kind::Mesh; r.mesh = std::make_shared<kernel::Mesh>(std::move(m)); return r; }

  bool IsNull() const { return kind == Kind::Null; }
  bool IsGeometry() const { return kind == Kind::Point || kind == Kind::Curve || kind == Kind::Surface || kind == Kind::Brep || kind == Kind::Mesh || kind == Kind::Plane || kind == Kind::Vector; }
  // Conversions (Text parses, Boolean/Integer widen, Point <-> Vector).
  bool AsNumber(double& out) const;
  bool AsBool(bool& out) const;
  bool AsPoint(Point3d& out) const;
  bool AsVector(Vector3d& out) const;
  bool AsPlane(Plane& out) const;
  bool AsColour(Colour& out) const;
  std::string AsText() const;  // human-readable form ("12.5", "{0,0,0}", "Curve")
  // Applies a transform to geometry values (numbers/text pass through).
  Value Transformed(const ON_Xform& x) const;
};

// A branch of a data tree: a path like {0;1} and its items.
struct Branch {
  std::vector<int> path{0};
  std::vector<Value> items;
};

std::string PathString(const std::vector<int>& path);

struct Tree {
  std::vector<Branch> branches;

  bool Empty() const;
  size_t ItemCount() const;
  void Clear() { branches.clear(); }
  // Appends `v` to the branch with `path` (created at the end when missing).
  void Add(const Value& v, const std::vector<int>& path = {0});
  Branch& BranchFor(const std::vector<int>& path);
  std::vector<Value> AllItems() const;
  const Value* First() const;
  Tree Flattened() const;
  Tree Grafted() const;
  Tree Simplified() const;
  static Tree Single(const Value& v) { Tree t; t.Add(v); return t; }
  static Tree FromList(const std::vector<Value>& items, const std::vector<int>& path = {0});
  std::string Summary() const;  // "8 items in 1 branch"
};

}  // namespace dino8::flow
