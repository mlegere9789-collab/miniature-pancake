// DwgCompare / XrefCompare / CompareClear: two-file geometric diff overlaid
// in the current viewport, without switching away from the current
// document - see compare/DwgCompare.h for the fingerprint/pairing
// heuristic and its disclosed limits.
#include "commands/annotate_common.h"
#include "commands/cmd_common.h"
#include "compare/DwgCompare.h"

namespace dino8::app {

namespace {

std::string StatsLine(const CompareStats& s) {
  return std::to_string(s.added) + " added, " + std::to_string(s.removed) + " removed, " +
         std::to_string(s.modified) + " modified, " + std::to_string(s.unchanged) + " unchanged";
}

void DwgCompare(CommandContext& ctx) {
  const std::string path = OptionOr(TakeOptionTokens(ctx), "path");
  if (path.empty()) { ctx.Warn("DwgCompare Path=<file.dwg|.dxf|.3dm|...>"); return; }
  std::string error;
  CompareStats stats;
  if (!RunDwgCompare(ctx.Doc(), path, error, &stats)) { ctx.Warn("DwgCompare: " + error); return; }
  ctx.Print("DwgCompare: " + StatsLine(stats) + " (comparing the current document against " + path + ")");
}

void XrefCompare(CommandContext& ctx) {
  const std::string alias = OptionOr(TakeOptionTokens(ctx), "alias");
  if (alias.empty()) { ctx.Warn("XrefCompare Alias=<attached reference model's path or alias>"); return; }
  std::string error;
  CompareStats stats;
  if (!RunXrefCompare(ctx.Doc(), alias, error, &stats)) { ctx.Warn("XrefCompare: " + error); return; }
  ctx.Print("XrefCompare: " + StatsLine(stats) + " (comparing attached reference model '" + alias + "' against its source file on disk)");
}

void CompareClear(CommandContext& ctx) {
  const int n = ClearDwgCompare(ctx.Doc());
  ctx.Print("CompareClear: " + std::to_string(n) + " object(s) restored/removed");
}

}  // namespace

void RegisterCompareCommands(CommandEngine& e) {
  Reg(e, "DwgCompare", Immediate(DwgCompare), CommandStatus::Implemented,
      "Diffs the current document against another file (DWG/DXF/3dm/...): green = added, orange = modified, red dashed = removed (ghosted from the old file). Path=<file>. Heuristic matching, not id-exact - see compare/DwgCompare.h.");
  Reg(e, "XrefCompare", Immediate(XrefCompare), CommandStatus::Implemented,
      "Like DwgCompare, scoped to one Worksession-attached reference model (xref): re-reads its source file and diffs it against the copy already in the document. Alias=<attached model's alias or path>.");
  Reg(e, "CompareClear", Immediate(CompareClear), CommandStatus::Implemented,
      "Clears a DwgCompare/XrefCompare overlay: removes the removed-object ghosts and restores added/modified objects' normal by-layer colour.");
}

}  // namespace dino8::app
