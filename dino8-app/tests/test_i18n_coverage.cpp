// Key-coverage test for the i18n language tables (data/i18n/*.json, loaded
// by src/i18n/I18n.cpp). English (en.json) is the reference key set: every
// other hand-translated language must define every key en.json defines, so
// that Tr()'s English fallback (see I18n.cpp) is only ever exercised by a
// key nobody has translated yet, not by a language file that quietly fell
// behind as new UI-chrome keys were added to en.json.
//
// es.json is the one documented exception: it deliberately omits
// "panel.imgui_demo" to give I18nSelfTest (src/commands/cmd_state.cpp) and
// tests/smoke.sh's i18n section a real missing-key fallback to exercise.
// Every other language - fr.json, de.json, ja.json (Japanese), pt.json
// (Portuguese), it.json (Italian), zh.json (Simplified Chinese),
// ko.json (Korean), and ru.json (Russian) - must have zero missing keys.
//
// This is a "did a language quietly drift behind en.json" regression
// guard: en.json gaining new keys (panel.activity_log, panel.block_manager,
// panel.mapping_widget, panel.uv_editor, panel.whats_new, and
// panel.plugin_marketplace) without every *.json file being updated to
// match is exactly the kind of silent gap this test exists to catch.
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "util/json_mini.h"

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

bool LoadObject(const std::string& path, dino8::json::Value& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::stringstream buf;
  buf << in.rdbuf();
  std::string err;
  return dino8::json::Parse(buf.str(), out, err) && out.IsObject();
}

