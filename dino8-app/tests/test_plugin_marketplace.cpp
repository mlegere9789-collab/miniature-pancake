// Unit test for the plug-in marketplace's dependency-free half
// (src/plugins/MarketplaceIndex.cpp: JSON index parsing, schema
// validation, compatibility check) and src/util/Sha256.cpp (used to verify
// a downloaded plug-in's integrity before InstallEntry ever loads it - see
// src/plugins/Marketplace.cpp).
//
// This intentionally does NOT exercise InstallEntry/Manager::LoadFile -
// those need a live app::Application and pull in the whole kernel/UI, so
// the true end-to-end "load the index, list plugins, install one through
// the in-app command flow" proof lives in tests/plugin_marketplace_script.txt,
// run through the real compiled Dino8 binary by tests/smoke.sh. This test
// is the fast, isolated half: does the index format actually parse
// correctly, including its error paths, and is the hash check correct.
#include <cstdio>
#include <fstream>
#include <sstream>

#include "dino8_plugin.h"
#include "plugins/MarketplaceIndex.h"
#include "util/Sha256.h"

using dino8::plugins::CheckCompatibility;
using dino8::plugins::CheckForUpdate;
using dino8::plugins::Compatibility;
using dino8::plugins::CompareVersions;
using dino8::plugins::LoadIndexFromFile;
using dino8::plugins::MarketplaceEntry;
using dino8::plugins::MarketplaceIndex;
using dino8::plugins::MatchesFilter;
using dino8::plugins::ParseIndex;
using dino8::plugins::SplitDependencySpec;
using dino8::plugins::UpdateStatus;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
}  // namespace

