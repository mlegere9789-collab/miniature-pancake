#include "viewport/Viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

const char* DisplayModeName(DisplayMode mode) {
  switch (mode) {
    case DisplayMode::Wireframe: return "Wireframe";
    case DisplayMode::Shaded: return "Shaded";
    case DisplayMode::Rendered: return "Rendered";
    case DisplayMode::Ghosted: return "Ghosted";
    case DisplayMode::XRay: return "X-Ray";
    case DisplayMode::Technical: return "Technical";
    case DisplayMode::Artistic: return "Artistic";
    case DisplayMode::Pen: return "Pen";
    case DisplayMode::Arctic: return "Arctic";
    case DisplayMode::Monochrome: return "Monochrome";
  }
  return "Wireframe";
}

std::vector<DisplayMode> AllDisplayModes() {
  return {DisplayMode::Wireframe, DisplayMode::Shaded, DisplayMode::Rendered, DisplayMode::Ghosted,
          DisplayMode::XRay, DisplayMode::Technical, DisplayMode::Artistic, DisplayMode::Pen,
          DisplayMode::Arctic, DisplayMode::Monochrome};
}

Viewport::Viewport(const std::string& name, const std::string& standard_view) : name_(name) {
  SetStandardView(standard_view);
  camera_.State().target = Point3d(0, 0, 0);
  if (standard_view == "Perspective") {
    camera_.State().eye = Point3d(60, -60, 45);
    camera_.SetPerspective();
    mode_ = DisplayMode::Shaded;
  } else {
    camera_.State().ortho_height = 80;
  }
}

