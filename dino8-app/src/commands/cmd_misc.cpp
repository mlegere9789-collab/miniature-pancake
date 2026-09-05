// Miscellaneous commands: help, options, aliases, snaps, macros, calculators.
#include "commands/cmd_common.h"

#include <fstream>
#include <sstream>

#include "ui/Panels.h"

namespace dino8::app {

namespace {

class AliasCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantText("Alias name (or Enter to list)"); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (!alias_) { alias_ = t; WantText("Command for alias " + t); return; }
    ctx.Engine().Aliases()[ToLower(*alias_)] = t;
    ctx.Print("Alias " + *alias_ + " -> " + t);
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    if (!alias_) { for (const auto& [a, c] : ctx.Engine().Aliases()) ctx.Print(a + " -> " + c); }
    Finish();
  }
  std::optional<std::string> alias_;
};

class CalcCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantText("Expression"); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    double v; std::string err;
    if (EvaluateExpression(t, v, err)) { ctx.Print(t + " = " + FormatNumber(v)); ctx.App().Notify(FormatNumber(v)); }
    else ctx.Warn(err);
    Finish();
  }
};

class MacroRunCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantText("Macro (commands separated by ';')"); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    Finish();
    std::istringstream in(t);
    std::string part;
    while (std::getline(in, part, ';')) if (!part.empty()) ctx.Engine().Execute(part);
  }
};

class HelpCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    const std::vector<std::string>& recent = ctx.Engine().RecentCommands();
    if (recent.size() > 1) { ctx.App().ShowHelpFor(recent[1]); Finish(); return; }
    WantText("Command name for help");
  }
  void OnText(CommandContext& ctx, const std::string& t) override { ctx.App().ShowHelpFor(t); Finish(); }
  void OnEnter(CommandContext& ctx) override { ctx.App().Panels().help = true; Finish(); }
};

CommandFactory Toggle(std::function<bool&(CommandContext&)> get, const char* label) {
  return Immediate([get, label](CommandContext& ctx) { bool& b = get(ctx); b = !b; ctx.Print(std::string(label) + (b ? " on" : " off")); });
}

}  // namespace

