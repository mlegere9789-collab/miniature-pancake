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
// Named CPlanes (see BuildNamedCPlanesNode), its Linetypes (see
// BuildLinetypesNode), its Materials (see BuildMaterialsPanelNode), its
// Clipping Planes (see BuildClippingPlanesPanelNode), its Layouts (see
// BuildLayoutsPanelNode), its Block Manager (see BuildBlockManagerNode), its
// Layer State Manager (see BuildLayerStateManagerNode), its Document
// User Text (see BuildDocumentUserTextNode), its Lights (see
// BuildLightsPanelNode), its Annotation Styles (see
// BuildAnnotationStylesNode), its Notes (see BuildDocumentNotesNode), its
// render Environment settings (see BuildEnvironmentsPanelNode), the last
// Audit run's results (see BuildAuditResultsNode), its pending Undo/Redo
// history (see BuildUndoHistoryNode/BuildRedoHistoryNode), the loaded Hatch
// Pattern library (see BuildHatchPatternsNode), its loaded plug-ins (see
// BuildPluginsNode), the ~1055-command Rhino 8 reference catalog (see
// BuildCommandListNode), the user's saved command aliases (see
// BuildCommandAliasesNode), the user's customized keyboard shortcuts (see
// BuildKeyboardShortcutsNode), the Options window's own fixed, non-
// customizable built-in-shortcuts reference tab (see
// BuildBuiltinShortcutsNode), the document's units/tolerances/grid/
// metadata Document Properties (see BuildDocumentPropertiesNode), the
// materials that carry an assigned Texture (see BuildTexturesPanelNode),
// and the active viewport/grid/display-tolerance Display settings (see
// BuildDisplayPanelNode).
// The 3D viewport's own rendered content and the ~18 remaining panels/
// dialogs (plus most of the Options window - only its Aliases, Shortcuts
// and built-in-shortcuts reference tabs are covered above; General,
// Modeling Aids, View and Toolbar remain unreached) are still not mirrored
// into this tree.
#pragma once

#include <cstdint>
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

// One Materials-panel row (doc/Document.h's Material, reduced to plain data
// the same way LayerSummary/LinetypeSummary above keep this module
// independent of doc/Document): the material's name and its diffuse colour
// already rendered as plain text ("200, 200, 200") - the one fact the
// on-screen Materials panel's colour swatch conveys visually per row (see
// DrawMaterialsPanel), which a screen reader otherwise has no way to read.
struct MaterialSummary {
  std::string name;
  std::string diffuse_text;
};

// Builds the "Materials" List accessible: one ListItem per material, in the
// same order Document::Materials() holds them, each named after it with a
// Description giving its diffuse colour as plain "R, G, B" text -
// independent of whether the Materials panel window is actually open right
// now, the same way the other panel-backed regions above don't depend on
// their own panel window.
AccessibleNode BuildMaterialsPanelNode(const std::vector<MaterialSummary>& materials);

// One Clipping Planes panel row (doc/Document.h's ClippingPlane, reduced to
// plain data the same way the summaries above keep this module independent
// of doc/Document): the plane's name, whether it's switched on (the
// on-screen row's own checkbox), and which viewports it clips - "every
// viewport" or a count of specifically chosen ones - the same two facts
// DrawClippingPlanesPanel's checkbox and its row's hover tooltip give a
// sighted user (the origin/normal themselves stay tooltip-only, the same way
// Named CPlanes above leaves origin/axes out of its own row).
struct ClippingPlaneSummary {
  std::string name;
  bool enabled = true;
  bool clips_every_viewport = true;   // ClippingPlane::viewports is empty
  int clipped_viewport_count = 0;     // meaningful only when !clips_every_viewport
};

// Builds the "Clipping Planes" List accessible: one ListItem per clipping
// plane, in the same order Document::ClippingPlanes() holds them, each named
// after it with a Description giving its on/off state and viewport scope -
// independent of whether the Clipping Planes panel window is actually open
// right now, the same way the other panel-backed regions above don't depend
// on their own panel window.
AccessibleNode BuildClippingPlanesPanelNode(const std::vector<ClippingPlaneSummary>& planes);

