#include "app/Application.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "imgui.h"
#include "imgui_internal.h"
#include "io/File3dm.h"
#include "ui/Panels.h"
#include "ui/Theme.h"
#include "app/Settings.h"

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#endif

namespace dino8::app {

namespace fs = std::filesystem;

// Registration entry points implemented in the cmd_*.cpp files.
void RegisterCreateCommands(CommandEngine&);
void RegisterSolidCommands(CommandEngine&);
void RegisterTransformCommands(CommandEngine&);
void RegisterEditCommands(CommandEngine&);
void RegisterBooleanCommands(CommandEngine&);
void RegisterAnalyzeCommands(CommandEngine&);
void RegisterSelectCommands(CommandEngine&);
void RegisterViewCommands(CommandEngine&);
void RegisterLayerCommands(CommandEngine&);
void RegisterFileCommands(CommandEngine&);
void RegisterMiscCommands(CommandEngine&);
void RegisterAnnotateCommands(CommandEngine&);
void RegisterDraftingCommands(CommandEngine&);
void RegisterCurveEditCommands(CommandEngine&);

Application::Application() = default;
Application::~Application() = default;

bool Application::Init(const std::string& exe_dir, std::string& error) {
  exe_dir_ = exe_dir;
  std::vector<std::string> candidates = {
      exe_dir + "/data/commands.json",
      exe_dir + "/../Resources/data/commands.json",             // macOS bundle
      exe_dir + "/../share/dino8/data/commands.json",           // Linux install
      exe_dir + "/../../data/commands.json",                    // build tree
      exe_dir + "/../../../dino8-app/data/commands.json",
      "data/commands.json",
  };
  std::string catalog_error;
  if (!catalog_.Load(candidates, catalog_error)) {
    error = "Could not load the command catalog: " + catalog_error;
    // Keep going: the app still works, just without the reference list.
  }
  engine_ = std::make_unique<CommandEngine>(*this, doc_, catalog_);
  RegisterCommands();
  engine_->RegisterCatalogPlaceholders();
  engine_->InstallDefaultAliases();

  renderer_ok_ = renderer_.Init(renderer_error_);
  if (!renderer_ok_) {
    error = "Renderer failed to initialise: " + renderer_error_;
    return false;
  }
  SetViewportLayout(4);
  LoadSettings(*this, ui_scale);
  if (has_saved_layout) layout_built_ = true;
  engine_->Print("Dino 8 " DINO8_VERSION " - free NURBS / SubD / mesh modeler");
  engine_->Print("Command catalog: " + std::to_string(catalog_.Size()) + " commands loaded (" +
                 std::to_string(engine_->CountWithStatus(CommandStatus::Implemented)) + " implemented, " +
                 std::to_string(engine_->CountWithStatus(CommandStatus::Partial)) + " partial, " +
                 std::to_string(engine_->CountWithStatus(CommandStatus::Planned)) + " planned)");
  engine_->Print("Type a command name, or press F1 for the command list.");
  return true;
}

void Application::Shutdown() {
  SaveSettings(*this, ui_scale);
  viewports_.clear();
  renderer_.Shutdown();
}

void Application::RegisterCommands() {
  RegisterCreateCommands(*engine_);
  RegisterSolidCommands(*engine_);
  RegisterTransformCommands(*engine_);
  RegisterEditCommands(*engine_);
  RegisterBooleanCommands(*engine_);
  RegisterAnalyzeCommands(*engine_);
  RegisterSelectCommands(*engine_);
  RegisterViewCommands(*engine_);
  RegisterLayerCommands(*engine_);
  RegisterFileCommands(*engine_);
  RegisterMiscCommands(*engine_);
  RegisterAnnotateCommands(*engine_);
  RegisterDraftingCommands(*engine_);
  RegisterCurveEditCommands(*engine_);  // last: replaces the solid-only Intersect/Split registrations
}

Viewport* Application::ActiveViewport() {
  for (auto& vp : viewports_) {
    if (vp->IsActive()) return vp.get();
  }
  if (!viewports_.empty()) {
    if (active_viewport_ >= 0 && active_viewport_ < static_cast<int>(viewports_.size())) {
      return viewports_[static_cast<size_t>(active_viewport_)].get();
    }
    return viewports_.back().get();
  }
  return nullptr;
}

Viewport* Application::FindViewport(const std::string& name) {
  for (auto& vp : viewports_) {
    if (ToLower(vp->Name()) == ToLower(name)) return vp.get();
  }
  return nullptr;
}

void Application::SetViewportLayout(int count) {
  // Keep existing cameras where names match.
  std::vector<std::unique_ptr<Viewport>> old = std::move(viewports_);
  viewports_.clear();
  auto reuse = [&](const std::string& name, const std::string& view) {
    for (auto& v : old) {
      if (v && v->Name() == name) {
        viewports_.push_back(std::move(v));
        return;
      }
    }
    viewports_.push_back(std::make_unique<Viewport>(name, view));
  };
  if (count <= 1) {
    reuse("Perspective", "Perspective");
  } else if (count == 3) {
    reuse("Top", "Top");
    reuse("Front", "Front");
    reuse("Perspective", "Perspective");
  } else {
    reuse("Top", "Top");
    reuse("Perspective", "Perspective");
    reuse("Front", "Front");
    reuse("Right", "Right");
  }
  for (auto& v : viewports_) v->SetActive(false);
  Viewport* persp = FindViewport("Perspective");
  (persp ? persp : viewports_.back().get())->SetActive(true);
  layout_built_ = false;
}

void Application::ShowHelpFor(const std::string& command_name) {
  help_command = command_name;
  panels_.help = true;
}

void Application::ZoomExtentsAll() {
  for (auto& vp : viewports_) vp->ZoomExtents(doc_, false);
}

void Application::Notify(const std::string& text) {
  notifications_.push_back({text, ImGui::GetTime()});
  engine_->Print(text);
}

void Application::ShowFileDialog(const std::string& title, const std::vector<std::string>& extensions,
                                 bool save, std::function<void(const std::string&)> callback) {
#if defined(_WIN32)
  // Native Windows dialog: what testers expect on that platform.
  {
    std::string filter;
    std::string all;
    for (const std::string& e : extensions) all += (all.empty() ? "*" : ";*") + e;
    filter += "Supported files (" + all + ")";
    filter.push_back('\0');
    filter += all;
    filter.push_back('\0');
    filter += "All files (*.*)";
    filter.push_back('\0');
    filter += "*.*";
    filter.push_back('\0');
    filter.push_back('\0');
    char file[MAX_PATH] = {};
    if (save) {
      std::string def = !doc_.Path().empty() ? fs::path(doc_.Path()).filename().string() : "Untitled" + (extensions.empty() ? std::string(".3dm") : extensions.front());
      std::snprintf(file, sizeof(file), "%s", def.c_str());
    }
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title.c_str();
    std::string def_ext = extensions.empty() ? "3dm" : extensions.front().substr(1);
    ofn.lpstrDefExt = def_ext.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    const BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if (ok && callback) callback(file);
    return;
  }
#endif
  file_dialog_.open = true;
  file_dialog_.save = save;
  file_dialog_.title = title;
  file_dialog_.extensions = extensions;
  file_dialog_.callback = std::move(callback);
  if (file_dialog_.directory.empty()) {
    std::error_code ec;
    file_dialog_.directory = fs::current_path(ec).string();
  }
  if (save && !doc_.Path().empty()) file_dialog_.filename = fs::path(doc_.Path()).filename().string();
  else if (save) file_dialog_.filename = "Untitled" + (extensions.empty() ? std::string(".3dm") : extensions.front());
  else file_dialog_.filename.clear();
}

void Application::AddRecentFile(const std::string& path) {
  recent_files_.erase(std::remove(recent_files_.begin(), recent_files_.end(), path), recent_files_.end());
  recent_files_.insert(recent_files_.begin(), path);
  if (recent_files_.size() > 10) recent_files_.pop_back();
}

void Application::ConfirmDiscard(std::function<void()> then) {
  if (!doc_.Modified() || doc_.ObjectCount() == 0) {
    if (then) then();
    return;
  }
  pending_after_confirm_ = std::move(then);
  confirm_open_ = true;
}

void Application::DrawPopupToolbar() {
  if (open_popup_toolbar_) {
    ImGui::OpenPopup("##mmb_toolbar");
    ImGui::SetNextWindowPos(popup_toolbar_pos_, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    open_popup_toolbar_ = false;
  }
  if (ImGui::BeginPopup("##mmb_toolbar")) {
    ImGui::TextDisabled("Popup toolbar");
    ImGui::Separator();
    const char* rows[][6] = {{"Move", "Copy", "Rotate", "Scale", "Mirror", "Delete"},
                             {"Line", "Polyline", "Circle", "Rectangle", "Box", "Sphere"},
                             {"Join", "Explode", "Trim", "Split", "Extend", "Offset"},
                             {"Undo", "Redo", "Hide", "Show", "ZoomExtents", "SelAll"}};
    for (auto& row : rows) {
      for (int i = 0; i < 6; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(row[i], ImVec2(86, 0))) { engine_->Execute(row[i]); ImGui::CloseCurrentPopup(); }
      }
    }
    ImGui::EndPopup();
  }
}

void Application::DrawConfirmDiscard() {
  if (confirm_open_) {
    ImGui::OpenPopup("Save changes?");
    confirm_open_ = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
    const std::string name = doc_.Path().empty() ? std::string("Untitled") : fs::path(doc_.Path()).filename().string();
    ImGui::Text("%s has unsaved changes.", name.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(110, 0))) {
      std::function<void()> then = pending_after_confirm_;
      pending_after_confirm_ = nullptr;
      ImGui::CloseCurrentPopup();
      if (!doc_.Path().empty()) {
        std::string err;
        if (SaveDocument(doc_.Path(), err)) { if (then) then(); } else Notify(err);
      } else {
        ShowFileDialog("Save model", {".3dm"}, true, [this, then](const std::string& path) {
          std::string err;
          if (SaveDocument(path, err)) { if (then) then(); } else Notify(err);
        });
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save", ImVec2(110, 0))) {
      std::function<void()> then = pending_after_confirm_;
      pending_after_confirm_ = nullptr;
      ImGui::CloseCurrentPopup();
      doc_.SetModified(false);
      if (then) then();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      pending_after_confirm_ = nullptr;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

bool Application::NewDocument(bool confirm_discard) {
  if (confirm_discard) {
    ConfirmDiscard([this]() { NewDocument(false); });
    return true;
  }
  doc_.Clear();
  for (auto& vp : viewports_) vp->SetStandardView(vp->StandardView());
  engine_->Print("New document.");
  return true;
}

bool Application::OpenDocument(const std::string& path, std::string& error) {
  const std::string ext = ToLower(fs::path(path).extension().string());
  Document fresh;
  bool ok = false;
  if (ext == ".3dm") ok = Load3dm(fresh, path, error);
  else if (ext == ".obj" || ext == ".stl") {
    ok = ImportMeshFile(fresh, path, error);
  } else {
    error = "Unsupported file type: " + ext;
  }
  if (!ok) return false;
  doc_ = std::move(fresh);
  doc_.SetPath(path);
  doc_.SetModified(false);
  AddRecentFile(path);
  ZoomExtentsAll();
  Notify("Opened " + path + " (" + std::to_string(doc_.ObjectCount()) + " objects)");
  return true;
}

bool Application::SaveDocument(const std::string& path, std::string& error) {
  const std::string ext = ToLower(fs::path(path).extension().string());
  bool ok = false;
  if (ext == ".3dm" || ext.empty()) {
    std::string p = path;
    if (ext.empty()) p += ".3dm";
    ok = Save3dm(doc_, p, error);
    if (ok) {
      doc_.SetPath(p);
      doc_.SetModified(false);
      AddRecentFile(p);
      Notify("Saved " + p);
    }
  } else if (ext == ".obj" || ext == ".stl") {
    ok = ExportMeshFile(doc_, path, false, error);
    if (ok) Notify("Exported " + path);
  } else {
    error = "Unsupported file type: " + ext;
  }
  return ok;
}

bool Application::ImportFile(const std::string& path, std::string& error) {
  const std::string ext = ToLower(fs::path(path).extension().string());
  bool ok = false;
  doc_.BeginChange("Import");
  if (ext == ".3dm") {
    Document other;
    ok = Load3dm(other, path, error);
    if (ok) {
      for (SceneObject& o : other.Objects()) {
        o.selected = false;
        doc_.Add(o);
      }
    }
  } else if (ext == ".obj" || ext == ".stl") {
    ok = ImportMeshFile(doc_, path, error);
  } else {
    error = "Unsupported file type: " + ext;
  }
  if (ok) Notify("Imported " + path);
  return ok;
}

bool Application::ExportSelected(const std::string& path, std::string& error) {
  const std::string ext = ToLower(fs::path(path).extension().string());
  if (ext == ".3dm") {
    Document sub;
    for (const SceneObject& o : doc_.Objects()) {
      if (o.selected) sub.Add(o);
    }
    sub.Layers() = doc_.Layers();
    return Save3dm(sub, path, error);
  }
  return ExportMeshFile(doc_, path, true, error);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Application::Frame() {
  if (std::getenv("DINO8_UI_DEBUG") && (ImGui::GetFrameCount() == 5 || ImGui::GetFrameCount() == 90)) { const ImVec4& w = ImGui::GetStyle().Colors[ImGuiCol_WindowBg]; std::fprintf(stderr, "[theme] light=%d WindowBg=%.2f %.2f %.2f a=%.2f\n", light_theme ? 1 : 0, w.x, w.y, w.z, w.w); }
  HandleShortcuts();
  DrawDockspace();
  DrawViewports();
  DrawPanels();
  DrawCommandLine();
  DrawStatusBar();
  DrawFileDialog();
  DrawConfirmDiscard();
  DrawPopupToolbar();
  DrawNotifications();
}

void Application::BuildDefaultLayout(unsigned dockspace_id) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

  ImGuiID main_id = dockspace_id;
  ImGuiID right_id = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Right, 0.22f, nullptr, &main_id);
  ImGuiID bottom_id = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.18f, nullptr, &main_id);
  ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right_id, ImGuiDir_Down, 0.5f, nullptr, &right_id);

  ImGui::DockBuilderDockWindow("Layers", right_id);
  ImGui::DockBuilderDockWindow("Named Views", right_id);
  ImGui::DockBuilderDockWindow("Materials", right_id);
  ImGui::DockBuilderDockWindow("Display", right_id);
  ImGui::DockBuilderDockWindow("Properties", right_bottom);
  ImGui::DockBuilderDockWindow("Help", right_bottom);
  ImGui::DockBuilderDockWindow("Notes", right_bottom);
  ImGui::DockBuilderDockWindow("Document User Text", right_bottom);
  ImGui::DockBuilderDockWindow("Command History", bottom_id);
  ImGui::DockBuilderDockWindow("Command List", bottom_id);
  ImGui::DockBuilderDockWindow("Notifications", bottom_id);

  // Viewport grid.
  if (viewports_.size() == 4) {
    ImGuiID left_col = main_id;
    ImGuiID right_col = ImGui::DockBuilderSplitNode(left_col, ImGuiDir_Right, 0.5f, nullptr, &left_col);
    ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left_col, ImGuiDir_Down, 0.5f, nullptr, &left_col);
    ImGuiID right_bottom_vp = ImGui::DockBuilderSplitNode(right_col, ImGuiDir_Down, 0.5f, nullptr, &right_col);
    const ImGuiID slots[4] = {left_col, right_col, left_bottom, right_bottom_vp};
    for (size_t i = 0; i < 4; ++i) {
      const std::string title = viewports_[i]->Name() + "###vp_" + viewports_[i]->Name();
      ImGui::DockBuilderDockWindow(title.c_str(), slots[i]);
    }
  } else if (viewports_.size() == 3) {
    ImGuiID left_col = main_id;
    ImGuiID right_col = ImGui::DockBuilderSplitNode(left_col, ImGuiDir_Right, 0.6f, nullptr, &left_col);
    ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left_col, ImGuiDir_Down, 0.5f, nullptr, &left_col);
    const ImGuiID slots[3] = {left_col, left_bottom, right_col};
    for (size_t i = 0; i < 3; ++i) {
      const std::string title = viewports_[i]->Name() + "###vp_" + viewports_[i]->Name();
      ImGui::DockBuilderDockWindow(title.c_str(), slots[i]);
    }
  } else {
    for (auto& vp : viewports_) {
      const std::string title = vp->Name() + "###vp_" + vp->Name();
      ImGui::DockBuilderDockWindow(title.c_str(), main_id);
    }
  }
  ImGui::DockBuilderFinish(dockspace_id);
}

