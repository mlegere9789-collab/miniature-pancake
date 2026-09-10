#include "ui/Icons.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>

#include "commands/CommandCatalog.h"

namespace dino8::app {

namespace {

constexpr float kPi = 3.14159265f;

ImU32 WithAlpha(ImU32 c, float a) {
  return (c & 0x00FFFFFFu) | (static_cast<ImU32>(a * 255.0f) << IM_COL32_A_SHIFT);
}

// Lighter (dark themes) or darker (light themes) companion of `c`.
ImU32 DeriveAccent(ImU32 c) {
  const float r = ((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
  const float g = ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
  const float b = ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
  const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
  auto mix = [](float v, float target, float t) { return v + (target - v) * t; };
  float nr, ng, nb;
  if (lum > 0.5f) { nr = mix(r, 0.0f, 0.45f); ng = mix(g, 0.0f, 0.45f); nb = mix(b, 0.0f, 0.45f); }
  else { nr = mix(r, 1.0f, 0.45f); ng = mix(g, 1.0f, 0.45f); nb = mix(b, 1.0f, 0.45f); }
  return IM_COL32(static_cast<int>(nr * 255), static_cast<int>(ng * 255), static_cast<int>(nb * 255), (c >> IM_COL32_A_SHIFT) & 0xFF);
}

// A tiny drawing DSL in unit coordinates (0..1, y down) mapped onto the
// icon square. Strokes use the main colour unless a colour is given.
struct Pen {
  ImDrawList* dl;
  ImVec2 o;
  float s;
  ImU32 col;    // main stroke
  ImU32 acc;    // accent stroke
  ImU32 fill;   // translucent fill (main colour)
  ImU32 afill;  // translucent fill (accent colour)
  float th;     // stroke thickness in pixels
  float thin;   // secondary thickness

  ImVec2 P(float x, float y) const { return ImVec2(o.x + x * s, o.y + y * s); }
  void L(float x0, float y0, float x1, float y1, ImU32 c = 0, float t = 0) const {
    dl->AddLine(P(x0, y0), P(x1, y1), c ? c : col, t > 0 ? t : th);
  }
  void Dash(float x0, float y0, float x1, float y1, int n = 4, ImU32 c = 0) const {
    for (int i = 0; i < n; ++i) {
      const float a = (i + 0.0f) / n, b = (i + 0.55f) / n;
      L(x0 + (x1 - x0) * a, y0 + (y1 - y0) * a, x0 + (x1 - x0) * b, y0 + (y1 - y0) * b, c ? c : acc, thin);
    }
  }
  void Poly(std::initializer_list<float> xy, bool closed, ImU32 c = 0, float t = 0) const {
    const float* v = xy.begin();
    const int n = static_cast<int>(xy.size() / 2);
    for (int i = 0; i + 1 < n; ++i) L(v[2 * i], v[2 * i + 1], v[2 * i + 2], v[2 * i + 3], c, t);
    if (closed && n > 2) L(v[2 * n - 2], v[2 * n - 1], v[0], v[1], c, t);
  }
  void Fill(std::initializer_list<float> xy, ImU32 c = 0) const {
    const float* v = xy.begin();
    const int n = static_cast<int>(xy.size() / 2);
    for (int i = 0; i < n; ++i) dl->PathLineTo(P(v[2 * i], v[2 * i + 1]));
    dl->PathFillConvex(c ? c : fill);
  }
  void C(float cx, float cy, float r, ImU32 c = 0, float t = 0) const { dl->AddCircle(P(cx, cy), r * s, c ? c : col, 0, t > 0 ? t : th); }
  void CF(float cx, float cy, float r, ImU32 c = 0) const { dl->AddCircleFilled(P(cx, cy), r * s, c ? c : fill); }
  void Dot(float x, float y, ImU32 c = 0, float r = 0) const { dl->AddCircleFilled(P(x, y), r > 0 ? r * s : th * 0.95f, c ? c : acc); }
  void Arc(float cx, float cy, float r, float a0, float a1, ImU32 c = 0, float t = 0) const {
    dl->PathArcTo(P(cx, cy), r * s, a0, a1);
    dl->PathStroke(c ? c : col, 0, t > 0 ? t : th);
  }
  void Ellipse(float cx, float cy, float rx, float ry, ImU32 c = 0, float t = 0, float a0 = 0, float a1 = 2 * kPi) const {
    const int n = 28;
    for (int i = 0; i <= n; ++i) {
      const float a = a0 + (a1 - a0) * i / n;
      dl->PathLineTo(P(cx + std::cos(a) * rx, cy + std::sin(a) * ry));
    }
    dl->PathStroke(c ? c : col, (a1 - a0 >= 2 * kPi - 1e-3f) ? ImDrawFlags_Closed : 0, t > 0 ? t : th);
  }
  void EllipseF(float cx, float cy, float rx, float ry, ImU32 c = 0) const {
    const int n = 28;
    for (int i = 0; i < n; ++i) {
      const float a = 2 * kPi * i / n;
      dl->PathLineTo(P(cx + std::cos(a) * rx, cy + std::sin(a) * ry));
    }
    dl->PathFillConvex(c ? c : fill);
  }
  void Bez(float x0, float y0, float cx0, float cy0, float cx1, float cy1, float x1, float y1, ImU32 c = 0, float t = 0) const {
    dl->AddBezierCubic(P(x0, y0), P(cx0, cy0), P(cx1, cy1), P(x1, y1), c ? c : col, t > 0 ? t : th);
  }
  void Rect(float x0, float y0, float x1, float y1, float r = 0, ImU32 c = 0, float t = 0) const {
    dl->AddRect(P(x0, y0), P(x1, y1), c ? c : col, r * s, 0, t > 0 ? t : th);
  }
  void RectF(float x0, float y0, float x1, float y1, float r = 0, ImU32 c = 0) const {
    dl->AddRectFilled(P(x0, y0), P(x1, y1), c ? c : fill, r * s);
  }
  // Arrow head pointing along (dx,dy) with its tip at (x,y).
  void Head(float x, float y, float dx, float dy, ImU32 c = 0, float len = 0.16f) const {
    const float l = std::sqrt(dx * dx + dy * dy);
    if (l < 1e-6f) return;
    dx /= l; dy /= l;
    const float bx = x - dx * len, by = y - dy * len;
    const float px = -dy * len * 0.55f, py = dx * len * 0.55f;
    dl->AddTriangleFilled(P(x, y), P(bx + px, by + py), P(bx - px, by - py), c ? c : col);
  }
  void Arrow(float x0, float y0, float x1, float y1, ImU32 c = 0) const {
    L(x0, y0, x1, y1, c);
    Head(x1, y1, x1 - x0, y1 - y0, c);
  }
  // Isometric cube helpers (front-left face, top face, right face).
  void Cube(bool fill_faces, bool top_accent) const {
    if (fill_faces) {
      Fill({0.18f, 0.40f, 0.60f, 0.40f, 0.60f, 0.88f, 0.18f, 0.88f});
      Fill({0.60f, 0.40f, 0.86f, 0.20f, 0.86f, 0.68f, 0.60f, 0.88f}, WithAlpha(col, 0.42f));
      Fill({0.18f, 0.40f, 0.44f, 0.20f, 0.86f, 0.20f, 0.60f, 0.40f}, top_accent ? afill : WithAlpha(col, 0.18f));
    } else if (top_accent) {
      Fill({0.18f, 0.40f, 0.44f, 0.20f, 0.86f, 0.20f, 0.60f, 0.40f}, afill);
    }
    Poly({0.18f, 0.40f, 0.60f, 0.40f, 0.60f, 0.88f, 0.18f, 0.88f}, true);
    Poly({0.18f, 0.40f, 0.44f, 0.20f, 0.86f, 0.20f, 0.60f, 0.40f}, true);
    Poly({0.86f, 0.20f, 0.86f, 0.68f, 0.60f, 0.88f}, false);
  }
  void Eye(bool open) const {
    Bez(0.08f, 0.5f, 0.3f, 0.15f, 0.7f, 0.15f, 0.92f, 0.5f);
    Bez(0.08f, 0.5f, 0.3f, 0.85f, 0.7f, 0.85f, 0.92f, 0.5f);
    C(0.5f, 0.5f, 0.16f);
    Dot(0.5f, 0.5f, acc, 0.08f);
    if (!open) L(0.15f, 0.88f, 0.85f, 0.12f, acc);
  }
  void Magnifier() const {
    C(0.42f, 0.42f, 0.28f);
    L(0.63f, 0.63f, 0.9f, 0.9f, col, th * 1.6f);
  }
  void Cutter() const { Dash(0.5f, 0.1f, 0.5f, 0.9f, 5, acc); }
};

struct Glyph {
  const char* name;
  void (*draw)(const Pen&);
};

// ---------------------------------------------------------------------------
// The glyphs
// ---------------------------------------------------------------------------
const Glyph kGlyphs[] = {
    {"New", [](const Pen& p) {
       p.Fill({0.24f, 0.1f, 0.6f, 0.1f, 0.78f, 0.28f, 0.78f, 0.9f, 0.24f, 0.9f});
       p.Poly({0.24f, 0.1f, 0.6f, 0.1f, 0.78f, 0.28f, 0.78f, 0.9f, 0.24f, 0.9f}, true);
       p.Poly({0.6f, 0.1f, 0.6f, 0.28f, 0.78f, 0.28f}, false, p.acc, p.thin);
       p.L(0.51f, 0.48f, 0.51f, 0.74f, p.acc); p.L(0.38f, 0.61f, 0.64f, 0.61f, p.acc);
     }},
    {"Open", [](const Pen& p) {
       p.Poly({0.1f, 0.25f, 0.36f, 0.25f, 0.44f, 0.34f, 0.88f, 0.34f, 0.88f, 0.5f}, false);
       p.L(0.1f, 0.25f, 0.1f, 0.82f);
       p.Fill({0.2f, 0.48f, 0.95f, 0.48f, 0.82f, 0.82f, 0.1f, 0.82f}, p.afill);
       p.Poly({0.2f, 0.48f, 0.95f, 0.48f, 0.82f, 0.82f, 0.1f, 0.82f}, true, p.acc);
     }},
    {"Save", [](const Pen& p) {
       p.RectF(0.12f, 0.12f, 0.88f, 0.88f, 0.08f);
       p.Rect(0.12f, 0.12f, 0.88f, 0.88f, 0.08f);
       p.RectF(0.3f, 0.12f, 0.7f, 0.38f, 0, p.acc);
       p.Rect(0.3f, 0.6f, 0.7f, 0.88f, 0.03f, p.col, p.thin);
     }},
    {"Undo", [](const Pen& p) {
       p.Arc(0.52f, 0.58f, 0.3f, kPi * 1.15f, kPi * 2.0f);
       p.L(0.82f, 0.58f, 0.82f, 0.76f);
       p.Head(0.19f, 0.5f, -0.25f, 0.3f);
     }},
    {"Redo", [](const Pen& p) {
       p.Arc(0.48f, 0.58f, 0.3f, kPi * 1.0f, kPi * 1.85f);
       p.L(0.18f, 0.58f, 0.18f, 0.76f);
       p.Head(0.81f, 0.5f, 0.25f, 0.3f);
     }},
    {"Point", [](const Pen& p) {
       p.L(0.5f, 0.12f, 0.5f, 0.32f, p.acc, p.thin); p.L(0.5f, 0.68f, 0.5f, 0.88f, p.acc, p.thin);
       p.L(0.12f, 0.5f, 0.32f, 0.5f, p.acc, p.thin); p.L(0.68f, 0.5f, 0.88f, 0.5f, p.acc, p.thin);
       p.Dot(0.5f, 0.5f, p.col, 0.11f);
     }},
    {"Line", [](const Pen& p) { p.L(0.15f, 0.85f, 0.85f, 0.15f); p.Dot(0.15f, 0.85f); p.Dot(0.85f, 0.15f); }},
    {"Polyline", [](const Pen& p) {
       p.Poly({0.1f, 0.82f, 0.34f, 0.28f, 0.6f, 0.66f, 0.9f, 0.18f}, false);
       p.Dot(0.1f, 0.82f); p.Dot(0.34f, 0.28f); p.Dot(0.6f, 0.66f); p.Dot(0.9f, 0.18f);
     }},
    {"Curve", [](const Pen& p) {
       p.Poly({0.1f, 0.85f, 0.22f, 0.12f, 0.75f, 0.95f, 0.9f, 0.2f}, false, p.acc, p.thin);
       p.Bez(0.1f, 0.85f, 0.22f, 0.12f, 0.75f, 0.95f, 0.9f, 0.2f);
       p.Dot(0.1f, 0.85f); p.Dot(0.22f, 0.12f); p.Dot(0.75f, 0.95f); p.Dot(0.9f, 0.2f);
     }},
    {"InterpCrv", [](const Pen& p) {
       p.Bez(0.1f, 0.8f, 0.3f, 0.05f, 0.65f, 1.0f, 0.9f, 0.25f);
       p.Dot(0.1f, 0.8f); p.Dot(0.36f, 0.42f); p.Dot(0.66f, 0.62f); p.Dot(0.9f, 0.25f);
     }},
    {"Circle", [](const Pen& p) { p.C(0.5f, 0.5f, 0.37f); p.Dot(0.5f, 0.5f); }},
    {"Arc", [](const Pen& p) {
       p.Arc(0.5f, 0.68f, 0.4f, kPi * 1.08f, kPi * 1.92f);
       p.Dot(0.5f, 0.68f, p.acc, 0.045f);
       p.Dot(0.11f, 0.78f); p.Dot(0.89f, 0.78f);
     }},
    {"Rectangle", [](const Pen& p) { p.RectF(0.12f, 0.24f, 0.88f, 0.76f); p.Rect(0.12f, 0.24f, 0.88f, 0.76f); p.Dot(0.12f, 0.24f); p.Dot(0.88f, 0.76f); }},
    {"Polygon", [](const Pen& p) {
       p.Fill({0.5f, 0.1f, 0.85f, 0.3f, 0.85f, 0.7f, 0.5f, 0.9f, 0.15f, 0.7f, 0.15f, 0.3f});
       p.Poly({0.5f, 0.1f, 0.85f, 0.3f, 0.85f, 0.7f, 0.5f, 0.9f, 0.15f, 0.7f, 0.15f, 0.3f}, true);
       p.Dot(0.5f, 0.5f);
     }},
    {"Ellipse", [](const Pen& p) { p.EllipseF(0.5f, 0.5f, 0.4f, 0.26f); p.Ellipse(0.5f, 0.5f, 0.4f, 0.26f); p.Dot(0.5f, 0.5f); }},
    {"Text", [](const Pen& p) {
       p.Poly({0.18f, 0.88f, 0.5f, 0.12f, 0.82f, 0.88f}, false, p.col, p.th * 1.15f);
       p.L(0.3f, 0.62f, 0.7f, 0.62f, p.acc);
     }},
    {"Dim", [](const Pen& p) {
       p.L(0.14f, 0.3f, 0.14f, 0.8f, p.col, p.thin); p.L(0.86f, 0.3f, 0.86f, 0.8f, p.col, p.thin);
       p.L(0.14f, 0.42f, 0.86f, 0.42f, p.acc);
       p.Head(0.14f, 0.42f, -1, 0, p.acc, 0.13f); p.Head(0.86f, 0.42f, 1, 0, p.acc, 0.13f);
       p.L(0.4f, 0.6f, 0.6f, 0.6f, p.col, p.thin); p.L(0.36f, 0.72f, 0.64f, 0.72f, p.col, p.thin);
     }},
    {"Box", [](const Pen& p) { p.Cube(true, true); }},
    {"Sphere", [](const Pen& p) {
       p.CF(0.5f, 0.5f, 0.38f);
       p.C(0.5f, 0.5f, 0.38f);
       p.Ellipse(0.5f, 0.5f, 0.38f, 0.14f, p.acc, p.thin);
       p.Ellipse(0.5f, 0.5f, 0.14f, 0.38f, p.acc, p.thin);
     }},
    {"Cylinder", [](const Pen& p) {
       p.RectF(0.2f, 0.25f, 0.8f, 0.78f);
       p.L(0.2f, 0.25f, 0.2f, 0.78f); p.L(0.8f, 0.25f, 0.8f, 0.78f);
       p.Ellipse(0.5f, 0.78f, 0.3f, 0.11f, p.col, 0, 0, kPi);
       p.EllipseF(0.5f, 0.25f, 0.3f, 0.11f, p.afill);
       p.Ellipse(0.5f, 0.25f, 0.3f, 0.11f);
     }},
    {"Cone", [](const Pen& p) {
       p.Fill({0.5f, 0.1f, 0.82f, 0.76f, 0.18f, 0.76f});
       p.L(0.5f, 0.1f, 0.18f, 0.76f); p.L(0.5f, 0.1f, 0.82f, 0.76f);
       p.Ellipse(0.5f, 0.76f, 0.32f, 0.12f, p.col, 0, 0, kPi);
       p.Ellipse(0.5f, 0.76f, 0.32f, 0.12f, p.acc, p.thin, kPi, 2 * kPi);
     }},
    {"Torus", [](const Pen& p) {
       p.EllipseF(0.5f, 0.5f, 0.42f, 0.26f);
       p.Ellipse(0.5f, 0.5f, 0.42f, 0.26f);
       p.EllipseF(0.5f, 0.47f, 0.17f, 0.09f, p.afill);
       p.Ellipse(0.5f, 0.47f, 0.17f, 0.09f, p.acc, p.thin);
     }},
    {"Plane", [](const Pen& p) {
       p.Fill({0.1f, 0.72f, 0.36f, 0.3f, 0.9f, 0.3f, 0.64f, 0.72f});
       p.Poly({0.1f, 0.72f, 0.36f, 0.3f, 0.9f, 0.3f, 0.64f, 0.72f}, true);
     }},
    {"PlanarSrf", [](const Pen& p) {
       p.dl->PathLineTo(p.P(0.15f, 0.55f));
       p.dl->PathBezierCubicCurveTo(p.P(0.05f, 0.15f), p.P(0.6f, 0.05f), p.P(0.75f, 0.3f));
       p.dl->PathBezierCubicCurveTo(p.P(0.95f, 0.55f), p.P(0.85f, 0.9f), p.P(0.5f, 0.88f));
       p.dl->PathBezierCubicCurveTo(p.P(0.2f, 0.88f), p.P(0.25f, 0.85f), p.P(0.15f, 0.55f));
       p.dl->PathFillConvex(p.afill);
       p.Bez(0.15f, 0.55f, 0.05f, 0.15f, 0.6f, 0.05f, 0.75f, 0.3f);
       p.Bez(0.75f, 0.3f, 0.95f, 0.55f, 0.85f, 0.9f, 0.5f, 0.88f);
       p.Bez(0.5f, 0.88f, 0.2f, 0.88f, 0.25f, 0.85f, 0.15f, 0.55f);
     }},
    {"ExtrudeCrv", [](const Pen& p) {
       p.Bez(0.15f, 0.85f, 0.35f, 0.65f, 0.6f, 1.0f, 0.85f, 0.8f, p.acc);
       p.L(0.15f, 0.85f, 0.15f, 0.35f); p.L(0.85f, 0.8f, 0.85f, 0.3f); p.L(0.5f, 0.82f, 0.5f, 0.32f, p.col, p.thin);
       p.Bez(0.15f, 0.35f, 0.35f, 0.15f, 0.6f, 0.5f, 0.85f, 0.3f);
       p.Arrow(0.5f, 0.6f, 0.5f, 0.12f, p.acc);
     }},
    {"Revolve", [](const Pen& p) {
       p.Dash(0.22f, 0.08f, 0.22f, 0.92f, 5, p.acc);
       p.Bez(0.5f, 0.15f, 0.9f, 0.3f, 0.35f, 0.6f, 0.62f, 0.85f);
       p.Ellipse(0.22f, 0.85f, 0.4f, 0.1f, p.col, p.thin, 0, kPi);
       p.Ellipse(0.22f, 0.15f, 0.28f, 0.08f, p.col, p.thin, 0, kPi * 0.5f);
       p.Head(0.5f, 0.15f, 0.3f, -0.1f, p.acc, 0.13f);
     }},
    {"Loft", [](const Pen& p) {
       for (float y : {0.2f, 0.5f, 0.8f}) p.Bez(0.12f, y, 0.35f, y - 0.14f, 0.62f, y + 0.14f, 0.88f, y, y == 0.5f ? p.acc : p.col);
       p.L(0.12f, 0.2f, 0.12f, 0.8f, p.col, p.thin); p.L(0.88f, 0.2f, 0.88f, 0.8f, p.col, p.thin);
       p.L(0.5f, 0.2f, 0.5f, 0.8f, p.col, p.thin);
     }},
    {"Sweep1", [](const Pen& p) {
       p.Bez(0.15f, 0.8f, 0.2f, 0.2f, 0.7f, 0.95f, 0.88f, 0.2f, p.acc);
       p.Ellipse(0.17f, 0.8f, 0.08f, 0.14f);
       p.Bez(0.15f, 0.66f, 0.2f, 0.06f, 0.7f, 0.81f, 0.88f, 0.06f, p.col, p.thin);
       p.Bez(0.15f, 0.94f, 0.2f, 0.34f, 0.7f, 1.09f, 0.88f, 0.34f, p.col, p.thin);
     }},
    {"Sweep2", [](const Pen& p) {
       p.Bez(0.12f, 0.9f, 0.1f, 0.4f, 0.5f, 0.5f, 0.6f, 0.1f, p.acc);
       p.Bez(0.4f, 0.9f, 0.4f, 0.55f, 0.85f, 0.55f, 0.9f, 0.1f, p.acc);
       p.Ellipse(0.26f, 0.9f, 0.14f, 0.07f);
       p.Bez(0.24f, 0.5f, 0.35f, 0.4f, 0.55f, 0.4f, 0.66f, 0.5f, p.col, p.thin);
     }},
    {"NetworkSrf", [](const Pen& p) {
       for (float t : {0.15f, 0.5f, 0.85f}) {
         p.Bez(0.1f, t, 0.35f, t - 0.12f, 0.65f, t + 0.12f, 0.9f, t, t == 0.5f ? p.acc : p.col, t == 0.5f ? p.th : p.thin);
         p.Bez(t, 0.1f, t - 0.12f, 0.35f, t + 0.12f, 0.65f, t, 0.9f, t == 0.5f ? p.acc : p.col, t == 0.5f ? p.th : p.thin);
       }
     }},
    {"Pipe", [](const Pen& p) {
       p.Bez(0.12f, 0.75f, 0.3f, 0.1f, 0.7f, 1.0f, 0.88f, 0.3f, p.fill, p.th * 4.0f);
       p.Bez(0.12f, 0.75f, 0.3f, 0.1f, 0.7f, 1.0f, 0.88f, 0.3f, p.acc, p.thin);
       p.Bez(0.05f, 0.7f, 0.23f, 0.05f, 0.63f, 0.95f, 0.81f, 0.25f, p.col, p.thin);
       p.Bez(0.19f, 0.8f, 0.37f, 0.15f, 0.77f, 1.05f, 0.95f, 0.35f, p.col, p.thin);
     }},
    {"OffsetSrf", [](const Pen& p) {
       p.Fill({0.1f, 0.85f, 0.35f, 0.5f, 0.9f, 0.5f, 0.65f, 0.85f});
       p.Poly({0.1f, 0.85f, 0.35f, 0.5f, 0.9f, 0.5f, 0.65f, 0.85f}, true);
       p.Poly({0.1f, 0.5f, 0.35f, 0.15f, 0.9f, 0.15f, 0.65f, 0.5f}, true, p.acc, p.thin);
       p.Arrow(0.5f, 0.62f, 0.5f, 0.28f, p.acc);
     }},
    {"BooleanUnion", [](const Pen& p) {
       p.CF(0.38f, 0.5f, 0.3f); p.CF(0.62f, 0.5f, 0.3f);
       p.dl->PathArcTo(p.P(0.38f, 0.5f), 0.3f * p.s, 1.19f, 5.09f);
       p.dl->PathArcTo(p.P(0.62f, 0.5f), 0.3f * p.s, 4.34f, 8.23f);
       p.dl->PathStroke(p.col, ImDrawFlags_Closed, p.th);
     }},
    {"BooleanDifference", [](const Pen& p) {
       p.dl->PathArcTo(p.P(0.38f, 0.5f), 0.3f * p.s, 1.19f, 5.09f);
       p.dl->PathArcTo(p.P(0.62f, 0.5f), 0.3f * p.s, 4.34f, 1.95f);
       p.dl->PathFillConcave(p.afill);
       p.dl->PathArcTo(p.P(0.38f, 0.5f), 0.3f * p.s, 1.19f, 5.09f);
       p.dl->PathArcTo(p.P(0.62f, 0.5f), 0.3f * p.s, 4.34f, 1.95f);
       p.dl->PathStroke(p.col, ImDrawFlags_Closed, p.th);
       p.C(0.62f, 0.5f, 0.3f, p.acc, p.thin);
     }},
    {"BooleanIntersection", [](const Pen& p) {
       p.dl->PathArcTo(p.P(0.38f, 0.5f), 0.3f * p.s, -1.19f, 1.19f);
       p.dl->PathArcTo(p.P(0.62f, 0.5f), 0.3f * p.s, 1.95f, 4.34f);
       p.dl->PathFillConvex(p.afill);
       p.C(0.38f, 0.5f, 0.3f, p.acc, p.thin); p.C(0.62f, 0.5f, 0.3f, p.acc, p.thin);
       p.dl->PathArcTo(p.P(0.38f, 0.5f), 0.3f * p.s, -1.19f, 1.19f);
       p.dl->PathArcTo(p.P(0.62f, 0.5f), 0.3f * p.s, 1.95f, 4.34f);
       p.dl->PathStroke(p.col, ImDrawFlags_Closed, p.th);
     }},
    {"Move", [](const Pen& p) {
       p.L(0.5f, 0.12f, 0.5f, 0.88f); p.L(0.12f, 0.5f, 0.88f, 0.5f);
       p.Head(0.5f, 0.1f, 0, -1); p.Head(0.5f, 0.9f, 0, 1); p.Head(0.1f, 0.5f, -1, 0); p.Head(0.9f, 0.5f, 1, 0);
     }},
    {"Copy", [](const Pen& p) {
       p.Rect(0.12f, 0.12f, 0.6f, 0.6f, 0.04f, p.acc, p.thin);
       p.RectF(0.38f, 0.38f, 0.88f, 0.88f, 0.04f);
       p.Rect(0.38f, 0.38f, 0.88f, 0.88f, 0.04f);
     }},
    {"Rotate", [](const Pen& p) {
       p.Arc(0.5f, 0.5f, 0.34f, kPi * 0.2f, kPi * 1.75f);
       p.Head(0.5f + 0.34f * std::cos(kPi * 1.75f), 0.5f + 0.34f * std::sin(kPi * 1.75f), 0.7f, 0.7f);
       p.Dot(0.5f, 0.5f);
     }},
    {"Scale", [](const Pen& p) {
       p.Rect(0.12f, 0.5f, 0.5f, 0.88f, 0, p.acc, p.thin);
       p.Rect(0.12f, 0.12f, 0.88f, 0.88f, 0, p.col, p.th);
       p.Arrow(0.4f, 0.6f, 0.78f, 0.22f, p.acc);
     }},
    {"Mirror", [](const Pen& p) {
       p.Dash(0.5f, 0.08f, 0.5f, 0.92f, 5, p.acc);
       p.Fill({0.4f, 0.2f, 0.4f, 0.8f, 0.1f, 0.65f});
       p.Poly({0.4f, 0.2f, 0.4f, 0.8f, 0.1f, 0.65f}, true);
       p.Poly({0.6f, 0.2f, 0.6f, 0.8f, 0.9f, 0.65f}, true, p.col, p.thin);
     }},
    {"Array", [](const Pen& p) {
       for (int i = 0; i < 3; ++i)
         for (int j = 0; j < 2; ++j) {
           const float x = 0.12f + i * 0.29f, y = 0.22f + j * 0.34f;
           p.RectF(x, y, x + 0.2f, y + 0.22f, 0.02f, (i == 0 && j == 0) ? p.acc : p.fill);
           p.Rect(x, y, x + 0.2f, y + 0.22f, 0.02f, p.col, p.thin);
         }
     }},
    {"ArrayPolar", [](const Pen& p) {
       p.C(0.5f, 0.5f, 0.34f, p.acc, p.thin);
       for (int i = 0; i < 6; ++i) {
         const float a = kPi * 2 * i / 6 - kPi / 2;
         p.Dot(0.5f + 0.34f * std::cos(a), 0.5f + 0.34f * std::sin(a), i == 0 ? p.acc : p.col, 0.07f);
       }
       p.Dot(0.5f, 0.5f, p.col, 0.04f);
     }},
    {"Trim", [](const Pen& p) {
       p.Cutter();
       p.Bez(0.08f, 0.6f, 0.25f, 0.2f, 0.4f, 0.9f, 0.5f, 0.5f);
       p.Bez(0.5f, 0.5f, 0.6f, 0.1f, 0.75f, 0.8f, 0.92f, 0.4f, WithAlpha(p.col, 0.3f), p.thin);
       p.L(0.6f, 0.2f, 0.85f, 0.45f, p.acc, p.thin); p.L(0.85f, 0.2f, 0.6f, 0.45f, p.acc, p.thin);
     }},
    {"Split", [](const Pen& p) {
       p.Cutter();
       p.Bez(0.08f, 0.6f, 0.25f, 0.2f, 0.4f, 0.9f, 0.46f, 0.52f);
       p.Bez(0.54f, 0.48f, 0.6f, 0.1f, 0.75f, 0.8f, 0.92f, 0.4f);
       p.Dot(0.46f, 0.52f); p.Dot(0.54f, 0.48f);
     }},
    {"Join", [](const Pen& p) {
       p.Bez(0.08f, 0.75f, 0.25f, 0.2f, 0.4f, 0.9f, 0.5f, 0.5f);
       p.Bez(0.5f, 0.5f, 0.6f, 0.1f, 0.75f, 0.8f, 0.92f, 0.3f);
       p.C(0.5f, 0.5f, 0.12f, p.acc, p.thin);
       p.Dot(0.5f, 0.5f, p.acc, 0.05f);
     }},
    {"Explode", [](const Pen& p) {
       p.Dot(0.5f, 0.5f, p.acc, 0.07f);
       for (int i = 0; i < 8; ++i) {
         const float a = kPi * 2 * i / 8 + 0.3f;
         p.L(0.5f + 0.16f * std::cos(a), 0.5f + 0.16f * std::sin(a), 0.5f + 0.4f * std::cos(a), 0.5f + 0.4f * std::sin(a), i % 2 ? p.acc : p.col, i % 2 ? p.thin : p.th);
       }
     }},
    {"Fillet", [](const Pen& p) {
       p.Poly({0.18f, 0.18f, 0.82f, 0.18f, 0.82f, 0.82f}, false, WithAlpha(p.col, 0.3f), p.thin);
       p.L(0.18f, 0.18f, 0.5f, 0.18f); p.L(0.82f, 0.5f, 0.82f, 0.82f);
       p.Arc(0.5f, 0.5f, 0.32f, -kPi / 2, 0, p.acc);
     }},
    {"Chamfer", [](const Pen& p) {
       p.Poly({0.18f, 0.18f, 0.82f, 0.18f, 0.82f, 0.82f}, false, WithAlpha(p.col, 0.3f), p.thin);
       p.L(0.18f, 0.18f, 0.5f, 0.18f); p.L(0.82f, 0.5f, 0.82f, 0.82f);
       p.L(0.5f, 0.18f, 0.82f, 0.5f, p.acc);
     }},
    {"Offset", [](const Pen& p) {
       p.Bez(0.1f, 0.78f, 0.3f, 0.2f, 0.6f, 0.95f, 0.9f, 0.45f);
       p.Bez(0.1f, 0.5f, 0.3f, -0.08f, 0.6f, 0.67f, 0.9f, 0.17f, p.acc, p.thin);
       p.Dash(0.5f, 0.63f, 0.5f, 0.33f, 3, p.acc);
     }},
    {"Extend", [](const Pen& p) {
       p.L(0.1f, 0.62f, 0.5f, 0.42f);
       p.Dash(0.5f, 0.42f, 0.82f, 0.26f, 3, p.acc);
       p.Head(0.9f, 0.22f, 0.9f, -0.45f, p.acc);
       p.Dot(0.5f, 0.42f);
     }},
    {"Rebuild", [](const Pen& p) {
       p.Bez(0.1f, 0.75f, 0.3f, 0.05f, 0.7f, 0.95f, 0.9f, 0.25f);
       for (int i = 0; i <= 4; ++i) {
         const float t = i / 4.0f, u = 1 - t;
         const float x = u * u * u * 0.1f + 3 * u * u * t * 0.3f + 3 * u * t * t * 0.7f + t * t * t * 0.9f;
         const float y = u * u * u * 0.75f + 3 * u * u * t * 0.05f + 3 * u * t * t * 0.95f + t * t * t * 0.25f;
         p.Dot(x, y, p.acc, 0.06f);
       }
     }},
    {"Delete", [](const Pen& p) {
       p.L(0.15f, 0.26f, 0.85f, 0.26f); p.L(0.4f, 0.26f, 0.42f, 0.14f); p.L(0.42f, 0.14f, 0.58f, 0.14f); p.L(0.58f, 0.14f, 0.6f, 0.26f);
       p.RectF(0.24f, 0.26f, 0.76f, 0.88f, 0.03f);
       p.Poly({0.24f, 0.26f, 0.27f, 0.88f, 0.73f, 0.88f, 0.76f, 0.26f}, false);
       p.L(0.4f, 0.4f, 0.41f, 0.76f, p.acc, p.thin); p.L(0.5f, 0.4f, 0.5f, 0.76f, p.acc, p.thin); p.L(0.6f, 0.4f, 0.59f, 0.76f, p.acc, p.thin);
     }},
    {"Hide", [](const Pen& p) { p.Eye(false); }},
    {"Show", [](const Pen& p) { p.Eye(true); }},
    {"Lock", [](const Pen& p) {
       p.Arc(0.5f, 0.38f, 0.2f, kPi, 2 * kPi);
       p.L(0.3f, 0.38f, 0.3f, 0.5f); p.L(0.7f, 0.38f, 0.7f, 0.5f);
       p.RectF(0.2f, 0.48f, 0.8f, 0.88f, 0.05f, p.afill);
       p.Rect(0.2f, 0.48f, 0.8f, 0.88f, 0.05f);
       p.Dot(0.5f, 0.66f, p.col, 0.06f); p.L(0.5f, 0.66f, 0.5f, 0.76f);
     }},
    {"Group", [](const Pen& p) {
       p.Dash(0.1f, 0.1f, 0.9f, 0.1f, 4, p.acc); p.Dash(0.9f, 0.1f, 0.9f, 0.9f, 4, p.acc);
       p.Dash(0.9f, 0.9f, 0.1f, 0.9f, 4, p.acc); p.Dash(0.1f, 0.9f, 0.1f, 0.1f, 4, p.acc);
       p.RectF(0.22f, 0.24f, 0.5f, 0.52f); p.Rect(0.22f, 0.24f, 0.5f, 0.52f, 0, p.col, p.thin);
       p.CF(0.66f, 0.62f, 0.16f); p.C(0.66f, 0.62f, 0.16f, p.col, p.thin);
       p.Poly({0.25f, 0.8f, 0.45f, 0.58f, 0.55f, 0.8f}, true, p.col, p.thin);
     }},
    {"Layer", [](const Pen& p) {
       for (int i = 2; i >= 0; --i) {
         const float y = 0.3f + i * 0.2f;
         p.Fill({0.1f, y + 0.18f, 0.5f, y - 0.02f, 0.9f, y + 0.18f, 0.5f, y + 0.38f}, i == 0 ? p.afill : p.fill);
         p.Poly({0.1f, y + 0.18f, 0.5f, y - 0.02f, 0.9f, y + 0.18f, 0.5f, y + 0.38f}, true, i == 0 ? p.acc : p.col, i == 0 ? p.th : p.thin);
       }
     }},
    {"Zoom", [](const Pen& p) {
       p.Magnifier();
       p.L(0.42f, 0.28f, 0.42f, 0.56f, p.acc); p.L(0.28f, 0.42f, 0.56f, 0.42f, p.acc);
     }},
    {"ZoomExtents", [](const Pen& p) {
       p.Magnifier();
       for (int i = 0; i < 4; ++i) {
         const float x = i % 2 ? 0.92f : 0.08f, y = i / 2 ? 0.92f : 0.08f;
         const float dx = i % 2 ? -0.16f : 0.16f, dy = i / 2 ? -0.16f : 0.16f;
         p.L(x, y, x + dx, y, p.acc); p.L(x, y, x, y + dy, p.acc);
       }
     }},
    {"Pan", [](const Pen& p) {
       // Open hand: palm plus four fingers and thumb.
       p.RectF(0.3f, 0.45f, 0.72f, 0.9f, 0.1f);
       p.Rect(0.3f, 0.45f, 0.72f, 0.9f, 0.1f);
       for (int i = 0; i < 4; ++i) {
         const float x = 0.33f + i * 0.12f;
         p.L(x, 0.5f, x, i == 1 || i == 2 ? 0.12f : 0.2f, p.col, p.th * 1.5f);
       }
       p.L(0.3f, 0.6f, 0.12f, 0.45f, p.col, p.th * 1.5f);
       p.Arrow(0.78f, 0.35f, 0.94f, 0.35f, p.acc);
     }},
    {"Wireframe", [](const Pen& p) {
       p.Cube(false, false);
       p.L(0.18f, 0.88f, 0.44f, 0.68f, p.acc, p.thin); p.L(0.44f, 0.68f, 0.44f, 0.2f, p.acc, p.thin); p.L(0.44f, 0.68f, 0.86f, 0.68f, p.acc, p.thin);
     }},
    {"Shade", [](const Pen& p) { p.Cube(true, false); }},
    {"Rendered", [](const Pen& p) {
       p.CF(0.5f, 0.52f, 0.36f, WithAlpha(p.col, 0.35f));
       p.C(0.5f, 0.52f, 0.36f);
       p.CF(0.4f, 0.4f, 0.11f, p.afill);
       p.Dot(0.37f, 0.37f, p.acc, 0.05f);
       p.Ellipse(0.5f, 0.9f, 0.34f, 0.06f, WithAlpha(p.col, 0.4f), p.thin);
     }},
    {"Zebra", [](const Pen& p) {
       p.C(0.5f, 0.5f, 0.37f);
       for (int i = 0; i < 4; ++i) {
         const float y = 0.24f + i * 0.17f;
         const float half = std::sqrt(std::max(0.0f, 0.37f * 0.37f - (y - 0.5f) * (y - 0.5f)));
         p.Bez(0.5f - half, y, 0.5f - half * 0.5f, y + 0.09f, 0.5f + half * 0.5f, y + 0.09f, 0.5f + half, y, i % 2 ? p.acc : p.col, p.th * 1.2f);
       }
     }},
    {"Render", [](const Pen& p) {
       p.Rect(0.1f, 0.15f, 0.9f, 0.85f, 0.04f);
       p.Fill({0.14f, 0.82f, 0.36f, 0.5f, 0.52f, 0.66f, 0.66f, 0.45f, 0.86f, 0.82f});
       p.Poly({0.14f, 0.82f, 0.36f, 0.5f, 0.52f, 0.66f, 0.66f, 0.45f, 0.86f, 0.82f}, false, p.col, p.thin);
       p.CF(0.7f, 0.32f, 0.08f, p.acc);
     }},
    {"Mesh", [](const Pen& p) {
       p.Rect(0.12f, 0.12f, 0.88f, 0.88f, 0, p.col, p.thin);
       p.L(0.5f, 0.12f, 0.5f, 0.88f, p.col, p.thin); p.L(0.12f, 0.5f, 0.88f, 0.5f, p.col, p.thin);
       p.L(0.12f, 0.5f, 0.5f, 0.12f, p.acc, p.thin); p.L(0.5f, 0.88f, 0.88f, 0.5f, p.acc, p.thin);
       p.L(0.12f, 0.88f, 0.5f, 0.5f, p.acc, p.thin); p.L(0.5f, 0.5f, 0.88f, 0.12f, p.acc, p.thin);
       p.Fill({0.5f, 0.5f, 0.88f, 0.5f, 0.88f, 0.88f}, p.afill);
     }},
    {"SubDBox", [](const Pen& p) {
       p.RectF(0.18f, 0.4f, 0.62f, 0.88f, 0.12f);
       p.Rect(0.18f, 0.4f, 0.62f, 0.88f, 0.12f);
       p.Bez(0.22f, 0.42f, 0.3f, 0.2f, 0.8f, 0.15f, 0.86f, 0.25f, p.acc);
       p.Bez(0.86f, 0.25f, 0.9f, 0.5f, 0.85f, 0.7f, 0.62f, 0.85f, p.acc);
       p.Bez(0.6f, 0.42f, 0.7f, 0.3f, 0.8f, 0.28f, 0.86f, 0.25f, p.acc, p.thin);
     }},
    {"ToSubD", [](const Pen& p) {
       p.Rect(0.08f, 0.3f, 0.4f, 0.7f, 0, p.col, p.thin);
       p.Arrow(0.44f, 0.5f, 0.6f, 0.5f, p.acc);
       p.RectF(0.62f, 0.3f, 0.94f, 0.7f, 0.14f, p.afill);
       p.Rect(0.62f, 0.3f, 0.94f, 0.7f, 0.14f, p.acc);
     }},
    {"Hatch", [](const Pen& p) {
       p.Rect(0.12f, 0.12f, 0.88f, 0.88f);
       for (int i = 0; i < 5; ++i) {
         const float t = 0.2f + i * 0.2f;
         if (t < 0.8f) p.L(0.12f, 0.12f + t + 0.2f, 0.12f + t + 0.2f, 0.12f, p.acc, p.thin);
         p.L(0.12f + t, 0.88f, 0.88f, 0.12f + t, p.acc, p.thin);
       }
     }},
    {"Make2D", [](const Pen& p) {
       p.Poly({0.3f, 0.36f, 0.58f, 0.36f, 0.58f, 0.62f, 0.3f, 0.62f}, true, p.col, p.thin);
       p.Poly({0.3f, 0.36f, 0.42f, 0.24f, 0.7f, 0.24f, 0.58f, 0.36f}, true, p.col, p.thin);
       p.Poly({0.7f, 0.24f, 0.7f, 0.5f, 0.58f, 0.62f}, false, p.col, p.thin);
       p.Arrow(0.82f, 0.3f, 0.82f, 0.62f, p.acc);
       p.Fill({0.12f, 0.9f, 0.32f, 0.72f, 0.92f, 0.72f, 0.72f, 0.9f}, p.afill);
       p.Poly({0.12f, 0.9f, 0.32f, 0.72f, 0.92f, 0.72f, 0.72f, 0.9f}, true);
     }},
    {"Block", [](const Pen& p) {
       p.Cube(true, false);
       p.Dot(0.18f, 0.88f, p.acc, 0.09f);
       p.L(0.06f, 0.88f, 0.3f, 0.88f, p.acc, p.thin); p.L(0.18f, 0.76f, 0.18f, 1.0f, p.acc, p.thin);
     }},
    {"Insert", [](const Pen& p) {
       p.Poly({0.12f, 0.36f, 0.5f, 0.36f, 0.5f, 0.78f, 0.12f, 0.78f}, true);
       p.Poly({0.12f, 0.36f, 0.32f, 0.18f, 0.7f, 0.18f, 0.5f, 0.36f}, true);
       p.Poly({0.7f, 0.18f, 0.7f, 0.58f, 0.5f, 0.78f}, false);
       p.CF(0.78f, 0.76f, 0.17f, p.acc);
       p.L(0.78f, 0.66f, 0.78f, 0.86f, WithAlpha(IM_COL32_WHITE, 0.95f), p.thin); p.L(0.68f, 0.76f, 0.88f, 0.76f, WithAlpha(IM_COL32_WHITE, 0.95f), p.thin);
     }},
    {"Distance", [](const Pen& p) {
       p.L(0.15f, 0.7f, 0.85f, 0.3f, p.acc);
       p.Dot(0.15f, 0.7f, p.col, 0.07f); p.Dot(0.85f, 0.3f, p.col, 0.07f);
       p.L(0.08f, 0.58f, 0.22f, 0.82f, p.col, p.thin); p.L(0.78f, 0.18f, 0.92f, 0.42f, p.col, p.thin);
     }},
    {"Length", [](const Pen& p) {
       p.Bez(0.12f, 0.7f, 0.3f, 0.1f, 0.7f, 0.9f, 0.88f, 0.3f);
       p.L(0.06f, 0.62f, 0.18f, 0.8f, p.acc); p.L(0.82f, 0.22f, 0.94f, 0.4f, p.acc);
       p.Dash(0.12f, 0.88f, 0.88f, 0.88f, 6, p.acc);
     }},
    {"Area", [](const Pen& p) {
       p.dl->PathLineTo(p.P(0.12f, 0.6f));
       p.dl->PathBezierCubicCurveTo(p.P(0.05f, 0.2f), p.P(0.55f, 0.02f), p.P(0.72f, 0.28f));
       p.dl->PathBezierCubicCurveTo(p.P(0.98f, 0.5f), p.P(0.88f, 0.92f), p.P(0.5f, 0.9f));
       p.dl->PathBezierCubicCurveTo(p.P(0.2f, 0.9f), p.P(0.22f, 0.85f), p.P(0.12f, 0.6f));
       p.dl->PathFillConvex(p.afill);
       p.Bez(0.12f, 0.6f, 0.05f, 0.2f, 0.55f, 0.02f, 0.72f, 0.28f);
       p.Bez(0.72f, 0.28f, 0.98f, 0.5f, 0.88f, 0.92f, 0.5f, 0.9f);
       p.Bez(0.5f, 0.9f, 0.2f, 0.9f, 0.22f, 0.85f, 0.12f, 0.6f);
       for (int i = 0; i < 3; ++i) p.L(0.3f + i * 0.15f, 0.72f, 0.5f + i * 0.15f, 0.36f, p.acc, p.thin);
     }},
    {"Volume", [](const Pen& p) {
       p.Cube(true, true);
       p.L(0.3f, 0.7f, 0.5f, 0.55f, p.acc, p.thin); p.L(0.3f, 0.78f, 0.5f, 0.63f, p.acc, p.thin);
     }},
    {"Options", [](const Pen& p) {
       for (int i = 0; i < 8; ++i) {
         const float a = kPi * 2 * i / 8;
         p.L(0.5f + 0.24f * std::cos(a), 0.5f + 0.24f * std::sin(a), 0.5f + 0.4f * std::cos(a), 0.5f + 0.4f * std::sin(a), p.col, p.th * 1.8f);
       }
       p.CF(0.5f, 0.5f, 0.27f, p.fill);
       p.C(0.5f, 0.5f, 0.27f);
       p.C(0.5f, 0.5f, 0.1f, p.acc);
     }},
    {"Help", [](const Pen& p) {
       p.C(0.5f, 0.5f, 0.4f);
       p.Bez(0.36f, 0.4f, 0.36f, 0.2f, 0.66f, 0.2f, 0.64f, 0.4f, p.acc);
       p.Bez(0.64f, 0.4f, 0.64f, 0.5f, 0.5f, 0.48f, 0.5f, 0.62f, p.acc);
       p.Dot(0.5f, 0.74f, p.acc, 0.05f);
     }},
    {"SelAll", [](const Pen& p) {
       p.Dash(0.1f, 0.1f, 0.9f, 0.1f, 4); p.Dash(0.9f, 0.1f, 0.9f, 0.9f, 4); p.Dash(0.9f, 0.9f, 0.1f, 0.9f, 4); p.Dash(0.1f, 0.9f, 0.1f, 0.1f, 4);
       p.L(0.28f, 0.52f, 0.44f, 0.7f, p.col, p.th * 1.3f); p.L(0.44f, 0.7f, 0.74f, 0.32f, p.col, p.th * 1.3f);
     }},
    {"Cut", [](const Pen& p) {
       p.C(0.3f, 0.75f, 0.13f); p.C(0.7f, 0.75f, 0.13f);
       p.L(0.38f, 0.65f, 0.72f, 0.12f); p.L(0.62f, 0.65f, 0.28f, 0.12f);
       p.Dot(0.5f, 0.46f, p.acc, 0.045f);
     }},
    {"Paste", [](const Pen& p) {
       p.Rect(0.18f, 0.18f, 0.72f, 0.88f, 0.04f);
       p.RectF(0.36f, 0.1f, 0.54f, 0.26f, 0.03f, p.acc);
       p.RectF(0.42f, 0.42f, 0.9f, 0.94f, 0.03f);
       p.Rect(0.42f, 0.42f, 0.9f, 0.94f, 0.03f, p.col, p.thin);
     }},
    {"Ungroup", [](const Pen& p) {
       p.RectF(0.1f, 0.3f, 0.36f, 0.56f); p.Rect(0.1f, 0.3f, 0.36f, 0.56f, 0, p.col, p.thin);
       p.CF(0.74f, 0.44f, 0.15f); p.C(0.74f, 0.44f, 0.15f, p.col, p.thin);
       p.Poly({0.34f, 0.9f, 0.5f, 0.66f, 0.64f, 0.9f}, true, p.col, p.thin);
       p.L(0.44f, 0.24f, 0.56f, 0.12f, p.acc); p.L(0.44f, 0.12f, 0.56f, 0.24f, p.acc);
     }},
    {"Unlock", [](const Pen& p) {
       p.Arc(0.62f, 0.3f, 0.18f, kPi, 2 * kPi);
       p.L(0.44f, 0.3f, 0.44f, 0.4f); p.L(0.8f, 0.3f, 0.8f, 0.48f);
       p.RectF(0.2f, 0.48f, 0.72f, 0.88f, 0.05f, p.afill);
       p.Rect(0.2f, 0.48f, 0.72f, 0.88f, 0.05f);
       p.Dot(0.46f, 0.66f, p.col, 0.06f); p.L(0.46f, 0.66f, 0.46f, 0.76f);
     }},
    {"Isolate", [](const Pen& p) {
       p.Eye(true);
       for (int i = 0; i < 4; ++i) {
         const float x = i % 2 ? 0.94f : 0.06f, y = i / 2 ? 0.94f : 0.06f;
         const float dx = i % 2 ? -0.14f : 0.14f, dy = i / 2 ? -0.14f : 0.14f;
         p.L(x, y, x + dx, y, p.acc, p.thin); p.L(x, y, x, y + dy, p.acc, p.thin);
       }
     }},
    {"Cap", [](const Pen& p) {
       p.RectF(0.2f, 0.4f, 0.8f, 0.8f);
       p.L(0.2f, 0.4f, 0.2f, 0.8f); p.L(0.8f, 0.4f, 0.8f, 0.8f);
       p.Ellipse(0.5f, 0.8f, 0.3f, 0.1f, p.col, 0, 0, kPi);
       p.EllipseF(0.5f, 0.4f, 0.3f, 0.1f, p.afill);
       p.Ellipse(0.5f, 0.4f, 0.3f, 0.1f, p.acc);
       p.Arrow(0.5f, 0.06f, 0.5f, 0.3f, p.acc);
     }},
    {"Shell", [](const Pen& p) {
       p.Cube(false, false);
       p.Poly({0.26f, 0.5f, 0.52f, 0.5f, 0.52f, 0.8f, 0.26f, 0.8f}, true, p.acc, p.thin);
       p.Fill({0.18f, 0.40f, 0.44f, 0.20f, 0.86f, 0.20f, 0.60f, 0.40f}, p.afill);
     }},
    {"Perspective", [](const Pen& p) { p.Cube(true, true); }},
    {"Top", [](const Pen& p) { p.RectF(0.15f, 0.15f, 0.85f, 0.85f, 0.03f, p.afill); p.Rect(0.15f, 0.15f, 0.85f, 0.85f, 0.03f); p.L(0.5f, 0.15f, 0.5f, 0.85f, p.col, p.thin); p.L(0.15f, 0.5f, 0.85f, 0.5f, p.col, p.thin); }},
    {"4View", [](const Pen& p) {
       p.Rect(0.1f, 0.12f, 0.9f, 0.88f, 0.03f);
       p.L(0.5f, 0.12f, 0.5f, 0.88f); p.L(0.1f, 0.5f, 0.9f, 0.5f);
       p.RectF(0.52f, 0.14f, 0.88f, 0.48f, 0, p.afill);
     }},
    {"What", [](const Pen& p) {
       p.Rect(0.16f, 0.1f, 0.84f, 0.9f, 0.04f);
       for (int i = 0; i < 4; ++i) p.L(0.28f, 0.26f + i * 0.16f, i == 3 ? 0.5f : 0.72f, 0.26f + i * 0.16f, i == 0 ? p.acc : p.col, p.thin);
     }},
};

// Commands that reuse another glyph.
struct Alias { const char* name; const char* glyph; };
const Alias kAliases[] = {
    {"Extrude", "ExtrudeCrv"}, {"SaveAs", "Save"}, {"Circle3Pt", "Circle"}, {"CircleD", "Circle"}, {"Arc3Pt", "Arc"}, {"ArcSED", "Arc"},
    {"Rectangle3Pt", "Rectangle"}, {"PolygonStar", "Polygon"}, {"Ellipse3Pt", "Ellipse"}, {"TextObject", "Text"},
    {"DimLinear", "Dim"}, {"DimAligned", "Dim"}, {"DimRadius", "Dim"}, {"DimAngle", "Dim"}, {"DimDiameter", "Dim"}, {"Leader", "Dim"},
    {"ExtrudeSrf", "ExtrudeCrv"}, {"ExtrudeCrvTapered", "ExtrudeCrv"}, {"ExtrudeCrvAlongCrv", "ExtrudeCrv"}, {"ExtrudeMesh", "ExtrudeCrv"},
    {"Loft2", "Loft"}, {"SubDLoft", "Loft"}, {"SubDSweep1", "Sweep1"}, {"SubDSweep2", "Sweep2"}, {"SubDRevolve", "Revolve"}, {"RailRevolve", "Revolve"},
    {"MeshBooleanUnion", "BooleanUnion"}, {"MeshBooleanDifference", "BooleanDifference"}, {"MeshBooleanIntersection", "BooleanIntersection"},
    {"BooleanSplit", "Split"}, {"MeshSplit", "Split"}, {"MeshTrim", "Trim"}, {"MeshBooleanSplit", "Split"},
    {"CopyToClipboard", "Copy"}, {"Rotate3D", "Rotate"}, {"ScaleNU", "Scale"}, {"Scale1D", "Scale"}, {"Scale2D", "Scale"},
    {"ArrayLinear", "Array"}, {"ArrayCrv", "Array"}, {"FilletEdge", "Fillet"}, {"ChamferEdge", "Chamfer"}, {"FilletCorners", "Fillet"},
    {"FilletSrf", "Fillet"}, {"ChamferSrf", "Chamfer"}, {"OffsetMesh", "OffsetSrf"}, {"OffsetSubD", "OffsetSrf"}, {"ExtendSrf", "Extend"},
    {"RebuildMesh", "Rebuild"}, {"RebuildUV", "Rebuild"}, {"ShowSelected", "Show"}, {"HideSwap", "Hide"}, {"UnlockSelected", "Unlock"},
    {"ZoomExtentsAll", "ZoomExtents"}, {"ZoomSelected", "Zoom"}, {"ZoomWindow", "Zoom"}, {"RotateView", "Rotate"},
    {"ShadedViewport", "Shade"}, {"RenderedViewport", "Rendered"}, {"GhostedViewport", "Shade"}, {"XRayViewport", "Wireframe"},
    {"ZebraOff", "Zebra"}, {"EMap", "Rendered"}, {"CurvatureAnalysis", "Zebra"}, {"RenderPreview", "Render"}, {"RenderSettings", "Options"},
    {"MeshBox", "Box"}, {"MeshSphere", "Sphere"}, {"MeshCylinder", "Cylinder"}, {"MeshCone", "Cone"}, {"MeshTorus", "Torus"}, {"MeshPlane", "Plane"},
    {"SubDSphere", "Sphere"}, {"SubDCylinder", "Cylinder"}, {"SubDCone", "Cone"}, {"SubDTorus", "Torus"}, {"SubDPlane", "Plane"},
    {"ToNURBS", "ToSubD"}, {"MeshToNURB", "ToSubD"}, {"MeshFromSubD", "Mesh"}, {"QuadRemesh", "Mesh"}, {"ReduceMesh", "Mesh"},
    {"TriangulateMesh", "Mesh"}, {"Weld", "Join"}, {"Unweld", "Explode"}, {"MeshRepair", "Mesh"}, {"FillMeshHoles", "Cap"},
    {"BlockManager", "Block"}, {"BlockEdit", "Block"}, {"Angle", "Dim"}, {"Radius", "Circle"}, {"Diameter", "Circle"},
    {"BoundingBox", "SelAll"}, {"Front", "Top"}, {"Right", "Top"}, {"Back", "Top"}, {"Left", "Top"}, {"Bottom", "Top"},
    {"3View", "4View"}, {"MaxViewport", "Top"}, {"Tube", "Pipe"}, {"MultiPipe", "Pipe"}, {"SubDMultiPipe", "Pipe"},
    {"Patch", "PlanarSrf"}, {"EdgeSrf", "PlanarSrf"}, {"SrfPt", "Plane"}, {"BlendSrf", "Fillet"}, {"BlendCrv", "Fillet"}, {"Blend", "Fillet"},
    {"MatchSrf", "Join"}, {"MergeSrf", "Join"}, {"Untrim", "Trim"}, {"Intersect", "BooleanIntersection"}, {"Project", "Make2D"},
    {"Pull", "Make2D"}, {"Section", "Split"}, {"Contour", "Loft"}, {"Silhouette", "Make2D"}, {"DupBorder", "Offset"}, {"DupEdge", "Line"},
    {"PointsOn", "Rebuild"}, {"PointsOff", "Curve"}, {"Handlebar", "Curve"}, {"Sketch", "Curve"}, {"Helix", "Sweep1"}, {"Spiral", "Rotate"},
    {"Orient", "Mirror"}, {"Orient3Pt", "Mirror"}, {"Align", "Array"}, {"Distribute", "Array"}, {"Twist", "Rotate"}, {"Bend", "Arc"},
    {"Taper", "Cone"}, {"Flow", "Sweep1"}, {"CageEdit", "Group"}, {"Gumball", "Move"}, {"BoxEdit", "Box"}, {"SetPt", "Point"},
    {"Nudge", "Move"}, {"Import", "Open"}, {"Export", "Save"}, {"Print", "Make2D"}, {"Properties", "What"}, {"List", "What"},
    {"Check", "SelAll"}, {"Dir", "Extend"}, {"Curvature", "Arc"}, {"CurvatureGraph", "Zebra"}, {"DraftAngleAnalysis", "Cone"},
    {"EvaluatePt", "Point"}, {"ClosestPt", "Point"}, {"Divide", "Rebuild"}, {"Points", "Point"}, {"PointGrid", "Array"},
    {"HatchBase", "Hatch"}, {"Annotate", "Text"}, {"DimStyles", "Options"}, {"Lights", "Rendered"}, {"PointLight", "Rendered"},
    {"Spotlight", "Cone"}, {"DirectionalLight", "Extend"}, {"Materials", "Rendered"}, {"Environment", "Sphere"}, {"Sun", "Rendered"},
    {"GroundPlane", "Plane"}, {"ViewCaptureToFile", "Render"}, {"NamedView", "Perspective"}, {"CPlane", "Plane"}, {"Grid", "Mesh"},
    {"UndoView", "Undo"}, {"RedoView", "Redo"}, {"Crease", "Chamfer"}, {"Bevel", "Chamfer"}, {"Bridge", "Loft"}, {"InsertEdge", "Split"},
    {"SubDThicken", "OffsetSrf"}, {"Reflect", "Mirror"}, {"Symmetry", "Mirror"}, {"SubDivide", "Mesh"}, {"Stitch", "Join"}, {"Fill", "Cap"},
    {"Append", "PlanarSrf"}, {"SubDDisplayToggle", "SubDBox"}, {"ExtrudeSubD", "ExtrudeCrv"}, {"MeshOutline", "Make2D"},
    {"SelNone", "SelAll"}, {"Invert", "SelAll"}, {"SelCrv", "Curve"}, {"SelSrf", "Plane"}, {"SelPolysrf", "Box"}, {"SelMesh", "Mesh"},
    {"SelPt", "Point"}, {"SelLast", "SelAll"}, {"SelPrev", "SelAll"}, {"SelDup", "Copy"}, {"Repeat", "Redo"}, {"Exit", "Delete"},
    {"Revert", "Undo"}, {"CommandList", "What"}, {"CommandHistory", "What"}, {"Notifications", "Help"}, {"Calc", "Dim"},
    {"Layers", "Layer"}, {"LayerStateManager", "Layer"}, {"ObjectProperties", "What"}, {"PictureFrame", "Render"},
};

const Glyph* FindGlyph(const char* command) {
  if (!command) return nullptr;
  for (const Glyph& g : kGlyphs) if (std::strcmp(g.name, command) == 0) return &g;
  for (const Alias& a : kAliases) if (std::strcmp(a.name, command) == 0) return FindGlyph(a.glyph);
  // Case-insensitive second pass (commands are case-insensitive).
  const std::string lower = ToLower(command);
  for (const Glyph& g : kGlyphs) if (ToLower(g.name) == lower) return &g;
  for (const Alias& a : kAliases) if (ToLower(a.name) == lower) return FindGlyph(a.glyph);
  return nullptr;
}

// Two or three significant letters of a command name for the fallback box
// ("BooleanUnion" -> "BU", "SelAll" -> "SA", "What" -> "Wh").
std::string ShortLabel(const char* command) {
  std::string caps;
  for (const char* c = command; *c; ++c) if (*c >= 'A' && *c <= 'Z') caps += *c;
  if (caps.size() >= 2) return caps.substr(0, 3);
  std::string s = command;
  if (s.empty()) return "?";
  if (s.size() == 1) return s;
  return s.substr(0, 2);
}

}  // namespace

bool HasIcon(const char* command) { return FindGlyph(command) != nullptr; }

void DrawIcon(ImDrawList* dl, const char* command, ImVec2 pos, float size, ImU32 color, ImU32 accent) {
  if (!dl || size <= 0) return;
  if (!accent) accent = DeriveAccent(color);
  Pen p;
  p.dl = dl;
  p.o = pos;
  p.s = size;
  p.col = color;
  p.acc = accent;
  p.fill = WithAlpha(color, 0.20f);
  p.afill = WithAlpha(accent, 0.40f);
  p.th = std::max(1.25f, size * 0.068f);
  p.thin = std::max(1.0f, p.th * 0.6f);
  if (const Glyph* g = FindGlyph(command)) {
    g->draw(p);
    return;
  }
  // Fallback: rounded box with the command's initials.
  p.RectF(0.06f, 0.06f, 0.94f, 0.94f, 0.16f);
  p.Rect(0.06f, 0.06f, 0.94f, 0.94f, 0.16f, accent, p.thin);
  const std::string label = ShortLabel(command ? command : "?");
  ImFont* font = ImGui::GetFont();
  const float fsize = size * (label.size() >= 3 ? 0.36f : 0.44f);
  const ImVec2 ts = font->CalcTextSizeA(fsize, FLT_MAX, 0.0f, label.c_str());
  dl->AddText(font, fsize, ImVec2(pos.x + (size - ts.x) * 0.5f, pos.y + (size - ts.y) * 0.5f), color, label.c_str());
}

}  // namespace dino8::app
