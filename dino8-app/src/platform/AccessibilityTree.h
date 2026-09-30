// The accessibility tree Dino 8 exposes to assistive technology (currently:
// AT-SPI2 on Linux - see AccessibilityLinux.cpp) kept here as plain,
// platform-independent data so its shape can be unit-tested (see
// tests/test_accessibility_tree.cpp) without a display, a D-Bus session, or
// even a build of the AT-SPI bridge itself.
//
// Scope (see docs/ACCESSIBILITY.md for how this grew): the command line's
// live typed text and full command-history log, the main menu bar (mirrored
// live from exactly what ui/MenuBar.cpp draws each frame - see
// MenuTreeBuilder below), the currently running command's options (the
// clickable chips Application.cpp's DrawCommandLine draws next to the
// prompt - see BuildCommandOptionsNode), the Layers and Properties panels'
// current content (Properties' editable rows are flagged as such - see
// PropertyEntry::editable), each viewport's title/view-menu button state
// (name, active/maximized, current display mode - see BuildViewportsPanelNode),
// the persisted Activity Log of finalized edits (see BuildActivityLogNode),
// the document's saved Named Views (see BuildNamedViewsNode), its saved
// Named CPlanes (see BuildNamedCPlanesNode), and its Linetypes (see
// BuildLinetypesNode).
// The 3D viewport's own rendered content and the ~36 other panels/dialogs are
// still not mirrored into this tree.
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
// is selected), reduced to plain text. `editable` is true for a row that
// mirrors an actual editable widget in DrawPropertiesPanel (a text field,
// checkbox or combo the user can change - e.g. Name, Locked, Layer) rather
// than a read-only fact (e.g. Type, or anything shown when nothing is
// selected) - see BuildPropertiesPanelNode for how that's surfaced.
struct PropertyEntry {
  std::string label;
  std::string value;
  bool editable = false;
};

// Builds the "Properties" List accessible: `heading` names what the
// properties describe (e.g. "3 objects selected", "No selection"), and one
// ListItem per entry follows. An `editable` entry gets a Description noting
// it can be changed and how (matching the real widget DrawPropertiesPanel
// draws for it - free text, or a fixed Yes/No choice), so a screen-reader
// user knows which values are just facts and which are actually editable
// controls, without yet giving AT-SPI a way to perform that edit itself
// (see docs/ACCESSIBILITY.md's "known gaps": still a read-only view).
AccessibleNode BuildPropertiesPanelNode(const std::string& heading, const std::vector<PropertyEntry>& entries);

// One option currently offered by the running command (Command.h's
// OptionSpec, reduced to plain data and kept here rather than in Command.h,
// the same way LayerSummary/PropertyEntry above keep this module
// independent of doc/Document and command state): e.g. {"Radius", "5", {},
// true, false} for a numeric Radius option, or {"Mode", "Lines",
// {"Lines","Arcs"}, false, false} for a value cycled through a fixed list.
struct CommandOptionSummary {
  std::string name;
  std::string value;
  std::vector<std::string> choices;
  bool numeric = false;
  bool toggle = false;
};

// Builds the "Command Options" List accessible: one ListItem per option the
// currently running command offers (Application.cpp's DrawCommandLine draws
// these as clickable chips next to the prompt), named the same way the chip
// is labelled ("Radius=5", or just "Diameter" when it has no value yet) and
// described with exactly how to change it - the same guidance
// DrawCommandLine's per-chip tooltip gives a sighted mouse user (click to
// toggle / click to cycle through its choices / click then type a new value
// / type the option's name), reduced to plain text. No children (an empty
// list, not a missing accessible) when no command is running or the running
// command offers no options right now - honestly matching "nothing to show"
// rather than omitting the accessible.
AccessibleNode BuildCommandOptionsNode(const std::vector<CommandOptionSummary>& options);

// One viewport's title/view-menu button state (Viewport.h's Name/IsActive/
// Maximized/Mode, the latter already reduced to plain text via
// DisplayModeName - kept here as plain data, independent of viewport/Viewport.h,
// the same way LayerSummary/PropertyEntry/CommandOptionSummary above keep
// this module independent of their own owning subsystem).
struct ViewportSummary {
  std::string name;
  bool active = false;
  bool maximized = false;
  std::string display_mode;
};

