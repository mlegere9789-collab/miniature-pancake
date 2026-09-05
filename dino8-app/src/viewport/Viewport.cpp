#include "viewport/Viewport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "imgui.h"
#include "ui/Theme.h"

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

DisplayMode DisplayModeFromName(const std::string& name) {
  std::string n;
  for (char c : name) if (c != '-' && c != ' ' && c != '_') n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  for (DisplayMode m : AllDisplayModes()) {
    std::string mn;
    for (const char* c = DisplayModeName(m); *c; ++c) if (*c != '-' && *c != ' ') mn.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*c))));
    if (mn == n) return m;
  }
  return DisplayMode::Shaded;
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
const Color kEdgeHighlightColor = Color::FromBytes(255, 70, 220);  // ShowEdges: all edges (magenta)
const Color kNakedEdgeColor = Color::FromBytes(255, 40, 40);       // ShowEdges: naked edges (red)

}  // namespace

// Background colours for a display mode, honouring the document's render
// environment (Rendered mode) and the GradientView toggle.
void BackgroundFor(DisplayMode mode, const Document* doc, bool arctic, Color& top, Color& bottom) {
  const ModeStyle style = StyleFor(mode);
  top = style.bg_top;
  bottom = style.bg_bottom;
  if (!doc) return;
  const RenderSettings& r = doc->Render();
  if (arctic) { top = bottom = Color::FromBytes(250, 250, 250); return; }
  if (mode == DisplayMode::Rendered) {
    switch (r.background) {
      case RenderSettings::Background::Solid: top = bottom = r.background_color; break;
      case RenderSettings::Background::Gradient: top = r.gradient_top; bottom = r.gradient_bottom; break;
      case RenderSettings::Background::Sky: top = Color::FromBytes(96, 138, 200); bottom = Color::FromBytes(222, 230, 240); break;
    }
    return;
  }
  if (!r.gradient_view) top = bottom;
}

void Viewport::Render(GlRenderer& renderer, const FrameContext& ctx) {
  if (!target_.Resize(std::max(width_, 1), std::max(height_, 1))) return;
  target_.Bind();
  Color top, bottom;
  BackgroundFor(mode_, ctx.doc, false, top, bottom);
  renderer.SetMatrices(camera_.ViewMatrix(), camera_.ProjectionMatrix(Aspect()));
  renderer.ClearGradient(top, bottom);
  renderer.EnableDepthTest(true);
  renderer.EnableBlend(true);
  if (page_) DrawPage(renderer);
  else DrawScene(renderer, ctx, mode_, Aspect());
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

bool Viewport::RenderToImage(GlRenderer& renderer, const FrameContext& base, int w, int h, int supersample,
                             bool arctic, std::vector<unsigned char>& rgb, std::string& error) {
  w = std::clamp(w, 1, 8192);
  h = std::clamp(h, 1, 8192);
  supersample = std::clamp(supersample, 1, 4);
  while (supersample > 1 && (w * supersample > 8192 || h * supersample > 8192)) --supersample;
  RenderTarget rt;
  if (!rt.Resize(w * supersample, h * supersample)) { error = "Could not create a render buffer of " + std::to_string(w) + " x " + std::to_string(h); return false; }
  FrameContext ctx = base;
  ctx.for_render = true;
  ctx.arctic = arctic;
  ctx.preview_lines = nullptr;
  ctx.preview_points = nullptr;
  ctx.cursor_marker.reset();
  const double aspect = static_cast<double>(w) / h;
  rt.Bind();
  Color top, bottom;
  BackgroundFor(DisplayMode::Rendered, ctx.doc, arctic, top, bottom);
  renderer.SetMatrices(camera_.ViewMatrix(), camera_.ProjectionMatrix(aspect));
  renderer.ClearGradient(top, bottom);
  renderer.EnableDepthTest(true);
  renderer.EnableBlend(true);
  DrawScene(renderer, ctx, DisplayMode::Rendered, aspect);
  renderer.EnableDepthTest(true);
  std::vector<unsigned char> big;
  rt.ReadPixels(big);
  RenderTarget::Unbind();
  // Box-filter the supersampled image down.
  rgb.assign(static_cast<size_t>(w) * h * 3, 0);
  const int W = w * supersample;
  const int n = supersample * supersample;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int acc[3] = {0, 0, 0};
      for (int sy = 0; sy < supersample; ++sy) {
        for (int sx = 0; sx < supersample; ++sx) {
          const unsigned char* p = &big[(static_cast<size_t>(y * supersample + sy) * W + static_cast<size_t>(x * supersample + sx)) * 3];
          acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2];
        }
      }
      unsigned char* o = &rgb[(static_cast<size_t>(y) * w + x) * 3];
      o[0] = static_cast<unsigned char>(acc[0] / n); o[1] = static_cast<unsigned char>(acc[1] / n); o[2] = static_cast<unsigned char>(acc[2] / n);
    }
  }
  // Restore this viewport's own projection for anything drawn afterwards.
  renderer.SetMatrices(camera_.ViewMatrix(), camera_.ProjectionMatrix(Aspect()));
  return true;
}

