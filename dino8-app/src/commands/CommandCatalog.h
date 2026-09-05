// The catalog of every Rhino 8 command (name, description, toolbar/menu
// placement, command-line options, full help text), loaded from
// data/commands.json at startup. This is the authoritative list the
// Command List panel, autocomplete, and Help panel are driven from.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace dino8::app {

struct CommandInfo {
  std::string name;
  std::string description;
  std::string toolbars;
  std::string menu;
  std::vector<std::string> options;
  std::string help;
};

class CommandCatalog {
 public:
  // Loads from the first path that exists. Returns false if none loaded.
  bool Load(const std::vector<std::string>& candidate_paths, std::string& error);
  const std::vector<CommandInfo>& All() const { return commands_; }
  const CommandInfo* Find(const std::string& name) const;  // case-insensitive
  // Names starting with `prefix` (case-insensitive), sorted.
  std::vector<const CommandInfo*> WithPrefix(const std::string& prefix, size_t limit = 50) const;
  // Names or descriptions containing `text`.
  std::vector<const CommandInfo*> Search(const std::string& text) const;
  size_t Size() const { return commands_.size(); }
  const std::string& LoadedFrom() const { return loaded_from_; }

 private:
  std::vector<CommandInfo> commands_;
  std::unordered_map<std::string, size_t> index_;  // lowercase name -> index
  std::string loaded_from_;
};

std::string ToLower(const std::string& s);

}  // namespace dino8::app
