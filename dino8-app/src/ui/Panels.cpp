// Dockable panels and dialogs.
#include "ui/Panels.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>
#include <vector>

#include "app/Application.h"
#include "imgui.h"
#include "ui/Theme.h"

namespace dino8::app {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

bool ColorEdit(const char* label, Color& color) {
  float c[4] = {color.r, color.g, color.b, color.a};
  if (ImGui::ColorEdit4(label, c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview)) {
    color = Color{c[0], c[1], c[2], c[3]};
    return true;
  }
  return false;
}

namespace {

// Tiny recursive-descent expression evaluator for the calculator and for
// numeric input fields (Rhino accepts "2*3" wherever a number is asked for).
class Expr {
 public:
  explicit Expr(const std::string& s) : s_(s) {}
  bool Eval(double& out, std::string& err) {
    pos_ = 0;
    ok_ = true;
    out = ParseAdd();
    SkipWs();
    if (ok_ && pos_ != s_.size()) { ok_ = false; err_ = "unexpected '" + std::string(1, s_[pos_]) + "'"; }
    err = err_;
    return ok_;
  }

 private:
  void SkipWs() { while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_; }
  bool Peek(char c) { SkipWs(); return pos_ < s_.size() && s_[pos_] == c; }
  double ParseAdd() {
    double v = ParseMul();
    for (;;) {
      if (Peek('+')) { ++pos_; v += ParseMul(); }
      else if (Peek('-')) { ++pos_; v -= ParseMul(); }
      else return v;
    }
  }
  double ParseMul() {
    double v = ParsePow();
    for (;;) {
      if (Peek('*')) { ++pos_; v *= ParsePow(); }
      else if (Peek('/')) { ++pos_; double d = ParsePow(); if (d == 0) { ok_ = false; err_ = "division by zero"; return 0; } v /= d; }
      else if (Peek('%')) { ++pos_; v = std::fmod(v, ParsePow()); }
      else return v;
    }
  }
  double ParsePow() {
    double v = ParseUnary();
    if (Peek('^')) { ++pos_; v = std::pow(v, ParsePow()); }
    return v;
  }
  double ParseUnary() {
    if (Peek('-')) { ++pos_; return -ParseUnary(); }
    if (Peek('+')) { ++pos_; return ParseUnary(); }
    return ParsePrimary();
  }
  double ParsePrimary() {
    SkipWs();
    if (pos_ >= s_.size()) { ok_ = false; err_ = "unexpected end"; return 0; }
    if (s_[pos_] == '(') {
      ++pos_;
      double v = ParseAdd();
      if (!Peek(')')) { ok_ = false; err_ = "missing ')'"; return 0; }
      ++pos_;
      return v;
    }
    if (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.') {
      size_t start = pos_;
      while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E')) ++pos_;
      return std::atof(s_.substr(start, pos_ - start).c_str());
    }
    if (std::isalpha(static_cast<unsigned char>(s_[pos_]))) {
      size_t start = pos_;
      while (pos_ < s_.size() && std::isalnum(static_cast<unsigned char>(s_[pos_]))) ++pos_;
      std::string name = s_.substr(start, pos_ - start);
      for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (name == "pi") return ON_PI;
      if (name == "e") return 2.718281828459045;
      if (!Peek('(')) { ok_ = false; err_ = "unknown symbol " + name; return 0; }
      ++pos_;
      std::vector<double> args;
      if (!Peek(')')) {
        args.push_back(ParseAdd());
        while (Peek(',')) { ++pos_; args.push_back(ParseAdd()); }
      }
      if (!Peek(')')) { ok_ = false; err_ = "missing ')'"; return 0; }
      ++pos_;
      auto a0 = [&]() { return args.empty() ? 0.0 : args[0]; };
      if (name == "sqrt") return std::sqrt(a0());
      if (name == "sin") return std::sin(a0() * ON_PI / 180.0);
      if (name == "cos") return std::cos(a0() * ON_PI / 180.0);
      if (name == "tan") return std::tan(a0() * ON_PI / 180.0);
      if (name == "asin") return std::asin(a0()) * 180.0 / ON_PI;
      if (name == "acos") return std::acos(a0()) * 180.0 / ON_PI;
      if (name == "atan") return std::atan(a0()) * 180.0 / ON_PI;
      if (name == "abs") return std::fabs(a0());
      if (name == "ln") return std::log(a0());
      if (name == "log") return std::log10(a0());
      if (name == "exp") return std::exp(a0());
      if (name == "floor") return std::floor(a0());
      if (name == "ceil") return std::ceil(a0());
      if (name == "round") return std::round(a0());
      if (name == "min" && args.size() >= 2) return std::min(args[0], args[1]);
      if (name == "max" && args.size() >= 2) return std::max(args[0], args[1]);
      if (name == "pow" && args.size() >= 2) return std::pow(args[0], args[1]);
      if (name == "hypot" && args.size() >= 2) return std::hypot(args[0], args[1]);
      ok_ = false;
      err_ = "unknown function " + name;
      return 0;
    }
    ok_ = false;
    err_ = "unexpected '" + std::string(1, s_[pos_]) + "'";
    return 0;
  }

