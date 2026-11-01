// History / RecordHistory / UpdateHistory: a real, intentionally-scoped
// constructional-history mechanism.
//
// This generalizes two patterns already proven elsewhere in this app:
//  - The HoleFeature/PipeFeature/ProvenanceInfo side-table pattern
//    (doc/Document.h): construction metadata that rides alongside an
//    object without touching SceneObject, Save3dm or the object list.
//  - The explicit-recompute pattern ElecRebuild (src/elec/ElecComponents.h,
//    cmd_elec.cpp) and UpdateDimensions (annotate_common.h, cmd_annotate.cpp)
//    already use: record a source reference at creation time, and a later,
//    explicitly-invoked command re-evaluates the dependent geometry from
//    the source's *current* state - not an automatic hook that fires on
//    every document edit (this app has no such hook anywhere, and adding
//    one here first would be a new, unproven mechanism instead of reuse).
//
// Scope: the constructions in RegisterBuiltinRebuilders' table below (22
// record names: Extrude/ExtrudeCrv, ExtrudeSrf, ExtrudeCrvToPoint,
// ExtrudeCrvAlongCrv, ExtrudeCrvTapered, Revolve, RailRevolve, Loft, SubDLoft,
// Sweep1/Sweep2 (single cross-section), Pipe, Patch (planar), PlanarSrf,
// NetworkSrf/EdgeSrf (2, 3 or 4 curves), Offset, OffsetSrf and the four
// booleans) record a doc::HistoryRecord for every object they build while
// History On is set. Every other construction command in this app (roughly
// a thousand) does NOT record history - an explicit scope limit, not a claim
// of full coverage, and not an automatic hook: history re-runs when
// UpdateHistory is run. Revolve's axis is the one input that does not move
// with the profile, like Rhino's own Revolve history.
//
// Adding a construction is one RegisterHistoryRebuilder() line in the table
// plus a RecordHistoryIfEnabled() call in the command; the dispatch (sources
// of any object kind, dependency ordering, printed reasons, ReplaceGeometry)
// is shared.

#include "commands/cmd_common.h"
#include "commands/history_rebuild.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace dino8::app {

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Moves fresh's geometry into target in place, keeping target's ObjectId,
// name, layer, color, group, user text, material and every other
// attribute untouched - the same "replace geometry, keep identity" shape
// ElecRebuild's RebuildElecComponent (src/elec/ElecComponents.cpp) and
// RebuildCommand (cmd_edit.cpp, `*o->curve = ...`) already use, just
// generalized to whatever object kind the construction actually produces
// (an Extrude can legitimately switch between Surface and Brep across a
// rebuild, e.g. a curve that becomes planar/non-planar).
void ReplaceGeometry(SceneObject& target, SceneObject fresh) {
  target.kind = fresh.kind;
  target.point = fresh.point;
  target.curve = std::move(fresh.curve);
  target.surface = std::move(fresh.surface);
  target.brep = std::move(fresh.brep);
  target.mesh = std::move(fresh.mesh);
  target.subd = std::move(fresh.subd);
  target.point_cloud = std::move(fresh.point_cloud);
  target.InvalidateDisplay();
}

// ---------------------------------------------------------------------------
// The registry itself (declared in history_rebuild.h) and the table of every
// construction that can re-execute. Adding a command to UpdateHistory is one
// RegisterHistoryRebuilder() line below plus a RecordHistoryIfEnabled() call
// in the command - nothing in the dispatch, ordering, error reporting or
// Undo handling changes.
// ---------------------------------------------------------------------------
std::map<std::string, HistoryRebuilder>& Registry() {
  static std::map<std::string, HistoryRebuilder> r;
  return r;
}

}  // namespace

void RegisterHistoryRebuilder(const std::string& command, const std::string& description, HistoryRebuildFn fn,
                              std::vector<HistoryParam> params) {
  Registry()[command] = HistoryRebuilder{command, description, std::move(fn), std::move(params)};
}

const HistoryRebuilder* FindHistoryRebuilder(const std::string& command) {
  const auto it = Registry().find(command);
  return it == Registry().end() ? nullptr : &it->second;
}

std::vector<const HistoryRebuilder*> HistoryRebuilders() {
  std::vector<const HistoryRebuilder*> out;
  for (const auto& [name, rb] : Registry()) out.push_back(&rb);
  return out;
}

const char* HistoryKindName(ObjectKind k) {
  switch (k) {
    case ObjectKind::Point: return "a point";
    case ObjectKind::Curve: return "a curve";
    case ObjectKind::Surface: return "a surface";
    case ObjectKind::Brep: return "a polysurface";
    case ObjectKind::Mesh: return "a mesh";
    case ObjectKind::SubD: return "a SubD";
    default: return "another kind of object";
  }
}

const kernel::NurbsCurve* HistoryRequireCurve(const SceneObject* o, std::string& why) {
  if (!o) { why = "a source object is missing"; return nullptr; }
  if (o->kind != ObjectKind::Curve || !o->curve) {
    why = "source object " + std::to_string(o->id) + " is " + HistoryKindName(o->kind) + ", not a curve";
    return nullptr;
  }
  return o->curve.get();
}

