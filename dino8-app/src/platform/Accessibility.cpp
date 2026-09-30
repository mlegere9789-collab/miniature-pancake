#include "platform/Accessibility.h"

#include "platform/AccessibilityPlatform.h"

namespace dino8::platform {

void InitAccessibility(const std::string& app_name) { PlatformInitAccessibility(app_name); }

void UpdateAccessibility(const std::string& prompt, const std::string& command_input,
                          const std::deque<std::string>& history, const AccessibleNode& menu_bar,
                          const AccessibleNode& command_options, const AccessibleNode& layers_panel,
                          const AccessibleNode& properties_panel, const AccessibleNode& viewports_panel,
                          const AccessibleNode& activity_log) {
  AccessibleNode command_line;
  command_line.name = "Command Line";
  command_line.role = AccessibleRole::Log;
  command_line.description = "Command line input and the command-history log above it";
  command_line.text = BuildCommandLineText(prompt, command_input, history);

  PlatformSetAccessibleTree(
      {command_line, menu_bar, command_options, layers_panel, properties_panel, viewports_panel, activity_log});
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
