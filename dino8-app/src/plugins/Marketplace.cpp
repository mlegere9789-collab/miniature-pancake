#include "plugins/Marketplace.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "app/Application.h"
#include "app/Settings.h"
#include "dino8_plugin.h"
#include "plugins/PluginManager.h"
#include "util/Sha256.h"

namespace dino8::plugins {

namespace fs = std::filesystem;

namespace {

#if defined(_WIN32)
const char* kLibExt = ".dll";
#elif defined(__APPLE__)
const char* kLibExt = ".dylib";
#else
const char* kLibExt = ".so";
#endif

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}

std::string DefaultFilenameFromUrl(const std::string& url) {
  const size_t slash = url.find_last_of('/');
  std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
  // Strip a query string, if any, so "plugin.so?token=..." installs as
  // "plugin.so" rather than a filename dlopen would never look twice at.
  const size_t q = name.find('?');
  if (q != std::string::npos) name = name.substr(0, q);
  return name.empty() ? ("plugin" + std::string(kLibExt)) : name;
}

// An index entry's library_filename is meant to be a plain filename (e.g.
// "hellodino.so"), but it comes straight from the loaded index - which can
// be an arbitrary http(s) URL via PluginMarketplaceIndex/the marketplace
// panel. Without this check a malicious index could set library_filename to
// an absolute path or a "../"-relative one and make InstallEntry's
// fs::path(dest_dir) / filename land (and overwrite) any file the process
// can write, entirely outside <config>/plugins.
bool IsPlainFilename(const std::string& name) {
  if (name.empty() || name == "." || name == "..") return false;
  if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
  return fs::path(name).is_relative();
}

const MarketplaceEntry* FindEntryById(const MarketplaceIndex& index, const std::string& id) {
  for (const MarketplaceEntry& e : index.plugins)
    if (e.id == id) return &e;
  return nullptr;
}

// The exact <config>/plugins filename/path InstallEntry copies `entry` to -
// factored out of InstallEntry so UninstallById can find (Manager::Unload
// matches by exact path) and remove precisely the file it put there, never
// a same-named plug-in loaded from anywhere else.
std::string DestFilename(const MarketplaceEntry& entry) {
  if (!entry.library_filename.empty()) return entry.library_filename;
  if (!entry.bundled_path.empty()) return fs::path(entry.bundled_path + kLibExt).filename().string();
  return DefaultFilenameFromUrl(entry.download_url);
}

std::string DestPath(const MarketplaceEntry& entry) {
  return (fs::path(app::ConfigDirectory()) / "plugins" / DestFilename(entry)).string();
}

bool IsLoadedAt(const std::string& path) {
  for (const LoadedPlugin& p : Manager::Get().Plugins())
    if (p.loaded_ok && p.path == path) return true;
  return false;
}

}  // namespace

bool InstallEntry(app::Application& app, const MarketplaceEntry& entry, const std::string& exe_dir, std::string& error) {
  if (entry.download_url.empty() && entry.bundled_path.empty()) {
    error = entry.name + ": entry has neither download_url nor bundled_path";
    return false;
  }
  const Compatibility compat = CheckCompatibility(entry, DINO8_VERSION);
  if (compat == Compatibility::ApiTooNew) {
    error = entry.name + " needs plug-in API v" + std::to_string(entry.api_version) +
            ", this build only supports up to v" + std::to_string(DINO8_PLUGIN_API_VERSION);
    return false;
  }
  if (compat == Compatibility::AppTooOld) {
    error = entry.name + " needs Dino 8 " + entry.min_app_version + " or newer, this build is " DINO8_VERSION;
    return false;
  }

  std::string source_path;
  bool source_is_temp = false;
  if (!entry.bundled_path.empty()) {
    source_path = (fs::path(exe_dir) / (entry.bundled_path + kLibExt)).string();
    std::error_code ec;
    if (!fs::exists(source_path, ec)) {
      error = entry.name + ": bundled plug-in not found at " + source_path + " (build it first - see docs/PLUGIN_SDK.md)";
      return false;
    }
  } else {
    if (!FetchUrlToTempFile(entry.download_url, source_path, error)) return false;
    source_is_temp = true;
    if (!entry.sha256.empty()) {
      std::string got, hash_error;
      if (!util::Sha256HexOfFile(source_path, got, hash_error)) {
        error = entry.name + ": " + hash_error;
        std::error_code ec;
        fs::remove(source_path, ec);
        return false;
      }
      if (!EqualsIgnoreCase(got, entry.sha256)) {
        error = entry.name + ": sha256 mismatch (index says " + entry.sha256 + ", downloaded file is " + got +
                ") - refusing to install";
        std::error_code ec;
        fs::remove(source_path, ec);
        return false;
      }
    }
  }

  if (!entry.library_filename.empty() && !IsPlainFilename(entry.library_filename)) {
    error = entry.name + ": library_filename \"" + entry.library_filename +
            "\" is not a plain filename - refusing to install outside <config>/plugins";
    if (source_is_temp) { std::error_code rm_ec; fs::remove(source_path, rm_ec); }
    return false;
  }

  const std::string dest_dir = app::ConfigDirectory() + "/plugins";
  std::error_code ec;
  fs::create_directories(dest_dir, ec);
  const std::string dest_path = DestPath(entry);
  // dest_path can already be dlopen'd by this same process - e.g. auto-loaded
  // from <config>/plugins at startup, or installed earlier this session - in
  // which case overwriting its backing file out from under the still-mapped
  // library corrupts the running process (observed as a crash on the very
  // next plug-in load). Unload it first so the file is safe to replace.
  Manager::Get().Unload(dest_path);
  fs::copy_file(source_path, dest_path, fs::copy_options::overwrite_existing, ec);
  if (source_is_temp) fs::remove(source_path, ec);
  if (ec) {
    error = entry.name + ": could not copy the plug-in library to " + dest_path + ": " + ec.message();
    return false;
  }

  return Manager::Get().LoadFile(app, dest_path, error);
}

