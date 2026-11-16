// IGES/STEP option and inspection commands. The actual readers and writers
// live in io/FileIgesStep.{h,cpp} and are wired into Import/Export/Open/Save
// through Application.cpp like every other exchange format; these commands
// cover the handful of Rhino options and inspectors that don't change what
// gets written or read, only how a user configures or inspects the process.
#include "commands/cmd_common.h"
#include "io/FileIgesStep.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dino8::app {

void RegisterExchange2Commands(CommandEngine& e) {
  // These six report a fixed, always-on policy (every supported entity is
  // always read, the level/layer map is always 1:1 by index or number, the
  // units policy is always mm) rather than opening a configuration dialog:
  // there is nothing left to toggle, so reporting the policy correctly *is*
  // the full, honest behavior of the command in this build.
  Reg(e, "IgesImportOptions", Immediate([](CommandContext& ctx) {
        ctx.Print("IGES import always reads every entity this importer supports (100-144, 308/408, 314, 402, 406); there is no reduced-entity mode to configure.");
      }), CommandStatus::Implemented, "Reports the (fixed) set of entities read; Rhino's dialog of import filters isn't reproduced.");
  Reg(e, "IGESStudy", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        app.ShowFileDialog("IGES Study", {".igs", ".iges"}, false, [&app](const std::string& path) {
          Document tmp; std::string summary;
          if (ImportIges(tmp, path, summary)) app.Notify(path + ": " + summary);
          else app.Notify(summary);
        });
      }), CommandStatus::Implemented, "Reads the file into a scratch document and reports the summary; no standalone entity-tree viewer.");
  Reg(e, "ReadEveryIGESEntity", Immediate([](CommandContext& ctx) {
        ctx.Print("Dino 8's IGES reader always reads every supported entity type - ReadEveryIGESEntity has nothing extra to enable.");
      }), CommandStatus::Implemented, "No partial-read mode exists to turn off.");
  // SetIgesLayerLevelMap [<layer>=<level> ...] [Reset]: the layer-to-level table of IGES export. With no argument it lists every layer with the
  // level the next IGES export gives it; <layer>=<level> pairs (level 1 to 9999999, a layer name or full path, quote names with spaces) set
  // levels, Reset forgets all of them (a layer then keeps its index as its level). Nothing changes when any pair is invalid. The table is saved
  // with the document; IGES export writes the mapped level in the directory entry of every entity and in the level definition (406 form 3).
  Reg(e, "SetIgesLayerLevelMap", Immediate([](CommandContext& ctx) {
        Document& doc = ctx.Doc();
        bool reset = false;
        std::vector<std::pair<std::string, int>> pairs;
        bool bad = false;
        while (auto t = ctx.Engine().TakePendingInput()) {
          std::string lower = *t;
          for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          if (lower == "reset") { reset = true; continue; }
          const size_t eq = t->rfind('=');
          std::string layer = eq == std::string::npos ? "" : t->substr(0, eq);
          const std::string number = eq == std::string::npos ? "" : t->substr(eq + 1);
          const bool digits = !number.empty() && number.size() <= 9 && std::all_of(number.begin(), number.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
          if (layer.empty() || !digits) { ctx.Warn("SetIgesLayerLevelMap: '" + *t + "' is not <layer>=<level> (a whole number from 1 to 9999999)"); bad = true; continue; }
          const int level = std::atoi(number.c_str());
          if (doc.FindLayer(layer) < 0) { ctx.Warn("SetIgesLayerLevelMap: no layer named '" + layer + "'"); bad = true; continue; }
          if (level < 1 || level > 9999999) { ctx.Warn("SetIgesLayerLevelMap: level " + number + " is out of range (1 to 9999999)"); bad = true; continue; }
          pairs.emplace_back(layer, level);
        }
        if (bad) return;
        if (!reset && pairs.empty()) {
          const std::map<std::string, int> map = IgesLayerLevelMap(doc);
          ctx.Print("IGES level of each layer on export:");
          for (int i = 0; i < static_cast<int>(doc.Layers().size()); ++i) {
            const std::string path = doc.LayerFullPath(i);
            ctx.Print("  " + path + " -> level " + std::to_string(IgesLevelForLayer(doc, i)) + (map.count(path) ? " (mapped)" : " (layer index)"));
          }
          return;
        }
        doc.BeginChange("SetIgesLayerLevelMap");
        if (reset) ClearIgesLayerLevelMap(doc);
        for (const auto& [layer, level] : pairs) {
          std::string error;
          if (!SetIgesLayerLevel(doc, layer, level, error)) { ctx.Warn("SetIgesLayerLevelMap: " + error); return; }
          ctx.Print("SetIgesLayerLevelMap: layer '" + doc.LayerFullPath(doc.FindLayer(layer)) + "' -> IGES level " + std::to_string(level));
        }
        if (reset && pairs.empty()) ctx.Print("SetIgesLayerLevelMap: every layer is back to its index as IGES level");
      }), CommandStatus::Implemented, "Edits the layer-to-level table used by IGES export: <layer>=<level> pairs set levels, Reset clears them, no argument lists them. Import still maps levels to layers by number and the level names in the file.");
  // STEPTree <file.stp>: prints the file's product structure (one line per occurrence, indented per assembly level) without importing it;
  // with no file name the file dialog asks, and the same listing goes to the notification line.
  Reg(e, "STEPTree", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        auto show = [](const std::string& path, const std::function<void(const std::string&)>& say, const std::function<void(const std::string&)>& fail) {
          std::vector<std::string> lines;
          std::string error;
          if (!ReadStepProductTree(path, lines, error)) { fail(error); return; }
          for (const std::string& l : lines) say(l);
        };
        if (auto p = ctx.Engine().TakePendingInput()) {
          show(*p, [&ctx](const std::string& l) { ctx.Print(l); }, [&ctx](const std::string& l) { ctx.Warn(l); });
          return;
        }
        app.ShowFileDialog("STEP Tree", {".stp", ".step"}, false, [&app, show](const std::string& path) {
          show(path, [&app](const std::string& l) { app.Notify(l); }, [&app](const std::string& l) { app.Notify(l); });
        });
      }), CommandStatus::Implemented, "Lists the product structure (products and assembly usages) of a STEP file, indented by assembly level, without importing it.");
  // StepUnitsAndTolerance <file.stp>: reads the length unit and the model tolerance a STEP file declares and says what an import into the
  // current document would do (scale factor, tolerance in document units). With no file name it reports the units policy of STEP export.
  Reg(e, "StepUnitsAndTolerance", Immediate([](CommandContext& ctx) {
        if (auto p = ctx.Engine().TakePendingInput()) {
          StepUnitsInfo info;
          std::string error;
          if (!ReadStepUnitsAndTolerance(*p, info, error)) { ctx.Warn(error); return; }
          ctx.Print(DescribeStepUnits(ctx.Doc(), info, *p));
          return;
        }
        ctx.Print("STEP export declares the document's own length unit (" + ctx.Settings().unit_system + ") and an UNCERTAINTY_MEASURE_WITH_UNIT of the document's absolute tolerance (" + FormatNumber(ctx.Settings().absolute_tolerance) + "); STEP and IGES import read the file's declared length unit and scale the geometry to the document's unit (the import summary reports the factor). Give a .stp file name to read that file's own unit and tolerance before importing it.");
      }), CommandStatus::Implemented, "With a file name: reports the file's declared length unit and model tolerance and the scale an import would apply; without one: the export policy.");
}

}  // namespace dino8::app
