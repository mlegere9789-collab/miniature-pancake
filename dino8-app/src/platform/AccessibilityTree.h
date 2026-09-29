// The accessibility tree Dino 8 exposes to assistive technology (currently:
// AT-SPI2 on Linux - see AccessibilityLinux.cpp) kept here as plain,
// platform-independent data so its shape can be unit-tested (see
// tests/test_accessibility_tree.cpp) without a display, a D-Bus session, or
// even a build of the AT-SPI bridge itself.
//
// Scope (see docs/ACCESSIBILITY.md for how this grew): the command line's
// live typed text and full command-history log, the main menu bar (mirrored
// live from exactly what ui/MenuBar.cpp draws each frame - see
// MenuTreeBuilder below), and the Layers and Properties panels' current
// content. The 3D viewport and the ~40 other panels/dialogs are still not
// mirrored into this tree.
#pragma once

#include <deque>
#include <string>
#include <vector>

namespace dino8::platform {

enum class AccessibleRole { Application, Log, MenuBar, Menu, MenuItem, List, ListItem };

struct AccessibleNode {
  std::string name;
  AccessibleRole role = AccessibleRole::Application;
  std::string description;  // AT-SPI Description property; empty is fine for most roles
  std::string text;         // full text body; meaningful only for role == Log
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

// Builds the whole tree: an Application root named `app_name` with the
// command-line Log accessible built from BuildCommandLineText, plus whatever
// other top-level regions (menu bar, panels, ...) the caller already built -
// appended in the order given. Used by the unit test to check the overall
// shape; the live bridge (Accessibility.cpp) assembles the same per-frame
// children list without needing the root wrapper, since the root's own
// identity (name) is set once at PlatformInitAccessibility time.
AccessibleNode BuildAccessibleTree(const std::string& app_name, const std::string& prompt,
                                    const std::string& command_input, const std::deque<std::string>& history,
                                    std::vector<AccessibleNode> extra_top_level_children = {});

// Incrementally builds a MenuBar AccessibleNode by recording exactly the
// Begin/EndMenu and menu-item calls a caller makes, in order - so the
// mirror can be produced right alongside the real ImGui menu-drawing calls
// (see ui/MenuBar.cpp) and can never drift out of sync with what was
// actually drawn: a submenu that isn't currently open in ImGui (its
// BeginMenu body never runs) simply never gets its items recorded here
// either, exactly matching what's really on screen. Pure bookkeeping, no
// ImGui/D-Bus dependency, so the push/pop tree-building logic itself is
// unit-tested directly (see tests/test_accessibility_tree.cpp).
class MenuTreeBuilder {
 public:
  MenuTreeBuilder();

  // Call when ImGui::BeginMenu(label) returned true (the submenu is open
  // this frame): starts recording its children until the matching CloseMenu.
  void OpenMenu(const std::string& label);
  // Matches a prior OpenMenu: attaches the finished Menu node (with
  // whatever items/submenus were recorded since OpenMenu) to its parent.
  void CloseMenu();
  // Call when ImGui::BeginMenu(label) returned false (the submenu exists -
  // it's a real, always-visible top-level or nested menu entry - but is not
  // currently open, so ImGui never drew its contents this frame): records
  // just the name, with no children, honestly reflecting "closed".
  void LeafMenu(const std::string& label);
  // Records one concrete, clickable menu entry (ImGui::MenuItem) as a child
  // of whatever menu is currently open. `shortcut` may be empty.
  void Item(const std::string& label, const std::string& shortcut = "");

  // The finished MenuBar node. Only meaningful once every OpenMenu has a
  // matching CloseMenu (i.e. after DrawMenuBar has returned).
  const AccessibleNode& Root() const { return stack_.front(); }

 private:
  std::vector<AccessibleNode> stack_;
};

// One Layers-panel row (doc/Document.h's Layer, plus the object count
// Panels.cpp already computes), reduced to exactly the facts the on-screen
// Layers table shows in its Name/Cur/On/Lock/Objects columns - kept here as
// plain data, independent of Document/ImGui, so BuildLayersPanelNode is
// unit-testable without either.
struct LayerSummary {
  std::string name;
  bool current = false;
  bool visible = true;
  bool locked = false;
  int object_count = 0;
};

// Builds the "Layers" List accessible: one ListItem per layer, in the same
// order as doc.Layers(), each with a plain-text summary of that row's state.
AccessibleNode BuildLayersPanelNode(const std::vector<LayerSummary>& layers);

// One Properties-panel row: a label/value pair, e.g. {"Locked", "No"} or
// {"Layer", "Default"} - the same facts DrawPropertiesPanel shows (either
// about the current selection, or about the document/viewport when nothing
// is selected), reduced to plain text.
struct PropertyEntry {
  std::string label;
  std::string value;
};

// Builds the "Properties" List accessible: `heading` names what the
// properties describe (e.g. "3 objects selected", "No selection"), and one
// ListItem per entry follows.
AccessibleNode BuildPropertiesPanelNode(const std::string& heading, const std::vector<PropertyEntry>& entries);

}  // namespace dino8::platform