// One Layouts panel row (doc/Document.h's Layout, reduced to plain data the
// same way the summaries above keep this module independent of doc/Document):
// the layout's name and whether it's the one currently active in the Layouts
// panel - DrawLayoutsPanel marks the active row only by selection highlight,
// not text, so a screen reader needs it spelled out the same way
// BuildViewportsPanelNode spells out which viewport is active. The page size
// and its details stay only in the expanded editor for the active layout,
// matching what the panel's own top-level row shows.
struct LayoutSummary {
  std::string name;
  bool active = false;
};

// Builds the "Layouts" List accessible: one ListItem per layout, in the same
// order Document::Layouts() holds them, naming which one is currently active
// - independent of whether the Layouts panel window is actually open right
// now, the same way the other panel-backed regions above don't depend on
// their own panel window. This does not include the always-present "Model"
// layout DrawLayoutsPanel itself lists first, since it isn't a
// Document::Layouts() entry - see Application::ActiveLayoutIndex()'s own -1
// "Model" convention.
AccessibleNode BuildLayoutsPanelNode(const std::vector<LayoutSummary>& layouts);

// One Block Manager row (doc/Document.h's BlockDefinition, reduced to plain
// data the same way the summaries above keep this module independent of
// doc/Document): the block's name, how many objects its own definition
// holds, and how many placed instances currently reference it - the same
// three facts DrawBlockManagerPanel's Name/Objects/Instances columns show
// (see cmd_drafting.cpp), independent of the objects/instances themselves.
struct BlockSummary {
  std::string name;
  int object_count = 0;
  int instance_count = 0;
};

// Builds the "Block Manager" List accessible: one ListItem per block
// definition, in the same order Document::Blocks() holds them, each named
// after it with a Description giving its object and instance counts -
// independent of whether the Block Manager panel window is actually open
// right now, the same way the other panel-backed regions above don't
// depend on their own panel window.
AccessibleNode BuildBlockManagerNode(const std::vector<BlockSummary>& blocks);

// One Layer State Manager row (doc/Document.h's LayerState, reduced to
// plain data the same way the summaries above keep this module independent
// of doc/Document): the saved state's name and how many layers it records
// a visible/locked snapshot for - a fact DrawLayerStateManager's own row (a
// bare Selectable naming the state) doesn't show at all, so this mirror
// gives a screen-reader user more than a sighted user gets from the row
// itself, not less.
struct LayerStateSummary {
  std::string name;
  int layer_count = 0;
};

// Builds the "Layer State Manager" List accessible: one ListItem per saved
// layer state, in the same order Document::LayerStates() holds them, each
// named after it with a Description giving how many layers it snapshots -
// independent of whether the Layer State Manager panel window is actually
// open right now, the same way the other panel-backed regions above don't
// depend on their own panel window.
AccessibleNode BuildLayerStateManagerNode(const std::vector<LayerStateSummary>& states);

// One Document User Text row (Document::UserText(), a
// std::map<std::string, std::string> reduced to plain data the same way the
// summaries above keep this module independent of doc/Document): a key and
// its value, the same two facts DrawDocumentUserTextPanel's own "key =
// value" row shows per entry.
struct DocumentUserTextSummary {
  std::string key;
  std::string value;
};

// Builds the "Document User Text" List accessible: one ListItem per
// document user-text key, in the same order Document::UserText() (a
// std::map, so already key-sorted) holds them, named after its key with a
// Description giving its value - independent of whether the Document User
// Text panel window is actually open right now, the same way the other
// panel-backed regions above don't depend on their own panel window.
AccessibleNode BuildDocumentUserTextNode(const std::vector<DocumentUserTextSummary>& entries);

// One Lights-panel row (doc/Document.h's Light, reduced to plain data the
// same way the summaries above keep this module independent of
// doc/Document): the light's name, its type (Point/Spot/Directional/
// Rectangular/Linear) and whether it's switched on - the same facts
// DrawLightsPanel's own collapsed row shows without needing to expand it
// (the tree-node label reads "name (type)"; the checkbox beside it is the
// on/off state). Colour, intensity, position and direction stay in the
// expanded editor only, the same way Materials' gloss/reflectivity/
// transparency/texture stay out of its own row mirror.
struct LightSummary {
  std::string name;
  std::string type_text;
  bool enabled = true;
};