namespace {

// Adapters: validate the sources' kinds and forward to the Rebuild* function
// that the live command itself builds through.
template <class F>
HistoryRebuildFn OneCurve(F f) {
  return [f](CommandContext& ctx, const HistoryInputs& in, const HistoryRecord& rec, std::string& why) -> std::optional<SceneObject> {
    if (in.size() != 1) { why = "expected 1 source object, the record has " + std::to_string(in.size()); return std::nullopt; }
    const kernel::NurbsCurve* c = HistoryRequireCurve(in[0], why);
    if (!c) return std::nullopt;
    return f(ctx, *c, rec);
  };
}
template <class F>
HistoryRebuildFn TwoCurves(F f) {
  return [f](CommandContext& ctx, const HistoryInputs& in, const HistoryRecord& rec, std::string& why) -> std::optional<SceneObject> {
    if (in.size() != 2) { why = "expected 2 source objects, the record has " + std::to_string(in.size()); return std::nullopt; }
    const kernel::NurbsCurve* a = HistoryRequireCurve(in[0], why);
    const kernel::NurbsCurve* b = a ? HistoryRequireCurve(in[1], why) : nullptr;
    if (!a || !b) return std::nullopt;
    return f(ctx, *a, *b, rec);
  };
}
template <class F>
HistoryRebuildFn ThreeCurves(F f) {
  return [f](CommandContext& ctx, const HistoryInputs& in, const HistoryRecord& rec, std::string& why) -> std::optional<SceneObject> {
    if (in.size() != 3) { why = "expected 3 source objects, the record has " + std::to_string(in.size()); return std::nullopt; }
    const kernel::NurbsCurve* a = HistoryRequireCurve(in[0], why);
    const kernel::NurbsCurve* b = a ? HistoryRequireCurve(in[1], why) : nullptr;
    const kernel::NurbsCurve* c = b ? HistoryRequireCurve(in[2], why) : nullptr;
    if (!a || !b || !c) return std::nullopt;
    return f(ctx, *a, *b, *c, rec);
  };
}

HistoryRebuildFn CurveList(std::optional<SceneObject> (*f)(CommandContext&, const std::vector<const kernel::NurbsCurve*>&, const HistoryRecord&), size_t min_n, size_t max_n) {
  return [f, min_n, max_n](CommandContext& ctx, const HistoryInputs& in, const HistoryRecord& rec, std::string& why) -> std::optional<SceneObject> {
    std::vector<const kernel::NurbsCurve*> curves;
    for (const SceneObject* o : in) { const kernel::NurbsCurve* c = HistoryRequireCurve(o, why); if (!c) return std::nullopt; curves.push_back(c); }
    if (curves.size() < min_n || curves.size() > max_n) { why = "the record has " + std::to_string(curves.size()) + " source curves, this construction takes " + std::to_string(min_n) + "-" + std::to_string(max_n); return std::nullopt; }
    return f(ctx, curves, rec);
  };
}

std::optional<SceneObject> NetworkLoopAdapter(CommandContext& ctx, const std::vector<const kernel::NurbsCurve*>& curves, const HistoryRecord& rec) {
  return RebuildNetworkSrfLoop(ctx, curves, rec, nullptr);
}

HistoryRebuildFn OneSurface(std::optional<SceneObject> (*f)(CommandContext&, const SceneObject&, const HistoryRecord&)) {
  return [f](CommandContext& ctx, const HistoryInputs& in, const HistoryRecord& rec, std::string& why) -> std::optional<SceneObject> {
    if (in.size() != 1) { why = "expected 1 source object, the record has " + std::to_string(in.size()); return std::nullopt; }
    if (in[0]->kind == ObjectKind::Curve || in[0]->kind == ObjectKind::Point) {
      why = "source object " + std::to_string(in[0]->id) + " is " + HistoryKindName(in[0]->kind) + ", not a surface or polysurface";
      return std::nullopt;
    }
    return f(ctx, *in[0], rec);
  };
}

// ---------------------------------------------------------------------------
// Editable options (EditHistory). Each recorded construction exposes the
// numeric/choice options it was built with under a user-facing name; `set`
// validates the value and rewrites every derived key of the record, so the
// ordinary Rebuild* function can re-run it unchanged.
// ---------------------------------------------------------------------------
double NumOf(const HistoryRecord& r, const char* k, double d = 0.0) {
  const auto it = r.num.find(k);
  return it == r.num.end() ? d : it->second;
}

bool ParseNumber(const std::string& s, double& v) {
  char* e = nullptr;
  v = std::strtod(s.c_str(), &e);
  return e && e != s.c_str() && *e == 0 && std::isfinite(v);
}

HistoryParam NumberParam(std::string name, std::string desc, std::string range, std::function<double(const HistoryRecord&)> get,
                         std::function<std::string(HistoryRecord&, double)> set) {
  HistoryParam p;
  p.name = std::move(name);
  p.description = std::move(desc);
  p.range = std::move(range);
  p.get = [get](const HistoryRecord& r) { return FormatNumber(get(r)); };
  p.set = [set](HistoryRecord& r, const std::string& v) -> std::string {
    double d = 0;
    if (!ParseNumber(v, d)) return "'" + v + "' is not a number";
    return set(r, d);
  };
  return p;
}

HistoryParam ChoiceParam(std::string name, std::string desc, std::vector<std::string> choices,
                         std::function<std::string(const HistoryRecord&)> get, std::function<std::string(HistoryRecord&, const std::string&)> set) {
  HistoryParam p;
  p.name = std::move(name);
  p.description = std::move(desc);
  p.choices = choices;
  p.get = std::move(get);
  p.set = [choices, set](HistoryRecord& r, const std::string& v) -> std::string {
    const std::string l = Lower(v);
    if (std::find(choices.begin(), choices.end(), l) == choices.end()) {
      std::string list;
      for (const std::string& c : choices) list += (list.empty() ? "" : ", ") + c;
      return "'" + v + "' is not one of: " + list;
    }
    return set(r, l);
  };
  return p;
}

HistoryParam YesNoParam(std::string name, std::string desc, const char* key, bool default_yes) {
  const std::string k = key;
  return ChoiceParam(std::move(name), std::move(desc), {"yes", "no"},
                     [k, default_yes](const HistoryRecord& r) { return NumOf(r, k.c_str(), default_yes ? 1.0 : 0.0) != 0.0 ? "yes" : "no"; },
                     [k](HistoryRecord& r, const std::string& l) { r.num[k] = l == "yes" ? 1.0 : 0.0; return std::string(); });
}

Vector3d VecOf(const HistoryRecord& r, const char* x, const char* y, const char* z) { return Vector3d(NumOf(r, x), NumOf(r, y), NumOf(r, z)); }

bool ExtrudeIsBoth(const HistoryRecord& r) { return NumOf(r, "shiftx") != 0.0 || NumOf(r, "shifty") != 0.0 || NumOf(r, "shiftz") != 0.0; }

// Extrude/ExtrudeSrf store the vector actually used (already doubled for
// BothSides) and the symmetric pre-shift; distance and BothSides are edited
// through this one function so the pair always stays consistent.
void SetExtrudeVector(HistoryRecord& r, const Vector3d& u, double d, bool both) {
  const Vector3d v = u * (both ? 2.0 * d : d);
  r.num["vx"] = v.x; r.num["vy"] = v.y; r.num["vz"] = v.z;
  const Vector3d s = both ? u * (-d) : Vector3d(0, 0, 0);
  r.num["shiftx"] = s.x; r.num["shifty"] = s.y; r.num["shiftz"] = s.z;
}

std::vector<HistoryParam> ExtrudeParams() {
  std::vector<HistoryParam> ps;
  ps.push_back(NumberParam("distance", "extrusion distance along the recorded direction", "non-zero; negative flips the direction",
      [](const HistoryRecord& r) { return VecOf(r, "vx", "vy", "vz").Length() / (ExtrudeIsBoth(r) ? 2.0 : 1.0); },
      [](HistoryRecord& r, double d) -> std::string {
        if (d == 0.0) return "distance must be non-zero";
        Vector3d u = VecOf(r, "vx", "vy", "vz");
        if (!u.Unitize()) return "the recorded extrusion vector is zero";
        SetExtrudeVector(r, u, d, ExtrudeIsBoth(r));
        return "";
      }));
  ps.push_back(ChoiceParam("bothsides", "extrude symmetrically to both sides of the curve", {"yes", "no"},
      [](const HistoryRecord& r) { return std::string(ExtrudeIsBoth(r) ? "yes" : "no"); },
      [](HistoryRecord& r, const std::string& l) -> std::string {
        Vector3d u = VecOf(r, "vx", "vy", "vz");
        const double d = u.Length() / (ExtrudeIsBoth(r) ? 2.0 : 1.0);
        if (!u.Unitize()) return "the recorded extrusion vector is zero";
        SetExtrudeVector(r, u, d, l == "yes");
        return "";
      }));
  ps.push_back(YesNoParam("solid", "cap the ends into a solid", "solid", true));
  return ps;
}

std::vector<HistoryParam> BooleanParams() {
  std::vector<HistoryParam> ps;
  ps.push_back(ChoiceParam("operation", "boolean operation", {"union", "difference", "intersection", "symmetricdifference"},
      [](const HistoryRecord& r) {
        const int op = static_cast<int>(NumOf(r, "op"));
        return std::string(op == 1 ? "difference" : op == 2 ? "intersection" : op == 3 ? "symmetricdifference" : "union");
      },
      [](HistoryRecord& r, const std::string& l) -> std::string {
        if (NumOf(r, "first") == 0.0 && l != "union") return "this is a single-group boolean (all operands combined): only union applies";
        r.num["op"] = l == "difference" ? 1 : l == "intersection" ? 2 : l == "symmetricdifference" ? 3 : 0;
        return "";
      }));
  ps.push_back(ChoiceParam("swap", "swap the two operand groups (A-B becomes B-A); reads back no", {"yes", "no"},
      [](const HistoryRecord&) { return std::string("no"); },
      [](HistoryRecord& r, const std::string& l) -> std::string {
        if (l != "yes") return "";
        const size_t first = static_cast<size_t>(NumOf(r, "first"));
        if (first == 0 || first >= r.sources.size()) return "swap needs a two-group boolean (BooleanDifference/BooleanIntersection/Boolean2Objects)";
        std::vector<ObjectId> s(r.sources.begin() + static_cast<long>(first), r.sources.end());
        s.insert(s.end(), r.sources.begin(), r.sources.begin() + static_cast<long>(first));
        r.num["first"] = static_cast<double>(r.sources.size() - first);
        r.sources = s;
        return "";
      }));
  return ps;
}

void SetParams(const std::string& command, std::vector<HistoryParam> params) {
  const auto it = Registry().find(command);
  if (it != Registry().end()) it->second.params = std::move(params);
}

void RegisterBuiltinParams() {
  SetParams("Extrude", ExtrudeParams());
  SetParams("ExtrudeSrf", ExtrudeParams());
  SetParams("ExtrudeCrvToPoint", {
      NumberParam("apexx", "apex point X", "any", [](const HistoryRecord& r) { return NumOf(r, "ax"); }, [](HistoryRecord& r, double d) { r.num["ax"] = d; return std::string(); }),
      NumberParam("apexy", "apex point Y", "any", [](const HistoryRecord& r) { return NumOf(r, "ay"); }, [](HistoryRecord& r, double d) { r.num["ay"] = d; return std::string(); }),
      NumberParam("apexz", "apex point Z", "any", [](const HistoryRecord& r) { return NumOf(r, "az"); }, [](HistoryRecord& r, double d) { r.num["az"] = d; return std::string(); }),
      YesNoParam("solid", "cap the base into a solid cone", "solid", true)});
  SetParams("ExtrudeCrvTapered", {
      NumberParam("distance", "extrusion distance along the recorded direction", "non-zero; negative flips the direction",
                  [](const HistoryRecord& r) { return VecOf(r, "dx", "dy", "dz").Length(); },
                  [](HistoryRecord& r, double d) -> std::string {
                    if (d == 0.0) return "distance must be non-zero";
                    Vector3d u = VecOf(r, "dx", "dy", "dz");
                    if (!u.Unitize()) return "the recorded extrusion vector is zero";
                    r.num["dx"] = u.x * d; r.num["dy"] = u.y * d; r.num["dz"] = u.z * d;
                    return "";
                  }),
      NumberParam("draft", "draft (taper) angle in degrees", "-90 < angle < 90 exclusive",
                  [](const HistoryRecord& r) { return NumOf(r, "draft") * 180.0 / ON_PI; },
                  [](HistoryRecord& r, double d) -> std::string {
                    if (!(std::fabs(d) < 90.0)) return "draft must be between -90 and 90 degrees (exclusive)";
                    r.num["draft"] = d * ON_PI / 180.0;
                    return "";
                  }),
      YesNoParam("solid", "cap the ends of a closed planar profile", "solid", true)});
  SetParams("Revolve", {
      NumberParam("angle", "sweep angle in degrees", "0 < angle <= 360",
                  [](const HistoryRecord& r) { return NumOf(r, "angle", 2.0 * ON_PI) * 180.0 / ON_PI; },
                  [](HistoryRecord& r, double d) -> std::string {
                    if (!(d > 0.0 && d <= 360.0)) return "angle must be greater than 0 and at most 360 degrees";
                    r.num["angle"] = d * ON_PI / 180.0;
                    return "";
                  }),
      NumberParam("startangle", "start angle in degrees", "any",
                  [](const HistoryRecord& r) { return NumOf(r, "start_angle") * 180.0 / ON_PI; },
                  [](HistoryRecord& r, double d) { r.num["start_angle"] = d * ON_PI / 180.0; return std::string(); })});
  auto style = ChoiceParam("style", "loft style (Normal interpolates smoothly, Straight is a ruled loft)", {"normal", "straight"},
                           [](const HistoryRecord& r) { return std::string(r.straight ? "straight" : "normal"); },
                           [](HistoryRecord& r, const std::string& l) { r.straight = l == "straight"; return std::string(); });
  SetParams("Loft", {style, YesNoParam("cap", "cap the ends of a loft through closed planar curves", "cap", true)});
  SetParams("SubDLoft", {style});
  SetParams("Pipe", {
      NumberParam("radius", "pipe radius", "> 0", [](const HistoryRecord& r) { return NumOf(r, "radius"); },
                  [](HistoryRecord& r, double d) -> std::string {
                    if (!(d > 0.0)) return "radius must be positive";
                    r.num["radius"] = d;
                    return "";
                  }),
      YesNoParam("cap", "cap the pipe ends", "cap", true)});
  SetParams("Offset", {
      NumberParam("distance", "signed offset distance", "non-zero; the sign picks the side",
                  [](const HistoryRecord& r) { return NumOf(r, "d"); },
                  [](HistoryRecord& r, double d) -> std::string {
                    if (d == 0.0) return "distance must be non-zero";
                    r.num["d"] = d;
                    return "";
                  }),
      ChoiceParam("corner", "corner style", {"sharp", "round", "chamfer", "smooth"},
                  [](const HistoryRecord& r) {
                    const int c = static_cast<int>(NumOf(r, "corner"));
                    return std::string(c == 1 ? "round" : c == 2 ? "chamfer" : c == 3 ? "smooth" : "sharp");
                  },
                  [](HistoryRecord& r, const std::string& l) { r.num["corner"] = l == "round" ? 1 : l == "chamfer" ? 2 : l == "smooth" ? 3 : 0; return std::string(); })});
  SetParams("OffsetSrf", {
      NumberParam("distance", "signed offset distance", "non-zero; the sign picks the side",
                  [](const HistoryRecord& r) { return NumOf(r, "d"); },
                  [](HistoryRecord& r, double d) -> std::string {
                    if (d == 0.0) return "distance must be non-zero";
                    r.num["d"] = d;
                    return "";
                  }),
      YesNoParam("solid", "build a solid between the surface and its offset", "solid", false)});
  for (const char* name : {"BooleanUnion", "BooleanDifference", "BooleanIntersection", "Boolean2Objects"}) SetParams(name, BooleanParams());
}

std::string ParamsText(const HistoryRecord& rec) {
  const HistoryRebuilder* rb = FindHistoryRebuilder(rec.command);
  std::string s;
  if (rb) for (const HistoryParam& p : rb->params) {
    if (p.name == "swap") continue;  // an action, always reads back "no"
    s += (s.empty() ? "" : ", ") + p.name + "=" + p.get(rec);
  }
  return s;
}

void RegisterBuiltinRebuilders() {
  RegisterHistoryRebuilder("Extrude", "Extrude/ExtrudeCrv of a curve", OneCurve(RebuildExtrude));
  RegisterHistoryRebuilder("ExtrudeCrvToPoint", "ExtrudeCrvToPoint", OneCurve(RebuildExtrudeToPoint));
  RegisterHistoryRebuilder("ExtrudeSrf", "ExtrudeSrf of a surface or polysurface", OneSurface(RebuildExtrudeSrf));
  RegisterHistoryRebuilder("ExtrudeCrvAlongCrv", "ExtrudeCrvAlongCrv {profile, path}", TwoCurves(RebuildExtrudeAlongCrv));
  RegisterHistoryRebuilder("ExtrudeCrvTapered", "ExtrudeCrvTapered", OneCurve(RebuildExtrudeTapered));
  RegisterHistoryRebuilder("Revolve", "Revolve (Output=Surface)", OneCurve(RebuildRevolve));
  RegisterHistoryRebuilder("RailRevolve", "RailRevolve {profile, rail} (exact kernel branch)", TwoCurves(RebuildRailRevolve));
  RegisterHistoryRebuilder("Loft", "Loft", CurveList(RebuildLoft, 2, 1000000));
  RegisterHistoryRebuilder("SubDLoft", "SubDLoft", CurveList(RebuildLoft, 2, 1000000));
  RegisterHistoryRebuilder("Pipe", "Pipe (single radius)", OneCurve(RebuildPipe));
  // Sweep1 sources are {rail, section}; the kernel call takes (section, rail).
  RegisterHistoryRebuilder("Sweep1", "Sweep1 {rail, section} (single cross-section)",
      TwoCurves([](CommandContext& ctx, const kernel::NurbsCurve& rail, const kernel::NurbsCurve& section, const HistoryRecord& rec) {
        return RebuildSweep1(ctx, section, rail, rec);
      }));
  // Sweep2 sources are {rail1, rail2, section}.
  RegisterHistoryRebuilder("Sweep2", "Sweep2 {rail1, rail2, section} (single cross-section)",
      ThreeCurves([](CommandContext& ctx, const kernel::NurbsCurve& r1, const kernel::NurbsCurve& r2, const kernel::NurbsCurve& section, const HistoryRecord& rec) {
        return RebuildSweep2(ctx, section, r1, r2, rec);
      }));
  RegisterHistoryRebuilder("Patch", "Patch (single closed planar curve)", OneCurve(RebuildPatch));
  RegisterHistoryRebuilder("PlanarSrf", "PlanarSrf", OneCurve(RebuildPlanarSrf));
  RegisterHistoryRebuilder("NetworkSrf2", "NetworkSrf/EdgeSrf, 2 curves (ruled)", TwoCurves(RebuildNetworkSrf2));
  RegisterHistoryRebuilder("NetworkSrf", "NetworkSrf/EdgeSrf, 3 or 4 curves (Coons)", CurveList(NetworkLoopAdapter, 3, 4));
  RegisterHistoryRebuilder("Offset", "Offset of a curve", OneCurve(RebuildOffsetCurve));
  RegisterHistoryRebuilder("OffsetSrf", "OffsetSrf of a surface or polysurface", OneSurface(RebuildOffsetSrf));
  for (const char* name : {"BooleanUnion", "BooleanDifference", "BooleanIntersection", "Boolean2Objects"})
    RegisterHistoryRebuilder(name, std::string(name) + " of closed solid B-reps (exact engines)", RebuildBoolean);
}

// Side effect that is not geometry: Pipe's PipeFeature rail tag (Document
// side table, ExtractPipedCurve's own source) is refreshed from the rail's
// current geometry too, otherwise it would keep pointing at the rail as it
// was when the pipe was first built.
void AfterRebuild(CommandContext& ctx, const SceneObject& target, const HistoryRecord& rec, const HistoryInputs& in) {
  if (rec.command == "Pipe" && in.size() == 1 && in[0]->curve) ctx.Doc().SetPipeFeature(target.id, *in[0]->curve);
}

// ---------------------------------------------------------------------------
// Rebuild one record in two steps (so EditHistory can compute the new geometry
// BEFORE it opens an undo step, and a refusal leaves the document untouched):
// PrepareRebuild resolves the sources and runs the registered function,
// CommitRebuild moves the result into the target object.
// ---------------------------------------------------------------------------
struct PreparedRebuild {
  std::optional<SceneObject> fresh;
  HistoryInputs inputs;
};

bool PrepareRebuild(CommandContext& ctx, const HistoryRecord& rec, PreparedRebuild& out, std::string& why) {
  const HistoryRebuilder* rb = FindHistoryRebuilder(rec.command);
  if (!rb) { why = "no rebuild is registered for '" + rec.command + "'"; return false; }  // unknown/future command name in an old side-table entry
  for (ObjectId sid : rec.sources) {
    const SceneObject* src = ctx.Doc().Find(sid);
    if (!src) { why = "source object " + std::to_string(sid) + " no longer exists"; return false; }
    out.inputs.push_back(src);
  }
  try {
    out.fresh = rb->fn(ctx, out.inputs, rec, why);
  } catch (const std::exception& ex) {
    why = std::string("the construction threw: ") + ex.what();
    return false;
  }
  if (!out.fresh) {
    if (why.empty()) why = "the construction failed on its sources' current shape";
    return false;
  }
  return true;
}

void CommitRebuild(CommandContext& ctx, SceneObject& target, const HistoryRecord& rec, PreparedRebuild& p) {
  AfterRebuild(ctx, target, rec, p.inputs);
  ReplaceGeometry(target, std::move(*p.fresh));
}

// Re-runs the one construction the HistoryRecord names against its
// source(s)' *current* geometry. Returns false (no-op, target untouched)
// with `why` set when a source no longer resolves, is no longer the kind of
// object the construction needs, or the construction itself refuses the
// current shape - same "degrade gracefully, never crash" contract
// ProvenanceInfo's own comment documents for a stale parent_id.
bool RebuildOneHistoryObject(CommandContext& ctx, SceneObject& target, const HistoryRecord& rec, std::string& why) {
  PreparedRebuild p;
  if (!PrepareRebuild(ctx, rec, p, why)) return false;
  CommitRebuild(ctx, target, rec, p);
  return true;
}

// Every recorded object, ordered so each one comes after the recorded
// objects it was built from (a depth-first walk over the sources that are
// themselves records), ties broken by ascending ObjectId. Object ids only
// ever grow, so a dependent normally has a higher id than its sources - but
// nothing here relies on that, and a (corrupt) cycle is broken rather than
// looped on.
std::vector<ObjectId> DependencyOrder(const std::map<ObjectId, HistoryRecord>& recs) {
  std::vector<ObjectId> order;
  std::map<ObjectId, int> state;  // 1 = in progress, 2 = done
  std::function<void(ObjectId)> visit = [&](ObjectId id) {
    if (state[id] != 0) return;
    state[id] = 1;
    for (ObjectId sid : recs.at(id).sources) if (recs.count(sid)) visit(sid);
    state[id] = 2;
    order.push_back(id);
  };
  for (const auto& [id, rec] : recs) visit(id);
  return order;
}

struct Downstream {
  std::vector<ObjectId> rebuilt;
  std::vector<std::pair<ObjectId, std::string>> kept;  // id, reason
};

// Rebuilds every record that depends, directly or through a chain, on any id in
// `changed`, in dependency order. A dependent whose own source could not be
// rebuilt is skipped with that reason. `join_pending` adds the dependents to
// the pending undo step first (they are modified in place AFTER the edit that
// started this).
Downstream RebuildDownstream(CommandContext& ctx, const std::set<ObjectId>& changed, bool join_pending) {
  Downstream res;
  const std::map<ObjectId, HistoryRecord> recs = ctx.Doc().HistoryRecords();
  std::set<ObjectId> affected = changed;
  std::vector<ObjectId> targets;
  for (ObjectId id : DependencyOrder(recs)) {
    for (ObjectId sid : recs.at(id).sources) {
      if (affected.count(sid)) { affected.insert(id); targets.push_back(id); break; }
    }
  }
  if (targets.empty()) return res;
  if (join_pending) ctx.Doc().JoinPendingChange(targets);
  std::map<ObjectId, std::string> failure;
  for (ObjectId id : targets) {
    const HistoryRecord& rec = recs.at(id);
    SceneObject* target = ctx.Doc().Find(id);
    if (!target) { res.kept.push_back({id, "the object no longer exists"}); failure[id] = "the object no longer exists"; continue; }
    std::string why;
    for (ObjectId sid : rec.sources) {
      const auto bad = failure.find(sid);
      if (bad != failure.end()) { why = "it depends on object " + std::to_string(sid) + ", which was not rebuilt (" + bad->second + ")"; break; }
    }
    if (why.empty() && RebuildOneHistoryObject(ctx, *target, rec, why)) { res.rebuilt.push_back(id); continue; }
    failure[id] = why;
    res.kept.push_back({id, why});
  }
  return res;
}

std::string IdList(const std::vector<ObjectId>& ids) {
  std::string s;
  for (size_t i = 0; i < ids.size(); ++i) s += (i ? "," : "") + std::to_string(ids[i]);
  return s;
}

// ---------------------------------------------------------------------------
// Automatic propagation. State per document: the fingerprint (OpenNURBS
// DataCRC of the geometry) of every object that is a SOURCE of some record, as
// of the last time we looked, and the document revision we looked at. A command
// that edits a source (Move/Rotate/Scale/Mirror/point edits/Set*/...) changes
// its fingerprint; the next hook finds it and rebuilds the dependents.
// ---------------------------------------------------------------------------
struct AutoState {
  std::map<ObjectId, std::uint64_t> fp;
  std::uint64_t revision = ~0ull;
};
std::map<const Document*, AutoState>& AutoStates() {
  static std::map<const Document*, AutoState> s;
  return s;
}
bool g_auto_busy = false;
int g_auto_rebuilt = 0;
int g_auto_kept = 0;

std::uint64_t Fingerprint(const Document& doc, const SceneObject& o) {
  std::uint64_t h = static_cast<std::uint64_t>(o.kind) + 1;
  auto mix = [&h](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
  switch (o.kind) {
    case ObjectKind::Curve: if (o.curve) mix(o.curve->raw().DataCRC(0)); break;
    case ObjectKind::Surface: if (o.surface) mix(o.surface->raw().DataCRC(0)); break;
    case ObjectKind::Brep: if (o.brep) mix(o.brep->raw().DataCRC(0)); break;
    case ObjectKind::Mesh: if (o.mesh) mix(o.mesh->raw().DataCRC(0)); break;
    case ObjectKind::Point: {
      for (double c : {o.point.x, o.point.y, o.point.z}) { std::uint64_t b = 0; std::memcpy(&b, &c, sizeof(b)); mix(b); }
      break;
    }
    default: {  // SubD / point cloud: counts + bounding box
      kernel::BoundingBox bb;
      if (doc.BoundingBoxOf({o.id}, bb)) {
        for (double c : {bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z}) { std::uint64_t b = 0; std::memcpy(&b, &c, sizeof(b)); mix(b); }
      }
      if (o.subd) { mix(static_cast<std::uint64_t>(o.subd->FaceCount())); mix(static_cast<std::uint64_t>(o.subd->VertexCount())); }
      break;
    }
  }
  return h;
}

std::map<ObjectId, std::uint64_t> SourceFingerprints(const Document& doc) {
  std::map<ObjectId, std::uint64_t> now;
  for (const auto& [id, rec] : doc.HistoryRecords())
    for (ObjectId sid : rec.sources)
      if (!now.count(sid)) if (const SceneObject* o = doc.Find(sid)) now[sid] = Fingerprint(doc, *o);
  return now;
}

// Records "this is the state everything is consistent with" - called after an
// explicit UpdateHistory/EditHistory (so the hook does not redo their work) and
// after Undo/Redo.
void ResyncAuto(Document& doc) {
  AutoState& st = AutoStates()[&doc];
  if (doc.HistoryRecords().empty()) st.fp.clear(); else st.fp = SourceFingerprints(doc);
  st.revision = doc.Revision();
}

bool IsUndoRedoName(const std::string& n) { return n == "Undo" || n == "Redo" || n == "UndoSelected"; }

std::string SourcesText(const HistoryRecord& rec) {
  std::string s;
  for (size_t i = 0; i < rec.sources.size(); ++i) { if (i) s += ","; s += std::to_string(rec.sources[i]); }
  return s.empty() ? "(no source)" : s;
}

// One line of measured geometry for the before/after report: bounding box plus
// length (curve), volume (closed solid) or area (everything else).
std::string MeasureObject(CommandContext& ctx, ObjectId id) {
  const SceneObject* o = ctx.Doc().Find(id);
  if (!o) return "(object missing)";
  std::string s;
  kernel::BoundingBox bb;
  if (ctx.Doc().BoundingBoxOf({id}, bb)) s = "bbox " + FormatPoint(bb.min) + " to " + FormatPoint(bb.max);
  if (o->kind == ObjectKind::Curve && o->curve) {
    s += ", length " + FormatNumber(o->curve->Length());
  } else if (o->kind != ObjectKind::Point) {
    bool closed = false;
    const double v = ObjectVolumeOf(*o, closed);
    s += closed ? ", volume " + FormatNumber(v) : ", area " + FormatNumber(ObjectAreaOf(*o));
  }
  return s;
}

std::string ParamHelp(const HistoryParam& p, const HistoryRecord& rec) {
  std::string s = "  " + p.name + " = " + p.get(rec) + "   - " + p.description;
  if (!p.choices.empty()) {
    s += " [";
    for (size_t i = 0; i < p.choices.size(); ++i) s += (i ? "|" : "") + p.choices[i];
    s += "]";
  } else if (!p.range.empty()) {
    s += " (" + p.range + ")";
  }
  return s;
}

std::string KindName(const SceneObject& o) {
  std::string n = HistoryKindName(o.kind);
  return n.rfind("a ", 0) == 0 ? n.substr(2) : n;
}

// EditHistory <id ...> [key=value ...]: re-runs a recorded construction with
// NEW options. Without key=value it lists the object's editable parameters.
// All values are validated first and the new geometry is computed before the
// undo step opens, so a refused edit changes nothing; an accepted one is ONE
// undo step covering the record, the object and every dependent.
void DoEditHistory(CommandContext& ctx) {
  std::vector<std::string> toks;
  while (std::optional<std::string> t = ctx.Engine().TakePendingInput()) toks.push_back(*t);
  std::vector<ObjectId> ids;
  std::vector<std::pair<std::string, std::string>> kvs;
  for (size_t i = 0; i < toks.size(); ++i) {
    const std::string& t = toks[i];
    const size_t eq = t.find('=');
    if (eq != std::string::npos && eq > 0) { kvs.push_back({Lower(t.substr(0, eq)), t.substr(eq + 1)}); continue; }
    const bool numeric = !t.empty() && std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); });
    if (numeric) { ids.push_back(static_cast<ObjectId>(std::stoul(t))); continue; }
    if (i + 1 < toks.size()) { kvs.push_back({Lower(t), toks[i + 1]}); ++i; continue; }
    ctx.Warn("EditHistory: '" + t + "' needs a value (key=value)");
    return;
  }
  if (ids.empty()) for (ObjectId id : ctx.Doc().SelectedIds()) ids.push_back(id);
  if (ids.empty()) {
    ctx.Warn("EditHistory: select an object that has construction history, or give its id: EditHistory <id> key=value ... (EditHistory <id> alone lists the editable parameters; ListHistory shows which objects have history)");
    return;
  }
  for (ObjectId id : ids) {
    const HistoryRecord* cur = ctx.Doc().FindHistoryRecord(id);
    const SceneObject* target = ctx.Doc().Find(id);
    if (!target) { ctx.Warn("EditHistory: object " + std::to_string(id) + " does not exist"); continue; }
    if (!cur) { ctx.Warn("EditHistory: object " + std::to_string(id) + " has no recorded construction history (it was made with History Off, or by a command that does not record)"); continue; }
    const HistoryRebuilder* rb = FindHistoryRebuilder(cur->command);
    if (!rb) { ctx.Warn("EditHistory: no rebuild is registered for '" + cur->command + "'"); continue; }
    if (kvs.empty()) {
      std::string msg = "EditHistory: object " + std::to_string(id) + " (" + cur->command + ") <- " + SourcesText(*cur) + (rb->params.empty() ? " has no editable options - its inputs are its source objects (edit those; history follows)" : " - editable parameters (EditHistory " + std::to_string(id) + " name=value ...):");
      for (const HistoryParam& p : rb->params) msg += "\n" + ParamHelp(p, *cur);
      ctx.Print(msg);
      continue;
    }
    HistoryRecord edited = *cur;
    std::string changes, bad;
    for (const auto& [key, value] : kvs) {
      const HistoryParam* param = nullptr;
      for (const HistoryParam& p : rb->params) if (p.name == key) param = &p;
      if (!param) {
        std::string names;
        for (const HistoryParam& p : rb->params) names += (names.empty() ? "" : ", ") + p.name;
        bad = "'" + key + "' is not an editable parameter of " + cur->command + (names.empty() ? " (it has none)" : " (valid: " + names + ")");
        break;
      }
      const std::string before = param->get(edited);
      const std::string err = param->set(edited, value);
      if (!err.empty()) { bad = key + ": " + err; break; }
      changes += (changes.empty() ? "" : ", ") + (key == "swap" ? std::string("operand groups swapped") : key + " " + before + " -> " + param->get(edited));
    }
    if (!bad.empty()) { ctx.Warn("EditHistory: object " + std::to_string(id) + " unchanged - " + bad); continue; }
    PreparedRebuild prep;
    std::string why;
    if (!PrepareRebuild(ctx, edited, prep, why)) { ctx.Warn("EditHistory: object " + std::to_string(id) + " unchanged - " + cur->command + " cannot be re-run with those options: " + why); continue; }
    const std::string measured_before = MeasureObject(ctx, id);
    ctx.Doc().BeginChange("EditHistory");
    SceneObject* t = ctx.Doc().Find(id);
    ctx.Doc().SetHistoryRecord(id, edited);
    CommitRebuild(ctx, *t, edited, prep);
    const Downstream down = RebuildDownstream(ctx, {id}, /*join_pending=*/false);
    std::string msg = "EditHistory: object " + std::to_string(id) + " (" + cur->command + "): " + changes + ".\n  before: " + measured_before + "\n  after:  " + MeasureObject(ctx, id);
    if (!down.rebuilt.empty()) msg += "\n  dependents re-evaluated: " + IdList(down.rebuilt);
    for (const auto& [kid, reason] : down.kept) msg += "\n  object " + std::to_string(kid) + " kept as it was: " + reason;
    ctx.Print(msg);
    ResyncAuto(ctx.Doc());
  }
}

