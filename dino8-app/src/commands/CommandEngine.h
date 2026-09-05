// Runs commands: keeps the registry (every catalog command, with an
// implementation where one exists), owns the active command, parses typed
// input (points, numbers, options, relative coordinates), and records the
// command history the Command History panel shows.
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "commands/Command.h"
#include "commands/CommandCatalog.h"

namespace dino8::app {

enum class CommandStatus { Implemented, Partial, Planned };
const char* CommandStatusName(CommandStatus s);

struct RegisteredCommand {
  std::string name;               // canonical (catalog) casing
  const CommandInfo* info = nullptr;
  CommandFactory factory;         // null => Planned
  CommandStatus status = CommandStatus::Planned;
  std::string note;               // shown for Partial/Planned commands
};

class CommandEngine {
 public:
  CommandEngine(Application& app, Document& doc, CommandCatalog& catalog);

  // ---- registration ----------------------------------------------------
  void Register(const std::string& name, CommandFactory factory,
                CommandStatus status = CommandStatus::Implemented, const std::string& note = "");
  // Registers an entry for every catalog command without an implementation.
  void RegisterCatalogPlaceholders();
  const std::map<std::string, RegisteredCommand>& Registry() const { return registry_; }
  const RegisteredCommand* Find(const std::string& name) const;
  size_t CountWithStatus(CommandStatus s) const;

  // ---- aliases -----------------------------------------------------------
  std::map<std::string, std::string>& Aliases() { return aliases_; }
  void InstallDefaultAliases();

  // ---- running commands --------------------------------------------------
  // Runs a typed command line: "Box", "_Line", "-Circle", "!", "Box 0 10".
  void Execute(const std::string& input);
  void RunCommand(const std::string& name, bool script_mode = false);
  bool IsRunning() const { return active_ != nullptr; }
  const std::string& ActiveName() const { return active_name_; }
  Command* Active() { return active_.get(); }
  void Cancel();
  void RepeatLast();

  // Input from the UI while a command runs.
  void FeedPoint(kernel::Point3d p);
  void FeedText(const std::string& text);   // typed on the command line
  void FeedEnter();
  void FeedObjects(const std::vector<ObjectId>& ids);
  void FeedOption(const std::string& name); // clicked an option
  void FeedHover(std::optional<kernel::Point3d> p);

  // What the command line should show.
  std::string Prompt() const;
  Want CurrentWant() const;
  const std::vector<OptionSpec>* CurrentOptions() const;

  // ---- history -------------------------------------------------------------
  void Print(const std::string& line);
  const std::deque<std::string>& History() const { return history_; }
  const std::vector<std::string>& RecentCommands() const { return recent_; }
  void ClearHistory() { history_.clear(); }

  // Point state shared with viewports (ortho base, relative coordinates).
  std::optional<kernel::Point3d> LastPoint() const { return last_point_; }
  void SetLastPoint(std::optional<kernel::Point3d> p) { last_point_ = p; }
  std::optional<kernel::Point3d> HoverPoint() const { return hover_point_; }
  bool ScriptMode() const { return script_mode_; }

  // Preview geometry for the active command.
  std::vector<float>& PreviewLines() { return preview_lines_; }
  std::vector<float>& PreviewPoints() { return preview_points_; }
  void ClearPreview() { preview_lines_.clear(); preview_points_.clear(); }

  CommandCatalog& Catalog() { return catalog_; }
  // Extra tokens typed after a command name ("Save file.3dm", "SelID 4"):
  // a command may consume them in Begin() instead of prompting.
  std::deque<std::string>& PendingInputs() { return pending_inputs_; }
  std::optional<std::string> TakePendingInput() {
    if (pending_inputs_.empty()) return std::nullopt;
    std::string t = pending_inputs_.front();
    pending_inputs_.pop_front();
    return t;
  }

 private:
  void StartPendingInputs();
  void HandleCommandException(const std::string& what);
  void AfterCallback();
  bool TryParsePoint(const std::string& text, kernel::Point3d& out);
  bool TryOption(const std::string& text);
  std::string ResolveName(const std::string& typed) const;

  Application& app_;
  Document& doc_;
  CommandCatalog& catalog_;
  std::map<std::string, RegisteredCommand> registry_;  // lowercase -> entry
  std::map<std::string, std::string> aliases_;         // lowercase alias -> command
  std::unique_ptr<Command> active_;
  std::string active_name_;
  bool script_mode_ = false;
  std::deque<std::string> pending_inputs_;  // extra tokens from a macro line
  std::deque<std::string> history_;
  std::vector<std::string> recent_;
  std::string last_command_;
  std::optional<kernel::Point3d> last_point_;
  std::optional<kernel::Point3d> hover_point_;
  std::vector<float> preview_lines_;
  std::vector<float> preview_points_;
};

}  // namespace dino8::app
