// DirectionArrows: draws the clickable direction-arrow glyph the Dir
// command turns on for a curve/surface (SceneObject::show_direction_arrow)
// and dispatches a click on one to the exact same reversal Flip performs
// (FlipObject(), cmd_common.h) - see Dir's own registration in
// cmd_edit.cpp for the location each glyph is drawn at
// (ComputeDirArrow(), also cmd_common.h): a curve's start point along its
// tangent, or a surface's domain-centre point along its normal.
//
// Unlike Gumball/MappingGizmo this widget never drags anything, so it only
// needs a hover state and a single-click dispatch, not a drag-start
// snapshot; it reuses Gumball.cpp's own screen-space distance helper
// (DistToSegmentPx, exported in Gumball.h) instead of a third copy of that
// math.
#pragma once

#include "doc/SceneObject.h"

namespace dino8::app {

class Application;
class Viewport;

class DirectionArrows {
 public:
  // Draws a glyph for every object in the document with
  // show_direction_arrow set and handles a click on one. Returns true
  // while the mouse is hovering a glyph, so the caller can keep Gumball/
  // MappingGizmo from also claiming that same click underneath it (same
  // convention as Gumball::Update's/MappingGizmo::Update's return value).
  bool Update(Application& app, Viewport& vp, bool viewport_hovered);

 private:
  ObjectId hover_ = kNoObject;
};

}  // namespace dino8::app
