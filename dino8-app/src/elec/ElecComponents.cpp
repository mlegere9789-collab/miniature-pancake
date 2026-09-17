#include "elec/ElecComponents.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "app/Application.h"
#include "doc/SceneObject.h"
#include "util/json_mini.h"

namespace dino8::elec {

using app::SceneObject;

const char* ElecTypeName(ElecType t) {
  switch (t) {
    case ElecType::Resistor: return "Resistor";
    case ElecType::Capacitor: return "Capacitor";
    case ElecType::Switch: return "Switch";
    case ElecType::Ground: return "Ground";
    case ElecType::Lamp: return "Lamp";
    case ElecType::WireRun: return "WireRun";
  }
  return "Resistor";
}

bool ParseElecType(const std::string& s, ElecType& out) {
  static const std::pair<const char*, ElecType> kTable[] = {
      {"Resistor", ElecType::Resistor}, {"Capacitor", ElecType::Capacitor}, {"Switch", ElecType::Switch},
      {"Ground", ElecType::Ground}, {"Lamp", ElecType::Lamp}, {"WireRun", ElecType::WireRun},
  };
  for (const auto& [name, type] : kTable) if (s == name) { out = type; return true; }
  return false;
}

std::vector<std::string> ElecTypeNames() {
  return {"Resistor", "Capacitor", "Switch", "Ground", "Lamp", "WireRun"};
}

// ---------------------------------------------------------------------------
// Geometry: every symbol is one or more flat curves built in a local
// (forward, right) frame - forward = unit(p1-p0), right = normal x forward -
// the same construction ArchComponents.cpp's MakeRunFrame uses for a Wall/
// Beam's local frame, just without the third (up) axis since these are flat
// schematic symbols rather than solids.
// ---------------------------------------------------------------------------
namespace {

Vector3d UnitOf(Vector3d v) { v.Unitize(); return v; }

struct Frame2D {
  Vector3d forward, right;
};

Frame2D MakeFrame(Point3d p0, Point3d p1, Vector3d up) {
  Frame2D f;
  Vector3d d = p1 - p0;
  f.forward = d.Length() > 1e-9 ? UnitOf(d) : Vector3d(1, 0, 0);
  Vector3d n = up.Length() > 1e-9 ? UnitOf(up) : Vector3d(0, 0, 1);
  f.right = kernel::Vector3d::CrossProduct(n, f.forward);
  if (f.right.Length() < 1e-9) f.right = Vector3d(0, 1, 0);
  else f.right = UnitOf(f.right);
  return f;
}

// Straight-segment (degree-1) NURBS curve through 2+ points - self-contained
// equivalent of cmd_common.h's PolylineCurve() (that helper lives in the
// commands/ layer, which this arch-style component layer doesn't depend on,
// the same separation ArchComponents.cpp itself keeps for its own OrientedBox
// mesh helper).
kernel::NurbsCurve Polyline(const std::vector<Point3d>& pts) {
  return kernel::NurbsCurve::FromControlPoints(pts, 1);
}

kernel::NurbsCurve Poly2(Point3d origin, const Frame2D& f, const std::vector<std::pair<double, double>>& ij) {
  std::vector<Point3d> pts;
  for (const auto& [i, j] : ij) pts.push_back(origin + f.forward * i + f.right * j);
  return Polyline(pts);
}

// Resistor: a lead-in, three zigzag peaks (alternating +-height/2), a
// lead-out - the common schematic zigzag. Built directly at the requested
// length/height (no extra scale factor): with 6 equal segments spanning i
// in [0, length] and j alternating {0, +h/2, -h/2, +h/2, -h/2, +h/2, 0}, the
// resulting bounding box is exactly [0, length] x [-height/2, height/2], so
// BoundingBox's own width/height read back `length`/`height` directly.
//
// Built as 6 separate two-point curve objects (one per zigzag segment)
// rather than one continuous 7-point polyline: every corner is then a real
// curve *endpoint*, which SceneObject's own display-bbox cache (built from
// NurbsCurve::SuggestedParameterValues's adaptive chord-flatness sampling,
// see doc/SceneObject.cpp's AppendCurvePolyline) always includes exactly -
// whereas a sharp interior vertex of a single many-segment polyline can sit
// off-center within that sampler's current subdivision chord and be
// undershot. Six short line objects read identically as one zigzag symbol
// and keep BoundingBox exact, the same "many simple curves, not one
// intricate one" shape Ground's own rungs already use below.
std::vector<kernel::NurbsCurve> BuildResistor(const ElecComponent& c) {
  Frame2D f = MakeFrame(c.p0, c.p1, c.normal);
  const double L = c.length, H = c.height;
  const double j[7] = {0, H / 2, -H / 2, H / 2, -H / 2, H / 2, 0};
  std::vector<kernel::NurbsCurve> out;
  for (int k = 0; k < 6; ++k) out.push_back(Poly2(c.p0, f, {{L * k / 6.0, j[k]}, {L * (k + 1) / 6.0, j[k + 1]}}));
  return out;
}

// Capacitor: two parallel plate segments straddling p0 along `forward` by
// +-gap/2, each spanning +-plate_length/2 along `right` - so the bounding
// box is exactly [-gap/2, gap/2] x [-plate_length/2, plate_length/2] (width
// = gap, height = plate_length, read back directly by BoundingBox).
std::vector<kernel::NurbsCurve> BuildCapacitor(const ElecComponent& c) {
  Frame2D f = MakeFrame(c.p0, c.p1, c.normal);
  const double g2 = c.gap / 2.0, p2 = c.plate_length / 2.0;
  return {Poly2(c.p0, f, {{-g2, -p2}, {-g2, p2}}), Poly2(c.p0, f, {{g2, -p2}, {g2, p2}})};
}

// Switch: an open knife-blade symbol. Two fixed ratios of `length` (a
// terminal-to-terminal span, exactly like Resistor's own `length`) place
// the blade tip and the stationary-contact stub, leaving a real horizontal
// (along `forward`) gap between them - the "open" state:
//   blade:        p0                 -> (length*kBladeTip, height)
//   contact stub: (length*kContact,0) -> (length, 0)
// so the open-gap distance is length*(kContact - kBladeTip), directly
// readable off the two curves' own endpoint coordinates.
constexpr double kSwitchBladeTipRatio = 0.5;
constexpr double kSwitchContactStartRatio = 0.6;
std::vector<kernel::NurbsCurve> BuildSwitch(const ElecComponent& c) {
  Frame2D f = MakeFrame(c.p0, c.p1, c.normal);
  const double L = c.length, H = c.height;
  kernel::NurbsCurve blade = Poly2(c.p0, f, {{0, 0}, {L * kSwitchBladeTipRatio, H}});
  kernel::NurbsCurve stub = Poly2(c.p0, f, {{L * kSwitchContactStartRatio, 0}, {L, 0}});
  return {blade, stub};
}

// Ground/Earth: a vertical lead of length 0.3*size down from p0, then three
// horizontal "rungs" of decreasing width (size, 0.66*size, 0.33*size)
// spaced 0.2*size apart below it - the classic stepped-line earth symbol.
// Total bounding-box height is therefore exactly 0.3*size + 2*0.2*size =
// 0.7*size, a fixed proportion of `size` as required.
std::vector<kernel::NurbsCurve> BuildGround(const ElecComponent& c) {
  Frame2D f = MakeFrame(c.p0, c.p1, c.normal);
  const double S = c.size;
  const double lead = 0.3 * S, step = 0.2 * S;
  std::vector<kernel::NurbsCurve> out;
  out.push_back(Poly2(c.p0, f, {{0, 0}, {0, -lead}}));
  const double widths[3] = {S, 0.66 * S, 0.33 * S};
  for (int k = 0; k < 3; ++k) {
    const double j = -(lead + step * k);
    const double w2 = widths[k] / 2.0;
    out.push_back(Poly2(c.p0, f, {{-w2, j}, {w2, j}}));
  }
  return out;
}

// Lamp: a circle of diameter `size` plus an inscribed X whose four
// endpoints sit exactly on the circle (at +-45 degrees), so both the
// circle's radius and the X-lines' endpoint coordinates are directly
// checkable.
std::vector<kernel::NurbsCurve> BuildLamp(const ElecComponent& c) {
  Frame2D f = MakeFrame(c.p0, c.p1, c.normal);
  const double r = c.size / 2.0;
  ON_Plane pl(c.p0, ON_3dVector(f.forward.x, f.forward.y, f.forward.z), ON_3dVector(f.right.x, f.right.y, f.right.z));
  ON_ArcCurve circle_arc((ON_Circle(pl, r)));
  ON_NurbsCurve circle_nurbs;
  circle_arc.GetNurbForm(circle_nurbs);
  kernel::NurbsCurve circle;
  circle.raw() = circle_nurbs;
  const double k = r * 0.70710678118654752;  // r / sqrt(2): the 45-degree point on the circle
  kernel::NurbsCurve x1 = Poly2(c.p0, f, {{-k, -k}, {k, k}});
  kernel::NurbsCurve x2 = Poly2(c.p0, f, {{-k, k}, {k, -k}});
  return {circle, x1, x2};
}

}  // namespace

namespace {

std::vector<ObjectId> BuildGeometry(Document& doc, ElecComponent& c) {
  std::vector<kernel::NurbsCurve> curves;
  switch (c.type) {
    case ElecType::Resistor: curves = BuildResistor(c); break;
    case ElecType::Capacitor: curves = BuildCapacitor(c); break;
    case ElecType::Switch: curves = BuildSwitch(c); break;
    case ElecType::Ground: curves = BuildGround(c); break;
    case ElecType::Lamp: curves = BuildLamp(c); break;
    case ElecType::WireRun: {
      // WireRun is the one type whose two points are the endpoints
      // themselves, not a placement + direction pair; build a straight
      // line directly in world coordinates instead of Poly2's local frame.
      std::vector<ObjectId> ids;
      SceneObject o = SceneObject::MakeCurve(Polyline({c.p0, c.p1}));
      o.name = "WireRun";
      ids.push_back(doc.Add(std::move(o)));
      return ids;
    }
  }
  std::vector<ObjectId> ids;
  for (const kernel::NurbsCurve& crv : curves) {
    SceneObject o = SceneObject::MakeCurve(crv);
    o.name = ElecTypeName(c.type);
    ids.push_back(doc.Add(std::move(o)));
  }
  return ids;
}

}  // namespace

// ---------------------------------------------------------------------------
// Persistence (Document::UserText()["dino8.elec"] as a JSON array) - same
// approach as ArchComponents.cpp's LoadArch/SaveArch.
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kUserTextKey = "dino8.elec";
}

