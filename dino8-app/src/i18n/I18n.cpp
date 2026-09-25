#include "i18n/I18n.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "util/json_mini.h"

namespace dino8::i18n {

namespace fs = std::filesystem;

namespace {

// A language table's own display name is stored under this key (e.g.
// "Espanol" in es.json), so the language picker shows names in their own
// language instead of the current UI language. Not looked up by Tr().
constexpr const char* kMetaNameKey = "_language_name";

std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& Tables() {
  static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> tables;
  return tables;
}

std::unordered_map<std::string, std::string>& Names() {
  static std::unordered_map<std::string, std::string> names;
  return names;
}

std::string& ActiveCode() {
  static std::string active = "en";
  return active;
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// A tiny built-in English table so the app has real menu text even if
// data/i18n/en.json can't be found (mirrors CommandCatalog's "keep going
// without the data file" philosophy - i18n must never be a hard dependency).
void SeedBuiltinEnglish() {
  auto& en = Tables()["en"];
  if (en.empty()) en["app.title"] = "Dino 8";
  Names()["en"] = "English";
}

void LoadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return;
  std::stringstream buf;
  buf << in.rdbuf();
  json::Value root;
  std::string err;
  if (!json::Parse(buf.str(), root, err) || !root.IsObject()) return;

  const std::string code = Lower(path.stem().string());
  auto& table = Tables()[code];
  std::string name = code;
  for (const auto& [key, value] : root.object) {
    if (!value.IsString()) continue;
    if (key == kMetaNameKey) { name = value.string; continue; }
    table[key] = value.string;
  }
  Names()[code] = name;
}

}  // namespace

void Init(const std::vector<std::string>& candidate_dirs) {
  SeedBuiltinEnglish();
  for (const std::string& dir : candidate_dirs) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      if (ec) break;
      if (entry.path().extension() == ".json") LoadFile(entry.path());
    }
    // First existing i18n directory wins, same convention as
    // CommandCatalog::Load's candidate-path search.
    if (!ec) break;
  }
}

bool SetLanguage(const std::string& code) {
  const std::string key = Lower(code);
  if (!Tables().count(key)) return false;
  ActiveCode() = key;
  return true;
}

const std::string& CurrentLanguage() { return ActiveCode(); }

const std::string& CurrentLanguageName() {
  const auto it = Names().find(ActiveCode());
  return it != Names().end() ? it->second : ActiveCode();
}

std::vector<LanguageEntry> AvailableLanguages() {
  std::vector<LanguageEntry> out;
  for (const auto& [code, name] : Names()) {
    if (code == "en") continue;
    out.push_back({code, name});
  }
  std::sort(out.begin(), out.end(), [](const LanguageEntry& a, const LanguageEntry& b) { return a.code < b.code; });
  if (Names().count("en")) out.insert(out.begin(), {"en", Names()["en"]});
  return out;
}

const std::string& Tr(const std::string& key) {
  const auto active_it = Tables().find(ActiveCode());
  if (active_it != Tables().end()) {
    const auto it = active_it->second.find(key);
    if (it != active_it->second.end()) return it->second;
  }
  const auto en_it = Tables().find("en");
  if (en_it != Tables().end()) {
    const auto it = en_it->second.find(key);
    if (it != en_it->second.end()) return it->second;
  }
  // Nothing knows this key in any language: echo the key itself so the UI
  // shows *something* legible instead of blanking out, while still making
  // an untranslated string obvious (it won't look like prose).
  return key;
}

}  // namespace dino8::i18n
