// Named macro OBJECTS (PARITY_MAP.md's "VBA-style macro recorder and editor"
// item): SaveMacro / ListMacros / ShowMacro / EditMacro / RenameMacro /
// DeleteMacro / DuplicateMacro / RunMacro / ExportMacro / ImportMacro /
// BindMacro / UnbindMacro, plus the older MacroSave / MacroLoad / MacroDelete /
// RunSavedMacro commands, now thin wrappers over the same library
// (AppState::macro_lib, app/MacroLibrary.h) so there is exactly one store.
//
// Bindings: a macro is bound to an alias (and optionally a toolbar button)
// BY NAME - the alias target is the text  RunMacro "<name>" [args]  - so the
// steps are looked up at run time (an edited macro runs its new steps from
// the same button) and the alias table stays the single source of truth for
// what is bound: nothing here keeps a second list that could go stale.
// RenameMacro rewrites the aliases that name the old macro; DeleteMacro
// removes them (and their toolbar buttons) rather than leave dead buttons.
#include "commands/cmd_common.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "app/MacroLibrary.h"
#include "ui/Panels.h"

namespace dino8::app {

namespace {

MacroLibrary& Lib(CommandContext& ctx) { return ctx.App().State().macro_lib; }

std::string Trimmed(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

// "Key=Value" with an identifier key.
bool SplitKeyValue(const std::string& tok, std::string& key, std::string& value) {
  const size_t eq = tok.find('=');
  if (eq == std::string::npos || eq == 0) return false;
  const std::string k = tok.substr(0, eq);
  if (!ValidParamName(k)) return false;
  key = k;
  value = tok.substr(eq + 1);
  return true;
}

bool YesValue(const std::string& v) {
  const std::string l = ToLower(v);
  return l == "yes" || l == "y" || l == "true" || l == "1" || l == "on";
}

std::vector<std::string> SplitTags(const std::string& text) {
  std::vector<std::string> tags;
  std::istringstream in(text);
  std::string t;
  while (std::getline(in, t, ',')) {
    t = Trimmed(t);
    if (!t.empty() && std::find(tags.begin(), tags.end(), t) == tags.end()) tags.push_back(t);
  }
  return tags;
}

std::string Join(const std::vector<std::string>& v, const std::string& sep, size_t from = 0) {
  std::string out;
  for (size_t i = from; i < v.size(); ++i) out += (i > from ? sep : std::string()) + v[i];
  return out;
}

std::string QuoteIfNeeded(const std::string& s) {
  if (s.empty() || s.find_first_of(" \t;") != std::string::npos) return "\"" + s + "\"";
  return s;
}

std::string ParamLabel(const MacroParam& p) {
  return "{" + p.name + "}" + (p.has_default ? "=" + p.default_value : std::string());
}

// ---- bindings (alias table is the source of truth) ------------------------

// If `target` is a RunMacro / RunSavedMacro command line, returns its tokens.
bool ParseBindingTarget(const std::string& target, std::vector<std::string>& toks) {
  toks = CommandEngine::Tokenize(target);
  if (toks.size() < 2) return false;
  std::string cmd = ToLower(toks[0]);
  while (!cmd.empty() && (cmd.front() == '_' || cmd.front() == '-' || cmd.front() == '!')) cmd.erase(cmd.begin());
  return cmd == "runmacro" || cmd == "runsavedmacro";
}

}  // namespace

std::vector<std::string> MacroBoundAliases(Application& app, const std::string& macro_name) {
  std::vector<std::string> out;
  const std::string want = ToLower(macro_name);
  for (const auto& [alias, target] : app.Engine().Aliases()) {
    std::vector<std::string> toks;
    if (ParseBindingTarget(target, toks) && ToLower(toks[1]) == want) out.push_back(alias);
  }
  return out;
}

namespace {

std::vector<std::string> BoundAliases(CommandContext& ctx, const std::string& macro_name) { return MacroBoundAliases(ctx.App(), macro_name); }

bool OnToolbar(CommandContext& ctx, const std::string& alias) {
  const auto& tb = ctx.App().toolbar_commands;
  return std::find(tb.begin(), tb.end(), alias) != tb.end();
}

void RemoveFromToolbar(CommandContext& ctx, const std::string& alias) {
  auto& tb = ctx.App().toolbar_commands;
  tb.erase(std::remove(tb.begin(), tb.end(), alias), tb.end());
}

// ---- argument plumbing ---------------------------------------------------

// A command whose arguments are all text tokens: drains everything typed after
// the command name, prompts for missing required ones, then runs once.
class ArgCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    while (std::optional<std::string> t = ctx.Engine().TakePendingInput()) args_.push_back(*t);
    Advance(ctx);
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    args_.push_back(t);
    Advance(ctx);
  }
  void OnEnter(CommandContext& ctx) override { OnBareEnter(ctx); }

 protected:
  virtual size_t MinArgs() const = 0;
  virtual std::string PromptFor(size_t index) const = 0;
  // Returns true when finished (the base then calls Finish()).
  virtual bool Run(CommandContext& ctx, std::vector<std::string>& args) = 0;
  virtual void OnBareEnter(CommandContext&) { Finish(); }