void Application::DrawDockspace() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  // Reserve space for the command line (top) and status bar (bottom).
  const float command_h = ImGui::GetFrameHeightWithSpacing() * 2.0f + 8.0f;
  const float status_h = StatusBarHeight();
  const float toolbar_h = panels_.toolbars ? 38.0f : 0.0f;
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + command_h + toolbar_h));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - command_h - status_h - toolbar_h));
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
  ImGui::Begin("##Dino8Dockspace", nullptr, flags);
  ImGui::PopStyleVar(3);
  const ImGuiID dockspace_id = ImGui::GetID("Dino8DockSpace");
  if (!layout_built_) {
    BuildDefaultLayout(dockspace_id);
    layout_built_ = true;
  }
  ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
  ImGui::End();
}

void Application::DrawViewports() {
  const Want want = engine_->CurrentWant();
  const bool want_point = want == Want::Point;
  const bool want_objects = want == Want::Objects;
  std::optional<kernel::Point3d> ortho_base = engine_->LastPoint();
  bool maximized_any = false;
  for (auto& vp : viewports_) maximized_any = maximized_any || vp->Maximized();

  // Render every visible viewport into its texture first.
  Viewport::FrameContext ctx;
  ctx.doc = &doc_;
  ctx.preview_lines = &engine_->PreviewLines();
  ctx.preview_points = &engine_->PreviewPoints();
  ctx.show_control_points_for_selected = show_control_points_for_selected;
  ctx.curve_tolerance = curve_display_tolerance;
  ctx.surface_tolerance = surface_display_tolerance;
  if (want_point && pending_hover_) ctx.cursor_marker = pending_hover_;

  bool request_focus = false;
  std::optional<kernel::Point3d> hover;
  for (size_t i = 0; i < viewports_.size(); ++i) {
    Viewport& vp = *viewports_[i];
    const bool show = !maximized_any || vp.Maximized();
    vp.SetVisible(show);
    if (!show) continue;
    vp.Render(renderer_, ctx);
    // The gumball hit-tests against last frame's image rectangle and locks
    // the viewport's left button while it owns the mouse.
    const bool gumball_wants_mouse = gumball_enabled && !engine_->IsRunning() &&
                                     gumball_.Update(*this, vp, ImGui::IsMouseHoveringRect(
                                         ImVec2(static_cast<float>(vp.ScreenX()), static_cast<float>(vp.ScreenY())),
                                         ImVec2(static_cast<float>(vp.ScreenX() + vp.Width()), static_cast<float>(vp.ScreenY() + vp.Height()))));
    vp.SetInputLocked(gumball_wants_mouse);
    ViewportEvents ev = vp.DrawUI(doc_, snaps_, want_point, want_objects, ortho_base,
                                  doc_.Settings().grid_spacing, request_focus);
    if (ev.hovered) {
      if (ev.hover_pick) hover = ev.hover_pick->point;
    }
    ProcessViewportEvents(vp, ev);
  }
  // Exactly one active viewport.
  int last_active = -1;
  for (size_t i = 0; i < viewports_.size(); ++i) {
    if (viewports_[i]->IsActive()) last_active = static_cast<int>(i);
  }
  if (last_active >= 0 && last_active != active_viewport_) {
    for (size_t i = 0; i < viewports_.size(); ++i) viewports_[i]->SetActive(static_cast<int>(i) == last_active);
    active_viewport_ = last_active;
  } else if (last_active < 0 && !viewports_.empty()) {
    viewports_[static_cast<size_t>(std::clamp(active_viewport_, 0, static_cast<int>(viewports_.size()) - 1))]->SetActive(true);
  }
  pending_hover_ = hover;
  engine_->FeedHover(hover);
  if (request_focus) focus_command_line_ = true;
}

