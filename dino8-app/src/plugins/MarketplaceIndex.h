// Plug-in index format: the JSON schema documented in
// plugin-index/SCHEMA.md, parsed and loaded here. Deliberately independent
// of app::Application/plugins::Manager (unlike plugins/Marketplace.h, which
// installs an entry) so it can be unit-tested on its own - see
// tests/test_plugin_marketplace.cpp.
#pragma once

#include <string>
#include <vector>

namespace dino8::plugins {

struct MarketplaceEntry {
  std::string id;           // unique slug, e.g. "hellodino" - used by Install
  std::string name;         // display name, e.g. "HelloDino"
  std::string version;
  std::string description;
  std::string author;
  std::string homepage;     // optional
  std::string min_app_version;  // optional, informational only
  std::vector<std::string> tags;
  int api_version = 0;      // the DINO8_PLUGIN_API_VERSION this plug-in targets

  // Exactly one of these two identifies where the library comes from:
  std::string download_url;  // http:// or https:// - fetched with curl
  std::string bundled_path;  // path relative to the app's own exe directory,
                              // WITHOUT the platform's .so/.dll/.dylib
                              // suffix - for a plug-in already shipped
                              // alongside this build (plugin-index/index.json).

  std::string sha256;            // optional, only meaningful with download_url
  std::string library_filename;  // optional override for the installed file's name
};

struct MarketplaceIndex {
  int schema_version = 0;
  std::string index_name;
  std::string updated;
  std::vector<MarketplaceEntry> plugins;
};

enum class Compatibility { Compatible, ApiTooNew, Unknown };

// Compares entry.api_version against DINO8_PLUGIN_API_VERSION. The plug-in
// ABI is additive (see include/dino8_plugin.h's version-2 comment), so any
// api_version <= this build's is expected to load; higher means this build
// is too old for it.
Compatibility CheckCompatibility(const MarketplaceEntry& entry);

// Compares two dotted-numeric version strings ("1.2.3", "1.10.0", ...)
// component by component as integers - not a lexical string compare, so
// "1.10.0" correctly orders above "1.2.0". A missing trailing component (or
// one whose leading characters aren't digits, e.g. a "-beta" suffix) counts
// as 0. This is a pragmatic dotted-integer comparison, not a full semver
// parser: pre-release/build-metadata tags are not given any special
// ordering. Returns <0 if a<b, 0 if a==b, >0 if a>b.
int CompareVersions(const std::string& a, const std::string& b);

enum class UpdateStatus { UpToDate, UpdateAvailable, Unknown };

// Compares an installed plug-in's version (e.g. LoadedPlugin::version, from
// PluginManager.h) against a marketplace entry's version. Unknown when
// either string is empty, since there's nothing meaningful to compare.
UpdateStatus CheckForUpdate(const std::string& installed_version, const std::string& available_version);

// Parses index JSON already read into memory. Returns false (with `error`
// set to a human-readable message) on malformed JSON, an unsupported
// schema_version, or a plugin entry missing a required field.
bool ParseIndex(const std::string& json_text, MarketplaceIndex& out, std::string& error);

// Reads and parses a local index file.
bool LoadIndexFromFile(const std::string& path, MarketplaceIndex& out, std::string& error);

// Fetches (via a real HTTP GET, see MarketplaceIndex.cpp) and parses a
// remote index. Only http:// and https:// are accepted.
bool LoadIndexFromUrl(const std::string& url, MarketplaceIndex& out, std::string& error);

// Dispatches to LoadIndexFromUrl or LoadIndexFromFile based on `source`'s
// scheme.
bool LoadIndexAuto(const std::string& source, MarketplaceIndex& out, std::string& error);

// Downloads `url` into a freshly created temp file (via the `curl` binary)
// and returns its path in `out_path`. Real network I/O - shared by
// LoadIndexFromUrl and, for a downloaded (non-bundled) entry, by
// plugins::InstallEntry (Marketplace.cpp).
bool FetchUrlToTempFile(const std::string& url, std::string& out_path, std::string& error);

}  // namespace dino8::plugins
