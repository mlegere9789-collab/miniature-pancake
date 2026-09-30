#include "platform/Accessibility.h"

#include "platform/AccessibilityPlatform.h"

namespace dino8::platform {

void InitAccessibility(const std::string& app_name) { PlatformInitAccessibility(app_name); }

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
                          const AccessibleNode& plugins) {
  AccessibleNode command_line;
  command_line.name = "Command Line";
  command_line.role = AccessibleRole::Log;
  command_line.description = "Command line input and the command-history log above it";
  command_line.text = BuildCommandLineText(prompt, command_input, history);

  PlatformSetAccessibleTree({command_line, menu_bar, command_options, layers_panel, properties_panel,
                              viewports_panel, activity_log, named_views, named_cplanes, linetypes, materials,
                              clipping_planes, layouts, block_manager, layer_state_manager, document_user_text,
                              lights, annotation_styles, document_notes, environments, audit_results, undo_history,
                              redo_history, hatch_patterns, plugins});
  PlatformPumpAccessibilityEvents();
}

void PumpAccessibilityEvents() { PlatformPumpAccessibilityEvents(); }

void ShutdownAccessibility() { PlatformShutdownAccessibility(); }

#if !defined(DINO8_HAVE_ATSPI)
// No AT-SPI2 client libraries at build time (non-Linux, or Linux without
// libatspi2.0-dev/gio-2.0 available at configure time - see CMakeLists.txt
// and docs/ACCESSIBILITY.md): accessibility is simply unavailable here.
// Every call below is a deliberate no-op, not a missing feature waiting to
// crash - the app runs exactly as it always did.
void PlatformInitAccessibility(const std::string&) {}
void PlatformSetAccessibleTree(std::vector<AccessibleNode>) {}
void PlatformPumpAccessibilityEvents() {}
void PlatformShutdownAccessibility() {}
#endif

}  // namespace dino8::platform
