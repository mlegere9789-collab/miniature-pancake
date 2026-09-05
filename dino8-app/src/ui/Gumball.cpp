#include "ui/Gumball.h"

#include <cmath>
#include <string>

#include "app/Application.h"
#include "imgui.h"
#include "viewport/Viewport.h"

namespace dino8::app {

namespace {

using kernel::Point3d;
using kernel::Vector3d;

Vector3d AxisDir(int axis) {
  return axis == 0 ? Vector3d(1, 0, 0) : axis == 1 ? Vector3d(0, 1, 0) : Vector3d(0, 0, 1);
}

// Parameter along the line (origin, dir) closest to the ray.
double ClosestParamOnLine(const Ray& ray, Point3d origin, Vector3d dir) {
  const Vector3d w0 = origin - ray.origin;
  const double a = ON_DotProduct(ray.direction, ray.direction), b = ON_DotProduct(ray.direction, dir), c = ON_DotProduct(dir, dir);
  const double d = ON_DotProduct(ray.direction, w0), e = ON_DotProduct(dir, w0);
  const double denom = a * c - b * b;
  if (std::fabs(denom) < 1e-12) return 0.0;
  return (a * e - b * d) / denom * -1.0 + 0.0;  // t on the line
}

bool RayPlane(const Ray& ray, Point3d p0, Vector3d n, Point3d& out) {
  const double denom = ON_DotProduct(n, ray.direction);
  if (std::fabs(denom) < 1e-12) return false;
  const double t = ON_DotProduct(n, p0 - ray.origin) / denom;
  out = ray.origin + ray.direction * t;
  return true;
}

double DistToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float len2 = dx * dx + dy * dy;
  float t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
  t = std::fmax(0.0f, std::fmin(1.0f, t));
  const float px = a.x + dx * t - p.x, py = a.y + dy * t - p.y;
  return std::sqrt(px * px + py * py);
}

}  // namespace

