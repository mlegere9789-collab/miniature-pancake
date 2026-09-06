// Miscellaneous commands: help, options, aliases, snaps, macros, calculators,
// scripting (RunScript / LoadScript / EditScript / ScriptEditor).
#include "commands/cmd_common.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "script/LuaEngine.h"
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

bool ParseMiscNumber(const std::string& t, double& v) {
  char* e = nullptr;
  v = std::strtod(t.c_str(), &e);
  return e && e != t.c_str() && *e == 0;
}

// Reverse Polish (postfix) evaluator: space-separated numbers, binary
// operators (+ - * / ^), and unary functions (sqrt neg sin cos tan abs).
bool EvaluateRPN(const std::string& text, double& out, std::string& error) {
  std::istringstream in(text);
  std::string tok;
  std::vector<double> s;
  auto pop = [&](double& v) { if (s.empty()) return false; v = s.back(); s.pop_back(); return true; };
  while (in >> tok) {
    double a, b;
    if (tok == "+" || tok == "-" || tok == "*" || tok == "/" || tok == "^") {
      if (!pop(b) || !pop(a)) { error = "not enough operands for '" + tok + "'"; return false; }
      if (tok == "+") s.push_back(a + b);
      else if (tok == "-") s.push_back(a - b);
      else if (tok == "*") s.push_back(a * b);
      else if (tok == "/") { if (b == 0) { error = "division by zero"; return false; } s.push_back(a / b); }
      else s.push_back(std::pow(a, b));
      continue;
    }
    if (tok == "sqrt" || tok == "neg" || tok == "sin" || tok == "cos" || tok == "tan" || tok == "abs") {
      if (!pop(a)) { error = "not enough operands for '" + tok + "'"; return false; }
      if (tok == "sqrt") { if (a < 0) { error = "sqrt of a negative number"; return false; } s.push_back(std::sqrt(a)); }
      else if (tok == "neg") s.push_back(-a);
      else if (tok == "sin") s.push_back(std::sin(a));
      else if (tok == "cos") s.push_back(std::cos(a));
      else if (tok == "tan") s.push_back(std::tan(a));
      else s.push_back(std::fabs(a));
      continue;
    }
    double v;
    if (!ParseMiscNumber(tok, v)) { error = "unknown token '" + tok + "'"; return false; }
    s.push_back(v);
  }
  if (s.size() != 1) { error = s.empty() ? "empty expression" : "leftover operands (missing operator)"; return false; }
  out = s.back();
  return true;
}

class CalcRPNCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantText("RPN expression (e.g. \"3 4 +\")"); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    double v; std::string err;
    if (EvaluateRPN(t, v, err)) { ctx.Print(t + " = " + FormatNumber(v)); ctx.App().Notify(FormatNumber(v)); }
    else ctx.Warn("CalcRPN: " + err);
    Finish();
  }
};

class MacroRunCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantText("Macro (commands separated by ';')"); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    Finish();
    // "Macro Line 0,0,0 10,0,0;SelAll": the rest of the typed line belongs to the macro too.
    std::string text = t;
    while (auto tok = ctx.Engine().TakePendingInput()) text += " " + *tok;
    std::istringstream in(text);
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

bool ParseMiscColor(const std::string& text, Color& out) {
  int r, g, b;
  if (std::sscanf(text.c_str(), "%d,%d,%d", &r, &g, &b) == 3) { out = Color::FromBytes(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255)); return true; }
  static const std::map<std::string, Color> named = {
      {"white", Color::FromBytes(255, 255, 255)}, {"black", Color::FromBytes(0, 0, 0)}, {"red", Color::FromBytes(220, 40, 40)},
      {"green", Color::FromBytes(40, 180, 60)}, {"blue", Color::FromBytes(50, 90, 220)}, {"yellow", Color::FromBytes(240, 220, 60)},
      {"gray", Color::FromBytes(128, 128, 128)}, {"grey", Color::FromBytes(128, 128, 128)}, {"orange", Color::FromBytes(240, 150, 40)}};
  auto it = named.find(ToLower(text));
  if (it == named.end()) return false;
  out = it->second;
  return true;
}

// Sets the selected objects' own display colour (not their layer's),
// scriptable from the command line as "SetRenderColor 255,0,0" - Properties
// offers the same field with a colour-picker widget, which this build has
// no headless equivalent for.
class SetRenderColorCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to set the colour of"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (ids.empty()) { ctx.Warn("SetRenderColor: nothing selected"); Finish(); return; }
    ids_ = ids;
    WantText("Colour (r,g,b 0-255, or a colour name)");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    Color c;
    if (!ParseMiscColor(t, c)) { ctx.Warn("SetRenderColor: use r,g,b (0-255) or a colour name"); Finish(); return; }
    ctx.Doc().BeginChange("SetRenderColor");
    int n = 0;
    for (ObjectId id : ids_) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      o->color = c;
      o->color_by_layer = false;
      o->InvalidateDisplay();
      ++n;
    }
    ctx.Print("SetRenderColor: " + std::to_string(n) + " object(s) set to " + t);
    Finish();
  }
  std::vector<ObjectId> ids_;
};

