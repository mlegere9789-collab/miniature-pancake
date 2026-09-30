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

  // Looks `id` up in the currently loaded index and installs it, first
  // resolving its manifest's `dependencies` (other ids in the same index,
  // each optionally suffixed "@min_version" - see SplitDependencySpec):
  // one already satisfied by a loaded plug-in whose version meets that
  // minimum (FindInstalled + CompareVersions) is skipped; one that's loaded
  // but older than the minimum is upgraded in place by installing it fresh
  // (recursively resolving its own dependencies first); one that isn't
  // loaded at all is installed the same way. Fails - installing nothing -
  // if a dependency id isn't in the loaded index, the dependency graph
  // cycles back on itself, or the loaded index's own version of a
  // dependency is older than what the minimum requires (a real version
  // conflict - upgrading would still leave the requirement unmet).
  bool InstallById(app::Application& app, const std::string& id, std::string& error);

  // Uninstalls the plug-in InstallEntry put at `id`'s own <config>/plugins
  // destination - the exact file/load an install of that entry would have
  // written, never a sample plug-in auto-loaded from next to the executable
  // or one dropped into <config>/plugins by hand outside the marketplace.
  // Then cascades: any of `id`'s declared `dependencies` that no other
  // still-installed entry in the loaded index still lists as a dependency is
  // uninstalled too, recursively, so removing the top of a chain can clear
  // the whole chain down to whatever's still shared. `removed` collects
  // every id actually uninstalled, the requested one first, then each
  // cascaded dependency in the order it came out. Fails - removing nothing
  // for `id` itself - if `id` isn't in the loaded index or has nothing
  // loaded from its marketplace install path; a dependency that fails to
  // cascade (e.g. it's part of a dependency cycle, or was never installed
  // via the marketplace) is simply left installed rather than failing the
  // whole call. `still_needed_by` is filled (before anything is actually
  // removed) with the display name of every other currently-installed
  // entry in the loaded index that lists `id` itself as a dependency - so
  // a caller can warn the user that uninstalling `id` directly (as opposed
  // to letting it fall out of a cascade) will leave that entry without a
  // dependency it declared, since UninstallById never refuses this the way
  // InstallById refuses an unmet dependency; it only reports it.
  bool UninstallById(const std::string& id, std::vector<std::string>& removed, std::vector<std::string>& still_needed_by,
                      std::string& error);

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
  // `chain` is the sequence of ids currently being resolved (this call's own
  // id last), so a dependency cycle is caught as soon as it repeats one
  // instead of recursing forever.
  bool InstallByIdChecked(app::Application& app, const std::string& id, std::vector<std::string>& chain,
                           std::string& error);
  bool UninstallByIdChecked(const std::string& id, std::vector<std::string>& chain, std::vector<std::string>& removed,
                             std::string& error);
  // Display names of every other entry in the loaded index that lists
  // `dep_id` as a dependency (see SplitDependencySpec - the id half, a
  // version constraint doesn't change who "needs" it) and is itself
  // currently installed - i.e. uninstalling whatever brought `dep_id` in
  // would leave each of these broken.
  std::vector<std::string> DependentsStillInstalled(const std::string& dep_id) const;
  // True if DependentsStillInstalled(dep_id) is non-empty - `dep_id` must
  // stay installed for the cascade in UninstallByIdChecked.
  bool IsDependencyStillNeeded(const std::string& dep_id) const;

  MarketplaceIndex index_;
  std::string source_;
};

}  // namespace dino8::plugins