void Application::ProcessViewportEvents(Viewport& vp, const ViewportEvents& ev) {
  const Want want = engine_->CurrentWant();
  if (ev.clicked) {
    for (auto& other : viewports_) other->SetActive(other.get() == &vp);
    if (want == Want::Point && ev.click_pick) {
      engine_->FeedPoint(ev.click_pick->point);
      return;
    }
    // Selection (either free, or while a command asks for objects).
    if (ev.clicked_object != kNoObject) {
      SceneObject* o = doc_.Find(ev.clicked_object);
      if (o) {
        if (ev.ctrl) {
          o->selected = !o->selected;
        } else if (ev.shift) {
          o->selected = true;
        } else {
          if (want != Want::Objects) doc_.SelectNone();
          o->selected = true;
        }
        // Group selection: clicking a member selects the whole group.
        if (o->group_id >= 0 && !ev.ctrl) {
          for (ObjectId id : doc_.GroupMembers(o->group_id)) doc_.Select(id, true);
        }
      }
      if (ev.double_clicked && want == Want::Objects) engine_->FeedEnter();
    } else if (!ev.ctrl && !ev.shift) {
      if (want != Want::Objects) doc_.SelectNone();
      else if (want == Want::Objects && ev.click_pick) {
        // Clicking empty space during object selection does nothing.
      }
    }
    return;
  }
  if (ev.window) {
    const auto& w = *ev.window;
    const std::vector<ObjectId> ids = vp.ObjectsInWindow(doc_, w[0], w[1], w[2], w[3], ev.window_is_crossing);
    if (!ev.ctrl && !ev.shift && want != Want::Objects) doc_.SelectNone();
    for (ObjectId id : ids) {
      if (ev.ctrl) {
        if (SceneObject* o = doc_.Find(id)) o->selected = !o->selected;
      } else {
        doc_.Select(id, true);
      }
    }
    return;
  }
  if (ev.right_clicked) {
    // Right click = Enter (Rhino convention).
    engine_->FeedEnter();
  }
  if (ev.middle_clicked) {
    // Rhino's middle-mouse popup toolbar.
    popup_toolbar_pos_ = ImGui::GetMousePos();
    open_popup_toolbar_ = true;
  }
}

