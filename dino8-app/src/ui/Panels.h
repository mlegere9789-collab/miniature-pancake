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

// Small shared widgets.
bool ColorEdit(const char* label, struct Color& color);
// The default main-toolbar command list ("|" is a separator).
std::vector<std::string> DefaultToolbarCommands();
// Evaluates a simple arithmetic expression ("2*(3+4)/5", sqrt, sin...).
bool EvaluateExpression(const std::string& text, double& out, std::string& error);

}  // namespace dino8::app