 private:
  void Advance(CommandContext& ctx) {
    if (args_.size() >= MinArgs()) {
      if (Run(ctx, args_)) Finish();
      return;
    }
    WantText(PromptFor(args_.size()));
  }
  std::vector<std::string> args_;
};

std::string StepsSummary(const MacroObject& m) {
  return std::to_string(m.steps.size()) + " step(s)";
}

// ---- SaveMacro -----------------------------------------------------------

class SaveMacroCommand : public ArgCommand {
 protected:
  size_t MinArgs() const override { return 1; }
  std::string PromptFor(size_t) const override { return "Name for the new macro (saved from the Macro Editor's recorder buffer)"; }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    const std::string name = args[0];
    std::string description;
    std::optional<std::vector<std::string>> tags;
    bool overwrite = false, clear = false, have_desc = false;
    for (size_t i = 1; i < args.size(); ++i) {
      std::string k, v;
      if (SplitKeyValue(args[i], k, v)) {
        const std::string lk = ToLower(k);
        if (lk == "description") { description = UnescapeMacroArg(v); have_desc = true; }
        else if (lk == "tags") tags = SplitTags(v);
        else if (lk == "overwrite") overwrite = YesValue(v);
        else if (lk == "clear") clear = YesValue(v);
        else { ctx.Warn("SaveMacro: unknown option '" + k + "' (Description=, Tags=, Overwrite=, Clear=)"); return true; }
      } else if (!have_desc) {
        description = UnescapeMacroArg(args[i]);
        have_desc = true;
      } else {
        ctx.Warn("SaveMacro: unexpected argument '" + args[i] + "'");
        return true;
      }
    }
    std::string why;
    if (!ValidMacroName(name, &why)) { ctx.Warn("SaveMacro: " + why); return true; }
    MacroLibrary& lib = Lib(ctx);
    MacroObject* m = lib.Find(name);
    if (m && !overwrite) {
      ctx.Warn("SaveMacro: a macro named '" + m->name + "' already exists (SaveMacro \"" + m->name + "\" Overwrite=Yes replaces its steps; EditMacro edits them in place)");
      return true;
    }
    const std::vector<std::string> steps = MacroStepsFromText(ctx.App().State().macro_text);
    if (steps.empty()) {
      ctx.Warn("SaveMacro: the recorder buffer is empty (RecordMacro On, type some commands, RecordMacro Off - or write lines in the Macro Editor)");
      return true;
    }
    const bool existed = m != nullptr;
    if (existed) {
      m->steps = steps;
      MacroLibrary::Touch(*m);
    } else {
      m = lib.Create(name, steps);
    }
    if (have_desc) m->description = description;
    if (tags) m->tags = *tags;
    const std::vector<std::string> added = SyncMacroParams(*m);
    (void)added;
    ctx.Print(std::string(existed ? "Replaced the steps of macro '" : "Saved macro '") + m->name + "': " + StepsSummary(*m) +
              (m->params.empty() ? ", no parameters" : ", parameters " + [&] {
                std::string p;
                for (const MacroParam& mp : m->params) p += (p.empty() ? "" : ", ") + ParamLabel(mp);
                return p;
              }()));
    if (clear) { ctx.App().State().macro_text.clear(); ctx.Print("SaveMacro: the recorder buffer was cleared"); }
    return true;
  }
};

// ---- ListMacros ----------------------------------------------------------

class ListMacrosCommand : public ArgCommand {
 protected:
  size_t MinArgs() const override { return 0; }
  std::string PromptFor(size_t) const override { return ""; }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    std::string tag_filter, text_filter;
    for (const std::string& a : args) {
      std::string k, v;
      if (!SplitKeyValue(a, k, v)) { ctx.Warn("ListMacros: unexpected argument '" + a + "' (Tag=, Filter=)"); return true; }
      if (ToLower(k) == "tag") tag_filter = ToLower(v);
      else if (ToLower(k) == "filter") text_filter = ToLower(v);
      else { ctx.Warn("ListMacros: unknown option '" + k + "' (Tag=, Filter=)"); return true; }
    }
    const MacroLibrary& lib = Lib(ctx);
    size_t shown = 0;
    std::vector<std::string> lines;
    for (const std::string& n : lib.Names()) {
      const MacroObject* m = lib.Find(n);
      if (!tag_filter.empty()) {
        bool has = false;
        for (const std::string& t : m->tags) if (ToLower(t) == tag_filter) has = true;
        if (!has) continue;
      }
      if (!text_filter.empty() && ToLower(m->name + " " + m->description).find(text_filter) == std::string::npos) continue;
      std::string line = "  " + m->name + "  [" + StepsSummary(*m);
      if (!m->params.empty()) {
        line += "; params ";
        for (size_t i = 0; i < m->params.size(); ++i) line += (i ? ", " : "") + ParamLabel(m->params[i]);
      }
      if (!m->tags.empty()) line += "; tags " + Join(m->tags, ", ");
      line += "]";
      if (!m->description.empty()) line += "  " + m->description;
      lines.push_back(line);
      ++shown;
    }
    ctx.Print("Macro library: " + std::to_string(shown) + " macro(s)" + (shown != lib.All().size() ? " shown of " + std::to_string(lib.All().size()) : std::string()));
    for (const std::string& l : lines) ctx.Print(l);
    return true;
  }
};

// ---- ShowMacro -----------------------------------------------------------

