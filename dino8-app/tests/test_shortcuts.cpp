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
#include <functional>
#include <utility>
#include <vector>

#include "app/ShortcutRules.h"
#include "imgui.h"

using dino8::app::IsReservedShortcut;
using dino8::app::IsUnbindableCaptureKey;
using dino8::app::ResolveCapturedKey;

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

  // IsUnbindableCaptureKey: the Options > Shortcuts "Press a key..."
  // capture flow (ui/Panels.cpp) must skip the four modifier keys (they're
  // captured separately, into ctrl_mod/shift_mod/alt_mod, from
  // ImGuiIO::KeyCtrl/KeyShift/KeyAlt at the moment the real key is
  // pressed), their internal mod-storage aliases, and the mouse-button
  // aliases ImGui exposes through the same ImGuiKey enum - see
  // ShortcutRules.h's own comment for why none of these can be "the key".
  const std::pair<ImGuiKey, const char*> modifiers[] = {
      {ImGuiKey_LeftCtrl, "LeftCtrl"}, {ImGuiKey_LeftShift, "LeftShift"}, {ImGuiKey_LeftAlt, "LeftAlt"},
      {ImGuiKey_LeftSuper, "LeftSuper"}, {ImGuiKey_RightCtrl, "RightCtrl"}, {ImGuiKey_RightShift, "RightShift"},
      {ImGuiKey_RightAlt, "RightAlt"}, {ImGuiKey_RightSuper, "RightSuper"}};
  for (const auto& [k, name] : modifiers) {
    char label[64];
    std::snprintf(label, sizeof(label), "modifier key %s is unbindable-as-key", name);
    Check(IsUnbindableCaptureKey(static_cast<int>(k)), label);
  }
  const ImGuiKey mod_aliases[] = {ImGuiKey_ReservedForModCtrl, ImGuiKey_ReservedForModShift, ImGuiKey_ReservedForModAlt,
                                   ImGuiKey_ReservedForModSuper};
  for (ImGuiKey k : mod_aliases) Check(IsUnbindableCaptureKey(static_cast<int>(k)), "reserved mod-storage alias is unbindable-as-key");
  const ImGuiKey mouse[] = {ImGuiKey_MouseLeft, ImGuiKey_MouseRight, ImGuiKey_MouseMiddle,
                             ImGuiKey_MouseX1, ImGuiKey_MouseX2, ImGuiKey_MouseWheelX, ImGuiKey_MouseWheelY};
  for (ImGuiKey k : mouse) Check(IsUnbindableCaptureKey(static_cast<int>(k)), "mouse-button alias is unbindable-as-key");
  // Ordinary keys must NOT be excluded - the function must not just return
  // true for everything, same cross-check style as the reserved-chord
  // table above.
  const std::pair<ImGuiKey, const char*> bindable[] = {
      {ImGuiKey_L, "L"}, {ImGuiKey_F5, "F5"}, {ImGuiKey_Escape, "Escape"}, {ImGuiKey_Delete, "Delete"},
      {ImGuiKey_Space, "Space"}, {ImGuiKey_Keypad0, "Keypad0"}, {ImGuiKey_GamepadFaceDown, "GamepadFaceDown"}};
  for (const auto& [k, name] : bindable) {
    char label[64];
    std::snprintf(label, sizeof(label), "ordinary key %s IS bindable-as-key", name);
    Check(!IsUnbindableCaptureKey(static_cast<int>(k)), label);
  }

  // ResolveCapturedKey: the Options > Shortcuts "Press a key..." capture
  // flow's own per-frame "which key" decision (ui/Panels.cpp), extracted
  // into app/ShortcutRules.{h,cpp} precisely so this - previously
  // untested beyond its own two helper predicates checked in isolation -
  // can be driven end to end here with a fake `is_pressed` standing in
  // for a live ImGuiIO, closing the gap PARITY_MAP.md's "Command aliases
  // and shortcut customization" item's own capture-button note names.
  auto PressedOf = [](std::vector<ImGuiKey> keys) {
    return [keys = std::move(keys)](int k) {
      for (ImGuiKey key : keys) if (static_cast<int>(key) == k) return true;
      return false;
    };
  };
  Check(ResolveCapturedKey(PressedOf({})) == static_cast<int>(ImGuiKey_None),
        "ResolveCapturedKey: nothing pressed returns None");
  Check(ResolveCapturedKey(PressedOf({ImGuiKey_L})) == static_cast<int>(ImGuiKey_L),
        "ResolveCapturedKey: an ordinary pressed key is returned");
  Check(ResolveCapturedKey(PressedOf({ImGuiKey_LeftCtrl})) == static_cast<int>(ImGuiKey_None),
        "ResolveCapturedKey: a lone modifier key is skipped, not returned as the bound key");
  Check(ResolveCapturedKey(PressedOf({ImGuiKey_MouseLeft})) == static_cast<int>(ImGuiKey_None),
        "ResolveCapturedKey: a mouse-button alias is skipped too (the button click itself)");
  Check(ResolveCapturedKey(PressedOf({ImGuiKey_LeftCtrl, ImGuiKey_L})) == static_cast<int>(ImGuiKey_L),
        "ResolveCapturedKey: Ctrl+L reports L as the key (Ctrl is read from ImGuiIO separately, not from this scan)");
  // A and Z are both ordinary, both "pressed" here - A must win, since it
  // sits earlier in ImGuiKey_NamedKey_BEGIN..END's own order, the exact
  // same scan order ui/Panels.cpp's old inline loop already used, now
  // just performed inside ResolveCapturedKey instead.
  Check(ResolveCapturedKey(PressedOf({ImGuiKey_Z, ImGuiKey_A})) == static_cast<int>(ImGuiKey_A),
        "ResolveCapturedKey: of two simultaneously-pressed keys, the earlier one in enum order wins");
  Check(ResolveCapturedKey(PressedOf({ImGuiKey_RightSuper, ImGuiKey_ReservedForModShift, ImGuiKey_MouseWheelY, ImGuiKey_F5})) ==
            static_cast<int>(ImGuiKey_F5),
        "ResolveCapturedKey: every unbindable kind (modifier, mod-storage alias, mouse) is skipped in favor of a real key");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
