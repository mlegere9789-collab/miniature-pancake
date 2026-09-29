// Plug-in marketplace install: installs a MarketplaceEntry (see
// plugins/MarketplaceIndex.h for the index format/parsing, kept dependency-
// free so it can be unit-tested on its own) by fetching or copying its
// shared library into <config>/plugins and loading it through
// plugins::Manager - the same install target PackageManager already uses
// (see PluginManager.h/.cpp), so an installed plug-in shows up in the
// Plug-in Manager panel exactly like one dropped in by hand.
#pragma once

#include <string>
#include <vector>

#include "plugins/MarketplaceIndex.h"

namespace dino8::app { class Application; }

namespace dino8::plugins {

// Installs one entry: resolves its library (download or bundled copy),
// verifies sha256 when the entry supplies one, copies it into
// <config>/plugins, and loads it through plugins::Manager::Get().LoadFile.
// `exe_dir` grounds a bundled_path entry - pass app.ExeDir(). Returns false
// (with `error` set) on any failure; nothing is left half-installed on the
// copy/verify path (a failed download or hash check never reaches
// <config>/plugins).
bool InstallEntry(app::Application& app, const MarketplaceEntry& entry, const std::string& exe_dir,
                   std::string& error);

// Holds the last index loaded by the marketplace panel/commands, so the
// panel and the PluginMarketplace* commands share one view of it - the same
// pattern as plugins::Manager::Get().
class Marketplace {
 public:
  static Marketplace& Get();

  bool LoadFrom(const std::string& source, std::string& error);
  const MarketplaceIndex& Index() const { return index_; }
  const std::string& Source() const { return source_; }

  // Looks `id` up in the currently loaded index and installs it.
  bool InstallById(app::Application& app, const std::string& id, std::string& error);

  // Matches `entry.name` (case-insensitively) against the plug-ins
  // plugins::Manager::Get() has actually loaded, and reports the loaded
  // one's version and update status if found. Returns false (out left
  // untouched) when no loaded plug-in has that name.
  bool FindInstalled(const MarketplaceEntry& entry, std::string& installed_version, UpdateStatus& status) const;

  // Every entry in the currently loaded index that FindInstalled matches to
  // a loaded plug-in with UpdateStatus::UpdateAvailable - i.e. what's
  // actually loaded is older than what the index offers.
  struct PluginUpdate {
    std::string id, name, installed_version, available_version;
  };
  std::vector<PluginUpdate> CheckForUpdates() const;

 private:
  MarketplaceIndex index_;
  std::string source_;
};

}  // namespace dino8::plugins