class ShowMacroCommand : public ArgCommand {
 protected:
  size_t MinArgs() const override { return 1; }
  std::string PromptFor(size_t) const override { return "Macro name to show (ListMacros lists them)"; }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    const MacroObject* m = Lib(ctx).Find(args[0]);
    if (!m) { ctx.Warn("ShowMacro: no macro named '" + args[0] + "'"); return true; }
    ctx.Print("Macro '" + m->name + "'");
    ctx.Print("  Description: " + (m->description.empty() ? std::string("(none)") : m->description));
    ctx.Print("  Tags: " + (m->tags.empty() ? std::string("(none)") : Join(m->tags, ", ")));
    ctx.Print("  Created: " + FormatMacroTime(m->created_ms));
    ctx.Print("  Modified: " + FormatMacroTime(m->modified_ms));
    const std::vector<std::string> used = MacroPlaceholders(*m);
    ctx.Print("  Parameters (" + std::to_string(m->params.size()) + "):");
    for (const MacroParam& p : m->params) {
      const bool is_used = std::find(used.begin(), used.end(), p.name) != used.end();
      ctx.Print("    {" + p.name + "}  " + (p.has_default ? "default \"" + p.default_value + "\"" : std::string("required")) +
                (p.prompt.empty() ? std::string() : ", prompt \"" + p.prompt + "\"") + (is_used ? "" : "  (unused by any step)"));
    }
    const std::vector<std::string> bound = BoundAliases(ctx, m->name);
    std::string b;
    for (const std::string& a : bound) b += (b.empty() ? "" : ", ") + a + (OnToolbar(ctx, a) ? " (alias + toolbar button)" : " (alias)");
    ctx.Print("  Bound to: " + (b.empty() ? std::string("(nothing)") : b));
    ctx.Print("  Steps (" + std::to_string(m->steps.size()) + "):");
    for (size_t i = 0; i < m->steps.size(); ++i) ctx.Print("    " + std::to_string(i + 1) + ": " + m->steps[i]);
    return true;
  }
};

// ---- EditMacro -----------------------------------------------------------

class EditMacroCommand : public ArgCommand {
 protected:
  size_t MinArgs() const override { return 2; }
  std::string PromptFor(size_t i) const override {
    return i == 0 ? "Macro name to edit" : "Edit action (Insert N text, Append text, Replace N text, Delete N, Describe text, Tags a,b, Param name [Default=v] [Prompt=text], Unparam name)";
  }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    MacroLibrary& lib = Lib(ctx);
    MacroObject* m = lib.Find(args[0]);
    if (!m) { ctx.Warn("EditMacro: no macro named '" + args[0] + "'"); return true; }
    const std::string action = ToLower(args[1]);
    auto step_index = [&](size_t arg_pos, size_t lo, size_t hi, size_t& out) {
      if (args.size() <= arg_pos) { ctx.Warn("EditMacro: " + args[1] + " needs a step number"); return false; }
      char* e = nullptr;
      const long v = std::strtol(args[arg_pos].c_str(), &e, 10);
      if (!e || *e || v < static_cast<long>(lo) || v > static_cast<long>(hi)) {
        ctx.Warn("EditMacro: step number " + args[arg_pos] + " is out of range (macro '" + m->name + "' has " + std::to_string(m->steps.size()) +
                 " step(s); valid here: " + std::to_string(lo) + ".." + std::to_string(hi) + ")");
        return false;
      }
      out = static_cast<size_t>(v);
      return true;
    };
    auto step_text = [&](size_t from, std::string& out) {
      out = UnescapeMacroArg(Trimmed(Join(args, " ", from)));
      if (args.size() <= from || out.empty()) { ctx.Warn("EditMacro: " + args[1] + " needs the command line for the step (use \\q for a double quote)"); return false; }
      return true;
    };
    auto finish_steps = [&](const std::string& what) {
      const std::vector<std::string> added = SyncMacroParams(*m);
      MacroLibrary::Touch(*m);
      std::string msg = "EditMacro '" + m->name + "': " + what + " (" + StepsSummary(*m) + ")";
      if (!added.empty()) {
        msg += "; new parameter(s):";
        for (const std::string& a : added) msg += " {" + a + "}";
      }
      ctx.Print(msg);
    };
    size_t n = 0;
    std::string text;
    if (action == "insert") {
      if (!step_index(2, 1, m->steps.size() + 1, n) || !step_text(3, text)) return true;
      m->steps.insert(m->steps.begin() + static_cast<long>(n - 1), text);
      finish_steps("inserted step " + std::to_string(n) + ": " + text);
    } else if (action == "append") {
      if (!step_text(2, text)) return true;
      m->steps.push_back(text);
      finish_steps("appended step " + std::to_string(m->steps.size()) + ": " + text);
    } else if (action == "replace") {
      if (!step_index(2, 1, m->steps.size(), n) || !step_text(3, text)) return true;
      const std::string old = m->steps[n - 1];
      m->steps[n - 1] = text;
      finish_steps("replaced step " + std::to_string(n) + " (was: " + old + ") with: " + text);
    } else if (action == "delete") {
      if (!step_index(2, 1, m->steps.size(), n)) return true;
      const std::string old = m->steps[n - 1];
      m->steps.erase(m->steps.begin() + static_cast<long>(n - 1));
      finish_steps("deleted step " + std::to_string(n) + " (was: " + old + ")");
    } else if (action == "describe") {
      m->description = UnescapeMacroArg(Trimmed(Join(args, " ", 2)));
      MacroLibrary::Touch(*m);
      ctx.Print("EditMacro '" + m->name + "': description set to \"" + m->description + "\"");
    } else if (action == "tags") {
      m->tags = SplitTags(Join(args, ",", 2));
      MacroLibrary::Touch(*m);
      ctx.Print("EditMacro '" + m->name + "': tags now " + (m->tags.empty() ? std::string("(none)") : Join(m->tags, ", ")));
    } else if (action == "param") {
      if (args.size() < 3 || !ValidParamName(args[2])) { ctx.Warn("EditMacro: Param needs a parameter name (letters, digits, underscore)"); return true; }
      MacroParam* p = nullptr;
      for (MacroParam& q : m->params) if (q.name == args[2]) p = &q;
      if (!p) { m->params.push_back(MacroParam{}); p = &m->params.back(); p->name = args[2]; }
      for (size_t i = 3; i < args.size(); ++i) {
        std::string k, v;
        if (!SplitKeyValue(args[i], k, v)) { ctx.Warn("EditMacro: Param option '" + args[i] + "' is not Default=, Prompt= or NoDefault=Yes"); return true; }
        const std::string lk = ToLower(k);
        if (lk == "default") { p->has_default = true; p->default_value = UnescapeMacroArg(v); }
        else if (lk == "prompt") p->prompt = UnescapeMacroArg(v);
        else if (lk == "nodefault") { if (YesValue(v)) { p->has_default = false; p->default_value.clear(); } }
        else { ctx.Warn("EditMacro: unknown Param option '" + k + "'"); return true; }
      }
      MacroLibrary::Touch(*m);
      ctx.Print("EditMacro '" + m->name + "': parameter " + ParamLabel(*p) + (p->has_default ? "" : " (required)") +
                (p->prompt.empty() ? std::string() : ", prompt \"" + p->prompt + "\""));
    } else if (action == "unparam") {
      if (args.size() < 3) { ctx.Warn("EditMacro: Unparam needs a parameter name"); return true; }
      auto it = std::find_if(m->params.begin(), m->params.end(), [&](const MacroParam& p) { return p.name == args[2]; });
      if (it == m->params.end()) { ctx.Warn("EditMacro: macro '" + m->name + "' has no parameter '" + args[2] + "'"); return true; }
      const std::vector<std::string> used = MacroPlaceholders(*m);
      if (std::find(used.begin(), used.end(), args[2]) != used.end()) {
        ctx.Warn("EditMacro: {" + args[2] + "} is still used by a step; edit or delete that step first (a used placeholder is always a parameter)");
        return true;
      }
      m->params.erase(it);
      MacroLibrary::Touch(*m);
      ctx.Print("EditMacro '" + m->name + "': removed parameter {" + args[2] + "}");
    } else {
      ctx.Warn("EditMacro: unknown action '" + args[1] + "' (Insert, Append, Replace, Delete, Describe, Tags, Param, Unparam)");
    }
    return true;
  }
};

