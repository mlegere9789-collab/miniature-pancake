#include "flow/FlowEditor.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "app/Application.h"
#include "flow/FlowPreview.h"
#include "imgui_internal.h"
#include "ui/Panels.h"

namespace dino8::flow {

namespace {

constexpr float kPortRadius = 5.5f;
constexpr float kNodeHeaderH = 22.0f;
constexpr float kPortRowH = 18.0f;
constexpr float kNodeRounding = 6.0f;

ImU32 Col(float r, float g, float b, float a = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a)); }

ImU32 PortColor(Kind k) {
  float c[4];
  KindColor(k, c);
  return Col(c[0], c[1], c[2], 1.0f);
}

// Fuzzy subsequence match: every character of `needle` appears in order in
// `hay` (case-insensitive). Good enough for a node-search popup.
bool FuzzyMatch(const std::string& needle, const std::string& hay) {
  if (needle.empty()) return true;
  size_t hi = 0;
  for (char nc : needle) {
    const char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(nc)));
    bool found = false;
    while (hi < hay.size()) {
      if (std::tolower(static_cast<unsigned char>(hay[hi])) == lc) { found = true; ++hi; break; }
      ++hi;
    }
    if (!found) return false;
  }
  return true;
}

float NodeWidth(const Node& n) {
  if (n.def && n.def->body_width > 0) return n.def->body_width;
  float w = 140.0f;
  for (const Port& p : n.inputs) w = std::max(w, 24.0f + ImGui::CalcTextSize(p.name.c_str()).x * 2 + 60.0f);
  return std::clamp(w, 130.0f, 220.0f);
}

float NodeBodyExtra(const Node& n) {
  if (!n.def) return 0;
  switch (n.def->special) {
    case NodeDef::Special::Slider: return 34.0f;
    case NodeDef::Special::Panel: return 60.0f;
    case NodeDef::Special::Toggle: return 6.0f;
    case NodeDef::Special::Colour: return 30.0f;
    case NodeDef::Special::Reference: return 26.0f;
    case NodeDef::Special::Expression: return 26.0f;
    default: return 0;
  }
}

float NodeHeight(const Node& n) {
  const size_t rows = std::max(n.inputs.size(), n.outputs.size());
  return kNodeHeaderH + static_cast<float>(rows) * kPortRowH + NodeBodyExtra(n) + 10.0f;
}

}  // namespace

Editor& Editor::Get() {
  static Editor e;
  return e;
}

ImVec2 Editor::ToScreen(ImVec2 p) const { return canvas_origin_ + (p + pan_) * zoom_; }
ImVec2 Editor::ToCanvas(ImVec2 p) const { return (p - canvas_origin_) * (1.0f / zoom_) - pan_; }

void Editor::DrawToolbar(app::Application& app) {
  if (ImGui::Button(graph.solver_enabled ? "Solver: On" : "Solver: Off")) graph.solver_enabled = !graph.solver_enabled;
  ImGui::SameLine();
  if (ImGui::Button("Solve Now")) { graph.MarkAllDirty(); graph.Solve(&app.Doc()); }
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  if (ImGui::Button("New")) { graph.Snapshot(); graph.Clear(); }
  ImGui::SameLine();
  if (ImGui::Button("Open...")) {
    app.ShowFileDialog("Open Dino Flow graph", {".dflow"}, false, [this](const std::string& path) {
      std::string err;
      if (!graph.LoadFile(path, err)) { /* reported via app.Notify from caller */ }
    });
  }
  ImGui::SameLine();
  if (ImGui::Button("Save")) {
    if (graph.path.empty()) {
      app.ShowFileDialog("Save Dino Flow graph", {".dflow"}, true, [this](const std::string& path) { std::string err; graph.SaveFile(path, err); graph.path = path; });
    } else {
      std::string err;
      graph.SaveFile(graph.path, err);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Save As...")) {
    app.ShowFileDialog("Save Dino Flow graph As", {".dflow"}, true, [this](const std::string& path) { std::string err; graph.SaveFile(path, err); graph.path = path; });
  }
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  if (ImGui::Button("Bake")) {
    const int n = BakeGraph(graph, app.Doc());
    app.Notify("Dino Flow: baked " + std::to_string(n) + " object(s)");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::Checkbox("Timing", &show_timing);
  if (show_timing) {
    ImGui::SameLine();
    ImGui::Text("%.2f ms / %d node(s)", graph.last_stats.total_ms, graph.last_stats.solved_nodes);
    if (graph.last_stats.errors > 0) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1), "%d error(s)", graph.last_stats.errors); }
  }
  if (!graph.path.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", graph.path.c_str()); }
}

