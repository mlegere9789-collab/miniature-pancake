// One model viewport: a camera, a render target, a display mode, a
// construction plane, and the input handling that turns mouse activity
// into navigation, point picks, and object selection.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "doc/Document.h"
#include "render/GlRenderer.h"
#include "viewport/Camera.h"

namespace dino8::app {

enum class DisplayMode {
  Wireframe, Shaded, Rendered, Ghosted, XRay, Technical, Artistic, Pen, Arctic, Monochrome
};
const char* DisplayModeName(DisplayMode mode);
std::vector<DisplayMode> AllDisplayModes();

struct ConstructionPlane {
  kernel::Point3d origin{0, 0, 0};
  kernel::Vector3d x_axis{1, 0, 0};
  kernel::Vector3d y_axis{0, 1, 0};
  kernel::Vector3d Normal() const { return ON_CrossProduct(x_axis, y_axis); }
  kernel::Point3d ToWorld(double u, double v, double w = 0) const {
    return origin + x_axis * u + y_axis * v + Normal() * w;
  }
};

// Object snaps the status bar toggles (persistent osnaps, Rhino-style).
struct SnapSettings {
  bool end = true, mid = false, cen = false, point = true, near_ = false, vertex = false, int_ = false, perp = false, tan = false, quad = false;
  bool grid_snap = false;
  bool ortho = false;
  bool planar = false;
  bool smart_track = false;
  bool disable_all = false;
};

struct PickResult {
  kernel::Point3d point;
  std::string snap_label;  // "End", "Mid", "Cen", "Grid", "" when free
  bool snapped = false;
};

// What happened in a viewport this frame, for the application to act on.
struct ViewportEvents {
  bool clicked = false;               // left click (down+up without drag)
  bool double_clicked = false;
  bool right_clicked = false;         // right click without drag = Enter
  bool shift = false, ctrl = false;
  std::optional<PickResult> click_pick;         // world point under cursor
  ObjectId clicked_object = kNoObject;          // object under cursor at click
  std::optional<std::array<double, 4>> window;  // x0,y0,x1,y1 in pixels: window/crossing select finished
  bool window_is_crossing = false;              // dragged right-to-left
  bool hovered = false;
  std::optional<PickResult> hover_pick;         // live cursor position on CPlane
  ObjectId hover_object = kNoObject;
};

class Viewport {
 public:
  Viewport(const std::string& name, const std::string& standard_view);

  const std::string& Name() const { return name_; }
  void SetName(const std::string& n) { name_ = n; }
  Camera& GetCamera() { return camera_; }
  const Camera& GetCamera() const { return camera_; }
  DisplayMode Mode() const { return mode_; }
  void SetMode(DisplayMode m) { mode_ = m; }
  ConstructionPlane& CPlane() { return cplane_; }
  bool IsActive() const { return active_; }
  void SetActive(bool a) { active_ = a; }
  bool Maximized() const { return maximized_; }
  void SetMaximized(bool m) { maximized_ = m; }
  bool Visible() const { return visible_; }
  void SetVisible(bool v) { visible_ = v; }
  void SetStandardView(const std::string& view);  // Top/Bottom/Front/Back/Right/Left/Perspective/Isometric
  std::string StandardView() const { return standard_view_; }
  int Width() const { return width_; }
  int Height() const { return height_; }
  double Aspect() const { return height_ > 0 ? static_cast<double>(width_) / height_ : 1.0; }

  // Renders the scene into this viewport's framebuffer. `preview_lines`
  // and `preview_points` are transient command feedback (rubber-band lines,
  // dynamic preview) drawn on top.
  struct FrameContext {
    const Document* doc = nullptr;
    const std::vector<float>* preview_lines = nullptr;
    const std::vector<float>* preview_points = nullptr;
    std::optional<kernel::Point3d> cursor_marker;
    bool show_control_points_for_selected = false;
    double curve_tolerance = 0.02;
    double surface_tolerance = 0.05;
  };
  void Render(GlRenderer& renderer, const FrameContext& ctx);

  // Draws the ImGui window that shows this viewport and handles its input.
  // Returns the events that occurred. `want_point` / `want_objects` tell
  // the viewport what the active command is asking for so hover feedback
  // and cursor snapping behave accordingly.
  ViewportEvents DrawUI(const Document& doc, const SnapSettings& snaps, bool want_point,
                        bool want_objects, std::optional<kernel::Point3d> ortho_base,
                        double grid_spacing, bool& request_focus_command_line);

  // Hit tests against the document's display geometry. Returns the
  // closest object within `pixel_radius` of the given pixel position.
  ObjectId PickObject(const Document& doc, double px, double py, double pixel_radius = 6.0) const;
  std::vector<ObjectId> ObjectsInWindow(const Document& doc, double x0, double y0, double x1, double y1,
                                        bool crossing) const;

  // Converts a pixel position to a world point on the CPlane, applying snaps.
  PickResult PickPoint(const Document& doc, const SnapSettings& snaps, double px, double py,
                       std::optional<kernel::Point3d> ortho_base, double grid_spacing,
                       bool want_point) const;

  // World -> pixel projection in this viewport's current image rectangle.
  bool WorldToPixel(kernel::Point3d p, double& px, double& py) const;
  Ray PixelRay(double px, double py) const;

  void ZoomExtents(const Document& doc, bool selected_only);
  void ZoomTo(const kernel::BoundingBox& box);

 private:
  void DrawGrid(GlRenderer& renderer, const DocumentSettings& settings);
  void DrawObjects(GlRenderer& renderer, const FrameContext& ctx);
  void DrawAxesGizmo(GlRenderer& renderer);

  std::string name_;
  std::string standard_view_;
  Camera camera_;
  RenderTarget target_;
  DisplayMode mode_ = DisplayMode::Wireframe;
  ConstructionPlane cplane_;
  bool active_ = false;
  bool maximized_ = false;
  bool visible_ = true;
  int width_ = 1, height_ = 1;
  // Image rectangle in screen pixels, updated every DrawUI.
  double img_x_ = 0, img_y_ = 0;
  // Drag state.
  bool dragging_ = false;
  int drag_button_ = -1;
  double drag_start_x_ = 0, drag_start_y_ = 0;
  double last_x_ = 0, last_y_ = 0;
  bool drag_moved_ = false;
  double last_click_time_ = -10.0;
};

}  // namespace dino8::app
