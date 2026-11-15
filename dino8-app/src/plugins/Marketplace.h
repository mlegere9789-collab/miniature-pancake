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

  // Installs every entry CheckForUpdates() reports right now, one at a time
  // through InstallById - so each resolves its own dependencies exactly like
  // a manual per-row Update click would, and an entry whose dependency was
  // itself just upgraded by an earlier entry in this same batch sees that
  // upgrade. `updated` collects the id of each one that installed
  // successfully, in CheckForUpdates' own order; `failed` collects "id:
  // error" for any that didn't (e.g. a compatibility or dependency failure)
  // - one failure never stops the rest of the batch from being attempted.
  // Returns false if `failed` ends up non-empty; true if every update found
  // (including none at all) installed cleanly.
  bool UpdateAll(app::Application& app, std::vector<std::string>& updated, std::vector<std::string>& failed);

  // Installs every entry in the currently loaded index that FindInstalled
  // does not currently match to a loaded plug-in - i.e. everything the panel
  // would otherwise show "not installed" for - one at a time through
  // InstallById, so a fresh index (or one just switched to) can be brought
  // in wholesale instead of clicking Install once per row. `installed`
  // collects the id of each one that installed successfully, in the loaded
  // index's own order; `failed` collects "id: error" for any that didn't
  // (an incompatible entry, an unresolved dependency, ...) - one failure
  // never stops the rest of the batch from being attempted. The
  // not-yet-installed set is snapshotted up front, the same reason UpdateAll
  // snapshots CheckForUpdates(): installing one entry can pull in another
  // later in this same batch as its own dependency (InstallById resolves
  // dependencies first), and re-checking FindInstalled mid-loop would just
  // make that already-installed pass look like nothing happened rather than
  // the skip it actually is. Returns false if `failed` ends up non-empty;
  // true if every entry found missing (including none at all) installed
  // cleanly.
  bool InstallAll(app::Application& app, std::vector<std::string>& installed, std::vector<std::string>& failed);

  enum class VerifyStatus { Verified, Mismatch, NoHashToCheck, NotInstalled, Error };

  // Recomputes the sha256 of the file currently installed at `id`'s own
  // marketplace destination path (DestPath in Marketplace.cpp - the exact
  // file InstallEntry wrote and UninstallById would remove) and compares it
  // to the loaded index entry's own `sha256` - the same check InstallEntry
  // makes before ever writing that file, re-run against what's on disk right
  // now, so a copy that was corrupted or tampered with after installing is
  // caught without having to reinstall to find out. `detail` is always
  // filled with a human-readable explanation. NoHashToCheck covers a
  // bundled_path entry or one whose index simply doesn't supply a sha256 -
  // that isn't a failure, there's just nothing to compare against.
  VerifyStatus VerifyInstalled(const std::string& id, std::string& detail) const;

  struct VerifyResult {
    std::string id, name, detail;
    VerifyStatus status = VerifyStatus::Error;
  };

  // VerifyInstalled, run once for every entry in the currently loaded index
  // that's actually installed via the marketplace right now (the same scope
  // VerifyInstalled itself requires - anything else would just report
  // NotInstalled) - so checking a whole install doesn't mean clicking
  // Verify, or running PluginMarketplaceVerify, once per id by hand. Order
  // matches the loaded index; an id with nothing installed at its own
  // destination is left out of the result entirely, not reported as
  // NotInstalled - there is nothing to verify there, unlike a direct
  // VerifyInstalled(id) call, which a caller might make about a specific id
  // it expected to be installed.
  std::vector<VerifyResult> VerifyAll() const;

  // True if any entry in the currently loaded index is currently installed
  // via the marketplace (IsLoadedAt(DestPath(entry)) in Marketplace.cpp) -
  // a cheap (no file hashing) check for enabling/disabling a "verify
  // everything" or "uninstall everything" control, since VerifyAll's own
  // per-entry sha256 hashing is too costly to run every UI frame just to
  // decide whether a button should be clickable.
  bool AnyInstalled() const;

  // Uninstalls every entry in the currently loaded index that's currently
  // installed via the marketplace, through the same UninstallById a single
  // row's Uninstall button already uses - so each one cascades its own
  // now-unneeded dependencies exactly like uninstalling it by hand would.
  // `removed` collects every id actually removed (the entries UninstallAll
  // targeted directly, plus whatever each one's own cascade took with it,
  // in the order UninstallById reports them) with no duplicate: an id that
  // an earlier target's cascade already removed is skipped rather than
  // re-attempted (which would otherwise show up as a spurious failure,
  // since it is no longer installed by the time its own turn comes up).
  // `failed` collects "id: error" for a target whose own uninstall call
  // failed outright (not a merely-already-gone-via-cascade id, which is
  // simply skipped, never counted as a failure). Returns false if `failed`
  // ends up non-empty.
  bool UninstallAll(std::vector<std::string>& removed, std::vector<std::string>& failed);

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