void RegisterMiscCommands(CommandEngine& e) {
  Reg(e, "Help", Make<HelpCommand>());
  Reg(e, "CommandHelp", Make<HelpCommand>());
  Reg(e, "CommandList", Immediate([](CommandContext& ctx) { ctx.App().Panels().command_list = true; }));
  Reg(e, "CommandHistory", Immediate([](CommandContext& ctx) { ctx.App().Panels().command_history = true; }));
  Reg(e, "CommandPaste", Immediate([](CommandContext& ctx) { ctx.App().Panels().macro_editor = true; }));
  Reg(e, "Options", Immediate([](CommandContext& ctx) { ctx.App().Panels().options = true; }));
  Reg(e, "ToggleLeftSidebar", Immediate([](CommandContext& ctx) { ctx.App().show_left_sidebar = !ctx.App().show_left_sidebar; ctx.Print(std::string("Left sidebar ") + (ctx.App().show_left_sidebar ? "shown." : "hidden.")); }));
  Reg(e, "Toolbar", Immediate([](CommandContext& ctx) { ctx.App().Panels().toolbars = !ctx.App().Panels().toolbars; }));
  Reg(e, "ToolbarReset", Immediate([](CommandContext& ctx) { ctx.App().Panels().toolbars = true; ctx.App().SetViewportLayout(4); }));
  Reg(e, "Alias", Make<AliasCommand>());
  Reg(e, "Calc", Make<CalcCommand>());
  Reg(e, "CalcRPN", Make<CalcCommand>(), CommandStatus::Partial, "Uses infix notation.");
  Reg(e, "Macro", Make<MacroRunCommand>());
  Reg(e, "MacroEditor", Immediate([](CommandContext& ctx) { ctx.App().Panels().macro_editor = true; }));
  Reg(e, "ReadCommandFile", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        app.ShowFileDialog("Read command file", {".txt", ".dino", ".cmd"}, false, [&app](const std::string& path) {
          std::ifstream in(path); std::string line;
          while (std::getline(in, line)) if (!line.empty() && line[0] != '#') app.Engine().Execute(line);
        });
      }));
  Reg(e, "Repeat", Immediate([](CommandContext& ctx) { ctx.Engine().RepeatLast(); }));
  Reg(e, "Snap", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().grid_snap; }, "Grid snap"));
  Reg(e, "Ortho", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().ortho; }, "Ortho"));
  Reg(e, "Planar", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().planar; }, "Planar"));
  Reg(e, "SmartTrack", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().smart_track; }, "SmartTrack"));
  Reg(e, "DisableOsnap", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().disable_all; }, "Osnaps disabled"));
  Reg(e, "Osnap", Immediate([](CommandContext& ctx) { ctx.App().Panels().object_snaps = true; }));
  Reg(e, "Gumball", Immediate([](CommandContext& ctx) { ctx.App().gumball_enabled = !ctx.App().gumball_enabled; ctx.Print(std::string("Gumball ") + (ctx.App().gumball_enabled ? "on" : "off")); }), CommandStatus::Partial, "Move handles; rotate/scale handles are planned.");
  Reg(e, "Materials", Immediate([](CommandContext& ctx) { ctx.App().Panels().materials = true; }));
  Reg(e, "MaterialEditor", Immediate([](CommandContext& ctx) { ctx.App().Panels().materials = true; }));
  Reg(e, "Notifications", Immediate([](CommandContext& ctx) { ctx.App().Panels().notifications = true; }));
  Reg(e, "About", Immediate([](CommandContext& ctx) { ctx.App().Panels().about = true; }));
  Reg(e, "Licenses", Immediate([](CommandContext& ctx) { ctx.Print("Dino 8 is free software. No licence keys, subscriptions or activation exist."); ctx.App().Panels().about = true; }));
  Reg(e, "CheckForUpdates", Immediate([](CommandContext& ctx) { ctx.Print("Dino 8 does not phone home. Get new builds from the project's GitHub releases."); }));
  Reg(e, "WhatsNew", Immediate([](CommandContext& ctx) { ctx.App().Panels().about = true; }));
  Reg(e, "TechSupport", Immediate([](CommandContext& ctx) { ctx.Print("Support: open an issue at https://github.com/mlegere9789-collab/miniature-pancake"); }));
  Reg(e, "LearnRhino", Immediate([](CommandContext& ctx) { ctx.App().Panels().help = true; }));
  Reg(e, "Tutorials", Immediate([](CommandContext& ctx) { ctx.App().Panels().help = true; }));
  Reg(e, "SetRenderColor", OnSelection("Select objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { for (ObjectId id : ids) ctx.Doc().Select(id, true); ctx.App().Panels().properties = true; }), CommandStatus::Partial, "Set colours in Properties.");
  Reg(e, "SetObjectDisplayMode", Immediate([](CommandContext& ctx) { ctx.App().Panels().display = true; }), CommandStatus::Partial, "Per-viewport modes; per-object modes are planned.");
  Reg(e, "Dragmode", Immediate([](CommandContext& ctx) { ctx.Print("Drag mode: CPlane (objects drag along the construction plane)."); }), CommandStatus::Partial);
  Reg(e, "History", Immediate([](CommandContext& ctx) { ctx.Print("History: not recorded. Every edit is captured by the snapshot undo instead."); }), CommandStatus::Partial);
  Reg(e, "RecordHistory", Immediate([](CommandContext& ctx) { ctx.Print("RecordHistory: not needed; undo snapshots cover every change."); }), CommandStatus::Partial);
  Reg(e, "Text", Immediate([](CommandContext& ctx) { ctx.Print("Text: use the Notes panel for document text; 3D text objects are planned."); ctx.App().Panels().notes = true; }), CommandStatus::Partial);
  Reg(e, "Grasshopper", Immediate([](CommandContext& ctx) { ctx.Print("Grasshopper: visual scripting is planned; use Macro / ReadCommandFile for automation today."); ctx.App().Panels().macro_editor = true; }), CommandStatus::Partial);
  Reg(e, "RunScript", Make<MacroRunCommand>(), CommandStatus::Partial, "Runs command macros; Python scripting is planned.");
  Reg(e, "RunPythonScript", Make<MacroRunCommand>(), CommandStatus::Partial, "Runs command macros; Python scripting is planned.");
  Reg(e, "ScriptEditor", Immediate([](CommandContext& ctx) { ctx.App().Panels().macro_editor = true; }), CommandStatus::Partial);
  Reg(e, "PackageManager", Immediate([](CommandContext& ctx) { ctx.Print("PackageManager: plug-ins are planned. Everything built in is free."); }), CommandStatus::Partial);
  Reg(e, "PluginManager", Immediate([](CommandContext& ctx) { ctx.Print("PluginManager: plug-ins are planned."); }), CommandStatus::Partial);
}

}  // namespace dino8::app