// Builds the "Viewports" List accessible: one ListItem per viewport (Top,
// Front, Right, Perspective, ...), in the same order Application::Viewports()
// holds them, naming which one currently has input focus, whether it is
// maximized (the other viewports are hidden while any one is), and its
// current display mode - the same facts the on-screen title-pill/view-menu
// button and the display-mode label in each viewport's corner show
// (Viewport.cpp's title-overlay block), independent of which viewport window
// happens to be visible right now, the same way the Layers/Properties lists
// above don't depend on their own panel being open.
AccessibleNode BuildViewportsPanelNode(const std::vector<ViewportSummary>& viewports);

// One Activity Log row (doc/Document.h's ActivityLogEntry, reduced to plain
// data the same way LayerSummary/PropertyEntry/CommandOptionSummary/
// ViewportSummary above keep this module independent of doc/Document): the
// wall-clock UTC time a finalized edit's FinalizePending ran, the
// BeginChange/BeginChangeForObjects label (e.g. "Move", "Delete"), and the
// object-count summary (e.g. "+2 -0 ~3 object(s) [ids 12,13,14,...]") -
// exactly the three facts the on-screen Activity Log panel's Time/Action/
// Detail columns show for that row.
struct ActivityLogSummary {
  std::string timestamp_utc;
  std::string label;
  std::string summary;
};

// Builds the "Activity Log" List accessible: one ListItem per recorded edit,
// in the same order Document::ActivityLog() holds them (oldest first), each
// naming its timestamp, action label and summary as one line of plain text -
// the persisted, structured record of every finalized edit, independent of
// the command line's raw text-history log (which mirrors CommandEngine
// output, not Document::ActivityLog) and of whether the Activity Log panel
// window is actually open right now.
AccessibleNode BuildActivityLogNode(const std::vector<ActivityLogSummary>& entries);

// One Named Views panel row (doc/Document.h's NamedView, reduced to plain
// data the same way LayerSummary/PropertyEntry/CommandOptionSummary/
// ViewportSummary/ActivityLogSummary above keep this module independent of
// doc/Document): just the saved view's name, matching the on-screen Named
// Views panel, which likewise shows only the name per row (no camera detail)
// - see DrawNamedViewsPanel.
struct NamedViewSummary {
  std::string name;
};

// Builds the "Named Views" List accessible: one ListItem per saved view, in
// the same order Document::NamedViews() holds them, named the same as the
// on-screen ##NamedViews Selectable row - independent of whether the Named
// Views panel window is actually open right now.
AccessibleNode BuildNamedViewsNode(const std::vector<NamedViewSummary>& views);

// One Named CPlanes panel row (doc/Document.h's NamedCPlane, reduced to
// plain data the same way NamedViewSummary above keeps this module
// independent of doc/Document): just the saved construction plane's name,
// matching the on-screen Named CPlanes panel, which likewise shows only the
// name per row (origin/axes are a hover tooltip there, not part of the row
// itself) - see DrawNamedCPlanesPanel.
struct NamedCPlaneSummary {
  std::string name;
};

// Builds the "Named CPlanes" List accessible: one ListItem per saved
// construction plane, in the same order Document::NamedCPlanes() holds
// them, named the same as the on-screen ##ncp Selectable row - independent
// of whether the Named CPlanes panel window is actually open right now, the
// same way BuildNamedViewsNode doesn't depend on its own panel.
AccessibleNode BuildNamedCPlanesNode(const std::vector<NamedCPlaneSummary>& cplanes);

// One Linetypes-panel row (doc/Document.h's Linetype, reduced to plain data
// the same way LayerSummary/NamedViewSummary/NamedCPlaneSummary above keep
// this module independent of doc/Document): the linetype's name and its
// dash/gap pattern already rendered as plain text ("5, 2" or "continuous"
// for an empty pattern) - the same two facts the on-screen Linetypes
// panel's Name/Pattern columns show per row (see DrawLinetypesPanel).
struct LinetypeSummary {
  std::string name;
  std::string pattern_text;
};

// Builds the "Linetypes" List accessible: one ListItem per linetype, in the
// same order Document::Linetypes() holds them, each named after it with a
// Description giving its dash pattern - independent of whether the
// Linetypes panel window is actually open right now, the same way the
// other panel-backed regions above don't depend on their own panel window.
AccessibleNode BuildLinetypesNode(const std::vector<LinetypeSummary>& linetypes);

}  // namespace dino8::platform
