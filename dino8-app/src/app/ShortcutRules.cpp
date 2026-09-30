#include "app/ShortcutRules.h"

// Only ImGuiKey's enum values are used below - no ImGui function is called,
// so this translation unit needs imgui.h on its include path but never
// links the imgui library itself (see CMakeLists.txt's dino8_test_shortcuts
// target, which does exactly that).
#include "imgui.h"

namespace dino8::app {

bool IsReservedShortcut(int key_i, bool ctrl, bool shift, bool alt) {
  const ImGuiKey key = static_cast<ImGuiKey>(key_i);
  if (!ctrl && !shift && !alt) {
    switch (key) {
      case ImGuiKey_Escape: case ImGuiKey_Delete: case ImGuiKey_F1: case ImGuiKey_F2: case ImGuiKey_F3:
      case ImGuiKey_F4: case ImGuiKey_F7: case ImGuiKey_F8: case ImGuiKey_F9: case ImGuiKey_F10: case ImGuiKey_F11:
      case ImGuiKey_Home: case ImGuiKey_PageUp: case ImGuiKey_PageDown:
      case ImGuiKey_LeftArrow: case ImGuiKey_RightArrow: case ImGuiKey_UpArrow: case ImGuiKey_DownArrow:
        return true;
      default: break;
    }
  }
  if (ctrl && !shift && !alt) {
    switch (key) {
      case ImGuiKey_Z: case ImGuiKey_Y: case ImGuiKey_A: case ImGuiKey_S: case ImGuiKey_O: case ImGuiKey_N:
      case ImGuiKey_G: case ImGuiKey_H: case ImGuiKey_C: case ImGuiKey_V: case ImGuiKey_X: case ImGuiKey_F1:
        return true;
      default: break;
    }
  }
  if (ctrl && shift && !alt && key == ImGuiKey_S) return true;  // SaveAs
  return false;
}

}  // namespace dino8::app