void Application::HandleShortcuts() {
  ImGuiIO& io = ImGui::GetIO();
  const bool text_active = io.WantTextInput;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    if (engine_->IsRunning()) engine_->Cancel();
    else doc_.SelectNone();
    command_input_.clear();
  }
  if (!text_active || engine_->IsRunning()) {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !engine_->IsRunning()) engine_->Execute("Delete");
  }
  if (io.KeyCtrl && !text_active) {
    if (ImGui::IsKeyPressed(ImGuiKey_Z)) engine_->Execute("Undo");
    if (ImGui::IsKeyPressed(ImGuiKey_Y)) engine_->Execute("Redo");
    if (ImGui::IsKeyPressed(ImGuiKey_A)) engine_->Execute("SelAll");
    if (ImGui::IsKeyPressed(ImGuiKey_S)) engine_->Execute(io.KeyShift ? "SaveAs" : "Save");
    if (ImGui::IsKeyPressed(ImGuiKey_O)) engine_->Execute("Open");
    if (ImGui::IsKeyPressed(ImGuiKey_N)) engine_->Execute("New");
    if (ImGui::IsKeyPressed(ImGuiKey_G)) engine_->Execute("Group");
    if (ImGui::IsKeyPressed(ImGuiKey_H)) engine_->Execute("Hide");
    if (ImGui::IsKeyPressed(ImGuiKey_C) && !engine_->IsRunning()) engine_->Execute("CopyToClipboard");
    if (ImGui::IsKeyPressed(ImGuiKey_V) && !engine_->IsRunning()) engine_->Execute("Paste");
    if (ImGui::IsKeyPressed(ImGuiKey_X) && !engine_->IsRunning()) engine_->Execute("Cut");
  }
  // Rhino defaults: F1 help, F2 command history, F3 properties, F4 layers.
  if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
    if (io.KeyCtrl) panels_.command_list = !panels_.command_list;
    else if (engine_->IsRunning()) ShowHelpFor(engine_->ActiveName());
    else if (!engine_->RecentCommands().empty()) ShowHelpFor(engine_->RecentCommands().front());
    else panels_.help = !panels_.help;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F2)) panels_.command_history = !panels_.command_history;
  if (ImGui::IsKeyPressed(ImGuiKey_F3)) panels_.properties = !panels_.properties;
  if (ImGui::IsKeyPressed(ImGuiKey_F4)) panels_.layers = !panels_.layers;
  if (ImGui::IsKeyPressed(ImGuiKey_F7)) doc_.Settings().show_grid = !doc_.Settings().show_grid;
  if (ImGui::IsKeyPressed(ImGuiKey_F8)) snaps_.ortho = !snaps_.ortho;
  if (ImGui::IsKeyPressed(ImGuiKey_F9)) snaps_.grid_snap = !snaps_.grid_snap;
  if (ImGui::IsKeyPressed(ImGuiKey_F10)) show_control_points_for_selected = !show_control_points_for_selected;
  if (ImGui::IsKeyPressed(ImGuiKey_F11)) show_control_points_for_selected = false;
  if (!text_active) {
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) engine_->Execute("UndoView");
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) { if (Viewport* v = ActiveViewport()) v->GetCamera().Dolly(1.0); }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) { if (Viewport* v = ActiveViewport()) v->GetCamera().Dolly(-1.0); }
    if (Viewport* v = ActiveViewport()) {
      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) v->GetCamera().Orbit(-40, 0);
      if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) v->GetCamera().Orbit(40, 0);
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) v->GetCamera().Orbit(0, -40);
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) v->GetCamera().Orbit(0, 40);
    }
  }
}

