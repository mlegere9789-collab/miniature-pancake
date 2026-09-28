// The accessibility tree Dino 8 exposes to assistive technology (currently:
// AT-SPI2 on Linux - see AccessibilityLinux.cpp) kept here as plain,
// platform-independent data so its shape can be unit-tested (see
// tests/test_accessibility_tree.cpp) without a display, a D-Bus session, or
// even a build of the AT-SPI bridge itself.
//
// Scope (see docs/ACCESSIBILITY.md for why it stops here): only the command
// line - its live typed text and the full command-history log the
// ##CommandLine ImGui window shows above it - is exposed. The 3D viewport,
// panels and dialogs are not yet mirrored into this tree.
#pragma once

#include <deque>
#include <string>
#include <vector>

namespace dino8::platform {

enum class AccessibleRole { Application, Log };

struct AccessibleNode {
  std::string name;
  AccessibleRole role = AccessibleRole::Application;
  std::string text;  // meaningful only for role == Log
  std::vector<AccessibleNode> children;
};

// Builds the exact text the single "Command Line" accessible object (role
// ATSPI_ROLE_LOG - "a text widget or container holding log content") should
// report: the full command-history log, one line per entry, followed by the
// current prompt and whatever is live in the input buffer right now - the
// same three pieces of information the on-screen ##CommandLine window shows
// (Application.cpp's DrawCommandLine: a muted history line, then the prompt,
// then the InputText box), just as plain queryable text instead of pixels.
std::string BuildCommandLineText(const std::string& prompt, const std::string& command_input,
                                  const std::deque<std::string>& history);

// Builds the whole tree: an Application root named `app_name` with exactly
// one child, the command-line Log accessible built from BuildCommandLineText.
AccessibleNode BuildAccessibleTree(const std::string& app_name, const std::string& prompt,
                                    const std::string& command_input, const std::deque<std::string>& history);

}  // namespace dino8::platform