Marketplace& Marketplace::Get() {
  static Marketplace m;
  return m;
}

bool Marketplace::LoadFrom(const std::string& source, std::string& error) {
  MarketplaceIndex idx;
  if (!LoadIndexAuto(source, idx, error)) return false;
  index_ = std::move(idx);
  source_ = source;
  return true;
}

bool Marketplace::InstallById(app::Application& app, const std::string& id, std::string& error) {
  std::vector<std::string> chain;
  return InstallByIdChecked(app, id, chain, error);
}

bool Marketplace::InstallByIdChecked(app::Application& app, const std::string& id, std::vector<std::string>& chain,
                                      std::string& error) {
  const MarketplaceEntry* entry = FindEntryById(index_, id);
  if (!entry) {
    error = "no plugin with id \"" + id + "\" in the loaded index (" + std::to_string(index_.plugins.size()) + " entries)";
    return false;
  }
  if (std::find(chain.begin(), chain.end(), id) != chain.end()) {
    error = entry->name + " (" + id + ") is part of a circular dependency chain";
    return false;
  }
  chain.push_back(id);

  for (const std::string& dep_id : entry->dependencies) {
    const MarketplaceEntry* dep = FindEntryById(index_, dep_id);
    if (!dep) {
      error = entry->name + " (" + id + ") requires plug-in \"" + dep_id + "\", which is not in the loaded index";
      return false;
    }
    std::string installed_version;
    UpdateStatus status;
    if (FindInstalled(*dep, installed_version, status)) continue;  // already satisfied
    if (!InstallByIdChecked(app, dep_id, chain, error)) return false;
  }

  return InstallEntry(app, *entry, app.ExeDir(), error);
}

bool Marketplace::UninstallById(const std::string& id, std::vector<std::string>& removed, std::string& error) {
  std::vector<std::string> chain;
  return UninstallByIdChecked(id, chain, removed, error);
}

bool Marketplace::UninstallByIdChecked(const std::string& id, std::vector<std::string>& chain,
                                        std::vector<std::string>& removed, std::string& error) {
  const MarketplaceEntry* entry = FindEntryById(index_, id);
  if (!entry) {
    error = "no plugin with id \"" + id + "\" in the loaded index (" + std::to_string(index_.plugins.size()) + " entries)";
    return false;
  }
  if (std::find(chain.begin(), chain.end(), id) != chain.end()) {
    error = entry->name + " (" + id + ") is part of a circular dependency chain";
    return false;
  }
  chain.push_back(id);

  const std::string dest_path = DestPath(*entry);
  if (!Manager::Get().Unload(dest_path)) {
    error = entry->name + " (" + id + ") is not currently installed via the marketplace (nothing loaded from " + dest_path + ")";
    return false;
  }
  std::error_code ec;
  fs::remove(dest_path, ec);
  removed.push_back(id);

  // Cascade: a dependency that nothing still-installed needs any more comes
  // out too - but only if the marketplace is actually the one that put it
  // there (never a sample plug-in that's merely auto-loaded from next to
  // the executable). A dependency that fails to cascade (e.g. its own
  // sub-dependency forms a cycle) is left installed; that failure isn't
  // this call's own, so it doesn't fail the whole uninstall.
  for (const std::string& dep_id : entry->dependencies) {
    if (IsDependencyStillNeeded(dep_id)) continue;
    const MarketplaceEntry* dep_entry = FindEntryById(index_, dep_id);
    if (!dep_entry || !IsLoadedAt(DestPath(*dep_entry))) continue;
    std::string dep_error;
    UninstallByIdChecked(dep_id, chain, removed, dep_error);
  }

  return true;
}

bool Marketplace::IsDependencyStillNeeded(const std::string& dep_id) const {
  for (const MarketplaceEntry& e : index_.plugins) {
    if (e.id == dep_id) continue;
    if (std::find(e.dependencies.begin(), e.dependencies.end(), dep_id) == e.dependencies.end()) continue;
    if (IsLoadedAt(DestPath(e))) return true;
  }
  return false;
}

bool Marketplace::FindInstalled(const MarketplaceEntry& entry, std::string& installed_version, UpdateStatus& status) const {
  for (const LoadedPlugin& p : Manager::Get().Plugins()) {
    if (!p.loaded_ok || !EqualsIgnoreCase(p.name, entry.name)) continue;
    installed_version = p.version;
    status = CheckForUpdate(p.version, entry.version);
    return true;
  }
  return false;
}

std::vector<Marketplace::PluginUpdate> Marketplace::CheckForUpdates() const {
  std::vector<PluginUpdate> updates;
  for (const MarketplaceEntry& e : index_.plugins) {
    std::string installed_version;
    UpdateStatus status;
    if (FindInstalled(e, installed_version, status) && status == UpdateStatus::UpdateAvailable) {
      updates.push_back({e.id, e.name, installed_version, e.version});
    }
  }
  return updates;
}

}  // namespace dino8::plugins