// Builds the "Lights" List accessible: one ListItem per document light, in
// the same order Document::Lights() holds them, each named after it with
// its type and on/off state folded into the name (e.g. "Key Light (Point),
// on") the same way BuildClippingPlanesPanelNode folds on/off into its own
// item's name - independent of whether the Lights panel window is actually
// open right now, the same way the other panel-backed regions above don't
// depend on their own panel window.
AccessibleNode BuildLightsPanelNode(const std::vector<LightSummary>& lights);

// One Annotation Styles row (doc/Document.h's AnnotationStyle, reduced to
// plain data the same way the summaries above keep this module independent
// of doc/Document): the style's name, whether it's the document's current
// style (DocumentSettings::annotation_style, picked from the "Current
// style" combo in DrawDocumentPropertiesWindow - shown by selection there,
// not by text, so spelled out here the same way BuildLayoutsPanelNode
// spells out which layout is active), and its text height/arrow size/font
// as plain text - facts that only appear once a style's own TreeNode row is
// expanded, so this mirror gives a screen-reader user more than the
// collapsed on-screen list itself shows, the same honest trade
// BuildLayerStateManagerNode makes for its own row.
struct AnnotationStyleSummary {
  std::string name;
  bool current = false;
  std::string text_height_text;  // e.g. "2.5" or "Auto (twice the grid spacing)"
  std::string arrow_size_text;   // e.g. "1" or "Auto (text height)"
  std::string font_text;         // e.g. "Arial" or "Default (first system sans-serif found)"
};

// Builds the "Annotation Styles" List accessible: one ListItem per style, in
// the same order Document::AnnotationStyles() holds them, naming which one
// is current with a Description giving its text height, arrow size and font
// - independent of whether the Document Properties window is actually open
// right now, the same way the other panel-backed regions above don't depend
// on their own panel window.
AccessibleNode BuildAnnotationStylesNode(const std::vector<AnnotationStyleSummary>& styles);

// Builds the "Document Notes" accessible: a single Log/Text object (the
// same role and shape as the "Command Line" accessible - see
// BuildCommandLineText) whose text is exactly Document::Notes(), the same
// plain string DrawNotesPanel's multiline text box edits - independent of
// whether the Notes panel window is actually open right now, the same way
// the other panel-backed regions above don't depend on their own panel
// window. Unlike every List region above, this is a single text value with
// no children, matching what the on-screen widget itself is: one editable
// block of free text, not a collection of rows.
AccessibleNode BuildDocumentNotesNode(const std::string& notes);

// Builds the "Environments" List accessible: one ListItem per Label: value
// fact about Document::Render()'s RenderSettings - reusing the same
// label/value PropertyEntry shape BuildPropertiesPanelNode already uses for
// the Properties panel's own facts, just under a different accessible name.
// The caller (ui::EnvironmentsAccessibleTree, RenderPanels.cpp) supplies one
// entry for the background type (plus its colour, gradient colours or image
// path, whichever DrawEnvironmentsPanel's "Background" section shows for the
// current type), and one entry each for ground plane on/off, height, colour
// and shadows, and sun/sky on/off, azimuth, altitude and skylight - the same
// facts DrawEnvironmentsPanel's "Background", "Ground plane" and "Sun and
// sky" sections show. Independent of whether the Environments panel window
// is actually open right now, the same way the other panel-backed regions
// above don't depend on their own panel window. A screen-reader user can
// change any of these entirely from the command line (Environments/
// GroundPlane/Sun, see cmd_render.cpp) and confirm the result without
// needing to see the panel at all.
AccessibleNode BuildEnvironmentsPanelNode(const std::vector<PropertyEntry>& entries);

// One invalid object found by Audit/Check (app/Application.h's AuditIssue,
// reduced to plain data the same way the summaries above keep this module
// independent of app::Application): the object's id, its kind name
// (ObjectKindName(o.kind)) and OpenNURBS' own IsValid(ON_TextLog*) failure
// text - the same three facts the on-screen Audit Results panel's Type and
// Problem columns show per row (see DrawAuditResultsPanel).
struct AuditIssueSummary {
  std::uint64_t id = 0;
  std::string type;
  std::string description;
};

