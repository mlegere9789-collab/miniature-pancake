// All dockable panels, dialogs and the main menu / toolbars. Each function
// draws one ImGui window for the current frame.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace dino8::app {

class Application;

void DrawMenuBar(Application& app);
void DrawToolbars(Application& app);
void DrawLayersPanel(Application& app);
void DrawPropertiesPanel(Application& app);
void DrawCommandHistoryPanel(Application& app);
void DrawCommandListPanel(Application& app, std::string& filter, int& status_filter);
// Activity Log panel: browses Document::ActivityLog(), filterable by a
// substring (label/summary) and an inclusive "YYYY-MM-DD" date range
// (each buffer must be at least 16 bytes; empty = unbounded on that side).
void DrawActivityLogPanel(Application& app, std::string& filter, char* from_date, char* to_date);
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
