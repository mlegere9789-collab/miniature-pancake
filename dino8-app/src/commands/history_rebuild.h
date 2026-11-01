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

#include <functional>
#include <map>
#include <optional>
#include <string>
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

// ExtrudeCrvToPoint. Reads num: ax,ay,az (the fixed apex point), solid
// (1/0, Solid=Yes/No). A closed, planar, non-self-intersecting profile
// with solid=1 builds a genuine capped B-rep cone via
// kernel::Brep::ExtrudeToPoint; anything else (including a profile
// ExtrudeToPoint itself refuses to cap) falls back to the old
// centroid-collapsed ruled surface.
std::optional<SceneObject> RebuildExtrudeToPoint(CommandContext& ctx, const kernel::NurbsCurve& curve,
                                                  const HistoryRecord& rec);

// Revolve. Reads num: ax,ay,az / bx,by,bz (the two axis points; the axis
// itself, picked independently of the curve, does not move on rebuild),
// angle/start_angle (radians; a non-default combination routes through
// kernel::Brep::Revolve instead of the pre-existing full-revolve-only
// construction - see RebuildRevolve's own comment, cmd_solids.cpp).
std::optional<SceneObject> RebuildRevolve(CommandContext& ctx, const kernel::NurbsCurve& curve,
                                           const HistoryRecord& rec);

// Loft/SubDLoft. `rec.straight` selects Style=Straight; `rec.command`
// distinguishes Loft from SubDLoft (always the SubD/mesh output kind, built
// from a control-point grid of the sections). Loft calls the kernel's exact
// interpolating Brep::Loft after reversing/re-seaming the curves to match
// (cmd_solids.cpp): reads num: cap (1/0, Cap=Yes/No; absent means Yes) - a
// closed planar loft with Cap=Yes is a polysurface solid, otherwise a Surface.
// Returns nullopt, with a warning printed, when the curves mix open and closed
// ones; a kernel refusal keeps the older control-point fit (reason printed).
std::optional<SceneObject> RebuildLoft(CommandContext& ctx, const std::vector<const kernel::NurbsCurve*>& curves,
                                        const HistoryRecord& rec);

// Pipe (PipeCommand, cmd_surface.cpp). Reads num: radius (> 0), cap (1/0,
// Cap=Yes/No). `wrap` (closed rail, and so mesh-vs-surface) is not a
// recorded parameter - it is read fresh from the curve passed in, same as
// the live command derives it from c.IsClosed() rather than recording it.
std::optional<SceneObject> RebuildPipe(CommandContext& ctx, const kernel::NurbsCurve& curve, const HistoryRecord& rec);

// Sweep1 (Sweep1Command, cmd_surface.cpp), single-cross-section case only -
// the kernel's own exact Brep::Sweep1 (RMF transport + skin), built uncapped.
// Reads no num parameters: stations and cap are fixed the same way the live
// command's single-section call is. The section is anchored where it sits
// along the rail (a section at the rail start is the kernel call unchanged;
// anywhere else the rail is split at the section, each side swept from it and
// the halves joined; a closed rail is re-seamed there) and the result is
// measured against the section, so the surface covers the rail and passes
// through the section. Prints one line saying where the section was found and
// the measured deviation. Returns nullopt (no history recorded, live command
// falls back to its own approximate section-anchored construction, reason
// printed) when the kernel refuses the inputs or the measured result misses
// the section - never throws.
std::optional<SceneObject> RebuildSweep1(CommandContext& ctx, const kernel::NurbsCurve& section,
                                          const kernel::NurbsCurve& rail, const HistoryRecord& rec);

// Sweep2 (Sweep2Command, cmd_surface.cpp), single-cross-section case only -
// the kernel's own exact Brep::Sweep2 (two-rail frame transport + skin).
// Reads no num parameters: stations and cap are fixed the same way the
// live command's single-section call is. Section placement is handled as in
// RebuildSweep1 (anchored on each rail; both rails split at the anchors and
// the sides joined; a closed rail pair re-seamed). Returns nullopt (no history
// recorded, live command falls back to its own approximate scaled-frame
// construction, reason printed) when the kernel itself refuses the inputs
// (e.g. a degenerate rail/section, or the rails touching), the section sits
// at the start of one rail but not the other, or the measured result misses
// the section - never throws.
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