void Viewport::SetStandardView(const std::string& view) {
  standard_view_ = view;
  ConstructionPlane cp;
  if (view == "Top") { camera_.SetTop(); }
  else if (view == "Bottom") { camera_.SetBottom(); cp.y_axis = Vector3d(0, -1, 0); }
  else if (view == "Front") { camera_.SetFront(); cp.x_axis = Vector3d(1, 0, 0); cp.y_axis = Vector3d(0, 0, 1); }
  else if (view == "Back") { camera_.SetBack(); cp.x_axis = Vector3d(-1, 0, 0); cp.y_axis = Vector3d(0, 0, 1); }
  else if (view == "Right") { camera_.SetRight(); cp.x_axis = Vector3d(0, 1, 0); cp.y_axis = Vector3d(0, 0, 1); }
  else if (view == "Left") { camera_.SetLeft(); cp.x_axis = Vector3d(0, -1, 0); cp.y_axis = Vector3d(0, 0, 1); }
  else if (view == "Isometric") { camera_.SetIsometric(); }
  else { camera_.SetPerspective(); }
  cplane_ = cp;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

namespace {

struct ModeStyle {
  Color bg_top, bg_bottom;
  bool fill = true;
  bool lit = true;
  float fill_alpha = 1.0f;
  bool edges = true;
  bool isocurves = true;
  bool force_white = false;   // Arctic / Pen fills
  bool monochrome = false;
  bool depth_lines = true;
  Color edge_color = Color::FromBytes(30, 30, 30);
};

ModeStyle StyleFor(DisplayMode mode) {
  ModeStyle s;
  s.bg_top = Color::FromBytes(46, 50, 58);
  s.bg_bottom = Color::FromBytes(24, 26, 31);
  switch (mode) {
    case DisplayMode::Wireframe: s.fill = false; s.edges = true; break;
    case DisplayMode::Shaded: break;
    case DisplayMode::Rendered: s.isocurves = false; s.edges = false; break;
    case DisplayMode::Ghosted: s.fill_alpha = 0.35f; break;
    case DisplayMode::XRay: s.fill_alpha = 0.18f; s.depth_lines = false; break;
    case DisplayMode::Technical: s.monochrome = true; s.edge_color = Color::FromBytes(20, 20, 20); s.bg_top = s.bg_bottom = Color::FromBytes(235, 235, 235); break;
    case DisplayMode::Artistic: s.monochrome = true; s.bg_top = Color::FromBytes(242, 236, 220); s.bg_bottom = Color::FromBytes(222, 214, 195); s.edge_color = Color::FromBytes(60, 50, 40); break;
    case DisplayMode::Pen: s.force_white = true; s.lit = false; s.isocurves = false; s.bg_top = s.bg_bottom = Color::FromBytes(255, 255, 255); s.edge_color = Color::FromBytes(0, 0, 0); break;
    case DisplayMode::Arctic: s.force_white = true; s.isocurves = false; s.bg_top = s.bg_bottom = Color::FromBytes(250, 250, 250); s.edge_color = Color::FromBytes(150, 150, 150); break;
    case DisplayMode::Monochrome: s.monochrome = true; s.isocurves = false; break;
  }
  return s;
}

Color Mix(Color a, Color b, float t) {
  return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

const Color kSelectionColor = Color::FromBytes(255, 210, 0);
const Color kLockedColor = Color::FromBytes(120, 120, 120);
const Color kControlPointColor = Color::FromBytes(255, 255, 255);
const Color kControlPolygonColor = Color::FromBytes(160, 160, 160);

}  // namespace

void Viewport::Render(GlRenderer& renderer, const FrameContext& ctx) {
  if (!target_.Resize(std::max(width_, 1), std::max(height_, 1))) return;
  target_.Bind();
  const ModeStyle style = StyleFor(mode_);
  renderer.SetMatrices(camera_.ViewMatrix(), camera_.ProjectionMatrix(Aspect()));
  renderer.ClearGradient(style.bg_top, style.bg_bottom);
  renderer.EnableDepthTest(true);
  renderer.EnableBlend(true);
  if (ctx.doc) {
    DrawGrid(renderer, ctx.doc->Settings());
    DrawObjects(renderer, ctx);
  }
  // Command preview geometry (rubber bands, dynamic previews).
  renderer.EnableDepthTest(false);
  if (ctx.preview_lines) renderer.DrawLines(*ctx.preview_lines, Color::FromBytes(255, 255, 255));
  if (ctx.preview_points) renderer.DrawPoints(*ctx.preview_points, Color::FromBytes(255, 255, 255), 7.0f);
  if (ctx.cursor_marker) {
    const Point3d& p = *ctx.cursor_marker;
    const std::vector<float> marker = {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
    renderer.DrawPoints(marker, Color::FromBytes(255, 255, 255), 9.0f);
  }
  DrawAxesGizmo(renderer);
  renderer.EnableDepthTest(true);
  RenderTarget::Unbind();
}

void Viewport::DrawGrid(GlRenderer& renderer, const DocumentSettings& s) {
  if (!s.show_grid && !s.show_axes) return;
  const int n = std::max(1, s.grid_extents);
  const double sp = std::max(s.grid_spacing, 1e-6);
  const double ext = n * sp;
  std::vector<float> minor, major;
  auto push = [](std::vector<float>& v, Point3d a, Point3d b) {
    v.push_back(static_cast<float>(a.x)); v.push_back(static_cast<float>(a.y)); v.push_back(static_cast<float>(a.z));
    v.push_back(static_cast<float>(b.x)); v.push_back(static_cast<float>(b.y)); v.push_back(static_cast<float>(b.z));
  };
  if (s.show_grid) {
    for (int i = -n; i <= n; ++i) {
      if (i == 0) continue;
      const double t = i * sp;
      std::vector<float>& dst = (s.grid_major_every > 0 && i % s.grid_major_every == 0) ? major : minor;
      push(dst, cplane_.ToWorld(t, -ext), cplane_.ToWorld(t, ext));
      push(dst, cplane_.ToWorld(-ext, t), cplane_.ToWorld(ext, t));
    }
  }
  const bool light = mode_ == DisplayMode::Pen || mode_ == DisplayMode::Arctic ||
                     mode_ == DisplayMode::Technical || mode_ == DisplayMode::Artistic;
  renderer.DrawLines(minor, light ? Color::FromBytes(215, 215, 215) : Color::FromBytes(58, 62, 70));
  renderer.DrawLines(major, light ? Color::FromBytes(190, 190, 190) : Color::FromBytes(78, 83, 92));
  if (s.show_axes) {
    std::vector<float> xa, ya, za;
    push(xa, cplane_.ToWorld(-ext, 0), cplane_.ToWorld(ext, 0));
    push(ya, cplane_.ToWorld(0, -ext), cplane_.ToWorld(0, ext));
    push(za, cplane_.origin, cplane_.ToWorld(0, 0, ext * 0.25));
    renderer.DrawLines(xa, Color::FromBytes(200, 70, 70));
    renderer.DrawLines(ya, Color::FromBytes(70, 180, 70));
    renderer.DrawLines(za, Color::FromBytes(70, 110, 220));
  }
}

void Viewport::DrawAxesGizmo(GlRenderer& renderer) {
  // Small world-axis indicator in the lower-left corner, drawn in a tiny
  // orthographic projection so it never scales with zoom.
  const Mat4 view = camera_.ViewMatrix();
  Mat4 rot = view;
  rot.m[12] = rot.m[13] = rot.m[14] = 0;  // drop translation
  const double aspect = Aspect();
  const double size = 0.08;
  Mat4 proj = Mat4::Ortho(-1 * aspect, 1 * aspect, -1, 1, -10, 10);
  Mat4 shift = Mat4::Identity();
  shift.m[12] = static_cast<float>(-aspect + size * 1.6);
  shift.m[13] = static_cast<float>(-1 + size * 1.6);
  renderer.SetMatrices(rot, shift * proj);
  const float L = static_cast<float>(size);
  renderer.DrawLines({0, 0, 0, L, 0, 0}, Color::FromBytes(230, 70, 70));
  renderer.DrawLines({0, 0, 0, 0, L, 0}, Color::FromBytes(70, 200, 70));
  renderer.DrawLines({0, 0, 0, 0, 0, L}, Color::FromBytes(80, 130, 255));
  renderer.SetMatrices(camera_.ViewMatrix(), camera_.ProjectionMatrix(aspect));
}

void Viewport::DrawObjects(GlRenderer& renderer, const FrameContext& ctx) {
  const Document& doc = *ctx.doc;
  const ModeStyle style = StyleFor(mode_);
  // Pass 1: fills (with polygon offset so edges win the depth test).
  if (style.fill) {
    renderer.EnablePolygonOffset(true);
    for (const SceneObject& o : doc.Objects()) {
      if (!doc.IsObjectVisible(o)) continue;
      o.EnsureDisplay(ctx.curve_tolerance, ctx.surface_tolerance);
      const DisplayCache& d = o.Display();
      if (d.triangles.empty()) continue;
      // Shaded/Ghosted/X-Ray fill with one light material like Rhino's
      // default, unless the object carries a material; Rendered uses the
      // object/layer colour.
      Color c = Color::FromBytes(205, 207, 212);
      if (mode_ == DisplayMode::Rendered || !o.material_name.empty() || !o.color_by_layer) c = doc.EffectiveColor(o);
      if (style.force_white) c = Color::FromBytes(245, 245, 245);
      else if (style.monochrome) c = Color::FromBytes(200, 200, 205);
      if (doc.IsObjectLocked(o)) c = Mix(c, kLockedColor, 0.6f);
      if (o.selected) c = Mix(c, kSelectionColor, 0.55f);
      c.a = style.fill_alpha;
      renderer.DrawTriangles(d.triangles, c, style.lit);
    }
    renderer.EnablePolygonOffset(false);
  }
  // Pass 2: curves, edges, isocurves, points, control points.
  if (!style.depth_lines) renderer.EnableDepthTest(false);
  for (const SceneObject& o : doc.Objects()) {
    if (!doc.IsObjectVisible(o)) continue;
    o.EnsureDisplay(ctx.curve_tolerance, ctx.surface_tolerance);
    const DisplayCache& d = o.Display();
    const bool is_curve_like = o.kind == ObjectKind::Curve;
    Color line_color = doc.EffectiveColor(o);
    // Rhino's default layer colour is black, which vanishes on a dark
    // background: lift near-black wire colours so curves stay readable.
    {
      const float bg_lum = 0.299f * style.bg_bottom.r + 0.587f * style.bg_bottom.g + 0.114f * style.bg_bottom.b;
      const float lum = 0.299f * line_color.r + 0.587f * line_color.g + 0.114f * line_color.b;
      if (bg_lum < 0.45f && lum < 0.25f && !style.fill) line_color = Color::FromBytes(222, 225, 230);
      else if (bg_lum < 0.45f && lum < 0.25f && is_curve_like) line_color = Color::FromBytes(222, 225, 230);
    }
    if (!is_curve_like) {
      if (style.fill) line_color = style.monochrome || style.force_white ? style.edge_color : Mix(line_color, style.edge_color, 0.55f);
      if (!style.edges && !style.isocurves && !o.selected) {
        // Rendered mode: no wires on surfaces/meshes at all.
        if (o.kind != ObjectKind::Point) continue;
      }
    }
    if (doc.IsObjectLocked(o)) line_color = Mix(line_color, kLockedColor, 0.7f);
    if (o.selected) line_color = kSelectionColor;
    if (!d.lines.empty() && (is_curve_like || style.edges || style.isocurves || o.selected)) {
      renderer.DrawLines(d.lines, line_color);
    }
    if (!d.points.empty()) {
      renderer.DrawPoints(d.points, o.selected ? kSelectionColor : line_color, 6.0f);
    }
    if (o.show_control_points || (ctx.show_control_points_for_selected && o.selected)) {
      renderer.EnableDepthTest(false);
      renderer.DrawLines(d.control_polygon, kControlPolygonColor);
      renderer.DrawPoints(d.control_points, kControlPointColor, 5.0f);
      renderer.EnableDepthTest(style.depth_lines);
    }
  }
  renderer.EnableDepthTest(true);
}

// ---------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------

bool Viewport::WorldToPixel(Point3d p, double& px, double& py) const {
  double nx, ny, depth;
  if (!camera_.Project(p, Aspect(), nx, ny, depth)) return false;
  px = (nx + 1.0) * 0.5 * width_;
  py = (1.0 - ny) * 0.5 * height_;
  return true;
}

Ray Viewport::PixelRay(double px, double py) const {
  const double nx = (px / std::max(width_, 1)) * 2.0 - 1.0;
  const double ny = 1.0 - (py / std::max(height_, 1)) * 2.0;
  return camera_.ScreenRay(nx, ny, Aspect());
}

namespace {

double PointSegmentDistance2D(double px, double py, double ax, double ay, double bx, double by, double& t_out) {
  const double dx = bx - ax, dy = by - ay;
  const double len2 = dx * dx + dy * dy;
  double t = len2 > 1e-12 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
  t = std::clamp(t, 0.0, 1.0);
  t_out = t;
  const double cx = ax + dx * t, cy = ay + dy * t;
  return std::hypot(px - cx, py - cy);
}

bool RayTriangle(const Ray& ray, Point3d a, Point3d b, Point3d c, double& t_out) {
  const Vector3d e1 = b - a, e2 = c - a;
  const Vector3d p = ON_CrossProduct(ray.direction, e2);
  const double det = ON_DotProduct(e1, p);
  if (std::abs(det) < 1e-12) return false;
  const double inv = 1.0 / det;
  const Vector3d s = ray.origin - a;
  const double u = ON_DotProduct(s, p) * inv;
  if (u < 0 || u > 1) return false;
  const Vector3d q = ON_CrossProduct(s, e1);
  const double v = ON_DotProduct(ray.direction, q) * inv;
  if (v < 0 || u + v > 1) return false;
  const double t = ON_DotProduct(e2, q) * inv;
  if (t < 0) return false;
  t_out = t;
  return true;
}

}  // namespace

ObjectId Viewport::PickObject(const Document& doc, double px, double py, double pixel_radius) const {
  ObjectId best_wire = kNoObject;
  double best_wire_dist = pixel_radius;
  ObjectId best_face = kNoObject;
  double best_face_t = 1e300;
  const Ray ray = PixelRay(px, py);
  for (const SceneObject& o : doc.Objects()) {
    if (!doc.IsObjectVisible(o) || doc.IsObjectLocked(o)) continue;
    o.EnsureDisplay(0.02, 0.05);
    const DisplayCache& d = o.Display();
    // Wires and points: screen-space distance.
    for (size_t i = 0; i + 5 < d.lines.size(); i += 6) {
      double ax, ay, bx, by;
      if (!WorldToPixel(Point3d(d.lines[i], d.lines[i + 1], d.lines[i + 2]), ax, ay)) continue;
      if (!WorldToPixel(Point3d(d.lines[i + 3], d.lines[i + 4], d.lines[i + 5]), bx, by)) continue;
      double t;
      const double dist = PointSegmentDistance2D(px, py, ax, ay, bx, by, t);
      if (dist < best_wire_dist) {
        best_wire_dist = dist;
        best_wire = o.id;
      }
    }
    for (size_t i = 0; i + 2 < d.points.size(); i += 3) {
      double ax, ay;
      if (!WorldToPixel(Point3d(d.points[i], d.points[i + 1], d.points[i + 2]), ax, ay)) continue;
      const double dist = std::hypot(px - ax, py - ay);
      if (dist < best_wire_dist) {
        best_wire_dist = dist;
        best_wire = o.id;
      }
    }
    // Faces: ray intersection (only when the mode draws fills).
    if (StyleFor(mode_).fill) {
      for (size_t i = 0; i + 17 < d.triangles.size(); i += 18) {
        double t;
        if (RayTriangle(ray, Point3d(d.triangles[i], d.triangles[i + 1], d.triangles[i + 2]),
                        Point3d(d.triangles[i + 6], d.triangles[i + 7], d.triangles[i + 8]),
                        Point3d(d.triangles[i + 12], d.triangles[i + 13], d.triangles[i + 14]), t)) {
          if (t < best_face_t) {
            best_face_t = t;
            best_face = o.id;
          }
        }
      }
    }
  }
  // Curves/points near the cursor beat a face behind them (Rhino behavior).
  if (best_wire != kNoObject && best_wire_dist <= pixel_radius) return best_wire;
  return best_face;
}

std::vector<ObjectId> Viewport::ObjectsInWindow(const Document& doc, double x0, double y0, double x1,
                                                double y1, bool crossing) const {
  const double left = std::min(x0, x1), right = std::max(x0, x1);
  const double top = std::min(y0, y1), bottom = std::max(y0, y1);
  std::vector<ObjectId> result;
  for (const SceneObject& o : doc.Objects()) {
    if (!doc.IsObjectVisible(o) || doc.IsObjectLocked(o)) continue;
    o.EnsureDisplay(0.02, 0.05);
    const DisplayCache& d = o.Display();
    bool any_inside = false, all_inside = true, any_vertex = false;
    auto test = [&](float x, float y, float z) {
      double px, py;
      any_vertex = true;
      if (!WorldToPixel(Point3d(x, y, z), px, py)) {
        all_inside = false;
        return;
      }
      const bool inside = px >= left && px <= right && py >= top && py <= bottom;
      any_inside = any_inside || inside;
      all_inside = all_inside && inside;
    };
    for (size_t i = 0; i + 2 < d.lines.size(); i += 3) test(d.lines[i], d.lines[i + 1], d.lines[i + 2]);
    for (size_t i = 0; i + 2 < d.points.size(); i += 3) test(d.points[i], d.points[i + 1], d.points[i + 2]);
    if (d.lines.empty() && d.points.empty()) {
      for (size_t i = 0; i + 5 < d.triangles.size(); i += 6) test(d.triangles[i], d.triangles[i + 1], d.triangles[i + 2]);
    }
    if (!any_vertex) continue;
    if (crossing ? any_inside : all_inside) result.push_back(o.id);
  }
  return result;
}

PickResult Viewport::PickPoint(const Document& doc, const SnapSettings& snaps, double px, double py,
                               std::optional<Point3d> ortho_base, double grid_spacing,
                               bool want_point) const {
  PickResult result;
  const Ray ray = PixelRay(px, py);
  // Free point: ray/CPlane intersection (fallback: a plane through the
  // target perpendicular to the view when the ray is parallel to CPlane).
  const Vector3d n = cplane_.Normal();
  const double denom = ON_DotProduct(ray.direction, n);
  Point3d free_point;
  if (std::abs(denom) > 1e-9) {
    const double t = ON_DotProduct(cplane_.origin - ray.origin, n) / denom;
    free_point = ray.origin + ray.direction * t;
  } else {
    const Vector3d f = camera_.Forward();
    const double t = ON_DotProduct(camera_.State().target - ray.origin, f) / std::max(ON_DotProduct(ray.direction, f), 1e-9);
    free_point = ray.origin + ray.direction * t;
  }
  result.point = free_point;

  if (!want_point) return result;

  // Object snaps: nearest candidate within a pixel radius.
  const double snap_radius = 10.0;
  double best = snap_radius;
  auto consider = [&](Point3d p, const char* label) {
    double sx, sy;
    if (!WorldToPixel(p, sx, sy)) return;
    const double dist = std::hypot(sx - px, sy - py);
    if (dist < best) {
      best = dist;
      result.point = p;
      result.snap_label = label;
      result.snapped = true;
    }
  };
  if (!snaps.disable_all) {
    for (const SceneObject& o : doc.Objects()) {
      if (!doc.IsObjectVisible(o)) continue;
      // Quick reject: bounding box far from the cursor.
      const kernel::BoundingBox bb = o.BoundingBox();
      double bx0, by0, bx1, by1;
      const bool p0 = WorldToPixel(bb.min, bx0, by0);
      const bool p1 = WorldToPixel(bb.max, bx1, by1);
      if (p0 && p1) {
        const double margin = 40.0;
        if (px < std::min(bx0, bx1) - margin || px > std::max(bx0, bx1) + margin ||
            py < std::min(by0, by1) - margin || py > std::max(by0, by1) + margin) {
          continue;
        }
      }
      switch (o.kind) {
        case ObjectKind::Point:
          if (snaps.point) consider(o.point, "Point");
          break;
        case ObjectKind::Curve: {
          const kernel::NurbsCurve& c = *o.curve;
          const kernel::Interval dom = c.Domain();
          if (snaps.end) {
            consider(c.PointAt(dom.min), "End");
            consider(c.PointAt(dom.max), "End");
            if (c.Degree() == 1) {
              for (int i = 1; i + 1 < c.ControlPointCount(); ++i) consider(c.ControlPointAt(i), "End");
            }
          }
          if (snaps.mid) {
            if (c.Degree() == 1) {
              for (int i = 0; i + 1 < c.ControlPointCount(); ++i) {
                const Point3d a = c.ControlPointAt(i), b = c.ControlPointAt(i + 1);
                consider(Point3d((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2), "Mid");
              }
            } else {
              consider(c.PointAt(c.ParameterAtArcLength(c.Length() / 2.0)), "Mid");
            }
          }
          if (snaps.cen) {
            ON_Arc arc;
            if (c.raw().IsArc(nullptr, &arc)) consider(arc.Center(), "Cen");
          }
          if (snaps.quad) {
            ON_Arc arc;
            if (c.raw().IsArc(nullptr, &arc) && arc.IsCircle()) {
              const ON_Plane pl = arc.Plane();
              const double r = arc.Radius();
              consider(arc.Center() + pl.xaxis * r, "Quad");
              consider(arc.Center() - pl.xaxis * r, "Quad");
              consider(arc.Center() + pl.yaxis * r, "Quad");
              consider(arc.Center() - pl.yaxis * r, "Quad");
            }
          }
          if (snaps.near_) {
            const DisplayCache& d = o.Display();
            for (size_t i = 0; i + 5 < d.lines.size(); i += 6) {
              double ax, ay, bx, by, t;
              const Point3d a(d.lines[i], d.lines[i + 1], d.lines[i + 2]);
              const Point3d b(d.lines[i + 3], d.lines[i + 4], d.lines[i + 5]);
              if (!WorldToPixel(a, ax, ay) || !WorldToPixel(b, bx, by)) continue;
              const double dist = PointSegmentDistance2D(px, py, ax, ay, bx, by, t);
              if (dist < best) {
                best = dist;
                result.point = a + (b - a) * t;
                result.snap_label = "Near";
                result.snapped = true;
              }
            }
          }
          break;
        }
        case ObjectKind::Surface: {
          if (snaps.end) {
            const kernel::NurbsSurface& s = *o.surface;
            const kernel::Interval du = s.Domain(0), dv = s.Domain(1);
            consider(s.PointAt(du.min, dv.min), "End");
            consider(s.PointAt(du.max, dv.min), "End");
            consider(s.PointAt(du.min, dv.max), "End");
            consider(s.PointAt(du.max, dv.max), "End");
          }
          break;
        }
        case ObjectKind::Brep: {
          if (snaps.end) {
            const ON_Brep& b = o.brep->raw();
            for (int i = 0; i < b.m_V.Count(); ++i) consider(b.m_V[i].Point(), "End");
          }
          break;
        }
        case ObjectKind::Mesh:
        case ObjectKind::SubD: {
          if (snaps.vertex || snaps.end) {
            const ON_Mesh* m = o.kind == ObjectKind::Mesh ? &o.mesh->raw() : nullptr;
            if (m) {
              for (int i = 0; i < m->m_V.Count(); ++i) {
                const ON_3fPoint& v = m->m_V[i];
                consider(Point3d(v.x, v.y, v.z), snaps.vertex ? "Vertex" : "End");
              }
            }
          }
          break;
        }
      }
    }
  }
  if (result.snapped) return result;

  // Ortho / planar constraints relative to the previous point.
  Point3d p = free_point;
  if (ortho_base) {
    const Point3d base = *ortho_base;
    if (snaps.planar) {
      // Keep the CPlane elevation of the base point.
      const double w = ON_DotProduct(base - cplane_.origin, n);
      const double pw = ON_DotProduct(p - cplane_.origin, n);
      p = p + n * (w - pw);
    }
    if (snaps.ortho) {
      const Vector3d rel = p - base;
      const double u = ON_DotProduct(rel, cplane_.x_axis);
      const double v = ON_DotProduct(rel, cplane_.y_axis);
      const double w = ON_DotProduct(rel, n);
      if (std::abs(u) >= std::abs(v)) p = base + cplane_.x_axis * u + n * w;
      else p = base + cplane_.y_axis * v + n * w;
      result.snap_label = "Ortho";
    }
  }
  if (snaps.grid_snap && grid_spacing > 0) {
    const Vector3d rel = p - cplane_.origin;
    const double u = std::round(ON_DotProduct(rel, cplane_.x_axis) / grid_spacing) * grid_spacing;
    const double v = std::round(ON_DotProduct(rel, cplane_.y_axis) / grid_spacing) * grid_spacing;
    const double w = ON_DotProduct(rel, n);
    p = cplane_.ToWorld(u, v, w);
    if (result.snap_label.empty()) result.snap_label = "Grid";
  }
  result.point = p;
  return result;
}

void Viewport::ZoomTo(const kernel::BoundingBox& box) { camera_.ZoomExtents(box, Aspect()); }

void Viewport::ZoomExtents(const Document& doc, bool selected_only) {
  kernel::BoundingBox box;
  bool has = selected_only ? doc.BoundingBoxOf(doc.SelectedIds(), box) : doc.VisibleBoundingBox(box);
  if (!has) {
    box.min = Point3d(-20, -20, -20);
    box.max = Point3d(20, 20, 20);
  }
  // Pad degenerate boxes (single point / flat curve) so they don't zoom to nothing.
  const Vector3d size = box.max - box.min;
  const double pad = std::max(size.Length() * 0.05, 1.0);
  if (size.x < 1e-6) { box.min.x -= pad; box.max.x += pad; }
  if (size.y < 1e-6) { box.min.y -= pad; box.max.y += pad; }
  if (size.z < 1e-6) { box.min.z -= pad; box.max.z += pad; }
  ZoomTo(box);
}

// ---------------------------------------------------------------------------
// ImGui window + input
// ---------------------------------------------------------------------------

ViewportEvents Viewport::DrawUI(const Document& doc, const SnapSettings& snaps, bool want_point,
                                bool want_objects, std::optional<Point3d> ortho_base,
                                double grid_spacing, bool& request_focus_command_line) {
  ViewportEvents ev;
  if (!visible_) return ev;
  ImGuiIO& io = ImGui::GetIO();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  const std::string title = name_ + "###vp_" + name_;
  bool open = true;
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                 ImGuiWindowFlags_NoCollapse;
  if (!ImGui::Begin(title.c_str(), &open, flags)) {
    ImGui::End();
    ImGui::PopStyleVar();
    return ev;
  }
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  width_ = std::max(1, static_cast<int>(avail.x));
  height_ = std::max(1, static_cast<int>(avail.y));
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  img_x_ = cursor.x;
  img_y_ = cursor.y;
  if (target_.Texture()) {
    ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(target_.Texture())), ImVec2(static_cast<float>(width_), static_cast<float>(height_)), ImVec2(0, 1), ImVec2(1, 0));
  } else {
    ImGui::Dummy(ImVec2(static_cast<float>(width_), static_cast<float>(height_)));
  }
  const bool hovered = ImGui::IsItemHovered();
  ev.hovered = hovered;
  const double mx = io.MousePos.x - img_x_;
  const double my = io.MousePos.y - img_y_;
  ev.shift = io.KeyShift;
  ev.ctrl = io.KeyCtrl;

  // Viewport title overlay with a click-to-open menu (views / display modes).
  ImDrawList* dl = ImGui::GetWindowDrawList();
  {
    const ImVec2 p0(cursor.x + 6, cursor.y + 4);
    const char* label = name_.c_str();
    const ImVec2 sz = ImGui::CalcTextSize(label);
    dl->AddRectFilled(ImVec2(p0.x - 4, p0.y - 2), ImVec2(p0.x + sz.x + 8, p0.y + sz.y + 2),
                      active_ ? IM_COL32(70, 130, 220, 200) : IM_COL32(30, 32, 38, 170), 4.0f);
    dl->AddText(p0, IM_COL32(255, 255, 255, 255), label);
    ImGui::SetCursorScreenPos(ImVec2(p0.x - 4, p0.y - 2));
    if (ImGui::InvisibleButton(("##title_" + name_).c_str(), ImVec2(sz.x + 12, sz.y + 4))) {
      ImGui::OpenPopup(("##vpmenu_" + name_).c_str());
    }
    if (ImGui::BeginPopup(("##vpmenu_" + name_).c_str())) {
      if (ImGui::BeginMenu("Set View")) {
        for (const char* v : {"Top", "Bottom", "Front", "Back", "Right", "Left", "Perspective", "Isometric"}) {
          if (ImGui::MenuItem(v)) SetStandardView(v);
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Display Mode")) {
        for (DisplayMode m : AllDisplayModes()) {
          if (ImGui::MenuItem(DisplayModeName(m), nullptr, mode_ == m)) mode_ = m;
        }
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem(maximized_ ? "Restore Viewports" : "Maximize Viewport")) maximized_ = !maximized_;
      if (ImGui::MenuItem("Zoom Extents")) ZoomExtents(doc, false);
      if (ImGui::MenuItem("Zoom Selected")) ZoomExtents(doc, true);
      ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(cursor);
  }
  // Display mode label in the corner.
  {
    const char* mode = DisplayModeName(mode_);
    const ImVec2 sz = ImGui::CalcTextSize(mode);
    dl->AddText(ImVec2(cursor.x + width_ - sz.x - 8, cursor.y + 4), IM_COL32(200, 200, 200, 160), mode);
  }

  // Hover feedback.
  if (hovered) {
    if (want_point) {
      ev.hover_pick = PickPoint(doc, snaps, mx, my, ortho_base, grid_spacing, true);
      if (ev.hover_pick->snapped || !ev.hover_pick->snap_label.empty()) {
        const std::string& lbl = ev.hover_pick->snap_label;
        dl->AddText(ImVec2(io.MousePos.x + 14, io.MousePos.y + 10), IM_COL32(255, 255, 255, 230), lbl.c_str());
      }
    } else {
      PickResult free = PickPoint(doc, snaps, mx, my, std::nullopt, grid_spacing, false);
      ev.hover_pick = free;
    }
    if (want_objects || !want_point) ev.hover_object = PickObject(doc, mx, my);
    if (ev.hover_object != kNoObject) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    // Typing while hovering a viewport goes to the command line.
    if (io.InputQueueCharacters.Size > 0 && !io.WantTextInput) request_focus_command_line = true;
  }

  // Mouse buttons.
  const double now = ImGui::GetTime();
  if (hovered && !dragging_) {
    for (int b = 0; b < 3; ++b) {
      if (ImGui::IsMouseClicked(b)) {
        dragging_ = true;
        drag_button_ = b;
        drag_start_x_ = last_x_ = mx;
        drag_start_y_ = last_y_ = my;
        drag_moved_ = false;
        active_ = true;
      }
    }
    if (hovered) active_ = active_ || ImGui::IsMouseClicked(0);
  }
  if (dragging_) {
    const double dx = mx - last_x_, dy = my - last_y_;
    if (std::hypot(mx - drag_start_x_, my - drag_start_y_) > 3.0) drag_moved_ = true;
    if (drag_button_ == 1 || drag_button_ == 2) {
      if (drag_moved_) {
        if (io.KeyShift || drag_button_ == 2 || !camera_.State().perspective) {
          camera_.Pan(dx, dy, width_, height_);
        } else if (io.KeyCtrl) {
          camera_.Dolly(-dy * 0.02);
        } else {
          camera_.Orbit(dx, dy);
        }
      }
    } else if (drag_button_ == 0 && drag_moved_) {
      // Rubber-band selection rectangle.
      const ImVec2 a(static_cast<float>(img_x_ + drag_start_x_), static_cast<float>(img_y_ + drag_start_y_));
      const ImVec2 b(static_cast<float>(img_x_ + mx), static_cast<float>(img_y_ + my));
      const bool crossing = mx < drag_start_x_;
      dl->AddRectFilled(a, b, crossing ? IM_COL32(120, 200, 120, 40) : IM_COL32(120, 160, 255, 40));
      dl->AddRect(a, b, crossing ? IM_COL32(120, 220, 120, 220) : IM_COL32(140, 180, 255, 220));
    }
    last_x_ = mx;
    last_y_ = my;
    if (!ImGui::IsMouseDown(drag_button_)) {
      dragging_ = false;
      if (drag_button_ == 0) {
        if (drag_moved_) {
          ev.window = std::array<double, 4>{drag_start_x_, drag_start_y_, mx, my};
          ev.window_is_crossing = mx < drag_start_x_;
        } else {
          ev.clicked = true;
          ev.double_clicked = (now - last_click_time_) < 0.35;
          last_click_time_ = now;
          ev.click_pick = PickPoint(doc, snaps, mx, my, ortho_base, grid_spacing, want_point);
          ev.clicked_object = PickObject(doc, mx, my);
        }
      } else if (drag_button_ == 1 && !drag_moved_) {
        ev.right_clicked = true;
      }
      drag_button_ = -1;
    }
  }
  // Wheel zoom about the cursor.
  if (hovered && std::abs(io.MouseWheel) > 0.0f) {
    PickResult under = PickPoint(doc, snaps, mx, my, std::nullopt, grid_spacing, false);
    camera_.DollyToward(io.MouseWheel, under.point);
  }
  ImGui::End();
  ImGui::PopStyleVar();
  return ev;
}

}  // namespace dino8::app

namespace dino8::app {

bool Viewport::CaptureToFile(const std::string& path, std::string& error) const {
  const int w = target_.Width(), h = target_.Height();
  if (w <= 0 || h <= 0 || target_.Texture() == 0) { error = "Viewport has not been rendered yet"; return false; }
  std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
  target_.Bind();
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
  RenderTarget::Unbind();
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) { error = "Cannot write " + path; return false; }
  const int row = (w * 3 + 3) & ~3;
  const unsigned int data_size = static_cast<unsigned int>(row) * h;
  const unsigned int file_size = 54 + data_size;
  unsigned char hdr[54] = {'B', 'M'};
  auto put32 = [&](int at, unsigned int v) { for (int i = 0; i < 4; ++i) hdr[at + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff); };
  auto put16 = [&](int at, unsigned int v) { hdr[at] = static_cast<unsigned char>(v & 0xff); hdr[at + 1] = static_cast<unsigned char>((v >> 8) & 0xff); };
  put32(2, file_size); put32(10, 54); put32(14, 40); put32(18, static_cast<unsigned int>(w)); put32(22, static_cast<unsigned int>(h));
  put16(26, 1); put16(28, 24); put32(34, data_size);
  std::fwrite(hdr, 1, 54, f);
  std::vector<unsigned char> line(static_cast<size_t>(row), 0);
  for (int y = 0; y < h; ++y) {  // BMP rows are bottom-up, which matches GL
    for (int x = 0; x < w; ++x) {
      const unsigned char* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
      line[static_cast<size_t>(x) * 3] = p[2]; line[static_cast<size_t>(x) * 3 + 1] = p[1]; line[static_cast<size_t>(x) * 3 + 2] = p[0];
    }
    std::fwrite(line.data(), 1, line.size(), f);
  }
  std::fclose(f);
  return true;
}

}  // namespace dino8::app