void Viewport::DrawScene(GlRenderer& renderer, const FrameContext& ctx, DisplayMode mode, double aspect) {
  (void)aspect;
  if (!ctx.doc) return;
  doc_for_grid_ = ctx.doc;
  // The ground plane replaces the grid in Rendered mode (as in Rhino).
  const bool ground = mode == DisplayMode::Rendered && ctx.doc->Render().ground_plane;
  if (!ctx.for_render && !ground) DrawGrid(renderer, ctx.doc->Settings(), mode);
  if (mode == DisplayMode::Rendered) {
    SetupLights(renderer, ctx);
    DrawGroundPlane(renderer, ctx);
  }
  // Clipping planes that clip this viewport cut the model (not the grid).
  std::vector<std::array<float, 4>> clip;
  for (const ClippingPlane& cp : ctx.doc->ClippingPlanes()) {
    if (!cp.enabled || !cp.ClipsViewport(name_)) continue;
    Vector3d n = cp.Normal();
    if (!n.Unitize()) continue;
    // Keep the half-space behind the plane: -(n . (p - origin)) >= 0.
    clip.push_back({static_cast<float>(-n.x), static_cast<float>(-n.y), static_cast<float>(-n.z),
                    static_cast<float>(ON_DotProduct(n, cp.origin))});
  }
  if (!clip.empty()) renderer.SetClipPlanes(clip);
  DrawObjects(renderer, ctx, mode);
  if (!clip.empty()) renderer.ClearClipPlanes();
  if (!ctx.for_render && ctx.show_clipping_planes) DrawClippingPlanes(renderer, *ctx.doc);
  if (!ctx.for_render) DrawLightWidgets(renderer, *ctx.doc);
}

void Viewport::SetupLights(GlRenderer& renderer, const FrameContext& ctx) {
  const Document& doc = *ctx.doc;
  const RenderSettings& r = doc.Render();
  std::vector<GpuLight> lights;
  for (const Light& L : doc.Lights()) {
    if (!L.enabled || lights.size() >= static_cast<size_t>(kMaxGpuLights)) continue;
    GpuLight g;
    g.r = L.color.r * L.intensity; g.g = L.color.g * L.intensity; g.b = L.color.b * L.intensity;
    g.direction = L.direction;
    switch (L.type) {
      case LightType::Point: g.kind = GpuLight::Point; g.position = L.position; break;
      case LightType::Directional: g.kind = GpuLight::Directional; break;
      case LightType::Spot: {
        g.kind = GpuLight::Spot;
        g.position = L.position;
        const double outer = std::clamp(static_cast<double>(L.spot_angle), 1.0, 89.0) * ON_PI / 180.0;
        const double inner = outer * (0.35 + 0.6 * std::clamp(L.spot_hardness, 0.f, 1.f));
        g.cos_outer = static_cast<float>(std::cos(outer));
        g.cos_inner = static_cast<float>(std::cos(inner));
        break;
      }
      case LightType::Rectangular: {
        // Area light approximated by a wide spot at its centre.
        g.kind = GpuLight::Spot;
        g.position = L.position;
        g.cos_outer = static_cast<float>(std::cos(85.0 * ON_PI / 180.0));
        g.cos_inner = static_cast<float>(std::cos(45.0 * ON_PI / 180.0));
        break;
      }
      case LightType::Linear: {
        g.kind = GpuLight::Point;
        g.position = L.position + L.x_axis * (L.length * 0.5);
        break;
      }
    }
    lights.push_back(g);
  }
  if (r.sun && lights.size() < static_cast<size_t>(kMaxGpuLights)) {
    GpuLight g;
    g.kind = GpuLight::Directional;
    const double az = r.sun_azimuth * ON_PI / 180.0, alt = r.sun_altitude * ON_PI / 180.0;
    const Vector3d towards_sun(std::cos(alt) * std::sin(az), std::cos(alt) * std::cos(az), std::sin(alt));
    g.direction = -towards_sun;
    g.r = r.sun_color.r * r.sun_intensity; g.g = r.sun_color.g * r.sun_intensity; g.b = r.sun_color.b * r.sun_intensity;
    lights.push_back(g);
  }
  if (lights.empty()) {
    // Rhino's default lighting: a key light over the camera's left
    // shoulder and a dimmer fill from the right, both following the view.
    const Vector3d f = camera_.Forward(), rgt = camera_.Right(), up = camera_.Up();
    GpuLight key;
    key.kind = GpuLight::Directional;
    key.direction = f * 0.7 - up * 0.55 + rgt * 0.35;
    key.r = key.g = key.b = 0.95f;
    GpuLight fill;
    fill.kind = GpuLight::Directional;
    fill.direction = f * 0.6 - up * 0.1 - rgt * 0.7;
    fill.r = 0.42f; fill.g = 0.43f; fill.b = 0.46f;
    lights = {key, fill};
  }
  Color ambient = r.skylight ? Color::FromBytes(84, 90, 100) : Color::FromBytes(40, 40, 42);
  if (ctx.arctic) {
    ambient = Color::FromBytes(150, 150, 152);
    for (GpuLight& g : lights) { g.r *= 0.7f; g.g *= 0.7f; g.b *= 0.7f; }
  }
  renderer.SetLights(lights, ambient);
}