// ListHistory / ShowHistory: every recorded object (or the selection) with its
// construction, source objects and the current value of each editable option.
void DoListHistory(CommandContext& ctx) {
  const bool show = ctx.Engine().ActiveName() == "ShowHistory";
  std::vector<ObjectId> ids = ctx.Doc().SelectedIds();
  std::string msg;
  int n = 0;
  auto describe = [&](ObjectId id, const HistoryRecord& rec) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) return;
    ++n;
    msg += (msg.empty() ? "" : "\n") + std::string("  object ") + std::to_string(id) + " (" + KindName(*o) + "): " + rec.command + " <- " + SourcesText(rec);
    const std::string ps = ParamsText(rec);
    if (!ps.empty()) msg += "  [" + ps + "]";
    if (show) {
      for (ObjectId sid : rec.sources) {
        const SceneObject* s = ctx.Doc().Find(sid);
        msg += "\n      input " + std::to_string(sid) + (s ? " (" + KindName(*s) + (s->visible ? "" : ", hidden") + ")" : " (deleted)");
      }
    }
  };
  if (ids.empty()) { for (const auto& [id, rec] : ctx.Doc().HistoryRecords()) describe(id, rec); }
  else { for (ObjectId id : ids) if (const HistoryRecord* r = ctx.Doc().FindHistoryRecord(id)) describe(id, *r); }
  ctx.Print(ctx.Engine().ActiveName() + ": " + std::to_string(n) + " object(s) with construction history" + (n ? ":\n" + msg : std::string(" (turn History On, build something, then use EditHistory <id> to change its options)")));
}

