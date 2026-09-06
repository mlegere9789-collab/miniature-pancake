// IGES/STEP option and inspection commands. The actual readers and writers
// live in io/FileIgesStep.{h,cpp} and are wired into Import/Export/Open/Save
// through Application.cpp like every other exchange format; these commands
// cover the handful of Rhino options and inspectors that don't change what
// gets written or read, only how a user configures or inspects the process.
#include "commands/cmd_common.h"
#include "io/FileIgesStep.h"

namespace dino8::app {

void RegisterExchange2Commands(CommandEngine& e) {
  Reg(e, "IgesImportOptions", Immediate([](CommandContext& ctx) {
        ctx.Print("IGES import always reads every entity this importer supports (100-144, 308/408, 314, 402, 406); there is no reduced-entity mode to configure.");
      }), CommandStatus::Partial, "Reports the (fixed) set of entities read; Rhino's dialog of import filters isn't reproduced.");
  Reg(e, "IGESStudy", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        app.ShowFileDialog("IGES Study", {".igs", ".iges"}, false, [&app](const std::string& path) {
          Document tmp; std::string summary;
          if (ImportIges(tmp, path, summary)) app.Notify(path + ": " + summary);
          else app.Notify(summary);
        });
      }), CommandStatus::Partial, "Reads the file into a scratch document and reports the summary; no standalone entity-tree viewer.");
  Reg(e, "ReadEveryIGESEntity", Immediate([](CommandContext& ctx) {
        ctx.Print("Dino 8's IGES reader always reads every supported entity type - ReadEveryIGESEntity has nothing extra to enable.");
      }), CommandStatus::Partial, "No partial-read mode exists to turn off.");
  Reg(e, "SetIgesLayerLevelMap", Immediate([](CommandContext& ctx) {
        ctx.Print("IGES levels map 1:1 to Dino 8 layers by number on import, and layers map to the DE level field by index on export; there is no separate remapping table.");
      }), CommandStatus::Partial, "The level<->layer mapping is fixed (by index), not user-editable.");
  Reg(e, "STEPTree", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        app.ShowFileDialog("STEP Tree", {".stp", ".step"}, false, [&app](const std::string& path) {
          Document tmp; std::string summary;
          if (ImportStep(tmp, path, summary)) app.Notify(path + ": " + summary);
          else app.Notify(summary);
        });
      }), CommandStatus::Partial, "Reads the file into a scratch document and reports the summary; no standalone product-structure tree viewer.");
  Reg(e, "StepUnitsAndTolerance", Immediate([](CommandContext& ctx) {
        ctx.Print("STEP export writes an SI_UNIT of millimetre and an UNCERTAINTY_MEASURE_WITH_UNIT of the document's absolute tolerance (" + FormatNumber(ctx.Settings().absolute_tolerance) + " " + ctx.Settings().unit_system + "); STEP import assumes the file is in millimetres.");
      }), CommandStatus::Partial, "Reports the fixed units/tolerance policy; there is no per-import unit override dialog.");
}

}  // namespace dino8::app
