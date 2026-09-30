// Live accessibility bridge: exposes the command line (prompt, live typed
// input, and the full command-history log), the main menu bar, the running
// command's options, the Layers/Properties panels' current content, every
// viewport's title/view-menu button state, the persisted Activity Log of
// finalized edits, the document's saved Named Views and Named CPlanes, its
// Linetypes, its Materials, its Clipping Planes, its Layouts, its Block
// Manager, its Layer State Manager, its Document User Text, its Lights, its
// Annotation Styles, its Notes, its render Environment settings, the last
// Audit run's results, its pending Undo/Redo history, the loaded Hatch
// Pattern library, its loaded plug-ins, the ~1055-command Rhino 8 reference
// catalog, its saved command aliases, its customized keyboard shortcuts, its
// units/tolerances/grid/metadata Document Properties, the materials that
// carry a Texture, and its active-viewport/grid/display-tolerance Display
// settings to assistive technology, so a screen reader can read and be
// notified of them without a sighted user's help.
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
// `viewports_panel`, `activity_log`, `named_views`, `named_cplanes`,
// `linetypes`, `materials`, `clipping_planes`, `layouts`, `block_manager`,
// `layer_state_manager`, `document_user_text`, `lights`,
// `annotation_styles`, `document_notes`, `environments`, `audit_results`,
// `undo_history`, `redo_history`, `hatch_patterns`, `plugins`,
// `command_list`, `command_aliases`, `keyboard_shortcuts`,
// `document_properties`, `textures` and `display` are typically
// ui::LastMenuBarAccessibleTree(), ui::CommandOptionsAccessibleTree(app),
// ui::LayersPanelAccessibleTree(app), ui::PropertiesPanelAccessibleTree(app),
// ui::ViewportsAccessibleTree(app), ui::ActivityLogAccessibleTree(app),
// ui::NamedViewsAccessibleTree(app), ui::NamedCPlanesAccessibleTree(app),
// ui::LinetypesAccessibleTree(app), ui::MaterialsAccessibleTree(app),
// ui::ClippingPlanesAccessibleTree(app), ui::LayoutsAccessibleTree(app),
// ui::BlockManagerAccessibleTree(app), ui::LayerStateManagerAccessibleTree(app),
// ui::DocumentUserTextAccessibleTree(app), ui::LightsAccessibleTree(app),
// ui::AnnotationStylesAccessibleTree(app), ui::DocumentNotesAccessibleTree(app),
// ui::EnvironmentsAccessibleTree(app), ui::AuditResultsAccessibleTree(app),
// ui::UndoHistoryAccessibleTree(app), ui::RedoHistoryAccessibleTree(app),
// ui::HatchPatternsAccessibleTree(), plugins::PluginsAccessibleTree(),
// ui::CommandListAccessibleTree(app), ui::CommandAliasesAccessibleTree(app),
// ui::KeyboardShortcutsAccessibleTree(app), ui::DocumentPropertiesAccessibleTree(app),
// ui::TexturesAccessibleTree(app) and ui::DisplayAccessibleTree(app) (see
// src/ui/Panels.h and src/plugins/PluginPanel.h) - passed in rather than
// computed here so this module stays independent of app::Application/Document.
void UpdateAccessibility(const std::string& prompt, const std::string& command_input,
                          const std::deque<std::string>& history, const AccessibleNode& menu_bar,
                          const AccessibleNode& command_options, const AccessibleNode& layers_panel,
                          const AccessibleNode& properties_panel, const AccessibleNode& viewports_panel,
                          const AccessibleNode& activity_log, const AccessibleNode& named_views,
                          const AccessibleNode& named_cplanes, const AccessibleNode& linetypes,
                          const AccessibleNode& materials, const AccessibleNode& clipping_planes,
                          const AccessibleNode& layouts, const AccessibleNode& block_manager,
                          const AccessibleNode& layer_state_manager, const AccessibleNode& document_user_text,
                          const AccessibleNode& lights, const AccessibleNode& annotation_styles,
                          const AccessibleNode& document_notes, const AccessibleNode& environments,
                          const AccessibleNode& audit_results, const AccessibleNode& undo_history,
                          const AccessibleNode& redo_history, const AccessibleNode& hatch_patterns,
                          const AccessibleNode& plugins, const AccessibleNode& command_list,
                          const AccessibleNode& command_aliases, const AccessibleNode& keyboard_shortcuts,
                          const AccessibleNode& document_properties, const AccessibleNode& textures,
                          const AccessibleNode& display);

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
