// Minimal i18n infrastructure: a key -> translated-string lookup backed by
// JSON tables under data/i18n/<code>.json (one file per language, loaded
// with the same next-to-the-executable search order as commands.json - see
// Application::Init). English ("en") is always the fallback: a key missing
// from the active language's table (a string nobody has translated yet, or
// a language file that only covers part of the UI) falls back to the
// English text rather than showing a raw key or a blank string, so a
// half-translated language degrades gracefully instead of breaking the UI.
//
// Scope: this covers the highest-visibility UI chrome (menu bar, panel
// titles, the status bar, common dialogs) - NOT the ~1055 commands' help
// text in data/commands.json, which stays English-only for every language
// (see RHINO8_KILLER_AUDIT.md). Any string not registered as an i18n key
// simply isn't translated yet; Tr() never fails, it just echoes English.
#pragma once

#include <string>
#include <vector>

namespace dino8::i18n {

// Scans the given candidate directories (same style as Application::Init's
// commands.json search) for a "i18n" subfolder and loads every *.json file
// in it as one language table, keyed by filename stem (e.g. "es.json" ->
// language code "es"). Safe to call with directories that don't exist.
// Always leaves "en" loaded even if no file for it is found on disk (a
// small built-in table so the app never has zero strings), then loaded
// files layer on top of / replace it.
void Init(const std::vector<std::string>& candidate_dirs);

// Sets the active language by code, case-insensitively. Returns false (and
// leaves the active language unchanged) if that code has no loaded table.
bool SetLanguage(const std::string& code);

const std::string& CurrentLanguage();  // e.g. "en"
const std::string& CurrentLanguageName();  // e.g. "English", in its own language

// One entry per loaded language: {code, native display name}, "en" first,
// the rest alphabetically by code.
struct LanguageEntry {
  std::string code;
  std::string name;
};
std::vector<LanguageEntry> AvailableLanguages();

// Translated text for `key`. Lookup order: active language -> English ->
// the key itself (so a missing translation is visible as English text, an
// entirely unknown key is visible as its key rather than an empty string
// or a crash).
const std::string& Tr(const std::string& key);

}  // namespace dino8::i18n