// ---- Rename / Delete / Duplicate / Bind ----------------------------------

std::string BindingTarget(const std::string& macro, const std::string& extra_args) {
  return "RunMacro \"" + macro + "\"" + (extra_args.empty() ? std::string() : " " + extra_args);
}

// Removes a macro plus every alias (and toolbar button) bound to it.
bool EraseWithBindings(CommandContext& ctx, const std::string& name, std::vector<std::string>* removed) {
  MacroLibrary& lib = Lib(ctx);
  const MacroObject* m = lib.Find(name);
  if (!m) return false;
  const std::string real = m->name;
  for (const std::string& alias : BoundAliases(ctx, real)) {
    ctx.Engine().Aliases().erase(alias);
    RemoveFromToolbar(ctx, alias);
    if (removed) removed->push_back(alias);
  }
  lib.Erase(real);
  return true;
}

class DeleteMacroCommand : public ArgCommand {
 public:
  explicit DeleteMacroCommand(std::string label = "DeleteMacro") : label_(std::move(label)) {}

 protected:
  size_t MinArgs() const override { return 1; }
  std::string PromptFor(size_t) const override { return "Macro name to delete"; }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    const MacroObject* m = Lib(ctx).Find(args[0]);
    const std::string shown = m ? m->name : args[0];
    std::vector<std::string> removed;
    if (!EraseWithBindings(ctx, args[0], &removed)) { ctx.Warn(label_ + ": no " + (label_ == "MacroDelete" ? "saved " : "") + "macro named '" + args[0] + "'"); return true; }
    ctx.Print("Deleted macro '" + shown + "'");
    if (!removed.empty()) ctx.Print("  also removed the alias/toolbar binding(s): " + Join(removed, ", "));
    return true;
  }
  void OnBareEnter(CommandContext& ctx) override {
    if (label_ == "MacroDelete") { const auto names = Lib(ctx).Names(); if (names.empty()) ctx.Print("No saved macros."); else for (const auto& n : names) ctx.Print(n); }
    Finish();
  }

 private:
  std::string label_;
};

