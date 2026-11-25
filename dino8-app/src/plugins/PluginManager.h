// Loads Dino 8 plug-ins (shared libraries implementing dino8_plugin.h) from
// disk and keeps track of the commands and Dino Flow nodes they register.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dino8::app { class Application; }

namespace dino8::plugins {

struct LoadedPlugin {
  std::string path;
  std::string name, version, description;
  bool enabled = true;
  bool loaded_ok = false;
  std::string error;
  std::vector<std::string> commands;   // names registered by this plug-in
  std::vector<std::string> flow_nodes; // Dino Flow node names registered
  void* handle = nullptr;              // dlopen/LoadLibrary handle
};

// A local package: a folder with manifest.json plus a shared library and/or
// Lua scripts / .dflow graphs (PackageManager's install format - no network).
struct PackageInfo {
  std::string folder;       // source folder the package was found/installed from
  std::string name, version, author, description;
  bool installed = false;   // already copied into the plug-ins folder
};

// A package installed under <config>/packages/<name>/ (PackageInstall): manifest.json plus its files. Every .lua / .dflow
// script it exposes becomes a command (manifest "commands": [{"name", "script", "help"}], or, without that key, one command per
// script named after the file's stem). Registered at every start-up (Manager::ScanDefaultFolders).
struct InstalledPackage {
  std::string folder;
  std::string name, version, author, description;
  std::vector<std::pair<std::string, std::string>> commands;  // command name -> script file name inside `folder`
};

class Manager {
 public:
  static Manager& Get();

  // Default search locations: <config>/plugins and <exe_dir>/plugins.
  void ScanDefaultFolders(app::Application& app);
  // Loads every shared library in `folder` that isn't already loaded.
  int LoadFolder(app::Application& app, const std::string& folder);
  bool LoadFile(app::Application& app, const std::string& path, std::string& error);
  bool Unload(const std::string& path);
  void SetEnabled(const std::string& path, bool enabled) { for (auto& p : plugins_) if (p.path == path) p.enabled = enabled; }

  const std::vector<LoadedPlugin>& Plugins() const { return plugins_; }

  // PackageManager: local folders (manifest.json + library/scripts) found
  // under the configured package search paths, and the ones already
  // installed (copied into the plug-ins folder).
  std::vector<PackageInfo> ScanPackages(const std::string& search_folder) const;
  bool InstallPackage(app::Application& app, const PackageInfo& pkg, std::string& error);
  // Reads `folder`/manifest.json (or just the folder name when there is none) as a package; false when there is no manifest.json.
  bool ReadPackageManifest(const std::string& folder, PackageInfo& out) const;
  // <config>/packages
  static std::string PackagesDirectory();
  // Packages InstallPackage has copied under PackagesDirectory(), sorted by folder name.
  std::vector<InstalledPackage> InstalledPackages() const;
  // Registers the script commands of every installed package; returns how many were registered. A command named like a
  // built-in is refused (printed), never allowed to shadow it.
  int RegisterPackageCommands(app::Application& app);
  bool RemovePackage(const std::string& name, std::string& error);

  // MigratePlugins: copies plug-ins from an older Dino 8 config folder.
  int MigrateFrom(app::Application& app, const std::string& old_config_dir, std::string& error);

  // Settings persisted alongside app settings.json.
  std::vector<std::string> extra_folders;      // GrasshopperFolders / plug-in search paths
  bool load_one_by_one = false;                // GrasshopperLoadOneByOne
  std::vector<std::string> ignored_plugins;    // GrasshopperIgnorePlugin (by file name)

 private:
  std::vector<LoadedPlugin> plugins_;
};

}  // namespace dino8::plugins
