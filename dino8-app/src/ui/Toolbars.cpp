// A single, always-visible toolbar row of the most used tools. Buttons are
// large, labelled, and grouped so they stay findable (the number one Rhino
// 8 toolbar complaint was icon-only buttons that shift around).
#include "app/Application.h"

#include <string>
#include <vector>
#include "imgui.h"
#include "ui/Panels.h"
#include "ui/Theme.h"

namespace dino8::app {

namespace {

struct ToolButton {
  const char* label;
  const char* command;
  const char* tip;
};

const ToolButton kButtons[] = {
    {"New", "New", "New document"},        {"Open", "Open", "Open a .3dm / OBJ / STL"},
    {"Save", "Save", "Save (Ctrl+S)"},     {"|", nullptr, nullptr},
    {"Undo", "Undo", "Undo (Ctrl+Z)"},     {"Redo", "Redo", "Redo (Ctrl+Y)"},
    {"|", nullptr, nullptr},
    {"Point", "Point", "Point object"},    {"Line", "Line", "Line from two points"},
    {"Polyline", "Polyline", "Polyline"},  {"Curve", "Curve", "Control point curve"},
    {"Circle", "Circle", "Circle: centre, radius"}, {"Arc", "Arc", "Arc: centre, start, end"},
    {"Rect", "Rectangle", "Rectangle: two corners"}, {"Polygon", "Polygon", "Polygon: centre, radius"},
    {"|", nullptr, nullptr},
    {"Box", "Box", "Box: two corners, height"}, {"Sphere", "Sphere", "Sphere: centre, radius"},
    {"Cylinder", "Cylinder", "Cylinder"},  {"Cone", "Cone", "Cone"},
    {"Torus", "Torus", "Torus"},           {"Plane", "Plane", "Planar surface"},
    {"Extrude", "ExtrudeCrv", "Extrude a curve"}, {"Revolve", "Revolve", "Revolve a curve"},
    {"Loft", "Loft", "Loft curves"},
    {"|", nullptr, nullptr},
    {"Union", "BooleanUnion", "Boolean union"}, {"Diff", "BooleanDifference", "Boolean difference"},
    {"Inter", "BooleanIntersection", "Boolean intersection"},
    {"|", nullptr, nullptr},
    {"Move", "Move", "Move"},              {"Copy", "Copy", "Copy"},
    {"Rotate", "Rotate", "Rotate"},        {"Scale", "Scale", "Scale"},
    {"Mirror", "Mirror", "Mirror"},        {"Array", "Array", "Rectangular array"},
    {"|", nullptr, nullptr},
    {"Join", "Join", "Join"},              {"Explode", "Explode", "Explode"},
    {"Delete", "Delete", "Delete"},        {"Hide", "Hide", "Hide"},
    {"Show", "Show", "Show all"},          {"Group", "Group", "Group"},
    {"|", nullptr, nullptr},
    {"Distance", "Distance", "Measure distance"}, {"Length", "Length", "Curve length"},
    {"Area", "Area", "Area"},              {"Volume", "Volume", "Volume"},
    {"What", "What", "Describe objects"},
    {"|", nullptr, nullptr},
    {"Zoom Ext", "ZoomExtents", "Zoom extents"}, {"4 Views", "4View", "Four viewports"},
    {"Shaded", "ShadedViewport", "Shaded display"}, {"Wire", "Wireframe", "Wireframe display"},
};

}  // namespace

std::vector<std::string> DefaultToolbarCommands() {
  std::vector<std::string> out;
  for (const ToolButton& b : kButtons) out.push_back(b.command ? b.command : "|");
  return out;
}

namespace {
const ToolButton* FindButton(const std::string& command) {
  for (const ToolButton& b : kButtons) if (b.command && command == b.command) return &b;
  return nullptr;
}
}  // namespace

void DrawToolbars(Application& app) {
  if (app.toolbar_commands.empty()) app.toolbar_commands = DefaultToolbarCommands();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float menu_h = ImGui::GetFrameHeight();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + menu_h));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, 38.0f));
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                           ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
  if (ImGui::Begin("##toolbar", nullptr, flags)) {
    const bool running = app.Engine().IsRunning();
    int remove_index = -1;
    for (size_t i = 0; i < app.toolbar_commands.size(); ++i) {
      const std::string& command = app.toolbar_commands[i];
      ImGui::PushID(static_cast<int>(i));
      if (command == "|") {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::PopID();
        continue;
      }
      const ToolButton* b = FindButton(command);
      const RegisteredCommand* rc = app.Engine().Find(command);
      const std::string label = b ? b->label : command;
      const bool implemented = rc && rc->status != CommandStatus::Planned;
      if (!implemented) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.62f, 0.66f, 1.0f));
      if (running && rc && rc->name == app.Engine().ActiveName()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ThemeColors::kAccentDim[0], ThemeColors::kAccentDim[1], ThemeColors::kAccentDim[2], 1.0f));
        if (ImGui::Button(label.c_str())) app.Engine().Execute(command);
        ImGui::PopStyleColor();
      } else if (ImGui::Button(label.c_str())) {
        app.Engine().Execute(command);
      }
      if (!implemented) ImGui::PopStyleColor();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s  (%s)\nRight-click to remove from the toolbar", b ? b->tip : (rc && rc->info ? rc->info->description.c_str() : ""), command.c_str());
      if (ImGui::BeginPopupContextItem("tbctx")) {
        if (ImGui::MenuItem("Remove from toolbar")) remove_index = static_cast<int>(i);
        if (ImGui::MenuItem("Customize toolbar...")) app.Panels().options = true;
        ImGui::EndPopup();
      }
      ImGui::PopID();
      ImGui::SameLine();
    }
    if (remove_index >= 0) app.toolbar_commands.erase(app.toolbar_commands.begin() + remove_index);
    ImGui::NewLine();
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

}  // namespace dino8::app
