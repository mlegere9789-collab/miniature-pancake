// State shared between the view-tools commands (cmd_viewtools.cpp) and the
// application frame loop: animation playback/recording, CPlane history,
// AutoAlignCPlane / MPlane tracking and a few display toggles.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "doc/Document.h"
#include "viewport/Viewport.h"

namespace dino8::app {

class Application;

struct AnimationPlayback {
  int current = 0;         // next frame to show
  int applied = -1;        // frame currently shown by the viewport (-1: none)
  bool playing = false;
  bool recording = false;
  std::string record_dir;
  int recorded = 0;        // files written so far
  int loops = 0;
};

struct CPlaneHistory {
  std::vector<ConstructionPlane> undo, redo;
  std::optional<ConstructionPlane> last;  // CPlane seen at the end of the previous frame
};

struct ViewToolsState {
  AnimationPlayback playback;
  std::map<std::string, CPlaneHistory> cplane_history;  // by viewport name
  bool suppress_cplane_history = false;  // set while NextCPlane/PrevCPlane restore a plane
  bool auto_align_cplane = false;
  std::vector<ObjectId> last_selection;
  // MPlane: the active CPlane follows this object's bounding-box centre.
  ObjectId mplane_object = kNoObject;
  std::string mplane_viewport;
  kernel::Vector3d mplane_offset{0, 0, 0};
  bool print_display = false;
  bool show_zbuffer = false;
  double screen_pixels_per_mm = 96.0 / 25.4;  // Zoom1To1 / Zoom1To1Calibrate
};

// Called once per application frame before the viewports render: advances
// a playing/recording animation by one frame, captures the frame rendered
// last time, records CPlane changes for NextCPlane/PrevCPlane and applies
// AutoAlignCPlane / MPlane.
void ViewToolsFrame(Application& app);

}  // namespace dino8::app
