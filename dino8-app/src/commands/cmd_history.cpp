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

void RegisterHistoryRebuilder(const std::string& command, const std::string& description, HistoryRebuildFn fn) {
  Registry()[command] = HistoryRebuilder{command, description, std::move(fn)};
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

// Re-runs the one construction the HistoryRecord names against its
// source(s)' *current* geometry. Returns false (no-op, target untouched)
// with `why` set when a source no longer resolves, is no longer the kind of
// object the construction needs, or the construction itself refuses the
// current shape - same "degrade gracefully, never crash" contract
// ProvenanceInfo's own comment documents for a stale parent_id.
bool RebuildOneHistoryObject(CommandContext& ctx, SceneObject& target, const HistoryRecord& rec, std::string& why) {
  const HistoryRebuilder* rb = FindHistoryRebuilder(rec.command);
  if (!rb) { why = "no rebuild is registered for '" + rec.command + "'"; return false; }  // unknown/future command name in an old side-table entry
  HistoryInputs inputs;
  for (ObjectId sid : rec.sources) {
    const SceneObject* src = ctx.Doc().Find(sid);
    if (!src) { why = "source object " + std::to_string(sid) + " no longer exists"; return false; }
    inputs.push_back(src);
  }
  std::optional<SceneObject> fresh;
  try {
    fresh = rb->fn(ctx, inputs, rec, why);
  } catch (const std::exception& ex) {
    why = std::string("the construction threw: ") + ex.what();
    return false;
  }
  if (!fresh) {
    if (why.empty()) why = "the construction failed on its sources' current shape";
    return false;
  }
  AfterRebuild(ctx, target, rec, inputs);
  ReplaceGeometry(target, std::move(*fresh));
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

std::string SourcesText(const HistoryRecord& rec) {
  std::string s;
  for (size_t i = 0; i < rec.sources.size(); ++i) { if (i) s += ","; s += std::to_string(rec.sources[i]); }
  return s.empty() ? "(no source)" : s;
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
    ctx.Print(std::string("History recording: ") + (on ? "on - new results of " + std::to_string(HistoryRebuilders().size()) + " construction commands (" + names + ") will remember their source object(s) and options for UpdateHistory"
                                                         : "off - new construction results will not remember their source (objects already recorded stay linked)"));
    return;
  }
  if (!report) { ctx.Print(std::string("History recording: ") + (on ? "on" : "off") + " (type On or Off to change)"); return; }
  const std::map<ObjectId, HistoryRecord>& recs = ctx.Doc().HistoryRecords();
  int live = 0;
  std::string lines;
  for (const auto& [id, rec] : recs) {
    if (!ctx.Doc().Find(id)) continue;  // orphaned entry (undo past the construction); UpdateHistory prunes these
    ++live;
    lines += "  object " + std::to_string(id) + ": " + rec.command + " <- " + SourcesText(rec) + "\n";
  }
  ctx.Print(std::string("History recording: ") + (on ? "on" : "off") + ". " + std::to_string(live) +
            " object(s) with live construction history" + (live ? ":" : " (turn History On, build with Extrude/Revolve/Loft/Sweep/Pipe/NetworkSrf/Offset/BooleanUnion/... then edit the source and run UpdateHistory)."));
  if (!lines.empty()) ctx.Print(lines.substr(0, lines.size() - 1));
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
  for (ObjectId id : targets) ctx.Doc().ClearHistoryRecord(id);
  std::string msg = label + ": " + std::to_string(targets.size()) + " object(s) had their recorded construction history removed";
  msg += targets.empty() ? " (nothing to purge)" : ("; " + label + " will no longer rebuild " + (targets.size() == 1 ? "it" : "them") + " from a source curve");
  ctx.Print(msg);
}

}  // namespace

void RegisterHistoryCommands(CommandEngine& e) {
  RegisterBuiltinRebuilders();
  Reg(e, "History", Immediate([](CommandContext& ctx) { ToggleOrReport(ctx, /*report=*/true); }), CommandStatus::Implemented,
      "Construction history: with no argument, reports the On/Off state and every object with live history and its source(s); On/Off toggles whether NEW results of the registered constructions (Extrude/ExtrudeCrv/ExtrudeSrf/ExtrudeCrvToPoint/ExtrudeCrvAlongCrv/ExtrudeCrvTapered, Revolve, RailRevolve, Loft, SubDLoft, Sweep1/Sweep2 single section, Pipe, Patch, PlanarSrf, NetworkSrf/EdgeSrf, Offset, OffsetSrf, and the exact-B-rep BooleanUnion/Difference/Intersection/Boolean2Objects) record their source object(s) and options - existing objects and every other construction command are unaffected, matching Rhino's own History On/Off gating 'new construction only'. While History is On the inputs a boolean consumes are hidden (kept as the boolean's recorded sources) instead of deleted, as Rhino does. UpdateHistory does the actual rebuild-on-edit, in dependency order through chains of any depth.");
  Reg(e, "RecordHistory", Immediate([](CommandContext& ctx) { ToggleOrReport(ctx, /*report=*/false); }), CommandStatus::Implemented,
      "The same On/Off toggle as History (Rhino's own alternate name for it); use History with no argument for the live report.");
  Reg(e, "UpdateHistory", Immediate(DoUpdateHistory), CommandStatus::Implemented,
      "Re-runs every recorded construction (see History for the list) against its source object(s)' *current* geometry (moved, reshaped, or control-point-edited since) and replaces that object's geometry in place - same object id, same layer/color/name/user text, only the shape changes. One registry-based dispatch (history_rebuild.h): sources may be curves, surfaces, polysurfaces or the results of other recorded constructions, which are rebuilt first (curve -> Extrude -> BooleanUnion updates in order). A source that was deleted, changed kind, or that the construction now refuses leaves that object exactly as it was, prints the reason, and skips everything built from it. The explicit-recompute shape of ElecRebuild (cmd_elec.cpp) and UpdateDimensions (cmd_annotate.cpp), not an automatic hook on every document edit. Revolve's axis is recorded as-picked and does not move with the curve, matching Rhino. Sweep1/Sweep2 only record from a single cross-section; Revolve Output=SubD, FilletEdge/ChamferEdge, Project/Pull, Shell, mesh/SubD booleans and every other command record nothing. Objects made before History was turned On have no recorded history and are left untouched.");
  Reg(e, "HistoryUpdate", Immediate(DoUpdateHistory), CommandStatus::Implemented,
      "Rhino's own alternate name for UpdateHistory - identical rebuild, reported under whichever name was typed. Previously a dead cmd_state.cpp stub that always claimed no history was recorded; now the real mechanism.");
  Reg(e, "HistoryPurge", Immediate(DoHistoryPurge), CommandStatus::Implemented,
      "Drops the recorded construction history for the current selection (or every object in the document, if nothing is selected), so a later UpdateHistory/HistoryUpdate no longer rebuilds them. Previously a dead cmd_state.cpp stub that always claimed no history was recorded; now checks Document::HistoryRecords() for real.");
}

}  // namespace dino8::app
