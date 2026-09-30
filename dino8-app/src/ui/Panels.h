// All dockable panels, dialogs and the main menu / toolbars. Each function
// draws one ImGui window for the current frame.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "platform/AccessibilityTree.h"

namespace dino8::app {

class Application;

void DrawMenuBar(Application& app);
// The AT-SPI2-queryable accessible tree for exactly what DrawMenuBar drew
// during its most recently completed call (see MenuBar.cpp's
// MenuTreeBuilder use and docs/ACCESSIBILITY.md). Empty children for any
// submenu that wasn't open that frame, mirroring what's really on screen.
const dino8::platform::AccessibleNode& LastMenuBarAccessibleTree();
// Plain-data snapshots of the Layers and Properties panels' current
// content, independent of whether either panel is actually open on screen
// right now (see docs/ACCESSIBILITY.md) - built straight from Document
// state, not from what DrawLayersPanel/DrawPropertiesPanel last drew.
dino8::platform::AccessibleNode LayersPanelAccessibleTree(Application& app);
dino8::platform::AccessibleNode PropertiesPanelAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of the option chips DrawCommandLine draws
// next to the prompt for whatever command is currently running (see
// CommandEngine::CurrentOptions/Command.h's OptionSpec) - an empty list
// (still a real "Command Options" accessible, just with no children) when
// no command is running or it currently offers none.
dino8::platform::AccessibleNode CommandOptionsAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of every viewport's title/view-menu button
// state (name, active/maximized, current display mode - see Viewport.cpp's
// title-overlay block), independent of which viewport window happens to be
// visible right now, built straight from Application::Viewports().
dino8::platform::AccessibleNode ViewportsAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of Document::ActivityLog() - the persisted,
// structured record of every finalized edit (see Document.h's
// ActivityLogEntry) - independent of whether DrawActivityLogPanel's window
// is open, and distinct from the command line's raw text log.
dino8::platform::AccessibleNode ActivityLogAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of Document::NamedViews() - independent of
// whether DrawNamedViewsPanel's window is open, mirroring only the name per
// row the same way the on-screen panel does.
dino8::platform::AccessibleNode NamedViewsAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of Document::NamedCPlanes() - independent
// of whether DrawNamedCPlanesPanel's window is open, mirroring only the name
// per row the same way the on-screen panel does (origin/axes are a hover
// tooltip there, not part of the row itself).
dino8::platform::AccessibleNode NamedCPlanesAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of Document::Linetypes() - independent of
// whether DrawLinetypesPanel's window is open, mirroring the name and dash
// pattern per row the same way the on-screen panel's Name/Pattern columns
// do.
dino8::platform::AccessibleNode LinetypesAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of Document::Materials() - independent of
// whether DrawMaterialsPanel's window is open, mirroring the name and
// diffuse colour per row (the colour swatch DrawMaterialsPanel draws next to
// each name, reduced to plain "R, G, B" text).
dino8::platform::AccessibleNode MaterialsAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of Document::ClippingPlanes() - independent
// of whether DrawClippingPlanesPanel's window is open, mirroring the name,
// on/off state and viewport scope each row's checkbox and hover tooltip
// show.
dino8::platform::AccessibleNode ClippingPlanesAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of Document::Layouts() - independent of
// whether DrawLayoutsPanel's window is open, mirroring the name per row plus
// which one is currently active (Application::ActiveLayoutIndex()), the way
// DrawLayoutsPanel itself only distinguishes the active row by selection
// highlight.
dino8::platform::AccessibleNode LayoutsAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of Document::Blocks() (defined alongside
// DrawBlockManagerPanel in cmd_drafting.cpp, since that's where the panel
// itself and its Document-level helpers live) - independent of whether the
// Block Manager panel window is open, mirroring the object and instance
// counts DrawBlockManagerPanel's own Objects/Instances columns show.
dino8::platform::AccessibleNode BlockManagerAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of Document::LayerStates() - independent
// of whether DrawLayerStateManager's window is open, mirroring the name per
// saved state plus how many layers it snapshots.
dino8::platform::AccessibleNode LayerStateManagerAccessibleTree(Application& app);
// The AT-SPI2-queryable snapshot of Document::UserText() - independent of
// whether DrawDocumentUserTextPanel's window is open, mirroring the
// key/value pair per entry the same way the on-screen panel's own "key =
// value" row does.
dino8::platform::AccessibleNode DocumentUserTextAccessibleTree(Application& app);
void DrawToolbars(Application& app);
void DrawLayersPanel(Application& app);
void DrawPropertiesPanel(Application& app);
void DrawCommandHistoryPanel(Application& app);
void DrawCommandListPanel(Application& app, std::string& filter, int& status_filter);
// Activity Log panel: browses Document::ActivityLog(), filterable by a
// substring (label/summary) and an inclusive "YYYY-MM-DD" date range
// (each buffer must be at least 16 bytes; empty = unbounded on that side).
void DrawActivityLogPanel(Application& app, std::string& filter, char* from_date, char* to_date);
// Audit results panel (Application::AuditResults, populated by the Audit
// command in cmd_analyze.cpp): one row per invalid object with Select/Zoom
// To actions, following the same Application::ActiveViewport()-driven
// select/zoom pattern the other object-list panels use.
void DrawAuditResultsPanel(Application& app);
// BlockManager: table of every block definition (name, object count,
// instance count) with per-row Select Instances / Rename / Delete (if
// unused) / Insert New Instance actions - defined in cmd_drafting.cpp,
// alongside the block data model it reads and writes.
void DrawBlockManagerPanel(Application& app);
// UVEditor: a read-only, pannable/zoomable 2D view of the first selected
// object's current texture mapping in UV space (the object's tessellated
// triangle edges projected through the same EnsureMappedUVs the renderer
// itself uses) - defined in cmd_remaining.cpp.
void DrawUVEditorPanel(Application& app);
// MappingWidget's companion panel: status/info for the interactive 3D
// mapping-plane gizmo (ui/MappingGizmo.h) that the main render loop draws
// directly into the viewport while this panel is open - defined in
// cmd_render.cpp, alongside ApplyCustomMapping's mapping-frame fields.
void DrawMappingWidgetPanel(Application& app);
// HBar: shows the active distance-lock constraint's locked distance (if
// any), a field to type a new one and a Release button - defined in
// cmd_select2.cpp, alongside the HBar/HBarSetDistance/HBarOff commands and
// the HBarConstraint data it reads and writes (doc/SubObjectEdit.h).
void DrawHBarPanel(Application& app);
void DrawHelpPanel(Application& app, std::string& search);
void DrawNotificationsPanel(Application& app);
void DrawNamedViewsPanel(Application& app);
void DrawNotesPanel(Application& app, char* buffer, size_t buffer_size);
void DrawDocumentUserTextPanel(Application& app);
void DrawMaterialsPanel(Application& app);
void DrawLightsPanel(Application& app);
void DrawRenderingPanel(Application& app);
void DrawEnvironmentsPanel(Application& app);
void DrawTexturesPanel(Application& app);
void DrawRenderWindow(Application& app);
void DrawDisplayPanel(Application& app);
void DrawCalculatorPanel(Application& app, std::string& input, std::string& result);
void DrawAboutWindow(Application& app);
// What's New: the real changelog (data/changelog.md), grouped by version,
// in a scrollable window. Separate from DrawAboutWindow, which is only the
// static version blurb.
void DrawWhatsNewWindow(Application& app);
void DrawOptionsWindow(Application& app);
void DrawDocumentPropertiesWindow(Application& app);
void DrawLinetypesPanel(Application& app);
void DrawBoxEditPanel(Application& app);
void DrawUndoMultipleWindow(Application& app, bool redo);
void DrawLayerStateManager(Application& app);
void DrawSelectionFilterPanel(Application& app);
void DrawMacroEditor(Application& app);
void DrawClippingPlanesPanel(Application& app);
void DrawLayoutsPanel(Application& app);
void DrawNamedCPlanesPanel(Application& app);
void DrawScriptEditor(Application& app);
void DrawScriptingReference(Application& app);
// Loads a script file into the Script Editor panel (and opens it).
bool OpenInScriptEditor(Application& app, const std::string& path);
// Runs whatever the Script Editor panel currently holds, exactly as its Run
// button does: dispatches to PythonEngine for a loaded .py file, Lua
// (RunScript) otherwise. Exposed so headless commands/tests can trigger the
// same routing the button uses instead of calling RunPythonScript/RunScript
// directly.
void RunScriptEditor(Application& app);
void DrawHatchPatternsPanel(Application& app);  // defined in cmd_drafting2.cpp
void DrawTableEditorPanel(Application& app);    // defined in cmd_drafting2.cpp

// Toolbars (Toolbars.cpp): tabbed icon toolbar + left sidebar.
struct IconButtonResult {
  bool left = false;     // run the command
  bool right = false;    // run the alternate command
  bool context = false;  // open the customize menu
};
// Draws one icon button for `command` (caption optional); the caller runs the commands.
IconButtonResult IconButton(Application& app, const char* command, const char* label, bool show_label, bool customizable);
// ImGui drag-and-drop payload type for "drag a command name": carried by
// every toolbar/sidebar IconButton as a drag source (a NUL-terminated
// command name), and accepted by the Standard toolbar's drop target in
// DrawButtonRow and by the reorderable list + icon-grid picker in
// DrawOptionsWindow's Toolbar tab.
inline constexpr const char* kToolbarCommandDragType = "DINO8_TB_CMD";
// Payload type for reordering entries within the Standard toolbar customize
// list itself: carries the dragged entry's index (an int) into
// app.toolbar_commands.
inline constexpr const char* kToolbarReorderDragType = "DINO8_TB_REORDER";
void DrawLeftSidebar(Application& app);
float ToolbarHeight(const Application& app);      // 0 when toolbars are hidden
float LeftSidebarWidth(const Application& app);   // 0 when the sidebar is hidden
int ToolbarTabCount();
const char* ToolbarTabName(int index);
// Commands of one toolbar tab ("|" = separator); tab 0 is the customizable Standard toolbar.
std::vector<std::string> ToolbarTabCommands(const Application& app, int index);
const char* ToolbarButtonLabel(const std::string& command);  // nullptr when unknown
// Every command with a curated icon/label/tooltip (Toolbars.cpp's kButtons),
// in a stable display order - the catalog the Options > Toolbar icon-grid
// picker searches and shows.
std::vector<std::string> AllToolbarButtonCommands();
// Appends `name` to the end of the Standard toolbar (app.toolbar_commands),
// initializing it to the default set first if it was empty. Returns false
// (nothing added) for an unrecognized command name. This is the single path
// both the Options > Toolbar icon-grid picker's click handler and the
// scriptable "ToolbarAddCommand" command (cmd_state.cpp) go through, so the
// picker's effect can be exercised headlessly without a mouse.
bool AddToolbarCommand(Application& app, const std::string& name);

// Small shared widgets.
bool ColorEdit(const char* label, struct Color& color);
// A translated window title with a "###<id>" ImGui-identity suffix, so a
// language switch doesn't reset the saved dock layout (see Panels.cpp).
std::string PanelTitle(const std::string& key, const char* stable_id);
// The default main-toolbar command list ("|" is a separator).
std::vector<std::string> DefaultToolbarCommands();
// Evaluates a simple arithmetic expression ("2*(3+4)/5", sqrt, sin...).
bool EvaluateExpression(const std::string& text, double& out, std::string& error);

}  // namespace dino8::app