int main(int argc, char** argv) {
  // ---- Sha256: known test vectors (FIPS 180-4 / RFC examples) -----------
  Check(dino8::util::Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "Sha256Hex(\"\") matches the well-known empty-string digest");
  Check(dino8::util::Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "Sha256Hex(\"abc\") matches the FIPS 180-4 test vector");
  Check(dino8::util::Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "Sha256Hex matches the FIPS 180-4 multi-block test vector");
  // A one-byte difference must produce a completely different digest, not
  // (say) a linear/incremental one - i.e. this isn't some trivial checksum.
  Check(dino8::util::Sha256Hex("abc") != dino8::util::Sha256Hex("abd"), "Sha256Hex is sensitive to a single changed byte");

  // ---- ParseIndex: malformed / invalid inputs ----------------------------
  MarketplaceIndex idx;
  std::string error;
  Check(!ParseIndex("{not json", idx, error), "ParseIndex rejects malformed JSON");
  Check(!ParseIndex("[]", idx, error), "ParseIndex rejects a non-object root");
  Check(!ParseIndex("{\"plugins\": []}", idx, error), "ParseIndex rejects a missing schema_version");
  Check(!ParseIndex("{\"schema_version\": 2, \"plugins\": []}", idx, error), "ParseIndex rejects an unsupported schema_version");
  Check(!ParseIndex("{\"schema_version\": 1}", idx, error), "ParseIndex rejects an index with no \"plugins\" array");
  Check(!ParseIndex("{\"schema_version\": 1, \"plugins\": [{\"id\":\"x\",\"name\":\"X\"}]}", idx, error),
        "ParseIndex rejects a plugin entry missing \"version\"");
  Check(!ParseIndex("{\"schema_version\": 1, \"plugins\": [{\"id\":\"x\",\"name\":\"X\",\"version\":\"1.0\"}]}", idx, error),
        "ParseIndex rejects a plugin entry with neither download_url nor bundled_path");
  Check(!ParseIndex(
            "{\"schema_version\": 1, \"plugins\": [{\"id\":\"x\",\"name\":\"X\",\"version\":\"1.0\","
            "\"download_url\":\"https://example.com/x.so\",\"bundled_path\":\"plugins/x\"}]}",
            idx, error),
        "ParseIndex rejects a plugin entry with BOTH download_url and bundled_path");

  // ---- ParseIndex: a real minimal valid index ---------------------------
  const std::string minimal =
      "{\"schema_version\": 1, \"index_name\": \"Test Index\", \"updated\": \"2026-01-01\", \"plugins\": ["
      "{\"id\":\"a\",\"name\":\"A\",\"version\":\"1.0.0\",\"author\":\"Me\",\"api_version\":1,"
      "\"bundled_path\":\"plugins/a\",\"tags\":[\"x\",\"y\"]},"
      "{\"id\":\"b\",\"name\":\"B\",\"version\":\"2.0.0\",\"download_url\":\"https://example.com/b.so\","
      "\"sha256\":\"deadbeef\",\"api_version\":99}"
      "]}";
  Check(ParseIndex(minimal, idx, error), "ParseIndex accepts a valid minimal index (" + error + ")");
  Check(idx.index_name == "Test Index", "ParseIndex reads index_name");
  Check(idx.plugins.size() == 2, "ParseIndex reads both plugin entries");
  if (idx.plugins.size() == 2) {
    Check(idx.plugins[0].id == "a" && idx.plugins[0].bundled_path == "plugins/a", "entry 'a' parsed as a bundled entry");
    Check(idx.plugins[0].tags.size() == 2 && idx.plugins[0].tags[0] == "x" && idx.plugins[0].tags[1] == "y",
          "entry 'a' parsed its tags array in order");
    Check(idx.plugins[1].id == "b" && idx.plugins[1].download_url == "https://example.com/b.so",
          "entry 'b' parsed as a download_url entry");
    Check(idx.plugins[1].sha256 == "deadbeef", "entry 'b' parsed its sha256 field");
  }

  // ---- ParseIndex: dependencies -------------------------------------------
  MarketplaceIndex dep_idx;
  std::string dep_error;
  const std::string with_deps =
      "{\"schema_version\": 1, \"plugins\": ["
      "{\"id\":\"a\",\"name\":\"A\",\"version\":\"1.0.0\",\"bundled_path\":\"plugins/a\"},"
      "{\"id\":\"b\",\"name\":\"B\",\"version\":\"1.0.0\",\"bundled_path\":\"plugins/b\",\"dependencies\":[\"a\"]},"
      "{\"id\":\"c\",\"name\":\"C\",\"version\":\"1.0.0\",\"bundled_path\":\"plugins/c\",\"dependencies\":[\"a\",\"b\"]}"
      "]}";
  Check(ParseIndex(with_deps, dep_idx, dep_error), "ParseIndex accepts an index with \"dependencies\" arrays (" + dep_error + ")");
  Check(dep_idx.plugins.size() == 3, "ParseIndex reads all 3 entries of the dependency-bearing index");
  if (dep_idx.plugins.size() == 3) {
    Check(dep_idx.plugins[0].dependencies.empty(), "entry 'a' has no dependencies (field omitted)");
    Check(dep_idx.plugins[1].dependencies.size() == 1 && dep_idx.plugins[1].dependencies[0] == "a",
          "entry 'b' parsed its single dependency on 'a'");
    Check(dep_idx.plugins[2].dependencies.size() == 2 && dep_idx.plugins[2].dependencies[0] == "a" &&
              dep_idx.plugins[2].dependencies[1] == "b",
          "entry 'c' parsed its two dependencies in order");
  }

  // ---- ParseIndex: version-constrained dependencies ("id@min_version") --
  MarketplaceIndex verdep_idx;
  std::string verdep_error;
  const std::string with_verdeps =
      "{\"schema_version\": 1, \"plugins\": ["
      "{\"id\":\"a\",\"name\":\"A\",\"version\":\"1.0.0\",\"bundled_path\":\"plugins/a\"},"
      "{\"id\":\"b\",\"name\":\"B\",\"version\":\"1.0.0\",\"bundled_path\":\"plugins/b\",\"dependencies\":[\"a@1.2.0\"]}"
      "]}";
  Check(ParseIndex(with_verdeps, verdep_idx, verdep_error), "ParseIndex accepts a dependency with an \"@min_version\" suffix");
  Check(verdep_idx.plugins.size() == 2 && verdep_idx.plugins[1].dependencies.size() == 1 &&
            verdep_idx.plugins[1].dependencies[0] == "a@1.2.0",
        "ParseIndex keeps the \"id@min_version\" spec string verbatim (split lazily by SplitDependencySpec)");

  // ---- SplitDependencySpec ------------------------------------------------
  std::string spec_id, spec_min;
  SplitDependencySpec("meshtools", spec_id, spec_min);
  Check(spec_id == "meshtools" && spec_min.empty(), "SplitDependencySpec: a bare id has no minimum version");
  SplitDependencySpec("meshtools@1.2.0", spec_id, spec_min);
  Check(spec_id == "meshtools" && spec_min == "1.2.0", "SplitDependencySpec: \"id@version\" splits into both parts");
  SplitDependencySpec("meshtools@", spec_id, spec_min);
  Check(spec_id == "meshtools" && spec_min.empty(), "SplitDependencySpec: a trailing bare \"@\" leaves an empty (unconstrained) minimum");

  // ---- MatchesFilter --------------------------------------------------------
  MarketplaceEntry filter_entry;
  filter_entry.id = "meshtools";
  filter_entry.name = "MeshTools";
  filter_entry.author = "Dino 8 Project";
  filter_entry.description = "Mesh cleanup and repair utilities.";
  filter_entry.tags = {"mesh", "repair"};
  Check(MatchesFilter(filter_entry, ""), "MatchesFilter: an empty filter matches every entry");
  Check(MatchesFilter(filter_entry, "mesh"), "MatchesFilter: matches a substring of the name");
  Check(MatchesFilter(filter_entry, "MESH"), "MatchesFilter: matches case-insensitively");
  Check(MatchesFilter(filter_entry, "meshtools"), "MatchesFilter: matches the id");
  Check(MatchesFilter(filter_entry, "Dino 8"), "MatchesFilter: matches the author");
  Check(MatchesFilter(filter_entry, "cleanup"), "MatchesFilter: matches the description");
  Check(MatchesFilter(filter_entry, "repair"), "MatchesFilter: matches a tag");
  Check(!MatchesFilter(filter_entry, "curvetools"), "MatchesFilter: does not match unrelated text");

  // ---- CheckCompatibility ------------------------------------------------
  MarketplaceEntry e;
  e.api_version = 0;
  Check(CheckCompatibility(e) == Compatibility::Unknown, "api_version 0 (absent) is Unknown compatibility");
  e.api_version = DINO8_PLUGIN_API_VERSION;
  Check(CheckCompatibility(e) == Compatibility::Compatible, "api_version == this build's is Compatible");
  e.api_version = 1;
  Check(1 <= DINO8_PLUGIN_API_VERSION, "sanity: this build's API version is >= 1 (version 1 always exists)");
  Check(CheckCompatibility(e) == Compatibility::Compatible, "an older api_version is still Compatible (the ABI is additive)");
  e.api_version = DINO8_PLUGIN_API_VERSION + 1;
  Check(CheckCompatibility(e) == Compatibility::ApiTooNew, "a newer-than-this-build api_version is ApiTooNew");

  // ---- CheckCompatibility: min_app_version (enforced only when the caller
  // passes a running_app_version; omitting it keeps the old "informational
  // only" behavior so every check above, which never passes one, still
  // holds unchanged) --------------------------------------------------------
  MarketplaceEntry old_app;
  old_app.api_version = 1;
  old_app.min_app_version = "1.0.0";
  Check(CheckCompatibility(old_app) == Compatibility::Compatible,
        "min_app_version is not enforced when the caller passes no running_app_version");
  Check(CheckCompatibility(old_app, "0.5.0") == Compatibility::AppTooOld,
        "an entry requiring a newer app version than the running one is AppTooOld");
  Check(CheckCompatibility(old_app, "1.0.0") == Compatibility::Compatible,
        "an app version exactly equal to min_app_version is Compatible, not AppTooOld");
  Check(CheckCompatibility(old_app, "2.0.0") == Compatibility::Compatible,
        "an app version newer than min_app_version is Compatible");
  Check(CheckCompatibility(old_app, "1.9.0") == Compatibility::Compatible,
        "min_app_version compares numerically, not lexically (1.9.0 >= 1.0.0)");
  MarketplaceEntry no_min;
  no_min.api_version = 1;
  Check(CheckCompatibility(no_min, "0.0.1") == Compatibility::Compatible,
        "an entry with no min_app_version is unaffected by running_app_version");
  MarketplaceEntry both_too_new;
  both_too_new.api_version = DINO8_PLUGIN_API_VERSION + 1;
  both_too_new.min_app_version = "1.0.0";
  Check(CheckCompatibility(both_too_new, "0.5.0") == Compatibility::ApiTooNew,
        "ApiTooNew takes priority over AppTooOld when an entry fails both checks");

  // ---- CompareVersions ----------------------------------------------------
  Check(CompareVersions("1.0.0", "1.0.0") == 0, "CompareVersions: equal versions compare equal");
  Check(CompareVersions("1.0.0", "1.0.1") < 0, "CompareVersions: a patch bump compares greater");
  Check(CompareVersions("1.2.0", "1.10.0") < 0, "CompareVersions: numeric, not lexical (1.2.0 < 1.10.0)");
  Check(CompareVersions("2.0.0", "1.9.9") > 0, "CompareVersions: a major bump beats any minor/patch");
  Check(CompareVersions("1.0", "1.0.0") == 0, "CompareVersions: a missing trailing component counts as 0");
  Check(CompareVersions("1.0.0", "1.0.0-beta") == 0, "CompareVersions: a non-numeric suffix component counts as 0");
  Check(CompareVersions("", "") == 0, "CompareVersions: two empty strings compare equal");

  // ---- CheckForUpdate ------------------------------------------------------
  Check(CheckForUpdate("1.0.0", "1.1.0") == UpdateStatus::UpdateAvailable,
        "CheckForUpdate: a newer index version is UpdateAvailable");
  Check(CheckForUpdate("1.1.0", "1.1.0") == UpdateStatus::UpToDate, "CheckForUpdate: matching versions are UpToDate");
  Check(CheckForUpdate("1.2.0", "1.1.0") == UpdateStatus::UpToDate,
        "CheckForUpdate: an installed version newer than the index is UpToDate, not flagged as an update");
  Check(CheckForUpdate("", "1.0.0") == UpdateStatus::Unknown, "CheckForUpdate: an empty installed version is Unknown");
  Check(CheckForUpdate("1.0.0", "") == UpdateStatus::Unknown, "CheckForUpdate: an empty available version is Unknown");
  Check(CheckForUpdate("1.9.0", "1.10.0") == UpdateStatus::UpdateAvailable,
        "CheckForUpdate: 1.10.0 is correctly seen as newer than 1.9.0 (numeric, not lexical)");

  // ---- The real reference index (plugin-index/index.json) ---------------
  // Path is passed on the command line by CMakeLists.txt (an absolute path
  // into the source tree, independent of the test's working directory).
  if (argc > 1) {
    const std::string path = argv[1];
    MarketplaceIndex real_idx;
    std::string real_error;
    Check(LoadIndexFromFile(path, real_idx, real_error), "LoadIndexFromFile parses the real plugin-index/index.json (" + real_error + ")");
    Check(real_idx.plugins.size() == 4, "plugin-index/index.json lists exactly the 4 sample plug-ins (found " +
                                             std::to_string(real_idx.plugins.size()) + ")");
    const char* expect_ids[] = {"hellodino", "meshtools", "curvetools", "analysistools"};
    for (const char* id : expect_ids) {
      bool found = false;
      for (const MarketplaceEntry& p : real_idx.plugins) {
        if (p.id != id) continue;
        found = true;
        Check(!p.name.empty() && !p.version.empty() && !p.author.empty() && !p.description.empty(),
              std::string("entry '") + id + "' has name/version/author/description");
        Check(!p.bundled_path.empty(), std::string("entry '") + id + "' is a bundled_path entry (no network needed to install it)");
        Check(CheckCompatibility(p) == Compatibility::Compatible,
              std::string("entry '") + id + "' is Compatible with this build's plug-in API version");
      }
      Check(found, std::string("plugin-index/index.json contains an entry with id \"") + id + "\"");
    }
  } else {
    std::printf("(skipping the real plugin-index/index.json check - no path given on the command line)\n");
  }

  Check(!LoadIndexFromFile("/no/such/file/dino8_marketplace_test_missing.json", idx, error),
        "LoadIndexFromFile fails cleanly on a nonexistent path");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
