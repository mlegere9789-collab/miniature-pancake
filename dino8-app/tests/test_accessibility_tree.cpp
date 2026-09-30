// Verifies the internal accessibility-tree mirror (platform/AccessibilityTree.h)
// that AccessibilityLinux.cpp's real AT-SPI2 bridge publishes over D-Bus:
// the command line's tree shape (one Application root, one Log child) and
// the exact text that child reports - history lines, then prompt, then live
// input - match what the on-screen ##CommandLine window shows (see
// Application.cpp's DrawCommandLine); the menu-bar mirror MenuTreeBuilder
// produces (see ui/MenuBar.cpp) matches exactly what its Begin/EndMenu/Item
// calls were; the Layers/Properties panel builders produce the plain text a
// screen reader should hear for a given layer/property list, including
// which Properties rows are flagged as real editable widgets; the
// command-options builder produces the same click/type guidance
// DrawCommandLine's option-chip tooltips give a sighted mouse user, in plain
// text, for whatever the running command's Command::options currently are;
// the viewports builder produces one row per viewport naming which one is
// active, whether it's maximized, and its current display mode, matching
// Viewport.cpp's title-overlay pill and corner display-mode label; the
// activity-log builder produces one row per recorded edit naming its
// timestamp, action label and object-count summary, matching
// Document::ActivityLog's persisted, structured edit history (distinct from
// the command line's own raw text log); the named-views builder produces one
// row per saved view naming it, matching Document::NamedViews; and the
// named-cplanes builder produces one row per saved construction plane naming
// it, matching Document::NamedCPlanes; the linetypes builder produces one
// row per linetype naming it with its dash pattern as the Description,
// matching Document::Linetypes; the materials builder produces one row per
// material naming it with its diffuse colour as the Description, matching
// Document::Materials; the clipping-planes builder produces one row per
// clipping plane naming it with its on/off state and viewport scope,
// matching Document::ClippingPlanes; the layouts builder produces one
// row per layout naming it and which one is active, matching
// Document::Layouts; the block manager builder produces one row per block
// definition naming it with its object and instance counts as the
// Description, matching Document::Blocks; the layer state manager builder
// produces one row per saved layer state naming it with how many layers it
// snapshots as the Description, matching Document::LayerStates; and the
// document user text builder produces one row per document user-text key,
// named after the key with its value as the Description, matching
// Document::UserText.
// This needs no display, no D-Bus session, and no AT-SPI2 build at all, so
// it runs on every platform and every CI job regardless of whether
// AccessibilityLinux.cpp itself was compiled in this build (see
// docs/ACCESSIBILITY.md).
#include <cstdio>
#include <deque>
#include <string>

#include "platform/AccessibilityTree.h"