// Shared by History and RecordHistory below: with no argument, prints the
// current on/off state (and, for History, the live report); with On/Off
// (Yes/No/1/0 also accepted), sets it.
void ToggleOrReport(CommandContext& ctx, bool report) {
  bool& on = ctx.App().State().history_recording;
  if (std::optional<std::string> tok = ctx.Engine().TakePendingInput()) {
    const std::string v = Lower(*tok);
    if (v == "on" || v == "yes" || v == "y" || v == "1") on = true;
    else if (v == "off" || v == "no" || v == "n" || v == "0") on = false;
    else { ctx.Warn("History: expected On or Off"); return; }
    std::string names;
    for (const HistoryRebuilder* rb : HistoryRebuilders()) names += (names.empty() ? "" : "/") + rb->command;
    ctx.Print(std::string("History recording: ") + (on ? "on - new results of " + std::to_string(HistoryRebuilders().size()) + " construction commands (" + names + ") will remember their source object(s) and options; editing a source now updates its dependents automatically (UpdateHistory re-runs everything on demand, EditHistory changes a construction's options)"
                                                         : "off - new construction results will not remember their source, and edits no longer propagate automatically (objects already recorded stay linked; UpdateHistory still rebuilds them on demand)"));
    return;
  }
  if (!report) { ctx.Print(std::string("History recording: ") + (on ? "on" : "off") + " (type On or Off to change)"); return; }
  const std::map<ObjectId, HistoryRecord>& recs = ctx.Doc().HistoryRecords();
  int live = 0;
  std::string lines;
  for (const auto& [id, rec] : recs) {
    if (!ctx.Doc().Find(id)) continue;  // orphaned entry (undo past the construction); UpdateHistory prunes these
    ++live;
    const std::string ps = ParamsText(rec);
    lines += "  object " + std::to_string(id) + ": " + rec.command + " <- " + SourcesText(rec) + (ps.empty() ? "" : "  [" + ps + "]") + "\n";
  }
  ctx.Print(std::string("History recording: ") + (on ? "on" : "off") + ". " + std::to_string(live) +
            " object(s) with live construction history" + (live ? ":" : " (turn History On, build with Extrude/Revolve/Loft/Sweep/Pipe/NetworkSrf/Offset/BooleanUnion/... then edit the source and run UpdateHistory)."));
  if (!lines.empty()) ctx.Print(lines.substr(0, lines.size() - 1));
  ctx.Print("Automatic updates this session: " + std::to_string(HistoryAutoRebuilt()) + " dependent rebuild(s), " + std::to_string(HistoryAutoKept()) + " kept as they were");
}

