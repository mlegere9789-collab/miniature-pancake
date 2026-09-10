#include "sketch/Constraints.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

#include "app/Application.h"
#include "util/json_mini.h"

namespace dino8::sketch {

using kernel::Vector3d;

const char* ConstraintTypeName(ConstraintType t) {
  switch (t) {
    case ConstraintType::Coincident: return "Coincident";
    case ConstraintType::Horizontal: return "Horizontal";
    case ConstraintType::Vertical: return "Vertical";
    case ConstraintType::Parallel: return "Parallel";
    case ConstraintType::Perpendicular: return "Perpendicular";
    case ConstraintType::Tangent: return "Tangent";
    case ConstraintType::EqualLength: return "EqualLength";
    case ConstraintType::EqualRadius: return "EqualRadius";
    case ConstraintType::Distance: return "Distance";
    case ConstraintType::Angle: return "Angle";
    case ConstraintType::Radius: return "Radius";
    case ConstraintType::Fixed: return "Fixed";
    case ConstraintType::Midpoint: return "Midpoint";
    case ConstraintType::Symmetric: return "Symmetric";
  }
  return "Coincident";
}

std::vector<std::string> ConstraintTypeNames() {
  return {"Coincident", "Horizontal", "Vertical", "Parallel", "Perpendicular", "Tangent",
          "EqualLength", "EqualRadius", "Distance", "Angle", "Radius", "Fixed", "Midpoint", "Symmetric"};
}

bool ParseConstraintType(const std::string& s, ConstraintType& out) {
  static const std::pair<const char*, ConstraintType> kTable[] = {
      {"Coincident", ConstraintType::Coincident}, {"Horizontal", ConstraintType::Horizontal},
      {"Vertical", ConstraintType::Vertical}, {"Parallel", ConstraintType::Parallel},
      {"Perpendicular", ConstraintType::Perpendicular}, {"Tangent", ConstraintType::Tangent},
      {"EqualLength", ConstraintType::EqualLength}, {"EqualRadius", ConstraintType::EqualRadius},
      {"Distance", ConstraintType::Distance}, {"Angle", ConstraintType::Angle},
      {"Radius", ConstraintType::Radius}, {"Fixed", ConstraintType::Fixed},
      {"Midpoint", ConstraintType::Midpoint}, {"Symmetric", ConstraintType::Symmetric},
  };
  for (const auto& [name, type] : kTable) {
    if (s == name) { out = type; return true; }
  }
  return false;
}

int RequiredObjectCount(ConstraintType t) {
  switch (t) {
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::Radius:
    case ConstraintType::Fixed:
      return 1;
    case ConstraintType::Symmetric:
      return 3;  // axis, object A, object B
    default:
      return 2;
  }
}

bool RequiresValue(ConstraintType t) {
  return t == ConstraintType::Distance || t == ConstraintType::Angle || t == ConstraintType::Radius;
}

// ---------------------------------------------------------------------------
// Geometry access: line/polyline vertices are ordinary control points;
// circle/arc "points" are their centre, reconstructed from ON_Arc on every
// read/write since NurbsCurve keeps no separate centre/radius state.
// ---------------------------------------------------------------------------
namespace {

bool GetArc(const app::SceneObject& o, ON_Arc& arc) {
  return o.kind == app::ObjectKind::Curve && o.curve && o.curve->raw().IsArc(nullptr, &arc, 1e-6);
}

// World-space position of a PointRef; false if the object/index no longer exists.
bool RefWorldPoint(const Document& doc, const PointRef& r, Point3d& out) {
  const app::SceneObject* o = doc.Find(r.object);
  if (!o || o->kind != app::ObjectKind::Curve || !o->curve) return false;
  if (r.index < 0) {
    ON_Arc arc;
    if (!GetArc(*o, arc)) return false;
    out = arc.Center();
    return true;
  }
  if (r.index >= o->curve->ControlPointCount()) return false;
  out = o->curve->ControlPointAt(r.index);
  return true;
}

double RefRadius(const Document& doc, ObjectId id) {
  const app::SceneObject* o = doc.Find(id);
  ON_Arc arc;
  if (o && GetArc(*o, arc)) return arc.Radius();
  return 0;
}

// Rewrites a circle/arc object's centre and/or radius, preserving its
// plane orientation and angular span (a partial arc stays a partial arc).
void SetArcCenterRadius(app::SceneObject& o, Point3d center, double radius) {
  ON_Arc arc;
  if (!GetArc(o, arc)) return;
  ON_Plane plane = arc.Plane();
  plane.origin = center;
  plane.UpdateEquation();
  ON_Interval dom = arc.Domain();
  ON_Arc new_arc(ON_Circle(plane, std::max(radius, 1e-6)), dom);
  ON_ArcCurve ac(new_arc);
  kernel::NurbsCurve k;
  ON_NurbsCurve nc;
  if (ac.GetNurbForm(nc) > 0) {
    k.raw() = nc;
    *o.curve = k;
    o.InvalidateDisplay();
  }
}

void SetLineVertex(app::SceneObject& o, int index, Point3d p) {
  if (index >= 0 && index < o.curve->ControlPointCount()) {
    o.curve->SetControlPointAt(index, p);
    o.InvalidateDisplay();
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Persistence (Document::UserText()["dino8.constraints"] as a JSON array).
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kUserTextKey = "dino8.constraints";

std::string Escape(const std::string& s) {
  std::string out;
  for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
  return out;
}
}  // namespace

std::vector<Constraint> LoadConstraints(const Document& doc) {
  std::vector<Constraint> out;
  auto it = const_cast<Document&>(doc).UserText().find(kUserTextKey);
  if (it == const_cast<Document&>(doc).UserText().end() || it->second.empty()) return out;
  json::Value root;
  std::string err;
  if (!json::Parse(it->second, root, err) || !root.IsArray()) return out;
  for (size_t i = 0; i < root.Size(); ++i) {
    const json::Value& v = root[i];
    Constraint c;
    c.id = static_cast<int>(v["id"].number);
    ConstraintType t;
    if (!ParseConstraintType(v["type"].AsString(), t)) continue;
    c.type = t;
    c.value = v["value"].number;
    c.fixed_target = Point3d(v["fx"].number, v["fy"].number, v["fz"].number);
    const json::Value& pts = v["points"];
    for (size_t j = 0; j < pts.Size(); ++j) {
      PointRef r;
      r.object = static_cast<ObjectId>(pts[j]["o"].number);
      r.index = static_cast<int>(pts[j]["i"].number);
      c.points.push_back(r);
    }
    const json::Value& rads = v["radius_objects"];
    for (size_t j = 0; j < rads.Size(); ++j) c.radius_objects.push_back(static_cast<ObjectId>(rads[j].number));
    // dino8.constraints is stored in the document's user-text - a plain
    // string field a crafted/corrupted .3dm can set to anything. BuildResiduals
    // indexes c.points[0..N-1]/c.radius_objects[0..M-1] unconditionally per
    // type, with N/M NOT the same as RequiredObjectCount() (that function
    // counts distinct geometry objects, not raw PointRef/point slots - e.g.
    // a Parallel constraint is "2 objects" but needs 4 points, one pair per
    // line). A constraint whose arrays are shorter than what BuildResiduals
    // actually reads for its type must be dropped here, not trusted, or
    // that becomes an out-of-bounds vector read the moment AutoResolveFrame
    // runs (every frame, no explicit command needed).
    int need_pts = 0, need_radius = 0;
    switch (c.type) {
      case ConstraintType::Coincident:
      case ConstraintType::Horizontal:
      case ConstraintType::Vertical:
      case ConstraintType::Distance:
        need_pts = 2; break;
      case ConstraintType::Parallel:
      case ConstraintType::Perpendicular:
      case ConstraintType::EqualLength:
      case ConstraintType::Angle:
      case ConstraintType::Symmetric:
        need_pts = 4; break;
      case ConstraintType::Tangent:
        need_pts = 3; need_radius = 1; break;
      case ConstraintType::Midpoint:
        need_pts = 3; break;
      case ConstraintType::Fixed:
        need_pts = 1; break;
      case ConstraintType::EqualRadius:
        need_radius = 2; break;
      case ConstraintType::Radius:
        need_radius = 1; break;
    }
    if (static_cast<int>(c.points.size()) < need_pts) continue;
    if (static_cast<int>(c.radius_objects.size()) < need_radius) continue;
    out.push_back(c);
  }
  return out;
}

void SaveConstraints(Document& doc, const std::vector<Constraint>& list) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < list.size(); ++i) {
    const Constraint& c = list[i];
    out << (i ? "," : "") << "{\"id\":" << c.id << ",\"type\":\"" << Escape(ConstraintTypeName(c.type)) << "\",\"value\":" << c.value
        << ",\"fx\":" << c.fixed_target.x << ",\"fy\":" << c.fixed_target.y << ",\"fz\":" << c.fixed_target.z
        << ",\"points\":[";
    for (size_t j = 0; j < c.points.size(); ++j) out << (j ? "," : "") << "{\"o\":" << c.points[j].object << ",\"i\":" << c.points[j].index << "}";
    out << "],\"radius_objects\":[";
    for (size_t j = 0; j < c.radius_objects.size(); ++j) out << (j ? "," : "") << c.radius_objects[j];
    out << "]}";
  }
  out << "]";
  doc.UserText()[kUserTextKey] = out.str();
}

int AddConstraint(Document& doc, Constraint c) {
  std::vector<Constraint> list = LoadConstraints(doc);
  int next_id = 1;
  for (const Constraint& e : list) next_id = std::max(next_id, e.id + 1);
  c.id = next_id;
  list.push_back(c);
  SaveConstraints(doc, list);
  return next_id;
}

bool DeleteConstraint(Document& doc, int id) {
  std::vector<Constraint> list = LoadConstraints(doc);
  const size_t before = list.size();
  list.erase(std::remove_if(list.begin(), list.end(), [id](const Constraint& c) { return c.id == id; }), list.end());
  if (list.size() == before) return false;
  SaveConstraints(doc, list);
  return true;
}

void DeleteConstraintsOn(Document& doc, ObjectId object) {
  std::vector<Constraint> list = LoadConstraints(doc);
  auto refs_object = [object](const Constraint& c) {
    for (const PointRef& r : c.points) if (r.object == object) return true;
    for (ObjectId o : c.radius_objects) if (o == object) return true;
    return false;
  };
  const size_t before = list.size();
  list.erase(std::remove_if(list.begin(), list.end(), refs_object), list.end());
  if (list.size() != before) SaveConstraints(doc, list);
}

// ---------------------------------------------------------------------------
// The solver: unknowns are (u,v) per distinct PointRef and r per distinct
// radius object, all packed into one vector `x`. Residuals are built with
// plain arithmetic (see BuildResiduals) and differentiated numerically -
// there are at most a few dozen unknowns in any real sketch, so a
// forward-difference Jacobian is more than fast enough, and it lets every
// constraint type share one generic Levenberg-Marquardt loop instead of
// each needing its own hand-derived gradient.
// ---------------------------------------------------------------------------
namespace {

struct VarSet {
  std::vector<std::pair<ObjectId, int>> point_keys;               // var offset i -> (2*i, 2*i+1) = (u,v)
  std::map<std::pair<ObjectId, int>, int> point_index;
  std::vector<ObjectId> radius_keys;                              // var offset = 2*point_keys.size() + i
  std::map<ObjectId, int> radius_index;

