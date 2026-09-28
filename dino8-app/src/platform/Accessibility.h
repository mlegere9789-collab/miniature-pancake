// Live accessibility bridge for the command line: exposes the same text the
// on-screen ##CommandLine window shows (prompt, live typed input, and the
// full command-history log) to assistive technology, so a screen reader can
// read and be notified of command output without a sighted user's help.
//
// Real implementation: AT-SPI2 over D-Bus on Linux (see
// AccessibilityLinux.cpp). A no-op everywhere else, and on Linux too if the
// AT-SPI2 client libraries were not available at build time - see
// docs/ACCESSIBILITY.md for exactly what that means and how to check which
// case a given build is in.
#pragma once

#include <deque>
#include <string>

namespace dino8::platform {

// Connects to the AT-SPI2 registry and publishes the application root plus
// its one "Command Line" accessible object. Safe to call once at startup;
// failure (no D-Bus session bus, no AT-SPI registry running) is silent and
// simply leaves accessibility unavailable for this run - never fatal, and
// never blocks: every failure path returns promptly rather than waiting on
// a service that may not exist in this environment.
void InitAccessibility(const std::string& app_name);

// Refreshes the "Command Line" accessible's text to match the app's current
// prompt/typed-input/history, then processes any AT-SPI requests (GetText,
// GetChildren, ...) queued since the last call. Call once per frame.
void UpdateAccessibility(const std::string& prompt, const std::string& command_input,
                          const std::deque<std::string>& history);

// Just the request-processing half of UpdateAccessibility, without touching
// the published text. Used by main.cpp's `waitfile` script directive (see
// tests/smoke_accessibility.py) so AT-SPI queries keep being answered while
// the smoke script is deliberately paused for a test to synchronize against.
void PumpAccessibilityEvents();

// Unregisters from the AT-SPI2 registry and closes the bridge's D-Bus
// connection. Safe to call even if InitAccessibility never got as far as
// connecting (no-op then). Called once from the app's shutdown path.
void ShutdownAccessibility();

}  // namespace dino8::platform
