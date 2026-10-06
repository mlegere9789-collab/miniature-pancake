// The construction-history mechanism behind History/UpdateHistory
// (cmd_history.cpp): a small, explicitly-scoped set of construction
// commands record a doc::HistoryRecord (doc/Document.h) for every object
// they build, while AppState::history_recording (History On) is set.
// UpdateHistory later walks every recorded object, re-fetches its source
// curve(s)' *current* geometry, and calls the matching Rebuild* function
// below to produce fresh geometry with the exact same construction
// parameters - the same "record source + params, replace geometry in
// place on demand" shape as ElecRebuild (src/elec/ElecComponents.h) and
// UpdateDimensions (src/commands/annotate_common.h), generalized here.
//
// Each Rebuild* function is the single source of truth for its command's
// geometry construction: the live command in cmd_solids.cpp calls it to
// build the object in the first place (then records history if enabled),
// and cmd_history.cpp's UpdateHistory calls the *same* function again
// later against the source's current geometry - so "rebuild" can never
// drift from what the command itself actually does.
#pragma once

#include <optional>
#include <vector>

#include "commands/cmd_common.h"
#include "doc/Document.h"

namespace dino8::app {

// Records a HistoryRecord for `new_id` when AppState::history_recording is
// on; a no-op otherwise (existing objects made while History is Off never
// get one, matching Rhino's own History On/Off gating "new construction
// only"). `num` holds the construction's numeric parameters (see each
// Rebuild* function below for exactly which keys it reads).
void RecordHistoryIfEnabled(CommandContext& ctx, ObjectId new_id, const std::string& command,
                             std::vector<ObjectId> sources, std::map<std::string, double> num,
                             bool straight = false);

// Extrude/ExtrudeCrv (Kind::Curve in ExtrudeCommand, cmd_solids.cpp).
// Reads num: vx,vy,vz (the extrusion vector actually used, already scaled
// by distance and BothSides), shiftx,shifty,shiftz (BothSides' symmetric
// pre-shift), solid (1/0, Solid=Yes/No). Returns nullopt if the curve
// can't be extruded (degenerate, ON_SumSurface failure, etc.).
std::optional<SceneObject> RebuildExtrude(CommandContext& ctx, const kernel::NurbsCurve& curve,
                                           const HistoryRecord& rec);

// ExtrudeCrvToPoint. Reads num: ax,ay,az (the fixed apex point).
std::optional<SceneObject> RebuildExtrudeToPoint(CommandContext& ctx, const kernel::NurbsCurve& curve,
                                                  const HistoryRecord& rec);

// Revolve. Reads num: ax,ay,az / bx,by,bz (the two axis points; the axis
// itself, picked independently of the curve, does not move on rebuild).
std::optional<SceneObject> RebuildRevolve(CommandContext& ctx, const kernel::NurbsCurve& curve,
                                           const HistoryRecord& rec);

// Loft/SubDLoft. `rec.straight` selects Style=Straight; `rec.command`
// distinguishes Loft (NURBS/mesh) from SubDLoft (always SubD output).
std::optional<SceneObject> RebuildLoft(CommandContext& ctx, const std::vector<const kernel::NurbsCurve*>& curves,
                                        const HistoryRecord& rec);

// Pipe (PipeCommand, cmd_surface.cpp). Reads num: radius (> 0), cap (1/0,
// Cap=Yes/No). `wrap` (closed rail, and so mesh-vs-surface) is not a
// recorded parameter - it is read fresh from the curve passed in, same as
// the live command derives it from c.IsClosed() rather than recording it.
std::optional<SceneObject> RebuildPipe(CommandContext& ctx, const kernel::NurbsCurve& curve, const HistoryRecord& rec);

// Sweep1 (Sweep1Command, cmd_surface.cpp), single-cross-section case only -
// the kernel's own exact Brep::Sweep1 (RMF transport + skin, real caps when
// the section is closed and planar). Reads no num parameters: stations and
// cap are fixed the same way the live command's single-section call is.
// Returns nullopt (no history recorded, live command falls back to its own
// approximate multi-section construction) when the kernel itself refuses
// the inputs (e.g. a non-planar closed section, or a degenerate rail/
// section) - never throws.
std::optional<SceneObject> RebuildSweep1(CommandContext& ctx, const kernel::NurbsCurve& section,
                                          const kernel::NurbsCurve& rail, const HistoryRecord& rec);

// Sweep2 (Sweep2Command, cmd_surface.cpp), single-cross-section case only -
// the kernel's own exact Brep::Sweep2 (two-rail frame transport + skin).
// Reads no num parameters: stations and cap are fixed the same way the
// live command's single-section call is. Returns nullopt (no history
// recorded, live command falls back to its own approximate scaled-frame
// construction) when the kernel itself refuses the inputs (e.g. a
// degenerate rail/section, or the rails touching) - never throws.
std::optional<SceneObject> RebuildSweep2(CommandContext& ctx, const kernel::NurbsCurve& section,
                                          const kernel::NurbsCurve& rail1, const kernel::NurbsCurve& rail2,
                                          const HistoryRecord& rec);

// Patch (the free Patch function, cmd_surface.cpp), single-closed-planar-
// curve case only - ON_BrepTrimmedPlane against the curve's own fitted
// plane. Reads no num parameters: this sub-case has none of its own.
// Returns nullopt (no history recorded, object stays stale) if the curve
// is no longer closed and planar after being edited - deliberately never
// falls back to the live command's own non-planar-projection construction,
// which would change the object's own shape in a way the recorded source
// list (just this one curve) cannot honestly explain.
std::optional<SceneObject> RebuildPatch(CommandContext& ctx, const kernel::NurbsCurve& curve, const HistoryRecord& rec);

}  // namespace dino8::app
