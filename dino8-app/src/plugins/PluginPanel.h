// PlugInManager and PackageManager panels.
#pragma once

#include "platform/AccessibilityTree.h"

namespace dino8::app { class Application; }

namespace dino8::plugins {

void DrawPlugInManagerPanel(app::Application& app, bool& open);
void DrawPackageManagerPanel(app::Application& app, bool& open);
// The AT-SPI2-queryable snapshot of Manager::Get().Plugins() - independent
// of whether the Plug-in Manager panel window is open, mirroring the same
// Name/Version/Commands/Flow Nodes/Status facts DrawPlugInManagerPanel's
// table shows per row (its Status tooltip's error text folded directly into
// the row's Description). Not empty by default in a real build:
// `Application`'s own constructor scans the default plugin folders at
// startup (see Application.cpp), so any already-installed plug-in (this
// repository's own bundled examples included) is already listed here.
dino8::platform::AccessibleNode PluginsAccessibleTree();

}  // namespace dino8::plugins
