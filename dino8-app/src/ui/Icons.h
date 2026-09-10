// Procedural vector icons for toolbar buttons, the popup toolbar and the
// command-line autocomplete. Every glyph is drawn with ImDrawList
// primitives (lines, arcs, beziers, filled polygons), so icons stay crisp
// at any size and DPI and recolour with the theme. Commands without a
// dedicated glyph get a rounded box with the first letters of their name.
#pragma once

#include "imgui.h"

namespace dino8::app {

// Draws the icon for `command` in the square (pos, pos + size). `color` is
// the main stroke colour (normally the theme text colour); `accent` is the
// secondary colour for highlights / fills (0 = derive it from `color`).
void DrawIcon(ImDrawList* dl, const char* command, ImVec2 pos, float size, ImU32 color, ImU32 accent = 0);

// True when a hand-drawn glyph exists for the command (false = letter box).
bool HasIcon(const char* command);

}  // namespace dino8::app
