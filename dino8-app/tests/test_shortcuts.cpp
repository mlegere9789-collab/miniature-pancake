// Unit test for the keyboard-shortcut chord table (src/app/ShortcutRules.cpp):
// IsReservedShortcut names every chord Application::HandleShortcuts
// (app/Application.cpp) gives a built-in default action. A user shortcut
// (Options > Shortcuts, app/Application.h's KeyShortcut) on one of these
// chords now REPLACES that built-in default instead of being silently
// blocked by it - see PARITY_MAP.md's "Command aliases and shortcut
// customization" item, which this closes the remaining half of. This test
// exists to pin the exact reserved set (a regression here would either
// silently re-block a chord users can now remap, or stop guarding one of
// HandleShortcuts's own hardcoded actions) and to prove the un-modified and
// modified cases are genuinely distinct, not a function that just always
// returns the same thing.
#include <cstdio>
#include <utility>

#include "app/ShortcutRules.h"
#include "imgui.h"

using dino8::app::IsReservedShortcut;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}  // namespace

int main() {
  // Plain (no modifier) reserved chords. Named by hand, not ImGui::GetKeyName
  // - that is a real ImGui function requiring the imgui library linked in,
  // which this standalone test deliberately does not do (see ShortcutRules.h's
  // own comment: only the enum *values* are needed, not the library).
  const std::pair<ImGuiKey, const char*> plain[] = {
      {ImGuiKey_Escape, "Escape"}, {ImGuiKey_Delete, "Delete"}, {ImGuiKey_F1, "F1"}, {ImGuiKey_F2, "F2"},
      {ImGuiKey_F3, "F3"}, {ImGuiKey_F4, "F4"}, {ImGuiKey_F7, "F7"}, {ImGuiKey_F8, "F8"}, {ImGuiKey_F9, "F9"},
      {ImGuiKey_F10, "F10"}, {ImGuiKey_F11, "F11"}, {ImGuiKey_Home, "Home"}, {ImGuiKey_PageUp, "PageUp"},
      {ImGuiKey_PageDown, "PageDown"}, {ImGuiKey_LeftArrow, "Left"}, {ImGuiKey_RightArrow, "Right"},
      {ImGuiKey_UpArrow, "Up"}, {ImGuiKey_DownArrow, "Down"}};
  for (const auto& [k, name] : plain) {
    char label[64];
    std::snprintf(label, sizeof(label), "plain %s is reserved", name);
    Check(IsReservedShortcut(static_cast<int>(k), false, false, false), label);
  }

  // Ctrl-modified reserved chords.
  const std::pair<ImGuiKey, const char*> ctrl[] = {
      {ImGuiKey_Z, "Z"}, {ImGuiKey_Y, "Y"}, {ImGuiKey_A, "A"}, {ImGuiKey_S, "S"}, {ImGuiKey_O, "O"},
      {ImGuiKey_N, "N"}, {ImGuiKey_G, "G"}, {ImGuiKey_H, "H"}, {ImGuiKey_C, "C"}, {ImGuiKey_V, "V"},
      {ImGuiKey_X, "X"}, {ImGuiKey_F1, "F1"}};
  for (const auto& [k, name] : ctrl) {
    char label[64];
    std::snprintf(label, sizeof(label), "Ctrl+%s is reserved", name);
    Check(IsReservedShortcut(static_cast<int>(k), true, false, false), label);
  }

  // Ctrl+Shift+S (SaveAs) is its own, narrower reserved entry: plain Ctrl+S
  // (Save) and Ctrl+Shift+S are BOTH reserved, but only because S appears in
  // both tables above and below - a chord is not reserved just because it
  // shares a letter with a reserved one.
  Check(IsReservedShortcut(static_cast<int>(ImGuiKey_S), true, true, false), "Ctrl+Shift+S (SaveAs) is reserved");
  Check(!IsReservedShortcut(static_cast<int>(ImGuiKey_Z), true, true, false), "Ctrl+Shift+Z is NOT reserved (only plain Ctrl+Z is)");
  Check(!IsReservedShortcut(static_cast<int>(ImGuiKey_A), true, false, true), "Ctrl+Alt+A is NOT reserved (only plain Ctrl+A is)");

  // A handful of ordinary, never-reserved chords a user is free to bind
  // without ever replacing a built-in - the function must not just return
  // true for everything.
  Check(!IsReservedShortcut(static_cast<int>(ImGuiKey_L), true, false, false), "Ctrl+L is NOT reserved (free for user binding)");
  Check(!IsReservedShortcut(static_cast<int>(ImGuiKey_F5), false, false, false), "plain F5 is NOT reserved (F1-F4/F7-F11 are, F5/F6 are not)");
  Check(!IsReservedShortcut(static_cast<int>(ImGuiKey_F6), false, false, false), "plain F6 is NOT reserved");
  Check(!IsReservedShortcut(static_cast<int>(ImGuiKey_Z), false, false, false), "plain Z (no Ctrl) is NOT reserved - only the Ctrl+Z chord is");
  Check(!IsReservedShortcut(static_cast<int>(ImGuiKey_Delete), true, false, false), "Ctrl+Delete is NOT reserved (only plain Delete is)");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