void Viewport::DrawGroundPlane(GlRenderer& renderer, const FrameContext& ctx) {
  const Document& doc = *ctx.doc;
  const RenderSettings& r = doc.Render();
  if (!r.ground_plane) return;
  kernel::BoundingBox box;
  const bool has = doc.VisibleBoundingBox(box);
  if (!has) { box.min = Point3d(-10, -10, 0); box.max = Point3d(10, 10, 0); }
  const double z = r.ground_auto_height ? box.min.z : r.ground_height;
  const double cx = (box.min.x + box.max.x) / 2, cy = (box.min.y + box.max.y) / 2;
  const double radius = std::max({box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, 1.0}) / 2;
  const double half = std::max(radius * 60, 100.0);
  const double fade = std::max(radius * 14, 25.0);
  std::vector<ShadowBlob> blobs;
  if (r.ground_shadows) {
    for (const SceneObject& o : doc.Objects()) {
      if (!doc.IsObjectVisible(o) || blobs.size() >= static_cast<size_t>(kMaxShadowBlobs)) continue;
      o.EnsureDisplay(ctx.curve_tolerance, ctx.surface_tolerance);
      const DisplayCache& d = o.Display();
      if (d.triangles.empty() || !d.has_bbox) continue;
      ShadowBlob b;
      b.cx = static_cast<float>((d.bbox.min.x + d.bbox.max.x) / 2);
      b.cy = static_cast<float>((d.bbox.min.y + d.bbox.max.y) / 2);
      const double sx = d.bbox.max.x - d.bbox.min.x, sy = d.bbox.max.y - d.bbox.min.y;
      b.rx = static_cast<float>(std::max(sx * 0.62, radius * 0.03));
      b.ry = static_cast<float>(std::max(sy * 0.62, radius * 0.03));
      const double gap = (d.bbox.min.z - z) / std::max(radius, 1e-9);
      b.strength = static_cast<float>(std::clamp(1.0 - gap * 1.2, 0.0, 1.0)) * (ctx.arctic ? 0.55f : 0.9f);
      if (b.strength > 0.01f) blobs.push_back(b);
    }
  }
  Color color = ctx.arctic ? Color::FromBytes(246, 246, 246) : r.ground_color;
  color.a = 1.f;
  renderer.DrawGroundPlane(cx, cy, z - radius * 2e-4, half, fade, color, blobs);
}

