// Per-user settings that survive restarts: recent files, snaps, panel
// visibility, display tolerances, UI scale. Stored as JSON in the user's
// configuration directory next to the ImGui window-layout file.
#pragma once

#include <string>
#include <vector>

namespace dino8::app {

class Application;

// Platform configuration directory for Dino 8 (created on demand):
//   Windows: %APPDATA%\Dino8   macOS: ~/Library/Application Support/Dino8
//   Linux: $XDG_CONFIG_HOME/dino8 or ~/.config/dino8
std::string ConfigDirectory();

bool LoadSettings(Application& app, float& ui_scale);
bool SaveSettings(const Application& app, float ui_scale);
// The same JSON, to/from an explicit file (OptionsExport / OptionsImport).
bool LoadSettingsFrom(const std::string& path, Application& app, float& ui_scale);
bool SaveSettingsTo(const std::string& path, const Application& app, float ui_scale);

}  // namespace dino8::app