void Editor::Draw(app::Application& app) {
  if (!open) return;
  ImGui::SetNextWindowSize(ImVec2(1100, 720), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Dino Flow", &open)) { ImGui::End(); return; }

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) graph.Undo();
    if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) graph.Redo();
  }

  DrawToolbar(app);
  ImGui::Separator();
  DrawCanvas(app);

  if (graph.solver_enabled && graph.AnyDirty()) graph.Solve(&app.Doc());

  ImGui::End();
}

void Editor::DrawCanvas(app::Application& app) {
  ImGui::BeginChild("dflow_canvas", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  canvas_origin_ = ImGui::GetCursorScreenPos();
  const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
  dl->AddRectFilled(canvas_origin_, canvas_origin_ + canvas_size, Col(0.098f, 0.106f, 0.122f));

  // Dot grid.
  const float grid = 24.0f * zoom_;
  if (grid > 4.0f) {
    const ImVec2 off = ImVec2(std::fmod(pan_.x * zoom_, grid), std::fmod(pan_.y * zoom_, grid));
    for (float x = off.x; x < canvas_size.x; x += grid)
      for (float y = off.y; y < canvas_size.y; y += grid)
        dl->AddCircleFilled(canvas_origin_ + ImVec2(x, y), 1.1f, Col(1, 1, 1, 0.06f));
  }

  ImGui::InvisibleButton("dflow_canvas_bg", canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  canvas_hovered_ = ImGui::IsItemHovered();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const ImVec2 mouse_canvas = ToCanvas(mouse);

  // Pan (middle-drag or right-drag on empty canvas) and zoom.
  if (canvas_hovered_) {
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) pan_ = pan_ + ImGui::GetIO().MouseDelta * (1.0f / zoom_);
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0) {
      const float old_zoom = zoom_;
      zoom_ = std::clamp(zoom_ * (1.0f + wheel * 0.1f), 0.25f, 2.5f);
      // Zoom toward the cursor.
      pan_ = pan_ - (mouse - canvas_origin_) * (1.0f / old_zoom - 1.0f / zoom_);
    }
  }

  // Wires (drawn first, under nodes).
  auto port_screen_pos = [&](Node* n, int idx, bool output) -> ImVec2 {
    const float w = NodeWidth(*n);
    const float y = kNodeHeaderH + kPortRowH * static_cast<float>(idx) + kPortRowH * 0.5f;
    return ToScreen(ImVec2(n->x + (output ? w : 0), n->y + y));
  };
  auto draw_wire = [&](ImVec2 a, ImVec2 b, ImU32 color) {
    const float dx = std::max(40.0f, std::fabs(b.x - a.x) * 0.5f);
    dl->AddBezierCubic(a, a + ImVec2(dx, 0), b - ImVec2(dx, 0), b, color, 2.2f * zoom_);
  };
  for (const Wire& w : graph.Wires()) {
    Node* fn = graph.Find(w.from);
    Node* tn = graph.Find(w.to);
    if (!fn || !tn) continue;
    const ImU32 color = PortColor(fn->outputs[static_cast<size_t>(w.from_port)].kind);
    draw_wire(port_screen_pos(fn, w.from_port, true), port_screen_pos(tn, w.to_port, false), color);
  }
  if (wiring_from_node_ != kNoNode) {
    if (Node* n = graph.Find(wiring_from_node_)) {
      const ImVec2 a = port_screen_pos(n, wiring_from_port_, wiring_is_output_);
      const ImU32 color = PortColor(wiring_is_output_ ? n->outputs[static_cast<size_t>(wiring_from_port_)].kind : n->inputs[static_cast<size_t>(wiring_from_port_)].kind);
      if (wiring_is_output_) draw_wire(a, mouse, color); else draw_wire(mouse, a, color);
    }
  }

  // Nodes.
  dragged_this_frame_ = false;
  NodeId hovered_output_port_node = kNoNode; int hovered_output_port = -1;
  NodeId hovered_input_port_node = kNoNode; int hovered_input_port = -1;
  for (auto& np : graph.Nodes()) {
    Node& n = *np;
    const float w = NodeWidth(n);
    const float h = NodeHeight(n);
    const ImVec2 p0 = ToScreen(ImVec2(n.x, n.y));
    const ImVec2 p1 = p0 + ImVec2(w, h) * zoom_;
    const bool node_hovered = canvas_hovered_ && mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y && mouse.y <= p1.y;

    // Ports: hit test + draw.
    for (size_t i = 0; i < n.inputs.size(); ++i) {
      const ImVec2 pp = port_screen_pos(&n, static_cast<int>(i), false);
      const bool hit = canvas_hovered_ && (mouse.x - pp.x) * (mouse.x - pp.x) + (mouse.y - pp.y) * (mouse.y - pp.y) < (kPortRadius * 2) * (kPortRadius * 2);
      if (hit) { hovered_input_port_node = n.id; hovered_input_port = static_cast<int>(i); }
      dl->AddCircleFilled(pp, kPortRadius * zoom_, PortColor(n.inputs[i].kind));
      dl->AddCircle(pp, kPortRadius * zoom_, Col(0, 0, 0, 0.5f));
    }
    for (size_t i = 0; i < n.outputs.size(); ++i) {
      const ImVec2 pp = port_screen_pos(&n, static_cast<int>(i), true);
      const bool hit = canvas_hovered_ && (mouse.x - pp.x) * (mouse.x - pp.x) + (mouse.y - pp.y) * (mouse.y - pp.y) < (kPortRadius * 2) * (kPortRadius * 2);
      if (hit) { hovered_output_port_node = n.id; hovered_output_port = static_cast<int>(i); }
      dl->AddCircleFilled(pp, kPortRadius * zoom_, PortColor(n.outputs[i].kind));
      dl->AddCircle(pp, kPortRadius * zoom_, Col(0, 0, 0, 0.5f));
    }

    if (hovered_input_port_node == kNoNode && hovered_output_port_node == kNoNode && node_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      if (!ImGui::GetIO().KeyShift) for (auto& other : graph.Nodes()) other->selected = false;
      n.selected = true;
      dragging_node_ = n.id;
      drag_offset_ = mouse_canvas - ImVec2(n.x, n.y);
    }

    DrawNode(app, n, dl, p0);
    (void)h;
  }

  // Port click handling: start / finish a wire.
  if (canvas_hovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (hovered_output_port_node != kNoNode) { wiring_from_node_ = hovered_output_port_node; wiring_from_port_ = hovered_output_port; wiring_is_output_ = true; dragging_node_ = kNoNode; }
    else if (hovered_input_port_node != kNoNode) {
      // Dragging out of an already-wired input detaches it.
      if (const Wire* w = graph.WireInto(hovered_input_port_node, hovered_input_port)) {
        graph.Snapshot();
        wiring_from_node_ = w->from; wiring_from_port_ = w->from_port; wiring_is_output_ = true;
        graph.Disconnect(hovered_input_port_node, hovered_input_port);
      } else {
        wiring_from_node_ = hovered_input_port_node; wiring_from_port_ = hovered_input_port; wiring_is_output_ = false;
      }
      dragging_node_ = kNoNode;
    }
  }
  if (wiring_from_node_ != kNoNode && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    bool connected = false;
    if (wiring_is_output_ && hovered_input_port_node != kNoNode) {
      graph.Snapshot();
      connected = graph.Connect(wiring_from_node_, wiring_from_port_, hovered_input_port_node, hovered_input_port);
    } else if (!wiring_is_output_ && hovered_output_port_node != kNoNode) {
      graph.Snapshot();
      connected = graph.Connect(hovered_output_port_node, hovered_output_port, wiring_from_node_, wiring_from_port_);
    }
    if (!connected && canvas_hovered_) {
      // Dropped on empty canvas: open the search popup pre-wired to this port.
      search_open_ = true;
      search_canvas_pos_ = mouse_canvas;
      search_text_.clear();
      search_link_node_ = wiring_from_node_;
      search_link_port_ = wiring_from_port_;
      search_link_is_output_ = wiring_is_output_;
    }
    wiring_from_node_ = kNoNode;
  }

  // Node dragging.
  if (dragging_node_ != kNoNode) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      if (Node* n = graph.Find(dragging_node_)) {
        const ImVec2 np = mouse_canvas - drag_offset_;
        if (std::fabs(n->x - np.x) > 0.01f || std::fabs(n->y - np.y) > 0.01f) dragged_this_frame_ = true;
        n->x = np.x; n->y = np.y;
      }
    } else {
      dragging_node_ = kNoNode;
    }
  }

  // Box select on empty canvas.
  if (canvas_hovered_ && dragging_node_ == kNoNode && wiring_from_node_ == kNoNode && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
      hovered_input_port_node == kNoNode && hovered_output_port_node == kNoNode) {
    bool over_any_node = false;
    for (auto& np : graph.Nodes()) {
      const ImVec2 p0 = ToScreen(ImVec2(np->x, np->y));
      const ImVec2 p1 = p0 + ImVec2(NodeWidth(*np), NodeHeight(*np)) * zoom_;
      if (mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y && mouse.y <= p1.y) { over_any_node = true; break; }
    }
    if (!over_any_node) { box_selecting_ = true; box_start_ = mouse; if (!ImGui::GetIO().KeyShift) for (auto& n : graph.Nodes()) n->selected = false; }
  }
  if (box_selecting_) {
    dl->AddRect(box_start_, mouse, Col(0.4f, 0.7f, 1.0f, 0.9f));
    dl->AddRectFilled(box_start_, mouse, Col(0.4f, 0.7f, 1.0f, 0.12f));
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      const ImVec2 lo(std::min(box_start_.x, mouse.x), std::min(box_start_.y, mouse.y));
      const ImVec2 hi(std::max(box_start_.x, mouse.x), std::max(box_start_.y, mouse.y));
      for (auto& np : graph.Nodes()) {
        const ImVec2 p0 = ToScreen(ImVec2(np->x, np->y));
        const ImVec2 p1 = p0 + ImVec2(NodeWidth(*np), NodeHeight(*np)) * zoom_;
        if (p0.x <= hi.x && p1.x >= lo.x && p0.y <= hi.y && p1.y >= lo.y) np->selected = true;
      }
      box_selecting_ = false;
    }
  }

  // Double-click / right-click empty canvas: node search.
  if (canvas_hovered_ && (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
    bool over_node = false;
    for (auto& np : graph.Nodes()) {
      const ImVec2 p0 = ToScreen(ImVec2(np->x, np->y));
      const ImVec2 p1 = p0 + ImVec2(NodeWidth(*np), NodeHeight(*np)) * zoom_;
      if (mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y && mouse.y <= p1.y) over_node = true;
    }
    if (!over_node) { search_open_ = true; search_canvas_pos_ = mouse_canvas; search_text_.clear(); search_link_node_ = kNoNode; }
  }

  // Delete / copy / paste while the canvas has focus.
  if (canvas_hovered_ || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
      std::vector<NodeId> dead;
      for (auto& n : graph.Nodes()) if (n->selected) dead.push_back(n->id);
      if (!dead.empty()) { graph.Snapshot(); for (NodeId id : dead) graph.Remove(id); }
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
      std::vector<NodeId> sel;
      for (auto& n : graph.Nodes()) if (n->selected) sel.push_back(n->id);
      if (!sel.empty()) clipboard_ = graph.ToJson(&sel);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !clipboard_.empty()) {
      graph.Snapshot();
      std::string err;
      for (auto& n : graph.Nodes()) n->selected = false;
      std::vector<NodeId> created;
      graph.FromJson(clipboard_, err, true, 24, 24, &created);
      for (NodeId id : created) if (Node* n = graph.Find(id)) n->selected = true;
    }
  }

  DrawSearchPopup(app);
  ImGui::EndChild();
}