bool Gumball::Update(Application& app, Viewport& vp, bool viewport_hovered) {
  Document& doc = app.Doc();
  ImGuiIO& io = ImGui::GetIO();
  const int vp_index = [&]() { int i = 0; for (auto& v : app.Viewports()) { if (v.get() == &vp) return i; ++i; } return -1; }();

  if (!dragging_) {
    std::vector<ObjectId> sel = doc.SelectedIds();
    kernel::BoundingBox bb;
    if (sel.empty() || !doc.BoundingBoxOf(sel, bb)) { hover_ = Handle::None; return false; }
    center_ = Point3d((bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2, (bb.min.z + bb.max.z) / 2);
    axis_len_ = 70.0 * vp.GetCamera().PixelSize(vp.Height());
  } else if (vp_index != drag_viewport_) {
    return false;
  }

  // Screen positions.
  double cx, cy;
  if (!vp.WorldToPixel(center_, cx, cy)) return false;
  const ImVec2 origin(static_cast<float>(vp.ScreenX()), static_cast<float>(vp.ScreenY()));
  const ImVec2 c(origin.x + static_cast<float>(cx), origin.y + static_cast<float>(cy));
  ImVec2 tips[3];
  bool tip_ok[3];
  for (int a = 0; a < 3; ++a) {
    double tx, ty;
    tip_ok[a] = vp.WorldToPixel(center_ + AxisDir(a) * axis_len_, tx, ty);
    tips[a] = ImVec2(origin.x + static_cast<float>(tx), origin.y + static_cast<float>(ty));
  }

  // Rotation rings and scale handles in screen space.
  constexpr int kRing = 40;
  ImVec2 rings[3][kRing];
  bool ring_ok[3] = {false, false, false};
  const double ring_r = axis_len_ * 0.85;
  for (int a = 0; a < 3; ++a) {
    const Vector3d n = AxisDir(a), u = AxisDir((a + 1) % 3), v = AxisDir((a + 2) % 3);
    (void)n;
    bool ok = true;
    for (int k = 0; k < kRing; ++k) {
      const double ang = 2 * ON_PI * k / kRing;
      double px, py;
      ok = ok && vp.WorldToPixel(center_ + u * (ring_r * std::cos(ang)) + v * (ring_r * std::sin(ang)), px, py);
      rings[a][k] = ImVec2(origin.x + static_cast<float>(px), origin.y + static_cast<float>(py));
    }
    ring_ok[a] = ok;
  }
  ImVec2 scale_pts[3];
  bool scale_ok[3];
  for (int a = 0; a < 3; ++a) {
    double px, py;
    scale_ok[a] = vp.WorldToPixel(center_ + AxisDir(a) * (axis_len_ * 1.3), px, py);
    scale_pts[a] = ImVec2(origin.x + static_cast<float>(px), origin.y + static_cast<float>(py));
  }
  auto plane_point = [&](const Ray& ray, int axis, Point3d& out) { return RayPlane(ray, center_, AxisDir(axis), out); };
  auto angle_on_plane = [&](const Point3d& p, int axis) {
    const Vector3d u = AxisDir((axis + 1) % 3), v = AxisDir((axis + 2) % 3);
    const Vector3d d = p - center_;
    return std::atan2(ON_DotProduct(d, v), ON_DotProduct(d, u));
  };

  // Hit test.
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
      originals_.clear();
      for (const SceneObject& o : doc.Objects()) if (o.selected) originals_.push_back({o.id, o});
      doc.BeginChange("Gumball");
      const Ray ray = vp.PixelRay(m.x - origin.x, m.y - origin.y);
      const int h = static_cast<int>(drag_handle_);
      if (drag_handle_ == Handle::Free) {
        RayPlane(ray, center_, vp.GetCamera().Forward(), start_free_);
      } else if (h >= static_cast<int>(Handle::RotX) && h <= static_cast<int>(Handle::RotZ)) {
        Point3d p;
        const int axis = h - static_cast<int>(Handle::RotX);
        start_angle_ = plane_point(ray, axis, p) ? angle_on_plane(p, axis) : 0.0;
      } else if (h >= static_cast<int>(Handle::ScaleX)) {
        const int axis = h - static_cast<int>(Handle::ScaleX);
        start_param_ = ClosestParamOnLine(ray, center_, AxisDir(axis));
      } else {
        start_param_ = ClosestParamOnLine(ray, center_, AxisDir(h - 1));
      }
      last_xform_ = ON_Xform::IdentityTransformation;
    }
  }

  // Drag.
  if (dragging_) {
    const Ray ray = vp.PixelRay(m.x - origin.x, m.y - origin.y);
    ON_Xform xf = ON_Xform::IdentityTransformation;
    const int h = static_cast<int>(drag_handle_);
    std::string what;
    if (drag_handle_ == Handle::Free) {
      Point3d now;
      Vector3d delta(0, 0, 0);
      if (RayPlane(ray, center_, vp.GetCamera().Forward(), now)) delta = now - start_free_;
      if (app.Snaps().grid_snap) { const double g = doc.Settings().grid_spacing; delta = Vector3d(std::round(delta.x / g) * g, std::round(delta.y / g) * g, std::round(delta.z / g) * g); }
      xf = ON_Xform::TranslationTransformation(delta);
      what = "moved " + FormatPoint(Point3d(delta.x, delta.y, delta.z));
    } else if (h >= static_cast<int>(Handle::RotX) && h <= static_cast<int>(Handle::RotZ)) {
      const int axis = h - static_cast<int>(Handle::RotX);
      Point3d p;
      double angle = 0;
      if (plane_point(ray, axis, p)) angle = angle_on_plane(p, axis) - start_angle_;
      if (io.KeyShift) angle = std::round(angle / (ON_PI / 12)) * (ON_PI / 12);  // 15 degree steps
      xf.Rotation(angle, AxisDir(axis), center_);
      what = "rotated " + FormatNumber(angle * 180.0 / ON_PI) + " degrees about " + "XYZ"[axis];
    } else if (h >= static_cast<int>(Handle::ScaleX)) {
      const int axis = h - static_cast<int>(Handle::ScaleX);
      const double now = ClosestParamOnLine(ray, center_, AxisDir(axis));
      double f = (std::fabs(start_param_) > 1e-9) ? now / start_param_ : 1.0;
      if (!std::isfinite(f) || std::fabs(f) < 1e-3) f = 1e-3;
      ON_Plane pl(center_, ON_xaxis, ON_yaxis);
      if (io.KeyShift) xf = ON_Xform::ScaleTransformation(pl, f, f, f);
      else xf = ON_Xform::ScaleTransformation(pl, axis == 0 ? f : 1.0, axis == 1 ? f : 1.0, axis == 2 ? f : 1.0);
      what = std::string("scaled ") + (io.KeyShift ? "uniformly " : "") + "by " + FormatNumber(f);
    } else {
      const Vector3d dir = AxisDir(h - 1);
      Vector3d delta = dir * (ClosestParamOnLine(ray, center_, dir) - start_param_);
      if (app.Snaps().grid_snap) { const double g = doc.Settings().grid_spacing; delta = Vector3d(std::round(delta.x / g) * g, std::round(delta.y / g) * g, std::round(delta.z / g) * g); }
      xf = ON_Xform::TranslationTransformation(delta);
      what = "moved " + FormatPoint(Point3d(delta.x, delta.y, delta.z));
    }
    for (auto& [id, original] : originals_) {
      if (SceneObject* o = doc.Find(id)) {
        SceneObject moved = original;
        moved.Transform(xf);
        moved.selected = true;
        *o = moved;
      }
    }
    if (!ImGui::IsMouseDown(0)) {
      dragging_ = false;
      originals_.clear();
      app.Engine().Print("Gumball: " + what);
      doc.Touch();
    }
  }

  // Draw.
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->PushClipRect(origin, ImVec2(origin.x + static_cast<float>(vp.Width()), origin.y + static_cast<float>(vp.Height())), true);
  const ImU32 colors[3] = {IM_COL32(235, 80, 80, 255), IM_COL32(80, 210, 80, 255), IM_COL32(90, 140, 255, 255)};
  const ImU32 dim[3] = {IM_COL32(235, 80, 80, 120), IM_COL32(80, 210, 80, 120), IM_COL32(90, 140, 255, 120)};
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
    const float dx = tips[a].x - c.x, dy = tips[a].y - c.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1.0f) {
      const float ux = dx / len, uy = dy / len;
      const float s = hot ? 11.0f : 9.0f;
      const ImVec2 p1(tips[a].x - ux * s + uy * s * 0.5f, tips[a].y - uy * s - ux * s * 0.5f);
      const ImVec2 p2(tips[a].x - ux * s - uy * s * 0.5f, tips[a].y - uy * s + ux * s * 0.5f);
      dl->AddTriangleFilled(tips[a], p1, p2, colors[a]);
    }
    if (scale_ok[a]) {
      const Handle hs = static_cast<Handle>(static_cast<int>(Handle::ScaleX) + a);
      const bool shot = hover_ == hs || (dragging_ && drag_handle_ == hs);
      const float r = shot ? 6.0f : 4.5f;
      dl->AddLine(tips[a], scale_pts[a], dim[a], 1.5f);
      dl->AddRectFilled(ImVec2(scale_pts[a].x - r, scale_pts[a].y - r), ImVec2(scale_pts[a].x + r, scale_pts[a].y + r), colors[a]);
    }
  }
  const bool free_hot = hover_ == Handle::Free || (dragging_ && drag_handle_ == Handle::Free);
  dl->AddRectFilled(ImVec2(c.x - 6, c.y - 6), ImVec2(c.x + 6, c.y + 6), free_hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 230, 235, 200));
  dl->AddRect(ImVec2(c.x - 6, c.y - 6), ImVec2(c.x + 6, c.y + 6), IM_COL32(30, 30, 30, 255));
  dl->PopClipRect();

  return dragging_ || hover_ != Handle::None;
}

}  // namespace dino8::app
