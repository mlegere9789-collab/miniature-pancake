#include "app/MacroLibrary.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace dino8::app {

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

bool IdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool IdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Finds the next placeholder at or after `from`. Returns false at the end.
// On success `begin` is the index of '{', `end` one past '}', `name` the text between.
bool NextPlaceholder(const std::string& s, size_t from, size_t& begin, size_t& end, std::string& name) {
  for (size_t i = from; i < s.size(); ++i) {
    if (s[i] != '{') continue;
    if (i > 0 && s[i - 1] == '\\') continue;  // "\{" is a literal brace
    if (i + 1 >= s.size() || !IdentStart(s[i + 1])) continue;
    size_t j = i + 1;
    while (j < s.size() && IdentChar(s[j])) ++j;
    if (j >= s.size() || s[j] != '}') continue;
    begin = i;
    end = j + 1;
    name = s.substr(i + 1, j - i - 1);
    return true;
  }
  return false;
}

}  // namespace

std::int64_t MacroNowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string FormatMacroTime(std::int64_t ms) {
  std::time_t secs = static_cast<std::time_t>(ms / 1000);
  int frac = static_cast<int>(ms % 1000);
  if (frac < 0) { frac += 1000; --secs; }
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &secs);
#else
  gmtime_r(&secs, &tm);
#endif
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, frac);
  return buf;
}

bool ValidMacroName(const std::string& name, std::string* why) {
  auto bad = [&](const char* w) { if (why) *why = w; return false; };
  if (name.empty()) return bad("the name is empty");
  if (name.size() > 80) return bad("the name is longer than 80 characters");
  if (name == "*") return bad("'*' is reserved for 'every macro'");
  if (std::isspace(static_cast<unsigned char>(name.front())) || std::isspace(static_cast<unsigned char>(name.back()))) return bad("the name has leading or trailing blanks");
  for (char c : name) if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return bad("the name has a control character");
  return true;
}

bool ValidParamName(const std::string& name) {
  if (name.empty() || !IdentStart(name[0])) return false;
  for (char c : name) if (!IdentChar(c)) return false;
  return true;
}

std::vector<std::string> ExtractPlaceholders(const std::string& step) {
  std::vector<std::string> out;
  size_t pos = 0, b, e;
  std::string n;
  while (NextPlaceholder(step, pos, b, e, n)) {
    if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    pos = e;
  }
  return out;
}

std::vector<std::string> MacroPlaceholders(const MacroObject& m) {
  std::vector<std::string> out;
  for (const std::string& s : m.steps)
    for (const std::string& n : ExtractPlaceholders(s))
      if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
  return out;
}

std::vector<std::string> SyncMacroParams(MacroObject& m) {
  std::vector<std::string> added;
  for (const std::string& n : MacroPlaceholders(m)) {
    const bool known = std::any_of(m.params.begin(), m.params.end(), [&](const MacroParam& p) { return p.name == n; });
    if (known) continue;
    MacroParam p;
    p.name = n;
    m.params.push_back(p);
    added.push_back(n);
  }
  return added;
}

bool SubstituteStep(const std::string& step, const std::map<std::string, std::string>& values, std::string& out, std::string& missing) {
  out.clear();
  size_t pos = 0, b, e;
  std::string n;
  while (NextPlaceholder(step, pos, b, e, n)) {
    out.append(step, pos, b - pos);
    const auto it = values.find(n);
    if (it == values.end()) { missing = n; return false; }
    out += it->second;
    pos = e;
  }
  out.append(step, pos, std::string::npos);
  // "\{" -> "{" (the escape only exists to keep a brace from being a placeholder).
  std::string unesc;
  for (size_t i = 0; i < out.size(); ++i) {
    if (out[i] == '\\' && i + 1 < out.size() && out[i + 1] == '{') continue;
    unesc += out[i];
  }
  out = unesc;
  return true;
}

std::string UnescapeMacroArg(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'q') { out += '"'; ++i; }
    else out += s[i];
  }
  return out;
}