// Builds the "Audit Results" List accessible: one ListItem per invalid
// object found by the last Audit run, in the same order
// Application::AuditResults() holds them, naming the object's id and type
// with its failure description as the Description - independent of whether
// the Audit Results panel window is actually open right now, the same way
// the other panel-backed regions above don't depend on their own panel
// window. Starts empty until a screen-reader user runs Audit from the
// command line (see cmd_analyze.cpp), the same "starts empty, gains rows"
// shape Named Views/CPlanes already use.
AccessibleNode BuildAuditResultsNode(const std::vector<AuditIssueSummary>& issues);

// Builds the "Undo History" List accessible: one ListItem per pending-or-undoable
// edit, in the same order Document::UndoLabels() holds them (most recent
// first, the pending in-progress edit if any first of all) - the same
// numbered "1. Line" rows DrawUndoMultipleWindow(app, /*redo=*/false) shows,
// so a screen-reader user can read exactly how many steps a given Undo would
// need to reach any past edit. Independent of whether the Undo panel window
// is actually open right now, the same way the other panel-backed regions
// above don't depend on their own panel window. Starts empty on a fresh
// document, the same "starts empty, gains rows" shape Named Views/CPlanes
// and Audit Results already use.
AccessibleNode BuildUndoHistoryNode(const std::vector<std::string>& labels);

// Builds the "Redo History" List accessible: the Redo-direction counterpart
// of BuildUndoHistoryNode, one ListItem per label in Document::RedoLabels()
// order, matching DrawUndoMultipleWindow(app, /*redo=*/true)'s own numbered
// rows. Empties out the moment a fresh edit is made after an Undo, the same
// way the on-screen Redo panel does.
AccessibleNode BuildRedoHistoryNode(const std::vector<std::string>& labels);

// One Hatch Patterns entry (drafting::HatchLibrary's HatchPattern, reduced
// to plain data the same way the summaries above keep this module
// independent of its owning subsystem): the pattern's name and its own
// human-readable description text (HatchPattern::description, e.g. "ANSI
// Iron, Brick, Stone masonry" - already plain text parsed straight out of
// the .pat file, unlike Linetypes' dash array which BuildLinetypesNode has
// to render into text itself).
struct HatchPatternSummary {
  std::string name;
  std::string description;
};

// Builds the "Hatch Patterns" List accessible: one ListItem per pattern, in
// the same order HatchLibrary::Instance().Patterns() holds them, each named
// after it with a Description giving its own descriptive text - the same
// two facts DrawHatchPatternsPanel's thumbnail grid conveys visually per
// pattern (name below the thumbnail, description as its hover tooltip),
// independent of whether the Hatch Patterns panel window is actually open
// right now, the same way the other panel-backed regions above don't depend
// on their own panel window. Non-empty from application startup (the
// library always carries its built-in patterns even with no data/*.pat
// files present), unlike the "starts empty" regions above.
AccessibleNode BuildHatchPatternsNode(const std::vector<HatchPatternSummary>& patterns);

// One loaded plug-in (plugins::Manager's LoadedPlugin, reduced to plain data
// the same way the summaries above keep this module independent of its
// owning subsystem): its name and version, whether it loaded successfully,
// the load error text when it didn't, and how many commands and Dino Flow
// nodes it registered - the same facts DrawPlugInManagerPanel's
// Name/Version/Commands/Flow Nodes/Status table columns show per row
// (Status's own hover-tooltip error text folded in directly here, since
// AT-SPI has no per-cell tooltip to mirror it into).
struct PluginSummary {
  std::string name;
  std::string version;
  bool loaded_ok = true;
  std::string error;
  int command_count = 0;
  int flow_node_count = 0;
};

// Builds the "Plug-ins" List accessible: one ListItem per loaded plug-in, in
// the same order plugins::Manager::Get().Plugins() holds them, each named
// after its name/version and Loaded/Error status with a Description giving
// its command and flow-node counts (plus the error text when it failed to
// load) - independent of whether the Plug-in Manager panel window is
// actually open right now, the same way the other panel-backed regions
// above don't depend on their own panel window. Not empty by default in a
// real build: `Application`'s own constructor runs
// `plugins::Manager::ScanDefaultFolders` at startup (see Application.cpp),
// so any plug-in already sitting in the config/exe-dir plugins folders
// (including this repository's own bundled example plug-ins) is already
// loaded and listed here before a screen-reader user does anything - the
// same "never empty" shape Hatch Patterns uses, not the "starts empty,
// gains rows" shape the document-scoped regions above use. It's the
// Plug-in Manager panel's own Load File.../Rescan Folders buttons that add
// more entries after that.
AccessibleNode BuildPluginsNode(const std::vector<PluginSummary>& plugins);

