#include "ui/Theme.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>

namespace dino8::app {

void ApplyDinoTheme(float ui_scale, bool light) {
  if (std::getenv("DINO8_UI_DEBUG")) std::fprintf(stderr, "[theme] ApplyDinoTheme scale=%.2f light=%d\n", ui_scale, light ? 1 : 0);
  ImGuiStyle& s = ImGui::GetStyle();
  s = ImGuiStyle();  // reset
  s.WindowRounding = 6.0f;
  s.ChildRounding = 4.0f;
  s.FrameRounding = 4.0f;
  s.PopupRounding = 4.0f;
  s.GrabRounding = 4.0f;
  s.TabRounding = 4.0f;
  s.ScrollbarRounding = 6.0f;
  s.WindowBorderSize = 1.0f;
  s.FrameBorderSize = 0.0f;
  s.WindowPadding = ImVec2(10, 8);
  s.FramePadding = ImVec2(8, 5);
  s.ItemSpacing = ImVec2(8, 6);
  s.ItemInnerSpacing = ImVec2(6, 4);
  s.IndentSpacing = 18.0f;
  s.ScrollbarSize = 14.0f;
  s.GrabMinSize = 12.0f;
  s.TabBarBorderSize = 1.0f;
  s.DockingSeparatorSize = 2.0f;
  s.WindowMenuButtonPosition = ImGuiDir_None;

  ImVec4* c = s.Colors;
  // Two complete palettes: the default dark UI and a Rhino-7-style light UI.
  const ImVec4 bg = light ? ImVec4(0.94f, 0.94f, 0.95f, 1.0f) : ImVec4(0.11f, 0.12f, 0.14f, 1.0f);
  const ImVec4 bg2 = light ? ImVec4(0.88f, 0.89f, 0.91f, 1.0f) : ImVec4(0.15f, 0.16f, 0.19f, 1.0f);
  const ImVec4 bg3 = light ? ImVec4(0.80f, 0.82f, 0.85f, 1.0f) : ImVec4(0.20f, 0.21f, 0.25f, 1.0f);
  const ImVec4 bg4 = light ? ImVec4(0.70f, 0.73f, 0.78f, 1.0f) : ImVec4(0.26f, 0.28f, 0.33f, 1.0f);
  const ImVec4 text = light ? ImVec4(0.10f, 0.11f, 0.13f, 1.0f) : ImVec4(0.92f, 0.93f, 0.95f, 1.0f);
  const ImVec4 muted = light ? ImVec4(0.40f, 0.42f, 0.46f, 1.0f) : ImVec4(0.62f, 0.64f, 0.68f, 1.0f);
  const ImVec4 accent(0.30f, 0.62f, 0.95f, 1.0f);
  const ImVec4 accent_hover(0.40f, 0.70f, 1.0f, 1.0f);
  const ImVec4 accent_active(0.22f, 0.50f, 0.85f, 1.0f);

  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = muted;
  c[ImGuiCol_WindowBg] = bg;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = light ? ImVec4(0.97f, 0.97f, 0.98f, 0.98f) : ImVec4(0.13f, 0.14f, 0.17f, 0.98f);
  c[ImGuiCol_Border] = light ? ImVec4(0.60f, 0.62f, 0.66f, 0.7f) : ImVec4(0.28f, 0.30f, 0.35f, 0.6f);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = bg2;
  c[ImGuiCol_FrameBgHovered] = bg3;
  c[ImGuiCol_FrameBgActive] = bg4;
  c[ImGuiCol_TitleBg] = bg;
  c[ImGuiCol_TitleBgActive] = bg2;
  c[ImGuiCol_TitleBgCollapsed] = bg;
  c[ImGuiCol_MenuBarBg] = bg2;
  c[ImGuiCol_ScrollbarBg] = bg;
  c[ImGuiCol_ScrollbarGrab] = bg3;
  c[ImGuiCol_ScrollbarGrabHovered] = bg4;
  c[ImGuiCol_ScrollbarGrabActive] = accent;
  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = accent_active;
  c[ImGuiCol_Button] = bg3;
  c[ImGuiCol_ButtonHovered] = bg4;
  c[ImGuiCol_ButtonActive] = accent_active;
  c[ImGuiCol_Header] = bg3;
  c[ImGuiCol_HeaderHovered] = bg4;
  c[ImGuiCol_HeaderActive] = accent_active;
  c[ImGuiCol_Separator] = ImVec4(0.28f, 0.30f, 0.35f, 0.8f);
  c[ImGuiCol_SeparatorHovered] = accent;
  c[ImGuiCol_SeparatorActive] = accent_active;
  c[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.30f, 0.35f, 0.5f);
  c[ImGuiCol_ResizeGripHovered] = accent_hover;
  c[ImGuiCol_ResizeGripActive] = accent_active;
  c[ImGuiCol_Tab] = bg2;
  c[ImGuiCol_TabHovered] = accent_hover;
  c[ImGuiCol_TabSelected] = bg3;
  c[ImGuiCol_TabSelectedOverline] = accent;
  c[ImGuiCol_TabDimmed] = bg;
  c[ImGuiCol_TabDimmedSelected] = bg2;
  c[ImGuiCol_TabDimmedSelectedOverline] = accent_active;
  c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.5f);
  c[ImGuiCol_DockingEmptyBg] = bg;
  c[ImGuiCol_PlotLines] = accent;
  c[ImGuiCol_PlotHistogram] = accent;
  c[ImGuiCol_TableHeaderBg] = bg2;
  c[ImGuiCol_TableBorderStrong] = ImVec4(0.28f, 0.30f, 0.35f, 1.0f);
  c[ImGuiCol_TableBorderLight] = ImVec4(0.22f, 0.24f, 0.28f, 1.0f);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
  c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
  c[ImGuiCol_DragDropTarget] = accent;
  c[ImGuiCol_NavCursor] = accent;
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);

  s.ScaleAllSizes(ui_scale);
}

}  // namespace dino8::app