using dino8::platform::AccessibleRole;
using dino8::platform::ActivityLogSummary;
using dino8::platform::BlockSummary;
using dino8::platform::BuildAccessibleTree;
using dino8::platform::BuildActivityLogNode;
using dino8::platform::BuildBlockManagerNode;
using dino8::platform::BuildCommandLineText;
using dino8::platform::BuildCommandOptionsNode;
using dino8::platform::BuildDocumentUserTextNode;
using dino8::platform::BuildLayerStateManagerNode;
using dino8::platform::BuildLayersPanelNode;
using dino8::platform::BuildPropertiesPanelNode;
using dino8::platform::BuildViewportsPanelNode;
using dino8::platform::CommandOptionSummary;
using dino8::platform::BuildNamedCPlanesNode;
using dino8::platform::BuildNamedViewsNode;
using dino8::platform::BuildLinetypesNode;
using dino8::platform::BuildMaterialsPanelNode;
using dino8::platform::BuildClippingPlanesPanelNode;
using dino8::platform::BuildLayoutsPanelNode;
using dino8::platform::ClippingPlaneSummary;
using dino8::platform::DocumentUserTextSummary;
using dino8::platform::LayerStateSummary;
using dino8::platform::LayerSummary;
using dino8::platform::LayoutSummary;
using dino8::platform::LinetypeSummary;
using dino8::platform::MaterialSummary;
using dino8::platform::MenuTreeBuilder;
using dino8::platform::NamedCPlaneSummary;
using dino8::platform::NamedViewSummary;
using dino8::platform::PropertyEntry;
using dino8::platform::ViewportSummary;

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

  // Properties panel value editors: a row built with editable=true (Name,
  // Layer, Locked, ... - the entries that mirror a real widget in
  // DrawPropertiesPanel's "Object" section) gets a Description explaining
  // it can be changed; a plain fact (Type, Objects/Layers counts, ...) gets
  // none, matching PropertiesPanelAccessibleTree's own editable/non-editable
  // split in ui/Panels.cpp.
  {
    std::vector<PropertyEntry> entries = {{"Name", "Box01", /*editable=*/true}, {"Type", "Box"}};
    const dino8::platform::AccessibleNode list = BuildPropertiesPanelNode("1 object selected", entries);
    Check(list.children.size() == 2, "two ListItem children");
    if (list.children.size() == 2) {
      Check(!list.children[0].description.empty(), "editable entry gets a non-empty Description");
      Check(list.children[0].description.find("Editable") != std::string::npos,
            "editable entry's Description says it's editable");
      Check(list.children[0].description.find("Name") != std::string::npos,
            "editable entry's Description names the field");
      Check(list.children[1].description.empty(), "a plain fact entry (Type) gets no Description");
    }
  }

  // Command options: while a command runs, Application.cpp's DrawCommandLine
  // draws each Command::options entry as a clickable chip next to the
  // prompt; BuildCommandOptionsNode mirrors that as a "Command Options" List,
  // one ListItem per option, named exactly like the chip's label and
  // described with the same click/type guidance the chip's tooltip gives a
  // sighted mouse user - a toggle, a value cycled through fixed choices, a
  // numeric value, and a bare no-value option each get distinct wording.
  {
    std::vector<CommandOptionSummary> options = {
        {"Diameter", "", {}, /*numeric=*/false, /*toggle=*/true},
        {"Mode", "Lines", {"Lines", "Arcs"}, /*numeric=*/false, /*toggle=*/false},
        {"Radius", "5", {}, /*numeric=*/true, /*toggle=*/false},
        {"3Point", "", {}, /*numeric=*/false, /*toggle=*/false},
    };
    const dino8::platform::AccessibleNode list = BuildCommandOptionsNode(options);
    Check(list.name == "Command Options", "command options list is named \"Command Options\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "command options list role is List");
    Check(list.children.size() == 4, "four ListItem children, one per option");
    if (list.children.size() == 4) {
      Check(list.children[0].name == "Diameter", "no-value option is named just its name");
      Check(list.children[0].description.find("toggle") != std::string::npos, "toggle option mentions \"toggle\"");
      Check(list.children[1].name == "Mode=Lines", "option with a value is named \"Name=Value\"");
      Check(list.children[1].description.find("Lines, Arcs") != std::string::npos,
            "choice option's Description lists every choice");
      Check(list.children[2].name == "Radius=5", "numeric option keeps its current value in the name");
      Check(list.children[2].description.find("type a new value") != std::string::npos,
            "numeric option mentions typing a new value");
      Check(list.children[3].description.find("3Point") != std::string::npos,
            "plain option's Description still names it so it can be typed");
      for (const auto& item : list.children) Check(item.role == AccessibleRole::ListItem, "each option row is a ListItem");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_options = BuildCommandOptionsNode({});
    Check(empty_options.name == "Command Options", "still named \"Command Options\" with nothing running");
    Check(empty_options.children.empty(), "no options -> no ListItem children, not a missing accessible");
  }

  // Viewports: one ListItem per viewport (Viewport.cpp's title-overlay pill
  // and view-menu button, and the display-mode label in its corner),
  // independent of which viewport window is actually visible right now -
  // naming which viewport has input focus, whether it's maximized (the
  // others are hidden while any one is), and its current display mode.
  {
    std::vector<ViewportSummary> viewports;
    viewports.push_back({"Perspective", /*active=*/true, /*maximized=*/false, "Shaded"});
    viewports.push_back({"Top", /*active=*/false, /*maximized=*/false, "Wireframe"});
    const dino8::platform::AccessibleNode list = BuildViewportsPanelNode(viewports);
    Check(list.name == "Viewports", "viewports list is named \"Viewports\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "viewports list role is List");
    Check(list.description == "2 viewports", "viewport count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per viewport");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "viewport row role is ListItem");
      Check(list.children[0].name.find("Perspective") != std::string::npos, "row names the viewport");
      Check(list.children[0].name.find("active") != std::string::npos, "active viewport is called out");
      Check(list.children[0].name.find("display mode Shaded") != std::string::npos, "display mode is present");
      Check(list.children[0].name.find("maximized") == std::string::npos,
            "non-maximized viewport isn't marked maximized");
      Check(list.children[1].name.find("active") == std::string::npos, "non-active viewport isn't marked active");
      Check(list.children[1].name.find("display mode Wireframe") != std::string::npos,
            "second viewport's own display mode is present");
    }
  }
  {
    std::vector<ViewportSummary> maximized = {{"Front", /*active=*/true, /*maximized=*/true, "Rendered"}};
    const dino8::platform::AccessibleNode list = BuildViewportsPanelNode(maximized);
    Check(list.children.size() == 1, "one ListItem for a single maximized viewport");
    if (!list.children.empty()) {
      Check(list.children[0].name.find("maximized") != std::string::npos, "maximized viewport is called out");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_viewports = BuildViewportsPanelNode({});
    Check(empty_viewports.name == "Viewports", "still named \"Viewports\" with none given");
    Check(empty_viewports.children.empty(), "no viewports -> no ListItem children");
  }

  // Activity Log: one ListItem per recorded edit, in the order given, each
  // naming its timestamp, action label and object-count summary as one
  // line of plain text - the same three facts the on-screen Activity Log
  // panel's Time/Action/Detail columns show for that row (see
  // Document::ActivityLog/ActivityLogEntry). This is a genuinely separate
  // record from the command line's raw text log: it mirrors Document's
  // persisted, structured edit history, not CommandEngine output.
  {
    std::vector<ActivityLogSummary> entries;
    entries.push_back({"2024-01-01 12:00:00", "Move", "+0 -0 ~2 object(s) [ids 1,2]"});
    entries.push_back({"2024-01-01 12:00:05", "Delete", "+0 -1 ~0 object(s) [ids 3]"});
    const dino8::platform::AccessibleNode list = BuildActivityLogNode(entries);
    Check(list.name == "Activity Log", "activity log list is named \"Activity Log\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "activity log list role is List");
    Check(list.description == "2 entries", "entry count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per recorded edit");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "activity row role is ListItem");
      Check(list.children[0].name.find("2024-01-01 12:00:00") != std::string::npos, "row carries its timestamp");
      Check(list.children[0].name.find("Move") != std::string::npos, "row names its action label");
      Check(list.children[0].name.find("ids 1,2") != std::string::npos, "row carries its object-count summary");
      Check(list.children[1].name.find("Delete") != std::string::npos, "second row names its own action label");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_log = BuildActivityLogNode({});
    Check(empty_log.name == "Activity Log", "still named \"Activity Log\" with no entries yet");
    Check(empty_log.children.empty(), "no entries -> no ListItem children, not a missing accessible");
  }

  // Named Views: one ListItem per saved view, naming it - the same single
  // fact the on-screen Named Views panel shows per row (see
  // DrawNamedViewsPanel, Document::NamedViews/NamedView), independent of the
  // saved camera state itself.
  {
    std::vector<NamedViewSummary> views;
    views.push_back({"Front Elevation"});
    views.push_back({"Roof Study"});
    const dino8::platform::AccessibleNode list = BuildNamedViewsNode(views);
    Check(list.name == "Named Views", "named views list is named \"Named Views\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "named views list role is List");
    Check(list.description == "2 named views", "view count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per saved view");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "named view row role is ListItem");
      Check(list.children[0].name == "Front Elevation", "first row names its saved view");
      Check(list.children[1].name == "Roof Study", "second row names its own saved view");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_views = BuildNamedViewsNode({});
    Check(empty_views.name == "Named Views", "still named \"Named Views\" with no saved views yet");
    Check(empty_views.children.empty(), "no saved views -> no ListItem children, not a missing accessible");
  }

  // Named CPlanes: one ListItem per saved construction plane, naming it -
  // the same single fact the on-screen Named CPlanes panel shows per row
  // (see DrawNamedCPlanesPanel, Document::NamedCPlanes/NamedCPlane),
  // independent of the saved origin/axes themselves (a hover tooltip there,
  // not part of the row).
  {
    std::vector<NamedCPlaneSummary> cplanes;
    cplanes.push_back({"Roof Slope"});
    cplanes.push_back({"Wall A"});
    const dino8::platform::AccessibleNode list = BuildNamedCPlanesNode(cplanes);
    Check(list.name == "Named CPlanes", "named cplanes list is named \"Named CPlanes\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "named cplanes list role is List");
    Check(list.description == "2 named cplanes", "cplane count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per saved cplane");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "named cplane row role is ListItem");
      Check(list.children[0].name == "Roof Slope", "first row names its saved cplane");
      Check(list.children[1].name == "Wall A", "second row names its own saved cplane");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_cplanes = BuildNamedCPlanesNode({});
    Check(empty_cplanes.name == "Named CPlanes", "still named \"Named CPlanes\" with no saved cplanes yet");
    Check(empty_cplanes.children.empty(), "no saved cplanes -> no ListItem children, not a missing accessible");
  }

  // Linetypes: one ListItem per linetype, naming it, with a Description
  // giving its dash pattern as plain text - the same two facts the on-screen
  // Linetypes panel's Name/Pattern columns show per row (see
  // DrawLinetypesPanel, Document::Linetypes/Linetype).
  {
    std::vector<LinetypeSummary> linetypes;
    linetypes.push_back({"Continuous", "continuous"});
    linetypes.push_back({"Dashed", "5, 2"});
    const dino8::platform::AccessibleNode list = BuildLinetypesNode(linetypes);
    Check(list.name == "Linetypes", "linetypes list is named \"Linetypes\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "linetypes list role is List");
    Check(list.description == "2 linetypes", "linetype count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per linetype");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "linetype row role is ListItem");
      Check(list.children[0].name == "Continuous", "first row names its linetype");
      Check(list.children[0].description == "Pattern: continuous", "first row's pattern is given as its Description");
      Check(list.children[1].name == "Dashed", "second row names its own linetype");
      Check(list.children[1].description == "Pattern: 5, 2", "second row's dash pattern is given as its Description");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_linetypes = BuildLinetypesNode({});
    Check(empty_linetypes.name == "Linetypes", "still named \"Linetypes\" with no linetypes at all");
    Check(empty_linetypes.children.empty(), "no linetypes -> no ListItem children, not a missing accessible");
  }

  // Materials: one ListItem per material, naming it, with a Description
  // giving its diffuse colour as plain "R, G, B" text - the one fact the
  // on-screen Materials panel's colour swatch otherwise conveys only
  // visually (see DrawMaterialsPanel, Document::Materials/Material).
  {
    std::vector<MaterialSummary> materials;
    materials.push_back({"Default", "200, 200, 200"});
    materials.push_back({"Steel", "150, 155, 165"});
    const dino8::platform::AccessibleNode list = BuildMaterialsPanelNode(materials);
    Check(list.name == "Materials", "materials list is named \"Materials\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "materials list role is List");
    Check(list.description == "2 materials", "material count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per material");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "material row role is ListItem");
      Check(list.children[0].name == "Default", "first row names its material");
      Check(list.children[0].description == "Colour: 200, 200, 200", "first row's diffuse colour is its Description");
      Check(list.children[1].name == "Steel", "second row names its own material");
      Check(list.children[1].description == "Colour: 150, 155, 165", "second row's own diffuse colour is present");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_materials = BuildMaterialsPanelNode({});
    Check(empty_materials.name == "Materials", "still named \"Materials\" with no materials at all");
    Check(empty_materials.children.empty(), "no materials -> no ListItem children, not a missing accessible");
  }

  // Clipping Planes: one ListItem per plane, naming it with its on/off state
  // folded into the name and a Description giving its viewport scope -
  // "every viewport" vs. a count of specifically chosen ones - the same two
  // facts DrawClippingPlanesPanel's row checkbox and hover tooltip give a
  // sighted user (see Document::ClippingPlanes/ClippingPlane).
  {
    std::vector<ClippingPlaneSummary> planes;
    planes.push_back({"Section A", /*enabled=*/true, /*clips_every_viewport=*/true, /*clipped_viewport_count=*/0});
    planes.push_back({"Section B", /*enabled=*/false, /*clips_every_viewport=*/false, /*clipped_viewport_count=*/2});
    const dino8::platform::AccessibleNode list = BuildClippingPlanesPanelNode(planes);
    Check(list.name == "Clipping Planes", "clipping planes list is named \"Clipping Planes\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "clipping planes list role is List");
    Check(list.description == "2 clipping planes", "plane count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per clipping plane");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "clipping plane row role is ListItem");
      Check(list.children[0].name == "Section A, on", "on-state plane's name reports \"on\"");
      Check(list.children[0].description == "Clips every viewport", "no chosen viewports -> \"every viewport\"");
      Check(list.children[1].name == "Section B, off", "off-state plane's name reports \"off\"");
      Check(list.children[1].description == "Clips 2 viewports", "specific viewport count is reported");
    }
  }
  {
    std::vector<ClippingPlaneSummary> one = {{"Only", true, false, 1}};
    const dino8::platform::AccessibleNode list = BuildClippingPlanesPanelNode(one);
    Check(list.children.size() == 1 && list.children[0].description == "Clips 1 viewport",
          "a single chosen viewport is singular, not \"1 viewports\"");
  }
  {
    const dino8::platform::AccessibleNode empty_planes = BuildClippingPlanesPanelNode({});
    Check(empty_planes.name == "Clipping Planes", "still named \"Clipping Planes\" with no planes at all");
    Check(empty_planes.children.empty(), "no clipping planes -> no ListItem children, not a missing accessible");
  }

  // Layouts: one ListItem per layout, naming it, with the currently active
  // one called out in the name - DrawLayoutsPanel only distinguishes it by
  // selection highlight, so a screen reader needs it spelled out (see
  // Document::Layouts/Layout, Application::ActiveLayoutIndex).
  {
    std::vector<LayoutSummary> layouts;
    layouts.push_back({"Layout 1", /*active=*/true});
    layouts.push_back({"Layout 2", /*active=*/false});
    const dino8::platform::AccessibleNode list = BuildLayoutsPanelNode(layouts);
    Check(list.name == "Layouts", "layouts list is named \"Layouts\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "layouts list role is List");
    Check(list.description == "2 layouts", "layout count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per layout");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "layout row role is ListItem");
      Check(list.children[0].name == "Layout 1, active", "active layout is called out in its name");
      Check(list.children[1].name == "Layout 2", "non-active layout isn't marked active");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_layouts = BuildLayoutsPanelNode({});
    Check(empty_layouts.name == "Layouts", "still named \"Layouts\" with no layouts at all");
    Check(empty_layouts.children.empty(), "no layouts -> no ListItem children, not a missing accessible");
  }

  // Block Manager: one ListItem per block definition, naming it, with a
  // Description giving its object and instance counts - the same two facts
  // DrawBlockManagerPanel's Objects/Instances columns show per row (see
  // Document::Blocks/BlockDefinition).
  {
    std::vector<BlockSummary> blocks;
    blocks.push_back({"Widget", 2, 5});
    blocks.push_back({"Gadget", 1, 1});
    const dino8::platform::AccessibleNode list = BuildBlockManagerNode(blocks);
    Check(list.name == "Block Manager", "block manager list is named \"Block Manager\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "block manager list role is List");
    Check(list.description == "2 block definitions", "block count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per block definition");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "block row role is ListItem");
      Check(list.children[0].name == "Widget", "first row names its block");
      Check(list.children[0].description == "2 objects, 5 instances", "first row's object/instance counts are its Description");
      Check(list.children[1].name == "Gadget", "second row names its own block");
      Check(list.children[1].description == "1 object, 1 instance", "singular object/instance counts aren't pluralized");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_blocks = BuildBlockManagerNode({});
    Check(empty_blocks.name == "Block Manager", "still named \"Block Manager\" with no blocks at all");
    Check(empty_blocks.children.empty(), "no blocks -> no ListItem children, not a missing accessible");
  }

  // Layer State Manager: one ListItem per saved layer state, naming it,
  // with a Description giving how many layers it snapshots - a fact the
  // on-screen row (a bare Selectable naming the state) doesn't itself show
  // (see Document::LayerStates/LayerState).
  {
    std::vector<LayerStateSummary> states;
    states.push_back({"Plan View", 3});
    states.push_back({"Solo Roof", 1});
    const dino8::platform::AccessibleNode list = BuildLayerStateManagerNode(states);
    Check(list.name == "Layer State Manager", "layer state manager list is named \"Layer State Manager\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "layer state manager list role is List");
    Check(list.description == "2 layer states", "state count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per saved state");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "layer state row role is ListItem");
      Check(list.children[0].name == "Plan View", "first row names its saved state");
      Check(list.children[0].description == "3 layers", "first row's layer count is its Description");
      Check(list.children[1].name == "Solo Roof", "second row names its own saved state");
      Check(list.children[1].description == "1 layer", "a single-layer state is singular, not \"1 layers\"");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_states = BuildLayerStateManagerNode({});
    Check(empty_states.name == "Layer State Manager", "still named \"Layer State Manager\" with no states at all");
    Check(empty_states.children.empty(), "no saved states -> no ListItem children, not a missing accessible");
  }

  // Document User Text: one ListItem per document user-text key, named
  // after the key, with its value as the Description - the same "key =
  // value" fact DrawDocumentUserTextPanel's own row shows per entry (see
  // Document::UserText).
  {
    std::vector<DocumentUserTextSummary> entries;
    entries.push_back({"Project", "Lakeside Cabin"});
    entries.push_back({"Revision", "3"});
    const dino8::platform::AccessibleNode list = BuildDocumentUserTextNode(entries);
    Check(list.name == "Document User Text", "document user text list is named \"Document User Text\"");
    Check(list.role == dino8::platform::AccessibleRole::List, "document user text list role is List");
    Check(list.description == "2 document user text entries", "entry count is carried as the list's Description");
    Check(list.children.size() == 2, "two ListItem children, one per key");
    if (list.children.size() == 2) {
      Check(list.children[0].role == dino8::platform::AccessibleRole::ListItem, "user text row role is ListItem");
      Check(list.children[0].name == "Project", "first row is named after its key");
      Check(list.children[0].description == "Lakeside Cabin", "first row's value is its Description");
      Check(list.children[1].name == "Revision", "second row is named after its own key");
      Check(list.children[1].description == "3", "second row's value is present");
    }
  }
  {
    const dino8::platform::AccessibleNode empty_user_text = BuildDocumentUserTextNode({});
    Check(empty_user_text.name == "Document User Text", "still named \"Document User Text\" with no keys at all");
    Check(empty_user_text.children.empty(), "no keys -> no ListItem children, not a missing accessible");
  }

  // BuildAccessibleTree accepts extra top-level regions (menu bar, panels)
  // alongside the always-present command line - the shape the live bridge
  // (Accessibility.cpp::UpdateAccessibility) assembles every frame.
  {
    dino8::platform::AccessibleNode menu_bar;
    menu_bar.name = "Menu Bar";
    menu_bar.role = dino8::platform::AccessibleRole::MenuBar;
    dino8::platform::AccessibleNode cmd_options = BuildCommandOptionsNode({});
    dino8::platform::AccessibleNode layers = BuildLayersPanelNode({});
    dino8::platform::AccessibleNode props = BuildPropertiesPanelNode("No selection", {});
    dino8::platform::AccessibleNode viewports = BuildViewportsPanelNode({});
    dino8::platform::AccessibleNode activity_log = BuildActivityLogNode({});
    dino8::platform::AccessibleNode named_views = BuildNamedViewsNode({});
    dino8::platform::AccessibleNode named_cplanes = BuildNamedCPlanesNode({});
    dino8::platform::AccessibleNode linetypes = BuildLinetypesNode({});
    dino8::platform::AccessibleNode materials = BuildMaterialsPanelNode({});
    dino8::platform::AccessibleNode clipping_planes = BuildClippingPlanesPanelNode({});
    dino8::platform::AccessibleNode layouts = BuildLayoutsPanelNode({});
    dino8::platform::AccessibleNode block_manager = BuildBlockManagerNode({});
    dino8::platform::AccessibleNode layer_state_manager = BuildLayerStateManagerNode({});
    dino8::platform::AccessibleNode document_user_text = BuildDocumentUserTextNode({});

    const dino8::platform::AccessibleNode root =
        BuildAccessibleTree("Dino8", "Command: ", "", {},
                             {menu_bar, cmd_options, layers, props, viewports, activity_log, named_views,
                              named_cplanes, linetypes, materials, clipping_planes, layouts, block_manager,
                              layer_state_manager, document_user_text});
    Check(root.children.size() == 16,
          "command line + menu bar + command options + layers + properties + viewports + activity log + "
          "named views + named cplanes + linetypes + materials + clipping planes + layouts + block manager + "
          "layer state manager + document user text = 16 top-level children");
    if (root.children.size() == 16) {
      Check(root.children[0].role == AccessibleRole::Log, "child 0 is still the command line");
      Check(root.children[1].role == dino8::platform::AccessibleRole::MenuBar, "child 1 is the menu bar");
      Check(root.children[2].name == "Command Options", "child 2 is the command options list");
      Check(root.children[3].name == "Layers", "child 3 is the layers panel");
      Check(root.children[4].name == "Properties", "child 4 is the properties panel");
      Check(root.children[5].name == "Viewports", "child 5 is the viewports panel");
      Check(root.children[6].name == "Activity Log", "child 6 is the activity log");
      Check(root.children[7].name == "Named Views", "child 7 is the named views panel");
      Check(root.children[8].name == "Named CPlanes", "child 8 is the named cplanes panel");
      Check(root.children[9].name == "Linetypes", "child 9 is the linetypes panel");
      Check(root.children[10].name == "Materials", "child 10 is the materials panel");
      Check(root.children[11].name == "Clipping Planes", "child 11 is the clipping planes panel");
      Check(root.children[12].name == "Layouts", "child 12 is the layouts panel");
      Check(root.children[13].name == "Block Manager", "child 13 is the block manager panel");
      Check(root.children[14].name == "Layer State Manager", "child 14 is the layer state manager panel");
      Check(root.children[15].name == "Document User Text", "child 15 is the document user text panel");
    }
  }

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