  int PointVar(ObjectId obj, int index) {
    auto key = std::make_pair(obj, index);
    auto it = point_index.find(key);
    if (it != point_index.end()) return it->second;
    int slot = static_cast<int>(point_keys.size());
    point_index[key] = slot;
    point_keys.push_back(key);
    return slot;
  }
  int RadiusVar(ObjectId obj) {
    auto it = radius_index.find(obj);
    if (it != radius_index.end()) return it->second;
    int slot = static_cast<int>(radius_keys.size());
    radius_index[obj] = slot;
    radius_keys.push_back(obj);
    return slot;
  }
  int Size() const { return static_cast<int>(point_keys.size()) * 2 + static_cast<int>(radius_keys.size()); }
};

struct FixedTarget { int point_slot; double u, v; };

// Collects every point/radius unknown referenced by `constraints` and seeds
// `x` from the objects' current geometry (projected onto `cplane`).
void CollectVariables(const Document& doc, const dino8::app::ConstructionPlane& cplane,
                      const std::vector<Constraint>& constraints, VarSet& vars, std::vector<double>& x) {
  for (const Constraint& c : constraints) {
    for (const PointRef& r : c.points) vars.PointVar(r.object, r.index);
    for (ObjectId o : c.radius_objects) vars.RadiusVar(o);
  }
  x.assign(static_cast<size_t>(vars.Size()), 0.0);
  for (size_t i = 0; i < vars.point_keys.size(); ++i) {
    Point3d p;
    if (RefWorldPoint(doc, PointRef{vars.point_keys[i].first, vars.point_keys[i].second}, p)) {
      Vector3d rel = p - cplane.origin;
      x[2 * i] = rel * cplane.x_axis;
      x[2 * i + 1] = rel * cplane.y_axis;
    }
  }
  const size_t base = vars.point_keys.size() * 2;
  for (size_t i = 0; i < vars.radius_keys.size(); ++i) x[base + i] = RefRadius(doc, vars.radius_keys[i]);
}

// One residual contribution per constraint (Coincident/Parallel-style
// constraints contribute 2, everything else 1) - kept in one flat vector
// so the solver never needs to know the per-type layout. `fixed_targets`
// gives each Fixed constraint's pinned (u,v) in the solve's own CPlane,
// precomputed once by the caller (SolveAll) since ON_Plane projection has
// no business inside the per-iteration residual evaluator.
void BuildResiduals(const std::vector<Constraint>& constraints, VarSet& vars, const std::vector<double>& x,
                    const std::map<int, std::pair<double, double>>& fixed_targets, std::vector<double>& r) {
  r.clear();
  auto uv = [&](const PointRef& ref) {
    int slot = vars.PointVar(ref.object, ref.index);
    return std::make_pair(x[2 * slot], x[2 * slot + 1]);
  };
  auto radius = [&](ObjectId o) { return x[vars.RadiusVar(o) + static_cast<int>(vars.point_keys.size()) * 2]; };
  for (const Constraint& c : constraints) {
    switch (c.type) {
      case ConstraintType::Coincident: {
        auto [u0, v0] = uv(c.points[0]); auto [u1, v1] = uv(c.points[1]);
        r.push_back(u0 - u1); r.push_back(v0 - v1);
        break;
      }
      case ConstraintType::Horizontal: {
        auto [u0, v0] = uv(c.points[0]); auto [u1, v1] = uv(c.points[1]);
        (void)u0; (void)u1; r.push_back(v0 - v1);
        break;
      }
      case ConstraintType::Vertical: {
        auto [u0, v0] = uv(c.points[0]); auto [u1, v1] = uv(c.points[1]);
        (void)v0; (void)v1; r.push_back(u0 - u1);
        break;
      }
      case ConstraintType::Parallel:
      case ConstraintType::Perpendicular: {
        auto [u0, v0] = uv(c.points[0]); auto [u1, v1] = uv(c.points[1]);
        auto [u2, v2] = uv(c.points[2]); auto [u3, v3] = uv(c.points[3]);
        double dux = u1 - u0, dvy = v1 - v0, eux = u3 - u2, evy = v3 - v2;
        double len_d = std::hypot(dux, dvy) + 1e-9, len_e = std::hypot(eux, evy) + 1e-9;
        double value = c.type == ConstraintType::Parallel ? (dux * evy - dvy * eux) : (dux * eux + dvy * evy);
        r.push_back(value / (len_d * len_e));
        break;
      }
      case ConstraintType::Tangent: {
        auto [cu, cv] = uv(c.points[0]); auto [u0, v0] = uv(c.points[1]); auto [u1, v1] = uv(c.points[2]);
        double dux = u1 - u0, dvy = v1 - v0, len = std::hypot(dux, dvy) + 1e-9;
        double cross = dux * (cv - v0) - dvy * (cu - u0);
        double dist = std::fabs(cross) / len;
        r.push_back(dist - radius(c.radius_objects[0]));
        break;
      }
      case ConstraintType::EqualLength: {
        auto [u0, v0] = uv(c.points[0]); auto [u1, v1] = uv(c.points[1]);
        auto [u2, v2] = uv(c.points[2]); auto [u3, v3] = uv(c.points[3]);
        r.push_back(std::hypot(u1 - u0, v1 - v0) - std::hypot(u3 - u2, v3 - v2));
        break;
      }
      case ConstraintType::EqualRadius:
        r.push_back(radius(c.radius_objects[0]) - radius(c.radius_objects[1]));
        break;
      case ConstraintType::Distance: {
        auto [u0, v0] = uv(c.points[0]); auto [u1, v1] = uv(c.points[1]);
        r.push_back(std::hypot(u1 - u0, v1 - v0) - c.value);
        break;
      }
      case ConstraintType::Angle: {
        auto [u0, v0] = uv(c.points[0]); auto [u1, v1] = uv(c.points[1]);
        auto [u2, v2] = uv(c.points[2]); auto [u3, v3] = uv(c.points[3]);
        double dux = u1 - u0, dvy = v1 - v0, eux = u3 - u2, evy = v3 - v2;
        double len_d = std::hypot(dux, dvy) + 1e-9, len_e = std::hypot(eux, evy) + 1e-9;
        double cos_actual = (dux * eux + dvy * evy) / (len_d * len_e);
        r.push_back(cos_actual - std::cos(c.value * ON_PI / 180.0));
        break;
      }
      case ConstraintType::Radius:
        r.push_back(radius(c.radius_objects[0]) - c.value);
        break;
      case ConstraintType::Fixed: {
        auto [u0, v0] = uv(c.points[0]);
        auto it = fixed_targets.find(c.id);
        const double tu = it != fixed_targets.end() ? it->second.first : u0;
        const double tv = it != fixed_targets.end() ? it->second.second : v0;
        r.push_back(u0 - tu);
        r.push_back(v0 - tv);
        break;
      }
      case ConstraintType::Midpoint: {
        auto [um, vm] = uv(c.points[0]); auto [u0, v0] = uv(c.points[1]); auto [u1, v1] = uv(c.points[2]);
        r.push_back(um - (u0 + u1) / 2.0);
        r.push_back(vm - (v0 + v1) / 2.0);
        break;
      }
      case ConstraintType::Symmetric: {
        auto [u0, v0] = uv(c.points[0]); auto [u1, v1] = uv(c.points[1]);
        auto [ax0u, ax0v] = uv(c.points[2]); auto [ax1u, ax1v] = uv(c.points[3]);
        double du = u1 - u0, dv = v1 - v0;
        double axu = ax1u - ax0u, axv = ax1v - ax0v, axlen = std::hypot(axu, axv) + 1e-9;
        double dot = du * axu + dv * axv;
        r.push_back(dot / (std::hypot(du, dv) + 1e-9) / axlen);  // segment perpendicular to axis
        double mu = (u0 + u1) / 2.0, mv = (v0 + v1) / 2.0;
        double cross = axu * (mv - ax0v) - axv * (mu - ax0u);
        r.push_back(cross / axlen);  // midpoint on axis
        break;
      }
    }
  }
}

}  // namespace

bool SolveAll(Document& doc, const dino8::app::ConstructionPlane& cplane, std::vector<std::string>& report) {
  std::vector<Constraint> constraints = LoadConstraints(doc);
  report.clear();
  if (constraints.empty()) { report.push_back("No constraints to solve."); return true; }

  VarSet vars;
  std::vector<double> x;
  CollectVariables(doc, cplane, constraints, vars, x);

  // Fixed constraints pin points[0] at the world position recorded when
  // the constraint was created; project that once into this solve's own
  // CPlane coordinates.
  std::map<int, std::pair<double, double>> fixed_targets;
  for (const Constraint& c : constraints) {
    if (c.type == ConstraintType::Fixed) {
      Vector3d rel = c.fixed_target - cplane.origin;
      fixed_targets[c.id] = {rel * cplane.x_axis, rel * cplane.y_axis};
    }
  }

  // Levenberg-Marquardt with a numeric (forward-difference) Jacobian.
  auto residuals_at = [&](const std::vector<double>& xv, std::vector<double>& out) {
    BuildResiduals(constraints, vars, xv, fixed_targets, out);
  };

  const int n = static_cast<int>(x.size());
  if (n == 0) { report.push_back("Constraints reference no live geometry."); return false; }
  std::vector<double> res;
  residuals_at(x, res);
  double lambda = 1e-3;
  bool converged = false;
  for (int iter = 0; iter < 60 && !res.empty(); ++iter) {
    double cost = 0; for (double v : res) cost += v * v;
    if (cost < 1e-16) { converged = true; break; }
    const int m = static_cast<int>(res.size());
    std::vector<std::vector<double>> J(static_cast<size_t>(m), std::vector<double>(static_cast<size_t>(n), 0.0));
    const double h = 1e-6;
    for (int j = 0; j < n; ++j) {
      std::vector<double> xp = x; xp[j] += h;
      std::vector<double> rp; residuals_at(xp, rp);
      for (int i = 0; i < m; ++i) J[static_cast<size_t>(i)][static_cast<size_t>(j)] = (rp[static_cast<size_t>(i)] - res[static_cast<size_t>(i)]) / h;
    }
    // Normal equations (JtJ + lambda*I) dx = -Jt r.
    std::vector<std::vector<double>> A(static_cast<size_t>(n), std::vector<double>(static_cast<size_t>(n), 0.0));
    std::vector<double> b(static_cast<size_t>(n), 0.0);
    for (int a = 0; a < n; ++a) {
      for (int c2 = 0; c2 < n; ++c2) { double s = 0; for (int i = 0; i < m; ++i) s += J[static_cast<size_t>(i)][static_cast<size_t>(a)] * J[static_cast<size_t>(i)][static_cast<size_t>(c2)]; A[static_cast<size_t>(a)][static_cast<size_t>(c2)] = s; }
      A[static_cast<size_t>(a)][static_cast<size_t>(a)] += lambda;
      double s = 0; for (int i = 0; i < m; ++i) s += J[static_cast<size_t>(i)][static_cast<size_t>(a)] * res[static_cast<size_t>(i)];
      b[static_cast<size_t>(a)] = -s;
    }
    // Gaussian elimination with partial pivoting.
    std::vector<double> dx(static_cast<size_t>(n), 0.0);
    bool ok = true;
    for (int col = 0; col < n && ok; ++col) {
      int piv = col;
      for (int row = col + 1; row < n; ++row) if (std::fabs(A[static_cast<size_t>(row)][static_cast<size_t>(col)]) > std::fabs(A[static_cast<size_t>(piv)][static_cast<size_t>(col)])) piv = row;
      if (std::fabs(A[static_cast<size_t>(piv)][static_cast<size_t>(col)]) < 1e-14) { ok = false; break; }
      std::swap(A[static_cast<size_t>(piv)], A[static_cast<size_t>(col)]);
      std::swap(b[static_cast<size_t>(piv)], b[static_cast<size_t>(col)]);
      for (int row = col + 1; row < n; ++row) {
        double f = A[static_cast<size_t>(row)][static_cast<size_t>(col)] / A[static_cast<size_t>(col)][static_cast<size_t>(col)];
        for (int k = col; k < n; ++k) A[static_cast<size_t>(row)][static_cast<size_t>(k)] -= f * A[static_cast<size_t>(col)][static_cast<size_t>(k)];
        b[static_cast<size_t>(row)] -= f * b[static_cast<size_t>(col)];
      }
    }
    if (ok) {
      for (int row = n - 1; row >= 0; --row) {
        double s = b[static_cast<size_t>(row)];
        for (int k = row + 1; k < n; ++k) s -= A[static_cast<size_t>(row)][static_cast<size_t>(k)] * dx[static_cast<size_t>(k)];
        dx[static_cast<size_t>(row)] = s / A[static_cast<size_t>(row)][static_cast<size_t>(row)];
      }
    }
    std::vector<double> x_try = x;
    if (ok) for (int i = 0; i < n; ++i) x_try[static_cast<size_t>(i)] += dx[static_cast<size_t>(i)];
    std::vector<double> res_try; residuals_at(x_try, res_try);
    double cost_try = 0; for (double v : res_try) cost_try += v * v;
    if (ok && cost_try < cost) { x = x_try; res = res_try; lambda = std::max(lambda * 0.5, 1e-10); }
    else { lambda *= 4.0; }
  }
  {
    double cost = 0; for (double v : res) cost += v * v;
    converged = cost < 1e-8;
  }

  // Write the solved variables back into the document's geometry.
  doc.BeginChange("ConstraintSolve");
  for (size_t i = 0; i < vars.point_keys.size(); ++i) {
    const auto& [obj, idx] = vars.point_keys[i];
    app::SceneObject* o = doc.Find(obj);
    if (!o || !o->curve) continue;
    Point3d p = cplane.origin + cplane.x_axis * x[2 * i] + cplane.y_axis * x[2 * i + 1];
    if (idx >= 0) SetLineVertex(*o, idx, p);
    else {
      ON_Arc arc;
      double radius = GetArc(*o, arc) ? arc.Radius() : 1.0;
      auto rit = vars.radius_index.find(obj);
      if (rit != vars.radius_index.end()) radius = x[vars.point_keys.size() * 2 + static_cast<size_t>(rit->second)];
      SetArcCenterRadius(*o, p, radius);
    }
  }
  for (size_t i = 0; i < vars.radius_keys.size(); ++i) {
    ObjectId obj = vars.radius_keys[i];
    // Skip objects whose centre was not itself a variable (handled above);
    // apply the radius-only update here.
    if (vars.point_index.count({obj, -1})) continue;
    app::SceneObject* o = doc.Find(obj);
    if (!o || !o->curve) continue;
    ON_Arc arc;
    if (GetArc(*o, arc)) SetArcCenterRadius(*o, arc.Center(), x[vars.point_keys.size() * 2 + i]);
  }

  // Human-readable report: current length/angle/radius for every
  // constraint that carries one of those measurements (used by
  // ConstraintSolve's printout and the smoke test).
  for (const Constraint& c : constraints) {
    std::ostringstream line;
    line << ConstraintTypeName(c.type) << " #" << c.id << ": ";
    if (c.type == ConstraintType::Distance || c.type == ConstraintType::EqualLength) {
      Point3d a, b;
      if (RefWorldPoint(doc, c.points[0], a) && RefWorldPoint(doc, c.points[1], b)) line << "length " << (a - b).Length();
    } else if (c.type == ConstraintType::Angle || c.type == ConstraintType::Parallel || c.type == ConstraintType::Perpendicular) {
      Point3d p0, p1, p2, p3;
      if (RefWorldPoint(doc, c.points[0], p0) && RefWorldPoint(doc, c.points[1], p1) && RefWorldPoint(doc, c.points[2], p2) && RefWorldPoint(doc, c.points[3], p3)) {
        Vector3d d0 = p1 - p0, d1 = p3 - p2;
        double cos_a = (d0 * d1) / (d0.Length() * d1.Length() + 1e-12);
        cos_a = std::clamp(cos_a, -1.0, 1.0);
        line << "angle " << (std::acos(cos_a) * 180.0 / ON_PI);
      }
    } else if (c.type == ConstraintType::Radius || c.type == ConstraintType::EqualRadius) {
      line << "radius " << RefRadius(doc, c.radius_objects[0]);
    } else {
      line << "ok";
    }
    report.push_back(line.str());
  }
  return converged;
}

// ---------------------------------------------------------------------------
// Per-frame auto-resolve: cheap bounding-box signature per constrained
// object, so a Move/gumball drag (which does not know constraints exist)
// gets pulled back into agreement without every command needing to know
// about the solver.
// ---------------------------------------------------------------------------
void AutoResolveFrame(dino8::app::Application& app) {
  Document& doc = app.Doc();
  std::vector<Constraint> constraints = LoadConstraints(doc);
  if (constraints.empty()) return;
  static std::map<ObjectId, kernel::BoundingBox> last_bbox;
  bool changed = false;
  std::vector<ObjectId> ids;
  for (const Constraint& c : constraints) {
    for (const PointRef& r : c.points) ids.push_back(r.object);
    for (ObjectId o : c.radius_objects) ids.push_back(o);
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  for (ObjectId id : ids) {
    const app::SceneObject* o = doc.Find(id);
    if (!o) continue;
    kernel::BoundingBox bb = o->BoundingBox();
    auto it = last_bbox.find(id);
    if (it == last_bbox.end() || (it->second.min - bb.min).Length() > 1e-9 || (it->second.max - bb.max).Length() > 1e-9) changed = true;
  }
  if (!changed) return;
  dino8::app::Viewport* vp = app.ActiveViewport();
  if (!vp) return;
  std::vector<std::string> report;
  SolveAll(doc, vp->CPlane(), report);
  last_bbox.clear();
  for (ObjectId id : ids) if (const app::SceneObject* o = doc.Find(id)) last_bbox[id] = o->BoundingBox();
}

std::vector<float> BuildGlyphs(const Document& doc, const dino8::app::ConstructionPlane& cplane) {
  std::vector<float> lines;
  (void)cplane;
  std::vector<Constraint> constraints = LoadConstraints(doc);
  auto push = [&](Point3d a, Point3d b) {
    lines.insert(lines.end(), {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z),
                               static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z)});
  };
  for (const Constraint& c : constraints) {
    if (c.points.empty()) continue;
    Point3d p0;
    if (!RefWorldPoint(doc, c.points[0], p0)) continue;
    const double s = 0.6;  // glyph half-size in model units
    switch (c.type) {
      case ConstraintType::Horizontal: push(p0 - Vector3d(s, 0, 0), p0 + Vector3d(s, 0, 0)); break;
      case ConstraintType::Vertical: push(p0 - Vector3d(0, s, 0), p0 + Vector3d(0, s, 0)); break;
      case ConstraintType::Fixed:
        push(p0 + Vector3d(-s, -s, 0), p0 + Vector3d(s, s, 0));
        push(p0 + Vector3d(-s, s, 0), p0 + Vector3d(s, -s, 0));
        break;
      case ConstraintType::Coincident:
        push(p0 + Vector3d(-s, 0, 0), p0 + Vector3d(s, 0, 0));
        push(p0 + Vector3d(0, -s, 0), p0 + Vector3d(0, s, 0));
        break;
      default:
        push(p0 + Vector3d(-s * 0.5, -s * 0.5, 0), p0 + Vector3d(s * 0.5, s * 0.5, 0));
        break;
    }
  }
  return lines;
}

}  // namespace dino8::sketch