// ---------------------------------------------------------------------------
// Scripting: RunScript / LoadScript run a Lua file (or a queued Script
// Editor / "= expr" chunk) through the shared LuaEngine, staying interactive
// while it's suspended on rs.GetPoint / rs.GetObject / rs.GetString / rs.GetReal
// / rs.GetInteger - exactly like any other command's Want* prompt, so script
// tokens on the command line ("RunScript t.lua 5,5,5") feed them the same
// way. A ".txt"/".dino"/".cmd" file still runs as a plain command script
// (ReadCommandFile's behaviour), for scripts written before Lua existed.
class ScriptCommand : public Command {
 public:
  explicit ScriptCommand(std::string label) : label_(std::move(label)) {}

  void Begin(CommandContext& ctx) override {
    Application& app = ctx.App();
    std::string code, chunk;
    bool expr = false;
    if (app.TakeQueuedScript(code, chunk, expr)) {
      if (expr ? app.Lua().StartExpression(code) : app.Lua().Start(code, chunk)) Pump(ctx);
      else Finish();
      return;
    }
    if (std::optional<std::string> path = ctx.Engine().TakePendingInput()) {
      RunPath(ctx, *path);
      return;
    }
    if (ctx.ScriptMode() || app.headless) {
      ctx.Warn(label_ + ": no script file given");
      Finish();
      return;
    }
    app.ShowFileDialog(label_, {".lua", ".txt", ".dino", ".cmd"}, false, [&app, label = label_](const std::string& path) {
      app.Engine().Execute("-" + label + " \"" + path + "\"");
    });
    Finish();
  }