void Editor::DrawSearchPopup(app::Application& app) {
  if (!search_open_) return;
  ImGui::SetNextWindowPos(ToScreen(search_canvas_pos_), ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(320, 360), ImGuiCond_Appearing);
  ImGui::OpenPopup("dflow_search");
  bool open = true;
  if (ImGui::BeginPopupModal("dflow_search", &open, ImGuiWindowFlags_NoTitleBar)) {
    static bool focus_once = true;
    char buf[128];
    std::snprintf(buf, sizeof buf, "%s", search_text_.c_str());
    ImGui::SetNextItemWidth(-1);
    if (focus_once) { ImGui::SetKeyboardFocusHere(); focus_once = false; }
    if (ImGui::InputTextWithHint("##search", "Search nodes... (Esc to cancel)", buf, sizeof buf)) search_text_ = buf;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { search_open_ = false; focus_once = true; ImGui::CloseCurrentPopup(); }
    ImGui::Separator();
    ImGui::BeginChild("dflow_search_list", ImVec2(0, 0));
    std::string current_cat;
    for (const NodeDef& def : Registry::Get().All()) {
      if (!FuzzyMatch(search_text_, def.name) && !FuzzyMatch(search_text_, def.category)) continue;
      if (def.category != current_cat) { ImGui::TextDisabled("%s", def.category.c_str()); current_cat = def.category; }
      if (ImGui::Selectable((def.name + "##" + def.category).c_str())) {
        graph.Snapshot();
        Node* n = graph.Add(def.name, search_canvas_pos_.x, search_canvas_pos_.y);
        if (n && search_link_node_ != kNoNode) {
          if (search_link_is_output_) graph.Connect(search_link_node_, search_link_port_, n->id, 0);
          else if (!n->outputs.empty()) graph.Connect(n->id, 0, search_link_node_, search_link_port_);
        }
        search_open_ = false;
        focus_once = true;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
  } else {
    search_open_ = false;
  }
  (void)app;
}

void Editor::DrawNode(app::Application& app, Node& n, ImDrawList* dl, ImVec2 origin) {
  const float w = NodeWidth(n) * zoom_;
  const float h = NodeHeight(n) * zoom_;
  const ImU32 body = n.error.empty() ? (n.warning.empty() ? Col(0.16f, 0.17f, 0.20f) : Col(0.22f, 0.19f, 0.10f)) : Col(0.24f, 0.11f, 0.11f);
  const ImU32 header = n.selected ? Col(0.30f, 0.46f, 0.72f) : Col(0.24f, 0.25f, 0.29f);
  dl->AddRectFilled(origin, origin + ImVec2(w, h), body, kNodeRounding * zoom_);
  dl->AddRectFilled(origin, origin + ImVec2(w, kNodeHeaderH * zoom_), header, kNodeRounding * zoom_, ImDrawFlags_RoundCornersTop);
  if (n.selected) dl->AddRect(origin, origin + ImVec2(w, h), Col(0.55f, 0.78f, 1.0f, 0.9f), kNodeRounding * zoom_, 0, 2.0f);
  else dl->AddRect(origin, origin + ImVec2(w, h), Col(0, 0, 0, 0.5f), kNodeRounding * zoom_);
  if (!n.enabled) dl->AddRectFilled(origin, origin + ImVec2(w, h), Col(0, 0, 0, 0.35f), kNodeRounding * zoom_);

  const std::string title = n.def ? (n.def->nick.empty() ? n.def->name : n.def->name) : n.type;
  dl->AddText(origin + ImVec2(8 * zoom_, 3 * zoom_), Col(0.95f, 0.95f, 0.97f), title.c_str());

  for (size_t i = 0; i < n.inputs.size(); ++i) {
    const float y = kNodeHeaderH + kPortRowH * static_cast<float>(i) + kPortRowH * 0.5f;
    dl->AddText(origin + ImVec2(10 * zoom_, (y - 7) * zoom_), Col(0.82f, 0.83f, 0.86f), n.inputs[i].name.c_str());
  }
  for (size_t i = 0; i < n.outputs.size(); ++i) {
    const float y = kNodeHeaderH + kPortRowH * static_cast<float>(i) + kPortRowH * 0.5f;
    const ImVec2 sz = ImGui::CalcTextSize(n.outputs[i].name.c_str());
    dl->AddText(origin + ImVec2(w - 10 * zoom_ - sz.x * zoom_, (y - 7) * zoom_), Col(0.82f, 0.83f, 0.86f), n.outputs[i].name.c_str());
  }

  // Node-specific interactive widgets, drawn as real ImGui items positioned
  // over the node body (so sliders/inputs are genuinely usable).
  if (!n.def) return;
  const float body_y = kNodeHeaderH + kPortRowH * static_cast<float>(std::max(n.inputs.size(), n.outputs.size()));
  ImGui::SetCursorScreenPos(origin + ImVec2(8 * zoom_, (body_y + 4) * zoom_));
  ImGui::PushID(static_cast<int>(n.id));
  ImGui::PushItemWidth((NodeWidth(n) - 16) * zoom_);
  bool changed = false;
  switch (n.def->special) {
    case NodeDef::Special::Slider: {
      if (n.slider_integer) { int v = static_cast<int>(std::llround(n.slider_value)); if (ImGui::SliderInt("##v", &v, static_cast<int>(n.slider_min), static_cast<int>(n.slider_max))) { n.slider_value = v; changed = true; } }
      else if (ImGui::SliderScalar("##v", ImGuiDataType_Double, &n.slider_value, &n.slider_min, &n.slider_max, "%.3f")) changed = true;
      break;
    }
    case NodeDef::Special::Panel: {
      char buf[256];
      std::snprintf(buf, sizeof buf, "%s", n.text.c_str());
      if (ImGui::InputTextMultiline("##t", buf, sizeof buf, ImVec2(-1, 40 * zoom_))) { n.text = buf; changed = true; }
      break;
    }
    case NodeDef::Special::Toggle:
      if (ImGui::Checkbox(n.toggle ? "True" : "False", &n.toggle)) changed = true;
      break;
    case NodeDef::Special::Colour: {
      float col[3] = {n.colour.r, n.colour.g, n.colour.b};
      if (ImGui::ColorEdit3("##c", col, ImGuiColorEditFlags_NoInputs)) { n.colour = Colour{col[0], col[1], col[2], 1.f}; changed = true; }
      break;
    }
    case NodeDef::Special::Reference: {
      std::string label = n.refs.empty() ? "(none)" : ("#" + std::to_string(n.refs.front()));
      if (ImGui::Button(("Pick " + label + "##ref").c_str())) {
        std::vector<app::ObjectId> sel = app.Doc().SelectedIds();
        if (!sel.empty()) { n.refs = {sel.front()}; changed = true; }
      }
      break;
    }
    case NodeDef::Special::Expression: {
      char buf[256];
      std::snprintf(buf, sizeof buf, "%s", n.text.c_str());
      if (ImGui::InputText("##expr", buf, sizeof buf)) { n.text = buf; changed = true; }
      break;
    }
    default: break;
  }
  ImGui::PopItemWidth();
  ImGui::PopID();
  if (changed) graph.MarkDirty(n.id);

  // Reference / Expression evaluation is bridged here since it needs the
  // Application/Document the generic Evaluator signature doesn't carry.
  if (n.def->special == NodeDef::Special::Reference && n.dirty) {
    if (!n.refs.empty()) {
      if (const app::SceneObject* o = app.Doc().Find(n.refs.front())) {
        if (o->kind == app::ObjectKind::Point) n.outputs[0].data = Tree::Single(Value::Point(o->point));
        else if (o->kind == app::ObjectKind::Curve && o->curve) n.outputs[0].data = Tree::Single(Value::Curve(*o->curve));
        else if (o->kind == app::ObjectKind::Surface && o->surface) n.outputs[0].data = Tree::Single(Value::Surface(*o->surface));
        else if (o->kind == app::ObjectKind::Brep && o->brep) n.outputs[0].data = Tree::Single(Value::BrepV(*o->brep));
        else if (o->kind == app::ObjectKind::Mesh && o->mesh) n.outputs[0].data = Tree::Single(Value::MeshV(*o->mesh));
      }
    }
  }
  if (n.def->special == NodeDef::Special::Expression && n.dirty) {
    std::string expr = n.text;
    double x, y, z;
    x = n.inputs.size() > 0 && n.inputs[0].has_user_value ? n.inputs[0].user_value.num : 0;
    y = n.inputs.size() > 1 && n.inputs[1].has_user_value ? n.inputs[1].user_value.num : 0;
    z = n.inputs.size() > 2 && n.inputs[2].has_user_value ? n.inputs[2].user_value.num : 0;
    // Substitute x/y/z textually, then reuse the app's own arithmetic evaluator.
    auto subst = [](std::string s, char v, double val) {
      const std::string rep = std::to_string(val);
      std::string out;
      for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == v && (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1]))) && (i + 1 == s.size() || !std::isalnum(static_cast<unsigned char>(s[i + 1])))) out += rep;
        else out += s[i];
      }
      return out;
    };
    expr = subst(expr, 'x', x);
    expr = subst(expr, 'y', y);
    expr = subst(expr, 'z', z);
    double result = 0;
    std::string err;
    if (!expr.empty() && app::EvaluateExpression(expr, result, err)) n.outputs[0].data = Tree::Single(Value::Number(result));
  }
}

bool Editor::RunHeadless(app::Application& app, const std::string& file, bool bake, std::string& error) {
  graph.Clear();
  if (!graph.LoadFile(file, error)) return false;
  graph.MarkAllDirty();
  graph.Solve(&app.Doc());
  if (bake) BakeGraph(graph, app.Doc());
  return true;
}

}  // namespace dino8::flow
