#include "commands/CommandCatalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "util/json_mini.h"

namespace dino8::app {

std::string ToLower(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool CommandCatalog::Load(const std::vector<std::string>& candidate_paths, std::string& error) {
  for (const std::string& path : candidate_paths) {
    std::ifstream in(path, std::ios::binary);
    if (!in) continue;
    std::stringstream buffer;
    buffer << in.rdbuf();
    json::Value root;
    if (!json::Parse(buffer.str(), root, error)) {
      error = path + ": " + error;
      return false;
    }
    commands_.clear();
    index_.clear();
    for (size_t i = 0; i < root.Size(); ++i) {
      const json::Value& e = root[i];
      CommandInfo info;
      info.name = e["name"].AsString();
      if (info.name.empty()) continue;
      info.description = e["description"].AsString();
      info.toolbars = e["toolbars"].AsString();
      info.menu = e["menu"].AsString();
      info.help = e["help"].AsString();
      const json::Value& opts = e["options"];
      for (size_t k = 0; k < opts.Size(); ++k) info.options.push_back(opts[k].AsString());
      index_[ToLower(info.name)] = commands_.size();
      commands_.push_back(std::move(info));
    }
    loaded_from_ = path;
    return !commands_.empty();
  }
  error = "commands.json not found in any candidate location";
  return false;
}

const CommandInfo* CommandCatalog::Find(const std::string& name) const {
  const auto it = index_.find(ToLower(name));
  return it == index_.end() ? nullptr : &commands_[it->second];
}

std::vector<const CommandInfo*> CommandCatalog::WithPrefix(const std::string& prefix, size_t limit) const {
  std::vector<const CommandInfo*> out;
  const std::string p = ToLower(prefix);
  for (const CommandInfo& c : commands_) {
    if (ToLower(c.name).compare(0, p.size(), p) == 0) {
      out.push_back(&c);
      if (out.size() >= limit) break;
    }
  }
  return out;
}

std::vector<const CommandInfo*> CommandCatalog::Search(const std::string& text) const {
  std::vector<const CommandInfo*> out;
  const std::string t = ToLower(text);
  for (const CommandInfo& c : commands_) {
    if (t.empty() || ToLower(c.name).find(t) != std::string::npos ||
        ToLower(c.description).find(t) != std::string::npos) {
      out.push_back(&c);
    }
  }
  return out;
}

}  // namespace dino8::app
