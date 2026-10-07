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
// Scope: eight construction commands record a doc::HistoryRecord for every
// object they build while History On is set - Extrude/ExtrudeCrv,
// ExtrudeCrvToPoint, Revolve, Loft, SubDLoft, Pipe, Sweep1 and Sweep2
// (both single-cross-section case only - see cmd_solids.cpp and
// cmd_surface.cpp, which own the actual construction math via the free
// Rebuild* functions declared in history_rebuild.h). These were chosen
// because each has a simple, unambiguous "rebuild from current source
// curve(s) + recorded parameters" definition with no hidden picked-in-3D-
// space state besides what's recorded (Revolve's axis is the one
// exception - like Rhino's own Revolve history, the axis itself does not
// move when the profile curve does). Every other construction command in
// this app (roughly 1048 of them) does NOT record history - that is an
// honest, explicit scope limit for this first real implementation, not a
// claim of full coverage, and matches the disclosed-partial-coverage shape
// ConnectSrf/CreateRegions/UpdateDimensions already use elsewhere in this
// app (AUDIT.md).
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

// Re-runs the one construction the HistoryRecord names against its
// source(s)' *current* geometry. Returns false (no-op, target untouched)
// when a source no longer resolves to a curve, or the construction itself
// fails (e.g. a degenerate result) - same "degrade gracefully, never
// crash" contract ProvenanceInfo's own comment documents for a stale
// parent_id.
bool RebuildOneHistoryObject(CommandContext& ctx, SceneObject& target, const HistoryRecord& rec) {
  std::optional<SceneObject> fresh;
  if (rec.command == "Extrude" || rec.command == "ExtrudeCrvToPoint") {
    if (rec.sources.size() != 1) return false;
    const SceneObject* src = ctx.Doc().Find(rec.sources[0]);
    if (!src || src->kind != ObjectKind::Curve) return false;
    fresh = rec.command == "Extrude" ? RebuildExtrude(ctx, *src->curve, rec) : RebuildExtrudeToPoint(ctx, *src->curve, rec);
  } else if (rec.command == "Revolve") {
    if (rec.sources.size() != 1) return false;
    const SceneObject* src = ctx.Doc().Find(rec.sources[0]);
    if (!src || src->kind != ObjectKind::Curve) return false;
    fresh = RebuildRevolve(ctx, *src->curve, rec);
  } else if (rec.command == "Loft" || rec.command == "SubDLoft") {
    std::vector<const kernel::NurbsCurve*> curves;
    for (ObjectId sid : rec.sources) {
      const SceneObject* src = ctx.Doc().Find(sid);
      if (!src || src->kind != ObjectKind::Curve) return false;
      curves.push_back(src->curve.get());
    }
    fresh = RebuildLoft(ctx, curves, rec);
  } else if (rec.command == "Pipe") {
    if (rec.sources.size() != 1) return false;
    const SceneObject* src = ctx.Doc().Find(rec.sources[0]);
    if (!src || src->kind != ObjectKind::Curve) return false;
    fresh = RebuildPipe(ctx, *src->curve, rec);
    // Keep PipeFeature (doc/Document.h, ExtractPipedCurve's own source of
    // the rail) in sync too - it is a side table keyed by this object's
    // (unchanged) id, so without this it would still point at the rail
    // curve's geometry from whenever the pipe was first built, stale the
    // same way the object's own SceneObject geometry would be without
    // ReplaceGeometry below.
    if (fresh) ctx.Doc().SetPipeFeature(target.id, *src->curve);
  } else if (rec.command == "Sweep1") {
    // Sources recorded as {rail, section} (Sweep1Command::Build's single-
    // cross-section case only - the live command never records history for
    // its multi-section blend case, since RebuildSweep1 itself only takes
    // one section).
    if (rec.sources.size() != 2) return false;
    const SceneObject* rail_src = ctx.Doc().Find(rec.sources[0]);
    const SceneObject* section_src = ctx.Doc().Find(rec.sources[1]);
    if (!rail_src || rail_src->kind != ObjectKind::Curve) return false;
    if (!section_src || section_src->kind != ObjectKind::Curve) return false;
    fresh = RebuildSweep1(ctx, *section_src->curve, *rail_src->curve, rec);
  } else if (rec.command == "Sweep2") {
    // Sources recorded as {rail1, rail2, section} (Sweep2Command::Build's
    // single-cross-section case only - same reasoning as Sweep1 above).
    if (rec.sources.size() != 3) return false;
    const SceneObject* rail1_src = ctx.Doc().Find(rec.sources[0]);
    const SceneObject* rail2_src = ctx.Doc().Find(rec.sources[1]);
    const SceneObject* section_src = ctx.Doc().Find(rec.sources[2]);
    if (!rail1_src || rail1_src->kind != ObjectKind::Curve) return false;
    if (!rail2_src || rail2_src->kind != ObjectKind::Curve) return false;
    if (!section_src || section_src->kind != ObjectKind::Curve) return false;
    fresh = RebuildSweep2(ctx, *section_src->curve, *rail1_src->curve, *rail2_src->curve, rec);
  } else if (rec.command == "Patch") {
    // Source recorded as {closed curve} (Patch's single-closed-planar-
    // curve case only - see RebuildPatch's own comment for why the
    // non-planar-boundary fallback is deliberately never recorded).
    if (rec.sources.size() != 1) return false;
    const SceneObject* src = ctx.Doc().Find(rec.sources[0]);
    if (!src || src->kind != ObjectKind::Curve) return false;
    fresh = RebuildPatch(ctx, *src->curve, rec);
  } else {
    return false;  // unknown/future command name in an old side-table entry
  }
  if (!fresh) return false;
  ReplaceGeometry(target, std::move(*fresh));
  return true;
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
    ctx.Print(std::string("History recording: ") + (on ? "on - new Extrude/ExtrudeCrvToPoint/Revolve/Loft/SubDLoft/Pipe/Sweep1/Sweep2 (single section)/Patch (single planar closed curve) results will remember their source curve(s) for UpdateHistory"
                                                         : "off - new construction results will not remember their source"));
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
            " object(s) with live construction history" + (live ? ":" : " (run Extrude/Revolve/Loft/ExtrudeCrvToPoint/SubDLoft/Pipe while History is On to create one, then edit the source curve and run UpdateHistory)."));
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
  for (const auto& [id, rec] : recs) {
    SceneObject* target = ctx.Doc().Find(id);
    if (!target) { ctx.Doc().ClearHistoryRecord(id); ++orphaned; continue; }
    if (RebuildOneHistoryObject(ctx, *target, rec)) ++rebuilt; else ++failed;
  }
  std::string msg = label + ": " + std::to_string(rebuilt) + " object(s) re-evaluated from their source curve(s)' current geometry";
  if (failed) msg += ", " + std::to_string(failed) + " skipped (source curve missing or the construction failed on its current shape)";
  if (orphaned) msg += ", " + std::to_string(orphaned) + " orphaned entr" + std::string(orphaned == 1 ? "y" : "ies") + " cleared (object deleted or an Undo passed the construction)";
  ctx.Print(msg);
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
  Reg(e, "History", Immediate([](CommandContext& ctx) { ToggleOrReport(ctx, /*report=*/true); }), CommandStatus::Implemented,
      "A real, scoped constructional-history mechanism: with no argument, reports the On/Off state and every object with live history and its source(s); On/Off toggles whether NEW results from Extrude/ExtrudeCrv, ExtrudeCrvToPoint, Revolve, Loft, SubDLoft, Pipe, Sweep1 and Sweep2 (both single cross-section only) record their source curve(s) and parameters - existing objects and every other construction command are unaffected, matching Rhino's own History On/Off gating 'new construction only'. UpdateHistory does the actual rebuild-on-edit. NOT a general dependency graph for all ~1050 commands - see UpdateHistory's own note for exactly why those eight and the honest scope limit.");
  Reg(e, "RecordHistory", Immediate([](CommandContext& ctx) { ToggleOrReport(ctx, /*report=*/false); }), CommandStatus::Implemented,
      "The same On/Off toggle as History (Rhino's own alternate name for it); use History with no argument for the live report.");
  Reg(e, "UpdateHistory", Immediate(DoUpdateHistory), CommandStatus::Implemented,
      "Re-runs Extrude/ExtrudeCrvToPoint/Revolve/Loft/SubDLoft/Pipe/Sweep1/Sweep2 for every object History recorded, against its source curve(s)' *current* geometry (moved, reshaped, or control-point-edited since), and replaces that object's geometry in place - same object id, same layer/color/name/user text, only the shape changes (Pipe's own PipeFeature rail tag is refreshed alongside it, so ExtractPipedCurve keeps working after a rebuild too). The same explicit-recompute shape as ElecRebuild (cmd_elec.cpp) and UpdateDimensions (cmd_annotate.cpp) use for their own associative rebuilds, not an automatic hook on every document edit. Revolve's axis is recorded as-picked and does not move with the curve, matching Rhino's own Revolve history. Sweep1/Sweep2 only record when built from a single cross-section curve (the kernel's exact Brep::Sweep1/Sweep2 case) - the multi-section blend case records nothing for either, matching how Sweep1 already behaved before this and how Sweep2 now matches it. Objects made before History was turned On, or by any other command, have no recorded history and are left untouched.");
  Reg(e, "HistoryUpdate", Immediate(DoUpdateHistory), CommandStatus::Implemented,
      "Rhino's own alternate name for UpdateHistory - identical rebuild, reported under whichever name was typed. Previously a dead cmd_state.cpp stub that always claimed no history was recorded; now the real mechanism.");
  Reg(e, "HistoryPurge", Immediate(DoHistoryPurge), CommandStatus::Implemented,
      "Drops the recorded construction history for the current selection (or every object in the document, if nothing is selected), so a later UpdateHistory/HistoryUpdate no longer rebuilds them. Previously a dead cmd_state.cpp stub that always claimed no history was recorded; now checks Document::HistoryRecords() for real.");
}

}  // namespace dino8::app
