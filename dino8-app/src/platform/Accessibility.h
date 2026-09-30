// Live accessibility bridge: exposes the command line (prompt, live typed
// input, and the full command-history log), the main menu bar, the running
// command's options, the Layers/Properties panels' current content, every
// viewport's title/view-menu button state, the persisted Activity Log of
// finalized edits, and the document's saved Named Views and Named CPlanes to
// assistive technology, so a screen reader can read and be notified of them
// without a sighted user's help.
//
// Real implementation: AT-SPI2 over D-Bus on Linux (see
// AccessibilityLinux.cpp). A no-op everywhere else, and on Linux too if the
// AT-SPI2 client libraries were not available at build time - see
// docs/ACCESSIBILITY.md for exactly what that means and how to check which
// case a given build is in.
#pragma once

#include <deque>
#include <string>

#include "platform/AccessibilityTree.h"

namespace dino8::platform {

// Connects to the AT-SPI2 registry and publishes the application root.
// Safe to call once at startup; failure (no D-Bus session bus, no AT-SPI
// registry running) is silent and simply leaves accessibility unavailable
// for this run - never fatal, and never blocks: every failure path returns
// promptly rather than waiting on a service that may not exist in this
// environment.
void InitAccessibility(const std::string& app_name);

// Refreshes every published accessible's content to match the app's
// current state, then processes any AT-SPI requests (GetText, GetChildren,
// ...) queued since the last call. Call once per frame.
//
// `menu_bar`, `command_options`, `layers_panel`, `properties_panel`,
// `viewports_panel`, `activity_log`, `named_views` and `named_cplanes` are
// typically ui::LastMenuBarAccessibleTree(), ui::CommandOptionsAccessibleTree(app),
// ui::LayersPanelAccessibleTree(app), ui::PropertiesPanelAccessibleTree(app),
// ui::ViewportsAccessibleTree(app), ui::ActivityLogAccessibleTree(app),
// ui::NamedViewsAccessibleTree(app) and ui::NamedCPlanesAccessibleTree(app)
// (see src/ui/Panels.h) - passed in rather than computed here so this module
// stays independent of app::Application/Document.
void UpdateAccessibility(const std::string& prompt, const std::string& command_input,
                          const std::deque<std::string>& history, const AccessibleNode& menu_bar,
                          const AccessibleNode& command_options, const AccessibleNode& layers_panel,
                          const AccessibleNode& properties_panel, const AccessibleNode& viewports_panel,
                          const AccessibleNode& activity_log, const AccessibleNode& named_views,
                          const AccessibleNode& named_cplanes);

// Just the request-processing half of UpdateAccessibility, without touching
// any published content. Used by main.cpp's `waitfile` script directive
// (see tests/smoke_accessibility.py) so AT-SPI queries keep being answered
// while the smoke script is deliberately paused for a test to synchronize
// against.
void PumpAccessibilityEvents();

// Unregisters from the AT-SPI2 registry and closes the bridge's D-Bus
// connection. Safe to call even if InitAccessibility never got as far as
// connecting (no-op then). Called once from the app's shutdown path.
void ShutdownAccessibility();

}  // namespace dino8::platform
