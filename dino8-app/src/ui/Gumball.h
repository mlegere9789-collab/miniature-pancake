// The Gumball: an on-object widget for dragging the selection along the
// world axes or freely in the view plane, like Rhino's. Drawn with ImGui on
// top of a viewport image; drags snapshot the document once (undoable).
#pragma once

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

 private:
  enum class Handle { None, X, Y, Z, Free };
  Handle hover_ = Handle::None;
  bool dragging_ = false;
  Handle drag_handle_ = Handle::None;
  kernel::Point3d center_{0, 0, 0};
  double axis_len_ = 1.0;
  double start_param_ = 0.0;
  kernel::Point3d start_free_{0, 0, 0};
  std::vector<std::pair<ObjectId, SceneObject>> originals_;
  int drag_viewport_ = -1;
};

}  // namespace dino8::app