// One Command List row (CommandEngine's RegisteredCommand, reduced to plain
// data the same way the summaries above keep this module independent of
// commands/CommandEngine): the command's canonical name, its status as
// plain text (CommandEngine.h's CommandStatusName - "Implemented",
// "Partial" or "Planned", kept as text here rather than the enum itself so
// this module stays independent of commands/CommandEngine), and its
// description - its real CommandInfo description when it has one, or its
// internal engineering note otherwise, exactly the `rc.info ?
// rc.info->description : rc.note` choice DrawCommandListPanel/DrawHelpPanel
// already make.
struct CommandListEntrySummary {
  std::string name;
  std::string status_text;
  std::string description;
};

// Builds the "Command List" List accessible: one ListItem per registered
// command, in the same order CommandEngine::Registry() (a std::map, so
// already name-sorted) holds them, each named after the command with a
// Description combining its status and description text - the same facts
// DrawCommandListPanel's Command/Status/Description table columns show per
// row. The list's own Description gives the total command count plus an
// implemented/partial/planned breakdown, matching DrawCommandListPanel's own
// header text. Independent of whether the Command List panel window is
// actually open right now, the same way the other panel-backed regions
// above don't depend on their own panel window. Never empty in a real
// build: the ~1055-command Rhino 8 reference catalog is registered as
// placeholders at startup even for commands with no implementation yet (see
// CommandEngine::RegisterCatalogPlaceholders), the same "never empty" shape
// Hatch Patterns/Plug-ins use.
AccessibleNode BuildCommandListNode(const std::vector<CommandListEntrySummary>& commands);

// One Command Aliases row (CommandEngine::Aliases(), a
// std::map<std::string, std::string> reduced to plain data the same way
// DocumentUserTextSummary above keeps this module independent of
// doc/Document): an alias and the full command name it expands to - the
// same two facts the Options window's Aliases tab shows per row.
struct CommandAliasSummary {
  std::string alias;
  std::string command;
};

// Builds the "Command Aliases" List accessible: one ListItem per alias, in
// the same order CommandEngine::Aliases() (a std::map, so already
// alias-sorted) holds them, named after the alias with a Description giving
// the full command name it runs - the same shape BuildDocumentUserTextNode
// already uses for its own key/value rows. Independent of whether the
// Options window's Aliases tab is actually open right now, the same way the
// other panel-backed regions above don't depend on their own panel window.
// Never empty in a real build: CommandEngine::InstallDefaultAliases()
// installs Rhino's own default alias set at startup, the same "never empty"
// shape Hatch Patterns/Plug-ins/Command List use, not the "starts empty,
// gains rows" shape Named Views/Named CPlanes use.
AccessibleNode BuildCommandAliasesNode(const std::vector<CommandAliasSummary>& aliases);

// One Keyboard Shortcuts row (app/Application.h's KeyShortcut, reduced to
// plain data the same way the summaries above keep this module independent
// of app::Application): the human-readable key combo already formatted as
// plain text ("Ctrl+Shift+Alt+L", built the same "Ctrl+"/"Shift+"/"Alt+"
// prefix-and-KeyShortcutName way the Options window's Shortcuts tab formats
// its own row - see DrawOptionsWindow), and the command it runs.
struct KeyboardShortcutSummary {
  std::string combo_text;
  std::string command;
};

