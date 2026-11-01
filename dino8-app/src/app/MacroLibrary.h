// Named macro OBJECTS (PARITY_MAP.md's "VBA-style macro recorder and editor"
// item). A macro is no longer a block of ';'-separated text: it is a record
// with a name, a description, tags, declared parameters (named {placeholders}
// substituted into the steps at run time, with defaults and prompts), a
// structured list of command-line steps, and created/modified times.
//
// This header is deliberately free of Application/ImGui so the data model,
// the placeholder substitution and the versioned JSON round trip are plain
// functions. The commands that drive it (SaveMacro, ListMacros, ShowMacro,
// EditMacro, RenameMacro, DeleteMacro, DuplicateMacro, RunMacro, ExportMacro,
// ImportMacro, BindMacro, UnbindMacro) live in commands/cmd_macros.cpp; the
// settings file embeds ToJson() under "macro_library" (app/Settings.cpp).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "util/json_mini.h"

namespace dino8::app {

struct MacroParam {
  std::string name;           // identifier used as {name} in the steps
  std::string default_value;  // used when the caller does not supply one
  bool has_default = false;   // false: the value is required (prompted for if missing)
  std::string prompt;         // shown when the value is prompted for ("" = the name)
};

struct MacroObject {
  std::string name;
  std::string description;
  std::vector<std::string> tags;
  std::vector<MacroParam> params;
  std::vector<std::string> steps;  // one command line per entry, placeholders unexpanded
  std::int64_t created_ms = 0;     // milliseconds since the Unix epoch (UTC)
  std::int64_t modified_ms = 0;
};

// The file format's name and the newest version this build reads and writes.
inline constexpr const char* kMacroFormatName = "dino8-macro-library";
inline constexpr int kMacroFormatVersion = 1;

std::int64_t MacroNowMs();
// "2026-11-01T12:34:56.789Z"
std::string FormatMacroTime(std::int64_t ms);
// A valid macro name: 1..80 chars, no control characters, no leading/trailing
// blanks, not "*" (reserved for "every macro" in ExportMacro).
bool ValidMacroName(const std::string& name, std::string* why = nullptr);
bool ValidParamName(const std::string& name);

// Placeholders: {identifier} in a step. "\{" is a literal brace (so Lua
// tables such as "= rs.AddPoint({1,2,3})" stay usable: only {identifier} is
// ever a placeholder, and "\{x}" escapes even that).
std::vector<std::string> ExtractPlaceholders(const std::string& step);
// Every placeholder used by the macro's steps, first-use order, de-duplicated.
std::vector<std::string> MacroPlaceholders(const MacroObject& m);
// Declares (without a default) every placeholder the steps use that has no
// declared parameter yet; returns the names it added.
std::vector<std::string> SyncMacroParams(MacroObject& m);
// Replaces each {name} by values[name]. Fails (returns false, `missing` set)
// if a placeholder has no value.
bool SubstituteStep(const std::string& step, const std::map<std::string, std::string>& values, std::string& out, std::string& missing);
// Command-line text escape used by EditMacro/RunMacro arguments, because the
// command line's tokenizer eats double quotes: "\q" means a literal '"'.
std::string UnescapeMacroArg(const std::string& s);

// Buffer text <-> steps. One step per line; a line is additionally split on
// ';' (the Macro command's separator) unless it is a Lua line ("= ..."). Blank
// lines are dropped. Lines are otherwise kept verbatim (trimmed).
std::vector<std::string> MacroStepsFromText(const std::string& text);
std::string MacroTextFromSteps(const std::vector<std::string>& steps);

class MacroLibrary {
 public:
  std::vector<MacroObject>& All() { return macros_; }
  const std::vector<MacroObject>& All() const { return macros_; }
  // Case-insensitive name lookup (names are unique case-insensitively).
  MacroObject* Find(const std::string& name);
  const MacroObject* Find(const std::string& name) const;
  // Names, sorted case-insensitively.
  std::vector<std::string> Names() const;
  bool Empty() const { return macros_.empty(); }
  void Clear() { macros_.clear(); }
  // Adds a new macro (created/modified = now). Returns nullptr if the name is
  // invalid or taken.
  MacroObject* Create(const std::string& name, std::vector<std::string> steps, const std::string& description = "");
  // Stamps modified: strictly greater than the previous value, so two edits in
  // the same millisecond are still ordered.
  static void Touch(MacroObject& m);
  bool Erase(const std::string& name);

  // JSON. include_header writes {"format","version",...} (a standalone file);
  // otherwise the same object is embedded in settings.json.
  std::string ToJson(const std::vector<std::string>& only_names = {}, bool for_export = false) const;
  // Parses a library object (checks format name and version). On success the
  // macros are appended to `out`; on failure `error` says why.
  static bool FromJson(const json::Value& root, std::vector<MacroObject>& out, std::string& error);
  static std::string MacroToJson(const MacroObject& m, int indent);

 private:
  std::vector<MacroObject> macros_;
};

std::string MacroJsonEscape(const std::string& s);

}  // namespace dino8::app