class RenameMacroCommand : public ArgCommand {
 protected:
  size_t MinArgs() const override { return 2; }
  std::string PromptFor(size_t i) const override { return i == 0 ? "Macro name to rename" : "New name"; }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    MacroLibrary& lib = Lib(ctx);
    MacroObject* m = lib.Find(args[0]);
    if (!m) { ctx.Warn("RenameMacro: no macro named '" + args[0] + "'"); return true; }
    std::string why;
    if (!ValidMacroName(args[1], &why)) { ctx.Warn("RenameMacro: " + why); return true; }
    const MacroObject* clash = lib.Find(args[1]);
    if (clash && clash != m) { ctx.Warn("RenameMacro: a macro named '" + clash->name + "' already exists"); return true; }
    const std::string old = m->name;
    // Re-point every alias that names the old macro BEFORE the rename, while
    // BoundAliases can still find them by the old name.
    int updated = 0;
    for (const std::string& alias : BoundAliases(ctx, old)) {
      std::string& target = ctx.Engine().Aliases()[alias];
      std::vector<std::string> toks;
      ParseBindingTarget(target, toks);
      toks[1] = args[1];
      std::string rebuilt;
      for (size_t i = 0; i < toks.size(); ++i) rebuilt += (i ? " " : "") + (i == 1 ? "\"" + toks[i] + "\"" : QuoteIfNeeded(toks[i]));
      target = rebuilt;
      ++updated;
    }
    m->name = args[1];
    MacroLibrary::Touch(*m);
    ctx.Print("Renamed macro '" + old + "' to '" + m->name + "' (" + std::to_string(updated) + " alias binding(s) updated)");
    return true;
  }
};

class DuplicateMacroCommand : public ArgCommand {
 protected:
  size_t MinArgs() const override { return 2; }
  std::string PromptFor(size_t i) const override { return i == 0 ? "Macro name to duplicate" : "Name for the copy"; }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    MacroLibrary& lib = Lib(ctx);
    const MacroObject* src = lib.Find(args[0]);
    if (!src) { ctx.Warn("DuplicateMacro: no macro named '" + args[0] + "'"); return true; }
    std::string why;
    if (!ValidMacroName(args[1], &why)) { ctx.Warn("DuplicateMacro: " + why); return true; }
    if (lib.Find(args[1])) { ctx.Warn("DuplicateMacro: a macro named '" + lib.Find(args[1])->name + "' already exists"); return true; }
    MacroObject copy = *src;  // copy before push_back can invalidate `src`
    copy.name = args[1];
    copy.created_ms = copy.modified_ms = MacroNowMs();
    lib.All().push_back(std::move(copy));
    ctx.Print("Duplicated macro '" + args[0] + "' as '" + args[1] + "' (" + StepsSummary(lib.All().back()) + "; bindings are not copied)");
    return true;
  }
};

class BindMacroCommand : public ArgCommand {
 protected:
  size_t MinArgs() const override { return 2; }
  std::string PromptFor(size_t i) const override { return i == 0 ? "Macro name to bind" : "Alias name to bind it to"; }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    const MacroObject* m = Lib(ctx).Find(args[0]);
    if (!m) { ctx.Warn("BindMacro: no macro named '" + args[0] + "'"); return true; }
    const std::string alias = ToLower(args[1]);
    if (alias.empty() || alias.find_first_of(" \t;\"") != std::string::npos) { ctx.Warn("BindMacro: an alias is one word without quotes or ';'"); return true; }
    bool toolbar = false, overwrite = false;
    std::string extra;
    for (size_t i = 2; i < args.size(); ++i) {
      std::string k, v;
      if (!SplitKeyValue(args[i], k, v)) { ctx.Warn("BindMacro: unexpected argument '" + args[i] + "' (Toolbar=Yes, Args=\"key=value ...\", Overwrite=Yes)"); return true; }
      const std::string lk = ToLower(k);
      if (lk == "toolbar") toolbar = YesValue(v);
      else if (lk == "overwrite") overwrite = YesValue(v);
      else if (lk == "args") extra = Trimmed(v);
      else { ctx.Warn("BindMacro: unknown option '" + k + "' (Toolbar=, Args=, Overwrite=)"); return true; }
    }
    if (ctx.Engine().Find(alias)) { ctx.Warn("BindMacro: '" + alias + "' is a command name; pick a different alias"); return true; }
    auto& aliases = ctx.Engine().Aliases();
    const auto existing = aliases.find(alias);
    if (existing != aliases.end() && !overwrite) {
      std::vector<std::string> toks;
      const bool is_macro = ParseBindingTarget(existing->second, toks);
      if (!is_macro || ToLower(toks[1]) != ToLower(m->name)) {
        ctx.Warn("BindMacro: alias '" + alias + "' already exists (-> " + existing->second + "); add Overwrite=Yes to replace it");
        return true;
      }
    }
    aliases[alias] = BindingTarget(m->name, extra);
    ctx.Print("Bound macro '" + m->name + "' to alias '" + alias + "' (-> " + aliases[alias] + ")");
    if (toolbar) {
      if (OnToolbar(ctx, alias)) ctx.Print("  '" + alias + "' is already a Standard toolbar button");
      else if (AddToolbarCommand(ctx.App(), alias)) ctx.Print("  added '" + alias + "' to the Standard toolbar (now " + std::to_string(ctx.App().toolbar_commands.size()) + " buttons)");
      else ctx.Warn("BindMacro: could not add '" + alias + "' to the toolbar");
    }
    return true;
  }
};