void Application::DrawCommandLine() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float toolbar_h = panels_.toolbars ? 38.0f : 0.0f;
  const float h = ImGui::GetFrameHeightWithSpacing() * 2.0f + 8.0f;
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + toolbar_h));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::Begin("##CommandLine", nullptr, flags);

  // Last few history lines (Rhino shows a two-line history above the prompt).
  const auto& hist = engine_->History();
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.74f, 0.78f, 1.0f));
  const size_t n = hist.size();
  const std::string line1 = n >= 1 ? hist[n - 1] : "";
  ImGui::TextUnformatted(line1.c_str());
  ImGui::PopStyleColor();

  // Prompt + options + input.
  const std::string prompt = engine_->Prompt();
  ImGui::AlignTextToFramePadding();
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::TextUnformatted(prompt.c_str());
  ImGui::PopStyleColor();
  if (const std::vector<OptionSpec>* opts = engine_->CurrentOptions()) {
    for (const OptionSpec& o : *opts) {
      ImGui::SameLine();
      std::string label = o.name;
      if (!o.value.empty()) label += "=" + o.value;
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.30f, 0.48f, 0.9f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.42f, 0.66f, 1.0f));
      if (ImGui::SmallButton((label + "##opt_" + o.name).c_str())) engine_->FeedOption(o.name);
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Option: %s (or type its name)", o.name.c_str());
    }
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1);
  char buf[512];
  std::strncpy(buf, command_input_.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  ImGuiIO& io = ImGui::GetIO();
  // Typing while a viewport is hovered: route the characters here.
  // Rhino behaviour: typing anywhere goes to the command line.
  if (!focus_command_line_ && !io.WantTextInput && io.InputQueueCharacters.Size > 0) focus_command_line_ = true;
  const bool focus_requested = focus_command_line_;
  if (focus_command_line_) {
    ImGui::SetKeyboardFocusHere();
    focus_command_line_ = false;
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
      const ImWchar c = io.InputQueueCharacters[i];
      if (c >= 32 && c < 127) command_input_ += static_cast<char>(c);
    }
    io.InputQueueCharacters.resize(0);
    std::strncpy(buf, command_input_.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
  }
  const ImGuiInputTextFlags in_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
                                       ImGuiInputTextFlags_AutoSelectAll;
  struct HistoryCb {
    static int Call(ImGuiInputTextCallbackData* data) {
      Application* self = static_cast<Application*>(data->UserData);
      if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        const auto& h = self->command_line_history_;
        if (h.empty()) return 0;
        if (data->EventKey == ImGuiKey_UpArrow) {
          self->history_cursor_ = std::max(0, self->history_cursor_ < 0 ? static_cast<int>(h.size()) - 1 : self->history_cursor_ - 1);
        } else if (data->EventKey == ImGuiKey_DownArrow) {
          if (self->history_cursor_ >= 0) self->history_cursor_ = std::min(static_cast<int>(h.size()) - 1, self->history_cursor_ + 1);
        }
        if (self->history_cursor_ >= 0) {
          data->DeleteChars(0, data->BufTextLen);
          data->InsertChars(0, h[static_cast<size_t>(self->history_cursor_)].c_str());
        }
      }
      return 0;
    }
  };
  const int chars_before = io.InputQueueCharacters.Size;
  bool entered = ImGui::InputText("##cmdinput", buf, sizeof(buf), in_flags, HistoryCb::Call, this);
  command_input_ = buf;
  const bool input_active = ImGui::IsItemActive();
  // ImGui ignores an Enter that lands on the first frame after the field was
  // focused programmatically; treat it as submitted anyway.
  if (!entered && input_active && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) entered = true;
  // Focus can fail on the very first frames (layout not settled): retry.
  if (focus_requested && !input_active && focus_retries_ < 8) { focus_command_line_ = true; ++focus_retries_; }
  if (input_active) focus_retries_ = 0;
  if (std::getenv("DINO8_UI_DEBUG")) {
    std::fprintf(stderr, "[ui] frame %d active=%d entered=%d chars=%d want_text=%d buf='%s' enter_down=%d\n", ImGui::GetFrameCount(), input_active ? 1 : 0, entered ? 1 : 0, chars_before, io.WantTextInput ? 1 : 0, buf, ImGui::IsKeyDown(ImGuiKey_Enter) ? 1 : 0);
  }
  if (entered) {
    const std::string text = command_input_;
    command_input_.clear();
    if (!text.empty()) {
      command_line_history_.push_back(text);
      if (command_line_history_.size() > 200) command_line_history_.erase(command_line_history_.begin());
    }
    history_cursor_ = -1;
    engine_->Execute(text);
    focus_command_line_ = true;
  }
  // Autocomplete popup while typing a command name.
  if (input_active && !engine_->IsRunning() && !command_input_.empty() && command_input_.find(' ') == std::string::npos) {
    std::string prefix = command_input_;
    while (!prefix.empty() && (prefix.front() == '_' || prefix.front() == '-' || prefix.front() == '!')) prefix.erase(prefix.begin());
    std::vector<const CommandInfo*> matches = catalog_.WithPrefix(prefix, 12);
    if (!matches.empty() && !prefix.empty()) {
      const ImVec2 pos = ImGui::GetItemRectMin();
      const ImVec2 size = ImGui::GetItemRectSize();
      ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + size.y + 2));
      ImGui::SetNextWindowSize(ImVec2(std::min(size.x, 560.0f), 0));
      const ImGuiWindowFlags pflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_AlwaysAutoResize;
      ImGui::Begin("##autocomplete", nullptr, pflags);
      for (const CommandInfo* c : matches) {
        const RegisteredCommand* r = engine_->Find(c->name);
        const CommandStatus st = r ? r->status : CommandStatus::Planned;
        ImVec4 col = st == CommandStatus::Implemented ? ImVec4(0.85f, 0.95f, 0.85f, 1) :
                     st == CommandStatus::Partial ? ImVec4(0.95f, 0.9f, 0.7f, 1) : ImVec4(0.6f, 0.6f, 0.65f, 1);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        if (ImGui::Selectable(c->name.c_str())) {
          command_input_ = c->name;
          focus_command_line_ = true;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("  %s", c->description.c_str());
      }
      ImGui::End();
      // Tab completes to the first match.
      if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        command_input_ = matches.front()->name;
        focus_command_line_ = true;
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

float Application::StatusBarHeight() const {
  const float row = ImGui::GetFrameHeightWithSpacing();
  return (panels_.object_snaps ? 2.0f * row : row) + 4.0f;
}

void Application::DrawStatusBar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float h = StatusBarHeight();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::Begin("##StatusBar", nullptr, flags);
  // Row 1: persistent object snaps (Rhino's osnap toolbar, docked).
  if (panels_.object_snaps) {
    auto osnap = [&](const char* label, bool& value) {
      ImGui::PushStyleColor(ImGuiCol_Button, value ? ImVec4(0.22f, 0.45f, 0.75f, 1) : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
      if (ImGui::SmallButton(label)) value = !value;
      ImGui::PopStyleColor();
      ImGui::SameLine(0, 4);
    };
    ImGui::TextDisabled("Osnap");
    ImGui::SameLine(0, 8);
    osnap("End", snaps_.end); osnap("Near", snaps_.near_); osnap("Point", snaps_.point); osnap("Mid", snaps_.mid);
    osnap("Cen", snaps_.cen); osnap("Int", snaps_.int_); osnap("Perp", snaps_.perp); osnap("Tan", snaps_.tan);
    osnap("Quad", snaps_.quad); osnap("Vertex", snaps_.vertex);
    ImGui::SameLine(0, 12);
    osnap("Disable", snaps_.disable_all);
    ImGui::NewLine();
  }
  // Cursor coordinates.
  if (pending_hover_) {
    ImGui::Text("%s", FormatPoint(*pending_hover_).c_str());
  } else {
    ImGui::TextDisabled("x,y,z");
  }
  ImGui::SameLine(0, 18);
  ImGui::TextDisabled("%s", doc_.Settings().unit_system.c_str());
  ImGui::SameLine(0, 18);
  // Current layer.
  const int cur = doc_.CurrentLayer();
  const Layer& layer = doc_.Layers()[static_cast<size_t>(cur)];
  ImGui::ColorButton("##curlayer", ImVec4(layer.color.r, layer.color.g, layer.color.b, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
  ImGui::SameLine(0, 4);
  ImGui::TextUnformatted(doc_.LayerFullPath(cur).c_str());
  ImGui::SameLine(0, 18);
  // Snap toggles.
  auto toggle = [&](const char* label, bool& value, const char* tip) {
    ImGui::PushStyleColor(ImGuiCol_Button, value ? ImVec4(0.22f, 0.45f, 0.75f, 1) : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
    if (ImGui::SmallButton(label)) value = !value;
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(0, 4);
  };
  toggle("Grid Snap", snaps_.grid_snap, "Snap picked points to the grid (F9)");
  toggle("Ortho", snaps_.ortho, "Constrain to 90 degree angles (F8)");
  toggle("Planar", snaps_.planar, "Keep picks at the elevation of the previous point");
  toggle("Osnap", panels_.object_snaps, "Show the object snap toolbar");
  toggle("SmartTrack", snaps_.smart_track, "SmartTrack tracking lines");
  toggle("Gumball", gumball_enabled, "Drag selected objects with the on-screen gumball");
  ImGui::SameLine(0, 18);
  ImGui::TextDisabled("%zu objects, %zu selected", doc_.ObjectCount(), doc_.SelectedCount());
  if (doc_.Modified()) {
    ImGui::SameLine(0, 12);
    ImGui::TextDisabled("(modified)");
  }
  ImGui::End();
  ImGui::PopStyleVar(2);

  if (false) {
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h - 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("Object Snaps", &panels_.object_snaps, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
      ImGui::Checkbox("End", &snaps_.end); ImGui::SameLine();
      ImGui::Checkbox("Near", &snaps_.near_); ImGui::SameLine();
      ImGui::Checkbox("Point", &snaps_.point); ImGui::SameLine();
      ImGui::Checkbox("Mid", &snaps_.mid); ImGui::SameLine();
      ImGui::Checkbox("Cen", &snaps_.cen); ImGui::SameLine();
      ImGui::Checkbox("Int", &snaps_.int_); ImGui::SameLine();
      ImGui::Checkbox("Perp", &snaps_.perp); ImGui::SameLine();
      ImGui::Checkbox("Tan", &snaps_.tan); ImGui::SameLine();
      ImGui::Checkbox("Quad", &snaps_.quad); ImGui::SameLine();
      ImGui::Checkbox("Vertex", &snaps_.vertex); ImGui::SameLine();
      ImGui::Checkbox("Disable", &snaps_.disable_all);
    }
    ImGui::End();
  }
}

void Application::DrawFileDialog() {
  if (!file_dialog_.open) return;
  ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
  ImGui::OpenPopup(file_dialog_.title.c_str());
  if (ImGui::BeginPopupModal(file_dialog_.title.c_str(), &file_dialog_.open)) {
    char dirbuf[1024];
    std::strncpy(dirbuf, file_dialog_.directory.c_str(), sizeof(dirbuf) - 1);
    dirbuf[sizeof(dirbuf) - 1] = 0;
    ImGui::SetNextItemWidth(-90);
    if (ImGui::InputText("##dir", dirbuf, sizeof(dirbuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
      file_dialog_.directory = dirbuf;
    }
    ImGui::SameLine();
    if (ImGui::Button("Up")) {
      file_dialog_.directory = fs::path(file_dialog_.directory).parent_path().string();
      if (file_dialog_.directory.empty()) file_dialog_.directory = "/";
    }
    ImGui::BeginChild("##files", ImVec2(0, -70), ImGuiChildFlags_Borders);
    std::error_code ec;
    std::vector<fs::directory_entry> dirs, files;
    for (const auto& entry : fs::directory_iterator(file_dialog_.directory, ec)) {
      if (entry.is_directory(ec)) dirs.push_back(entry);
      else {
        const std::string ext = ToLower(entry.path().extension().string());
        bool match = file_dialog_.extensions.empty();
        for (const std::string& e : file_dialog_.extensions) match = match || e == ext;
        if (match) files.push_back(entry);
      }
    }
    auto by_name = [](const fs::directory_entry& a, const fs::directory_entry& b) {
      return ToLower(a.path().filename().string()) < ToLower(b.path().filename().string());
    };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
    for (const auto& d : dirs) {
      const std::string label = "[" + d.path().filename().string() + "]";
      if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
        if (ImGui::IsMouseDoubleClicked(0)) file_dialog_.directory = d.path().string();
      }
    }
    for (const auto& f : files) {
      const std::string name = f.path().filename().string();
      if (ImGui::Selectable(name.c_str(), file_dialog_.filename == name, ImGuiSelectableFlags_AllowDoubleClick)) {
        file_dialog_.filename = name;
        if (ImGui::IsMouseDoubleClicked(0)) {
          const std::string full = (fs::path(file_dialog_.directory) / name).string();
          file_dialog_.open = false;
          ImGui::CloseCurrentPopup();
          if (file_dialog_.callback) file_dialog_.callback(full);
        }
      }
    }
    ImGui::EndChild();
    char namebuf[512];
    std::strncpy(namebuf, file_dialog_.filename.c_str(), sizeof(namebuf) - 1);
    namebuf[sizeof(namebuf) - 1] = 0;
    ImGui::SetNextItemWidth(-200);
    if (ImGui::InputText("File name", namebuf, sizeof(namebuf))) file_dialog_.filename = namebuf;
    ImGui::SameLine();
    std::string exts;
    for (const std::string& e : file_dialog_.extensions) exts += (exts.empty() ? "" : " ") + e;
    ImGui::TextDisabled("%s", exts.c_str());
    const char* ok_label = file_dialog_.save ? "Save" : "Open";
    if (ImGui::Button(ok_label, ImVec2(120, 0)) && !file_dialog_.filename.empty()) {
      std::string full = (fs::path(file_dialog_.directory) / file_dialog_.filename).string();
      if (file_dialog_.save && !file_dialog_.extensions.empty() && fs::path(full).extension().empty()) {
        full += file_dialog_.extensions.front();
      }
      file_dialog_.open = false;
      ImGui::CloseCurrentPopup();
      if (file_dialog_.callback) file_dialog_.callback(full);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      file_dialog_.open = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void Application::DrawNotifications() {
  const double now = ImGui::GetTime();
  while (!notifications_.empty() && now - notifications_.front().time > 5.0) notifications_.pop_front();
  if (notifications_.empty()) return;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  float y = vp->WorkPos.y + vp->WorkSize.y - 70.0f;
  int i = 0;
  for (auto it = notifications_.rbegin(); it != notifications_.rend() && i < 4; ++it, ++i) {
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 20.0f, y), ImGuiCond_Always, ImVec2(1, 1));
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin(("##notify" + std::to_string(i)).c_str(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_NoInputs);
    ImGui::TextUnformatted(it->text.c_str());
    y -= ImGui::GetWindowSize().y + 6.0f;
    ImGui::End();
  }
}

void Application::DrawPanels() {
  DrawMenuBar(*this);
  if (panels_.toolbars) DrawToolbars(*this);
  if (panels_.layers) DrawLayersPanel(*this);
  if (panels_.properties) DrawPropertiesPanel(*this);
  if (panels_.command_history) DrawCommandHistoryPanel(*this);
  if (panels_.command_list) DrawCommandListPanel(*this, command_list_filter_, command_list_status_filter_);
  if (panels_.help) DrawHelpPanel(*this, help_search_);
  if (panels_.notifications) DrawNotificationsPanel(*this);
  if (panels_.named_views) DrawNamedViewsPanel(*this);
  if (panels_.notes) DrawNotesPanel(*this, notes_buffer_, sizeof(notes_buffer_));
  if (panels_.document_user_text) DrawDocumentUserTextPanel(*this);
  if (panels_.materials) DrawMaterialsPanel(*this);
  if (panels_.display) DrawDisplayPanel(*this);
  if (panels_.calculator) DrawCalculatorPanel(*this, calc_input_, calc_result_);
  if (panels_.about) DrawAboutWindow(*this);
  if (panels_.options) DrawOptionsWindow(*this);
  if (panels_.document_properties) DrawDocumentPropertiesWindow(*this);
  if (panels_.box_edit) DrawBoxEditPanel(*this);
  if (panels_.undo_multiple) DrawUndoMultipleWindow(*this, false);
  if (panels_.redo_multiple) DrawUndoMultipleWindow(*this, true);
  if (panels_.layer_state_manager) DrawLayerStateManager(*this);
  if (panels_.selection_filter) DrawSelectionFilterPanel(*this);
  if (panels_.macro_editor) DrawMacroEditor(*this);
  if (panels_.imgui_demo) ImGui::ShowDemoWindow(&panels_.imgui_demo);
}

}  // namespace dino8::app