// NetworkSrf (the free NetworkSrf function, cmd_surface.cpp), 2-curve case
// only - ON_NurbsSurface::CreateRuledSurface between the two boundary
// curves. Reads no num parameters: which curve needs reversing to run
// head-to-tail with the other is re-derived fresh from both curves' own
// current geometry on every call (live or rebuilt), never itself
// recorded - the same "derive fresh, don't freeze" shape RebuildPipe
// already uses for a rail's closed/open `wrap`. Returns nullopt only if
// OpenNURBS' own CreateRuledSurface refuses the (degenerate) inputs.
std::optional<SceneObject> RebuildNetworkSrf2(CommandContext& ctx, const kernel::NurbsCurve& a,
                                               const kernel::NurbsCurve& b, const HistoryRecord& rec);

// ---------------------------------------------------------------------------
// The rebuild registry: ONE dispatch table for every construction that can
// re-execute through UpdateHistory/HistoryUpdate (cmd_history.cpp).
//
// A construction command makes itself history-capable in exactly two steps:
//   1. cmd_history.cpp's table calls RegisterHistoryRebuilder() once with the
//      HistoryRecord::command name it records under and a rebuild function
//      that maps (the source objects' CURRENT state, the recorded numeric
//      options) to a fresh SceneObject (or a printed reason why not) - the
//      adapter only validates the source kinds and calls the Rebuild*
//      function declared in this header;
//   2. at construction time it calls RecordHistoryIfEnabled() with the
//      source ObjectIds (in the order the rebuild function expects) and the
//      options, and builds its own result through the same rebuild function
//      so live construction and re-execution can never drift apart.
// UpdateHistory then needs no per-command code: it resolves each record's
// sources to live objects (any kind - a curve, a surface, a polysurface, or
// the result of another recorded construction), orders the records so every
// object is rebuilt after the objects it was built from (a chain of any
// depth), calls the registered function, and replaces the geometry in place.
// ---------------------------------------------------------------------------
using HistoryInputs = std::vector<const SceneObject*>;
// `inputs` has exactly rec.sources.size() entries, all non-null, in the
// recorded order. Return nullopt and set `why` to refuse (e.g. an input is no
// longer the kind of object the construction needs, or the kernel refuses its
// current shape); the target is then left exactly as it was.
using HistoryRebuildFn = std::function<std::optional<SceneObject>(CommandContext& ctx, const HistoryInputs& inputs,
                                                                   const HistoryRecord& rec, std::string& why)>;

// One editable option of a recorded construction (EditHistory, cmd_history.cpp).
// `name` is the user-facing key ("distance"), matched case-insensitively.
// `choices` non-empty makes it a choice option (values matched
// case-insensitively), otherwise it is a number. `get` renders the current
// value from the record; `set` validates `value`, applies it to the record
// (updating every derived num key) and returns "" or the reason it is invalid.
struct HistoryParam {
  std::string name;
  std::string description;
  std::string range;                 // shown for numbers, e.g. "non-zero (negative flips the side)"
  std::vector<std::string> choices;
  std::function<std::string(const HistoryRecord&)> get;
  std::function<std::string(HistoryRecord&, const std::string&)> set;
};

struct HistoryRebuilder {
  std::string command;      // HistoryRecord::command
  std::string description;  // what it rebuilds, shown by History
  HistoryRebuildFn fn;
  std::vector<HistoryParam> params;  // editable options (empty: the inputs are the source objects only)
};

// Idempotent per command name (the last registration wins), so every
// CommandEngine's Register*Commands call can repeat it safely.
void RegisterHistoryRebuilder(const std::string& command, const std::string& description, HistoryRebuildFn fn,
                              std::vector<HistoryParam> params = {});
const HistoryRebuilder* FindHistoryRebuilder(const std::string& command);
// Every registered rebuilder, sorted by command name.
std::vector<const HistoryRebuilder*> HistoryRebuilders();

// Human name of an object kind for refusal messages ("a polysurface").
const char* HistoryKindName(ObjectKind k);
// The curve behind `o`, or nullptr with `why` set ("source object 3 is a
// polysurface, not a curve").
const kernel::NurbsCurve* HistoryRequireCurve(const SceneObject* o, std::string& why);

// Boolean (cmd_boolean.cpp). Reads num: op (0 Union, 1 Difference, 2
// Intersection, 3 SymmetricDifference), first (how many of the leading
// sources form the first group; 0 means a single-group Union). Operands must
// all be closed solid B-reps; the exact planar engine is tried first, then the
// general engine - the same order the live commands use. Never falls back to a
// mesh result (that would silently change the object's kind).
std::optional<SceneObject> RebuildBoolean(CommandContext& ctx, const HistoryInputs& inputs, const HistoryRecord& rec, std::string& why);