// Shared by UpdateHistory and HistoryUpdate below (Rhino ships both names
// for the same rebuild-on-edit command - see cmd_state.cpp's removed dead
// stub, which used to own the "HistoryUpdate" catalog entry and always
// claimed no history was recorded even when UpdateHistory's own real
// mechanism, right next to it, had live records). ctx.Engine().ActiveName()
// reports back whichever of the two the user actually typed, so the
// printed message always names the command that was really run.
void DoUpdateHistory(CommandContext& ctx) {
  const std::string label = ctx.Engine().ActiveName();
  const std::map<ObjectId, HistoryRecord> recs = ctx.Doc().HistoryRecords();  // copy: ClearHistoryRecord below mutates the live table
  ctx.Doc().BeginChange(label);
  int rebuilt = 0, failed = 0, orphaned = 0;
  // Dependency order, not map order: a boolean of two extrusions of two curves
  // must see the extrusions' NEW geometry, so each object is rebuilt only after
  // the recorded objects it was built from. An object whose source could not be
  // rebuilt is skipped (its geometry stays exactly as it was) instead of being
  // rebuilt from a half-updated chain.
  std::map<ObjectId, std::string> failure;  // id -> reason, for the dependents check and the report
  std::vector<std::string> reasons;
  for (ObjectId id : DependencyOrder(recs)) {
    const HistoryRecord& rec = recs.at(id);
    SceneObject* target = ctx.Doc().Find(id);
    if (!target) { ctx.Doc().ClearHistoryRecord(id); ++orphaned; continue; }
    std::string why;
    for (ObjectId sid : rec.sources) {
      const auto bad = failure.find(sid);
      if (bad != failure.end()) { why = "it depends on object " + std::to_string(sid) + ", which was not rebuilt (" + bad->second + ")"; break; }
    }
    if (why.empty() && RebuildOneHistoryObject(ctx, *target, rec, why)) { ++rebuilt; continue; }
    ++failed;
    failure[id] = why;
    reasons.push_back("object " + std::to_string(id) + " (" + rec.command + ") kept as it was: " + why);
  }
  std::string msg = label + ": " + std::to_string(rebuilt) + " object(s) re-evaluated from their source curve(s)' current geometry";
  if (failed) msg += ", " + std::to_string(failed) + " skipped (source curve missing or the construction failed on its current shape)";
  if (orphaned) msg += ", " + std::to_string(orphaned) + " orphaned entr" + std::string(orphaned == 1 ? "y" : "ies") + " cleared (object deleted or an Undo passed the construction)";
  ctx.Print(msg);
  for (const std::string& r : reasons) ctx.Warn(label + ": " + r);
  ResyncAuto(ctx.Doc());  // the automatic propagation must not redo what this just did
}

