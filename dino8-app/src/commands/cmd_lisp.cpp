// AutoLISP-style scripting (PARITY_MAP.md's "AutoLISP-equivalent command
// scripting language"): the Lisp dialect itself is script/Lisp.{h,cpp}; this
// file plugs it into the command line.
//   (expr ...)            typed at the command line (CommandEngine::Execute hands the
//                         raw text over), in a script file line, or as  Lisp <expr>
//   LoadLisp <file>       evaluates a .lsp file (RunScript / LoadScript of a .lsp do the same)
//   (defun c:name () ...) defines a new command "name"
//   <config dir>/acad.lsp is loaded at every start-up, so its c: commands survive a restart
#include "commands/cmd_common.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "app/Application.h"
#include "app/Settings.h"
#include "script/Lisp.h"

namespace dino8::app {

void RunLispFile(CommandContext& ctx, const std::string& path);

namespace {

lisp::Interp& TheInterp() {
  static lisp::Interp in;
  return in;
}
std::set<std::string>& LispCommands() {
  static std::set<std::string> s;   // lowercase names registered by (defun c:name ...)
  return s;
}

std::string QuoteArg(const std::string& a) {
  if (a.empty()) return "Enter";
  if (a.find_first_of(" \t\"") != std::string::npos) {
    std::string o = "\"";
    for (char c : a) if (c != '"') o += c;
    return o + "\"";
  }
  return a;
}

// Points the interpreter's host callbacks at the running app. The pointers
// stay valid for the life of the process (one Application).
void BindHost(CommandContext& ctx) {
  CommandEngine* eng = &ctx.Engine();
  Document* doc = &ctx.Doc();
  Application* app = &ctx.App();
  lisp::LispHost& h = TheInterp().host;
  h.print = [eng](const std::string& line) { eng->Print(line); };
  h.command = [eng](const std::vector<std::string>& parts) {
    std::string line = parts[0];
    for (size_t k = 1; k < parts.size(); ++k) line += " " + QuoteArg(parts[k]);
    return eng->RunNested(line, true);
  };
  h.last_entity = [doc]() -> std::optional<long long> {
    const auto& objs = doc->Objects();
    if (objs.empty()) return std::nullopt;
    return static_cast<long long>(objs.back().id);
  };
  h.all_entities = [doc]() {
    std::vector<long long> ids;
    for (const SceneObject& o : doc->Objects()) ids.push_back(static_cast<long long>(o.id));
    return ids;
  };
  h.read_file = [](const std::string& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    return true;
  };
  h.on_define_command = [eng, app](const std::string& fn_name) {
    const std::string cmd = fn_name.substr(2);
    if (cmd.empty()) throw lisp::LispError("(defun c:) needs a command name after c:");
    const std::string lower = ToLower(cmd);
    const RegisteredCommand* existing = eng->Find(lower);
    if (existing && existing->factory && !LispCommands().count(lower))
      throw lisp::LispError("c:" + cmd + " would replace the built-in command " + existing->name + "; not defined");
    LispCommands().insert(lower);
    eng->Register(cmd, Immediate([fn_name](CommandContext& c) {
      lisp::Interp& in = TheInterp();
      try {
        in.CallNamed(fn_name);
        in.FlushOutput();
      } catch (const lisp::LispError& e) {
        in.FlushOutput();
        c.Warn(std::string("Lisp error in ") + fn_name + ": " + e.what());
      }
    }), CommandStatus::Implemented, "User command defined in AutoLISP: (defun " + fn_name + " () ...)");
    (void)app;
  };
}

// Evaluates `text`; echoes the final value like AutoCAD does. Returns false on an error.
bool RunText(CommandContext& ctx, const std::string& text, const std::string& label, bool echo) {
  BindHost(ctx);
  lisp::Interp& in = TheInterp();
  try {
    lisp::Value v = in.EvalText(text);
    in.FlushOutput();
    if (echo) ctx.Print(label + ": " + lisp::Interp::Format(v, true));
    return true;
  } catch (const lisp::LispError& e) {
    in.FlushOutput();
    ctx.Warn(label + " error: " + e.what());
    return false;
  }
}

class LispCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    std::string text = ctx.Engine().PendingLispText();
    ctx.Engine().PendingLispText().clear();
    if (text.empty()) {
      std::deque<std::string>& pend = ctx.Engine().PendingInputs();
      while (!pend.empty()) { text += (text.empty() ? "" : " ") + pend.front(); pend.pop_front(); }
    }
    if (text.empty()) ctx.Warn("Lisp: give an expression, e.g. Lisp (+ 1 2), or type it directly: (+ 1 2)");
    else RunText(ctx, text, "Lisp", true);
    Finish();
  }
};

class LoadLispCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    std::optional<std::string> path = ctx.Engine().TakePendingInput();
    if (!path) ctx.Warn("LoadLisp: give a .lsp file, e.g. LoadLisp tools.lsp");
    else RunLispFile(ctx, *path);
    Finish();
  }
};

}  // namespace

void RunLispFile(CommandContext& ctx, const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) { ctx.Warn("LoadLisp: cannot open " + path); return; }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (RunText(ctx, ss.str(), "LoadLisp " + path, false)) ctx.Print("LoadLisp: loaded " + path);
}

void LoadLispAutoload(Application& app) {
  const std::filesystem::path p = std::filesystem::path(ConfigDirectory()) / "acad.lsp";
  std::error_code ec;
  if (!std::filesystem::exists(p, ec)) return;
  CommandContext ctx(app, app.Doc(), app.Engine());
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  if (RunText(ctx, ss.str(), "acad.lsp", false)) ctx.Print("Lisp: autoloaded acad.lsp");
}

void RegisterLispCommands(CommandEngine& e) {
  Reg(e, "Lisp", Make<LispCommand>(), CommandStatus::Implemented,
      "Evaluates an AutoLISP-style expression: (+ 1 2) typed directly, or Lisp (expr). A subset of AutoLISP: defun (with c:name commands), setq, "
      "if/cond/while/repeat/foreach, list/string/math functions, (command ...) to run any command, (entlast), (ssget \"X\"), (load \"f.lsp\").");
  Reg(e, "LoadLisp", Make<LoadLispCommand>(), CommandStatus::Implemented,
      "Evaluates a .lsp file: LoadLisp <file>. RunScript/LoadScript accept .lsp files too. <config dir>/acad.lsp loads at every start-up.");
}

}  // namespace dino8::app