void Viewport::DrawLightWidgets(GlRenderer& renderer, const Document& doc) {
  if (doc.Lights().empty()) return;
  const double px = camera_.PixelSize(std::max(height_, 1));
  const double s = px * 10;  // widget size in world units
  std::vector<float> lines, sel_lines;
  auto seg = [&](std::vector<float>& v, Point3d a, Point3d b) {
    v.push_back(static_cast<float>(a.x)); v.push_back(static_cast<float>(a.y)); v.push_back(static_cast<float>(a.z));
    v.push_back(static_cast<float>(b.x)); v.push_back(static_cast<float>(b.y)); v.push_back(static_cast<float>(b.z));
  };
  auto frame = [](Vector3d d, Vector3d& u, Vector3d& v) {
    if (!d.Unitize()) d = Vector3d(0, 0, -1);
    Vector3d ref = std::fabs(d.z) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
    u = ON_CrossProduct(d, ref); u.Unitize();
    v = ON_CrossProduct(d, u); v.Unitize();
  };
  std::vector<float> disabled;
  for (const Light& L : doc.Lights()) {
    std::vector<float>& out = L.selected ? sel_lines : (L.enabled ? lines : disabled);
    const Point3d p = L.position;
    Vector3d d = L.direction;
    if (!d.Unitize()) d = Vector3d(0, 0, -1);
    switch (L.type) {
      case LightType::Point: {
        // A small star: three axes plus the four space diagonals.
        seg(out, p + Vector3d(-s, 0, 0), p + Vector3d(s, 0, 0));
        seg(out, p + Vector3d(0, -s, 0), p + Vector3d(0, s, 0));
        seg(out, p + Vector3d(0, 0, -s), p + Vector3d(0, 0, s));
        const double t = s * 0.6;
        seg(out, p + Vector3d(-t, -t, -t), p + Vector3d(t, t, t));
        seg(out, p + Vector3d(-t, t, -t), p + Vector3d(t, -t, t));
        seg(out, p + Vector3d(t, -t, -t), p + Vector3d(-t, t, t));
        seg(out, p + Vector3d(t, t, -t), p + Vector3d(-t, -t, t));
        break;
      }
      case LightType::Spot: {
        const double len = L.length > 0 ? L.length : s * 6;
        const double rad = len * std::tan(std::clamp(static_cast<double>(L.spot_angle), 1.0, 89.0) * ON_PI / 180.0);
        Vector3d u, v;
        frame(d, u, v);
        const Point3d base = p + d * len;
        const int n = 16;
        Point3d prev = base + u * rad;
        for (int i = 1; i <= n; ++i) {
          const double a = 2 * ON_PI * i / n;
          const Point3d q = base + u * (rad * std::cos(a)) + v * (rad * std::sin(a));
          seg(out, prev, q);
          if (i % 4 == 0) seg(out, p, q);
          prev = q;
        }
        seg(out, p + Vector3d(-s, 0, 0), p + Vector3d(s, 0, 0));
        seg(out, p + Vector3d(0, -s, 0), p + Vector3d(0, s, 0));
        seg(out, p + Vector3d(0, 0, -s), p + Vector3d(0, 0, s));
        break;
      }
      case LightType::Directional: {
        Vector3d u, v;
        frame(d, u, v);
        const double len = s * 5;
        const Point3d tip = p + d * len;
        seg(out, p, tip);
        seg(out, tip, tip - d * (s * 1.2) + u * (s * 0.6));
        seg(out, tip, tip - d * (s * 1.2) - u * (s * 0.6));
        seg(out, tip, tip - d * (s * 1.2) + v * (s * 0.6));
        seg(out, tip, tip - d * (s * 1.2) - v * (s * 0.6));
        // Three parallel rays show it lights everything the same way.
        seg(out, p + u * s, p + u * s + d * (len * 0.7));
        seg(out, p - u * s, p - u * s + d * (len * 0.7));
        break;
      }
      case LightType::Rectangular: {
        Vector3d x = L.x_axis;
        if (!x.Unitize()) x = Vector3d(1, 0, 0);
        Vector3d y = ON_CrossProduct(d, x);
        if (!y.Unitize()) y = Vector3d(0, 1, 0);
        const double hl = L.length / 2, hw = L.width / 2;
        const Point3d c0 = p - x * hl - y * hw, c1 = p + x * hl - y * hw, c2 = p + x * hl + y * hw, c3 = p - x * hl + y * hw;
        seg(out, c0, c1); seg(out, c1, c2); seg(out, c2, c3); seg(out, c3, c0);
        seg(out, c0, c2); seg(out, c1, c3);
        seg(out, p, p + d * std::max(hl, hw));
        break;
      }
      case LightType::Linear: {
        Vector3d x = L.x_axis;
        if (!x.Unitize()) x = Vector3d(1, 0, 0);
        const Point3d q = p + x * L.length;
        seg(out, p, q);
        Vector3d u, v;
        frame(x, u, v);
        seg(out, p - u * s, p + u * s); seg(out, q - u * s, q + u * s);
        seg(out, p - v * s, p + v * s); seg(out, q - v * s, q + v * s);
        seg(out, p + d * s, q + d * s);
        break;
      }
    }
  }
  renderer.DrawLines(lines, Color::FromBytes(255, 214, 90));
  renderer.DrawLines(disabled, Color::FromBytes(130, 130, 130));
  renderer.DrawLines(sel_lines, kSelectionColor, 2.0f);
}