// HistoryPurge: drops the recorded HistoryRecord for objects so a later
// UpdateHistory/HistoryUpdate no longer touches them, matching Rhino's own
// HistoryPurge (remove history from the selection, or the whole document
// when nothing is selected). The side table this clears is, like every
// other RebuildOneHistoryObject input, deliberately not part of Undo (see
// Document.cpp), so this does not itself open a BeginChange - no scene
// geometry is touched, only which objects UpdateHistory will still rebuild.
void DoHistoryPurge(CommandContext& ctx) {
  const std::string label = ctx.Engine().ActiveName();
  const std::vector<ObjectId> sel = ctx.Doc().SelectedIds();
  std::vector<ObjectId> targets;
  if (!sel.empty()) {
    for (ObjectId id : sel) if (ctx.Doc().FindHistoryRecord(id)) targets.push_back(id);
  } else {
    for (const auto& [id, rec] : ctx.Doc().HistoryRecords()) targets.push_back(id);
  }
  if (!targets.empty()) ctx.Doc().BeginChange(label);  // records are part of the undo step now
  for (ObjectId id : targets) ctx.Doc().ClearHistoryRecord(id);
  std::string msg = label + ": " + std::to_string(targets.size()) + " object(s) had their recorded construction history removed";
  msg += targets.empty() ? " (nothing to purge)" : ("; " + label + " will no longer rebuild " + (targets.size() == 1 ? "it" : "them") + " from a source curve");
  ctx.Print(msg);
}

}  // namespace

