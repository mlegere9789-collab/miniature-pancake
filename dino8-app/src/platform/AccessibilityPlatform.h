// Internal seam between Accessibility.cpp (the shared tree-building logic +
// DINO8_HAVE_ATSPI no-op fallback) and AccessibilityLinux.cpp, the one
// platform-specific translation unit CMake compiles when AT-SPI2's client
// libraries (atspi-2, gio-2.0) were found at configure time. Not part of the
// public API - see Accessibility.h for that.
#pragma once

#include <string>
#include <vector>

#include "platform/AccessibilityTree.h"

namespace dino8::platform {

void PlatformInitAccessibility(const std::string& app_name);
// Replaces the application root's children wholesale with `top_level` for
// this frame (Command Line, Menu Bar, Layers, Properties - see
// Accessibility.cpp) - the live counterpart of AccessibilityTree.h's pure
// BuildAccessibleTree, minus the root wrapper itself (the root's own
// identity was already set by PlatformInitAccessibility).
void PlatformSetAccessibleTree(std::vector<AccessibleNode> top_level);
void PlatformPumpAccessibilityEvents();
void PlatformShutdownAccessibility();

}  // namespace dino8::platform
