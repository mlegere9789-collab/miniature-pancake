#include "ui/Theme.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace dino8::app {

namespace {

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

}  // namespace

float StatusBadge(const char* text, ImVec4 color) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 ts = ImGui::CalcTextSize(text);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float pad = 5.0f;
  const float h = ts.y + 2.0f;
  const float w = ts.x + pad * 2;
  const float y = pos.y + (ImGui::GetTextLineHeight() - h) * 0.5f;
  dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + w, y + h), ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.22f)), 3.0f);
  dl->AddText(ImVec2(pos.x + pad, y + 1.0f), ImGui::GetColorU32(color), text);
  ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
  return w;
}

void ApplyDinoTheme(float ui_scale, bool light, const float* accent_rgb) {
  if (std::getenv("DINO8_UI_DEBUG")) std::fprintf(stderr, "[theme] ApplyDinoTheme scale=%.2f light=%d\n", ui_scale, light ? 1 : 0);
  if (accent_rgb) {
    for (int i = 0; i < 3; ++i) ThemeColors::kAccent[i] = std::clamp(accent_rgb[i], 0.0f, 1.0f);
  }
  const ImVec4 accent(ThemeColors::kAccent[0], ThemeColors::kAccent[1], ThemeColors::kAccent[2], 1.0f);
  const ImVec4 accent_hover = Mix(accent, ImVec4(1, 1, 1, 1), 0.18f);
  const ImVec4 accent_active = Mix(accent, ImVec4(0, 0, 0, 1), 0.22f);
  for (int i = 0; i < 3; ++i) {
    ThemeColors::kAccentDim[i] = (&accent_active.x)[i] * 0.85f;
    ThemeColors::kAccentBright[i] = (&accent_hover.x)[i];
  }

  ImGuiStyle& s = ImGui::GetStyle();
  s = ImGuiStyle();  // reset
  // One rounding everywhere (4 px) so frames, popups, tabs and buttons agree.
  s.WindowRounding = 4.0f;
  s.ChildRounding = 4.0f;
  s.FrameRounding = 4.0f;
  s.PopupRounding = 4.0f;
  s.GrabRounding = 4.0f;
  s.TabRounding = 4.0f;
  s.ScrollbarRounding = 4.0f;
  s.WindowBorderSize = 1.0f;
  s.PopupBorderSize = 1.0f;
  s.FrameBorderSize = 0.0f;
  s.TabBorderSize = 0.0f;
  s.WindowPadding = ImVec2(10, 8);
  s.FramePadding = ImVec2(8, 5);
  s.ItemSpacing = ImVec2(8, 6);
  s.ItemInnerSpacing = ImVec2(6, 4);
  s.IndentSpacing = 18.0f;
  s.ScrollbarSize = 12.0f;
  s.GrabMinSize = 12.0f;
  s.TabBarBorderSize = 1.0f;
  s.TabBarOverlineSize = 2.0f;
  s.DockingSeparatorSize = 2.0f;
  s.SeparatorTextBorderSize = 1.0f;
  s.WindowMenuButtonPosition = ImGuiDir_None;
  s.HoverDelayShort = 0.25f;
  s.HoverStationaryDelay = 0.1f;

  ImVec4* c = s.Colors;
  // Two complete palettes: the default dark UI and a Rhino-style light UI.
  const ImVec4 bg = light ? ImVec4(0.945f, 0.948f, 0.955f, 1.0f) : ImVec4(0.110f, 0.118f, 0.137f, 1.0f);
  const ImVec4 bg2 = light ? ImVec4(0.890f, 0.898f, 0.912f, 1.0f) : ImVec4(0.150f, 0.160f, 0.185f, 1.0f);
  const ImVec4 bg3 = light ? ImVec4(0.820f, 0.835f, 0.860f, 1.0f) : ImVec4(0.205f, 0.218f, 0.250f, 1.0f);
  const ImVec4 bg4 = light ? ImVec4(0.735f, 0.760f, 0.800f, 1.0f) : ImVec4(0.270f, 0.290f, 0.335f, 1.0f);
  const ImVec4 text = light ? ImVec4(0.105f, 0.115f, 0.135f, 1.0f) : ImVec4(0.905f, 0.915f, 0.935f, 1.0f);
  // Disabled text: still >= 4.5:1 contrast against the window background.
  const ImVec4 muted = light ? ImVec4(0.36f, 0.385f, 0.43f, 1.0f) : ImVec4(0.60f, 0.625f, 0.67f, 1.0f);
  const ImVec4 border = light ? ImVec4(0.66f, 0.68f, 0.72f, 0.75f) : ImVec4(0.30f, 0.32f, 0.37f, 0.65f);

  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = muted;
  c[ImGuiCol_WindowBg] = bg;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = light ? ImVec4(0.975f, 0.978f, 0.985f, 0.985f) : ImVec4(0.135f, 0.145f, 0.168f, 0.985f);
  c[ImGuiCol_Border] = border;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = bg2;
  c[ImGuiCol_FrameBgHovered] = Mix(bg2, bg3, 0.6f);
  c[ImGuiCol_FrameBgActive] = bg3;
  c[ImGuiCol_TitleBg] = bg;
  c[ImGuiCol_TitleBgActive] = bg2;
  c[ImGuiCol_TitleBgCollapsed] = bg;
  c[ImGuiCol_MenuBarBg] = bg2;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = bg3;
  c[ImGuiCol_ScrollbarGrabHovered] = bg4;
  c[ImGuiCol_ScrollbarGrabActive] = accent;
  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = accent_active;
  c[ImGuiCol_Button] = bg3;
  c[ImGuiCol_ButtonHovered] = Mix(bg3, bg4, 0.7f);
  c[ImGuiCol_ButtonActive] = Mix(bg4, accent, 0.45f);
  c[ImGuiCol_Header] = Mix(bg2, accent, 0.25f);
  c[ImGuiCol_HeaderHovered] = Mix(bg3, accent, 0.30f);
  c[ImGuiCol_HeaderActive] = Mix(bg3, accent, 0.45f);
  c[ImGuiCol_Separator] = border;
  c[ImGuiCol_SeparatorHovered] = accent;
  c[ImGuiCol_SeparatorActive] = accent_active;
  c[ImGuiCol_ResizeGrip] = ImVec4(border.x, border.y, border.z, 0.4f);
  c[ImGuiCol_ResizeGripHovered] = accent_hover;
  c[ImGuiCol_ResizeGripActive] = accent_active;
  // Rhino-style tabs: flat, selected tab lifts to the window colour with a
  // coloured overline; hovering tints instead of flashing the accent.
  c[ImGuiCol_Tab] = bg2;
  c[ImGuiCol_TabHovered] = Mix(bg3, accent, 0.25f);
  c[ImGuiCol_TabSelected] = bg;
  c[ImGuiCol_TabSelectedOverline] = accent;
  c[ImGuiCol_TabDimmed] = bg2;
  c[ImGuiCol_TabDimmedSelected] = bg;
  c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(accent.x, accent.y, accent.z, 0.5f);
  c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.5f);
  c[ImGuiCol_DockingEmptyBg] = bg;
  c[ImGuiCol_PlotLines] = accent;
  c[ImGuiCol_PlotLinesHovered] = accent_hover;
  c[ImGuiCol_PlotHistogram] = accent;
  c[ImGuiCol_PlotHistogramHovered] = accent_hover;
  c[ImGuiCol_TableHeaderBg] = bg2;
  c[ImGuiCol_TableBorderStrong] = border;
  c[ImGuiCol_TableBorderLight] = ImVec4(border.x, border.y, border.z, border.w * 0.5f);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = light ? ImVec4(0, 0, 0, 0.03f) : ImVec4(1, 1, 1, 0.03f);
  c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
  c[ImGuiCol_DragDropTarget] = accent;
  c[ImGuiCol_NavCursor] = accent;
  c[ImGuiCol_NavWindowingHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.7f);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);

  s.ScaleAllSizes(ui_scale);
}

}  // namespace dino8::app
