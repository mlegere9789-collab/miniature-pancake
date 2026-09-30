#pragma once

// Pure keyboard-shortcut logic, deliberately free of ImGui/GLFW linkage (it
// only needs ImGuiKey's *enum values*, from imgui.h, not the ImGui library
// itself - see ShortcutRules.cpp) or any Application/Document dependency,
// the same "standalone, no live document/window" shape
// tests/test_aci_palette.cpp and tests/test_plugin_marketplace.cpp already
// use for their own narrow slice of app logic - so IsReservedShortcut's
// chord table can be unit-tested (tests/test_shortcuts.cpp) without
// building the whole app or a GL context.

namespace dino8::app {

// True for the ~20 chords Application::HandleShortcuts (app/Application.cpp)
// gives a built-in default action: Ctrl+Z/Y/A/S/O/N/G/H/C/V/X, Ctrl+Shift+S,
// F1-F11, Escape, Delete, Home, PageUp/PageDown, the arrow keys. `key` is an
// ImGuiKey value, passed as int so this header does not have to include
// imgui.h. Not used to block a user-assigned shortcut (app/Application.h's
// KeyShortcut) on one of these chords - a user shortcut REPLACES the
// built-in default instead, same as Rhino's own fully remappable Tools >
// Options > Keyboard dialog. Purely informational: HandleShortcuts uses it
// to know when to skip its own built-in action in favor of the user's, and
// the Options > Shortcuts tab (ui/Panels.cpp) uses it to show "this
// replaces a built-in" instead of silently doing nothing the way it used to.
bool IsReservedShortcut(int key, bool ctrl, bool shift, bool alt);

}  // namespace dino8::app
