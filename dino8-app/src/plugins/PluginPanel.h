// PlugInManager and PackageManager panels.
#pragma once

namespace dino8::app { class Application; }

namespace dino8::plugins {

void DrawPlugInManagerPanel(app::Application& app, bool& open);
void DrawPackageManagerPanel(app::Application& app, bool& open);

}  // namespace dino8::plugins
