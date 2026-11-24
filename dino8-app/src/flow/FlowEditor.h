// The Dino Flow node editor panel: canvas (pan/zoom), nodes with ports and
// bezier wires, box select, delete/copy/paste, a fuzzy node-search popup,
// and per-node widgets (sliders, panels, toggles, colour swatches,
// reference pickers, the expression editor).
#pragma once

#include <string>

#include "flow/FlowGraph.h"
#include "imgui.h"

namespace dino8::app { class Application; }

namespace dino8::flow {

class Editor {
 public:
  static Editor& Get();

  Graph graph;
  bool open = false;
  bool show_timing = true;

  void Draw(app::Application& app);
  // GrasshopperPlayer: loads, solves and (optionally) bakes without any UI.
  bool RunHeadless(app::Application& app, const std::string& file, bool bake, std::string& error);

 private:
  void DrawCanvas(app::Application& app);
  void DrawNode(app::Application& app, Node& n, ImDrawList* dl, ImVec2 origin);
  void DrawSearchPopup(app::Application& app);
  void DrawToolbar(app::Application& app);
  ImVec2 ToScreen(ImVec2 canvas_pos) const;
  ImVec2 ToCanvas(ImVec2 screen_pos) const;

  ImVec2 pan_{0, 0};
  float zoom_ = 1.0f;
  NodeId dragging_node_ = kNoNode;
  ImVec2 drag_offset_{0, 0};
  bool box_selecting_ = false;
  ImVec2 box_start_{0, 0};
  // Wire being dragged from an output (or into an input): kNoNode when idle.
  NodeId wiring_from_node_ = kNoNode;
  int wiring_from_port_ = -1;
  bool wiring_is_output_ = true;
  bool search_open_ = false;
  ImVec2 search_canvas_pos_{0, 0};
  std::string search_text_;
  NodeId search_link_node_ = kNoNode;  // node the newly created node should auto-wire from/to
  int search_link_port_ = -1;
  bool search_link_is_output_ = true;
  std::vector<std::string> clipboard_json_;
  std::string clipboard_;
  bool canvas_hovered_ = false;
  ImVec2 canvas_origin_{0, 0};
  bool dragged_this_frame_ = false;
  double last_solve_time_ = 0;
};

}  // namespace dino8::flow