std::vector<ElecComponent> LoadElec(const Document& doc) {
  std::vector<ElecComponent> out;
  auto it = const_cast<Document&>(doc).UserText().find(kUserTextKey);
  if (it == const_cast<Document&>(doc).UserText().end() || it->second.empty()) return out;
  json::Value root;
  std::string err;
  if (!json::Parse(it->second, root, err) || !root.IsArray()) return out;
  for (size_t i = 0; i < root.Size(); ++i) {
    const json::Value& v = root[i];
    ElecComponent c;
    c.id = static_cast<int>(v["id"].number);
    ElecType t;
    if (!ParseElecType(v["type"].AsString(), t)) continue;
    c.type = t;
    c.p0 = Point3d(v["p0x"].number, v["p0y"].number, v["p0z"].number);
    c.p1 = Point3d(v["p1x"].number, v["p1y"].number, v["p1z"].number);
    c.normal = Vector3d(v["nx"].number, v["ny"].number, v["nz"].number);
    if (c.normal.Length() < 1e-9) c.normal = Vector3d(0, 0, 1);
    double len = v["length"].number;
    c.length = len > 0 ? len : 0.6;
    double h = v["height"].number;
    c.height = h != 0 ? h : 0.3;
    double g = v["gap"].number;
    c.gap = g > 0 ? g : 0.1;
    double pl = v["plate_length"].number;
    c.plate_length = pl > 0 ? pl : 0.4;
    double sz = v["size"].number;
    c.size = sz > 0 ? sz : 0.3;
    const json::Value& hr0 = v["has_ref0"];
    c.has_ref0 = hr0.type == json::Value::Type::Number && hr0.number != 0;
    const json::Value& hr1 = v["has_ref1"];
    c.has_ref1 = hr1.type == json::Value::Type::Number && hr1.number != 0;
    c.ref0 = static_cast<ObjectId>(v["ref0"].number);
    c.ref1 = static_cast<ObjectId>(v["ref1"].number);
    c.end0 = v["end0"].AsString();
    c.end1 = v["end1"].AsString();
    const json::Value& objs = v["objects"];
    for (size_t j = 0; j < objs.Size(); ++j) c.objects.push_back(static_cast<ObjectId>(objs[j].number));
    out.push_back(c);
  }
  return out;
}