std::set<std::string> StringKeys(const dino8::json::Value& obj) {
  std::set<std::string> keys;
  for (const auto& [key, value] : obj.object) {
    if (key == "_language_name") continue;
    if (value.IsString()) keys.insert(key);
  }
  return keys;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <path-to-data/i18n>\n", argv[0]);
    return 1;
  }
  const std::string dir = argv[1];

  dino8::json::Value en_root;
  Check(LoadObject(dir + "/en.json", en_root), "en.json parses as a JSON object");
  const std::set<std::string> en_keys = StringKeys(en_root);
  {
    char label[128];
    std::snprintf(label, sizeof(label), "en.json defines a non-trivial key set (%zu keys)", en_keys.size());
    Check(en_keys.size() > 100, label);
  }

  struct LangCase {
    const char* code;
    // Keys this language is allowed to be missing, on top of the reference
    // set - empty for every language except the documented es.json gap.
    std::set<std::string> allowed_missing;
  };
  const LangCase cases[] = {
      {"fr", {}},
      {"de", {}},
      {"ja", {}},
      {"pt", {}},
      {"it", {}},
      {"zh", {}},
      {"ko", {}},
      {"ru", {}},
      {"es", {"panel.imgui_demo"}},
  };

  for (const LangCase& c : cases) {
    dino8::json::Value root;
    const std::string path = dir + "/" + c.code + ".json";
    const bool parsed = LoadObject(path, root);
    Check(parsed, std::string(c.code) + ".json parses as a JSON object");
    if (!parsed) continue;

    const std::set<std::string> lang_keys = StringKeys(root);

    std::set<std::string> missing;
    for (const std::string& key : en_keys)
      if (!lang_keys.count(key)) missing.insert(key);

    std::set<std::string> unexpectedly_missing;
    for (const std::string& key : missing)
      if (!c.allowed_missing.count(key)) unexpectedly_missing.insert(key);

    std::string label = std::string(c.code) + ".json covers every en.json key";
    if (!c.allowed_missing.empty()) label += " except the documented deliberate gap";
    label += " (" + std::to_string(unexpectedly_missing.size()) + " unexpected gaps)";
    Check(unexpectedly_missing.empty(), label);

    for (const std::string& key : c.allowed_missing) {
      std::string still_missing_label =
          std::string(c.code) + ".json still deliberately omits '" + key + "' (documented fallback case)";
      Check(missing.count(key) == 1, still_missing_label);
    }

    // No language file should carry a stray key en.json doesn't have -
    // that would mean either a typo (never looked up by Tr()) or a key
    // that was renamed in en.json but not everywhere else.
    std::set<std::string> extra;
    for (const std::string& key : lang_keys)
      if (!en_keys.count(key)) extra.insert(key);
    Check(extra.empty(), std::string(c.code) + ".json defines no keys absent from en.json (" +
                              std::to_string(extra.size()) + " stray keys)");

    const auto& name_value = root["_language_name"];
    Check(name_value.IsString() && !name_value.AsString().empty(),
          std::string(c.code) + ".json has a non-empty _language_name");
  }

  // The headline deliverable: Japanese is a fourth complete, hand-
  // translated language, matching en.json's key set exactly (not just
  // "mostly", the way a machine-generated stub might partially cover it).
  {
    dino8::json::Value ja_root;
    Check(LoadObject(dir + "/ja.json", ja_root), "ja.json exists and parses");
    if (LoadObject(dir + "/ja.json", ja_root)) {
      const std::set<std::string> ja_keys = StringKeys(ja_root);
      char label[160];
      std::snprintf(label, sizeof(label), "ja.json defines exactly en.json's key set (%zu keys, 0 missing, 0 extra)",
                    ja_keys.size());
      Check(ja_keys == en_keys, label);
      Check(ja_root["_language_name"].AsString("") == "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",
            "ja.json's _language_name is the Japanese word for Japanese (\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e)");
    }
  }

  // The headline deliverable: Portuguese is a fifth complete, hand-
  // translated language, matching en.json's key set exactly (not just
  // "mostly", the way a machine-generated stub might partially cover it).
  {
    dino8::json::Value pt_root;
    Check(LoadObject(dir + "/pt.json", pt_root), "pt.json exists and parses");
    if (LoadObject(dir + "/pt.json", pt_root)) {
      const std::set<std::string> pt_keys = StringKeys(pt_root);
      char label[160];
      std::snprintf(label, sizeof(label), "pt.json defines exactly en.json's key set (%zu keys, 0 missing, 0 extra)",
                    pt_keys.size());
      Check(pt_keys == en_keys, label);
      Check(pt_root["_language_name"].AsString("") == "Portugu\xc3\xaas",
            "pt.json's _language_name is the Portuguese word for Portuguese (Portugu\xc3\xaas)");
    }
  }

  // The headline deliverable: Italian is a sixth complete, hand-translated
  // language, matching en.json's key set exactly (not just "mostly", the
  // way a machine-generated stub might partially cover it).
  {
    dino8::json::Value it_root;
    Check(LoadObject(dir + "/it.json", it_root), "it.json exists and parses");
    if (LoadObject(dir + "/it.json", it_root)) {
      const std::set<std::string> it_keys = StringKeys(it_root);
      char label[160];
      std::snprintf(label, sizeof(label), "it.json defines exactly en.json's key set (%zu keys, 0 missing, 0 extra)",
                    it_keys.size());
      Check(it_keys == en_keys, label);
      Check(it_root["_language_name"].AsString("") == "Italiano",
            "it.json's _language_name is the Italian word for Italian (Italiano)");
    }
  }

  // The headline deliverable: Simplified Chinese is a seventh complete,
  // hand-translated language, matching en.json's key set exactly (not just
  // "mostly", the way a machine-generated stub might partially cover it).
  {
    dino8::json::Value zh_root;
    Check(LoadObject(dir + "/zh.json", zh_root), "zh.json exists and parses");
    if (LoadObject(dir + "/zh.json", zh_root)) {
      const std::set<std::string> zh_keys = StringKeys(zh_root);
      char label[160];
      std::snprintf(label, sizeof(label), "zh.json defines exactly en.json's key set (%zu keys, 0 missing, 0 extra)",
                    zh_keys.size());
      Check(zh_keys == en_keys, label);
      Check(zh_root["_language_name"].AsString("") == "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87",
            "zh.json's _language_name is the Chinese phrase for Simplified Chinese "
            "(\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87)");
    }
  }

  // The headline deliverable: Korean is an eighth complete, hand-
  // translated language, matching en.json's key set exactly (not just
  // "mostly", the way a machine-generated stub might partially cover it).
  {
    dino8::json::Value ko_root;
    Check(LoadObject(dir + "/ko.json", ko_root), "ko.json exists and parses");
    if (LoadObject(dir + "/ko.json", ko_root)) {
      const std::set<std::string> ko_keys = StringKeys(ko_root);
      char label[160];
      std::snprintf(label, sizeof(label), "ko.json defines exactly en.json's key set (%zu keys, 0 missing, 0 extra)",
                    ko_keys.size());
      Check(ko_keys == en_keys, label);
      Check(ko_root["_language_name"].AsString("") == "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4",
            "ko.json's _language_name is the Korean word for Korean (\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4)");
    }
  }

  // The headline deliverable: Russian is a ninth complete, hand-
  // translated language, matching en.json's key set exactly (not just
  // "mostly", the way a machine-generated stub might partially cover it).
  {
    dino8::json::Value ru_root;
    Check(LoadObject(dir + "/ru.json", ru_root), "ru.json exists and parses");
    if (LoadObject(dir + "/ru.json", ru_root)) {
      const std::set<std::string> ru_keys = StringKeys(ru_root);
      char label[160];
      std::snprintf(label, sizeof(label), "ru.json defines exactly en.json's key set (%zu keys, 0 missing, 0 extra)",
                    ru_keys.size());
      Check(ru_keys == en_keys, label);
      Check(ru_root["_language_name"].AsString("") ==
                "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9",
            "ru.json's _language_name is the Russian word for Russian "
            "(\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9)");
    }
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