// ExtrudeSrf (cmd_solids.cpp): one surface or polysurface in, extruded along
// the recorded vector. Reads num: vx,vy,vz, shiftx,shifty,shiftz, solid.
std::optional<SceneObject> RebuildExtrudeSrf(CommandContext& ctx, const SceneObject& src, const HistoryRecord& rec);

// ExtrudeCrvAlongCrv (cmd_surface.cpp): exact translational sum surface of
// the profile along the path. Reads no num parameters.
std::optional<SceneObject> RebuildExtrudeAlongCrv(CommandContext& ctx, const kernel::NurbsCurve& profile,
                                                   const kernel::NurbsCurve& path, const HistoryRecord& rec);

// ExtrudeCrvTapered (cmd_surface.cpp): kernel Brep::ExtrudeTapered. Reads num:
// dx,dy,dz (the extrusion vector), draft (radians), solid (1/0). Whether the
// ends are capped is derived fresh from the profile (closed and planar).
std::optional<SceneObject> RebuildExtrudeTapered(CommandContext& ctx, const kernel::NurbsCurve& profile, const HistoryRecord& rec);

// NetworkSrf / EdgeSrf with 3 or 4 boundary curves (cmd_surface.cpp): the
// kernel's exact Coons patch, or its triangular variant, or the fitted bilinear
// fallback when those refuse - the same chain the live command runs. Reads no
// num parameters; the loop order and orientation are re-derived from the
// curves' current geometry. `note` (optional) receives the one-line
// description the live command prints.
std::optional<SceneObject> RebuildNetworkSrfLoop(CommandContext& ctx, const std::vector<const kernel::NurbsCurve*>& curves,
                                                  const HistoryRecord& rec, std::string* note);

// RailRevolve (cmd_srfedit.cpp), exact kernel branch only: Brep::RailRevolve.
// Reads num: ax,ay,az (axis start), dx,dy,dz (unit axis direction).
std::optional<SceneObject> RebuildRailRevolve(CommandContext& ctx, const kernel::NurbsCurve& profile,
                                               const kernel::NurbsCurve& rail, const HistoryRecord& rec);

// OffsetSrf (cmd_surface.cpp): the same per-object construction the live
// command runs (OffsetRefit for a surface, OffsetSolidConvexPlanar for a
// convex planar solid, otherwise the mesh offset). Reads num: d (signed
// distance), solid (1/0).
std::optional<SceneObject> RebuildOffsetSrf(CommandContext& ctx, const SceneObject& src, const HistoryRecord& rec);

// Offset (cmd_edit.cpp), curves: NurbsCurve::OffsetInPlane in the curve's own
// plane, else in the CPlane that was active. Reads num: d (signed),
// corner (0 Sharp, 1 Round, 2 Chamfer, 3 Smooth), ox,oy,oz / xx,xy,xz /
// yx,yy,yz (that CPlane's origin and x/y axes).
std::optional<SceneObject> RebuildOffsetCurve(CommandContext& ctx, const kernel::NurbsCurve& curve, const HistoryRecord& rec);

// PlanarSrf (cmd_solids.cpp): trimmed plane bounded by one closed planar,
// non-self-intersecting curve. Reads no num parameters.
std::optional<SceneObject> RebuildPlanarSrf(CommandContext& ctx, const kernel::NurbsCurve& curve, const HistoryRecord& rec);


// Automatic propagation (cmd_history.cpp): called by CommandEngine after every
// finished command (`cause` is its name) and by Application::Frame (cause
// "frame") for edits that never went through a command. While History is On
// and the document has any recorded construction, it fingerprints the SOURCE
// objects of the records, and when one differs from last time rebuilds every
// dependent in dependency order inside the pending undo step of the edit (see
// Document::JoinPendingChange), printing one summary line. Re-entrancy
// guarded; a no-op when the document has no records. Undo/Redo only resync
// the fingerprints: the dependents travel with the edit in the same undo step.
void HistoryAutoUpdate(CommandContext& ctx, const std::string& cause);
// Number of dependent rebuilds the automatic propagation has done / refused in
// this session (shown by History).
int HistoryAutoRebuilt();
int HistoryAutoKept();

}  // namespace dino8::app