std::vector<std::string> MacroStepsFromText(const std::string& text) {
  std::vector<std::string> steps;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    line = Trim(line);
    if (line.empty()) continue;
    if (line.front() == '=') { steps.push_back(line); continue; }  // Lua: ';' is Lua syntax
    std::istringstream parts(line);
    std::string part;
    while (std::getline(parts, part, ';')) {
      part = Trim(part);
      if (!part.empty()) steps.push_back(part);
    }
  }
  return steps;
}

std::string MacroTextFromSteps(const std::vector<std::string>& steps) {
  std::string out;
  for (const std::string& s : steps) out += s + "\n";
  return out;
}

MacroObject* MacroLibrary::Find(const std::string& name) {
  const std::string l = Lower(name);
  for (MacroObject& m : macros_) if (Lower(m.name) == l) return &m;
  return nullptr;
}

const MacroObject* MacroLibrary::Find(const std::string& name) const {
  const std::string l = Lower(name);
  for (const MacroObject& m : macros_) if (Lower(m.name) == l) return &m;
  return nullptr;
}

std::vector<std::string> MacroLibrary::Names() const {
  std::vector<std::string> out;
  for (const MacroObject& m : macros_) out.push_back(m.name);
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    const std::string la = Lower(a), lb = Lower(b);
    return la != lb ? la < lb : a < b;
  });
  return out;
}

MacroObject* MacroLibrary::Create(const std::string& name, std::vector<std::string> steps, const std::string& description) {
  if (!ValidMacroName(name) || Find(name)) return nullptr;
  MacroObject m;
  m.name = name;
  m.description = description;
  m.steps = std::move(steps);
  m.created_ms = m.modified_ms = MacroNowMs();
  SyncMacroParams(m);
  macros_.push_back(std::move(m));
  return &macros_.back();
}

void MacroLibrary::Touch(MacroObject& m) {
  m.modified_ms = std::max(MacroNowMs(), m.modified_ms + 1);
}

bool MacroLibrary::Erase(const std::string& name) {
  const std::string l = Lower(name);
  for (auto it = macros_.begin(); it != macros_.end(); ++it) {
    if (Lower(it->name) == l) { macros_.erase(it); return true; }
  }
  return false;
}

