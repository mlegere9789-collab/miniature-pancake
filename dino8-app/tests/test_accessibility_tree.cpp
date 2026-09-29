// Verifies the internal accessibility-tree mirror (platform/AccessibilityTree.h)
// that AccessibilityLinux.cpp's real AT-SPI2 bridge publishes over D-Bus:
// the command line's tree shape (one Application root, one Log child) and
// the exact text that child reports - history lines, then prompt, then live
// input - match what the on-screen ##CommandLine window shows (see
// Application.cpp's DrawCommandLine); the menu-bar mirror MenuTreeBuilder
// produces (see ui/MenuBar.cpp) matches exactly what its Begin/EndMenu/Item
// calls were; and the Layers/Properties panel builders produce the plain
// text a screen reader should hear for a given layer/property list. This
// needs no display, no D-Bus session, and no AT-SPI2 build at all, so it
// runs on every platform and every CI job regardless of whether
// AccessibilityLinux.cpp itself was compiled in this build (see
// docs/ACCESSIBILITY.md).
#include <cstdio>
#include <deque>
#include <string>

#include "platform/AccessibilityTree.h"

using dino8::platform::AccessibleRole;
using dino8::platform::BuildAccessibleTree;
using dino8::platform::BuildCommandLineText;
using dino8::platform::BuildLayersPanelNode;
using dino8::platform::BuildPropertiesPanelNode;
using dino8::platform::LayerSummary;
using dino8::platform::MenuTreeBuilder;
using dino8::platform::PropertyEntry;

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

  // MenuTreeBuilder: a closed top-level menu (BeginMenu returned false)
  // records just its name, with no children - honestly reflecting that
  // ImGui never drew its contents this frame.
  {
    MenuTreeBuilder mb;
    mb.LeafMenu("File");
    mb.LeafMenu("Edit");
    const dino8::platform::AccessibleNode& root = mb.Root();
    Check(root.role == dino8::platform::AccessibleRole::MenuBar, "menu tree root role is MenuBar");
    Check(root.children.size() == 2, "two closed top-level menus recorded");
    if (root.children.size() == 2) {
      Check(root.children[0].name == "File", "first closed menu named File");
      Check(root.children[0].role == dino8::platform::AccessibleRole::Menu, "closed menu role is Menu");
      Check(root.children[0].children.empty(), "closed menu has no items");
      Check(root.children[1].name == "Edit", "second closed menu named Edit");
    }
  }

  // An open menu's items nest under it, in call order; a submenu that
  // itself isn't open records as a leaf right alongside real items, exactly
  // as ui/MenuBar.cpp's BeginMenuA/EndMenuA/Item calls would produce.
  {
    MenuTreeBuilder mb;
    mb.OpenMenu("File");
    mb.Item("New", "Ctrl+N");
    mb.Item("Open", "Ctrl+O");
    mb.LeafMenu("Open Recent");  // not open this frame
    mb.Item("Exit", "Alt+F4");
    mb.CloseMenu();
    mb.LeafMenu("Edit");  // File is open, Edit isn't (only one menu open at a time in ImGui)

    const dino8::platform::AccessibleNode& root = mb.Root();
    Check(root.children.size() == 2, "one open menu + one closed menu at top level");
    const dino8::platform::AccessibleNode& file = root.children[0];
    Check(file.name == "File" && file.role == dino8::platform::AccessibleRole::Menu, "File is an open Menu node");
    Check(file.children.size() == 4, "File has 4 recorded children (New, Open, Open Recent, Exit)");
    if (file.children.size() == 4) {
      Check(file.children[0].name == "New (Ctrl+N)", "shortcut is folded into the item's name");
      Check(file.children[0].role == dino8::platform::AccessibleRole::MenuItem, "New is a MenuItem");
      Check(file.children[2].name == "Open Recent", "the closed Open Recent submenu is still listed, with no items");
      Check(file.children[2].children.empty(), "Open Recent has no recorded items (it wasn't open)");
    }
  }

  // A menu item with no shortcut is just its label, not "(...)"-suffixed.
  {
    MenuTreeBuilder mb;
    mb.OpenMenu("Help");
    mb.Item("About");
    mb.CloseMenu();
    Check(mb.Root().children[0].children[0].name == "About", "no shortcut -> bare label, no trailing \"()\"");
  }

  // Layers panel: one ListItem per layer, in the order given, with a plain
  // text summary of exactly the state the on-screen Layers table's
  // Cur/On/Lock/Objects columns show for that row.
  {
    std::vector<LayerSummary> layers;
    layers.push_back({"Default", /*current=*/true, /*visible=*/true, /*locked=*/false, /*object_count=*/3});
    layers.push_back({"Hidden Stuff", /*current=*/false, /*visible=*/false, /*locked=*/true, /*object_count=*/0});
    const dino8::platform::AccessibleNode list = BuildLayersPanelNode(layers);
    Check(list.name == "Layers", "layers list is named \"Layers\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "layers list role is List");
    Check(list.children.size() == 2, "two ListItem children, one per layer");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "layer row role is ListItem");
      Check(list.children[0].name.find("Default") != std::string::npos, "row names the layer");
      Check(list.children[0].name.find("current layer") != std::string::npos, "current layer is called out");
      Check(list.children[0].name.find("3 object") != std::string::npos, "object count is present");
      Check(list.children[1].name.find("current layer") == std::string::npos,
            "non-current layer isn't marked current");
      Check(list.children[1].name.find("locked yes") != std::string::npos, "locked state reported");
      Check(list.children[1].name.find("visible no") != std::string::npos, "hidden state reported");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_list = BuildLayersPanelNode({});
    Check(empty_list.children.empty(), "no layers -> no ListItem children");
  }

  // Properties panel: a heading (surfaced as Description) plus one
  // ListItem per "Label: value" entry, matching what DrawPropertiesPanel
  // shows for the current selection (or the document/viewport when nothing
  // is selected).
  {
    std::vector<PropertyEntry> entries = {{"Name", "Box01"}, {"Locked", "No"}};
    const dino8::platform::AccessibleNode list = BuildPropertiesPanelNode("1 object selected", entries);
    Check(list.name == "Properties", "properties list is named \"Properties\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "properties list role is List");
    Check(list.description == "1 object selected", "heading is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per property");
    if (list.children.size() == 2) {
      Check(list.children[0].name == "Name: Box01", "entry rendered as \"Label: value\"");
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "property row role is ListItem");
    }
  }

  // BuildAccessibleTree accepts extra top-level regions (menu bar, panels)
  // alongside the always-present command line - the shape the live bridge
  // (Accessibility.cpp::UpdateAccessibility) assembles every frame.
  {
    dino8::platform::AccessibleNode menu_bar;
    menu_bar.name = "Menu Bar";
    menu_bar.role = dino8::platform::AccessibleRole::MenuBar;
    dino8::platform::AccessibleNode layers = BuildLayersPanelNode({});
    dino8::platform::AccessibleNode props = BuildPropertiesPanelNode("No selection", {});

    const dino8::platform::AccessibleNode root =
        BuildAccessibleTree("Dino8", "Command: ", "", {}, {menu_bar, layers, props});
    Check(root.children.size() == 4, "command line + menu bar + layers + properties = 4 top-level children");
    if (root.children.size() == 4) {
      Check(root.children[0].role == AccessibleRole::Log, "child 0 is still the command line");
      Check(root.children[1].role == dino8::platform::AccessibleRole::MenuBar, "child 1 is the menu bar");
      Check(root.children[2].name == "Layers", "child 2 is the layers panel");
      Check(root.children[3].name == "Properties", "child 3 is the properties panel");
    }
  }

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
