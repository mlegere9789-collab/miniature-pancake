// The Gumball: an on-object widget for dragging the selection along the
// world axes or freely in the view plane, like Rhino's. Drawn with ImGui on
// top of a viewport image; drags snapshot the document once (undoable).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "doc/SceneObject.h"

namespace dino8::app {

class Application;
class Viewport;

class Gumball {
 public:
  // Draws the widget for the current selection in `vp` and handles mouse
  // interaction. Returns true while it owns the mouse (drag in progress or
  // pointer over a handle), so the viewport must not start its own click.
  bool Update(Application& app, Viewport& vp, bool viewport_hovered);
  bool Dragging() const { return dragging_; }

  // Settings adjusted by the Gumball* commands. `alignment` is where the
  // widget's axes come from ("CPlane", "World", "Object"); relocation moves
  // the widget origin away from the selection's bounding-box centre.
  struct Settings {
    std::string alignment = "CPlane";
    bool auto_reset = true;          // GumballAutoReset: forget relocation after each drag
    bool dynamic_relocate = false;   // GumballDynamicRelocate: Ctrl-drag moves the widget
    bool relocated = false;          // GumballRelocate set a custom origin
    kernel::Point3d relocated_origin{0, 0, 0};
    std::string scale_mode = "Independent";  // GumballScaleMode: Independent / Uniform
    bool snap_to_grid = false;
    double axis_length = 0;          // 0 = automatic
  };
  Settings& GetSettings() { return settings_; }
  const Settings& GetSettings() const { return settings_; }

 private:
  enum class Handle { None, X, Y, Z, Free, RotX, RotY, RotZ, ScaleX, ScaleY, ScaleZ };
  Handle hover_ = Handle::None;
  bool dragging_ = false;
  Handle drag_handle_ = Handle::None;
  kernel::Point3d center_{0, 0, 0};
  double axis_len_ = 1.0;
  double start_param_ = 0.0;
  double start_angle_ = 0.0;
  ON_Xform last_xform_ = ON_Xform::IdentityTransformation;
  kernel::Point3d start_free_{0, 0, 0};
  std::vector<std::pair<ObjectId, SceneObject>> originals_;
  int drag_viewport_ = -1;
  Settings settings_;
};

}  // namespace dino8::app