class UnbindMacroCommand : public ArgCommand {
 protected:
  size_t MinArgs() const override { return 1; }
  std::string PromptFor(size_t) const override { return "Alias (bound to a macro) to remove"; }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    const std::string alias = ToLower(args[0]);
    auto& aliases = ctx.Engine().Aliases();
    const auto it = aliases.find(alias);
    std::vector<std::string> toks;
    if (it == aliases.end() || !ParseBindingTarget(it->second, toks)) { ctx.Warn("UnbindMacro: '" + args[0] + "' is not an alias bound to a macro"); return true; }
    const bool tb = OnToolbar(ctx, alias);
    aliases.erase(it);
    RemoveFromToolbar(ctx, alias);
    ctx.Print("Unbound alias '" + alias + "' from macro '" + toks[1] + "'" + (tb ? " and removed its toolbar button" : ""));
    return true;
  }
};

// ---- RunMacro ------------------------------------------------------------

class RunMacroCommand : public Command {
 public:
  explicit RunMacroCommand(std::string label = "RunMacro") : label_(std::move(label)) {}

  void Begin(CommandContext& ctx) override {
    std::vector<std::string> args;
    while (std::optional<std::string> t = ctx.Engine().TakePendingInput()) args.push_back(*t);
    if (args.empty()) { WantText("Macro name to run (or Enter to list the macros)"); return; }
    Start(ctx, args);
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (!prompting_) { Start(ctx, {t}); return; }
    values_[todo_.front()] = UnescapeMacroArg(t);
    todo_.erase(todo_.begin());
    Next(ctx);
  }
  void OnEnter(CommandContext& ctx) override {
    if (prompting_) {
      ctx.Warn(label_ + ": cancelled - no value given for {" + todo_.front() + "}; nothing was run");
    } else {
      const auto names = Lib(ctx).Names();
      if (names.empty()) ctx.Print("No saved macros.");
      for (const std::string& n : names) ctx.Print(n);
    }
    Finish();
  }

 private:
  void Start(CommandContext& ctx, const std::vector<std::string>& args) {
    const MacroObject* found = Lib(ctx).Find(args[0]);
    if (!found) { ctx.Warn(label_ + ": no saved macro named '" + args[0] + "'"); Finish(); return; }
    macro_ = *found;
    if (macro_.steps.empty()) { ctx.Warn(label_ + ": macro '" + macro_.name + "' has no steps"); Finish(); return; }
    // The parameters this run needs: every {placeholder} the steps use.
    const std::vector<std::string> used = MacroPlaceholders(macro_);
    std::vector<std::string> order;  // declared order first, then any undeclared
    for (const MacroParam& p : macro_.params) if (std::find(used.begin(), used.end(), p.name) != used.end()) order.push_back(p.name);
    for (const std::string& u : used) if (std::find(order.begin(), order.end(), u) == order.end()) order.push_back(u);
    auto canonical = [&](const std::string& key) -> std::string {
      for (const std::string& o : order) if (ToLower(o) == ToLower(key)) return o;
      return "";
    };
    std::vector<std::string> positional;
    for (size_t i = 1; i < args.size(); ++i) {
      std::string k, v;
      if (SplitKeyValue(args[i], k, v)) {
        const std::string c = canonical(k);
        if (c.empty()) {
          ctx.Warn(label_ + ": macro '" + macro_.name + "' has no parameter '" + k + "'" +
                   (order.empty() ? std::string(" (it takes no parameters)") : " (parameters: " + Join(order, ", ") + ")"));
          Finish();
          return;
        }
        values_[c] = UnescapeMacroArg(v);
      } else {
        positional.push_back(UnescapeMacroArg(args[i]));
      }
    }
    size_t next_pos = 0;
    for (const std::string& o : order) {
      if (next_pos >= positional.size()) break;
      if (values_.count(o)) continue;
      values_[o] = positional[next_pos++];
    }
    if (next_pos < positional.size()) {
      ctx.Warn(label_ + ": too many values for macro '" + macro_.name + "' (" + std::to_string(order.size()) + " parameter(s): " + Join(order, ", ") + ")");
      Finish();
      return;
    }
    for (const std::string& o : order) {
      if (values_.count(o)) continue;
      const MacroParam* p = nullptr;
      for (const MacroParam& q : macro_.params) if (q.name == o) p = &q;
      if (p && p->has_default) values_[o] = p->default_value;
      else todo_.push_back(o);
    }
    Next(ctx);
  }

  void Next(CommandContext& ctx) {
    if (!todo_.empty()) {
      prompting_ = true;
      const MacroParam* p = nullptr;
      for (const MacroParam& q : macro_.params) if (q.name == todo_.front()) p = &q;
      const std::string prompt = p && !p->prompt.empty() ? p->prompt : "Value for {" + todo_.front() + "}";
      ctx.Print(label_ + " '" + macro_.name + "': needs a value for {" + todo_.front() + "}");
      WantText(prompt);
      return;
    }
    prompting_ = false;
    std::vector<std::string> lines;
    for (const std::string& step : macro_.steps) {
      std::string out, missing;
      if (!SubstituteStep(step, values_, out, missing)) { ctx.Warn(label_ + ": no value for {" + missing + "}; nothing was run"); Finish(); return; }
      lines.push_back(out);
    }
    std::string with;
    for (const auto& [k, v] : values_) with += (with.empty() ? "" : ", ") + k + "=" + v;
    ctx.Print(label_ + " '" + macro_.name + "': running " + std::to_string(lines.size()) + " step(s)" + (with.empty() ? std::string() : " with " + with));
    Finish();
    for (const std::string& l : lines) ctx.Engine().Execute(l);
  }

  std::string label_;
  MacroObject macro_;
  std::map<std::string, std::string> values_;
  std::vector<std::string> todo_;
  bool prompting_ = false;
};

