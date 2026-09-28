// Verifies the internal accessibility-tree mirror (platform/AccessibilityTree.h)
// that AccessibilityLinux.cpp's real AT-SPI2 bridge publishes over D-Bus:
// the tree shape (one Application root, one Log child) and the exact text
// that child reports - history lines, then prompt, then live input - match
// what the on-screen ##CommandLine window shows (see Application.cpp's
// DrawCommandLine). This needs no display, no D-Bus session, and no AT-SPI2
// build at all, so it runs on every platform and every CI job regardless of
// whether AccessibilityLinux.cpp itself was compiled in this build (see
// docs/ACCESSIBILITY.md).
#include <cstdio>
#include <deque>
#include <string>

#include "platform/AccessibilityTree.h"

using dino8::platform::AccessibleRole;
using dino8::platform::BuildAccessibleTree;
using dino8::platform::BuildCommandLineText;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}  // namespace

int main() {
  // Empty history, no input: just the bare prompt.
  {
    const std::string text = BuildCommandLineText("Command: ", "", {});
    Check(text == "Command: ", "empty history + empty input -> bare prompt");
  }

  // History lines come first (one per line, in order), then the prompt,
  // then whatever's currently typed - exactly the order the ##CommandLine
  // window itself renders top (history) to bottom (prompt + input).
  {
    const std::deque<std::string> history = {"Command: Line 0,0,0 10,10,0", "Length = 14.14"};
    const std::string text = BuildCommandLineText("Command: ", "Bo", history);
    const std::string expected = "Command: Line 0,0,0 10,10,0\nLength = 14.14\nCommand: Bo";
    Check(text == expected, "history lines, then prompt, then live input, in on-screen order");
  }

  // The text must actually change once a new line is appended to history -
  // this is the property tests/smoke_accessibility.py checks end-to-end
  // over real AT-SPI2 (Text.GetText before vs. after a command runs); here
  // it's checked directly against the pure function with no D-Bus involved.
  {
    std::deque<std::string> history = {"Command: Box 0,0,0 5,5,5"};
    const std::string before = BuildCommandLineText("Command: ", "", history);
    history.push_back("Command: Line 0,0,0 10,10,0");
    const std::string after = BuildCommandLineText("Command: ", "", history);
    Check(before != after, "appending a history line changes the built text");
    Check(after.find("Line 0,0,0 10,10,0") != std::string::npos, "the new command's text is actually present");
  }

  // Tree shape: exactly one child, an Application root and a Log leaf,
  // named the way the on-screen widget's title implies.
  {
    const std::deque<std::string> history = {"Command: Circle 0,0,0 5"};
    const dino8::platform::AccessibleNode root = BuildAccessibleTree("Dino8", "Command: ", "Li", history);
    Check(root.name == "Dino8", "root is named after the app");
    Check(root.role == AccessibleRole::Application, "root's role is Application");
    Check(root.children.size() == 1, "root has exactly one child (command line only - see docs/ACCESSIBILITY.md)");
    if (root.children.size() == 1) {
      const dino8::platform::AccessibleNode& cmdline = root.children[0];
      Check(cmdline.name == "Command Line", "child is named \"Command Line\"");
      Check(cmdline.role == AccessibleRole::Log, "child's role is Log (a text region holding log content)");
      Check(cmdline.text == BuildCommandLineText("Command: ", "Li", history),
            "child's text matches BuildCommandLineText's own output");
      Check(cmdline.children.empty(), "the command-line node is a leaf");
    }
  }

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
