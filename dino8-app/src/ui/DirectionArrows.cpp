#include "ui/DirectionArrows.h"

#include <cmath>
#include <optional>
#include <vector>

#include "app/Application.h"
#include "commands/cmd_common.h"
#include "imgui.h"
#include "ui/Gumball.h"
#include "viewport/Viewport.h"

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

namespace {
struct Glyph {
  ObjectId id;
  ImVec2 base, tip;
};
}  // namespace

bool DirectionArrows::Update(Application& app, Viewport& vp, bool viewport_hovered) {
  Document& doc = app.Doc();
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 origin(static_cast<float>(vp.ScreenX()), static_cast<float>(vp.ScreenY()));
  // Same "about 60/70 screen pixels regardless of zoom" convention Gumball
  // uses for its own axis length (Gumball::Update, ui/Gumball.cpp).
  const double len = 60.0 * vp.GetCamera().PixelSize(vp.Height());

  // Project every flagged object's glyph (ComputeDirArrow - cmd_common.h,
  // the exact same origin/direction Dir itself printed) into this
  // viewport's screen space once, then hit-test and draw from that.
  std::vector<Glyph> glyphs;
  for (SceneObject& o : doc.Objects()) {
    if (!o.show_direction_arrow) continue;
    if (!doc.IsObjectVisible(o) || doc.IsObjectLocked(o)) continue;
    const std::optional<DirArrow> a = ComputeDirArrow(o);
    if (!a) continue;
    Vector3d dir = a->direction;
    if (!dir.Unitize()) continue;
    double bx, by, tx, ty;
    if (!vp.WorldToPixel(a->origin, bx, by)) continue;
    if (!vp.WorldToPixel(a->origin + dir * len, tx, ty)) continue;
    glyphs.push_back({o.id, ImVec2(origin.x + static_cast<float>(bx), origin.y + static_cast<float>(by)),
                       ImVec2(origin.x + static_cast<float>(tx), origin.y + static_cast<float>(ty))});
  }

  const ImVec2 m = io.MousePos;
  hover_ = kNoObject;
  if (viewport_hovered) {
    double best = 1e300;
    for (const Glyph& g : glyphs) {
      const double d = DistToSegmentPx(m, g.base, g.tip);
      if (d < 9.0 && d < best) {
        best = d;
        hover_ = g.id;
      }
    }
    if (hover_ != kNoObject && ImGui::IsMouseClicked(0)) {
      if (SceneObject* o = doc.Find(hover_)) {
        // The click's whole job: call the exact same reversal Flip
        // performs (FlipObject, cmd_common.h) - never a second,
        // independent curve/surface-reversal implementation.
        doc.BeginChange("Flip");
        FlipObject(*o);
        doc.Touch();
        app.Engine().Print("Flip (direction arrow): reversed object " + std::to_string(hover_));
      }
    }
  }

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->PushClipRect(origin, ImVec2(origin.x + static_cast<float>(vp.Width()), origin.y + static_cast<float>(vp.Height())), true);
  for (const Glyph& g : glyphs) {
    const bool hot = hover_ == g.id;
    const ImU32 color = hot ? IM_COL32(255, 205, 60, 255) : IM_COL32(255, 150, 0, 255);
    dl->AddLine(g.base, g.tip, color, hot ? 3.5f : 2.5f);
    const float dx = g.tip.x - g.base.x, dy = g.tip.y - g.base.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length > 1.0f) {
      const float ux = dx / length, uy = dy / length;
      const float s = hot ? 11.0f : 9.0f;
      const ImVec2 p1(g.tip.x - ux * s + uy * s * 0.5f, g.tip.y - uy * s - ux * s * 0.5f);
      const ImVec2 p2(g.tip.x - ux * s - uy * s * 0.5f, g.tip.y - uy * s + ux * s * 0.5f);
      dl->AddTriangleFilled(g.tip, p1, p2, color);
    }
    dl->AddCircleFilled(g.base, hot ? 5.0f : 3.5f, color);
  }
  dl->PopClipRect();

  return hover_ != kNoObject;
}

}  // namespace dino8::app