void SaveElec(Document& doc, const std::vector<ElecComponent>& list) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < list.size(); ++i) {
    const ElecComponent& c = list[i];
    out << (i ? "," : "") << "{\"id\":" << c.id << ",\"type\":\"" << ElecTypeName(c.type) << "\""
        << ",\"p0x\":" << c.p0.x << ",\"p0y\":" << c.p0.y << ",\"p0z\":" << c.p0.z
        << ",\"p1x\":" << c.p1.x << ",\"p1y\":" << c.p1.y << ",\"p1z\":" << c.p1.z
        << ",\"nx\":" << c.normal.x << ",\"ny\":" << c.normal.y << ",\"nz\":" << c.normal.z
        << ",\"length\":" << c.length << ",\"height\":" << c.height
        << ",\"gap\":" << c.gap << ",\"plate_length\":" << c.plate_length << ",\"size\":" << c.size
        << ",\"has_ref0\":" << (c.has_ref0 ? 1 : 0) << ",\"has_ref1\":" << (c.has_ref1 ? 1 : 0)
        << ",\"ref0\":" << c.ref0 << ",\"ref1\":" << c.ref1
        << ",\"end0\":\"" << c.end0 << "\",\"end1\":\"" << c.end1 << "\""
        << ",\"objects\":[";
    for (size_t j = 0; j < c.objects.size(); ++j) out << (j ? "," : "") << c.objects[j];
    out << "]}";
  }
  out << "]";
  doc.UserText()[kUserTextKey] = out.str();
}