// ---- ExportMacro / ImportMacro -------------------------------------------

class ExportMacroCommand : public ArgCommand {
 protected:
  size_t MinArgs() const override { return 2; }
  std::string PromptFor(size_t i) const override { return i == 0 ? "Macro name to export (or * for the whole library)" : "File to write"; }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    MacroLibrary& lib = Lib(ctx);
    std::vector<std::string> only;
    if (args[0] == "*" || (ToLower(args[0]) == "all" && !lib.Find(args[0]))) {
      if (lib.Empty()) { ctx.Warn("ExportMacro: the library is empty"); return true; }
    } else {
      const MacroObject* m = lib.Find(args[0]);
      if (!m) { ctx.Warn("ExportMacro: no macro named '" + args[0] + "'"); return true; }
      only.push_back(m->name);
    }
    std::string path = args[1];
    if (std::filesystem::path(path).extension().empty()) path += ".json";
    std::ofstream out(path, std::ios::binary);
    if (!out) { ctx.Warn("ExportMacro: cannot write " + path); return true; }
    out << lib.ToJson(only, true) << "\n";
    out.close();
    if (!out) { ctx.Warn("ExportMacro: writing " + path + " failed"); return true; }
    ctx.Print("Exported " + (only.empty() ? std::to_string(lib.All().size()) + " macro(s) (the whole library)" : "macro '" + only[0] + "'") + " to " + path +
              " (format " + kMacroFormatName + " v" + std::to_string(kMacroFormatVersion) + ")");
    return true;
  }
};

class ImportMacroCommand : public ArgCommand {
 protected:
  size_t MinArgs() const override { return 1; }
  std::string PromptFor(size_t) const override { return "Macro library file to read"; }
  bool Run(CommandContext& ctx, std::vector<std::string>& args) override {
    std::string conflict = "rename";
    for (size_t i = 1; i < args.size(); ++i) {
      std::string k, v;
      if (!SplitKeyValue(args[i], k, v) || ToLower(k) != "conflict") { ctx.Warn("ImportMacro: unexpected argument '" + args[i] + "' (Conflict=Rename|Replace|Skip)"); return true; }
      conflict = ToLower(v);
      if (conflict != "rename" && conflict != "replace" && conflict != "skip") { ctx.Warn("ImportMacro: Conflict must be Rename, Replace or Skip"); return true; }
    }
    std::ifstream in(args[0], std::ios::binary);
    if (!in) { ctx.Warn("ImportMacro: cannot read " + args[0]); return true; }
    std::stringstream buf;
    buf << in.rdbuf();
    json::Value root;
    std::string err;
    if (!json::Parse(buf.str(), root, err)) { ctx.Warn("ImportMacro: " + args[0] + " is not valid JSON (" + err + ")"); return true; }
    std::vector<MacroObject> incoming;
    if (!MacroLibrary::FromJson(root, incoming, err)) { ctx.Warn("ImportMacro: " + args[0] + ": " + err); return true; }
    MacroLibrary& lib = Lib(ctx);
    int added = 0, renamed = 0, replaced = 0, skipped = 0;
    for (MacroObject& m : incoming) {
      MacroObject* clash = lib.Find(m.name);
      if (clash) {
        if (conflict == "skip") { ++skipped; continue; }
        if (conflict == "replace") {
          const std::string keep_name = clash->name;
          *clash = std::move(m);
          clash->name = keep_name;
          ++replaced;
          continue;
        }
        const std::string base = m.name;
        for (int k = 2; lib.Find(m.name); ++k) m.name = base + " (" + std::to_string(k) + ")";
        ++renamed;
      }
      lib.All().push_back(std::move(m));
      ++added;
    }
    ctx.Print("Imported " + std::to_string(added + replaced) + " macro(s) from " + args[0] + ": " + std::to_string(added) + " added (" + std::to_string(renamed) +
              " renamed to avoid a name clash), " + std::to_string(replaced) + " replaced, " + std::to_string(skipped) + " skipped");
    return true;
  }
};

// ---- the older single-text-era commands, now over the same library -------

class MacroSaveCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (std::optional<std::string> t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Name to save the Macro Editor's current buffer as (or Enter to list saved macros)");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    std::string why;
    if (!ValidMacroName(t, &why)) { ctx.Warn("MacroSave: " + why); Finish(); return; }
    MacroLibrary& lib = Lib(ctx);
    std::vector<std::string> steps = MacroStepsFromText(ctx.App().State().macro_text);
    if (MacroObject* m = lib.Find(t)) {
      m->steps = std::move(steps);
      SyncMacroParams(*m);
      MacroLibrary::Touch(*m);
      ctx.Print("Saved macro '" + m->name + "'");
    } else {
      lib.Create(t, std::move(steps));
      ctx.Print("Saved macro '" + t + "'");
    }
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    const auto names = Lib(ctx).Names();
    if (names.empty()) ctx.Print("No saved macros.");
    else for (const std::string& n : names) ctx.Print(n);
    Finish();
  }
};

class MacroLoadCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (std::optional<std::string> t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Saved macro name to load into the Macro Editor buffer (or Enter to list saved macros)");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    const MacroObject* m = Lib(ctx).Find(t);
    if (!m) { ctx.Warn("MacroLoad: no saved macro named '" + t + "'"); Finish(); return; }
    ctx.App().State().macro_text = MacroTextFromSteps(m->steps);
    ctx.Print("Loaded macro '" + m->name + "' into the Macro Editor buffer");
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    const auto names = Lib(ctx).Names();
    if (names.empty()) ctx.Print("No saved macros.");
    else for (const std::string& n : names) ctx.Print(n);
    Finish();
  }
};

}  // namespace

void RegisterMacroCommands(CommandEngine& e) {
  Reg(e, "SaveMacro", Make<SaveMacroCommand>(), CommandStatus::Implemented,
      "Creates a named macro OBJECT in the macro library from the Macro Editor's recorder buffer (RecordMacro On ... Off): "
      "SaveMacro <name> [Description=text] [Tags=a,b] [Overwrite=Yes] [Clear=Yes]. Each buffer line becomes one structured step; "
      "a {placeholder} in a step becomes a parameter. See ListMacros, ShowMacro, EditMacro, RunMacro.");
  Reg(e, "ListMacros", Make<ListMacrosCommand>(), CommandStatus::Implemented,
      "Lists the macro library: name, step count, parameters, tags and description. ListMacros [Tag=x] [Filter=text].");
  Reg(e, "ShowMacro", Make<ShowMacroCommand>(), CommandStatus::Implemented,
      "Shows one macro in full: description, tags, created/modified time, parameters with defaults and prompts, the aliases/toolbar buttons bound to it, and its numbered steps.");
  Reg(e, "EditMacro", Make<EditMacroCommand>(), CommandStatus::Implemented,
      "Edits a macro in place: EditMacro <name> Insert <N> <command line> | Append <command line> | Replace <N> <command line> | Delete <N> | "
      "Describe <text> | Tags <a,b> | Param <name> [Default=v] [Prompt=text] [NoDefault=Yes] | Unparam <name>. \\q in a command line is a double quote. "
      "Every edit updates the macro's modified time; {placeholders} in a new step are declared as parameters automatically.");
  Reg(e, "RenameMacro", Make<RenameMacroCommand>(), CommandStatus::Implemented,
      "Renames a macro; aliases (and so toolbar buttons) bound to it by name are re-pointed at the new name.");
  Reg(e, "DeleteMacro", Make<DeleteMacroCommand>("DeleteMacro"), CommandStatus::Implemented,
      "Deletes a macro and the aliases/toolbar buttons bound to it.");
  Reg(e, "DuplicateMacro", Make<DuplicateMacroCommand>(), CommandStatus::Implemented,
      "Copies a macro (steps, parameters, tags, description) under a new name; bindings are not copied.");
  Reg(e, "RunMacro", Make<RunMacroCommand>("RunMacro"), CommandStatus::Implemented,
      "Runs a library macro: RunMacro <name> [key=value ...] [value ...]. Each {parameter} in the steps is replaced by the key=value given "
      "(or the next bare value, in declaration order), else the parameter's default; a parameter with neither is prompted for on the command line. "
      "Steps run in order as typed command lines.");
  Reg(e, "ExportMacro", Make<ExportMacroCommand>(), CommandStatus::Implemented,
      "Writes one macro (or * for the whole library) to a versioned JSON file (format dino8-macro-library, version 1): ExportMacro <name|*> <file>. "
      "Aliases/toolbar bindings are local settings and are not exported.");
  Reg(e, "ImportMacro", Make<ImportMacroCommand>(), CommandStatus::Implemented,
      "Reads a file written by ExportMacro into the library: ImportMacro <file> [Conflict=Rename|Replace|Skip] (default Rename: a clashing name becomes 'name (2)'). "
      "Created/modified times are preserved. A file of another format, or written by a newer version, is refused.");
  Reg(e, "BindMacro", Make<BindMacroCommand>(), CommandStatus::Implemented,
      "Binds a macro to an alias BY NAME: BindMacro <macro> <alias> [Toolbar=Yes] [Args=\"key=value ...\"] [Overwrite=Yes]. The alias runs RunMacro \"<macro>\" "
      "so editing the macro's steps changes what the alias/button does, and RenameMacro keeps the binding. Toolbar=Yes also adds a Standard toolbar button.");
  Reg(e, "UnbindMacro", Make<UnbindMacroCommand>(), CommandStatus::Implemented,
      "Removes an alias bound to a macro (and its toolbar button, if any).");
  // The older, single-text-era commands - same names and messages, now backed by the library.
  Reg(e, "MacroSave", Make<MacroSaveCommand>(), CommandStatus::Implemented,
      "Saves the Macro Editor's current buffer under a name in the macro library (it becomes a macro object; use SaveMacro for description/tags). No argument lists every saved name.");
  Reg(e, "MacroLoad", Make<MacroLoadCommand>(), CommandStatus::Implemented,
      "Loads a library macro's steps into the Macro Editor's buffer, one per line, replacing its current contents. No argument lists every saved name.");
  Reg(e, "MacroDelete", Make<DeleteMacroCommand>("MacroDelete"), CommandStatus::Implemented, "Removes a named macro from the macro library (same as DeleteMacro).");
  Reg(e, "RunSavedMacro", Make<RunMacroCommand>("RunSavedMacro"), CommandStatus::Implemented,
      "Runs a library macro by name (same as RunMacro) without loading it into or disturbing the Macro Editor's own buffer - the way a toolbar button bound to a macro runs it. No argument lists every saved name.");
}

}  // namespace dino8::app
