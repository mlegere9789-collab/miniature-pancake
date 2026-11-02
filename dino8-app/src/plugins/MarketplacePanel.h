// Plug-in Marketplace panel: browse a loaded plug-in index and install an
// entry from it. See plugins/Marketplace.h for the index format/loader and
// plugin-index/SCHEMA.md for the on-disk JSON schema.
#pragma once

namespace dino8::app { class Application; }

namespace dino8::plugins {

void DrawPluginMarketplacePanel(app::Application& app, bool& open);

}  // namespace dino8::plugins