namespace {
void RemoveObjects(Document& doc, const std::vector<ObjectId>& ids) {
  for (ObjectId id : ids) doc.Remove(id);
}
}  // namespace

int AddElecComponent(Document& doc, ElecComponent c) {
  std::vector<ElecComponent> list = LoadElec(doc);
  int next_id = 1;
  for (const ElecComponent& e : list) next_id = std::max(next_id, e.id + 1);
  c.id = next_id;
  list.push_back(c);
  ElecComponent& stored = list.back();
  stored.objects = BuildGeometry(doc, stored);
  SaveElec(doc, list);
  return stored.id;
}

bool DeleteElecComponent(Document& doc, int id) {
  std::vector<ElecComponent> list = LoadElec(doc);
  auto it = std::find_if(list.begin(), list.end(), [id](const ElecComponent& c) { return c.id == id; });
  if (it == list.end()) return false;
  RemoveObjects(doc, it->objects);
  list.erase(it);
  SaveElec(doc, list);
  return true;
}

bool FindElecComponent(const Document& doc, int id, ElecComponent& out) {
  for (const ElecComponent& c : LoadElec(doc)) if (c.id == id) { out = c; return true; }
  return false;
}

bool FindElecComponentByObject(const Document& doc, ObjectId object, ElecComponent& out) {
  for (const ElecComponent& c : LoadElec(doc)) {
    if (std::find(c.objects.begin(), c.objects.end(), object) != c.objects.end()) { out = c; return true; }
  }
  return false;
}

int RebuildElecComponent(Document& doc, const ElecComponent& updated) {
  std::vector<ElecComponent> list = LoadElec(doc);
  auto it = std::find_if(list.begin(), list.end(), [&](const ElecComponent& c) { return c.id == updated.id; });
  if (it == list.end()) return -1;
  RemoveObjects(doc, it->objects);
  *it = updated;
  it->objects = BuildGeometry(doc, *it);
  SaveElec(doc, list);
  return it->id;
}

void RebuildAll(Document& doc) {
  std::vector<ElecComponent> list = LoadElec(doc);
  for (ElecComponent& c : list) RemoveObjects(doc, c.objects);
  for (ElecComponent& c : list) c.objects = BuildGeometry(doc, c);
  SaveElec(doc, list);
}

}  // namespace dino8::elec