void Viewport::DrawGrid(GlRenderer& renderer, const DocumentSettings& s, DisplayMode mode) {
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
  bool light = mode == DisplayMode::Pen || mode == DisplayMode::Arctic ||
               mode == DisplayMode::Technical || mode == DisplayMode::Artistic;
  if (mode == DisplayMode::Rendered) {
    Color top, bottom;
    BackgroundFor(mode, doc_for_grid_, false, top, bottom);
    light = 0.299f * bottom.r + 0.587f * bottom.g + 0.114f * bottom.b > 0.5f;
  }
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

void Viewport::DrawPage(GlRenderer& renderer) {
  // A white sheet with a drop shadow on the neutral background; the page
  // lies in the world XY plane (1 unit = 1 mm) so picks give page coordinates.
  auto quad = [](std::vector<float>& v, double x0, double y0, double x1, double y1, double z) {
    const float pts[6][3] = {{static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(z)}, {static_cast<float>(x1), static_cast<float>(y0), static_cast<float>(z)}, {static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(z)},
                             {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(z)}, {static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(z)}, {static_cast<float>(x0), static_cast<float>(y1), static_cast<float>(z)}};
    for (auto& p : pts) { v.insert(v.end(), {p[0], p[1], p[2], 0.f, 0.f, 1.f}); }
  };
  std::vector<float> shadow, sheet;
  const double sh = std::max(page_w_, page_h_) * 0.01;
  quad(shadow, sh, -sh, page_w_ + sh, page_h_ - sh, -0.02);
  quad(sheet, 0, 0, page_w_, page_h_, -0.01);
  renderer.EnableDepthTest(false);
  renderer.DrawTriangles(shadow, Color{0.f, 0.f, 0.f, 0.35f}, false);
  renderer.DrawTriangles(sheet, Color::FromBytes(255, 255, 255), false);
  const float w = static_cast<float>(page_w_), h = static_cast<float>(page_h_);
  std::vector<float> border = {0, 0, 0, w, 0, 0, w, 0, 0, w, h, 0, w, h, 0, 0, h, 0, 0, h, 0, 0, 0, 0};
  renderer.DrawLines(border, Color::FromBytes(120, 120, 120));
  renderer.EnableDepthTest(true);
}

void Viewport::DrawClippingPlanes(GlRenderer& renderer, const Document& doc) {
  for (const ClippingPlane& cp : doc.ClippingPlanes()) {
    Vector3d x = cp.x_axis, y = cp.y_axis;
    if (!x.Unitize() || !y.Unitize()) continue;
    const Vector3d n = ON_CrossProduct(x, y);
    const Point3d c = cp.origin;
    const Point3d p00 = c - x * (cp.width / 2) - y * (cp.height / 2), p10 = c + x * (cp.width / 2) - y * (cp.height / 2);
    const Point3d p11 = c + x * (cp.width / 2) + y * (cp.height / 2), p01 = c - x * (cp.width / 2) + y * (cp.height / 2);
    std::vector<float> tri;
    for (const Point3d* p : {&p00, &p10, &p11, &p00, &p11, &p01}) {
      tri.insert(tri.end(), {static_cast<float>(p->x), static_cast<float>(p->y), static_cast<float>(p->z), static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
    }
    const Color fill = cp.selected ? Color{1.f, 0.82f, 0.f, 0.35f} : (cp.enabled ? Color{0.35f, 0.65f, 1.f, 0.22f} : Color{0.6f, 0.6f, 0.6f, 0.15f});
    renderer.DrawTriangles(tri, fill, false);
    std::vector<float> lines;
    auto push = [&](Point3d a, Point3d b) { lines.insert(lines.end(), {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z), static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z)}); };
    push(p00, p10); push(p10, p11); push(p11, p01); push(p01, p00);
    // Normal arrow (the side that gets cut away).
    const double len = 0.25 * std::max(cp.width, cp.height);
    const Point3d tip = c + n * len;
    push(c, tip);
    push(tip, tip - n * (len * 0.2) + x * (len * 0.08));
    push(tip, tip - n * (len * 0.2) - x * (len * 0.08));
    const Color edge = cp.selected ? kSelectionColor : (cp.enabled ? Color::FromBytes(90, 150, 240) : Color::FromBytes(140, 140, 140));
    renderer.DrawLines(lines, edge, cp.selected ? 2.0f : 1.0f);
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

void Viewport::DrawObjects(GlRenderer& renderer, const FrameContext& ctx, DisplayMode mode) {
  const Document& doc = *ctx.doc;
  const ModeStyle style = StyleFor(mode);
  const bool rendered = mode == DisplayMode::Rendered;
  // Rendered mode draws every opaque object first, then the transparent
  // ones back to front with depth writes off so glass composites properly.
  std::vector<std::pair<double, const SceneObject*>> transparent;
  auto draw_rendered = [&](const SceneObject& o, const Material& m) {
    const DisplayCache& d = o.Display();
    RenderMaterial rm;
    rm.diffuse = m.diffuse;
    if (ctx.arctic) { rm.diffuse = Color::FromBytes(240, 240, 240); rm.specular = Color::FromBytes(40, 40, 40); rm.shininess = 6.f; rm.reflectivity = 0.f; }
    else {
      const float gloss = std::clamp(m.gloss, 0.f, 1.f);
      rm.specular = Color{m.specular.r * (0.15f + 0.85f * gloss), m.specular.g * (0.15f + 0.85f * gloss), m.specular.b * (0.15f + 0.85f * gloss), 1.f};
      rm.shininess = 4.f * std::pow(2.f, gloss * 6.f);
      rm.reflectivity = std::clamp(m.reflectivity, 0.f, 1.f);
      rm.emission = m.emission;
      if (!m.texture_path.empty()) rm.texture = renderer.TextureFor(m.texture_path);
    }
    if (doc.IsObjectLocked(o)) rm.diffuse = Mix(rm.diffuse, kLockedColor, 0.6f);
    if (o.selected && !ctx.for_render) rm.diffuse = Mix(rm.diffuse, kSelectionColor, 0.55f);
    rm.diffuse.a = std::clamp(1.f - m.transparency, 0.f, 1.f) * style.fill_alpha;
    const std::vector<float>* uvs = nullptr;
    if (rm.texture) {
      const TextureMapping mapping = o.mapping != TextureMapping::Default ? o.mapping : m.mapping;
      const float scale = o.mapping != TextureMapping::Default ? o.mapping_scale : m.mapping_scale;
      o.EnsureMappedUVs(mapping, scale);
      uvs = &d.mapped_uvs;
    }
    renderer.DrawTrianglesRendered(d.triangles, uvs, rm);
  };
  auto shown = [&](const SceneObject& o) {
    if (!doc.IsObjectVisible(o)) return false;
    if (ctx.hidden_layers && std::find(ctx.hidden_layers->begin(), ctx.hidden_layers->end(), o.layer_index) != ctx.hidden_layers->end()) return false;
    if (ctx.hidden_objects && std::find(ctx.hidden_objects->begin(), ctx.hidden_objects->end(), o.id) != ctx.hidden_objects->end()) return false;
    return true;
  };
  const float curve_width = ctx.print_display ? 2.5f : 1.0f;
  // Pass 1: fills (with polygon offset so edges win the depth test).
  if (style.fill) {
    renderer.EnablePolygonOffset(true);
    for (const SceneObject& o : doc.Objects()) {
      if (!shown(o)) continue;
      o.EnsureDisplay(ctx.curve_tolerance, ctx.surface_tolerance);
      const DisplayCache& d = o.Display();
      if (d.triangles.empty()) continue;
      // Shaded/Ghosted/X-Ray fill with one light material like Rhino's
      // default, unless the object carries a material; Rendered uses the
      // object/layer colour.
      Color c = Color::FromBytes(205, 207, 212);
      if (rendered || !o.material_name.empty() || !o.color_by_layer) c = doc.EffectiveColor(o);
      if (style.force_white) c = Color::FromBytes(245, 245, 245);
      else if (style.monochrome) c = Color::FromBytes(200, 200, 205);
      if (doc.IsObjectLocked(o)) c = Mix(c, kLockedColor, 0.6f);
      if (o.selected) c = Mix(c, kSelectionColor, 0.55f);
      c.a = style.fill_alpha;
      // Surface analysis: the object's own setting wins, else the app-wide
      // fallback (Zebra/EMap with nothing selected applies to every surface).
      const AnalysisSettings* analysis = nullptr;
      if (o.analysis.mode != AnalysisMode::None) analysis = &o.analysis;
      else if (ctx.fallback_analysis && ctx.fallback_analysis->mode != AnalysisMode::None) analysis = ctx.fallback_analysis;
      if (analysis) {
        switch (analysis->mode) {
          case AnalysisMode::Zebra:
            renderer.DrawTrianglesZebra(d.triangles, analysis->zebra_direction == ZebraDirection::Vertical,
                                        analysis->zebra_density, style.fill_alpha);
            continue;
          case AnalysisMode::EMap: {
            Color tint = Color::FromBytes(255, 255, 255);
            if (o.selected) tint = Mix(tint, kSelectionColor, 0.2f);
            tint.a = style.fill_alpha;
            renderer.DrawTrianglesEMap(d.triangles, tint);
            continue;
          }
          case AnalysisMode::Curvature:
          case AnalysisMode::DraftAngle:
            o.EnsureAnalysisColors(*analysis);
            if (!d.colors.empty()) {
              renderer.DrawTriangles(d.triangles, d.colors, style.fill_alpha);
              continue;
            }
            break;
          case AnalysisMode::None: break;
        }
      }
      if (rendered) {
        const Material m = doc.MaterialFor(o);
        if (m.transparency > 0.001f && !ctx.arctic) {
          // Sort key: view-space depth of the bounding-box centre.
          const Point3d centre = d.has_bbox ? Point3d((d.bbox.min.x + d.bbox.max.x) / 2, (d.bbox.min.y + d.bbox.max.y) / 2, (d.bbox.min.z + d.bbox.max.z) / 2) : Point3d(0, 0, 0);
          transparent.emplace_back((centre - camera_.State().eye) * camera_.Forward(), &o);
          continue;
        }
        draw_rendered(o, m);
        continue;
      }
      renderer.DrawTriangles(d.triangles, c, style.lit);
    }
    if (!transparent.empty()) {
      std::sort(transparent.begin(), transparent.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
      renderer.EnableDepthWrite(false);
      for (const auto& [depth, o] : transparent) draw_rendered(*o, doc.MaterialFor(*o));
      renderer.EnableDepthWrite(true);
    }
    renderer.EnablePolygonOffset(false);
  }
  // Pass 2: curves, edges, isocurves, points, control points.
  if (!style.depth_lines) renderer.EnableDepthTest(false);
  for (const SceneObject& o : doc.Objects()) {
    if (!doc.IsObjectVisible(o)) continue;
    if (o.kind == ObjectKind::Curve) o.SetDisplayDashes(doc.EffectiveDashes(o));
    if (!shown(o)) continue;
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
      renderer.DrawLines(d.lines, line_color, is_curve_like ? curve_width : 1.0f);
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
    if (o.highlight_edges && !d.edges.empty()) {
      // ShowEdges: every edge thick, naked edges on top in a second colour.
      renderer.DrawLines(d.edges, kEdgeHighlightColor, 3.0f);
      if (!d.naked_edges.empty()) renderer.DrawLines(d.naked_edges, kNakedEdgeColor, 4.0f);
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
  if (page_) return kNoObject;
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
  if (page_) return result;
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
          if (snaps.perp && ortho_base) {
            consider(c.ClosestPoint(*ortho_base), "Perp");
          }
          if (snaps.tan && ortho_base) {
            // Tangent from the previous point: minimise the angle between
            // (C(t) - base) and the curve tangent, then refine locally.
            const int n = 64;
            double best_t = dom.min, best_v = 1e300;
            auto score = [&](double t) {
              Vector3d d = c.PointAt(t) - *ortho_base;
              Vector3d tg = c.TangentAt(t);
              if (!d.Unitize()) return 1e300;
              return 1.0 - std::fabs(ON_DotProduct(d, tg));
            };
            for (int i = 0; i <= n; ++i) {
              const double t = dom.min + (dom.max - dom.min) * i / n;
              const double v = score(t);
              if (v < best_v) { best_v = v; best_t = t; }
            }
            double lo = std::max(dom.min, best_t - (dom.max - dom.min) / n), hi = std::min(dom.max, best_t + (dom.max - dom.min) / n);
            for (int it = 0; it < 30; ++it) {
              const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
              if (score(m1) < score(m2)) hi = m2; else lo = m1;
            }
            best_t = (lo + hi) / 2;
            if (score(best_t) < 0.01) consider(c.PointAt(best_t), "Tan");
          }
          if (snaps.int_) {
            // Intersections with other visible curves near the cursor, from
            // the display polylines (screen-space test, 3D result).
            const DisplayCache& d = o.Display();
            for (const SceneObject& other : doc.Objects()) {
              if (&other == &o || other.kind != ObjectKind::Curve || !doc.IsObjectVisible(other) || other.id < o.id) continue;
              const DisplayCache& e = other.Display();
              for (size_t i = 0; i + 5 < d.lines.size(); i += 6) {
                double ax, ay, bx, by, tt;
                const Point3d a(d.lines[i], d.lines[i + 1], d.lines[i + 2]), b(d.lines[i + 3], d.lines[i + 4], d.lines[i + 5]);
                if (!WorldToPixel(a, ax, ay) || !WorldToPixel(b, bx, by)) continue;
                if (PointSegmentDistance2D(px, py, ax, ay, bx, by, tt) > snap_radius * 2) continue;
                for (size_t j = 0; j + 5 < e.lines.size(); j += 6) {
                  double cx, cy, dx, dy, t2;
                  const Point3d p(e.lines[j], e.lines[j + 1], e.lines[j + 2]), q(e.lines[j + 3], e.lines[j + 4], e.lines[j + 5]);
                  if (!WorldToPixel(p, cx, cy) || !WorldToPixel(q, dx, dy)) continue;
                  if (PointSegmentDistance2D(px, py, cx, cy, dx, dy, t2) > snap_radius * 2) continue;
                  const double r1x = bx - ax, r1y = by - ay, r2x = dx - cx, r2y = dy - cy;
                  const double den = r1x * r2y - r1y * r2x;
                  if (std::fabs(den) < 1e-9) continue;
                  const double u = ((cx - ax) * r2y - (cy - ay) * r2x) / den;
                  const double v = ((cx - ax) * r1y - (cy - ay) * r1x) / den;
                  if (u < 0 || u > 1 || v < 0 || v > 1) continue;
                  consider(a + (b - a) * u, "Int");
                }
              }
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
  ev = DrawContent(doc, snaps, want_point, want_objects, ortho_base, grid_spacing, request_focus_command_line, false);
  ImGui::End();
  ImGui::PopStyleVar();
  return ev;
}

ViewportEvents Viewport::DrawEmbedded(const Document& doc, const SnapSettings& snaps, bool want_point,
                                      bool want_objects, std::optional<Point3d> ortho_base,
                                      double grid_spacing, bool& request_focus_command_line, int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  ImGui::PushID(name_.c_str());
  ViewportEvents ev = DrawContent(doc, snaps, want_point, want_objects, ortho_base, grid_spacing, request_focus_command_line, true);
  ImGui::PopID();
  return ev;
}

ViewportEvents Viewport::DrawContent(const Document& doc, const SnapSettings& snaps, bool want_point,
                                     bool want_objects, std::optional<Point3d> ortho_base,
                                     double grid_spacing, bool& request_focus_command_line, bool embedded) {
  ViewportEvents ev;
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  screen_x_ = cursor.x;
  screen_y_ = cursor.y;
  img_x_ = cursor.x;
  img_y_ = cursor.y;
  if (target_.Texture()) {
    ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(target_.Texture())), ImVec2(static_cast<float>(width_), static_cast<float>(height_)), ImVec2(0, 1), ImVec2(1, 0));
  } else {
    ImGui::Dummy(ImVec2(static_cast<float>(width_), static_cast<float>(height_)));
  }
  const bool hovered = ImGui::IsItemHovered();
  ev.hovered = hovered;
  if (std::getenv("DINO8_UI_DEBUG")) {
    const ImVec2 mp = ImGui::GetIO().MousePos;
    if (mp.x >= img_x_ && mp.x <= img_x_ + width_ && mp.y >= img_y_ && mp.y <= img_y_ + height_)
      std::fprintf(stderr, "[vp] %s frame %d mouse %.0f,%.0f in rect (%.0f,%.0f %dx%d) hovered=%d down0=%d dragging=%d\n", name_.c_str(), ImGui::GetFrameCount(), mp.x, mp.y, img_x_, img_y_, width_, height_, hovered ? 1 : 0, ImGui::IsMouseDown(0) ? 1 : 0, dragging_ ? 1 : 0);
  }
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
    const ImVec4 acc = ImVec4(ThemeColors::kAccent[0], ThemeColors::kAccent[1], ThemeColors::kAccent[2], 1.0f);
    const ImU32 pill = active_ ? ImGui::GetColorU32(ImVec4(acc.x, acc.y, acc.z, 0.85f)) : IM_COL32(30, 32, 38, 150);
    dl->AddRectFilled(ImVec2(p0.x - 4, p0.y - 2), ImVec2(p0.x + sz.x + 8, p0.y + sz.y + 2), pill, 4.0f);
    if (!active_) dl->AddRect(ImVec2(p0.x - 4, p0.y - 2), ImVec2(p0.x + sz.x + 8, p0.y + sz.y + 2), IM_COL32(255, 255, 255, 40), 4.0f);
    dl->AddText(p0, IM_COL32(255, 255, 255, active_ ? 255 : 215), label);
    ImGui::SetCursorScreenPos(ImVec2(p0.x - 4, p0.y - 2));
    if (ImGui::InvisibleButton(("##title_" + name_).c_str(), ImVec2(sz.x + 12, sz.y + 4)) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
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
      if (!embedded && ImGui::MenuItem(maximized_ ? "Restore Viewports" : "Maximize Viewport")) maximized_ = !maximized_;
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
  if (hovered && !dragging_ && !all_input_locked_) {
    for (int b = 0; b < 3; ++b) {
      if (b == 0 && input_locked_) continue;
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
        ev.right_click_object = PickObject(doc, mx, my);
      } else if (drag_button_ == 2 && !drag_moved_) {
        ev.middle_clicked = true;
      }
      drag_button_ = -1;
    }
  }
  // Wheel zoom about the cursor.
  if (hovered && !all_input_locked_ && std::abs(io.MouseWheel) > 0.0f) {
    PickResult under = PickPoint(doc, snaps, mx, my, std::nullopt, grid_spacing, false);
    camera_.DollyToward(io.MouseWheel, under.point);
  }
  if (embedded) ImGui::SetCursorScreenPos(cursor);
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