  void OnPoint(CommandContext& ctx, Point3d p) override { ctx.App().Lua().ResumePoint(p); Pump(ctx); }
  void OnNumber(CommandContext& ctx, double v) override { ctx.App().Lua().ResumeNumber(v); Pump(ctx); }
  void OnText(CommandContext& ctx, const std::string& t) override { ctx.App().Lua().ResumeText(t); Pump(ctx); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override { ctx.App().Lua().ResumeObjects(ids); Pump(ctx); }
  void OnEnter(CommandContext& ctx) override {
    if (want == Want::Objects) ctx.App().Lua().ResumeObjects({});
    else ctx.App().Lua().ResumeNil();
    Pump(ctx);
  }
  void OnCancel(CommandContext& ctx) override { ctx.App().Lua().Abort(); }

 private:
  void RunPath(CommandContext& ctx, const std::string& raw_path) {
    Application& app = ctx.App();
    const std::string ext = ToLower(std::filesystem::path(raw_path).extension().string());
    if (ext == ".txt" || ext == ".dino" || ext == ".cmd") {
      std::ifstream in(raw_path);
      if (!in) { ctx.Warn(label_ + ": cannot open " + raw_path); Finish(); return; }
      // RunNested keeps this command's own state intact while each line
      // runs (and finishes) as an ordinary top-level command in between.
      std::string line;
      while (std::getline(in, line)) if (!line.empty() && line[0] != '#') ctx.Engine().RunNested(line);
      Finish();
      return;
    }
    if (app.Lua().StartFile(raw_path)) Pump(ctx);
    else Finish();
  }

  // Reflects the running script's current rs.Get* prompt (or finishes the
  // command once the script itself has finished or failed).
  void Pump(CommandContext& ctx) {
    LuaEngine& lua = ctx.App().Lua();
    if (!lua.Running()) { Finish(); return; }
    const ScriptRequest& r = lua.Request();
    switch (r.want) {
      case ScriptWant::Point: WantPoint(r.prompt); break;
      case ScriptWant::Objects: WantObjects(r.prompt, std::max(0, r.min_objects)); accept_preselection = true; break;
      case ScriptWant::Text: WantText(r.prompt, r.default_text); break;
      case ScriptWant::Number:
      case ScriptWant::Integer: WantNumber(r.prompt, r.default_number); break;
      case ScriptWant::Nothing: default: Finish(); break;
    }
  }

  std::string label_;
};

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
  Reg(e, "CalcRPN", Make<CalcRPNCommand>(), CommandStatus::Implemented, "Evaluates a postfix expression (numbers then + - * / ^ or sqrt/neg/sin/cos/tan/abs) with an explicit operand stack.");
  Reg(e, "Macro", Make<MacroRunCommand>());
  Reg(e, "MacroEditor", Immediate([](CommandContext& ctx) { ctx.App().Panels().macro_editor = true; }));
  Reg(e, "ReadCommandFile", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        app.ShowFileDialog("Read command file", {".txt", ".dino", ".cmd"}, false, [&app](const std::string& path) {
          std::ifstream in(path); std::string line;
          while (std::getline(in, line)) if (!line.empty() && line[0] != '#') app.Engine().Execute(line);
        });
      }));
  // Runs after Repeat itself has finished (Execute defers while a command runs); Repeat is never recorded as the last command.
  Reg(e, "Repeat", Immediate([](CommandContext& ctx) { if (!ctx.Engine().LastCommand().empty()) ctx.Engine().Execute(ctx.Engine().LastCommand()); }));
  Reg(e, "Snap", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().grid_snap; }, "Grid snap"));
  Reg(e, "Ortho", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().ortho; }, "Ortho"));
  Reg(e, "Planar", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().planar; }, "Planar"));
  Reg(e, "SmartTrack", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().smart_track; }, "SmartTrack"));
  Reg(e, "DisableOsnap", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().disable_all; }, "Osnaps disabled"));
  Reg(e, "Osnap", Immediate([](CommandContext& ctx) { ctx.App().Panels().object_snaps = true; }));
  Reg(e, "Gumball", Immediate([](CommandContext& ctx) { ctx.App().gumball_enabled = !ctx.App().gumball_enabled; ctx.Print(std::string("Gumball ") + (ctx.App().gumball_enabled ? "on" : "off")); }), CommandStatus::Implemented, "Toggles the gumball widget (move, rotate and scale handles; see GumballSettings).");
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
  Reg(e, "SetRenderColor", Make<SetRenderColorCommand>(), CommandStatus::Implemented, "Sets the selected objects' own display colour to the given r,g,b value or colour name.");
  Reg(e, "SetObjectDisplayMode", Immediate([](CommandContext& ctx) { ctx.App().Panels().display = true; }), CommandStatus::Partial,
      "Opens the viewport Display panel; SceneObject has no per-object display-mode override field, so a mode can only be set per viewport, not per object.");
  // Dragmode: same command name as cmd_state.cpp's "DragMode" (registry
  // keys are case-insensitive) - superseded by that real ChoiceCommand
  // (RegisterStateCommands runs after this file, so it always won here
  // anyway; this stub was dead code).
  Reg(e, "History", Immediate([](CommandContext& ctx) { ctx.Print("History: not recorded. Every edit is captured by the snapshot undo instead."); }), CommandStatus::Partial,
      "There is no constructional-history dependency graph in this build (e.g. a moved curve does not update surfaces built from it); undo snapshots are a substitute for undo/redo only, not for live parametric updates.");
  Reg(e, "RecordHistory", Immediate([](CommandContext& ctx) { ctx.Print("RecordHistory: not needed; undo snapshots cover every change."); }), CommandStatus::Partial,
      "Toggles nothing real: there is no history-recording engine to turn on (see History).");
  // Grasshopper: superseded, dead code - cmd_flow.cpp's real Dino Flow node
  // editor (RegisterFlowCommands runs last, so it always wins here anyway).
  Reg(e, "RunScript", Make<ScriptCommand>("RunScript"),
      CommandStatus::Implemented, "Runs a .lua script (embedded Lua 5.4, rhinoscriptsyntax-like rs.* API) or a .txt command file.");
  Reg(e, "LoadScript", Make<ScriptCommand>("LoadScript"),
      CommandStatus::Implemented, "Runs a .lua script; its top-level functions stay callable afterwards (Lua globals persist for the session).");
  Reg(e, "EditScript", Immediate([](CommandContext& ctx) {
        if (std::optional<std::string> path = ctx.Engine().TakePendingInput()) OpenInScriptEditor(ctx.App(), *path);
        ctx.App().Panels().script_editor = true;
      }), CommandStatus::Implemented, "Opens the Lua Script Editor.");
  Reg(e, "RunPythonScript", Immediate([](CommandContext& ctx) {
        ctx.Print("Dino 8 has no bundled Python interpreter (no CPython in a small offline installer); use RunScript with Lua's rs.* API instead - it covers the same rhinoscriptsyntax surface.");
        ctx.App().Panels().script_editor = true;
      }), CommandStatus::Partial, "Python is not bundled; opens the Lua Script Editor instead.");
  Reg(e, "EditPythonScript", Immediate([](CommandContext& ctx) {
        ctx.Print("Dino 8 has no bundled Python interpreter; use EditScript / the Script Editor to write Lua instead.");
        ctx.App().Panels().script_editor = true;
      }), CommandStatus::Partial, "Python is not bundled; opens the Lua Script Editor instead.");
  Reg(e, "ScriptEditor", Immediate([](CommandContext& ctx) { ctx.App().Panels().script_editor = true; }));
  Reg(e, "ScriptingReference", Immediate([](CommandContext& ctx) { ctx.App().Panels().scripting_reference = true; }));
  // PackageManager / PluginManager: superseded, dead code - cmd_flow.cpp
  // registers the real panel-opening commands (RegisterFlowCommands runs
  // last, so it always wins here anyway).
}

}  // namespace dino8::app
