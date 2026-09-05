// Dino 8 visual theme: a calm dark (or light) UI with a single accent
// colour, larger hit targets than Rhino's, and consistent spacing everywhere.
#pragma once

#include "imgui.h"

namespace dino8::app {

// Applies the theme. `accent_rgb` (3 floats, 0..1) overrides the default
// Dino teal; nullptr keeps the last accent that was set.
void ApplyDinoTheme(float ui_scale, bool light = false, const float* accent_rgb = nullptr);

// Colours shared by toolbars, status toggles, badges and selection
// highlights. kAccent follows the "Accent colour" option; the others are
// fixed semantic colours that read well on both themes.
struct ThemeColors {
  inline static float kAccent[4] = {0.184f, 0.655f, 0.627f, 1.0f};      // #2FA7A0 Dino teal
  inline static float kAccentDim[4] = {0.15f, 0.45f, 0.43f, 1.0f};
  inline static float kAccentBright[4] = {0.30f, 0.80f, 0.77f, 1.0f};
  static constexpr float kWarn[4] = {0.95f, 0.70f, 0.25f, 1.0f};
  static constexpr float kOk[4] = {0.45f, 0.80f, 0.45f, 1.0f};
  static constexpr float kMuted[4] = {0.62f, 0.64f, 0.68f, 1.0f};
  static ImVec4 Accent(float alpha = 1.0f) { return ImVec4(kAccent[0], kAccent[1], kAccent[2], alpha); }
  static ImU32 AccentU32(float alpha = 1.0f) { return ImGui::GetColorU32(Accent(alpha)); }
};

// Draws a small solid badge (e.g. "Implemented") in `color`; returns its width.
float StatusBadge(const char* text, ImVec4 color);

}  // namespace dino8::app
