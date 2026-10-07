// Sheet set commands: SheetSetNew/SheetSetAdd/SheetSetOpen/SheetSetPlot
// (alias SheetSetPublish). See session/SheetSet.h for the file format and
// what PlotSheetSet actually renders.
//
// Dino 8 has no dockable-panel framework big enough to add a real Sheet
// Set Manager panel in this increment (see src/ui/Panels.cpp's
// DrawLayoutsPanel for the closest existing pattern - wiring a twin of it
// in would also mean touching Application.h's PanelState, MenuBar.cpp and
// the main docking layout, well past what SheetSetOpen needs). SheetSetOpen
// instead lists the set's entries to the command history via ctx.Print,
// same as Worksession's "List" option lists attached reference models -
// an honest, disclosed scope reduction rather than a real panel.
#include "commands/cmd_common.h"

#include <filesystem>

#include "session/SheetSet.h"

namespace dino8::app {

namespace {

// SheetSetNew <file> <name>
class SheetSetNewCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Sheet set file to create (.dss8set)");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (path_.empty()) {
      path_ = t;
      if (auto next = ctx.Engine().TakePendingInput()) { OnText(ctx, *next); return; }
      WantText("Sheet set name", std::filesystem::path(path_).stem().string());
      return;
    }
    std::string error;
    if (!CreateSheetSet(path_, t, error)) { ctx.Warn("SheetSetNew: " + error); Finish(); return; }
    ctx.Print("SheetSetNew: created '" + t + "' at " + path_);
    Finish();
  }

 private:
  std::string path_;
};

// SheetSetAdd <file> [layout]  - adds the current document's active layout
// (or a named one) as an entry. The current document must already be saved
// (a sheet set entry is a file path + layout name - there is nothing to
// point at until the document has one).
class SheetSetAddCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (ctx.Doc().Path().empty()) {
      ctx.Warn("SheetSetAdd: the current document has not been saved yet - save it first so it has a file path to add.");
      Finish();
      return;
    }
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Sheet set file to add to (must already exist - use SheetSetNew)");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (path_.empty()) {
      path_ = t;
      const int idx = ctx.App().ActiveLayoutIndex();
      const std::string current = (idx >= 0 && static_cast<size_t>(idx) < ctx.Doc().Layouts().size())
                                       ? ctx.Doc().Layouts()[static_cast<size_t>(idx)].name
                                       : "";
      if (auto next = ctx.Engine().TakePendingInput()) { OnText(ctx, *next); return; }
      WantText(std::string("Layout to add") + (current.empty() ? " (no active layout - type a layout name)" : ""), current);
      return;
    }
    if (t.empty()) { ctx.Warn("SheetSetAdd: no layout name given"); Finish(); return; }
    if (!ctx.Doc().FindLayout(t)) { ctx.Warn("SheetSetAdd: '" + t + "' is not a layout in the current document"); Finish(); return; }
    std::string error;
    if (!AddSheetSetEntry(path_, ctx.Doc().Path(), t, error)) { ctx.Warn("SheetSetAdd: " + error); Finish(); return; }
    ctx.Print("SheetSetAdd: added " + ctx.Doc().Path() + " / " + t + " to " + path_);
    Finish();
  }

 private:
  std::string path_;
};

// SheetSetOpen <file> - loads a sheet set and lists its entries.
class SheetSetOpenCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Sheet set file to open");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    SheetSetFile set;
    std::string error;
    if (!LoadSheetSet(t, set, error)) { ctx.Warn("SheetSetOpen: " + error); Finish(); return; }
    ctx.Print("SheetSetOpen: '" + set.name + "' (" + t + "), " + std::to_string(set.entries.size()) + " sheet(s)");
    for (size_t i = 0; i < set.entries.size(); ++i) {
      ctx.Print("  " + std::to_string(i + 1) + ". " + set.entries[i].file + " : " + set.entries[i].layout);
    }
    Finish();
  }
};

// SheetSetPlot / SheetSetPublish <file> [output-dir]
class SheetSetPlotCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Sheet set file to plot");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (path_.empty()) {
      path_ = t;
      if (auto next = ctx.Engine().TakePendingInput()) { OnText(ctx, *next); return; }
      WantText("Output folder for the PDFs", std::filesystem::path(path_).stem().string() + "_plot");
      return;
    }
    SheetSetFile set;
    std::string error;
    if (!LoadSheetSet(path_, set, error)) { ctx.Warn("SheetSetPlot: " + error); Finish(); return; }
    if (set.entries.empty()) { ctx.Warn("SheetSetPlot: '" + set.name + "' has no sheets to plot"); Finish(); return; }
    const std::vector<SheetPlotResult> results = PlotSheetSet(set, t);
    int ok = 0;
    for (const SheetPlotResult& r : results) {
      if (r.ok) {
        ++ok;
        ctx.Print("SheetSetPlot: " + r.file + " : " + r.layout + " -> " + r.output_path);
      } else {
        ctx.Warn("SheetSetPlot: " + r.file + " : " + r.layout + " failed - " + r.error);
      }
    }
    ctx.Print("SheetSetPlot: " + std::to_string(ok) + "/" + std::to_string(results.size()) + " sheet(s) plotted to " + t);
    Finish();
  }

 private:
  std::string path_;
};

}  // namespace

void RegisterSheetSetCommands(CommandEngine& e) {
  Reg(e, "SheetSetNew", Make<SheetSetNewCommand>(), CommandStatus::Implemented,
      "Creates a new, named, empty sheet set file (a JSON list of file+layout entries spanning multiple .3dm files).");
  Reg(e, "SheetSetAdd", Make<SheetSetAddCommand>(), CommandStatus::Implemented,
      "Adds the current (saved) document's active layout, or a named layout, to an existing sheet set file.");
  Reg(e, "SheetSetOpen", Make<SheetSetOpenCommand>(), CommandStatus::Implemented,
      "Loads a sheet set file and lists its entries to the command history (no dockable panel in this build - see cmd_sheetset.cpp).");
  Reg(e, "SheetSetPlot", Make<SheetSetPlotCommand>(), CommandStatus::Implemented,
      "Batch-plots every sheet in a sheet set to its own PDF in an output folder, opening each referenced file read-only without disturbing the current document.");
  Reg(e, "SheetSetPublish", Make<SheetSetPlotCommand>(), CommandStatus::Implemented,
      "Alias for SheetSetPlot (AutoCAD calls the same batch operation 'Publish').");
}

}  // namespace dino8::app