int HistoryAutoRebuilt() { return g_auto_rebuilt; }
int HistoryAutoKept() { return g_auto_kept; }

void HistoryAutoUpdate(CommandContext& ctx, const std::string& cause) {
  if (g_auto_busy) return;
  Document& doc = ctx.Doc();
  AutoState& st = AutoStates()[&doc];
  if (doc.HistoryRecords().empty()) { st.fp.clear(); st.revision = doc.Revision(); return; }  // nothing depends on anything: no work at all
  // Document::Revision() cannot be used to skip the comparison: Move/Rotate/
  // Scale/point edits modify objects in place through Find()/FindMany(), which
  // do not bump it. Fingerprinting the (few) source objects is the cheap,
  // reliable test; the per-frame hook for edits that bypass commands (gizmo and
  // control-point drags, Ctrl+Z) is throttled to every 6th frame instead.
  if (cause == "frame") {
    static int frame_counter = 0;
    if (++frame_counter % 6 != 0) return;
  }
  const std::map<ObjectId, std::uint64_t> now = SourceFingerprints(doc);
  std::set<ObjectId> changed;
  const bool follow = ctx.App().State().history_recording && !IsUndoRedoName(cause);
  if (follow) {
    for (const auto& [id, fp] : st.fp) {
      const auto it = now.find(id);
      if (it == now.end() || it->second != fp) changed.insert(id);  // edited, or deleted
    }
    // Objects that just became sources (a record was created this command) are
    // simply taken as the new baseline - they cannot have been "edited" since.
  }
  if (changed.empty()) { st.fp = now; st.revision = doc.Revision(); return; }
  // A source that differs with NO undo step pending was not edited just now: an
  // Undo/Redo that bypassed the command line (the History panel's click-to-undo)
  // restored it together with its dependents. Take the new baseline; rebuilding
  // here would modify objects outside any undo step.
  if (cause == "frame" && !doc.JoinPendingChange({})) { st.fp = now; st.revision = doc.Revision(); return; }
  g_auto_busy = true;
  Downstream down;
  try {
    down = RebuildDownstream(ctx, changed, /*join_pending=*/true);
  } catch (...) {
    g_auto_busy = false;
    throw;
  }
  g_auto_busy = false;
  g_auto_rebuilt += static_cast<int>(down.rebuilt.size());
  g_auto_kept += static_cast<int>(down.kept.size());
  ResyncAuto(doc);
  if (down.rebuilt.empty() && down.kept.empty()) return;  // the edited object has no dependents
  std::vector<ObjectId> src(changed.begin(), changed.end());
  std::string msg = "History: source object(s) " + IdList(src) + " changed - " + std::to_string(down.rebuilt.size()) + " dependent(s) re-evaluated" +
                    (down.rebuilt.empty() ? "" : " (" + IdList(down.rebuilt) + ")");
  if (!down.kept.empty()) {
    msg += ", " + std::to_string(down.kept.size()) + " kept as they were: ";
    for (size_t i = 0; i < down.kept.size(); ++i) msg += (i ? "; " : "") + std::string("object ") + std::to_string(down.kept[i].first) + " - " + down.kept[i].second;
    ctx.Warn(msg);
  } else {
    ctx.Print(msg);
  }
}