std::string MacroJsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char b[8];
          std::snprintf(b, sizeof(b), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += b;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string MacroLibrary::MacroToJson(const MacroObject& m, int indent) {
  const std::string pad(static_cast<size_t>(indent), ' ');
  const std::string pad2 = pad + "  ";
  auto q = [](const std::string& s) { return "\"" + MacroJsonEscape(s) + "\""; };
  std::ostringstream o;
  o << pad << "{\n";
  o << pad2 << "\"name\": " << q(m.name) << ",\n";
  o << pad2 << "\"description\": " << q(m.description) << ",\n";
  o << pad2 << "\"tags\": [";
  for (size_t i = 0; i < m.tags.size(); ++i) o << (i ? ", " : "") << q(m.tags[i]);
  o << "],\n";
  o << pad2 << "\"parameters\": [";
  for (size_t i = 0; i < m.params.size(); ++i) {
    const MacroParam& p = m.params[i];
    o << (i ? ", " : "") << "{\"name\": " << q(p.name);
    if (p.has_default) o << ", \"default\": " << q(p.default_value);
    o << ", \"prompt\": " << q(p.prompt) << "}";
  }
  o << "],\n";
  o << pad2 << "\"steps\": [";
  for (size_t i = 0; i < m.steps.size(); ++i) o << (i ? ",\n" : "\n") << pad2 << "  " << q(m.steps[i]);
  if (!m.steps.empty()) o << "\n" << pad2;
  o << "],\n";
  o << pad2 << "\"created\": " << q(FormatMacroTime(m.created_ms)) << ",\n";
  o << pad2 << "\"modified\": " << q(FormatMacroTime(m.modified_ms)) << ",\n";
  o << pad2 << "\"created_ms\": " << m.created_ms << ",\n";
  o << pad2 << "\"modified_ms\": " << m.modified_ms << "\n";
  o << pad << "}";
  return o.str();
}

std::string MacroLibrary::ToJson(const std::vector<std::string>& only_names, bool for_export) const {
  std::ostringstream o;
  o << "{\n";
  o << "  \"format\": \"" << kMacroFormatName << "\",\n";
  o << "  \"version\": " << kMacroFormatVersion << ",\n";
  if (for_export) o << "  \"exported\": \"" << FormatMacroTime(MacroNowMs()) << "\",\n";
  o << "  \"macros\": [";
  bool first = true;
  for (const std::string& n : Names()) {
    if (!only_names.empty() && std::find(only_names.begin(), only_names.end(), n) == only_names.end()) continue;
    const MacroObject* m = Find(n);
    if (!m) continue;
    o << (first ? "\n" : ",\n") << MacroToJson(*m, 4);
    first = false;
  }
  if (!first) o << "\n  ";
  o << "]\n}";
  return o.str();
}

bool MacroLibrary::FromJson(const json::Value& root, std::vector<MacroObject>& out, std::string& error) {
  if (!root.IsObject()) { error = "not a JSON object"; return false; }
  if (root["format"].AsString() != kMacroFormatName) {
    error = std::string("not a Dino 8 macro library file (\"format\" is not \"") + kMacroFormatName + "\")";
    return false;
  }
  const json::Value& ver = root["version"];
  if (ver.type != json::Value::Type::Number) { error = "missing \"version\""; return false; }
  const int version = static_cast<int>(ver.number);
  if (version > kMacroFormatVersion) {
    error = "written by a newer Dino 8 (macro format version " + std::to_string(version) + "; this build reads up to " +
            std::to_string(kMacroFormatVersion) + ")";
    return false;
  }
  if (version < 1) { error = "unsupported macro format version " + std::to_string(version); return false; }
  const json::Value& list = root["macros"];
  if (!list.IsArray()) { error = "missing \"macros\" array"; return false; }
  std::vector<MacroObject> parsed;
  for (size_t i = 0; i < list.Size(); ++i) {
    const json::Value& jm = list[i];
    if (!jm.IsObject() || !jm["name"].IsString()) { error = "macro #" + std::to_string(i + 1) + " has no \"name\""; return false; }
    MacroObject m;
    m.name = jm["name"].AsString();
    std::string why;
    if (!ValidMacroName(m.name, &why)) { error = "macro #" + std::to_string(i + 1) + ": " + why; return false; }
    m.description = jm["description"].AsString();
    const json::Value& tags = jm["tags"];
    for (size_t k = 0; tags.IsArray() && k < tags.Size(); ++k) if (tags[k].IsString()) m.tags.push_back(tags[k].AsString());
    const json::Value& params = jm["parameters"];
    for (size_t k = 0; params.IsArray() && k < params.Size(); ++k) {
      const json::Value& jp = params[k];
      if (!jp.IsObject() || !ValidParamName(jp["name"].AsString())) { error = "macro '" + m.name + "': parameter #" + std::to_string(k + 1) + " has no valid \"name\""; return false; }
      MacroParam p;
      p.name = jp["name"].AsString();
      if (jp["default"].IsString()) { p.has_default = true; p.default_value = jp["default"].AsString(); }
      p.prompt = jp["prompt"].AsString();
      m.params.push_back(p);
    }
    const json::Value& steps = jm["steps"];
    if (!steps.IsArray()) { error = "macro '" + m.name + "' has no \"steps\" array"; return false; }
    for (size_t k = 0; k < steps.Size(); ++k) {
      if (!steps[k].IsString()) { error = "macro '" + m.name + "': step #" + std::to_string(k + 1) + " is not a string"; return false; }
      m.steps.push_back(steps[k].AsString());
    }
    const json::Value& cm = jm["created_ms"];
    const json::Value& mm = jm["modified_ms"];
    m.created_ms = cm.type == json::Value::Type::Number ? static_cast<std::int64_t>(cm.number) : MacroNowMs();
    m.modified_ms = mm.type == json::Value::Type::Number ? static_cast<std::int64_t>(mm.number) : m.created_ms;
    SyncMacroParams(m);
    parsed.push_back(std::move(m));
  }
  for (MacroObject& m : parsed) out.push_back(std::move(m));
  return true;
}

}  // namespace dino8::app
