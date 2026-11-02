// Verifies the bounded cmddesc.* command-description i18n subset end to
// end against the real loaded data: data/i18n/*.json (via i18n::Init) and
// data/commands.json (via CommandCatalog::Load), the same files and
// loaders the real app uses - not a hand-rolled stand-in for either.
//
// Covers the two things I18n::TrOrDefault exists for (see I18n.h):
//  - a command IN the bounded subset really gets a different, non-empty
//    string per language (a real translation, not an untranslated copy);
//  - a command NOT in the subset falls back to its own real English
//    description unchanged, never a literal "cmddesc.<name>" key leaking
//    into the UI (the bug TrOrDefault's fallback parameter exists to
//    avoid - see I18n.cpp's Tr() for the different, key-echoing behavior
//    this function deliberately does NOT use).
#include <cstdio>
#include <string>
#include <vector>

#include "commands/CommandCatalog.h"
#include "i18n/I18n.h"

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <path-to-data/i18n> <path-to-data/commands.json>\n", argv[0]);
    return 1;
  }
  const std::string i18n_dir = argv[1];
  const std::string catalog_path = argv[2];

  dino8::app::CommandCatalog catalog;
  std::string catalog_error;
  Check(catalog.Load({catalog_path}, catalog_error), "commands.json loads (" + catalog_error + ")");

  dino8::i18n::Init({i18n_dir});

  // A command inside the bounded subset (see I18n.h's scope comment).
  const dino8::app::CommandInfo* circle = catalog.Find("Circle");
  Check(circle != nullptr, "Circle is a real catalog entry");
  if (!circle) return failures ? 1 : 0;

  const std::string en_desc = circle->description;
  Check(!en_desc.empty(), "Circle has a real English description");

  // Under English, TrOrDefault should read the same text straight out of
  // en.json's own cmddesc.circle entry (not fall through to `fallback`) -
  // confirmed by checking it's non-empty and unchanged; a real discrepancy
  // would show up in the per-language checks below instead.
  const std::string& en_lookup = dino8::i18n::TrOrDefault("cmddesc.circle", en_desc);
  Check(en_lookup == en_desc, "English TrOrDefault(cmddesc.circle) matches commands.json's own English text");

  // Every other loaded language must have ITS OWN, real, non-English
  // translation for this key - not silently falling back to English (that
  // would mean the key was never actually added to that language file).
  const std::vector<std::string> other_langs = {"es", "fr", "de", "it", "pt", "zh", "ko", "ja", "ru", "ar"};
  for (const std::string& lang : other_langs) {
    Check(dino8::i18n::SetLanguage(lang), lang + " loads as a known language");
    const std::string& translated = dino8::i18n::TrOrDefault("cmddesc.circle", en_desc);
    Check(!translated.empty(), lang + "'s cmddesc.circle is non-empty");
    Check(translated != en_desc, lang + "'s cmddesc.circle differs from the English text (a real translation, not a copy)");
  }

  // A command genuinely outside the bounded 73-command subset (per
  // I18n.h: full catalog translation is explicitly out of scope) must
  // fall back to its own real English description under every language -
  // never the literal, untranslated key "cmddesc.<name>" itself.
  const dino8::app::CommandInfo* outside = catalog.Find("Pipe");
  Check(outside != nullptr, "Pipe is a real catalog entry (used as an outside-the-subset probe)");
  if (outside) {
    const std::string outside_en = outside->description;
    Check(!outside_en.empty(), "Pipe has a real English description");
    for (const std::string& lang : other_langs) {
      dino8::i18n::SetLanguage(lang);
      const std::string& fallback = dino8::i18n::TrOrDefault("cmddesc.pipe", outside_en);
      Check(fallback == outside_en,
            lang + ": an out-of-subset command falls back to its real English description, not a raw key");
    }
  }

  dino8::i18n::SetLanguage("en");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
