// Internal seam between Accessibility.cpp (the shared text-building logic +
// DINO8_HAVE_ATSPI no-op fallback) and AccessibilityLinux.cpp, the one
// platform-specific translation unit CMake compiles when AT-SPI2's client
// libraries (atspi-2, gio-2.0) were found at configure time. Not part of the
// public API - see Accessibility.h for that.
#pragma once

#include <string>

namespace dino8::platform {

void PlatformInitAccessibility(const std::string& app_name);
void PlatformSetAccessibleText(const std::string& text);
void PlatformPumpAccessibilityEvents();
void PlatformShutdownAccessibility();

}  // namespace dino8::platform
