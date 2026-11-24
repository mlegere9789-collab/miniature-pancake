// MappingGizmo: an on-object 3D widget for dragging a selected object's own
// texture-mapping reference plane (SceneObject::custom_mapping_origin/x/y/
// size - the exact fields ApplyCustomMapping's picked origin/X-axis-point
// already fill in non-interactively) in place, instead of the object
// itself.
//
// This is the MappingWidget command's real gizmo (see cmd_render.cpp):
// previously "MappingWidget" could only print a note that no such widget
// existed and point at ApplyCustomMapping's typed Scale=/picked-plane
// options as the non-interactive equivalent. Rather than write a second,
// independent drag-and-hit-test implementation, it reuses ui/Gumball.cpp's
// own ray/plane math (RayPlane, ClosestParamOnLine, DistToSegmentPx,
// exported in Gumball.h) and mirrors its screen-space handle layout
// (Free + 3 translate axes, 3 rotation rings, 3 scale handles) - the two
// widgets differ only in what a drag writes back: Gumball transforms the
// selected SceneObjects, this one transforms one object's mapping frame.
//
// Honest simplification: SceneObject only stores a single scalar mapping
// size (custom_mapping_size) and per-object tiling scale (mapping_scale),
// not two independent U/V axis lengths, so all three scale handles behave
// identically (uniform scale of custom_mapping_size) rather than offering
// independent per-axis scale the way Gumball's ScaleX/Y/Z do for a whole
// object's XYZ dimensions.
#pragma once

#include "doc/SceneObject.h"

namespace dino8::app {

class Application;
class Viewport;

class MappingGizmo {
 public:
  // Draws/handles the gizmo for `obj`'s mapping frame in `vp`. If the
  // object has no custom mapping frame yet, seeds one from its current
  // bounding box the first time it is drawn (the same bounding-box
  // fallback Planar/Box mapping already use), so there is always a
  // sensible frame to grab - dragging it only switches the object's
  // `mapping` to TextureMapping::Custom once a drag actually finishes.
  // Returns true while the gizmo owns the mouse (dragging or hovered),
  // exactly like Gumball::Update, so the viewport can avoid starting its
  // own click underneath it.
  bool Update(Application& app, Viewport& vp, SceneObject& obj, bool viewport_hovered);
  bool Dragging() const { return dragging_; }

 private:
  enum class Handle { None, X, Y, Z, Free, RotX, RotY, RotZ, ScaleX, ScaleY, ScaleZ };
  Handle hover_ = Handle::None;
  bool dragging_ = false;
  Handle drag_handle_ = Handle::None;
  int drag_viewport_ = -1;
  ObjectId drag_object_ = kNoObject;

  // Drag-start snapshot of the frame being edited (so every frame of the
  // drag computes an absolute new value from the same start, exactly like
  // Gumball's own originals_ snapshot - no per-frame drift).
  kernel::Point3d start_origin_{0, 0, 0};
  kernel::Vector3d start_x_{1, 0, 0};
  kernel::Vector3d start_y_{0, 1, 0};
  double start_size_ = 1.0;

  // Per-handle drag seeds (ray-hit parameter/angle at mouse-down).
  kernel::Point3d start_free_{0, 0, 0};
  double start_param_ = 0.0;
  double start_angle_ = 0.0;
};

}  // namespace dino8::app
