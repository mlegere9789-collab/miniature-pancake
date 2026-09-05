// Dino 8 visual theme: a calm dark UI with a single accent colour, larger
// hit targets than Rhino's, and consistent spacing everywhere.
#pragma once

namespace dino8::app {

void ApplyDinoTheme(float ui_scale, bool light = false);

// Accent colour used by toolbars, status toggles and selection highlights.
struct ThemeColors {
  static constexpr float kAccent[4] = {0.30f, 0.62f, 0.95f, 1.0f};
  static constexpr float kAccentDim[4] = {0.24f, 0.42f, 0.62f, 1.0f};
  static constexpr float kWarn[4] = {0.95f, 0.70f, 0.25f, 1.0f};
  static constexpr float kOk[4] = {0.45f, 0.80f, 0.45f, 1.0f};
  static constexpr float kMuted[4] = {0.62f, 0.64f, 0.68f, 1.0f};
};

}  // namespace dino8::app
