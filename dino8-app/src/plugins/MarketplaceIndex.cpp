#include "plugins/MarketplaceIndex.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#else
#include <unistd.h>
#endif

#include "dino8_plugin.h"
#include "util/json_mini.h"

namespace dino8::plugins {

namespace fs = std::filesystem;

namespace {

#if defined(_WIN32)
long CurrentPid() { return static_cast<long>(_getpid()); }
#else
long CurrentPid() { return static_cast<long>(getpid()); }
#endif

bool IsHttpUrl(const std::string& s) { return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0; }

// curl is invoked through std::system rather than a shell-free exec, so the
// URL and destination path must never reach a shell unescaped - this rejects
// anything that could break out of the double-quoted argument, mirroring
// the same quote-absence guard cmd_state.cpp's OpenUrl/FileExplorer already
// use for xdg-open/open (see commands/cmd_state.cpp).
bool HasShellMetacharacters(const std::string& s) {
  return s.find_first_of("\"'`$;|&\n\r<>\\") != std::string::npos;
}

}  // namespace

bool FetchUrlToTempFile(const std::string& url, std::string& out_path, std::string& error) {
  if (!IsHttpUrl(url)) {
    error = "only http:// and https:// URLs are supported, got: " + url;
    return false;
  }
  if (HasShellMetacharacters(url)) {
    error = "URL contains a character that is not allowed (quotes/shell metacharacters): " + url;
    return false;
  }
  std::error_code ec;
  const fs::path tmp_dir = fs::temp_directory_path(ec);
  if (ec) {
    error = "could not find a temp directory: " + ec.message();
    return false;
  }
  static std::atomic<long> s_counter{0};
  const fs::path tmp_file =
      tmp_dir / ("dino8_marketplace_dl_" + std::to_string(CurrentPid()) + "_" + std::to_string(s_counter++));
  const std::string cmd = "curl -fsSL --max-time 30 -o \"" + tmp_file.string() + "\" \"" + url + "\"";
  const int rc = std::system(cmd.c_str());
  if (rc != 0 || !fs::exists(tmp_file, ec) || fs::file_size(tmp_file, ec) == 0) {
    error = "download failed (curl exit code " + std::to_string(rc) + "): " + url;
    fs::remove(tmp_file, ec);
    return false;
  }
  out_path = tmp_file.string();
  return true;
}

Compatibility CheckCompatibility(const MarketplaceEntry& entry) {
  if (entry.api_version <= 0) return Compatibility::Unknown;
  return entry.api_version <= DINO8_PLUGIN_API_VERSION ? Compatibility::Compatible : Compatibility::ApiTooNew;
}

namespace {

std::vector<long> VersionParts(const std::string& v) {
  std::vector<long> out;
  size_t i = 0;
  while (i <= v.size()) {
    const size_t dot = v.find('.', i);
    const std::string part = v.substr(i, dot == std::string::npos ? std::string::npos : dot - i);
    size_t digits = 0;
    while (digits < part.size() && std::isdigit(static_cast<unsigned char>(part[digits]))) ++digits;
    out.push_back(digits > 0 ? std::stol(part.substr(0, digits)) : 0);
    if (dot == std::string::npos) break;
    i = dot + 1;
  }
  return out;
}

}  // namespace

int CompareVersions(const std::string& a, const std::string& b) {
  const std::vector<long> pa = VersionParts(a);
  const std::vector<long> pb = VersionParts(b);
  const size_t n = std::max(pa.size(), pb.size());
  for (size_t i = 0; i < n; ++i) {
    const long va = i < pa.size() ? pa[i] : 0;
    const long vb = i < pb.size() ? pb[i] : 0;
    if (va != vb) return va < vb ? -1 : 1;
  }
  return 0;
}

UpdateStatus CheckForUpdate(const std::string& installed_version, const std::string& available_version) {
  if (installed_version.empty() || available_version.empty()) return UpdateStatus::Unknown;
  return CompareVersions(available_version, installed_version) > 0 ? UpdateStatus::UpdateAvailable : UpdateStatus::UpToDate;
}

bool ParseIndex(const std::string& json_text, MarketplaceIndex& out, std::string& error) {
  json::Value root;
  if (!json::Parse(json_text, root, error)) return false;
  if (!root.IsObject()) {
    error = "index root is not a JSON object";
    return false;
  }
  const json::Value& sv = root["schema_version"];
  const int schema_version = sv.type == json::Value::Type::Number ? static_cast<int>(sv.number) : 0;
  if (schema_version != 1) {
    error = "unsupported schema_version " + std::to_string(schema_version) +
            " (this build reads schema_version 1 - see plugin-index/SCHEMA.md)";
    return false;
  }
  MarketplaceIndex idx;
  idx.schema_version = schema_version;
  idx.index_name = root["index_name"].AsString("Untitled Plugin Index");
  idx.updated = root["updated"].AsString();

  const json::Value& plugins = root["plugins"];
  if (!plugins.IsArray()) {
    error = "index has no \"plugins\" array";
    return false;
  }
  for (size_t i = 0; i < plugins.array.size(); ++i) {
    const json::Value& p = plugins.array[i];
    if (!p.IsObject()) {
      error = "plugins[" + std::to_string(i) + "] is not an object";
      return false;
    }
    MarketplaceEntry e;
    e.id = p["id"].AsString();
    e.name = p["name"].AsString();
    e.version = p["version"].AsString();
    e.description = p["description"].AsString();
    e.author = p["author"].AsString();
    e.homepage = p["homepage"].AsString();
    e.min_app_version = p["min_app_version"].AsString();
    e.download_url = p["download_url"].AsString();
    e.bundled_path = p["bundled_path"].AsString();
    e.sha256 = p["sha256"].AsString();
    e.library_filename = p["library_filename"].AsString();
    const json::Value& av = p["api_version"];
    e.api_version = av.type == json::Value::Type::Number ? static_cast<int>(av.number) : 0;
    const json::Value& tags = p["tags"];
    if (tags.IsArray())
      for (const json::Value& t : tags.array)
        if (t.IsString()) e.tags.push_back(t.string);
    const json::Value& deps = p["dependencies"];
    if (deps.IsArray())
      for (const json::Value& d : deps.array)
        if (d.IsString()) e.dependencies.push_back(d.string);

    const std::string tag = "plugins[" + std::to_string(i) + "]" + (e.id.empty() ? "" : " (" + e.id + ")");
    if (e.id.empty() || e.name.empty() || e.version.empty()) {
      error = tag + " is missing a required field (id/name/version)";
      return false;
    }
    if (e.download_url.empty() && e.bundled_path.empty()) {
      error = tag + " has neither download_url nor bundled_path";
      return false;
    }
    if (!e.download_url.empty() && !e.bundled_path.empty()) {
      error = tag + " has both download_url and bundled_path - exactly one is required";
      return false;
    }
    idx.plugins.push_back(std::move(e));
  }
  out = std::move(idx);
  return true;
}

bool LoadIndexFromFile(const std::string& path, MarketplaceIndex& out, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "could not open " + path;
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ParseIndex(ss.str(), out, error);
}

bool LoadIndexFromUrl(const std::string& url, MarketplaceIndex& out, std::string& error) {
  if (!IsHttpUrl(url)) {
    error = "only http:// and https:// index URLs are supported";
    return false;
  }
  std::string tmp_path;
  if (!FetchUrlToTempFile(url, tmp_path, error)) return false;
  const bool ok = LoadIndexFromFile(tmp_path, out, error);
  std::error_code ec;
  fs::remove(tmp_path, ec);
  return ok;
}

bool LoadIndexAuto(const std::string& source, MarketplaceIndex& out, std::string& error) {
  return IsHttpUrl(source) ? LoadIndexFromUrl(source, out, error) : LoadIndexFromFile(source, out, error);
}

}  // namespace dino8::plugins