// Builds the "Keyboard Shortcuts" List accessible: one ListItem per
// customized shortcut, in the same order Application::user_shortcuts holds
// them, named after its key combo with a Description giving the command it
// runs - the same two facts the Options window's Shortcuts tab shows per
// row. Independent of whether the Options window's Shortcuts tab is
// actually open right now, the same way the other panel-backed regions
// above don't depend on their own panel window. Starts empty on a fresh
// app: unlike Command Aliases, there is no default-shortcuts installer, so
// this is the "starts empty, gains rows" shape Named Views/Named CPlanes/
// Audit Results/Undo History use, not the "never empty" shape Command
// Aliases uses.
AccessibleNode BuildKeyboardShortcutsNode(const std::vector<KeyboardShortcutSummary>& shortcuts);

// Builds the "Keyboard Shortcuts Reference" List accessible: one ListItem
// per fixed, non-customizable shortcut the Options window's own
// built-in-shortcuts reference tab lists (its three BulletText rows,
// DrawOptionsWindow's options.tab_keyboard - distinct from the Shortcuts
// tab's user-customized ones above), reusing KeyboardShortcutSummary's same
// combo/command shape even though these come from a fixed table
// (ui/Panels.cpp's kBuiltinShortcutRow1/2/3) rather than
// Application::user_shortcuts. Named differently from "Keyboard Shortcuts"
// so a screen-reader user can tell the two apart. Never empty in a real
// build, the same "never empty" shape Command Aliases/Command List use,
// since the table is fixed, not user data.
AccessibleNode BuildBuiltinShortcutsNode(const std::vector<KeyboardShortcutSummary>& shortcuts);

// Builds the "Document Properties" List accessible: one ListItem per Label:
// value fact about the document's units, tolerances, grid and saved
// metadata - reusing the same label/value PropertyEntry shape
// BuildEnvironmentsPanelNode already uses for its own facts, just under a
// different accessible name. The caller (ui::DocumentPropertiesAccessibleTree,
// Panels.cpp) supplies entries for Units, Absolute tolerance, Angle
// tolerance, Grid spacing/major-every/extents, and Title/Author/Comments -
// the same facts DrawDocumentPropertiesWindow's own Units/tolerance/Grid/
// Metadata sections show (its own Annotation Styles and Linetypes
// sub-sections are already covered by BuildAnnotationStylesNode and
// BuildLinetypesNode, so are not repeated here). Independent of whether the
// Document Properties window is actually open right now, the same way the
// other panel-backed regions above don't depend on their own panel window.
AccessibleNode BuildDocumentPropertiesNode(const std::vector<PropertyEntry>& entries);

// One Textures-panel row (derived from doc/Document.h's Material, the same
// source BuildMaterialsPanelNode reads, filtered to just the materials that
// carry a texture - Dino 8 has no separate texture entity of its own, see
// DrawTexturesPanel): the material's name, its texture mapping mode already
// rendered as plain text (SceneObject.h's TextureMappingName), and whether
// the referenced image file was found on disk - the same two facts
// DrawTexturesPanel's own "(mode, found/missing)" disabled-text suffix shows
// per row.
struct TextureSummary {
  std::string name;
  std::string mapping_text;
  bool found = true;
};

// Builds the "Textures" List accessible: one ListItem per material that
// carries a texture, in Document::Materials() order, each named after the
// material with a Description giving its mapping mode and found/missing
// status - independent of whether the Textures panel window is actually
// open right now, the same way the other panel-backed regions above don't
// depend on their own panel window. Empty when no material has a texture
// assigned, the same "starts empty" shape Named Views/Named CPlanes use.
AccessibleNode BuildTexturesPanelNode(const std::vector<TextureSummary>& textures);

// Builds the "Display" List accessible: one ListItem per Label: value fact
// about the active viewport's display mode/projection/lens and the
// document's grid and display-tolerance settings - reusing the same
// label/value PropertyEntry shape BuildEnvironmentsPanelNode/
// BuildDocumentPropertiesNode already use. The caller
// (ui::DisplayAccessibleTree, Panels.cpp) supplies entries for the active
// viewport's name, display mode, perspective projection and lens, plus
// Show grid/Show axes/Grid spacing/Major line every/Grid extents/Curve
// display tolerance/Surface display tolerance/Control points on selected -
// the same facts DrawDisplayPanel itself shows. Independent of whether the
// Display panel window is actually open right now, the same way the other
// panel-backed regions above don't depend on their own panel window.
AccessibleNode BuildDisplayPanelNode(const std::vector<PropertyEntry>& entries);

}  // namespace dino8::platform
