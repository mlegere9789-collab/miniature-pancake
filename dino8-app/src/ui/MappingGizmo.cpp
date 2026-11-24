#include "ui/MappingGizmo.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "app/Application.h"
#include "imgui.h"
#include "ui/Gumball.h"
#include "viewport/Viewport.h"

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

namespace {

// Seeds a usable custom mapping frame from the object's own bounding box -
// the same fallback ApplyPlanarMapping/EnsureMappedUVs already use for an
// object with no picked plane - so the gizmo always has something sensible
// to draw and drag, even before ApplyCustomMapping has ever run on this
// object.
void EnsureMappingFrame(SceneObject& obj) {
  if (obj.has_custom_mapping_frame) return;
  const kernel::BoundingBox bb = obj.BoundingBox();
  obj.custom_mapping_origin = Point3d((bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2, bb.min.z);
  obj.custom_mapping_x = Vector3d(1, 0, 0);
  obj.custom_mapping_y = Vector3d(0, 1, 0);
  const double sx = bb.max.x - bb.min.x, sy = bb.max.y - bb.min.y;
  obj.custom_mapping_size = std::max(1e-6, std::max(sx, sy));
  obj.has_custom_mapping_frame = true;
}

double DistToSegment(ImVec2 p, ImVec2 a, ImVec2 b) { return DistToSegmentPx(p, a, b); }

}  // namespace

bool MappingGizmo::Update(Application& app, Viewport& vp, SceneObject& obj, bool viewport_hovered) {
  Document& doc = app.Doc();
  ImGuiIO& io = ImGui::GetIO();
  const int vp_index = [&]() { int i = 0; for (auto& v : app.Viewports()) { if (v.get() == &vp) return i; ++i; } return -1; }();

  if (dragging_ && obj.id != drag_object_) {
    // The target changed mid-drag (selection changed underneath us) -
    // cancel rather than risk writing to the wrong object.
    dragging_ = false;
    hover_ = Handle::None;
  }
  if (dragging_ && vp_index != drag_viewport_) return false;

  if (!dragging_) EnsureMappingFrame(obj);

  const Point3d center = dragging_ ? start_origin_ : obj.custom_mapping_origin;
  Vector3d x_axis = dragging_ ? start_x_ : obj.custom_mapping_x;
  Vector3d y_axis = dragging_ ? start_y_ : obj.custom_mapping_y;
  if (!x_axis.Unitize()) x_axis = Vector3d(1, 0, 0);
  if (!y_axis.Unitize()) y_axis = Vector3d(0, 1, 0);
  Vector3d z_axis = ON_CrossProduct(x_axis, y_axis);
  if (!z_axis.Unitize()) z_axis = Vector3d(0, 0, 1);
  const std::array<Vector3d, 3> axes = {x_axis, y_axis, z_axis};
  auto AxisDir = [&](int a) { return axes[static_cast<size_t>(a)]; };

  const double axis_len = 80.0 * vp.GetCamera().PixelSize(vp.Height());

  double cx, cy;
  if (!vp.WorldToPixel(center, cx, cy)) return false;
  const ImVec2 origin(static_cast<float>(vp.ScreenX()), static_cast<float>(vp.ScreenY()));
  const ImVec2 c(origin.x + static_cast<float>(cx), origin.y + static_cast<float>(cy));
  ImVec2 tips[3];
  bool tip_ok[3];
  for (int a = 0; a < 3; ++a) {
    double tx, ty;
    tip_ok[a] = vp.WorldToPixel(center + AxisDir(a) * axis_len, tx, ty);
    tips[a] = ImVec2(origin.x + static_cast<float>(tx), origin.y + static_cast<float>(ty));
  }
  constexpr int kRing = 40;
  ImVec2 rings[3][kRing];
  bool ring_ok[3] = {false, false, false};
  const double ring_r = axis_len * 0.85;
  for (int a = 0; a < 3; ++a) {
    const Vector3d u = AxisDir((a + 1) % 3), v = AxisDir((a + 2) % 3);
    bool ok = true;
    for (int k = 0; k < kRing; ++k) {
      const double ang = 2 * ON_PI * k / kRing;
      double px, py;
      ok = ok && vp.WorldToPixel(center + u * (ring_r * std::cos(ang)) + v * (ring_r * std::sin(ang)), px, py);
      rings[a][k] = ImVec2(origin.x + static_cast<float>(px), origin.y + static_cast<float>(py));
    }
    ring_ok[a] = ok;
  }
  ImVec2 scale_pts[3];
  bool scale_ok[3];
  for (int a = 0; a < 3; ++a) {
    double px, py;
    scale_ok[a] = vp.WorldToPixel(center + AxisDir(a) * (axis_len * 1.3), px, py);
    scale_pts[a] = ImVec2(origin.x + static_cast<float>(px), origin.y + static_cast<float>(py));
  }
  auto plane_point = [&](const Ray& ray, int axis, Point3d& out) { return RayPlane(ray, center, AxisDir(axis), out); };
  auto angle_on_plane = [&](const Point3d& p, int axis) {
    const Vector3d u = AxisDir((axis + 1) % 3), v = AxisDir((axis + 2) % 3);
    const Vector3d d = p - center;
    return std::atan2(ON_DotProduct(d, v), ON_DotProduct(d, u));
  };

  const ImVec2 m = io.MousePos;
  if (!dragging_) {
    hover_ = Handle::None;
    if (viewport_hovered) {
      if (std::hypot(m.x - c.x, m.y - c.y) < 9.0f) hover_ = Handle::Free;
      else {
        for (int a = 0; a < 3; ++a) {
          if (tip_ok[a] && DistToSegment(m, c, tips[a]) < 6.0f && std::hypot(tips[a].x - c.x, tips[a].y - c.y) > 12.0f) hover_ = static_cast<Handle>(a + 1);
        }
        if (hover_ == Handle::None) {
          for (int a = 0; a < 3; ++a) {
            if (scale_ok[a] && std::fabs(m.x - scale_pts[a].x) < 7.0f && std::fabs(m.y - scale_pts[a].y) < 7.0f) hover_ = static_cast<Handle>(static_cast<int>(Handle::ScaleX) + a);
          }
        }
        if (hover_ == Handle::None) {
          for (int a = 0; a < 3; ++a) {
            if (!ring_ok[a]) continue;
            for (int k = 0; k < kRing; ++k) {
              if (DistToSegment(m, rings[a][k], rings[a][(k + 1) % kRing]) < 5.0f) { hover_ = static_cast<Handle>(static_cast<int>(Handle::RotX) + a); break; }
            }
            if (hover_ != Handle::None) break;
          }
        }
      }
    }
    if (hover_ != Handle::None && ImGui::IsMouseClicked(0)) {
      dragging_ = true;
      drag_handle_ = hover_;
      drag_viewport_ = vp_index;
      drag_object_ = obj.id;
      start_origin_ = obj.custom_mapping_origin;
      start_x_ = x_axis;
      start_y_ = y_axis;
      start_size_ = obj.custom_mapping_size;
      doc.BeginChange("MappingWidget");
      const Ray ray = vp.PixelRay(m.x - origin.x, m.y - origin.y);
      const int h = static_cast<int>(drag_handle_);
      if (drag_handle_ == Handle::Free) {
        RayPlane(ray, center, z_axis, start_free_);
      } else if (h >= static_cast<int>(Handle::RotX) && h <= static_cast<int>(Handle::RotZ)) {
        Point3d p;
        const int axis = h - static_cast<int>(Handle::RotX);
        start_angle_ = plane_point(ray, axis, p) ? angle_on_plane(p, axis) : 0.0;
      } else if (h >= static_cast<int>(Handle::ScaleX)) {
        const int axis = h - static_cast<int>(Handle::ScaleX);
        start_param_ = ClosestParamOnLine(ray, center, AxisDir(axis));
      } else {
        start_param_ = ClosestParamOnLine(ray, center, AxisDir(h - 1));
      }
    }
  } else {
    const Ray ray = vp.PixelRay(m.x - origin.x, m.y - origin.y);
    const int h = static_cast<int>(drag_handle_);
    std::string what;
    if (drag_handle_ == Handle::Free) {
      Point3d now;
      if (RayPlane(ray, center, z_axis, now)) {
        obj.custom_mapping_origin = start_origin_ + (now - start_free_);
        what = "moved the mapping frame's origin";
      }
    } else if (h >= static_cast<int>(Handle::RotX) && h <= static_cast<int>(Handle::RotZ)) {
      const int axis = h - static_cast<int>(Handle::RotX);
      Point3d p;
      double angle = 0;
      if (plane_point(ray, axis, p)) angle = angle_on_plane(p, axis) - start_angle_;
      if (io.KeyShift) angle = std::round(angle / (ON_PI / 12)) * (ON_PI / 12);  // 15 degree steps
      ON_Xform xf = ON_Xform::IdentityTransformation;
      xf.Rotation(angle, AxisDir(axis), center);
      obj.custom_mapping_x = xf * start_x_;
      obj.custom_mapping_y = xf * start_y_;
      what = "rotated the mapping frame " + std::to_string(angle * 180.0 / ON_PI) + " degrees about its own " + "XYZ"[axis];
    } else if (h >= static_cast<int>(Handle::ScaleX)) {
      // Honest simplification (see MappingGizmo.h): every scale handle
      // uniformly scales the one scalar custom_mapping_size this object
      // has, since there is no independent per-axis mapping size to scale.
      const int axis = h - static_cast<int>(Handle::ScaleX);
      const double now = ClosestParamOnLine(ray, center, AxisDir(axis));
      double f = (std::fabs(start_param_) > 1e-9) ? now / start_param_ : 1.0;
      if (!std::isfinite(f) || f < 1e-3) f = 1e-3;
      obj.custom_mapping_size = start_size_ * f;
      what = "scaled the mapping frame by " + std::to_string(f);
    } else {
      const Vector3d dir = AxisDir(h - 1);
      const double now = ClosestParamOnLine(ray, center, dir);
      obj.custom_mapping_origin = start_origin_ + dir * (now - start_param_);
      what = "moved the mapping frame's origin";
    }
    obj.mapping = TextureMapping::Custom;
    obj.InvalidateDisplay();
    if (!ImGui::IsMouseDown(0)) {
      dragging_ = false;
      doc.Touch();
      app.Engine().Print("MappingWidget: " + what + " on '" + (obj.name.empty() ? std::string("(unnamed)") : obj.name) + "'");
    }
  }

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->PushClipRect(origin, ImVec2(origin.x + static_cast<float>(vp.Width()), origin.y + static_cast<float>(vp.Height())), true);
  // A distinct orange/teal/violet palette (rather than Gumball's red/green/
  // blue) so the two widgets stay visually distinguishable when both are
  // visible on the same object at once.
  const ImU32 colors[3] = {IM_COL32(240, 150, 40, 255), IM_COL32(40, 200, 190, 255), IM_COL32(180, 110, 235, 255)};
  const ImU32 dim[3] = {IM_COL32(240, 150, 40, 120), IM_COL32(40, 200, 190, 120), IM_COL32(180, 110, 235, 120)};
  for (int a = 0; a < 3; ++a) {
    if (!ring_ok[a]) continue;
    const Handle hr = static_cast<Handle>(static_cast<int>(Handle::RotX) + a);
    const bool hot = hover_ == hr || (dragging_ && drag_handle_ == hr);
    for (int k = 0; k < kRing; ++k) dl->AddLine(rings[a][k], rings[a][(k + 1) % kRing], hot ? colors[a] : dim[a], hot ? 3.0f : 1.5f);
  }
  for (int a = 0; a < 3; ++a) {
    if (!tip_ok[a]) continue;
    const bool hot = (hover_ == static_cast<Handle>(a + 1)) || (dragging_ && drag_handle_ == static_cast<Handle>(a + 1));
    const float w = hot ? 4.0f : 2.5f;
    dl->AddLine(c, tips[a], colors[a], w);
    if (scale_ok[a]) {
      const Handle hs = static_cast<Handle>(static_cast<int>(Handle::ScaleX) + a);
      const bool shot = hover_ == hs || (dragging_ && drag_handle_ == hs);
      const float r = shot ? 6.0f : 4.5f;
      dl->AddLine(tips[a], scale_pts[a], dim[a], 1.5f);
      dl->AddRectFilled(ImVec2(scale_pts[a].x - r, scale_pts[a].y - r), ImVec2(scale_pts[a].x + r, scale_pts[a].y + r), colors[a]);
    }
  }
  const bool free_hot = hover_ == Handle::Free || (dragging_ && drag_handle_ == Handle::Free);
  dl->AddCircleFilled(c, 6.0f, free_hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 230, 235, 200));
  dl->AddCircle(c, 6.0f, IM_COL32(30, 30, 30, 255));
  dl->PopClipRect();

  return dragging_ || hover_ != Handle::None;
}

}  // namespace dino8::app