void RegisterHistoryCommands(CommandEngine& e) {
  RegisterBuiltinRebuilders();
  RegisterBuiltinParams();
  Reg(e, "History", Immediate([](CommandContext& ctx) { ToggleOrReport(ctx, /*report=*/true); }), CommandStatus::Implemented,
      "Construction history: with no argument, reports the On/Off state and every object with live history and its source(s); On/Off toggles whether NEW results of the registered constructions (Extrude/ExtrudeCrv/ExtrudeSrf/ExtrudeCrvToPoint/ExtrudeCrvAlongCrv/ExtrudeCrvTapered, Revolve, RailRevolve, Loft, SubDLoft, Sweep1/Sweep2 single section, Pipe, Patch, PlanarSrf, NetworkSrf/EdgeSrf, Offset, OffsetSrf, and the exact-B-rep BooleanUnion/Difference/Intersection/Boolean2Objects) record their source object(s) and options - existing objects and every other construction command are unaffected, matching Rhino's own History On/Off gating 'new construction only'. While History is On the inputs a boolean consumes are hidden (kept as the boolean's recorded sources) instead of deleted, as Rhino does. UpdateHistory does the actual rebuild-on-edit, in dependency order through chains of any depth.");
  Reg(e, "RecordHistory", Immediate([](CommandContext& ctx) { ToggleOrReport(ctx, /*report=*/false); }), CommandStatus::Implemented,
      "The same On/Off toggle as History (Rhino's own alternate name for it); use History with no argument for the live report.");
  Reg(e, "UpdateHistory", Immediate(DoUpdateHistory), CommandStatus::Implemented,
      "Re-runs every recorded construction (see History for the list) against its source object(s)' *current* geometry (moved, reshaped, or control-point-edited since) and replaces that object's geometry in place - same object id, same layer/color/name/user text, only the shape changes. One registry-based dispatch (history_rebuild.h): sources may be curves, surfaces, polysurfaces or the results of other recorded constructions, which are rebuilt first (curve -> Extrude -> BooleanUnion updates in order). A source that was deleted, changed kind, or that the construction now refuses leaves that object exactly as it was, prints the reason, and skips everything built from it. The explicit-recompute shape of ElecRebuild (cmd_elec.cpp) and UpdateDimensions (cmd_annotate.cpp), not an automatic hook on every document edit. Revolve's axis is recorded as-picked and does not move with the curve, matching Rhino. Sweep1/Sweep2 only record from a single cross-section; Revolve Output=SubD, FilletEdge/ChamferEdge, Project/Pull, Shell, mesh/SubD booleans and every other command record nothing. Objects made before History was turned On have no recorded history and are left untouched.");
  Reg(e, "EditHistory", Immediate(DoEditHistory), CommandStatus::Implemented,
      "EditHistory <id> [key=value ...]: re-runs the recorded construction of an object with NEW options (Extrude distance/bothsides/solid, ExtrudeCrvTapered distance/draft, Revolve angle/startangle, Loft style/cap, Pipe radius/cap, Offset and OffsetSrf distance, boolean operation/swap, ...). With no key=value it lists the object's editable parameters, their current values and valid ranges. Values are validated and the new geometry computed before anything changes; an accepted edit prints the measured before/after (bounding box and length/area/volume), re-evaluates every dependent and is one undo step (the record is part of it).");
  Reg(e, "ListHistory", Immediate(DoListHistory), CommandStatus::Implemented,
      "Lists every object with recorded construction history (or the selection): the command, its source object ids and the current value of each editable option.");
  Reg(e, "ShowHistory", Immediate(DoListHistory), CommandStatus::Implemented,
      "Like ListHistory but also names each input object (kind, hidden or deleted) of the selected objects.");
  Reg(e, "HistoryUpdate", Immediate(DoUpdateHistory), CommandStatus::Implemented,
      "Rhino's own alternate name for UpdateHistory - identical rebuild, reported under whichever name was typed. Previously a dead cmd_state.cpp stub that always claimed no history was recorded; now the real mechanism.");
  Reg(e, "HistoryPurge", Immediate(DoHistoryPurge), CommandStatus::Implemented,
      "Drops the recorded construction history for the current selection (or every object in the document, if nothing is selected), so a later UpdateHistory/HistoryUpdate no longer rebuilds them. Previously a dead cmd_state.cpp stub that always claimed no history was recorded; now checks Document::HistoryRecords() for real.");
}

}  // namespace dino8::app