  std::string s_;
  size_t pos_ = 0;
  bool ok_ = true;
  std::string err_;
};

bool InputString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", value.c_str());
  if (ImGui::InputText(label, buf, sizeof(buf), flags)) {
    value = buf;
    return true;
  }
  return false;
}

const char* StatusName(CommandStatus s) { return CommandStatusName(s); }

ImVec4 StatusColor(CommandStatus s) {
  switch (s) {
    case CommandStatus::Implemented: return ImVec4(ThemeColors::kOk[0], ThemeColors::kOk[1], ThemeColors::kOk[2], 1);
    case CommandStatus::Partial: return ImVec4(ThemeColors::kWarn[0], ThemeColors::kWarn[1], ThemeColors::kWarn[2], 1);
    default: return ImVec4(ThemeColors::kMuted[0], ThemeColors::kMuted[1], ThemeColors::kMuted[2], 1);
  }
}

}  // namespace

bool EvaluateExpression(const std::string& text, double& out, std::string& error) {
  Expr e(text);
  return e.Eval(out, error);
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

void DrawLayersPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin("Layers", &app.Panels().layers)) { ImGui::End(); return; }
  static char new_name[128] = "";
  if (ImGui::Button("New Layer")) {
    doc.BeginChange("New layer");
    std::string name = std::strlen(new_name) ? new_name : "Layer " + std::to_string(doc.Layers().size() + 1);
    int idx = doc.AddLayer(name);
    doc.SetCurrentLayer(idx);
    new_name[0] = 0;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  ImGui::InputTextWithHint("##newlayer", "name", new_name, sizeof(new_name));
  ImGui::SameLine();
  if (ImGui::Button("Sublayer")) {
    doc.BeginChange("New sublayer");
    int idx = doc.AddLayer("Sublayer", Color::FromBytes(0, 0, 0), doc.CurrentLayer());
    doc.SetCurrentLayer(idx);
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete")) {
    doc.BeginChange("Delete layer");
    if (!doc.RemoveLayer(doc.CurrentLayer())) app.Notify("Layer is in use or is the only layer");
  }
  ImGui::SameLine();
  if (ImGui::Button("States")) app.Panels().layer_state_manager = true;
  ImGui::Separator();

  if (ImGui::BeginTable("layers", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Cur", ImGuiTableColumnFlags_WidthFixed, 30);
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 30);
    ImGui::TableSetupColumn("Lock", ImGuiTableColumnFlags_WidthFixed, 36);
    ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 40);
    ImGui::TableSetupColumn("Objects", ImGuiTableColumnFlags_WidthFixed, 56);
    ImGui::TableHeadersRow();

    std::vector<int> counts(doc.Layers().size(), 0);
    for (const SceneObject& o : doc.Objects()) {
      if (o.layer_index >= 0 && static_cast<size_t>(o.layer_index) < counts.size()) ++counts[static_cast<size_t>(o.layer_index)];
    }
    // Draw as a tree by parent.
    std::function<void(int)> draw_children = [&](int parent) {
      for (int i = 0; i < static_cast<int>(doc.Layers().size()); ++i) {
        Layer& L = doc.Layers()[static_cast<size_t>(i)];
        if (L.parent != parent) continue;
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        bool has_children = false;
        for (const Layer& c : doc.Layers()) if (c.parent == i) { has_children = true; break; }
        ImGuiTreeNodeFlags tf = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
        if (!has_children) tf |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (i == doc.CurrentLayer()) tf |= ImGuiTreeNodeFlags_Selected;
        const bool open = ImGui::TreeNodeEx("##node", tf, "%s", L.name.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) doc.SetCurrentLayer(i);
        if (ImGui::BeginPopupContextItem("layer_ctx")) {
          static char rename[128];
          if (ImGui::IsWindowAppearing()) std::snprintf(rename, sizeof(rename), "%s", L.name.c_str());
          if (ImGui::InputText("Rename", rename, sizeof(rename), ImGuiInputTextFlags_EnterReturnsTrue)) {
            doc.BeginChange("Rename layer");
            L.name = rename;
            ImGui::CloseCurrentPopup();
          }
          static char desc[512];
          if (ImGui::IsWindowAppearing()) std::snprintf(desc, sizeof(desc), "%s", L.description.c_str());
          if (ImGui::InputTextMultiline("Notes", desc, sizeof(desc), ImVec2(240, 60))) L.description = desc;
          if (ImGui::MenuItem("Select objects on layer")) {
            doc.SelectWhere([i](const SceneObject& o) { return o.layer_index == i; });
          }
          if (ImGui::MenuItem("Move selected objects here")) {
            doc.BeginChange("Change layer");
            for (SceneObject& o : doc.Objects()) if (o.selected) o.layer_index = i;
          }
          if (ImGui::MenuItem("One layer on (isolate)")) {
            for (Layer& other : doc.Layers()) other.visible = false;
            L.visible = true;
          }
          if (ImGui::MenuItem("All layers on")) for (Layer& other : doc.Layers()) other.visible = true;
          if (ImGui::MenuItem("Delete layer")) {
            doc.BeginChange("Delete layer");
            if (!doc.RemoveLayer(i)) app.Notify("Layer is in use or is the only layer");
            ImGui::EndPopup();
            ImGui::PopID();
            if (open && has_children) ImGui::TreePop();
            return;
          }
          ImGui::EndPopup();
        }
        ImGui::TableNextColumn();
        if (ImGui::RadioButton("##cur", i == doc.CurrentLayer())) doc.SetCurrentLayer(i);
        ImGui::TableNextColumn();
        if (ImGui::Checkbox("##on", &L.visible)) doc.Touch();
        ImGui::TableNextColumn();
        if (ImGui::Checkbox("##lock", &L.locked)) doc.Touch();
        ImGui::TableNextColumn();
        if (ColorEdit("##color", L.color)) {
          doc.Touch();
          for (SceneObject& o : doc.Objects()) if (o.layer_index == i) o.InvalidateDisplay();
        }
        ImGui::TableNextColumn();
        ImGui::Text("%d", counts[static_cast<size_t>(i)]);
        if (open && has_children) {
          draw_children(i);
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
    };
    draw_children(-1);
    ImGui::EndTable();
  }
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

void DrawPropertiesPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin("Properties", &app.Panels().properties)) { ImGui::End(); return; }
  std::vector<ObjectId> sel = doc.SelectedIds();
  if (sel.empty()) {
    ImGui::TextDisabled("No objects selected.");
    ImGui::Separator();
    ImGui::Text("Document");
    ImGui::BulletText("%zu objects, %zu layers", doc.ObjectCount(), doc.Layers().size());
    ImGui::BulletText("Units: %s", doc.Settings().unit_system.c_str());
    ImGui::BulletText("Tolerance: %g", doc.Settings().absolute_tolerance);
    if (Viewport* vp = app.ActiveViewport()) {
      ImGui::Separator();
      ImGui::Text("Viewport: %s", vp->Name().c_str());
      ImGui::BulletText("Display mode: %s", DisplayModeName(vp->Mode()));
      const CameraState& c = vp->GetCamera().State();
      ImGui::BulletText("Camera: %s", FormatPoint(c.eye).c_str());
      ImGui::BulletText("Target: %s", FormatPoint(c.target).c_str());
      ImGui::BulletText("Projection: %s", c.perspective ? "Perspective" : "Parallel");
    }
    ImGui::End();
    return;
  }
  SceneObject* first = doc.Find(sel[0]);
  if (!first) { ImGui::End(); return; }
  ImGui::Text("%zu object%s selected", sel.size(), sel.size() == 1 ? "" : "s");
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Object", ImGuiTreeNodeFlags_DefaultOpen)) {
    std::string name = first->name;
    if (InputString("Name", name, ImGuiInputTextFlags_EnterReturnsTrue)) {
      doc.BeginChange("Rename");
      for (ObjectId id : sel) if (SceneObject* o = doc.Find(id)) o->name = name;
    }
    ImGui::Text("Type: %s", ObjectKindName(first->kind));
    // Layer combo.
    std::string current = doc.LayerFullPath(first->layer_index);
    if (ImGui::BeginCombo("Layer", current.c_str())) {
      for (int i = 0; i < static_cast<int>(doc.Layers().size()); ++i) {
        if (ImGui::Selectable(doc.LayerFullPath(i).c_str(), i == first->layer_index)) {
          doc.BeginChange("Change layer");
          for (ObjectId id : sel) if (SceneObject* o = doc.Find(id)) o->layer_index = i;
        }
      }
      ImGui::EndCombo();
    }
    bool by_layer = first->color_by_layer;
    if (ImGui::Checkbox("Color by layer", &by_layer)) {
      doc.BeginChange("Color source");
      for (ObjectId id : sel) if (SceneObject* o = doc.Find(id)) { o->color_by_layer = by_layer; o->InvalidateDisplay(); }
    }
    if (!by_layer) {
      Color c = first->color;
      if (ColorEdit("Object color", c)) {
        doc.BeginChange("Object color");
        for (ObjectId id : sel) if (SceneObject* o = doc.Find(id)) { o->color = c; o->InvalidateDisplay(); }
      }
    }
    bool locked = first->locked;
    if (ImGui::Checkbox("Locked", &locked)) {
      doc.BeginChange("Lock");
      for (ObjectId id : sel) if (SceneObject* o = doc.Find(id)) o->locked = locked;
    }
    bool cps = first->show_control_points;
    if (ImGui::Checkbox("Show control points", &cps)) {
      for (ObjectId id : sel) if (SceneObject* o = doc.Find(id)) { o->show_control_points = cps; o->InvalidateDisplay(); }
    }
    if (first->group_id >= 0) ImGui::Text("Group: %d", first->group_id);
    std::string mat = first->material_name;
    if (InputString("Material", mat, ImGuiInputTextFlags_EnterReturnsTrue)) {
      doc.BeginChange("Material");
      for (ObjectId id : sel) if (SceneObject* o = doc.Find(id)) o->material_name = mat;
    }
  }
  if (sel.size() == 1 && ImGui::CollapsingHeader("Details", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextWrapped("%s", first->Describe().c_str());
    kernel::BoundingBox bb;
    if (doc.BoundingBoxOf(sel, bb)) {
      ImGui::Text("Bounding box:");
      ImGui::BulletText("min %s", FormatPoint(bb.min).c_str());
      ImGui::BulletText("max %s", FormatPoint(bb.max).c_str());
      ImGui::BulletText("size %s x %s x %s", FormatNumber(bb.max.x - bb.min.x).c_str(), FormatNumber(bb.max.y - bb.min.y).c_str(), FormatNumber(bb.max.z - bb.min.z).c_str());
    }
  }
  if (ImGui::CollapsingHeader("Attribute User Text")) {
    static char key[128], value[256];
    for (auto it = first->user_text.begin(); it != first->user_text.end();) {
      ImGui::PushID(it->first.c_str());
      ImGui::Text("%s = %s", it->first.c_str(), it->second.c_str());
      ImGui::SameLine();
      bool erase = ImGui::SmallButton("x");
      ImGui::PopID();
      if (erase) { doc.BeginChange("Remove user text"); it = first->user_text.erase(it); }
      else ++it;
    }
    ImGui::SetNextItemWidth(100);
    ImGui::InputText("Key", key, sizeof(key));
    ImGui::SetNextItemWidth(140);
    ImGui::InputText("Value", value, sizeof(value));
    if (ImGui::Button("Set") && key[0]) {
      doc.BeginChange("Set user text");
      for (ObjectId id : sel) if (SceneObject* o = doc.Find(id)) o->user_text[key] = value;
    }
  }
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Command history / list / help
// ---------------------------------------------------------------------------

void DrawCommandHistoryPanel(Application& app) {
  if (!ImGui::Begin("Command History", &app.Panels().command_history)) { ImGui::End(); return; }
  if (ImGui::SmallButton("Clear")) app.Engine().ClearHistory();
  ImGui::SameLine();
  if (ImGui::SmallButton("Copy all")) {
    std::string all;
    for (const std::string& l : app.Engine().History()) all += l + "\n";
    ImGui::SetClipboardText(all.c_str());
  }
  ImGui::Separator();
  ImGui::BeginChild("hist", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
  for (const std::string& l : app.Engine().History()) {
    if (!l.empty() && l[0] == '!') ImGui::TextColored(ImVec4(ThemeColors::kWarn[0], ThemeColors::kWarn[1], ThemeColors::kWarn[2], 1), "%s", l.c_str() + 1);
    else ImGui::TextUnformatted(l.c_str());
  }
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

void DrawCommandListPanel(Application& app, std::string& filter, int& status_filter) {
  if (!ImGui::Begin("Command List", &app.Panels().command_list)) { ImGui::End(); return; }
  CommandEngine& eng = app.Engine();
  const size_t n_impl = eng.CountWithStatus(CommandStatus::Implemented);
  const size_t n_part = eng.CountWithStatus(CommandStatus::Partial);
  const size_t n_plan = eng.CountWithStatus(CommandStatus::Planned);
  ImGui::Text("%zu commands in the Rhino 8 reference.", eng.Registry().size());
  ImGui::TextColored(StatusColor(CommandStatus::Implemented), "%zu implemented", n_impl); ImGui::SameLine();
  ImGui::TextColored(StatusColor(CommandStatus::Partial), "%zu partial", n_part); ImGui::SameLine();
  ImGui::TextColored(StatusColor(CommandStatus::Planned), "%zu planned (help only)", n_plan);
  InputString("Search", filter);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::Combo("##status", &status_filter, "All\0Implemented\0Partial\0Planned\0");
  ImGui::Separator();
  const std::string f = ToLower(filter);
  if (ImGui::BeginTable("cmds", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthFixed, 180);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90);
    ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const auto& [key, rc] : eng.Registry()) {
      if (status_filter == 1 && rc.status != CommandStatus::Implemented) continue;
      if (status_filter == 2 && rc.status != CommandStatus::Partial) continue;
      if (status_filter == 3 && rc.status != CommandStatus::Planned) continue;
      const std::string desc = rc.info ? rc.info->description : rc.note;
      if (!f.empty() && key.find(f) == std::string::npos && ToLower(desc).find(f) == std::string::npos) continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (ImGui::Selectable(rc.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
        if (ImGui::IsMouseDoubleClicked(0)) eng.Execute(rc.name);
        else app.ShowHelpFor(rc.name);
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click: help.  Double-click: run.");
      ImGui::TableNextColumn();
      ImGui::TextColored(StatusColor(rc.status), "%s", StatusName(rc.status));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(desc.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void DrawHelpPanel(Application& app, std::string& search) {
  if (!ImGui::Begin("Help", &app.Panels().help)) { ImGui::End(); return; }
  if (InputString("Find command", search)) {
    const CommandInfo* exact = app.Catalog().Find(search);
    if (exact) app.help_command = exact->name;
  }
  if (!search.empty()) {
    auto matches = app.Catalog().WithPrefix(search, 8);
    for (const CommandInfo* m : matches) {
      if (ImGui::SmallButton(m->name.c_str())) { app.help_command = m->name; search.clear(); }
      ImGui::SameLine();
    }
    ImGui::NewLine();
  }
  ImGui::Separator();
  const CommandInfo* info = app.help_command.empty() ? nullptr : app.Catalog().Find(app.help_command);
  if (!info) {
    ImGui::TextWrapped("Type a command name above, click a command in the Command List, or press F1 while a command runs.");
    ImGui::Spacing();
    ImGui::TextWrapped("Mouse: right-drag orbits (perspective) or pans (parallel views); Shift+right-drag pans; wheel zooms toward the cursor; left-click selects; drag left-to-right for window select, right-to-left for crossing select.");
    ImGui::TextWrapped("Command line: type a name and Enter. Options show as buttons; type an option name or click it. Enter repeats the last command. Esc cancels. Coordinates: x,y,z  @dx,dy (relative)  <angle (polar)  or a plain distance to constrain along the cursor direction.");
    ImGui::End();
    return;
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ThemeColors::kAccent[0], ThemeColors::kAccent[1], ThemeColors::kAccent[2], 1));
  ImGui::Text("%s", info->name.c_str());
  ImGui::PopStyleColor();
  if (const RegisteredCommand* rc = app.Engine().Find(info->name)) {
    ImGui::SameLine();
    ImGui::TextColored(StatusColor(rc->status), "[%s]", StatusName(rc->status));
    if (!rc->note.empty()) ImGui::TextDisabled("%s", rc->note.c_str());
  }
  if (ImGui::SmallButton("Run")) app.Engine().Execute(info->name);
  ImGui::TextWrapped("%s", info->description.c_str());
  if (!info->toolbars.empty()) ImGui::TextDisabled("Toolbars: %s", info->toolbars.c_str());
  if (!info->menu.empty()) ImGui::TextDisabled("Menu: %s", info->menu.c_str());
  if (!info->options.empty()) {
    ImGui::Separator();
    ImGui::Text("Options");
    for (const std::string& o : info->options) ImGui::BulletText("%s", o.c_str());
  }
  if (!info->help.empty()) {
    ImGui::Separator();
    ImGui::BeginChild("helpbody", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::TextWrapped("%s", info->help.c_str());
    ImGui::EndChild();
  }
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Smaller panels
// ---------------------------------------------------------------------------

void DrawNotificationsPanel(Application& app) {
  if (!ImGui::Begin("Notifications", &app.Panels().notifications)) { ImGui::End(); return; }
  ImGui::TextWrapped("Dino 8 is free software. There are no licences, subscriptions, sign-ins or update nags to manage.");
  ImGui::Separator();
  ImGui::Text("Recent messages:");
  int shown = 0;
  const auto& h = app.Engine().History();
  for (auto it = h.rbegin(); it != h.rend() && shown < 30; ++it, ++shown) ImGui::BulletText("%s", it->c_str());
  ImGui::End();
}

void DrawNamedViewsPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin("Named Views", &app.Panels().named_views)) { ImGui::End(); return; }
  static char name[128] = "";
  ImGui::InputTextWithHint("##nv", "view name", name, sizeof(name));
  ImGui::SameLine();
  if (ImGui::Button("Save current") && app.ActiveViewport()) {
    NamedView nv;
    nv.name = std::strlen(name) ? name : "View " + std::to_string(doc.NamedViews().size() + 1);
    nv.camera = app.ActiveViewport()->GetCamera().State();
    doc.NamedViews().push_back(nv);
    doc.Touch();
    name[0] = 0;
  }
  ImGui::Separator();
  for (size_t i = 0; i < doc.NamedViews().size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable(doc.NamedViews()[i].name.c_str())) {
      if (Viewport* vp = app.ActiveViewport()) vp->GetCamera().SetState(doc.NamedViews()[i].camera);
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
    if (ImGui::SmallButton("x")) { doc.NamedViews().erase(doc.NamedViews().begin() + static_cast<long>(i)); doc.Touch(); ImGui::PopID(); break; }
    ImGui::PopID();
  }
  ImGui::End();
}

void DrawNotesPanel(Application& app, char* buffer, size_t buffer_size) {
  Document& doc = app.Doc();
  if (!ImGui::Begin("Notes", &app.Panels().notes)) { ImGui::End(); return; }
  if (std::strcmp(buffer, doc.Notes().c_str()) != 0 && !ImGui::IsAnyItemActive()) std::snprintf(buffer, buffer_size, "%s", doc.Notes().c_str());
  if (ImGui::InputTextMultiline("##notes", buffer, buffer_size, ImVec2(-1, -1))) {
    doc.Notes() = buffer;
    doc.Touch();
  }
  ImGui::End();
}

void DrawDocumentUserTextPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin("Document User Text", &app.Panels().document_user_text)) { ImGui::End(); return; }
  static char key[128], value[512];
  ImGui::InputText("Key", key, sizeof(key));
  ImGui::InputText("Value", value, sizeof(value));
  if (ImGui::Button("Set") && key[0]) { doc.UserText()[key] = value; doc.Touch(); }
  ImGui::Separator();
  for (auto it = doc.UserText().begin(); it != doc.UserText().end();) {
    ImGui::PushID(it->first.c_str());
    ImGui::Text("%s = %s", it->first.c_str(), it->second.c_str());
    ImGui::SameLine();
    const bool erase = ImGui::SmallButton("x");
    ImGui::PopID();
    if (erase) { it = doc.UserText().erase(it); doc.Touch(); }
    else ++it;
  }
  ImGui::End();
}

void DrawMaterialsPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin("Materials", &app.Panels().materials)) { ImGui::End(); return; }
  static std::map<std::string, Color> materials = {
      {"Default", Color::FromBytes(200, 200, 200)}, {"Plastic Red", Color::FromBytes(220, 60, 50)},
      {"Plastic Blue", Color::FromBytes(60, 110, 220)}, {"Steel", Color::FromBytes(150, 155, 165)},
      {"Brass", Color::FromBytes(205, 170, 80)}, {"Glass", Color::FromBytes(180, 220, 240)},
      {"Wood", Color::FromBytes(160, 110, 60)}, {"Rubber Black", Color::FromBytes(35, 35, 38)}};
  static char new_mat[64] = "";
  ImGui::InputTextWithHint("##nm", "new material name", new_mat, sizeof(new_mat));
  ImGui::SameLine();
  if (ImGui::Button("Add") && new_mat[0]) { materials[new_mat] = Color::FromBytes(180, 180, 180); new_mat[0] = 0; }
  ImGui::Separator();
  for (auto& [name, color] : materials) {
    ImGui::PushID(name.c_str());
    ColorEdit("##c", color);
    ImGui::SameLine();
    ImGui::Text("%s", name.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110);
    if (ImGui::SmallButton("Assign to selection")) {
      doc.BeginChange("Assign material");
      for (SceneObject& o : doc.Objects()) {
        if (!o.selected) continue;
        o.material_name = name;
        o.color = color;
        o.color_by_layer = false;
        o.InvalidateDisplay();
      }
    }
    ImGui::PopID();
  }
  ImGui::End();
}

void DrawDisplayPanel(Application& app) {
  if (!ImGui::Begin("Display", &app.Panels().display)) { ImGui::End(); return; }
  Viewport* vp = app.ActiveViewport();
  if (vp) {
    ImGui::Text("Viewport: %s", vp->Name().c_str());
    int mode = static_cast<int>(vp->Mode());
    std::string names;
    for (DisplayMode m : AllDisplayModes()) { names += DisplayModeName(m); names.push_back('\0'); }
    names.push_back('\0');
    if (ImGui::Combo("Display mode", &mode, names.c_str())) vp->SetMode(static_cast<DisplayMode>(mode));
    CameraState& c = vp->GetCamera().State();
    if (ImGui::Checkbox("Perspective projection", &c.perspective)) {}
    float lens = static_cast<float>(c.lens_mm);
    if (ImGui::SliderFloat("Lens (mm)", &lens, 10.0f, 200.0f)) c.lens_mm = lens;
  }
  DocumentSettings& s = app.Doc().Settings();
  ImGui::Separator();
  ImGui::Checkbox("Show grid", &s.show_grid);
  ImGui::Checkbox("Show axes", &s.show_axes);
  float spacing = static_cast<float>(s.grid_spacing);
  if (ImGui::InputFloat("Grid spacing", &spacing, 0.5f, 5.0f)) s.grid_spacing = std::max(0.001f, spacing);
  ImGui::InputInt("Major line every", &s.grid_major_every);
  ImGui::InputInt("Grid extents", &s.grid_extents);
  ImGui::Separator();
  float ct = static_cast<float>(app.curve_display_tolerance), st = static_cast<float>(app.surface_display_tolerance);
  if (ImGui::SliderFloat("Curve display tolerance", &ct, 0.001f, 0.5f, "%.3f")) {
    app.curve_display_tolerance = ct;
    for (SceneObject& o : app.Doc().Objects()) o.InvalidateDisplay();
  }
  if (ImGui::SliderFloat("Surface display tolerance", &st, 0.001f, 1.0f, "%.3f")) {
    app.surface_display_tolerance = st;
    for (SceneObject& o : app.Doc().Objects()) o.InvalidateDisplay();
  }
  ImGui::Checkbox("Control points on selected", &app.show_control_points_for_selected);
  ImGui::End();
}

void DrawCalculatorPanel(Application& app, std::string& input, std::string& result) {
  if (!ImGui::Begin("Calculator", &app.Panels().calculator)) { ImGui::End(); return; }
  ImGui::TextDisabled("+ - * / ^ %%  sqrt sin cos tan asin acos atan abs ln log exp floor ceil round min max pow hypot pi e");
  if (InputString("Expression", input, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::Button("Evaluate")) {
    double v = 0;
    std::string err;
    result = EvaluateExpression(input, v, err) ? FormatNumber(v) : "Error: " + err;
  }
  ImGui::Text("= %s", result.c_str());
  if (!result.empty() && result.rfind("Error", 0) != 0) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(result.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Send to command line")) app.Engine().FeedText(result);
  }
  ImGui::End();
}

void DrawAboutWindow(Application& app) {
  ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
  if (!ImGui::Begin("About Dino 8", &app.Panels().about, ImGuiWindowFlags_NoDocking)) { ImGui::End(); return; }
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ThemeColors::kAccent[0], ThemeColors::kAccent[1], ThemeColors::kAccent[2], 1));
  ImGui::Text("Dino 8  %s", DINO8_VERSION);
  ImGui::PopStyleColor();
  ImGui::TextWrapped("A free NURBS, SubD and mesh modeler with Rhino 8's command vocabulary. Totally free: no subscription, no payment, no licence keys, no accounts, no telemetry.");
  ImGui::Separator();
  ImGui::BulletText("Geometry kernel: OpenNURBS (McNeel, MIT licence) + Manifold (Apache 2.0)");
  ImGui::BulletText("UI: Dear ImGui (MIT) + GLFW (zlib)");
  ImGui::BulletText("Command reference: %zu commands", app.Catalog().Size());
  ImGui::BulletText("Native file format: .3dm (reads and writes Rhino 8 files)");
  ImGui::Separator();
  ImGui::TextWrapped("Keyboard: F1 command list, F2 history, F3 properties, F7 grid, F8 ortho, F9 grid snap, F10 control points, Ctrl+Z/Y undo/redo, Esc cancel.");
  if (ImGui::Button("Close")) app.Panels().about = false;
  ImGui::End();
}

void DrawOptionsWindow(Application& app) {
  ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_Appearing);
  if (!ImGui::Begin("Options", &app.Panels().options)) { ImGui::End(); return; }
  if (ImGui::BeginTabBar("opts")) {
    if (ImGui::BeginTabItem("General")) {
      ImGui::TextWrapped("Dino 8 has no licence, update-check or account settings: there is nothing to configure here that could ever lock you out.");
      if (ImGui::SliderFloat("UI scale", &app.ui_scale, 0.75f, 2.0f)) { ApplyDinoTheme(app.ui_scale); }
      ImGui::Checkbox("Show toolbars", &app.Panels().toolbars);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Modeling Aids")) {
      SnapSettings& s = app.Snaps();
      ImGui::Checkbox("Grid snap", &s.grid_snap);
      ImGui::Checkbox("Ortho", &s.ortho);
      ImGui::Checkbox("Planar", &s.planar);
      ImGui::Checkbox("SmartTrack", &s.smart_track);
      ImGui::Separator();
      ImGui::Text("Object snaps");
      ImGui::Checkbox("End", &s.end); ImGui::SameLine(); ImGui::Checkbox("Near", &s.near_); ImGui::SameLine(); ImGui::Checkbox("Point", &s.point);
      ImGui::Checkbox("Mid", &s.mid); ImGui::SameLine(); ImGui::Checkbox("Cen", &s.cen); ImGui::SameLine(); ImGui::Checkbox("Int", &s.int_);
      ImGui::Checkbox("Perp", &s.perp); ImGui::SameLine(); ImGui::Checkbox("Tan", &s.tan); ImGui::SameLine(); ImGui::Checkbox("Quad", &s.quad);
      ImGui::Checkbox("Vertex", &s.vertex); ImGui::SameLine(); ImGui::Checkbox("Disable all", &s.disable_all);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("View")) {
      DocumentSettings& d = app.Doc().Settings();
      ImGui::Checkbox("Grid", &d.show_grid);
      ImGui::Checkbox("Axes", &d.show_axes);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Aliases")) {
      static char alias[64], cmd[128];
      ImGui::InputText("Alias", alias, sizeof(alias));
      ImGui::InputText("Command", cmd, sizeof(cmd));
      if (ImGui::Button("Add / Update") && alias[0] && cmd[0]) { app.Engine().Aliases()[ToLower(alias)] = cmd; alias[0] = cmd[0] = 0; }
      ImGui::Separator();
      for (auto it = app.Engine().Aliases().begin(); it != app.Engine().Aliases().end();) {
        ImGui::PushID(it->first.c_str());
        ImGui::Text("%-10s -> %s", it->first.c_str(), it->second.c_str());
        ImGui::SameLine();
        const bool del = ImGui::SmallButton("x");
        ImGui::PopID();
        if (del) it = app.Engine().Aliases().erase(it); else ++it;
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Keyboard")) {
      ImGui::BulletText("F1 Command list   F2 History   F3 Properties   F7 Grid   F8 Ortho   F9 Grid snap   F10/F11 Points on/off");
      ImGui::BulletText("Ctrl+N/O/S New/Open/Save   Ctrl+Z/Y Undo/Redo   Ctrl+A Select all   Ctrl+G Group   Ctrl+H Hide");
      ImGui::BulletText("Home Undo view   PgUp/PgDn zoom   Arrow keys orbit   Esc cancel / deselect   Enter repeat last");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void DrawDocumentPropertiesWindow(Application& app) {
  ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_Appearing);
  if (!ImGui::Begin("Document Properties", &app.Panels().document_properties)) { ImGui::End(); return; }
  DocumentSettings& s = app.Doc().Settings();
  static const char* units[] = {"Millimeters", "Centimeters", "Meters", "Inches", "Feet"};
  int cur = 0;
  for (int i = 0; i < 5; ++i) if (s.unit_system == units[i]) cur = i;
  if (ImGui::Combo("Units", &cur, units, 5)) { s.unit_system = units[cur]; app.Doc().Touch(); }
  double tol = s.absolute_tolerance;
  if (ImGui::InputDouble("Absolute tolerance", &tol, 0, 0, "%.5f")) { s.absolute_tolerance = std::max(1e-8, tol); app.Doc().Touch(); }
  double ang = s.angle_tolerance_degrees;
  if (ImGui::InputDouble("Angle tolerance (deg)", &ang, 0, 0, "%.3f")) { s.angle_tolerance_degrees = std::max(0.001, ang); app.Doc().Touch(); }
  ImGui::Separator();
  ImGui::Text("Grid");
  double gs = s.grid_spacing;
  if (ImGui::InputDouble("Spacing", &gs)) s.grid_spacing = std::max(0.001, gs);
  ImGui::InputInt("Major every", &s.grid_major_every);
  ImGui::InputInt("Extents", &s.grid_extents);
  ImGui::Separator();
  ImGui::Text("File: %s", app.Doc().Path().empty() ? "(unsaved)" : app.Doc().Path().c_str());
  ImGui::Text("Objects: %zu   Layers: %zu   Revision: %llu", app.Doc().ObjectCount(), app.Doc().Layers().size(),
              static_cast<unsigned long long>(app.Doc().Revision()));
  ImGui::End();
}

void DrawBoxEditPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin("BoxEdit", &app.Panels().box_edit)) { ImGui::End(); return; }
  std::vector<ObjectId> sel = doc.SelectedIds();
  kernel::BoundingBox bb;
  if (sel.empty() || !doc.BoundingBoxOf(sel, bb)) {
    ImGui::TextDisabled("Select objects to edit their bounding box numerically.");
    ImGui::End();
    return;
  }
  const kernel::Point3d center((bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2, (bb.min.z + bb.max.z) / 2);
  const double size[3] = {bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z};
  double pos[3] = {center.x, center.y, center.z};
  double sz[3] = {size[0], size[1], size[2]};
  bool changed_pos = ImGui::InputScalarN("Position (centre)", ImGuiDataType_Double, pos, 3, nullptr, nullptr, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
  bool changed_size = ImGui::InputScalarN("Size", ImGuiDataType_Double, sz, 3, nullptr, nullptr, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
  static bool uniform = true;
  ImGui::Checkbox("Uniform scale", &uniform);
  if (changed_pos || changed_size) {
    doc.BeginChange("BoxEdit");
    ON_Xform xf = ON_Xform::IdentityTransformation;
    if (changed_size) {
      double sx = size[0] > 1e-12 ? sz[0] / size[0] : 1, sy = size[1] > 1e-12 ? sz[1] / size[1] : 1, sz_ = size[2] > 1e-12 ? sz[2] / size[2] : 1;
      if (uniform) {
        double f = sx != 1 ? sx : (sy != 1 ? sy : sz_);
        sx = sy = sz_ = f;
      }
      ON_Xform to_origin = ON_Xform::TranslationTransformation(ON_3dVector(-center.x, -center.y, -center.z));
      ON_Xform scale = ON_Xform::DiagonalTransformation(sx, sy, sz_);
      ON_Xform back = ON_Xform::TranslationTransformation(ON_3dVector(center.x, center.y, center.z));
      xf = back * scale * to_origin;
    }
    if (changed_pos) xf = ON_Xform::TranslationTransformation(ON_3dVector(pos[0] - center.x, pos[1] - center.y, pos[2] - center.z)) * xf;
    for (ObjectId id : sel) if (SceneObject* o = doc.Find(id)) o->Transform(xf);
  }
  ImGui::Separator();
  ImGui::Text("Min %s", FormatPoint(bb.min).c_str());
  ImGui::Text("Max %s", FormatPoint(bb.max).c_str());
  ImGui::End();
}

void DrawUndoMultipleWindow(Application& app, bool redo) {
  bool& flag = redo ? app.Panels().redo_multiple : app.Panels().undo_multiple;
  if (!ImGui::Begin(redo ? "Redo Multiple" : "Undo Multiple", &flag, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
  std::vector<std::string> labels = redo ? app.Doc().RedoLabels() : app.Doc().UndoLabels();
  if (labels.empty()) ImGui::TextDisabled("Nothing to %s.", redo ? "redo" : "undo");
  for (size_t i = 0; i < labels.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable((std::to_string(i + 1) + ". " + labels[i]).c_str())) {
      for (size_t k = 0; k <= i; ++k) { if (redo) app.Doc().Redo(); else app.Doc().Undo(); }
      flag = false;
    }
    ImGui::PopID();
  }
  ImGui::End();
}

void DrawLayerStateManager(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin("Layer State Manager", &app.Panels().layer_state_manager)) { ImGui::End(); return; }
  struct State { std::string name; std::vector<std::pair<std::string, std::pair<bool, bool>>> layers; };
  static std::vector<State> states;
  static char name[128] = "";
  ImGui::InputTextWithHint("##ls", "state name", name, sizeof(name));
  ImGui::SameLine();
  if (ImGui::Button("Save state")) {
    State s;
    s.name = std::strlen(name) ? name : "State " + std::to_string(states.size() + 1);
    for (const Layer& L : doc.Layers()) s.layers.push_back({L.name, {L.visible, L.locked}});
    states.push_back(s);
    name[0] = 0;
  }
  ImGui::Separator();
  for (size_t i = 0; i < states.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable(states[i].name.c_str())) {
      for (const auto& [lname, vis_lock] : states[i].layers) {
        int idx = doc.FindLayer(lname);
        if (idx >= 0) { doc.Layers()[static_cast<size_t>(idx)].visible = vis_lock.first; doc.Layers()[static_cast<size_t>(idx)].locked = vis_lock.second; }
      }
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
    if (ImGui::SmallButton("x")) { states.erase(states.begin() + static_cast<long>(i)); ImGui::PopID(); break; }
    ImGui::PopID();
  }
  ImGui::End();
}

void DrawSelectionFilterPanel(Application& app) {
  if (!ImGui::Begin("Selection Filter", &app.Panels().selection_filter, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
  ImGui::TextDisabled("Select only these object types with clicks and windows:");
  static bool filt[6] = {true, true, true, true, true, true};
  const char* names[6] = {"Points", "Curves", "Surfaces", "Polysurfaces", "Meshes", "SubDs"};
  for (int i = 0; i < 6; ++i) { ImGui::Checkbox(names[i], &filt[i]); if (i % 3 != 2) ImGui::SameLine(); }
  if (ImGui::Button("All")) for (bool& b : filt) b = true;
  ImGui::SameLine();
  if (ImGui::Button("None")) for (bool& b : filt) b = false;
  // Apply as a document-wide "selectable" flag by deselecting filtered kinds.
  Document& doc = app.Doc();
  for (SceneObject& o : doc.Objects()) {
    if (o.selected && !filt[static_cast<int>(o.kind)]) o.selected = false;
  }
  ImGui::End();
}

void DrawMacroEditor(Application& app) {
  ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_Appearing);
  if (!ImGui::Begin("Macro Editor", &app.Panels().macro_editor)) { ImGui::End(); return; }
  static char text[4096] = "! _Box 0,0,0 10,10,10\n_ZoomExtents\n";
  ImGui::TextDisabled("One command per line. ! cancels the running command, _ forces English names, - suppresses dialogs.");
  ImGui::InputTextMultiline("##macro", text, sizeof(text), ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() * 1.5f));
  if (ImGui::Button("Run")) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) if (!line.empty()) app.Engine().Execute(line);
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy")) ImGui::SetClipboardText(text);
  ImGui::End();
}

}  // namespace dino8::app
